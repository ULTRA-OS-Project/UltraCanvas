// Apps/DemoApp/UltraCanvasImagePerformanceTest.cpp
// Bitmap codec comparison benchmark: file size / encode time / decode time per format
// Version: 2.7.0 - the shared page look (UltraCanvasDemoPageKit): the page fills the
//                  area, toolbar, chart and status in one card, every label sized
//                  to its text, the toolbar wraps
// Version: 2.7.1 - HEIF and AVIF are charted only where this build can encode them
// Version: 2.6.1 - JPEG XL is benchmarked again wherever libvips can write it
// Version: 2.8.0 - the formats come from FileLoader's inventory and are chosen in
//                  a Settings dialog (gear at the right of the toolbar); JP2K is
//                  off by default; a popup on opening asks for an image
// Version: 2.9.0 - QOI and BMP are written and read through FileLoader like every
//                  other format; the page's own encoders and decoders are gone
// Version: 2.9.1 - the format choice is kept between runs, in the demo's
//                  settings file (UltraCanvasAppSettings "UltraCanvasDemo")
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework
//
// ================================================================================
// VERSION 2.9.0 - EVERY FORMAT THROUGH FILELOADER
// ================================================================================
// QOI and BMP were the last two formats the page wrote with encoders of its
// own (qoi.h's qoi_write, a 24bpp BI_RGB writer in this file) and read with
// decoders of its own - against the rule that every file goes through
// UltraCanvasFileLoader, and timing code no application runs. Both now take
// the same path as the rest: UCImage::Save with GetImageExportOptions, then
// UCImage::Load with a forced full decode. They are offered where FileLoader
// writes and reads them (the framework's VipsQoiLoader for QOI, libvips for
// BMP), like any other format. QOI and BMP figures are therefore not
// comparable with runs before this version: they now include libvips' own
// pipeline, as every other column always did.
//
// ================================================================================
// VERSION 2.8.0 - FORMAT SETTINGS FROM FILELOADER, OPENING HINT
// ================================================================================
// The fixed codec table is gone. The page lists every bitmap format
// UltraCanvasFileLoader reports as both written and read by this build
// (GetSupportedFormats(Bitmap): canSave && canLoad) and that the image writer
// takes options for (GetImageExportOptions), plus the two the page encoded
// itself until 2.9.0 (QOI, BMP). Each libvips format is written with the options
// GetImageExportOptions gives - its Save settings - so the numbers are the
// ones a Save in any application would produce. Which formats are timed is
// chosen in a dialog of switches behind the gear button at the right end of
// the toolbar, and remembered (since 2.9.1 across restarts, in the demo's
// settings file). The [x] Include BMP
// checkbox went into that list. On by default: PNG, QOI, JPG, WebP, AVIF,
// HEIF, GIF, TIFF, JXL - JPEG 2000 is off (it took far longer to encode than
// any other and stretched the time axis), and so is BMP and every format the
// page did not chart before. Opening the page shows a popup asking for an
// image to start the test.
//
// ================================================================================
// VERSION 2.7.1 - HEIF AND AVIF ONLY WHERE THEY ENCODE
// ================================================================================
// CodecAvailable now covers HEIF and AVIF as well as JXL: libheif can be
// present with no encoder for a format (the macOS build reads HEIC but has no
// x265 to write it), and its column then only ever showed a failure. Each is
// charted where VipsCanEncode and VipsCanLoad say this build handles it.
//
// ================================================================================
// VERSION 2.6.1 - JPEG XL BACK IN THE CODEC LIST
// ================================================================================
// JXL was taken out in 0.1.35 because the libvips of the day had no JPEG XL
// support, so its column always read as failed. It is back, between TIFF and
// BMP, and is left out of the active set (CodecAvailable) where libvips
// cannot both save and load .jxl - Ubuntu 22.04's libvips has no libjxl,
// 24.04's has. Tests/ImageJxlTest checks the same UCImage::Save /
// UCImage::Load round trip this page times.
//
// ================================================================================
// VERSION 2.5.7 - LEGEND ROW CENTRED HORIZONTALLY
// ================================================================================
// The title is horizontally centred but the legend was previously left-
// aligned at finalBounds.x + leftMargin, leaving a visual mismatch. This
// release measures the total legend width up front (sum of swatch + gap
// + label, plus inter-item gaps) and starts drawing from a centred X so
// the legend row lines up with the title above it.
//
// ================================================================================
// VERSION 2.5.6 - LEGEND/TITLE LAYOUT (TIGHTENED)
// ================================================================================
// v2.5.5 finally got text/swatch alignment correct (using empirical evidence
// from RenderXAxisLabels that DrawText's Y is the text top), but the inter-
// row gap of 14px and topMargin of 70px left a large empty strip between
// the legend and the plot area. This release:
//   - Reduces gap from 14 → 4, placing the legend immediately under the title
//   - Reverts topMargin from 70 → 48, eliminating the empty strip
// Centring formula and ordering unchanged from v2.5.5.
//
// ================================================================================
// VERSION 2.5.5 - LEGEND/TITLE LAYOUT (EMPIRICALLY VERIFIED)
// ================================================================================
// Previous attempts (2.5.1 through 2.5.4) all used incorrect vertical-
// centring formulas because of guesses about whether DrawText's Y is the
// text top or baseline. This release verifies it directly against a piece
// of code in the same file that already renders correctly:
//
//   RenderXAxisLabels (line ~1226) draws codec names at:
//       Y = plot.y + plot.height + 4
//   and the names appear *below* the X-axis line.
//
//   If Y were the baseline, the text would appear *above* that Y (text
//   sits on its baseline). It appears below, so Y is the TOP of the text.
//
// With Y = top confirmed, both text and swatch are now centred inside
// their row using the identical formula:
//     elementY = rowTop + (rowH - elementHeight) / 2
// applied to title text, legend swatch (rectangle top), and legend label
// (text top). All three end up visually centred on the same horizontal
// line within their respective rows.
//
// ================================================================================
// VERSION 2.5.4 - LAYOUT: ENLARGE TOP MARGIN
// ================================================================================
// Repeated attempts to fit the chart title + legend in the existing 48px
// top margin all produced visible overlap on this renderer — the title kept
// landing on the same visual row as the legend. Root cause was simply that
// 48px was not enough vertical real-estate for two rows of text plus a
// visible gap.
//
// This release increases topMargin from 48 to 70 and bumps the inter-row
// gap from 8 to 14, giving title and legend each their own row with
// generous breathing room. The vertical-centring formula and ordering
// (title above, legend below) are unchanged from v2.5.3.
//
// ================================================================================
// VERSION 2.5.3 - LEGEND/TITLE LAYOUT (incomplete — superseded by 2.5.4)
// ================================================================================
// v2.5.2 produced a layout where the legend rendered above the title,
// regardless of which Y was passed to which DrawText call. Empirically this
// renderer offsets text downwards from the supplied Y by approximately the
// text height — meaning the row drawn at a smaller Y consistently ends up
// visually below the row drawn at a larger Y when using `(rowH-textH)/2`
// centring.
//
// This release returns to the v2.5.1 vertical-centring formula
// `(rowH + textH) / 2` (which empirically lands text correctly inside its
// row in this renderer) AND keeps title-then-legend ordering with an
// 8px gap, so the title sits visibly above the legend with no overlap.
//
// ================================================================================
// VERSION 2.5.2 - LEGEND/TITLE LAYOUT FIX (incorrect — superseded by 2.5.3)
// ================================================================================
// v2.5.1 split title and legend into two rows but used the wrong vertical
// offset formula — it assumed DrawText's Y argument is the text baseline,
// when in this renderer it is the TOP of the text glyph box. Result: text
// rendered noticeably below the centre of its row, which made the title
// land at swatch height (visually fused with the legend) and made the
// legend labels drift below their colour swatches.
//
// This release uses the correct formula for top-anchored text:
//     textY = rowTop + (rowH - textHeight) / 2
// applied symmetrically to title, legend labels, and colour swatches. The
// inter-row gap is also bumped from 4px to 6px for clearer separation.
//
// ================================================================================
// VERSION 2.5.1 - LEGEND/TITLE LAYOUT FIX (incorrect — superseded by 2.5.2)
// ================================================================================
// Two layout bugs in CodecComparisonChartElement::RenderLegend():
//
// 1. Title (centered) and legend swatches (left-aligned) shared the same
//    vertical strip. On narrow viewports the centered title's text would
//    drift left and overlap the "Decode time" label. Fixed by stacking
//    title and legend in two distinct rows with an explicit gap.
//
// 2. Legend label text was misaligned relative to its colour swatch — the
//    text drifted noticeably below the swatch box. Caused by passing
//    `swatchY + swatchSize - 1` as the DrawText Y, which assumed one
//    text-baseline convention but the renderer follows the other on this
//    platform. Fixed by computing both the swatch top and text baseline
//    from a shared row centre, using the actual text height measured at
//    runtime — works regardless of the backend's baseline-vs-top choice.
//
// ================================================================================
// VERSION 2.5.0 - REMOVE [Run Benchmark] BUTTON
// ================================================================================
// The benchmark has fully auto-run since v2.3.0 — the [Run Benchmark] button
// existed only as a manual re-run option, but in practice every entry point
// (load image, toggle BMP) already triggers a fresh run. Removing the button
// simplifies the toolbar to its essential controls: [Choose Image...] +
// [x] Include BMP + filename label.
//
// ================================================================================
// VERSION 2.4.0 - STANDALONE BMP ENCODER
// ================================================================================
// libvips has no BMP writer, so previously enabling [x] Include BMP would fail
// the BMP codec with "encoder not available". This release ships a small
// in-tree BMP encoder/decoder pair (uncompressed BI_RGB, 24bpp) so BMP can be
// genuinely benchmarked alongside the libvips and QOI codecs.
//
// To accommodate a third backend cleanly, the CodecEntry::isQOI bool has been
// replaced by an EncoderBackend enum { Libvips, QOI, BMP }. This generalises
// to additional standalone codecs (e.g. a future GIF encoder) without further
// schema churn — runBenchmark dispatches on the enum via a single switch.
//
// The BMP encoder writes uncompressed RGB pixels via the documented Windows
// BITMAPFILEHEADER + BITMAPINFOHEADER layout (rows bottom-up, BGR order, each
// row padded to a 4-byte boundary). The decoder reads the same layout back to
// allocate and free the pixel buffer, producing a comparable decode timing.
//
// ================================================================================
// VERSION 2.3.0 - UX REFINEMENTS
// ================================================================================
// 1. Auto-run: benchmark fires automatically as soon as an image is loaded via
//    [Choose Image...]. (In v2.5.0 the [Run Benchmark] button was removed
//    altogether — every action that changes inputs triggers a fresh run.)
// 2. BMP excluded by default: BMP encodes uncompressed (≈ width*height*3 bytes),
//    which dwarfs every other codec on the size axis and squashes the chart.
//    A new [x] Include BMP checkbox in the toolbar opts BMP back in. Toggling
//    the checkbox between runs triggers a fresh benchmark on the loaded image.
// 3. Codec X-axis order regrouped by family:
//        PNG, QOI, JPG, JP2K, WebP, AVIF, HEIF, GIF, TIFF, JXL  (+ BMP last
//        when enabled). PNG sits next to its lossless cousin QOI; JPG sits
//        next to JP2K; remaining lossy/modern codecs follow.
// 4. Info-label refresh fix: the toolbar's filename/dimensions label now
//    correctly repaints when a new image is chosen (previously only thumbnails
//    updated). Calls RequestRedraw() after SetText().
// 5. Thumbnail staleness fix: state->runStamp is regenerated at the start of
//    every benchmark so each run produces unique temp file paths. Without
//    this, repeat runs reused identical paths and UCImage's path-keyed cache
//    served the previous run's pixels into the thumbnails.
//
// Internal restructuring to support the above:
//   - Codec table is now a single CodecEntry[] array with an isQOI flag, so
//     QOI is no longer hard-coded as "the last index". Reordering the array
//     reorders the chart.
//   - The Run-Benchmark logic is extracted into a runBenchmark lambda so it
//     can be invoked from the button click, from Choose-Image (auto-run), and
//     from the BMP checkbox toggle.
//   - codecNames + thumbs are rebuilt from the active codec set whenever the
//     BMP checkbox flips, so the chart's group count matches reality.
//
// ================================================================================
// VERSION 2.2.0 - EAGER DECODE TIMING (CORRECTNESS FIX)
// ================================================================================
// Libvips uses lazy evaluation: UCImage::Load() typically only parses the file
// header, deferring pixel decompression until pixels are read. Previous versions
// timed only that header-parse, severely understating true decode cost for PNG,
// JPG, WebP, TIFF, and JP2K — and making them unfairly comparable to QOI, which
// always decompresses every pixel eagerly.
//
// 2.2.0 forces libvips to materialize all pixels during the timed region by
// calling VImage::copy_memory() on the loaded image. Decode times now reflect
// real file→RAM decompression work for every codec in the benchmark.
//
// ================================================================================
// VERSION 2.1.x - TOOLTIP LAYOUT FIXES
// ================================================================================
// Author: UltraCanvas Framework
//
// ================================================================================
// VERSION 2.1.0 - HOVER TOOLTIP
// ================================================================================
// Adds a per-codec hover tooltip drawn inline inside the chart element. Hovering
// over any codec's group column displays detailed metrics (size, encode, decode,
// derived MB/s), or the failure reason for codecs that failed. The tooltip flips
// horizontally/vertically to stay inside the chart bounds near edges.
//
// ================================================================================
// VERSION 2.0.0 - COMPLETE REWRITE
// ================================================================================
// REPLACES the previous iteration-count benchmark (v1.0.0, 2025-12-24).
//
// NEW WORKFLOW:
//   [Choose Image...] button opens native file dialog at <resources>/media/images.
//   [Run Benchmark]   button iterates all 11 codecs, measuring:
//       - encoded file size (bytes)
//       - encode time       (ms)
//       - decode time       (ms)
//   Results stream into a custom grouped-bar chart (3 bars per codec):
//       black = file size, green = encode time, blue = decode time
//   A thumbnail row under the X-axis shows each codec's re-encoded preview.
//   Clicking any thumbnail opens the 1:1 full-size image viewer.
//
// CODEC LIST (11 codecs):
//   PNG, JPEG, WebP, AVIF, HEIF, GIF, BMP, TIFF, JPEG2000, JXL
//       (via UCImage::Save -> libvips)
//   QOI
//       (via bundled qoi.h / qoi_encode from libspecific/Cairo/qoi.h;
//        QOI_IMPLEMENTATION is provided by the standalone libspecific/Cairo/
//        qoi.cpp, so we only include the header and link against those symbols)
//
// Since 2.8.0 the list is FileLoader's bitmap inventory (see the 2.8.0 notes);
// the formats above are the ones on by default, with JPEG2000 left off. Since
// 2.9.0 QOI goes through UCImage::Save as well; no bundled codec is left.
// ================================================================================

