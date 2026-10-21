#pragma once

#include <QMainWindow>
#include <QMenuBar>
#include <QToolBar>
#include <QDockWidget>
#include <QAction>
#include <QWidget>

#include "model_maker/application.hpp"

class QComboBox;
class QMdiArea;
class QMdiSubWindow;
class QMenu;

namespace mm {

class QtMainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit QtMainWindow(QWidget* parent = nullptr);
    ~QtMainWindow() override;

    Application& application() { return app_; }

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void createMenus();
    // Tekla "Create rectangular grid" + property pane esdegeri: grid
    // SPESIFIKASYONU (koordinat dili, etiketler, uzanti, orijin, gorunum)
    // ile yeni grid olusturur.
    void promptCreateGrid();
    // Tekla "Grids" yoneticisi: mevcut gridleri listeler; duzenle/sil/
    // gorunurluk. Double-click grid property pane'in karsiligidir.
    void promptGridManager();
    // Tekla Component Bridge: .mmbridge paket KLASORUNU sectirir ve
    // app_.importTeklaBridge() ile atomik import yapar (Windows GUI).
    void importTeklaBridge();
    // Tekla Component Bridge: .mmbridge paketini native tekla_shear_plate_146
    // component'ine donusturur (app_.convertTeklaBridgeToNative). Uye eksikse
    // kullaniciya kolon+kiriş secmesi bildirilir (Windows GUI).
    void importTeklaBridgeNative();
    // Tekla grid property pane karsiligi: grid spesifikasyonunu duzenler.
    bool editGridProperties(GridDefinition& grid, const QString& title);
    void createToolbar();
    // Adlandırılmış görünüm dialogları (menü + ribbon ortak kullanır).
    void promptSaveNamedView();
    void promptGotoNamedView();
    void promptDeleteNamedView();
    // Bolt tool penceresi (Phase B1): bir bulon display'ine cift tiklaninca
    // acilir. Cap/kalite/delik/bas/somun/pul + adet (countX/countY) + mesafe
    // (spacingX/spacingY) duzenlenir; OK -> app_.setSelectedBoltGroup.
    void openBoltPropertiesDialog(const mm::BoltGroup& bolt);
    // End Plate parametre diyaloğu (faz D1): uygulama-anı (komuttan önce) ve
    // sonradan düzenleme (çift tık) için ortak. current -> out; OK ise true.
    bool openEndPlateParamsDialog(const mm::EndPlateParams& current,
                                  mm::EndPlateParams& out);
    // Universal parametrik component (Fin Plate vb.) parametre diyaloğu: registry
    // parametre semasindan (CompParamSpec) tip-agnostik olarak uretilir.
    // OK -> app_.setSelectedComponentParams(rec.id, params) (recompute yolu).
    bool openComponentParamsDialog(const mm::ComponentInstanceRecord& rec);
    void createDockPanels();
    void resizeEmbeddedCanvas();
    void openTwoPointView(const ViewDefinition& view);
    void closeAllTwoPointViews();

    Application app_;
    QWidget* canvasContainer_{};
    QMdiArea* mdiArea_{};
    QMdiSubWindow* modelSubWindow_{}; // ana model gorunusu (kapatilamaz)
    int twoPointViewCounter_{};
    QComboBox* profileSelector_{};
    bool profileUiLogged_{};
    bool startupDefaultsApplied_{}; // GL + 3B + Solid bir kez, ilk show'da
    QMenu* appMenu_{}; // Tekla tarzi: tek kok menu (menubar yerine sol ust dugme)
};

} // namespace mm
