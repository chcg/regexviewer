#pragma once
// A parser for Perl-style regular expressions (the syntax Boost.Regex and
// Notepad++ use, plus the Python/.NET/Java variants Regex Viewer converts
// from). It only serves the explanation and the flavour checks; the engine
// itself is always the referee on what a pattern means.
#include <string>
#include <vector>

namespace rv {

enum class NodeKind { Sequence, Alternation, Literal, Any, Class, Escape, Anchor, Group, Backref, Unknown };

enum class GroupKind {
    Capture, Named, NonCapture, Lookahead, NegLookahead, Lookbehind, NegLookbehind,
    Atomic, InlineFlags, ScopedFlags, Comment, Other
};

enum class QMode { None, Greedy, Lazy, Possessive };

struct Node {
    NodeKind  kind = NodeKind::Sequence;
    size_t    pos = 0, len = 0;      // span in the pattern, quantifier included
    std::wstring text;               // literal text, escape body, class body, flags, unknown text
    // groups
    GroupKind gkind = GroupKind::Capture;
    int       group = 0;             // capture number (Capture / Named), else 0
    std::wstring name;               // named group / named backref
    // classes
    bool      negated = false;
    // quantifier on this node
    QMode     q = QMode::None;
    int       qmin = 1, qmax = 1;    // qmax -1 = unbounded
    std::vector<Node> kids;
};

struct GroupSpan { int group; size_t open, close; std::wstring name; };   // close = index of ')'

struct ParseResult {
    Node root;
    int  group_count = 0;
    std::vector<GroupSpan> groups;
    // constructs the flavour checks look for
    bool has_lookahead = false, has_lookbehind = false, has_backref = false;
    bool has_possessive = false, has_atomic = false, has_lazy = false;
    bool has_line_anchors = false;          // ^ or $ outside classes
    bool has_python_named = false;          // (?P<name>…) / (?P=name)
    bool has_unicode_prop = false;          // \p{…}
    bool has_unknown = false;               // something the explanation can't describe
    bool has_conditional = false, has_branch_reset = false, has_balancing = false;
    bool has_shorthand = false;             // \d \w \s and friends
};

// extended: the x flag (whitespace and # comments ignored outside classes).
ParseResult parse_regex(const std::wstring& pattern, bool extended);

} // namespace rv
