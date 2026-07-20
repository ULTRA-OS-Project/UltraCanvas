// core/UltraCanvasFilerWidget.cpp
// Filer folder widget: displays one folder's content with selectable view types
// (details, list, thumbnails, size bars, treemap), sorting, an inline rename
// editor, a hover icon menu, the full file context menu and a selection info
// bar (type / size / dates / attributes, image dimensions, media duration and
// codec via lightweight header probes, recursive folder stats). Image
// thumbnails decode asynchronously (see ASYNC THUMBNAILS) so the folder page
// never waits for image files.
// Files drag out to other windows / applications via the native OS drag &
// drop, external drops are copied into the shown folder, and Copy / Cut /
// Paste go through the system clipboard so files can be exchanged with other
// programs (external file managers, editors, ...).
// Version: 1.4.0
// Last Modified: 2026-07-20
// Author: UltraCanvas Framework

// VirtualFS + bridge must be included before the UI headers: X11 (pulled in
// via UltraCanvasApplication.h) #defines None/Success, which collide with the
// None enumerators in VirtualFS and UCVFSCompressionType.
#ifdef ULTRACANVAS_HAS_VIRTUALFS
#include "VirtualFS/VirtualFS.h"
#include "UltraCanvasVirtualFSBridge.h"
#endif

#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasImage.h"
#include "../libspecific/Cairo/QoiPixmapCodec.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasTooltipManager.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sys/stat.h>

namespace fs = std::filesystem;

namespace UltraCanvas {

    std::vector<std::string> UltraCanvasFilerWidget::clipboardPaths;
    bool UltraCanvasFilerWidget::clipboardCut = false;

    namespace {
        constexpr int kWheelStep = 64;                 // px per wheel notch
        constexpr uint64_t kDirSizeEntryCap = 50000;   // recursive-size safety cap
        constexpr int kDragStartSlop = 5;              // px before a press becomes a drag-out

        int clampi(int v, int lo, int hi) {
            return v < lo ? lo : (v > hi ? hi : v);
        }

        int CompareNoCase(const std::string& a, const std::string& b) {
            size_t n = std::min(a.size(), b.size());
            for (size_t i = 0; i < n; ++i) {
                int ca = std::tolower(static_cast<unsigned char>(a[i]));
                int cb = std::tolower(static_cast<unsigned char>(b[i]));
                if (ca != cb) return ca < cb ? -1 : 1;
            }
            if (a.size() == b.size()) return 0;
            return a.size() < b.size() ? -1 : 1;
        }

        // Extension -> (type label, category). The label is completed to
        // "<LABEL> <category noun>" ("PNG Image") in ApplyEntryTypeInfo.
        struct TypeInfo {
            const char* label;
            FilerFileCategory category;
        };

        const std::map<std::string, TypeInfo>& ExtensionTypeMap() {
            static const std::map<std::string, TypeInfo> m = {
                {"png",  {"PNG",  FilerFileCategory::Image}},
                {"jpg",  {"JPEG", FilerFileCategory::Image}},
                {"jpeg", {"JPEG", FilerFileCategory::Image}},
                {"gif",  {"GIF",  FilerFileCategory::Image}},
                {"bmp",  {"BMP",  FilerFileCategory::Image}},
                {"webp", {"WebP", FilerFileCategory::Image}},
                {"avif", {"AVIF", FilerFileCategory::Image}},
                {"heif", {"HEIF", FilerFileCategory::Image}},
                {"heic", {"HEIC", FilerFileCategory::Image}},
                {"tif",  {"TIFF", FilerFileCategory::Image}},
                {"tiff", {"TIFF", FilerFileCategory::Image}},
                {"qoi",  {"QOI",  FilerFileCategory::Image}},
                {"ico",  {"Icon", FilerFileCategory::Image}},
                {"svg",  {"SVG",  FilerFileCategory::Vector}},
                {"eps",  {"EPS",  FilerFileCategory::Vector}},
                {"cdr",  {"CorelDRAW", FilerFileCategory::Vector}},
                {"xar",  {"Xara", FilerFileCategory::Vector}},
                {"mp3",  {"MP3",  FilerFileCategory::Audio}},
                {"wav",  {"WAV",  FilerFileCategory::Audio}},
                {"flac", {"FLAC", FilerFileCategory::Audio}},
                {"ogg",  {"OGG",  FilerFileCategory::Audio}},
                {"m4a",  {"M4A",  FilerFileCategory::Audio}},
                {"aac",  {"AAC",  FilerFileCategory::Audio}},
                {"opus", {"Opus", FilerFileCategory::Audio}},
                {"mp4",  {"MP4",  FilerFileCategory::Video}},
                {"mkv",  {"MKV",  FilerFileCategory::Video}},
                {"avi",  {"AVI",  FilerFileCategory::Video}},
                {"mov",  {"QuickTime", FilerFileCategory::Video}},
                {"webm", {"WebM", FilerFileCategory::Video}},
                {"wmv",  {"WMV",  FilerFileCategory::Video}},
                {"pdf",  {"PDF",  FilerFileCategory::Document}},
                {"odt",  {"OpenDocument", FilerFileCategory::Document}},
                {"doc",  {"Word", FilerFileCategory::Document}},
                {"docx", {"Word", FilerFileCategory::Document}},
                {"rtf",  {"RTF",  FilerFileCategory::Document}},
                {"md",   {"Markdown", FilerFileCategory::Document}},
                {"html", {"HTML", FilerFileCategory::Document}},
                {"htm",  {"HTML", FilerFileCategory::Document}},
                {"tex",  {"LaTeX", FilerFileCategory::Document}},
                {"epub", {"EPUB", FilerFileCategory::Document}},
                {"txt",  {"Text", FilerFileCategory::Text}},
                {"log",  {"Log",  FilerFileCategory::Text}},
                {"ini",  {"Config", FilerFileCategory::Text}},
                {"conf", {"Config", FilerFileCategory::Text}},
                {"json", {"JSON", FilerFileCategory::Text}},
                {"xml",  {"XML",  FilerFileCategory::Text}},
                {"yaml", {"YAML", FilerFileCategory::Text}},
                {"yml",  {"YAML", FilerFileCategory::Text}},
                {"csv",  {"CSV",  FilerFileCategory::Text}},
                {"cpp",  {"C++ Source", FilerFileCategory::Text}},
                {"cc",   {"C++ Source", FilerFileCategory::Text}},
                {"h",    {"C Header", FilerFileCategory::Text}},
                {"hpp",  {"C++ Header", FilerFileCategory::Text}},
                {"c",    {"C Source", FilerFileCategory::Text}},
                {"py",   {"Python", FilerFileCategory::Text}},
                {"js",   {"JavaScript", FilerFileCategory::Text}},
                {"ts",   {"TypeScript", FilerFileCategory::Text}},
                {"sh",   {"Shell Script", FilerFileCategory::Text}},
                {"ods",  {"OpenDocument", FilerFileCategory::Spreadsheet}},
                {"xls",  {"Excel", FilerFileCategory::Spreadsheet}},
                {"xlsx", {"Excel", FilerFileCategory::Spreadsheet}},
                {"zip",  {"ZIP",  FilerFileCategory::Archive}},
                {"7z",   {"7-Zip", FilerFileCategory::Archive}},
                {"rar",  {"RAR",  FilerFileCategory::Archive}},
                {"tar",  {"TAR",  FilerFileCategory::Archive}},
                {"gz",   {"GZip", FilerFileCategory::Archive}},
                {"tgz",  {"TAR GZip", FilerFileCategory::Archive}},
                {"bz2",  {"BZip2", FilerFileCategory::Archive}},
                {"xz",   {"XZ",   FilerFileCategory::Archive}},
                {"zst",  {"Zstandard", FilerFileCategory::Archive}},
                {"jar",  {"Java Archive", FilerFileCategory::Archive}},
                {"exe",  {"Executable", FilerFileCategory::Executable}},
                {"appimage", {"AppImage", FilerFileCategory::Executable}},
                {"deb",  {"Debian Package", FilerFileCategory::Executable}},
                {"rpm",  {"RPM Package", FilerFileCategory::Executable}},
                {"so",   {"Shared Library", FilerFileCategory::Executable}},
                {"dll",  {"Library", FilerFileCategory::Executable}},
            };
            return m;
        }

        const char* CategoryNoun(FilerFileCategory c) {
            switch (c) {
                case FilerFileCategory::Folder:      return "Folder";
                case FilerFileCategory::Image:       return "Image";
                case FilerFileCategory::Vector:      return "Vector";
                case FilerFileCategory::Audio:       return "Audio";
                case FilerFileCategory::Video:       return "Video";
                case FilerFileCategory::Document:    return "Document";
                case FilerFileCategory::Text:        return "Text";
                case FilerFileCategory::Spreadsheet: return "Spreadsheet";
                case FilerFileCategory::Archive:     return "Archive";
                case FilerFileCategory::Executable:  return "Program";
                default:                             return "File";
            }
        }

        Color CategoryColor(FilerFileCategory c) {
            switch (c) {
                case FilerFileCategory::Folder:      return Color(247, 190, 80, 255);
                case FilerFileCategory::Image:       return Color(76, 175, 130, 255);
                case FilerFileCategory::Vector:      return Color(0, 150, 167, 255);
                case FilerFileCategory::Audio:       return Color(156, 89, 182, 255);
                case FilerFileCategory::Video:       return Color(230, 106, 86, 255);
                case FilerFileCategory::Document:    return Color(66, 133, 244, 255);
                case FilerFileCategory::Text:        return Color(120, 144, 156, 255);
                case FilerFileCategory::Spreadsheet: return Color(46, 125, 50, 255);
                case FilerFileCategory::Archive:     return Color(141, 110, 99, 255);
                case FilerFileCategory::Executable:  return Color(84, 110, 122, 255);
                default:                             return Color(158, 158, 158, 255);
            }
        }

