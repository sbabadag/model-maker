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
#include "model_maker/report.hpp"
#include "model_maker/camera.hpp"
#include "model_maker/component_catalog.hpp"
#include "model_maker/component_dialog.hpp"   // P-1/5: componentDialogSchema
#include "model_maker/component_engine.hpp"
#include "model_maker/document.hpp"
#include "model_maker/drafting.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/view_cube.hpp"
#include "model_maker/qt_main_window.hpp"
#include "model_maker/properties_panel.hpp"
#include "model_maker/profile_picker_dialog.hpp"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QTreeWidget>

#include <windows.h>

#include <cstdio>

#include <algorithm>
#include <cmath>

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

// H-8: component instance'inin GEOMETRIK digest'i — roleKey + bagli uye
// eksenleri + parca modellerinin koseleri (FNV-1a 64). EntityId'den
// bagimsizdir (MM-2b): kaydet-ac ve yeniden uretim sonrasi yalnizca
// GERCEK geometri degisirse digest degisir. Deterministik kayit sirasi.
QString componentGeometricDigest(Application& app, const ComponentInstanceRecord& rec) {
    std::uint64_t fnv = 14695981039346656037ULL;
    const auto feedBytes = [&](const void* data, std::size_t n) {
        const auto* b = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < n; ++i) {
            fnv ^= b[i];
            fnv *= 1099511628211ULL;
        }
    };
    const auto feedD = [&](double v) { feedBytes(&v, sizeof v); };
    const auto feedS = [&](const std::string& s) { feedBytes(s.data(), s.size()); };
    const auto feedModel = [&](EntityId eid) {
        const auto mi = app.document().indexOfEntity(eid);
        if (!mi || *mi >= app.document().models().size()) return;
        const auto& m = app.document().models()[*mi];
        for (const Vec3& v : m.vertices()) {
            feedD(v.x); feedD(v.y); feedD(v.z);
        }
        const auto& p = m.properties();
        feedD(p.axisFromX); feedD(p.axisFromY); feedD(p.axisFromZ);
        feedD(p.axisToX); feedD(p.axisToY); feedD(p.axisToZ);
    };
    feedS("main");
    feedModel(rec.mainPart);
    feedS("sec");
    for (const EntityId sid : rec.secondaryParts) feedModel(sid);
    feedS("children");
    for (const ComponentChildRecord& c : rec.children) {
        feedS(c.roleKey);
        feedBytes(&c.kind, sizeof c.kind);
        feedModel(c.entityId);
    }
    feedS("plates");
    for (const EntityId pid : rec.ownedPlateIds) feedModel(pid);
    char buf[24];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(fnv));
    return QString::fromLatin1(buf);
}

