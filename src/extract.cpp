#define BOOST_REGEX_STANDALONE
#include <boost/regex.hpp>
#include "extract.h"
#include <algorithm>
#include <cwctype>

namespace rv {

namespace {

// ─── string literals ──────────────────────────────────────────────────────────

enum class Style { Python, Java, Kotlin, CSharp, JS, Go, Rust, PHP, Shell, PowerShell };

struct Lit {
    bool ok = false;
    std::wstring value;
    size_t end = 0;                      // index after the literal
    std::wstring kind;                   // "raw string", "verbatim string", …
    std::vector<std::wstring> notes;
};

bool is_space(wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; }

size_t skip_ws(const std::wstring& s, size_t i) {
    while (i < s.size() && is_space(s[i])) ++i;
    return i;
}

// Escapes inside a C-family string. `unknown`: what to do with \d and friends.
enum class Unknown { Keep, Drop };

std::wstring unescape(const std::wstring& raw, Unknown unk, const wchar_t* lang, std::vector<std::wstring>& notes) {
    std::wstring o;
    std::wstring dropped;
    for (size_t i = 0; i < raw.size(); ++i) {
        wchar_t c = raw[i];
        if (c != L'\\' || i + 1 >= raw.size()) { o += c; continue; }
        wchar_t n = raw[++i];
        switch (n) {
        case L'\\': o += L'\\'; break;
        case L'"':  o += L'"'; break;
        case L'\'': o += L'\''; break;
        case L'`':  o += L'`'; break;
        case L'n':  o += L'\n'; break;
        case L't':  o += L'\t'; break;
        case L'r':  o += L'\r'; break;
        case L'0':  o += L'\0'; break;
        case L'/':  o += L'/'; break;
        case L'u': {
            size_t j = i + 1;
            std::wstring hex;
            if (j < raw.size() && raw[j] == L'{') { size_t e = raw.find(L'}', j); if (e != std::wstring::npos) { hex = raw.substr(j + 1, e - j - 1); i = e; } }
            else { while (j < raw.size() && hex.size() < 4 && iswxdigit(raw[j])) hex += raw[j++]; i = j - 1; }
            if (!hex.empty()) { o += (wchar_t)std::wcstoul(hex.c_str(), nullptr, 16); break; }
            o += L"\\u";
            break;
        }
        case L'x': {
            size_t j = i + 1;
            std::wstring hex;
            while (j < raw.size() && hex.size() < 2 && iswxdigit(raw[j])) hex += raw[j++];
            if (!hex.empty()) { o += (wchar_t)std::wcstoul(hex.c_str(), nullptr, 16); i = j - 1; break; }
            o += L"\\x";
            break;
        }
        default:
            if (unk == Unknown::Drop) {
                o += n;
                if (dropped.find(n) == std::wstring::npos) dropped += n;
            } else {
                o += L'\\';
                o += n;
            }
        }
    }
    if (!dropped.empty()) {
        std::wstring list;
        for (wchar_t d : dropped) { if (!list.empty()) list += L", "; list += L"\\"; list += d; }
        notes.push_back(std::wstring(L"In a ") + lang + L" string " + list + L" lose their backslash (\\d becomes just \"d\"). "
                        L"The pattern shown is what " + lang + L" really gets; write \\\\d in the string, or use a /…/ literal.");
    }
    return o;
}

// Reads a quoted string starting at s[i] ('"', '\'', '`'), with backslash
// escapes left in place. Returns the raw body; `end` after the closing quote.
bool read_quoted(const std::wstring& s, size_t i, wchar_t q, std::wstring& body, size_t& end, bool backslash = true) {
    size_t j = i + 1;
    while (j < s.size()) {
        if (backslash && s[j] == L'\\' && j + 1 < s.size()) { j += 2; continue; }
        if (s[j] == q) { body = s.substr(i + 1, j - i - 1); end = j + 1; return true; }
        ++j;
    }
    return false;
}

Lit read_literal(const std::wstring& s, size_t i, Style st) {
    Lit L;
    i = skip_ws(s, i);
    if (i >= s.size()) return L;

    switch (st) {
    case Style::Python: {
        size_t j = i;
        std::wstring prefix;
        while (j < s.size() && prefix.size() < 2 && wcschr(L"rRbBuUfF", s[j])) prefix += s[j++];
        if (j >= s.size() || (s[j] != L'"' && s[j] != L'\'')) return L;
        bool raw = prefix.find_first_of(L"rR") != std::wstring::npos;
        wchar_t q = s[j];
        std::wstring body;
        if (s.compare(j, 3, std::wstring(3, q)) == 0) {
            size_t e = j + 3;
            for (;;) {
                size_t k = s.find(std::wstring(3, q), e);
                if (k == std::wstring::npos) return L;
                if (raw || k == 0 || s[k - 1] != L'\\') { body = s.substr(j + 3, k - j - 3); L.end = k + 3; break; }
                e = k + 1;
            }
        } else if (!read_quoted(s, j, q, body, L.end)) return L;
        L.value = raw ? body : unescape(body, Unknown::Keep, L"Python", L.notes);
        L.kind = raw ? L"raw string" : L"string";
        L.ok = true;
        return L;
    }
    case Style::CSharp:
        if (s.compare(i, 2, L"@\"") == 0) {
            size_t j = i + 2;
            std::wstring o;
            while (j < s.size()) {
                if (s[j] == L'"') { if (j + 1 < s.size() && s[j + 1] == L'"') { o += L'"'; j += 2; continue; } break; }
                o += s[j++];
            }
            if (j >= s.size()) return L;
            L.value = o; L.end = j + 1; L.kind = L"verbatim string"; L.ok = true;
            return L;
        }
        if (s.compare(i, 3, L"\"\"\"") == 0) {
            size_t k = s.find(L"\"\"\"", i + 3);
            if (k == std::wstring::npos) return L;
            L.value = s.substr(i + 3, k - i - 3); L.end = k + 3; L.kind = L"raw string"; L.ok = true;
            return L;
        }
        [[fallthrough]];
    case Style::Java:
    case Style::Kotlin:
        if (s.compare(i, 3, L"\"\"\"") == 0) {
            size_t k = s.find(L"\"\"\"", i + 3);
            if (k == std::wstring::npos) return L;
            std::wstring body = s.substr(i + 3, k - i - 3);
            L.value = st == Style::Kotlin ? body : unescape(body, Unknown::Keep, L"Java", L.notes);
            L.end = k + 3; L.kind = st == Style::Kotlin ? L"raw string" : L"text block"; L.ok = true;
            return L;
        }
        if (s[i] != L'"') return L;
        {
            std::wstring body;
            if (!read_quoted(s, i, L'"', body, L.end)) return L;
            L.value = unescape(body, Unknown::Keep, st == Style::CSharp ? L"C#" : st == Style::Kotlin ? L"Kotlin" : L"Java", L.notes);
            L.kind = L"string"; L.ok = true;
        }
        return L;
    case Style::JS: {
        wchar_t q = s[i];
        if (q != L'"' && q != L'\'' && q != L'`') return L;
        std::wstring body;
        if (!read_quoted(s, i, q, body, L.end)) return L;
        L.value = unescape(body, Unknown::Drop, L"JavaScript", L.notes);
        L.kind = q == L'`' ? L"template string" : L"string"; L.ok = true;
        return L;
    }
    case Style::Go: {
        std::wstring body;
        if (s[i] == L'`') {
            size_t k = s.find(L'`', i + 1);
            if (k == std::wstring::npos) return L;
            L.value = s.substr(i + 1, k - i - 1); L.end = k + 1; L.kind = L"raw string"; L.ok = true;
            return L;
        }
        if (s[i] != L'"' || !read_quoted(s, i, L'"', body, L.end)) return L;
        L.value = unescape(body, Unknown::Keep, L"Go", L.notes); L.kind = L"string"; L.ok = true;
        return L;
    }
    case Style::Rust: {
        if (s[i] == L'r') {
            size_t j = i + 1, hashes = 0;
            while (j < s.size() && s[j] == L'#') { ++hashes; ++j; }
            if (j >= s.size() || s[j] != L'"') return L;
            std::wstring close = L"\"" + std::wstring(hashes, L'#');
            size_t k = s.find(close, j + 1);
            if (k == std::wstring::npos) return L;
            L.value = s.substr(j + 1, k - j - 1); L.end = k + close.size(); L.kind = L"raw string"; L.ok = true;
            return L;
        }
        std::wstring body;
        if (s[i] != L'"' || !read_quoted(s, i, L'"', body, L.end)) return L;
        L.value = unescape(body, Unknown::Keep, L"Rust", L.notes); L.kind = L"string"; L.ok = true;
        return L;
    }
    case Style::PHP: {
        wchar_t q = s[i];
        if (q != L'\'' && q != L'"') return L;
        std::wstring body;
        if (!read_quoted(s, i, q, body, L.end)) return L;
        if (q == L'\'') {
            std::wstring o;
            for (size_t k = 0; k < body.size(); ++k) {
                if (body[k] == L'\\' && k + 1 < body.size() && (body[k + 1] == L'\\' || body[k + 1] == L'\'')) { o += body[++k]; continue; }
                o += body[k];
            }
            L.value = o;
        } else {
            L.value = unescape(body, Unknown::Keep, L"PHP", L.notes);
        }
        L.kind = L"string"; L.ok = true;
        return L;
    }
    case Style::PowerShell: {
        wchar_t q = s[i];
        if (q == L'\'') {
            size_t j = i + 1;
            std::wstring o;
            while (j < s.size()) {
                if (s[j] == L'\'') { if (j + 1 < s.size() && s[j + 1] == L'\'') { o += L'\''; j += 2; continue; } break; }
                o += s[j++];
            }
            if (j >= s.size()) return L;
            L.value = o; L.end = j + 1; L.kind = L"string"; L.ok = true;
            return L;
        }
        if (q != L'"') return L;
        size_t j = i + 1;
        std::wstring o;
        while (j < s.size() && s[j] != L'"') {
            if (s[j] == L'`' && j + 1 < s.size()) { o += s[j + 1]; j += 2; continue; }
            o += s[j++];
        }
        if (j >= s.size()) return L;
        L.value = o; L.end = j + 1; L.kind = L"string"; L.ok = true;
        return L;
    }
    case Style::Shell:
        break;
    }
    return L;
}

// One shell word: 'raw', "with \" escapes", or bare up to whitespace/|;&.
bool read_shell_word(const std::wstring& s, size_t& i, std::wstring& out) {
    i = skip_ws(s, i);
    if (i >= s.size() || wcschr(L"|;&\n", s[i])) return false;
    out.clear();
    while (i < s.size() && !is_space(s[i]) && !wcschr(L"|;&", s[i])) {
        wchar_t c = s[i];
        if (c == L'\'') {
            size_t k = s.find(L'\'', i + 1);
            if (k == std::wstring::npos) return false;
            out += s.substr(i + 1, k - i - 1);
            i = k + 1;
        } else if (c == L'"') {
            ++i;
            while (i < s.size() && s[i] != L'"') {
                if (s[i] == L'\\' && i + 1 < s.size() && wcschr(L"\\\"$`", s[i + 1])) { out += s[i + 1]; i += 2; continue; }
                out += s[i++];
            }
            if (i >= s.size()) return false;
            ++i;
        } else if (c == L'\\' && i + 1 < s.size()) {
            out += s[i + 1];
            i += 2;
        } else {
            out += c;
            ++i;
        }
    }
    return true;
}

// ─── call arguments ───────────────────────────────────────────────────────────

struct Arg { size_t b, e; };

// open = index of '('. Fills args (top-level, comma separated); returns index of ')' or npos.
size_t call_args(const std::wstring& s, size_t open, std::vector<Arg>& args) {
    int depth = 0;
    size_t start = open + 1;
    for (size_t i = open; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (c == L'"' || c == L'\'' || c == L'`') {
            if (s.compare(i, 3, std::wstring(3, c)) == 0) {
                size_t k = s.find(std::wstring(3, c), i + 3);
                if (k == std::wstring::npos) return std::wstring::npos;
                i = k + 2;
                continue;
            }
            bool verbatim = c == L'"' && i > 0 && s[i - 1] == L'@';
            size_t j = i + 1;
            while (j < s.size() && s[j] != c) {
                if (verbatim && s[j] == L'"' && j + 1 < s.size() && s[j + 1] == L'"') { j += 2; continue; }
                if (!verbatim && s[j] == L'\\') ++j;
                ++j;
            }
            i = j;
            continue;
        }
        if (c == L'(' || c == L'[' || c == L'{') { ++depth; continue; }
        if (c == L')' || c == L']' || c == L'}') {
            if (--depth == 0) { args.push_back({ start, i }); return i; }
            continue;
        }
        if (c == L',' && depth == 1) { args.push_back({ start, i }); start = i + 1; }
    }
    return std::wstring::npos;
}

// The argument is exactly one literal (plus whitespace).
bool arg_literal(const std::wstring& s, const Arg& a, Style st, Lit& out) {
    Lit L = read_literal(s.substr(0, a.e), a.b, st);
    if (!L.ok || skip_ws(s, L.end) < a.e) return false;
    out = L;
    return true;
}

// ─── collecting candidates ────────────────────────────────────────────────────

struct Collector {
    const std::wstring& s;     // the segment
    size_t base;               // its offset in the paste
    std::wstring tag;          // markdown fence language, lower-case
    std::vector<Candidate> out;
    std::vector<std::pair<size_t, size_t>> taken;

