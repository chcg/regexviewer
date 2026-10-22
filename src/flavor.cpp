#include "flavor.h"
#include <algorithm>
#include <cwctype>

namespace rv {

static const wchar_t* const NAMES[] = {
    L"Notepad++", L"Python", L"JavaScript", L".NET", L"Java", L"PCRE", L"POSIX ERE", L"POSIX BRE", L"RE2",
};
static const char* const KEYS[] = {
    "npp", "python", "javascript", "dotnet", "java", "pcre", "ere", "bre", "re2",
};

const wchar_t* flavor_name(Flavor f) { return NAMES[(int)f]; }
const char*    flavor_key(Flavor f)  { return KEYS[(int)f]; }

bool flavor_from_key(const std::string& key, Flavor& out) {
    for (int i = 0; i < (int)Flavor::Count; ++i)
        if (key == KEYS[i]) { out = (Flavor)i; return true; }
    return false;
}

std::wstring flags_text(const Flags& f) {
    std::wstring s;
    auto add = [&](const wchar_t* t) { if (!s.empty()) s += L", "; s += t; };
    if (f.i) add(L"i");
    if (f.m) add(L"m");
    if (f.s) add(L"s");
    if (f.x) add(L"x");
    return s;
}

static bool is_posix(Flavor f) { return f == Flavor::PosixERE || f == Flavor::PosixBRE; }

bool Prepared::multiline_anchors() const {
    return flavor == Flavor::NotepadPP || is_posix(flavor) || flags.m;
}

// BRE <-> ERE: in BRE \( \) \{ \} \| \+ \? are operators and the bare
// characters are literals; in ERE it is the other way round. The swap is its
// own inverse. Brackets are copied untouched.
static std::wstring swap_bre_ere(const std::wstring& p) {
    std::wstring o;
    bool in_class = false;
    for (size_t i = 0; i < p.size(); ++i) {
        wchar_t c = p[i];
        if (in_class) {
            o += c;
            if (c == L']') in_class = false;
            continue;
        }
        if (c == L'[') {
            in_class = true;
            o += c;
            if (i + 1 < p.size() && p[i + 1] == L'^') o += p[++i];
            if (i + 1 < p.size() && p[i + 1] == L']') o += p[++i];
            continue;
        }
        if (c == L'\\' && i + 1 < p.size()) {
            wchar_t n = p[i + 1];
            if (wcschr(L"(){}|+?", n)) { o += n; ++i; continue; }
            o += c; o += n; ++i;
            continue;
        }
        if (wcschr(L"(){}|+?", c)) { o += L'\\'; o += c; continue; }
        o += c;
    }
    return o;
}

// Python / Go syntax Boost doesn't take: (?P<name>…) -> (?<name>…),
// (?P=name) -> \k<name>, Python's \Z (absolute end) -> \z.
static std::wstring to_boost_perl(const std::wstring& p, Flavor source) {
    std::wstring o;
    bool in_class = false;
    for (size_t i = 0; i < p.size(); ++i) {
        wchar_t c = p[i];
        if (c == L'\\' && i + 1 < p.size()) {
            if (!in_class && p[i + 1] == L'Z' && source == Flavor::Python) { o += L"\\z"; ++i; continue; }
            o += c; o += p[++i];
            continue;
        }
        if (in_class) { o += c; if (c == L']') in_class = false; continue; }
        if (c == L'[') { in_class = true; o += c; if (i + 1 < p.size() && p[i + 1] == L'^') o += p[++i]; if (i + 1 < p.size() && p[i + 1] == L']') o += p[++i]; continue; }
        if (c == L'(' && p.compare(i, 4, L"(?P<") == 0) { o += L"(?<"; i += 3; continue; }
        if (c == L'(' && p.compare(i, 4, L"(?P=") == 0) {
            size_t e = p.find(L')', i + 4);
            if (e != std::wstring::npos) { o += L"\\k<" + p.substr(i + 4, e - i - 4) + L">"; i = e; continue; }
        }
        o += c;
    }
    return o;
}

// \p{L}, \p{Lu}, \p{Ll}, \p{N} … -> POSIX classes Boost understands without
// ICU. An approximation: the classes follow Windows' character tables.
static bool posix_for_prop(const std::wstring& name, bool negated, std::wstring& cls) {
    std::wstring n = name;
    if (n == L"L" || n == L"Letter" || n == L"Alpha" || n == L"Alphabetic") cls = L"alpha";
    else if (n == L"Lu" || n == L"Uppercase_Letter" || n == L"Upper")      cls = L"upper";
    else if (n == L"Ll" || n == L"Lowercase_Letter" || n == L"Lower")      cls = L"lower";
    else if (n == L"N" || n == L"Nd" || n == L"Number" || n == L"Digit")   cls = L"digit";
    else if (n == L"P" || n == L"Punctuation" || n == L"Punct")            cls = L"punct";
    else if (n == L"Z" || n == L"Zs" || n == L"Separator" || n == L"Space") cls = L"space";
    else return false;
    if (negated) cls = L"^" + cls;
    return true;
}

static std::wstring props_to_posix(const std::wstring& p, bool& changed) {
    std::wstring o;
    bool in_class = false;
    for (size_t i = 0; i < p.size(); ++i) {
        wchar_t c = p[i];
        if (c == L'\\' && i + 1 < p.size() && (p[i + 1] == L'p' || p[i + 1] == L'P')) {
            bool neg = p[i + 1] == L'P';
            std::wstring name;
            size_t j = i + 2;
            if (j < p.size() && p[j] == L'{') {
                size_t e = p.find(L'}', j);
                if (e != std::wstring::npos) { name = p.substr(j + 1, e - j - 1); j = e + 1; }
            } else if (j < p.size()) { name = p.substr(j, 1); ++j; }
            if (!name.empty() && name[0] == L'^') { neg = !neg; name = name.substr(1); }
            std::wstring cls;
            if (posix_for_prop(name, neg, cls)) {
                bool cneg = cls[0] == L'^';
                std::wstring base = cneg ? cls.substr(1) : cls;
                if (in_class) o += L"[:" + base + L":]";            // negation inside a class isn't expressible; rare
                else o += cneg ? L"[^[:" + base + L":]]" : L"[[:" + base + L":]]";
                changed = true;
                i = j - 1;
                continue;
            }
        }
        if (c == L'\\' && i + 1 < p.size()) { o += c; o += p[++i]; continue; }
        if (!in_class && c == L'[') { in_class = true; o += c; if (i + 1 < p.size() && p[i + 1] == L'^') o += p[++i]; if (i + 1 < p.size() && p[i + 1] == L']') o += p[++i]; continue; }
        if (in_class && c == L']') in_class = false;
        o += c;
    }
    return o;
}

std::wstring convert_replacement(const std::wstring& r, Flavor src) {
    std::wstring o;
    auto digits = [&](size_t& i, size_t max) {
        std::wstring d;
        while (i < r.size() && d.size() < max && iswdigit(r[i])) d += r[i++];
        return d;
    };
    for (size_t i = 0; i < r.size();) {
        wchar_t c = r[i];
        if (src == Flavor::Python && c == L'\\' && i + 1 < r.size()) {
            if (r[i + 1] == L'g' && i + 2 < r.size() && r[i + 2] == L'<') {
                size_t e = r.find(L'>', i + 3);
                if (e != std::wstring::npos) {
                    std::wstring n = r.substr(i + 3, e - i - 3);
                    bool num = !n.empty() && std::all_of(n.begin(), n.end(), [](wchar_t ch) { return iswdigit(ch) != 0; });
                    o += num ? L"${" + n + L"}" : L"$+{" + n + L"}";
                    i = e + 1;
                    continue;
                }
            }
            if (iswdigit(r[i + 1])) { ++i; std::wstring d = digits(i, 2); o += L"${" + d + L"}"; continue; }
            o += c; o += r[i + 1]; i += 2;
            continue;
        }
        if ((src == Flavor::PCRE || is_posix(src)) && c == L'\\' && i + 1 < r.size() && iswdigit(r[i + 1])) {
            ++i; std::wstring d = digits(i, 2); o += L"${" + d + L"}";
            continue;
        }
        if (is_posix(src) && c == L'&') { o += L"$&"; ++i; continue; }
        if (is_posix(src) && c == L'\\' && i + 1 < r.size() && r[i + 1] == L'&') { o += L"&"; i += 2; continue; }
        if (src == Flavor::Java && c == L'\\' && i + 1 < r.size() && r[i + 1] == L'$') { o += L"$$"; i += 2; continue; }
        if (c == L'$' && i + 1 < r.size()) {
            wchar_t n = r[i + 1];
            if (n == L'<' && src == Flavor::JavaScript) {
                size_t e = r.find(L'>', i + 2);
                if (e != std::wstring::npos) { o += L"$+{" + r.substr(i + 2, e - i - 2) + L"}"; i = e + 1; continue; }
            }
            if (n == L'{') {
                size_t e = r.find(L'}', i + 2);
                if (e != std::wstring::npos) {
                    std::wstring nm = r.substr(i + 2, e - i - 2);
                    bool num = !nm.empty() && std::all_of(nm.begin(), nm.end(), [](wchar_t ch) { return iswdigit(ch) != 0; });
                    o += num ? L"${" + nm + L"}" : L"$+{" + nm + L"}";
                    i = e + 1;
                    continue;
                }
            }
            if (iswdigit(n)) {
                ++i;
                std::wstring d = digits(i, 2);
                if (d == L"0" && (src == Flavor::DotNet || src == Flavor::Java || src == Flavor::RE2)) o += L"$&";
                else o += L"${" + d + L"}";
                continue;
            }
        }
        o += c;
        ++i;
    }
    return o;
}

Prepared prepare(const std::wstring& pattern, const std::wstring* replacement,
                 Flavor source, Flavor flavor, const Flags& flags) {
    Prepared p;
    p.source = source;
    p.flavor = flavor;
    p.flags = flags;

    // Perl-syntax form of the pattern, whatever it came from.
    std::wstring perl = source == Flavor::PosixBRE ? swap_bre_ere(pattern) : to_boost_perl(pattern, source);
    p.parse_pattern = perl;

    if (flavor == Flavor::PosixBRE)      p.engine_pattern = source == Flavor::PosixBRE ? pattern : swap_bre_ere(perl);
    else if (flavor == Flavor::PosixERE) p.engine_pattern = perl;
    else                                 p.engine_pattern = perl;

    // Unicode properties: Notepad++ doesn't know them (its Find gives an
    // error, and so do we); the flavours that do get a POSIX approximation.
    bool props_converted = false;
    if (flavor != Flavor::NotepadPP && flavor != Flavor::Python)
        p.engine_pattern = props_to_posix(p.engine_pattern, props_converted);

    std::wstring inline_flags;
    if (flags.i) inline_flags += L'i';
    if (flags.s) inline_flags += L's';
    if (flags.x) inline_flags += L'x';
    p.npp_pattern = (inline_flags.empty() ? L"" : L"(?" + inline_flags + L")") + perl;

    if (replacement) {
        p.has_replacement = true;
        p.npp_replacement = convert_replacement(*replacement, source);
    }

    p.parse = parse_regex(perl, flags.x);
    // The parser sees the converted pattern; Python syntax is detected on the original.
    if (pattern.find(L"(?P<") != std::wstring::npos || pattern.find(L"(?P=") != std::wstring::npos)
        p.parse.has_python_named = true;
    const ParseResult& pr = p.parse;

    // ── what the flavour in effect can't do ──
    auto warn = [&](const std::wstring& w) { p.warnings.push_back(w); };
    switch (flavor) {
    case Flavor::RE2: {
        std::wstring what;
        if (pr.has_lookahead)  what = L"lookahead";
        else if (pr.has_lookbehind) what = L"lookbehind";
        else if (pr.has_backref)    what = L"backreferences";
        else if (pr.has_possessive) what = L"possessive quantifiers";
        else if (pr.has_atomic)     what = L"atomic groups";
        if (!what.empty()) {
            p.rejected = true;
            p.reject_reason = L"RE2 (Go, Rust, Google Sheets, BigQuery) would reject this regex: it doesn't support " + what + L". Nothing is highlighted, because that is what those tools would do.";
        }
        break;
    }
    case Flavor::JavaScript:
        if (pr.has_possessive) warn(L"JavaScript has no possessive quantifiers (*+, ++, ?+): this regex is a syntax error there.");
        if (pr.has_atomic)     warn(L"JavaScript has no atomic groups (?>…): this regex is a syntax error there.");
        if (pr.has_shorthand)  warn(L"In JavaScript (without the u flag) \\d, \\w and \\b only cover ASCII; here they also match other scripts, such as Arabic digits.");
        break;
    case Flavor::Python:
        if (pr.has_possessive || pr.has_atomic) warn(L"Possessive quantifiers and atomic groups need Python 3.11 or newer.");
        break;
    case Flavor::PosixERE:
    case Flavor::PosixBRE:
        if (pr.has_shorthand) warn(L"\\d, \\w and \\s are not POSIX. GNU grep and sed accept \\w and \\s, but not \\d: use [[:digit:]] or [0-9].");
        if (pr.has_lazy)      warn(L"POSIX has no lazy quantifiers (*?, +?): the ? is read differently by grep and sed.");
        if (pr.has_lookahead || pr.has_lookbehind) warn(L"POSIX has no lookahead or lookbehind; use grep -P for that.");
        break;
    default:
        break;
    }

    if (pr.has_unicode_prop && flavor == Flavor::NotepadPP)
        warn(L"Notepad++'s Find doesn't support \\p{\x2026} Unicode properties. Use POSIX classes instead: [[:alpha:]] for \\p{L}, [[:upper:]] for \\p{Lu}, [[:lower:]] for \\p{Ll}, [[:digit:]] for \\p{N}.");
    if (pr.has_unicode_prop && flavor == Flavor::Python)
        warn(L"Python's re module has no \\p{\x2026} Unicode properties (the third-party regex module does).");
    if (props_converted)
        warn(L"\\p{\x2026} Unicode properties are matched with the closest POSIX class ([[:upper:]] for \\p{Lu} and so on); the result can differ slightly for rare characters.");
    if (pr.has_python_named && flavor == Flavor::NotepadPP)
        warn(L"(?P<name>…) is Python syntax. Notepad++ needs (?<name>…); the converted form is used and copied.");
    if (pr.has_balancing)
        warn(L".NET balancing groups (?<a-b>…) are not supported by Notepad++'s engine.");
    if (pr.has_unknown)
        warn(L"Some parts are marked \"not explained\". The highlights are still exact: they come from the regex engine.");

    p.anchors_differ = flavor == Flavor::NotepadPP && source != Flavor::NotepadPP && !is_posix(source) &&
                       !flags.m && pr.has_line_anchors;
    return p;
}

} // namespace rv
