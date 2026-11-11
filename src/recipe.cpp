#include "model_maker/recipe.hpp"

#include "model_maker/component_codec.hpp"  // utf8FromWide / wideFromUtf8 (param adi esleme + hata)
#include "model_maker/component_checks.hpp" // applyChecksToPlan (endplate.placement kontrolu)
#include "model_maker/geometry.hpp"        // ProfileSectionFrame, WorkPlane, Vec3
#include "model_maker/end_plate.hpp"       // computeEndPlatePlacement (endplate.placement tasi)
#include "model_maker/haunch40.hpp"        // Haunch 40 taslari (C2)

#include <algorithm>
#include <cmath>
#include <cctype>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mm {

namespace {

// --- Temel vektor yardimcilari (geometry.hpp'de cross/dot/normalize yok) ----
static double vecLen(const Vec3& v) noexcept {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}
static Vec3 vecNorm(const Vec3& v) noexcept {
    const double l = vecLen(v);
    if (l <= 1e-12) return Vec3{0.0, 0.0, 0.0};
    return Vec3{v.x / l, v.y / l, v.z / l};
}

static constexpr double kPi = 3.14159265358979323846;
static constexpr double kDegToRad = kPi / 180.0;
static constexpr double kRadToDeg = 180.0 / kPi;

// --- Rol -> uye cozumu ------------------------------------------------------
// "main" -> input.main; "secondary_N" -> input.secondary[N].
static const MemberGeometry* memberByRole(const ComponentInput& input,
                                          const std::string& role) {
    if (role == "main") return &input.main;
    if (role.rfind("secondary_", 0) == 0) {
        const std::string num = role.substr(10);  // strlen("secondary_") == 10
        if (!num.empty() && num.find_first_not_of("0123456789") == std::string::npos) {
            int idx = 0;
            try { idx = std::stoi(num); } catch (...) { return nullptr; }
            if (idx >= 0 && idx < static_cast<int>(input.secondary.size()))
                return &input.secondary[static_cast<std::size_t>(idx)];
        }
    }
    return nullptr;
}

static double memberFieldValue(const ComponentInput& input, const std::string& role,
                               const std::string& field, std::wstring& err) {
    const MemberGeometry* m = memberByRole(input, role);
    if (!m) { err = wideFromUtf8("bilinmeyen uye rolu: " + role); return 0.0; }
    if (field == "h") return m->secHeight;
    if (field == "b") return m->secWidth;
    if (field == "tw") return m->secWebThickness;
    if (field == "tf") return m->secFlangeThickness;
    if (field == "length") {
        const Vec3 d = m->axisTo - m->axisFrom;
        return std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    }
    err = wideFromUtf8("bilinmeyen uye alani: " + role + "." + field);
    return 0.0;
}

// --- Degerlendirme baglami --------------------------------------------------
struct EvalCtx {
    const ComponentInput& input;
    const Recipe& recipe;
    const std::vector<CompParamSpec>& specs;
    std::map<std::string, double> params;             // UTF-8 ad -> deger
    std::map<std::string, std::string> lets;          // ad -> ifade
    std::map<std::string, double> letCache;           // ad -> deger (lazy)
    std::set<std::string> letEval;                    // dongu tespiti (devam eden)
    std::map<std::string, MemberLocalFrame> frames;   // step id -> cerceve
    int totalBolts{0};                                // §5.5 toplam bulon siniri
    HaunchParams haunchPrm;                           // (eski; MM-3d'de kullanilmiyor)
    std::shared_ptr<void> haunchState;                // (eski; MM-3d'de kullanilmiyor)
    bool haunchPrmReady{false};
    std::map<std::string, std::shared_ptr<void>> haunchFrames;  // MM-3d: step id -> frame (opak)
    std::wstring error;
};

bool evaluateExpr(EvalCtx& ctx, const std::string& text, double& out);

// --- Tokenizer --------------------------------------------------------------
struct Tok {
    enum Kind { Num, Ident, LParen, RParen, Comma, Plus, Minus, Star, Slash,
                Caret, Lt, Le, Gt, Ge, Eq, Ne, And, Or, Not, End } kind{End};
    double num{0.0};
    std::string ident;
};

static void tokenize(const std::string& s, std::vector<Tok>& out, std::wstring& err) {
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n) {
        const char c = s[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(s[i + 1])))) {
            const std::size_t start = i;
            while (i < n && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.')) ++i;
            if (i < n && (s[i] == 'e' || s[i] == 'E')) {
                const std::size_t save = i;
                ++i;
                if (i < n && (s[i] == '+' || s[i] == '-')) ++i;
                if (i < n && std::isdigit(static_cast<unsigned char>(s[i]))) {
                    while (i < n && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
                } else {
                    i = save;
                }
            }
            try {
                out.push_back(Tok{Tok::Num, std::stod(s.substr(start, i - start)), ""});
            } catch (...) {
                err = L"gecersiz sayi";
                return;
            }
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            const std::size_t start = i;
            while (i < n && (std::isalnum(static_cast<unsigned char>(s[i])) ||
                             s[i] == '_' || s[i] == '.')) ++i;
            const std::string id = s.substr(start, i - start);
            Tok::Kind k = Tok::Ident;
            if (id == "and") k = Tok::And;
            else if (id == "or") k = Tok::Or;
            else if (id == "not") k = Tok::Not;
            out.push_back(Tok{k, 0.0, id});
            continue;
        }
        switch (c) {
            case '(': out.push_back(Tok{Tok::LParen, 0.0, ""}); ++i; break;
            case ')': out.push_back(Tok{Tok::RParen, 0.0, ""}); ++i; break;
            case ',': out.push_back(Tok{Tok::Comma, 0.0, ""}); ++i; break;
            case '+': out.push_back(Tok{Tok::Plus, 0.0, ""}); ++i; break;
            case '-': out.push_back(Tok{Tok::Minus, 0.0, ""}); ++i; break;
            case '*': out.push_back(Tok{Tok::Star, 0.0, ""}); ++i; break;
            case '/': out.push_back(Tok{Tok::Slash, 0.0, ""}); ++i; break;
            case '^': out.push_back(Tok{Tok::Caret, 0.0, ""}); ++i; break;
            case '<':
                if (i + 1 < n && s[i + 1] == '=') { out.push_back(Tok{Tok::Le, 0.0, ""}); i += 2; }
                else { out.push_back(Tok{Tok::Lt, 0.0, ""}); ++i; }
                break;
            case '>':
                if (i + 1 < n && s[i + 1] == '=') { out.push_back(Tok{Tok::Ge, 0.0, ""}); i += 2; }
                else { out.push_back(Tok{Tok::Gt, 0.0, ""}); ++i; }
                break;
            case '=':
                if (i + 1 < n && s[i + 1] == '=') { out.push_back(Tok{Tok::Eq, 0.0, ""}); i += 2; }
                else { out.push_back(Tok{Tok::Eq, 0.0, ""}); ++i; }
                break;
            case '!':
                if (i + 1 < n && s[i + 1] == '=') { out.push_back(Tok{Tok::Ne, 0.0, ""}); i += 2; }
                else { out.push_back(Tok{Tok::Not, 0.0, ""}); ++i; }
                break;
            case '&':
                if (i + 1 < n && s[i + 1] == '&') { out.push_back(Tok{Tok::And, 0.0, ""}); i += 2; }
                else { err = L"ifadede gecersiz karakter: &"; return; }
                break;
            case '|':
                if (i + 1 < n && s[i + 1] == '|') { out.push_back(Tok{Tok::Or, 0.0, ""}); i += 2; }
                else { err = L"ifadede gecersiz karakter: |"; return; }
                break;
            default:
                err = L"ifadede gecersiz karakter: " + wideFromUtf8(std::string(1, c));
                return;
        }
    }
    out.push_back(Tok{Tok::End, 0.0, ""});
}

// --- Tanimlayici cozumu -----------------------------------------------------
static double letValue(EvalCtx& ctx, const std::string& name);

static double resolveIdent(EvalCtx& ctx, const std::string& name) {
    const auto dot = name.find('.');
    if (dot != std::string::npos) {
        const std::string prefix = name.substr(0, dot);
        const std::string field = name.substr(dot + 1);
        if (prefix == "param") {
            const auto it = ctx.params.find(field);
            if (it == ctx.params.end()) {
                ctx.error = wideFromUtf8("bilinmeyen parametre: " + field);
                return 0.0;
            }
            return it->second;
        }
        if (prefix == "main" || prefix.rfind("secondary_", 0) == 0) {
            return memberFieldValue(ctx.input, prefix, field, ctx.error);
        }
        ctx.error = wideFromUtf8("bilinmeyen tanimlayici: " + name);
        return 0.0;
    }
    return letValue(ctx, name);
}

static double letValue(EvalCtx& ctx, const std::string& name) {
    const auto it = ctx.lets.find(name);
    if (it == ctx.lets.end()) {
        ctx.error = wideFromUtf8("tanimsiz let: " + name);
        return 0.0;
    }
    const auto c = ctx.letCache.find(name);
    if (c != ctx.letCache.end()) return c->second;
    if (ctx.letEval.count(name)) {
        ctx.error = wideFromUtf8("dongusel let: " + name);
        return 0.0;
    }
    ctx.letEval.insert(name);
    double v = 0.0;
    const bool ok = evaluateExpr(ctx, it->second, v);
    ctx.letEval.erase(name);
    if (!ok) return 0.0;
    ctx.letCache[name] = v;
    return v;
}

// --- Recursive descent parser -----------------------------------------------
struct Parser {
    EvalCtx& ctx;
    const std::vector<Tok>& t;
    std::size_t i{0};
    int depth{0};

    struct Guard {
        int& depth;
        EvalCtx& ctx;
        bool overflow{false};
        Guard(int& d, EvalCtx& c) : depth(d), ctx(c) {
            if (++depth > 32) { overflow = true; ctx.error = L"ifade derinligi 32'yi asiyor"; }
        }
        ~Guard() { --depth; }
    };

    bool fail(const std::wstring& m) { ctx.error = m; return false; }

    bool expectRParen() {
        if (i < t.size() && t[i].kind == Tok::RParen) { ++i; return true; }
        return fail(L"eksik parantez");
    }

