#pragma once
// DRAWING GUI (P1): sayfa cizimi (QPainter), onizleme widget'i, PDF/PNG cikti ve secenek diyalogu.
//
// Cizim TEK fonksiyondan gelir (paintSheet): ekran, PDF ve PNG birebir ayni yolu kullanir.
// Qt Widgets/Gui yeterli (OCCT yok) -> Linux offscreen testinde render edilip dogrulanir.

#include "model_maker/drawing_builder.hpp"
#include "model_maker/drawing_set.hpp"
#include "model_maker/paper_editing.hpp"

#include <QDialog>
#include <QImage>
#include <QString>
#include <QWidget>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;

namespace mm::drawing {

// Cizgi kalinliklari (kagit mm; PDF'de gercek mm). Ince tutulur: ayrintili cizimde okunabilirlik.
namespace lineWeightMm {
inline constexpr double visible = 0.18;
inline constexpr double hidden = 0.10;
inline constexpr double circle = 0.18;
inline constexpr double centre = 0.08;
inline constexpr double dimension = 0.10;
inline constexpr double mark = 0.12;
inline constexpr double markAssembly = 0.20;
inline constexpr double frame = 0.35;
inline constexpr double titleOuter = 0.25;
inline constexpr double titleInner = 0.10;
} // namespace lineWeightMm

// Sayfayi `target` dikdortgenine (en-boy korunarak, ortalanmis) cizer. Birim: sayfa mm.
void paintSheet(QPainter& painter, const DrawingSheet& sheet, const QRectF& target);

// Gercek Qt metin genisligi (kagit mm, buyuk harf yuksekligi `capHeightMm`). Yerlesim motorunun
// `textWidthEstimate` degerinin bunu ASMADIGI GUI testinde dogrulanir.
double measureTextWidthMm(const QString& text, double capHeightMm, bool bold = false);

QImage renderSheetImage(const DrawingSheet& sheet, int widthPixels);

// A-serisi yatay PDF (kagit boyutu = sheet.format). Basarisizlikta false + error.
bool exportSheetPdf(const DrawingSheet& sheet, const QString& path, QString* error = nullptr);

// P2C: TUM paftalari tek vektor PDF'e yazar (her pafta kendi boyutunda bir sayfa).
bool exportDrawingSetPdf(const DrawingDocument& document, const QString& path, QString* error = nullptr);

class DrawingSheetWidget : public QWidget {
    Q_OBJECT
public:
    // P2: Move = secimi snap'li olarak tasima (taban tik + hedef tik).
    enum class EditTool { Select, Move, Line, Circle, Centerline };
    explicit DrawingSheetWidget(QWidget* parent = nullptr);
    void setSheet(const DrawingSheet* sheet); // sahiplik cagirandadir; nullptr = bos
    void fitToWindow();
    double zoom() const noexcept { return zoom_; }

    // --- P2B: paftada duzenleme (sec/tasi/2B sekil). history sahipligi cagirandadir. ---
    void setEditor(PaperEditHistory* history);
    bool hasEditor() const noexcept { return history_ != nullptr; }
    void setTool(EditTool tool) noexcept;
    EditTool tool() const noexcept { return tool_; }
    PaperHit selection() const noexcept { return selection_; }              // birincil (ilk) secim
    const std::vector<PaperHit>& selectionAll() const noexcept { return selectionAll_; }
    void clearSelection();
    // P2: secili TUM elemanlari tek deltayla tasir -> tek geri-al adimi. true: en az biri tasindi.
    bool moveSelection(Vec2 deltaMm);
    void undoEdit();
    void redoEdit();
    bool hideSelection();     // secili zorunlu olcu/notu gizler -> cizim INCOMPLETE olabilir

protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    QRectF targetRect() const;
    Vec2 paperAt(const QPointF& pos) const;
    QPointF widgetAt(Vec2 paperMm) const;
    double mmPerPixel() const;
    void setSelection(std::vector<PaperHit> hits);
    void commitShape(Vec2 end);
    void paintEditOverlay(QPainter& p);
    const DrawingSheet* sheet_{};
    double zoom_{1.0};
    QPointF pan_{};
    QPoint lastPos_{};
    bool fitted_{true};
    PaperEditHistory* history_{};
    PaperHit selection_{};
    std::vector<PaperHit> selectionAll_{};
    EditTool tool_{EditTool::Select};
    bool dragging_{false};
    bool drawing_{false};
    Vec2 lastPaper_{};
    Vec2 drawStart_{};
    // --- P2: bant secimi / snap'li tasima / onizleme durumu ---
    bool banding_{false};
    Vec2 bandStart_{};
    Vec2 bandEnd_{};
    bool moveBaseSet_{false};
    Vec2 moveBase_{};
    Vec2 cursorPaper_{};
    PaperSnap snap_{};
};

struct SubjectChoice {
    QString label;
    EntityId id{kInvalidEntityId};
};

// "Secimden cizim" diyalogu: konu/kagit/projeksiyon/gorunum/gizli cizgi secenekleri; her
// degisimde cizim yeniden uretilir. Builder, uygulamanin Document + MarkRegistry'sini kullanir.
class DrawingPreviewDialog : public QDialog {
    Q_OBJECT
public:
    using Builder = std::function<BuildResult(EntityId subject, const DrawingOptions&)>;
    // P2C: akilli pafta yoneticisi (tek pafta -> gerekirse bagli coklu pafta + tercihler).
    using SetBuilder = std::function<DrawingSetResult(EntityId subject, const DrawingOptions&,
                                                      const DrawingPreferences&)>;
    DrawingPreviewDialog(std::vector<SubjectChoice> subjects, int defaultSubject, Builder builder,
                         QWidget* parent = nullptr, SetBuilder setBuilder = {});
    // Son basarili cizim; yoksa nullptr. (Set modunda: o an gosterilen pafta.)
    const DrawingSheet* currentSheet() const noexcept;
    // Set modunda tum paftalar; yoksa nullptr.
    const DrawingDocument* currentDocument() const noexcept { return haveDoc_ ? &doc_ : nullptr; }

