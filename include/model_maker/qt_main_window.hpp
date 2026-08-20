#pragma once

#include <QMainWindow>
#include <QMenuBar>
#include <QToolBar>
#include <QDockWidget>
#include <QAction>
#include <QWidget>

#include "model_maker/application.hpp"

class QComboBox;

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
    void createToolbar();
    void createDockPanels();
    void resizeEmbeddedCanvas();

    Application app_;
    QWidget* canvasContainer_{};
    QComboBox* profileSelector_{};
    bool profileUiLogged_{};
};

} // namespace mm
