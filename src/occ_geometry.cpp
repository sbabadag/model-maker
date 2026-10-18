#ifdef MM_HAS_OCC
#include "model_maker/occ_geometry.hpp"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <TopTools_ListOfShape.hxx>
#include <Standard_Failure.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Face.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <GeomAPI_IntCS.hxx>
#include <Geom_Line.hxx>
#include <Geom_Surface.hxx>
#include <gp_Circ.hxx>
#include <gp_Ax2.hxx>
#include <GeomAbs_CurveType.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Pnt.hxx>
#include <gp_Dir.hxx>
#include <GProp_GProps.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <TopoDS_Shape.hxx>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace mm {

double boxVolume(double x, double y, double z) {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(x, y, z).Shape();
    GProp_GProps props;
    BRepGProp::VolumeProperties(box, props);
    return props.Mass();
}

#ifndef MM_OCC_NO_STEP
bool writeBoxStep(const std::filesystem::path& path, double x, double y, double z) {
    STEPControl_Writer writer;
    if (writer.Transfer(BRepPrimAPI_MakeBox(x, y, z).Shape(),
                        STEPControl_AsIs) != IFSelect_RetDone)
        return false;
    return writer.Write(path.string().c_str()) == IFSelect_RetDone;
}

double readStepVolume(const std::filesystem::path& path) {
    STEPControl_Reader reader;
    if (reader.ReadFile(path.string().c_str()) != IFSelect_RetDone)
        throw std::runtime_error("STEP dosyasi okunamadi");
    reader.TransferRoots();
    GProp_GProps props;
    BRepGProp::VolumeProperties(reader.OneShape(), props);
    return props.Mass();
}
#endif // MM_OCC_NO_STEP