#include "UltraCanvasContainer.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8 / GetEnvUtf8
#include "UltraCanvasSpacer.h"
#include "CSSLayout/CSSLayout.h"
#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasSwitch.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasModalDialog.h"   // FileFilter, the settings dialog, the opening popup
#include "UltraCanvasConfig.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasAppSettings.h"   // the format choice, kept between runs

#ifdef HAS_LIBVIPS
#include <vips/vips8>
#endif

#include <chrono>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>
#include <map>
#include <cctype>

namespace UltraCanvas {

// ============================================================================
// EXTERNAL HELPER (defined in UltraCanvasBitmapFormatDemo.cpp)
// Opens the file in the demo's shared UltraCanvasMediaViewerWindow.
// ============================================================================
    void ShowInMediaViewer(const std::string& path);

// ============================================================================
// FILE-SCOPE HELPERS (anonymous namespace — no symbols escape this TU)
// ============================================================================
    namespace {

        struct CodecResult {
            UCImageSaveFormat format = UCImageSaveFormat::PNG;
            std::string name;               // "PNG", "JPG", "QOI", ...
            std::string extension;          // "png", "jpg", "qoi", ...
            std::string encodedFilePath;    // path of the temp file produced
            size_t      encodedSizeBytes = 0;
            double      encodeTimeMs = 0.0;
            double      decodeTimeMs = 0.0;
            bool        succeeded = false;
            std::string errorMessage;
        };

        // Monotonic millisecond clock for timing measurements.
        double NowMs() {
            using namespace std::chrono;
            return duration<double, std::milli>(
                    steady_clock::now().time_since_epoch()).count();
        }

        std::string TempDirBase() {
#if defined(_WIN32) || defined(_WIN64)
            // UTF-8 from the wide environment: the files are written by
            // UTF-8 path, and getenv would answer in the ANSI code page.
            std::string tmp = GetEnvUtf8("TEMP");
            if (tmp.empty()) tmp = GetEnvUtf8("TMP");
            if (tmp.empty()) tmp = "C:\\Temp";
            return tmp;
#else
            const char* tmp = std::getenv("TMPDIR");
            if (tmp && *tmp) return std::string(tmp);
            return "/tmp";
#endif
        }

