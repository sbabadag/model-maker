#include "model_maker/application.hpp"
#include "model_maker/qt_main_window.hpp"
#include "model_maker/force_diagram.hpp"

#include <QApplication>
#include <QGuiApplication>
#include <windows.h>

#include <cstdio>

static void milestone(const char* message) {
    FILE* log = fopen("model-maker-startup.log", "a");
    if (log) { fprintf(log, "%s\n", message); fclose(log); }
}

static LONG WINAPI crashFilter(EXCEPTION_POINTERS* info) {
    FILE* log = fopen("model-maker-crash.log", "w");
    if (log) {
        fprintf(log, "code=0x%08lX addr=%p\n",
                static_cast<unsigned long>(info->ExceptionRecord->ExceptionCode),
                info->ExceptionRecord->ExceptionAddress);
        fclose(log);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

int main(int argc, char* argv[]) {
    SetUnhandledExceptionFilter(crashFilter);
    milestone("main-start");
    try {
        // HYBRID DPI DUZELTMESI: Qt6 kabugu + gomulu Win32 GDI canvas ayni
        // pencerede yasar. Qt6 varsayilan yuksek-DPI olceklemesi MANTIKSAL
        // (logical) piksel bildirirken native Win32 canvas FIZIKSEL piksel
        // kullanir. 125% ekranda oran 1.25 olur: resizeEmbeddedCanvas tuvali
        // GetClientRect ile FIZIKSEL boyuta ayarlar, ama Qt mantiksal boyut
        // uzerinden yerlesir -> model yanlis olcekte / "yarim pencere".
        // QT_SCALE_FACTOR=1 Qt'yi 1:1 fiziksel piksele ceker; surec zaten
        // Per-Monitor-V2 farkinda oldugu icin Windows bitmap-uzatma YAPMAZ,
        // goruntu keskin kalir ve iki koordinat uzayi birebir eslesir.
        qputenv("QT_SCALE_FACTOR", "1");
        QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
            Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

        HINSTANCE instance = GetModuleHandleW(nullptr);
        mm::registerForceDiagramClass(instance);

        QApplication app(argc, argv);
        app.setStyle("Fusion");

        // Dark palette
        QPalette darkPalette;
        darkPalette.setColor(QPalette::Window, QColor(45, 45, 48));
        darkPalette.setColor(QPalette::WindowText, QColor(220, 220, 220));
        darkPalette.setColor(QPalette::Base, QColor(30, 30, 30));
        darkPalette.setColor(QPalette::AlternateBase, QColor(45, 45, 48));
        darkPalette.setColor(QPalette::ToolTipBase, QColor(45, 45, 48));
        darkPalette.setColor(QPalette::ToolTipText, QColor(220, 220, 220));
        darkPalette.setColor(QPalette::Text, QColor(220, 220, 220));
        darkPalette.setColor(QPalette::Button, QColor(58, 69, 85));
        darkPalette.setColor(QPalette::ButtonText, QColor(220, 220, 220));
        darkPalette.setColor(QPalette::BrightText, Qt::red);
        darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
        darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        darkPalette.setColor(QPalette::HighlightedText, Qt::black);
        app.setPalette(darkPalette);

        mm::QtMainWindow window;
        milestone("window-created");
        window.show();
        milestone("window-shown");
        const int result = app.exec();
        milestone("exec-exit");
        return result;
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "Model Maker - Fatal Error", MB_OK | MB_ICONERROR);
        return 1;
    }
}
