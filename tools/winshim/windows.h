#pragma once
// ============================================================================
// Win32 SHIM — yalnizca -fsyntax-only CAPRAZ SOZDIZIMI ICIN.
//
// Amac (ST-4 surec kurali): qt_main_window.cpp gibi WIN32-only dosyalar Linux'ta
// derlenmedigi icin GUI degisiklikleri 163 testte YAKALANMIYORDU (bir kapsam
// hatasi Windows'ta patladi). Bu baslik, model-maker'in Win32 yuzeyinin (tipler
// + kullandigimiz fonksiyon imzalari) minimal bir kopyasidir; boylece Linux'ta
// `g++ -fsyntax-only` ile o dosyalar DENETLENEBILIR.
//
// KURALLAR:
//  - Bu baslik CALISTIRILABILIR kod uretmez; yalniz sozdizimi denetimi icindir.
//  - Gercek windows.h yerine GECMEZ: burada olmayan bir Win32 sembolu
//    kullanilirsa sozdizimi denetimi hata verir (bilinçli fail-closed).
//  - Yeni bir Win32 API kullanildiginda buraya imzasini ekle.
// ============================================================================

// --- temel tipler -----------------------------------------------------------
typedef wchar_t WCHAR;
typedef char CHAR;
typedef unsigned int UINT;
typedef unsigned long DWORD;
typedef unsigned long ULONG;
typedef unsigned long long UINT_PTR;
typedef long long INT_PTR;
typedef UINT_PTR WPARAM;
typedef INT_PTR LPARAM;
typedef INT_PTR LRESULT;
typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef short SHORT;
typedef long LONG;
typedef long long LONGLONG;
typedef unsigned long long ULONGLONG;
typedef long long LONG_PTR;
typedef unsigned long long ULONG_PTR;
typedef ULONG_PTR DWORD_PTR;
typedef LONG_PTR LPARAM2_UNUSED;
typedef void* HANDLE;
typedef void* HWND;
typedef void* HINSTANCE;
typedef void* HMODULE;
typedef void* HDC;
typedef void* HBRUSH;
typedef void* HPEN;
typedef void* HFONT;
typedef void* HBITMAP;
typedef void* HGDIOBJ;
typedef void* HMENU;
typedef void* HCURSOR;
typedef void* HICON;
typedef void* HKEY;
typedef void* HRGN;
typedef void* HGLRC;
typedef void* HKL;
typedef unsigned short ATOM;
typedef void VOID;
typedef DWORD COLORREF;

typedef struct tagPOINT { LONG x; LONG y; } POINT, *LPPOINT;
typedef struct tagSIZE { LONG cx; LONG cy; } SIZE, *LPSIZE;
typedef struct tagRECT { LONG left; LONG top; LONG right; LONG bottom; } RECT, *LPRECT;
typedef struct tagMSG { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; } MSG, *LPMSG;
typedef struct tagPAINTSTRUCT {
    HDC hdc; BOOL fErase; RECT rcPaint; BOOL fRestore; BOOL fIncUpdate; BYTE rgbReserved[32];
} PAINTSTRUCT, *LPPAINTSTRUCT;
typedef struct tagBITMAPINFOHEADER {
    DWORD biSize; LONG biWidth; LONG biHeight; WORD biPlanes; WORD biBitCount;
    DWORD biCompression; DWORD biSizeImage; LONG biXPelsPerMeter; LONG biYPelsPerMeter;
    DWORD biClrUsed; DWORD biClrImportant;
} BITMAPINFOHEADER, *LPBITMAPINFOHEADER;
typedef struct tagRGBQUAD { BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved; } RGBQUAD;
typedef struct tagBITMAPINFO { BITMAPINFOHEADER bmiHeader; RGBQUAD bmiColors[1]; } BITMAPINFO, *LPBITMAPINFO;
typedef struct tagBLENDFUNCTION { BYTE BlendOp, BlendFlags, SourceConstantAlpha, AlphaFormat; } BLENDFUNCTION, *PBLENDFUNCTION;
typedef struct tagLOGFONTW {
    LONG lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
    BYTE lfItalic, lfUnderline, lfStrikeOut, lfCharSet, lfOutPrecision, lfClipPrecision,
        lfQuality, lfPitchAndFamily;
    WCHAR lfFaceName[32];
} LOGFONTW, *LPLOGFONTW;
typedef struct tagTEXTMETRICW {
    LONG tmHeight, tmAscent, tmDescent, tmInternalLeading, tmExternalLeading;
    LONG tmAveCharWidth, tmMaxCharWidth, tmWeight, tmOverhang;
    LONG tmDigitizedAspectX, tmDigitizedAspectY;
    WCHAR tmFirstChar, tmLastChar, tmDefaultChar, tmBreakChar;
    BYTE tmItalic, tmUnderlined, tmStruckOut, tmPitchAndFamily, tmCharSet;
} TEXTMETRICW, *LPTEXTMETRICW;

