#pragma once
// ILERLEME BILDIRIMI — Qt'siz cekirdek.
//
// Uzun suren islemler (dosya ac/kaydet, assembly cizimi) kendi asamalarini burada bildirir;
// GUI katmani bunu ekranin ortasindaki pencereye cevirir (`progress_overlay.hpp`).
//
// KURAL: yalniz GERCEK adimlar bildirilir, sahte yuzde uretilmez.
//   * total > 0  -> belirli cubuk: done 0..total araliginda ARTAR (asama sayisi kadar).
//   * total <= 0 -> belirsiz (busy) cubuk: yalniz asama etiketi guncellenir.
//
// Callback bos olabilir (Qt'siz testler/CLI): reportProgress sessizce hicbir sey yapmaz.

#include <functional>
#include <string>

namespace mm {

struct ProgressReport {
    std::string stage;   // kullaniciya gosterilen kisa etiket ("Profil katilari 3/12")
    int done{0};         // tamamlanan adim (total > 0 ise anlamli)
    int total{0};        // toplam adim; <= 0 -> belirsiz cubuk
};

using ProgressCallback = std::function<void(const ProgressReport&)>;

inline void reportProgress(const ProgressCallback& callback, const std::string& stage, int done, int total) {
    if (callback) callback(ProgressReport{stage, done, total});
}

} // namespace mm
