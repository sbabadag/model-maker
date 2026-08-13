#include "model_maker/application.hpp"
#include "model_maker/dxf.hpp"
#include "model_maker/force_diagram.hpp"
#include "model_maker/render_backend.hpp"
#include "model_maker/view_cube.hpp"

#include <commdlg.h>
#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <stdexcept>
#include <string>

namespace mm {
namespace {
constexpr int ribbonHeight = RibbonLayout::height;
constexpr int statusHeight = 27;
constexpr int commandBarHeight = 22;
constexpr wchar_t className[] = L"ModelMakerWindow";
constexpr wchar_t canvasClassName[] = L"ModelMakerCanvas";
constexpr wchar_t viewCubeClassName[] = L"ModelMakerViewCube";

enum CommandId {
    CmdNew = 100, CmdOpen, CmdSave, CmdImportDxf, CmdExportDxf,
    CmdLine = 200, CmdPolyline, CmdRectangle, CmdCircle, CmdFace3D, CmdLayerManager,
    CmdCube = 300, CmdPyramid, CmdResetView, CmdView3D, CmdWorkPlane, CmdZoomExtents, CmdZoomWindow,
    CmdVisualStyle, CmdStandardView, CmdWireframe = 310, CmdSolid, CmdTransparent,
    CmdViewFront = 320, CmdViewBack, CmdViewLeft, CmdViewRight, CmdViewIsometric,
    CmdViewTop, CmdViewBottom,
    CmdOsnap = 400, CmdGridSnap, CmdDynamicInput, CmdSnapSettings, CmdPolarTracking,
    CmdMove = 500, CmdCopy, CmdOffset, CmdMirror, CmdDelete, CmdLinearArray, CmdPolarArray,
    CmdTrim, CmdExtend, CmdNeutral, CmdFillet, CmdRotate, CmdRotate3D,
    CmdTabFile = 600, CmdTabDrawing, CmdTabModify, CmdTabView, CmdTabAids,
    CmdUndo = 610, CmdRedo,
    CmdDxfProgress = 700, CmdSnapTypeFirst = 720,
    CmdSapExport = 710,
    CmdOpenSeesExport = 711,
    CmdOpenSeesRun = 712,
    CmdOpenSeesClear = 713,
    CmdAnalyze = 714,
    CmdNodeConstraint = 715,
    CmdClearNodeConstraints = 716,
    CmdNodeDofCombo = 717,
    CmdNodeDofApply = 718,
    CmdNodeDofClose = 719,
    CmdBeamLoad = 750,
    CmdClearBeamLoads = 751,
    CmdLayerCombo = 800, CmdColorCombo, CmdLineTypeCombo,
    CmdProfileCombo = 810,
    CmdResultNone = 800,
    CmdResultDeformed = 801,
    CmdResultMomentY = 802,
    CmdResultMomentZ = 803,
    CmdResultAxial = 804,
    CmdResultShearY = 805,
    CmdResultShearZ = 806,
    CmdDepthZPlus = 807,
    CmdDepthZMinus = 808,
    CmdDepthToggle = 809,
    CmdProfilePanel = 815,
    CmdLayerNew = 820, CmdLayerDelete, CmdLayerSetCurrent, CmdLayerRefresh, CmdLayerClose,
    CmdLayerSearch = 830, CmdLayerTree, CmdLayerList, CmdLayerCellEdit,
    CmdCommandBar = 850,
    CmdUcsCommand = 900,
    CmdUcs3Point = 901, CmdUcsZAxis, CmdUcsView, CmdUcsWorld, CmdUcsObject,
    CmdUcsX, CmdUcsY, CmdUcsZ
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
constexpr std::array<ProfileDefinition, 19> profileDefinitions{{
    {L"<None>", "", "", 0, 0, 0, 0, 0},
    {L"IPE 100", "IPE100", "I", 100, 55, 4.1, 2.7, 10.3},
    {L"IPE 120", "IPE120", "I", 120, 64, 4.4, 2.7, 13.2},
    {L"IPE 140", "IPE140", "I", 140, 73, 4.7, 2.7, 16.4},
    {L"IPE 160", "IPE160", "I", 160, 82, 5.0, 3.0, 20.1},
    {L"IPE 180", "IPE180", "I", 180, 91, 5.3, 3.0, 23.9},
    {L"IPE 200", "IPE200", "I", 200, 100, 5.6, 3.4, 28.5},
    {L"IPE 220", "IPE220", "I", 220, 110, 5.9, 3.6, 33.4},
    {L"IPE 240", "IPE240", "I", 240, 120, 6.2, 3.8, 39.1},
    {L"IPE 270", "IPE270", "I", 270, 135, 6.6, 4.2, 45.9},
    {L"IPE 300", "IPE300", "I", 300, 150, 7.1, 4.5, 53.8},
    {L"HEA 100", "HEA100", "I", 96, 100, 5.0, 3.6, 21.2},
    {L"HEA 120", "HEA120", "I", 114, 120, 5.5, 3.8, 25.3},
    {L"HEA 140", "HEA140", "I", 133, 140, 5.5, 3.8, 31.4},
    {L"HEA 160", "HEA160", "I", 152, 160, 6.0, 4.0, 38.8},
    {L"HEA 180", "HEA180", "I", 171, 180, 6.0, 4.5, 45.3},
    {L"HEA 200", "HEA200", "I", 190, 200, 6.5, 5.0, 53.8},
    {L"CHS 88.9x3.2", "CHS88.9x3.2", "CHS", 88.9, 88.9, 3.2, 3.2, 8.6},
    {L"CHS 114.3x3.6", "CHS114.3x3.6", "CHS", 114.3, 114.3, 3.6, 3.6, 12.5},
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
    wchar_t exePath[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH))
        optionsPath_ = std::filesystem::path(exePath).replace_filename(L"model-maker-options.cfg");
    else
        optionsPath_ = L"model-maker-options.cfg";
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
    loadOptions();
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

    WNDCLASSEXW viewCubeClass{};
    viewCubeClass.cbSize = sizeof(viewCubeClass);
    viewCubeClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    viewCubeClass.lpfnWndProc = viewCubeProc;
    viewCubeClass.hInstance = instance_;
    viewCubeClass.hCursor = LoadCursorW(nullptr, IDC_HAND);
    viewCubeClass.hbrBackground = nullptr;
    viewCubeClass.lpszClassName = viewCubeClassName;
    if (!RegisterClassExW(&viewCubeClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        throw std::runtime_error("ViewCube window class could not be registered");

    window_ = CreateWindowExW(0, className, L"Model Maker — Professional Wireframe CAD",
                              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                              1280, 820, nullptr, nullptr, instance_, this);
    if (!window_) throw std::runtime_error("Main window could not be created");

    createControlPanel();
    RECT client{};
    GetClientRect(window_, &client);
    layoutChildren(client.right, client.bottom);
    updateControls();
    camera_.setView(StandardView::Top);
    ShowWindow(window_, showCommand);
    UpdateWindow(window_);
    SetFocus(canvas_);
}

void Application::createControlPanel() {
    INITCOMMONCONTROLSEX commonControls{sizeof(commonControls),
        ICC_PROGRESS_CLASS | ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_STANDARD_CLASSES};
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
    addCommand(L"↩\r\nGeri Al", CmdUndo);
    addCommand(L"↪\r\nYinele", CmdRedo);
    addCommand(L"SAP\r\nS2K", CmdSapExport);
    addCommand(L"Open\r\nSees", CmdOpenSeesExport);
    addCommand(L"▶\r\nÇöz", CmdOpenSeesRun);
    addCommand(L"✕\r\nTemizle", CmdOpenSeesClear);
    addCommand(L"↓\r\nYayılı Yük", CmdBeamLoad);
    addCommand(L"⚡\r\nAnaliz", CmdAnalyze);
    addCommand(L"✕\r\nSonuçlar", CmdResultNone);
    addCommand(L"━\r\nDeforme", CmdResultDeformed);
    addCommand(L"━\r\nMy", CmdResultMomentY);
    addCommand(L"━\r\nMz", CmdResultMomentZ);
    addCommand(L"━\r\nN", CmdResultAxial);
    addCommand(L"━\r\nVy", CmdResultShearY);
    addCommand(L"━\r\nVz", CmdResultShearZ);
    addCommand(L"Z+\r\nGeri", CmdDepthZPlus);
    addCommand(L"Z-\r\nİleri", CmdDepthZMinus);
    addCommand(L"⊞\r\nKırpma", CmdDepthToggle, BS_AUTOCHECKBOX | BS_PUSHLIKE);

    lineButton_ = addCommand(L"╱\r\nÇizgi", CmdLine, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    polylineButton_ = addCommand(L"⌁\r\nPolyline", CmdPolyline, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    rectangleButton_ = addCommand(L"□\r\nDikdört.", CmdRectangle, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    circleButton_ = addCommand(L"○\r\nDaire", CmdCircle, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    face3DButton_ = addCommand(L"▱\r\n3DFACE", CmdFace3D, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    layerManagerButton_ = addCommand(L"▤\r\nKatmanlar", CmdLayerManager,
                                     BS_AUTOCHECKBOX | BS_PUSHLIKE);

    addCommand(L"◇\r\nKüp", CmdCube);
    addCommand(L"△\r\nPiramit", CmdPyramid);
    addCommand(L"⌂\r\nSıfırla", CmdResetView);
    view3DButton_ = addCommand(L"3B\r\n2B / 3B", CmdView3D, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    workPlaneButton_ = addCommand(L"▱\r\nDüzlem", CmdWorkPlane, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    ucsButton_ = addCommand(L"⊞\r\nKOS", CmdUcsCommand);
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
    rotateButton_ = addCommand(L"↻\r\nDöndür", CmdRotate, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    rotate3DButton_ = addCommand(L"↻3D\r\n3B Döndür", CmdRotate3D, BS_AUTOCHECKBOX | BS_PUSHLIKE);

    canvas_ = CreateWindowExW(WS_EX_CLIENTEDGE, canvasClassName, nullptr,
                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                              0, ribbonHeight, 800, 600, window_, nullptr, instance_, this);
    viewCube_ = CreateWindowExW(0, viewCubeClassName, nullptr,
                                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                596, 12, 192, 238, canvas_, nullptr, instance_, this);
    if (!viewCube_) throw std::runtime_error("ViewCube child window could not be created");

    // Initialize the render backend (GDI for now — DX11 backend later)
    renderBackend_ = createGdiRenderBackend();
    RECT canvasClient{};
    GetClientRect(canvas_, &canvasClient);
    renderBackend_->initialize(canvas_, canvasClient.right, canvasClient.bottom);

    status_ = CreateWindowExW(0, L"STATIC", nullptr,
                              WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_LEFT | SS_CENTERIMAGE,
                              0, 700, 1000, statusHeight, window_, nullptr, instance_, nullptr);
    SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    dxfProgressBar_ = CreateWindowExW(0, PROGRESS_CLASSW, nullptr,
                                      WS_CHILD | WS_CLIPSIBLINGS | PBS_SMOOTH,
                                      0, 0, 280, statusHeight - 6, window_,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(CmdDxfProgress)), instance_, nullptr);
    SendMessageW(dxfProgressBar_, PBM_SETRANGE32, 0, 100);
    commandBarPrompt_ = CreateWindowExW(0, L"STATIC", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_LEFT | SS_CENTERIMAGE,
        0, 0, 1, 22, window_, nullptr, instance_, nullptr);
    SendMessageW(commandBarPrompt_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    commandBar_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 1, 22, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(CmdCommandBar)), instance_, nullptr);
    SendMessageW(commandBar_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    SetWindowSubclass(commandBar_, [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                       UINT_PTR, DWORD_PTR ref) -> LRESULT {
        auto* app = reinterpret_cast<Application*>(ref);
        if (msg == WM_CHAR && wParam == VK_RETURN) {
            const int length = GetWindowTextLengthW(hwnd);
            if (length > 0) {
                std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
                GetWindowTextW(hwnd, text.data(), length + 1);
                text.resize(static_cast<std::size_t>(length));
                if (app->transformCommand_ != TransformCommand::None ||
                    app->workPlanePicking_ || app->drawingActive_) {
                    if (app->commandBarInput(text)) {
                        app->commandHistory_.push_back(text);
                        app->commandHistoryIndex_ = app->commandHistory_.size();
                        if (app->transformCommand_ == TransformCommand::None &&
                            !app->workPlanePicking_ && !app->drawingActive_)
                            SetWindowTextW(hwnd, L"");
                    } else {
                        SetWindowTextW(hwnd, L"");
                        MessageBeep(MB_ICONWARNING);
                    }
                } else {
                    app->processCommandLine(text);
                    SetWindowTextW(hwnd, L"");
                    app->commandHistory_.push_back(text);
                    app->commandHistoryIndex_ = app->commandHistory_.size();
                }
            }
            return 0;
        }
        if (msg == WM_CHAR && wParam == VK_ESCAPE && app) {
            if (app->transformCommand_ != TransformCommand::None)
                app->cancelTransformCommand();
            else app->cancelDrawing();
            app->updateControls(); app->updateStatus(); app->invalidateCanvas();
            SetWindowTextW(hwnd, L"");
            SetFocus(app->canvas_);
            return 0;
        }
        if (msg == WM_KEYDOWN && wParam == VK_UP && app && !app->commandHistory_.empty()) {
            if (app->commandHistoryIndex_ > 0) {
                --app->commandHistoryIndex_;
                SetWindowTextW(hwnd, app->commandHistory_[app->commandHistoryIndex_].c_str());
                SendMessageW(hwnd, EM_SETSEL, 0, -1);
            }
            return 0;
        }
        if (msg == WM_KEYDOWN && wParam == VK_DOWN && app && !app->commandHistory_.empty()) {
            if (app->commandHistoryIndex_ + 1 < app->commandHistory_.size()) {
                ++app->commandHistoryIndex_;
                SetWindowTextW(hwnd, app->commandHistory_[app->commandHistoryIndex_].c_str());
            } else {
                app->commandHistoryIndex_ = app->commandHistory_.size();
                SetWindowTextW(hwnd, L"");
            }
            SendMessageW(hwnd, EM_SETSEL, 0, -1);
            return 0;
        }
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }, 0, reinterpret_cast<DWORD_PTR>(this));
    snapPanel_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"  Aktif Nesne Yakalamaları",
                                 WS_CHILD | WS_CLIPSIBLINGS | SS_LEFT,
                                 0, 0, 365, 250, window_, nullptr, instance_, nullptr);
    SendMessageW(snapPanel_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);

    // DOF node constraint panel
    nodeDofPanel_ = CreateWindowExW(WS_EX_CLIENTEDGE | WS_EX_CONTROLPARENT, L"STATIC", L"  Düğüm Serbestlikleri",
                                    WS_CHILD | WS_CLIPSIBLINGS | SS_LEFT,
                                    0, 0, 210, 48, window_, nullptr, instance_, nullptr);
    SendMessageW(nodeDofPanel_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    nodeDofCombo_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_COMBOBOXW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_HASSTRINGS,
        0, 0, 110, 180, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(CmdNodeDofCombo)), instance_, nullptr);
    SendMessageW(nodeDofCombo_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    SendMessageW(nodeDofCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Fixed"));
    SendMessageW(nodeDofCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Pinned"));
    SendMessageW(nodeDofCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Free"));
    SendMessageW(nodeDofCombo_, CB_SETCURSEL, 0, 0);
    nodeDofApply_ = CreateWindowExW(0, L"BUTTON", L"Uygula",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 76, 25, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(CmdNodeDofApply)), instance_, nullptr);
    SendMessageW(nodeDofApply_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    // Close button
    nodeDofClose_ = CreateWindowExW(0, L"BUTTON", L"✕",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 22, 22, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(CmdNodeDofClose)), instance_, nullptr);
    SendMessageW(nodeDofClose_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    ShowWindow(nodeDofPanel_, SW_HIDE);
    ShowWindow(nodeDofCombo_, SW_HIDE);
    ShowWindow(nodeDofApply_, SW_HIDE);
    ShowWindow(nodeDofClose_, SW_HIDE);
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
    styleLabels_[3] = createStyleLabel(L"Profile");
    layerCombo_ = createCombo(CmdLayerCombo, 150);
    colorCombo_ = createCombo(CmdColorCombo, 120);
    lineTypeCombo_ = createCombo(CmdLineTypeCombo, 130);
    profileCombo_ = createCombo(CmdProfileCombo, 140);
    for (const auto& choice : colorChoices)
        SendMessageW(colorCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.label));
    for (const auto& choice : lineTypeChoices)
        SendMessageW(lineTypeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.label));
    for (const auto& profile : profileDefinitions)
        SendMessageW(profileCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(profile.label));
    SendMessageW(colorCombo_, CB_SETCURSEL, 0, 0);
    SendMessageW(lineTypeCombo_, CB_SETCURSEL, 0, 0);
    SendMessageW(profileCombo_, CB_SETCURSEL, 0, 0);

    layerPanel_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 660, 500,
        window_, nullptr, instance_, nullptr);
    layerTitle_ = CreateWindowExW(0, L"STATIC", L"Current layer: 0",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        0, 0, 300, 27, window_, nullptr, instance_, nullptr);
    SendMessageW(layerTitle_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);

    const std::array<std::pair<const wchar_t*, int>, 5> layerTools{{
        {L"＋ New Layer", CmdLayerNew}, {L"✕ Delete", CmdLayerDelete},
        {L"✓ Set Current", CmdLayerSetCurrent}, {L"↻ Refresh", CmdLayerRefresh},
        {L"×", CmdLayerClose}
    }};
    for (std::size_t index = 0; index < layerTools.size(); ++index)
        layerToolbarButtons_[index] = createButton(layerTools[index].first, layerTools[index].second,
                                                    0, 0, index == 4 ? 34 : 82, 28);

    layerSearch_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 180, 25, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(CmdLayerSearch)),
        instance_, nullptr);
    SendMessageW(layerSearch_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    SendMessageW(layerSearch_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search layers"));

    layerTree_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
        0, 0, 120, 300, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(CmdLayerTree)),
        instance_, nullptr);
    SendMessageW(layerTree_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    TVINSERTSTRUCTW treeItem{};
    treeItem.hParent = TVI_ROOT;
    treeItem.hInsertAfter = TVI_LAST;
    treeItem.item.mask = TVIF_TEXT | TVIF_PARAM;
    treeItem.item.pszText = const_cast<wchar_t*>(L"All");
    treeItem.item.lParam = 0;
    const HTREEITEM allLayers = TreeView_InsertItem(layerTree_, &treeItem);
    treeItem.item.pszText = const_cast<wchar_t*>(L"Used");
    treeItem.item.lParam = 1;
    TreeView_InsertItem(layerTree_, &treeItem);
    TreeView_SelectItem(layerTree_, allLayers);

    layerList_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_HSCROLL | WS_VSCROLL |
            LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_EDITLABELS,
        0, 0, 500, 300, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(CmdLayerList)),
        instance_, nullptr);
    SendMessageW(layerList_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    ListView_SetExtendedListViewStyle(layerList_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES |
                                                  LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    ListView_SetBkColor(layerList_, RGB(57, 68, 84));
    ListView_SetTextBkColor(layerList_, RGB(57, 68, 84));
    ListView_SetTextColor(layerList_, RGB(235, 240, 247));
    const std::array<std::pair<const wchar_t*, int>, 11> columns{{
        {L"S", 34}, {L"Name", 145}, {L"On", 38}, {L"Freeze", 50}, {L"Lock", 40},
        {L"Plot", 40}, {L"Color", 88}, {L"Linetype", 90}, {L"Lineweight", 82},
        {L"Transparency", 86}, {L"Description", 170}
    }};
    for (std::size_t index = 0; index < columns.size(); ++index) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        column.pszText = const_cast<wchar_t*>(columns[index].first);
        column.cx = columns[index].second;
        column.fmt = index == 0 ? LVCFMT_CENTER : LVCFMT_LEFT;
        ListView_InsertColumn(layerList_, static_cast<int>(index), &column);
    }

    layerStatus_ = CreateWindowExW(0, L"STATIC", L"All: 1 layer displayed",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        0, 0, 400, 24, window_, nullptr, instance_, nullptr);
    SendMessageW(layerStatus_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    refreshLayerCombo();
    refreshLayerManager();
    updateLayerManagerVisibility();
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
    const int contentHeight = std::max(ribbonHeight + 1, height - statusHeight - commandBarHeight);
    int panelWidth = layerManagerOpen_ ? std::min(720, std::max(360, width - 560)) : 0;
    const int drawingWidth = std::max(240, width - panelWidth);
    panelWidth = width - drawingWidth;
    MoveWindow(canvas_, 0, ribbonHeight, std::max(1, drawingWidth),
               std::max(1, contentHeight - ribbonHeight), TRUE);
    if (viewCube_) {
        RECT canvasClient{};
        GetClientRect(canvas_, &canvasClient);
        const auto bounds = ViewCube::hostBounds(std::max(1L, canvasClient.right),
            std::max(1L, canvasClient.bottom));
        SetWindowPos(viewCube_, HWND_TOP, bounds.left, bounds.top,
                     bounds.right - bounds.left, bounds.bottom - bounds.top,
                     SWP_SHOWWINDOW | SWP_NOACTIVATE);
    }
    if (nodeDofPanel_) {
        SetWindowPos(nodeDofPanel_, HWND_TOP, 8, ribbonHeight + 4, 210, 48,
                     SWP_NOACTIVATE);
        if (nodeDofCombo_) SetWindowPos(nodeDofCombo_, HWND_TOP, 16, ribbonHeight + 22, 110, 180, SWP_NOACTIVATE);
        if (nodeDofApply_) SetWindowPos(nodeDofApply_, HWND_TOP, 134, ribbonHeight + 20, 76, 25, SWP_NOACTIVATE);
        if (nodeDofClose_) SetWindowPos(nodeDofClose_, HWND_TOP, 193, ribbonHeight + 6, 22, 22, SWP_NOACTIVATE);
    }
    const bool showProgress = dxfImportInProgress_ && dxfProgressBar_;
    const int progressWidth = std::min(300, std::max(180, width / 4));
    MoveWindow(status_, 0, contentHeight, showProgress ? std::max(1, width - progressWidth - 6) : width,
               statusHeight, TRUE);
    if (dxfProgressBar_) {
        MoveWindow(dxfProgressBar_, std::max(0, width - progressWidth), contentHeight + 3,
                   progressWidth - 4, statusHeight - 6, TRUE);
        ShowWindow(dxfProgressBar_, showProgress ? SW_SHOW : SW_HIDE);
    }
    const int commandBarY = contentHeight + statusHeight;
    const int promptWidth = std::min(120, std::max(80, drawingWidth / 6));
    if (commandBarPrompt_) {
        MoveWindow(commandBarPrompt_, 0, commandBarY, promptWidth, commandBarHeight, TRUE);
    }
    if (commandBar_) {
        MoveWindow(commandBar_, promptWidth, commandBarY, std::max(1, drawingWidth - promptWidth), commandBarHeight, TRUE);
    }
    if (snapPanel_) {
        const int panelX = std::max(8, drawingWidth - 375);
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

    if (layerPanel_) {
        const int panelX = drawingWidth;
        const int panelY = ribbonHeight;
        const int panelHeight = std::max(1, contentHeight - ribbonHeight);
        MoveWindow(layerPanel_, panelX, panelY, std::max(1, panelWidth), panelHeight, TRUE);
        MoveWindow(layerTitle_, panelX + 9, panelY + 5, std::max(120, panelWidth - 245), 27, TRUE);
        MoveWindow(layerSearch_, std::max(panelX + 180, panelX + panelWidth - 220), panelY + 5,
                   std::min(180, std::max(80, panelWidth - 230)), 25, TRUE);

        const int toolbarY = panelY + 36;
        const bool compactLayerToolbar = panelWidth < 500;
        const std::array<int, 4> toolWidths = compactLayerToolbar
            ? std::array<int, 4>{{84, 76, 92, 72}}
            : std::array<int, 4>{{104, 100, 112, 88}};
        int toolX = panelX + 8;
        for (std::size_t index = 0; index < toolWidths.size(); ++index) {
            MoveWindow(layerToolbarButtons_[index], toolX, toolbarY, toolWidths[index], 28, TRUE);
            toolX += toolWidths[index] + 4;
        }
        MoveWindow(layerToolbarButtons_[4], panelX + panelWidth - 38, panelY + 4, 30, 27, TRUE);
        const int contentTop = panelY + 70;
        const int contentBottom = contentHeight - 29;
        const int treeWidth = std::min(138, std::max(105, panelWidth / 5));
        MoveWindow(layerTree_, panelX + 8, contentTop, treeWidth,
                   std::max(1, contentBottom - contentTop), TRUE);
        MoveWindow(layerList_, panelX + treeWidth + 13, contentTop,
                   std::max(1, panelWidth - treeWidth - 21),
                   std::max(1, contentBottom - contentTop), TRUE);
        MoveWindow(layerStatus_, panelX + 8, contentBottom + 2,
                   std::max(1, panelWidth - 16), 24, TRUE);
        updateLayerManagerVisibility();
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
    constexpr int styleGap = 8;
    constexpr int styleRightPadding = 14;
    const std::array<int, 4> styleWidths{{150, 120, 130, 140}};
    const int styleTotalWidth = styleWidths[0] + styleWidths[1] + styleWidths[2] + styleWidths[3] + styleGap * 3;
    int styleX = std::max(8, width - styleRightPadding - styleTotalWidth);
    for (std::size_t index = 0; index < styleLabels_.size(); ++index) {
        MoveWindow(styleLabels_[index], styleX, 65, styleWidths[index], 17, TRUE);
        HWND combo = index == 0 ? layerCombo_ : index == 1 ? colorCombo_ : index == 2 ? lineTypeCombo_ : profileCombo_;
        MoveWindow(combo, styleX, 83, styleWidths[index], 220, TRUE);
        styleX += styleWidths[index] + styleGap;
    }
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
    constexpr bool showProperties = true;
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
    else if (id == CmdLayerNew) background = selected ? RGB(46, 147, 111) : RGB(35, 126, 94);
    else if (id == CmdLayerDelete) background = selected ? RGB(158, 74, 82) : RGB(126, 57, 66);
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
        if (separator == std::wstring::npos) {
            HGDIOBJ old = SelectObject(item.hDC, uiFont_);
            DrawTextW(item.hDC, text.c_str(), -1, &content,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(item.hDC, old);
            if (checked || selected) {
                HBRUSH borderBrush = CreateSolidBrush(checked ? RGB(99, 181, 225) : RGB(110, 127, 150));
                FrameRect(item.hDC, &item.rcItem, borderBrush);
                DeleteObject(borderBrush);
            }
            return;
        }
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

LRESULT CALLBACK Application::viewCubeProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    Application* app = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = static_cast<Application*>(create->lpCreateParams);
        app->viewCube_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    } else {
        app = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    }
    return app ? app->handleViewCubeMessage(message, wParam, lParam)
               : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK Application::layerCellEditSubclass(HWND window, UINT message, WPARAM wParam,
                                                     LPARAM lParam, UINT_PTR subclassId,
                                                     DWORD_PTR referenceData) {
    auto* application = reinterpret_cast<Application*>(referenceData);
    if (application && message == WM_KEYDOWN && wParam == VK_RETURN) {
        SetFocus(application->layerList_);
        return 0;
    }
    if (application && message == WM_KEYDOWN && wParam == VK_ESCAPE) {
        if (application->layerCellEditor_ == window) {
            application->layerCellEditor_ = nullptr;
            application->editingLayerName_.clear();
            application->editingLayerSubItem_ = 0;
        }
        RemoveWindowSubclass(window, layerCellEditSubclass, subclassId);
        DestroyWindow(window);
        SetFocus(application->layerList_);
        application->refreshLayerManager();
        return 0;
    }
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(window, layerCellEditSubclass, subclassId);
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT Application::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
#ifndef NDEBUG
    case WM_APP + 20: return static_cast<LRESULT>(document_.layers().size()); // Layer smoke probe.
    case WM_APP + 21: { // Current-layer visibility smoke probe.
        const auto found = document_.layers().find(currentLayerName());
        return found != document_.layers().end() && found->second.visible ? TRUE : FALSE;
    }
    case WM_APP + 22: return layerManagerOpen_ ? TRUE : FALSE; // Dock visibility smoke probe.
    case WM_APP + 23: return static_cast<LRESULT>(displayedLayers_.size()); // Filter smoke probe.
    case WM_APP + 24: { // Exercise the same current-layer visibility action as the list cell.
        editLayerProperty(currentLayerName(), 2, POINT{});
        const auto found = document_.layers().find(currentLayerName());
        return found != document_.layers().end() && found->second.visible ? TRUE : FALSE;
    }
    case WM_APP + 25: return static_cast<LRESULT>(document_.layerNames("wall").size());
    case WM_APP + 26: { // Ensure the smoke-created layer has its edited name.
        if (document_.layers().contains("Walls")) return TRUE;
        const bool renamed = document_.renameLayer("Layer 1", "Walls");
        if (renamed) {
            refreshLayerCombo();
            setCurrentLayer("Walls");
        }
        return renamed ? TRUE : FALSE;
    }
    case WM_APP + 27:
        SetWindowTextW(layerSearch_, L"wall");
        refreshLayerManager();
        return static_cast<LRESULT>(displayedLayers_.size());
    case WM_APP + 28:
        SetWindowTextW(layerSearch_, L"");
        refreshLayerManager();
        return static_cast<LRESULT>(displayedLayers_.size());
    case WM_APP + 29: { // Prepare two editable entities for neutral-selection property smoke tests.
        document_.clear();
        document_.createLayer("EDITED");
        document_.addLine({-1.0, 0.0, 0.0}, {1.0, 0.0, 0.0});
        document_.addLine({-1.0, 2.0, 0.0}, {1.0, 2.0, 0.0});
        selectedModels_.clear();
        selectionFirstCorner_.reset();
        deactivateAllCommands();
        refreshLayerCombo();
        invalidateCanvas();
        return static_cast<LRESULT>(document_.models().size());
    }
    case WM_APP + 30: return static_cast<LRESULT>(selectedModels_.size());
    case WM_APP + 31: {
        if (document_.models().size() != 2) return FALSE;
        return std::all_of(document_.models().begin(), document_.models().end(), [&](const auto& model) {
            const auto& properties = model.properties();
            if (wParam == 0) return properties.layer == "EDITED";
            if (wParam == 1) return properties.trueColor == 0xFF4040u;
            if (wParam == 2) return properties.lineType == "DASHED";
            return false;
        }) ? TRUE : FALSE;
    }
    case WM_APP + 32: return currentLayerName() == "0" ? TRUE : FALSE;
    case WM_APP + 33: {
        startTransformCommand(static_cast<TransformCommand>(wParam));
        return (transformCommand_ != TransformCommand::None ? 1 : 0) |
               (static_cast<LRESULT>(transformPhase_) << 1) |
               (filletFirstPick_ ? (1 << 7) : 0) |
               (static_cast<LRESULT>(selectedModels_.size()) << 8) |
               (static_cast<LRESULT>(modifierBoundaries_.size()) << 16) |
               (static_cast<LRESULT>(document_.models().size()) << 24);
    }
    case WM_APP + 34:
        return !drawingActive_ && transformCommand_ == TransformCommand::None &&
               !zoomWindowActive_ && !workPlanePicking_ && selectedModels_.empty() &&
               !selectionFirstCorner_ ? TRUE : FALSE;
    case WM_APP + 35: {
        const auto found = document_.layers().find("Walls");
        if (found == document_.layers().end()) return FALSE;
        auto layer = found->second;
        layer.trueColor = layer.effectiveColor = 0xFF4040u;
        layer.colorIndex = 256;
        document_.setLayerProperties(std::move(layer));
        refreshLayerManager();
        UpdateWindow(layerList_);
        return TRUE;
    }
    case WM_APP + 36: {
        document_.clear();
        document_.clearHistory();
        document_.addLine({0.0, 0.0, 0.0}, {5.0, 0.0, 0.0});
        pushUndoSnapshot();
        document_.addLine({0.0, 1.0, 0.0}, {5.0, 1.0, 0.0});
        pushUndoSnapshot();
        document_.addLine({0.0, 2.0, 0.0}, {5.0, 2.0, 0.0});
        return static_cast<LRESULT>(document_.models().size());
    }
    case WM_APP + 37: {
        undo();
        return static_cast<LRESULT>(document_.models().size());
    }
    case WM_APP + 38: {
        redo();
        return static_cast<LRESULT>(document_.models().size());
    }
    case WM_APP + 39: {
        saveOptions();
        std::ifstream saved(optionsPath_);
        if (!saved) return FALSE;
        std::string sig;
        saved >> sig;
        return sig == "MMOPT1" ? TRUE : FALSE;
    }
    case WM_APP + 40: {
        document_.clear();
        document_.clearHistory();
        document_.addLine({1.0, 0.0, 0.0}, {0.0, 1.0, 0.0});
        selectedModels_ = {0};
        startTransformCommand(TransformCommand::Rotate);
        if (transformPhase_ != TransformPhase::BasePoint) return 0;
        commitTransformPoint({0.0, 0.0, 0.0});
        if (transformPhase_ != TransformPhase::Destination) return 0;
        commitTransformPoint({0.0, 1.0, 0.0});
        return document_.models().size() == 1 &&
               std::abs(document_.models()[0].vertices()[0].x - 0.0) < 1e-6 &&
               std::abs(document_.models()[0].vertices()[0].y - 1.0) < 1e-6 &&
               std::abs(document_.models()[0].vertices()[1].x - (-1.0)) < 1e-6 &&
               std::abs(document_.models()[0].vertices()[1].y - 0.0) < 1e-6 ? TRUE : FALSE;
    }
    case WM_APP + 41: {
        // Minimal S2K format test - verify comma as decimal separator
        std::ostringstream testOut;
        testOut.imbue(std::locale::classic());
        testOut << std::fixed << std::setprecision(6) << 0.0;
        std::string num = testOut.str();
        for (auto& c : num) if (c == '.') c = ',';
        if (num != "0,000000") return FALSE;
        // Verify the table/header format
        std::ostringstream header;
        header << "TABLE:  \"JOINT COORDINATES\"\n";
        header << "Joint=1   CoordSys=GLOBAL   CoordType=Cartesian   XorR=0,000000   Y=0,000000   Z=0,000000   SpecialJt=Yes\n";
        header << "TABLE:  \"CONNECTIVITY - FRAME\"\n";
        header << "Frame=2   JointI=1   JointJ=2   IsCurved=No\n";
        std::string output = header.str();
        // Verify space-separated format (no commas between key=value pairs at top level)
        if (output.find(",GLOBAL") != std::string::npos) return FALSE;
        if (output.find(",Co") == std::string::npos) {
            // Check that we have proper space-separated key=value pairs
            if (output.find("CoordSys=GLOBAL") == std::string::npos) return FALSE;
            if (output.find("Joint=1") == std::string::npos) return FALSE;
            if (output.find("JointI=1") == std::string::npos) return FALSE;
        }
        return TRUE;
    }
#endif
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
    case WM_NOTIFY:
        if (const auto* notification = reinterpret_cast<const NMHDR*>(lParam))
            return handleLayerManagerNotification(*notification);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) >= CmdLayerNew && LOWORD(wParam) <= CmdLayerClose &&
            HIWORD(wParam) == BN_CLICKED) {
            handleLayerManagerCommand(LOWORD(wParam));
        } else if (LOWORD(wParam) == CmdLayerSearch && HIWORD(wParam) == EN_CHANGE) {
            refreshLayerManager();
        } else if (LOWORD(wParam) == CmdLayerCellEdit && HIWORD(wParam) == EN_KILLFOCUS) {
            commitLayerTextEdit();
        } else if (HIWORD(wParam) == BN_CLICKED) executeCommand(LOWORD(wParam));
        else if (HIWORD(wParam) == CBN_SELCHANGE &&
                 (LOWORD(wParam) == CmdLayerCombo || LOWORD(wParam) == CmdColorCombo ||
                  LOWORD(wParam) == CmdLineTypeCombo || LOWORD(wParam) == CmdProfileCombo)) {
            if (LOWORD(wParam) == CmdProfileCombo) {
                const LRESULT sel = SendMessageW(profileCombo_, CB_GETCURSEL, 0, 0);
                if (sel != CB_ERR) currentProfileChoice_ = static_cast<int>(sel);
                // Apply profile to selected models
                if (!selectedModels_.empty() && sel > 0 && static_cast<std::size_t>(sel) < profileDefinitions.size()) {
                    document_.setModelProfile(selectedModels_, profileDefinitions[static_cast<std::size_t>(sel)].name);
                } else if (!selectedModels_.empty() && sel == 0) {
                    document_.setModelProfile(selectedModels_, "");
                }
                updateStatus(); invalidateCanvas();
            } else handleStyleComboChange(LOWORD(wParam));
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
        saveOptions();
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
            const int mx = GET_X_LPARAM(lParam), my = GET_Y_LPARAM(lParam);
            RECT client{}; GetClientRect(canvas_, &client);
            if (hover_) camera_.setOrbitCenter(hover_->point);
            else if (const auto wp = camera_.unprojectToPlane(
                         {static_cast<double>(mx), static_cast<double>(my)},
                         std::max(1L, client.right), std::max(1L, client.bottom), 0.0))
                camera_.setOrbitCenter(*wp);
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
            selectedModels_.clear();
            selectionFirstCorner_.reset();
            nodeSelectionFirstCorner_.reset();
            syncStyleControls();
            deactivateAllCommands();
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
        else if (wParam == VK_F11) performanceOverlayEnabled_ = !performanceOverlayEnabled_;
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
        else if (wParam == 'R') startTransformCommand(TransformCommand::Rotate);
        else if (wParam == 'N' && !(GetKeyState(VK_CONTROL) & 0x8000)) {
            nodeConstraintVisible_ = !nodeConstraintVisible_;
            selectedNodeConstraints_.clear();
            nodeSelectionFirstCorner_.reset();
            if (document_.nodeConstraints().empty()) {
                for (const auto& model : document_.models()) {
                    if (model.vertices().size() != 2 || model.edges().size() != 1) continue;
                    for (const auto& v : model.vertices())
                        document_.setNodeConstraint(v, NodeConstraint{});
                }
            }
            invalidateCanvas();
        }
        else if (wParam == 'V') toggle3DView();
        else if (wParam == 'W') startWorkPlaneCommand();
        else if (wParam == 'B') addCube();
        else if (wParam == 'Y') addPyramid();
        else if (wParam == 'R' && mode_ == EditMode::View3D) camera_.reset();
        else if (wParam == VK_DELETE) startTransformCommand(TransformCommand::Delete);
        else if (wParam == 'Z' && (GetKeyState(VK_CONTROL) & 0x8000)) undo();
        else if (wParam == 'Y' && (GetKeyState(VK_CONTROL) & 0x8000)) redo();
        else if (wParam == 'S' && (GetKeyState(VK_CONTROL) & 0x8000)) saveDocument();
        else if (wParam == 'O' && (GetKeyState(VK_CONTROL) & 0x8000)) openDocument();
        else if (nodeConstraintVisible_ && !selectedNodeConstraints_.empty()) {
            if (wParam == 'F') setSelectedNodeConstraintsFixed();
            else if (wParam == 'P') setSelectedNodeConstraintsPinned();
            else if (wParam == VK_DELETE || wParam == VK_SPACE) setSelectedNodeConstraintsFree();
        }
        updateHover(cursorScreen_.x, cursorScreen_.y); updateControls(); invalidateCanvas();
        return 0;
    case WM_SIZE: invalidateCanvas(); return 0;
    default: return DefWindowProcW(canvas_, message, wParam, lParam);
    }
}

LRESULT Application::handleViewCubeMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        return DefWindowProcW(viewCube_, message, wParam, lParam);
    case WM_PAINT:
        onViewCubePaint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN: {
        SetFocus(canvas_);
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        RECT client{};
        GetClientRect(viewCube_, &client);
        viewCubeCursor_ = {x, y};
        viewCubeManipulating_ = true;
        viewCubeDragged_ = false;
        viewCubePressedView_ = ViewCube::hitTest(x, y, std::max(1L, client.right), camera_);
        lastMouse_ = {x, y};
        SetCapture(viewCube_);
        invalidateViewCube();
        return 0;
    }
    case WM_LBUTTONUP: {
        if (!viewCubeManipulating_) return 0;
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        RECT client{};
        GetClientRect(viewCube_, &client);
        if (!viewCubeDragged_ && viewCubePressedView_) {
            const auto releasedView = ViewCube::hitTest(x, y, std::max(1L, client.right), camera_);
            if (releasedView == viewCubePressedView_) setStandardView(*viewCubePressedView_);
        }
        viewCubeManipulating_ = false;
        viewCubePressedView_.reset();
        if (GetCapture() == viewCube_) ReleaseCapture();
        updateHover(cursorScreen_.x, cursorScreen_.y);
        updateControls();
        updateStatus();
        invalidateCanvas();
        invalidateViewCube();
        return 0;
    }
    case WM_MOUSEMOVE: {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        viewCubeCursor_ = {x, y};
        if (!viewCubeMouseTracking_) {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, viewCube_, 0};
            TrackMouseEvent(&tracking);
            viewCubeMouseTracking_ = true;
        }
        if (viewCubeManipulating_ && (wParam & MK_LBUTTON)) {
            const int dx = x - lastMouse_.x;
            const int dy = y - lastMouse_.y;
            if (dx != 0 || dy != 0) {
                activate3DNavigation();
                camera_.rotate(dx * 0.008, dy * 0.008);
                viewCubeDragged_ = true;
                lastMouse_ = {x, y};
                if (drawingActive_) updateHover(cursorScreen_.x, cursorScreen_.y);
                invalidateCanvas();
            }
        }
        invalidateViewCube();
        return 0;
    }
    case WM_MOUSELEAVE:
        viewCubeMouseTracking_ = false;
        viewCubeCursor_ = {-1, -1};
        invalidateViewCube();
        return 0;
    case WM_MOUSEWHEEL:
        SetFocus(canvas_);
        return SendMessageW(canvas_, message, wParam, lParam);
    case WM_CAPTURECHANGED:
        if (reinterpret_cast<HWND>(lParam) != viewCube_) {
            viewCubeManipulating_ = false;
            viewCubePressedView_.reset();
            invalidateViewCube();
        }
        return 0;
    case WM_SIZE:
        invalidateViewCube();
        return 0;
    default:
        return DefWindowProcW(viewCube_, message, wParam, lParam);
    }
}

void Application::onCanvasPaint() {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(canvas_, &paint);
    RECT client{};
    GetClientRect(canvas_, &client);
    const bool wasSnapPreview = snapPreviewActive_;
    renderer_.draw(dc, client, document_, camera_, mode_, draftView(), renderBackend_.get());
    snapPreviewActive_ = false;
    if (wasSnapPreview) SetTimer(window_, 4, 150, nullptr);
    EndPaint(canvas_, &paint);
}

void Application::onViewCubePaint() {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(viewCube_, &paint);
    RECT client{};
    GetClientRect(viewCube_, &client);
    viewCubeRenderer_.draw(dc, client, viewCubeCursor_, camera_, renderBackend_.get());
    EndPaint(viewCube_, &paint);
}

void Application::onLeftButtonDown(int x, int y) {
    if (zoomWindowActive_) {
        if (zoomWindowFirstCorner_) completeZoomWindow2D(x, y);
        else zoomWindowFirstCorner_ = POINT{x, y};
        updateControls(); invalidateCanvas(); return;
    }

    // Beam load mode: click beam to toggle/enter load (before other click handlers)
    if (beamLoadMode_ && transformCommand_ == TransformCommand::None && !workPlanePicking_) {
        RECT viewport{}; GetClientRect(canvas_, &viewport);
        const int vw = std::max(1L, viewport.right), vh = std::max(1L, viewport.bottom);
        std::size_t hitIndex = static_cast<std::size_t>(-1);
        double bestDist = 12.0;
        for (std::size_t i = 0; i < document_.models().size(); ++i) {
            const auto& m = document_.models()[i];
            if (m.vertices().size() != 2 || m.edges().size() != 1) continue;
            const Vec2 p1 = camera_.project(m.vertices()[0], vw, vh);
            const Vec2 p2 = camera_.project(m.vertices()[1], vw, vh);
            const double dx = p2.x - p1.x, dy = p2.y - p1.y;
            const double len2 = dx * dx + dy * dy;
            if (len2 < 1e-9) continue;
            const double t = std::clamp(((x - p1.x) * dx + (y - p1.y) * dy) / len2, 0.0, 1.0);
            const double dist = std::hypot(x - (p1.x + t * dx), y - (p1.y + t * dy));
            if (dist < bestDist) { bestDist = dist; hitIndex = i; }
        }
        if (hitIndex < document_.models().size()) {
            auto existing = document_.getBeamLoad(hitIndex);
            if (existing) {
                document_.setBeamLoad(hitIndex, BeamLoad{});  // Remove
            } else {
                beamLoadTargetIndex_ = hitIndex;
                input_.clear();
                updateStatus();
            }
        }
        invalidateCanvas(); return;
    }

    // Node selection when constraints are visible
    if (nodeConstraintVisible_ && transformCommand_ == TransformCommand::None && !workPlanePicking_) {
        RECT viewport{}; GetClientRect(canvas_, &viewport);
        const int vw = std::max(1L, viewport.right), vh = std::max(1L, viewport.bottom);
        if (nodeSelectionFirstCorner_) { completeNodeWindowSelection(x, y, vw, vh); return; }
        std::optional<std::string> hitKey;
        const double pickRadius = 12.0;
        for (const auto& [key, constraint] : document_.nodeConstraints()) {
            Vec3 pt{}; std::sscanf(key.c_str(), "%lf,%lf,%lf", &pt.x, &pt.y, &pt.z);
            const Vec2 proj = camera_.project(pt, vw, vh);
            const double dx = x - proj.x, dy = y - proj.y;
            if (dx * dx + dy * dy < pickRadius * pickRadius) { hitKey = key; break; }
        }
        if (hitKey) {
            if (selectedNodeConstraints_.count(*hitKey))
                selectedNodeConstraints_.erase(*hitKey);
            else
                selectedNodeConstraints_.insert(*hitKey);
        } else {
            nodeSelectionFirstCorner_ = POINT{x, y};
        }
        updateStatus(); invalidateCanvas(); return;
    }

    // Element force diagram on click when results loaded
    if (openseesResultsLoaded_ && resultView_ != ResultView::None &&
        transformCommand_ == TransformCommand::None && !workPlanePicking_ && !drawingActive_) {
        RECT viewport{}; GetClientRect(canvas_, &viewport);
        const int vw = std::max(1L, viewport.right), vh = std::max(1L, viewport.bottom);
        std::size_t hitIndex = static_cast<std::size_t>(-1);
        double bestDist = 12.0;
        for (std::size_t i = 0; i < document_.models().size(); ++i) {
            const auto& m = document_.models()[i];
            if (m.vertices().size() != 2 || m.edges().size() != 1) continue;
            const Vec2 p1 = camera_.project(m.vertices()[0], vw, vh);
            const Vec2 p2 = camera_.project(m.vertices()[1], vw, vh);
            const double dx = p2.x - p1.x, dy = p2.y - p1.y;
            const double len2 = dx * dx + dy * dy;
            if (len2 < 1e-9) continue;
            const double t = std::clamp(((x - p1.x) * dx + (y - p1.y) * dy) / len2, 0.0, 1.0);
            const double dist = std::hypot(x - (p1.x + t * dx), y - (p1.y + t * dy));
            if (dist < bestDist) { bestDist = dist; hitIndex = i; }
        }
        if (hitIndex < document_.models().size()) {
            const auto& m = document_.models()[hitIndex];
            ForceDiagramData fd;
            fd.label = L"Çubuk #" + std::to_wstring(hitIndex + 1);
            const double dx2 = m.vertices()[1].x - m.vertices()[0].x;
            const double dy2 = m.vertices()[1].y - m.vertices()[0].y;
            const double dz2 = m.vertices()[1].z - m.vertices()[0].z;
            fd.elementLength = std::sqrt(dx2*dx2 + dy2*dy2 + dz2*dz2);
            fd.sectionName = m.properties().profileName.empty()
                ? L"None" : std::wstring(m.properties().profileName.begin(),
                                          m.properties().profileName.end());
            const auto& ef = openseesElementForces_;
            const std::size_t base = hitIndex * 12;
            if (base + 11 < ef.size()) {
                fd.axialI = ef[base]; fd.shearYI = ef[base+1]; fd.shearZI = ef[base+2];
                fd.torsionI = ef[base+3]; fd.momentYI = ef[base+4]; fd.momentZI = ef[base+5];
                fd.axialJ = ef[base+6]; fd.shearYJ = ef[base+7]; fd.shearZJ = ef[base+8];
                fd.torsionJ = ef[base+9]; fd.momentYJ = ef[base+10]; fd.momentZJ = ef[base+11];
            }
            createForceDiagramWindow(instance_, window_, fd);
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
                    pushUndoSnapshot();
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
    } else {
        if (selectionFirstCorner_) completeWindowSelection(x, y);
        else if (!toggleModelSelection(x, y)) selectionFirstCorner_ = POINT{x, y};
    }
    updateStatus(); invalidateCanvas();
}

void Application::onLeftButtonUp(int x, int y) {
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
    if (rotating_ && (buttons & (MK_LBUTTON | MK_MBUTTON)) && mode_ == EditMode::View3D) {
        const int dx = x - lastMouse_.x;
        const int dy = y - lastMouse_.y;
        if (dx != 0 || dy != 0) {
            camera_.rotate(dx * 0.008, dy * 0.008);
            lastMouse_ = {x, y};
            if (drawingActive_) updateHover(x, y);
            invalidateViewCube();
            redraw = true;
        }
    } else if (panning2D_ && (buttons & MK_MBUTTON) && mode_ == EditMode::Draw2D) {
        camera_.pan2DByPixels(x - lastMouse_.x, y - lastMouse_.y);
        lastMouse_ = {x, y};
        updateHover(x, y);
        redraw = true;
    } else if (zoomWindowActive_ || selectionFirstCorner_ ||
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
    if (nodeSelectionFirstCorner_) redraw = true;
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
                    pushUndoSnapshot();
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
        } else if (transformCommand_ == TransformCommand::Rotate3D &&
                   transformPhase_ == TransformPhase::RotateAxis && !input_.empty()) {
            const std::wstring upper = [&] {
                std::wstring s = input_;
                for (auto& c : s) c = static_cast<wchar_t>(std::toupper(c));
                return s;
            }();
            if (upper == L"X") rotateAxis_ = transformBase_ ? *transformBase_ + Vec3{1.0, 0.0, 0.0} : Vec3{1.0, 0.0, 0.0};
            else if (upper == L"Y") rotateAxis_ = transformBase_ ? *transformBase_ + Vec3{0.0, 1.0, 0.0} : Vec3{0.0, 1.0, 0.0};
            else if (upper == L"Z") rotateAxis_ = transformBase_ ? *transformBase_ + Vec3{0.0, 0.0, 1.0} : Vec3{0.0, 0.0, 1.0};
            else { MessageBeep(MB_ICONWARNING); return; }
            input_.clear();
            transformPhase_ = TransformPhase::Destination;
            SetCursor(currentCanvasCursor());
            updateControls(); updateStatus(); invalidateCanvas();
        } else if (beamLoadTargetIndex_ && !input_.empty()) {
            try {
                std::size_t used{};
                const double wY = std::stod(input_, &used);
                std::wstring rest = input_.substr(used);
                double wZ = 0.0;
                if (!rest.empty()) { std::size_t u2; wZ = std::stod(rest, &u2); }
                BeamLoad bl{wY, wZ};
                if (bl.wY == 0.0 && bl.wZ == 0.0)
                    document_.setBeamLoad(*beamLoadTargetIndex_, BeamLoad{});  // Remove
                else
                    document_.setBeamLoad(*beamLoadTargetIndex_, bl);
                beamLoadTargetIndex_.reset();
                input_.clear();
                updateStatus(); invalidateCanvas();
            } catch (...) { MessageBeep(MB_ICONWARNING); input_.clear(); }
        } else if (transformCommand_ == TransformCommand::Rotate3D &&
                   transformPhase_ == TransformPhase::Destination && !input_.empty()) {
            try {
                std::size_t used{};
                const double angle = std::stod(input_, &used);
                if (used != input_.size() || !std::isfinite(angle)) throw std::invalid_argument("angle");
                if (transformBase_ && rotateAxis_) {
                    const Vec3 axisDir = *rotateAxis_ - *transformBase_;
                    if (axisDir.x * axisDir.x + axisDir.y * axisDir.y + axisDir.z * axisDir.z > 1e-12) {
                        pushUndoSnapshot();
                        for (const auto index : selectedModels_) {
                            if (index >= document_.models().size()) continue;
                            const auto rotated = rotateModelAroundAxis(
                                document_.models()[index], *transformBase_, axisDir, angle);
                            if (!rotated) { MessageBeep(MB_ICONWARNING); return; }
                            document_.replaceModel(index, {std::move(*rotated)});
                        }
                        cancelTransformCommand();
                    }
                }
                input_.clear();
                updateControls(); updateStatus(); invalidateCanvas();
            } catch (...) { MessageBeep(MB_ICONWARNING); }
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
    else if (transformCommand_ == TransformCommand::Rotate3D &&
             transformPhase_ == TransformPhase::RotateAxis &&
             (character == L'x' || character == L'X' || character == L'y' || character == L'Y' ||
              character == L'z' || character == L'Z' || character == L'l' || character == L'L'))
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
    selectedModels_.erase(
        std::remove_if(selectedModels_.begin(), selectedModels_.end(), [&](std::size_t index) {
            return !document_.modelIsEditable(index);
        }),
        selectedModels_.end());
    const auto preselectionAction = modifierPreselectionAction(command, selectedModels_.size());
    lastTransformCommand_ = command;
    transformCommand_ = command;
    transformPhase_ = TransformPhase::Selecting;
    selectionFirstCorner_.reset();
    transformBase_.reset();
    offsetDistance_.reset();
    filletFirstPick_.reset();
    arrayItemCount_.reset();
    modifierBoundaries_.clear();
    drawingActive_ = false;
    hover_.reset();

    if (preselectionAction == ModifierPreselectionAction::DeleteEntities) {
        pushUndoSnapshot();
        document_.deleteModels(selectedModels_);
        cancelTransformCommand();
        return;
    }
    if (preselectionAction == ModifierPreselectionAction::PickTargets) {
        modifierBoundaries_.reserve(selectedModels_.size());
        for (const auto index : selectedModels_)
            if (index < document_.models().size())
                modifierBoundaries_.push_back(document_.models()[index]);
        selectedModels_.clear();
        transformPhase_ = TransformPhase::Destination;
    } else if (preselectionAction == ModifierPreselectionAction::BasePoint) {
        transformPhase_ = TransformPhase::BasePoint;
    } else if (preselectionAction == ModifierPreselectionAction::PickSecondFilletEntity) {
        const std::size_t first = selectedModels_.front();
        selectedModels_.assign(1, first);
        if (!document_.models()[first].vertices().empty())
            filletFirstPick_ = document_.models()[first].vertices().front();
    }
    syncStyleControls();
    SetCursor(currentCanvasCursor());
    updateCommandBar();
}

void Application::toggle3DView() {
    cancelZoomWindow2D();
    if (workPlanePicking_) cancelWorkPlaneCommand();
    if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
    cancelDrawing();
    mode_ = mode_ == EditMode::View3D ? EditMode::Draw2D : EditMode::View3D;
    drawingActive_ = mode_ == EditMode::Draw2D;
    invalidateViewCube();
    invalidateCanvas();
}

void Application::activate3DNavigation() {
    if (mode_ == EditMode::View3D) return;
    cancelZoomWindow2D();
    if (workPlanePicking_) cancelWorkPlaneCommand();
    if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
    cancelDrawing();
    mode_ = EditMode::View3D;
    drawingActive_ = false;
}

void Application::setStandardView(StandardView view) {
    cancelZoomWindow2D();
    if (workPlanePicking_) cancelWorkPlaneCommand();
    if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
    cancelDrawing();
    camera_.setView(view);
    mode_ = EditMode::View3D;
    drawingActive_ = false;
    invalidateViewCube();
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
    syncStyleControls();
    transformBase_.reset();
    offsetDistance_.reset();
    filletFirstPick_.reset();
    arrayItemCount_.reset();
    modifierBoundaries_.clear();
    input_.clear();
    drawingActive_ = false;
    clearTemporaryTracking();
    SetCursor(currentCanvasCursor());
    updateCommandBar();
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
        syncStyleControls();
        return true;
    }
    const auto existing = std::find(selectedModels_.begin(), selectedModels_.end(), *hit);
    if (existing == selectedModels_.end()) selectedModels_.push_back(*hit);
    else selectedModels_.erase(existing);
    syncStyleControls();
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
    syncStyleControls();
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
        pushUndoSnapshot();
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
            pushUndoSnapshot();
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
        pushUndoSnapshot();
        for (auto& model : copies) document_.addModel(std::move(model));
        cancelTransformCommand();
        return;
    }
    if (transformPhase_ == TransformPhase::RotateAxis) {
        rotateAxis_ = point;
        transformPhase_ = TransformPhase::Destination;
        SetCursor(currentCanvasCursor());
        return;
    }
    if (transformPhase_ == TransformPhase::BasePoint) {
        if (transformCommand_ == TransformCommand::LinearArray && !arrayItemCount_) {
            MessageBeep(MB_ICONWARNING);
            return;
        }
        if (transformCommand_ == TransformCommand::Rotate3D) {
            transformBase_ = point;
            transformPhase_ = TransformPhase::RotateAxis;
            SetCursor(currentCanvasCursor());
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
        pushUndoSnapshot();
        for (auto& model : mirrored) document_.addModel(std::move(model));
        cancelTransformCommand();
        return;
    }
    if (transformCommand_ == TransformCommand::Rotate) {
        const double angleDeg = std::atan2(point.y - transformBase_->y,
                                           point.x - transformBase_->x) * 180.0 / std::numbers::pi;
        pushUndoSnapshot();
        for (const auto index : selectedModels_) {
            if (index >= document_.models().size()) continue;
            const auto rotated = mode_ == EditMode::View3D
                ? rotateModelOnPlane(document_.models()[index], *transformBase_, angleDeg, workPlane_)
                : rotateModel2D(document_.models()[index], *transformBase_, angleDeg);
            if (!rotated) { MessageBeep(MB_ICONWARNING); return; }
            document_.replaceModel(index, {std::move(*rotated)});
        }
        cancelTransformCommand();
        return;
    }
    if (transformCommand_ == TransformCommand::Rotate3D && rotateAxis_) {
        const Vec3 axisDir = *rotateAxis_ - *transformBase_;
        constexpr double axisEpsilon = 1e-12;
        if (axisDir.x * axisDir.x + axisDir.y * axisDir.y + axisDir.z * axisDir.z <= axisEpsilon) {
            MessageBeep(MB_ICONWARNING); return;
        }
        const double angleDeg = std::atan2(point.y - transformBase_->y,
                                           point.x - transformBase_->x) * 180.0 / std::numbers::pi;
        pushUndoSnapshot();
        for (const auto index : selectedModels_) {
            if (index >= document_.models().size()) continue;
            const auto rotated = rotateModelAroundAxis(document_.models()[index],
                                                       *transformBase_, axisDir, angleDeg);
            if (!rotated) { MessageBeep(MB_ICONWARNING); return; }
            document_.replaceModel(index, {std::move(*rotated)});
        }
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
        pushUndoSnapshot();
        for (auto& model : copies) document_.addModel(std::move(model));
        cancelTransformCommand();
        return;
    }
    if (transformCommand_ == TransformCommand::Move) {
        pushUndoSnapshot();
        document_.moveModels(selectedModels_, displacement);
        cancelTransformCommand();
    } else if (transformCommand_ == TransformCommand::Copy) {
        pushUndoSnapshot();
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
            hover_ = applyOrtho3D(*orthoAnchor, {static_cast<double>(x), static_cast<double>(y)}, *hover_,
                                  camera_, std::max(1L, client.right), std::max(1L, client.bottom),
                                  workPlane_, true, true);
        } else {
            hover_ = applyOrtho(*orthoAnchor, *hover_, true);
        }
        // Parallel and Extension snaps interfere with strict Ortho — disable them
        if (hover_ && (hover_->type == SnapType::Parallel || hover_->type == SnapType::Extension))
            hover_->type = SnapType::None;
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
        pushUndoSnapshot();
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
    case CmdSapExport: exportS2K(); break;
    case CmdOpenSeesExport: exportOpenSees(); break;
    case CmdOpenSeesRun: runOpenSees(); break;
    case CmdOpenSeesClear: clearOpenSeesResults(); break;
    case CmdAnalyze: analyzeOpenSees(); break;
    case CmdResultNone: resultView_ = ResultView::None; break;
    case CmdResultDeformed: resultView_ = ResultView::Deformed; break;
    case CmdResultMomentY: resultView_ = ResultView::MomentY; break;
    case CmdResultMomentZ: resultView_ = ResultView::MomentZ; break;
    case CmdResultAxial: resultView_ = ResultView::Axial; break;
    case CmdResultShearY: resultView_ = ResultView::ShearY; break;
    case CmdResultShearZ: resultView_ = ResultView::ShearZ; break;
    case CmdDepthZPlus: depthClipZMax_ -= 1.0; break;
    case CmdDepthZMinus: depthClipZMin_ += 1.0; break;
    case CmdDepthToggle: depthClipEnabled_ = !depthClipEnabled_; break;
    case CmdLine: selectTool(DrawTool::Line); break;
    case CmdPolyline: selectTool(DrawTool::Polyline); break;
    case CmdRectangle: selectTool(DrawTool::Rectangle); break;
    case CmdCircle: selectTool(DrawTool::Circle); break;
    case CmdFace3D: selectTool(DrawTool::Face3D); break;
    case CmdLayerManager: {
        layerManagerOpen_ = !layerManagerOpen_;
        RECT client{};
        GetClientRect(window_, &client);
        layoutChildren(client.right, client.bottom);
        updateLayerManagerVisibility();
        break;
    }
    case CmdCube: addCube(); break;
    case CmdPyramid: addPyramid(); break;
    case CmdResetView: camera_.reset(); break;
    case CmdView3D: toggle3DView(); break;
    case CmdWorkPlane: startWorkPlaneCommand(); break;
    case CmdUcsCommand: {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, CmdUcs3Point, L"3 Nokta");
        AppendMenuW(menu, MF_STRING, CmdUcsZAxis, L"Z Ekseni");
        AppendMenuW(menu, MF_STRING, CmdUcsView, L"Görünüş");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, CmdUcsWorld, L"Dünya");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, CmdUcsX, L"X Ekseni Etrafında Döndür");
        AppendMenuW(menu, MF_STRING, CmdUcsY, L"Y Ekseni Etrafında Döndür");
        AppendMenuW(menu, MF_STRING, CmdUcsZ, L"Z Ekseni Etrafında Döndür");
        RECT buttonRect{};
        GetWindowRect(ucsButton_, &buttonRect);
        const UINT selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                             buttonRect.left, buttonRect.bottom, 0, window_, nullptr);
        DestroyMenu(menu);
        if (selected) SendMessageW(window_, WM_COMMAND, selected, 0);
        break;
    }
    case CmdUcs3Point: startWorkPlaneCommand(); break;
    case CmdUcsZAxis: workPlane_ = WorkPlane{};
        mode_ = EditMode::View3D; drawingActive_ = false;
        invalidateCanvas(); break;
    case CmdUcsView: {
        if (mode_ == EditMode::View3D) {
            const double cy = std::cos(camera_.yaw()), sy = std::sin(camera_.yaw());
            const double cp = std::cos(camera_.pitch()), sp = std::sin(camera_.pitch());
            const Vec3 viewDir{sy * cp, sp, -cy * cp};
            const Vec3 upDir{0.0, 1.0, 0.0};
            workPlane_ = WorkPlane::fromCurrentView(workPlane_.origin, viewDir, upDir);
        }
        invalidateCanvas(); break;
    }
    case CmdUcsWorld: workPlane_ = WorkPlane{};
        invalidateCanvas(); break;
    case CmdUcsX: workPlane_ = workPlane_.rotatedX(90.0);
        invalidateCanvas(); break;
    case CmdUcsY: workPlane_ = workPlane_.rotatedY(90.0);
        invalidateCanvas(); break;
    case CmdUcsZ: workPlane_ = workPlane_.rotatedZ(90.0);
        invalidateCanvas(); break;
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
    case CmdUndo: undo(); break;
    case CmdRedo: redo(); break;
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
    case CmdRotate: startTransformCommand(TransformCommand::Rotate); break;
    case CmdRotate3D: startTransformCommand(TransformCommand::Rotate3D); break;
    case CmdNodeConstraint: cycleNodeConstraint(); break;
    case CmdClearNodeConstraints: clearNodeConstraintsAction(); break;
    case CmdNodeDofApply: applyNodeDofFromCombo(); break;
    case CmdNodeDofClose: nodeConstraintVisible_ = false; selectedNodeConstraints_.clear();
                          nodeSelectionFirstCorner_.reset(); invalidateCanvas(); break;
    case CmdBeamLoad: beamLoadMode_ = !beamLoadMode_; beamLoadTargetIndex_.reset(); input_.clear();
                      updateStatus(); updateControls(); invalidateCanvas(); break;
    case CmdClearBeamLoads: document_.clearBeamLoads(); invalidateCanvas(); break;
    default: break;
    }
    updateHover(cursorScreen_.x, cursorScreen_.y);
    updateControls(); invalidateCanvas(); invalidateViewCube(); SetFocus(canvas_);
}

void Application::selectTool(DrawTool tool) {
    cancelZoomWindow2D();
    if (workPlanePicking_) cancelWorkPlaneCommand();
    if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
    selectedModels_.clear();
    selectionFirstCorner_.reset();
    syncStyleControls();
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
    check(rotateButton_, transformCommand_ == TransformCommand::Rotate);
    check(rotate3DButton_, transformCommand_ == TransformCommand::Rotate3D);
    check(view3DButton_, mode_ == EditMode::View3D);
    if (visualStyleButton_) {
        const wchar_t* label = L"◇\r\nWireframe ▼";
        if (visualStyle_ == VisualStyle::Solid) label = L"◆\r\nSolid ▼";
        else if (visualStyle_ == VisualStyle::Transparent) label = L"◈\r\nSaydam ▼";
        SetWindowTextW(visualStyleButton_, label);
    }
    check(workPlaneButton_, workPlanePicking_);
    check(zoomWindowButton_, zoomWindowActive_);
    check(layerManagerButton_, layerManagerOpen_);
    if (nodeDofPanel_) {
        ShowWindow(nodeDofPanel_, nodeConstraintVisible_ ? SW_SHOW : SW_HIDE);
        if (nodeDofCombo_) ShowWindow(nodeDofCombo_, nodeConstraintVisible_ ? SW_SHOW : SW_HIDE);
        if (nodeDofApply_) ShowWindow(nodeDofApply_, nodeConstraintVisible_ ? SW_SHOW : SW_HIDE);
        if (nodeDofClose_) ShowWindow(nodeDofClose_, nodeConstraintVisible_ ? SW_SHOW : SW_HIDE);
    }
    updateCommandBar();
    updateStatus();
}

void Application::refreshLayerCombo() {
    if (!layerCombo_) return;
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
    if (!document_.layers().contains(currentLayer_)) currentLayer_ = "0";
    syncStyleControls();
    if (layerList_) refreshLayerManager();
}

std::string Application::currentLayerName() const {
    return currentLayer_;
}

void Application::syncStyleControls() {
    if (!layerCombo_ || !colorCombo_ || !lineTypeCombo_) return;
    const auto selectLayer = [&](const std::string& name) {
        const std::wstring wideName = utf8ToWide(name);
        const LRESULT index = SendMessageW(layerCombo_, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                                           reinterpret_cast<LPARAM>(wideName.c_str()));
        SendMessageW(layerCombo_, CB_SETCURSEL, index == CB_ERR ? -1 : index, 0);
    };
    if (selectedModels_.empty()) {
        selectLayer(currentLayer_);
        SendMessageW(colorCombo_, CB_SETCURSEL, currentColorChoice_, 0);
        SendMessageW(lineTypeCombo_, CB_SETCURSEL, currentLineTypeChoice_, 0);
        return;
    }

    const auto first = std::find_if(selectedModels_.begin(), selectedModels_.end(), [&](std::size_t index) {
        return index < document_.models().size();
    });
    if (first == selectedModels_.end()) {
        selectedModels_.clear();
        syncStyleControls();
        return;
    }
    const EntityProperties& reference = document_.models()[*first].properties();
    bool sameLayer = true;
    bool sameColor = true;
    bool sameLineType = true;
    for (const auto index : selectedModels_) {
        if (index >= document_.models().size()) continue;
        const auto& properties = document_.models()[index].properties();
        sameLayer = sameLayer && properties.layer == reference.layer;
        sameColor = sameColor && properties.trueColor == reference.trueColor;
        sameLineType = sameLineType && properties.lineType == reference.lineType;
    }
    if (sameLayer) selectLayer(reference.layer);
    else SendMessageW(layerCombo_, CB_SETCURSEL, -1, 0);

    LRESULT colorSelection = 0;
    if (reference.trueColor) {
        colorSelection = CB_ERR;
        for (std::size_t index = 1; index < colorChoices.size(); ++index)
            if (colorChoices[index].color == reference.trueColor) {
                colorSelection = static_cast<LRESULT>(index);
                break;
            }
    }
    SendMessageW(colorCombo_, CB_SETCURSEL, sameColor && colorSelection != CB_ERR ? colorSelection : -1, 0);

    const std::string lineType = reference.lineType.empty() ? "BYLAYER" : reference.lineType;
    LRESULT lineTypeSelection = CB_ERR;
    for (std::size_t index = 0; index < lineTypeChoices.size(); ++index)
        if (lineType == lineTypeChoices[index].value) {
            lineTypeSelection = static_cast<LRESULT>(index);
            break;
        }
    SendMessageW(lineTypeCombo_, CB_SETCURSEL,
                 sameLineType && lineTypeSelection != CB_ERR ? lineTypeSelection : -1, 0);
}

void Application::handleStyleComboChange(int id) {
    const LRESULT selection = SendMessageW(GetDlgItem(window_, id), CB_GETCURSEL, 0, 0);
    if (selection == CB_ERR) return;
    if (selectedModels_.empty()) {
        if (id == CmdLayerCombo) {
            wchar_t value[256]{};
            GetWindowTextW(layerCombo_, value, static_cast<int>(std::size(value)));
            setCurrentLayer(wideToUtf8(value));
            return;
        }
        if (id == CmdColorCombo) currentColorChoice_ = static_cast<int>(selection);
        else if (id == CmdLineTypeCombo) currentLineTypeChoice_ = static_cast<int>(selection);
    } else if (id == CmdLayerCombo) {
        wchar_t value[256]{};
        GetWindowTextW(layerCombo_, value, static_cast<int>(std::size(value)));
        pushUndoSnapshot();
        document_.setModelLayer(selectedModels_, wideToUtf8(value));
        refreshLayerManager();
    } else if (id == CmdColorCombo && static_cast<std::size_t>(selection) < colorChoices.size()) {
        pushUndoSnapshot();
        document_.setModelColor(selectedModels_, colorChoices[static_cast<std::size_t>(selection)].color);
    } else if (id == CmdLineTypeCombo && static_cast<std::size_t>(selection) < lineTypeChoices.size()) {
        pushUndoSnapshot();
        document_.setModelLineType(selectedModels_, lineTypeChoices[static_cast<std::size_t>(selection)].value);
    }
    syncStyleControls();
    updateStatus();
    invalidateCanvas();
}

std::optional<std::string> Application::selectedLayerName() const {
    if (!layerList_) return std::nullopt;
    const int row = ListView_GetNextItem(layerList_, -1, LVNI_SELECTED);
    if (row < 0 || static_cast<std::size_t>(row) >= displayedLayers_.size()) return std::nullopt;
    return displayedLayers_[static_cast<std::size_t>(row)];
}

void Application::refreshLayerManager() {
    if (!layerList_) return;
    const auto selected = selectedLayerName();
    wchar_t searchText[256]{};
    if (layerSearch_) GetWindowTextW(layerSearch_, searchText, static_cast<int>(std::size(searchText)));
    displayedLayers_ = document_.layerNames(wideToUtf8(searchText));
    if (showUsedLayersOnly_) {
        std::erase_if(displayedLayers_, [&](const std::string& name) {
            if (name == currentLayerName()) return false;
            return std::none_of(document_.models().begin(), document_.models().end(),
                [&](const WireframeModel& model) { return model.properties().layer == name; });
        });
    }

    ListView_DeleteAllItems(layerList_);
    const std::string current = currentLayerName();
    const auto setCell = [&](int row, int column, const std::wstring& value) {
        ListView_SetItemText(layerList_, row, column, const_cast<wchar_t*>(value.c_str()));
    };
    for (std::size_t index = 0; index < displayedLayers_.size(); ++index) {
        const std::string& name = displayedLayers_[index];
        const auto found = document_.layers().find(name);
        if (found == document_.layers().end()) continue;
        const EntityProperties& layer = found->second;
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(index);
        std::wstring currentMark = name == current ? L"✓" : L"";
        item.pszText = currentMark.data();
        const int row = ListView_InsertItem(layerList_, &item);
        setCell(row, 1, utf8ToWide(name));
        setCell(row, 2, layer.visible ? L"●" : L"○");
        setCell(row, 3, layer.frozen ? L"❄" : L"☀");
        setCell(row, 4, layer.locked ? L"🔒" : L"🔓");
        setCell(row, 5, layer.plottable ? L"✓" : L"—");
        wchar_t color[32]{};
        std::swprintf(color, std::size(color), L"■ #%06X", layer.effectiveColor & 0xFFFFFFu);
        setCell(row, 6, color);
        setCell(row, 7, utf8ToWide(layer.effectiveLineType.empty() ? "CONTINUOUS" : layer.effectiveLineType));
        if (layer.effectiveLineWeight <= 0) setCell(row, 8, L"Default");
        else {
            wchar_t weight[32]{};
            std::swprintf(weight, std::size(weight), L"%.2f mm", layer.effectiveLineWeight / 100.0);
            setCell(row, 8, weight);
        }
        int transparency = layer.transparency;
        if (transparency > 100) transparency = (transparency & 0xFF) * 100 / 255;
        setCell(row, 9, std::to_wstring(std::clamp(transparency, 0, 100)) + L"%");
        setCell(row, 10, utf8ToWide(layer.description));
        if (selected && *selected == name)
            ListView_SetItemState(layerList_, row, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
    }
    if (layerTitle_) {
        const std::wstring title = L"Current layer: " + utf8ToWide(current);
        SetWindowTextW(layerTitle_, title.c_str());
    }
    if (layerStatus_) {
        const std::wstring status = (showUsedLayersOnly_ ? L"Used: " : L"All: ") +
            std::to_wstring(displayedLayers_.size()) + L" layer(s) displayed of " +
            std::to_wstring(document_.layers().size()) + L" total";
        SetWindowTextW(layerStatus_, status.c_str());
    }
}

void Application::updateLayerManagerVisibility() {
    const int command = layerManagerOpen_ ? SW_SHOW : SW_HIDE;
    for (HWND control : {layerPanel_, layerTitle_, layerSearch_, layerTree_, layerList_, layerStatus_})
        if (control) ShowWindow(control, command);
    for (HWND button : layerToolbarButtons_) if (button) ShowWindow(button, command);
    if (layerManagerOpen_) {
        SetWindowPos(layerPanel_, HWND_BOTTOM, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        for (HWND control : {layerTitle_, layerSearch_, layerTree_, layerList_, layerStatus_})
            if (control) SetWindowPos(control, HWND_TOP, 0, 0, 0, 0,
                                      SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        for (HWND button : layerToolbarButtons_)
            if (button) SetWindowPos(button, HWND_TOP, 0, 0, 0, 0,
                                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    if (!layerManagerOpen_ && layerCellEditor_) {
        HWND editor = layerCellEditor_;
        layerCellEditor_ = nullptr;
        DestroyWindow(editor);
        editingLayerName_.clear();
    }
    if (layerManagerButton_)
        SendMessageW(layerManagerButton_, BM_SETCHECK, layerManagerOpen_ ? BST_CHECKED : BST_UNCHECKED, 0);
}

void Application::setCurrentLayer(const std::string& name) {
    if (!layerCombo_ || !document_.layers().contains(name)) return;
    auto layer = document_.layers().at(name);
    layer.visible = true;
    layer.frozen = false;
    pushUndoSnapshot();
    document_.setLayerProperties(layer);
    currentLayer_ = name;
    syncStyleControls();
    refreshLayerManager();
    invalidateCanvas();
    updateStatus();
}

void Application::beginLayerTextEdit(const std::string& name, int row, int subItem) {
    if (!layerList_ || row < 0 || (subItem != 1 && subItem != 10) ||
        (subItem == 1 && name == "0")) return;
    if (layerCellEditor_) commitLayerTextEdit();
    RECT cell{};
    if (!ListView_GetSubItemRect(layerList_, row, subItem, LVIR_BOUNDS, &cell)) return;
    MapWindowPoints(layerList_, window_, reinterpret_cast<POINT*>(&cell), 2);
    const auto found = document_.layers().find(name);
    if (found == document_.layers().end()) return;
    const std::wstring text = subItem == 1 ? utf8ToWide(name) : utf8ToWide(found->second.description);
    editingLayerName_ = name;
    editingLayerSubItem_ = subItem;
    layerCellEditor_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        cell.left, cell.top, std::max(80L, cell.right - cell.left), std::max(23L, cell.bottom - cell.top),
        window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(CmdLayerCellEdit)), instance_, nullptr);
    SendMessageW(layerCellEditor_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    SetWindowSubclass(layerCellEditor_, layerCellEditSubclass, 1,
                      reinterpret_cast<DWORD_PTR>(this));
    SendMessageW(layerCellEditor_, EM_SETSEL, 0, -1);
    SetFocus(layerCellEditor_);
}

void Application::commitLayerTextEdit() {
    if (!layerCellEditor_) return;
    wchar_t value[512]{};
    GetWindowTextW(layerCellEditor_, value, static_cast<int>(std::size(value)));
    HWND editor = layerCellEditor_;
    layerCellEditor_ = nullptr;
    DestroyWindow(editor);
    const std::string oldName = editingLayerName_;
    const int subItem = editingLayerSubItem_;
    editingLayerName_.clear();
    const std::string text = wideToUtf8(value);
    if (subItem == 1) {
        if (text == oldName) return;
        const bool wasCurrent = oldName == currentLayerName();
        if (!document_.renameLayer(oldName, text)) {
            MessageBoxW(window_, L"Katman adı boş, mevcut veya korumalı olamaz.",
                        L"Layer Manager", MB_OK | MB_ICONWARNING);
        } else {
            pushUndoSnapshot();
            refreshLayerCombo();
            if (wasCurrent) setCurrentLayer(text);
        }
    } else if (subItem == 10) {
        const auto found = document_.layers().find(oldName);
        if (found != document_.layers().end()) {
            auto layer = found->second;
            layer.description = text;
            pushUndoSnapshot();
            document_.setLayerProperties(std::move(layer));
        }
    }
    refreshLayerManager();
}

void Application::pushUndoSnapshot() {
    document_.pushSnapshot();
}

void Application::undo() {
    if (!document_.canUndo()) return;
    document_.undo();
    refreshLayerCombo();
    refreshLayerManager();
    syncStyleControls();
    selectedModels_.clear();
    selectionFirstCorner_.reset();
    deactivateAllCommands();
    invalidateCanvas();
    updateStatus();
}

void Application::redo() {
    if (!document_.canRedo()) return;
    document_.redo();
    refreshLayerCombo();
    refreshLayerManager();
    syncStyleControls();
    selectedModels_.clear();
    selectionFirstCorner_.reset();
    deactivateAllCommands();
    invalidateCanvas();
    updateStatus();
}

void Application::editLayerProperty(const std::string& name, int subItem, POINT screenPoint) {
    const auto found = document_.layers().find(name);
    if (found == document_.layers().end()) return;
    auto layer = found->second;
    if (subItem == 2) layer.visible = !layer.visible;
    else if (subItem == 3) {
        if (name == currentLayerName() && !layer.frozen) {
            MessageBoxW(window_, L"Geçerli katman dondurulamaz.", L"Layer Manager",
                        MB_OK | MB_ICONINFORMATION);
            return;
        }
        layer.frozen = !layer.frozen;
    } else if (subItem == 4) layer.locked = !layer.locked;
    else if (subItem == 5) layer.plottable = !layer.plottable;
    else if (subItem == 6) {
        HMENU menu = CreatePopupMenu();
        for (std::size_t index = 1; index < colorChoices.size(); ++index)
            AppendMenuW(menu, MF_STRING, 9100 + index, colorChoices[index].label);
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, 9199, L"Özel renk...");
        const UINT selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                             screenPoint.x, screenPoint.y, 0, window_, nullptr);
        DestroyMenu(menu);
        if (!selected) return;
        if (selected == 9199) {
            static COLORREF customColors[16]{};
            CHOOSECOLORW chooser{};
            chooser.lStructSize = sizeof(chooser);
            chooser.hwndOwner = window_;
            chooser.rgbResult = RGB((layer.effectiveColor >> 16) & 0xFF,
                                    (layer.effectiveColor >> 8) & 0xFF, layer.effectiveColor & 0xFF);
            chooser.lpCustColors = customColors;
            chooser.Flags = CC_FULLOPEN | CC_RGBINIT;
            if (!ChooseColorW(&chooser)) return;
            layer.effectiveColor = (GetRValue(chooser.rgbResult) << 16) |
                                   (GetGValue(chooser.rgbResult) << 8) | GetBValue(chooser.rgbResult);
        } else {
            const auto& choice = colorChoices[selected - 9100];
            if (!choice.color) return;
            layer.effectiveColor = *choice.color;
        }
        layer.trueColor = layer.effectiveColor;
        layer.colorIndex = 256;
    } else if (subItem == 7) {
        HMENU menu = CreatePopupMenu();
        for (std::size_t index = 1; index < lineTypeChoices.size(); ++index)
            AppendMenuW(menu, MF_STRING, 9200 + index, lineTypeChoices[index].label);
        const UINT selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                             screenPoint.x, screenPoint.y, 0, window_, nullptr);
        DestroyMenu(menu);
        if (!selected) return;
        layer.lineType = layer.effectiveLineType = lineTypeChoices[selected - 9200].value;
    } else if (subItem == 8) {
        constexpr std::array<int, 7> weights{{0, 13, 25, 35, 50, 70, 100}};
        HMENU menu = CreatePopupMenu();
        for (std::size_t index = 0; index < weights.size(); ++index) {
            const std::wstring label = weights[index] == 0 ? L"Default" :
                std::to_wstring(weights[index] / 100.0).substr(0, 4) + L" mm";
            AppendMenuW(menu, MF_STRING, 9300 + index, label.c_str());
        }
        const UINT selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                             screenPoint.x, screenPoint.y, 0, window_, nullptr);
        DestroyMenu(menu);
        if (!selected) return;
        layer.lineWeight = layer.effectiveLineWeight = weights[selected - 9300];
    } else if (subItem == 9) {
        constexpr std::array<int, 5> transparencies{{0, 25, 50, 75, 90}};
        HMENU menu = CreatePopupMenu();
        for (std::size_t index = 0; index < transparencies.size(); ++index) {
            const std::wstring label = std::to_wstring(transparencies[index]) + L"%";
            AppendMenuW(menu, MF_STRING, 9400 + index, label.c_str());
        }
        const UINT selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                             screenPoint.x, screenPoint.y, 0, window_, nullptr);
        DestroyMenu(menu);
        if (!selected) return;
        layer.transparency = transparencies[selected - 9400];
    } else return;

    pushUndoSnapshot();
    document_.setLayerProperties(std::move(layer));
    std::erase_if(selectedModels_, [&](std::size_t index) { return !document_.modelIsEditable(index); });
    updateHover(cursorScreen_.x, cursorScreen_.y);
    refreshLayerManager();
    invalidateCanvas();
    updateStatus();
}

void Application::handleLayerManagerCommand(int id) {
    if (id == CmdLayerClose) {
        layerManagerOpen_ = false;
    } else if (id == CmdLayerRefresh) {
        refreshLayerManager();
        return;
    } else if (id == CmdLayerSetCurrent) {
        if (const auto name = selectedLayerName()) setCurrentLayer(*name);
        return;
    } else if (id == CmdLayerDelete) {
        const auto name = selectedLayerName();
        if (!name) return;
        if (*name == currentLayerName()) {
            MessageBoxW(window_, L"Katman 0, geçerli katman veya nesne içeren bir katman silinemez.",
                        L"Layer Manager", MB_OK | MB_ICONWARNING);
            return;
        }
        pushUndoSnapshot();
        if (!document_.deleteLayer(*name)) {
            MessageBoxW(window_, L"Katman silinemedi (nesne içeriyor olabilir).",
                        L"Layer Manager", MB_OK | MB_ICONWARNING);
            return;
        }
        refreshLayerCombo();
        return;
    } else if (id == CmdLayerNew) {
        int suffix = 1;
        std::string name;
        do { name = "Layer " + std::to_string(suffix++); } while (document_.layers().contains(name));
        pushUndoSnapshot();
        document_.createLayer(name);
        refreshLayerCombo();
        const auto row = std::find(displayedLayers_.begin(), displayedLayers_.end(), name);
        if (row != displayedLayers_.end()) {
            const int index = static_cast<int>(std::distance(displayedLayers_.begin(), row));
            ListView_SetItemState(layerList_, index, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
            beginLayerTextEdit(name, index, 1);
        }
        return;
    }
    RECT client{};
    GetClientRect(window_, &client);
    layoutChildren(client.right, client.bottom);
    updateLayerManagerVisibility();
    invalidateCanvas();
}

LRESULT Application::handleLayerManagerNotification(const NMHDR& notification) {
    if (notification.hwndFrom == layerTree_ && notification.code == TVN_SELCHANGEDW) {
        const auto& change = reinterpret_cast<const NMTREEVIEWW&>(notification);
        showUsedLayersOnly_ = change.itemNew.lParam != 0;
        refreshLayerManager();
        return 0;
    }
    if (notification.hwndFrom != layerList_) return 0;
    if (notification.code == NM_CUSTOMDRAW) {
        auto& draw = const_cast<NMLVCUSTOMDRAW&>(
            reinterpret_cast<const NMLVCUSTOMDRAW&>(notification));
        if (draw.nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
        if (draw.nmcd.dwDrawStage == CDDS_ITEMPREPAINT) return CDRF_NOTIFYSUBITEMDRAW;
        if (draw.nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM) && draw.iSubItem == 6) {
            const std::size_t row = static_cast<std::size_t>(draw.nmcd.dwItemSpec);
            if (row >= displayedLayers_.size()) return CDRF_DODEFAULT;
            const auto found = document_.layers().find(displayedLayers_[row]);
            if (found == document_.layers().end()) return CDRF_DODEFAULT;

            RECT cell{};
            if (!ListView_GetSubItemRect(layerList_, static_cast<int>(row), 6, LVIR_BOUNDS, &cell))
                return CDRF_DODEFAULT;
            --cell.right;
            --cell.bottom;
            const bool selected = (ListView_GetItemState(layerList_, static_cast<int>(row),
                                                          LVIS_SELECTED) & LVIS_SELECTED) != 0;
            HBRUSH background = CreateSolidBrush(selected ? RGB(42, 91, 142) : RGB(57, 68, 84));
            FillRect(draw.nmcd.hdc, &cell, background);
            DeleteObject(background);

            const std::uint32_t color = found->second.effectiveColor & 0xFFFFFFu;
            RECT swatch{cell.left + 6, cell.top + 4, cell.left + 22, cell.bottom - 4};
            if (swatch.bottom <= swatch.top) swatch.bottom = swatch.top + 12;
            HBRUSH colorBrush = CreateSolidBrush(RGB((color >> 16) & 0xFF,
                                                     (color >> 8) & 0xFF, color & 0xFF));
            FillRect(draw.nmcd.hdc, &swatch, colorBrush);
            DeleteObject(colorBrush);
            HBRUSH frameBrush = CreateSolidBrush(RGB(18, 22, 29));
            FrameRect(draw.nmcd.hdc, &swatch, frameBrush);
            DeleteObject(frameBrush);

            wchar_t label[16]{};
            std::swprintf(label, std::size(label), L"#%06X", color);
            RECT textRect{swatch.right + 5, cell.top, cell.right - 3, cell.bottom};
            SetBkMode(draw.nmcd.hdc, TRANSPARENT);
            SetTextColor(draw.nmcd.hdc, RGB(235, 240, 247));
            DrawTextW(draw.nmcd.hdc, label, -1, &textRect,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            return CDRF_SKIPDEFAULT;
        }
        return CDRF_DODEFAULT;
    }
    if (notification.code == NM_CLICK || notification.code == NM_DBLCLK) {
        const auto& action = reinterpret_cast<const NMITEMACTIVATE&>(notification);
        if (action.iItem < 0 || static_cast<std::size_t>(action.iItem) >= displayedLayers_.size()) return 0;
        const std::string name = displayedLayers_[static_cast<std::size_t>(action.iItem)];
        if (notification.code == NM_DBLCLK && (action.iSubItem == 1 || action.iSubItem == 10)) {
            beginLayerTextEdit(name, action.iItem, action.iSubItem);
        } else if (action.iSubItem >= 2 && action.iSubItem <= 9) {
            POINT cursor{};
            GetCursorPos(&cursor);
            editLayerProperty(name, action.iSubItem, cursor);
        }
        return 0;
    }
    if (notification.code == LVN_KEYDOWN) {
        const auto& key = reinterpret_cast<const NMLVKEYDOWN&>(notification);
        const auto name = selectedLayerName();
        if (!name) return 0;
        const int row = ListView_GetNextItem(layerList_, -1, LVNI_SELECTED);
        if (key.wVKey == VK_F2) beginLayerTextEdit(*name, row, 1);
        else if (key.wVKey == VK_DELETE) handleLayerManagerCommand(CmdLayerDelete);
        else if (key.wVKey == VK_RETURN) setCurrentLayer(*name);
        return 0;
    }
    return 0;
}

EntityProperties Application::currentEntityProperties() const {
    EntityStyleSelection selection;
    selection.layer = currentLayer_;
    if (currentColorChoice_ >= 0 && static_cast<std::size_t>(currentColorChoice_) < colorChoices.size())
        selection.trueColor = colorChoices[static_cast<std::size_t>(currentColorChoice_)].color;
    if (currentLineTypeChoice_ >= 0 &&
        static_cast<std::size_t>(currentLineTypeChoice_) < lineTypeChoices.size())
        selection.lineType = lineTypeChoices[static_cast<std::size_t>(currentLineTypeChoice_)].value;
    return resolveEntityStyle(selection, document_.layers());
}

void Application::addStyledModel(WireframeModel model) {
    model.setProperties(currentEntityProperties());
    pushUndoSnapshot();
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
        else if (transformCommand_ == TransformCommand::Fillet) text += L"FILLET";
        else if (transformCommand_ == TransformCommand::Rotate) text += L"ROTATE";
        else if (transformCommand_ == TransformCommand::Rotate3D) text += L"3D ROTATE";
        else text += L"ROTATE";
        text += L" \u2014 ";
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
            else if (transformCommand_ == TransformCommand::Rotate) text += L"Döndürme merkezini belirtin";
            else if (transformCommand_ == TransformCommand::Rotate3D) text += L"Döndürme merkezini belirtin";
            else text += L"Baz noktayı belirtin";
        } else if (transformCommand_ == TransformCommand::Offset) {
            text += L"Ofset tarafını belirtin";
        } else if (transformCommand_ == TransformCommand::Mirror) {
            text += L"Ayna ekseninin ikinci noktasını belirtin";
        } else if (transformCommand_ == TransformCommand::Rotate) {
            text += L"Döndürme açısını belirtin (tıklayın veya açı yazıp Enter)";
        } else if (transformCommand_ == TransformCommand::Rotate3D) {
            if (transformPhase_ == TransformPhase::RotateAxis)
                text += L"Ekseni belirtin: X / Y / Z yazın ya da eksen noktası tıklayın";
            else
                text += L"Döndürme açısını belirtin (açı yazıp Enter ya da tıklayın)";
        } else if (transformCommand_ == TransformCommand::LinearArray) {
            text += L"Öğeler arası aralık için ikinci noktayı belirtin";
        } else if (transformCommand_ == TransformCommand::Trim) {
            text += L"Kesilecek çizgi bölümünü tıklayın veya crossing pencere çizin; Enter ile bitir";
        } else if (transformCommand_ == TransformCommand::Extend) {
            text += L"Uzatılacak çizginin uçlarını tıklayın veya crossing pencere çizin; Enter ile bitir";
        } else text += L"İkinci noktayı belirtin";
    } else if (!drawingActive_) {
        if (selectionFirstCorner_) text += L"  |  SEÇİM — diğer köşeyi belirtin";
        else if (!selectedModels_.empty())
            text += L"  |  Seçili: " + std::to_wstring(selectedModels_.size()) +
                    L" — Layer, renk ve çizgi tipini üst listelerden değiştirebilirsiniz";
        else text += L"  |  PASİF — seçmek için nesneye tıklayın veya seçim penceresi oluşturun";
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
    if (orthoEnabled_ && hover_ && hover_->orthoAxis != OrthoAxis::None) {
        text += L" [";
        text += hover_->orthoAxis == OrthoAxis::X ? L"X" :
                hover_->orthoAxis == OrthoAxis::Y ? L"Y" : L"Z";
        text += L"]";
    }
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
    if (beamLoadMode_) text += L"  |  YÜK: Açık";
    text += L"  |  PERF F11: "; text += performanceOverlayEnabled_ ? L"Açık" : L"Kapalı";
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
    if (commandBarPrompt_ && GetWindowTextLengthW(commandBar_) == 0) {
        std::wstring prompt = L" KOMUT:";
        if (beamLoadTargetIndex_) {
            prompt = L" YAYILI YÜK (wY wZ kN/m):";
        } else if (transformCommand_ != TransformCommand::None) {
            if (transformCommand_ == TransformCommand::Fillet) {
                wchar_t rbuf[64]{};
                std::swprintf(rbuf, std::size(rbuf), L" FİLLET R=%.3f:", filletRadius_);
                prompt = rbuf;
            } else {
                prompt = L" " + text.substr(text.find(L"Komut:"));
                if (auto pos = prompt.find(L"  |  "); pos != std::wstring::npos) prompt.resize(pos);
            }
        }
        wchar_t currentPrompt[128]{};
        GetWindowTextW(commandBarPrompt_, currentPrompt, 128);
        if (prompt != L" " + std::wstring(currentPrompt))
            SetWindowTextW(commandBarPrompt_, prompt.c_str());
    }
}

void Application::invalidateCanvas() { if (canvas_) InvalidateRect(canvas_, nullptr, FALSE); }

void Application::invalidateViewCube() {
    if (viewCube_) InvalidateRect(viewCube_, nullptr, FALSE);
}

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
    if (beamLoadMode_) return draftingCursor_;
    if (transformCommand_ == TransformCommand::None)
        return (drawingActive_ || workPlanePicking_ || zoomWindowActive_) ? draftingCursor_ : neutralCursor_;
    return modifierUsesPointCursor(transformCommand_, transformPhase_, arrayItemCount_.has_value(),
                                   offsetDistance_.has_value())
        ? draftingCursor_ : modifyCursor_;
}

DraftView Application::draftView() const {
    DraftView view;
    view.tool = tool_; view.visualStyle = visualStyle_; view.anchor = anchor_; view.facePoints = facePoints_;
    if (hover_) { view.cursor = hover_->point; view.snapType = hover_->type;
                  view.orthoAxis = hover_->orthoAxis; }
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
    view.rotateAxis = rotateAxis_;
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
    view.performanceOverlayEnabled = performanceOverlayEnabled_;
    view.nodeConstraints = &document_.nodeConstraints();
    view.selectedNodeKeys = &selectedNodeConstraints_;
    view.nodeConstraintsVisible = nodeConstraintVisible_;
    view.nodeSelectionFirstCorner = nodeSelectionFirstCorner_;
    view.beamLoads = &document_.beamLoads();
    view.resultView = static_cast<int>(resultView_);
    view.resultScale = resultScale_;
    view.nodeDisplacements = &openseesDisplacements_;
    view.elementForces = &openseesElementForces_;
    view.resultsLoaded = openseesResultsLoaded_;
    view.depthClipEnabled = depthClipEnabled_;
    view.depthClipZMin = depthClipZMin_;
    view.depthClipZMax = depthClipZMax_;
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

void Application::exportS2K() {
    const auto path = chooseFile(true, false);
    if (!path) return;
    try {
        std::ofstream output(*path);
        if (!output) throw std::runtime_error("Could not open file for writing");

        // Helper: format double with comma as decimal separator (European locale)
        const auto fmt = [](double v) -> std::string {
            std::ostringstream s;
            s << std::fixed << std::setprecision(6) << v;
            std::string r = s.str();
            for (auto& c : r) if (c == '.') c = ',';
            return r;
        };

        // Collect unique section names in use
        std::vector<std::string> sectionNames;
        sectionNames.push_back("None");
        for (std::size_t i = 0; i < document_.models().size(); ++i) {
            const auto& model = document_.models()[i];
            if (model.vertices().size() != 2 || model.edges().size() != 1) continue;
            const auto& props = model.properties();
            if (!props.profileName.empty() &&
                std::find(sectionNames.begin(), sectionNames.end(), props.profileName) == sectionNames.end()) {
                sectionNames.push_back(props.profileName);
            }
        }

        output << "TABLE:  \"PROGRAM CONTROL\"\n";
        output << "ProgramName=ModelMaker   Version=1.0   CurrUnits=\"KN, m, C\"\n";
        output << "\n";
        output << "TABLE:  \"ACTIVE DEGREES OF FREEDOM\"\n";
        output << "   UX=Yes   UY=Yes   UZ=Yes   RX=Yes   RY=Yes   RZ=Yes\n";
        output << "\n";
        output << "TABLE:  \"COORDINATE SYSTEMS\"\n";
        output << "Name=GLOBAL   Type=Cartesian   X=0   Y=0   Z=0   AboutZ=0   AboutY=0   AboutX=0\n";
        output << "\n";
        output << "TABLE:  \"MATERIAL PROPERTIES 01 - GENERAL\"\n";
        output << "Material=S275   Type=Steel   SymType=Isotropic   TempDepend=No   Color=Cyan\n";
        output << "TABLE:  \"MATERIAL PROPERTIES 02 - BASIC MECHANICAL PROPERTIES\"\n";
        output << "Material=S275   UnitWeight=76,9729   UnitMass=7,84905   E1=210000000   G12=80769230,77   U12=0,3   A1=1,17E-05\n";
        output << "\n";

        // Frame section properties
        output << "TABLE:  \"FRAME SECTION PROPERTIES 01 - GENERAL\"\n";
        output << "SectionName=None   Material=S275   Shape=General   Area=0   TorsConst=0   I33=0   I22=0\n";
        for (const auto& section : sectionNames) {
            if (section == "None") continue;
            const auto def = std::find_if(profileDefinitions.begin(), profileDefinitions.end(),
                [&](const ProfileDefinition& p) { return p.name == section; });
            if (def != profileDefinitions.end()) {
                output << "SectionName=" << def->name
                       << "   Material=S275"
                       << "   Shape=\"I/Wide Flange\""
                       << "   t3=" << fmt(def->h / 1000.0)
                       << "   t2=" << fmt(def->b / 1000.0)
                       << "   tf=" << fmt(def->tf / 1000.0)
                       << "   tw=" << fmt(def->tw / 1000.0)
                       << "   Area=" << fmt(def->area / 10000.0)
                       << "\n";
            }
        }
        output << "\n";

        // Joint coordinates
        output << "TABLE:  \"JOINT COORDINATES\"\n";
        int jointCounter = 0;
        std::unordered_map<std::string, int> jointMap;
        auto getJoint = [&](const Vec3& pt) -> int {
            std::string key = std::to_string(pt.x) + "," + std::to_string(pt.y) + "," + std::to_string(pt.z);
            auto it = jointMap.find(key);
            if (it != jointMap.end()) return it->second;
            ++jointCounter;
            jointMap[key] = jointCounter;
            output << "Joint=" << jointCounter
                   << "   CoordSys=GLOBAL   CoordType=Cartesian"
                   << "   XorR=" << fmt(pt.x)
                   << "   Y=" << fmt(pt.y)
                   << "   Z=" << fmt(pt.z)
                   << "   SpecialJt=Yes\n";
            return jointCounter;
        };

        // Collect all unique joint points
        for (std::size_t i = 0; i < document_.models().size(); ++i)
            if (document_.models()[i].vertices().size() == 2 && document_.models()[i].edges().size() == 1) {
                getJoint(document_.models()[i].vertices()[0]);
                getJoint(document_.models()[i].vertices()[1]);
            }
        output << "\n";

        // Connectivity - Frame
        output << "TABLE:  \"CONNECTIVITY - FRAME\"\n";
        int frameIdx = 1;
        for (std::size_t i = 0; i < document_.models().size(); ++i) {
            const auto& model = document_.models()[i];
            if (model.vertices().size() != 2 || model.edges().size() != 1) continue;
            ++frameIdx;
            output << "Frame=" << frameIdx
                   << "   JointI=" << getJoint(model.vertices()[0])
                   << "   JointJ=" << getJoint(model.vertices()[1])
                   << "   IsCurved=No\n";
        }
        output.close();
    } catch (const std::exception& error) {
        showError(L"SAP2000 S2K dışa aktarma başarısız", error);
    }
}

void Application::exportOpenSees() {
    const auto path = chooseFile(true, false);
    if (!path) return;
    auto tclPath = *path;
    if (tclPath.extension() != L".tcl") tclPath.replace_extension(L".tcl");
    try {
        std::ofstream output(tclPath);
        if (!output) throw std::runtime_error("Could not open file for writing");
        output << "# OpenSees Tcl script generated by ModelMaker\n";
        output << "wipe\n";
        output << "model BasicBuilder -ndm 3 -ndf 6\n";
        output << "\n";

        // Collect unique joints
        struct Joint {
            int id;
            double x, y, z;
        };
        std::vector<Joint> joints;
        std::unordered_map<std::string, int> jointMap;
        auto getJoint = [&](const Vec3& pt) -> int {
            std::string key = std::to_string(pt.x) + "," + std::to_string(pt.y) + "," + std::to_string(pt.z);
            auto it = jointMap.find(key);
            if (it != jointMap.end()) return it->second;
            int id = static_cast<int>(joints.size()) + 1;
            jointMap[key] = id;
            joints.push_back({id, pt.x, pt.y, pt.z});
            return id;
        };

        // Collect elements (only lines with exactly 2 vertices)
        struct FrameElement {
            int id, jointI, jointJ;
            std::string section;
        };
        std::vector<FrameElement> frames;
        for (std::size_t i = 0; i < document_.models().size(); ++i) {
            const auto& model = document_.models()[i];
            if (model.vertices().size() != 2 || model.edges().size() != 1) continue;
            int ji = getJoint(model.vertices()[0]);
            int jj = getJoint(model.vertices()[1]);
            const auto& props = model.properties();
            std::string section = props.profileName.empty() ? "None" : props.profileName;
            frames.push_back({static_cast<int>(frames.size()) + 1, ji, jj, section});
        }

        // Nodes
        output << "# Nodes\n";
        for (const auto& j : joints)
            output << "node " << j.id << " " << j.x << " " << j.y << " " << j.z << "\n";
        output << "\n";

        // Material - steel
        output << "# Material\n";
        output << "uniaxialMaterial Elastic 1 210000000.0\n";
        output << "\n";

        // Section definitions - collect unique section names
        std::vector<std::string> usedSections;
        usedSections.push_back("None");
        for (const auto& f : frames)
            if (f.section != "None" && std::find(usedSections.begin(), usedSections.end(), f.section) == usedSections.end())
                usedSections.push_back(f.section);

        output << "# Section properties\n";
        output << "set A_None 0.0; set Iz_None 0.0; set Iy_None 0.0; set J_None 0.0\n";
        for (const auto& s : usedSections) {
            if (s == "None") continue;
            const auto def = std::find_if(profileDefinitions.begin(), profileDefinitions.end(),
                [&](const ProfileDefinition& p) { return p.name == s; });
            if (def != profileDefinitions.end()) {
                double area = def->area / 10000.0; // cm² to m²
                double h = def->h / 1000.0;
                double b = def->b / 1000.0;
                double Izz = b * h * h * h / 12.0; // Approximate
                double Iyy = h * b * b * b / 12.0;
                double J = (b * h * h * h / 3.0) * (1.0 - 0.63 * h / b);
                output << "set A_" << s << " " << area << "\n";
                output << "set Iz_" << s << " " << Izz << "\n";
                output << "set Iy_" << s << " " << Iyy << "\n";
                output << "set J_" << s << " " << J << "\n";
            }
        }
        output << "\n";

        // Geometric transformation
        output << "# Geometric transformation\n";
        output << "geomTransf Linear 1 0 0 1\n";
        output << "\n";

        // Elements
        output << "# Frame elements\n";
        for (const auto& f : frames) {
            std::string s = f.section.empty() || f.section == "None" ? "None" : f.section;
            output << "element elasticBeamColumn " << f.id << " " << f.jointI << " " << f.jointJ
                   << " $A_" << s << " $E 210000000.0 $G 80769230.77"
                   << " $J_" << s << " $Iz_" << s << " $Iy_" << s << " 1\n";
        }
        output << "\n";

        // Boundary conditions - use node constraints or auto-fix at Z=0
        output << "# Boundary conditions\n";
        for (const auto& j : joints) {
            std::string key = std::to_string(j.x) + "," + std::to_string(j.y) + "," + std::to_string(j.z);
            auto it = document_.nodeConstraints().find(key);
            if (it != document_.nodeConstraints().end()) {
                const auto& c = it->second;
                if (!c.isFree()) {
                    output << "fix " << j.id;
                    output << " " << (c.ux ? 1 : 0);
                    output << " " << (c.uy ? 1 : 0);
                    output << " " << (c.uz ? 1 : 0);
                    output << " " << (c.rx ? 1 : 0);
                    output << " " << (c.ry ? 1 : 0);
                    output << " " << (c.rz ? 1 : 0);
                    output << "\n";
                }
            } else if (std::abs(j.z) < 1e-9) {
                output << "fix " << j.id << " 1 1 1 1 1 1\n";
            }
        }
        output << "\n";

        // Loads - self-weight
        output << "# Loads - user-defined beam loads + self weight\n";
        output << "set g 9.81\n";
        output << "pattern Plain 1 Linear {\n";
        // User-defined beam loads first
        for (std::size_t i = 0; i < frames.size(); ++i) {
            if (i >= document_.models().size()) continue;
            auto bl = document_.getBeamLoad(i);
            if (bl && (bl->wY != 0.0 || bl->wZ != 0.0))
                output << "  eleLoad -ele " << (i + 1) << " -type -beamUniform " << bl->wY << " " << bl->wZ << "\n";
        }
        // Self-weight
        for (const auto& f : frames) {
            std::string s = f.section.empty() || f.section == "None" ? "None" : f.section;
            output << "  eleLoad -ele " << f.id << " -type -beamUniform 0 0 -$A_" << s << "*7850.0*$g\n";
        }
        output << "}\n";
        output << "\n";

        // Analysis
        output << "# Analysis\n";
        output << "constraints Plain\n";
        output << "numberer Plain\n";
        output << "system BandGeneral\n";
        output << "test NormDispIncr 1.0e-6 10\n";
        output << "algorithm Newton\n";
        output << "integrator LoadControl 0.1\n";
        output << "analysis Static\n";
        output << "analyze 10\n";
        output << "\n";

        // Output
        output << "# Output\n";
        output << "recorder Node -file node_disp.out -time -node";
        for (const auto& j : joints) output << " " << j.id;
        output << " -dof 1 2 3 4 5 6 disp\n";
        output << "recorder Element -file elem_force.out -time -ele";
        for (const auto& f : frames) output << " " << f.id;
        output << " force\n";
        output << "\n";
        output << "puts \"Analysis complete\"\n";

        output.close();
    } catch (const std::exception& error) {
        showError(L"OpenSees Tcl dışa aktarma başarısız", error);
    }
}

void Application::runOpenSees() {
    // First check if OpenSees.exe is available
    const auto openseesPaths = {L"OpenSees.exe", L"opensees.exe"};
    std::filesystem::path openseesPath;
    bool found = false;
    for (const auto& name : openseesPaths) {
        wchar_t pathBuf[MAX_PATH]{};
        if (SearchPathW(nullptr, name, nullptr, MAX_PATH, pathBuf, nullptr)) {
            openseesPath = pathBuf;
            found = true;
            break;
        }
    }
    if (!found) {
        // Try common install locations
        wchar_t exeDir[MAX_PATH]{};
        if (GetModuleFileNameW(nullptr, exeDir, MAX_PATH)) {
            std::filesystem::path appPath(exeDir);
            // Check both the exe directory and its parent (project root)
            for (const auto& base : {appPath.parent_path(), appPath.parent_path().parent_path()}) {
                auto projectOpenSees = base / L"OpenSees3.8.0" / L"bin" / L"OpenSees.exe";
                if (std::filesystem::exists(projectOpenSees)) {
                    openseesPath = projectOpenSees;
                    found = true;
                    break;
                }
            }
        }
    }
    if (!found) {
        const auto candidates = {
            L"C:\\Program Files\\OpenSees\\OpenSees.exe",
            L"C:\\Program Files (x86)\\OpenSees\\OpenSees.exe",
        };
        for (const auto& candidate : candidates)
            if (std::filesystem::exists(candidate)) {
                openseesPath = candidate;
                found = true;
                break;
            }
    }
    if (!found) {
        MessageBoxW(window_, L"OpenSees bulunamadı. Önce OpenSees'i kurun veya Tcl script'i manuel çalıştırın.",
                    L"OpenSees", MB_OK | MB_ICONINFORMATION);
        return;
    }

    // Export Tcl script to temp file
    const auto tclPath = std::filesystem::temp_directory_path() / "model_maker_opensees.tcl";
    const auto workDir = tclPath.parent_path();
    // Generate Tcl script directly to temp path
    std::ofstream output(tclPath);
    if (!output) {
        MessageBoxW(window_, L"Tcl script yazılamadı.", L"OpenSees", MB_OK | MB_ICONERROR);
        return;
    }
    // Write the Tcl script using the same logic as exportOpenSees
    // (Duplicate the generation logic here)
    output << "# OpenSees Tcl script generated by ModelMaker\nwipe\n";
    output << "model BasicBuilder -ndm 3 -ndf 6\n\n";

    // Collect joints and frames (same logic as exportOpenSees)
    struct OJ { int id; double x, y, z; };
    struct OF { int id, ji, jj; std::string sec; };
    std::vector<OJ> joints;
    std::vector<OF> frames;
    std::unordered_map<std::string, int> jointMap;
    auto getJoint = [&](const Vec3& pt) -> int {
        std::string key = std::to_string(pt.x) + "," + std::to_string(pt.y) + "," + std::to_string(pt.z);
        auto it = jointMap.find(key);
        if (it != jointMap.end()) return it->second;
        int id = static_cast<int>(joints.size()) + 1;
        jointMap[key] = id;
        joints.push_back({id, pt.x, pt.y, pt.z});
        return id;
    };
    for (std::size_t i = 0; i < document_.models().size(); ++i) {
        const auto& model = document_.models()[i];
        if (model.vertices().size() != 2 || model.edges().size() != 1) continue;
        std::string section = model.properties().profileName.empty() ? "None" : model.properties().profileName;
        frames.push_back({static_cast<int>(frames.size()) + 1, getJoint(model.vertices()[0]),
                          getJoint(model.vertices()[1]), section});
    }
    for (const auto& j : joints) output << "node " << j.id << " " << j.x << " " << j.y << " " << j.z << "\n";
    output << "\nuniaxialMaterial Elastic 1 210000000.0\n\n";

    // Sections
    std::vector<std::string> usedSects;
    usedSects.push_back("None");
    for (const auto& f : frames)
        if (f.sec != "None" && std::find(usedSects.begin(), usedSects.end(), f.sec) == usedSects.end())
            usedSects.push_back(f.sec);
    output << "set A_None 0.0; set Iz_None 0.0; set Iy_None 0.0; set J_None 0.0\n";
    for (const auto& s : usedSects) {
        if (s == "None") continue;
        const auto def = std::find_if(profileDefinitions.begin(), profileDefinitions.end(),
            [&](const ProfileDefinition& p) { return p.name == s; });
        if (def != profileDefinitions.end()) {
            double area = def->area / 10000.0; double h = def->h / 1000.0; double b = def->b / 1000.0;
            output << "set A_" << s << " " << area << "\nset Iz_" << s << " " << (b*h*h*h/12.0)
                   << "\nset Iy_" << s << " " << (h*b*b*b/12.0) << "\nset J_" << s << " "
                   << ((b*h*h*h/3.0)*(1.0-0.63*h/b)) << "\n";
        }
    }
    output << "\ngeomTransf Linear 1 0 0 1\n\n";
    for (const auto& f : frames) {
        std::string s = f.sec.empty() || f.sec == "None" ? "None" : f.sec;
        output << "element elasticBeamColumn " << f.id << " " << f.ji << " " << f.jj
               << " $A_" << s << " 210000000.0 80769230.77"
               << " $J_" << s << " $Iz_" << s << " $Iy_" << s << " 1\n";
    }
    output << "\n";
    for (const auto& j : joints)
        if (std::abs(j.z) < 1e-9) output << "fix " << j.id << " 1 1 1 1 1 1\n";
    output << "\nset g 9.81\npattern Plain 1 Linear {\n";
    for (const auto& f : frames) {
        std::string s = f.sec.empty() || f.sec == "None" ? "None" : f.sec;
        output << "  eleLoad -ele " << f.id << " -type -beamUniform 0 0 -$A_" << s << "*7850.0*$g\n";
    }
    output << "}\n\nconstraints Plain\nnumberer Plain\nsystem BandGeneral\n";
    output << "test NormDispIncr 1.0e-6 10\nalgorithm Newton\nintegrator LoadControl 0.1\n";
    output << "analysis Static\nanalyze 10\n\nrecorder Node -file node_disp.out -time -node";
    for (const auto& j : joints) output << " " << j.id;
    output << " -dof 1 2 3 4 5 6 disp\n";
    output << "recorder Element -file elem_force.out -time -ele";
    for (const auto& f : frames) output << " " << f.id;
    output << " force\n\nputs \"Analysis complete\"\n";
    output.close();
    std::wstring cmd = L"\"" + openseesPath.wstring() + L"\"" + L" \"" + tclPath.wstring() + L"\"";
    PROCESS_INFORMATION procInfo{};
    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_HIDE;

    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                         CREATE_NO_WINDOW, nullptr, workDir.c_str(), &startupInfo, &procInfo)) {
        MessageBoxW(window_, L"OpenSees çalıştırılamadı.", L"OpenSees", MB_OK | MB_ICONERROR);
        return;
    }

