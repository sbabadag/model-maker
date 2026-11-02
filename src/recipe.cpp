#include "model_maker/recipe.hpp"

#include "model_maker/component_codec.hpp"  // utf8FromWide / wideFromUtf8 (param adi esleme + hata)
#include "model_maker/component_checks.hpp" // applyChecksToPlan (endplate.placement kontrolu)
#include "model_maker/geometry.hpp"        // ProfileSectionFrame, WorkPlane, Vec3
#include "model_maker/end_plate.hpp"       // computeEndPlatePlacement (endplate.placement tasi)
#include "model_maker/haunch40.hpp"        // Haunch 40 taslari (C2)

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
static bool strParamArg(const EvalCtx& ctx, const RecipeStep& st, const char* key,
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
        return false;  // parametre metin degil
    }
    // Girdi parametresi YOKSA sema VARSAYILANI (golden girdiler parametresiz
    // gelir; generator da ayni varsayilani kullanir).
    for (const auto& spec : ctx.specs) {
        if (spec.name != name) continue;
        if (!spec.defString.empty()) { out = utf8FromWide(spec.defString); return true; }
        if (!spec.choices.empty()) { out = utf8FromWide(spec.choices.front()); return true; }
        return false;
    }
    return false;      // tanimsiz parametre
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
    const bool atStart = (end == "start" || end == "from");
    MemberLocalFrame f;
    f.origin = atStart ? m->axisFrom : m->axisTo;
    const ProfileSectionFrame pf = profileSectionFrame(dir, m->refNormal);
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