    // P2D: konu degisikligi gibi kaynak eslesmesi OLMAYAN (yikici) yeniden uretimden once sorulur.
    // Varsayilan: QMessageBox. Testler kendi kararini enjekte eder (modal olmadan dogrulanabilir).
    void setRebuildConfirmation(std::function<bool(const QString&)> confirm);

private:
    void rebuild();
    void onExportPdf();
    void onSaveJson();
    void showSheet(int index);
    void syncEditedSheet(int index);
    // O anki pafta duzenlemeleri: (pafta indeksi, duzenlenmis kopya). Yeniden uretimde korunur.
    std::vector<std::pair<int, DrawingSheet>> collectEditedSheets() const;
    int mergePreservedEdits(const std::vector<std::pair<int, DrawingSheet>>& edits, DrawingSetResult& r);

    std::vector<SubjectChoice> subjects_;
    Builder builder_;
    SetBuilder setBuilder_;
    DrawingSheet sheet_;
    bool haveSheet_{false};
    DrawingDocument doc_;
    bool haveDoc_{false};
    DrawingPreferences prefs_{};
    int currentSheet_{0};
    int builtSubjectIndex_{-1};                 // doc_ hangi konu indeksinden uretildi
    std::vector<DrawingSheet> baseline_;        // uretim sonrasi (duzenleme karsilastirma tabani)
    std::function<bool(const QString&)> confirmDiscard_;
    bool rebuilding_{false};
    DrawingSheetWidget* view_{};
    std::unique_ptr<PaperEditHistory> edit_;
    QComboBox* subjectCombo_{};
    QComboBox* sizeCombo_{};
    QComboBox* sheetCombo_{};
    QComboBox* toolCombo_{};
    QCheckBox* autoSizeCheck_{};
    QCheckBox* multiSheetCheck_{};
    QComboBox* projectionCombo_{};
    QCheckBox* frontCheck_{};
    QCheckBox* topCheck_{};
    QCheckBox* sideCheck_{};
    QCheckBox* hiddenCheck_{};
    QLabel* status_{};
};

// Secili model indekslerinden diyalog konulari: ilk cizilebilir secili parca (+ varsa
// bagli oldugu assembly). defaultIndex: assembly tercih edilmisse (veya birden fazla secim
// varsa) assembly, yoksa parca. Cizilebilir secim yoksa bos liste.
std::vector<SubjectChoice> subjectChoicesFor(const Document& document,
                                             const std::vector<std::size_t>& selected,
                                             bool preferAssembly, int& defaultIndex);

// Menu komutu: konulari cikarir, bos ise bilgi kutusu gosterir, aksi halde modal diyalogu acar.
// Mark kayit defteri her cagrida yenidir: numaralar belge model sirasindan deterministik gelir.
bool showDrawingDialog(QWidget* parent, const Document& document,
                       const std::vector<std::size_t>& selected, bool preferAssembly);

} // namespace mm::drawing