    bool overlaps(size_t b, size_t e) const {
        for (auto& t : taken) if (b < t.second && t.first < e) return true;
        return false;
    }

    void add(Candidate c, size_t b, size_t e) {
        if (overlaps(b, e)) return;
        taken.push_back({ b, e });
        c.begin = base + b;
        c.end = base + e;
        c.recognised = true;
        out.push_back(std::move(c));
    }

    bool tag_is(std::initializer_list<const wchar_t*> names) const {
        for (const wchar_t* n : names) if (tag == n) return true;
        return false;
    }

    template <class F>
    void each(const wchar_t* re, F f) {
        boost::wregex r(re, boost::regex::perl | boost::regex::icase);
        for (boost::wsregex_iterator it(s.begin(), s.end(), r), end; it != end; ++it) f(*it);
    }

    static std::wstring desc(const wchar_t* lang, const std::wstring& kind, const Flags& f, bool repl, const std::wstring& extra = L"") {
        std::wstring d = std::wstring(lang) + (kind.empty() ? L"" : L" \x00B7 " + kind);
        if (!extra.empty()) d += L" \x00B7 " + extra;
        std::wstring ft = flags_text(f);
        if (!ft.empty()) d += L" \x00B7 flags: " + ft;
        if (repl) d += L" \x00B7 with replacement";
        return d;
    }