    WaitForSingleObject(procInfo.hProcess, INFINITE);
    CloseHandle(procInfo.hProcess);
    CloseHandle(procInfo.hThread);

    // Load results
    loadOpenSeesResults();
    invalidateCanvas();
}

void Application::loadOpenSeesResults() {
    const auto workDir = std::filesystem::temp_directory_path();
    const auto dispPath = workDir / "node_disp.out";
    const auto forcePath = workDir / "elem_force.out";

    openseesDisplacements_.clear();
    openseesElementForces_.clear();

    std::ifstream dispFile(dispPath);
    if (!dispFile) {
        MessageBoxW(window_, L"node_disp.out bulunamadı. Analiz sonuçlanmamış olabilir.",
                    L"OpenSees", MB_OK | MB_ICONWARNING);
        return;
    }

    // Parse displacements: one line per step, take the LAST line.
    // Format: time n1_dx n1_dy n1_dz n2_dx n2_dy n2_dz ... (3 dofs per node)
    std::string line, lastLine;
    while (std::getline(dispFile, line))
        if (!line.empty()) lastLine = line;
    if (lastLine.empty()) return;
    std::istringstream iss(lastLine);
    double time;
    iss >> time;
    double dx, dy, dz;
    while (iss >> dx >> dy >> dz)
        openseesDisplacements_.push_back({dx, dy, dz});

    // Parse element forces: same pattern, last line
    std::ifstream forceFile(forcePath);
    if (forceFile) {
        std::string fLine, fLast;
        while (std::getline(forceFile, fLine))
            if (!fLine.empty()) fLast = fLine;
        if (!fLast.empty()) {
            std::istringstream fss(fLast);
            double ft; fss >> ft;
            double fv;
            while (fss >> fv) openseesElementForces_.push_back(fv);
        }
    }

    openseesResultsLoaded_ = !openseesDisplacements_.empty();
    if (openseesResultsLoaded_)
        MessageBoxW(window_, (L"Sonuçlar yüklendi: " + std::to_wstring(openseesDisplacements_.size()) +
                              L" düğüm deplasmanı, " + std::to_wstring(openseesElementForces_.size()) +
                              L" eleman kuvveti").c_str(), L"OpenSees", MB_OK | MB_ICONINFORMATION);
    updateStatus();
}

void Application::clearOpenSeesResults() {
    openseesDisplacements_.clear();
    openseesElementForces_.clear();
    openseesResultsLoaded_ = {};
    invalidateCanvas();
    updateStatus();
}

void Application::analyzeOpenSees() {
    // Diagnostic: verify the function is called
    if (document_.models().empty()) {
        MessageBoxW(window_, L"Model boş — analiz yapılacak çubuk yok.", L"Analiz", MB_OK | MB_ICONWARNING);
        return;
    }
    // Find OpenSees executable
    std::filesystem::path openseesPath;
    bool found = false;
    wchar_t exeBuf[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exeBuf, MAX_PATH)) {
        std::filesystem::path appPath(exeBuf);
        for (const auto& base : {appPath.parent_path(), appPath.parent_path().parent_path()}) {
            auto candidate = base / L"OpenSees3.8.0" / L"bin" / L"OpenSees.exe";
            if (std::filesystem::exists(candidate)) { openseesPath = candidate; found = true; break; }
        }
    }
    if (!found) {
        wchar_t pathBuf[MAX_PATH]{};
        if (SearchPathW(nullptr, L"OpenSees.exe", nullptr, MAX_PATH, pathBuf, nullptr)) {
            openseesPath = pathBuf; found = true;
        }
    }
    if (!found) {
        MessageBoxW(window_, L"OpenSees bulunamadı.", L"Analiz", MB_OK | MB_ICONWARNING);
        return;
    }

