#include "model_maker/recipe.hpp"

#include "model_maker/component_codec.hpp"  // utf8FromWide / wideFromUtf8 (param adi esleme + hata)
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
    HaunchParams haunchPrm;                           // C2: frame.contact args'tan
    std::shared_ptr<void> haunchState;                // C2: adimlar arasi opak durum
    bool haunchPrmReady{false};
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
static bool boolArg(EvalCtx& ctx, const RecipeStep& st, const char* key, bool def, bool& out) {
    const auto it = st.args.find(key);
    if (it == st.args.end()) { out = def; return true; }
    const std::string& s = it->second.text;
    if (s == "true" || s == "1" || s == "yes" || s == "on") { out = true; return true; }
    if (s == "false" || s == "0" || s == "no" || s == "off") { out = false; return true; }
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
    strArg(st, "grade", "", grade);
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

    EndPlateParams ep;
    ep.plateThickness = paramAsDouble(ctx.input.params, L"plateThickness", 12.0);
    ep.plateExtra = paramAsDouble(ctx.input.params, L"plateExtra", 20.0);
    ep.boltDiameter = paramAsDouble(ctx.input.params, L"boltDiameter", 20.0);
    ep.holeDiameter = paramAsDouble(ctx.input.params, L"holeDiameter", 22.0);
    ep.boltCountX = paramAsInt(ctx.input.params, L"boltCountX", 2);
    ep.boltCountY = paramAsInt(ctx.input.params, L"boltCountY", 4);
    ep.boltEdge = paramAsDouble(ctx.input.params, L"boltEdge", 30.0);
    {
        const std::wstring g = paramAsString(ctx.input.params, L"grade", L"8.8");
        std::string narrow;
        narrow.assign(g.begin(), g.end());
        ep.boltGrade = narrow;
    }
    ep.hasHead = paramAsBool(ctx.input.params, L"hasHead", true);
    ep.hasNut = paramAsBool(ctx.input.params, L"hasNut", true);
    ep.hasWasher = paramAsBool(ctx.input.params, L"hasWasher", true);

    const Vec3 beamAxis{beam->axisTo.x - beam->axisFrom.x,
                        beam->axisTo.y - beam->axisFrom.y,
                        beam->axisTo.z - beam->axisFrom.z};
    const EndPlatePlacement placement = computeEndPlatePlacementRotated(
        beam->axisTo, beamAxis, beam->secWidth, beam->secHeight, beam->profileRotation, ep);

    PlateSpec plate;
    plate.center = placement.plateCenter;
    plate.u = placement.u;
    plate.v = placement.v;
    plate.axis = placement.axis;
    plate.width = placement.plateWidth;
    plate.height = placement.plateHeight;
    plate.thickness = placement.plateThickness;
    plate.name = L"Alin Levhasi";
    plan.plates.push_back(plate);
    plan.plateKeys.push_back(rolePlate);

    BoltGroup bolts = placement.bolts;
    bolts.parts = {col->id};
    plan.boltGroups.push_back(bolts);
    plan.boltGroupKeys.push_back(roleBolts);
    return true;
}

