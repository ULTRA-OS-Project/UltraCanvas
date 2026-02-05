// UltraCanvasString.cpp
// UTF-8 aware string class implementation
// Powered by libgrapheme for full Unicode 17.0 compliance
// Version: 2.0.0
// Last Modified: 2026-02-01
// Author: UltraCanvas Framework

#include "UltraCanvasString.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <cstring>

extern "C" {
    #include <grapheme.h>
}

namespace UltraCanvas {

// ===== UNICODE CHARACTER CLASSIFICATION =====
// Compact range tables for codepoint classification
// libgrapheme handles segmentation/case; these cover General Category queries

namespace Unicode {

    bool IsAlphabetic(uint32_t cp) {
        // ASCII fast path
        if (cp < 0x80) {
            return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z');
        }
        // Latin Extended
        if ((cp >= 0x00C0 && cp <= 0x00D6) || (cp >= 0x00D8 && cp <= 0x00F6) ||
            (cp >= 0x00F8 && cp <= 0x02FF)) return true;
        // Latin Extended Additional / IPA / Spacing Modifiers
        if (cp >= 0x0370 && cp <= 0x0373) return true;
        if (cp >= 0x0376 && cp <= 0x0377) return true;
        // Greek and Coptic
        if ((cp >= 0x0388 && cp <= 0x038A) || cp == 0x038C ||
            (cp >= 0x038E && cp <= 0x03A1) || (cp >= 0x03A3 && cp <= 0x03FF)) return true;
        // Cyrillic
        if (cp >= 0x0400 && cp <= 0x04FF) return true;
        if (cp >= 0x0500 && cp <= 0x052F) return true;  // Cyrillic Supplement
        // Armenian
        if ((cp >= 0x0531 && cp <= 0x0556) || (cp >= 0x0561 && cp <= 0x0587)) return true;
        // Hebrew letters
        if (cp >= 0x05D0 && cp <= 0x05EA) return true;
        if (cp >= 0x05F0 && cp <= 0x05F2) return true;
        // Arabic letters
        if ((cp >= 0x0620 && cp <= 0x064A) || (cp >= 0x066E && cp <= 0x066F) ||
            (cp >= 0x0671 && cp <= 0x06D3) || cp == 0x06D5 ||
            (cp >= 0x06E5 && cp <= 0x06E6) || (cp >= 0x06EE && cp <= 0x06EF) ||
            (cp >= 0x06FA && cp <= 0x06FC) || cp == 0x06FF) return true;
        // Devanagari
        if ((cp >= 0x0904 && cp <= 0x0939) || cp == 0x093D ||
            (cp >= 0x0958 && cp <= 0x0961) || (cp >= 0x0972 && cp <= 0x097F)) return true;
        // Bengali
        if ((cp >= 0x0985 && cp <= 0x098C) || (cp >= 0x098F && cp <= 0x0990) ||
            (cp >= 0x0993 && cp <= 0x09A8) || (cp >= 0x09AA && cp <= 0x09B0) ||
            cp == 0x09B2 || (cp >= 0x09B6 && cp <= 0x09B9)) return true;
        // Thai
        if (cp >= 0x0E01 && cp <= 0x0E3A) return true;
        if (cp >= 0x0E40 && cp <= 0x0E4E) return true;
        // Georgian
        if ((cp >= 0x10A0 && cp <= 0x10C5) || cp == 0x10C7 || cp == 0x10CD ||
            (cp >= 0x10D0 && cp <= 0x10FA) || (cp >= 0x10FC && cp <= 0x10FF)) return true;
        // Hangul Jamo
        if (cp >= 0x1100 && cp <= 0x11FF) return true;
        // Latin Extended Additional
        if (cp >= 0x1E00 && cp <= 0x1EFF) return true;
        // Greek Extended
        if (cp >= 0x1F00 && cp <= 0x1FFF) return true;
        // Letterlike Symbols
        if (cp >= 0x2100 && cp <= 0x214F) return true;
        // CJK Unified Ideographs
        if (cp >= 0x4E00 && cp <= 0x9FFF) return true;
        // Hangul Syllables
        if (cp >= 0xAC00 && cp <= 0xD7A3) return true;
        // CJK Compatibility Ideographs
        if (cp >= 0xF900 && cp <= 0xFAFF) return true;
        // Hiragana
        if (cp >= 0x3040 && cp <= 0x309F) return true;
        // Katakana
        if (cp >= 0x30A0 && cp <= 0x30FF) return true;
        // Bopomofo
        if (cp >= 0x3100 && cp <= 0x312F) return true;
        // CJK Extension A
        if (cp >= 0x3400 && cp <= 0x4DBF) return true;
        // CJK Extension B
        if (cp >= 0x20000 && cp <= 0x2A6DF) return true;

        return false;
    }

