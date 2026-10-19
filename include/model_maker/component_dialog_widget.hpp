#pragma once

// UNIVERSAL COMPONENT DIALOG — Qt SHELL (Tekla-tarzi parametre editörü).
//
// Taşınabilir çekirdek (component_dialog.hpp) diyalog ŞEMASINI taşır: 2B
// sematik çizim + hotspot'lar + parametre grupları. Bu katman o şemayı GERÇEK
// bir Qt diyaloğuna dönüştürür:
//
//   sol  = tıklanabilir sematik çizim (ComponentSchematicView),
//   sağ  = grup grup parametre editörleri (ComponentDialogWidget).
//
// Hotspot <-> parametre satırı vurgusu ÇİFT YÖNLÜDÜR (Tekla tarzı): çizimde
// bir hotspot'a tıklayınca ilgili satır vurgulanır + görünüme kaydırılır; bir
// satıra odaklanınca çizimde bağlı hotspot vurgulanır. Çizim ile hit-test AYNI
// şema verisini kullanır (hitTestDiagramHotspot) — tıklanan şey çizilen şeydir.
//
// Editör widget'ları CompParamSpec'ten (component_engine.hpp) tip-agnostik
// üretilir; resultParams() düzenlenen değerleri semaya hizalar ve `current`'taki
// BİLİNMEYEN (ekstra) parametreleri korur (engine felsefesi: sessiz kayıp yok).
//
// Yalnız Qt; Windows'ta model-maker exe'siyle derlenir (Linux'ta offscreen testi
// component_dialog_widget_test.cpp ile doğrulanır).

#include "model_maker/component_dialog.hpp"

#include <QWidget>
#include <map>
#include <vector>

class QScrollArea;
class QVBoxLayout;

namespace mm {

// Sematik tuval: normalize 2B çizimi çizer, hotspot tıklamasını yakalar.
class ComponentSchematicView : public QWidget {
    Q_OBJECT
public:
    explicit ComponentSchematicView(const ComponentDialogSchema& schema,
                                    QWidget* parent = nullptr);

    // Bu parametreye bağlı TÜM hotspot'ları vurgula (boş = vurgu yok).
    void setHighlightedParam(const std::wstring& paramName);
    std::wstring highlightedParam() const { return highlightedParam_; }

signals:
    // Bir hotspot tıklandı; bağlı parametre adı (bağsız/dekoratif ise boş).
    void hotspotClicked(const QString& paramName);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void recomputeTransform();
    QPointF toScreen(const DiagramPoint& w) const;
    DiagramPoint toWorld(const QPointF& s) const;

    const ComponentDialogSchema& schema_;
    std::wstring highlightedParam_;
    double scale_{1.0};
    double offsetX_{0.0};
    double offsetY_{0.0};
    double minX_{0.0}, minY_{0.0}, maxX_{0.0}, maxY_{0.0};
};

// Diyalog gövdesi: solda sematik, sağda gruplanmış parametre editörleri.
class ComponentDialogWidget : public QWidget {
    Q_OBJECT
public:
    explicit ComponentDialogWidget(const ComponentDialogDefinition& def,
                                   const std::vector<CompParam>& current,
                                   QWidget* parent = nullptr);

    // Düzenlenen parametreler: def.specs'e hizalanır, bilinmeyen (ekstra)
    // parametreler `current`'tan korunur.
    std::vector<CompParam> resultParams() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onHotspotClicked(const QString& paramName);

private:
    QWidget* buildEditor(const CompParamSpec& spec, const CompParamValue& value,
                         QWidget* parent);
    static CompParamValue valueFromEditor(QWidget* editor, const CompParamSpec& spec);
    void highlightParamRow(const std::wstring& paramName);

    ComponentDialogDefinition def_;       // kopya — registry ömründen bağımsız
    std::vector<CompParam> current_;

    ComponentSchematicView* schematic_{};
    QScrollArea* scroll_{};
    QWidget* paramBody_{};
    QVBoxLayout* paramLayout_{};
    std::map<std::wstring, QWidget*> editors_;      // paramName -> editor
    std::map<std::wstring, QWidget*> rows_;         // paramName -> satır etiketi (vurgu)
    std::map<std::wstring, std::vector<std::wstring>> paramToHotspots_;
    std::wstring highlightedRow_;
};

} // namespace mm