static bool blockPlateRect(EvalCtx& ctx, const RecipeStep& st, GenerationPlan& plan) {
    MemberLocalFrame* f = frameArg(ctx, st, "frame");
    if (!f) return false;
    double w = 0.0, h = 0.0, t = 0.0;
    if (!numArg(ctx, st, "w", 0.0, w)) return false;
    if (!numArg(ctx, st, "h", 0.0, h)) return false;
    if (!numArg(ctx, st, "t", 0.0, t)) return false;
    std::string name, material, finish, posNo;
    strArg(st, "name", "", name);
    strArg(st, "material", "", material);
    strArg(st, "finish", "", finish);
    strArg(st, "posNo", "", posNo);
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
    for (const auto& r : through) {
        const MemberGeometry* m = memberByRole(ctx.input, r);
        if (!m) { ctx.error = wideFromUtf8("bolts.grid: bilinmeyen uye rolu '" + r + "'"); return false; }
        b.parts.push_back(m->id);
    }
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
    else w.partARole = a;
    const MemberGeometry* mb = memberByRole(ctx.input, b);
    if (mb) w.partB = mb->id;
    else w.partBRole = b;
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
    plan.boltGroups.push_back(bolts);
    plan.boltGroupKeys.push_back(roleBolts);

    // Item 4: kiris kesimi generator ile AYNI kaynaktan (endPlateBeamCut).
    CutModifier beamCut;
    endPlateBeamCut(placement, beam->id, beamCut);
    plan.cuts.push_back(beamCut);
    plan.cutKeys.push_back("cut_beam_end");

    // MM-7 (item 1): generator ile AYNI kontrol (gen == rec). Ihlalde ctx.error
    // kurulur; runRecipe plan.ok=false yapar.
    applyChecksToPlan(plan, ctx.input);
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
// Bugun motor tek mertek destekliyor: main=main, secondary=secondary_0.
// Farkli bir rol istenirse ACIK hata (sessizce ilk uyeyi kullanma YOK).
static bool blockHaunchContact(EvalCtx& ctx, const RecipeStep& st, GenerationPlan& plan) {
    std::string mainRole, secRole;
    strArg(st, "main", "main", mainRole);
    strArg(st, "secondary", "secondary_0", secRole);
    if (mainRole != "main") {
        ctx.error = wideFromUtf8("frame.contact: main rolu 'main' olmali, verilen '" + mainRole + "'");
        return false;
    }
    if (secRole != "secondary_0") {
        ctx.error = wideFromUtf8(
            "frame.contact: yalniz 'secondary_0' destekleniyor, verilen '" + secRole + "'");
        return false;
    }
    return runTas(ctx, st, "frame.contact", HaunchParams{}, "", false, plan);
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
};

static const std::vector<BlockDecl>& blockRegistry() {
    static const std::vector<BlockDecl> reg = {
        {"frame.member_end", {}, {"member", "end"}, {}, &blockFrameMemberEnd},
        {"frame.offset", {"du", "dv", "dw"}, {"frame"}, {}, &blockFrameOffset},
        {"plate.rect", {"w", "h", "t"}, {"frame", "name", "material", "finish", "posNo"}, {}, &blockPlateRect},
        {"bolts.grid", {"nx", "ny", "px", "py", "d", "hole"}, {"frame", "grade"}, {"through"}, &blockBoltsGrid},
        {"weld.fillet", {"size"}, {"roleA", "roleB", "double", "doubleSided"}, {}, &blockWeldFillet},
        {"cut.plane", {}, {"target", "frame", "keep"}, {}, &blockCutPlane},
        {"endplate.placement", {}, {"main", "beam", "rolePlate", "roleBolts"}, {}, &blockEndPlatePlacement},
        // MM-3d: her taş kendi parametrelerini alır; "frame" adıyla çerçeve,
        // "role" üretilen roleKey. frame.contact PARAMETRESİZ.
        {"frame.contact", {}, {"main", "secondary", "role"}, {}, &blockHaunchContact},
        {"frame.plumb_face", {"gap", "endPlateThickness"},
         {"frame", "plateMaterial", "role"}, {}, &blockHaunchPlumbFace},
        {"haunch.end_plate",
         {"haunchLength", "haunchDepth", "haunchTipDepth", "haunchWebThickness",
          "haunchFlangeThickness", "endPlateThickness", "gap", "boltEdgeLeft",
          "boltEdgeRight", "boltEdge", "endPlateWidth", "endPlateHeight",
          "haunchFlangeWidth", "upperBoltCountX", "upperBoltCountY", "upperBoltPitchX",
          "upperBoltPitchY", "upperBoltDiameter", "upperHoleDiameter",
          "upperBoltEdgeClear", "lowerBoltCountX", "lowerBoltCountY",
          "lowerBoltPitchX", "lowerBoltPitchY", "lowerBoltDiameter",
          "lowerHoleDiameter", "lowerBoltEdgeClear"},
         {"frame", "plateMaterial", "role"}, {}, &blockHaunchEndPlate},
        {"haunch.web", {"haunchLength", "haunchDepth", "haunchTipDepth", "haunchWebThickness"},
         {"frame", "role"}, {}, &blockHaunchWeb},
        {"haunch.flange", {"haunchLength", "haunchDepth", "haunchTipDepth",
                           "haunchFlangeThickness", "haunchFlangeWidth"},
         {"frame", "role"}, {}, &blockHaunchFlange},
        {"haunch.stiffeners",
         {"haunchLength", "haunchDepth", "haunchFlangeThickness", "topPlateThickness",
          "stiffenerClipSize", "stiffenerThickness", "addStiffenerThickness",
          "rafterBottomStiffenerThickness", "comprStiffenerThickness",
          "morrisStiffenerThickness", "diagStiffenerThickness", "diagStiffenerWidth",
          "createBeamStiffener", "createAddStiffener", "createRafterBottomStiffener",
          "createComprStiffener", "createMorrisStiffener", "createDiagStiffener"},
         {"frame", "plateMaterial", "role"}, {}, &blockHaunchStiffeners},
        {"bolts.haunch_groups",
         {"upperBoltCountX", "upperBoltCountY", "upperBoltPitchX", "upperBoltPitchY",
          "upperBoltDiameter", "upperHoleDiameter", "upperBoltEdgeClear",
          "lowerBoltCountX", "lowerBoltCountY", "lowerBoltPitchX", "lowerBoltPitchY",
          "lowerBoltDiameter", "lowerHoleDiameter", "lowerBoltEdgeClear",
          "endPlateThickness", "gap", "boltEdgeLeft", "boltEdgeRight"},
         {"frame", "grade", "role"}, {}, &blockHaunchBolts},
        {"haunch.welds", {"weldSize", "topPlateThickness", "createWelds"},
         {"frame", "role"}, {}, &blockHaunchWelds},
    };
    return reg;
}

static const BlockDecl* findBlock(const std::string& use) {
    for (const auto& d : blockRegistry())
        if (use == d.name) return &d;
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

// roleKey formati + rezerve + tekillik + tur tutarliligi denetimi.
static bool checkRoleKey(const std::string& role, const std::string& kind,
                         std::map<std::string, std::string>& roleKind, RecipeValidation& r) {
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
        } else {
            r.ok = false;
            r.message = L"yinelenen roleKey: " + wideFromUtf8(role);
        }
        return false;
    }
    roleKind[role] = kind;
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
    for (const auto& st : recipe.steps) {
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
        if (st.id.empty()) {
            r.ok = false;
            r.message = L"blok adimi id gerektirir: " + wideFromUtf8(st.use);
            return r;
        }
        for (const char* key : decl->numArgs) {
            const auto it = st.args.find(key);
            if (it != st.args.end() && !it->second.isList) {
                if (!scanExpr(specs, letNames, it->second.text, r)) return r;
            }
        }
        if (!st.when.empty()) {
            if (!scanExpr(specs, letNames, st.when, r)) return r;
        }
        const std::string kind = kindForBlock(st.use);
        if (!st.role.empty() && !kind.empty()) {
            if (!checkRoleKey(st.role, kind, roleKind, r)) return r;
        }
        if (st.use == "endplate.placement") {
            std::string rp = "end_plate", rb = "beam_bolts";
            const auto ip = st.args.find("rolePlate");
            if (ip != st.args.end() && !ip->second.text.empty()) rp = ip->second.text;
            const auto ib = st.args.find("roleBolts");
            if (ib != st.args.end() && !ib->second.text.empty()) rb = ib->second.text;
            if (!checkRoleKey(rp, "plate", roleKind, r)) return r;
            if (!checkRoleKey(rb, "bolt", roleKind, r)) return r;
        }
    }
    // let ifadelerini tara (tanimsiz let / bilinmeyen parametre) + dongu.
    for (const auto& l : recipe.lets)
        if (!scanExpr(specs, letNames, l.second, r)) return r;
    if (!cyclicLetCheck(recipe, r)) return r;
    return r;
}