    bool parseOr(double& out) {
        Guard g(depth, ctx);
        if (g.overflow) return false;
        double l = 0.0;
        if (!parseAnd(l)) return false;
        while (i < t.size() && t[i].kind == Tok::Or) {
            ++i;
            double r = 0.0;
            if (!parseAnd(r)) return false;
            l = (l != 0.0 || r != 0.0) ? 1.0 : 0.0;
        }
        out = l;
        return true;
    }
    bool parseAnd(double& out) {
        Guard g(depth, ctx);
        if (g.overflow) return false;
        double l = 0.0;
        if (!parseNot(l)) return false;
        while (i < t.size() && t[i].kind == Tok::And) {
            ++i;
            double r = 0.0;
            if (!parseNot(r)) return false;
            l = (l != 0.0 && r != 0.0) ? 1.0 : 0.0;
        }
        out = l;
        return true;
    }
    bool parseNot(double& out) {
        Guard g(depth, ctx);
        if (g.overflow) return false;
        if (i < t.size() && t[i].kind == Tok::Not) {
            ++i;
            double v = 0.0;
            if (!parseNot(v)) return false;
            out = (v == 0.0) ? 1.0 : 0.0;
            return true;
        }
        return parseCmp(out);
    }
    bool parseCmp(double& out) {
        Guard g(depth, ctx);
        if (g.overflow) return false;
        double l = 0.0;
        if (!parseAdd(l)) return false;
        if (i < t.size()) {
            const Tok::Kind k = t[i].kind;
            if (k == Tok::Lt || k == Tok::Le || k == Tok::Gt || k == Tok::Ge ||
                k == Tok::Eq || k == Tok::Ne) {
                ++i;
                double r = 0.0;
                if (!parseAdd(r)) return false;
                bool c = false;
                switch (k) {
                    case Tok::Lt: c = l < r; break;
                    case Tok::Le: c = l <= r; break;
                    case Tok::Gt: c = l > r; break;
                    case Tok::Ge: c = l >= r; break;
                    case Tok::Eq: c = l == r; break;
                    case Tok::Ne: c = l != r; break;
                    default: break;
                }
                out = c ? 1.0 : 0.0;
                return true;
            }
        }
        out = l;
        return true;
    }
    bool parseAdd(double& out) {
        Guard g(depth, ctx);
        if (g.overflow) return false;
        double l = 0.0;
        if (!parseMul(l)) return false;
        while (i < t.size() && (t[i].kind == Tok::Plus || t[i].kind == Tok::Minus)) {
            const Tok::Kind k = t[i].kind;
            ++i;
            double r = 0.0;
            if (!parseMul(r)) return false;
            l = (k == Tok::Plus) ? l + r : l - r;
        }
        out = l;
        return true;
    }
    bool parseMul(double& out) {
        Guard g(depth, ctx);
        if (g.overflow) return false;
        double l = 0.0;
        if (!parseUnary(l)) return false;
        while (i < t.size() && (t[i].kind == Tok::Star || t[i].kind == Tok::Slash)) {
            const Tok::Kind k = t[i].kind;
            ++i;
            double r = 0.0;
            if (!parseUnary(r)) return false;
            l = (k == Tok::Star) ? l * r : l / r;
        }
        out = l;
        return true;
    }
    bool parseUnary(double& out) {
        Guard g(depth, ctx);
        if (g.overflow) return false;
        if (i < t.size() && t[i].kind == Tok::Minus) {
            ++i;
            double v = 0.0;
            if (!parseUnary(v)) return false;
            out = -v;
            return true;
        }
        if (i < t.size() && t[i].kind == Tok::Plus) {
            ++i;
            return parseUnary(out);
        }
        return parsePow(out);
    }
    bool parsePow(double& out) {
        Guard g(depth, ctx);
        if (g.overflow) return false;
        double base = 0.0;
        if (!parseAtom(base)) return false;
        if (i < t.size() && t[i].kind == Tok::Caret) {
            ++i;
            double ex = 0.0;
            if (!parsePow(ex)) return false;  // sag-birlesik
            out = std::pow(base, ex);
            return true;
        }
        out = base;
        return true;
    }
    bool parseAtom(double& out) {
        Guard g(depth, ctx);
        if (g.overflow) return false;
        if (i >= t.size()) return fail(L"ifade beklenmedik sekilde bitti");
        const Tok& tk = t[i];
        if (tk.kind == Tok::Num) { ++i; out = tk.num; return true; }
        if (tk.kind == Tok::LParen) {
            ++i;
            double v = 0.0;
            if (!parseOr(v)) return false;
            if (!expectRParen()) return false;
            out = v;
            return true;
        }
        if (tk.kind == Tok::Ident) {
            ++i;
            if (i < t.size() && t[i].kind == Tok::LParen) {
                return parseFunc(tk.ident, out);
            }
            out = resolveIdent(ctx, tk.ident);
            return ctx.error.empty();
        }
        return fail(L"ifadede gecersiz token");
    }
    bool parseFunc(const std::string& name, double& out) {
        // i LParen'de; tuket.
        ++i;
        if (name == "min" || name == "max") {
            if (i < t.size() && t[i].kind == Tok::RParen)
                return fail(L"min/max en az bir arguman ister");
            double acc = 0.0;
            if (!parseOr(acc)) return false;
            while (i < t.size() && t[i].kind == Tok::Comma) {
                ++i;
                double v = 0.0;
                if (!parseOr(v)) return false;
                acc = (name == "min") ? (acc < v ? acc : v) : (acc > v ? acc : v);
            }
            if (!expectRParen()) return false;
            out = acc;
            return true;
        }
        if (name == "abs" || name == "sqrt" || name == "sin" || name == "cos" || name == "tan") {
            double a = 0.0;
            if (!parseOr(a)) return false;
            if (!expectRParen()) return false;
            if (name == "abs") out = std::fabs(a);
            else if (name == "sqrt") out = std::sqrt(a);
            else {
                const double r = a * kDegToRad;
                out = (name == "sin") ? std::sin(r) : (name == "cos") ? std::cos(r) : std::tan(r);
            }
            return true;
        }
        if (name == "atan2") {
            double y = 0.0;
            if (!parseOr(y)) return false;
            if (i >= t.size() || t[i].kind != Tok::Comma) return fail(L"atan2 2 arguman ister");
            ++i;
            double x = 0.0;
            if (!parseOr(x)) return false;
            if (!expectRParen()) return false;
            out = std::atan2(y, x) * kRadToDeg;
            return true;
        }
        if (name == "clamp") {
            double v = 0.0, lo = 0.0, hi = 0.0;
            if (!parseOr(v)) return false;
            if (i >= t.size() || t[i].kind != Tok::Comma) return fail(L"clamp 3 arguman ister");
            ++i;
            if (!parseOr(lo)) return false;
            if (i >= t.size() || t[i].kind != Tok::Comma) return fail(L"clamp 3 arguman ister");
            ++i;
            if (!parseOr(hi)) return false;
            if (!expectRParen()) return false;
            out = v < lo ? lo : (v > hi ? hi : v);
            return true;
        }
        if (name == "if") {
            double c = 0.0, a = 0.0, b = 0.0;
            if (!parseOr(c)) return false;
            if (i >= t.size() || t[i].kind != Tok::Comma) return fail(L"if 3 arguman ister");
            ++i;
            if (!parseOr(a)) return false;
            if (i >= t.size() || t[i].kind != Tok::Comma) return fail(L"if 3 arguman ister");
            ++i;
            if (!parseOr(b)) return false;
            if (!expectRParen()) return false;
            out = (c != 0.0) ? a : b;
            return true;
        }
        return fail(L"bilinmeyen fonksiyon: " + wideFromUtf8(name));
    }
};

bool evaluateExpr(EvalCtx& ctx, const std::string& text, double& out) {
    std::vector<Tok> toks;
    std::wstring err;
    tokenize(text, toks, err);
    if (!err.empty()) { ctx.error = err; return false; }
    Parser p{ctx, toks};
    if (!p.parseOr(out)) return false;
    if (p.i + 1 != toks.size()) { ctx.error = L"ifadede beklenmedik kalan token"; return false; }
    return true;
}

// --- Blok arguman yardimcilari ----------------------------------------------
static bool numArg(EvalCtx& ctx, const RecipeStep& st, const char* key, double def, double& out) {
    const auto it = st.args.find(key);
    if (it == st.args.end()) { out = def; return true; }
    if (it->second.isList) { ctx.error = wideFromUtf8(std::string("arguman sayi olmali: ") + key); return false; }
    return evaluateExpr(ctx, it->second.text, out);
}
static bool strArg(const RecipeStep& st, const char* key, const std::string& def, std::string& out) {
    const auto it = st.args.find(key);
    if (it == st.args.end()) { out = def; return true; }
    out = it->second.text;
    return true;
}

// MM-5 fix (item 1): STRING arguman da `param.<ad>` ifadesi kabul eder.
// Metin/durum parametreleri (plateMaterial, grade) sayisal `param.*` gibi
// evaluateExpr'den GECMEZ; burada input.params icinden okunur. Literal metin
// eskisi gibi aynen gecerli (geriye donuk uyum).
static bool strParamArg(EvalCtx& ctx, const RecipeStep& st, const char* key,
                        const std::string& def, std::string& out) {
    const auto it = st.args.find(key);
    if (it == st.args.end()) { out = def; return true; }
    const std::string& text = it->second.text;
    static const std::string kPrefix = "param.";
    if (text.rfind(kPrefix, 0) != 0) { out = text; return true; }
    const std::wstring name = wideFromUtf8(text.substr(kPrefix.size()));
    for (const auto& p : ctx.input.params) {
        if (p.name != name) continue;
        if (const auto* s = std::get_if<std::wstring>(&p.value)) { out = utf8FromWide(*s); return true; }
        // Sessiz yanlis yok: parametre metin degilse ACIK hata.
        ctx.error = std::wstring(L"arguman metin parametresi metin degil: ") + name +
                    L" (arguman: " + wideFromUtf8(key) + L")";
        return false;
    }
    // Girdi parametresi YOKSA sema VARSAYILANI (golden girdiler parametresiz
    // gelir; generator da ayni varsayilani kullanir).
    for (const auto& spec : ctx.specs) {
        if (spec.name != name) continue;
        if (!spec.defString.empty()) { out = utf8FromWide(spec.defString); return true; }
        if (!spec.choices.empty()) { out = utf8FromWide(spec.choices.front()); return true; }
        ctx.error = std::wstring(L"metin parametresinin varsayilani yok: ") + name +
                    L" (arguman: " + wideFromUtf8(key) + L")";
        return false;
    }
    ctx.error = std::wstring(L"tanimsiz metin parametresi: ") + name +
                L" (arguman: " + wideFromUtf8(key) + L")";
    return false;
}
static bool boolArg(EvalCtx& ctx, const RecipeStep& st, const char* key, bool def, bool& out) {
    const auto it = st.args.find(key);
    if (it == st.args.end()) { out = def; return true; }
    const std::string& s = it->second.text;
    if (s == "true" || s == "1" || s == "yes" || s == "on") { out = true; return true; }
    if (s == "false" || s == "0" || s == "no" || s == "off") { out = false; return true; }
    // MM-5 fix (item 1): `param.<ad>` ifadesi (Boolean/Sayisal parametre) kabul edilir.
    if (s.rfind("param.", 0) == 0) {
        double v = 0.0;
        if (!evaluateExpr(ctx, s, v)) return false;
        out = (v != 0.0);
        return true;
    }
    ctx.error = wideFromUtf8(std::string("arguman mantiksal olmali: ") + key);
    return false;
}
static bool strListArg(EvalCtx& ctx, const RecipeStep& st, const char* key, std::vector<std::string>& out) {
    const auto it = st.args.find(key);
    if (it == st.args.end()) { out.clear(); return true; }
    if (!it->second.isList) { out.assign(1, it->second.text); return true; }
    out.clear();
    for (const auto& item : it->second.items) {
        if (item.isList) { ctx.error = wideFromUtf8(std::string("liste icinde liste gecersiz: ") + key); return false; }
        out.push_back(item.text);
    }
    return true;
}
static MemberLocalFrame* frameArg(EvalCtx& ctx, const RecipeStep& st, const char* key) {
    std::string id;
    strArg(st, key, "", id);
    const auto it = ctx.frames.find(id);
    if (it == ctx.frames.end()) { ctx.error = wideFromUtf8("tanimsiz cerceve: " + id); return nullptr; }
    return &it->second;
}

// --- v0 TASLARI -------------------------------------------------------------
static bool blockFrameMemberEnd(EvalCtx& ctx, const RecipeStep& st, GenerationPlan&) {
    std::string role, end;
    strArg(st, "member", "main", role);
    strArg(st, "end", "end", end);
    const MemberGeometry* m = memberByRole(ctx.input, role);
    if (!m) { ctx.error = wideFromUtf8("frame.member_end: bilinmeyen uye rolu '" + role + "'"); return false; }
    const Vec3 dir = m->axisTo - m->axisFrom;
    if (vecLen(dir) <= 1e-9) { ctx.error = wideFromUtf8("frame.member_end: uzunlugu sifir uye '" + role + "'"); return false; }
    // 1.16.0 (contract istegi d / HR-12): `end` DUNYA Z'ye gore de secilebilir.
    //  - start|from : axisFrom ucu
    //  - end|to     : axisTo ucu
    //  - lower      : dunya Z'si KUCUK olan uc  (yukaridan asagi cizilmis kolonda TABAN)
    //  - upper      : dunya Z'si BUYUK olan uc
    bool atStart = false;
    if (end == "start" || end == "from") {
        atStart = true;
    } else if (end == "end" || end == "to") {
        atStart = false;
    } else if (end == "lower" || end == "upper") {
        const bool fromIsLower = m->axisFrom.z <= m->axisTo.z;
        atStart = (end == "lower") ? fromIsLower : !fromIsLower;
    } else {
        ctx.error = wideFromUtf8("frame.member_end: gecersiz end '" + end +
                                "' (start|from|end|to|lower|upper)");
        return false;
    }
    MemberLocalFrame f;
    f.origin = atStart ? m->axisFrom : m->axisTo;
    // CERCEVE = uyenin KESIT cercevesi: profileSectionFrame(donus YOK) yerine
    // memberSectionFrame (refNormal + profileRotation). Donuk kolonda (or. 90°)
    // genislik/yukseklik yer degistirir; eski kod donusu yok sayip taban
    // levhasini/bulonlari yanlis yone koyuyordu.
    const ProfileSectionFrame pf = memberSectionFrame(*m);
    f.u = pf.width;
    f.v = pf.height;
    f.w = pf.axis;
    ctx.frames[st.id] = f;
    return true;
}

static bool blockFrameOffset(EvalCtx& ctx, const RecipeStep& st, GenerationPlan&) {
    MemberLocalFrame* f = frameArg(ctx, st, "frame");
    if (!f) return false;
    double du = 0.0, dv = 0.0, dw = 0.0;
    if (!numArg(ctx, st, "du", 0.0, du)) return false;
    if (!numArg(ctx, st, "dv", 0.0, dv)) return false;
    if (!numArg(ctx, st, "dw", 0.0, dw)) return false;
    MemberLocalFrame out = *f;
    out.origin = f->origin + f->u * du + f->v * dv + f->w * dw;
    ctx.frames[st.id] = out;
    return true;
}

