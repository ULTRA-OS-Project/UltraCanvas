// core/UltraCanvasUtils.h
// Utils
// Version: 1.0.0
// Last Modified: 2025-09-14
// Author: UltraCanvas Framework

#include "UltraCanvasUtilsUtf8.h"
#include <cstring>

namespace UltraCanvas {
    // Substring by codepoint position/count (-1 count = to end)
    std::string utf8_substr(const std::string& s, int pos, int count) {
        const char* start = g_utf8_offset_to_pointer(s.c_str(), pos);
        if (count < 0) return std::string(start);
        const char* end = g_utf8_offset_to_pointer(start, count);
        return std::string(start, static_cast<size_t>(end - start));
    }

    // Erase codepoints at position (in-place)
    void utf8_erase(std::string& s, int cpPos, int cpCount) {
        size_t bStart = utf8_cp_to_byte(s, cpPos);
        size_t bEnd = utf8_cp_to_byte(s, cpPos + cpCount);
        s.erase(bStart, bEnd - bStart);
    }

    // Replace codepoints at position (in-place)
    void utf8_replace(std::string& s, int cpPos, int cpCount, const std::string& rep) {
        size_t bStart = utf8_cp_to_byte(s, cpPos);
        size_t bEnd = utf8_cp_to_byte(s, cpPos + cpCount);
        s.replace(bStart, bEnd - bStart, rep);
    }

    // Encode a codepoint to UTF-8
    std::string utf8_encode(gunichar cp) {
        char buf[6];
        int len = g_unichar_to_utf8(cp, buf);
        return std::string(buf, static_cast<size_t>(len));
    }

    // Forward find. Returns codepoint position, or -1 if not found.
    // Case-insensitive mode uses g_utf8_strdown (preserves codepoint count).
    int utf8_find(const std::string& haystack, const std::string& needle,
                        int startCp, bool caseSensitive) {
        if (needle.empty()) return -1;
        if (caseSensitive) {
            const char* base = haystack.c_str();
            const char* from = g_utf8_offset_to_pointer(base, startCp);
            const char* found = strstr(from, needle.c_str());
            if (!found) return -1;
            return static_cast<int>(g_utf8_pointer_to_offset(base, found));
        }
        gchar* lH = g_utf8_strdown(haystack.c_str(), -1);
        gchar* lN = g_utf8_strdown(needle.c_str(), -1);
        const char* from = g_utf8_offset_to_pointer(lH, startCp);
        const char* found = strstr(from, lN);
        int result = found ? static_cast<int>(g_utf8_pointer_to_offset(lH, found)) : -1;
        g_free(lH);
        g_free(lN);
        return result;
    }

    // Reverse find. Returns codepoint position, or -1.
    int utf8_rfind(const std::string& haystack, const std::string& needle,
                        int maxCp, bool caseSensitive) {
        if (needle.empty()) return -1;
        std::string h, n;
        if (caseSensitive) {
            h = haystack; n = needle;
        } else {
            gchar* lH = g_utf8_strdown(haystack.c_str(), -1);
            gchar* lN = g_utf8_strdown(needle.c_str(), -1);
            h = lH; n = lN;
            g_free(lH); g_free(lN);
        }
        size_t maxByte = (maxCp < 0) ? std::string::npos : utf8_cp_to_byte(h, maxCp);
        size_t bp = h.rfind(n, maxByte);
        if (bp == std::string::npos) return -1;
        return static_cast<int>(g_utf8_pointer_to_offset(h.c_str(), h.c_str() + bp));
    }

    // Split by single-byte delimiter (e.g. '\n')
    std::vector<std::string> utf8_split(const std::string& s, char delim) {
        std::vector<std::string> result;
        size_t start = 0, pos;
        while ((pos = s.find(delim, start)) != std::string::npos) {
            result.push_back(s.substr(start, pos - start));
            start = pos + 1;
        }
        result.push_back(s.substr(start));
        return result;
    }
}    
