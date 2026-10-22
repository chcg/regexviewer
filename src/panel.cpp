#define NOMINMAX
#include "panel.h"
#include <commctrl.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>
#include <cwchar>
#include "extract.h"
#include "explain.h"
#include "flavor.h"
#include "matcher.h"
#include "encoding.h"
#include "settings.h"
#include "resource.h"
#include "util.h"

using namespace rv;

namespace {

// ─── control ids ──────────────────────────────────────────────────────────────

enum {
    ID_PASTE_LABEL = 1001, ID_BTN_PASTE, ID_BTN_CLEAR, ID_FOUND, ID_CANDIDATES, ID_PATTERN_LABEL,
    ID_REPLACE_LABEL, ID_REPLACE, ID_REPL_NPP, ID_BTN_REPLACE, ID_BTN_REPLACE_ALL, ID_LIST,
    ID_BAR, ID_BTN_CLOSE_VIEW, ID_STATUS, ID_LIKE, ID_FLAG_I, ID_FLAG_M, ID_FLAG_S, ID_FLAG_X,
    ID_SCOPE, ID_BTN_COPY, ID_BTN_PREV, ID_BTN_NEXT,
};
const UINT WM_RV_RESULT = WM_APP + 21;
const UINT_PTR TIMER_EXTRACT = 1, TIMER_RUN = 2;
const int NGROUP_COLORS = 8;

// ─── state ────────────────────────────────────────────────────────────────────

struct Result {
    int run = 0;
    bool partial = false;                    // first stage of a large file
    std::shared_ptr<TextMap> tm;
    MatchRun mr;
    size_t source_count = (size_t)-1;        // count when matched like the source (anchors_differ)
    DWORD ms = 0;
};

struct Job {
    int run = 0;
    std::string bytes;
    unsigned codepage = SC_CP_UTF8;
    Prepared prep, prep_source;
    bool count_source = false;
    size_t scope_b = 0, scope_e = (size_t)-1;      // bytes
    size_t vis_b = 0, vis_e = (size_t)-1;          // bytes, for the first stage
    bool two_stage = false;
    size_t max_items = 10000;
    unsigned budget_ms = 2000;
    bool with_repl = false;
};

DockedWidgetData   g_dock;
HWND               g_dlg = nullptr;
HWND               g_paste = nullptr, g_pattern = nullptr, g_explain = nullptr, g_list = nullptr;
HWND               g_ctl[64] = {};             // other controls by (id - 1000)
int                g_show_index = 0, g_show_cmd = 0;
int                g_ind = 0;                    // first allocated indicator (match box), groups follow
bool               g_visible = false;
std::atomic<int>   g_run{ 0 };

std::vector<Candidate> g_cands;
int                g_cand = -1;
Prepared           g_prep;
bool               g_prep_ok = false;            // compiled
std::wstring       g_compile_error;
bool               g_flags_user = false;          // flags overridden by the user for this candidate
bool               g_repl_user = false;           // replacement typed by the user
int                g_like = 0;                    // combo index: 0 Notepad++, 1 source, 2.. flavours
std::shared_ptr<Result> g_res;
bool               g_doc_dirty = false;           // document changed since the last result
HWND               g_res_editor = nullptr;
size_t             g_pending_select = (size_t)-1; // after Replace: select the next match from here (bytes)
bool               g_split = false;
std::wstring       g_status_extra;

HWND ctl(int id) { return g_ctl[id - 1000]; }

int scale(int px) {
    static int dpi = [] { HDC dc = GetDC(nullptr); int d = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(nullptr, dc); return d; }();
    return MulDiv(px, dpi, 96);
}

HWND editor() {
    int which = 0;
    SendMessage(g_npp._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&which);
    return which ? g_npp._scintillaSecondHandle : g_npp._scintillaMainHandle;
}

LRESULT sci(HWND h, UINT m, WPARAM w = 0, LPARAM l = 0) { return SendMessage(h, m, w, l); }

std::string sci_text(HWND h) {
    LRESULT n = sci(h, SCI_GETLENGTH);
    std::string s((size_t)n + 1, '\0');
    sci(h, SCI_GETTEXT, (WPARAM)n + 1, (LPARAM)s.data());
    s.resize((size_t)n);
    return s;
}

void sci_set_text(HWND h, const std::string& s, bool readonly) {
    sci(h, SCI_SETREADONLY, 0);
    sci(h, SCI_SETTEXT, 0, (LPARAM)s.c_str());
    sci(h, SCI_EMPTYUNDOBUFFER);
    if (readonly) sci(h, SCI_SETREADONLY, 1);
}

std::wstring thousands(size_t n) {
    std::wstring d = std::to_wstring(n), o;
    for (size_t i = 0; i < d.size(); ++i) { if (i && (d.size() - i) % 3 == 0) o += L','; o += d[i]; }
    return o;
}

bool dark() { return SendMessage(g_npp._nppHandle, NPPM_ISDARKMODEENABLED, 0, 0) != 0; }

// ─── colours / indicators ─────────────────────────────────────────────────────

const COLORREF GROUP_COLORS[NGROUP_COLORS] = {
    RGB(255, 193, 7), RGB(3, 169, 244), RGB(76, 175, 80), RGB(233, 30, 99),
    RGB(156, 39, 176), RGB(255, 87, 34), RGB(0, 150, 136), RGB(205, 220, 57),
};

int group_indicator(int g) { return g_ind + 1 + ((g - 1) % NGROUP_COLORS); }

void style_indicators(HWND h) {
    if (!h || !g_ind) return;
    int alpha = dark() ? 110 : 80;
    sci(h, SCI_INDICSETSTYLE, g_ind, INDIC_STRAIGHTBOX);
    sci(h, SCI_INDICSETFORE, g_ind, dark() ? RGB(176, 190, 197) : RGB(96, 125, 139));
    sci(h, SCI_INDICSETALPHA, g_ind, 30);
    sci(h, SCI_INDICSETOUTLINEALPHA, g_ind, 200);
    sci(h, SCI_INDICSETUNDER, g_ind, 1);
    for (int k = 0; k < NGROUP_COLORS; ++k) {
        int ind = g_ind + 1 + k;
        sci(h, SCI_INDICSETSTYLE, ind, INDIC_FULLBOX);
        sci(h, SCI_INDICSETFORE, ind, GROUP_COLORS[k]);
        sci(h, SCI_INDICSETALPHA, ind, alpha);
        sci(h, SCI_INDICSETOUTLINEALPHA, ind, 0);
        sci(h, SCI_INDICSETUNDER, ind, 1);
    }
}

void clear_indicators(HWND h) {
    if (!h || !g_ind) return;
    LRESULT len = sci(h, SCI_GETLENGTH);
    for (int k = 0; k <= NGROUP_COLORS; ++k) {
        sci(h, SCI_SETINDICATORCURRENT, g_ind + k);
        sci(h, SCI_INDICATORCLEARRANGE, 0, len);
    }
}

void fill(HWND h, int ind, size_t b, size_t e) {
    if (e <= b) return;
    sci(h, SCI_SETINDICATORCURRENT, ind);
    sci(h, SCI_INDICATORFILLRANGE, b, e - b);
}

// ─── highlighting in the editor (visible part only) ──────────────────────────

void paint_visible() {
    HWND ed = editor();
    clear_indicators(g_npp._scintillaMainHandle);
    clear_indicators(g_npp._scintillaSecondHandle);
    if (!g_visible || g_split || !g_res || g_doc_dirty || ed != g_res_editor) return;
    const auto& items = g_res->mr.items;
    if (items.empty()) return;
    const TextMap& tm = *g_res->tm;

    LRESULT first = sci(ed, SCI_DOCLINEFROMVISIBLE, sci(ed, SCI_GETFIRSTVISIBLELINE));
    LRESULT lines = sci(ed, SCI_LINESONSCREEN);
    LRESULT count = sci(ed, SCI_GETLINECOUNT);
    LRESULT la = std::max<LRESULT>(0, first - 100), lb = std::min<LRESULT>(count, first + lines + 100);
    size_t ba = (size_t)sci(ed, SCI_POSITIONFROMLINE, la);
    size_t bb = lb >= count ? (size_t)sci(ed, SCI_GETLENGTH) : (size_t)sci(ed, SCI_POSITIONFROMLINE, lb);
    size_t ua = tm.to_unit(ba), ub = tm.to_unit(bb);

    auto it = std::partition_point(items.begin(), items.end(), [&](const MatchItem& m) { return (size_t)m.groups[0].end < ua; });
    for (; it != items.end() && (size_t)it->groups[0].start <= ub; ++it) {
        const MatchItem& m = *it;
        size_t b0 = tm.to_byte(m.groups[0].start), e0 = tm.to_byte(m.groups[0].end);
        if (g_settings.match_box) fill(ed, g_ind, b0, e0);
        for (size_t g = 1; g < m.groups.size(); ++g) {
            if (m.groups[g].start < 0) continue;
            fill(ed, group_indicator((int)g), tm.to_byte(m.groups[g].start), tm.to_byte(m.groups[g].end));
        }
    }
}

// ─── extraction and preparation ───────────────────────────────────────────────

Flavor effective_flavor() {
    if (g_like == 0) return Flavor::NotepadPP;
    if (g_like == 1) {
        if (g_cand >= 0 && g_cands[g_cand].recognised) return g_cands[g_cand].flavor;
        return Flavor::NotepadPP;
    }
    return (Flavor)(g_like - 1);
}

Flags current_flags() {
    Flags f;
    f.i = IsDlgButtonChecked(g_dlg, ID_FLAG_I) == BST_CHECKED;
    f.m = IsDlgButtonChecked(g_dlg, ID_FLAG_M) == BST_CHECKED;
    f.s = IsDlgButtonChecked(g_dlg, ID_FLAG_S) == BST_CHECKED;
    f.x = IsDlgButtonChecked(g_dlg, ID_FLAG_X) == BST_CHECKED;
    return f;
}

void set_flags_ui(const Flags& f) {
    CheckDlgButton(g_dlg, ID_FLAG_I, f.i ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g_dlg, ID_FLAG_M, f.m ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g_dlg, ID_FLAG_S, f.s ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g_dlg, ID_FLAG_X, f.x ? BST_CHECKED : BST_UNCHECKED);
}

std::wstring window_text(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s((size_t)n + 1, L'\0');
    GetWindowTextW(h, &s[0], n + 1);
    s.resize((size_t)n);
    return s;
}

// Explanation pane: warnings first, then the explanation, groups coloured.
void render_explanation() {
    std::string text;
    struct Mark { size_t b, e; int style_or_group; bool group; };
    std::vector<Mark> marks;
    auto add_line = [&](const std::wstring& w, int style) {
        size_t b = text.size();
        text += narrow(w) + "\n";
        if (style) marks.push_back({ b, text.size() - 1, style, false });
    };

    if (g_cand < 0) {
        add_line(L"Paste a regex above - on its own, or inside the code you got it in.", 0);
    } else if (!g_compile_error.empty()) {
        add_line(L"\x2716 " + g_compile_error, 2);
        for (auto& w : g_prep.warnings) add_line(L"\x26A0 " + w, 1);
    } else {
        if (g_prep.rejected) add_line(L"\x2716 " + g_prep.reject_reason, 2);
        for (auto& n : g_cands[g_cand].notes) add_line(L"\x26A0 " + n, 1);
        for (auto& w : g_prep.warnings) add_line(L"\x26A0 " + w, 1);
        if (g_res && g_prep.anchors_differ && g_res->source_count != (size_t)-1 && g_res->source_count != g_res->mr.total)
            add_line(L"\x26A0 In " + std::wstring(flavor_name(g_prep.source)) + L" (without the multiline flag) ^ and $ only match at the start and end of the text: " +
                     thousands(g_res->source_count) + (g_res->source_count == 1 ? L" match" : L" matches") + L" instead of " + thousands(g_res->mr.total) + L".", 1);
        if (!marks.empty()) text += "\n";

        ExplainContext ctx;
        ctx.multiline_anchors = g_prep.multiline_anchors();
        ctx.dot_all = g_prep.dot_all();
        for (const ExplainLine& l : explain(g_prep.parse_pattern, g_prep.parse, ctx)) {
            std::string indent(2 * l.depth, ' ');
            std::string code = narrow(l.code);
            size_t b = text.size() + indent.size();
            std::string row = indent + code;
            size_t cols = (size_t)MultiByteToWideChar(CP_UTF8, 0, row.c_str(), (int)row.size(), nullptr, 0);
            row += cols < 28 ? std::string(28 - cols, ' ') : std::string("  ");
            text += row + narrow(l.text) + "\n";
            if (l.group) marks.push_back({ b, b + code.size(), l.group, true });
        }
    }
    sci(g_explain, SCI_SETREADONLY, 0);
    sci(g_explain, SCI_SETTEXT, 0, (LPARAM)text.c_str());
    clear_indicators(g_explain);
    sci(g_explain, SCI_STARTSTYLING, 0);
    sci(g_explain, SCI_SETSTYLING, text.size(), 0);
    for (const Mark& m : marks) {
        if (m.group) fill(g_explain, group_indicator(m.style_or_group), m.b, m.e);
        else { sci(g_explain, SCI_STARTSTYLING, m.b); sci(g_explain, SCI_SETSTYLING, m.e - m.b, m.style_or_group); }
    }
    sci(g_explain, SCI_SETREADONLY, 1);
}

void render_pattern() {
    std::wstring pat = g_cand >= 0 ? g_prep.npp_pattern : L"";
    std::string u = narrow(pat);
    sci_set_text(g_pattern, u, true);
    clear_indicators(g_pattern);
    if (g_cand < 0) return;
    size_t prefix = g_prep.npp_pattern.size() - g_prep.parse_pattern.size();
    for (const GroupSpan& gs : g_prep.parse.groups) {
        if (gs.close == std::wstring::npos) continue;
        size_t b = narrow(pat.substr(0, prefix + gs.open)).size();
        size_t e = narrow(pat.substr(0, prefix + gs.close + 1)).size();
        fill(g_pattern, group_indicator(gs.group), b, e);
    }
}

void update_replacement_label() {
    std::wstring r = window_text(ctl(ID_REPLACE));
    bool has = !r.empty() || (g_cand >= 0 && g_cands[g_cand].has_replacement && !g_repl_user);
    std::wstring label = has && g_cand >= 0 ? L"Notepad++: " + g_prep.npp_replacement : L"";
    SetWindowTextW(ctl(ID_REPL_NPP), label.c_str());
    EnableWindow(ctl(ID_BTN_REPLACE), has && g_prep_ok && !g_prep.rejected);
    EnableWindow(ctl(ID_BTN_REPLACE_ALL), has && g_prep_ok && !g_prep.rejected);
}

void schedule_run(UINT delay = 250) { if (g_dlg) SetTimer(g_dlg, TIMER_RUN, delay, nullptr); }

void prepare_current() {
    g_prep_ok = false;
    g_compile_error.clear();
    if (g_cand < 0) { g_res.reset(); render_pattern(); render_explanation(); update_replacement_label(); return; }
    const Candidate& c = g_cands[g_cand];
    std::wstring repl = window_text(ctl(ID_REPLACE));
    bool has_repl = !repl.empty();
    g_prep = prepare(c.pattern, has_repl ? &repl : nullptr, c.recognised ? c.flavor : Flavor::NotepadPP, effective_flavor(), current_flags());
    int pos = -1;
    g_compile_error = check_pattern(g_prep, &pos);
    if (!g_compile_error.empty() && pos >= 0) g_compile_error += L" (at position " + std::to_wstring(pos + 1) + L")";
    g_prep_ok = g_compile_error.empty();

    std::wstring found = L"Found: " + c.source;
    if (g_cands.size() > 1) found += L"   (" + std::to_wstring(g_cands.size()) + L" regexes in the paste; pick one on the right)";
    SetWindowTextW(ctl(ID_FOUND), found.c_str());
    render_pattern();
    render_explanation();
    update_replacement_label();
    schedule_run(0);
}

void select_candidate(int k) {
    g_cand = k;
    g_flags_user = false;
    g_repl_user = false;
    if (k >= 0) {
        set_flags_ui(g_cands[k].flags);
        SetWindowTextW(ctl(ID_REPLACE), g_cands[k].has_replacement ? g_cands[k].replacement.c_str() : L"");
        g_repl_user = false;
    }
    prepare_current();
}

void do_extract() {
    std::wstring paste = widen(sci_text(g_paste));
    g_cands = extract(paste);
    HWND cb = ctl(ID_CANDIDATES);
    SendMessageW(cb, CB_RESETCONTENT, 0, 0);
    for (size_t k = 0; k < g_cands.size(); ++k) {
        std::wstring p = g_cands[k].pattern;
        if (p.size() > 60) p = p.substr(0, 57) + L"...";
        std::wstring item = std::to_wstring(k + 1) + L". " + p;
        SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)item.c_str());
    }
    ShowWindow(cb, g_cands.size() > 1 ? SW_SHOW : SW_HIDE);
    if (!g_cands.empty()) SendMessageW(cb, CB_SETCURSEL, 0, 0);
    if (g_cands.empty()) SetWindowTextW(ctl(ID_FOUND), L"");
    select_candidate(g_cands.empty() ? -1 : 0);
}

