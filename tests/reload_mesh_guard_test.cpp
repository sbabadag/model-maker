// KAYIT/OKUMA GUARD TESTI — açılışta yeniden üretim kaydedilmiş elemanı
// KAYDIRMAMALI. Kural: mesh bbox sapması toleransı aşarsa kaydedilmiş mesh
// korunur (reloadKeepsSavedMesh true).
#include "model_maker/reload_mesh_guard.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* label) {
    if (condition) {
        std::printf("  ok   %s\n", label);
    } else {
        std::printf("  FAIL %s\n", label);
        ++failures;
    }
}

std::vector<mm::Vec3> boxVertices(double x0, double y0, double z0,
                                  double x1, double y1, double z1) {
    return {mm::Vec3{x0, y0, z0}, mm::Vec3{x1, y0, z0}, mm::Vec3{x1, y1, z0},
            mm::Vec3{x0, y1, z0}, mm::Vec3{x0, y0, z1}, mm::Vec3{x1, y0, z1},
            mm::Vec3{x1, y1, z1}, mm::Vec3{x0, y1, z1}};
}

} // namespace

int main() {
    const auto saved = mm::meshExtent(boxVertices(0, 10150, 6940, 260, 14850, 7200));

    std::printf("reload mesh guard:\n");
    check(saved.valid(), "saved extent gecerli");
    check(std::fabs(saved.lo[1] - 10150.0) < 1e-9 && std::fabs(saved.hi[1] - 14850.0) < 1e-9,
          "saved bbox dogru okundu");

    // Ayni geometri -> yeniden uretim kazanir (kaydetmeye gerek yok).
    const auto identical = mm::meshExtent(boxVertices(0, 10150, 6940, 260, 14850, 7200));
    check(!mm::reloadKeepsSavedMesh(saved, identical), "ayni bbox -> kaydedilen korunmaz");
    check(mm::meshExtentDeviation(saved, identical) == 0.0, "ayni bbox sapmasi 0");

    // Tessellation gurultusu (0.15 mm deflection) toleransin altinda.
    const auto noise = mm::meshExtent(boxVertices(0, 10150.2, 6940.1, 260.2, 14850.2, 7200.1));
    check(!mm::reloadKeepsSavedMesh(saved, noise), "0.2 mm tesselasyon gurultusu kabul edilir");

    // Gercek trim farki: 20 mm -> kaydedilen KORUNUR (kullanici bunu goruyordu).
    const auto trimmed = mm::meshExtent(boxVertices(0, 10170, 6940, 260, 14830, 7200));
    check(mm::reloadKeepsSavedMesh(saved, trimmed), "20 mm trim farki -> kaydedilen korunur");
    check(std::fabs(mm::meshExtentDeviation(saved, trimmed) - 20.0) < 1e-9,
          "20 mm sapma olculdu");

    // 280 mm fark (kalitli_yapi2 id=328 deseni) -> korunur.
    const auto longBar = mm::meshExtent(boxVertices(0, 10130, 6940, 260, 14870, 7200));
    check(mm::reloadKeepsSavedMesh(saved, longBar), "280/20 mm uzama -> kaydedilen korunur");

    // Saf oteleme (5 mm) -> korunur: eleman yeri degismemeli.
    const auto shifted = mm::meshExtent(boxVertices(5, 10155, 6945, 265, 14855, 7205));
    check(mm::reloadKeepsSavedMesh(saved, shifted), "5 mm oteleme -> kaydedilen korunur");

    // Bos / gecersiz taraf -> kiyaslama yapilamaz, yeniden uretim korunur.
    const mm::MeshExtent empty{};
    check(!mm::reloadKeepsSavedMesh(empty, identical), "bos kayit -> yeniden uretim kazanir");
    check(!mm::reloadKeepsSavedMesh(saved, empty), "bos yeniden uretim -> kaydedilen korunur degil");
    check(!mm::reloadKeepsSavedMesh(empty, empty), "iki bos -> kiyaslama yok");
    check(std::isinf(mm::meshExtentDeviation(empty, identical)), "gecersiz extent sapmasi +inf");

    // Tolerans parametresi: 20 mm sapma 50 mm toleransta kabul edilir.
    check(!mm::reloadKeepsSavedMesh(saved, trimmed, 50.0), "tolerans parametresi isliyor");

    if (failures) {
        std::printf("reload mesh guard: %d FAILED\n", failures);
        return 1;
    }
    std::printf("reload mesh guard: tum kontroller gecti\n");
    return 0;
}