typedef LRESULT (*WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef BOOL (*MONITORENUMPROC)(void*, HDC, LPRECT, LPARAM);

typedef struct tagWNDCLASSEXW {
    UINT cbSize; UINT style; WNDPROC lpfnWndProc; int cbClsExtra; int cbWndExtra;
    HINSTANCE hInstance; HICON hIcon; HCURSOR hCursor; HBRUSH hbrBackground;
    const WCHAR* lpszMenuName; const WCHAR* lpszClassName; HICON hIconSm;
} WNDCLASSEXW, *LPWNDCLASSEXW;

typedef struct tagDRAWITEMSTRUCT {
    UINT CtlType; UINT CtlID; UINT itemID; UINT itemAction; UINT itemState;
    HWND hwndItem; HDC hDC; RECT rcItem; ULONG_PTR itemData;
} DRAWITEMSTRUCT, *LPDRAWITEMSTRUCT;

typedef float FLOAT;
typedef unsigned short COLOR16;

typedef struct tagEXCEPTION_RECORD {
    DWORD ExceptionCode; unsigned long ExceptionFlags; void* ExceptionRecord;
    void* ExceptionAddress; unsigned long NumberParameters;
    ULONG_PTR ExceptionInformation[15];
} EXCEPTION_RECORD, *PEXCEPTION_RECORD;
typedef struct tagEXCEPTION_POINTERS {
    PEXCEPTION_RECORD ExceptionRecord; void* ContextRecord;
} EXCEPTION_POINTERS, *PEXCEPTION_POINTERS;

typedef struct tagPIXELFORMATDESCRIPTOR {
    WORD nSize; WORD nVersion; DWORD dwFlags; BYTE iPixelType; BYTE cColorBits;
    BYTE cRedBits; BYTE cRedShift; BYTE cGreenBits; BYTE cGreenShift;
    BYTE cBlueBits; BYTE cBlueShift; BYTE cAlphaBits; BYTE cAlphaShift;
    BYTE cAccumBits; BYTE cAccumRedBits; BYTE cAccumGreenBits; BYTE cAccumBlueBits;
    BYTE cAccumAlphaBits; BYTE cDepthBits; BYTE cStencilBits; BYTE cAuxBuffers;
    BYTE iLayerType; BYTE bReserved; DWORD dwLayerMask; DWORD dwVisibleMask;
    DWORD dwDamageMask;
} PIXELFORMATDESCRIPTOR, *LPPIXELFORMATDESCRIPTOR;

typedef struct tagLOGBRUSH { UINT lbStyle; COLORREF lbColor; ULONG_PTR lbHatch; } LOGBRUSH, *LPLOGBRUSH;
typedef struct tagTRIVERTEX {
    LONG x; LONG y; COLOR16 Red; COLOR16 Green; COLOR16 Blue; COLOR16 Alpha;
} TRIVERTEX, *LPTRIVERTEX;
typedef struct tagGRADIENT_RECT { ULONG UpperLeft; ULONG LowerRight; } GRADIENT_RECT, *LPGRADIENT_RECT;

typedef struct tagCREATESTRUCTW {
    void* lpCreateParams; HINSTANCE hInstance; HMENU hMenu; HWND hwndParent;
    int cy; int cx; int y; int x; LONG style; const WCHAR* lpszName; const WCHAR* lpszClass;
    DWORD dwExStyle;
} CREATESTRUCTW, *LPCREATESTRUCTW;

// --- sabitler / makrolar ----------------------------------------------------
#define WINAPI
#define CALLBACK
#define APIENTRY
#define WINUSERAPI
#define TRUE 1
#define FALSE 0
#define NULL_HANDLE nullptr
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r)) | ((WORD)((BYTE)(g)) << 8) | (((DWORD)(BYTE)(b)) << 16)))
#define GetRValue(rgb) ((BYTE)(rgb))
#define GetGValue(rgb) ((BYTE)(((WORD)(rgb)) >> 8))
#define GetBValue(rgb) ((BYTE)((rgb) >> 16))
#define MAKEWORD(a, b) ((WORD)(((BYTE)(a)) | ((WORD)((BYTE)(b))) << 8))
#define MAKELONG(a, b) ((LONG)(((WORD)(a)) | ((DWORD)((WORD)(b))) << 16))
#define LOWORD(l) ((WORD)((DWORD_PTR)(l) & 0xffff))
#define HIWORD(l) ((WORD)(((DWORD_PTR)(l) >> 16) & 0xffff))
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#define MAKEINTRESOURCEW(i) ((const WCHAR*)((ULONG_PTR)((WORD)(i))))
#define MAKEINTRESOURCE MAKEINTRESOURCEW
#define LPCTSTR const WCHAR*
#define LPCWSTR const WCHAR*
#define LPWSTR WCHAR*
#define LPTSTR WCHAR*
#define TEXT(x) L##x