        std::string FormatBytes(size_t bytes) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1);
            if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
                oss << (bytes / (1024.0 * 1024.0 * 1024.0)) << " GB";
            } else if (bytes >= 1024ULL * 1024ULL) {
                oss << (bytes / (1024.0 * 1024.0)) << " MB";
            } else if (bytes >= 1024ULL) {
                oss << (bytes / 1024.0) << " KB";
            } else {
                oss << bytes << " B";
            }
            return oss.str();
        }

        std::string FormatMs(double ms) {
            std::ostringstream oss;
            if (ms >= 1000.0) {
                oss << std::fixed << std::setprecision(2) << (ms / 1000.0) << " s";
            } else if (ms >= 10.0) {
                oss << std::fixed << std::setprecision(0) << ms << " ms";
            } else {
                oss << std::fixed << std::setprecision(1) << ms << " ms";
            }
            return oss.str();
        }

        // ============ CODEC LIST ============
        // Every codec the benchmark can drive in this build, in X-axis order.
        // Each is written with UCImage::Save and read with UCImage::Load - the
        // image writer and reader FileLoader stands for.
        struct CodecEntry {
            UCImageSaveFormat format = UCImageSaveFormat::PNG;
            std::string name;          // chart label: "PNG", "JP2K", ...
            std::string extension;     // FileLoader's canonical extension
            std::string description;   // FileLoader's name for the format
            bool enabledByDefault = false;
        };

        // The formats the page always charted, in their X-axis order: the
        // lossless pair first (PNG, QOI), then JPG and its successor JP2K,
        // then the modern lossy / animated / legacy ones. Anything else
        // FileLoader writes follows in its inventory's order.
        const std::vector<std::string>& PreferredOrder() {
            static const std::vector<std::string> order = {
                    "png", "qoi", "jpg", "jp2", "webp", "avif", "heic", "gif", "tiff", "jxl", "bmp"};
            return order;
        }

        // The chart's short label for a format.
        std::string ChartNameFor(const std::string& ext) {
            static const std::map<std::string, std::string> names = {
                    {"png", "PNG"}, {"qoi", "QOI"}, {"jpg", "JPG"}, {"jp2", "JP2K"},
                    {"webp", "WebP"}, {"avif", "AVIF"}, {"heic", "HEIF"}, {"gif", "GIF"},
                    {"tiff", "TIFF"}, {"jxl", "JXL"}, {"bmp", "BMP"}};
            auto it = names.find(ext);
            if (it != names.end()) return it->second;
            std::string upper = ext;
            for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return upper;
        }

        // On until the user says otherwise. JPEG 2000 is off: its encoder is
        // several times slower than any other and stretches the time axis
        // until every other bar is a sliver. BMP is off because uncompressed
        // RGB (width * height * 3 bytes) does the same to the size axis, and
        // the formats the page did not chart before are off so that the
        // default chart stays readable.
        bool EnabledByDefault(const std::string& ext) {
            static const std::vector<std::string> on = {
                    "png", "qoi", "jpg", "webp", "avif", "heic", "gif", "tiff", "jxl"};
            return std::find(on.begin(), on.end(), ext) != on.end();
        }

        // Every bitmap format FileLoader reports this build both writes and
        // reads, and that the image writer takes options for - the same list
        // a Save dialog offers. A format with no encoder here (JPEG XL without
        // libjxl, HEIC on macOS without x265) is not in the inventory, so it
        // is not offered rather than shown as a failure.
        std::vector<CodecEntry> BuildCodecList() {
            std::vector<CodecEntry> found;
            for (const MediaFormatInfo& f :
                    UltraCanvasFileLoader::GetSupportedFormats(MediaFormatCategory::Bitmap)) {
                if (!f.canSave || !f.canLoad) continue;
                UCImageSave::ImageExportOptions opts;
                if (!UltraCanvasFileLoader::GetImageExportOptions("probe." + f.extension, opts)) continue;
                CodecEntry e;
                e.format = opts.format;
                e.extension = f.extension;
                e.description = f.description;
                e.name = ChartNameFor(f.extension);
                e.enabledByDefault = EnabledByDefault(f.extension);
                found.push_back(std::move(e));
            }
            const auto& order = PreferredOrder();
            auto rank = [&order](const CodecEntry& e) {
                auto it = std::find(order.begin(), order.end(), e.extension);
                return static_cast<int>(it - order.begin());   // the rest: order.size()
            };
            std::stable_sort(found.begin(), found.end(),
                             [&rank](const CodecEntry& a, const CodecEntry& b) { return rank(a) < rank(b); });
            return found;
        }

        // What the user switched on or off in the settings dialog, one
        // value per extension under "imagePerformance.formats" in the demo's
        // settings file (<settings folder>/UltraCanvasDemo/settings.json), so
        // the choice outlives a restart. Read once, on first use; written on
        // Apply. A format never switched follows its default, and so does
        // every format when the file is missing or unreadable.
        UltraCanvasAppSettings& FormatSettings() {
            static UltraCanvasAppSettings settings = []() {
                UltraCanvasAppSettings s("UltraCanvasDemo");
                s.Load();   // false for a first run: the defaults then
                return s;
            }();
            return settings;
        }

        std::string FormatKey(const std::string& extension) {
            return "imagePerformance.formats." + extension;
        }

        bool IsCodecEnabled(const CodecEntry& e) {
            const UltraCanvasAppSettings& settings = FormatSettings();
            const std::string key = FormatKey(e.extension);
            return settings.Has(key) ? settings.GetBool(key, e.enabledByDefault) : e.enabledByDefault;
        }

        // Stores the switches' states and writes the file at once, so a
        // crash or a kill later loses nothing.
        void StoreFormatChoice(const std::vector<std::pair<std::string, bool>>& choice) {
            UltraCanvasAppSettings& settings = FormatSettings();
            for (const auto& [extension, on] : choice) settings.Set(FormatKey(extension), on);
            if (!settings.Save())
                debugOutput << "Image Performance Test: could not write " << settings.File() << std::endl;
        }

        std::string InitialImageDir() {
            std::string dir = GetResourcesDir() + "media/images";
            std::error_code ec;
            if (std::filesystem::exists(UltraCanvas::PathFromUtf8(dir), ec)) return dir;
            std::string alt = GetResourcesDir() + "media";
            if (std::filesystem::exists(UltraCanvas::PathFromUtf8(alt), ec)) return alt;
            return PathToUtf8(std::filesystem::current_path(ec));
        }

    } // namespace (anonymous)

