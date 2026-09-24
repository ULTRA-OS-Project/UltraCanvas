// include/UltraCanvasTextUtils.h
// Standalone text utilities: trimming, case folding, splitting, and the
// RFC 4648 Base64 / Base32 codecs, and the repair of file names written in a
// legacy code page.
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
// Numbers in machine-readable formats
// ---------------------------------------------------------------------------
// SVG, CSS, OBJ, JSON, DXF and every other format the framework reads write
// their numbers with a '.', always — that is the specification, not a property
// of whoever is running the program. std::stof / strtof / atof do not: they
// honour LC_NUMERIC, and the Linux backend calls setlocale(LC_ALL, "") before
// opening the display (XIM needs LC_CTYPE for UTF-8 input), so on a
// comma-decimal desktop — de_DE, fr_FR, ru_RU, pt_BR and the rest — every one
// of them stops dead at the '.'. An SVG `opacity="0.25"` then reads as 0 and
// the element renders invisible; a `stroke-width="1.5"` becomes 1.
//
// Use these instead of std::stof/strtof/atof wherever the text comes from a
// file format rather than from something the user typed in their own locale.
//
// Why not std::from_chars, which would be the obvious answer: Apple's libc++
// implements the INTEGRAL overloads only, so a float call there resolves to
// the deleted bool overload and does not compile at all.

// Reads the float at the start of [first, last) exactly as a general-format
// std::from_chars would, and returns one past the last character consumed
// (== first when there is no number there, which is how callers detect it).
// `out` is left as the caller set it unless a value was read.
//
// The number is scanned first and converted second, which is what keeps a unit
// beginning like an exponent intact: in "1.5em" the 'e' starts no exponent, so
// the number ends at "1.5" and "em" is left for the caller. Converting the
// whole string in one go (istringstream >> float) instead consumes that 'e'
// and then fails outright.
//
// from_chars parity means no leading '+' and no leading whitespace. Callers
// parsing a format that allows either — SVG path data writes "M+10+20" — want
// TryParseFloat below.
const char* ParseFloatClassic(const char* first, const char* last, float& out);
const char* ParseFloatClassic(const char* first, const char* last, double& out);

// Whole-string front end to the above, and the drop-in replacement for a
// std::stof call: it skips leading whitespace, accepts a leading '+', reads
// the number at the front and ignores any trailing text ("10px" is 10), the
// three things std::stof does that ParseFloatClassic deliberately does not.
// Unlike std::stof it neither throws nor consults the locale — it returns
// false and leaves `out` untouched when the string does not start with a
// number, so a malformed attribute keeps the caller's default instead of
// unwinding out of a parser. A number too large for a float likewise leaves
// `out` alone, but returns true: one was there, it just did not fit.
//
// The overload is picked by the type of `out`, so a caller holding a double
// keeps double precision rather than rounding through a float on the way.
bool TryParseFloat(const std::string& text, float& out);
bool TryParseFloat(const std::string& text, double& out);

// The WRITE side of the same problem, and the worse half: snprintf("%.6g")
// and std::to_string(double) both render through LC_NUMERIC, so on a
// comma-decimal desktop they emit `stroke-width="1,5"` - and inside SVG path
// data a comma is the coordinate separator, so `M 1,5` reads back as the
// point (1, 5) rather than a move to 1.5. A different picture, not a corrupt
// file, which is why it survived so long.
//
// Formats the way "%.<precision>g" does (significant digits, shortest of
// fixed/scientific, no trailing zeros) with the decimal point pinned to '.'.
// Six digits is what the SVG writer settled on: enough to round-trip a float
// through text, short enough not to bloat a path with noise.
//
// Use it for every number that goes INTO a file format or a wire protocol.
// Numbers shown to a person are the opposite case - a German reader expects
// "12,5" - so leave display formatting to the locale.
std::string FormatFloatClassic(double value, int precision = 6);

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

// ---------------------------------------------------------------------------
// Legacy-encoded file names
// ---------------------------------------------------------------------------
// UltraCanvas text is UTF-8, but a file name is whatever bytes the program
// that made it wrote. Two legacy encodings account for nearly every name that
// is not UTF-8:
//   * Windows-1252 / ISO-8859-1 — names written by older Linux tools, Samba or
//     FAT mounts without iocharset=utf8 ("Namens\xE4nderung");
//   * IBM437 / 850 — names out of a ZIP made on Windows and unpacked by a tool
//     that did not re-encode them (ä = 0x84, ü = 0x81, ß = 0xE1).
// Drawn as they are, each such byte becomes U+FFFD and "Namensänderung" reads
// "Namens\uFFFDnderung".

// True when `s` is well-formed UTF-8 (no stray continuation bytes, overlong
// forms, surrogates or code points past U+10FFFF).
bool IsWellFormedUtf8(const std::string& s);

// `name` as UTF-8 text for display. Well-formed UTF-8 — Thai, Cyrillic, CJK,
// anything — is returned unchanged. Otherwise the valid UTF-8 runs are kept
// and every other byte is read in the legacy code page above that makes more
// of those bytes letters inside a word (Windows-1252 on a tie, so a lone
// "\x96" stays an en dash), so both "Namens\xE4nderung"
// and "Namens\x84nderung" come back as "Namensänderung". For display only:
// the file is still reached by its real bytes.
std::string RepairLegacyEncodedName(const std::string& name);

} // namespace UltraCanvas
