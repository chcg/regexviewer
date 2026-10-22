#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include "settings.h"
#include "resource.h"
#include "util.h"

extern HINSTANCE g_module;

ViewerSettings g_settings;

static const wchar_t* const TITLE = L"Datamodder Regex Viewer";

// ─── paths ────────────────────────────────────────────────────────────────────

static std::wstring config_dir() {
    wchar_t appdata[MAX_PATH] = {};
    GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    std::wstring dir = std::wstring(appdata) + L"\\Notepad++\\plugins\\config";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}
static std::wstring get_ini_path() { return config_dir() + L"\\RegexViewer.ini"; }

static const char* like_key(MatchLike m) {
    return m == MatchLike::Source ? "source" : m == MatchLike::Fixed ? "fixed" : "npp";
}
static MatchLike like_from(const std::string& s) {
    return s == "source" ? MatchLike::Source : s == "fixed" ? MatchLike::Fixed : MatchLike::NotepadPP;
}

static void clamp() {
    if (g_settings.budget_s < 1)    g_settings.budget_s = 2;
    if (g_settings.budget_s > 60)   g_settings.budget_s = 60;
    if (g_settings.large_mb < 1)    g_settings.large_mb = 5;
    if (g_settings.max_list < 100)  g_settings.max_list = 10000;
    if (g_settings.fixed == rv::Flavor::NotepadPP) g_settings.fixed = rv::Flavor::Python;
}

// ─── INI persistence ──────────────────────────────────────────────────────────

void save_settings() {
    std::wstring p = get_ini_path();
    const wchar_t* f = p.c_str();
    auto ws = [&](LPCWSTR sec, LPCWSTR key, const std::string& v) { WritePrivateProfileStringW(sec, key, widen(v).c_str(), f); };
    auto wi = [&](LPCWSTR sec, LPCWSTR key, int v) { wchar_t b[16]; wsprintfW(b, L"%d", v); WritePrivateProfileStringW(sec, key, b, f); };

    ws(L"Matching", L"MatchLike", like_key(g_settings.match_like));
    ws(L"Matching", L"FixedFlavor", rv::flavor_key(g_settings.fixed));
    wi(L"Matching", L"BudgetSeconds", g_settings.budget_s);
    wi(L"Matching", L"LargeFileMB", g_settings.large_mb);
    wi(L"Highlighting", L"MatchBox", g_settings.match_box ? 1 : 0);
    wi(L"Highlighting", L"MaxList", g_settings.max_list);
    wi(L"General", L"RerunOnEdit", g_settings.rerun_on_edit ? 1 : 0);
}

void load_settings() {
    std::wstring p = get_ini_path();
    const wchar_t* f = p.c_str();
    auto rs = [&](LPCWSTR sec, LPCWSTR key, LPCWSTR def) { wchar_t b[256] = {}; GetPrivateProfileStringW(sec, key, def, b, 256, f); return narrow(b); };
    auto ri = [&](LPCWSTR sec, LPCWSTR key, int def) { return (int)GetPrivateProfileIntW(sec, key, def, f); };

    g_settings.match_like = like_from(rs(L"Matching", L"MatchLike", L"npp"));
    rv::flavor_from_key(rs(L"Matching", L"FixedFlavor", L"python"), g_settings.fixed);
    g_settings.budget_s   = ri(L"Matching", L"BudgetSeconds", 2);
    g_settings.large_mb   = ri(L"Matching", L"LargeFileMB", 5);
    g_settings.match_box  = ri(L"Highlighting", L"MatchBox", 1) != 0;
    g_settings.max_list   = ri(L"Highlighting", L"MaxList", 10000);
    g_settings.rerun_on_edit = ri(L"General", L"RerunOnEdit", 1) != 0;
    clamp();
}

// ─── JSON profiles ────────────────────────────────────────────────────────────

static std::string settings_to_json(const ViewerSettings& s) {
    std::string j = "{\n";
    j += std::string("  \"match_like\": \"") + like_key(s.match_like) + "\",\n";
    j += std::string("  \"fixed_flavor\": \"") + rv::flavor_key(s.fixed) + "\",\n";
    j += "  \"budget_s\": " + std::to_string(s.budget_s) + ",\n";
    j += "  \"large_mb\": " + std::to_string(s.large_mb) + ",\n";
    j += std::string("  \"match_box\": ") + (s.match_box ? "true" : "false") + ",\n";
    j += "  \"max_list\": " + std::to_string(s.max_list) + ",\n";
    j += std::string("  \"rerun_on_edit\": ") + (s.rerun_on_edit ? "true" : "false") + "\n";
    return j + "}\n";
}