// ─── running ──────────────────────────────────────────────────────────────────

DWORD WINAPI worker(LPVOID param) {
    std::unique_ptr<Job> job(static_cast<Job*>(param));
    auto tm = std::make_shared<TextMap>();
    tm->build(job->bytes.data(), job->bytes.size(), job->codepage);
    job->bytes.clear();
    job->bytes.shrink_to_fit();
    const std::wstring& text = tm->text();

    RunOptions ro;
    ro.budget_ms = job->budget_ms;
    ro.max_items = job->max_items;
    ro.with_replacement = job->with_repl;
    ro.cancel_token = &g_run;
    ro.cancel_value = job->run;
    size_t sb = job->scope_b == 0 ? 0 : tm->to_unit(job->scope_b);
    size_t se = job->scope_e == (size_t)-1 ? text.size() : tm->to_unit(job->scope_e);

    auto post = [&](Result* r) {
        if (g_run.load() != job->run || !g_dlg) { delete r; return; }
        PostMessageW(g_dlg, WM_RV_RESULT, 0, (LPARAM)r);
    };

    if (job->two_stage) {
        Result* r = new Result;
        r->run = job->run; r->partial = true; r->tm = tm;
        DWORD t0 = GetTickCount();
        RunOptions r1 = ro;
        r1.begin = std::max(sb, tm->to_unit(job->vis_b));
        r1.end = std::min(se, tm->to_unit(job->vis_e));
        r->mr = run_matches(job->prep, text, r1);
        r->ms = GetTickCount() - t0;
        post(r);
        if (g_run.load() != job->run) return 0;
    }

    Result* r = new Result;
    r->run = job->run;
    r->tm = tm;
    DWORD t0 = GetTickCount();
    ro.begin = sb;
    ro.end = se;
    r->mr = run_matches(job->prep, text, ro);
    if (job->count_source && r->mr.compiled && !r->mr.too_slow) {
        RunOptions rs = ro;
        rs.max_items = 0;
        rs.with_replacement = false;
        MatchRun m2 = run_matches(job->prep_source, text, rs);
        if (!m2.too_slow && !m2.timed_out) r->source_count = m2.total;
    }
    r->ms = GetTickCount() - t0;
    post(r);
    return 0;
}

