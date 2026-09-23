// core/UltraCanvasTextUtils.cpp
// Standalone text utilities. See the header for why this is a separate file.
//
// The string helpers and Base64, moved verbatim from UltraCanvasUtils.cpp.
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

} // namespace UltraCanvas
