// SNAP AYARLARI (UI-6a): tasinabilir serilestirme + varsayilan degerler.
// Qt'siz / windows.h'siz / OCC'siz; Linux'ta dogrudan kosar.
#include "model_maker/snap_settings.hpp"

#include <cmath>
#include <cstdio>
#include <string>

static int failures = 0;
static int checks = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        ++checks;                                                              \
        if (!(cond)) {                                                         \
            ++failures;                                                        \
            std::printf("FAIL: %s\n", msg);                                    \
        }                                                                      \
    } while (0)

int main() {
    using mm::SnapSettings;

    // Varsayilan degerler: eski 2x -> 1.5 kat = 3; kalem 2 -> 3; 300 ms; 1.5x.
    {
        SnapSettings d;
        CHECK(d.markerScale == 3, "markerScale varsayilani 3 olmali");
        CHECK(d.markerPenWidth == 3, "markerPenWidth varsayilani 3 olmali");
        CHECK(d.holdTimeMs == 300.0, "holdTimeMs varsayilani 300 ms olmali");
        CHECK(d.apertureScale == 1.5, "apertureScale varsayilani 1.5 olmali");
    }

    // Yakalama toleransi tabani (eski 10 px) korunuyor.
    CHECK(mm::kSnapApertureBasePx == 10.0, "kSnapApertureBasePx = 10.0 olmali");

    // Serilestirme -> parse gidis-donus.
    {
        SnapSettings s{5, 4, 450.0, 2.0};
        const std::string text = mm::serializeSnapSettings(s);
        const auto parsed = mm::parseSnapSettings(text);
        CHECK(parsed.has_value(), "round-trip parse basarili olmali");
        if (parsed) {
            CHECK(parsed->markerScale == 5, "round-trip markerScale");
            CHECK(parsed->markerPenWidth == 4, "round-trip markerPenWidth");
            CHECK(std::fabs(parsed->holdTimeMs - 450.0) < 1e-9, "round-trip holdTimeMs");
            CHECK(std::fabs(parsed->apertureScale - 2.0) < 1e-9, "round-trip apertureScale");
        }
    }

    // Varsayilan degerlerin serilestirmesi de ayni doner.
    {
        const auto parsed = mm::parseSnapSettings(mm::serializeSnapSettings(SnapSettings{}));
        CHECK(parsed.has_value(), "varsayilan round-trip basarili olmali");
        if (parsed) {
            CHECK(parsed->markerScale == 3 && parsed->markerPenWidth == 3 &&
                      parsed->holdTimeMs == 300.0 && parsed->apertureScale == 1.5,
                  "varsayilan round-trip degerleri esit olmali");
        }
    }

    // Gecersiz / bilinmeyen girdi.
    {
        CHECK(!mm::parseSnapSettings("").has_value(), "bos metin -> nullopt");
        CHECK(!mm::parseSnapSettings("foo bar\nbaz").has_value(), "anahtarsiz metin -> nullopt");
        const auto p = mm::parseSnapSettings("markerScale=abc\nmarkerPenWidth=2\nholdTimeMs=300\napertureScale=1.5");
        CHECK(p.has_value() && p->markerPenWidth == 2, "bilinmeyen degerli alan varsayilana dusmeli");
    }

    // Bilinmeyen anahtar yok sayilir, bilinen alanlar korunur (ileri uyumluluk).
    {
        const auto p = mm::parseSnapSettings("markerScale=7\nfutureKey=42\nholdTimeMs=150\n");
        CHECK(p.has_value() && p->markerScale == 7 && p->holdTimeMs == 150.0,
              "bilinmeyen anahtar yok sayilmali");
    }

    // clamp: sinir disi degerler guvenli araliga cekilir.
    {
        SnapSettings s{-5, 999, -1.0, 0.1};
        s.clamp();
        CHECK(s.markerScale == 1, "markerScale alt sinir 1");
        CHECK(s.markerPenWidth == 8, "markerPenWidth ust sinir 8");
        CHECK(s.holdTimeMs == 0.0, "negatif holdTimeMs 0'a cekilmeli");
        CHECK(s.apertureScale == 0.5, "apertureScale alt sinir 0.5");
    }

    if (failures == 0) {
        std::printf("snap_settings_test: %d kontrol GECTI\n", checks);
        return 0;
    }
    std::printf("snap_settings_test: %d/%d kontrol HATALI\n", failures, checks);
    return 1;
}
