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
    // Tekla grid property pane karsiligi: grid spesifikasyonunu duzenler.
    bool editGridProperties(GridDefinition& grid, const QString& title);
    void createToolbar();
    // Adlandırılmış görünüm dialogları (menü + ribbon ortak kullanır).
    void promptSaveNamedView();
    void promptGotoNamedView();
    void promptDeleteNamedView();
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
};

} // namespace mm
