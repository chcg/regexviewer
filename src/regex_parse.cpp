#include "regex_parse.h"
#include <cwctype>

namespace rv {

namespace {

struct Parser {
    const std::wstring& p;
    bool x;
    size_t i = 0;
    ParseResult r;

    Parser(const std::wstring& pat, bool extended) : p(pat), x(extended) {}

    bool at_end() const { return i >= p.size(); }
    wchar_t peek(size_t k = 0) const { return i + k < p.size() ? p[i + k] : 0; }

    void skip_extended() {
        if (!x) return;
        while (!at_end()) {
            if (iswspace(p[i])) { ++i; continue; }
            if (p[i] == L'#') { while (!at_end() && p[i] != L'\n') ++i; continue; }
            break;
        }
    }

    Node parse_alternation(int depth) {
        Node alt;
        alt.kind = NodeKind::Alternation;
        alt.pos = i;
        alt.kids.push_back(parse_sequence(depth));
        while (!at_end() && p[i] == L'|') {
            ++i;
            alt.kids.push_back(parse_sequence(depth));
        }
        alt.len = i - alt.pos;
        if (alt.kids.size() == 1) return alt.kids[0];
        return alt;
    }

    Node parse_sequence(int depth) {
        Node seq;
        seq.kind = NodeKind::Sequence;
        seq.pos = i;
        for (;;) {
            skip_extended();
            if (at_end() || p[i] == L'|' || (p[i] == L')' && depth > 0)) break;
            if (p[i] == L')') {                 // unbalanced ')' at top level
                Node u; u.kind = NodeKind::Unknown; u.pos = i; u.len = 1; u.text = L")";
                r.has_unknown = true;
                ++i;
                seq.kids.push_back(u);
                continue;
            }
            Node atom = parse_atom(depth);
            parse_quantifier(atom);
            // A quantifier after a literal run applies to its last character only.
            if (atom.kind == NodeKind::Literal && atom.q != QMode::None && atom.text.size() > 1) {
                Node head = atom;
                head.text = atom.text.substr(0, atom.text.size() - 1);
                head.q = QMode::None; head.qmin = head.qmax = 1;
                head.len = head.text.size();
                Node tail = atom;
                tail.text = atom.text.substr(atom.text.size() - 1);
                tail.pos = atom.pos + head.len;
                tail.len = atom.len - head.len;
                seq.kids.push_back(head);
                seq.kids.push_back(tail);
                continue;
            }
            // Merge adjacent unquantified literals.
            if (atom.kind == NodeKind::Literal && atom.q == QMode::None && !seq.kids.empty() &&
                seq.kids.back().kind == NodeKind::Literal && seq.kids.back().q == QMode::None &&
                seq.kids.back().pos + seq.kids.back().len == atom.pos) {
                seq.kids.back().text += atom.text;
                seq.kids.back().len += atom.len;
                continue;
            }
            seq.kids.push_back(atom);
        }
        seq.len = i - seq.pos;
        return seq;
    }

    static bool is_digit(wchar_t c) { return c >= L'0' && c <= L'9'; }

    // {n}, {n,}, {n,m} — returns false (and leaves i) if not a valid interval.
    bool parse_interval(int& mn, int& mx) {
        size_t j = i + 1;
        auto num = [&](int& out) {
            size_t s = j; out = 0;
            while (j < p.size() && is_digit(p[j])) { out = out * 10 + (p[j] - L'0'); ++j; }
            return j > s;
        };
        int a = 0, b = -1;
        bool has_a = num(a);
        if (j < p.size() && p[j] == L'}') { if (!has_a) return false; mn = mx = a; i = j + 1; return true; }
        if (j >= p.size() || p[j] != L',') return false;
        ++j;
        bool has_b = num(b);
        if (j >= p.size() || p[j] != L'}') return false;
        if (!has_a && !has_b) return false;
        mn = has_a ? a : 0;
        mx = has_b ? b : -1;
        i = j + 1;
        return true;
    }

    void parse_quantifier(Node& atom) {
        skip_extended();
        if (at_end()) return;
        int mn, mx;
        wchar_t c = p[i];
        if (c == L'*')      { mn = 0; mx = -1; ++i; }
        else if (c == L'+') { mn = 1; mx = -1; ++i; }
        else if (c == L'?') { mn = 0; mx = 1;  ++i; }
        else if (c == L'{') { if (!parse_interval(mn, mx)) return; }
        else return;
        atom.q = QMode::Greedy;
        atom.qmin = mn; atom.qmax = mx;
        if (!at_end() && p[i] == L'?')      { atom.q = QMode::Lazy; r.has_lazy = true; ++i; }
        else if (!at_end() && p[i] == L'+') { atom.q = QMode::Possessive; r.has_possessive = true; ++i; }
        atom.len = i - atom.pos;
    }

