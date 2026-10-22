#define _CRT_SECURE_NO_WARNINGS
// Console harness for Regex Viewer's pure parts.
//
//   test_harness extract <paste.txt>     candidates found in a paste
//   test_harness explain <patterns.txt>  one pattern per line ("flags|pattern" or "pattern")
//   test_harness match   <cases.txt>     matching cases, see tests/match_in.txt
//   test_harness encode  <file>          UTF-8 / ANSI position mapping
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include "extract.h"
#include "explain.h"
#include "flavor.h"
#include "matcher.h"
#include "encoding.h"

using namespace rv;

static std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
static std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
static std::string read_all(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return {};
    std::string s;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}
static void put(const std::string& s) { fwrite(s.data(), 1, s.size(), stdout); }
static void putw(const std::wstring& w) { put(utf8(w)); }

// Show control characters so goldens stay readable.
static std::wstring vis(const std::wstring& s) {
    std::wstring o;
    for (wchar_t c : s) {
        if (c == L'\n') o += L"\\n";
        else if (c == L'\r') o += L"\\r";
        else if (c == L'\t') o += L"\\t";
        else o += c;
    }
    return o;
}

static Flags parse_flags(const std::wstring& f) {
    Flags fl;
    for (wchar_t c : f) { if (c == L'i') fl.i = true; if (c == L'm') fl.m = true; if (c == L's') fl.s = true; if (c == L'x') fl.x = true; }
    return fl;
}

static int do_extract(const std::wstring& paste) {
    auto cands = extract(paste);
    put("candidates: " + std::to_string(cands.size()) + "\n");
    for (size_t k = 0; k < cands.size(); ++k) {
        const Candidate& c = cands[k];
        put("[" + std::to_string(k + 1) + "] ");
        putw(c.source);
        put("\n    flavor:  "); putw(flavor_name(c.flavor));
        put(c.recognised ? "" : " (not recognised)");
        put("\n    pattern: "); putw(vis(c.pattern));
        if (!flags_text(c.flags).empty()) { put("\n    flags:   "); putw(flags_text(c.flags)); }
        if (c.has_replacement) { put("\n    replace: "); putw(vis(c.replacement)); put("  ->  "); putw(vis(convert_replacement(c.replacement, c.flavor))); }
        for (auto& n : c.notes) { put("\n    note:    "); putw(n); }
        put("\n");
    }
    return 0;
}

static int do_explain(const std::wstring& all) {
    size_t i = 0;
    while (i < all.size()) {
        size_t e = all.find(L'\n', i);
        if (e == std::wstring::npos) e = all.size();
        std::wstring line = all.substr(i, e - i);
        i = e + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.empty() || line[0] == L'#') continue;
        Flags fl;
        std::wstring pat = line;
        size_t bar = line.find(L"|||");
        if (bar != std::wstring::npos) { fl = parse_flags(line.substr(0, bar)); pat = line.substr(bar + 3); }
        Prepared p = prepare(pat, nullptr, Flavor::NotepadPP, Flavor::NotepadPP, fl);
        int pos = -1;
        std::wstring err = check_pattern(p, &pos);
        put("=== "); putw(pat); put("\n");
        if (!err.empty()) { put("    engine error at " + std::to_string(pos) + ": "); putw(err); put("\n"); continue; }
        ExplainContext ctx;
        ctx.multiline_anchors = p.multiline_anchors();
        ctx.dot_all = p.dot_all();
        for (const ExplainLine& l : explain(p.parse_pattern, p.parse, ctx)) {
            std::wstring code = l.code;
            std::wstring row = std::wstring(4 + 2 * l.depth, L' ') + code;
            if (row.size() < 34) row += std::wstring(34 - row.size(), L' ');
            else row += L"  ";
            putw(row + l.text);
            if (l.group) put("   [g" + std::to_string(l.group) + "]");
            put("\n");
        }
        for (auto& w : p.warnings) { put("    ! "); putw(w); put("\n"); }
    }
    return 0;
}