// HR-1 — DONDUREN TAŞ: cerceveyi kendi u/v/w ekseni etrafinda `deg` (derece) kadar
// dondurur. u/v/w ORTONORMAL kalir; el yonu KORUNUR (sol elli cercevede u x v = -w
// iliskisi aynen kalir — w'yi u x v'den YENIDEN HESAPLAMAYIZ).
static bool blockFrameRotate(EvalCtx& ctx, const RecipeStep& st, GenerationPlan&) {
    MemberLocalFrame* f = frameArg(ctx, st, "frame");
    if (!f) return false;
    std::string about;
    strArg(st, "about", "w", about);
    double deg = 0.0;
    if (!numArg(ctx, st, "deg", 0.0, deg)) return false;
    if (about != "u" && about != "v" && about != "w") {
        ctx.error = wideFromUtf8("frame.rotate: about 'u', 'v' veya 'w' olmali (verilen: " +
                                 about + ")");
        return false;
    }
    const double rad = deg * kDegToRad;
    const double c = std::cos(rad), s = std::sin(rad);
    MemberLocalFrame out = *f;
    if (about == "u") {
        out.v = f->v * c + f->w * s;
        out.w = f->w * c - f->v * s;
    } else if (about == "v") {
        out.w = f->w * c + f->u * s;
        out.u = f->u * c - f->w * s;
    } else {
        out.u = f->u * c + f->v * s;
        out.v = f->v * c - f->u * s;
    }
    // Sayisal ortonormalizasyon: u birim, v u'ya dik, w u ve v'ye dik. Handedness'e
    // DOKUNMAYIZ: w'yi carpimla yeniden uretmek sol elli cerceveyi saga cevirirdi.
    const auto normalizeInPlace = [](Vec3& v) {
        const double len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (len > 1e-12) { v.x /= len; v.y /= len; v.z /= len; }
    };
    normalizeInPlace(out.u);
    const double vu = out.v.x * out.u.x + out.v.y * out.u.y + out.v.z * out.u.z;
    out.v = out.v - out.u * vu;
    normalizeInPlace(out.v);
    const double wu = out.w.x * out.u.x + out.w.y * out.u.y + out.w.z * out.u.z;
    const double wv = out.w.x * out.v.x + out.w.y * out.v.y + out.w.z * out.v.z;
    out.w = out.w - out.u * wu - out.v * wv;
    normalizeInPlace(out.w);
    ctx.frames[st.id] = out;
    return true;
}

// P — YUZ CERCEVESI (frame.member_face): `member` uyesinin `toward` uyesine
// BAKAN yuzunun cercevesini uretir. Kanonik MemberGeometry ve digest
// kurallarina DOKUNMAZ; yalniz YENI bir cercevedir.
//   w  = yuzun DIS normali (toward uyesine bakan); u/v kesit genislik/yukseklik
//        + uye ekseninden deterministik.
//   origin = secilen YUZ duzleminde: kesit merkez ofseti (sectionCenterOffset)
//            + yuz ofseti (flans dis yuzu h/2, govde yuzu tw/2), toward ucunun
//            eksene izdusumu hizasinda (mid-height baglantilar dahil).
// Yuz kumesi YALNIZ baglanti yuzleridir: flans dis yuzu (±height @ h/2) ve
// govde yuzu (±width @ tw/2). Flans KENARI (b/2) bir baglanti yuzu degildir.
// `toward` eksenle ayni dogrultuda (izdusum sifir) veya iki yuze esit (~45°)
// ise ACIK ret (fail-closed).
static bool computeMemberFace(EvalCtx& ctx, const std::string& memberRole,
                              const std::string& towardRole, MemberLocalFrame& out) {
    if (memberRole == towardRole) {
        ctx.error = wideFromUtf8("frame.member_face: member ve toward ayni rol: " + memberRole);
        return false;
    }
    const MemberGeometry* m = memberByRole(ctx.input, memberRole);
    if (!m) {
        ctx.error = wideFromUtf8("frame.member_face: bilinmeyen uye rolu '" + memberRole + "'");
        return false;
    }
    const MemberGeometry* t = memberByRole(ctx.input, towardRole);
    if (!t) {
        ctx.error = wideFromUtf8("frame.member_face: bilinmeyen toward rolu '" + towardRole + "'");
        return false;
    }
    if (!memberGeometryUsable(*m)) {
        ctx.error = wideFromUtf8("frame.member_face: uye geometrisi gecersiz '" + memberRole + "'");
        return false;
    }
    const Vec3 axisRaw = m->axisTo - m->axisFrom;
    const Vec3 axis = vecNorm(axisRaw);
    if (vecLen(axisRaw) <= 1e-9) {
        ctx.error = wideFromUtf8("frame.member_face: uzunlugu sifir uye '" + memberRole + "'");
        return false;
    }
    // "toward" yonu: baglanti ucundan (axisTo) uyenin GOVDESINE (axisFrom) dogru;
    // eksene dik bileseni hangi yuzun bakacagini soyler.
    const Vec3 towardRaw = t->axisFrom - t->axisTo;
    const Vec3 towardDir = vecNorm(towardRaw);
    if (vecLen(towardRaw) <= 1e-9) {
        ctx.error = wideFromUtf8("frame.member_face: uzunlugu sifir toward uyesi '" + towardRole + "'");
        return false;
    }
    const double tAlong = towardDir.x * axis.x + towardDir.y * axis.y + towardDir.z * axis.z;
    Vec3 d = towardDir - axis * tAlong;   // eksene dik
    const double dLen = vecLen(d);
    if (dLen <= 1e-9) {
        ctx.error = wideFromUtf8("frame.member_face: toward uyesi '" + towardRole +
                                 "' uye ekseniyle ayni dogrultuda, yuz secilemez");
        return false;
    }
    d = d * (1.0 / dLen);
    const ProfileSectionFrame sf = memberSectionFrame(*m);
    const double dotU = d.x * sf.width.x + d.y * sf.width.y + d.z * sf.width.z;
    const double dotV = d.x * sf.height.x + d.y * sf.height.y + d.z * sf.height.z;
    const double au = std::fabs(dotU), av = std::fabs(dotV);
    const double dom = std::max(au, av);
    if (dom <= 1e-9) {
        ctx.error = wideFromUtf8("frame.member_face: toward yonu belirsiz");
        return false;
    }
    if ((dom - std::min(au, av)) <= 1e-6 * dom) {
        ctx.error = wideFromUtf8("frame.member_face: toward yonu iki yuze esit (belirsiz)");
        return false;
    }
    Vec3 normal;
    double offset = 0.0;
    if (au > av) {
        // GOVDE yuzu: normal ±genislik, ofset tw/2. u = yukseklik, v = eksen.
        normal = sf.width * (dotU >= 0.0 ? 1.0 : -1.0);
        offset = m->secWebThickness * 0.5;
        out.u = sf.height;
        out.v = axis;
    } else {
        // FLANS (baslik) yuzu: normal ±yukseklik, ofset h/2. u = genislik, v = eksen.
        normal = sf.height * (dotV >= 0.0 ? 1.0 : -1.0);
        offset = m->secHeight * 0.5;
        out.u = sf.width;
        out.v = axis;
    }
    out.w = normal;
    // origin: toward ucunun eksene izdusumu + kesit merkez ofseti + yuz ofseti.
    const Vec3 rel = t->axisTo - m->axisFrom;
    const double along = rel.x * axis.x + rel.y * axis.y + rel.z * axis.z;
    const Vec3 axialPoint = m->axisFrom + axis * along;
    const Vec3 center = axialPoint + sf.height * m->sectionCenterOffset;
    out.origin = center + normal * offset;
    return true;
}

static bool blockFrameMemberFace(EvalCtx& ctx, const RecipeStep& st, GenerationPlan&) {
    std::string memberRole, towardRole;
    strArg(st, "member", "main", memberRole);
    strArg(st, "toward", "secondary_0", towardRole);
    MemberLocalFrame f;
    if (!computeMemberFace(ctx, memberRole, towardRole, f)) return false;
    ctx.frames[st.id] = f;
    return true;
}

// KONTUR TESTLERI (plate.polygon icin): ic ice kesisme (self-intersection) ve disbukeylik.
// Studio karari (ST-3e): icbukey ya da kendini kesen kontur REDDEDILIR.
namespace {
struct P2 { double u, v; };
double cross2(const P2& a, const P2& b, const P2& c) {
    return (b.u - a.u) * (c.v - a.v) - (b.v - a.v) * (c.u - a.u);
}
bool segmentsIntersect(const P2& a, const P2& b, const P2& c, const P2& d) {
    const double d1 = cross2(c, d, a), d2 = cross2(c, d, b);
    const double d3 = cross2(a, b, c), d4 = cross2(a, b, d);
    return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
           ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
}
bool isSimpleConvexPolygon(const std::vector<P2>& pts) {
    const std::size_t n = pts.size();
    if (n < 3) return false;
    int sign = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double cr = cross2(pts[i], pts[(i + 1) % n], pts[(i + 2) % n]);
        if (std::fabs(cr) <= 1e-9) continue;  // collinear: yok say
        const int s = cr > 0 ? 1 : -1;
        if (sign == 0) sign = s;
        else if (s != sign) return false;     // icbukey
    }
    if (sign == 0) return false;              // dejenere (alan yok)
    // Komsu olmayan kenarlar kesisiyor mu?
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = i + 2; j < n; ++j) {
            if (i == 0 && j == n - 1) continue;  // komsu kenarlar
            if (segmentsIntersect(pts[i], pts[(i + 1) % n], pts[j], pts[(j + 1) % n]))
                return false;
        }
    return true;
}
}  // namespace

static bool blockPlateRect(EvalCtx& ctx, const RecipeStep& st, GenerationPlan& plan) {
    MemberLocalFrame* f = frameArg(ctx, st, "frame");
    if (!f) return false;
    double w = 0.0, h = 0.0, t = 0.0;
    if (!numArg(ctx, st, "w", 0.0, w)) return false;
    if (!numArg(ctx, st, "h", 0.0, h)) return false;
    if (!numArg(ctx, st, "t", 0.0, t)) return false;
    std::string name, material, finish, posNo;
    // STRING alanlar da `param.<ad>` kabul eder (MM-5 strParamArg). eskiden
    // strArg ile okunuyordu: recipe'de "param.plateMaterial" YAZILDIYSA levha
    // malzemesi literal "param.plateMaterial" oluyordu — sessiz yanlis.
    if (!strParamArg(ctx, st, "name", "", name)) return false;
    if (!strParamArg(ctx, st, "material", "", material)) return false;
    if (!strParamArg(ctx, st, "finish", "", finish)) return false;
    if (!strParamArg(ctx, st, "posNo", "", posNo)) return false;
    // HR-3 — KOSE PAHLARI: `chamfers` listesi, her oge "corner:type:h:v"
    // (sayisal alanlar IFADE olabilir, or. "2:1:t*2:30"). Bacaklar KIRPILMAZ:
    // kenari asan pah ACIK Turkce hatayla REDDEDILIR (Studio ST-3e karari).
    std::vector<std::string> chamferSpecs;
    if (!strListArg(ctx, st, "chamfers", chamferSpecs)) return false;
    std::vector<PlateSpec::Chamfer> chamfers;
    if (!chamferSpecs.empty()) {
        if (w <= 0.0 || h <= 0.0) {
            ctx.error = L"plate.rect: pah icin w/h pozitif olmali";
            return false;
        }
        std::vector<int> cornerLegH(4, -1), cornerLegV(4, -1);
        for (const auto& item : chamferSpecs) {
            std::vector<std::string> parts;
            std::size_t start = 0;
            while (true) {
                const std::size_t pos = item.find(':', start);
                parts.push_back(item.substr(start, pos == std::string::npos ? pos : pos - start));
                if (pos == std::string::npos) break;
                start = pos + 1;
            }
            if (parts.size() != 4) {
                ctx.error = wideFromUtf8("plate.rect: pah bicimi 'corner:type:h:v' olmali: " + item);
                return false;
            }
            double cv = 0.0;
            if (!evaluateExpr(ctx, parts[0], cv)) return false;
            const int corner = static_cast<int>(std::llround(cv));
            if (corner < 0 || corner > 3) {
                ctx.error = wideFromUtf8("plate.rect: pah corner 0..3 olmali: " + item);
                return false;
            }
            double tv = 0.0;
            if (!evaluateExpr(ctx, parts[1], tv)) return false;
            const int type = static_cast<int>(std::llround(tv));
            if (type < 1 || type > 3) {
                ctx.error = wideFromUtf8("plate.rect: pah type 1..3 olmali: " + item);
                return false;
            }
            double legH = 0.0, legV = 0.0;
            if (!evaluateExpr(ctx, parts[2], legH)) return false;
            if (!evaluateExpr(ctx, parts[3], legV)) return false;
            if (!(legH > 0.0) || !(legV > 0.0)) {
                ctx.error = wideFromUtf8("plate.rect: pah bacaklari pozitif olmali: " + item);
                return false;
            }
            if (cornerLegH[corner] >= 0) {
                ctx.error = wideFromUtf8("plate.rect: ayni koseye iki pah: " + item);
                return false;
            }
            if (legH >= w - 1e-9 || legV >= h - 1e-9) {
                std::wstring msg = L"plate.rect: pah kenari asiyor (kose " +
                                   std::to_wstring(corner) + L", h=" + std::to_wstring(legH) +
                                   L" w=" + std::to_wstring(w) + L", v=" + std::to_wstring(legV) +
                                   L" h=" + std::to_wstring(h) + L"); bacaklar kirpilmaz.";
                ctx.error = msg;
                return false;
            }
            cornerLegH[corner] = static_cast<int>(std::llround(legH * 1000.0));
            cornerLegV[corner] = static_cast<int>(std::llround(legV * 1000.0));
            PlateSpec::Chamfer ch;
            ch.corner = corner;
            ch.type = type;
            ch.h = legH;
            ch.v = legV;
            chamfers.push_back(ch);
        }
        // Ayni kenari paylasan komsu pahlar cakismamali (0-1 alt u kenari,
        // 2-3 ust u kenari, 0-3 sol v kenari, 1-2 sag v kenari).
        const auto sumExceeds = [](int a, int b, double limit) {
            if (a < 0 || b < 0) return false;
            return (static_cast<double>(a) + static_cast<double>(b)) / 1000.0 > limit - 1e-9;
        };
        if (sumExceeds(cornerLegH[0], cornerLegH[1], w) ||
            sumExceeds(cornerLegH[3], cornerLegH[2], w) ||
            sumExceeds(cornerLegV[0], cornerLegV[3], h) ||
            sumExceeds(cornerLegV[1], cornerLegV[2], h)) {
            ctx.error = L"plate.rect: komsu pahlarin bacaklari ayni kenarda cakisiyor";
            return false;
        }
    }
    PlateSpec p;
    p.center = f->origin;
    p.u = f->u;
    p.v = f->v;
    p.axis = f->w;
    p.width = w;
    p.height = h;
    p.thickness = t;
    p.name = wideFromUtf8(name);
    p.material = wideFromUtf8(material);
    p.finish = wideFromUtf8(finish);
    p.posNo = wideFromUtf8(posNo);
    p.chamfers = std::move(chamfers);
    plan.plates.push_back(p);
    plan.plateKeys.push_back(st.role);
    return true;
}

