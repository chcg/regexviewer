#include "npp.h"
#include <string>
#include "panel.h"
#include "settings.h"
#include "resource.h"
#include "util.h"

NppData   g_npp = {};
HINSTANCE g_module = nullptr;

static const wchar_t* const TITLE = L"Datamodder Regex Viewer";

// g_funcs slot map (one flat menu).
enum {
    F_SHOW,          // Show Regex Viewer (toolbar button; docking dlgID)
    F_PASTE,         // Paste regex from clipboard
    F_EXPLAIN_SEL,   // Explain selection as regex
    F_SEP1,
    F_NEXT, F_PREV,
    F_REPLACE, F_REPLACE_ALL,
    F_CLEAR,
    F_SEP2,
    F_SETTINGS, F_HELP, F_ABOUT,
    NFUNCS
};
static FuncItem g_funcs[NFUNCS] = {};

static HWND current_editor() {
    int which = 0;
    SendMessage(g_npp._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
    return which ? g_npp._scintillaSecondHandle : g_npp._scintillaMainHandle;
}

static std::wstring clipboard_text() {
    std::wstring t;
    if (!OpenClipboard(g_npp._nppHandle)) return t;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* p = (const wchar_t*)GlobalLock(h)) { t = p; GlobalUnlock(h); }
    }
    CloseClipboard();
    return t;
}

static void cmd_show()  { panel_show(); }
static void cmd_paste() { panel_set_paste(clipboard_text()); }

static void cmd_explain_selection() {
    HWND ed = current_editor();
    LRESULT a = SendMessage(ed, SCI_GETSELECTIONSTART, 0, 0), b = SendMessage(ed, SCI_GETSELECTIONEND, 0, 0);
    if (b <= a) {
        MessageBoxW(g_npp._nppHandle, L"Select a regex first - on its own, or a line of code that contains one.", TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::string sel((size_t)(b - a) + 1, '\0');
    SendMessage(ed, SCI_GETSELTEXT, 0, (LPARAM)sel.data());
    sel.resize((size_t)(b - a));
    panel_set_paste(widen(sel));
}

static void cmd_next()        { panel_next(); }
static void cmd_prev()        { panel_prev(); }
static void cmd_replace()     { panel_replace(); }
static void cmd_replace_all() { panel_replace_all(); }
static void cmd_clear()       { panel_clear_highlights(); }
static void cmd_settings()    { show_settings_dialog(g_npp._nppHandle); panel_on_dark_mode(); }
static void cmd_about()       { show_about_dialog(g_npp._nppHandle); }
static void cmd_help() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(g_module, path, MAX_PATH);
    std::wstring p = path;
    p = p.substr(0, p.find_last_of(L"\\/")) + L"\\help.txt";
    SendMessage(g_npp._nppHandle, NPPM_DOOPEN, 0, (LPARAM)p.c_str());
}

static void add_toolbar_button() {
    toolbarIconsWithDarkMode icons = {};
    icons.hToolbarBmp = (HBITMAP)LoadImageW(g_module, MAKEINTRESOURCEW(IDB_TB_LENS), IMAGE_BITMAP, 0, 0, LR_DEFAULTSIZE | LR_LOADMAP3DCOLORS);
    icons.hToolbarIcon = (HICON)LoadImageW(g_module, MAKEINTRESOURCEW(IDI_TB_LENS), IMAGE_ICON, 0, 0, LR_LOADTRANSPARENT | LR_DEFAULTSIZE);
    icons.hToolbarIconDarkMode = (HICON)LoadImageW(g_module, MAKEINTRESOURCEW(IDI_TB_LENS_DARK), IMAGE_ICON, 0, 0, LR_LOADTRANSPARENT | LR_DEFAULTSIZE);
    SendMessage(g_npp._nppHandle, NPPM_ADDTOOLBARICON_FORDARKMODE, (WPARAM)g_funcs[F_SHOW]._cmdID, (LPARAM)&icons);
}

extern "C" {

__declspec(dllexport) const wchar_t* getName() { return TITLE; }

__declspec(dllexport) void setInfo(NppData d) { g_npp = d; load_settings(); }

__declspec(dllexport) FuncItem* getFuncsArray(int* n) { *n = NFUNCS; return g_funcs; }

__declspec(dllexport) void beNotified(SCNotification* n) {
    HWND from = (HWND)n->nmhdr.hwndFrom;
    if (from == g_npp._scintillaMainHandle || from == g_npp._scintillaSecondHandle) {
        if (n->nmhdr.code == SCN_MODIFIED || n->nmhdr.code == SCN_UPDATEUI) panel_on_editor_notify(n);
        return;
    }
    switch (n->nmhdr.code) {
    case NPPN_TBMODIFICATION: add_toolbar_button(); break;
    case NPPN_READY:          panel_init(F_SHOW, g_funcs[F_SHOW]._cmdID); break;
    case NPPN_BUFFERACTIVATED: panel_on_buffer_activated(); break;
    case NPPN_DARKMODECHANGED: panel_on_dark_mode(); break;
    case NPPN_SHUTDOWN:       panel_shutdown(); break;
    }
}

__declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM) { return TRUE; }

__declspec(dllexport) BOOL isUnicode() { return TRUE; }

} // extern "C"

static void init_func(int i, const wchar_t* name, PFUNCPLUGINCMD fn) {
    wcscpy_s(g_funcs[i]._menuItemName, name);
    g_funcs[i]._pFunc = fn;
}

BOOL APIENTRY DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = h;
        DisableThreadLibraryCalls(h);
        init_func(F_SHOW,        L"Show Regex Viewer",           cmd_show);
        init_func(F_PASTE,       L"Paste regex from clipboard",  cmd_paste);
        init_func(F_EXPLAIN_SEL, L"Explain selection as regex",  cmd_explain_selection);
        init_func(F_SEP1,        L"-",                           nullptr);
        init_func(F_NEXT,        L"Next match",                  cmd_next);
        init_func(F_PREV,        L"Previous match",              cmd_prev);
        init_func(F_REPLACE,     L"Replace",                     cmd_replace);
        init_func(F_REPLACE_ALL, L"Replace all",                 cmd_replace_all);
        init_func(F_CLEAR,       L"Clear highlights",            cmd_clear);
        init_func(F_SEP2,        L"-",                           nullptr);
        init_func(F_SETTINGS,    L"Settings...",                 cmd_settings);
        init_func(F_HELP,        L"Help",                        cmd_help);
        init_func(F_ABOUT,       L"About",                       cmd_about);
    }
    return TRUE;
}
