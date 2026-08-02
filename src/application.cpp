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
#include <iterator>
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
    CmdLine = 200, CmdPolyline, CmdRectangle, CmdCircle, CmdFace3D,
    CmdCube = 300, CmdPyramid, CmdResetView, CmdView3D, CmdWorkPlane, CmdZoomExtents, CmdZoomWindow,
    CmdVisualStyle, CmdStandardView, CmdWireframe = 310, CmdSolid, CmdTransparent,
    CmdViewFront = 320, CmdViewBack, CmdViewLeft, CmdViewRight, CmdViewIsometric,
    CmdViewTop, CmdViewBottom,
    CmdOsnap = 400, CmdGridSnap, CmdDynamicInput, CmdSnapSettings, CmdPolarTracking,
    CmdMove = 500, CmdCopy, CmdOffset, CmdMirror, CmdDelete, CmdLinearArray, CmdPolarArray,
    CmdTrim, CmdExtend, CmdNeutral, CmdFillet,
    CmdTabFile = 600, CmdTabDrawing, CmdTabModify, CmdTabView, CmdTabAids,
    CmdDxfProgress = 700, CmdSnapTypeFirst = 720,
    CmdLayerCombo = 800, CmdColorCombo, CmdLineTypeCombo
};

struct SnapChoice { SnapType type; const wchar_t* label; };
constexpr std::array<SnapChoice, 14> snapChoices{{
    {SnapType::Endpoint, L"Endpoint"}, {SnapType::Midpoint, L"Midpoint"},
    {SnapType::Center, L"Center"}, {SnapType::GeometricCenter, L"Geometric Center"},
    {SnapType::Node, L"Node"}, {SnapType::Quadrant, L"Quadrant"},
    {SnapType::Intersection, L"Intersection"},
    {SnapType::ApparentIntersection, L"Apparent Intersection"},
    {SnapType::Extension, L"Extension"}, {SnapType::Insertion, L"Insertion"},
    {SnapType::Perpendicular, L"Perpendicular"}, {SnapType::Tangent, L"Tangent"},
    {SnapType::Nearest, L"Nearest"}, {SnapType::Parallel, L"Parallel"}
}};

struct ColorChoice { const wchar_t* label; std::optional<std::uint32_t> color; };
constexpr std::array<ColorChoice, 8> colorChoices{{
    {L"ByLayer", std::nullopt}, {L"White", 0xFFFFFFu}, {L"Red", 0xFF4040u},
    {L"Yellow", 0xFFFF00u}, {L"Green", 0x40FF40u}, {L"Cyan", 0x68CAFFu},
    {L"Blue", 0x4080FFu}, {L"Magenta", 0xFF40FFu}
}};
struct LineTypeChoice { const wchar_t* label; const char* value; };
constexpr std::array<LineTypeChoice, 5> lineTypeChoices{{
    {L"ByLayer", "BYLAYER"}, {L"Continuous", "CONTINUOUS"}, {L"Dashed", "DASHED"},
    {L"Center", "CENTER"}, {L"Dotted", "DOTTED"}
}};

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0);
    if (size <= 0) return std::wstring(text.begin(), text.end());
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

std::string wideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) return std::string(text.begin(), text.end());
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

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

Application::Application(HINSTANCE instance) : instance_(instance) {
    enabledSnapTypes_.fill(true);
}

