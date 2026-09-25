// core/UltraCanvasTextUtils.cpp
// Standalone text utilities. See the header for why this is a separate file.
//
// The string helpers and Base64, moved verbatim from UltraCanvasUtils.cpp, and
// the repair of legacy-encoded file names.
// These are the text utilities the UltraCanvas library itself links — UltraNet
// calls Base64 and is absorbed into the shared core — so a shared build exports
// them all. Base32, whose only consumer is UltraCrypt, lives apart in
// UltraCanvasBase32.cpp; see that file for why that split matters at link time.
//
// Version: 1.0.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCanvasTextUtils.h"

#include <locale>
#include <sstream>
#include <iomanip>

namespace UltraCanvas {

// ===========================================================================
// Strings
// ===========================================================================

std::string ToLowerCase(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

bool StartsWith(const std::string& str, const std::string& prefix) {
    return str.substr(0, prefix.length()) == prefix;
}

std::string Trim(const std::string& s, const std::string& strippedChars) {
    const size_t a = s.find_first_not_of(strippedChars);
    if (a == std::string::npos) return {};
    const size_t b = s.find_last_not_of(strippedChars);
    return s.substr(a, b - a + 1);
}

std::vector<std::string> Split(const std::string& str, char delimiter) {
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, delimiter)) {
        if (!item.empty()) result.push_back(item);
    }
    return result;
}

// ===========================================================================
// Numbers in machine-readable formats
// ===========================================================================
// See the header for why these exist rather than std::stof / strtof / atof.

namespace {

// One scanner for both widths. The scan is pure text - it decides where the
// number ends without ever converting - so only the final istringstream
// extraction differs between float and double.
template <typename T>
const char* ScanClassic(const char* first, const char* last, T& out) {
    auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
    const char* p = first;
    if (p != last && *p == '-') ++p;
    bool sawDigit = false;
    while (p != last && isDigit(*p)) { ++p; sawDigit = true; }
    if (p != last && *p == '.') {
        ++p;
        while (p != last && isDigit(*p)) { ++p; sawDigit = true; }
    }
    if (!sawDigit) return first;
    // An exponent counts only when it is complete - see "1.5em" in the header.
    if (p != last && (*p == 'e' || *p == 'E')) {
        const char* exponent = p + 1;
        if (exponent != last && (*exponent == '+' || *exponent == '-')) ++exponent;
        if (exponent != last && isDigit(*exponent)) {
            while (exponent != last && isDigit(*exponent)) ++exponent;
            p = exponent;
        }
    }
    std::string number(first, p);
    if (!number.empty() && number.back() == '.') number.pop_back();  // "1." is 1
    std::istringstream in(number);
    in.imbue(std::locale::classic());   // '.' is the decimal point, always
    T value = T(0);
    in >> value;
    if (in.fail()) {
        // What was scanned is a well-formed number, so a failure here means it
        // does not fit the type - the out-of-range from_chars reports and these
        // callers ignore. Leave `out` as the caller set it and report the
        // number as read, exactly as from_chars does.
        return p;
    }
    out = value;
    return p;
}

template <typename T>
bool TryParseClassic(const std::string& text, T& out) {
    const char* first = text.data();
    const char* last = first + text.size();
    while (first != last && std::isspace(static_cast<unsigned char>(*first))) ++first;
    if (first != last && *first == '+') ++first;   // from_chars refuses it; stof took it
    // Seeded with the caller's value so that a number too large for the type
    // leaves `out` alone, exactly as ParseFloatClassic does on that path.
    T value = out;
    if (ScanClassic(first, last, value) == first) return false;
    out = value;
    return true;
}

} // namespace

const char* ParseFloatClassic(const char* first, const char* last, float& out) {
    return ScanClassic(first, last, out);
}

const char* ParseFloatClassic(const char* first, const char* last, double& out) {
    return ScanClassic(first, last, out);
}

bool TryParseFloat(const std::string& text, float& out)  { return TryParseClassic(text, out); }
bool TryParseFloat(const std::string& text, double& out) { return TryParseClassic(text, out); }

std::string FormatFloatClassic(double value, int precision) {
    std::ostringstream out;
    out.imbue(std::locale::classic());   // '.' is the decimal point, always
    out << std::setprecision(precision) << value;
    return out.str();
}

namespace {

const char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

} // namespace

// ===========================================================================
// Base64
// ===========================================================================

std::vector<uint8_t> Base64Decode(const std::string& input) {
    std::vector<uint8_t> result;

    if (input.empty()) {
        return result;
    }

    // Build decode table
    std::vector<int> decodeTable(256, -1);
    for (size_t i = 0; i < sizeof(kBase64Alphabet) - 1; ++i) {
        decodeTable[static_cast<unsigned char>(kBase64Alphabet[i])] =
            static_cast<int>(i);
    }

    // Calculate output size (approximate)
    size_t inputLen = input.length();
    size_t padding = 0;
    if (inputLen >= 2) {
        if (input[inputLen - 1] == '=') padding++;
        if (input[inputLen - 2] == '=') padding++;
    }
    size_t outputLen = (inputLen / 4) * 3 - padding;
    result.reserve(outputLen);

    uint32_t buffer = 0;
    int bitsCollected = 0;

    for (char c : input) {
        if (c == '=') break; // End of data
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue; // Skip whitespace

        int value = decodeTable[static_cast<unsigned char>(c)];
        if (value < 0) continue; // Invalid character, skip

        buffer = (buffer << 6) | static_cast<uint32_t>(value);
        bitsCollected += 6;

        if (bitsCollected >= 8) {
            bitsCollected -= 8;
            result.push_back(static_cast<uint8_t>((buffer >> bitsCollected) & 0xFF));
        }
    }

    return result;
}

