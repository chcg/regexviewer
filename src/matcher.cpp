#define BOOST_REGEX_STANDALONE
#include <boost/regex.hpp>
#include "matcher.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace rv {

static boost::wregex::flag_type syntax_of(const Prepared& p) {
    boost::wregex::flag_type f;
    switch (p.flavor) {
    case Flavor::PosixERE: f = boost::regex::extended; break;
    case Flavor::PosixBRE: f = boost::regex::basic; break;
    default:               f = boost::regex::perl; break;
    }
    if (p.flags.i) f |= boost::regex::icase;
    if (p.flags.x && p.flavor != Flavor::PosixERE && p.flavor != Flavor::PosixBRE) f |= boost::regex::mod_x;
    return f;
}

static boost::match_flag_type match_flags_of(const Prepared& p) {
    boost::match_flag_type m = boost::match_default;
    if (!p.multiline_anchors()) m |= boost::match_single_line;
    if (!p.dot_all())           m |= boost::match_not_dot_newline;
    return m;
}

static std::wstring widen_ascii(const char* s) {
    std::wstring w;
    while (*s) w += (wchar_t)(unsigned char)*s++;
    return w;
}

static bool compile(const Prepared& p, boost::wregex& re, std::wstring& err, int& pos) {
    try {
        re.assign(p.engine_pattern, syntax_of(p));
        return true;
    } catch (const boost::regex_error& e) {
        err = widen_ascii(e.what());
        size_t nl = err.find(L"  The error occurred");
        if (nl != std::wstring::npos) err.erase(nl);
        pos = (int)e.position();
        return false;
    }
}

std::wstring check_pattern(const Prepared& p, int* error_pos) {
    boost::wregex re;
    std::wstring err;
    int pos = -1;
    if (compile(p, re, err, pos)) return L"";
    if (error_pos) *error_pos = pos;
    return err;
}

MatchRun run_matches(const Prepared& p, const std::wstring& text, const RunOptions& opt) {
    MatchRun r;
    boost::wregex re;
    if (!compile(p, re, r.error, r.error_pos)) { r.compiled = false; return r; }
    if (p.rejected) return r;

    size_t end = opt.end == std::wstring::npos || opt.end > text.size() ? text.size() : opt.end;
    size_t begin = opt.begin > end ? end : opt.begin;
    auto b = text.begin() + begin, e = text.begin() + end;

    boost::match_flag_type mf = match_flags_of(p);
    if (begin > 0) mf |= boost::match_prev_avail;

    ULONGLONG t0 = GetTickCount64();
    const std::wstring fmt = p.npp_replacement;
    try {
        boost::wsregex_iterator it(b, e, re, mf), stop;
        for (; it != stop; ++it) {
            const boost::wsmatch& m = *it;
            if (r.items.size() < opt.max_items) {
                MatchItem mi;
                mi.groups.resize(m.size());
                for (size_t g = 0; g < m.size(); ++g) {
                    if (!m[g].matched) continue;
                    mi.groups[g].start = (int)(m[g].first - text.begin());
                    mi.groups[g].end   = (int)(m[g].second - text.begin());
                }
                if (opt.with_replacement && p.has_replacement)
                    mi.replaced = m.format(fmt, boost::format_perl);
                r.items.push_back(std::move(mi));
            } else {
                r.capped = true;
            }
            ++r.total;
            if (r.total >= opt.count_limit) { r.count_capped = true; r.stopped_at = (size_t)(m[0].second - text.begin()); break; }
            if ((r.total & 255) == 0) {
                if (opt.cancel_token && opt.cancel_token->load() != opt.cancel_value) { r.cancelled = true; break; }
                if (GetTickCount64() - t0 > opt.budget_ms) {
                    r.timed_out = true;
                    r.stopped_at = (size_t)(m[0].second - text.begin());
                    break;
                }
            }
        }
    } catch (const std::exception&) {
        r.too_slow = true;
        r.stopped_at = r.items.empty() ? begin : (size_t)r.items.back().groups[0].end;
    }
    return r;
}

} // namespace rv