    std::wstring read_until(wchar_t close) {
        std::wstring s;
        while (!at_end() && p[i] != close) s += p[i++];
        if (!at_end()) ++i;
        return s;
    }

    Node parse_atom(int depth) {
        Node n;
        n.pos = i;
        wchar_t c = p[i];
        if (c == L'(') { n = parse_group(depth); return n; }
        if (c == L'[') { n = parse_class(); return n; }
        if (c == L'\\') { n = parse_escape(); return n; }
        ++i;
        n.len = 1;
        if (c == L'.') { n.kind = NodeKind::Any; return n; }
        if (c == L'^' || c == L'$') { n.kind = NodeKind::Anchor; n.text = std::wstring(1, c); r.has_line_anchors = true; return n; }
        n.kind = NodeKind::Literal;
        n.text = std::wstring(1, c);
        return n;
    }

    Node parse_group(int depth) {
        Node g;
        g.kind = NodeKind::Group;
        g.pos = i;
        size_t open = i;
        ++i;                                   // '('
        if (peek() == L'?') {
            ++i;
            wchar_t c = peek();
            if (c == L':') { ++i; g.gkind = GroupKind::NonCapture; }
            else if (c == L'=') { ++i; g.gkind = GroupKind::Lookahead; r.has_lookahead = true; }
            else if (c == L'!') { ++i; g.gkind = GroupKind::NegLookahead; r.has_lookahead = true; }
            else if (c == L'>') { ++i; g.gkind = GroupKind::Atomic; r.has_atomic = true; }
            else if (c == L'#') {
                ++i; g.gkind = GroupKind::Comment; g.text = read_until(L')');
                g.len = i - g.pos;
                return g;
            }
            else if (c == L'|') { ++i; g.gkind = GroupKind::Other; g.text = L"branch reset"; r.has_branch_reset = true; r.has_unknown = true; }
            else if (c == L'(') { g.gkind = GroupKind::Other; g.text = L"conditional"; r.has_conditional = true; r.has_unknown = true; }
            else if (c == L'<' && (peek(1) == L'=' || peek(1) == L'!')) {
                g.gkind = peek(1) == L'=' ? GroupKind::Lookbehind : GroupKind::NegLookbehind;
                r.has_lookbehind = true;
                i += 2;
            }
            else if (c == L'P' && peek(1) == L'<') {          // Python (?P<name>…)
                i += 2; g.gkind = GroupKind::Named; g.name = read_until(L'>'); r.has_python_named = true;
            }
            else if (c == L'P' && peek(1) == L'=') {          // Python (?P=name) backref
                i += 2;
                Node b; b.kind = NodeKind::Backref; b.pos = g.pos; b.name = read_until(L')');
                b.len = i - b.pos; r.has_backref = true; r.has_python_named = true;
                return b;
            }
            else if (c == L'<' || c == L'\'') {
                wchar_t close = c == L'<' ? L'>' : L'\'';
                ++i;
                g.gkind = GroupKind::Named;
                g.name = read_until(close);
                if (g.name.find(L'-') != std::wstring::npos) { r.has_balancing = true; r.has_unknown = true; g.gkind = GroupKind::Other; g.text = L"balancing group"; }
            }
            else {
                // inline flags: (?imsx-imsx) or (?imsx-imsx:…)
                std::wstring flags;
                while (!at_end() && (iswalpha(p[i]) || p[i] == L'-')) flags += p[i++];
                if (!at_end() && p[i] == L')' && !flags.empty()) {
                    ++i;
                    g.gkind = GroupKind::InlineFlags; g.text = flags; g.len = i - g.pos;
                    return g;
                }
                if (!at_end() && p[i] == L':' && !flags.empty()) { ++i; g.gkind = GroupKind::ScopedFlags; g.text = flags; }
                else { g.gkind = GroupKind::Other; g.text = flags; r.has_unknown = true; }
            }
        } else {
            g.gkind = GroupKind::Capture;
        }
        if (g.gkind == GroupKind::Capture || g.gkind == GroupKind::Named) g.group = ++r.group_count;

        size_t slot = r.groups.size();
        if (g.group) r.groups.push_back({ g.group, open, std::wstring::npos, g.name });
        g.kids.push_back(parse_alternation(depth + 1));
        if (!at_end() && p[i] == L')') {
            if (g.group) r.groups[slot].close = i;
            ++i;
        }
        g.len = i - g.pos;
        return g;
    }

    Node parse_class() {
        Node n;
        n.kind = NodeKind::Class;
        n.pos = i;
        ++i;                                   // '['
        if (peek() == L'^') { n.negated = true; ++i; }
        size_t body = i;
        bool first = true;
        while (!at_end()) {
            wchar_t c = p[i];
            if (c == L']' && !first) break;
            first = false;
            if (c == L'\\' && i + 1 < p.size()) { i += 2; continue; }
            if (c == L'[' && (peek(1) == L':' || peek(1) == L'.' || peek(1) == L'=')) {
                wchar_t k = peek(1);
                size_t e = p.find(std::wstring{ k, L']' }, i + 2);
                if (e != std::wstring::npos) { i = e + 2; continue; }
            }
            ++i;
        }
        n.text = p.substr(body, i - body);
        if (!at_end()) ++i;                    // ']'
        n.len = i - n.pos;
        return n;
    }

