#pragma once
// P2B — PAFTADA (paper-space) 2B DUZENLEME CEKIRDEGI (Qt'siz, OCCT'siz).
//
// KURAL: kagit duzenlemesi MUHENDISLIK VERISINI degistirmez.
//   * Tasima yalniz yerlesim alanlarini yazar: gorunus `origin`, olcu `offset/side/textT/textSide/textLevel`,
//     mark `labelPos`, metin `pos`, sekil `a/b/center`. `Dimension2D::value/p1/p2` ASLA degismez.
//   * Kullanici 2B cizimleri (`PaperShape`) kagit mm'dedir; model geometrisi degildir.
//   * Zorunlu imalat olcusu/notu gizlenirse cizim INCOMPLETE (TASLAK) olur; sahte READY yok.
//
// Ayni nihai plan ekranda (QPainter) ve PDF'te cizilir (tek renderer: paintSheet).
// Kalicilik: `DrawingSheet` JSON'u (sekil + gorunurluk alanlari) — ayri format YOK.

#include "model_maker/drawing.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace mm::drawing {

enum class PaperHitKind { None, View, Dimension, Mark, Text, Shape };

struct PaperHit {
    PaperHitKind kind{PaperHitKind::None};
    ElementId id{0};
    Vec2 paper{};
    bool valid() const noexcept { return kind != PaperHitKind::None; }
};

// Kagit mm noktasindaki eleman (oncelik: sekil > metin > mark > olcu > gorunus).
// `tolMm`: kagit mm toleransi (zoom'a gore cagiran verir; varsayilan 1.5 mm).
PaperHit hitTestPaper(const DrawingSheet& sheet, Vec2 paper, double tolMm = 1.5);

// Secili elemani kagit mm delta kadar tasir. true: tasindi.
// Olcu tasimada `offset` (olcu cizgisi uzakligi) ve `textT` (metin konumu) degisir; `value` DEGISMEZ.
bool movePaperElement(DrawingSheet& sheet, const PaperHit& hit, Vec2 deltaMm);

// Kullanici 2B sekli ekler (Line/Centerline: a-b; Circle: center=a, radius).
ElementId addPaperShape(DrawingSheet& sheet, ShapeKind kind, Vec2 a, Vec2 b, double radius = 0.0,
                        EntityId source = kInvalidEntityId);

// Goster/gizle (gorunusler gizlenemez; mimari geometri kagit duzenlemesiyle silinmez).
bool setPaperElementVisible(DrawingSheet& sheet, const PaperHit& hit, bool visible);

// Gizlenen zorunlu imalat olcusu/notu varsa kontrol listesine ANNOTATIONS (Unresolved) ekler,
// `fabrication.draft` ve `unresolvedCodes` guncellenir.
void evaluateAnnotationStatus(DrawingSheet& sheet);

// Model yeniden uretildikten sonra kullanici kagit duzenlemesini koru (kaynak eslesirse).
// Eslesme anahtarlari: gorunus (tur+subject+detay levhasi), olcu (gorunus+rol+featureKey+chain+yon),
// mark (gorunus+hedef+assembly), metin (gorunus+tur+featureKey+ilk satir). Kullanici sekilleri aynen tasinir.
void preservePaperEdits(const DrawingSheet& edited, DrawingSheet& regenerated);

// P2D: kagit boyutu / pafta sayisi degistiginde BIRDEN FAZLA duzenlenmis paftayi TEK yeniden
// uretilmis paftaya uygular (eslesen kaynak ogeler korunur; kullanici 2B sekilleri kimlik
// cakismasi olmadan BIRLESIR). Hicbir sey sessizce atilmaz: sonuc `evaluateAnnotationStatus` ile
// yeniden degerlendirilir; cagiran ayrica `refreshSheetQuality` ile cakisma kontrolu yapmalidir.
struct PaperEditMerge {
    int sheets{0};              // uygulanan duzenlenmis pafta sayisi
    std::size_t shapes{0};      // sonuc paftadaki kullanici 2B sekli
};
// `withShapes == false`: yalniz eslesen gorunus/olcu/mark/metin YERLESIMI uygulanir (sekiller
// cagiran tarafindan ayrica eklenir) — cok paftali birlestirmede sekil ciftlenmesini onler.
PaperEditMerge mergePaperEdits(const std::vector<DrawingSheet>& edited, DrawingSheet& regenerated,
                               bool withShapes = true);

