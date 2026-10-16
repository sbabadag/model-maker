// I-kesit konturu kabul testi — DIK (perpendicular) flans, radius/egim YOK.
//
// extrudeProfileSolid I-kesitleri tek KAPALI keskin-kose kontur olarak uretir
// (Tekla tarzi). Bu test, kesit alaninin (hacim / uzunluk) KESKIN-KOSE
// formulune 2*b*tf + tw*(h-2*tf) uydugunu ve katalog A degerinin ALTINDA
// (fason/radius eklenmedigi) kaldigini kilitler; bbox olculeri de dogrulanir.
#include "model_maker/occ_geometry.hpp"
#include "model_maker/profile_database.hpp"
#include "model_maker/geometry.hpp"

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
#define CHECK(cond, msg) do { if(!(cond)){ ++failures; std::printf("FAIL: %s\n", msg);} else { std::printf("PASS %s\n", msg);} } while(0)

static double solidVolume(const TopoDS_Shape& s) {
    GProp_GProps g;
    BRepGProp::VolumeProperties(s, g);
    return g.Mass();
}

// Kesit alanini (hacim / uzunluk) KESKIN-KOSE formulu (2*b*tf + tw*(h-2tf))
// ile karsilastirir. catalogA = katalog degeri (fason/radius eklenince daha
// buyuk olur — area < catalogA saglanir).
static void checkProfileArea(const SteelProfile& p, double catalogA, double length) {
    TopoDS_Shape s = extrudeProfileSolid(p, Vec3{0,0,0}, Vec3{0,0,length}, 0.0, Vec3{0,1,0});
    double area = solidVolume(s) / length;
    const double sharp = 2.0 * p.width * p.flangeThickness +
                         p.plateThickness * (p.height - 2.0 * p.flangeThickness);
    std::printf("  %s area = %.1f (keskin=%.1f, katalog A=%.1f)\n",
                p.name.c_str(), area, sharp, catalogA);
    CHECK(std::fabs(area - sharp) / sharp < 0.005,
          (p.name + " kesit alani keskin-kose formuluyle uyumlu").c_str());
    CHECK(area < catalogA,
          (p.name + " fason/radius eklenmemis (area < katalog A)").c_str());
}

int main() {
    // IPE200: h=200 b=100 tw=5.6 tf=8.5 r=12  A=28.48 cm2 = 2848 mm2
    SteelProfile ipe200;
    ipe200.name = "IPE200";
    ipe200.height = 200.0; ipe200.width = 100.0;
    ipe200.plateThickness = 5.6; ipe200.flangeThickness = 8.5;
    ipe200.roundingRadius = 12.0;
    checkProfileArea(ipe200, 2848.0, 1000.0);

    // IPE300: h=300 b=150 tw=7.1 tf=10.7 r=15  A=53.81 cm2 = 5381 mm2
    SteelProfile ipe300;
    ipe300.name = "IPE300";
    ipe300.height = 300.0; ipe300.width = 150.0;
    ipe300.plateThickness = 7.1; ipe300.flangeThickness = 10.7;
    ipe300.roundingRadius = 15.0;
    checkProfileArea(ipe300, 5381.0, 1000.0);

    // HEB300: h=300 b=300 tw=11 tf=19 r=27  A=149.1 cm2 = 14910 mm2
    SteelProfile heb300;
    heb300.name = "HEB300";
    heb300.height = 300.0; heb300.width = 300.0;
    heb300.plateThickness = 11.0; heb300.flangeThickness = 19.0;
    heb300.roundingRadius = 27.0;
    checkProfileArea(heb300, 14910.0, 1000.0);

    // HEA200: h=190 b=200 tw=6.5 tf=10 r=18  A=53.83 cm2 = 5383 mm2
    SteelProfile hea200;
    hea200.name = "HEA200";
    hea200.height = 190.0; hea200.width = 200.0;
    hea200.plateThickness = 6.5; hea200.flangeThickness = 10.0;
    hea200.roundingRadius = 18.0;
    checkProfileArea(hea200, 5383.0, 1000.0);

    // bbox dogrulugu: IPE200 yukseklik 200, genislik 100, uzunluk 1000.
    TopoDS_Shape s = extrudeProfileSolid(ipe200, Vec3{0,0,0}, Vec3{0,0,1000}, 0.0, Vec3{0,1,0});
    Bnd_Box bb;
    BRepBndLib::Add(s, bb);
    double xmin,ymin,zmin,xmax,ymax,zmax;
    bb.Get(xmin,ymin,zmin,xmax,ymax,zmax);
    std::printf("  IPE200 bbox: X[%.1f,%.1f] Y[%.1f,%.1f] Z[%.1f,%.1f]\n",
                xmin,xmax,ymin,ymax,zmin,zmax);
    CHECK(std::fabs((xmax-xmin) - 100.0) < 1.0, "IPE200 bbox genislik = 100");
    CHECK(std::fabs((ymax-ymin) - 200.0) < 1.0, "IPE200 bbox yukseklik = 200");
    CHECK(std::fabs((zmax-zmin) - 1000.0) < 1.0, "IPE200 bbox uzunluk = 1000");

    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
