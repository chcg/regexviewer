#pragma once
// Getting regexes out of whatever was pasted: code in Python, JavaScript, C#,
// Java/Kotlin, PHP, Go/Rust, PowerShell, sed/grep/perl one-liners, markdown
// code blocks, or a bare pattern.
#include <string>
#include <vector>
#include "flavor.h"

namespace rv {

struct Candidate {
    std::wstring pattern;                 // unescaped: what the regex engine gets
    Flags        flags;
    bool         has_replacement = false;
    std::wstring replacement;             // in the source's own syntax
    Flavor       flavor = Flavor::NotepadPP;   // the source's flavour
    bool         recognised = false;      // false = bare text, source unknown
    std::wstring source;                  // "Python · raw string · flags: MULTILINE"
    size_t       begin = 0, end = 0;      // span in the paste
    std::vector<std::wstring> notes;      // e.g. string-escape surprises
};

std::vector<Candidate> extract(const std::wstring& paste);

} // namespace rv
