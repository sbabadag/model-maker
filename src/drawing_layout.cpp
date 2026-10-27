#include "model_maker/drawing_layout.hpp"

#include "model_maker/drawing_builder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <tuple>

namespace mm::drawing {
namespace {

constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------- geometri ilkelleri
struct Cs { double c, s; };
Cs rot(double angleDeg) {
    const double a = angleDeg * kPi / 180.0;
    return {std::cos(a), std::sin(a)};
}

double cross2(const Vec2& a, const Vec2& b) { return a.x * b.y - a.y * b.x; }

Vec2 sub2(const Vec2& a, const Vec2& b) { return {a.x - b.x, a.y - b.y}; }

std::array<Vec2, 4> corners(const Obb& o) {
    const Cs r = rot(o.angleDeg);
    const Vec2 ax{r.c, r.s}, ay{-r.s, r.c};
    return {Vec2{o.c.x - ax.x * o.hx - ay.x * o.hy, o.c.y - ax.y * o.hx - ay.y * o.hy},
            Vec2{o.c.x + ax.x * o.hx - ay.x * o.hy, o.c.y + ax.y * o.hx - ay.y * o.hy},
            Vec2{o.c.x + ax.x * o.hx + ay.x * o.hy, o.c.y + ax.y * o.hx + ay.y * o.hy},
            Vec2{o.c.x - ax.x * o.hx + ay.x * o.hy, o.c.y - ax.y * o.hx + ay.y * o.hy}};
}

bool overlapOnAxis(const std::array<Vec2, 4>& a, const std::array<Vec2, 4>& b, const Vec2& axis) {
    double amin = 1e300, amax = -1e300, bmin = 1e300, bmax = -1e300;
    for (const Vec2& p : a) { const double d = p.x * axis.x + p.y * axis.y; amin = std::min(amin, d); amax = std::max(amax, d); }
    for (const Vec2& p : b) { const double d = p.x * axis.x + p.y * axis.y; bmin = std::min(bmin, d); bmax = std::max(bmax, d); }
    return amax > bmin && bmax > amin;   // dokunma (esit) cakisma SAYILMAZ
}

bool boundsOverlap(const Bounds2& a, const Bounds2& b) {
    if (a.empty || b.empty) return false;
    return a.min.x < b.max.x && b.min.x < a.max.x && a.min.y < b.max.y && b.min.y < a.max.y;
}

Bounds2 inflateB(const Bounds2& b, double d) {
    Bounds2 o = b;
    if (o.empty) return o;
    o.min = {o.min.x - d, o.min.y - d};
    o.max = {o.max.x + d, o.max.y + d};
    return o;
}

bool within(const Bounds2& inner, const Bounds2& outer) {
    return inner.min.x >= outer.min.x && inner.min.y >= outer.min.y && inner.max.x <= outer.max.x &&
           inner.max.y <= outer.max.y;
}

} // namespace

Bounds2 Obb::aabb() const noexcept {
    Bounds2 b;
    for (const Vec2& p : corners(*this)) b.include(p);
    return b;
}

Obb obbOfRect(const Bounds2& r) noexcept {
    return {r.centre(), r.width() / 2.0, r.height() / 2.0, 0.0};
}

bool intersects(const Obb& a, const Obb& b) noexcept {
    const auto ca = corners(a), cb = corners(b);
    for (const Obb* o : {&a, &b}) {
        const Cs r = rot(o->angleDeg);
        if (!overlapOnAxis(ca, cb, {r.c, r.s}) || !overlapOnAxis(ca, cb, {-r.s, r.c})) return false;
    }
    return true;
}

bool intersects(const LineSeg& s, const Obb& b) noexcept {
    // Segmenti kutunun yerel cercevesine tasi, slab testi.
    const Cs r = rot(-b.angleDeg);
    const auto loc = [&](const Vec2& p) {
        const double dx = p.x - b.c.x, dy = p.y - b.c.y;
        return Vec2{dx * r.c - dy * r.s, dx * r.s + dy * r.c};
    };
    const Vec2 p0 = loc(s.a), p1 = loc(s.b);
    double t0 = 0.0, t1 = 1.0;
    const double d[2] = {p1.x - p0.x, p1.y - p0.y};
    const double o[2] = {p0.x, p0.y};
    const double h[2] = {b.hx, b.hy};
    for (int i = 0; i < 2; ++i) {
        if (std::fabs(d[i]) < 1e-12) {
            if (std::fabs(o[i]) >= h[i]) return false;
        } else {
            double ta = (-h[i] - o[i]) / d[i], tb = (h[i] - o[i]) / d[i];
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
            if (t0 >= t1) return false;
        }
    }
    return true;
}

bool intersects(const LineSeg& a, const LineSeg& b) noexcept {
    const Vec2 r = sub2(a.b, a.a), s = sub2(b.b, b.a);
    const double den = cross2(r, s);
    if (std::fabs(den) < 1e-12) return false;   // paralel: kesisim SAYILMAZ (kolineer binme ayri ele alinir)
    const Vec2 qp = sub2(b.a, a.a);
    const double t = cross2(qp, s) / den, u = cross2(qp, r) / den;
    return t > 1e-9 && t < 1 - 1e-9 && u > 1e-9 && u < 1 - 1e-9;
}

// ---------------------------------------------------------------- metin olcusu
double textWidthEstimate(const std::string& s, double h, bool bold) {
    double w = 0.0;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        unsigned cp = c;
        std::size_t n = 1;
        if (c >= 0xF0) { n = 4; cp = c & 0x07; }
        else if (c >= 0xE0) { n = 3; cp = c & 0x0F; }
        else if (c >= 0xC0) { n = 2; cp = c & 0x1F; }
        for (std::size_t k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        i += n;
        double f;
        if (cp >= '0' && cp <= '9') f = 0.98;
        else if (cp == '.' || cp == ',' || cp == ':' || cp == ';' || cp == '\'' || cp == ' ' || cp == '|' || cp == '!' || cp == 'i' || cp == 'l' || cp == 'I') f = 0.55;
        else if (cp == 'W' || cp == 'M' || cp == 'm' || cp == 'w' || cp == '@' || cp == 0xD8 || cp == 0xF8) f = 1.45;
        else if (cp >= 'A' && cp <= 'Z') f = 1.22;
        else if (cp >= 'a' && cp <= 'z') f = 1.08;
        else if (cp == '(' || cp == ')' || cp == '[' || cp == ']' || cp == '/' || cp == '-') f = 0.7;
        else f = 1.3;   // x, +, =, Turkce harfler, digerleri
        w += f;
    }
    return (w * (bold ? 1.1 : 1.0) * 1.08 + 0.3) * h;   // 1.08: yazi tipi cesitliligi icin guvenlik payi
}

double textBoxHeight(double h) noexcept { return 1.5 * h; }

std::string dimensionText(double v) {
    char buf[32];
    const double r = std::round(v);
    if (std::fabs(v - r) < 0.05) std::snprintf(buf, sizeof(buf), "%.0f", r);
    else std::snprintf(buf, sizeof(buf), "%.1f", v);
    return buf;
}

Bounds2 markBox(const MarkAnnotation& m) noexcept { return markLabelBox(m); }

const std::vector<double>& standardScaleDenominators() {
    static const std::vector<double> k{1, 2, 5, 10, 20, 25, 50, 75, 100, 125, 150, 200, 250, 500, 1000};
    return k;
}

// ---------------------------------------------------------------- cozumleme (cizim ve yerlesim ortak)
DimensionGeometry resolveDimension(const DrawingSheet& s, const Dimension2D& d) {
    DimensionGeometry g;
    const DrawingView* v = s.findView(d.viewId);
    if (!v || v->bounds.empty) return g;
    const double h = s.layoutConfig.dimTextHeight;
    const double bh = textBoxHeight(h);
    g.text = dimensionText(d.value);
    g.textHeight = h;
    const double w = textWidthEstimate(g.text, h) + 0.5;
    const Vec2 a = v->toPaper(d.p1), b = v->toPaper(d.p2);
    const Bounds2 pb = v->paperBounds();
    const double gap = 0.8;
    const double off = gap + bh / 2.0 + d.textLevel * (bh + gap);
    const double e1 = 1.5;   // uzatma cizgisi tasmasi

    if (d.direction == DimensionDirection::Horizontal) {
        const double e = d.side >= 0 ? -1.0 : 1.0;
        const double y = d.side >= 0 ? pb.min.y - d.offset : pb.max.y + d.offset;
        g.lines.push_back({a, {a.x, y + e * e1}});
        g.lines.push_back({b, {b.x, y + e * e1}});
        g.lines.push_back({{a.x, y}, {b.x, y}});
        g.tick1 = {{a.x - 1.2, y - 1.2}, {a.x + 1.2, y + 1.2}};
        g.tick2 = {{b.x - 1.2, y - 1.2}, {b.x + 1.2, y + 1.2}};
        const double mx = a.x + d.textT * (b.x - a.x);
        const double cy = y + d.textSide * e * off;
        g.textCentre = {mx, cy};
        g.textAngleDeg = 0.0;
        g.textBox = {g.textCentre, w / 2.0, bh / 2.0, 0.0};
        if (d.textLevel > 0) g.lines.push_back({{mx, y}, {mx, cy - d.textSide * e * (bh / 2.0)}});
    } else if (d.direction == DimensionDirection::Vertical) {
        const double e = d.side >= 0 ? 1.0 : -1.0;
        const double x = d.side >= 0 ? pb.max.x + d.offset : pb.min.x - d.offset;
        g.lines.push_back({a, {x + e * e1, a.y}});
        g.lines.push_back({b, {x + e * e1, b.y}});
        g.lines.push_back({{x, a.y}, {x, b.y}});
        g.tick1 = {{x - 1.2, a.y - 1.2}, {x + 1.2, a.y + 1.2}};
        g.tick2 = {{x - 1.2, b.y - 1.2}, {x + 1.2, b.y + 1.2}};
        const double my = a.y + d.textT * (b.y - a.y);
        const double cx = x + d.textSide * e * off;
        g.textCentre = {cx, my};
        g.textAngleDeg = 90.0;
        g.textBox = {g.textCentre, bh / 2.0, w / 2.0, 0.0};
        if (d.textLevel > 0) g.lines.push_back({{x, my}, {cx - d.textSide * e * (bh / 2.0), my}});
    } else {
        const double dx = b.x - a.x, dy = b.y - a.y;
        const double len = std::hypot(dx, dy);
        if (len < 1e-9) return g;
        const Vec2 dir{dx / len, dy / len};
        Vec2 n{-dir.y, dir.x};
        const Vec2 c = pb.centre();
        const Vec2 mid{(a.x + b.x) / 2.0, (a.y + b.y) / 2.0};
        double sgn = (n.x * (mid.x - c.x) + n.y * (mid.y - c.y)) >= 0.0 ? 1.0 : -1.0;
        sgn *= d.side >= 0 ? 1.0 : -1.0;
        n = {n.x * sgn, n.y * sgn};   // dis normal
        const Vec2 a2{a.x + n.x * d.offset, a.y + n.y * d.offset};
        const Vec2 b2{b.x + n.x * d.offset, b.y + n.y * d.offset};
        g.lines.push_back({a, {a2.x + n.x * e1, a2.y + n.y * e1}});
        g.lines.push_back({b, {b2.x + n.x * e1, b2.y + n.y * e1}});
        g.lines.push_back({a2, b2});
        g.tick1 = {{a2.x - 1.2, a2.y - 1.2}, {a2.x + 1.2, a2.y + 1.2}};
        g.tick2 = {{b2.x - 1.2, b2.y - 1.2}, {b2.x + 1.2, b2.y + 1.2}};
        double ang = std::atan2(dy, dx) * 180.0 / kPi;
        if (ang > 90.0) ang -= 180.0;
        if (ang <= -90.0) ang += 180.0;
        const Vec2 lp{a2.x + (b2.x - a2.x) * d.textT, a2.y + (b2.y - a2.y) * d.textT};
        g.textCentre = {lp.x + n.x * d.textSide * off, lp.y + n.y * d.textSide * off};
        g.textAngleDeg = ang;
        g.textBox = {g.textCentre, w / 2.0, bh / 2.0, ang};
        if (d.textLevel > 0)
            g.lines.push_back({lp, {g.textCentre.x - n.x * d.textSide * (bh / 2.0), g.textCentre.y - n.y * d.textSide * (bh / 2.0)}});
    }
    g.valid = true;
    return g;
}

TextGeometry resolveText(const DrawingSheet& s, const TextAnnotation& t) {
    (void)s;
    TextGeometry g;
    double w = 0.0;
    for (const std::string& l : t.lines) w = std::max(w, textWidthEstimate(l, t.height, t.bold));
    w += 1.0;
    const double lh = textBoxHeight(t.height);
    const double H = lh * static_cast<double>(std::max<std::size_t>(t.lines.size(), 1)) + 0.5;
    g.box = {{t.pos.x + w / 2.0, t.pos.y + H / 2.0}, w / 2.0, H / 2.0, 0.0};
    for (std::size_t i = 0; i < t.lines.size(); ++i)
        g.lineCentres.push_back({t.pos.x + 0.5, t.pos.y + H - 0.25 - (static_cast<double>(i) + 0.5) * lh});
    if (t.leader && t.hasAnchor) {
        if (const DrawingView* v = s.findView(t.viewId)) {
            const Vec2 ap = v->toPaper(t.anchor);
            const Vec2 cp{std::clamp(ap.x, t.pos.x, t.pos.x + w), std::clamp(ap.y, t.pos.y, t.pos.y + H)};
            g.hasLeader = true;
            g.leader = {ap, cp};
        }
    }
    return g;
}

namespace {

// ---------------------------------------------------------------- sahne (engeller)
struct Tagged { LineSeg s; int owner; };
struct TaggedText { Obb o; int owner; };

struct ViewObs {
    ElementId id{0};
    Bounds2 pb;
    std::vector<LineSeg> segs;
    std::vector<Obb> circles;
};

struct Scene {
    std::vector<ViewObs> views;
    Bounds2 border;      // izinli ic dikdortgen
    Bounds2 title;       // bos olabilir
    bool useTitle{true};
    std::vector<TaggedText> texts;
    std::vector<Tagged> lines;
};

ViewObs obsOf(const DrawingView& v) {
    ViewObs o;
    o.id = v.id;
    o.pb = v.paperBounds();
    for (const Segment2& s : v.segments) o.segs.push_back({v.toPaper(s.a), v.toPaper(s.b)});
    for (const Circle2& c : v.circles) {
        const Vec2 p = v.toPaper(c.center);
        const double r = c.radius * v.scale;
        o.circles.push_back({p, r, r, 0.0});
    }
    return o;
}

Scene buildViewScene(const DrawingSheet& s, int onlyView = -1) {
    Scene sc;
    for (std::size_t i = 0; i < s.views.size(); ++i) {
        if (onlyView >= 0 && static_cast<int>(i) != onlyView) continue;
        sc.views.push_back(obsOf(s.views[i]));
    }
    return sc;
}

bool textFits(const Obb& t, const Scene& sc, double clr, int owner) {
    const Bounds2 ab = t.aabb();
    if (!within(ab, inflateB(sc.border, -clr))) return false;
    const Obb ti = t.inflated(clr);
    if (sc.useTitle && !sc.title.empty && intersects(ti, obbOfRect(inflateB(sc.title, 0.0)))) return false;
    for (const ViewObs& v : sc.views)
        if (boundsOverlap(ab, inflateB(v.pb, clr))) return false;
    for (const TaggedText& o : sc.texts)
        if (o.owner != owner && intersects(ti, o.o)) return false;
    for (const Tagged& l : sc.lines)
        if (l.owner != owner && intersects(l.s, ti)) return false;
    return true;
}

bool lineFits(const LineSeg& l, const Scene& sc, double clr, int owner) {
    Bounds2 lb;
    lb.include(l.a);
    lb.include(l.b);
    if (!within(lb, inflateB(sc.border, 0.0))) return false;
    if (sc.useTitle && !sc.title.empty) {
        if (intersects(l, obbOfRect(sc.title))) return false;
    }
    for (const TaggedText& o : sc.texts)
        if (o.owner != owner && intersects(l, o.o.inflated(clr * 0.5))) return false;
    return true;
}

int crossings(const LineSeg& l, const Scene& sc, int owner) {
    int n = 0;
    for (const Tagged& o : sc.lines)
        if (o.owner != owner && intersects(l, o.s)) ++n;
    for (const ViewObs& v : sc.views)
        for (const LineSeg& g : v.segs)
            if (intersects(l, g)) ++n;
    return n;
}

// ---------------------------------------------------------------- olcu yerlesimi
struct Ctx {
    DrawingSheet* s;
    LayoutConfig cfg;
    std::vector<LayoutIssue> issues;
};

double rowPitchOf(const LayoutConfig& c) {
    return c.rowPitch > 0.0 ? c.rowPitch : textBoxHeight(c.dimTextHeight) + 2.5;
}

int ownerOfDim(const Dimension2D& d) { return static_cast<int>(d.id); }

bool dimensionTry(Ctx& cx, Scene& sc, Dimension2D& d) {
    const DrawingSheet& s = *cx.s;
    const double clr = cx.cfg.clearance;
    const DrawingView* v = s.findView(d.viewId);
    if (!v) return false;
    const Vec2 a = v->toPaper(d.p1), b = v->toPaper(d.p2);
    const double span = std::hypot(b.x - a.x, b.y - a.y);
    if (span < 1e-9) return false;
    const double w = textWidthEstimate(dimensionText(d.value), cx.cfg.dimTextHeight) + 0.5;
    struct Cand { double t; int side; int level; };
    std::vector<Cand> cands;
    const bool fitsInside = w + 1.0 <= span;
    const double tl = -(w / 2.0 + 1.5) / span, tr = 1.0 + (w / 2.0 + 1.5) / span;
    for (int lvl = 0; lvl <= 5; ++lvl) {
        std::vector<double> ts;
        if (lvl == 0) {
            if (fitsInside) ts.push_back(0.5);
            ts.push_back(tl);
            ts.push_back(tr);
        } else {
            ts.push_back(0.5);
            ts.push_back(tl);
            ts.push_back(tr);
        }
        for (const double t : ts)
            for (const int ts2 : {-1, 1}) cands.push_back({t, ts2, lvl});
    }
    for (const Cand& c : cands) {
        d.textT = c.t;
        d.textSide = c.side;
        d.textLevel = c.level;
        const DimensionGeometry g = resolveDimension(s, d);
        if (!g.valid) continue;
        // Iskelet (uzatma + olcu cizgileri) placeChain'de ZATEN sahnede; burada yalniz metin + (varsa) kilavuz.
        bool ok = textFits(g.textBox, sc, clr, ownerOfDim(d));
        if (ok && g.lines.size() > 3) ok = lineFits(g.lines[3], sc, clr, ownerOfDim(d));
        if (!ok) continue;
        // Kendi metni kendi iskeletine binmesin.
        bool selfHit = false;
        for (std::size_t i = 0; i < 3 && i < g.lines.size(); ++i)
            if (intersects(g.lines[i], g.textBox.inflated(0.2))) { selfHit = true; break; }
        if (selfHit) continue;
        sc.texts.push_back({g.textBox, ownerOfDim(d)});
        if (g.lines.size() > 3) sc.lines.push_back({g.lines[3], ownerOfDim(d)});
        return true;
    }
    return false;
}

// Bir zinciri (ayni satirdaki olculer) yerlestir; basarisizsa sahneyi geri al.
bool placeChain(Ctx& cx, Scene& sc, const std::vector<std::size_t>& idx, int side, int row) {
    DrawingSheet& s = *cx.s;
    const std::size_t nt = sc.texts.size(), nl = sc.lines.size();
    const double off = cx.cfg.firstOffset + row * rowPitchOf(cx.cfg);
    const auto rollback = [&]() {
        sc.texts.resize(nt);
        sc.lines.resize(nl);
        return false;
    };
    for (const std::size_t i : idx) {
        Dimension2D& d = s.dimensions[i];
        d.side = side;
        d.offset = off;
        d.textT = 0.5;
        d.textSide = -1;
        d.textLevel = 0;
    }
    // 1) ISKELET: zincirin tum uzatma/olcu cizgileri once sahneye (komsu metinleri bunlardan kacar).
    for (const std::size_t i : idx) {
        const Dimension2D& d = s.dimensions[i];
        const DimensionGeometry g = resolveDimension(s, d);
        if (!g.valid) return rollback();
        for (std::size_t k = 0; k < 3 && k < g.lines.size(); ++k) {
            if (!lineFits(g.lines[k], sc, cx.cfg.clearance, ownerOfDim(d))) return rollback();
            sc.lines.push_back({g.lines[k], ownerOfDim(d)});
        }
    }
    // 2) Metinler (soldan saga).
    for (const std::size_t i : idx)
        if (!dimensionTry(cx, sc, s.dimensions[i])) return rollback();
    return true;
}

// Bir gorunumun tum olculerini yerlestir (zincir = Dimension2D::chain; kucuk id = govdeye yakin).
void placeViewDimensions(Ctx& cx, Scene& sc, ElementId viewId) {
    DrawingSheet& s = *cx.s;
    std::map<ElementId, std::vector<std::size_t>> chains;   // chain id -> boyutlar
    for (std::size_t i = 0; i < s.dimensions.size(); ++i)
        if (s.dimensions[i].viewId == viewId) chains[s.dimensions[i].chain != 0 ? s.dimensions[i].chain : 1000000 + s.dimensions[i].id].push_back(i);
    // Zincir ici: cizgi boyunca soldan saga/alttan uste (deterministik).
    const DrawingView* v = s.findView(viewId);
    std::map<std::pair<int, int>, int> rows;   // (yon sinifi, taraf) -> sonraki satir
    // Sira: GENIS aralikli zincirler govdeye yakin, DAR aralikli (metni uclara tasan) zincirler DISA.
    // Neden: dis zincirin uzatma cizgileri ic satirlardan gecer; ic satirin metni araligin ORTASINDA
    // ise cizgiye carpmaz, uclara tasmissa carpar. (Deterministik: en dar aralik, sonra zincir kimligi.)
    std::vector<ElementId> chainOrder;
    {
        std::vector<std::pair<double, ElementId>> keyed;
        for (const auto& [cid, idx] : chains) {
            double minSpan = 1e300;
            for (const std::size_t i : idx) {
                const Vec2 a = v->toPaper(s.dimensions[i].p1), b = v->toPaper(s.dimensions[i].p2);
                minSpan = std::min(minSpan, std::hypot(b.x - a.x, b.y - a.y));
            }
            keyed.push_back({-std::floor(minSpan / 2.0), cid});   // 2 mm'lik kovalar: yakin aralik oncelik/kimlik sirasini korur
        }
        std::sort(keyed.begin(), keyed.end());
        for (const auto& k : keyed) chainOrder.push_back(k.second);
    }
    for (const ElementId cid : chainOrder) {
        std::vector<std::size_t>& idx = chains[cid];
        std::sort(idx.begin(), idx.end(), [&](std::size_t x, std::size_t y) {
            const Dimension2D &p = s.dimensions[x], &q = s.dimensions[y];
            const Vec2 a = v->toPaper(p.p1), b = v->toPaper(q.p1);
            return std::tie(a.x, a.y, p.id) < std::tie(b.x, b.y, q.id);
        });
        const int dirClass = static_cast<int>(s.dimensions[idx.front()].direction);
        const int firstSide = s.dimensions[idx.front()].side >= 0 ? 1 : -1;
        bool done = false;
        for (int extra = 0; extra < 3 && !done; ++extra)
            for (const int sd : {firstSide, -firstSide}) {
                int& rowRef = rows[{dirClass, sd}];
                if (placeChain(cx, sc, idx, sd, rowRef + extra)) {
                    rowRef = rowRef + extra + 1;
                    done = true;
                    break;
                }
            }
        if (!done)
            cx.issues.push_back({"DIMENSION_UNPLACEABLE",
                                 "olcu zinciri cakismasiz yerlestirilemedi (zincir " + std::to_string(cid) + ")",
                                 s.dimensions[idx.front()].id});
    }
}

// ---------------------------------------------------------------- aday tabanli metin/etiket yerlesimi
std::vector<Vec2> ringOffsets() {
    std::vector<Vec2> out;
    static const double radii[] = {4, 8, 13, 19, 26, 34, 44, 56, 70};
    for (const double r : radii)
        for (int k = 0; k < 16; ++k) {
            const double a = (kPi / 8.0) * k;
            out.push_back({r * std::cos(a), r * std::sin(a)});
        }
    return out;
}

// Anchor'li metin (callout): halka adaylari, sert filtre, en dusuk maliyet.
bool placeAnchoredText(Ctx& cx, Scene& sc, TextAnnotation& t) {
    DrawingSheet& s = *cx.s;
    const DrawingView* v = s.findView(t.viewId);
    if (!v || !t.hasAnchor) return false;
    const Vec2 ap = v->toPaper(t.anchor);
    const int owner = 500000 + static_cast<int>(t.id);
    const std::vector<Vec2> ring = ringOffsets();
    double bestCost = 1e300;
    Vec2 bestPos{};
    bool found = false;
    TextAnnotation probe = t;
    for (const Vec2& r : ring) {
        // Kutunun hangi koseden baglanacagi: yone gore (sag tarafta sol-alt, solda sag-alt...).
        TextAnnotation tmp = t;
        tmp.pos = ap;
        const TextGeometry g0 = resolveText(s, tmp);
        const double W = g0.box.hx * 2.0, H = g0.box.hy * 2.0;
        tmp.pos = {ap.x + r.x - (r.x < -1e-9 ? W : (std::fabs(r.x) <= 1e-9 ? W / 2.0 : 0.0)),
                   ap.y + r.y - (r.y < -1e-9 ? H : (std::fabs(r.y) <= 1e-9 ? H / 2.0 : 0.0))};
        const TextGeometry g = resolveText(s, tmp);
        if (!textFits(g.box, sc, cx.cfg.clearance, owner)) continue;
        if (g.hasLeader && !lineFits(g.leader, sc, cx.cfg.clearance, owner)) continue;
        double cost = std::hypot(r.x, r.y);
        if (g.hasLeader) cost += 6.0 * crossings(g.leader, sc, owner);
        if (cost < bestCost - 1e-9) {
            bestCost = cost;
            bestPos = tmp.pos;
            found = true;
        }
    }
    if (!found) return false;
    t.pos = bestPos;
    const TextGeometry g = resolveText(s, t);
    sc.texts.push_back({g.box, owner});
    if (g.hasLeader) sc.lines.push_back({g.leader, owner});
    return true;
}

// Anchor'siz metin (not/legend/damga): once notlar seridi, sonra serbest alan taramasi.
bool placeFreeText(Ctx& cx, Scene& sc, TextAnnotation& t, const Bounds2& preferred) {
    DrawingSheet& s = *cx.s;
    const int owner = 500000 + static_cast<int>(t.id);
    TextAnnotation tmp = t;
    tmp.pos = {0, 0};
    const TextGeometry g0 = resolveText(s, tmp);
    const double W = g0.box.hx * 2.0, H = g0.box.hy * 2.0;
    const auto tryRegion = [&](const Bounds2& r, double step) {
        for (double y = r.min.y; y + H <= r.max.y + 1e-9; y += step)
            for (double x = r.min.x; x + W <= r.max.x + 1e-9; x += step) {
                tmp.pos = {x, y};
                const TextGeometry g = resolveText(s, tmp);
                if (textFits(g.box, sc, cx.cfg.clearance, owner)) {
                    t.pos = tmp.pos;
                    sc.texts.push_back({g.box, owner});
                    return true;
                }
            }
        return false;
    };
    // Notlar serisinde yukaridan asagi (ust satir ilk) taranir.
    if (!preferred.empty) {
        for (double y = preferred.max.y - H; y >= preferred.min.y - 1e-9; y -= 1.0)
            for (double x = preferred.min.x; x + W <= preferred.max.x + 1e-9; x += 1.0) {
                tmp.pos = {x, y};
                const TextGeometry g = resolveText(s, tmp);
                if (textFits(g.box, sc, cx.cfg.clearance, owner)) {
                    t.pos = tmp.pos;
                    sc.texts.push_back({g.box, owner});
                    return true;
                }
            }
    }
    return tryRegion(s.drawingArea(), 2.0);
}

// Gorunum basligi: gorunumun etrafinda aday konumlar (ust-sag, ust-sol, alt-sag, alt-sol, ...).
bool placeViewTitle(Ctx& cx, Scene& sc, TextAnnotation& t) {
    DrawingSheet& s = *cx.s;
    const DrawingView* v = s.findView(t.viewId);
    if (!v) return false;
    const Bounds2 pb = v->paperBounds();
    const int owner = 500000 + static_cast<int>(t.id);
    TextAnnotation tmp = t;
    tmp.pos = {0, 0};
    const TextGeometry g0 = resolveText(s, tmp);
    const double W = g0.box.hx * 2.0, H = g0.box.hy * 2.0;
    std::vector<Vec2> cands;
    for (double lift = 1.5; lift <= 40.0; lift += 3.0) {
        cands.push_back({pb.max.x - W, pb.max.y + lift});
        cands.push_back({pb.min.x, pb.max.y + lift});
        cands.push_back({pb.max.x - W, pb.min.y - lift - H});
        cands.push_back({pb.min.x, pb.min.y - lift - H});
    }
    for (double side = 1.5; side <= 40.0; side += 3.0) {
        cands.push_back({pb.max.x + side, pb.max.y - H});
        cands.push_back({pb.min.x - side - W, pb.max.y - H});
    }
    for (const Vec2& c : cands) {
        tmp.pos = c;
        const TextGeometry g = resolveText(s, tmp);
        if (!textFits(g.box, sc, cx.cfg.clearance, owner)) continue;
        t.pos = c;
        sc.texts.push_back({g.box, owner});
        return true;
    }
    return false;
}

bool placeMark(Ctx& cx, Scene& sc, MarkAnnotation& m) {
    DrawingSheet& s = *cx.s;
    const DrawingView* v = s.findView(m.viewId);
    if (!v) return false;
    const Vec2 ap = v->toPaper(m.anchor);
    const Bounds2 vb = v->paperBounds();
    const int owner = 700000 + static_cast<int>(m.id);
    MarkAnnotation tmp = m;
    tmp.labelPos = {0, 0};
    const Bounds2 b0 = markBox(tmp);
    const double W = b0.width(), H = b0.height();
    // Adaylar: halka + gorunum kenarlari boyunca (yukari/asagi/sol/sag, kaydirmali). Maliyet: kilavuz uzunlugu + kesisim.
    std::vector<Vec2> cands;
    for (const Vec2& r : ringOffsets()) cands.push_back({ap.x + r.x - W / 2.0, ap.y + r.y - H / 2.0});
    static const double lifts[] = {2, 5, 8, 12, 16, 22, 30, 40, 55, 75};
    static const double shifts[] = {0, 8, -8, 16, -16, 24, -24, 40, -40, 60, -60, 90, -90, 130, -130};
    for (const double lift : lifts)
        for (const double sh : shifts) {
            cands.push_back({ap.x - W / 2.0 + sh, vb.max.x >= vb.min.x ? vb.max.y + lift : 0});
            cands.push_back({ap.x - W / 2.0 + sh, vb.min.y - lift - H});
            cands.push_back({vb.min.x - lift - W, ap.y - H / 2.0 + sh});
            cands.push_back({vb.max.x + lift, ap.y - H / 2.0 + sh});
        }
    double bestCost = 1e300;
    Vec2 best{};
    bool found = false;
    for (const Vec2& c : cands) {
        tmp.labelPos = c;
        const Bounds2 bb = markBox(tmp);
        const Obb box = obbOfRect(bb);
        if (!textFits(box, sc, cx.cfg.clearance, owner)) continue;
        const LineSeg lead{ap, bb.centre()};
        if (!lineFits(lead, sc, cx.cfg.clearance, owner)) continue;
        const double cost = std::hypot(lead.b.x - lead.a.x, lead.b.y - lead.a.y) + 6.0 * crossings(lead, sc, owner);
        if (cost < bestCost - 1e-9) {
            bestCost = cost;
            best = c;
            found = true;
        }
    }
    if (!found) return false;
    m.labelPos = best;
    const Bounds2 bb = markBox(m);
    sc.texts.push_back({obbOfRect(bb), owner});
    sc.lines.push_back({{ap, bb.centre()}, owner});
    return true;
}

std::string viewTitleText(const DrawingSheet& s, const DrawingView& v) {
    (void)s;
    char buf[32];
    const double den = v.scale > 0.0 ? 1.0 / v.scale : 0.0;
    std::snprintf(buf, sizeof(buf), "1:%.0f", den);
    return v.label + "  " + buf;
}

// ---------------------------------------------------------------- bir deneme
struct Margins { double l{0}, r{0}, b{0}, t{0}; };

void resetAnnotationDefaults(DrawingSheet& s) {
    for (Dimension2D& d : s.dimensions) {
        d.textT = 0.5;
        d.textSide = -1;
        d.textLevel = 0;
        d.offset = s.layoutConfig.firstOffset;
    }
    for (TextAnnotation& t : s.texts) {
        if (t.kind == TextKind::ViewTitle)
            if (const DrawingView* v = s.findView(t.viewId)) t.lines = {viewTitleText(s, *v)};
        t.pos = {0, 0};
    }
}

// Gorunum yerlesimi: ANA blok (Front/Top/Side grid, projeksiyon iliskili) + DETAY blogu (yan yana tek satir).
// Iki dizilim denenir: (A) detaylar ana blogun SAGINDA, (B) detaylar ana blogun USTUNDE. Ilk sigan secilir.
struct BlockLayout {
    double W{0}, H{0};
    std::vector<std::pair<std::size_t, Vec2>> originRel;   // gorunum -> blok-goreli origin
};

BlockLayout layoutMainBlock(const DrawingSheet& s, const std::vector<Margins>& mg, double extra) {
    BlockLayout bl;
    const bool third = s.projection == ProjectionMethod::ThirdAngle;
    std::vector<std::size_t> idx;
    std::vector<std::pair<int, int>> cellOf;
    for (std::size_t i = 0; i < s.views.size(); ++i) {
        const ViewKind k = s.views[i].kind;
        if (k == ViewKind::Detail) continue;
        idx.push_back(i);
        switch (k) {
        case ViewKind::Front: cellOf.push_back(third ? std::pair{0, 0} : std::pair{1, 1}); break;
        case ViewKind::Top: cellOf.push_back(third ? std::pair{0, 1} : std::pair{1, 0}); break;
        case ViewKind::Side: cellOf.push_back(third ? std::pair{1, 0} : std::pair{0, 1}); break;
        case ViewKind::Detail: break;
        }
    }
    if (idx.empty()) return bl;
    std::set<int> cols, rows;
    for (const auto& c : cellOf) { cols.insert(c.first); rows.insert(c.second); }
    const auto rank = [](const std::set<int>& st, int val) { return static_cast<int>(std::distance(st.begin(), st.find(val))); };
    const int nc = static_cast<int>(cols.size()), nr = static_cast<int>(rows.size());
    std::vector<double> innerW(nc, 0), innerH(nr, 0), L(nc, 0), R(nc, 0), B(nr, 0), T(nr, 0);
    for (std::size_t k = 0; k < idx.size(); ++k) {
        const DrawingView& v = s.views[idx[k]];
        const Margins& m = mg[idx[k]];
        const int ci = rank(cols, cellOf[k].first), ri = rank(rows, cellOf[k].second);
        innerW[ci] = std::max(innerW[ci], v.bounds.width() * v.scale);
        innerH[ri] = std::max(innerH[ri], v.bounds.height() * v.scale);
        L[ci] = std::max(L[ci], m.l + extra);
        R[ci] = std::max(R[ci], m.r + extra);
        B[ri] = std::max(B[ri], m.b + extra);
        T[ri] = std::max(T[ri], m.t + extra);
    }
    std::vector<double> colX(nc), rowY(nr);
    double x = 0;
    for (int c = 0; c < nc; ++c) { colX[c] = x + L[c]; x += L[c] + innerW[c] + R[c]; }
    bl.W = x;
    double y = 0;
    for (int r = 0; r < nr; ++r) { rowY[r] = y + B[r]; y += B[r] + innerH[r] + T[r]; }
    bl.H = y;
    for (std::size_t k = 0; k < idx.size(); ++k) {
        const DrawingView& v = s.views[idx[k]];
        const int ci = rank(cols, cellOf[k].first), ri = rank(rows, cellOf[k].second);
        bl.originRel.push_back({idx[k], Vec2{colX[ci] + (innerW[ci] - v.bounds.width() * v.scale) / 2.0,
                                             rowY[ri] + (innerH[ri] - v.bounds.height() * v.scale) / 2.0}});
    }
    return bl;
}

BlockLayout layoutDetailBlock(const DrawingSheet& s, const std::vector<Margins>& mg, double extra) {
    BlockLayout bl;
    double x = 0, hmax = 0;
    std::vector<std::size_t> idx;
    for (std::size_t i = 0; i < s.views.size(); ++i)
        if (s.views[i].kind == ViewKind::Detail) idx.push_back(i);
    for (const std::size_t i : idx) {
        const DrawingView& v = s.views[i];
        const Margins& m = mg[i];
        const double iw = v.bounds.width() * v.scale, ih = v.bounds.height() * v.scale;
        const double colW = m.l + extra + iw + m.r + extra;
        hmax = std::max(hmax, m.b + extra + ih + m.t + extra);
        bl.originRel.push_back({i, Vec2{x + m.l + extra, m.b + extra}});
        x += colW;
    }
    bl.W = x;
    bl.H = hmax;
    return bl;
}

bool placeViewsWithMargins(DrawingSheet& s, const std::vector<Margins>& mg, double extra) {
    const Bounds2 area = s.drawingArea();
    const double pad = s.layoutConfig.clearance * 2.0;
    const double availW = area.width() - 2 * pad, availH = area.height() - 2 * pad;
    if (availW <= 0 || availH <= 0 || s.views.empty()) return false;
    const BlockLayout main = layoutMainBlock(s, mg, extra);
    const BlockLayout det = layoutDetailBlock(s, mg, extra);
    const bool hasDet = !det.originRel.empty();
    const double x0 = area.min.x + pad, y0 = area.min.y + pad;
    const auto apply = [&](const BlockLayout& bl, double ox, double oy) {
        for (const auto& [i, o] : bl.originRel) s.views[i].origin = {ox + o.x, oy + o.y};
    };
    // (A) yan yana
    const double wA = main.W + det.W, hA = std::max(main.H, det.H);
    if (!hasDet) {
        if (main.W > availW + 1e-9 || main.H > availH + 1e-9) return false;
        apply(main, x0 + (availW - main.W) / 2.0, y0 + (availH - main.H) / 2.0);
        return true;
    }
    if (wA <= availW + 1e-9 && hA <= availH + 1e-9) {
        const double ox = x0 + (availW - wA) / 2.0, oy = y0 + (availH - hA) / 2.0;
        apply(main, ox, oy);
        apply(det, ox + main.W, oy);
        return true;
    }
    // (B) ustuste: detaylar ANA blogun ustunde (yatay ortali)
    const double wB = std::max(main.W, det.W), hB = main.H + det.H;
    if (wB <= availW + 1e-9 && hB <= availH + 1e-9) {
        const double ox = x0 + (availW - wB) / 2.0, oy = y0 + (availH - hB) / 2.0;
        apply(main, ox + (wB - main.W) / 2.0, oy);
        apply(det, ox + (wB - det.W) / 2.0, oy + main.H);
        return true;
    }
    return false;
}

// Sadece olcu/mark/baslik: bir gorunumun kendi basina annotation zarfi (komsu engelsiz).
Margins isolatedMargins(DrawingSheet s, std::size_t vi, bool* clean = nullptr) {
    // Gorunumu uzak bir noktaya koy; yalniz kendi annotation'lari.
    DrawingView& v = s.views[vi];
    v.origin = {2000.0, 2000.0};
    Ctx cx{&s, s.layoutConfig, {}};
    Scene sc;
    sc.views.push_back(obsOf(v));
    sc.border.include({0, 0});
    sc.border.include({6000, 6000});
    sc.useTitle = false;
    placeViewDimensions(cx, sc, v.id);
    if (clean) *clean = cx.issues.empty();
    Bounds2 ext;
    const Bounds2 pb = v.paperBounds();
    ext.include(pb.min);
    ext.include(pb.max);
    for (const TaggedText& t : sc.texts) { ext.include(t.o.aabb().min); ext.include(t.o.aabb().max); }
    for (const Tagged& l : sc.lines) { ext.include(l.s.a); ext.include(l.s.b); }
    Margins m;
    m.l = pb.min.x - ext.min.x + s.layoutConfig.clearance;
    m.r = ext.max.x - pb.max.x + s.layoutConfig.clearance;
    m.b = pb.min.y - ext.min.y + s.layoutConfig.clearance;
    m.t = ext.max.y - pb.max.y + s.layoutConfig.clearance;
    // Gorunum basligi icin ust pay.
    m.t = std::max(m.t, textBoxHeight(s.layoutConfig.noteTextHeight) + 3.0);
    return m;
}

struct Attempt {
    DrawingSheet sheet;
    std::vector<LayoutIssue> issues;
    bool ok{false};
};

Attempt tryLayout(const DrawingSheet& in, SheetSize size, double den) {
    Attempt at;
    at.sheet = in;
    DrawingSheet& s = at.sheet;
    const double margin = s.format.margin;
    s.format = sheetFormat(size);
    s.format.margin = margin;
    s.scale = 1.0 / den;
    s.scaleIsStandard = true;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "1:%.0f", den);
    s.titleBlock.scaleText = buf;
    for (DrawingView& v : s.views) v.scale = s.scale;
    // DETAY gorunumleri: ana olcekten ayni veya BUYUK olcek (daha kucuk payda); en buyuk makul olcek,
    // annotation'lari izole yerlesimde SORUNSUZ olan ilk standart olcek.
    {
        const Bounds2 area = s.drawingArea();
        const double maxPaper = std::min(130.0, 0.6 * std::min(area.width(), area.height()));
        for (std::size_t i = 0; i < s.views.size(); ++i) {
            DrawingView& v = s.views[i];
            if (v.kind != ViewKind::Detail) continue;
            double chosen = den;
            for (const double dd : standardScaleDenominators()) {
                if (dd < 2.0) continue;
                if (dd > den) break;
                if (std::max(v.bounds.width(), v.bounds.height()) / dd > maxPaper) continue;
                v.scale = 1.0 / dd;
                v.origin = {2000.0, 2000.0};
                resetAnnotationDefaults(s);
                bool clean = false;
                isolatedMargins(s, i, &clean);
                if (clean) { chosen = dd; break; }
            }
            v.scale = 1.0 / chosen;
        }
    }
    resetAnnotationDefaults(s);

