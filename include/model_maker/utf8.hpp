#pragma once
// UTF-8 <-> std::wstring. JSON kalicilik katmanlari (component_presets, marking,
// drawing) icin tek tanim.

#include <cstdint>
#include <string>

namespace mm::text {

inline std::string toUtf8(const std::wstring& ws) {
    std::string out;
    out.reserve(ws.size());
    for (const wchar_t wc : ws) {
        const std::uint32_t cp = static_cast<std::uint32_t>(wc);
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

inline std::wstring fromUtf8(const std::string& s) {
    std::wstring out;
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::uint32_t cp = 0;
        int extra = 0;
        if (c < 0x80) {
            cp = c;
        } else if ((c >> 5) == 0x6) {
            cp = c & 0x1F;
            extra = 1;
        } else if ((c >> 4) == 0xE) {
            cp = c & 0x0F;
            extra = 2;
        } else if ((c >> 3) == 0x1E) {
            cp = c & 0x07;
            extra = 3;
        } else {
            ++i;
            continue;
        }
        ++i;
        for (int k = 0; k < extra && i < s.size(); ++k, ++i)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
        out.push_back(static_cast<wchar_t>(cp));
    }
    return out;
}

} // namespace mm::text
