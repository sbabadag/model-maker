// COMPONENT REBIND (C4): kopyalanan component YENI KONUMDAKI ana/ara parcalara
// baglanir (Tekla davranisi). Kullanici istegi (2026-10-28): "component tek
// basina bile kopyalansa, kopyalandigi koordinatta ana ve ara objeler varsa
// kendisi otomatik baglantiyi kurmali".
#include "model_maker/component_rebind.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

// Kolon: Z boyunca (main). Kiris: X boyunca (secondary).
static RebindCandidate column(EntityId id, double x, double y) {
    return RebindCandidate{id, {x, y, 0.0}, {x, y, 3000.0}, 300.0, 300.0};
}
static RebindCandidate beam(EntityId id, double y, double z, double len) {
    return RebindCandidate{id, {0.0, y, z}, {len, y, z}, 200.0, 400.0};
}

int main() {
    std::printf("== A: tek birlesim — rol yonle secilir ==\n");
    {
        std::vector<RebindCandidate> c{column(11, 0, 0), beam(21, 0, 1200, 4000)};
        const RebindResult r = findRebindParts(c, Vec3{0, 0, 1200}, Vec3{0, 0, 1}, {Vec3{1, 0, 0}}, 1, 750.0);
        check(r.ok && r.main == 11, "A: main = kolon (Z ekseni)");
        check(r.secondary.size() == 1 && r.secondary[0] == 21, "A: secondary = kiris (X ekseni)");
    }

    std::printf("== B: ikinci birlesim — yon ipucu yakin-yanlis adayi ezmez ==\n");
    {
        // Ikinci kolon x=6000'de; capa orada. Kiris 1 ile ayni yonde ikinci kiris de var.
        std::vector<RebindCandidate> c{column(11, 0, 0),   beam(21, 0, 1200, 4000),
                                       column(12, 6000, 0), beam(22, 0, 1200, 9000)};
        const RebindResult r =
            findRebindParts(c, Vec3{6000, 0, 1200}, Vec3{0, 0, 1}, {Vec3{1, 0, 0}}, 1, 750.0);
        check(r.ok && r.main == 12, "B: main = IKINCI kolon (yeni konum)");
        check(r.secondary.size() == 1 && r.secondary[0] == 22, "B: secondary = IKINCI kiris (yeni konum)");
    }

    std::printf("== C: main ipucu verilmezse yalniz uzaklik ==\n");
    {
        // Kolon eksenden 2 m uzakta; capa kiris eksenine cok yakin.
        std::vector<RebindCandidate> c{column(11, 2000, 0), beam(21, 0, 1200, 4000)};
        const RebindResult r = findRebindParts(c, Vec3{0, 0, 1210}, Vec3{}, {}, 0, 750.0);
        check(r.ok && r.main == 21, "C: ipucu yok -> en yakin aday main");
        check(r.secondary.empty(), "C: secondary istenmedi -> bos");
    }

    std::printf("== D: capa cevresinde parca yok ==\n");
    {
        std::vector<RebindCandidate> c{column(11, 0, 0), beam(21, 0, 1200, 4000)};
        const RebindResult r =
            findRebindParts(c, Vec3{9000, 9000, 1200}, Vec3{0, 0, 1}, {Vec3{1, 0, 0}}, 1, 750.0);
        check(!r.ok, "D: uzak capa -> baglanti kurulmaz (ok=false)");
    }

    std::printf("== E: secondary eksik -> main bulunur, secondary bos ==\n");
    {
        std::vector<RebindCandidate> c{column(11, 0, 0)};
        const RebindResult r = findRebindParts(c, Vec3{0, 0, 900}, Vec3{0, 0, 1}, {Vec3{1, 0, 0}}, 1, 750.0);
        check(r.ok && r.main == 11, "E: main bulundu");
        check(r.secondary.empty(), "E: secondary yok (cagiran tam baglanti kurmaz)");
    }

    std::printf("== F: ayni aday iki role atanmaz ==\n");
    {
        std::vector<RebindCandidate> c{column(11, 0, 0)};
        const RebindResult r = findRebindParts(c, Vec3{0, 0, 900}, Vec3{0, 0, 1}, {}, 2, 750.0);
        check(r.ok && r.main == 11 && r.secondary.empty(), "F: tek aday yalniz main olur");
    }

    std::printf("== G: diklik kurali — paralel aday secondary olmaz ==\n");
    {
        std::vector<RebindCandidate> c{column(11, 0, 0), column(31, 400, 0)};
        const RebindResult r = findRebindParts(c, Vec3{0, 0, 600}, Vec3{0, 0, 1}, {}, 1, 750.0);
        check(r.ok && r.main == 11 && r.secondary.empty(),
              "G: paralel ikinci kolon secondary sayilmaz");
    }

    std::printf("== H: nokta-eksen uzakligi ==\n");
    {
        const double d1 = distancePointToAxis(Vec3{100, 0, 500}, Vec3{0, 0, 0}, Vec3{0, 0, 1000});
        check(std::fabs(d1 - 100.0) < 1e-9, "H: eksene dik uzaklik 100");
        const double d2 = distancePointToAxis(Vec3{0, 0, 1100}, Vec3{0, 0, 0}, Vec3{0, 0, 1000});
        check(std::fabs(d2 - 100.0) < 1e-9, "H: segment disi izdusum uca kelepcelenir");
        const double d3 = distancePointToAxis(Vec3{3, 4, 0}, Vec3{0, 0, 0}, Vec3{0, 0, 0});
        check(std::fabs(d3 - 5.0) < 1e-9, "H: sifir uzunluk -> nokta uzakligi");
    }

    if (failures) std::printf("%d FAIL\n", failures);
    else std::printf("ALL PASS\n");
    return failures == 0 ? 0 : 1;
}
