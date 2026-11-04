#pragma once
// commctrl.h SHIM — -fsyntax-only icin (bkz. windows.h basligindaki aciklama).
#include <windows.h>

typedef void* HTREEITEM;
typedef void* HIMAGELIST;
typedef void* HWND__;
typedef struct tagNMHDR { HWND hwndFrom; UINT_PTR idFrom; UINT code; } NMHDR, *LPNMHDR;
typedef struct tagINITCOMMONCONTROLSEX { DWORD dwSize; DWORD dwICC; } INITCOMMONCONTROLSEX;
typedef struct tagTVITEMW {
    UINT mask; HTREEITEM hItem; UINT state; UINT stateMask; WCHAR* pszText;
    int cchTextMax; int iImage; int iSelectedImage; int cChildren; LPARAM lParam;
} TVITEMW, *LPTVITEMW;
typedef struct tagTVINSERTSTRUCTW {
    HTREEITEM hParent; HTREEITEM hInsertAfter; TVITEMW item;
} TVINSERTSTRUCTW, *LPTVINSERTSTRUCTW;
typedef struct tagTCITEMW {
    UINT mask; DWORD dwState; DWORD dwStateMask; WCHAR* pszText; int cchTextMax;
    int iImage; LPARAM lParam;
} TCITEMW, *LTCITEMW;
typedef struct tagLVITEMW {
    UINT mask; int iItem; int iSubItem; UINT state; UINT stateMask; WCHAR* pszText;
    int cchTextMax; int iImage; LPARAM lParam; int iIndent;
} LVITEMW, *LPLVITEMW;

typedef struct tagLVCOLUMNW {
    UINT mask; int fmt; int cx; WCHAR* pszText; int cchTextMax;
    int iSubItem; int iImage; int iOrder;
} LVCOLUMNW, *LPLVCOLUMNW;
typedef struct tagTOOLINFOW {
    UINT cbSize; UINT uFlags; HWND hwnd; UINT_PTR uId; RECT rect;
    HINSTANCE hinst; WCHAR* lpszText; LPARAM lParam;
} TOOLINFOW, *LPTOOLINFOW;

#define TVS_HASBUTTONS 0x0001
#define TVS_HASLINES 0x0002
#define TVS_LINESATROOT 0x0004
#define TVS_SHOWSELALWAYS 0x0020
#define TVIF_TEXT 0x0001
#define TVIF_PARAM 0x0004
#define TVIF_STATE 0x0008
#define TVI_ROOT ((HTREEITEM)0)
#define TVI_FIRST ((HTREEITEM)0xFFFF0001)
#define TVI_LAST ((HTREEITEM)0xFFFF0002)
#define TVM_INSERTITEMW (WM_USER + 50)
#define TVM_DELETEITEM (WM_USER + 1)
#define TVM_SETIMAGELIST (WM_USER + 9)
#define TVM_EXPAND (WM_USER + 2)
#define TVE_EXPAND 2
#define TVE_COLLAPSE 1
#define TVGN_CARET 9
#define TVM_SELECTITEM (WM_USER + 11)
#define TVM_GETNEXTITEM (WM_USER + 10)
#define TVM_GETITEMW (WM_USER + 62)
#define TVM_SETITEMW (WM_USER + 63)
#define TVM_GETCOUNT (WM_USER + 5)
#define TCM_INSERTITEMW (WM_USER + 7)
#define TCM_GETITEMCOUNT (WM_USER + 4)
#define TCM_GETCURSEL (WM_USER + 11)
#define TCM_SETCURSEL (WM_USER + 12)
#define TCM_DELETEALLITEMS (WM_USER + 9)
#define TCIF_TEXT 0x0001
#define LVM_INSERTCOLUMNW (WM_USER + 97)
#define LVM_INSERTITEMW (WM_USER + 77)
#define LVM_SETITEMTEXTW (WM_USER + 116)
#define LVM_DELETEALLITEMS (WM_USER + 9)
#define LVCF_TEXT 0x0004
#define LVCF_WIDTH 0x0002
#define LVCF_FMT 0x0001
#define LVIF_TEXT 0x0001
#define LVIF_PARAM 0x0004
#define ICC_TREEVIEW_CLASSES 0x00000002
#define ICC_LISTVIEW_CLASSES 0x00000001
#define ICC_TAB_CLASSES 0x00000008
#define ICC_STANDARD_CLASSES 0x00004000
#define ICC_WIN95_CLASSES 0x000000FF
#define BST_CHECKED 0x0001
#define BST_UNCHECKED 0x0000
#define BM_SETCHECK 0x00F1
#define BM_GETCHECK 0x00F0
#define TBM_SETPOS 0x0405
#define TBM_GETPOS 0x0400
#define TBS_AUTOTICKS 0x0001
#define TVM_SETITEMHEIGHT (WM_USER + 27)
#define TVS_SHOWSELALWAYS_ 0
#define TV_FIRST 0x1100
#define TOOLBARCLASSNAMEW L"ToolbarWindow32"

