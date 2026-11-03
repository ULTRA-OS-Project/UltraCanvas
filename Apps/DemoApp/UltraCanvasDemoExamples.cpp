// Apps/DemoApp/UltraCanvasDemoExamples.cpp
// Implementation of all component example creators
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): the placeholder
//                  pages as cards of bullets, the document screens (module docs,
//                  ULTRA OS, Markdown) in a card filling the page area
// Version: 1.0.1
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8
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
        auto page = DemoKit::CreatePage("TableViewExamples", "Spreadsheet View Examples",
                "Table View Component - Not Implemented");
        DemoKit::AddNotes(page, "TableViewPlaceholder", "Planned Features", {
                "Data grid with rows and columns",
                "Sortable column headers",
                "Cell editing capabilities",
                "Row selection (single/multiple)",
                "Virtual scrolling for large datasets",
                "Custom cell renderers",
                "Column resizing and reordering",
                "Filtering and search",
                "Export to CSV/Excel"});
        return page;
    }

    // CreateListViewExamples() moved to UltraCanvasListViewExamples.cpp

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateDialogExamples() {
        auto page = DemoKit::CreatePage("DialogExamples", "Dialog Examples",
                "Dialog Component - Not Implemented");
        DemoKit::AddNotes(page, "DialogPlaceholder", "Planned Features", {
                "Modal and modeless dialogs",
                "Message boxes (Info, Warning, Error)",
                "File open/save dialogs",
                "Color picker dialogs",
                "Font selection dialogs",
                "Custom dialog layouts",
                "Dialog result handling",
                "Animation effects",
                "Keyboard shortcuts (ESC, Enter)"});
        return page;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateBitmapNotImplementedExamples(const std::string& format) {
        auto page = DemoKit::CreatePage("BitmapExamples", "Bitmap Graphics Examples",
                format + " Graphics - Partially Implemented yet");
        auto row = DemoKit::AddRow(page, "BitmapPlaceholder");
        DemoKit::AddNotes(row, "BitmapFormats", "Supported Formats", {
                "PNG - Full support with transparency",
                "JPEG - Standard image display"});
        DemoKit::AddNotes(row, "BitmapFeatures", "Features", {
                "Image scaling and cropping",
                "Rotation and transformation",
                "Filter effects",
                "Multi-frame animation",
                "Memory-efficient loading"});
        return page;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::Create3DExamples() {
        auto page = DemoKit::CreatePage("Graphics3DExamples", "3D Graphics Examples",
                "3D Graphics - Partially Implemented");
        auto row = DemoKit::AddRow(page, "Graphics3DPlaceholder");
        DemoKit::AddNotes(row, "Graphics3DFeatures", "Supported Features", {
                "3DS model loading and display",
                "3DM model support",
                "Basic scene graph",
                "Camera controls (orbit, pan, zoom)",
                "Basic lighting and shading",
                "Texture mapping"});
        DemoKit::AddNotes(row, "Graphics3DBackend", "OpenGL/Vulkan Backend", {
                "Hardware accelerated rendering",
                "Vertex and fragment shaders",
                "Multiple render targets",
                "Anti-aliasing support"});
        return page;
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
                {"R",            "sample.r"},
                {"Scala",        "sample.scala"},
                {"MATLAB",       "sample.m"},
                {"VBA",          "sample.vba"},
                {"x86 Assembly",   "sample.asm"},
                {"ARM Assembly",   "sample.arm"},
                {"68000 Assembly", "sample.68k"},
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
        const std::string fileName = PathToUtf8(PathFromUtf8(filePath).filename());

        auto page = std::make_shared<UltraCanvasContainer>("TextSamplePage_" + language, 0, 0, 800, 600);
        page->layout.SetFlexColumn().SetFlexGap(6)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        page->SetPadding(8, 10, 8, 10);
        page->SetBackgroundColor(Colors::White);

        // As tall as its wrapped lines: a long file name or translation wraps.
        auto header = DemoKit::CreateHeading("TextSampleHeader_" + language,
                language + " — " + fileName +
                " (highlighted live by UltraCanvasTextArea's syntax renderer)");
        header->SetFontSize(12);
        header->SetTextColor(Color(40, 110, 40, 255));
        DemoKit::Add(page, header);

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
            if (std::filesystem::exists(UltraCanvas::PathFromUtf8(path), ec)) {
                presentSamples.push_back(sample);
            }
        }

        auto root = DemoKit::CreatePage("TextDocumentExamples", "Text Documents",
                "UltraCanvasTextArea's built-in syntax renderer highlights " +
                std::to_string(languages.size()) +
                " plain-text and source-code file types. The tabs below open live "
                "sample files from " + samplesDir + " — file types with a bundled "
                "sample are listed first, the remaining supported types follow.");
        // The tab column and the documents scroll themselves: the page is the
        // area's height.
        root->SetFillsArea(true);

        auto card = DemoKit::CreateCard("TextDocumentCard");
        DemoKit::Add(root, card, 1);
        // The tab pages are built 800x600; the card may still be shorter.
        card->layoutItem.SetFlexShrink(1);

        // Vertical tab bar on the left: one tab per supported file type.
        auto tabs = std::make_shared<UltraCanvasTabbedContainer>("TextDocumentTabs", 0, 0, 0, 0);
        tabs->SetTabPosition(TabPosition::Left);
        tabs->SetTabStyle(TabStyle::Modern);
        tabs->SetCloseMode(TabCloseMode::NoClose);
        tabs->SetTabHeight(26);
        // Many file types — the tab column scrolls when it does not fit the
        // window height. The overflow dropdown is intentionally not enabled: it
        // is not supported for vertical (Left/Right) tab layouts.

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

            auto note = DemoKit::CreateText("TextTypeNote_" + lang,
                    lang + "\n\nUltraCanvasTextArea highlights this file type — no bundled\n"
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
        DemoKit::Add(card, tabs, 1);
        tabs->layoutItem.SetFlexShrink(1);

        return root;
    }

//    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateMarkdownExamples() {
////        auto container = std::make_shared<UltraCanvasContainer>("MarkdownExamples", 0, 0, 1020, 780);
//
//        auto text = std::make_shared<UltraCanvasMarkdownDisplay>("MarkDownText", 0, 0, 1026, 785);
//        text->SetMarkdownText(LoadFile(NormalizePath(GetResourcesDir() + "media/docs/MarkdownExample.md")));
//        MarkdownStyle style = MarkdownStyle::Default();
//        style.fontSize = 12;
//        text->SetStyle(style);
////        container->AddChild(text);
//
//        return text;
//    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateDiagramExamples() {
        auto page = DemoKit::CreatePage("DiagramExamples", "Diagram Examples",
                "Diagram Support - Partially Implemented");
        auto row = DemoKit::AddRow(page, "DiagramPlaceholder");
        DemoKit::AddNotes(row, "DiagramPlantUML", "PlantUML Integration", {
                "Class diagrams",
                "Sequence diagrams",
                "Activity diagrams",
                "Use case diagrams",
                "Component diagrams",
                "State diagrams"});
        DemoKit::AddNotes(row, "DiagramNative", "Native Diagram Engine", {
                "Flowcharts",
                "Organizational charts",
                "Network diagrams",
                "Mind maps",
                "Interactive diagram editing",
                "Export to SVG/PNG",
                "Automatic layout algorithms"});
        return page;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateInfoGraphicsExamples() {
        auto page = DemoKit::CreatePage("InfoGraphicsExamples", "Info Graphics Examples",
                "Info Graphics Component - Not Implemented");
        auto row = DemoKit::AddRow(page, "InfoGraphicsPlaceholder");
        DemoKit::AddNotes(row, "InfoGraphicsWidgets", "Planned Widget Types", {
                "Dashboard tiles and KPI widgets",
                "Gauge and meter displays",
                "Progress indicators and health meters",
                "Statistical summary panels",
                "Interactive data cards",
                "Geographic data maps",
                "Timeline visualizations",
                "Comparison matrices"});
        DemoKit::AddNotes(row, "InfoGraphicsAdvanced", "Advanced Features", {
                "Real-time data updates",
                "Responsive layout adaptation",
                "Custom color schemes and branding",
                "Animation and transition effects",
                "Touch and gesture support",
                "Export and sharing capabilities",
                "Template library"});
        return page;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateMarkdownDocScreen(const std::string& filename) {
        // Titled after the file, or after its folder for a module's README.md
        // (Docs/Modules/OCR/README.md -> "OCR").
        const auto filePath = PathFromUtf8(filename);
        std::string docTitle = PathToUtf8(filePath.stem());
        if (docTitle == "README" && filePath.has_parent_path()) {
            docTitle = PathToUtf8(filePath.parent_path().filename());
        }
        auto page = DemoKit::CreatePage("MarkdownDocPage", docTitle,
                PathToUtf8(filePath.filename()) + ", shown by UltraCanvasTextArea in Markdown mode");
        // The document scrolls itself: the page is the area's height.
        page->SetFillsArea(true);
        auto card = DemoKit::CreateCard("MarkdownDocCard");
        DemoKit::Add(page, card, 1);

        auto text = std::make_shared<UltraCanvasTextArea>("ExamplesText");
        text->SetText(LoadFile(filename));
        text->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
        text->SetReadOnly(true);
        text->SetWordWrap(true);
        text->SetCursorPosition(LineColumnIndex::INVALID);
        text->SetPadding(0,5,0,7);
        //text->SetBorders(3, Colors::Yellow);
        DemoKit::Add(card, text, 1);
        return page;
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
        if (std::ifstream(UltraCanvas::PathFromUtf8(svgPath)).good()) {
            combined += "![" + moduleName + " architecture](" + svgName + ")\n\n";
        } else {
            debugOutput << "Module diagram not found: " << svgPath << std::endl;
        }

        // 3) Full documentation.
        combined += LoadFile(readmePath);

        auto page = DemoKit::CreatePage("ModuleDocPage", moduleName,
                "The " + moduleName + " module: overview and documentation");
        // The document scrolls itself: the page is the area's height.
        page->SetFillsArea(true);
        auto card = DemoKit::CreateCard("ModuleDocCard");
        DemoKit::Add(page, card, 1);

        auto text = std::make_shared<UltraCanvasTextArea>("ModuleDocs");
        // Resolve the relative diagram path (and any other relative image in the docs)
        // against the module folder.
        text->SetMarkdownBaseDirectory(base);
        text->SetText(combined);
        text->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
        text->SetReadOnly(true);
        text->SetWordWrap(true);
        text->SetCursorPosition(LineColumnIndex::INVALID);
        text->SetPadding(0, 5, 0, 7);
        DemoKit::Add(card, text, 1);
        return page;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateUltraOSInfoScreen() {
        // Overview page shown when the "ULTRA OS modules" category itself is selected.
        // The whole page is a single Markdown view: the descriptive overview text with
        // the ULTRA-OS.svg diagram embedded at the bottom via a Markdown image tag.
        // The relative image path (media/diagrams/ULTRA-OS.svg) is resolved against the
        // markdown base directory set below, so the shared asset is not duplicated.
        const std::string readmePath = NormalizePath(GetResourcesDir() + "Docs/Modules/ULTRA-OS/README.md");

        auto root = DemoKit::CreatePage("UltraOSInfoScreen", "ULTRA OS",
                "The ULTRA OS modules: an overview and how they fit together");
        // The document scrolls itself: the page is the area's height.
        root->SetFillsArea(true);
        auto card = DemoKit::CreateCard("UltraOSDocsCard");
        DemoKit::Add(root, card, 1);

        auto docs = std::make_shared<UltraCanvasTextArea>("UltraOSDocs");
        // Resolve relative markdown image paths against the resources dir (which holds
        // media/), so ![ULTRA OS](media/diagrams/ULTRA-OS.svg) renders the diagram.
        docs->SetMarkdownBaseDirectory(GetResourcesDir());
        docs->SetText(LoadFile(readmePath));
        docs->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
        docs->SetReadOnly(true);
        docs->SetWordWrap(true);
        docs->SetCursorPosition(LineColumnIndex::INVALID);
        docs->SetPadding(0, 5, 0, 7);
        DemoKit::Add(card, docs, 1);

        return root;
    }

    namespace {
        // A page holding one Markdown view in a card. The view scrolls itself,
        // so the page is the area's height.
        std::shared_ptr<UltraCanvasUIElement> CreateMarkdownPage(const std::string& pageId,
                                                                 const std::string& title,
                                                                 const std::string& subtitle,
                                                                 const std::string& textId,
                                                                 const std::string& markdown) {
            auto page = DemoKit::CreatePage(pageId, title, subtitle);
            page->SetFillsArea(true);
            auto card = DemoKit::CreateCard(pageId + "Card");
            DemoKit::Add(page, card, 1);

            auto text = std::make_shared<UltraCanvasMarkdownDisplay>(textId, 0, 0, 0, 0);
            text->SetMarkdownText(markdown);
            MarkdownStyle style = MarkdownStyle::Default();
            style.fontSize = 12;
            text->SetStyle(style);
            DemoKit::Add(card, text, 1);
            return page;
        }
    } // anonymous namespace

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateGPIOExamples() {
        return CreateMarkdownPage("GPIOExamples", "GPIO", "General-purpose input/output support",
                                  "GPIOExamplesText", "**GPIO**");
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreatePartiallyImplementedExamples(const std::string& descr) {
        return CreateMarkdownPage("PartiallyImplementedExamples", "Partially Implemented",
                                  "This example is still being built",
                                  "PartiallyImplementedExamplesText", descr);
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateScannerSupportExamples() {
        return CreateMarkdownPage("ScannerExamples", "Scanner Support", "Image acquisition from scanners",
                                  "ScannerExamplesText", "**Scanner support**");
    }

} // namespace UltraCanvas