// ============================================================================
// CUSTOM CHART ELEMENT — grouped bars with dual Y-axis
// ============================================================================
// Inherits from UltraCanvasUIElement (per Rule 10 of master_types_registry_V4:
// "All UI components must inherit from UltraCanvasUIElement"). Paints three
// bars per codec slot (black = size, green = encode, blue = decode) scaled
// against two independent maxima (bytes on left axis, ms on right axis).
//
// This is a file-local class because it is only used inside this demo tab;
// it therefore doesn't need a registry entry.
// ============================================================================
    class CodecComparisonChartElement : public UltraCanvasUIElement {
    public:
        CodecComparisonChartElement(const std::string& identifier)
                : UltraCanvasUIElement(identifier) {}

        void SetCodecNames(const std::vector<std::string>& names) {
            codecNames = names;
            RequestRedraw();
        }

        void SetResults(const std::vector<CodecResult>& newResults) {
            results = newResults;
            RequestRedraw();
        }

        void SetThumbnailImages(const std::vector<std::shared_ptr<UltraCanvasImageElement>>& thumbs) {
            thumbnailImages = thumbs;
            RequestRedraw();
        }

        // Callback invoked from Render() whenever the chart's bounds have
        // changed since the last paint. The demo uses this to reposition
        // thumbnails under the bar groups without needing framework-level
        // bounds-change events.
        std::function<void()> onGeometryChanged;

        // Public geometry accessors so the demo can align thumbnails with bar groups.
        Rect2Di GetPlotArea() const {
            return ComputePlotArea(GetBounds());
        }

        int GetGroupCenterX(int index) const {
            Rect2Di plot = ComputePlotArea(GetBounds());
            size_t n = codecNames.size();
            if (n == 0 || index < 0 || static_cast<size_t>(index) >= n) {
                return plot.x;
            }
            float groupWidth = static_cast<float>(plot.width) / static_cast<float>(n);
            return static_cast<int>(plot.x + (index + 0.5f) * groupWidth);
        }

        int GetGroupInnerWidth() const {
            Rect2Di plot = ComputePlotArea(GetBounds());
            size_t n = codecNames.size();
            if (n == 0) return 0;
            float groupWidth = static_cast<float>(plot.width) / static_cast<float>(n);
            float groupGap = groupWidth * groupGapRatio;
            int innerW = static_cast<int>(groupWidth - groupGap);
            if (innerW < 8) innerW = 8;
            return innerW;
        }

        int GetXAxisBottomY() const {
            Rect2Di plot = ComputePlotArea(GetBounds());
            return plot.y + plot.height;
        }

        bool OnEvent(const UCEvent& event) override {
            switch (event.type) {
                case UCEventType::MouseMove: {
                    int localX = event.pointer.x;
                    int localY = event.pointer.y;
                    int newHover = HitTestGroup(localX, localY);
                    if (newHover != hoveredIndex) {
                        hoveredIndex = newHover;
                        RequestRedraw();
                    }
                    tooltipAnchorX = localX;
                    tooltipAnchorY = localY;
                    if (hoveredIndex >= 0) RequestRedraw();
                    return false;   // don't consume — let siblings still see it
                }
                case UCEventType::MouseLeave: {
                    if (hoveredIndex != -1) {
                        hoveredIndex = -1;
                        RequestRedraw();
                    }
                    return false;
                }
                default:
                    break;
            }
            return UltraCanvasUIElement::OnEvent(event);
        }

        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override {
            if (!ctx || !IsVisible()) return;
            Rect2Di bounds = GetLocalBounds();
            if (finalBounds.width < 40 || finalBounds.height < 40) return;

            // Fire geometry-changed callback if bounds have shifted since the last paint.
            if (finalBounds.x != lastRenderBounds.x || finalBounds.y != lastRenderBounds.y ||
                finalBounds.width != lastRenderBounds.width || finalBounds.height != lastRenderBounds.height) {
                lastRenderBounds = bounds;
                if (onGeometryChanged) onGeometryChanged();
            }

            ctx->PushState();

            // Background panel
            ctx->SetFillPaint(backgroundColor);
            ctx->FillRectangle(bounds);
            ctx->SetStrokePaint(borderColor);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawRectangle(bounds);

            RenderLegend(ctx, bounds);

            Rect2Di plotArea = ComputePlotArea(bounds);
            if (plotArea.width < 20 || plotArea.height < 20) {
                ctx->PopState();
                return;
            }

            // Scale maxima (never zero)
            double maxBytes = 1.0;
            double maxMs    = 1.0;
            for (const auto& r : results) {
                if (!r.succeeded) continue;
                if (static_cast<double>(r.encodedSizeBytes) > maxBytes)
                    maxBytes = static_cast<double>(r.encodedSizeBytes);
                if (r.encodeTimeMs > maxMs) maxMs = r.encodeTimeMs;
                if (r.decodeTimeMs > maxMs) maxMs = r.decodeTimeMs;
            }
            maxBytes *= 1.10;
            maxMs    *= 1.10;

            RenderGrid(ctx, plotArea, maxBytes, maxMs);
            RenderBars(ctx, plotArea, maxBytes, maxMs);
            RenderXAxisLabels(ctx, plotArea);
            RenderTooltip(ctx, bounds);

            ctx->PopState();
        }

    private:
        // ===== STYLE =====
        Color backgroundColor    = Color(255, 255, 255, 255);
        Color borderColor        = Color(180, 190, 200, 255);
        Color axisColor          = Color(60, 60, 60, 255);
        Color gridColor          = Color(225, 228, 232, 255);
        Color textColor          = Color(40, 40, 40, 255);
        Color titleColor         = Color(40, 80, 120, 255);

        Color sizeBarColor       = Color(0, 0, 0, 255);        // black
        Color encodeBarColor     = Color(10, 190, 30, 255);    // green
        Color decodeBarColor     = Color(40, 110, 255, 255);   // blue

        int topMargin            = 48;  // space for legend / title (two rows)
        int leftMargin           = 72;  // left Y-axis labels
        int rightMargin          = 72;  // right Y-axis labels
        int bottomMargin         = 48;  // space for codec labels + thumbnails

        float titleFontSize      = 13.0f;
        float legendFontSize     = 11.0f;
        float axisFontSize       = 10.0f;
        float labelFontSize      = 11.0f;

        float groupGapRatio      = 0.22f;
        float barGapRatio        = 0.10f;

        // ===== STATE =====
        std::vector<std::string> codecNames;
        std::vector<CodecResult> results;
        std::vector<std::shared_ptr<UltraCanvasImageElement>> thumbnailImages;

        // Tracks last-rendered bounds so Render() can detect geometry changes
        // and fire onGeometryChanged without a framework-level resize event.
        Rect2Di lastRenderBounds = Rect2Di(0, 0, 0, 0);

        // Hover state for per-codec tooltip. -1 means no group is hovered.
        int hoveredIndex   = -1;
        int tooltipAnchorX = 0;
        int tooltipAnchorY = 0;

        // Tooltip styling
        Color tooltipBgColor     = Color(32, 36, 42, 240);
        Color tooltipBorderColor = Color(80, 90, 105, 255);
        Color tooltipTextColor   = Color(240, 242, 245, 255);
        Color tooltipMutedColor  = Color(170, 178, 190, 255);
        Color tooltipErrorColor  = Color(255, 130, 110, 255);
        float tooltipFontSize    = 11.0f;

        // ===== HELPERS =====
        // Returns codec index [0..n-1] if (localX, localY) falls inside a codec's
        // group column (in chart-local element coordinates). Returns -1 otherwise.
        // Hit area is the full-height column above the X-axis band, so the user
        // doesn't have to pixel-aim at the bars themselves.
        int HitTestGroup(int localX, int localY) const {
            Rect2Di b = GetLocalBounds();
            int ex = localX;
            int ey = localY;
            Rect2Di plot = ComputePlotArea(b);
            if (ex < plot.x || ex >= plot.x + plot.width) return -1;
            // Vertical band spans plot area + X-axis label strip (bottomMargin),
            // so labels and just below are also "sticky".
            int yTop = plot.y;
            int yBot = plot.y + plot.height + bottomMargin;
            if (ey < yTop || ey > yBot) return -1;
            size_t n = codecNames.size();
            if (n == 0) return -1;
            float groupWidth = static_cast<float>(plot.width) /
                               static_cast<float>(n);
            int idx = static_cast<int>((ex - plot.x) / groupWidth);
            if (idx < 0) idx = 0;
            if (idx >= static_cast<int>(n)) idx = static_cast<int>(n) - 1;
            return idx;
        }

        Rect2Di ComputePlotArea(const Rect2Di& b) const {
            int px = b.x + leftMargin;
            int py = b.y + topMargin;
            int pw = b.width - leftMargin - rightMargin;
            int ph = b.height - topMargin - bottomMargin;
            if (pw < 10) pw = 10;
            if (ph < 10) ph = 10;
            return Rect2Di(px, py, pw, ph);
        }

        void RenderLegend(IRenderContext* ctx, const Rect2Di& bounds) {
            // Vertical centring formula (empirically verified against
            // RenderXAxisLabels which renders correctly):
            //
            //   This renderer's DrawText interprets the Y argument as the
            //   TOP of the text glyph box. Proof: codec names at the X-axis
            //   pass Y = plot.y + plot.height + 4 and appear *below* the
            //   axis line (they would appear *above* if Y were the baseline).
            //
            // So to centre an element of height `h` inside a row of height
            // `rowH`, we use:
            //     elementY = rowTop + (rowH - h) / 2
            // applied identically to text (h = textHeight) and to swatch
            // rectangles (h = swatchSize). This makes their visual centres
            // align on the same horizontal line.

            // ---- Title (top row) ----
            ctx->SetFontSize(titleFontSize);
            ctx->SetTextPaint(titleColor);
            const std::string title = "Codec comparison: file size / encode / decode";
            auto dims = ctx->GetTextLineDimensions(title);
            int tw = dims.width, th = dims.height;

            const int titleRowTop = finalBounds.y + 4;
            const int titleRowH   = th + 2;
            ctx->DrawText(title,
                          Point2Dd(finalBounds.x + (finalBounds.width - tw) / 2,
                          titleRowTop + (titleRowH - th) / 2));

            // ---- Legend (lower row) ----
            ctx->SetFontSize(legendFontSize);
            const int swatchSize = 10;

            int legendTextH = 0;
            {
                auto dims = ctx->GetTextLineDimensions("Decode time");
                int probeW = dims.width, probeH = dims.height;
                legendTextH = probeH;
            }

            const int gap          = 4;
            const int legendRowH   = std::max(swatchSize, legendTextH) + 2;
            const int legendRowTop = titleRowTop + titleRowH + gap;

            // Both text and swatch use the SAME centring formula now —
            // (rowH - elementHeight) / 2 — so their tops align such that
            // their visual centres land on the same horizontal line.
            const int swatchY = legendRowTop + (legendRowH - swatchSize)  / 2;
            const int textY   = legendRowTop + (legendRowH - legendTextH) / 2;

            struct LegendItem { Color c; const char* label; };
            const LegendItem items[3] = {
                    { sizeBarColor,   "File size" },
                    { encodeBarColor, "Encode time" },
                    { decodeBarColor, "Decode time" }
            };

            // Per-item layout constants (must match draw loop below)
            const int swatchTextGap = 5;   // px between swatch and its label
            const int interItemGap  = 22;  // px between one item's label and next swatch

            // Pass 1 — measure total legend width so the row can be centred
            // horizontally beneath the (centred) chart title. Each item's
            // contribution is: swatch + gap + text. Items are joined by
            // interItemGap. We re-measure each label here so the layout
            // adapts if labels change in future.
            int totalLegendWidth = 0;
            int labelWidths[3] = {0, 0, 0};
            for (int i = 0; i < 3; ++i) {
                auto dims = ctx->GetTextLineDimensions(items[i].label);
                int lw = dims.width, lh = dims.height;
                labelWidths[i] = lw;
                totalLegendWidth += swatchSize + swatchTextGap + lw;
                if (i < 2) totalLegendWidth += interItemGap;
            }

            // Pass 2 — draw, starting from the X that centres the row in the
            // chart bounds.
            int swatchX = finalBounds.x + (finalBounds.width - totalLegendWidth) / 2;

            for (int i = 0; i < 3; ++i) {
                const auto& it = items[i];
                ctx->SetFillPaint(it.c);
                ctx->FillRectangle({swatchX, swatchY, swatchSize, swatchSize});
                ctx->SetTextPaint(textColor);
                ctx->DrawText(it.label,
                              {swatchX + swatchSize + swatchTextGap,
                              textY});
                swatchX += swatchSize + swatchTextGap + labelWidths[i] + interItemGap;
            }
        }

        void RenderGrid(IRenderContext* ctx, const Rect2Di& plot,
                        double maxBytes, double maxMs) {
            // Plot area border
            ctx->SetStrokePaint(axisColor);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawLine({plot.x, plot.y}, {plot.x, plot.y + plot.height});                 // left Y
            ctx->DrawLine({plot.x + plot.width, plot.y},                                   // right Y
                          {plot.x + plot.width, plot.y + plot.height});
            ctx->DrawLine({plot.x, plot.y + plot.height},                                  // X axis
                           {plot.x + plot.width, plot.y + plot.height});

            // Gridlines + tick labels
            ctx->SetFontSize(axisFontSize);
            const int ticks = 5;
            for (int i = 0; i <= ticks; ++i) {
                float t = static_cast<float>(i) / ticks;
                int y = static_cast<int>(plot.y + plot.height - t * plot.height);

                if (i > 0 && i < ticks) {
                    ctx->SetStrokePaint(gridColor);
                    ctx->DrawLine({plot.x + 1, y}, {plot.x + plot.width - 1, y});
                }

                // Left label: bytes
                std::string leftTxt = FormatBytes(static_cast<size_t>(maxBytes * t));
                ctx->SetTextPaint(textColor);
                auto dims = ctx->GetTextLineDimensions(leftTxt);
                int tw = dims.width, th = dims.height;
                ctx->DrawText(leftTxt, {plot.x - tw - 6, y + th / 2 - 2});

                // Right label: ms
                std::string rightTxt = FormatMs(maxMs * t);
                dims = ctx->GetTextLineDimensions(rightTxt);
                tw = dims.width, th = dims.height;
                ctx->DrawText(rightTxt, {plot.x + plot.width + 6, y + th / 2 - 2});
            }

            // Axis captions
            ctx->SetFontSize(axisFontSize);
            ctx->SetTextPaint(textColor);
            ctx->DrawText("Size", {plot.x - 28, plot.y - 8});
            ctx->DrawText("Time", {plot.x + plot.width + 6, plot.y - 8});
        }

        void RenderBars(IRenderContext* ctx, const Rect2Di& plot,
                        double maxBytes, double maxMs) {
            size_t n = codecNames.size();
            if (n == 0) return;

            float groupWidth = static_cast<float>(plot.width) / static_cast<float>(n);
            float groupGap   = groupWidth * groupGapRatio;
            float innerW     = groupWidth - groupGap;
            float barW       = innerW / 3.0f;
            float barGap     = barW * barGapRatio;
            float actualBarW = barW - barGap;
            if (actualBarW < 1.5f) actualBarW = 1.5f;

            auto clamp01 = [](double v) {
                if (v < 0.0) return 0.0;
                if (v > 1.0) return 1.0;
                return v;
            };

            auto drawBar = [&](float bx, double norm, const Color& color) {
                float h = static_cast<float>(norm * plot.height);
                if (h < 0.5f) h = 0.5f;
                float y = plot.y + plot.height - h;
                ctx->SetFillPaint(color);
                ctx->FillRectangle({bx, y, actualBarW, h});
            };

            for (size_t i = 0; i < n; ++i) {
                if (i >= results.size()) continue;
                const CodecResult& r = results[i];
                if (!r.succeeded) continue;

                double sizeN = clamp01(static_cast<double>(r.encodedSizeBytes) / maxBytes);
                double encN  = clamp01(r.encodeTimeMs / maxMs);
                double decN  = clamp01(r.decodeTimeMs / maxMs);

                float gx   = plot.x + i * groupWidth + groupGap / 2.0f;
                float bar0 = gx + 0 * barW + barGap / 2.0f;
                float bar1 = gx + 1 * barW + barGap / 2.0f;
                float bar2 = gx + 2 * barW + barGap / 2.0f;

                drawBar(bar0, sizeN, sizeBarColor);
                drawBar(bar1, encN,  encodeBarColor);
                drawBar(bar2, decN,  decodeBarColor);
            }
        }

        void RenderTooltip(IRenderContext* ctx, const Rect2Di& chartBounds) {
    if (hoveredIndex < 0) return;
    if (hoveredIndex >= static_cast<int>(codecNames.size())) return;

    // Build the tooltip text lines
    struct Line { std::string text; Color color; bool isTitle; };
    std::vector<Line> lines;

    const std::string& codecName = codecNames[hoveredIndex];
    lines.push_back({ codecName, tooltipTextColor, true });
    lines.push_back({ std::string(), tooltipMutedColor, false });  // separator

    if (hoveredIndex < static_cast<int>(results.size())) {
        const CodecResult& r = results[hoveredIndex];
        if (r.succeeded) {
            lines.push_back({ "Size:    " + FormatBytes(r.encodedSizeBytes), tooltipTextColor, false });
            lines.push_back({ "Encode:  " + FormatMs(r.encodeTimeMs), tooltipTextColor, false });
            lines.push_back({ "Decode:  " + FormatMs(r.decodeTimeMs), tooltipTextColor, false });

            if (r.decodeTimeMs > 0.01) {
                double mbPerSec = (static_cast<double>(r.encodedSizeBytes) / (1024.0 * 1024.0)) /
                                  (r.decodeTimeMs / 1000.0);
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(1) << mbPerSec << " MB/s decode";
                lines.push_back({ oss.str(), tooltipMutedColor, false });
            }
        } else {
            lines.push_back({ "Failed", tooltipErrorColor, true });
            if (!r.errorMessage.empty()) {
                const std::string& msg = r.errorMessage;
                const size_t wrapAt = 48;
                size_t pos = 0;
                while (pos < msg.size()) {
                    size_t take = std::min(wrapAt, msg.size() - pos);
                    if (take < msg.size() - pos) {
                        size_t sp = msg.rfind(' ', pos + take);
                        if (sp != std::string::npos && sp > pos) {
                            take = sp - pos;
                        }
                    }
                    lines.push_back({ msg.substr(pos, take), tooltipMutedColor, false });
                    pos += take;
                    while (pos < msg.size() && msg[pos] == ' ') ++pos;
                }
            }
        }
    } else {
        lines.push_back({ "No result yet", tooltipMutedColor, false });
    }

    // Constants for layout
    const int paddingX = 12;
    const int paddingY = 10;
    const int separatorHeight = 6;
    const int lineSpacing = 2;
    
    // Measure all lines - store both width and height
    struct LineMetrics {
        int width;
        int height;
    };
    std::vector<LineMetrics> metrics;
    int maxWidth = 0;
    int totalContentHeight = 0;
    
    // First pass: measure all lines (Y = top coordinate system)
    for (size_t i = 0; i < lines.size(); ++i) {
        const auto& line = lines[i];
        
        if (line.text.empty()) {
            // Separator
            metrics.push_back({0, separatorHeight});
            totalContentHeight += separatorHeight;
            continue;
        }
        
        // Set appropriate font size for measurement
        if (line.isTitle) {
            ctx->SetFontSize(tooltipFontSize + 3.0f);
        } else {
            ctx->SetFontSize(tooltipFontSize);
        }
        
        auto dims = ctx->GetTextLineDimensions(line.text);
        int w = dims.width, h = dims.height;
        metrics.push_back({w, h});
        
        if (w > maxWidth) maxWidth = w;
        totalContentHeight += h;
        
        // Add spacing between text lines
        if (i < lines.size() - 1 && !line.text.empty() && !lines[i+1].text.empty()) {
            totalContentHeight += lineSpacing;
        }
    }
    
    // Calculate tooltip dimensions
    int tooltipWidth = maxWidth + paddingX * 2;
    int tooltipHeight = totalContentHeight + paddingY * 2;
    
    // Position tooltip
    int tooltipX = tooltipAnchorX + 14;
    int tooltipY = tooltipAnchorY + 14;
    
    // Flip horizontally if needed
    if (tooltipX + tooltipWidth > chartBounds.x + chartBounds.width - 4) {
        tooltipX = tooltipAnchorX - tooltipWidth - 14;
    }
    if (tooltipX < chartBounds.x + 4) {
        tooltipX = chartBounds.x + 4;
    }
    
    // Flip vertically if needed
    if (tooltipY + tooltipHeight > chartBounds.y + chartBounds.height - 4) {
        tooltipY = tooltipAnchorY - tooltipHeight - 14;
    }
    if (tooltipY < chartBounds.y + 4) {
        tooltipY = chartBounds.y + 4;
    }
    
    // Draw background
    ctx->SetFillPaint(tooltipBgColor);
    ctx->FillRectangle({tooltipX, tooltipY, tooltipWidth, tooltipHeight});
    ctx->SetStrokePaint(tooltipBorderColor);
    ctx->SetStrokeWidth(1.0f);
    ctx->DrawRectangle(Rect2Di(tooltipX, tooltipY, tooltipWidth, tooltipHeight));
    
    // Calculate vertical centering
    int extraSpace = tooltipHeight - (totalContentHeight + paddingY * 2);
    int verticalOffset = extraSpace / 2;
    
    // Draw all lines - Y coordinate is TOP of text
    int currentY = tooltipY + paddingY + verticalOffset;
    
    for (size_t i = 0; i < lines.size(); ++i) {
        const auto& line = lines[i];
        const auto& metric = metrics[i];
        
        if (line.text.empty()) {
            // Draw separator line - centered in its vertical space
            int lineY = currentY + (separatorHeight / 2);
            ctx->SetStrokePaint(tooltipBorderColor);
            ctx->DrawLine({tooltipX + paddingX, lineY},
                          {tooltipX + tooltipWidth - paddingX, lineY});
            currentY += separatorHeight;
            continue;
        }
        
        // Set font and color
        if (line.isTitle) {
            ctx->SetFontSize(tooltipFontSize + 3.0f);
        } else {
            ctx->SetFontSize(tooltipFontSize);
        }
        ctx->SetTextPaint(line.color);
        
        // Calculate X position
        int textX = tooltipX + paddingX;
        if (line.isTitle) {
            // Center title
            textX = tooltipX + (tooltipWidth - metric.width) / 2;
        }
        
        // Draw text - Y coordinate is TOP of the text bounding box
        ctx->DrawText(line.text, {textX, currentY});
        
        // Move to next line
        currentY += metric.height;
        
        // Add spacing if next line is text (not separator)
        if (i + 1 < lines.size() && !lines[i+1].text.empty()) {
            currentY += lineSpacing;
        }
    }
}

        void RenderXAxisLabels(IRenderContext* ctx, const Rect2Di& plot) {
            size_t n = codecNames.size();
            if (n == 0) return;
            ctx->SetFontSize(labelFontSize);
            ctx->SetTextPaint(textColor);

            float groupWidth = static_cast<float>(plot.width) / static_cast<float>(n);
            for (size_t i = 0; i < n; ++i) {
                auto dims = ctx->GetTextLineDimensions(codecNames[i]);
                int tw = dims.width, th = dims.height;
                float cx = plot.x + (i + 0.5f) * groupWidth;
                ctx->DrawText(codecNames[i],
                              Point2Dd(cx - tw / 2,
                              plot.y + plot.height + 4));
            }
        }
    };