void show_split_bar(bool split) {
    g_split = split;
    ShowWindow(ctl(ID_BAR), split ? SW_SHOW : SW_HIDE);
    ShowWindow(ctl(ID_BTN_CLOSE_VIEW), split ? SW_SHOW : SW_HIDE);
    ShowWindow(g_explain, split ? SW_HIDE : SW_SHOW);
    ShowWindow(g_list, split ? SW_HIDE : SW_SHOW);
}

bool split_view() { return IsWindowVisible(g_npp._scintillaSecondHandle) && IsWindowVisible(g_npp._scintillaMainHandle); }

std::wstring doc_name() {
    wchar_t name[MAX_PATH] = {};
    SendMessage(g_npp._nppHandle, NPPM_GETFILENAME, MAX_PATH, (LPARAM)name);
    return name;
}

void set_status(const std::wstring& s) { SetWindowTextW(ctl(ID_STATUS), s.c_str()); }

std::wstring like_text() { return L"matching like " + std::wstring(flavor_name(effective_flavor())); }

void start_run() {
    KillTimer(g_dlg, TIMER_RUN);
    if (!g_visible) return;
    bool split = split_view();
    show_split_bar(split);
    if (split) {
        g_run++;
        g_res.reset();
        clear_indicators(g_npp._scintillaMainHandle);
        clear_indicators(g_npp._scintillaSecondHandle);
        ListView_SetItemCountEx(g_list, 0, 0);
        set_status(L"Regex Viewer is paused: split view.");
        return;
    }
    if (g_cand < 0 || !g_prep_ok || g_prep.rejected) {
        g_run++;
        g_res.reset();
        paint_visible();
        ListView_SetItemCountEx(g_list, 0, 0);
        set_status(L"on: " + doc_name() + L"  \x00B7  " + like_text());
        return;
    }
    HWND ed = editor();
    auto job = std::make_unique<Job>();
    job->run = ++g_run;
    const char* p = (const char*)sci(ed, SCI_GETCHARACTERPOINTER);
    size_t len = (size_t)sci(ed, SCI_GETLENGTH);
    job->bytes.assign(p ? p : "", p ? len : 0);
    LRESULT cp = sci(ed, SCI_GETCODEPAGE);
    job->codepage = cp == SC_CP_UTF8 ? SC_CP_UTF8 : GetACP();
    job->prep = g_prep;
    job->max_items = (size_t)g_settings.max_list;
    job->budget_ms = (unsigned)g_settings.budget_s * 1000;
    job->with_repl = g_prep.has_replacement;
    if (g_prep.anchors_differ) {
        job->count_source = true;
        job->prep_source = prepare(g_cands[g_cand].pattern, nullptr, g_prep.source, g_prep.source, g_prep.flags);
    }
    if (SendDlgItemMessageW(g_dlg, ID_SCOPE, CB_GETCURSEL, 0, 0) == 1) {
        size_t a = (size_t)sci(ed, SCI_GETSELECTIONSTART), b = (size_t)sci(ed, SCI_GETSELECTIONEND);
        if (b > a) { job->scope_b = a; job->scope_e = b; }
    }
    if (len > (size_t)g_settings.large_mb * 1024 * 1024) {
        job->two_stage = true;
        LRESULT first = sci(ed, SCI_DOCLINEFROMVISIBLE, sci(ed, SCI_GETFIRSTVISIBLELINE));
        LRESULT count = sci(ed, SCI_GETLINECOUNT);
        LRESULT la = std::max<LRESULT>(0, first - 2000), lb = std::min<LRESULT>(count, first + 2000);
        job->vis_b = (size_t)sci(ed, SCI_POSITIONFROMLINE, la);
        job->vis_e = lb >= count ? len : (size_t)sci(ed, SCI_POSITIONFROMLINE, lb);
    }
    g_res_editor = ed;
    set_status(L"on: " + doc_name() + L"  \x00B7  " + like_text() + L"  \x00B7  matching...");
    HANDLE t = CreateThread(nullptr, 0, worker, job.get(), 0, nullptr);
    if (t) { job.release(); CloseHandle(t); }
}

