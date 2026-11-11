// Apps/UltraDBAdmin/engine/DbAdminBlob.h
// What a blob is, read from its first bytes, so Browse can show it the way it
// is meant to be seen - a picture as a picture, anything else as a hex dump -
// and offer to save it under a fitting extension. Nothing here trusts a file
// name or a column's declared type: the bytes decide.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace UltraDBAdmin {

struct BlobKind {
    std::string name = "Binary data";   // "PNG image", "PDF document", "Text", ...
    std::string extension = "bin";      // without the dot
    bool image = false;                 // a picture the image viewer decodes
    bool text = false;                  // UTF-8 text without control bytes
};

// The kind of `bytes`, by their signature (PNG, JPEG, GIF, BMP, WebP, TIFF,
// SVG, PDF, ZIP, gzip, SQLite) or, failing that, whether they read as text.
BlobKind SniffBlob(const std::vector<uint8_t>& bytes);

// The classic dump: an offset, sixteen bytes in hex, the same as characters
// ("." for one that does not print). At most `maxBytes`; a line says how
// many more there are.
std::string HexDump(const std::vector<uint8_t>& bytes, size_t maxBytes = 64 * 1024);

} // namespace UltraDBAdmin