    const auto workDir = std::filesystem::temp_directory_path();
    const auto tclPath = workDir / "model_maker_analysis.tcl";
    const auto dispPath = workDir / "node_disp.out";
    const auto forcePath = workDir / "elem_force.out";
    const auto logPath = workDir / "opensees_log.txt";

    std::ofstream output(tclPath);
    if (!output) { MessageBoxW(window_, L"Tcl script yazılamadı.", L"Analiz", MB_OK | MB_ICONERROR); return; }
    output << "# OpenSees analysis script generated by ModelMaker\nwipe\nmodel BasicBuilder -ndm 3 -ndf 6\n\n";
    struct OJ { int id; double x, y, z; };
    struct OF { int id, ji, jj; std::string sec; };
    std::vector<OJ> joints; std::vector<OF> frames;
    std::unordered_map<std::string, int> jointMap;
    auto getJoint = [&](const Vec3& pt) -> int {
        // Merge coincident nodes with 1e-6 tolerance (round to micro-units)
        auto r = [](double v) { return std::llround(v * 1.0e6); };
        std::string key = std::to_string(r(pt.x)) + "," + std::to_string(r(pt.y)) + "," + std::to_string(r(pt.z));
        auto it = jointMap.find(key);
        if (it != jointMap.end()) return it->second;
        int id = static_cast<int>(joints.size()) + 1;
        jointMap[key] = id; joints.push_back({id, pt.x, pt.y, pt.z});
        return id;
    };
    for (std::size_t i = 0; i < document_.models().size(); ++i) {
        const auto& m = document_.models()[i];
        if (m.vertices().size() != 2 || m.edges().size() != 1) continue;
        int a = getJoint(m.vertices()[0]);
        int b = getJoint(m.vertices()[1]);
        if (a == b) continue; // zero-length element -> skip
        frames.push_back({static_cast<int>(frames.size()) + 1, a, b,
                          m.properties().profileName.empty() ? "None" : m.properties().profileName});
    }
    if (joints.empty() || frames.empty()) {
        output.close();
        MessageBoxW(window_, L"Analiz için çubuk elemanı bulunamadı.", L"Analiz", MB_OK | MB_ICONWARNING);
        return;
    }

