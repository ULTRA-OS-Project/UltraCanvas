// Apps/UltraDBAdmin/engine/DbAdminBlob.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "DbAdminBlob.h"

#include "DbAdminFormat.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace UltraDBAdmin {
namespace {

bool StartsWith(const std::vector<uint8_t>& bytes, const char* signature, size_t at = 0) {
    const size_t n = std::strlen(signature);
    return bytes.size() >= at + n && std::memcmp(bytes.data() + at, signature, n) == 0;
}

// UTF-8 throughout, and no control byte but tab, line feed and carriage return.
bool ReadsAsText(const std::vector<uint8_t>& bytes) {
    size_t i = 0;
    while (i < bytes.size()) {
        const uint8_t c = bytes[i];
        if (c < 0x80) {
            if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') return false;
            if (c == 0x7F) return false;
            ++i;
            continue;
        }
        // A lead byte and its continuation bytes, all present.
        const size_t more = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 0;
        if (more == 0) return false;
        for (size_t k = 1; k <= more; ++k)
            if (i + k >= bytes.size() || (bytes[i + k] & 0xC0) != 0x80) return false;
        i += more + 1;
    }
    return true;
}

BlobKind Kind(const char* name, const char* extension, bool image = false, bool text = false) {
    BlobKind kind;
    kind.name = name;
    kind.extension = extension;
    kind.image = image;
    kind.text = text;
    return kind;
}

} // namespace

BlobKind SniffBlob(const std::vector<uint8_t>& bytes) {
    if (StartsWith(bytes, "\x89PNG\r\n\x1a\n")) return Kind("PNG image", "png", true);
    if (StartsWith(bytes, "\xff\xd8\xff")) return Kind("JPEG image", "jpg", true);
    if (StartsWith(bytes, "GIF87a") || StartsWith(bytes, "GIF89a")) return Kind("GIF image", "gif", true);
    if (StartsWith(bytes, "BM") && bytes.size() > 14) return Kind("BMP image", "bmp", true);
    if (StartsWith(bytes, "RIFF") && StartsWith(bytes, "WEBP", 8)) return Kind("WebP image", "webp", true);
    if (StartsWith(bytes, "II*") || StartsWith(bytes, "MM")) {
        if (bytes.size() > 3 && (bytes[2] == 0x2A || bytes[3] == 0x2A)) return Kind("TIFF image", "tif", true);
    }
    if (StartsWith(bytes, "%PDF-")) return Kind("PDF document", "pdf");
    if (StartsWith(bytes, "PK\x03\x04")) return Kind("ZIP archive", "zip");
    if (StartsWith(bytes, "\x1f\x8b")) return Kind("gzip data", "gz");
    if (StartsWith(bytes, "SQLite format 3")) return Kind("SQLite database", "db");
    if (!bytes.empty() && ReadsAsText(bytes)) {
        // An SVG is text that holds an <svg element near its start.
        const size_t look = std::min<size_t>(bytes.size(), 1024);
        const std::string head(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(look));
        if (head.find("<svg") != std::string::npos) return Kind("SVG image", "svg", true, true);
        return Kind("Text", "txt", false, true);
    }
    return BlobKind();
}

std::string HexDump(const std::vector<uint8_t>& bytes, size_t maxBytes) {
    const size_t shown = std::min(bytes.size(), maxBytes);
    std::string out;
    out.reserve(shown / 16 * 78 + 80);
    char cell[16];
    for (size_t line = 0; line < shown; line += 16) {
        std::snprintf(cell, sizeof cell, "%08zx  ", line);
        out += cell;
        std::string chars;
        for (size_t i = 0; i < 16; ++i) {
            if (line + i < shown) {
                const uint8_t b = bytes[line + i];
                std::snprintf(cell, sizeof cell, "%02x ", b);
                out += cell;
                chars += (b >= 0x20 && b < 0x7F) ? static_cast<char>(b) : '.';
            } else {
                out += "   ";
            }
            if (i == 7) out += ' ';
        }
        out += " |" + chars + "|\n";
    }
    if (shown < bytes.size())
        out += "... " + HumanSize(static_cast<int64_t>(bytes.size() - shown)) + " more, not shown\n";
    return out;
}

} // namespace UltraDBAdmin
