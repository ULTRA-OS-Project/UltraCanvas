// core/UltraCanvasFilerWidget.cpp
// Filer folder widget: displays one folder's content with selectable view types
// (details, list, thumbnails, size bars, treemap), sorting, an inline rename
// editor, a hover icon menu, the full file context menu and a selection info
// bar (type / size / dates / attributes, image dimensions, media duration and
// codec via lightweight header probes, recursive folder stats). Image
// thumbnails decode asynchronously (see ASYNC THUMBNAILS) so the folder page
// never waits for image files.
// Entries are draggable (see DRAGGING ENTRIES): the drag is drawn here and a
// drop on a folder of the view moves the files into it (Ctrl copies). Crossing
// the widget's border does not end it — the badge keeps following the cursor
// over the rest of the window (through the window's drag overlay) and a release
// over another element hands it the files as a Drop event; only leaving the
// window turns the same set into a native OS drag onto other applications.
// Dragging never changes the selection. External drops are copied into the
// shown folder, and Copy / Cut / Paste go through the system clipboard so
// files can be exchanged with other programs (external file managers,
// editors, ...). Pasting a clipboard that
// holds raw data instead of files (an image or text copied elsewhere) writes
// that content as a new file into the shown folder.
// The column views (details, list, size bars) carry UltraCanvasSplitPane-style
// splitters between their columns (see COLUMN SPLITTERS), and names too long
// for the space they are drawn in show the full name in a hover tooltip.
// Tile captions (thumbnail grids, treemap) wrap long names over several lines
// instead of cutting them off after one, with the breaks balanced so the
// lines come out near equal (see WRAPPED CAPTIONS).
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
// Content previews are produced per file kind (see SELECTIVE PREVIEWS): image
// pipeline for bitmaps and vectors, poster frame for videos, first page for
// PDFs, a shaded software render for 3D models and a miniature page of the
// file's own text for text, documents and spreadsheets. Each kind can be
// switched off individually (Display > Preview), which drops its entries back
// to the plain type glyph and stops the widget from reading those files.
// Version: 1.17.0
// Last Modified: 2026-08-23
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
#include "UltraCanvasFileAssociations.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasNativeFileIcons.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasSupportedFormats.h"
#include "UltraCanvasUtils.h"
#include "../libspecific/Cairo/QoiPixmapCodec.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasTooltipManager.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasProgressDialog.h"
#include "UltraCanvasSwitch.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasVideoThumbnail.h"
#include "UltraCanvasZipPackage.h"
#include "Models/STL/UltraCanvasSTLLoader.h"
#include "Plugins/Documents/Word/UltraCanvasWordDocumentIO.h"
#ifdef ULTRACANVAS_PLUGIN_PDF
#include "Plugins/Documents/UltraCanvasPDF.h"
#endif
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sys/stat.h>
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>   // GetFileAttributesExW: the attribute bits ::stat cannot see
#endif

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
        // How often the UI reads the archive worker's counters. Fast enough
        // that the ring moves smoothly, slow enough to cost nothing.
        constexpr unsigned int kArchivePollIntervalMs = 100;

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
                {"stl",  {"STL",  FilerFileCategory::Model3D}},
                {"obj",  {"Wavefront", FilerFileCategory::Model3D}},
                {"ply",  {"PLY",  FilerFileCategory::Model3D}},
                {"3ds",  {"3D Studio", FilerFileCategory::Model3D}},
                {"3mf",  {"3MF",  FilerFileCategory::Model3D}},
                {"gltf", {"glTF", FilerFileCategory::Model3D}},
                {"glb",  {"glTF Binary", FilerFileCategory::Model3D}},
                {"dae",  {"COLLADA", FilerFileCategory::Model3D}},
                {"fbx",  {"FBX",  FilerFileCategory::Model3D}},
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
                {"tsv",  {"TSV",  FilerFileCategory::Text}},
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
                case FilerFileCategory::Model3D:     return "Model";
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
                case FilerFileCategory::Model3D:     return Color(126, 87, 194, 255);
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

        // True when the tail after the last dot is a real file-type suffix and
        // not simply part of the name. Version numbers and architecture tags
        // carry dots too — "UCDemo-Windows-0.3.27-x86_64" ends in
        // ".27-x86_64", which is no more an extension than ".3" is.
        bool LooksLikeFileExtension(const std::string& lowerExt) {
            if (lowerExt.empty() || lowerExt.size() > 8) return false;
            bool anyAlpha = false;
            for (unsigned char c : lowerExt) {
                if (!std::isalnum(c)) return false;   // '-', '_', ' ' -> not a type
                if (std::isalpha(c)) anyAlpha = true;
            }
            return anyAlpha;   // a pure number ("0.3", "2024") is not a type
        }

        // Default archive name for an entry: its name without the file-type
        // suffix. Folders keep their full name — a folder has no extension, so
        // every dot in it belongs to the name — and so do files whose tail is
        // not a plausible extension.
        std::string ArchiveBaseNameOf(const std::string& name, bool isDirectory) {
            if (isDirectory) return name;
            size_t dot = name.find_last_of('.');
            if (dot == std::string::npos || dot == 0) return name;
            if (!LooksLikeFileExtension(LowerExtension(name))) return name;
            std::string base = name.substr(0, dot);
            // Compound suffixes: dropping ".gz" off "sources.tar.gz" leaves a
            // ".tar" that belongs to the suffix, not to the name.
            if (base.size() > 4) {
                std::string tail = base.substr(base.size() - 4);
                std::transform(tail.begin(), tail.end(), tail.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                if (tail == ".tar") base.erase(base.size() - 4);
            }
            return base;
        }

        // Detaching a child from inside its own event handler must not drop the
        // last reference to it: the handler keeps touching the object after the
        // callback returns (UltraCanvasButton::OnEvent calls RequestRedraw()
        // straight after onClick, and a text input's Enter/Escape callbacks
        // return through HandleKeyDown). Park it on the UI queue so it dies a
        // turn of the loop later instead. The task holds only the element.
        void ReleaseAfterEventLoopTurn(std::shared_ptr<UltraCanvasUIElement> element) {
            if (!element) return;
            if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
                app->PostToUIThread([element]() {});
            }
        }

        // ===== SELECTIVE PREVIEWS =====
        // The preview kind a file belongs to — what a content preview for it
        // would cost to produce, and therefore which Display > Preview switch
        // governs it. The extension decides first because the kinds do not
        // line up with FilerFileCategory one to one: PDF is its own kind
        // (it renders a page) and CSV / TSV preview as a cell grid like the
        // real spreadsheet formats although their category is Text.
        FilerPreviewType PreviewTypeForFile(const std::string& ext,
                                            FilerFileCategory category) {
            if (ext == "pdf") return FilerPreviewType::PDF;
            if (ext == "csv" || ext == "tsv") return FilerPreviewType::Spreadsheets;
            switch (category) {
                case FilerFileCategory::Image:       return FilerPreviewType::Bitmaps;
                case FilerFileCategory::Vector:      return FilerPreviewType::VectorGraphics;
                case FilerFileCategory::Model3D:     return FilerPreviewType::Models3D;
                case FilerFileCategory::Video:       return FilerPreviewType::Videos;
                case FilerFileCategory::Document:    return FilerPreviewType::Docs;
                case FilerFileCategory::Text:        return FilerPreviewType::Text;
                case FilerFileCategory::Spreadsheet: return FilerPreviewType::Spreadsheets;
                default:                             return FilerPreviewType::NonePreview;
            }
        }

        // Same answer for a bare path — used by the decode workers, which see
        // only the file they were handed (that may be an entry's explicit
        // thumbnail image rather than the entry itself).
        FilerPreviewType PreviewTypeForPath(const std::string& path) {
            const std::string ext = LowerExtension(path);
            const auto& m = ExtensionTypeMap();
            auto it = m.find(ext);
            return PreviewTypeForFile(ext, it != m.end() ? it->second.category
                                                         : FilerFileCategory::Other);
        }

        // True when this build can render a PDF page into a preview.
        bool PdfPreviewAvailable() {
#ifdef ULTRACANVAS_PLUGIN_PDF
            return !PDFEngineFactory::Available().empty();
#else
            return false;
#endif
        }

        // Straight (non-premultiplied) RGBA rows into a fresh pixmap. The
        // pixmap holds premultiplied ARGB32 in little-endian byte order, which
        // is what every Cairo-backed surface in the framework expects.
        std::shared_ptr<UCPixmap> PixmapFromRGBA(const uint8_t* rgba, int w, int h,
                                                 int srcStride) {
            if (!rgba || w <= 0 || h <= 0) return nullptr;
            auto pm = std::make_shared<UCPixmap>();
            if (!pm->Init(w, h)) return nullptr;
            uint32_t* dst = pm->GetPixelData();
            if (!dst) return nullptr;
            for (int y = 0; y < h; ++y) {
                const uint8_t* src = rgba + static_cast<size_t>(y) * srcStride;
                uint32_t* row = dst + static_cast<size_t>(y) * w;
                for (int x = 0; x < w; ++x, src += 4) {
                    const uint8_t r = src[0], g = src[1], b = src[2], a = src[3];
                    row[x] = (uint32_t(a) << 24)
                           | (uint32_t((uint16_t(r) * a + 127) / 255) << 16)
                           | (uint32_t((uint16_t(g) * a + 127) / 255) << 8)
                           |  uint32_t((uint16_t(b) * a + 127) / 255);
                }
            }
            pm->MarkDirty();
            return pm;
        }

        // ===== PDF PREVIEW (first page) =====
        // Rendered on the thumbnail workers, so a folder of PDFs pages in the
        // same way a folder of photos does. Each call opens its own document
        // (and with it its own engine context), which is what makes it safe to
        // run several of them on different threads at once.
        std::shared_ptr<UCPixmap> RenderPdfPreviewPixmap(const std::string& path,
                                                         int w, int h, float scale) {
#ifdef ULTRACANVAS_PLUGIN_PDF
            const int maxDim = std::max(16, static_cast<int>(std::lround(
                    std::max(w, h) * std::max(1.0f, scale))));
            std::unique_ptr<IPDFDocument> doc = OpenPDF(path);
            if (!doc || doc->GetPageCount() < 1) return nullptr;
            PDFRenderedPage page = doc->RenderThumbnail(1, maxDim);
            if (!page.IsValid() || page.colorMode != PDFColorMode::RGBA) return nullptr;
            auto pm = PixmapFromRGBA(page.pixels.data(), page.width, page.height,
                                     page.stride);
            if (!pm) return nullptr;
            // A page is white on a white widget: outline it so the tile shows
            // a sheet of paper rather than floating text.
            if (uint32_t* px = pm->GetPixelData()) {
                constexpr uint32_t kEdge = 0xFFB4B4B8u;
                for (int x = 0; x < page.width; ++x) {
                    px[x] = kEdge;
                    px[static_cast<size_t>(page.height - 1) * page.width + x] = kEdge;
                }
                for (int y = 0; y < page.height; ++y) {
                    px[static_cast<size_t>(y) * page.width] = kEdge;
                    px[static_cast<size_t>(y) * page.width + page.width - 1] = kEdge;
                }
                pm->MarkDirty();
            }
            return pm;
#else
            (void)path; (void)w; (void)h; (void)scale;
            return nullptr;
#endif
        }

        // ===== 3D MODEL PREVIEW =====
        // A shaded three-quarter view of the mesh, rasterized in software on
        // the worker thread: the GL-backed viewer needs a window and a current
        // context, neither of which a background decode has. Flat shading off
        // the triangle geometry (STL facet normals are often wrong or absent)
        // with a single head-light, drawn onto a transparent background so the
        // tile keeps the widget's colour behind the model.
        constexpr size_t kModelPreviewTriangleCap = 2000000;

        std::shared_ptr<UCPixmap> RenderModelPreviewPixmap(const std::string& path,
                                                           int w, int h, float scale) {
            if (!UltraCanvasSTLLoader::HasSTLExtension(path)) return nullptr;
            Mesh3D mesh;
            if (!UltraCanvasSTLLoader::Load(path, mesh) || mesh.Empty()) return nullptr;
            if (mesh.TriangleCount() > kModelPreviewTriangleCap) return nullptr;
            if (!mesh.bounds.IsValid()) mesh.ComputeBounds();

            const int pw = std::max(8, static_cast<int>(std::lround(
                    w * std::max(1.0f, scale))));
            const int ph = std::max(8, static_cast<int>(std::lround(
                    h * std::max(1.0f, scale))));

            // Yaw / pitch of the standard "look at it from the front left and
            // slightly above" pose used by model viewers.
            constexpr float kYaw   = -0.55f;   // radians
            constexpr float kPitch =  0.42f;
            const float cy = std::cos(kYaw),   sy = std::sin(kYaw);
            const float cp = std::cos(kPitch), sp = std::sin(kPitch);
            auto rotate = [&](const Vec3& v) {
                const float x1 =  v.x * cy + v.z * sy;
                const float z1 = -v.x * sy + v.z * cy;
                return Vec3{x1, v.y * cp - z1 * sp, v.y * sp + z1 * cp};
            };

            const Vec3 center = mesh.bounds.Center();
            const float radius = mesh.bounds.Radius();
            // 0.92 leaves a hair of margin so the silhouette never touches the
            // tile edge; the rotated bounding sphere fits in either direction.
            const float unit = 0.92f * 0.5f * static_cast<float>(std::min(pw, ph)) / radius;
            const float ox = pw * 0.5f, oy = ph * 0.5f;

            std::vector<float> depth(static_cast<size_t>(pw) * ph,
                                     -std::numeric_limits<float>::max());
            std::vector<uint32_t> pixels(static_cast<size_t>(pw) * ph, 0u);

            const Vec3 light = Vec3{0.35f, 0.55f, 0.76f}.Normalized();
            constexpr float kBaseR = 132.0f, kBaseG = 158.0f, kBaseB = 205.0f;

            for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
                const uint32_t ia = mesh.indices[t], ib = mesh.indices[t + 1],
                               ic = mesh.indices[t + 2];
                if (ia >= mesh.positions.size() || ib >= mesh.positions.size() ||
                    ic >= mesh.positions.size()) continue;
                const Vec3 a = rotate(mesh.positions[ia] - center);
                const Vec3 b = rotate(mesh.positions[ib] - center);
                const Vec3 c = rotate(mesh.positions[ic] - center);
                Vec3 n = (b - a).Cross(c - a).Normalized();
                if (n.z < 0.0f) n = n * -1.0f;   // two-sided: light the facet we see
                const float lambert = std::max(0.0f, n.Dot(light));
                const float shade = 0.28f + 0.72f * lambert;

                // Screen space: y grows downwards, so the model's up axis is
                // negated. Depth is the rotated z (bigger = closer).
                const float ax = ox + a.x * unit, ay = oy - a.y * unit;
                const float bx = ox + b.x * unit, by = oy - b.y * unit;
                const float cx2 = ox + c.x * unit, cy2 = oy - c.y * unit;
                const float area = (bx - ax) * (cy2 - ay) - (by - ay) * (cx2 - ax);
                if (std::fabs(area) < 1e-6f) continue;
                const float invArea = 1.0f / area;

                int minX = std::max(0, static_cast<int>(std::floor(std::min({ax, bx, cx2}))));
                int maxX = std::min(pw - 1, static_cast<int>(std::ceil(std::max({ax, bx, cx2}))));
                int minY = std::max(0, static_cast<int>(std::floor(std::min({ay, by, cy2}))));
                int maxY = std::min(ph - 1, static_cast<int>(std::ceil(std::max({ay, by, cy2}))));
                if (minX > maxX || minY > maxY) continue;

                const uint8_t rr = static_cast<uint8_t>(std::min(255.0f, kBaseR * shade));
                const uint8_t gg = static_cast<uint8_t>(std::min(255.0f, kBaseG * shade));
                const uint8_t bb = static_cast<uint8_t>(std::min(255.0f, kBaseB * shade));
                const uint32_t argb = 0xFF000000u | (uint32_t(rr) << 16)
                                    | (uint32_t(gg) << 8) | uint32_t(bb);

                for (int py = minY; py <= maxY; ++py) {
                    const float fy = py + 0.5f;
                    for (int px = minX; px <= maxX; ++px) {
                        const float fx = px + 0.5f;
                        float w0 = ((bx - ax) * (fy - ay) - (by - ay) * (fx - ax)) * invArea;
                        float w1 = ((fx - ax) * (cy2 - ay) - (fy - ay) * (cx2 - ax)) * invArea;
                        // w0 weights c, w1 weights b, the rest weights a.
                        const float w2 = 1.0f - w0 - w1;
                        if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
                        const float z = a.z * w2 + b.z * w1 + c.z * w0;
                        const size_t idx = static_cast<size_t>(py) * pw + px;
                        if (z <= depth[idx]) continue;
                        depth[idx] = z;
                        pixels[idx] = argb;
                    }
                }
            }

            auto pm = std::make_shared<UCPixmap>();
            if (!pm->Init(pw, ph)) return nullptr;
            uint32_t* dst = pm->GetPixelData();
            if (!dst) return nullptr;
            std::memcpy(dst, pixels.data(), pixels.size() * sizeof(uint32_t));
            pm->MarkDirty();
            return pm;
        }

        // Smallest box a page-shaped preview is drawn in. Below it a page of
        // text, a PDF page or a shaded model is an indistinct smudge, so the
        // small icon slots of the Details / List rows keep the type glyph —
        // and the widget never opens those files for a preview.
        constexpr int kContentPreviewMinEdge = 40;

        // ===== TEXT-CONTENT PREVIEWS (Text / Docs / Spreadsheets) =====
        // These files have nothing to decode: their preview is the beginning
        // of their own content, drawn later as a miniature page. Only the
        // reading and un-wrapping happens here (on a worker thread) — enough
        // lines to fill the biggest tile, each cut to a length no tile can
        // show in full anyway.
        constexpr size_t kPreviewMaxLines   = 18;
        constexpr size_t kPreviewMaxColumns = 8;     // spreadsheet cells per row
        constexpr size_t kPreviewLineChars  = 160;
        constexpr size_t kPreviewReadBytes  = 128 * 1024;

        // First bytes of a file, stopping at a NUL (binary files preview as
        // nothing rather than as mojibake).
        std::string ReadFileHead(const std::string& path, size_t maxBytes) {
            std::ifstream f(PathFromUtf8(path), std::ios::binary);
            if (!f) return {};
            std::string buf(maxBytes, '\0');
            f.read(buf.data(), static_cast<std::streamsize>(maxBytes));
            buf.resize(static_cast<size_t>(std::max<std::streamsize>(0, f.gcount())));
            size_t nul = buf.find('\0');
            if (nul != std::string::npos) buf.resize(nul);
            return buf;
        }

        // Collapses runs of whitespace, trims the ends and caps the length —
        // one preview line is a single short line of prose whatever the file
        // did with tabs, CRs and indentation. Tabs are kept when `keepTabs`
        // is set, because they separate the cells of a tabular preview.
        std::string TidyPreviewLine(const std::string& raw, bool keepTabs) {
            std::string out;
            out.reserve(std::min(raw.size(), kPreviewLineChars));
            bool pendingSpace = false;
            for (char ch : raw) {
                const unsigned char c = static_cast<unsigned char>(ch);
                if (ch == '\t' && keepTabs) {
                    pendingSpace = false;
                    out.push_back('\t');
                    continue;
                }
                if (c < 0x20 || c == 0x7F) { pendingSpace = !out.empty(); continue; }
                if (ch == ' ') { pendingSpace = !out.empty(); continue; }
                if (pendingSpace) { out.push_back(' '); pendingSpace = false; }
                out.push_back(ch);
                if (out.size() >= kPreviewLineChars) break;
            }
            return out;
        }

        // Appends a line unless it is empty or the snippet is already full.
        // Leading blank lines of a file are skipped so the preview starts on
        // content instead of on the file's top margin.
        void AppendPreviewLine(std::vector<std::string>& lines,
                               const std::string& raw, bool keepTabs = false) {
            if (lines.size() >= kPreviewMaxLines) return;
            std::string tidy = TidyPreviewLine(raw, keepTabs);
            if (tidy.empty()) return;
            lines.push_back(std::move(tidy));
        }

        void SplitPreviewLines(const std::string& text,
                               std::vector<std::string>& lines) {
            size_t pos = 0;
            while (pos <= text.size() && lines.size() < kPreviewMaxLines) {
                size_t nl = text.find('\n', pos);
                AppendPreviewLine(lines, text.substr(pos, nl == std::string::npos
                                                              ? std::string::npos
                                                              : nl - pos));
                if (nl == std::string::npos) break;
                pos = nl + 1;
            }
        }

        // The five predefined XML / HTML entities plus numeric references —
        // everything else is left as written, which is harmless in a preview.
        std::string DecodeEntities(const std::string& in) {
            std::string out;
            out.reserve(in.size());
            for (size_t i = 0; i < in.size(); ++i) {
                if (in[i] != '&') { out.push_back(in[i]); continue; }
                size_t end = in.find(';', i + 1);
                if (end == std::string::npos || end - i > 10) { out.push_back('&'); continue; }
                const std::string name = in.substr(i + 1, end - i - 1);
                if      (name == "amp")  out.push_back('&');
                else if (name == "lt")   out.push_back('<');
                else if (name == "gt")   out.push_back('>');
                else if (name == "quot") out.push_back('"');
                else if (name == "apos") out.push_back('\'');
                else if (name == "nbsp") out.push_back(' ');
                else if (!name.empty() && name[0] == '#') out.push_back(' ');
                else { out.push_back('&'); continue; }
                i = end;
            }
            return out;
        }

        // Text of a markup document with the tags removed. `breakTags` names
        // the elements that end a preview line (paragraphs, headings, rows);
        // everything else is treated as inline. `<script>` / `<style>` bodies
        // are dropped so an HTML preview shows the page, not its code.
        void MarkupToPreviewLines(const std::string& markup,
                                  const std::vector<std::string>& breakTags,
                                  std::vector<std::string>& lines) {
            auto isBreakTag = [&](const std::string& tag) {
                for (const std::string& b : breakTags) {
                    if (tag == b || tag.rfind(b + " ", 0) == 0) return true;
                }
                return false;
            };
            std::string current;
            for (size_t i = 0; i < markup.size() && lines.size() < kPreviewMaxLines;) {
                if (markup[i] != '<') { current.push_back(markup[i++]); continue; }
                size_t end = markup.find('>', i + 1);
                if (end == std::string::npos) break;
                std::string tag = markup.substr(i + 1, end - i - 1);
                i = end + 1;
                if (!tag.empty() && (tag[0] == '!' || tag[0] == '?')) continue;
                const bool closing = !tag.empty() && tag[0] == '/';
                if (closing) tag.erase(0, 1);
                std::string name = tag.substr(0, tag.find_first_of(" \t\r\n/"));
                std::transform(name.begin(), name.end(), name.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                if (!closing && (name == "script" || name == "style")) {
                    const std::string closeTag = "</" + name;
                    size_t skip = markup.find(closeTag, i);
                    i = (skip == std::string::npos) ? markup.size() : skip;
                    continue;
                }
                if (isBreakTag(name) || name == "br") {
                    AppendPreviewLine(lines, DecodeEntities(current));
                    current.clear();
                }
            }
            AppendPreviewLine(lines, DecodeEntities(current));
        }

        // RTF: drop the control words, the groups the reader is meant to skip
        // and the braces, keeping the literal text.
        void RtfToPreviewLines(const std::string& rtf,
                               std::vector<std::string>& lines) {
            std::string current;
            for (size_t i = 0; i < rtf.size() && lines.size() < kPreviewMaxLines;) {
                if (rtf[i] == '\\') {
                    size_t j = i + 1;
                    while (j < rtf.size() && (std::isalpha(static_cast<unsigned char>(rtf[j])))) ++j;
                    const std::string word = rtf.substr(i + 1, j - i - 1);
                    while (j < rtf.size() && (rtf[j] == '-' || std::isdigit(
                            static_cast<unsigned char>(rtf[j])))) ++j;
                    if (j < rtf.size() && rtf[j] == ' ') ++j;
                    if (word == "par" || word == "line" || word == "pard") {
                        AppendPreviewLine(lines, current);
                        current.clear();
                    } else if (word.empty() && j < rtf.size()) {
                        ++j;   // escaped literal (\{, \}, \\)
                    }
                    i = j;
                    continue;
                }
                if (rtf[i] == '{' || rtf[i] == '}') { ++i; continue; }
                current.push_back(rtf[i++]);
            }
            AppendPreviewLine(lines, current);
        }

        // ODF spreadsheet: the first cells of the first rows of content.xml.
        void OdsToPreviewLines(const std::string& contentXml,
                               std::vector<std::string>& lines) {
            size_t pos = 0;
            while (lines.size() < kPreviewMaxLines) {
                size_t rowStart = contentXml.find("<table:table-row", pos);
                if (rowStart == std::string::npos) break;
                size_t rowEnd = contentXml.find("</table:table-row>", rowStart);
                const std::string row = contentXml.substr(
                        rowStart, rowEnd == std::string::npos ? std::string::npos
                                                              : rowEnd - rowStart);
                pos = (rowEnd == std::string::npos) ? contentXml.size()
                                                    : rowEnd + 18;
                std::vector<std::string> cells;
                size_t cpos = 0;
                while (cells.size() < kPreviewMaxColumns) {
                    size_t cellStart = row.find("<table:table-cell", cpos);
                    if (cellStart == std::string::npos) break;
                    size_t cellEnd = row.find("</table:table-cell>", cellStart);
                    if (cellEnd == std::string::npos) {
                        // Self-closing cell: an empty one.
                        cells.emplace_back();
                        cpos = cellStart + 17;
                        continue;
                    }
                    std::vector<std::string> cellText;
                    MarkupToPreviewLines(row.substr(cellStart, cellEnd - cellStart),
                                         {"text:p"}, cellText);
                    cells.push_back(cellText.empty() ? std::string() : cellText.front());
                    cpos = cellEnd + 19;
                }
                std::string joined;
                for (size_t i = 0; i < cells.size(); ++i) {
                    if (i) joined.push_back('\t');
                    joined += cells[i];
                }
                AppendPreviewLine(lines, joined, true);
            }
        }

        // OOXML spreadsheet: the first sheet's rows, with the string cells
        // resolved through the shared string table.
        void XlsxToPreviewLines(const UCZipPackageReader& zip,
                                std::vector<std::string>& lines) {
            std::string sheetXml;
            if (!zip.ReadEntry("xl/worksheets/sheet1.xml", sheetXml)) return;

            std::vector<std::string> shared;
            std::string sharedXml;
            if (zip.ReadEntry("xl/sharedStrings.xml", sharedXml)) {
                size_t pos = 0;
                while (true) {
                    size_t si = sharedXml.find("<si", pos);
                    if (si == std::string::npos) break;
                    size_t siEnd = sharedXml.find("</si>", si);
                    if (siEnd == std::string::npos) break;
                    std::vector<std::string> parts;
                    MarkupToPreviewLines(sharedXml.substr(si, siEnd - si), {}, parts);
                    shared.push_back(parts.empty() ? std::string() : parts.front());
                    pos = siEnd + 5;
                }
            }

            size_t pos = 0;
            while (lines.size() < kPreviewMaxLines) {
                size_t rowStart = sheetXml.find("<row", pos);
                if (rowStart == std::string::npos) break;
                size_t rowEnd = sheetXml.find("</row>", rowStart);
                if (rowEnd == std::string::npos) break;
                const std::string row = sheetXml.substr(rowStart, rowEnd - rowStart);
                pos = rowEnd + 6;

                std::vector<std::string> cells;
                size_t cpos = 0;
                while (cells.size() < kPreviewMaxColumns) {
                    size_t cellStart = row.find("<c", cpos);
                    if (cellStart == std::string::npos) break;
                    size_t tagEnd = row.find('>', cellStart);
                    if (tagEnd == std::string::npos) break;
                    const std::string attrs = row.substr(cellStart, tagEnd - cellStart);
                    const bool sharedString = attrs.find("t=\"s\"") != std::string::npos;
                    size_t cellEnd = row.find("</c>", tagEnd);
                    if (row[tagEnd - 1] == '/' || cellEnd == std::string::npos) {
                        cells.emplace_back();
                        cpos = tagEnd + 1;
                        continue;
                    }
                    std::vector<std::string> value;
                    MarkupToPreviewLines(row.substr(tagEnd + 1, cellEnd - tagEnd - 1),
                                         {}, value);
                    std::string text = value.empty() ? std::string() : value.front();
                    if (sharedString) {
                        const long idx = std::strtol(text.c_str(), nullptr, 10);
                        text = (idx >= 0 && static_cast<size_t>(idx) < shared.size())
                                       ? shared[static_cast<size_t>(idx)]
                                       : std::string();
                    }
                    cells.push_back(std::move(text));
                    cpos = cellEnd + 4;
                }
                std::string joined;
                for (size_t i = 0; i < cells.size(); ++i) {
                    if (i) joined.push_back('\t');
                    joined += cells[i];
                }
                AppendPreviewLine(lines, joined, true);
            }
        }

        // CSV / TSV: the separator becomes a tab so the drawing code lays the
        // values out as cells. Quoted fields keep their separators.
        void DelimitedToPreviewLines(const std::string& text, char separator,
                                     std::vector<std::string>& lines) {
            size_t pos = 0;
            while (pos <= text.size() && lines.size() < kPreviewMaxLines) {
                size_t nl = text.find('\n', pos);
                const std::string row = text.substr(
                        pos, nl == std::string::npos ? std::string::npos : nl - pos);
                std::string cells;
                bool quoted = false;
                size_t columns = 1;
                for (char ch : row) {
                    if (ch == '"') { quoted = !quoted; continue; }
                    if (ch == separator && !quoted) {
                        if (++columns > kPreviewMaxColumns) break;
                        cells.push_back('\t');
                        continue;
                    }
                    cells.push_back(ch);
                }
                AppendPreviewLine(lines, cells, true);
                if (nl == std::string::npos) break;
                pos = nl + 1;
            }
        }

        // Word-processing formats go through the shared rich-document reader,
        // so ODT, DOCX and legacy DOC all preview from the same block model.
        void RichDocumentToPreviewLines(const std::string& path,
                                        std::vector<std::string>& lines) {
            UCRichDocument doc;
            std::string error;
            if (!UCWordDocumentIO::Load(path, doc, error)) return;
            for (const RichDocBlock& block : doc.blocks) {
                if (lines.size() >= kPreviewMaxLines) break;
                std::string text;
                for (const RichTextRun& run : block.runs) {
                    if (run.lineBreakBefore && !text.empty()) text.push_back(' ');
                    text += run.text;
                    if (text.size() > kPreviewLineChars) break;
                }
                if (text.empty() && !block.tableRows.empty()) {
                    for (const RichTableRow& row : block.tableRows) {
                        if (lines.size() >= kPreviewMaxLines) break;
                        std::string joined;
                        size_t column = 0;
                        for (const RichTableCell& cell : row.cells) {
                            if (++column > kPreviewMaxColumns) break;
                            if (column > 1) joined.push_back('\t');
                            for (const RichTextRun& run : cell.runs) joined += run.text;
                        }
                        AppendPreviewLine(lines, joined, true);
                    }
                    continue;
                }
                AppendPreviewLine(lines, text);
            }
        }

        // Fills `lines` (and `tabular`) with the start of the file's content.
        // False when this build cannot read the format — the entry then keeps
        // its type glyph instead of showing an empty page.
        bool ExtractTextPreview(const std::string& path,
                                std::vector<std::string>& lines, bool& tabular) {
            const std::string ext = LowerExtension(path);
            tabular = false;

            if (ext == "csv" || ext == "tsv") {
                tabular = true;
                DelimitedToPreviewLines(ReadFileHead(path, kPreviewReadBytes),
                                        ext == "tsv" ? '\t' : ',', lines);
                return true;
            }
            if (ext == "ods" || ext == "xlsx") {
                UCZipPackageReader zip;
                if (!zip.Open(path)) return false;
                tabular = true;
                if (ext == "ods") {
                    std::string contentXml;
                    if (!zip.ReadEntry("content.xml", contentXml)) return false;
                    OdsToPreviewLines(contentXml, lines);
                } else {
                    XlsxToPreviewLines(zip, lines);
                }
                return true;
            }
            if (ext == "odt" || ext == "docx" || ext == "doc") {
                RichDocumentToPreviewLines(path, lines);
                return true;
            }
            if (ext == "html" || ext == "htm") {
                MarkupToPreviewLines(ReadFileHead(path, kPreviewReadBytes),
                                     {"p", "div", "li", "tr", "h1", "h2", "h3",
                                      "h4", "h5", "h6", "title"}, lines);
                return true;
            }
            if (ext == "rtf") {
                RtfToPreviewLines(ReadFileHead(path, kPreviewReadBytes), lines);
                return true;
            }
            // Everything else in the Text / Docs kinds is plain text already
            // (txt, log, json, xml, yaml, markdown, LaTeX, source code, ...).
            // Binary formats we have no reader for (xls, epub) come back empty
            // from ReadFileHead and fall back to the glyph.
            const std::string head = ReadFileHead(path, kPreviewReadBytes);
            if (head.empty()) return false;
            SplitPreviewLines(head, lines);
            return true;
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

        // Parse the OS association database on its background worker while
        // the first folder is still scanning, so the context menu's
        // "Open with >" is a cache read by the time it can be opened.
        FileAssociations::PrewarmAsync();
    }

    UltraCanvasFilerWidget::~UltraCanvasFilerWidget() {
        CancelPendingRename();      // the timer callback captures `this`
        // Detach the rename editor now: its callbacks capture `this`, and the
        // container teardown dropping its focus must not commit into a
        // half-destroyed widget.
        DestroyRenameInput(false);
        // Same for the compress dialog's name editor and its window key filter:
        // both hold callbacks bound to `this`.
        RemoveCompressKeyFilter();
        DestroyCompressNameInput();
        DestroyCompressButtons();
        if (dragMouseCaptured) {    // never leave the pointer grabbed
            if (auto* app = UltraCanvasApplication::GetInstance()) app->ReleaseMouse();
            dragMouseCaptured = false;
        }
        HideDragOverlay();          // its renderer captures `this`
        thumbAlive->store(false);   // neutralize queued cross-thread redraws
        StopThumbnailWorkers();
        StopFolderWatchTimer();     // its callback captures `this`
        // A pack / unpack still running: ask it to stop, then wait for it. The
        // worker holds no widget state, but its thread must not outlive the
        // widget whose poll timer would report it.
        if (archiveJob) {
            archiveJob->cancelRequested.store(true);
            archiveJob->onFinished = nullptr;   // nothing left to refresh
            FinishArchiveJob();
        }
        StopFolderStatsWorker();
        StopFolderPrefetchWorker();
        StopFolderWatchWorker();
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
        // Dot names plus the platform's own notion of hidden (the attribute
        // bit on Windows, UF_HIDDEN on macOS) - so NTUSER.DAT and the
        // profile-folder compatibility junctions filter like dot files do.
        e.isHidden = IsHiddenFileSystemEntry(fs::path(path));
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

            // A dot name is hidden on every platform - skip it before paying
            // for the metadata call. Entries hidden by attribute (Windows) or
            // file flag (macOS) are only recognisable from that call and are
            // skipped below.
            if (e.isHidden && !includeHidden) continue;

            // One metadata call per entry: type, size, times, the write bit
            // and (Windows / macOS) the hidden state all come from the same
            // call. A second lookup per file doubled the scan time on a big
            // or network folder, so nothing here may add one.
#if defined(_WIN32) || defined(_WIN64)
            // ::stat cannot see the attribute bits, so the one call is
            // GetFileAttributesExW - which is also what makes NTUSER.DAT and
            // the hidden "Anwendungsdaten"-style profile junctions filter
            // here the way Explorer filters them.
            WIN32_FILE_ATTRIBUTE_DATA fad{};
            if (GetFileAttributesExW(it->path().c_str(), GetFileExInfoStandard,
                                     &fad)) {
                e.isHidden = e.isHidden ||
                        (fad.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
                if (e.isHidden && !includeHidden) continue;
                e.isDirectory =
                        (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                if (!e.isDirectory)
                    e.size = (uint64_t(fad.nFileSizeHigh) << 32) |
                             fad.nFileSizeLow;
                auto toTimeT = [](const FILETIME& ft) {
                    ULARGE_INTEGER u;
                    u.LowPart = ft.dwLowDateTime;
                    u.HighPart = ft.dwHighDateTime;
                    // FILETIME epoch (1601) to Unix epoch, 100ns to seconds.
                    return time_t((u.QuadPart - 116444736000000000ULL) /
                                  10000000ULL);
                };
                e.modifiedTime = toTimeT(fad.ftLastWriteTime);
                e.createdTime = toTimeT(fad.ftCreationTime);
                e.isReadOnly =
                        (fad.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
            }
#else
            struct stat st{};
            if (::stat(e.path.c_str(), &st) == 0) {
#ifdef UF_HIDDEN
                // macOS / BSD: the Finder-hidden flag (chflags hidden) - what
                // keeps ~/Library out of sight there.
                e.isHidden = e.isHidden || (st.st_flags & UF_HIDDEN) != 0;
                if (e.isHidden && !includeHidden) continue;
#endif
                e.isDirectory = (st.st_mode & S_IFMT) == S_IFDIR;
                if (!e.isDirectory)
                    e.size = static_cast<uint64_t>(st.st_size);
                e.modifiedTime = st.st_mtime;
                e.createdTime = st.st_ctime;
                e.isReadOnly = (st.st_mode & S_IWUSR) == 0;
            }
#endif
            else {
                // Broken symlink or a name the metadata call cannot resolve
                // (e.g. a non-ACP name on Windows): keep the iterator's
                // cached view so the entry still lists with its type and size.
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
        for (size_t idx : selection) {
            if (idx >= entries.size()) continue;
            // A just-committed rename moved one of them: follow it to its new
            // name instead of losing it (CommitRename sets the pair), so the
            // renamed entry stays selected and the restore below counts it as
            // unchanged — no spurious selection-changed for the host.
            const std::string& path = entries[idx].path;
            selectedPaths.insert((!renamedFromPath.empty() && path == renamedFromPath)
                                 ? renamedToPath : path);
        }
        // Kept for the reveal below: the new name can sort anywhere in the
        // listing, so the entry the user just renamed is scrolled back into
        // view rather than left wherever the new order put it.
        const std::string renamedTo = renamedToPath;
        renamedFromPath.clear();
        renamedToPath.clear();

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

        // And its distinct file extensions for the "Open with >" prewarm, so
        // a right-click finds the OS application lists already resolved.
        if (systemOpenWith) {
            std::unordered_set<std::string> distinct;
            for (const FilerEntry& e : entries)
                if (!e.isDirectory && !e.extension.empty())
                    distinct.insert(e.extension);
            if (!distinct.empty())
                FileAssociations::PrewarmExtensionsAsync(
                        {distinct.begin(), distinct.end()});
        }

        // Point the folder watch at what is now on screen. Done after the scan
        // so the fingerprint the worker takes describes the listing the user
        // is looking at, not the one that was there a moment ago.
        WatchFolder((fileListMode || !isRealDir) ? std::string() : currentPath);

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

    // ===== SELECTIVE PREVIEWS =====
    FilerPreviewType UltraCanvasFilerWidget::PreviewTypeOf(const FilerEntry& e) {
        if (e.isDirectory) return FilerPreviewType::NonePreview;
        return PreviewTypeForFile(e.extension, e.category);
    }

    bool UltraCanvasFilerWidget::PreviewEnabledFor(const FilerEntry& e) const {
        const FilerPreviewType type = PreviewTypeOf(e);
        // Entries of no preview kind (folders, audio, archives, programs) are
        // never gated: a host that attached an explicit thumbnail to one still
        // gets it drawn.
        if (type == FilerPreviewType::NonePreview) return true;
        return (previewTypes & static_cast<uint32_t>(type)) != 0;
    }

    void UltraCanvasFilerWidget::SetPreviewType(FilerPreviewType type, bool on) {
        const uint32_t bit = static_cast<uint32_t>(type);
        SetPreviewTypes(on ? (previewTypes | bit) : (previewTypes & ~bit));
    }

    bool UltraCanvasFilerWidget::IsPreviewTypeEnabled(FilerPreviewType type) const {
        return (previewTypes & static_cast<uint32_t>(type)) != 0;
    }

    bool UltraCanvasFilerWidget::PreviewFitsRect(const FilerEntry& e,
                                                 const Rect2Di& rect) {
        switch (PreviewTypeOf(e)) {
            case FilerPreviewType::PDF:
            case FilerPreviewType::Models3D:
                return rect.width >= kContentPreviewMinEdge &&
                       rect.height >= kContentPreviewMinEdge;
            default:
                // Bitmaps, vectors and video poster frames read fine even in
                // the icon column of a Details row.
                return true;
        }
    }

    void UltraCanvasFilerWidget::SetPreviewTypes(uint32_t mask) {
        mask &= kFilerAllPreviewTypes;
        if (previewTypes == mask) return;
        previewTypes = mask;
        // A tile that falls back to the type glyph fills its square again, so
        // the shrink-to-image row heights of the thumbnail grid change with it.
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

    void UltraCanvasFilerWidget::DeselectPathsForModification(
            const std::vector<std::string>& paths) {
        if (selection.empty() || paths.empty()) return;
        std::vector<size_t> keep;
        keep.reserve(selection.size());
        for (size_t idx : selection) {
            if (idx >= entries.size()) continue;
            if (std::find(paths.begin(), paths.end(), entries[idx].path) == paths.end())
                keep.push_back(idx);
        }
        if (keep.size() == selection.size()) return;   // none of them selected
        selection = std::move(keep);
        // Synchronous: a host preview pane closes the file inside this call.
        FireSelectionChanged();
        RequestRedraw();
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
        dragNativeRefused = false;
        dragPos = localPoint;
        dragDropFolderIndex = DragDropFolderAt(localPoint);
        if (hoveredIndex != -1) hoveredIndex = -1;
        SetMouseCursor(UCMouseCursor::Hand);
        MeasureDragBadge();
        UpdateDragOverlay(localPoint);
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::UpdateItemDrag(const Point2Di& localPoint) {
        dragPos = localPoint;

        // Left the window: the same set continues as a native OS drag, so it
        // can be dropped on any other window or application. Crossing the
        // widget's own border does NOT end the drag — the badge simply keeps
        // travelling over the rest of the window (see UpdateDragOverlay).
        Point2Di windowPoint = ToWindowPoint(localPoint);
        if (!IsInsideWindow(windowPoint) && !dragNativeRefused) {
            std::vector<std::string> paths = dragPaths;
            EndDragGesture();
            if (!StartNativeDragOfPaths(paths)) {
                // No native drag available (no window / no implementation on
                // this platform / refused grab): keep our own drag running so
                // the gesture is not lost, and stop asking for this gesture.
                draggingItems = true;
                dragNativeRefused = true;
                dragPaths = paths;
                dragPos = localPoint;
                if (auto* app = UltraCanvasApplication::GetInstance()) {
                    app->CaptureMouse(this);
                    dragMouseCaptured = true;
                }
                UpdateDragOverlay(localPoint);
            }
            RequestRedraw();
            return;
        }

        // Inside the widget the folder under the cursor is the drop target;
        // outside it there is none to highlight.
        auto lb = GetLocalBounds();
        Rect2Di local(static_cast<int>(lb.x), static_cast<int>(lb.y),
                      static_cast<int>(lb.width), static_cast<int>(lb.height));
        int folder = local.Contains(localPoint) ? DragDropFolderAt(localPoint) : -1;
        if (folder != dragDropFolderIndex) dragDropFolderIndex = folder;
        UpdateDragOverlay(localPoint);
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::FinishItemDrag(const Point2Di& localPoint,
                                                bool copy) {
        std::vector<std::string> paths = dragPaths;
        auto lb = GetLocalBounds();
        Rect2Di local(static_cast<int>(lb.x), static_cast<int>(lb.y),
                      static_cast<int>(lb.width), static_cast<int>(lb.height));
        bool inWidget = local.Contains(localPoint);
        int folder = inWidget ? DragDropFolderAt(localPoint) : -1;
        std::string destDir = (folder >= 0 && folder < static_cast<int>(entries.size()))
                ? entries[folder].path : std::string();
        Point2Di windowPoint = ToWindowPoint(localPoint);
        EndDragGesture();
        RequestRedraw();
        if (!paths.empty()) {
            if (!destDir.empty()) {
                DropPathsInto(paths, destDir, copy);
            } else if (!inWidget && IsInsideWindow(windowPoint)) {
                // Released over another element of this window (a second filer
                // pane, a folder tree, ...): offer it the files the same way an
                // external drop would.
                DeliverInWindowDrop(windowPoint, paths);
            }
            // A drop on nothing (empty space of this view, outside the window
            // with no native drag) just ends the drag.
        }
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
        HideDragOverlay();
        draggingItems = false;
        dragOutArmed = false;
        dragNativeRefused = false;
        dragPressIndex = -1;
        dragDropFolderIndex = -1;
        dragPaths.clear();
        dragLabel.clear();
    }

    Point2Di UltraCanvasFilerWidget::ToWindowPoint(const Point2Di& localPoint) const {
        Point2Df origin = GetPositionInWindow();
        return Point2Di(localPoint.x + static_cast<int>(origin.x),
                        localPoint.y + static_cast<int>(origin.y));
    }

    bool UltraCanvasFilerWidget::IsInsideWindow(const Point2Di& windowPoint) const {
        auto* win = GetWindow();
        if (!win) return false;
        int w = 0, h = 0;
        win->GetWindowSize(w, h);
        return windowPoint.x >= 0 && windowPoint.y >= 0 &&
               windowPoint.x < w && windowPoint.y < h;
    }

    void UltraCanvasFilerWidget::MeasureDragBadge() {
        // The label is fixed for the whole gesture, so one measurement is
        // enough; without a render context (widget not on a window yet) an
        // estimate keeps the badge roughly the right size.
        const int iconSz = 20, padX = 8, padY = 6, gap = 6;
        Size2Di ts(static_cast<int>(dragLabel.size()) * 7, 16);
        if (IRenderContext* ctx = GetRenderContext()) {
            FontStyle fsty;
            fsty.fontFamily = style.fontFamily;
            fsty.fontSize = style.smallFontSize;
            ctx->PushState();
            ctx->SetFontStyle(fsty);
            ts = ctx->GetTextLineDimensions(EllipsizeText(ctx, dragLabel, 220));
            ctx->PopState();
        }
        dragBadgeSize = Size2Di(padX * 2 + iconSz + gap + ts.width,
                                std::max(iconSz, ts.height) + padY * 2);
    }

    Rect2Di UltraCanvasFilerWidget::DragBadgeRect(const Point2Di& windowPoint) const {
        int bw = dragBadgeSize.width, bh = dragBadgeSize.height;
        int bx = windowPoint.x + 14;
        int by = windowPoint.y + 14;
        auto* win = GetWindow();
        if (win) {
            // Keep it on screen: flipped above the cursor at the bottom edge.
            int ww = 0, wh = 0;
            win->GetWindowSize(ww, wh);
            if (bx + bw > ww) bx = ww - bw;
            if (by + bh > wh) by = windowPoint.y - bh - 6;
            if (bx < 0) bx = 0;
            if (by < 0) by = 0;
        }
        return Rect2Di(bx, by, bw, bh);
    }

    void UltraCanvasFilerWidget::UpdateDragOverlay(const Point2Di& localPoint) {
        auto* win = GetWindow();
        if (!win || !draggingItems || dragPaths.empty()) return;
        Rect2Di badge = DragBadgeRect(ToWindowPoint(localPoint));
        win->SetDragOverlay(this, badge,
                [this](IRenderContext* ctx, const Rect2Di& rect) {
                    DrawDragBadge(ctx, rect);
                });
        dragOverlayShown = true;
    }

    void UltraCanvasFilerWidget::HideDragOverlay() {
        if (!dragOverlayShown) return;
        dragOverlayShown = false;
        if (auto* win = GetWindow()) win->ClearDragOverlay(this);
    }

    void UltraCanvasFilerWidget::DeliverInWindowDrop(
            const Point2Di& windowPoint, const std::vector<std::string>& paths) {
        auto* win = GetWindow();
        auto* app = UltraCanvasApplication::GetInstance();
        if (!win || !app || paths.empty()) return;

        UCEvent drop;
        drop.type = UCEventType::Drop;
        drop.targetWindow = win->GetWindowWeakPtr();
        drop.nativeWindowHandle = win->GetNativeHandle();
        drop.pointerWindow = windowPoint;
        drop.pointer = windowPoint;
        drop.droppedFiles = paths;
        drop.dragMimeType = "text/uri-list";
        std::string joined;
        for (const std::string& p : paths) {
            if (!joined.empty()) joined += "\n";
            joined += p;
        }
        drop.dragData = joined;
        // Queued, not dispatched inline: the release that produced it is still
        // being handled, and the drop is routed to the element under the
        // cursor exactly like a drop arriving from another application.
        app->PushEvent(drop);
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

        // Entries the drop cannot mean: the target itself, and entries
        // already living in the target (a move would be a no-op, a copy
        // would just litter it with a duplicate). Everything else goes
        // through the paste machinery, so a taken name raises the conflict
        // dialog and a failure the retry dialog; the folder-into-itself
        // guard lives there too.
        std::vector<std::string> sources;
        for (const std::string& src : paths) {
            ec.clear();
            fs::path canonicalFrom = fs::weakly_canonical(fs::path(src), ec);
            if (canonicalFrom == canonicalDest) continue;
            if (canonicalFrom.parent_path() == canonicalDest) continue;
            sources.push_back(src);
        }
        if (sources.empty()) return;
        PasteFilesInto(destDir, std::move(sources), /*cut=*/!copy,
                       [this, destDir, copy](bool changed) {
            if (!changed) return;
            Refresh();
            NotifyFolderModified(destDir);
            // A move also emptied the folder the files came from.
            if (!copy) NotifyFolderModified();
        });
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

        // Skip files already in this folder and the folder itself; the rest
        // goes through the paste machinery, so a taken name raises the
        // conflict dialog and the folder-into-itself guard applies there.
        fs::path canonicalHere = fs::weakly_canonical(fs::path(currentPath), ec);
        std::vector<std::string> sources;
        for (const std::string& src : paths) {
            ec.clear();
            fs::path canonicalFrom = fs::weakly_canonical(fs::path(src), ec);
            if (canonicalFrom == canonicalHere) continue;
            if (canonicalFrom.parent_path() == canonicalHere) continue;
            sources.push_back(src);
        }
        if (sources.empty()) return;
        PasteFilesInto(currentPath, std::move(sources), /*cut=*/false);
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

        PasteFilesInto(currentPath, std::move(paths), cut,
                       [this, cut](bool changed) {
            // A cut is consumed by its paste, even a partially skipped one.
            if (cut) { clipboardPaths.clear(); clipboardCut = false; }
            Refresh();
            if (changed) NotifyFolderModified();
        });
    }

    namespace {
        // Is `path` equal to `ancestor` or somewhere below it?
        bool PathIsSameOrBelow(const std::string& path, const std::string& ancestor) {
            if (ancestor.empty() || path.size() < ancestor.size()) return false;
            if (path.compare(0, ancestor.size(), ancestor) != 0) return false;
            return path.size() == ancestor.size() ||
                   path[ancestor.size()] == '/' || path[ancestor.size()] == '\\' ||
                   ancestor.back() == '/' || ancestor.back() == '\\';
        }

        // Adds `labels` to `dialog` as a group of exclusive switches:
        // toggling one on turns the others off, the selected one cannot be
        // toggled off — only replaced by another — and `onSelect(index)`
        // follows the selection. `checkedIndex` starts selected.
        void AddExclusiveSwitches(UltraCanvasModalDialog* dialog,
                                  const std::string& idPrefix,
                                  const std::vector<std::string>& labels,
                                  size_t checkedIndex,
                                  std::function<void(size_t)> onSelect) {
            auto switches = std::make_shared<std::vector<UltraCanvasSwitch*>>();
            auto selected = std::make_shared<size_t>(checkedIndex);
            for (size_t i = 0; i < labels.size(); ++i) {
                auto sw = UltraCanvasSwitch::Create(
                        idPrefix + std::to_string(i), 0, 0, labels[i],
                        i == checkedIndex);
                sw->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                switches->push_back(sw.get());
                UltraCanvasSwitch* me = sw.get();
                sw->onChecked = [switches, selected, onSelect, me, i]() {
                    *selected = i;
                    if (onSelect) onSelect(i);
                    for (UltraCanvasSwitch* other : *switches)
                        if (other != me) other->SetChecked(false);
                };
                sw->onUnchecked = [selected, me, i]() {
                    if (*selected == i) me->SetChecked(true);
                };
                dialog->AddDialogElement(sw);
            }
        }
    }

    bool UltraCanvasFilerWidget::ShowProceedSkipDialog(
            DialogConfig& cfg,
            const std::string& proceedLabel, const std::string& skipLabel,
            const std::string& allLabel, bool proceedDefault,
            std::function<void(bool proceed, bool all)> onContinue,
            std::function<void()> onCancel) {
        cfg.buttons = DialogButtons::NoButtons;   // custom buttons added below
        auto dialog = UltraCanvasDialogManager::CreateDialog(cfg);
        if (!dialog) return false;

        struct Choice { bool proceed = true; bool all = false; };
        auto choice = std::make_shared<Choice>();
        choice->proceed = proceedDefault;
        AddExclusiveSwitches(dialog.get(), "FilerProblemOpt",
                {proceedLabel, skipLabel}, proceedDefault ? 0 : 1,
                [choice](size_t index) { choice->proceed = (index == 0); });

        // Scope: ask again on the next problem (off, the default) or apply
        // this choice to the remaining entries of the operation.
        auto allSwitch = UltraCanvasSwitch::Create(
                "FilerProblemAll", 0, 0, allLabel, false);
        allSwitch->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        allSwitch->onStateChanged = [choice](CheckedState, CheckedState state) {
            choice->all = (state == CheckedState::Checked);
        };
        dialog->AddDialogElement(allSwitch);

        dialog->AddCustomButton("Continue", DialogResult::Yes, nullptr);
        dialog->AddCustomButton("Cancel", DialogResult::Cancel, nullptr);
        dialog->onResult = [choice, onContinue, onCancel](DialogResult result) {
            if (result == DialogResult::Yes) {
                if (onContinue) onContinue(choice->proceed, choice->all);
            } else if (onCancel) {
                onCancel();
            }
        };
        UltraCanvasDialogManager::ShowDialog(dialog, nullptr, GetWindow());
        return true;
    }

    void UltraCanvasFilerWidget::PasteFilesInto(std::string folder,
                                                std::vector<std::string> paths,
                                                bool cut,
                                                std::function<void(bool changed)> onDone) {
        if (pendingPaste) return;   // one paste (and its dialog) at a time
        std::error_code ec;
        if (!fs::is_directory(folder, ec)) {
            ReportError("Paste target is not a writable folder: " + folder);
            if (onDone) onDone(false);
            return;
        }
        // A move renames the source away, which fails while something still
        // holds it open - the host's preview pane being the usual culprit.
        // Letting the sources go out of the selection closes that preview
        // before the first rename is attempted.
        if (cut) DeselectPathsForModification(paths);
        pendingPaste = std::make_unique<PendingPaste>();
        pendingPaste->folder = std::move(folder);
        pendingPaste->sources = std::move(paths);
        pendingPaste->cut = cut;
        pendingPaste->onDone = std::move(onDone);
        ContinuePendingPaste();
    }

    void UltraCanvasFilerWidget::ContinuePendingPaste() {
        std::error_code ec;
        while (pendingPaste && pendingPaste->next < pendingPaste->sources.size()) {
            PendingPaste& pp = *pendingPaste;
            const std::string& src = pp.sources[pp.next];
            const fs::path from(src);
            if (!fs::exists(from, ec)) { ++pp.next; continue; }
            // Cut-pasting into the folder the file already lives in is a no-op,
            // and a folder must never be pasted into itself.
            if (pp.cut && from.parent_path() == fs::path(pp.folder)) { ++pp.next; continue; }
            if (fs::is_directory(from, ec) && PathIsSameOrBelow(pp.folder, src)) {
                ReportError("Cannot paste a folder into itself: " + src);
                ++pp.next;
                continue;
            }
            const std::string dest = (fs::path(pp.folder) / from.filename()).string();
            // Copy-pasting alongside the original never asks — the copy simply
            // takes the next free name, exactly like Duplicate.
            if (fs::exists(dest, ec) && dest != src) {
                if (!pp.applyToAll) { ShowPasteConflictDialog(src); return; }
                if (!PasteCurrentAndAdvance(pp.action)) return;
            } else {
                if (!PasteCurrentAndAdvance(PasteConflictAction::KeepBoth)) return;
            }
        }
        FinishPendingPaste();
    }

    void UltraCanvasFilerWidget::FinishPendingPaste() {
        if (!pendingPaste) return;
        const bool changed = pendingPaste->changed;
        std::function<void(bool)> onDone = std::move(pendingPaste->onDone);
        pendingPaste.reset();
        if (onDone) { onDone(changed); return; }   // the caller owns refresh / history
        Refresh();
        if (changed) NotifyFolderModified();
    }

    bool UltraCanvasFilerWidget::PasteCurrentAndAdvance(PasteConflictAction action) {
        PendingPaste& pp = *pendingPaste;
        pp.currentAction = action;
        const std::string src = pp.sources[pp.next];
        for (;;) {
            std::string why;
            if (PasteOneEntry(src, action, why)) break;   // pasted or skipped
            if (pp.skipFailedForAll) break;               // skip it, silently
            if (pp.retryFailedForAll && !pp.currentRetried) {
                pp.currentRetried = true;   // one silent retry, then ask
                continue;
            }
            ShowPasteProblemDialog(src, why);
            return false;
        }
        ++pp.next;
        pp.currentRetried = false;
        return true;
    }

    bool UltraCanvasFilerWidget::PasteOneEntry(const std::string& src,
                                               PasteConflictAction action,
                                               std::string& whyFailed) {
        whyFailed.clear();
        if (!pendingPaste || action == PasteConflictAction::Skip) return true;
        PendingPaste& pp = *pendingPaste;
        std::error_code ec;
        const fs::path from(src);
        std::string dest = (fs::path(pp.folder) / from.filename()).string();
        if (fs::exists(dest, ec)) {
            if (action == PasteConflictAction::Replace && dest != src) {
                fs::remove_all(dest, ec);
                if (ec) {
                    whyFailed = ec.message();
                    return false;
                }
            } else {   // keep both (also a copy pasted alongside its original)
                dest = UniquePathIn(pp.folder, from.filename().string());
            }
        }
        ec.clear();
        if (pp.cut) {
            fs::rename(from, dest, ec);
            if (ec) {
                // Either the two paths are on different volumes - where a
                // rename cannot work and copy + delete is the move - or the
                // rename was refused outright (the usual reason: something
                // still holds the file open). Keep the rename's own error:
                // when the fallback fails too, that is the one that names the
                // real cause.
                const std::error_code renameError = ec;
                std::error_code fallback;
                fs::copy(from, dest, fs::copy_options::recursive, fallback);
                if (fallback) {
                    whyFailed = renameError.message();
                    return false;
                }
                fs::remove_all(from, fallback);
                if (fallback) {
                    // The copy landed but the original would not go: undo the
                    // copy, so a move that failed does not leave the entry in
                    // both places.
                    std::error_code cleanup;
                    fs::remove_all(dest, cleanup);
                    whyFailed = renameError.message();
                    return false;
                }
                ec.clear();
            }
        } else {
            fs::copy(from, dest, fs::copy_options::recursive, ec);
        }
        if (ec) {
            whyFailed = ec.message();
            return false;
        }
        pp.changed = true;
        return true;
    }

    void UltraCanvasFilerWidget::ShowPasteProblemDialog(const std::string& src,
                                                        const std::string& reason) {
        std::error_code ec;
        const std::string name = fs::path(src).filename().string();
        const std::string kind = fs::is_directory(src, ec) ? "folder" : "file";

        const bool moving = pendingPaste && pendingPaste->cut;
        const std::string verb   = moving ? "moved" : "copied";
        const std::string folder = pendingPaste ? pendingPaste->folder : std::string();

        DialogConfig cfg;
        cfg.title = moving ? "Cannot Move" : "Cannot Copy";
        cfg.dialogType = DialogType::Warning;
        cfg.message = "The " + kind + " \"" + name + "\" could not be " + verb + ".";
        // The whole failure, spelled out: what was attempted, on which paths,
        // and the operating system's own words for why it did not work. Paths
        // go in code spans so a Windows backslash survives the Markdown pass.
        cfg.details =
                "**Reason:** "
                + (reason.empty() ? std::string("unknown error") : reason) + "\n\n"
                + "**" + (moving ? std::string("Move") : std::string("Copy")) + ":** `"
                + src + "`\n\n"
                + "**Into:** `"
                + (folder.empty() ? std::string("(unknown folder)") : folder) + "`\n\n"
                + "A " + kind + " that another program still holds open - or that is "
                  "still being shown in a preview - cannot be " + verb + " until that "
                  "program lets go of it. Close it and choose \"Try again\", or skip "
                  "this " + kind + ".";
        cfg.width = 620;
        cfg.height = 340;

        auto self = this;
        const bool shown = ShowProceedSkipDialog(cfg,
                "Try again", "Skip this " + kind,
                "Do this for all remaining items",
                /*proceedDefault=*/true,
                [self](bool proceed, bool all) {
                    if (!self->pendingPaste) return;
                    PendingPaste& pp = *self->pendingPaste;
                    bool resumed;
                    if (!proceed) {
                        if (all) pp.skipFailedForAll = true;
                        ++pp.next;
                        pp.currentRetried = false;
                        resumed = true;
                    } else {
                        // Try again now; a stored "for all" grants every later
                        // failing entry one silent retry before asking again.
                        if (all) pp.retryFailedForAll = true;
                        pp.currentRetried = true;
                        resumed = self->PasteCurrentAndAdvance(pp.currentAction);
                    }
                    if (resumed) self->ContinuePendingPaste();
                },
                [self]() {
                    // Cancel keeps what was already pasted and drops the rest.
                    self->FinishPendingPaste();
                });
        if (!shown) {   // dialogs disabled — the old fixed behavior
            ReportError((moving ? std::string("Move") : std::string("Copy"))
                        + " failed for " + src + ": " + reason);
            ++pendingPaste->next;
            pendingPaste->currentRetried = false;
            ContinuePendingPaste();
        }
    }

    void UltraCanvasFilerWidget::ShowPasteConflictDialog(const std::string& src) {
        std::error_code ec;
        const std::string name = fs::path(src).filename().string();
        const bool isDir = fs::is_directory(src, ec);
        const std::string kind = isDir ? "folder" : "file";

        DialogConfig cfg;
        cfg.title = isDir ? "Folder Already Exists" : "File Already Exists";
        cfg.dialogType = DialogType::Question;
        cfg.message = "A " + kind + " named \"" + name
                    + "\" already exists in this folder.";
        cfg.details = "Choose what to do with the pasted " + kind + ":";
        cfg.buttons = DialogButtons::NoButtons;   // custom buttons added below
        cfg.width = 560;
        cfg.height = 330;

        auto dialog = UltraCanvasDialogManager::CreateDialog(cfg);
        auto self = this;
        if (!dialog) {   // dialogs disabled — keep both, the old fixed behavior
            pendingPaste->action = PasteConflictAction::KeepBoth;
            pendingPaste->applyToAll = true;
            if (PasteCurrentAndAdvance(PasteConflictAction::KeepBoth))
                ContinuePendingPaste();
            return;
        }

        // The action, one switch per choice (the common Keep both / Replace /
        // Skip trio), exclusive. The last dialog's choice is preselected.
        static const PasteConflictAction kActions[3] = {
            PasteConflictAction::KeepBoth, PasteConflictAction::Replace,
            PasteConflictAction::Skip,
        };
        size_t checkedIndex = 0;
        for (size_t i = 0; i < 3; ++i)
            if (kActions[i] == pendingPaste->action) checkedIndex = i;
        AddExclusiveSwitches(dialog.get(), "FilerPasteOpt",
                {"Keep both " + kind + "s (the pasted one is renamed)",
                 "Replace the existing " + kind,
                 "Skip this " + kind},
                checkedIndex,
                [self](size_t index) {
                    if (self->pendingPaste)
                        self->pendingPaste->action = kActions[index];
                });

        // Scope: ask again on the next conflict (off, the default) or apply
        // this choice to every remaining conflict of this paste.
        auto allSwitch = UltraCanvasSwitch::Create(
                "FilerPasteAll", 0, 0, "Do this for all remaining conflicts",
                pendingPaste->applyToAll);
        allSwitch->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        allSwitch->onStateChanged = [self](CheckedState, CheckedState state) {
            if (self->pendingPaste)
                self->pendingPaste->applyToAll = (state == CheckedState::Checked);
        };
        dialog->AddDialogElement(allSwitch);

        // The switches update pendingPaste as they are toggled, so the
        // buttons only decide whether the paste goes on. Escape and the
        // window's close button land in onResult as Cancel / NoResult.
        dialog->AddCustomButton("Continue", DialogResult::Yes, nullptr);
        dialog->AddCustomButton("Cancel", DialogResult::Cancel, nullptr);
        dialog->onResult = [self](DialogResult result) {
            if (!self->pendingPaste) return;
            if (result != DialogResult::Yes) {
                // Cancel keeps what was already pasted and drops the rest.
                self->FinishPendingPaste();
                return;
            }
            if (self->PasteCurrentAndAdvance(self->pendingPaste->action))
                self->ContinuePendingPaste();
        };

        UltraCanvasDialogManager::ShowDialog(dialog, nullptr, GetWindow());
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
        if (pendingDelete) return;   // one delete (and its dialogs) at a time
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

        // Folders that already lost an entry (archive deletions below) —
        // seeds the queue's onFolderModified reports.
        std::vector<std::string> archiveModified;
#ifdef ULTRACANVAS_HAS_VIRTUALFS
        std::error_code ec;
        // Entries living inside an archive cannot be removed via the real
        // filesystem. They are grouped per archive and deleted with ONE
        // batched VirtualFS call each, so the archive is rewritten once for
        // the whole selection — deleting entries one-by-one would rewrite
        // the archive once per entry, which for thousands of files takes
        // practically forever.
        std::vector<std::string> archiveOrder;
        std::map<std::string, std::vector<std::string>> archiveVictims;
        std::map<std::string, std::vector<std::string>> archiveParents;
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
                    archiveParents[resolved.realPath].push_back(
                            fs::path(e.path).parent_path().string());
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
                continue;
            }
            for (const std::string& folder : archiveParents[archive])
                if (!folder.empty()) archiveModified.push_back(folder);
        }
#else
        const std::vector<FilerEntry>& fsVictims = victims;
#endif
        // Real-filesystem victims go through an interactive queue: a
        // write-protected (locked) entry asks before the attempt, a failed
        // delete asks afterwards — see ShowDeleteProblemDialog.
        pendingDelete = std::make_unique<PendingDelete>();
        pendingDelete->victims = fsVictims;   // copy: Refresh() rebuilds `entries`
        pendingDelete->modifiedFolders = std::move(archiveModified);
        ContinuePendingDelete();
    }

    void UltraCanvasFilerWidget::ContinuePendingDelete() {
        std::error_code ec;
        while (pendingDelete && pendingDelete->next < pendingDelete->victims.size()) {
            PendingDelete& pd = *pendingDelete;
            const FilerEntry& e = pd.victims[pd.next];
            // A write-protected (locked) entry asks before the attempt.
            if (e.isReadOnly) {
                DeleteProblemAction action;
                if (pd.currentDecided)       action = pd.currentAction;
                else if (pd.protectedForAll) action = pd.protectedAction;
                else { ShowDeleteProblemDialog(e, true, {}); return; }
                if (action == DeleteProblemAction::Skip) {
                    AdvancePendingDelete();
                    continue;
                }
                // Delete anyway: lift the protection first — a read-only
                // entry cannot be removed at all on Windows without this.
                std::error_code pec;
                fs::permissions(e.path, fs::perms::owner_write,
                                fs::perm_options::add, pec);
            }
            fs::remove_all(e.path, ec);
            if (ec) {
                if (pd.skipFailedForAll) { AdvancePendingDelete(); continue; }
                if (pd.retryFailedForAll && !pd.currentRetried) {
                    pd.currentRetried = true;   // one silent retry, then ask
                    continue;
                }
                ShowDeleteProblemDialog(e, false, ec.message());
                return;
            }
            const std::string folder = fs::path(e.path).parent_path().string();
            if (!folder.empty()) pd.modifiedFolders.push_back(folder);
            AdvancePendingDelete();
        }
        FinishPendingDelete();
    }

    void UltraCanvasFilerWidget::AdvancePendingDelete() {
        if (!pendingDelete) return;
        ++pendingDelete->next;
        pendingDelete->currentDecided = false;
        pendingDelete->currentRetried = false;
    }

    void UltraCanvasFilerWidget::FinishPendingDelete() {
        if (!pendingDelete) return;
        std::unique_ptr<PendingDelete> pd = std::move(pendingDelete);
        // Silent clear when a neighbour is waiting to inherit the selection:
        // the rescan reports that one change. Firing an empty selection first
        // would fold an attached preview pane away and open it again.
        if (selectAfterScanPath.empty()) ClearSelection();
        else                             selection.clear();
        Refresh();
        // Report every folder that really lost an entry: in a file-list
        // display the victims can come from different folders. For a folder
        // listing they all share currentPath, so this reports it once.
        if (onFolderModified) {
            std::unordered_set<std::string> reported;
            for (const std::string& folder : pd->modifiedFolders)
                if (reported.insert(folder).second) NotifyFolderModified(folder);
        }
    }

    void UltraCanvasFilerWidget::ShowDeleteProblemDialog(const FilerEntry& entry,
                                                         bool writeProtected,
                                                         const std::string& reason) {
        const std::string kind = entry.isDirectory ? "folder" : "file";

        DialogConfig cfg;
        cfg.dialogType = DialogType::Warning;
        cfg.buttons = DialogButtons::NoButtons;   // custom buttons added below
        cfg.width = 560;
        cfg.height = 300;
        if (writeProtected) {
            cfg.title = entry.isDirectory
                    ? "Folder Is Write-Protected" : "File Is Write-Protected";
            cfg.message = "\"" + entry.name + "\" is write-protected.";
            cfg.details = "Choose what to do with the locked " + kind + ":";
        } else {
            cfg.title = "Cannot Delete";
            cfg.message = "\"" + entry.name + "\" could not be deleted: "
                    + (reason.empty() ? std::string("unknown error") : reason)
                    + ".";
            cfg.details = "The " + kind
                    + " may be locked or in use by another program.";
        }

        // Skipping is the safe default for a locked entry, trying again for
        // a failure.
        auto self = this;
        const bool shown = ShowProceedSkipDialog(cfg,
                writeProtected ? "Delete it anyway" : "Try again",
                "Skip this " + kind,
                writeProtected
                        ? "Do this for all remaining write-protected items"
                        : "Do this for all remaining items",
                /*proceedDefault=*/!writeProtected,
                [self, writeProtected](bool proceed, bool all) {
                    if (!self->pendingDelete) return;
                    PendingDelete& pd = *self->pendingDelete;
                    if (writeProtected) {
                        if (all) {
                            pd.protectedForAll = true;
                            pd.protectedAction = proceed
                                    ? DeleteProblemAction::Delete
                                    : DeleteProblemAction::Skip;
                        }
                        if (!proceed) {
                            self->AdvancePendingDelete();
                        } else {
                            pd.currentDecided = true;
                            pd.currentAction = DeleteProblemAction::Delete;
                        }
                    } else if (!proceed) {
                        if (all) pd.skipFailedForAll = true;
                        self->AdvancePendingDelete();
                    } else {
                        // Try again now; a stored "for all" grants every later
                        // failing entry one silent retry before asking again.
                        if (all) pd.retryFailedForAll = true;
                        pd.currentRetried = true;
                    }
                    self->ContinuePendingDelete();
                },
                [self]() {
                    // Cancel keeps what was already deleted and drops the rest.
                    self->FinishPendingDelete();
                });
        if (!shown) {   // dialogs disabled — the old fixed behavior
            if (writeProtected) {   // attempt the delete like before
                pendingDelete->currentDecided = true;
                pendingDelete->currentAction = DeleteProblemAction::Delete;
            } else {
                ReportError("Delete failed for " + entry.path + ": " + reason);
                AdvancePendingDelete();
            }
            ContinuePendingDelete();
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
        ts.textColor        = style.renameTextColor;
        ts.caretColor       = style.renameTextColor;
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
            ShowRenameReplaceDialog(oldPath, target.string());
            return;
        }
        PerformRename(oldPath, target.string());
    }

    void UltraCanvasFilerWidget::PerformRename(const std::string& oldPath,
                                               const std::string& targetPath) {
        std::error_code ec;
        fs::rename(oldPath, targetPath, ec);
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
            renamedToPath = targetPath;
        }
        // The rescan below clears renamedToPath, so decide here whether the
        // rename went through — only then was work done in the folder.
        const bool renamed = !ec;
        Refresh();
        if (renamed)
            NotifyFolderModified(fs::path(targetPath).parent_path().string());
    }

    void UltraCanvasFilerWidget::ShowRenameReplaceDialog(
            const std::string& oldPath, const std::string& targetPath) {
        std::error_code ec;
        const std::string newName = fs::path(targetPath).filename().string();
        const std::string kind = fs::is_directory(targetPath, ec) ? "folder"
                                                                  : "file";
        DialogConfig cfg;
        cfg.title = "Name Already Taken";
        cfg.dialogType = DialogType::Warning;
        cfg.message = "A " + kind + " named \"" + newName
                + "\" already exists in this folder.";
        cfg.details = "Replacing it overwrites the existing " + kind
                + ". This cannot be undone.";
        cfg.buttons = DialogButtons::NoButtons;   // custom buttons added below
        cfg.width = 520;
        cfg.height = 200;

        auto dialog = UltraCanvasDialogManager::CreateDialog(cfg);
        if (!dialog) {   // dialogs disabled — refuse, the old fixed behavior
            ReportError("Rename failed: \"" + newName + "\" already exists");
            RequestRedraw();
            return;
        }

        auto self = this;
        dialog->AddCustomButton("Replace", DialogResult::Yes, nullptr);
        dialog->AddCustomButton("Cancel", DialogResult::Cancel, nullptr);
        dialog->onResult = [self, oldPath, targetPath](DialogResult result) {
            if (result != DialogResult::Yes) {   // keep the old name
                self->RequestRedraw();
                return;
            }
            std::error_code rec;
            fs::remove_all(targetPath, rec);
            if (rec) {
                self->ReportError("Rename could not replace " + targetPath
                                  + ": " + rec.message());
                return;
            }
            self->PerformRename(oldPath, targetPath);
        };
        UltraCanvasDialogManager::ShowDialog(dialog, nullptr, GetWindow());
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

    // ===== ARCHIVE WORKER =====

    void UltraCanvasFilerWidget::StartArchiveJob(
            const std::string& title, const std::string& caption,
            const std::string& destination, bool packing,
            std::function<bool(const ArchiveProgressReporter&)> work,
            std::function<void(bool ok, bool cancelled)> onFinished) {
        if (archiveJob) return;   // one at a time

        archiveJob = std::make_unique<ArchiveJob>();
        ArchiveJob* job = archiveJob.get();
        job->destination = destination;
        job->packing = packing;
        job->onFinished = std::move(onFinished);

        // The window is optional: without it (dialogs disabled, no parent) the
        // work still runs, it just runs unannounced.
        job->dialog = UltraCanvasProgressDialog::Show(
                GetWindow(), title, caption,
                [this]() { if (archiveJob) archiveJob->cancelRequested.store(true); });

        // The reporter is the worker's only way to talk to the UI: it stores
        // numbers the poll timer reads, and answers whether to keep going.
        ArchiveProgressReporter report =
                [job](uint64_t done, uint64_t total, const std::string& file) {
            job->doneBytes.store(done);
            job->totalBytes.store(total);
            if (!file.empty()) {
                std::lock_guard<std::mutex> lk(job->fileMutex);
                job->currentFile = file;
            }
            return !job->cancelRequested.load();
        };

        job->worker = std::thread([job, work = std::move(work), report]() {
            bool ok = false;
            try {
                ok = work(report);
            } catch (...) {
                ok = false;      // a throwing backend must not take the app down
            }
            job->succeeded.store(ok);
            job->finished.store(true);
        });

        if (auto* app = UltraCanvasApplication::GetInstance()) {
            job->timer = app->StartTimer(kArchivePollIntervalMs, true,
                                         [this](TimerId) { PollArchiveJob(); });
        }
    }

    void UltraCanvasFilerWidget::PollArchiveJob() {
        if (!archiveJob) return;
        ArchiveJob* job = archiveJob.get();

        if (job->dialog) {
            const uint64_t total = job->totalBytes.load();
            const uint64_t done = job->doneBytes.load();
            // An unknown total (a backend that only names files) shows the busy
            // ring rather than a percentage invented from nothing.
            job->dialog->SetProgress(total > 0
                    ? static_cast<double>(done) / static_cast<double>(total)
                    : -1.0);
            std::string file;
            {
                std::lock_guard<std::mutex> lk(job->fileMutex);
                file = job->currentFile;
            }
            job->dialog->SetDetail(fs::path(file).filename().string());
        }
        if (job->finished.load()) FinishArchiveJob();
    }

    void UltraCanvasFilerWidget::FinishArchiveJob() {
        if (!archiveJob) return;
        // Move the job out first: onFinished may start the next one (the
        // extract queue does exactly that).
        std::unique_ptr<ArchiveJob> job = std::move(archiveJob);
        if (job->timer != InvalidTimerId) {
            if (auto* app = UltraCanvasApplication::GetInstance())
                app->StopTimer(job->timer);
            job->timer = InvalidTimerId;
        }
        if (job->worker.joinable()) job->worker.join();
        if (job->dialog) { job->dialog->Close(); job->dialog.reset(); }

        const bool cancelled = job->cancelRequested.load();
        const bool ok = job->succeeded.load() && !cancelled;
        // A cancelled pack leaves a half-written archive behind; nobody wants
        // that in the folder listing. A cancelled extraction keeps what it
        // already wrote - those are real files the user may still want.
        if (cancelled && job->packing && !job->destination.empty()) {
            std::error_code ec;
            fs::remove(job->destination, ec);
        }
        if (job->onFinished) job->onFinished(ok, cancelled);
    }

    void UltraCanvasFilerWidget::CompressSelection(const std::string& extension) {
#ifdef ULTRACANVAS_HAS_VIRTUALFS
        std::vector<FilerEntry> targets = SelectionOrAll();
        if (targets.empty()) return;
        std::vector<std::string> paths;
        for (const FilerEntry& e : targets) paths.push_back(e.path);
        std::string base = (targets.size() == 1)
                ? ArchiveBaseNameOf(targets[0].name, targets[0].isDirectory)
                : fs::path(currentPath).filename().string();
        if (base.empty()) base = "archive";
        // The extension drives the archive format chosen by the VirtualFS bridge.
        std::string ext = extension.empty() ? std::string("zip") : extension;
        if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
        std::string dest = UniqueChildPath(base + "." + ext);
        if (archiveJob) return;   // one pack / unpack at a time
        // Initialize on the UI thread: the worker must not be the first caller
        // to build the VirtualFS provider registry.
        UCVFSBridge::Initialize();
        const std::string archiveName = fs::path(dest).filename().string();
        StartArchiveJob("Compressing", "Creating \"" + archiveName + "\"",
                        dest, /*packing=*/true,
                        [dest, paths](const ArchiveProgressReporter& report) {
            return UCVFSBridge::CreateArchive(dest, paths,
                    UCVFSCompressionOptions::Default(),
                    [&report](uint64_t done, uint64_t total,
                              const std::string& file) {
                return report(done, total, file);
            });
        },
                        [this, dest](bool ok, bool cancelled) {
            if (!ok && !cancelled) ReportError("Compression failed for " + dest);
            Refresh();
            if (ok) NotifyFolderModified(fs::path(dest).parent_path().string());
        });
#else
        (void)extension;
        ReportError("Compress requires the VirtualFS module");
#endif
    }

    void UltraCanvasFilerWidget::ExtractSelection() {
#ifdef ULTRACANVAS_HAS_VIRTUALFS
        if (pendingExtract) return;   // one extract (and its dialog) at a time
        std::vector<FilerEntry> archives;
        for (const FilerEntry& e : GetSelectedEntries())
            if (e.isArchive) archives.push_back(e);
        if (archives.empty()) return;
        pendingExtract = std::make_unique<PendingExtract>();
        pendingExtract->archives = std::move(archives);
        ContinuePendingExtract();
#else
        ReportError("Extract requires the VirtualFS module");
#endif
    }

#ifdef ULTRACANVAS_HAS_VIRTUALFS
    void UltraCanvasFilerWidget::ContinuePendingExtract() {
        std::error_code ec;
        while (pendingExtract &&
               pendingExtract->next < pendingExtract->archives.size()) {
            PendingExtract& pe = *pendingExtract;
            const FilerEntry& e = pe.archives[pe.next];
            const std::string destDir =
                    (fs::path(currentPath) / fs::path(e.name).stem()).string();
            if (fs::exists(destDir, ec) && !pe.applyToAll) {
                ShowExtractConflictDialog(e);
                return;
            }
            if (!ExtractCurrentAndAdvance(fs::exists(destDir, ec)
                                                  ? pe.action
                                                  : PasteConflictAction::KeepBoth))
                return;   // running on the archive worker; it resumes the queue
        }
        FinishPendingExtract();
    }

    void UltraCanvasFilerWidget::FinishPendingExtract() {
        if (!pendingExtract) return;
        const bool changed = pendingExtract->changed;
        pendingExtract.reset();
        if (changed) { Refresh(); NotifyFolderModified(); }
    }

    bool UltraCanvasFilerWidget::ExtractCurrentAndAdvance(
            PasteConflictAction action) {
        PendingExtract& pe = *pendingExtract;
        const FilerEntry& e = pe.archives[pe.next];
        if (action == PasteConflictAction::Skip) {
            ++pe.next;
            return true;
        }
        if (archiveJob) return false;   // a job is already running

        const std::string baseName = fs::path(e.name).stem().string();
        std::string destDir = (fs::path(currentPath) / baseName).string();
        std::error_code ec;
        // Keep both renames the destination; Replace merges the archive
        // content into the existing folder.
        if (action == PasteConflictAction::KeepBoth && fs::exists(destDir, ec))
            destDir = UniqueChildPath(baseName);
        fs::create_directories(destDir, ec);

        // Initialize on the UI thread: the worker must not be the first caller
        // to build the VirtualFS provider registry.
        UCVFSBridge::Initialize();
        const std::string archivePath = e.path;
        const std::string archiveName = e.name;
        StartArchiveJob("Extracting", "Unpacking \"" + archiveName + "\"",
                        destDir, /*packing=*/false,
                        [archivePath, destDir](const ArchiveProgressReporter& report) {
            return UCVFSBridge::ExtractArchive(archivePath, destDir,
                    [&report](uint64_t done, uint64_t total,
                              const std::string& file) {
                return report(done, total, file);
            });
        },
                        [this, archivePath](bool ok, bool cancelled) {
            if (!pendingExtract) return;
            if (ok) pendingExtract->changed = true;
            else if (!cancelled) ReportError("Extraction failed for " + archivePath);
            ++pendingExtract->next;
            // Cancelling stops the whole queue, not just this archive.
            if (cancelled) FinishPendingExtract();
            else           ContinuePendingExtract();
        });
        return false;   // the queue resumes from the callback above
    }

    void UltraCanvasFilerWidget::ShowExtractConflictDialog(
            const FilerEntry& archive) {
        const std::string folderName = fs::path(archive.name).stem().string();

        DialogConfig cfg;
        cfg.title = "Folder Already Exists";
        cfg.dialogType = DialogType::Question;
        cfg.message = "A folder named \"" + folderName
                + "\" already exists in this folder.";
        cfg.details = "Choose where to extract \"" + archive.name + "\":";
        cfg.buttons = DialogButtons::NoButtons;   // custom buttons added below
        cfg.width = 560;
        cfg.height = 330;

        auto self = this;
        auto dialog = UltraCanvasDialogManager::CreateDialog(cfg);
        if (!dialog) {   // dialogs disabled — keep both, the old fixed behavior
            pendingExtract->action = PasteConflictAction::KeepBoth;
            pendingExtract->applyToAll = true;
            ContinuePendingExtract();
            return;
        }

        // The destination, one exclusive switch per choice. The last
        // dialog's choice is preselected.
        static const PasteConflictAction kActions[3] = {
            PasteConflictAction::KeepBoth, PasteConflictAction::Replace,
            PasteConflictAction::Skip,
        };
        size_t checkedIndex = 0;
        for (size_t i = 0; i < 3; ++i)
            if (kActions[i] == pendingExtract->action) checkedIndex = i;
        AddExclusiveSwitches(dialog.get(), "FilerExtractOpt",
                {"Keep both (extract into a renamed folder)",
                 "Extract into the existing folder",
                 "Skip this archive"},
                checkedIndex,
                [self](size_t index) {
                    if (self->pendingExtract)
                        self->pendingExtract->action = kActions[index];
                });

        // Scope: ask again on the next conflict (off, the default) or apply
        // this choice to every remaining archive of this extract.
        auto allSwitch = UltraCanvasSwitch::Create(
                "FilerExtractAll", 0, 0, "Do this for all remaining archives",
                pendingExtract->applyToAll);
        allSwitch->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        allSwitch->onStateChanged = [self](CheckedState, CheckedState state) {
            if (self->pendingExtract)
                self->pendingExtract->applyToAll =
                        (state == CheckedState::Checked);
        };
        dialog->AddDialogElement(allSwitch);

        dialog->AddCustomButton("Continue", DialogResult::Yes, nullptr);
        dialog->AddCustomButton("Cancel", DialogResult::Cancel, nullptr);
        dialog->onResult = [self](DialogResult result) {
            if (!self->pendingExtract) return;
            if (result != DialogResult::Yes) {
                // Cancel keeps what was already extracted and drops the rest.
                self->FinishPendingExtract();
                return;
            }
            self->ExtractCurrentAndAdvance(self->pendingExtract->action);
            self->ContinuePendingExtract();
        };
        UltraCanvasDialogManager::ShowDialog(dialog, nullptr, GetWindow());
    }
#endif   // ULTRACANVAS_HAS_VIRTUALFS

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
                ? ArchiveBaseNameOf(targets[0].name, targets[0].isDirectory)
                : fs::path(currentPath).filename().string();
        if (base.empty()) base = "archive";
        compressDlg.destDir = currentPath;

        OpenArchiveDialogChrome(base, "Compress");
    }

    void UltraCanvasFilerWidget::OpenExtractDialog() {
        std::vector<FilerEntry> archives;
        for (const FilerEntry& e : GetSelectedEntries())
            if (e.isArchive) archives.push_back(e);
        if (archives.empty()) return;

        compressDlg = CompressDialogState();
        compressDlg.active = true;
        compressDlg.extractMode = true;
        for (const FilerEntry& e : archives) compressDlg.sourcePaths.push_back(e.path);
        compressDlg.destDir = currentPath;

        // The first archive's suffix drives the icon tag; the label under the
        // icon names what is being unpacked.
        std::string firstBase = ArchiveBaseNameOf(archives[0].name, false);
        if (archives[0].name.size() > firstBase.size() + 1) {
            std::string suffix = archives[0].name.substr(firstBase.size() + 1);
            std::transform(suffix.begin(), suffix.end(), suffix.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            compressDlg.extension = suffix;
        }
        compressDlg.formatLabel = (archives.size() == 1)
                ? archives[0].name
                : std::to_string(archives.size()) + " archives";

        // Default destination folder: the archive's own name without its
        // suffix — what the direct ExtractSelection() would pick.
        std::string base = (archives.size() == 1) ? firstBase : std::string("Extracted");
        if (base.empty()) base = "Extracted";

        OpenArchiveDialogChrome(base, "Extract");
    }

    void UltraCanvasFilerWidget::OpenArchiveDialogChrome(const std::string& defaultName,
                                                         const std::string& okLabel) {
        compressDlg.nameBuffer = defaultName;

        if (renamingIndex >= 0) CancelRename();

        // A real editor for the name — the same component the inline rename
        // uses, so the field has a caret, click-to-position, selection and
        // clipboard instead of append-and-backspace-only editing.
        DestroyCompressNameInput();
        compressNameInput = CreateTextInput("filer-compress-name", 1, 1, 120, 26);
        TextInputStyle ts;
        ts.backgroundColor  = style.renameFieldColor;
        ts.borderColor      = Color(0, 0, 0, 55);
        ts.focusBorderColor = style.renameBorderColor;
        ts.textColor        = style.textColor;
        ts.caretColor       = style.textColor;
        ts.selectionColor   = Color(style.selectionColor.r, style.selectionColor.g,
                                    style.selectionColor.b, 170);
        ts.borderWidth = 1;
        ts.borderRadius = 4;
        ts.paddingLeft = 7;
        ts.paddingRight = 4;
        ts.paddingTop = 1;
        ts.paddingBottom = 1;
        ts.fontStyle.fontFamily = style.fontFamily;
        ts.fontStyle.fontSize = style.fontSize;
        compressNameInput->SetStyle(ts);
        compressNameInput->SetShowValidationState(false);
        compressNameInput->SetText(defaultName);
        // Whole name selected, so typing replaces the suggestion outright.
        compressNameInput->SelectAll();
        compressNameInput->onTextChanged = [this](const std::string& t) {
            compressDlg.nameBuffer = t;
        };
        compressNameInput->onEnterPressed = [this](const std::string&) {
            CommitCompressDialog();
            return true;
        };
        compressNameInput->onEscapePressed = [this]() {
            CloseCompressDialog();
            return true;
        };
        AddChild(compressNameInput);
        PositionCompressNameInput();
        compressNameInput->SetFocus(true);

        DestroyCompressButtons();
        compressOkButton = MakeCompressButton(
                "filer-compress-ok", okLabel, true,
                [this]() { CommitCompressDialog(); });
        compressCancelButton = MakeCompressButton(
                "filer-compress-cancel", "Cancel", false,
                [this]() { CloseCompressDialog(); });

        InstallCompressKeyFilter();
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::DestroyCompressNameInput() {
        if (!compressNameInput) return;
        auto input = compressNameInput;
        compressNameInput.reset();
        // Teardown must not fire the callbacks again: RemoveChild drops the
        // editor's focus, which would otherwise re-enter this dialog.
        input->onTextChanged = nullptr;
        input->onEnterPressed = nullptr;
        input->onEscapePressed = nullptr;
        RemoveChild(input);
        ReleaseAfterEventLoopTurn(input);
    }

    void UltraCanvasFilerWidget::DestroyCompressButtons() {
        for (std::shared_ptr<UltraCanvasButton>* slot :
                 {&compressOkButton, &compressCancelButton}) {
            if (!*slot) continue;
            auto button = *slot;
            slot->reset();
            button->onClick = nullptr;   // never re-enter a closing dialog
            RemoveChild(button);
            ReleaseAfterEventLoopTurn(button);
        }
    }

    std::shared_ptr<UltraCanvasButton> UltraCanvasFilerWidget::MakeCompressButton(
            const std::string& identifier, const std::string& label, bool primary,
            std::function<void()> action) {
        auto button = CreateButton(identifier, 0, 0, 104, 30, label);
        ButtonStyle bs;
        bs.normalColor   = primary ? Color(66, 133, 244, 255) : Color(238, 238, 242, 255);
        bs.hoverColor    = primary ? Color(90, 150, 250, 255) : Color(226, 226, 232, 255);
        bs.pressedColor  = primary ? Color(52, 112, 214, 255) : Color(214, 214, 220, 255);
        bs.normalTextColor = bs.hoverTextColor = bs.pressedTextColor =
                primary ? Colors::White : style.textColor;
        bs.borderColor = Color(0, 0, 0, 40);
        bs.borderWidth = primary ? 0.0f : 1.0f;
        bs.cornerRadius = 5.0f;
        bs.fontFamily = style.fontFamily;
        bs.fontSize = style.fontSize;
        bs.fontWeight = FontWeight::Bold;
        button->SetStyle(bs);
        // The name editor keeps the keyboard: a click on Compress / Cancel must
        // not pull the focus out of the field it is about to act on.
        button->SetAcceptsFocus(false);
        button->SetOnClick(std::move(action));
        AddChild(button);
        return button;
    }

    void UltraCanvasFilerWidget::PositionCompressNameInput() {
        PlaceChildAt(compressNameInput, Rect2Df(compressDlg.nameEditRect));
    }

    std::string UltraCanvasFilerWidget::CompressKeyFilterId() const {
        return GetIdentifier() + "_compress_keys";
    }

    void UltraCanvasFilerWidget::InstallCompressKeyFilter() {
        auto* win = GetWindow();
        if (!win || compressKeyFilterInstalled) return;
        compressKeyFilterInstalled = true;
        win->InstallEventFilter(CompressKeyFilterId(),
                [this](const UCEvent& e) -> bool {
            return HandleCompressFilteredKey(e);
        }, { UCEventType::KeyDown });
    }

    void UltraCanvasFilerWidget::RemoveCompressKeyFilter() {
        if (!compressKeyFilterInstalled) return;
        compressKeyFilterInstalled = false;
        if (auto* win = GetWindow())
            win->UnInstallWindowEventFilter(CompressKeyFilterId());
    }

    bool UltraCanvasFilerWidget::HandleCompressFilteredKey(const UCEvent& event) {
        // The dialog is modal: while it is up every keystroke in the window
        // belongs to it, whoever the window currently considers focused. Without
        // this the field went dead as soon as anything else claimed the focus —
        // and stayed dead for every dialog opened afterwards.
        if (!compressDlg.active) return false;
        auto* win = GetWindow();
        if (!win) return false;
        // A menu or dropdown on top owns the keyboard while it is up.
        if (win->GetActivePopupElement()) return false;
        UltraCanvasUIElement* focused = win->GetFocusedElement();
        if (focused == compressNameInput.get()) return false;   // already there
        if (focused == this) return false;                      // OnEvent handles it
        return HandleCompressDialogEvent(event);
    }

    void UltraCanvasFilerWidget::CloseCompressDialog() {
        if (compressDlg.draggingIcon) {
            if (auto* app = UltraCanvasApplication::GetInstance()) app->ReleaseMouse();
        }
        RemoveCompressKeyFilter();
        DestroyCompressNameInput();
        DestroyCompressButtons();
        compressDlg = CompressDialogState();
        // The editor held the keyboard; hand it back so the folder display
        // answers the arrow keys again.
        SetFocus(true);
        RequestRedraw();
    }

#ifdef ULTRACANVAS_HAS_VIRTUALFS
    void UltraCanvasFilerWidget::ExtractArchivesSequentially(
            std::vector<std::pair<std::string, std::string>> jobs, size_t index,
            std::string notifyFolder) {
        if (index >= jobs.size()) {
            Refresh();
            if (!notifyFolder.empty()) NotifyFolderModified(notifyFolder);
            return;
        }
        const std::string src = jobs[index].first;
        const std::string dest = jobs[index].second;
        const std::string caption = jobs.size() > 1
                ? "Unpacking \"" + fs::path(src).filename().string() + "\" (" +
                  std::to_string(index + 1) + " of " + std::to_string(jobs.size()) + ")"
                : "Unpacking \"" + fs::path(src).filename().string() + "\"";
        UCVFSBridge::Initialize();
        StartArchiveJob("Extracting", caption, dest, /*packing=*/false,
                        [src, dest](const ArchiveProgressReporter& report) {
            return UCVFSBridge::ExtractArchive(src, dest,
                    [&report](uint64_t done, uint64_t total,
                              const std::string& file) {
                return report(done, total, file);
            });
        },
                        [this, jobs, index, notifyFolder, src](bool ok, bool cancelled) mutable {
            if (!ok && !cancelled) ReportError("Extraction failed for " + src);
            // Cancelling stops the whole run, not just the archive in flight.
            if (cancelled) {
                Refresh();
                if (!notifyFolder.empty()) NotifyFolderModified(notifyFolder);
                return;
            }
            ExtractArchivesSequentially(std::move(jobs), index + 1, notifyFolder);
        });
    }
#endif

    void UltraCanvasFilerWidget::CommitCompressDialog() {
        // Read the editor before closing tears it down.
        if (compressNameInput) compressDlg.nameBuffer = compressNameInput->GetText();
        CompressDialogState d = compressDlg;   // copy: we close before the work
        CloseCompressDialog();
#ifdef ULTRACANVAS_HAS_VIRTUALFS
        if (d.sourcePaths.empty()) return;
        // The editor accepts any text; a name is not a path, so separators are
        // dropped and surrounding blanks trimmed before it becomes a file name.
        std::string baseName;
        for (char c : d.nameBuffer) {
            if (c == '/' || c == '\\') continue;
            if (static_cast<unsigned char>(c) < 32) continue;
            baseName += c;
        }
        size_t first = baseName.find_first_not_of(' ');
        size_t last  = baseName.find_last_not_of(' ');
        baseName = (first == std::string::npos)
                 ? std::string() : baseName.substr(first, last - first + 1);
        if (baseName.empty()) baseName = d.extractMode ? "Extracted" : "archive";

        std::error_code ec;
        fs::path dir(d.destDir.empty() ? currentPath : d.destDir);
        if (!fs::is_directory(dir, ec)) dir = currentPath;

        if (d.extractMode) {
            // The name is the destination folder the archives unpack into.
            fs::path target = dir / baseName;
            int n = 2;
            while (fs::exists(target, ec))
                target = dir / (baseName + " (" + std::to_string(n++) + ")");
            fs::create_directories(target, ec);
            if (ec || !fs::is_directory(target, ec)) {
                ReportError("Extraction failed: cannot create " + target.string());
                return;
            }
            std::vector<std::pair<std::string, std::string>> jobs;
            for (const std::string& src : d.sourcePaths) {
                fs::path dest = target;
                if (d.sourcePaths.size() > 1) {
                    // Several archives: each unpacks into its own subfolder so
                    // their contents cannot collide.
                    std::string stem =
                            ArchiveBaseNameOf(fs::path(src).filename().string(), false);
                    if (stem.empty()) stem = "archive";
                    dest = target / stem;
                    int m = 2;
                    while (fs::exists(dest, ec))
                        dest = target / (stem + " (" + std::to_string(m++) + ")");
                    fs::create_directories(dest, ec);
                }
                jobs.emplace_back(src, dest.string());
            }
            // The target can sit inside a folder the icon was dragged onto.
            ExtractArchivesSequentially(std::move(jobs), 0, dir.string());
            return;
        }

        std::string ext = d.extension.empty() ? std::string("zip") : d.extension;

        // Uniquify while keeping the full (possibly compound) extension intact,
        // so ".tar.gz" stays ".tar.gz" rather than becoming ".tar (2).gz".
        fs::path candidate = dir / (baseName + "." + ext);
        int n = 2;
        while (fs::exists(candidate, ec)) {
            candidate = dir / (baseName + " (" + std::to_string(n++) + ")." + ext);
        }
        std::string dest = candidate.string();

        if (archiveJob) return;   // one pack / unpack at a time
        // Initialize on the UI thread: the worker must not be the first caller
        // to build the VirtualFS provider registry.
        UCVFSBridge::Initialize();
        const std::vector<std::string> sources = d.sourcePaths;
        StartArchiveJob("Compressing",
                        "Creating \"" + candidate.filename().string() + "\"",
                        dest, /*packing=*/true,
                        [dest, sources](const ArchiveProgressReporter& report) {
            return UCVFSBridge::CreateArchive(dest, sources,
                    UCVFSCompressionOptions::Default(),
                    [&report](uint64_t done, uint64_t total,
                              const std::string& file) {
                return report(done, total, file);
            });
        },
                        [this, dest](bool ok, bool cancelled) {
            if (!ok && !cancelled) ReportError("Compression failed for " + dest);
            Refresh();
            // The archive can be written into a folder the icon was dragged onto.
            if (ok) NotifyFolderModified(fs::path(dest).parent_path().string());
        });
#else
        ReportError(std::string(d.extractMode ? "Extract" : "Compress") +
                    " requires the VirtualFS module");
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
        PlaceChildAt(compressOkButton, Rect2Df(d.okRect));
        PlaceChildAt(compressCancelButton, Rect2Df(d.cancelRect));
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
        ctx->DrawText(d.extractMode ? "Extract" : "Compress",
                      Point2Dd(d.panel.x + 16, d.panel.y + 12));

        // File-type icon on top. While being dragged a ghost follows the cursor
        // and the panel shows a faint placeholder in its place.
        if (d.draggingIcon) {
            ctx->SetFillPaint(Color(0, 0, 0, 22));
            ctx->FillRoundedRectangle(Rect2Dd(d.iconRect), 4);
        } else {
            DrawEntryIcon(ctx, synth, d.iconRect);
        }

        // Format label under the icon: the archive format when compressing,
        // the archive name / count when extracting.
        FontStyle bodyFont;
        bodyFont.fontFamily = style.fontFamily;
        bodyFont.fontSize = style.fontSize;
        ctx->SetFontStyle(bodyFont);
        ctx->SetTextPaint(style.textColor);
        std::string fmtFit = EllipsizeText(ctx, d.formatLabel, d.panel.width - 32);
        Size2Di fts = ctx->GetTextLineDimensions(fmtFit);
        int fmtY = d.iconRect.y + d.iconRect.height + 8;
        ctx->DrawText(fmtFit,
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

        // Name row: the editor holds the base name; when compressing the
        // archive extension is fixed and shown next to it in grey (when
        // extracting the name is a folder, so there is no suffix). The editor
        // is a real text input child, so it draws (and scrolls) its own
        // content.
        ctx->SetFontStyle(bodyFont);
        const std::string extText = d.extractMode ? std::string()
                                                  : "." + d.extension;
        int editW = d.nameRect.width;
        Size2Di extSz{0, 0};
        if (!extText.empty()) {
            extSz = ctx->GetTextLineDimensions(extText);
            editW = std::max(80, d.nameRect.width - extSz.width - 8);
        }
        compressDlg.nameEditRect = Rect2Di(d.nameRect.x, d.nameRect.y,
                                           editW, d.nameRect.height);
        PositionCompressNameInput();
        if (compressNameInput) {
            Rect2Df b = compressNameInput->GetBounds();
            ctx->PushState();
            ctx->Translate(Point2Df(b.x, b.y));
            compressNameInput->Render(ctx, Rect2Df(0, 0, b.width, b.height));
            ctx->PopState();
        }
        if (!extText.empty()) {
            ctx->SetFontStyle(bodyFont);
            ctx->SetTextPaint(style.secondaryTextColor);
            ctx->DrawText(extText,
                          Point2Dd(d.nameRect.x + editW + 6,
                                   d.nameRect.y + (d.nameRect.height - extSz.height) / 2.0));
        }

        // Destination path shown separately as smaller text.
        ctx->SetFontStyle(smallFont);
        ctx->SetTextPaint(style.secondaryTextColor);
        std::string dir = d.destDir.empty() ? currentPath : d.destDir;
        std::string pathText = "Location:  " + dir;
        pathText = EllipsizeText(ctx, pathText, d.panel.width - 32);
        ctx->DrawText(pathText,
                      Point2Dd(d.panel.x + 16, d.nameRect.y + d.nameRect.height + 8));

        // Buttons — real UltraCanvasButton children, drawn here because this
        // widget renders its own content and never paints its children.
        for (const auto& button : {compressOkButton, compressCancelButton}) {
            if (!button) continue;
            Rect2Df b = button->GetBounds();
            ctx->PushState();
            ctx->Translate(Point2Df(b.x, b.y));
            button->Render(ctx, Rect2Df(0, 0, b.width, b.height));
            ctx->PopState();
        }

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
            case UCEventType::KeyDown:
            case UCEventType::TextInput: {
                if (event.virtualKey == UCKeys::Escape) { CloseCompressDialog(); return true; }
                if (event.virtualKey == UCKeys::Return ||
                    event.virtualKey == UCKeys::NumPadEnter) { CommitCompressDialog(); return true; }
                // Everything else is text: hand it (and the keyboard) to the
                // name editor. Reaching this point at all means the focus had
                // drifted off the editor — anything from the click that opened
                // the dialog to a background pane claiming it — and without
                // this the field would simply stop responding. A key the
                // editor already saw and declined bubbles up here too; it must
                // not be delivered a second time.
                auto* win = GetWindow();
                bool editorHasKeys = compressNameInput && win &&
                        win->GetFocusedElement() == compressNameInput.get();
                if (compressNameInput && !editorHasKeys) {
                    compressNameInput->SetFocus(true);
                    compressNameInput->OnEvent(event);
                    RequestRedraw();
                }
                return true;   // stay modal: no key escapes to the folder view
            }
            case UCEventType::MouseDown: {
                if (event.button != UCMouseButton::Left) return true;
                Point2Di local(event.pointer.x, event.pointer.y);
                if (d.iconRect.Contains(local)) {
                    // Keyboard events go to the window's focused element, so a
                    // click that is not for the editor must not leave the focus
                    // on it — but the dialog still needs the keys, which the
                    // window key filter takes care of.
                    SetFocus(true);
                    d.draggingIcon = true;
                    d.dragPos = local;
                    d.dropFolderIndex = -1;
                    if (auto* app = UltraCanvasApplication::GetInstance())
                        app->CaptureMouse(this);
                    RequestRedraw();
                    return true;
                }
                // The Compress / Cancel buttons are child elements and are hit
                // before this handler sees the press, so there is nothing to
                // test for here. A click anywhere else in the modal (including
                // the name row beside the editor) puts the caret back in the
                // name.
                if (compressNameInput) {
                    compressNameInput->SetFocus(true);
                    RequestRedraw();
                }
                return true;   // modal: clicks elsewhere do nothing
            }
            case UCEventType::MouseMove: {
                Point2Di local(event.pointer.x, event.pointer.y);
                if (d.draggingIcon) {
                    d.dragPos = local;
                    d.dropFolderIndex = FolderIndexAtLocal(local);
                    RequestRedraw();
                }
                // Button hover is the buttons' own business — they get the
                // move directly as the elements under the pointer.
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

    void UltraCanvasFilerWidget::CreateNewFolder() {
        std::error_code ec;
        if (!fs::is_directory(currentPath, ec)) {
            ReportError("Cannot create a folder here: " + currentPath);
            return;
        }
        const std::string dest = UniqueChildPath("New folder");
        fs::create_directory(dest, ec);
        if (ec) {
            ReportError("New folder failed: " + ec.message());
            return;
        }
        Refresh();
        NotifyFolderModified();
        // Put the fresh folder straight into rename mode, like New > <document>.
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].path == dest) { StartRename(i); break; }
        }
    }

    void UltraCanvasFilerWidget::OpenSelectionWithChooser() {
        std::vector<std::string> paths;
        for (const FilerEntry& e : GetSelectedEntries())
            if (!e.isDirectory) paths.push_back(e.path);
        if (paths.empty()) return;

        const FileAssociations::ApplicationFilter filter =
                FileAssociations::GetApplicationFilter();
        FileDialogOptions opts;
        opts.SetTitle("Select the application to open with")
            .SetInitialDirectory(FileAssociations::GetApplicationsDirectory())
            // Picking a program is not a document the shell should remember.
            .SetRegisterAsRecent(false)
            .SetParentWindow(GetWindow())
            .AddFilter(filter.description, filter.extensions);
        // On platforms whose executables carry no extension the application
        // filter already shows everything - a second all-files entry would
        // just be the same list under another name.
        const bool showsEverything =
                filter.extensions.size() == 1 && filter.extensions.front() == "*";
        if (!showsEverything) opts.AddFilter("All files", "*");

        // The dialog outlives this call; the widget may not (a tab closed
        // while it is open), so the result is delivered through a weak ref.
        std::weak_ptr<UltraCanvasUIElement> weakSelf = weak_from_this();
        UltraCanvasFileLoader::OpenFileDialog(opts,
                [weakSelf, paths](DialogResult result, const std::string& appPath) {
            if (result != DialogResult::OK || appPath.empty()) return;
            auto self = std::dynamic_pointer_cast<UltraCanvasFilerWidget>(weakSelf.lock());
            std::string error;
            if (!FileAssociations::OpenWithApplicationPath(appPath, paths, error)) {
                if (self) self->ReportError(error);
            }
        });
    }

    void UltraCanvasFilerWidget::OpenSelectionWithDefaultApp() {
        const std::vector<FilerEntry> sel = GetSelectedEntries();
        if (sel.empty()) return;
        if (sel.size() == 1) {
            // One entry: OpenEntryWithOS also knows what to do with an
            // executable (run a program, ask about a script).
            OpenEntryWithOS(sel.front());
            return;
        }
        // Several files in one call, so applications that group their
        // documents open them in one window — the service splits the
        // selection per default handler itself.
        std::vector<std::string> paths;
        std::error_code ec;
        for (const FilerEntry& e : sel) {
            if (e.isDirectory) continue;
            if (!fs::is_regular_file(e.path, ec) || ec) continue;
            paths.push_back(e.path);
        }
        if (paths.empty()) return;
        std::string error;
        if (!FileAssociations::OpenWithDefaultApplication(paths, error))
            ReportError(error);
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
            // A changed display area reflows the view, so the scroll offset
            // has to be re-derived instead of kept (see ScrollAnchor): note
            // which entry the viewport is anchored to while the old layout is
            // still there, and put it back once the new one is built. Only a
            // resize does this — a relayout at an unchanged size comes from a
            // rescan or a view switch, which bring their own scroll position.
            const bool resized = lastAreaW >= 0 && lastAreaH >= 0
                                 && (w != lastAreaW || h != lastAreaH);
            ScrollAnchor anchor;
            if (resized) anchor = CaptureScrollAnchor();
            lastAreaW = w;
            lastAreaH = h;
            RecomputeLayout();
            layoutValid = true;
            RestoreScrollAnchor(anchor);
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
        // Flexible tile widths (Explorer-style): the leftover strip on the
        // right — too narrow for one more column — is folded into the tiles,
        // so the row always fills the width and resizing stretches the cells
        // smoothly until the next column fits. Only the cell widens (the
        // caption gets the room); the image box keeps the square edge (see
        // below), so decode sizes — and the async thumbnail cache keyed on
        // them — are unaffected. The division remainder (< cols px) stays on
        // the right so all columns are equally wide.
        if (flexibleTileWidths)
            tileW = std::max(tileW, (availW - (cols - 1) * gap) / cols);
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
                // The image box stays the square edge, centered in the (possibly
                // stretched) cell, so a resize never changes what the decode
                // workers are asked for.
                it.imageRect = Rect2Di(it.rect.x + (tileW - edge) / 2,
                                       it.rect.y, edge, rowImageH);
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

    UltraCanvasFilerWidget::ScrollAnchor
    UltraCanvasFilerWidget::CaptureScrollAnchor() const {
        ScrollAnchor anchor;
        if (items.empty()) return anchor;
        auto b = GetLocalBounds();
        const bool horizontal = IsHorizontal();
        // The band a file can actually be seen in: the widget minus the info
        // bar, and minus the Details header the rows scroll under.
        const int viewLead = (!horizontal && viewType == FilerViewType::Details)
                                ? detailsHeaderHeight : 0;
        const int viewEnd  = horizontal
                                ? static_cast<int>(b.width)
                                : static_cast<int>(b.height) - InfoBarHeight();
        if (viewEnd <= viewLead) return anchor;

        for (const ItemLayout& it : items) {
            const int lead = horizontal ? it.rect.x - scrollOffsetX
                                        : it.rect.y - scrollOffsetY;
            const int size = horizontal ? it.rect.width : it.rect.height;
            if (lead + size <= viewLead || lead >= viewEnd) continue;  // off screen
            const bool selected = std::find(selection.begin(), selection.end(),
                                            it.entryIndex) != selection.end();
            if (!anchor.valid || selected) {
                anchor.valid = true;
                anchor.entryIndex = it.entryIndex;
                anchor.offset = lead;
            }
            // The first visible entry only holds the place until a selected
            // one turns up on screen — that one is the reference (it is what
            // the preview pane shows, and what the user is working with).
            if (selected) break;
        }
        return anchor;
    }

    void UltraCanvasFilerWidget::RestoreScrollAnchor(const ScrollAnchor& anchor) {
        if (!anchor.valid) return;
        for (const ItemLayout& it : items) {
            if (it.entryIndex != anchor.entryIndex) continue;
            if (IsHorizontal()) scrollOffsetX = it.rect.x - anchor.offset;
            else                scrollOffsetY = it.rect.y - anchor.offset;
            ClampScroll();
            // Putting it back at the same offset can leave it hanging over an
            // edge when the reflow changed its size (a wrapped caption, a
            // taller row) or when the clamp pulled the scroll back at the end
            // of the content, so finish with the usual reveal.
            ScrollEntryIntoView(anchor.entryIndex);
            return;
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
            if (currentPath.empty() && !fileListMode) {
                // No folder was ever set - a programmatic state, not an
                // attention-worthy one.
                ctx->SetTextPaint(style.secondaryTextColor);
                FontStyle fsty;
                fsty.fontFamily = style.fontFamily;
                fsty.fontSize = style.fontSize;
                ctx->SetFontStyle(fsty);
                ctx->DrawTextInRect("(no folder)", Rect2Dd(bounds));
            } else {
                DrawEmptyState(ctx, bounds,
                               fileListMode ? "No entries" : "Folder is empty!");
            }
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
        CommitTextPreviewWants();

        if (viewType == FilerViewType::Details) DrawDetailsHeader(ctx, bounds);
        DrawColumnSplitters(ctx, bounds);
        DrawScrollbar(ctx);
        DrawSelectionInfoBar(ctx, bounds);
        // Drop-folder highlight above the whole view (including chrome); the
        // badge travels on the window overlay so it survives the widget border.
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

        // The badge that follows the cursor is NOT drawn here: it has to stay
        // visible after the cursor leaves this widget, and an element cannot
        // paint outside its own bounds. It goes onto the window's drag overlay
        // instead (UpdateDragOverlay → DrawDragBadge).
    }

    void UltraCanvasFilerWidget::DrawDragBadge(IRenderContext* ctx,
                                               const Rect2Di& badgeRect) {
        // The dragged entry's icon plus its name (or the item count), so it is
        // always visible what is being carried. Window coordinates.
        if (dragLabel.empty()) return;
        const int iconSz = 20, padX = 8, padY = 6, gap = 6;

        FontStyle fsty;
        fsty.fontFamily = style.fontFamily;
        fsty.fontSize = style.smallFontSize;
        ctx->PushState();
        ctx->SetFontStyle(fsty);
        std::string label = EllipsizeText(ctx, dragLabel, 220);
        Size2Di ts = ctx->GetTextLineDimensions(label);

        Color back = style.backgroundColor; back.a = 235;
        ctx->SetFillPaint(back);
        ctx->FillRoundedRectangle(Rect2Dd(badgeRect), 5);
        ctx->SetStrokePaint(style.selectionBorderColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRoundedRectangle(Rect2Dd(badgeRect), 5);

        DrawEntryIcon(ctx, dragLeadEntry,
                      Rect2Di(badgeRect.x + padX,
                              badgeRect.y + (badgeRect.height - iconSz) / 2,
                              iconSz, iconSz));
        ctx->SetFontStyle(fsty);
        ctx->SetTextPaint(style.textColor);
        ctx->DrawText(label,
                      Point2Dd(badgeRect.x + padX + iconSz + gap,
                               badgeRect.y + (badgeRect.height - ts.height) / 2.0));
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

    void UltraCanvasFilerWidget::DrawEmptyState(IRenderContext* ctx,
                                                const Rect2Di& bounds,
                                                const std::string& message) {
        // "Nothing to show" notice: an attention icon above the message,
        // vertically centered in the area above the info bar. The icon is a
        // vector-drawn warning triangle, like the icon-menu glyphs, so no
        // icon assets are required.
        Rect2Di area(bounds.x, bounds.y, bounds.width,
                     bounds.height - InfoBarHeight());
        ctx->SetTextPaint(style.secondaryTextColor);
        FontStyle fsty;
        fsty.fontFamily = style.fontFamily;
        fsty.fontSize = style.fontSize;
        ctx->SetFontStyle(fsty);

        const int iconEdge = 44;
        const int gap = 10;
        Size2Di ts = ctx->GetTextLineDimensions(message);
        const int blockHeight = iconEdge + gap + ts.height;
        if (area.height < blockHeight + 8) {
            // Too flat for the stacked layout - the centered text alone.
            ctx->DrawTextInRect(message, Rect2Dd(area));
            return;
        }

        const double cx = area.x + area.width / 2.0;
        const int top = area.y + (area.height - blockHeight) / 2;

        // Triangle sitting on the icon box's bottom edge, apex centered.
        const double w = iconEdge;
        const double h = iconEdge * 0.9;
        const double baseY = top + (iconEdge + h) / 2.0;
        ctx->PushState();
        ctx->SetStrokePaint(style.secondaryTextColor);
        ctx->SetStrokeWidth(2.2f);
        ctx->SetLineCap(LineCap::Round);
        ctx->DrawLinePath({Point2Dd(cx, baseY - h),
                           Point2Dd(cx + w / 2.0, baseY),
                           Point2Dd(cx - w / 2.0, baseY)}, true);
        // Exclamation mark: bar + dot, kept clear of the apex and the base.
        ctx->SetStrokeWidth(2.6f);
        ctx->DrawLine(Point2Dd(cx, baseY - h * 0.60),
                      Point2Dd(cx, baseY - h * 0.32));
        ctx->SetFillPaint(style.secondaryTextColor);
        ctx->FillCircle(Point2Dd(cx, baseY - h * 0.16), 2.0);
        ctx->PopState();

        ctx->DrawText(message,
                      Point2Dd(cx - ts.width / 2.0, top + iconEdge + gap));
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

    std::string UltraCanvasFilerWidget::TruncateTextToWidth(IRenderContext* ctx,
                                                            const std::string& text,
                                                            int maxWidth) const {
        if (maxWidth <= 0) return "";
        if (ctx->GetTextLineDimensions(text).width <= maxWidth) return text;
        // Same binary search as EllipsizeText, without the trailing "…": in a
        // page preview the ellipsis would be most of what a narrow spreadsheet
        // column has room for.
        std::vector<size_t> bounds = Utf8Boundaries(text);
        size_t lo = 0, hi = bounds.size() - 1;
        while (lo < hi) {
            size_t mid = (lo + hi + 1) / 2;
            if (ctx->GetTextLineDimensions(text.substr(0, bounds[mid])).width <= maxWidth)
                lo = mid;
            else
                hi = mid - 1;
        }
        return text.substr(0, bounds[lo]);
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
    // A name that fits its lines completely is then re-broken so the lines
    // come out near equal — "CoderBox" / "compiler.png" rather than the
    // greedy "CoderBox compiler" / ".png" (see WrapText).

    std::vector<std::string> UltraCanvasFilerWidget::WrapTextGreedy(
            IRenderContext* ctx, const std::string& text,
            int lineWidth, int maxLines, bool* outTruncated) const {
        std::vector<std::string> lines;
        std::string rest = text;
        for (int line = 0; line < maxLines && !rest.empty(); ++line) {
            std::vector<size_t> bounds = Utf8Boundaries(rest);
            const bool lastLine = (line == maxLines - 1);

            if (lastLine) {
                if (ctx->GetTextLineDimensions(rest).width <= lineWidth) {
                    lines.push_back(rest);
                    break;
                }
                // Longest tail that fits behind a leading "…" (the shorter the
                // tail the narrower the line, so the fit is monotone in `lo`).
                size_t lo = 0, hi = bounds.size() - 1;
                while (lo < hi) {
                    size_t mid = (lo + hi) / 2;
                    std::string cand = "…" + rest.substr(bounds[mid]);
                    if (ctx->GetTextLineDimensions(cand).width <= lineWidth) hi = mid;
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
                if (ctx->GetTextLineDimensions(rest.substr(0, bounds[mid])).width <= lineWidth)
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

    std::vector<std::string> UltraCanvasFilerWidget::WrapText(
            IRenderContext* ctx, const std::string& text,
            int maxWidth, int maxLines, bool* outTruncated) const {
        if (outTruncated) *outTruncated = false;
        std::vector<std::string> lines;
        if (!ctx || maxWidth <= 0 || text.empty()) return lines;
        if (maxLines < 1) maxLines = 1;

        const int totalWidth = ctx->GetTextLineDimensions(text).width;
        if (totalWidth <= maxWidth) {
            lines.push_back(text);
            return lines;                       // the common case: one measure
        }
        if (maxLines == 1) {
            lines.push_back(EllipsizeText(ctx, text, maxWidth));
            if (outTruncated) *outTruncated = true;
            return lines;
        }

        bool truncated = false;
        lines = WrapTextGreedy(ctx, text, maxWidth, maxLines, &truncated);
        if (outTruncated) *outTruncated = truncated;

        // ===== BALANCED BREAKS =====
        // Greedy filling front-loads the lines and leaves a stub on the last
        // one — "CoderBox compiler" / ".png". When the whole name fits its
        // lines, it is re-broken at the smallest line width that still needs
        // no extra line, which evens the lines out ("CoderBox" /
        // "compiler.png") while the line count — and with it the caption
        // band height — stays exactly the same.
        if (!truncated && lines.size() >= 2) {
            const size_t lineCount = lines.size();
            // No re-break can make every line narrower than the average.
            int lo = clampi(totalWidth / static_cast<int>(lineCount), 1, maxWidth);
            int hi = maxWidth;
            while (lo < hi) {
                const int mid = lo + (hi - lo) / 2;
                bool cut = false;
                const size_t n =
                        WrapTextGreedy(ctx, text, mid, maxLines, &cut).size();
                if (!cut && n <= lineCount) hi = mid;
                else lo = mid + 1;
            }
            if (hi < maxWidth) {
                bool cut = false;
                std::vector<std::string> balanced =
                        WrapTextGreedy(ctx, text, hi, maxLines, &cut);
                if (!cut && balanced.size() <= lineCount)
                    lines = std::move(balanced);
            }
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
        if (ctx->GetTextLineDimensions(name).width <= maxWidth) return 1;
        // Balancing (WrapText) never changes the line count, so the cheaper
        // greedy pass is enough to size the caption band.
        int n = static_cast<int>(
                WrapTextGreedy(ctx, name, maxWidth, maxLines, nullptr).size());
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
        // Executables show their embedded application icon (Windows .exe /
        // .dll / .ico — Explorer-style). Not a content preview, so it is not
        // gated by the Display > Preview toggles; an explicit thumbnail
        // still wins below. On platforms without an extractor this is false
        // for every path.
        if (!e.isDirectory && e.thumbnailPath.empty() &&
            NativeFileIconAvailable(e.path))
            return e.path;
        // Display > Preview: a switched-off kind is never read at all.
        if (!PreviewEnabledFor(e)) return {};
        if (!e.thumbnailPath.empty()) return e.thumbnailPath;
        switch (PreviewTypeOf(e)) {
            case FilerPreviewType::Bitmaps:
            case FilerPreviewType::VectorGraphics:
                // The Image/Vector categories are wider than what the image
                // pipeline decodes (cdr/xar render through graphics plugins).
                return ImagePipelineLoadsExtension(e.extension) ? e.path
                                                                : std::string{};
            // Videos thumbnail as their poster frame (the first frame of the
            // clip), decoded by the same background workers. Without a video
            // backend the capture fails once, the slot is marked Failed and the
            // tile keeps its generic glyph.
            case FilerPreviewType::Videos:
                return e.path;
            // The first page of the document, rendered by the PDF plugin.
            case FilerPreviewType::PDF:
                return PdfPreviewAvailable() ? e.path : std::string{};
            // Only STL is rasterized so far; the other 3D formats have no
            // loader that works without a GL context.
            case FilerPreviewType::Models3D:
                return UltraCanvasSTLLoader::HasSTLExtension(e.path)
                               ? e.path : std::string{};
            // Text, Docs and Spreadsheets have no image to decode: they
            // preview through AcquireTextPreview instead.
            default:
                return {};
        }
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
            if (!PreviewFitsRect(entries[item.entryIndex], r)) continue;
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

    // ===== TEXT-CONTENT PREVIEWS =====
    bool UltraCanvasFilerWidget::AcquireTextPreview(const FilerEntry& e,
                                                    TextPreviewSnippet& out) {
        if (e.isDirectory || e.path.empty()) return false;
        std::lock_guard<std::mutex> lk(thumbMutex);
        auto it = textSlots.find(e.path);
        if (it != textSlots.end()) {
            if (it->second.state == TextPreviewState::Ready) {
                out = it->second.snippet;
                return true;
            }
            if (it->second.state == TextPreviewState::Failed) return false;
            // Pending: re-record the want so the file keeps its place when the
            // queue is rebuilt for this frame.
        } else {
            textSlots.emplace(e.path, TextPreviewSlot{});
        }
        textFrameWants.push_back(e.path);
        return false;
    }

    void UltraCanvasFilerWidget::CommitTextPreviewWants() {
        std::lock_guard<std::mutex> lk(thumbMutex);
        // Rebuilt from scratch each frame in want order, exactly like the
        // image decode queue: only what the current viewport shows is read.
        textQueue.clear();
        std::unordered_set<std::string> wanted;
        wanted.reserve(textFrameWants.size());
        for (const std::string& p : textFrameWants) {
            if (!wanted.insert(p).second) continue;
            textQueue.push_back(p);
        }
        // Finished snippets are kept for scroll-back, but a folder with tens of
        // thousands of documents must not grow the cache without limit: past
        // the cap everything outside the current want set goes.
        constexpr size_t kTextSlotCap = 4096;
        const bool overCap = textSlots.size() > kTextSlotCap;
        for (auto it = textSlots.begin(); it != textSlots.end();) {
            const bool wantedNow = wanted.find(it->first) != wanted.end();
            if (!wantedNow &&
                (overCap || it->second.state == TextPreviewState::Pending)) {
                it = textSlots.erase(it);
            } else {
                ++it;
            }
        }
        textFrameWants.clear();
        if (!textQueue.empty()) {
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
            textQueue.clear();
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
        textQueue.clear();
        textSlots.clear();
    }

    void UltraCanvasFilerWidget::SetCompressedThumbnails(bool enabled) {
        if (compressedThumbs.exchange(enabled) == enabled) return;
        // Existing slots hold the other representation; drop them and let
        // the visible tiles re-decode into the new one.
        DropThumbnailCache();
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::SetFlexibleTileWidths(bool enabled) {
        if (flexibleTileWidths == enabled) return;
        flexibleTileWidths = enabled;
        // Only the thumbnail grid layout depends on this, and the image boxes
        // keep their size either way — relayout, no decode is redone.
        InvalidateFilerLayout();
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
        // Vectors scale to fill the tile, everything else draws a glyph — and
        // so does a bitmap whose preview kind is switched off.
        if (e.category != FilerFileCategory::Image) return 0.0f;
        if (!PreviewEnabledFor(e)) return 0.0f;
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
            std::string textPath;      // set instead of req for a text preview
            uint64_t textGeneration = 0;
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
                    // Nothing to decode: read a text-content preview instead.
                    // Image work always wins, because a tile waiting for a
                    // photo is the more visible gap.
                    while (!textQueue.empty() && textPath.empty()) {
                        std::string p = std::move(textQueue.front());
                        textQueue.pop_front();
                        auto sit = textSlots.find(p);
                        if (sit == textSlots.end() ||
                            sit->second.state != TextPreviewState::Pending ||
                            textPathsInFlight.count(p) != 0) {
                            continue;
                        }
                        textPathsInFlight.insert(p);
                        textGeneration = thumbGeneration;
                        textPath = std::move(p);
                    }
                    if (!textPath.empty()) break;
                    thumbCond.wait(lk);
                }
            }

            if (!textPath.empty()) {
                // Reading + un-wrapping a document, outside the lock.
                TextPreviewSnippet snippet;
                const bool readable = ExtractTextPreview(
                        textPath, snippet.lines, snippet.tabular);
                bool textReport = false;
                {
                    std::lock_guard<std::mutex> lk(thumbMutex);
                    textPathsInFlight.erase(textPath);
                    if (thumbShutdown) return;
                    if (textGeneration == thumbGeneration) {
                        TextPreviewSlot& slot = textSlots[textPath];
                        slot.state = (readable && !snippet.lines.empty())
                                             ? TextPreviewState::Ready
                                             : TextPreviewState::Failed;
                        slot.snippet = std::move(snippet);
                        textReport = slot.state == TextPreviewState::Ready;
                    }
                }
                if (textReport) PostThumbnailRedraw();
                continue;
            }

            // The expensive part — outside the lock. UCImage::Get and
            // GetPixmap populate the shared mutex-guarded caches, so later
            // synchronous users (e.g. the media viewer) get free cache hits.
            // Which producer runs is decided by the file itself, not by the
            // entry: the request may name an entry's explicit thumbnail image
            // rather than the entry's own file.
            std::shared_ptr<UCPixmap> pm;
            if (NativeFileIconAvailable(req.path)) {
                // The icon embedded in an executable (or an .ico file),
                // extracted by the OS shell at the nearest embedded size.
                const int edge = std::max(1, static_cast<int>(std::lround(
                        std::max(req.w, req.h) * req.scale)));
                pm = LoadNativeFileIconPixmap(req.path, edge);
            } else switch (PreviewTypeForPath(req.path)) {
                case FilerPreviewType::Videos: {
                    // Poster frame of a video (may block for a few seconds on
                    // a cold file — that is exactly what these workers are
                    // for).
                    VideoThumbnailRequest vreq;
                    vreq.maxWidth = std::max(
                            1, static_cast<int>(std::lround(req.w * req.scale)));
                    vreq.maxHeight = std::max(
                            1, static_cast<int>(std::lround(req.h * req.scale)));
                    pm = CaptureVideoThumbnailPixmap(req.path, vreq);
                    break;
                }
                case FilerPreviewType::PDF:
                    pm = RenderPdfPreviewPixmap(req.path, req.w, req.h, req.scale);
                    break;
                case FilerPreviewType::Models3D:
                    pm = RenderModelPreviewPixmap(req.path, req.w, req.h, req.scale);
                    break;
                default: {
                    auto img = UCImage::Get(req.path);
                    if (img && img->GetWidth() > 0 && img->GetHeight() > 0) {
                        pm = img->GetPixmap(req.w, req.h, req.fit, req.scale);
                    }
                    break;
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
        if (!PreviewFitsRect(e, rect)) thumb.clear();
        if (!thumb.empty()) {
            auto pm = AcquireThumbnail(thumb, rect.width, rect.height,
                                       imageFit, ctx->GetDeviceScale());
            if (pm) {
                ctx->DrawPixmap(*pm, Rect2Dd(rect), imageFit);
                return;
            }
        }

        // Text-shaped files (Text / Docs / Spreadsheets) preview as a
        // miniature page of their own content once the background read
        // finished. Only where a page would be legible at all: the icon
        // column of the Details and List rows keeps the type glyph.
        if (thumb.empty() && !e.isDirectory && rect.width >= kContentPreviewMinEdge &&
            rect.height >= kContentPreviewMinEdge && PreviewEnabledFor(e)) {
            const FilerPreviewType kind = PreviewTypeOf(e);
            if (kind == FilerPreviewType::Text || kind == FilerPreviewType::Docs ||
                kind == FilerPreviewType::Spreadsheets) {
                TextPreviewSnippet snippet;
                if (AcquireTextPreview(e, snippet)) {
                    DrawTextPreview(ctx, rect, snippet);
                    return;
                }
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

    void UltraCanvasFilerWidget::DrawTextPreview(IRenderContext* ctx,
                                                 const Rect2Di& rect,
                                                 const TextPreviewSnippet& snippet) {
        // The sheet the content sits on: a white page with a hairline border,
        // so a text preview reads as a document even where the content itself
        // is too small to decipher.
        ctx->SetFillPaint(Color(255, 255, 255, 255));
        ctx->FillRoundedRectangle(Rect2Dd(rect), 2);
        ctx->SetStrokePaint(Color(0, 0, 0, 45));
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRoundedRectangle(Rect2Dd(rect), 2);

        const int pad = std::max(2, rect.height / 18);
        const Rect2Di inner(rect.x + pad, rect.y + pad,
                            rect.width - 2 * pad, rect.height - 2 * pad);
        if (inner.width < 8 || inner.height < 8 || snippet.lines.empty()) return;

        // Scale the type to the tile: a maximized tile shows readable text, a
        // small one shows the shape of the content.
        FontStyle fsty;
        fsty.fontFamily = style.fontFamily;
        fsty.fontSize = std::max(4.5, std::min<double>(style.smallFontSize,
                                                       inner.height / 11.0));
        ctx->SetFontStyle(fsty);
        const int lineH = std::max(4, static_cast<int>(
                std::lround(fsty.fontSize * 1.35)));
        const size_t maxLines = std::min<size_t>(
                snippet.lines.size(),
                std::max(1, inner.height / lineH));

        ctx->PushState();
        ctx->ClipRect(Rect2Dd(inner));
        if (snippet.tabular) {
            // Spreadsheet-shaped content: the cells of each row spread over
            // equal columns with the grid drawn behind them.
            size_t columns = 1;
            for (size_t i = 0; i < maxLines; ++i) {
                columns = std::max<size_t>(
                        columns,
                        static_cast<size_t>(std::count(snippet.lines[i].begin(),
                                                       snippet.lines[i].end(),
                                                       '\t')) + 1);
            }
            const double colW = static_cast<double>(inner.width) / columns;
            ctx->SetStrokePaint(Color(0, 0, 0, 28));
            ctx->SetStrokeWidth(1.0f);
            for (size_t c = 1; c < columns; ++c) {
                const double x = inner.x + c * colW;
                ctx->DrawLine(Point2Dd(x, inner.y),
                              Point2Dd(x, inner.y + maxLines * lineH));
            }
            for (size_t i = 1; i <= maxLines; ++i) {
                const double y = inner.y + i * lineH;
                if (y > inner.y + inner.height) break;
                ctx->DrawLine(Point2Dd(inner.x, y),
                              Point2Dd(inner.x + inner.width, y));
            }
            for (size_t i = 0; i < maxLines; ++i) {
                const std::string& row = snippet.lines[i];
                size_t start = 0, column = 0;
                while (start <= row.size() && column < columns) {
                    const size_t tab = row.find('\t', start);
                    const std::string cell = row.substr(
                            start, tab == std::string::npos ? std::string::npos
                                                            : tab - start);
                    if (!cell.empty()) {
                        ctx->SetTextPaint(i == 0 ? style.textColor
                                                 : style.secondaryTextColor);
                        // Cells are cut, not ellipsized: in a tile-sized grid
                        // the "…" would be all that is left of the value.
                        ctx->DrawText(TruncateTextToWidth(
                                              ctx, cell, static_cast<int>(colW) - 3),
                                      Point2Dd(inner.x + column * colW + 2,
                                               inner.y + i * lineH + 1));
                    }
                    if (tab == std::string::npos) break;
                    start = tab + 1;
                    ++column;
                }
            }
        } else {
            // The first line stands out like a document title; what follows is
            // body text. Lines are cut at the right margin the way a page cuts
            // them, without an ellipsis.
            ctx->SetTextPaint(style.textColor);
            for (size_t i = 0; i < maxLines; ++i) {
                if (i == 1) ctx->SetTextPaint(style.secondaryTextColor);
                ctx->DrawText(TruncateTextToWidth(ctx, snippet.lines[i], inner.width),
                              Point2Dd(inner.x, inner.y + i * lineH));
            }
        }
        ctx->PopState();
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

    // ===== FOLDER WATCH =====
    // The shown folder can change without the widget doing anything: another
    // application saves a file into it, a download finishes, a script deletes
    // one. A background worker re-fingerprints the folder every interval and
    // the UI timer below turns a changed fingerprint into a Refresh().

    void UltraCanvasFilerWidget::SetFolderWatchEnabled(bool enabled) {
        if (folderWatchEnabled == enabled) return;
        folderWatchEnabled = enabled;
        if (enabled) {
            WatchFolder(fileListMode ? std::string() : currentPath);
        } else {
            WatchFolder("");
            StopFolderWatchTimer();
            folderWatchDirty.store(false);
        }
    }

    void UltraCanvasFilerWidget::SetFolderWatchIntervalMs(int ms) {
        const int clamped = std::max(250, ms);
        if (folderWatchIntervalMs == clamped) return;
        folderWatchIntervalMs = clamped;
        folderWatchCond.notify_all();   // the worker picks the new period up
        if (folderWatchTimer != InvalidTimerId) {
            StopFolderWatchTimer();
            ArmFolderWatchTimer();
        }
    }

    uint64_t UltraCanvasFilerWidget::FolderSignature(const std::string& path,
                                                     bool includeHidden) {
        // FNV-1a over the facts a listing shows. Order-independent per entry
        // (each entry's own hash is mixed in with a commutative add), so the
        // order directory_iterator happens to return does not matter.
        auto fnv = [](uint64_t h, uint64_t v) {
            h ^= v;
            h *= 1099511628211ull;
            return h;
        };
        uint64_t total = 0;
        uint64_t count = 0;

        // std::filesystem throughout rather than ::stat: a path is wide on
        // Windows, where ::stat neither takes what path::c_str() returns nor
        // reaches a name outside the local codepage.
        std::error_code ec;
        const auto dirTime = fs::last_write_time(path, ec);
        if (ec) return 0;   // gone or unreadable: signature 0
        uint64_t dirHash = fnv(1469598103934665603ull,
                static_cast<uint64_t>(dirTime.time_since_epoch().count()));

        for (fs::directory_iterator it(path, ec), end; it != end; it.increment(ec)) {
            if (ec) break;
            const std::string name = it->path().filename().string();
            if (!includeHidden && !name.empty() && name[0] == '.') continue;
            uint64_t h = 1469598103934665603ull;
            for (unsigned char c : name) h = fnv(h, c);
            // The directory_entry answers from what the scan already read where
            // the platform supplies it, so these are not extra syscalls.
            std::error_code entryEc;
            uint64_t sizeValue = 0;
            if (!it->is_directory(entryEc)) {
                const auto size = it->file_size(entryEc);
                sizeValue = entryEc ? 0ull : static_cast<uint64_t>(size);
            }
            h = fnv(h, sizeValue);
            entryEc.clear();
            const auto mtime = it->last_write_time(entryEc);
            if (!entryEc)
                h = fnv(h, static_cast<uint64_t>(mtime.time_since_epoch().count()));
            total += h;
            ++count;
        }
        return fnv(dirHash, total) ^ (count * 1099511628211ull);
    }

    void UltraCanvasFilerWidget::WatchFolder(const std::string& path) {
        std::error_code ec;
        // Only a real directory is watched: an archive interior or a file list
        // has no folder whose changes would mean anything here.
        const std::string target =
                (folderWatchEnabled && !path.empty() && fs::is_directory(path, ec))
                        ? path : std::string();
        {
            std::lock_guard<std::mutex> lk(folderWatchMutex);
            if (folderWatchShutdown) return;
            const bool sameFolder = (folderWatchPath == target) &&
                                    (folderWatchIncludeHidden == showHiddenFiles);
            folderWatchPath = target;
            folderWatchIncludeHidden = showHiddenFiles;
            // A new folder (or a changed hidden-files setting) needs a fresh
            // baseline: the worker's first fingerprint of it only measures.
            if (!sameFolder) {
                folderWatchHaveBaseline = false;
                folderWatchSignature = 0;
            }
            if (target.empty()) return;
            StartFolderWatchWorkerLocked();
        }
        folderWatchDirty.store(false);   // whatever was pending described the old folder
        folderWatchCond.notify_all();
        ArmFolderWatchTimer();
    }

    void UltraCanvasFilerWidget::StartFolderWatchWorkerLocked() {
        if (folderWatchWorker.joinable() || folderWatchShutdown) return;
        folderWatchWorker = std::thread([this]() { FolderWatchWorkerMain(); });
    }

    void UltraCanvasFilerWidget::StopFolderWatchWorker() {
        {
            std::lock_guard<std::mutex> lk(folderWatchMutex);
            folderWatchShutdown = true;
            folderWatchPath.clear();
        }
        folderWatchCond.notify_all();
        if (folderWatchWorker.joinable()) folderWatchWorker.join();
    }

    void UltraCanvasFilerWidget::FolderWatchWorkerMain() {
        // The worker never touches widget state beyond the guarded fields and
        // the atomic flag, so teardown is a plain shutdown + join.
        for (;;) {
            std::string path;
            bool includeHidden = false;
            uint64_t known = 0;
            bool haveBaseline = false;
            {
                std::unique_lock<std::mutex> lk(folderWatchMutex);
                folderWatchCond.wait_for(
                        lk, std::chrono::milliseconds(folderWatchIntervalMs),
                        [this]() { return folderWatchShutdown; });
                if (folderWatchShutdown) return;
                path = folderWatchPath;
                includeHidden = folderWatchIncludeHidden;
                known = folderWatchSignature;
                haveBaseline = folderWatchHaveBaseline;
            }
            if (path.empty()) continue;

            const uint64_t now = FolderSignature(path, includeHidden);

            std::lock_guard<std::mutex> lk(folderWatchMutex);
            if (folderWatchShutdown) return;
            // Navigated (or the setting changed) while we were scanning: that
            // fingerprint describes a folder nobody is looking at any more.
            if (path != folderWatchPath || includeHidden != folderWatchIncludeHidden)
                continue;
            folderWatchSignature = now;
            if (!haveBaseline) {
                folderWatchHaveBaseline = true;   // first pass only measures
                continue;
            }
            if (now != known) folderWatchDirty.store(true);
        }
    }

    bool UltraCanvasFilerWidget::IsBusyForAutoRefresh() const {
        // A rescan rebuilds `entries` and drops the thumbnail cache: harmless
        // on its own, disastrous in the middle of something the user is doing.
        // The dirty flag stays set, so the refresh lands the moment they stop.
        return renamingIndex >= 0 || pendingRenameIndex >= 0 ||
               draggingItems || dragOutArmed || marqueeActive || marqueeArmed ||
               compressDlg.active || activePopupMenu || archiveJob ||
               pendingPaste || pendingDelete
#ifdef ULTRACANVAS_HAS_VIRTUALFS
               || pendingExtract
#endif
               ;
    }

    void UltraCanvasFilerWidget::ArmFolderWatchTimer() {
        if (folderWatchTimer != InvalidTimerId) return;
        auto* app = UltraCanvasApplication::GetInstance();
        if (!app) return;
        folderWatchTimer = app->StartTimer(folderWatchIntervalMs, true,
                                           [this](TimerId) {
            if (!folderWatchDirty.load()) return;
            if (IsBusyForAutoRefresh()) return;   // try again on the next tick
            folderWatchDirty.store(false);
            Refresh();
            // The listing changed under the host too: its status bar counts and
            // any preview fed from the selection describe the folder as well.
            if (onFolderRefreshed) onFolderRefreshed();
        });
    }

    void UltraCanvasFilerWidget::StopFolderWatchTimer() {
        if (folderWatchTimer == InvalidTimerId) return;
        if (auto* app = UltraCanvasApplication::GetInstance())
            app->StopTimer(folderWatchTimer);
        folderWatchTimer = InvalidTimerId;
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
        if (onFileActivated) {
            onFileActivated(e);
            return;
        }
        if (activateOpensDefault) {
            // No host callback: Explorer semantics for simple embedders.
            OpenEntryWithOS(e);
        }
    }

    void UltraCanvasFilerWidget::OpenEntryWithOS(const FilerEntry& e) {
        // Only a real file — an entry inside an archive is a virtual path no
        // external application (or the kernel) can read.
        std::error_code ec;
        if (!fs::is_regular_file(e.path, ec) || ec) return;
        std::string error;
        switch (FileAssociations::ClassifyExecutable(e.path)) {
            case FileAssociations::ExecutableKind::Binary:
                // A native program: running it IS opening it (on Windows
                // this case never fires — ShellExecute's "open" verb below
                // already runs executables).
                if (!FileAssociations::LaunchExecutable(e.path, error))
                    ReportError(error);
                return;
            case FileAssociations::ExecutableKind::Script:
                // A script is as much a document as a program — ask.
                ShowRunOrOpenDialog(e);
                return;
            default:
                break;
        }
        if (!FileAssociations::OpenWithDefaultApplication({e.path}, error))
            ReportError(error);
    }

    void UltraCanvasFilerWidget::ShowRunOrOpenDialog(const FilerEntry& e) {
        DialogConfig cfg;
        cfg.title = "Executable Script";
        cfg.dialogType = DialogType::Question;
        cfg.message = "\"" + e.name + "\" is an executable script.";
        cfg.details = "Run it, or open it to view its contents?";
        cfg.buttons = DialogButtons::NoButtons;   // custom buttons added below
        cfg.width = 520;
        cfg.height = 200;

        auto dialog = UltraCanvasDialogManager::CreateDialog(cfg);
        if (!dialog) {   // dialogs disabled — open, the old fixed behavior
            std::string error;
            if (!FileAssociations::OpenWithDefaultApplication({e.path}, error))
                ReportError(error);
            return;
        }

        auto self = this;
        const std::string path = e.path;
        dialog->AddCustomButton("Run", DialogResult::Yes, nullptr);
        dialog->AddCustomButton("Open", DialogResult::No, nullptr);
        dialog->AddCustomButton("Cancel", DialogResult::Cancel, nullptr);
        dialog->onResult = [self, path](DialogResult result) {
            std::string error;
            if (result == DialogResult::Yes) {
                if (!FileAssociations::LaunchExecutable(path, error))
                    self->ReportError(error);
            } else if (result == DialogResult::No) {
                if (!FileAssociations::OpenWithDefaultApplication({path}, error))
                    self->ReportError(error);
            }
        };
        UltraCanvasDialogManager::ShowDialog(dialog, nullptr, GetWindow());
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

        // Open with > , first in the menu: opening a file is what the menu
        // is opened for most often. The entry itself opens the selection
        // with the OS default application — a click on "Open with" is a
        // double-click. Its submenu lists the applications the OS registers
        // for the selection (default application first — see
        // UltraCanvasFileAssociations), then the host's AddOpenWithApp
        // entries, then the file-dialog picker. Both need launchable paths:
        // files only (no folders), really on disk (an entry inside an
        // archive is a virtual path no external application can read).
        {
            bool openable = hasSel;
            std::vector<std::string> targetPaths;
            for (const FilerEntry& t : targets) {
                if (t.isDirectory) { openable = false; break; }
                targetPaths.push_back(t.path);
            }
            if (openable) {
                std::error_code ec;
                for (const std::string& p : targetPaths) {
                    if (fs::is_regular_file(p, ec) && !ec) continue;
                    openable = false;
                    break;
                }
            }
            // The OS-registered section is what SetSystemOpenWithEnabled
            // switches off; the default-open click stays either way.
            const bool launchable = systemOpenWith && openable;

            std::vector<MenuItemData> openItems;
            if (launchable) {
                // Served from the prewarm cache (the folder scan queued this
                // folder's extensions) — no database parse on the UI thread.
                for (const FileAssociationApp& app :
                     FileAssociations::GetApplicationsForFiles(targetPaths)) {
                    auto cb = [this, app]() {
                        std::vector<std::string> paths;
                        for (const FilerEntry& e : GetSelectedEntries())
                            if (!e.isDirectory) paths.push_back(e.path);
                        if (paths.empty()) return;
                        std::string error;
                        if (!FileAssociations::OpenWithApplication(app, paths, error))
                            ReportError(error);
                    };
                    if (!app.iconPath.empty()) {
                        openItems.push_back(MenuItemData::Action(app.name, app.iconPath, cb));
                    } else {
                        openItems.push_back(MenuItemData::Action(app.name, cb));
                    }
                }
            }
            if (!openWithApps.empty() && !openItems.empty())
                openItems.push_back(MenuItemData::Separator());
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
            if (launchable) {
                if (!openItems.empty())
                    openItems.push_back(MenuItemData::Separator());
                openItems.push_back(MenuItemData::Action(
                        "Other application…",
                        [this]() { OpenSelectionWithChooser(); }));
            }
            if (openItems.empty()) {
                MenuItemData none = MenuItemData::Action("(no applications)", []() {});
                none.enabled = false;
                openItems.push_back(none);
            }
            MenuItemData openWith = MenuItemData::Submenu("Open with", openItems);
            if (openable) {
                openWith.onClick = [this]() { OpenSelectionWithDefaultApp(); };
            }
            menu.AddItem(openWith);
        }
        menu.AddItem(MenuItemData::Separator());

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
            // A folder first, above the document kinds and set apart from them
            // — it is the entry this submenu is opened for most often.
            newItems.push_back(MenuItemData::ActionWithShortcut(
                    "Folder", "Ctrl+F", [this]() { CreateNewFolder(); }));
            newItems.push_back(MenuItemData::Separator());
            for (const FilerNewDocumentType& t : newDocumentTypes) {
                FilerNewDocumentType copy = t;
                newItems.push_back(MenuItemData::Action(
                        t.label, [this, copy]() { CreateNewDocument(copy); }));
            }
            menu.AddItem(MenuItemData::Submenu("New", newItems));
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
        addAction("Extract", anyArchive, [this]() { OpenExtractDialog(); });
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

            // The host's tail (extrasMenuProvider) — asked on every open so
            // item flags can follow the host's state (e.g. pinned-or-not).
            if (extrasMenuProvider) {
                std::vector<MenuItemData> hostItems = extrasMenuProvider();
                if (!hostItems.empty()) {
                    extraItems.push_back(MenuItemData::Separator());
                    for (MenuItemData& item : hostItems)
                        extraItems.push_back(std::move(item));
                }
            }

            menu.AddItem(MenuItemData::Submenu("Extras", extraItems));
        }

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

            // Preview > which file kinds show their content instead of the
            // plain type glyph. All on by default.
            std::vector<MenuItemData> previewItems;
            struct PreviewOption { const char* label; FilerPreviewType type; };
            static const PreviewOption previewOptions[] = {
                {"Bitmaps",         FilerPreviewType::Bitmaps},
                {"Vector graphics", FilerPreviewType::VectorGraphics},
                {"3D",              FilerPreviewType::Models3D},
                {"PDF",             FilerPreviewType::PDF},
                {"Text",            FilerPreviewType::Text},
                {"Docs",            FilerPreviewType::Docs},
                {"Spreadsheets",    FilerPreviewType::Spreadsheets},
                {"Videos",          FilerPreviewType::Videos},
            };
            for (const PreviewOption& o : previewOptions) {
                FilerPreviewType t = o.type;
                previewItems.push_back(MenuItemData::Checkbox(
                        o.label, IsPreviewTypeEnabled(t),
                        [this, t](bool on) { SetPreviewType(t, on); }));
            }

            std::vector<MenuItemData> displayItems;
            displayItems.push_back(MenuItemData::Submenu("Sort", sortItems));
            displayItems.push_back(MenuItemData::Submenu("Type", typeItems));
            displayItems.push_back(MenuItemData::Submenu("Preview", previewItems));
            displayItems.push_back(MenuItemData::Submenu("Dataset", datasetItems));
            displayItems.push_back(MenuItemData::Checkbox(
                    "Icon-Menu", hoverIconMenu,
                    [this](bool on) { SetHoverIconMenuEnabled(on); }));
            displayItems.push_back(MenuItemData::Checkbox(
                    "Info-Bar", showSelectionInfo,
                    [this](bool on) { SetSelectionInfoVisible(on); }));
            menu.AddItem(MenuItemData::Submenu("Display", displayItems));
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
                    // Drop: the configured default (move, unless the host set
                    // "copy files"), with Ctrl forcing a copy and Shift a move.
                    bool copy = dropOnFolderCopies;
                    if (event.ctrl)       copy = true;
                    else if (event.shift) copy = false;
                    FinishItemDrag(Point2Di(event.pointer.x, event.pointer.y),
                                   copy);
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
                        case 'f': case 'F': CreateNewFolder(); return true;
                        case 'p': case 'P':
                            if (onPrint) onPrint(SelectionOrAll());
                            return true;
                        default: break;
                    }
                    return false;
                }
                switch (event.virtualKey) {
                    // Both Enters open the selected entry: the numeric
                    // keypad's Return arrives as its own key code.
                    case UCKeys::NumPadEnter:
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
