#pragma once
// Running a prepared regex over UTF-16 text with Boost.Regex: every match with
// the span of every group, optional replacement text, a time budget.
#include <string>
#include <vector>
#include <atomic>
#include "flavor.h"

namespace rv {

struct Span { int start = -1, end = -1; };          // UTF-16 indices; -1 = group didn't take part

struct MatchItem {
    std::vector<Span> groups;                       // [0] = the whole match
    std::wstring      replaced;                     // set when a replacement was given
};

struct MatchRun {
    bool         compiled = true;
    std::wstring error;                             // compile error text
    int          error_pos = -1;                    // position in the pattern, if known
    std::vector<MatchItem> items;                   // first `max_items` matches
    size_t       total = 0;                         // all matches counted (up to count_limit)
    bool         capped = false;                    // items stopped at max_items
    bool         count_capped = false;              // counting stopped at count_limit
    bool         too_slow = false;                  // engine gave up (catastrophic backtracking)
    bool         timed_out = false;                 // time budget used up
    size_t       stopped_at = 0;                    // UTF-16 index where it stopped
    bool         cancelled = false;
};

struct RunOptions {
    size_t   begin = 0, end = std::wstring::npos;   // range of the text to search
    size_t   max_items = 10000;
    size_t   count_limit = 1000000;
    unsigned budget_ms = 2000;
    bool     with_replacement = false;
    const std::atomic<int>* cancel_token = nullptr; // run stops when *cancel_token != cancel_value
    int      cancel_value = 0;
};

MatchRun run_matches(const Prepared& p, const std::wstring& text, const RunOptions& opt);

// Compile only, for an early syntax check. Returns "" when the pattern is valid.
std::wstring check_pattern(const Prepared& p, int* error_pos);

} // namespace rv
