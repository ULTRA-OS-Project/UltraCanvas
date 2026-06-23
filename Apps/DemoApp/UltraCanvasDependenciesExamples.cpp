// Apps/DemoApp/UltraCanvasDependenciesExamples.cpp
// Dependencies & Third-Party libraries table for the UltraCanvas core and the
// additional ULTRA OS modules listed in this demo app. Rendered entirely with
// UltraCanvas GUI elements (UltraCanvasContainer / UltraCanvasLabel) and the
// UltraCanvasListView multi-column element.
//
// The list is grouped PER MODULE: a section-header row names each module
// (UltraCanvas core, the optional plugins, then FileLoader / UltraNet /
// SmartHome / UltraAI / AudioFX / PixelFX / VideoFX / VirtualFS /
// IODeviceManager). Under each header the rows give the purpose and the
// third-party libraries used on Linux, macOS and Windows, one library per line.
//
// QOI and XAR are intentionally NOT listed: those formats are implemented by
// UltraCanvas itself, not pulled in as third-party libraries.
//
// "(bundled)" = vendored in-tree, "(optional)" = built only when present.
// Version: 5.0.0
// Last Modified: 2026-06-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasListView.h"
#include <vector>

namespace UltraCanvas {

    // ===== MULTI-LINE / GROUPED CELL DELEGATE =====
    // Two row kinds:
    //  * Module section header  - all OS columns empty: drawn as a coloured band
    //                             with the module name in the first column.
    //  * Dependency row         - each newline-separated library on its own line.
    class DependencyCellDelegate : public IItemDelegate {
    public:
        DependencyCellDelegate(float font, int lineH, int pad)
            : fontSize(font), lineHeight(lineH), padding(pad) {}

