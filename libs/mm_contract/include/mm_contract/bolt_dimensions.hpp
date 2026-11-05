#pragma once

// ============================================================================
// BULON BOYUT TABLOSU — TEK KAYNAK (MM-8b/3).
//
// ISO 4014 (altigen basli civata) / ISO 4032 (altigen somun) / ISO 7089
// (pul, normal seri) TABLO degerleri, standart nominal caplar icin.
// Formul (s=1.5d, k=0.7d, m=0.8d, D=2d, t=0.15d) YALNIZ tabloda olmayan
// capta yedektir (mm::boltDimensions icinde). Bu dosya Qt/OCCT'sizdir;
// model-maker VE Studio (mmcs) ayni tabloyu buradan okur — iki tarafta ayri
// tablo tutmak renk/olcu kaymasinin klasik kaynagidir.
//
// s  = kosegen degil, altigen "across flats" (anahtar agzi) genisligi; bas ve
//      somun ayni nominal capta AYNI s degerini kullanir (ISO).
// k  = bas yuksekligi (ISO 4014), m = somun yuksekligi (ISO 4032),
// D  = pul dis capi (ISO 7089), t = pul kalinligi.
// ============================================================================

#include <cmath>

namespace mm {

struct BoltIsoDimension {
    double acrossFlats{0.0};       // s (bas + somun, altigen anahtar agzi)
    double headHeight{0.0};        // k (ISO 4014 bas yuksekligi)
    double nutHeight{0.0};         // m (ISO 4032 somun yuksekligi)
    double washerDiameter{0.0};    // D (ISO 7089 dis cap)
    double washerThickness{0.0};   // t (ISO 7089 kalinlik)
};

// Standart nominal cap (M12..M36) icin tablo satiri; capta yoksa nullptr
// (cagiran formul yedegine duser). Tolerans 1e-9 (nominal cap tam verilir).
inline const BoltIsoDimension* boltIsoTableDimension(double nominalDiameter) noexcept {
    struct Row {
        double d;
        BoltIsoDimension dim;
    };
    static const Row kTable[] = {
        {12.0, {18.0, 7.5, 10.8, 24.0, 2.5}},
        {16.0, {24.0, 10.0, 14.8, 30.0, 3.0}},
        {20.0, {30.0, 12.5, 18.0, 37.0, 3.0}},
        {24.0, {36.0, 15.0, 21.5, 44.0, 4.0}},
        {27.0, {41.0, 17.0, 23.8, 50.0, 4.0}},
        {30.0, {46.0, 18.7, 25.6, 56.0, 4.0}},
        {36.0, {55.0, 22.5, 31.0, 66.0, 5.0}},
    };
    for (const auto& row : kTable)
        if (std::abs(nominalDiameter - row.d) < 1e-9) return &row.dim;
    return nullptr;
}

}  // namespace mm
