// UltraCanvasString.cpp
// UTF-8 aware string class implementation
// Version: 1.0.0
// Last Modified: 2026-01-30
// Author: UltraCanvas Framework

#include "UltraCanvasString.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace UltraCanvas {

// ===== GRAPHEME CLUSTER IMPLEMENTATION =====
// Simplified UAX #29 implementation for grapheme cluster boundaries

namespace Grapheme {

    // Codepoint ranges for break properties (simplified)
    BreakProperty GetBreakProperty(uint32_t cp) {
        // CR/LF
        if (cp == 0x000D) return BreakProperty::CR;
        if (cp == 0x000A) return BreakProperty::LF;

        // Control characters
        if (cp <= 0x001F || (cp >= 0x007F && cp <= 0x009F)) return BreakProperty::Control;
        if (cp == 0x200B || cp == 0x200C) return BreakProperty::Control; // ZWSP, ZWNJ

        // ZWJ (Zero Width Joiner) - critical for emoji sequences
        if (cp == 0x200D) return BreakProperty::ZWJ;

        // Regional Indicators (flags)
        if (cp >= 0x1F1E6 && cp <= 0x1F1FF) return BreakProperty::Regional_Indicator;

        // Extend characters (combining marks, variation selectors, etc.)
        // General Category: Mn, Mc, Me
        if ((cp >= 0x0300 && cp <= 0x036F) ||   // Combining Diacritical Marks
            (cp >= 0x0483 && cp <= 0x0489) ||   // Cyrillic combining marks
            (cp >= 0x0591 && cp <= 0x05BD) ||   // Hebrew combining marks
            (cp >= 0x05BF && cp <= 0x05BF) ||
            (cp >= 0x05C1 && cp <= 0x05C2) ||
            (cp >= 0x05C4 && cp <= 0x05C5) ||
            (cp >= 0x05C7 && cp <= 0x05C7) ||
            (cp >= 0x0610 && cp <= 0x061A) ||   // Arabic combining marks
            (cp >= 0x064B && cp <= 0x065F) ||
            (cp >= 0x0670 && cp <= 0x0670) ||
            (cp >= 0x06D6 && cp <= 0x06DC) ||
            (cp >= 0x06DF && cp <= 0x06E4) ||
            (cp >= 0x06E7 && cp <= 0x06E8) ||
            (cp >= 0x06EA && cp <= 0x06ED) ||
            (cp >= 0x0711 && cp <= 0x0711) ||
            (cp >= 0x0730 && cp <= 0x074A) ||
            (cp >= 0x07A6 && cp <= 0x07B0) ||
            (cp >= 0x07EB && cp <= 0x07F3) ||
            (cp >= 0x0816 && cp <= 0x0819) ||
            (cp >= 0x081B && cp <= 0x0823) ||
            (cp >= 0x0825 && cp <= 0x0827) ||
            (cp >= 0x0829 && cp <= 0x082D) ||
            (cp >= 0x0859 && cp <= 0x085B) ||
            (cp >= 0x08D4 && cp <= 0x08E1) ||
            (cp >= 0x08E3 && cp <= 0x0903) ||   // Devanagari combining marks
            (cp >= 0x093A && cp <= 0x093C) ||
            (cp >= 0x093E && cp <= 0x094F) ||
            (cp >= 0x0951 && cp <= 0x0957) ||
            (cp >= 0x0962 && cp <= 0x0963) ||
            (cp >= 0x0981 && cp <= 0x0983) ||   // Bengali
            (cp >= 0x09BC && cp <= 0x09BC) ||
            (cp >= 0x09BE && cp <= 0x09C4) ||
            (cp >= 0x09C7 && cp <= 0x09C8) ||
            (cp >= 0x09CB && cp <= 0x09CD) ||
            (cp >= 0x09D7 && cp <= 0x09D7) ||
            (cp >= 0x09E2 && cp <= 0x09E3) ||
            (cp >= 0x0A01 && cp <= 0x0A03) ||   // Gurmukhi
            (cp >= 0x0A3C && cp <= 0x0A3C) ||
            (cp >= 0x0A3E && cp <= 0x0A42) ||
            (cp >= 0x0A47 && cp <= 0x0A48) ||
            (cp >= 0x0A4B && cp <= 0x0A4D) ||
            (cp >= 0x0A51 && cp <= 0x0A51) ||
            (cp >= 0x0A70 && cp <= 0x0A71) ||
            (cp >= 0x0A75 && cp <= 0x0A75) ||
            (cp >= 0x1AB0 && cp <= 0x1AFF) ||   // Combining Diacritical Marks Extended
            (cp >= 0x1DC0 && cp <= 0x1DFF) ||   // Combining Diacritical Marks Supplement
            (cp >= 0x20D0 && cp <= 0x20FF) ||   // Combining Diacritical Marks for Symbols
            (cp >= 0xFE00 && cp <= 0xFE0F) ||   // Variation Selectors
            (cp >= 0xFE20 && cp <= 0xFE2F) ||   // Combining Half Marks
            (cp >= 0x101FD && cp <= 0x101FD) ||
            (cp >= 0x102E0 && cp <= 0x102E0) ||
            (cp >= 0x10376 && cp <= 0x1037A) ||
            (cp >= 0x10A01 && cp <= 0x10A03) ||
            (cp >= 0x10A05 && cp <= 0x10A06) ||
            (cp >= 0x10A0C && cp <= 0x10A0F) ||
            (cp >= 0x10A38 && cp <= 0x10A3A) ||
            (cp >= 0x10A3F && cp <= 0x10A3F) ||
            (cp >= 0x10AE5 && cp <= 0x10AE6) ||
            (cp >= 0x11000 && cp <= 0x11002) ||
            (cp >= 0x11038 && cp <= 0x11046) ||
            (cp >= 0xE0100 && cp <= 0xE01EF))   // Variation Selectors Supplement
        {
            return BreakProperty::Extend;
        }

        // Spacing marks (some common ones)
        if ((cp >= 0x0903 && cp <= 0x0903) ||
            (cp >= 0x093B && cp <= 0x093B) ||
            (cp >= 0x093E && cp <= 0x0940) ||
            (cp >= 0x0949 && cp <= 0x094C) ||
            (cp >= 0x094E && cp <= 0x094F) ||
            (cp >= 0x0982 && cp <= 0x0983) ||
            (cp >= 0x09BE && cp <= 0x09C0) ||
            (cp >= 0x09C7 && cp <= 0x09C8) ||
            (cp >= 0x09CB && cp <= 0x09CC) ||
            (cp >= 0x09D7 && cp <= 0x09D7))
        {
            return BreakProperty::SpacingMark;
        }

        // Hangul syllables (simplified)
        if (cp >= 0x1100 && cp <= 0x115F) return BreakProperty::L;  // Leading consonants
        if (cp >= 0x1160 && cp <= 0x11A7) return BreakProperty::V;  // Vowels
        if (cp >= 0x11A8 && cp <= 0x11FF) return BreakProperty::T;  // Trailing consonants
        if (cp >= 0xAC00 && cp <= 0xD7A3) {
            // Precomposed syllables
            int syllableIndex = cp - 0xAC00;
            int trailing = syllableIndex % 28;
            if (trailing == 0) return BreakProperty::LV;
            return BreakProperty::LVT;
        }

        // Prepend characters (rare, simplified)
        if (cp == 0x0600 || cp == 0x0601 || cp == 0x0602 || cp == 0x0603 ||
            cp == 0x0604 || cp == 0x0605 || cp == 0x06DD || cp == 0x070F ||
            cp == 0x0890 || cp == 0x0891 || cp == 0x08E2 || cp == 0x110BD ||
            cp == 0x110CD)
        {
            return BreakProperty::Prepend;
        }

        return BreakProperty::Other;
    }