    bool IsNumeric(uint32_t cp) {
        // ASCII fast path
        if (cp >= '0' && cp <= '9') return true;
        // Arabic-Indic digits
        if (cp >= 0x0660 && cp <= 0x0669) return true;
        // Extended Arabic-Indic digits
        if (cp >= 0x06F0 && cp <= 0x06F9) return true;
        // Devanagari digits
        if (cp >= 0x0966 && cp <= 0x096F) return true;
        // Bengali digits
        if (cp >= 0x09E6 && cp <= 0x09EF) return true;
        // Gurmukhi digits
        if (cp >= 0x0A66 && cp <= 0x0A6F) return true;
        // Gujarati digits
        if (cp >= 0x0AE6 && cp <= 0x0AEF) return true;
        // Oriya digits
        if (cp >= 0x0B66 && cp <= 0x0B6F) return true;
        // Tamil digits
        if (cp >= 0x0BE6 && cp <= 0x0BEF) return true;
        // Telugu digits
        if (cp >= 0x0C66 && cp <= 0x0C6F) return true;
        // Kannada digits
        if (cp >= 0x0CE6 && cp <= 0x0CEF) return true;
        // Malayalam digits
        if (cp >= 0x0D66 && cp <= 0x0D6F) return true;
        // Thai digits
        if (cp >= 0x0E50 && cp <= 0x0E59) return true;
        // Lao digits
        if (cp >= 0x0ED0 && cp <= 0x0ED9) return true;
        // Tibetan digits
        if (cp >= 0x0F20 && cp <= 0x0F29) return true;
        // Fullwidth digits
        if (cp >= 0xFF10 && cp <= 0xFF19) return true;

        return false;
    }

    bool IsAlphanumeric(uint32_t cp) {
        return IsAlphabetic(cp) || IsNumeric(cp);
    }

    bool IsWhitespace(uint32_t cp) {
        // ASCII whitespace
        if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' ||
            cp == '\f' || cp == '\v') return true;
        // Unicode whitespace
        if (cp == 0x00A0) return true;   // NBSP
        if (cp >= 0x2000 && cp <= 0x200A) return true;  // En/Em spaces
        if (cp == 0x200B) return true;   // ZWSP
        if (cp == 0x2028) return true;   // Line Separator
        if (cp == 0x2029) return true;   // Paragraph Separator
        if (cp == 0x202F) return true;   // Narrow NBSP
        if (cp == 0x205F) return true;   // Medium Mathematical Space
        if (cp == 0x3000) return true;   // Ideographic Space
        if (cp == 0xFEFF) return true;   // BOM / ZWNBSP
        return false;
    }

    bool IsPunctuation(uint32_t cp) {
        // ASCII punctuation
        if ((cp >= 0x21 && cp <= 0x2F) || (cp >= 0x3A && cp <= 0x40) ||
            (cp >= 0x5B && cp <= 0x60) || (cp >= 0x7B && cp <= 0x7E)) return true;
        // Latin-1 Supplement punctuation
        if (cp == 0x00A1 || cp == 0x00A7 || cp == 0x00AB || cp == 0x00B6 ||
            cp == 0x00B7 || cp == 0x00BB || cp == 0x00BF) return true;
        // General Punctuation
        if (cp >= 0x2010 && cp <= 0x2027) return true;
        if (cp >= 0x2030 && cp <= 0x205E) return true;
        // CJK punctuation
        if (cp >= 0x3001 && cp <= 0x3003) return true;
        if (cp >= 0x3008 && cp <= 0x3011) return true;
        if (cp >= 0x3014 && cp <= 0x301F) return true;
        if (cp == 0x3030) return true;
        // Fullwidth punctuation
        if (cp >= 0xFF01 && cp <= 0xFF0F) return true;
        if (cp >= 0xFF1A && cp <= 0xFF20) return true;
        if (cp >= 0xFF3B && cp <= 0xFF40) return true;
        if (cp >= 0xFF5B && cp <= 0xFF65) return true;
        // Supplemental punctuation
        if (cp >= 0x2E00 && cp <= 0x2E4F) return true;

        return false;
    }

    bool IsUppercase(uint32_t cp) {
        // ASCII fast path
        if (cp >= 'A' && cp <= 'Z') return true;
        // Latin Extended uppercase ranges
        if ((cp >= 0x00C0 && cp <= 0x00D6) || (cp >= 0x00D8 && cp <= 0x00DE)) return true;
        // Latin Extended-A (even codepoints are often uppercase)
        if (cp >= 0x0100 && cp <= 0x012E && (cp % 2 == 0)) return true;
        if (cp >= 0x0130 && cp <= 0x0136 && (cp % 2 == 0)) return true;
        if (cp >= 0x0139 && cp <= 0x0147 && (cp % 2 == 1)) return true;
        if (cp >= 0x014A && cp <= 0x0176 && (cp % 2 == 0)) return true;
        if (cp == 0x0178 || cp == 0x0179 || cp == 0x017B || cp == 0x017D) return true;
        // Greek uppercase
        if ((cp >= 0x0388 && cp <= 0x038A) || cp == 0x038C ||
            (cp >= 0x038E && cp <= 0x038F) || (cp >= 0x0391 && cp <= 0x03A1) ||
            (cp >= 0x03A3 && cp <= 0x03AB)) return true;
        // Cyrillic uppercase
        if (cp >= 0x0410 && cp <= 0x042F) return true;
        if (cp >= 0x0400 && cp <= 0x040F) return true;  // Cyrillic supplement uppercase
        // Armenian uppercase
        if (cp >= 0x0531 && cp <= 0x0556) return true;
        // Georgian Mkhedruli uppercase (Mtavruli)
        if (cp >= 0x1C90 && cp <= 0x1CBA) return true;
        // Latin Extended Additional uppercase
        if (cp >= 0x1E00 && cp <= 0x1EFF && (cp % 2 == 0)) return true;
        // Greek Extended uppercase
        if ((cp >= 0x1F08 && cp <= 0x1F0F) || (cp >= 0x1F18 && cp <= 0x1F1D) ||
            (cp >= 0x1F28 && cp <= 0x1F2F) || (cp >= 0x1F38 && cp <= 0x1F3F) ||
            (cp >= 0x1F48 && cp <= 0x1F4D) || (cp >= 0x1F59 && cp <= 0x1F5F) ||
            (cp >= 0x1F68 && cp <= 0x1F6F) || (cp >= 0x1FB8 && cp <= 0x1FBB) ||
            (cp >= 0x1FC8 && cp <= 0x1FCB) || (cp >= 0x1FD8 && cp <= 0x1FDB) ||
            (cp >= 0x1FE8 && cp <= 0x1FEC) || (cp >= 0x1FF8 && cp <= 0x1FFB)) return true;
        // Fullwidth Latin uppercase
        if (cp >= 0xFF21 && cp <= 0xFF3A) return true;

        return false;
    }