Application::~Application() {
    if (dxfImportThread_.joinable()) {
        dxfImportThread_.request_stop();
        dxfImportThread_.join();
    }
    if (modifyCursor_) DestroyCursor(modifyCursor_);
    if (uiFont_) DeleteObject(uiFont_);
    if (titleFont_) DeleteObject(titleFont_);
    if (iconFont_) DeleteObject(iconFont_);
    if (windowBrush_) DeleteObject(windowBrush_);
    if (panelBrush_) DeleteObject(panelBrush_);
    if (statusBrush_) DeleteObject(statusBrush_);
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
    neutralCursor_ = LoadCursorW(nullptr, IDC_ARROW);
    if (!draftingCursor_ || !modifyCursor_ || !neutralCursor_)
        throw std::runtime_error("Drawing cursors could not be created");

    windowBrush_ = CreateSolidBrush(RGB(31, 38, 48));
    panelBrush_ = CreateSolidBrush(RGB(57, 68, 84));
    statusBrush_ = CreateSolidBrush(RGB(29, 36, 46));
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hbrBackground = windowBrush_;
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
    iconFont_ = CreateFontW(-25, 0, 0, 0, FW_LIGHT, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Symbol");

    const auto addTab = [&](const wchar_t* text, int id, DWORD extra = 0) {
        HWND tab = createButton(text, id, 0, 0, 82, 30,
                                BS_AUTORADIOBUTTON | BS_PUSHLIKE | extra);
        ribbonTabButtons_.push_back(tab);
        return tab;
    };
    addTab(L"M", CmdTabFile, WS_GROUP);
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
    face3DButton_ = addCommand(L"▱\r\n3DFACE", CmdFace3D, BS_AUTOCHECKBOX | BS_PUSHLIKE);

    addCommand(L"◇\r\nKüp", CmdCube);
    addCommand(L"△\r\nPiramit", CmdPyramid);
    addCommand(L"⌂\r\nSıfırla", CmdResetView);
    view3DButton_ = addCommand(L"3B\r\n2B / 3B", CmdView3D, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    workPlaneButton_ = addCommand(L"▱\r\nDüzlem", CmdWorkPlane, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    addCommand(L"⤢\r\nExtents", CmdZoomExtents);
    zoomWindowButton_ = addCommand(L"⌗\r\nPencere", CmdZoomWindow, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    visualStyleButton_ = addCommand(L"◇\r\nWireframe ▼", CmdVisualStyle);
    standardViewButton_ = addCommand(L"▦\r\nGörünüş ▼", CmdStandardView);

    snapButton_ = addCommand(L"◎\r\nOSNAP F3", CmdOsnap, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    gridSnapButton_ = addCommand(L"#\r\nGrid F9", CmdGridSnap, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    dynamicInputButton_ = addCommand(L"123\r\nDinamik", CmdDynamicInput, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    snapSettingsButton_ = addCommand(L"☑\r\nSnap Türleri", CmdSnapSettings,
                                     BS_AUTOCHECKBOX | BS_PUSHLIKE);
    polarTrackingButton_ = addCommand(L"∠\r\nPolar F10", CmdPolarTracking,
                                      BS_AUTOCHECKBOX | BS_PUSHLIKE);

    neutralButton_ = addCommand(L"↖\r\nPasif", CmdNeutral, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    moveButton_ = addCommand(L"↔\r\nTaşı", CmdMove, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    copyButton_ = addCommand(L"⧉\r\nKopyala", CmdCopy, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    offsetButton_ = addCommand(L"⇶\r\nOfset", CmdOffset, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    mirrorButton_ = addCommand(L"◁│▷\r\nAyna", CmdMirror, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    deleteButton_ = addCommand(L"✕\r\nSil", CmdDelete, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    linearArrayButton_ = addCommand(L"▦\r\nDoğrusal", CmdLinearArray, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    polarArrayButton_ = addCommand(L"◌\r\nDairesel", CmdPolarArray, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    trimButton_ = addCommand(L"✂\r\nTrim", CmdTrim, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    extendButton_ = addCommand(L"↗\r\nExtend", CmdExtend, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    filletButton_ = addCommand(L"⌒\r\nFillet", CmdFillet, BS_AUTOCHECKBOX | BS_PUSHLIKE);

    canvas_ = CreateWindowExW(WS_EX_CLIENTEDGE, canvasClassName, nullptr,
                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS,
                              0, ribbonHeight, 800, 600, window_, nullptr, instance_, this);
    status_ = CreateWindowExW(0, L"STATIC", nullptr,
                              WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_LEFT | SS_CENTERIMAGE,
                              0, 700, 1000, statusHeight, window_, nullptr, instance_, nullptr);
    SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    dxfProgressBar_ = CreateWindowExW(0, PROGRESS_CLASSW, nullptr,
                                      WS_CHILD | WS_CLIPSIBLINGS | PBS_SMOOTH,
                                      0, 0, 280, statusHeight - 6, window_,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(CmdDxfProgress)), instance_, nullptr);
    SendMessageW(dxfProgressBar_, PBM_SETRANGE32, 0, 100);
    snapPanel_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"  Aktif Nesne Yakalamaları",
                                 WS_CHILD | WS_CLIPSIBLINGS | SS_LEFT,
                                 0, 0, 365, 250, window_, nullptr, instance_, nullptr);
    SendMessageW(snapPanel_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    for (std::size_t i = 0; i < snapChoices.size(); ++i) {
        snapTypeCheckboxes_[i] = createButton(snapChoices[i].label,
            CmdSnapTypeFirst + static_cast<int>(i), 0, 0, 170, 27, BS_AUTOCHECKBOX);
        SendMessageW(snapTypeCheckboxes_[i], BM_SETCHECK, BST_CHECKED, 0);
        ShowWindow(snapTypeCheckboxes_[i], SW_HIDE);
    }
    const auto createStyleLabel = [&](const wchar_t* text) {
        HWND label = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                                     0, 0, 120, 17, window_, nullptr, instance_, nullptr);
        SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        return label;
    };
    const auto createCombo = [&](int id, int width) {
        HWND combo = CreateWindowExW(WS_EX_CLIENTEDGE, WC_COMBOBOXW, nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
            0, 0, width, 220, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            instance_, nullptr);
        SendMessageW(combo, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        return combo;
    };
    styleLabels_[0] = createStyleLabel(L"Layer");
    styleLabels_[1] = createStyleLabel(L"Line Color");
    styleLabels_[2] = createStyleLabel(L"Line Type");
    layerCombo_ = createCombo(CmdLayerCombo, 150);
    colorCombo_ = createCombo(CmdColorCombo, 120);
    lineTypeCombo_ = createCombo(CmdLineTypeCombo, 130);
    for (const auto& choice : colorChoices)
        SendMessageW(colorCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.label));
    for (const auto& choice : lineTypeChoices)
        SendMessageW(lineTypeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.label));
    SendMessageW(colorCombo_, CB_SETCURSEL, 0, 0);
    SendMessageW(lineTypeCombo_, CB_SETCURSEL, 0, 0);
    refreshLayerCombo();
    activateRibbonTab(RibbonTab::Drawing);
}

HWND Application::createButton(const wchar_t* text, int id, int x, int y, int width, int height, DWORD style) {
    const DWORD ownerDrawStyle = (style & ~BS_TYPEMASK) | BS_OWNERDRAW;
    HWND button = CreateWindowExW(0, L"BUTTON", text,
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | ownerDrawStyle,
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
    if (snapPanel_) {
        const int panelX = std::max(8, width - 375);
        const int panelY = ribbonHeight + 8;
        MoveWindow(snapPanel_, panelX, panelY, 365, 250, TRUE);
        for (std::size_t i = 0; i < snapTypeCheckboxes_.size(); ++i) {
            const int column = static_cast<int>(i / 7);
            const int row = static_cast<int>(i % 7);
            MoveWindow(snapTypeCheckboxes_[i], panelX + 10 + column * 176,
                       panelY + 27 + row * 29, 170, 27, TRUE);
        }
        updateSnapPanelVisibility();
    }

    for (std::size_t i = 0; i < ribbonTabButtons_.size(); ++i) {
        if (i == 0) MoveWindow(ribbonTabButtons_[i], 8, 3, 35, 27, TRUE);
        else MoveWindow(ribbonTabButtons_[i], 8 + static_cast<int>(i - 1) * 92, 32, 90, 29, TRUE);
    }
    const auto geometry = RibbonLayout::layout(activeRibbonTab_, width);
    for (const auto& item : geometry.commandButtons) {
        if (HWND button = GetDlgItem(window_, item.commandId)) {
            const auto& r = item.rect;
            MoveWindow(button, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
        }
    }
    const int commandsRight = geometry.commandButtons.empty() ? 350 : geometry.commandButtons.back().rect.right;
    const int styleX = std::min(std::max(commandsRight + 24, 365), std::max(365, width - 426));
    const std::array<int, 3> xPositions{{styleX, styleX + 158, styleX + 286}};
    const std::array<int, 3> widths{{150, 120, 130}};
    for (std::size_t i = 0; i < styleLabels_.size(); ++i) {
        if (styleLabels_[i]) MoveWindow(styleLabels_[i], xPositions[i], 65, widths[i], 17, TRUE);
    }
    if (layerCombo_) MoveWindow(layerCombo_, xPositions[0], 83, widths[0], 220, TRUE);
    if (colorCombo_) MoveWindow(colorCombo_, xPositions[1], 83, widths[1], 220, TRUE);
    if (lineTypeCombo_) MoveWindow(lineTypeCombo_, xPositions[2], 83, widths[2], 220, TRUE);
}

void Application::updateSnapPanelVisibility() {
    if (snapPanel_) ShowWindow(snapPanel_, snapPanelOpen_ ? SW_SHOW : SW_HIDE);
    for (HWND checkbox : snapTypeCheckboxes_)
        if (checkbox) ShowWindow(checkbox, snapPanelOpen_ ? SW_SHOW : SW_HIDE);
    if (snapPanelOpen_) {
        SetWindowPos(snapPanel_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        for (HWND checkbox : snapTypeCheckboxes_)
            if (checkbox) SetWindowPos(checkbox, HWND_TOP, 0, 0, 0, 0,
                                       SWP_NOMOVE | SWP_NOSIZE);
    }
}

void Application::activateRibbonTab(RibbonTab tab) {
    activeRibbonTab_ = tab;
    for (HWND button : ribbonCommandButtons_) ShowWindow(button, SW_HIDE);
    const bool showProperties = tab == RibbonTab::Drawing || tab == RibbonTab::Modify;
    for (HWND label : styleLabels_) if (label) ShowWindow(label, showProperties ? SW_SHOW : SW_HIDE);
    if (layerCombo_) ShowWindow(layerCombo_, showProperties ? SW_SHOW : SW_HIDE);
    if (colorCombo_) ShowWindow(colorCombo_, showProperties ? SW_SHOW : SW_HIDE);
    if (lineTypeCombo_) ShowWindow(lineTypeCombo_, showProperties ? SW_SHOW : SW_HIDE);
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
    HBRUSH titleBrush = CreateSolidBrush(RGB(31, 38, 48));
    HBRUSH commandBrush = CreateSolidBrush(RGB(58, 69, 85));
    HBRUSH dividerBrush = CreateSolidBrush(RGB(76, 89, 108));
    RECT ribbon{0, 0, client.right, ribbonHeight};
    FillRect(dc, &ribbon, titleBrush);
    RECT commandBand{0, 61, client.right, ribbonHeight};
    FillRect(dc, &commandBand, commandBrush);
    RECT bandLine{0, 60, client.right, 61};
    FillRect(dc, &bandLine, dividerBrush);

    const auto geometry = RibbonLayout::layout(activeRibbonTab_, std::max(1L, client.right));
    const auto oldFont = SelectObject(dc, uiFont_);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(225, 232, 241));
    RECT title{160, 3, client.right - 160, 30};
    DrawTextW(dc, L"MODEL MAKER — PROFESSIONAL CAD", -1, &title,
              DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SetTextColor(dc, RGB(220, 227, 236));
    for (const auto& group : geometry.groups) {
        RECT divider{group.rect.right - 1, group.rect.top + 3,
                     group.rect.right, group.rect.bottom - 4};
        FillRect(dc, &divider, dividerBrush);
        RECT caption{group.rect.left, group.rect.bottom - 21,
                     group.rect.right - 2, group.rect.bottom - 2};
        DrawTextW(dc, group.label.c_str(), -1, &caption,
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }
    SelectObject(dc, oldFont);
    DeleteObject(dividerBrush);
    DeleteObject(commandBrush);
    DeleteObject(titleBrush);
    EndPaint(window_, &paint);
}

void Application::drawOwnerButton(const DRAWITEMSTRUCT& item) {
    const int id = GetDlgCtrlID(item.hwndItem);
    const bool appButton = id == CmdTabFile;
    const bool tabButton = id >= CmdTabFile && id <= CmdTabAids;
    const bool selected = (item.itemState & ODS_SELECTED) != 0;
    const bool checked = SendMessageW(item.hwndItem, BM_GETCHECK, 0, 0) == BST_CHECKED;
    COLORREF background = tabButton ? RGB(31, 38, 48) : RGB(58, 69, 85);
    if (appButton) background = selected ? RGB(230, 36, 86) : RGB(215, 24, 75);
    else if (selected) background = RGB(77, 92, 113);
    else if (checked) background = RGB(54, 101, 139);
    HBRUSH backgroundBrush = CreateSolidBrush(background);
    FillRect(item.hDC, &item.rcItem, backgroundBrush);
    DeleteObject(backgroundBrush);

    RECT content = item.rcItem;
    if (selected) OffsetRect(&content, 1, 1);
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, (item.itemState & ODS_DISABLED)
        ? RGB(135, 145, 158) : RGB(231, 237, 245));
    wchar_t label[128]{};
    GetWindowTextW(item.hwndItem, label, static_cast<int>(std::size(label)));
    const std::wstring text(label);
    if (appButton) {
        HGDIOBJ old = SelectObject(item.hDC, titleFont_);
        DrawTextW(item.hDC, text.c_str(), -1, &content,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(item.hDC, old);
    } else if (tabButton) {
        HGDIOBJ old = SelectObject(item.hDC, uiFont_);
        DrawTextW(item.hDC, text.c_str(), -1, &content,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(item.hDC, old);
        if (checked) {
            HBRUSH accent = CreateSolidBrush(RGB(71, 174, 227));
            RECT underline{item.rcItem.left + 7, item.rcItem.bottom - 2,
                           item.rcItem.right - 7, item.rcItem.bottom};
            FillRect(item.hDC, &underline, accent);
            DeleteObject(accent);
        }
    } else {
        const auto separator = text.find(L"\r\n");
        const std::wstring glyph = separator == std::wstring::npos ? L"" : text.substr(0, separator);
        const std::wstring caption = separator == std::wstring::npos ? text : text.substr(separator + 2);
        RECT glyphRect{content.left + 2, content.top + 2, content.right - 2, content.top + 37};
        RECT captionRect{content.left + 2, content.top + 37, content.right - 2, content.bottom - 1};
        HGDIOBJ old = SelectObject(item.hDC, iconFont_);
        DrawTextW(item.hDC, glyph.c_str(), -1, &glyphRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(item.hDC, uiFont_);
        DrawTextW(item.hDC, caption.c_str(), -1, &captionRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(item.hDC, old);
        if (checked || selected) {
            HBRUSH borderBrush = CreateSolidBrush(checked ? RGB(99, 181, 225) : RGB(110, 127, 150));
            FrameRect(item.hDC, &item.rcItem, borderBrush);
            DeleteObject(borderBrush);
        }
    }
    if (item.itemState & ODS_FOCUS) {
        RECT focus = item.rcItem;
        InflateRect(&focus, -3, -3);
        DrawFocusRect(item.hDC, &focus);
    }
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
    case WM_DRAWITEM:
        if (const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            item && item->CtlType == ODT_BUTTON) {
            drawOwnerButton(*item);
            return TRUE;
        }
        return DefWindowProcW(window_, message, wParam, lParam);
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, RGB(226, 233, 241));
        SetBkColor(dc, reinterpret_cast<HWND>(lParam) == status_
            ? RGB(29, 36, 46) : RGB(57, 68, 84));
        return reinterpret_cast<LRESULT>(reinterpret_cast<HWND>(lParam) == status_
            ? statusBrush_ : panelBrush_);
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, RGB(232, 238, 246));
        SetBkColor(dc, RGB(44, 53, 67));
        return reinterpret_cast<LRESULT>(panelBrush_);
    }
    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED) executeCommand(LOWORD(wParam));
        else if (HIWORD(wParam) == CBN_SELCHANGE &&
                 (LOWORD(wParam) == CmdLayerCombo || LOWORD(wParam) == CmdColorCombo ||
                  LOWORD(wParam) == CmdLineTypeCombo)) {
            updateStatus();
            SetFocus(canvas_);
        }
        return 0;
    case WM_APP + 2:
        finishDxfImport();
        return 0;
    case WM_TIMER:
        if (wParam == 5) {
            KillTimer(window_, 5);
            if (temporaryPointDwellCandidate_ && hover_ && polarTrackingEnabled_ &&
                temporaryPointDwellCandidate_->point == hover_->point &&
                temporaryPointDwellCandidate_->type == hover_->type) {
                const Vec3 point = temporaryPointDwellCandidate_->point;
                if (std::find(temporaryTrackingPoints_.begin(), temporaryTrackingPoints_.end(), point) ==
                    temporaryTrackingPoints_.end()) {
                    if (temporaryTrackingPoints_.size() == 8)
                        temporaryTrackingPoints_.erase(temporaryTrackingPoints_.begin());
                    temporaryTrackingPoints_.push_back(point);
                }
            }
            temporaryPointDwellCandidate_.reset();
            updateHover(cursorScreen_.x, cursorScreen_.y);
            updateStatus();
            invalidateCanvas();
            return 0;
        }
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
            updateHover(cursorScreen_.x, cursorScreen_.y);
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
    case WM_APP + 2: return hover_.has_value() ? TRUE : FALSE; // Effective-snap smoke-test probe.
    case WM_APP + 3: return hover_ ? static_cast<LRESULT>(hover_->type) : 0; // Snap-type probe.
    case WM_APP + 4: { // First model/vertex projection probe for native 3D snap verification.
        if (document_.models().empty() || document_.models().front().vertices().empty()) return 0;
        RECT rect{};
        GetClientRect(canvas_, &rect);
        const auto& vertices = document_.models().front().vertices();
        const std::size_t index = std::min<std::size_t>(static_cast<std::size_t>(wParam),
                                                        vertices.size() - 1);
        const Vec2 point = mode_ == EditMode::Draw2D
            ? camera_.project2D(vertices[index], rect.right, rect.bottom)
            : camera_.project(vertices[index], rect.right, rect.bottom);
        return MAKELPARAM(static_cast<short>(std::lround(point.x)),
                          static_cast<short>(std::lround(point.y)));
    }
    case WM_APP + 10: {
        const std::size_t modelIndex = static_cast<std::size_t>(wParam);
        const std::size_t vertexIndex = static_cast<std::size_t>(lParam);
        if (modelIndex >= document_.models().size() ||
            vertexIndex >= document_.models()[modelIndex].vertices().size()) return 0;
        RECT rect{};
        GetClientRect(canvas_, &rect);
        const Vec3 point3D = document_.models()[modelIndex].vertices()[vertexIndex];
        const Vec2 point = mode_ == EditMode::Draw2D
            ? camera_.project2D(point3D, rect.right, rect.bottom)
            : camera_.project(point3D, rect.right, rect.bottom);
        return MAKELPARAM(static_cast<short>(std::lround(point.x)),
                          static_cast<short>(std::lround(point.y)));
    }
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            SetCursor(currentCanvasCursor());
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
        else if (transformCommand_ != TransformCommand::None) onCharacter(L'\r');
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
        rotating_ = false;
        panning2D_ = false;
        ReleaseCapture();
        updateHover(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        invalidateCanvas();
        return 0;
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
        else if (wParam == VK_F8) {
            orthoEnabled_ = !orthoEnabled_;
            if (orthoEnabled_) {
                polarTrackingEnabled_ = false;
                clearTemporaryTracking();
            }
        }
        else if (wParam == VK_F9) gridSnapEnabled_ = !gridSnapEnabled_;
        else if (wParam == VK_F10) {
            polarTrackingEnabled_ = !polarTrackingEnabled_;
            if (polarTrackingEnabled_) orthoEnabled_ = false;
            else clearTemporaryTracking();
        }
        else if (wParam == VK_F12) dynamicInputEnabled_ = !dynamicInputEnabled_;
        else if (wParam == 'L') selectTool(DrawTool::Line);
        else if (wParam == 'P') selectTool(DrawTool::Polyline);
        else if (wParam == 'A') selectTool(DrawTool::Rectangle);
        else if (wParam == 'C') selectTool(DrawTool::Circle);
        else if (wParam == 'F') selectTool(DrawTool::Face3D);
        else if (wParam == 'M') startTransformCommand(TransformCommand::Move);
        else if (wParam == 'K') startTransformCommand(TransformCommand::Copy);
        else if (wParam == 'O' && !(GetKeyState(VK_CONTROL) & 0x8000))
            startTransformCommand(TransformCommand::Offset);
        else if (wParam == 'I') startTransformCommand(TransformCommand::Mirror);
        else if (wParam == 'T') startTransformCommand(TransformCommand::Trim);
        else if (wParam == 'E') startTransformCommand(TransformCommand::Extend);
        else if (wParam == 'G') startTransformCommand(TransformCommand::Fillet);
        else if (wParam == 'V') toggle3DView();
        else if (wParam == 'W') startWorkPlaneCommand();
        else if (wParam == 'B') addCube();
        else if (wParam == 'Y') addPyramid();
        else if (wParam == 'R' && mode_ == EditMode::View3D) camera_.reset();
        else if (wParam == VK_DELETE) startTransformCommand(TransformCommand::Delete);
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
    if (zoomWindowActive_) {
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
        if (hover_) {
            const Vec3 point = hover_->point;
            clearTemporaryTracking();
            commitWorkPlanePoint(point);
        }
        updateControls(); invalidateCanvas(); return;
    }
    if (transformCommand_ != TransformCommand::None) {
        if (transformCommand_ == TransformCommand::Fillet &&
            transformPhase_ == TransformPhase::Selecting) {
            const auto hit = trimExtendTargetAt(x, y);
            std::optional<Vec3> pick;
            if (mode_ == EditMode::Draw2D) pick = screenTo2D(x, y);
            else {
                RECT viewport{}; GetClientRect(canvas_, &viewport);
                pick = camera_.unprojectToPlane({static_cast<double>(x), static_cast<double>(y)},
                    std::max(1L, viewport.right), std::max(1L, viewport.bottom), workPlane_);
            }
            if (!hit || !pick || *hit >= document_.models().size()) {
                MessageBeep(MB_ICONWARNING);
            } else if (selectedModels_.empty()) {
                selectedModels_.push_back(*hit);
                filletFirstPick_ = *pick;
            } else if (*hit == selectedModels_.front() || !filletFirstPick_) {
                MessageBeep(MB_ICONWARNING);
            } else {
                const std::size_t firstIndex = selectedModels_.front();
                const auto result = filletLinesOnPlane(
                    document_.models()[firstIndex], *filletFirstPick_,
                    document_.models()[*hit], *pick, filletRadius_, workPlane_);
                if (!result) {
                    MessageBeep(MB_ICONWARNING);
                } else {
                    std::vector<WireframeModel> firstReplacement;
                    firstReplacement.push_back(result->first);
                    std::vector<WireframeModel> secondReplacement;
                    secondReplacement.push_back(result->second);
                    document_.replaceModel(firstIndex, std::move(firstReplacement));
                    document_.replaceModel(*hit, std::move(secondReplacement));
                    document_.addModel(result->arc);
                    cancelTransformCommand();
                }
            }
        } else if (transformPhase_ == TransformPhase::Selecting) {
            if (selectionFirstCorner_) completeWindowSelection(x, y);
            else if (!toggleModelSelection(x, y)) selectionFirstCorner_ = POINT{x, y};
        } else {
            if (transformCommand_ == TransformCommand::Trim ||
                transformCommand_ == TransformCommand::Extend) {
                if (transformPhase_ == TransformPhase::Destination) {
                    cursorScreen_ = {x, y};
                    if (selectionFirstCorner_) {
                        completeTrimExtendTargetSelection(x, y);
                    } else if (const auto target = trimExtendTargetAt(x, y)) {
                        if (mode_ == EditMode::Draw2D) {
                            applyTrimExtendTarget(*target, screenTo2D(x, y));
                        } else {
                        RECT viewport{}; GetClientRect(canvas_, &viewport);
                        WorkPlane targetPlane = workPlane_;
                        if (!modifierBoundaries_.empty() && !modifierBoundaries_.front().vertices().empty())
                            targetPlane.origin = modifierBoundaries_.front().vertices().front();
                        if (const auto point = camera_.unprojectToPlane(
                                {static_cast<double>(x), static_cast<double>(y)},
                                std::max(1L, viewport.right), std::max(1L, viewport.bottom), targetPlane))
                                applyTrimExtendTarget(*target, *point);
                        }
                    } else {
                        selectionFirstCorner_ = POINT{x, y};
                    }
                } else MessageBeep(MB_ICONWARNING);
            } else {
                updateHover(x, y);
                if (hover_) {
                    const Vec3 point = hover_->point;
                    clearTemporaryTracking();
                    commitTransformPoint(point);
                }
            }
        }
        updateControls(); updateStatus(); invalidateCanvas(); return;
    }
    if (drawingActive_) {
        updateHover(x, y);
        if (hover_) {
            const Vec3 point = hover_->point;
            clearTemporaryTracking();
            commitPoint(point);
        }
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
    const bool wasRotating = rotating_;
    rotating_ = false;
    ReleaseCapture();
    if (wasRotating) updateHover(x, y);
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
    if (snapRedraw) updateStatus();
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
    if (character == L'\r' && transformCommand_ == TransformCommand::None &&
        lastTransformCommand_ != TransformCommand::None &&
        shouldRepeatLastModifierOnEnter(drawingActive_, !input_.empty(),
                                        zoomWindowActive_ || workPlanePicking_)) {
        startTransformCommand(lastTransformCommand_);
        updateControls();
        updateStatus();
        invalidateCanvas();
        return;
    }
    const bool transforming = transformCommand_ != TransformCommand::None;
    if (!transforming && !workPlanePicking_ &&
        ((mode_ != EditMode::Draw2D && !drawingActive_) || !dynamicInputEnabled_)) return;
    if (character == L'\r') {
        if (transformCommand_ == TransformCommand::Fillet &&
            transformPhase_ == TransformPhase::Selecting && !input_.empty()) {
            try {
                std::size_t used{};
                const double radius = std::stod(input_, &used);
                if (used != input_.size() || !std::isfinite(radius) || radius <= 0.0)
                    throw std::invalid_argument("fillet radius");
                filletRadius_ = radius;
                input_.clear();
            } catch (...) {
                MessageBeep(MB_ICONWARNING);
            }
        } else if (transformCommand_ == TransformCommand::Fillet &&
                   transformPhase_ == TransformPhase::Selecting) {
            cancelTransformCommand();
        } else if (transforming && transformPhase_ == TransformPhase::Selecting) {
            if (selectionFirstCorner_) selectionFirstCorner_.reset();
            else if (!selectedModels_.empty() &&
                     (transformCommand_ != TransformCommand::Offset || selectedModels_.size() == 1)) {
                if (transformCommand_ == TransformCommand::Delete) {
                    document_.deleteModels(selectedModels_);
                    cancelTransformCommand();
                } else if (transformCommand_ == TransformCommand::Trim ||
                           transformCommand_ == TransformCommand::Extend) {
                    modifierBoundaries_.clear();
                    modifierBoundaries_.reserve(selectedModels_.size());
                    for (const auto index : selectedModels_)
                        if (index < document_.models().size())
                            modifierBoundaries_.push_back(document_.models()[index]);
                    selectedModels_.clear();
                    transformPhase_ = TransformPhase::Destination;
                } else transformPhase_ = TransformPhase::BasePoint;
            }
            else MessageBeep(MB_ICONWARNING);
        } else if ((transformCommand_ == TransformCommand::LinearArray ||
                    transformCommand_ == TransformCommand::PolarArray) &&
                   transformPhase_ == TransformPhase::BasePoint && !arrayItemCount_ && !input_.empty()) {
            try {
                std::size_t used{};
                const auto count = std::stoull(input_, &used);
                if (used != input_.size() || count < 2 || count > 1000)
                    throw std::invalid_argument("array count");
                arrayItemCount_ = static_cast<std::size_t>(count);
                input_.clear();
            } catch (...) {
                MessageBeep(MB_ICONWARNING);
            }
        } else if (transformCommand_ == TransformCommand::Offset &&
                   transformPhase_ == TransformPhase::BasePoint && !input_.empty()) {
            try {
                std::size_t used{};
                const double distance = std::stod(input_, &used);
                if (used != input_.size() || !std::isfinite(distance) || distance <= 0.0)
                    throw std::invalid_argument("offset distance");
                offsetDistance_ = distance;
                transformPhase_ = TransformPhase::Destination;
                input_.clear();
            } catch (...) {
                MessageBeep(MB_ICONWARNING);
            }
        } else if (transforming && transformPhase_ == TransformPhase::Destination && input_.empty() &&
                   (transformCommand_ == TransformCommand::Copy ||
                    transformCommand_ == TransformCommand::Trim ||
                    transformCommand_ == TransformCommand::Extend)) {
            cancelTransformCommand();
        } else
        if (!input_.empty()) {
            const auto origin = workPlanePicking_ && !workPlanePoints_.empty()
                ? std::optional<Vec3>{workPlanePoints_.back()}
                : (transforming ? transformBase_ : anchor_);
            if (const auto point = parseDynamicPoint(input_, origin, hover_ ? std::optional<Vec3>{hover_->point}
                                                                             : std::nullopt)) {
                clearTemporaryTracking();
                if (workPlanePicking_) commitWorkPlanePoint(*point);
                else if (transforming) commitTransformPoint(*point);
                else commitPoint(*point);
                input_.clear();
            }
            else MessageBeep(MB_ICONWARNING);
        } else if (tool_ == DrawTool::Polyline || tool_ == DrawTool::Face3D) cancelDrawing();
    } else if (character == L'\b') {
        if (!input_.empty()) input_.pop_back();
    } else if ((character >= L'0' && character <= L'9') || character == L'-' || character == L'+' ||
               character == L'.' || character == L',' || character == L'<' || character == L'@')
        input_.push_back(character);
    SetCursor(currentCanvasCursor());
    updateStatus(); invalidateCanvas();
}

void Application::commitPoint(const Vec3& point) {
    if (tool_ == DrawTool::Face3D) {
        if (!facePoints_.empty() && point == facePoints_.back()) {
            MessageBeep(MB_ICONWARNING);
            return;
        }
        facePoints_.push_back(point);
        anchor_ = point;
        if (facePoints_.size() == 4) {
            addStyledModel(WireframeModel::face3D(
                {facePoints_[0], facePoints_[1], facePoints_[2], facePoints_[3]}));
            facePoints_.clear();
            anchor_.reset();
        }
        return;
    }
    if (!anchor_) { anchor_ = point; return; }
    const Vec3 start = *anchor_;
    switch (tool_) {
    case DrawTool::Line:
        if (point != start) addStyledModel(WireframeModel::line(start, point));
        anchor_.reset();
        break;
    case DrawTool::Polyline:
        if (point != start) addStyledModel(WireframeModel::line(start, point));
        anchor_ = point;
        break;
    case DrawTool::Rectangle:
        if (point != start) {
            if (mode_ == EditMode::View3D)
                addStyledModel(WireframeModel::rectangleOnPlane(workPlane_, workPlane_.toPlane(start),
                                                                 workPlane_.toPlane(point)));
            else addStyledModel(WireframeModel::rectangle(start, point));
        }
        anchor_.reset();
        break;
    case DrawTool::Circle: {
        double radius{};
        if (mode_ == EditMode::View3D) {
            const Vec2 center = workPlane_.toPlane(start);
            const Vec2 edge = workPlane_.toPlane(point);
            radius = std::hypot(edge.x - center.x, edge.y - center.y);
            if (radius > 0.0) addStyledModel(WireframeModel::circleOnPlane(workPlane_, center, radius));
        } else {
            radius = std::hypot(point.x - start.x, point.y - start.y);
            if (radius > 0.0) addStyledModel(WireframeModel::circle(start, radius));
        }
        anchor_.reset(); break;
    }
    case DrawTool::Face3D:
        break;
    }
}

void Application::clearTemporaryTracking() {
    if (window_) KillTimer(window_, 5);
    temporaryPointDwellCandidate_.reset();
    temporaryTrackingPoints_.clear();
    temporaryTrackingGuides_.clear();
    temporaryDerivedPoints_.clear();
    polarTrackingLocked_ = false;
    temporaryTrackingLocked_ = false;
}

void Application::cancelDrawing() {
    anchor_.reset();
    facePoints_.clear();
    input_.clear();
    clearTemporaryTracking();
}

void Application::startTransformCommand(TransformCommand command) {
    cancelZoomWindow2D();
    if (workPlanePicking_) cancelWorkPlaneCommand();
    cancelDrawing();
    lastTransformCommand_ = command;
    transformCommand_ = command;
    transformPhase_ = TransformPhase::Selecting;
    selectedModels_.clear();
    selectionFirstCorner_.reset();
    transformBase_.reset();
    offsetDistance_.reset();
    filletFirstPick_.reset();
    arrayItemCount_.reset();
    modifierBoundaries_.clear();
    drawingActive_ = false;
    hover_.reset();
    SetCursor(currentCanvasCursor());
}

void Application::toggle3DView() {
    cancelZoomWindow2D();
    if (workPlanePicking_) cancelWorkPlaneCommand();
    if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
    cancelDrawing();
    mode_ = mode_ == EditMode::View3D ? EditMode::Draw2D : EditMode::View3D;
    drawingActive_ = mode_ == EditMode::Draw2D;
}

void Application::setStandardView(StandardView view) {
    cancelZoomWindow2D();
    if (workPlanePicking_) cancelWorkPlaneCommand();
    if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
    cancelDrawing();
    camera_.setView(view);
    mode_ = EditMode::View3D;
    drawingActive_ = false;
}

void Application::zoomExtents2D() {
    cancelZoomWindow2D();
    RECT client{}; GetClientRect(canvas_, &client);
    const auto bounds = document_.bounds();
    if (!bounds) camera_.reset();
    else if (mode_ == EditMode::View3D)
        camera_.fit3D(bounds->minimum, bounds->maximum, std::max(1L, client.right),
                      std::max(1L, client.bottom), 50.0);
    else
        camera_.fit2D(bounds->minimum, bounds->maximum, std::max(1L, client.right),
                      std::max(1L, client.bottom), 50.0);
}

void Application::startZoomWindow2D() {
    if (workPlanePicking_) cancelWorkPlaneCommand();
    if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
    cancelDrawing();
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
    RECT client{}; GetClientRect(canvas_, &client);
    if (std::abs(x - first.x) >= 4 && std::abs(y - first.y) >= 4) {
        if (mode_ == EditMode::View3D) {
            const double factor = std::min(static_cast<double>(std::max(1L, client.right)) /
                                               static_cast<double>(std::abs(x - first.x)),
                                           static_cast<double>(std::max(1L, client.bottom)) /
                                               static_cast<double>(std::abs(y - first.y)));
            camera_.zoom3DAt({0.5 * (first.x + x), 0.5 * (first.y + y)}, factor,
                             std::max(1L, client.right), std::max(1L, client.bottom));
        } else {
            const Vec3 a = screenTo2D(first.x, first.y);
            const Vec3 b = screenTo2D(x, y);
            camera_.fit2D({std::min(a.x, b.x), std::min(a.y, b.y), 0.0},
                          {std::max(a.x, b.x), std::max(a.y, b.y), 0.0},
                          std::max(1L, client.right), std::max(1L, client.bottom), 0.0);
        }
    }
    cancelZoomWindow2D();
    drawingActive_ = mode_ == EditMode::Draw2D;
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
    clearTemporaryTracking();
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
    offsetDistance_.reset();
    filletFirstPick_.reset();
    arrayItemCount_.reset();
    modifierBoundaries_.clear();
    input_.clear();
    drawingActive_ = false;
    clearTemporaryTracking();
    SetCursor(currentCanvasCursor());
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
    if (transformCommand_ == TransformCommand::Offset) {
        selectedModels_.assign(1, *hit);
        selectionFirstCorner_.reset();
        return true;
    }
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

std::optional<std::size_t> Application::trimExtendTargetAt(int x, int y) const {
    if (mode_ == EditMode::Draw2D)
        return hitTestModel2D(screenTo2D(x, y), document_, 10.0 / (60.0 * camera_.zoom()));
    RECT viewport{};
    GetClientRect(canvas_, &viewport);
    return hitTestModel3D({static_cast<double>(x), static_cast<double>(y)}, document_, camera_,
                          std::max(1L, viewport.right), std::max(1L, viewport.bottom), 10.0);
}

bool Application::applyTrimExtendTarget(std::size_t target, const Vec3& pickPoint) {
    if (target >= document_.models().size() || modifierBoundaries_.empty()) return false;
    if (transformCommand_ == TransformCommand::Trim) {
        auto result = mode_ == EditMode::View3D
            ? trimLineOnPlane(document_.models()[target], modifierBoundaries_, pickPoint, workPlane_)
            : trimLine2D(document_.models()[target], modifierBoundaries_, pickPoint);
        if (!result) return false;
        document_.replaceModel(target, std::move(*result));
        return true;
    }
    if (transformCommand_ == TransformCommand::Extend) {
        auto result = mode_ == EditMode::View3D
            ? extendLineOnPlane(document_.models()[target], modifierBoundaries_, pickPoint, workPlane_)
            : extendLine2D(document_.models()[target], modifierBoundaries_, pickPoint);
        if (!result) return false;
        std::vector<WireframeModel> replacement;
        replacement.push_back(std::move(*result));
        document_.replaceModel(target, std::move(replacement));
        return true;
    }
    return false;
}

void Application::completeTrimExtendTargetSelection(int x, int y) {
    if (!selectionFirstCorner_) return;
    const POINT first = *selectionFirstCorner_;
    selectionFirstCorner_.reset();
    if (first.x == x || first.y == y) return;
    const bool crossing = x < first.x;
    std::vector<std::pair<std::size_t, Vec3>> targets;
    if (mode_ == EditMode::Draw2D) {
        const Vec3 firstWorld = screenTo2D(first.x, first.y);
        const Vec3 secondWorld = screenTo2D(x, y);
        const auto hits = selectModelsInRect2D(firstWorld, secondWorld, document_, crossing);
        for (const auto index : hits) {
            if (index >= document_.models().size()) continue;
            if (const auto pick = crossingSelectionPickPoint2D(document_.models()[index],
                                                                firstWorld, secondWorld))
                targets.emplace_back(index, *pick);
        }
    } else {
        RECT viewport{}; GetClientRect(canvas_, &viewport);
        const Vec2 firstScreen{static_cast<double>(first.x), static_cast<double>(first.y)};
        const Vec2 secondScreen{static_cast<double>(x), static_cast<double>(y)};
        const auto hits = selectModelsInRect3D(firstScreen, secondScreen, document_, camera_,
                                               std::max(1L, viewport.right),
                                               std::max(1L, viewport.bottom), crossing);
        for (const auto index : hits) {
            if (index >= document_.models().size()) continue;
            if (const auto pick = crossingSelectionPickPoint3D(
                    document_.models()[index], firstScreen, secondScreen, camera_,
                    std::max(1L, viewport.right), std::max(1L, viewport.bottom)))
                targets.emplace_back(index, *pick);
        }
    }
    std::sort(targets.begin(), targets.end(), [](const auto& left, const auto& right) {
        return left.first > right.first;
    });
    bool changed = false;
    for (const auto& [index, pick] : targets)
        changed = applyTrimExtendTarget(index, pick) || changed;
    if (!changed) MessageBeep(MB_ICONWARNING);
}

void Application::commitTransformPoint(const Vec3& point) {
    if (transformCommand_ == TransformCommand::Trim ||
        transformCommand_ == TransformCommand::Extend) {
        if (transformPhase_ != TransformPhase::Destination || modifierBoundaries_.empty()) return;
        std::optional<std::size_t> target;
        if (mode_ == EditMode::Draw2D)
            target = hitTestModel2D(point, document_, 10.0 / (60.0 * camera_.zoom()));
        else {
            RECT viewport{}; GetClientRect(canvas_, &viewport);
            target = hitTestModel3D({static_cast<double>(cursorScreen_.x),
                                     static_cast<double>(cursorScreen_.y)},
                                    document_, camera_, std::max(1L, viewport.right),
                                    std::max(1L, viewport.bottom), 10.0);
        }
        if (!target || *target >= document_.models().size()) {
            MessageBeep(MB_ICONWARNING);
            return;
        }
        if (!applyTrimExtendTarget(*target, point)) {
            MessageBeep(MB_ICONWARNING);
            return;
        }
        if (modifierCompletesAfterCommit(transformCommand_)) cancelTransformCommand();
        return;
    }
    if (transformCommand_ == TransformCommand::Offset) {
        if (transformPhase_ != TransformPhase::Destination || !offsetDistance_ ||
            selectedModels_.size() != 1 || selectedModels_.front() >= document_.models().size()) return;
        const auto offset = mode_ == EditMode::View3D
            ? offsetModelOnPlane(document_.models()[selectedModels_.front()],
                                 *offsetDistance_, point, workPlane_)
            : offsetModel2D(document_.models()[selectedModels_.front()], *offsetDistance_, point);
        if (offset) {
            document_.addModel(*offset);
            cancelTransformCommand();
        } else {
            MessageBeep(MB_ICONWARNING);
        }
        return;
    }
    if (transformCommand_ == TransformCommand::PolarArray) {
        if (transformPhase_ != TransformPhase::BasePoint || !arrayItemCount_) {
            MessageBeep(MB_ICONWARNING);
            return;
        }
        std::vector<WireframeModel> copies;
        for (const auto index : selectedModels_) {
            if (index >= document_.models().size()) continue;
            auto generated = mode_ == EditMode::View3D
                ? polarArrayOnPlane(document_.models()[index], *arrayItemCount_, point, workPlane_)
                : polarArray2D(document_.models()[index], *arrayItemCount_, point);
            copies.insert(copies.end(), std::make_move_iterator(generated.begin()),
                          std::make_move_iterator(generated.end()));
        }
        for (auto& model : copies) document_.addModel(std::move(model));
        cancelTransformCommand();
        return;
    }
    if (transformPhase_ == TransformPhase::BasePoint) {
        if (transformCommand_ == TransformCommand::LinearArray && !arrayItemCount_) {
            MessageBeep(MB_ICONWARNING);
            return;
        }
        transformBase_ = point;
        transformPhase_ = TransformPhase::Destination;
        SetCursor(currentCanvasCursor());
        return;
    }
    if (transformPhase_ != TransformPhase::Destination || !transformBase_) return;
    if (transformCommand_ == TransformCommand::Mirror) {
        std::vector<WireframeModel> mirrored;
        mirrored.reserve(selectedModels_.size());
        for (const auto index : selectedModels_) {
            if (index >= document_.models().size()) continue;
            const auto model = mode_ == EditMode::View3D
                ? mirrorModelOnPlane(document_.models()[index], *transformBase_, point, workPlane_)
                : mirrorModel2D(document_.models()[index], *transformBase_, point);
            if (!model) {
                MessageBeep(MB_ICONWARNING);
                return;
            }
            mirrored.push_back(std::move(*model));
        }
        for (auto& model : mirrored) document_.addModel(std::move(model));
        cancelTransformCommand();
        return;
    }
    const Vec3 displacement = point - *transformBase_;
    if (transformCommand_ == TransformCommand::LinearArray) {
        std::vector<WireframeModel> copies;
        for (const auto index : selectedModels_) {
            if (index >= document_.models().size()) continue;
            auto generated = linearArray2D(document_.models()[index], *arrayItemCount_, displacement);
            copies.insert(copies.end(), std::make_move_iterator(generated.begin()),
                          std::make_move_iterator(generated.end()));
        }
        if (copies.empty()) {
            MessageBeep(MB_ICONWARNING);
            return;
        }
        for (auto& model : copies) document_.addModel(std::move(model));
        cancelTransformCommand();
        return;
    }
    if (transformCommand_ == TransformCommand::Move) {
        document_.moveModels(selectedModels_, displacement);
        cancelTransformCommand();
    } else if (transformCommand_ == TransformCommand::Copy) {
        document_.copyModels(selectedModels_, displacement);
        if (modifierCompletesAfterCommit(transformCommand_)) cancelTransformCommand();
    }
}

void Application::updateHover(int x, int y) {
    if (!canvas_) return;
    cursorScreen_ = {x, y};
    polarTrackingLocked_ = false;
    temporaryTrackingLocked_ = false;
    const bool snapCommandActive = workPlanePicking_ ||
        commandAllowsSnapping(drawingActive_, transformCommand_, transformPhase_,
                              arrayItemCount_.has_value(), offsetDistance_.has_value());
    if (!snapCommandActive) {
        clearTemporaryTracking();
        hover_.reset();
        return;
    }
    const bool selectingEntities = transformCommand_ != TransformCommand::None &&
                                   transformPhase_ == TransformPhase::Selecting;
    const bool cameraNavigating = wheelNavigating_ || panning2D_ || rotating_ || viewCubeManipulating_;
    if (!shouldEvaluateSnapping(selectingEntities, zoomWindowActive_, cameraNavigating)) {
        KillTimer(window_, 5);
        temporaryPointDwellCandidate_.reset();
        temporaryTrackingGuides_.clear();
        temporaryDerivedPoints_.clear();
        hover_.reset();
        return;
    }
    if (transformCommand_ == TransformCommand::Offset &&
        transformPhase_ == TransformPhase::Destination) {
        if (mode_ == EditMode::Draw2D) hover_ = SnapResult{screenTo2D(x, y), SnapType::None, 0.0};
        else {
            RECT viewport{}; GetClientRect(canvas_, &viewport);
            WorkPlane sidePlane = workPlane_;
            if (!selectedModels_.empty() && selectedModels_.front() < document_.models().size() &&
                !document_.models()[selectedModels_.front()].vertices().empty())
                sidePlane.origin = document_.models()[selectedModels_.front()].vertices().front();
            if (const auto point = camera_.unprojectToPlane({static_cast<double>(x), static_cast<double>(y)},
                                                            std::max(1L, viewport.right),
                                                            std::max(1L, viewport.bottom), sidePlane))
                hover_ = SnapResult{*point, SnapType::None, 0.0};
            else hover_.reset();
        }
        return;
    }
    if (mode_ == EditMode::Draw2D) {
        const auto reference = transformPhase_ == TransformPhase::Destination ? transformBase_ : anchor_;
        hover_ = SnapEngine::snap(screenTo2D(x, y), document_, 10.0 / (60.0 * camera_.zoom()), 1.0,
                                  snapEnabled_, gridSnapEnabled_, reference, &enabledSnapTypes_);
    } else {
        RECT client{}; GetClientRect(canvas_, &client);
        const int width = std::max(1L, client.right);
        const int height = std::max(1L, client.bottom);
        const auto reference = transformPhase_ == TransformPhase::Destination ? transformBase_ : anchor_;
        WorkPlane activePlane = workPlane_;
        if (reference && !workPlanePicking_) activePlane.origin = *reference;
        hover_ = SnapEngine::snap3D({static_cast<double>(x), static_cast<double>(y)}, document_, camera_,
                                    width, height, 10.0, 1.0, activePlane,
                                    snapEnabled_, gridSnapEnabled_, reference, &enabledSnapTypes_);
    }
    const SnapResult rawSnap = *hover_;
    const bool acquirable = polarTrackingEnabled_ && rawSnap.type != SnapType::None &&
                            rawSnap.type != SnapType::Grid;
    const bool alreadyAcquired = std::find(temporaryTrackingPoints_.begin(),
        temporaryTrackingPoints_.end(), rawSnap.point) != temporaryTrackingPoints_.end();
    if (acquirable && !alreadyAcquired) {
        if (!temporaryPointDwellCandidate_ || temporaryPointDwellCandidate_->point != rawSnap.point ||
            temporaryPointDwellCandidate_->type != rawSnap.type) {
            KillTimer(window_, 5);
            temporaryPointDwellCandidate_ = rawSnap;
            SetTimer(window_, 5, 450, nullptr);
        }
    } else {
        KillTimer(window_, 5);
        temporaryPointDwellCandidate_.reset();
    }

    temporaryTrackingGuides_.clear();
    temporaryDerivedPoints_.clear();
    if (polarTrackingEnabled_ && !orthoEnabled_ && !temporaryTrackingPoints_.empty()) {
        const double trackingTolerance = 10.0 / (60.0 * camera_.zoom());
        auto tracking = resolveTemporaryPointTracking(*hover_, temporaryTrackingPoints_, workPlane_,
                                                       trackingTolerance);
        hover_ = tracking.result;
        temporaryTrackingGuides_ = std::move(tracking.guides);
        temporaryDerivedPoints_ = std::move(tracking.derivedPoints);
        polarTrackingLocked_ = tracking.locked;
        temporaryTrackingLocked_ = tracking.locked;
    }
    auto orthoAnchor = transformPhase_ == TransformPhase::Destination ? transformBase_ : anchor_;
    if (!orthoAnchor && workPlanePicking_ && !workPlanePoints_.empty())
        orthoAnchor = workPlanePoints_.back();
    if (orthoEnabled_ && orthoAnchor && hover_) {
        if (mode_ == EditMode::View3D) {
            RECT client{}; GetClientRect(canvas_, &client);
            const bool modifying = transformCommand_ != TransformCommand::None;
            hover_ = applyOrtho3D(*orthoAnchor, {static_cast<double>(x), static_cast<double>(y)}, *hover_,
                                  camera_, std::max(1L, client.right), std::max(1L, client.bottom),
                                  workPlane_, modifying, !modifying);
        } else {
            hover_ = applyOrtho(*orthoAnchor, *hover_, transformCommand_ == TransformCommand::None);
        }
    } else if (polarTrackingEnabled_ && !polarTrackingLocked_ && orthoAnchor && hover_) {
        const SnapResult original = *hover_;
        hover_ = mode_ == EditMode::View3D
            ? applyPolarTracking(*orthoAnchor, *hover_, workPlane_, 90.0, 12.0, true)
            : applyPolarTracking(*orthoAnchor, *hover_, 90.0, 12.0, true);
        polarTrackingLocked_ = hover_->point != original.point;
    }
}

void Application::executeCommand(int id) {
    if (id >= CmdSnapTypeFirst && id < CmdSnapTypeFirst + static_cast<int>(snapChoices.size())) {
        const auto choiceIndex = static_cast<std::size_t>(id - CmdSnapTypeFirst);
        const auto snapIndex = static_cast<std::size_t>(snapChoices[choiceIndex].type);
        enabledSnapTypes_[snapIndex] =
            SendMessageW(snapTypeCheckboxes_[choiceIndex], BM_GETCHECK, 0, 0) == BST_CHECKED;
        updateHover(cursorScreen_.x, cursorScreen_.y);
        updateControls();
        invalidateCanvas();
        SetFocus(canvas_);
        return;
    }
    switch (id) {
    case CmdTabFile: activateRibbonTab(RibbonTab::File); break;
    case CmdTabDrawing: activateRibbonTab(RibbonTab::Drawing); break;
    case CmdTabModify: activateRibbonTab(RibbonTab::Modify); break;
    case CmdTabView: activateRibbonTab(RibbonTab::View); break;
    case CmdTabAids: activateRibbonTab(RibbonTab::Aids); break;
    case CmdNew:
        document_.clear();
        refreshLayerCombo();
        if (workPlanePicking_) cancelWorkPlaneCommand();
        if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
        else cancelDrawing();
        camera_.reset();
        drawingActive_ = mode_ == EditMode::Draw2D;
        break;
    case CmdOpen: openDocument(); break;
    case CmdSave: saveDocument(); break;
    case CmdImportDxf: importDxf(); break;
    case CmdExportDxf: exportDxf(); break;
    case CmdLine: selectTool(DrawTool::Line); break;
    case CmdPolyline: selectTool(DrawTool::Polyline); break;
    case CmdRectangle: selectTool(DrawTool::Rectangle); break;
    case CmdCircle: selectTool(DrawTool::Circle); break;
    case CmdFace3D: selectTool(DrawTool::Face3D); break;
    case CmdCube: addCube(); break;
    case CmdPyramid: addPyramid(); break;
    case CmdResetView: camera_.reset(); break;
    case CmdView3D: toggle3DView(); break;
    case CmdWorkPlane: startWorkPlaneCommand(); break;
    case CmdZoomExtents: zoomExtents2D(); break;
    case CmdZoomWindow: startZoomWindow2D(); break;
    case CmdVisualStyle: {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING | (visualStyle_ == VisualStyle::Wireframe ? MF_CHECKED : 0),
                    CmdWireframe, L"Wireframe");
        AppendMenuW(menu, MF_STRING | (visualStyle_ == VisualStyle::Solid ? MF_CHECKED : 0),
                    CmdSolid, L"Solid");
        AppendMenuW(menu, MF_STRING | (visualStyle_ == VisualStyle::Transparent ? MF_CHECKED : 0),
                    CmdTransparent, L"Saydam");
        RECT buttonRect{};
        GetWindowRect(visualStyleButton_, &buttonRect);
        const UINT selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                             buttonRect.left, buttonRect.bottom, 0, window_, nullptr);
        DestroyMenu(menu);
        if (selected) SendMessageW(window_, WM_COMMAND, selected, 0);
        break;
    }
    case CmdStandardView: {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, CmdViewTop, L"Üstten Görünüş");
        AppendMenuW(menu, MF_STRING, CmdViewBottom, L"Alttan Görünüş");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, CmdViewFront, L"Önden Görünüş");
        AppendMenuW(menu, MF_STRING, CmdViewBack, L"Arkadan Görünüş");
        AppendMenuW(menu, MF_STRING, CmdViewLeft, L"Sol Görünüş");
        AppendMenuW(menu, MF_STRING, CmdViewRight, L"Sağ Görünüş");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, CmdViewIsometric, L"ISO Görünüş");
        RECT buttonRect{};
        GetWindowRect(standardViewButton_, &buttonRect);
        const UINT selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                             buttonRect.left, buttonRect.bottom, 0, window_, nullptr);
        DestroyMenu(menu);
        if (selected) SendMessageW(window_, WM_COMMAND, selected, 0);
        break;
    }
    case CmdWireframe: visualStyle_ = VisualStyle::Wireframe; break;
    case CmdSolid: visualStyle_ = VisualStyle::Solid; break;
    case CmdTransparent: visualStyle_ = VisualStyle::Transparent; break;
    case CmdViewFront: setStandardView(StandardView::Front); break;
    case CmdViewBack: setStandardView(StandardView::Back); break;
    case CmdViewLeft: setStandardView(StandardView::Left); break;
    case CmdViewRight: setStandardView(StandardView::Right); break;
    case CmdViewIsometric: setStandardView(StandardView::Isometric); break;
    case CmdViewTop: setStandardView(StandardView::Top); break;
    case CmdViewBottom: setStandardView(StandardView::Bottom); break;
    case CmdOsnap: snapEnabled_ = !snapEnabled_; break;
    case CmdGridSnap: gridSnapEnabled_ = !gridSnapEnabled_; break;
    case CmdDynamicInput: dynamicInputEnabled_ = !dynamicInputEnabled_; break;
    case CmdPolarTracking:
        polarTrackingEnabled_ = !polarTrackingEnabled_;
        if (polarTrackingEnabled_) orthoEnabled_ = false;
        else clearTemporaryTracking();
        break;
    case CmdSnapSettings:
        snapPanelOpen_ = !snapPanelOpen_;
        updateSnapPanelVisibility();
        break;
    case CmdNeutral: deactivateAllCommands(); break;
    case CmdMove: startTransformCommand(TransformCommand::Move); break;
    case CmdCopy: startTransformCommand(TransformCommand::Copy); break;
    case CmdOffset: startTransformCommand(TransformCommand::Offset); break;
    case CmdMirror: startTransformCommand(TransformCommand::Mirror); break;
    case CmdDelete: startTransformCommand(TransformCommand::Delete); break;
    case CmdLinearArray: startTransformCommand(TransformCommand::LinearArray); break;
    case CmdPolarArray: startTransformCommand(TransformCommand::PolarArray); break;
    case CmdTrim: startTransformCommand(TransformCommand::Trim); break;
    case CmdExtend: startTransformCommand(TransformCommand::Extend); break;
    case CmdFillet: startTransformCommand(TransformCommand::Fillet); break;
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

void Application::deactivateAllCommands() {
    cancelZoomWindow2D();
    if (workPlanePicking_) cancelWorkPlaneCommand();
    if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
    cancelDrawing();
    drawingActive_ = false;
    lastTransformCommand_ = TransformCommand::None;
    hover_.reset();
    SetCursor(currentCanvasCursor());
}

void Application::updateControls() {
    const auto check = [](HWND control, bool value) {
        if (control) SendMessageW(control, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
    };
    check(lineButton_, drawingActive_ && tool_ == DrawTool::Line);
    check(polylineButton_, drawingActive_ && tool_ == DrawTool::Polyline);
    check(rectangleButton_, drawingActive_ && tool_ == DrawTool::Rectangle);
    check(circleButton_, drawingActive_ && tool_ == DrawTool::Circle);
    check(face3DButton_, drawingActive_ && tool_ == DrawTool::Face3D);
    check(snapButton_, snapEnabled_);
    check(gridSnapButton_, gridSnapEnabled_);
    check(dynamicInputButton_, dynamicInputEnabled_);
    check(snapSettingsButton_, snapPanelOpen_);
    check(polarTrackingButton_, polarTrackingEnabled_);
    check(neutralButton_, !drawingActive_ && transformCommand_ == TransformCommand::None &&
                          !workPlanePicking_ && !zoomWindowActive_);
    check(moveButton_, transformCommand_ == TransformCommand::Move);
    check(copyButton_, transformCommand_ == TransformCommand::Copy);
    check(offsetButton_, transformCommand_ == TransformCommand::Offset);
    check(mirrorButton_, transformCommand_ == TransformCommand::Mirror);
    check(deleteButton_, transformCommand_ == TransformCommand::Delete);
    check(linearArrayButton_, transformCommand_ == TransformCommand::LinearArray);
    check(polarArrayButton_, transformCommand_ == TransformCommand::PolarArray);
    check(trimButton_, transformCommand_ == TransformCommand::Trim);
    check(extendButton_, transformCommand_ == TransformCommand::Extend);
    check(filletButton_, transformCommand_ == TransformCommand::Fillet);
    check(view3DButton_, mode_ == EditMode::View3D);
    if (visualStyleButton_) {
        const wchar_t* label = L"◇\r\nWireframe ▼";
        if (visualStyle_ == VisualStyle::Solid) label = L"◆\r\nSolid ▼";
        else if (visualStyle_ == VisualStyle::Transparent) label = L"◈\r\nSaydam ▼";
        SetWindowTextW(visualStyleButton_, label);
    }
    check(workPlaneButton_, workPlanePicking_);
    check(zoomWindowButton_, zoomWindowActive_);
    updateStatus();
}

void Application::refreshLayerCombo() {
    if (!layerCombo_) return;
    wchar_t previous[256]{};
    GetWindowTextW(layerCombo_, previous, static_cast<int>(std::size(previous)));
    std::vector<std::string> layers{"0"};
    for (const auto& [name, properties] : document_.layers()) {
        (void)properties;
        if (name != "0") layers.push_back(name);
    }
    std::sort(layers.begin() + 1, layers.end());
    SendMessageW(layerCombo_, CB_RESETCONTENT, 0, 0);
    for (const auto& name : layers) {
        const auto wideName = utf8ToWide(name);
        SendMessageW(layerCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wideName.c_str()));
    }
    LRESULT selection = previous[0]
        ? SendMessageW(layerCombo_, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                       reinterpret_cast<LPARAM>(previous))
        : CB_ERR;
    if (selection == CB_ERR) selection = 0;
    SendMessageW(layerCombo_, CB_SETCURSEL, selection, 0);
}

EntityProperties Application::currentEntityProperties() const {
    wchar_t layerText[256]{L'0', L'\0'};
    if (layerCombo_) GetWindowTextW(layerCombo_, layerText, static_cast<int>(std::size(layerText)));
    EntityStyleSelection selection;
    selection.layer = wideToUtf8(layerText);
    const LRESULT colorIndex = colorCombo_ ? SendMessageW(colorCombo_, CB_GETCURSEL, 0, 0) : 0;
    if (colorIndex >= 0 && static_cast<std::size_t>(colorIndex) < colorChoices.size())
        selection.trueColor = colorChoices[static_cast<std::size_t>(colorIndex)].color;
    const LRESULT lineTypeIndex = lineTypeCombo_ ? SendMessageW(lineTypeCombo_, CB_GETCURSEL, 0, 0) : 0;
    if (lineTypeIndex >= 0 && static_cast<std::size_t>(lineTypeIndex) < lineTypeChoices.size())
        selection.lineType = lineTypeChoices[static_cast<std::size_t>(lineTypeIndex)].value;
    return resolveEntityStyle(selection, document_.layers());
}

void Application::addStyledModel(WireframeModel model) {
    model.setProperties(currentEntityProperties());
    document_.addModel(std::move(model));
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
    text += mode_ == EditMode::View3D ? L"3B Paralel" : L"2B Plan XY";
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
        if (transformCommand_ == TransformCommand::Move) text += L"MOVE";
        else if (transformCommand_ == TransformCommand::Copy) text += L"COPY";
        else if (transformCommand_ == TransformCommand::Offset) text += L"OFFSET";
        else if (transformCommand_ == TransformCommand::Mirror) text += L"MIRROR";
        else if (transformCommand_ == TransformCommand::Delete) text += L"DELETE";
        else if (transformCommand_ == TransformCommand::LinearArray) text += L"LINEAR ARRAY";
        else if (transformCommand_ == TransformCommand::PolarArray) text += L"POLAR ARRAY";
        else if (transformCommand_ == TransformCommand::Trim) text += L"TRIM";
        else if (transformCommand_ == TransformCommand::Extend) text += L"EXTEND";
        else text += L"FILLET";
        text += L" — ";
        if (transformPhase_ == TransformPhase::Selecting) {
            if (transformCommand_ == TransformCommand::Fillet) {
                wchar_t radius[64]{};
                std::swprintf(radius, std::size(radius), L"R=%.3f — ", filletRadius_);
                text += radius;
                text += selectedModels_.empty()
                    ? L"İlk çizginin korunacak tarafını seçin; yeni yarıçapı yazıp Enter'a basabilirsiniz"
                    : L"İkinci çizginin korunacak tarafını seçin";
            } else if (transformCommand_ == TransformCommand::Offset) text += L"Bir çizgi veya daire seçin, Enter";
            else if (transformCommand_ == TransformCommand::Trim ||
                     transformCommand_ == TransformCommand::Extend)
                text += selectionFirstCorner_ ? L"Diğer köşeyi belirtin" : L"Sınır nesnelerini seçin, Enter";
            else text += selectionFirstCorner_ ? L"Diğer köşeyi belirtin" : L"Nesneleri seçin, Enter";
            text += L" (" + std::to_wstring(selectedModels_.size()) + L")";
        } else if (transformPhase_ == TransformPhase::BasePoint) {
            if (transformCommand_ == TransformCommand::Offset) text += L"Ofset mesafesini yazın, Enter";
            else if ((transformCommand_ == TransformCommand::LinearArray ||
                      transformCommand_ == TransformCommand::PolarArray) && !arrayItemCount_)
                text += L"Öğe sayısını yazın (2-1000), Enter";
            else if (transformCommand_ == TransformCommand::PolarArray) text += L"Merkez noktasını belirtin";
            else if (transformCommand_ == TransformCommand::Mirror) text += L"Ayna ekseninin ilk noktasını belirtin";
            else text += L"Baz noktayı belirtin";
        } else if (transformCommand_ == TransformCommand::Offset) {
            text += L"Ofset tarafını belirtin";
        } else if (transformCommand_ == TransformCommand::Mirror) {
            text += L"Ayna ekseninin ikinci noktasını belirtin";
        } else if (transformCommand_ == TransformCommand::LinearArray) {
            text += L"Öğeler arası aralık için ikinci noktayı belirtin";
        } else if (transformCommand_ == TransformCommand::Trim) {
            text += L"Kesilecek çizgi bölümünü tıklayın veya crossing pencere çizin; Enter ile bitir";
        } else if (transformCommand_ == TransformCommand::Extend) {
            text += L"Uzatılacak çizginin uçlarını tıklayın veya crossing pencere çizin; Enter ile bitir";
        } else text += L"İkinci noktayı belirtin";
    } else if (!drawingActive_) {
        text += L"  |  PASİF — Komut yok; Snap pasif";
    } else {
        text += L"  |  Araç: "; text += toolLabel(tool_);
        if (tool_ == DrawTool::Face3D)
            text += L" — " + std::to_wstring(facePoints_.size() + 1) + L". köşeyi belirtin (4 köşe)";
    }
    text += L"  |  Nesne: " + std::to_wstring(document_.models().size());
    const bool snapEffective = snapEnabled_ && (workPlanePicking_ ||
        commandAllowsSnapping(drawingActive_, transformCommand_, transformPhase_,
                              arrayItemCount_.has_value(), offsetDistance_.has_value()));
    text += L"  |  OSNAP: ";
    text += snapEffective ? L"Açık" : (!drawingActive_ && transformCommand_ == TransformCommand::None
                                       ? L"Pasif (komut yok)" : L"Kapalı");
    text += L"  |  ORTHO F8: "; text += orthoEnabled_ ? L"Açık" : L"Kapalı";
    if (orthoEnabled_ && mode_ == EditMode::View3D)
        text += transformCommand_ == TransformCommand::None ? L" (U/V)" : L" (U/V/N)";
    text += L"  |  POLAR F10: ";
    text += polarTrackingEnabled_ ? (polarTrackingLocked_ ? L"90° İzleme" : L"90° Açık") : L"Kapalı";
    if (polarTrackingEnabled_ && !temporaryTrackingPoints_.empty()) {
        text += L"  |  TEMP: " + std::to_wstring(temporaryTrackingPoints_.size());
        if (temporaryTrackingLocked_ && hover_) {
            text += L" (";
            text += snapTypeLabel(hover_->type);
            text += L")";
        }
    }
    text += L"  |  GRID: "; text += gridSnapEnabled_ ? L"Açık" : L"Kapalı";
    text += L"  |  DYN: "; text += dynamicInputEnabled_ ? L"Açık" : L"Kapalı";
    text += L"  |  Görünüm: ";
    switch (visualStyle_) {
    case VisualStyle::Wireframe: text += L"Wireframe"; break;
    case VisualStyle::Solid: text += L"Solid"; break;
    case VisualStyle::Transparent: text += L"Saydam"; break;
    }
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
    addStyledModel(std::move(cube)); mode_ = EditMode::View3D;
    cancelDrawing(); drawingActive_ = false;
}

void Application::addPyramid() {
    if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
    auto pyramid = WireframeModel::pyramid(3.0, 3.2); pyramid.translate({0.0, 0.0, -1.0});
    addStyledModel(std::move(pyramid)); mode_ = EditMode::View3D;
    cancelDrawing(); drawingActive_ = false;
}

Vec3 Application::screenTo2D(int x, int y) const noexcept {
    RECT client{}; GetClientRect(canvas_, &client);
    return camera_.unproject2D({static_cast<double>(x), static_cast<double>(y)},
                               client.right, client.bottom);
}

HCURSOR Application::currentCanvasCursor() const noexcept {
    if (transformCommand_ == TransformCommand::None)
        return (drawingActive_ || workPlanePicking_ || zoomWindowActive_) ? draftingCursor_ : neutralCursor_;
    return modifierUsesPointCursor(transformCommand_, transformPhase_, arrayItemCount_.has_value(),
                                   offsetDistance_.has_value())
        ? draftingCursor_ : modifyCursor_;
}

DraftView Application::draftView() const {
    DraftView view;
    view.tool = tool_; view.visualStyle = visualStyle_; view.anchor = anchor_; view.facePoints = facePoints_;
    if (hover_) { view.cursor = hover_->point; view.snapType = hover_->type; }
    view.drawingActive = drawingActive_; view.snapEnabled = snapEnabled_;
    view.gridSnapEnabled = gridSnapEnabled_; view.dynamicInputEnabled = dynamicInputEnabled_;
    view.polarTrackingEnabled = polarTrackingEnabled_; view.polarTrackingLocked = polarTrackingLocked_;
    view.temporaryTrackingLocked = temporaryTrackingLocked_;
    view.temporaryTrackingPoints = temporaryTrackingPoints_;
    view.temporaryTrackingGuides = temporaryTrackingGuides_;
    view.temporaryDerivedPoints = temporaryDerivedPoints_;
    view.workPlaneZ = workPlane_.origin.z; view.workPlane = workPlane_;
    view.workPlanePicking = workPlanePicking_; view.workPlanePoints = workPlanePoints_;
    view.input = input_; view.cursorScreen = cursorScreen_;
    view.transformCommand = transformCommand_; view.transformPhase = transformPhase_;
    view.selectedModels = selectedModels_; view.selectionFirstCorner = selectionFirstCorner_;
    view.transformBase = transformBase_;
    view.offsetDistance = offsetDistance_;
    view.filletRadius = filletRadius_;
    view.filletFirstPick = filletFirstPick_;
    view.arrayItemCount = arrayItemCount_;
    view.modifierBoundaries = modifierBoundaries_;
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
        refreshLayerCombo();
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
        refreshLayerCombo();
        const auto bounds = document_.bounds();
        drawingActive_ = mode_ == EditMode::Draw2D;
        if (bounds) {
            RECT canvasClient{};
            GetClientRect(canvas_, &canvasClient);
            if (mode_ == EditMode::View3D)
                camera_.fit3D(bounds->minimum, bounds->maximum,
                              std::max(1L, canvasClient.right), std::max(1L, canvasClient.bottom), 50.0);
            else
                camera_.fit2D(bounds->minimum, bounds->maximum,
                              std::max(1L, canvasClient.right), std::max(1L, canvasClient.bottom), 50.0);
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
