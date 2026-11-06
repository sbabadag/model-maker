#pragma once

// SNAP GORUNURLUK / KAPMA AYARLARI (UI-6a) — TASINABILIR, Qt'siz, windows.h'siz.
//
// Dort deger de kullanicidan degistirilebilir ve oturumlar arasi hatirlanir
// (Application::saveOptions/loadOptions bunlari options dosyasina yazar).
// Degerler 96 DPI tabanindadir; ekranda DPI olcegiyle carpilir (marker boyutu
// ve kalem kalinligi icin). Linux'ta serialize/parse dogrudan test edilir.

#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace mm {

struct SnapSettings {
    int markerScale{3};        // eski kSnapMarkerScale=2 -> 1.5 kat = 3 (taban yaricap 7 px)
    int markerPenWidth{3};     // eski 2 px -> 3 px (kalin cizgi)
    double holdTimeMs{300.0};  // imlec uzaklasinca marker bu kadar ms tutulur
    double apertureScale{1.5}; // yakalama toleransi carpani (eski 10 px -> 15 px)

    // Sinir disi degerleri guvenli araliga ceker (serilestirmeden sonra cagrilir).
    // NaN yalnizca varsayilana doner; sayisal degerler [min,max] araligina kistirilir.
    void clamp() noexcept {
        if (markerScale < 1) markerScale = 1;
        else if (markerScale > 8) markerScale = 8;
        if (markerPenWidth < 1) markerPenWidth = 1;
        else if (markerPenWidth > 8) markerPenWidth = 8;
        if (std::isnan(holdTimeMs)) holdTimeMs = 300.0;
        else if (holdTimeMs < 0.0) holdTimeMs = 0.0;
        else if (holdTimeMs > 2000.0) holdTimeMs = 2000.0;
        if (std::isnan(apertureScale)) apertureScale = 1.5;
        else if (apertureScale < 0.5) apertureScale = 0.5;
        else if (apertureScale > 8.0) apertureScale = 8.0;
    }
};

// Eski (UI-6a oncesi) yakalama toleransi — TEK KAYNAK. 2B'de
// kSnapApertureBasePx / (60.0 * zoom), 3B'de dogrudan piksel olarak kullanilir.
// apertureScale bunu carpar (UI-6a: 1.5 kat).
inline constexpr double kSnapApertureBasePx = 10.0;

// key=value satirlari. Bilinmeyen anahtar yok sayilir (ileri uyumluluk);
// gecersiz deger o alan icin varsayilana duser.
inline std::string serializeSnapSettings(const SnapSettings& s) {
    std::string out;
    out.reserve(128);
    out += "markerScale=" + std::to_string(s.markerScale) + "\n";
    out += "markerPenWidth=" + std::to_string(s.markerPenWidth) + "\n";
    out += "holdTimeMs=" + std::to_string(s.holdTimeMs) + "\n";
    out += "apertureScale=" + std::to_string(s.apertureScale) + "\n";
    return out;
}

namespace detail {
inline double parseDouble(std::string_view value) {
    std::string tmp(value);
    try {
        std::size_t pos = 0;
        const double d = std::stod(tmp, &pos);
        return (pos == tmp.size()) ? d : std::numeric_limits<double>::quiet_NaN();
    } catch (...) {
        return std::numeric_limits<double>::quiet_NaN();
    }
}
inline int parseInt(std::string_view value) {
    std::string tmp(value);
    try {
        std::size_t pos = 0;
        const long n = std::stol(tmp, &pos);
        return (pos == tmp.size()) ? static_cast<int>(n) : 0;
    } catch (...) {
        return 0;
    }
}
} // namespace detail

inline std::optional<SnapSettings> parseSnapSettings(std::string_view text) {
    SnapSettings s;
    bool any = false;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t nl = text.find('\n', pos);
        const std::string_view line =
            (nl == std::string_view::npos) ? text.substr(pos) : text.substr(pos, nl - pos);
        pos = (nl == std::string_view::npos) ? text.size() : nl + 1;
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        const std::string_view key = line.substr(0, eq);
        const std::string_view value = line.substr(eq + 1);
        if (key == "markerScale") { s.markerScale = detail::parseInt(value); any = true; }
        else if (key == "markerPenWidth") { s.markerPenWidth = detail::parseInt(value); any = true; }
        else if (key == "holdTimeMs") { s.holdTimeMs = detail::parseDouble(value); any = true; }
        else if (key == "apertureScale") { s.apertureScale = detail::parseDouble(value); any = true; }
    }
    if (!any) return std::nullopt;
    s.clamp();
    return s;
}

} // namespace mm
