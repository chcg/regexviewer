#include "encoding.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>

namespace rv {

static const unsigned CP_UTF8_ = 65001;
static const size_t   STEP = 64;          // checkpoint spacing in UTF-16 units

size_t TextMap::decode_one(size_t b, std::wstring* out, unsigned* units) const {
    const unsigned char* s = reinterpret_cast<const unsigned char*>(copy_.data());
    auto emit = [&](unsigned cp) {
        if (cp >= 0x10000) {
            cp -= 0x10000;
            if (out) { out->push_back(wchar_t(0xD800 + (cp >> 10))); out->push_back(wchar_t(0xDC00 + (cp & 0x3FF))); }
            *units = 2;
        } else {
            if (out) out->push_back(wchar_t(cp));
            *units = 1;
        }
    };

    if (cp_ != CP_UTF8_) {
        // ANSI: one byte, or two for a DBCS lead byte.
        size_t n = (IsDBCSLeadByteEx(cp_, s[b]) && b + 1 < len_) ? 2 : 1;
        wchar_t w[2] = {};
        int k = MultiByteToWideChar(cp_, 0, reinterpret_cast<const char*>(s + b), (int)n, w, 2);
        if (k <= 0) { w[0] = 0xFFFD; k = 1; }
        if (out) out->append(w, (size_t)k);
        *units = (unsigned)k;
        return n;
    }

    unsigned c = s[b];
    if (c < 0x80) { emit(c); return 1; }
    // Continuation bytes needed after the lead byte (0 = not a valid lead byte).
    size_t need = (c >= 0xC2 && c <= 0xDF) ? 1 : (c >= 0xE0 && c <= 0xEF) ? 2 : (c >= 0xF0 && c <= 0xF4) ? 3 : 0;
    if (need && b + need < len_) {
        unsigned cp = c & (0x3F >> need);
        bool ok = true;
        for (size_t k = 1; k <= need; ++k) {
            if ((s[b + k] & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (s[b + k] & 0x3F);
        }
        if (ok && !(need == 2 && cp < 0x800) && !(need == 3 && (cp < 0x10000 || cp > 0x10FFFF)) &&
            !(cp >= 0xD800 && cp <= 0xDFFF)) {
            emit(cp);
            return need + 1;
        }
    }
    // Invalid byte: becomes U+FFFD, consumes exactly one byte.
    emit(0xFFFD);
    return 1;
}

void TextMap::build(const char* bytes, size_t len, unsigned codepage) {
    copy_.assign(bytes, len);
    len_ = len;
    cp_ = codepage;
    wide_.clear();
    wide_.reserve(len);
    cps_.clear();
    invalid_ = false;

    size_t b = 0;
    size_t next_cp = 0;
    while (b < len_) {
        if (wide_.size() >= next_cp) {
            cps_.push_back({ wide_.size(), b });
            next_cp = wide_.size() + STEP;
        }
        unsigned units = 0;
        size_t before = wide_.size();
        size_t n = decode_one(b, &wide_, &units);
        if (cp_ == CP_UTF8_ && units == 1 && wide_[before] == 0xFFFD &&
            !(n == 3 && (unsigned char)copy_[b] == 0xEF))          // a real U+FFFD is EF BF BD
            invalid_ = true;
        b += n;
    }
    cps_.push_back({ wide_.size(), len_ });
}

size_t TextMap::to_byte(size_t unit) const {
    if (cps_.empty()) return 0;
    if (unit >= wide_.size()) return len_;
    auto it = std::upper_bound(cps_.begin(), cps_.end(), unit,
                               [](size_t u, const Checkpoint& c) { return u < c.unit; });
    --it;
    size_t u = it->unit, b = it->byte;
    while (u < unit && b < len_) {
        unsigned units = 0;
        size_t n = decode_one(b, nullptr, &units);
        if (u + units > unit) break;              // unit points into a surrogate pair
        u += units;
        b += n;
    }
    return b;
}

size_t TextMap::to_unit(size_t byte) const {
    if (cps_.empty()) return 0;
    if (byte >= len_) return wide_.size();
    auto it = std::upper_bound(cps_.begin(), cps_.end(), byte,
                               [](size_t x, const Checkpoint& c) { return x < c.byte; });
    --it;
    size_t u = it->unit, b = it->byte;
    while (b < byte) {
        unsigned units = 0;
        size_t n = decode_one(b, nullptr, &units);
        if (b + n > byte) break;                  // byte is inside this character
        u += units;
        b += n;
    }
    return u;
}

} // namespace rv
