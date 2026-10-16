// P1 KABUL SENARYOSU — GERCEK OCCT dogrulama (Fitting + egimli duzlem +
// duzlemi TASIMA = yeniden hesap).
//
// Brief'in "Ilk kabul senaryosu"nun cekirdegini kilitler:
//   HEB300 benzeri I-kesit kati olustur -> EGIMLI Construction Plane -> Fitting
//   -> duzlemi TASI -> kesim GUNCELLENIR (ve orta noktayi gecince otomatik taraf
//   secimi TERSINE doner). Orijinal taban B-Rep HIC DEGISMEZ.
//
// Hacim ORANLARI yalniz DIK duzlemlerle (normal +X) kesin dogrulanir; egimli
// duzlemler kama kestigi icin hacim orani kapali-form degildir — onlar bbox
// (hangi ucun kesildigi) + "hacim < taban" ile dogrulanir.
//
// Derleme (bu host, Linux OCCT):
//   g++ -std=gnu++20 -O1 -DMM_HAS_OCC -I include -I /usr/include/opencascade \
//       tests/occ_fitting_acceptance.cpp src/occ_geometry.cpp src/geometry.cpp \
//       src/cut_modifier.cpp src/profile_database.cpp \
//       -lTKernel -lTKMath -lTKG2d -lTKG3d -lTKGeomBase -lTKBRep -lTKGeomAlgo \
//       -lTKTopAlgo -lTKPrim -lTKBO -lTKBool -lTKShHealing -lTKMesh -lTKXSBase \
//       -lTKDESTEP -o occ_fitting_acceptance && ./occ_fitting_acceptance

#include "model_maker/cut_modifier.hpp"
#include "model_maker/occ_geometry.hpp"
#include "model_maker/profile_database.hpp"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

static void bbox(const TopoDS_Shape& s, double& xMin, double& yMin, double& zMin,
                 double& xMax, double& yMax, double& zMax) {
    Bnd_Box b;
    BRepBndLib::Add(s, b);
    b.Get(xMin, yMin, zMin, xMax, yMax, zMax);
}