    std::vector<Margins> mg;
    for (std::size_t i = 0; i < s.views.size(); ++i) {
        s.views[i].origin = {2000.0, 2000.0};
        bool clean = true;
        mg.push_back(isolatedMargins(s, i, &clean));
    }
    const int maxIt = std::max(1, s.layoutConfig.maxIterations);
    for (int it = 0; it < maxIt; ++it) {
        at.issues.clear();
        if (!placeViewsWithMargins(s, mg, 6.0 * it)) {
            at.issues.push_back({"NO_FIT", "gorunumler + annotation payi secili kagit/olcege sigmiyor", 0});
            return at;
        }
        Ctx cx{&s, s.layoutConfig, {}};
        Scene sc = buildViewScene(s);
        sc.border.include({s.format.margin, s.format.margin});
        sc.border.include({s.format.width - s.format.margin, s.format.height - s.format.margin});
        sc.title = s.titleBlockRect();
        for (Dimension2D& d : s.dimensions) { d.textT = 0.5; d.textSide = -1; d.textLevel = 0; }
        // 1) olculer (gorunum sirasiyla)
        for (const DrawingView& v : s.views) placeViewDimensions(cx, sc, v.id);
        // 2) mark etiketleri
        for (MarkAnnotation& m : s.marks)
            if (!placeMark(cx, sc, m))
                cx.issues.push_back({"ELEMENT_UNPLACEABLE", "mark etiketi cakismasiz yerlestirilemedi: " + m.text, m.id});
        // 3) callout'lar (anchor'li)
        for (TextAnnotation& t : s.texts)
            if (t.kind != TextKind::ViewTitle && t.hasAnchor && t.viewId != 0)
                if (!placeAnchoredText(cx, sc, t))
                    cx.issues.push_back({"ELEMENT_UNPLACEABLE", "callout/not cakismasiz yerlestirilemedi", t.id});
        // 4) gorunum basliklari
        for (TextAnnotation& t : s.texts)
            if (t.kind == TextKind::ViewTitle)
                if (!placeViewTitle(cx, sc, t))
                    cx.issues.push_back({"ELEMENT_UNPLACEABLE", "gorunum basligi yerlestirilemedi", t.id});
        // 5) sayfa duzeyi notlar (damga, legend, checklist)
        for (TextAnnotation& t : s.texts)
            if (t.kind != TextKind::ViewTitle && !(t.hasAnchor && t.viewId != 0))
                if (!placeFreeText(cx, sc, t, s.notesArea()))
                    cx.issues.push_back({"ELEMENT_UNPLACEABLE", "sayfa notu cakismasiz yerlestirilemedi", t.id});
        at.issues = cx.issues;
        if (at.issues.empty()) {
            at.ok = true;
            return at;
        }
        // Daha genis gorunum payi ile tekrar (gorunumler arasi bosluk buyur).
    }
    return at;
}

} // namespace

