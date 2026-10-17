// CONSTRAINT SYSTEM (Phase R2a) — deterministik position solver testleri.
//
// Duzlem kisitlari n·x = d + signedOffset. Rank-tabanli, lineer bagimsizlik +
// tutarlilik ile cozulur. Durumlar: Free/UnderConstrained/Solved/Conflict/
// Unresolved/Cyclic. OCC'siz, Linux'ta kosar.

#include "model_maker/constraint_system.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

static int g_failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++g_failures;
}
static bool near(double a, double b, double tol = 1e-9) { return std::abs(a - b) < tol; }

// Cozumlenmis (resolved) duzlem referansi.
static FaceReference makePlane(const Vec3& normal, const Vec3& origin) {
    FaceReference f;
    f.source = 1;  // gecerli EntityId
    f.resolved = true;
    f.plane.normal = normal;
    f.plane.origin = origin;
    return f;
}
static FaceReference makeUnresolved() {
    FaceReference f;
    f.source = kInvalidEntityId;  // eksik uye
    f.resolved = false;
    return f;
}
static DistanceConstraint cstr(const char* handle, FaceReference target, double offset = 0.0) {
    DistanceConstraint d;
    d.handle = HandleKey{handle};
    d.target = target;
    d.signedOffset = offset;
    return d;
}

