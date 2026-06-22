// Apps/DemoApp/UltraCanvasDemoExamples.cpp
// Implementation of all component example creators
// Version: 1.0.1
// Last Modified: 2026-05-01
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Text/UltraCanvasMarkdown.h"
//#include "UltraCanvasButton3Sections.h"
#include "Plugins/Charts/UltraCanvasDivergingBarChart.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasAudioPlayerElement.h"
#include "UltraCanvasAudioRecorderElement.h"
#include "UltraCanvasAudioDevices.h"
#include "UltraCanvasButton.h"
#include <sstream>
#include <random>
#include <map>

namespace UltraCanvas {
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateVectorExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("VectorExamples", 0, 0, 1000, 600);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("VectorTitle", 10, 10, 300, 30);
        title->SetText("Vector Graphics Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // Drawing Surface
//        auto drawingSurface = std::make_shared<UltraCanvasDrawingSurface>("DrawingSurface", 20, 50, 600, 400);
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
//        auto toolsLabel = std::make_shared<UltraCanvasLabel>("VectorTools", 650, 70, 320, 200);
//        toolsLabel->SetText("Drawing Surface Features:\n• Vector primitives (lines, circles, rectangles)\n• Bezier curves and paths\n• Fill and stroke styling\n• Layer management\n• Undo/redo support\n• Selection and manipulation\n• Export to SVG/PNG");
//        toolsLabel->SetBackgroundColor(Color(240, 255, 240, 255));
////        toolsLabel->SetBorderStyle(BorderStyle::Solid);
//        toolsLabel->SetPadding(10.0f);
//        container->AddChild(toolsLabel);

        return container;
    }

// ===== NOT IMPLEMENTED PLACEHOLDERS =====

//    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateToolbarExamples() {
//        auto container = std::make_shared<UltraCanvasContainer>("ToolbarExamples", 0, 0, 1000, 600);
//
//        // Title
//        auto title = std::make_shared<UltraCanvasLabel>("ToolbarTitle", 10, 10, 300, 30);
//        title->SetText("Toolbar Examples");
//        title->SetFontSize(16);
//        title->SetFontWeight(FontWeight::Bold);
//        container->AddChild(title);
//
//        // Placeholder for toolbar implementation
//        auto placeholder = std::make_shared<UltraCanvasLabel>("ToolbarPlaceholder", 20, 50, 800, 400);
//        placeholder->SetText("Toolbar Component - Partially Implemented\n\nPlanned Features:\n• Horizontal and vertical toolbars\n• Icon buttons with tooltips\n• Separator elements\n• Dropdown menu buttons\n• Overflow handling\n• Customizable button groups\n• Ribbon-style layout\n• Drag and drop reordering");
//        placeholder->SetAlignment(TextAlignment::Left);
//        placeholder->SetBackgroundColor(Color(255, 255, 200, 100));
////        placeholder->SetBorderStyle(BorderStyle::Dashed);
//        placeholder->SetBorders(2.0f);
//        placeholder->SetPadding(20.0f);
//        container->AddChild(placeholder);
//
//        return container;
//    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTableViewExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("TableViewExamples", 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("TableViewTitle", 10, 10, 300, 30);
        title->SetText("Spreadsheet View Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("TableViewPlaceholder", 20, 50, 800, 400);
        placeholder->SetText("Table View Component - Not Implemented\n\nPlanned Features:\n• Data grid with rows and columns\n• Sortable column headers\n• Cell editing capabilities\n• Row selection (single/multiple)\n• Virtual scrolling for large datasets\n• Custom cell renderers\n• Column resizing and reordering\n• Filtering and search\n• Export to CSV/Excel");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 200, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorders(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    // CreateListViewExamples() moved to UltraCanvasListViewExamples.cpp

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateDialogExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("DialogExamples", 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("DialogTitle", 10, 10, 300, 30);
        title->SetText("Dialog Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("DialogPlaceholder", 20, 50, 800, 400);
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
        auto container = std::make_shared<UltraCanvasContainer>("BitmapExamples", 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("BitmapTitle", 10, 10, 300, 30);
        title->SetText("Bitmap Graphics Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("BitmapPlaceholder", 20, 50, 800, 400);
        placeholder->SetText(format + " Graphics - Partially Implemented yet\n\nSupported Formats:\n• PNG - Full support with transparency\n• JPEG - Standard image display\n\nFeatures:\n• Image scaling and cropping\n• Rotation and transformation\n• Filter effects\n• Multi-frame animation\n• Memory-efficient loading");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 255, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorders(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::Create3DExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("Graphics3DExamples", 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("Graphics3DTitle", 10, 10, 300, 30);
        title->SetText("3D Graphics Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("Graphics3DPlaceholder", 20, 50, 800, 400);
        placeholder->SetText("3D Graphics - Partially Implemented\n\nSupported Features:\n• 3DS model loading and display\n• 3DM model support\n• Basic scene graph\n• Camera controls (orbit, pan, zoom)\n• Basic lighting and shading\n• Texture mapping\n\nOpenGL/Vulkan Backend:\n• Hardware accelerated rendering\n• Vertex and fragment shaders\n• Multiple render targets\n• Anti-aliasing support");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 255, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorders(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    // CreateVideoExamples() now lives in UltraCanvasVideoExamples.cpp — a real
    // player + recorder demo backed by UltraCanvasVideoPlayerElement /
    // UltraCanvasVideoRecorderElement.

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTextDocumentExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("TextDocumentExamples", 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("TextDocumentTitle", 10, 10, 300, 30);
        title->SetText("Text Document Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("TextDocumentPlaceholder", 20, 50, 800, 400);
        placeholder->SetText("Text Document Support - Not Implemented\n\nPlanned Document Types:\n• ODT (OpenDocument Text)\n• RTF (Rich Text Format)\n• TeX/LaTeX documents\n• Plain text with syntax highlighting\n• HTML document rendering\n\nFeatures:\n• Document structure navigation\n• Text formatting display\n• Search and replace\n• Print preview\n• Export capabilities\n• Embedded images and tables");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 200, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorders(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

//    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateMarkdownExamples() {
////        auto container = std::make_shared<UltraCanvasContainer>("MarkdownExamples", 0, 0, 1020, 780);
//
//        auto text = std::make_shared<UltraCanvasMarkdownDisplay>("MarkDownText", 0, 0, 1026, 785);
//        text->SetMarkdownText(LoadFile(NormalizePath(GetResourcesDir() + "media/MarkdownExample.md")));
//        MarkdownStyle style = MarkdownStyle::Default();
//        style.fontSize = 12;
//        text->SetStyle(style);
////        container->AddChild(text);
//
//        return text;
//    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateDiagramExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("DiagramExamples", 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("DiagramTitle", 10, 10, 300, 30);
        title->SetText("Diagram Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("DiagramPlaceholder", 20, 50, 800, 400);
        placeholder->SetText("Diagram Support - Partially Implemented\n\nPlantUML Integration:\n• Class diagrams\n• Sequence diagrams\n• Activity diagrams\n• Use case diagrams\n• Component diagrams\n• State diagrams\n\nNative Diagram Engine:\n• Flowcharts\n• Organizational charts\n• Network diagrams\n• Mind maps\n• Interactive diagram editing\n• Export to SVG/PNG\n• Automatic layout algorithms");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 255, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorders(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateInfoGraphicsExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("InfoGraphicsExamples", 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("InfoGraphicsTitle", 10, 10, 300, 30);
        title->SetText("Info Graphics Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("InfoGraphicsPlaceholder", 20, 50, 800, 400);
        placeholder->SetText("Info Graphics Component - Not Implemented\n\nPlanned Widget Types:\n• Dashboard tiles and KPI widgets\n• Gauge and meter displays\n• Progress indicators and health meters\n• Statistical summary panels\n• Interactive data cards\n• Geographic data maps\n• Timeline visualizations\n• Comparison matrices\n\nAdvanced Features:\n• Real-time data updates\n• Responsive layout adaptation\n• Custom color schemes and branding\n• Animation and transition effects\n• Touch and gesture support\n• Export and sharing capabilities\n• Template library");
        placeholder->SetAlignment(TextAlignment::Left);
        placeholder->SetBackgroundColor(Color(255, 200, 200, 100));
//        placeholder->SetBorderStyle(BorderStyle::Dashed);
        placeholder->SetBorders(2.0f);
        placeholder->SetPadding(20.0f);
        container->AddChild(placeholder);

        return container;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateMarkdownDocScreen(const std::string& filename) {
        auto text = std::make_shared<UltraCanvasTextArea>("ExamplesText");
        text->size.width = CSSLayout::Dimension::Pct(100);
        text->size.height = CSSLayout::Dimension::Pct(100);
        text->SetText(LoadFile(filename));
        text->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
        text->SetReadOnly(true);
        text->SetWordWrap(true);
        text->SetCursorPosition(LineColumnIndex::INVALID);
        text->SetPadding(0,5,0,7);
        //text->SetBorders(3, Colors::Yellow);
        return text;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateModuleDocScreen(const std::string& moduleDir) {
        const std::string base   = NormalizePath(GetResourcesDir() + moduleDir + "/");
        // Diagram is named after the module (e.g. "UltraAI.svg"), living alongside the
        // module's intro.md / README.md in its Docs/Modules/<Module>/ folder.
        const std::string moduleName  = moduleDir.substr(moduleDir.find_last_of("/\\") + 1);
        const std::string introPath   = base + "intro.md";
        const std::string svgPath      = base + moduleName + ".svg";
        const std::string readmePath = base + "README.md";

        // Root is a vertical stack: intro (top) -> diagram (middle) -> docs (fills).
        auto root = std::make_shared<UltraCanvasContainer>("ModuleDocScreen");
        root->size.width  = CSSLayout::Dimension::Pct(100);
        root->size.height = CSSLayout::Dimension::Pct(100);
        root->layout.SetFlexColumn();

        // 1) Short intro (Markdown). Skipped when intro.md is missing/empty so the
        //    screen degrades to a plain README view.
        std::string introText = LoadFile(introPath);
        if (introText.find_first_not_of(" \t\r\n") != std::string::npos) {
            auto intro = std::make_shared<UltraCanvasTextArea>("ModuleIntro");
            intro->size.width  = CSSLayout::Dimension::Pct(100);
            intro->size.height = CSSLayout::Dimension::Px(170);
            intro->SetText(introText);
            intro->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
            intro->SetReadOnly(true);
            intro->SetWordWrap(true);
            intro->SetCursorPosition(LineColumnIndex::INVALID);
            intro->SetPadding(0, 5, 0, 7);
            // Fixed band at the top: don't grow or shrink.
            intro->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
            root->AddChild(intro);
        }

        // 2) Rendered SVG module diagram, centered. Only added when the file loads,
        //    so a not-yet-uploaded <Module>.svg simply leaves no empty box behind.
        auto svg = std::make_shared<UltraCanvasImageElement>("ModuleDiagram", 0, 0, 820, 300);
        if (svg->LoadFromFile(svgPath)) {
            svg->SetFitMode(ImageFitMode::Contain);   // preserve the diagram's aspect ratio
            auto diagramBox = std::make_shared<UltraCanvasContainer>("ModuleDiagramBox");
            diagramBox->size.width  = CSSLayout::Dimension::Pct(100);
            diagramBox->size.height = CSSLayout::Dimension::Px(320);
            diagramBox->SetBackgroundColor(Color(250, 250, 250, 255));
            diagramBox->layout.SetFlexRow();
            diagramBox->layout.SetFlexJustifyContent(CSSLayout::JustifyContent::Center);
            diagramBox->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
            diagramBox->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
            diagramBox->AddChild(svg);
            root->AddChild(diagramBox);
        } else {
            debugOutput << "Module diagram not loaded (" << svgPath
                        << "): " << svg->GetLastError() << std::endl;
        }

        // 3) Full documentation (existing behaviour): fills the remaining height and
        //    scrolls internally, exactly as the plain markdown screen does today.
        auto docs = std::make_shared<UltraCanvasTextArea>("ModuleDocs");
        docs->size.width  = CSSLayout::Dimension::Pct(100);
        docs->size.height = CSSLayout::Dimension::Pct(100);
        docs->SetText(LoadFile(readmePath));
        docs->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
        docs->SetReadOnly(true);
        docs->SetWordWrap(true);
        docs->SetCursorPosition(LineColumnIndex::INVALID);
        docs->SetPadding(0, 5, 0, 7);
        docs->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        root->AddChild(docs);

        return root;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateGPIOExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("GPIOExamples", 0, 0, 1020, 780);

        auto text = std::make_shared<UltraCanvasMarkdownDisplay>("GPIOExamplesText", 10, 10, 1000, 750);
        text->SetMarkdownText("**GPIO**");
        MarkdownStyle style = MarkdownStyle::Default();
        style.fontSize = 12;
        text->SetStyle(style);
        container->AddChild(text);

        return container;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreatePartiallyImplementedExamples(const std::string& descr) {
        auto container = std::make_shared<UltraCanvasContainer>("PartiallyImplementedExamples", 0, 0, 1020, 780);

        auto text = std::make_shared<UltraCanvasMarkdownDisplay>("PartiallyImplementedExamplesText", 10, 10, 1000, 750);
        text->SetMarkdownText(descr);
        MarkdownStyle style = MarkdownStyle::Default();
        style.fontSize = 12;
        text->SetStyle(style);
        container->AddChild(text);

        return container;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateScannerSupportExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("ScannerExamples", 0, 0, 1020, 780);

        auto text = std::make_shared<UltraCanvasMarkdownDisplay>("ScannerExamplesText", 10, 10, 1000, 750);
        text->SetMarkdownText("**Scanner support**");
        MarkdownStyle style = MarkdownStyle::Default();
        style.fontSize = 12;
        text->SetStyle(style);
        container->AddChild(text);

        return container;
    }

} // namespace UltraCanvas