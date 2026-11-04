#pragma once
// windowsx.h SHIM — -fsyntax-only icin.
#include <windows.h>
#define GET_WM_COMMAND_ID(wp,lp) ((UINT)(wp))
#define GET_WM_COMMAND_CMD(wp,lp) ((UINT)(HIWORD(lp)))
#define GET_WM_COMMAND_HWND(wp,lp) ((HWND)(lp))
// GET_X_LPARAM / GET_Y_LPARAM windows.h'da tanimli (LOWORD/HIWORD tabanli).