    // ── Python ──
    void python() {
        each(L"\\b(?:re|regex)\\s*\\.\\s*(compile|search|match|fullmatch|findall|finditer|subn?|split)\\s*\\(|\\.str\\s*\\.\\s*(contains|extract|extractall|match|fullmatch|replace|findall|split|count)\\s*\\(",
             [&](const boost::wsmatch& m) {
            size_t open = (size_t)(m[0].second - s.begin()) - 1;
            std::vector<Arg> args;
            size_t close = call_args(s, open, args);
            if (close == std::wstring::npos || args.empty()) return;
            Lit pat;
            if (!arg_literal(s, args[0], Style::Python, pat)) return;
            std::wstring fn = m[1].matched ? m[1].str() : m[2].str();
            bool pandas = !m[1].matched;
            Candidate c;
            c.pattern = pat.value;
            c.notes = pat.notes;
            c.flavor = Flavor::Python;
            Lit rep;
            if ((fn == L"sub" || fn == L"subn" || (pandas && fn == L"replace")) && args.size() > 1 &&
                arg_literal(s, args[1], Style::Python, rep)) {
                c.has_replacement = true;
                c.replacement = rep.value;
            }
            std::wstring call = s.substr(open, close - open);
            boost::wregex fl(L"\\bre\\.(I|IGNORECASE|M|MULTILINE|S|DOTALL|X|VERBOSE)\\b");
            for (boost::wsregex_iterator it(call.begin(), call.end(), fl), e; it != e; ++it) {
                wchar_t k = (*it)[1].str()[0];
                if (k == L'I') c.flags.i = true; else if (k == L'M') c.flags.m = true;
                else if (k == L'S' || k == L'D') c.flags.s = true; else if (k == L'X' || k == L'V') c.flags.x = true;
            }
            if (pandas && call.find(L"case=False") != std::wstring::npos) c.flags.i = true;
            c.source = desc(pandas ? L"Python (pandas)" : L"Python", pat.kind, c.flags, c.has_replacement);
            add(std::move(c), (size_t)(m[0].first - s.begin()), close + 1);
        });
    }