    bool IsGraphemeBoundary(uint32_t cp1, uint32_t cp2, bool& inExtendSequence, bool& inRISequence, int& riCount) {
        BreakProperty prop1 = GetBreakProperty(cp1);
        BreakProperty prop2 = GetBreakProperty(cp2);

        // GB3: Do not break between CR and LF
        if (prop1 == BreakProperty::CR && prop2 == BreakProperty::LF)
            return false;

        // GB4: Break after controls (including CR/LF)
        if (prop1 == BreakProperty::Control || prop1 == BreakProperty::CR || prop1 == BreakProperty::LF)
            return true;

        // GB5: Break before controls
        if (prop2 == BreakProperty::Control || prop2 == BreakProperty::CR || prop2 == BreakProperty::LF)
            return true;

        // GB6-8: Hangul syllable sequences
        if (prop1 == BreakProperty::L && 
            (prop2 == BreakProperty::L || prop2 == BreakProperty::V || 
             prop2 == BreakProperty::LV || prop2 == BreakProperty::LVT))
            return false;

        if ((prop1 == BreakProperty::LV || prop1 == BreakProperty::V) &&
            (prop2 == BreakProperty::V || prop2 == BreakProperty::T))
            return false;

        if ((prop1 == BreakProperty::LVT || prop1 == BreakProperty::T) && prop2 == BreakProperty::T)
            return false;

        // GB9: Do not break before Extend or ZWJ
        if (prop2 == BreakProperty::Extend || prop2 == BreakProperty::ZWJ)
            return false;

        // GB9a: Do not break before SpacingMarks
        if (prop2 == BreakProperty::SpacingMark)
            return false;

        // GB9b: Do not break after Prepend
        if (prop1 == BreakProperty::Prepend)
            return false;

        // GB11: Do not break within emoji modifier sequences or emoji ZWJ sequences
        // \p{Extended_Pictographic} Extend* ZWJ × \p{Extended_Pictographic}
        if (prop1 == BreakProperty::ZWJ) {
            // Check if cp2 is Extended_Pictographic (emoji)
            // Simplified check for common emoji ranges
            if ((cp2 >= 0x1F300 && cp2 <= 0x1F9FF) ||  // Miscellaneous Symbols and Pictographs, Emoticons, etc.
                (cp2 >= 0x2600 && cp2 <= 0x26FF) ||     // Miscellaneous Symbols
                (cp2 >= 0x2700 && cp2 <= 0x27BF) ||     // Dingbats
                (cp2 >= 0x1FA00 && cp2 <= 0x1FAFF) ||   // Chess, Symbols, etc.
                (cp2 >= 0x231A && cp2 <= 0x231B) ||     // Watch, Hourglass
                (cp2 >= 0x23E9 && cp2 <= 0x23F3) ||     // Various symbols
                (cp2 >= 0x23F8 && cp2 <= 0x23FA) ||
                (cp2 >= 0x25AA && cp2 <= 0x25AB) ||
                (cp2 >= 0x25B6 && cp2 <= 0x25C0) ||
                (cp2 >= 0x25FB && cp2 <= 0x25FE) ||
                (cp2 >= 0x2614 && cp2 <= 0x2615) ||
                (cp2 >= 0x2648 && cp2 <= 0x2653) ||
                (cp2 >= 0x267F && cp2 <= 0x267F) ||
                (cp2 >= 0x2693 && cp2 <= 0x2693) ||
                (cp2 >= 0x26A1 && cp2 <= 0x26A1) ||
                (cp2 >= 0x26AA && cp2 <= 0x26AB) ||
                (cp2 >= 0x26BD && cp2 <= 0x26BE) ||
                (cp2 >= 0x26C4 && cp2 <= 0x26C5) ||
                (cp2 >= 0x26CE && cp2 <= 0x26CE) ||
                (cp2 >= 0x26D4 && cp2 <= 0x26D4) ||
                (cp2 >= 0x26EA && cp2 <= 0x26EA) ||
                (cp2 >= 0x26F2 && cp2 <= 0x26F3) ||
                (cp2 >= 0x26F5 && cp2 <= 0x26F5) ||
                (cp2 >= 0x26FA && cp2 <= 0x26FA) ||
                (cp2 >= 0x26FD && cp2 <= 0x26FD) ||
                (cp2 >= 0x2702 && cp2 <= 0x2702) ||
                (cp2 >= 0x2705 && cp2 <= 0x2705) ||
                (cp2 >= 0x2708 && cp2 <= 0x270D) ||
                (cp2 >= 0x270F && cp2 <= 0x270F))
            {
                return false;
            }
        }

        // GB12/13: Regional Indicator sequences (flags)
        if (prop1 == BreakProperty::Regional_Indicator && prop2 == BreakProperty::Regional_Indicator) {
            // Track RI count for proper pairing
            if (!inRISequence) {
                inRISequence = true;
                riCount = 1;
                return false;  // Don't break - first pair
            } else {
                riCount++;
                if (riCount % 2 == 0) {
                    // Even count - end of pair, should break
                    return true;
                }
                return false;
            }
        } else {
            inRISequence = false;
            riCount = 0;
        }

        // GB999: Otherwise, break everywhere
        return true;
    }

