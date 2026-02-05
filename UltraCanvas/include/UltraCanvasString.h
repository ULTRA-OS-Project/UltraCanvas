// UltraCanvasString.h
// UTF-8 aware string class with grapheme cluster support for proper text handling
// Powered by libgrapheme for full Unicode 17.0 compliance
// Version: 2.0.0
// Last Modified: 2026-02-01
// Author: UltraCanvas Framework
#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <functional>
#include <iostream>

namespace UltraCanvas {

// ===== FORWARD DECLARATIONS =====
class UCString;
class UCStringIterator;
class UCStringConstIterator;

// ===== UTF-8 UTILITIES =====
namespace UTF8 {
    // Byte sequence length from leading byte
    inline size_t SequenceLength(uint8_t leadByte) {
        if ((leadByte & 0x80) == 0x00) return 1;      // 0xxxxxxx - ASCII
        if ((leadByte & 0xE0) == 0xC0) return 2;      // 110xxxxx
        if ((leadByte & 0xF0) == 0xE0) return 3;      // 1110xxxx
        if ((leadByte & 0xF8) == 0xF0) return 4;      // 11110xxx
        return 1; // Invalid, treat as single byte
    }

    // Check if byte is continuation byte (10xxxxxx)
    inline bool IsContinuation(uint8_t byte) {
        return (byte & 0xC0) == 0x80;
    }

    // Check if byte is start of sequence
    inline bool IsLeadByte(uint8_t byte) {
        return (byte & 0xC0) != 0x80;
    }

    // Decode single codepoint from UTF-8 sequence
    // Returns codepoint and advances iterator
    template<typename Iterator>
    uint32_t DecodeCodepoint(Iterator& it, Iterator end) {
        if (it == end) return 0xFFFD; // Replacement character

        uint8_t lead = static_cast<uint8_t>(*it);
        uint32_t codepoint = 0;
        size_t length = SequenceLength(lead);

        if (length == 1) {
            codepoint = lead;
            ++it;
        } else if (length == 2) {
            codepoint = lead & 0x1F;
            ++it;
            if (it != end && IsContinuation(*it)) {
                codepoint = (codepoint << 6) | (*it & 0x3F);
                ++it;
            }
        } else if (length == 3) {
            codepoint = lead & 0x0F;
            for (int i = 0; i < 2 && it != end; ++i) {
                ++it;
                if (it != end && IsContinuation(*it)) {
                    codepoint = (codepoint << 6) | (*it & 0x3F);
                }
            }
            if (it != end) ++it;
        } else if (length == 4) {
            codepoint = lead & 0x07;
            for (int i = 0; i < 3 && it != end; ++i) {
                ++it;
                if (it != end && IsContinuation(*it)) {
                    codepoint = (codepoint << 6) | (*it & 0x3F);
                }
            }
            if (it != end) ++it;
        } else {
            ++it;
            codepoint = 0xFFFD;
        }

        return codepoint;
    }