WireframeModel shapeToWireframe(const TopoDS_Shape& shape, int circleSegments) {
    // Once cakisan kenarlari birlestir (kutu: 24 topolojik kenar -> 12
    // paylasilan kenar), boylece asagidaki yuz-paylasim filtresi dogru
    // calisir.
    ShapeUpgrade_UnifySameDomain unify;
    unify.Initialize(shape, true, true, false);
    unify.SetAngularTolerance(1.0e-3);
    unify.SetLinearTolerance(1.0e-5);
    unify.Build();
    const TopoDS_Shape unified = unify.Shape();

    // Kenar -> yuz atalari: gercek tel kafes kenari en az iki yuzde
    // paylasilir; yuz insa artigi (silindir kapaklarindaki merkez/radyal
    // cizgiler, dikiş kenarlari) tek yuzdedir.
    TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
    TopExp::MapShapesAndAncestors(unified, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
    const auto sharedByTwoFaces = [&](const TopoDS_Edge& edge) {
        return edgeFaces.FindFromKey(edge).Extent() >= 2;
    };
    // Duşuzlemsel komşu yuzler arasındaki dikis kenarlari (duz yuzeyde
    // gorunen "mesh" cizgileri) tel kafesten cikarilir: yalniz gercek
    // kivrimlar (normal yon degisimi) cizilir.
    const auto isCreaseEdge = [&](const TopoDS_Edge& edge) -> bool {
        const auto faceList = edgeFaces.FindFromKey(edge);
        if (faceList.Extent() < 2) return false;
        const auto planeNormal = [](const TopoDS_Face& face, gp_Dir& normal) -> bool {
            BRepAdaptor_Surface surface(face, true);
            if (surface.GetType() != GeomAbs_Plane) return false;
            normal = surface.Plane().Axis().Direction();
            return true;
        };
        gp_Dir normals[2];
        int normalCount = 0;
        for (TopTools_ListIteratorOfListOfShape it(faceList);
             it.More() && normalCount < 2; it.Next()) {
            if (!planeNormal(TopoDS::Face(it.Value()), normals[normalCount]))
                return true; // egri yuz: ciz
            ++normalCount;
        }
        if (normalCount < 2) return true;
        return std::abs(normals[0].Dot(normals[1])) < 0.999;
    };

    std::vector<Vec3> vertices;
    std::vector<Edge> edges;
    std::map<std::tuple<long, long, long>, std::size_t> dedupe;
    const auto vertexIndex = [&](const gp_Pnt& p) -> std::size_t {
        // 1e-7 toleransla anahtarlayip paylasilan koseleri birlestir.
        const auto key = std::make_tuple(
            static_cast<long>(std::llround(p.X() * 1e7)),
            static_cast<long>(std::llround(p.Y() * 1e7)),
            static_cast<long>(std::llround(p.Z() * 1e7)));
        const auto existing = dedupe.find(key);
        if (existing != dedupe.end()) return existing->second;
        const std::size_t index = vertices.size();
        vertices.push_back({p.X(), p.Y(), p.Z()});
        dedupe.emplace(key, index);
        return index;
    };
    // OCCT katilarinda ayni fiziksel kenar iki komsu yuzde birer kez
    // bulunur (kutu: 24 topolojik kenar = 12 cift). Vertex birlesmesinden
    // sonra cakisan kenarlar ayni uclara sahiptir — siralanmis uclarla
    // kenar seviyesinde de dedupe yap.
    // Her yuzun DIS telini (OuterWire) topla: yuz insa artigi ic kenarlar
    // (silindir kapaklarindaki merkez/radyal cizgiler) dis telde yoktur,
    // boylece temiz tel kafes dogal olarak elde edilir. Cakisan kenarlar
    // (iki komsu yuzun ortak siniri) siralanmis uclarla dedupe edilir.
    std::set<std::pair<std::size_t, std::size_t>> addedEdges;
    const auto tessellateEdge = [&](const TopoDS_Edge& topoEdge) {
        BRepAdaptor_Curve curve(topoEdge);
        std::size_t segments = 1;
        if (curve.GetType() != GeomAbs_Line)
            segments = static_cast<std::size_t>(std::max(2, circleSegments));
        const double first = curve.FirstParameter();
        const double last = curve.LastParameter();
        std::size_t previous = vertexIndex(curve.Value(first));
        for (std::size_t i = 1; i <= segments; ++i) {
            const double t = first + (last - first) * static_cast<double>(i) / segments;
            const std::size_t current = vertexIndex(curve.Value(t));
            const auto edgeKey = current < previous
                ? std::make_pair(current, previous) : std::make_pair(previous, current);
            if (addedEdges.insert(edgeKey).second)
                edges.push_back({previous, current});
            previous = current;
        }
    };
    for (TopExp_Explorer faces(unified, TopAbs_FACE); faces.More(); faces.Next()) {
        const TopoDS_Wire outer = BRepTools::OuterWire(TopoDS::Face(faces.Current()));
        for (TopExp_Explorer wireEdges(outer, TopAbs_EDGE); wireEdges.More(); wireEdges.Next()) {
            const TopoDS_Edge edge = TopoDS::Edge(wireEdges.Current());
            if (!isCreaseEdge(edge)) continue;
            tessellateEdge(edge);
        }
    }
    return WireframeModel(std::move(vertices), std::move(edges));
}

WireframeModel solidBoxWireframe(double dx, double dy, double dz) {
    return shapeToWireframe(BRepPrimAPI_MakeBox(dx, dy, dz).Shape(), 48);
}

TopoDS_Shape cutSolidByPlane(const TopoDS_Shape& shape, const Vec3& planePoint,
                             const Vec3& planeNormal, bool keepPositive) {
    const gp_Pnt origin(planePoint.x, planePoint.y, planePoint.z);
    const gp_Dir direction(planeNormal.x, planeNormal.y, planeNormal.z);
    const gp_Pln plane(origin, direction);
    // 7.8 API'si: (Face, RefPnt) — yarim uzay, referans noktasinin bulundugu
    // taraf. Referans nokta = tutulacak tarafta (normal yonunde bir nokta).
    const gp_Pnt reference = keepPositive
        ? origin.Translated(direction.XYZ() * 1.0)
        : origin.Translated(direction.XYZ() * -1.0);
    // Sinirli prizma (slab) yaklasimi: sonsuz/half-space boolean'lari
    // bozuk kesim yuzleri uretiyordu. Katiyi tamamen kapatan buyuk bir kare
    // yuzu, kesilecek tarafa dogru extrude edilir ve cikarilir.
    Bnd_Box boundingBox;
    BRepBndLib::Add(shape, boundingBox);
    double xMin = 0.0, yMin = 0.0, zMin = 0.0, xMax = 0.0, yMax = 0.0, zMax = 0.0;
    boundingBox.Get(xMin, yMin, zMin, xMax, yMax, zMax);
    // 2x marj: duzlemin yerel eksenleri katinin kosegenini (kadar ~1.73x)
    // kapsamali — 0.75x marj kenarlari acikta birakiyordu.
    const double margin = std::max({xMax - xMin, yMax - yMin, zMax - zMin}) * 2.0 + 1.0;
    // Kalacak taraf = keepPositive ise NEGATIF taraf cikarilir (ve tersi).
    const gp_Vec removeVector = keepPositive
        ? direction.XYZ() * (-margin * 2.0)
        : direction.XYZ() * (margin * 2.0);
    BRepBuilderAPI_MakeFace planeFace(plane, -margin, margin, -margin, margin);
    auto slab = BRepPrimAPI_MakePrism(planeFace.Face(), removeVector);
    BRepAlgoAPI_Cut cut(shape, slab.Shape());
    // Kesim yuzu boolean'dan bolunmus parcalar halinde gelebilir (gozle
    // gorulur dikis cizgileri); es-düzlemsel yuzleri birlestir.
    ShapeUpgrade_UnifySameDomain unifier(cut.Shape(), true, true, true);
    unifier.Build();
    return unifier.Shape();
}

// PolygonCut (Tekla "cut with polygon"): kapali cokgen duzleminden bir prizma
// olusturup (cokgen normali boyunca iki yonde, hedefi tamamen kaplayan kalinlik)
// hedeften cikarir — through cut. Cokgen <3 nokta, dejenere (alan ~0) veya yuz
// kurulamazsa base'i DOKUNMADAN dondurur (guvenli basarisizlik).
static TopoDS_Shape cutSolidByPolygon(const TopoDS_Shape& base,
                                      const std::vector<Vec3>& polygon) {
    if (polygon.size() < 3) return base;
    const auto n = polygonNormal(polygon);
    if (!n) return base;
    BRepBuilderAPI_MakePolygon builder;
    for (const auto& p : polygon) builder.Add(gp_Pnt(p.x, p.y, p.z));
    builder.Close();
    if (!builder.IsDone()) return base;
    const TopoDS_Face face = BRepBuilderAPI_MakeFace(builder.Wire()).Face();
    if (face.IsNull()) return base;
    Bnd_Box bb;
    BRepBndLib::Add(base, bb);
    double xMin = 0.0, yMin = 0.0, zMin = 0.0, xMax = 0.0, yMax = 0.0, zMax = 0.0;
    bb.Get(xMin, yMin, zMin, xMax, yMax, zMax);
    const gp_Vec normal(n->x, n->y, n->z);
    // Parca bbox'inin 8 kosesini normal eksenine izdusur -> [tmin, tmax].
    // Prizma bu araligi (kucuk marjla) kapsar -> cokgen duzlemi parcadan UZAK
    // olsa bile through-cut uretir (Tekla: derinlik otomatik, yuzeyin ~3mm
    // disina uzatilir). Izdusum ACIK bilesen hesabiyla yapilir (gp_Vec(P1,P2)
    // = P2-P1 yon belirsizliginden kacinmak icin).
    const double ox = polygon[0].x, oy = polygon[0].y, oz = polygon[0].z;
    double tmin = 1e100, tmax = -1e100;
    const double cx[2] = {xMin, xMax}, cy[2] = {yMin, yMax}, cz[2] = {zMin, zMax};
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
            for (int k = 0; k < 2; ++k) {
                const double t = (cx[i] - ox) * n->x + (cy[j] - oy) * n->y +
                                 (cz[k] - oz) * n->z;
                tmin = std::min(tmin, t);
                tmax = std::max(tmax, t);
            }
    const double pad = 1.0;
    const double from = tmin - pad;
    const double length = (tmax - tmin) + 2.0 * pad;
    gp_Trsf shift;
    shift.SetTranslation(normal * from);
    BRepBuilderAPI_Transform moved(face, shift);
    const TopoDS_Shape prism =
        BRepPrimAPI_MakePrism(moved.Shape(), normal * length).Shape();
    BRepAlgoAPI_Cut cut(base, prism);
    if (!cut.IsDone()) return base;  // guvenli basarisizlik: base'e dokunma
    ShapeUpgrade_UnifySameDomain unifier(cut.Shape(), true, true, true);
    unifier.Build();
    return unifier.Shape();
}

// --- Fitting UZATMA (P9) ---------------------------------------------------
// Tekla "Fit part end": duzlem ekseni KESMIYORSA (kati tamamen tek tarafta,
// yani kiris duzleme kisa kaliyorsa) yakin uc duzleme kadar UZATILIR. Eski
// davranis bu durumda base'e dokunmuyordu (yalniz kesen duzlemi destekliyordu).
// Yontem: yakin ucun kapak yuzunu eksen boyunca duzlemi gecene kadar extrude et
// -> fuse -> duzlemle kes (uzak taraf korunur). Sonuc: uc yuzeyi TAM duzlemde
// (egimli duzlemlerde dahi duzgun egimli uy yuzeyi uretilir).

// Eksene ~dik, eksen yonunde en uc duzlemdeki TUM kapak yuzlerini toplar.
// Profil kesiti TEK yuz degildir; profil uretici onu flans/govde gibi alt
// yuzlere boler (ayni duzlemde birden fazla duzlemsel yuz). Uzatma prizmasi
// kesitin TAMAMINI surmelidir, tek yuz degil.
static void collectCapFaces(const TopoDS_Shape& shape, const gp_Dir& axisDir,
                            bool atToEnd, std::vector<TopoDS_Face>& out) {
    double best = atToEnd ? -1e100 : 1e100;
    std::vector<TopoDS_Face> candidates;
    for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next()) {
        const TopoDS_Face face = TopoDS::Face(ex.Current());
        if (face.IsNull()) continue;
        BRepAdaptor_Surface surface(face, true);
        if (surface.GetType() != GeomAbs_Plane) continue;
        const gp_Pln pln = surface.Plane();
        if (std::abs(pln.Axis().Direction().Dot(axisDir)) < 0.999) continue;  // kapak eksene ~dik
        const double proj = pln.Location().XYZ().Dot(axisDir.XYZ());
        if (atToEnd ? (proj > best + 1e-6) : (proj < best - 1e-6)) {
            best = proj;
            candidates.clear();
            candidates.push_back(face);
        } else if (std::abs(proj - best) <= 1e-6) {
            candidates.push_back(face);
        }
    }
    out = std::move(candidates);
}