// HR-3 — PLATE.POLYGON: serbest kontur levhasi (ucgen/kirpik kose rijitlestirici).
// `outline`: "u,v" ikilileri listesi, IFADE olabilir (or. ["0,0","w,0","0,h"]).
// Kontur levha MERKEZINE gore yerel (u,v) koordinatidir. En az 3 nokta; icbukey ya da
// kendini kesen kontur REDDEDILIR (Studio ST-3e karari).
static bool blockPlatePolygon(EvalCtx& ctx, const RecipeStep& st, GenerationPlan& plan) {
    MemberLocalFrame* f = frameArg(ctx, st, "frame");
    if (!f) return false;
    double t = 0.0;
    if (!numArg(ctx, st, "t", 0.0, t)) return false;
    if (!(t > 0.0)) {
        ctx.error = L"plate.polygon: kalinlik (t) pozitif olmali";
        return false;
    }
    std::string name, material, finish, posNo;
    if (!strParamArg(ctx, st, "name", "", name)) return false;
    if (!strParamArg(ctx, st, "material", "", material)) return false;
    if (!strParamArg(ctx, st, "finish", "", finish)) return false;
    if (!strParamArg(ctx, st, "posNo", "", posNo)) return false;
    std::vector<std::string> points;
    if (!strListArg(ctx, st, "outline", points)) return false;
    if (points.size() < 3) {
        ctx.error = L"plate.polygon: kontur en az 3 nokta olmali";
        return false;
    }
    std::vector<P2> pts;
    pts.reserve(points.size());
    for (const auto& item : points) {
        const std::size_t comma = item.find(',');
        if (comma == std::string::npos) {
            ctx.error = wideFromUtf8("plate.polygon: nokta bicimi 'u,v' olmali: " + item);
            return false;
        }
        double pu = 0.0, pv = 0.0;
        if (!evaluateExpr(ctx, item.substr(0, comma), pu)) return false;
        if (!evaluateExpr(ctx, item.substr(comma + 1), pv)) return false;
        pts.push_back({pu, pv});
    }
    if (!isSimpleConvexPolygon(pts)) {
        ctx.error = L"plate.polygon: kontur icbukey ya da kendini kesiyor (disbukey ve basit olmali)";
        return false;
    }
    PlateSpec p;
    p.center = f->origin;
    p.u = f->u;
    p.v = f->v;
    p.axis = f->w;
    p.thickness = t;
    p.outline.clear();
    p.outline.reserve(pts.size());
    for (const auto& q : pts) p.outline.emplace_back(q.u, q.v);
    p.name = wideFromUtf8(name);
    p.material = wideFromUtf8(material);
    p.finish = wideFromUtf8(finish);
    p.posNo = wideFromUtf8(posNo);
    plan.plates.push_back(p);
    plan.plateKeys.push_back(st.role);
    return true;
}

static bool blockBoltsGrid(EvalCtx& ctx, const RecipeStep& st, GenerationPlan& plan) {
    MemberLocalFrame* f = frameArg(ctx, st, "frame");
    if (!f) return false;
    double nx = 1.0, ny = 1.0, px = 0.0, py = 0.0, d = 20.0, hole = 22.0;
    if (!numArg(ctx, st, "nx", 1.0, nx)) return false;
    if (!numArg(ctx, st, "ny", 1.0, ny)) return false;
    if (!numArg(ctx, st, "px", 0.0, px)) return false;
    if (!numArg(ctx, st, "py", 0.0, py)) return false;
    if (!numArg(ctx, st, "d", 20.0, d)) return false;
    if (!numArg(ctx, st, "hole", 22.0, hole)) return false;
    // HR-4 / I — ANKRAJ / GRIP: gripLength ve holeDepth recipe'den verilebilir.
    // Verilmezse varsayilan grip = through uyelerinin delinen kalinliklari toplami
    // (bulon ekseni kesit YUKSEKLIGINE hizaliysa flans tf, GENISLIGINE hizaliysa
    // govde tw); holeDepth varsayilani = grip + 2 (end plate kurali). Studio grip
    // TURETMEZ — deger her zaman plandan gelir.
    const bool hasGrip = st.args.count("grip") != 0;
    const bool hasHoleDepth = st.args.count("holeDepth") != 0;
    double grip = 0.0, holeDepth = 0.0;
    if (hasGrip && !numArg(ctx, st, "grip", 0.0, grip)) return false;
    if (hasHoleDepth && !numArg(ctx, st, "holeDepth", 0.0, holeDepth)) return false;
    if (grip < 0.0 || holeDepth < 0.0) {
        ctx.error = L"bolts.grid: grip/holeDepth negatif olamaz";
        return false;
    }
    std::vector<std::string> through;
    if (!strListArg(ctx, st, "through", through)) return false;
    std::string grade;
    if (!strParamArg(ctx, st, "grade", "", grade)) return false;
    const int cx = static_cast<int>(nx);
    const int cy = static_cast<int>(ny);
    if (cx < 1 || cy < 1) { ctx.error = L"bolts.grid: nx/ny en az 1 olmali"; return false; }
    if (cx * cy > 64) { ctx.error = L"bolts.grid: grup basina en fazla 64 bulon"; return false; }
    if (ctx.totalBolts + cx * cy > 512) { ctx.error = L"bolts.grid: toplam bulon 512'yi asiyor"; return false; }
    BoltGroup b;
    b.origin = f->origin;
    b.axis = f->w;
    b.xDir = f->u;
    b.countX = cx;
    b.countY = cy;
    b.spacingX = px;
    b.spacingY = py;
    b.params.nominalDiameter = d;
    b.params.holeDiameter = hole;
    if (!grade.empty()) b.params.grade = grade;
    double derivedGrip = 0.0;
    for (const auto& r : through) {
        const MemberGeometry* m = memberByRole(ctx.input, r);
        if (!m) { ctx.error = wideFromUtf8("bolts.grid: bilinmeyen uye rolu '" + r + "'"); return false; }
        b.parts.push_back(m->id);
        b.partRoles.push_back(r);  // MM-2b: rol anahtari (digest kimlik-bagimsiz)
        if (!hasGrip) {
            // Delinen kalinlik: bulon ekseninin kesit cercevesine izdusumu.
            // YUKSEKLIK (v) -> flans tf; GENISLIK (u) -> govde tw.
            const ProfileSectionFrame pf = memberSectionFrame(*m);
            const double aU = std::fabs(f->w.x * pf.width.x + f->w.y * pf.width.y +
                                        f->w.z * pf.width.z);
            const double aV = std::fabs(f->w.x * pf.height.x + f->w.y * pf.height.y +
                                        f->w.z * pf.height.z);
            derivedGrip += (aV >= aU) ? m->secFlangeThickness : m->secWebThickness;
        }
    }
    if (!hasGrip) grip = derivedGrip;
    // Varsayilan holeDepth = grip + 2 (end plate kurali); grip turetilmediyse
    // (bos through / profilsiz uye) sentinel 0 korunur -> materializer bbox'a duser.
    if (!hasHoleDepth) holeDepth = (grip > 0.0) ? grip + 2.0 : 0.0;
    b.gripLength = grip;
    b.holeDepth = holeDepth;
    ctx.totalBolts += cx * cy;
    plan.boltGroups.push_back(b);
    plan.boltGroupKeys.push_back(st.role);
    return true;
}

static bool blockWeldFillet(EvalCtx& ctx, const RecipeStep& st, GenerationPlan& plan) {
    std::string a, b;
    strArg(st, "roleA", "", a);
    strArg(st, "roleB", "", b);
    double size = 5.0;
    if (!numArg(ctx, st, "size", 5.0, size)) return false;
    bool dbl = false;
    if (!boolArg(ctx, st, "double", false, dbl)) return false;
    if (st.args.find("double") == st.args.end()) {
        if (!boolArg(ctx, st, "doubleSided", false, dbl)) return false;
    }
    WeldSpec w;
    const MemberGeometry* ma = memberByRole(ctx.input, a);
    if (ma) w.partA = ma->id;
    w.partARole = a;  // MM-2b: rol anahtari HER ZAMAN isaretlenir (digest kimlik-bagimsiz)
    const MemberGeometry* mb = memberByRole(ctx.input, b);
    if (mb) w.partB = mb->id;
    w.partBRole = b;
    w.size = size;
    w.doubleSided = dbl;
    plan.welds.push_back(w);
    plan.weldKeys.push_back(st.role);
    return true;
}

static bool blockCutPlane(EvalCtx& ctx, const RecipeStep& st, GenerationPlan& plan) {
    std::string target, keep;
    strArg(st, "target", "", target);
    strArg(st, "keep", "positive", keep);
    const MemberGeometry* m = memberByRole(ctx.input, target);
    if (!m) { ctx.error = wideFromUtf8("cut.plane: bilinmeyen hedef uye '" + target + "'"); return false; }
    MemberLocalFrame* f = frameArg(ctx, st, "frame");
    if (!f) return false;
    CutModifier c;
    c.kind = CutKind::PlaneCut;
    c.target = m->id;
    c.targetRole = target;  // MM-2b: rol anahtari (digest kimlik-bagimsiz)
    c.planeOrigin = f->origin;
    c.planeNormal = f->w;
    c.keepPositive = (keep == "positive" || keep == "1" || keep == "true");
    c.hidden = true;  // component ic motor elemani — cizilmez/secilmez
    plan.cuts.push_back(c);
    plan.cutKeys.push_back(st.role);
    return true;
}

