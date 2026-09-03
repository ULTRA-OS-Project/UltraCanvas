// core/UltraCanvasEmbeddedPreview.cpp
// Extracts the preview bitmap a vector document stores inside itself - see
// UltraCanvasEmbeddedPreview.h for what this is for.
// Version: 1.0.0
// Last Modified: 2026-09-02
// Author: UltraCanvas Framework
#include "UltraCanvasEmbeddedPreview.h"
#include "UltraCanvasZipPackage.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

namespace UltraCanvas {

    namespace {

        std::string LowerExtensionOf(const std::string& pathOrExtension) {
            // A bare extension ("xar", ".XAR") has no separator to cut at, so
            // the whole string is the extension in that case.
            const size_t slash = pathOrExtension.find_last_of("/\\");
            const std::string name = slash == std::string::npos
                                             ? pathOrExtension
                                             : pathOrExtension.substr(slash + 1);
            const size_t dot = name.find_last_of('.');
            std::string ext = (dot == std::string::npos) ? name
                                                         : name.substr(dot + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return ext;
        }

        bool IsXaraExtension(const std::string& ext) {
            return ext == "xar" || ext == "web" || ext == "wix";
        }

        bool IsCorelDrawExtension(const std::string& ext) {
            return ext == "cdr" || ext == "cdt";
        }

        // ===== XARA (.xar / .web / .wix) =====
        // The record grammar of the uncompressed file head is trivial -
        // 8-byte signature, then (tag:u32le, size:u32le, body) - so the
        // preview bytes are lifted out directly: no XAR renderer involved,
        // and nothing to configure.
        std::vector<uint8_t> ExtractXaraPreview(const std::string& path) {
            std::ifstream f(path, std::ios::binary);
            if (!f.is_open()) return {};
            static const uint8_t kSig[8] = {'X', 'A', 'R', 'A', 0xA3, 0xA3, 0x0D, 0x0A};
            uint8_t sig[8];
            f.read(reinterpret_cast<char*>(sig), sizeof(sig));
            if (!f.good() || std::memcmp(sig, kSig, sizeof(kSig)) != 0) return {};

            // Preview records: 61 GIF, 62 JPEG, 63 PNG. Record 30 starts the
            // compressed body - the preview always precedes it, so stop there
            // (and at 3, end of file). The record cap is a corrupt-file guard;
            // real writers put the preview second, right after the header.
            constexpr uint32_t kPreviewGif = 61, kPreviewJpeg = 62, kPreviewPng = 63;
            constexpr uint32_t kEndOfFile = 3, kStartCompression = 30;
            constexpr uint32_t kMaxPreviewBytes = 64u << 20;
            for (int i = 0; i < 64; ++i) {
                uint8_t hdr[8];
                f.read(reinterpret_cast<char*>(hdr), sizeof(hdr));
                if (!f.good()) return {};
                auto u32 = [&](int o) {
                    return static_cast<uint32_t>(hdr[o]) |
                           (static_cast<uint32_t>(hdr[o + 1]) << 8) |
                           (static_cast<uint32_t>(hdr[o + 2]) << 16) |
                           (static_cast<uint32_t>(hdr[o + 3]) << 24);
                };
                const uint32_t tag = u32(0), size = u32(4);
                if (tag == kPreviewGif || tag == kPreviewJpeg || tag == kPreviewPng) {
                    if (size == 0 || size > kMaxPreviewBytes) return {};
                    std::vector<uint8_t> bytes(size);
                    f.read(reinterpret_cast<char*>(bytes.data()), size);
                    if (!f.good()) return {};
                    return bytes;
                }
                if (tag == kEndOfFile || tag == kStartCompression) return {};
                f.seekg(size, std::ios::cur);
                if (!f.good()) return {};
            }
            return {};
        }

        // ===== CORELDRAW (.cdr / .cdt), X4 and newer =====
        // Those are ZIP containers holding "previews/thumbnail.png" (and one
        // PNG per page). The older RIFF-based .cdr files carry no entry this
        // can reach and read as "no preview" - UCZipPackageReader::Open
        // rejects them.
        std::vector<uint8_t> ExtractCorelDrawPreview(const std::string& path) {
            UCZipPackageReader zip;
            if (!zip.Open(path)) return {};
            static const char* const kEntries[] = {
                "previews/thumbnail.png",
                "previews/page1.png",
            };
            for (const char* entry : kEntries) {
                std::vector<uint8_t> bytes;
                if (zip.HasEntry(entry) && zip.ReadEntry(entry, bytes) && !bytes.empty())
                    return bytes;
            }
            return {};
        }

    } // namespace

    bool FormatCarriesEmbeddedPreview(const std::string& pathOrExtension) {
        const std::string ext = LowerExtensionOf(pathOrExtension);
        return IsXaraExtension(ext) || IsCorelDrawExtension(ext);
    }

    std::vector<uint8_t> ExtractEmbeddedPreviewBytes(const std::string& filePath) {
        if (filePath.empty()) return {};
        const std::string ext = LowerExtensionOf(filePath);
        try {
            if (IsXaraExtension(ext)) return ExtractXaraPreview(filePath);
            if (IsCorelDrawExtension(ext)) return ExtractCorelDrawPreview(filePath);
        } catch (...) {
            // A malformed document must cost that file its preview, nothing
            // more - the callers are background workers.
            return {};
        }
        return {};
    }

} // namespace UltraCanvas