// "Fit part end" (Tekla ucu uydur): belirtilen ucu DISARI dogru, duzlemi kesin
// gecene kadar uzatir, sonra korunacak tarafi birakarak duzlemle keser. Bu
// yaklasim UZATMA (kisa kiris), KESME (uzun kiris) ve EGIMLI kismi kesisim
// (bir kose kisa, digeri uzun) durumlarini tek yolda cozer: kisa kose doldurulur,
// uzun kose kesilir -> uc yuzeyi tam duzlemde olur.
//
// fitFromEnd  : true -> from ucunu uydur (extrude -u yonunde), false -> to ucu.
// keepPositive: kesmede korunacak yarim-uzay (duzlem normaline gore).
static TopoDS_Shape fitEndToPlane(const TopoDS_Shape& base,
                                  const Vec3& axisFrom, const Vec3& axisTo,
                                  const Vec3& planeOrigin, const Vec3& planeNormal,
                                  bool fitFromEnd, bool keepPositive) {
    const gp_Vec axis(axisTo.x - axisFrom.x, axisTo.y - axisFrom.y,
                      axisTo.z - axisFrom.z);
    if (axis.SquareMagnitude() < 1e-18) return base;
    const gp_Dir u(axis);
    const gp_Dir n(planeNormal.x, planeNormal.y, planeNormal.z);
    const double uDotN = u.Dot(n);
    if (std::abs(uDotN) < 1e-9) return base;  // eksen duzleme ~paralel -> uzatilamaz

    // Uydurulan ucun eksen noktasindan duzleme eksen boyunca uzaklik (mutlak).
    const double sFit = signedDistanceToPlane(planeOrigin, planeNormal,
                                              fitFromEnd ? axisFrom : axisTo);
    const double gap = std::abs(sFit) / std::abs(uDotN);

    // Kesitin eksen disina tasan kismi icin guvenli ust sinir: kiris bbox
    // kosegeni. Bu sayede EGIMLI duzlemde kisa kose bile duzlemi kesin gecer.
    Bnd_Box bb;
    BRepBndLib::Add(base, bb);
    double x1, y1, z1, x2, y2, z2;
    bb.Get(x1, y1, z1, x2, y2, z2);
    const double dx = x2 - x1, dy = y2 - y1, dz = z2 - z1;
    const double diagonal = std::sqrt(dx * dx + dy * dy + dz * dz);

    const double extendLen = gap + diagonal + 1.0;
    const gp_Dir outward = fitFromEnd ? u.Reversed() : u;
    const gp_Vec extVec(outward.XYZ() * extendLen);

    std::vector<TopoDS_Face> caps;
    collectCapFaces(base, u, /*atToEnd=*/ !fitFromEnd, caps);
    if (caps.empty()) return base;  // kapak bulunamadi -> guvenli basarisizlik

    // Kesitin TUM alt yuzlerini ayni vektorle surup base ile birlestir.
    TopoDS_Shape extended = base;
    for (const auto& cap : caps) {
        BRepPrimAPI_MakePrism prism(cap, extVec);
        if (!prism.IsDone()) continue;
        extended = BRepAlgoAPI_Fuse(extended, prism.Shape()).Shape();
    }
    ShapeUpgrade_UnifySameDomain unifier(extended, true, true, true);
    unifier.Build();
    return cutSolidByPlane(unifier.Shape(), planeOrigin, planeNormal, keepPositive);
}