    Node parse_escape() {
        Node n;
        n.pos = i;
        ++i;                                   // '\'
        if (at_end()) { n.kind = NodeKind::Unknown; n.text = L"\\"; n.len = 1; r.has_unknown = true; return n; }
        wchar_t c = p[i++];
        n.kind = NodeKind::Escape;
        n.text = std::wstring(1, c);
        if (c >= L'1' && c <= L'9') {
            n.kind = NodeKind::Backref;
            int g = c - L'0';
            while (!at_end() && is_digit(p[i]) && g * 10 + (p[i] - L'0') <= r.group_count) g = g * 10 + (p[i++] - L'0');
            n.group = g;
            r.has_backref = true;
        } else if (c == L'k' && !at_end() && (p[i] == L'<' || p[i] == L'{' || p[i] == L'\'')) {
            wchar_t close = p[i] == L'<' ? L'>' : p[i] == L'{' ? L'}' : L'\'';
            ++i;
            n.kind = NodeKind::Backref;
            n.name = read_until(close);
            r.has_backref = true;
        } else if (c == L'g' && !at_end() && (is_digit(p[i]) || p[i] == L'{' || p[i] == L'<' || p[i] == L'-')) {
            n.kind = NodeKind::Backref;
            r.has_backref = true;
            if (p[i] == L'{' || p[i] == L'<') { wchar_t close = p[i] == L'{' ? L'}' : L'>'; ++i; n.name = read_until(close); }
            else { std::wstring d; while (!at_end() && (is_digit(p[i]) || p[i] == L'-')) d += p[i++]; n.name = d; }
            bool numeric = !n.name.empty();
            for (wchar_t ch : n.name) if (!is_digit(ch)) numeric = false;
            if (numeric) { n.group = std::stoi(n.name); n.name.clear(); }
        } else if ((c == L'p' || c == L'P') && !at_end()) {
            r.has_unicode_prop = true;
            if (p[i] == L'{') { ++i; n.text += L"{" + read_until(L'}') + L"}"; }
            else n.text += p[i++];
        } else if (c == L'x' && !at_end()) {
            if (p[i] == L'{') { ++i; n.text += L"{" + read_until(L'}') + L"}"; }
            else for (int k = 0; k < 2 && !at_end() && iswxdigit(p[i]); ++k) n.text += p[i++];
        } else if (c == L'u') {
            for (int k = 0; k < 4 && !at_end() && iswxdigit(p[i]); ++k) n.text += p[i++];
        } else if (c == L'0') {
            for (int k = 0; k < 2 && !at_end() && p[i] >= L'0' && p[i] <= L'7'; ++k) n.text += p[i++];
        } else if (c == L'c' && !at_end()) {
            n.text += p[i++];
        } else if (c == L'Q') {
            size_t e = p.find(L"\\E", i);
            n.kind = NodeKind::Literal;
            n.text = p.substr(i, e == std::wstring::npos ? std::wstring::npos : e - i);
            i = e == std::wstring::npos ? p.size() : e + 2;
        } else if (wcschr(L"dDwWsShHvVR", c)) {
            r.has_shorthand = true;
        } else if (wcschr(L"bBAzZG", c)) {
            // assertions; handled by the explanation
        } else if (wcschr(L"tnrfeaN", c)) {
            // control characters
        } else if (!iswalnum(c)) {
            n.kind = NodeKind::Literal;        // \. \* \( …
            n.text = std::wstring(1, c);
        } else {
            n.kind = NodeKind::Unknown;
            r.has_unknown = true;
        }
        n.len = i - n.pos;
        return n;
    }
};

} // namespace

ParseResult parse_regex(const std::wstring& pattern, bool extended) {
    Parser ps(pattern, extended);
    ps.r.root = ps.parse_alternation(0);
    while (!ps.at_end()) {                      // stray ')' at top level
        Node u; u.kind = NodeKind::Unknown; u.pos = ps.i; u.len = 1; u.text = std::wstring(1, pattern[ps.i]);
        ps.r.has_unknown = true;
        ++ps.i;
        if (ps.r.root.kind != NodeKind::Sequence) { Node s; s.kind = NodeKind::Sequence; s.kids.push_back(ps.r.root); ps.r.root = s; }
        ps.r.root.kids.push_back(u);
        Node rest = ps.parse_alternation(0);
        ps.r.root.kids.push_back(rest);
    }
    return ps.r;
}

} // namespace rv
