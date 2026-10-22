#include "explain.h"
#include <cwctype>

namespace rv {

namespace {

struct Noun { std::wstring one, many; bool ok = false; };

std::wstring quoted(const std::wstring& s) {
    if (s == L" ")  return L"a space";
    if (s == L"\t") return L"a tab";
    if (s == L"\"") return L"'\"'";
    return L"\"" + s + L"\"";
}

std::wstring flags_desc(const std::wstring& f) {
    std::wstring out;
    bool off = false;
    for (wchar_t c : f) {
        if (c == L'-') { off = true; continue; }
        std::wstring d;
        switch (c) {
        case L'i': d = off ? L"case-sensitive" : L"ignore case"; break;
        case L'm': d = off ? L"^ and $ only at the start/end of the text" : L"^ and $ also at line breaks"; break;
        case L's': d = off ? L". does not match line breaks" : L". also matches line breaks"; break;
        case L'x': d = off ? L"whitespace in the pattern counts" : L"whitespace in the pattern is ignored"; break;
        default:   d = std::wstring(L"flag ") + c; break;
        }
        if (!out.empty()) out += L", ";
        out += d;
    }
    return out;
}

Noun shorthand_noun(wchar_t c) {
    switch (c) {
    case L'd': return { L"a digit", L"digits", true };
    case L'D': return { L"a character that is not a digit", L"characters that are not digits", true };
    case L'w': return { L"a word character (letter, digit or _)", L"word characters (letters, digits or _)", true };
    case L'W': return { L"a character that is not a word character", L"characters that are not word characters", true };
    case L's': return { L"a whitespace character (space, tab or line break)", L"whitespace characters", true };
    case L'S': return { L"a character that is not whitespace", L"characters that are not whitespace", true };
    case L'h': return { L"a space or tab", L"spaces or tabs", true };
    case L'H': return { L"a character that is not a space or tab", L"characters that are not spaces or tabs", true };
    case L'v': return { L"a vertical whitespace character (line break)", L"vertical whitespace characters", true };
    case L'V': return { L"a character that is not vertical whitespace", L"characters that are not vertical whitespace", true };
    case L'R': return { L"a line break (\\r\\n, \\n or \\r)", L"line breaks", true };
    case L't': return { L"a tab", L"tabs", true };
    case L'n': return { L"a line break (\\n)", L"line breaks (\\n)", true };
    case L'r': return { L"a carriage return (\\r)", L"carriage returns (\\r)", true };
    case L'f': return { L"a form feed", L"form feeds", true };
    case L'e': return { L"an escape character", L"escape characters", true };
    case L'a': return { L"a bell character", L"bell characters", true };
    case L'N': return { L"a character that is not a line break", L"characters that are not line breaks", true };
    }
    return {};
}

std::wstring unicode_prop(const std::wstring& body) {   // body like "p{Lu}" or "pL"
    std::wstring name = body.substr(1);
    if (!name.empty() && name.front() == L'{') name = name.substr(1, name.size() - 2);
    std::wstring what;
    if (name == L"L" || name == L"Letter")       what = L"a letter";
    else if (name == L"Lu")                      what = L"an uppercase letter";
    else if (name == L"Ll")                      what = L"a lowercase letter";
    else if (name == L"N" || name == L"Nd")      what = L"a number";
    else if (name == L"P")                       what = L"a punctuation character";
    else if (name == L"S")                       what = L"a symbol";
    else if (name == L"Z")                       what = L"a separator";
    else                                         what = L"a character with Unicode property " + name;
    if (body[0] == L'P') what = L"a character that is not " + what.substr(what.find(L' ') + 1);
    return what;
}

// "A–Z, a–z, digits, \"_\""
std::wstring class_items(const std::wstring& b) {
    std::wstring out;
    auto add = [&](const std::wstring& s) { if (!out.empty()) out += L", "; out += s; };
    size_t i = 0;
    while (i < b.size()) {
        wchar_t c = b[i];
        if (c == L'[' && i + 1 < b.size() && b[i + 1] == L':') {
            size_t e = b.find(L":]", i + 2);
            if (e != std::wstring::npos) {
                std::wstring n = b.substr(i + 2, e - i - 2);
                std::wstring d = n == L"alpha" ? L"letters" : n == L"digit" ? L"digits" : n == L"alnum" ? L"letters and digits"
                               : n == L"space" ? L"whitespace" : n == L"upper" ? L"uppercase letters" : n == L"lower" ? L"lowercase letters"
                               : n == L"punct" ? L"punctuation" : n == L"xdigit" ? L"hex digits" : L"[:" + n + L":] characters";
                add(d);
                i = e + 2;
                continue;
            }
        }
        std::wstring item;
        bool is_escape_class = false;
        if (c == L'\\' && i + 1 < b.size()) {
            wchar_t e = b[i + 1];
            Noun nn = shorthand_noun(e);
            if (nn.ok && wcschr(L"dDwWsShHvV", e)) { add(nn.many); i += 2; continue; }
            if (e == L'p' || e == L'P') {
                size_t j = i + 2;
                std::wstring body(1, e);
                if (j < b.size() && b[j] == L'{') { size_t k = b.find(L'}', j); if (k == std::wstring::npos) k = b.size() - 1; body += b.substr(j, k - j + 1); j = k + 1; }
                else if (j < b.size()) { body += b[j]; ++j; }
                add(unicode_prop(body));
                i = j;
                continue;
            }
            item = e == L't' ? L"\t" : e == L'n' ? L"\\n" : e == L'r' ? L"\\r" : std::wstring(1, e);
            i += 2;
            is_escape_class = true;
        } else {
            item = std::wstring(1, c);
            ++i;
        }
        // range?
        if (i + 1 < b.size() && b[i] == L'-' && b[i + 1] != L']') {
            std::wstring hi;
            if (b[i + 1] == L'\\' && i + 2 < b.size()) { hi = std::wstring(1, b[i + 2]); i += 3; }
            else { hi = std::wstring(1, b[i + 1]); i += 2; }
            add(item + L"\x2013" + hi);
            continue;
        }
        (void)is_escape_class;
        add(quoted(item));
    }
    return out;
}

Noun noun_of(const Node& n, const ExplainContext& ctx) {
    switch (n.kind) {
    case NodeKind::Literal:
        if (n.text.size() == 1) {
            if (n.text == L" ") return { L"a space", L"spaces", true };
            return { L"a " + quoted(n.text), quoted(n.text) + L" characters", true };
        }
        return {};
    case NodeKind::Any:
        return ctx.dot_all ? Noun{ L"any character", L"characters of any kind", true }
                           : Noun{ L"any character except a line break", L"characters (anything except line breaks)", true };
    case NodeKind::Class: {
        std::wstring items = class_items(n.text);
        return n.negated ? Noun{ L"one character that is not: " + items, L"characters that are not: " + items, true }
                         : Noun{ L"one character from: " + items, L"characters from: " + items, true };
    }
    case NodeKind::Escape: {
        wchar_t c = n.text.empty() ? 0 : n.text[0];
        if (c == L'p' || c == L'P') {
            std::wstring one = unicode_prop(n.text);
            return { one, L"characters like that (" + one + L")", true };
        }
        if (c == L'x' || c == L'u') {
            std::wstring hex = n.text.substr(1);
            if (!hex.empty() && hex.front() == L'{') hex = hex.substr(1, hex.size() - 2);
            return { L"the character U+" + hex, L"characters U+" + hex, true };
        }
        Noun s = shorthand_noun(c);
        if (s.ok) return s;
        return {};
    }
    default:
        return {};
    }
}

std::wstring times(const Node& n) {
    int a = n.qmin, b = n.qmax;
    if (a == b)              return a == 1 ? L"once" : L"exactly " + std::to_wstring(a) + L" times";
    if (b == -1)             return a == 0 ? L"zero or more times" : a == 1 ? L"one or more times" : std::to_wstring(a) + L" or more times";
    if (a == 0 && b == 1)    return L"optional";
    return L"between " + std::to_wstring(a) + L" and " + std::to_wstring(b) + L" times";
}

std::wstring mode_suffix(const Node& n) {
    if (n.q == QMode::Lazy)       return L" (as few as possible)";
    if (n.q == QMode::Possessive) return L" (as many as possible, never giving any back)";
    return L"";
}

std::wstring quantified(const Noun& no, const Node& n) {
    int a = n.qmin, b = n.qmax;
    std::wstring s;
    if (a == b)                   s = a == 1 ? no.one : L"exactly " + std::to_wstring(a) + L" " + no.many;
    else if (a == 0 && b == 1)    s = L"optionally " + no.one;
    else if (b == -1)             s = (a == 0 ? L"zero or more " : a == 1 ? L"one or more " : std::to_wstring(a) + L" or more ") + no.many;
    else                          s = L"between " + std::to_wstring(a) + L" and " + std::to_wstring(b) + L" " + no.many;
    return s + mode_suffix(n);
}

struct Renderer {
    const std::wstring& p;
    const ParseResult& pr;
    const ExplainContext& ctx;
    std::vector<ExplainLine> out;

