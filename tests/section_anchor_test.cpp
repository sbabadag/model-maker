// SECTION ANCHOR (Slice 2d-2c P3) — OCC'siz core test.
//
// Mevcut "top of section" kesit yerlesiminin KALICI dogruluk kaynagi oldugunu
// ve SectionAnchor (Top/Center/Bottom) ofsetlerinin dogru oldugunu dogrular.
// DEFAULT Top korunur; var olan modeller sessizce baska anchor'a donusturulmez.
//
// windows.h yok; Linux'ta kosar.

#include "model_maker/profile_database.hpp"

#include <cstdio>
#include <cmath>

using namespace mm;

static int g_checks = 0;
static int g_failures = 0;
static void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL %s\n", what);
    }
}

static bool approx(double a, double b, double tol = 1e-9) { return std::abs(a - b) < tol; }

int main() {
    // HEB300 benzeri I-kesit: h=300 -> topY=150.
    SteelProfile heb;
    heb.name = "HEB300";
    heb.height = 300.0;
    heb.width = 300.0;
    heb.plateThickness = 11.0;
    heb.flangeThickness = 19.0;

    check(approx(profileSectionTopY(heb), 150.0), "HEB300 topY = 150 (h/2)");
    check(approx(profileSectionAnchorOffset(heb, SectionAnchor::Top), -150.0),
          "Top anchor ofseti -150 (ust yuzey eksende — mevcut davranis)");
    check(approx(profileSectionAnchorOffset(heb, SectionAnchor::Center), 0.0),
          "Center anchor ofseti 0 (merkez eksende)");
    check(approx(profileSectionAnchorOffset(heb, SectionAnchor::Bottom), 150.0),
          "Bottom anchor ofseti +150 (alt yuzey eksende)");

    // Yuvarlak (CHS): h=w=200 -> topY=100.
    SteelProfile chs;
    chs.name = "CHS200";
    chs.height = 200.0;
    chs.width = 200.0;
    chs.plateThickness = 10.0;
    check(approx(profileSectionTopY(chs), 100.0), "CHS200 topY = 100 (dis yaricap)");
    check(approx(profileSectionAnchorOffset(chs, SectionAnchor::Top), -100.0),
          "CHS Top -100");
    check(approx(profileSectionAnchorOffset(chs, SectionAnchor::Center), 0.0),
          "CHS Center 0");
    check(approx(profileSectionAnchorOffset(chs, SectionAnchor::Bottom), 100.0),
          "CHS Bottom +100");

    // DEFAULT Top: ofset, eski hard-coded -sectionTopY ile BIREBIR ayni.
    // Bu, "mevcut modeller sessizce degistirilmez" garantisinin matematiksel
    // temelidir (eski kod -profileSectionTopY uyguluyordu).
    check(approx(profileSectionAnchorOffset(heb, SectionAnchor::Top),
               -profileSectionTopY(heb)),
          "default Top == eski -sectionTopY (davranis DEGISMEDI)");

    std::printf("section_anchor: %d kontrol, %d basarisiz\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