// endplate.placement: mevcut computeEndPlatePlacement mantigini SARAR (yeniden
// yazmaz). Parametreleri generateEndPlate ile BIREBIR ayni sekilde input.params'tan
// okur; boylece plan ciktisi (ve planDigest) generateEndPlate ile ozdes kalir.
// MM-BUG-01: profil DONUSU (profileRotation) artik computeEndPlatePlacementRotated
// uzerinden uygulanir — 90° donuk kiriste levha u/v'si gercek kesit yonune oturur.
// Iki nesne uretir: levha (rolePlate, varsayilan end_plate) + bulon grubu
// (roleBolts, varsayilan beam_bolts).
static bool blockEndPlatePlacement(EvalCtx& ctx, const RecipeStep& st, GenerationPlan& plan) {
    std::string mainRole, beamRole, rolePlate, roleBolts;
    strArg(st, "main", "main", mainRole);
    strArg(st, "beam", "secondary_0", beamRole);
    strArg(st, "rolePlate", "end_plate", rolePlate);
    strArg(st, "roleBolts", "beam_bolts", roleBolts);
    const MemberGeometry* col = memberByRole(ctx.input, mainRole);
    const MemberGeometry* beam = memberByRole(ctx.input, beamRole);
    if (!col) { ctx.error = wideFromUtf8("endplate.placement: bilinmeyen ana uye '" + mainRole + "'"); return false; }
    if (!beam) { ctx.error = wideFromUtf8("endplate.placement: bilinmeyen kiris uyesi '" + beamRole + "'"); return false; }

    EndPlateParams ep = endPlateFromParams(ctx.input.params);

    EndPlatePlacement placement;
    EntityId mainPartId = kInvalidEntityId;
    std::wstring perr;
    if (!endPlatePlacementFromInput(ctx.input, ep, placement, mainPartId, perr)) {
        ctx.error = perr;
        return false;
    }

    PlateSpec plate;
    plate.center = placement.plateCenter;
    plate.u = placement.u;
    plate.v = placement.v;
    plate.axis = placement.axis;
    plate.width = placement.plateWidth;
    plate.height = placement.plateHeight;
    plate.thickness = placement.plateThickness;
    plate.name = L"Alin Levhasi";
    if (!ep.plateMaterial.empty()) plate.material.assign(ep.plateMaterial.begin(), ep.plateMaterial.end());
    if (!ep.plateName.empty()) plate.name.assign(ep.plateName.begin(), ep.plateName.end());
    if (!ep.plateFinish.empty()) plate.finish.assign(ep.plateFinish.begin(), ep.plateFinish.end());
    plan.plates.push_back(plate);
    plan.plateKeys.push_back(rolePlate);

    BoltGroup bolts = placement.bolts;
    bolts.parts = {mainPartId};
    bolts.partRoles = {inputRoleFor(ctx.input, mainPartId)};
    plan.boltGroups.push_back(bolts);
    plan.boltGroupKeys.push_back(roleBolts);

    // Item 4: kiris kesimi generator ile AYNI kaynaktan (endPlateBeamCut).
    CutModifier beamCut;
    endPlateBeamCut(placement, beam->id, beamCut);
    beamCut.targetRole = inputRoleFor(ctx.input, beam->id);
    plan.cuts.push_back(beamCut);
    plan.cutKeys.push_back("cut_beam_end");

    // MM-7 (item 1): generator ile AYNI kontrol (gen == rec). Ihlalde ctx.error
    // kurulur; runRecipe plan.ok=false yapar.
    applyChecksToPlan(plan, ctx.input);

    // MM-7 duzeltme: varsayilan (otomatik) yerlesim satir sayisini azalttiginda
    // bilgi/uyari check'i (generator ile AYNI — gen == rec, digest'e girmez).
    if (placement.boltRowsReduced) {
        ComponentCheck c;
        c.id = "bolt_rows_reduced";
        c.pass = true;
        c.warning = true;
        c.value = static_cast<double>(placement.boltRowsActual);
        c.limit = static_cast<double>(placement.boltRowsRequested);
        c.message = L"Bulon satiri " + std::to_wstring(placement.boltRowsRequested) +
                    L" -> " + std::to_wstring(placement.boltRowsActual) +
                    L" (kesit yuksekligi yetersiz)";
        plan.checks.push_back(c);
    }

    if (!plan.ok) {
        ctx.error = plan.error;
        return false;
    }
    return true;
}

// --- Haunch 40 taşları (MM-3d) ----------------------------------------------
// Her taş kendi parametrelerini args'tan alır; frame ADIYLA ("frame": "id")
// geçirilir; "role" üretilen roleKey'i görünür kılar. Gizli ortak durum yok:
// bir taş yalnız adı geçen frame'i + kendi args'ını okur.
static std::shared_ptr<void> haunchFrameArg(EvalCtx& ctx, const RecipeStep& st,
                                            const char* key, bool required) {
    std::string id;
    strArg(st, key, "", id);
    if (id.empty()) {
        if (required) ctx.error = wideFromUtf8(std::string("haunch: '") + key + "' frame argumani gerekli");
        return nullptr;
    }
    const auto it = ctx.haunchFrames.find(id);
    if (it == ctx.haunchFrames.end()) {
        ctx.error = wideFromUtf8("haunch: tanimsiz frame '" + id + "'");
        return nullptr;
    }
    return it->second;
}

static bool readGroupArgs(EvalCtx& ctx, const RecipeStep& st, const std::string& pre,
                          Haunch40Group& gr) {
    double nx = gr.nx, ny = gr.ny;
    if (!numArg(ctx, st, (pre + "BoltCountX").c_str(), static_cast<double>(gr.nx), nx)) return false;
    if (!numArg(ctx, st, (pre + "BoltCountY").c_str(), static_cast<double>(gr.ny), ny)) return false;
    gr.nx = static_cast<int>(std::lround(nx));
    gr.ny = static_cast<int>(std::lround(ny));
    if (!numArg(ctx, st, (pre + "BoltPitchX").c_str(), gr.pitchX, gr.pitchX)) return false;
    if (!numArg(ctx, st, (pre + "BoltPitchY").c_str(), gr.pitchY, gr.pitchY)) return false;
    if (!numArg(ctx, st, (pre + "BoltDiameter").c_str(), gr.diameter, gr.diameter)) return false;
    if (!numArg(ctx, st, (pre + "HoleDiameter").c_str(), gr.hole, gr.hole)) return false;
    if (!numArg(ctx, st, (pre + "BoltEdgeClear").c_str(), gr.clear, gr.clear)) return false;
    gr.spanX = (gr.nx - 1) * gr.pitchX;
    gr.spanY = (gr.ny - 1) * gr.pitchY;
    return true;
}

static bool runTas(EvalCtx& ctx, const RecipeStep& st, const char* name,
                   const HaunchParams& prm, const char* frameKey, bool frameRequired,
                   GenerationPlan& plan) {
    const auto inFrame = haunchFrameArg(ctx, st, frameKey, frameRequired);
    if (frameRequired && !inFrame) return false;
    std::string role;
    strArg(st, "role", "", role);
    std::shared_ptr<void> outFrame;
    if (!runHaunch40Tas(name, prm, ctx.input, inFrame, outFrame, role, plan)) {
        ctx.error = plan.error;
        return false;
    }
    ctx.haunchFrames[st.id] = outFrame;
    return true;
}

// frame.contact: parametresiz GEOMETRI; roller ARGS'tan gelir (MM-5 fix item 1).
// O: argumanlar main/secondary yerine target/member — `target` = ana (kolon,
// main), `member` = ikincil (kiris, secondary_N). Hem opak haunch cercevesini
// (haunch taslari; digest DEGISMEZ) hem de P'nin yuz cercevesi mantigiyla
// GENERIC bir cerceve uretir (genel taslar plate.rect/bolts.grid/frame.offset
// bunu `frame` olarak kullanabilir). Opak cerceve her zaman input.secondary[0]'i
// kullanir (haunch tek kiris destekler); generic cerceve `member` rolunu dogru
// kullanir. Generic cerceve yalniz true-dejenerelikte (eksenle ayni dogrultu /
// iki yuze esit) uretilmez — haunch cercevesi ve digest bundan ETKILENMEZ.
static bool blockHaunchContact(EvalCtx& ctx, const RecipeStep& st, GenerationPlan& plan) {
    std::string targetRole, memberRole;
    strArg(st, "target", "main", targetRole);
    strArg(st, "member", "secondary_0", memberRole);
    if (targetRole != "main") {
        ctx.error = wideFromUtf8("frame.contact: target rolu 'main' olmali, verilen '" + targetRole + "'");
        return false;
    }
    if (memberRole.rfind("secondary_", 0) != 0) {
        ctx.error = wideFromUtf8("frame.contact: member rolu secondary_N olmali, verilen '" + memberRole + "'");
        return false;
    }
    if (!memberByRole(ctx.input, memberRole)) {
        ctx.error = wideFromUtf8("frame.contact: bilinmeyen member rolu '" + memberRole + "'");
        return false;
    }
    // Opak haunch cercevesi (haunch taslari icin; member=secondary_0 -> digest
    // DEGISMEZ, onceki davranisla birebir).
    std::shared_ptr<void> outFrame;
    if (!runHaunch40Tas("frame.contact", HaunchParams{}, ctx.input, nullptr,
                        outFrame, "", plan)) {
        ctx.error = plan.error;
        return false;
    }
    ctx.haunchFrames[st.id] = outFrame;
    // Generic yuz cercevesi (P mantigi): target'in (kolon) member'a (kiris)
    // bakan yuzu. Genel taslar icin; dejenerelikte best-effort (atlanir).
    MemberLocalFrame gf;
    if (computeMemberFace(ctx, targetRole, memberRole, gf)) {
        ctx.frames[st.id] = gf;
    } else {
        ctx.error.clear();  // best-effort: haunch cercevesi/digest etkilenmez
    }
    return true;
}

// frame.plumb_face: yalniz gap + endPlateThickness + malzeme.
static bool blockHaunchPlumbFace(EvalCtx& ctx, const RecipeStep& st, GenerationPlan& plan) {
    HaunchParams prm;
    if (!numArg(ctx, st, "gap", prm.gap, prm.gap)) return false;
    if (!numArg(ctx, st, "endPlateThickness", prm.endPlateThickness, prm.endPlateThickness)) return false;
    std::string mat;
    if (!strParamArg(ctx, st, "plateMaterial", "S235JR", mat)) return false;
    prm.plateMaterial = wideFromUtf8(mat);
    return runTas(ctx, st, "frame.plumb_face", prm, "frame", true, plan);
}

// haunch.end_plate: boyutlandirma + alin levhasi (sizing parametreleri burada).
static bool blockHaunchEndPlate(EvalCtx& ctx, const RecipeStep& st, GenerationPlan& plan) {
    HaunchParams prm;
    if (!numArg(ctx, st, "haunchLength", prm.haunchLength, prm.haunchLength)) return false;
    if (!numArg(ctx, st, "haunchDepth", prm.haunchDepth, prm.haunchDepth)) return false;
    if (!numArg(ctx, st, "haunchTipDepth", prm.haunchTipDepth, prm.haunchTipDepth)) return false;
    if (!numArg(ctx, st, "haunchWebThickness", prm.haunchWebThickness, prm.haunchWebThickness)) return false;
    if (!numArg(ctx, st, "haunchFlangeThickness", prm.haunchFlangeThickness, prm.haunchFlangeThickness)) return false;
    if (!numArg(ctx, st, "endPlateThickness", prm.endPlateThickness, prm.endPlateThickness)) return false;
    if (!numArg(ctx, st, "gap", prm.gap, prm.gap)) return false;
    if (!numArg(ctx, st, "boltEdgeLeft", prm.boltEdgeLeft, prm.boltEdgeLeft)) return false;
    if (!numArg(ctx, st, "boltEdgeRight", prm.boltEdgeRight, prm.boltEdgeRight)) return false;
    if (!numArg(ctx, st, "boltEdge", prm.boltEdge, prm.boltEdge)) return false;
    if (!numArg(ctx, st, "endPlateWidth", prm.endPlateWidth, prm.endPlateWidth)) return false;
    if (!numArg(ctx, st, "endPlateHeight", prm.endPlateHeight, prm.endPlateHeight)) return false;
    if (!numArg(ctx, st, "haunchFlangeWidth", prm.haunchFlangeWidth, prm.haunchFlangeWidth)) return false;
    if (!readGroupArgs(ctx, st, "upper", prm.upper)) return false;
    if (!readGroupArgs(ctx, st, "lower", prm.lower)) return false;
    std::string mat;
    if (!strParamArg(ctx, st, "plateMaterial", "S235JR", mat)) return false;
    prm.plateMaterial = wideFromUtf8(mat);
    return runTas(ctx, st, "haunch.end_plate", prm, "frame", true, plan);
}

// haunch.web: L, D, tip, webT (yalniz kendi parametreleri).
static bool blockHaunchWeb(EvalCtx& ctx, const RecipeStep& st, GenerationPlan& plan) {
    HaunchParams prm;
    if (!numArg(ctx, st, "haunchLength", prm.haunchLength, prm.haunchLength)) return false;
    if (!numArg(ctx, st, "haunchDepth", prm.haunchDepth, prm.haunchDepth)) return false;
    if (!numArg(ctx, st, "haunchTipDepth", prm.haunchTipDepth, prm.haunchTipDepth)) return false;
    if (!numArg(ctx, st, "haunchWebThickness", prm.haunchWebThickness, prm.haunchWebThickness)) return false;
    return runTas(ctx, st, "haunch.web", prm, "frame", true, plan);
}

// haunch.flange: L, D, tip, flangeT, flangeWidth.
static bool blockHaunchFlange(EvalCtx& ctx, const RecipeStep& st, GenerationPlan& plan) {
    HaunchParams prm;
    if (!numArg(ctx, st, "haunchLength", prm.haunchLength, prm.haunchLength)) return false;
    if (!numArg(ctx, st, "haunchDepth", prm.haunchDepth, prm.haunchDepth)) return false;
    if (!numArg(ctx, st, "haunchTipDepth", prm.haunchTipDepth, prm.haunchTipDepth)) return false;
    if (!numArg(ctx, st, "haunchFlangeThickness", prm.haunchFlangeThickness, prm.haunchFlangeThickness)) return false;
    if (!numArg(ctx, st, "haunchFlangeWidth", prm.haunchFlangeWidth, prm.haunchFlangeWidth)) return false;
    return runTas(ctx, st, "haunch.flange", prm, "frame", true, plan);
}