        void RenderItem(IRenderContext* ctx, const IListModel* model,
                        int row, int column,
                        const ListItemStyleOption& option) override {
            if (!ctx || !model) return;

            // A row is a module section header when its OS columns are all empty.
            bool isHeader = true;
            for (int c = 1; c < option.columnCount; ++c) {
                if (!GetStringValue(model->GetData(ListIndex{row, c}, ListDataRole::DisplayRole)).empty()) {
                    isHeader = false;
                    break;
                }
            }

            int textX = option.columnX + padding;
            int availW = option.columnWidth - padding * 2;

            if (isHeader) {
                // Coloured band across every cell of the row.
                ctx->SetFillPaint(headerBackground);
                ctx->FillRectangle(Rect2Dd(static_cast<float>(option.columnX),
                                           static_cast<float>(option.rect.y),
                                           static_cast<float>(option.columnWidth),
                                           static_cast<float>(option.rect.height)));
                if (column == 0 && availW > 0) {
                    std::string name = GetStringValue(model->GetData(ListIndex{row, 0}, ListDataRole::DisplayRole));
                    ctx->SetFontSize(fontSize + 1.0f);
                    ctx->SetTextWrap(TextWrap::WrapNone);
                    ctx->SetTextAlignment(TextAlignment::Left);
                    ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
                    ctx->SetTextPaint(headerTextColor);
                    ctx->DrawTextInRect(name, Rect2Dd(static_cast<float>(textX),
                                                      static_cast<float>(option.rect.y),
                                                      static_cast<float>(availW),
                                                      static_cast<float>(option.rect.height)));
                }
                return;
            }

            // Regular dependency cell: one library per line.
            std::string text = GetStringValue(model->GetData(ListIndex{row, column}, ListDataRole::DisplayRole));
            if (text.empty() || availW <= 0) return;

            std::vector<std::string> lines;
            size_t start = 0;
            while (true) {
                size_t nl = text.find('\n', start);
                lines.push_back(nl == std::string::npos ? text.substr(start)
                                                        : text.substr(start, nl - start));
                if (nl == std::string::npos) break;
                start = nl + 1;
            }

            ctx->SetFontSize(fontSize);
            ctx->SetTextWrap(TextWrap::WrapNone);
            ctx->SetTextAlignment(option.columnAlignment);
            ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
            ctx->SetTextPaint(option.isSelected ? selectedTextColor : textColor);

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

    private:
        float fontSize;
        int lineHeight;
        int padding;
        int rowHeight = 24;
        Color textColor = Colors::TextDefault;
        Color selectedTextColor = Colors::White;
        Color headerBackground = Color(222, 228, 245);
        Color headerTextColor = Color(40, 50, 120);
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
        subtitle->SetText("Grouped per module. Under each module the OS columns list one library per line.  "
                          "(bundled) = vendored, (optional) = feature-gated.");
        subtitle->SetFontSize(12);
        subtitle->SetTextColor(Color(120, 120, 120, 255));
        container->AddChild(subtitle);

        // ============================================================
        // Build the model. A row with empty OS columns is a module section
        // header; otherwise it is a dependency row (libraries split on '\n').
        // ============================================================
        auto model = std::make_shared<UltraCanvasMultiColumnListModel>();
        model->AddColumn(ListColumnDef("Module / Purpose", 215, TextAlignment::Left));
        model->AddColumn(ListColumnDef("Linux", 230, TextAlignment::Left));
        model->AddColumn(ListColumnDef("macOS", 225, TextAlignment::Left));
        model->AddColumn(ListColumnDef("Windows", 225, TextAlignment::Left));

        auto header = [&](const std::string& moduleName) {
            model->AddItem(MultiColumnListItem({moduleName, "", "", ""}));
        };
        auto dep = [&](const std::string& purpose, const std::string& linux,
                       const std::string& mac, const std::string& win) {
            model->AddItem(MultiColumnListItem({purpose, linux, mac, win}));
        };

        // ===== UltraCanvas (core) =====
        header("UltraCanvas (core)");
        dep("2D graphics rendering", "Cairo", "Cairo", "Cairo");
        dep("Text layout & shaping", "Pango\nHarfBuzz\nFreeType", "Pango\nHarfBuzz\nFreeType", "Pango\nHarfBuzz\nFreeType");
        dep("Font discovery", "FontConfig", "CoreText", "FontConfig");
        dep("Native windowing & cursors", "X11\nXcursor", "Cocoa\nQuartzCore", "Win32 (gdi32)\nWin32 (user32)");
        dep("Native dialogs & printing", "GTK 3", "Cocoa (AppKit)", "Win32 (comdlg32)");
        dep("Core utilities", "GLib", "GLib", "GLib");
        dep("XML parsing (SVG/config/ODS)", "TinyXML2", "TinyXML2", "TinyXML2");
        dep("String formatting", "fmt", "fmt", "fmt");
        dep("Image processing & resize", "libvips", "libvips", "libvips");
        dep("Bitmap codecs (via libvips)", "libpng\nlibjpeg-turbo\nlibtiff\nlibwebp", "libpng\nlibjpeg-turbo\nlibtiff\nlibwebp", "libpng\nlibjpeg-turbo\nlibtiff\nlibwebp");
        dep("GIF / HEIF / AVIF (via libvips)", "giflib\nlibheif", "giflib\nlibheif", "giflib\nlibheif");
        dep("BMP / PCX coders (via libvips)", "ImageMagick", "ImageMagick", "ImageMagick");
        dep("Audio playback & capture", "miniaudio (bundled)", "miniaudio (bundled)", "miniaudio (bundled)");
        dep("Video playback & capture", "GStreamer (optional)", "AVFoundation (optional)", "Media Foundation (optional)");
        dep("OpenGL 3D surface", "OpenGL\nEGL / GLX (optional)", "OpenGL\nCGL (optional)", "OpenGL\nGLEW / WGL (optional)");
        dep("QR / barcode decoding", "zbar (optional)", "zbar (optional)", "zbar (optional)");
        dep("QR code generation", "qrcodegen (bundled)", "qrcodegen (bundled)", "qrcodegen (bundled)");
        dep("ZIP for ODS / XLSX I/O", "miniz (bundled)", "miniz (bundled)", "miniz (bundled)");
        dep("FFT / spectrogram", "KissFFT (bundled)", "KissFFT (bundled)", "KissFFT (bundled)");

        // ===== Optional plugins =====
        header("PDF plugin (optional)");
        dep("PDF rendering", "MuPDF\nmujs\ngumbo\njbig2dec / openjp2", "MuPDF\nmujs\ngumbo", "MuPDF\nmujs\ngumbo");

        header("CDR plugin (optional)");
        dep("CorelDRAW import", "libcdr\nlibrevenge\nlibrevenge-stream\nLCMS2, ICU (optional)", "libcdr\nlibrevenge\nlibrevenge-stream\nLCMS2, ICU (optional)", "– (not available)");

        // ===== Demo application =====
        header("Demo application");
        dep("JSON parsing", "jsoncpp", "jsoncpp", "jsoncpp");
        dep("App icon → .exe (build tool)", "– (Windows only)", "– (Windows only)", "ImageMagick (magick CLI)");

        // ===== Additional ULTRA OS modules =====
        header("AudioFX module");
        dep("Audio effects / decode", "FFmpeg\nlibsndfile\nFFTW3", "FFmpeg\nlibsndfile\nFFTW3", "FFmpeg\nlibsndfile\nFFTW3");

        header("FileLoader module");
        dep("No additional third party", "(core only)", "(core only)", "(core only)");

        header("IODeviceManager module");
        dep("Scanners / cameras / print", "SANE\nV4L2\nCUPS", "ICA\nAVFoundation", "WIA\nTWAIN\nMedia Foundation");

        header("PixelFX module");
        dep("Image effects", "libvips", "libvips", "libvips");

        header("Smart Home module");
        dep("No additional third party", "(core only)", "(core only)", "(core only)");

        header("Ultra AI module");
        dep("No additional third party", "(core only)", "(core only)", "(core only)");

        header("Ultra Net module");
        dep("Networking / TLS", "libcurl\nOpenSSL", "libcurl\nOpenSSL", "libcurl\nOpenSSL");

        header("VideoFX module");
        dep("Video effects / transcode", "FFmpeg", "FFmpeg", "FFmpeg");

        header("VirtualFS module");
        dep("Archive formats", "libarchive\nlibmspack\nwimlib", "libarchive\nlibmspack\nwimlib", "libarchive\nlibmspack\nwimlib");
        dep("Compression", "zlib\nlibzstd\nliblz4\nlibbrotli", "zlib\nlibzstd\nliblz4\nlibbrotli", "zlib\nlibzstd\nliblz4\nlibbrotli");

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
        style.alternateRowColors = false;   // grouping bands provide the structure
        listView->SetStyle(style);

        auto delegate = std::make_shared<DependencyCellDelegate>(11.0f, lineHeight, 8);
        delegate->SetRowHeight(rowHeight);
        listView->SetDelegate(delegate);

        container->AddChild(listView);

        return container;
    }

} // namespace UltraCanvas
