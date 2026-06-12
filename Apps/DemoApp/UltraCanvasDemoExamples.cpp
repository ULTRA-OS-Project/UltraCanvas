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
        title->SetAutoResize(true);
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

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateVideoExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("VideoExamples", 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("VideoTitle", 10, 10, 300, 30);
        title->SetText("Video Player Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto placeholder = std::make_shared<UltraCanvasLabel>("VideoPlaceholder", 20, 50, 800, 400);
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

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateMarkdownExamples() {
//        auto container = std::make_shared<UltraCanvasContainer>("MarkdownExamples", 0, 0, 1020, 780);

        auto text = std::make_shared<UltraCanvasMarkdownDisplay>("MarkDownText", 0, 0, 1026, 785);
        text->SetMarkdownText(LoadFile(NormalizePath(GetResourcesDir() + "media/MarkdownExample.md")));
        MarkdownStyle style = MarkdownStyle::Default();
        style.fontSize = 12;
        text->SetStyle(style);
//        container->AddChild(text);

        return text;
    }

//    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateCodeEditorExamples() {
//        auto container = std::make_shared<UltraCanvasContainer>("CodeEditorExamples", 0, 0, 1000, 600);
//
//        auto title = std::make_shared<UltraCanvasLabel>("CodeEditorTitle", 10, 10, 300, 30);
//        title->SetText("Code Editor Examples");
//        title->SetFontSize(16);
//        title->SetFontWeight(FontWeight::Bold);
//        container->AddChild(title);
//
//        // Formula Editor (partially implemented)
//        auto formulaEditor = std::make_shared<UltraCanvasFormulaEditor>("FormulaEditor", 20, 50, 600, 100);
//        ProceduralFormula f;
//        f.formula = "SUM(A1:A10) + AVERAGE(B1:B10) * 2.5";
//        formulaEditor->SetFormula(f);
////        formulaEditor->SetBackgroundColor(Color(248, 248, 248, 255));
//        container->AddChild(formulaEditor);
//
//        auto formulaLabel = std::make_shared<UltraCanvasLabel>("FormulaLabel", 20, 160, 600, 20);
//        formulaLabel->SetText("Formula Editor (Partially Implemented) - Supports mathematical expressions");
//        formulaLabel->SetFontSize(12);
//        container->AddChild(formulaLabel);
//
//        auto placeholder = std::make_shared<UltraCanvasLabel>("CodeEditorPlaceholder", 20, 200, 800, 300);
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
        auto container = std::make_shared<UltraCanvasContainer>("AudioExamples", 0, 0, 1000, 600);

        auto title = std::make_shared<UltraCanvasLabel>("AudioTitle", 10, 10, 600, 30);
        title->SetText("Audio Player & Recorder");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto status = std::make_shared<UltraCanvasLabel>("AudioStatus", 10, 45, 980, 25);
        if (UltraCanvasAudioDevices::IsAvailable()) {
            status->SetText("Backend: " + UltraCanvasAudioDevices::GetBackendName() +
                            " — ready");
            status->SetTextColor(Color(40, 120, 40));
        } else {
            status->SetText("Audio backend not compiled in. "
                            "Rebuild with -DULTRACANVAS_ENABLE_AUDIO=ON.");
            status->SetTextColor(Color(180, 60, 60));
        }
        status->SetFontSize(11);
        container->AddChild(status);

        // ----- Playback section -----
        auto playbackLabel = std::make_shared<UltraCanvasLabel>("PlaybackLabel", 10, 90, 400, 22);
        playbackLabel->SetText("Playback");
        playbackLabel->SetFontWeight(FontWeight::Bold);
        playbackLabel->SetTextColor(Color(0, 100, 200));
        container->AddChild(playbackLabel);

        auto player = CreateAudioPlayer("DemoPlayer", 10, 120, 600, 56);
        container->AddChild(player);

        auto loadBtn = CreateButton("LoadAudio", 620, 130, 110, 36, "Open...");
        player->onFileOpened = [status](const std::string& path) {
            status->SetText("Loaded: " + path);
        };
        player->onOpenCancelled = [status]() {
            status->SetText("Open cancelled.");
        };
        auto playerWeak = std::weak_ptr<UltraCanvasAudioPlayerElement>(player);
        loadBtn->onClick = [playerWeak]() {
            if (auto p = playerWeak.lock()) p->ShowOpenDialog();
        };
        container->AddChild(loadBtn);

        // ----- Recording section -----
        auto recLabel = std::make_shared<UltraCanvasLabel>("RecLabel", 10, 200, 400, 22);
        recLabel->SetText("Recording");
        recLabel->SetFontWeight(FontWeight::Bold);
        recLabel->SetTextColor(Color(200, 60, 60));
        container->AddChild(recLabel);

        auto recorder = CreateAudioRecorder("DemoRecorder", 10, 230, 600, 72);
        recorder->onRecordStarted = [status]() { status->SetText("Recording..."); };
        recorder->onRecordStopped = [status]() { status->SetText("Recording stopped."); };
        container->AddChild(recorder);

        auto saveBtn = CreateButton("SaveRec", 620, 244, 110, 36, "Save WAV...");
        recorder->onSaved = [status](const std::string& path) {
            status->SetText("Saved to " + path);
        };
        recorder->onSaveCancelled = [status]() {
            status->SetText("Save cancelled or no recording available.");
        };
        auto recWeak = std::weak_ptr<UltraCanvasAudioRecorderElement>(recorder);
        saveBtn->onClick = [recWeak]() {
            if (auto r = recWeak.lock()) r->ShowSaveDialog();
        };
        container->AddChild(saveBtn);

        auto playRecBtn = CreateButton("PlayRec", 620, 286, 110, 36, "Play back");
        playRecBtn->onClick = [player, recorder, status]() {
            auto audio = recorder->TakeBuffer();
            if (audio && audio->IsValid()) {
                player->LoadFromAudio(audio);
                player->Play();
                status->SetText("Playing back recording.");
            } else {
                status->SetText("No recording captured yet.");
            }
        };
        container->AddChild(playRecBtn);

        return container;
    }

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
        auto container = std::make_shared<UltraCanvasContainer>("Examples", 0, 0, 1020, 780);

        auto text = std::make_shared<UltraCanvasTextArea>("ExamplesText", 10, 10, 1030, 800);
        text->SetText(LoadFile(filename));
        text->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
        text->SetReadOnly(true);
        text->SetWordWrap(true);
        text->SetCursorPosition(LineColumnIndex::INVALID);
        container->AddChild(text);

        return container;
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