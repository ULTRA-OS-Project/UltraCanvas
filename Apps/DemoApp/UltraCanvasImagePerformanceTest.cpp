// Apps/DemoApp/UltraCanvasImagePerformanceTest.cpp
// Bitmap codec comparison benchmark: file size / encode time / decode time per format
// Version: 2.5.8
// Last Modified: 2026-06-01
// Author: UltraCanvas Framework
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
// TGA is not benchmarked: neither UCImageSaveFormat::TGA nor a bundled TGA
// encoder is available, and adding either would require core changes.
// ================================================================================

#include "UltraCanvasContainer.h"
#include "UltraCanvasSpacer.h"
#include "CSSLayout/CSSLayout.h"
#include "UltraCanvasDemo.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasNativeDialogs.h"
#include "UltraCanvasModalDialog.h"   // for FileFilter
#include "UltraCanvasConfig.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasDebug.h"

// QOI third-party header. IMPORTANT: Do NOT define QOI_IMPLEMENTATION here;
// it is already defined in the standalone libspecific/Cairo/qoi.cpp, which
// provides the qoi_encode / qoi_decode symbols for the whole program via the
// linker.
#include "../../UltraCanvas/libspecific/Cairo/qoi.h"

#ifdef HAS_LIBVIPS
#include <vips/vips8>
#endif

#include <chrono>
#include <sstream>
#include <iomanip>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>

namespace UltraCanvas {

// ============================================================================
// EXTERNAL HELPER (defined in UltraCanvasBitmapFormatDemo.cpp)
// Opens a full-size 1:1 image viewer window with zoom/pan support.
// ============================================================================
    void ShowFullSizeImageViewer(const std::string& imagePath);

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
            const char* tmp = std::getenv("TEMP");
            if (!tmp) tmp = std::getenv("TMP");
            if (!tmp) tmp = "C:\\Temp";
            return std::string(tmp);
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

        // ============ CODEC TABLE ============
        // Unified table of every codec the benchmark knows how to drive.
        // Order here is the X-axis display order on the chart.
        //
        // EncoderBackend selects which encode/decode path runBenchmark uses:
        //   Libvips → UCImage::Save / UCImage::Load (most codecs)
        //   QOI     → bundled qoi.h (qoi_write / qoi_read)
        //   BMP     → bundled BMP encoder/decoder (this file)
        //
        // isOptional codecs are excluded from the active set unless explicitly
        // enabled by the user (see [x] Include BMP checkbox in the toolbar).
        // BMP is optional because uncompressed RGB ≈ width*height*3 bytes,
        // which dominates the size axis and crushes the chart's scale.
        enum class EncoderBackend { Libvips, QOI, BMP };

        struct CodecEntry {
            UCImageSaveFormat format;     // ignored when backend != Libvips
            const char* name;
            const char* extension;
            EncoderBackend backend;
            bool isOptional;
        };

        // X-axis display order: lossless cluster first (PNG, QOI), then JPG
        // and its successor JP2K, then modern lossy/animated/legacy codecs.
        // BMP is appended last so it sits at the right edge when enabled.
        const CodecEntry kAllCodecs[] = {
                { UCImageSaveFormat::PNG,      "PNG",  "png",  EncoderBackend::Libvips, false },
                { UCImageSaveFormat::PNG,      "QOI",  "qoi",  EncoderBackend::QOI,     false },
                { UCImageSaveFormat::JPEG,     "JPG",  "jpg",  EncoderBackend::Libvips, false },
                { UCImageSaveFormat::JPEG2000, "JP2K", "jp2",  EncoderBackend::Libvips, false },
                { UCImageSaveFormat::WEBP,     "WebP", "webp", EncoderBackend::Libvips, false },
                { UCImageSaveFormat::AVIF,     "AVIF", "avif", EncoderBackend::Libvips, false },
                { UCImageSaveFormat::HEIF,     "HEIF", "heif", EncoderBackend::Libvips, false },
                { UCImageSaveFormat::GIF,      "GIF",  "gif",  EncoderBackend::Libvips, false },
                { UCImageSaveFormat::TIFF,     "TIFF", "tiff", EncoderBackend::Libvips, false },
                //{ UCImageSaveFormat::JXL,      "JXL",  "jxl",  EncoderBackend::Libvips, false },
                { UCImageSaveFormat::BMP,      "BMP",  "bmp",  EncoderBackend::BMP,     true  },
        };
        constexpr int kAllCodecCount =
                sizeof(kAllCodecs) / sizeof(kAllCodecs[0]);

