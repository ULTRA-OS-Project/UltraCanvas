// core/UltraCanvasBase32.cpp
// The Base32 codec, kept in its own translation unit and its own static library
// for a linkage reason, not a stylistic one.
//
// Every other text helper here — the string helpers and Base64 — is linked into
// libultracanvas, so a SHARED core exports them. Base32 has exactly one
// consumer, UltraCrypt, which is deliberately UI-free and must not link the
// widget layer. While Base32 shared an object with those symbols, linking
// UltraCrypt dragged the whole object onto the line and its copies collided
// with the ones the shared core already exports — a hard link error on PE/COFF
// ("multiple definition of UltraCanvas::Trim").
//
// Splitting by *link-time home* rather than by kind is what makes this work:
// what the core owns stays with the core, and the one symbol only UltraCrypt
// needs lives here, where nothing else can duplicate it.
//
// The declarations stay in UltraCanvasTextUtils.h, so callers are unaffected.
// Behaviour is unchanged — the code below is moved verbatim.
//
// Version: 1.0.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCanvasTextUtils.h"

namespace UltraCanvas {

namespace {

const char kBase32Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

int Base32Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a';      // case-insensitive
    if (c >= '2' && c <= '7') return c - '2' + 26;
    return -1;
}

} // namespace

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
