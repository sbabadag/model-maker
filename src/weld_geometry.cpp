#ifdef MM_HAS_OCC

#include "model_maker/weld_geometry.hpp"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <Geom_Curve.hxx>
#include <Geom_Line.hxx>
#include <Geom_Plane.hxx>
#include <Geom_Surface.hxx>
#include <gp_Pln.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>

#include <algorithm>
#include <cmath>

namespace mm {

namespace {

gp_Pnt toPnt(const Vec3& v) { return gp_Pnt(v.x, v.y, v.z); }
Vec3 fromDir(const gp_Dir& d) { return Vec3{d.X(), d.Y(), d.Z()}; }
Vec3 fromPnt(const gp_Pnt& p) { return Vec3{p.X(), p.Y(), p.Z()}; }

double vecLen(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
Vec3 normalized(const Vec3& v) {
    const double l = vecLen(v);
    if (l < 1e-12) return Vec3{0, 0, 0};
    return Vec3{v.x / l, v.y / l, v.z / l};
}
Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

double pointToShapeDistance(const Vec3& p, const TopoDS_Shape& shape) {
    if (shape.IsNull()) return 1e18;
    try {
        TopoDS_Shape vtx = BRepBuilderAPI_MakeVertex(toPnt(p)).Vertex();
        BRepExtrema_DistShapeShape ext(vtx, shape);
        if (!ext.IsDone() || ext.NbSolution() == 0) return 1e18;
        return ext.Value();
    } catch (...) {
        return 1e18;
    }
}

// Planar bir yuzun DISA yonlu normali (kati yoneliminden; TopAbs_REVERSED ise
// ters cevrilir — standard OCCT yontemi). Planar degilse false.
bool outwardPlanarNormal(const TopoDS_Face& face, Vec3& out) {
    Handle(Geom_Surface) surf = BRep_Tool::Surface(face);
    if (surf.IsNull()) return false;
    Handle(Geom_Plane) plane = Handle(Geom_Plane)::DownCast(surf);
    if (plane.IsNull()) return false;
    gp_Dir n = plane->Pln().Axis().Direction();
    Vec3 r = fromDir(n);
    if (face.Orientation() == TopAbs_REVERSED) r = Vec3{-r.x, -r.y, -r.z};
    out = r;
    return true;
}

// `p` noktasini iceren (tol) DISA yonlu planar yuz normali. Birden fazla yuz
// iceriyorsa `refNormal`e DIK olan tercih edilir (kose kaynak yuzu); dik yuz
// yoksa ilk bulunan doner. Bulunamazsa false.
// `weldDir` kaynak ilerleme yonu: yuz bu yon boyunca UZANMALI (normali weldDir'e
// DIK) — kaynak kesiti weldDir'e dik duzlemde cizilir; weldDir'e paralel normali
// olan "uc" yuzu (or. kirisin temas yuzu) kesitte NOKTA olur ve cross(weldDir,n)
// dejenere olur (fillet kati uretilemez). weldDir sifirsa bu filtre devre disi.
bool cornerFaceNormal(const TopoDS_Shape& shape, const Vec3& p, double tol,
                      const Vec3& refNormal, const Vec3& weldDir, Vec3& out) {
    const gp_Pnt pt = toPnt(p);
    const Vec3 wd = normalized(weldDir);
    Vec3 fallback; bool haveFallback = false;
    for (TopExp_Explorer fe(shape, TopAbs_FACE); fe.More(); fe.Next()) {
        const TopoDS_Face face = TopoDS::Face(fe.Current());
        Vec3 n;
        if (!outwardPlanarNormal(face, n)) continue;
        Handle(Geom_Surface) surf = BRep_Tool::Surface(face);
        Handle(Geom_Plane) plane = Handle(Geom_Plane)::DownCast(surf);
        if (plane.IsNull() || plane->Pln().Distance(pt) > tol) continue;
        // W3-FIX: weldDir'e PARALEL normali olan yuzu disla (kesit dejenere olur).
        if (vecLen(wd) > 1e-6 && std::fabs(dot(n, wd)) > 1e-3) continue;
        if (!haveFallback) { fallback = n; haveFallback = true; }
        // refNormal'e DIK yuz tercih edilir (kose kaynagi bu yuzde).
        if (std::fabs(dot(n, refNormal)) < 1e-3) {
            out = n;
            return true;
        }
    }
    if (haveFallback) { out = fallback; return true; }
    return false;
}

} // namespace

std::vector<WeldJunction> findWeldJunctions(const TopoDS_Shape& from,
                                            const TopoDS_Shape& onto,
                                            double tol) {
    std::vector<WeldJunction> out;
    if (from.IsNull() || onto.IsNull()) return out;
    for (TopExp_Explorer ee(from, TopAbs_EDGE); ee.More(); ee.Next()) {
        const TopoDS_Edge edge = TopoDS::Edge(ee.Current());
        double first = 0.0, last = 0.0;
        Handle(Geom_Curve) curve = BRep_Tool::Curve(edge, first, last);
        if (curve.IsNull()) continue;
        Handle(Geom_Line) line = Handle(Geom_Line)::DownCast(curve);
        if (line.IsNull()) continue;  // yalniz dogrusal kenarlar
        const gp_Pnt s = line->Value(first);
        const gp_Pnt e = line->Value(last);
        const Vec3 start = fromPnt(s), end = fromPnt(e);
        const double len = vecLen(Vec3{end.x - start.x, end.y - start.y, end.z - start.z});
        if (len < 1e-9) continue;
        const Vec3 mid{(start.x + end.x) / 2, (start.y + end.y) / 2, (start.z + end.z) / 2};
        if (pointToShapeDistance(start, onto) > tol) continue;
        if (pointToShapeDistance(mid, onto) > tol) continue;
        if (pointToShapeDistance(end, onto) > tol) continue;
        WeldJunction j;
        j.valid = true;
        j.start = start;
        j.end = end;
        j.direction = normalized(Vec3{end.x - start.x, end.y - start.y, end.z - start.z});
        j.length = len;
        out.push_back(std::move(j));
    }
    // Yuz dikiş (seam) kenarlari nedeniyle ayni dogru parcasi iki kez rapor
    // edilebilir (yuz sinirlari + seam cakismasi). Ayri ayri yonlendirilmis
    // ayni parcayi tekillestir (baslangic/bitis tol icinde esitse yinelenir).
    {
        std::vector<WeldJunction> deduped;
        for (const auto& j : out) {
            bool dup = false;
            for (const auto& d : deduped) {
                const auto ptsClose = [&](const Vec3& a, const Vec3& b) {
                    return vecLen(Vec3{a.x - b.x, a.y - b.y, a.z - b.z}) < tol;
                };
                if ((ptsClose(j.start, d.start) && ptsClose(j.end, d.end)) ||
                    (ptsClose(j.start, d.end) && ptsClose(j.end, d.start))) {
                    dup = true;
                    break;
                }
            }
            if (!dup) deduped.push_back(j);
        }
        out = std::move(deduped);
    }
    std::sort(out.begin(), out.end(),
              [](const WeldJunction& a, const WeldJunction& b) { return a.length > b.length; });
    return out;
}

WeldJunction findBestWeldJunction(const TopoDS_Shape& main,
                                  const TopoDS_Shape& secondary,
                                  double tol) {
    WeldJunction none;
    none.valid = false;
    none.reason = "uygun dogrusal temas kenari bulunamadi";
    std::vector<WeldJunction> cands = findWeldJunctions(secondary, main, tol);
    std::vector<WeldJunction> cands2 = findWeldJunctions(main, secondary, tol);
    cands.insert(cands.end(), cands2.begin(), cands2.end());
    if (cands.empty()) return none;
    WeldJunction best = cands.front();
    cornerFaceNormal(main, best.start, tol, Vec3{0, 0, 0}, best.direction, best.mainNormal);
    if (vecLen(best.mainNormal) > 1e-6)
        cornerFaceNormal(secondary, best.start, tol, best.mainNormal, best.direction, best.secNormal);
    return best;
}

TopoDS_Shape buildFilletWeldSolid(const Vec3& start, const Vec3& end,
                                  const Vec3& mainNormal, const Vec3& secNormal,
                                  double leg, bool aboveSide) {
    if (leg <= 0.0) return TopoDS_Shape();
    const Vec3 d = normalized(Vec3{end.x - start.x, end.y - start.y, end.z - start.z});
    const double len = vecLen(Vec3{end.x - start.x, end.y - start.y, end.z - start.z});
    if (len < 1e-9 || vecLen(d) < 1e-6) return TopoDS_Shape();
    Vec3 nm = normalized(mainNormal), ns = normalized(secNormal);
    if (vecLen(nm) < 1e-6 || vecLen(ns) < 1e-6) return TopoDS_Shape();
    Vec3 tm = cross(d, nm);
    Vec3 ts = cross(d, ns);
    if (vecLen(tm) < 1e-6 || vecLen(ts) < 1e-6) return TopoDS_Shape();
    tm = normalized(tm);
    ts = normalized(ts);
    const Vec3 bis = normalized(Vec3{nm.x + ns.x, nm.y + ns.y, nm.z + ns.z});
    if (vecLen(bis) < 1e-6) return TopoDS_Shape();  // zit normaller — destek yok
    if (dot(tm, bis) < 0.0) tm = Vec3{-tm.x, -tm.y, -tm.z};
    if (dot(ts, bis) < 0.0) ts = Vec3{-ts.x, -ts.y, -ts.z};
    if (!aboveSide) {
        tm = Vec3{-tm.x, -tm.y, -tm.z};
        ts = Vec3{-ts.x, -ts.y, -ts.z};
    }
    const gp_Pnt O = toPnt(start);
    const gp_Pnt A = toPnt(Vec3{start.x + tm.x * leg, start.y + tm.y * leg, start.z + tm.z * leg});
    const gp_Pnt B = toPnt(Vec3{start.x + ts.x * leg, start.y + ts.y * leg, start.z + ts.z * leg});
    try {
        BRepBuilderAPI_MakePolygon poly(O, A, B);
        poly.Close();
        TopoDS_Wire wire = poly.Wire();
        BRepBuilderAPI_MakeFace mkFace(wire);
        if (!mkFace.IsDone()) return TopoDS_Shape();
        const gp_Vec sweep(d.x * len, d.y * len, d.z * len);
        BRepPrimAPI_MakePrism prism(mkFace.Face(), sweep);
        if (!prism.IsDone()) return TopoDS_Shape();
        TopoDS_Shape solid = prism.Shape();
        BRepCheck_Analyzer an(solid);
        if (!an.IsValid()) return TopoDS_Shape();
        return solid;
    } catch (...) {
        return TopoDS_Shape();
    }
}

std::vector<TopoDS_Shape> buildWeldSolids(const Weld& w,
                                          const TopoDS_Shape& mainShape,
                                          const TopoDS_Shape& secondaryShape,
                                          double leg,
                                          std::string& outUnsupportedReason) {
    outUnsupportedReason.clear();
    if (w.type != WeldType::Fillet) {
        outUnsupportedReason = "butt kaynak katisi bu surumde uretilmiyor (agiz/nufuziyet bilinmiyor)";
        return {};
    }
    if (leg <= 0.0) {
        outUnsupportedReason = "gecersiz kaynak olcusu";
        return {};
    }
    const double tol = 0.5;
    std::vector<WeldJunction> juncts = findWeldJunctions(secondaryShape, mainShape, tol);
    if (juncts.empty()) juncts = findWeldJunctions(mainShape, secondaryShape, tol);
    if (juncts.empty()) {
        outUnsupportedReason = "uygun dogrusal temas kenari bulunamadi";
        return {};
    }
    std::vector<WeldJunction> chosen;
    chosen.push_back(juncts[0]);
    if (w.doubleSided && juncts.size() >= 2) chosen.push_back(juncts[1]);
    std::vector<TopoDS_Shape> solids;
    for (const auto& j : chosen) {
        Vec3 nm, ns;
        cornerFaceNormal(mainShape, j.start, tol, Vec3{0, 0, 0}, j.direction, nm);
        if (vecLen(nm) < 1e-6) { outUnsupportedReason = "ana parca temas yuzu normali cozumlenemedi"; return {}; }
        cornerFaceNormal(secondaryShape, j.start, tol, nm, j.direction, ns);
        if (vecLen(ns) < 1e-6) { outUnsupportedReason = "ikincil parca temas yuzu normali cozumlenemedi"; return {}; }
        TopoDS_Shape s = buildFilletWeldSolid(j.start, j.end, nm, ns, leg, true);
        if (s.IsNull()) {
            outUnsupportedReason = "kaynak kati uretilemedi (BRepCheck gecersiz)";
            return {};
        }
        solids.push_back(s);
    }
    return solids;
}

} // namespace mm

#endif // MM_HAS_OCC