        // ============ QOI ENCODE/DECODE VIA BUNDLED qoi.h ============
        // Encodes a libvips VImage as QOI using the bundled reference encoder.
        // Returns "" on success or an error message.
#ifdef HAS_LIBVIPS
        std::string EncodeQOIFromVImage(vips::VImage vImg, const std::string& outPath) {
            try {
                // QOI supports 3 (RGB) or 4 (RGBA) channels, 8-bit unsigned.
                // Normalize: sRGB colourspace, 8-bit unsigned, exactly 3 or 4 bands.
                if (vImg.interpretation() != VIPS_INTERPRETATION_sRGB) {
                    vImg = vImg.colourspace(VIPS_INTERPRETATION_sRGB);
                }
                int bands = vImg.bands();
                if (bands == 1) {
                    // Grayscale -> RGB
                    vImg = vImg.bandjoin({ vImg, vImg });
                    bands = 3;
                } else if (bands == 2) {
                    // Gray + alpha -> RGBA (duplicate gray into R, G, B, keep alpha)
                    vips::VImage gray  = vImg.extract_band(0);
                    vips::VImage alpha = vImg.extract_band(1);
                    vImg = gray.bandjoin({ gray, gray, alpha });
                    bands = 4;
                } else if (bands > 4) {
                    vImg = vImg.extract_band(0, vips::VImage::option()->set("n", 4));
                    bands = 4;
                }
                // bands is now 3 or 4. Cast to uchar AFTER band manipulation so
                // any intermediate float pipelines are fully reduced.
                if (vImg.format() != VIPS_FORMAT_UCHAR) {
                    vImg = vImg.cast(VIPS_FORMAT_UCHAR);
                }
                // Force materialization so vImg.data() returns a contiguous buffer.
                vImg = vImg.copy_memory();

                qoi_desc desc;
                desc.width      = static_cast<unsigned int>(vImg.width());
                desc.height     = static_cast<unsigned int>(vImg.height());
                desc.channels   = static_cast<unsigned char>(bands);
                desc.colorspace = QOI_SRGB;

                // qoi_write returns bytes written, or 0 on failure.
                int written = qoi_write(outPath.c_str(), vImg.data(), &desc);
                if (written <= 0) {
                    return "qoi_write failed";
                }
                return "";
            } catch (vips::VError& err) {
                return std::string("vips error preparing QOI data: ") + err.what();
            } catch (std::exception& ex) {
                return std::string("QOI encode exception: ") + ex.what();
            }
        }
#else
        std::string EncodeQOIFromVImage(vips::VImage, const std::string&) {
            return "QOI encoding requires libvips";
        }
#endif

        // Decodes a QOI file fully (allocates pixel buffer and frees it).
        // Returns true on success. Used purely to measure decode time.
        bool DecodeQOIFile(const std::string& path) {
            qoi_desc desc;
            void* pixels = qoi_read(path.c_str(), &desc, 0);
            if (!pixels) return false;
            std::free(pixels);
            return true;
        }