    bool IsLowercase(uint32_t cp) {
        // ASCII fast path
        if (cp >= 'a' && cp <= 'z') return true;
        // Latin-1 Supplement lowercase
        if ((cp >= 0x00DF && cp <= 0x00F6) || (cp >= 0x00F8 && cp <= 0x00FF)) return true;
        // Latin Extended-A (odd codepoints are often lowercase)
        if (cp >= 0x0101 && cp <= 0x012F && (cp % 2 == 1)) return true;
        if (cp >= 0x0131 && cp <= 0x0137 && (cp % 2 == 1)) return true;
        if (cp >= 0x013A && cp <= 0x0148 && (cp % 2 == 0)) return true;
        if (cp >= 0x014B && cp <= 0x0177 && (cp % 2 == 1)) return true;
        if (cp == 0x017A || cp == 0x017C || cp == 0x017E) return true;
        // Greek lowercase
        if ((cp >= 0x03AC && cp <= 0x03CE) || (cp >= 0x03D0 && cp <= 0x03D7) ||
            (cp >= 0x03D9 && cp <= 0x03EF && (cp % 2 == 1))) return true;
        // Cyrillic lowercase
        if (cp >= 0x0430 && cp <= 0x044F) return true;
        if (cp >= 0x0450 && cp <= 0x045F) return true;  // Cyrillic supplement lowercase
        // Armenian lowercase
        if (cp >= 0x0561 && cp <= 0x0587) return true;
        // Georgian lowercase (Mkhedruli)
        if (cp >= 0x10D0 && cp <= 0x10FA) return true;
        // Latin Extended Additional lowercase
        if (cp >= 0x1E01 && cp <= 0x1EFF && (cp % 2 == 1)) return true;
        // Fullwidth Latin lowercase
        if (cp >= 0xFF41 && cp <= 0xFF5A) return true;

        return false;
    }

} // namespace Unicode

// ===== GRAPHEME CLUSTER IMPLEMENTATION =====
// Full Unicode compliance via libgrapheme

namespace Grapheme {

    size_t NextGraphemeBoundary(const std::string& str, size_t bytePos) {
        if (bytePos >= str.size()) return str.size();
        size_t bytesInCluster = grapheme_next_character_break_utf8(
            str.c_str() + bytePos, str.size() - bytePos);
        return bytePos + bytesInCluster;
    }

    size_t PrevGraphemeBoundary(const std::string& str, size_t bytePos) {
        if (bytePos == 0 || str.empty()) return 0;
        if (bytePos > str.size()) bytePos = str.size();

        // Scan forward from start, tracking grapheme boundaries
        size_t prevBoundary = 0;
        size_t pos = 0;
        while (pos < bytePos) {
            size_t next = pos + grapheme_next_character_break_utf8(
                str.c_str() + pos, str.size() - pos);
            if (next >= bytePos) break;
            prevBoundary = next;
            pos = next;
        }
        // If bytePos is at or past the last boundary we found, return it
        // If pos < bytePos, then prevBoundary is the start of the current cluster
        if (pos == 0 && bytePos > 0) return 0;
        return (pos < bytePos) ? pos : prevBoundary;
    }

    size_t CountGraphemes(const std::string& str) {
        if (str.empty()) return 0;

        size_t count = 0;
        size_t pos = 0;
        while (pos < str.size()) {
            pos += grapheme_next_character_break_utf8(
                str.c_str() + pos, str.size() - pos);
            count++;
        }
        return count;
    }
    
    int CountWords(const std::string& text) {
        if (text.empty()) return 0;

        int wordCount = 0;
        size_t pos = 0;
        size_t textLen = text.size();

        while (pos < textLen) {
            // Find next word boundary
            size_t nextBoundary = Grapheme::NextWordBoundary(text, pos);
            if (nextBoundary == pos || nextBoundary > textLen) {
                break;
            }

            // Extract the segment between boundaries
            std::string segment = text.substr(pos, nextBoundary - pos);

            // Check if this segment contains any non-whitespace graphemes
            // (word boundaries include whitespace segments)
            bool hasContent = false;
            size_t segPos = 0;
            while (segPos < segment.size()) {
                size_t nextGrapheme = Grapheme::NextGraphemeBoundary(segment, segPos);
                if (nextGrapheme == segPos) {
                    nextGrapheme = segPos + 1; // Fallback for malformed
                }

                // Get the first codepoint of this grapheme to check if whitespace
                auto it = segment.begin() + segPos;
                uint32_t codepoint = UTF8::DecodeCodepoint(it, segment.end());

                if (!Unicode::IsWhitespace(codepoint)) {
                    hasContent = true;
                    break;
                }

                segPos = nextGrapheme;
            }

            if (hasContent) {
                wordCount++;
            }

            pos = nextBoundary;
        }

        return wordCount;
    }

