// GT-1 (INBOX 6): GUI TEST MODU kosucusu — model-maker.exe --test-script.
//
// Senaryo adimlari GERCEK girdi yolu uzerinden uygulanir (tuval Win32 HWND'si,
// SendMessage ile WM_LBUTTONDOWN/UP, WM_MOUSEMOVE, WM_KEYDOWN, WM_CHAR) — ayni
// WndProc dali, ayni komut cozumlemesi (UI-2 kayit), ayni cizim kodu.
// Her adimdan sonra: tuval ekran goruntusu (step_%03d.png) + durum JSON'u
// (step_%03d.json). Bitince result.json; basari 0, hata/zaman asimi 1.
//
// Ekran yakalama: GDI ve GL kompoziti WM_PAINT'te AYNI pencerenin DC'sine
// yazilir (GL FBO'su AlphaBlend ile kompoze edilir) — bu yuzden tek BitBlt
// yolu iki backend'i de yakalar; kapali/ortulu pencere icin PrintWindow
// yedegi vardir.

#include "model_maker/gui_test_runner.hpp"

#include "model_maker/application.hpp"
#include "model_maker/camera.hpp"
#include "model_maker/document.hpp"
#include "model_maker/drafting.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/qt_main_window.hpp"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QEventLoop>

#include <windows.h>

#include <cstdio>

