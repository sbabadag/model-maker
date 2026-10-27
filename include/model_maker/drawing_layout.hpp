#pragma once
// AKILLI SAYFA YERLESIM MOTORU (P2A) — annotation'lari KAGIT mm'de, deterministik, cakismasiz yerlestirir.
//
// AYRIM: geometri (gorunum segmentleri, model degerleri) ve annotation yerlesimi AYRIDIR.
//   * Yerlesim yalniz `Dimension2D::{offset,side,textT,textSide,textLevel}`, `TextAnnotation::pos`,
//     `MarkAnnotation::labelPos`, gorunum `scale/origin` ve sayfa boyutunu degistirir.
//   * Olcu DEGERLERI (value, p1, p2), modelden gelen hicbir sey DEGISMEZ; Document'a hic dokunulmaz.
//
// SERT KISITLAR (ihlal -> sorun raporu, SESSIZCE cakistirma/kucultme YOK):
//   metin/metin cakismasi yok; metin hicbir olcu/uzatma/kilavuz cizgisine, gorunum geometrisine
//   (segment/daire), baska gorunumun sinirina, title block'a, cerceve disina binmez;
//   metin yuksekligi yerlesim ayarindaki minimumun (varsayilan 3.0 olcu / 3.5 not) ALTINA inmez.
// YUMUSAK: kisa kilavuz, az kesisim, standart olcek, tekrarlanabilir (deterministik) yerlesim.
// OLCEK/KAGIT: once en buyuk standart olcek, sigmazsa/cakisirsa daha kucuk olcek, sonra A3->A2->A1.
//
// Ayni nihai plan onizlemede (QPainter) ve PDF'te cizilir: cizim `resolve*` fonksiyonlarini kullanir.
// OCCT'siz, Qt'siz.

#include "model_maker/drawing.hpp"

#include <string>
#include <vector>

namespace mm::drawing {

struct LineSeg {
    Vec2 a{};
    Vec2 b{};
};

// Yonlu dikdortgen (metin kutusu): merkez, yari-boyutlar, aci (derece).
struct Obb {
    Vec2 c{};
    double hx{0.0};
    double hy{0.0};
    double angleDeg{0.0};
    Bounds2 aabb() const noexcept;
    Obb inflated(double d) const noexcept { return {c, hx + d, hy + d, angleDeg}; }
};

bool intersects(const Obb& a, const Obb& b) noexcept;
bool intersects(const LineSeg& s, const Obb& b) noexcept;
bool intersects(const LineSeg& a, const LineSeg& b) noexcept;
Obb obbOfRect(const Bounds2& r) noexcept;

// Muhafazakar, Qt'siz metin genisligi (kagit mm). `capHeightMm`: buyuk harf yuksekligi.
// Cizim katmani glifleri em = capHeight / 0.70 ile cizer; gercek Qt genisligi <= bu tahmin (GUI testi dogrular).
double textWidthEstimate(const std::string& utf8, double capHeightMm, bool bold = false);
// Metin kutusu yuksekligi (tek satir): kapak yuksekligi + inis/binis payi.
double textBoxHeight(double capHeightMm) noexcept;
std::string dimensionText(double value);   // tam sayiya yakinsa tam sayi, degilse 1 ondalik

struct DimensionGeometry {
    bool valid{false};
    std::vector<LineSeg> lines;   // uzatma cizgileri + olcu cizgisi + (varsa) kilavuz
    LineSeg tick1{}, tick2{};     // 45 derece isaretler (cizim)
    Obb textBox;
    Vec2 textCentre{};
    double textAngleDeg{0.0};
    double textHeight{0.0};
    std::string text;
};
DimensionGeometry resolveDimension(const DrawingSheet& sheet, const Dimension2D& dim);

struct TextGeometry {
    Obb box;                        // aci 0
    bool hasLeader{false};
    LineSeg leader{};
    std::vector<Vec2> lineCentres;  // her satirin SOL-ORTA noktasi (kagit)
};
TextGeometry resolveText(const DrawingSheet& sheet, const TextAnnotation& t);
Bounds2 markBox(const MarkAnnotation& m) noexcept;   // = markLabelBox (tek kaynak)

// Bagimsiz OLCUM: nihai sayfada kalan cakismalar (yerlesim motorundan bagimsiz kod yolu).
struct OverlapReport {
    int textText{0};
    int textLine{0};        // metin x (olcu/uzatma/kilavuz cizgisi)
    int textGeometry{0};    // metin x gorunum segmenti/dairesi
    int textView{0};        // metin x gorunum sinirlari (kendi gorunumu haric degil: tum gorunumler)
    int textTitle{0};       // metin x title block
    int outsideBorder{0};
    int tooSmall{0};        // minimum yazi yuksekliginin altinda
    std::vector<std::string> details;
    int total() const noexcept {
        return textText + textLine + textGeometry + textView + textTitle + outsideBorder + tooSmall;
    }
};
OverlapReport measureOverlaps(const DrawingSheet& sheet);

// Gorunumleri + annotation'lari yerlestirir (sheet.layoutConfig kullanilir). Sonuc sheet.layout'ta.
// true: zorunlu hicbir sorun yok (sheet.layout.ok). false: sorunlar sheet.layout.issues'ta (CIZIM ONAYSIZ).
// Girdi: gorunumler (yerel geometri) + dimensions/texts/marks (varsayilan parametrelerle) hazir olmali.
bool layoutSheet(DrawingSheet& sheet);

// Standart olcek paydalari (1:n).
const std::vector<double>& standardScaleDenominators();

} // namespace mm::drawing