// haunch.stiffeners: kapak + kiriş/kolon berkitmeleri (kendi parametreleri).
static bool blockHaunchStiffeners(EvalCtx& ctx, const RecipeStep& st, GenerationPlan& plan) {
    HaunchParams prm;
    if (!numArg(ctx, st, "haunchLength", prm.haunchLength, prm.haunchLength)) return false;
    if (!numArg(ctx, st, "haunchDepth", prm.haunchDepth, prm.haunchDepth)) return false;
    if (!numArg(ctx, st, "haunchFlangeThickness", prm.haunchFlangeThickness, prm.haunchFlangeThickness)) return false;
    if (!numArg(ctx, st, "topPlateThickness", prm.topPlateThickness, prm.topPlateThickness)) return false;
    if (!numArg(ctx, st, "stiffenerClipSize", prm.stiffenerClipSize, prm.stiffenerClipSize)) return false;
    if (!numArg(ctx, st, "stiffenerThickness", prm.stiffenerThickness, prm.stiffenerThickness)) return false;
    if (!numArg(ctx, st, "addStiffenerThickness", prm.addStiffenerThickness, prm.addStiffenerThickness)) return false;
    if (!numArg(ctx, st, "rafterBottomStiffenerThickness", prm.rafterBottomStiffenerThickness, prm.rafterBottomStiffenerThickness)) return false;
    if (!numArg(ctx, st, "comprStiffenerThickness", prm.comprStiffenerThickness, prm.comprStiffenerThickness)) return false;
    if (!numArg(ctx, st, "morrisStiffenerThickness", prm.morrisStiffenerThickness, prm.morrisStiffenerThickness)) return false;
    if (!numArg(ctx, st, "diagStiffenerThickness", prm.diagStiffenerThickness, prm.diagStiffenerThickness)) return false;
    if (!numArg(ctx, st, "diagStiffenerWidth", prm.diagStiffenerWidth, prm.diagStiffenerWidth)) return false;
    if (!boolArg(ctx, st, "createBeamStiffener", prm.createBeamStiffener, prm.createBeamStiffener)) return false;
    if (!boolArg(ctx, st, "createAddStiffener", prm.createAddStiffener, prm.createAddStiffener)) return false;
    if (!boolArg(ctx, st, "createRafterBottomStiffener", prm.createRafterBottomStiffener, prm.createRafterBottomStiffener)) return false;
    if (!boolArg(ctx, st, "createComprStiffener", prm.createComprStiffener, prm.createComprStiffener)) return false;
    if (!boolArg(ctx, st, "createMorrisStiffener", prm.createMorrisStiffener, prm.createMorrisStiffener)) return false;
    if (!boolArg(ctx, st, "createDiagStiffener", prm.createDiagStiffener, prm.createDiagStiffener)) return false;
    std::string mat;
    if (!strParamArg(ctx, st, "plateMaterial", "S235JR", mat)) return false;
    prm.plateMaterial = wideFromUtf8(mat);
    return runTas(ctx, st, "haunch.stiffeners", prm, "frame", true, plan);
}

// bolts.haunch_groups: bulon gruplari + grade (kendi parametreleri).
static bool blockHaunchBolts(EvalCtx& ctx, const RecipeStep& st, GenerationPlan& plan) {
    HaunchParams prm;
    if (!readGroupArgs(ctx, st, "upper", prm.upper)) return false;
    if (!readGroupArgs(ctx, st, "lower", prm.lower)) return false;
    if (!numArg(ctx, st, "endPlateThickness", prm.endPlateThickness, prm.endPlateThickness)) return false;
    if (!numArg(ctx, st, "gap", prm.gap, prm.gap)) return false;
    if (!numArg(ctx, st, "boltEdgeLeft", prm.boltEdgeLeft, prm.boltEdgeLeft)) return false;
    if (!numArg(ctx, st, "boltEdgeRight", prm.boltEdgeRight, prm.boltEdgeRight)) return false;
    std::string gr;
    if (!strParamArg(ctx, st, "grade", "8.8", gr)) return false;
    prm.grade = wideFromUtf8(gr);
    return runTas(ctx, st, "bolts.haunch_groups", prm, "frame", true, plan);
}

// haunch.welds: kaynak parametreleri (kendi parametreleri).
static bool blockHaunchWelds(EvalCtx& ctx, const RecipeStep& st, GenerationPlan& plan) {
    HaunchParams prm;
    if (!numArg(ctx, st, "weldSize", prm.weldSize, prm.weldSize)) return false;
    if (!numArg(ctx, st, "topPlateThickness", prm.topPlateThickness, prm.topPlateThickness)) return false;
    if (!boolArg(ctx, st, "createWelds", prm.createWelds, prm.createWelds)) return false;
    return runTas(ctx, st, "haunch.welds", prm, "frame", true, plan);
}

// --- Blok kayit defteri -----------------------------------------------------
using BlockFn = bool (*)(EvalCtx&, const RecipeStep&, GenerationPlan&);

struct BlockDecl {
    const char* name;
    std::vector<const char*> numArgs;   // sayisal ifade argumanlari (statik tarama)
    std::vector<const char*> strArgs;   // sabit metin argumanlari
    std::vector<const char*> listArgs;  // liste argumanlari
    BlockFn fn;
    // `param.<ad>` kabul eden METIN argumanlari (strParamArg). validateRecipe bunlari
    // semaya karsi denetler (HR-7): parametre var olmali ve String/Material/Enum olmali.
    std::vector<const char*> strParamArgs;
    // M1: mantiksal argumanlar (boolArg) `param.<ad>` kabul eder; parametre semada
    // olmali ve Boolean olmali (or. weld.fillet double/doubleSided).
    std::vector<const char*> boolParamArgs;
    // M2: uye rolune baglanan argumanlar (main / secondary_N); baska metin reddedilir.
    // Listede de olabilir (or. bolts.grid through).
    std::vector<const char*> memberArgs;
    // M3: liste ogeleri IFADE olan argumanlar (plate.rect chamfers ':', plate.polygon
    // outline ','). Her oge parcasi validateRecipe'te scanExpr'den gecer.
    std::vector<const char*> exprListArgs;
};

static const std::vector<BlockDecl>& blockRegistry() {
    static const std::vector<BlockDecl> reg = {
        {"frame.member_end", {}, {"member", "end"}, {}, &blockFrameMemberEnd,
         {}, {}, {"member"}, {}},
        {"frame.offset", {"du", "dv", "dw"}, {"frame"}, {}, &blockFrameOffset,
         {}, {}, {}, {}},
        // HR-1: cerceveyi kendi ekseni etrafinda dondurur (u/v/w ortonormal kalir).
        {"frame.rotate", {"deg"}, {"frame", "about"}, {}, &blockFrameRotate,
         {}, {}, {}, {}},
        // P: uyenin `toward` uyesine BAKAN yuzunun cercevesi (baglantiya gore
        // yonlenir; dunyaya degil). member/toward = girdi rolu (main/secondary_N).
        {"frame.member_face", {}, {"member", "toward"}, {}, &blockFrameMemberFace,
         {}, {}, {"member", "toward"}, {}},
        // HR-3: `chamfers` listesi ("corner:type:h:v") + metin alanlari param.* kabul eder.
        {"plate.rect", {"w", "h", "t"}, {"frame", "name", "material", "finish", "posNo"},
         {"chamfers"}, &blockPlateRect, {"name", "material", "finish", "posNo"},
         {}, {}, {"chamfers"}},
        // HR-3: serbest kontur levhasi (disbukey + basit kontur sart).
        {"plate.polygon", {"t"}, {"frame", "name", "material", "finish", "posNo"},
         {"outline"}, &blockPlatePolygon, {"name", "material", "finish", "posNo"},
         {}, {}, {"outline"}},
        // HR-4: `grip` ve `holeDepth` ankraj/grip boyunu recipe'den verir.
        {"bolts.grid", {"nx", "ny", "px", "py", "d", "hole", "grip", "holeDepth"},
         {"frame", "grade"}, {"through"}, &blockBoltsGrid, {"grade"},
         {}, {"through"}, {}},
        {"weld.fillet", {"size"}, {"roleA", "roleB", "double", "doubleSided"}, {},
         &blockWeldFillet, {}, {"double", "doubleSided"}, {}, {}},
        {"cut.plane", {}, {"target", "frame", "keep"}, {}, &blockCutPlane,
         {}, {}, {"target"}, {}},
        {"endplate.placement", {}, {"main", "beam", "rolePlate", "roleBolts"}, {},
         &blockEndPlatePlacement, {}, {}, {}, {}},
        // MM-3d: her taş kendi parametrelerini alır; "frame" adıyla çerçeve,
        // "role" üretilen roleKey. frame.contact PARAMETRESİZ (roller args'tan).
        {"frame.contact", {}, {"member", "target", "role"}, {}, &blockHaunchContact,
         {}, {}, {"member", "target"}, {}},
        {"frame.plumb_face", {"gap", "endPlateThickness"},
         {"frame", "plateMaterial", "role"}, {}, &blockHaunchPlumbFace, {"plateMaterial"},
         {}, {}, {}},
        {"haunch.end_plate",
         {"haunchLength", "haunchDepth", "haunchTipDepth", "haunchWebThickness",
          "haunchFlangeThickness", "endPlateThickness", "gap", "boltEdgeLeft",
          "boltEdgeRight", "boltEdge", "endPlateWidth", "endPlateHeight",
          "haunchFlangeWidth", "upperBoltCountX", "upperBoltCountY", "upperBoltPitchX",
          "upperBoltPitchY", "upperBoltDiameter", "upperHoleDiameter",
          "upperBoltEdgeClear", "lowerBoltCountX", "lowerBoltCountY",
          "lowerBoltPitchX", "lowerBoltPitchY", "lowerBoltDiameter",
          "lowerHoleDiameter", "lowerBoltEdgeClear"},
         {"frame", "plateMaterial", "role"}, {}, &blockHaunchEndPlate, {"plateMaterial"},
         {}, {}, {}},
        {"haunch.web", {"haunchLength", "haunchDepth", "haunchTipDepth", "haunchWebThickness"},
         {"frame", "role"}, {}, &blockHaunchWeb, {}, {}, {}, {}},
        {"haunch.flange", {"haunchLength", "haunchDepth", "haunchTipDepth",
                           "haunchFlangeThickness", "haunchFlangeWidth"},
         {"frame", "role"}, {}, &blockHaunchFlange, {}, {}, {}, {}},
        {"haunch.stiffeners",
         {"haunchLength", "haunchDepth", "haunchFlangeThickness", "topPlateThickness",
          "stiffenerClipSize", "stiffenerThickness", "addStiffenerThickness",
          "rafterBottomStiffenerThickness", "comprStiffenerThickness",
          "morrisStiffenerThickness", "diagStiffenerThickness", "diagStiffenerWidth",
          "createBeamStiffener", "createAddStiffener", "createRafterBottomStiffener",
          "createComprStiffener", "createMorrisStiffener", "createDiagStiffener"},
         {"frame", "plateMaterial", "role"}, {}, &blockHaunchStiffeners, {"plateMaterial"},
         {}, {}, {}},
        {"bolts.haunch_groups",
         {"upperBoltCountX", "upperBoltCountY", "upperBoltPitchX", "upperBoltPitchY",
          "upperBoltDiameter", "upperHoleDiameter", "upperBoltEdgeClear",
          "lowerBoltCountX", "lowerBoltCountY", "lowerBoltPitchX", "lowerBoltPitchY",
          "lowerBoltDiameter", "lowerHoleDiameter", "lowerBoltEdgeClear",
          "endPlateThickness", "gap", "boltEdgeLeft", "boltEdgeRight"},
         {"frame", "grade", "role"}, {}, &blockHaunchBolts, {"grade"},
         {}, {}, {}},
        {"haunch.welds", {"weldSize", "topPlateThickness", "createWelds"},
         {"frame", "role"}, {}, &blockHaunchWelds, {}, {}, {}, {}},
    };
    return reg;
}

static const BlockDecl* findBlock(const std::string& use) {
    for (const auto& d : blockRegistry())
        if (use == d.name) return &d;
    return nullptr;
}

// L: public bildirim tablosundan isimle arama (J/M denetimleri bu tablodan beslenir).
static const BlockDeclInfo* findBlockInfo(const std::string& use) {
    static const std::vector<BlockDeclInfo> decls = blockDeclarations();
    for (const auto& d : decls)
        if (d.use == use) return &d;
    return nullptr;
}

static std::string kindForBlock(const std::string& use) {
    if (use == "plate.rect") return "plate";
    if (use == "bolts.grid") return "bolt";
    if (use == "weld.fillet") return "weld";
    if (use == "cut.plane") return "cut";
    return "";
}

// --- Statik analiz yardimcilari ---------------------------------------------
static bool isKnownFunction(const std::string& n) {
    return n == "min" || n == "max" || n == "abs" || n == "sqrt" || n == "sin" ||
           n == "cos" || n == "tan" || n == "atan2" || n == "clamp" || n == "if";
}

static bool validRoleKey(const std::string& role) {
    if (role.empty()) return false;
    for (const char c : role)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    return true;
}