// --- listview mesajlari / stilleri ---
#define LVM_FIRST 0x1000
#define LVM_GETITEMCOUNT (LVM_FIRST + 4)
#define LVM_SETBKCOLOR (LVM_FIRST + 1)
#define LVM_SETTEXTBKCOLOR (LVM_FIRST + 38)
#define LVM_SETTEXTCOLOR (LVM_FIRST + 36)
#define LVM_SETEXTENDEDLISTVIEWSTYLE (LVM_FIRST + 54)
#define LVS_REPORT 0x0001
#define LVS_SINGLESEL 0x0004
#define LVS_SHOWSELALWAYS 0x0008
#define LVS_EX_GRIDLINES 0x00000001
#define LVS_EX_FULLROWSELECT 0x00000020
#define LVS_EX_DOUBLEBUFFER 0x00010000
#define ListView_GetItemCount(hwnd) (int)SendMessageW((hwnd), LVM_GETITEMCOUNT, 0, 0)
#define ListView_DeleteAllItems(hwnd) (BOOL)SendMessageW((hwnd), LVM_DELETEALLITEMS, 0, 0)
#define ListView_InsertItem(hwnd, pitem) (int)SendMessageW((hwnd), LVM_INSERTITEMW, 0, (LPARAM)(pitem))
#define ListView_SetItemText(hwnd, i, sub, text) (BOOL)SendMessageW((hwnd), LVM_SETITEMTEXTW, (i), (LPARAM)(text))
#define ListView_InsertColumn(hwnd, i, pcol) (int)SendMessageW((hwnd), LVM_INSERTCOLUMNW, (i), (LPARAM)(pcol))
#define ListView_SetExtendedListViewStyle(hwnd, ex) (void)SendMessageW((hwnd), LVM_SETEXTENDEDLISTVIEWSTYLE, 0, (ex))
#define ListView_SetBkColor(hwnd, c) (BOOL)SendMessageW((hwnd), LVM_SETBKCOLOR, 0, (c))
#define ListView_SetTextBkColor(hwnd, c) (BOOL)SendMessageW((hwnd), LVM_SETTEXTBKCOLOR, 0, (c))
#define ListView_SetTextColor(hwnd, c) (BOOL)SendMessageW((hwnd), LVM_SETTEXTCOLOR, 0, (c))
#define WC_LISTVIEWW L"SysListView32"

// --- combobox ---
#define CBS_DROPDOWNLIST 0x0003
#define CBS_HASSTRINGS 0x0200
#define CB_ADDSTRING 0x0143
#define CB_SETCURSEL 0x014E
#define CB_GETCURSEL 0x0147
#define CB_RESETCONTENT 0x014B
#define CB_FINDSTRINGEXACT 0x0158
#define CB_ERR (-1)
#define CBN_SELCHANGE 1
#define WC_COMBOBOXW L"ComboBox"

// --- listbox ---
#define LBS_NOINTEGRALHEIGHT 0x0100
#define LBS_NOTIFY 0x0001
#define LB_ADDSTRING 0x0180
#define LB_GETCURSEL 0x0188
#define LB_RESETCONTENT 0x0184
#define LBN_SELCHANGE 1
#define LBN_DBLCLK 2

// --- button / edit ---
#define BS_AUTOCHECKBOX 0x0003
#define BS_AUTORADIOBUTTON 0x0009
#define BS_MULTILINE 0x2000
#define BS_OWNERDRAW 0x000B
#define BS_PUSHLIKE 0x1000
#define BS_TYPEMASK 0x000F
#define BN_CLICKED 0
#define ES_AUTOHSCROLL 0x0080
#define EN_CHANGE 0x0300
#define EM_SETCUEBANNER 0x1501
#define SS_LEFT 0x0000
#define SS_CENTERIMAGE 0x0200
#define ODS_SELECTED 0x0001
#define ODS_DISABLED 0x0004
#define ODS_FOCUS 0x0010
#define ODT_BUTTON 4

// --- tooltip ---
#define TOOLTIPS_CLASSW L"tooltips_class32"
#define TTS_NOPREFIX 0x02
#define TTS_ALWAYSTIP 0x01
#define TTF_IDISHWND 0x0001
#define TTF_SUBCLASS 0x0010
#define TTM_ADDTOOLW (WM_USER + 50)
#define TTM_SETMAXTIPWIDTH (WM_USER + 24)

// --- progress ---
#define PROGRESS_CLASSW L"msctls_progress32"
#define PBS_SMOOTH 0x01
#define PBM_SETRANGE32 (WM_USER + 6)
#define PBM_SETPOS (WM_USER + 2)
#define ICC_PROGRESS_CLASS 0x00000020

// --- menu (TPM / MF) ---
#define TPM_LEFTALIGN 0x0000
#define TPM_TOPALIGN 0x0000
#define TPM_RIGHTBUTTON 0x0002
#define TPM_RETURNCMD 0x0100
#define TPM_NONOTIFY 0x0080
#define MF_STRING 0x0000
#define MF_SEPARATOR 0x0800
#define MF_CHECKED 0x0008
#define MF_GRAYED 0x0001

static inline void InitCommonControlsEx_Noop(const INITCOMMONCONTROLSEX*) {}
#define InitCommonControlsEx(p) InitCommonControlsEx_Noop(p)