// ─── match list ───────────────────────────────────────────────────────────────

std::vector<std::wstring> g_col_names;

void setup_columns() {
    SendMessage(g_list, WM_SETREDRAW, FALSE, 0);
    while (ListView_DeleteColumn(g_list, 0)) {}
    g_col_names.clear();
    g_col_names.push_back(L"#");
    g_col_names.push_back(L"Line");
    g_col_names.push_back(L"Match");
    int groups = g_cand >= 0 ? g_prep.parse.group_count : 0;
    for (int g = 1; g <= groups; ++g) {
        std::wstring n = L"Group " + std::to_wstring(g);
        for (auto& gs : g_prep.parse.groups) if (gs.group == g && !gs.name.empty()) n = gs.name;
        g_col_names.push_back(n);
    }
    if (g_prep.has_replacement) g_col_names.push_back(L"Result");
    for (size_t k = 0; k < g_col_names.size(); ++k) {
        LVCOLUMNW c = {};
        c.mask = LVCF_TEXT | LVCF_WIDTH;
        c.pszText = const_cast<wchar_t*>(g_col_names[k].c_str());
        c.cx = k == 0 ? scale(48) : k == 1 ? scale(52) : scale(140);
        ListView_InsertColumn(g_list, (int)k, &c);
    }
    SendMessage(g_list, WM_SETREDRAW, TRUE, 0);
}

std::wstring cell_text(const std::wstring& t) {
    std::wstring o;
    for (wchar_t c : t) {
        if (o.size() > 200) { o += L"..."; break; }
        o += c == L'\n' ? L'\x23CE' : c == L'\r' ? L' ' : c == L'\t' ? L' ' : c;
    }
    return o;
}

// ─── results ──────────────────────────────────────────────────────────────────

void select_match(const MatchItem& m, bool focus_editor) {
    if (!g_res) return;
    HWND ed = editor();
    size_t b = g_res->tm->to_byte(m.groups[0].start), e = g_res->tm->to_byte(m.groups[0].end);
    sci(ed, SCI_ENSUREVISIBLE, sci(ed, SCI_LINEFROMPOSITION, b));
    sci(ed, SCI_SETSEL, b, e);
    sci(ed, SCI_SCROLLCARET);
    if (focus_editor) { SetFocus(ed); sci(ed, SCI_GRABFOCUS); }
}

void on_result(Result* raw) {
    std::shared_ptr<Result> r(raw);
    if (r->run != g_run.load()) return;
    g_res = r;
    g_doc_dirty = false;
    const MatchRun& mr = r->mr;

    std::wstring st = L"on: " + doc_name() + L"  \x00B7  " + like_text() + L"  \x00B7  ";
    st += thousands(mr.total) + (mr.total == 1 ? L" match" : L" matches");
    if (r->partial) st += L" in view so far \x2026 counting";
    else {
        wchar_t secs[32];
        swprintf(secs, 32, L"%.2f s", r->ms / 1000.0);
        st += std::wstring(L"  \x00B7  ") + secs;
    }
    if (mr.capped) st += L"  \x00B7  first " + thousands(mr.items.size()) + L" listed";
    if (mr.too_slow || mr.timed_out) {
        size_t line = (size_t)sci(g_res_editor, SCI_LINEFROMPOSITION, r->tm->to_byte(mr.stopped_at)) + 1;
        st += mr.too_slow ? L"  \x00B7  stopped: this regex is too slow on line " + thousands(line)
                          : L"  \x00B7  stopped after " + std::to_wstring(g_settings.budget_s) + L" s near line " + thousands(line);
    }
    if (r->tm->had_invalid_utf8()) st += L"  \x00B7  contains invalid UTF-8; those bytes are ignored";
    set_status(st + g_status_extra);
    g_status_extra.clear();

    setup_columns();
    ListView_SetItemCountEx(g_list, (int)mr.items.size(), 0);
    InvalidateRect(g_list, nullptr, TRUE);
    if (g_prep.anchors_differ) render_explanation();
    paint_visible();

    if (!r->partial && g_pending_select != (size_t)-1) {
        size_t from = r->tm->to_unit(g_pending_select);
        g_pending_select = (size_t)-1;
        for (const MatchItem& m : mr.items) if ((size_t)m.groups[0].start >= from) { select_match(m, false); break; }
    }
}