    std::pair<size_t, size_t> GetGraphemeAt(const std::string& str, size_t graphemeIndex) {
        if (str.empty()) return {0, 0};

        size_t currentGrapheme = 0;
        size_t pos = 0;

        while (pos < str.size()) {
            size_t clusterLen = grapheme_next_character_break_utf8(
                str.c_str() + pos, str.size() - pos);

            if (currentGrapheme == graphemeIndex) {
                return {pos, pos + clusterLen};
            }

            pos += clusterLen;
            currentGrapheme++;
        }

        // Index out of bounds
        return {str.size(), str.size()};
    }

    // ===== WORD BOUNDARY NAVIGATION =====

    size_t NextWordBoundary(const std::string& str, size_t bytePos) {
        if (bytePos >= str.size()) return str.size();
        size_t bytesInWord = grapheme_next_word_break_utf8(
            str.c_str() + bytePos, str.size() - bytePos);
        return bytePos + bytesInWord;
    }

    size_t PrevWordBoundary(const std::string& str, size_t bytePos) {
        if (bytePos == 0 || str.empty()) return 0;
        if (bytePos > str.size()) bytePos = str.size();

        // Scan forward from start, tracking word boundaries
        size_t prevBoundary = 0;
        size_t pos = 0;
        while (pos < bytePos) {
            size_t next = pos + grapheme_next_word_break_utf8(
                str.c_str() + pos, str.size() - pos);
            if (next >= bytePos) break;
            prevBoundary = next;
            pos = next;
        }
        if (pos == 0 && bytePos > 0) return 0;
        return (pos < bytePos) ? pos : prevBoundary;
    }

    // ===== SENTENCE BOUNDARY NAVIGATION =====

    size_t NextSentenceBoundary(const std::string& str, size_t bytePos) {
        if (bytePos >= str.size()) return str.size();
        size_t bytesInSentence = grapheme_next_sentence_break_utf8(
            str.c_str() + bytePos, str.size() - bytePos);
        return bytePos + bytesInSentence;
    }

    size_t PrevSentenceBoundary(const std::string& str, size_t bytePos) {
        if (bytePos == 0 || str.empty()) return 0;
        if (bytePos > str.size()) bytePos = str.size();

        size_t prevBoundary = 0;
        size_t pos = 0;
        while (pos < bytePos) {
            size_t next = pos + grapheme_next_sentence_break_utf8(
                str.c_str() + pos, str.size() - pos);
            if (next >= bytePos) break;
            prevBoundary = next;
            pos = next;
        }
        if (pos == 0 && bytePos > 0) return 0;
        return (pos < bytePos) ? pos : prevBoundary;
    }

    // ===== LINE BREAK NAVIGATION =====

    size_t NextLineBreak(const std::string& str, size_t bytePos) {
        if (bytePos >= str.size()) return str.size();
        size_t bytesInSegment = grapheme_next_line_break_utf8(
            str.c_str() + bytePos, str.size() - bytePos);
        return bytePos + bytesInSegment;
    }

    size_t PrevLineBreak(const std::string& str, size_t bytePos) {
        if (bytePos == 0 || str.empty()) return 0;
        if (bytePos > str.size()) bytePos = str.size();

        size_t prevBoundary = 0;
        size_t pos = 0;
        while (pos < bytePos) {
            size_t next = pos + grapheme_next_line_break_utf8(
                str.c_str() + pos, str.size() - pos);
            if (next >= bytePos) break;
            prevBoundary = next;
            pos = next;
        }
        if (pos == 0 && bytePos > 0) return 0;
        return (pos < bytePos) ? pos : prevBoundary;
    }

} // namespace Grapheme

// ===== GRAPHEME REFERENCE IMPLEMENTATION =====

GraphemeRef::GraphemeRef(UCString* str, size_t index)
    : owner(str), graphemeIndex(index) {}

std::string GraphemeRef::ToString() const {
    if (!owner) return "";
    auto [start, end] = Grapheme::GetGraphemeAt(owner->data, graphemeIndex);
    return owner->data.substr(start, end - start);
}

uint32_t GraphemeRef::ToCodepoint() const {
    if (!owner) return 0;
    auto [start, end] = Grapheme::GetGraphemeAt(owner->data, graphemeIndex);
    if (start >= end) return 0;
    auto it = owner->data.begin() + start;
    auto endIt = owner->data.begin() + end;
    return UTF8::DecodeCodepoint(it, endIt);
}

bool GraphemeRef::IsAlpha() const { return Unicode::IsAlphabetic(ToCodepoint()); }
bool GraphemeRef::IsDigit() const { return Unicode::IsNumeric(ToCodepoint()); }
bool GraphemeRef::IsAlnum() const { return Unicode::IsAlphanumeric(ToCodepoint()); }
bool GraphemeRef::IsSpace() const { return Unicode::IsWhitespace(ToCodepoint()); }
bool GraphemeRef::IsPunct() const { return Unicode::IsPunctuation(ToCodepoint()); }
bool GraphemeRef::IsUpper() const { return Unicode::IsUppercase(ToCodepoint()); }
bool GraphemeRef::IsLower() const { return Unicode::IsLowercase(ToCodepoint()); }

bool GraphemeRef::operator==(const GraphemeRef& other) const {
    return ToString() == other.ToString();
}

bool GraphemeRef::operator==(const std::string& other) const {
    return ToString() == other;
}