// Olay dongusu calismiyorken (app.exec() cagrilmadan) bekleyen isleri bosalt.
void pump(int ms) {
    // Zaman tabanli bekleme: Sleep tek basina mesaj islemez, Qt zamanlayicilari
    // (gecis animasyonu ~180 ms) ilerlemez. Bu yuzden dongu halinde isle.
    const DWORD start = GetTickCount();
    for (;;) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (ms <= 0) break;
        if (GetTickCount() - start >= static_cast<DWORD>(ms)) break;
        Sleep(5);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

// Tuval HWND'sini goruntuye alir: WM_PAINT zorla, pencere yuzeyinden BitBlt;
// basarisizsa (ortulu pencere vb.) PrintWindow yedegi.
// forcePrintWindow=true: BitBlt atlanir, dogrudan PrintWindow (DWM yedek
// yuzeyinden CIZILI COCUKLARI DA alir — ornegin MDI alt pencereleri).
bool captureHwnd(HWND hwnd, QImage& out, bool forcePrintWindow = false) {
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
    if (forcePrintWindow ||
        !BitBlt(mem, 0, 0, w, h, winDc, 0, 0, SRCCOPY)) {
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

// H-6: yuzeyi OLDUGU GIBI yakala (WM_PAINT zorlamadan, BitBlt yalniz).
// Motion feedback (snap marker + soluk iz) yalniz interaktif karelerde
// cizilir; writeStep'in zorla tam karesi bunlari sildigi icin canli
// yakalama gerekli.
bool captureHwndLive(HWND hwnd, QImage& out) {
    if (!hwnd || !IsWindow(hwnd)) return false;
    RECT rc{};
    if (!GetClientRect(hwnd, &rc)) return false;
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return false;
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
    const bool ok = BitBlt(mem, 0, 0, w, h, winDc, 0, 0, SRCCOPY) != FALSE;
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
    return ok && lines == h;
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
    // H-8: component geometric digest hafizasi (remember/equals/notEquals).
    QHash<QString, QString> digestMemory_;

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

        // H-6: tutulan snap + soluk (silan) iz durumu — zamanlama kaniti icin.
        QJsonObject snap;
        snap[QStringLiteral("held")] = app.heldSnapActive();
        snap[QStringLiteral("heldType")] =
            QString::fromWCharArray(snapTypeLabel(app.heldSnapTypeValue()));
        snap[QStringLiteral("fading")] = app.fadingSnapVisible();
        snap[QStringLiteral("fadingType")] =
            QString::fromWCharArray(snapTypeLabel(app.fadingSnapTypeValue()));
        out[QStringLiteral("snap")] = snap;

        out[QStringLiteral("activeTool")] = toolName(app.activeTool());
        // Kullaniciya gorunen durum cubugu metni (publishStatus yolu) — tanida
        // adim adim ne oldu/olmadigini soyler.
        out[QStringLiteral("status")] = window.statusBar() && window.statusBar()->currentMessage().size()
                                             ? window.statusBar()->currentMessage()
                                             : QStringLiteral("Hazır");

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
        int profiled = 0, plain = 0, face3d = 0, point = 0, construction = 0,
            visibleCount = 0;
        for (const WireframeModel& model : app.document().models()) {
            const std::string kind = modelKind(model);
            if (kind == "profiled") ++profiled;
            else if (kind == "face3d") ++face3d;
            else if (kind == "point") ++point;
            else if (kind == "construction") ++construction;
            else ++plain;
            if (model.properties().visible && !model.properties().frozen)
                ++visibleCount;
        }
        // "models": sayi (geriye uyumlu) ya da nesne {count, profiled, plain,
        // face3d, point, construction, visible} — alt tip kontrolu icin.
        const QJsonValue modelsValue = expect[QStringLiteral("models")];
        if (modelsValue.isDouble()) {
            if (app.document().models().size() !=
                static_cast<std::size_t>(modelsValue.toInt(-1)))
                problems << QStringLiteral("models=%1 (beklenen %2)")
                                .arg(app.document().models().size())
                                .arg(modelsValue.toInt(-1));
        } else if (modelsValue.isObject()) {
            const QJsonObject expected = modelsValue.toObject();
            const auto checkField = [&](const char* field, int actual) {
                if (expected.contains(QString::fromLatin1(field)) &&
                    actual != expected[QString::fromLatin1(field)].toInt(-1))
                    problems << QStringLiteral("models.%1=%2 (beklenen %3)")
                                    .arg(QString::fromLatin1(field))
                                    .arg(actual)
                                    .arg(expected[QString::fromLatin1(field)].toInt(-1));
            };
            checkField("count", static_cast<int>(app.document().models().size()));
            checkField("profiled", profiled);
            checkField("plain", plain);
            checkField("face3d", face3d);
            checkField("point", point);
            checkField("construction", construction);
            checkField("visible", visibleCount);
        }
        if (expect.contains(QStringLiteral("visibleCount")) &&
            visibleCount != expect[QStringLiteral("visibleCount")].toInt(-1))
            problems << QStringLiteral("visibleCount=%1 (beklenen %2)")
                            .arg(visibleCount)
                            .arg(expect[QStringLiteral("visibleCount")].toInt(-1));
        if (expect.contains(QStringLiteral("selection")) &&
            app.selectedModelIndices().size() !=
                static_cast<std::size_t>(expect[QStringLiteral("selection")].toInt(-1)))
            problems << QStringLiteral("selection=%1 (beklenen %2)")
                            .arg(app.selectedModelIndices().size())
                            .arg(expect[QStringLiteral("selection")].toInt(-1));
        // "snapState": {held, fading, type} — H-6 sozlesmesi: imlec
        // histerezis disina cikinca tutulan snap DERHAL gecersizlesir
        // (held=false, H-4), ama marker holdTimeMs boyunca soluk iz olarak
        // gorunur (fading=true); sure dolunca fading=false.
        if (expect.contains(QStringLiteral("snapState"))) {
            const QJsonObject expected = expect[QStringLiteral("snapState")].toObject();
            if (expected.contains(QStringLiteral("held")) &&
                app.heldSnapActive() != expected[QStringLiteral("held")].toBool())
                problems << QStringLiteral("snapState.held=%1 (beklenen %2)")
                                .arg(app.heldSnapActive())
                                .arg(expected[QStringLiteral("held")].toBool());
            if (expected.contains(QStringLiteral("fading")) &&
                app.fadingSnapVisible() !=
                    expected[QStringLiteral("fading")].toBool())
                problems << QStringLiteral("snapState.fading=%1 (beklenen %2)")
                                .arg(app.fadingSnapVisible())
                                .arg(expected[QStringLiteral("fading")].toBool());
            if (expected.contains(QStringLiteral("type"))) {
                const QString actual =
                    QString::fromWCharArray(snapTypeLabel(app.heldSnapTypeValue()));
                if (actual != expected[QStringLiteral("type")].toString())
                    problems << QStringLiteral("snapState.type=%1 (beklenen %2)")
                                    .arg(actual,
                                         expected[QStringLiteral("type")].toString());
            }
        }
        // B5: kup tiklamasi -> standart gorunus. Kamera aci cifti (yaw/pitch,
        // radyan) beklenen degere toleransla esit olmali; aci farki [-pi,pi]
        // araligina indirgenir (yaw ±pi civarinda sarmalanir).
        if (expect.contains(QStringLiteral("camera"))) {
            const QJsonObject expected = expect[QStringLiteral("camera")].toObject();
            const double tol = expected.contains(QStringLiteral("tol"))
                                   ? expected[QStringLiteral("tol")].toDouble()
                                   : 0.02;
            const auto angleDiff = [](double a, double b) {
                double d = a - b;
                while (d > 3.14159265358979323846) d -= 2.0 * 3.14159265358979323846;
                while (d < -3.14159265358979323846) d += 2.0 * 3.14159265358979323846;
                return std::abs(d);
            };
            if (expected.contains(QStringLiteral("yaw")))
                if (angleDiff(app.camera().yaw(), expected[QStringLiteral("yaw")].toDouble()) > tol)
                    problems << QStringLiteral("camera.yaw=%1 (beklenen %2)")
                                    .arg(app.camera().yaw())
                                    .arg(expected[QStringLiteral("yaw")].toDouble());
            if (expected.contains(QStringLiteral("pitch")))
                if (angleDiff(app.camera().pitch(),
                              expected[QStringLiteral("pitch")].toDouble()) > tol)
                    problems << QStringLiteral("camera.pitch=%1 (beklenen %2)")
                                    .arg(app.camera().pitch())
                                    .arg(expected[QStringLiteral("pitch")].toDouble());
        }
        if (expect.contains(QStringLiteral("viewports")) &&
            window.viewportCount() != expect[QStringLiteral("viewports")].toInt(-1))
            problems << QStringLiteral("viewports=%1 (beklenen %2)")
                            .arg(window.viewportCount())
                            .arg(expect[QStringLiteral("viewports")].toInt(-1));
        // H-5: son acilan gorunumun basligi + tanimi. Yuzey gorunusunde baslik
        // "yüzey görünüşü, derinlik …" olmali (yatay yuzler plan sanilmasin);
        // origin Z = yuzey kotu; normal Z yatayligi ayirt eder (yan yuz ->
        // dikey duzlem, |normal.z| ≈ 0).
        if (expect.contains(QStringLiteral("viewTitle"))) {
            const QJsonObject expected = expect[QStringLiteral("viewTitle")].toObject();
            const QString title = window.activeViewWindowTitle();
            if (expected.contains(QStringLiteral("contains")) &&
                !title.contains(expected[QStringLiteral("contains")].toString()))
                problems << QStringLiteral("viewTitle=\"%1\" (\"%2\" icermiyor)")
                                .arg(title, expected[QStringLiteral("contains")].toString());
            if (expected.contains(QStringLiteral("notContains")) &&
                title.contains(expected[QStringLiteral("notContains")].toString()))
                problems << QStringLiteral("viewTitle=\"%1\" (\"%2\" iceriyor, olmamali)")
                                .arg(title, expected[QStringLiteral("notContains")].toString());
        }
        if (expect.contains(QStringLiteral("viewOriginZ"))) {
            const QJsonObject spec = expect[QStringLiteral("viewOriginZ")].toObject();
            const double value = spec[QStringLiteral("value")].toDouble();
            const double tol = spec[QStringLiteral("tol")].toDouble(1.0);
            const double actual = window.lastViewDefinition().origin.z;
            if (std::fabs(actual - value) > tol)
                problems << QStringLiteral("viewOriginZ=%1 (beklenen %2±%3)")
                                .arg(actual).arg(value).arg(tol);
        }
        if (expect.contains(QStringLiteral("viewNormalZ"))) {
            const QJsonObject spec = expect[QStringLiteral("viewNormalZ")].toObject();
            const double below = spec[QStringLiteral("absBelow")].toDouble(0.1);
            const double actual = std::fabs(window.lastViewDefinition().normal.z);
            if (!(actual < below))
                problems << QStringLiteral("viewNormalZ|=%1 (|.|<%2 bekleniyordu)")
                                .arg(actual).arg(below);
        }
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
        // "status": {contains: "..."} — durum cubugu metninin ALT DIZGE icermesi
        // (publishStatus/updateStatus yolu; H-1 "açıldı" kaniti gibi).
        // "startupLog": {contains, tailLines?} — KALICI teshis logu
        // (model-maker-startup.log). Durum cubugu sonradan ezilebildigi icin
        // uzun acilis raporlarinin (paket guncellendi / paket eksik) tek
        // guvenilir kaniti burasidir.
        if (expect.contains(QStringLiteral("startupLog"))) {
            const QJsonObject expected = expect[QStringLiteral("startupLog")].toObject();
            const QString needle = expected[QStringLiteral("contains")].toString();
            const QString logPath = QCoreApplication::applicationDirPath() +
                                    QStringLiteral("/model-maker-startup.log");
            QFile lf(logPath);
            QString text;
            if (lf.open(QIODevice::ReadOnly | QIODevice::Text))
                text = QString::fromUtf8(lf.readAll());
            if (text.isEmpty())
                problems << QStringLiteral("startupLog: log okunamadi (%1)").arg(logPath);
            else if (!text.contains(needle))
                problems << QStringLiteral("startupLog \"%1\" icermiyor").arg(needle);
            else
                log(QStringLiteral("startupLog: \"%1\" bulundu").arg(needle));
        }
        if (expect.contains(QStringLiteral("status"))) {
            const QJsonObject expected = expect[QStringLiteral("status")].toObject();
            const QString current = window.statusBar() &&
                                            window.statusBar()->currentMessage().size()
                                        ? window.statusBar()->currentMessage()
                                        : QStringLiteral("Hazır");
            if (expected.contains(QStringLiteral("contains")) &&
                !current.contains(expected[QStringLiteral("contains")].toString()))
                problems << QStringLiteral("status=\"%1\" (\"%2\" icermiyor)")
                                .arg(current, expected[QStringLiteral("contains")].toString());
        }
        // "quickSearch": {text, has} — B1 sozlesmesi: arama kutusu metni ve
        // oneri listesi (searchSuggestions — completer'in kullandigi yol).
        if (expect.contains(QStringLiteral("quickSearch"))) {
            const QJsonObject expected = expect[QStringLiteral("quickSearch")].toObject();
            if (expected.contains(QStringLiteral("text")) &&
                window.quickSearchText() != expected[QStringLiteral("text")].toString())
                problems << QStringLiteral("quickSearch.text=\"%1\" (beklenen \"%2\")")
                                .arg(window.quickSearchText(),
                                     expected[QStringLiteral("text")].toString());
            if (expected.contains(QStringLiteral("has"))) {
                const QString needle = expected[QStringLiteral("has")].toString();
                const QStringList suggestions =
                    window.quickSearchSuggestions(window.quickSearchText());
                if (!suggestions.contains(needle))
                    problems << QStringLiteral("quickSearch onerilerinde \"%1\" yok: %2")
                                    .arg(needle, suggestions.join(QStringLiteral(" | ")));
            }
        }
        // "catalog": {count, minCount, has} — B1 sozlesmesi: Katalog acilir
        // icerigi (dynamicMenu("steel.catalog")); "has" etiket ALT DIZGESI arar.
        if (expect.contains(QStringLiteral("catalog"))) {
            const QJsonObject expected = expect[QStringLiteral("catalog")].toObject();
            const std::vector<DynamicMenuItem> items =
                window.dynamicMenu(QStringLiteral("steel.catalog").toStdString());
            if (expected.contains(QStringLiteral("count")) &&
                static_cast<int>(items.size()) != expected[QStringLiteral("count")].toInt(-1))
                problems << QStringLiteral("catalog=%1 oge (beklenen %2)")
                                .arg(items.size())
                                .arg(expected[QStringLiteral("count")].toInt(-1));
            if (expected.contains(QStringLiteral("minCount")) &&
                static_cast<int>(items.size()) < expected[QStringLiteral("minCount")].toInt(-1))
                problems << QStringLiteral("catalog=%1 oge (en az %2)")
                                .arg(items.size())
                                .arg(expected[QStringLiteral("minCount")].toInt(-1));
            if (expected.contains(QStringLiteral("has"))) {
                const QString needle = expected[QStringLiteral("has")].toString();
                bool found = false;
                for (const DynamicMenuItem& item : items)
                    if (QString::fromUtf8(item.label.c_str()).contains(needle)) found = true;
                if (!found)
                    problems << QStringLiteral("katalogda \"%1\" yok (%2 oge)")
                                    .arg(needle).arg(items.size());
            }
        }
        // H-8: modelin eksen uc noktalari (dunya koordinati) — "kolon tepesi
        // 7000'de mi", "mertek ucu kolon basliginda mi" kanitlari. Yalnizca
        // senaryoda VERILEN alanlar kontrol edilir.
        if (expect.contains(QStringLiteral("modelAxis"))) {
            const QJsonObject spec = expect[QStringLiteral("modelAxis")].toObject();
            const int model = spec[QStringLiteral("model")].toInt(-1);
            const double tol = spec[QStringLiteral("tol")].toDouble(1.0);
            if (model < 0 ||
                static_cast<std::size_t>(model) >= app.document().models().size())
                problems << QStringLiteral("modelAxis: model %1 yok").arg(model);
            else {
                const auto& p = app.document().models()[model].properties();
                const auto check = [&](const char* field, double actual) {
                    if (spec.contains(QString::fromLatin1(field)) &&
                        std::abs(actual - spec[QString::fromLatin1(field)].toDouble()) > tol)
                        problems << QStringLiteral("modelAxis.%1=%2 (beklenen ±%3)")
                                        .arg(QString::fromLatin1(field))
                                        .arg(actual, 0, 'f', 2)
                                        .arg(tol);
                };
                check("fromX", p.axisFromX);
                check("fromY", p.axisFromY);
                check("fromZ", p.axisFromZ);
                check("toX", p.axisToX);
                check("toY", p.axisToY);
                check("toZ", p.axisToZ);
            }
        }
        // H-8: modelin profil adi (mertek profili degisti mi / kolon degismedi mi).
        if (expect.contains(QStringLiteral("modelProfile"))) {
            const QJsonObject spec = expect[QStringLiteral("modelProfile")].toObject();
            const int model = spec[QStringLiteral("model")].toInt(-1);
            if (model < 0 ||
                static_cast<std::size_t>(model) >= app.document().models().size())
                problems << QStringLiteral("modelProfile: model %1 yok").arg(model);
            else {
                const QString actual = QString::fromStdString(
                    app.document().models()[model].properties().profileName);
                if (actual != spec[QStringLiteral("name")].toString())
                    problems << QStringLiteral("modelProfile[%1]=\"%2\" (beklenen \"%3\")")
                                    .arg(model)
                                    .arg(actual, spec[QStringLiteral("name")].toString());
            }
        }
        // UI-4: ozellikler paneli profil alani (ham metin + kirmizi durum).
        if (expect.contains(QStringLiteral("panelProfile"))) {
            const QJsonObject spec = expect[QStringLiteral("panelProfile")].toObject();
            PropertiesPanel* panel = window.propertiesPanel();
            if (!panel)
                problems << QStringLiteral("panelProfile: panel yok");
            else if (spec.contains(QStringLiteral("text"))) {
                const QString actual = panel->profileText();
                if (actual != spec[QStringLiteral("text")].toString())
                    problems << QStringLiteral("panelProfile=\"%1\" (beklenen \"%2\")")
                                    .arg(actual,
                                         spec[QStringLiteral("text")].toString());
            }
        }
        if (expect.contains(QStringLiteral("panelProfileValid"))) {
            const bool expected = expect[QStringLiteral("panelProfileValid")].toObject()
                                      [QStringLiteral("valid")].toBool();
            PropertiesPanel* panel = window.propertiesPanel();
            if (!panel)
                problems << QStringLiteral("panelProfileValid: panel yok");
            else if (panel->isProfileValid() != expected)
                problems << QStringLiteral("panelProfileValid=%1 (beklenen %2)")
                                .arg(panel->isProfileValid())
                                .arg(expected);
        }
        // UI-4: secim penceresinin secili profili.
        if (expect.contains(QStringLiteral("pickerProfile"))) {
            const QString expected = expect[QStringLiteral("pickerProfile")].toObject()
                                         [QStringLiteral("name")].toString();
            ProfilePickerDialog* dialog = window.profilePickerDialog();
            if (!dialog)
                problems << QStringLiteral("pickerProfile: pencere yok");
            else if (dialog->selectedProfile() != expected)
                problems << QStringLiteral("pickerProfile=\"%1\" (beklenen \"%2\")")
                                .arg(dialog->selectedProfile(), expected);
        }
        // P-2/3: BELGE SAYIMLARI — kesme modifier'i ve konstrüksiyon
        // duzlemlerinin sayisi + GORUNURLUGU (kopya ciziliyor mu?).
        if (expect.contains(QStringLiteral("document"))) {
            const QJsonObject spec = expect[QStringLiteral("document")].toObject();
            const mm::Document& doc = window.application().document();
            int planes = 0, visiblePlanes = 0;
            for (std::size_t i = 0; i < doc.models().size(); ++i) {
                if (doc.models()[i].constructionKind() != mm::ConstructionKind::Plane) continue;
                ++planes;
                if (doc.effectiveProperties(i).visible) ++visiblePlanes;
            }
            const int cuts = static_cast<int>(doc.cutModifiers().size());
            if (spec.contains(QStringLiteral("cutModifiers")) &&
                spec[QStringLiteral("cutModifiers")].toInt() != cuts)
                problems << QStringLiteral("document: kesme modifier sayisi %1 != %2")
                                .arg(cuts)
                                .arg(spec[QStringLiteral("cutModifiers")].toInt());
            if (spec.contains(QStringLiteral("planes")) &&
                spec[QStringLiteral("planes")].toInt() != planes)
                problems << QStringLiteral("document: duzlem sayisi %1 != %2")
                                .arg(planes)
                                .arg(spec[QStringLiteral("planes")].toInt());
            if (spec.contains(QStringLiteral("visiblePlanes")) &&
                spec[QStringLiteral("visiblePlanes")].toInt() != visiblePlanes)
                problems << QStringLiteral("document: GORUNUR duzlem sayisi %1 != %2")
                                .arg(visiblePlanes)
                                .arg(spec[QStringLiteral("visiblePlanes")].toInt());
        }
        // P-2/7: DOSYA ICERIK KONTROLU — senaryo urettigi rapor/dokum
        // dosyalarinda beklenen metinler var mi (malzeme listesi satirlari).
        if (expect.contains(QStringLiteral("fileContains"))) {
            const QJsonObject spec = expect[QStringLiteral("fileContains")].toObject();
            QString path = spec[QStringLiteral("path")].toString();
            if (!path.isEmpty()) {
                const QFileInfo fi(path);
                if (fi.isRelative()) path = outDir + QStringLiteral("/") + path;
                QFile f(path);
                if (!f.open(QIODevice::ReadOnly))
                    problems << QStringLiteral("fileContains: dosya yok (%1)").arg(path);
                else {
                    const QString text = QString::fromUtf8(f.readAll());
                    f.close();
                    for (const auto v : spec[QStringLiteral("contains")].toArray()) {
                        const QString needle = v.toString();
                        if (!text.contains(needle))
                            problems << QStringLiteral("fileContains: '%1' bulunamadi").arg(needle);
                    }
                    for (const auto v : spec[QStringLiteral("notContains")].toArray()) {
                        const QString needle = v.toString();
                        if (text.contains(needle))
                            problems << QStringLiteral("fileContains: '%1' OLMAMALI").arg(needle);
                    }
                }
            }
        }
        // H-8: tip eslesen component instance sayisi (haunch/end plate kuruldu mu).
        if (expect.contains(QStringLiteral("componentCount"))) {
            const QJsonObject spec = expect[QStringLiteral("componentCount")].toObject();
            const QString typeId = spec[QStringLiteral("typeId")].toString();
            int n = 0;
            for (const ComponentInstanceRecord& r : app.document().componentInstances())
                if (QString::fromStdWString(r.typeId) == typeId) ++n;
            if (n != spec[QStringLiteral("count")].toInt(-1))
                problems << QStringLiteral("componentCount[%1]=%2 (beklenen %3)")
                                .arg(typeId).arg(n).arg(spec[QStringLiteral("count")].toInt(-1));
        }
        // H-8: BAGLILIK — main/secondary uye eksen uclari arasindaki EN KISA
        // mesafe maxGap'i asmamali ("kolon mertekten ayrik" regresyonunu
        // dogrudan yakalar; kopuklukta bosluk metrelerce olur).
        if (expect.contains(QStringLiteral("componentAttached"))) {
            const QJsonObject spec = expect[QStringLiteral("componentAttached")].toObject();
            const QString typeId = spec[QStringLiteral("typeId")].toString();
            const double maxGap = spec[QStringLiteral("maxGap")].toDouble(500.0);
            int checked = 0;
            const auto gapMm = [&](const auto& mp, const auto& sp) {
                const auto d2 = [](double ax, double ay, double az,
                                   double bx, double by, double bz) {
                    const double dx = ax - bx, dy = ay - by, dz = az - bz;
                    return dx * dx + dy * dy + dz * dz;
                };
                const double g2 = std::min(
                    std::min(d2(sp.axisFromX, sp.axisFromY, sp.axisFromZ,
                                mp.axisFromX, mp.axisFromY, mp.axisFromZ),
                             d2(sp.axisFromX, sp.axisFromY, sp.axisFromZ,
                                mp.axisToX, mp.axisToY, mp.axisToZ)),
                    std::min(d2(sp.axisToX, sp.axisToY, sp.axisToZ,
                                mp.axisFromX, mp.axisFromY, mp.axisFromZ),
                             d2(sp.axisToX, sp.axisToY, sp.axisToZ,
                                mp.axisToX, mp.axisToY, mp.axisToZ)));
                return std::sqrt(g2);
            };
            for (const ComponentInstanceRecord& r : app.document().componentInstances()) {
                if (QString::fromStdWString(r.typeId) != typeId) continue;
                const auto mainIdx = app.document().indexOfEntity(r.mainPart);
                if (!mainIdx || *mainIdx >= app.document().models().size()) continue;
                const auto& mp = app.document().models()[*mainIdx].properties();
                for (const EntityId sid : r.secondaryParts) {
                    const auto si = app.document().indexOfEntity(sid);
                    if (!si || *si >= app.document().models().size()) continue;
                    const auto& sp = app.document().models()[*si].properties();
                    const double gap = gapMm(mp, sp);
                    ++checked;
                    if (gap > maxGap)
                        problems << QStringLiteral(
                                        "componentAttached[%1]: uyeler kopuk "
                                        "(bosluk %2 mm > %3)")
                                        .arg(typeId).arg(gap, 0, 'f', 1).arg(maxGap);
                }
            }
            if (checked == 0)
                problems << QStringLiteral("componentAttached: %1 instance yok").arg(typeId);
        }
        // H-8: component GEOMETRIK DIGEST kalkanı — {typeId, key, remember} |
        // {equals} | {notEquals}. remember kaydeder (sonraki expect'ler icin);
        // equals/notEquals ayni senaryo icindeki onceki digest ile karsilastirir.
        // Kaydet-ac + yeniden uretim sonrasi digest DEGISMEMELI (geometry
        // stabil); profil degisimi sonrasi DEGISMELI (recompute calisti).
        // "packageMessage": {contains: ["..."]} — son paket isleminin Turkce
        // mesajinda bu parcalar GECMELI (P-1/3 parametre esleme raporu).
        if (expect.contains(QStringLiteral("packageMessage"))) {
            const QJsonObject spec = expect[QStringLiteral("packageMessage")].toObject();
            const QString msgQ = QString::fromStdWString(app.lastComponentPackageMessage());
            for (const QJsonValue v : spec[QStringLiteral("contains")].toArray()) {
                const QString part = v.toString();
                if (!msgQ.contains(part))
                    problems << QStringLiteral("packageMessage: \"%1\" yok | mesaj: %2")
                                    .arg(part, msgQ);
            }
            for (const QJsonValue v : spec[QStringLiteral("excludes")].toArray()) {
                const QString part = v.toString();
                if (msgQ.contains(part))
                    problems << QStringLiteral("packageMessage: \"%1\" OLMAMALI | mesaj: %2")
                                    .arg(part, msgQ);
            }
            log(QStringLiteral("packageMessage = %1").arg(msgQ));
        }
        // "catalogEntry": {typeId, pickCount?, visible?, hasDialog?} — paket
        // komponenti KATALOGDA gorunuyor mu ve secim sayisi dogru mu (P-1/4).
        if (expect.contains(QStringLiteral("catalogEntry"))) {
            const QJsonObject spec = expect[QStringLiteral("catalogEntry")].toObject();
            const QString typeId = spec[QStringLiteral("typeId")].toString();
            const std::vector<mm::CatalogEntry> cat = mm::buildComponentCatalog(
                mm::ComponentRegistry::instance(), mm::ComponentDialogRegistry::instance());
            const mm::CatalogEntry* found = mm::findCatalogEntry(cat, typeId.toStdWString());
            if (found == nullptr) {
                problems << QStringLiteral("catalogEntry: %1 katalogda YOK").arg(typeId);
            } else {
                log(QStringLiteral("catalogEntry[%1] pick=%2 visible=%3 dialog=%4")
                        .arg(typeId)
                        .arg(found->pickCount)
                        .arg(found->catalogVisible ? QStringLiteral("1") : QStringLiteral("0"))
                        .arg(found->hasDialog ? QStringLiteral("1") : QStringLiteral("0")));
                if (spec.contains(QStringLiteral("pickCount")) &&
                    found->pickCount != spec[QStringLiteral("pickCount")].toInt())
                    problems << QStringLiteral("catalogEntry: %1 pickCount %2, beklenen %3")
                                    .arg(typeId)
                                    .arg(found->pickCount)
                                    .arg(spec[QStringLiteral("pickCount")].toInt());
                if (spec.contains(QStringLiteral("visible")) &&
                    found->catalogVisible != spec[QStringLiteral("visible")].toBool())
                    problems << QStringLiteral("catalogEntry: %1 catalogVisible beklenenden farkli")
                                    .arg(typeId);
                if (spec.contains(QStringLiteral("hasDialog")) &&
                    found->hasDialog != spec[QStringLiteral("hasDialog")].toBool())
                    problems << QStringLiteral("catalogEntry: %1 hasDialog beklenenden farkli")
                                    .arg(typeId);
            }
        }
        if (expect.contains(QStringLiteral("componentDigest"))) {
            const QJsonObject spec = expect[QStringLiteral("componentDigest")].toObject();
            const QString typeId = spec[QStringLiteral("typeId")].toString();
            const QString key = spec[QStringLiteral("key")].toString();
            QString actual;
            for (const ComponentInstanceRecord& r : app.document().componentInstances())
                if (QString::fromStdWString(r.typeId) == typeId)
                    actual += componentGeometricDigest(app, r);
            if (actual.isEmpty()) {
                problems << QStringLiteral("componentDigest: %1 instance yok").arg(typeId);
            } else if (spec.contains(QStringLiteral("remember"))) {
                digestMemory_.insert(key, actual);
                log(QStringLiteral("componentDigest[%1] = %2")
                        .arg(key, actual));
            } else if (spec.contains(QStringLiteral("equals"))) {
                if (!digestMemory_.contains(key))
                    problems << QStringLiteral("componentDigest: %1 once remember edilmedi").arg(key);
                else if (digestMemory_.value(key) != actual)
                    problems << QStringLiteral("componentDigest[%1] DEGISTI: %2 -> %3")
                                    .arg(key, digestMemory_.value(key), actual);
            } else if (spec.contains(QStringLiteral("notEquals"))) {
                if (digestMemory_.contains(key) && digestMemory_.value(key) == actual)
                    problems << QStringLiteral("componentDigest[%1] DEGISMEDI (beklenen degisim)")
                                    .arg(key);
            }
        }
        // "componentPlateGap": {typeId, against:"main"|"secondary", maxGap} —
        // levha (ownedPlateIds) ile hedef uyenin AABB'leri arasindaki en kisa
        // mesafe. H-8'in componentAttached beklentisi yalniz EKSEN uclarini
        // olcuyordu (maxGap 500 mm): alin levhasi kolona degmese de YESIL
        // kaliyordu. Bu beklenti levhanin GERCEK geometrisini olcer.
        if (expect.contains(QStringLiteral("componentPlateGap"))) {
            const QJsonObject spec = expect[QStringLiteral("componentPlateGap")].toObject();
            const QString typeId = spec[QStringLiteral("typeId")].toString();
            const double maxGap = spec[QStringLiteral("maxGap")].toDouble(5.0);
            const QString against =
                spec.value(QStringLiteral("against")).toString(QStringLiteral("main"));
            struct Box { bool ok = false; double lo[3]{0, 0, 0}; double hi[3]{0, 0, 0}; };
            const auto boxOf = [](const WireframeModel& m) {
                Box b;
                for (const Vec3& v : m.vertices()) {
                    const double c[3]{v.x, v.y, v.z};
                    if (!b.ok) {
                        for (int i = 0; i < 3; ++i) { b.lo[i] = c[i]; b.hi[i] = c[i]; }
                        b.ok = true;
                    } else {
                        for (int i = 0; i < 3; ++i) {
                            b.lo[i] = std::min(b.lo[i], c[i]);
                            b.hi[i] = std::max(b.hi[i], c[i]);
                        }
                    }
                }
                return b;
            };
            const auto unionBox = [](Box& a, const Box& b) {
                if (!b.ok) return;
                if (!a.ok) { a = b; return; }
                for (int i = 0; i < 3; ++i) {
                    a.lo[i] = std::min(a.lo[i], b.lo[i]);
                    a.hi[i] = std::max(a.hi[i], b.hi[i]);
                }
            };
            const auto boxGap = [](const Box& a, const Box& b) {
                double s = 0.0;
                for (int i = 0; i < 3; ++i) {
                    const double d = std::max(std::max(a.lo[i] - b.hi[i], b.lo[i] - a.hi[i]), 0.0);
                    s += d * d;
                }
                return std::sqrt(s);
            };
            int checked = 0;
            double worst = -1.0;
            for (const ComponentInstanceRecord& r : app.document().componentInstances()) {
                if (QString::fromStdWString(r.typeId) != typeId) continue;
                Box target;
                if (against == QStringLiteral("secondary")) {
                    for (const EntityId sid : r.secondaryParts) {
                        const auto si = app.document().indexOfEntity(sid);
                        if (!si || *si >= app.document().models().size()) continue;
                        unionBox(target, boxOf(app.document().models()[*si]));
                    }
                } else {
                    const auto mi = app.document().indexOfEntity(r.mainPart);
                    if (mi && *mi < app.document().models().size())
                        target = boxOf(app.document().models()[*mi]);
                }
                if (!target.ok) continue;
                // childRole verilirse yalniz o roldeki levha olculur
                // (haunch alt/ust levhalari kolona degil ALIN LEVHASINA
                // baglidir; end-plate kalinligi kadar mesafe NORMALDIR).
                std::vector<EntityId> plates = r.ownedPlateIds;
                const QString childRole =
                    spec.value(QStringLiteral("childRole")).toString();
                if (!childRole.isEmpty()) {
                    plates.clear();
                    for (const ComponentChildRecord& c : r.children)
                        if (QString::fromStdString(c.roleKey) == childRole)
                            plates.push_back(c.entityId);
                }
                for (const EntityId pid : plates) {
                    const auto pi = app.document().indexOfEntity(pid);
                    if (!pi || *pi >= app.document().models().size()) continue;
                    const Box pb = boxOf(app.document().models()[*pi]);
                    if (!pb.ok) continue;
                    const double gap = boxGap(pb, target);
                    ++checked;
                    worst = worst < 0.0 ? gap : std::max(worst, gap);
                    log(QStringLiteral("plateGap[%1] model=%2 vs %3 = %4 mm  plate[%5,%6,%7]-[%8,%9,%10] %11[%12,%13,%14]-[%15,%16,%17]")
                            .arg(typeId)
                            .arg(*pi)
                            .arg(against)
                            .arg(gap, 0, 'f', 2)
                            .arg(pb.lo[0], 0, 'f', 1).arg(pb.lo[1], 0, 'f', 1).arg(pb.lo[2], 0, 'f', 1)
                            .arg(pb.hi[0], 0, 'f', 1).arg(pb.hi[1], 0, 'f', 1).arg(pb.hi[2], 0, 'f', 1)
                            .arg(against)
                            .arg(target.lo[0], 0, 'f', 1).arg(target.lo[1], 0, 'f', 1).arg(target.lo[2], 0, 'f', 1)
                            .arg(target.hi[0], 0, 'f', 1).arg(target.hi[1], 0, 'f', 1).arg(target.hi[2], 0, 'f', 1));
                    if (gap > maxGap)
                        problems << QStringLiteral(
                                        "componentPlateGap[%1]: levha uyeden kopuk "
                                        "(%2 mm > %3 mm)")
                                        .arg(typeId)
                                        .arg(gap, 0, 'f', 2)
                                        .arg(maxGap, 0, 'f', 2);
                }
            }
            if (checked == 0)
                problems << QStringLiteral("componentPlateGap: %1 icin levha modeli yok")
                                .arg(typeId);
            else
                log(QStringLiteral("plateGap[%1]: en buyuk bosluk %2 mm (max %3)")
                        .arg(typeId)
                        .arg(worst, 0, 'f', 2)
                        .arg(maxGap, 0, 'f', 2));
        }
        // "modelBox": {model: i} | {all: true} — model(ler)in AABB'si + profil +
        // eksen; yalniz LOG'a yazar (tani icin; hata uretmez). Kusur avinda
        // "hangi uye nerede" sorusunu kod okumadan yanitlar.
        if (expect.contains(QStringLiteral("modelBox"))) {
            const QJsonObject spec = expect[QStringLiteral("modelBox")].toObject();
            const auto dump = [&](int i) {
                const auto& m = app.document().models()[static_cast<std::size_t>(i)];
                double lo[3]{0, 0, 0}, hi[3]{0, 0, 0};
                bool ok = false;
                for (const Vec3& v : m.vertices()) {
                    const double c[3]{v.x, v.y, v.z};
                    if (!ok) {
                        for (int k = 0; k < 3; ++k) { lo[k] = c[k]; hi[k] = c[k]; }
                        ok = true;
                    } else {
                        for (int k = 0; k < 3; ++k) {
                            lo[k] = std::min(lo[k], c[k]);
                            hi[k] = std::max(hi[k], c[k]);
                        }
                    }
                }
                const auto& p = m.properties();
                log(QStringLiteral("modelBox[%1] profile=%2 box[%3,%4,%5]-[%6,%7,%8] "
                                   "axis[%9,%10,%11]->[%12,%13,%14]")
                        .arg(i)
                        .arg(QString::fromStdString(p.profileName))
                        .arg(lo[0], 0, 'f', 1).arg(lo[1], 0, 'f', 1).arg(lo[2], 0, 'f', 1)
                        .arg(hi[0], 0, 'f', 1).arg(hi[1], 0, 'f', 1).arg(hi[2], 0, 'f', 1)
                        .arg(p.axisFromX, 0, 'f', 1).arg(p.axisFromY, 0, 'f', 1)
                        .arg(p.axisFromZ, 0, 'f', 1)
                        .arg(p.axisToX, 0, 'f', 1).arg(p.axisToY, 0, 'f', 1)
                        .arg(p.axisToZ, 0, 'f', 1));
            };
            if (spec.value(QStringLiteral("all")).toBool(false)) {
                for (int i = 0; i < static_cast<int>(app.document().models().size()); ++i)
                    dump(i);
            } else {
                const int i = spec[QStringLiteral("model")].toInt(-1);
                if (i < 0 || static_cast<std::size_t>(i) >= app.document().models().size())
                    problems << QStringLiteral("modelBox: model %1 yok").arg(i);
                else
                    dump(i);
            }
        }
        // "axisScreen": {model, x, y, tol} — modelin eksen baslangicinin
        // (axisFrom, kullanici tiklamasi) tuval izdusumu beklenen noktaya tol
        // piksel icinde mi. H-4 kaniti: tutulan snap tiklamayi uzak noktaya
        // kacirdiysa kiris ekseni beklenen konumda OLMAZ.
        if (expect.contains(QStringLiteral("axisScreen"))) {
            const QJsonObject expected = expect[QStringLiteral("axisScreen")].toObject();
            const int model = expected[QStringLiteral("model")].toInt(-1);
            const int x = expected[QStringLiteral("x")].toInt(0);
            const int y = expected[QStringLiteral("y")].toInt(0);
            const int tol = expected[QStringLiteral("tol")].toInt(8);
            if (model < 0 ||
                static_cast<std::size_t>(model) >= app.document().models().size())
                problems << QStringLiteral("axisScreen: model %1 yok").arg(model);
            else {
                const auto& props = app.document().models()[model].properties();
                POINT p{};
                if (!app.projectWorldPointToScreen(
                        {props.axisFromX, props.axisFromY, props.axisFromZ}, p))
                    problems << QStringLiteral("axisScreen: model %1 ekseni tuval disinda").arg(model);
                else if (std::abs(p.x - x) > tol || std::abs(p.y - y) > tol)
                    problems << QStringLiteral("axisScreen: (%1,%2) beklenen (%3,%4) tol %5 [axis=%6,%7,%8]")
                                    .arg(p.x).arg(p.y).arg(x).arg(y).arg(tol)
                                    .arg(props.axisFromX).arg(props.axisFromY).arg(props.axisFromZ);
            }
        }
        // "vertexScreen": {model, vertex, x, y, tol} — DUZ cizgi/yardimci obje
        // eksenleri (axisFrom) doldurulmadigi icin (H-6 bulgusu) tiklama
        // cozunurlugu KOSE izdusumuyle dogrulanir: vertices()[vertex] beklenen
        // tuval noktasinda mi.
        if (expect.contains(QStringLiteral("vertexScreen"))) {
            const QJsonObject expected = expect[QStringLiteral("vertexScreen")].toObject();
            const int model = expected[QStringLiteral("model")].toInt(-1);
            const int vertex = expected[QStringLiteral("vertex")].toInt(0);
            const int x = expected[QStringLiteral("x")].toInt(0);
            const int y = expected[QStringLiteral("y")].toInt(0);
            const int tol = expected[QStringLiteral("tol")].toInt(8);
            if (model < 0 ||
                static_cast<std::size_t>(model) >= app.document().models().size())
                problems << QStringLiteral("vertexScreen: model %1 yok").arg(model);
            else {
                const auto& vertices = app.document().models()[model].vertices();
                if (vertex < 0 || static_cast<std::size_t>(vertex) >= vertices.size())
                    problems << QStringLiteral("vertexScreen: model %1 kose %2 yok (%3 kose)")
                                    .arg(model).arg(vertex).arg(vertices.size());
                else {
                    POINT p{};
                    if (!app.projectModelVertexToScreen(
                            static_cast<std::size_t>(model),
                            static_cast<std::size_t>(vertex), p))
                        problems << QStringLiteral("vertexScreen: model %1 kose %2 tuval disinda")
                                        .arg(model).arg(vertex);
                    else if (std::abs(p.x - x) > tol || std::abs(p.y - y) > tol)
                        problems << QStringLiteral("vertexScreen: (%1,%2) beklenen (%3,%4) tol %5")
                                        .arg(p.x).arg(p.y).arg(x).arg(y).arg(tol);
                }
            }
        }
        // "lineVisible": {model, min} — H-7/B2 sozlesmesi: modelin ilk iki
        // kosesi arasindaki EKRAN SEGMENTI uzerinde arka plana gore yeterince
        // farkli, akromatik (cizgi rengi — eksen oklari/renkli glifler
        // sayilmaz) piksel var mi. VISION'SIZ, deterministik piksel kaniti:
        // "cizgi yalniz model listesinde degil, PNG'de de GORUNUYOR" (3B +
        // GL + Solid hata sinifini yakalar: model var ama render yok).
        if (expect.contains(QStringLiteral("lineVisible"))) {
            const QJsonObject spec = expect[QStringLiteral("lineVisible")].toObject();
            const int model = spec[QStringLiteral("model")].toInt(-1);
            const int minHits = spec[QStringLiteral("min")].toInt(10);
            if (model < 0 ||
                static_cast<std::size_t>(model) >= app.document().models().size())
                problems << QStringLiteral("lineVisible: model %1 yok").arg(model);
            else {
                const auto& vertices = app.document().models()[model].vertices();
                POINT a{}, b{};
                if (vertices.size() < 2)
                    problems << QStringLiteral("lineVisible: model %1 (%2 kose) cizgi degil")
                                    .arg(model).arg(vertices.size());
                else if (!app.projectModelVertexToScreen(static_cast<std::size_t>(model), 0, a) ||
                         !app.projectModelVertexToScreen(static_cast<std::size_t>(model), 1, b))
                    problems << QStringLiteral("lineVisible: model %1 uclari tuval disinda").arg(model);
                else {
                    QImage image;
                    if (!captureHwnd(window.application().canvasHandle(), image))
                        image = window.grab().toImage();
                    if (image.isNull()) {
                        problems << QStringLiteral("lineVisible: gorsel alinamadi");
                    } else {
                        const int w = image.width(), h = image.height();
                        // Arka plan tahmini: segmentle AYNI satirdaki sol/sag
                        // kenar piksellerinin ortalamasi (gradient dusey).
                        const auto bgFor = [&](int y) {
                            const auto px = [&](int x) {
                                if (x < 0 || x >= w || y < 0 || y >= h) return 0;
                                return static_cast<int>(image.pixel(x, y));
                            };
                            int c1 = px(2), c2 = px(w - 3);
                            return QColor((qRed(c1) + qRed(c2)) / 2,
                                          (qGreen(c1) + qGreen(c2)) / 2,
                                          (qBlue(c1) + qBlue(c2)) / 2);
                        };
                        const double dx = b.x - a.x, dy = b.y - a.y;
                        const double len = std::hypot(dx, dy);
                        if (len < 1.0) {
                            problems << QStringLiteral("lineVisible: segment cok kisa");
                        } else {
                            long hits = 0, samples = 0;
                            const int steps = std::max(20, static_cast<int>(len / 2.0));
                            for (int i = 0; i <= steps; ++i) {
                                const int x = a.x + static_cast<int>(std::lround(dx * i / steps));
                                const int y = a.y + static_cast<int>(std::lround(dy * i / steps));
                                if (x < 0 || x >= w || y < 0 || y >= h) continue;
                                const QRgb p = image.pixel(x, y);
                                const int pr = qRed(p), pg = qGreen(p), pb = qBlue(p);
                                const QColor bg = bgFor(y);
                                int diff = std::abs(pr - bg.red());
                                diff = std::max(diff, std::abs(pg - bg.green()));
                                diff = std::max(diff, std::abs(pb - bg.blue()));
                                int hi = std::max(pr, pg), lo = std::min(pr, pg);
                                hi = std::max(hi, pb); lo = std::min(lo, pb);
                                ++samples;
                                if (diff >= 24 && (hi - lo) <= 48) ++hits;
                            }
                            if (hits < minHits)
                                problems << QStringLiteral(
                                                "lineVisible: %1/%2 piksel cizgi benzeri (en az %3) "
                                                "[segment (%4,%5)-(%6,%7)]")
                                                .arg(hits).arg(samples).arg(minHits)
                                                .arg(a.x).arg(a.y).arg(b.x).arg(b.y);
                        }
                    }
                }
            }
        }
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
        if (step.contains(QStringLiteral("viewCubeFace"))) {
            // B5: VIEW CUBE'a GERCEK fare ile tikla (yuz merkezi layout'tan).
            // Amac: secim ve aktif komut KORUNUR; yuz cizimi kaybolmaz.
            // Tiklama, uygulamanin kendi WndProc yolundan gecer (sendMouse).
            const QJsonObject spec = step[QStringLiteral("viewCubeFace")].toObject();
            const QString faceName = spec[QStringLiteral("view")].toString();
            if (window.application().editMode() != mm::EditMode::View3D)
                return fail(QStringLiteral("viewCubeFace: 3B modda degil"));
            RECT client{};
            HWND canvas = window.application().canvasHandle();
            GetClientRect(canvas, &client);
            const mm::ViewCubeLayout layout =
                mm::ViewCube::layout(client.right, window.application().camera());
            int target = -1;
            // faces[] sirasi sabit (view_cube.cpp faceIndexOfNormal ile ayni):
            // 0=Left 1=Right 2=Bottom 3=Top 4=Back 5=Front
            static const char* const kFaceNames[6] = {"left", "right", "bottom",
                                                      "top",  "back",  "front"};
            for (int i = 0; i < 6; ++i) {
                if (!layout.faces[i].visible) continue;
                if (faceName == QLatin1String(kFaceNames[i])) target = i;
            }
            if (target < 0) {
                // Istenen yuz bu bakistan GORUNMUYOR (kup arka yuzu çevirmez):
                // gorunur yuzleri yaz ve ILK gorunur yuzu tikla — secim koruma
                // kurali her yuz icin aynidir; hangi yuz oldugu log'ta.
                QString seen;
                for (int i = 0; i < 6; ++i) {
                    if (!layout.faces[i].visible) continue;
                    if (seen.isEmpty()) target = i;
                    seen += QString::number(i) + QLatin1Char(',');
                }
                std::printf("[viewcube] '%s' gorunmuyor; gorunur yuzler: %s\n",
                            faceName.toUtf8().constData(), seen.toUtf8().constData());
                if (target < 0)
                    return fail(QStringLiteral("viewCubeFace: kup uzerinde GORUNUR yuz yok"));
            }
            const mm::ViewCubeFace& f = layout.faces[target];
            int cx = 0, cy = 0;
            for (const auto& p : f.points) { cx += p.x; cy += p.y; }
            cx /= 4;
            cy /= 4;
            std::printf("[viewcube] %s yuzu merkezi (%d,%d)\n", faceName.toUtf8().constData(),
                        cx, cy);
            execClick(cx, cy);
            pump(400);   // gecis animasyonu ~180 ms
            return writeStep(step, QStringLiteral("viewCubeFace"));
        }
        if (step.contains(QStringLiteral("clickWorld"))) {
            // H-5: dunya noktasi -> ekran -> tikla. Isin o noktadan gectigi
            // garantidir (izdusum isini ters yonde kurar); kamera tarafindaki
            // ILK yuz carpar — yan yuz secimi icin yuzun disa bakan noktasini
            // hedefle.
            const QJsonObject spec = step[QStringLiteral("clickWorld")].toObject();
            POINT target{};
            if (!window.application().projectWorldPointToScreen(
                    {spec[QStringLiteral("x")].toDouble(),
                     spec[QStringLiteral("y")].toDouble(),
                     spec[QStringLiteral("z")].toDouble()},
                    target))
                return fail(QStringLiteral("clickWorld: nokta tuval disinda"));
            execClick(target.x, target.y);
            return writeStep(step, QStringLiteral("clickWorld@(%1,%2)")
                                           .arg(target.x).arg(target.y));
        }
        if (step.contains(QStringLiteral("fit"))) {
            // H-5: ana tuvali modele sigdir — yuksek kottaki modeller
            // baslangic kadrajinin disinda kalabilir.
            window.application().fitViewport(window.application().canvasHandle());
            pump(80);
            return writeStep(step, QStringLiteral("fit"));
        }
        if (step.contains(QStringLiteral("faceClick"))) {
            // H-5: yuz turune gore deterministik tik (view.toSurface icin).
            // Aday dunya noktalari yuz uzerinde kurulur; her aday ekrana
            // izdusulur ve pickSceneFaceAt ile DOGRU yuzun vuruldugu onaylanir
            // (kamera hangi tarafta olursa olsun gecerli — yan yuz icin +Y/-Y
            // iki aday denenir). Eslesen aday tıklanır.
            const QJsonObject spec = step[QStringLiteral("faceClick")].toObject();
            const int model = spec[QStringLiteral("model")].toInt(-1);
            const QString kind = spec[QStringLiteral("kind")].toString(QStringLiteral("top"));
            Application& app = window.application();
            if (model < 0 ||
                static_cast<std::size_t>(model) >= app.document().models().size())
                return fail(QStringLiteral("faceClick: model %1 yok").arg(model));
            if (kind != QStringLiteral("top") && kind != QStringLiteral("side"))
                return fail(QStringLiteral("faceClick: kind top|side"));
            const auto& p = app.document().models()[model].properties();
            const double mx = (p.axisFromX + p.axisToX) / 2.0;
            const double my = (p.axisFromY + p.axisToY) / 2.0;
            const double mz = (p.axisFromZ + p.axisToZ) / 2.0;
            double height = 240.0, width = 240.0;
            for (const auto& prof : app.profiles())
                if (prof.name == p.profileName) {
                    height = prof.height;
                    width = prof.width;
                    break;
                }
            // Varsayilan ankraj Top: kesit eksenin ALTINDA sarkar (ust yuz =
            // eksen koti). Adaylar yuzey UZERINDE; isin o noktadan gecer.
            std::vector<std::pair<Vec3, QString>> candidates;
            if (kind == QStringLiteral("top"))
                candidates.push_back({{mx, my, mz}, QStringLiteral("top")});
            else {
                candidates.push_back({{mx, my + width / 2.0, mz - height / 2.0},
                                      QStringLiteral("+Y")});
                candidates.push_back({{mx, my - width / 2.0, mz - height / 2.0},
                                      QStringLiteral("-Y")});
            }
            const auto matches = [&](const std::optional<Application::PendingFaceHit>& hit) {
                if (!hit) return false;
                const double nz = hit->hit.normal.z;
                return kind == QStringLiteral("top") ? std::fabs(nz) > 0.9
                                                     : std::fabs(nz) < 0.1;
            };
            QStringList diag;
            for (const auto& [point, label] : candidates) {
                POINT target{};
                if (!app.projectWorldPointToScreen(point, target)) {
                    diag << QStringLiteral("%1:ekran disi").arg(label);
                    continue;
                }
                const auto hit = app.pickSceneFaceAt(target.x, target.y);
                diag << QStringLiteral("%1@(%2,%3):%4")
                            .arg(label).arg(target.x).arg(target.y)
                            .arg(hit ? QStringLiteral("nz=%1")
                                           .arg(hit->hit.normal.z, 0, 'f', 3)
                                     : QStringLiteral("bos"));
                if (matches(hit)) {
                    execClick(target.x, target.y);
                    return writeStep(step, QStringLiteral("faceClick:%1@(%2,%3)")
                                                   .arg(label).arg(target.x).arg(target.y));
                }
            }
            return fail(QStringLiteral("faceClick: %1 yuzu hicbir adaydan vurulamadi (%2)")
                            .arg(kind, diag.join(QStringLiteral(" | "))));
        }
        if (step.contains(QStringLiteral("move"))) {
            // H-4: tiklamasiz imlec hareketi (WM_MOUSEMOVE) — snap tutma/histerezis
            // davranisini adim adim surmek icin.
            const QJsonArray point = step[QStringLiteral("move")].toArray();
            if (point.size() < 2) return fail(QStringLiteral("move: [x, y] bekleniyor"));
            sendMouse(WM_MOUSEMOVE, 0, point[0].toInt(), point[1].toInt());
            pump(40);
            return writeStep(step, QStringLiteral("move"));
        }
        if (step.contains(QStringLiteral("moveClick"))) {
            // H-4: [[x1,y1],[x2,y2]] — tek adimda once ilk noktaya HAREKET
            // (snap yakalanir), sonra ikinci noktaya hareket + tikla. Adimlar
            // arasi writeStep/PNG gideri tutma suresi (holdTimeMs ~300 ms)
            // icindeki senaryoyu belirsiz yapmasin diye uc olay tek adimda.
            const QJsonArray seq = step[QStringLiteral("moveClick")].toArray();
            if (seq.size() < 2)
                return fail(QStringLiteral("moveClick: [[x,y],[x2,y2]] bekleniyor"));
            const QJsonArray from = seq[0].toArray();
            const QJsonArray to = seq[1].toArray();
            if (from.size() < 2 || to.size() < 2)
                return fail(QStringLiteral("moveClick: [[x,y],[x2,y2]] bekleniyor"));
            sendMouse(WM_MOUSEMOVE, 0, from[0].toInt(), from[1].toInt());
            pump(20);
            sendMouse(WM_MOUSEMOVE, 0, to[0].toInt(), to[1].toInt());
            pump(20);
            sendMouse(WM_LBUTTONDOWN, MK_LBUTTON, to[0].toInt(), to[1].toInt());
            sendMouse(WM_LBUTTONUP, 0, to[0].toInt(), to[1].toInt());
            pump(40);
            return writeStep(step, QStringLiteral("moveClick"));
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
        if (step.contains(QStringLiteral("snapHold"))) {
            // H-6: holdTimeMs'i senaryoya ozgu ayarla (zamanlama kaniti:
            // uzun sure soluk izi guvenle gozlemleyebilmek icin).
            SnapSettings settings = window.application().snapSettings();
            settings.holdTimeMs = step[QStringLiteral("snapHold")].toDouble(300.0);
            window.application().setSnapSettings(settings);
            pump(40);
            return writeStep(step, QStringLiteral("snapHold"));
        }
        if (step.contains(QStringLiteral("gridSnap"))) {
            // H-6: grid snap'i kapat — uzak nokta deterministik olarak
            // snapsiz kalsin (aksi halde GRID snap fading'i engeller).
            window.application().setGridSnapEnabled(
                step[QStringLiteral("gridSnap")].toBool());
            pump(40);
            return writeStep(step, QStringLiteral("gridSnap"));
        }
        if (step.contains(QStringLiteral("open"))) {
            QString path = step[QStringLiteral("open")].toString();
            const QFileInfo info(path);
            if (info.isRelative()) {
                // Once CIKTI klasoru (aynı senaryonun "save" adimi), yoksa
                // senaryo dosyasinin klasoru (sabit test belgeleri).
                const QString inOut = outDir + QStringLiteral("/") + path;
                const QFileInfo scriptDir(scriptDirPath_);
                path = QFileInfo(inOut).exists() ? inOut
                                                 : scriptDir.dir().filePath(path);
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
        // ---- H-8: uygulama-katmani e2e op'lari (kot degisimi -> bagli
        // bilesen zinciri). Panel ENTER/UYGULA akisinin cagirdigi AYNI public
        // API'ler kullanilir (panel kablolamasi Python testlerinde dogrulanir);
        // burada asil sozlesme BELGE uzerindeki zincirdir.
        if (step.contains(QStringLiteral("appColumnLevels"))) {
            const QJsonObject spec = step[QStringLiteral("appColumnLevels")].toObject();
            window.application().applyColumnLevels(
                spec[QStringLiteral("top")].toDouble(),
                spec[QStringLiteral("bottom")].toDouble());
            pump(100);
            return writeStep(step, QStringLiteral("appColumnLevels"));
        }
        if (step.contains(QStringLiteral("placeColumn"))) {
            const QJsonObject spec = step[QStringLiteral("placeColumn")].toObject();
            window.application().placeColumn(
                {spec[QStringLiteral("x")].toDouble(),
                 spec[QStringLiteral("y")].toDouble(),
                 spec[QStringLiteral("z")].toDouble()});
            pump(100);
            return writeStep(step, QStringLiteral("placeColumn"));
        }
        if (step.contains(QStringLiteral("assignLine"))) {
            const QJsonObject spec = step[QStringLiteral("assignLine")].toObject();
            window.application().assignProfileToLine(
                {spec[QStringLiteral("x1")].toDouble(),
                 spec[QStringLiteral("y1")].toDouble(),
                 spec[QStringLiteral("z1")].toDouble()},
                {spec[QStringLiteral("x2")].toDouble(),
                 spec[QStringLiteral("y2")].toDouble(),
                 spec[QStringLiteral("z2")].toDouble()},
                spec[QStringLiteral("profile")].toString().toStdString());
            pump(100);
            return writeStep(step, QStringLiteral("assignLine"));
        }
        if (step.contains(QStringLiteral("select"))) {
            const QJsonArray arr = step[QStringLiteral("select")].toArray();
            std::vector<std::size_t> idx;
            idx.reserve(static_cast<std::size_t>(arr.size()));
            for (const QJsonValue v : arr)
                idx.push_back(static_cast<std::size_t>(v.toInt()));
            window.application().selectModels(idx);
            pump(60);
            return writeStep(step, QStringLiteral("select"));
        }
        if (step.contains(QStringLiteral("assignProfile"))) {
            const QJsonObject spec = step[QStringLiteral("assignProfile")].toObject();
            const int model = spec[QStringLiteral("model")].toInt(-1);
            if (model < 0) return fail(QStringLiteral("assignProfile: model eksik"));
            window.application().selectModels({static_cast<std::size_t>(model)});
            window.application().assignProfileToSelection(
                spec[QStringLiteral("profile")].toString().toStdString());
            pump(120);
            return writeStep(step, QStringLiteral("assignProfile"));
        }
        // ---- UI-4 GT-1: panel + profil secim penceresi (Qt tarafi) ----
        if (step.contains(QStringLiteral("panelType"))) {
            const QString text = step[QStringLiteral("panelType")].toObject()
                                     [QStringLiteral("text")].toString();
            PropertiesPanel* panel = window.propertiesPanel();
            if (!panel) return fail(QStringLiteral("panelType: panel yok"));
            QLineEdit* edit = panel->profileEdit();
            if (!edit) return fail(QStringLiteral("panelType: profil alani yok"));
            edit->setFocus();
            edit->clear();
            for (const QChar c : text) {
                QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QString(c));
                QApplication::sendEvent(edit, &press);
                QKeyEvent release(QEvent::KeyRelease, 0, Qt::NoModifier,
                                  QString(c));
                QApplication::sendEvent(edit, &release);
            }
            pump(80);
            return writeStep(step, QStringLiteral("panelType"));
        }
        if (step.contains(QStringLiteral("panelApply"))) {
            PropertiesPanel* panel = window.propertiesPanel();
            if (!panel) return fail(QStringLiteral("panelApply: panel yok"));
            QPushButton* apply =
                panel->findChild<QPushButton*>(QStringLiteral("propsApply"));
            if (!apply) return fail(QStringLiteral("panelApply: UYGULA yok"));
            apply->click();
            pump(120);
            return writeStep(step, QStringLiteral("panelApply"));
        }
        if (step.contains(QStringLiteral("pickerOpen"))) {
            ProfilePickerDialog* dialog = window.profilePickerDialog();
            if (!dialog) return fail(QStringLiteral("pickerOpen: pencere yok"));
            dialog->show();
            dialog->raise();
            pump(80);
            return writeStep(step, QStringLiteral("pickerOpen"));
        }
        if (step.contains(QStringLiteral("pickerFilter"))) {
            ProfilePickerDialog* dialog = window.profilePickerDialog();
            if (!dialog) return fail(QStringLiteral("pickerFilter: pencere yok"));
            QLineEdit* filter = dialog->findChild<QLineEdit*>(
                QStringLiteral("profileFilter"));
            if (!filter) return fail(QStringLiteral("pickerFilter: filtre yok"));
            filter->setFocus();
            filter->setText(
                step[QStringLiteral("pickerFilter")].toObject()
                    [QStringLiteral("text")].toString());
            pump(80);
            return writeStep(step, QStringLiteral("pickerFilter"));
        }
        if (step.contains(QStringLiteral("pickerSelect"))) {
            ProfilePickerDialog* dialog = window.profilePickerDialog();
            if (!dialog) return fail(QStringLiteral("pickerSelect: pencere yok"));
            const QString name = step[QStringLiteral("pickerSelect")].toObject()
                                     [QStringLiteral("name")].toString();
            QTreeWidget* tree =
                dialog->findChild<QTreeWidget*>(QStringLiteral("profileTree"));
            if (!tree) return fail(QStringLiteral("pickerSelect: agac yok"));
            bool found = false;
            for (int i = 0; i < tree->topLevelItemCount() && !found; ++i) {
                QTreeWidgetItem* familyItem = tree->topLevelItem(i);
                for (int j = 0; j < familyItem->childCount(); ++j) {
                    QTreeWidgetItem* child = familyItem->child(j);
                    if (child->data(0, Qt::UserRole).toString() == name) {
                        tree->setCurrentItem(child);
                        found = true;
                        break;
                    }
                }
            }
            if (!found)
                return fail(QStringLiteral("pickerSelect: %1 agacta yok").arg(name));
            pump(80);
            return writeStep(step, QStringLiteral("pickerSelect"));
        }
        if (step.contains(QStringLiteral("pickerOk"))) {
            ProfilePickerDialog* dialog = window.profilePickerDialog();
            if (!dialog) return fail(QStringLiteral("pickerOk: pencere yok"));
            QPushButton* ok = dialog->findChild<QPushButton*>(
                QStringLiteral("profileOkButton"));
            if (!ok) return fail(QStringLiteral("pickerOk: Tamam yok"));
            ok->click();
            pump(80);
            return writeStep(step, QStringLiteral("pickerOk"));
        }
        if (step.contains(QStringLiteral("appComponent"))) {
            const QJsonObject spec = step[QStringLiteral("appComponent")].toObject();
            const QJsonArray arr = spec[QStringLiteral("parts")].toArray();
            std::vector<std::size_t> parts;
            parts.reserve(static_cast<std::size_t>(arr.size()));
            for (const QJsonValue v : arr)
                parts.push_back(static_cast<std::size_t>(v.toInt()));
            // Istege bagli params: {"ad": deger, ...} — sayilar double,
            // true/false bool, metin wstring olarak CompParam'a cevrilir.
            std::vector<mm::CompParam> params;
            if (spec.contains(QStringLiteral("params"))) {
                const QJsonObject po = spec[QStringLiteral("params")].toObject();
                for (auto it = po.begin(); it != po.end(); ++it) {
                    mm::CompParam p;
                    p.name = it.key().toStdWString();
                    const QJsonValue v = it.value();
                    if (v.isBool()) p.value = v.toBool();
                    else if (v.isString()) p.value = v.toString().toStdWString();
                    else p.value = v.toDouble();
                    params.push_back(std::move(p));
                }
            }
            window.application().applyComponentFromParts(
                spec[QStringLiteral("typeId")].toString().toStdWString(),
                parts, params);
            pump(160);
            return writeStep(step, QStringLiteral("appComponent"));
        }
        if (step.contains(QStringLiteral("appComponentParams"))) {
            // H-10: MEVCUT bilesen kaydinin parametrelerini degistir (Properties /
            // diyalog OK yolu). {typeId, params:{ad:deger}} — verilmeyen
            // parametreler MEVCUT degerlerinde kalir (alignParamsToSpecs'in
            // varsayilana cekmesini onlemek icin once kayittan kopyalanir).
            const QJsonObject spec = step[QStringLiteral("appComponentParams")].toObject();
            const std::wstring typeId =
                spec[QStringLiteral("typeId")].toString().toStdWString();
            // P-1/6: orphan (paket eksik) kayit da BULUNUR — "duzenleme kilitli"
            // ancak boyle dogrulanabilir (setSelectedComponentParams reddeder).
            const bool expectFailEarly =
                spec.value(QStringLiteral("expectFail")).toBool(false);
            EntityId compId = kInvalidEntityId;
            std::vector<mm::CompParam> eff;
            for (const auto& rec : window.application().document().componentInstances()) {
                if (rec.orphan && !expectFailEarly) continue;
                if (rec.typeId != typeId) continue;
                compId = rec.id;
                eff = rec.params;
                break;
            }
            if (compId == kInvalidEntityId)
                return fail(QStringLiteral("appComponentParams: kayit yok (%1)")
                                .arg(QString::fromStdWString(typeId)));
            const QJsonObject po = spec[QStringLiteral("params")].toObject();
            for (auto it = po.begin(); it != po.end(); ++it) {
                const std::wstring name = it.key().toStdWString();
                mm::CompParam p;
                p.name = name;
                const QJsonValue v = it.value();
                if (v.isBool()) p.value = v.toBool();
                else if (v.isString()) p.value = v.toString().toStdWString();
                else p.value = v.toDouble();
                bool replaced = false;
                for (auto& cur : eff) {
                    if (cur.name != name) continue;
                    cur.value = p.value;
                    replaced = true;
                    break;
                }
                if (!replaced) eff.push_back(std::move(p));
            }
            const bool applied = window.application().setSelectedComponentParams(compId, eff);
            // P-1/5: "expectFail": true → adim yalniz REDDEDILIRSE gecer
            // (recipe 'check' ihlali: belge DEGISMEMELI, Turkce mesaj durum
            // cubugunda olmali). Beklenmedik basari da hatadir.
            const bool expectFail =
                spec.value(QStringLiteral("expectFail")).toBool(false);
            if (expectFail && applied)
                return fail(QStringLiteral("appComponentParams: reddetmeliydi ama uygulandi (%1)")
                                .arg(QString::fromStdWString(typeId)));
            if (!expectFail && !applied)
                return fail(QStringLiteral("appComponentParams: uygulanamadi (%1)")
                                .arg(QString::fromStdWString(typeId)));
            pump(160);
            return writeStep(step, QStringLiteral("appComponentParams"));
        }
        if (step.contains(QStringLiteral("appRemovePackage"))) {
            // P-1/6: "PAKET EKSIK" SENARYOSU. Kurulu paketi registry'den ve
            // senaryo-yerel koke (outDir/components) yazilmis klasorden siler;
            // sonra ayni .mmw acilinca kayit "paket eksik" olmali ve duzenleme
            // KILITLI olmali (geometri korunur).
            const QJsonObject spec = step[QStringLiteral("appRemovePackage")].toObject();
            const std::wstring typeId =
                spec[QStringLiteral("typeId")].toString().toStdWString();
            if (typeId.empty())
                return fail(QStringLiteral("appRemovePackage: 'typeId' eksik"));
            mm::ComponentRegistry::instance().unregisterType(typeId);
            mm::ComponentDialogRegistry::instance().unregisterDialog(typeId);
            if (spec.value(QStringLiteral("deleteDir")).toBool(true)) {
                QString root = spec[QStringLiteral("userRoot")].toString();
                if (root.isEmpty()) root = QStringLiteral("components");
                const QFileInfo ur(root);
                if (ur.isRelative())
                    root = QFileInfo(outDir + QStringLiteral("/") + root).absoluteFilePath();
                const QString dir = root + QStringLiteral("/") +
                                    QString::fromStdWString(typeId) + QStringLiteral(".mmcpkg");
                QDir(dir).removeRecursively();
                printf("[paket-sil] %s\n", dir.toUtf8().constData());
            }
            pump(80);
            return writeStep(step, QStringLiteral("appRemovePackage"));
        }
        if (step.contains(QStringLiteral("appMaterialList"))) {
            // P-2/7: MALZEME LISTESI raporunu (levha + bulon + kaynak) dosyaya
            // yaz — senaryo icerigi boylece dogrulanabilir kalir.
            const QJsonObject spec = step[QStringLiteral("appMaterialList")].toObject();
            QString path = spec[QStringLiteral("path")].toString();
            if (path.isEmpty()) path = QStringLiteral("malzeme-listesi.txt");
            const QFileInfo fi(path);
            if (fi.isRelative()) path = outDir + QStringLiteral("/") + path;
            QFile out(path);
            if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return fail(QStringLiteral("appMaterialList: dosya yazilamadi (%1)").arg(path));
            const QString text = QString::fromStdWString(
                mm::materialListReport(window.application().document()));
            out.write(text.toUtf8());
            out.close();
            printf("[malzeme-listesi] %s (%lld bayt)\n", path.toUtf8().constData(),
                   static_cast<long long>(text.toUtf8().size()));
            pump(60);
            return writeStep(step, QStringLiteral("appMaterialList"));
        }
        if (step.contains(QStringLiteral("appMoveModels"))) {
            // P-2/7: modelleri otele; bagli komponentler uretim cekirdeginden
            // yeniden uretilir (fail-closed ise eski hal kalir).
            const QJsonObject spec = step[QStringLiteral("appMoveModels")].toObject();
            std::vector<std::size_t> indices;
            for (const auto v : spec[QStringLiteral("indices")].toArray())
                indices.push_back(static_cast<std::size_t>(v.toInt(-1)));
            const QJsonArray d = spec[QStringLiteral("delta")].toArray();
            if (d.size() < 3 || indices.empty())
                return fail(QStringLiteral("appMoveModels: 'indices' ve 3'lu 'delta' bekleniyor"));
            const mm::Vec3 delta{d[0].toDouble(), d[1].toDouble(), d[2].toDouble()};
            if (!window.application().translateModelsAndRecompute(indices, delta))
                return fail(QStringLiteral("appMoveModels: tasima uygulanamadi"));
            pump(300);
            return writeStep(step, QStringLiteral("appMoveModels"));
        }
        if (step.contains(QStringLiteral("appConstructionPlane"))) {
            // P-2/3: konstrüksiyon duzlemi kur (kesim duzlemi kaynagi).
            const QJsonObject spec = step[QStringLiteral("appConstructionPlane")].toObject();
            const QJsonArray o = spec[QStringLiteral("origin")].toArray();
            const QJsonArray n = spec[QStringLiteral("normal")].toArray();
            if (o.size() < 3 || n.size() < 3)
                return fail(QStringLiteral("appConstructionPlane: 'origin' ve 'normal' 3'lu olmali"));
            const mm::Vec3 origin{o[0].toDouble(), o[1].toDouble(), o[2].toDouble()};
            const mm::Vec3 normal{n[0].toDouble(), n[1].toDouble(), n[2].toDouble()};
            const double size = spec.contains(QStringLiteral("size"))
                                    ? spec[QStringLiteral("size")].toDouble()
                                    : 1000.0;
            const mm::WorkPlane plane = mm::WorkPlane::fromAxisAndPoint(origin, origin + normal);
            window.application().documentEditable().addConstructionPlane(plane, size);
            pump(60);
            return writeStep(step, QStringLiteral("appConstructionPlane"));
        }
        if (step.contains(QStringLiteral("appPlaneCut"))) {
            // P-2/3: duzlem kesme uygula (kullanici yolunun ayni cekirdegi).
            const QJsonObject spec = step[QStringLiteral("appPlaneCut")].toObject();
            const int target = spec[QStringLiteral("target")].toInt(-1);
            const int plane = spec[QStringLiteral("plane")].toInt(-1);
            if (target < 0 || plane < 0)
                return fail(QStringLiteral("appPlaneCut: 'target' ve 'plane' indeksleri bekleniyor"));
            if (!window.application().applyPlaneCutForTest(
                    static_cast<std::size_t>(target), static_cast<std::size_t>(plane)))
                return fail(QStringLiteral("appPlaneCut: kesme kurulamadi"));
            pump(300);
            return writeStep(step, QStringLiteral("appPlaneCut"));
        }
        if (step.contains(QStringLiteral("appCopyModels"))) {
            // P-2/3: secili modelleri kopyala (kopya YENI kimlik + yeni konum).
            const QJsonObject spec = step[QStringLiteral("appCopyModels")].toObject();
            std::vector<std::size_t> indices;
            for (const auto v : spec[QStringLiteral("indices")].toArray())
                indices.push_back(static_cast<std::size_t>(v.toInt(-1)));
            const QJsonArray d = spec[QStringLiteral("delta")].toArray();
            if (d.size() < 3 || indices.empty())
                return fail(QStringLiteral("appCopyModels: 'indices' ve 3'lu 'delta' bekleniyor"));
            if (window.application().copyModelsAndRecompute(
                    indices, mm::Vec3{d[0].toDouble(), d[1].toDouble(), d[2].toDouble()}) == 0)
                return fail(QStringLiteral("appCopyModels: kopya uretilemedi"));
            pump(120);
            return writeStep(step, QStringLiteral("appCopyModels"));
        }
        if (step.contains(QStringLiteral("componentDialogSchema"))) {
            // P-1/5: PAKET DIYALOG SEMASI uygulamaya KAYITLI mi? dialog.json
            // sekmeleri (useTabs/asTab) + parametre dagitimi diyalog
            // registry'sinden okunur — widget'in gordugu semanin aynisi.
            const QJsonObject spec =
                step[QStringLiteral("componentDialogSchema")].toObject();
            const std::wstring typeId =
                spec[QStringLiteral("typeId")].toString().toStdWString();
            const mm::ComponentDialogDefinition* def =
                mm::ComponentDialogRegistry::instance().find(typeId);
            if (!def)
                return fail(QStringLiteral("componentDialogSchema: sema yok (%1)")
                                .arg(QString::fromStdWString(typeId)));
            int tabs = 0;
            QStringList titles;
            for (const auto& g : def->schema.groups) {
                if (g.asTab && !g.paramNames.empty()) ++tabs;
                titles << QString::fromStdWString(g.title);
            }
            const int groups = static_cast<int>(def->schema.groups.size());
            if (spec.contains(QStringLiteral("groups")) &&
                spec[QStringLiteral("groups")].toInt() != groups)
                return fail(QStringLiteral("componentDialogSchema: grup sayisi %1 != %2")
                                .arg(groups)
                                .arg(spec[QStringLiteral("groups")].toInt()));
            if (spec.contains(QStringLiteral("tabs")) &&
                spec[QStringLiteral("tabs")].toInt() != tabs)
                return fail(QStringLiteral("componentDialogSchema: sekme sayisi %1 != %2")
                                .arg(tabs)
                                .arg(spec[QStringLiteral("tabs")].toInt()));
            if (spec.contains(QStringLiteral("useTabs")) &&
                spec[QStringLiteral("useTabs")].toBool() != def->schema.useTabs)
                return fail(QStringLiteral("componentDialogSchema: useTabs uyusmuyor"));
            if (spec.contains(QStringLiteral("paramCount")) &&
                spec[QStringLiteral("paramCount")].toInt() !=
                    static_cast<int>(def->specs.size()))
                return fail(QStringLiteral("componentDialogSchema: parametre sayisi %1 != %2")
                                .arg(static_cast<int>(def->specs.size()))
                                .arg(spec[QStringLiteral("paramCount")].toInt()));
            if (spec.contains(QStringLiteral("titles"))) {
                const QJsonArray want = spec[QStringLiteral("titles")].toArray();
                if (want.size() != titles.size())
                    return fail(QStringLiteral("componentDialogSchema: baslik sayisi uyusmuyor"));
                for (int i = 0; i < want.size(); ++i)
                    if (want.at(i).toString() != titles.at(i))
                        return fail(QStringLiteral("componentDialogSchema: %1. sekme '%2' != '%3'")
                                        .arg(i + 1)
                                        .arg(titles.at(i), want.at(i).toString()));
            }
            printf("[diyalog-sema] %s: %d grup, %d sekme, %d parametre\n",
                   spec[QStringLiteral("typeId")].toString().toUtf8().constData(),
                   groups, tabs, static_cast<int>(def->specs.size()));
            pump(80);
            return writeStep(step, QStringLiteral("componentDialogSchema"));
        }
        if (step.contains(QStringLiteral("appImportPackage"))) {
            // P-1/1: paket ICE AKTARMA yolunu uctan uca kosturur (klasor secimi
            // yerine verilen kaynak). {"path": "<kaynak klasor>", "embed": bool}
            // Kurulum ATOMIK; ret edilirse adim BASARISIZ olur (sessiz gecis yok).
            const QJsonObject spec = step[QStringLiteral("appImportPackage")].toObject();
            const QString path = spec[QStringLiteral("path")].toString();
            const bool embed = spec[QStringLiteral("embed")].toBool(false);
            // GT izolasyonu: paket senaryo-yerel koke kurulur (outDir/components),
            // boylece gercek %APPDATA% kirlenmez ve senaryo TEKRAR kosabilir.
            QString userRoot;
            if (spec.contains(QStringLiteral("userRoot"))) {
                userRoot = spec[QStringLiteral("userRoot")].toString();
                const QFileInfo ur(userRoot);
                if (ur.isRelative())
                    userRoot = QFileInfo(outDir + QStringLiteral("/") + userRoot).absoluteFilePath();
                    // P-1/2: yeniden yukleme ayni koku taramali (GT izolasyonu).
                    window.application().setComponentUserPackageRootOverride(
                        std::filesystem::path(userRoot.toStdWString()));
            }
            if (path.isEmpty())
                return fail(QStringLiteral("appImportPackage: 'path' eksik"));
            // Goreli yol: senaryo dosyasinin klasorune gore (sabit fixture'lar).
            QString resolved = path;
            {
                const QFileInfo info(path);
                if (info.isRelative())
                    resolved = QFileInfo(scriptDirPath_).dir().filePath(path);
            }
            if (!window.application().importComponentPackage(
                    resolved.toStdWString(), embed, userRoot.toStdWString()))
                return fail(QStringLiteral("appImportPackage: paket kurulmadi: %1 — %2")
                                .arg(path,
                                     QString::fromStdWString(
                                         window.application().lastComponentPackageMessage())));
            pump(120);
            return writeStep(step, QStringLiteral("appImportPackage:") + path);
        }
        if (step.contains(QStringLiteral("appReloadPackages"))) {
            // P-1/2: paketleri yeniden yukle. {"devDirs": ["../fixtures/p1_dev"]}
            // verilirse gelistirme klasorleri once ayarlanir (goreli yollar
            // cikti klasorune gore cozulur) — Studio'nun canli paketi ayni
            // typeId'yi golgeler.
            const QJsonObject spec = step[QStringLiteral("appReloadPackages")].toObject();
            if (spec.contains(QStringLiteral("devDirs"))) {
                std::vector<std::filesystem::path> dirs;
                for (const QJsonValue v : spec[QStringLiteral("devDirs")].toArray()) {
                    QString d = v.toString();
                    const QFileInfo info(d);
                    if (info.isRelative()) {
                        // fixture'lar script'e gore (tests/gt1 -> tests/fixtures)
                        const QString viaScript =
                            QFileInfo(scriptDirPath_).dir().filePath(d);
                        d = QFileInfo::exists(viaScript)
                                ? QFileInfo(viaScript).absoluteFilePath()
                                : QFileInfo(outDir + QStringLiteral("/") + d).absoluteFilePath();
                    }
                    dirs.push_back(std::filesystem::path(d.toStdWString()));
                }
                window.application().setComponentDevPackageDirs(std::move(dirs));
            }
            const int loaded = window.application().reloadComponentPackages();
            pump(160);
            return writeStep(step, QStringLiteral("appReloadPackages:") +
                                       QString::number(loaded));
        }
        if (step.contains(QStringLiteral("componentTypeRegistered"))) {
            // P-1/1: kurulumdan sonra tip registry'de HEMEN gorunmeli (yeniden
            // baslatma yok). {"typeId": "...", "package": bool}
            const QJsonObject spec = step[QStringLiteral("componentTypeRegistered")].toObject();
            const std::wstring typeId = spec[QStringLiteral("typeId")].toString().toStdWString();
            const bool wantPackage = spec[QStringLiteral("package")].toBool(true);
            const mm::ComponentType* type = mm::ComponentRegistry::instance().findType(typeId);
            if (type == nullptr)
                return fail(QStringLiteral("componentTypeRegistered: tip yok: %1")
                                .arg(QString::fromStdWString(typeId)));
            if (wantPackage && !mm::ComponentRegistry::instance().isPackageType(typeId))
                return fail(QStringLiteral("componentTypeRegistered: tip paket kaynagi degil: %1")
                                .arg(QString::fromStdWString(typeId)));
            // P-1/2/3: {"schemaVersion": N} verilirse surum de denetlenir
            // (gelistirme klasoru kurulu paketi golgeliyorsa surum DEGISIR).
            if (spec.contains(QStringLiteral("schemaVersion"))) {
                const int want = spec[QStringLiteral("schemaVersion")].toInt();
                if (type->schemaVersion != want)
                    return fail(QStringLiteral("componentTypeRegistered: %1 surum %2, "
                                               "beklenen %3")
                                    .arg(QString::fromStdWString(typeId))
                                    .arg(type->schemaVersion)
                                    .arg(want));
            }
            return writeStep(step,
                             QStringLiteral("componentTypeRegistered:") +
                                 QString::fromStdWString(typeId) + QStringLiteral(" v") +
                                 QString::number(type->schemaVersion));
        }
        if (step.contains(QStringLiteral("save"))) {
            QString path = step[QStringLiteral("save")].toString();
            const QFileInfo info(path);
            if (info.isRelative()) {
                // Cikti klasorune (outDir) yazilir — kaydet-ac cifti ayni
                // klasorde kalir, repo/senaryo klasoru kirlenmez.
                path = QFileInfo(outDir + QStringLiteral("/") + path).absoluteFilePath();
            }
            if (!window.application().saveDocumentPath(path.toStdWString()))
                return fail(QStringLiteral("belge kaydedilemedi: %1").arg(path));
            pump(120);
            return writeStep(step, QStringLiteral("save:") + path);
        }
        if (step.contains(QStringLiteral("search"))) {
            // B1: arama kutusuna metin KOY (deterministik; completer ayni
            // searchSuggestions yolundan beslenir) — asil sozlesme "expect
            // quickSearch" adiminda kilitlenir.
            const QString text = step[QStringLiteral("search")].toString();
            window.setQuickSearchText(text);
            pump(60);
            return writeStep(step, QStringLiteral("search:") + text);
        }
        if (step.contains(QStringLiteral("clickModel"))) {
            // H-1: modelin izdusumu UZERINDEN deterministik yuz tiklamasi
            // (vision'siz). {"model": N} -> tum koselerin ekran agirlik
            // merkezi; {"model": N, "vertex": i} -> tek kose; {"vertex": [i,j]}
            // -> iki kosenin orta noktasi. Nokta tuval icindeyse oraya tiklar.
            const QJsonObject spec = step[QStringLiteral("clickModel")].toObject();
            Application& app = window.application();
            const int model = spec[QStringLiteral("model")].toInt(-1);
            if (model < 0 ||
                static_cast<std::size_t>(model) >= app.document().models().size())
                return fail(QStringLiteral("clickModel: model %1 yok").arg(model));
            const auto& vertices = app.document().models()[model].vertices();
            if (vertices.empty()) return fail(QStringLiteral("clickModel: model %1 koseleri bos").arg(model));
            auto project = [&](std::size_t vertexIndex, POINT& out) {
                return app.projectModelVertexToScreen(
                    static_cast<std::size_t>(model), vertexIndex, out);
            };
            POINT target{};
            bool have = false;
            const QJsonValue vertexValue = spec[QStringLiteral("vertex")];
            if (vertexValue.isArray() && vertexValue.toArray().size() == 2) {
                POINT a{}, b{};
                if (project(static_cast<std::size_t>(vertexValue.toArray()[0].toInt()), a) &&
                    project(static_cast<std::size_t>(vertexValue.toArray()[1].toInt()), b)) {
                    target = {a.x + (b.x - a.x) / 2, a.y + (b.y - a.y) / 2};
                    have = true;
                }
            } else if (vertexValue.isDouble()) {
                have = project(static_cast<std::size_t>(vertexValue.toInt()), target);
            } else {
                // Varsayilan: modelin EKSEN orta noktasi (axisFrom/axisTo).
                // Profil kesiti kucuk zoom'da tuval disina tasabildigi icin
                // mesh koseleri yerine kullanici tiklamalarindan gelen eksen
                // uclari daima tuval icindedir; eksendeki isin kirisin
                // govdesine carpar (H-1 yuz tiklamasi icin ideal).
                const auto& props = app.document().models()[model].properties();
                POINT a{}, b{};
                if (app.projectWorldPointToScreen(
                        {props.axisFromX, props.axisFromY, props.axisFromZ}, a) &&
                    app.projectWorldPointToScreen(
                        {props.axisToX, props.axisToY, props.axisToZ}, b)) {
                    target = {a.x + (b.x - a.x) / 2, a.y + (b.y - a.y) / 2};
                    have = true;
                } else {
                    // Eksen yoksa tum koselerin agirlik merkezi (kiris silueti).
                    long sumX = 0, sumY = 0;
                    std::size_t count = 0;
                    for (std::size_t i = 0; i < vertices.size(); ++i) {
                        POINT p{};
                        if (project(i, p)) { sumX += p.x; sumY += p.y; ++count; }
                    }
                    if (count) {
                        target = {sumX / static_cast<long>(count),
                                  sumY / static_cast<long>(count)};
                        have = true;
                    }
                }
            }
            if (!have) {
                // Tanida yardimci: tuval boyutu + ilk kosenin dunya/ekran degeri.
                RECT rc{};
                QString diag = QStringLiteral(" rect=0x0");
                if (GetClientRect(reinterpret_cast<HWND>(window.winId()), &rc))
                    diag = QStringLiteral(" rect=%1x%2").arg(rc.right).arg(rc.bottom);
                QString first = QStringLiteral("-");
                if (!vertices.empty()) {
                    POINT p{};
                    const bool ok = app.projectModelVertexToScreen(
                        static_cast<std::size_t>(model), 0, p);
                    first = QStringLiteral("v0=(%1,%2,%3)->(%4,%5) ok=%6")
                                .arg(vertices[0].x, 0, 'f', 2)
                                .arg(vertices[0].y, 0, 'f', 2)
                                .arg(vertices[0].z, 0, 'f', 2)
                                .arg(p.x).arg(p.y)
                                .arg(ok ? 1 : 0);
                }
                return fail(QStringLiteral("clickModel: model %1 izdusumu tuval disinda (%2 kose;%3; %4)")
                                .arg(model).arg(vertices.size()).arg(diag, first));
            }
            execClick(target.x, target.y);
            return writeStep(step, QStringLiteral("clickModel:%1@(%2,%3)")
                                       .arg(model).arg(target.x).arg(target.y));
        }
        if (step.contains(QStringLiteral("capture"))) {
            // Ek kanit: EN USTTEKI ModelMakerCanvas (yeni acilan gorunum pencere
            // tuvali olabilir) ya da tum ana pencere. <out>/<ad>.png.
            // String form: {"capture": "ad"} -> tuval. Object form:
            // {"capture": {"name": "ad", "target": "window"}} -> ana pencere
            // (MDI cercevesi + alt pencereler + ribbon; PrintWindow ile).
            QString name = QStringLiteral("extra");
            QString target = QStringLiteral("canvas");
            if (step[QStringLiteral("capture")].isObject()) {
                const QJsonObject cap = step[QStringLiteral("capture")].toObject();
                name = cap[QStringLiteral("name")].toString(QStringLiteral("extra"));
                target = cap[QStringLiteral("target")].toString(QStringLiteral("canvas"));
            } else {
                name = step[QStringLiteral("capture")].toString(QStringLiteral("extra"));
            }
            QImage image;
            HWND root = reinterpret_cast<HWND>(window.winId());
            if (target == QStringLiteral("window") && root && IsWindow(root)) {
                if (!captureHwnd(root, image, /*forcePrintWindow=*/true))
                    image = QImage();
            } else {
                HWND top = nullptr;
                struct Ctx { const wchar_t* klass; HWND found; } ctx{L"ModelMakerCanvas", nullptr};
                if (root && IsWindow(root)) {
                    EnumChildWindows(root,
                        [](HWND child, LPARAM param) -> BOOL {
                            auto* c = reinterpret_cast<Ctx*>(param);
                            wchar_t buffer[64]{};
                            GetClassNameW(child, buffer, 63);
                            if (wcscmp(buffer, c->klass) == 0) { c->found = child; return FALSE; }
                            return TRUE;
                        },
                        reinterpret_cast<LPARAM>(&ctx));
                    top = ctx.found;
                }
                if (top && !captureHwnd(top, image)) image = QImage();
            }
            if (image.isNull()) image = window.grab().toImage();
            if (image.isNull())
                return fail(QStringLiteral("capture: ekran görüntüsü alınamadı"));
            image.save(outDir + QStringLiteral("/") + name + QStringLiteral(".png"));
            return writeStep(step, QStringLiteral("capture:") + name);
        }
        if (step.contains(QStringLiteral("captureLive"))) {
            // H-6: motion feedback YALNIZ interaktif karelerde cizilir ve
            // onceki adimlarin zorla tam kareleri onu siler. Bu adim: imleci
            // "at" noktasina MOVE et (updateHover + snap-preview isareti),
            // UpdateWindow ile ESZAMANLI interaktif kareyi cizdir, yuzeyi
            // oldugu gibi yakala. "at" yoksa son imlec konumu kullanilir.
            QString name = QStringLiteral("extra");
            int mx = -1, my = -1;
            if (step[QStringLiteral("captureLive")].isObject()) {
                const QJsonObject cap = step[QStringLiteral("captureLive")].toObject();
                name = cap[QStringLiteral("name")].toString(QStringLiteral("extra"));
                const QJsonArray at = cap[QStringLiteral("at")].toArray();
                if (at.size() >= 2) {
                    mx = at[0].toInt();
                    my = at[1].toInt();
                }
            } else {
                name = step[QStringLiteral("captureLive")].toString(QStringLiteral("extra"));
            }
            HWND canvas = window.application().canvasHandle();
            if (mx >= 0 && my >= 0) {
                sendMouse(WM_MOUSEMOVE, 0, mx, my);
                UpdateWindow(canvas);  // eszamanli interaktif kare (fastpath)
            }
            QImage image;
            if (!captureHwndLive(canvas, image)) image = QImage();
            if (image.isNull())
                return fail(QStringLiteral("captureLive: ekran görüntüsü alınamadı"));
            image.save(outDir + QStringLiteral("/") + name + QStringLiteral(".png"));
            return writeStep(step, QStringLiteral("captureLive:") + name);
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