    // ── C# and PowerShell [regex]:: ──
    void dotnet() {
        each(L"\\bnew\\s+Regex\\s*\\(|(\\[regex\\]\\s*::|\\bRegex\\s*\\.)\\s*(Match|Matches|IsMatch|Replace|Split|Count|EnumerateMatches)\\s*\\(",
             [&](const boost::wsmatch& m) {
            bool ps = m[1].matched && m[1].str()[0] == L'[';
            bool instance = !m[1].matched;
            size_t open = (size_t)(m[0].second - s.begin()) - 1;
            std::vector<Arg> args;
            size_t close = call_args(s, open, args);
            if (close == std::wstring::npos) return;
            size_t pi = instance ? 0 : 1;
            if (args.size() <= pi) return;
            Style st = ps ? Style::PowerShell : Style::CSharp;
            Lit pat;
            if (!arg_literal(s, args[pi], st, pat)) return;
            Candidate c;
            c.pattern = pat.value;
            c.notes = pat.notes;
            c.flavor = Flavor::DotNet;
            Lit rep;
            if (!instance && m[2].str() == L"Replace" && args.size() > 2 && arg_literal(s, args[2], st, rep)) {
                c.has_replacement = true;
                c.replacement = rep.value;
            }
            std::wstring call = s.substr(open, close - open);
            boost::wregex fl(L"RegexOptions\\s*\\.\\s*(IgnoreCase|Multiline|Singleline|IgnorePatternWhitespace)\\b|'(IgnoreCase|Multiline|Singleline)'");
            for (boost::wsregex_iterator it(call.begin(), call.end(), fl), e; it != e; ++it) {
                std::wstring k = (*it)[1].matched ? (*it)[1].str() : (*it)[2].str();
                if (k == L"IgnoreCase") c.flags.i = true; else if (k == L"Multiline") c.flags.m = true;
                else if (k == L"Singleline") c.flags.s = true; else c.flags.x = true;
            }
            c.source = desc(ps ? L"PowerShell" : L"C#", pat.kind, c.flags, c.has_replacement);
            add(std::move(c), (size_t)(m[0].first - s.begin()), close + 1);
        });
    }

    // ── PowerShell operators: -match, -replace, -split, Select-String ──
    void powershell() {
        each(L"(?<![\\w-])-(i|c)?(match|notmatch|replace|split)\\s+|\\bSelect-String\\b[^|\\n]*?-Pattern\\s+",
             [&](const boost::wsmatch& m) {
            size_t i = (size_t)(m[0].second - s.begin());
            Lit pat = read_literal(s, i, Style::PowerShell);
            if (!pat.ok) return;
            Candidate c;
            c.pattern = pat.value;
            c.flavor = Flavor::DotNet;
            std::wstring op = m[2].matched ? m[2].str() : L"Select-String";
            c.flags.i = !(m[1].matched && (m[1].str() == L"c" || m[1].str() == L"C"));   // -match is case-insensitive
            size_t end = pat.end;
            if (op == L"replace") {
                size_t j = skip_ws(s, pat.end);
                if (j < s.size() && s[j] == L',') {
                    Lit rep = read_literal(s, j + 1, Style::PowerShell);
                    if (rep.ok) { c.has_replacement = true; c.replacement = rep.value; end = rep.end; }
                }
            }
            c.source = desc(L"PowerShell", L"-" + op, c.flags, c.has_replacement);
            add(std::move(c), (size_t)(m[0].first - s.begin()), end);
        });
    }