TopoDS_Shape applyCutModifier(const TopoDS_Shape& base, const CutModifier& mod,
                              const Vec3& axisFrom, const Vec3& axisTo,
                              const ToolShapeResolver& toolShape) {
    (void)axisFrom;
    (void)axisTo;
    switch (mod.kind) {
        case CutKind::PlaneCut:
            return cutSolidByPlane(base, mod.planeOrigin, mod.planeNormal,
                                   mod.keepPositive != mod.invertSide);
        case CutKind::Fitting: {
            // Tekla "Fit part end": kisa taraf otomatik uydurulur, uzun taraf
            // korunur. Tek yolda hem UZATMA (kisa kiris) hem KESME (uzun kiris)
            // hem de EGIMLI kismi kesisim (kisa kose uzat + uzun kose kes).
            // invertSide (P8, Alt+sol tik) uyduracagi ucu tersine cevirir.
            const double sFrom = signedDistanceToPlane(mod.planeOrigin,
                                                       mod.planeNormal, axisFrom);
            const double sTo = signedDistanceToPlane(mod.planeOrigin,
                                                     mod.planeNormal, axisTo);
            constexpr double tol = 1e-9;
            // Eksen duzleme yatik/uzerinde: kesim anlamsiz.
            if (std::abs(sFrom) < tol && std::abs(sTo) < tol) return base;

            const bool fromIsNear = std::abs(sFrom) <= std::abs(sTo);
            if (sFrom * sTo > 0.0) {
                // Kisa kiris: yakin ucu duzleme kadar uzat. invertSide anlamsiz
                // (butun kiris korunur) — daima yakin ucu uydur, uzak tarafi koru.
                const double sFar = fromIsNear ? sTo : sFrom;
                return fitEndToPlane(base, axisFrom, axisTo, mod.planeOrigin,
                                     mod.planeNormal, /*fitFromEnd=*/ fromIsNear,
                                     /*keepPositive=*/ sFar > 0.0);
            }
            // Kesen duzlem: uzun tarafi koru. fitFromEnd uyduracagi ucu isaretler
            // (kisa taraf; invertSide ile terslenir). keepPositive korunan taraf.
            const auto keep = fittingKeepPositive(axisFrom, axisTo, mod.planeOrigin,
                                                  mod.planeNormal);
            if (!keep) return base;
            const bool fitFromEnd = mod.invertSide ? !fromIsNear : fromIsNear;
            const bool keepPositive = mod.invertSide ? !*keep : *keep;
            return fitEndToPlane(base, axisFrom, axisTo, mod.planeOrigin,
                                 mod.planeNormal, fitFromEnd, keepPositive);
        }
        case CutKind::LineCut: {
            // Tekla "cut with a line": cizgiyi iceren, cizgi duzlemine dik
            // kesme duzlemi. polygon = {cizgiBas, cizgiSon}; planeNormal =
            // cizginin cizildigi duzlemin normali. Taraf secimi keepPositive
            // (invertSide ile terslenir).
            if (mod.polygon.size() < 2) return base;
            const auto n = lineCutPlaneNormal(mod.polygon[0], mod.polygon[1],
                                              mod.planeNormal);
            if (!n) return base;
            return cutSolidByPlane(base, mod.polygon[0], *n,
                                   mod.keepPositive != mod.invertSide);
        }
        case CutKind::PolygonCut:
            return cutSolidByPolygon(base, mod.polygon);
        case CutKind::BooleanCut: {
            // P3 Part Cut (Tekla "cut with a part"): hedeften arac kati cikarilir
            // (Boolean difference). Arac sekli resolver'dan cozumlenir; yoksa
            // (veya gecersizse) base'e DOKUNULMAZ.
            if (!toolShape) return base;
            const TopoDS_Shape* tool = toolShape(mod.tool);
            if (!tool || tool->IsNull()) return base;
            return booleanCutShape(base, *tool);
        }
        case CutKind::Hole: {
            // Phase B1 "bolt hole": delik = cikarilan silindir (through-cut).
            // Eksen = planeNormal (birimlestirilir), merkez = planeOrigin,
            // cap = diameter. Silindir katiyi boydan boya kesecek kadar uzun
            // tutulur (bbox kosegeni + cap yedegi). cap<=0 veya eksen ~0 ise
            // base'e dokunulmaz (guvenli no-op).
            if (mod.diameter <= 0.0) return base;
            const double axlen = std::sqrt(mod.planeNormal.x * mod.planeNormal.x +
                                           mod.planeNormal.y * mod.planeNormal.y +
                                           mod.planeNormal.z * mod.planeNormal.z);
            if (axlen < 1e-12) return base;
            const gp_Dir axisDir(mod.planeNormal.x / axlen,
                                 mod.planeNormal.y / axlen,
                                 mod.planeNormal.z / axlen);
            Bnd_Box bb;
            BRepBndLib::Add(base, bb);
            double xMin = 0.0, yMin = 0.0, zMin = 0.0;
            double xMax = 0.0, yMax = 0.0, zMax = 0.0;
            bb.Get(xMin, yMin, zMin, xMax, yMax, zMax);
            const double diag = std::sqrt((xMax - xMin) * (xMax - xMin) +
                                          (yMax - yMin) * (yMax - yMin) +
                                          (zMax - zMin) * (zMax - zMin));
            const double half = diag + mod.diameter;
            const gp_Pnt center(mod.planeOrigin.x - axisDir.X() * half,
                                mod.planeOrigin.y - axisDir.Y() * half,
                                mod.planeOrigin.z - axisDir.Z() * half);
            const TopoDS_Shape cylinder =
                BRepPrimAPI_MakeCylinder(gp_Ax2(center, axisDir),
                                         mod.diameter * 0.5, 2.0 * half).Shape();
            return booleanCutShape(base, cylinder);
        }
    }
    return base;
}