        // ============ BMP ENCODE/DECODE (BUNDLED, NO EXTERNAL DEPS) ============
        //
        // libvips has no BMP writer at all (it can read BMP via magickload only),
        // so to include BMP in the benchmark we ship a small standalone encoder
        // and decoder for uncompressed 24-bit RGB BMP — the simplest, most
        // universally readable BMP variant.
        //
        // BMP layout: 14-byte BITMAPFILEHEADER + 40-byte BITMAPINFOHEADER
        // followed by pixel rows in BGR order, bottom-up, with each row padded
        // to a 4-byte boundary. Files are written little-endian as required by
        // the BMP spec.
        //
        // Helpers for little-endian writing — the BMP spec is strictly LE
        // regardless of host endianness, so we serialise byte-by-byte rather
        // than rely on std::fwrite of a packed struct.
        inline void WriteLE16(unsigned char* p, uint16_t v) {
            p[0] = static_cast<unsigned char>(v & 0xFF);
            p[1] = static_cast<unsigned char>((v >> 8) & 0xFF);
        }
        inline void WriteLE32(unsigned char* p, uint32_t v) {
            p[0] = static_cast<unsigned char>(v & 0xFF);
            p[1] = static_cast<unsigned char>((v >> 8) & 0xFF);
            p[2] = static_cast<unsigned char>((v >> 16) & 0xFF);
            p[3] = static_cast<unsigned char>((v >> 24) & 0xFF);
        }
        inline uint16_t ReadLE16(const unsigned char* p) {
            return static_cast<uint16_t>(p[0]) |
                   (static_cast<uint16_t>(p[1]) << 8);
        }
        inline uint32_t ReadLE32(const unsigned char* p) {
            return  static_cast<uint32_t>(p[0])        |
                   (static_cast<uint32_t>(p[1]) << 8)  |
                   (static_cast<uint32_t>(p[2]) << 16) |
                   (static_cast<uint32_t>(p[3]) << 24);
        }

#ifdef HAS_LIBVIPS
        // Encodes a libvips VImage as 24bpp BI_RGB BMP. Returns "" on success
        // or a human-readable error message on failure.
        std::string EncodeBMPFromVImage(vips::VImage vImg, const std::string& outPath) {
            try {
                // Normalise to sRGB 8-bit, drop alpha. BI_RGB BMP carries no
                // alpha channel; if the source has one we discard it (a 32bpp
                // BMP with BI_BITFIELDS is technically possible but is poorly
                // supported by older readers — sticking with the universal
                // 24bpp form).
                if (vImg.interpretation() != VIPS_INTERPRETATION_sRGB) {
                    vImg = vImg.colourspace(VIPS_INTERPRETATION_sRGB);
                }
                int bands = vImg.bands();
                if (bands == 1) {
                    vImg = vImg.bandjoin({ vImg, vImg });
                    bands = 3;
                } else if (bands == 2) {
                    // gray + alpha → drop alpha, expand gray to RGB
                    vips::VImage gray = vImg.extract_band(0);
                    vImg = gray.bandjoin({ gray, gray });
                    bands = 3;
                } else if (bands >= 4) {
                    vImg = vImg.extract_band(0, vips::VImage::option()->set("n", 3));
                    bands = 3;
                }
                if (vImg.format() != VIPS_FORMAT_UCHAR) {
                    vImg = vImg.cast(VIPS_FORMAT_UCHAR);
                }
                vImg = vImg.copy_memory();   // ensure contiguous buffer

                const int width  = vImg.width();
                const int height = vImg.height();
                if (width <= 0 || height <= 0) {
                    return "BMP encode: invalid image dimensions";
                }

                // Row-stride math: 24bpp → 3 bytes/px, padded to 4-byte boundary
                const size_t rowBytes  = static_cast<size_t>(width) * 3;
                const size_t rowStride = (rowBytes + 3u) & ~size_t(3);
                const size_t pixelDataSize = rowStride * static_cast<size_t>(height);
                const uint32_t fileSize = static_cast<uint32_t>(54 + pixelDataSize);

                // ---- BITMAPFILEHEADER (14 bytes) ----
                unsigned char fileHdr[14] = {0};
                fileHdr[0] = 'B';
                fileHdr[1] = 'M';
                WriteLE32(fileHdr + 2,  fileSize);     // total file size
                WriteLE16(fileHdr + 6,  0);             // reserved
                WriteLE16(fileHdr + 8,  0);             // reserved
                WriteLE32(fileHdr + 10, 54);            // pixel-data offset

                // ---- BITMAPINFOHEADER (40 bytes) ----
                unsigned char infoHdr[40] = {0};
                WriteLE32(infoHdr + 0,  40);                            // header size
                WriteLE32(infoHdr + 4,  static_cast<uint32_t>(width));  // width
                WriteLE32(infoHdr + 8,  static_cast<uint32_t>(height)); // height (positive ⇒ bottom-up)
                WriteLE16(infoHdr + 12, 1);                             // planes
                WriteLE16(infoHdr + 14, 24);                            // bits per pixel
                WriteLE32(infoHdr + 16, 0);                             // BI_RGB (no compression)
                WriteLE32(infoHdr + 20, static_cast<uint32_t>(pixelDataSize));
                WriteLE32(infoHdr + 24, 2835);   // X pixels per meter (~72 DPI)
                WriteLE32(infoHdr + 28, 2835);   // Y pixels per meter
                WriteLE32(infoHdr + 32, 0);      // colours used
                WriteLE32(infoHdr + 36, 0);      // important colours

                // ---- WRITE FILE ----
                FILE* fp = std::fopen(outPath.c_str(), "wb");
                if (!fp) return "BMP encode: failed to open output file";

                if (std::fwrite(fileHdr, 1, 14, fp) != 14 ||
                    std::fwrite(infoHdr, 1, 40, fp) != 40) {
                    std::fclose(fp);
                    return "BMP encode: header write failed";
                }

                // Pixel rows: bottom-up, RGB→BGR, padded to 4-byte stride
                const unsigned char* src = static_cast<const unsigned char*>(vImg.data());
                std::vector<unsigned char> rowBuf(rowStride, 0);
                for (int y = height - 1; y >= 0; --y) {
                    const unsigned char* srcRow = src + static_cast<size_t>(y) * rowBytes;
                    for (int x = 0; x < width; ++x) {
                        rowBuf[x * 3 + 0] = srcRow[x * 3 + 2]; // B
                        rowBuf[x * 3 + 1] = srcRow[x * 3 + 1]; // G
                        rowBuf[x * 3 + 2] = srcRow[x * 3 + 0]; // R
                    }
                    // Padding bytes (already zero from initialisation)
                    if (std::fwrite(rowBuf.data(), 1, rowStride, fp) != rowStride) {
                        std::fclose(fp);
                        return "BMP encode: pixel row write failed";
                    }
                }

                std::fclose(fp);
                return "";
            } catch (vips::VError& err) {
                return std::string("vips error preparing BMP data: ") + err.what();
            } catch (std::exception& ex) {
                return std::string("BMP encode exception: ") + ex.what();
            }
        }
#else
        std::string EncodeBMPFromVImage(vips::VImage, const std::string&) {
            return "BMP encoding requires libvips (for source pixel access)";
        }
#endif