    // ── Java / Kotlin ──
    void java() {
        bool kotlin = tag_is({ L"kotlin", L"kt", L"kts" }) || s.find(L"fun ") != std::wstring::npos || s.find(L"val ") != std::wstring::npos;
        bool javaish = tag_is({ L"java", L"kotlin", L"kt", L"scala" }) || s.find(L"Pattern") != std::wstring::npos ||
                       s.find(L"String ") != std::wstring::npos || s.find(L"System.out") != std::wstring::npos || kotlin;
        Style st = kotlin ? Style::Kotlin : Style::Java;
        each(L"\\bPattern\\s*\\.\\s*(compile|matches)\\s*\\(|\\bRegex\\s*\\((?!\\s*\\))|\\.\\s*(matches|replaceAll|replaceFirst|split)\\s*\\(",
             [&](const boost::wsmatch& m) {
            bool string_method = m[2].matched;
            bool kotlin_regex = !m[1].matched && !m[2].matched;
            if (string_method && !javaish) return;
            if (kotlin_regex && !kotlin && !tag_is({ L"kotlin", L"kt" })) {
                // "Regex(" without Kotlin context: still Kotlin-like if followed by a string
            }
            size_t open = (size_t)(m[0].second - s.begin()) - 1;
            std::vector<Arg> args;
            size_t close = call_args(s, open, args);
            if (close == std::wstring::npos || args.empty()) return;
            Lit pat;
            if (!arg_literal(s, args[0], kotlin_regex ? Style::Kotlin : st, pat)) return;
            Candidate c;
            c.pattern = pat.value;
            c.notes = pat.notes;
            c.flavor = Flavor::Java;
            Lit rep;
            if (string_method && (m[2].str() == L"replaceAll" || m[2].str() == L"replaceFirst") && args.size() > 1 &&
                arg_literal(s, args[1], st, rep)) {
                c.has_replacement = true;
                c.replacement = rep.value;
            }
            std::wstring call = s.substr(open, close - open);
            boost::wregex fl(L"\\b(?:Pattern|RegexOption)\\s*\\.\\s*(CASE_INSENSITIVE|IGNORE_CASE|MULTILINE|DOTALL|DOT_MATCHES_ALL|COMMENTS)\\b");
            for (boost::wsregex_iterator it(call.begin(), call.end(), fl), e; it != e; ++it) {
                std::wstring k = (*it)[1].str();
                if (k == L"CASE_INSENSITIVE" || k == L"IGNORE_CASE") c.flags.i = true;
                else if (k == L"MULTILINE") c.flags.m = true;
                else if (k == L"COMMENTS") c.flags.x = true;
                else c.flags.s = true;
            }
            c.source = desc(kotlin_regex || kotlin ? L"Kotlin" : L"Java", pat.kind, c.flags, c.has_replacement);
            add(std::move(c), (size_t)(m[0].first - s.begin()), close + 1);
        });
        // Kotlin "…".toRegex()
        each(L"(\"\"\"[\\s\\S]*?\"\"\"|\"(?:[^\"\\\\\\n]|\\\\.)*\")\\s*\\.\\s*toRegex\\s*\\(", [&](const boost::wsmatch& m) {
            size_t b = (size_t)(m[0].first - s.begin());
            Lit pat = read_literal(s, b, Style::Kotlin);
            if (!pat.ok) return;
            Candidate c;
            c.pattern = pat.value;
            c.notes = pat.notes;
            c.flavor = Flavor::Java;
            c.source = desc(L"Kotlin", pat.kind, c.flags, false);
            add(std::move(c), b, (size_t)(m[0].second - s.begin()));
        });
    }

    // ── PHP preg_* ──
    void php() {
        each(L"\\bpreg_(match_all|match|replace_callback|replace|split|grep)\\s*\\(", [&](const boost::wsmatch& m) {
            size_t open = (size_t)(m[0].second - s.begin()) - 1;
            std::vector<Arg> args;
            size_t close = call_args(s, open, args);
            if (close == std::wstring::npos || args.empty()) return;
            Lit lit;
            if (!arg_literal(s, args[0], Style::PHP, lit)) return;
            const std::wstring& v = lit.value;
            if (v.size() < 2) return;
            wchar_t d = v[0];
            if (iswalnum(d) || d == L'\\' || is_space(d)) return;
            wchar_t cl = d == L'(' ? L')' : d == L'{' ? L'}' : d == L'[' ? L']' : d == L'<' ? L'>' : d;
            size_t last = v.rfind(cl);
            if (last == 0 || last == std::wstring::npos) return;
            Candidate c;
            c.pattern = v.substr(1, last - 1);
            if (cl == d && d != L'/') {        // \# inside #…# means a literal #
                std::wstring esc = std::wstring(L"\\") + d, o;
                for (size_t k = 0; k < c.pattern.size(); ++k) {
                    if (c.pattern.compare(k, 2, esc) == 0) { o += d; ++k; } else o += c.pattern[k];
                }
                c.pattern = o;
            } else if (d == L'/') {
                std::wstring o;
                for (size_t k = 0; k < c.pattern.size(); ++k) {
                    if (c.pattern.compare(k, 2, L"\\/") == 0) { o += L'/'; ++k; } else o += c.pattern[k];
                }
                c.pattern = o;
            }
            std::wstring mods = v.substr(last + 1);
            c.flags.i = mods.find(L'i') != std::wstring::npos;
            c.flags.m = mods.find(L'm') != std::wstring::npos;
            c.flags.s = mods.find(L's') != std::wstring::npos;
            c.flags.x = mods.find(L'x') != std::wstring::npos;
            c.notes = lit.notes;
            if (mods.find(L'U') != std::wstring::npos) c.notes.push_back(L"The U modifier (ungreedy) swaps greedy and lazy quantifiers; that is not emulated here.");
            c.flavor = Flavor::PCRE;
            Lit rep;
            if (m[1].str() == L"replace" && args.size() > 1 && arg_literal(s, args[1], Style::PHP, rep)) {
                c.has_replacement = true;
                c.replacement = rep.value;
            }
            c.source = desc(L"PHP", L"preg_" + m[1].str(), c.flags, c.has_replacement);
            add(std::move(c), (size_t)(m[0].first - s.begin()), close + 1);
        });
    }

    // ── Go and Rust (RE2-style engines) ──
    void go_rust() {
        each(L"\\bregexp\\s*\\.\\s*(MustCompilePOSIX|CompilePOSIX|MustCompile|Compile|MatchString|Match)\\s*\\(|\\b(?:Regex|RegexBuilder)\\s*::\\s*new\\s*\\(",
             [&](const boost::wsmatch& m) {
            bool go = m[1].matched;
            size_t open = (size_t)(m[0].second - s.begin()) - 1;
            std::vector<Arg> args;
            size_t close = call_args(s, open, args);
            if (close == std::wstring::npos || args.empty()) return;
            Lit pat;
            if (!arg_literal(s, args[0], go ? Style::Go : Style::Rust, pat)) return;
            Candidate c;
            c.pattern = pat.value;
            c.notes = pat.notes;
            bool posix = go && m[1].str().find(L"POSIX") != std::wstring::npos;
            c.flavor = posix ? Flavor::PosixERE : Flavor::RE2;
            c.source = desc(go ? L"Go" : L"Rust", pat.kind, c.flags, false, posix ? L"POSIX (leftmost-longest)" : L"");
            add(std::move(c), (size_t)(m[0].first - s.begin()), close + 1);
        });
    }

