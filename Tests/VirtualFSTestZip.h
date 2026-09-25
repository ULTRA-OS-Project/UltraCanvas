// Tests/VirtualFSTestZip.h
// A minimal stored (uncompressed) ZIP writer for the VirtualFS tests.
//
// Written by hand rather than through libarchive or miniz, because the tests
// need archives those libraries will not write: names in a legacy code page
// without the UTF-8 flag (VirtualFSNameEncodingTest), and hostile entries -
// "../escape.txt", "/abs.txt", a symbolic link followed by a file inside it -
// (VirtualFSExtractSafetyTest).
// Version: 1.0.0
// Last Modified: 2026-09-24
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace VirtualFSTestZip {

inline uint32_t Crc32(const std::string& data) {
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char c : data) {
        crc ^= c;
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

inline void Put16(std::string& out, uint16_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>(v >> 8));
}

inline void Put32(std::string& out, uint32_t v) {
    Put16(out, static_cast<uint16_t>(v & 0xFFFF));
    Put16(out, static_cast<uint16_t>(v >> 16));
}

struct ZipItem {
    std::string rawName;     // exactly the bytes stored in the header
    bool utf8Flag = false;   // general-purpose bit 11
    std::string data;        // empty for a directory ("name/"); a link's target
    // Unix st_mode (e.g. 0120777 for a symbolic link, 0100644 for a file).
    // 0: an MS-DOS entry, typed by its trailing '/' alone.
    uint32_t unixMode = 0;
};

inline std::string BuildZip(const std::vector<ZipItem>& items) {
    std::string out, central;
    for (const ZipItem& it : items) {
        const uint32_t offset = static_cast<uint32_t>(out.size());
        const uint32_t crc = Crc32(it.data);
        const uint16_t flags = it.utf8Flag ? 0x0800 : 0;
        const uint32_t size = static_cast<uint32_t>(it.data.size());
        const uint16_t nameLen = static_cast<uint16_t>(it.rawName.size());
        const bool isDir = !it.rawName.empty() && it.rawName.back() == '/';

        Put32(out, 0x04034b50);            // local file header
        Put16(out, 20);                    // version needed
        Put16(out, flags);
        Put16(out, 0);                     // stored
        Put16(out, 0); Put16(out, 0x21);   // time, date (1980-01-01)
        Put32(out, crc); Put32(out, size); Put32(out, size);
        Put16(out, nameLen); Put16(out, 0);
        out += it.rawName;
        out += it.data;

        Put32(central, 0x02014b50);        // central directory header
        // Made by: MS-DOS (0) or Unix (3) - the external attributes mean a
        // st_mode only when the entry says it was made on Unix.
        Put16(central, static_cast<uint16_t>(it.unixMode ? (3u << 8) | 20u : 20u));
        Put16(central, 20);
        Put16(central, flags);
        Put16(central, 0);
        Put16(central, 0); Put16(central, 0x21);
        Put32(central, crc); Put32(central, size); Put32(central, size);
        Put16(central, nameLen); Put16(central, 0); Put16(central, 0);
        Put16(central, 0);                 // disk
        Put16(central, 0);                 // internal attributes
        const uint32_t dosAttrs = isDir ? 0x10u : 0x20u;
        Put32(central, it.unixMode ? (it.unixMode << 16) | dosAttrs : dosAttrs);
        Put32(central, offset);
        central += it.rawName;
    }
    const uint32_t centralOffset = static_cast<uint32_t>(out.size());
    out += central;
    Put32(out, 0x06054b50);                // end of central directory
    Put16(out, 0); Put16(out, 0);
    Put16(out, static_cast<uint16_t>(items.size()));
    Put16(out, static_cast<uint16_t>(items.size()));
    Put32(out, static_cast<uint32_t>(central.size()));
    Put32(out, centralOffset);
    Put16(out, 0);
    return out;
}

} // namespace VirtualFSTestZip