    // Supports live at the lowest Y level (Y-up convention)
    double minY = 1e300;
    for (const auto& j : joints) minY = std::min(minY, j.y);

    // Connectivity: keep only elements connected to a supported node (union-find).
    // Unconnected fragments make the stiffness matrix singular.
    std::vector<int> parent(joints.size() + 1);
    for (std::size_t i = 0; i < parent.size(); ++i) parent[i] = static_cast<int>(i);
    std::function<int(int)> findRoot = [&](int x) -> int {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    };
    for (const auto& f : frames) {
        int ra = findRoot(f.ji), rb = findRoot(f.jj);
        if (ra != rb) parent[ra] = rb;
    }
    std::unordered_set<int> supportedRoots;
    for (const auto& j : joints)
        if (std::abs(j.y - minY) < 1e-3) supportedRoots.insert(findRoot(j.id));
    std::size_t skipped = 0;
    {
        std::vector<OF> kept;
        for (const auto& f : frames) {
            if (supportedRoots.count(findRoot(f.ji))) kept.push_back(f);
            else ++skipped;
        }
        frames = std::move(kept);
    }
    if (frames.empty()) {
        output.close();
        MessageBoxW(window_, L"Hiçbir çubuk mesnetli bir bileşene bağlı değil. Model tabana (en düşük Y kotuna) bağlanmalı.",
                    L"Analiz", MB_OK | MB_ICONWARNING);
        return;
    }
    // Renumber joints/frames compactly (only used ones)
    {
        std::unordered_map<int, int> newId;
        std::vector<OJ> keptJoints;
        auto mapId = [&](int oldId) -> int {
            auto it = newId.find(oldId);
            if (it != newId.end()) return it->second;
            int id = static_cast<int>(keptJoints.size()) + 1;
            const auto& oj = joints[static_cast<std::size_t>(oldId) - 1];
            keptJoints.push_back({id, oj.x, oj.y, oj.z});
            newId[oldId] = id;
            return id;
        };
        for (auto& f : frames) { f.ji = mapId(f.ji); f.jj = mapId(f.jj); }
        for (std::size_t i = 0; i < frames.size(); ++i) frames[i].id = static_cast<int>(i) + 1;
        joints = std::move(keptJoints);
    }
    for (const auto& j : joints) output << "node " << j.id << " " << j.x << " " << j.y << " " << j.z << "\n";
    output << "\nuniaxialMaterial Elastic 1 210000000.0\n\n";
    std::vector<std::string> usedSects; usedSects.push_back("None");
    for (const auto& f : frames)
        if (f.sec != "None" && std::find(usedSects.begin(), usedSects.end(), f.sec) == usedSects.end())
            usedSects.push_back(f.sec);
    output << "set A_None 0.01; set Iz_None 1.0e-5; set Iy_None 1.0e-5; set J_None 1.0e-5\n";
    for (const auto& s : usedSects) {
        if (s == "None") continue;
        const auto def = std::find_if(profileDefinitions.begin(), profileDefinitions.end(),
            [&](const ProfileDefinition& p) { return p.name == s; });
        if (def != profileDefinitions.end()) {
            double a = def->area / 10000.0, h = def->h / 1000.0, b = def->b / 1000.0;
            output << "set A_" << s << " " << a << "\nset Iz_" << s << " " << (b*h*h*h/12.0)
                   << "\nset Iy_" << s << " " << (h*b*b*b/12.0) << "\nset J_" << s << " "
                   << ((b*h*h*h/3.0)*(1.0-0.63*h/b)) << "\n";
        }
    }
    // Two transforms: transf 1 (vecxz = 0 0 1) for general members,
    // transf 2 (vecxz = 1 0 0) for members parallel to global Z
    output << "\ngeomTransf Linear 1 0 0 1\ngeomTransf Linear 2 1 0 0\n\n";
    auto jointById = [&](int id) -> const OJ& { return joints[static_cast<std::size_t>(id) - 1]; };
    for (const auto& f : frames) {
        std::string s = f.sec.empty() || f.sec == "None" ? "None" : f.sec;
        const auto& a = jointById(f.ji);
        const auto& b = jointById(f.jj);
        double dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
        double len = std::sqrt(dx*dx + dy*dy + dz*dz);
        int transf = 1;
        if (len > 1e-12) {
            // if member axis is (anti)parallel to global Z, vecxz (0 0 1) fails -> use transf 2
            double cosZ = std::abs(dz) / len;
            if (cosZ > 0.999) transf = 2;
        }
        output << "element elasticBeamColumn " << f.id << " " << f.ji << " " << f.jj
               << " $A_" << s << " 210000000.0 80769230.77"
               << " $J_" << s << " $Iz_" << s << " $Iy_" << s << " " << transf << "\n";
    }
    output << "\n";
    // Supports: node constraints or lowest-Y auto-fix
    for (const auto& j : joints) {
        std::string key = std::to_string(j.x) + "," + std::to_string(j.y) + "," + std::to_string(j.z);
        auto nc = document_.nodeConstraints().find(key);
        if (nc != document_.nodeConstraints().end() && !nc->second.isFree())
            output << "fix " << j.id << " " << (nc->second.ux?1:0) << " " << (nc->second.uy?1:0)
                   << " " << (nc->second.uz?1:0) << " " << (nc->second.rx?1:0)
                   << " " << (nc->second.ry?1:0) << " " << (nc->second.rz?1:0) << "\n";
        else if (std::abs(j.y - minY) < 1e-3)
            output << "fix " << j.id << " 1 1 1 1 1 1\n";
    }
    output << "\nset g 9.81\npattern Plain 1 Linear {\n";
    for (const auto& f : frames) {
        std::string s = f.sec.empty() || f.sec == "None" ? "None" : f.sec;
        // self-weight acts along -Y (gravity), local loads via -beamUniform Wy Wz
        output << "  eleLoad -ele " << f.id << " -type -beamUniform [expr -$A_" << s << "*7850.0*$g] 0\n";
    }
    // Tcl treats Windows backslashes as escapes (e.g. \t in Temp), so recorder paths
    // must use forward slashes. Otherwise OpenSees reports success but creates no files.
    const auto dispTclPath = dispPath.generic_string();
    const auto forceTclPath = forcePath.generic_string();
    output << "}\n\nconstraints Plain\nnumberer RCM\nsystem BandGeneral\n"
           << "test NormDispIncr 1.0e-6 25\nalgorithm Newton\nintegrator LoadControl 0.1\n"
           << "analysis Static\n"
           << "recorder Node -file \"" << dispTclPath << "\" -time -nodeRange 1 "
           << joints.size() << " -dof 1 2 3 disp\n"
           << "recorder Element -file \"" << forceTclPath << "\" -time -eleRange 1 "
           << frames.size() << " localForce\n"
           << "set ok [analyze 10]\n"
           << "if {$ok == 0} { puts \"ANALYSIS_OK\" } else { puts \"ANALYSIS_FAILED code $ok\" }\n";
    output.close();

