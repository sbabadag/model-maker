// PARAMETRIK BULON (Phase B1) — cekirdek test (OCC'siz).
//
// BoltGroup veri modeli + delik CutModifier uretimi + MMW8 cap round-trip.
// windows.h yok; Linux'ta kosar.

#include "model_maker/bolt.hpp"
#include "model_maker/mmw8_codec.hpp"

#include <cmath>
#include <cstdio>
#include <sstream>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

static bool near(double a, double b, double tol = 1e-9) {
    return std::abs(a - b) < tol;
}

int main() {
    // 1) Standard bulon boyutlari.
    const BoltDimensions d = boltDimensions(20.0);
    check(near(d.headAcrossFlats, 30.0), "bas kosegen s=1.5*d");
    check(near(d.headHeight, 14.0), "bas yuksekligi k=0.7*d");
    check(near(d.nutAcrossFlats, 30.0), "somun kosegen s=1.5*d");
    check(near(d.nutHeight, 16.0), "somun yuksekligi m=0.8*d");
    check(near(d.washerOuterDiameter, 40.0), "pul dis cap D=2*d");
    check(near(d.washerThickness, 3.0), "pul kalinligi t=0.15*d");
    // Sifir/negatif cap -> 1mm taban (degenereden kac).
    check(near(boltDimensions(0.0).headAcrossFlats, 1.5), "0 cap -> 1mm taban");

    // 2) makeHoleCutModifier.
    const CutModifier m = makeHoleCutModifier(7, {10, 20, 30}, {0, 0, 2}, 22.0);
    check(m.kind == CutKind::Hole, "kind = Hole");
    check(m.target == 7, "hedef EntityId korunur");
    check(near(m.diameter, 22.0), "delik capi 22");
    check(near(m.planeNormal.x, 0.0) && near(m.planeNormal.y, 0.0) &&
              near(m.planeNormal.z, 1.0),
          "eksen normalize edilir");
    check(m.planeOrigin == Vec3{10, 20, 30}, "merkez korunur");
    // Dejenere eksen -> +Z.
    const CutModifier m0 = makeHoleCutModifier(7, {0, 0, 0}, {0, 0, 0}, 22.0);
    check(near(m0.planeNormal.z, 1.0), "sifir eksen -> +Z");

    // 3) boltGroupHoleModifiers: gecersiz id atlanir.
    BoltGroup bg;
    bg.parts = {1, 2, kInvalidEntityId, 4};
    bg.origin = {5, 6, 7};
    bg.axis = {0, 0, 1};
    bg.params.holeDiameter = 22.0;
    const auto holes = boltGroupHoleModifiers(bg);
    check(holes.size() == 3, "3 gecerli parca -> 3 delik (invalid atlanir)");
    for (const auto& h : holes) {
        check(h.kind == CutKind::Hole && near(h.diameter, 22.0), "delik capi 22");
        check(h.planeOrigin == bg.origin, "delik merkezi = bulon merkezi");
    }
    check(holes[0].target == 1 && holes[1].target == 2 && holes[2].target == 4,
          "hedef parcalar sirasi korunur");

    // 3b) boltGroupBoltOrigins: rectangular pattern.
    {
        BoltGroup p;
        p.origin = {0, 0, 0};
        p.axis = {0, 0, 1};
        p.xDir = {1, 0, 0};
        p.countX = 2; p.countY = 1; p.spacingX = 50.0; p.spacingY = 0.0;
        const auto o1 = boltGroupBoltOrigins(p);
        check(o1.size() == 2, "2x1 pattern -> 2 merkez");
        if (o1.size() == 2) {
            check(o1[0] == Vec3{0, 0, 0}, "ilk merkez = origin");
            check(o1[1] == Vec3{50, 0, 0}, "ikinci merkez +x spacingX");
        }
        p.countX = 2; p.countY = 2; p.spacingX = 50.0; p.spacingY = 100.0;
        const auto o2 = boltGroupBoltOrigins(p);
        check(o2.size() == 4, "2x2 pattern -> 4 merkez");
        if (o2.size() == 4) {
            check(o2[0] == Vec3{0, 0, 0}, "grid (0,0)");
            check(o2[1] == Vec3{50, 0, 0}, "grid (1,0)");
            check(o2[2] == Vec3{0, 100, 0}, "grid (0,1)");
            check(o2[3] == Vec3{50, 100, 0}, "grid (1,1)");
        }
        // y yonu = axis x xDir = (0,0,1)x(1,0,0) = (0,1,0) -> +y dogru.
        // Gecersiz adet -> 1.
        p.countX = 0; p.countY = -3; p.spacingX = 10; p.spacingY = 20;
        const auto o3 = boltGroupBoltOrigins(p);
        check(o3.size() == 1 && o3[0] == p.origin, "gecersiz adet -> tek merkez");
    }

    // 3c) boltGroupHoleModifiers pattern: 2x1 bolt x 3 parca -> 6 delik.
    {
        BoltGroup p;
        p.parts = {1, 2, kInvalidEntityId, 4};
        p.origin = {5, 6, 7};
        p.axis = {0, 0, 1};
        p.xDir = {1, 0, 0};
        p.countX = 2; p.countY = 1; p.spacingX = 50.0; p.spacingY = 0.0;
        p.params.holeDiameter = 22.0;
        const auto hs = boltGroupHoleModifiers(p);
        check(hs.size() == 6, "2 bolt x 3 parca -> 6 delik");
        if (hs.size() == 6) {
            // Siralama origin-major: her merkez icin tum parcalar.
            check(hs[0].target == 1 && hs[0].planeOrigin == Vec3{5, 6, 7}, "o0 parca1");
            check(hs[1].target == 2 && hs[1].planeOrigin == Vec3{5, 6, 7}, "o0 parca2");
            check(hs[2].target == 4 && hs[2].planeOrigin == Vec3{5, 6, 7}, "o0 parca4");
            check(hs[3].target == 1 && hs[3].planeOrigin == Vec3{55, 6, 7}, "o1 parca1");
            check(hs[4].target == 2 && hs[4].planeOrigin == Vec3{55, 6, 7}, "o1 parca2");
            check(hs[5].target == 4 && hs[5].planeOrigin == Vec3{55, 6, 7}, "o1 parca4");
        }
    }

    // 4) MMW8 round-trip: Hole capi korunur (eski dosya uyumlu opsiyonel alan).
    {
        Mmw8Document doc;
        doc.cutModifiers = {m};
        std::stringstream ss;
        writeMmw8(ss, doc);
        Mmw8Document back;
        readMmw8(ss, back);
        check(back.cutModifiers.size() == 1, "MMW8: 1 modifier geri geldi");
        check(back.cutModifiers.size() == 1 &&
                  back.cutModifiers[0].kind == CutKind::Hole &&
                  near(back.cutModifiers[0].diameter, 22.0),
              "MMW8: Hole capi round-trip");
    }

    // 5) isBoltAxisValid.
    check(isBoltAxisValid({0, 0, 1}), "eksen gecerli");
    check(!isBoltAxisValid({0, 0, 0}), "sifir eksen gecersiz");

    // 6) Standart boyut katalogu + kalite.
    check(standardBoltDiameters().size() == 6, "6 standart cap");
    check(near(standardBoltHoleDiameter(20.0), 22.0), "M20 -> 22 delik");
    check(near(standardBoltHoleDiameter(30.0), 33.0), "M30 -> 33 delik");
    check(near(standardBoltHoleDiameter(9.0), 11.0), "katalog disi -> +2");
    check(isBoltGradeValid("8.8") && isBoltGradeValid("10.9") &&
              !isBoltGradeValid("5.6") && isBoltGradeValid(""),
          "kalite dogrulama");

    if (failures) {
        std::printf("%d FAIL\n", failures);
        return 1;
    }
    std::printf("ALL PASS\n");
    return 0;
}
