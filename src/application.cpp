#include "model_maker/application.hpp"
#include "model_maker/view_cube.hpp"

#include <commdlg.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <exception>
#include <stdexcept>
#include <string>

namespace mm {
namespace {
constexpr int panelWidth = 230;
constexpr int statusHeight = 27;
constexpr wchar_t className[] = L"ModelMakerWindow";
constexpr wchar_t canvasClassName[] = L"ModelMakerCanvas";

enum CommandId {
    CmdNew = 100, CmdOpen, CmdSave,
    CmdLine = 200, CmdPolyline, CmdRectangle, CmdCircle,
    CmdCube = 300, CmdPyramid, CmdResetView,
    CmdOsnap = 400, CmdGridSnap, CmdDynamicInput
};
}

Application::Application(HINSTANCE instance) : instance_(instance) {}

Application::~Application() {
    if (uiFont_) DeleteObject(uiFont_);
    if (titleFont_) DeleteObject(titleFont_);
}

int Application::run(int showCommand) {
    createMainWindow(showCommand);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

void Application::createMainWindow(int showCommand) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    windowClass.lpszClassName = className;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        throw std::runtime_error("Main window class could not be registered");

    WNDCLASSEXW canvasClass{};
    canvasClass.cbSize = sizeof(canvasClass);
    canvasClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    canvasClass.lpfnWndProc = canvasProc;
    canvasClass.hInstance = instance_;
    canvasClass.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    canvasClass.hbrBackground = nullptr;
    canvasClass.lpszClassName = canvasClassName;
    if (!RegisterClassExW(&canvasClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        throw std::runtime_error("Drawing canvas class could not be registered");

    window_ = CreateWindowExW(0, className, L"Model Maker — Professional Wireframe CAD",
                              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                              1280, 820, nullptr, nullptr, instance_, this);
    if (!window_) throw std::runtime_error("Main window could not be created");

    createControlPanel();
    RECT client{};
    GetClientRect(window_, &client);
    layoutChildren(client.right, client.bottom);
    updateControls();
    ShowWindow(window_, showCommand);
    UpdateWindow(window_);
    SetFocus(canvas_);
}

void Application::createControlPanel() {
    uiFont_ = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    titleFont_ = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    HWND title = CreateWindowExW(0, L"STATIC", L"MODEL MAKER", WS_CHILD | WS_VISIBLE | SS_CENTER,
                                  12, 12, 204, 29, window_, nullptr, instance_, nullptr);
    SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont_), TRUE);

    auto group = [&](const wchar_t* text, int y, int height) {
        HWND box = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                    10, y, 210, height, window_, nullptr, instance_, nullptr);
        SendMessageW(box, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    };

    group(L"Dosya", 48, 82);
    createButton(L"Yeni", CmdNew, 20, 70, 58, 42);
    createButton(L"Aç...", CmdOpen, 84, 70, 58, 42);
    createButton(L"Kaydet", CmdSave, 148, 70, 62, 42);

    group(L"Çizim Araçları", 140, 154);
    lineButton_ = createButton(L"Çizgi  (L)", CmdLine, 20, 164, 88, 48, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    polylineButton_ = createButton(L"Polyline  (P)", CmdPolyline, 114, 164, 96, 48, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    rectangleButton_ = createButton(L"Dikdörtgen", CmdRectangle, 20, 218, 88, 58, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    circleButton_ = createButton(L"Daire  (C)", CmdCircle, 114, 218, 96, 58, BS_AUTOCHECKBOX | BS_PUSHLIKE);

    group(L"3B Model ve Görünüm", 304, 132);
    createButton(L"Küp  (B)", CmdCube, 20, 328, 88, 44);
    createButton(L"Piramit  (Y)", CmdPyramid, 114, 328, 96, 44);
    createButton(L"Görünümü Sıfırla  (R)", CmdResetView, 20, 378, 190, 40);

    group(L"Çizim Yardımcıları", 446, 158);
    snapButton_ = createButton(L"OSNAP   F3", CmdOsnap, 20, 471, 190, 34, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    gridSnapButton_ = createButton(L"GRID SNAP   F9", CmdGridSnap, 20, 511, 190, 34, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    dynamicInputButton_ = createButton(L"DYNAMIC INPUT   F12", CmdDynamicInput, 20, 551, 190, 34, BS_AUTOCHECKBOX | BS_PUSHLIKE);

    HWND hint = CreateWindowExW(0, L"STATIC",
        L"Canvas: çizim alanı\r\nOrta tuş: döndür\r\nTekerlek: yakınlaştır\r\nEsc / sağ tık: bitir",
        WS_CHILD | WS_VISIBLE, 20, 620, 190, 78, window_, nullptr, instance_, nullptr);
    SendMessageW(hint, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);

    canvas_ = CreateWindowExW(WS_EX_CLIENTEDGE, canvasClassName, nullptr,
                              WS_CHILD | WS_VISIBLE | WS_TABSTOP, panelWidth, 0, 800, 600,
                              window_, nullptr, instance_, this);
    status_ = CreateWindowExW(0, L"STATIC", nullptr,
                              WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_SUNKEN,
                              0, 700, 1000, statusHeight, window_, nullptr, instance_, nullptr);
    SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
}

HWND Application::createButton(const wchar_t* text, int id, int x, int y, int width, int height, DWORD style) {
    HWND button = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
                                   x, y, width, height, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   instance_, nullptr);
    SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    return button;
}

void Application::layoutChildren(int width, int height) {
    if (!canvas_ || !status_) return;
    const int contentHeight = std::max(1, height - statusHeight);
    MoveWindow(canvas_, panelWidth, 0, std::max(1, width - panelWidth), contentHeight, TRUE);
    MoveWindow(status_, 0, contentHeight, width, statusHeight, TRUE);
}

LRESULT CALLBACK Application::windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    Application* app = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = static_cast<Application*>(create->lpCreateParams);
        app->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    } else {
        app = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    }
    return app ? app->handleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK Application::canvasProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    Application* app = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = static_cast<Application*>(create->lpCreateParams);
        app->canvas_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    } else {
        app = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    }
    return app ? app->handleCanvasMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Application::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED) executeCommand(LOWORD(wParam));
        return 0;
    case WM_SIZE:
        layoutChildren(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_SETFOCUS:
        if (canvas_) SetFocus(canvas_);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window_, message, wParam, lParam);
    }
}

