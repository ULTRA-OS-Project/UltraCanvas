// Apps/DemoApp/UltraCanvasDependenciesExamples.cpp
// Dependencies & Third-Party libraries table for the UltraCanvas core and the
// additional ULTRA OS modules listed in this demo app. Rendered entirely with
// UltraCanvas GUI elements (UltraCanvasContainer / UltraCanvasLabel) and the
// UltraCanvasListView multi-column element.
//
// The table is organised by function: the first column names the component /
// purpose and each OS column lists the third-party libraries used for that
// purpose on Linux, macOS and Windows, one library PER LINE (a custom item
// delegate splits the cell text on '\n').
//
// Compiled from a full scan of every CMakeLists.txt (root, UltraCanvas core,
// Plugins/Vector/{CDR,XAR}) and the third-party #include statements across the
// source tree:
//   core      : Cairo, Pango/PangoCairo, HarfBuzz, FreeType, FontConfig, GLib,
//               TinyXML2, fmt, libvips (+ libpng/libjpeg-turbo/libtiff/libwebp/
//               giflib/libheif image codecs and the ImageMagick coder bridge)
//   bundled   : miniaudio, miniz, KissFFT, qrcodegen, qoi.h
//   optional  : GStreamer / AVFoundation / Media Foundation (video),
//               OpenGL+EGL/GLX/CGL/GLEW (3D), zbar (QR decode)
//   plugins   : MuPDF (+mujs/gumbo/jbig2dec/openjp2) PDF, libcdr+librevenge
//               (+librevenge-stream, LCMS2, ICU) CDR, libvips+zlib XAR,
//               jsoncpp (demo), ImageMagick `magick` (Windows .exe icon tool)
//   modules   : FFmpeg/libsndfile/FFTW3 (AudioFX), libvips (PixelFX),
//               libcurl/OpenSSL (UltraNet), FFmpeg (VideoFX),
//               libarchive/libmspack/wimlib + zlib/libzstd/liblz4/libbrotli
//               (VirtualFS); FileLoader/SmartHome/UltraAI use the core only.
// "(bundled)" = vendored in-tree, "(optional)" = built only when present.
// Version: 4.0.0
// Last Modified: 2026-06-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasListView.h"
#include <vector>

namespace UltraCanvas {

    // ===== MULTI-LINE CELL DELEGATE =====
    // Draws each newline-separated entry of a cell on its own line, vertically
    // centred within the (taller) uniform row, so every library used for a given
    // component appears on a separate line in the OS column.
    class DependencyCellDelegate : public IItemDelegate {
    public:
        DependencyCellDelegate(float font, int lineH, int pad)
            : fontSize(font), lineHeight(lineH), padding(pad) {}

        void RenderItem(IRenderContext* ctx, const IListModel* model,
                        int row, int column,
                        const ListItemStyleOption& option) override {
            if (!ctx || !model) return;

            ListIndex idx{row, column};
            std::string text = GetStringValue(model->GetData(idx, ListDataRole::DisplayRole));
            if (text.empty()) return;

            // Split the cell text into one entry per line.
            std::vector<std::string> lines;
            size_t start = 0;
            while (true) {
                size_t nl = text.find('\n', start);
                lines.push_back(nl == std::string::npos ? text.substr(start)
                                                        : text.substr(start, nl - start));
                if (nl == std::string::npos) break;
                start = nl + 1;
            }

            int textX = option.columnX + padding;
            int availW = option.columnWidth - padding * 2;
            if (availW <= 0) return;

            ctx->SetFontSize(fontSize);
            ctx->SetTextWrap(TextWrap::WrapNone);
            ctx->SetTextAlignment(option.columnAlignment);
            ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
            ctx->SetTextPaint(option.isSelected ? selectedTextColor : textColor);

            // Vertically centre the block of lines within the row.
            int blockH = static_cast<int>(lines.size()) * lineHeight;
            int lineY = option.rect.y + (option.rect.height - blockH) / 2;
            if (lineY < option.rect.y) lineY = option.rect.y;

            for (const auto& line : lines) {
                if (!line.empty()) {
                    ctx->DrawTextInRect(line, Rect2Dd(static_cast<float>(textX),
                                                      static_cast<float>(lineY),
                                                      static_cast<float>(availW),
                                                      static_cast<float>(lineHeight)));
                }
                lineY += lineHeight;
            }
        }

        int GetRowHeight(const IListModel* /*model*/, int /*row*/) const override {
            return rowHeight;
        }

        void SetRowHeight(int h) { rowHeight = h; }
        void SetTextColor(const Color& c) { textColor = c; }
        void SetSelectedTextColor(const Color& c) { selectedTextColor = c; }

