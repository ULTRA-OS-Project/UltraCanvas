// include/UltraCanvasUtils.h
// Utils
// Version: 1.0.0
// Last Modified: 2025-09-14
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasCommonTypes.h"
#include <string>
#include <glib.h>

namespace UltraCanvas {
    // Codepoint count
    inline int utf8_length(const std::string& s) {
        if (s.empty()) return 0;
        return static_cast<int>(g_utf8_strlen(s.c_str(), static_cast<gssize>(s.size())));
    }

    // Codepoint index -> byte offset
    inline size_t utf8_cp_to_byte(const std::string& s, int cpIndex) {
        if (cpIndex <= 0 || s.empty()) return 0;
        const char* p = g_utf8_offset_to_pointer(s.c_str(), cpIndex);
        return static_cast<size_t>(p - s.c_str());
    }

    // Byte offset -> codepoint index
    inline int utf8_byte_to_cp(const std::string& s, size_t byteOff) {
        if (byteOff == 0 || s.empty()) return 0;
        return static_cast<int>(g_utf8_pointer_to_offset(s.c_str(), s.c_str() + byteOff));
    }

    // Get codepoint at codepoint index
    inline gunichar utf8_get_cp(const std::string& s, int idx) {
        return g_utf8_get_char(g_utf8_offset_to_pointer(s.c_str(), idx));
    }

    // Substring by codepoint position/count (-1 count = to end)
    std::string utf8_substr(const std::string& s, int pos, int count = -1);

    // Get single codepoint as UTF-8 string
    inline std::string utf8_char_at(const std::string& s, int idx) {
        return utf8_substr(s, idx, 1);
    }

    // Insert string at codepoint position (in-place)
    inline void utf8_insert(std::string& s, int cpPos, const std::string& ins) {
        s.insert(utf8_cp_to_byte(s, cpPos), ins);
    }

    // Erase codepoints at position (in-place)
    void utf8_erase(std::string& s, int cpPos, int cpCount = 1);

    // Replace codepoints at position (in-place)
    void utf8_replace(std::string& s, int cpPos, int cpCount, const std::string& rep);

    // Encode a codepoint to UTF-8
    std::string utf8_encode(gunichar cp);

    // Forward find. Returns codepoint position, or -1 if not found.
    // Case-insensitive mode uses g_utf8_strdown (preserves codepoint count).
    int utf8_find(const std::string& haystack, const std::string& needle,
                        int startCp = 0, bool caseSensitive = true);

    // Reverse find. Returns codepoint position, or -1.
    int utf8_rfind(const std::string& haystack, const std::string& needle,
                        int maxCp = -1, bool caseSensitive = true);

    // Split by single-byte delimiter (e.g. '\n')
    std::vector<std::string> utf8_split(const std::string& s, char delim);
}