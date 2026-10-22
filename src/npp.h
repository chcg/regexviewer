#pragma once
// The parts of the Notepad++ plugin ABI and Scintilla API Regex Viewer uses.
// Values checked against Notepad_plus_msgs.h, menuCmdID.h, Docking.h and
// Scintilla.h from the Notepad++ repository.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>

struct NppData {
    HWND _nppHandle;
    HWND _scintillaMainHandle;
    HWND _scintillaSecondHandle;
};
struct ShortcutKey { bool _isCtrl, _isAlt, _isShift; UCHAR _key; };
typedef void (*PFUNCPLUGINCMD)();
struct FuncItem {
    wchar_t        _menuItemName[64];
    PFUNCPLUGINCMD _pFunc;
    int            _cmdID;
    bool           _init2Check;
    ShortcutKey*   _pShKey;
};

// Scintilla notification (full layout, Scintilla 5)
struct SCNotification {
    NMHDR     nmhdr;
    intptr_t  position;
    int       ch;
    int       modifiers;
    int       modificationType;
    const char* text;
    intptr_t  length;
    intptr_t  linesAdded;
    int       message;
    uintptr_t wParam;
    intptr_t  lParam;
    intptr_t  line;
    int       foldLevelNow;
    int       foldLevelPrev;
    int       margin;
    int       listType;
    int       x;
    int       y;
    int       token;
    intptr_t  annotationLinesAdded;
    int       updated;
    int       listCompletionMethod;
    int       characterSource;
};

struct toolbarIconsWithDarkMode { HBITMAP hToolbarBmp; HICON hToolbarIcon; HICON hToolbarIconDarkMode; };

struct DockedWidgetData {
    HWND           hClient = nullptr;
    const wchar_t* pszName = nullptr;
    int            dlgID = 0;
    UINT           uMask = 0;
    HICON          hIconTab = nullptr;
    const wchar_t* pszAddInfo = nullptr;
    RECT           rcFloat = {};
    int            iPrevCont = 0;
    const wchar_t* pszModuleName = nullptr;
};

enum : UINT {
    NPPMSG                          = WM_USER + 1000,
    NPPM_GETCURRENTSCINTILLA        = NPPMSG + 4,
    NPPM_MODELESSDIALOG             = NPPMSG + 12,
    NPPM_CREATESCINTILLAHANDLE      = NPPMSG + 20,
    NPPM_ACTIVATEDOC                = NPPMSG + 28,
    NPPM_DMMSHOW                    = NPPMSG + 30,
    NPPM_DMMHIDE                    = NPPMSG + 31,
    NPPM_DMMREGASDCKDLG             = NPPMSG + 33,
    NPPM_SETMENUITEMCHECK           = NPPMSG + 40,
    NPPM_MENUCOMMAND                = NPPMSG + 48,
    NPPM_GETBUFFERIDFROMPOS         = NPPMSG + 59,
    NPPM_GETCURRENTBUFFERID         = NPPMSG + 60,
    NPPM_DOOPEN                     = NPPMSG + 77,
    NPPM_GETNBOPENFILES             = NPPMSG + 7,
    NPPM_GETEDITORDEFAULTFOREGROUNDCOLOR = NPPMSG + 90,
    NPPM_GETEDITORDEFAULTBACKGROUNDCOLOR = NPPMSG + 91,
    NPPM_ADDTOOLBARICON_FORDARKMODE = NPPMSG + 101,
    NPPM_ISDARKMODEENABLED          = NPPMSG + 107,
    NPPM_DARKMODESUBCLASSANDTHEME   = NPPMSG + 112,
    NPPM_ALLOCATEINDICATOR          = NPPMSG + 113,
    NPPM_GETFILENAME                = WM_USER + 3000 + 3,

    NPPN_READY             = 1001,
    NPPN_TBMODIFICATION    = 1002,
    NPPN_FILECLOSED        = 1005,
    NPPN_SHUTDOWN          = 1009,
    NPPN_BUFFERACTIVATED   = 1010,
    NPPN_DARKMODECHANGED   = 1027,