static std::string jget_str(const std::string& j, const char* key, const std::string& def) {
    std::string pat = std::string("\"") + key + "\": \"";
    auto p = j.find(pat);
    if (p == std::string::npos) return def;
    p += pat.size();
    auto e = j.find('"', p);
    return e == std::string::npos ? def : j.substr(p, e - p);
}
static int jget_int(const std::string& j, const char* key, int def) {
    std::string pat = std::string("\"") + key + "\": ";
    auto p = j.find(pat);
    if (p == std::string::npos) return def;
    p += pat.size();
    return (p < j.size() && (std::isdigit((unsigned char)j[p]) || j[p] == '-')) ? std::atoi(j.c_str() + p) : def;
}
static bool jget_bool(const std::string& j, const char* key, bool def) {
    std::string pat = std::string("\"") + key + "\": ";
    auto p = j.find(pat);
    if (p == std::string::npos) return def;
    p += pat.size();
    if (j.compare(p, 4, "true") == 0) return true;
    if (j.compare(p, 5, "false") == 0) return false;
    return def;
}

static ViewerSettings settings_from_json(const std::string& j) {
    ViewerSettings s;
    s.match_like = like_from(jget_str(j, "match_like", "npp"));
    rv::flavor_from_key(jget_str(j, "fixed_flavor", "python"), s.fixed);
    s.budget_s = jget_int(j, "budget_s", s.budget_s);
    s.large_mb = jget_int(j, "large_mb", s.large_mb);
    s.match_box = jget_bool(j, "match_box", s.match_box);
    s.max_list = jget_int(j, "max_list", s.max_list);
    s.rerun_on_edit = jget_bool(j, "rerun_on_edit", s.rerun_on_edit);
    return s;
}

static std::wstring get_profiles_dir() {
    std::wstring dir = config_dir() + L"\\RegexViewer_profiles";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

static std::vector<std::wstring> list_profiles() {
    std::vector<std::wstring> names;
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW((get_profiles_dir() + L"\\*.json").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return names;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring n = fd.cFileName;
        if (n.size() > 5) names.push_back(n.substr(0, n.size() - 5));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(names.begin(), names.end());
    return names;
}

// ─── pages ────────────────────────────────────────────────────────────────────

struct TabPage { UINT id; LPCWSTR label; DLGPROC proc; };

static void settings_to_ui();
static void ui_to_settings();
static void refresh_profile_list(HWND hPage);

static INT_PTR CALLBACK page_proc(HWND, UINT msg, WPARAM, LPARAM) { return msg == WM_INITDIALOG ? TRUE : FALSE; }

static INT_PTR CALLBACK matching_page_proc(HWND hPage, UINT msg, WPARAM, LPARAM) {
    if (msg == WM_INITDIALOG) {
        HWND c = GetDlgItem(hPage, IDC_FIXED_FLAVOR);
        for (int i = 1; i < (int)rv::Flavor::Count; ++i)
            SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)rv::flavor_name((rv::Flavor)i));
        return TRUE;
    }
    return FALSE;
}

static bool browse(HWND owner, bool save, std::wstring& out) {
    wchar_t fname[MAX_PATH] = L"settings";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"JSON files\0*.json\0All files\0*.*\0";
    ofn.lpstrFile = fname;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"json";
    ofn.Flags = save ? OFN_OVERWRITEPROMPT : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);
    if (!save) fname[0] = 0;
    BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    if (ok) out = fname;
    return ok != FALSE;
}

