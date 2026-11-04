#pragma once
// commdlg.h SHIM — -fsyntax-only icin (bkz. windows.h basligindaki aciklama).
#include <windows.h>

typedef struct tagOPENFILENAMEW {
    DWORD lStructSize; HWND hwndOwner; HINSTANCE hInstance;
    const WCHAR* lpstrFilter; WCHAR* lpstrCustomFilter; DWORD nMaxCustFilter;
    DWORD nFilterIndex; WCHAR* lpstrFile; DWORD nMaxFile; WCHAR* lpstrFileTitle;
    DWORD nMaxFileTitle; const WCHAR* lpstrInitialDir; const WCHAR* lpstrTitle;
    DWORD Flags; WORD nFileOffset; WORD nFileExtension; const WCHAR* lpstrDefExt;
    LPARAM lCustData; void* lpfnHook; const WCHAR* lpTemplateName;
} OPENFILENAMEW, *LPOPENFILENAMEW;

#define OFN_PATHMUSTEXIST 0x00000800
#define OFN_FILEMUSTEXIST 0x00001000
#define OFN_OVERWRITEPROMPT 0x00000002

extern "C" {
BOOL GetOpenFileNameW(OPENFILENAMEW*);
BOOL GetSaveFileNameW(OPENFILENAMEW*);
}
