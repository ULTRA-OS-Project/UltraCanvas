// core/UltraCanvasTextUtils.cpp
// Standalone text utilities. See the header for why this is a separate file.
//
// Everything here is moved verbatim: the string helpers and the Base64 pair
// from UltraCanvasUtils.cpp, the Base32 pair from UltraCryptCore.cpp (which
// previously carried its own copy of both codecs). Behaviour is unchanged.
//
// Version: 1.0.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCanvasTextUtils.h"

#include <sstream>

namespace UltraCanvas {

namespace {

const char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const char kBase32Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

int Base32Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a';      // case-insensitive
    if (c >= '2' && c <= '7') return c - '2' + 26;
    return -1;
}

} // namespace

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
// Base32
// ===========================================================================

std::string Base32Encode(const uint8_t* data, size_t size, bool pad) {
    std::string out;
    if (!data || size == 0) return out;
    uint32_t accumulator = 0;
    int bits = 0;
    for (size_t i = 0; i < size; ++i) {
        accumulator = (accumulator << 8) | data[i];
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            out.push_back(kBase32Alphabet[(accumulator >> bits) & 0x1F]);
        }
    }
    if (bits > 0) {
        out.push_back(kBase32Alphabet[(accumulator << (5 - bits)) & 0x1F]);
    }
    if (pad) {
        while (out.size() % 8 != 0) out.push_back('=');
    }
    return out;
}

std::string Base32Encode(const std::vector<uint8_t>& in, bool pad) {
    return Base32Encode(in.data(), in.size(), pad);
}

bool Base32Decode(const std::string& input, std::vector<uint8_t>& out) {
    out.clear();
    uint32_t accumulator = 0;
    int bits = 0;
    bool sawPadding = false;

    for (char c : input) {
        if (c == ' ' || c == '\t') continue;      // keys are often typed in groups
        if (c == '=') { sawPadding = true; continue; }
        if (sawPadding) return false;              // data after padding
        const int value = Base32Value(c);
        if (value < 0) return false;               // outside the alphabet
        accumulator = (accumulator << 5) | static_cast<uint32_t>(value);
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((accumulator >> bits) & 0xFF));
        }
    }
    return true;
}

} // namespace UltraCanvas