bool GraphemeRef::operator==(const char* other) const {
    return ToString() == other;
}

bool GraphemeRef::operator==(char32_t codepoint) const {
    return ToCodepoint() == codepoint;
}

GraphemeRef& GraphemeRef::operator=(const std::string& str) {
    if (!owner) return *this;
    auto [start, end] = Grapheme::GetGraphemeAt(owner->data, graphemeIndex);
    owner->data.replace(start, end - start, str);
    owner->InvalidateCache();
    return *this;
}

GraphemeRef& GraphemeRef::operator=(const char* str) {
    return operator=(std::string(str ? str : ""));
}

GraphemeRef& GraphemeRef::operator=(char32_t codepoint) {
    return operator=(UTF8::EncodeCodepoint(codepoint));
}

// ===== CONST GRAPHEME REFERENCE IMPLEMENTATION =====

ConstGraphemeRef::ConstGraphemeRef(const UCString* str, size_t index)
    : owner(str), graphemeIndex(index) {}

std::string ConstGraphemeRef::ToString() const {
    if (!owner) return "";
    auto [start, end] = Grapheme::GetGraphemeAt(owner->data, graphemeIndex);
    return owner->data.substr(start, end - start);
}

uint32_t ConstGraphemeRef::ToCodepoint() const {
    if (!owner) return 0;
    auto [start, end] = Grapheme::GetGraphemeAt(owner->data, graphemeIndex);
    if (start >= end) return 0;
    auto it = owner->data.begin() + start;
    auto endIt = owner->data.begin() + end;
    return UTF8::DecodeCodepoint(it, endIt);
}

bool ConstGraphemeRef::IsAlpha() const { return Unicode::IsAlphabetic(ToCodepoint()); }
bool ConstGraphemeRef::IsDigit() const { return Unicode::IsNumeric(ToCodepoint()); }
bool ConstGraphemeRef::IsAlnum() const { return Unicode::IsAlphanumeric(ToCodepoint()); }
bool ConstGraphemeRef::IsSpace() const { return Unicode::IsWhitespace(ToCodepoint()); }
bool ConstGraphemeRef::IsPunct() const { return Unicode::IsPunctuation(ToCodepoint()); }
bool ConstGraphemeRef::IsUpper() const { return Unicode::IsUppercase(ToCodepoint()); }
bool ConstGraphemeRef::IsLower() const { return Unicode::IsLowercase(ToCodepoint()); }

bool ConstGraphemeRef::operator==(const ConstGraphemeRef& other) const {
    return ToString() == other.ToString();
}

bool ConstGraphemeRef::operator==(const std::string& other) const {
    return ToString() == other;
}

bool ConstGraphemeRef::operator==(const char* other) const {
    return ToString() == other;
}

bool ConstGraphemeRef::operator==(char32_t codepoint) const {
    return ToCodepoint() == codepoint;
}

// ===== ITERATOR IMPLEMENTATIONS =====

GraphemeRef UCStringIterator::operator*() {
    return GraphemeRef(owner, graphemeIndex);
}

UCStringIterator& UCStringIterator::operator++() {
    ++graphemeIndex;
    return *this;
}

UCStringIterator UCStringIterator::operator++(int) {
    UCStringIterator tmp = *this;
    ++graphemeIndex;
    return tmp;
}

UCStringIterator& UCStringIterator::operator--() {
    if (graphemeIndex > 0) --graphemeIndex;
    return *this;
}

UCStringIterator UCStringIterator::operator--(int) {
    UCStringIterator tmp = *this;
    if (graphemeIndex > 0) --graphemeIndex;
    return tmp;
}

ConstGraphemeRef UCStringConstIterator::operator*() const {
    return ConstGraphemeRef(owner, graphemeIndex);
}

UCStringConstIterator& UCStringConstIterator::operator++() {
    ++graphemeIndex;
    return *this;
}

UCStringConstIterator UCStringConstIterator::operator++(int) {
    UCStringConstIterator tmp = *this;
    ++graphemeIndex;
    return tmp;
}

UCStringConstIterator& UCStringConstIterator::operator--() {
    if (graphemeIndex > 0) --graphemeIndex;
    return *this;
}

UCStringConstIterator UCStringConstIterator::operator--(int) {
    UCStringConstIterator tmp = *this;
    if (graphemeIndex > 0) --graphemeIndex;
    return tmp;
}

// ===== UCSTRING IMPLEMENTATION =====

// Constructor from repeated codepoint
UCString::UCString(size_t count, char32_t codepoint) {
    std::string encoded = UTF8::EncodeCodepoint(codepoint);
    data.reserve(encoded.size() * count);
    for (size_t i = 0; i < count; ++i) {
        data += encoded;
    }
}

// Constructor from char32_t string
UCString::UCString(const char32_t* str) {
    if (!str) return;
    while (*str) {
        data += UTF8::EncodeCodepoint(*str);
        ++str;
    }
}

// Assignment operators
UCString& UCString::operator=(const UCString& other) {
    if (this != &other) {
        data = other.data;
        InvalidateCache();
    }
    return *this;
}

UCString& UCString::operator=(UCString&& other) noexcept {
    if (this != &other) {
        data = std::move(other.data);
        InvalidateCache();
    }
    return *this;
}

UCString& UCString::operator=(const std::string& str) {
    data = str;
    InvalidateCache();
    return *this;
}

UCString& UCString::operator=(std::string&& str) noexcept {
    data = std::move(str);
    InvalidateCache();
    return *this;
}