// M2: girdi rolu = "main" ya da "secondary_" + rakamlar.
static bool isInputRole(const std::string& s) {
    if (s == "main") return true;
    if (s.rfind("secondary_", 0) != 0) return false;
    const std::string n = s.substr(std::string("secondary_").size());
    if (n.empty()) return false;
    for (const char c : n)
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

// roleKey formati + rezerve + tekillik + tur tutarliligi denetimi.
// 1.15.0 (contract istegi g): AYNI roleKey, BIRBIRINI DISLAYAN `when`
// kosullariyla birden fazla kez kullanilabilir (or. pahli / pahsiz alternatif).
// Kosullarin gercekten dislayici oldugu statik olarak KANITLANAMAZ; ikisi
// birden URETILIRSE runRecipeCore calisma aninda HATA verir.
static bool checkRoleKey(const std::string& role, const std::string& kind,
                         const std::string& when,
                         std::map<std::string, std::string>& roleKind,
                         std::map<std::string, std::string>& roleWhen,
                         RecipeValidation& r) {
    if (role.empty()) return true;
    if (!validRoleKey(role)) {
        r.ok = false;
        r.message = L"gecersiz roleKey: " + wideFromUtf8(role);
        return false;
    }
    if (role == "main" || role.rfind("secondary_", 0) == 0) {
        r.ok = false;
        r.message = L"rezerve roleKey kullanilamaz: " + wideFromUtf8(role);
        return false;
    }
    const auto ex = roleKind.find(role);
    if (ex != roleKind.end()) {
        if (ex->second != kind) {
            r.ok = false;
            r.message = L"roleKey farkli ture baglandi: " + wideFromUtf8(role);
            return false;
        }
        // 1.15.0 (g): iki adim da BOS OLMAYAN ve FARKLI `when` tasiyorsa
        // kosullu alternatif kabul edilir.
        const auto w = roleWhen.find(role);
        if (!when.empty() && w != roleWhen.end() && !w->second.empty() && w->second != when) {
            return true;
        }
        r.ok = false;
        r.message = L"yinelenen roleKey: " + wideFromUtf8(role);
        return false;
    }
    roleKind[role] = kind;
    roleWhen[role] = when;
    return true;
}

// Bir ifade metnini tarar: bilinmeyen fonksiyon / bilinmeyen parametre /
// tanimsiz let / bilinmeyen uye alani -> hata.
static bool scanExpr(const std::vector<CompParamSpec>& specs,
                     const std::set<std::string>& letNames,
                     const std::string& text, RecipeValidation& r) {
    std::vector<Tok> toks;
    std::wstring err;
    tokenize(text, toks, err);
    if (!err.empty()) { r.ok = false; r.message = err; return false; }
    for (std::size_t k = 0; k < toks.size(); ++k) {
        if (toks[k].kind != Tok::Ident) continue;
        const bool isCall = (k + 1 < toks.size() && toks[k + 1].kind == Tok::LParen);
        if (isCall) {
            if (!isKnownFunction(toks[k].ident)) {
                r.ok = false;
                r.message = L"bilinmeyen fonksiyon: " + wideFromUtf8(toks[k].ident);
                return false;
            }
            continue;
        }
        const std::string& name = toks[k].ident;
        const auto dot = name.find('.');
        if (dot == std::string::npos) {
            if (!letNames.count(name)) {
                r.ok = false;
                r.message = L"tanimsiz let: " + wideFromUtf8(name);
                return false;
            }
        } else {
            const std::string prefix = name.substr(0, dot);
            const std::string field = name.substr(dot + 1);
            if (prefix == "param") {
                bool found = false;
                for (const auto& s : specs)
                    if (utf8FromWide(s.name) == field) { found = true; break; }
                if (!found) {
                    r.ok = false;
                    r.message = L"bilinmeyen parametre: " + wideFromUtf8(field);
                    return false;
                }
            } else if (prefix == "main" || prefix.rfind("secondary_", 0) == 0) {
                static const std::set<std::string> fields{"h", "b", "tw", "tf", "length"};
                if (!fields.count(field)) {
                    r.ok = false;
                    r.message = L"bilinmeyen uye alani: " + wideFromUtf8(name);
                    return false;
                }
            } else {
                r.ok = false;
                r.message = L"bilinmeyen tanimlayici: " + wideFromUtf8(name);
                return false;
            }
        }
    }
    return true;
}

// Bir ifadedeki cıplak (noktasiz, fonksiyon olmayan) tanimlayicilari toplar
// (let bagimlilik grafi icin).
static void collectBareIdents(const std::string& text, std::set<std::string>& out) {
    std::vector<Tok> toks;
    std::wstring err;
    tokenize(text, toks, err);
    if (!err.empty()) return;
    for (std::size_t k = 0; k < toks.size(); ++k) {
        if (toks[k].kind != Tok::Ident) continue;
        if (k + 1 < toks.size() && toks[k + 1].kind == Tok::LParen) continue;  // fonksiyon
        if (toks[k].ident.find('.') != std::string::npos) continue;            // param./main./secondary_.
        out.insert(toks[k].ident);
    }
}

static bool cyclicLetCheck(const Recipe& recipe, RecipeValidation& r) {
    std::set<std::string> letNames;
    for (const auto& l : recipe.lets) letNames.insert(l.first);
    std::map<std::string, std::set<std::string>> deps;
    for (const auto& l : recipe.lets) {
        std::set<std::string> refs;
        collectBareIdents(l.second, refs);
        for (const auto& rr : refs)
            if (letNames.count(rr)) deps[l.first].insert(rr);
    }
    std::map<std::string, int> state;  // 0=ziyaret edilmedi, 1=devam, 2=bitti
    std::function<bool(const std::string&)> dfs = [&](const std::string& n) -> bool {
        if (state[n] == 1) {
            r.ok = false;
            r.message = L"dongusel let: " + wideFromUtf8(n);
            return false;
        }
        if (state[n] == 2) return true;
        state[n] = 1;
        for (const auto& d : deps[n])
            if (!dfs(d)) return false;
        state[n] = 2;
        return true;
    };
    for (const auto& l : recipe.lets)
        if (!dfs(l.first)) return false;
    return true;
}

} // namespace

// --- H: cerceve ad uzayi siniflandirmasi -------------------------------------
// Generic bloklar frameArg ile ctx.frames'i okur/yazar; haunch bloklari
// haunchFrameArg ile ctx.haunchFrames'i. Bir 'frame' argumani YALNIZ kendi ad
// uzayindaki ONCEKI bir adimin urettigi frame id'sini gosterebilir. Bu siniflar
// blockRegistry() ile birebir paraleldir; yeni tas eklerken IKISINI de guncelle.
static bool producesGenericFrame(const std::string& use) {
    return use == "frame.member_end" || use == "frame.offset" || use == "frame.rotate" ||
           use == "frame.member_face" || use == "frame.contact";
}
static bool producesHaunchFrame(const std::string& use) {
    return use == "frame.contact" || use == "frame.plumb_face" ||
           use == "haunch.end_plate" || use == "haunch.web" || use == "haunch.flange" ||
           use == "haunch.stiffeners" || use == "bolts.haunch_groups" ||
           use == "haunch.welds";
}
// 'frame' argumani HANGI ad uzaydan okur? (yalniz strArgs'inde "frame" olan bloklar)
static bool frameArgReadsHaunch(const std::string& use) {
    return use == "frame.plumb_face" || use == "haunch.end_plate" ||
           use == "haunch.web" || use == "haunch.flange" || use == "haunch.stiffeners" ||
           use == "bolts.haunch_groups" || use == "haunch.welds";
}

// --- L: tas bildirimleri (salt okunur API) -----------------------------------
// blockRegistry()'nin disa acik aynasi. Argumanlar ada gore alfabetik sirali;
// bir argumanin turu en ozgul kategoriden secilir (member > logical > text-param
// > list > expression > text).
std::vector<BlockDeclInfo> blockDeclarations() {
    std::vector<BlockDeclInfo> out;
    out.reserve(blockRegistry().size());
    for (const BlockDecl& d : blockRegistry()) {
        BlockDeclInfo info;
        info.use = d.name;
        std::map<std::string, BlockArgDecl> byName;
        for (const char* k : d.numArgs)
            byName[k] = BlockArgDecl{k, RecipeArgKind::Expression, false, false};
        for (const char* k : d.strArgs)
            if (!byName.count(k)) byName[k] = BlockArgDecl{k, RecipeArgKind::Text, false, false};
        for (const char* k : d.listArgs) {
            byName[k].name = k;
            byName[k].isList = true;
            if (byName[k].kind == RecipeArgKind::Text) byName[k].kind = RecipeArgKind::List;
        }
        for (const char* k : d.strParamArgs) byName[k].kind = RecipeArgKind::TextParam;
        for (const char* k : d.boolParamArgs) byName[k].kind = RecipeArgKind::Logical;
        for (const char* k : d.memberArgs) byName[k].kind = RecipeArgKind::MemberRole;
        for (const char* k : d.exprListArgs) byName[k].exprItems = true;
        for (const auto& kv : byName) info.args.push_back(kv.second);
        // ada gore alfabetik (map zaten sirali; yine de garanti).
        std::sort(info.args.begin(), info.args.end(),
                  [](const BlockArgDecl& a, const BlockArgDecl& b) { return a.name < b.name; });
        out.push_back(std::move(info));
    }
    return out;
}

// O (migration): yeniden adlandirilan argumanlar icin eski ad -> yeni ad ipucu.
// Bilinmeyen arguman bu listedeyse fail-closed mesaji YENI adi soyler; aksi halde
// genel "bilinmeyen arguman" mesaji doner.
static const char* renamedArgHint(const std::string& use, const std::string& arg) {
    if (use == "frame.contact") {
        if (arg == "main") return "target";
        if (arg == "secondary") return "member";
    }
    return nullptr;
}