    size_t NextGraphemeBoundary(const std::string& str, size_t bytePos) {
        if (bytePos >= str.size()) return str.size();

        // Start from current position
        auto it = str.begin() + bytePos;
        auto end = str.end();

        // Get first codepoint
        auto tempIt = it;
        uint32_t prevCp = UTF8::DecodeCodepoint(tempIt, end);
        if (tempIt == end) return str.size();

        bool inExtendSeq = false;
        bool inRISeq = false;
        int riCount = 0;

        // Look for next boundary
        while (tempIt != end) {
            auto nextIt = tempIt;
            uint32_t currCp = UTF8::DecodeCodepoint(nextIt, end);

            if (IsGraphemeBoundary(prevCp, currCp, inExtendSeq, inRISeq, riCount)) {
                return tempIt - str.begin();
            }

            prevCp = currCp;
            tempIt = nextIt;
        }

        return str.size();
    }

    size_t PrevGraphemeBoundary(const std::string& str, size_t bytePos) {
        if (bytePos == 0 || str.empty()) return 0;
        if (bytePos > str.size()) bytePos = str.size();

        // Move back to start of current character
        size_t pos = bytePos;
        while (pos > 0 && UTF8::IsContinuation(static_cast<uint8_t>(str[pos - 1]))) {
            --pos;
        }
        if (pos > 0) --pos;

        // Now find the start of the grapheme cluster containing this position
        // by scanning forward from the beginning
        size_t graphemeStart = 0;
        size_t currentPos = 0;

        bool inExtendSeq = false;
        bool inRISeq = false;
        int riCount = 0;

        auto it = str.begin();
        auto end = str.end();
        uint32_t prevCp = 0;
        bool first = true;

        while (it != end && static_cast<size_t>(it - str.begin()) <= pos) {
            size_t currBytePos = it - str.begin();
            auto tempIt = it;
            uint32_t currCp = UTF8::DecodeCodepoint(tempIt, end);

            if (!first && IsGraphemeBoundary(prevCp, currCp, inExtendSeq, inRISeq, riCount)) {
                if (currBytePos <= pos) {
                    graphemeStart = currBytePos;
                }
            }

            prevCp = currCp;
            it = tempIt;
            first = false;
        }

        return graphemeStart;
    }

