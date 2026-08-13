#pragma once

#include <QMainWindow>
#include <QMenuBar>
#include <QToolBar>
#include <QDockWidget>
#include <QAction>
#include <QWidget>

#include "model_maker/application.hpp"

namespace mm {

class QtMainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit QtMainWindow(QWidget* parent = nullptr);
    ~QtMainWindow() override;

    Application& application() { return app_; }

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void createMenus();
    void createToolbar();
    void createDockPanels();

    Application app_;
    QWidget* canvasContainer_{};
};

} // namespace mm