TopoDS_Shape applyCutModifierChain(const TopoDS_Shape& base,
                                   const std::vector<CutModifier>& mods,
                                   const Vec3& axisFrom, const Vec3& axisTo,
                                   const ToolShapeResolver& toolShape) {
    TopoDS_Shape result = base;
    for (const auto& mod : mods) {
        if (!mod.enabled) continue;
        result = applyCutModifier(result, mod, axisFrom, axisTo, toolShape);
    }
    return result;
}

// Tek KAPALI keskin-kose I-kesit konturu. Flanslar DIK (perpendicular),
// radius ve IPE ic yuz egimi YOK — her zaman keskin kose (Tekla tarzi,
// hizli cizim + boolean). Noktalar saat yonunde, sag-ust dis koseden baslar.
// Yerel koordinat: Y = kesit yuksekligi (orta=0), X = flans genisligi
// (orta=0), Z=0.
static std::vector<gp_Pnt> buildISectionContour(const SteelProfile& profile) {
    const double w = profile.width;
    const double h = profile.height;
    const double tw = profile.plateThickness;    // govde kalinligi
    const double tf = profile.flangeThickness;   // flans kalinligi
    const double hw = w / 2.0;
    const double hh = h / 2.0;
    const double xw = tw / 2.0;
    const double yFlIn = hh - tf;                // flans alt (ic) yuzu

    std::vector<gp_Pnt> pts;
    pts.emplace_back(hw, hh, 0.0);      // 1 sag ust dis kose
    pts.emplace_back(hw, yFlIn, 0.0);   // 2 ust flans sag ic
    pts.emplace_back(xw, yFlIn, 0.0);   // 3 govde sag ust
    pts.emplace_back(xw, -yFlIn, 0.0);  // 4 govde sag alt
    pts.emplace_back(hw, -yFlIn, 0.0);  // 5 alt flans sag ic
    pts.emplace_back(hw, -hh, 0.0);     // 6 sag alt dis kose
    pts.emplace_back(-hw, -hh, 0.0);    // 7 sol alt dis kose
    pts.emplace_back(-hw, -yFlIn, 0.0); // 8 alt flans sol ic
    pts.emplace_back(-xw, -yFlIn, 0.0); // 9 govde sol alt
    pts.emplace_back(-xw, yFlIn, 0.0);  // 10 govde sol ust
    pts.emplace_back(-hw, yFlIn, 0.0);  // 11 ust flans sol ic
    pts.emplace_back(-hw, hh, 0.0);     // 12 sol ust dis kose
    return pts;
}