// Cases:  @case <name>
//         @flavor <key>  @source <key>  @flags <imsx>   (optional)
//         @pattern <pattern>
//         @replace <replacement>                         (optional)
//         @text           then the text up to @end
static int do_match(const std::wstring& all) {
    std::vector<std::wstring> lines;
    size_t i = 0;
    while (i <= all.size()) {
        size_t e = all.find(L'\n', i);
        if (e == std::wstring::npos) e = all.size();
        std::wstring l = all.substr(i, e - i);
        if (!l.empty() && l.back() == L'\r') l.pop_back();
        lines.push_back(l);
        i = e + 1;
        if (e == all.size()) break;
    }
    for (size_t k = 0; k < lines.size(); ++k) {
        if (lines[k].compare(0, 5, L"@case") != 0) continue;
        std::wstring name = lines[k].substr(6), pat, rep, text;
        Flavor fl = Flavor::NotepadPP, src = Flavor::NotepadPP;
        Flags flags;
        bool has_rep = false;
        for (++k; k < lines.size(); ++k) {
            const std::wstring& l = lines[k];
            if (l.compare(0, 7, L"@flavor") == 0) flavor_from_key(utf8(l.substr(8)), fl);
            else if (l.compare(0, 7, L"@source") == 0) flavor_from_key(utf8(l.substr(8)), src);
            else if (l.compare(0, 6, L"@flags") == 0) flags = parse_flags(l.substr(7));
            else if (l.compare(0, 8, L"@pattern") == 0) pat = l.substr(9);
            else if (l.compare(0, 8, L"@replace") == 0) { rep = l.size() > 9 ? l.substr(9) : L""; has_rep = true; }
            else if (l == L"@text") {
                for (++k; k < lines.size() && lines[k] != L"@end"; ++k) text += lines[k] + L"\n";
                break;
            }
        }
        Prepared p = prepare(pat, has_rep ? &rep : nullptr, src, fl, flags);
        RunOptions ro;
        ro.with_replacement = has_rep;
        ro.max_items = 20;
        MatchRun r = run_matches(p, text, ro);
        put("=== "); putw(name); put("  ("); putw(flavor_name(fl)); put(")\n");
        if (!r.compiled) { put("    compile error: "); putw(r.error); put("\n"); continue; }
        if (p.rejected) { put("    rejected: "); putw(p.reject_reason); put("\n"); continue; }
        if (p.anchors_differ) put("    anchors differ from the source\n");
        put("    matches: " + std::to_string(r.total) + (r.too_slow ? "  (too slow)" : "") + (r.timed_out ? "  (timed out)" : "") + "\n");
        for (auto& m : r.items) {
            put("    ");
            for (size_t g = 0; g < m.groups.size(); ++g) {
                const Span& sp = m.groups[g];
                if (g) put(" ");
                if (sp.start < 0) { put("-"); continue; }
                put(g ? "g" + std::to_string(g) + "=" : "");
                putw(L"[" + vis(text.substr(sp.start, sp.end - sp.start)) + L"]");
            }
            if (has_rep) { put("  ->  "); putw(L"[" + vis(m.replaced) + L"]"); }
            put("\n");
        }
        if (has_rep) { put("    npp replacement: "); putw(p.npp_replacement); put("\n"); }
        for (auto& w : p.warnings) { put("    ! "); putw(w); put("\n"); }
    }
    return 0;
}

static int do_encode(const std::string& bytes) {
    for (unsigned cp : { 65001u, 1252u }) {
        TextMap tm;
        tm.build(bytes.data(), bytes.size(), cp);
        put("codepage " + std::to_string(cp) + ": " + std::to_string(bytes.size()) + " bytes -> " +
            std::to_string(tm.text().size()) + " units" + (tm.had_invalid_utf8() ? " (invalid UTF-8 seen)" : "") + "\n");
        bool ok = true;
        for (size_t u = 0; u <= tm.text().size(); ++u) {
            size_t b = tm.to_byte(u);
            size_t back = tm.to_unit(b);
            bool low_surrogate = u < tm.text().size() && tm.text()[u] >= 0xDC00 && tm.text()[u] <= 0xDFFF;
            if (back != u && !low_surrogate) { ok = false; put("  mismatch at unit " + std::to_string(u) + "\n"); break; }
        }
        put(ok ? "  round trip ok\n" : "");
        // A few sample positions
        for (size_t u : { (size_t)0, (size_t)5, tm.text().size() }) {
            if (u > tm.text().size()) continue;
            put("  unit " + std::to_string(u) + " -> byte " + std::to_string(tm.to_byte(u)) + "\n");
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: test_harness extract|explain|match|encode <file>\n"); return 2; }
    std::string mode = argv[1];
    std::string bytes = read_all(argv[2]);
    if (mode == "encode") return do_encode(bytes);
    std::wstring text = wide(bytes);
    if (mode == "extract") return do_extract(text);
    if (mode == "explain") return do_explain(text);
    if (mode == "match")   return do_match(text);
    return 2;
}