    std::wstring code(const Node& n) const { return p.substr(n.pos, n.len); }

    void line(int depth, const std::wstring& c, const std::wstring& t, int group = 0) {
        out.push_back({ depth, c, t, group });
    }

    void render(const Node& n, int depth) {
        switch (n.kind) {
        case NodeKind::Sequence:
            if (n.kids.empty()) { line(depth, L"", L"(nothing: matches an empty string)"); return; }
            for (const Node& k : n.kids) render(k, depth);
            return;

        case NodeKind::Alternation: {
            std::wstring c = code(n);
            if (c.size() > 40) c = c.substr(0, 37) + L"...";
            line(depth, c, L"one of these alternatives:");
            int k = 0;
            for (const Node& br : n.kids) {
                ++k;
                if (br.kind == NodeKind::Sequence && br.kids.size() > 1) {
                    line(depth + 1, code(br), L"alternative " + std::to_wstring(k) + L":");
                    render(br, depth + 2);
                } else if (br.kind == NodeKind::Sequence && br.kids.empty()) {
                    line(depth + 1, L"", L"alternative " + std::to_wstring(k) + L": nothing (an empty match)");
                } else {
                    render(br, depth + 1);
                }
            }
            return;
        }

        case NodeKind::Literal: {
            Noun no = noun_of(n, ctx);
            if (n.q != QMode::None && no.ok) { line(depth, code(n), quantified(no, n)); return; }
            std::wstring t = n.text.size() == 1 ? L"a " + quoted(n.text) : L"the text " + quoted(n.text);
            if (n.text == L" ") t = L"a space";
            line(depth, code(n), t + (n.q != QMode::None ? L", " + times(n) + mode_suffix(n) : L""));
            return;
        }

        case NodeKind::Any:
        case NodeKind::Class: {
            Noun no = noun_of(n, ctx);
            line(depth, code(n), n.q != QMode::None ? quantified(no, n) : no.one);
            return;
        }

        case NodeKind::Anchor: {
            std::wstring t = n.text == L"^" ? (ctx.multiline_anchors ? L"start of a line" : L"start of the text")
                                            : (ctx.multiline_anchors ? L"end of a line" : L"end of the text (or just before a final line break)");
            line(depth, code(n), t);
            return;
        }

        case NodeKind::Escape: {
            wchar_t c = n.text.empty() ? 0 : n.text[0];
            std::wstring t;
            switch (c) {
            case L'b': t = L"a word boundary (between a word character and something else)"; break;
            case L'B': t = L"not a word boundary"; break;
            case L'A': t = L"start of the text"; break;
            case L'z': t = L"end of the text"; break;
            case L'Z': t = L"end of the text (or just before a final line break)"; break;
            case L'G': t = L"where the previous match ended"; break;
            }
            if (!t.empty()) { line(depth, code(n), t); return; }
            Noun no = noun_of(n, ctx);
            if (no.ok) { line(depth, code(n), n.q != QMode::None ? quantified(no, n) : no.one); return; }
            line(depth, code(n), L"(not explained: " + code(n) + L")");
            return;
        }

        case NodeKind::Backref: {
            std::wstring t = !n.name.empty() ? L"the same text that group \"" + n.name + L"\" matched"
                                             : L"the same text that group " + std::to_wstring(n.group) + L" matched";
            if (n.q != QMode::None) t += L", " + times(n) + mode_suffix(n);
            line(depth, code(n), t);
            return;
        }

        case NodeKind::Group: {
            std::wstring open = n.kids.empty() ? code(n) : p.substr(n.pos, n.kids[0].pos - n.pos);
            std::wstring t;
            switch (n.gkind) {
            case GroupKind::Capture:       t = L"group " + std::to_wstring(n.group) + L":"; break;
            case GroupKind::Named:         t = L"group " + std::to_wstring(n.group) + L" \"" + n.name + L"\":"; break;
            case GroupKind::NonCapture:    t = L"a group (not captured):"; break;
            case GroupKind::Lookahead:     t = L"followed by (checked, not part of the match):"; break;
            case GroupKind::NegLookahead:  t = L"not followed by:"; break;
            case GroupKind::Lookbehind:    t = L"preceded by (checked, not part of the match):"; break;
            case GroupKind::NegLookbehind: t = L"not preceded by:"; break;
            case GroupKind::Atomic:        t = L"an atomic group (no backtracking into it):"; break;
            case GroupKind::ScopedFlags:   t = L"with " + flags_desc(n.text) + L":"; break;
            case GroupKind::InlineFlags:   line(depth, code(n), L"from here on: " + flags_desc(n.text)); return;
            case GroupKind::Comment:       line(depth, code(n), L"a comment (ignored)"); return;
            case GroupKind::Other:         line(depth, code(n), L"(not explained: " + code(n) + L")"); return;
            }
            if (n.q != QMode::None) {
                std::wstring rep = times(n);
                t.pop_back();                                   // the ':'
                t += rep == L"optional" ? L" (optional" + mode_suffix(n) + L"):" : L", repeated " + rep + mode_suffix(n) + L":";
            }
            line(depth, open, t, n.group);
            if (!n.kids.empty()) render(n.kids[0], depth + 1);
            return;
        }

        case NodeKind::Unknown:
            line(depth, code(n), L"(not explained: " + code(n) + L")");
            return;
        }
    }
};

} // namespace

std::vector<ExplainLine> explain(const std::wstring& pattern, const ParseResult& pr, const ExplainContext& ctx) {
    Renderer r{ pattern, pr, ctx, {} };
    r.render(pr.root, 0);
    return r.out;
}

} // namespace rv
