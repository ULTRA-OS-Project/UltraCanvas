// Apps/DemoApp/UltraCanvasDependenciesExamples.cpp
// Dependencies & Third-Party libraries table for the UltraCanvas core and the
// additional ULTRA OS modules listed in this demo app. Rendered entirely with
// UltraCanvas GUI elements (UltraCanvasContainer / UltraCanvasLabel) and the
// UltraCanvasListView multi-column element.
//
// The table is organised by function: the first column names the component /
// purpose (e.g. "Native windowing & cursors") and each OS column lists the
// third-party libraries used for that purpose on Linux, macOS and Windows.
// Cross-platform libraries repeat across the three OS columns; platform-specific
// ones appear only under their OS. "(bundled)" = vendored in-tree, "(optional)"
// = built only when the dependency / feature is present, "(core only)" = needs
// no extra third party. Applicability mirrors the build files.
// Version: 2.0.0
// Last Modified: 2026-06-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasListView.h"

namespace UltraCanvas {

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
        subtitle->SetText("Organised by function: each row is a component / purpose; the OS columns list "
                          "the libraries used on each platform.  (bundled) = vendored, (optional) = feature-gated.");
        subtitle->SetFontSize(12);
        subtitle->SetTextColor(Color(120, 120, 120, 255));
        container->AddChild(subtitle);

        // ============================================================
        // Build the multi-column model:
        //   Component / Purpose | Linux | macOS | Windows
        // ============================================================
        auto model = std::make_shared<UltraCanvasMultiColumnListModel>();
        model->AddColumn(ListColumnDef("Component / Purpose", 190, TextAlignment::Left));
        model->AddColumn(ListColumnDef("Linux", 240, TextAlignment::Left));
        model->AddColumn(ListColumnDef("macOS", 235, TextAlignment::Left));
        model->AddColumn(ListColumnDef("Windows", 235, TextAlignment::Left));

        // ----- UltraCanvas core -----
        model->AddItem(MultiColumnListItem({"2D graphics rendering", "Cairo", "Cairo", "Cairo"}));
        model->AddItem(MultiColumnListItem({"Text layout & shaping", "Pango, HarfBuzz, FreeType", "Pango, HarfBuzz, FreeType", "Pango, HarfBuzz, FreeType"}));
        model->AddItem(MultiColumnListItem({"Font discovery", "FontConfig", "CoreText", "FontConfig"}));
        model->AddItem(MultiColumnListItem({"Native windowing & cursors", "X11, Xcursor", "Cocoa, QuartzCore", "Win32 (gdi32, user32)"}));
        model->AddItem(MultiColumnListItem({"Native dialogs & printing", "GTK 3", "Cocoa (AppKit)", "Win32 (comdlg32)"}));
        model->AddItem(MultiColumnListItem({"Image processing", "libvips", "libvips", "libvips"}));
        model->AddItem(MultiColumnListItem({"XML parsing (SVG/config/ODS)", "TinyXML2", "TinyXML2", "TinyXML2"}));
        model->AddItem(MultiColumnListItem({"Core utilities", "GLib", "GLib", "GLib"}));
        model->AddItem(MultiColumnListItem({"String formatting", "fmt", "fmt", "fmt"}));
        model->AddItem(MultiColumnListItem({"Audio playback & capture", "miniaudio (bundled)", "miniaudio (bundled)", "miniaudio (bundled)"}));
        model->AddItem(MultiColumnListItem({"Video playback & capture", "GStreamer (optional)", "AVFoundation (optional)", "Media Foundation (optional)"}));
        model->AddItem(MultiColumnListItem({"OpenGL 3D surface", "OpenGL + EGL/GLX (opt.)", "OpenGL / CGL (opt.)", "OpenGL + GLEW/WGL (opt.)"}));
        model->AddItem(MultiColumnListItem({"QR / barcode decoding", "zbar (optional)", "zbar (optional)", "zbar (optional)"}));

        // ----- Bundled (vendored) third-party sources -----
        model->AddItem(MultiColumnListItem({"QR code generation", "qrcodegen (bundled)", "qrcodegen (bundled)", "qrcodegen (bundled)"}));
        model->AddItem(MultiColumnListItem({"ZIP for ODS / XLSX I/O", "miniz (bundled)", "miniz (bundled)", "miniz (bundled)"}));
        model->AddItem(MultiColumnListItem({"FFT / spectrogram", "KissFFT (bundled)", "KissFFT (bundled)", "KissFFT (bundled)"}));

