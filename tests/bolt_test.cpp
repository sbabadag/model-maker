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

    if (failures) {
        std::printf("%d FAIL\n", failures);
        return 1;
    }
    std::printf("ALL PASS\n");
    return 0;
}