// Index of the match at or after the caret (dir +1) or before it (dir -1).
int match_from_caret(int dir) {
    if (!g_res || g_res->mr.items.empty()) return -1;
    HWND ed = editor();
    const auto& items = g_res->mr.items;
    size_t sel_b = g_res->tm->to_unit((size_t)sci(ed, SCI_GETSELECTIONSTART));
    size_t sel_e = g_res->tm->to_unit((size_t)sci(ed, SCI_GETSELECTIONEND));
    if (dir > 0) {
        for (size_t k = 0; k < items.size(); ++k)
            if ((size_t)items[k].groups[0].start > sel_b || ((size_t)items[k].groups[0].start == sel_b && sel_b == sel_e)) return (int)k;
        return 0;
    }
    for (size_t k = items.size(); k-- > 0;)
        if ((size_t)items[k].groups[0].start < sel_b) return (int)k;
    return (int)items.size() - 1;
}

void go(int dir) {
    if (!g_res || g_doc_dirty) { start_run(); return; }
    int k = match_from_caret(dir);
    if (k < 0) return;
    select_match(g_res->mr.items[k], true);
    ListView_SetItemState(g_list, -1, 0, LVIS_SELECTED);
    ListView_SetItemState(g_list, k, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(g_list, k, FALSE);
}

void replace_current() {
    if (!g_res || g_doc_dirty || !g_prep.has_replacement) { start_run(); return; }
    HWND ed = editor();
    size_t sb = (size_t)sci(ed, SCI_GETSELECTIONSTART), se = (size_t)sci(ed, SCI_GETSELECTIONEND);
    for (const MatchItem& m : g_res->mr.items) {
        size_t b = g_res->tm->to_byte(m.groups[0].start), e = g_res->tm->to_byte(m.groups[0].end);
        if (b == sb && e == se) {
            std::string rep = narrow(m.replaced);
            sci(ed, SCI_SETTARGETRANGE, b, e);
            sci(ed, SCI_REPLACETARGET, (WPARAM)rep.size(), (LPARAM)rep.c_str());
            sci(ed, SCI_SETSEL, b + rep.size(), b + rep.size());
            g_pending_select = b + rep.size();
            g_doc_dirty = true;
            start_run();
            return;
        }
    }
    go(+1);   // like Notepad++'s Replace: the first click selects the next match
}

void replace_all() {
    if (g_cand < 0 || !g_prep_ok || !g_prep.has_replacement || g_split) return;
    HWND ed = editor();
    const char* p = (const char*)sci(ed, SCI_GETCHARACTERPOINTER);
    size_t len = (size_t)sci(ed, SCI_GETLENGTH);
    TextMap tm;
    LRESULT cp = sci(ed, SCI_GETCODEPAGE);
    tm.build(p ? p : "", p ? len : 0, cp == SC_CP_UTF8 ? SC_CP_UTF8 : GetACP());
    RunOptions ro;
    ro.max_items = (size_t)-1;
    ro.count_limit = (size_t)-1;
    ro.budget_ms = 30000;
    ro.with_replacement = true;
    if (SendDlgItemMessageW(g_dlg, ID_SCOPE, CB_GETCURSEL, 0, 0) == 1) {
        size_t a = (size_t)sci(ed, SCI_GETSELECTIONSTART), b = (size_t)sci(ed, SCI_GETSELECTIONEND);
        if (b > a) { ro.begin = tm.to_unit(a); ro.end = tm.to_unit(b); }
    }
    HCURSOR old = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    MatchRun mr = run_matches(g_prep, tm.text(), ro);
    SetCursor(old);
    if (mr.too_slow || mr.timed_out) {
        MessageBoxW(g_npp._nppHandle, L"Replace all stopped: the regex is too slow on this document. Nothing was replaced.", L"Datamodder Regex Viewer", MB_OK | MB_ICONWARNING);
        return;
    }
    if (mr.items.empty()) { set_status(L"Replace all: no matches."); return; }
    if (mr.items.size() > 1000) {
        std::wstring q = L"Replace all " + thousands(mr.items.size()) + L" matches?";
        if (MessageBoxW(g_npp._nppHandle, q.c_str(), L"Datamodder Regex Viewer", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    }
    sci(ed, SCI_BEGINUNDOACTION);
    for (size_t k = mr.items.size(); k-- > 0;) {
        const MatchItem& m = mr.items[k];
        size_t b = tm.to_byte(m.groups[0].start), e = tm.to_byte(m.groups[0].end);
        std::string rep = narrow(m.replaced);
        sci(ed, SCI_SETTARGETRANGE, b, e);
        sci(ed, SCI_REPLACETARGET, (WPARAM)rep.size(), (LPARAM)rep.c_str());
    }
    sci(ed, SCI_ENDUNDOACTION);
    g_status_extra = L"  \x00B7  replaced " + thousands(mr.items.size()) + L" (one undo step)";
    g_doc_dirty = true;
    start_run();
}

void close_second_view() {
    HWND npp = g_npp._nppHandle;
    for (int guard = 0; guard < 500 && IsWindowVisible(g_npp._scintillaSecondHandle); ++guard) {
        SendMessage(npp, NPPM_ACTIVATEDOC, 1, 0);
        SendMessage(npp, NPPM_MENUCOMMAND, 0, IDM_VIEW_GOTO_ANOTHER_VIEW);
    }
    schedule_run(0);
}

void copy_for_find() {
    if (g_cand < 0) return;
    const std::wstring& t = g_prep.npp_pattern;
    if (!OpenClipboard(g_dlg)) return;
    EmptyClipboard();
    size_t bytes = (t.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        memcpy(GlobalLock(h), t.c_str(), bytes);
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
    }
    CloseClipboard();
    std::wstring s = window_text(ctl(ID_STATUS));
    set_status(s + L"  \x00B7  copied for Notepad++ Find" + (g_prep.has_replacement ? L" (replacement: " + g_prep.npp_replacement + L")" : L""));
}

std::wstring clipboard_text() {
    std::wstring t;
    if (!OpenClipboard(g_dlg)) return t;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* p = (const wchar_t*)GlobalLock(h)) { t = p; GlobalUnlock(h); }
    }
    CloseClipboard();
    return t;
}

// ─── layout & creation ────────────────────────────────────────────────────────

void layout() {
    if (!g_dlg) return;
    RECT rc;
    GetClientRect(g_dlg, &rc);
    int W = rc.right, H = rc.bottom;
    int pad = scale(6), row = scale(22), gap = scale(4);
    int x = pad, y = pad, w = W - 2 * pad;
    int bw = scale(64);

    MoveWindow(ctl(ID_PASTE_LABEL), x, y + scale(3), w - 2 * bw - gap * 2, row - scale(3), TRUE);
    MoveWindow(ctl(ID_BTN_PASTE), x + w - 2 * bw - gap, y, bw, row, TRUE);
    MoveWindow(ctl(ID_BTN_CLEAR), x + w - bw, y, bw, row, TRUE);
    y += row + gap;
    int paste_h = scale(66);
    MoveWindow(g_paste, x, y, w, paste_h, TRUE);
    y += paste_h + gap;

    int cw = std::min(scale(320), w / 2);
    bool many = g_cands.size() > 1;
    MoveWindow(ctl(ID_FOUND), x, y + scale(3), many ? w - cw - gap : w, row - scale(3), TRUE);
    MoveWindow(ctl(ID_CANDIDATES), x + w - cw, y, cw, scale(200), TRUE);
    y += row + gap;

    int lw = scale(86);
    MoveWindow(ctl(ID_PATTERN_LABEL), x, y + scale(3), lw, row - scale(3), TRUE);
    MoveWindow(g_pattern, x + lw, y, w - lw, row, TRUE);
    y += row + gap;

    int rbw = scale(92);
    int ew = std::max(scale(120), (w - lw - 2 * rbw - 3 * gap) / 2);
    MoveWindow(ctl(ID_REPLACE_LABEL), x, y + scale(3), lw, row - scale(3), TRUE);
    MoveWindow(ctl(ID_REPLACE), x + lw, y, ew, row, TRUE);
    int nx = x + lw + ew + gap;
    int nw = std::max(0, w - (nx - x) - 2 * rbw - 2 * gap);
    MoveWindow(ctl(ID_REPL_NPP), nx, y + scale(3), nw, row - scale(3), TRUE);
    MoveWindow(ctl(ID_BTN_REPLACE), x + w - 2 * rbw - gap, y, rbw, row, TRUE);
    MoveWindow(ctl(ID_BTN_REPLACE_ALL), x + w - rbw, y, rbw, row, TRUE);
    y += row + gap;

    int bottom = H - pad - 2 * row - gap;
    int mid_h = std::max(scale(40), bottom - y - gap);
    int ex_w = w * 45 / 100;
    MoveWindow(g_explain, x, y, ex_w, mid_h, TRUE);
    MoveWindow(g_list, x + ex_w + gap, y, w - ex_w - gap, mid_h, TRUE);
    MoveWindow(ctl(ID_BAR), x, y, w, std::min(mid_h, scale(48)), TRUE);
    MoveWindow(ctl(ID_BTN_CLOSE_VIEW), x + w - scale(150) - pad, y + scale(12), scale(150), row, TRUE);
    y += mid_h + gap;

    int cx = x;
    auto place = [&](int id, int width) { MoveWindow(ctl(id), cx, y, width, id == ID_LIKE || id == ID_SCOPE ? scale(300) : row, TRUE); cx += width + gap; };
    place(ID_LIKE, scale(200));
    place(ID_FLAG_I, scale(34));
    place(ID_FLAG_M, scale(38));
    place(ID_FLAG_S, scale(34));
    place(ID_FLAG_X, scale(34));
    place(ID_SCOPE, scale(96));
    int right = x + w;
    MoveWindow(ctl(ID_BTN_NEXT), right - scale(30), y, scale(30), row, TRUE);
    MoveWindow(ctl(ID_BTN_PREV), right - scale(64), y, scale(30), row, TRUE);
    MoveWindow(ctl(ID_BTN_COPY), std::max(cx, right - scale(64) - gap - scale(170)), y, scale(170), row, TRUE);
    y += row + gap;
    MoveWindow(ctl(ID_STATUS), x, y + scale(2), w, row - scale(2), TRUE);
}

HWND make(int id, const wchar_t* cls, const wchar_t* text, DWORD style, DWORD ex = 0) {
    HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, g_dlg, (HMENU)(INT_PTR)id, g_module, nullptr);
    SendMessage(h, WM_SETFONT, SendMessage(g_dlg, WM_GETFONT, 0, 0), FALSE);
    g_ctl[id - 1000] = h;
    return h;
}