// ---------------------------------------------------------------- bagimsiz olcum
OverlapReport measureOverlaps(const DrawingSheet& s) {
    OverlapReport rep;
    struct T { Obb o; int owner; std::string what; };
    std::vector<T> texts;
    std::vector<Tagged> lines;
    std::vector<std::string> lineWhat;
    for (const Dimension2D& d : s.dimensions) {
        const DimensionGeometry g = resolveDimension(s, d);
        if (!g.valid) continue;
        texts.push_back({g.textBox, static_cast<int>(d.id), "dim#" + std::to_string(d.id) + "[" + g.text + "]"});
        for (const LineSeg& l : g.lines) { lines.push_back({l, static_cast<int>(d.id)}); lineWhat.push_back("dim#" + std::to_string(d.id)); }
        if (g.textHeight + 1e-9 < s.layoutConfig.minDimText) {
            ++rep.tooSmall;
            rep.details.push_back("dim text too small #" + std::to_string(d.id));
        }
    }
    for (const TextAnnotation& t : s.texts) {
        const TextGeometry g = resolveText(s, t);
        const int owner = 500000 + static_cast<int>(t.id);
        texts.push_back({g.box, owner, "text#" + std::to_string(t.id)});
        if (g.hasLeader) { lines.push_back({g.leader, owner}); lineWhat.push_back("leader#" + std::to_string(t.id)); }
        if (t.height + 1e-9 < s.layoutConfig.minNoteText) {
            ++rep.tooSmall;
            rep.details.push_back("note text too small #" + std::to_string(t.id));
        }
    }
    for (const MarkAnnotation& m : s.marks) {
        const DrawingView* v = s.findView(m.viewId);
        const int owner = 700000 + static_cast<int>(m.id);
        const Bounds2 bb = markBox(m);
        texts.push_back({obbOfRect(bb), owner, "mark#" + std::to_string(m.id)});
        if (v) { lines.push_back({{v->toPaper(m.anchor), bb.centre()}, owner}); lineWhat.push_back("markleader#" + std::to_string(m.id)); }
    }
    const Bounds2 title = s.titleBlockRect();
    Bounds2 border;
    border.include({s.format.margin, s.format.margin});
    border.include({s.format.width - s.format.margin, s.format.height - s.format.margin});
    std::vector<ViewObs> vobs;
    for (const DrawingView& v : s.views) vobs.push_back(obsOf(v));

    for (std::size_t i = 0; i < texts.size(); ++i) {
        for (std::size_t j = i + 1; j < texts.size(); ++j)
            if (intersects(texts[i].o, texts[j].o)) {
                ++rep.textText;
                rep.details.push_back("text/text " + texts[i].what + " x " + texts[j].what);
            }
        for (std::size_t k = 0; k < lines.size(); ++k)
            if (lines[k].owner != texts[i].owner && intersects(lines[k].s, texts[i].o)) {
                ++rep.textLine;
                rep.details.push_back("text/line " + texts[i].what + " x " + lineWhat[k]);
            }
        const Bounds2 ab = texts[i].o.aabb();
        for (const ViewObs& v : vobs) {
            if (boundsOverlap(ab, v.pb)) {
                ++rep.textView;
                rep.details.push_back("text/view " + texts[i].what);
            }
            if (boundsOverlap(ab, inflateB(v.pb, 0.0)))
                for (const LineSeg& g : v.segs)
                    if (intersects(g, texts[i].o)) { ++rep.textGeometry; rep.details.push_back("text/geometry " + texts[i].what); break; }
        }
        if (boundsOverlap(ab, title)) { ++rep.textTitle; rep.details.push_back("text/title " + texts[i].what); }
        if (!within(ab, border)) { ++rep.outsideBorder; rep.details.push_back("outside border " + texts[i].what); }
    }
    for (std::size_t a = 0; a < vobs.size(); ++a) {
        for (std::size_t b = a + 1; b < vobs.size(); ++b)
            if (boundsOverlap(vobs[a].pb, vobs[b].pb)) { ++rep.textView; rep.details.push_back("view/view overlap"); }
        if (boundsOverlap(vobs[a].pb, title)) { ++rep.textTitle; rep.details.push_back("view/title overlap"); }
        if (!within(vobs[a].pb, border)) { ++rep.outsideBorder; rep.details.push_back("view outside border"); }
    }
    return rep;
}

