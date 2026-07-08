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
#include <fstream>
#include <random>
#include <map>
#include <algorithm>
#include <filesystem>
#include <vector>
#include <set>

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

    namespace {
        // Language <-> bundled sample file. The language name must match a value
        // returned by UltraCanvasTextArea::GetSupportedLanguages() exactly, since
        // it is fed straight to ApplyCodeStyle(). The order here is the order the
        // "with a sample" tabs appear in — the ones we can actually show first.
        struct TextSample {
            const char* language;   // syntax-renderer language name
            const char* fileName;   // file under media/textsamples/
        };

        const std::vector<TextSample>& BundledTextSamples() {
            static const std::vector<TextSample> kSamples = {
                {"C++",          "sample.cpp"},
                {"C",            "sample.c"},
                {"Pascal",       "sample.pas"},
                {"Python",       "sample.py"},
                {"Markdown",     "sample.md"},
                {"Java",         "sample.java"},
                {"C#",           "sample.cs"},
                {"JavaScript",   "sample.js"},
                {"TypeScript",   "sample.ts"},
                {"Go",           "sample.go"},
                {"Rust",         "sample.rs"},
                {"Ruby",         "sample.rb"},
                {"PHP",          "sample.php"},
                {"Swift",        "sample.swift"},
                {"Kotlin",       "sample.kt"},
                {"Lua",          "sample.lua"},
                {"Shell Script", "sample.sh"},
                {"SQL",          "sample.sql"},
                {"JSON",         "sample.json"},
                {"XML",          "sample.xml"},
                {"HTML",         "sample.html"},
                {"CSS",          "sample.css"},
                {"YAML",         "sample.yaml"},
            };
            return kSamples;
        }
    } // anonymous namespace

    // One vertical tab page: the file's source rendered live in a read-only
    // UltraCanvasTextArea using the syntax renderer for `language`.
    std::shared_ptr<UltraCanvasUIElement>
    UltraCanvasDemoApplication::CreateTextSampleTabPage(const std::string& language,
                                                        const std::string& filePath) {
        const std::string fileName = std::filesystem::path(filePath).filename().string();

        auto page = std::make_shared<UltraCanvasContainer>("TextSamplePage_" + language, 0, 0, 800, 600);
        page->layout.SetFlexColumn().SetFlexGap(6)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        page->SetPadding(8, 10, 8, 10);
        page->SetBackgroundColor(Colors::White);

        auto header = std::make_shared<UltraCanvasLabel>("TextSampleHeader_" + language, 0, 0, 0, 20);
        header->SetText(language + " — " + fileName +
                        " (highlighted live by UltraCanvasTextArea's syntax renderer)");
        header->SetFontSize(12);
        header->SetFontWeight(FontWeight::Bold);
        header->SetTextColor(Color(40, 110, 40, 255));
        header->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        page->AddChild(header);

        auto codeView = std::make_shared<UltraCanvasTextArea>("TextSampleArea_" + language);
        codeView->ApplyCodeStyle(language);
        codeView->SetFontSize(11);
        codeView->SetReadOnly(true);
        codeView->SetShowLineNumbers(true);
        codeView->SetHighlightCurrentLine(true);
        codeView->SetWordWrap(false);
        codeView->SetText(LoadFile(filePath));
        codeView->SetCursorPosition(LineColumnIndex::INVALID);
        codeView->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                            .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        page->AddChild(codeView);

        return page;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTextDocumentExamples() {
        // Root: flex column filling the display area.
        auto root = std::make_shared<UltraCanvasContainer>("TextDocumentExamples", 0, 0, 1000, 700);
        root->layout.SetFlexColumn().SetFlexGap(8)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        root->SetPadding(10, 12, 10, 12);

        auto title = std::make_shared<UltraCanvasLabel>("TextDocumentTitle", 0, 0, 0, 28);
        title->SetText("Text Documents");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        title->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        root->AddChild(title);

        // Query the renderer for every file type it can highlight.
        auto probe = std::make_shared<UltraCanvasTextArea>("TextDocumentProbe", 0, 0, 10, 10);
        std::vector<std::string> languages = probe->GetSupportedLanguages();
        std::sort(languages.begin(), languages.end());

        const std::string samplesDir = NormalizePath(GetResourcesDir() + "media/textsamples/");

        // Figure out which bundled samples are actually present on disk.
        std::vector<TextSample> presentSamples;
        for (const auto& sample : BundledTextSamples()) {
            const std::string path = NormalizePath(samplesDir + sample.fileName);
            std::error_code ec;
            if (std::filesystem::exists(path, ec)) {
                presentSamples.push_back(sample);
            }
        }

        auto info = std::make_shared<UltraCanvasLabel>("TextDocumentInfo", 0, 0, 0, 34);
        info->SetText("UltraCanvasTextArea's built-in syntax renderer highlights " +
                      std::to_string(languages.size()) +
                      " plain-text and source-code file types. The tabs below open live "
                      "sample files from " + samplesDir + " — file types with a bundled "
                      "sample are listed first, the remaining supported types follow.");
        info->SetFontSize(11);
        info->SetTextColor(Color(110, 110, 110, 255));
        info->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        root->AddChild(info);

        // Vertical tab bar on the left: one tab per supported file type.
        auto tabs = std::make_shared<UltraCanvasTabbedContainer>("TextDocumentTabs", 0, 0, 0, 0);
        tabs->SetTabPosition(TabPosition::Left);
        tabs->SetTabStyle(TabStyle::Modern);
        tabs->SetCloseMode(TabCloseMode::NoClose);
        tabs->SetTabHeight(26);
        // Many file types — allow jumping to any of them through the overflow
        // dropdown when the tab column does not fit the window height.
        tabs->SetOverflowDropdownPosition(OverflowDropdownPosition::Right);
        tabs->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        // 1) File types we can actually show — a live TextArea per sample file.
        std::set<std::string> withSample;
        for (const auto& sample : presentSamples) {
            withSample.insert(sample.language);
            const std::string path = NormalizePath(samplesDir + sample.fileName);
            tabs->AddTab(sample.language, CreateTextSampleTabPage(sample.language, path));
        }

        // 2) Remaining supported file types, listed after the ones with a sample.
        for (const auto& lang : languages) {
            if (withSample.count(lang)) continue;

            auto page = std::make_shared<UltraCanvasContainer>("TextTypePage_" + lang, 0, 0, 800, 600);
            page->layout.SetFlexColumn()
                        .SetFlexJustifyContent(CSSLayout::JustifyContent::Center)
                        .SetFlexAlignItems(CSSLayout::AlignItems::Center);
            page->SetBackgroundColor(Colors::White);

            auto note = std::make_shared<UltraCanvasLabel>("TextTypeNote_" + lang, 0, 0, 0, 0);
            note->SetText(lang + "\n\nUltraCanvasTextArea highlights this file type — no bundled\n"
                                 "sample ships for it yet. Open any matching file to see it rendered.");
            note->SetFontSize(13);
            note->SetTextColor(Color(120, 120, 120, 255));
            note->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
            note->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
            page->AddChild(note);

            tabs->AddTab(lang, page);
        }

        // Open on the first sample tab so a live document greets the user.
        tabs->SetActiveTab(0);
        root->AddChild(tabs);

        return root;
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
        const std::string svgName     = moduleName + ".svg";
        const std::string svgPath     = base + svgName;
        const std::string readmePath  = base + "README.md";

        // The whole page is a single Markdown document rendered in one TextArea:
        //   intro.md  ->  ![diagram](<Module>.svg)  ->  README.md
        // The relative SVG path is resolved against the module folder (see
        // SetMarkdownBaseDirectory below), so clicking the rendered diagram opens it
        // zoomed in the TextArea's built-in fullscreen image viewer.
        std::string combined;

        // 1) Short intro (Markdown). Skipped when intro.md is missing/empty so the
        //    page degrades to a plain README view.
        std::string introText = LoadFile(introPath);
        if (introText.find_first_not_of(" \t\r\n") != std::string::npos) {
            combined += introText;
            combined += "\n\n";
        }

        // 2) Embed the module diagram as a Markdown image, but only when the SVG file
        //    actually exists, so a not-yet-uploaded <Module>.svg leaves no broken
        //    image behind. Existence is checked directly (LoadFile returns an error
        //    string rather than failing for a missing path).
        if (std::ifstream(svgPath).good()) {
            combined += "![" + moduleName + " architecture](" + svgName + ")\n\n";
        } else {
            debugOutput << "Module diagram not found: " << svgPath << std::endl;
        }

        // 3) Full documentation.
        combined += LoadFile(readmePath);

        auto text = std::make_shared<UltraCanvasTextArea>("ModuleDocs");
        text->size.width  = CSSLayout::Dimension::Pct(100);
        text->size.height = CSSLayout::Dimension::Pct(100);
        // Resolve the relative diagram path (and any other relative image in the docs)
        // against the module folder.
        text->SetMarkdownBaseDirectory(base);
        text->SetText(combined);
        text->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
        text->SetReadOnly(true);
        text->SetWordWrap(true);
        text->SetCursorPosition(LineColumnIndex::INVALID);
        text->SetPadding(0, 5, 0, 7);
        return text;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateUltraOSInfoScreen() {
        // Overview page shown when the "ULTRA OS modules" category itself is selected.
        // The whole page is a single Markdown view: the descriptive overview text with
        // the ULTRA-OS.svg diagram embedded at the bottom via a Markdown image tag.
        // The relative image path (media/diagrams/ULTRA-OS.svg) is resolved against the
        // markdown base directory set below, so the shared asset is not duplicated.
        const std::string readmePath = NormalizePath(GetResourcesDir() + "Docs/Modules/ULTRA-OS/README.md");

        auto root = std::make_shared<UltraCanvasContainer>("UltraOSInfoScreen");
        root->size.width  = CSSLayout::Dimension::Pct(100);
        root->size.height = CSSLayout::Dimension::Pct(100);
        root->layout.SetFlexColumn();

        auto docs = std::make_shared<UltraCanvasTextArea>("UltraOSDocs");
        docs->size.width  = CSSLayout::Dimension::Pct(100);
        docs->size.height = CSSLayout::Dimension::Pct(100);
        // Resolve relative markdown image paths against the resources dir (which holds
        // media/), so ![ULTRA OS](media/diagrams/ULTRA-OS.svg) renders the diagram.
        docs->SetMarkdownBaseDirectory(GetResourcesDir());
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