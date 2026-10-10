// Dikme (perpendicular) izleme cekirdek testi — OCC GEREKTIRMEZ.
//
// Kullanici kurali: "polar tracking'de iki nokta secildiginde ona DIK NOKTA
// gorunmeli ve ona snap yapabilmeliyim" — iki izleme noktasinin tanimladigi
// dogruya imlecin dikme ayagi hem ekranda gorunur (perpendicularPoints) hem
// SnapType::Perpendicular olarak gercek bir snap noktasi olur.
//
// Bu dosya GERCEK resolveTemporaryPointTracking() kodunu cagirir (kopya
// mantik yok); model_maker_core'a baglanir.
#include "model_maker/drafting.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

static int checks = 0, failures = 0;
static void check(bool ok, const char* what) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }
static bool nearV(const Vec3& a, const Vec3& b, double eps = 1e-9) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}
static double dotOf(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

static const WorkPlane kWorld = WorkPlane::world();

int main() {
    const Vec3 a{0.0, 0.0, 0.0};
    const Vec3 b{1000.0, 0.0, 0.0};

    // 1) Iki nokta + imlec dogrunun yaninda: dikme ayagi (400,0,0) ve KILITLI.
    {
        SnapResult cursor{Vec3{400.0, 120.0, 0.0}, SnapType::None, 0.0};
        const auto t = resolveTemporaryPointTracking(cursor, {a, b}, kWorld, 200.0);
        check(t.locked, "iki nokta + yakin imlec: KILITLI");
        check(t.result.type == SnapType::Perpendicular, "snap tipi Perpendicular");
        check(nearV(t.result.point, {400.0, 0.0, 0.0}, 1e-9), "dikme ayagi (400,0,0)");
        check(t.perpendicularPoints.size() == 1 &&
                  nearV(t.perpendicularPoints.front(), {400.0, 0.0, 0.0}, 1e-9),
              "dikme ayagi gorunur (perpendicularPoints)");
    }

    // 2) Gercek DIKME geometrisi: ayak dogru uzerinde ve (imlec - ayak)
    //    vektoru dogru dogrultmanina DIK (nokta carpim = 0).
    {
        SnapResult cursor{Vec3{400.0, 120.0, 0.0}, SnapType::None, 0.0};
        const auto t = resolveTemporaryPointTracking(cursor, {a, b}, kWorld, 200.0);
        const Vec3 axis{1.0, 0.0, 0.0};
        const Vec3 onLine = sub(t.result.point, a);   // ayak A'dan itibaren
        check(near(onLine.y, 0.0, 1e-9) && near(onLine.z, 0.0, 1e-9),
              "ayak dogru uzerinde (dogrultmana dik sapma yok)");
        check(near(dotOf(sub(cursor.point, t.result.point), onLine), 0.0, 1e-9),
              "DIK ACI: (imlec - ayak) . (ayak - A) = 0");
        check(near(dotOf(onLine, axis), 400.0, 1e-9), "ayak dogru boyunca 400 mm");
    }

    // 3) TEK izleme noktasi: dikme yok (iki nokta gerekir).
    {
        SnapResult cursor{Vec3{400.0, 120.0, 0.0}, SnapType::None, 0.0};
        const auto t = resolveTemporaryPointTracking(cursor, {a}, kWorld, 200.0);
        check(t.perpendicularPoints.empty(), "tek nokta: dikme noktasi GORUNMEZ");
        check(!t.locked || t.result.type != SnapType::Perpendicular,
              "tek nokta: dikme snap'i YOK");
    }

    // 4) Uzak imlec (> 4 x tolerans): ne gorunur ne kilitlenir — ekran kirlenmez.
    {
        SnapResult cursor{Vec3{400.0, 5000.0, 0.0}, SnapType::None, 0.0};
        const auto t = resolveTemporaryPointTracking(cursor, {a, b}, kWorld, 200.0);
        check(t.perpendicularPoints.empty(), "cok uzak imlec: dikme GORUNMEZ");
        check(!t.locked, "cok uzak imlec: kilit YOK");
    }

    // 5) Snap yaricapi ile gorunurluk yaricapi ayri: tolerans 200 -> gorunurluk
    //    800. 400 uzaktaki ayak gorunur AMA kilitlenmez (henuz yaklasmadi).
    {
        SnapResult cursor{Vec3{400.0, 400.0, 0.0}, SnapType::None, 0.0};
        const auto t = resolveTemporaryPointTracking(cursor, {a, b}, kWorld, 200.0);
        check(t.perpendicularPoints.size() == 1, "orta mesafe: dikme GORUNUR");
        check(!t.locked, "orta mesafe: henuz KILITLI DEGIL");
    }

    // 6) Sonsuz dogru: ayak parcanin DISINA da dusebilir (yapim geometrisi).
    {
        SnapResult cursor{Vec3{1400.0, 60.0, 0.0}, SnapType::None, 0.0};
        const auto t = resolveTemporaryPointTracking(cursor, {a, b}, kWorld, 200.0);
        check(t.locked && t.result.type == SnapType::Perpendicular,
              "parca disindaki ayak da snap noktasi");
        check(near(t.result.point.x, 1400.0, 1e-9) && near(t.result.point.y, 0.0, 1e-9),
              "parca disi ayak (1400,0,0)");
    }

    // 7) UC nokta: en yakin cift kazanir (imlec 2. dogruya yakin).
    {
        // (0,0) - (1000,0) yatay; (1000,0) - (1000,1000) dusey.
        SnapResult cursor{Vec3{1060.0, 600.0, 0.0}, SnapType::None, 0.0};
        const auto t = resolveTemporaryPointTracking(cursor, {a, b, Vec3{1000.0, 1000.0, 0.0}},
                                                     kWorld, 200.0);
        check(t.locked && t.result.type == SnapType::Perpendicular,
              "uc nokta: dikme kilitlendi");
        check(nearV(t.result.point, {1000.0, 600.0, 0.0}, 1e-9),
              "uc nokta: en yakin ciftin ayagi (1000,600,0)");
        check(t.perpendicularPoints.size() == 1, "tek dikme noktasi gosterilir (en yakin)");
    }

    // 8) Farkli kotlu iki nokta: planDA dogru tanimlar, dikme yine calisir.
    {
        const Vec3 yuksek{1000.0, 0.0, 500.0};
        SnapResult cursor{Vec3{400.0, 80.0, 0.0}, SnapType::None, 0.0};
        const auto t = resolveTemporaryPointTracking(cursor, {a, yuksek}, kWorld, 200.0);
        check(t.locked && near(t.result.point.x, 400.0, 1e-9) &&
                  near(t.result.point.y, 0.0, 1e-9),
              "farkli kotlu noktalar: plan dikmesi (400,0)");
    }

    // 9) Donmus is duzlemi (YZ): dikme o duzlemde hesaplanir.
    {
        WorkPlane yz{};
        yz.origin = Vec3{0.0, 0.0, 0.0};
        yz.u = Vec3{0.0, 1.0, 0.0};
        yz.v = Vec3{0.0, 0.0, 1.0};
        yz.normal = Vec3{1.0, 0.0, 0.0};
        const Vec3 p1{0.0, 0.0, 0.0};
        const Vec3 p2{0.0, 0.0, 1000.0}; // duzlemde +v boyunca
        SnapResult cursor{Vec3{150.0, 0.0, 400.0}, SnapType::None, 0.0};
        const auto t = resolveTemporaryPointTracking(cursor, {p1, p2}, yz, 400.0);
        check(t.locked && t.result.type == SnapType::Perpendicular,
              "YZ duzlemi: dikme kilitlendi");
        check(nearV(t.result.point, {150.0, 0.0, 400.0}, 1e-9),
              "YZ duzlemi: ayak (150,0,400) — normal boyunca korunur");
    }

    // 10) Regresyon: orta nokta snap'i bozulmadi ve esitlikte ORTA NOKTA kazanir.
    {
        SnapResult cursor{Vec3{500.0, 0.0, 0.0}, SnapType::None, 0.0};
        const auto t = resolveTemporaryPointTracking(cursor, {a, b}, kWorld, 200.0);
        check(t.locked && t.result.type == SnapType::Midpoint, "orta nokta snap'i korunur");
        check(nearV(t.result.point, {500.0, 0.0, 0.0}, 1e-9), "orta nokta (500,0,0)");
    }

    // 11) Regresyon: acik nesne snap'i varken dikme kilidi devralmaz.
    {
        SnapResult cursor{Vec3{400.0, 120.0, 0.0}, SnapType::Endpoint, 0.0};
        const auto t = resolveTemporaryPointTracking(cursor, {a, b}, kWorld, 200.0);
        check(t.result.type == SnapType::Endpoint, "acik nesne snap'i kazanir (Endpoint)");
        check(t.perpendicularPoints.size() == 1, "dikme yine de gosterilir (bilgi)");
    }

    // 12) Regresyon: izleme noktasi yoksa cikti aday aynen doner.
    {
        SnapResult cursor{Vec3{400.0, 120.0, 0.0}, SnapType::None, 0.0};
        const auto t = resolveTemporaryPointTracking(cursor, {}, kWorld, 200.0);
        check(!t.locked && nearV(t.result.point, cursor.point, 1e-12),
              "izleme noktasi yok: aday degismez");
        check(t.perpendicularPoints.empty() && t.derivedPoints.empty(),
              "izleme noktasi yok: turetilmis nokta yok");
    }

    // 13) DIK BIRLESIM KOSESI (ayni kot): iki TP'nin izleme cizgileri planDA
    //     (P1.u,P2.v) ve (P2.u,P1.v) noktalarinda kesisir; imlec kosedeyse
    //     Intersection snap'i + iki kose isareti (derivedPoints).
    {
        const Vec3 p1{0.0, 0.0, 0.0};
        const Vec3 p2{1000.0, 500.0, 0.0};
        SnapResult cursor{Vec3{1000.0, 0.0, 0.0}, SnapType::None, 0.0};
        const auto t = resolveTemporaryPointTracking(cursor, {p1, p2}, kWorld, 200.0);
        check(t.locked && t.result.type == SnapType::Intersection,
              "ayni kot: dik birlesim kosesi Intersection snap'i");
        check(nearV(t.result.point, {1000.0, 0.0, 0.0}, 1e-9), "ayni kot: kose (1000,0,0)");
        check(t.derivedPoints.size() == 2, "ayni kot: iki kose isareti gorunur");
    }

    // 14) DIK BIRLESIM KOSESI (FARKLI kot): 3B cercevede uyeler farkli
    //     yukseklikte olabilir; plan kosesi yine gecerli ve isaretlenmeli.
    {
        const Vec3 p1{0.0, 0.0, 0.0};
        const Vec3 p2{1000.0, 500.0, 800.0};  // ikinci TP 800 mm yukarida
        SnapResult cursor{Vec3{1000.0, 0.0, 0.0}, SnapType::None, 0.0};
        const auto t = resolveTemporaryPointTracking(cursor, {p1, p2}, kWorld, 200.0);
        check(t.locked && t.result.type == SnapType::Intersection,
              "farkli kot: dik birlesim kosesi yine Intersection");
        check(near(t.result.point.x, 1000.0, 1e-9) && near(t.result.point.y, 0.0, 1e-9),
              "farkli kot: plan kosesi (1000,0)");
        check(t.derivedPoints.size() == 2, "farkli kot: iki kose isareti gorunur");
    }

    // 15) Kose imlec duzleminde (cursorNormal) konur: farkli kotta da snap
    //     mesafesi plan uzakligidir; kose isaretleri hem (P1.u,P2.v) hem
    //     (P2.u,P1.v) icerir.
    {
        const Vec3 p1{0.0, 0.0, 0.0};
        const Vec3 p2{1000.0, 500.0, 800.0};
        SnapResult cursor{Vec3{0.0, 500.0, 0.0}, SnapType::None, 0.0};
        const auto t = resolveTemporaryPointTracking(cursor, {p1, p2}, kWorld, 200.0);
        check(t.locked && t.result.type == SnapType::Intersection,
              "diger kose de snap noktasi (0,500)");
        bool hasA = false, hasB = false;
        for (const auto& d : t.derivedPoints) {
            if (nearV(d, {0.0, 500.0, 0.0}, 1e-9)) hasA = true;
            if (nearV(d, {1000.0, 0.0, 0.0}, 1e-9)) hasB = true;
        }
        check(hasA && hasB, "her iki kose isareti de listede");
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