        // Decodes a 24bpp BI_RGB BMP file fully — reads headers, allocates the
        // pixel buffer, walks every row (with stride padding) into a contiguous
        // RGB buffer, then frees it. Returns true on success. Used purely to
        // measure decode time symmetrically with the other codecs.
        //
        // Only handles the variant we write (24bpp, BI_RGB, bottom-up). Other
        // BMP flavours (1/4/8/16bpp, RLE, top-down, BITFIELDS, embedded JPEG)
        // are out of scope — the encoder never produces them, and supporting
        // arbitrary BMPs would be irrelevant to the timing experiment.
        bool DecodeBMPFile(const std::string& path) {
            FILE* fp = std::fopen(path.c_str(), "rb");
            if (!fp) return false;

            unsigned char fileHdr[14];
            unsigned char infoHdr[40];
            if (std::fread(fileHdr, 1, 14, fp) != 14 ||
                std::fread(infoHdr, 1, 40, fp) != 40) {
                std::fclose(fp);
                return false;
            }

            if (fileHdr[0] != 'B' || fileHdr[1] != 'M') {
                std::fclose(fp);
                return false;
            }

            uint32_t pixelOffset = ReadLE32(fileHdr + 10);
            uint32_t headerSize  = ReadLE32(infoHdr + 0);
            int32_t  width       = static_cast<int32_t>(ReadLE32(infoHdr + 4));
            int32_t  heightField = static_cast<int32_t>(ReadLE32(infoHdr + 8));
            uint16_t bitsPerPx   = ReadLE16(infoHdr + 14);
            uint32_t compression = ReadLE32(infoHdr + 16);

            if (headerSize < 40 || width <= 0 || heightField == 0 ||
                bitsPerPx != 24 || compression != 0) {
                std::fclose(fp);
                return false;
            }
            const bool bottomUp = (heightField > 0);
            const int  height   = std::abs(heightField);

            // Skip any extra header bytes / colour table to reach pixel data
            if (std::fseek(fp, static_cast<long>(pixelOffset), SEEK_SET) != 0) {
                std::fclose(fp);
                return false;
            }

            const size_t rowBytes  = static_cast<size_t>(width) * 3;
            const size_t rowStride = (rowBytes + 3u) & ~size_t(3);
            std::vector<unsigned char> rowBuf(rowStride);
            std::vector<unsigned char> pixels(static_cast<size_t>(width) *
                                              static_cast<size_t>(height) * 3);

            for (int row = 0; row < height; ++row) {
                if (std::fread(rowBuf.data(), 1, rowStride, fp) != rowStride) {
                    std::fclose(fp);
                    return false;
                }
                int destRow = bottomUp ? (height - 1 - row) : row;
                unsigned char* dst = pixels.data() +
                                     static_cast<size_t>(destRow) * rowBytes;
                for (int x = 0; x < width; ++x) {
                    dst[x * 3 + 0] = rowBuf[x * 3 + 2]; // R
                    dst[x * 3 + 1] = rowBuf[x * 3 + 1]; // G
                    dst[x * 3 + 2] = rowBuf[x * 3 + 0]; // B
                }
            }

            std::fclose(fp);
            // pixels buffer freed here when vector goes out of scope —
            // mirrors qoi_read + free pattern for symmetric timing.
            return true;
        }

