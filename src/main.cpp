#include "model_maker/application.hpp"
#include "model_maker/qt_main_window.hpp"
#include "model_maker/force_diagram.hpp"

#include <QApplication>
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
        // HYBRID DPI DUZELTMESI (kok neden): Qt6 kabugu + gomulu Win32 GDI
        // canvas ayni surecte yasar. Qt host pencereleri Per-Monitor-V2
        // farkindadir ve GetClientRect FIZIKSEL piksel doner; ama application
        // .cpp'nin CreateWindowExW ile yarattigi Win32 canvas HWND'si ayni
        // farkindaligi miras almazsa GetClientRect MANTIKSAL (virtualized)
        // piksel doner. resizeEmbeddedCanvas tuvali FIZIKSEL boyuta ayarlar,
        // projeksiyon ise tuvalin MANTIKSAL olcusunu okur -> cizim tuvalin
        // kose/bolgesinde kalir ("yarim pencere"). Cozum: sureci EN BASTA
        // (herhangi bir pencere yaratilmadan once) Per-Monitor-V2 farkinda
        // yap — Qt ve Win32 canvas ayni (fiziksel) koordinat uzayini kullanir.
        // QT_SCALE_FACTOR=1 denemesi kaldirildi: yalniz Qt'yi 1:1'e ceker,
        // Win32 canvas'in GetClientRect'ini DEGISTIRMEZ ve 125% ekranda
        // arayuzu kucultup kotu gosterir.
        if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
            // Zaten ayarlanmis surec (Qt daha once PMv2 kurmus olabilir) ya da
            // cok eski Windows: PMv2 kurulamadiysa bile uygulama calisir; yalniz
            // DPI uyusmazligi riski kalir. (SetProcessDpiAwareness yedegi
            // SHCore bagimliligi gerektirdigi icin kasitli olarak yok.)
        }

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