#define WM_NULL 0
#define WM_CREATE 1
#define WM_DESTROY 2
#define WM_MOVE 3
#define WM_SIZE 5
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_ERASEBKGND 0x0014
#define WM_SHOWWINDOW 0x0018
#define WM_SETCURSOR 0x0020
#define WM_GETMINMAXINFO 0x0024
#define WM_WINDOWPOSCHANGED 0x0047
#define WM_NOTIFY 0x004E
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_TIMER 0x0113
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_RBUTTONDBLCLK 0x0206
#define WM_MBUTTONDOWN 0x0207
#define WM_MBUTTONUP 0x0208
#define WM_MOUSEWHEEL 0x020A
#define WM_MOUSELEAVE 0x02A3
#define WM_SETTINGCHANGE 0x001A
#define WM_COMMAND 0x0111
#define WM_CTLCOLORSTATIC 0x0138
#define WM_DPICHANGED 0x02E0
#define WM_APP 0x8000
#define WM_NCCREATE 0x0081
#define WM_NCDESTROY 0x0082
#define WM_NCHITTEST 0x0084
#define WM_SETFONT 0x0030
#define WM_SETFOCUS 0x0007
#define WM_SYSKEYDOWN 0x0104
#define WM_CAPTURECHANGED 0x0215
#define WM_CTLCOLORBTN 0x0135
#define WM_CTLCOLOREDIT 0x0133
#define WM_CTLCOLORLISTBOX 0x0134
#define WM_DRAWITEM 0x002B
#define WM_GETDLGCODE 0x0087
#define WM_KEYFIRST 0x0100
#define WM_KEYLAST 0x0108
#define WM_MOUSEFIRST 0x0200
#define WM_MOUSELAST 0x020D
#define WS_OVERLAPPEDWINDOW 0x00CF0000L
#define WS_CHILD 0x40000000L
#define WS_VISIBLE 0x10000000L
#define WS_CLIPCHILDREN 0x02000000L
#define WS_CLIPSIBLINGS 0x04000000L
#define WS_TABSTOP 0x00010000L
#define WS_POPUP 0x80000000L
#define WS_BORDER 0x00800000L
#define WS_VSCROLL 0x00200000L
#define WS_THICKFRAME 0x00040000L
#define WS_GROUP 0x00020000L
#define WS_EX_CLIENTEDGE 0x00000200L
#define WS_EX_NOACTIVATE 0x08000000L
#define WS_EX_TOOLWINDOW 0x00000080L
#define CS_HREDRAW 0x0002
#define CS_VREDRAW 0x0001
#define CS_OWNDC 0x0020
#define CS_DBLCLKS 0x0008
#define SW_HIDE 0
#define SW_SHOW 5
#define SW_SHOWNORMAL 1
#define SW_SHOWMAXIMIZED 3
#define SW_MAXIMIZE 3
#define SW_SHOWNOACTIVATE 4
#define GWLP_USERDATA (-21)
#define GWLP_WNDPROC (-4)
#define GWL_STYLE (-16)
#define GWL_EXSTYLE (-20)
#define GWL_USERDATA (-21)
#define IDC_ARROW MAKEINTRESOURCEW(32512)
#define IDC_CROSS MAKEINTRESOURCEW(32515)
#define IDC_HAND MAKEINTRESOURCEW(32649)
#define MB_OK 0x0
#define MB_ICONWARNING 0x30
#define MB_ICONERROR 0x10
#define MB_ICONINFORMATION 0x40
#define MB_YESNO 0x4
#define IDYES 6
#define IDNO 7
#define SRCCOPY 0x00CC0020
#define PATCOPY 0x00F00021
#define BLACKNESS 0x00000042
#define WHITENESS 0x00FF0062
#define AC_SRC_OVER 0x00
#define AC_SRC_ALPHA 0x01
#define NULL_BRUSH 5
#define NULL_PEN 8
#define HOLLOW_BRUSH 5
#define PS_SOLID 0
#define PS_DASH 1
#define PS_DOT 2
#define PS_DASHDOT 3
#define TRANSPARENT 1
#define OPAQUE 2
#define ANSI_CHARSET 0
#define DEFAULT_CHARSET 1
#define CLEARTYPE_QUALITY 5
#define ANTIALIASED_QUALITY 4
#define DEFAULT_GUI_FONT 17
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define MONITOR_DEFAULTTONEAREST 2
#define VK_ESCAPE 0x1B
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_DELETE 0x2E
#define VK_TAB 0x09
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_F13 0x7C
#define VK_F24 0x87
#define VK_RETURN 0x0D
#define VK_SPACE 0x20
#define VK_BACK 0x08
#define VK_HOME 0x24
#define VK_END 0x23
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_INSERT 0x2D
#define VK_LEFT 0x25
#define VK_RIGHT 0x27
#define VK_UP 0x26
#define VK_DOWN 0x28
#define MK_LBUTTON 0x0001
#define MK_RBUTTON 0x0002
#define MK_SHIFT 0x0004
#define MK_CONTROL 0x0008
#define MK_MBUTTON 0x0010
#define GWL_STYLE_ 0
#define SWP_NOZORDER 0x0004
#define SWP_NOMOVE 0x0002
#define SWP_NOSIZE 0x0001
#define SWP_NOACTIVATE 0x0010
#define SWP_SHOWWINDOW 0x0040
#define BS_PUSHBUTTON 0x00000000L
#define HWND_TOP ((HWND)0)
#define WM_USER 0x0400
#define CP_UTF8 65001
#define MAX_PATH 260
#define INFINITE 0xFFFFFFFF
#define GW_HWNDFIRST 0
#define GW_HWNDNEXT 2
#define HTCLIENT 1
#define HWND_DESKTOP ((HWND)0)
#define HWND_TOPMOST ((HWND)-1)
#define IDI_APPLICATION MAKEINTRESOURCEW(32512)
#define ERROR_CLASS_ALREADY_EXISTS 1410
#define FW_LIGHT 300
#define WHEEL_DELTA 120
#define DC_BRUSH 18
#define DLGC_WANTTAB 0x0002
#define DT_NOPREFIX 0x0800
#define PM_REMOVE 0x0001
#define MAKELPARAM(l, h) ((LPARAM)(DWORD)(((WORD)(l)) | ((DWORD)((WORD)(h))) << 16))
#define GET_WHEEL_DELTA_WPARAM(w) ((short)HIWORD(w))