int main() {
    // HEB300 benzeri I-kesit (ad "HEB300" -> classifyProfileSection = ISection).
    SteelProfile heb300;
    heb300.name = "HEB300";
    heb300.height = 300.0;
    heb300.width = 300.0;
    heb300.plateThickness = 11.0;   // govde
    heb300.flangeThickness = 19.0;  // baslik
    heb300.roundingRadius = 27.0;

    const Vec3 axisFrom{0.0, 0.0, 0.0};
    const Vec3 axisTo{6000.0, 0.0, 0.0};
    const TopoDS_Shape base = extrudeProfileSolid(heb300, axisFrom, axisTo);
    const double baseVol = shapeVolume(base);
    check(baseVol > 1e6, "HEB300 6m katisi olustu (hacim > 1e6 mm^3)");

    const Vec3 nx{1.0, 0.0, 0.0};  // DIK duzlem (kesin hacim oranlari icin)

    // --- 1) EGIMLI duzlem (XZ'de 45 deg; Y sabit). ---
    const double invSqrt2 = 1.0 / std::sqrt(2.0);
    const Vec3 nInclined{invSqrt2, 0.0, invSqrt2};

    // 1a) Fitting x=2000 -> kisa from-tarafi kesilir (bbox ile dogrulanir).
    {
        CutModifier fit;
        fit.kind = CutKind::Fitting;
        fit.planeOrigin = {2000.0, 0.0, 0.0};
        fit.planeNormal = nInclined;
        const TopoDS_Shape cut = applyCutModifier(base, fit, axisFrom, axisTo);
        double xMin, yMin, zMin, xMax, yMax, zMax;
        bbox(cut, xMin, yMin, zMin, xMax, yMax, zMax);
        check(xMin > 1000.0 && xMax > 5900.0, "egimli fitting x=2000: from ucu kesildi");
        const double vol = shapeVolume(cut);
        check(vol > 0.0 && vol < baseVol, "egimli fitting x=2000: kama kesildi (0 < hacim < taban)");
    }

    // 1b) Duzlemi TASI (x=3500): orta nokta gecildi -> taraf TERSINE doner,
    //     simdi KISA to-tarafi kesilir.
    {
        CutModifier fit;
        fit.kind = CutKind::Fitting;
        fit.planeOrigin = {3500.0, 0.0, 0.0};
        fit.planeNormal = nInclined;
        const TopoDS_Shape cut = applyCutModifier(base, fit, axisFrom, axisTo);
        double xMin, yMin, zMin, xMax, yMax, zMax;
        bbox(cut, xMin, yMin, zMin, xMax, yMax, zMax);
        check(xMin < 100.0 && xMax < 4500.0,
              "egimli fitting x=3500: taraf tersine dondu, to ucu kesildi");
    }

    // --- 2) DIK duzlemle KESIN hacim oranlari. ---
    {
        CutModifier fit;
        fit.kind = CutKind::Fitting;
        fit.planeOrigin = {2000.0, 0.0, 0.0};
        fit.planeNormal = nx;
        const double ratio = shapeVolume(applyCutModifier(base, fit, axisFrom, axisTo)) / baseVol;
        check(std::abs(ratio - 4000.0 / 6000.0) < 0.01, "dik fitting x=2000: ~2/3 kati kalir");
    }
    {
        CutModifier fit;
        fit.kind = CutKind::Fitting;
        fit.planeOrigin = {3500.0, 0.0, 0.0};
        fit.planeNormal = nx;
        const double ratio = shapeVolume(applyCutModifier(base, fit, axisFrom, axisTo)) / baseVol;
        check(std::abs(ratio - 3500.0 / 6000.0) < 0.01,
              "dik fitting x=3500: ~0.583 kati kalir (taraf tersine dondu)");
    }
    {
        CutModifier pc;
        pc.kind = CutKind::PlaneCut;
        pc.planeOrigin = {2000.0, 0.0, 0.0};
        pc.planeNormal = nx;
        pc.keepPositive = true;
        const double ratio = shapeVolume(applyCutModifier(base, pc, axisFrom, axisTo)) / baseVol;
        check(std::abs(ratio - 4000.0 / 6000.0) < 0.01, "dik planeCut x=2000 keep+: ~2/3 kati kalir");
    }
    // --- P8) Yon tersi (invertSide): Alt+sol tik davranisinin OCC katmani. ---
    {
        CutModifier pc;
        pc.kind = CutKind::PlaneCut;
        pc.planeOrigin = {2000.0, 0.0, 0.0};
        pc.planeNormal = nx;
        pc.keepPositive = true;   // normal +X (x>2000) korunur
        pc.invertSide = true;     // P8: ters -> x<2000 korunur
        const double ratio = shapeVolume(applyCutModifier(base, pc, axisFrom, axisTo)) / baseVol;
        check(std::abs(ratio - 2000.0 / 6000.0) < 0.01, "P8 planeCut invertSide: ~1/3 kati kalir (x<2000)");
    }
    {
        CutModifier fit;
        fit.kind = CutKind::Fitting;
        fit.planeOrigin = {2000.0, 0.0, 0.0};
        fit.planeNormal = nx;
        fit.invertSide = true;    // otomatik kisa-from tarafi ters -> uzun taraf atilir
        // Normal fitting x=2000: kisa from (x<2000) atilir -> 4000 kalir (2/3).
        // invertSide: ters -> x>2000 atilir -> 2000 kalir (1/3).
        const double ratio = shapeVolume(applyCutModifier(base, fit, axisFrom, axisTo)) / baseVol;
        check(std::abs(ratio - 2000.0 / 6000.0) < 0.01, "P8 fitting invertSide: ~1/3 kati kalir");
    }
    // --- 3) Zincir: fit(x=2000, koru +X) + planeCut(x=4000, koru -X) -> x in [2000,4000].
    {
        CutModifier fit;
        fit.kind = CutKind::Fitting;
        fit.planeOrigin = {2000.0, 0.0, 0.0};
        fit.planeNormal = nx;
        CutModifier pc;
        pc.kind = CutKind::PlaneCut;
        pc.planeOrigin = {4000.0, 0.0, 0.0};
        pc.planeNormal = nx;
        pc.keepPositive = false;  // -X tarafi (x<4000) korunur
        std::vector<CutModifier> mods{fit, pc};
        const TopoDS_Shape cut = applyCutModifierChain(base, mods, axisFrom, axisTo);
        double xMin, yMin, zMin, xMax, yMax, zMax;
        bbox(cut, xMin, yMin, zMin, xMax, yMax, zMax);
        const double ratio = shapeVolume(cut) / baseVol;
        check(std::abs(ratio - 2000.0 / 6000.0) < 0.01, "zincir fit(2000)+planeCut(4000): ~1/3 kati kalir");
        check(xMin > 1500.0 && xMax < 4500.0, "zincir: x in [2000,4000]");
    }

    // --- P9) UZATMA: duzlem ekseni kesmiyorsa yakin ucu duzleme kadar uzatir.
    {
        // Duzlem from ucunun GERISINDE (x=-500): kiris from ucundan x=-500'e uzar.
        CutModifier fit;
        fit.kind = CutKind::Fitting;
        fit.planeOrigin = {-500.0, 0.0, 0.0};
        fit.planeNormal = nx;
        double xMin, yMin, zMin, xMax, yMax, zMax;
        bbox(applyCutModifier(base, fit, axisFrom, axisTo), xMin, yMin, zMin, xMax, yMax, zMax);
        const double ratio = shapeVolume(applyCutModifier(base, fit, axisFrom, axisTo)) / baseVol;
        check(std::abs(ratio - 6500.0 / 6000.0) < 0.01, "P9 fitting x=-500: kiris uzar (~6500/6000 hacim)");
        check(xMin < -450.0 && xMax > 5900.0, "P9 fitting x=-500: bbox -500..6000");
    }
    {
        // Duzlem to ucunun OTESINDE (x=6500): kiris to ucundan x=6500'e uzar.
        CutModifier fit;
        fit.kind = CutKind::Fitting;
        fit.planeOrigin = {6500.0, 0.0, 0.0};
        fit.planeNormal = nx;
        double xMin, yMin, zMin, xMax, yMax, zMax;
        bbox(applyCutModifier(base, fit, axisFrom, axisTo), xMin, yMin, zMin, xMax, yMax, zMax);
        const double ratio = shapeVolume(applyCutModifier(base, fit, axisFrom, axisTo)) / baseVol;
        check(std::abs(ratio - 6500.0 / 6000.0) < 0.01, "P9 fitting x=6500: kiris uzar (~6500/6000 hacim)");
        check(xMin > -50.0 && xMax > 6400.0, "P9 fitting x=6500: bbox 0..6500");
    }
    {
        // EGIMLI duzlem from ucunun gerisinde: kama uzatma (hacim > taban).
        CutModifier fit;
        fit.kind = CutKind::Fitting;
        fit.planeOrigin = {-500.0, 0.0, 0.0};
        fit.planeNormal = nInclined;
        const double vol = shapeVolume(applyCutModifier(base, fit, axisFrom, axisTo));
        check(vol > baseVol, "P9 egimli fitting x=-500: kiris uzadi (hacim > taban)");
    }

    // --- P10) EGIMLI kismi kesisim: bir kose uzun (kesilir), digeri kisa
    //          (uzatilir) -> uc yuzeyi tam duzlemde, bosluk kalmaz.
    {
        // XY'de 45 deg egimli duzlem, to ucuna yakin (ekseni x=5900'de keser).
        // Duzlem denklemi x = 5900 - y: y=+150'de x=5750 (uzun kose, kesilir),
        // y=-150'de x=6050 (kisa kose, uzatilmalidir).
        const Vec3 nXY{invSqrt2, invSqrt2, 0.0};
        CutModifier fit;
        fit.kind = CutKind::Fitting;
        fit.planeOrigin = {5900.0, 0.0, 0.0};
        fit.planeNormal = nXY;
        const TopoDS_Shape cut = applyCutModifier(base, fit, axisFrom, axisTo);
        double xMin, yMin, zMin, xMax, yMax, zMax;
        bbox(cut, xMin, yMin, zMin, xMax, yMax, zMax);
        // Kisa kose uzatildi -> xMax ~6050 (eski davranis 6000'de birakirdi).
        check(xMax > 6040.0, "P10 egimli kismi kesisim: kisa kose uzatildi (xMax>6040)");
        check(xMin > -50.0, "P10 egimli kismi kesisim: from ucu yerinde (xMin~0)");
    }

    // --- 4) Taban immutabilite: tum islemlerden sonra base DEGISMEDI.
    check(std::abs(shapeVolume(base) - baseVol) < 1e-3, "taban B-Rep degismedi");

    std::printf("%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