LRESULT Application::handleCanvasMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: onCanvasPaint(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_LBUTTONDOWN: SetFocus(canvas_); onLeftButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
    case WM_RBUTTONDOWN:
        SetFocus(canvas_); cancelDrawing();
        if (mode_ == EditMode::View3D) drawingActive_ = false;
        updateControls(); invalidateCanvas(); return 0;
    case WM_MBUTTONDOWN:
        SetFocus(canvas_);
        if (mode_ == EditMode::View3D) {
            rotating_ = true; lastMouse_ = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; SetCapture(canvas_);
        }
        return 0;
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
        rotating_ = false; ReleaseCapture(); return 0;
    case WM_CAPTURECHANGED:
        rotating_ = false; return 0;
    case WM_MOUSEMOVE: onMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam); return 0;
    case WM_MOUSEWHEEL:
        if (mode_ == EditMode::View3D) {
            camera_.zoomBy(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1.12 : 1.0 / 1.12);
            invalidateCanvas();
        }
        return 0;
    case WM_CHAR: onCharacter(static_cast<wchar_t>(wParam)); return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            input_.clear(); cancelDrawing();
            if (mode_ == EditMode::View3D) drawingActive_ = false;
        } else if (wParam == VK_F3) snapEnabled_ = !snapEnabled_;
        else if (wParam == VK_F9) gridSnapEnabled_ = !gridSnapEnabled_;
        else if (wParam == VK_F12) dynamicInputEnabled_ = !dynamicInputEnabled_;
        else if (wParam == 'L') selectTool(DrawTool::Line);
        else if (wParam == 'P') selectTool(DrawTool::Polyline);
        else if (wParam == 'A') selectTool(DrawTool::Rectangle);
        else if (wParam == 'C') selectTool(DrawTool::Circle);
        else if (wParam == 'B') addCube();
        else if (wParam == 'Y') addPyramid();
        else if (wParam == 'R' && mode_ == EditMode::View3D) camera_.reset();
        else if (wParam == VK_DELETE) { document_.clear(); cancelDrawing(); }
        else if (wParam == 'S' && (GetKeyState(VK_CONTROL) & 0x8000)) saveDocument();
        else if (wParam == 'O' && (GetKeyState(VK_CONTROL) & 0x8000)) openDocument();
        updateHover(cursorScreen_.x, cursorScreen_.y); updateControls(); invalidateCanvas();
        return 0;
    case WM_SIZE: invalidateCanvas(); return 0;
    default: return DefWindowProcW(canvas_, message, wParam, lParam);
    }
}

void Application::onCanvasPaint() {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(canvas_, &paint);
    RECT client{};
    GetClientRect(canvas_, &client);
    renderer_.draw(dc, client, document_, camera_, mode_, draftView());
    EndPaint(canvas_, &paint);
}

