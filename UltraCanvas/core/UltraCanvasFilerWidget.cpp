// core/UltraCanvasFilerWidget.cpp
// Filer folder widget: displays one folder's content with selectable view types
// (details, list, thumbnails, size bars, treemap), sorting, an inline rename
// editor, a hover icon menu, the full file context menu and a selection info
// bar (type / size / dates / attributes, image dimensions, media duration and
// codec via lightweight header probes, recursive folder stats). Image
// thumbnails decode asynchronously (see ASYNC THUMBNAILS) so the folder page
// never waits for image files.
// Entries are draggable (see DRAGGING ENTRIES): inside the widget the drag is
// drawn here and a drop on a folder of the view moves the files into it (Ctrl
// copies), and once the cursor leaves the widget the same set continues as a
// native OS drag onto other windows / applications. Dragging never changes the
// selection. External drops are copied into the shown folder, and Copy / Cut /
// Paste go through the system clipboard so files can be exchanged with other
// programs (external file managers, editors, ...). Pasting a clipboard that
// holds raw data instead of files (an image or text copied elsewhere) writes
// that content as a new file into the shown folder.
// The column views (details, list, size bars) carry UltraCanvasSplitPane-style
// splitters between their columns (see COLUMN SPLITTERS), and names too long
// for the space they are drawn in show the full name in a hover tooltip.
// Tile captions (thumbnail grids, treemap) wrap long names over several lines
// instead of cutting them off after one (see WRAPPED CAPTIONS).
// Besides clicking, entries are selected with a rubber band: dragging from
// empty space draws a selection rectangle and everything it touches becomes
// the selection (Ctrl adds the rectangle to the selection held before).
// The inline rename editor is a real UltraCanvasTextInput overlaid on the
// item's name, and video files show their poster frame in the thumbnail
// views (decoded on the same worker threads as the image thumbnails).
// A file list shown with ShowFileList can keep the order it was handed over in
// (SetFileListOrderPreserved) instead of being sorted. Every content change
// the user makes is reported through onFolderModified with the folder it
// landed in, next to the plain rescan notification onFolderRefreshed.
// Version: 1.10.0
// Last Modified: 2026-08-08
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
#include "UltraCanvasSupportedFormats.h"
#include "UltraCanvasUtils.h"
#include "../libspecific/Cairo/QoiPixmapCodec.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasTooltipManager.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasVideoThumbnail.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sys/stat.h>

// X11 (pulled in via UltraCanvasApplication.h) #defines Success and None,
// which collide with the VirtualFS::VirtualFSResult enumerators used below.
#ifdef Success
#undef Success
#endif
#ifdef None
#undef None
#endif

namespace fs = std::filesystem;

namespace UltraCanvas {

    std::vector<std::string> UltraCanvasFilerWidget::clipboardPaths;
    bool UltraCanvasFilerWidget::clipboardCut = false;

    namespace {
        constexpr int kWheelStep = 64;                 // px per wheel notch
        constexpr uint64_t kDirSizeEntryCap = 50000;   // recursive-size safety cap
        constexpr int kDragStartSlop = 5;              // px before a press becomes a drag-out
        // Delay before a click on the selected entry's name opens the rename
        // editor (Windows style). Must exceed the platform double-click
        // interval so the first click of a double-click never renames.
        constexpr unsigned int kRenameClickDelayMs = 500;

        // ===== RESIZABLE COLUMNS =====
        // Narrowest a column can be dragged; the Name column keeps more so it
        // never collapses to the icon.
        constexpr int kMinColumnWidth = 44;
        constexpr int kMinNameColumnWidth = 120;
        constexpr int kMinListColumnWidth = 80;
        constexpr int kMinBarWidth = 60;             // BarSize: shortest bar area
        constexpr int kScrollbarGutter = 10;         // reserved on the right edge
        constexpr int kListColumnGap = 12;           // List: gap between columns

        int clampi(int v, int lo, int hi) {
            return v < lo ? lo : (v > hi ? hi : v);
        }

        // Cached per-extension answer to "can the UCImage pipeline decode
        // this?". The Image/Vector file categories are broader than what the
        // image pipeline loads (e.g. xar/cdr render through graphics plugins),
        // and feeding such files to the loader just produces libvips warnings
        // and a failed decode.
        bool ImagePipelineLoadsExtension(const std::string& ext) {
            static std::map<std::string, bool> cache;
            static std::mutex cacheMutex;
            std::lock_guard<std::mutex> lk(cacheMutex);
            auto it = cache.find(ext);
            if (it == cache.end()) {
                it = cache.emplace(
                        ext, UltraCanvasSupportedFormats::CanImagePipelineLoad(ext)).first;
            }
            return it->second;
        }

        std::string LowerExtension(const std::string& name);   // defined below

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

