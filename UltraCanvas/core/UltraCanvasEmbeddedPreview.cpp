// core/UltraCanvasEmbeddedPreview.cpp
// Extracts the preview bitmap a vector document stores inside itself - see
// UltraCanvasEmbeddedPreview.h for what this is for.
// Version: 1.1.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework
#include "UltraCanvasEmbeddedPreview.h"
#include "UltraCanvasUtils.h"     // PathFromUtf8: paths are UTF-8 everywhere
#include "UltraCanvasZipPackage.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>

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

        // PostScript documents. ".ai" is here because every Illustrator file
        // up to CS was an EPS; the PDF-compatible ones written since carry no
        // EPS preview and simply read as "no preview" (a PDF renderer takes
        // those over).
        bool IsPostScriptExtension(const std::string& ext) {
            return ext == "eps" || ext == "epsf" || ext == "epsi" ||
                   ext == "ps"  || ext == "ai";
        }

        // ===== XARA (.xar / .web / .wix) =====
        // The record grammar of the uncompressed file head is trivial -
        // 8-byte signature, then (tag:u32le, size:u32le, body) - so the
        // preview bytes are lifted out directly: no XAR renderer involved,
        // and nothing to configure.
        std::vector<uint8_t> ExtractXaraPreview(const std::string& path) {
            // Paths are UTF-8 throughout the framework; on Windows a plain
            // std::ifstream(std::string) would interpret them in the ANSI code
            // page and fail to open anything outside it.
            std::ifstream f(PathFromUtf8(path), std::ios::binary);
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

        // ===== POSTSCRIPT (.eps / .epsf / .epsi / .ps / old .ai) =====
        // Two ways a PostScript document carries its preview, both defined by
        // the EPSF specification:
        //
        //   * a DOS EPS binary header - a 30-byte record in front of the
        //     PostScript itself, holding the offset and length of a TIFF
        //     (and/or a Windows Metafile) preview of the page. The TIFF is a
        //     complete file, so it is lifted out as it is; the metafile is
        //     skipped, there being no metafile decoder to hand it to.
        //   * an EPSI preview - "%%BeginPreview: <width> <height> <depth>
        //     <lines>" in the comment block, followed by that many comment
        //     lines of hexadecimal samples. The samples are ink coverage
        //     (0 = white, the maximum = black), which is turned into an
        //     8-bit greyscale PGM below so the image pipeline can read it.
        //
        // A file with neither - a plain ASCII EPS - has no preview to give;
        // rendering one needs a PostScript interpreter.
        std::vector<uint8_t> ExtractDosEpsTiffPreview(std::ifstream& f) {
            // C5 D0 D3 C6, then five little-endian u32: PostScript offset and
            // length, WMF offset and length, TIFF offset and length.
            constexpr uint32_t kMaxPreviewBytes = 64u << 20;
            f.seekg(0);
            uint8_t hdr[30];
            f.read(reinterpret_cast<char*>(hdr), sizeof(hdr));
            if (!f.good()) return {};
            if (hdr[0] != 0xC5 || hdr[1] != 0xD0 ||
                hdr[2] != 0xD3 || hdr[3] != 0xC6) return {};
            auto u32 = [&hdr](int o) {
                return static_cast<uint32_t>(hdr[o]) |
                       (static_cast<uint32_t>(hdr[o + 1]) << 8) |
                       (static_cast<uint32_t>(hdr[o + 2]) << 16) |
                       (static_cast<uint32_t>(hdr[o + 3]) << 24);
            };
            const uint32_t tiffOffset = u32(20), tiffLength = u32(24);
            if (tiffOffset < sizeof(hdr) || tiffLength == 0 ||
                tiffLength > kMaxPreviewBytes) return {};
            f.seekg(tiffOffset);
            std::vector<uint8_t> bytes(tiffLength);
            f.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
            if (!f.good()) return {};
            return bytes;
        }

        int HexValue(char c) {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        // The EPSI preview as an 8-bit greyscale PGM (P5). `startOffset` is
        // where in the file to begin looking - past the DOS header when there
        // is one - and the search is bounded because the preview, when there
        // is one, sits in the comment block at the head of the document.
        std::vector<uint8_t> ExtractEpsiPreview(std::ifstream& f,
                                                std::streamoff startOffset) {
            constexpr size_t kMaxHeadBytes = 1u << 20;   // comment block only
            constexpr long   kMaxEdge = 8192;            // sanity, not a spec limit
            f.clear();
            f.seekg(startOffset);
            std::string line;
            long width = 0, height = 0, depth = 0, lines = 0;
            size_t read = 0;
            bool found = false;
            while (read < kMaxHeadBytes && std::getline(f, line)) {
                read += line.size() + 1;
                // The preview sits between the header comments and the
                // program itself (ps2epsi writes it right after
                // %%EndComments), so anything that opens the program means
                // there is none - and a whole PostScript file need not be
                // read to find that out.
                if (line.compare(0, 13, "%%BeginProlog") == 0 ||
                    line.compare(0, 12, "%%BeginSetup") == 0 ||
                    line.compare(0, 7,  "%%Page:") == 0) return {};
                if (line.compare(0, 15, "%%BeginPreview:") != 0) continue;
                std::istringstream is(line.substr(15));
                if (!(is >> width >> height >> depth >> lines)) return {};
                found = true;
                break;
            }
            if (!found) return {};
            if (width <= 0 || height <= 0 || lines <= 0 ||
                width > kMaxEdge || height > kMaxEdge) return {};
            if (depth != 1 && depth != 2 && depth != 4 && depth != 8) return {};

            const size_t pixelCount = static_cast<size_t>(width) *
                                      static_cast<size_t>(height);
            std::vector<uint8_t> gray;
            gray.reserve(pixelCount);
            const int maxSample = (1 << depth) - 1;
            const int perByte = 8 / static_cast<int>(depth);

            // Samples run row by row, each row starting on a byte boundary,
            // so the padding bits at the end of a row are dropped rather than
            // carried into the next one.
            long row = 0;
            long column = 0;
            int nibbleHigh = -1;
            auto pushByte = [&](uint8_t byte) {
                for (int i = 0; i < perByte && column < width; ++i, ++column) {
                    const int shift = 8 - static_cast<int>(depth) * (i + 1);
                    const int sample = (byte >> shift) & maxSample;
                    // Ink coverage, so the maximum is black.
                    gray.push_back(static_cast<uint8_t>(
                            255 - (sample * 255 + maxSample / 2) / maxSample));
                }
            };
            while (row < height && std::getline(f, line)) {
                if (line.compare(0, 12, "%%EndPreview") == 0) break;
                if (line.empty() || line[0] != '%') continue;
                nibbleHigh = -1;
                for (size_t i = 1; i < line.size(); ++i) {
                    const int v = HexValue(line[i]);
                    if (v < 0) continue;
                    if (nibbleHigh < 0) { nibbleHigh = v; continue; }
                    pushByte(static_cast<uint8_t>((nibbleHigh << 4) | v));
                    nibbleHigh = -1;
                    if (column >= width) break;
                }
                if (column >= width) { column = 0; ++row; }
            }
            if (gray.size() < pixelCount) {
                if (gray.empty()) return {};
                gray.resize(pixelCount, 255);   // a truncated preview ends white
            }

            std::string header = "P5\n" + std::to_string(width) + " " +
                                 std::to_string(height) + "\n255\n";
            std::vector<uint8_t> pgm(header.begin(), header.end());
            pgm.insert(pgm.end(), gray.begin(), gray.begin() + pixelCount);
            return pgm;
        }

        std::vector<uint8_t> ExtractPostScriptPreview(const std::string& path) {
            std::ifstream f(PathFromUtf8(path), std::ios::binary);
            if (!f.is_open()) return {};
            // The DOS header, when present, names where the PostScript starts;
            // an EPSI preview then lives inside that section.
            std::streamoff epsiStart = 0;
            uint8_t sig[4];
            f.read(reinterpret_cast<char*>(sig), sizeof(sig));
            const bool dosHeader = f.good() && sig[0] == 0xC5 && sig[1] == 0xD0 &&
                                   sig[2] == 0xD3 && sig[3] == 0xC6;
            if (dosHeader) {
                std::vector<uint8_t> tiff = ExtractDosEpsTiffPreview(f);
                if (!tiff.empty()) return tiff;
                f.clear();
                f.seekg(4);
                uint8_t off[4];
                f.read(reinterpret_cast<char*>(off), sizeof(off));
                if (f.good())
                    epsiStart = static_cast<std::streamoff>(
                            static_cast<uint32_t>(off[0]) |
                            (static_cast<uint32_t>(off[1]) << 8) |
                            (static_cast<uint32_t>(off[2]) << 16) |
                            (static_cast<uint32_t>(off[3]) << 24));
            }
            return ExtractEpsiPreview(f, epsiStart);
        }

    } // namespace

    bool FormatCarriesEmbeddedPreview(const std::string& pathOrExtension) {
        const std::string ext = LowerExtensionOf(pathOrExtension);
        return IsXaraExtension(ext) || IsCorelDrawExtension(ext) ||
               IsPostScriptExtension(ext);
    }

    std::vector<uint8_t> ExtractEmbeddedPreviewBytes(const std::string& filePath) {
        if (filePath.empty()) return {};
        const std::string ext = LowerExtensionOf(filePath);
        try {
            if (IsXaraExtension(ext)) return ExtractXaraPreview(filePath);
            if (IsCorelDrawExtension(ext)) return ExtractCorelDrawPreview(filePath);
            if (IsPostScriptExtension(ext)) return ExtractPostScriptPreview(filePath);
        } catch (...) {
            // A malformed document must cost that file its preview, nothing
            // more - the callers are background workers.
            return {};
        }
        return {};
    }

} // namespace UltraCanvas