void setup_scintilla(HWND h, bool readonly, bool wrap) {
    COLORREF fg = (COLORREF)SendMessage(g_npp._nppHandle, NPPM_GETEDITORDEFAULTFOREGROUNDCOLOR, 0, 0);
    COLORREF bg = (COLORREF)SendMessage(g_npp._nppHandle, NPPM_GETEDITORDEFAULTBACKGROUNDCOLOR, 0, 0);
    sci(h, SCI_SETCODEPAGE, SC_CP_UTF8);
    sci(h, SCI_STYLESETFONT, STYLE_DEFAULT, (LPARAM)"Consolas");
    sci(h, SCI_STYLESETSIZE, STYLE_DEFAULT, 10);
    sci(h, SCI_STYLESETFORE, STYLE_DEFAULT, fg);
    sci(h, SCI_STYLESETBACK, STYLE_DEFAULT, bg);
    sci(h, SCI_STYLECLEARALL);
    sci(h, SCI_STYLESETFORE, 1, dark() ? RGB(255, 183, 77) : RGB(176, 96, 0));      // warnings
    sci(h, SCI_STYLESETFORE, 2, dark() ? RGB(239, 154, 154) : RGB(198, 40, 40));    // errors
    sci(h, SCI_STYLESETBOLD, 2, 1);
    sci(h, SCI_SETCARETFORE, fg);
    for (int m = 0; m < 5; ++m) sci(h, SCI_SETMARGINWIDTHN, m, 0);
    sci(h, SCI_SETWRAPMODE, wrap ? 1 : 0);
    sci(h, SCI_SETHSCROLLBAR, wrap ? 0 : 1);
    sci(h, SCI_SETSCROLLWIDTHTRACKING, 1);
    sci(h, SCI_SETCARETLINEVISIBLE, 0);
    if (readonly) sci(h, SCI_SETREADONLY, 1);
    style_indicators(h);
}

HWND make_scintilla(bool readonly, bool wrap) {
    HWND h = (HWND)SendMessage(g_npp._nppHandle, NPPM_CREATESCINTILLAHANDLE, 0, (LPARAM)g_dlg);
    setup_scintilla(h, readonly, wrap);
    ShowWindow(h, SW_SHOW);           // Notepad++ creates it hidden
    return h;
}