        std::string FormatSize(uint64_t bytes) {
            char buf[32];
            if (bytes < 1024) {
                snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
            } else if (bytes < 1024ull * 1024) {
                snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
            } else if (bytes < 1024ull * 1024 * 1024) {
                snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024));
            } else {
                snprintf(buf, sizeof(buf), "%.2f GB", bytes / (1024.0 * 1024 * 1024));
            }
            return buf;
        }

        std::string FormatTime(std::time_t t) {
            if (t == 0) return "";
            char buf[32];
            std::tm tmv{};
#ifdef _WIN32
            localtime_s(&tmv, &t);
#else
            localtime_r(&t, &tmv);
#endif
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv);
            return buf;
        }

        std::time_t ParseIso8601(const std::string& s) {
            int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
            if (sscanf(s.c_str(), "%d-%d-%d%*c%d:%d:%d", &y, &mo, &d, &h, &mi, &se) < 3)
                return 0;
            std::tm tmv{};
            tmv.tm_year = y - 1900; tmv.tm_mon = mo - 1; tmv.tm_mday = d;
            tmv.tm_hour = h; tmv.tm_min = mi; tmv.tm_sec = se;
            tmv.tm_isdst = -1;
            return std::mktime(&tmv);
        }

        std::string LowerExtension(const std::string& name) {
            size_t dot = name.find_last_of('.');
            if (dot == std::string::npos || dot == 0 || dot + 1 >= name.size())
                return "";
            std::string ext = name.substr(dot + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            return ext;
        }

        bool IsBrowsableArchiveExt(const std::string& ext) {
            static const char* exts[] = {"zip", "7z", "rar", "tar", "gz", "tgz",
                                         "bz2", "xz", "zst", "jar"};
            for (const char* e : exts) if (ext == e) return true;
            return false;
        }

        const char* SortFieldLabel(FilerSortField f) {
            switch (f) {
                case FilerSortField::Name:         return "Name";
                case FilerSortField::Size:         return "Size";
                case FilerSortField::Type:         return "Type";
                case FilerSortField::ModifiedDate: return "Modified";
                case FilerSortField::CreatedDate:  return "Created";
            }
            return "";
        }

        const char* ViewTypeLabel(FilerViewType v) {
            switch (v) {
                case FilerViewType::Details:             return "Details";
                case FilerViewType::List:                return "List";
                case FilerViewType::ThumbnailsSmall:     return "Thumbnails, small";
                case FilerViewType::ThumbnailsMedium:    return "Thumbnails, medium";
                case FilerViewType::ThumbnailsBig:       return "Thumbnails, big";
                case FilerViewType::ThumbnailsMaximized: return "Thumbnails, maximized";
                case FilerViewType::BarSize:             return "Bar size";
                case FilerViewType::TreeMap:             return "Treemap";
                case FilerViewType::GourceTree:          return "Force-directed tree";
                case FilerViewType::View3D:              return "3D";
            }
            return "";
        }

        std::string FormatDuration(double seconds) {
            if (seconds < 0) return "";
            long total = std::lround(seconds);
            long h = total / 3600, m = (total % 3600) / 60, s = total % 60;
            char buf[32];
            if (h > 0) snprintf(buf, sizeof(buf), "%ld:%02ld:%02ld", h, m, s);
            else       snprintf(buf, sizeof(buf), "%ld:%02ld", m, s);
            return buf;
        }

        // ===== LIGHTWEIGHT FILE-HEADER PROBES (selection info bar) =====
        // Parse just the container headers so describing a selection never
        // decodes an image or plays a media file. Every probe reads a bounded
        // number of bytes and fails soft — the info bar simply omits the
        // detail it could not determine.

        uint16_t U16LE(const unsigned char* p) { return uint16_t(p[0] | (p[1] << 8)); }
        uint32_t U24LE(const unsigned char* p) {
            return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16);
        }
        uint32_t U32LE(const unsigned char* p) {
            return uint32_t(p[0]) | (uint32_t(p[1]) << 8)
                 | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
        }
        uint64_t U64LE(const unsigned char* p) {
            return uint64_t(U32LE(p)) | (uint64_t(U32LE(p + 4)) << 32);
        }
        uint16_t U16BE(const unsigned char* p) { return uint16_t((p[0] << 8) | p[1]); }
        uint32_t U24BE(const unsigned char* p) {
            return (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
        }
        uint32_t U32BE(const unsigned char* p) {
            return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16)
                 | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
        }
        uint64_t U64BE(const unsigned char* p) {
            return (uint64_t(U32BE(p)) << 32) | U32BE(p + 4);
        }

        std::vector<unsigned char> ReadFileBytes(std::ifstream& f, uint64_t offset,
                                                 size_t count) {
            std::vector<unsigned char> out(count);
            f.clear();
            f.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
            f.read(reinterpret_cast<char*>(out.data()),
                   static_cast<std::streamsize>(count));
            out.resize(static_cast<size_t>(std::max<std::streamsize>(0, f.gcount())));
            return out;
        }

        std::string FourCCName(const unsigned char* p) {
            std::string s;
            for (int i = 0; i < 4; ++i) {
                char c = char(p[i]);
                if (c >= 32 && c < 127) s += c;
            }
            return s;
        }

        // --- Bitmap dimensions (PNG / GIF / BMP / QOI / WebP / ICO / JPEG / TIFF) ---
        bool ProbeImageDimensions(const std::string& path, int& w, int& h) {
            w = h = 0;
            std::ifstream f(path, std::ios::binary);
            if (!f) return false;
            auto head = ReadFileBytes(f, 0, 64);
            if (head.size() < 16) return false;
            const unsigned char* p = head.data();

            static const unsigned char pngSig[8] =
                {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
            if (head.size() >= 24 && std::memcmp(p, pngSig, 8) == 0) {
                w = int(U32BE(p + 16)); h = int(U32BE(p + 20));
                return w > 0 && h > 0;
            }
            if (std::memcmp(p, "GIF8", 4) == 0) {
                w = U16LE(p + 6); h = U16LE(p + 8);
                return w > 0 && h > 0;
            }
            if (head.size() >= 26 && p[0] == 'B' && p[1] == 'M') {
                w = int(int32_t(U32LE(p + 18)));
                h = std::abs(int(int32_t(U32LE(p + 22))));
                return w > 0 && h > 0;
            }
            if (std::memcmp(p, "qoif", 4) == 0) {
                w = int(U32BE(p + 4)); h = int(U32BE(p + 8));
                return w > 0 && h > 0;
            }
            if (head.size() >= 30 && std::memcmp(p, "RIFF", 4) == 0 &&
                std::memcmp(p + 8, "WEBP", 4) == 0) {
                if (std::memcmp(p + 12, "VP8X", 4) == 0) {
                    w = int(U24LE(p + 24)) + 1; h = int(U24LE(p + 27)) + 1;
                    return true;
                }
                if (std::memcmp(p + 12, "VP8L", 4) == 0 && p[20] == 0x2F) {
                    uint32_t bits = U32LE(p + 21);
                    w = int(bits & 0x3FFF) + 1; h = int((bits >> 14) & 0x3FFF) + 1;
                    return true;
                }
                if (std::memcmp(p + 12, "VP8 ", 4) == 0 &&
                    p[23] == 0x9D && p[24] == 0x01 && p[25] == 0x2A) {
                    w = U16LE(p + 26) & 0x3FFF; h = U16LE(p + 28) & 0x3FFF;
                    return w > 0 && h > 0;
                }
                return false;
            }
            if (p[0] == 0 && p[1] == 0 && (p[2] == 1 || p[2] == 2) && p[3] == 0 &&
                U16LE(p + 4) > 0) {   // ICO / CUR, first directory entry
                w = p[6] ? p[6] : 256; h = p[7] ? p[7] : 256;
                return true;
            }
            if (p[0] == 0xFF && p[1] == 0xD8) {   // JPEG: walk to the first SOF
                uint64_t pos = 2;
                for (int i = 0; i < 256; ++i) {
                    auto m = ReadFileBytes(f, pos, 4);
                    if (m.size() < 4 || m[0] != 0xFF) return false;
                    unsigned char marker = m[1];
                    if (marker == 0xFF) { ++pos; continue; }   // fill byte
                    if (marker == 0xD8 || marker == 0x01 ||
                        (marker >= 0xD0 && marker <= 0xD7)) { pos += 2; continue; }
                    if (marker == 0xD9 || marker == 0xDA) return false;   // EOI / SOS
                    uint32_t len = U16BE(m.data() + 2);
                    if (len < 2) return false;
                    bool sof = (marker >= 0xC0 && marker <= 0xCF &&
                                marker != 0xC4 && marker != 0xC8 && marker != 0xCC);
                    if (sof) {
                        auto d = ReadFileBytes(f, pos + 4, 5);
                        if (d.size() < 5) return false;
                        h = U16BE(d.data() + 1); w = U16BE(d.data() + 3);
                        return w > 0 && h > 0;
                    }
                    pos += 2 + len;
                }
                return false;
            }
            bool tiffLE = std::memcmp(p, "II\x2A\x00", 4) == 0;
            bool tiffBE = std::memcmp(p, "MM\x00\x2A", 4) == 0;
            if (tiffLE || tiffBE) {
                auto rd16 = [tiffLE](const unsigned char* q) {
                    return tiffLE ? U16LE(q) : U16BE(q);
                };
                auto rd32 = [tiffLE](const unsigned char* q) {
                    return tiffLE ? U32LE(q) : U32BE(q);
                };
                uint32_t ifd = rd32(p + 4);
                auto cnt = ReadFileBytes(f, ifd, 2);
                if (cnt.size() < 2) return false;
                uint32_t n = rd16(cnt.data());
                if (n == 0 || n > 512) return false;
                auto dir = ReadFileBytes(f, ifd + 2, size_t(n) * 12);
                if (dir.size() < size_t(n) * 12) return false;
                for (uint32_t i = 0; i < n; ++i) {
                    const unsigned char* e = dir.data() + i * 12;
                    uint16_t tag = rd16(e), type = rd16(e + 2);
                    uint32_t val = (type == 3) ? rd16(e + 8) : rd32(e + 8);
                    if (tag == 256) w = int(val);
                    else if (tag == 257) h = int(val);
                }
                return w > 0 && h > 0;
            }
            return false;
        }

        // --- Audio / video duration + codec ---
        struct FilerMediaProbe {
            double seconds = -1.0;
            std::string codec;
        };

        bool ProbeWav(std::ifstream& f, uint64_t fileSize, FilerMediaProbe& out) {
            auto head = ReadFileBytes(f, 0, 12);
            if (head.size() < 12 || std::memcmp(head.data(), "RIFF", 4) != 0 ||
                std::memcmp(head.data() + 8, "WAVE", 4) != 0) return false;
            uint64_t pos = 12;
            uint16_t fmtTag = 0;
            uint32_t byteRate = 0;
            uint64_t dataSize = 0;
            for (int i = 0; i < 64 && pos + 8 <= fileSize; ++i) {
                auto ch = ReadFileBytes(f, pos, 8);
                if (ch.size() < 8) break;
                uint32_t sz = U32LE(ch.data() + 4);
                if (std::memcmp(ch.data(), "fmt ", 4) == 0) {
                    auto fmt = ReadFileBytes(f, pos + 8, std::min<uint32_t>(sz, 16));
                    if (fmt.size() >= 16) {
                        fmtTag = U16LE(fmt.data());
                        byteRate = U32LE(fmt.data() + 8);
                    }
                } else if (std::memcmp(ch.data(), "data", 4) == 0) {
                    dataSize = (sz == 0 || sz == 0xFFFFFFFFu)
                            ? (fileSize > pos + 8 ? fileSize - pos - 8 : 0) : sz;
                }
                pos += 8 + uint64_t(sz) + (sz & 1);
            }
            if (byteRate && dataSize) out.seconds = double(dataSize) / byteRate;
            switch (fmtTag) {
                case 0x01:   out.codec = "PCM"; break;
                case 0x03:   out.codec = "PCM Float"; break;
                case 0x06:   out.codec = "A-law"; break;
                case 0x07:   out.codec = "µ-law"; break;
                case 0x55:   out.codec = "MP3"; break;
                case 0xFFFE: out.codec = "PCM"; break;
                default:     out.codec = fmtTag ? "WAV" : ""; break;
            }
            return out.seconds >= 0 || !out.codec.empty();
        }

        bool ProbeFlac(std::ifstream& f, FilerMediaProbe& out) {
            auto head = ReadFileBytes(f, 0, 4);
            if (head.size() < 4 || std::memcmp(head.data(), "fLaC", 4) != 0)
                return false;
            uint64_t pos = 4;
            for (int i = 0; i < 64; ++i) {
                auto bh = ReadFileBytes(f, pos, 4);
                if (bh.size() < 4) break;
                bool last = (bh[0] & 0x80) != 0;
                int type = bh[0] & 0x7F;
                uint32_t sz = U24BE(bh.data() + 1);
                if (type == 0 && sz >= 18) {   // STREAMINFO
                    auto d = ReadFileBytes(f, pos + 4, 18);
                    if (d.size() >= 18) {
                        uint32_t sr = (uint32_t(d[10]) << 12)
                                    | (uint32_t(d[11]) << 4) | (d[12] >> 4);
                        uint64_t samples = (uint64_t(d[13] & 0x0F) << 32)
                                         | U32BE(d.data() + 14);
                        if (sr && samples) out.seconds = double(samples) / sr;
                    }
                }
                pos += 4 + uint64_t(sz);
                if (last) break;
            }
            out.codec = "FLAC";
            return true;
        }

        bool ProbeMp3(std::ifstream& f, uint64_t fileSize, FilerMediaProbe& out) {
            uint64_t off = 0;
            auto id3 = ReadFileBytes(f, 0, 10);
            if (id3.size() >= 10 && std::memcmp(id3.data(), "ID3", 3) == 0) {
                off = 10 + ((uint32_t(id3[6] & 0x7F) << 21)
                          | (uint32_t(id3[7] & 0x7F) << 14)
                          | (uint32_t(id3[8] & 0x7F) << 7) | (id3[9] & 0x7F));
            }
            auto buf = ReadFileBytes(f, off, 16384);
            static const int kBitrateV1L3[16] =
                {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
            static const int kBitrateV2L3[16] =
                {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
            static const int kRateV1[4] = {44100, 48000, 32000, 0};
            for (size_t i = 0; i + 4 <= buf.size(); ++i) {
                if (buf[i] != 0xFF || (buf[i + 1] & 0xE0) != 0xE0) continue;
                int verBits = (buf[i + 1] >> 3) & 3;     // 0 = 2.5, 2 = 2, 3 = 1
                int layerBits = (buf[i + 1] >> 1) & 3;   // 1 = Layer III
                if (verBits == 1 || layerBits != 1) continue;
                int brIdx = buf[i + 2] >> 4;
                int srIdx = (buf[i + 2] >> 2) & 3;
                if (brIdx == 0 || brIdx == 15 || srIdx == 3) continue;
                bool v1 = (verBits == 3);
                int sr = kRateV1[srIdx];
                if (verBits == 2) sr /= 2;
                else if (verBits == 0) sr /= 4;
                int bitrate = (v1 ? kBitrateV1L3 : kBitrateV2L3)[brIdx] * 1000;
                if (!sr || !bitrate) continue;
                int spf = v1 ? 1152 : 576;   // samples per frame, Layer III
                bool mono = ((buf[i + 3] >> 6) & 3) == 3;
                size_t xing = i + 4 + (v1 ? (mono ? 17 : 32) : (mono ? 9 : 17));
                uint32_t frames = 0;
                if (xing + 12 <= buf.size() &&
                    (std::memcmp(&buf[xing], "Xing", 4) == 0 ||
                     std::memcmp(&buf[xing], "Info", 4) == 0)) {
                    if (U32BE(&buf[xing + 4]) & 1) frames = U32BE(&buf[xing + 8]);
                } else if (i + 4 + 32 + 18 <= buf.size() &&
                           std::memcmp(&buf[i + 4 + 32], "VBRI", 4) == 0) {
                    frames = U32BE(&buf[i + 4 + 32 + 14]);
                }
                if (frames) out.seconds = double(frames) * spf / sr;
                else if (fileSize > off + i)
                    out.seconds = double(fileSize - off - i) * 8.0 / bitrate;
                out.codec = "MP3";
                return true;
            }
            return false;
        }

        bool ProbeOgg(std::ifstream& f, uint64_t fileSize, FilerMediaProbe& out) {
            auto b = ReadFileBytes(f, 0, 512);
            if (b.size() < 28 || std::memcmp(b.data(), "OggS", 4) != 0) return false;
            size_t pk = 27 + b[26];   // first packet, after the segment table
            uint32_t rate = 0;
            if (pk + 8 <= b.size()) {
                if (std::memcmp(&b[pk], "\x01vorbis", 7) == 0) {
                    out.codec = "Vorbis";
                    if (pk + 16 <= b.size()) rate = U32LE(&b[pk + 12]);
                } else if (std::memcmp(&b[pk], "OpusHead", 8) == 0) {
                    out.codec = "Opus";
                    rate = 48000;   // Opus granules always run at 48 kHz
                } else if (std::memcmp(&b[pk], "\x80theora", 7) == 0) {
                    out.codec = "Theora";
                } else if (std::memcmp(&b[pk], "\x7f""FLAC", 5) == 0) {
                    out.codec = "FLAC";
                } else {
                    out.codec = "OGG";
                }
            }
            if (rate) {
                // Duration = granule position of the last page.
                size_t tailLen = size_t(std::min<uint64_t>(fileSize, 65536));
                auto tail = ReadFileBytes(f, fileSize - tailLen, tailLen);
                if (tail.size() >= 27) {
                    for (size_t i = tail.size() - 27;; --i) {
                        if (std::memcmp(&tail[i], "OggS", 4) == 0) {
                            uint64_t granule = U64LE(&tail[i + 6]);
                            if (granule && granule != ~0ull)
                                out.seconds = double(granule) / rate;
                            break;
                        }
                        if (i == 0) break;
                    }
                }
            }
            return !out.codec.empty();
        }

        std::string Mp4CodecName(const std::string& fcc, bool& isVideo) {
            struct Map { const char* fcc; const char* name; bool video; };
            static const Map map[] = {
                {"avc1", "H.264", true},  {"avc3", "H.264", true},
                {"hvc1", "H.265", true},  {"hev1", "H.265", true},
                {"vp08", "VP8", true},    {"vp09", "VP9", true},
                {"av01", "AV1", true},    {"mp4v", "MPEG-4", true},
                {"s263", "H.263", true},  {"mjpa", "MJPEG", true},
                {"jpeg", "MJPEG", true},
                {"mp4a", "AAC", false},   {"ac-3", "AC-3", false},
                {"ec-3", "E-AC-3", false},{"alac", "ALAC", false},
                {"Opus", "Opus", false},  {"opus", "Opus", false},
                {"fLaC", "FLAC", false},  {"twos", "PCM", false},
                {"sowt", "PCM", false},   {"lpcm", "PCM", false},
                {"samr", "AMR", false},
            };
            for (const Map& m : map)
                if (fcc == m.fcc) { isVideo = m.video; return m.name; }
            isVideo = false;
            return fcc;   // unknown: show the raw sample-entry code
        }

        void ParseMp4Boxes(const unsigned char* p, size_t n, FilerMediaProbe& out,
                           bool& haveVideoCodec, int depth) {
            if (depth > 6) return;
            size_t pos = 0;
            while (pos + 8 <= n) {
                uint64_t sz = U32BE(p + pos);
                std::string type = FourCCName(p + pos + 4);
                size_t hdr = 8;
                if (sz == 1) {
                    if (pos + 16 > n) return;
                    sz = U64BE(p + pos + 8);
                    hdr = 16;
                } else if (sz == 0) {
                    sz = n - pos;
                }
                if (sz < hdr || sz > n - pos) return;
                const unsigned char* body = p + pos + hdr;
                size_t bodyLen = size_t(sz - hdr);
                if (type == "trak" || type == "mdia" || type == "minf" ||
                    type == "stbl") {
                    ParseMp4Boxes(body, bodyLen, out, haveVideoCodec, depth + 1);
                } else if (type == "mvhd" && bodyLen >= 20) {
                    if (body[0] == 1 && bodyLen >= 32) {
                        uint32_t scale = U32BE(body + 20);
                        uint64_t dur = U64BE(body + 24);
                        if (scale && dur != ~0ull) out.seconds = double(dur) / scale;
                    } else if (body[0] == 0 && bodyLen >= 20) {
                        uint32_t scale = U32BE(body + 12);
                        uint32_t dur = U32BE(body + 16);
                        if (scale && dur != 0xFFFFFFFFu)
                            out.seconds = double(dur) / scale;
                    }
                } else if (type == "stsd" && bodyLen >= 16) {
                    bool isVideo = false;
                    std::string name = Mp4CodecName(FourCCName(body + 12), isVideo);
                    if (!name.empty() &&
                        (out.codec.empty() || (isVideo && !haveVideoCodec))) {
                        out.codec = name;
                        haveVideoCodec = haveVideoCodec || isVideo;
                    }
                }
                pos += size_t(sz);
            }
        }

        bool ProbeMp4(std::ifstream& f, uint64_t fileSize, FilerMediaProbe& out) {
            auto head = ReadFileBytes(f, 0, 12);
            if (head.size() < 12) return false;
            std::string first = FourCCName(head.data() + 4);
            if (first != "ftyp" && first != "moov" && first != "mdat" &&
                first != "wide" && first != "free" && first != "skip")
                return false;
            uint64_t pos = 0;
            for (int i = 0; i < 128 && pos + 8 <= fileSize; ++i) {
                auto bh = ReadFileBytes(f, pos, 16);
                if (bh.size() < 8) break;
                uint64_t sz = U32BE(bh.data());
                uint64_t hdr = 8;
                if (sz == 1 && bh.size() >= 16) {
                    sz = U64BE(bh.data() + 8);
                    hdr = 16;
                } else if (sz == 0) {
                    sz = fileSize - pos;
                }
                if (sz < hdr) break;
                if (std::memcmp(bh.data() + 4, "moov", 4) == 0) {
                    size_t load = size_t(std::min<uint64_t>(sz - hdr, 8u << 20));
                    auto moov = ReadFileBytes(f, pos + hdr, load);
                    bool haveVideo = false;
                    ParseMp4Boxes(moov.data(), moov.size(), out, haveVideo, 0);
                    break;
                }
                pos += sz;
            }
            return out.seconds >= 0 || !out.codec.empty();
        }

        std::string AviCodecName(std::string fcc) {
            std::transform(fcc.begin(), fcc.end(), fcc.begin(),
                           [](unsigned char c) { return std::toupper(c); });
            if (fcc == "H264" || fcc == "X264" || fcc == "AVC1") return "H.264";
            if (fcc == "H265" || fcc == "X265" || fcc == "HEVC") return "H.265";
            if (fcc == "XVID" || fcc == "DIVX" || fcc == "DX50" ||
                fcc == "FMP4" || fcc == "MP4V") return "MPEG-4";
            if (fcc == "MJPG") return "MJPEG";
            if (fcc == "DVSD") return "DV";
            if (fcc == "WMV3") return "WMV";
            if (fcc == "VP80") return "VP8";
            if (fcc == "VP90") return "VP9";
            return fcc;
        }

        void ParseAviChunks(const unsigned char* p, size_t n, FilerMediaProbe& out,
                            int depth) {
            if (depth > 4) return;
            size_t pos = 0;
            while (pos + 8 <= n) {
                const unsigned char* c = p + pos;
                uint32_t sz = U32LE(c + 4);
                if (sz > n - pos - 8) break;
                if (std::memcmp(c, "LIST", 4) == 0 && sz >= 4) {
                    ParseAviChunks(c + 12, sz - 4, out, depth + 1);
                } else if (std::memcmp(c, "avih", 4) == 0 && sz >= 20) {
                    uint32_t usPerFrame = U32LE(c + 8);
                    uint32_t totalFrames = U32LE(c + 8 + 16);
                    if (usPerFrame && totalFrames)
                        out.seconds = double(usPerFrame) * totalFrames / 1e6;
                } else if (std::memcmp(c, "strh", 4) == 0 && sz >= 8) {
                    if (std::memcmp(c + 8, "vids", 4) == 0 && out.codec.empty())
                        out.codec = AviCodecName(FourCCName(c + 12));
                }
                pos += 8 + size_t(sz) + (sz & 1);
            }
        }

        bool ProbeAvi(std::ifstream& f, FilerMediaProbe& out) {
            auto b = ReadFileBytes(f, 0, 65536);
            if (b.size() < 16 || std::memcmp(b.data(), "RIFF", 4) != 0 ||
                std::memcmp(b.data() + 8, "AVI ", 4) != 0) return false;
            ParseAviChunks(b.data() + 12, b.size() - 12, out, 0);
            if (out.codec.empty()) out.codec = "AVI";
            return true;
        }

        // Matroska / WebM: a bounded EBML walk over Segment > Info / Tracks.
        struct MkvScan {
            double durationTicks = -1.0;
            uint64_t timescale = 1000000;   // ns per tick, Matroska default
            std::string videoCodec, audioCodec;
        };

        bool EbmlRead(std::ifstream& f, uint64_t& pos, uint64_t end,
                      bool keepMarker, uint64_t& value, bool& unknown) {
            unknown = false;
            if (pos >= end) return false;
            auto b = ReadFileBytes(f, pos, 8);
            if (b.empty() || b[0] == 0) return false;
            int len = 1;
            while (len <= 8 && !(b[0] & (0x80 >> (len - 1)))) ++len;
            if (len > 8 || size_t(len) > b.size()) return false;
            value = keepMarker ? b[0] : uint64_t(b[0] & (0xFF >> len));
            for (int i = 1; i < len; ++i) value = (value << 8) | b[i];
            if (!keepMarker) {
                uint64_t allOnes = (len == 8) ? 0x00FFFFFFFFFFFFFFull
                                              : ((1ull << (7 * len)) - 1);
                unknown = (value == allOnes);
            }
            pos += len;
            return true;
        }

        std::string MkvCodecName(const std::string& id) {
            if (id == "V_MPEG4/ISO/AVC")  return "H.264";
            if (id == "V_MPEGH/ISO/HEVC") return "H.265";
            if (id == "V_VP8")            return "VP8";
            if (id == "V_VP9")            return "VP9";
            if (id == "V_AV1")            return "AV1";
            if (id == "V_THEORA")         return "Theora";
            if (id == "V_MPEG2")          return "MPEG-2";
            if (id.rfind("V_MPEG4", 0) == 0) return "MPEG-4";
            if (id == "A_OPUS")           return "Opus";
            if (id == "A_VORBIS")         return "Vorbis";
            if (id == "A_FLAC")           return "FLAC";
            if (id.rfind("A_AAC", 0) == 0) return "AAC";
            if (id == "A_MPEG/L3")        return "MP3";
            if (id == "A_AC3")            return "AC-3";
            if (id == "A_EAC3")           return "E-AC-3";
            if (id.rfind("A_PCM", 0) == 0) return "PCM";
            if (id.size() > 2 && id[1] == '_') return id.substr(2);
            return id;
        }

        void ParseMkvLevel(std::ifstream& f, uint64_t pos, uint64_t end,
                           MkvScan& scan, int depth) {
            if (depth > 5) return;
            for (int guard = 0; pos < end && guard < 256; ++guard) {
                uint64_t id = 0, sz = 0;
                bool unknown = false, idUnknown = false;
                if (!EbmlRead(f, pos, end, true, id, idUnknown)) return;
                if (!EbmlRead(f, pos, end, false, sz, unknown)) return;
                uint64_t bodyEnd = unknown ? end : std::min(end, pos + sz);
                if (id == 0x1F43B675ull) return;   // Cluster: headers are done
                if (id == 0x18538067ull || id == 0x1549A966ull ||
                    id == 0x1654AE6Bull || id == 0xAEull) {
                    // Segment / Info / Tracks / TrackEntry
                    ParseMkvLevel(f, pos, bodyEnd, scan, depth + 1);
                } else if (id == 0x2AD7B1ull) {    // TimestampScale (uint)
                    auto d = ReadFileBytes(f, pos, size_t(std::min<uint64_t>(sz, 8)));
                    uint64_t v = 0;
                    for (unsigned char c : d) v = (v << 8) | c;
                    if (v) scan.timescale = v;
                } else if (id == 0x4489ull) {      // Duration (float)
                    auto d = ReadFileBytes(f, pos, size_t(std::min<uint64_t>(sz, 8)));
                    if (d.size() == 4) {
                        uint32_t u = U32BE(d.data());
                        float fv;
                        std::memcpy(&fv, &u, 4);
                        scan.durationTicks = fv;
                    } else if (d.size() == 8) {
                        uint64_t u = U64BE(d.data());
                        double dv;
                        std::memcpy(&dv, &u, 8);
                        scan.durationTicks = dv;
                    }
                } else if (id == 0x86ull) {        // CodecID (string)
                    auto d = ReadFileBytes(f, pos, size_t(std::min<uint64_t>(sz, 32)));
                    std::string codecId(d.begin(), d.end());
                    if (codecId.rfind("V_", 0) == 0 && scan.videoCodec.empty())
                        scan.videoCodec = MkvCodecName(codecId);
                    else if (codecId.rfind("A_", 0) == 0 && scan.audioCodec.empty())
                        scan.audioCodec = MkvCodecName(codecId);
                }
                if (unknown) return;   // can't skip an unknown-size element
                pos += sz;
            }
        }

        bool ProbeMkv(std::ifstream& f, uint64_t fileSize, FilerMediaProbe& out) {
            auto head = ReadFileBytes(f, 0, 4);
            if (head.size() < 4 ||
                std::memcmp(head.data(), "\x1A\x45\xDF\xA3", 4) != 0) return false;
            MkvScan scan;
            ParseMkvLevel(f, 0, fileSize, scan, 0);
            if (scan.durationTicks > 0)
                out.seconds = scan.durationTicks * double(scan.timescale) / 1e9;
            out.codec = !scan.videoCodec.empty() ? scan.videoCodec : scan.audioCodec;
            return out.seconds >= 0 || !out.codec.empty();
        }

        bool ProbeAsf(std::ifstream& f, FilerMediaProbe& out) {
            static const unsigned char kAsfHeader[16] =
                {0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11,
                 0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C};
            static const unsigned char kFileProps[16] =
                {0xA1, 0xDC, 0xAB, 0x8C, 0x47, 0xA9, 0xCF, 0x11,
                 0x8E, 0xE4, 0x00, 0xC0, 0x0C, 0x20, 0x53, 0x65};
            auto b = ReadFileBytes(f, 0, 65536);
            if (b.size() < 30 || std::memcmp(b.data(), kAsfHeader, 16) != 0)
                return false;
            size_t pos = 30;
            for (int i = 0; i < 64 && pos + 24 <= b.size(); ++i) {
                uint64_t sz = U64LE(&b[pos + 16]);
                if (sz < 24) break;
                if (std::memcmp(&b[pos], kFileProps, 16) == 0 &&
                    pos + 24 + 64 <= b.size()) {
                    uint64_t play = U64LE(&b[pos + 24 + 40]);      // 100 ns units
                    uint64_t preroll = U64LE(&b[pos + 24 + 56]);   // ms
                    double s = double(play) / 1e7 - double(preroll) / 1000.0;
                    if (s > 0) out.seconds = s;
                    break;
                }
                if (sz > b.size() - pos) break;
                pos += size_t(sz);
            }
            out.codec = "WMV";
            return true;
        }

        bool ProbeMediaFile(const std::string& path, const std::string& ext,
                            FilerMediaProbe& out) {
            std::ifstream f(path, std::ios::binary);
            if (!f) return false;
            f.seekg(0, std::ios::end);
            uint64_t fileSize = uint64_t(std::max<std::streamoff>(0, f.tellg()));
            if (fileSize < 16) return false;

            if (ext == "wav")  return ProbeWav(f, fileSize, out);
            if (ext == "flac") return ProbeFlac(f, out);
            if (ext == "mp3")  return ProbeMp3(f, fileSize, out);
            if (ext == "ogg" || ext == "oga" || ext == "opus")
                return ProbeOgg(f, fileSize, out);
            if (ext == "mp4" || ext == "m4a" || ext == "m4v" || ext == "mov" ||
                ext == "3gp")
                return ProbeMp4(f, fileSize, out);
            if (ext == "avi")  return ProbeAvi(f, out);
            if (ext == "mkv" || ext == "webm" || ext == "mka")
                return ProbeMkv(f, fileSize, out);
            if (ext == "wmv")  return ProbeAsf(f, out);
            return false;
        }
    }

    // ===== CONSTRUCTION =====
    UltraCanvasFilerWidget::UltraCanvasFilerWidget(const std::string& identifier,
                                                   float x, float y, float w, float h)
            : UltraCanvasContainer(identifier, x, y, w, h) {
        SetMouseCursor(UCMouseCursor::Default);
        SetBackgroundColor(style.backgroundColor);
        // The filer paints its own content; hide the container's child scrollbars.
        ContainerStyle cs = GetContainerStyle();
        cs.autoShowScrollbars = false;
        cs.forceShowVerticalScrollbar = false;
        cs.forceShowHorizontalScrollbar = false;
        SetContainerStyle(cs);

        newDocumentTypes = {
            {"Text",        "txt", ""},
            {"Doc",         "odt", ""},
            {"Spreadsheet", "ods", ""},
            {"Bitmap",      "png", ""},
            {"Vector",      "svg", ""},
            {"Audio",       "wav", ""},
            {"Video",       "mp4", ""},
        };
    }

    UltraCanvasFilerWidget::~UltraCanvasFilerWidget() {
        thumbAlive->store(false);   // neutralize queued cross-thread redraws
        StopThumbnailWorkers();
        StopFolderStatsWorker();
    }

    // ===== FOLDER =====
    void UltraCanvasFilerWidget::SetPath(const std::string& folderPath) {
        currentPath = folderPath;
        scrollOffsetX = scrollOffsetY = 0;
        CancelRename();
        ClearSelection();
        ScanFolder();
        if (onPathChanged) onPathChanged(currentPath);
    }

    void UltraCanvasFilerWidget::Refresh() {
        CancelRename();
        ScanFolder();
    }

    void UltraCanvasFilerWidget::ApplyEntryTypeInfo(FilerEntry& e) const {
        if (e.isDirectory) {
            e.category = FilerFileCategory::Folder;
            e.typeName = "Folder";
            return;
        }
        const auto& m = ExtensionTypeMap();
        auto it = m.find(e.extension);
        if (it != m.end()) {
            e.category = it->second.category;
            e.typeName = std::string(it->second.label) + " "
                         + CategoryNoun(it->second.category);
        } else {
            e.category = FilerFileCategory::Other;
            std::string upper = e.extension;
            std::transform(upper.begin(), upper.end(), upper.begin(),
                           [](unsigned char c) { return std::toupper(c); });
            e.typeName = upper.empty() ? "File" : upper + " File";
        }
        if (e.category == FilerFileCategory::Archive) e.isArchive = true;
    }

    void UltraCanvasFilerWidget::ScanFolder() {
        entries.clear();
        effectiveSizesValid = false;
        hoveredIndex = -1;
        lastClickedIndex = -1;
        {
            // Files may have changed on disk: forget the folder stats and
            // drop queued / in-flight background walks of the old view.
            std::lock_guard<std::mutex> lk(statsMutex);
            ++statsGeneration;
            statsQueue.clear();
            folderStatsCache.clear();
        }
        mediaInfoCache.clear();
        DropThumbnailCache();

        std::error_code ec;
        bool isRealDir = !currentPath.empty() && fs::is_directory(currentPath, ec);

        if (isRealDir) {
            for (fs::directory_iterator it(currentPath, ec), end; it != end;
                 it.increment(ec)) {
                if (ec) break;
                FilerEntry e;
                e.name = it->path().filename().string();
                e.path = it->path().string();
                e.isDirectory = it->is_directory(ec);
                e.isSymlink = it->is_symlink(ec);
                e.isHidden = !e.name.empty() && e.name[0] == '.';
                if (!e.isDirectory) {
                    std::error_code sec;
                    e.size = it->file_size(sec);
                    if (sec) e.size = 0;
                }
                e.extension = e.isDirectory ? "" : LowerExtension(e.name);

                struct stat st{};
                if (::stat(e.path.c_str(), &st) == 0) {
                    e.modifiedTime = st.st_mtime;
                    e.createdTime = st.st_ctime;
                    e.isReadOnly = (st.st_mode & S_IWUSR) == 0;
                }
                ApplyEntryTypeInfo(e);
                if (e.isHidden && !showHiddenFiles) continue;
                entries.push_back(std::move(e));
            }
        }
#ifdef ULTRACANVAS_HAS_VIRTUALFS
        else if (!currentPath.empty()) {
            // Not a real directory: let VirtualFS list it (an archive interior —
            // "/path/archive.zip" or a path inside one).
            for (const VirtualFS::VirtualFSEntry& v
                 : VirtualFS::VirtualFS_ListDirectory(currentPath)) {
                FilerEntry e;
                e.name = v.name;
                e.path = v.path;
                e.isDirectory = v.IsDirectory();
                e.isSymlink = v.IsSymlink();
                e.isHidden = v.isHidden;
                e.isReadOnly = v.isReadOnly;
                e.isArchive = v.isArchive;
                e.size = v.size;
                e.compressedSize = v.compressedSize;
                e.modifiedTime = ParseIso8601(v.modifiedTime);
                e.createdTime = ParseIso8601(v.createdTime);
                e.extension = e.isDirectory ? "" : LowerExtension(e.name);
                ApplyEntryTypeInfo(e);
                if (e.isHidden && !showHiddenFiles) continue;
                entries.push_back(std::move(e));
            }
        }
#endif

        for (FilerEntry& e : entries) {
            e.effectiveSize = e.size;

            std::string attr;
            if (e.isDirectory) attr += 'D';
            if (e.isSymlink)   attr += 'L';
            if (e.isReadOnly)  attr += 'R';
            if (e.isHidden)    attr += 'H';
            if (e.isArchive)   attr += 'A';
            e.attributes = attr;

            if (e.compressedSize > 0 && e.size > 0 && e.compressedSize <= e.size) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%.0f%% compressed",
                         100.0 * (1.0 - double(e.compressedSize) / double(e.size)));
                e.info = buf;
            }
            if (infoProvider) {
                std::string s = infoProvider(e);
                if (!s.empty()) e.info = s;
            }
        }

        SortEntries();
        InvalidateFilerLayout();
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::SortEntries() {
        const FilerSortField field = sortField;
        const bool asc = sortAscending;
        std::stable_sort(entries.begin(), entries.end(),
                         [field, asc](const FilerEntry& a, const FilerEntry& b) {
            // Folders always list before files, regardless of direction.
            if (a.isDirectory != b.isDirectory) return a.isDirectory;
            int cmp = 0;
            switch (field) {
                case FilerSortField::Name:
                    cmp = CompareNoCase(a.name, b.name);
                    break;
                case FilerSortField::Size:
                    cmp = a.size < b.size ? -1 : (a.size > b.size ? 1 : 0);
                    break;
                case FilerSortField::Type:
                    cmp = CompareNoCase(a.typeName, b.typeName);
                    break;
                case FilerSortField::ModifiedDate:
                    cmp = a.modifiedTime < b.modifiedTime ? -1
                        : (a.modifiedTime > b.modifiedTime ? 1 : 0);
                    break;
                case FilerSortField::CreatedDate:
                    cmp = a.createdTime < b.createdTime ? -1
                        : (a.createdTime > b.createdTime ? 1 : 0);
                    break;
            }
            if (cmp == 0) cmp = CompareNoCase(a.name, b.name);
            return asc ? cmp < 0 : cmp > 0;
        });
    }

    void UltraCanvasFilerWidget::EnsureEffectiveSizes() {
        if (effectiveSizesValid) return;
        // Directory weights come from the async folder stats: not-yet-walked
        // folders keep weight 0 for now and the layout reflows when their
        // background walk lands (PostFolderStatsRedraw invalidates us).
        bool allReady = true;
        for (FilerEntry& e : entries) {
            e.effectiveSize = e.size;
            if (!e.isDirectory) continue;
            FolderStats st = GetFolderStats(e.path);
            if (st.ready) e.effectiveSize = st.bytes;
            else allReady = false;
        }
        effectiveSizesValid = allReady;
    }

    // ===== VIEW SETTINGS =====
    void UltraCanvasFilerWidget::SetViewType(FilerViewType type) {
        if (viewType == type) return;
        viewType = type;
        scrollOffsetX = scrollOffsetY = 0;
        CancelRename();
        DropThumbnailCache();   // tile size changed; free the old-size pixmaps
        InvalidateFilerLayout();
        RequestRedraw();
        if (onViewTypeChanged) onViewTypeChanged(viewType);
    }

    void UltraCanvasFilerWidget::SetSort(FilerSortField field, bool ascending) {
        if (sortField == field && sortAscending == ascending) return;
        sortField = field;
        sortAscending = ascending;
        SortEntries();
        InvalidateFilerLayout();
        RequestRedraw();
        if (onSortChanged) onSortChanged(sortField, sortAscending);
    }

    void UltraCanvasFilerWidget::SetShowHiddenFiles(bool show) {
        if (showHiddenFiles == show) return;
        showHiddenFiles = show;
        Refresh();
    }

    void UltraCanvasFilerWidget::SetHoverIconMenuEnabled(bool enabled) {
        hoverIconMenu = enabled;
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::SetSelectionInfoVisible(bool visible) {
        if (showSelectionInfo == visible) return;
        showSelectionInfo = visible;
        InvalidateFilerLayout();   // the bar changes the content area height
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::SetStyle(const FilerStyle& s) {
        style = s;
        SetBackgroundColor(style.backgroundColor);
        InvalidateFilerLayout();
        RequestRedraw();
    }

    // ===== SELECTION =====
    std::vector<FilerEntry> UltraCanvasFilerWidget::GetSelectedEntries() const {
        std::vector<FilerEntry> out;
        for (size_t idx : selection)
            if (idx < entries.size()) out.push_back(entries[idx]);
        return out;
    }

    void UltraCanvasFilerWidget::ClearSelection() {
        if (selection.empty()) return;
        selection.clear();
        FireSelectionChanged();
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::SelectAll() {
        selection.clear();
        for (size_t i = 0; i < entries.size(); ++i) selection.push_back(i);
        FireSelectionChanged();
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::FireSelectionChanged() {
        if (onSelectionChanged) onSelectionChanged(GetSelectedEntries());
    }

    std::vector<size_t> UltraCanvasFilerWidget::SelectionOrItem(int index) const {
        if (!selection.empty()) return selection;
        if (index >= 0 && index < (int)entries.size())
            return {static_cast<size_t>(index)};
        return {};
    }

    std::vector<FilerEntry> UltraCanvasFilerWidget::SelectionOrAll() const {
        std::vector<FilerEntry> out = GetSelectedEntries();
        if (out.empty()) out = entries;
        return out;
    }

    // ===== CLIPBOARD / FILE OPERATIONS =====
    bool UltraCanvasFilerWidget::ClipboardHasContent() {
        if (!clipboardPaths.empty()) return true;
        // Files copied in another application (external file manager).
        if (UltraCanvasClipboard* cb = GetClipboard()) {
            return cb->IsFormatAvailable("text/uri-list");
        }
        return false;
    }

    void UltraCanvasFilerWidget::SelectionToClipboard(bool cut) {
        clipboardPaths.clear();
        for (const FilerEntry& e : GetSelectedEntries()) clipboardPaths.push_back(e.path);
        clipboardCut = cut && !clipboardPaths.empty();
        // Mirror to the system clipboard (text/uri-list + cut/copy marker) so
        // the files can be pasted in other programs.
        if (!clipboardPaths.empty()) {
            if (UltraCanvasClipboard* cb = GetClipboard()) {
                cb->SetFiles(clipboardPaths, clipboardCut);
            }
        }
    }

    void UltraCanvasFilerWidget::CopySelection() { SelectionToClipboard(false); }
    void UltraCanvasFilerWidget::CutSelection()  { SelectionToClipboard(true); }

    // ===== NATIVE DRAG & DROP =====
    bool UltraCanvasFilerWidget::StartNativeDragOfSelection() {
        UltraCanvasWindowBase* win = GetWindow();
        if (!win) return false;

        std::vector<std::string> paths;
        for (const FilerEntry& e : GetSelectedEntries()) paths.push_back(e.path);
        if (paths.empty()) return false;

        // The pointer leaves for the drag: drop the hover state now, the
        // widget won't see mouse events until the drag ends.
        if (hoveredIndex != -1) { hoveredIndex = -1; RequestRedraw(); }
        UltraCanvasTooltipManager::HideTooltip();

        // The drop target performs the copy / move itself; after a move this
        // folder needs a rescan to drop the vanished entries.
        std::weak_ptr<UltraCanvasUIElement> weakSelf = weak_from_this();
        return win->StartNativeFileDrag(paths,
                [weakSelf](bool accepted, bool moved) {
                    if (!accepted || !moved) return;
                    if (auto self = weakSelf.lock()) {
                        static_cast<UltraCanvasFilerWidget*>(self.get())->Refresh();
                    }
                });
    }

    void UltraCanvasFilerWidget::AcceptDroppedFiles(const std::vector<std::string>& paths) {
        if (paths.empty()) return;
        std::error_code ec;
        if (!fs::is_directory(currentPath, ec)) return;

        bool changed = false;
        for (const std::string& src : paths) {
            fs::path from(src);
            if (!fs::exists(from, ec)) continue;
            // Skip files already in this folder and the folder itself.
            fs::path canonicalFrom = fs::weakly_canonical(from, ec);
            fs::path canonicalHere = fs::weakly_canonical(fs::path(currentPath), ec);
            if (canonicalFrom == canonicalHere) continue;
            if (canonicalFrom.parent_path() == canonicalHere) continue;
            // Don't copy a folder into itself.
            std::string fromStr = canonicalFrom.string();
            std::string hereStr = canonicalHere.string();
            if (fs::is_directory(from, ec) && !fromStr.empty() &&
                hereStr.compare(0, fromStr.size(), fromStr) == 0 &&
                (hereStr.size() == fromStr.size() || hereStr[fromStr.size()] == '/')) {
                ReportError("Cannot drop a folder into itself: " + src);
                continue;
            }
            std::string dest = UniqueChildPath(from.filename().string());
            fs::copy(from, dest, fs::copy_options::recursive, ec);
            if (ec) ReportError("Drop failed for " + src + ": " + ec.message());
            else changed = true;
        }
        if (changed) Refresh();
    }

    std::string UltraCanvasFilerWidget::UniqueChildPath(const std::string& baseName) const {
        fs::path base(baseName);
        std::string stem = base.stem().string();
        std::string ext = base.extension().string();   // includes the dot
        fs::path dir(currentPath);
        fs::path candidate = dir / baseName;
        std::error_code ec;
        int n = 2;
        while (fs::exists(candidate, ec)) {
            candidate = dir / (stem + " (" + std::to_string(n++) + ")" + ext);
        }
        return candidate.string();
    }

    void UltraCanvasFilerWidget::Paste() {
        // The system clipboard wins: it holds whatever was copied last,
        // whether here (mirrored by SelectionToClipboard) or in another
        // program. The internal clipboard is the fallback when no system
        // clipboard is available.
        std::vector<std::string> paths;
        bool cut = false;
        if (UltraCanvasClipboard* cb = GetClipboard()) {
            cb->GetFiles(paths, cut);
        }
        if (paths.empty()) {
            paths = clipboardPaths;
            cut = clipboardCut;
        }
        if (paths.empty()) return;

        std::error_code ec;
        if (!fs::is_directory(currentPath, ec)) {
            ReportError("Paste target is not a writable folder: " + currentPath);
            return;
        }
        for (const std::string& src : paths) {
            fs::path from(src);
            if (!fs::exists(from, ec)) continue;
            // Cut-pasting into the folder the file already lives in is a no-op.
            if (cut && fs::path(src).parent_path() == fs::path(currentPath)) continue;
            std::string dest = UniqueChildPath(from.filename().string());
            if (cut) {
                fs::rename(from, dest, ec);
                if (ec) {   // cross-device move: copy + delete
                    ec.clear();
                    fs::copy(from, dest, fs::copy_options::recursive, ec);
                    if (!ec) fs::remove_all(from, ec);
                }
            } else {
                fs::copy(from, dest, fs::copy_options::recursive, ec);
            }
            if (ec) ReportError("Paste failed for " + src + ": " + ec.message());
        }
        if (cut) { clipboardPaths.clear(); clipboardCut = false; }
        Refresh();
    }

    void UltraCanvasFilerWidget::DeleteSelection() {
        std::vector<FilerEntry> victims = GetSelectedEntries();
        if (victims.empty()) return;
        // An app-provided veto takes precedence over the built-in dialog so
        // existing hosts keep full control of the confirmation flow.
        if (confirmDelete) {
            if (!confirmDelete(victims)) return;
            PerformDeletion(victims);
            return;
        }
        ShowDeleteConfirmation(victims);
    }

    void UltraCanvasFilerWidget::PerformDeletion(
            const std::vector<FilerEntry>& victims) {
        std::error_code ec;
        for (const FilerEntry& e : victims) {
            fs::remove_all(e.path, ec);
            if (ec) ReportError("Delete failed for " + e.path + ": " + ec.message());
        }
        ClearSelection();
        Refresh();
    }

    void UltraCanvasFilerWidget::ShowDeleteConfirmation(
            const std::vector<FilerEntry>& victims) {
        // Build the confirmation message.
        size_t folderCount = 0, fileCount = 0;
        for (const FilerEntry& e : victims) {
            if (e.isDirectory) ++folderCount; else ++fileCount;
        }
        std::string message;
        if (victims.size() == 1) {
            message = "Delete \"" + victims.front().name + "\" permanently?";
        } else {
            message = "Delete " + std::to_string(victims.size())
                    + " items permanently?";
        }

        DialogConfig cfg;
        cfg.title = "Confirm Delete";
        cfg.dialogType = DialogType::Warning;
        cfg.message = message;
        cfg.details = "This action cannot be undone.";
        cfg.buttons = DialogButtons::NoButtons;   // custom buttons added below
        cfg.width = 480;
        // Taller when a folder preview (thumbnail grid) is shown.
        cfg.height = folderCount > 0 ? 440 : 200;

        auto dialog = UltraCanvasDialogManager::CreateDialog(cfg);
        if (!dialog) {   // dialogs disabled — fall back to an immediate delete
            PerformDeletion(victims);
            return;
        }

        // When a folder is being deleted, preview the first entries inside it
        // (with thumbnails) so the user sees what the folder holds.
        const FilerEntry* previewFolder = nullptr;
        for (const FilerEntry& e : victims) {
            if (e.isDirectory) { previewFolder = &e; break; }
        }
        if (previewFolder) {
            std::error_code ec;
            std::vector<fs::directory_entry> inner;
            for (fs::directory_iterator it(previewFolder->path, ec), end;
                 it != end && inner.size() < 10; it.increment(ec)) {
                if (ec) break;
                inner.push_back(*it);
            }
            size_t totalInner = 0;
            for (fs::directory_iterator it(previewFolder->path, ec), end;
                 it != end; it.increment(ec)) {
                if (ec) break;
                ++totalInner;
            }

            auto caption = std::make_shared<UltraCanvasLabel>(
                    "FilerDelPreviewCap", 0, 0, 0, 18);
            caption->SetText("Folder \"" + previewFolder->name + "\" contains "
                             + std::to_string(totalInner) + " item(s)"
                             + (totalInner > inner.size()
                                    ? "  ·  showing first "
                                          + std::to_string(inner.size())
                                    : ""));
            caption->SetFontSize(11);
            caption->SetTextColor(Color(90, 90, 96, 255));
            caption->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
            dialog->AddDialogElement(caption);

            // A wrapping row of small thumbnail tiles.
            auto grid = std::make_shared<UltraCanvasContainer>("FilerDelPreviewGrid");
            grid->layout.SetFlexRow().SetFlexWrap(CSSLayout::FlexWrap::Wrap)
                        .SetFlexGap(6)
                        .SetFlexAlignItems(CSSLayout::AlignItems::Start);
            grid->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                            .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

            const int tile = 64;
            int idx = 0;
            for (const fs::directory_entry& de : inner) {
                std::error_code e2;
                std::string name = de.path().filename().string();
                bool isDir = de.is_directory(e2);

                auto cell = std::make_shared<UltraCanvasContainer>(
                        "FilerDelCell" + std::to_string(idx));
                cell->layout.SetFlexColumn().SetFlexGap(2)
                            .SetFlexAlignItems(CSSLayout::AlignItems::Center);
                cell->layoutItem.SetFlexGrow(0).SetFlexShrink(0);

                auto thumb = CreateImageElement(
                        "FilerDelThumb" + std::to_string(idx), 0, 0, tile, tile);
                thumb->SetFitMode(ImageFitMode::Contain);
                if (!isDir) thumb->LoadFromFile(de.path().string());
                thumb->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                cell->AddChild(thumb);

                auto lbl = std::make_shared<UltraCanvasLabel>(
                        "FilerDelName" + std::to_string(idx), 0, 0, tile, 14);
                std::string shown = name.size() > 12 ? name.substr(0, 11) + "…" : name;
                lbl->SetText(shown);
                lbl->SetFontSize(9);
                lbl->SetTextColor(Color(80, 80, 86, 255));
                lbl->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
                lbl->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                cell->AddChild(lbl);

                grid->AddChild(cell);
                ++idx;
            }
            dialog->AddDialogElement(grid);
        }

        auto self = this;
        std::vector<FilerEntry> captured = victims;
        dialog->AddCustomButton("Delete", DialogResult::Yes,
                [self, captured]() { self->PerformDeletion(captured); });
        dialog->AddCustomButton("Cancel", DialogResult::Cancel, nullptr);

        UltraCanvasDialogManager::ShowDialog(dialog, nullptr, GetWindow());
    }

    void UltraCanvasFilerWidget::DuplicateSelection() {
        std::vector<FilerEntry> sources = GetSelectedEntries();
        if (sources.empty()) return;
        std::error_code ec;
        for (const FilerEntry& e : sources) {
            std::string dest = UniqueChildPath(e.name);
            fs::copy(e.path, dest, fs::copy_options::recursive, ec);
            if (ec) ReportError("Duplicate failed for " + e.path + ": " + ec.message());
        }
        Refresh();
    }

    void UltraCanvasFilerWidget::StartRename(size_t entryIndex) {
        if (entryIndex >= entries.size()) return;
        renamingIndex = static_cast<int>(entryIndex);
        renameBuffer = entries[entryIndex].name;
        EnsureVisible(entryIndex);
        SetFocus(true);
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::CommitRename() {
        if (renamingIndex < 0 || renamingIndex >= (int)entries.size()) {
            renamingIndex = -1;
            return;
        }
        const FilerEntry& e = entries[renamingIndex];
        std::string newName = renameBuffer;
        renamingIndex = -1;
        if (newName.empty() || newName == e.name ||
            newName.find('/') != std::string::npos) {
            RequestRedraw();
            return;
        }
        std::error_code ec;
        fs::path target = fs::path(currentPath) / newName;
        if (fs::exists(target, ec)) {
            ReportError("Rename failed: \"" + newName + "\" already exists");
            RequestRedraw();
            return;
        }
        fs::rename(e.path, target, ec);
        if (ec) ReportError("Rename failed for " + e.path + ": " + ec.message());
        Refresh();
    }

    void UltraCanvasFilerWidget::CancelRename() {
        if (renamingIndex == -1) return;
        renamingIndex = -1;
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::CompressSelection() {
#ifdef ULTRACANVAS_HAS_VIRTUALFS
        std::vector<FilerEntry> targets = SelectionOrAll();
        if (targets.empty()) return;
        std::vector<std::string> paths;
        for (const FilerEntry& e : targets) paths.push_back(e.path);
        std::string base = (targets.size() == 1)
                ? fs::path(targets[0].name).stem().string()
                : fs::path(currentPath).filename().string();
        if (base.empty()) base = "archive";
        std::string dest = UniqueChildPath(base + ".zip");
        if (!UCVFSBridge::CreateArchive(dest, paths)) {
            ReportError("Compression failed for " + dest);
            return;
        }
        Refresh();
#else
        ReportError("Compress requires the VirtualFS module");
#endif
    }

    void UltraCanvasFilerWidget::ExtractSelection() {
#ifdef ULTRACANVAS_HAS_VIRTUALFS
        bool any = false;
        for (const FilerEntry& e : GetSelectedEntries()) {
            if (!e.isArchive) continue;
            any = true;
            std::string destDir = UniqueChildPath(fs::path(e.name).stem().string());
            std::error_code ec;
            fs::create_directories(destDir, ec);
            if (!UCVFSBridge::ExtractArchive(e.path, destDir)) {
                ReportError("Extraction failed for " + e.path);
            }
        }
        if (any) Refresh();
#else
        ReportError("Extract requires the VirtualFS module");
#endif
    }

    void UltraCanvasFilerWidget::SetNewDocumentTypes(
            const std::vector<FilerNewDocumentType>& types) {
        newDocumentTypes = types;
    }

    void UltraCanvasFilerWidget::CreateNewDocument(const FilerNewDocumentType& type) {
        if (onNewDocument && onNewDocument(type, currentPath)) {
            Refresh();
            return;
        }
        std::error_code ec;
        if (!fs::is_directory(currentPath, ec)) {
            ReportError("Cannot create a document here: " + currentPath);
            return;
        }
        std::string dest = UniqueChildPath("New " + type.label + "." + type.extension);
        if (!type.templatePath.empty() && fs::exists(type.templatePath, ec)) {
            fs::copy_file(type.templatePath, dest, ec);
            if (ec) { ReportError("New document failed: " + ec.message()); return; }
        } else {
            std::ofstream out(dest, std::ios::binary);
            if (!out) { ReportError("New document failed: " + dest); return; }
        }
        Refresh();
        // Put the fresh file straight into rename mode.
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].path == dest) { StartRename(i); break; }
        }
    }

    void UltraCanvasFilerWidget::AddOpenWithApp(const FilerOpenWithApp& app) {
        openWithApps.push_back(app);
    }

    void UltraCanvasFilerWidget::ClearOpenWithApps() {
        openWithApps.clear();
    }

    void UltraCanvasFilerWidget::ReportError(const std::string& message) {
        if (onError) onError(message);
        else std::cerr << "UltraCanvasFilerWidget: " << message << std::endl;
    }

    // ===== LAYOUT =====
    Rect2Di UltraCanvasFilerWidget::ContentBounds() const {
        auto b = GetLocalBounds();
        int pad = style.outerPadding;
        return Rect2Di(static_cast<int>(b.x) + pad, static_cast<int>(b.y) + pad,
                       std::max(0, static_cast<int>(b.width) - 2 * pad),
                       std::max(0, static_cast<int>(b.height) - 2 * pad
                                   - InfoBarHeight()));
    }

    int UltraCanvasFilerWidget::ThumbnailEdge() const {
        switch (viewType) {
            case FilerViewType::ThumbnailsSmall:     return style.thumbnailSmall;
            case FilerViewType::ThumbnailsMedium:    return style.thumbnailMedium;
            case FilerViewType::ThumbnailsBig:       return style.thumbnailBig;
            case FilerViewType::ThumbnailsMaximized: return style.thumbnailMaximized;
            default:                                 return style.thumbnailMedium;
        }
    }

    void UltraCanvasFilerWidget::EnsureLayout() {
        auto b = GetLocalBounds();
        int w = static_cast<int>(b.width), h = static_cast<int>(b.height);
        if (!layoutValid || w != lastAreaW || h != lastAreaH) {
            lastAreaW = w;
            lastAreaH = h;
            RecomputeLayout();
            layoutValid = true;
        }
    }

    void UltraCanvasFilerWidget::RecomputeLayout() {
        items.clear();
        detailsColumns.clear();
        contentWidth = contentHeight = 0;
        Rect2Di area = ContentBounds();
        if (area.width <= 0 || area.height <= 0) return;

        switch (viewType) {
            case FilerViewType::Details:   LayoutDetails(area); break;
            case FilerViewType::List:      LayoutList(area); break;
            case FilerViewType::ThumbnailsSmall:
            case FilerViewType::ThumbnailsMedium:
            case FilerViewType::ThumbnailsBig:
            case FilerViewType::ThumbnailsMaximized:
                LayoutThumbnails(area);
                break;
            case FilerViewType::BarSize:   LayoutBarSize(area); break;
            case FilerViewType::TreeMap:   LayoutTreeMap(area); break;
            case FilerViewType::GourceTree:
            case FilerViewType::View3D:
                contentHeight = area.height;   // placeholder page, no scrolling
                break;
        }
        ClampScroll();
    }

    void UltraCanvasFilerWidget::LayoutDetails(const Rect2Di& area) {
        // Fixed columns; the name column absorbs the remaining width.
        struct Fixed { FilerSortField field; const char* title; int width;
                       bool right; bool sortable; };
        static const Fixed fixedCols[] = {
            {FilerSortField::Size,         "Size",     90,  true,  true},
            {FilerSortField::Type,         "Type",     130, false, true},
            {FilerSortField::ModifiedDate, "Modified", 150, false, true},
            {FilerSortField::CreatedDate,  "Created",  150, false, true},
            {FilerSortField::Name,         "Attr",     55,  false, false},
            {FilerSortField::Name,         "Info",     120, false, false},
        };
        int fixedTotal = 0;
        for (const Fixed& f : fixedCols) fixedTotal += f.width;
        int scrollbarGutter = 10;
        int nameWidth = std::max(180, area.width - fixedTotal - scrollbarGutter);

        int x = area.x;
        DetailsColumn name;
        name.field = FilerSortField::Name;
        name.title = "Name";
        name.x = x; name.width = nameWidth;
        detailsColumns.push_back(name);
        x += nameWidth;
        for (const Fixed& f : fixedCols) {
            DetailsColumn c;
            c.field = f.field; c.title = f.title;
            c.x = x; c.width = f.width;
            c.rightAligned = f.right;
            c.sortable = f.sortable;
            detailsColumns.push_back(c);
            x += f.width;
        }

        int rowH = style.detailsRowHeight;
        int y = area.y + detailsHeaderHeight;
        for (size_t i = 0; i < entries.size(); ++i) {
            ItemLayout it;
            it.entryIndex = i;
            it.rect = Rect2Di(area.x, y, x - area.x, rowH);
            it.imageRect = Rect2Di(area.x + 4, y + 3, rowH - 6, rowH - 6);
            items.push_back(it);
            y += rowH;
        }
        contentWidth = area.width;
        contentHeight = y + style.outerPadding;
    }

    void UltraCanvasFilerWidget::LayoutList(const Rect2Di& area) {
        int rowH = style.listRowHeight;
        int colW = style.listColumnWidth;
        int gap = 12;
        int rowsPerColumn = std::max(1, area.height / rowH);
        for (size_t i = 0; i < entries.size(); ++i) {
            int col = static_cast<int>(i) / rowsPerColumn;
            int row = static_cast<int>(i) % rowsPerColumn;
            ItemLayout it;
            it.entryIndex = i;
            it.rect = Rect2Di(area.x + col * (colW + gap), area.y + row * rowH,
                              colW, rowH);
            it.imageRect = Rect2Di(it.rect.x + 2, it.rect.y + 2,
                                   rowH - 4, rowH - 4);
            items.push_back(it);
        }
        int cols = (static_cast<int>(entries.size()) + rowsPerColumn - 1)
                   / std::max(1, rowsPerColumn);
        contentWidth = cols * (colW + gap) - gap + 2 * style.outerPadding;
        contentHeight = area.height;
    }

    void UltraCanvasFilerWidget::LayoutThumbnails(const Rect2Di& area) {
        int edge = ThumbnailEdge();
        int gap = style.tileGap;
        int capH = style.captionHeight;
        int tileW = edge;
        int tileH = edge + capH;
        int scrollbarGutter = 10;
        int availW = area.width - scrollbarGutter;
        int cols = std::max(1, (availW + gap) / (tileW + gap));
        for (size_t i = 0; i < entries.size(); ++i) {
            int col = static_cast<int>(i) % cols;
            int row = static_cast<int>(i) / cols;
            ItemLayout it;
            it.entryIndex = i;
            it.rect = Rect2Di(area.x + col * (tileW + gap),
                              area.y + row * (tileH + gap), tileW, tileH);
            it.imageRect = Rect2Di(it.rect.x, it.rect.y, tileW, edge);
            items.push_back(it);
        }
        int rows = (static_cast<int>(entries.size()) + cols - 1) / cols;
        contentWidth = area.width;
        contentHeight = area.y + rows * (tileH + gap) - gap + style.outerPadding;
        if (rows == 0) contentHeight = area.height;
    }

    void UltraCanvasFilerWidget::LayoutBarSize(const Rect2Di& area) {
        EnsureEffectiveSizes();
        int rowH = style.detailsRowHeight;
        int y = area.y;
        for (size_t i = 0; i < entries.size(); ++i) {
            ItemLayout it;
            it.entryIndex = i;
            it.rect = Rect2Di(area.x, y, area.width - 10, rowH);
            it.imageRect = Rect2Di(area.x + 4, y + 3, rowH - 6, rowH - 6);
            items.push_back(it);
            y += rowH;
        }
        contentWidth = area.width;
        contentHeight = y + style.outerPadding;
    }

    // Squarified treemap: lay the weighted entries into `area` so every cell's
    // aspect ratio stays close to 1 (Bruls / Huizing / van Wijk).
    void UltraCanvasFilerWidget::LayoutTreeMap(const Rect2Di& area) {
        EnsureEffectiveSizes();
        if (entries.empty()) { contentHeight = area.height; return; }

        std::vector<std::pair<size_t, double>> weighted;
        weighted.reserve(entries.size());
        for (size_t i = 0; i < entries.size(); ++i) {
            weighted.emplace_back(i, double(std::max<uint64_t>(1, entries[i].effectiveSize)));
        }
        std::stable_sort(weighted.begin(), weighted.end(),
                         [](const auto& a, const auto& b) { return a.second > b.second; });

        // Normalize weights to the pixel area.
        double totalWeight = 0;
        for (auto& wp : weighted) totalWeight += wp.second;
        double areaPixels = double(area.width) * double(area.height);
        for (auto& wp : weighted) wp.second *= areaPixels / totalWeight;

        SquarifyRange(weighted, 0, weighted.size(), area);
        contentWidth = area.width;
        contentHeight = area.height;   // treemap always fits the viewport
    }

    void UltraCanvasFilerWidget::SquarifyRange(
            std::vector<std::pair<size_t, double>>& weighted,
            size_t first, size_t last, Rect2Di rect) {
        while (first < last && rect.width > 0 && rect.height > 0) {
            bool horizontal = rect.width >= rect.height;   // row along the short side
            double side = horizontal ? rect.height : rect.width;
            side = std::max(1.0, side);

            // Grow the current row while the worst aspect ratio improves.
            size_t rowEnd = first;
            double rowSum = 0, rowMax = 0, rowMin = 1e300, worst = 1e300;
            while (rowEnd < last) {
                double w = weighted[rowEnd].second;
                double ns = rowSum + w;
                double nMax = std::max(rowMax, w);
                double nMin = std::min(rowMin, w);
                double nWorst = std::max(side * side * nMax / (ns * ns),
                                         ns * ns / (side * side * nMin));
                if (rowEnd > first && nWorst > worst) break;
                rowSum = ns; rowMax = nMax; rowMin = nMin; worst = nWorst;
                ++rowEnd;
            }

            double thickness = rowSum / side;   // row depth in pixels
            double offset = 0;
            for (size_t k = first; k < rowEnd; ++k) {
                double len = weighted[k].second / std::max(1e-9, thickness);
                ItemLayout it;
                it.entryIndex = weighted[k].first;
                if (horizontal) {
                    it.rect = Rect2Di(rect.x, rect.y + int(std::floor(offset)),
                                      std::max(1, int(std::lround(thickness))),
                                      std::max(1, int(std::lround(len))));
                } else {
                    it.rect = Rect2Di(rect.x + int(std::floor(offset)), rect.y,
                                      std::max(1, int(std::lround(len))),
                                      std::max(1, int(std::lround(thickness))));
                }
                it.imageRect = it.rect;
                items.push_back(it);
                offset += len;
            }

            int t = std::max(1, int(std::lround(thickness)));
            if (horizontal) {
                rect.x += t;
                rect.width = std::max(0, rect.width - t);
            } else {
                rect.y += t;
                rect.height = std::max(0, rect.height - t);
            }
            first = rowEnd;
        }
    }

    // ===== SCROLLING =====
    int UltraCanvasFilerWidget::MaxScrollY() const {
        auto b = GetLocalBounds();
        return std::max(0, contentHeight
                           - (static_cast<int>(b.height) - InfoBarHeight()));
    }

    int UltraCanvasFilerWidget::MaxScrollX() const {
        auto b = GetLocalBounds();
        return std::max(0, contentWidth - static_cast<int>(b.width));
    }

    void UltraCanvasFilerWidget::ClampScroll() {
        scrollOffsetY = clampi(scrollOffsetY, 0, MaxScrollY());
        scrollOffsetX = clampi(scrollOffsetX, 0, MaxScrollX());
    }

    UltraCanvasFilerWidget::ScrollbarGeom UltraCanvasFilerWidget::ScrollbarGeometry() const {
        ScrollbarGeom g;
        auto b = GetLocalBounds();
        constexpr int kBarThickness = 6;
        constexpr int kMinThumb = 30;
        if (IsHorizontal()) {
            int maxX = MaxScrollX();
            if (maxX <= 0) return g;
            int y = static_cast<int>(b.y + b.height) - InfoBarHeight()
                    - kBarThickness - 2;
            double frac = static_cast<double>(b.width) / std::max(1, contentWidth);
            int thumbW = std::max(kMinThumb, static_cast<int>(b.width * frac));
            int travel = std::max(0, static_cast<int>(b.width) - thumbW);
            int tx = static_cast<int>(b.x) + (maxX > 0 ? travel * scrollOffsetX / maxX : 0);
            g.active = true; g.horizontal = true; g.travel = travel; g.maxScroll = maxX;
            g.track = Rect2Di(static_cast<int>(b.x), y, static_cast<int>(b.width), kBarThickness);
            g.thumb = Rect2Di(tx, y, thumbW, kBarThickness);
        } else {
            int maxY = MaxScrollY();
            if (maxY <= 0) return g;
            int viewH = static_cast<int>(b.height) - InfoBarHeight();
            int x = static_cast<int>(b.x + b.width) - kBarThickness - 2;
            double frac = static_cast<double>(viewH) / std::max(1, contentHeight);
            int thumbH = std::max(kMinThumb, static_cast<int>(viewH * frac));
            int travel = std::max(0, viewH - thumbH);
            int ty = static_cast<int>(b.y) + (maxY > 0 ? travel * scrollOffsetY / maxY : 0);
            g.active = true; g.horizontal = false; g.travel = travel; g.maxScroll = maxY;
            g.track = Rect2Di(x, static_cast<int>(b.y), kBarThickness, viewH);
            g.thumb = Rect2Di(x, ty, kBarThickness, thumbH);
        }
        return g;
    }

    void UltraCanvasFilerWidget::ScrollThumbTo(int thumbLeadPx) {
        ScrollbarGeom g = ScrollbarGeometry();
        if (!g.active || g.travel <= 0) return;
        auto b = GetLocalBounds();
        if (g.horizontal) {
            int rel = clampi(thumbLeadPx - static_cast<int>(b.x), 0, g.travel);
            scrollOffsetX = rel * g.maxScroll / g.travel;
        } else {
            int rel = clampi(thumbLeadPx - static_cast<int>(b.y), 0, g.travel);
            scrollOffsetY = rel * g.maxScroll / g.travel;
        }
        ClampScroll();
    }

    void UltraCanvasFilerWidget::EnsureVisible(size_t entryIndex) {
        EnsureLayout();
        for (const ItemLayout& it : items) {
            if (it.entryIndex != entryIndex) continue;
            auto b = GetLocalBounds();
            if (IsHorizontal()) {
                if (it.rect.x - scrollOffsetX < 0)
                    scrollOffsetX = it.rect.x;
                else if (it.rect.x + it.rect.width - scrollOffsetX > (int)b.width)
                    scrollOffsetX = it.rect.x + it.rect.width - (int)b.width;
            } else {
                int top = (viewType == FilerViewType::Details) ? detailsHeaderHeight : 0;
                int viewH = static_cast<int>(b.height) - InfoBarHeight();
                if (it.rect.y - scrollOffsetY < top)
                    scrollOffsetY = it.rect.y - top;
                else if (it.rect.y + it.rect.height - scrollOffsetY > viewH)
                    scrollOffsetY = it.rect.y + it.rect.height - viewH;
            }
            ClampScroll();
            break;
        }
    }

    // ===== RENDER =====
    void UltraCanvasFilerWidget::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        UltraCanvasUIElement::Render(ctx, dirtyRect);
        EnsureLayout();

        auto lb = GetLocalBounds();
        Rect2Di bounds(static_cast<int>(lb.x), static_cast<int>(lb.y),
                       static_cast<int>(lb.width), static_cast<int>(lb.height));

        ctx->PushState();
        ctx->ClipRect(Rect2Dd(bounds));
        ctx->SetFillPaint(style.backgroundColor);
        ctx->FillRectangle(Rect2Dd(bounds));

        iconMenuHits.clear();

        if (viewType == FilerViewType::GourceTree) {
            DrawPlaceholderView(ctx, bounds,
                                "Force-directed tree view (Gource) — to be implemented");
            DrawSelectionInfoBar(ctx, bounds);
            ctx->PopState();
            return;
        }
        if (viewType == FilerViewType::View3D) {
            DrawPlaceholderView(ctx, bounds, "3D view — to be implemented");
            DrawSelectionInfoBar(ctx, bounds);
            ctx->PopState();
            return;
        }

        if (entries.empty()) {
            ctx->SetTextPaint(style.secondaryTextColor);
            FontStyle fsty;
            fsty.fontFamily = style.fontFamily;
            fsty.fontSize = style.fontSize;
            ctx->SetFontStyle(fsty);
            ctx->DrawTextInRect(currentPath.empty() ? "(no folder)" : "(empty folder)",
                                Rect2Dd(bounds));
            DrawSelectionInfoBar(ctx, bounds);
            ctx->PopState();
            return;
        }

        // Scrolled content. Every drawn tile records the decode it wants;
        // the prefetch pass below adds the next screenful, and the commit
        // rebuilds the worker queue from exactly that set.
        thumbFrameWants.clear();
        ctx->PushState();
        ctx->Translate(-scrollOffsetX, -scrollOffsetY);
        for (const ItemLayout& item : items) {
            int top = item.rect.y - scrollOffsetY;
            int left = item.rect.x - scrollOffsetX;
            if (top + item.rect.height < bounds.y || top > bounds.y + bounds.height) continue;
            if (left + item.rect.width < bounds.x || left > bounds.x + bounds.width) continue;
            bool hov = (static_cast<int>(item.entryIndex) == hoveredIndex);
            switch (viewType) {
                case FilerViewType::Details: DrawDetailsRow(ctx, item, hov); break;
                case FilerViewType::List:    DrawListItem(ctx, item, hov); break;
                case FilerViewType::BarSize: DrawBarSizeRow(ctx, item, hov); break;
                case FilerViewType::TreeMap: DrawTreeMapCell(ctx, item, hov); break;
                default:                     DrawThumbnailTile(ctx, item, hov); break;
            }
        }
        // Hover icon menu on top of its item, inside the scrolled space so it
        // tracks the item; hit rects are recorded in content space.
        if (hoverIconMenu && hoveredIndex >= 0 && renamingIndex < 0 &&
            viewType != FilerViewType::TreeMap) {
            for (const ItemLayout& item : items) {
                if (static_cast<int>(item.entryIndex) == hoveredIndex) {
                    DrawHoverIconMenu(ctx, item);
                    break;
                }
            }
        }
        if (renamingIndex >= 0) {
            for (const ItemLayout& item : items) {
                if (static_cast<int>(item.entryIndex) == renamingIndex) {
                    DrawRenameEditor(ctx, item);
                    break;
                }
            }
        }
        ctx->PopState();

        PrefetchThumbnails(ctx, bounds);
        CommitThumbnailWants();

        if (viewType == FilerViewType::Details) DrawDetailsHeader(ctx, bounds);
        DrawScrollbar(ctx);
        DrawSelectionInfoBar(ctx, bounds);
        ctx->PopState();
    }

    void UltraCanvasFilerWidget::DrawPlaceholderView(IRenderContext* ctx,
                                                     const Rect2Di& bounds,
                                                     const std::string& message) {
        ctx->SetTextPaint(style.secondaryTextColor);
        FontStyle fsty;
        fsty.fontFamily = style.fontFamily;
        fsty.fontSize = style.fontSize + 2;
        ctx->SetFontStyle(fsty);
        ctx->DrawTextInRect(message, Rect2Dd(bounds));
    }

    std::string UltraCanvasFilerWidget::EllipsizeText(IRenderContext* ctx,
                                                      const std::string& text,
                                                      int maxWidth) const {
        if (maxWidth <= 0) return "";
        Size2Di ts = ctx->GetTextLineDimensions(text);
        if (ts.width <= maxWidth) return text;
        std::string s = text;
        while (s.size() > 1) {
            // Trim whole UTF-8 code points so multibyte names stay valid.
            size_t cut = s.size() - 1;
            while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
            s.erase(cut);
            ts = ctx->GetTextLineDimensions(s + "…");
            if (ts.width <= maxWidth) return s + "…";
        }
        return "…";
    }

    void UltraCanvasFilerWidget::DrawSelectionState(IRenderContext* ctx,
                                                    const ItemLayout& item,
                                                    bool hovered) {
        bool selected = std::find(selection.begin(), selection.end(),
                                  item.entryIndex) != selection.end();
        if (selected) {
            ctx->SetFillPaint(style.selectionColor);
            ctx->FillRectangle(Rect2Dd(item.rect));
        } else if (hovered) {
            ctx->SetFillPaint(style.hoverColor);
            ctx->FillRectangle(Rect2Dd(item.rect));
        }
    }

    // ===== ASYNC THUMBNAILS =====
    // The decode results are held per (path, size, fit, scale) so the same
    // file can appear at several sizes (details icon vs. thumbnail tile).
    std::string UltraCanvasFilerWidget::ThumbSlotKey(const std::string& path,
                                                     int w, int h,
                                                     ImageFitMode fit,
                                                     float scale) {
        return path + '|' + std::to_string(w) + 'x' + std::to_string(h)
             + '|' + std::to_string(static_cast<int>(fit))
             + '|' + std::to_string(static_cast<int>(scale * 100.0f));
    }

    std::string UltraCanvasFilerWidget::ThumbSourceFor(const FilerEntry& e) const {
        if (!e.thumbnailPath.empty()) return e.thumbnailPath;
        if (e.category == FilerFileCategory::Image ||
            e.category == FilerFileCategory::Vector) {
            return e.path;
        }
        return {};
    }

    void UltraCanvasFilerWidget::ThumbGeometryForItem(const ItemLayout& item,
                                                      Rect2Di& outRect,
                                                      ImageFitMode& outFit) const {
        switch (viewType) {
            case FilerViewType::ThumbnailsSmall:
            case FilerViewType::ThumbnailsMedium:
            case FilerViewType::ThumbnailsBig:
            case FilerViewType::ThumbnailsMaximized: {
                int inset = std::max(4, item.imageRect.width / 12);
                outRect = Rect2Di(item.imageRect.x + inset,
                                  item.imageRect.y + inset,
                                  item.imageRect.width - 2 * inset,
                                  item.imageRect.height - 2 * inset);
                // ScaleDown keeps images smaller than the tile at their
                // original size (centered) instead of blowing them up.
                outFit = ImageFitMode::ScaleDown;
                break;
            }
            default:
                outRect = item.imageRect;
                outFit = ImageFitMode::Contain;
                break;
        }
    }

    std::shared_ptr<UCPixmap> UltraCanvasFilerWidget::AcquireThumbnail(
            const std::string& path, int w, int h,
            ImageFitMode fit, float scale) {
        if (w <= 0 || h <= 0 || path.empty()) return nullptr;
        const std::string key = ThumbSlotKey(path, w, h, fit, scale);
        std::shared_ptr<std::vector<uint8_t>> blob;
        {
            std::lock_guard<std::mutex> lk(thumbMutex);
            auto it = thumbSlots.find(key);
            if (it != thumbSlots.end()) {
                if (it->second.state == ThumbState::Ready) {
                    if (it->second.pixmap) return it->second.pixmap;
                    // Compressed slot: serve from the hot cache, or take a
                    // reference to the blob and inflate outside the lock.
                    auto hot = thumbHot.find(key);
                    if (hot != thumbHot.end()) {
                        hot->second.tick = ++thumbHotTick;
                        return hot->second.pixmap;
                    }
                    blob = it->second.qoi;
                }
                if (!blob) {
                    if (it->second.state == ThumbState::Failed) return nullptr;
                    // Pending: fall through and re-record the want so the
                    // item keeps its place when the queue is rebuilt.
                }
            } else {
                thumbSlots.emplace(key, ThumbSlot{});
            }
        }

        if (blob) {
            // ~0.1-2 ms per tile, only when a tile (re-)enters the drawn
            // band; repaints afterwards hit the hot cache above.
            auto pm = QoiDecompressPixmap(*blob);
            if (!pm) return nullptr;   // cannot happen for our own blobs
            std::lock_guard<std::mutex> lk(thumbMutex);
            HotThumb& he = thumbHot[key];
            if (!he.pixmap) {
                he.pixmap = pm;
                he.bytes = static_cast<size_t>(pm->GetRawWidth())
                         * static_cast<size_t>(pm->GetRawHeight()) * 4;
                thumbHotBytes += he.bytes;
            }
            he.tick = ++thumbHotTick;
            // Evict least-recently-drawn tiles beyond the hot budget — it
            // only needs to cover the visible + prefetch bands.
            constexpr size_t kHotBudgetBytes = 32 * 1024 * 1024;
            while (thumbHotBytes > kHotBudgetBytes && thumbHot.size() > 1) {
                auto oldest = thumbHot.end();
                for (auto hit = thumbHot.begin(); hit != thumbHot.end(); ++hit) {
                    if (hit->first == key) continue;
                    if (oldest == thumbHot.end() ||
                        hit->second.tick < oldest->second.tick) {
                        oldest = hit;
                    }
                }
                if (oldest == thumbHot.end()) break;
                thumbHotBytes -= oldest->second.bytes;
                thumbHot.erase(oldest);
            }
            return pm;
        }

        thumbFrameWants.push_back(ThumbRequest{path, w, h, fit, scale, 0});
        return nullptr;
    }

    void UltraCanvasFilerWidget::PrefetchThumbnails(IRenderContext* ctx,
                                                    const Rect2Di& bounds) {
        // The treemap always fills the viewport (nothing to scroll to), and
        // the placeholder views draw no entries.
        if (viewType == FilerViewType::TreeMap) return;

        const bool horiz = IsHorizontal();
        const int viewEnd = horiz ? bounds.x + bounds.width
                                  : bounds.y + bounds.height;
        const int band = horiz ? bounds.width : bounds.height;
        const float scale = ctx->GetDeviceScale();

        for (const ItemLayout& item : items) {
            int lead = horiz ? item.rect.x - scrollOffsetX
                             : item.rect.y - scrollOffsetY;
            // Items overlapping the viewport were already requested by their
            // draw call; take only the next viewport-sized band past it.
            if (lead <= viewEnd || lead > viewEnd + band) continue;
            std::string src = ThumbSourceFor(entries[item.entryIndex]);
            if (src.empty()) continue;
            Rect2Di r;
            ImageFitMode fit;
            ThumbGeometryForItem(item, r, fit);
            AcquireThumbnail(src, r.width, r.height, fit, scale);
        }
    }

    void UltraCanvasFilerWidget::CommitThumbnailWants() {
        std::lock_guard<std::mutex> lk(thumbMutex);
        // The queue is rebuilt from scratch each frame in want order —
        // visible tiles first, prefetch band after — so decode priority
        // always tracks the current viewport.
        thumbQueue.clear();
        std::unordered_set<std::string> wantedKeys;
        wantedKeys.reserve(thumbFrameWants.size());
        for (ThumbRequest& r : thumbFrameWants) {
            r.generation = thumbGeneration;
            wantedKeys.insert(ThumbSlotKey(r.path, r.w, r.h, r.fit, r.scale));
            thumbQueue.push_back(r);
        }
        // Forget pending slots that fell out of the visible + prefetch
        // bands: files scrolled past are never decoded. (Finished slots are
        // kept for scroll-back; the byte budget bounds those.)
        for (auto it = thumbSlots.begin(); it != thumbSlots.end();) {
            if (it->second.state == ThumbState::Pending &&
                wantedKeys.find(it->first) == wantedKeys.end()) {
                it = thumbSlots.erase(it);
            } else {
                ++it;
            }
        }
        thumbFrameWants.clear();
        if (!thumbQueue.empty()) {
            StartThumbnailWorkersLocked();
            thumbCond.notify_all();
        }
    }

    void UltraCanvasFilerWidget::StartThumbnailWorkersLocked() {
        if (!thumbWorkers.empty() || thumbShutdown) return;
        unsigned hw = std::thread::hardware_concurrency();
        unsigned count = std::min(4u, std::max(1u, hw / 2));
        thumbWorkers.reserve(count);
        for (unsigned i = 0; i < count; ++i) {
            thumbWorkers.emplace_back([this]() { ThumbnailWorkerMain(); });
        }
    }

    void UltraCanvasFilerWidget::StopThumbnailWorkers() {
        {
            std::lock_guard<std::mutex> lk(thumbMutex);
            thumbShutdown = true;
            thumbQueue.clear();
        }
        thumbCond.notify_all();
        for (std::thread& t : thumbWorkers) {
            if (t.joinable()) t.join();
        }
        thumbWorkers.clear();
    }

    void UltraCanvasFilerWidget::DropThumbnailCache() {
        std::lock_guard<std::mutex> lk(thumbMutex);
        ++thumbGeneration;   // results of in-flight decodes are discarded
        thumbQueue.clear();
        thumbSlots.clear();
        thumbBytes = 0;
        thumbHot.clear();
        thumbHotBytes = 0;
    }

    void UltraCanvasFilerWidget::SetCompressedThumbnails(bool enabled) {
        if (compressedThumbs.exchange(enabled) == enabled) return;
        // Existing slots hold the other representation; drop them and let
        // the visible tiles re-decode into the new one.
        DropThumbnailCache();
        RequestRedraw();
    }

    UltraCanvasFilerWidget::ThumbCacheStats
    UltraCanvasFilerWidget::GetThumbnailCacheStats() const {
        ThumbCacheStats st;
        std::lock_guard<std::mutex> lk(thumbMutex);
        for (const auto& kv : thumbSlots) {
            if (kv.second.state != ThumbState::Ready) continue;
            ++st.entries;
            st.storedBytes += kv.second.bytes;
            st.rawBytes += kv.second.rawBytes;
        }
        st.hotEntries = thumbHot.size();
        st.hotBytes = thumbHotBytes;
        return st;
    }

    void UltraCanvasFilerWidget::ThumbnailWorkerMain() {
        // Keeps the retained pixmap bytes bounded: browsing a huge folder in a
        // big tile size cannot grow without limit. On overflow the finished
        // slots are simply dropped — anything still visible is re-queued by
        // the next draw and comes straight back from the shared pixmap cache.
        constexpr size_t kThumbBudgetBytes = 96 * 1024 * 1024;

        for (;;) {
            ThumbRequest req;
            {
                std::unique_lock<std::mutex> lk(thumbMutex);
                for (;;) {
                    if (thumbShutdown) return;
                    // Pick the highest-priority request that is still worth
                    // decoding (slot pending) and whose file no other worker
                    // is on right now (same file at two sizes must
                    // serialize). Entries whose slot was pruned or already
                    // finished — the queue is rebuilt every frame and may
                    // repeat in-flight work — are dropped on the way.
                    auto qit = thumbQueue.begin();
                    while (qit != thumbQueue.end()) {
                        auto sit = thumbSlots.find(ThumbSlotKey(
                                qit->path, qit->w, qit->h, qit->fit, qit->scale));
                        if (sit == thumbSlots.end() ||
                            sit->second.state != ThumbState::Pending) {
                            qit = thumbQueue.erase(qit);
                            continue;
                        }
                        if (thumbPathsInFlight.count(qit->path) == 0) break;
                        ++qit;
                    }
                    if (qit != thumbQueue.end()) {
                        req = std::move(*qit);
                        thumbQueue.erase(qit);
                        thumbPathsInFlight.insert(req.path);
                        break;
                    }
                    thumbCond.wait(lk);
                }
            }

            // The expensive part — outside the lock. UCImage::Get and
            // GetPixmap populate the shared mutex-guarded caches, so later
            // synchronous users (e.g. the media viewer) get free cache hits.
            std::shared_ptr<UCPixmap> pm;
            auto img = UCImage::Get(req.path);
            if (img && img->GetWidth() > 0 && img->GetHeight() > 0) {
                pm = img->GetPixmap(req.w, req.h, req.fit, req.scale);
            }

            // "Compressed thumbnails": deflate here on the worker so the UI
            // thread never pays for compression; the slot then holds the
            // blob instead of the raw pixmap.
            std::shared_ptr<std::vector<uint8_t>> blob;
            if (pm && compressedThumbs.load()) {
                std::vector<uint8_t> v = QoiCompressPixmap(*pm);
                if (!v.empty()) {
                    blob = std::make_shared<std::vector<uint8_t>>(std::move(v));
                }
            }

            bool report = false;
            {
                std::lock_guard<std::mutex> lk(thumbMutex);
                thumbPathsInFlight.erase(req.path);
                if (thumbShutdown) return;
                if (req.generation == thumbGeneration) {
                    const std::string key = ThumbSlotKey(req.path, req.w, req.h,
                                                         req.fit, req.scale);
                    ThumbSlot& slot = thumbSlots[key];
                    if (pm) {
                        slot.state = ThumbState::Ready;
                        slot.rawBytes = static_cast<size_t>(pm->GetRawWidth())
                                      * static_cast<size_t>(pm->GetRawHeight()) * 4;
                        if (blob) {
                            slot.qoi = blob;
                            slot.pixmap = nullptr;
                            slot.bytes = blob->size();
                        } else {
                            slot.pixmap = pm;
                            slot.qoi = nullptr;
                            slot.bytes = slot.rawBytes;
                        }
                        thumbBytes += slot.bytes;
                        if (thumbBytes > kThumbBudgetBytes) {
                            for (auto sit = thumbSlots.begin();
                                 sit != thumbSlots.end();) {
                                if (sit->first != key &&
                                    sit->second.state == ThumbState::Ready) {
                                    auto hit = thumbHot.find(sit->first);
                                    if (hit != thumbHot.end()) {
                                        thumbHotBytes -= hit->second.bytes;
                                        thumbHot.erase(hit);
                                    }
                                    sit = thumbSlots.erase(sit);
                                } else {
                                    ++sit;
                                }
                            }
                            thumbBytes = slot.bytes;
                        }
                    } else {
                        slot.state = ThumbState::Failed;   // don't retry-loop
                        slot.pixmap = nullptr;
                        slot.qoi = nullptr;
                    }
                    report = true;
                }
            }
            // A sibling worker may be parked on a queued request for the
            // path just released.
            thumbCond.notify_all();
            if (report) PostThumbnailRedraw();
        }
    }

    void UltraCanvasFilerWidget::PostThumbnailRedraw() {
        // Coalesced: one queued UI task repaints however many thumbnails
        // finished before it ran.
        if (thumbRedrawPosted.exchange(true)) return;
        UltraCanvasApplicationBase* app = UltraCanvasApplicationBase::GetCurrent();
        if (!app) {
            thumbRedrawPosted.store(false);
            return;
        }
        auto alive = thumbAlive;
        app->PostToUIThread([this, alive]() {
            if (!alive->load()) return;   // widget destroyed meanwhile
            thumbRedrawPosted.store(false);
            RequestRedraw();
        });
    }

    void UltraCanvasFilerWidget::DrawEntryIcon(IRenderContext* ctx, const FilerEntry& e,
                                               const Rect2Di& rect,
                                               ImageFitMode imageFit) {
        if (rect.width <= 2 || rect.height <= 2) return;

        // Real image thumbnails (explicit thumbnail, else the bitmap itself).
        // Never decoded here: the frame must not wait for image files, so the
        // pixmap is fetched from the async loader and the tile shows the
        // generic glyph until its decode lands (which then repaints us).
        std::string thumb = ThumbSourceFor(e);
        if (!thumb.empty()) {
            auto pm = AcquireThumbnail(thumb, rect.width, rect.height,
                                       imageFit, ctx->GetDeviceScale());
            if (pm) {
                ctx->DrawPixmap(*pm, Rect2Dd(rect), imageFit);
                return;
            }
        }

        Color color = CategoryColor(e.category);
        if (e.isDirectory) {
            // Folder shape: a tab above the body.
            double tabW = rect.width * 0.45, tabH = std::max(2.0, rect.height * 0.18);
            ctx->SetFillPaint(color);
            ctx->FillRoundedRectangle(Rect2Dd(rect.x, rect.y, tabW, tabH * 2), 2);
            Color body(std::min(255, color.r + 20), std::min(255, color.g + 20),
                       std::min(255, color.b + 25), 255);
            ctx->SetFillPaint(body);
            ctx->FillRoundedRectangle(Rect2Dd(rect.x, rect.y + tabH,
                                              rect.width, rect.height - tabH), 2);
            return;
        }

        // Generic file glyph: a colored "sheet" with the extension on it.
        ctx->SetFillPaint(Color(252, 252, 253, 255));
        ctx->FillRoundedRectangle(Rect2Dd(rect), 2);
        ctx->SetStrokePaint(Color(0, 0, 0, 40));
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRoundedRectangle(Rect2Dd(rect), 2);
        double bandH = std::max(3.0, rect.height * 0.30);
        ctx->SetFillPaint(color);
        ctx->FillRoundedRectangle(Rect2Dd(rect.x, rect.y + rect.height - bandH,
                                          rect.width, bandH), 2);
        if (rect.height >= 26 && !e.extension.empty()) {
            std::string ext = e.extension.substr(0, 4);
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return std::toupper(c); });
            FontStyle fsty;
            fsty.fontFamily = style.fontFamily;
            fsty.fontSize = std::max(8.0, rect.height * 0.24);
            fsty.fontWeight = FontWeight::Bold;
            ctx->SetFontStyle(fsty);
            ctx->SetTextPaint(Color(60, 60, 66, 255));
            Size2Di ts = ctx->GetTextLineDimensions(ext);
            ctx->DrawText(ext, Point2Dd(rect.x + (rect.width - ts.width) / 2.0,
                                        rect.y + (rect.height - bandH - ts.height) / 2.0));
        }
    }

    void UltraCanvasFilerWidget::DrawDetailsHeader(IRenderContext* ctx,
                                                   const Rect2Di& bounds) {
        Rect2Di area = ContentBounds();
        Rect2Di header(bounds.x, bounds.y, bounds.width,
                       area.y - bounds.y + detailsHeaderHeight);
        ctx->SetFillPaint(style.headerBackground);
        ctx->FillRectangle(Rect2Dd(header));
        ctx->SetStrokePaint(style.gridLineColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine(Point2Dd(header.x, header.y + header.height),
                      Point2Dd(header.x + header.width, header.y + header.height));

        FontStyle fsty;
        fsty.fontFamily = style.fontFamily;
        fsty.fontSize = style.fontSize;
        fsty.fontWeight = FontWeight::Bold;
        ctx->SetFontStyle(fsty);
        for (const DetailsColumn& c : detailsColumns) {
            std::string title = c.title;
            if (c.sortable && c.field == sortField &&
                (c.title != "Attr" && c.title != "Info")) {
                title += sortAscending ? " ▲" : " ▼";
            }
            ctx->SetTextPaint(style.headerTextColor);
            Size2Di ts = ctx->GetTextLineDimensions(title);
            int tx = c.rightAligned ? c.x + c.width - ts.width - 8 : c.x + 6;
            int ty = area.y + (detailsHeaderHeight - ts.height) / 2;
            ctx->DrawText(title, Point2Dd(tx, ty));
        }
    }

    void UltraCanvasFilerWidget::DrawDetailsRow(IRenderContext* ctx,
                                                const ItemLayout& item, bool hovered) {
        const FilerEntry& e = entries[item.entryIndex];
        DrawSelectionState(ctx, item, hovered);
        DrawEntryIcon(ctx, e, item.imageRect);

        FontStyle fsty;
        fsty.fontFamily = style.fontFamily;
        fsty.fontSize = style.fontSize;
        ctx->SetFontStyle(fsty);

        int textInsetTop = 0;
        for (const DetailsColumn& c : detailsColumns) {
            std::string value;
            Color color = style.textColor;
            if (c.title == "Name") {
                value = e.name;
            } else if (c.title == "Size") {
                value = e.isDirectory ? "" : FormatSize(e.size);
                color = style.secondaryTextColor;
            } else if (c.title == "Type") {
                value = e.typeName;
                color = style.secondaryTextColor;
            } else if (c.title == "Modified") {
                value = FormatTime(e.modifiedTime);
                color = style.secondaryTextColor;
            } else if (c.title == "Created") {
                value = FormatTime(e.createdTime);
                color = style.secondaryTextColor;
            } else if (c.title == "Attr") {
                value = e.attributes;
                color = style.secondaryTextColor;
            } else if (c.title == "Info") {
                value = e.info;
                color = style.secondaryTextColor;
            }
            if (value.empty()) continue;

            int pad = 6;
            int textX = c.x + pad;
            int avail = c.width - 2 * pad;
            if (c.title == "Name") {
                textX = item.imageRect.x + item.imageRect.width + 6;
                avail = c.x + c.width - textX - pad;
            }
            std::string shown = EllipsizeText(ctx, value, avail);
            ctx->SetTextPaint(color);
            Size2Di ts = ctx->GetTextLineDimensions(shown);
            if (textInsetTop == 0) textInsetTop = (item.rect.height - ts.height) / 2;
            int tx = c.rightAligned ? c.x + c.width - ts.width - 8 : textX;
            ctx->DrawText(shown, Point2Dd(tx, item.rect.y + textInsetTop));
        }

        ctx->SetStrokePaint(style.gridLineColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine(Point2Dd(item.rect.x, item.rect.y + item.rect.height),
                      Point2Dd(item.rect.x + item.rect.width,
                               item.rect.y + item.rect.height));
    }

    void UltraCanvasFilerWidget::DrawListItem(IRenderContext* ctx,
                                              const ItemLayout& item, bool hovered) {
        const FilerEntry& e = entries[item.entryIndex];
        DrawSelectionState(ctx, item, hovered);
        DrawEntryIcon(ctx, e, item.imageRect);

        FontStyle fsty;
        fsty.fontFamily = style.fontFamily;
        fsty.fontSize = style.fontSize;
        ctx->SetFontStyle(fsty);
        ctx->SetTextPaint(style.textColor);
        int textX = item.imageRect.x + item.imageRect.width + 6;
        int avail = item.rect.x + item.rect.width - textX - 4;
        std::string shown = EllipsizeText(ctx, e.name, avail);
        Size2Di ts = ctx->GetTextLineDimensions(shown);
        ctx->DrawText(shown, Point2Dd(textX,
                item.rect.y + (item.rect.height - ts.height) / 2));
    }

    void UltraCanvasFilerWidget::DrawThumbnailTile(IRenderContext* ctx,
                                                   const ItemLayout& item, bool hovered) {
        const FilerEntry& e = entries[item.entryIndex];
        bool selected = std::find(selection.begin(), selection.end(),
                                  item.entryIndex) != selection.end();
        if (selected || hovered) {
            ctx->SetFillPaint(selected ? style.selectionColor : style.hoverColor);
            ctx->FillRoundedRectangle(Rect2Dd(item.rect), 5);
        }
        // Same geometry the prefetch requests, so the cache keys line up.
        Rect2Di img;
        ImageFitMode fit;
        ThumbGeometryForItem(item, img, fit);
        DrawEntryIcon(ctx, e, img, fit);

        FontStyle fsty;
        fsty.fontFamily = style.fontFamily;
        fsty.fontSize = style.smallFontSize;
        ctx->SetFontStyle(fsty);
        ctx->SetTextPaint(style.textColor);
        std::string shown = EllipsizeText(ctx, e.name, item.rect.width - 8);
        Size2Di ts = ctx->GetTextLineDimensions(shown);
        int capTop = item.imageRect.y + item.imageRect.height;
        ctx->DrawText(shown, Point2Dd(
                item.rect.x + (item.rect.width - ts.width) / 2.0,
                capTop + (style.captionHeight - ts.height) / 2.0));

        if (selected) {
            ctx->SetStrokePaint(style.selectionBorderColor);
            ctx->SetStrokeWidth(2.0f);
            ctx->DrawRoundedRectangle(Rect2Dd(item.rect), 5);
        }
    }

    void UltraCanvasFilerWidget::DrawBarSizeRow(IRenderContext* ctx,
                                                const ItemLayout& item, bool hovered) {
        const FilerEntry& e = entries[item.entryIndex];
        DrawSelectionState(ctx, item, hovered);
        DrawEntryIcon(ctx, e, item.imageRect);

        uint64_t maxSize = 1;
        for (const FilerEntry& x : entries) maxSize = std::max(maxSize, x.effectiveSize);

        FontStyle fsty;
        fsty.fontFamily = style.fontFamily;
        fsty.fontSize = style.fontSize;
        ctx->SetFontStyle(fsty);

        int nameX = item.imageRect.x + item.imageRect.width + 6;
        int nameW = std::min(220, item.rect.width / 3);
        std::string shown = EllipsizeText(ctx, e.name, nameW - 8);
        ctx->SetTextPaint(style.textColor);
        Size2Di ts = ctx->GetTextLineDimensions(shown);
        int textY = item.rect.y + (item.rect.height - ts.height) / 2;
        ctx->DrawText(shown, Point2Dd(nameX, textY));

        // Size bar scaled against the folder's largest entry.
        std::string sizeText = FormatSize(e.effectiveSize);
        Size2Di sts = ctx->GetTextLineDimensions(sizeText);
        int barX = item.rect.x + nameW + item.imageRect.width + 12;
        int barMaxW = item.rect.x + item.rect.width - barX - sts.width - 14;
        if (barMaxW > 20) {
            int barH = std::max(6, item.rect.height - 12);
            int barY = item.rect.y + (item.rect.height - barH) / 2;
            ctx->SetFillPaint(style.barBackground);
            ctx->FillRoundedRectangle(Rect2Dd(barX, barY, barMaxW, barH), 3);
            double frac = double(e.effectiveSize) / double(maxSize);
            int w = std::max(e.effectiveSize > 0 ? 2 : 0,
                             int(std::lround(barMaxW * frac)));
            if (w > 0) {
                Color bar = e.isDirectory ? CategoryColor(FilerFileCategory::Folder)
                                          : style.barColor;
                ctx->SetFillPaint(bar);
                ctx->FillRoundedRectangle(Rect2Dd(barX, barY, w, barH), 3);
            }
            ctx->SetTextPaint(style.secondaryTextColor);
            ctx->DrawText(sizeText, Point2Dd(barX + barMaxW + 8, textY));
        }
    }

    void UltraCanvasFilerWidget::DrawTreeMapCell(IRenderContext* ctx,
                                                 const ItemLayout& item, bool hovered) {
        const FilerEntry& e = entries[item.entryIndex];
        bool selected = std::find(selection.begin(), selection.end(),
                                  item.entryIndex) != selection.end();
        Color base = CategoryColor(e.category);
        // Vary the shade a little by index so equal categories stay separable.
        int delta = int(item.entryIndex % 5) * 6 - 12;
        Color fill(clampi(base.r + delta, 0, 255), clampi(base.g + delta, 0, 255),
                   clampi(base.b + delta, 0, 255), 255);
        if (hovered) fill = Color(clampi(fill.r + 24, 0, 255),
                                  clampi(fill.g + 24, 0, 255),
                                  clampi(fill.b + 24, 0, 255), 255);
        ctx->SetFillPaint(fill);
        ctx->FillRectangle(Rect2Dd(item.rect));
        ctx->SetStrokePaint(Color(255, 255, 255, 200));
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRectangle(Rect2Dd(item.rect));

        if (item.rect.width >= 46 && item.rect.height >= 26) {
            ctx->PushState();
            ctx->ClipRect(Rect2Dd(item.rect));
            FontStyle fsty;
            fsty.fontFamily = style.fontFamily;
            fsty.fontSize = style.smallFontSize;
            fsty.fontWeight = FontWeight::Bold;
            ctx->SetFontStyle(fsty);
            ctx->SetTextPaint(Color(255, 255, 255, 235));
            std::string shown = EllipsizeText(ctx, e.name, item.rect.width - 8);
            ctx->DrawText(shown, Point2Dd(item.rect.x + 4, item.rect.y + 3));
            if (item.rect.height >= 42) {
                fsty.fontWeight = FontWeight::Normal;
                ctx->SetFontStyle(fsty);
                ctx->SetTextPaint(Color(255, 255, 255, 190));
                ctx->DrawText(FormatSize(e.effectiveSize),
                              Point2Dd(item.rect.x + 4,
                                       item.rect.y + 3 + style.smallFontSize + 4));
            }
            ctx->PopState();
        }

        if (selected) {
            ctx->SetStrokePaint(style.selectionBorderColor);
            ctx->SetStrokeWidth(2.0f);
            ctx->DrawRectangle(Rect2Dd(item.rect));
        }
    }

    void UltraCanvasFilerWidget::DrawHoverIconMenu(IRenderContext* ctx,
                                                   const ItemLayout& item) {
        static const IconMenuAction actions[] = {
            IconMenuAction::Copy, IconMenuAction::Cut,
            IconMenuAction::Rename, IconMenuAction::Delete,
        };
        int sz = style.iconMenuButtonSize;
        int gap = 2;
        int total = 4 * sz + 3 * gap;
        int x = item.rect.x + item.rect.width - total - 4;
        int y = item.rect.y + 2;
        for (IconMenuAction a : actions) {
            Rect2Di button(x, y, sz, sz);
            ctx->SetFillPaint(style.iconMenuBackground);
            ctx->FillRoundedRectangle(Rect2Dd(button), 4);
            DrawIconMenuGlyph(ctx, a, button);
            iconMenuHits.push_back({button, item.entryIndex, a});
            x += sz + gap;
        }
    }

    void UltraCanvasFilerWidget::DrawIconMenuGlyph(IRenderContext* ctx,
                                                   IconMenuAction action,
                                                   const Rect2Di& button) {
        // Small vector glyphs so no icon assets are required.
        double cx = button.x + button.width / 2.0;
        double cy = button.y + button.height / 2.0;
        double r = button.width * 0.26;
        ctx->SetStrokePaint(style.iconMenuGlyphColor);
        ctx->SetStrokeWidth(1.4f);
        switch (action) {
            case IconMenuAction::Copy: {
                // Two overlapping sheets.
                ctx->DrawRectangle(Rect2Dd(cx - r, cy - r * 1.2, r * 1.4, r * 1.7));
                ctx->DrawRectangle(Rect2Dd(cx - r * 0.4, cy - r * 0.5, r * 1.4, r * 1.7));
                break;
            }
            case IconMenuAction::Cut: {
                // Scissors: crossing blades + finger rings.
                ctx->DrawLine(Point2Dd(cx - r, cy - r), Point2Dd(cx + r * 0.7, cy + r));
                ctx->DrawLine(Point2Dd(cx + r, cy - r), Point2Dd(cx - r * 0.7, cy + r));
                ctx->DrawCircle(Point2Dd(cx - r * 0.85, cy + r * 1.05), r * 0.35);
                ctx->DrawCircle(Point2Dd(cx + r * 0.85, cy + r * 1.05), r * 0.35);
                break;
            }
            case IconMenuAction::Rename: {
                // Pencil over a baseline.
                ctx->DrawLine(Point2Dd(cx - r, cy + r), Point2Dd(cx + r * 0.9, cy - r * 0.9));
                ctx->DrawLine(Point2Dd(cx - r, cy + r), Point2Dd(cx - r * 0.5, cy + r));
                ctx->DrawLine(Point2Dd(cx - r, cy + r), Point2Dd(cx - r, cy + r * 0.5));
                break;
            }
            case IconMenuAction::Delete: {
                // Trash can: body, lid and handle.
                ctx->DrawRectangle(Rect2Dd(cx - r * 0.8, cy - r * 0.5, r * 1.6, r * 1.6));
                ctx->DrawLine(Point2Dd(cx - r * 1.1, cy - r * 0.5),
                              Point2Dd(cx + r * 1.1, cy - r * 0.5));
                ctx->DrawLine(Point2Dd(cx - r * 0.35, cy - r * 0.5),
                              Point2Dd(cx - r * 0.35, cy - r * 0.95));
                ctx->DrawLine(Point2Dd(cx - r * 0.35, cy - r * 0.95),
                              Point2Dd(cx + r * 0.35, cy - r * 0.95));
                ctx->DrawLine(Point2Dd(cx + r * 0.35, cy - r * 0.95),
                              Point2Dd(cx + r * 0.35, cy - r * 0.5));
                break;
            }
        }
    }

    void UltraCanvasFilerWidget::DrawRenameEditor(IRenderContext* ctx,
                                                  const ItemLayout& item) {
        // The editable field sits where the item's name is shown.
        Rect2Di field;
        if (viewType == FilerViewType::Details || viewType == FilerViewType::List ||
            viewType == FilerViewType::BarSize) {
            int x = item.imageRect.x + item.imageRect.width + 4;
            field = Rect2Di(x, item.rect.y + 1,
                            std::max(80, item.rect.width
                                     - (x - item.rect.x) - 8),
                            item.rect.height - 2);
            if (viewType == FilerViewType::Details && !detailsColumns.empty()) {
                field.width = std::max(80, detailsColumns[0].x
                                       + detailsColumns[0].width - x - 4);
            }
        } else {   // thumbnails / treemap
            int capTop = item.imageRect.y + item.imageRect.height;
            field = Rect2Di(item.rect.x + 2, capTop,
                            item.rect.width - 4, style.captionHeight);
        }

        ctx->SetFillPaint(style.renameFieldColor);
        ctx->FillRectangle(Rect2Dd(field));
        ctx->SetStrokePaint(style.renameBorderColor);
        ctx->SetStrokeWidth(1.5f);
        ctx->DrawRectangle(Rect2Dd(field));

        FontStyle fsty;
        fsty.fontFamily = style.fontFamily;
        fsty.fontSize = style.fontSize;
        ctx->SetFontStyle(fsty);
        ctx->SetTextPaint(style.textColor);
        ctx->PushState();
        ctx->ClipRect(Rect2Dd(field));
        Size2Di ts = ctx->GetTextLineDimensions(renameBuffer);
        int tx = field.x + 4;
        if (ts.width > field.width - 12) tx = field.x + field.width - 12 - ts.width;
        int ty = field.y + (field.height - ts.height) / 2;
        ctx->DrawText(renameBuffer, Point2Dd(tx, ty));
        // Caret at the end of the text.
        ctx->SetStrokePaint(style.textColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine(Point2Dd(tx + ts.width + 1, field.y + 3),
                      Point2Dd(tx + ts.width + 1, field.y + field.height - 3));
        ctx->PopState();
    }

    void UltraCanvasFilerWidget::DrawScrollbar(IRenderContext* ctx) {
        ScrollbarGeom g = ScrollbarGeometry();
        if (!g.active) return;
        Color thumbColor = draggingScrollbar ? Color(70, 70, 78, 220)
                                             : Color(90, 90, 96, 180);
        if (g.horizontal) {
            ctx->SetFillPaint(Color(0, 0, 0, 30));
            ctx->FillRoundedRectangle(Rect2Dd(g.track.x + 2, g.track.y,
                                              g.track.width - 4, g.track.height), 3);
        } else {
            ctx->SetFillPaint(Color(0, 0, 0, 30));
            ctx->FillRoundedRectangle(Rect2Dd(g.track.x, g.track.y + 2,
                                              g.track.width, g.track.height - 4), 3);
        }
        ctx->SetFillPaint(thumbColor);
        ctx->FillRoundedRectangle(Rect2Dd(g.thumb), 3);
    }

    // ===== ASYNC FOLDER STATS =====
    // The recursive walk behind these stats is the single most expensive
    // thing the widget can do (up to kDirSizeEntryCap directory entries per
    // folder — seconds on a big subtree, cold cache or slow storage). It
    // used to run inline here, on the UI thread, so merely selecting a
    // folder — including the first click of the double-click that opens it —
    // froze the window until the whole subtree had been visited. Now the
    // walk runs on a background worker and callers immediately get a
    // placeholder that is filled in by a posted redraw.
    UltraCanvasFilerWidget::FolderStats
    UltraCanvasFilerWidget::GetFolderStats(const std::string& path) {
        std::lock_guard<std::mutex> lk(statsMutex);
        auto it = folderStatsCache.find(path);
        if (it != folderStatsCache.end()) return it->second;

        // Pending marker so repeated calls (every frame while the info bar
        // shows this folder) queue the walk only once.
        folderStatsCache.emplace(path, FolderStats{});
        statsQueue.push_back(path);
        StartFolderStatsWorkerLocked();
        statsCond.notify_one();
        return FolderStats{};
    }

    void UltraCanvasFilerWidget::StartFolderStatsWorkerLocked() {
        if (statsWorker.joinable() || statsShutdown) return;
        statsWorker = std::thread([this]() { FolderStatsWorkerMain(); });
    }

    void UltraCanvasFilerWidget::StopFolderStatsWorker() {
        {
            std::lock_guard<std::mutex> lk(statsMutex);
            statsShutdown = true;
            statsQueue.clear();
        }
        statsCond.notify_all();
        if (statsWorker.joinable()) statsWorker.join();
    }

    void UltraCanvasFilerWidget::FolderStatsWorkerMain() {
        for (;;) {
            std::string path;
            uint64_t gen;
            {
                std::unique_lock<std::mutex> lk(statsMutex);
                statsCond.wait(lk, [this]() {
                    return statsShutdown || !statsQueue.empty();
                });
                if (statsShutdown) return;
                path = std::move(statsQueue.front());
                statsQueue.pop_front();
                gen = statsGeneration;
            }

            // The expensive walk — outside the lock, one folder at a time
            // (a single worker is enough: deep walks are I/O bound).
            FolderStats st;
            st.ready = true;
            std::error_code ec;
            if (fs::is_directory(path, ec)) {
                uint64_t visited = 0;
                for (fs::recursive_directory_iterator rit(
                         path, fs::directory_options::skip_permission_denied, ec), end;
                     rit != end; rit.increment(ec)) {
                    if (ec) break;
                    if (visited++ >= kDirSizeEntryCap) { st.capped = true; break; }
                    std::error_code fec;
                    if (rit->is_directory(fec)) {
                        ++st.folders;
                    } else {
                        ++st.files;
                        uint64_t sz = rit->file_size(fec);
                        if (!fec) st.bytes += sz;
                    }
                }
            }

            bool report = false;
            {
                std::lock_guard<std::mutex> lk(statsMutex);
                if (statsShutdown) return;
                if (gen == statsGeneration) {   // folder view unchanged
                    folderStatsCache[path] = st;
                    report = true;
                }
            }
            if (report) PostFolderStatsRedraw();
        }
    }

    void UltraCanvasFilerWidget::PostFolderStatsRedraw() {
        // Coalesced like the thumbnail redraw: one queued UI task picks up
        // however many finished walks preceded it.
        if (statsRedrawPosted.exchange(true)) return;
        UltraCanvasApplicationBase* app = UltraCanvasApplicationBase::GetCurrent();
        if (!app) {
            statsRedrawPosted.store(false);
            return;
        }
        auto alive = thumbAlive;
        app->PostToUIThread([this, alive]() {
            if (!alive->load()) return;   // widget destroyed meanwhile
            statsRedrawPosted.store(false);
            // New directory weights change the size-weighted geometries.
            if (viewType == FilerViewType::BarSize ||
                viewType == FilerViewType::TreeMap) {
                effectiveSizesValid = false;
                InvalidateFilerLayout();
            }
            RequestRedraw();
        });
    }

    // ===== SELECTION INFO BAR =====

    std::string UltraCanvasFilerWidget::EntryExtraInfo(const FilerEntry& e) const {
        if (e.isDirectory) return "";
        auto it = mediaInfoCache.find(e.path);
        if (it != mediaInfoCache.end()) return it->second;

        std::string out;
        if (e.category == FilerFileCategory::Image) {
            int w = 0, h = 0;
            if (!ProbeImageDimensions(e.path, w, h)) {
                // Unknown container (AVIF, HEIC, ...): ask the shared image
                // cache — the thumbnail views load these files anyway.
                auto img = UCImage::Get(e.path);
                if (img) { w = img->GetWidth(); h = img->GetHeight(); }
            }
            if (w > 0 && h > 0)
                out = std::to_string(w) + " × " + std::to_string(h) + " px";
        } else if (e.category == FilerFileCategory::Audio ||
                   e.category == FilerFileCategory::Video) {
            FilerMediaProbe probe;
            if (ProbeMediaFile(e.path, e.extension, probe)) {
                out = FormatDuration(probe.seconds);
                if (!probe.codec.empty()) {
                    if (!out.empty()) out += " · ";
                    out += probe.codec;
                }
            }
        }
        mediaInfoCache.emplace(e.path, out);
        return out;
    }

    void UltraCanvasFilerWidget::BuildSelectionInfoText(std::string& primary,
                                                        std::string& secondary) {
        primary.clear();
        secondary.clear();

        auto addPart = [&secondary](const std::string& part) {
            if (part.empty()) return;
            if (!secondary.empty()) secondary += " · ";
            secondary += part;
        };
        auto countsText = [](uint64_t files, uint64_t folders) {
            std::string s;
            if (files)
                s += std::to_string(files) + (files == 1 ? " file" : " files");
            if (folders) {
                if (!s.empty()) s += ", ";
                s += std::to_string(folders) + (folders == 1 ? " folder" : " folders");
            }
            return s;
        };

        std::vector<FilerEntry> sel = GetSelectedEntries();

        if (sel.empty()) {
            // Folder summary: entry counts + non-recursive size of its files.
            uint64_t files = 0, folders = 0, bytes = 0;
            for (const FilerEntry& e : entries) {
                if (e.isDirectory) ++folders;
                else { ++files; bytes += e.size; }
            }
            addPart(std::to_string(entries.size())
                    + (entries.size() == 1 ? " item" : " items"));
            if (files && folders) addPart(countsText(files, folders));
            if (files) addPart(FormatSize(bytes));
            return;
        }

        if (sel.size() == 1) {
            const FilerEntry& e = sel.front();
            primary = e.name;
            addPart(e.typeName);
            if (e.isDirectory) {
                FolderStats st = GetFolderStats(e.path);
                if (!st.ready) {
                    // Background walk still running; the finished stats
                    // arrive with a posted redraw.
                    addPart("…");
                } else if (st.files || st.folders || st.bytes) {
                    std::string prefix = st.capped ? "≥ " : "";
                    addPart(prefix + countsText(st.files, st.folders));
                    addPart(prefix + FormatSize(st.bytes));
                }
            } else {
                addPart(FormatSize(e.size));
            }
            std::string extra = EntryExtraInfo(e);
            if (extra.empty()) extra = e.info;   // provider / compression fallback
            addPart(extra);
            addPart(FormatTime(e.modifiedTime));
            if (!e.attributes.empty()) addPart("[" + e.attributes + "]");
            return;
        }

        // Multi selection: counts + summed sizes (folders counted recursively).
        uint64_t files = 0, folders = 0, bytes = 0;
        bool capped = false;
        for (const FilerEntry& e : sel) {
            if (e.isDirectory) {
                ++folders;
                FolderStats st = GetFolderStats(e.path);
                bytes += st.bytes;
                // Walks still pending make the sum a lower bound, same as a
                // capped traversal; the posted redraw refines it.
                capped = capped || st.capped || !st.ready;
            } else {
                ++files;
                bytes += e.size;
            }
        }
        primary = std::to_string(sel.size()) + " items selected";
        addPart(countsText(files, folders));
        addPart(std::string(capped ? "≥ " : "") + FormatSize(bytes) + " total");
    }

    void UltraCanvasFilerWidget::DrawSelectionInfoBar(IRenderContext* ctx,
                                                      const Rect2Di& bounds) {
        int h = InfoBarHeight();
        if (h <= 0 || bounds.height <= h) return;
        Rect2Di bar(bounds.x, bounds.y + bounds.height - h, bounds.width, h);
        ctx->SetFillPaint(style.infoBarBackground);
        ctx->FillRectangle(Rect2Dd(bar));
        ctx->SetStrokePaint(style.gridLineColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine(Point2Dd(bar.x, bar.y), Point2Dd(bar.x + bar.width, bar.y));

        std::string primary, secondary;
        BuildSelectionInfoText(primary, secondary);
        if (primary.empty() && secondary.empty()) return;

        FontStyle fsty;
        fsty.fontFamily = style.fontFamily;
        fsty.fontSize = style.fontSize;

        int pad = 8;
        int x = bar.x + pad;
        int avail = bar.width - 2 * pad;
        if (avail <= 0) return;

        if (!primary.empty()) {
            fsty.fontWeight = FontWeight::Bold;
            ctx->SetFontStyle(fsty);
            // Leave room for the details after a long name.
            int nameMax = secondary.empty() ? avail : std::max(60, avail * 3 / 5);
            std::string shown = EllipsizeText(ctx, primary, nameMax);
            Size2Di ts = ctx->GetTextLineDimensions(shown);
            ctx->SetTextPaint(style.infoBarTextColor);
            ctx->DrawText(shown, Point2Dd(x, bar.y + (h - ts.height) / 2.0));
            x += ts.width;
            avail -= ts.width;
        }
        if (!secondary.empty() && avail > 12) {
            std::string text = primary.empty() ? secondary : ("  ·  " + secondary);
            fsty.fontWeight = FontWeight::Normal;
            ctx->SetFontStyle(fsty);
            std::string shown = EllipsizeText(ctx, text, avail);
            Size2Di ts = ctx->GetTextLineDimensions(shown);
            ctx->SetTextPaint(style.secondaryTextColor);
            ctx->DrawText(shown, Point2Dd(x, bar.y + (h - ts.height) / 2.0));
        }
    }

    bool UltraCanvasFilerWidget::IsInInfoBar(const Point2Di& localPoint) const {
        int h = InfoBarHeight();
        if (h <= 0) return false;
        auto b = GetLocalBounds();
        return localPoint.y >= static_cast<int>(b.y + b.height) - h;
    }

    // ===== HIT TESTING =====
    Point2Di UltraCanvasFilerWidget::ToContentPoint(const Point2Di& localPoint) const {
        return Point2Di(localPoint.x + scrollOffsetX, localPoint.y + scrollOffsetY);
    }

    int UltraCanvasFilerWidget::ItemAt(const Point2Di& contentPoint) const {
        for (const ItemLayout& it : items) {
            if (it.rect.Contains(contentPoint))
                return static_cast<int>(it.entryIndex);
        }
        return -1;
    }

    int UltraCanvasFilerWidget::IconMenuActionAt(const Point2Di& localPoint,
                                                 size_t& outEntry) const {
        for (const IconMenuHit& h : iconMenuHits) {
            Rect2Di screenRect(h.rect.x - scrollOffsetX, h.rect.y - scrollOffsetY,
                               h.rect.width, h.rect.height);
            if (screenRect.Contains(localPoint)) {
                outEntry = h.entryIndex;
                return static_cast<int>(h.action);
            }
        }
        return -1;
    }

    int UltraCanvasFilerWidget::DetailsHeaderColumnAt(const Point2Di& localPoint) const {
        if (viewType != FilerViewType::Details) return -1;
        Rect2Di area = ContentBounds();
        if (localPoint.y < area.y || localPoint.y > area.y + detailsHeaderHeight)
            return -1;
        for (size_t i = 0; i < detailsColumns.size(); ++i) {
            const DetailsColumn& c = detailsColumns[i];
            if (c.sortable && localPoint.x >= c.x && localPoint.x < c.x + c.width)
                return static_cast<int>(i);
        }
        return -1;
    }

    // ===== INTERACTION =====
    void UltraCanvasFilerWidget::HandleItemClick(int index, bool ctrl, bool shift) {
        if (index < 0) {
            ClearSelection();
            return;
        }
        size_t idx = static_cast<size_t>(index);
        if (shift && lastClickedIndex >= 0) {
            size_t from = std::min<size_t>(lastClickedIndex, idx);
            size_t to = std::max<size_t>(lastClickedIndex, idx);
            if (!ctrl) selection.clear();
            for (size_t i = from; i <= to; ++i) {
                if (std::find(selection.begin(), selection.end(), i) == selection.end())
                    selection.push_back(i);
            }
        } else if (ctrl) {
            auto it = std::find(selection.begin(), selection.end(), idx);
            if (it != selection.end()) selection.erase(it);
            else selection.push_back(idx);
            lastClickedIndex = index;
        } else {
            selection.clear();
            selection.push_back(idx);
            lastClickedIndex = index;
        }
        FireSelectionChanged();
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::ActivateEntry(size_t index) {
        if (index >= entries.size()) return;
        const FilerEntry e = entries[index];   // copy: SetPath frees `entries`
        if (e.isDirectory || e.isArchive) {
            SetPath(e.path);
            return;
        }
        if (onFileActivated) onFileActivated(e);
    }

    void UltraCanvasFilerWidget::OpenContextMenu(const Point2Di& localPoint) {
        auto win = GetWindow();
        if (!win) return;

        std::vector<FilerEntry> targets = GetSelectedEntries();
        bool hasSel = !targets.empty();
        bool singleSel = targets.size() == 1;
        bool anyArchive = false;
        for (const FilerEntry& t : targets) if (t.isArchive) anyArchive = true;

        activePopupMenu = std::make_shared<UltraCanvasMenu>(
                GetIdentifier() + "_ctx", 0, 0, 230, 0);
        activePopupMenu->SetMenuType(MenuType::PopupMenu);
        auto& menu = *activePopupMenu;

        auto addAction = [&menu](const std::string& label, bool enabled,
                                 std::function<void()> cb,
                                 const std::string& shortcut = "") {
            MenuItemData item = shortcut.empty()
                    ? MenuItemData::Action(label, std::move(cb))
                    : MenuItemData::ActionWithShortcut(label, shortcut,
                                                       std::move(cb));
            item.enabled = enabled;
            menu.AddItem(item);
        };

        addAction("Copy", hasSel, [this]() { CopySelection(); }, "Ctrl+C");
        addAction("Cut", hasSel, [this]() { CutSelection(); }, "Ctrl+X");
        addAction("Paste", ClipboardHasContent(), [this]() { Paste(); }, "Ctrl+V");
        addAction("Delete", hasSel, [this]() { DeleteSelection(); }, "Del");
        addAction("Duplicate", hasSel, [this]() { DuplicateSelection(); }, "Ctrl+D");
        {
            size_t renameIdx = singleSel ? selection.front() : 0;
            addAction("Rename", singleSel,
                      [this, renameIdx]() { StartRename(renameIdx); }, "F2");
        }
        menu.AddItem(MenuItemData::Separator());

        // New >
        {
            std::vector<MenuItemData> newItems;
            for (const FilerNewDocumentType& t : newDocumentTypes) {
                FilerNewDocumentType copy = t;
                newItems.push_back(MenuItemData::Action(
                        t.label, [this, copy]() { CreateNewDocument(copy); }));
            }
            menu.AddItem(MenuItemData::Submenu("New", newItems));
        }
        menu.AddItem(MenuItemData::Separator());

        // Display > Sort / Type / Icon-Menu
        {
            std::vector<MenuItemData> sortItems;
            static const FilerSortField fields[] = {
                FilerSortField::Name, FilerSortField::Size, FilerSortField::Type,
                FilerSortField::ModifiedDate, FilerSortField::CreatedDate,
            };
            for (FilerSortField f : fields) {
                sortItems.push_back(MenuItemData::Radio(
                        SortFieldLabel(f), 1, sortField == f,
                        [this, f]() { SetSortField(f); }));
            }
            sortItems.push_back(MenuItemData::Separator());
            sortItems.push_back(MenuItemData::Radio(
                    "Ascending", 2, sortAscending,
                    [this]() { SetSortAscending(true); }));
            sortItems.push_back(MenuItemData::Radio(
                    "Descending", 2, !sortAscending,
                    [this]() { SetSortAscending(false); }));

            std::vector<MenuItemData> typeItems;
            static const FilerViewType views[] = {
                FilerViewType::Details, FilerViewType::List,
                FilerViewType::ThumbnailsSmall, FilerViewType::ThumbnailsMedium,
                FilerViewType::ThumbnailsBig, FilerViewType::ThumbnailsMaximized,
                FilerViewType::BarSize, FilerViewType::TreeMap,
                FilerViewType::GourceTree, FilerViewType::View3D,
            };
            for (FilerViewType v : views) {
                typeItems.push_back(MenuItemData::Radio(
                        ViewTypeLabel(v), 3, viewType == v,
                        [this, v]() { SetViewType(v); }));
            }

            std::vector<MenuItemData> displayItems;
            displayItems.push_back(MenuItemData::Submenu("Sort", sortItems));
            displayItems.push_back(MenuItemData::Submenu("Type", typeItems));
            displayItems.push_back(MenuItemData::Checkbox(
                    "Icon-Menu", hoverIconMenu,
                    [this](bool on) { SetHoverIconMenuEnabled(on); }));
            displayItems.push_back(MenuItemData::Checkbox(
                    "Info-Bar", showSelectionInfo,
                    [this](bool on) { SetSelectionInfoVisible(on); }));
            menu.AddItem(MenuItemData::Submenu("Display", displayItems));
        }
        menu.AddItem(MenuItemData::Separator());

        // Open with >
        {
            std::vector<MenuItemData> openItems;
            if (openWithApps.empty()) {
                MenuItemData none = MenuItemData::Action("(no applications)", []() {});
                none.enabled = false;
                openItems.push_back(none);
            }
            for (const FilerOpenWithApp& app : openWithApps) {
                auto onOpen = app.onOpen;
                auto cb = [this, onOpen]() {
                    if (onOpen) onOpen(GetSelectedEntries());
                };
                if (!app.iconPath.empty()) {
                    openItems.push_back(MenuItemData::Action(app.label, app.iconPath, cb));
                } else {
                    openItems.push_back(MenuItemData::Action(app.label, cb));
                }
            }
            menu.AddItem(MenuItemData::Submenu("Open with", openItems));
        }
        menu.AddItem(MenuItemData::Separator());

        addAction("Compress", !entries.empty(), [this]() { CompressSelection(); });
        addAction("Extract", anyArchive, [this]() { ExtractSelection(); });
        menu.AddItem(MenuItemData::Separator());

        addAction("Print", static_cast<bool>(onPrint), [this]() {
            if (onPrint) onPrint(SelectionOrAll());
        }, "Ctrl+P");
        menu.AddItem(MenuItemData::Separator());

        if (showOpenPathItem) {
            size_t openIdx = hasSel ? selection.front() : 0;
            addAction("Open Path", hasSel, [this, openIdx]() {
                if (openIdx >= entries.size()) return;
                const FilerEntry e = entries[openIdx];
                if (onOpenPath) onOpenPath(e);
                else SetPath(fs::path(e.path).parent_path().string());
            });
        }

        // Extras >
        {
            std::vector<MenuItemData> extraItems;
            MenuItemData share = MenuItemData::Action("Share", [this]() {
                if (onShare) onShare(SelectionOrAll());
            });
            share.enabled = static_cast<bool>(onShare);
            extraItems.push_back(share);

            MenuItemData attrs = MenuItemData::Action("Attributes", [this]() {
                if (onAttributes) onAttributes(SelectionOrAll());
            });
            attrs.enabled = static_cast<bool>(onAttributes);
            extraItems.push_back(attrs);

            extraItems.push_back(MenuItemData::Action("Copy path", [this]() {
                std::string text;
                std::vector<FilerEntry> sel = GetSelectedEntries();
                if (sel.empty()) text = currentPath;
                else for (const FilerEntry& e : sel) {
                    if (!text.empty()) text += '\n';
                    text += e.path;
                }
                SetClipboardText(text);
            }));

            MenuItemData access = MenuItemData::Action("Access", [this]() {
                if (onAccess) onAccess(SelectionOrAll());
            });
            access.enabled = static_cast<bool>(onAccess);
            extraItems.push_back(access);

            menu.AddItem(MenuItemData::Submenu("Extras", extraItems));
        }

        addAction("Settings", static_cast<bool>(onSettings), [this]() {
            if (onSettings) onSettings();
        });

        Point2Di winPos(static_cast<int>(GetXInWindow()) + localPoint.x,
                        static_cast<int>(GetYInWindow()) + localPoint.y);
        PopupElementSettings settings;
        settings.popupOwner = weak_from_this();
        activePopupMenu->OpenMenu(winPos, *win, settings);
    }

    bool UltraCanvasFilerWidget::HandleRenameKey(const UCEvent& event) {
        if (renamingIndex < 0) return false;
        if (event.type == UCEventType::KeyDown) {
            switch (event.virtualKey) {
                case UCKeys::Return:
                    CommitRename();
                    return true;
                case UCKeys::Escape:
                    CancelRename();
                    return true;
                case UCKeys::Backspace: {
                    if (!renameBuffer.empty()) {
                        size_t cut = renameBuffer.size() - 1;
                        while (cut > 0 &&
                               (static_cast<unsigned char>(renameBuffer[cut]) & 0xC0) == 0x80)
                            --cut;
                        renameBuffer.erase(cut);
                        RequestRedraw();
                    }
                    return true;
                }
                default:
                    return false;   // characters arrive via KeyChar / TextInput
            }
        }
        if (event.type == UCEventType::TextInput) {
            std::string in = event.text;
            if (in.empty() && event.character >= 32) in.assign(1, event.character);
            // Strip control characters and the path separator.
            std::string filtered;
            for (char c : in) {
                if (static_cast<unsigned char>(c) >= 32 && c != '/') filtered += c;
            }
            if (!filtered.empty()) {
                renameBuffer += filtered;
                RequestRedraw();
            }
            return true;
        }
        return false;
    }

    // ===== EVENTS =====
    bool UltraCanvasFilerWidget::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseLeave: {
                if (hoveredIndex != -1) { hoveredIndex = -1; RequestRedraw(); }
                if (hoveredIconAction != -1) {
                    hoveredIconAction = -1;
                    UltraCanvasTooltipManager::HideTooltip();
                }
                draggingScrollbar = false;
                dragOutArmed = false;
                dragCollapseIndex = -1;
                return true;
            }
            case UCEventType::MouseWheel: {
                if (IsHorizontal()) {
                    if (MaxScrollX() <= 0) return false;
                    scrollOffsetX -= event.wheelDelta * kWheelStep;
                } else {
                    if (MaxScrollY() <= 0) return false;
                    scrollOffsetY -= event.wheelDelta * kWheelStep;
                }
                ClampScroll();
                RequestRedraw();
                return true;
            }
            case UCEventType::MouseMove: {
                Point2Di local(event.pointer.x, event.pointer.y);
                // Armed drag-out: once the press moves past the slop the
                // selection leaves as a native OS drag.
                if (dragOutArmed) {
                    int dx = local.x - dragOutPressPoint.x;
                    int dy = local.y - dragOutPressPoint.y;
                    if (dx * dx + dy * dy >= kDragStartSlop * kDragStartSlop) {
                        dragOutArmed = false;
                        dragCollapseIndex = -1;   // it's a drag, not a click
                        if (StartNativeDragOfSelection()) return true;
                    }
                }
                if (draggingScrollbar) {
                    ScrollThumbTo((IsHorizontal() ? local.x : local.y)
                                  - scrollbarGrabOffset);
                    RequestRedraw();
                    return true;
                }
                int newHover = IsInInfoBar(local) ? -1 : ItemAt(ToContentPoint(local));
                if (newHover != hoveredIndex) {
                    hoveredIndex = newHover;
                    RequestRedraw();
                }

                // Tooltip for the hover icon-menu button under the cursor.
                {
                    size_t tipEntry = 0;
                    int tipAction = IconMenuActionAt(local, tipEntry);
                    if (tipAction != hoveredIconAction || tipEntry != hoveredIconEntry) {
                        hoveredIconAction = tipAction;
                        hoveredIconEntry  = tipEntry;
                        auto* win = GetWindow();
                        if (win && tipAction >= 0) {
                            static const char* kIconMenuTips[] = {
                                "Copy", "Cut", "Rename", "Delete"
                            };
                            UltraCanvasTooltipManager::UpdateAndShowTooltip(
                                    win, kIconMenuTips[tipAction],
                                    Point2Di(event.pointerWindow.x,
                                             event.pointerWindow.y));
                        } else {
                            UltraCanvasTooltipManager::HideTooltip();
                        }
                    }
                }
                return false;
            }
            case UCEventType::MouseDown: {
                Point2Di local(event.pointer.x, event.pointer.y);
                SetFocus(true);

                // The info bar covers items scrolled behind it.
                if (IsInInfoBar(local)) return true;

                if (event.button == UCMouseButton::Right) {
                    if (renamingIndex >= 0) CommitRename();
                    int index = ItemAt(ToContentPoint(local));
                    // Right-click keeps an existing multi-selection when it hits
                    // one of its items; otherwise it selects what's under it.
                    if (index >= 0 &&
                        std::find(selection.begin(), selection.end(),
                                  static_cast<size_t>(index)) == selection.end()) {
                        HandleItemClick(index, false, false);
                    } else if (index < 0) {
                        ClearSelection();
                    }
                    OpenContextMenu(local);
                    return true;
                }
                if (event.button != UCMouseButton::Left) return false;

                if (renamingIndex >= 0) CommitRename();

                // Scrollbar first: it sits above the content.
                {
                    ScrollbarGeom g = ScrollbarGeometry();
                    if (g.active) {
                        Rect2Di hit = g.track;
                        if (g.horizontal) { hit.y -= 6; hit.height += 8; }
                        else              { hit.x -= 6; hit.width  += 8; }
                        if (hit.Contains(local)) {
                            if (g.thumb.Contains(local)) {
                                scrollbarGrabOffset = g.horizontal
                                        ? (local.x - g.thumb.x) : (local.y - g.thumb.y);
                            } else {
                                scrollbarGrabOffset =
                                        (g.horizontal ? g.thumb.width : g.thumb.height) / 2;
                                ScrollThumbTo((g.horizontal ? local.x : local.y)
                                              - scrollbarGrabOffset);
                            }
                            draggingScrollbar = true;
                            RequestRedraw();
                            return true;
                        }
                    }
                }

                // Hover icon-menu buttons take precedence over the item.
                {
                    size_t entryIdx = 0;
                    int action = IconMenuActionAt(local, entryIdx);
                    if (action >= 0) {
                        if (std::find(selection.begin(), selection.end(), entryIdx)
                            == selection.end()) {
                            HandleItemClick(static_cast<int>(entryIdx), false, false);
                        }
                        switch (static_cast<IconMenuAction>(action)) {
                            case IconMenuAction::Copy:   CopySelection(); break;
                            case IconMenuAction::Cut:    CutSelection(); break;
                            case IconMenuAction::Rename: StartRename(entryIdx); break;
                            case IconMenuAction::Delete: DeleteSelection(); break;
                        }
                        return true;
                    }
                }

                // Details header: click toggles / switches the sort column.
                {
                    int col = DetailsHeaderColumnAt(local);
                    if (col >= 0) {
                        FilerSortField f = detailsColumns[col].field;
                        if (sortField == f) SetSortAscending(!sortAscending);
                        else SetSort(f, true);
                        return true;
                    }
                }

                {
                    int index = ItemAt(ToContentPoint(local));
                    bool alreadySelected = index >= 0 &&
                            std::find(selection.begin(), selection.end(),
                                      static_cast<size_t>(index)) != selection.end();
                    if (alreadySelected && !event.ctrl && !event.shift) {
                        // Keep the (multi-)selection so it can be dragged as a
                        // whole; collapsing to just this item happens on
                        // release when no drag started.
                        dragCollapseIndex = index;
                    } else {
                        HandleItemClick(index, event.ctrl, event.shift);
                        dragCollapseIndex = -1;
                    }
                    // Pressing on an item arms the drag-out gesture; the drag
                    // starts once the pointer moves past the slop threshold.
                    if (index >= 0 && !selection.empty()) {
                        dragOutArmed = true;
                        dragOutPressPoint = local;
                    }
                }
                return true;
            }
            case UCEventType::MouseUp: {
                if (dragOutArmed && dragCollapseIndex >= 0) {
                    // The press on a selected item turned out to be a plain
                    // click: apply the deferred "select only this item".
                    HandleItemClick(dragCollapseIndex, false, false);
                }
                dragOutArmed = false;
                dragCollapseIndex = -1;
                if (draggingScrollbar) {
                    draggingScrollbar = false;
                    RequestRedraw();
                    return true;
                }
                return false;
            }
            case UCEventType::MouseDoubleClick: {
                Point2Di local(event.pointer.x, event.pointer.y);
                if (IsInInfoBar(local)) return true;
                int index = ItemAt(ToContentPoint(local));
                if (index >= 0) {
                    ActivateEntry(static_cast<size_t>(index));
                    return true;
                }
                return false;
            }
            case UCEventType::KeyDown: {
                if (HandleRenameKey(event)) return true;
                if (renamingIndex >= 0) return true;   // swallow while editing

                if (event.ctrl) {
                    switch (event.virtualKey) {
                        case 'a': case 'A': SelectAll(); return true;
                        case 'c': case 'C': CopySelection(); return true;
                        case 'x': case 'X': CutSelection(); return true;
                        case 'v': case 'V': Paste(); return true;
                        case 'd': case 'D': DuplicateSelection(); return true;
                        case 'p': case 'P':
                            if (onPrint) onPrint(SelectionOrAll());
                            return true;
                        default: break;
                    }
                    return false;
                }
                switch (event.virtualKey) {
                    case UCKeys::Return:
                        if (!selection.empty()) ActivateEntry(selection.front());
                        return true;
                    case UCKeys::Delete:
                        DeleteSelection();
                        return true;
                    case UCKeys::F2:
                        if (selection.size() == 1) StartRename(selection.front());
                        return true;
                    case UCKeys::Up:
                    case UCKeys::Down:
                    case UCKeys::Left:
                    case UCKeys::Right: {
                        if (entries.empty()) return true;
                        // Per-view arrow steps: rows step by 1; grids by column
                        // count; the list flows down first, so left/right jump
                        // a whole column.
                        int step = 1;
                        bool vertical = (event.virtualKey == UCKeys::Up ||
                                         event.virtualKey == UCKeys::Down);
                        bool negative = (event.virtualKey == UCKeys::Up ||
                                         event.virtualKey == UCKeys::Left);
                        Rect2Di area = ContentBounds();
                        if (viewType == FilerViewType::List) {
                            int rowsPerColumn =
                                    std::max(1, area.height / style.listRowHeight);
                            step = vertical ? 1 : rowsPerColumn;
                        } else if (viewType == FilerViewType::Details ||
                                   viewType == FilerViewType::BarSize) {
                            if (!vertical) return true;
                            step = 1;
                        } else {
                            int edge = ThumbnailEdge();
                            int cols = std::max(1, (area.width - 10 + style.tileGap)
                                                   / (edge + style.tileGap));
                            step = vertical ? cols : 1;
                        }
                        int current = selection.empty()
                                ? (negative ? (int)entries.size() : -1)
                                : (int)selection.front();
                        int next = clampi(current + (negative ? -step : step),
                                          0, (int)entries.size() - 1);
                        selection.clear();
                        selection.push_back(static_cast<size_t>(next));
                        lastClickedIndex = next;
                        EnsureVisible(static_cast<size_t>(next));
                        FireSelectionChanged();
                        RequestRedraw();
                        return true;
                    }
                    default:
                        break;
                }
                return false;
            }
            case UCEventType::TextInput:
                return HandleRenameKey(event);
            case UCEventType::Drop: {
                // Files dragged in from other applications / windows are
                // copied into the shown folder.
                if (event.droppedFiles.empty()) return false;
                AcceptDroppedFiles(event.droppedFiles);
                return true;
            }
            default:
                break;
        }
        return UltraCanvasContainer::OnEvent(event);
    }

} // namespace UltraCanvas
