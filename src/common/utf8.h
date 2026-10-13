#pragma once

#include <cstdint>
#include <string>

namespace utf8 {

// Decodes the code point starting at s[i] and advances i. Invalid bytes decode to U+FFFD.
inline uint32_t decode(const std::string& s, size_t& i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
        i++;
        return c;
    }
    int len = (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
    if (len == 1 || i + len > s.size()) {
        i++;
        return 0xFFFD;
    }
    uint32_t cp = c & (0xFFu >> (len + 1));
    for (int k = 1; k < len; k++) {
        unsigned char cc = static_cast<unsigned char>(s[i + k]);
        if ((cc & 0xC0) != 0x80) {
            i++;
            return 0xFFFD;
        }
        cp = (cp << 6) | (cc & 0x3F);
    }
    i += len;
    return cp;
}

inline void append(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

inline size_t length(const std::string& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size();) {
        decode(s, i);
        n++;
    }
    return n;
}

}  // namespace utf8
