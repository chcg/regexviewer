#pragma once
// Document bytes (UTF-8 or an ANSI code page) <-> UTF-16 for the regex engine,
// with a position map both ways. Notepad++'s own Find also matches on UTF-16
// (boost::wregex), so this keeps the semantics identical.
#include <string>
#include <vector>
#include <cstddef>

namespace rv {

class TextMap {
public:
    // codepage: 65001 for UTF-8, otherwise the document's ANSI code page.
    void build(const char* bytes, size_t len, unsigned codepage);

    const std::wstring& text() const { return wide_; }
    bool had_invalid_utf8() const { return invalid_; }

    // UTF-16 index -> byte offset in the document (index may equal size()).
    size_t to_byte(size_t unit) const;
    // Byte offset -> UTF-16 index of the character containing that byte.
    size_t to_unit(size_t byte) const;

private:
    struct Checkpoint { size_t unit, byte; };
    // Decode one character at bytes_[b]; appends its UTF-16 units to `out`
    // (if not null) and returns the number of bytes consumed (>= 1).
    size_t decode_one(size_t b, std::wstring* out, unsigned* units) const;

    std::string             copy_;              // own copy for later lookups
    size_t                  len_ = 0;
    unsigned                cp_ = 65001;
    std::wstring            wide_;
    std::vector<Checkpoint> cps_;               // every ~64 units, at character starts
    bool                    invalid_ = false;
};

} // namespace rv