        // ============ DEFAULT EXPORT OPTIONS BUILDER ============
        UCImageSave::ImageExportOptions DefaultOptionsFor(UCImageSaveFormat f) {
            UCImageSave::ImageExportOptions opt;
            opt.format = f;
            opt.preserveMetadata = false;
            opt.preserveTransparency = true;
            // Struct defaults are already sensible (quality 85 / 80 / 65 / 50 / ...).
            return opt;
        }

        std::string InitialImageDir() {
            std::string dir = GetResourcesDir() + "media/images";
            std::error_code ec;
            if (std::filesystem::exists(dir, ec)) return dir;
            std::string alt = GetResourcesDir() + "media";
            if (std::filesystem::exists(alt, ec)) return alt;
            return std::filesystem::current_path(ec).string();
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
//   main VBox
//     toolbar     (HBox: Choose | Run | Info | stretch)
//     chart+thumbs VBox (stretch)
//         CodecComparisonChartElement
//         thumbsRow (HBox: 11 thumbnail ImageElements)
//     status      (HBox: status label)
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

            // UI refs
            std::shared_ptr<UltraCanvasButton>   chooseBtn;
            std::shared_ptr<UltraCanvasCheckbox> includeBmpCheckbox;
            std::shared_ptr<UltraCanvasLabel>    infoLabel;
            std::shared_ptr<UltraCanvasLabel>    statusLabel;
            std::shared_ptr<CodecComparisonChartElement> chartElem;
            std::shared_ptr<UltraCanvasContainer> thumbsRow;
            std::vector<std::shared_ptr<UltraCanvasImageElement>> thumbs;

            // Codec slot names in display order. Built dynamically based on
            // which optional codecs are currently enabled (e.g. BMP checkbox).
            std::vector<std::string> codecNames;

            // Indices into kAllCodecs[] for the codecs that are currently
            // active. Length matches codecNames.size() and chart group count.
            std::vector<int> activeCodecIndices;

            // True while a benchmark run is in flight — used to suppress the
            // checkbox-toggle handler from re-entering Run mid-run.
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

        // Helper to rebuild the active codec set based on optional-codec flags.
        // Mutates state->codecNames and state->activeCodecIndices in lockstep.
        // includeOptional[i] determines whether kAllCodecs[i] is active when
        // kAllCodecs[i].isOptional == true; mandatory codecs are always active.
        auto rebuildActiveCodecs = [state](bool includeBmp) {
            state->codecNames.clear();
            state->activeCodecIndices.clear();
            state->codecNames.reserve(kAllCodecCount);
            state->activeCodecIndices.reserve(kAllCodecCount);
            for (int i = 0; i < kAllCodecCount; ++i) {
                const CodecEntry& e = kAllCodecs[i];
                if (e.isOptional) {
                    // Currently only BMP is optional. Extend here if more
                    // optional codecs are added.
                    if (std::string(e.name) == "BMP" && !includeBmp) continue;
                }
                state->codecNames.push_back(e.name);
                state->activeCodecIndices.push_back(i);
            }
        };

        // Initial codec set: BMP excluded by default (see VERSION 2.3.0 notes).
        rebuildActiveCodecs(/*includeBmp=*/false);

        // ========== ROOT CONTAINER + MAIN VBOX ==========
        auto root = std::make_shared<UltraCanvasContainer>(
                "ImagePerfTestMain", 0, 0, 1000, 810);
        root->SetBackgroundColor(Color(255, 255, 255, 255));
        root->SetPadding(12, 12, 12, 12);

        root->layout.SetFlexColumn();
        root->layout.SetFlexGap(10);

        // ========== TITLE ==========
        auto title = std::make_shared<UltraCanvasLabel>(
                "PerfTestTitle", 0, 0, 0, 32);
        title->SetText("Image Performance Test");
        title->SetFontSize(20);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(40, 80, 120, 255));
        root->AddChild(title); title->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        // ========== DESCRIPTION ==========
        auto desc = std::make_shared<UltraCanvasLabel>(
                "PerfTestDesc", 0, 0, 0, 42);
        desc->SetText(
                "Choose a bitmap image, then run the benchmark to compare how each codec "
                "handles it: file size, encode time, and decode time are shown as three "
                "bars per format. Click any thumbnail below the chart to view the "
                "re-encoded result at 1:1.");
        desc->SetFontSize(11);
        desc->SetWrap(TextWrap::WrapWord);
        desc->SetTextColor(Color(80, 80, 80, 255));
        root->AddChild(desc); desc->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        // ========== TOOLBAR ==========
        auto toolbar = std::make_shared<UltraCanvasContainer>(
                "PerfToolbar", 0, 0, 0, 44);
        toolbar->SetBackgroundColor(Color(245, 248, 252, 255));
        toolbar->SetBorders(1.0f, Color(200, 210, 220, 255));
        toolbar->SetPadding(6, 6, 6, 6);