UCString& UCString::operator=(const char* str) {
    data = str ? str : "";
    InvalidateCache();
    return *this;
}

UCString& UCString::operator=(std::string_view sv) {
    data = sv;
    InvalidateCache();
    return *this;
}

UCString& UCString::operator=(char32_t codepoint) {
    data = UTF8::EncodeCodepoint(codepoint);
    InvalidateCache();
    return *this;
}

// Length (grapheme count)
size_t UCString::Length() const {
    if (!graphemeCountValid) {
        cachedGraphemeCount = Grapheme::CountGraphemes(data);
        graphemeCountValid = true;
    }
    return cachedGraphemeCount;
}

// Element access
GraphemeRef UCString::operator[](size_t graphemeIndex) {
    return GraphemeRef(this, graphemeIndex);
}

ConstGraphemeRef UCString::operator[](size_t graphemeIndex) const {
    return ConstGraphemeRef(this, graphemeIndex);
}

GraphemeRef UCString::At(size_t graphemeIndex) {
    if (graphemeIndex >= Length()) {
        throw std::out_of_range("UCString::At: grapheme index out of range");
    }
    return GraphemeRef(this, graphemeIndex);
}

ConstGraphemeRef UCString::At(size_t graphemeIndex) const {
    if (graphemeIndex >= Length()) {
        throw std::out_of_range("UCString::At: grapheme index out of range");
    }
    return ConstGraphemeRef(this, graphemeIndex);
}

std::string UCString::GetGrapheme(size_t graphemeIndex) const {
    auto [start, end] = Grapheme::GetGraphemeAt(data, graphemeIndex);
    return data.substr(start, end - start);
}

uint32_t UCString::GetCodepoint(size_t codepointIndex) const {
    size_t bytePos = CodepointToByteOffset(codepointIndex);
    if (bytePos >= data.size()) return 0;
    auto it = data.begin() + bytePos;
    return UTF8::DecodeCodepoint(it, data.end());
}

std::string UCString::Front() const {
    if (data.empty()) return "";
    return GetGrapheme(0);
}

std::string UCString::Back() const {
    if (data.empty()) return "";
    size_t len = Length();
    return len > 0 ? GetGrapheme(len - 1) : "";
}

// Position conversion
size_t UCString::GraphemeToByteOffset(size_t graphemeIndex) const {
    auto [start, end] = Grapheme::GetGraphemeAt(data, graphemeIndex);
    return start;
}

size_t UCString::ByteToGraphemeIndex(size_t byteOffset) const {
    if (byteOffset == 0) return 0;
    if (byteOffset >= data.size()) return Length();

    size_t graphemeCount = 0;
    size_t pos = 0;

    while (pos < byteOffset && pos < data.size()) {
        pos = Grapheme::NextGraphemeBoundary(data, pos);
        if (pos <= byteOffset) graphemeCount++;
    }

    return graphemeCount;
}

size_t UCString::CodepointToByteOffset(size_t codepointIndex) const {
    size_t bytePos = 0;
    for (size_t i = 0; i < codepointIndex && bytePos < data.size(); ++i) {
        bytePos += UTF8::SequenceLength(static_cast<uint8_t>(data[bytePos]));
    }
    return bytePos;
}

size_t UCString::ByteToCodepointIndex(size_t byteOffset) const {
    size_t codepointCount = 0;
    size_t pos = 0;
    while (pos < byteOffset && pos < data.size()) {
        pos += UTF8::SequenceLength(static_cast<uint8_t>(data[pos]));
        ++codepointCount;
    }
    return codepointCount;
}

// Conversion
std::u32string UCString::ToUTF32() const {
    std::u32string result;
    auto it = data.begin();
    while (it != data.end()) {
        result += UTF8::DecodeCodepoint(it, data.end());
    }
    return result;
}

UCString UCString::FromUTF32(const std::u32string& str) {
    UCString result;
    for (char32_t cp : str) {
        result.data += UTF8::EncodeCodepoint(cp);
    }
    return result;
}

UCString UCString::FromUTF32(const char32_t* str) {
    UCString result;
    if (str) {
        while (*str) {
            result.data += UTF8::EncodeCodepoint(*str++);
        }
    }
    return result;
}

// Append operations
UCString& UCString::Append(const UCString& str) {
    data += str.data;
    InvalidateCache();
    return *this;
}

UCString& UCString::Append(const std::string& str) {
    data += str;
    InvalidateCache();
    return *this;
}

UCString& UCString::Append(const char* str) {
    if (str) data += str;
    InvalidateCache();
    return *this;
}

UCString& UCString::Append(std::string_view sv) {
    data += sv;
    InvalidateCache();
    return *this;
}

UCString& UCString::Append(char32_t codepoint) {
    data += UTF8::EncodeCodepoint(codepoint);
    InvalidateCache();
    return *this;
}

UCString& UCString::Append(size_t count, char32_t codepoint) {
    std::string encoded = UTF8::EncodeCodepoint(codepoint);
    for (size_t i = 0; i < count; ++i) {
        data += encoded;
    }
    InvalidateCache();
    return *this;
}

// Insert operations
UCString& UCString::Insert(size_t graphemePos, const UCString& str) {
    size_t bytePos = GraphemeToByteOffset(graphemePos);
    data.insert(bytePos, str.data);
    InvalidateCache();
    return *this;
}

UCString& UCString::Insert(size_t graphemePos, const std::string& str) {
    size_t bytePos = GraphemeToByteOffset(graphemePos);
    data.insert(bytePos, str);
    InvalidateCache();
    return *this;
}

