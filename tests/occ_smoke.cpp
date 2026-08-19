#ifdef MM_HAS_OCC
#include "model_maker/occ_geometry.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>

int main() {
    // 1) Kati kutu + hacim
    const double volume = mm::boxVolume(10.0, 10.0, 10.0);
    if (std::abs(volume - 1000.0) > 1e-6) {
        std::printf("HATA: hacim %f (beklenen 1000.0)\n", volume);
        return 1;
    }

    // 2) STEP yaz + geri oku — veri alisverisi dongusu kapali
    const auto step = std::filesystem::temp_directory_path() / "mm_occ_smoke.step";
    if (!mm::writeBoxStep(step, 10.0, 10.0, 10.0)) {
        std::printf("HATA: STEP dosyasi yazilamadi\n");
        return 1;
    }
    const double readBack = mm::readStepVolume(step);
    std::filesystem::remove(step);
    if (std::abs(readBack - 1000.0) > 1e-3) {
        std::printf("HATA: STEP'ten okunan hacim %f (beklenen 1000.0)\n", readBack);
        return 1;
    }

    std::printf("OCC SMOKE OK — kutu 10x10x10 hacim=%.6f, STEP yazildi/okundu hacim=%.6f\n",
                volume, readBack);
    return 0;
}
#else
#include <cstdio>
int main() {
    std::printf("OCC bulunamadi — test atlandi\n");
    return 77;
}
#endif