        toolbar->layout.SetFlexRow();
        toolbar->layout.SetFlexGap(8);
        toolbar->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);

        state->chooseBtn = std::make_shared<UltraCanvasButton>(
                "PerfChooseBtn", 0, 0, 160, 30);
        state->chooseBtn->SetText("Choose Image...");

        // [x] Include BMP — opt-in for the BMP codec, which would otherwise
        // dominate the size axis. Toggling between runs re-runs automatically
        // (see handler installed below loadSourceImage / runBenchmark).
        state->includeBmpCheckbox = std::make_shared<UltraCanvasCheckbox>(
                "PerfIncludeBmp", 0, 0, 130, 28);
        state->includeBmpCheckbox->SetText("Include BMP");
        state->includeBmpCheckbox->SetChecked(false);

        state->infoLabel = std::make_shared<UltraCanvasLabel>(
                "PerfImageInfo", 0, 0, 0, 28);
        state->infoLabel->SetText("No image loaded");
        state->infoLabel->SetFontSize(11);
        state->infoLabel->SetTextColor(Color(60, 60, 60, 255));

        toolbar->AddChild(state->chooseBtn);
        toolbar->AddSpacer(8);
        toolbar->AddChild(state->includeBmpCheckbox);
        toolbar->AddSpacer(8);
        toolbar->AddChild(state->infoLabel); state->infoLabel->layoutItem.SetFlexGrow(1);   // stretches

        root->AddChild(toolbar); toolbar->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        // ========== CHART AREA (fills remaining vertical space) ==========
        auto chartSection = std::make_shared<UltraCanvasContainer>(
                "PerfChartSection", 0, 0, 0, 420);
        chartSection->SetBackgroundColor(Color(255, 255, 255, 255));

        chartSection->layout.SetFlexColumn();
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

        state->thumbs.reserve(kAllCodecCount);
        for (int i = 0; i < kAllCodecCount; ++i) {
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

        chartSection->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        root->AddChild(chartSection);

        // ========== STATUS ROW ==========
        auto statusRow = std::make_shared<UltraCanvasContainer>(
                "PerfStatus", 0, 0, 0, 24);
        statusRow->SetBackgroundColor(Color(255, 255, 255, 255));
        statusRow->layout.SetFlexRow();

        state->statusLabel = std::make_shared<UltraCanvasLabel>(
                "PerfStatusLabel", 0, 0, 0, 20);
        state->statusLabel->SetText("Click [Choose Image...] to pick a bitmap.");
        state->statusLabel->SetFontSize(11);
        state->statusLabel->SetTextColor(Color(60, 60, 60, 255));
        statusRow->AddChild(state->statusLabel); state->statusLabel->layoutItem.SetFlexGrow(1);

        root->AddChild(statusRow); statusRow->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        // Position each active codec's thumbnail horizontally centered under
        // its bar group, and hide thumbnails for codecs that are not in the
        // current active set (e.g. BMP when the checkbox is off).
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
                    std::filesystem::remove(r.encodedFilePath, ec);
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
        //   * loadSourceImage          — auto-run after [Choose Image...]
        //   * [x] Include BMP toggle   — re-run after the codec set changes
        auto runBenchmark = [state, root, cleanupTempFiles, resetThumbnails]() {
            if (state->benchmarkRunning) return;   // re-entry guard
            if (!state->sourceImage || !state->sourceImage->IsValid()) {
                state->statusLabel->SetText("Status: No image loaded.");
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
            state->includeBmpCheckbox->SetDisabled(true);
            state->statusLabel->SetText("Status: Benchmark running...");

            auto win = root->GetWindow();
            auto ctx = root->GetRenderContext();

            // Process each active codec synchronously, refreshing UI per codec.
            for (int slot = 0; slot < activeCount; ++slot) {
                int globalIdx = state->activeCodecIndices[slot];
                const CodecEntry& entry = kAllCodecs[globalIdx];

                CodecResult r;
                r.name      = entry.name;
                r.extension = entry.extension;
                r.format    = entry.format;

                std::string outPath = NormalizePath(TempDirBase() + "/" + state->runStamp
                                      + "_" + std::to_string(globalIdx) + "."
                                      + entry.extension);

                // -------- ENCODE --------
                double t0 = 0.0, t1 = 0.0;

                switch (entry.backend) {
                case EncoderBackend::Libvips: {
                    // Libvips-backed codec (goes through UCImage::Save)
                    auto opts = DefaultOptionsFor(entry.format);
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
                    break;
                }
                case EncoderBackend::QOI: {
                    // QOI via bundled qoi.h
#ifdef HAS_LIBVIPS
                    try {
                        vips::VImage vImg = state->sourceImage->GetVImage();
                        t0 = NowMs();
                        std::string err = EncodeQOIFromVImage(vImg, outPath);
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
                        r.errorMessage = "unknown exception during QOI encode";
                    }
#else
                    r.succeeded = false;
                    r.errorMessage = "libvips not available";
#endif
                    break;
                }
                case EncoderBackend::BMP: {
                    // BMP via bundled standalone encoder (this file)
#ifdef HAS_LIBVIPS
                    try {
                        vips::VImage vImg = state->sourceImage->GetVImage();
                        t0 = NowMs();
                        std::string err = EncodeBMPFromVImage(vImg, outPath);
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
                        r.errorMessage = "unknown exception during BMP encode";
                    }
#else
                    r.succeeded = false;
                    r.errorMessage = "libvips not available";
#endif
                    break;
                }
                }

                // -------- FILE SIZE + DECODE --------
                if (r.errorMessage.empty()) {
                    std::error_code ec;
                    r.encodedSizeBytes = static_cast<size_t>(
                            std::filesystem::file_size(outPath, ec));
                    if (ec) r.encodedSizeBytes = 0;

                    double d0 = 0.0, d1 = 0.0;
                    bool decodeOk = false;
                    try {
                        switch (entry.backend) {
                        case EncoderBackend::Libvips: {
                            // Libvips decode path.
                            //
                            // IMPORTANT: libvips uses lazy evaluation. A bare
                            // UCImage::Load() often only opens the file and
                            // parses the header — actual pixel decompression
                            // is deferred until someone reads the pixels. To
                            // make this timing comparable to QOI's (which
                            // always fully decompresses), we force pixel
                            // materialization via VImage::copy_memory(),
                            // which walks the full pipeline and writes every
                            // decoded pixel into a contiguous RAM buffer.
                            d0 = NowMs();
                            auto round = UCImage::Load(outPath, false);
                            if (round && round->IsValid()) {
#ifdef HAS_LIBVIPS
                                // Force eager decode: materialize all pixels now.
                                vips::VImage v = round->GetVImage();
                                v = v.copy_memory();
                                (void)v;
#endif
                                decodeOk = true;
                            }
                            d1 = NowMs();
                            break;
                        }
                        case EncoderBackend::QOI: {
                            // QOI decode via bundled library (already eager —
                            // qoi_read decompresses every pixel before returning).
                            d0 = NowMs();
                            decodeOk = DecodeQOIFile(outPath);
                            d1 = NowMs();
                            break;
                        }
                        case EncoderBackend::BMP: {
                            // BMP decode via bundled reader — reads headers,
                            // walks every row into a contiguous RGB buffer,
                            // releases it. Symmetric with the QOI timing path.
                            d0 = NowMs();
                            decodeOk = DecodeBMPFile(outPath);
                            d1 = NowMs();
                            break;
                        }
                        }
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
                            ShowFullSizeImageViewer(slotPath);
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
            state->includeBmpCheckbox->SetDisabled(false);
            state->benchmarkRunning = false;

            if (win && ctx) {
                win->UpdateAndRender();
            }

            debugOutput << "Image Performance Test v2.3.0 complete: "
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
            std::string fileName = std::filesystem::path(path).filename().string();
            std::ostringstream info;
            info << fileName << "  -  "
                 << state->sourceWidth << " x " << state->sourceHeight << " px";
            state->infoLabel->SetText(info.str());
            state->infoLabel->RequestRedraw();

            // Auto-run: kick the benchmark immediately after a successful load.
            runBenchmark();
            return true;
        };

        // [Choose Image...] click handler — opens dialog, loads, auto-runs.
        state->chooseBtn->SetOnClick([state, loadSourceImage, root]() {
            std::vector<FileFilter> filters = {
                    FileFilter("Bitmap Images",
                               {"png","jpg","jpeg","bmp","tiff","tif","gif",
                                "webp","avif","heif","heic","qoi","jp2"}),
                    FileFilter("All Files", { std::string("*") })
            };
            std::string path = UltraCanvasNativeDialogs::OpenFile(
                    "Choose Image for Benchmark",
                    filters,
                    InitialImageDir(),
                    root->GetWindow());
            if (path.empty()) return;   // user cancelled
            loadSourceImage(path);
        });

        // [x] Include BMP toggle handler — rebuild active codec set, sync the
        // chart's group count and labels, and re-run if an image is loaded.
        state->includeBmpCheckbox->onStateChanged =
                [state, rebuildActiveCodecs, runBenchmark, positionThumbnails](
                        CheckedState /*oldState*/, CheckedState newState) {
            if (state->benchmarkRunning) return;   // ignore mid-run toggles
            bool include = (newState == CheckedState::Checked);
            rebuildActiveCodecs(include);
            state->chartElem->SetCodecNames(state->codecNames);
            // Reposition thumbnails under the (possibly new) group columns.
            positionThumbnails();
            // Re-run if we already have an image to benchmark.
            if (state->sourceImage && state->sourceImage->IsValid()) {
                runBenchmark();
            }
        };

        return root;
    }

} // namespace UltraCanvas