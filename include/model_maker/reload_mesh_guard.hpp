#pragma once

#include "model_maker/geometry.hpp"

#include <cmath>
#include <vector>

// KAYIT/OKUMA SÖZLEŞMESİ (Selahattin 2026-10-31): "kaydedilen portal
// dosyasında sonradan attığım çapraz elemanlar yüklendiğinde farklı yerlerde
// çıktı." MMW8 mesh'i (görünen geometri) VE profilin parametrelerini (eksen /
// rotasyon / kesit referans normali) birlikte saklar. Açılışta
// Application::openDocument her profilli model için recomputeCutTarget çağırır;
// bu, mesh'i PARAMETRELERDEN yeniden üretip belgeye YERİNE yazar. Kaydedilen
// mesh ile parametreler bir noktada ayrışmışsa (mesh bir kesme/trim sonucu,
// parametreler ise ham eksen) yeniden üretim elemanı KAYDIRIR — kullanıcı
// kaydettiği modeli başka yerde bulur.
//
// Kural: açılışta yeniden üretim yalnız kaydedilmiş mesh'i DOĞRULAR; bbox
// toleransın üzerinde oynuyorsa KAYDEDİLMİŞ geometri korunur (B-Rep yine de
// güncel parametrelerden yazılır, kesme çalışmaya devam eder). Böylece "ne
// kaydettiysen onu görürsün" garanti edilir; sessiz kaydırma olmaz.
namespace mm {

struct MeshExtent {
    double lo[3]{1e18, 1e18, 1e18};
    double hi[3]{-1e18, -1e18, -1e18};

    bool valid() const noexcept {
        return hi[0] >= lo[0] && hi[1] >= lo[1] && hi[2] >= lo[2];
    }
};

inline MeshExtent meshExtent(const std::vector<Vec3>& vertices) noexcept {
    MeshExtent extent;
    for (const auto& v : vertices) {
        const double c[3] = {v.x, v.y, v.z};
        for (int k = 0; k < 3; ++k) {
            if (c[k] < extent.lo[k]) extent.lo[k] = c[k];
            if (c[k] > extent.hi[k]) extent.hi[k] = c[k];
        }
    }
    return extent;
}

// Kaydedilmiş mesh bbox'ı ile yeniden üretilenin bbox'ı arasındaki en büyük
// sapma (mm). Geçersiz extent -> +inf (karşılaştırma yapılamaz).
inline double meshExtentDeviation(const MeshExtent& saved,
                                  const MeshExtent& reloaded) noexcept {
    if (!saved.valid() || !reloaded.valid()) return HUGE_VAL;
    double deviation = 0.0;
    for (int k = 0; k < 3; ++k) {
        deviation = std::fmax(deviation, std::fabs(saved.lo[k] - reloaded.lo[k]));
        deviation = std::fmax(deviation, std::fabs(saved.hi[k] - reloaded.hi[k]));
    }
    return deviation;
}

// Açılışta KAYDEDİLMİŞ mesh korunmalı mı? Yalnız iki taraf da geçerliyken ve
// sapma toleransı aşarken true. Tolerans tesselasyon gürültüsünü (0.15 mm
// deflection) kapsar; gerçek bir trim/kesme farkı (mm mertebesi) her zaman
// yakalanır.
inline bool reloadKeepsSavedMesh(const MeshExtent& saved,
                                 const MeshExtent& reloaded,
                                 double toleranceMm = 0.5) noexcept {
    if (!saved.valid() || !reloaded.valid()) return false;
    return meshExtentDeviation(saved, reloaded) > toleranceMm;
}

} // namespace mm