    size_t CountGraphemes(const std::string& str) {
        if (str.empty()) return 0;

        size_t count = 1;  // At least one grapheme if non-empty
        bool inExtendSeq = false;
        bool inRISeq = false;
        int riCount = 0;

        auto it = str.begin();
        auto end = str.end();

        uint32_t prevCp = UTF8::DecodeCodepoint(it, end);

        while (it != end) {
            uint32_t currCp = UTF8::DecodeCodepoint(it, end);

            if (IsGraphemeBoundary(prevCp, currCp, inExtendSeq, inRISeq, riCount)) {
                count++;
            }

            prevCp = currCp;
        }

        return count;
    }

    std::pair<size_t, size_t> GetGraphemeAt(const std::string& str, size_t graphemeIndex) {
        if (str.empty()) return {0, 0};

        size_t currentGrapheme = 0;
        size_t graphemeStart = 0;
        bool inExtendSeq = false;
        bool inRISeq = false;
        int riCount = 0;

        auto it = str.begin();
        auto end = str.end();

        uint32_t prevCp = 0;
        bool first = true;

        while (it != end) {
            size_t bytePos = it - str.begin();
            auto tempIt = it;
            uint32_t currCp = UTF8::DecodeCodepoint(tempIt, end);

            if (!first && IsGraphemeBoundary(prevCp, currCp, inExtendSeq, inRISeq, riCount)) {
                if (currentGrapheme == graphemeIndex) {
                    return {graphemeStart, bytePos};
                }
                currentGrapheme++;
                graphemeStart = bytePos;
            }

            prevCp = currCp;
            it = tempIt;
            first = false;
        }

        // Last grapheme
        if (currentGrapheme == graphemeIndex) {
            return {graphemeStart, str.size()};
        }

        // Index out of bounds
        return {str.size(), str.size()};
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

// Trim helpers
static bool IsWhitespaceCodepoint(uint32_t cp) {
    return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' ||
           cp == 0x00A0 ||  // NBSP
           cp == 0x2000 || cp == 0x2001 || cp == 0x2002 || cp == 0x2003 ||
           cp == 0x2004 || cp == 0x2005 || cp == 0x2006 || cp == 0x2007 ||
           cp == 0x2008 || cp == 0x2009 || cp == 0x200A || cp == 0x200B ||
           cp == 0x2028 || cp == 0x2029 || cp == 0x202F || cp == 0x205F ||
           cp == 0x3000 || cp == 0xFEFF;
}

UCString& UCString::TrimLeft() {
    size_t pos = 0;
    auto it = data.begin();
    while (it != data.end()) {
        auto tempIt = it;
        uint32_t cp = UTF8::DecodeCodepoint(tempIt, data.end());
        if (!IsWhitespaceCodepoint(cp)) break;
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
        if (!IsWhitespaceCodepoint(cp)) break;
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

// Case conversion (ASCII only for now)
UCString UCString::ToLower() const {
    UCString result;
    result.data.reserve(data.size());
    
    auto it = data.begin();
    while (it != data.end()) {
        auto startIt = it;
        uint32_t cp = UTF8::DecodeCodepoint(it, data.end());
        
        if (cp >= 'A' && cp <= 'Z') {
            result.data += UTF8::EncodeCodepoint(cp + 32);
        } else {
            result.data.append(startIt, it);
        }
    }
    return result;
}

UCString UCString::ToUpper() const {
    UCString result;
    result.data.reserve(data.size());
    
    auto it = data.begin();
    while (it != data.end()) {
        auto startIt = it;
        uint32_t cp = UTF8::DecodeCodepoint(it, data.end());
        
        if (cp >= 'a' && cp <= 'z') {
            result.data += UTF8::EncodeCodepoint(cp - 32);
        } else {
            result.data.append(startIt, it);
        }
    }
    return result;
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
