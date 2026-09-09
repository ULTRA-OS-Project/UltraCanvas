// UltraAI/adapters/_shared/include/UltraAIBase64.h
// Standard base64 (RFC 4648, no line breaks) shared by adapters that put
// binary payloads on the wire: inline media data URLs, ComfyUI image
// uploads, MiniMax base64 image responses, cassette binary frames.
// Version: 0.1.0
// Last Modified: 2026-08-24
// Author: UltraAI Module
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UltraAI {

// Encode raw bytes. Padded with '=' to a multiple of four characters.
std::string Base64Encode(const std::vector<uint8_t>& bytes);
std::string Base64Encode(const std::string& bytes);

// Decode. Whitespace is ignored; any other character outside the alphabet
// makes the input invalid and yields an empty result with *outOk = false.
std::vector<uint8_t> Base64Decode(const std::string& text, bool* outOk = nullptr);

// "data:<mimeType>;base64,<payload>" — the form both OpenAI-compatible
// chat APIs and several image providers accept for inline media.
std::string Base64DataUrl(const std::string& mimeType,
                          const std::vector<uint8_t>& bytes);

} // namespace UltraAI