    // Write a batch file to capture stdout/stderr (CreateProcessW ignores shell redirects)
    const auto batPath = workDir / "_run_opensees.bat";
    { std::ofstream bat(batPath); bat << "@\"" << openseesPath.string() << "\" \"" << tclPath.string() << "\" > \"" << logPath.string() << "\" 2>&1"; }
    std::wstring cmd = L"cmd.exe /c \"" + batPath.wstring() + L"\"";
    PROCESS_INFORMATION pi{}; STARTUPINFOW si{};
    si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                         0, nullptr, workDir.c_str(), &si, &pi)) {
        MessageBoxW(window_, L"OpenSees çalıştırılamadı.", L"Analiz", MB_OK | MB_ICONERROR); return;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);

    if (exitCode != 0) {
        MessageBoxW(window_, (L"OpenSees hata koduyla kapandı: " + std::to_wstring(exitCode)).c_str(),
                    L"Analiz", MB_OK | MB_ICONERROR);
        return;
    }
    if (!std::filesystem::exists(dispPath)) {
        std::wstring msg = L"node_disp.out oluşturulamadı.\nOpenSees çıktısı:\n";
        std::ifstream logFile(logPath);
        if (logFile) msg += utf8ToWide({std::istreambuf_iterator<char>(logFile), {}});
        MessageBoxW(window_, msg.c_str(), L"Analiz", MB_OK | MB_ICONWARNING); return;
    }
    loadOpenSeesResults();
    if (openseesResultsLoaded_)
        MessageBoxW(window_, L"Analiz başarılı! Sonuçlar görselleştirildi.", L"Analiz", MB_OK | MB_ICONINFORMATION);
}

