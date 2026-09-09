// include/UltraCanvasTextUtils.h
// Standalone text utilities: trimming, case folding, splitting, and the
// RFC 4648 Base64 / Base32 codecs.
//
// This header is part of UltraCanvasUtils — UltraCanvasUtils.h includes it, so
// existing callers need no change — but it is kept in its own file with no
// framework or platform includes. UltraCanvasUtils.cpp is platform glue
// (process launching, path handling, <windows.h>) that a headless module cannot
// compile; everything here is plain C++ over std::string and std::vector. The
// implementation is compiled into the small UltraCanvasTextUtils static
// library, which both the UltraCanvas library and UltraCrypt link, so each
// function exists exactly once even in an application that links both.
//
// The codecs are encodings, not cryptography. They used to be duplicated inside
// UltraCrypt because no other headless home existed and TOTP seeds pass through
// Base32; the one crypto-relevant variant — decoding straight into a zeroizing
// buffer — remains there as UltraCrypt_Base32Decode and calls the decoder below.
//
// Version: 1.0.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {

// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------
// ASCII case folding via std::tolower; bytes outside ASCII pass through, so
// UTF-8 input stays valid UTF-8 (only the ASCII letters change).
std::string ToLowerCase(const std::string& str);

bool StartsWith(const std::string& str, const std::string& prefix);

// Strips every leading and trailing character found in `strippedChars`.
std::string Trim(const std::string& str, const std::string& strippedChars = " \t\r\n");

// Splits on `delimiter`; empty pieces (from adjacent delimiters or a leading /
// trailing one) are dropped.
std::vector<std::string> Split(const std::string& str, char delimiter);

// Whitespace trimming per std::isspace. These return a trimmed copy.
inline std::string LTrimWhitespace(std::string s) {
    std::string result = s;
    // NOTE: iterate `result` consistently. Mixing s.begin() with result.end()
    // walks off the end of a different allocation (heap overflow), since `s`
    // and `result` are distinct string objects.
    result.erase(result.begin(), std::find_if(result.begin(), result.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    return result;
}

inline std::string RTrimWhitespace(std::string s) {
    std::string result = s;
    result.erase(std::find_if(result.rbegin(), result.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), result.end());
    return result;
}

inline std::string TrimWhitespace(std::string s) {
    return LTrimWhitespace(RTrimWhitespace(s));
}

// ---------------------------------------------------------------------------
// Base64 (RFC 4648 §4)
// ---------------------------------------------------------------------------
// Lenient decoder: whitespace and characters outside the alphabet are skipped,
// and decoding stops at the first '='. This is the long-standing contract that
// MIME and document consumers rely on for wrapped, sometimes slightly mangled
// input. It cannot report malformed input; callers needing strictness should
// validate separately.
std::vector<uint8_t> Base64Decode(const std::string& input);

// `wrap` inserts CRLF every 76 output characters, as MIME bodies expect.
std::string Base64Encode(const std::vector<uint8_t>& in, bool wrap = true);

// ---------------------------------------------------------------------------
// Base32 (RFC 4648 §6)
// ---------------------------------------------------------------------------
// `pad` appends '=' to a multiple of eight characters, as the RFC requires.
// otpauth:// URIs conventionally omit the padding, hence the switch.
std::string Base32Encode(const std::vector<uint8_t>& in, bool pad = true);
std::string Base32Encode(const uint8_t* data, size_t size, bool pad = true);

// Strict decoder, unlike Base64Decode above, and deliberately so: its main
// consumer is the otpauth:// parser, which must refuse a malformed seed rather
// than silently produce a different one. Returns false on any character
// outside the alphabet or on data after padding.
//
// On failure `out` holds whatever was decoded before the error, so that a
// caller handling secrets can wipe it; this function cannot zero memory it is
// about to release without a dead-store-proof primitive, and that lives in
// UltraCrypt, which sits above this layer. Callers that do not care should
// simply discard `out`.
//
// Two tolerances, because setup keys are shown for humans to type: letters may
// be either case, and ASCII spaces and tabs are ignored so a key can be
// entered in readable groups. Padding is optional.
bool Base32Decode(const std::string& input, std::vector<uint8_t>& out);

} // namespace UltraCanvas
