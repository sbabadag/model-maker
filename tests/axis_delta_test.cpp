// DOGRUSAL KOPYA / TASIMA sayisal oteleme cozumleyici testi.
// Kullanici akisi: secim + baz noktasi + hedef adiminda "dx,dy,dz" yazimi.
#include "model_maker/axis_delta.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

int main() {
    std::printf("== A: uc bilesenli oteleme ==\n");
    {
        const auto d = parseAxisDeltaText(L"1000,500,-250");
        check(d.has_value() && near(d->x, 1000.0) && near(d->y, 500.0) && near(d->z, -250.0),
              "A: dx,dy,dz okunur");
    }
    std::printf("== B: iki bilesen -> dz=0 ==\n");
    {
        const auto d = parseAxisDeltaText(L"1500,-300");
        check(d.has_value() && near(d->x, 1500.0) && near(d->y, -300.0) && near(d->z, 0.0),
              "B: dx,dy okunur, dz sifir");
    }
    std::printf("== C: tek sayi = imlec yonunde mesafe ==\n");
    {
        const auto d = parseAxisDeltaText(L"2000", Vec3{0, 0, 0}, Vec3{0, 5000, 0});
        check(d.has_value() && near(d->x, 0.0) && near(d->y, 2000.0) && near(d->z, 0.0),
              "C: +Y yonunde 2000");
        // Negatif mesafe yonu TERS cevirir: -Z yonunde -750 => +Z'ye 750.
        const auto dz = parseAxisDeltaText(L"-750", Vec3{0, 0, 0}, Vec3{0, 0, -3000});
        check(dz.has_value() && near(dz->z, 750.0) && near(dz->x, 0.0),
              "C: negatif mesafe yonu ters cevirir (-Z yonu, -750 => +750)");
    }
    std::printf("== D: yon yoksa +X ==\n");
    {
        const auto d = parseAxisDeltaText(L"600");
        check(d.has_value() && near(d->x, 600.0) && near(d->y, 0.0) && near(d->z, 0.0),
              "D: yon verilmezse +X");
        const auto same = parseAxisDeltaText(L"600", Vec3{10, 10, 10}, Vec3{10, 10, 10});
        check(same.has_value() && near(same->x, 600.0), "D: baz == imlec -> +X");
    }
    std::printf("== E: bosluk / noktali virgul ayirici ==\n");
    {
        const auto d = parseAxisDeltaText(L"100 200 300");
        check(d.has_value() && near(d->x, 100.0) && near(d->y, 200.0) && near(d->z, 300.0),
              "E: bosluk ayirici");
        const auto s = parseAxisDeltaText(L"100;200");
        check(s.has_value() && near(s->x, 100.0) && near(s->y, 200.0), "E: noktali virgul ayirici");
    }
    std::printf("== F: gecersiz girdi reddedilir ==\n");
    {
        check(!parseAxisDeltaText(L"").has_value(), "F: bos girdi -> nullopt");
        check(!parseAxisDeltaText(L"abc").has_value(), "F: harf -> nullopt");
        check(parseAxisDeltaText(L"100,").has_value(), "F: sondaki virgul tolere edilir");
        check(!parseAxisDeltaText(L"1,2,3,4").has_value(), "F: 4 bilesen -> nullopt");
        check(!parseAxisDeltaText(L"1e").has_value(), "F: bozuk sayi -> nullopt");
    }
    if (failures) std::printf("%d FAIL\n", failures);
    else std::printf("ALL PASS\n");
    return failures == 0 ? 0 : 1;
}
