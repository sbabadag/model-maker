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

// Snapshot tabanli geri al/yinele (deterministik; sinirli gecmis).
class PaperEditHistory {
public:
    explicit PaperEditHistory(DrawingSheet initial, std::size_t limit = 64);

    DrawingSheet& sheet() noexcept { return sheet_; }
    const DrawingSheet& sheet() const noexcept { return sheet_; }

    void begin();    // degisiklik oncesi anlik goruntu
    void commit();   // degisiklik yoksa at; varsa geri al yiginina koy
    void abort();    // anlik goruntuyu at (degisiklik yok)

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
