#pragma once
#ifdef MM_HAS_OCC
#include "model_maker/cut_modifier.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/profile_database.hpp"

#include <TopoDS_Shape.hxx>

#include <filesystem>
#include <functional>

namespace mm {

// OCC ilk yuzeyi: kati kutu uretimi + hacim + STEP yazma/okuma.
// Daha genis modelleme API'si bu modulde buyuyecek (BRep, boolean,
// fillet, STEP/IGES alisverisi, dogrudan goruntuleme).

double boxVolume(double x, double y, double z);

bool writeBoxStep(const std::filesystem::path& path, double x, double y, double z);

double readStepVolume(const std::filesystem::path& path);

// --- Kati ilkeller: OCC katisini uygulamanin WireframeModel'ine donusturur ---

// Herhangi bir TopoDS_Shape'in kenarlarini dogru parcaciklarina ayirir;
// daire/egri kenarlar circleSegments adedine orneklenir, duz kenarlar tek
// parcadir. Paylasilan kose noktalari tek vertex'te birlesir.
WireframeModel shapeToWireframe(const TopoDS_Shape& shape, int circleSegments = 48);

// Yuzey tesselasyonu acikken (faceDeflection > 0) katilarin yuzleri BRepMesh
// ile ucgenlere ayrilir ve modelin faces() listesine eklenir — dolu (solid)
// gorunum stilinde yuzeyler boyali gorunur; 0 iken yalniz tel kafes.
// Katiyi bir duzlemle keser (yarim uzay): planePoint + planeNormal ile
// tanimlanan duzlemin keepPositive tarafindaki tarafi birakir.
TopoDS_Shape cutSolidByPlane(const TopoDS_Shape& shape, const Vec3& planePoint,
                             const Vec3& planeNormal, bool keepPositive);

// --- Tekla tarzi KESME MODIFIER uygulamasi (Phase 4.2) ----------------------
// BooleanCut (Part Cut) icin arac sekli cozumleyici: EntityId -> aracin B-Rep
// sekli (yoksa nullptr). Uygulama katmani bunu OccSolidStore'a baglar.
using ToolShapeResolver = std::function<const TopoDS_Shape*(EntityId)>;
// Bir CutModifier kaydini B-Rep katiya uygular. SONUC YENI sekildir; girdi
// (base) DEGISMEZ (immutabilite kurali). axisFrom/axisTo = hedef parcanin
// profil ekseni — Fitting'in "kisa tarafi otomatik at" karari icin gerekli.
// Desteklenen turler: PlaneCut (keepPositive) + Fitting (otomatik taraf) +
// LineCut (cizgi duzlemi, keepPositive) + PolygonCut (cokgen prizmasi, through
// cut) + BooleanCut (Part Cut: hedeften arac kati cikarilir). BooleanCut icin
// `toolShape` resolver'i gerekir (aracin B-Rep sekli); verilmezse base DONER.
// axisFrom/axisTo = hedef parcanin profil ekseni (Fitting karari icin).
TopoDS_Shape applyCutModifier(const TopoDS_Shape& base, const CutModifier& mod,
                              const Vec3& axisFrom, const Vec3& axisTo,
                              const ToolShapeResolver& toolShape = nullptr);
// Ayni varligin tum ETKIN modifier'larini SIRA ILE uygular (zincir; birden fazla
// kesme sirali uygulanabilir). mods bos/etkin-degil ise base aynen doner.
TopoDS_Shape applyCutModifierChain(const TopoDS_Shape& base,
                                   const std::vector<CutModifier>& mods,
                                   const Vec3& axisFrom, const Vec3& axisTo,
                                   const ToolShapeResolver& toolShape = nullptr);

// Profil kesitini (SteelProfile) from->to ekseni boyunca extrude eder.
// KKR/box profiller icin ici bos kutu; digerleri icin dolu dikdortgen.
// referenceNormal: baslik plakalarinin PARALEL olacagi duzlemin normali
// (varsayilan dunya Z = XY; aktif is duzlemi icin bkz. view_definition.hpp).
// anchor: kesitin uye eksenine gore dusey yerlesimi (Top/Center/Bottom).
// DEFAULT Top ("top of section") — mevcut modellerin davranisi DEGISMEZ.
TopoDS_Shape extrudeProfileSolid(const SteelProfile& profile, const Vec3& from,
                                     const Vec3& to, double rotationDegrees = 0.0,
                                     const Vec3& referenceNormal = Vec3{0.0, 0.0, 1.0},
                                     SectionAnchor anchor = SectionAnchor::Top);

WireframeModel shapeToWireframeWithFaces(const TopoDS_Shape& shape, double faceDeflection,
                                         int circleSegments = 48);

WireframeModel solidBoxSolid(double dx, double dy, double dz, double faceDeflection = 0.15);

WireframeModel solidCylinderSolid(double radius, double height, double faceDeflection = 0.15,
                                  int circleSegments = 48);

// --- Boolean islemler: iki BRep kati uzerinde ---
TopoDS_Shape booleanFuseShape(const TopoDS_Shape& a, const TopoDS_Shape& b);
TopoDS_Shape booleanCommonShape(const TopoDS_Shape& a, const TopoDS_Shape& b);
TopoDS_Shape booleanCutShape(const TopoDS_Shape& a, const TopoDS_Shape& b);

// Herhangi bir seklin hacmi (br^3).
double shapeVolume(const TopoDS_Shape& shape);

// Isin (ray) ile bir B-Rep seklin GERCEK yuzlerinin tam geometrik kesisimi.
// Ucgen mesh yaklasimi yerine OCCT'nin asil yuzeylerine isabet eder; isabet
// eden yuzun normalini (izleyiciye/isinin cikis noktasina dogru yonlenmis,
// birim) ve isin parametresini (mesafe) dondurur. Bu, bulon ekseni = "tiklanan
// yuzun normali" icin dogru kaynaktir: mesh bayat/eksik olsa bile B-Rep her
// zaman mevcuttur. Isabet yoksa false doner (hitPoint/hitNormal/distance
// DEGISMEZ).
bool raycastBrepFace(const TopoDS_Shape& shape, const Vec3& rayOrigin,
                     const Vec3& rayDirection, Vec3& hitPoint, Vec3& hitNormal,
                     double& distance);

WireframeModel solidBoxWireframe(double dx, double dy, double dz);

WireframeModel solidCylinderWireframe(double radius, double height, int circleSegments = 48);

// BRep kati hacmi (br^3) — GProp_GProps uzerinden.
double solidCylinderVolume(double radius, double height);

} // namespace mm
#endif // MM_HAS_OCC