// ============================================================================
// MAIN DEMO FACTORY: CreateImagePerformanceTest()
// ============================================================================
// Replaces the v1.0.0 iteration-count benchmark. Function signature is
// preserved so the demo app's tab registration continues to work unchanged.
//
// LAYOUT (Rule 15 compliant — VBox + HBox everywhere, zero manual coords):
//
//   page (DemoKit: title, description; fills the page area)
//     card (stretch)
//       toolbar     (wrapping HBox: Choose | Info | Settings gear)
//       chart+thumbs VBox (stretch)
//           CodecComparisonChartElement
//           thumbsRow (one thumbnail ImageElement per format, under its bars)
//       status label
// ============================================================================
    std::shared_ptr<UltraCanvasUIElement>
    UltraCanvasDemoApplication::CreateImagePerformanceTest() {

        // Shared mutable state for all lambdas
        struct PanelState {
            std::string sourcePath;
            UCImagePtr  sourceImage;
            int         sourceWidth = 0;
            int         sourceHeight = 0;
            std::vector<CodecResult> results;

            // Every format this build can time (BuildCodecList), in X-axis
            // order. state->thumbs is parallel to it.
            std::vector<CodecEntry> codecs;

            // UI refs
            std::shared_ptr<UltraCanvasButton>   chooseBtn;
            std::shared_ptr<UltraCanvasButton>   settingsBtn;
            std::shared_ptr<UltraCanvasLabel>    infoLabel;
            std::shared_ptr<UltraCanvasLabel>    statusLabel;
            std::shared_ptr<CodecComparisonChartElement> chartElem;
            std::shared_ptr<UltraCanvasContainer> thumbsRow;
            std::vector<std::shared_ptr<UltraCanvasImageElement>> thumbs;

            // Chart group names of the formats switched on, in display order.
            std::vector<std::string> codecNames;

            // Indices into codecs for the formats switched on. Length matches
            // codecNames.size() and the chart's group count.
            std::vector<int> activeCodecIndices;

            // True while a benchmark run is in flight — keeps the settings
            // dialog from changing the format set mid-run.
            bool benchmarkRunning = false;

            // One temp-file suffix per run (so parallel UltraCanvas instances
            // don't stomp each other's temp files).
            std::string runStamp;
        };
        auto state = std::make_shared<PanelState>();

        // Unique run stamp for temp files (nanoseconds since epoch).
        {
            auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            std::ostringstream s;
            s << "ultracanvas_bench_" << stamp;
            state->runStamp = s.str();
        }

        state->codecs = BuildCodecList();

        // The formats switched on (IsCodecEnabled), in display order.
        // Mutates state->codecNames and state->activeCodecIndices in lockstep.
        auto rebuildActiveCodecs = [state]() {
            state->codecNames.clear();
            state->activeCodecIndices.clear();
            for (int i = 0; i < static_cast<int>(state->codecs.size()); ++i) {
                if (!IsCodecEnabled(state->codecs[i])) continue;
                state->codecNames.push_back(state->codecs[i].name);
                state->activeCodecIndices.push_back(i);
            }
        };
        rebuildActiveCodecs();

        // ========== PAGE (title + description) ==========
        auto root = DemoKit::CreatePage("ImagePerfTestMain", "Image Performance Test",
                "Choose a bitmap image, then run the benchmark to compare how each codec "
                "handles it: file size, encode time, and decode time are shown as three "
                "bars per format. Click any thumbnail below the chart to view the "
                "re-encoded result at 1:1.");
        // The chart takes the height the toolbar and status leave: the page is
        // the display area's height, not its content's.
        root->SetFillsArea(true);

        auto card = DemoKit::CreateCard("PerfCard");
        DemoKit::Add(root, card, 1);

        // ========== TOOLBAR ==========
        // A row that wraps: longer texts put the image info on a line of its own.
        auto toolbar = DemoKit::CreateRow("PerfToolbar", 8);
        toolbar->SetBackgroundColor(Color(245, 248, 252, 255));
        toolbar->SetBorders(1.0f, Color(200, 210, 220, 255));
        toolbar->SetPadding(6, 6, 6, 6);
        toolbar->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);

        // As wide as its text, at least the 160 px it had.
        state->chooseBtn = std::make_shared<UltraCanvasButton>("PerfChooseBtn", "Choose Image");
        {
            CSSLayout::BoxConstraints limits;
            limits.minWidth = CSSLayout::Dimension::Px(160);
            limits.minHeight = CSSLayout::Dimension::Px(30);
            state->chooseBtn->boxConstraints = limits;
        }

        state->infoLabel = DemoKit::CreateText("PerfImageInfo", "No image loaded");
        state->infoLabel->SetFontSize(11);
        state->infoLabel->SetTextColor(Color(60, 60, 60, 255));

        // The gear at the right end: which formats the benchmark times.
        state->settingsBtn = std::make_shared<UltraCanvasButton>("PerfSettingsBtn", 0, 0, 32, 30, "");
        {
            ButtonStyle style = state->settingsBtn->GetStyle();
            style.normalColor = Colors::Transparent;
            style.hoverColor = Color(225, 232, 240, 255);
            style.pressedColor = Color(205, 215, 228, 255);
            style.borderColor = Colors::Transparent;
            style.cornerRadius = 6;
            style.useIconAsMask = true;
            // A mask icon takes the text colour.
            style.normalTextColor = Color(70, 80, 95, 255);
            style.hoverTextColor = Color(20, 30, 45, 255);
            style.pressedTextColor = Color(20, 30, 45, 255);
            state->settingsBtn->SetStyle(style);
        }
        state->settingsBtn->SetIcon(NormalizePath(GetResourcesDir() + "media/icons/settings.svg"));
        state->settingsBtn->SetIconSize(18, 18);
        state->settingsBtn->SetTooltip("Settings: the formats the benchmark encodes and decodes");

        DemoKit::Add(toolbar, state->chooseBtn);
        toolbar->AddSpacer(8);
        DemoKit::Add(toolbar, state->infoLabel, 1);   // stretches
        DemoKit::AddFixed(toolbar, state->settingsBtn);

        DemoKit::Add(card, toolbar);

        // ========== CHART AREA (fills remaining vertical space) ==========
        auto chartSection = std::make_shared<UltraCanvasContainer>("PerfChartSection");
        chartSection->SetBackgroundColor(Color(255, 255, 255, 255));

        chartSection->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        chartSection->layout.SetFlexGap(4);

        state->chartElem = std::make_shared<CodecComparisonChartElement>(
                "PerfChartElem");
        state->chartElem->SetCodecNames(state->codecNames);
        state->chartElem->SetThumbnailImages(state->thumbs);
        state->chartElem->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        chartSection->AddChild(state->chartElem);

        // Thumbnails row — use AddChild without layout manager.
        state->thumbsRow = std::make_shared<UltraCanvasContainer>(
                "PerfThumbsRow", 0, 0, 0, 48);
        state->thumbsRow->SetBackgroundColor(Color(255, 255, 255, 255));
        state->thumbsRow->SetBorders(1.0f, Color(220, 220, 220, 255));

        state->thumbs.reserve(state->codecs.size());
        for (size_t i = 0; i < state->codecs.size(); ++i) {
            auto thumb = std::make_shared<UltraCanvasImageElement>(
                    "PerfThumb_" + std::to_string(i),
                    0, 0, 0, 42);
            thumb->SetFitMode(ImageFitMode::Contain);
            thumb->SetBackgroundColor(Color(245, 245, 245, 255));
            thumb->SetBorders(1.0f, Color(210, 215, 220, 255));
            thumb->SetClickable(true);
            thumb->SetMouseCursor(UCMouseCursor::Hand);
            thumb->SetVisible(false);
            state->thumbs.push_back(thumb);
            state->thumbsRow->AddChild(thumb);
        }

        state->thumbsRow->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        chartSection->AddChild(state->thumbsRow);

        // Grows from nothing: the chart gets what the toolbar and the status
        // line leave of the card.
        DemoKit::Add(card, chartSection, 1);
        chartSection->layoutItem.SetFlexShrink(1).SetFlexBasis(CSSLayout::Dimension::Px(0));

        // ========== STATUS LINE ==========
        state->statusLabel = DemoKit::AddText(card, "PerfStatusLabel",
                "Click Choose Image to pick a bitmap.");
        state->statusLabel->SetFontSize(11);
        state->statusLabel->SetTextColor(Color(60, 60, 60, 255));

        // Position each active codec's thumbnail horizontally centered under
        // its bar group, and hide thumbnails for codecs that are not in the
        // current active set (switched off in the settings).
        auto positionThumbnails = [state]() {
            if (!state->chartElem || !state->thumbsRow) return;

            int innerW = state->chartElem->GetGroupInnerWidth();
            int thumbW = innerW;
            if (thumbW > 48) thumbW = 48;
            if (thumbW < 16) thumbW = 16;
            int thumbH = 40;

            Rect2Di hostBounds = state->thumbsRow->GetBounds();
            int hostX = hostBounds.x;
            int thumbYRel = 4;

            // First, position the thumbs that are in the active set.
            std::vector<bool> isActive(state->thumbs.size(), false);
            const int activeCount = static_cast<int>(state->activeCodecIndices.size());
            for (int slot = 0; slot < activeCount; ++slot) {
                int globalIdx = state->activeCodecIndices[slot];
                if (globalIdx < 0 ||
                    globalIdx >= static_cast<int>(state->thumbs.size())) continue;
                auto& t = state->thumbs[globalIdx];
                if (!t) continue;
                int centerXWin = state->chartElem->GetGroupCenterX(slot);
                int thumbXRel = centerXWin - hostX - thumbW / 2;
                t->SetElementAbsolutePosition(Point2Df(thumbXRel, thumbYRel));
                t->SetElementSize(Size2Df(thumbW, thumbH));
                isActive[globalIdx] = true;
            }
            // Inactive thumbs: keep them hidden so they don't render off-grid.
            for (size_t i = 0; i < state->thumbs.size(); ++i) {
                if (isActive[i]) continue;
                if (state->thumbs[i]) state->thumbs[i]->SetVisible(false);
            }
            state->thumbsRow->RequestRedraw();
        };

        state->chartElem->onGeometryChanged = positionThumbnails;

        // Initial chart configuration matches the active codec set.
        state->chartElem->SetCodecNames(state->codecNames);

        // ========== ACTIONS ==========

        // Helper: clean up any temp files left over from a previous run.
        auto cleanupTempFiles = [state]() {
            for (auto& r : state->results) {
                if (!r.encodedFilePath.empty()) {
                    std::error_code ec;
                    std::filesystem::remove(UltraCanvas::PathFromUtf8(r.encodedFilePath), ec);
                }
            }
            state->results.clear();
        };

        // Helper: hide all thumbnails and detach their click handlers. Called
        // before every benchmark so stale slots from the previous run can't
        // open the viewer pointing at a now-deleted temp file.
        auto resetThumbnails = [state]() {
            for (auto& t : state->thumbs) {
                if (t) {
                    t->SetVisible(false);
                    t->onClick = [](){};
                }
            }
        };

        // ---- runBenchmark ----
        // Encodes + decodes every active codec on state->sourceImage and
        // streams results into the chart codec-by-codec. Called from:
        //   * loadSourceImage          — auto-run after [Choose Image]
        //   * the settings dialog      — re-run after the format set changes
        auto runBenchmark = [state, root, cleanupTempFiles, resetThumbnails]() {
            if (state->benchmarkRunning) return;   // re-entry guard
            if (!state->sourceImage || !state->sourceImage->IsValid()) {
                state->statusLabel->SetText("Status: No image loaded.");
                return;
            }

            if (state->activeCodecIndices.empty()) {
                state->statusLabel->SetText(
                        "Status: No format is switched on - choose some in the settings (gear).");
                return;
            }

            state->benchmarkRunning = true;

            // Regenerate runStamp per benchmark. Without this, every run
            // produces identical temp file paths (e.g. ".../bench_<x>_0.png")
            // and UltraCanvasImageElement::LoadFromFile() would short-circuit
            // through UCImage's path-keyed cache and display the previous
            // run's pixels in the thumbnail — even though the new file on
            // disk is correct. Each run gets its own nanosecond stamp so
            // every temp path is unique and no cache entry survives.
            {
                auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
                std::ostringstream s;
                s << "ultracanvas_bench_" << stamp;
                state->runStamp = s.str();
            }

            cleanupTempFiles();
            const int activeCount = static_cast<int>(state->activeCodecIndices.size());
            state->results.reserve(activeCount);

            resetThumbnails();

            state->chooseBtn->SetDisabled(true);
            state->settingsBtn->SetDisabled(true);
            state->statusLabel->SetText("Status: Benchmark running...");

            auto win = root->GetWindow();
            auto ctx = root->GetRenderContext();

            // Process each active codec synchronously, refreshing UI per codec.
            for (int slot = 0; slot < activeCount; ++slot) {
                int globalIdx = state->activeCodecIndices[slot];
                const CodecEntry& entry = state->codecs[globalIdx];

                CodecResult r;
                r.name      = entry.name;
                r.extension = entry.extension;
                r.format    = entry.format;

                std::string outPath = NormalizePath(TempDirBase() + "/" + state->runStamp
                                      + "_" + std::to_string(globalIdx) + "."
                                      + entry.extension);

                // -------- ENCODE --------
                double t0 = 0.0, t1 = 0.0;

                // Through the image writer, with the options FileLoader
                // gives it for this type: its Save settings, as any
                // application saves it.
                {
                    UCImageSave::ImageExportOptions opts;
                    if (!UltraCanvasFileLoader::GetImageExportOptions(outPath, opts)) opts.format = entry.format;
                    opts.preserveMetadata = false;
                    try {
                        t0 = NowMs();
                        std::string err = state->sourceImage->Save(outPath, opts);
                        t1 = NowMs();
                        if (!err.empty()) {
                            r.succeeded = false;
                            r.errorMessage = err;
                        } else {
                            r.encodeTimeMs = t1 - t0;
                            r.encodedFilePath = outPath;
                        }
                    } catch (const std::exception& ex) {
                        r.succeeded = false;
                        r.errorMessage = ex.what();
                    } catch (...) {
                        r.succeeded = false;
                        r.errorMessage = "unknown exception during encode";
                    }
                }

                // -------- FILE SIZE + DECODE --------
                if (r.errorMessage.empty()) {
                    std::error_code ec;
                    r.encodedSizeBytes = static_cast<size_t>(
                            std::filesystem::file_size(UltraCanvas::PathFromUtf8(outPath), ec));
                    if (ec) r.encodedSizeBytes = 0;

                    double d0 = 0.0, d1 = 0.0;
                    bool decodeOk = false;
                    try {
                        // IMPORTANT: libvips uses lazy evaluation. A bare
                        // UCImage::Load() often only opens the file and parses
                        // the header — actual pixel decompression is deferred
                        // until someone reads the pixels. To time the real
                        // decode, force pixel materialization via
                        // VImage::copy_memory(), which walks the full pipeline
                        // and writes every decoded pixel into a contiguous
                        // RAM buffer.
                        d0 = NowMs();
                        auto round = UCImage::Load(outPath, false);
                        if (round && round->IsValid()) {
#ifdef HAS_LIBVIPS
                            vips::VImage v = round->GetVImage();
                            v = v.copy_memory();
                            (void)v;
#endif
                            decodeOk = true;
                        }
                        d1 = NowMs();
                    } catch (...) {
                        decodeOk = false;
                    }
                    if (decodeOk) {
                        r.decodeTimeMs = d1 - d0;
                        r.succeeded = true;
                    } else {
                        r.errorMessage = "decode failed";
                        r.succeeded = false;
                    }
                }

                state->results.push_back(r);

                // Update thumbnail for this codec's global slot (so the thumb
                // physically lives at index globalIdx in state->thumbs even
                // though the chart group is at slot index `slot`).
                if (r.succeeded && globalIdx < static_cast<int>(state->thumbs.size())) {
                    auto thumb = state->thumbs[globalIdx];
                    if (thumb) {
                        thumb->LoadFromFile(outPath);
                        thumb->SetVisible(true);
                        std::string slotPath = outPath;
                        thumb->onClick = [slotPath]() {
                            ShowInMediaViewer(slotPath);
                        };
                        thumb->RequestRedraw();
                    }
                }

                state->chartElem->SetResults(state->results);

                // Live status update
                std::ostringstream s;
                s << "Status: " << r.name
                  << (r.succeeded ? " OK" : " FAILED") << "  ("
                  << (slot + 1) << "/" << activeCount << ")";
                if (!r.succeeded && !r.errorMessage.empty()) {
                    s << " - " << r.errorMessage;
                }
                state->statusLabel->SetText(s.str());

                // Force immediate repaint so the bar chart grows codec-by-codec
                if (win && ctx) {
                    win->UpdateAndRender();
                }
            }

            // Final summary
            int okCount = 0;
            for (const auto& rr : state->results) if (rr.succeeded) ++okCount;
            std::ostringstream finalMsg;
            finalMsg << "Status: Benchmark complete (" << okCount << "/"
                     << activeCount << " codecs succeeded)";
            state->statusLabel->SetText(finalMsg.str());

            state->chooseBtn->SetDisabled(false);
            state->settingsBtn->SetDisabled(false);
            state->benchmarkRunning = false;

            if (win && ctx) {
                win->UpdateAndRender();
            }

            debugOutput << "Image Performance Test complete: "
                      << okCount << "/" << activeCount
                      << " codecs succeeded" << std::endl;
        };

        // ---- loadSourceImage ----
        // Loads an image from disk, refreshes the toolbar info label, and
        // auto-fires the benchmark on success.
        auto loadSourceImage = [state, cleanupTempFiles, resetThumbnails,
                                runBenchmark](const std::string& path) -> bool {
            auto img = UCImage::Load(path, false);
            if (!img || !img->IsValid()) {
                state->statusLabel->SetText("Status: Failed to load " + path);
                state->infoLabel->SetText("No image loaded");
                state->infoLabel->RequestRedraw();
                return false;
            }
            state->sourceImage  = img;
            state->sourcePath   = path;
            state->sourceWidth  = img->GetWidth();
            state->sourceHeight = img->GetHeight();

            cleanupTempFiles();
            state->chartElem->SetResults(state->results);
            resetThumbnails();

            // Refresh toolbar info label. RequestRedraw() is required because
            // SetText() updates the label's internal string but does not, on
            // its own, mark the label dirty for the next paint pass — without
            // it, the new filename only appears the next time something else
            // triggers a repaint of the toolbar.
            std::string fileName = PathToUtf8(PathFromUtf8(path).filename());
            std::ostringstream info;
            info << fileName << "  -  "
                 << state->sourceWidth << " x " << state->sourceHeight << " px";
            state->infoLabel->SetText(info.str());
            state->infoLabel->RequestRedraw();

            // Auto-run: kick the benchmark immediately after a successful load.
            runBenchmark();
            return true;
        };

        // [Choose Image] click handler — opens dialog, loads, auto-runs.
        // Every picture type this build reads - FileLoader's list.
        state->chooseBtn->SetOnClick([loadSourceImage, root]() {
            FileDialogOptions opts;
            opts.SetTitle("Choose Image for Benchmark")
                .SetInitialDirectory(InitialImageDir())
                .SetParentWindow(root->GetWindow());
            UltraCanvasFileLoader::AddLoadTypes(opts, "Bitmap Images", {MediaFormatCategory::Bitmap});
            opts.AddFilter("All Files", "*");
            UltraCanvasFileLoader::OpenFileDialog(opts,
                [loadSourceImage](DialogResult result, const std::string& path) {
                    if (result != DialogResult::OK || path.empty()) return;   // user cancelled
                    loadSourceImage(path);
                });
        });

        // The gear: a dialog with a switch per format this build can time.
        // Apply rebuilds the active set, re-lays the chart and the
        // thumbnails, and re-runs the benchmark when an image is loaded.
        state->settingsBtn->SetOnClick(
                [state, root = root.get(), rebuildActiveCodecs, runBenchmark, positionThumbnails]() {
            if (state->benchmarkRunning) return;

            DialogConfig config;
            config.title = "Image Performance Test - Formats";
            config.message = "Switch on the formats the benchmark encodes and decodes. "
                             "The list holds every bitmap format this build both writes "
                             "and reads (FileLoader's formats); each is written with its "
                             "Save settings.";
            config.dialogType = DialogType::Information;
            config.buttons = DialogButtons::NoButtons;
            config.width = 760;
            config.height = 420;
            auto dialog = UltraCanvasDialogManager::CreateDialog(config);
            dialog->SetIconVisible(false);

            // Two columns of switches, filled top to bottom.
            constexpr float kRowHeight = 30.0f;
            const int count = static_cast<int>(state->codecs.size());
            const int rows = (count + 1) / 2;
            auto grid = std::make_shared<UltraCanvasContainer>(
                    "PerfFormatGrid", 0, 0, 720, rows * kRowHeight);
            grid->layout.SetFlexRow().SetFlexGap(16);
            std::shared_ptr<UltraCanvasContainer> columns[2];
            for (int c = 0; c < 2; ++c) {
                columns[c] = std::make_shared<UltraCanvasContainer>(
                        "PerfFormatColumn" + std::to_string(c), 0, 0, 350, rows * kRowHeight);
                columns[c]->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Start);
                columns[c]->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
                grid->AddChild(columns[c]);
            }
            std::vector<std::pair<std::string, UltraCanvasSwitch*>> switches;
            for (int i = 0; i < count; ++i) {
                const CodecEntry& e = state->codecs[i];
                std::string label = e.name;
                if (!e.description.empty() && e.description != e.name) label += " - " + e.description;
                auto sw = UltraCanvasSwitch::Create("PerfFormat_" + e.extension, 0, 0,
                                                    label, IsCodecEnabled(e));
                sw->layoutItem.SetFlexShrink(0);
                columns[i < rows ? 0 : 1]->AddChild(sw);
                switches.emplace_back(e.extension, sw.get());
            }
            dialog->AddDialogElement(grid);

            dialog->AddCustomButton("Cancel", DialogResult::Cancel, DialogButtonRole::Cancel);
            dialog->AddCustomButton("Apply", DialogResult::OK, DialogButtonRole::Default);

            UltraCanvasDialogManager::ShowDialog(dialog,
                [state, switches, rebuildActiveCodecs, runBenchmark, positionThumbnails](DialogResult result) {
                    if (result != DialogResult::OK || state->benchmarkRunning) return;
                    std::vector<std::pair<std::string, bool>> choice;
                    for (const auto& [ext, sw] : switches) choice.emplace_back(ext, sw->IsChecked());
                    StoreFormatChoice(choice);
                    rebuildActiveCodecs();
                    state->chartElem->SetCodecNames(state->codecNames);
                    // Reposition thumbnails under the (possibly new) group columns.
                    positionThumbnails();
                    if (state->sourceImage && state->sourceImage->IsValid()) {
                        runBenchmark();
                    } else {
                        state->chartElem->SetResults({});
                    }
                },
                root->GetWindow());
        });

        // Opening the page asks for an image. Posted, so that it opens once
        // the page is in the window, and not for a page already left again;
        // the layout audit never runs posted tasks, so it opens no popup.
        if (auto* app = UltraCanvasApplication::GetInstance()) {
            std::weak_ptr<PanelState> weakState = state;
            app->PostToUIThread([weakState]() {
                auto st = weakState.lock();
                if (!st || !st->chooseBtn || st->sourceImage) return;
                UltraCanvasWindowBase* window = st->chooseBtn->GetWindow();
                if (!window) return;   // the page was replaced before this ran
                UltraCanvasDialogManager::ShowInformation(
                        "Upload an image to start the test.\n\n"
                        "Click Choose Image and pick a bitmap: every format switched on "
                        "in the settings (gear) is then encoded and decoded.",
                        "Image Performance Test", nullptr, window);
            });
        }

        return root;
    }

} // namespace UltraCanvas