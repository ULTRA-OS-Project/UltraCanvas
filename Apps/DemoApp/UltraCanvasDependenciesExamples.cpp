// Apps/DemoApp/UltraCanvasDependenciesExamples.cpp
// Dependencies & Third-Party libraries table for the UltraCanvas core and the
// additional ULTRA OS modules listed in this demo app. Rendered entirely with
// UltraCanvas GUI elements (UltraCanvasContainer / UltraCanvasLabel) and the
// UltraCanvasListView multi-column element.
// Version: 1.0.0
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
        auto subtitle = std::make_shared<UltraCanvasLabel>("DepSubtitle", 20, 45, 900, 25);
        subtitle->SetText("Libraries used by the UltraCanvas core and the additional ULTRA OS modules. "
                          "Type: Required / Optional / Bundled (vendored).");
        subtitle->SetFontSize(12);
        subtitle->SetTextColor(Color(120, 120, 120, 255));
        container->AddChild(subtitle);

        // ============================================================
        // Build the multi-column model: Used By | Library | Purpose | License | Type
        // ============================================================
        auto model = std::make_shared<UltraCanvasMultiColumnListModel>();
        model->AddColumn(ListColumnDef("Used By", 150, TextAlignment::Left));
        model->AddColumn(ListColumnDef("Library", 150, TextAlignment::Left));
        model->AddColumn(ListColumnDef("Purpose", 330, TextAlignment::Left));
        model->AddColumn(ListColumnDef("License", 150, TextAlignment::Left));
        model->AddColumn(ListColumnDef("Type", 100, TextAlignment::Left));

        // ----- UltraCanvas core: rendering & text -----
        model->AddItem(MultiColumnListItem({"Core", "Cairo", "2D vector graphics rendering backend", "LGPL-2.1 / MPL-1.1", "Required"}));
        model->AddItem(MultiColumnListItem({"Core", "Pango / PangoCairo", "Unicode text layout and rendering", "LGPL-2.1", "Required"}));
        model->AddItem(MultiColumnListItem({"Core", "HarfBuzz", "OpenType text shaping", "MIT", "Required"}));
        model->AddItem(MultiColumnListItem({"Core", "FreeType", "Font loading and glyph rasterization", "FTL / GPL-2.0", "Required"}));
        model->AddItem(MultiColumnListItem({"Core", "FontConfig", "Font discovery / bundled-font registration", "MIT", "Required"}));
        model->AddItem(MultiColumnListItem({"Core", "GLib (glib-2.0)", "Core utility and data structures", "LGPL-2.1", "Required"}));
        model->AddItem(MultiColumnListItem({"Core", "TinyXML2", "XML parsing (SVG, config, ODS)", "Zlib", "Required"}));
        model->AddItem(MultiColumnListItem({"Core", "fmt", "Type-safe string formatting", "MIT", "Required"}));
        model->AddItem(MultiColumnListItem({"Core", "libvips / vips-cpp", "Image loading, processing and effects", "LGPL-2.1", "Required"}));

        // ----- UltraCanvas core: platform windowing -----
        model->AddItem(MultiColumnListItem({"Core (Linux)", "X11 / Xcursor", "Window system and cursors", "MIT / X11", "Required"}));
        model->AddItem(MultiColumnListItem({"Core (Linux)", "GTK 3", "Native dialogs and Unix printing", "LGPL-2.1", "Required"}));
        model->AddItem(MultiColumnListItem({"Core (Windows)", "Win32 SDK (gdi32, ole32, ...)", "Native windowing and dialogs", "Proprietary (SDK)", "Required"}));
        model->AddItem(MultiColumnListItem({"Core (macOS)", "Cocoa / QuartzCore / CoreText", "Native windowing and text", "Proprietary (SDK)", "Required"}));

        // ----- UltraCanvas core: optional features -----
        model->AddItem(MultiColumnListItem({"Core (Audio)", "miniaudio", "Cross-platform audio playback / capture", "MIT-0 / Public Domain", "Bundled"}));
        model->AddItem(MultiColumnListItem({"Core (Video)", "GStreamer 1.0", "Video playback and capture (Linux)", "LGPL-2.1", "Optional"}));
        model->AddItem(MultiColumnListItem({"Core (Video)", "Media Foundation", "Video playback and capture (Windows)", "Proprietary (SDK)", "Optional"}));
        model->AddItem(MultiColumnListItem({"Core (Video)", "AVFoundation", "Video playback and capture (macOS)", "Proprietary (SDK)", "Optional"}));
        model->AddItem(MultiColumnListItem({"Core (3D)", "OpenGL / EGL / GLEW", "OpenGL 3D surface rendering", "MIT (GLEW) / SDK", "Optional"}));
        model->AddItem(MultiColumnListItem({"Core (QR)", "zbar", "QR / barcode decoding", "LGPL-2.1", "Optional"}));

        // ----- UltraCanvas core: vendored (bundled) third-party sources -----
        model->AddItem(MultiColumnListItem({"Core (Spreadsheet)", "miniz", "ZIP support for ODS / XLSX I/O", "MIT", "Bundled"}));
        model->AddItem(MultiColumnListItem({"Core (Charts)", "KissFFT", "Real-input FFT for the spectrogram", "BSD-3-Clause", "Bundled"}));
        model->AddItem(MultiColumnListItem({"QRCode plugin", "qrcodegen", "QR code generation", "MIT", "Bundled"}));

        // ----- UltraCanvas optional plugins -----
        model->AddItem(MultiColumnListItem({"PDF plugin", "MuPDF (+ mujs, gumbo, jbig2dec, openjp2)", "PDF rendering and navigation", "AGPL-3.0", "Optional"}));
        model->AddItem(MultiColumnListItem({"CDR plugin", "libcdr / librevenge", "CorelDRAW (CDR) import", "MPL-2.0", "Optional"}));
        model->AddItem(MultiColumnListItem({"Demo app", "jsoncpp", "JSON parsing for demo data", "MIT / Public Domain", "Optional"}));

        // ----- Additional ULTRA OS modules -----
        model->AddItem(MultiColumnListItem({"AudioFX", "FFmpeg", "Audio decoding / encoding", "LGPL-2.1 / GPL", "Optional"}));
        model->AddItem(MultiColumnListItem({"AudioFX", "libsndfile", "Reading / writing sound files", "LGPL-2.1", "Optional"}));
        model->AddItem(MultiColumnListItem({"AudioFX", "FFTW3", "Fast Fourier transforms", "GPL-2.0 / Commercial", "Optional"}));
        model->AddItem(MultiColumnListItem({"FileLoader", "(none)", "Uses UltraCanvas core only", "-", "-"}));
        model->AddItem(MultiColumnListItem({"IODeviceManager", "(OS protocols)", "SANE/TWAIN/V4L2/CUPS via OS APIs", "-", "Optional"}));
        model->AddItem(MultiColumnListItem({"PixelFX", "libvips", "Image processing and effects", "LGPL-2.1", "Required"}));
        model->AddItem(MultiColumnListItem({"Smart Home", "(none)", "Uses UltraCanvas core only", "-", "-"}));
        model->AddItem(MultiColumnListItem({"Ultra AI", "(none)", "Uses UltraNet / UltraVault siblings", "-", "-"}));
        model->AddItem(MultiColumnListItem({"Ultra Net", "libcurl", "HTTP / networking transport", "MIT / curl", "Required"}));
        model->AddItem(MultiColumnListItem({"Ultra Net", "OpenSSL", "TLS / cryptography", "Apache-2.0", "Required"}));
        model->AddItem(MultiColumnListItem({"VideoFX", "FFmpeg", "Video decoding / encoding / filtering", "LGPL-2.1 / GPL", "Required"}));
        model->AddItem(MultiColumnListItem({"VirtualFS", "libarchive", "Archive format support (40+ formats)", "BSD-2-Clause", "Required"}));
        model->AddItem(MultiColumnListItem({"VirtualFS", "zlib", "Deflate / GZIP compression", "Zlib", "Required"}));
        model->AddItem(MultiColumnListItem({"VirtualFS", "libzstd", "Zstandard compression", "BSD-3-Clause", "Optional"}));
        model->AddItem(MultiColumnListItem({"VirtualFS", "liblz4", "LZ4 fast compression", "BSD-2-Clause", "Optional"}));
        model->AddItem(MultiColumnListItem({"VirtualFS", "libmspack", "CHM / LIT / CAB formats", "LGPL-2.1", "Optional"}));
        model->AddItem(MultiColumnListItem({"VirtualFS", "wimlib", "Windows Imaging (WIM) format", "LGPL-3.0", "Optional"}));
        model->AddItem(MultiColumnListItem({"VirtualFS", "libbrotli", "Brotli compression", "MIT", "Optional"}));

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

        // Status feedback label updated on row click.
        auto statusLabel = std::make_shared<UltraCanvasLabel>("DepStatusLabel", 20, 870, 920, 22);
        statusLabel->SetText("Total third-party dependencies listed: " + std::to_string(model->GetRowCount()) +
                             "   |   Click a row for details");
        statusLabel->SetFontSize(11);
        statusLabel->SetTextColor(Color(120, 120, 120, 255));

        listView->onItemClicked = [statusLabel, model](int row) {
            const auto& item = model->GetItem(row);
            if (item.labels.size() >= 5) {
                statusLabel->SetText(item.labels[1] + " — " + item.labels[2] +
                                     "   |   License: " + item.labels[3] +
                                     "   |   " + item.labels[4]);
            }
        };

        container->AddChild(listView);
        container->AddChild(statusLabel);

        return container;
    }

} // namespace UltraCanvas