static INT_PTR CALLBACK behavior_page_proc(HWND hPage, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG:
        refresh_profile_list(hPage);
        return TRUE;
    case WM_COMMAND: {
        UINT notif = HIWORD(wParam), ctrl = LOWORD(wParam);
        if (ctrl == IDC_PROFILE_LIST && notif == LBN_DBLCLK) ctrl = IDC_PROFILE_LOAD;
        else if (notif != BN_CLICKED) break;
        HWND hList = GetDlgItem(hPage, IDC_PROFILE_LIST);
        auto selected = [&](std::wstring& name) {
            int sel = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
            if (sel == LB_ERR) return false;
            wchar_t b[512] = {};
            SendMessageW(hList, LB_GETTEXT, sel, (LPARAM)b);
            name = b;
            return true;
        };
        std::wstring name, f;
        switch (ctrl) {
        case IDC_PROFILE_LOAD:
            if (selected(name)) {
                std::string j = read_file(get_profiles_dir() + L"\\" + name + L".json");
                if (!j.empty()) {
                    g_settings = settings_from_json(j);
                    settings_to_ui();
                    MessageBoxW(hPage, (L"Profile '" + name + L"' loaded.").c_str(), TITLE, MB_OK | MB_ICONINFORMATION);
                }
            }
            break;
        case IDC_PROFILE_SAVE: {
            wchar_t b[512] = {};
            GetDlgItemTextW(hPage, IDC_PROFILE_NAME, b, 512);
            if (!b[0]) break;
            ui_to_settings();
            write_file(get_profiles_dir() + L"\\" + b + L".json", settings_to_json(g_settings));
            refresh_profile_list(hPage);
            break;
        }
        case IDC_PROFILE_DELETE:
            if (selected(name) && MessageBoxW(GetParent(hPage), (L"Delete profile \"" + name + L"\"?").c_str(), TITLE, MB_YESNO | MB_ICONQUESTION) == IDYES) {
                DeleteFileW((get_profiles_dir() + L"\\" + name + L".json").c_str());
                refresh_profile_list(hPage);
            }
            break;
        case IDC_PROFILE_EXPORT:
            if (browse(GetParent(hPage), true, f)) { ui_to_settings(); write_file(f, settings_to_json(g_settings)); }
            break;
        case IDC_PROFILE_IMPORT:
            if (browse(GetParent(hPage), false, f)) {
                std::string j = read_file(f);
                if (!j.empty()) { g_settings = settings_from_json(j); settings_to_ui(); }
            }
            break;
        }
        break;
    }
    }
    return FALSE;
}

static const TabPage PAGES[] = {
    { IDD_TAB_MATCHING,  L"Matching",     matching_page_proc },
    { IDD_TAB_HIGHLIGHT, L"Highlighting", page_proc },
    { IDD_TAB_BEHAVIOR,  L"Behavior",     behavior_page_proc },
};
static const int PAGE_COUNT = 3;
static HWND g_pages[PAGE_COUNT] = {};
#define P_MATCH g_pages[0]
#define P_HIGH  g_pages[1]
#define P_BEHAV g_pages[2]

static void show_page(int idx) {
    for (int i = 0; i < PAGE_COUNT; ++i) ShowWindow(g_pages[i], i == idx ? SW_SHOW : SW_HIDE);
}

static int  edit_int(HWND d, int id) { wchar_t b[16] = {}; GetDlgItemTextW(d, id, b, 16); return b[0] ? _wtoi(b) : 0; }
static void set_int(HWND d, int id, int v) { wchar_t b[16]; wsprintfW(b, L"%d", v); SetDlgItemTextW(d, id, b); }
static bool checked(HWND d, int id) { return IsDlgButtonChecked(d, id) == BST_CHECKED; }

static void refresh_profile_list(HWND hPage) {
    HWND hList = GetDlgItem(hPage, IDC_PROFILE_LIST);
    SendMessage(hList, LB_RESETCONTENT, 0, 0);
    for (const auto& n : list_profiles()) SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)n.c_str());
}

static void settings_to_ui() {
    int r = g_settings.match_like == MatchLike::Source ? IDC_LIKE_SOURCE : g_settings.match_like == MatchLike::Fixed ? IDC_LIKE_FIXED : IDC_LIKE_NPP;
    CheckRadioButton(P_MATCH, IDC_LIKE_NPP, IDC_LIKE_FIXED, r);
    SendDlgItemMessageW(P_MATCH, IDC_FIXED_FLAVOR, CB_SETCURSEL, (int)g_settings.fixed - 1, 0);
    set_int(P_MATCH, IDC_BUDGET, g_settings.budget_s);
    set_int(P_MATCH, IDC_LARGE_MB, g_settings.large_mb);
    CheckDlgButton(P_HIGH, IDC_MATCH_BOX, g_settings.match_box ? BST_CHECKED : BST_UNCHECKED);
    set_int(P_HIGH, IDC_MAX_LIST, g_settings.max_list);
    CheckDlgButton(P_BEHAV, IDC_RERUN_ON_EDIT, g_settings.rerun_on_edit ? BST_CHECKED : BST_UNCHECKED);
}