TopoDS_Shape extrudeProfileSolid(const SteelProfile& profile, const Vec3& from,
                                     const Vec3& to, double rotationDegrees,
                                     const Vec3& referenceNormal,
                                     SectionAnchor anchor) {
    const double w = profile.width > 0.0 ? profile.width : 50.0;
    const double h = profile.height > 0.0 ? profile.height : 50.0;
    const double t = profile.plateThickness > 0.0 ? profile.plateThickness : 0.0;
    const double hw = w / 2.0;
    const double hh = h / 2.0;

    TopoDS_Shape section;

    // Kesit turu — TEK KAYNAK: mm::classifyProfileSection (profile_database).
    // Kesit yerlesimi de (profileSectionTopY) ayni siniflandirmayi
    // kullanir; harf kumesi listesini burada TEKRAR YAZMA.
    const ProfileSectionKind kind = classifyProfileSection(profile);

    if (kind == ProfileSectionKind::Round) {
        const double outerR = std::max(w, h) / 2.0;
        const double innerR = outerR - t;
        const auto circleFace = [&](double radius) {
            gp_Circ circle(gp_Ax2(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0)), radius);
            BRepBuilderAPI_MakeEdge edge(circle);
            BRepBuilderAPI_MakeWire wire(edge);
            return BRepBuilderAPI_MakeFace(wire).Shape();
        };
        if (innerR > 1.0) {
            BRepAlgoAPI_Cut cut(circleFace(outerR), circleFace(innerR));
            section = cut.Shape();
        } else {
            section = circleFace(outerR);
        }
    } else if (kind == ProfileSectionKind::ISection) {
        // I-kesit (HEA/HEB/HEM/IPE/IPN/UB/UC/W/T): tek KAPALI keskin-kose
        // kontur yuzu (DIK flans; radius/egim yok — Tekla tarzi hizli).
        const auto contour = buildISectionContour(profile);
        BRepBuilderAPI_MakePolygon polygon;
        for (const auto& p : contour) polygon.Add(p);
        polygon.Close();
        BRepBuilderAPI_MakeFace face(polygon.Wire());
        if (face.IsDone()) {
            section = face.Shape();
        } else {
            // Degrade durum (ornegin gecersiz profil olculeri): eski yola dus.
            const double tf = profile.flangeThickness > 0.0 ? profile.flangeThickness : t;
            const double tw = t;
            const double webHeight = h - 2.0 * tf;
            const auto rectFaceAt = [&](double yCenter, double halfW, double halfH) {
                BRepBuilderAPI_MakePolygon poly(gp_Pnt(-halfW, yCenter - halfH, 0.0),
                                                gp_Pnt(halfW, yCenter - halfH, 0.0),
                                                gp_Pnt(halfW, yCenter + halfH, 0.0),
                                                gp_Pnt(-halfW, yCenter + halfH, 0.0));
                poly.Close();
                return BRepBuilderAPI_MakeFace(poly.Wire()).Shape();
            };
            const TopoDS_Shape topFlange = rectFaceAt(hh - tf / 2.0, hw, tf / 2.0);
            const TopoDS_Shape bottomFlange = rectFaceAt(-hh + tf / 2.0, hw, tf / 2.0);
            const TopoDS_Shape web = rectFaceAt(0.0, tw / 2.0, std::max(1.0, webHeight) / 2.0);
            BRepAlgoAPI_Fuse flanges(topFlange, bottomFlange);
            BRepAlgoAPI_Fuse complete(flanges.Shape(), web);
            section = complete.Shape();
        }
    } else {
    const auto rectFace = [&](double halfW, double halfH) {
        BRepBuilderAPI_MakePolygon polygon(gp_Pnt(-halfW, -halfH, 0.0),
                                           gp_Pnt(halfW, -halfH, 0.0),
                                           gp_Pnt(halfW, halfH, 0.0),
                                           gp_Pnt(-halfW, halfH, 0.0));
        polygon.Close();
        return BRepBuilderAPI_MakeFace(polygon.Wire()).Shape();
    };
    const double innerW = w - 2.0 * t;
    const double innerH = h - 2.0 * t;
    if (innerW > 1.0 && innerH > 1.0) {
        // ici bos kutu (KKR): dis - ic
        const TopoDS_Shape outer = rectFace(hw, hh);
        const TopoDS_Shape inner = rectFace(innerW / 2.0, innerH / 2.0);
        BRepAlgoAPI_Cut cut(outer, inner);
        section = cut.Shape();
    } else {
        section = rectFace(hw, hh);
    }
    }

    // KESIT YERLESIMI (KULLANICI SARTI v4): "ust flansin USTUNDEN cizmiyor,
    // ust flansin MERKEZINDEN ciziyor" → kesit uye eksenine UST YUZEYINDEN
    // oturur: eksen cizgisi kesitin en ustunden gecer ve TUM kesit cizginin
    // ALTINA sarkar (Tekla "position: top of section"). Bu DEFAULT (Top)
    // davranistir; Slice 2d-2c P3 ile SectionAnchor (Top/Center/Bottom) ile
    // parametrik hale getirildi. DEFAULT Top KORUNUR — mevcut modeller sessizce
    // baska bir anchor'a donusturulmez. Baslik paralelligi (v1/v2) DEGISMEZ;
    // degisen yalnizca DUSEY KONUMdur. Kaydirma dondurmeden ONCE uygulanir,
    // boylece profil donmus olsa da uye ekseni hep secilen anchor'dan gecer.
    const double anchorOffset = profileSectionAnchorOffset(profile, anchor);
    if (std::abs(anchorOffset) > 1e-9) {
        gp_Trsf offsetTrsf;
        offsetTrsf.SetTranslation(gp_Vec(0.0, anchorOffset, 0.0));
        section = BRepBuilderAPI_Transform(section, offsetTrsf).Shape();
    }

    // Kesit, profil ekseni (Z) etrafinda dondurulur — Tekla Rotation.
    if (std::abs(rotationDegrees) > 1e-9) {
        gp_Trsf sectionRotation;
        sectionRotation.SetRotation(
            gp_Ax1(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0)),
            rotationDegrees * M_PI / 180.0);
        BRepBuilderAPI_Transform rotated(section, sectionRotation);
        section = rotated.Shape();
    }

    const gp_Vec direction(to.x - from.x, to.y - from.y, to.z - from.z);
    const double length = direction.Magnitude();
    if (length < 1e-12) return {};
    // Prism yuksekligi = cizgi uzunlugu (birim vektor degil!)
    auto prism = BRepPrimAPI_MakePrism(section, gp_Vec(0.0, 0.0, length));
    // HEDEF cerceve: kesitin YEREL eksenleri — tek kaynak profileSectionFrame.
    // Genislik ekseni, baslik plakalari REFERANS DUZLEMINE (aktif is duzlemi;
    // varsayilan dunya XY) PARALEL olacak sekilde secilir → govde o duzleme
    // diktir ("profil cizilirken ust baslik workplane ile paralel").
    // Eskiden iki argumanli gp_Ax3(P, dir) kullaniliyordu: o kurucu X eksenini
    // dir'in en kucuk koordinatina gore keyfi sectigi icin eksenel kirisler
    // 90° yatik geliyordu (probe: IPE200 +X boyunca dy=200 dz=100 veriyordu;
    // dogrusu dy=100 dz=200).
    const ProfileSectionFrame frame =
        profileSectionFrame(Vec3{direction.X(), direction.Y(), direction.Z()}, referenceNormal);
    gp_Trsf trsf;
    // OCC 7.9'da ilk sistem HEDEF, ikinci KAYNAK (probe ile dogrulandi:
    // SetTransformation(hedef, kaynak) — tersi durumda kiriş X ekseninde kalir).
    trsf.SetTransformation(
        gp_Ax3(gp_Pnt(from.x, from.y, from.z),
               gp_Dir(frame.axis.x, frame.axis.y, frame.axis.z),
               gp_Dir(frame.width.x, frame.width.y, frame.width.z)),
        gp_Ax3(gp_Pnt(0.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0)));
    return BRepBuilderAPI_Transform(prism.Shape(), trsf).Shape();
}