// --- Haunch 40 taşları (C2) -------------------------------------------------
// frame.contact tüm parametreleri ARGS'tan okur ve HaunchParams'a yazar;
// diğer taşlar bu parametreleri paylaşır. Parametre input.params'tan gizlice
// okunmaz (args -> HaunchParams -> geçici input enjeksiyonu).
static bool blockHaunchContact(EvalCtx& ctx, const RecipeStep& st, GenerationPlan& plan) {
    HaunchParams& p = ctx.haunchPrm;
    auto gd = [&](const char* k, double d, double& o) { return numArg(ctx, st, k, d, o); };
    if (!gd("haunchLength", 450.0, p.haunchLength)) return false;
    if (!gd("haunchDepth", 240.0, p.haunchDepth)) return false;
    if (!gd("haunchTipDepth", 25.0, p.haunchTipDepth)) return false;
    if (!gd("haunchWebThickness", 10.0, p.haunchWebThickness)) return false;
    if (!gd("haunchFlangeThickness", 10.0, p.haunchFlangeThickness)) return false;
    if (!gd("endPlateThickness", 20.0, p.endPlateThickness)) return false;
    if (!gd("gap", 0.0, p.gap)) return false;
    if (!gd("boltEdgeLeft", 35.0, p.boltEdgeLeft)) return false;
    if (!gd("boltEdgeRight", 35.0, p.boltEdgeRight)) return false;
    if (!gd("boltEdge", 35.0, p.boltEdge)) return false;
    if (!gd("endPlateWidth", 0.0, p.endPlateWidth)) return false;
    if (!gd("endPlateHeight", 0.0, p.endPlateHeight)) return false;
    if (!gd("haunchFlangeWidth", 0.0, p.haunchFlangeWidth)) return false;
    if (!gd("topPlateThickness", 10.0, p.topPlateThickness)) return false;
    if (!gd("stiffenerClipSize", 15.0, p.stiffenerClipSize)) return false;
    if (!gd("stiffenerThickness", 10.0, p.stiffenerThickness)) return false;
    if (!gd("addStiffenerThickness", 10.0, p.addStiffenerThickness)) return false;
    if (!gd("rafterBottomStiffenerThickness", 10.0, p.rafterBottomStiffenerThickness)) return false;
    if (!gd("comprStiffenerThickness", 10.0, p.comprStiffenerThickness)) return false;
    if (!gd("morrisStiffenerThickness", 10.0, p.morrisStiffenerThickness)) return false;
    if (!gd("diagStiffenerThickness", 10.0, p.diagStiffenerThickness)) return false;
    if (!gd("diagStiffenerWidth", 80.0, p.diagStiffenerWidth)) return false;
    if (!gd("weldSize", 6.0, p.weldSize)) return false;
    if (!boolArg(ctx, st, "createBeamStiffener", false, p.createBeamStiffener)) return false;
    if (!boolArg(ctx, st, "createAddStiffener", false, p.createAddStiffener)) return false;
    if (!boolArg(ctx, st, "createRafterBottomStiffener", false, p.createRafterBottomStiffener)) return false;
    if (!boolArg(ctx, st, "createComprStiffener", false, p.createComprStiffener)) return false;
    if (!boolArg(ctx, st, "createMorrisStiffener", false, p.createMorrisStiffener)) return false;
    if (!boolArg(ctx, st, "createDiagStiffener", false, p.createDiagStiffener)) return false;
    if (!boolArg(ctx, st, "createWelds", true, p.createWelds)) return false;
    std::string s;
    strArg(st, "plateMaterial", "S235JR", s); p.plateMaterial = wideFromUtf8(s);
    strArg(st, "grade", "8.8", s); p.grade = wideFromUtf8(s);
    const auto grp = [&](const std::string& pre, Haunch40Group& gr) -> bool {
        double nx = 2.0, ny = 2.0;
        if (!numArg(ctx, st, (pre + "BoltCountX").c_str(), 2.0, nx)) return false;
        if (!numArg(ctx, st, (pre + "BoltCountY").c_str(), 2.0, ny)) return false;
        gr.nx = static_cast<int>(std::lround(nx));
        gr.ny = static_cast<int>(std::lround(ny));
        if (!numArg(ctx, st, (pre + "BoltPitchX").c_str(), 120.0, gr.pitchX)) return false;
        if (!numArg(ctx, st, (pre + "BoltPitchY").c_str(), 80.0, gr.pitchY)) return false;
        if (!numArg(ctx, st, (pre + "BoltDiameter").c_str(), 20.0, gr.diameter)) return false;
        if (!numArg(ctx, st, (pre + "HoleDiameter").c_str(), 22.0, gr.hole)) return false;
        if (!numArg(ctx, st, (pre + "BoltEdgeClear").c_str(), 44.0, gr.clear)) return false;
        gr.spanX = (gr.nx - 1) * gr.pitchX;
        gr.spanY = (gr.ny - 1) * gr.pitchY;
        return true;
    };
    if (!grp("upper", p.upper)) return false;
    if (!grp("lower", p.lower)) return false;
    ctx.haunchPrmReady = true;
    if (!runHaunch40Block("frame.contact", p, ctx.input, ctx.haunchState, plan)) {
        ctx.error = plan.error; return false;
    }
    return true;
}