// --- YORUMCU ----------------------------------------------------------------
GenerationPlan runRecipe(const Recipe& recipe,
                         const std::vector<CompParamSpec>& specs,
                         const ComponentInput& input) noexcept {
    GenerationPlan plan;
    plan.ok = true;
    const RecipeValidation v = validateRecipe(recipe, specs);
    if (!v.ok) {
        plan.ok = false;
        plan.error = v.message;
        return plan;
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
            if (!evaluateExpr(ctx, st.check, cv)) { plan.ok = false; plan.error = ctx.error; return plan; }
            if (cv == 0.0) {
                plan.ok = false;
                plan.error = st.message.empty() ? L"check basarisiz" : wideFromUtf8(st.message);
                return plan;
            }
            continue;
        }
        if (!st.when.empty()) {
            double wv = 0.0;
            if (!evaluateExpr(ctx, st.when, wv)) { plan.ok = false; plan.error = ctx.error; return plan; }
            if (wv == 0.0) continue;  // kosul saglanmadi -> atla
        }
        const BlockDecl* decl = findBlock(st.use);
        if (!decl) { plan.ok = false; plan.error = L"bilinmeyen blok: " + wideFromUtf8(st.use); return plan; }
        if (!decl->fn(ctx, st, plan)) { plan.ok = false; plan.error = ctx.error; return plan; }
    }
    return plan;
}

} // namespace mm
