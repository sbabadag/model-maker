#ifdef MM_HAS_OCC
#include "model_maker/occ_bridge_api.h"
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

    // 3) Tesselasyon: kutu = 8 kose / 12 kenar; silindir = 2 daire x N + 2 dik
    const auto box = mm::solidBoxWireframe(2.0, 3.0, 4.0);
    if (box.vertices().size() != 8 || box.edges().size() != 12) {
        std::printf("HATA: kutu tesselasyonu %zu kose / %zu kenar (beklenen 8/12)\n",
                    box.vertices().size(), box.edges().size());
        return 1;
    }
    const int seg = 32;
    const auto cyl = mm::solidCylinderWireframe(1.5, 5.0, seg);

    const std::size_t expectedEdges = 2 * static_cast<std::size_t>(seg) + 2;
    if (cyl.edges().size() != expectedEdges) {
        std::printf("HATA: silindir tesselasyonu %zu kenar (beklenen %zu)\n",
                    cyl.edges().size(), expectedEdges);
        return 1;
    }

    // 4) Kopru C-API'si: ayni kutu, POD tamponlar uzerinden
    float* verts = nullptr; int nv = 0;
    unsigned int* ed = nullptr; int ne = 0;
    if (mm_occ_solid_box(1.0, 2.0, 3.0, &verts, &nv, &ed, &ne) != 0 ||
        nv != 8 || ne != 12) {
        std::printf("HATA: kopru C-API %d kose / %d kenar (beklenen 8/12)\n", nv, ne);
        return 1;
    }
    mm_occ_free(verts);
    mm_occ_free(ed);

    std::printf("OCC SMOKE OK — kutu 10x10x10 hacim=%.6f, STEP yazildi/okundu hacim=%.6f, "
                "tesselasyon: kutu 8/12, silindir %zu kenar, kopru C-API 8/12, surum %s\n",
                volume, readBack, cyl.edges().size(), mm_occ_version());
    return 0;
}
#else
#include <cstdio>
int main() {
    std::printf("OCC bulunamadi — test atlandi\n");
    return 77;
}
#endif