    // Encode codepoint to UTF-8 bytes
    inline std::string EncodeCodepoint(uint32_t codepoint) {
        std::string result;
        if (codepoint <= 0x7F) {
            result += static_cast<char>(codepoint);
        } else if (codepoint <= 0x7FF) {
            result += static_cast<char>(0xC0 | (codepoint >> 6));
            result += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else if (codepoint <= 0xFFFF) {
            result += static_cast<char>(0xE0 | (codepoint >> 12));
            result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else if (codepoint <= 0x10FFFF) {
            result += static_cast<char>(0xF0 | (codepoint >> 18));
            result += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else {
            // Invalid codepoint, use replacement character
            result += "\xEF\xBF\xBD";
        }
        return result;
    }

    // Validate UTF-8 string
    inline bool IsValid(const std::string& str) {
        auto it = str.begin();
        while (it != str.end()) {
            uint8_t lead = static_cast<uint8_t>(*it);
            size_t len = SequenceLength(lead);
            
            if (std::distance(it, str.end()) < static_cast<ptrdiff_t>(len)) {
                return false;
            }
            
            ++it;
            for (size_t i = 1; i < len; ++i) {
                if (!IsContinuation(static_cast<uint8_t>(*it))) {
                    return false;
                }
                ++it;
            }
        }
        return true;
    }

    // Count codepoints in UTF-8 string
    inline size_t CountCodepoints(const std::string& str) {
        size_t count = 0;
        for (size_t i = 0; i < str.size(); ) {
            i += SequenceLength(static_cast<uint8_t>(str[i]));
            ++count;
        }
        return count;
    }
}

// ===== UNICODE CHARACTER CLASSIFICATION =====
// Provides codepoint-level classification (IsAlpha, IsDigit, etc.)
// libgrapheme handles segmentation and case; these cover General Category queries
namespace Unicode {

    // Character classification functions
    bool IsAlphabetic(uint32_t codepoint);
    bool IsNumeric(uint32_t codepoint);
    bool IsAlphanumeric(uint32_t codepoint);
    bool IsWhitespace(uint32_t codepoint);
    bool IsPunctuation(uint32_t codepoint);
    bool IsUppercase(uint32_t codepoint);
    bool IsLowercase(uint32_t codepoint);

} // namespace Unicode

// ===== GRAPHEME CLUSTER UTILITIES =====
// Wraps libgrapheme for full Unicode 17.0 compliant segmentation
namespace Grapheme {

    // Find next grapheme cluster boundary (returns byte offset)
    // Delegates to grapheme_next_character_break_utf8()
    size_t NextGraphemeBoundary(const std::string& str, size_t bytePos);

    // Find previous grapheme cluster boundary (returns byte offset)
    size_t PrevGraphemeBoundary(const std::string& str, size_t bytePos);

    // Count grapheme clusters in string
    size_t CountGraphemes(const std::string& str);

    // Get grapheme cluster at position (returns byte range: start, end)
    std::pair<size_t, size_t> GetGraphemeAt(const std::string& str, size_t graphemeIndex);

    // ===== WORD BOUNDARY NAVIGATION =====
    // UAX #29 compliant word segmentation via libgrapheme

    // Find next word boundary (returns byte offset)
    size_t NextWordBoundary(const std::string& str, size_t bytePos);

    // Find previous word boundary (returns byte offset)
    size_t PrevWordBoundary(const std::string& str, size_t bytePos);

    // ===== SENTENCE BOUNDARY NAVIGATION =====
    // UAX #29 compliant sentence segmentation via libgrapheme

    // Find next sentence boundary (returns byte offset)
    size_t NextSentenceBoundary(const std::string& str, size_t bytePos);

    // Find previous sentence boundary (returns byte offset)
    size_t PrevSentenceBoundary(const std::string& str, size_t bytePos);

    // ===== LINE BREAK NAVIGATION =====
    // UAX #14 compliant line break opportunity detection via libgrapheme

    // Find next permissible line break (returns byte offset)
    size_t NextLineBreak(const std::string& str, size_t bytePos);

    // Find previous permissible line break (returns byte offset)
    size_t PrevLineBreak(const std::string& str, size_t bytePos);

    int CountWords(const std::string& text);
}

// ===== GRAPHEME REFERENCE (for operator[]) =====
class GraphemeRef {
private:
    UCString* owner;
    size_t graphemeIndex;

public:
    GraphemeRef(UCString* str, size_t index);

    // Get as string (the grapheme cluster)
    std::string ToString() const;
    operator std::string() const { return ToString(); }

    // Get first codepoint
    uint32_t ToCodepoint() const;

    // ===== CHARACTER CLASSIFICATION =====
    // Classification based on first codepoint of the grapheme cluster
    bool IsAlpha() const;
    bool IsDigit() const;
    bool IsAlnum() const;
    bool IsSpace() const;
    bool IsPunct() const;
    bool IsUpper() const;
    bool IsLower() const;

    // ===== COMPARISON =====
    bool operator==(const GraphemeRef& other) const;
    bool operator==(const std::string& other) const;
    bool operator==(const char* other) const;
    bool operator==(char32_t codepoint) const;

    bool operator!=(const GraphemeRef& other) const { return !(*this == other); }
    bool operator!=(const std::string& other) const { return !(*this == other); }
    bool operator!=(const char* other) const { return !(*this == other); }
    bool operator!=(char32_t codepoint) const { return !(*this == codepoint); }

    // Assignment (replace grapheme)
    GraphemeRef& operator=(const std::string& str);
    GraphemeRef& operator=(const char* str);
    GraphemeRef& operator=(char32_t codepoint);
};

// ===== CONST GRAPHEME REFERENCE =====
class ConstGraphemeRef {
private:
    const UCString* owner;
    size_t graphemeIndex;

public:
    ConstGraphemeRef(const UCString* str, size_t index);

    std::string ToString() const;
    operator std::string() const { return ToString(); }
    uint32_t ToCodepoint() const;

    // ===== CHARACTER CLASSIFICATION =====
    bool IsAlpha() const;
    bool IsDigit() const;
    bool IsAlnum() const;
    bool IsSpace() const;
    bool IsPunct() const;
    bool IsUpper() const;
    bool IsLower() const;

    // ===== COMPARISON =====
    bool operator==(const ConstGraphemeRef& other) const;
    bool operator==(const std::string& other) const;
    bool operator==(const char* other) const;
    bool operator==(char32_t codepoint) const;

    bool operator!=(const ConstGraphemeRef& other) const { return !(*this == other); }
    bool operator!=(const std::string& other) const { return !(*this == other); }
    bool operator!=(const char* other) const { return !(*this == other); }
    bool operator!=(char32_t codepoint) const { return !(*this == codepoint); }
};

// ===== GRAPHEME ITERATOR =====
class UCStringIterator {
public:
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = std::string;
    using difference_type = std::ptrdiff_t;
    using pointer = std::string*;
    using reference = GraphemeRef;

private:
    UCString* owner;
    size_t graphemeIndex;

public:
    UCStringIterator() : owner(nullptr), graphemeIndex(0) {}
    UCStringIterator(UCString* str, size_t index) : owner(str), graphemeIndex(index) {}

    GraphemeRef operator*();
    UCStringIterator& operator++();
    UCStringIterator operator++(int);
    UCStringIterator& operator--();
    UCStringIterator operator--(int);

    bool operator==(const UCStringIterator& other) const {
        return owner == other.owner && graphemeIndex == other.graphemeIndex;
    }
    bool operator!=(const UCStringIterator& other) const { return !(*this == other); }

    size_t GetIndex() const { return graphemeIndex; }
};

// ===== CONST GRAPHEME ITERATOR =====
class UCStringConstIterator {
public:
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = std::string;
    using difference_type = std::ptrdiff_t;
    using pointer = const std::string*;
    using reference = ConstGraphemeRef;

private:
    const UCString* owner;
    size_t graphemeIndex;

public:
    UCStringConstIterator() : owner(nullptr), graphemeIndex(0) {}
    UCStringConstIterator(const UCString* str, size_t index) : owner(str), graphemeIndex(index) {}

    ConstGraphemeRef operator*() const;
    UCStringConstIterator& operator++();
    UCStringConstIterator operator++(int);
    UCStringConstIterator& operator--();
    UCStringConstIterator operator--(int);

    bool operator==(const UCStringConstIterator& other) const {
        return owner == other.owner && graphemeIndex == other.graphemeIndex;
    }
    bool operator!=(const UCStringConstIterator& other) const { return !(*this == other); }

    size_t GetIndex() const { return graphemeIndex; }
};

// ===== MAIN UCSTRING CLASS =====
class UCString {
private:
    std::string data;           // UTF-8 encoded data
    mutable size_t cachedGraphemeCount = 0;
    mutable bool graphemeCountValid = false;

    // Invalidate cache
    void InvalidateCache() { graphemeCountValid = false; }

    // Friend classes
    friend class GraphemeRef;
    friend class ConstGraphemeRef;
    friend class UCStringIterator;
    friend class UCStringConstIterator;

public:
    // ===== CONSTRUCTORS =====
    UCString() = default;
    UCString(const UCString& other) : data(other.data) {}
    UCString(UCString&& other) noexcept : data(std::move(other.data)) {}

    // From std::string (assumed UTF-8)
    UCString(const std::string& str) : data(str) {}
    UCString(std::string&& str) noexcept : data(std::move(str)) {}

    // From C-string (assumed UTF-8)
    UCString(const char* str) : data(str ? str : "") {}
    UCString(const char* str, size_t byteLen) : data(str, byteLen) {}

    // From string_view
    UCString(std::string_view sv) : data(sv) {}

    // From single codepoint
    explicit UCString(char32_t codepoint) : data(UTF8::EncodeCodepoint(codepoint)) {}

    // From codepoint repeated n times
    UCString(size_t count, char32_t codepoint);

    // From char32_t string
    UCString(const char32_t* str);

    // Destructor
    ~UCString() = default;

    // ===== ASSIGNMENT OPERATORS =====
    UCString& operator=(const UCString& other);
    UCString& operator=(UCString&& other) noexcept;
    UCString& operator=(const std::string& str);
    UCString& operator=(std::string&& str) noexcept;
    UCString& operator=(const char* str);
    UCString& operator=(std::string_view sv);
    UCString& operator=(char32_t codepoint);

    // ===== SIZE & CAPACITY =====

    // Returns the number of bytes (raw UTF-8 length)
    size_t ByteLength() const { return data.size(); }
    size_t size() const { return ByteLength(); }  // STL compatibility (bytes)

    // Returns the number of Unicode codepoints
    size_t CodepointCount() const { return UTF8::CountCodepoints(data); }

    // Returns the number of grapheme clusters (user-perceived characters)
    size_t Length() const;
    size_t length() const { return Length(); }  // STL compatibility (graphemes)

    // Check if empty
    bool Empty() const { return data.empty(); }
    bool empty() const { return Empty(); }

    // Capacity operations
    void Reserve(size_t byteCapacity) { data.reserve(byteCapacity); }
    void ShrinkToFit() { data.shrink_to_fit(); }
    size_t Capacity() const { return data.capacity(); }

    // ===== ELEMENT ACCESS =====

    // Access by grapheme index (returns grapheme cluster as string)
    GraphemeRef operator[](size_t graphemeIndex);
    ConstGraphemeRef operator[](size_t graphemeIndex) const;

    // Access with bounds checking
    GraphemeRef At(size_t graphemeIndex);
    ConstGraphemeRef At(size_t graphemeIndex) const;

    // Get grapheme cluster at index as string
    std::string GetGrapheme(size_t graphemeIndex) const;

    // Get codepoint at index (first codepoint of grapheme at that index)
    uint32_t GetCodepoint(size_t codepointIndex) const;

    // First and last grapheme
    std::string Front() const;
    std::string Back() const;

    // ===== RAW DATA ACCESS =====

    // Get underlying UTF-8 data
    const std::string& Data() const { return data; }
    const char* CStr() const { return data.c_str(); }
    const char* c_str() const { return data.c_str(); }

    // Access raw bytes (careful - not grapheme-aware!)
    char& RawAt(size_t byteIndex) { InvalidateCache(); return data[byteIndex]; }
    const char& RawAt(size_t byteIndex) const { return data[byteIndex]; }

    // ===== CONVERSION =====

    // To std::string (returns copy)
    std::string ToString() const { return data; }
    operator std::string() const { return data; }

    // To std::string_view
    std::string_view ToStringView() const { return std::string_view(data); }
    operator std::string_view() const { return ToStringView(); }

    // To UTF-32 string
    std::u32string ToUTF32() const;

    // From UTF-32 string
    static UCString FromUTF32(const std::u32string& str);
    static UCString FromUTF32(const char32_t* str);

    // ===== ITERATORS =====

    UCStringIterator begin() { return UCStringIterator(this, 0); }
    UCStringIterator end() { return UCStringIterator(this, Length()); }
    UCStringConstIterator begin() const { return UCStringConstIterator(this, 0); }
    UCStringConstIterator end() const { return UCStringConstIterator(this, Length()); }
    UCStringConstIterator cbegin() const { return begin(); }
    UCStringConstIterator cend() const { return end(); }

    // Raw byte iterators
    std::string::iterator RawBegin() { InvalidateCache(); return data.begin(); }
    std::string::iterator RawEnd() { InvalidateCache(); return data.end(); }
    std::string::const_iterator RawBegin() const { return data.begin(); }
    std::string::const_iterator RawEnd() const { return data.end(); }

    // ===== POSITION CONVERSION =====

    // Convert grapheme index to byte offset
    size_t GraphemeToByteOffset(size_t graphemeIndex) const;

    // Convert byte offset to grapheme index
    size_t ByteToGraphemeIndex(size_t byteOffset) const;

    // Convert codepoint index to byte offset
    size_t CodepointToByteOffset(size_t codepointIndex) const;

    // Convert byte offset to codepoint index
    size_t ByteToCodepointIndex(size_t byteOffset) const;

    // ===== CURSOR NAVIGATION (For text editors) =====

    // Get byte position of next/previous grapheme cluster
    size_t NextGraphemePosition(size_t bytePos) const {
        return Grapheme::NextGraphemeBoundary(data, bytePos);
    }
    size_t PrevGraphemePosition(size_t bytePos) const {
        return Grapheme::PrevGraphemeBoundary(data, bytePos);
    }

    // Get byte position of next/previous word boundary (UAX #29)
    size_t NextWordPosition(size_t bytePos) const {
        return Grapheme::NextWordBoundary(data, bytePos);
    }
    size_t PrevWordPosition(size_t bytePos) const {
        return Grapheme::PrevWordBoundary(data, bytePos);
    }

    // Get byte position of next/previous sentence boundary (UAX #29)
    size_t NextSentencePosition(size_t bytePos) const {
        return Grapheme::NextSentenceBoundary(data, bytePos);
    }
    size_t PrevSentencePosition(size_t bytePos) const {
        return Grapheme::PrevSentenceBoundary(data, bytePos);
    }

    // Get byte position of next/previous line break opportunity (UAX #14)
    size_t NextLineBreakPosition(size_t bytePos) const {
        return Grapheme::NextLineBreak(data, bytePos);
    }
    size_t PrevLineBreakPosition(size_t bytePos) const {
        return Grapheme::PrevLineBreak(data, bytePos);
    }

    // ===== MODIFICATION =====

    // Clear content
    void Clear() { data.clear(); InvalidateCache(); }
    void clear() { Clear(); }

    // Append operations
    UCString& Append(const UCString& str);
    UCString& Append(const std::string& str);
    UCString& Append(const char* str);
    UCString& Append(std::string_view sv);
    UCString& Append(char32_t codepoint);
    UCString& Append(size_t count, char32_t codepoint);

    UCString& operator+=(const UCString& str) { return Append(str); }
    UCString& operator+=(const std::string& str) { return Append(str); }
    UCString& operator+=(const char* str) { return Append(str); }
    UCString& operator+=(std::string_view sv) { return Append(sv); }
    UCString& operator+=(char32_t codepoint) { return Append(codepoint); }

    // Insert at grapheme position
    UCString& Insert(size_t graphemePos, const UCString& str);
    UCString& Insert(size_t graphemePos, const std::string& str);
    UCString& Insert(size_t graphemePos, const char* str);
    UCString& Insert(size_t graphemePos, char32_t codepoint);

    // Insert at byte position (raw)
    UCString& InsertRaw(size_t bytePos, const std::string& str);

    // Erase graphemes
    UCString& Erase(size_t graphemePos, size_t graphemeCount = 1);

    // Erase bytes (raw)
    UCString& EraseRaw(size_t bytePos, size_t byteCount);

    // Replace graphemes
    UCString& Replace(size_t graphemePos, size_t graphemeCount, const UCString& str);
    UCString& Replace(size_t graphemePos, size_t graphemeCount, const std::string& str);
    UCString& Replace(size_t graphemePos, size_t graphemeCount, const char* str);

    // Pop last grapheme
    void PopBack();

    // ===== SUBSTRING =====

    // Get substring by grapheme positions
    UCString Substr(size_t graphemePos, size_t graphemeCount = std::string::npos) const;

    // Get substring by byte positions (raw)
    UCString SubstrRaw(size_t bytePos, size_t byteCount = std::string::npos) const;

    // ===== SEARCH =====

    // Find substring (returns grapheme position, npos if not found)
    static constexpr size_t npos = std::string::npos;

    size_t Find(const UCString& str, size_t startGraphemePos = 0) const;
    size_t Find(const std::string& str, size_t startGraphemePos = 0) const;
    size_t Find(const char* str, size_t startGraphemePos = 0) const;
    size_t Find(char32_t codepoint, size_t startGraphemePos = 0) const;

    // Find from end
    size_t RFind(const UCString& str, size_t startGraphemePos = npos) const;
    size_t RFind(const std::string& str, size_t startGraphemePos = npos) const;
    size_t RFind(const char* str, size_t startGraphemePos = npos) const;

    // Contains
    bool Contains(const UCString& str) const { return Find(str) != npos; }
    bool Contains(const std::string& str) const { return Find(str) != npos; }
    bool Contains(const char* str) const { return Find(str) != npos; }
    bool Contains(char32_t codepoint) const { return Find(codepoint) != npos; }

    // Starts/Ends with
    bool StartsWith(const UCString& prefix) const;
    bool StartsWith(const std::string& prefix) const;
    bool StartsWith(const char* prefix) const;

    bool EndsWith(const UCString& suffix) const;
    bool EndsWith(const std::string& suffix) const;
    bool EndsWith(const char* suffix) const;

    // ===== COMPARISON =====

    int Compare(const UCString& other) const { return data.compare(other.data); }
    int Compare(const std::string& other) const { return data.compare(other); }
    int Compare(const char* other) const { return data.compare(other); }

    // ===== VALIDATION =====

    bool IsValidUTF8() const { return UTF8::IsValid(data); }

    // Fix invalid UTF-8 sequences (replace with replacement character)
    UCString& Sanitize();
    static UCString Sanitized(const std::string& str);

    // ===== UTILITY =====

    // Split by delimiter
    std::vector<UCString> Split(const UCString& delimiter) const;
    std::vector<UCString> Split(const std::string& delimiter) const;
    std::vector<UCString> Split(const char* delimiter) const;
    std::vector<UCString> Split(char32_t delimiter) const;

    // Join strings
    static UCString Join(const std::vector<UCString>& parts, const UCString& separator);

    // Trim whitespace
    UCString& TrimLeft();
    UCString& TrimRight();
    UCString& Trim();

    UCString Trimmed() const;
    UCString TrimmedLeft() const;
    UCString TrimmedRight() const;

    // ===== CASE CONVERSION (Full Unicode via libgrapheme) =====
    UCString ToLower() const;
    UCString ToUpper() const;
    UCString ToTitleCase() const;

    // ===== CASE DETECTION (Full Unicode via libgrapheme) =====
    bool IsLowerCase() const;
    bool IsUpperCase() const;
    bool IsTitleCase() const;

    // Reverse (grapheme-aware)
    UCString Reversed() const;
};

// ===== COMPARISON OPERATORS =====

inline bool operator==(const UCString& lhs, const UCString& rhs) { return lhs.Compare(rhs) == 0; }
inline bool operator!=(const UCString& lhs, const UCString& rhs) { return lhs.Compare(rhs) != 0; }
inline bool operator<(const UCString& lhs, const UCString& rhs) { return lhs.Compare(rhs) < 0; }
inline bool operator<=(const UCString& lhs, const UCString& rhs) { return lhs.Compare(rhs) <= 0; }
inline bool operator>(const UCString& lhs, const UCString& rhs) { return lhs.Compare(rhs) > 0; }
inline bool operator>=(const UCString& lhs, const UCString& rhs) { return lhs.Compare(rhs) >= 0; }

// UCString vs std::string
inline bool operator==(const UCString& lhs, const std::string& rhs) { return lhs.Compare(rhs) == 0; }
inline bool operator==(const std::string& lhs, const UCString& rhs) { return rhs.Compare(lhs) == 0; }
inline bool operator!=(const UCString& lhs, const std::string& rhs) { return lhs.Compare(rhs) != 0; }
inline bool operator!=(const std::string& lhs, const UCString& rhs) { return rhs.Compare(lhs) != 0; }
inline bool operator<(const UCString& lhs, const std::string& rhs) { return lhs.Compare(rhs) < 0; }
inline bool operator<(const std::string& lhs, const UCString& rhs) { return rhs.Compare(lhs) > 0; }
inline bool operator<=(const UCString& lhs, const std::string& rhs) { return lhs.Compare(rhs) <= 0; }
inline bool operator<=(const std::string& lhs, const UCString& rhs) { return rhs.Compare(lhs) >= 0; }
inline bool operator>(const UCString& lhs, const std::string& rhs) { return lhs.Compare(rhs) > 0; }
inline bool operator>(const std::string& lhs, const UCString& rhs) { return rhs.Compare(lhs) < 0; }
inline bool operator>=(const UCString& lhs, const std::string& rhs) { return lhs.Compare(rhs) >= 0; }
inline bool operator>=(const std::string& lhs, const UCString& rhs) { return rhs.Compare(lhs) <= 0; }

// UCString vs const char*
inline bool operator==(const UCString& lhs, const char* rhs) { return lhs.Compare(rhs) == 0; }
inline bool operator==(const char* lhs, const UCString& rhs) { return rhs.Compare(lhs) == 0; }
inline bool operator!=(const UCString& lhs, const char* rhs) { return lhs.Compare(rhs) != 0; }
inline bool operator!=(const char* lhs, const UCString& rhs) { return rhs.Compare(lhs) != 0; }
inline bool operator<(const UCString& lhs, const char* rhs) { return lhs.Compare(rhs) < 0; }
inline bool operator<(const char* lhs, const UCString& rhs) { return rhs.Compare(lhs) > 0; }
inline bool operator<=(const UCString& lhs, const char* rhs) { return lhs.Compare(rhs) <= 0; }
inline bool operator<=(const char* lhs, const UCString& rhs) { return rhs.Compare(lhs) >= 0; }
inline bool operator>(const UCString& lhs, const char* rhs) { return lhs.Compare(rhs) > 0; }
inline bool operator>(const char* lhs, const UCString& rhs) { return rhs.Compare(lhs) < 0; }
inline bool operator>=(const UCString& lhs, const char* rhs) { return lhs.Compare(rhs) >= 0; }
inline bool operator>=(const char* lhs, const UCString& rhs) { return rhs.Compare(lhs) <= 0; }

// ===== CONCATENATION OPERATORS =====

inline UCString operator+(const UCString& lhs, const UCString& rhs) {
    UCString result(lhs);
    result.Append(rhs);
    return result;
}

inline UCString operator+(const UCString& lhs, const std::string& rhs) {
    UCString result(lhs);
    result.Append(rhs);
    return result;
}

inline UCString operator+(const std::string& lhs, const UCString& rhs) {
    UCString result(lhs);
    result.Append(rhs);
    return result;
}

inline UCString operator+(const UCString& lhs, const char* rhs) {
    UCString result(lhs);
    result.Append(rhs);
    return result;
}

inline UCString operator+(const char* lhs, const UCString& rhs) {
    UCString result(lhs);
    result.Append(rhs);
    return result;
}

inline UCString operator+(const UCString& lhs, char32_t rhs) {
    UCString result(lhs);
    result.Append(rhs);
    return result;
}

inline UCString operator+(char32_t lhs, const UCString& rhs) {
    UCString result(lhs);
    result.Append(rhs);
    return result;
}

// ===== STREAM OPERATORS =====

inline std::ostream& operator<<(std::ostream& os, const UCString& str) {
    return os << str.Data();
}

inline std::istream& operator>>(std::istream& is, UCString& str) {
    std::string temp;
    is >> temp;
    str = temp;
    return is;
}

// ===== STRING LITERAL SUPPORT =====

namespace StringLiterals {
    inline UCString operator""_uc(const char* str, size_t len) {
        return UCString(str, len);
    }
}

} // namespace UltraCanvas