// --- DOGRULAMA (statik analiz) ----------------------------------------------
RecipeValidation validateRecipe(const Recipe& recipe,
                                const std::vector<CompParamSpec>& specs) noexcept {
    RecipeValidation r;
    r.ok = true;
    if (recipe.steps.size() > 256) {
        r.ok = false;
        r.message = L"adim sayisi 256'yi asiyor";
        return r;
    }
    std::set<std::string> letNames;
    for (const auto& l : recipe.lets) letNames.insert(l.first);

    std::map<std::string, std::string> roleKind;
    std::map<std::string, std::string> roleWhen;  // 1.15.0 (g): roleKey -> ilk `when`
    // H: cerceve ad uzaylari — bir 'frame' argumani yalniz kendi ad uzayinda
    // ONCEKI bir adimin urettigi frame id'sini gosterebilir (tanimsiz cerceve
    // statik yakalanir; runRecipe calisma anina kadar beklenmez).
    std::set<std::string> genericFrames, haunchFrames;
    for (std::size_t stepIndex = 0; stepIndex < recipe.steps.size(); ++stepIndex) {
        const RecipeStep& st = recipe.steps[stepIndex];
        if (!st.check.empty()) {
            if (!scanExpr(specs, letNames, st.check, r)) return r;
            continue;
        }
        const BlockDecl* decl = findBlock(st.use);
        if (!decl) {
            r.ok = false;
            r.message = L"bilinmeyen blok: " + wideFromUtf8(st.use);
            return r;
        }
        const BlockDeclInfo* info = findBlockInfo(st.use);
        if (st.id.empty()) {
            r.ok = false;
            r.message = L"blok adimi id gerektirir: " + wideFromUtf8(st.use);
            return r;
        }
        // J: bildirilmemis arguman -> adim indeksiyle fail-closed. Onceden
        // (or. plate.rect'e 'origin' ya da 'width') sessizce yok sayiliyordu.
        // L: denetim public bildirim tablosundan beslenir.
        {
            std::set<std::string> declared;
            if (info) for (const auto& a : info->args) declared.insert(a.name);
            for (const auto& kv : st.args) {
                if (!declared.count(kv.first)) {
                    r.ok = false;
                    if (const char* nw = renamedArgHint(st.use, kv.first)) {
                        r.message = L"recipe adim " + std::to_wstring(stepIndex) + L" (" +
                                    wideFromUtf8(st.use) + L"): '" + wideFromUtf8(kv.first) +
                                    L"' argumani '" + wideFromUtf8(nw) +
                                    L"' olarak yeniden adlandirildi; bu adi kullanin";
                    } else {
                        r.message = L"recipe adim " + std::to_wstring(stepIndex) + L" (" +
                                    wideFromUtf8(st.use) + L"): bilinmeyen arguman: " +
                                    wideFromUtf8(st.use) + L"." + wideFromUtf8(kv.first);
                    }
                    return r;
                }
            }
        }
        for (const char* key : decl->numArgs) {
            const auto it = st.args.find(key);
            if (it != st.args.end() && !it->second.isList) {
                if (!scanExpr(specs, letNames, it->second.text, r)) return r;
            }
        }
        // HR-7: `param.<ad>` METIN argumanlari STATIK denetlenir — parametre semada
        // olmali ve metin tipinde olmali. Onceden "param.yok" dogrulamadan geciyor,
        // calisma aninda ise bos mesajli ok=false uretiyordu.
        for (const char* key : decl->strParamArgs) {
            const auto it = st.args.find(key);
            if (it == st.args.end() || it->second.isList) continue;
            static const std::string kParamPrefix = "param.";
            if (it->second.text.rfind(kParamPrefix, 0) != 0) continue;  // literal
            const std::wstring pname = wideFromUtf8(it->second.text.substr(kParamPrefix.size()));
            const CompParamSpec* spec = nullptr;
            for (const auto& s : specs)
                if (s.name == pname) { spec = &s; break; }
            if (!spec) {
                r.ok = false;
                r.message = L"recipe: " + wideFromUtf8(st.use) + L"." + wideFromUtf8(key) +
                            L" -> bilinmeyen parametre: " + pname;
                return r;
            }
            const bool isText = spec->type == CompParamType::String ||
                                spec->type == CompParamType::Material ||
                                spec->type == CompParamType::Enum ||
                                spec->type == CompParamType::Profile;
            if (!isText) {
                r.ok = false;
                r.message = L"recipe: " + wideFromUtf8(st.use) + L"." + wideFromUtf8(key) +
                            L" metin parametresi degil (metin olmayan parametre): " + pname;
                return r;
            }
        }
        if (!st.when.empty()) {
            if (!scanExpr(specs, letNames, st.when, r)) return r;
        }
        // M1: mantiksal argumanlar (boolArg) `param.<ad>` kabul eder; parametre
        // semada olmali ve Boolean olmali (literal true/false/1/0 atlanir).
        // L: tur bilgisi public bildirim tablosundan (kind==Logical) gelir.
        for (const auto& arg : (info ? info->args : std::vector<BlockArgDecl>{})) {
            if (arg.kind != RecipeArgKind::Logical) continue;
            const std::string key = arg.name;
            const auto it = st.args.find(key);
            if (it == st.args.end() || it->second.isList) continue;
            const std::string& text = it->second.text;
            static const std::string kPfx = "param.";
            if (text.rfind(kPfx, 0) != 0) continue;  // literal mantiksal
            const std::wstring pname = wideFromUtf8(text.substr(kPfx.size()));
            const CompParamSpec* bspec = nullptr;
            for (const auto& s : specs)
                if (s.name == pname) { bspec = &s; break; }
            if (!bspec) {
                r.ok = false;
                r.message = L"recipe adim " + std::to_wstring(stepIndex) + L" (" +
                            wideFromUtf8(st.use) + L"): " + wideFromUtf8(key) +
                            L" -> bilinmeyen parametre: " + pname;
                return r;
            }
            if (bspec->type != CompParamType::Boolean) {
                r.ok = false;
                r.message = L"recipe adim " + std::to_wstring(stepIndex) + L" (" +
                            wideFromUtf8(st.use) + L"): " + wideFromUtf8(key) +
                            L" Boolean parametre degil: " + pname;
                return r;
            }
        }
        // M2: uye argumanlari (member/target/through oge.../main/beam/...) girdi
        // rolu olmali (main veya secondary_N); baska metin reddedilir.
        // L: tur bilgisi public bildirim tablosundan (kind==MemberRole) gelir.
        for (const auto& arg : (info ? info->args : std::vector<BlockArgDecl>{})) {
            if (arg.kind != RecipeArgKind::MemberRole) continue;
            const std::string key = arg.name;
            const auto it = st.args.find(key);
            if (it == st.args.end()) continue;
            const auto reject = [&](const std::string& val, const std::wstring& roleName) {
                r.ok = false;
                r.message = L"recipe adim " + std::to_wstring(stepIndex) + L" (" +
                            wideFromUtf8(st.use) + L"): uye argumani '" + roleName +
                            L"' girdi rolu olmali (main veya secondary_N): " +
                            wideFromUtf8(val);
            };
            if (it->second.isList) {
                for (const auto& item : it->second.items) {
                    if (item.isList || !isInputRole(item.text)) {
                        reject(item.text, wideFromUtf8(key));
                        return r;
                    }
                }
            } else {
                const std::string& v = it->second.text;
                if (!v.empty() && !isInputRole(v)) {
                    reject(v, wideFromUtf8(key));
                    return r;
                }
            }
        }
        // M3: liste ogeleri ifade olan argumanlar (chamfers ':', outline ',')
        // statik taranir (bilinmeyen parametre/let/uye alani).
        // L: tur bilgisi public bildirim tablosundan (kind==List && exprItems) gelir.
        for (const auto& arg : (info ? info->args : std::vector<BlockArgDecl>{})) {
            if (arg.kind != RecipeArgKind::List || !arg.exprItems) continue;
            const std::string key = arg.name;
            const auto it = st.args.find(key);
            if (it == st.args.end() || !it->second.isList) continue;
            const char sep = (key == "chamfers") ? ':' : ',';
            for (const auto& item : it->second.items) {
                if (item.isList) continue;
                std::vector<std::string> parts;
                std::size_t start = 0;
                while (true) {
                    const std::size_t pos = item.text.find(sep, start);
                    parts.push_back(item.text.substr(start, pos == std::string::npos ? std::string::npos : pos - start));
                    if (pos == std::string::npos) break;
                    start = pos + 1;
                }
                for (const auto& part : parts) {
                    if (!scanExpr(specs, letNames, part, r)) {
                        r.message = L"recipe adim " + std::to_wstring(stepIndex) + L" (" +
                                    wideFromUtf8(st.use) + L"): liste ogesi '" +
                                    wideFromUtf8(key) + L"': " + r.message;
                        return r;
                    }
                }
            }
        }
        const std::string kind = kindForBlock(st.use);
        if (!st.role.empty() && !kind.empty()) {
            if (!checkRoleKey(st.role, kind, st.when, roleKind, roleWhen, r)) return r;
        }
        if (st.use == "endplate.placement") {
            std::string rp = "end_plate", rb = "beam_bolts";
            const auto ip = st.args.find("rolePlate");
            if (ip != st.args.end() && !ip->second.text.empty()) rp = ip->second.text;
            const auto ib = st.args.find("roleBolts");
            if (ib != st.args.end() && !ib->second.text.empty()) rb = ib->second.text;
            if (!checkRoleKey(rp, "plate", st.when, roleKind, roleWhen, r)) return r;
            if (!checkRoleKey(rb, "bolt", st.when, roleKind, roleWhen, r)) return r;
        }
        // H: 'frame' argumani (varsa) kendi ad uzayinda ONCEKI bir adimin frame
        // id'sini gostermeli; degilse ACIK ret (adim indeksiyle). frame.offset /
        // frame.rotate gibi hem tuketip hem ureten bloklar da ayni kurala girer.
        if (std::find(decl->strArgs.begin(), decl->strArgs.end(), std::string("frame")) !=
            decl->strArgs.end()) {
            const auto fit = st.args.find("frame");
            if (fit != st.args.end() && !fit->second.isList && !fit->second.text.empty()) {
                const std::string& fid = fit->second.text;
                const bool known = frameArgReadsHaunch(st.use)
                                       ? haunchFrames.count(fid) != 0
                                       : genericFrames.count(fid) != 0;
                if (!known) {
                    r.ok = false;
                    r.message = L"recipe adim " + std::to_wstring(stepIndex) + L" (" +
                                wideFromUtf8(st.use) + L"): tanimsiz cerceve: " +
                                wideFromUtf8(fid);
                    return r;
                }
            }
        }
        // Bu adim kendi ad uzayina bir frame uretiyorsa kaydet. frame.contact
        // (O) IKISINI de uretir: opak haunch cercevesi + generic yuz cercevesi.
        if (producesGenericFrame(st.use)) genericFrames.insert(st.id);
        if (producesHaunchFrame(st.use)) haunchFrames.insert(st.id);
    }
    // let ifadelerini tara (tanimsiz let / bilinmeyen parametre) + dongu.
    for (const auto& l : recipe.lets)
        if (!scanExpr(specs, letNames, l.second, r)) return r;
    if (!cyclicLetCheck(recipe, r)) return r;
    return r;
}

// --- YORUMCU ----------------------------------------------------------------
// Ortak cekirdek: runRecipe ve runRecipeFrames AYNI koddan gecer (plan/digest
// birebir). framesOut != nullptr ise generic cerceve ureten her adimin
// (producesGenericFrame) cercevesi adim id'siyle kaydedilir.
static void runRecipeCore(const Recipe& recipe,
                          const std::vector<CompParamSpec>& specs,
                          const ComponentInput& input, GenerationPlan& plan,
                          std::map<std::string, MemberLocalFrame>* framesOut) {
    plan.ok = true;
    const RecipeValidation v = validateRecipe(recipe, specs);
    if (!v.ok) {
        plan.ok = false;
        plan.error = v.message;
        return;
    }
    EvalCtx ctx{input, recipe, specs, {}, {}, {}, {}, {}, 0, {}};
    const std::vector<CompParam> aligned = alignParamsToSpecs(specs, input.params);
    for (const auto& spec : specs) {
        // Boolean parametreler 1/0 olarak yayinlanir: defDouble'a dusmek
        // (or. createWelds=true varsayilani) sessizce FALSE uretirdi.
        if (spec.type == CompParamType::Boolean)
            ctx.params[utf8FromWide(spec.name)] =
                paramAsBool(aligned, spec.name, spec.defBool) ? 1.0 : 0.0;
        else
            ctx.params[utf8FromWide(spec.name)] = paramAsDouble(aligned, spec.name, spec.defDouble);
    }
    for (const auto& l : recipe.lets) ctx.lets[l.first] = l.second;

    for (const auto& st : recipe.steps) {
        if (!st.check.empty()) {
            double cv = 0.0;
            if (!evaluateExpr(ctx, st.check, cv)) { plan.ok = false; plan.error = ctx.error; return; }
            if (cv == 0.0) {
                plan.ok = false;
                plan.error = st.message.empty() ? L"check basarisiz" : wideFromUtf8(st.message);
                // P-1/5: IHLAL EDEN CHECK RAPORLANIR. Application bu listeye
                // bakarak "parametreyle duzeltilebilir" karari verir ve
                // komponent diyalogunu acar. Eskiden check adimi erken
                // dondugu icin checks bos kaliyor, paket dogrulamalari
                // diyalog YERINE yalniz uyari gosteriyordu.
                ComponentCheck c;
                c.id = st.id.empty() ? std::string("check") : st.id;
                c.pass = false;
                c.message = plan.error;
                plan.checks.push_back(c);
                return;
            }
            continue;
        }
        if (!st.when.empty()) {
            double wv = 0.0;
            if (!evaluateExpr(ctx, st.when, wv)) { plan.ok = false; plan.error = ctx.error; return; }
            if (wv == 0.0) continue;  // kosul saglanmadi -> atla
        }
        const BlockDecl* decl = findBlock(st.use);
        if (!decl) { plan.ok = false; plan.error = L"bilinmeyen blok: " + wideFromUtf8(st.use); return; }
        if (!decl->fn(ctx, st, plan)) { plan.ok = false; plan.error = ctx.error; return; }
        // Generic cerceve urettiyse (ve uretildiyse) disari kaydet.
        if (framesOut && producesGenericFrame(st.use)) {
            const auto it = ctx.frames.find(st.id);
            if (it != ctx.frames.end()) (*framesOut)[st.id] = it->second;
        }
    }
    // 1.15.0 (g): kosullu alternatifler AYNI roleKey'i kullanabilir; ikisi
    // birden uretildiyse plan REDDEDILIR (sessiz cift nesne yok).
    {
        const std::vector<const std::vector<std::string>*> lists = {
            &plan.plateKeys, &plan.boltGroupKeys, &plan.cutKeys, &plan.weldKeys};
        for (const auto* keys : lists) {
            std::set<std::string> seen;
            for (const auto& k : *keys) {
                if (k.empty()) continue;
                if (!seen.insert(k).second) {
                    plan.ok = false;
                    plan.error = L"roleKey iki kez uretildi (when kosullari dislayici degil): " +
                                 wideFromUtf8(k);
                    return;
                }
            }
        }
    }
}

GenerationPlan runRecipe(const Recipe& recipe,
                         const std::vector<CompParamSpec>& specs,
                         const ComponentInput& input) noexcept {
    GenerationPlan plan;
    runRecipeCore(recipe, specs, input, plan, nullptr);
    return plan;
}

// N — salt okunur cerceve API'si (Studio'nun plate.rect hilesi yerine).
std::map<std::string, RecipeFrame> runRecipeFrames(
    const Recipe& recipe, const std::vector<CompParamSpec>& specs,
    const ComponentInput& input) noexcept {
    GenerationPlan plan;
    std::map<std::string, MemberLocalFrame> frames;
    runRecipeCore(recipe, specs, input, plan, &frames);
    std::map<std::string, RecipeFrame> out;
    for (const auto& kv : frames) {
        RecipeFrame f;
        f.origin = kv.second.origin;
        f.u = kv.second.u;
        f.v = kv.second.v;
        f.w = kv.second.w;
        out[kv.first] = f;
    }
    return out;
}

} // namespace mm