// Kullanici 2B sekillerini paftaya ekler: ayni geometri tek kez, ElementId cakismasi yeniden atanir.
std::size_t appendPaperShapes(DrawingSheet& sheet, const std::vector<PaperShape>& shapes);

// ---------------------------------------------------------------------------------------
// P2 — COKLU SECIM (Window/Crossing) + SNAP'LI GRUP TASIMA (Qt'siz cekirdek)
//
// Kural: bant secimi de yalniz YERLESIM alanlarini tasir; muhendislik verisi (olcu `value`,
// `p1/p2`) degismez. Secim kumesi deterministiktir (tur onceligi + vektor sirasi).
// ---------------------------------------------------------------------------------------

enum class PaperBandMode { None, Window, Crossing };

// Kapali alanda basilan nokta `down`, birakilan nokta `up` (kagit mm):
//   * soldan saga (dx >= 0) -> Window: BANT ICINE TAMAMEN giren elemanlar,
//   * sagdan sola (dx < 0)  -> Crossing: bantla KESISEN (veya icinde kalan) elemanlar.
// `minDragMm` altindaki hareket tik sayilir -> None (kazara secim yok).
PaperBandMode paperBandMode(Vec2 down, Vec2 up, double minDragMm = 0.0);

// Iki kose -> normalize edilmis sinirlar (surukleme yonu ne olursa olsun).
Bounds2 paperBandBounds(Vec2 a, Vec2 b);

// Elemanin kagit mm sinirlari (gorunus/olcu/mark/metin/sekil); gecersizse `empty`.
Bounds2 paperElementBounds(const DrawingSheet& sheet, const PaperHit& hit);

struct PaperSelection {
    std::vector<PaperHit> hits;
    bool empty() const noexcept { return hits.empty(); }
    std::size_t size() const noexcept { return hits.size(); }
    bool contains(PaperHitKind kind, ElementId id) const noexcept;
};

// Bant secimi. GORUNUSLER (View) bantla SECILMEZ: buyuk bir gorunus Crossing modunda her
// seferinde secilir ve yanlislikla tasinir; gorunus tiklayarak secilir/tasinir.
PaperSelection selectPaperElements(const DrawingSheet& sheet, Bounds2 band, PaperBandMode mode);

// Secili elemanlarin TUMUNU ayni delta ile tasir (tek cagri = tek geri-al adimi icin).
// Donus: tasinan eleman sayisi (0 = hicbir sey degismedi).
std::size_t movePaperElements(DrawingSheet& sheet, const PaperSelection& selection, Vec2 deltaMm);

// Pikselden bagimsiz snap (cagiran toleransi verir; GUI'de 6 px -> mm).
// Adaylar: sekil uclari/orta noktasi/merkezi, gorunus origin+koseleri, mark/metin konumu,
// cerceve + cizim alani koseleri. En yakin aday kazanir (esitlikte once eklenen).
struct PaperSnap {
    Vec2 point{};
    bool snapped{false};
    std::string what;   // "sekil ucu", "sekil merkezi", "gorunus", "metin", "cerceve"
};
PaperSnap snapPaperPoint(const DrawingSheet& sheet, Vec2 p, double tolMm);


// Snapshot tabanli geri al/yinele (deterministik; sinirli gecmis).
class PaperEditHistory {
public:
    explicit PaperEditHistory(DrawingSheet initial, std::size_t limit = 64);

    DrawingSheet& sheet() noexcept { return sheet_; }
    const DrawingSheet& sheet() const noexcept { return sheet_; }

    void begin();    // degisiklik oncesi anlik goruntu
    void commit();   // degisiklik yoksa at; varsa geri al yiginina koy
    void abort();    // anlik goruntuyu at (degisiklik yok)
    // P2: acik snapshot'tan GERI DON (surukleme iptali / Esc). Yigin degismez.
    bool cancel();

    bool undo();
    bool redo();
    bool canUndo() const noexcept { return !undo_.empty(); }
    bool canRedo() const noexcept { return !redo_.empty(); }
    std::size_t undoDepth() const noexcept { return undo_.size(); }

    void reset(DrawingSheet sheet);

private:
    DrawingSheet sheet_;
    std::vector<DrawingSheet> undo_;
    std::vector<DrawingSheet> redo_;
    DrawingSheet pending_;
    std::size_t limit_;
    bool open_{false};
};

} // namespace mm::drawing