    MODELESSDIALOGADD      = 0,
    SECOND_VIEW            = 2,
    DMN_CLOSE              = 1050 + 1,
    DWS_ICONTAB            = 0x00000001,
    DWS_DF_CONT_BOTTOM     = 3u << 28,
    DMF_INIT               = 0x0000000B,
    DMF_HANDLE_CHANGE      = 0x0000000C,

    IDM_VIEW_GOTO_ANOTHER_VIEW = 10001,
};

enum : UINT {
    SCI_GETLENGTH          = 2006,
    SCI_GETCURRENTPOS      = 2008,
    SCI_GOTOPOS            = 2025,
    SCI_STARTSTYLING       = 2032,
    SCI_SETSTYLING         = 2033,
    SCI_SETCODEPAGE        = 2037,
    SCI_STYLECLEARALL      = 2050,
    SCI_STYLESETFORE       = 2051,
    SCI_STYLESETBACK       = 2052,
    SCI_STYLESETBOLD       = 2053,
    SCI_STYLESETITALIC     = 2054,
    SCI_STYLESETSIZE       = 2055,
    SCI_STYLESETFONT       = 2056,
    SCI_SETSELFORE         = 2067,
    SCI_SETSELBACK         = 2068,
    SCI_SETCARETFORE       = 2069,
    SCI_BEGINUNDOACTION    = 2078,
    SCI_ENDUNDOACTION      = 2079,
    SCI_INDICSETSTYLE      = 2080,
    SCI_INDICSETFORE       = 2082,
    SCI_SETCARETLINEVISIBLE = 2096,
    SCI_SETHSCROLLBAR      = 2130,
    SCI_GETCODEPAGE        = 2137,
    SCI_GETSELECTIONSTART  = 2143,
    SCI_GETSELECTIONEND    = 2145,
    SCI_GETFIRSTVISIBLELINE = 2152,
    SCI_GETLINECOUNT       = 2154,
    SCI_SETSEL             = 2160,
    SCI_GETSELTEXT         = 2161,
    SCI_LINEFROMPOSITION   = 2166,
    SCI_POSITIONFROMLINE   = 2167,
    SCI_SCROLLCARET        = 2169,
    SCI_SETREADONLY        = 2171,
    SCI_EMPTYUNDOBUFFER    = 2175,
    SCI_SETTEXT            = 2181,
    SCI_GETTEXT            = 2182,
    SCI_REPLACETARGET      = 2194,
    SCI_DOCLINEFROMVISIBLE = 2221,
    SCI_ENSUREVISIBLE      = 2232,
    SCI_SETMARGINWIDTHN    = 2242,
    SCI_SETWRAPMODE        = 2268,
    SCI_SETVSCROLLBAR      = 2280,
    SCI_LINESONSCREEN      = 2370,
    SCI_GRABFOCUS          = 2400,
    SCI_SETINDICATORCURRENT = 2500,
    SCI_INDICATORFILLRANGE = 2504,
    SCI_INDICATORCLEARRANGE = 2505,
    SCI_INDICSETUNDER      = 2510,
    SCI_SETSCROLLWIDTHTRACKING = 2516,
    SCI_GETCHARACTERPOINTER = 2520,
    SCI_INDICSETALPHA      = 2523,
    SCI_INDICSETOUTLINEALPHA = 2558,
    SCI_SETTARGETRANGE     = 2686,

    SCN_UPDATEUI           = 2007,
    SCN_MODIFIED           = 2008,
    SC_MOD_INSERTTEXT      = 0x1,
    SC_MOD_DELETETEXT      = 0x2,
    SC_UPDATE_CONTENT      = 0x1,
    SC_UPDATE_V_SCROLL     = 0x4,
    STYLE_DEFAULT          = 32,
    SC_CP_UTF8             = 65001,
    INDIC_ROUNDBOX         = 7,
    INDIC_STRAIGHTBOX      = 8,
    INDIC_FULLBOX          = 16,
};

extern NppData   g_npp;
extern HINSTANCE g_module;