namespace mm {
namespace {

// ---------------------------------------------------------------- yardimcilar

QString toolName(DrawTool tool) {
    switch (tool) {
        case DrawTool::Line: return QStringLiteral("draw.line");
        case DrawTool::Polyline: return QStringLiteral("draw.polyline");
        case DrawTool::Rectangle: return QStringLiteral("draw.rectangle");
        case DrawTool::Circle: return QStringLiteral("draw.circle");
        case DrawTool::Face3D: return QStringLiteral("draw.face3d");
        case DrawTool::Column: return QStringLiteral("draw.column");
        case DrawTool::Beam: return QStringLiteral("draw.beam");
        case DrawTool::Divide: return QStringLiteral("draw.divide");
    }
    return QStringLiteral("none");
}

std::string modelKind(const WireframeModel& model) {
    if (!model.properties().profileName.empty()) return "profiled";
    if (model.isFace3D()) return "face3d";
    if (model.isPointEntity()) return "point";
    if (model.constructionKind() != ConstructionKind::None) return "construction";
    return "plain";
}

// Olay dongusu calismiyorken (app.exec() cagrilmadan) bekleyen isleri bosalt.
void pump(int ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (ms > 0) Sleep(static_cast<DWORD>(ms));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

// Tuval HWND'sini goruntuye alir: WM_PAINT zorla, pencere yuzeyinden BitBlt;
// basarisizsa (ortulu pencere vb.) PrintWindow yedegi.
bool captureHwnd(HWND hwnd, QImage& out) {
    if (!hwnd || !IsWindow(hwnd)) return false;
    RECT rc{};
    if (!GetClientRect(hwnd, &rc)) return false;
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return false;

    InvalidateRect(hwnd, nullptr, TRUE);
    UpdateWindow(hwnd);  // eszamanli WM_PAINT: yuzey guncel

    HDC winDc = GetDC(hwnd);
    if (!winDc) return false;
    HDC mem = CreateCompatibleDC(winDc);
    HBITMAP bmp = CreateCompatibleBitmap(winDc, w, h);
    if (!mem || !bmp) {
        if (mem) DeleteDC(mem);
        if (bmp) DeleteObject(bmp);
        ReleaseDC(hwnd, winDc);
        return false;
    }
    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    if (!BitBlt(mem, 0, 0, w, h, winDc, 0, 0, SRCCOPY)) {
        // DWM kompozit yedegi: pencere kismen kapali/ortuluyken bile icerik alir.
        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);
        bmp = nullptr;
        mem = CreateCompatibleDC(winDc);
        bmp = CreateCompatibleBitmap(winDc, w, h);
        if (!mem || !bmp) {
            if (mem) DeleteDC(mem);
            if (bmp) DeleteObject(bmp);
            ReleaseDC(hwnd, winDc);
            return false;
        }
        oldBmp = SelectObject(mem, bmp);
        PrintWindow(hwnd, mem, PW_RENDERFULLCONTENT | PW_CLIENTONLY);
    }
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // yukaridan asagi: QImage satir duzeni
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    out = QImage(w, h, QImage::Format_ARGB32);
    const int lines =
        GetDIBits(winDc, bmp, 0, h, out.bits(), &bi, DIB_RGB_COLORS);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(hwnd, winDc);
    return lines == h;
}

// Isim -> sanal tus; harf ise WM_CHAR metni de doner.
bool keyToVk(const QString& name, WPARAM& vk, QString& chars) {
    const QString n = name.trimmed().toLower();
    if (n.isEmpty()) return false;
    if (n == QStringLiteral("esc") || n == QStringLiteral("escape")) vk = VK_ESCAPE;
    else if (n == QStringLiteral("return") || n == QStringLiteral("enter")) vk = VK_RETURN;
    else if (n == QStringLiteral("tab")) vk = VK_TAB;
    else if (n == QStringLiteral("delete") || n == QStringLiteral("del")) vk = VK_DELETE;
    else if (n == QStringLiteral("backspace")) vk = VK_BACK;
    else if (n == QStringLiteral("space")) { vk = VK_SPACE; chars = QStringLiteral(" "); }
    else if (n.startsWith(QLatin1Char('f')) && n.size() <= 3) {
        bool ok = false;
        const int idx = n.mid(1).toInt(&ok);
        if (!ok || idx < 1 || idx > 24) return false;
        vk = VK_F1 + static_cast<WPARAM>(idx - 1);
    } else if (n.size() == 1) {
        const QChar c = name.at(0);
        chars = name;
        const SHORT scan = VkKeyScanW(c.unicode());
        if (scan == -1) return false;
        vk = static_cast<WPARAM>(scan & 0xFF);
    } else {
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- kosucu

struct Runner {
    QtMainWindow& window;
    QString outDir;
    qint64 timeoutMs = 60000;
    QElapsedTimer watch;
    bool ok = true;
    QString error;
    int stepIndex = 0;

    Runner(QtMainWindow& w, QString dir) : window(w), outDir(std::move(dir)) {}

    bool fail(const QString& why) {
        ok = false;
        error = why;
        return false;
    }

    void log(const QString& line) {
        QFile f(outDir + QStringLiteral("/runner.log"));
        if (f.open(QIODevice::Append | QIODevice::Text))
            f.write((line + QLatin1Char('\n')).toUtf8());
    }

    // --- durum JSON'u (GT-1 spesifikasyonundaki alanlar) ---
    QJsonObject state() {
        Application& app = window.application();
        QJsonObject out;

        QJsonObject selection;
        QJsonArray selectionTypes;
        for (std::size_t index : app.selectedModelIndices()) {
            if (index < app.document().models().size())
                selectionTypes.append(
                    QString::fromStdString(modelKind(app.document().models()[index])));
        }
        selection[QStringLiteral("count")] =
            static_cast<int>(app.selectedModelIndices().size());
        selection[QStringLiteral("types")] = selectionTypes;
        out[QStringLiteral("selection")] = selection;

        out[QStringLiteral("activeTool")] = toolName(app.activeTool());

        int profiled = 0, plain = 0, face3d = 0, point = 0, construction = 0;
        QJsonArray visible;
        const auto& models = app.document().models();
        for (std::size_t i = 0; i < models.size(); ++i) {
            const std::string kind = modelKind(models[i]);
            if (kind == "profiled") ++profiled;
            else if (kind == "face3d") ++face3d;
            else if (kind == "point") ++point;
            else if (kind == "construction") ++construction;
            else ++plain;
            if (models[i].properties().visible && !models[i].properties().frozen)
                visible.append(static_cast<int>(i));
        }
        QJsonObject modelState;
        modelState[QStringLiteral("count")] = static_cast<int>(models.size());
        modelState[QStringLiteral("profiled")] = profiled;
        modelState[QStringLiteral("plain")] = plain;
        modelState[QStringLiteral("face3d")] = face3d;
        modelState[QStringLiteral("point")] = point;
        modelState[QStringLiteral("construction")] = construction;
        modelState[QStringLiteral("visible")] = visible;
        out[QStringLiteral("models")] = modelState;

        out[QStringLiteral("viewports")] = window.viewportCount();

        QJsonObject quickSearch;
        quickSearch[QStringLiteral("text")] = window.quickSearchText();
        QJsonArray suggestions;
        for (const QString& s :
             window.quickSearchSuggestions(window.quickSearchText()))
            suggestions.append(s);
        quickSearch[QStringLiteral("suggestions")] = suggestions;
        out[QStringLiteral("quickSearch")] = quickSearch;

        QJsonArray catalog;
        for (const DynamicMenuItem& item : window.dynamicMenu(QStringLiteral("steel.catalog").toStdString())) {
            QJsonObject entry;
            entry[QStringLiteral("id")] = QString::fromStdString(item.id);
            entry[QStringLiteral("label")] = QString::fromUtf8(item.label.c_str());
            catalog.append(entry);
        }
        out[QStringLiteral("catalog")] = catalog;

        const Camera& camera = app.camera();
        QJsonObject cameraState;
        cameraState[QStringLiteral("yaw")] = camera.yaw();
        cameraState[QStringLiteral("pitch")] = camera.pitch();
        cameraState[QStringLiteral("roll")] = camera.roll();
        cameraState[QStringLiteral("zoom")] = camera.zoom();
        QJsonArray rotation;
        for (double value : camera.rotationMatrix()) rotation.append(value);
        cameraState[QStringLiteral("rotation")] = rotation;
        out[QStringLiteral("camera")] = cameraState;

        out[QStringLiteral("backend")] =
            app.gpuLinesEnabled() ? QStringLiteral("gpu") : QStringLiteral("gdi");
        return out;
    }

    bool writeStep(const QJsonObject& action, const QString& actionName) {
        const int index = stepIndex;
        const QString number =
            QStringLiteral("%1").arg(index, 3, 10, QLatin1Char('0'));
        QImage image;
        if (!captureHwnd(window.application().canvasHandle(), image)) {
            image = window.grab().toImage();  // Qt penceresi yedegi
        }
        if (image.isNull()) return fail(QStringLiteral("adım %1: ekran görüntüsü alınamadı").arg(index));
        image.save(outDir + QStringLiteral("/step_%1.png").arg(number));

        QJsonObject status = state();
        status[QStringLiteral("step")] = index;
        status[QStringLiteral("action")] = actionName;
        status[QStringLiteral("actionData")] = action;
        QFile file(outDir + QStringLiteral("/step_%1.json").arg(number));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return fail(QStringLiteral("adım %1: durum dosyası yazılamadı").arg(index));
        file.write(QJsonDocument(status).toJson(QJsonDocument::Indented));
        ++stepIndex;
        return true;
    }

    // --- girdi enjeksiyonu (gercek WndProc yolu) ---
    void sendMouse(UINT message, WPARAM wParam, int x, int y) {
        HWND canvas = window.application().canvasHandle();
        SendMessageW(canvas, message, wParam, MAKELPARAM(static_cast<WORD>(x),
                                                         static_cast<WORD>(y)));
    }

    HWND focused() const {
        HWND focus = GetFocus();
        return focus ? focus : window.application().canvasHandle();
    }

    bool execClick(int x, int y) {
        sendMouse(WM_MOUSEMOVE, 0, x, y);
        sendMouse(WM_LBUTTONDOWN, MK_LBUTTON, x, y);
        sendMouse(WM_LBUTTONUP, 0, x, y);
        pump(40);
        return true;
    }

    bool execDrag(int x1, int y1, int x2, int y2) {
        sendMouse(WM_LBUTTONDOWN, MK_LBUTTON, x1, y1);
        const int steps = 24;
        for (int i = 1; i <= steps; ++i) {
            const int x = x1 + (x2 - x1) * i / steps;
            const int y = y1 + (y2 - y1) * i / steps;
            sendMouse(WM_MOUSEMOVE, MK_LBUTTON, x, y);
            pump(8);
        }
        sendMouse(WM_LBUTTONUP, 0, x2, y2);
        pump(40);
        return true;
    }

    bool execKey(const QString& name) {
        WPARAM vk = 0;
        QString chars;
        if (!keyToVk(name, vk, chars))
            return fail(QStringLiteral("bilinmeyen tuş: %1").arg(name));
        const HWND target = focused();
        SendMessageW(target, WM_KEYDOWN, vk, 0);
        for (const QChar c : chars)
            SendMessageW(target, WM_CHAR, static_cast<WPARAM>(c.unicode()), 0);
        SendMessageW(target, WM_KEYUP, vk, 0);
        pump(40);
        return true;
    }

    bool execType(const QString& text) {
        const HWND target = focused();
        for (const QChar c : text)
            SendMessageW(target, WM_CHAR, static_cast<WPARAM>(c.unicode()), 0);
        pump(40);
        return true;
    }

    bool execExpect(const QJsonObject& expect) {
        Application& app = window.application();
        QStringList problems;
        if (expect.contains(QStringLiteral("models")) &&
            app.document().models().size() !=
                static_cast<std::size_t>(expect[QStringLiteral("models")].toInt(-1)))
            problems << QStringLiteral("models=%1 (beklenen %2)")
                            .arg(app.document().models().size())
                            .arg(expect[QStringLiteral("models")].toInt(-1));
        if (expect.contains(QStringLiteral("selection")) &&
            app.selectedModelIndices().size() !=
                static_cast<std::size_t>(expect[QStringLiteral("selection")].toInt(-1)))
            problems << QStringLiteral("selection=%1 (beklenen %2)")
                            .arg(app.selectedModelIndices().size())
                            .arg(expect[QStringLiteral("selection")].toInt(-1));
        if (expect.contains(QStringLiteral("viewports")) &&
            window.viewportCount() != expect[QStringLiteral("viewports")].toInt(-1))
            problems << QStringLiteral("viewports=%1 (beklenen %2)")
                            .arg(window.viewportCount())
                            .arg(expect[QStringLiteral("viewports")].toInt(-1));
        if (expect.contains(QStringLiteral("activeTool")) &&
            toolName(app.activeTool()) != expect[QStringLiteral("activeTool")].toString())
            problems << QStringLiteral("activeTool=%1 (beklenen %2)")
                            .arg(toolName(app.activeTool()),
                                 expect[QStringLiteral("activeTool")].toString());
        if (expect.contains(QStringLiteral("backend")) &&
            (app.gpuLinesEnabled() ? QStringLiteral("gpu") : QStringLiteral("gdi")) !=
                expect[QStringLiteral("backend")].toString())
            problems << QStringLiteral("backend=%1 (beklenen %2)")
                            .arg(app.gpuLinesEnabled() ? QStringLiteral("gpu")
                                                       : QStringLiteral("gdi"),
                                 expect[QStringLiteral("backend")].toString());
        if (!problems.isEmpty())
            return fail(QStringLiteral("expect başarısız: ") + problems.join(QStringLiteral("; ")));
        return true;
    }

    bool execStep(const QJsonObject& step) {
        if (watch.elapsed() > timeoutMs)
            return fail(QStringLiteral("zaman aşımı (%1 ms)").arg(timeoutMs));

        if (step.contains(QStringLiteral("cmd"))) {
            const QString id = step[QStringLiteral("cmd")].toString();
            window.runCommand(id.toStdString());
            pump(60);
            return writeStep(step, QStringLiteral("cmd:") + id);
        }
        if (step.contains(QStringLiteral("click"))) {
            const QJsonArray point = step[QStringLiteral("click")].toArray();
            if (point.size() < 2) return fail(QStringLiteral("click: [x, y] bekleniyor"));
            execClick(point[0].toInt(), point[1].toInt());
            return writeStep(step, QStringLiteral("click"));
        }
        if (step.contains(QStringLiteral("drag"))) {
            const QJsonArray rect = step[QStringLiteral("drag")].toArray();
            if (rect.size() < 4) return fail(QStringLiteral("drag: [x1, y1, x2, y2] bekleniyor"));
            execDrag(rect[0].toInt(), rect[1].toInt(), rect[2].toInt(), rect[3].toInt());
            return writeStep(step, QStringLiteral("drag"));
        }
        if (step.contains(QStringLiteral("key"))) {
            if (!execKey(step[QStringLiteral("key")].toString())) return false;
            return writeStep(step, QStringLiteral("key:") + step[QStringLiteral("key")].toString());
        }
        if (step.contains(QStringLiteral("type"))) {
            execType(step[QStringLiteral("type")].toString());
            return writeStep(step, QStringLiteral("type:") + step[QStringLiteral("type")].toString());
        }
        if (step.contains(QStringLiteral("wait"))) {
            pump(step[QStringLiteral("wait")].toInt(50));
            return writeStep(step, QStringLiteral("wait"));
        }
        if (step.contains(QStringLiteral("open"))) {
            QString path = step[QStringLiteral("open")].toString();
            const QFileInfo info(path);
            if (info.isRelative()) {
                // Senaryo dosyasinin klasorune gore cozulur.
                const QFileInfo scriptDir(scriptDirPath_);
                path = scriptDir.dir().filePath(path);
            }
            Application& app = window.application();
            if (!app.openDocumentPath(path.toStdWString()))
                return fail(QStringLiteral("belge açılamadı: %1").arg(path));
            pump(120);
            return writeStep(step, QStringLiteral("open:") + path);
        }
        if (step.contains(QStringLiteral("profile"))) {
            window.application().setPendingProfileName(
                step[QStringLiteral("profile")].toString().toStdString());
            pump(40);
            return writeStep(step, QStringLiteral("profile:") + step[QStringLiteral("profile")].toString());
        }
        if (step.contains(QStringLiteral("expect"))) {
            if (!execExpect(step[QStringLiteral("expect")].toObject())) return false;
            return writeStep(step, QStringLiteral("expect"));
        }
        return fail(QStringLiteral("bilinmeyen adım: %1")
                        .arg(QString::fromUtf8(QJsonDocument(step).toJson(QJsonDocument::Compact))));
    }

    QString scriptDirPath_;
};

}  // namespace

int runGuiTestScript(QtMainWindow& window, const QString& scriptPath,
                     const QString& outDir) {
    QFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::ReadOnly)) {
        fprintf(stderr, "gt1: senaryo acilamadi: %s\n", scriptPath.toUtf8().constData());
        return 1;
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(scriptFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        fprintf(stderr, "gt1: senaryo JSON hatasi (%d): %s\n", parseError.error,
                parseError.errorString().toUtf8().constData());
        return 1;
    }
    const QJsonObject root = doc.object();
    const QJsonArray steps = root[QStringLiteral("steps")].toArray();
    if (steps.isEmpty()) {
        fprintf(stderr, "gt1: senaryoda adim yok\n");
        return 1;
    }

    const QString actualOut =
        outDir.isEmpty() ? QStringLiteral("gt1-out") : outDir;
    if (!QDir().mkpath(actualOut)) {
        fprintf(stderr, "gt1: cikti klasoru olusturulamadi: %s\n",
                actualOut.toUtf8().constData());
        return 1;
    }

    Runner runner(window, actualOut);
    runner.scriptDirPath_ = scriptPath;
    runner.timeoutMs = root[QStringLiteral("timeoutMs")].toDouble(60000.0);
    runner.watch.start();

    // Backend secimi: senaryo gpu/gdi isterse baslangicta o yola gec.
    // SIRA KRITIK: once pump — showEvent ilk boyamada startup varsayilanlarini
    // (GL + 3B + Solid) uygular; backend zorlamasi ONDAN SONRA yapilmali,
    // yoksa acilis GL'si senaryonun secimini ezer.
    Application& app = window.application();
    pump(120);
    const QString backend = root[QStringLiteral("backend")].toString(QStringLiteral("auto"));
    if (backend == QStringLiteral("gpu") && !app.gpuLinesEnabled()) app.toggleGpuLines();
    else if (backend == QStringLiteral("gdi") && app.gpuLinesEnabled()) app.toggleGpuLines();
    pump(120);  // backend gecisi yerlessin

    runner.log(QStringLiteral("GT-1 basladi: %1 (%2 adim)")
                   .arg(scriptPath)
                   .arg(steps.size()));

    int index = 0;
    for (const QJsonValue& value : steps) {
        const QJsonObject step = value.toObject();
        if (!runner.execStep(step)) {
            runner.log(QStringLiteral("ADIM %1 HATA: %2").arg(index).arg(runner.error));
            break;
        }
        ++index;
    }

    QJsonObject result;
    result[QStringLiteral("ok")] = runner.ok;
    result[QStringLiteral("steps")] = runner.stepIndex;
    result[QStringLiteral("error")] = runner.error;
    result[QStringLiteral("script")] = scriptPath;
    QFile resultFile(actualOut + QStringLiteral("/result.json"));
    if (resultFile.open(QIODevice::WriteOnly | QIODevice::Text))
        resultFile.write(QJsonDocument(result).toJson(QJsonDocument::Indented));

    runner.log(runner.ok ? QStringLiteral("GT-1 OK (%1 adim)").arg(runner.stepIndex)
                         : QStringLiteral("GT-1 HATA: %1").arg(runner.error));
    // "Bitince kendiliginden kapanir; hata/zaman asiminda cikis kodu != 0."
    window.close();
    return runner.ok ? 0 : 1;
}

}  // namespace mm