    private:
        float fontSize;
        int lineHeight;
        int padding;
        int rowHeight = 24;
        Color textColor = Colors::TextDefault;
        Color selectedTextColor = Colors::White;
    };

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateDependenciesExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("DependenciesExamples", 0, 0, 1000, 900);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("DepTitle", 20, 10, 700, 35);
        title->SetText("Dependencies & Third-Party Libraries");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        container->AddChild(title);

        // Subtitle
        auto subtitle = std::make_shared<UltraCanvasLabel>("DepSubtitle", 20, 45, 940, 25);
        subtitle->SetText("By function: each row is a component / purpose; OS columns list one library per line.  "
                          "(bundled) = vendored, (optional) = feature-gated.  Image codecs are provided via libvips.");
        subtitle->SetFontSize(12);
        subtitle->SetTextColor(Color(120, 120, 120, 255));
        container->AddChild(subtitle);

        // ============================================================
        // Build the multi-column model:
        //   Component / Purpose | Linux | macOS | Windows
        // Multiple libraries in one OS cell are separated by '\n' so the custom
        // delegate renders each on its own line.
        // ============================================================
        auto model = std::make_shared<UltraCanvasMultiColumnListModel>();
        model->AddColumn(ListColumnDef("Component / Purpose", 215, TextAlignment::Left));
        model->AddColumn(ListColumnDef("Linux", 230, TextAlignment::Left));
        model->AddColumn(ListColumnDef("macOS", 225, TextAlignment::Left));
        model->AddColumn(ListColumnDef("Windows", 225, TextAlignment::Left));

        // ----- UltraCanvas core: rendering, text, windowing -----
        model->AddItem(MultiColumnListItem({"2D graphics rendering", "Cairo", "Cairo", "Cairo"}));
        model->AddItem(MultiColumnListItem({"Text layout & shaping", "Pango\nHarfBuzz\nFreeType", "Pango\nHarfBuzz\nFreeType", "Pango\nHarfBuzz\nFreeType"}));
        model->AddItem(MultiColumnListItem({"Font discovery", "FontConfig", "CoreText", "FontConfig"}));
        model->AddItem(MultiColumnListItem({"Native windowing & cursors", "X11\nXcursor", "Cocoa\nQuartzCore", "Win32 (gdi32)\nWin32 (user32)"}));
        model->AddItem(MultiColumnListItem({"Native dialogs & printing", "GTK 3", "Cocoa (AppKit)", "Win32 (comdlg32)"}));
        model->AddItem(MultiColumnListItem({"Core utilities", "GLib", "GLib", "GLib"}));
        model->AddItem(MultiColumnListItem({"XML parsing (SVG/config/ODS)", "TinyXML2", "TinyXML2", "TinyXML2"}));
        model->AddItem(MultiColumnListItem({"String formatting", "fmt", "fmt", "fmt"}));

        // ----- UltraCanvas core: image loading / processing (via libvips) -----
        model->AddItem(MultiColumnListItem({"Image processing & resize", "libvips", "libvips", "libvips"}));
        model->AddItem(MultiColumnListItem({"Bitmap codecs (via libvips)", "libpng\nlibjpeg-turbo\nlibtiff\nlibwebp", "libpng\nlibjpeg-turbo\nlibtiff\nlibwebp", "libpng\nlibjpeg-turbo\nlibtiff\nlibwebp"}));
        model->AddItem(MultiColumnListItem({"GIF / HEIF / AVIF (via libvips)", "giflib\nlibheif", "giflib\nlibheif", "giflib\nlibheif"}));
        model->AddItem(MultiColumnListItem({"BMP / PCX coders (via libvips)", "ImageMagick", "ImageMagick", "ImageMagick"}));
        model->AddItem(MultiColumnListItem({"QOI image format", "qoi.h (bundled)", "qoi.h (bundled)", "qoi.h (bundled)"}));

        // ----- UltraCanvas core: audio / video / 3D (optional) -----
        model->AddItem(MultiColumnListItem({"Audio playback & capture", "miniaudio (bundled)", "miniaudio (bundled)", "miniaudio (bundled)"}));
        model->AddItem(MultiColumnListItem({"Video playback & capture", "GStreamer (optional)", "AVFoundation (optional)", "Media Foundation (optional)"}));
        model->AddItem(MultiColumnListItem({"OpenGL 3D surface", "OpenGL\nEGL / GLX (optional)", "OpenGL\nCGL (optional)", "OpenGL\nGLEW / WGL (optional)"}));

