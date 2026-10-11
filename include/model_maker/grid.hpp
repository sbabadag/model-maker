#pragma once

// TEKLA-TARZI YAPI GRIDI (portatif cekirdek).
//
// Tekla'da grid bir *ozellik spesifikasyonu* ile tanimlanir; cizim geometrisi
// bundan TUREtilir. Bu baslik o spesifikasyonu, koordinat DILINI ve turetmeyi
// tutar. Windows basligi tanimaz — Linux'ta test edilebilir.
//
// Tekla koordinat dili (Rectangular grid > Coordinates):
//   * girdiler boslukla ayrilir
//   * "N*deger" N kez tekrar demektir ("0 2*4000" == "0 4000 4000")
//   * X ve Y girdileri ONCEKINE GORE ARTIMSAL (kumulatif toplanir)
//   * Z girdileri MUTLAK (is duzlemi orijininden)
// Etiketler: X kutusu Y eksenine PARALEL cizgileri, Y kutusu X eksenine
// paralel cizgileri, Z kutusu kat seviyelerini adlandirir. Bos birakilabilir.

#include "model_maker/geometry.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace mm {

// Tek bir aks cizgisi (cizim/snap geometrisi).
struct GridAxisLine {
    Vec3 from{};         // aks cizgisinin baslangici
    Vec3 to{};           // bitisi
    std::string label;   // ornek: "1", "2", "A", "B"
    bool horizontal{};   // Y-yonu (A-B-C) veya Z kot cizgisi ise true
};

enum class GridKind { Rectangular = 0, Radial = 1 };

// TEKLA-TARZI grid tanimi. `axes` TUREtilmis geometridir (rebuildGridAxes);
// dosyaya yazilan `axes` degil, yukaridaki spesifikasyondur.
struct GridDefinition {
    GridKind kind{GridKind::Rectangular};
    std::string name = "GRID";
    bool visible{true};
    bool magnetic{false};   // "Magnetic grid plane": nesneler aksa bagli

    // --- Tekla koordinat spesifikasyonu (KAYNAK) ---
    std::vector<double> xCoords;   // dikey akslarin X konumlari (grid yerel)
    std::vector<double> yCoords;   // yatay akslarin Y konumlari (grid yerel)
    std::vector<double> zCoords;   // kat kotlari (MUTLAK, is duzleminden)
    std::vector<std::string> xLabels;  // Y'ye paralel cizgilerin adlari
    std::vector<std::string> yLabels;  // X'e paralel cizgilerin adlari
    std::vector<std::string> zLabels;  // kat adlari

    // --- Uzanti (Tekla: Left/Below ve Right/Above AYRI) ---
    double extendLeft{1000.0};
    double extendRight{1000.0};
    double extendBelow{1000.0};
    double extendAbove{1000.0};

    // --- Orijin (is duzlemi orijininden offset) ---
    Vec3 origin{};

    // --- Gorunum ---
    unsigned int color{0x6E82AAu};       // aks cizgi rengi (0xRRGGBB)
    unsigned int labelColor{0x3C446Eu};  // etiket rengi (0xRRGGBB)
    int labelFontSize{14};               // etiket yazi boyu (px)

    // --- Turetilmis cizim geometrisi (rebuildGridAxes ile dolar) ---
    std::vector<GridAxisLine> axes;
};

// "0 2*4000" -> cumulative=true: {0,4000,8000}; false: {0,4000,4000}.
// Gecersiz token'lar sessizce atlanir (bos sonuc -> bos grid).
std::vector<double> parseGridCoordinateLine(std::string_view text, bool cumulative);

// Etiket satiri: boslukla ayrilmis adlar. Bos metin -> bos vektor.
std::vector<std::string> parseGridLabelLine(std::string_view text);

// Otomatik etiketler: X icin sayi (1,2,3...), Y icin harf (A,B,C...).
std::string gridAutoNumberLabel(std::size_t index);
std::string gridAutoLetterLabel(std::size_t index);

// Eksik etiketleri tamamlar: etiket listesi DOLU ama koordinat sayisindan
// kisa ise kalanlar otomatik adlandirilir. Liste TAMAMEN bos birakilirsa
// dokunulmaz (Tekla: etiket kutusu bos birakilabilir).
void ensureGridLabels(GridDefinition& grid);

// Spesifikasyondan `axes`'i yeniden uretir (origin + uzanti + etiket dahil).
// xCoords/yCoords/zCoords bos ise o yon cizilmez.
void rebuildGridAxes(GridDefinition& grid);

} // namespace mm
