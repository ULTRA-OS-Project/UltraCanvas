// Apps/Texter/UltraCanvasEncoding.h
// Text encoding detection and conversion utilities using POSIX iconv
// Version: 1.0.0

#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace UltraCanvas {

struct EncodingInfo {
    std::string iconvName;    // iconv identifier, e.g. "CP1251"
    std::string displayName;  // User-visible name, e.g. "Windows-1251 (Cyrillic)"
};

struct DetectionResult {
    std::string encoding;  // iconv name of detected encoding
    float confidence;      // 0.0 to 1.0
};

// Returns the list of all supported encodings for the dropdown
std::vector<EncodingInfo> GetSupportedEncodings();

// Returns the index of the given encoding in GetSupportedEncodings(), or -1
int FindEncodingIndex(const std::string& iconvName);

// Detect encoding of raw bytes (BOM check, UTF-8 validation, heuristics)
DetectionResult DetectEncoding(const std::vector<uint8_t>& rawBytes);

// Check for BOM and return encoding name. Sets bomLength to number of BOM bytes.
// Returns empty string if no BOM found.
std::string DetectBOM(const std::vector<uint8_t>& data, size_t& bomLength);

// Validate if raw bytes are valid UTF-8 per RFC 3629
bool IsValidUtf8(const std::vector<uint8_t>& data);

// Convert raw bytes from sourceEncoding to UTF-8
bool ConvertToUtf8(const std::vector<uint8_t>& rawBytes,
                   const std::string& sourceEncoding,
                   std::string& outUtf8);

// Convert UTF-8 string to target encoding
bool ConvertFromUtf8(const std::string& utf8Text,
                     const std::string& targetEncoding,
                     std::vector<uint8_t>& outBytes);

// Max raw bytes to cache per document for re-encoding (10 MB)
static constexpr size_t MAX_RAW_BYTES_CACHE = 10 * 1024 * 1024;

} // namespace UltraCanvas