WireframeModel shapeToWireframeWithFaces(const TopoDS_Shape& shape, double faceDeflection,
                                         int circleSegments) {
    // Tel kafes kisim = shapeToWireframe ile ayni; yuzler sonradan eklenir.
    auto wireframe = shapeToWireframe(shape, circleSegments);
    if (faceDeflection <= 0.0) return wireframe;

    std::vector<Vec3> vertices = wireframe.vertices();
    std::vector<Edge> edges = wireframe.edges();
    std::vector<Face> faceList;

    // Yuz koordinatlari tel kafes vertex'leriyle AYNI dedupe anahtariyla
    // birlestirilir: kutu kose noktalari paylasilir, egri yuzeylerin ic
    // dugumleri yeni vertex olarak eklenir.
    std::map<std::tuple<long, long, long>, std::size_t> dedupe;
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const auto key = std::make_tuple(
            static_cast<long>(std::llround(vertices[i].x * 1e7)),
            static_cast<long>(std::llround(vertices[i].y * 1e7)),
            static_cast<long>(std::llround(vertices[i].z * 1e7)));
        dedupe.emplace(key, i);
    }
    const auto nodeIndex = [&](const gp_Pnt& p) -> std::size_t {
        const auto key = std::make_tuple(
            static_cast<long>(std::llround(p.X() * 1e7)),
            static_cast<long>(std::llround(p.Y() * 1e7)),
            static_cast<long>(std::llround(p.Z() * 1e7)));
        const auto existing = dedupe.find(key);
        if (existing != dedupe.end()) return existing->second;
        const std::size_t index = vertices.size();
        vertices.push_back({p.X(), p.Y(), p.Z()});
        dedupe.emplace(key, index);
        return index;
    };

    BRepMesh_IncrementalMesh mesher(shape, faceDeflection);
    for (TopExp_Explorer faces(shape, TopAbs_FACE); faces.More(); faces.Next()) {
        const TopoDS_Face& face = TopoDS::Face(faces.Current());
        TopLoc_Location location;
        const Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation(face, location);
        if (triangulation.IsNull()) continue;
        const gp_Trsf transform = location.Transformation();
        for (int t = 1; t <= triangulation->NbTriangles(); ++t) {
            int n1 = 0, n2 = 0, n3 = 0;
            triangulation->Triangle(t).Get(n1, n2, n3);
            const std::size_t v1 = nodeIndex(triangulation->Node(n1).Transformed(transform));
            const std::size_t v2 = nodeIndex(triangulation->Node(n2).Transformed(transform));
            const std::size_t v3 = nodeIndex(triangulation->Node(n3).Transformed(transform));
            faceList.push_back({v1, v2, v3});
        }
    }
    return WireframeModel(std::move(vertices), std::move(edges), std::move(faceList));
}

TopoDS_Shape booleanFuseShape(const TopoDS_Shape& a, const TopoDS_Shape& b) {
    return BRepAlgoAPI_Fuse(a, b).Shape();
}

TopoDS_Shape booleanCommonShape(const TopoDS_Shape& a, const TopoDS_Shape& b) {
    return BRepAlgoAPI_Common(a, b).Shape();
}

TopoDS_Shape booleanCutShape(const TopoDS_Shape& a, const TopoDS_Shape& b) {
    if (a.IsNull() || b.IsNull()) return a;
    TopTools_ListOfShape args, tools;
    args.Append(a);
    tools.Append(b);
    BRepAlgoAPI_Cut cut;
    try {
        // Kucuk fuzzy degeri: boolean sirasinda neredeyse cakisan nokta/kenarlari
        // birlestirerek dejenere (sliver) yuz/kenar olusmasini onler — Windows'ta
        // Extrema_ExtCC segfault'u ile sonuclanan klasik kaynak.
        cut.SetFuzzyValue(1e-5);
        cut.SetArguments(args);
        cut.SetTools(tools);
        cut.Build();
    } catch (const Standard_Failure&) {
        return a;  // guvenli fallback: tabani dokunmadan birak
    } catch (...) {
        return a;
    }
    if (!cut.IsDone()) return a;  // boolean basarisiz: taban korunur
    const TopoDS_Shape result = cut.Shape();
    return result.IsNull() ? a : result;
}

double shapeVolume(const TopoDS_Shape& shape) {
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.Mass();
}