UCString& UCString::Insert(size_t graphemePos, const char* str) {
    if (str) {
        size_t bytePos = GraphemeToByteOffset(graphemePos);
        data.insert(bytePos, str);
        InvalidateCache();
    }
    return *this;
}

UCString& UCString::Insert(size_t graphemePos, char32_t codepoint) {
    size_t bytePos = GraphemeToByteOffset(graphemePos);
    data.insert(bytePos, UTF8::EncodeCodepoint(codepoint));
    InvalidateCache();
    return *this;
}

UCString& UCString::InsertRaw(size_t bytePos, const std::string& str) {
    data.insert(bytePos, str);
    InvalidateCache();
    return *this;
}

// Erase operations
UCString& UCString::Erase(size_t graphemePos, size_t graphemeCount) {
    if (graphemeCount == 0) return *this;

    auto [start, _] = Grapheme::GetGraphemeAt(data, graphemePos);
    
    size_t endPos = start;
    for (size_t i = 0; i < graphemeCount && endPos < data.size(); ++i) {
        endPos = Grapheme::NextGraphemeBoundary(data, endPos);
    }

    data.erase(start, endPos - start);
    InvalidateCache();
    return *this;
}

UCString& UCString::EraseRaw(size_t bytePos, size_t byteCount) {
    data.erase(bytePos, byteCount);
    InvalidateCache();
    return *this;
}

// Replace operations
UCString& UCString::Replace(size_t graphemePos, size_t graphemeCount, const UCString& str) {
    Erase(graphemePos, graphemeCount);
    Insert(graphemePos, str);
    return *this;
}

UCString& UCString::Replace(size_t graphemePos, size_t graphemeCount, const std::string& str) {
    Erase(graphemePos, graphemeCount);
    Insert(graphemePos, str);
    return *this;
}

UCString& UCString::Replace(size_t graphemePos, size_t graphemeCount, const char* str) {
    Erase(graphemePos, graphemeCount);
    Insert(graphemePos, str);
    return *this;
}

void UCString::PopBack() {
    if (data.empty()) return;
    size_t len = Length();
    if (len > 0) {
        Erase(len - 1, 1);
    }
}

// Substring
UCString UCString::Substr(size_t graphemePos, size_t graphemeCount) const {
    if (graphemePos >= Length()) return UCString();

    auto [start, _] = Grapheme::GetGraphemeAt(data, graphemePos);
    
    if (graphemeCount == npos) {
        return UCString(data.substr(start));
    }

    size_t endPos = start;
    for (size_t i = 0; i < graphemeCount && endPos < data.size(); ++i) {
        endPos = Grapheme::NextGraphemeBoundary(data, endPos);
    }

    return UCString(data.substr(start, endPos - start));
}

UCString UCString::SubstrRaw(size_t bytePos, size_t byteCount) const {
    return UCString(data.substr(bytePos, byteCount));
}

// Search operations
size_t UCString::Find(const UCString& str, size_t startGraphemePos) const {
    return Find(str.data, startGraphemePos);
}

size_t UCString::Find(const std::string& str, size_t startGraphemePos) const {
    size_t byteStart = GraphemeToByteOffset(startGraphemePos);
    size_t bytePos = data.find(str, byteStart);
    if (bytePos == npos) return npos;
    return ByteToGraphemeIndex(bytePos);
}

size_t UCString::Find(const char* str, size_t startGraphemePos) const {
    if (!str) return npos;
    return Find(std::string(str), startGraphemePos);
}

size_t UCString::Find(char32_t codepoint, size_t startGraphemePos) const {
    return Find(UTF8::EncodeCodepoint(codepoint), startGraphemePos);
}

size_t UCString::RFind(const UCString& str, size_t startGraphemePos) const {
    return RFind(str.data, startGraphemePos);
}

size_t UCString::RFind(const std::string& str, size_t startGraphemePos) const {
    size_t byteStart = (startGraphemePos == npos) ? npos : GraphemeToByteOffset(startGraphemePos);
    size_t bytePos = data.rfind(str, byteStart);
    if (bytePos == npos) return npos;
    return ByteToGraphemeIndex(bytePos);
}

size_t UCString::RFind(const char* str, size_t startGraphemePos) const {
    if (!str) return npos;
    return RFind(std::string(str), startGraphemePos);
}

// Prefix/Suffix checks
bool UCString::StartsWith(const UCString& prefix) const {
    return StartsWith(prefix.data);
}

bool UCString::StartsWith(const std::string& prefix) const {
    if (prefix.size() > data.size()) return false;
    return data.compare(0, prefix.size(), prefix) == 0;
}

bool UCString::StartsWith(const char* prefix) const {
    if (!prefix) return true;
    return StartsWith(std::string(prefix));
}

bool UCString::EndsWith(const UCString& suffix) const {
    return EndsWith(suffix.data);
}

