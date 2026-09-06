#include "model_maker/application.hpp"
#include "model_maker/dxf.hpp"
#ifdef MM_HAS_OCC
#include "model_maker/occ_geometry.hpp"
#endif
#ifdef MM_HAS_OCC
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs.hxx>
#endif
#include "model_maker/view_cube.hpp"

#include <QComboBox>

#include <commdlg.h>
#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <exception>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace mm {
namespace {
constexpr int ribbonHeight = RibbonLayout::height;
constexpr int statusHeight = 27;
constexpr wchar_t className[] = L"ModelMakerWindow";
constexpr wchar_t canvasClassName[] = L"ModelMakerCanvas";

enum CommandId {
    CmdNew = 100, CmdOpen, CmdSave, CmdImportDxf, CmdExportDxf,
    CmdLine = 200, CmdPolyline, CmdRectangle, CmdCircle, CmdFace3D,
    CmdCube = 300, CmdPyramid, CmdCylinder, CmdResetView, CmdView3D, CmdWorkPlane, CmdZoomExtents, CmdZoomWindow,
    CmdVisualStyle, CmdStandardView, CmdWireframe = 310, CmdSolid, CmdTransparent,
    CmdViewFront = 320, CmdViewBack, CmdViewLeft, CmdViewRight, CmdViewIsometric,
    CmdViewTop, CmdViewBottom,
    CmdOsnap = 400, CmdGridSnap, CmdDynamicInput, CmdSnapSettings, CmdPolarTracking,
    CmdMove = 500, CmdCopy, CmdOffset, CmdMirror, CmdDelete, CmdLinearArray, CmdPolarArray,
    CmdTrim, CmdExtend, CmdNeutral, CmdFillet,
    CmdTabFile = 600, CmdTabDrawing, CmdTabModify, CmdTabView, CmdTabAids,
    CmdDxfProgress = 700, CmdSnapTypeFirst = 720,
    CmdLayerCombo = 800, CmdColorCombo, CmdLineTypeCombo, CmdProfileCombo,
    CmdProfileButton, CmdProfilePopup,
    CmdProperties = 820, CmdPropsSearch = 821, CmdPropsList = 822, CmdPropsFilter = 823,
    CmdFilterPopup = 824, CmdFilterFind = 825, CmdFilterSelect = 826,
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

Application::Application(HINSTANCE instance, HWND /*parentCanvas*/) : instance_(instance) {
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
    iconFont_ = CreateFontW(-18, 0, 0, 0, FW_LIGHT, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Symbol");

    const auto addTab = [&](const wchar_t* text, int id, DWORD extra = 0) {
        HWND tab = createButton(text, id, 0, 0, 42, 17,
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
        HWND button = createButton(text, id, 0, 0, 40, 40, style | BS_MULTILINE);
        ribbonCommandButtons_.push_back(button);
        return button;
    };

    addCommand(L"＋", CmdNew);
    addCommand(L"▣", CmdOpen);
    addCommand(L"▤", CmdSave);
    addCommand(L"⇩", CmdImportDxf);
    addCommand(L"⇧", CmdExportDxf);

    lineButton_ = addCommand(L"╱", CmdLine, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    polylineButton_ = addCommand(L"⌁", CmdPolyline, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    rectangleButton_ = addCommand(L"□", CmdRectangle, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    circleButton_ = addCommand(L"○", CmdCircle, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    face3DButton_ = addCommand(L"▱", CmdFace3D, BS_AUTOCHECKBOX | BS_PUSHLIKE);

    addCommand(L"◇", CmdCube);
    addCommand(L"△", CmdPyramid);
    addCommand(L"⬤", CmdCylinder);
    addCommand(L"⌂", CmdResetView);
    view3DButton_ = addCommand(L"3B", CmdView3D, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    workPlaneButton_ = addCommand(L"▱", CmdWorkPlane, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    addCommand(L"⤢", CmdZoomExtents);
    zoomWindowButton_ = addCommand(L"⌗", CmdZoomWindow, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    visualStyleButton_ = addCommand(L"◇", CmdVisualStyle);
    standardViewButton_ = addCommand(L"▦", CmdStandardView);
    addCommand(L"📋", CmdProperties, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    addCommand(L"🔍", CmdFilterPopup, BS_PUSHBUTTON);

    snapButton_ = addCommand(L"◎", CmdOsnap, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    gridSnapButton_ = addCommand(L"#", CmdGridSnap, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    dynamicInputButton_ = addCommand(L"123", CmdDynamicInput, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    snapSettingsButton_ = addCommand(L"☑", CmdSnapSettings, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    polarTrackingButton_ = addCommand(L"∠", CmdPolarTracking, BS_AUTOCHECKBOX | BS_PUSHLIKE);

    neutralButton_ = addCommand(L"↖", CmdNeutral, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    moveButton_ = addCommand(L"↔", CmdMove, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    copyButton_ = addCommand(L"⧉", CmdCopy, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    offsetButton_ = addCommand(L"⇶", CmdOffset, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    mirrorButton_ = addCommand(L"◁│▷", CmdMirror, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    deleteButton_ = addCommand(L"✕", CmdDelete, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    linearArrayButton_ = addCommand(L"▦", CmdLinearArray, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    polarArrayButton_ = addCommand(L"◌", CmdPolarArray, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    trimButton_ = addCommand(L"✂", CmdTrim, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    extendButton_ = addCommand(L"↗", CmdExtend, BS_AUTOCHECKBOX | BS_PUSHLIKE);
    filletButton_ = addCommand(L"⌒", CmdFillet, BS_AUTOCHECKBOX | BS_PUSHLIKE);

    canvas_ = CreateWindowExW(WS_EX_CLIENTEDGE, canvasClassName, nullptr,
                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS,
                              0, ribbonHeight, 800, 600, window_, nullptr, instance_, this);
    status_ = CreateWindowExW(0, L"STATIC", nullptr,
                              WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_LEFT | SS_CENTERIMAGE,
                              0, 700, 1000, statusHeight, window_, nullptr, instance_, nullptr);
    SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    // Tooltip control for all ribbon buttons
    tooltipWnd_ = CreateWindowExW(0, TOOLTIPS_CLASSW, nullptr,
                                   WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                                   CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                   window_, nullptr, instance_, nullptr);
    if (tooltipWnd_) {
        SendMessageW(tooltipWnd_, TTM_SETMAXTIPWIDTH, 0, 300);
        // Register every ribbon button with the tooltip
        std::vector<std::pair<HWND, std::wstring>> tooltips;
        auto regTT = [&](HWND btn, const wchar_t* tip) {
            if (btn) tooltips.push_back({btn, tip});
        };
        regTT(GetDlgItem(window_, CmdNew), L"Yeni");
        regTT(GetDlgItem(window_, CmdOpen), L"Aç");
        regTT(GetDlgItem(window_, CmdSave), L"Kaydet");
        regTT(GetDlgItem(window_, CmdImportDxf), L"DXF Aç");
        regTT(GetDlgItem(window_, CmdExportDxf), L"DXF Yaz");
        regTT(GetDlgItem(window_, CmdLine), L"Çizgi");
        regTT(GetDlgItem(window_, CmdPolyline), L"Polyline");
        regTT(GetDlgItem(window_, CmdRectangle), L"Dikdörtgen");
        regTT(GetDlgItem(window_, CmdCircle), L"Daire");
        regTT(GetDlgItem(window_, CmdFace3D), L"3DFACE");
        regTT(GetDlgItem(window_, CmdCube), L"Küp");
        regTT(GetDlgItem(window_, CmdPyramid), L"Piramit");
        regTT(GetDlgItem(window_, CmdCylinder), L"Silindir");
        regTT(GetDlgItem(window_, CmdResetView), L"Sıfırla");
        regTT(GetDlgItem(window_, CmdView3D), L"2B / 3B");
        regTT(GetDlgItem(window_, CmdWorkPlane), L"Düzlem");
        regTT(GetDlgItem(window_, CmdZoomExtents), L"Extents");
        regTT(GetDlgItem(window_, CmdZoomWindow), L"Pencere");
        regTT(GetDlgItem(window_, CmdVisualStyle), L"Wireframe");
        regTT(GetDlgItem(window_, CmdStandardView), L"Görünüş");
        regTT(GetDlgItem(window_, CmdProperties), L"Özellikler");
        regTT(GetDlgItem(window_, CmdFilterPopup), L"Filtrele");
        regTT(GetDlgItem(window_, CmdOsnap), L"OSNAP F3");
        regTT(GetDlgItem(window_, CmdGridSnap), L"Grid F9");
        regTT(GetDlgItem(window_, CmdDynamicInput), L"Dinamik");
        regTT(GetDlgItem(window_, CmdSnapSettings), L"Snap Türleri");
        regTT(GetDlgItem(window_, CmdPolarTracking), L"Polar F10");
        regTT(GetDlgItem(window_, CmdNeutral), L"Pasif");
        regTT(GetDlgItem(window_, CmdMove), L"Taşı");
        regTT(GetDlgItem(window_, CmdCopy), L"Kopyala");
        regTT(GetDlgItem(window_, CmdOffset), L"Ofset");
        regTT(GetDlgItem(window_, CmdMirror), L"Ayna");
        regTT(GetDlgItem(window_, CmdDelete), L"Sil");
        regTT(GetDlgItem(window_, CmdLinearArray), L"Doğrusal");
        regTT(GetDlgItem(window_, CmdPolarArray), L"Dairesel");
        regTT(GetDlgItem(window_, CmdTrim), L"Trim");
        regTT(GetDlgItem(window_, CmdExtend), L"Extend");
        regTT(GetDlgItem(window_, CmdFillet), L"Fillet");
        for (const auto& [btn, tip] : tooltips) {
            TOOLINFOW ti{};
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            ti.hwnd = window_;
            ti.uId = reinterpret_cast<UINT_PTR>(btn);
            ti.lpszText = const_cast<wchar_t*>(tip.c_str());
            SendMessageW(tooltipWnd_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
        }
    }
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
        snapTypeCheckboxes_[i] = CreateWindowExW(0, L"BUTTON", snapChoices[i].label,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0, 0, 170, 27, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(CmdSnapTypeFirst + static_cast<int>(i))),
            instance_, nullptr);
        SendMessageW(snapTypeCheckboxes_[i], WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
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
    profileCombo_ = createCombo(CmdProfileCombo, 150);
    styleLabels_[3] = createStyleLabel(L"Profil");
    profileButton_ = CreateWindowExW(0, L"BUTTON", L"Profil: KKR30*3",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 150, 24, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(CmdProfileButton)),
        instance_, nullptr);
    if (profileButton_) {
        SendMessageW(profileButton_, WM_SETFONT,
                     reinterpret_cast<WPARAM>(uiFont_), TRUE);
    }
    // Popup liste: WS_POPUP — ribbon boyasinin altinda kalma ihtimali yok.
    profilePopup_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"LISTBOX", nullptr,
        WS_POPUP | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        0, 0, 240, 420, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(CmdProfilePopup)),
        instance_, nullptr);
    if (profilePopup_) {
        SendMessageW(profilePopup_, WM_SETFONT,
                     reinterpret_cast<WPARAM>(uiFont_), TRUE);
    }
    // Z-order: profil combosu ustte kalsin (ozel boyalı ribbonun altinda
    // kalma ihtimaline karsi).
    if (profileCombo_) SetWindowPos(profileCombo_, HWND_TOP, 0, 0, 0, 0,
                                    SWP_NOMOVE | SWP_NOSIZE);
    // Ebeveyn pencere cocuklarin uzerine boyamasin (WS_CLIPCHILDREN).
    SetWindowLongPtrW(window_, GWL_STYLE,
                      GetWindowLongPtrW(window_, GWL_STYLE) | WS_CLIPCHILDREN);
    for (const auto& choice : colorChoices)
        SendMessageW(colorCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.label));
    for (const auto& choice : lineTypeChoices)
        SendMessageW(lineTypeCombo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.label));
    SendMessageW(colorCombo_, CB_SETCURSEL, 0, 0);
    SendMessageW(lineTypeCombo_, CB_SETCURSEL, 0, 0);
    refreshLayerCombo();
    refreshProfileCombo();

    // Properties panel (floating popup window with filter toggle)
    propsPanel_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"Ozellikler",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        100, 100, 320, 400, window_, nullptr, instance_, nullptr);
    propsSearch_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
        WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
        8, 8, 288, 25, propsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(CmdPropsSearch)), instance_, nullptr);
    SendMessageW(propsSearch_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    SendMessageW(propsSearch_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Ozellik ara..."));
    ShowWindow(propsSearch_, SW_HIDE); // hidden until filter toggled
    propsList_ = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        8, 8, 288, 300, propsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(CmdPropsList)), instance_, nullptr);
    SendMessageW(propsList_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    ListView_SetExtendedListViewStyle(propsList_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    ListView_SetBkColor(propsList_, RGB(57, 68, 84));
    ListView_SetTextBkColor(propsList_, RGB(57, 68, 84));
    ListView_SetTextColor(propsList_, RGB(235, 240, 247));
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = const_cast<wchar_t*>(L"Ozellik");
    col.cx = 135;
    ListView_InsertColumn(propsList_, 0, &col);
    col.pszText = const_cast<wchar_t*>(L"Deger");
    col.cx = 135;
    ListView_InsertColumn(propsList_, 1, &col);
    // Toolbar: filter toggle + close buttons at top of client area
    propsClose_ = CreateWindowExW(0, L"BUTTON", L"\u00d7",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        252, 8, 28, 25, propsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(CmdProperties)), instance_, nullptr);
    SendMessageW(propsClose_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    propsFilterBtn_ = CreateWindowExW(0, L"BUTTON", L"\U0001f50d",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        220, 8, 28, 25, propsPanel_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(CmdPropsFilter)), instance_, nullptr);
    SendMessageW(propsFilterBtn_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    // Subclass popup to hide on close instead of destroy
    SetWindowLongPtrW(propsPanel_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetWindowLongPtrW(propsPanel_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(propsWndProc));

    // Filter popup (separate window for entity property filtering)
    filterPopup_ = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"Filtrele",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
        150, 150, 300, 230, window_, nullptr, instance_, nullptr);
    SetWindowLongPtrW(filterPopup_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetWindowLongPtrW(filterPopup_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(propsWndProc));
    // Labels
    auto makeLabel = [&](const wchar_t* text, int x, int y, int w, int h) {
        HWND lbl = CreateWindowExW(0, L"STATIC", text,
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            x, y, w, h, filterPopup_, nullptr, instance_, nullptr);
        SendMessageW(lbl, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        return lbl;
    };
    makeLabel(L"Katman:", 10, 12, 60, 25);
    makeLabel(L"Renk:", 10, 50, 60, 25);
    makeLabel(L"Uzunluk:", 10, 88, 60, 25);
    // Edit boxes
    filterLayerEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        75, 12, 180, 25, filterPopup_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(CmdFilterPopup+1)), instance_, nullptr);
    SendMessageW(filterLayerEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    filterColorEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        75, 50, 180, 25, filterPopup_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(CmdFilterPopup+2)), instance_, nullptr);
    SendMessageW(filterColorEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    filterLengthEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        75, 88, 180, 25, filterPopup_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(CmdFilterPopup+3)), instance_, nullptr);
    SendMessageW(filterLengthEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    // Buttons
    filterBtnFind_ = CreateWindowExW(0, L"BUTTON", L"Bul",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        30, 140, 100, 32, filterPopup_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(CmdFilterFind)), instance_, nullptr);
    SendMessageW(filterBtnFind_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    filterBtnSelect_ = CreateWindowExW(0, L"BUTTON", L"Sec",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        145, 140, 100, 32, filterPopup_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(CmdFilterSelect)), instance_, nullptr);
    SendMessageW(filterBtnSelect_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    ShowWindow(filterPopup_, SW_HIDE);

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

    int tabX = 8;
    for (std::size_t i = 0; i < ribbonTabButtons_.size(); ++i) {
        wchar_t label[64]{};
        GetWindowTextW(ribbonTabButtons_[i], label, 63);
        HDC dc = GetDC(ribbonTabButtons_[i]);
        HGDIOBJ old = SelectObject(dc, uiFont_);
        SIZE sz{};
        GetTextExtentPoint32W(dc, label, static_cast<int>(wcslen(label)), &sz);
        SelectObject(dc, old);
        ReleaseDC(ribbonTabButtons_[i], dc);
        const int tabW = sz.cx + 24; // text width + generous padding
        const int tabH = sz.cy + 12;
        MoveWindow(ribbonTabButtons_[i], tabX, 4, tabW, tabH, TRUE);
        tabX += tabW + 4;
    }
    const auto geometry = RibbonLayout::layout(activeRibbonTab_, width);
    for (const auto& item : geometry.commandButtons) {
        if (HWND button = GetDlgItem(window_, item.commandId)) {
            const auto& r = item.rect;
            MoveWindow(button, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
        }
    }
    const int commandsRight = geometry.commandButtons.empty() ? 350 : geometry.commandButtons.back().rect.right;
    // 4. sutun (Profil) icin sag bosluk buyutuldu — dar pencerelerde
    // combo ekran disina tasiyordu (gorunmez dropdown hatasi).
    const int styleX = std::min(std::max(commandsRight + 24, 365), std::max(365, width - 570));
    const std::array<int, 4> xPositions{{styleX, styleX + 148, styleX + 268, styleX + 388}};
    const std::array<int, 4> widths{{140, 110, 110, 150}};
    for (std::size_t i = 0; i < styleLabels_.size(); ++i) {
        if (styleLabels_[i]) MoveWindow(styleLabels_[i], xPositions[i], 65, widths[i], 17, TRUE);
    }
    if (layerCombo_) MoveWindow(layerCombo_, xPositions[0], 83, widths[0], 220, TRUE);
    if (colorCombo_) MoveWindow(colorCombo_, xPositions[1], 83, widths[1], 220, TRUE);
    if (lineTypeCombo_) MoveWindow(lineTypeCombo_, xPositions[2], 83, widths[2], 220, TRUE);
    if (profileCombo_) MoveWindow(profileCombo_, xPositions[3], 83, widths[3], 220, TRUE);
    if (profileButton_) MoveWindow(profileButton_, xPositions[3], 81, widths[3], 25, TRUE);

#ifndef NDEBUG
    // --- Overlap validation rule ---
    // Collect all ribbon child rects and verify no two intersect.
    // This catches layout bugs at window resize / tab switch.
    auto rectsOverlap = [](RECT a, RECT b) -> bool {
        return a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top;
    };
    std::vector<std::pair<HWND, RECT>> ribbonRects;
    auto addRect = [&](HWND hwnd, POINT offset) {
        if (!hwnd) return;
        RECT r{};
        if (GetWindowRect(hwnd, &r)) {
            MapWindowPoints(HWND_DESKTOP, window_, reinterpret_cast<POINT*>(&r), 2);
            r.left += offset.x; r.right += offset.x;
            r.top += offset.y; r.bottom += offset.y;
            ribbonRects.push_back({hwnd, r});
        }
    };
    POINT zero{};
    for (HWND btn : ribbonTabButtons_) addRect(btn, zero);
    for (const auto& item : geometry.commandButtons) {
        if (HWND btn = GetDlgItem(window_, item.commandId)) addRect(btn, zero);
    }
    for (std::size_t i = 0; i < styleLabels_.size(); ++i) addRect(styleLabels_[i], zero);
    addRect(layerCombo_, zero);
    addRect(colorCombo_, zero);
    addRect(lineTypeCombo_, zero);
    for (std::size_t i = 0; i < ribbonRects.size(); ++i) {
        for (std::size_t j = i + 1; j < ribbonRects.size(); ++j) {
            if (rectsOverlap(ribbonRects[i].second, ribbonRects[j].second)) {
                wchar_t buf[256]{};
                swprintf(buf, 255, L"OVERLAP: %p vs %p at (%d,%d,%d,%d) & (%d,%d,%d,%d)\n",
                         ribbonRects[i].first, ribbonRects[j].first,
                         ribbonRects[i].second.left, ribbonRects[i].second.top,
                         ribbonRects[i].second.right, ribbonRects[i].second.bottom,
                         ribbonRects[j].second.left, ribbonRects[j].second.top,
                         ribbonRects[j].second.right, ribbonRects[j].second.bottom);
                OutputDebugStringW(buf);
            }
        }
    }
    // Command buttons must not extend below ribbon height
    for (const auto& item : geometry.commandButtons) {
        if (item.rect.bottom > RibbonLayout::height) {
            wchar_t buf[128]{};
            swprintf(buf, 127, L"OVERFLOW: cmd %d bottom %d > ribbon %d\n",
                     item.commandId, item.rect.bottom, RibbonLayout::height);
            OutputDebugStringW(buf);
        }
    }
#endif

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

void Application::updatePropertiesPanel() {
    if (!propsPanel_) return;
    if (propsPanelOpen_) {
        ShowWindow(propsPanel_, SW_SHOW);
        SetWindowPos(propsPanel_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    } else {
        ShowWindow(propsPanel_, SW_HIDE);
        return;
    }

    if (!propsPanelOpen_ || !propsList_) return;
    // Layout: list starts at top if filter hidden, or below filter box if shown
    const bool filterVisible = IsWindowVisible(propsSearch_);
    const int listTop = filterVisible ? 38 : 8;
    RECT rc; GetClientRect(propsPanel_, &rc);
    if (filterVisible) MoveWindow(propsSearch_, 8, 8, std::max(1, static_cast<int>(rc.right) - 16), 25, TRUE);
    MoveWindow(propsList_, 8, listTop, std::max(1, static_cast<int>(rc.right) - 16), std::max(1, static_cast<int>(rc.bottom) - listTop - 8), TRUE);

    ListView_DeleteAllItems(propsList_);

    // If nothing selected, show a hint
    if (selectedModels_.empty()) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.pszText = const_cast<wchar_t*>(L"Secili obje yok");
        item.iItem = 0;
        ListView_InsertItem(propsList_, &item);
        return;
    }

    // Gather properties from first selected entity
    const auto& model = document_.models()[selectedModels_.front()];
    const auto& props = model.properties();
    const auto& verts = model.vertices();

    struct Prop { const wchar_t* name; std::wstring value; };
    std::vector<Prop> properties;

    // Entity type
    if (model.isFace3D()) properties.push_back({L"Tip", L"3DFACE"});
    else if (model.isPointEntity()) properties.push_back({L"Tip", L"NOKTA"});
    else properties.push_back({L"Tip", L"CIZGI"});

    // Basic properties
    properties.push_back({L"Katman", utf8ToWide(props.layer)});
    properties.push_back({L"Renk indeksi", std::to_wstring(props.colorIndex)});
    properties.push_back({L"Cizgi tipi", utf8ToWide(props.lineType)});
    properties.push_back({L"Cizgi kalinligi", std::to_wstring(props.lineWeight)});
    if (props.trueColor)
        properties.push_back({L"Gercek renk", std::to_wstring(*props.trueColor)});

    // Geometry
    if (verts.size() >= 2) {
        auto fmt = [](const Vec3& v) {
            return std::to_wstring(v.x) + L", " + std::to_wstring(v.y) + L", " + std::to_wstring(v.z);
        };
        properties.push_back({L"Baslangic", fmt(verts.front())});
        properties.push_back({L"Bitis", fmt(verts.back())});
        if (verts.size() == 2) {
            double dx = verts[1].x - verts[0].x;
            double dy = verts[1].y - verts[0].y;
            double dz = verts[1].z - verts[0].z;
            double len = std::sqrt(dx*dx + dy*dy + dz*dz);
            properties.push_back({L"Uzunluk", std::to_wstring(len)});
        }
    }

    // Filter: search across ALL models and select matching ones
    wchar_t filter[256]{};
    if (propsSearch_) GetWindowTextW(propsSearch_, filter, 256);
    std::wstring filterStr(filter);
    
    if (!filterStr.empty()) {
        // Search all entities for matching properties
        std::wstring lowerFilter = filterStr;
        for (auto& c : lowerFilter) c = std::towlower(c);
        
        std::vector<std::size_t> matched;
        for (std::size_t idx = 0; idx < document_.models().size(); ++idx) {
            const auto& m = document_.models()[idx];
            const auto& p = m.properties();
            // Check layer name
            std::wstring layer = utf8ToWide(p.layer);
            for (auto& c : layer) c = std::towlower(c);
            if (layer.find(lowerFilter) != std::wstring::npos) {
                matched.push_back(idx); continue;
            }
            // Check color index
            if (std::to_wstring(p.colorIndex).find(lowerFilter) != std::wstring::npos) {
                matched.push_back(idx); continue;
            }
            // Check line type
            std::wstring ltype = utf8ToWide(p.lineType);
            for (auto& c : ltype) c = std::towlower(c);
            if (ltype.find(lowerFilter) != std::wstring::npos) {
                matched.push_back(idx); continue;
            }
            // Check length for 2-vertex models
            const auto& v = m.vertices();
            if (v.size() == 2) {
                double dx = v[1].x - v[0].x, dy = v[1].y - v[0].y, dz = v[1].z - v[0].z;
                double len = std::sqrt(dx*dx + dy*dy + dz*dz);
                std::wstring lenStr = std::to_wstring(len);
                // Truncate for matching
                if (lenStr.find(lowerFilter) != std::wstring::npos) {
                    matched.push_back(idx); continue;
                }
            }
            // Also check coordinates
            for (const auto& vert : v) {
                std::wstring coord = std::to_wstring(vert.x) + L"," + std::to_wstring(vert.y);
                if (coord.find(lowerFilter) != std::wstring::npos) {
                    matched.push_back(idx); goto next_model;
                }
            }
            next_model:;
        }
        selectedModels_ = matched;
        
        // Show match count and first entity details
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.pszText = const_cast<wchar_t*>(L"Eslesen obje");
        item.iItem = 0;
        ListView_InsertItem(propsList_, &item);
        ListView_SetItemText(propsList_, 0, 1, const_cast<wchar_t*>(std::to_wstring(matched.size()).c_str()));
        
        if (!matched.empty()) {
            // Append first matched entity's properties
            goto show_properties;
        }
        return;
    }
    
    show_properties:
    int row = ListView_GetItemCount(propsList_);
    for (const auto& prop : properties) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = row;
        item.iSubItem = 0;
        item.pszText = const_cast<wchar_t*>(prop.name);
        ListView_InsertItem(propsList_, &item);
        ListView_SetItemText(propsList_, row, 1, const_cast<wchar_t*>(prop.value.c_str()));
        ++row;
    }
}

void Application::activateRibbonTab(RibbonTab tab) {
    activeRibbonTab_ = tab;
    for (HWND button : ribbonCommandButtons_) ShowWindow(button, SW_HIDE);
    const bool showProperties = tab == RibbonTab::Drawing || tab == RibbonTab::Modify;
    for (std::size_t i = 0; i < styleLabels_.size(); ++i)
        if (styleLabels_[i]) ShowWindow(styleLabels_[i],
            showProperties || i == 3 ? SW_SHOW : SW_HIDE);
    if (layerCombo_) ShowWindow(layerCombo_, showProperties ? SW_SHOW : SW_HIDE);
    if (colorCombo_) ShowWindow(colorCombo_, showProperties ? SW_SHOW : SW_HIDE);
    if (lineTypeCombo_) ShowWindow(lineTypeCombo_, showProperties ? SW_SHOW : SW_HIDE);
    // Native controls remain as a standalone fallback. The shipped Qt shell
    // uses its own always-visible profile toolbar.
    if (profileCombo_) ShowWindow(profileCombo_, SW_SHOW);
    if (profileButton_) ShowWindow(profileButton_, SW_SHOW);
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
    RECT commandBand{0, 34, client.right, ribbonHeight};
    FillRect(dc, &commandBand, commandBrush);
    RECT bandLine{0, 33, client.right, 34};
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
        // Icon-only button: center the text in the full button rect
        HGDIOBJ old = SelectObject(item.hDC, iconFont_);
        DrawTextW(item.hDC, text.c_str(), -1, &content,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
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

static void trimExtendLog(const std::wstring& message) noexcept {
    try {
        std::ofstream log("model-maker-trim.log", std::ios::app);
        log << "[" << GetTickCount() << "] " << wideToUtf8(message) << "\n";
    } catch (...) {
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

LRESULT CALLBACK Application::propsWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* app = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_CLOSE) {
        if (app) {
            if (window == app->filterPopup_) {
                app->filterPopupOpen_ = false;
                ShowWindow(app->filterPopup_, SW_HIDE);
            } else {
                app->propsPanelOpen_ = false;
                app->updatePropertiesPanel();
            }
        }
        return 0;
    }
    if (message == WM_COMMAND && app) {
        return SendMessageW(app->window_, WM_COMMAND, wParam, lParam);
    }
    if (message == WM_CTLCOLORBTN || message == WM_CTLCOLOREDIT || message == WM_CTLCOLORSTATIC) {
        // Forward to main window for dark theme handling
        HWND target = app ? app->window_ : GetParent(window);
        return SendMessageW(target, message, wParam, lParam);
    }
    if (message == WM_CTLCOLORSTATIC) {
        // Dark background for popup static controls
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, RGB(226, 233, 241));
        SetBkColor(dc, RGB(44, 53, 67));
        return reinterpret_cast<LRESULT>(app ? app->panelBrush_ : GetStockObject(DC_BRUSH));
    }
    if (message == WM_ERASEBKGND) {
        // Dark background for popup window
        HDC dc = reinterpret_cast<HDC>(wParam);
        RECT rc; GetClientRect(window, &rc);
        HBRUSH bg = CreateSolidBrush(RGB(44, 53, 67));
        FillRect(dc, &rc, bg);
        DeleteObject(bg);
        return TRUE;
    }
    if (message == WM_SIZE && app && app->propsList_) {
        RECT rc; GetClientRect(window, &rc);
        const int cw = std::max(1L, static_cast<long>(rc.right));
        // Reposition toolbar buttons
        if (app->propsClose_) MoveWindow(app->propsClose_, cw - 36, 4, 28, 25, TRUE);
        if (app->propsFilterBtn_) MoveWindow(app->propsFilterBtn_, cw - 68, 4, 28, 25, TRUE);
        // Reposition search and list
        const bool filterVisible = IsWindowVisible(app->propsSearch_);
        const int listTop = filterVisible ? 38 : 34;
        if (filterVisible) MoveWindow(app->propsSearch_, 8, 8, std::max(1, cw - 16), 25, TRUE);
        MoveWindow(app->propsList_, 8, listTop, std::max(1, cw - 16), std::max(1L, static_cast<long>(rc.bottom) - listTop - 4), TRUE);
    }
    return DefWindowProcW(window, message, wParam, lParam);
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
    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, RGB(226, 233, 241));
        SetBkColor(dc, RGB(31, 38, 48));
        return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    }
    case WM_COMMAND:
        // Handle the profile button before the generic BN_CLICKED path. The
        // old ordering swallowed this command in executeCommand(), so even a
        // visible native fallback button could never open its picker.
        if (HIWORD(wParam) == BN_CLICKED && LOWORD(wParam) == CmdProfileButton) {
            if (profilePickerCallback_) profilePickerCallback_();
            else toggleProfilePopup();
            return 0;
        }
        if (HIWORD(wParam) == BN_CLICKED) executeCommand(LOWORD(wParam));
        else if (HIWORD(wParam) == EN_CHANGE && LOWORD(wParam) == CmdPropsSearch) {
            if (propsPanelOpen_) updatePropertiesPanel();
        }
        else if (LOWORD(wParam) == CmdProfilePopup) {
            if (HIWORD(wParam) == LBN_SELCHANGE || HIWORD(wParam) == LBN_DBLCLK) {
                applyProfilePopupSelection();
                SetFocus(canvas_);
            }
            return 0;
        }
        else if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == CmdProfileCombo) {
            const int selection = static_cast<int>(
                SendMessageW(profileCombo_, CB_GETCURSEL, 0, 0));
            if (selection >= 0 && selection < static_cast<int>(profileCatalog_.size()))
                assignProfileToSelection(profileCatalog_[static_cast<std::size_t>(selection)].name);
            SetFocus(canvas_);
        }
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
        if (wParam == 6) {
            KillTimer(window_, 6);
            invalidateCanvas();
            return 0;
        }
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
        if (wParam == 7) {
            KillTimer(window_, 7);
            runRenderBenchmark();
            return 0;
        }
        if (wParam == 8) {
            // Sönümlemeli zoom karesi: kalan hedefin ~%28'i uygulanır
            // (üstel yakınlama; ~5 karede (~75ms) tamamlanır — ani değil,
            // algılanabilir yumuşaklık). Çarpan 1'e yaklaştığında biter.
            if (!zoomAnimActive_) { KillTimer(window_, 8); return 0; }
            RECT wc{}; GetClientRect(canvas_, &wc);
            const double step = 1.0 + (zoomAnimTarget_ - 1.0) * 0.28;
            const bool done = std::abs(zoomAnimTarget_ - 1.0) < 0.004;
            const double apply = done ? zoomAnimTarget_ : step;
            if (mode_ == EditMode::Draw2D)
                camera_.zoom2DAt(zoomAnimCursor_, apply,
                                 std::max(1L, wc.right), std::max(1L, wc.bottom));
            else
                camera_.zoom3DAt(zoomAnimCursor_, apply,
                                 std::max(1L, wc.right), std::max(1L, wc.bottom));
            zoomAnimTarget_ /= apply;
            if (done || std::abs(zoomAnimTarget_ - 1.0) < 0.004) {
                zoomAnimTarget_ = 1.0;
                zoomAnimActive_ = false;
                KillTimer(window_, 8);
                // Animasyon bitti: tekerlek ~120ms sessiz kalirsa navigasyon
                // kapanir (timer 2 mevcut akis — hover/snap geri gelir).
                SetTimer(window_, 2, 120, nullptr);
            }
            invalidateCanvas();
            return 0;
        }
        if (wParam == 2) {
            KillTimer(window_, 2);
            wheelNavigating_ = false;
            if (wheelPendingFactor_ != 1.0) {
                // Momentum burst'unden kalan birikmis zoom artigi: tek seferde
                // uygula (120ms hiz limiti disinda kalan adimlar kaybolmaz).
                RECT wc{}; GetClientRect(canvas_, &wc);
                const double applied = wheelPendingFactor_;
                wheelPendingFactor_ = 1.0;
                lastWheelApplyMs_ = GetTickCount64();
                if (mode_ == EditMode::Draw2D)
                    camera_.zoom2DAt({static_cast<double>(cursorScreen_.x), static_cast<double>(cursorScreen_.y)},
                                     applied, std::max(1L, wc.right), std::max(1L, wc.bottom));
                else
                    camera_.zoom3DAt({static_cast<double>(cursorScreen_.x), static_cast<double>(cursorScreen_.y)},
                                     applied, std::max(1L, wc.right), std::max(1L, wc.bottom));
            }
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
    case WM_KEYDOWN:
        // Canvas odagi yokken F10 dis pencereye duser — toggle'i burada da
        // yakala (canvas odakliyken canvasProc halledince buraya gelmez).
        if (wParam == VK_F6) { toggleGpuLines(); return 0; }
        if (wParam == VK_F5) { runRenderBenchmark(); return 0; }
        return 0;
    case WM_DESTROY:
        if (renderBackend_) { renderBackend_->shutdown(); renderBackend_.reset(); }
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
        else if (transformCommand_ == TransformCommand::Copy) {
            if (transformPhase_ == TransformPhase::Selecting && !selectedModels_.empty())
                onCharacter(L'\r');      // seçim bitti -> baz noktasına geç
            else
                cancelTransformCommand(); // Destination'da sağ tık -> komut biter
        }
        else if (transformCommand_ != TransformCommand::None) onCharacter(L'\r');
        else cancelDrawing();
        if (mode_ == EditMode::View3D) drawingActive_ = false;
        updateControls(); invalidateCanvas(); return 0;
    case WM_MBUTTONDOWN:
        SetFocus(canvas_);
        // 3B: orta tus surukleme = rotate; Alt + orta tus = pan.
        // 2B: orta tus surukleme = pan.
        if (mode_ == EditMode::View3D && !(GetKeyState(VK_MENU) & 0x8000)) {
            rotating_ = true;
        } else {
            panning2D_ = true;
        }
        lastMouse_ = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        SetCapture(canvas_);
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
        const int wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        {
            // Serseri tekerlek olaylarinin desenini yakala (ilk 60 olay):
            // delta degeri + zaman damgasi — filtre tasarimi bu veriden.
            static int wheelLogCount = 0;
            if (wheelLogCount < 60) {
                ++wheelLogCount;
                FILE* diag = fopen("model-maker-render.log", "a");
                if (diag) {
                    fprintf(diag, "WHEEL-EVT n=%d delta=%d t=%llu\n",
                            wheelLogCount, wheelDelta,
                            static_cast<unsigned long long>(GetTickCount64()));
                    fclose(diag);
                }
            }
        }
        // Touchpad/gevsemis tekerlek mikro delta'lari (|delta| < WHEEL_DELTA/4)
        // gercek zoom sayilmaz: her biri %12 zoom + 350ms wheelNavigating_ acip
        // motion overlay'i kapatir, hover kareleri fallback'e duser -> flicker.
        if (wheelDelta == 0 || (wheelDelta > 0 ? wheelDelta : -wheelDelta) < WHEEL_DELTA / 4)
            return 0;
        RECT zoomClient{}; GetClientRect(canvas_, &zoomClient);
        const double factor = wheelDelta > 0 ? 1.12 : 1.0 / 1.12;
        if (!wheelNavigating_) { wheelPreviewFactor_ = 1.0; wheelPreviewOffset_ = {}; }
        // HIZ SINIRLAYICI: free-spin tekerlek tek dokunusta 3-4 detent
        // (31-47ms arayla) uretiyor — her biri %12 zoom = gorunum kac kez
        // atliyor, tiklamalar "yanlis yere" dusuyor ve redraw'lar flicker
        // uretiyor. Detentler BIRIKTIRILIR; 64ms'de en fazla BIR zoom
        // uygulanir, artan zamanlayiciya birakilir (case 2 flush).
        // SÖNÜMLEMELİ ZOOM: tekerlek hedefi biriktirir; uygulama animasyon
        // karelerinde (timer 8, ~15ms) küçük adımlarla yapılır. İmleç
        // çapası sabit — her adım zoomXAt ile imleç altındaki noktaya
        // kilitli, böylece toplam etki eski anlık %12 sıçramasıyla aynı
        // fakat akıcı (Tekla/OCC viewer hissi). Hız sınırlayıcı kalktı:
        // artık kayıp yok, her detent hedefe eklenir.
        (void)wheelPendingFactor_; (void)lastWheelApplyMs_;
        zoomAnimTarget_ *= factor;
        zoomAnimCursor_ = {static_cast<double>(zoomCursor.x), static_cast<double>(zoomCursor.y)};
        if (!zoomAnimActive_) { zoomAnimActive_ = true; SetTimer(window_, 8, 15, nullptr); }
        wheelNavigating_ = true;
        if (mode_ == EditMode::Draw2D || drawingActive_ || workPlanePicking_ ||
            (transformCommand_ != TransformCommand::None && transformPhase_ != TransformPhase::Selecting))
            updateHover(cursorScreen_.x, cursorScreen_.y);
        else hover_.reset();
        invalidateCanvas();
        }
        return 0;
    case WM_CHAR: onCharacter(static_cast<wchar_t>(wParam)); return 0;
    case WM_SYSKEYDOWN: {
        // Alt+1..4: 3B gorsel stiller (AutoCAD VPMONITOR tarzi)
        const bool altDown = (GetKeyState(VK_MENU) & 0x8000) != 0;
        if (altDown) {
            if (wParam == '1') { setVisualStyle(VisualStyle::Wireframe); return 0; }
            if (wParam == '2') { setVisualStyle(VisualStyle::Transparent); return 0; }
            if (wParam == '3') { setVisualStyle(VisualStyle::HiddenLine); return 0; }
            if (wParam == '4') { setVisualStyle(VisualStyle::Solid); return 0; }
        }
        break;
    }
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            if (profilePopup_ && IsWindowVisible(profilePopup_)) {
                ShowWindow(profilePopup_, SW_HIDE);
                SetFocus(canvas_);
                return 0;
            }
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
        else if (wParam == VK_F6) toggleGpuLines(); // F10 menulerce SYSKEY olarak yutuluyor + Polar Tracking'in tusuydu
        else if (wParam == VK_F9) gridSnapEnabled_ = !gridSnapEnabled_;
        else if (wParam == VK_F10) {
            polarTrackingEnabled_ = !polarTrackingEnabled_;
            if (polarTrackingEnabled_) orthoEnabled_ = false;
            else clearTemporaryTracking();
        }
        else if (wParam == VK_F12) dynamicInputEnabled_ = !dynamicInputEnabled_;
        else if (wParam == VK_F2) {
            // Gorsel stil dongusu: Wireframe -> Solid -> Transparent
            visualStyle_ = visualStyle_ == VisualStyle::Wireframe ? VisualStyle::Solid
                         : visualStyle_ == VisualStyle::Solid ? VisualStyle::Transparent
                         : visualStyle_ == VisualStyle::Transparent ? VisualStyle::HiddenLine
                         : VisualStyle::Wireframe;
            updateControls(); invalidateCanvas();
        }
        else if (wParam == VK_F11) { performanceOverlayEnabled_ = !performanceOverlayEnabled_; invalidateCanvas(); }
        else if (wParam == VK_F5) runRenderBenchmark();
        else if (wParam == 'Z' && (GetKeyState(VK_CONTROL) & 0x8000)) undo();
        else if (wParam == 'Y' && (GetKeyState(VK_CONTROL) & 0x8000)) redo();
        else if (wParam == 'N' && (GetKeyState(VK_CONTROL) & 0x8000)) newDocument();
        else if (wParam == 'O' && (GetKeyState(VK_CONTROL) & 0x8000)) openDocument();
        else if (wParam == 'S' && (GetKeyState(VK_CONTROL) & 0x8000)) saveDocument();
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
        else if (wParam == 'S') addCylinder();
        else if (wParam == 'X') {
            // In the Qt shell X opens the actual visible QComboBox. Retain
            // the Win32 popup only as a standalone Application fallback.
            if (profilePickerCallback_) profilePickerCallback_();
            else toggleProfilePopup();
        }
#ifdef MM_HAS_OCC
        else if (wParam == 'J') {
            if (GetKeyState(VK_CONTROL) < 0) addBooleanCommon();
            else if (GetKeyState(VK_SHIFT) < 0) addBooleanCut();
            else addBooleanFuse();
        }
#endif
        else if (wParam == 'R' && mode_ == EditMode::View3D) camera_.reset();
        else if (wParam == VK_DELETE) startTransformCommand(TransformCommand::Delete);
        else if (wParam == 'S' && (GetKeyState(VK_CONTROL) & 0x8000)) saveDocument();
        else if (wParam == 'O' && (GetKeyState(VK_CONTROL) & 0x8000)) openDocument();
        updateHover(cursorScreen_.x, cursorScreen_.y); updateControls(); invalidateCanvas();
        return 0;
    case WM_SIZE:
        if (renderBackend_) renderBackend_->resize(LOWORD(lParam), HIWORD(lParam));
        invalidateCanvas(); return 0;
    default: return DefWindowProcW(canvas_, message, wParam, lParam);
    }
}

void Application::onCanvasPaint() {
    if (transformCommand_ == TransformCommand::Trim ||
        transformCommand_ == TransformCommand::Extend)
        trimExtendLog(L"WM_PAINT models=" + std::to_wstring(document_.models().size()));
    if (paintSequence_ < 10) {
        trimExtendLog(L"PAINT-ENTER " + std::to_wstring(paintSequence_) +
                      L" models=" + std::to_wstring(document_.models().size()));
        ++paintSequence_;
    }
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(canvas_, &paint);
    RECT client{};
    GetClientRect(canvas_, &client);
    const bool wasSnapPreview = snapPreviewActive_;
    // F1: GPU hatti — backend YALNIZ F9 ile GL acilinca uretilir; GDI
    // varsayilanda canvas'a hicbir GL dokunmaz (ilk boyamada init edilen GL
    // context'i GDI cizimini bozup "cizgiler kayboldu" hatasi uretiyordu).
    // 2B'de GL overlay kapali: FBO+AlphaBlend kompoziti DWM tarafinda arka
    // planda "gradient" flicker uretiyordu (drawing/koordinatlar saglikliydi).
    // 3B modunda GL acik kalir; F5 (kalici VBO + GPU kompozit) 2B'ye geri doner.
    IRenderBackend* activeBackend =
        (renderBackend_ && gpuLinesEnabled_ && mode_ == EditMode::View3D)
            ? renderBackend_.get() : nullptr;
    {
        // Kosulsuz oturum isareti: log yolunun calistigini ve bu build'in
        // aktif oldugunu kanitlar (teshis icin).
        static bool sessionLogged = false;
        if (!sessionLogged) {
            sessionLogged = true;
            FILE* diag = fopen("model-maker-render.log", "a");
            if (diag) { fprintf(diag, "SESSION-START build=2026-08-18-bench\n"); fclose(diag); }
        }
    }
    {
        // F8 dogrulamasi tamamlandi (benchmark tablosu render.log'da) —
        // otomatik tetikleyici kaldirildi; benchmark artik yalniz F5 veya
        // Gorunum > Benchmark menu ogesiyle calisir.
    }
    const auto paintStart = std::chrono::steady_clock::now();
    renderer_.draw(dc, client, document_, camera_, mode_, draftView(), activeBackend);
    if (paintSequence_ <= 10 && paintSequence_ > 0)
        trimExtendLog(L"PAINT-EXIT " + std::to_wstring(paintSequence_ - 1));
    const double paintMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - paintStart).count();
    if (paintMs > 5.0 && document_.models().size() > 5'000)
        trimExtendLog(L"PAINT ms=" + std::to_wstring(paintMs) +
                      L" interactive=" + std::to_wstring(wasSnapPreview ? 1 : 0) +
                      L" models=" + std::to_wstring(document_.models().size()));
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
    if (GetKeyState(VK_CONTROL) < 0) {
        // Ctrl+tiklama: bos modda nesne secimi (profil atama icin).
        updateHover(x, y);
        if (!toggleModelSelection(x, y)) selectedModels_.clear();
        // updateStatus publishes to both the hidden Win32 fallback status and
        // the visible Qt status bar; direct SetWindowTextW only reached the
        // hidden host.
        updateStatus();
        invalidateCanvas();
        return;
    }
    bool documentHasSolids = false;
    for (std::size_t i = 0; i < document_.models().size(); ++i) {
        if (!document_.models()[i].faces().empty() &&
            !document_.models()[i].properties().profileName.empty()) {
            documentHasSolids = true;
            break;
        }
    }
    if (transformCommand_ == TransformCommand::Trim && documentHasSolids) {
        // 3B kati trim (yalniz dokumanda profil katisi varken devrede):
        // kati -> kesim cizgisi -> kalacak taraf. Kati yokken normal 2B
        // cizgi trim akisi (asagidaki genel dal) oldugu gibi calisir.
        const auto hit = trimSolidPhase_ == 0 ? solidTrimTargetAt(x, y)
                : trimSolidPhase_ == 1 ? trimLineTargetAt(x, y)
                                       : trimExtendTargetAt(x, y);
        if (trimSolidPhase_ == 0) {
            if (hit && *hit < document_.models().size() &&
                !document_.models()[*hit].faces().empty()) {
                trimSolidIndex_ = *hit;
                trimSolidPhase_ = 1;
                selectedModels_.assign(1, *hit);
                publishStatus(L"Kesim çizgisini seçin (katiyi kesecek çizgi)");
                updateControls();
                invalidateCanvas();
            }
            FILE* phaseLog = fopen("model-maker-render.log", "a");
            if (phaseLog) {
                fprintf(phaseLog, "TRIM-PHASE0 hit=%d hasFaces=%d\n",
                        hit ? static_cast<int>(*hit) : -1,
                        (hit && *hit < document_.models().size() &&
                         !document_.models()[*hit].faces().empty()) ? 1 : 0);
                fclose(phaseLog);
            }
            if (hit && *hit < document_.models().size() &&
                document_.models()[*hit].faces().empty() &&
                document_.models()[*hit].vertices().size() == 2 &&
                document_.models()[*hit].properties().profileName.empty()) {
                trimLineIndex_ = *hit;
                trimSolidPhase_ = 3;
                selectedModels_.assign(1, *hit);
                publishStatus(L"Kesilecek katiya tıklayın");
                updateControls();
                invalidateCanvas();
                return;
            }
            return;
        }
        if (trimSolidPhase_ == 3) {
            if (const auto solid = solidTrimTargetAt(x, y)) {
                trimSolidIndex_ = *solid;
                selectedModels_.push_back(*solid);
                updateControls();
                invalidateCanvas();
                performSolidTrimByLine(trimLineIndex_);
            }
            return;
        }
        if (trimSolidPhase_ == 1) {
            RECT viewport{}; GetClientRect(canvas_, &viewport);
            std::optional<Vec3> pickPoint;
            if (mode_ == EditMode::Draw2D) {
                pickPoint = screenTo2D(x, y);
            } else {
                pickPoint = camera_.unprojectToPlane(
                    {static_cast<double>(x), static_cast<double>(y)},
                    std::max(1L, viewport.right), std::max(1L, viewport.bottom), workPlane_);
            }
            if (pickPoint) {
                double bestDistance = 1e18;
                std::optional<std::size_t> bestLine;
                for (std::size_t i = 0; i < document_.models().size(); ++i) {
                    const auto& candidate = document_.models()[i];
                    if (!candidate.faces().empty() || candidate.vertices().size() != 2) continue;
                    // Profil atanmis eksen cizgisi (kirisin olustugu cizgi)
                    // kesim cizgisi OLAMAZ — onunla kesim boruyu boyuna yarar.
                    if (!candidate.properties().profileName.empty()) continue;
                    const Vec3 a = candidate.vertices()[0];
                    const Vec3 b = candidate.vertices()[1];
                    const double abx = b.x - a.x, aby = b.y - a.y, abz = b.z - a.z;
                    const double lengthSq = abx * abx + aby * aby + abz * abz;
                    double t = 0.0;
                    if (lengthSq > 1e-12) {
                        t = std::clamp(((pickPoint->x - a.x) * abx +
                                        (pickPoint->y - a.y) * aby +
                                        (pickPoint->z - a.z) * abz) / lengthSq, 0.0, 1.0);
                    }
                    const double dx = pickPoint->x - (a.x + t * abx);
                    const double dy = pickPoint->y - (a.y + t * aby);
                    const double dz = pickPoint->z - (a.z + t * abz);
                    const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        bestLine = i;
                    }
                }
                if (!bestLine) {
                    publishStatus(L"Yakin cizgi bulunamadi — once bir cizgi cizin");
                } else {
                    selectedModels_.push_back(*bestLine);
                    updateControls();
                    invalidateCanvas();
                    performSolidTrimByLine(*bestLine);
                }
            }
            return;
        }

    }
    if (transformCommand_ != TransformCommand::None) {
        if (transformCommand_ == TransformCommand::Trim ||
            transformCommand_ == TransformCommand::Extend) {
            trimExtendLog(L"DOWN x=" + std::to_wstring(x) + L" y=" + std::to_wstring(y) +
                          L" phase=" + std::to_wstring(static_cast<int>(transformPhase_)) +
                          L" models=" + std::to_wstring(document_.models().size()));
        }
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
                        bool applied = false;
                        if (mode_ == EditMode::Draw2D) {
                            applied = applyTrimExtendTarget(*target, screenTo2D(x, y));
                        } else {
                        RECT viewport{}; GetClientRect(canvas_, &viewport);
                        WorkPlane targetPlane = workPlane_;
                        if (!modifierBoundaries_.empty() && !modifierBoundaries_.front().vertices().empty())
                            targetPlane.origin = modifierBoundaries_.front().vertices().front();
                        if (const auto point = camera_.unprojectToPlane(
                                {static_cast<double>(x), static_cast<double>(y)},
                                std::max(1L, viewport.right), std::max(1L, viewport.bottom), targetPlane))
                                applied = applyTrimExtendTarget(*target, *point);
                        }
                        if (!applied) MessageBeep(MB_ICONWARNING);
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
        if (transformCommand_ == TransformCommand::Trim ||
            transformCommand_ == TransformCommand::Extend)
            trimExtendPreviewSuppressed_ = true;
        updateControls(); updateStatus(); invalidateCanvas();
        // Trim/Extend: WM_PAINT kuyruğu Qt gömme ortamında gecikebiliyor ve
        // sonuç ancak bir sonraki fare hareketinde görünüyor. Kuyruktan
        // bağımsız olarak sonucu eşzamanlı çiz (sonraki WM_PAINT aynı
        // durumu tekrar boyar, zararsız).
        if ((transformCommand_ == TransformCommand::Trim ||
             transformCommand_ == TransformCommand::Extend) &&
            !trimRegionRefreshed_) {
            trimExtendLog(L"SYNC DRAW baslıyor");
            if (HDC dc = GetDC(canvas_)) {
                RECT client{};
                GetClientRect(canvas_, &client);
                renderer_.draw(dc, client, document_, camera_, mode_, draftView());
                ReleaseDC(canvas_, dc);
                trimExtendLog(L"SYNC DRAW tamam client=" +
                              std::to_wstring(client.right) + L"x" +
                              std::to_wstring(client.bottom));
            } else {
                trimExtendLog(L"SYNC DRAW GetDC BASARISIZ");
            }
            // Parent/üst pencere boyamaları canvas'ı ezebiliyor; 80ms sonra
            // bir kez daha yeniden çiz (kısa gecikmeli garantili kare).
            SetTimer(window_, 6, 80, nullptr);
        }
        trimRegionRefreshed_ = false;
        return;
    }
    if (drawingActive_) {
        updateHover(x, y);
        if (hover_) {
            const Vec3 point = hover_->point;
            clearTemporaryTracking();
            commitPoint(point);
        }
    } else if (mode_ == EditMode::Draw2D && transformCommand_ == TransformCommand::None) {
        // Neutral selection: single click toggle, drag for window
        if (selectionFirstCorner_) {
            completeWindowSelection(x, y);
            trimExtendLog(L"SECIM-PENCERE sel=" + std::to_wstring(selectedModels_.size()));
        } else {
            const bool toggled = toggleModelSelection(x, y);
            if (!toggled) selectionFirstCorner_ = POINT{x, y};
            trimExtendLog(L"SECIM-TIK toggled=" + std::to_wstring(toggled) +
                          L" sel=" + std::to_wstring(selectedModels_.size()));
        }
        updateHover(x, y);
    } else if (mode_ == EditMode::View3D) {
        // 3B'de sol surukleme kamerayi dondurmez — 2B'deki gibi notr secim
        // penceresi cizer (rotate yalniz orta tusta; Alt+orta tus pan).
        if (selectionFirstCorner_) {
            completeWindowSelection(x, y);
            trimExtendLog(L"SECIM-PENCERE3D sel=" + std::to_wstring(selectedModels_.size()));
        } else {
            const bool toggled = toggleModelSelection(x, y);
            if (!toggled) selectionFirstCorner_ = POINT{x, y};
            trimExtendLog(L"SECIM-TIK3D toggled=" + std::to_wstring(toggled) +
                          L" sel=" + std::to_wstring(selectedModels_.size()));
        }
        updateHover(x, y);
    }
    updateStatus(); updateControls(); invalidateCanvas();
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
    trimExtendPreviewSuppressed_ = false;
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
    } else if (panning2D_ && (buttons & MK_MBUTTON)) {
        if (mode_ == EditMode::Draw2D) {
            camera_.pan2DByPixels(x - lastMouse_.x, y - lastMouse_.y);
        } else {
            camera_.pan3DByPixels(x - lastMouse_.x, y - lastMouse_.y);
        }
        lastMouse_ = {x, y};
        updateHover(x, y);
        redraw = true;
    } else if (zoomWindowActive_ ||
               (transformCommand_ != TransformCommand::None && transformPhase_ == TransformPhase::Selecting) ||
               workPlanePicking_ || mode_ == EditMode::Draw2D || drawingActive_ ||
               (mode_ == EditMode::View3D && selectionFirstCorner_) ||
               (transformCommand_ != TransformCommand::None && transformPhase_ != TransformPhase::Selecting)) {
        {
            const auto hoverStart = std::chrono::steady_clock::now();
            updateHover(x, y);
            const double hoverMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - hoverStart).count();
            if (hoverMs > 2.0)
                trimExtendLog(L"HOVER ms=" + std::to_wstring(hoverMs) +
                              L" models=" + std::to_wstring(document_.models().size()));
        }
        redraw = true;
        snapRedraw = commandAllowsSnapping(drawingActive_, transformCommand_, transformPhase_,
                                           arrayItemCount_.has_value(), offsetDistance_.has_value());
    }
    if (snapRedraw) updateStatus();
    if (redraw) {
        // Motion overlay her boyutta: tam kare temiz tabanı motion buffer'a
        // yazar; hareket kareleri yalnızca taban + geri bildirim çizer.
        // HER hareket anında bir kare tetikler; timer-3 son çare yenileme,
        // timer-4 ise hareket durduktan sonraki tam karedir.
        KillTimer(window_, 4);
        snapPreviewActive_ = true;
        KillTimer(window_, 3);
        SetTimer(window_, 3, 50, nullptr);
        snapPreviewTimerArmed_ = true;
        invalidateCanvas();
    }
}

void Application::onCharacter(wchar_t character) {
    if (profileAssignmentActive_) {
        if (character == L'\r') { commitProfileAssignment(); return; }
        if (character == 27) { // Esc
            profileAssignmentActive_ = false;
            profileInput_.clear();
            updateStatus(); invalidateCanvas();
            return;
        }
        if (character == L'\b') {
            if (!profileInput_.empty()) profileInput_.pop_back();
        } else if (iswalnum(character) || character == L'*' || character == L'-' ||
                   character == L'.' || character == L' ') {
            profileInput_ += static_cast<wchar_t>(towupper(character));
        } else {
            return;
        }
        std::wstring prompt = L"Profil adı: " + profileInput_;
        SetWindowTextW(status_, prompt.c_str());
        return;
    }
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
        if (point != start) {
            if (polylineModelIndex_ && *polylineModelIndex_ < document_.models().size()) {
                // Mevcut polyline'i tek obje olarak uzat.
                const auto& existing = document_.models()[*polylineModelIndex_];
                std::vector<Vec3> vertices = existing.vertices();
                vertices.push_back(point);
                WireframeModel extended = WireframeModel::polyline(vertices);
                extended.setProperties(existing.properties());
                document_.replaceModel(*polylineModelIndex_,
                                       std::vector<WireframeModel>{std::move(extended)});
            } else {
                pushUndoSnapshot();
                addStyledModel(WireframeModel::polyline(std::vector<Vec3>{start, point}));
                polylineModelIndex_ = document_.models().size() - 1;
            }
        }
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
    polylineModelIndex_.reset();
    anchor_.reset();
    facePoints_.clear();
    input_.clear();
    clearTemporaryTracking();
    drawingActive_ = false;
    selectedModels_.clear();
    selectionFirstCorner_.reset();
}

void Application::startTransformCommand(TransformCommand command) {
    cancelZoomWindow2D();
    if (workPlanePicking_) cancelWorkPlaneCommand();
    // cancelDrawing seçimi de temizler; noun-verb akışı için koru.
    trimExtendLog(L"STARTCMD cmd=" + std::to_wstring(static_cast<int>(command)) +
                  L" sel-giris=" + std::to_wstring(selectedModels_.size()));
    auto preservedSelection = std::move(selectedModels_);
    cancelDrawing();
    selectedModels_ = std::move(preservedSelection);
    trimExtendLog(L"STARTCMD cancel-sonrasi sel=" + std::to_wstring(selectedModels_.size()));

    // Noun-verb: boş ekranda seçilmiş modeller komutun verisidir.
    if (command == TransformCommand::Delete && !selectedModels_.empty()) {
        pushUndoSnapshot();
        document_.deleteModels(selectedModels_);
        selectedModels_.clear();
        cancelTransformCommand();
        updateControls();
        updateStatus();
        invalidateCanvas();
        return;
    }

    lastTransformCommand_ = command;
    transformCommand_ = command;
    if (command == TransformCommand::Trim || command == TransformCommand::Extend) {
        if (!selectedModels_.empty()) {
            // Seçili modeller sınır (kesici) olur; doğrudan hedef seçimine geç.
            modifierBoundaries_.clear();
            modifierBoundaries_.reserve(selectedModels_.size());
            for (const auto index : selectedModels_)
                if (index < document_.models().size())
                    modifierBoundaries_.push_back(document_.models()[index]);
            selectedModels_.clear();
            transformPhase_ = TransformPhase::Destination;
        } else {
            modifierBoundaries_.clear();
            transformPhase_ = TransformPhase::Selecting;
        }
    } else if (command == TransformCommand::Offset && selectedModels_.size() != 1) {
        selectedModels_.clear();
        transformPhase_ = TransformPhase::Selecting;
    } else if (!selectedModels_.empty()) {
        transformPhase_ = TransformPhase::BasePoint;
    } else {
        transformPhase_ = TransformPhase::Selecting;
    }
    selectionFirstCorner_.reset();
    transformBase_.reset();
    offsetDistance_.reset();
    filletFirstPick_.reset();
    arrayItemCount_.reset();
    lastTrimExtendStatus_.clear();
    drawingActive_ = false;
    hover_.reset();
    SetCursor(currentCanvasCursor());
    updateHover(cursorScreen_.x, cursorScreen_.y);
    updateControls();
    updateStatus();
    invalidateCanvas();
    trimExtendLog(L"APP BASLADI build=" __DATE__ " " __TIME__);
}

void Application::toggle3DView() {
    cancelZoomWindow2D();
    if (workPlanePicking_) cancelWorkPlaneCommand();
    if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
    cancelDrawing();
    mode_ = mode_ == EditMode::View3D ? EditMode::Draw2D : EditMode::View3D;
    drawingActive_ = mode_ == EditMode::Draw2D;
    updateHover(cursorScreen_.x, cursorScreen_.y);
    updateControls();
    updateStatus();
    invalidateCanvas();
}

void Application::toggleSnapType(SnapType type) noexcept {
    auto idx = static_cast<std::size_t>(type);
    if (idx < enabledSnapTypes_.size()) enabledSnapTypes_[idx] = !enabledSnapTypes_[idx];
}

void Application::setCurrentLayer(const std::string& layer) {
    currentLayer_ = layer;
    // Secili nesneler varsa katman dogrudan atanir (AutoCAD davranisi).
    if (!selectedModels_.empty()) {
        document_.setModelLayer(selectedModels_, layer);
        updateControls();
        invalidateCanvas();
    }
}

void Application::setCurrentColorChoice(int index) noexcept {
    if (index < 0 || index >= static_cast<int>(colorChoices.size())) return;
    currentColorChoice_ = index;
    // Secili nesne varsa renk dogrudan atanir (AutoCAD davranisi);
    // yoksa renk sonraki cizimlerin rengi olur.
    const auto choice = colorChoices[static_cast<std::size_t>(index)];
    if (!choice.color || selectedModels_.empty()) return; // ByLayer = atama yok
    document_.setModelColor(selectedModels_, *choice.color);
    updateControls();
    invalidateCanvas();
}

void Application::setCurrentLineTypeChoice(int index) noexcept {
    if (index < 0 || index >= static_cast<int>(lineTypeChoices.size())) return;
    currentLineTypeChoice_ = index;
    if (selectedModels_.empty()) return;
    const auto& choice = lineTypeChoices[static_cast<std::size_t>(index)];
    document_.setModelLineType(selectedModels_, choice.value);
    updateControls();
    invalidateCanvas();
}

void Application::refreshLayerList() { refreshLayerCombo(); }

std::vector<std::string> Application::layerNames() const {
    return document_.layerNames();
}

bool Application::createLayer(std::string name) {
    bool ok = document_.createLayer(std::move(name));
    if (ok) refreshLayerList();
    return ok;
}

bool Application::deleteLayer(const std::string& name) {
    if (name == "0") return false; // cannot delete default layer
    bool ok = document_.deleteLayer(name);
    if (ok) refreshLayerList();
    return ok;
}

bool Application::renameLayer(const std::string& oldName, std::string newName) {
    if (oldName == "0") return false; // cannot rename default layer
    bool ok = document_.renameLayer(oldName, std::move(newName));
    if (ok) refreshLayerList();
    return ok;
}

const std::unordered_map<std::string, EntityProperties>&
Application::layerProperties() const {
    return document_.layers();
}

const std::vector<std::string>&
Application::lineTypePalette() {
    static std::vector<std::string> values;
    if (values.empty()) {
        values.reserve(lineTypeChoices.size());
        for (const auto& choice : lineTypeChoices)
            values.push_back(choice.value);
    }
    return values;
}

const std::vector<std::pair<const wchar_t*, std::optional<std::uint32_t>>>&
Application::colorPalette() {
    static const std::vector<std::pair<const wchar_t*, std::optional<std::uint32_t>>> palette = []() {
        std::vector<std::pair<const wchar_t*, std::optional<std::uint32_t>>> p;
        for (const auto& c : colorChoices)
            p.emplace_back(c.label, c.color);
        return p;
    }();
    return palette;
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
    updateHover(cursorScreen_.x, cursorScreen_.y);
    updateControls();
    updateStatus();
    invalidateCanvas();
}

void Application::startZoomWindow2D() {
    if (workPlanePicking_) cancelWorkPlaneCommand();
    if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
    cancelDrawing();
    drawingActive_ = false;
    zoomWindowActive_ = true;
    zoomWindowFirstCorner_.reset();
    hover_.reset();
    updateHover(cursorScreen_.x, cursorScreen_.y);
    updateControls();
    updateStatus();
    invalidateCanvas();
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
    // Qt menü/ribbon dogrudan cagirdiginda kendi kendini yenile (5 yontem kurali)
    updateHover(cursorScreen_.x, cursorScreen_.y);
    updateControls();
    updateStatus();
    invalidateCanvas();
}

void Application::cancelWorkPlaneCommand() {
    workPlanePicking_ = false;
    workPlanePoints_.clear();
    hover_.reset();
    clearTemporaryTracking();
}

void Application::resetWorkPlane() {
    cancelWorkPlaneCommand();
    workPlane_ = WorkPlane::world();
    hover_.reset();
    updateHover(cursorScreen_.x, cursorScreen_.y);
    updateControls();
    updateStatus();
    invalidateCanvas();
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
    trimSolidPhase_ = 0;
    trimSolidIndex_ = 0;
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

std::optional<std::size_t> Application::solidTrimTargetAt(int x, int y) const {
    // Yalniz kati modeller (yuzu olanlar) icin isabet testi: kesim cizgisi
    // katinin ustunde duruyorsa bile kati oncelikli secilir.
    std::vector<std::size_t> solids;
    for (std::size_t i = 0; i < document_.models().size(); ++i)
        if (!document_.models()[i].faces().empty()) solids.push_back(i);
    if (solids.empty()) return std::nullopt;
    if (mode_ == EditMode::Draw2D)
        return hitTestModelCandidates2D(screenTo2D(x, y), document_,
                                        10.0 / (60.0 * camera_.zoom()), solids);
    RECT viewport{};
    GetClientRect(canvas_, &viewport);
    return hitTestModelCandidates3D({static_cast<double>(x), static_cast<double>(y)}, document_,
                                    camera_, std::max(1L, viewport.right),
                                    std::max(1L, viewport.bottom), 10.0, solids);
}

std::optional<std::size_t> Application::trimLineTargetAt(int x, int y) const {
    // Kesim cizgisi secimi: yalniz yuzsuz (cizgi/egri) modeller — katinin
    // dolgulu yuzu cizginin uzerindeyse bile cizgiyi calamasin.
    std::vector<std::size_t> lines;
    for (std::size_t i = 0; i < document_.models().size(); ++i)
        if (document_.models()[i].faces().empty() &&
            document_.models()[i].properties().profileName.empty())
            lines.push_back(i);
    if (lines.empty()) return std::nullopt;
    if (mode_ == EditMode::Draw2D)
        return hitTestModelCandidates2D(screenTo2D(x, y), document_,
                                        10.0 / (60.0 * camera_.zoom()), lines);
    RECT viewport{};
    GetClientRect(canvas_, &viewport);
    return hitTestModelCandidates3D({static_cast<double>(x), static_cast<double>(y)}, document_,
                                    camera_, std::max(1L, viewport.right),
                                    std::max(1L, viewport.bottom), 10.0, lines);
}

#ifdef MM_HAS_OCC
void Application::performSolidTrimByLine(std::size_t lineIndex) {
    if (lineIndex >= document_.models().size()) return;
    const auto& cutLine = document_.models()[lineIndex];
    if (cutLine.vertices().size() != 2) return;
    const Vec3 a = cutLine.vertices()[0];
    const Vec3 b = cutLine.vertices()[1];
    Vec3 direction{b.x - a.x, b.y - a.y, b.z - a.z};
    const double length = std::sqrt(direction.x * direction.x +
                                    direction.y * direction.y +
                                    direction.z * direction.z);
    if (length <= 1e-9) return;
    direction = {direction.x / length, direction.y / length, direction.z / length};
    const Vec3& n = workPlane_.normal;
    Vec3 planeNormal{direction.y * n.z - direction.z * n.y,
                     direction.z * n.x - direction.x * n.z,
                     direction.x * n.y - direction.y * n.x};
    const double normalLength = std::sqrt(planeNormal.x * planeNormal.x +
                                          planeNormal.y * planeNormal.y +
                                          planeNormal.z * planeNormal.z);
    if (normalLength <= 1e-9) return;
    planeNormal = {planeNormal.x / normalLength, planeNormal.y / normalLength,
                   planeNormal.z / normalLength};
    trimPlanePoint_ = a;
    trimPlaneNormal_ = planeNormal;
    const auto& solidModel = document_.models()[trimSolidIndex_];
    Vec3 center{0.0, 0.0, 0.0};
    for (const auto& vertex : solidModel.vertices()) {
        center.x += vertex.x;
        center.y += vertex.y;
        center.z += vertex.z;
    }
    const double count =
        static_cast<double>(std::max<std::size_t>(1, solidModel.vertices().size()));
    center = {center.x / count, center.y / count, center.z / count};
    const double side = (center.x - a.x) * planeNormal.x +
                        (center.y - a.y) * planeNormal.y +
                        (center.z - a.z) * planeNormal.z;
    executeSolidTrim(side >= 0.0);
    trimSolidPhase_ = 0;
    trimSolidIndex_ = 0;
    trimLineIndex_ = 0;
    cancelTransformCommand();
}
#endif

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
        lastTrimExtendStatus_ = L"Trim: " + std::to_wstring(result->size()) +
                                (result->size() == 1 ? L" parça" : L" parça");

        // Bölgesel yenileme: eski model + yeni parçaların ekran kapsama
        // alanını hesapla, yalnızca o bölgeyi eşzamanlı boya. Büyük
        // modellerde tam kare maliyeti olmadan kesim anında görünür.
        RECT client{};
        GetClientRect(canvas_, &client);
        const int canvasW = std::max(1L, client.right);
        const int canvasH = std::max(1L, client.bottom);
        RECT refreshRect{};
        bool haveRefresh = false;
        const auto includeWorldBounds = [&](const Bounds3& bounds) {
            const std::array<Vec3, 8> corners = {{
                {bounds.minimum.x, bounds.minimum.y, bounds.minimum.z},
                {bounds.maximum.x, bounds.minimum.y, bounds.minimum.z},
                {bounds.minimum.x, bounds.maximum.y, bounds.minimum.z},
                {bounds.maximum.x, bounds.maximum.y, bounds.minimum.z},
                {bounds.minimum.x, bounds.minimum.y, bounds.maximum.z},
                {bounds.maximum.x, bounds.minimum.y, bounds.maximum.z},
                {bounds.minimum.x, bounds.maximum.y, bounds.maximum.z},
                {bounds.maximum.x, bounds.maximum.y, bounds.maximum.z}}};
            for (const auto& corner : corners) {
                const Vec2 projected = mode_ == EditMode::Draw2D
                    ? camera_.project2D(corner, canvasW, canvasH)
                    : camera_.project(corner, canvasW, canvasH);
                const LONG px = static_cast<LONG>(std::lround(projected.x));
                const LONG py = static_cast<LONG>(std::lround(projected.y));
                if (!haveRefresh) {
                    refreshRect = {px, py, px, py};
                    haveRefresh = true;
                } else {
                    refreshRect.left = std::min(refreshRect.left, px);
                    refreshRect.top = std::min(refreshRect.top, py);
                    refreshRect.right = std::max(refreshRect.right, px);
                    refreshRect.bottom = std::max(refreshRect.bottom, py);
                }
            }
        };
        const auto pieceBounds = [](const WireframeModel& piece) -> std::optional<Bounds3> {
            if (piece.vertices().empty()) return std::nullopt;
            Bounds3 bounds{piece.vertices().front(), piece.vertices().front()};
            for (const auto& vertex : piece.vertices()) {
                bounds.minimum.x = std::min(bounds.minimum.x, vertex.x);
                bounds.minimum.y = std::min(bounds.minimum.y, vertex.y);
                bounds.minimum.z = std::min(bounds.minimum.z, vertex.z);
                bounds.maximum.x = std::max(bounds.maximum.x, vertex.x);
                bounds.maximum.y = std::max(bounds.maximum.y, vertex.y);
                bounds.maximum.z = std::max(bounds.maximum.z, vertex.z);
            }
            return bounds;
        };
        if (target < document_.modelBounds().size())
            includeWorldBounds(document_.modelBounds()[target]);
        for (const auto& piece : *result)
            if (const auto bounds = pieceBounds(piece)) includeWorldBounds(*bounds);

        document_.replaceModel(target, std::move(*result));

        pushUndoSnapshot();
        if (haveRefresh) {
            snapPreviewActive_ = false; // tıklama boyaması tam kare olmalı
            constexpr LONG inflate = 24;
            refreshRect.left -= inflate;
            refreshRect.top -= inflate;
            refreshRect.right += inflate;
            refreshRect.bottom += inflate;
            InvalidateRect(canvas_, &refreshRect, FALSE);
            UpdateWindow(canvas_);
            trimRegionRefreshed_ = true;
        }
        trimExtendLog(L"APPLY OK target=" + std::to_wstring(target) +
                      L" models=" + std::to_wstring(document_.models().size()) +
                      (haveRefresh ? L" region_refresh=OK" : L" region_refresh=YOK"));
        return true;
    }
    if (transformCommand_ == TransformCommand::Extend) {
        auto result = mode_ == EditMode::View3D
            ? extendLineOnPlane(document_.models()[target], modifierBoundaries_, pickPoint, workPlane_)
            : extendLine2D(document_.models()[target], modifierBoundaries_, pickPoint);
        if (!result) return false;
        lastTrimExtendStatus_ = L"Extend: uygulandı";
        pushUndoSnapshot();
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
        pushUndoSnapshot();
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
        // AUTOCAD AKISI: komut Destination fazinda AKTIF kalir; her hedef
        // tiklamasi bir kopya daha uretir. Kritik duzeltmeler:
        //  1) KAYNAK hep orijinaller: secim degismez — eskiden secim
        //     kopyalara gecince her tiklama kopyalarin kopyasini uretip
        //     "golge nesneler ilerliyor" anomaliydi.
        //  2) Taban noktasi her kopyada GUNCELLENIR: yeni kopya son
        //     tiklanan noktadan degil, tabandan degiserek uretilir
        //     (AutoCAD "Multiple" davranisi — ayni delta degil, imlec
        //     nereye giderse oraya).
        pushUndoSnapshot();
        document_.copyModels(selectedModels_, displacement);
        // Taban SABIT kalir (AutoCAD): kopya = kaynak + (tiklanan - taban).
        // Tabani tıklamaya güncellemek deltayı bozuyordu: ikinci kopya
        // kaynağın üstüne biniyor, toplu rotasyonda nesneler "yer
        // değiştirmiş" gibi görünüyordu.
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
        // Grid snap araligi = cizilen grid'in araligi (adaptif 1-2-5): snap
        // eskiden sabit 1.0mm'e yapisiyordu; artik gorunur grid noktalarina.
        const double gridStep = niceGridStep(60.0 * camera_.zoom());
        hover_ = SnapEngine::snap(screenTo2D(x, y), document_, 10.0 / (60.0 * camera_.zoom()), gridStep,
                                  snapEnabled_, gridSnapEnabled_, reference, &enabledSnapTypes_);
    } else {
        RECT client{}; GetClientRect(canvas_, &client);
        const int width = std::max(1L, client.right);
        const int height = std::max(1L, client.bottom);
        const auto reference = transformPhase_ == TransformPhase::Destination ? transformBase_ : anchor_;
        WorkPlane activePlane = workPlane_;
        if (reference && !workPlanePicking_) activePlane.origin = *reference;
        // 3B grid snap: is duzlemindeki adaptif adim (grid ile ayni).
        const Vec2 gOrigin = camera_.project(activePlane.origin, width, height);
        const Vec2 gUnit = camera_.project(activePlane.fromPlane({1.0, 0.0}), width, height);
        const double gPx = std::hypot(gUnit.x - gOrigin.x, gUnit.y - gOrigin.y);
        hover_ = SnapEngine::snap3D({static_cast<double>(x), static_cast<double>(y)}, document_, camera_,
                                    width, height, 10.0, niceGridStep(gPx), activePlane,
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
            if (modifying) {
                // Transform: is duzleminin U/V eksenleri (README davranisi).
                hover_ = applyOrtho3D(*orthoAnchor, {static_cast<double>(x), static_cast<double>(y)}, *hover_,
                                      camera_, std::max(1L, client.right), std::max(1L, client.bottom),
                                      workPlane_, true, false);
            } else {
                // Cizim: UCS secilmisse (is duzlemi dunya XY'den farkliysa)
                // F8 o duzlemin U/V eksenlerinde kisitlar — AutoCAD UCS
                // davranisi: kullanici duzlem secince objeler o duzlemde
                // ilerler. Dunya duzlemindeyse ayni seydir (U=X, V=Y).
                const bool customUcs =
                    std::abs(workPlane_.u.x - 1.0) > 1e-9 || std::abs(workPlane_.u.y) > 1e-9 ||
                    std::abs(workPlane_.u.z) > 1e-9 || std::abs(workPlane_.v.y - 1.0) > 1e-9 ||
                    std::abs(workPlane_.v.x) > 1e-9 || std::abs(workPlane_.v.z) > 1e-9;
                if (customUcs) {
                    hover_ = applyOrtho3D(*orthoAnchor, {static_cast<double>(x), static_cast<double>(y)}, *hover_,
                                          camera_, std::max(1L, client.right), std::max(1L, client.bottom),
                                          workPlane_, true, false);
                } else {
                    // Dunya duzlemi: uc renkli ortho guide ile ayni eksen
                    // kumesi (X/Y/Z) — Z yonunde izleme de calisir.
                    hover_ = applyOrtho3D(*orthoAnchor, {static_cast<double>(x), static_cast<double>(y)}, *hover_,
                                          camera_, std::max(1L, client.right), std::max(1L, client.bottom),
                                          false);
                }
            }
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
    case CmdFilterPopup:
        filterPopupOpen_ = !filterPopupOpen_;
        ShowWindow(filterPopup_, filterPopupOpen_ ? SW_SHOW : SW_HIDE);
        if (filterPopupOpen_) {
            SetWindowPos(filterPopup_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            SetFocus(filterLayerEdit_);
        }
        SetFocus(canvas_);
        break;
    case CmdFilterFind:
    case CmdFilterSelect: {
        // Read filter criteria
        wchar_t layerBuf[256]{}, colorBuf[256]{}, lenBuf[256]{};
        GetWindowTextW(filterLayerEdit_, layerBuf, 256);
        GetWindowTextW(filterColorEdit_, colorBuf, 256);
        GetWindowTextW(filterLengthEdit_, lenBuf, 256);
        std::wstring layerStr(layerBuf), colorStr(colorBuf), lenStr(lenBuf);
        
        std::vector<std::size_t> matched;
        for (std::size_t idx = 0; idx < document_.models().size(); ++idx) {
            const auto& m = document_.models()[idx];
            const auto& p = m.properties();
            bool ok = true;
            if (!layerStr.empty()) {
                std::wstring l = utf8ToWide(p.layer);
                for (auto& c : l) c = std::towlower(c);
                auto lf = layerStr;
                for (auto& c : lf) c = std::towlower(c);
                if (l.find(lf) == std::wstring::npos) ok = false;
            }
            if (!colorStr.empty()) {
                // Exact integer match for color
                try {
                    int targetColor = std::stoi(colorStr);
                    if (p.colorIndex != targetColor) ok = false;
                } catch (...) { ok = false; }
            }
            if (!lenStr.empty()) {
                const auto& v = m.vertices();
                if (v.size() == 2) {
                    double dx = v[1].x - v[0].x, dy = v[1].y - v[0].y, dz = v[1].z - v[0].z;
                    double len = std::sqrt(dx*dx + dy*dy + dz*dz);
                    try {
                        double targetLen = std::stod(lenStr);
                        if (std::abs(len - targetLen) > 0.01) ok = false;
                    } catch (...) { ok = false; }
                } else ok = false;
            }
            if (ok) matched.push_back(idx);
        }
        if (id == CmdFilterFind) {
            // Just select/highlight
            selectedModels_ = matched;
        } else {
            // Select and zoom to matches
            selectedModels_ = matched;
        }
        updateControls();
        invalidateCanvas();
        if (propsPanelOpen_) updatePropertiesPanel();
        break;
    }
    case CmdProperties:
        propsPanelOpen_ = !propsPanelOpen_;
        updatePropertiesPanel();
        SetFocus(canvas_);
        updateControls();
        invalidateCanvas();
        break;
    case CmdPropsFilter: {
        const bool vis = IsWindowVisible(propsSearch_);
        ShowWindow(propsSearch_, vis ? SW_HIDE : SW_SHOW);
        updatePropertiesPanel();
        if (!vis) SetFocus(propsSearch_);
        break;
    }
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
    case CmdCylinder: addCylinder(); break;
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
    updateHover(cursorScreen_.x, cursorScreen_.y);
    updateControls();
    updateStatus();
    invalidateCanvas();
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
    if (propsPanelOpen_) updatePropertiesPanel();
}

void Application::refreshLayerCombo() {
    std::vector<std::string> layers{"0"};
    for (const auto& [name, properties] : document_.layers()) {
        (void)properties;
        if (name != "0") layers.push_back(name);
    }
    std::sort(layers.begin() + 1, layers.end());

    if (layerComboWidget_) {
        // Qt widget owns the combo — always use Qt API to keep the model in sync
        QComboBox* combo = layerComboWidget_;
        const QString previous = combo->currentText();
        combo->blockSignals(true);
        combo->clear();
        for (const auto& name : layers) {
            combo->addItem(QString::fromStdString(name));
        }
        int selection = combo->findText(previous);
        if (selection < 0) selection = 0;
        combo->setCurrentIndex(selection);
        combo->blockSignals(false);
        return;
    }

    if (!layerCombo_) return;
    wchar_t previous[256]{};
    GetWindowTextW(layerCombo_, previous, static_cast<int>(std::size(previous)));
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
    EntityStyleSelection selection;
    if (layerComboWidget_) {
        // Qt UI: style state lives in the Application members updated by the
        // ribbon/menu widgets (setCurrentLayer/setCurrentColorChoice/
        // setCurrentLineTypeChoice), not in hidden GDI combos.
        selection.layer = currentLayer_;
        if (currentColorChoice_ >= 0 && static_cast<std::size_t>(currentColorChoice_) < colorChoices.size())
            selection.trueColor = colorChoices[static_cast<std::size_t>(currentColorChoice_)].color;
        if (currentLineTypeChoice_ >= 0 && static_cast<std::size_t>(currentLineTypeChoice_) < lineTypeChoices.size())
            selection.lineType = lineTypeChoices[static_cast<std::size_t>(currentLineTypeChoice_)].value;
        return resolveEntityStyle(selection, document_.layers());
    }
    wchar_t layerText[256]{L'0', L'\0'};
    if (layerCombo_) GetWindowTextW(layerCombo_, layerText, static_cast<int>(std::size(layerText)));
    selection.layer = wideToUtf8(layerText);
    const LRESULT colorIndex = colorCombo_ ? SendMessageW(colorCombo_, CB_GETCURSEL, 0, 0) : 0;
    if (colorIndex >= 0 && static_cast<std::size_t>(colorIndex) < colorChoices.size())
        selection.trueColor = colorChoices[static_cast<std::size_t>(colorIndex)].color;
    const LRESULT lineTypeIndex = lineTypeCombo_ ? SendMessageW(lineTypeCombo_, CB_GETCURSEL, 0, 0) : 0;
    if (lineTypeIndex >= 0 && static_cast<std::size_t>(lineTypeIndex) < lineTypeChoices.size())
        selection.lineType = lineTypeChoices[static_cast<std::size_t>(lineTypeIndex)].value;
    return resolveEntityStyle(selection, document_.layers());
}

void Application::pushUndoSnapshot() { document_.pushSnapshot(); }

void Application::undo() {
    if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
    if (!document_.undo()) return;
    polylineModelIndex_.reset();
    selectedModels_.clear();
    selectionFirstCorner_.reset();
    refreshLayerCombo();
    updateHover(cursorScreen_.x, cursorScreen_.y);
    updateControls();
    updateStatus();
    invalidateCanvas();
}

void Application::redo() {
    if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
    if (!document_.redo()) return;
    polylineModelIndex_.reset();
    selectedModels_.clear();
    selectionFirstCorner_.reset();
    refreshLayerCombo();
    updateHover(cursorScreen_.x, cursorScreen_.y);
    updateControls();
    updateStatus();
    invalidateCanvas();
}

void Application::addStyledModel(WireframeModel model) {
    pushUndoSnapshot();
    // Stil alanlari (katman/renk/cizgi tipi) akimdan gelir; modelin kendi
    // tasiydigi profil kimligi (profileName/Rotation/SourceLine) KORUNUR —
    // Onceki kod butun ozellikleri eziyor, katinin kaynak cizgisi kayboluyordu.
    auto styled = currentEntityProperties();
    auto kept = model.properties();
    styled.profileName = kept.profileName;
    styled.profileRotation = kept.profileRotation;
    styled.profileSourceLine = kept.profileSourceLine;
    styled.axisFromX = kept.axisFromX; styled.axisFromY = kept.axisFromY;
    styled.axisFromZ = kept.axisFromZ;
    styled.axisToX = kept.axisToX; styled.axisToY = kept.axisToY;
    styled.axisToZ = kept.axisToZ;
    styled.material = kept.material;
    model.setProperties(std::move(styled));
    document_.addModel(std::move(model));
}

void Application::purgeStaleAxisLines() {
    // Eski tasarim artiklari: profilli katinin ekseniyle cakisan 2-ugencli
    // cizgiler (kaynak eksen cizgileri) silinir — kati kendi eksenini
    // tasir, cizgi yalniz giris aracidir. Dosya acilisi ve atama sonrasi
    // cagirilir; sarI "extra cizgiler" bu artiklardi.
    std::vector<std::size_t> stale;
    for (std::size_t i = 0; i < document_.models().size(); ++i) {
        const auto& m = document_.models()[i];
        if (m.vertices().size() != 2 || m.edges().size() != 1) continue;
        if (m.properties().profileName.empty()) continue; // yalniz profilli kaynak cizgiler
        const Vec3 lf = m.vertices()[0];
        const Vec3 lt = m.vertices()[1];
        for (std::size_t k = 0; k < document_.models().size(); ++k) {
            if (k == i) continue;
            const auto& solid = document_.models()[k];
            if (solid.faces().empty() || solid.properties().profileName.empty()) continue;
            const Vec3 af(solid.properties().axisFromX, solid.properties().axisFromY,
                          solid.properties().axisFromZ);
            const Vec3 at(solid.properties().axisToX, solid.properties().axisToY,
                          solid.properties().axisToZ);
            const bool fwd = std::abs(lf.x - af.x) < 1e-6 && std::abs(lf.y - af.y) < 1e-6 &&
                std::abs(lf.z - af.z) < 1e-6 && std::abs(lt.x - at.x) < 1e-6 &&
                std::abs(lt.y - at.y) < 1e-6 && std::abs(lt.z - at.z) < 1e-6;
            const bool rev = std::abs(lf.x - at.x) < 1e-6 && std::abs(lf.y - at.y) < 1e-6 &&
                std::abs(lf.z - at.z) < 1e-6 && std::abs(lt.x - af.x) < 1e-6 &&
                std::abs(lt.y - af.y) < 1e-6 && std::abs(lt.z - af.z) < 1e-6;
            if (fwd || rev) { stale.push_back(i); break; }
        }
    }
    if (!stale.empty()) document_.deleteModels(stale);
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
        if (statusCallback_) statusCallback_(progress);
        SetWindowTextW(status_, progress.c_str());
        return;
    }
    std::wstring text = L"   ";
    text += mode_ == EditMode::View3D ? L"3B Paralel" : L"2B Plan XY";
    {
        // GPU modu gostergesi: her durum guncellemesinde aktif modu gosterir.
        const bool glActive = gpuLinesEnabled_ && renderBackend_ &&
                              renderBackend_->isHardwareAccelerated();
        text += glActive ? L"  |  GPU: GL" : L"  |  GPU: GDI";
    }
    // Keep selection and section data near the start of the Qt status bar so
    // A/Ix/Wx/G remain visible even when the remaining CAD state is lengthy.
    if (!selectedModels_.empty()) {
        text += L"  |  Seçim: " + std::to_wstring(selectedModels_.size());
        bool haveProfile = false;
        bool mixedProfiles = false;
        std::string selectedProfile;
        for (const auto index : selectedModels_) {
            if (index >= document_.models().size()) continue;
            const auto& name = document_.models()[index].properties().profileName;
            if (!haveProfile) {
                selectedProfile = name;
                haveProfile = true;
            } else if (name != selectedProfile) {
                mixedProfiles = true;
                break;
            }
        }
        if (haveProfile && mixedProfiles) {
            text += L"  |  Profil: karma";
        } else if (haveProfile && selectedProfile.empty()) {
            text += L"  |  Profil: atanmamış";
        } else if (haveProfile) {
            text += L"  |  Profil: " + utf8ToWide(selectedProfile);
            if (const auto* profile = mm::findProfile(profileCatalog_, selectedProfile)) {
                wchar_t section[192]{};
                std::swprintf(section, std::size(section),
                              L"  |  A=%.0f mm²  Ix=%.0f cm⁴  Wx=%.0f cm³  G=%.2f kg/m",
                              profile->crossSectionArea, profile->inertiaX / 1.0e4,
                              profile->sectionModulusX / 1.0e3,
                              profile->weightPerUnitLength);
                text += section;
            }
        }
    }
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
        if (!lastTrimExtendStatus_.empty()) {
            text += L"  |  ";
            text += lastTrimExtendStatus_;
        }
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
    if (!solidStatusMessage_.empty())
        text += L"  |  " + solidStatusMessage_;
    if (hover_) {
        wchar_t coordinates[96]{};
        std::swprintf(coordinates, std::size(coordinates), L"  |  X %.3f  Y %.3f  Z %.3f",
                      hover_->point.x, hover_->point.y, hover_->point.z);
        text += coordinates;
    }
    if (statusCallback_) statusCallback_(text);
    SetWindowTextW(status_, text.c_str());
}

void Application::publishStatus(const std::wstring& text) {
    if (statusCallback_) statusCallback_(text);
    if (status_) SetWindowTextW(status_, text.c_str());
}

void Application::invalidateCanvas() { if (canvas_) InvalidateRect(canvas_, nullptr, FALSE); }

#ifdef _WIN32
void Application::ensureOccBridge() {
    static bool tried = false;
    if (tried || occBridgeDll_) return;
    tried = true;
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    const auto exeDir = std::filesystem::path(exePath).parent_path();
    const auto dllPath = exeDir / L"occ" / L"mm_occ.dll";
    // OCC DLL'leri occ\ alt dizininde yasar; MSYS2 runtime'lari da oradadir.
    // LoadLibrary'den ONCE o dizini DLL arama yoluna ekle ki uygulamanin
    // kendi runtime'ina (Qt GCC13 libstdc++) dokunulmasin.
    const auto addDllDir = reinterpret_cast<void* (WINAPI*)(PCWSTR)>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "AddDllDirectory")));
    if (addDllDir) addDllDir((exeDir / L"occ").c_str());
    occBridgeDll_ = LoadLibraryW(dllPath.c_str());
    if (!occBridgeDll_) {
        FILE* diag = fopen("model-maker-render.log", "a");
        if (diag) {
            fprintf(diag, "OCC-BRIDGE-LOAD-FAILED err=%lu path=%ls\n",
                    static_cast<unsigned long>(GetLastError()), dllPath.c_str());
            fclose(diag);
        }
        return;
    }
    occBridgeVersion_ = reinterpret_cast<decltype(occBridgeVersion_)>(
        reinterpret_cast<void*>(GetProcAddress(occBridgeDll_, "mm_occ_version")));
    {
        // Kanit satiri: koprunun yuklendigini ve surumunu loga yaz.
        FILE* diag = fopen("model-maker-render.log", "a");
        if (diag) {
            fprintf(diag, "OCC-BRIDGE-OK version=%s\n",
                    occBridgeVersion_ ? occBridgeVersion_() : "?");
            fclose(diag);
        }
    }
    occBridgeSolidBox_ = reinterpret_cast<decltype(occBridgeSolidBox_)>(
        reinterpret_cast<void*>(GetProcAddress(occBridgeDll_, "mm_occ_solid_box")));
    occBridgeFree_ = reinterpret_cast<decltype(occBridgeFree_)>(
        reinterpret_cast<void*>(GetProcAddress(occBridgeDll_, "mm_occ_free")));
}
#endif

void Application::addCube() {
    if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
#ifdef MM_HAS_OCC
    // vcpkg/Linux: OCC ayni derleyiciyle dogrudan derlenmistir — kopru
    // gereksiz, gercek BRep kutu dogrudan uretilir.
    {
        TopoDS_Shape shape = BRepPrimAPI_MakeBox(2.6, 2.6, 2.6).Shape();
        const double offset = static_cast<double>(document_.models().size() % 4) * 0.45;
        if (offset != 0.0) {
            gp_Trsf trsf; trsf.SetTranslation(gp_Vec(offset, 0.0, 0.0));
            shape = BRepBuilderAPI_Transform(shape, trsf).Shape();
        }
        occShapes_.emplace(document_.models().size() - 1, shape);
        auto cube = mm::shapeToWireframeWithFaces(shape, 0.15);
        cube.translate({offset, 0.0, 0.0});
        addStyledModel(std::move(cube)); mode_ = EditMode::View3D;
        cancelDrawing(); drawingActive_ = false;
        wchar_t message[96]{};
        std::swprintf(message, std::size(message), L"Kutu (BRep) hacmi: %.2f birim³",
                      mm::shapeVolume(shape));
        solidStatusMessage_ = message;
        updateStatus();
        return;
    }
#endif
#ifdef _WIN32
    // OCC koprusu varsa gercek BRep kutu uret; yoksa eski analitik
    // WireframeModel::cube yolu aynen calisir.
    ensureOccBridge();
    if (occBridgeSolidBox_ && occBridgeFree_) {
        float* verts = nullptr; int nv = 0;
        unsigned int* ed = nullptr; int ne = 0;
        if (occBridgeSolidBox_(2.6, 2.6, 2.6, &verts, &nv, &ed, &ne) == 0 &&
            verts && ed && nv > 0 && ne > 0) {
            std::vector<Vec3> vertices(static_cast<std::size_t>(nv));
            for (int i = 0; i < nv; ++i)
                vertices[static_cast<std::size_t>(i)] = {verts[i * 3], verts[i * 3 + 1], verts[i * 3 + 2]};
            std::vector<Edge> edges(static_cast<std::size_t>(ne));
            for (int i = 0; i < ne; ++i)
                edges[static_cast<std::size_t>(i)] = {ed[i * 2], ed[i * 2 + 1]};
            occBridgeFree_(verts);
            occBridgeFree_(ed);
            auto cube = WireframeModel(std::move(vertices), std::move(edges));
            cube.translate({static_cast<double>(document_.models().size() % 4) * 0.45, 0.0, 0.0});
            addStyledModel(std::move(cube)); mode_ = EditMode::View3D;
            cancelDrawing(); drawingActive_ = false;
            return;
        }
        if (verts) occBridgeFree_(verts);
        if (ed) occBridgeFree_(ed);
    }
#endif
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

void Application::addCylinder() {
    if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
#ifdef MM_HAS_OCC
    {
        // OCC BRep silindir: r=1.3, h=3.0 (kutu/piramit ile ayni sabit-olusum akisi)
        TopoDS_Shape shape = BRepPrimAPI_MakeCylinder(1.3, 1.3, 3.0).Shape();
        const double offset = static_cast<double>(document_.models().size() % 4) * 0.45;
        if (offset != 0.0) {
            gp_Trsf trsf; trsf.SetTranslation(gp_Vec(offset, 0.0, 0.0));
            shape = BRepBuilderAPI_Transform(shape, trsf).Shape();
        }
        occShapes_.emplace(document_.models().size() - 1, shape);
        auto cylinder = mm::shapeToWireframeWithFaces(shape, 0.15);
        cylinder.translate({offset, 0.0, 0.0});
        addStyledModel(std::move(cylinder)); mode_ = EditMode::View3D;
        cancelDrawing(); drawingActive_ = false;
        wchar_t message[96]{};
        std::swprintf(message, std::size(message), L"Silindir (BRep) hacmi: %.2f birim³",
                      mm::shapeVolume(shape));
        solidStatusMessage_ = message;
        updateStatus();
        return;
    }
#endif
    // OCC yoksa yaklasik tel kafes silindir (2 daire + 2 dik cizgi)
    auto cylinder = WireframeModel::circle({0.0, 0.0, 0.0}, 1.3, 48);
    auto top = WireframeModel::circle({0.0, 0.0, 3.0}, 1.3, 48);
    addStyledModel(std::move(cylinder));
    addStyledModel(std::move(top));
    addStyledModel(WireframeModel::line({1.3, 0.0, 0.0}, {1.3, 0.0, 3.0}));
    addStyledModel(WireframeModel::line({-1.3, 0.0, 0.0}, {-1.3, 0.0, 3.0}));
    mode_ = EditMode::View3D;
    cancelDrawing(); drawingActive_ = false;
}

#ifdef MM_HAS_OCC
void Application::addBooleanFuse() {
    if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
    if (occShapes_.size() < 2) {
        publishStatus(L"Boolean: en az 2 kati gerekli (B ve S ile ekleyin)");
        return;
    }
    const TopoDS_Shape result =
        mm::booleanFuseShape(std::prev(occShapes_.end(), 2)->second, occShapes_.rbegin()->second);
    occShapes_.emplace(document_.models().size() - 1, result);
    auto model = mm::shapeToWireframeWithFaces(result, 0.15);
    addStyledModel(std::move(model)); mode_ = EditMode::View3D;
    cancelDrawing(); drawingActive_ = false;
    wchar_t message[96]{};
    std::swprintf(message, std::size(message), L"Birlesim (Fuse) hacmi: %.2f birim³",
                  mm::shapeVolume(result));
    solidStatusMessage_ = message;
    updateStatus();
}

void Application::addBooleanCommon() {
    if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
    if (occShapes_.size() < 2) {
        publishStatus(L"Boolean: en az 2 kati gerekli (B ve S ile ekleyin)");
        return;
    }
    const TopoDS_Shape result =
        mm::booleanCommonShape(std::prev(occShapes_.end(), 2)->second, occShapes_.rbegin()->second);
    occShapes_.emplace(document_.models().size() - 1, result);
    auto model = mm::shapeToWireframeWithFaces(result, 0.15);
    addStyledModel(std::move(model)); mode_ = EditMode::View3D;
    cancelDrawing(); drawingActive_ = false;
    wchar_t message[96]{};
    std::swprintf(message, std::size(message), L"Kesisim (Common) hacmi: %.2f birim³",
                  mm::shapeVolume(result));
    solidStatusMessage_ = message;
    updateStatus();
}

void Application::addBooleanCut() {
    if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
    if (occShapes_.size() < 2) {
        publishStatus(L"Boolean: en az 2 kati gerekli (B ve S ile ekleyin)");
        return;
    }
    // Sondan bir onceki sekilden sonuncusunu cikar (A - B).
    const TopoDS_Shape result =
        mm::booleanCutShape(std::prev(occShapes_.end(), 2)->second, occShapes_.rbegin()->second);
    occShapes_.emplace(document_.models().size() - 1, result);
    auto model = mm::shapeToWireframeWithFaces(result, 0.15);
    addStyledModel(std::move(model)); mode_ = EditMode::View3D;
    cancelDrawing(); drawingActive_ = false;
    wchar_t message[96]{};
    std::swprintf(message, std::size(message), L"Cikarma (Cut) hacmi: %.2f birim³",
                  mm::shapeVolume(result));
    solidStatusMessage_ = message;
    updateStatus();
}
#endif

void Application::ensureProfileCatalog() {
    if (profileCatalogTried_) return;
    profileCatalogTried_ = true;
    const wchar_t* candidates[] = {
        L"C:\\TeklaStructures\\2025.0\\Environments\\default\\General\\Shared\\Profil",
        L"C:\\TeklaStructures\\2023.0\\Environments\\default\\General\\Shared\\Profil",
    };
    for (const auto* directory : candidates) {
        std::error_code errorCode;
        if (!std::filesystem::exists(directory, errorCode)) continue;
        profileCatalog_ = mm::loadProfileCatalog(directory);
        if (!profileCatalog_.empty()) break;
    }
    // Gömülü Avrupa kataloğu (HEA/HEB/HEM/IPE/IPN/UPN — 123 profil):
    // exe'nin yanindaki profiles klasöründen eklenir. Tekla yoksa bile
    // bu katalog her zaman yuklenir.
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path european(exePath);
    european = european.parent_path() / "profiles" / "european.lis";
    {
        std::error_code europeanError;
        if (std::filesystem::exists(european, europeanError)) {
            auto europeanProfiles = mm::parseTeklaProfileDatabase(european);
            std::unordered_set<std::string> existing;
            for (const auto& profile : profileCatalog_)
                existing.insert(profile.name);
            for (auto& profile : europeanProfiles)
                if (existing.insert(profile.name).second)
                    profileCatalog_.push_back(std::move(profile));
        }
    }
    if (!profileCatalog_.empty()) {
        FILE* diag = fopen("model-maker-render.log", "a");
        if (diag) {
            fprintf(diag, "PROFILE-CATALOG loaded=%zu\n", profileCatalog_.size());
            fclose(diag);
        }
    }
}

int Application::selectedModelIndex() const {
    return selectedModels_.empty()
        ? -1
        : static_cast<int>(selectedModels_.front());
}

std::string Application::selectedEntityProfile() const {
    if (selectedModels_.empty() ||
        selectedModels_.front() >= document_.models().size())
        return {};
    return document_.models()[selectedModels_.front()].properties().profileName;
}

std::string Application::selectedEntityLayer() const {
    if (selectedModels_.empty() ||
        selectedModels_.front() >= document_.models().size())
        return {};
    return document_.models()[selectedModels_.front()].properties().layer;
}

int Application::selectedEntityColorIndex() const {
    if (selectedModels_.empty() ||
        selectedModels_.front() >= document_.models().size())
        return -1;
    const auto& props = document_.models()[selectedModels_.front()].properties();
    const auto color = props.trueColor.value_or(0xFFFFFFFFu);
    for (std::size_t i = 0; i < colorChoices.size(); ++i)
        if (colorChoices[i].color && *colorChoices[i].color == color)
            return static_cast<int>(i);
    return -1;
}

std::string Application::selectedEntityLineType() const {
    if (selectedModels_.empty() ||
        selectedModels_.front() >= document_.models().size())
        return {};
    return document_.models()[selectedModels_.front()].properties().lineType;
}

double Application::selectedEntityProfileRotation() const {
    if (selectedModels_.empty() ||
        selectedModels_.front() >= document_.models().size())
        return 0.0;
    return document_.models()[selectedModels_.front()].properties().profileRotation;
}

void Application::setSelectedEntityProfileRotation(double degrees) {
    // TOPLU ROTASYON: secimdeki TUM profilli uyelere ayni aci uygulanir
    // (override). Her kati kendi ekseninde yeniden uretilir; eksen
    // koordinatlari katinin uzerinde tasindigindan indeks kaymalari ve
    // yanlis nesne eslesmesi soz konusu olamaz. Eski dosyalarda secilen
    // eksen cizgisi, uc noktalariyla partner katiya yonlendirilir.
    if (selectedModels_.empty()) return;
    ensureProfileCatalog();

    // 1) Hedefleri topla: secimdeki her oge -> islenecek KATI indeksi.
    //    Cizgi secildiyse partner kati bulunur; kati dogrudan hedeftir.
    std::vector<std::size_t> targets;
    for (const auto selIdx : selectedModels_) {
        if (selIdx >= document_.models().size()) continue;
        std::size_t target = selIdx;
        const auto& sel = document_.models()[selIdx];
        if (sel.faces().empty() && sel.vertices().size() == 2 && sel.edges().size() == 1) {
            const Vec3 lf = sel.vertices()[0];
            const Vec3 lt = sel.vertices()[1];
            bool found = false;
            for (std::size_t i = 0; i < document_.models().size(); ++i) {
                const auto& m = document_.models()[i];
                if (m.faces().empty() || m.properties().profileName.empty()) continue;
                const Vec3 af(m.properties().axisFromX, m.properties().axisFromY,
                              m.properties().axisFromZ);
                const Vec3 at(m.properties().axisToX, m.properties().axisToY,
                              m.properties().axisToZ);
                const bool fwd = std::abs(af.x - lf.x) < 1e-6 && std::abs(af.y - lf.y) < 1e-6 &&
                    std::abs(af.z - lf.z) < 1e-6 && std::abs(at.x - lt.x) < 1e-6 &&
                    std::abs(at.y - lt.y) < 1e-6 && std::abs(at.z - lt.z) < 1e-6;
                const bool rev = std::abs(af.x - lt.x) < 1e-6 && std::abs(af.y - lt.y) < 1e-6 &&
                    std::abs(af.z - lt.z) < 1e-6 && std::abs(at.x - lf.x) < 1e-6 &&
                    std::abs(at.y - lf.y) < 1e-6 && std::abs(at.z - lf.z) < 1e-6;
                if (fwd || rev) { target = i; found = true; break; }
            }
            if (!found) continue; // profilsiz cizgi — rotasyon islemi yok
        }
        const auto& m = document_.models()[target];
        if (m.properties().profileName.empty()) continue;
        bool dup = false;
        for (const auto t : targets) if (t == target) { dup = true; break; }
        if (!dup) targets.push_back(target);
    }
    if (targets.empty()) return;

    // 2) Hedeflerin mevcut ozelliklerini kopyala (dongu ici degisimlere dayanikli)
    struct TargetInfo {
        std::string profileName;
        std::string material, lineType, layer;
        std::optional<std::uint32_t> trueColor; int colorIndex;
        std::int64_t profileSourceLine;
        double axisFrom[3], axisTo[3];
    };
    std::vector<TargetInfo> infos;
    infos.reserve(targets.size());
    for (const auto t : targets) {
        const auto& props = document_.models()[t].properties();
        TargetInfo info;
        info.profileName = props.profileName;
        info.material = props.material;
        info.lineType = props.lineType;
        info.layer = props.layer;
        info.trueColor = props.trueColor;
        info.colorIndex = props.colorIndex;
        info.profileSourceLine = props.profileSourceLine;
        info.axisFrom[0] = props.axisFromX; info.axisFrom[1] = props.axisFromY;
        info.axisFrom[2] = props.axisFromZ;
        info.axisTo[0] = props.axisToX; info.axisTo[1] = props.axisToY;
        info.axisTo[2] = props.axisToZ;
        infos.push_back(std::move(info));
    }

    // 3) Her hedefi kendi ekseninde yeniden uret ve YERINE koy.
    pushUndoSnapshot();
    std::size_t applied = 0;
    for (std::size_t k = 0; k < targets.size(); ++k) {
        const auto& info = infos[k];
        const auto* profile = mm::findProfile(profileCatalog_, info.profileName);
        if (!profile) continue;
        const Vec3 from{info.axisFrom[0], info.axisFrom[1], info.axisFrom[2]};
        const Vec3 to{info.axisTo[0], info.axisTo[1], info.axisTo[2]};
        if (std::abs(to.x - from.x) + std::abs(to.y - from.y) +
            std::abs(to.z - from.z) < 1e-9) continue;
        const TopoDS_Shape solid = mm::extrudeProfileSolid(*profile, from, to, degrees);
        if (solid.IsNull()) continue;
        auto solidModel = mm::shapeToWireframeWithFaces(solid, 0.15);
        auto solidProps = solidModel.properties();
        solidProps.profileName = info.profileName;
        solidProps.profileRotation = degrees;
        solidProps.profileSourceLine = info.profileSourceLine;
        solidProps.axisFromX = info.axisFrom[0]; solidProps.axisFromY = info.axisFrom[1];
        solidProps.axisFromZ = info.axisFrom[2];
        solidProps.axisToX = info.axisTo[0]; solidProps.axisToY = info.axisTo[1];
        solidProps.axisToZ = info.axisTo[2];
        solidProps.material = info.material;
        solidProps.trueColor = info.trueColor;
        solidProps.colorIndex = info.colorIndex;
        solidProps.lineType = info.lineType;
        solidProps.layer = info.layer;
        solidModel.setProperties(std::move(solidProps));
        document_.replaceModel(targets[k], {std::move(solidModel)});
        occShapes_[targets[k]] = solid;
        ++applied;
    }

    // 4) Eski dosya temizligi: hedef eksenlerle cakisan cizgileri sil.
    {
        std::vector<std::size_t> staleLines;
        for (std::size_t i = 0; i < document_.models().size(); ++i) {
            const auto& m = document_.models()[i];
            if (m.vertices().size() != 2 || m.edges().size() != 1) continue;
            bool isTarget = false;
            for (const auto& info : infos) {
                const Vec3 af(info.axisFrom[0], info.axisFrom[1], info.axisFrom[2]);
                const Vec3 at(info.axisTo[0], info.axisTo[1], info.axisTo[2]);
                const Vec3 lf = m.vertices()[0];
                const Vec3 lt = m.vertices()[1];
                const bool fwd = std::abs(lf.x - af.x) < 1e-6 && std::abs(lf.y - af.y) < 1e-6 &&
                    std::abs(lf.z - af.z) < 1e-6 && std::abs(lt.x - at.x) < 1e-6 &&
                    std::abs(lt.y - at.y) < 1e-6 && std::abs(lt.z - at.z) < 1e-6;
                const bool rev = std::abs(lf.x - at.x) < 1e-6 && std::abs(lf.y - at.y) < 1e-6 &&
                    std::abs(lf.z - at.z) < 1e-6 && std::abs(lt.x - af.x) < 1e-6 &&
                    std::abs(lt.y - af.y) < 1e-6 && std::abs(lt.z - af.z) < 1e-6;
                if (fwd || rev) { isTarget = true; break; }
            }
            if (isTarget) staleLines.push_back(i);
        }
        if (!staleLines.empty()) {
            struct OldAxisEntry { Vec3 from, to; TopoDS_Shape shape; };
            std::vector<OldAxisEntry> oldShapes;
            for (const auto& [oldIdx, shape] : occShapes_) {
                if (oldIdx >= document_.models().size()) continue;
                const auto& m = document_.models()[oldIdx];
                if (m.faces().empty() || m.properties().profileName.empty()) continue;
                OldAxisEntry entry;
                entry.from = Vec3(m.properties().axisFromX, m.properties().axisFromY,
                                  m.properties().axisFromZ);
                entry.to = Vec3(m.properties().axisToX, m.properties().axisToY,
                                m.properties().axisToZ);
                entry.shape = shape;
                oldShapes.push_back(std::move(entry));
            }
            document_.deleteModels(staleLines);
            std::map<std::size_t, TopoDS_Shape> rebuilt;
            std::vector<std::size_t> keptTargets;
            for (std::size_t i = 0; i < document_.models().size(); ++i) {
                const auto& m = document_.models()[i];
                if (m.faces().empty() || m.properties().profileName.empty()) continue;
                const Vec3 af(m.properties().axisFromX, m.properties().axisFromY,
                              m.properties().axisFromZ);
                const Vec3 at(m.properties().axisToX, m.properties().axisToY,
                              m.properties().axisToZ);
                bool isInfo = false;
                for (const auto& info : infos) {
                    const bool sameF = std::abs(af.x - info.axisFrom[0]) < 1e-6 &&
                        std::abs(af.y - info.axisFrom[1]) < 1e-6 &&
                        std::abs(af.z - info.axisFrom[2]) < 1e-6;
                    const bool sameT = std::abs(at.x - info.axisTo[0]) < 1e-6 &&
                        std::abs(at.y - info.axisTo[1]) < 1e-6 &&
                        std::abs(at.z - info.axisTo[2]) < 1e-6;
                    if (sameF && sameT) { isInfo = true; break; }
                }
                if (isInfo) keptTargets.push_back(i);
                for (auto& oldEntry : oldShapes) {
                    const bool sameF = std::abs(af.x - oldEntry.from.x) < 1e-6 &&
                        std::abs(af.y - oldEntry.from.y) < 1e-6 &&
                        std::abs(af.z - oldEntry.from.z) < 1e-6;
                    const bool sameT = std::abs(at.x - oldEntry.to.x) < 1e-6 &&
                        std::abs(at.y - oldEntry.to.y) < 1e-6 &&
                        std::abs(at.z - oldEntry.to.z) < 1e-6;
                    if (sameF && sameT) { rebuilt[i] = oldEntry.shape; break; }
                }
            }
            occShapes_ = std::move(rebuilt);
            if (!keptTargets.empty()) targets = std::move(keptTargets);
        }
    }

    selectedModels_ = std::move(targets);
    {
        FILE* rotationDiag = fopen("model-maker-render.log", "a");
        if (rotationDiag) {
            fprintf(rotationDiag, "ROTATION-BULK deg=%.1f applied=%zu\n",
                    degrees, applied);
            fclose(rotationDiag);
        }
    }
    updateControls();
    invalidateCanvas();
}

std::string Application::selectedEntityMaterial() const {
    if (selectedModels_.empty() ||
        selectedModels_.front() >= document_.models().size())
        return {};
    return document_.models()[selectedModels_.front()].properties().material;
}

void Application::setSelectedEntityLineType(const std::string& lineType) {
    if (selectedModels_.empty()) return;
    document_.setModelLineType(selectedModels_, lineType);
    updateControls();
    invalidateCanvas();
}

void Application::setSelectedEntityMaterial(const std::string& material) {
    if (selectedModels_.empty()) return;
    document_.setModelMaterial(selectedModels_, material);
    updateControls();
    invalidateCanvas();
}

std::string Application::selectedEntityTypeLabel() const {
    if (selectedModels_.empty() ||
        selectedModels_.front() >= document_.models().size())
        return {};
    const auto& model = document_.models()[selectedModels_.front()];
    if (!model.properties().profileName.empty()) return "PROFIL (3B kati)";
    if (model.isFace3D()) return "3DFACE";
    if (model.isPointEntity()) return "NOKTA";
    return "CIZGI";
}

std::string Application::selectedEntityLengthLabel() const {
    if (selectedModels_.empty() ||
        selectedModels_.front() >= document_.models().size())
        return {};
    const auto& model = document_.models()[selectedModels_.front()];
    if (model.vertices().size() < 2) return {};
    const Vec3 a = model.vertices().front();
    const Vec3 b = model.vertices().back();
    const double dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
    const double length = std::sqrt(dx * dx + dy * dy + dz * dz);
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "%.2f mm", length);
    return buffer;
}

std::vector<std::string> Application::profileNames() {
    ensureProfileCatalog();
    std::vector<std::string> names;
    names.reserve(profileCatalog_.size());
    for (const auto& profile : profileCatalog_) names.push_back(profile.name);
    std::sort(names.begin(), names.end());
    return names;
}

void Application::startProfileAssignment() {
    if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
    ensureProfileCatalog();
    if (profileCatalog_.empty()) {
        SetWindowTextW(status_, L"Profil katalogu bulunamadi — Tekla .lis dosyalari yok");
        return;
    }
    profileAssignmentActive_ = true;
    profileInput_.clear();
    SetWindowTextW(status_, L"Profil adı: (Esc = iptal)");
    invalidateCanvas();
}

void Application::commitProfileAssignment() {
    profileAssignmentActive_ = false;
    if (profileInput_.empty()) { updateStatus(); return; }
    // Genis -> UTF-8 (profil adlari ASCII olsa da O gibi karakterler icin)
    char utf8Buffer[128]{};
    WideCharToMultiByte(CP_UTF8, 0, profileInput_.c_str(), -1, utf8Buffer,
                        static_cast<int>(std::size(utf8Buffer)), nullptr, nullptr);
    const std::string name(utf8Buffer);
    profileInput_.clear();
    const auto* profile = mm::findProfile(profileCatalog_, name);
    if (!profile) {
        wchar_t message[160]{};
        std::swprintf(message, std::size(message), L"Profil bulunamadi: %hs", name.c_str());
        SetWindowTextW(status_, message);
        updateStatus();
        return;
    }
    if (document_.models().empty()) {
        SetWindowTextW(status_, L"Once bir cizgi cizin (L)");
        updateStatus();
        return;
    }
    const std::size_t index = document_.models().size() - 1;
    const auto& model = document_.models()[index];
    if (model.vertices().size() != 2 || model.edges().size() != 1) {
        SetWindowTextW(status_, L"Son eklenen nesne cizgi degil — once cizgi cizin (L)");
        updateStatus();
        return;
    }
    document_.setModelProfile({index}, profile->name);
    solidStatusMessage_ = L"";
    wchar_t message[220]{};
    std::swprintf(message, std::size(message),
                  L"%hs atandi: A=%.0f mm², Ix=%.0f cm⁴, Wx=%.0f cm³, G=%.2f kg/m",
                  profile->name.c_str(), profile->crossSectionArea,
                  profile->inertiaX / 1.0e4, profile->sectionModulusX / 1.0e3,
                  profile->weightPerUnitLength);
    SetWindowTextW(status_, message);
    updateControls(); invalidateCanvas();
}

void Application::refreshProfileCombo() {
    ensureProfileCatalog();
    if (!profileCombo_) return;
    SendMessageW(profileCombo_, CB_RESETCONTENT, 0, 0);
    for (const auto& profile : profileCatalog_) {
        wchar_t wide[128]{};
        MultiByteToWideChar(CP_UTF8, 0, profile.name.c_str(), -1, wide,
                            static_cast<int>(std::size(wide)));
        SendMessageW(profileCombo_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(wide));
    }
    if (!profileCatalog_.empty())
        SendMessageW(profileCombo_, CB_SETCURSEL, 0, 0);
    if (profilePopup_) {
        SendMessageW(profilePopup_, LB_RESETCONTENT, 0, 0);
        for (const auto& profile : profileCatalog_) {
            wchar_t wide[128]{};
            MultiByteToWideChar(CP_UTF8, 0, profile.name.c_str(), -1, wide,
                                static_cast<int>(std::size(wide)));
            SendMessageW(profilePopup_, LB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(wide));
        }
    }
    if (profileButton_ && !profileCatalog_.empty()) {
        wchar_t wide[128]{};
        MultiByteToWideChar(CP_UTF8, 0, profileCatalog_.front().name.c_str(), -1,
                            wide, static_cast<int>(std::size(wide)));
        std::wstring caption = L"Profil: " + std::wstring(wide);
        SetWindowTextW(profileButton_, caption.c_str());
    }
}

void Application::toggleProfilePopup() {
    if (!profilePopup_) return;
    ensureProfileCatalog();
    if (profileCatalog_.empty()) {
        SetWindowTextW(status_, L"Profil katalogu bulunamadi — Tekla .lis dosyalari yok");
        return;
    }
    const bool visible = IsWindowVisible(profilePopup_);
    if (!visible) {
        RECT buttonRect{};
        GetWindowRect(profileButton_, &buttonRect);
        SetWindowPos(profilePopup_, HWND_TOPMOST, buttonRect.left,
                     buttonRect.bottom + 2, 240, 420,
                     SWP_NOACTIVATE);
        ShowWindow(profilePopup_, SW_SHOWNOACTIVATE);
        SetFocus(profilePopup_);
    } else {
        ShowWindow(profilePopup_, SW_HIDE);
    }
}

void Application::applyProfilePopupSelection() {
    if (!profilePopup_) return;
    const LRESULT selection = SendMessageW(profilePopup_, LB_GETCURSEL, 0, 0);
    ShowWindow(profilePopup_, SW_HIDE);
    if (selection < 0 ||
        selection >= static_cast<LRESULT>(profileCatalog_.size())) return;
    assignProfileToSelection(
        profileCatalog_[static_cast<std::size_t>(selection)].name);
}

#ifdef MM_HAS_OCC
void Application::executeSolidTrim(bool keepPositive) {
    FILE* trace = fopen("model-maker-render.log", "a");
    if (trace) {
        fprintf(trace, "TRIM-TRACE index=%zu models=%zu shapes=%zu keepPos=%d\n",
                trimSolidIndex_, document_.models().size(), occShapes_.size(),
                keepPositive ? 1 : 0);
        fclose(trace);
    }
    if (trimSolidIndex_ >= document_.models().size()) {
        publishStatus(L"Trim: kati bulunamadi");
        return;
    }
    const auto found = occShapes_.find(trimSolidIndex_);
    if (found == occShapes_.end() || found->second.IsNull()) {
        publishStatus(L"Trim: bu kati icin BRep sekli yok (Boolean/extrude ile olusturun)");
        return;
    }
    const TopoDS_Shape result =
        mm::cutSolidByPlane(found->second, trimPlanePoint_, trimPlaneNormal_, keepPositive);
    if (result.IsNull()) {
        MessageBeep(MB_ICONWARNING);
        return;
    }
    auto model = mm::shapeToWireframeWithFaces(result, 0.15);
    {
        // Kesim sonucunun yapisal teşhisi (bozuk gorunum avi)
        int faceCount = 0, edgeCount = 0;
        for (TopExp_Explorer faces(result, TopAbs_FACE); faces.More(); faces.Next()) ++faceCount;
        for (TopExp_Explorer edges(result, TopAbs_EDGE); edges.More(); edges.Next()) ++edgeCount;
        FILE* diag = fopen("model-maker-render.log", "a");
        if (diag) {
            fprintf(diag, "TRIM-RESULT faces=%d edges=%d tessTri=%zu keepPos=%d\n",
                    faceCount, edgeCount, model.faces().size(),
                    keepPositive ? 1 : 0);
            fclose(diag);
        }
    }
    const auto oldProps = document_.models()[trimSolidIndex_].properties();
    auto props = model.properties();
    props.profileName = oldProps.profileName;
    model.setProperties(std::move(props));
    pushUndoSnapshot();
    document_.replaceModel(trimSolidIndex_, {std::move(model)});
    found->second = result;
    publishStatus(L"Kati duzlemle kesildi");
    updateControls();
    invalidateCanvas();
}
#endif

void Application::assignProfileToSelection(const std::string& profileName) {
    ensureProfileCatalog();
    const auto* profile = mm::findProfile(profileCatalog_, profileName);
    if (!profile) {
        publishStatus(L"Profil bulunamadı: " + utf8ToWide(profileName));
        return;
    }
    if (selectedModels_.empty()) {
        publishStatus(L"Önce çizgiyi Ctrl+tıklama ile seçin, sonra profili seçin.");
        return;
    }
    const std::size_t assigned = document_.setModelProfile(selectedModels_, profile->name);
    if (assigned == 0) {
        publishStatus(L"Profil atanamadı — seçili nesne kilitli olabilir.");
        return;
    }
    solidStatusMessage_ = L"";
    {
        FILE* diag = fopen("model-maker-render.log", "a");
        if (diag) {
            fprintf(diag,
                    "PROFILE-ASSIGN name=%s requested=%zu assigned=%zu "
                    "A=%.0f Ix=%.0f Wx=%.0f G=%.2f\n",
                    profile->name.c_str(), selectedModels_.size(), assigned,
                    profile->crossSectionArea, profile->inertiaX,
                    profile->sectionModulusX, profile->weightPerUnitLength);
            fclose(diag);
        }
    }
    // updateControls() calls updateStatus(), which derives the persistent
    // profile summary from the selected models and publishes it to Qt.
    updateControls();
    invalidateCanvas();
#ifdef MM_HAS_OCC
    // 3B extruded gorsel: secili cizgiler icin profil kesitini cizgi ekseni
    // boyunca extrude edip kati olarak ekle (cizgi merkez eksen kalir).
    {
        // Toplu atama: secili cizgilerin EKSEN KOORDINATLARI once kopyalanir
        // (dongu icinde replace/add/delete indeksleri degistirir; eski
        // indeksler yanlis nesneyi isaret eder — "diger objenin yerinin
        // degismesi" hatasinin nedeni). Eslesme yalniz koordinatla yapilir.
        struct AxisTarget { Vec3 from, to; double rotation; };
        std::vector<AxisTarget> axes;
        for (const auto index : selectedModels_) {
            if (index >= document_.models().size()) continue;
            const auto& model = document_.models()[index];
            if (model.vertices().size() != 2 || model.edges().size() != 1) {
                FILE* assignDiag = fopen("model-maker-render.log", "a");
                if (assignDiag) {
                    fprintf(assignDiag,
                            "ASSIGN-SKIP index=%zu verts=%zu edges=%zu (cizgi degil)\n",
                            index, model.vertices().size(), model.edges().size());
                    fclose(assignDiag);
                }
                continue;
            }
            AxisTarget target;
            target.from = model.vertices()[0];
            target.to = model.vertices()[1];
            target.rotation = model.properties().profileRotation;
            axes.push_back(target);
        }
        std::vector<std::size_t> solidIndices;
        std::vector<Vec3> consumedAxes;
        for (const auto& axis : axes) {
            const Vec3 from = axis.from;
            const Vec3 to = axis.to;
            const TopoDS_Shape solid = mm::extrudeProfileSolid(
                *profile, from, to, axis.rotation);
            if (solid.IsNull()) continue;
            auto solidModel = mm::shapeToWireframeWithFaces(solid, 0.15);
            auto solidProps = solidModel.properties();
            solidProps.profileName = profile->name;
            solidProps.profileRotation = axis.rotation;
            solidProps.axisFromX = from.x; solidProps.axisFromY = from.y; solidProps.axisFromZ = from.z;
            solidProps.axisToX = to.x; solidProps.axisToY = to.y; solidProps.axisToZ = to.z;
            solidModel.setProperties(std::move(solidProps));
            // MEVCUT KATI ESLENMESI EKSENLE: ayni profil + ayni eksen
            // baslangic noktasi = ayni eleman (yerinde degistirilir).
            // Farkli eksen = baska eleman; ASLA degistirilmez/silinmez.
            std::optional<std::size_t> existingSolid;
            for (std::size_t i = 0; i < document_.models().size(); ++i) {
                const auto& checkProps = document_.models()[i].properties();
                if (document_.models()[i].faces().empty() || checkProps.profileName.empty())
                    continue;
                const bool sameStart =
                    std::abs(checkProps.axisFromX - from.x) < 1e-6 &&
                    std::abs(checkProps.axisFromY - from.y) < 1e-6 &&
                    std::abs(checkProps.axisFromZ - from.z) < 1e-6;
                const bool sameEnd =
                    std::abs(checkProps.axisToX - to.x) < 1e-6 &&
                    std::abs(checkProps.axisToY - to.y) < 1e-6 &&
                    std::abs(checkProps.axisToZ - to.z) < 1e-6;
                if (sameStart && sameEnd) { existingSolid = i; break; }
            }
            // Eski, eslesen elemanlar (indeks kaymalarindan artiklalar):
            // ayni eksen + farkli profil = bu cizginin eski katisi -> sil.
            std::vector<std::size_t> duplicates;
            for (std::size_t i = 0; i < document_.models().size(); ++i) {
                if (existingSolid && i == *existingSolid) continue;
                const auto& checkProps = document_.models()[i].properties();
                if (document_.models()[i].faces().empty() || checkProps.profileName.empty())
                    continue;
                const bool sameStart =
                    std::abs(checkProps.axisFromX - from.x) < 1e-6 &&
                    std::abs(checkProps.axisFromY - from.y) < 1e-6 &&
                    std::abs(checkProps.axisFromZ - from.z) < 1e-6;
                const bool sameEnd =
                    std::abs(checkProps.axisToX - to.x) < 1e-6 &&
                    std::abs(checkProps.axisToY - to.y) < 1e-6 &&
                    std::abs(checkProps.axisToZ - to.z) < 1e-6;
                if (sameStart && sameEnd) duplicates.push_back(i);
            }
            if (existingSolid) {
                document_.replaceModel(*existingSolid, {std::move(solidModel)});
                occShapes_[*existingSolid] = solid;
                solidIndices.push_back(*existingSolid);
            } else {
                addStyledModel(std::move(solidModel));
                solidIndices.push_back(document_.models().size() - 1);
                occShapes_.emplace(document_.models().size() - 1, solid);
            }
            consumedAxes.push_back(from);
            consumedAxes.push_back(to);
            if (!duplicates.empty()) document_.deleteModels(duplicates);
            {
                const auto& addedModel = document_.models().back();
                FILE* solidLog = fopen("model-maker-render.log", "a");
                if (solidLog) {
                    fprintf(solidLog,
                            "SOLID-MODEL verts=%zu edges=%zu faces=%zu\n",
                            addedModel.vertices().size(), addedModel.edges().size(),
                            addedModel.faces().size());
                    fclose(solidLog);
                }
            }
        }
        // KATI OLUSUNCA KAYNAK CIZGIYI SIL — cizgi yalnizca giris aracidir;
        // kati kendi eksenini tasir, tum islemler (rotasyon/trim/modify)
        // artik kati uzerinden yurur. Eslesme koordinatla: ayni from+to.
        if (!consumedAxes.empty()) {
            std::vector<std::size_t> sourceLines;
            for (std::size_t i = 0; i < document_.models().size(); ++i) {
                const auto& m = document_.models()[i];
                if (m.vertices().size() != 2 || m.edges().size() != 1) continue;
                const Vec3 mf = m.vertices()[0];
                const Vec3 mt = m.vertices()[1];
                const bool hasProfile = !m.properties().profileName.empty();
                if (!hasProfile) continue;
                for (std::size_t k = 0; k < consumedAxes.size(); k += 2) {
                    const Vec3 cf = consumedAxes[k];
                    const Vec3 ct = consumedAxes[k + 1];
                    const bool sameF = std::abs(mf.x - cf.x) < 1e-6 &&
                        std::abs(mf.y - cf.y) < 1e-6 && std::abs(mf.z - cf.z) < 1e-6;
                    const bool sameT = std::abs(mt.x - ct.x) < 1e-6 &&
                        std::abs(mt.y - ct.y) < 1e-6 && std::abs(mt.z - ct.z) < 1e-6;
                    if (sameF && sameT) { sourceLines.push_back(i); break; }
                }
            }
            if (!sourceLines.empty()) {
                // ONCE eski indeks -> eksen eslenigi (silme oncesi durum)
                std::map<std::size_t, TopoDS_Shape> rebuilt;
                struct OldAxisEntry { Vec3 from, to; TopoDS_Shape shape; };
                std::vector<OldAxisEntry> oldShapes;
                for (const auto& [oldIdx, shape] : occShapes_) {
                    if (oldIdx >= document_.models().size()) continue;
                    const auto& m = document_.models()[oldIdx];
                    if (m.faces().empty() || m.properties().profileName.empty()) continue;
                    OldAxisEntry entry;
                    entry.from = Vec3(m.properties().axisFromX, m.properties().axisFromY,
                                      m.properties().axisFromZ);
                    entry.to = Vec3(m.properties().axisToX, m.properties().axisToY,
                                    m.properties().axisToZ);
                    entry.shape = shape;
                    oldShapes.push_back(std::move(entry));
                }
                document_.deleteModels(sourceLines);
                // SILME SONRASI: her katinin YENI indeksini bul, ayni eksenli
                // eski shape'i tasi (koordinat kimligi — indeks kaymasi etkisiz)
                for (std::size_t i = 0; i < document_.models().size(); ++i) {
                    const auto& m = document_.models()[i];
                    if (m.faces().empty() || m.properties().profileName.empty()) continue;
                    const Vec3 mf(m.properties().axisFromX, m.properties().axisFromY,
                                  m.properties().axisFromZ);
                    const Vec3 mt(m.properties().axisToX, m.properties().axisToY,
                                  m.properties().axisToZ);
                    for (auto& oldEntry : oldShapes) {
                        const bool sameF = std::abs(mf.x - oldEntry.from.x) < 1e-6 &&
                            std::abs(mf.y - oldEntry.from.y) < 1e-6 &&
                            std::abs(mf.z - oldEntry.from.z) < 1e-6;
                        const bool sameT = std::abs(mt.x - oldEntry.to.x) < 1e-6 &&
                            std::abs(mt.y - oldEntry.to.y) < 1e-6 &&
                            std::abs(mt.z - oldEntry.to.z) < 1e-6;
                        if (sameF && sameT) { rebuilt[i] = oldEntry.shape; break; }
                    }
                }
                occShapes_ = std::move(rebuilt);
                FILE* lineLog = fopen("model-maker-render.log", "a");
                if (lineLog) {
                    fprintf(lineLog, "SOURCE-LINES-REMOVED count=%zu shapes=%zu\n",
                            sourceLines.size(), occShapes_.size());
                    fclose(lineLog);
                }
            }
            // Secim artik kaynagi silinen katilari gostersin: eksen
            // eslenmesiyle secili katilarin YENI indekslerini bul.
            if (!selectedModels_.empty()) {
                std::vector<std::size_t> keptProps;
                for (const auto selIdx : selectedModels_) {
                    if (selIdx >= document_.models().size()) continue;
                    const auto& m = document_.models()[selIdx];
                    if (!m.properties().profileName.empty() && !m.faces().empty())
                        keptProps.push_back(selIdx);
                }
                (void)keptProps; // indeks duzeltmesi asagida eksen bazli yapilir
                std::vector<std::size_t> newSelection;
                for (const auto& consumed : consumedAxes) (void)consumed;
                for (std::size_t k = 0; k + 1 < consumedAxes.size(); k += 2) {
                    const Vec3 cf = consumedAxes[k];
                    const Vec3 ct = consumedAxes[k + 1];
                    for (std::size_t i = 0; i < document_.models().size(); ++i) {
                        const auto& m = document_.models()[i];
                        if (m.faces().empty() || m.properties().profileName.empty()) continue;
                        const Vec3 af(m.properties().axisFromX, m.properties().axisFromY,
                                      m.properties().axisFromZ);
                        const Vec3 at(m.properties().axisToX, m.properties().axisToY,
                                      m.properties().axisToZ);
                        const bool sameF = std::abs(af.x - cf.x) < 1e-6 &&
                            std::abs(af.y - cf.y) < 1e-6 && std::abs(af.z - cf.z) < 1e-6;
                        const bool sameT = std::abs(at.x - ct.x) < 1e-6 &&
                            std::abs(at.y - ct.y) < 1e-6 && std::abs(at.z - ct.z) < 1e-6;
                        if (sameF && sameT) { newSelection.push_back(i); break; }
                    }
                }
                if (!newSelection.empty())
                    selectedModels_ = std::move(newSelection);
            }
        }
        if (!solidIndices.empty()) {
            FILE* diag = fopen("model-maker-render.log", "a");
            if (diag) {
                const auto& firstLine = document_.models()[selectedModels_.front()];
                const Vec3 diagFrom = firstLine.vertices()[0];
                const Vec3 diagTo = firstLine.vertices()[1];
                fprintf(diag,
                        "PROFILE-EXTRUDE count=%zu profile=%s "
                        "line=(%.2f,%.2f,%.2f)->(%.2f,%.2f,%.2f) dir=(%.2f,%.2f,%.2f)\n",
                        solidIndices.size(), profile->name.c_str(),
                        diagFrom.x, diagFrom.y, diagFrom.z,
                        diagTo.x, diagTo.y, diagTo.z,
                        diagTo.x - diagFrom.x, diagTo.y - diagFrom.y,
                        diagTo.z - diagFrom.z);
                fclose(diag);
            }
        }
    }
#endif
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
    if (hover_) {
        view.cursor = hover_->point;
        view.snapType = hover_->type;
        view.orthoAxis = hover_->orthoAxis;
    }
    view.orthoEnabled = orthoEnabled_;
    view.drawingActive = drawingActive_; view.snapEnabled = snapEnabled_;
    view.gridSnapEnabled = gridSnapEnabled_; view.dynamicInputEnabled = dynamicInputEnabled_;
    view.performanceOverlayEnabled = performanceOverlayEnabled_;
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
    view.trimExtendPreviewSuppressed = trimExtendPreviewSuppressed_;
    view.zoomWindowActive = zoomWindowActive_; view.zoomWindowFirstCorner = zoomWindowFirstCorner_;
    view.interactiveNavigation = rotating_ || panning2D_ || viewCubeManipulating_ ||
                                 wheelNavigating_ || snapPreviewActive_;
    view.motionOverlay = snapPreviewActive_ && !rotating_ && !panning2D_ &&
                         !viewCubeManipulating_ && !wheelNavigating_;
    view.snapPreviewActive = snapPreviewActive_;
    view.wheelNavigating = wheelNavigating_; view.rotating = rotating_;
    view.panning = panning2D_; view.viewCubeActive = viewCubeManipulating_;
    // Esik 8'000'e indirildi: 18k'lik cizimde tekerlek zoom'u stride-4 seyrek
    // fallback kareler uretiyordu (modellerin ~%75'i kaybolup geri geliyordu
    // = zoom sirasinda nabiz/flicker). Raster preview son tam kareyi esnetir —
    // zoom suresince hic seyrek cizim yapilmaz.
    view.rasterZoomPreview = document_.models().size() > 8'000 && wheelNavigating_ &&
                             std::abs(wheelPreviewFactor_ - 1.0) > 1e-12;
    view.rasterZoomFactor = wheelPreviewFactor_;
    view.rasterZoomOffset = wheelPreviewOffset_;
    return view;
}

void Application::runRenderBenchmark() {
    if (!canvas_) return;
    {
        FILE* diag = fopen("model-maker-render.log", "a");
        if (diag) {
            fprintf(diag, "BENCH-START models=%zu\n", document_.models().size());
            fclose(diag);
        }
    }
    if (statusCallback_) statusCallback_(L"Benchmark çalışıyor — GDI fazı...");

    const EditMode originalMode = mode_;
    const bool originalGpu = gpuLinesEnabled_;
    mode_ = EditMode::View3D; // 2B'de GL overlay kapali — karsilastirma 3B'de

    RECT wc{}; GetClientRect(canvas_, &wc);
    const int vw = std::max(1L, wc.right), vh = std::max(1L, wc.bottom);

    const auto pumpUntilFrame = [&](std::uint64_t target) {
        for (int guard = 0; guard < 120; ++guard) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
                if (renderer_.performanceFrameNumber() >= target) return true;
            }
            if (renderer_.performanceFrameNumber() >= target) return true;
            Sleep(5);
        }
        return false;
    };

    const auto benchBackend = [&](const char* label) {
        double total = 0, mn = 1e9, mx = 0; int n = 0;
        const auto step = [&](auto&& apply) {
            apply();
            invalidateCanvas();
            const std::uint64_t target = renderer_.performanceFrameNumber() + 1;
            if (!pumpUntilFrame(target)) return;
            const auto sample = renderer_.latestPerformanceSample();
            total += sample.cpuFrameMilliseconds; ++n;
            mn = std::min(mn, sample.cpuFrameMilliseconds);
            mx = std::max(mx, sample.cpuFrameMilliseconds);
        };
        for (int i = 0; i < 8; ++i) step([] {});
        for (int i = 0; i < 24; ++i) step([&] { camera_.rotate(0.04, 0.0); });
        for (int i = 0; i < 24; ++i)
            step([&] { camera_.zoom3DAt({vw / 2.0, vh / 2.0}, 1.03, vw, vh); });
        for (int i = 0; i < 24; ++i) step([&] { camera_.pan3DByPixels(6.0, 3.0); });
        FILE* f = fopen("model-maker-render.log", "a");
        if (f) {
            fprintf(f, "BENCH-RESULT %s frames=%d avg=%.3f ms min=%.3f ms max=%.3f ms\n",
                    label, n, n ? total / n : 0.0, n ? mn : 0.0, n ? mx : 0.0);
            fclose(f);
        }
    };

    // Uc olcekte olc: 20k / 100k / 250k — GL'nin sabit kompozit maliyetine
    // karsi GDI'nin cizgi-basina maliyeti; kirilma noktasi bu uc noktadan cikar.
    const std::size_t scales[] = {20'000, 100'000, 250'000};
    for (const std::size_t scale : scales) {
        document_.clear();
        std::uint32_t seed = 12345u;
        const auto rnd = [&seed]() {
            seed = seed * 1664525u + 1013904223u;
            return (seed >> 8) / 16777215.0; // [0,1)
        };
        document_.reserveModels(scale);
        for (std::size_t i = 0; i < scale; ++i) {
            const Vec3 a{(rnd() - 0.5) * 100.0, (rnd() - 0.5) * 100.0, (rnd() - 0.5) * 100.0};
            const Vec3 b{a.x + (rnd() - 0.5) * 4.0, a.y + (rnd() - 0.5) * 4.0, a.z + (rnd() - 0.5) * 4.0};
            document_.addModel(WireframeModel::line(a, b));
        }
        FILE* scaleLog = fopen("model-maker-render.log", "a");
        if (scaleLog) { fprintf(scaleLog, "BENCH-SCALE %zu\n", scale); fclose(scaleLog); }
        if (gpuLinesEnabled_) toggleGpuLines();
        benchBackend("GDI");
        if (!gpuLinesEnabled_) toggleGpuLines();
        benchBackend("GL");
    }

    if (gpuLinesEnabled_ != originalGpu) toggleGpuLines();
    mode_ = originalMode;
    if (statusCallback_) statusCallback_(L"Benchmark tamamlandı — render.log'a bakın");
    updateStatus();
    invalidateCanvas();
}

void Application::setVisualStyle(VisualStyle style) noexcept {
    // Gorsel stil (Alt+1..4): Wireframe / Tar-Şeffaf / Hidden / Solid.
    // Hidden = Wireframe'in derinlik duyarli varyanti: cizgiler gorunur ama
    // arkadakiler soluk cizilir (renderer mevcut stillerle esler).
    visualStyle_ = style;
    updateControls();
    invalidateCanvas();
}

void Application::toggleGpuLines() {
    gpuLinesEnabled_ = !gpuLinesEnabled_;
    if (gpuLinesEnabled_ && !backendInitTried_) {
        // Ilk GL acilisinda backend uretilir (GL basarisizsa GDI'ye dus).
        backendInitTried_ = true;
        RECT canvasRect{}; GetClientRect(canvas_, &canvasRect);
        renderBackend_ = createOpenGLRenderBackend();
        if (!renderBackend_ ||
            !renderBackend_->initialize(canvas_, canvasRect.right, canvasRect.bottom)) {
            renderBackend_ = createGdiRenderBackend();
            if (renderBackend_)
                renderBackend_->initialize(canvas_, canvasRect.right, canvasRect.bottom);
        }
    }
    FILE* diag = fopen("model-maker-render.log", "a");
    if (diag) {
        const bool glWillBeActive = gpuLinesEnabled_ && renderBackend_ &&
                                    renderBackend_->isHardwareAccelerated();
        fprintf(diag, "BACKEND-TOGGLE %s\n", glWillBeActive ? "GL" : "GDI");
        fclose(diag);
    }
    if (gpuLinesEnabled_ && renderBackend_)
        renderBackend_->resetDiagnostics(); // taze GLDIAG verisi
    updateStatus();
    invalidateCanvas();
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

void Application::newDocument() {
    document_.clear();
    refreshLayerCombo();
    if (workPlanePicking_) cancelWorkPlaneCommand();
    if (transformCommand_ != TransformCommand::None) cancelTransformCommand();
    else cancelDrawing();
    camera_.reset();
    drawingActive_ = mode_ == EditMode::Draw2D;
    updateHover(cursorScreen_.x, cursorScreen_.y);
    updateControls();
    updateStatus();
    invalidateCanvas();
}

void Application::saveDocument() {
    const auto path = chooseFile(true); if (!path) return;
    try { document_.save(*path); } catch (const std::exception& error) { showError(L"Kaydetme başarısız", error); }
}

void Application::openDocument() {
    const auto path = chooseFile(false); if (!path) return;
    try {
        document_.load(*path);
        purgeStaleAxisLines();
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