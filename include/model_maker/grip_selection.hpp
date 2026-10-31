#pragma once

// KONTROL NOKTASI (profil ucu tutamagi) COKLU SECIMI — platformdan bagimsiz
// saf mantik. Windows'a bagli Application kodu bu fonksiyonlari cagirir;
// boylece secim kurali Linux'ta birim testiyle kilitlenir.
//
// Kullanici kurali (Selahattin 2026-10-31):
//   * Alt + pencere secimi: ONCEKI nokta secimini BIRAK, pencere ICINDE kalan
//     noktalari sec.
//   * Shift + Alt + pencere secimi: eskileri KORU, yenileri EKLE.
//   * Yalniz SECILI profillerin kontrol noktalari acilir/secilabilir.
//   * Secimden sonra move / move-delta komutu noktalari TOPLU tasir.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace mm {

// Kontrol noktasi kimligi: (profilli kati indeksi, uc). endIsTo=false -> from
// (sari), true -> to (mor).
struct GripRef {
    std::size_t solidIndex{};
    bool endIsTo{};
};

inline bool operator==(const GripRef& a, const GripRef& b) noexcept {
    return a.solidIndex == b.solidIndex && a.endIsTo == b.endIsTo;
}
inline bool operator!=(const GripRef& a, const GripRef& b) noexcept { return !(a == b); }

// Ekran noktasi (piksel). Vec2'den ayri tutuldu: secim tamamen 2B ekran uzayinda.
struct GripScreenPoint {
    double x{};
    double y{};
};

struct GripCandidate {
    GripRef ref;
    GripScreenPoint screen;
};

inline bool gripIsSelected(const std::vector<GripRef>& grips, const GripRef& ref) {
    return std::find(grips.begin(), grips.end(), ref) != grips.end();
}

// WINDOW semantigi: yalnizca dikdortgenin ICINDE kalan noktalar (kesen nokta
// secilmez). Surukleme yonu (sol-ust -> sag-alt ya da tersi) onemsizdir.
inline std::vector<GripRef> gripsInWindow(const std::vector<GripCandidate>& candidates,
                                          GripScreenPoint first, GripScreenPoint second) {
    const double left = std::min(first.x, second.x);
    const double right = std::max(first.x, second.x);
    const double top = std::min(first.y, second.y);
    const double bottom = std::max(first.y, second.y);
    std::vector<GripRef> out;
    for (const auto& candidate : candidates) {
        const double px = candidate.screen.x;
        const double py = candidate.screen.y;
        if (px < left || px > right || py < top || py > bottom) continue;
        if (!gripIsSelected(out, candidate.ref)) out.push_back(candidate.ref);
    }
    return out;
}

// Secim birlestirme: additive=false -> eski secim BIRAKILIR (yalniz yeni
// pencere icerigi); additive=true -> eskiler korunur, yeniler eklenir (tekrar
// eden kimlik iki kez girmez).
inline std::vector<GripRef> mergeGripSelection(const std::vector<GripRef>& existing,
                                               const std::vector<GripRef>& incoming,
                                               bool additive) {
    std::vector<GripRef> result = additive ? existing : std::vector<GripRef>{};
    for (const auto& ref : incoming)
        if (!gripIsSelected(result, ref)) result.push_back(ref);
    return result;
}

// Tek nokta toggle (Ctrl'siz Alt+tik ile nokta uzerine basma): varsa cikar,
// yoksa ekle.
inline void toggleGripSelection(std::vector<GripRef>& grips, const GripRef& ref) {
    const auto it = std::find(grips.begin(), grips.end(), ref);
    if (it == grips.end()) grips.push_back(ref);
    else grips.erase(it);
}

// Yalnizca SECILI profilli katilarin tutamaklari gecerli: kati secimden
// ciktiysa (ya da indeks belgeden dustuyse) grip secimi de duser.
inline std::vector<GripRef> pruneGripSelection(const std::vector<GripRef>& grips,
                                               const std::vector<std::size_t>& selectedModels) {
    std::vector<GripRef> out;
    out.reserve(grips.size());
    for (const auto& ref : grips) {
        if (std::find(selectedModels.begin(), selectedModels.end(), ref.solidIndex) ==
            selectedModels.end())
            continue;
        if (!gripIsSelected(out, ref)) out.push_back(ref);
    }
    return out;
}

} // namespace mm