    // ── sed / grep / rg / perl one-liners ──
    void shell() {
        each(L"(?:^|[\\s|;&(`$])(sed|grep|egrep|fgrep|zgrep|rg|perl)(?=\\s)", [&](const boost::wsmatch& m) {
            std::wstring tool = m[1].str();
            size_t i = (size_t)(m[1].second - s.begin());
            size_t start = (size_t)(m[1].first - s.begin());
            std::wstring w, script;
            bool ere = tool == L"egrep", pcre = false, fixed = tool == L"fgrep", icase = false, have = false;
            while (read_shell_word(s, i, w)) {
                if (w.size() > 1 && w[0] == L'-' && !have) {
                    if (w == L"-e" || w == L"--regexp") { if (read_shell_word(s, i, script)) have = true; continue; }
                    if (tool == L"perl" && w.find(L'e') != std::wstring::npos) { if (read_shell_word(s, i, script)) have = true; continue; }
                    if (w.compare(0, 2, L"--") == 0) {
                        if (w == L"--extended-regexp" || w == L"--regexp-extended") ere = true;
                        else if (w == L"--perl-regexp") pcre = true;
                        else if (w == L"--ignore-case") icase = true;
                        else if (w == L"--fixed-strings") fixed = true;
                        continue;
                    }
                    for (wchar_t f : w.substr(1)) {
                        if (f == L'E' || (f == L'r' && tool == L"sed")) ere = true;
                        else if (f == L'P') pcre = true;
                        else if (f == L'F') fixed = true;
                        else if (f == L'i' && tool != L"sed") icase = true;
                    }
                    if (wcschr(L"ABCm", w.back()) && w.size() == 2 && tool != L"sed") { std::wstring skip; read_shell_word(s, i, skip); }
                    continue;
                }
                if (!have) { script = w; have = true; }
                break;
            }
            if (!have || script.empty()) return;

            Candidate c;
            c.flags.i = icase;
            if (tool == L"sed" || tool == L"perl") {
                if (!parse_subst(script, c, tool == L"perl")) return;
                c.flavor = tool == L"perl" ? Flavor::PCRE : ere ? Flavor::PosixERE : Flavor::PosixBRE;
                c.source = desc(tool == L"perl" ? L"perl" : ere ? L"sed -E" : L"sed", c.flavor == Flavor::PosixBRE ? L"POSIX BRE" : c.flavor == Flavor::PosixERE ? L"POSIX ERE" : L"", c.flags, c.has_replacement);
            } else {
                c.pattern = script;
                if (fixed) {
                    std::wstring o;
                    for (wchar_t ch : script) { if (wcschr(L"\\^$.|?*+()[]{}", ch)) o += L'\\'; o += ch; }
                    c.pattern = o;
                }
                c.flavor = tool == L"rg" ? Flavor::RE2 : pcre ? Flavor::PCRE : (ere || fixed) ? Flavor::PosixERE : Flavor::PosixBRE;
                std::wstring kind = tool == L"rg" ? L"Rust regex" : pcre ? L"-P (PCRE)" : fixed ? L"-F (fixed string)" : ere ? L"-E (POSIX ERE)" : L"POSIX BRE";
                c.source = desc(tool.c_str(), kind, c.flags, false);
            }
            add(std::move(c), start, i);
        });
    }

    // s<d>pattern<d>replacement<d>flags (also m<d>…<d> and /…/ for perl)
    static bool parse_subst(const std::wstring& sc, Candidate& c, bool perl) {
        auto is_delim = [](wchar_t ch) { return !iswalnum(ch) && !is_space(ch) && ch != L'\\'; };
        size_t d_at = std::wstring::npos;
        bool subst = false;
        for (size_t q = 0; q < sc.size(); ++q) {
            bool boundary = q == 0 || wcschr(L" ;{}(~$0123456789/", sc[q - 1]);
            if (!boundary) continue;
            wchar_t ch = sc[q];
            if ((ch == L's' || (perl && ch == L'm')) && q + 1 < sc.size() && is_delim(sc[q + 1])) {
                d_at = q + 1;
                subst = ch == L's';
                break;
            }
            if (perl && ch == L'/') { d_at = q; break; }       // perl: /…/ match
        }
        if (d_at == std::wstring::npos) return false;
        wchar_t d = sc[d_at];
        auto part = [&](size_t& j, std::wstring& out) {
            out.clear();
            while (j < sc.size() && sc[j] != d) {
                if (sc[j] == L'\\' && j + 1 < sc.size()) {
                    if (sc[j + 1] == d) { out += d; j += 2; continue; }
                    out += sc[j]; out += sc[j + 1]; j += 2; continue;
                }
                out += sc[j++];
            }
            if (j >= sc.size()) return false;
            ++j;
            return true;
        };
        size_t j = d_at + 1;
        std::wstring pat, rep;
        if (!part(j, pat)) return false;
        if (subst && !part(j, rep)) return false;
        std::wstring fl;
        while (j < sc.size() && iswalpha(sc[j])) fl += sc[j++];
        c.pattern = pat;
        if (subst) { c.has_replacement = true; c.replacement = rep; }
        if (fl.find(L'I') != std::wstring::npos || fl.find(L'i') != std::wstring::npos) c.flags.i = true;
        if (perl) {
            if (fl.find(L'm') != std::wstring::npos) c.flags.m = true;
            if (fl.find(L's') != std::wstring::npos) c.flags.s = true;
            if (fl.find(L'x') != std::wstring::npos) c.flags.x = true;
        }
        return true;
    }