void Application::saveOptions() {
    std::filesystem::create_directories(optionsPath_.parent_path());
    std::ofstream output(optionsPath_);
    if (!output) return;
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    GetWindowPlacement(window_, &placement);
    output << "MMOPT1\n";
    output << placement.showCmd << ' ' << placement.rcNormalPosition.left << ' '
           << placement.rcNormalPosition.top << ' '
           << placement.rcNormalPosition.right << ' '
           << placement.rcNormalPosition.bottom << '\n';
    output << static_cast<int>(visualStyle_) << '\n';
    output << snapEnabled_ << ' ' << gridSnapEnabled_ << ' ' << orthoEnabled_ << ' '
           << polarTrackingEnabled_ << ' ' << dynamicInputEnabled_ << ' '
           << performanceOverlayEnabled_ << '\n';
    output << layerManagerOpen_ << ' ' << showUsedLayersOnly_ << '\n';
    output << currentLayer_ << '\n';
    output << currentColorChoice_ << ' ' << currentLineTypeChoice_ << '\n';
    output << static_cast<int>(activeRibbonTab_) << '\n';
    output << static_cast<int>(mode_) << '\n';
    output << static_cast<int>(lastTransformCommand_) << '\n';
    for (const auto snap : enabledSnapTypes_)
        output << (snap ? '1' : '0');
    output << '\n';
}