// --- GDI / font / drawtext sabitleri (renderer, gdi, view_cube, force_diagram) ---
#define PS_GEOMETRIC 0x00010000
#define PS_USERSTYLE 0x00000007
#define PS_ENDCAP_FLAT 0x00000200
#define PS_JOIN_ROUND 0x00000000
#define HS_FDIAGONAL 2
#define BS_SOLID 0
#define GRAY_BRUSH 2
#define HALFTONE 4
#define COLORONCOLOR 3
#define OBJ_PEN 1
#define OBJ_BITMAP 7
#define FW_NORMAL 400
#define FW_SEMIBOLD 600
#define OUT_DEFAULT_PRECIS 0
#define OUT_TT_PRECIS 4
#define CLIP_DEFAULT_PRECIS 0
#define DEFAULT_PITCH 0
#define FF_DONTCARE 0
#define DT_LEFT 0x00000000
#define DT_TOP 0x00000000
#define DT_RIGHT 0x00000002
#define DT_CENTER 0x00000001
#define DT_VCENTER 0x00000004
#define DT_SINGLELINE 0x00000020
// --- pencere stilleri (force_diagram) ---
#define WS_CAPTION 0x00C00000L
#define WS_SYSMENU 0x00080000L
#define WS_EX_DLGMODALFRAME 0x00000001L
#define WS_EX_TOPMOST 0x00000008L
#define CW_USEDEFAULT ((int)0x80000000)
#define COLOR_WINDOW 5
// --- piksel formati / DIB (opengl_render_backend) ---
#define PFD_DRAW_TO_WINDOW 0x00000004
#define PFD_SUPPORT_OPENGL 0x00000020
#define PFD_TYPE_RGBA 0
#define PFD_MAIN_PLANE 0
#define BI_RGB 0
#define DIB_RGB_COLORS 0
#define GRADIENT_FILL_RECT_V 0x00000001
#define EXCEPTION_EXECUTE_HANDLER 1