LRESULT on_list_notify(NMHDR* h) {
    switch (h->code) {
    case LVN_GETDISPINFOW: {
        auto* di = (NMLVDISPINFOW*)h;
        if (!(di->item.mask & LVIF_TEXT) || !g_res) return 0;
        const auto& items = g_res->mr.items;
        if (di->item.iItem < 0 || di->item.iItem >= (int)items.size()) return 0;
        const MatchItem& m = items[di->item.iItem];
        const std::wstring& text = g_res->tm->text();
        int col = di->item.iSubItem;
        std::wstring s;
        if (col == 0) s = std::to_wstring(di->item.iItem + 1);
        else if (col == 1) s = g_doc_dirty ? L"" : std::to_wstring(sci(g_res_editor, SCI_LINEFROMPOSITION, g_res->tm->to_byte(m.groups[0].start)) + 1);
        else if (col == 2) s = cell_text(text.substr(m.groups[0].start, m.groups[0].end - m.groups[0].start));
        else {
            size_t g = (size_t)col - 2;
            if (g < m.groups.size()) {
                if (m.groups[g].start >= 0) s = cell_text(text.substr(m.groups[g].start, m.groups[g].end - m.groups[g].start));
                else s = L"\x2014";
            } else s = cell_text(m.replaced);
        }
        lstrcpynW(di->item.pszText, s.c_str(), di->item.cchTextMax);
        return 0;
    }
    case NM_DBLCLK:
    case NM_RETURN: {
        int k = ListView_GetNextItem(g_list, -1, LVNI_FOCUSED);
        if (k >= 0 && g_res && !g_doc_dirty && k < (int)g_res->mr.items.size()) select_match(g_res->mr.items[k], true);
        return 0;
    }
    case LVN_ITEMCHANGED: {
        auto* nv = (NMLISTVIEW*)h;
        if ((nv->uNewState & LVIS_SELECTED) && !(nv->uOldState & LVIS_SELECTED) && g_res && !g_doc_dirty &&
            nv->iItem >= 0 && nv->iItem < (int)g_res->mr.items.size())
            select_match(g_res->mr.items[nv->iItem], false);
        return 0;
    }
    }
    return 0;
}

INT_PTR CALLBACK panel_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        g_dlg = dlg;
        make(ID_PASTE_LABEL, L"STATIC", L"Paste the regex, or the code / AI answer that contains it:", SS_NOPREFIX | SS_ENDELLIPSIS);
        make(ID_BTN_PASTE, L"BUTTON", L"Paste", BS_PUSHBUTTON | WS_TABSTOP);
        make(ID_BTN_CLEAR, L"BUTTON", L"Clear", BS_PUSHBUTTON | WS_TABSTOP);
        g_paste = make_scintilla(false, true);
        make(ID_FOUND, L"STATIC", L"", SS_NOPREFIX | SS_ENDELLIPSIS);
        make(ID_CANDIDATES, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP);
        ShowWindow(ctl(ID_CANDIDATES), SW_HIDE);
        make(ID_PATTERN_LABEL, L"STATIC", L"Pattern:", SS_NOPREFIX);
        g_pattern = make_scintilla(true, false);
        make(ID_REPLACE_LABEL, L"STATIC", L"Replace with:", SS_NOPREFIX);
        make(ID_REPLACE, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP | WS_BORDER);
        make(ID_REPL_NPP, L"STATIC", L"", SS_NOPREFIX | SS_ENDELLIPSIS);
        make(ID_BTN_REPLACE, L"BUTTON", L"Replace", BS_PUSHBUTTON | WS_TABSTOP);
        make(ID_BTN_REPLACE_ALL, L"BUTTON", L"Replace all", BS_PUSHBUTTON | WS_TABSTOP);
        g_explain = make_scintilla(true, false);
        g_list = make(ID_LIST, WC_LISTVIEWW, L"", LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SINGLESEL | WS_TABSTOP);
        ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
        make(ID_BAR, L"STATIC", L"  Regex Viewer doesn't work with a split view. Close the second view (or move its tabs back) to continue.",
             SS_NOPREFIX | SS_CENTERIMAGE);
        make(ID_BTN_CLOSE_VIEW, L"BUTTON", L"Close second view", BS_PUSHBUTTON | WS_TABSTOP);
        ShowWindow(ctl(ID_BAR), SW_HIDE);
        ShowWindow(ctl(ID_BTN_CLOSE_VIEW), SW_HIDE);
        make(ID_LIKE, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP);
        SendMessageW(ctl(ID_LIKE), CB_ADDSTRING, 0, (LPARAM)L"Match like Notepad++");
        SendMessageW(ctl(ID_LIKE), CB_ADDSTRING, 0, (LPARAM)L"Match like the source");
        for (int f = 1; f < (int)Flavor::Count; ++f)
            SendMessageW(ctl(ID_LIKE), CB_ADDSTRING, 0, (LPARAM)(std::wstring(L"Match like ") + flavor_name((Flavor)f)).c_str());
        g_like = g_settings.match_like == MatchLike::Source ? 1 : g_settings.match_like == MatchLike::Fixed ? (int)g_settings.fixed + 1 : 0;
        SendMessageW(ctl(ID_LIKE), CB_SETCURSEL, g_like, 0);
        make(ID_FLAG_I, L"BUTTON", L"i", BS_AUTOCHECKBOX | WS_TABSTOP);
        make(ID_FLAG_M, L"BUTTON", L"m", BS_AUTOCHECKBOX | WS_TABSTOP);
        make(ID_FLAG_S, L"BUTTON", L"s", BS_AUTOCHECKBOX | WS_TABSTOP);
        make(ID_FLAG_X, L"BUTTON", L"x", BS_AUTOCHECKBOX | WS_TABSTOP);
        make(ID_SCOPE, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP);
        SendMessageW(ctl(ID_SCOPE), CB_ADDSTRING, 0, (LPARAM)L"Document");
        SendMessageW(ctl(ID_SCOPE), CB_ADDSTRING, 0, (LPARAM)L"Selection");
        SendMessageW(ctl(ID_SCOPE), CB_SETCURSEL, 0, 0);
        make(ID_BTN_COPY, L"BUTTON", L"Copy for Notepad++ Find", BS_PUSHBUTTON | WS_TABSTOP);
        make(ID_BTN_PREV, L"BUTTON", L"\x25C0", BS_PUSHBUTTON | WS_TABSTOP);
        make(ID_BTN_NEXT, L"BUTTON", L"\x25B6", BS_PUSHBUTTON | WS_TABSTOP);
        make(ID_STATUS, L"STATIC", L"", SS_NOPREFIX | SS_ENDELLIPSIS);
        render_explanation();
        update_replacement_label();
        return TRUE;
    }
    case WM_SIZE:
        layout();
        return TRUE;
    case WM_CTLCOLORSTATIC:
        if ((HWND)lp == ctl(ID_BAR)) {
            HDC dc = (HDC)wp;
            SetTextColor(dc, RGB(255, 255, 255));
            SetBkColor(dc, RGB(198, 40, 40));
            static HBRUSH red = CreateSolidBrush(RGB(198, 40, 40));
            return (INT_PTR)red;
        }
        break;
    case WM_TIMER:
        if (wp == TIMER_EXTRACT) { KillTimer(dlg, TIMER_EXTRACT); do_extract(); }
        if (wp == TIMER_RUN) start_run();
        return TRUE;
    case WM_RV_RESULT:
        on_result((Result*)lp);
        return TRUE;
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        switch (id) {
        case IDOK: case IDCANCEL: return TRUE;
        case ID_BTN_PASTE: panel_set_paste(clipboard_text()); return TRUE;
        case ID_BTN_CLEAR: sci_set_text(g_paste, "", false); SetFocus(g_paste); return TRUE;
        case ID_CANDIDATES:
            if (code == CBN_SELCHANGE) select_candidate((int)SendMessageW(ctl(ID_CANDIDATES), CB_GETCURSEL, 0, 0));
            return TRUE;
        case ID_LIKE:
            if (code == CBN_SELCHANGE) { g_like = (int)SendMessageW(ctl(ID_LIKE), CB_GETCURSEL, 0, 0); prepare_current(); }
            return TRUE;
        case ID_FLAG_I: case ID_FLAG_M: case ID_FLAG_S: case ID_FLAG_X:
            if (code == BN_CLICKED) { g_flags_user = true; prepare_current(); }
            return TRUE;
        case ID_SCOPE:
            if (code == CBN_SELCHANGE) schedule_run(0);
            return TRUE;
        case ID_REPLACE:
            if (code == EN_CHANGE && g_cand >= 0) { g_repl_user = true; prepare_current(); }
            return TRUE;
        case ID_BTN_REPLACE: replace_current(); return TRUE;
        case ID_BTN_REPLACE_ALL: replace_all(); return TRUE;
        case ID_BTN_COPY: copy_for_find(); return TRUE;
        case ID_BTN_PREV: go(-1); return TRUE;
        case ID_BTN_NEXT: go(+1); return TRUE;
        case ID_BTN_CLOSE_VIEW: close_second_view(); return TRUE;
        }
        break;
    }
    case WM_NOTIFY: {
        NMHDR* h = (NMHDR*)lp;
        if (h->hwndFrom == g_list) { SetWindowLongPtrW(dlg, DWLP_MSGRESULT, on_list_notify(h)); return TRUE; }
        if (h->hwndFrom == g_paste && h->code == SCN_MODIFIED) {
            const SCNotification* n = (const SCNotification*)lp;
            if (n->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) SetTimer(dlg, TIMER_EXTRACT, 250, nullptr);
            return TRUE;
        }
        if (LOWORD(h->code) == DMN_CLOSE) {
            g_visible = false;
            g_run++;
            clear_indicators(g_npp._scintillaMainHandle);
            clear_indicators(g_npp._scintillaSecondHandle);
            SendMessage(g_npp._nppHandle, NPPM_SETMENUITEMCHECK, g_show_cmd, FALSE);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

void ensure_created() {
    if (g_dlg) return;
    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icc);
    if (!g_ind) {
        int start = 0;
        if (SendMessage(g_npp._nppHandle, NPPM_ALLOCATEINDICATOR, NGROUP_COLORS + 1, (LPARAM)&start) && start > 0) g_ind = start;
        else g_ind = 9;   // fallback: Scintilla's container range starts at 8
        style_indicators(g_npp._scintillaMainHandle);
        style_indicators(g_npp._scintillaSecondHandle);
    }
    CreateDialogParamW(g_module, MAKEINTRESOURCEW(IDD_PANEL), g_npp._nppHandle, panel_proc, 0);
    if (!g_dlg) return;
    g_dock.hClient = g_dlg;
    g_dock.pszName = L"Regex Viewer";
    g_dock.dlgID = g_show_index;
    g_dock.uMask = DWS_DF_CONT_BOTTOM | DWS_ICONTAB;
    g_dock.hIconTab = (HICON)LoadImageW(g_module, MAKEINTRESOURCEW(IDI_TB_LENS), IMAGE_ICON, 16, 16, 0);
    g_dock.pszModuleName = L"RegexViewer.dll";
    SendMessage(g_npp._nppHandle, NPPM_DMMREGASDCKDLG, 0, (LPARAM)&g_dock);
    SendMessage(g_npp._nppHandle, NPPM_MODELESSDIALOG, MODELESSDIALOGADD, (LPARAM)g_dlg);
    SendMessage(g_npp._nppHandle, NPPM_DARKMODESUBCLASSANDTHEME, DMF_INIT, (LPARAM)g_dlg);
}

} // namespace

