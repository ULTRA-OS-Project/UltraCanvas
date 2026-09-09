// UltraAI/adapters/_shared/src/Base64.cpp
// Implementation of the shared base64 helpers.
// Version: 0.1.0
// Last Modified: 2026-08-24
// Author: UltraAI Module

#include "UltraAIBase64.h"

namespace UltraAI {

namespace {

constexpr const char* kAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// 0-63 for alphabet characters, -1 for anything else. Index by unsigned
// char so high bytes stay in range.
int DecodeChar(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

bool IsSkippable(unsigned char c) {
    return c == '\n' || c == '\r' || c == '\t' || c == ' ';
}

} // namespace

std::string Base64Encode(const std::vector<uint8_t>& bytes) {
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        uint32_t n = (static_cast<uint32_t>(bytes[i]) << 16) |
                     (static_cast<uint32_t>(bytes[i + 1]) << 8) |
                     static_cast<uint32_t>(bytes[i + 2]);
        out += kAlphabet[(n >> 18) & 63]; out += kAlphabet[(n >> 12) & 63];
        out += kAlphabet[(n >> 6) & 63];  out += kAlphabet[n & 63];
    }
    if (i + 1 == bytes.size()) {
        uint32_t n = static_cast<uint32_t>(bytes[i]) << 16;
        out += kAlphabet[(n >> 18) & 63]; out += kAlphabet[(n >> 12) & 63];
        out += "==";
    } else if (i + 2 == bytes.size()) {
        uint32_t n = (static_cast<uint32_t>(bytes[i]) << 16) |
                     (static_cast<uint32_t>(bytes[i + 1]) << 8);
        out += kAlphabet[(n >> 18) & 63]; out += kAlphabet[(n >> 12) & 63];
        out += kAlphabet[(n >> 6) & 63];  out += '=';
    }
    return out;
}

std::string Base64Encode(const std::string& bytes) {
    return Base64Encode(std::vector<uint8_t>(bytes.begin(), bytes.end()));
}

std::vector<uint8_t> Base64Decode(const std::string& text, bool* outOk) {
    std::vector<uint8_t> out;
    out.reserve((text.size() / 4) * 3);

    uint32_t accumulator = 0;
    int bitsHeld = 0;
    int padding  = 0;
    for (char raw : text) {
        const unsigned char c = static_cast<unsigned char>(raw);
        if (IsSkippable(c)) continue;
        if (c == '=') { ++padding; continue; }
        const int value = DecodeChar(c);
        // A character after padding, or outside the alphabet, is malformed.
        if (value < 0 || padding > 0) {
            if (outOk) *outOk = false;
            return {};
        }
        accumulator = (accumulator << 6) | static_cast<uint32_t>(value);
        bitsHeld += 6;
        if (bitsHeld >= 8) {
            bitsHeld -= 8;
            out.push_back(static_cast<uint8_t>((accumulator >> bitsHeld) & 0xFF));
        }
    }
    // Leftover bits must be zero padding, never a truncated byte.
    if (bitsHeld >= 6 || (accumulator & ((1u << bitsHeld) - 1)) != 0) {
        if (outOk) *outOk = false;
        return {};
    }
    if (outOk) *outOk = true;
    return out;
}

std::string Base64DataUrl(const std::string& mimeType,
                          const std::vector<uint8_t>& bytes) {
    return "data:" + mimeType + ";base64," + Base64Encode(bytes);
}

} // namespace UltraAI