        // ----- Optional plugins -----
        model->AddItem(MultiColumnListItem({"PDF rendering (plugin)", "MuPDF (+mujs, gumbo, jbig2dec)", "MuPDF (+mujs, gumbo)", "MuPDF (+mujs, gumbo)"}));
        model->AddItem(MultiColumnListItem({"CorelDRAW import (plugin)", "libcdr, librevenge", "libcdr, librevenge", "– (not available)"}));
        model->AddItem(MultiColumnListItem({"JSON parsing (demo app)", "jsoncpp", "jsoncpp", "jsoncpp"}));

        // ----- Additional ULTRA OS modules -----
        model->AddItem(MultiColumnListItem({"AudioFX module", "FFmpeg, libsndfile, FFTW3", "FFmpeg, libsndfile, FFTW3", "FFmpeg, libsndfile, FFTW3"}));
        model->AddItem(MultiColumnListItem({"FileLoader module", "(core only)", "(core only)", "(core only)"}));
        model->AddItem(MultiColumnListItem({"IODeviceManager module", "SANE, V4L2, CUPS", "ICA, AVFoundation", "WIA, TWAIN, Media Foundation"}));
        model->AddItem(MultiColumnListItem({"PixelFX module", "libvips", "libvips", "libvips"}));
        model->AddItem(MultiColumnListItem({"Smart Home module", "(core only)", "(core only)", "(core only)"}));
        model->AddItem(MultiColumnListItem({"Ultra AI module", "(core only)", "(core only)", "(core only)"}));
        model->AddItem(MultiColumnListItem({"Ultra Net module", "libcurl, OpenSSL", "libcurl, OpenSSL", "libcurl, OpenSSL"}));
        model->AddItem(MultiColumnListItem({"VideoFX module", "FFmpeg", "FFmpeg", "FFmpeg"}));
        model->AddItem(MultiColumnListItem({"VirtualFS – archive formats", "libarchive, libmspack, wimlib", "libarchive, libmspack, wimlib", "libarchive, libmspack, wimlib"}));
        model->AddItem(MultiColumnListItem({"VirtualFS – compression", "zlib, libzstd, liblz4, brotli", "zlib, libzstd, liblz4, brotli", "zlib, libzstd, liblz4, brotli"}));

        // ============================================================
        // The UltraCanvasListView table itself
        // ============================================================
        auto listView = std::make_shared<UltraCanvasListView>("DependenciesListView", 20, 80, 920, 790);
        listView->SetModel(model.get());

        ListViewStyle style;
        style.showHeader = true;
        style.showGridLines = true;
        style.headerHeight = 26;
        style.headerFontSize = 11;
        style.rowHeight = 22;
        style.alternateRowColors = true;
        style.alternateRowColor = Color(244, 246, 250);
        listView->SetStyle(style);

        auto delegate = std::make_shared<UltraCanvasDefaultListDelegate>();
        delegate->SetFontSize(11);
        listView->SetDelegate(delegate);

        // Status feedback label updated on row click (shows the full per-OS lists,
        // which may be ellipsized inside the narrower table columns).
        auto statusLabel = std::make_shared<UltraCanvasLabel>("DepStatusLabel", 20, 870, 920, 22);
        statusLabel->SetText("Functional areas listed: " + std::to_string(model->GetRowCount()) +
                             "   |   Click a row for the full per-platform library list");
        statusLabel->SetFontSize(11);
        statusLabel->SetTextColor(Color(120, 120, 120, 255));

        listView->onItemClicked = [statusLabel, model](int row) {
            const auto& item = model->GetItem(row);
            if (item.labels.size() >= 4) {
                statusLabel->SetText(item.labels[0] +
                                     "   |   Linux: " + item.labels[1] +
                                     "   |   macOS: " + item.labels[2] +
                                     "   |   Windows: " + item.labels[3]);
            }
        };

        container->AddChild(listView);
        container->AddChild(statusLabel);

        return container;
    }

} // namespace UltraCanvas
