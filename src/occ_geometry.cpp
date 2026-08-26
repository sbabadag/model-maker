#ifdef MM_HAS_OCC
#include "model_maker/occ_geometry.hpp"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
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
#include <gp_Circ.hxx>
#include <gp_Ax2.hxx>
#include <GeomAbs_CurveType.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Pnt.hxx>
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
            if (!sharedByTwoFaces(edge)) continue;
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
    BRepBuilderAPI_MakeFace planeFace(plane);
    BRepPrimAPI_MakeHalfSpace halfSpace(planeFace.Face(), reference);
    BRepAlgoAPI_Cut cut(shape, halfSpace.Solid());
    return cut.Shape();
}

TopoDS_Shape extrudeProfileSolid(const SteelProfile& profile, const Vec3& from, const Vec3& to) {
    const double w = profile.width > 0.0 ? profile.width : 50.0;
    const double h = profile.height > 0.0 ? profile.height : 50.0;
    const double t = profile.plateThickness > 0.0 ? profile.plateThickness : 0.0;
    const double hw = w / 2.0;
    const double hh = h / 2.0;

    TopoDS_Shape section;

    // Yuvarlak profil mi? (CHS/CFCHS/ROD/D/P/TUBE/O/Ø — rakamdan onceki harf kumesi)
    std::string alphaPrefix;
    for (const char c : profile.name) {
        if (c >= '0' && c <= '9') break;
        if (c >= 'A' && c <= 'Z') alphaPrefix += c;
    }
    const bool isRound = (alphaPrefix == "CHS" || alphaPrefix == "CFCHS" ||
                          alphaPrefix == "ROD" || alphaPrefix == "D" ||
                          alphaPrefix == "P" || alphaPrefix == "TUBE" ||
                          alphaPrefix == "O");

    if (isRound) {
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

    const gp_Vec direction(to.x - from.x, to.y - from.y, to.z - from.z);
    const double length = direction.Magnitude();
    if (length < 1e-12) return {};
    // Prism yuksekligi = cizgi uzunlugu (birim vektor degil!)
    auto prism = BRepPrimAPI_MakePrism(section, gp_Vec(0.0, 0.0, length));
    gp_Trsf trsf;
    // OCC 7.9'da ilk sistem HEDEF, ikinci KAYNAK (probe ile dogrulandi:
    // SetTransformation(hedef, kaynak) — tersi durumda kiriş X ekseninde kalir).
    trsf.SetTransformation(gp_Ax3(gp_Pnt(from.x, from.y, from.z), gp_Dir(direction)),
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
    return BRepAlgoAPI_Cut(a, b).Shape();
}

double shapeVolume(const TopoDS_Shape& shape) {
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.Mass();
}

WireframeModel solidBoxSolid(double dx, double dy, double dz, double faceDeflection) {
    return shapeToWireframeWithFaces(BRepPrimAPI_MakeBox(dx, dy, dz).Shape(),
                                     faceDeflection, 48);
}

WireframeModel solidCylinderSolid(double radius, double height, double faceDeflection,
                                  int circleSegments) {
    return shapeToWireframeWithFaces(BRepPrimAPI_MakeCylinder(radius, radius, height).Shape(),
                                     faceDeflection, circleSegments);
}

double solidCylinderVolume(double radius, double height) {
    const TopoDS_Shape cylinder = BRepPrimAPI_MakeCylinder(radius, radius, height).Shape();
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