void Application::loadOptions() {
    std::ifstream input(optionsPath_);
    if (!input) return;
    std::string signature;
    input >> signature;
    if (signature != "MMOPT1") return;
    int showCmd{}, left{}, top{}, right{}, bottom{};
    input >> showCmd >> left >> top >> right >> bottom;
    int visualStyleRaw{};
    input >> visualStyleRaw;
    if (visualStyleRaw >= 0 && visualStyleRaw <= 2)
        visualStyle_ = static_cast<VisualStyle>(visualStyleRaw);
    input >> snapEnabled_ >> gridSnapEnabled_ >> orthoEnabled_
          >> polarTrackingEnabled_ >> dynamicInputEnabled_
          >> performanceOverlayEnabled_;
    input >> layerManagerOpen_ >> showUsedLayersOnly_;
    input >> currentLayer_;
    input >> currentColorChoice_ >> currentLineTypeChoice_;
    int tabRaw{}, modeRaw{}, lastCommandRaw{};
    input >> tabRaw >> modeRaw >> lastCommandRaw;
    if (tabRaw >= 0 && tabRaw <= 4) activeRibbonTab_ = static_cast<RibbonTab>(tabRaw);
    if (modeRaw >= 0 && modeRaw <= 1) mode_ = static_cast<EditMode>(modeRaw);
    if (lastCommandRaw >= 0 && lastCommandRaw <= 10)
        lastTransformCommand_ = static_cast<TransformCommand>(lastCommandRaw);
    std::string snapFlags;
    input >> snapFlags;
    for (std::size_t index = 0; index < snapFlags.size() && index < enabledSnapTypes_.size(); ++index)
        enabledSnapTypes_[index] = snapFlags[index] == '1';

    if (showCmd == SW_SHOWMAXIMIZED) showCmd = SW_MAXIMIZE;
    else if (showCmd != SW_MINIMIZE && showCmd != SW_SHOWMINIMIZED) showCmd = SW_NORMAL;
    if (window_) {
        WINDOWPLACEMENT placement{};
        placement.length = sizeof(placement);
        placement.showCmd = static_cast<UINT>(showCmd);
        placement.rcNormalPosition = {left, top, right, bottom};
        SetWindowPlacement(window_, &placement);
    }
}