int main() {
    // --- 1) 0 kisit: FREE -----------------------------------------------------
    {
        ConstraintSystem cs;
        cs.addHandle({"h"}, HandleKind::Point, {1.0, 2.0, 3.0});
        cs.solve();
        const auto r = cs.position({"h"});
        check(r.state == ConstraintState::Free, "0 kisit: FREE");
        check(r.dof == 3, "0 kisit: dof=3");
        check(near(r.position.x, 1.0) && near(r.position.y, 2.0) && near(r.position.z, 3.0),
              "0 kisit: konum korunur");
    }

    // --- 2) 1 kisit: UNDERCONSTRAINED, duzlem uzerinde serbest -----------------
    {
        ConstraintSystem cs;
        cs.addHandle({"h"}, HandleKind::Point, {10.0, 4.0, 5.0});
        cs.addDistanceConstraint(cstr("h", makePlane({1.0, 0.0, 0.0}, {0.0, 0.0, 0.0})));
        cs.solve();
        const auto r = cs.position({"h"});
        check(r.state == ConstraintState::UnderConstrained, "1 kisit: UNDERCONSTRAINED");
        check(r.dof == 2, "1 kisit: dof=2");
        check(near(r.position.x, 0.0), "1 kisit: x=0 duzlemine oturur");
        check(near(r.position.y, 4.0) && near(r.position.z, 5.0),
              "1 kisit: serbest yonlerde son konum korunur (en yakin cozum)");
        // Serbest yonler normale dik olmali.
        check(near(r.freeAxis[0].x, 0.0) && near(r.freeAxis[1].x, 0.0),
              "1 kisit: serbest yonler x-eksenine dik");
    }

    // --- 3) 2 dik duzlem: UNDERCONSTRAINED, kesisim dogrusu --------------------
    {
        ConstraintSystem cs;
        cs.addHandle({"h"}, HandleKind::Point, {5.0, 5.0, 7.0});
        cs.addDistanceConstraint(cstr("h", makePlane({1.0, 0.0, 0.0}, {0.0, 0.0, 0.0})));
        cs.addDistanceConstraint(cstr("h", makePlane({0.0, 1.0, 0.0}, {0.0, 0.0, 0.0})));
        cs.solve();
        const auto r = cs.position({"h"});
        check(r.state == ConstraintState::UnderConstrained, "2 dik duzlem: UNDERCONSTRAINED");
        check(r.dof == 1, "2 dik duzlem: dof=1");
        check(near(r.position.x, 0.0) && near(r.position.y, 0.0), "2 dik duzlem: x=y=0");
        check(near(r.position.z, 7.0), "2 dik duzlem: z serbest (son konum korunur)");
    }

    // --- 4) 2 paralel TUTARLI duzlem: UNDERCONSTRAINED --------------------------
    {
        ConstraintSystem cs;
        cs.addHandle({"h"}, HandleKind::Point, {3.0, 1.0, 2.0});
        cs.addDistanceConstraint(cstr("h", makePlane({1.0, 0.0, 0.0}, {0.0, 0.0, 0.0})));
        cs.addDistanceConstraint(cstr("h", makePlane({1.0, 0.0, 0.0}, {0.0, 0.0, 0.0})));
        cs.solve();
        const auto r = cs.position({"h"});
        check(r.state == ConstraintState::UnderConstrained, "2 paralel tutarli: UNDERCONSTRAINED");
        check(r.dof == 2, "2 paralel tutarli: dof=2");
        check(near(r.position.x, 0.0), "2 paralel tutarli: x=0");
    }

    // --- 5) 2 paralel CELISKILI duzlem: CONFLICT --------------------------------
    {
        ConstraintSystem cs;
        cs.addHandle({"h"}, HandleKind::Point, {0.5, 0.0, 0.0});
        cs.addDistanceConstraint(cstr("h", makePlane({1.0, 0.0, 0.0}, {0.0, 0.0, 0.0})));
        cs.addDistanceConstraint(cstr("h", makePlane({1.0, 0.0, 0.0}, {1.0, 0.0, 0.0})));
        cs.solve();
        const auto r = cs.position({"h"});
        check(r.state == ConstraintState::Conflict, "2 paralel celiskili: CONFLICT");
    }

    // --- 6) 3 bagimsiz duzlem: SOLVED ------------------------------------------
    {
        ConstraintSystem cs;
        cs.addHandle({"h"}, HandleKind::Point, {0.0, 0.0, 0.0});
        cs.addDistanceConstraint(cstr("h", makePlane({1.0, 0.0, 0.0}, {1.0, 0.0, 0.0})));
        cs.addDistanceConstraint(cstr("h", makePlane({0.0, 1.0, 0.0}, {0.0, 2.0, 0.0})));
        cs.addDistanceConstraint(cstr("h", makePlane({0.0, 0.0, 1.0}, {0.0, 0.0, 3.0})));
        cs.solve();
        const auto r = cs.position({"h"});
        check(r.state == ConstraintState::Solved, "3 bagimsiz duzlem: SOLVED");
        check(r.dof == 0, "3 bagimsiz duzlem: dof=0");
        check(near(r.position.x, 1.0) && near(r.position.y, 2.0) && near(r.position.z, 3.0),
              "3 bagimsiz duzlem: konum (1,2,3)");
    }

    // --- 7a) 3 BAGIMLI tutarli duzlem: UNDERCONSTRAINED -------------------------
    {
        ConstraintSystem cs;
        cs.addHandle({"h"}, HandleKind::Point, {0.0, 0.0, 9.0});
        cs.addDistanceConstraint(cstr("h", makePlane({1.0, 0.0, 0.0}, {0.0, 0.0, 0.0})));
        cs.addDistanceConstraint(cstr("h", makePlane({0.0, 1.0, 0.0}, {0.0, 0.0, 0.0})));
        cs.addDistanceConstraint(cstr("h", makePlane({1.0, 1.0, 0.0}, {0.0, 0.0, 0.0})));
        cs.solve();
        const auto r = cs.position({"h"});
        check(r.state == ConstraintState::UnderConstrained, "3 bagimli tutarli: UNDERCONSTRAINED");
        check(r.dof == 1, "3 bagimli tutarli: dof=1 (rank=2)");
    }

    // --- 7b) 3 BAGIMLI celiskili duzlem: CONFLICT -------------------------------
    {
        ConstraintSystem cs;
        cs.addHandle({"h"}, HandleKind::Point, {0.0, 0.0, 9.0});
        cs.addDistanceConstraint(cstr("h", makePlane({1.0, 0.0, 0.0}, {0.0, 0.0, 0.0})));
        cs.addDistanceConstraint(cstr("h", makePlane({0.0, 1.0, 0.0}, {0.0, 0.0, 0.0})));
        cs.addDistanceConstraint(cstr("h", makePlane({1.0, 1.0, 0.0}, {1.0, 0.0, 0.0})));  // x+y=1
        cs.solve();
        const auto r = cs.position({"h"});
        check(r.state == ConstraintState::Conflict, "3 bagimli celiskili: CONFLICT");
    }

    // --- 8) 4 TUTARLI duzlem: SOLVED (4+ otomatik conflict DEGIL) ---------------
    {
        ConstraintSystem cs;
        cs.addHandle({"h"}, HandleKind::Point, {0.0, 0.0, 0.0});
        cs.addDistanceConstraint(cstr("h", makePlane({1.0, 0.0, 0.0}, {0.0, 0.0, 0.0})));
        cs.addDistanceConstraint(cstr("h", makePlane({0.0, 1.0, 0.0}, {0.0, 0.0, 0.0})));
        cs.addDistanceConstraint(cstr("h", makePlane({0.0, 0.0, 1.0}, {0.0, 0.0, 0.0})));
        cs.addDistanceConstraint(cstr("h", makePlane({1.0, 1.0, 1.0}, {0.0, 0.0, 0.0})));
        cs.solve();
        const auto r = cs.position({"h"});
        check(r.state == ConstraintState::Solved, "4 tutarli duzlem: SOLVED");
        check(r.dof == 0, "4 tutarli duzlem: dof=0");
    }

    // --- 9) 4 CELISKILI duzlem: CONFLICT ---------------------------------------
    {
        ConstraintSystem cs;
        cs.addHandle({"h"}, HandleKind::Point, {0.0, 0.0, 0.0});
        cs.addDistanceConstraint(cstr("h", makePlane({1.0, 0.0, 0.0}, {0.0, 0.0, 0.0})));
        cs.addDistanceConstraint(cstr("h", makePlane({0.0, 1.0, 0.0}, {0.0, 0.0, 0.0})));
        cs.addDistanceConstraint(cstr("h", makePlane({0.0, 0.0, 1.0}, {0.0, 0.0, 0.0})));
        cs.addDistanceConstraint(cstr("h", makePlane({1.0, 1.0, 1.0}, {1.0, 0.0, 0.0})));
        cs.solve();
        const auto r = cs.position({"h"});
        check(r.state == ConstraintState::Conflict, "4 celiskili duzlem: CONFLICT");
    }

    // --- 10) Signed offset + ters normal isareti --------------------------------
    {
        ConstraintSystem a;
        a.addHandle({"h"}, HandleKind::Point, {0.0, 0.0, 0.0});
        a.addDistanceConstraint(cstr("h", makePlane({1.0, 0.0, 0.0}, {0.0, 0.0, 0.0}), 5.0));
        a.solve();
        check(near(a.position({"h"}).position.x, 5.0), "signedOffset +5 -> x=5");

        ConstraintSystem b;
        b.addHandle({"h"}, HandleKind::Point, {0.0, 0.0, 0.0});
        b.addDistanceConstraint(cstr("h", makePlane({-1.0, 0.0, 0.0}, {0.0, 0.0, 0.0}), -5.0));
        b.solve();
        check(near(b.position({"h"}).position.x, 5.0), "ters normal -n + offset -5 -> x=5 (ayni)");

        // Ikisi birlikte: tutarli (celismez).
        ConstraintSystem c;
        c.addHandle({"h"}, HandleKind::Point, {0.0, 0.0, 0.0});
        c.addDistanceConstraint(cstr("h", makePlane({1.0, 0.0, 0.0}, {0.0, 0.0, 0.0}), 5.0));
        c.addDistanceConstraint(cstr("h", makePlane({-1.0, 0.0, 0.0}, {0.0, 0.0, 0.0}), -5.0));
        c.solve();
        check(c.position({"h"}).state == ConstraintState::UnderConstrained,
              "ters normal cifti: tutarli (CONFLICT degil)");
        check(near(c.position({"h"}).position.x, 5.0), "ters normal cifti: x=5");
    }

    // --- 11) Serbestlik kalan durumda onceki konuma en yakin cozum --------------
    {
        ConstraintSystem cs;
        cs.addHandle({"h"}, HandleKind::Point, {10.0, 3.0, 4.0});
        cs.addDistanceConstraint(cstr("h", makePlane({1.0, 0.0, 0.0}, {0.0, 0.0, 0.0})));
        cs.solve();
        const auto r = cs.position({"h"});
        check(near(r.position.x, 0.0) && near(r.position.y, 3.0) && near(r.position.z, 4.0),
              "onceki konuma en yakin: (0,3,4)");
    }

    // --- 12) Dongusel referans: CYCLIC ------------------------------------------
    {
        ConstraintSystem cs;
        cs.addHandle({"a"}, HandleKind::Point, {0.0, 0.0, 0.0});
        cs.addHandle({"b"}, HandleKind::Point, {1.0, 1.0, 1.0});
        cs.addDependency({"a"}, {"b"});
        cs.addDependency({"b"}, {"a"});
        const bool clean = cs.solve();
        check(!clean, "dongu tespit edildi (solve() = false)");
        check(cs.position({"a"}).state == ConstraintState::Cyclic, "dongudeki a: CYCLIC");
        check(cs.position({"b"}).state == ConstraintState::Cyclic, "dongudeki b: CYCLIC");
    }

    // --- 13) Eksik EntityId: UNRESOLVED -----------------------------------------
    {
        ConstraintSystem cs;
        cs.addHandle({"h"}, HandleKind::Point, {0.0, 0.0, 0.0});
        cs.addDistanceConstraint(cstr("h", makeUnresolved()));
        cs.solve();
        check(cs.position({"h"}).state == ConstraintState::Unresolved,
              "eksik EntityId: UNRESOLVED");
    }

    // --- 14) Sifira yakin normal: gecersiz giris -> UNRESOLVED -------------------
    {
        ConstraintSystem cs;
        cs.addHandle({"h"}, HandleKind::Point, {0.0, 0.0, 0.0});
        cs.addDistanceConstraint(cstr("h", makePlane({1e-15, 0.0, 0.0}, {0.0, 0.0, 0.0})));
        cs.solve();
        check(cs.position({"h"}).state == ConstraintState::Unresolved,
              "sifira yakin normal: UNRESOLVED (gecersiz giris)");
    }

    // --- 15) Buyuk koordinatlar + kucuk offset: sayisal kararlilik --------------
    {
        const double L = 1e9;
        ConstraintSystem cs;
        cs.addHandle({"h"}, HandleKind::Point, {L, L, L});
        cs.addDistanceConstraint(cstr("h", makePlane({1.0, 0.0, 0.0}, {L + 1e-3, 0.0, 0.0})));
        cs.addDistanceConstraint(cstr("h", makePlane({0.0, 1.0, 0.0}, {0.0, L, 0.0})));
        cs.addDistanceConstraint(cstr("h", makePlane({0.0, 0.0, 1.0}, {0.0, 0.0, L})));
        cs.solve();
        const auto r = cs.position({"h"});
        check(r.state == ConstraintState::Solved, "buyuk koordinat: SOLVED");
        check(std::isfinite(r.position.x) && std::isfinite(r.position.y) &&
                  std::isfinite(r.position.z),
              "buyuk koordinat: NaN/Inf yok");
        check(near(r.position.x, L + 1e-3, 1e-4), "buyuk koordinat: kucuk offset korunur");
        check(near(r.position.y, L, 1e-4) && near(r.position.z, L, 1e-4),
              "buyuk koordinat: y/z dogru");
    }

    // --- 16) 30° dondurulmus referans duzlemi -----------------------------------
    {
        const double rad = 30.0 * 3.14159265358979323846 / 180.0;
        const Vec3 n{std::cos(rad), std::sin(rad), 0.0};
        ConstraintSystem cs;
        cs.addHandle({"h"}, HandleKind::Point, {1.0, 0.0, 0.0});
        cs.addDistanceConstraint(cstr("h", makePlane(n, {0.0, 0.0, 0.0})));
        cs.solve();
        const auto r = cs.position({"h"});
        check(r.state == ConstraintState::UnderConstrained, "30° duzlem: UNDERCONSTRAINED");
        const double d = r.position.x * n.x + r.position.y * n.y + r.position.z * n.z;
        check(near(d, 0.0, 1e-9), "30° duzlem: konum duzlem uzerinde (n·x=0)");
    }

    // --- 17) Reviewer bulgusu: cakisan donguler (Tarjan SCC) ----------------------
    {
        ConstraintSystem cs;
        cs.addHandle({"a"}, HandleKind::Point, {0, 0, 0});
        cs.addHandle({"b"}, HandleKind::Point, {0, 0, 0});
        cs.addHandle({"c"}, HandleKind::Point, {0, 0, 0});
        cs.addHandle({"d"}, HandleKind::Point, {0, 0, 0});
        cs.addDependency({"a"}, {"b"});
        cs.addDependency({"a"}, {"c"});
        cs.addDependency({"b"}, {"d"});
        cs.addDependency({"c"}, {"d"});
        cs.addDependency({"d"}, {"a"});
        cs.solve();
        check(cs.position({"a"}).state == ConstraintState::Cyclic &&
                  cs.position({"b"}).state == ConstraintState::Cyclic &&
                  cs.position({"c"}).state == ConstraintState::Cyclic &&
                  cs.position({"d"}).state == ConstraintState::Cyclic,
              "cakisan dongu: a,b,c,d TUMU CYCLIC (c atlanmaz)");
    }

    // --- 18) Reviewer bulgusu: buyuk koordinatta 1mm celiski -> CONFLICT ----------
    {
        ConstraintSystem cs;
        cs.addHandle({"h"}, HandleKind::Point, {0, 0, 0});
        cs.addDistanceConstraint(cstr("h", makePlane({1, 0, 0}, {1e9, 0, 0})));
        cs.addDistanceConstraint(cstr("h", makePlane({1, 0, 0}, {1e9 + 1, 0, 0})));
        cs.solve();
        check(cs.position({"h"}).state == ConstraintState::Conflict,
              "buyuk koordinat 1mm celiski: CONFLICT (bagil esik maskelenmez)");
    }

    // --- 19) Reviewer bulgusu: neredeyse paralel + buyuk son konum -----------------
    {
        ConstraintSystem cs;
        cs.addHandle({"h"}, HandleKind::Point, {0, 1e12, 0});
        cs.addDistanceConstraint(cstr("h", makePlane({1, 0, 0}, {0, 0, 0})));
        cs.addDistanceConstraint(cstr("h", makePlane({1, 1e-11, 0}, {0, 0, 0})));
        cs.solve();
        const auto r = cs.position({"h"});
        check(r.state == ConstraintState::UnderConstrained, "neredeyse paralel: UNDERCONSTRAINED");
        const double e1 = r.position.x;                     // n1·x = x
        const double e2 = r.position.x + 1e-11 * r.position.y;  // n2·x
        check(near(e1, 0.0, 1e-6) && near(e2, 0.0, 1e-6),
              "neredeyse paralel: konum her iki denklemi saglar (buyuk artik yok)");
    }

    // --- 20) Reviewer bulgusu: non-finite giris -> UNRESOLVED ----------------------
    {
        ConstraintSystem cs;
        cs.addHandle({"h"}, HandleKind::Point, {0, 0, 0});
        DistanceConstraint d = cstr("h", makePlane({1, 0, 0}, {0, 0, 0}));
        d.signedOffset = std::nan("");
        cs.addDistanceConstraint(d);
        cs.solve();
        const auto r = cs.position({"h"});
        check(r.state == ConstraintState::Unresolved, "NaN offset: UNRESOLVED");
        check(std::isfinite(r.position.x) && std::isfinite(r.position.y) &&
                  std::isfinite(r.position.z),
              "NaN offset: NaN konum uretilmez");
    }

    // --- 21) Reviewer bulgusu: mukerrer anahtar tek kayit --------------------------
    {
        ConstraintSystem cs;
        cs.addHandle({"h"}, HandleKind::Point, {1, 2, 3});
        cs.addHandle({"h"}, HandleKind::Point, {9, 9, 9});  // ayni anahtar
        check(cs.handleCount() == 1, "mukerrer anahtar: tek kayit");
        cs.solve();
        check(near(cs.position({"h"}).position.x, 9.0), "mukerrer anahtar: son konum guncellenir");
    }

    std::printf("FAILURES: %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
