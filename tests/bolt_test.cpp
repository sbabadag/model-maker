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

    // 7) boltGroupCenteredOrigin: bulon grubunu yuzey kutusuna ortala (x/y).
    {
        // Tek bulon (1x1): grup merkezi = origin.
        BoltGroup s;
        s.origin = {10, 20, 5};
        s.axis = {0, 0, 1};
        s.xDir = {1, 0, 0};
        s.countX = 1; s.countY = 1; s.spacingX = 0; s.spacingY = 0;
        const Vec3 center{50, 60, 5};
        const Vec3 ox = boltGroupCenteredOrigin(s, center, true, false);
        check(ox == Vec3{50, 20, 5}, "ortala X: x=50, y/z degismez");
        const Vec3 oy = boltGroupCenteredOrigin(s, center, false, true);
        check(oy == Vec3{10, 60, 5}, "ortala Y: y=60, x/z degismez");
        const Vec3 oxy = boltGroupCenteredOrigin(s, center, true, true);
        check(oxy == Vec3{50, 60, 5}, "ortala X+Y: (50,60,5)");

        // Grup 2x2: grup merkezi = origin + (cx,cy) = origin + (20,15).
        BoltGroup g;
        g.origin = {10, 20, 5};
        g.axis = {0, 0, 1};
        g.xDir = {1, 0, 0};
        g.countX = 2; g.countY = 2; g.spacingX = 40.0; g.spacingY = 30.0;
        const Vec3 gx = boltGroupCenteredOrigin(g, center, true, false);
        check(gx == Vec3{30, 20, 5}, "2x2 ortala X: origin.x=30 (grup merkezi 50)");
        const Vec3 gy = boltGroupCenteredOrigin(g, center, false, true);
        check(gy == Vec3{10, 45, 5}, "2x2 ortala Y: origin.y=45 (grup merkezi 60)");
        const Vec3 gxy = boltGroupCenteredOrigin(g, center, true, true);
        check(gxy == Vec3{30, 45, 5}, "2x2 ortala X+Y: (30,45,5)");
        // Grup merkezini dogrulayalim: 2x2 origin (30,45) -> merkezler (30,45),
        // (70,45), (30,75), (70,75); ortalama (50,60) = hedef.
        const auto cents = boltGroupBoltOrigins([&] {
            BoltGroup c = g; c.origin = gxy; return c;
        }());
        if (cents.size() == 4) {
            const Vec3 avg = (cents[0] + cents[1] + cents[2] + cents[3]) * 0.25;
            check(avg == center, "2x2 grup merkezi hedef kutu merkezine esit");
        }
    }

    // 8) raycastMesh: yuz isabeti + normal (bulon yuz secimi icin).
    {
        const WireframeModel tri({Vec3{0, 0, 0}, Vec3{10, 0, 0}, Vec3{0, 10, 0}}, {},
                                 {Face{0, 1, 2}});
        // Ustten gelen isin (sarim +Z) -> normal +Z.
        const auto h1 = raycastMesh(tri, {5, 5, 10}, {0, 0, -1});
        check(h1.has_value(), "ray: ustten isabet var");
        if (h1) {
            check(near(h1->normal.z, 1.0), "ray: normal +Z");
            check(near(h1->point.z, 0.0), "ray: isabet z=0");
            check(near(h1->distance, 10.0), "ray: mesafe 10");
        }
        // Ters yon (asagi) -> isabet yok.
        check(!raycastMesh(tri, {5, 5, 10}, {0, 0, 1}).has_value(), "ray: ters yon isabetsiz");
        // Paralel isin -> isabet yok.
        check(!raycastMesh(tri, {5, 5, 10}, {1, 0, 0}).has_value(), "ray: paralel isabetsiz");

        // Iki ucgen farkli derinlikte -> en yakin olan secilir.
        const WireframeModel two(
            {Vec3{0, 0, 0}, Vec3{10, 0, 0}, Vec3{0, 10, 0}, Vec3{0, 0, 5},
             Vec3{10, 0, 5}, Vec3{0, 10, 5}},
            {}, {Face{0, 1, 2}, Face{3, 4, 5}});
        const auto h2 = raycastMesh(two, {5, 5, 10}, {0, 0, -1});
        check(h2.has_value() && near(h2->distance, 5.0), "ray: en yakin ucgen (z=5)");
        if (h2) check(near(h2->point.z, 5.0), "ray: en yakin nokta z=5");
    }

    if (failures) {
        std::printf("%d FAIL\n", failures);
        return 1;
    }
    std::printf("ALL PASS\n");
    return 0;
}