        // ----- Bundled (vendored) third-party sources -----
        model->AddItem(MultiColumnListItem({"QR / barcode decoding", "zbar (optional)", "zbar (optional)", "zbar (optional)"}));
        model->AddItem(MultiColumnListItem({"QR code generation", "qrcodegen (bundled)", "qrcodegen (bundled)", "qrcodegen (bundled)"}));
        model->AddItem(MultiColumnListItem({"ZIP for ODS / XLSX I/O", "miniz (bundled)", "miniz (bundled)", "miniz (bundled)"}));
        model->AddItem(MultiColumnListItem({"FFT / spectrogram", "KissFFT (bundled)", "KissFFT (bundled)", "KissFFT (bundled)"}));

        // ----- Optional plugins -----
        model->AddItem(MultiColumnListItem({"PDF rendering (plugin)", "MuPDF\nmujs\ngumbo\njbig2dec / openjp2", "MuPDF\nmujs\ngumbo", "MuPDF\nmujs\ngumbo"}));
        model->AddItem(MultiColumnListItem({"CorelDRAW import (plugin)", "libcdr\nlibrevenge\nlibrevenge-stream\nLCMS2, ICU (optional)", "libcdr\nlibrevenge\nlibrevenge-stream\nLCMS2, ICU (optional)", "– (not available)"}));
        model->AddItem(MultiColumnListItem({"XARA (XAR) import (plugin)", "libvips\nzlib", "libvips\nzlib", "libvips\nzlib"}));
        model->AddItem(MultiColumnListItem({"JSON parsing (demo app)", "jsoncpp", "jsoncpp", "jsoncpp"}));
        model->AddItem(MultiColumnListItem({"App icon embedding (build tool)", "– (Windows only)", "– (Windows only)", "ImageMagick (magick CLI)"}));

        // ----- Additional ULTRA OS modules -----
        model->AddItem(MultiColumnListItem({"AudioFX module", "FFmpeg\nlibsndfile\nFFTW3", "FFmpeg\nlibsndfile\nFFTW3", "FFmpeg\nlibsndfile\nFFTW3"}));
        model->AddItem(MultiColumnListItem({"FileLoader module", "(core only)", "(core only)", "(core only)"}));
        model->AddItem(MultiColumnListItem({"IODeviceManager module", "SANE\nV4L2\nCUPS", "ICA\nAVFoundation", "WIA\nTWAIN\nMedia Foundation"}));
        model->AddItem(MultiColumnListItem({"PixelFX module", "libvips", "libvips", "libvips"}));
        model->AddItem(MultiColumnListItem({"Smart Home module", "(core only)", "(core only)", "(core only)"}));
        model->AddItem(MultiColumnListItem({"Ultra AI module", "(core only)", "(core only)", "(core only)"}));
        model->AddItem(MultiColumnListItem({"Ultra Net module", "libcurl\nOpenSSL", "libcurl\nOpenSSL", "libcurl\nOpenSSL"}));
        model->AddItem(MultiColumnListItem({"VideoFX module", "FFmpeg", "FFmpeg", "FFmpeg"}));
        model->AddItem(MultiColumnListItem({"VirtualFS – archive formats", "libarchive\nlibmspack\nwimlib", "libarchive\nlibmspack\nwimlib", "libarchive\nlibmspack\nwimlib"}));
        model->AddItem(MultiColumnListItem({"VirtualFS – compression", "zlib\nlibzstd\nliblz4\nlibbrotli", "zlib\nlibzstd\nliblz4\nlibbrotli", "zlib\nlibzstd\nliblz4\nlibbrotli"}));

        // ============================================================
        // The UltraCanvasListView table itself
        // ============================================================
        // The tallest cells carry 4 libraries, so size every row for 4 lines.
        const int lineHeight = 16;
        const int maxLines = 4;
        const int rowHeight = lineHeight * maxLines + 8;   // 72 px

        auto listView = std::make_shared<UltraCanvasListView>("DependenciesListView", 20, 80, 920, 790);
        listView->SetModel(model.get());

        ListViewStyle style;
        style.showHeader = true;
        style.showGridLines = true;
        style.headerHeight = 28;
        style.headerFontSize = 11;
        style.rowHeight = rowHeight;
        style.alternateRowColors = true;
        style.alternateRowColor = Color(244, 246, 250);
        listView->SetStyle(style);

        auto delegate = std::make_shared<DependencyCellDelegate>(11.0f, lineHeight, 8);
        delegate->SetRowHeight(rowHeight);
        listView->SetDelegate(delegate);

        container->AddChild(listView);

        return container;
    }

} // namespace UltraCanvas
