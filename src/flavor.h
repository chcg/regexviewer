#pragma once
// Regex flavours: how a pattern from Python, JavaScript, … is turned into a
// pattern Boost.Regex (and Notepad++ Find) accepts, which semantics the engine
// should use, and what to warn about.
#include <string>
#include <vector>
#include "regex_parse.h"

namespace rv {

enum class Flavor { NotepadPP, Python, JavaScript, DotNet, Java, PCRE, PosixERE, PosixBRE, RE2, Count };

const wchar_t* flavor_name(Flavor f);      // "Notepad++", "Python", …
const char*    flavor_key(Flavor f);       // "npp", "python", … (settings)
bool           flavor_from_key(const std::string& key, Flavor& out);

struct Flags {
    bool i = false;   // ignore case
    bool m = false;   // ^/$ at line breaks
    bool s = false;   // . matches line breaks
    bool x = false;   // extended (whitespace/comments ignored)
};

std::wstring flags_text(const Flags& f);   // "i, m" or ""

struct Prepared {
    Flavor       source = Flavor::NotepadPP;     // where the regex came from
    Flavor       flavor = Flavor::NotepadPP;     // the flavour whose semantics are used
    Flags        flags;
    std::wstring engine_pattern;                 // what Boost compiles (syntax per flavour)
    std::wstring npp_pattern;                    // for "Copy for Notepad++ Find"
    std::wstring parse_pattern;                  // Perl-syntax form the explanation is built from
    std::wstring npp_replacement;                // replacement in Notepad++ syntax ($1, $+{name})
    bool         has_replacement = false;
    bool         rejected = false;               // the flavour would refuse this regex (RE2)
    std::wstring reject_reason;
    bool         anchors_differ = false;         // like-Notepad++ mode, but the source uses ^/$ per text
    std::vector<std::wstring> warnings;
    ParseResult  parse;

    // engine semantics
    bool multiline_anchors() const;
    bool dot_all() const { return flags.s; }
};

// source: the flavour the regex was written for (from the paste);
// flavor: the flavour to match like (Notepad++ by default).
Prepared prepare(const std::wstring& pattern, const std::wstring* replacement,
                 Flavor source, Flavor flavor, const Flags& flags);

// Convert a replacement string from `source` syntax to Notepad++/Boost syntax.
std::wstring convert_replacement(const std::wstring& repl, Flavor source);

} // namespace rv
