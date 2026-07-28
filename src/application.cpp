#include "model_maker/application.hpp"
#include "model_maker/dxf.hpp"
#include "model_maker/view_cube.hpp"

#include <commdlg.h>
#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <exception>
#include <stdexcept>
#include <string>

namespace mm {
namespace {
constexpr int ribbonHeight = RibbonLayout::height;
constexpr int statusHeight = 27;
constexpr wchar_t className[] = L"ModelMakerWindow";
constexpr wchar_t canvasClassName[] = L"ModelMakerCanvas";

enum CommandId {
    CmdNew = 100, CmdOpen, CmdSave, CmdImportDxf, CmdExportDxf,
    CmdLine = 200, CmdPolyline, CmdRectangle, CmdCircle,
    CmdCube = 300, CmdPyramid, CmdResetView, CmdView3D, CmdWorkPlane, CmdZoomExtents, CmdZoomWindow,
    CmdOsnap = 400, CmdGridSnap, CmdDynamicInput,
    CmdMove = 500, CmdCopy,
    CmdTabFile = 600, CmdTabDrawing, CmdTabModify, CmdTabView, CmdTabAids,
    CmdDxfProgress = 700
};

HCURSOR createSquarePickboxCursor(HINSTANCE instance) {
    constexpr int cursorSize = 32;
    constexpr int bytesPerRow = cursorSize / 8;
    BYTE andMask[cursorSize * bytesPerRow];
    BYTE xorMask[cursorSize * bytesPerRow]{};
    std::fill(std::begin(andMask), std::end(andMask), static_cast<BYTE>(0xFF));

    const auto setPixel = [&](int x, int y) {
        xorMask[y * bytesPerRow + x / 8] |= static_cast<BYTE>(0x80u >> (x % 8));
    };
    constexpr int left = 10;
    constexpr int right = 20;
    constexpr int top = 10;
    constexpr int bottom = 20;
    for (int x = left; x <= right; ++x) {
        setPixel(x, top);
        setPixel(x, bottom);
    }
    for (int y = top + 1; y < bottom; ++y) {
        setPixel(left, y);
        setPixel(right, y);
    }
    return CreateCursor(instance, 15, 15, cursorSize, cursorSize, andMask, xorMask);
}
}

Application::Application(HINSTANCE instance) : instance_(instance) {}

Application::~Application() {
    if (dxfImportThread_.joinable()) {
        dxfImportThread_.request_stop();
        dxfImportThread_.join();
    }
    if (modifyCursor_) DestroyCursor(modifyCursor_);
    if (uiFont_) DeleteObject(uiFont_);
    if (titleFont_) DeleteObject(titleFont_);
}

int Application::run(int showCommand, std::optional<std::filesystem::path> startupDxf) {
    createMainWindow(showCommand);
    if (startupDxf && startupDxf->extension() == L".dxf") beginDxfImport(*startupDxf);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

void Application::createMainWindow(int showCommand) {
    draftingCursor_ = LoadCursorW(nullptr, IDC_CROSS);
    modifyCursor_ = createSquarePickboxCursor(instance_);
    if (!draftingCursor_ || !modifyCursor_)
        throw std::runtime_error("Drawing cursors could not be created");

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
    canvasClass.hCursor = draftingCursor_;
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
    INITCOMMONCONTROLSEX commonControls{sizeof(commonControls), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&commonControls);
    uiFont_ = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    titleFont_ = CreateFontW(-18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    const auto addTab = [&](const wchar_t* text, int id, DWORD extra = 0) {
        HWND tab = createButton(text, id, 0, 0, 82, 30,
                                BS_AUTORADIOBUTTON | BS_PUSHLIKE | extra);
        ribbonTabButtons_.push_back(tab);
        return tab;
    };
    addTab(L"Dosya", CmdTabFile, WS_GROUP);
    addTab(L"Çizim", CmdTabDrawing);
    addTab(L"Düzenle", CmdTabModify);
    addTab(L"Görünüm", CmdTabView);
    addTab(L"Yardımcılar", CmdTabAids);

    const auto addCommand = [&](const wchar_t* text, int id, DWORD style = BS_PUSHBUTTON) {
        HWND button = createButton(text, id, 0, 0, 64, 57, style | BS_MULTILINE);
        ribbonCommandButtons_.push_back(button);
        return button;
    };

    addCommand(L"＋\r\nYeni", CmdNew);
    addCommand(L"▣\r\nAç", CmdOpen);
    addCommand(L"▤\r\nKaydet", CmdSave);
    addCommand(L"⇩\r\nDXF Aç", CmdImportDxf);
    addCommand(L"⇧\r\nDXF Yaz", CmdExportDxf);

    lineButton_ = addCommand(L"╱\r\nÇizgi", CmdLine, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    polylineButton_ = addCommand(L"⌁\r\nPolyline", CmdPolyline, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    rectangleButton_ = addCommand(L"□\r\nDikdört.", CmdRectangle, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    circleButton_ = addCommand(L"○\r\nDaire", CmdCircle, BS_AUTOCHECKBOX | BS_PUSHLIKE);

    addCommand(L"◇\r\nKüp", CmdCube);
    addCommand(L"△\r\nPiramit", CmdPyramid);
    addCommand(L"⌂\r\nSıfırla", CmdResetView);
    view3DButton_ = addCommand(L"3B\r\n2B / 3B", CmdView3D, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    workPlaneButton_ = addCommand(L"▱\r\nDüzlem", CmdWorkPlane, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    addCommand(L"⤢\r\nExtents", CmdZoomExtents);
    zoomWindowButton_ = addCommand(L"⌗\r\nPencere", CmdZoomWindow, BS_AUTOCHECKBOX | BS_PUSHLIKE);

    snapButton_ = addCommand(L"◎\r\nOSNAP F3", CmdOsnap, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    gridSnapButton_ = addCommand(L"#\r\nGrid F9", CmdGridSnap, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    dynamicInputButton_ = addCommand(L"123\r\nDinamik", CmdDynamicInput, BS_AUTOCHECKBOX | BS_PUSHLIKE);

    moveButton_ = addCommand(L"↔\r\nTaşı", CmdMove, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    copyButton_ = addCommand(L"⧉\r\nKopyala", CmdCopy, BS_AUTOCHECKBOX | BS_PUSHLIKE);

    canvas_ = CreateWindowExW(WS_EX_CLIENTEDGE, canvasClassName, nullptr,
                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS,
                              0, ribbonHeight, 800, 600, window_, nullptr, instance_, this);
    status_ = CreateWindowExW(0, L"STATIC", nullptr,
                              WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_LEFT | SS_CENTERIMAGE | SS_SUNKEN,
                              0, 700, 1000, statusHeight, window_, nullptr, instance_, nullptr);
    SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    dxfProgressBar_ = CreateWindowExW(0, PROGRESS_CLASSW, nullptr,
                                      WS_CHILD | WS_CLIPSIBLINGS | PBS_SMOOTH,
                                      0, 0, 280, statusHeight - 6, window_,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(CmdDxfProgress)), instance_, nullptr);
    SendMessageW(dxfProgressBar_, PBM_SETRANGE32, 0, 100);
    activateRibbonTab(RibbonTab::Drawing);
}

HWND Application::createButton(const wchar_t* text, int id, int x, int y, int width, int height, DWORD style) {
    HWND button = CreateWindowExW(0, L"BUTTON", text,
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | style,
                                   x, y, width, height, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   instance_, nullptr);
    SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    return button;
}

void Application::layoutChildren(int width, int height) {
    if (!canvas_ || !status_) return;
    const int contentHeight = std::max(ribbonHeight + 1, height - statusHeight);
    MoveWindow(canvas_, 0, ribbonHeight, std::max(1, width),
               std::max(1, contentHeight - ribbonHeight), TRUE);
    const bool showProgress = dxfImportInProgress_ && dxfProgressBar_;
    const int progressWidth = std::min(300, std::max(180, width / 4));
    MoveWindow(status_, 0, contentHeight, showProgress ? std::max(1, width - progressWidth - 6) : width,
               statusHeight, TRUE);
    if (dxfProgressBar_) {
        MoveWindow(dxfProgressBar_, std::max(0, width - progressWidth), contentHeight + 3,
                   progressWidth - 4, statusHeight - 6, TRUE);
        ShowWindow(dxfProgressBar_, showProgress ? SW_SHOW : SW_HIDE);
    }

    for (std::size_t i = 0; i < ribbonTabButtons_.size(); ++i)
        MoveWindow(ribbonTabButtons_[i], 10 + static_cast<int>(i) * 88, 5, 84, 29, TRUE);
    const auto geometry = RibbonLayout::layout(activeRibbonTab_, width);
    for (const auto& item : geometry.commandButtons) {
        if (HWND button = GetDlgItem(window_, item.commandId)) {
            const auto& r = item.rect;
            MoveWindow(button, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
        }
    }
}

void Application::activateRibbonTab(RibbonTab tab) {
    activeRibbonTab_ = tab;
    for (HWND button : ribbonCommandButtons_) ShowWindow(button, SW_HIDE);
    const auto geometry = RibbonLayout::layout(tab, window_ ? [] (HWND window) {
        RECT client{}; GetClientRect(window, &client); return std::max(1L, client.right);
    }(window_) : 1280);
    for (const auto& item : geometry.commandButtons) {
        if (HWND button = GetDlgItem(window_, item.commandId)) {
            const auto& r = item.rect;
            MoveWindow(button, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
            ShowWindow(button, SW_SHOW);
        }
    }
    for (std::size_t i = 0; i < ribbonTabButtons_.size(); ++i)
        SendMessageW(ribbonTabButtons_[i], BM_SETCHECK,
                     i == static_cast<std::size_t>(tab) ? BST_CHECKED : BST_UNCHECKED, 0);
    if (window_) InvalidateRect(window_, nullptr, TRUE);
}

void Application::paintRibbon() {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window_, &paint);
    RECT client{};
    GetClientRect(window_, &client);
    RECT ribbon{0, 0, client.right, ribbonHeight};
    FillRect(dc, &ribbon, GetSysColorBrush(COLOR_BTNFACE));
    RECT commandBand{0, 36, client.right, ribbonHeight};
    FillRect(dc, &commandBand, GetSysColorBrush(COLOR_3DFACE));
    DrawEdge(dc, &commandBand, EDGE_ETCHED, BF_TOP | BF_BOTTOM);

    const auto oldFont = SelectObject(dc, titleFont_);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(55, 65, 78));
    RECT title{std::max(455L, client.right - 210), 4, client.right - 14, 34};
    DrawTextW(dc, L"MODEL MAKER", -1, &title, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    SelectObject(dc, oldFont);
    EndPaint(window_, &paint);
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
    case WM_PAINT:
        paintRibbon();
        return 0;
    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED) executeCommand(LOWORD(wParam));
        return 0;
    case WM_APP + 2:
        finishDxfImport();
        return 0;
    case WM_TIMER:
        if (wParam == 4) {
            KillTimer(window_, 4);
            if (!snapPreviewActive_) invalidateCanvas();
            return 0;
        }
        if (wParam == 3) {
            KillTimer(window_, 3);
            snapPreviewTimerArmed_ = false;
            if (snapPreviewActive_) invalidateCanvas();
            return 0;
        }
        if (wParam == 2) {
            KillTimer(window_, 2);
            wheelNavigating_ = false;
            wheelPreviewFactor_ = 1.0;
            wheelPreviewOffset_ = {};
            invalidateCanvas();
            return 0;
        }
        if (wParam == 1 && dxfImportInProgress_) {
            const auto total = dxfTotalBytes_.load();
            const auto completed = dxfBytesRead_.load();
            const auto percent = total ? std::min<std::uint64_t>(100, completed * 100 / total) : 0;
            if (dxfProgressBar_) SendMessageW(dxfProgressBar_, PBM_SETPOS, percent, 0);
            updateStatus();
        }
        return 0;
    case WM_SIZE:
        layoutChildren(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_SETFOCUS:
        if (canvas_) SetFocus(canvas_);
        return 0;
    case WM_DESTROY:
        if (dxfImportThread_.joinable()) dxfImportThread_.request_stop();
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window_, message, wParam, lParam);
    }
}

LRESULT Application::handleCanvasMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_APP + 1: return reinterpret_cast<LRESULT>(GetCursor()); // GUI smoke-test probe.
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            SetCursor(transformCommand_ != TransformCommand::None ? modifyCursor_ : draftingCursor_);
            return TRUE;
        }
        return DefWindowProcW(canvas_, message, wParam, lParam);
    case WM_PAINT: onCanvasPaint(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_LBUTTONDOWN: SetFocus(canvas_); onLeftButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
    case WM_RBUTTONDOWN:
        SetFocus(canvas_);
        if (zoomWindowActive_) cancelZoomWindow2D();
        else if (workPlanePicking_) cancelWorkPlaneCommand();
        else if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
        else cancelDrawing();
        if (mode_ == EditMode::View3D) drawingActive_ = false;
        updateControls(); invalidateCanvas(); return 0;
    case WM_MBUTTONDOWN:
        SetFocus(canvas_);
        if (mode_ == EditMode::View3D) {
            rotating_ = true; lastMouse_ = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; SetCapture(canvas_);
        } else {
            panning2D_ = true; lastMouse_ = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; SetCapture(canvas_);
        }
        return 0;
    case WM_LBUTTONUP: onLeftButtonUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
    case WM_MBUTTONUP:
        rotating_ = false; panning2D_ = false; ReleaseCapture(); invalidateCanvas(); return 0;
    case WM_CAPTURECHANGED:
        rotating_ = false;
        panning2D_ = false;
        if (reinterpret_cast<HWND>(lParam) != canvas_) {
            viewCubeManipulating_ = false;
            viewCubePressedView_.reset();
        }
        return 0;
    case WM_MOUSEMOVE: onMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam); return 0;
    case WM_MOUSEWHEEL:
        {
        POINT zoomCursor{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(canvas_, &zoomCursor);
        cursorScreen_ = zoomCursor;
        RECT zoomClient{}; GetClientRect(canvas_, &zoomClient);
        const double factor = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1.12 : 1.0 / 1.12;
        if (!wheelNavigating_) { wheelPreviewFactor_ = 1.0; wheelPreviewOffset_ = {}; }
        if (mode_ == EditMode::Draw2D)
            camera_.zoom2DAt({static_cast<double>(zoomCursor.x), static_cast<double>(zoomCursor.y)}, factor,
                             std::max(1L, zoomClient.right), std::max(1L, zoomClient.bottom));
        else
            camera_.zoom3DAt({static_cast<double>(zoomCursor.x), static_cast<double>(zoomCursor.y)}, factor,
                             std::max(1L, zoomClient.right), std::max(1L, zoomClient.bottom));
        wheelNavigating_ = true;
        wheelPreviewOffset_.x = factor * wheelPreviewOffset_.x + (1.0 - factor) * zoomCursor.x;
        wheelPreviewOffset_.y = factor * wheelPreviewOffset_.y + (1.0 - factor) * zoomCursor.y;
        wheelPreviewFactor_ *= factor;
        if (mode_ == EditMode::Draw2D || drawingActive_ || workPlanePicking_ ||
            (transformCommand_ != TransformCommand::None && transformPhase_ != TransformPhase::Selecting))
            updateHover(cursorScreen_.x, cursorScreen_.y);
        else hover_.reset();
        SetTimer(window_, 2, 350, nullptr);
        invalidateCanvas();
        }
        return 0;
    case WM_CHAR: onCharacter(static_cast<wchar_t>(wParam)); return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            input_.clear();
            if (zoomWindowActive_) cancelZoomWindow2D();
            else if (workPlanePicking_) cancelWorkPlaneCommand();
            else if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
            else cancelDrawing();
            if (mode_ == EditMode::View3D) drawingActive_ = false;
        } else if (wParam == VK_F3) snapEnabled_ = !snapEnabled_;
        else if (wParam == VK_F8) orthoEnabled_ = !orthoEnabled_;
        else if (wParam == VK_F9) gridSnapEnabled_ = !gridSnapEnabled_;
        else if (wParam == VK_F12) dynamicInputEnabled_ = !dynamicInputEnabled_;
        else if (wParam == 'L') selectTool(DrawTool::Line);
        else if (wParam == 'P') selectTool(DrawTool::Polyline);
        else if (wParam == 'A') selectTool(DrawTool::Rectangle);
        else if (wParam == 'C') selectTool(DrawTool::Circle);
        else if (wParam == 'M') startTransformCommand(TransformCommand::Move);
        else if (wParam == 'K') startTransformCommand(TransformCommand::Copy);
        else if (wParam == 'V') toggle3DView();
        else if (wParam == 'W') startWorkPlaneCommand();
        else if (wParam == 'B') addCube();
        else if (wParam == 'Y') addPyramid();
        else if (wParam == 'R' && mode_ == EditMode::View3D) camera_.reset();
        else if (wParam == VK_DELETE) {
            document_.clear();
            if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
            else cancelDrawing();
        }
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
    const bool wasSnapPreview = snapPreviewActive_;
    renderer_.draw(dc, client, document_, camera_, mode_, draftView());
    snapPreviewActive_ = false;
    if (wasSnapPreview) SetTimer(window_, 4, 150, nullptr);
    EndPaint(canvas_, &paint);
}

void Application::onLeftButtonDown(int x, int y) {
    RECT client{};
    GetClientRect(canvas_, &client);
    if (zoomWindowActive_ && mode_ == EditMode::Draw2D) {
        if (zoomWindowFirstCorner_) completeZoomWindow2D(x, y);
        else zoomWindowFirstCorner_ = POINT{x, y};
        updateControls(); invalidateCanvas(); return;
    }
    if (mode_ == EditMode::View3D) {
        if (ViewCube::containsWidget(x, y, client.right)) {
            viewCubeManipulating_ = true;
            viewCubeDragged_ = false;
            viewCubePressedView_ = ViewCube::hitTest(x, y, client.right, camera_);
            lastMouse_ = {x, y};
            rotating_ = false;
            SetCapture(canvas_);
            return;
        }
    }
    if (workPlanePicking_) {
        updateHover(x, y);
        if (hover_) commitWorkPlanePoint(hover_->point);
        updateControls(); invalidateCanvas(); return;
    }
    if (transformCommand_ != TransformCommand::None) {
        if (transformPhase_ == TransformPhase::Selecting) {
            if (selectionFirstCorner_) completeWindowSelection(x, y);
            else if (!toggleModelSelection(x, y)) selectionFirstCorner_ = POINT{x, y};
        } else {
            updateHover(x, y);
            if (hover_) commitTransformPoint(hover_->point);
        }
        updateStatus(); invalidateCanvas(); return;
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

void Application::onLeftButtonUp(int x, int y) {
    if (viewCubeManipulating_) {
        RECT client{};
        GetClientRect(canvas_, &client);
        if (!viewCubeDragged_ && viewCubePressedView_) {
            const auto releasedView = ViewCube::hitTest(x, y, client.right, camera_);
            if (releasedView == viewCubePressedView_) camera_.setView(*viewCubePressedView_);
        }
        viewCubeManipulating_ = false;
        viewCubePressedView_.reset();
        ReleaseCapture();
        updateHover(x, y);
        invalidateCanvas();
        return;
    }
    rotating_ = false;
    ReleaseCapture();
    invalidateCanvas();
}

void Application::onMouseMove(int x, int y, WPARAM buttons) {
    cursorScreen_ = {x, y};
    bool redraw = false;
    bool snapRedraw = false;
    if (viewCubeManipulating_ && (buttons & MK_LBUTTON) && mode_ == EditMode::View3D) {
        const int dx = x - lastMouse_.x;
        const int dy = y - lastMouse_.y;
        if (dx != 0 || dy != 0) {
            camera_.rotate(dx * 0.008, dy * 0.008);
            viewCubeDragged_ = true;
            lastMouse_ = {x, y};
            if (drawingActive_) updateHover(x, y);
            redraw = true;
        }
    } else if (rotating_ && (buttons & (MK_LBUTTON | MK_MBUTTON)) && mode_ == EditMode::View3D) {
        const int dx = x - lastMouse_.x;
        const int dy = y - lastMouse_.y;
        if (dx != 0 || dy != 0) {
            camera_.rotate(dx * 0.008, dy * 0.008);
            lastMouse_ = {x, y};
            if (drawingActive_) updateHover(x, y);
            redraw = true;
        }
    } else if (panning2D_ && (buttons & MK_MBUTTON) && mode_ == EditMode::Draw2D) {
        camera_.pan2DByPixels(x - lastMouse_.x, y - lastMouse_.y);
        lastMouse_ = {x, y};
        updateHover(x, y);
        redraw = true;
    } else if (zoomWindowActive_ ||
               (transformCommand_ != TransformCommand::None && transformPhase_ == TransformPhase::Selecting) ||
               workPlanePicking_ || mode_ == EditMode::Draw2D || drawingActive_ ||
               (transformCommand_ != TransformCommand::None && transformPhase_ != TransformPhase::Selecting)) {
        const bool largeSnapTracking = document_.models().size() > 5'000;
        const auto now = std::chrono::steady_clock::now();
        if (!largeSnapTracking || lastLargeSnapEvaluation_.time_since_epoch().count() == 0 ||
            now - lastLargeSnapEvaluation_ >= std::chrono::milliseconds(50)) {
            updateHover(x, y);
            if (largeSnapTracking) lastLargeSnapEvaluation_ = now;
        }
        redraw = true;
        snapRedraw = true;
    }
    if (redraw) {
        if (snapRedraw && document_.models().size() > 5'000) {
            KillTimer(window_, 4);
            snapPreviewActive_ = true;
            if (!snapPreviewTimerArmed_) {
                SetTimer(window_, 3, 50, nullptr);
                snapPreviewTimerArmed_ = true;
            }
        } else invalidateCanvas();
    }
}

void Application::onCharacter(wchar_t character) {
    const bool transforming = transformCommand_ != TransformCommand::None;
    if (!transforming && ((mode_ != EditMode::Draw2D && !drawingActive_) || !dynamicInputEnabled_)) return;
    if (character == L'\r') {
        if (transforming && transformPhase_ == TransformPhase::Selecting) {
            if (selectionFirstCorner_) selectionFirstCorner_.reset();
            else if (!selectedModels_.empty()) transformPhase_ = TransformPhase::BasePoint;
            else MessageBeep(MB_ICONWARNING);
        } else if (transforming && transformPhase_ == TransformPhase::Destination && input_.empty() &&
                   transformCommand_ == TransformCommand::Copy) {
            cancelTransformCommand();
        } else
        if (!input_.empty()) {
            const auto origin = transforming ? transformBase_ : anchor_;
            if (const auto point = parseDynamicPoint(input_, origin, hover_ ? std::optional<Vec3>{hover_->point}
                                                                             : std::nullopt)) {
                if (transforming) commitTransformPoint(*point);
                else commitPoint(*point);
                input_.clear();
            }
            else MessageBeep(MB_ICONWARNING);
        } else if (tool_ == DrawTool::Polyline) cancelDrawing();
    } else if (character == L'\b') {
        if (!input_.empty()) input_.pop_back();
    } else if ((character >= L'0' && character <= L'9') || character == L'-' || character == L'+' ||
               character == L'.' || character == L',' || character == L'<') input_.push_back(character);
    updateStatus(); invalidateCanvas();
}

void Application::commitPoint(const Vec3& point) {
    if (!anchor_) { anchor_ = point; return; }
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
        if (point != start) {
            if (mode_ == EditMode::View3D)
                document_.addModel(WireframeModel::rectangleOnPlane(workPlane_, workPlane_.toPlane(start),
                                                                    workPlane_.toPlane(point)));
            else document_.addModel(WireframeModel::rectangle(start, point));
        }
        anchor_.reset();
        break;
    case DrawTool::Circle: {
        double radius{};
        if (mode_ == EditMode::View3D) {
            const Vec2 center = workPlane_.toPlane(start);
            const Vec2 edge = workPlane_.toPlane(point);
            radius = std::hypot(edge.x - center.x, edge.y - center.y);
            if (radius > 0.0) document_.addModel(WireframeModel::circleOnPlane(workPlane_, center, radius));
        } else {
            radius = std::hypot(point.x - start.x, point.y - start.y);
            if (radius > 0.0) document_.addModel(WireframeModel::circle(start, radius));
        }
        anchor_.reset(); break;
    }}
}

void Application::cancelDrawing() { anchor_.reset(); input_.clear(); }

void Application::startTransformCommand(TransformCommand command) {
    cancelZoomWindow2D();
    if (workPlanePicking_) cancelWorkPlaneCommand();
    cancelDrawing();
    transformCommand_ = command;
    transformPhase_ = TransformPhase::Selecting;
    selectedModels_.clear();
    selectionFirstCorner_.reset();
    transformBase_.reset();
    drawingActive_ = false;
    hover_.reset();
    if (modifyCursor_) SetCursor(modifyCursor_);
}

void Application::toggle3DView() {
    cancelZoomWindow2D();
    if (workPlanePicking_) cancelWorkPlaneCommand();
    if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
    cancelDrawing();
    mode_ = mode_ == EditMode::View3D ? EditMode::Draw2D : EditMode::View3D;
    drawingActive_ = mode_ == EditMode::Draw2D;
}

void Application::zoomExtents2D() {
    mode_ = EditMode::Draw2D;
    drawingActive_ = true;
    cancelZoomWindow2D();
    RECT client{}; GetClientRect(canvas_, &client);
    const auto bounds = document_.bounds();
    if (!bounds) camera_.reset();
    else camera_.fit2D(bounds->minimum, bounds->maximum, std::max(1L, client.right),
                       std::max(1L, client.bottom), 50.0);
}

void Application::startZoomWindow2D() {
    if (workPlanePicking_) cancelWorkPlaneCommand();
    if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
    cancelDrawing();
    mode_ = EditMode::Draw2D;
    drawingActive_ = false;
    zoomWindowActive_ = true;
    zoomWindowFirstCorner_.reset();
    hover_.reset();
}

void Application::cancelZoomWindow2D() {
    zoomWindowActive_ = false;
    zoomWindowFirstCorner_.reset();
    if (mode_ == EditMode::Draw2D) drawingActive_ = true;
}

void Application::completeZoomWindow2D(int x, int y) {
    if (!zoomWindowFirstCorner_) return;
    const POINT first = *zoomWindowFirstCorner_;
    const Vec3 a = screenTo2D(first.x, first.y);
    const Vec3 b = screenTo2D(x, y);
    RECT client{}; GetClientRect(canvas_, &client);
    if (std::abs(x - first.x) >= 4 && std::abs(y - first.y) >= 4) {
        camera_.fit2D({std::min(a.x, b.x), std::min(a.y, b.y), 0.0},
                      {std::max(a.x, b.x), std::max(a.y, b.y), 0.0},
                      std::max(1L, client.right), std::max(1L, client.bottom), 0.0);
    }
    cancelZoomWindow2D();
    drawingActive_ = true;
    updateHover(x, y);
}

void Application::startWorkPlaneCommand() {
    cancelZoomWindow2D();
    if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
    cancelDrawing();
    mode_ = EditMode::View3D;
    drawingActive_ = false;
    workPlanePicking_ = true;
    workPlanePoints_.clear();
    hover_.reset();
}

void Application::cancelWorkPlaneCommand() {
    workPlanePicking_ = false;
    workPlanePoints_.clear();
    hover_.reset();
}

void Application::commitWorkPlanePoint(const Vec3& point) {
    workPlanePoints_.push_back(point);
    if (workPlanePoints_.size() < 3) return;
    if (const auto plane = WorkPlane::fromThreePoints(workPlanePoints_[0], workPlanePoints_[1],
                                                       workPlanePoints_[2])) {
        workPlane_ = *plane;
        cancelWorkPlaneCommand();
    } else {
        workPlanePoints_.pop_back();
        MessageBeep(MB_ICONWARNING);
    }
}

void Application::cancelTransformCommand() {
    transformCommand_ = TransformCommand::None;
    transformPhase_ = TransformPhase::Selecting;
    selectedModels_.clear();
    selectionFirstCorner_.reset();
    transformBase_.reset();
    input_.clear();
    drawingActive_ = mode_ == EditMode::Draw2D;
    if (draftingCursor_) SetCursor(draftingCursor_);
}

bool Application::toggleModelSelection(int x, int y) {
    std::optional<std::size_t> hit;
    if (mode_ == EditMode::Draw2D) {
        hit = hitTestModel2D(screenTo2D(x, y), document_, 10.0 / (60.0 * camera_.zoom()));
    } else {
        RECT client{}; GetClientRect(canvas_, &client);
        hit = hitTestModel3D({static_cast<double>(x), static_cast<double>(y)}, document_, camera_,
                             std::max(1L, client.right), std::max(1L, client.bottom), 10.0);
    }
    if (!hit) return false;
    const auto existing = std::find(selectedModels_.begin(), selectedModels_.end(), *hit);
    if (existing == selectedModels_.end()) selectedModels_.push_back(*hit);
    else selectedModels_.erase(existing);
    return true;
}

void Application::completeWindowSelection(int x, int y) {
    if (!selectionFirstCorner_) return;
    const POINT first = *selectionFirstCorner_;
    selectionFirstCorner_.reset();
    if (first.x == x || first.y == y) return;
    const bool crossing = x < first.x;
    std::vector<std::size_t> hits;
    if (mode_ == EditMode::Draw2D) {
        hits = selectModelsInRect2D(screenTo2D(first.x, first.y), screenTo2D(x, y), document_, crossing);
    } else {
        RECT client{}; GetClientRect(canvas_, &client);
        hits = selectModelsInRect3D({static_cast<double>(first.x), static_cast<double>(first.y)},
                                    {static_cast<double>(x), static_cast<double>(y)}, document_, camera_,
                                    std::max(1L, client.right), std::max(1L, client.bottom), crossing);
    }
    for (const auto index : hits) {
        if (std::find(selectedModels_.begin(), selectedModels_.end(), index) == selectedModels_.end())
            selectedModels_.push_back(index);
    }
}

void Application::commitTransformPoint(const Vec3& point) {
    if (transformPhase_ == TransformPhase::BasePoint) {
        transformBase_ = point;
        transformPhase_ = TransformPhase::Destination;
        return;
    }
    if (transformPhase_ != TransformPhase::Destination || !transformBase_) return;
    const Vec3 displacement = point - *transformBase_;
    if (transformCommand_ == TransformCommand::Move) {
        document_.moveModels(selectedModels_, displacement);
        cancelTransformCommand();
    } else if (transformCommand_ == TransformCommand::Copy) {
        document_.copyModels(selectedModels_, displacement);
    }
}

void Application::updateHover(int x, int y) {
    if (!canvas_) return;
    cursorScreen_ = {x, y};
    const bool selectingEntities = transformCommand_ != TransformCommand::None &&
                                   transformPhase_ == TransformPhase::Selecting;
    const bool cameraNavigating = wheelNavigating_ || panning2D_ || rotating_ || viewCubeManipulating_;
    if (!shouldEvaluateSnapping(selectingEntities, zoomWindowActive_, cameraNavigating)) {
        hover_.reset();
        return;
    }
    if (mode_ == EditMode::Draw2D) {
        const auto reference = transformPhase_ == TransformPhase::Destination ? transformBase_ : anchor_;
        hover_ = SnapEngine::snap(screenTo2D(x, y), document_, 10.0 / (60.0 * camera_.zoom()), 1.0,
                                  snapEnabled_, gridSnapEnabled_, reference);
    } else {
        RECT client{}; GetClientRect(canvas_, &client);
        const int width = std::max(1L, client.right);
        const int height = std::max(1L, client.bottom);
        const auto reference = transformPhase_ == TransformPhase::Destination ? transformBase_ : anchor_;
        WorkPlane activePlane = workPlane_;
        if (reference && !workPlanePicking_) activePlane.origin = *reference;
        hover_ = SnapEngine::snap3D({static_cast<double>(x), static_cast<double>(y)}, document_, camera_,
                                    width, height, 10.0, 1.0, activePlane,
                                    snapEnabled_, gridSnapEnabled_, reference);
    }
    const auto orthoAnchor = transformPhase_ == TransformPhase::Destination ? transformBase_ : anchor_;
    if (orthoEnabled_ && orthoAnchor && hover_) {
        if (mode_ == EditMode::View3D) {
            RECT client{}; GetClientRect(canvas_, &client);
            hover_ = applyOrtho3D(*orthoAnchor, {static_cast<double>(x), static_cast<double>(y)}, *hover_,
                                  camera_, std::max(1L, client.right), std::max(1L, client.bottom));
        } else {
            hover_ = applyOrtho(*orthoAnchor, *hover_);
        }
    }
}

void Application::executeCommand(int id) {
    switch (id) {
    case CmdTabFile: activateRibbonTab(RibbonTab::File); break;
    case CmdTabDrawing: activateRibbonTab(RibbonTab::Drawing); break;
    case CmdTabModify: activateRibbonTab(RibbonTab::Modify); break;
    case CmdTabView: activateRibbonTab(RibbonTab::View); break;
    case CmdTabAids: activateRibbonTab(RibbonTab::Aids); break;
    case CmdNew:
        document_.clear();
        if (workPlanePicking_) cancelWorkPlaneCommand();
        if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
        else cancelDrawing();
        mode_ = EditMode::Draw2D; drawingActive_ = true;
        break;
    case CmdOpen: openDocument(); break;
    case CmdSave: saveDocument(); break;
    case CmdImportDxf: importDxf(); break;
    case CmdExportDxf: exportDxf(); break;
    case CmdLine: selectTool(DrawTool::Line); break;
    case CmdPolyline: selectTool(DrawTool::Polyline); break;
    case CmdRectangle: selectTool(DrawTool::Rectangle); break;
    case CmdCircle: selectTool(DrawTool::Circle); break;
    case CmdCube: addCube(); break;
    case CmdPyramid: addPyramid(); break;
    case CmdResetView: camera_.reset(); break;
    case CmdView3D: toggle3DView(); break;
    case CmdWorkPlane: startWorkPlaneCommand(); break;
    case CmdZoomExtents: zoomExtents2D(); break;
    case CmdZoomWindow: startZoomWindow2D(); break;
    case CmdOsnap: snapEnabled_ = !snapEnabled_; break;
    case CmdGridSnap: gridSnapEnabled_ = !gridSnapEnabled_; break;
    case CmdDynamicInput: dynamicInputEnabled_ = !dynamicInputEnabled_; break;
    case CmdMove: startTransformCommand(TransformCommand::Move); break;
    case CmdCopy: startTransformCommand(TransformCommand::Copy); break;
    default: break;
    }
    updateHover(cursorScreen_.x, cursorScreen_.y);
    updateControls(); invalidateCanvas(); SetFocus(canvas_);
}

void Application::selectTool(DrawTool tool) {
    cancelZoomWindow2D();
    if (workPlanePicking_) cancelWorkPlaneCommand();
    if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
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
    check(moveButton_, transformCommand_ == TransformCommand::Move);
    check(copyButton_, transformCommand_ == TransformCommand::Copy);
    check(view3DButton_, mode_ == EditMode::View3D);
    check(workPlaneButton_, workPlanePicking_);
    check(zoomWindowButton_, zoomWindowActive_);
    updateStatus();
}

void Application::updateStatus() {
    if (!status_) return;
    if (dxfImportInProgress_) {
        const auto completed = dxfBytesRead_.load();
        const auto total = dxfTotalBytes_.load();
        const auto percent = total ? std::min<std::uint64_t>(100, completed * 100 / total) : 0;
        std::wstring progress = L"   DXF ";
        progress += dxfImportThread_.joinable() && dxfImportThread_.get_stop_token().stop_requested()
            ? L"iptal ediliyor..." : L"yükleniyor... ";
        if (total) {
            progress += std::to_wstring(percent) + L"%  (" +
                        std::to_wstring(completed / (1024 * 1024)) + L" / " +
                        std::to_wstring(total / (1024 * 1024)) + L" MB)";
        }
        progress += L"  |  Uygulama kullanıma açık  |  İptal: DXF Aç düğmesine tekrar basın";
        SetWindowTextW(status_, progress.c_str());
        return;
    }
    std::wstring text = L"   ";
    text += mode_ == EditMode::View3D ? L"3B Paralel" : L"2B";
    if (workPlanePicking_) {
        text += L"  |  WORK PLANE — ";
        if (workPlanePoints_.empty()) text += L"1. noktayı belirtin";
        else if (workPlanePoints_.size() == 1) text += L"2. noktayı belirtin (U ekseni)";
        else text += L"3. noktayı belirtin (düzlem yönü)";
    } else if (zoomWindowActive_) {
        text += zoomWindowFirstCorner_ ? L"  |  ZOOM WINDOW — diğer köşeyi belirtin"
                                       : L"  |  ZOOM WINDOW — ilk köşeyi belirtin";
    } else if (transformCommand_ != TransformCommand::None) {
        text += L"  |  Komut: ";
        text += transformCommand_ == TransformCommand::Move ? L"MOVE" : L"COPY";
        text += L" — ";
        if (transformPhase_ == TransformPhase::Selecting) {
            text += selectionFirstCorner_ ? L"Diğer köşeyi belirtin" : L"Nesneleri seçin, Enter";
            text += L" (" + std::to_wstring(selectedModels_.size()) + L")";
        } else if (transformPhase_ == TransformPhase::BasePoint) text += L"Baz noktayı belirtin";
        else text += transformCommand_ == TransformCommand::Copy
            ? L"İkinci noktayı belirtin; kopyalamaya devam edin, Enter ile bitir"
            : L"İkinci noktayı belirtin";
    } else {
        text += L"  |  Araç: "; text += toolLabel(tool_);
    }
    text += L"  |  Nesne: " + std::to_wstring(document_.models().size());
    text += L"  |  OSNAP: "; text += snapEnabled_ ? L"Açık" : L"Kapalı";
    text += L"  |  ORTHO F8: "; text += orthoEnabled_ ? L"Açık" : L"Kapalı";
    if (orthoEnabled_ && mode_ == EditMode::View3D) text += L" (X/Y/Z)";
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
    if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
    auto cube = WireframeModel::cube(2.6);
    cube.translate({static_cast<double>(document_.models().size() % 4) * 0.45, 0.0, 0.0});
    document_.addModel(std::move(cube)); mode_ = EditMode::View3D;
    cancelDrawing(); drawingActive_ = false;
}

void Application::addPyramid() {
    if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
    auto pyramid = WireframeModel::pyramid(3.0, 3.2); pyramid.translate({0.0, 0.0, -1.0});
    document_.addModel(std::move(pyramid)); mode_ = EditMode::View3D;
    cancelDrawing(); drawingActive_ = false;
}

Vec3 Application::screenTo2D(int x, int y) const noexcept {
    RECT client{}; GetClientRect(canvas_, &client);
    return camera_.unproject2D({static_cast<double>(x), static_cast<double>(y)},
                               client.right, client.bottom);
}

DraftView Application::draftView() const {
    DraftView view;
    view.tool = tool_; view.anchor = anchor_;
    if (hover_) { view.cursor = hover_->point; view.snapType = hover_->type; }
    view.drawingActive = drawingActive_; view.snapEnabled = snapEnabled_;
    view.gridSnapEnabled = gridSnapEnabled_; view.dynamicInputEnabled = dynamicInputEnabled_;
    view.workPlaneZ = workPlane_.origin.z; view.workPlane = workPlane_;
    view.workPlanePicking = workPlanePicking_; view.workPlanePoints = workPlanePoints_;
    view.input = input_; view.cursorScreen = cursorScreen_;
    view.transformCommand = transformCommand_; view.transformPhase = transformPhase_;
    view.selectedModels = selectedModels_; view.selectionFirstCorner = selectionFirstCorner_;
    view.transformBase = transformBase_;
    view.zoomWindowActive = zoomWindowActive_; view.zoomWindowFirstCorner = zoomWindowFirstCorner_;
    view.interactiveNavigation = rotating_ || panning2D_ || viewCubeManipulating_ ||
                                 wheelNavigating_ || snapPreviewActive_;
    view.rasterZoomPreview = document_.models().size() > 5'000 && wheelNavigating_ &&
                             std::abs(wheelPreviewFactor_ - 1.0) > 1e-12;
    view.rasterZoomFactor = wheelPreviewFactor_;
    view.rasterZoomOffset = wheelPreviewOffset_;
    return view;
}

std::optional<std::filesystem::path> Application::chooseFile(bool save, bool dxf) const {
    wchar_t filename[MAX_PATH]{};
    OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = window_;
    dialog.lpstrFilter = dxf
        ? L"AutoCAD ASCII DXF (*.dxf)\0*.dxf\0Tüm dosyalar (*.*)\0*.*\0"
        : L"Model Maker Wireframe (*.mmw)\0*.mmw\0Tüm dosyalar (*.*)\0*.*\0";
    dialog.lpstrFile = filename; dialog.nMaxFile = MAX_PATH; dialog.lpstrDefExt = dxf ? L"dxf" : L"mmw";
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
    try {
        document_.load(*path);
        if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
        else cancelDrawing();
    } catch (const std::exception& error) { showError(L"Dosya açılamadı", error); }
}

void Application::importDxf() {
    if (dxfImportInProgress_) {
        if (dxfImportThread_.joinable()) dxfImportThread_.request_stop();
        return;
    }
    const auto path = chooseFile(false, true);
    if (!path) return;
    beginDxfImport(*path);
}

void Application::beginDxfImport(const std::filesystem::path& path) {
    if (dxfImportThread_.joinable()) dxfImportThread_.join();
    {
        std::lock_guard lock(dxfImportMutex_);
        pendingDxfDocument_.reset();
        pendingDxfError_.clear();
    }
    dxfBytesRead_ = 0;
    try { dxfTotalBytes_ = static_cast<std::uint64_t>(std::filesystem::file_size(path)); }
    catch (...) { dxfTotalBytes_ = 0; }
    dxfImportInProgress_ = true;
    if (dxfProgressBar_) SendMessageW(dxfProgressBar_, PBM_SETPOS, 0, 0);
    RECT importClient{};
    GetClientRect(window_, &importClient);
    layoutChildren(importClient.right, importClient.bottom);
    SetTimer(window_, 1, 100, nullptr);
    updateStatus();
    dxfImportThread_ = std::jthread([this, path](std::stop_token stopToken) {
        try {
            Document loaded = DxfFile::read(path, stopToken,
                [this](std::uint64_t completed, std::uint64_t total) {
                    dxfBytesRead_ = completed;
                    dxfTotalBytes_ = total;
                });
            std::lock_guard lock(dxfImportMutex_);
            pendingDxfDocument_ = std::move(loaded);
        } catch (const DxfImportCancelled&) {
        } catch (const std::exception& error) {
            std::lock_guard lock(dxfImportMutex_);
            pendingDxfError_ = error.what();
        }
        PostMessageW(window_, WM_APP + 2, 0, 0);
    });
}

void Application::finishDxfImport() {
    KillTimer(window_, 1);
    if (dxfImportThread_.joinable()) dxfImportThread_.join();
    std::optional<Document> loaded;
    std::string error;
    {
        std::lock_guard lock(dxfImportMutex_);
        loaded = std::move(pendingDxfDocument_);
        pendingDxfDocument_.reset();
        error = std::move(pendingDxfError_);
        pendingDxfError_.clear();
    }
    dxfImportInProgress_ = false;
    RECT finishedClient{};
    GetClientRect(window_, &finishedClient);
    layoutChildren(finishedClient.right, finishedClient.bottom);
    if (loaded) {
        if (workPlanePicking_) cancelWorkPlaneCommand();
        if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
        cancelDrawing();
        hover_.reset();
        document_ = std::move(*loaded);
        const auto bounds = document_.bounds();
        bool nonPlanar = false;
        if (bounds) {
            const double xSpan = bounds->maximum.x - bounds->minimum.x;
            const double ySpan = bounds->maximum.y - bounds->minimum.y;
            const double zSpan = bounds->maximum.z - bounds->minimum.z;
            nonPlanar = zSpan > std::max(1e-6, std::max(xSpan, ySpan) * 1e-6);
        }
        if (nonPlanar && bounds) {
            mode_ = EditMode::View3D;
            drawingActive_ = false;
            camera_.reset();
            camera_.setView(StandardView::Isometric);
            RECT canvasClient{};
            GetClientRect(canvas_, &canvasClient);
            camera_.fit3D(bounds->minimum, bounds->maximum,
                          std::max(1L, canvasClient.right), std::max(1L, canvasClient.bottom), 50.0);
        } else {
            mode_ = EditMode::Draw2D;
            drawingActive_ = true;
            zoomExtents2D();
        }
        updateControls();
        invalidateCanvas();
    } else {
        updateStatus();
    }
    if (!error.empty()) {
        const std::runtime_error importError(error);
        showError(L"DXF içe aktarma başarısız", importError);
    }
}

void Application::exportDxf() {
    const auto path = chooseFile(true, true);
    if (!path) return;
    try { DxfFile::write(document_, *path); }
    catch (const std::exception& error) { showError(L"DXF dışa aktarma başarısız", error); }
}

void Application::showError(const wchar_t* action, const std::exception& error) const {
    std::wstring message(action); message += L".\n\n";
    const std::string detail = error.what(); message.append(detail.begin(), detail.end());
    MessageBoxW(window_, message.c_str(), L"Model Maker", MB_OK | MB_ICONERROR);
}

} // namespace mm
