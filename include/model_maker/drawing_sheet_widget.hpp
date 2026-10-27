#pragma once
// DRAWING GUI (P1): sayfa cizimi (QPainter), onizleme widget'i, PDF/PNG cikti ve secenek diyalogu.
//
// Cizim TEK fonksiyondan gelir (paintSheet): ekran, PDF ve PNG birebir ayni yolu kullanir.
// Qt Widgets/Gui yeterli (OCCT yok) -> Linux offscreen testinde render edilip dogrulanir.

#include "model_maker/drawing_builder.hpp"

#include <QDialog>
#include <QImage>
#include <QString>
#include <QWidget>

#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;

namespace mm::drawing {

// Sayfayi `target` dikdortgenine (en-boy korunarak, ortalanmis) cizer. Birim: sayfa mm.
void paintSheet(QPainter& painter, const DrawingSheet& sheet, const QRectF& target);

QImage renderSheetImage(const DrawingSheet& sheet, int widthPixels);

// A-serisi yatay PDF (kagit boyutu = sheet.format). Basarisizlikta false + error.
bool exportSheetPdf(const DrawingSheet& sheet, const QString& path, QString* error = nullptr);

class DrawingSheetWidget : public QWidget {
    Q_OBJECT
public:
    explicit DrawingSheetWidget(QWidget* parent = nullptr);
    void setSheet(const DrawingSheet* sheet); // sahiplik cagirandadir; nullptr = bos
    void fitToWindow();
    double zoom() const noexcept { return zoom_; }

protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    QRectF targetRect() const;
    const DrawingSheet* sheet_{};
    double zoom_{1.0};
    QPointF pan_{};
    QPoint lastPos_;
    bool fitted_{true};
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
    DrawingPreviewDialog(std::vector<SubjectChoice> subjects, int defaultSubject, Builder builder,
                         QWidget* parent = nullptr);
    // Son basarili cizim; yoksa nullptr.
    const DrawingSheet* currentSheet() const noexcept { return haveSheet_ ? &sheet_ : nullptr; }

private:
    void rebuild();
    void onExportPdf();
    void onSaveJson();

    std::vector<SubjectChoice> subjects_;
    Builder builder_;
    DrawingSheet sheet_;
    bool haveSheet_{false};
    DrawingSheetWidget* view_{};
    QComboBox* subjectCombo_{};
    QComboBox* sizeCombo_{};
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