bool UCString::EndsWith(const std::string& suffix) const {
    if (suffix.size() > data.size()) return false;
    return data.compare(data.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool UCString::EndsWith(const char* suffix) const {
    if (!suffix) return true;
    return EndsWith(std::string(suffix));
}

// Sanitization
UCString& UCString::Sanitize() {
    std::string result;
    result.reserve(data.size());

    auto it = data.begin();
    while (it != data.end()) {
        uint8_t lead = static_cast<uint8_t>(*it);
        size_t len = UTF8::SequenceLength(lead);

        if (std::distance(it, data.end()) >= static_cast<ptrdiff_t>(len)) {
            bool valid = true;
            auto checkIt = it;
            ++checkIt;
            for (size_t i = 1; i < len && valid; ++i, ++checkIt) {
                if (!UTF8::IsContinuation(static_cast<uint8_t>(*checkIt))) {
                    valid = false;
                }
            }

            if (valid) {
                result.append(it, it + len);
                it += len;
            } else {
                result += "\xEF\xBF\xBD";  // Replacement character
                ++it;
            }
        } else {
            result += "\xEF\xBF\xBD";
            ++it;
        }
    }

    data = std::move(result);
    InvalidateCache();
    return *this;
}

UCString UCString::Sanitized(const std::string& str) {
    UCString result(str);
    result.Sanitize();
    return result;
}

// Split
std::vector<UCString> UCString::Split(const UCString& delimiter) const {
    return Split(delimiter.data);
}

std::vector<UCString> UCString::Split(const char* delimiter) const {
    return Split(std::string(delimiter ? delimiter : ""));
}

std::vector<UCString> UCString::Split(const std::string& delimiter) const {
    std::vector<UCString> result;
    if (delimiter.empty()) {
        // Split into graphemes
        for (size_t i = 0; i < Length(); ++i) {
            result.push_back(GetGrapheme(i));
        }
        return result;
    }

    size_t start = 0;
    size_t pos = data.find(delimiter);
    while (pos != npos) {
        result.push_back(data.substr(start, pos - start));
        start = pos + delimiter.size();
        pos = data.find(delimiter, start);
    }
    result.push_back(data.substr(start));
    return result;
}

std::vector<UCString> UCString::Split(char32_t delimiter) const {
    return Split(UTF8::EncodeCodepoint(delimiter));
}

// Join
UCString UCString::Join(const std::vector<UCString>& parts, const UCString& separator) {
    UCString result;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) result.Append(separator);
        result.Append(parts[i]);
    }
    return result;
}

// Trim helpers — uses Unicode::IsWhitespace for full Unicode support
UCString& UCString::TrimLeft() {
    size_t pos = 0;
    auto it = data.begin();
    while (it != data.end()) {
        auto tempIt = it;
        uint32_t cp = UTF8::DecodeCodepoint(tempIt, data.end());
        if (!Unicode::IsWhitespace(cp)) break;
        pos = tempIt - data.begin();
        it = tempIt;
    }
    if (pos > 0) {
        data.erase(0, pos);
        InvalidateCache();
    }
    return *this;
}

UCString& UCString::TrimRight() {
    while (!data.empty()) {
        size_t lastBoundary = Grapheme::PrevGraphemeBoundary(data, data.size());
        auto it = data.begin() + lastBoundary;
        uint32_t cp = UTF8::DecodeCodepoint(it, data.end());
        if (!Unicode::IsWhitespace(cp)) break;
        data.erase(lastBoundary);
        InvalidateCache();
    }
    return *this;
}

UCString& UCString::Trim() {
    TrimLeft();
    TrimRight();
    return *this;
}

UCString UCString::Trimmed() const {
    UCString result(*this);
    result.Trim();
    return result;
}

UCString UCString::TrimmedLeft() const {
    UCString result(*this);
    result.TrimLeft();
    return result;
}

UCString UCString::TrimmedRight() const {
    UCString result(*this);
    result.TrimRight();
    return result;
}

// ===== CASE CONVERSION (Full Unicode via libgrapheme) =====

UCString UCString::ToLower() const {
    if (data.empty()) return UCString();

    // grapheme_to_lowercase_utf8 returns the number of bytes written,
    // even if dest is NULL (to query required size)
    size_t needed = grapheme_to_lowercase_utf8(data.c_str(), data.size(), nullptr, 0);

    std::string result(needed, '\0');
    grapheme_to_lowercase_utf8(data.c_str(), data.size(), result.data(), needed);

    return UCString(std::move(result));
}

UCString UCString::ToUpper() const {
    if (data.empty()) return UCString();

    size_t needed = grapheme_to_uppercase_utf8(data.c_str(), data.size(), nullptr, 0);

    std::string result(needed, '\0');
    grapheme_to_uppercase_utf8(data.c_str(), data.size(), result.data(), needed);

    return UCString(std::move(result));
}

UCString UCString::ToTitleCase() const {
    if (data.empty()) return UCString();

    size_t needed = grapheme_to_titlecase_utf8(data.c_str(), data.size(), nullptr, 0);

    std::string result(needed, '\0');
    grapheme_to_titlecase_utf8(data.c_str(), data.size(), result.data(), needed);

    return UCString(std::move(result));
}

// ===== CASE DETECTION (Full Unicode via libgrapheme) =====

bool UCString::IsLowerCase() const {
    if (data.empty()) return false;
    return grapheme_is_lowercase_utf8(data.c_str(), data.size(), nullptr);
}

bool UCString::IsUpperCase() const {
    if (data.empty()) return false;
    return grapheme_is_uppercase_utf8(data.c_str(), data.size(), nullptr);
}

bool UCString::IsTitleCase() const {
    if (data.empty()) return false;
    return grapheme_is_titlecase_utf8(data.c_str(), data.size(), nullptr);
}

// Reverse (grapheme-aware)
UCString UCString::Reversed() const {
    UCString result;
    size_t len = Length();
    for (size_t i = len; i > 0; --i) {
        result.Append(GetGrapheme(i - 1));
    }
    return result;
}

} // namespace UltraCanvas
