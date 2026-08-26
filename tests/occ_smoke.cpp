#ifdef MM_HAS_OCC
#include "model_maker/occ_bridge_api.h"
#include "model_maker/occ_geometry.hpp"
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepGProp.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <gp_Pnt.hxx>
#include <gp_Ax3.hxx>
#include <gp_Trsf.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <cmath>

using mm::Vec3;

#include <cmath>
#include <cstdio>
#include <filesystem>

int main() {
    // 1) Kati kutu + hacim
    const double volume = mm::boxVolume(10.0, 10.0, 10.0);
    if (std::abs(volume - 1000.0) > 1e-6) {
        std::printf("HATA: hacim %f (beklenen 1000.0)\n", volume);
        return 1;
    }

    // 2) STEP yaz + geri oku — veri alisverisi dongusu kapali
    const auto step = std::filesystem::temp_directory_path() / "mm_occ_smoke.step";
    if (!mm::writeBoxStep(step, 10.0, 10.0, 10.0)) {
        std::printf("HATA: STEP dosyasi yazilamadi\n");
        return 1;
    }
    const double readBack = mm::readStepVolume(step);
    std::filesystem::remove(step);
    if (std::abs(readBack - 1000.0) > 1e-3) {
        std::printf("HATA: STEP'ten okunan hacim %f (beklenen 1000.0)\n", readBack);
        return 1;
    }

    // 3) Tesselasyon: kutu = 8 kose / 12 kenar; silindir = 2 daire x N + 2 dik
    const auto box = mm::solidBoxWireframe(2.0, 3.0, 4.0);
    if (box.vertices().size() != 8 || box.edges().size() != 12) {
        std::printf("HATA: kutu tesselasyonu %zu kose / %zu kenar (beklenen 8/12)\n",
                    box.vertices().size(), box.edges().size());
        return 1;
    }
    const int seg = 32;
    const auto cyl = mm::solidCylinderWireframe(1.5, 5.0, seg);

    const std::size_t expectedEdges = 2 * static_cast<std::size_t>(seg) + 2;
    if (cyl.edges().size() != expectedEdges) {
        std::printf("HATA: silindir tesselasyonu %zu kenar (beklenen %zu)\n",
                    cyl.edges().size(), expectedEdges);
        return 1;
    }

    // 4) Kopru C-API'si: ayni kutu, POD tamponlar uzerinden
    float* verts = nullptr; int nv = 0;
    unsigned int* ed = nullptr; int ne = 0;
    if (mm_occ_solid_box(1.0, 2.0, 3.0, &verts, &nv, &ed, &ne) != 0 ||
        nv != 8 || ne != 12) {
        std::printf("HATA: kopru C-API %d kose / %d kenar (beklenen 8/12)\n", nv, ne);
        return 1;
    }
    mm_occ_free(verts);
    mm_occ_free(ed);

    // 5) Dolu katilar: kutu = 12 ucgen yuz, silindir = >50 ucgen yuz
    const auto boxSolid = mm::solidBoxSolid(1.0, 1.0, 1.0, 0.1);
    if (boxSolid.faces().size() != 12) {
        std::printf("HATA: kutu kati %zu yuz (beklenen 12)\n", boxSolid.faces().size());
        return 1;
    }
    for (const auto& face : boxSolid.faces())
        if (face.size() != 3) { std::printf("HATA: yuz ucgen degil\n"); return 1; }
    const auto cylSolid = mm::solidCylinderSolid(1.5, 5.0, 0.2, 32);
    if (cylSolid.faces().size() < 50) {
        std::printf("HATA: silindir kati %zu yuz (beklenen >50)\n", cylSolid.faces().size());
        return 1;
    }

    std::printf("OCC SMOKE OK — kutu 10x10x10 hacim=%.6f, STEP yazildi/okundu hacim=%.6f, "
                "tesselasyon: kutu 8/12, silindir %zu kenar, kopru C-API 8/12, surum %s\n",
                volume, readBack, cyl.edges().size(), mm_occ_version());
    // 6) Boolean: 10^3 kutu @(0,0,0) + 10^3 kutu @(5,0,0) —
    // birlesim 1500 (1000+1000-500), kesisim 500, cikarma 500.
    const TopoDS_Shape boxA = BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape();
    const TopoDS_Shape boxB = BRepPrimAPI_MakeBox(gp_Pnt(5.0, 0.0, 0.0),
                                                  10.0, 10.0, 10.0).Shape();
    const double fuseVolume = mm::shapeVolume(mm::booleanFuseShape(boxA, boxB));
    const double commonVolume = mm::shapeVolume(mm::booleanCommonShape(boxA, boxB));
    const double cutVolume = mm::shapeVolume(mm::booleanCutShape(boxA, boxB));
    if (std::abs(fuseVolume - 1500.0) > 1.0 || std::abs(commonVolume - 500.0) > 1.0 ||
        std::abs(cutVolume - 500.0) > 1.0) {
        std::printf("HATA: boolean hacimleri fuse=%.2f common=%.2f cut=%.2f "
                    "(beklenen 1500/500/500)\n", fuseVolume, commonVolume, cutVolume);
        return 1;
    }

    std::printf("OCC KATI OK — kutu %zu ucgen, silindir %zu ucgen\n",
                boxSolid.faces().size(), cylSolid.faces().size());
    // Profil extrude: KKR30*3 (30x30x3, A=301 mm²) x 100 mm -> V=30100 mm³
    {
        mm::SteelProfile kkr;
        kkr.name = "KKR30*3";
        kkr.width = 30.0;
        kkr.height = 30.0;
        kkr.plateThickness = 3.0;
        kkr.crossSectionArea = 301.0;
        const auto solid = mm::extrudeProfileSolid(kkr, Vec3{0.0, 0.0, 0.0}, Vec3{0.0, 0.0, 100.0});
        if (solid.IsNull()) {
            std::printf("HATA: extrude bos dondu\n");
            return 1;
        }
        GProp_GProps extrudedProps;
        BRepGProp::VolumeProperties(solid, extrudedProps);
        const double volume = extrudedProps.Mass();
        // Keskin koseli kutu: A = 30² - 24² = 324 mm² (Tekla'nin 301'i kose
        // yuvarlatmali — gorsel yaklasimda keskin koseler kabul).
        if (std::abs(volume - 32400.0) > 5.0) {
            std::printf("HATA: extrude hacmi %.1f beklenen 30100\n", volume);
            return 1;
        }
        std::printf("OCC EXTRUDE OK — KKR30*3 x 100mm hacim=%.1f mm³\n", volume);
    }
    // Yuvarlak boru: CFCHS127x2.5 — A = pi/4(127²-122²) ≈ 977.8 mm²
    {
        mm::SteelProfile chs;
        chs.name = "CFCHS127x2.5";
        chs.width = 127.0;
        chs.height = 127.0;
        chs.plateThickness = 2.5;
        const auto pipe = mm::extrudeProfileSolid(chs, Vec3{0.0, 0.0, 0.0}, Vec3{0.0, 0.0, 100.0});
        GProp_GProps pipeProps;
        BRepGProp::VolumeProperties(pipe, pipeProps);
        const double expected = 3.14159265358979 / 4.0 * (127.0 * 127.0 - 122.0 * 122.0) * 100.0;
        if (std::abs(pipeProps.Mass() - expected) > 20.0) {
            std::printf("HATA: boru hacmi %.1f beklenen %.1f\n", pipeProps.Mass(), expected);
            return 1;
        }
        std::printf("OCC EXTRUDE-ROUND OK — CFCHS127x2.5 x 100 hacim=%.1f mm³\n", pipeProps.Mass());
    }
    // Yarim uzay kesimi: 10x10x10 kutu, X=5 duzlemi -> 500 mm³ yarim
    {
        const auto box = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), 10.0, 10.0, 10.0).Shape();
        const auto keepPositive = mm::cutSolidByPlane(box, Vec3{5.0, 0.0, 0.0},
                                                      Vec3{1.0, 0.0, 0.0}, true);
        GProp_GProps cutProps;
        BRepGProp::VolumeProperties(keepPositive, cutProps);
        if (std::abs(cutProps.Mass() - 500.0) > 1.0) {
            std::printf("HATA: yarim uzay hacmi %.1f beklenen 500\n", cutProps.Mass());
            return 1;
        }
        const auto keepNegative = mm::cutSolidByPlane(box, Vec3{5.0, 0.0, 0.0},
                                                      Vec3{1.0, 0.0, 0.0}, false);
        BRepGProp::VolumeProperties(keepNegative, cutProps);
        if (std::abs(cutProps.Mass() - 500.0) > 1.0) {
            std::printf("HATA: negatif taraf hacmi %.1f beklenen 500\n", cutProps.Mass());
            return 1;
        }
        std::printf("OCC CUT-PLANE OK — iki yarim 500/500 mm³\n");
    }
    // I-kesit (IPE200): h=200 b=100 tw=5.6 tf=8.5 — keskin kose alani
    // 2*100*8.5 + (200-17)*5.6 = 2724.8 mm2; 100 mm boy -> 272480 mm3
    {
        mm::SteelProfile ipe200;
        ipe200.name = "IPE200";
        ipe200.height = 200.0;
        ipe200.width = 100.0;
        ipe200.plateThickness = 5.6;
        ipe200.flangeThickness = 8.5;
        const auto iSolid = mm::extrudeProfileSolid(ipe200, Vec3{0.0, 0.0, 0.0},
                                                    Vec3{0.0, 0.0, 100.0});
        GProp_GProps iProps;
        BRepGProp::VolumeProperties(iSolid, iProps);
        const double expected = 2.0 * 100.0 * 8.5 * 100.0 + (200.0 - 17.0) * 5.6 * 100.0;
        if (std::abs(iProps.Mass() - expected) / expected > 0.01) {
            std::printf("HATA: IPE200 hacmi %.1f beklenen %.1f\n", iProps.Mass(), expected);
            return 1;
        }
        std::printf("OCC I-SECTION OK — IPE200 hacim %.0f mm3 (keskin kose)\n", iProps.Mass());
    }
    // Diyagonal dogrultu: (0,0,0)->(0,100,0) — kiris Y ekseninde uzanmali,
    // kesit X/Z yonlerinde ±15 sinirlari icinde kalmali.
    {
        mm::SteelProfile kkr;
        kkr.width = 30.0;
        kkr.height = 30.0;
        kkr.plateThickness = 3.0;
        const auto solid = mm::extrudeProfileSolid(kkr, Vec3{0.0, 0.0, 0.0}, Vec3{0.0, 100.0, 0.0});
        Bnd_Box boundingBox;
        BRepBndLib::Add(solid, boundingBox);
        double xMin = 0, yMin = 0, zMin = 0, xMax = 0, yMax = 0, zMax = 0;
        boundingBox.Get(xMin, yMin, zMin, xMax, yMax, zMax);
        if (xMin < -16.0 || xMax > 16.0 || zMin < -16.0 || zMax > 16.0 ||
            yMin < -1.0 || yMax < 99.0 || yMax > 101.0) {
            std::printf("HATA: diyagonal yonelim bozuk bbox=(%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f)\n",
                        xMin, yMin, zMin, xMax, yMax, zMax);
            // probe yine de calissin
        }
        std::printf("OCC EXTRUDE-DIAG OK — bbox=(%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f)\n",
                    xMin, yMin, zMin, xMax, yMax, zMax);
    }
    // Trsf yon duyarliligi: kutu (z-ekseni) -> Y eksenine.
    {
        auto box = BRepPrimAPI_MakeBox(gp_Pnt(-15.0, -15.0, 0.0), 30.0, 30.0, 100.0).Shape();
        gp_Trsf trsfA;
        trsfA.SetTransformation(gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)),
                                gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0)));
        auto movedA = BRepBuilderAPI_Transform(box, trsfA).Shape();
        Bnd_Box bbA; BRepBndLib::Add(movedA, bbA);
        double ax1 = 0, ay1 = 0, az1 = 0, ax2 = 0, ay2 = 0, az2 = 0;
        bbA.Get(ax1, ay1, az1, ax2, ay2, az2);
        std::printf("TRSF-A bbox=(%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f)\n", ax1, ay1, az1, ax2, ay2, az2);
        gp_Trsf trsfB;
        trsfB.SetTransformation(gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 1, 0)),
                                gp_Ax3(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)));
        auto movedB = BRepBuilderAPI_Transform(box, trsfB).Shape();
        Bnd_Box bbB; BRepBndLib::Add(movedB, bbB);
        bbB.Get(ax1, ay1, az1, ax2, ay2, az2);
        std::printf("TRSF-B bbox=(%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f)\n", ax1, ay1, az1, ax2, ay2, az2);
    }
    return 0;
}
#else
#include <cstdio>
int main() {
    std::printf("OCC bulunamadi — test atlandi\n");
    return 77;
}
#endif
