#pragma once

#include <QMainWindow>
#include <QMenuBar>
#include <QToolBar>
#include <QDockWidget>
#include <QAction>
#include <QWidget>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "model_maker/application.hpp"
#include "model_maker/command_registry.hpp"
#include "model_maker/ribbon_widget.hpp"

class QComboBox;
class RibbonWidget;
class QMdiArea;
class QMdiSubWindow;
class QMenu;

namespace mm {

class QtMainWindow : public QMainWindow, public ICommandHost {
    Q_OBJECT
public:
    explicit QtMainWindow(QWidget* parent = nullptr);
    ~QtMainWindow() override;

    Application& application() { return app_; }

    // ---- ICommandHost (UI-2): serit ile uygulama arasindaki ince arayuz ----
    void runCommand(const std::string& id) override;
    bool isEnabled(const std::string& key) const override;
    bool toggleState(const std::string& key) const override;
    std::vector<DynamicMenuItem> dynamicMenu(const std::string& id) const override;

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // TEK KAYNAK: komut tanimlari + serit/menu plani command_catalog.hpp'de.
    // Burada yalnizca ISLEYICI govdeleri (id -> lambda) ve Qt cizimi var.
    void buildCommandHandlers();
    bool runCommandById(const std::string& id);
    void createMenus();
    void fillMenu(QMenu* menu, const std::vector<PlanItem>& items);
    // Registry'deki Host teslimli kisayollari QShortcut olarak baglar
    // (odak paneldeyken de calissin). Canvas teslimliler Win32 yonlendiricisinde.
    void bindRegistryShortcuts();
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
    // Tekla Component Bridge: .mmbridge paketini KULLANICI KATALOGUNA sablon
    // olarak kaydeder (app_.saveBridgeAsCatalogTemplate). Kullaniciya sablon
    // adi sorulur; unsupported geometri icin REFERANS sablonu onayi istenir.
    void saveTeklaBridgeTemplate();
    // Tekla grid property pane karsiligi: grid spesifikasyonunu duzenler.
    bool editGridProperties(GridDefinition& grid, const QString& title);
    void createToolbar();
    // Adlandırılmış görünüm dialogları (menü + ribbon ortak kullanır).
    void promptSaveNamedView();
    void promptGotoNamedView();
    void promptDeleteNamedView();
    // TEKLA "work plane parallel to XYZ plane": duzlem (XY/XZ/YZ) + derinlik
    // sorar; OK -> app_.setWorkPlaneParallelXYZ(plane, depth).
    void promptWorkPlaneParallelXYZ();
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
    // MM-7 item 2: CHECK ihlaliyle reddedilen generic uretim icin diyalog acar.
    // Girdi Application::componentInputForParts ile AYNI kanonik yoldan kurulur;
    // diyalog checks[] tablosunu kirmizi gosterir, OK yalniz tum kontroller
    // gecince acilir. OK -> applyComponentFromParts (fail-closed burada kalir).
    bool openRejectedComponentDialog(const std::wstring& typeId,
                                     const std::vector<std::size_t>& parts,
                                     const std::vector<mm::CompParam>& params);
    // KAYNAK (Weld) komutu + diyalogu (Phase W2): tip/olcu/uzunluk/shop-site
    // secilir; OK -> app_.beginWeldCommand. Escape/Dialog iptali komut baslatmaz.
    bool openWeldDialog(mm::WeldType& type, double& size, double& length, mm::WeldClass& cls);
    void startWeldCommand();
    void createDockPanels();
    void resizeEmbeddedCanvas();
    void openTwoPointView(const ViewDefinition& view);
    void closeAllTwoPointViews();
    // Ctrl+Tab / Ctrl+Shift+Tab: olusturma sirasina gore alt pencere gecisi.
    void cycleViewWindow(int direction);

    Application app_;
    QWidget* canvasContainer_{};
    QMdiArea* mdiArea_{};
    QMdiSubWindow* modelSubWindow_{}; // ana model gorunusu (kapatilamaz)
    int twoPointViewCounter_{};
    QComboBox* profileSelector_{};
    bool profileUiLogged_{};
    bool startupDefaultsApplied_{}; // GL + 3B + Solid bir kez, ilk show'da
    QMenu* appMenu_{}; // Tekla tarzi: tek kok menu (menubar yerine sol ust dugme)
    // UI-2: komut kaydi + serit
    CommandCatalog commandCatalog_;
    std::map<std::string, std::function<void()>> commandHandlers_;
    RibbonWidget* ribbon_{};
    QDockWidget* modelTreeDock_{};
};

} // namespace mm