static void ui_to_settings() {
    g_settings.match_like = checked(P_MATCH, IDC_LIKE_SOURCE) ? MatchLike::Source : checked(P_MATCH, IDC_LIKE_FIXED) ? MatchLike::Fixed : MatchLike::NotepadPP;
    int sel = (int)SendDlgItemMessageW(P_MATCH, IDC_FIXED_FLAVOR, CB_GETCURSEL, 0, 0);
    if (sel >= 0) g_settings.fixed = (rv::Flavor)(sel + 1);
    g_settings.budget_s = edit_int(P_MATCH, IDC_BUDGET);
    g_settings.large_mb = edit_int(P_MATCH, IDC_LARGE_MB);
    g_settings.match_box = checked(P_HIGH, IDC_MATCH_BOX);
    g_settings.max_list = edit_int(P_HIGH, IDC_MAX_LIST);
    g_settings.rerun_on_edit = checked(P_BEHAV, IDC_RERUN_ON_EDIT);
    clamp();
}

static void destroy_pages() {
    for (int i = 0; i < PAGE_COUNT; ++i) if (g_pages[i]) { DestroyWindow(g_pages[i]); g_pages[i] = nullptr; }
}

static INT_PTR CALLBACK settings_proc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG: {
        HWND hNav = GetDlgItem(hDlg, IDC_NAV);
        for (int i = 0; i < PAGE_COUNT; ++i) SendMessageW(hNav, LB_ADDSTRING, 0, (LPARAM)PAGES[i].label);
        SendMessageW(hNav, LB_SETCURSEL, 0, 0);
        RECT rc;
        GetWindowRect(hNav, &rc);
        MapWindowPoints(HWND_DESKTOP, hDlg, (POINT*)&rc, 2);
        for (int i = 0; i < PAGE_COUNT; ++i) {
            g_pages[i] = CreateDialog(g_module, MAKEINTRESOURCE(PAGES[i].id), hDlg, PAGES[i].proc);
            SetWindowPos(g_pages[i], HWND_TOP, rc.right + 5, rc.top, 0, 0, SWP_NOSIZE | SWP_HIDEWINDOW);
        }
        settings_to_ui();
        show_page(0);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_NAV && HIWORD(wParam) == LBN_SELCHANGE) {
            int sel = (int)SendMessageW(GetDlgItem(hDlg, IDC_NAV), LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) show_page(sel);
            return TRUE;
        }
        if (LOWORD(wParam) == IDOK) { ui_to_settings(); save_settings(); destroy_pages(); EndDialog(hDlg, IDOK); return TRUE; }
        if (LOWORD(wParam) == IDCANCEL) { destroy_pages(); EndDialog(hDlg, IDCANCEL); return TRUE; }
        break;
    case WM_CLOSE:
        destroy_pages();
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

void show_settings_dialog(HWND parent) {
    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_LINK_CLASS };
    InitCommonControlsEx(&icc);
    ViewerSettings before = g_settings;
    if (DialogBox(g_module, MAKEINTRESOURCE(IDD_SETTINGS), parent, settings_proc) != IDOK) g_settings = before;
}

// ─── about ────────────────────────────────────────────────────────────────────

static INT_PTR CALLBACK about_proc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemTextW(hDlg, IDC_ABOUT_VERSION, L"Datamodder Regex Viewer  " REGEXVIEWER_VERSION_W);
        SetDlgItemTextW(hDlg, IDC_ABOUT_BODY,
            L"Paste a regex the way you got it - inside Python, JavaScript, C#, Java, PHP, Go, "
            L"PowerShell or a sed/grep command - and see what it does in the open document.\n\n"
            L"Features:\n"
            L"- Every match highlighted, each group in its own colour\n"
            L"- The regex explained in plain language\n"
            L"- Match like Notepad++, like the source language, or a fixed flavour\n"
            L"- Converted for Notepad++'s Find, with Replace and Replace all\n\n"
            L"This plugin is distributed under the MIT license.\n\n"
            L"For usage tips, see the plugin page. For updates or to report a bug, visit the project repository:");
        return TRUE;
    case WM_NOTIFY: {
        NMHDR* h = (NMHDR*)lParam;
        if ((h->idFrom == IDC_ABOUT_LINK || h->idFrom == IDC_ABOUT_LINK_WEBSITE) && (h->code == NM_CLICK || h->code == NM_RETURN)) {
            ShellExecuteW(hDlg, L"open", ((NMLINK*)lParam)->item.szUrl, nullptr, nullptr, SW_SHOWNORMAL);
            return TRUE;
        }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) { EndDialog(hDlg, LOWORD(wParam)); return TRUE; }
        break;
    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

void show_about_dialog(HWND parent) {
    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_LINK_CLASS };
    InitCommonControlsEx(&icc);
    DialogBox(g_module, MAKEINTRESOURCE(IDD_ABOUT), parent, about_proc);
}
