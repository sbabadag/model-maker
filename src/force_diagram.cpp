#include "model_maker/force_diagram.hpp"
#include <algorithm>
#include <cmath>
#include <cwchar>

namespace mm {
namespace {

constexpr int kWidth  = 480;
constexpr int kHeight = 420;
constexpr int kMargin = 55;
constexpr int kTop    = 30;
constexpr int kRowH   = 85;
constexpr int kGraphW = kWidth - kMargin - 20;

void drawDiagram(HDC dc, int yBase, double vi, double vj, double maxVal,
                 const wchar_t* title, const wchar_t* unit, COLORREF color) {
    const int yMid = yBase + kRowH / 2;
    SetTextColor(dc, RGB(200, 200, 200)); SetBkMode(dc, TRANSPARENT);
    RECT tr{kMargin, yBase, kWidth - 5, yBase + 16};
    DrawTextW(dc, title, -1, &tr, DT_LEFT | DT_TOP);
    wchar_t ubuf[64]; std::swprintf(ubuf, 64, L"[%s]", unit);
    RECT ur{kWidth - 80, yBase, kWidth - 5, yBase + 16};
    DrawTextW(dc, ubuf, -1, &ur, DT_RIGHT | DT_TOP);
    HPEN axisPen = CreatePen(PS_SOLID, 1, RGB(100, 100, 100));
    SelectObject(dc, axisPen);
    MoveToEx(dc, kMargin, yMid, NULL); LineTo(dc, kWidth - 20, yMid);
    const double scale = (maxVal < 1e-12) ? 0.0 : (kRowH * 0.4) / maxVal;
    SetTextColor(dc, color);
    wchar_t vbuf[64]; std::swprintf(vbuf, 64, L"%.2f", vi);
    RECT vl{kMargin - 52, yBase + 20, kMargin - 2, yBase + 36};
    DrawTextW(dc, vbuf, -1, &vl, DT_RIGHT | DT_VCENTER);
    std::swprintf(vbuf, 64, L"%.2f", vj);
    RECT vr{kWidth - 5, yBase + 20, kWidth + 50, yBase + 36};
    DrawTextW(dc, vbuf, -1, &vr, DT_LEFT | DT_VCENTER);
    if (maxVal < 1e-12) { DeleteObject(axisPen); return; }
    const int di = static_cast<int>(-vi * scale);
    const int dj = static_cast<int>(-vj * scale);
    HPEN diagPen = CreatePen(PS_SOLID, 2, color);
    SelectObject(dc, diagPen);
    MoveToEx(dc, kMargin, yMid + di, NULL); LineTo(dc, kWidth - 20, yMid + dj);
    DeleteObject(diagPen);
    DeleteObject(axisPen);
}

LRESULT CALLBACK forceDiagramProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* data = reinterpret_cast<ForceDiagramData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return 0;
    }
    case WM_PAINT: {
        if (!data) return 0;
        PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps);
        RECT client; GetClientRect(hwnd, &client);
        HBRUSH bg = CreateSolidBrush(RGB(25, 28, 36));
        FillRect(dc, &client, bg); DeleteObject(bg);
        SetTextColor(dc, RGB(255, 206, 84)); SetBkMode(dc, TRANSPARENT);
        RECT titleRect{10, 4, kWidth - 10, 24};
        DrawTextW(dc, data->label.c_str(), -1, &titleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        HPEN lenPen = CreatePen(PS_SOLID, 1, RGB(80, 80, 80));
        SelectObject(dc, lenPen);
        MoveToEx(dc, kMargin, kTop + 8, NULL); LineTo(dc, kWidth - 20, kTop + 8);
        HPEN markPen = CreatePen(PS_SOLID, 1, RGB(200, 200, 200));
        SelectObject(dc, markPen);
        MoveToEx(dc, kMargin, kTop, NULL); LineTo(dc, kMargin, kTop + 16);
        MoveToEx(dc, kWidth - 20, kTop, NULL); LineTo(dc, kWidth - 20, kTop + 16);
        SetTextColor(dc, RGB(160, 160, 160));
        wchar_t lenBuf[64]; std::swprintf(lenBuf, 64, L"L = %.2f m", data->elementLength);
        RECT lr{kMargin, kTop + 10, kWidth - 20, kTop + 26};
        DrawTextW(dc, lenBuf, -1, &lr, DT_CENTER | DT_TOP);
        DeleteObject(lenPen); DeleteObject(markPen);
        SetTextColor(dc, RGB(140, 140, 140));
        wchar_t secBuf[128]; std::swprintf(secBuf, 128, L"Profil: %s", data->sectionName.c_str());
        RECT sr{kMargin, kTop + 40, kWidth - 20, kTop + 56};
        DrawTextW(dc, secBuf, -1, &sr, DT_CENTER | DT_TOP);
        const double maxM  = std::max({std::abs(data->momentYI), std::abs(data->momentYJ),
                                       std::abs(data->momentZI), std::abs(data->momentZJ)});
        const double maxV = std::max({std::abs(data->shearYI), std::abs(data->shearYJ),
                                       std::abs(data->shearZI), std::abs(data->shearZJ)});
        const double maxN = std::max(std::abs(data->axialI), std::abs(data->axialJ));
        drawDiagram(dc, 100, data->momentYI, data->momentYJ, maxM,
                    L"Moment Y (My)", L"kN·m", RGB(78, 148, 255));
        drawDiagram(dc, 185, data->momentZI, data->momentZJ, maxM,
                    L"Moment Z (Mz)", L"kN·m", RGB(78, 200, 255));
        drawDiagram(dc, 265, data->shearYI, data->shearYJ, maxV,
                    L"Shear Y (Vy)", L"kN", RGB(72, 211, 121));
        drawDiagram(dc, 345, data->axialI, data->axialJ, maxN,
                    L"Axial (N)", L"kN", RGB(235, 145, 82));
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: delete data; return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // anonymous

HWND createForceDiagramWindow(HINSTANCE instance, HWND parent, const ForceDiagramData& data) {
    auto* copy = new ForceDiagramData(data);
    return CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"ForceDiagramClass",
        copy->label.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, kWidth, kHeight,
        parent, nullptr, instance, copy);
}

bool registerForceDiagramClass(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = forceDiagramProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"ForceDiagramClass";
    return RegisterClassExW(&wc) != 0;
}

} // namespace mm