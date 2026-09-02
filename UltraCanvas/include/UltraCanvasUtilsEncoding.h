// include/UltraCanvasUtilsEncoding.h
// RFC 4648 text encodings: Base64 and Base32.
//
// Part of UltraCanvasUtils (UltraCanvasUtils.h includes this header), but kept
// in its own file, with no framework includes, so that headless consumers can
// use it without the rest of Utils. UltraCanvasUtils.cpp is platform glue —
// process launching, path handling, <windows.h> — and UltraCrypt must stay
// linkable by tools that never touch the widget layer. The implementation is
// compiled into the tiny UltraCanvasEncoding static library, which both the
// UltraCanvas library and UltraCrypt link, so each codec exists exactly once.
//
// These are encodings, not cryptography. They used to be duplicated inside
// UltraCrypt because no other home existed and TOTP seeds pass through Base32;
// the one crypto-relevant variant — decoding straight into a zeroizing buffer —
// remains there as UltraCrypt_Base32Decode and calls the decoder below.
//
// Version: 1.0.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {

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