// ─── public ───────────────────────────────────────────────────────────────────

void panel_init(int show_func_index, int show_cmd_id) {
    g_show_index = show_func_index;
    g_show_cmd = show_cmd_id;
}

void panel_show() {
    ensure_created();
    if (!g_dlg) return;
    SendMessage(g_npp._nppHandle, NPPM_DMMSHOW, 0, (LPARAM)g_dlg);
    SendMessage(g_npp._nppHandle, NPPM_SETMENUITEMCHECK, g_show_cmd, TRUE);
    g_visible = true;
    schedule_run(0);
    SetFocus(g_paste);
}

void panel_set_paste(const std::wstring& text) {
    panel_show();
    if (!g_dlg) return;
    sci_set_text(g_paste, narrow(text), false);
    KillTimer(g_dlg, TIMER_EXTRACT);
    do_extract();
    SetFocus(g_paste);
}

void panel_next() { if (g_visible) go(+1); else panel_show(); }
void panel_prev() { if (g_visible) go(-1); else panel_show(); }
void panel_replace() { if (g_visible) replace_current(); else panel_show(); }
void panel_replace_all() { if (g_visible) replace_all(); else panel_show(); }

void panel_clear_highlights() {
    g_run++;
    g_res.reset();
    clear_indicators(g_npp._scintillaMainHandle);
    clear_indicators(g_npp._scintillaSecondHandle);
    if (g_list) ListView_SetItemCountEx(g_list, 0, 0);
}

void panel_on_editor_notify(const SCNotification* n) {
    if (!g_visible || !g_dlg) return;
    if (n->nmhdr.code == SCN_MODIFIED && (n->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT))) {
        if ((HWND)n->nmhdr.hwndFrom != editor()) return;
        g_doc_dirty = true;
        if (g_settings.rerun_on_edit) schedule_run();
    } else if (n->nmhdr.code == SCN_UPDATEUI && (n->updated & SC_UPDATE_V_SCROLL)) {
        if ((HWND)n->nmhdr.hwndFrom == editor()) paint_visible();
    }
}

void panel_on_buffer_activated() {
    if (!g_visible || !g_dlg) return;
    g_res.reset();
    g_doc_dirty = true;
    clear_indicators(g_npp._scintillaMainHandle);
    clear_indicators(g_npp._scintillaSecondHandle);
    schedule_run(0);
}

void panel_on_dark_mode() {
    if (g_ind) { style_indicators(g_npp._scintillaMainHandle); style_indicators(g_npp._scintillaSecondHandle); }
    if (!g_dlg) return;
    SendMessage(g_npp._nppHandle, NPPM_DARKMODESUBCLASSANDTHEME, DMF_HANDLE_CHANGE, (LPARAM)g_dlg);
    for (HWND h : { g_paste, g_pattern, g_explain }) setup_scintilla(h, h != g_paste, h == g_paste);
    render_pattern();
    render_explanation();
    paint_visible();
}

void panel_shutdown() {
    g_run++;
    g_visible = false;
}
