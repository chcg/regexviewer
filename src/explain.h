#pragma once
// Plain-language explanation of a regex, one line per part.
#include <string>
#include <vector>
#include "regex_parse.h"

namespace rv {

struct ExplainLine {
    int          depth = 0;      // indentation level
    std::wstring code;           // the part of the pattern this line is about
    std::wstring text;           // what it does
    int          group = 0;      // capture group this line opens (for colouring), else 0
};

struct ExplainContext {
    bool multiline_anchors = true;   // ^/$ per line (Notepad++, or the m flag)
    bool dot_all = false;            // . also matches line breaks (s flag)
};

std::vector<ExplainLine> explain(const std::wstring& pattern, const ParseResult& pr, const ExplainContext& ctx);

} // namespace rv
