#include "model_maker/component_schematic.hpp"

#include "model_maker/bolt.hpp"
#include "model_maker/component_preview.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace mm {
namespace {

constexpr double kPi = 3.14159265358979323846;

Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 add3(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 mul3(Vec3 p, double k) { return {p.x * k, p.y * k, p.z * k}; }
double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double len(Vec3 p) { return std::sqrt(dot(p, p)); }
Vec3 unit(Vec3 p) {
    const double n = len(p);
    return n > 1e-12 ? mul3(p, 1.0 / n) : Vec3{0.0, 0.0, 1.0};
}

// I-kesit 12 nokta (u = genislik yonu, v = yukseklik yonu). URETICIYLE AYNI
// kesit (b/h/tw/tf; gercek degerler, kok yaricapi yok).
std::vector<Vec3> sectionI(Vec3 center, Vec3 u, Vec3 v, double b, double h,
                           double tw, double tf) {
    b = std::max(10.0, b);
    h = std::max(10.0, h);
    tw = std::clamp(tw, 1.0, b);
    tf = std::clamp(tf, 1.0, h * 0.49);
    const double B = b * 0.5, H = h * 0.5, T = tw * 0.5;
    const Vec2 outline[12] = {
        {-B, -H}, {B, -H}, {B, -H + tf}, {T, -H + tf}, {T, H - tf}, {B, H - tf},
        {B, H},   {-B, H}, {-B, H - tf}, {-T, H - tf}, {-T, -H + tf}, {-B, -H + tf},
    };
    std::vector<Vec3> r;
    r.reserve(12);
    for (const auto& p : outline)
        r.push_back(add3(add3(center, mul3(u, p.x)), mul3(v, p.y)));
    return r;
}

// Levha yuz konturu (side = +1 on, -1 arka). Serbest kontur (egik kiris haunch
// govdesi) varsa o; yoksa dikdortgen (kose pahi sematik icin onemsiz, yok sayilir).
std::vector<Vec3> plateFace(const PlateSpec& p, double side) {
    const double o = side * p.thickness * 0.5;
    std::vector<Vec3> r;
    if (!p.outline.empty()) {
        r.reserve(p.outline.size());
        for (const auto& q : p.outline)
            r.push_back(add3(add3(p.center, mul3(p.u, q.first)),
                             add3(mul3(p.v, q.second), mul3(p.axis, o))));
        return r;
    }
    const double u = p.width * 0.5, v = p.height * 0.5;
    const Vec2 c[4] = {{-u, -v}, {u, -v}, {u, v}, {-u, v}};
    r.reserve(4);
    for (const auto& q : c)
        r.push_back(add3(add3(p.center, mul3(p.u, q.x)),
                         add3(mul3(p.v, q.y), mul3(p.axis, o))));
    return r;
}

// 2B konveks govde (monotone chain). Deterministik (kesisim/duplikasyon yok).
std::vector<Vec2> convexHull(std::vector<Vec2> pts) {
    std::sort(pts.begin(), pts.end(), [](const Vec2& a, const Vec2& b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    pts.erase(std::unique(pts.begin(), pts.end(),
                          [](const Vec2& a, const Vec2& b) {
                              return a.x == b.x && a.y == b.y;
                          }),
              pts.end());
    if (pts.size() <= 2) return pts;
    const auto crossz = [](const Vec2& o, const Vec2& a, const Vec2& b) {
        return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    };
    std::vector<Vec2> lo, hi;
    for (const auto& p : pts) {
        while (lo.size() >= 2 && crossz(lo[lo.size() - 2], lo.back(), p) <= 0)
            lo.pop_back();
        lo.push_back(p);
    }
    for (auto it = pts.rbegin(); it != pts.rend(); ++it) {
        while (hi.size() >= 2 && crossz(hi[hi.size() - 2], hi.back(), *it) <= 0)
            hi.pop_back();
        hi.push_back(*it);
    }
    lo.pop_back();
    hi.pop_back();
    lo.insert(lo.end(), hi.begin(), hi.end());
    return lo;
}

}  // namespace

SchematicDrawing buildComponentSchematic(const GenerationPlan& plan,
                                         const ComponentInput& input,
                                         SchematicViewDir view,
                                         const std::vector<PreviewDimension>& dims,
                                         const std::vector<std::wstring>* ineffective) noexcept {
    SchematicDrawing out;
    if (!plan.ok) {
        out.error = plan.error;
        return out;
    }

    Vec3 axisMain = sub(input.main.axisTo, input.main.axisFrom);
    Vec3 axisBeam{};
    if (!input.secondary.empty())
        axisBeam = sub(input.secondary.front().axisTo, input.secondary.front().axisFrom);
    axisMain = len(axisMain) > 1e-9 ? unit(axisMain) : Vec3{0.0, 0.0, 1.0};
    axisBeam = len(axisBeam) > 1e-9 ? unit(axisBeam) : Vec3{1.0, 0.0, 0.0};

    // Gorunum bazı (eX saga, eY yukari, eZ derinlik/izdusum yonu).
    Vec3 eZ, eY;
    switch (view) {
        case SchematicViewDir::Side:
            eZ = unit(axisBeam);
            eY = axisMain;
            break;
        case SchematicViewDir::Top:
            eZ = unit(axisMain);
            eY = unit(cross(axisMain, axisBeam));
            break;
        case SchematicViewDir::Front:
        default:
            eZ = unit(cross(axisMain, axisBeam));
            eY = axisMain;
            break;
    }
    if (len(eZ) < 1e-6) eZ = unit(cross(axisMain, Vec3{1.0, 0.0, 0.0}));
    if (len(eZ) < 1e-6) eZ = Vec3{0.0, 0.0, 1.0};
    eZ = unit(eZ);
    eY = len(eY) > 1e-9 ? unit(eY) : Vec3{0.0, 1.0, 0.0};
    const Vec3 eX = unit(cross(eZ, eY));

    const auto project = [&](Vec3 p) -> Vec2 { return {dot(p, eX), dot(p, eY)}; };

    // Uyeleri baglanti cevresine ±180mm kirpmak icin ozellik noktalari.
    std::vector<Vec3> featurePoints;
    for (const auto& p : plan.plates) featurePoints.push_back(p.center);
    for (const auto& g : plan.boltGroups)
        for (const Vec3 o : boltGroupBoltOrigins(g)) featurePoints.push_back(o);

    // 1) Uyeler: kesitten kurulan kisa prizma; kesme duzlemleri uc kesimine
    //    yansitilir (beam end cut alin levhasinda).
    const auto addMember = [&](const MemberGeometry& m,
                               const std::vector<CutModifier>& cuts,
                               const char* role) {
        const Vec3 axis = unit(sub(m.axisTo, m.axisFrom));
        ProfileSectionFrame sf = profileSectionFrame(axis, m.refNormal);
        Vec3 w = sf.width, h = sf.height;
        const double rot = m.profileRotation * kPi / 180.0;
        if (std::fabs(rot) > 1e-9) {
            const double cr = std::cos(rot), sr = std::sin(rot);
            const Vec3 wn = add3(mul3(w, cr), mul3(h, sr));
            const Vec3 hn = sub(mul3(h, cr), mul3(w, sr));
            w = wn;
            h = hn;
        }
        const double total = len(sub(m.axisTo, m.axisFrom));
        if (total < 1.0) return;
        double lower = total, upper = 0.0;
        for (const Vec3 pt : featurePoints) {
            const double t = dot(sub(pt, m.axisFrom), axis);
            lower = std::min(lower, t - 180.0);
            upper = std::max(upper, t + 180.0);
        }
        if (featurePoints.empty()) { lower = 0.0; upper = total; }
        lower = std::clamp(lower, 0.0, total);
        upper = std::clamp(upper, lower, total);
        const CutModifier* lowerPlane = nullptr;
        const CutModifier* upperPlane = nullptr;
        for (const auto& c : cuts) {
            if (c.target != m.id || c.kind != CutKind::PlaneCut) continue;
            const double den = dot(c.planeNormal, axis);
            if (std::fabs(den) < 1e-9) continue;
            const double t = dot(sub(c.planeOrigin, m.axisFrom), c.planeNormal) / den;
            if (c.keepPositive) {
                if (den > 0) { if (t > lower) { lower = t; lowerPlane = &c; } }
                else { if (t < upper) { upper = t; upperPlane = &c; } }
            } else {
                if (den > 0) { if (t < upper) { upper = t; upperPlane = &c; } }
                else { if (t > lower) { lower = t; lowerPlane = &c; } }
            }
        }
        lower = std::clamp(lower, 0.0, total);
        upper = std::clamp(upper, 0.0, total);
        if (upper - lower < 1.0) return;
        const auto sectionAt = [&](double t) {
            Vec3 centre = add3(m.axisFrom, mul3(axis, t));
            centre = add3(centre, mul3(h, m.sectionCenterOffset));
            return sectionI(centre, w, h, m.secWidth, m.secHeight,
                            m.secWebThickness > 0.0 ? m.secWebThickness : 8.0,
                            m.secFlangeThickness > 0.0 ? m.secFlangeThickness : 12.0);
        };
        const auto shearOntoPlane = [&](std::vector<Vec3> pts, const CutModifier* plane) {
            if (!plane) return pts;
            const double den = dot(plane->planeNormal, axis);
            if (std::fabs(den) < 1e-9) return pts;
            if (std::fabs(den) > 0.9995) return pts;
            for (auto& p : pts) {
                const double d = dot(sub(p, plane->planeOrigin), plane->planeNormal) / den;
                p = sub(p, mul3(axis, d));
            }
            return pts;
        };
        std::vector<Vec2> all;
        for (const Vec3& p : shearOntoPlane(sectionAt(upper), upperPlane))
            all.push_back(project(p));
        for (const Vec3& p : shearOntoPlane(sectionAt(lower), lowerPlane))
            all.push_back(project(p));
        const std::vector<Vec2> hull = convexHull(all);
        if (hull.size() < 3) return;
        SchematicPrim prim;
        prim.kind = SchematicPrimKind::Polygon;
        prim.pts = hull;
        prim.role = role;
        prim.filled = true;
        out.prims.push_back(std::move(prim));
    };

    // Kolon/kiris rollerini uyelik (verticality) ile coz — onizleme ile ayni.
    const auto verticality = [](const MemberGeometry& m) {
        const Vec3 d = sub(m.axisTo, m.axisFrom);
        const double n = len(d);
        return n > 1e-9 ? std::fabs(d.z) / n : 0.0;
    };
    const bool swapped = !input.secondary.empty() &&
                         verticality(input.main) + 1e-6 <
                             verticality(input.secondary.front());
    addMember(input.main, plan.cuts, swapped ? "beam" : "column");
    for (const auto& sec : input.secondary)
        addMember(sec, plan.cuts, swapped ? "column" : "beam");

    // 2) Levhalar: on + arka yuz -> izdusum -> govde -> gri dolgu.
    for (std::size_t i = 0; i < plan.plates.size(); ++i) {
        const PlateSpec& p = plan.plates[i];
        std::vector<Vec2> all;
        for (const Vec3& q : plateFace(p, +1.0)) all.push_back(project(q));
        for (const Vec3& q : plateFace(p, -1.0)) all.push_back(project(q));
        const std::vector<Vec2> hull = convexHull(all);
        if (hull.size() < 3) continue;
        SchematicPrim prim;
        prim.kind = SchematicPrimKind::Polygon;
        prim.pts = hull;
        prim.role = i < plan.plateKeys.size() ? plan.plateKeys[i] : "plate";
        prim.filled = true;
        prim.isPlate = true;
        out.prims.push_back(std::move(prim));
    }

    // 3) Bulonlar: eksene dik kesit (pul / delik / govde / bas / somun daire)
    //    — sematik konvansiyonu. Gercek cap ISO tablosundan (MM-8b/3).
    for (std::size_t gi = 0; gi < plan.boltGroups.size(); ++gi) {
        const BoltGroup& g = plan.boltGroups[gi];
        const std::string role =
            gi < plan.boltGroupKeys.size() ? plan.boltGroupKeys[gi] : "bolts";
        const BoltDimensions dim = boltDimensions(g.params.nominalDiameter);
        for (const Vec3 o : boltGroupBoltOrigins(g)) {
            const Vec2 c = project(o);
            const auto circle = [&](double dia, const std::string& d, bool dashed,
                                    bool filled) {
                SchematicPrim pr;
                pr.kind = SchematicPrimKind::Circle;
                pr.cx = c.x;
                pr.cy = c.y;
                pr.r = dia * 0.5;
                pr.role = "bolt";
                pr.detail = d;
                pr.dashed = dashed;
                pr.filled = filled;
                out.prims.push_back(std::move(pr));
            };
            {
                SchematicPrim pr;
                pr.kind = SchematicPrimKind::Annulus;
                pr.cx = c.x;
                pr.cy = c.y;
                pr.r = dim.washerOuterDiameter * 0.5;
                pr.rInner = g.params.nominalDiameter * 0.5;
                pr.role = "bolt";
                pr.detail = "washer";
                out.prims.push_back(std::move(pr));
            }
            circle(g.params.holeDiameter, "hole", /*dashed=*/true, /*filled=*/false);
            circle(g.params.nominalDiameter, "shank", false, true);
            circle(dim.headAcrossFlats, "head", false, true);
            circle(dim.nutAcrossFlats, "nut", false, true);
            (void)role;
        }
    }

    // 3b) MM-8b/5: OLCU KATMANI. PreviewDimension'lari ayni izdusumle ciz:
    //     uzatma cizgileri + olcu cizgisi + etiket. prim.role = parametre adi
    //     (odak/hover vurgusu rol eslesmesiyle). Etkisiz parametre KESIKLI.
    {
        double px0 = 1e18, py0 = 1e18, px1 = -1e18, py1 = -1e18;
        for (const auto& pr : out.prims) {
            if (pr.kind == SchematicPrimKind::Circle ||
                pr.kind == SchematicPrimKind::Annulus) {
                px0 = std::min(px0, pr.cx - pr.r);
                py0 = std::min(py0, pr.cy - pr.r);
                px1 = std::max(px1, pr.cx + pr.r);
                py1 = std::max(py1, pr.cy + pr.r);
            } else {
                for (const auto& q : pr.pts) {
                    px0 = std::min(px0, q.x);
                    py0 = std::min(py0, q.y);
                    px1 = std::max(px1, q.x);
                    py1 = std::max(py1, q.y);
                }
            }
        }
        const double span = std::max({1.0, px1 - px0, py1 - py0});
        const double off = std::max(18.0, span * 0.10);
        const Vec2 centre{(px0 + px1) * 0.5, (py0 + py1) * 0.5};
        for (const auto& d : dims) {
            const PreviewAnchor a = resolvePreviewAnchor(plan, input, d.from);
            const PreviewAnchor b = resolvePreviewAnchor(plan, input, d.to);
            if (!a.ok || !b.ok) continue;
            const Vec2 pa = project(a.point);
            const Vec2 pb = project(b.point);
            Vec2 dir{pb.x - pa.x, pb.y - pa.y};
            const double dl = std::hypot(dir.x, dir.y);
            if (dl < 1e-6) continue;
            dir = {dir.x / dl, dir.y / dl};
            Vec2 nrm{-dir.y, dir.x};
            const Vec2 mid{(pa.x + pb.x) * 0.5, (pa.y + pb.y) * 0.5};
            // Normal bbox merkezinden DISARI baksin (olcu disari cizilir).
            if ((mid.x - centre.x) * nrm.x + (mid.y - centre.y) * nrm.y < 0.0)
                nrm = {-nrm.x, -nrm.y};
            const Vec2 oa{pa.x + nrm.x * off, pa.y + nrm.y * off};
            const Vec2 ob{pb.x + nrm.x * off, pb.y + nrm.y * off};
            const bool bad = ineffective &&
                             std::find(ineffective->begin(), ineffective->end(),
                                       d.param) != ineffective->end();
            const std::string role(d.param.begin(), d.param.end());
            const auto line = [&](Vec2 s, Vec2 e) {
                SchematicPrim pr;
                pr.kind = SchematicPrimKind::Line;
                pr.pts = {s, e};
                pr.role = role;
                pr.detail = bad ? "dim_ineffective" : "dim";
                pr.dashed = bad;
                out.prims.push_back(std::move(pr));
            };
            line(pa, oa);
            line(pb, ob);
            line(oa, ob);
            SchematicPrim t;
            t.kind = SchematicPrimKind::Text;
            t.cx = (oa.x + ob.x) * 0.5;
            t.cy = (oa.y + ob.y) * 0.5 + (nrm.y >= 0.0 ? off * 0.22 : -off * 0.22);
            t.role = role;
            t.detail = bad ? "dimlabel_ineffective" : "dimlabel";
            t.text = d.label.empty() ? d.param : d.label;
            t.dashed = bad;
            out.prims.push_back(std::move(t));
        }
    }

    // 4) Sinir kutusu.
    out.minX = out.minY = std::numeric_limits<double>::max();
    out.maxX = out.maxY = std::numeric_limits<double>::lowest();
    const auto incl = [&](double x, double y) {
        out.minX = std::min(out.minX, x);
        out.minY = std::min(out.minY, y);
        out.maxX = std::max(out.maxX, x);
        out.maxY = std::max(out.maxY, y);
    };
    for (const auto& pr : out.prims) {
        switch (pr.kind) {
            case SchematicPrimKind::Polygon:
            case SchematicPrimKind::Polyline:
            case SchematicPrimKind::Line:
                for (const auto& p : pr.pts) incl(p.x, p.y);
                break;
            case SchematicPrimKind::Circle:
            case SchematicPrimKind::Annulus:
                incl(pr.cx - pr.r, pr.cy - pr.r);
                incl(pr.cx + pr.r, pr.cy + pr.r);
                break;
            case SchematicPrimKind::Text:
                incl(pr.cx, pr.cy);
                break;
        }
    }
    if (out.maxX < out.minX) {
        out.minX = out.minY = 0.0;
        out.maxX = out.maxY = 1.0;
    }
    out.ok = true;
    return out;
}

SchematicDrawing buildComponentSchematic(const GenerationPlan& plan,
                                         const ComponentInput& input,
                                         SchematicViewDir view) noexcept {
    return buildComponentSchematic(plan, input, view, {}, nullptr);
}

}  // namespace mm
