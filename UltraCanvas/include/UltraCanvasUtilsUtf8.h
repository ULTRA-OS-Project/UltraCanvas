// include/UltraCanvasUtilsUtf8.h
// UTF-8 string utilities: codepoint indexing, byte-offset character
// boundaries, search, split and repair.
// Version: 1.1.0
// Last Modified: 2026-09-15
// V1.1.0: byte-offset boundary helpers (utf8_align_boundary /
//   utf8_prev_boundary / utf8_next_boundary / utf8_boundaries /
//   utf8_bytes_for_chars) plus utf8_make_valid, for buffers that address text
//   by byte offset and must not split a character in half.
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasCommonTypes.h"
#include <string>
#include <vector>
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

    // ===== Byte-offset character boundaries =====
    // For buffers that address text by byte offset (a caret position, a
    // selection end, a hit-test result). Walking such an offset by one byte
    // splits every multi-byte character, and the half sequence that is left
    // is not UTF-8 any more: Pango rejects it, so the field stops drawing and
    // the debug log fills with encoding complaints. These move by whole
    // characters instead.
    //
    // They scan for lead bytes rather than decoding, so text that arrived
    // malformed still advances one byte at a time instead of tripping an
    // assertion inside GLib.
    inline bool utf8_is_continuation_byte(unsigned char b) {
        return (b & 0xC0) == 0x80;
    }

    // Start of the character containing `bytePos` (`bytePos` itself when it
    // already sits on a boundary).
    inline size_t utf8_align_boundary(const std::string& s, size_t bytePos) {
        if (bytePos >= s.size()) return s.size();
        while (bytePos > 0 &&
               utf8_is_continuation_byte(static_cast<unsigned char>(s[bytePos]))) {
            --bytePos;
        }
        return bytePos;
    }

    // Start of the character before `bytePos` (0 when there is none).
    inline size_t utf8_prev_boundary(const std::string& s, size_t bytePos) {
        if (bytePos == 0 || s.empty()) return 0;
        if (bytePos > s.size()) bytePos = s.size();
        --bytePos;
        while (bytePos > 0 &&
               utf8_is_continuation_byte(static_cast<unsigned char>(s[bytePos]))) {
            --bytePos;
        }
        return bytePos;
    }

    // Start of the character after `bytePos` (end of string when there is none).
    inline size_t utf8_next_boundary(const std::string& s, size_t bytePos) {
        if (bytePos >= s.size()) return s.size();
        ++bytePos;
        while (bytePos < s.size() &&
               utf8_is_continuation_byte(static_cast<unsigned char>(s[bytePos]))) {
            ++bytePos;
        }
        return bytePos;
    }

    // Byte offsets of every character start, plus one past the last character.
    // A field that measures prefixes of its text (caret x, hit test, selection
    // highlight) steps through these so no prefix ever ends mid-character.
    inline std::vector<size_t> utf8_boundaries(const std::string& s) {
        std::vector<size_t> offsets;
        offsets.push_back(0);
        for (size_t i = utf8_next_boundary(s, 0); i < s.size();
             i = utf8_next_boundary(s, i)) {
            offsets.push_back(i);
        }
        if (!s.empty()) offsets.push_back(s.size());
        return offsets;
    }

    // Byte length of the first `maxChars` characters - what to truncate to when
    // a limit is expressed in characters, as a user-facing one always is.
    inline size_t utf8_bytes_for_chars(const std::string& s, int maxChars) {
        if (maxChars <= 0) return 0;
        size_t pos = 0;
        for (int i = 0; i < maxChars && pos < s.size(); ++i) {
            pos = utf8_next_boundary(s, pos);
        }
        return pos;
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

    // Return `s` with every byte that is not part of a well-formed UTF-8
    // sequence replaced by U+FFFD. Text entering a widget from outside - the
    // clipboard, a keyboard backend without an input method, a file - is not
    // guaranteed to be UTF-8, and one stray byte is enough to make the whole
    // string unrenderable. Valid input is returned unchanged.
    std::string utf8_make_valid(const std::string& s);

    // Forward find. Returns codepoint position, or -1 if not found.
    // Case-insensitive mode uses g_utf8_strdown (preserves codepoint count).
    int utf8_find(const std::string& haystack, const std::string& needle,
                        int startCp = 0, bool caseSensitive = true);

    // Reverse find. Returns codepoint position, or -1.
    int utf8_rfind(const std::string& haystack, const std::string& needle,
                        int maxCp = -1, bool caseSensitive = true);

    // Replace occurrences of 'find' with 'rep' in 'src'. maxCount=0 means replace all.
    std::string utf8_strreplace(const std::string& src, const std::string& find,
                                const std::string& rep, int maxCount = 0);

    // Split by single-byte delimiter (e.g. '\n')
    std::vector<std::string> utf8_split(const std::string& s, char delim);

    // Split text into lines, handling all EOL styles: \r\n, \n, \r
    std::vector<std::string> utf8_split_lines(const std::string& s);

    // Break-char predicate for long-line sharding (space, tab, punctuation).
    inline bool utf8_is_break_char_cp(gunichar cp) {
        return cp == ' ' || cp == '\t' || cp == ',' || cp == ':' || cp == '*' || cp == '+';
    }

    // Split text into segments:
    //  - \r\n and \r are normalized to \n
    //  - A segment that ends with a real source-line newline keeps the \n at its end
    //  - A logical line longer than hardLimit codepoints is split into multiple
    //    continuation segments (no trailing \n). The sharder scans codepoints
    //    [softLimit..hardLimit) for a break char (space/tab/punctuation) and
    //    splits just after it; if none found, force-splits at hardLimit.
    //  - Concatenating the result verbatim reproduces the normalized text.
    std::vector<std::string> utf8_split_lines_sharded(const std::string& s,
                                                      int softLimit = 4000,
                                                      int hardLimit = 8000);
}