void Application::updateCommandBar() {
    if (!commandBar_) return;
    std::wstring cmd;
    if (transformCommand_ != TransformCommand::None) {
        switch (transformCommand_) {
        case TransformCommand::Move: cmd = L"MOVE"; break;
        case TransformCommand::Copy: cmd = L"COPY"; break;
        case TransformCommand::Offset: cmd = L"OFFSET"; break;
        case TransformCommand::Mirror: cmd = L"MIRROR"; break;
        case TransformCommand::Delete: cmd = L"DELETE"; break;
        case TransformCommand::LinearArray: cmd = L"LINEAR ARRAY"; break;
        case TransformCommand::PolarArray: cmd = L"POLAR ARRAY"; break;
        case TransformCommand::Trim: cmd = L"TRIM"; break;
        case TransformCommand::Extend: cmd = L"EXTEND"; break;
        case TransformCommand::Fillet: {
            wchar_t buf[64]{};
            std::swprintf(buf, std::size(buf), L"FILLET  R=%.3f", filletRadius_);
            cmd = buf;
            break;
        }
        default: break;
        }
    } else if (drawingActive_) {
        switch (tool_) {
        case DrawTool::Line: cmd = L"LINE"; break;
        case DrawTool::Polyline: cmd = L"POLYLINE"; break;
        case DrawTool::Rectangle: cmd = L"RECTANGLE"; break;
        case DrawTool::Circle: cmd = L"CIRCLE"; break;
        case DrawTool::Face3D: cmd = L"3DFACE"; break;
        default: break;
        }
    } else if (workPlanePicking_) {
        cmd = L"WORKPLANE";
    } else if (transformCommand_ == TransformCommand::None && !drawingActive_ && !workPlanePicking_ &&
               !zoomWindowActive_ && selectedModels_.empty() && !selectionFirstCorner_) {
        cmd = L"PASİF";
    }
    if (!cmd.empty()) {
        wchar_t prompt[128]{};
        GetWindowTextW(commandBar_, prompt, 128);
        if (std::wstring(prompt) != cmd) {
            SetWindowTextW(commandBar_, cmd.c_str());
            SendMessageW(commandBar_, EM_SETSEL, 0, -1);
        }
        return;
    }
    if (GetWindowTextLengthW(commandBar_) > 0) SetWindowTextW(commandBar_, L"");
}

bool Application::commandBarInput(const std::wstring& input) {
    if (input.empty()) return false;
    input_ = input;
    onCharacter(L'\r');
    if (input_.empty()) {
        updateCommandBar();
        updateStatus();
        return true;
    }
    input_.clear();
    return false;
}

void Application::processCommandLine(const std::wstring& command) {
    if (command.empty()) return;
    std::wstring cmd = command;
    for (auto& c : cmd) c = static_cast<wchar_t>(std::towupper(c));
    if (cmd == L"LINE" || cmd == L"L") { executeCommand(CmdLine); }
    else if (cmd == L"POLYLINE" || cmd == L"PL") { executeCommand(CmdPolyline); }
    else if (cmd == L"RECTANGLE" || cmd == L"REC") { executeCommand(CmdRectangle); }
    else if (cmd == L"CIRCLE" || cmd == L"C") { executeCommand(CmdCircle); }
    else if (cmd == L"3DFACE" || cmd == L"FACE" || cmd == L"F") { executeCommand(CmdFace3D); }
    else if (cmd == L"MOVE" || cmd == L"M") { executeCommand(CmdMove); }
    else if (cmd == L"COPY" || cmd == L"CP") { executeCommand(CmdCopy); }
    else if (cmd == L"OFFSET" || cmd == L"O") { executeCommand(CmdOffset); }
    else if (cmd == L"MIRROR" || cmd == L"MI") { executeCommand(CmdMirror); }
    else if (cmd == L"DELETE" || cmd == L"DEL" || cmd == L"ERASE" || cmd == L"E") { executeCommand(CmdDelete); }
    else if (cmd == L"LINEARARRAY" || cmd == L"ARRAY" || cmd == L"AR") { executeCommand(CmdLinearArray); }
    else if (cmd == L"POLARARRAY" || cmd == L"PARRAY") { executeCommand(CmdPolarArray); }
    else if (cmd == L"TRIM" || cmd == L"TR") { executeCommand(CmdTrim); }
    else if (cmd == L"EXTEND" || cmd == L"EX") { executeCommand(CmdExtend); }
    else if (cmd == L"FILLET" || cmd == L"FILLET") { executeCommand(CmdFillet); }
    else if (cmd == L"NEUTRAL" || cmd == L"PASIF" || cmd == L"ESC") { executeCommand(CmdNeutral); }
    else if (cmd == L"ZOOM" || cmd == L"Z") { executeCommand(CmdZoomWindow); }
    else if (cmd == L"ZOOMEXTENTS" || cmd == L"ZE") { executeCommand(CmdZoomExtents); }
    else if (cmd == L"3D" || cmd == L"VIEW3D" || cmd == L"3B") { executeCommand(CmdView3D); }
    else if (cmd == L"TOP" || cmd == L"ÜST") { executeCommand(CmdViewTop); }
    else if (cmd == L"BOTTOM" || cmd == L"ALT") { executeCommand(CmdViewBottom); }
    else if (cmd == L"FRONT" || cmd == L"ÖN") { executeCommand(CmdViewFront); }
    else if (cmd == L"BACK" || cmd == L"ARKA") { executeCommand(CmdViewBack); }
    else if (cmd == L"LEFT" || cmd == L"SOL") { executeCommand(CmdViewLeft); }
    else if (cmd == L"RIGHT" || cmd == L"SAĞ") { executeCommand(CmdViewRight); }
    else if (cmd == L"ISO" || cmd == L"ISOMETRIC") { executeCommand(CmdViewIsometric); }
    else if (cmd == L"OSNAP" || cmd == L"F3") { executeCommand(CmdOsnap); }
    else if (cmd == L"GRID" || cmd == L"F9") { executeCommand(CmdGridSnap); }
    else if (cmd == L"DYN" || cmd == L"DYNAMIC") { executeCommand(CmdDynamicInput); }
    else if (cmd == L"POLAR" || cmd == L"F10") { executeCommand(CmdPolarTracking); }
    else if (cmd == L"ORTHO" || cmd == L"F8") { orthoEnabled_ = !orthoEnabled_; if (orthoEnabled_) polarTrackingEnabled_ = false; updateControls(); invalidateCanvas(); }
    else if (cmd == L"PERF" || cmd == L"F11") { performanceOverlayEnabled_ = !performanceOverlayEnabled_; updateControls(); invalidateCanvas(); }
    else if (cmd == L"NEW") { executeCommand(CmdNew); }
    else if (cmd == L"OPEN") { executeCommand(CmdOpen); }
    else if (cmd == L"SAVE") { executeCommand(CmdSave); }
    else if (cmd == L"DXFIN" || cmd == L"IMPORT" || cmd == L"DXF AÇ") { executeCommand(CmdImportDxf); }
    else if (cmd == L"DXFOUT" || cmd == L"EXPORT" || cmd == L"DXF YAZ") { executeCommand(CmdExportDxf); }
    else if (cmd == L"CUBE" || cmd == L"KÜP") { executeCommand(CmdCube); }
    else if (cmd == L"PYRAMID" || cmd == L"PİRAMİT") { executeCommand(CmdPyramid); }
    else if (cmd == L"WORKPLANE" || cmd == L"DÜZLEM" || cmd == L"WP") { executeCommand(CmdWorkPlane); }
    else if (cmd == L"UCS" || cmd == L"KOS") { executeCommand(CmdUcsCommand); }
    else if (cmd == L"UCSWORLD" || cmd == L"DÜNYA") { executeCommand(CmdUcsWorld); }
    else if (cmd == L"UCSVIEW" || cmd == L"GÖRÜNÜŞ") { executeCommand(CmdUcsView); }
    else if (cmd == L"UCS3P" || cmd == L"3NOKTA") { executeCommand(CmdUcs3Point); }
    else if (cmd == L"CLEAR" || cmd == L"TEMİZLE") { pushUndoSnapshot(); document_.clear(); selectedModels_.clear(); cancelDrawing(); updateControls(); invalidateCanvas(); }
    else if (cmd == L"HELP" || cmd == L"?") {
        std::wstring help = L"Komutlar: LINE, POLYLINE, RECTANGLE, CIRCLE, 3DFACE, MOVE, COPY, OFFSET, "
            L"MIRROR, DELETE, LINEARARRAY, POLARARRAY, TRIM, EXTEND, FILLET, ZOOM, ZOOMEXTENTS, "
            L"ORTHO, OSNAP, GRID, POLAR, DYN, PERF, NEW, OPEN, SAVE, DXFIN, DXFOUT, "
            L"CUBE, PYRAMID, WORKPLANE, UCS, NEUTRAL, CLEAR, TOP, BOTTOM, FRONT, BACK, LEFT, RIGHT, ISO, 3D, HELP";
        SetWindowTextW(commandBarPrompt_, L" KOMUT YARDIM: ");
        SetWindowTextW(commandBar_, help.c_str());
        SendMessageW(commandBar_, EM_SETSEL, 0, -1);
        SetFocus(commandBar_);
        return;
    } else {
        SetWindowTextW(commandBarPrompt_, L" BİLİNMEYEN KOMUT: ");
        return;
    }
    SetWindowTextW(commandBarPrompt_, L" KOMUT: ");
    SetFocus(commandBar_);
}

void Application::showError(const wchar_t* action, const std::exception& error) const {
    std::wstring message(action); message += L".\n\n";
    const std::string detail = error.what(); message.append(detail.begin(), detail.end());
    MessageBoxW(window_, message.c_str(), L"Model Maker", MB_OK | MB_ICONERROR);
}

void Application::cycleNodeConstraint() {
    if (!hover_) { MessageBeep(MB_ICONWARNING); return; }
    const auto& pt = hover_->point;
    auto existing = document_.getNodeConstraint(pt);
    if (!existing) {
        document_.setNodeConstraint(pt, NodeConstraint{});  // Start with Pinned
        existing = NodeConstraint{};
    }
    // Cycle: Free → Pinned → Fixed → Free
    if (existing->isFree()) {
        existing->setPinned();
        document_.setNodeConstraint(pt, *existing);
    } else if (existing->isPinned()) {
        existing->setFixed();
        document_.setNodeConstraint(pt, *existing);
    } else {
        // Fixed or custom → Free
        document_.setNodeConstraint(pt, NodeConstraint{});
    }
    updateStatus(); invalidateCanvas();
}

void Application::clearNodeConstraintsAction() {
    document_.clearNodeConstraints();
    selectedNodeConstraints_.clear();
    updateStatus(); invalidateCanvas();
}

static Vec3 parseJointKey(const std::string& key) {
    Vec3 pt{};
    std::sscanf(key.c_str(), "%lf,%lf,%lf", &pt.x, &pt.y, &pt.z);
    return pt;
}

void Application::setSelectedNodeConstraintsFixed() {
    for (const auto& key : selectedNodeConstraints_) {
        NodeConstraint c; c.setFixed();
        document_.setNodeConstraint(parseJointKey(key), c);
    }
    updateStatus(); invalidateCanvas();
}

void Application::setSelectedNodeConstraintsPinned() {
    for (const auto& key : selectedNodeConstraints_) {
        auto c = NodeConstraint{};
        c.setPinned();
        document_.setNodeConstraint(parseJointKey(key), c);
    }
    updateStatus(); invalidateCanvas();
}

void Application::setSelectedNodeConstraintsFree() {
    for (const auto& key : selectedNodeConstraints_)
        document_.setNodeConstraint(parseJointKey(key), NodeConstraint{});
    updateStatus(); invalidateCanvas();
}

void Application::completeNodeWindowSelection(int x, int y, int vw, int vh) {
    const int left   = std::min(static_cast<int>(nodeSelectionFirstCorner_->x), x);
    const int top    = std::min(static_cast<int>(nodeSelectionFirstCorner_->y), y);
    const int right  = std::max(static_cast<int>(nodeSelectionFirstCorner_->x), x);
    const int bottom = std::max(static_cast<int>(nodeSelectionFirstCorner_->y), y);
    nodeSelectionFirstCorner_.reset();

    if (!(GetKeyState(VK_SHIFT) & 0x8000))
        selectedNodeConstraints_.clear();

    for (const auto& [key, constraint] : document_.nodeConstraints()) {
        Vec3 pt{}; std::sscanf(key.c_str(), "%lf,%lf,%lf", &pt.x, &pt.y, &pt.z);
        const Vec2 proj = camera_.project(pt, vw, vh);
        if (proj.x >= left && proj.x <= right && proj.y >= top && proj.y <= bottom)
            selectedNodeConstraints_.insert(key);
    }
    updateStatus(); invalidateCanvas();
}

void Application::applyNodeDofFromCombo() {
    if (selectedNodeConstraints_.empty()) { MessageBeep(MB_ICONWARNING); return; }
    const auto sel = SendMessageW(nodeDofCombo_, CB_GETCURSEL, 0, 0);
    for (const auto& key : selectedNodeConstraints_) {
        NodeConstraint c;
        if (sel == 0) c.setFixed();
        else if (sel == 1) c.setPinned();
        // sel == 2: leave as free (default)
        document_.setNodeConstraint(parseJointKey(key), c);
    }
    updateStatus(); invalidateCanvas();
}

} // namespace mm