void Application::onLeftButtonDown(int x, int y) {
    RECT client{};
    GetClientRect(canvas_, &client);
    if (mode_ == EditMode::View3D) {
        if (const auto view = ViewCube::hitTest(x, y, client.right)) {
            camera_.setView(*view); rotating_ = false; invalidateCanvas(); return;
        }
    }
    const bool drafting = mode_ == EditMode::Draw2D || drawingActive_;
    if (drafting) {
        updateHover(x, y);
        if (hover_) commitPoint(hover_->point);
    } else if (mode_ == EditMode::View3D) {
        rotating_ = true; lastMouse_ = {x, y}; SetCapture(canvas_);
    }
    updateStatus(); invalidateCanvas();
}

void Application::onMouseMove(int x, int y, WPARAM buttons) {
    cursorScreen_ = {x, y};
    if (rotating_ && (buttons & (MK_LBUTTON | MK_MBUTTON)) && mode_ == EditMode::View3D) {
        camera_.rotate((x - lastMouse_.x) * 0.008, (y - lastMouse_.y) * 0.008);
        lastMouse_ = {x, y};
        if (drawingActive_) updateHover(x, y);
    } else if (mode_ == EditMode::Draw2D || drawingActive_) {
        updateHover(x, y);
    }
    invalidateCanvas();
}

void Application::onCharacter(wchar_t character) {
    if ((mode_ != EditMode::Draw2D && !drawingActive_) || !dynamicInputEnabled_) return;
    if (character == L'\r') {
        if (!input_.empty()) {
            if (const auto point = parseDynamicPoint(input_, anchor_)) { commitPoint(*point); input_.clear(); }
            else MessageBeep(MB_ICONWARNING);
        } else if (tool_ == DrawTool::Polyline) cancelDrawing();
    } else if (character == L'\b') {
        if (!input_.empty()) input_.pop_back();
    } else if ((character >= L'0' && character <= L'9') || character == L'-' || character == L'+' ||
               character == L'.' || character == L',' || character == L'<') input_.push_back(character);
    updateStatus(); invalidateCanvas();
}

void Application::commitPoint(const Vec3& point) {
    if (!anchor_) { anchor_ = point; workPlaneZ_ = point.z; return; }
    const Vec3 start = *anchor_;
    switch (tool_) {
    case DrawTool::Line:
        if (point != start) document_.addLine(start, point);
        anchor_.reset();
        break;
    case DrawTool::Polyline:
        if (point != start) document_.addLine(start, point);
        anchor_ = point;
        break;
    case DrawTool::Rectangle:
        if (point != start) document_.addModel(WireframeModel::rectangle(start, point));
        anchor_.reset();
        break;
    case DrawTool::Circle: {
        const double radius = std::hypot(point.x - start.x, point.y - start.y);
        if (radius > 0.0) document_.addModel(WireframeModel::circle(start, radius));
        anchor_.reset(); break;
    }}
}

void Application::cancelDrawing() { anchor_.reset(); input_.clear(); }

void Application::updateHover(int x, int y) {
    if (!canvas_) return;
    cursorScreen_ = {x, y};
    if (mode_ == EditMode::Draw2D) {
        hover_ = SnapEngine::snap(screenTo2D(x, y), document_, 10.0 / 60.0, 1.0,
                                  snapEnabled_, gridSnapEnabled_);
    } else {
        RECT client{}; GetClientRect(canvas_, &client);
        const int width = std::max(1L, client.right);
        const int height = std::max(1L, client.bottom);
        const double planeZ = anchor_ ? anchor_->z : workPlaneZ_;
        hover_ = SnapEngine::snap3D({static_cast<double>(x), static_cast<double>(y)}, document_, camera_,
                                    width, height, 10.0, 1.0, planeZ, snapEnabled_, gridSnapEnabled_);
    }
}

void Application::executeCommand(int id) {
    switch (id) {
    case CmdNew: document_.clear(); cancelDrawing(); mode_ = EditMode::Draw2D; drawingActive_ = true; break;
    case CmdOpen: openDocument(); break;
    case CmdSave: saveDocument(); break;
    case CmdLine: selectTool(DrawTool::Line); break;
    case CmdPolyline: selectTool(DrawTool::Polyline); break;
    case CmdRectangle: selectTool(DrawTool::Rectangle); break;
    case CmdCircle: selectTool(DrawTool::Circle); break;
    case CmdCube: addCube(); break;
    case CmdPyramid: addPyramid(); break;
    case CmdResetView: camera_.reset(); break;
    case CmdOsnap: snapEnabled_ = !snapEnabled_; break;
    case CmdGridSnap: gridSnapEnabled_ = !gridSnapEnabled_; break;
    case CmdDynamicInput: dynamicInputEnabled_ = !dynamicInputEnabled_; break;
    default: break;
    }
    updateHover(cursorScreen_.x, cursorScreen_.y);
    updateControls(); invalidateCanvas(); SetFocus(canvas_);
}

