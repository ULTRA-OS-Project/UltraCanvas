// Apps/DemoApp/UltraCanvasDemoExamples.cpp
// Implementation of all component example creators
// Version: 1.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Text/UltraCanvasMarkdown.h"
//#include "UltraCanvasButton3Sections.h"
#include "UltraCanvasFormulaEditor.h"
#include "Plugins/Charts/UltraCanvasDivergingBarChart.h"
#include <sstream>
#include <random>
#include <map>

namespace UltraCanvas {
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateVectorExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("VectorExamples", 900, 0, 0, 1000, 600);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("VectorTitle", 901, 10, 10, 300, 30);
        title->SetText("Vector Graphics Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // Drawing Surface
//        auto drawingSurface = std::make_shared<UltraCanvasDrawingSurface>("DrawingSurface", 902, 20, 50, 600, 400);
//        drawingSurface->SetBackgroundColor(Colors::White);
//        drawingSurface->SetBorderStyle(BorderStyle::Solid);
//        drawingSurface->SetBorderWidth(2.0f);
//
//        // Draw some example shapes
//        drawingSurface->SetForegroundColor(Color(255, 0, 0, 255));
//        drawingSurface->DrawRectangle(50, 50, 100, 80);
//
//        drawingSurface->SetForegroundColor(Color(0, 255, 0, 255));
//        drawingSurface->DrawCircle(200, 100, 40);
//
//        drawingSurface->SetForegroundColor(Color(0, 0, 255, 255));
//        drawingSurface->SetLineWidth(3.0f);
//        drawingSurface->DrawLine(Point2D(300, 50), Point2D(400, 150));
//
//        container->AddChild(drawingSurface);
//
//        // Drawing tools info
//        auto toolsLabel = std::make_shared<UltraCanvasLabel>("VectorTools", 903, 650, 70, 320, 200);
//        toolsLabel->SetText("Drawing Surface Features:\n• Vector primitives (lines, circles, rectangles)\n• Bezier curves and paths\n• Fill and stroke styling\n• Layer management\n• Undo/redo support\n• Selection and manipulation\n• Export to SVG/PNG");
//        toolsLabel->SetBackgroundColor(Color(240, 255, 240, 255));
////        toolsLabel->SetBorderStyle(BorderStyle::Solid);
//        toolsLabel->SetPadding(10.0f);
//        container->AddChild(toolsLabel);

        return container;
    }

// ===== NOT IMPLEMENTED PLACEHOLDERS =====

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateToolbarExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("ToolbarExamples", 1000, 0, 0, 1000, 600);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("ToolbarTitle", 1001, 10, 10, 300, 30);
        title->SetText("Toolbar Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // Placeholder for toolbar implementation
        auto placeholder = std::make_shared<UltraCanvasLabel>("ToolbarPlaceholder", 1002, 20, 50, 800, 400);
        placeholder->SetText("Toolbar Component - Partially Implemented\n\nPlanned Features:\n• Horizontal and vertical toolbars\n• Icon buttons with tooltips\n• Separator elements\n• Dropdown menu buttons\n• Overflow handling\n• Customizable button groups\n• Ribbon-style layout\n• Drag and drop reordering");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 255, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorders(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTableViewExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("TableViewExamples", 1100, 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("TableViewTitle", 1101, 10, 10, 300, 30);
        title->SetText("Spreadsheet View Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("TableViewPlaceholder", 1102, 20, 50, 800, 400);
        placeholder->SetText("Table View Component - Not Implemented\n\nPlanned Features:\n• Data grid with rows and columns\n• Sortable column headers\n• Cell editing capabilities\n• Row selection (single/multiple)\n• Virtual scrolling for large datasets\n• Custom cell renderers\n• Column resizing and reordering\n• Filtering and search\n• Export to CSV/Excel");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 200, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorders(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateListViewExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("ListViewExamples", 1200, 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("ListViewTitle", 1201, 10, 10, 300, 30);
        title->SetText("List View Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("ListViewPlaceholder", 1202, 20, 50, 800, 400);
        placeholder->SetText("List View Component - Not Implemented\n\nPlanned Features:\n• Vertical and horizontal lists\n• Icon and text display modes\n• Custom item templates\n• Virtual scrolling\n• Item selection and highlighting\n• Drag and drop reordering\n• Search and filtering\n• Group headers\n• Context menus");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 200, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorders(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateDialogExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("DialogExamples", 1400, 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("DialogTitle", 1401, 10, 10, 300, 30);
        title->SetText("Dialog Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("DialogPlaceholder", 1402, 20, 50, 800, 400);
        placeholder->SetText("Dialog Component - Not Implemented\n\nPlanned Features:\n• Modal and modeless dialogs\n• Message boxes (Info, Warning, Error)\n• File open/save dialogs\n• Color picker dialogs\n• Font selection dialogs\n• Custom dialog layouts\n• Dialog result handling\n• Animation effects\n• Keyboard shortcuts (ESC, Enter)");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 200, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorders(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateBitmapNotImplementedExamples(const std::string& format) {
        auto container = std::make_shared<UltraCanvasContainer>("BitmapExamples", 1500, 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("BitmapTitle", 1501, 10, 10, 300, 30);
        title->SetText("Bitmap Graphics Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("BitmapPlaceholder", 1502, 20, 50, 800, 400);
        placeholder->SetText(format + " Graphics - Not Implemented yet\n\nSupported Formats:\n• PNG - Full support with transparency\n• JPEG - Standard image display\n\nFeatures:\n• Image scaling and cropping\n• Rotation and transformation\n• Filter effects\n• Multi-frame animation\n• Memory-efficient loading");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 255, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorders(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::Create3DExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("Graphics3DExamples", 1600, 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("Graphics3DTitle", 1601, 10, 10, 300, 30);
        title->SetText("3D Graphics Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("Graphics3DPlaceholder", 1602, 20, 50, 800, 400);
        placeholder->SetText("3D Graphics - Partially Implemented\n\nSupported Features:\n• 3DS model loading and display\n• 3DM model support\n• Basic scene graph\n• Camera controls (orbit, pan, zoom)\n• Basic lighting and shading\n• Texture mapping\n\nOpenGL/Vulkan Backend:\n• Hardware accelerated rendering\n• Vertex and fragment shaders\n• Multiple render targets\n• Anti-aliasing support");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 255, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorders(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateVideoExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("VideoExamples", 1700, 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("VideoTitle", 1701, 10, 10, 300, 30);
        title->SetText("Video Player Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("VideoPlaceholder", 1702, 20, 50, 800, 400);
        placeholder->SetText("Video Player Component - Not Implemented\n\nPlanned Features:\n• MP4, AVI, MOV playback\n• Hardware accelerated decoding\n• Custom playback controls\n• Fullscreen mode\n• Volume and timeline controls\n• Subtitle support\n• Frame-by-frame stepping\n• Video filters and effects\n• Streaming protocol support\n• Audio track selection");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 200, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorders(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTextDocumentExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("TextDocumentExamples", 1800, 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("TextDocumentTitle", 1801, 10, 10, 300, 30);
        title->SetText("Text Document Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("TextDocumentPlaceholder", 1802, 20, 50, 800, 400);
        placeholder->SetText("Text Document Support - Not Implemented\n\nPlanned Document Types:\n• ODT (OpenDocument Text)\n• RTF (Rich Text Format)\n• TeX/LaTeX documents\n• Plain text with syntax highlighting\n• HTML document rendering\n\nFeatures:\n• Document structure navigation\n• Text formatting display\n• Search and replace\n• Print preview\n• Export capabilities\n• Embedded images and tables");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 200, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorders(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateMarkdownExamples() {
//        auto container = std::make_shared<UltraCanvasContainer>("MarkdownExamples", 1900, 0, 0, 1020, 780);

        auto text = std::make_shared<UltraCanvasMarkdownDisplay>("MarkDownText", 1201, 0, 0, 1026, 785);
        text->SetMarkdownText(LoadFile("assets/MarkdownExample.md"));
        MarkdownStyle style = MarkdownStyle::Default();
        style.fontSize = 12;
        text->SetStyle(style);
//        container->AddChild(text);

        return text;
    }

//    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateCodeEditorExamples() {
//        auto container = std::make_shared<UltraCanvasContainer>("CodeEditorExamples", 2000, 0, 0, 1000, 600);
//
//        auto title = std::make_shared<UltraCanvasLabel>("CodeEditorTitle", 2001, 10, 10, 300, 30);
//        title->SetText("Code Editor Examples");
//        title->SetFontSize(16);
//        title->SetFontWeight(FontWeight::Bold);
//        container->AddChild(title);
//
//        // Formula Editor (partially implemented)
//        auto formulaEditor = std::make_shared<UltraCanvasFormulaEditor>("FormulaEditor", 2002, 20, 50, 600, 100);
//        ProceduralFormula f;
//        f.formula = "SUM(A1:A10) + AVERAGE(B1:B10) * 2.5";
//        formulaEditor->SetFormula(f);
////        formulaEditor->SetBackgroundColor(Color(248, 248, 248, 255));
//        container->AddChild(formulaEditor);
//
//        auto formulaLabel = std::make_shared<UltraCanvasLabel>("FormulaLabel", 2003, 20, 160, 600, 20);
//        formulaLabel->SetText("Formula Editor (Partially Implemented) - Supports mathematical expressions");
//        formulaLabel->SetFontSize(12);
//        container->AddChild(formulaLabel);
//
//        auto placeholder = std::make_shared<UltraCanvasLabel>("CodeEditorPlaceholder", 2004, 20, 200, 800, 300);
//        placeholder->SetText("Code Editor Component - Partially Implemented\n\nCurrent Features:\n• Formula/Expression editing\n• Basic syntax validation\n\nPlanned Features:\n• C++ syntax highlighting\n• Pascal/Delphi support\n• COBOL syntax support\n• Line numbers and folding\n• Auto-completion\n• Error markers and tooltips\n• Find and replace\n• Multiple cursors\n• Code formatting\n• Plugin architecture for languagesRules");
//        placeholder->SetAlignment(TextAlignment::Left);
//        placeholder->SetBackgroundColor(Color(255, 255, 200, 100));
////        placeholder->SetBorderStyle(BorderStyle::Dashed);
//        placeholder->SetBorders(2.0f);
//        placeholder->SetPadding(20.0f);
//        container->AddChild(placeholder);
//
//        return container;
//    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateAudioExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("AudioExamples", 2100, 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("AudioTitle", 2101, 10, 10, 300, 30);
        title->SetText("Audio Player Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("AudioPlaceholder", 2102, 20, 50, 800, 400);
        placeholder->SetText("Audio Player Component - Not Implemented\n\nPlanned Features:\n• FLAC lossless audio support\n• MP3, WAV, OGG playback\n• Waveform visualization\n• Spectrum analyzer\n• Playback controls (play/pause/stop)\n• Volume and position sliders\n• Playlist management\n• Audio effects and filters\n• Recording capabilities\n• Audio format conversion\n• Metadata display (ID3 tags)");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 200, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorders(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateDiagramExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("DiagramExamples", 2200, 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("DiagramTitle", 2201, 10, 10, 300, 30);
        title->SetText("Diagram Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("DiagramPlaceholder", 2202, 20, 50, 800, 400);
        placeholder->SetText("Diagram Support - Partially Implemented\n\nPlantUML Integration:\n• Class diagrams\n• Sequence diagrams\n• Activity diagrams\n• Use case diagrams\n• Component diagrams\n• State diagrams\n\nNative Diagram Engine:\n• Flowcharts\n• Organizational charts\n• Network diagrams\n• Mind maps\n• Interactive diagram editing\n• Export to SVG/PNG\n• Automatic layout algorithms");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 255, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorders(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

//    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateChartExamples() {
//        auto container = std::make_shared<UltraCanvasContainer>("ChartExamples", 2300, 0, 0, 1000, 600);
//
//        auto title = std::make_shared<UltraCanvasLabel>("chartTitle", 2301, 10, 10, 300, 30);
//        title->SetText("Chart Examples");
//        title->SetFontSize(16);
//        title->SetFontWeight(FontWeight::Bold);
//        container->AddChild(title);
//
//        auto placeholder = std::make_shared<UltraCanvasLabel>("ChartPlaceholder", 2302, 20, 50, 800, 400);
//        placeholder->SetText("Charts Component - Not Implemented\n\nPlanned Chart Types:\n• Line charts with multiple series\n• Bar charts (vertical/horizontal)\n• Pie and donut charts\n• Scatter plots with trend lines\n• Area charts (stacked/overlapped)\n• Candlestick charts for financial data\n• Heatmaps and bubble charts\n• Real-time updating charts\n\nFeatures:\n• Interactive legends\n• Zooming and panning\n• Data point tooltips\n• Animation effects\n• Custom styling themes\n• Export capabilities\n• Data binding from multiple sources");
//        placeholder->SetAlignment(TextAlignment::Left);
//        placeholder->SetBackgroundColor(Color(255, 200, 200, 100));
////        placeholder->SetBorderStyle(BorderStyle::Dashed);
//        placeholder->SetBorders(2.0f);
//        placeholder->SetPadding(20.0f);
//        container->AddChild(placeholder);
//
//        return container;
//    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateInfoGraphicsExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("InfoGraphicsExamples", 2400, 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("InfoGraphicsTitle", 2401, 10, 10, 300, 30);
        title->SetText("Info Graphics Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("InfoGraphicsPlaceholder", 2402, 20, 50, 800, 400);
        placeholder->SetText("Info Graphics Component - Not Implemented\n\nPlanned Widget Types:\n• Dashboard tiles and KPI widgets\n• Gauge and meter displays\n• Progress indicators and health meters\n• Statistical summary panels\n• Interactive data cards\n• Geographic data maps\n• Timeline visualizations\n• Comparison matrices\n\nAdvanced Features:\n• Real-time data updates\n• Responsive layout adaptation\n• Custom color schemes and branding\n• Animation and transition effects\n• Touch and gesture support\n• Export and sharing capabilities\n• Template library");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 200, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorders(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateFileLoaderExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("FileLoaderExamples", 1200, 0, 0, 1020, 780);

        auto text = std::make_shared<UltraCanvasMarkdownDisplay>("FileLoaderExamplesText", 1201, 10, 10, 1000, 750);
        text->SetMarkdownText("**FileLoader** is a comprehensive, cross-platform file handling system that eliminates the complexity of working with diverse file formats. Whether you're building desktop applications, content management systems, or media processing tools, FileLoader provides a single, unified API for loading, saving, and converting files across dozens of formats.\n"
                               "\n"
                               "-----------------\n"
                               "\n"
                               "**Purpose**\n"
                               "\n"
                               "Stop wrestling with dozens of different libraries and APIs for each file format. FileLoader gives you:\n"
                               "\n"
                               "-   **One API for Everything** - Load images, audio, video, documents, and 3D models through a single, consistent interface\n"
                               "-   **Effortless Format Conversion** - Convert between compatible formats with a single function call\n"
                               "-   **Built-in Security** - Automatic malware and virus scanning protects your users\n"
                               "-   **Cross-Platform Compatibility** - Same code works on Windows, Linux, and macOS\n"
                               "\n"
                               "**Supported Formats**\n"
                               "\n"
                               "**Images:** PNG, JPEG, WebP, AVIF, HEIC, GIF, BMP, TIFF, SVG, PSD, HDR, TGA, RAW formats\n"
                               "**Audio:** MP3, FLAC, WAV, OGG, AAC, M4A, OPUS, WMA\n"
                               "**Video:** MP4, AVI, MKV, WebM, MOV, FLV, WMV\n"
                               "**Documents:** PDF, DOCX, ODT, RTF, TXT, Markdown\n"
                               "**3D Models:** OBJ, FBX, GLTF, STL, 3DS, PLY, COLLADA\n"
                               "**Archives:** ZIP, RAR, 7Z, TAR, GZ, BZ2");
        MarkdownStyle style = MarkdownStyle::Default();
        style.fontSize = 12;
        text->SetStyle(style);
        container->AddChild(text);

        return container;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreatePixeLXExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("Examples", 1200, 0, 0, 1020, 780);

        auto text = std::make_shared<UltraCanvasMarkdownDisplay>("ExamplesText", 1201, 10, 10, 1000, 750);
        text->SetMarkdownText("###PixelFX -- Professional Bitmap Processing for UltraCanvas\n"
                              "-----------------------\n"
                              "\n"
                              "**Transform pixels into perfection with industrial-strength image processing**\n"
                              "\n"
                              "PixelFX is UltraCanvas's comprehensive bitmap manipulation and analytics engine, delivering professional-grade image processing capabilities to any application. Built on proven open-source foundations and wrapped in an intuitive API, PixelFX eliminates the complexity of bitmap operations while providing enterprise-level performance.\n"
                              "\n"
                              "**Core Capabilities**\n"
                              "\n"
                              "**300+ Operations Across 14 Categories:**\n"
                              "-   **Arithmetic & Mathematical** -- Pixel-perfect calculations, trigonometry, statistics, and complex number operations\n"
                              "-   **Color Space Mastery** -- Seamless conversions between RGB, sRGB, HSV, Lab, CMYK, XYZ, and specialized color spaces\n"
                              "-   **Format Flexibility** -- Cast, scale, and transform between any image format or bit depth\n"
                              "-   **Advanced Filtering** -- Convolution, edge detection, blur, sharpen, and custom kernel operations\n"
                              "-   **Histogram Intelligence** -- Analysis, equalization, matching, and adaptive processing\n"
                              "-   **Morphological Processing** -- Erosion, dilation, opening, closing, and rank filters\n"
                              "-   **Frequency Domain** -- FFT transforms and frequency-based filtering\n"
                              "-   **Geometric Transforms** -- Resize, rotate, affine, and perspective corrections\n"
                              "-   **Drawing Operations** -- Shapes, lines, text, and flood fill with anti-aliasing\n"
                              "-   **Statistical Analysis** -- Min/max detection, deviation, profiles, and measurements");
        MarkdownStyle style = MarkdownStyle::Default();
        style.fontSize = 12;
        text->SetStyle(style);
        container->AddChild(text);

        return container;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateQRCodeExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("QRCodeExamples", 1201, 0, 0, 1020, 780);

        auto text = std::make_shared<UltraCanvasMarkdownDisplay>("QRCodeExamplesText", 1201, 10, 10, 1000, 750);
        text->SetMarkdownText("###UltraCanvas QR Module -- Seamless Code Integration Across All Platforms\n"
                              "--------------------------\n"
                              "\n"
                              "**Scan. Generate. Integrate. Anywhere.**\n"
                              "\n"
                              "The **UltraCanvas QR Module** brings professional QR code capabilities directly into your cross-platform applications with zero platform-specific headaches. Whether you're building inventory systems, contactless payment solutions, event ticketing, or authentication workflows, this module delivers enterprise-grade QR functionality through UltraCanvas's signature unified API.\n"
                              "\n"
                              "What It Does\n"
                              "\n"
                              "**QR Code Generation**\n"
                              "\n"
                              "-   Create customized QR codes with configurable error correction levels\n"
                              "-   Support for URLs, vCards, WiFi credentials, plain text, and custom data formats\n"
                              "-   Real-time preview with adjustable size, margin, and styling options\n"
                              "-   Export to multiple formats: PNG, SVG, PDF, or direct canvas rendering\n"
                              "\n"
                              "**QR Code Scanning**\n"
                              "\n"
                              "-   High-speed camera integration with automatic focus and frame detection\n"
                              "-   Multi-code detection -- scan multiple QR codes simultaneously\n"
                              "-   Works with webcams, USB cameras, and built-in device cameras\n"
                              "-   Robust decoding even with damaged, rotated, or partially obscured codes");
        MarkdownStyle style = MarkdownStyle::Default();
        style.fontSize = 12;
        text->SetStyle(style);
        container->AddChild(text);

        return container;
    }

} // namespace UltraCanvas