bool layoutSheet(DrawingSheet& s) {
    LayoutReport rep;
    rep.ran = true;
    rep.requestedSheet = s.format.size;
    const LayoutConfig& cfg = s.layoutConfig;
    if (cfg.dimTextHeight + 1e-9 < cfg.minDimText || cfg.noteTextHeight + 1e-9 < cfg.minNoteText) {
        rep.issues.push_back({"TEXT_TOO_SMALL",
                              "yapilandirilan yazi yuksekligi okunabilir minimumun altinda; yerlesim reddedildi (yazi kucultulmez)", 0});
        s.layout = rep;
        return false;
    }
    for (const TextAnnotation& t : s.texts)
        if (t.height + 1e-9 < cfg.minNoteText) {
            rep.issues.push_back({"TEXT_TOO_SMALL", "not/callout yuksekligi minimumun altinda", t.id});
            s.layout = rep;
            return false;
        }
    std::vector<SheetSize> sizes;
    const SheetSize first = std::max(s.format.size, cfg.minSheet);
    for (SheetSize z : {SheetSize::A4, SheetSize::A3, SheetSize::A2, SheetSize::A1, SheetSize::A0})
        if (z >= first && z <= cfg.maxSheet) sizes.push_back(z);
    if (sizes.empty()) sizes.push_back(first);
    std::vector<double> dens;
    if (cfg.fixedScaleDen > 0.0) dens.push_back(cfg.fixedScaleDen);
    else dens = standardScaleDenominators();

    Attempt best;
    bool haveBest = false;
    int attempts = 0;
    int escal = 0;
    for (const SheetSize z : sizes) {
        for (const double den : dens) {
            ++attempts;
            Attempt at = tryLayout(s, z, den);
            if (at.ok) {
                const OverlapReport m = measureOverlaps(at.sheet);
                if (m.total() == 0) {
                    rep.ok = true;
                    rep.finalSheet = z;
                    rep.scaleDenominator = den;
                    rep.attempts = attempts;
                    rep.escalations = escal;
                    at.sheet.layout = rep;
                    s = std::move(at.sheet);
                    return true;
                }
                for (const std::string& d : m.details) at.issues.push_back({"MEASURED_OVERLAP", d, 0});
                at.ok = false;
            }
            const auto score = [](const Attempt& a) {
                std::size_t n = a.issues.size();
                for (const LayoutIssue& i : a.issues) if (i.code == "NO_FIT") n += 100000;   // yerlestirilemeyen deneme son tercih
                return n;
            };
            if (!haveBest || score(at) < score(best)) {
                best = std::move(at);
                haveBest = true;
                rep.finalSheet = z;
                rep.scaleDenominator = den;
            }
        }
        ++escal;
    }
    rep.attempts = attempts;
    rep.escalations = std::max(0, escal - 1);
    rep.ok = false;
    const DrawingSheet original = s;
    if (haveBest) rep.issues = best.issues;
    if (rep.issues.empty()) rep.issues.push_back({"NO_FIT", "yerlesim basarisiz", 0});
    const bool noFitOnly = std::all_of(rep.issues.begin(), rep.issues.end(), [](const LayoutIssue& i) { return i.code == "NO_FIT"; });
    if (haveBest && !noFitOnly) {
        s = std::move(best.sheet);
    } else {
        // Hicbir kagit/olcege sigmadi: P1 dogrusal yerlesimi onizleme icin koru (CIZIM ONAYSIZ kalir).
        s = original;
        DrawingOptions o;
        o.sheet = cfg.maxSheet;
        const SheetSize keep = s.format.size;
        const double margin = s.format.margin;
        s.format = sheetFormat(cfg.maxSheet);
        s.format.margin = margin;
        (void)keep;
        placeViews(s, o);
        rep.finalSheet = cfg.maxSheet;
    }
    s.layout = rep;
    return false;
}

} // namespace mm::drawing