        // Uniform size string for the "Bar size" view: always a mantissa with
        // exactly one decimal place plus a unit chosen so the value stays below
        // 1024 (e.g. "100.9 KB", "23.2 MB", "1.5 TB").  Keeping every value in
        // the same "NNN.N UU" shape lets the number column line up and every bar
        // share the same width.
        std::string FormatSizeFixed(uint64_t bytes) {
            static const char* kUnits[] = { "B", "KB", "MB", "GB", "TB", "PB" };
            double v = static_cast<double>(bytes);
            int u = 0;
            while (v >= 1024.0 && u < 5) { v /= 1024.0; ++u; }
            char buf[32];
            snprintf(buf, sizeof(buf), "%.1f %s", v, kUnits[u]);
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

        // True when `path` names a video file (by extension). Such thumbnail
        // requests decode a poster frame via CaptureVideoThumbnailPixmap
        // instead of going through the image pipeline.
        bool IsVideoFilePath(const std::string& path) {
            const auto& m = ExtensionTypeMap();
            auto it = m.find(LowerExtension(path));
            return it != m.end() && it->second.category == FilerFileCategory::Video;
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
            // Append the largest applicable unit: seconds ("34 s"),
            // minutes ("3:45 min") or hours ("2:32:20 h").
            if (h > 0)         snprintf(buf, sizeof(buf), "%ld:%02ld:%02ld h", h, m, s);
            else if (m > 0)    snprintf(buf, sizeof(buf), "%ld:%02ld min", m, s);
            else               snprintf(buf, sizeof(buf), "%ld s", s);
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
            std::ifstream f(PathFromUtf8(path), std::ios::binary);
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
            std::ifstream f(PathFromUtf8(path), std::ios::binary);
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
        EnsureDetailsColumnWidths();

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
        CancelPendingRename();      // the timer callback captures `this`
        // Detach the rename editor now: its callbacks capture `this`, and the
        // container teardown dropping its focus must not commit into a
        // half-destroyed widget.
        DestroyRenameInput(false);
        if (dragMouseCaptured) {    // never leave the pointer grabbed
            if (auto* app = UltraCanvasApplication::GetInstance()) app->ReleaseMouse();
            dragMouseCaptured = false;
        }
        thumbAlive->store(false);   // neutralize queued cross-thread redraws
        StopThumbnailWorkers();
        StopFolderStatsWorker();
        StopFolderPrefetchWorker();
    }

    // ===== FOLDER =====
    void UltraCanvasFilerWidget::SetPath(const std::string& folderPath) {
        currentPath = folderPath;
        fileListMode = false;
        fileListPaths.clear();
        scrollOffsetX = scrollOffsetY = 0;
        CancelRename();
        CancelPendingRename();
        ClearSelection();
        // A navigation may serve the listing straight from the prefetch cache.
        ScanFolder(true);
        if (onPathChanged) onPathChanged(currentPath);
    }

    void UltraCanvasFilerWidget::ShowFileList(const std::vector<std::string>& paths) {
        fileListMode = true;
        fileListPaths = paths;
        scrollOffsetX = scrollOffsetY = 0;
        CancelRename();
        CancelPendingRename();
        ClearSelection();
        ScanFolder();
    }

    void UltraCanvasFilerWidget::SetFileListOrderPreserved(bool preserved) {
        if (preserveFileListOrder == preserved) return;
        preserveFileListOrder = preserved;
        // Turning it on has to restore the order the paths came in, which only
        // a rescan of fileListPaths can do (the entries have been sorted).
        if (fileListMode) Refresh();
    }

    void UltraCanvasFilerWidget::Refresh() {
        CancelRename();
        CancelPendingRename();
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

    bool UltraCanvasFilerWidget::StatEntryForPath(const std::string& path,
                                                  FilerEntry& e) const {
        std::error_code ec;
        fs::file_status st = fs::symlink_status(path, ec);
        if (ec || !fs::exists(st)) return false;
        e.name = fs::path(path).filename().string();
        e.path = path;
        e.isSymlink = fs::is_symlink(st);
        e.isDirectory = fs::is_directory(path, ec) && !ec;
        e.isHidden = !e.name.empty() && e.name[0] == '.';
        if (!e.isDirectory) {
            std::error_code sec;
            e.size = fs::file_size(path, sec);
            if (sec) e.size = 0;
        }
        e.extension = e.isDirectory ? "" : LowerExtension(e.name);

        struct stat sb{};
        if (::stat(path.c_str(), &sb) == 0) {
            e.modifiedTime = sb.st_mtime;
            e.createdTime = sb.st_ctime;
            e.isReadOnly = (sb.st_mode & S_IWUSR) == 0;
        }
        ApplyEntryTypeInfo(e);
        return true;
    }

    void UltraCanvasFilerWidget::ScanRealDirectory(const std::string& path,
                                                   bool includeHidden,
                                                   std::vector<FilerEntry>& out) const {
        std::error_code ec;
        for (fs::directory_iterator it(path, ec), end; it != end;
             it.increment(ec)) {
            if (ec) break;
            FilerEntry e;
            e.name = it->path().filename().string();
            e.path = it->path().string();
            e.isSymlink = it->is_symlink(ec);   // d_type, no extra syscall
            e.isHidden = !e.name.empty() && e.name[0] == '.';
            if (e.isHidden && !includeHidden) continue;

            // One stat per entry: type, size, times and the write bit all
            // come from the same call. Asking the directory_entry for
            // file_size() and then ::stat()ing again for the times cost a
            // second metadata lookup per file, which on a big or network
            // folder doubled the scan time.
            struct stat st{};
            if (::stat(e.path.c_str(), &st) == 0) {
                e.isDirectory = (st.st_mode & S_IFMT) == S_IFDIR;
                if (!e.isDirectory)
                    e.size = static_cast<uint64_t>(st.st_size);
                e.modifiedTime = st.st_mtime;
                e.createdTime = st.st_ctime;
                e.isReadOnly = (st.st_mode & S_IWUSR) == 0;
            } else {
                // Broken symlink or a name stat() cannot resolve (e.g. a
                // non-ACP name on Windows): keep the iterator's cached
                // view so the entry still lists with its type and size.
                e.isDirectory = it->is_directory(ec);
                if (!e.isDirectory) {
                    std::error_code sec;
                    e.size = it->file_size(sec);
                    if (sec) e.size = 0;
                }
            }
            e.extension = e.isDirectory ? "" : LowerExtension(e.name);
            ApplyEntryTypeInfo(e);
            out.push_back(std::move(e));
        }
    }

    void UltraCanvasFilerWidget::ScanFolder(bool usePrefetched) {
        // `selection` indexes `entries`, which is rebuilt below — remember what
        // is selected by path so a rescan (Refresh after a file operation, a
        // drop, ...) keeps the same files selected instead of whatever ends up
        // at the old indices.
        std::unordered_set<std::string> selectedPaths;
        for (size_t idx : selection)
            if (idx < entries.size()) selectedPaths.insert(entries[idx].path);

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
            aspectQueue.clear();
            aspectCache.clear();
            mediaQueue.clear();
            mediaInfoCache.clear();
        }
        DropThumbnailCache();

        std::error_code ec;
        bool isRealDir = !currentPath.empty() && fs::is_directory(currentPath, ec);

        if (fileListMode) {
            for (const std::string& p : fileListPaths) {
                FilerEntry e;
                if (!StatEntryForPath(p, e)) continue;
                if (e.isHidden && !showHiddenFiles) continue;
                entries.push_back(std::move(e));
            }
        } else if (isRealDir) {
            // A prefetched listing (navigation only) skips the directory scan
            // entirely; it holds hidden entries too, so it serves either
            // hidden-files setting. A miss or a stale hit scans normally.
            std::vector<FilerEntry> listing;
            bool fromCache = usePrefetched && folderPrefetchEnabled &&
                             TakePrefetchedListing(currentPath, listing);
            if (!fromCache)
                ScanRealDirectory(currentPath, showHiddenFiles, listing);
            if (fromCache && !showHiddenFiles) {
                for (FilerEntry& e : listing)
                    if (!e.isHidden) entries.push_back(std::move(e));
            } else {
                entries = std::move(listing);
            }
        }
#ifdef ULTRACANVAS_HAS_VIRTUALFS
        else if (!currentPath.empty()) {
            // Not a real directory: let VirtualFS list it (an archive interior —
            // "/path/archive.zip" or a path inside one). VirtualFS registers
            // its providers in Initialize(); without it every archive lists
            // as empty, so make sure it ran (idempotent, cheap after the
            // first call).
            if (!UltraCanvasVirtualFSBridge::Initialize()) {
                ReportError("VirtualFS unavailable: "
                            + UltraCanvasVirtualFSBridge::GetLastError());
            }
            for (const VirtualFS::VirtualFSEntry& v
                 : VirtualFS::VirtualFS_ListDirectory(currentPath)) {
                FilerEntry e;
                e.name = v.name;
                // v.path is the archive-internal path ("media/photo.jpg");
                // the widget needs the full virtual path so navigation and
                // file operations can resolve the entry again.
                e.path = (!currentPath.empty() && currentPath.back() == '/')
                             ? currentPath + v.name
                             : currentPath + "/" + v.name;
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
            if (entries.empty()) {
                // An unreadable archive and a genuinely empty one both produce
                // an empty listing. When the path denotes the archive itself,
                // ask the provider layer which case it is, so a failed open
                // (no provider for the format, corrupt or password-protected
                // file) is reported instead of masquerading as "(empty folder)".
                auto resolved = VirtualFS::VirtualFSPath::Resolve(currentPath);
                if (!resolved.archiveStack.empty() && !resolved.isInsideArchive &&
                    VirtualFS::VirtualFS_GetArchiveInfo(currentPath).path.empty()) {
                    ReportError("Cannot read archive: " + currentPath);
                }
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

        // A delete that wiped out the whole selection left the entry that
        // inherits it here (SetSelectNextAfterDelete): it replaces the restore
        // below, so the new selection is in place before onFolderRefreshed
        // fires and a host feeding a preview from it never sees an empty one.
        if (!selectAfterScanPath.empty()) {
            const std::string wanted = selectAfterScanPath;
            selectAfterScanPath.clear();
            selection.clear();
            for (size_t i = 0; i < entries.size(); ++i) {
                if (entries[i].path != wanted) continue;
                selection.push_back(i);
                lastClickedIndex = static_cast<int>(i);
                // Deferred reveal: the layout is rebuilt below, and the host
                // may narrow the widget in the same frame (its preview pane
                // stays open), so the scroll uses the new geometry.
                pendingRevealEntry = static_cast<int>(i);
                break;
            }
            FireSelectionChanged();
        } else {
            // Restore the selection on the entries that are still there. Files
            // that vanished (moved away, deleted elsewhere) drop out of it,
            // which is a real selection change and is reported.
            std::vector<size_t> restored;
            for (size_t i = 0; i < entries.size(); ++i) {
                if (selectedPaths.count(entries[i].path)) {
                    restored.push_back(i);
                }
            }
            bool changed = restored.size() != selectedPaths.size();
            selection.swap(restored);
            if (changed) FireSelectionChanged();
        }

        if (!renamedTo.empty()) {
            for (size_t i = 0; i < entries.size(); ++i) {
                if (entries[i].path != renamedTo) continue;
                pendingRevealEntry = static_cast<int>(i);
                // Shift-range anchor, but only when it really is selected —
                // an icon-menu rename leaves the selection where it was.
                if (std::find(selection.begin(), selection.end(), i)
                    != selection.end()) {
                    lastClickedIndex = static_cast<int>(i);
                }
                break;
            }
        }

        InvalidateFilerLayout();
        RequestRedraw();

        // The folder is on screen — line up its subfolders for the prefetch
        // worker so entering one of them can skip the cold scan.
        if (!fileListMode && isRealDir) QueueFolderPrefetch();

        // The listing itself changed (entries added / removed / renamed) —
        // hosts use this to refresh what they show about the folder.
        if (onFolderRefreshed) onFolderRefreshed();
    }

    void UltraCanvasFilerWidget::SortEntries() {
        // A file list whose order is the information it carries (see
        // SetFileListOrderPreserved) is left exactly as it was handed over.
        if (fileListMode && preserveFileListOrder) return;
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
        CancelPendingRename();
        // The old view's splitters are gone; their hit strips are rebuilt by
        // the next frame.
        EndColumnSplitterDrag();
        hoveredSplitter = -1;
        columnSplitters.clear();
        HideHoverTooltip();
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

    void UltraCanvasFilerWidget::SetDatasetField(FilerDatasetField field, bool on) {
        uint32_t bit = static_cast<uint32_t>(field);
        uint32_t next = on ? (datasetFields | bit) : (datasetFields & ~bit);
        SetDatasetFields(next);
    }

    bool UltraCanvasFilerWidget::IsDatasetFieldEnabled(FilerDatasetField field) const {
        return (datasetFields & static_cast<uint32_t>(field)) != 0;
    }

    void UltraCanvasFilerWidget::SetDatasetFields(uint32_t mask) {
        if (datasetFields == mask) return;
        datasetFields = mask;
        // Each enabled field adds a caption line, so the tile height changes.
        InvalidateFilerLayout();
        RequestRedraw();
    }

    int UltraCanvasFilerWidget::DatasetLineCount() const {
        int n = 0;
        for (uint32_t m = datasetFields; m; m &= (m - 1)) ++n;
        return n;
    }

    int UltraCanvasFilerWidget::DatasetLineHeight() const {
        return static_cast<int>(style.smallFontSize) + 3;
    }

    std::vector<std::string> UltraCanvasFilerWidget::DatasetLinesFor(
            const FilerEntry& e) {
        std::vector<std::string> lines;
        if (datasetFields == 0) return lines;

        auto has = [this](FilerDatasetField f) {
            return (datasetFields & static_cast<uint32_t>(f)) != 0;
        };
        if (has(FilerDatasetField::Size) && !e.isDirectory) {
            lines.push_back(FormatSize(e.size));
        }
        if (has(FilerDatasetField::ModifiedDate) && e.modifiedTime != 0) {
            lines.push_back(FormatTime(e.modifiedTime));
        }
        if (has(FilerDatasetField::CreatedDate) && e.createdTime != 0) {
            lines.push_back(FormatTime(e.createdTime));
        }
        if (has(FilerDatasetField::Attributes) && !e.attributes.empty()) {
            lines.push_back(e.attributes);
        }
        // Length (audio / video) and Dimensions (bitmaps) both come from the
        // lazily-probed, cached media info — gated by category so each only
        // shows where it applies.
        if (has(FilerDatasetField::Length) &&
            (e.category == FilerFileCategory::Audio ||
             e.category == FilerFileCategory::Video)) {
            std::string info = EntryExtraInfo(e);
            if (!info.empty()) lines.push_back(info);
        }
        if (has(FilerDatasetField::Dimensions) &&
            e.category == FilerFileCategory::Image) {
            std::string info = EntryExtraInfo(e);
            if (!info.empty()) lines.push_back(info);
        }
        return lines;
    }

    void UltraCanvasFilerWidget::SetStyle(const FilerStyle& s) {
        style = s;
        SetBackgroundColor(style.backgroundColor);
        InvalidateFilerLayout();
        RequestRedraw();
    }

    // ===== RESIZABLE COLUMNS =====
    void UltraCanvasFilerWidget::SetColumnResizeEnabled(bool enabled) {
        if (columnResizeEnabled == enabled) return;
        columnResizeEnabled = enabled;
        if (!enabled) {
            EndColumnSplitterDrag();
            hoveredSplitter = -1;
            columnSplitters.clear();
            SetMouseCursor(UCMouseCursor::Default);
        }
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::SetDetailsColumnWidth(FilerDetailsColumn column,
                                                       int pixels) {
        EnsureDetailsColumnWidths();
        size_t index = static_cast<size_t>(column);
        if (index >= kFilerDetailsColumnCount) return;
        int minWidth = (column == FilerDetailsColumn::Name) ? kMinNameColumnWidth
                                                            : kMinColumnWidth;
        int width = std::max(minWidth, pixels);
        if (detailsColumnWidths[index] == width) return;
        // Name is derived from what the others leave, so widening it means
        // taking that width from the visible column next to it.
        if (column == FilerDetailsColumn::Name) {
            int delta = width - detailsColumnWidths[0];
            const std::vector<size_t> vis = VisibleDetailsSpecIndices();
            size_t next = vis.size() > 1 ? vis[1]
                                         : static_cast<size_t>(FilerDetailsColumn::Size);
            detailsColumnWidths[next] = std::max(kMinColumnWidth,
                                                 detailsColumnWidths[next] - delta);
        }
        detailsColumnWidths[index] = width;
        InvalidateFilerLayout();
        NotifyColumnWidthsChanged();
        RequestRedraw();
    }

    int UltraCanvasFilerWidget::GetDetailsColumnWidth(FilerDetailsColumn column) const {
        size_t index = static_cast<size_t>(column);
        return (index < detailsColumnWidths.size()) ? detailsColumnWidths[index] : 0;
    }

    void UltraCanvasFilerWidget::ResetDetailsColumnWidths() {
        detailsColumnWidths.clear();
        EnsureDetailsColumnWidths();
        InvalidateFilerLayout();
        NotifyColumnWidthsChanged();
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::SetListColumnWidth(int pixels) {
        int width = std::max(kMinListColumnWidth, pixels);
        if (style.listColumnWidth == width) return;
        style.listColumnWidth = width;
        InvalidateFilerLayout();
        NotifyColumnWidthsChanged();
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::SetBarSizeNameColumnWidth(int pixels) {
        int width = std::max(kMinColumnWidth, pixels);
        if (barSizeNameWidth == width) return;
        barSizeNameWidth = width;
        InvalidateFilerLayout();
        NotifyColumnWidthsChanged();
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::SetBarSizeValueColumnWidth(int pixels) {
        int width = (pixels <= 0) ? 0 : std::max(kMinColumnWidth, pixels);
        if (barSizeValueWidth == width) return;
        barSizeValueWidth = width;
        InvalidateFilerLayout();
        NotifyColumnWidthsChanged();
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::SetNameTooltipsEnabled(bool enabled) {
        if (nameTooltips == enabled) return;
        nameTooltips = enabled;
        if (!enabled && tooltipTarget == TooltipTarget::ItemName) HideHoverTooltip();
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

    bool UltraCanvasFilerWidget::SelectPath(const std::string& path) {
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].path != path) continue;
            selection.clear();
            selection.push_back(i);
            lastClickedIndex = static_cast<int>(i);
            EnsureSelectionVisible();
            FireSelectionChanged();
            RequestRedraw();
            return true;
        }
        return false;
    }

    void UltraCanvasFilerWidget::FireSelectionChanged() {
        if (onSelectionChanged) onSelectionChanged(GetSelectedEntries());
    }

    void UltraCanvasFilerWidget::NotifyFolderModified(const std::string& folderPath) {
        if (!onFolderModified) return;
        // A file list spans many folders, so its "current folder" is not where
        // a change landed — only an explicitly named folder is reported there.
        if (folderPath.empty() && (fileListMode || currentPath.empty())) return;
        onFolderModified(folderPath.empty() ? currentPath : folderPath);
    }

    std::vector<size_t> UltraCanvasFilerWidget::SelectionOrItem(int index) const {
        if (!selection.empty()) return selection;
        if (index >= 0 && index < (int)entries.size())
            return {static_cast<size_t>(index)};
        return {};
    }

    std::vector<FilerEntry> UltraCanvasFilerWidget::SelectionOrEntry(
            size_t entryIndex) const {
        if (entryIndex >= entries.size()) return {};
        if (std::find(selection.begin(), selection.end(), entryIndex)
            != selection.end()) {
            return GetSelectedEntries();
        }
        return {entries[entryIndex]};
    }

    std::vector<FilerEntry> UltraCanvasFilerWidget::SelectionOrAll() const {
        std::vector<FilerEntry> out = GetSelectedEntries();
        if (out.empty()) out = entries;
        return out;
    }

    // ===== CLIPBOARD / FILE OPERATIONS =====
    bool UltraCanvasFilerWidget::ClipboardHasContent() {
        if (!clipboardPaths.empty()) return true;
        // Files copied in another application (text/uri-list), or raw
        // clipboard data — an image or text — that Paste writes as a new
        // file. One format scan instead of per-format queries: on X11 each
        // IsFormatAvailable() is a TARGETS round trip to the selection owner.
        if (UltraCanvasClipboard* cb = GetClipboard()) {
            for (const std::string& f : cb->GetAvailableFormats()) {
                if (f.rfind("image/", 0) == 0 || f.rfind("text/", 0) == 0 ||
                    f == "UTF8_STRING" || f == "STRING" || f == "TEXT") {
                    return true;
                }
            }
        }
        return false;
    }

    bool UltraCanvasFilerWidget::IsCutEntry(const FilerEntry& e) const {
        if (!clipboardCut) return false;
        return std::find(clipboardPaths.begin(), clipboardPaths.end(), e.path)
               != clipboardPaths.end();
    }

    void UltraCanvasFilerWidget::SelectionToClipboard(bool cut) {
        EntriesToClipboard(GetSelectedEntries(), cut);
    }

    void UltraCanvasFilerWidget::EntriesToClipboard(
            const std::vector<FilerEntry>& targets, bool cut) {
        clipboardPaths.clear();
        for (const FilerEntry& e : targets) clipboardPaths.push_back(e.path);
        clipboardCut = cut && !clipboardPaths.empty();
        // Mirror to the system clipboard (text/uri-list + cut/copy marker) so
        // the files can be pasted in other programs.
        if (!clipboardPaths.empty()) {
            if (UltraCanvasClipboard* cb = GetClipboard()) {
                cb->SetFiles(clipboardPaths, clipboardCut);
            }
        }
        RequestRedraw();   // reflect (or clear) the cut ghosting immediately
    }

    void UltraCanvasFilerWidget::CopySelection() { SelectionToClipboard(false); }
    void UltraCanvasFilerWidget::CutSelection()  { SelectionToClipboard(true); }

    // ===== DRAGGING ENTRIES =====
    void UltraCanvasFilerWidget::SetDragEnabled(bool enabled) {
        if (dragEnabled == enabled) return;
        dragEnabled = enabled;
        if (!dragEnabled && (draggingItems || dragOutArmed)) CancelItemDrag();
    }

    void UltraCanvasFilerWidget::BeginItemDrag(const Point2Di& localPoint) {
        dragOutArmed = false;
        // A drag is not a click: whatever the press deferred (collapsing a
        // multi-selection, selecting the pressed item, the rename click) is
        // dropped, so the selection — and any preview fed by it — is untouched.
        dragCollapseIndex = -1;
        pendingSelectIndex = -1;
        CancelPendingRename();
        HideHoverTooltip();

        // The pressed item alone, unless the press landed inside the selection
        // — then the whole selection travels.
        std::vector<size_t> indices;
        if (dragPressIndex >= 0 &&
            std::find(selection.begin(), selection.end(),
                      static_cast<size_t>(dragPressIndex)) != selection.end()) {
            indices = selection;
        } else if (dragPressIndex >= 0 &&
                   dragPressIndex < static_cast<int>(entries.size())) {
            indices.push_back(static_cast<size_t>(dragPressIndex));
        }

        dragPaths.clear();
        for (size_t idx : indices)
            if (idx < entries.size()) dragPaths.push_back(entries[idx].path);
        if (dragPaths.empty()) return;

        // Badge contents: the single dragged entry, or the item count.
        const FilerEntry& lead = entries[indices.front()];
        dragLabel = dragPaths.size() == 1
                ? lead.name
                : (std::to_string(dragPaths.size()) + " items");
        dragLeadEntry = lead;

        draggingItems = true;
        dragPos = localPoint;
        dragDropFolderIndex = DragDropFolderAt(localPoint);
        if (hoveredIndex != -1) hoveredIndex = -1;
        SetMouseCursor(UCMouseCursor::Hand);
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::UpdateItemDrag(const Point2Di& localPoint) {
        dragPos = localPoint;

        // Left the widget: the same set continues as a native OS drag, so it
        // can be dropped on any other window or application.
        auto lb = GetLocalBounds();
        Rect2Di local(static_cast<int>(lb.x), static_cast<int>(lb.y),
                      static_cast<int>(lb.width), static_cast<int>(lb.height));
        if (!local.Contains(localPoint)) {
            std::vector<std::string> paths = dragPaths;
            EndDragGesture();
            if (!StartNativeDragOfPaths(paths)) {
                // No native drag available (no window / refused grab): keep the
                // in-widget drag running so the gesture is not lost.
                draggingItems = true;
                dragPaths = paths;
                dragPos = localPoint;
                if (auto* app = UltraCanvasApplication::GetInstance()) {
                    app->CaptureMouse(this);
                    dragMouseCaptured = true;
                }
            }
            RequestRedraw();
            return;
        }

        int folder = DragDropFolderAt(localPoint);
        if (folder != dragDropFolderIndex) dragDropFolderIndex = folder;
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::FinishItemDrag(const Point2Di& localPoint,
                                                bool copy) {
        std::vector<std::string> paths = dragPaths;
        int folder = DragDropFolderAt(localPoint);
        std::string destDir = (folder >= 0 && folder < static_cast<int>(entries.size()))
                ? entries[folder].path : std::string();
        EndDragGesture();
        RequestRedraw();
        // A drop that is not on a folder of this view just ends the drag.
        if (!destDir.empty() && !paths.empty()) DropPathsInto(paths, destDir, copy);
    }

    void UltraCanvasFilerWidget::CancelItemDrag() {
        if (!draggingItems && !dragOutArmed) return;
        EndDragGesture();
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::EndDragGesture() {
        if (dragMouseCaptured) {
            if (auto* app = UltraCanvasApplication::GetInstance()) app->ReleaseMouse();
            dragMouseCaptured = false;
        }
        if (draggingItems) SetMouseCursor(UCMouseCursor::Default);
        draggingItems = false;
        dragOutArmed = false;
        dragPressIndex = -1;
        dragDropFolderIndex = -1;
        dragPaths.clear();
        dragLabel.clear();
    }

    int UltraCanvasFilerWidget::DragDropFolderAt(const Point2Di& localPoint) const {
        if (IsInInfoBar(localPoint)) return -1;
        int idx = ItemAt(ToContentPoint(localPoint));
        if (idx < 0 || idx >= static_cast<int>(entries.size())) return -1;
        if (!entries[idx].isDirectory) return -1;
        // A folder cannot be dropped on itself.
        if (std::find(dragPaths.begin(), dragPaths.end(), entries[idx].path)
            != dragPaths.end()) {
            return -1;
        }
        return idx;
    }

    void UltraCanvasFilerWidget::DropPathsInto(const std::vector<std::string>& paths,
                                               const std::string& destDir,
                                               bool copy) {
        std::error_code ec;
        if (!fs::is_directory(destDir, ec)) {
            ReportError("Drop target is not a folder: " + destDir);
            return;
        }
        fs::path canonicalDest = fs::weakly_canonical(fs::path(destDir), ec);

        bool changed = false;
        for (const std::string& src : paths) {
            fs::path from(src);
            ec.clear();
            if (!fs::exists(from, ec)) continue;
            fs::path canonicalFrom = fs::weakly_canonical(from, ec);
            if (canonicalFrom == canonicalDest) continue;
            // Already in the target folder: a move would be a no-op, a copy
            // would just litter it with a duplicate.
            if (canonicalFrom.parent_path() == canonicalDest) continue;
            // Dropping a folder into itself (or into one of its own children)
            // would recurse forever.
            std::string fromStr = canonicalFrom.string();
            std::string destStr = canonicalDest.string();
            if (fs::is_directory(from, ec) && !fromStr.empty() &&
                destStr.compare(0, fromStr.size(), fromStr) == 0 &&
                (destStr.size() == fromStr.size() || destStr[fromStr.size()] == '/')) {
                ReportError("Cannot drop a folder into itself: " + src);
                continue;
            }

            std::string dest = UniquePathIn(destDir, from.filename().string());
            ec.clear();
            if (copy) {
                fs::copy(from, dest, fs::copy_options::recursive, ec);
            } else {
                fs::rename(from, dest, ec);
                if (ec) {   // cross-device move: copy + delete
                    ec.clear();
                    fs::copy(from, dest, fs::copy_options::recursive, ec);
                    if (!ec) fs::remove_all(from, ec);
                }
            }
            if (ec) ReportError((copy ? "Copy failed for " : "Move failed for ")
                                + src + ": " + ec.message());
            else changed = true;
        }
        if (changed) {
            Refresh();
            NotifyFolderModified(destDir);
            // A move also emptied the folder the files came from.
            if (!copy) NotifyFolderModified();
        }
    }

    // ===== NATIVE DRAG & DROP =====
    bool UltraCanvasFilerWidget::StartNativeDragOfSelection() {
        std::vector<std::string> paths;
        for (const FilerEntry& e : GetSelectedEntries()) paths.push_back(e.path);
        return StartNativeDragOfPaths(paths);
    }

    bool UltraCanvasFilerWidget::StartNativeDragOfPaths(
            const std::vector<std::string>& paths) {
        UltraCanvasWindowBase* win = GetWindow();
        if (!win || paths.empty()) return false;

        // The pointer leaves for the drag: drop the hover state now, the
        // widget won't see mouse events until the drag ends.
        if (hoveredIndex != -1) { hoveredIndex = -1; RequestRedraw(); }
        HideHoverTooltip();

        // The drop target performs the copy / move itself; after a move this
        // folder needs a rescan to drop the vanished entries.
        std::weak_ptr<UltraCanvasUIElement> weakSelf = weak_from_this();
        return win->StartNativeFileDrag(paths,
                [weakSelf](bool accepted, bool moved) {
                    if (!accepted || !moved) return;
                    if (auto self = weakSelf.lock()) {
                        auto* filer = static_cast<UltraCanvasFilerWidget*>(self.get());
                        filer->Refresh();
                        filer->NotifyFolderModified();   // files left this folder
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
        if (changed) { Refresh(); NotifyFolderModified(); }
    }

    std::string UltraCanvasFilerWidget::UniquePathIn(const std::string& folder,
                                                     const std::string& baseName) {
        fs::path base(baseName);
        std::string stem = base.stem().string();
        std::string ext = base.extension().string();   // includes the dot
        fs::path dir(folder);
        fs::path candidate = dir / baseName;
        std::error_code ec;
        int n = 2;
        while (fs::exists(candidate, ec)) {
            candidate = dir / (stem + " (" + std::to_string(n++) + ")" + ext);
        }
        return candidate.string();
    }

    std::string UltraCanvasFilerWidget::UniqueChildPath(const std::string& baseName) const {
        return UniquePathIn(currentPath, baseName);
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
        if (paths.empty()) {
            // No files on either clipboard: paste raw clipboard data (an
            // image or text copied in another program) as a new file.
            PasteClipboardDataAsFile();
            return;
        }

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
        NotifyFolderModified();
    }

    namespace {
        // File extension for a clipboard image MIME type ("image/png" → "png").
        std::string ImageMimeExtension(const std::string& mimeType) {
            std::string sub = mimeType.substr(mimeType.find('/') + 1);
            size_t params = sub.find(';');   // "image/png;foo=bar"
            if (params != std::string::npos) sub = sub.substr(0, params);
            if (sub == "jpeg") return "jpg";
            if (sub == "svg+xml") return "svg";
            if (sub == "x-bmp" || sub == "x-ms-bmp") return "bmp";
            if (sub.empty()) return "png";
            return sub;
        }

        bool WriteFileBytes(const std::string& path, const void* data, size_t size) {
            std::ofstream out(path, std::ios::binary);
            if (!out) return false;
            out.write(static_cast<const char*>(data),
                      static_cast<std::streamsize>(size));
            out.close();   // flush now — the folder is rescanned right after
            return out.good();
        }
    }

    bool UltraCanvasFilerWidget::PasteClipboardDataAsFile() {
        UltraCanvasClipboard* cb = GetClipboard();
        if (!cb) return false;

        std::error_code ec;
        if (!fs::is_directory(currentPath, ec)) {
            ReportError("Paste target is not a writable folder: " + currentPath);
            return false;
        }

        // An image wins over text: copying a bitmap in a browser typically
        // offers its URL / alt text as a text target too, and the image is
        // what the user copied.
        std::vector<uint8_t> imageData;
        std::string mimeType;
        if (cb->GetImage(imageData, mimeType) && !imageData.empty()) {
            std::string dest =
                    UniqueChildPath("Pasted image." + ImageMimeExtension(mimeType));
            if (!WriteFileBytes(dest, imageData.data(), imageData.size())) {
                ReportError("Paste failed for clipboard image: " + dest);
                return false;
            }
            Refresh();
            NotifyFolderModified();
            return true;
        }

        std::string text;
        if (cb->GetText(text) && !text.empty()) {
            std::string dest = UniqueChildPath("Pasted text.txt");
            if (!WriteFileBytes(dest, text.data(), text.size())) {
                ReportError("Paste failed for clipboard text: " + dest);
                return false;
            }
            Refresh();
            NotifyFolderModified();
            return true;
        }
        return false;
    }

    void UltraCanvasFilerWidget::DeleteSelection() {
        DeleteEntries(GetSelectedEntries());
    }

    void UltraCanvasFilerWidget::DeleteEntries(
            const std::vector<FilerEntry>& victims) {
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

    std::string UltraCanvasFilerWidget::NeighbourPathAfterRemoval(
            const std::vector<FilerEntry>& victims) const {
        auto isVictim = [&victims](const std::string& path) {
            for (const FilerEntry& v : victims) if (v.path == path) return true;
            return false;
        };
        // First / last position the removal touches, so a scattered
        // multi-selection walks outwards from the block it spans.
        size_t first = entries.size(), last = 0;
        for (size_t i = 0; i < entries.size(); ++i) {
            if (!isVictim(entries[i].path)) continue;
            if (first == entries.size()) first = i;
            last = i;
        }
        if (first == entries.size()) return {};   // nothing of it is listed
        for (size_t i = last + 1; i < entries.size(); ++i)
            if (!isVictim(entries[i].path)) return entries[i].path;
        for (size_t i = first; i-- > 0; )
            if (!isVictim(entries[i].path)) return entries[i].path;
        return {};                                // the folder empties out
    }

    void UltraCanvasFilerWidget::PerformDeletion(
            const std::vector<FilerEntry>& victims) {
        // When the delete takes the whole selection away, hand the selection
        // on to the entry that fills its place instead of leaving nothing
        // selected (SetSelectNextAfterDelete). Picked here, while the old
        // listing still describes the folder; the rescan below applies it.
        // A delete of entries that are not selected — the hover icon menu
        // acting on the entry under the cursor — leaves the selection alone.
        if (selectNextAfterDelete && !selection.empty()) {
            bool selectionSurvives = false;
            for (const FilerEntry& s : GetSelectedEntries()) {
                bool doomed = false;
                for (const FilerEntry& v : victims)
                    if (v.path == s.path) { doomed = true; break; }
                if (!doomed) { selectionSurvives = true; break; }
            }
            if (!selectionSurvives)
                selectAfterScanPath = NeighbourPathAfterRemoval(victims);
        }

        std::error_code ec;
#ifdef ULTRACANVAS_HAS_VIRTUALFS
        // Entries living inside an archive cannot be removed via the real
        // filesystem. They are grouped per archive and deleted with ONE
        // batched VirtualFS call each, so the archive is rewritten once for
        // the whole selection — deleting entries one-by-one would rewrite
        // the archive once per entry, which for thousands of files takes
        // practically forever.
        std::vector<std::string> archiveOrder;
        std::map<std::string, std::vector<std::string>> archiveVictims;
        std::vector<FilerEntry> fsVictims;
        for (const FilerEntry& e : victims) {
            // A real file/dir always wins - even if a path component looks
            // like an archive name (a real folder named "backup.zip").
            if (!fs::exists(e.path, ec)) {
                auto resolved = VirtualFS::VirtualFSPath::Resolve(e.path);
                if (resolved.isInsideArchive && !resolved.virtualPath.empty()) {
                    auto& list = archiveVictims[resolved.realPath];
                    if (list.empty()) archiveOrder.push_back(resolved.realPath);
                    list.push_back(resolved.virtualPath);
                    continue;
                }
            }
            fsVictims.push_back(e);
        }
        for (const std::string& archive : archiveOrder) {
            auto result = VirtualFS::VirtualFS_DeleteFromArchive(
                    archive, archiveVictims[archive]);
            if (result != VirtualFS::VirtualFSResult::Success) {
                ReportError("Delete failed in " + archive + ": " +
                            VirtualFS::VirtualFSResultToString(result));
            }
        }
#else
        const std::vector<FilerEntry>& fsVictims = victims;
#endif
        for (const FilerEntry& e : fsVictims) {
            fs::remove_all(e.path, ec);
            if (ec) ReportError("Delete failed for " + e.path + ": " + ec.message());
        }
        // Silent clear when a neighbour is waiting to inherit the selection:
        // the rescan reports that one change. Firing an empty selection first
        // would fold an attached preview pane away and open it again.
        if (selectAfterScanPath.empty()) ClearSelection();
        else                             selection.clear();
        Refresh();
        // Report every folder the deletion emptied: in a file-list display the
        // victims can come from different folders. For a folder listing they
        // all share currentPath, so this reports it once.
        if (onFolderModified) {
            std::unordered_set<std::string> reported;
            for (const FilerEntry& e : victims) {
                const std::string folder = fs::path(e.path).parent_path().string();
                if (!folder.empty() && reported.insert(folder).second)
                    NotifyFolderModified(folder);
            }
        }
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
            // realPath stays empty for entries inside archives - no
            // thumbnail can be decoded from those, only name and type.
            struct PreviewItem {
                std::string name;
                std::string realPath;
                bool isDir = false;
            };
            std::error_code ec;
            std::vector<PreviewItem> inner;
            size_t totalInner = 0;
            if (fs::is_directory(previewFolder->path, ec)) {
                for (fs::directory_iterator it(previewFolder->path, ec), end;
                     it != end; it.increment(ec)) {
                    if (ec) break;
                    if (inner.size() < 10) {
                        std::error_code e2;
                        inner.push_back({it->path().filename().string(),
                                         it->path().string(),
                                         it->is_directory(e2)});
                    }
                    ++totalInner;
                }
            }
#ifdef ULTRACANVAS_HAS_VIRTUALFS
            else {
                for (const VirtualFS::VirtualFSEntry& v
                     : VirtualFS::VirtualFS_ListDirectory(previewFolder->path)) {
                    if (inner.size() < 10) {
                        inner.push_back({v.name, "", v.IsDirectory()});
                    }
                    ++totalInner;
                }
            }
#endif

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
            for (const PreviewItem& pi : inner) {
                const std::string& name = pi.name;

                auto cell = std::make_shared<UltraCanvasContainer>(
                        "FilerDelCell" + std::to_string(idx));
                cell->layout.SetFlexColumn().SetFlexGap(2)
                            .SetFlexAlignItems(CSSLayout::AlignItems::Center);
                cell->layoutItem.SetFlexGrow(0).SetFlexShrink(0);

                auto thumb = CreateImageElement(
                        "FilerDelThumb" + std::to_string(idx), 0, 0, tile, tile);
                thumb->SetFitMode(ImageFitMode::Contain);
                if (!pi.isDir && !pi.realPath.empty() &&
                    ImagePipelineLoadsExtension(
                            LowerExtension(fs::path(pi.realPath).filename().string())))
                    thumb->LoadFromFile(pi.realPath);
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
        NotifyFolderModified();
    }

    void UltraCanvasFilerWidget::StartRename(size_t entryIndex) {
        if (entryIndex >= entries.size()) return;
        CancelPendingRename();   // the editor opens now; drop any armed click
        if (renamingIndex >= 0) CancelRename();   // only one editor at a time
        renamingIndex = static_cast<int>(entryIndex);
        EnsureVisible(entryIndex);

        // A real text field, created fresh per rename so no caret / selection /
        // undo state leaks from an earlier edit. It is positioned over the
        // item's name by PositionRenameInput() on every frame.
        const FilerEntry& e = entries[entryIndex];
        renameInput = CreateTextInput("filer-rename-input", 1, 1, 120, 24);
        TextInputStyle ts;
        ts.backgroundColor  = style.renameFieldColor;
        ts.borderColor      = style.renameBorderColor;
        ts.focusBorderColor = style.renameBorderColor;
        ts.textColor        = style.textColor;
        ts.caretColor       = style.textColor;
        ts.selectionColor   = Color(style.selectionColor.r, style.selectionColor.g,
                                    style.selectionColor.b, 170);
        ts.borderWidth = 1;
        ts.borderRadius = 0;
        ts.paddingLeft = 3;
        ts.paddingRight = 3;
        ts.paddingTop = 1;
        ts.paddingBottom = 1;
        ts.fontStyle.fontFamily = style.fontFamily;
        ts.fontStyle.fontSize = ItemNameFontSize();  // match the on-screen name
        renameInput->SetStyle(ts);
        // A rename field is not a form: no validation state (its ✓/✗ glyph
        // would sit inside the narrow field) and no clear button.
        renameInput->SetShowValidationState(false);
        renameInput->SetText(e.name);
        // Windows-style initial selection: the base name without the extension
        // (folders select whole), so typing replaces the name and keeps ".ext".
        size_t selEnd = e.name.size();
        if (!e.isDirectory) {
            size_t dot = e.name.rfind('.');
            if (dot != std::string::npos && dot > 0) selEnd = dot;
        }
        renameInput->SetSelection(0, selEnd);
        renameInput->onEnterPressed = [this](const std::string&) {
            CommitRename();
            return true;
        };
        renameInput->onEscapePressed = [this]() {
            CancelRename();
            return true;
        };
        // Clicking anywhere else — another entry, another widget — takes the
        // focus and commits, Explorer-style. Commit drops renamingIndex before
        // tearing the editor down, so the teardown's own focus loss cannot
        // re-enter. restoreFocus=false: whatever took the focus keeps it.
        renameInput->onFocusLost = [this]() {
            if (renamingIndex >= 0) CommitRename(false);
        };
        AddChild(renameInput);
        PositionRenameInput();
        renameInput->SetFocus(true);
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::CommitRename(bool restoreFocus) {
        if (renamingIndex < 0 || renamingIndex >= (int)entries.size()) {
            renamingIndex = -1;
            DestroyRenameInput(restoreFocus);
            return;
        }
        // Copies: Refresh() below rebuilds `entries`.
        const std::string oldName = entries[renamingIndex].name;
        const std::string oldPath = entries[renamingIndex].path;
        std::string newName = renameInput ? renameInput->GetText() : std::string();
        renamingIndex = -1;
        DestroyRenameInput(restoreFocus);
        if (newName.empty() || newName == oldName ||
            newName.find('/') != std::string::npos) {
            RequestRedraw();
            return;
        }
        std::error_code ec;
        // Rename in place: in the file-list (search result) display the entry
        // may live outside currentPath, so target its own parent folder.
        fs::path target = fs::path(oldPath).parent_path() / newName;
        // "Already exists" must not fire when the target IS this entry: on a
        // case-insensitive filesystem (Windows, macOS) "photos" -> "Photos"
        // resolves to the same directory, and rejecting it would make a
        // case-only rename impossible.
        if (fs::exists(target, ec) && !fs::equivalent(oldPath, target, ec)) {
            ReportError("Rename failed: \"" + newName + "\" already exists");
            RequestRedraw();
            return;
        }
        fs::rename(oldPath, target, ec);
        if (ec) {
            ReportError("Rename failed for " + oldPath + ": " + ec.message());
        } else {
            // The rescan restores the selection by path, and the renamed
            // entry's old path is gone — without this substitution it drops
            // out and the entry ends up unselected, which silently breaks
            // every follow-up command that works on the selection (F2 and the
            // Rename button most visibly: they need a single selected entry,
            // so a second rename in a row did nothing at all). Renaming an
            // entry that was not selected still leaves the selection alone.
            renamedFromPath = oldPath;
            renamedToPath = target.string();
        }
        // The rescan below clears renamedToPath, so decide here whether the
        // rename went through — only then was work done in the folder.
        const bool renamed = !ec;
        Refresh();
        if (renamed)
            NotifyFolderModified(target.parent_path().string());
    }

    void UltraCanvasFilerWidget::CancelRename(bool restoreFocus) {
        if (renamingIndex == -1 && !renameInput) return;
        renamingIndex = -1;
        DestroyRenameInput(restoreFocus);
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::DestroyRenameInput(bool restoreFocus) {
        if (!renameInput) return;
        auto input = renameInput;
        renameInput.reset();
        // Teardown must not fire the finish callbacks again: RemoveChild drops
        // the editor's focus, which would call onFocusLost mid-destruction.
        input->onFocusLost = nullptr;
        input->onEnterPressed = nullptr;
        input->onEscapePressed = nullptr;
        RemoveChild(input);
        // Enter / Escape end the edit with the keyboard still "in" the editor;
        // hand the focus back to the widget so list navigation carries on.
        if (restoreFocus) SetFocus(true);
    }

    Rect2Di UltraCanvasFilerWidget::RenameFieldRect(const ItemLayout& item) const {
        // The editable field sits where the item's name is shown.
        if (viewType == FilerViewType::Details || viewType == FilerViewType::List ||
            viewType == FilerViewType::BarSize) {
            int x = item.imageRect.x + item.imageRect.width + 4;
            Rect2Di field(x, item.rect.y + 1,
                          std::max(80, item.rect.width - (x - item.rect.x) - 8),
                          item.rect.height - 2);
            if (viewType == FilerViewType::Details && !detailsColumns.empty()) {
                field.width = std::max(80, detailsColumns[0].x
                                       + detailsColumns[0].width - x - 4);
            }
            return field;
        }
        // A treemap cell has no caption band below its icon — the cell IS the
        // icon rect and the name is drawn inside it, at the top — so the band
        // formula below would put the editor under the cell, off the item
        // entirely (it lands on the next cell or past the last row, which is
        // why renaming looked like it did nothing here). Put it over the name.
        if (viewType == FilerViewType::TreeMap) {
            int h = NameLineHeight() + 4;
            // Cells get arbitrarily small; keep the field usable by growing it
            // over the neighbours rather than shrinking it to a few pixels,
            // and keep it inside the viewport so it stays reachable.
            int w = std::max(80, item.rect.width - 4);
            int x = item.rect.x + 2;
            // The treemap fills the viewport exactly, so its right edge is the
            // visible one: pull a widened field back inside it.
            if (contentWidth > 0 && x + w > contentWidth - 2)
                x = std::max(0, contentWidth - 2 - w);
            return Rect2Di(x, item.rect.y + 2, w, h);
        }
        // Thumbnails: the whole (possibly multi-line) caption band; the editor
        // itself is a single line filling it.
        int capTop = item.imageRect.y + item.imageRect.height;
        return Rect2Di(item.rect.x + 2, capTop, item.rect.width - 4,
                       std::max(CaptionBandHeight(item.captionLines),
                                NameLineHeight() + 4));
    }

    void UltraCanvasFilerWidget::PositionRenameInput() {
        if (renamingIndex < 0 || !renameInput) return;
        for (const ItemLayout& it : items) {
            if (static_cast<int>(it.entryIndex) != renamingIndex) continue;
            Rect2Di field = RenameFieldRect(it);
            Rect2Df want(field.x - scrollOffsetX, field.y - scrollOffsetY,
                         field.width, field.height);
            if (renameInput->GetBounds() == want) return;
            // Keep the CSS-layout position in sync with the immediate bounds:
            // the next Arrange pass must place the editor where it is drawn.
            renameInput->layoutItem.position = CSSLayout::Position();
            renameInput->layoutItem.position->left = CSSLayout::Dimension::Px(want.x);
            renameInput->layoutItem.position->top  = CSSLayout::Dimension::Px(want.y);
            renameInput->layoutItem.SetPositionType(CSSLayout::PositionType::AbsoluteUI);
            renameInput->SetElementSize(Size2Df(want.width, want.height));
            renameInput->SetBounds(want);
            return;
        }
    }

    void UltraCanvasFilerWidget::ArmPendingRenameTimer() {
        auto* app = UltraCanvasApplication::GetInstance();
        if (!app || pendingRenameIndex < 0) {
            pendingRenameIndex = -1;
            return;
        }
        if (pendingRenameTimer != InvalidTimerId) app->StopTimer(pendingRenameTimer);
        pendingRenameTimer = app->StartTimer(kRenameClickDelayMs, false,
                                             [this](TimerId) {
            pendingRenameTimer = InvalidTimerId;
            int idx = pendingRenameIndex;
            pendingRenameIndex = -1;
            // Only rename if the entry is still the sole selection — a
            // refresh, keyboard move or programmatic change in the meantime
            // means the click no longer applies.
            if (idx >= 0 && idx < (int)entries.size() &&
                selection.size() == 1 && (int)selection.front() == idx) {
                StartRename(static_cast<size_t>(idx));
            }
        });
    }

    void UltraCanvasFilerWidget::CancelPendingRename() {
        pendingRenameIndex = -1;
        if (pendingRenameTimer == InvalidTimerId) return;
        if (auto* app = UltraCanvasApplication::GetInstance())
            app->StopTimer(pendingRenameTimer);
        pendingRenameTimer = InvalidTimerId;
    }

    void UltraCanvasFilerWidget::CompressSelection(const std::string& extension) {
#ifdef ULTRACANVAS_HAS_VIRTUALFS
        std::vector<FilerEntry> targets = SelectionOrAll();
        if (targets.empty()) return;
        std::vector<std::string> paths;
        for (const FilerEntry& e : targets) paths.push_back(e.path);
        std::string base = (targets.size() == 1)
                ? fs::path(targets[0].name).stem().string()
                : fs::path(currentPath).filename().string();
        if (base.empty()) base = "archive";
        // The extension drives the archive format chosen by the VirtualFS bridge.
        std::string ext = extension.empty() ? std::string("zip") : extension;
        if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
        std::string dest = UniqueChildPath(base + "." + ext);
        if (!UCVFSBridge::CreateArchive(dest, paths)) {
            ReportError("Compression failed for " + dest);
            return;
        }
        Refresh();
        NotifyFolderModified(fs::path(dest).parent_path().string());
#else
        (void)extension;
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
        if (any) { Refresh(); NotifyFolderModified(); }
#else
        ReportError("Extract requires the VirtualFS module");
#endif
    }

    // ===== COMPRESS DIALOG =====

    std::string UltraCanvasFilerWidget::ArchiveIconTag(const std::string& extension) {
        if (extension == "zip")     return "ZIP";
        if (extension == "7z")      return "7Z";
        if (extension == "tar")     return "TAR";
        if (extension == "tar.gz")  return "TGZ";
        if (extension == "tar.bz2") return "TBZ";
        if (extension == "tar.xz")  return "TXZ";
        if (extension == "tar.zst") return "ZST";
        // Fallback: the last extension token, uppercased and clipped.
        std::string tag = extension;
        size_t dot = tag.rfind('.');
        if (dot != std::string::npos) tag = tag.substr(dot + 1);
        tag = tag.substr(0, 4);
        std::transform(tag.begin(), tag.end(), tag.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        return tag;
    }

    void UltraCanvasFilerWidget::OpenCompressDialog(const std::string& extension,
                                                    const std::string& formatLabel) {
        std::vector<FilerEntry> targets = SelectionOrAll();
        if (targets.empty()) return;

        compressDlg = CompressDialogState();
        compressDlg.active = true;
        compressDlg.extension = extension;
        compressDlg.formatLabel = formatLabel;
        for (const FilerEntry& e : targets) compressDlg.sourcePaths.push_back(e.path);

        // Same default name the direct CompressSelection() would pick.
        std::string base = (targets.size() == 1)
                ? fs::path(targets[0].name).stem().string()
                : fs::path(currentPath).filename().string();
        if (base.empty()) base = "archive";
        compressDlg.nameBuffer = base;
        compressDlg.destDir = currentPath;
        compressDlg.nameFocused = true;

        if (renamingIndex >= 0) CancelRename();
        SetFocus(true);
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::CloseCompressDialog() {
        if (compressDlg.draggingIcon) {
            if (auto* app = UltraCanvasApplication::GetInstance()) app->ReleaseMouse();
        }
        compressDlg = CompressDialogState();
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::CommitCompressDialog() {
        CompressDialogState d = compressDlg;   // copy: we close before the work
        CloseCompressDialog();
#ifdef ULTRACANVAS_HAS_VIRTUALFS
        if (d.sourcePaths.empty()) return;
        std::string baseName = d.nameBuffer.empty() ? std::string("archive")
                                                     : d.nameBuffer;
        std::string ext = d.extension.empty() ? std::string("zip") : d.extension;

        std::error_code ec;
        fs::path dir(d.destDir.empty() ? currentPath : d.destDir);
        if (!fs::is_directory(dir, ec)) dir = currentPath;

        // Uniquify while keeping the full (possibly compound) extension intact,
        // so ".tar.gz" stays ".tar.gz" rather than becoming ".tar (2).gz".
        fs::path candidate = dir / (baseName + "." + ext);
        int n = 2;
        while (fs::exists(candidate, ec)) {
            candidate = dir / (baseName + " (" + std::to_string(n++) + ")." + ext);
        }
        std::string dest = candidate.string();

        if (!UCVFSBridge::CreateArchive(dest, d.sourcePaths)) {
            ReportError("Compression failed for " + dest);
            return;
        }
        Refresh();
        // The archive can be written into a folder the icon was dragged onto.
        NotifyFolderModified(fs::path(dest).parent_path().string());
#else
        (void)d;
        ReportError("Compress requires the VirtualFS module");
#endif
    }

    int UltraCanvasFilerWidget::FolderIndexAtLocal(const Point2Di& localPoint) const {
        // Points over the dialog panel are never folder drop targets.
        if (compressDlg.panel.Contains(localPoint)) return -1;
        int idx = ItemAt(ToContentPoint(localPoint));
        if (idx >= 0 && idx < static_cast<int>(entries.size()) &&
            entries[idx].isDirectory) {
            return idx;
        }
        return -1;
    }

    void UltraCanvasFilerWidget::LayoutCompressDialog(const Rect2Di& bounds) {
        CompressDialogState& d = compressDlg;

        int pw = std::min(400, std::max(280, bounds.width - 60));
        int ph = 300;
        int px = bounds.x + (bounds.width - pw) / 2;
        int py = bounds.y + (bounds.height - ph) / 2;
        if (py < bounds.y + 8) py = bounds.y + 8;
        d.panel = Rect2Di(px, py, pw, ph);

        int iconSz = 64;
        d.iconRect = Rect2Di(px + (pw - iconSz) / 2, py + 44, iconSz, iconSz);

        // Below the icon: format label (~18) + hint (~16) + gap, then the field.
        int nameY = d.iconRect.y + iconSz + 8 + 18 + 16 + 12;
        d.nameRect = Rect2Di(px + 16, nameY, pw - 32, 30);

        int btnW = 104, btnH = 30, gap = 10;
        int by = py + ph - btnH - 14;
        d.cancelRect = Rect2Di(px + pw - 16 - btnW, by, btnW, btnH);
        d.okRect     = Rect2Di(d.cancelRect.x - gap - btnW, by, btnW, btnH);
    }

    void UltraCanvasFilerWidget::DrawDialogButton(IRenderContext* ctx,
                                                  const Rect2Di& rect,
                                                  const std::string& label,
                                                  bool primary, bool hovered) {
        Color fill = primary ? Color(66, 133, 244, 255) : Color(238, 238, 242, 255);
        if (hovered) {
            fill = primary ? Color(90, 150, 250, 255) : Color(226, 226, 232, 255);
        }
        ctx->SetFillPaint(fill);
        ctx->FillRoundedRectangle(Rect2Dd(rect), 5);
        if (!primary) {
            ctx->SetStrokePaint(Color(0, 0, 0, 40));
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawRoundedRectangle(Rect2Dd(rect), 5);
        }
        FontStyle fsty;
        fsty.fontFamily = style.fontFamily;
        fsty.fontSize = style.fontSize;
        fsty.fontWeight = FontWeight::Bold;
        ctx->SetFontStyle(fsty);
        ctx->SetTextPaint(primary ? Colors::White : style.textColor);
        Size2Di ts = ctx->GetTextLineDimensions(label);
        ctx->DrawText(label, Point2Dd(rect.x + (rect.width - ts.width) / 2.0,
                                      rect.y + (rect.height - ts.height) / 2.0));
    }

    void UltraCanvasFilerWidget::DrawCompressDialog(IRenderContext* ctx,
                                                    const Rect2Di& bounds) {
        LayoutCompressDialog(bounds);
        const CompressDialogState& d = compressDlg;

        // A synthetic archive entry drives the file-type icon glyph.
        FilerEntry synth;
        synth.isDirectory = false;
        synth.category = FilerFileCategory::Archive;
        synth.extension = ArchiveIconTag(d.extension);

        ctx->PushState();
        ctx->ClipRect(Rect2Dd(bounds));

        // Dimmed, semi-transparent backdrop — folders stay visible so the icon
        // can be dragged onto one of them.
        ctx->SetFillPaint(Color(20, 22, 28, 96));
        ctx->FillRectangle(Rect2Dd(bounds));

        // Highlight the folder currently under the dragged icon.
        if (d.draggingIcon && d.dropFolderIndex >= 0) {
            for (const ItemLayout& it : items) {
                if (static_cast<int>(it.entryIndex) == d.dropFolderIndex) {
                    Rect2Di r(it.rect.x - scrollOffsetX, it.rect.y - scrollOffsetY,
                              it.rect.width, it.rect.height);
                    Color fillc = style.selectionColor; fillc.a = 130;
                    ctx->SetFillPaint(fillc);
                    ctx->FillRoundedRectangle(Rect2Dd(r), 6);
                    ctx->SetStrokePaint(style.selectionBorderColor);
                    ctx->SetStrokeWidth(2.5f);
                    ctx->DrawRoundedRectangle(Rect2Dd(r), 6);
                    break;
                }
            }
        }

        // Panel.
        ctx->SetFillPaint(style.backgroundColor);
        ctx->FillRoundedRectangle(Rect2Dd(d.panel), 10);
        ctx->SetStrokePaint(Color(0, 0, 0, 45));
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRoundedRectangle(Rect2Dd(d.panel), 10);

        // Title.
        FontStyle titleFont;
        titleFont.fontFamily = style.fontFamily;
        titleFont.fontSize = style.fontSize + 2;
        titleFont.fontWeight = FontWeight::Bold;
        ctx->SetFontStyle(titleFont);
        ctx->SetTextPaint(style.textColor);
        ctx->DrawText("Compress", Point2Dd(d.panel.x + 16, d.panel.y + 12));

        // File-type icon on top. While being dragged a ghost follows the cursor
        // and the panel shows a faint placeholder in its place.
        if (d.draggingIcon) {
            ctx->SetFillPaint(Color(0, 0, 0, 22));
            ctx->FillRoundedRectangle(Rect2Dd(d.iconRect), 4);
        } else {
            DrawEntryIcon(ctx, synth, d.iconRect);
        }

        // Format label (already includes the extension) under the icon.
        FontStyle bodyFont;
        bodyFont.fontFamily = style.fontFamily;
        bodyFont.fontSize = style.fontSize;
        ctx->SetFontStyle(bodyFont);
        ctx->SetTextPaint(style.textColor);
        Size2Di fts = ctx->GetTextLineDimensions(d.formatLabel);
        int fmtY = d.iconRect.y + d.iconRect.height + 8;
        ctx->DrawText(d.formatLabel,
                      Point2Dd(d.panel.x + (d.panel.width - fts.width) / 2.0, fmtY));

        // Drag hint (smaller, grey).
        FontStyle smallFont;
        smallFont.fontFamily = style.fontFamily;
        smallFont.fontSize = style.smallFontSize;
        ctx->SetFontStyle(smallFont);
        ctx->SetTextPaint(style.secondaryTextColor);
        std::string hint = "Drag the icon onto a folder to change the location";
        std::string hintFit = EllipsizeText(ctx, hint, d.panel.width - 24);
        Size2Di hts = ctx->GetTextLineDimensions(hintFit);
        ctx->DrawText(hintFit,
                      Point2Dd(d.panel.x + (d.panel.width - hts.width) / 2.0,
                               fmtY + fts.height + 4));

        // Name field.
        ctx->SetFillPaint(style.renameFieldColor);
        ctx->FillRoundedRectangle(Rect2Dd(d.nameRect), 4);
        ctx->SetStrokePaint(d.nameFocused ? style.renameBorderColor
                                          : Color(0, 0, 0, 55));
        ctx->SetStrokeWidth(d.nameFocused ? 2.0f : 1.0f);
        ctx->DrawRoundedRectangle(Rect2Dd(d.nameRect), 4);

        ctx->SetFontStyle(bodyFont);
        Size2Di baseSz = ctx->GetTextLineDimensions(
                d.nameBuffer.empty() ? std::string("Ag") : d.nameBuffer);
        int tx = d.nameRect.x + 8;
        int ty = d.nameRect.y + (d.nameRect.height - baseSz.height) / 2;
        Size2Di nameSz = ctx->GetTextLineDimensions(d.nameBuffer);
        ctx->SetTextPaint(style.textColor);
        ctx->DrawText(d.nameBuffer, Point2Dd(tx, ty));
        ctx->SetTextPaint(style.secondaryTextColor);
        ctx->DrawText("." + d.extension, Point2Dd(tx + nameSz.width, ty));
        if (d.nameFocused) {
            int cx = tx + nameSz.width + 1;
            ctx->SetStrokePaint(style.renameBorderColor);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawLine(Point2Dd(cx, d.nameRect.y + 5),
                          Point2Dd(cx, d.nameRect.y + d.nameRect.height - 5));
        }

        // Destination path shown separately as smaller text.
        ctx->SetFontStyle(smallFont);
        ctx->SetTextPaint(style.secondaryTextColor);
        std::string dir = d.destDir.empty() ? currentPath : d.destDir;
        std::string pathText = "Location:  " + dir;
        pathText = EllipsizeText(ctx, pathText, d.panel.width - 32);
        ctx->DrawText(pathText,
                      Point2Dd(d.panel.x + 16, d.nameRect.y + d.nameRect.height + 8));

        // Buttons.
        DrawDialogButton(ctx, d.okRect, "Compress", true, d.okHover);
        DrawDialogButton(ctx, d.cancelRect, "Cancel", false, d.cancelHover);

        // Ghost icon under the cursor, drawn last so it floats above everything.
        if (d.draggingIcon) {
            Rect2Di ghost(d.dragPos.x - 24, d.dragPos.y - 24, 48, 48);
            DrawEntryIcon(ctx, synth, ghost);
        }

        ctx->PopState();
    }

    bool UltraCanvasFilerWidget::HandleCompressDialogEvent(const UCEvent& event) {
        CompressDialogState& d = compressDlg;
        switch (event.type) {
            case UCEventType::KeyDown: {
                if (event.virtualKey == UCKeys::Escape) { CloseCompressDialog(); return true; }
                if (event.virtualKey == UCKeys::Return)  { CommitCompressDialog(); return true; }
                if (event.virtualKey == UCKeys::Backspace) {
                    if (d.nameFocused && !d.nameBuffer.empty()) {
                        size_t cut = d.nameBuffer.size() - 1;
                        while (cut > 0 &&
                               (static_cast<unsigned char>(d.nameBuffer[cut]) & 0xC0) == 0x80)
                            --cut;
                        d.nameBuffer.erase(cut);
                        RequestRedraw();
                    }
                    return true;
                }
                // Printable characters are delivered on the KeyDown event
                // itself (event.character / event.text) — the platform layers
                // never emit a separate KeyChar/TextInput event. Same
                // convention as UltraCanvasTextInput.
                if (d.nameFocused && !event.ctrl && !event.alt) {
                    std::string in = event.text;
                    if (in.empty() && event.character >= 32)
                        in.assign(1, event.character);
                    std::string filtered;
                    for (char c : in) {
                        if (static_cast<unsigned char>(c) >= 32 && c != '/' && c != '\\')
                            filtered += c;
                    }
                    if (!filtered.empty()) { d.nameBuffer += filtered; RequestRedraw(); }
                }
                return true;   // stay modal: swallow every other key
            }
            case UCEventType::TextInput: {
                if (d.nameFocused) {
                    std::string in = event.text;
                    if (in.empty() && event.character >= 32)
                        in.assign(1, static_cast<char>(event.character));
                    std::string filtered;
                    for (char c : in) {
                        if (static_cast<unsigned char>(c) >= 32 && c != '/' && c != '\\')
                            filtered += c;
                    }
                    if (!filtered.empty()) { d.nameBuffer += filtered; RequestRedraw(); }
                }
                return true;
            }
            case UCEventType::MouseDown: {
                if (event.button != UCMouseButton::Left) return true;
                // Keyboard events are routed to the window's focused element,
                // so any click while the modal is up must pull focus back to
                // this widget or typing would go elsewhere.
                SetFocus(true);
                Point2Di local(event.pointer.x, event.pointer.y);
                if (d.iconRect.Contains(local)) {
                    d.draggingIcon = true;
                    d.dragPos = local;
                    d.dropFolderIndex = -1;
                    if (auto* app = UltraCanvasApplication::GetInstance())
                        app->CaptureMouse(this);
                    RequestRedraw();
                    return true;
                }
                if (d.nameRect.Contains(local)) { d.nameFocused = true; RequestRedraw(); return true; }
                if (d.okRect.Contains(local))     { CommitCompressDialog(); return true; }
                if (d.cancelRect.Contains(local)) { CloseCompressDialog();  return true; }
                return true;   // modal: clicks elsewhere do nothing
            }
            case UCEventType::MouseMove: {
                Point2Di local(event.pointer.x, event.pointer.y);
                if (d.draggingIcon) {
                    d.dragPos = local;
                    d.dropFolderIndex = FolderIndexAtLocal(local);
                    RequestRedraw();
                } else {
                    bool ok = d.okRect.Contains(local);
                    bool cn = d.cancelRect.Contains(local);
                    if (ok != d.okHover || cn != d.cancelHover) {
                        d.okHover = ok; d.cancelHover = cn; RequestRedraw();
                    }
                }
                return true;
            }
            case UCEventType::MouseUp: {
                if (d.draggingIcon) {
                    Point2Di local(event.pointer.x, event.pointer.y);
                    if (auto* app = UltraCanvasApplication::GetInstance())
                        app->ReleaseMouse();
                    d.draggingIcon = false;
                    int fi = FolderIndexAtLocal(local);
                    if (fi >= 0) d.destDir = entries[fi].path;
                    d.dropFolderIndex = -1;
                    RequestRedraw();
                }
                return true;
            }
            default:
                return true;   // stay modal
        }
    }

    void UltraCanvasFilerWidget::SetNewDocumentTypes(
            const std::vector<FilerNewDocumentType>& types) {
        newDocumentTypes = types;
    }

    void UltraCanvasFilerWidget::CreateNewDocument(const FilerNewDocumentType& type) {
        if (onNewDocument && onNewDocument(type, currentPath)) {
            Refresh();
            NotifyFolderModified();
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
        NotifyFolderModified();
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
        // A reveal requested while a resize was still in flight (see
        // EnsureSelectionVisible) is applied here, against the settled layout.
        if (pendingRevealEntry >= 0) {
            size_t idx = static_cast<size_t>(pendingRevealEntry);
            pendingRevealEntry = -1;
            ScrollEntryIntoView(idx);
        }
    }

    void UltraCanvasFilerWidget::RecomputeLayout() {
        items.clear();
        detailsColumns.clear();
        captionLinesMeasured = true;   // only the thumbnail grid measures names
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

    // The Details table description: the Name column is flexible (it absorbs
    // whatever the others leave), the rest carry the widths the splitters edit.
    // Ordered exactly like FilerDetailsColumn.
    const UltraCanvasFilerWidget::DetailsColumnSpec
            UltraCanvasFilerWidget::kDetailsColumnSpecs[kFilerDetailsColumnCount] = {
        {FilerDetailsColumn::Name,         FilerSortField::Name,         "Name",     260, false, true},
        {FilerDetailsColumn::Path,         FilerSortField::Name,         "Path",     220, false, false},
        {FilerDetailsColumn::Size,         FilerSortField::Size,         "Size",     90,  true,  true},
        {FilerDetailsColumn::Type,         FilerSortField::Type,         "Type",     130, false, true},
        {FilerDetailsColumn::ModifiedDate, FilerSortField::ModifiedDate, "Modified", 150, false, true},
        {FilerDetailsColumn::CreatedDate,  FilerSortField::CreatedDate,  "Created",  150, false, true},
        {FilerDetailsColumn::Attributes,   FilerSortField::Name,         "Attr",     55,  false, false},
        {FilerDetailsColumn::Info,         FilerSortField::Name,         "Info",     120, false, false},
    };

    void UltraCanvasFilerWidget::EnsureDetailsColumnWidths() {
        if (detailsColumnWidths.size() == kFilerDetailsColumnCount) return;
        detailsColumnWidths.assign(kFilerDetailsColumnCount, 0);
        for (size_t i = 0; i < kFilerDetailsColumnCount; ++i)
            detailsColumnWidths[i] = kDetailsColumnSpecs[i].defaultWidth;
    }

    std::vector<size_t> UltraCanvasFilerWidget::VisibleDetailsSpecIndices() const {
        std::vector<size_t> vis;
        vis.reserve(kFilerDetailsColumnCount);
        for (size_t i = 0; i < kFilerDetailsColumnCount; ++i) {
            if (!fileListMode &&
                kDetailsColumnSpecs[i].id == FilerDetailsColumn::Path)
                continue;
            vis.push_back(i);
        }
        return vis;
    }

    void UltraCanvasFilerWidget::LayoutDetails(const Rect2Di& area) {
        EnsureDetailsColumnWidths();
        const std::vector<size_t> vis = VisibleDetailsSpecIndices();
        // The name column absorbs whatever the (splitter-resized) rest leaves,
        // so the table always spans the widget and dragging a splitter moves
        // width between two neighbours instead of scrolling the table sideways.
        int othersTotal = 0;
        for (size_t k = 1; k < vis.size(); ++k)
            othersTotal += detailsColumnWidths[vis[k]];
        detailsColumnWidths[0] = std::max(kMinNameColumnWidth,
                                          area.width - othersTotal - kScrollbarGutter);

        int x = area.x;
        for (size_t k = 0; k < vis.size(); ++k) {
            const DetailsColumnSpec& spec = kDetailsColumnSpecs[vis[k]];
            DetailsColumn c;
            c.id = spec.id;
            c.field = spec.field;
            c.title = spec.title;
            c.x = x;
            c.width = detailsColumnWidths[vis[k]];
            c.rightAligned = spec.rightAligned;
            c.sortable = spec.sortable;
            detailsColumns.push_back(c);
            x += c.width;
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
        int colW = std::max(kMinListColumnWidth, style.listColumnWidth);
        int gap = kListColumnGap;
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
        // The name occupies the caption band (one line, more when a name in the
        // row wraps); each enabled dataset field adds one line below it
        // (reserved uniformly so the grid stays aligned).
        int datasetH = DatasetLineCount() * DatasetLineHeight();
        int tileW = edge;
        int scrollbarGutter = 10;
        int availW = area.width - scrollbarGutter;
        int cols = std::max(1, (availW + gap) / (tileW + gap));
        size_t n = entries.size();

        // Wrapping needs text metrics. Outside a render pass the widget may not
        // be on a window yet: captions then stay single-line and the first
        // Render() rebuilds the layout with real measurements.
        int maxNameLines = std::max(1, style.captionMaxLines);
        IRenderContext* mctx = measureContext ? measureContext : GetRenderContext();
        captionLinesMeasured = (mctx != nullptr);
        if (mctx && maxNameLines > 1) {
            mctx->PushState();
            FontStyle nameFont;
            nameFont.fontFamily = style.fontFamily;
            nameFont.fontSize = style.smallFontSize;
            mctx->SetFontStyle(nameFont);
        } else {
            mctx = nullptr;
        }

        // Rows are laid out one grid line at a time so each can take its own
        // height. A row's image band is the tallest thumbnail actually shown in
        // it: any full-height item (folder / glyph / portrait or not-yet-measured
        // image) keeps the whole row at the square edge, and only a row whose
        // every tile is a shorter landscape image shrinks to close the gap.
        int y = area.y;
        for (size_t rowStart = 0; rowStart < n; rowStart += static_cast<size_t>(cols)) {
            size_t rowEnd = std::min(n, rowStart + static_cast<size_t>(cols));
            int rowImageH = 0;
            for (size_t i = rowStart; i < rowEnd; ++i) {
                rowImageH = std::max(rowImageH, ThumbnailImageHeight(entries[i], edge));
                if (rowImageH >= edge) break;   // pinned to full height already
            }
            if (rowImageH <= 0) rowImageH = edge;
            // Caption band of the row: the deepest wrapped name in it, so every
            // tile of the row keeps the same height and the grid stays aligned.
            int rowNameLines = 1;
            for (size_t i = rowStart; mctx && i < rowEnd; ++i) {
                rowNameLines = std::max(rowNameLines,
                                        CaptionLinesFor(mctx, entries[i].name,
                                                        tileW - 8));
                if (rowNameLines >= maxNameLines) break;
            }
            int tileH = rowImageH + CaptionBandHeight(rowNameLines) + datasetH;
            for (size_t i = rowStart; i < rowEnd; ++i) {
                int col = static_cast<int>(i - rowStart);
                ItemLayout it;
                it.entryIndex = i;
                it.rect = Rect2Di(area.x + col * (tileW + gap), y, tileW, tileH);
                it.imageRect = Rect2Di(it.rect.x, it.rect.y, tileW, rowImageH);
                it.captionLines = rowNameLines;
                items.push_back(it);
            }
            y += tileH + gap;
        }
        if (mctx) mctx->PopState();
        contentWidth = area.width;
        contentHeight = (n == 0) ? area.height : (y - gap + style.outerPadding);
    }

    void UltraCanvasFilerWidget::LayoutBarSize(const Rect2Di& area) {
        EnsureEffectiveSizes();
        int rowH = style.detailsRowHeight;
        int y = area.y;
        for (size_t i = 0; i < entries.size(); ++i) {
            ItemLayout it;
            it.entryIndex = i;
            it.rect = Rect2Di(area.x, y, area.width - kScrollbarGutter, rowH);
            it.imageRect = Rect2Di(area.x + 4, y + 3, rowH - 6, rowH - 6);
            items.push_back(it);
            y += rowH;
        }
        contentWidth = area.width;
        contentHeight = y + style.outerPadding;
    }

    int UltraCanvasFilerWidget::BarSizeValueWidthFor(IRenderContext* ctx) const {
        if (barSizeValueWidth > 0) return barSizeValueWidth;
        // Auto: the widest value FormatSizeFixed() can produce, so every bar
        // ends at the same x no matter how wide the individual number is. It
        // is remembered because a splitter drag has no render context to
        // measure it with.
        barSizeAutoValueWidth = ctx->GetTextLineDimensions("1023.9 MB").width;
        return barSizeAutoValueWidth;
    }

    // Name column | bar | value column. The name column is what the first
    // splitter drags; the value column is fixed-width (auto or dragged) and
    // right-aligned, and the bar takes what is left between them.
    UltraCanvasFilerWidget::BarSizeColumns
    UltraCanvasFilerWidget::BarSizeColumnsFor(const ItemLayout& item,
                                              int autoValueWidth) const {
        const int rightPad = 14;
        const int labelGap = 8;
        BarSizeColumns c;
        int iconRight = item.imageRect.x + item.imageRect.width;
        c.nameX = iconRight + 6;
        int nameSpan = iconRight - item.rect.x + 6;   // icon + gaps before the name

        int valueW = std::max(kMinColumnWidth, autoValueWidth);
        // Keep name + bar + value inside the row even on a narrow widget.
        int available = std::max(0, item.rect.width - nameSpan - rightPad - labelGap);
        valueW = std::min(valueW, std::max(0, available - kMinBarWidth - kMinColumnWidth));
        int nameW = clampi(barSizeNameWidth, kMinColumnWidth,
                           std::max(kMinColumnWidth, available - valueW - kMinBarWidth));

        c.nameWidth = nameW;
        c.valueWidth = std::max(0, valueW);
        c.valueX = item.rect.x + item.rect.width - rightPad - c.valueWidth;
        c.barX = c.nameX + nameW;
        c.barWidth = c.valueX - labelGap - c.barX;
        return c;
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
        ScrollEntryIntoView(entryIndex);
    }

    void UltraCanvasFilerWidget::EnsureSelectionVisible() {
        if (selection.empty()) return;
        // Deferred to the next EnsureLayout() (every Render runs one): when
        // the widget is being resized in the same frame — the UltraFiler
        // preview pane opening narrows the folder display — the reveal then
        // uses the new geometry instead of the stale one.
        pendingRevealEntry = static_cast<int>(selection.front());
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::ScrollEntryIntoView(size_t entryIndex) {
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
        // Tile heights depend on measured names, so hand the layout the context
        // of this pass; a layout that ran without one is redone here.
        measureContext = ctx;
        if (!captionLinesMeasured) InvalidateFilerLayout();
        EnsureLayout();
        measureContext = nullptr;

        auto lb = GetLocalBounds();
        Rect2Di bounds(static_cast<int>(lb.x), static_cast<int>(lb.y),
                       static_cast<int>(lb.width), static_cast<int>(lb.height));

        DrawViewContent(ctx, bounds);

        // The inline rename editor is a child element the self-rendered view
        // must draw itself (the widget's Render never paints children). Placed
        // fresh each frame so it tracks scrolling and relayouts of its item.
        if (renamingIndex >= 0 && renameInput) {
            PositionRenameInput();
            Rect2Df b = renameInput->GetBounds();
            ctx->PushState();
            ctx->ClipRect(Rect2Dd(lb.x, lb.y, lb.width, lb.height));
            ctx->Translate(Point2Df(b.x, b.y));
            renameInput->Render(ctx, Rect2Df(dirtyRect.x - b.x, dirtyRect.y - b.y,
                                             dirtyRect.width, dirtyRect.height));
            ctx->PopState();
        }

        // Modal overlay painted last so it sits above the folder view and chrome.
        if (compressDlg.active) DrawCompressDialog(ctx, bounds);
    }

    void UltraCanvasFilerWidget::DrawViewContent(IRenderContext* ctx,
                                                 const Rect2Di& bounds) {
        ctx->PushState();
        ctx->ClipRect(Rect2Dd(bounds));
        ctx->SetFillPaint(style.backgroundColor);
        ctx->FillRectangle(Rect2Dd(bounds));

        iconMenuHits.clear();
        columnSplitters.clear();
        // Refreshed per drawn item; only the items the last frame painted can
        // be hovered, so their flags are always the current ones.
        if (nameTruncated.size() != entries.size())
            nameTruncated.assign(entries.size(), 0);
        // Selection membership per entry for this paint (see frameSelected).
        frameSelected.assign(entries.size(), 0);
        for (size_t idx : selection)
            if (idx < frameSelected.size()) frameSelected[idx] = 1;

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
        // Except for the treemap, `items` is ordered along the scroll axis
        // (rows top-down, List columns left-to-right), so the loop can stop at
        // the first item past the viewport instead of testing every entry of
        // a large folder each frame.
        const bool xOrdered = IsHorizontal();
        const bool ordered = viewType != FilerViewType::TreeMap;
        for (const ItemLayout& item : items) {
            int top = item.rect.y - scrollOffsetY;
            int left = item.rect.x - scrollOffsetX;
            if (ordered) {
                if (!xOrdered && top > bounds.y + bounds.height) break;
                if (xOrdered && left > bounds.x + bounds.width) break;
            }
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
            // Ghost entries that are pending a "cut": wash the tile toward the
            // background so it reads as dimmed until the move is pasted.
            if (IsCutEntry(entries[item.entryIndex])) {
                Color wash = style.backgroundColor;
                wash.a = 150;
                ctx->SetFillPaint(wash);
                ctx->FillRoundedRectangle(Rect2Dd(item.rect), 5);
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
        ctx->PopState();

        PrefetchThumbnails(ctx, bounds);
        CommitThumbnailWants();

        if (viewType == FilerViewType::Details) DrawDetailsHeader(ctx, bounds);
        DrawColumnSplitters(ctx, bounds);
        DrawScrollbar(ctx);
        DrawSelectionInfoBar(ctx, bounds);
        // Drag badge + drop highlight above the whole view (including chrome).
        if (draggingItems) DrawDragFeedback(ctx, bounds);
        // Rubber-band rectangle of a running drag selection.
        if (marqueeActive) DrawMarquee(ctx);
        ctx->PopState();
    }

    void UltraCanvasFilerWidget::DrawDragFeedback(IRenderContext* ctx,
                                                  const Rect2Di& bounds) {
        // The folder the drop would land in, framed like a selected item.
        if (dragDropFolderIndex >= 0) {
            for (const ItemLayout& it : items) {
                if (static_cast<int>(it.entryIndex) != dragDropFolderIndex) continue;
                Rect2Di r(it.rect.x - scrollOffsetX, it.rect.y - scrollOffsetY,
                          it.rect.width, it.rect.height);
                Color fillc = style.selectionColor; fillc.a = 130;
                ctx->SetFillPaint(fillc);
                ctx->FillRoundedRectangle(Rect2Dd(r), 6);
                ctx->SetStrokePaint(style.selectionBorderColor);
                ctx->SetStrokeWidth(2.5f);
                ctx->DrawRoundedRectangle(Rect2Dd(r), 6);
                break;
            }
        }

        // Badge following the cursor: the dragged entry's icon plus its name
        // (or the item count), so it is always visible what is being carried.
        FontStyle fsty;
        fsty.fontFamily = style.fontFamily;
        fsty.fontSize = style.smallFontSize;
        ctx->SetFontStyle(fsty);
        std::string label = EllipsizeText(ctx, dragLabel, 220);
        Size2Di ts = ctx->GetTextLineDimensions(label);

        const int iconSz = 20, padX = 8, padY = 6, gap = 6;
        int bw = padX * 2 + iconSz + gap + ts.width;
        int bh = std::max(iconSz, ts.height) + padY * 2;
        int bx = dragPos.x + 14;
        int by = dragPos.y + 14;
        // Keep the badge inside the widget so it never paints over neighbours.
        if (bx + bw > bounds.x + bounds.width)  bx = bounds.x + bounds.width - bw;
        if (by + bh > bounds.y + bounds.height) by = dragPos.y - bh - 6;
        if (bx < bounds.x) bx = bounds.x;
        if (by < bounds.y) by = bounds.y;
        Rect2Di badge(bx, by, bw, bh);

        Color back = style.backgroundColor; back.a = 235;
        ctx->SetFillPaint(back);
        ctx->FillRoundedRectangle(Rect2Dd(badge), 5);
        ctx->SetStrokePaint(style.selectionBorderColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRoundedRectangle(Rect2Dd(badge), 5);

        DrawEntryIcon(ctx, dragLeadEntry,
                      Rect2Di(bx + padX, by + (bh - iconSz) / 2, iconSz, iconSz));
        ctx->SetFontStyle(fsty);
        ctx->SetTextPaint(style.textColor);
        ctx->DrawText(label, Point2Dd(bx + padX + iconSz + gap,
                                      by + (bh - ts.height) / 2.0));
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

    namespace {
        // Byte offset of every UTF-8 code point start in `s`, plus s.size() as
        // the closing boundary: cutting on one never splits a multibyte
        // sequence. Index i of the result addresses the prefix s[0, b[i]) and
        // the suffix s[b[i], end).
        std::vector<size_t> Utf8Boundaries(const std::string& s) {
            std::vector<size_t> b;
            b.reserve(s.size() + 1);
            for (size_t i = 0; i < s.size(); ++i) {
                if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) b.push_back(i);
            }
            b.push_back(s.size());
            return b;
        }

        // Break the line after one of these when it sits in the back half of
        // what fits: a name reads much better broken at its own separators
        // ("Holiday photos - Rome.jpg") than in the middle of a word.
        bool IsNameBreakChar(char c) {
            return c == ' ' || c == '-' || c == '_' || c == '.' || c == ',' ||
                   c == ';' || c == '(' || c == ')' || c == '[' || c == ']';
        }
    }

    std::string UltraCanvasFilerWidget::EllipsizeText(IRenderContext* ctx,
                                                      const std::string& text,
                                                      int maxWidth) const {
        if (maxWidth <= 0) return "";
        Size2Di ts = ctx->GetTextLineDimensions(text);
        if (ts.width <= maxWidth) return text;
        // Longest prefix that fits with the trailing "…": binary search over
        // the code-point boundaries (fit is monotone in prefix length). The
        // one-code-point-at-a-time trim measured the text once per removed
        // character — a long name in a narrow Details column cost hundreds of
        // text measurements per cell, every frame.
        std::vector<size_t> bounds = Utf8Boundaries(text);
        size_t lo = 0, hi = bounds.size() - 1;   // prefix is text[0, bounds[i])
        while (lo < hi) {
            size_t mid = (lo + hi + 1) / 2;
            if (ctx->GetTextLineDimensions(text.substr(0, bounds[mid]) + "…").width
                    <= maxWidth)
                lo = mid;
            else
                hi = mid - 1;
        }
        if (lo == 0) return "…";
        return text.substr(0, bounds[lo]) + "…";
    }

    std::string UltraCanvasFilerWidget::EllipsizeEntryName(IRenderContext* ctx,
                                                           size_t entryIndex,
                                                           const std::string& name,
                                                           int maxWidth) {
        std::string shown = EllipsizeText(ctx, name, maxWidth);
        if (entryIndex < nameTruncated.size())
            nameTruncated[entryIndex] = (shown != name) ? 1 : 0;
        return shown;
    }

    // ===== WRAPPED CAPTIONS =====
    // Tile captions (thumbnail grids, treemap cells) are only as wide as the
    // tile, which is far less than a file name often needs. Instead of cutting
    // the name off after one line it is broken over up to captionMaxLines
    // lines; only what does not fit even then is dropped, from the front of the
    // last line, so the tail — the extension — always remains readable.

    std::vector<std::string> UltraCanvasFilerWidget::WrapText(
            IRenderContext* ctx, const std::string& text,
            int maxWidth, int maxLines, bool* outTruncated) const {
        if (outTruncated) *outTruncated = false;
        std::vector<std::string> lines;
        if (!ctx || maxWidth <= 0 || text.empty()) return lines;
        if (maxLines < 1) maxLines = 1;

        if (ctx->GetTextLineDimensions(text).width <= maxWidth) {
            lines.push_back(text);
            return lines;                       // the common case: one measure
        }
        if (maxLines == 1) {
            lines.push_back(EllipsizeText(ctx, text, maxWidth));
            if (outTruncated) *outTruncated = true;
            return lines;
        }

        std::string rest = text;
        for (int line = 0; line < maxLines && !rest.empty(); ++line) {
            std::vector<size_t> bounds = Utf8Boundaries(rest);
            const bool lastLine = (line == maxLines - 1);

            if (lastLine) {
                if (ctx->GetTextLineDimensions(rest).width <= maxWidth) {
                    lines.push_back(rest);
                    break;
                }
                // Longest tail that fits behind a leading "…" (the shorter the
                // tail the narrower the line, so the fit is monotone in `lo`).
                size_t lo = 0, hi = bounds.size() - 1;
                while (lo < hi) {
                    size_t mid = (lo + hi) / 2;
                    std::string cand = "…" + rest.substr(bounds[mid]);
                    if (ctx->GetTextLineDimensions(cand).width <= maxWidth) hi = mid;
                    else lo = mid + 1;
                }
                lines.push_back("…" + rest.substr(bounds[lo]));
                if (outTruncated) *outTruncated = true;
                break;
            }

            // Longest prefix that still fits this line.
            size_t lo = 0, hi = bounds.size() - 1;
            while (lo < hi) {
                size_t mid = (lo + hi + 1) / 2;
                if (ctx->GetTextLineDimensions(rest.substr(0, bounds[mid])).width <= maxWidth)
                    lo = mid;
                else
                    hi = mid - 1;
            }
            size_t fit = bounds[lo];
            // Not even one code point fits: take one anyway so the loop always
            // makes progress (a caption this narrow is unreadable regardless).
            if (fit == 0) fit = bounds.size() > 1 ? bounds[1] : rest.size();

            // The exact fit is kept when it already ends on a word boundary;
            // otherwise the line backs off to the last separator inside it —
            // unless that would leave more than half the line empty.
            size_t cut = fit;
            if (fit < rest.size() && !IsNameBreakChar(rest[fit])) {
                for (size_t i = fit; i > 0; --i) {
                    if (!IsNameBreakChar(rest[i - 1])) continue;
                    if (i * 2 >= fit) cut = i;
                    break;
                }
            }

            std::string head = rest.substr(0, cut);
            rest.erase(0, cut);
            while (!head.empty() && head.back() == ' ') head.pop_back();
            while (!rest.empty() && rest.front() == ' ') rest.erase(0, 1);
            if (!head.empty()) lines.push_back(head);
        }
        return lines;
    }

    std::vector<std::string> UltraCanvasFilerWidget::WrapEntryName(
            IRenderContext* ctx, size_t entryIndex, const std::string& name,
            int maxWidth, int maxLines) {
        bool truncated = false;
        std::vector<std::string> lines = WrapText(ctx, name, maxWidth, maxLines,
                                                  &truncated);
        if (entryIndex < nameTruncated.size())
            nameTruncated[entryIndex] = truncated ? 1 : 0;
        return lines;
    }

    int UltraCanvasFilerWidget::CaptionLinesFor(IRenderContext* ctx,
                                                const std::string& name,
                                                int maxWidth) const {
        int maxLines = std::max(1, style.captionMaxLines);
        if (!ctx || maxLines == 1 || maxWidth <= 0) return 1;
        int n = static_cast<int>(WrapText(ctx, name, maxWidth, maxLines).size());
        return clampi(n, 1, maxLines);
    }

    int UltraCanvasFilerWidget::NameLineHeight() const {
        return style.captionLineHeight > 0 ? style.captionLineHeight
                                           : static_cast<int>(style.smallFontSize) + 3;
    }

    int UltraCanvasFilerWidget::CaptionBandHeight(int lines) const {
        return style.captionHeight + (std::max(1, lines) - 1) * NameLineHeight();
    }

    void UltraCanvasFilerWidget::DrawSelectionState(IRenderContext* ctx,
                                                    const ItemLayout& item,
                                                    bool hovered) {
        bool selected = item.entryIndex < frameSelected.size() &&
                        frameSelected[item.entryIndex];
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
        if ((e.category == FilerFileCategory::Image ||
             e.category == FilerFileCategory::Vector) &&
            ImagePipelineLoadsExtension(e.extension)) {
            return e.path;
        }
        // Videos thumbnail as their poster frame (the first frame of the
        // clip), decoded by the same background workers. Without a video
        // backend the capture fails once, the slot is marked Failed and the
        // tile keeps its generic glyph.
        if (e.category == FilerFileCategory::Video) return e.path;
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
            // Items are viewport-ordered (the treemap, which is not, returned
            // above): past the prefetch band nothing further is wanted.
            if (lead > viewEnd + band) break;
            // Items overlapping the viewport were already requested by their
            // draw call; take only the next viewport-sized band past it.
            if (lead <= viewEnd) continue;
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

    void UltraCanvasFilerWidget::SetShrinkThumbnailRows(bool enabled) {
        if (shrinkThumbnailRows == enabled) return;
        shrinkThumbnailRows = enabled;
        // Only the thumbnail grid layout depends on this; recompute it (and its
        // decode geometry) on the next paint.
        InvalidateFilerLayout();
        RequestRedraw();
    }

    float UltraCanvasFilerWidget::EntryAspect(const FilerEntry& e) {
        // Only raster images have a meaningful, cheaply-probeable pixel size.
        // Vectors scale to fill the tile, everything else draws a glyph.
        if (e.category != FilerFileCategory::Image) return 0.0f;
        std::lock_guard<std::mutex> lk(statsMutex);
        auto it = aspectCache.find(e.path);
        if (it != aspectCache.end()) return it->second;
        // Not probed yet. The layout asks this for every entry of the folder,
        // so the header read goes to the background worker and the row keeps
        // the full tile height until the answer lands (which the layout
        // already treats as the "not yet measured" case). The 0 stored here
        // doubles as the marker that the probe is queued.
        aspectCache.emplace(e.path, 0.0f);
        aspectQueue.push_back(e.path);
        StartFolderStatsWorkerLocked();
        statsCond.notify_one();
        return 0.0f;
    }

    int UltraCanvasFilerWidget::ThumbnailImageHeight(const FilerEntry& e, int edge) {
        if (!shrinkThumbnailRows || edge <= 0) return edge;
        float aspect = EntryAspect(e);
        if (aspect <= 1.0f) return edge;   // unknown / portrait / square: fill
        // The image is drawn ScaleDown-fit inside a symmetric inset box (see
        // ThumbGeometryForItem). Predict its displayed height for that same box
        // and re-add the inset so the tile keeps equal padding all around, the
        // way a portrait tile does.
        int inset = std::max(4, edge / 12);
        int box = edge - 2 * inset;
        if (box <= 0) return edge;
        int shown = static_cast<int>(std::lround(box / aspect)) + 2 * inset;
        // Never grow past the square tile, and keep at least the inset padding.
        return std::max(2 * inset + 1, std::min(edge, shown));
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
            if (IsVideoFilePath(req.path)) {
                // Poster frame of a video (may block for a few seconds on a
                // cold file — that is exactly what these workers are for).
                VideoThumbnailRequest vreq;
                vreq.maxWidth = std::max(
                        1, static_cast<int>(std::lround(req.w * req.scale)));
                vreq.maxHeight = std::max(
                        1, static_cast<int>(std::lround(req.h * req.scale)));
                pm = CaptureVideoThumbnailPixmap(req.path, vreq);
            } else {
                auto img = UCImage::Get(req.path);
                if (img && img->GetWidth() > 0 && img->GetHeight() > 0) {
                    pm = img->GetPixmap(req.w, req.h, req.fit, req.scale);
                }
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
            if (c.sortable && c.field == sortField) {
                title += sortAscending ? " ▲" : " ▼";
            }
            title = EllipsizeText(ctx, title, c.width - 12);
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
            Color color = style.secondaryTextColor;
            switch (c.id) {
                case FilerDetailsColumn::Name:
                    value = e.name;
                    color = style.textColor;
                    break;
                case FilerDetailsColumn::Path:
                    value = fs::path(e.path).parent_path().string();
                    break;
                case FilerDetailsColumn::Size:
                    value = e.isDirectory ? "" : FormatSize(e.size);
                    break;
                case FilerDetailsColumn::Type:
                    value = e.typeName;
                    break;
                case FilerDetailsColumn::ModifiedDate:
                    value = FormatTime(e.modifiedTime);
                    break;
                case FilerDetailsColumn::CreatedDate:
                    value = FormatTime(e.createdTime);
                    break;
                case FilerDetailsColumn::Attributes:
                    value = e.attributes;
                    break;
                case FilerDetailsColumn::Info:
                    value = e.info;
                    break;
            }
            if (value.empty()) continue;

            int pad = 6;
            int textX = c.x + pad;
            int avail = c.width - 2 * pad;
            if (c.id == FilerDetailsColumn::Name) {
                textX = item.imageRect.x + item.imageRect.width + 6;
                avail = c.x + c.width - textX - pad;
            }
            std::string shown = (c.id == FilerDetailsColumn::Name)
                    ? EllipsizeEntryName(ctx, item.entryIndex, value, avail)
                    : EllipsizeText(ctx, value, avail);
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
        std::string shown = EllipsizeEntryName(ctx, item.entryIndex, e.name, avail);
        Size2Di ts = ctx->GetTextLineDimensions(shown);
        ctx->DrawText(shown, Point2Dd(textX,
                item.rect.y + (item.rect.height - ts.height) / 2));
    }

    void UltraCanvasFilerWidget::DrawThumbnailTile(IRenderContext* ctx,
                                                   const ItemLayout& item, bool hovered) {
        const FilerEntry& e = entries[item.entryIndex];
        bool selected = item.entryIndex < frameSelected.size() &&
                        frameSelected[item.entryIndex];
        if (selected || hovered) {
            ctx->SetFillPaint(selected ? style.selectionColor : style.hoverColor);
            ctx->FillRoundedRectangle(Rect2Dd(item.rect), 5);
        }
        // Same geometry the prefetch requests, so the cache keys line up.
        Rect2Di img;
        ImageFitMode fit;
        ThumbGeometryForItem(item, img, fit);
        if (e.isDirectory && style.folderIconScale < 1.0f) {
            // Shrink the folder glyph inside its image box, centered.
            int w = std::max(2, (int)(img.width * style.folderIconScale));
            int h = std::max(2, (int)(img.height * style.folderIconScale));
            img = Rect2Di(img.x + (img.width - w) / 2,
                          img.y + (img.height - h) / 2, w, h);
        }
        DrawEntryIcon(ctx, e, img, fit);

        FontStyle fsty;
        fsty.fontFamily = style.fontFamily;
        fsty.fontSize = style.smallFontSize;
        ctx->SetFontStyle(fsty);
        ctx->SetTextPaint(style.textColor);
        // The name wraps over the lines the row reserved for it; a name too
        // long even for those keeps its tail behind a leading "…".
        int capTop = item.imageRect.y + item.imageRect.height;
        int capH = CaptionBandHeight(item.captionLines);
        int nameLineH = NameLineHeight();
        std::vector<std::string> nameLines = WrapEntryName(
                ctx, item.entryIndex, e.name, item.rect.width - 8,
                std::max(1, item.captionLines));
        double ny = capTop + (capH - static_cast<int>(nameLines.size()) * nameLineH) / 2.0;
        for (const std::string& ln : nameLines) {
            Size2Di ts = ctx->GetTextLineDimensions(ln);
            ctx->DrawText(ln, Point2Dd(
                    item.rect.x + (item.rect.width - ts.width) / 2.0,
                    ny + (nameLineH - ts.height) / 2.0));
            ny += nameLineH;
        }

        // Dataset lines (Display > Dataset) under the name, smaller and greyed.
        if (datasetFields != 0) {
            std::vector<std::string> lines = DatasetLinesFor(e);
            if (!lines.empty()) {
                ctx->SetTextPaint(style.secondaryTextColor);
                int lineH = DatasetLineHeight();
                int y = capTop + capH;
                for (const std::string& raw : lines) {
                    std::string ln = EllipsizeText(ctx, raw, item.rect.width - 8);
                    Size2Di lts = ctx->GetTextLineDimensions(ln);
                    ctx->DrawText(ln, Point2Dd(
                            item.rect.x + (item.rect.width - lts.width) / 2.0,
                            y + (lineH - lts.height) / 2.0));
                    y += lineH;
                }
            }
        }

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

        // Size bar scaled against the folder's largest entry. The size label
        // lives in a fixed-width column on the right so every bar ends at the
        // same x and all bars share the same width, regardless of how wide the
        // individual number happens to be. Both column widths are draggable
        // (see DrawColumnSplitters); "auto" sizes the label column for the
        // widest value we can format ("NNN.N UU").
        BarSizeColumns cols = BarSizeColumnsFor(item, BarSizeValueWidthFor(ctx));

        std::string shown = EllipsizeEntryName(ctx, item.entryIndex, e.name,
                                               cols.nameWidth - 8);
        ctx->SetTextPaint(style.textColor);
        Size2Di ts = ctx->GetTextLineDimensions(shown);
        int textY = item.rect.y + (item.rect.height - ts.height) / 2;
        ctx->DrawText(shown, Point2Dd(cols.nameX, textY));

        std::string sizeText = EllipsizeText(ctx, FormatSizeFixed(e.effectiveSize),
                                             cols.valueWidth);
        Size2Di sts = ctx->GetTextLineDimensions(sizeText);
        int sizeColW = cols.valueWidth;
        int sizeColX = cols.valueX;
        int barX = cols.barX;
        int barMaxW = cols.barWidth;
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
            // Right-align the number within its fixed-width column.
            ctx->SetTextPaint(style.secondaryTextColor);
            ctx->DrawText(sizeText,
                          Point2Dd(sizeColX + (sizeColW - sts.width), textY));
        }
    }

    void UltraCanvasFilerWidget::DrawTreeMapCell(IRenderContext* ctx,
                                                 const ItemLayout& item, bool hovered) {
        const FilerEntry& e = entries[item.entryIndex];
        bool selected = item.entryIndex < frameSelected.size() &&
                        frameSelected[item.entryIndex];
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

        if (item.rect.width < 46 || item.rect.height < 26) {
            // The cell is too small for a caption: the name is not shown at
            // all, so the hover tooltip is the only way to read it.
            if (item.entryIndex < nameTruncated.size())
                nameTruncated[item.entryIndex] = 1;
        } else {
            ctx->PushState();
            ctx->ClipRect(Rect2Dd(item.rect));
            FontStyle fsty;
            fsty.fontFamily = style.fontFamily;
            fsty.fontSize = style.smallFontSize;
            fsty.fontWeight = FontWeight::Bold;
            ctx->SetFontStyle(fsty);
            ctx->SetTextPaint(Color(255, 255, 255, 235));
            // The name wraps into whatever the cell has room for above the size
            // line (cells are sized by the treemap, not by the caption).
            int lineH = NameLineHeight();
            bool showSize = item.rect.height >= 42;
            int nameRoom = item.rect.height - 6 - (showSize ? lineH : 0);
            int maxLines = clampi(nameRoom / std::max(1, lineH), 1,
                                  std::max(1, style.captionMaxLines));
            std::vector<std::string> nameLines = WrapEntryName(
                    ctx, item.entryIndex, e.name, item.rect.width - 8, maxLines);
            int ny = item.rect.y + 3;
            for (const std::string& ln : nameLines) {
                ctx->DrawText(ln, Point2Dd(item.rect.x + 4, ny));
                ny += lineH;
            }
            if (showSize) {
                fsty.fontWeight = FontWeight::Normal;
                ctx->SetFontStyle(fsty);
                ctx->SetTextPaint(Color(255, 255, 255, 190));
                ctx->DrawText(FormatSize(e.effectiveSize),
                              Point2Dd(item.rect.x + 4, ny + 1));
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
        // Right-align the strip inside the item, but never let it spill past the
        // item's left edge: on narrow thumbnail tiles the four buttons are wider
        // than the tile, so clamp the start to the left edge instead.
        int x = item.rect.x + item.rect.width - total - 4;
        if (x < item.rect.x) x = item.rect.x;
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

    void UltraCanvasFilerWidget::DrawMarquee(IRenderContext* ctx) {
        Rect2Di r = MarqueeRect();
        Rect2Dd lr(r.x - scrollOffsetX, r.y - scrollOffsetY, r.width, r.height);
        Color fill = style.selectionColor;
        fill.a = 70;
        ctx->SetFillPaint(fill);
        ctx->FillRectangle(lr);
        ctx->SetStrokePaint(style.selectionBorderColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRectangle(lr);
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

    // ===== COLUMN SPLITTERS =====
    // The column views get UltraCanvasSplitPane dividers between their
    // columns: the same look (FilerStyle::columnSplitter is a SplitPaneStyle),
    // the same drag feel (a press snapshots the two neighbouring widths and
    // the move re-splits that pair) and the same SizeWE cursor. They are drawn
    // — and their hit strips recorded — here rather than being child elements,
    // because the filer paints its whole view itself (Album pattern).
    //
    // Where the strip sits differs per view: the Details table has a header,
    // so its splitters live there (Explorer style) and the rows stay fully
    // clickable; List and BarSize have no header, so their dividers run the
    // full height of the entries, which is also the only place they are
    // visible.
    void UltraCanvasFilerWidget::DrawColumnSplitters(IRenderContext* ctx,
                                                     const Rect2Di& bounds) {
        if (!columnResizeEnabled) return;
        Rect2Di area = ContentBounds();
        if (area.width <= 0 || area.height <= 0) return;

        int thickness = std::max(1, style.columnSplitter.splitterThickness);
        int viewBottom = std::min(bounds.y + bounds.height - InfoBarHeight(),
                                  area.y + area.height);

        // Strip geometry per view: (x of the divider centre, top, bottom).
        struct Strip { int centerX, top, bottom; };
        std::vector<Strip> strips;

        switch (viewType) {
            case FilerViewType::Details: {
                // One splitter on the right edge of every column but the last;
                // inside the header strip only.
                for (size_t i = 0; i + 1 < detailsColumns.size(); ++i) {
                    const DetailsColumn& c = detailsColumns[i];
                    strips.push_back({c.x + c.width, area.y,
                                      area.y + detailsHeaderHeight});
                }
                break;
            }
            case FilerViewType::List: {
                // The list columns are uniform, so every gap carries a
                // splitter and dragging any of them re-widths all of them.
                int colW = std::max(kMinListColumnWidth, style.listColumnWidth);
                // Items run column by column, so the last one is in the last
                // column.
                int cols = items.empty() ? 0
                        : (items.back().rect.x - area.x) / (colW + kListColumnGap) + 1;
                for (int k = 0; k < cols; ++k) {
                    int right = area.x + k * kListColumnGap + (k + 1) * colW;
                    strips.push_back({right + kListColumnGap / 2 - scrollOffsetX,
                                      area.y, viewBottom});
                }
                break;
            }
            case FilerViewType::BarSize: {
                if (items.empty()) break;
                BarSizeColumns cols = BarSizeColumnsFor(items.front(),
                                                        BarSizeValueWidthFor(ctx));
                strips.push_back({cols.barX - 3, area.y, viewBottom});
                strips.push_back({cols.valueX - 4, area.y, viewBottom});
                break;
            }
            default:
                return;   // the other views have no columns to resize
        }

        for (size_t i = 0; i < strips.size(); ++i) {
            const Strip& s = strips[i];
            Rect2Di rect(s.centerX - thickness / 2, s.top, thickness,
                         std::max(0, s.bottom - s.top));
            if (rect.height <= 0) continue;
            // Skip dividers scrolled out of the view (List scrolls sideways).
            if (rect.x + rect.width < bounds.x ||
                rect.x > bounds.x + bounds.width - kScrollbarGutter) {
                continue;
            }
            columnSplitters.push_back({rect, static_cast<int>(i)});

            if (!style.columnSplitter.showSplitterBackground) continue;
            bool dragging = (draggingSplitter == static_cast<int>(i));
            Color fill = style.columnSplitter.splitterColor;
            if (dragging) fill = style.columnSplitter.splitterActiveColor;
            else if (hoveredSplitter == static_cast<int>(i))
                fill = style.columnSplitter.splitterHoverColor;
            ctx->SetFillPaint(fill);
            ctx->FillRectangle(Rect2Dd(rect));

            // While dragging, a guide line down the whole view shows where the
            // boundary will land — the Details splitters only live in the
            // header, so without it the drag would have no visible effect
            // until it is released.
            if (dragging && rect.y + rect.height < viewBottom) {
                Color guide = style.columnSplitter.splitterActiveColor;
                guide.a = 120;
                ctx->SetFillPaint(guide);
                ctx->FillRectangle(Rect2Dd(rect.x, rect.y + rect.height,
                                           rect.width, viewBottom - rect.y - rect.height));
            }
        }
    }

    // Returns the splitter's own index (the column it belongs to), not its
    // position in columnSplitters — that vector only holds the strips the last
    // frame painted, and a drag must survive one scrolling out of view.
    int UltraCanvasFilerWidget::ColumnSplitterAt(const Point2Di& localPoint) const {
        if (!columnResizeEnabled) return -1;
        int margin = std::max(0, style.columnSplitter.splitterHitMargin);
        for (const ColumnSplitterHit& h : columnSplitters) {
            Rect2Di grab = h.rect;
            grab.x -= margin;
            grab.width += 2 * margin;
            if (grab.Contains(localPoint)) return h.index;
        }
        return -1;
    }

    void UltraCanvasFilerWidget::BeginColumnSplitterDrag(int index,
                                                         const Point2Di& localPoint) {
        if (index < 0) return;
        draggingSplitter = index;
        splitterDragStartX = localPoint.x;
        splitterDragStartA = 0;
        splitterDragStartB = 0;

        switch (viewType) {
            case FilerViewType::Details: {
                EnsureDetailsColumnWidths();
                // The splitter index counts visible columns; the widths stay
                // indexed by FilerDetailsColumn (Path may be hidden).
                const std::vector<size_t> vis = VisibleDetailsSpecIndices();
                if (index + 1 >= (int)vis.size()) {
                    draggingSplitter = -1;
                    return;
                }
                splitterDragStartA = detailsColumnWidths[vis[index]];
                splitterDragStartB = detailsColumnWidths[vis[index + 1]];
                break;
            }
            case FilerViewType::List:
                splitterDragStartA = std::max(kMinListColumnWidth, style.listColumnWidth);
                break;
            case FilerViewType::BarSize: {
                if (items.empty()) { draggingSplitter = -1; return; }
                // The value column may be on auto: snapshot the width it is
                // actually drawn with, so the drag continues from there.
                BarSizeColumns cols = BarSizeColumnsFor(
                        items.front(), barSizeValueWidth > 0 ? barSizeValueWidth
                                                             : barSizeAutoValueWidth);
                splitterDragStartA = cols.nameWidth;
                splitterDragStartB = cols.valueWidth;
                break;
            }
            default:
                draggingSplitter = -1;
                return;
        }
        if (auto* app = UltraCanvasApplication::GetInstance()) app->CaptureMouse(this);
        SetMouseCursor(UCMouseCursor::SizeWE);
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::UpdateColumnSplitterDrag(const Point2Di& localPoint) {
        if (draggingSplitter < 0) return;
        int index = draggingSplitter;
        int delta = localPoint.x - splitterDragStartX;

        switch (viewType) {
            case FilerViewType::Details: {
                // Split-pane semantics: the width the left column gains is
                // taken from the column on the right, so the table keeps
                // spanning the widget. The Name column is derived from what
                // the others leave, which makes dragging the first splitter
                // resize Name by the same amount in the opposite direction.
                const std::vector<size_t> vis = VisibleDetailsSpecIndices();
                if (index + 1 >= (int)vis.size()) return;
                int pairTotal = splitterDragStartA + splitterDragStartB;
                int minLeft = (index == 0) ? kMinNameColumnWidth : kMinColumnWidth;
                int newLeft = clampi(splitterDragStartA + delta, minLeft,
                                     std::max(minLeft, pairTotal - kMinColumnWidth));
                detailsColumnWidths[vis[index]] = newLeft;
                detailsColumnWidths[vis[index + 1]] = pairTotal - newLeft;
                break;
            }
            case FilerViewType::List: {
                // Boundary k sits after k+1 columns and k gaps, so the pointer
                // position divides by the number of columns before it.
                int colsBefore = index + 1;
                int width = splitterDragStartA + delta / colsBefore;
                style.listColumnWidth = std::max(kMinListColumnWidth, width);
                break;
            }
            case FilerViewType::BarSize: {
                if (index == 0) {
                    barSizeNameWidth = std::max(kMinColumnWidth,
                                                splitterDragStartA + delta);
                } else {
                    // Dragging the value splitter right shrinks the label
                    // column: its right edge is pinned to the row's edge.
                    int rowWidth = items.empty() ? 0 : items.front().rect.width;
                    barSizeValueWidth = clampi(splitterDragStartB - delta,
                                               kMinColumnWidth,
                                               std::max(kMinColumnWidth, rowWidth / 2));
                }
                break;
            }
            default:
                return;
        }
        InvalidateFilerLayout();
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::EndColumnSplitterDrag() {
        if (draggingSplitter < 0) return;
        draggingSplitter = -1;
        if (auto* app = UltraCanvasApplication::GetInstance()) app->ReleaseMouse();
        NotifyColumnWidthsChanged();
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::NotifyColumnWidthsChanged() {
        if (onColumnWidthsChanged) onColumnWidthsChanged();
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
            aspectQueue.clear();
            mediaQueue.clear();
        }
        statsCond.notify_all();
        if (statsWorker.joinable()) statsWorker.join();
    }

    void UltraCanvasFilerWidget::FolderStatsWorkerMain() {
        for (;;) {
            std::string path;
            std::string aspectPath;
            MediaProbeRequest media;
            bool haveMedia = false;
            uint64_t gen;
            {
                std::unique_lock<std::mutex> lk(statsMutex);
                statsCond.wait(lk, [this]() {
                    return statsShutdown || !statsQueue.empty() ||
                           !aspectQueue.empty() || !mediaQueue.empty();
                });
                if (statsShutdown) return;
                // Shortest jobs first: aspect probes (one header read) settle
                // the grid geometry the user is looking at, media probes are a
                // few reads for the info bar / dataset lines, and a recursive
                // folder walk can run for seconds.
                if (!aspectQueue.empty()) {
                    aspectPath = std::move(aspectQueue.front());
                    aspectQueue.pop_front();
                } else if (!mediaQueue.empty()) {
                    media = std::move(mediaQueue.front());
                    mediaQueue.pop_front();
                    haveMedia = true;
                } else {
                    path = std::move(statsQueue.front());
                    statsQueue.pop_front();
                }
                gen = statsGeneration;
            }

            if (haveMedia) {
                std::string out;
                if (media.isImage) {
                    int w = 0, h = 0;
                    if (!ProbeImageDimensions(media.path, w, h)) {
                        // Unknown container (AVIF, HEIC, ...): ask the shared
                        // image cache — same call the thumbnail workers make,
                        // so a later tile decode is a free cache hit.
                        auto img = UCImage::Get(media.path);
                        if (img) { w = img->GetWidth(); h = img->GetHeight(); }
                    }
                    if (w > 0 && h > 0)
                        out = std::to_string(w) + " × " + std::to_string(h) + " px";
                } else {
                    FilerMediaProbe probe;
                    if (ProbeMediaFile(media.path, media.extension, probe)) {
                        out = FormatDuration(probe.seconds);
                        if (!probe.codec.empty()) {
                            if (!out.empty()) out += " · ";
                            out += probe.codec;
                        }
                    }
                }
                bool report = false;
                {
                    std::lock_guard<std::mutex> lk(statsMutex);
                    if (statsShutdown) return;
                    if (gen == statsGeneration) {
                        MediaInfoSlot& slot = mediaInfoCache[media.path];
                        slot.text = std::move(out);
                        slot.ready = true;
                        // An empty answer changes nothing the pending ""
                        // did not already show.
                        report = !slot.text.empty();
                    }
                }
                if (report) PostFolderStatsRedraw();
                continue;
            }

            if (!aspectPath.empty()) {
                int w = 0, h = 0;
                const float aspect =
                        (ProbeImageDimensions(aspectPath, w, h) && w > 0 && h > 0)
                            ? static_cast<float>(w) / static_cast<float>(h)
                            : 0.0f;
                bool report = false;
                {
                    std::lock_guard<std::mutex> lk(statsMutex);
                    if (statsShutdown) return;
                    if (gen == statsGeneration) {
                        aspectCache[aspectPath] = aspect;
                        // Only a landscape image shortens its row; anything
                        // else keeps the full-height layout already drawn.
                        report = aspect > 1.0f;
                    }
                }
                if (report) {
                    aspectsChanged.store(true);
                    PostFolderStatsRedraw();
                }
                continue;
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
            // A freshly probed landscape image shortens the row it sits in.
            if (aspectsChanged.exchange(false) && shrinkThumbnailRows) {
                InvalidateFilerLayout();
            }
            RequestRedraw();
        });
    }

    // ===== FOLDER LISTING PREFETCH =====

    namespace {
        // Grace delay before a batch starts: quick successive navigations
        // replace the queue without any wasted scans, and the folder the user
        // is looking at gets the disk first (thumbnails, stats).
        constexpr auto kPrefetchGraceDelay = std::chrono::milliseconds(300);
        // A cached listing older than this is discarded on use — there is no
        // change watcher, so age bounds how stale a served listing can be.
        constexpr auto kPrefetchMaxAge = std::chrono::seconds(60);
        constexpr size_t kPrefetchMaxFolders = 24;     // cached listings
        constexpr size_t kPrefetchMaxEntries = 50000;  // entries across them
    }

    void UltraCanvasFilerWidget::SetFolderPrefetchEnabled(bool enabled) {
        if (folderPrefetchEnabled == enabled) return;
        folderPrefetchEnabled = enabled;
        std::lock_guard<std::mutex> lk(prefetchMutex);
        ++prefetchGeneration;
        prefetchQueue.clear();
        prefetchCache.clear();
        prefetchLru.clear();
        prefetchCachedEntries = 0;
    }

    void UltraCanvasFilerWidget::QueueFolderPrefetch() {
        if (!folderPrefetchEnabled) return;
        std::lock_guard<std::mutex> lk(prefetchMutex);
        if (prefetchShutdown) return;
        ++prefetchGeneration;      // drops whatever the last folder queued
        prefetchQueue.clear();
        const auto now = std::chrono::steady_clock::now();
        for (const FilerEntry& e : entries) {
            // Real subfolders only: archives list through VirtualFS.
            if (!e.isDirectory || e.isArchive) continue;
            // A listing scanned moments ago is still good; older ones are
            // re-queued so a hit at entry time passes the age check.
            auto it = prefetchCache.find(e.path);
            if (it != prefetchCache.end() &&
                now - it->second.when < std::chrono::seconds(5))
                continue;
            prefetchQueue.push_back(e.path);
            if (prefetchQueue.size() >= kPrefetchMaxFolders) break;
        }
        if (!prefetchQueue.empty()) {
            StartFolderPrefetchWorkerLocked();
            prefetchCond.notify_one();
        }
    }

    bool UltraCanvasFilerWidget::TakePrefetchedListing(const std::string& path,
                                                       std::vector<FilerEntry>& out) {
        PrefetchedListing listing;
        {
            std::lock_guard<std::mutex> lk(prefetchMutex);
            auto it = prefetchCache.find(path);
            if (it == prefetchCache.end()) return false;
            listing = std::move(it->second);
            prefetchCachedEntries -= listing.entries.size();
            prefetchCache.erase(it);   // it becomes the live listing (or is stale)
        }
        if (std::chrono::steady_clock::now() - listing.when > kPrefetchMaxAge)
            return false;
        // The folder must not have changed since the pre-scan. The mtime
        // covers entries added / removed / renamed; a file merely growing
        // inside the window is caught by the age bound above.
        struct stat st{};
        if (::stat(path.c_str(), &st) != 0 || st.st_mtime != listing.dirMtime)
            return false;
        out = std::move(listing.entries);
        return true;
    }

    void UltraCanvasFilerWidget::StartFolderPrefetchWorkerLocked() {
        if (prefetchWorker.joinable() || prefetchShutdown) return;
        prefetchWorker = std::thread([this]() { FolderPrefetchWorkerMain(); });
    }

    void UltraCanvasFilerWidget::StopFolderPrefetchWorker() {
        {
            std::lock_guard<std::mutex> lk(prefetchMutex);
            prefetchShutdown = true;
            prefetchQueue.clear();
        }
        prefetchCond.notify_all();
        if (prefetchWorker.joinable()) prefetchWorker.join();
    }

    void UltraCanvasFilerWidget::FolderPrefetchWorkerMain() {
        // The worker never posts to the UI thread — it only fills the cache
        // that the next SetPath reads — so teardown is a plain shutdown+join.
        uint64_t gracedGeneration = 0;
        for (;;) {
            std::string path;
            uint64_t gen;
            {
                std::unique_lock<std::mutex> lk(prefetchMutex);
                prefetchCond.wait(lk, [this]() {
                    return prefetchShutdown || !prefetchQueue.empty();
                });
                if (prefetchShutdown) return;
                gen = prefetchGeneration;
                if (gen != gracedGeneration) {
                    // New batch: idle a moment first. A navigation during the
                    // wait bumps the generation and restarts the grace.
                    prefetchCond.wait_for(lk, kPrefetchGraceDelay, [this]() {
                        return prefetchShutdown;
                    });
                    if (prefetchShutdown) return;
                    gracedGeneration = gen;   // this batch had its grace
                    if (gen != prefetchGeneration) continue;   // superseded:
                    // the next round graces the replacing batch itself
                }
                if (prefetchQueue.empty()) continue;
                path = std::move(prefetchQueue.front());
                prefetchQueue.pop_front();
            }

            // Take the folder's mtime before the scan: a change while
            // scanning then fails the equality check at entry time.
            struct stat st{};
            if (::stat(path.c_str(), &st) != 0) continue;

            PrefetchedListing listing;
            listing.dirMtime = st.st_mtime;
            ScanRealDirectory(path, true, listing.entries);
            listing.when = std::chrono::steady_clock::now();

            // An oversized listing is not stored — the scan already warmed
            // the OS metadata cache, which is most of the win — and neither
            // is anything from a superseded batch.
            if (listing.entries.size() > kPrefetchMaxEntries) continue;
            {
                std::lock_guard<std::mutex> lk(prefetchMutex);
                if (prefetchShutdown) return;
                if (gen != prefetchGeneration) continue;
                auto it = prefetchCache.find(path);
                if (it != prefetchCache.end()) {
                    prefetchCachedEntries -= it->second.entries.size();
                    prefetchCache.erase(it);
                }
                prefetchCachedEntries += listing.entries.size();
                prefetchCache.emplace(path, std::move(listing));
                prefetchLru.push_back(path);
                // Evict oldest-inserted listings past the budget. The LRU
                // deque may hold paths already taken or replaced; those
                // simply no longer match a cache entry and are skipped.
                while (!prefetchLru.empty() &&
                       (prefetchCache.size() > kPrefetchMaxFolders ||
                        prefetchCachedEntries > kPrefetchMaxEntries)) {
                    std::string victim = std::move(prefetchLru.front());
                    prefetchLru.pop_front();
                    if (victim == path) continue;   // never evict the newest
                    auto vit = prefetchCache.find(victim);
                    if (vit != prefetchCache.end()) {
                        prefetchCachedEntries -= vit->second.entries.size();
                        prefetchCache.erase(vit);
                    }
                }
            }
        }
    }

    // ===== SELECTION INFO BAR =====

    std::string UltraCanvasFilerWidget::EntryExtraInfo(const FilerEntry& e) {
        if (e.isDirectory) return "";
        const bool isImage = e.category == FilerFileCategory::Image;
        if (!isImage && e.category != FilerFileCategory::Audio &&
            e.category != FilerFileCategory::Video)
            return "";

        std::lock_guard<std::mutex> lk(statsMutex);
        auto it = mediaInfoCache.find(e.path);
        if (it != mediaInfoCache.end())
            return it->second.text;   // "" while the probe is still pending

        // Probing opens the file — for an exotic image container it decodes
        // it — and this is asked from the paint path (info bar, dataset
        // lines), so the read goes to the worker. The pending slot keeps
        // repeated calls from queueing the file twice; the finished probe
        // posts a redraw that picks the text up.
        mediaInfoCache.emplace(e.path, MediaInfoSlot{});
        mediaQueue.push_back(MediaProbeRequest{e.path, e.extension, isImage});
        StartFolderStatsWorkerLocked();
        statsCond.notify_one();
        return "";
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

        // The selection is inspected in place: this runs on every repaint, and
        // copying the selected FilerEntrys (GetSelectedEntries) made each
        // frame after a Select All duplicate the whole listing.
        std::vector<const FilerEntry*> sel;
        sel.reserve(selection.size());
        for (size_t idx : selection)
            if (idx < entries.size()) sel.push_back(&entries[idx]);

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
            const FilerEntry& e = *sel.front();
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
        for (const FilerEntry* ep : sel) {
            const FilerEntry& e = *ep;
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

    float UltraCanvasFilerWidget::ItemNameFontSize() const {
        switch (viewType) {
            case FilerViewType::ThumbnailsSmall:
            case FilerViewType::ThumbnailsMedium:
            case FilerViewType::ThumbnailsBig:
            case FilerViewType::ThumbnailsMaximized:
            case FilerViewType::TreeMap:
                return style.smallFontSize;
            default:
                return style.fontSize;
        }
    }

    bool UltraCanvasFilerWidget::IsOnItemName(const ItemLayout& item,
                                              const Point2Di& contentPoint) const {
        // The icon is never the name (clicking it never starts a rename).
        if (item.imageRect.Contains(contentPoint)) return false;

        switch (viewType) {
            case FilerViewType::ThumbnailsSmall:
            case FilerViewType::ThumbnailsMedium:
            case FilerViewType::ThumbnailsBig:
            case FilerViewType::ThumbnailsMaximized:
            case FilerViewType::TreeMap: {
                // Caption sits below the icon.
                int capTop = item.imageRect.y + item.imageRect.height;
                return contentPoint.y >= capTop;
            }
            default: {
                // Row views: the name runs to the right of the icon. In the
                // column views it is limited to its own column, so the other
                // columns (and the size bar) still open the entry.
                int nameLeft = item.imageRect.x + item.imageRect.width;
                int nameRight = item.rect.x + item.rect.width;
                if (viewType == FilerViewType::Details && !detailsColumns.empty()) {
                    nameRight = detailsColumns[0].x + detailsColumns[0].width;
                } else if (viewType == FilerViewType::BarSize) {
                    BarSizeColumns cols = BarSizeColumnsFor(
                            item, barSizeValueWidth > 0 ? barSizeValueWidth
                                                        : barSizeAutoValueWidth);
                    nameRight = cols.nameX + cols.nameWidth;
                }
                return contentPoint.x >= nameLeft && contentPoint.x < nameRight;
            }
        }
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

    // ===== HOVER TOOLTIPS =====
    // Two things under the cursor can describe themselves: a hover icon-menu
    // button (its action) and an item name that did not fit the space it was
    // drawn in (the full name). The button wins where they overlap, so in the
    // Details view the name column tells the user the file name while the icon
    // strip — which sits over the columns to its right — keeps describing its
    // buttons.
    void UltraCanvasFilerWidget::UpdateHoverTooltip(const UCEvent& event,
                                                    const Point2Di& localPoint) {
        TooltipTarget target = TooltipTarget::NoneTarget;
        size_t entry = 0;
        int action = -1;
        std::string text;

        size_t iconEntry = 0;
        int iconAction = IconMenuActionAt(localPoint, iconEntry);
        if (iconAction >= 0) {
            static const char* kIconMenuTips[] = { "Copy", "Cut", "Rename", "Delete" };
            target = TooltipTarget::IconButton;
            entry  = iconEntry;
            action = iconAction;
            text   = kIconMenuTips[iconAction];
        } else if (nameTooltips && renamingIndex < 0 && !IsInInfoBar(localPoint) &&
                   hoveredSplitter < 0) {
            Point2Di content = ToContentPoint(localPoint);
            for (const ItemLayout& item : items) {
                if (!item.rect.Contains(content)) continue;
                if (item.entryIndex < nameTruncated.size() &&
                    nameTruncated[item.entryIndex] &&
                    IsOnItemName(item, content)) {
                    target = TooltipTarget::ItemName;
                    entry  = item.entryIndex;
                    text   = entries[item.entryIndex].name;
                }
                break;
            }
        }

        // Nothing changed: leave the tooltip (and its show timer) alone so it
        // does not restart on every pixel of movement.
        if (target == tooltipTarget && entry == tooltipEntry && action == tooltipAction)
            return;
        tooltipTarget = target;
        tooltipEntry  = entry;
        tooltipAction = action;

        auto* win = GetWindow();
        if (!win || target == TooltipTarget::NoneTarget || text.empty()) {
            UltraCanvasTooltipManager::HideTooltip();
            return;
        }
        UltraCanvasTooltipManager::UpdateAndShowTooltip(
                win, text, Point2Di(event.pointerWindow.x, event.pointerWindow.y));
    }

    void UltraCanvasFilerWidget::HideHoverTooltip() {
        if (tooltipTarget == TooltipTarget::NoneTarget) return;
        tooltipTarget = TooltipTarget::NoneTarget;
        tooltipAction = -1;
        UltraCanvasTooltipManager::HideTooltip();
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
#ifdef ULTRACANVAS_HAS_VIRTUALFS
        // Archives open like folders: descending SetPath()s into the archive
        // and ScanFolder() lists its interior through VirtualFS.
        bool enters = e.isDirectory || e.isArchive;
#else
        // Without VirtualFS an archive can't be browsed — activate it like
        // any other file instead of navigating into an empty view.
        bool enters = e.isDirectory;
#endif
        if (enters) {
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

        // Search-result displays put "Open Path" first: the entries come from
        // different folders, so jumping to an entry's folder is the primary
        // action there.
        if (showOpenPathItem) {
            size_t openIdx = hasSel ? selection.front() : 0;
            addAction(openPathItemLabel, hasSel, [this, openIdx]() {
                if (openIdx >= entries.size()) return;
                const FilerEntry e = entries[openIdx];
                if (onOpenPath) onOpenPath(e);
                else SetPath(fs::path(e.path).parent_path().string());
            });
            menu.AddItem(MenuItemData::Separator());
        }

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

            // Dataset > extra per-file facts under thumbnail captions.
            std::vector<MenuItemData> datasetItems;
            struct DatasetOption { const char* label; FilerDatasetField field; };
            static const DatasetOption datasetOptions[] = {
                {"Size",                   FilerDatasetField::Size},
                {"Edit date",              FilerDatasetField::ModifiedDate},
                {"Creation date",          FilerDatasetField::CreatedDate},
                {"Attributes",             FilerDatasetField::Attributes},
                {"Length (audio/video)",   FilerDatasetField::Length},
                {"Dimensions (bitmaps)",   FilerDatasetField::Dimensions},
            };
            for (const DatasetOption& o : datasetOptions) {
                FilerDatasetField f = o.field;
                datasetItems.push_back(MenuItemData::Checkbox(
                        o.label, IsDatasetFieldEnabled(f),
                        [this, f](bool on) { SetDatasetField(f, on); }));
            }

            std::vector<MenuItemData> displayItems;
            displayItems.push_back(MenuItemData::Submenu("Sort", sortItems));
            displayItems.push_back(MenuItemData::Submenu("Type", typeItems));
            displayItems.push_back(MenuItemData::Submenu("Dataset", datasetItems));
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

        // Compress > (pick the archive format)
        {
            bool canCompress = !entries.empty();
            struct CompressFormat { const char* label; const char* ext; };
            static const CompressFormat compressFormats[] = {
                {"ZIP (.zip)",             "zip"},
                {"7-Zip (.7z)",            "7z"},
                {"TAR (.tar)",             "tar"},
                {"TAR + gzip (.tar.gz)",   "tar.gz"},
                {"TAR + bzip2 (.tar.bz2)", "tar.bz2"},
                {"TAR + xz (.tar.xz)",     "tar.xz"},
                {"TAR + Zstd (.tar.zst)",  "tar.zst"},
            };
            std::vector<MenuItemData> compressItems;
            for (const CompressFormat& f : compressFormats) {
                std::string ext = f.ext;
                std::string label = f.label;
                MenuItemData item = MenuItemData::Action(
                        f.label,
                        [this, ext, label]() { OpenCompressDialog(ext, label); });
                item.enabled = canCompress;
                compressItems.push_back(item);
            }
            MenuItemData compressSub = MenuItemData::Submenu("Compress", compressItems);
            compressSub.enabled = canCompress;
            menu.AddItem(compressSub);
        }
        addAction("Extract", anyArchive, [this]() { ExtractSelection(); });
        menu.AddItem(MenuItemData::Separator());

        addAction("Print", static_cast<bool>(onPrint), [this]() {
            if (onPrint) onPrint(SelectionOrAll());
        }, "Ctrl+P");
        menu.AddItem(MenuItemData::Separator());

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
        // Deliberately leave popupOwner unset. The owner would be this whole
        // widget, and the window's dismissal logic treats a click on the owner as
        // "inside" the popup — so a left click anywhere in the file view would
        // fail to close the context menu. With no owner, any click outside the
        // menu bounds (including on the file view itself) dismisses it.
        settings.closeByClickOutside = true;
        activePopupMenu->OpenMenu(winPos, *win, settings);
    }

    // ===== RUBBER-BAND SELECTION =====
    Rect2Di UltraCanvasFilerWidget::MarqueeRect() const {
        int x0 = std::min(marqueeAnchor.x, marqueeCurrent.x);
        int y0 = std::min(marqueeAnchor.y, marqueeCurrent.y);
        int x1 = std::max(marqueeAnchor.x, marqueeCurrent.x);
        int y1 = std::max(marqueeAnchor.y, marqueeCurrent.y);
        return Rect2Di(x0, y0, x1 - x0, y1 - y0);
    }

    void UltraCanvasFilerWidget::UpdateMarquee(const Point2Di& localPoint) {
        // Auto-scroll at the viewport edge so the rectangle can grow past it.
        Rect2Di area = ContentBounds();
        if (IsHorizontal()) {
            if (localPoint.x > area.x + area.width)
                scrollOffsetX += std::min(localPoint.x - (area.x + area.width),
                                          kWheelStep);
            else if (localPoint.x < area.x)
                scrollOffsetX -= std::min(area.x - localPoint.x, kWheelStep);
        } else {
            if (localPoint.y > area.y + area.height)
                scrollOffsetY += std::min(localPoint.y - (area.y + area.height),
                                          kWheelStep);
            else if (localPoint.y < area.y)
                scrollOffsetY -= std::min(area.y - localPoint.y, kWheelStep);
        }
        ClampScroll();
        marqueeCurrent = ToContentPoint(localPoint);

        // Reselect from scratch every move: the rectangle's touch set plus —
        // with Ctrl — whatever was selected when the band started. Membership
        // is tracked in a flat flag array; deduplicating with std::find made
        // every move of a band over thousands of items quadratic.
        Rect2Di r = MarqueeRect();
        std::vector<size_t> newSel = marqueeAdditive ? marqueeBaseSelection
                                                     : std::vector<size_t>();
        std::vector<uint8_t> inSel(entries.size(), 0);
        for (size_t idx : newSel)
            if (idx < inSel.size()) inSel[idx] = 1;
        for (const ItemLayout& it : items) {
            if (!it.rect.Intersects(r)) continue;
            if (it.entryIndex < inSel.size() && !inSel[it.entryIndex]) {
                inSel[it.entryIndex] = 1;
                newSel.push_back(it.entryIndex);
            }
        }
        if (newSel != selection) {
            selection = std::move(newSel);
            FireSelectionChanged();
        }
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::FinishMarquee() {
        bool wasActive = marqueeActive;
        marqueeArmed = false;
        marqueeActive = false;
        if (dragMouseCaptured) {
            if (auto* app = UltraCanvasApplication::GetInstance()) app->ReleaseMouse();
            dragMouseCaptured = false;
        }
        if (!wasActive) {
            // The press never moved: it was a plain click on empty space,
            // which keeps its old meaning — clear the selection (a Ctrl
            // click on empty space leaves it alone).
            if (!marqueeAdditive) HandleItemClick(-1, false, false);
        }
        marqueeBaseSelection.clear();
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::CancelMarquee() {
        if (!marqueeArmed && !marqueeActive) return;
        bool wasActive = marqueeActive;
        marqueeArmed = false;
        marqueeActive = false;
        if (dragMouseCaptured) {
            if (auto* app = UltraCanvasApplication::GetInstance()) app->ReleaseMouse();
            dragMouseCaptured = false;
        }
        if (wasActive && selection != marqueeBaseSelection) {
            selection = marqueeBaseSelection;   // back to the pre-band state
            FireSelectionChanged();
        }
        marqueeBaseSelection.clear();
        RequestRedraw();
    }

    // ===== EVENTS =====
    bool UltraCanvasFilerWidget::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;

        // The compress dialog is a modal in-widget overlay: while it is up it
        // consumes every event and nothing behind it reacts.
        if (compressDlg.active) return HandleCompressDialogEvent(event);

        switch (event.type) {
            case UCEventType::MouseLeave: {
                if (hoveredIndex != -1) { hoveredIndex = -1; RequestRedraw(); }
                if (hoveredSplitter != -1 && draggingSplitter < 0) {
                    hoveredSplitter = -1;
                    RequestRedraw();
                }
                HideHoverTooltip();
                // Do not cancel an active drag here: the pointer is captured
                // for the whole gesture and it ends on the button release, so
                // a leave (the pointer crossing an edge — which is exactly how
                // a drag reaches another window) must not interrupt it.
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
                // What the tooltip describes slides away under the cursor.
                HideHoverTooltip();
                ClampScroll();
                RequestRedraw();
                return true;
            }
            case UCEventType::MouseMove: {
                Point2Di local(event.pointer.x, event.pointer.y);
                // A column splitter drag owns the pointer until it is released.
                if (draggingSplitter >= 0) {
                    UpdateColumnSplitterDrag(local);
                    return true;
                }
                // A running item drag owns the pointer too.
                if (draggingItems) {
                    UpdateItemDrag(local);
                    return true;
                }
                // Armed gesture: once the press moves past the slop the
                // pressed item (or the selection it belongs to) is picked up.
                if (dragOutArmed) {
                    int dx = local.x - dragOutPressPoint.x;
                    int dy = local.y - dragOutPressPoint.y;
                    if (dx * dx + dy * dy >= kDragStartSlop * kDragStartSlop) {
                        BeginItemDrag(local);
                        if (draggingItems) {
                            // The press may already be outside the widget when
                            // the first move arrives (a fast flick out): hand
                            // the drag straight over to the OS.
                            UpdateItemDrag(local);
                            return true;
                        }
                    }
                }
                // A running rubber-band selection owns the pointer as well.
                if (marqueeActive) {
                    UpdateMarquee(local);
                    return true;
                }
                // Armed on empty space: past the slop the press becomes the
                // rubber band instead of a click.
                if (marqueeArmed) {
                    int dx = local.x - marqueePressLocal.x;
                    int dy = local.y - marqueePressLocal.y;
                    if (dx * dx + dy * dy >= kDragStartSlop * kDragStartSlop) {
                        marqueeActive = true;
                        UpdateMarquee(local);
                        return true;
                    }
                }
                if (draggingScrollbar) {
                    ScrollThumbTo((IsHorizontal() ? local.x : local.y)
                                  - scrollbarGrabOffset);
                    RequestRedraw();
                    return true;
                }
                // Column splitters: highlight the one under the cursor and
                // show the resize cursor, so they are discoverable the same
                // way a split-pane divider is.
                {
                    int splitter = ColumnSplitterAt(local);
                    if (splitter != hoveredSplitter) {
                        hoveredSplitter = splitter;
                        RequestRedraw();
                    }
                    SetMouseCursor(splitter >= 0 ? UCMouseCursor::SizeWE
                                                 : UCMouseCursor::Default);
                }

                int newHover = IsInInfoBar(local) ? -1 : ItemAt(ToContentPoint(local));
                // Keep the item hovered while the pointer is over one of its
                // hover icon-menu buttons. On narrow tiles the button strip can
                // extend past the item's own rect, so a plain ItemAt() test
                // would drop the hover the moment the cursor reaches a button
                // and the menu would flicker away.
                if (hoverIconMenu) {
                    size_t iconEntry = 0;
                    if (IconMenuActionAt(local, iconEntry) >= 0)
                        newHover = static_cast<int>(iconEntry);
                }
                if (newHover != hoveredIndex) {
                    hoveredIndex = newHover;
                    RequestRedraw();
                }

                UpdateHoverTooltip(event, local);
                // While the gesture holds the pointer capture this move is
                // ours: consuming it keeps the dispatcher from handing the
                // same move to whatever else is under the cursor.
                return dragMouseCaptured;
            }
            case UCEventType::MouseDown: {
                // A running drag owns the pointer until it is released.
                if (draggingItems) return true;
                Point2Di local(event.pointer.x, event.pointer.y);
                // Read before SetFocus: taking the focus away from the rename
                // editor commits it (its onFocusLost), which already clears
                // renamingIndex by the time SetFocus returns.
                bool wasRenaming = renamingIndex >= 0;
                SetFocus(true);

                // Any new press supersedes a not-yet-fired rename click and
                // whatever an earlier press left deferred (its release may
                // have gone elsewhere).
                CancelPendingRename();
                pendingSelectIndex = -1;
                dragCollapseIndex = -1;

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
                            // Capture the mouse so the drag keeps tracking even
                            // when the pointer leaves the widget (the vertical
                            // scrollbar hugs the right edge, so dragging right
                            // would otherwise trigger a MouseLeave that killed
                            // the drag). Captured MouseMove/MouseUp route here
                            // directly and the spurious leave is never sent.
                            if (auto* app = UltraCanvasApplication::GetInstance())
                                app->CaptureMouse(this);
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
                        // The buttons act on the hovered entry — or on the
                        // whole selection when that entry is part of it — and
                        // never change the selection themselves: pressing one
                        // is "do this to that file", not "show me that file",
                        // so it must not fire onSelectionChanged and re-target
                        // (or open) a preview pane fed by it.
                        std::vector<FilerEntry> targets = SelectionOrEntry(entryIdx);
                        switch (static_cast<IconMenuAction>(action)) {
                            case IconMenuAction::Copy:
                                EntriesToClipboard(targets, false); break;
                            case IconMenuAction::Cut:
                                EntriesToClipboard(targets, true); break;
                            case IconMenuAction::Rename:
                                StartRename(entryIdx); break;
                            case IconMenuAction::Delete:
                                DeleteEntries(targets); break;
                        }
                        return true;
                    }
                }

                // A column splitter grabs the press before the header sort
                // click and before the entries underneath it.
                {
                    int splitter = ColumnSplitterAt(local);
                    if (splitter >= 0) {
                        HideHoverTooltip();
                        BeginColumnSplitterDrag(splitter, local);
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
                    Point2Di content = ToContentPoint(local);
                    int index = ItemAt(content);
                    bool alreadySelected = index >= 0 &&
                            std::find(selection.begin(), selection.end(),
                                      static_cast<size_t>(index)) != selection.end();
                    if (index >= 0 && !alreadySelected && !event.ctrl && !event.shift
                        && dragEnabled) {
                        // A plain press on an unselected item may still turn
                        // into a drag, and a drag must not change the selection
                        // (an attached preview would load the file that is only
                        // being carried). Selecting it waits for the release.
                        pendingSelectIndex = index;
                    } else if (alreadySelected && !event.ctrl && !event.shift) {
                        // Keep the (multi-)selection so it can be dragged as a
                        // whole; collapsing to just this item happens on
                        // release when no drag started.
                        dragCollapseIndex = index;
                        // Windows-style rename: pressing the name of the entry
                        // that is already the sole selection is a rename click
                        // — unless it turns into a drag or a double-click. The
                        // delay timer is armed on release. The press that
                        // commits an active rename doesn't count.
                        if (!wasRenaming && selection.size() == 1) {
                            const ItemLayout* layout = nullptr;
                            for (const ItemLayout& it : items) {
                                if (static_cast<int>(it.entryIndex) == index) {
                                    layout = &it;
                                    break;
                                }
                            }
                            if (layout && IsOnItemName(*layout, content))
                                pendingRenameIndex = index;
                        }
                    } else if (index >= 0) {
                        HandleItemClick(index, event.ctrl, event.shift);
                        dragCollapseIndex = -1;
                    } else {
                        // Empty space: arm the rubber band. What a plain
                        // click means (clear the selection) waits for the
                        // release, so a drag selects instead of clearing.
                        marqueeArmed = true;
                        marqueeAdditive = event.ctrl;
                        marqueePressLocal = local;
                        marqueeAnchor = content;
                        marqueeCurrent = content;
                        marqueeBaseSelection = selection;
                        if (auto* app = UltraCanvasApplication::GetInstance()) {
                            app->CaptureMouse(this);
                            dragMouseCaptured = true;
                        }
                    }
                    // Pressing on an item arms the drag gesture; the drag starts
                    // once the pointer moves past the slop threshold. The mouse
                    // is captured for the gesture so a fast flick out of the
                    // widget still delivers the move that starts it (without
                    // the capture that move goes to whatever is under the
                    // cursor and the drag is simply lost).
                    if (index >= 0 && dragEnabled) {
                        dragOutArmed = true;
                        dragOutPressPoint = local;
                        dragPressIndex = index;
                        if (auto* app = UltraCanvasApplication::GetInstance()) {
                            app->CaptureMouse(this);
                            dragMouseCaptured = true;
                        }
                    }
                }
                return true;
            }
            case UCEventType::MouseUp: {
                if (draggingSplitter >= 0) {
                    EndColumnSplitterDrag();
                    return true;
                }
                if (marqueeArmed || marqueeActive) {
                    FinishMarquee();
                    return true;
                }
                if (draggingItems) {
                    // Drop: Ctrl copies, a plain drop moves.
                    FinishItemDrag(Point2Di(event.pointer.x, event.pointer.y),
                                   event.ctrl);
                    pendingRenameIndex = -1;
                    return true;
                }
                if (dragOutArmed && dragCollapseIndex >= 0) {
                    // The press on a selected item turned out to be a plain
                    // click: apply the deferred "select only this item".
                    HandleItemClick(dragCollapseIndex, false, false);
                } else if (dragOutArmed && pendingSelectIndex >= 0) {
                    // Same for the press on an unselected item: no drag
                    // started, so it was a plain click after all.
                    HandleItemClick(pendingSelectIndex, false, false);
                }
                if (dragOutArmed && pendingRenameIndex >= 0) {
                    // Plain click on the sole selection's name: rename after
                    // the delay unless a double-click cancels it first.
                    ArmPendingRenameTimer();
                } else {
                    pendingRenameIndex = -1;
                }
                pendingSelectIndex = -1;
                dragCollapseIndex = -1;
                // Consume the release when the gesture held the pointer
                // capture: the dispatcher offers a captured release to the
                // capturing element first and then, if it was not handled,
                // again to whatever sits under the cursor — a second pass
                // through here would undo what this one just armed.
                bool ownedRelease = dragMouseCaptured;
                EndDragGesture();
                if (draggingScrollbar) {
                    draggingScrollbar = false;
                    if (auto* app = UltraCanvasApplication::GetInstance())
                        app->ReleaseMouse();
                    RequestRedraw();
                    return true;
                }
                return ownedRelease;
            }
            case UCEventType::MouseDoubleClick: {
                // While the rename editor is open a double-click can only be
                // inside it (a click anywhere else commits on its MouseDown):
                // it belongs to the editor, never opens the entry behind it.
                if (renamingIndex >= 0) return true;
                // The first click of this double-click may have armed the
                // deferred rename — opening the entry supersedes it.
                CancelPendingRename();
                Point2Di local(event.pointer.x, event.pointer.y);
                if (IsInInfoBar(local)) return true;
                // A double-click on a divider must not open the entry behind it.
                if (ColumnSplitterAt(local) >= 0) return true;
                int index = ItemAt(ToContentPoint(local));
                if (index >= 0) {
                    ActivateEntry(static_cast<size_t>(index));
                    return true;
                }
                return false;
            }
            case UCEventType::KeyDown: {
                // Escape abandons a running drag (nothing is moved).
                if (draggingItems && event.virtualKey == UCKeys::Escape) {
                    CancelItemDrag();
                    return true;
                }
                // Escape abandons a running rubber band (selection restored).
                if (marqueeActive && event.virtualKey == UCKeys::Escape) {
                    CancelMarquee();
                    return true;
                }
                CancelPendingRename();   // keyboard action outruns the click
                // While the rename editor (a focused child) is open, the only
                // keys that reach the widget are ones the editor did not
                // consume — swallow them so shortcuts / navigation stay off.
                if (renamingIndex >= 0) return true;

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