bool raycastBrepFace(const TopoDS_Shape& shape, const Vec3& rayOrigin,
                     const Vec3& rayDirection, Vec3& hitPoint, Vec3& hitNormal,
                     double& distance) {
    if (shape.IsNull()) return false;
    const double dirLen = std::sqrt(rayDirection.x * rayDirection.x +
                                    rayDirection.y * rayDirection.y +
                                    rayDirection.z * rayDirection.z);
    if (dirLen < 1e-12) return false;
    const gp_Pnt o(rayOrigin.x, rayOrigin.y, rayOrigin.z);
    const gp_Dir d(rayDirection.x / dirLen, rayDirection.y / dirLen,
                   rayDirection.z / dirLen);
    const gp_Lin line(o, d);

    // BRepClass_FaceClassifier'in yuz ici testi ucgenlemeyi kullanir; boolean
    // (kesme/delik) sonrasi uretilen sekillerde tesselasyon olmayabilir. Bu
    // yuzden once hafif bir IncrementalMesh — zaten varsa ve daha inceyse
    // dokunmaz (yalnizca eksikse doldurur).
    BRepMesh_IncrementalMesh mesher(shape, 0.1);

    const Handle(Geom_Line) gline = new Geom_Line(line);

    bool found = false;
    double bestW = 1e100;
    Vec3 bestPoint{}, bestNormal{};
    for (TopExp_Explorer ex(shape, TopAbs_FACE); ex.More(); ex.Next()) {
        const TopoDS_Face face = TopoDS::Face(ex.Current());
        if (face.IsNull()) continue;
        const Handle(Geom_Surface) surf = BRep_Tool::Surface(face);
        if (surf.IsNull()) continue;

        GeomAPI_IntCS inter(gline, surf);
        if (!inter.IsDone()) continue;
        for (Standard_Integer i = 1; i <= inter.NbPoints(); ++i) {
            Standard_Real u = 0.0, v = 0.0, w = 0.0;
            inter.Parameters(i, u, v, w);
            if (w < 1e-9) continue;  // gerideki isabetleri ele (isin onunde kal)
            // Yuzun KESILMIS sinirlari icinde mi? (sonsuz yuzeydeki kesisim
            // yuz disinda kalabilir — orn. ici bos kutu profilin govdesi.)
            BRepClass_FaceClassifier classifier;
            classifier.Perform(face, gp_Pnt2d(u, v), 1e-7);
            const TopAbs_State state = classifier.State();
            if (state != TopAbs_IN && state != TopAbs_ON) continue;

            if (w >= bestW) continue;
            const gp_Pnt pt = inter.Point(i);
            // Yuzey normali D1(u,v) ile tam hesaplanir.
            gp_Pnt sp;
            gp_Vec d1u, d1v;
            surf->D1(u, v, sp, d1u, d1v);
            gp_Vec n = d1u.Crossed(d1v);
            if (n.SquareMagnitude() < 1e-18) continue;
            n.Normalize();
            // Normal izleyiciye (isinin cikis noktasina) dogru yonlensin.
            const gp_Vec toOrigin(o.X() - pt.X(), o.Y() - pt.Y(), o.Z() - pt.Z());
            if (n.Dot(toOrigin) < 0.0) n.Reverse();

            bestW = w;
            bestPoint = {pt.X(), pt.Y(), pt.Z()};
            bestNormal = {n.X(), n.Y(), n.Z()};
            found = true;
        }
    }
    if (!found) return false;
    hitPoint = bestPoint;
    hitNormal = bestNormal;
    distance = bestW;
    return true;
}

WireframeModel solidBoxSolid(double dx, double dy, double dz, double faceDeflection) {
    return shapeToWireframeWithFaces(BRepPrimAPI_MakeBox(dx, dy, dz).Shape(),
                                     faceDeflection, 48);
}

WireframeModel solidCylinderSolid(double radius, double height, double faceDeflection,
                                  int circleSegments) {
    // 2-arg (R, H): tam silindir. 3-arg overload'in ucuncu parametresi ACI'dir
    // ve dilim uretir (bkz. application.cpp addCylinder notu).
    return shapeToWireframeWithFaces(BRepPrimAPI_MakeCylinder(radius, height).Shape(),
                                     faceDeflection, circleSegments);
}

double solidCylinderVolume(double radius, double height) {
    const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(radius, height).Shape();
    GProp_GProps props;
    BRepGProp::VolumeProperties(cylinder, props);
    return props.Mass();
}

WireframeModel solidCylinderWireframe(double radius, double height, int circleSegments) {
    // Analitik kurulum: alt/ust daire + iki meridyen cizgisi. Genel
    // tesselatoru kullanmaz — OCCT'nin silindir ilkelinin kapak insa
    // artefaktlari (merkez/radyal cizgiler) tel kafese sizamaz.
    const int segments = std::max(8, circleSegments);
    std::vector<Vec3> vertices;
    std::vector<Edge> edges;
    vertices.reserve(static_cast<std::size_t>(segments) * 2);
    edges.reserve(static_cast<std::size_t>(segments) * 2 + 2);
    const auto addRing = [&](double z) -> std::size_t {
        const std::size_t base = vertices.size();
        for (int i = 0; i < segments; ++i) {
            const double angle = 2.0 * 3.14159265358979323846 * i / segments;
            vertices.push_back({radius * std::cos(angle), radius * std::sin(angle), z});
        }
        for (int i = 0; i < segments; ++i)
            edges.push_back({base + i, base + (i + 1) % segments});
        return base;
    };
    const std::size_t top = addRing(height);
    const std::size_t bottom = addRing(0.0);
    edges.push_back({top, bottom});                              // 0° meridyeni
    edges.push_back({top + segments / 2, bottom + segments / 2}); // 180° meridyeni
    return WireframeModel(std::move(vertices), std::move(edges));
}

} // namespace mm
#endif // MM_HAS_OCC