static bool haunchBlockNoArgs(EvalCtx& ctx, const char* name, GenerationPlan& plan) {
    if (!ctx.haunchPrmReady) {
        ctx.error = wideFromUtf8(std::string("Haunch 40: once frame.contact calismali: ") + name);
        return false;
    }
    if (!runHaunch40Block(name, ctx.haunchPrm, ctx.input, ctx.haunchState, plan)) {
        ctx.error = plan.error; return false;
    }
    return true;
}

static bool blockHaunchPlumbFace(EvalCtx& ctx, const RecipeStep&, GenerationPlan& plan) {
    return haunchBlockNoArgs(ctx, "frame.plumb_face", plan);
}
static bool blockHaunchEndPlate(EvalCtx& ctx, const RecipeStep&, GenerationPlan& plan) {
    return haunchBlockNoArgs(ctx, "haunch.end_plate", plan);
}
static bool blockHaunchWeb(EvalCtx& ctx, const RecipeStep&, GenerationPlan& plan) {
    return haunchBlockNoArgs(ctx, "haunch.web", plan);
}
static bool blockHaunchFlange(EvalCtx& ctx, const RecipeStep&, GenerationPlan& plan) {
    return haunchBlockNoArgs(ctx, "haunch.flange", plan);
}
static bool blockHaunchStiffeners(EvalCtx& ctx, const RecipeStep&, GenerationPlan& plan) {
    return haunchBlockNoArgs(ctx, "haunch.stiffeners", plan);
}
static bool blockHaunchBolts(EvalCtx& ctx, const RecipeStep&, GenerationPlan& plan) {
    return haunchBlockNoArgs(ctx, "bolts.haunch_groups", plan);
}
static bool blockHaunchWelds(EvalCtx& ctx, const RecipeStep&, GenerationPlan& plan) {
    return haunchBlockNoArgs(ctx, "haunch.welds", plan);
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
        {"frame.contact",
         {"haunchLength", "haunchDepth", "haunchTipDepth", "haunchWebThickness",
          "haunchFlangeThickness", "endPlateThickness", "gap", "boltEdgeLeft",
          "boltEdgeRight", "boltEdge", "endPlateWidth", "endPlateHeight",
          "haunchFlangeWidth", "topPlateThickness", "stiffenerClipSize",
          "stiffenerThickness", "addStiffenerThickness",
          "rafterBottomStiffenerThickness", "comprStiffenerThickness",
          "morrisStiffenerThickness", "diagStiffenerThickness",
          "diagStiffenerWidth", "weldSize", "createBeamStiffener",
          "createAddStiffener", "createRafterBottomStiffener",
          "createComprStiffener", "createMorrisStiffener", "createDiagStiffener",
          "createWelds", "upperBoltCountX", "upperBoltCountY", "upperBoltPitchX",
          "upperBoltPitchY", "upperBoltDiameter", "upperHoleDiameter",
          "upperBoltEdgeClear", "lowerBoltCountX", "lowerBoltCountY",
          "lowerBoltPitchX", "lowerBoltPitchY", "lowerBoltDiameter",
          "lowerHoleDiameter", "lowerBoltEdgeClear"},
         {"plateMaterial", "grade"}, {}, &blockHaunchContact},
        {"frame.plumb_face", {}, {}, {}, &blockHaunchPlumbFace},
        {"haunch.end_plate", {}, {}, {}, &blockHaunchEndPlate},
        {"haunch.web", {}, {}, {}, &blockHaunchWeb},
        {"haunch.flange", {}, {}, {}, &blockHaunchFlange},
        {"haunch.stiffeners", {}, {}, {}, &blockHaunchStiffeners},
        {"bolts.haunch_groups", {}, {}, {}, &blockHaunchBolts},
        {"haunch.welds", {}, {}, {}, &blockHaunchWelds},
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
    for (const auto& spec : specs)
        ctx.params[utf8FromWide(spec.name)] = paramAsDouble(aligned, spec.name, spec.defDouble);
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
