// UltraCanvas/include/UltraCanvasPathUtf8.h
// UTF-8 <-> std::filesystem::path, independent of the Windows code page.
//
// UltraCanvas strings are UTF-8 everywhere. std::filesystem does not agree on
// Windows: with libc++ (the MSYS2 CLANG64 / CLANGARM64 toolchain the Windows
// builds use) both `path(std::string)` and `path::string()` convert through
// the process's ANSI code page. Reading a name back with `.string()` THROWS
// ("filesystem error: in __wide_to_char: Illegal byte sequence") as soon as
// one character has no equivalent there - an emoji, a CJK name, an accented
// letter on a Thai system - and building a path from a UTF-8 string silently
// decodes it as that code page and names a different file. The manifest's
// UTF-8 activeCodePage would avoid both, but Windows before 10 1903 ignores
// it, so nothing may rely on it.
//
// So every conversion between a path and a std::string goes through these two
// functions (scripts/check_path_string.py enforces it):
//
//     const std::string name = PathToUtf8(entry.path().filename());
//     std::ifstream in(PathFromUtf8(utf8Path), std::ios::binary);
//
// On Windows they convert UTF-8 <-> UTF-16 here, in code that never throws: a
// malformed sequence (invalid UTF-8 in, an unpaired surrogate from NTFS out)
// becomes U+FFFD, as MultiByteToWideChar / WideCharToMultiByte do without
// their error flags. Everywhere else a path's native string is already the
// bytes, so they pass through unchanged.
//
// Header-only on purpose: headless engines, VirtualFS and single-file test
// builds use it without linking the UltraCanvas core.
// Version: 1.0.0
// Last Modified: 2026-09-27
// Author: UltraCanvas Framework
#pragma once

#include <cstdio>
#include <filesystem>
#include <string>

namespace UltraCanvas {

namespace PathUtf8Detail {

    // UTF-8 -> UTF-16, lenient: each malformed or overlong sequence, surrogate
    // code point or value above U+10FFFF becomes one U+FFFD.
    template<class WideString>
    WideString Utf8ToUtf16(const std::string& utf8) {
        using Unit = typename WideString::value_type;
        static_assert(sizeof(Unit) == 2, "UTF-16 code units expected");
        WideString out;
        out.reserve(utf8.size());
        const size_t n = utf8.size();
        size_t i = 0;
        while (i < n) {
            const unsigned char b0 = static_cast<unsigned char>(utf8[i]);
            char32_t cp = 0xFFFD;
            size_t len = 1;
            if (b0 < 0x80) {
                cp = b0;
            } else {
                size_t need = 0;
                char32_t min = 0;
                if (b0 >= 0xC2 && b0 <= 0xDF)      { need = 1; cp = b0 & 0x1F; min = 0x80; }
                else if (b0 >= 0xE0 && b0 <= 0xEF) { need = 2; cp = b0 & 0x0F; min = 0x800; }
                else if (b0 >= 0xF0 && b0 <= 0xF4) { need = 3; cp = b0 & 0x07; min = 0x10000; }
                bool ok = need > 0;
                size_t k = 1;
                for (; ok && k <= need; ++k) {
                    if (i + k >= n) { ok = false; break; }
                    const unsigned char b = static_cast<unsigned char>(utf8[i + k]);
                    if ((b & 0xC0) != 0x80) { ok = false; break; }
                    cp = (cp << 6) | (b & 0x3F);
                }
                if (ok && (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)))
                    ok = false;
                if (ok) {
                    len = need + 1;
                } else {
                    // Skip the lead byte and the continuation bytes that did
                    // belong to it; the next lead starts a fresh sequence.
                    cp = 0xFFFD;
                    len = need > 0 ? k : 1;
                    if (len == 0) len = 1;
                }
            }
            if (cp >= 0x10000) {
                cp -= 0x10000;
                out.push_back(static_cast<Unit>(0xD800 + (cp >> 10)));
                out.push_back(static_cast<Unit>(0xDC00 + (cp & 0x3FF)));
            } else {
                out.push_back(static_cast<Unit>(cp));
            }
            i += len;
        }
        return out;
    }

    // UTF-16 -> UTF-8, lenient: an unpaired surrogate becomes U+FFFD.
    template<class WideString>
    std::string Utf16ToUtf8(const WideString& wide) {
        std::string out;
        out.reserve(wide.size());
        const size_t n = wide.size();
        for (size_t i = 0; i < n; ++i) {
            char32_t cp = static_cast<char16_t>(wide[i]);
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < n) {
                const char32_t low = static_cast<char16_t>(wide[i + 1]);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    ++i;
                } else {
                    cp = 0xFFFD;
                }
            } else if (cp >= 0xD800 && cp <= 0xDFFF) {
                cp = 0xFFFD;
            }
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

} // namespace PathUtf8Detail

    // A UTF-8 string as a path that names that file on every platform.
    inline std::filesystem::path PathFromUtf8(const std::string& utf8) {
#if defined(_WIN32) || defined(_WIN64)
        return std::filesystem::path(PathUtf8Detail::Utf8ToUtf16<std::wstring>(utf8));
#else
        return std::filesystem::path(utf8);
#endif
    }

    // A path's name as UTF-8. Never throws, whatever the name holds.
    inline std::string PathToUtf8(const std::filesystem::path& p) {
#if defined(_WIN32) || defined(_WIN64)
        return PathUtf8Detail::Utf16ToUtf8(p.native());
#else
        return p.string();   // path-string-ok: the native string is the bytes
#endif
    }

    // std::fopen for a UTF-8 path. The narrow fopen reads the name in the
    // Windows code page, so there it goes through _wfopen instead.
    inline std::FILE* OpenFileUtf8(const std::string& utf8Path, const char* mode) {
#if defined(_WIN32) || defined(_WIN64)
        const std::wstring wideMode(mode, mode + std::char_traits<char>::length(mode));
        return ::_wfopen(PathFromUtf8(utf8Path).c_str(), wideMode.c_str());
#else
        return std::fopen(utf8Path.c_str(), mode);
#endif
    }

} // namespace UltraCanvas