    // ── JavaScript: new RegExp(…) and /…/flags literals ──
    void javascript() {
        each(L"\\bnew\\s+RegExp\\s*\\(|(?<![\\w.])RegExp\\s*\\(", [&](const boost::wsmatch& m) {
            size_t open = (size_t)(m[0].second - s.begin()) - 1;
            std::vector<Arg> args;
            size_t close = call_args(s, open, args);
            if (close == std::wstring::npos || args.empty()) return;
            Lit pat;
            if (!arg_literal(s, args[0], Style::JS, pat)) return;
            Candidate c;
            c.pattern = pat.value;
            c.notes = pat.notes;
            c.flavor = Flavor::JavaScript;
            Lit fl;
            if (args.size() > 1 && arg_literal(s, args[1], Style::JS, fl)) apply_js_flags(fl.value, c);
            c.source = desc(L"JavaScript", L"new RegExp (" + pat.kind + L")", c.flags, false);
            add(std::move(c), (size_t)(m[0].first - s.begin()), close + 1);
        });

        bool jsish = tag_is({ L"js", L"javascript", L"ts", L"typescript", L"jsx", L"tsx", L"node", L"mjs" });
        bool other = tag_is({ L"python", L"py", L"bash", L"sh", L"shell", L"zsh", L"php", L"go", L"rust", L"java", L"kotlin", L"csharp", L"cs", L"c#", L"powershell", L"ps1", L"pwsh" });
        if (other && !jsish) return;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] != L'/') continue;
            if (i + 1 < s.size() && (s[i + 1] == L'/' || s[i + 1] == L'*')) { i = skip_comment(i); continue; }
            size_t p = i;
            while (p > 0 && (s[p - 1] == L' ' || s[p - 1] == L'\t')) --p;
            bool ok_before = p == 0 || wcschr(L"(,=:[!&|?{};\n", s[p - 1]);
            if (!ok_before && p >= 6 && s.compare(p - 6, 6, L"return") == 0) ok_before = true;
            if (!ok_before) continue;
            size_t j = i + 1;
            bool in_class = false;
            while (j < s.size() && s[j] != L'\n') {
                if (s[j] == L'\\' && j + 1 < s.size()) { j += 2; continue; }
                if (s[j] == L'[') in_class = true;
                else if (s[j] == L']') in_class = false;
                else if (s[j] == L'/' && !in_class) break;
                ++j;
            }
            if (j >= s.size() || s[j] != L'/' || j == i + 1) continue;
            size_t k = j + 1;
            std::wstring flags;
            while (k < s.size() && wcschr(L"dgimsuyv", s[k])) flags += s[k++];
            if (k < s.size() && (iswalnum(s[k]) || s[k] == L'_')) continue;
            Candidate c;
            std::wstring body = s.substr(i + 1, j - i - 1), o;
            for (size_t q = 0; q < body.size(); ++q) {
                if (body.compare(q, 2, L"\\/") == 0) { o += L'/'; ++q; } else o += body[q];
            }
            c.pattern = o;
            c.flavor = Flavor::JavaScript;
            apply_js_flags(flags, c);
            size_t end = k;
            // .replace(/…/g, "…")
            size_t q = p;
            while (q > 0 && is_space(s[q - 1])) --q;
            boost::wregex before(L"\\.\\s*(replace|replaceAll)\\s*\\($");
            std::wstring head = s.substr(0, q);
            if (boost::regex_search(head, before)) {
                size_t a = skip_ws(s, k);
                if (a < s.size() && s[a] == L',') {
                    Lit rep = read_literal(s, a + 1, Style::JS);
                    if (rep.ok) { c.has_replacement = true; c.replacement = rep.value; end = rep.end; for (auto& n : rep.notes) c.notes.push_back(n); }
                }
            }
            c.source = desc(L"JavaScript", L"/\x2026/" + flags + L" literal", c.flags, c.has_replacement);
            add(std::move(c), i, end);
            i = k - 1;
        }
    }

    size_t skip_comment(size_t i) const {
        if (s[i + 1] == L'/') { size_t e = s.find(L'\n', i); return e == std::wstring::npos ? s.size() : e; }
        size_t e = s.find(L"*/", i + 2);
        return e == std::wstring::npos ? s.size() : e + 1;
    }

    static void apply_js_flags(const std::wstring& f, Candidate& c) {
        c.flags.i = f.find(L'i') != std::wstring::npos;
        c.flags.m = f.find(L'm') != std::wstring::npos;
        c.flags.s = f.find(L's') != std::wstring::npos;
        if (f.find(L'y') != std::wstring::npos) c.notes.push_back(L"The y (sticky) flag is not emulated: matches are not required to start where the previous one ended.");
    }

    void run() {
        php();          // before JS: '/…/' inside a PHP string must not be read as a JS literal
        shell();        // before JS: sed 's/…/…/'
        python();
        dotnet();
        powershell();
        java();
        go_rust();
        javascript();
    }
};