void Application::selectTool(DrawTool tool) {
    tool_ = tool; cancelDrawing(); drawingActive_ = true;
    updateHover(cursorScreen_.x, cursorScreen_.y); updateControls(); invalidateCanvas();
}

void Application::updateControls() {
    const auto check = [](HWND control, bool value) {
        if (control) SendMessageW(control, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
    };
    check(lineButton_, drawingActive_ && tool_ == DrawTool::Line);
    check(polylineButton_, drawingActive_ && tool_ == DrawTool::Polyline);
    check(rectangleButton_, drawingActive_ && tool_ == DrawTool::Rectangle);
    check(circleButton_, drawingActive_ && tool_ == DrawTool::Circle);
    check(snapButton_, snapEnabled_);
    check(gridSnapButton_, gridSnapEnabled_);
    check(dynamicInputButton_, dynamicInputEnabled_);
    updateStatus();
}

void Application::updateStatus() {
    if (!status_) return;
    std::wstring text = L"   ";
    text += mode_ == EditMode::View3D ? L"3B Paralel" : L"2B";
    text += L"  |  Araç: "; text += toolLabel(tool_);
    text += L"  |  OSNAP: "; text += snapEnabled_ ? L"Açık" : L"Kapalı";
    text += L"  |  GRID: "; text += gridSnapEnabled_ ? L"Açık" : L"Kapalı";
    text += L"  |  DYN: "; text += dynamicInputEnabled_ ? L"Açık" : L"Kapalı";
    if (hover_) {
        wchar_t coordinates[96]{};
        std::swprintf(coordinates, std::size(coordinates), L"  |  X %.3f  Y %.3f  Z %.3f",
                      hover_->point.x, hover_->point.y, hover_->point.z);
        text += coordinates;
    }
    SetWindowTextW(status_, text.c_str());
}

void Application::invalidateCanvas() { if (canvas_) InvalidateRect(canvas_, nullptr, FALSE); }

void Application::addCube() {
    auto cube = WireframeModel::cube(2.6);
    cube.translate({static_cast<double>(document_.models().size() % 4) * 0.45, 0.0, 0.0});
    document_.addModel(std::move(cube)); mode_ = EditMode::View3D;
    cancelDrawing(); drawingActive_ = false;
}

void Application::addPyramid() {
    auto pyramid = WireframeModel::pyramid(3.0, 3.2); pyramid.translate({0.0, 0.0, -1.0});
    document_.addModel(std::move(pyramid)); mode_ = EditMode::View3D;
    cancelDrawing(); drawingActive_ = false;
}

Vec3 Application::screenTo2D(int x, int y) const noexcept {
    RECT client{}; GetClientRect(canvas_, &client);
    return {(x - client.right * 0.5) / 60.0, (client.bottom * 0.5 - y) / 60.0, 0.0};
}

DraftView Application::draftView() const {
    DraftView view;
    view.tool = tool_; view.anchor = anchor_;
    if (hover_) { view.cursor = hover_->point; view.snapType = hover_->type; }
    view.drawingActive = drawingActive_; view.snapEnabled = snapEnabled_;
    view.gridSnapEnabled = gridSnapEnabled_; view.dynamicInputEnabled = dynamicInputEnabled_;
    view.workPlaneZ = anchor_ ? anchor_->z : workPlaneZ_; view.input = input_; view.cursorScreen = cursorScreen_;
    return view;
}

std::optional<std::filesystem::path> Application::chooseFile(bool save) const {
    wchar_t filename[MAX_PATH]{};
    OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = window_;
    dialog.lpstrFilter = L"Model Maker Wireframe (*.mmw)\0*.mmw\0Tüm dosyalar (*.*)\0*.*\0";
    dialog.lpstrFile = filename; dialog.nMaxFile = MAX_PATH; dialog.lpstrDefExt = L"mmw";
    dialog.Flags = OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const BOOL accepted = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
    if (!accepted) return std::nullopt;
    return std::filesystem::path(filename);
}

void Application::saveDocument() {
    const auto path = chooseFile(true); if (!path) return;
    try { document_.save(*path); } catch (const std::exception& error) { showError(L"Kaydetme başarısız", error); }
}

void Application::openDocument() {
    const auto path = chooseFile(false); if (!path) return;
    try { document_.load(*path); cancelDrawing(); } catch (const std::exception& error) { showError(L"Dosya açılamadı", error); }
}

void Application::showError(const wchar_t* action, const std::exception& error) const {
    std::wstring message(action); message += L".\n\n";
    const std::string detail = error.what(); message.append(detail.begin(), detail.end());
    MessageBoxW(window_, message.c_str(), L"Model Maker", MB_OK | MB_ICONERROR);
}

} // namespace mm
