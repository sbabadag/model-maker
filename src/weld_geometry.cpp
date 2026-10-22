#ifdef MM_HAS_OCC

#include "model_maker/weld_geometry.hpp"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
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
#include <sstream>
#include <utility>
#include <vector>

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

// Nokta katinin ICINDE mi? (kaynak katisi yanlis tarafa dusunce yakalamak icin)
bool pointInsideShape(const Vec3& p, const TopoDS_Shape& shape) {
    if (shape.IsNull()) return false;
    try {
        BRepClass3d_SolidClassifier cls(shape);
        cls.Perform(toPnt(p), 1e-6);
        return cls.State() == TopAbs_IN;
    } catch (...) {
        return false;
    }
}

// Katinin hacim merkezi (kaynak katisinin nereye dustugunu olcmek icin).
Vec3 shapeCentroid(const TopoDS_Shape& shape) {
    if (shape.IsNull()) return Vec3{0, 0, 0};
    try {
        GProp_GProps props;
        BRepGProp::VolumeProperties(shape, props);
        if (props.Mass() <= 1e-9) return Vec3{0, 0, 0};
        const gp_Pnt c = props.CentreOfMass();
        return Vec3{c.X(), c.Y(), c.Z()};
    } catch (...) {
        return Vec3{0, 0, 0};
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

// p noktasina tol icinde kalan (GERCEK yuz sinirlari icinde — sinirsiz duzlem
// degil) planar yuzler arasindan, normali `preferToward` yonune en iyi bakan
// yuzu secer. Kose noktalarinda iki duzlem birden gecer; duzlem mesafesi tek
// basina yetmez, bu yuzden gercek yuz mesafesi + yon tercihi kullanilir.
// Uygun TUM yuz normallerini, `preferToward` yonune bakma skoruna gore azalan
// sirada dondurur. Kose noktasinda secim tek adayla sinirli kalmasin — her aday
// kesit kurulup geometriyle dogrulanir.
std::vector<Vec3> candidateFaceNormals(const TopoDS_Shape& shape, const Vec3& p, double tol,
                                       const Vec3& weldDir, const Vec3& preferToward) {
    const Vec3 wd = normalized(weldDir);
    const Vec3 pref = normalized(preferToward);
    std::vector<std::pair<double, Vec3>> scored;
    for (TopExp_Explorer fe(shape, TopAbs_FACE); fe.More(); fe.Next()) {
        const TopoDS_Face face = TopoDS::Face(fe.Current());
        Vec3 n;
        if (!outwardPlanarNormal(face, n)) continue;
        if (vecLen(wd) > 1e-6 && std::fabs(dot(n, wd)) > 1e-3) continue;
        if (pointToShapeDistance(p, face) > tol) continue;
        bool dup = false;
        for (const auto& s : scored)
            if (std::fabs(dot(s.second, n)) > 1.0 - 1e-6) { dup = true; break; }
        if (dup) continue;
        scored.emplace_back(vecLen(pref) > 1e-6 ? dot(n, pref) : 0.0, n);
    }
    std::sort(scored.begin(), scored.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<Vec3> out;
    out.reserve(scored.size());
    for (const auto& s : scored) out.push_back(s.second);
    return out;
}

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
                                          std::string& outUnsupportedReason,
                                          double* outSeamLength) {
    outUnsupportedReason.clear();
    if (outSeamLength) *outSeamLength = 0.0;
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
        // Teshis: iki katı arasindaki EN KISA mesafeyi olcup nedene ekle.
        // "0.8mm" -> tolerans sorunu, "150mm" -> yerlesim/kesit olcusu hatasi.
        double gap = -1.0;
        if (BRepExtrema_DistShapeShape dist(mainShape, secondaryShape); dist.Perform()) {
            if (dist.IsDone() && dist.NbSolution() > 0) gap = dist.Value();
        }
        std::ostringstream ss;
        ss << "secili iki parca birbirine TEMAS ETMIYOR (dogrusal temas kenari yok)";
        if (gap >= 0.0) {
            ss.setf(std::ios::fixed);
            ss.precision(2);
            ss << "; en yakin mesafe " << gap << " mm";
        }
        ss << " — kaynak icin birbirine degen iki parca secin";
        outUnsupportedReason = ss.str();
        return {};
    }
    // TAM CEVRE KAYNAGI: temas eden GERCEK her dogrusal kenara fillet uretilir
    // (levha-kolon baglantisinda 4 kenar; kiris-kolon T'sinde kiris ucu cevresi).
    // Eski davranis en UZUN tek kenari uretip geri kalanlari atiyordu — kullanicinin
    // gordugu "bir kenarda kaynak var, digerlerinde yok" hatasi buydu. Her adayda
    // kesit kurulup hacim merkezi PARCA ICINDE kalan yerlesim hâla reddedilir
    // (parca kosesinde iki farkli yuz ayni noktadan gecerse duzlem mesafesi
    // ikisini de "temas" sayar — geometri karar verir).
    std::vector<TopoDS_Shape> solids;
    const Vec3 secCentroid = shapeCentroid(secondaryShape);
    const Vec3 mainCentroid = shapeCentroid(mainShape);
    std::string lastReason;
    double totalSeam = 0.0;
    for (std::size_t ji = 0; ji < juncts.size(); ++ji) {
        const WeldJunction& j = juncts[ji];
        const Vec3 towardSec{secCentroid.x - j.start.x, secCentroid.y - j.start.y,
                             secCentroid.z - j.start.z};
        const Vec3 towardMain{mainCentroid.x - j.start.x, mainCentroid.y - j.start.y,
                              mainCentroid.z - j.start.z};
        const std::vector<Vec3> mainCands =
            candidateFaceNormals(mainShape, j.start, tol, j.direction, towardSec);
        if (mainCands.empty()) {
            lastReason = "ana parca temas yuzu normali cozumlenemedi";
            continue;
        }
        // Collect solids from both sides when possible
        std::vector<TopoDS_Shape> builtSides;
        for (const Vec3& nm : mainCands) {
            std::vector<Vec3> secCands =
                candidateFaceNormals(secondaryShape, j.start, tol, j.direction, towardMain);
            if (secCands.empty()) {
                Vec3 ns;
                if (cornerFaceNormal(secondaryShape, j.start, tol, nm, j.direction, ns))
                    secCands.push_back(ns);
            }
            if (secCands.empty()) {
                lastReason = "ikincil parca temas yuzu normali cozumlenemedi";
                continue;
            }
            for (const Vec3& ns : secCands) {
                // Try both sides of the fillet; add each valid solid
                for (int side = 0; side < 2; ++side) {
                    TopoDS_Shape s =
                        buildFilletWeldSolid(j.start, j.end, nm, ns, leg, side == 0);
                    if (s.IsNull()) continue;
                    const Vec3 c = shapeCentroid(s);
                    if (pointInsideShape(c, mainShape) || pointInsideShape(c, secondaryShape))
                        continue;
                    builtSides.push_back(std::move(s));
                }
            }
        }
        if (!builtSides.empty()) {
            totalSeam += j.length;
            for (auto& bs : builtSides) {
                solids.push_back(std::move(bs));
            }
        } else {
            // No valid side produced a solid
            if (lastReason.empty())
                lastReason = "kaynak kesiti iki parca arasindaki koseye yerlestirilemedi (kati parca icinde kaliyor)";
        }
    }
    if (solids.empty()) {
        outUnsupportedReason = lastReason.empty() ? "kaynak katisi uretilemedi" : lastReason;
        return {};
    }
    if (outSeamLength) *outSeamLength = totalSeam;
    return solids;
}

} // namespace mm

#endif // MM_HAS_OCC