// --- fonksiyon imzalari (yalniz sozdizimi) ---------------------------------
extern "C" {
HWND CreateWindowExW(DWORD, const WCHAR*, const WCHAR*, DWORD, int, int, int, int,
                     HWND, HMENU, HINSTANCE, void*);
BOOL DestroyWindow(HWND);
BOOL ShowWindow(HWND, int);
BOOL UpdateWindow(HWND);
BOOL GetClientRect(HWND, LPRECT);
BOOL GetWindowRect(HWND, LPRECT);
BOOL MoveWindow(HWND, int, int, int, int, BOOL);
BOOL SetWindowPos(HWND, HWND, int, int, int, int, UINT);
HWND SetParent(HWND, HWND);
BOOL InvalidateRect(HWND, const RECT*, BOOL);
BOOL ValidateRect(HWND, const RECT*);
HWND SetFocus(HWND);
HWND GetFocus();
BOOL SetCapture(HWND);
BOOL ReleaseCapture();
BOOL ScreenToClient(HWND, LPPOINT);
BOOL ClientToScreen(HWND, LPPOINT);
BOOL GetCursorPos(LPPOINT);
LONG_PTR GetWindowLongPtrW(HWND, int);
LONG_PTR SetWindowLongPtrW(HWND, int, LONG_PTR);
BOOL SetWindowTextW(HWND, const WCHAR*);
int GetWindowTextW(HWND, WCHAR*, int);
BOOL SetTimer(HWND, UINT_PTR, UINT, void (*)(HWND, UINT, UINT_PTR, DWORD));
BOOL KillTimer(HWND, UINT_PTR);
LRESULT SendMessageW(HWND, UINT, WPARAM, LPARAM);
BOOL PostMessageW(HWND, UINT, WPARAM, LPARAM);
LRESULT DefWindowProcW(HWND, UINT, WPARAM, LPARAM);
ATOM RegisterClassExW(const WNDCLASSEXW*);
HINSTANCE GetModuleHandleW(const WCHAR*);
int GetSystemMetrics(int);
HCURSOR LoadCursorW(HINSTANCE, const WCHAR*);
HCURSOR SetCursor(HCURSOR);
int MessageBoxW(HWND, const WCHAR*, const WCHAR*, UINT);
BOOL MessageBeep(UINT);
HDC GetDC(HWND);
int ReleaseDC(HWND, HDC);
HDC BeginPaint(HWND, LPPAINTSTRUCT);
BOOL EndPaint(HWND, const PAINTSTRUCT*);
HDC CreateCompatibleDC(HDC);
HBITMAP CreateCompatibleBitmap(HDC, int, int);
HDC CreateCompatibleDC_(HDC);
HGDIOBJ SelectObject(HDC, HGDIOBJ);
BOOL DeleteObject(HGDIOBJ);
BOOL DeleteDC(HDC);
HBRUSH CreateSolidBrush(COLORREF);
HPEN CreatePen(int, int, COLORREF);
HGDIOBJ GetStockObject(int);
HFONT CreateFontW(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD,
                  DWORD, const WCHAR*);
BOOL TextOutW(HDC, int, int, const WCHAR*, int);
int DrawTextW(HDC, const WCHAR*, int, LPRECT, UINT);
int SetBkMode(HDC, int);
COLORREF SetTextColor(HDC, COLORREF);
COLORREF SetBkColor(HDC, COLORREF);
BOOL MoveToEx(HDC, int, int, LPPOINT);
BOOL LineTo(HDC, int, int);
BOOL Rectangle(HDC, int, int, int, int);
BOOL Ellipse(HDC, int, int, int, int);
BOOL Polygon(HDC, const POINT*, int);
BOOL Polyline(HDC, const POINT*, int);
BOOL PolyPolyline(HDC, const POINT*, const DWORD*, DWORD);
BOOL BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD);
BOOL StretchBlt(HDC, int, int, int, int, HDC, int, int, int, int, DWORD);
BOOL AlphaBlend(HDC, int, int, int, int, HDC, int, int, int, int, BLENDFUNCTION);
int SetStretchBltMode(HDC, int);
BOOL GetTextExtentPoint32W(HDC, const WCHAR*, int, LPSIZE);
BOOL SetRect(LPRECT, int, int, int, int);
int GetDeviceCaps(HDC, int);
HGDIOBJ GetCurrentObject(HDC, UINT);
int GetObjectW(HGDIOBJ, int, void*);
UINT GetDpiForWindow(HWND);
BOOL AdjustWindowRectEx(LPRECT, DWORD, BOOL, DWORD);
DWORD GetLastError();
void OutputDebugStringW(const WCHAR*);
HMODULE LoadLibraryW(const WCHAR*);
void* GetProcAddress(HMODULE, const char*);
BOOL FreeLibrary(HMODULE);
BOOL SwapBuffers(HDC);
HGLRC wglCreateContext(HDC);
BOOL wglMakeCurrent(HDC, HGLRC);
BOOL wglDeleteContext(HGLRC);
void* wglGetProcAddress(const char*);
BOOL ChoosePixelFormat(HDC, const void*);
int SetPixelFormat(HDC, int, const void*);
BOOL RemoveMenu(HMENU, UINT, UINT);
BOOL DeleteMenu(HMENU, UINT, UINT);
HMENU CreatePopupMenu();
BOOL AppendMenuW(HMENU, UINT, UINT_PTR, const WCHAR*);
BOOL TrackPopupMenu(HMENU, UINT, int, int, int, HWND, const RECT*);
BOOL DestroyMenu(HMENU);
int GetMenuItemCount(HMENU);
BOOL SetForegroundWindow(HWND);
// --- exception / error / misc (main, application, two_point_view) ---
void* SetUnhandledExceptionFilter(LONG (*)(EXCEPTION_POINTERS*));
int MessageBoxA(HWND, const char*, const char*, UINT);
BOOL IsWindow(HWND);
// --- mesaj dongusu / giris (application) ---
BOOL GetMessageW(MSG*, HWND, UINT, UINT);
BOOL PeekMessageW(MSG*, HWND, UINT, UINT, UINT);
BOOL TranslateMessage(const MSG*);
LRESULT DispatchMessageW(const MSG*);
void PostQuitMessage(int);
void Sleep(DWORD);
SHORT GetKeyState(int);
HWND GetCapture();
HCURSOR GetCursor();
HCURSOR CreateCursor(HINSTANCE, int, int, int, int, const void*, const void*);
BOOL DestroyCursor(HCURSOR);
HICON LoadIconW(HINSTANCE, const WCHAR*);
BOOL IsWindowVisible(HWND);
HWND GetParent(HWND);
HWND GetDlgItem(HWND, int);
int GetDlgCtrlID(HWND);
DWORD GetModuleFileNameW(HMODULE, WCHAR*, DWORD);
DWORD GetEnvironmentVariableW(const WCHAR*, WCHAR*, DWORD);
DWORD GetTickCount();
ULONGLONG GetTickCount64();
UINT GetDoubleClickTime();
LONG GetMessageTime();
int MultiByteToWideChar(UINT, DWORD, const char*, int, WCHAR*, int);
int WideCharToMultiByte(UINT, DWORD, const WCHAR*, int, char*, int, const char*, BOOL*);
BOOL OffsetRect(LPRECT, int, int);
BOOL InflateRect(LPRECT, int, int);
BOOL DrawFocusRect(HDC, const RECT*);
int MapWindowPoints(HWND, HWND, LPPOINT, UINT);
// --- ANSI / process / GDI (opengl_render_backend) ---
HWND CreateWindowExA(DWORD, const char*, const char*, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, void*);
HMODULE LoadLibraryA(const char*);
HANDLE GetCurrentProcess();
DWORD GetGuiResources(HANDLE, UINT);
int DescribePixelFormat(HDC, int, UINT, void*);
HBITMAP CreateDIBSection(HDC, const BITMAPINFO*, UINT, void**, HANDLE, DWORD);
// --- GDI cizim (renderer, gdi_render_backend, view_cube_renderer, force_diagram) ---
int FillRect(HDC, const RECT*, HBRUSH);
int FrameRect(HDC, const RECT*, HBRUSH);
COLORREF SetPixel(HDC, int, int, COLORREF);
BOOL SetBrushOrgEx(HDC, int, int, LPPOINT);
HPEN ExtCreatePen(DWORD, DWORD, const LOGBRUSH*, DWORD, const DWORD*);
HBRUSH CreateHatchBrush(int, COLORREF);
BOOL GradientFill(HDC, TRIVERTEX*, ULONG, void*, ULONG, ULONG);
BOOL SetViewportOrgEx(HDC, int, int, LPPOINT);
BOOL RoundRect(HDC, int, int, int, int, int, int);
}