std::wstring lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

std::wstring trim(const std::wstring& s, size_t& b) {
    size_t e = s.size();
    b = 0;
    while (b < e && is_space(s[b])) ++b;
    while (e > b && is_space(s[e - 1])) --e;
    return s.substr(b, e - b);
}

bool looks_like_regex(const std::wstring& t) {
    return t.find_first_of(L"\\^$[]()*+?|{}") != std::wstring::npos;
}

Candidate bare(const std::wstring& text, size_t base) {
    size_t lead = 0;
    std::wstring t = trim(text, lead);
    Candidate c;
    c.begin = base + lead;
    c.end = c.begin + t.size();
    // A lone /…/flags is a JavaScript literal.
    if (t.size() > 2 && t[0] == L'/') {
        size_t last = t.rfind(L'/');
        std::wstring fl = t.substr(last + 1);
        if (last > 0 && fl.find_first_not_of(L"dgimsuyv") == std::wstring::npos) {
            c.pattern = t.substr(1, last - 1);
            c.flavor = Flavor::JavaScript;
            c.recognised = true;
            Collector::apply_js_flags(fl, c);
            c.source = L"JavaScript \x00B7 /\x2026/" + fl + L" literal";
            return c;
        }
    }
    c.pattern = t;
    c.source = L"plain pattern";
    return c;
}

} // namespace

std::vector<Candidate> extract(const std::wstring& paste) {
    std::vector<Candidate> all;

    // Markdown fenced blocks become separate segments.
    struct Seg { size_t b, e; std::wstring tag; };
    std::vector<Seg> segs;
    std::vector<std::pair<size_t, size_t>> fenced;   // whole fences incl. markers
    size_t i = 0;
    while (i < paste.size()) {
        size_t ls = i;
        size_t le = paste.find(L'\n', i);
        if (le == std::wstring::npos) le = paste.size();
        size_t t = ls;
        while (t < le && (paste[t] == L' ' || paste[t] == L'\t')) ++t;
        if (paste.compare(t, 3, L"```") == 0 || paste.compare(t, 3, L"~~~") == 0) {
            std::wstring marker = paste.substr(t, 3);
            std::wstring tag;
            size_t q = t + 3;
            while (q < le && !is_space(paste[q]) && paste[q] != L'`') tag += paste[q++];
            size_t body = le < paste.size() ? le + 1 : le;
            size_t close = body, j = body;
            bool found = false;
            while (j < paste.size()) {
                size_t e2 = paste.find(L'\n', j);
                if (e2 == std::wstring::npos) e2 = paste.size();
                size_t k = j;
                while (k < e2 && (paste[k] == L' ' || paste[k] == L'\t')) ++k;
                if (paste.compare(k, 3, marker) == 0) { close = j; found = true; i = e2 < paste.size() ? e2 + 1 : e2; break; }
                j = e2 < paste.size() ? e2 + 1 : e2;
            }
            if (!found) { close = paste.size(); i = paste.size(); }
            segs.push_back({ body, close, lower(tag) });
            fenced.push_back({ ls, i });
            continue;
        }
        i = le < paste.size() ? le + 1 : le;
    }
    if (segs.empty()) segs.push_back({ 0, paste.size(), L"" });

    for (const Seg& sg : segs) {
        std::wstring text = paste.substr(sg.b, sg.e - sg.b);
        Collector col{ text, sg.b, sg.tag, {}, {} };
        col.run();
        if (col.out.empty() && (sg.tag == L"regex" || sg.tag == L"re" || sg.tag == L"regexp" || sg.tag == L"text" || sg.tag.empty() || segs.size() > 1)) {
            size_t lead = 0;
            std::wstring t = trim(text, lead);
            if (!t.empty() && t.find(L'\n') == std::wstring::npos && segs.size() > 1) {
                Candidate c = bare(text, sg.b);
                c.source = sg.tag.empty() ? c.source : c.source + L" \x00B7 ```" + sg.tag + L" block";
                c.recognised = c.recognised || !sg.tag.empty();
                col.out.push_back(c);
            }
        }
        std::sort(col.out.begin(), col.out.end(), [](const Candidate& a, const Candidate& b) { return a.begin < b.begin; });
        for (auto& c : col.out) all.push_back(std::move(c));
    }

    // Inline `code` in prose: "Use `^\d{4}$` for that."
    if (all.empty()) {
        boost::wregex inl(L"`([^`\\n]+)`");
        for (boost::wsregex_iterator it(paste.begin(), paste.end(), inl), e; it != e; ++it) {
            size_t b = (size_t)((*it)[0].first - paste.begin());
            bool in_fence = false;
            for (auto& f : fenced) if (b >= f.first && b < f.second) in_fence = true;
            if (in_fence) continue;
            std::wstring body = (*it)[1].str();
            if (!looks_like_regex(body)) continue;
            Candidate c = bare(body, (size_t)((*it)[1].first - paste.begin()));
            c.source = c.flavor == Flavor::JavaScript ? c.source : L"inline code";
            all.push_back(c);
        }
    }

    if (all.empty()) {
        size_t lead = 0;
        std::wstring t = trim(paste, lead);
        if (!t.empty()) {
            if (segs.size() == 1 && segs[0].tag.empty()) all.push_back(bare(paste, 0));
            else all.push_back(bare(paste.substr(segs[0].b, segs[0].e - segs[0].b), segs[0].b));
        }
    }
    return all;
}

} // namespace rv