std::string Base64Encode(const std::vector<uint8_t>& in, bool wrap) {
    std::string out;
    std::size_t lineWidth = 0;
    for (std::size_t i = 0; i < in.size(); i += 3) {
        const std::size_t left = in.size() - i;
        const uint32_t t = (static_cast<uint32_t>(in[i]) << 16) |
                           (left > 1 ? static_cast<uint32_t>(in[i+1]) << 8 : 0) |
                           (left > 2 ? static_cast<uint32_t>(in[i+2])      : 0);
        out.push_back(kBase64Alphabet[(t >> 18) & 0x3f]);
        out.push_back(kBase64Alphabet[(t >> 12) & 0x3f]);
        out.push_back(left > 1 ? kBase64Alphabet[(t >> 6) & 0x3f] : '=');
        out.push_back(left > 2 ? kBase64Alphabet[ t       & 0x3f] : '=');
        lineWidth += 4;
        if (wrap && lineWidth >= 76) { out.append("\r\n"); lineWidth = 0; }
    }
    if (wrap && lineWidth > 0) out.append("\r\n");
    return out;
}

// ===========================================================================
// Legacy-encoded file names
// ===========================================================================
namespace {

// Length of the well-formed UTF-8 sequence starting at s[i], or 0 when the
// byte there does not start one (RFC 3629: no overlong forms, no surrogates,
// nothing past U+10FFFF).
size_t Utf8SequenceAt(const std::string& s, size_t i) {
    const auto byte = [&s](size_t k) { return static_cast<unsigned char>(s[k]); };
    const unsigned char lead = byte(i);
    if (lead < 0x80) return 1;
    size_t len = 0;
    if (lead >= 0xC2 && lead <= 0xDF)      len = 2;
    else if (lead >= 0xE0 && lead <= 0xEF) len = 3;
    else if (lead >= 0xF0 && lead <= 0xF4) len = 4;
    else return 0;
    if (i + len > s.size()) return 0;
    for (size_t k = 1; k < len; ++k) {
        if ((byte(i + k) & 0xC0) != 0x80) return 0;
    }
    const unsigned char second = byte(i + 1);
    if (lead == 0xE0 && second < 0xA0) return 0;   // overlong
    if (lead == 0xED && second > 0x9F) return 0;   // UTF-16 surrogate
    if (lead == 0xF0 && second < 0x90) return 0;   // overlong
    if (lead == 0xF4 && second > 0x8F) return 0;   // past U+10FFFF
    return len;
}

// IBM437, bytes 0x80..0xFF.
const uint16_t kCp437High[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0,
};

// Windows-1252, bytes 0x80..0x9F; 0xA0..0xFF are ISO-8859-1 (= Unicode).
// The five bytes 1252 leaves undefined keep their Latin-1 control code.
const uint16_t kCp1252C1[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

uint32_t Cp1252ToUnicode(unsigned char c) {
    return (c >= 0x80 && c < 0xA0) ? kCp1252C1[c - 0x80] : c;
}

uint32_t Cp437ToUnicode(unsigned char c) {
    return c >= 0x80 ? kCp437High[c - 0x80] : c;
}

// The accented letters a European name is made of (Latin-1 Supplement and
// Latin Extended-A/B, less × and ÷). Box drawing, Greek maths symbols and
// typographic quotes are what the wrong code page turns the same bytes into.
bool IsLatinLetter(uint32_t cp) {
    return cp >= 0xC0 && cp <= 0x24F && cp != 0xD7 && cp != 0xF7;
}

void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

} // namespace

bool IsWellFormedUtf8(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        const size_t len = Utf8SequenceAt(s, i);
        if (len == 0) return false;
        i += len;
    }
    return true;
}

std::string RepairLegacyEncodedName(const std::string& name) {
    if (IsWellFormedUtf8(name)) return name;

    // Which code page makes letters of the stray bytes? Only a byte inside a
    // word votes: next to a letter an umlaut is what it most likely is
    // ("Namens\x84nderung"), while between spaces or after a digit the same
    // 0x80..0x9F byte is far more often Windows-1252 punctuation - the dash
    // in "Bericht \x96 Entwurf", the euro sign in "Preis 5\x80" - than an
    // IBM437 letter standing on its own.
    const auto letterish = [&name](size_t k) {
        const unsigned char c = static_cast<unsigned char>(name[k]);
        return c >= 0x80 || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    };
    int latinIn1252 = 0, latinIn437 = 0;
    for (size_t i = 0; i < name.size();) {
        const size_t len = Utf8SequenceAt(name, i);
        if (len > 0) { i += len; continue; }
        const bool inWord = (i > 0 && letterish(i - 1)) ||
                            (i + 1 < name.size() && letterish(i + 1));
        const unsigned char c = static_cast<unsigned char>(name[i++]);
        if (!inWord) continue;
        if (IsLatinLetter(Cp1252ToUnicode(c))) ++latinIn1252;
        if (IsLatinLetter(Cp437ToUnicode(c)))  ++latinIn437;
    }
    const bool dos = latinIn437 > latinIn1252;

    std::string out;
    out.reserve(name.size() + name.size() / 2);
    for (size_t i = 0; i < name.size();) {
        const size_t len = Utf8SequenceAt(name, i);
        if (len > 0) {
            out.append(name, i, len);
            i += len;
            continue;
        }
        const unsigned char c = static_cast<unsigned char>(name[i++]);
        AppendUtf8(out, dos ? Cp437ToUnicode(c) : Cp1252ToUnicode(c));
    }
    return out;
}

} // namespace UltraCanvas
