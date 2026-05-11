// Apps/DemoApp/UltraCanvasFlowChartExamples.cpp
// Interactive flow chart demo - tabbed: Order Example + rich diagram builder
// Version: 2.3.2
// Last Modified: 2026-05-02
// Author: UltraCanvas Framework
//
// Changelog:
//   2.3.2 - Killed the spurious vertical scrollbar:
//           Order Example content needed y up to 874 (toolbar at y=800
//           + checkboxes at y=850), but the tab content area was only
//           828 px tall (tabs_height 860 minus the 32 px tab strip).
//           That 46 px overflow forced the TabbedContainer to show a
//           scrollbar even though the visible content fit fine. Fix:
//           outer 900 -> 970, tabs 860 -> 930, content containers
//           860 -> 898 so each tab's available area (898 px) matches
//           the declared content height with no overflow.
//   2.3.1 - Numeric inputs commit-on-blur instead of every keystroke.
//   2.3.0 - Rich diagram builder with full node and connection editing:
//           NODE editing (when a node is selected):
//             * Shape dropdown (change shape after creation).
//             * Label TextInput (live edit).
//             * Width / Height numeric inputs.
//             * Fill / Border / Text color preset rows.
//             * Font size numeric input.
//           CONNECTION editing (when a connection is selected):
//             * Label TextInput.
//             * Style dropdown (Straight / Orthogonal / Curved).
//             * Arrow dropdown (Arrow / None / Diamond).
//             * Width numeric input.
//             * Color preset row.
//             * Branch radio (Yes / No / Custom) — visible only when the
//               connection's source node is a Decision/Diamond. Yes auto-
//               applies "Yes" label + green color, No applies "No" + red,
//               Custom keeps the existing user-chosen label/color.
//           STICKY NOTE: dedicated "Add Note" button on the toolbar; when
//             a sticky note is selected, the panel shows a multi-line
//             label input + size + fill color (border/text presets hidden
//             since sticky notes use their own style).
//   2.2.0 - Two-tab demo (Order Example + basic Builder).
//           Properties panel uses three sub-containers (nodeSection,
//             connectionSection, noteSection) with SetVisible toggling for
//             clean swap based on what's selected.
//   2.2.0 - Two-tab demo (Order Example + basic Builder).
//   2.1.1 - Instructions panel moved out of canvas.
//   2.1.0 - Strict grid layout fix.
//   2.0.0 - Initial Miro-style demo.

#include "UltraCanvasDemo.h"
#include "Plugins/Diagrams/UltraCanvasFlowChart.h"
#include "Plugins/Diagrams/UltraCanvasFlowChartPalette.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasDropdown.h"

namespace UltraCanvas {

// =============================================================================
// COLOR PRESETS shared by node-fill, node-border, connection, and sticky-note
// pickers. Each preset bundles fill/border/text so picking "Green" gives a
// coherent 3-color combo rather than three independent picks.
// =============================================================================
namespace {
struct ColorPreset {
    const char* name;
    Color fill;
    Color border;
    Color text;
};
const ColorPreset kColorPresets[] = {
    {"Green",  Color(200, 255, 200, 255), Color( 50, 180,  50, 255), Color(  0,  60,   0, 255)},
    {"Blue",   Color(200, 220, 255, 255), Color( 60, 100, 220, 255), Color(  0,   0,  80, 255)},
    {"Yellow", Color(255, 240, 200, 255), Color(220, 170,  60, 255), Color( 80,  50,   0, 255)},
    {"Orange", Color(255, 220, 200, 255), Color(220, 120,  80, 255), Color(100,  30,   0, 255)},
    {"Purple", Color(220, 200, 255, 255), Color(140,  80, 220, 255), Color( 60,   0, 100, 255)},
    {"Gray",   Color(230, 230, 230, 255), Color(140, 140, 140, 255), Color( 40,  40,  40, 255)}
};
constexpr size_t kNumPresets = sizeof(kColorPresets) / sizeof(kColorPresets[0]);

// Shape dropdown options (mirrors the palette's shape categories).
struct ShapeOption { const char* label; FlowChartShape shape; };
const ShapeOption kShapeOptions[] = {
    {"Rectangle",        FlowChartShape::Rectangle},
    {"Rounded Rect",     FlowChartShape::RoundedRectangle},
    {"Diamond",          FlowChartShape::Diamond},
    {"Oval",             FlowChartShape::Oval},
    {"Circle",           FlowChartShape::Circle},
    {"Parallelogram",    FlowChartShape::Parallelogram},
    {"Hexagon",          FlowChartShape::Hexagon},
    {"Triangle",         FlowChartShape::Triangle},
    {"Process",          FlowChartShape::Process},
    {"Decision",         FlowChartShape::Decision},
    {"Document",         FlowChartShape::Document},
    {"Database",         FlowChartShape::Database},
    {"Manual Input",     FlowChartShape::ManualInput},
    {"Sticky Note",      FlowChartShape::StickyNote}
};
constexpr size_t kNumShapeOptions = sizeof(kShapeOptions) / sizeof(kShapeOptions[0]);

int IndexForShape(FlowChartShape s) {
    for (size_t i = 0; i < kNumShapeOptions; ++i) {
        if (kShapeOptions[i].shape == s) return static_cast<int>(i);
    }
    return 0;
}

const ColorPreset* PresetForLineColor(const Color& c) {
    // Find the preset whose `border` color matches (lines reuse the border slot).
    for (size_t i = 0; i < kNumPresets; ++i) {
        if (kColorPresets[i].border.r == c.r &&
            kColorPresets[i].border.g == c.g &&
            kColorPresets[i].border.b == c.b) return &kColorPresets[i];
    }
    return nullptr;
}
}

// =============================================================================
// TAB 1: ORDER EXAMPLE - Pre-built chart, unchanged from 2.2.0
// =============================================================================
static std::shared_ptr<UltraCanvasContainer> BuildOrderExampleChart() {
    auto container = std::make_shared<UltraCanvasContainer>("OrderExample", 0, 0, 1400, 898);
    container->SetBackgroundColor(Color(245, 247, 250, 255));
    
    auto title = std::make_shared<UltraCanvasLabel>("Title", 30, 20, 1100, 35);
    title->SetText("Order Processing Flow Chart");
    title->SetFont("Arial", 22.0f, FontWeight::Bold);
    title->SetTextColor(Color(30, 30, 30, 255));
    container->AddChild(title);
    
    auto desc = std::make_shared<UltraCanvasLabel>("Desc", 30, 58, 1100, 22);
    desc->SetText("Pre-built example. Click Select to move/connect, drag nodes to reposition.");
    desc->SetFontSize(13);
    desc->SetTextColor(Color(100, 100, 100, 255));
    container->AddChild(desc);
    
    auto chart = CreateFlowChart("flowChart", 30, 95, 1130, 680);
    chart->SetBackgroundColor(Color(255, 255, 255, 255));
    chart->SetGridVisible(true, 20.0f);
    chart->SetGridColor(Color(235, 235, 235, 255));
    chart->SetSnapToGrid(true);
    
    chart->AddNode("start", FlowChartShape::RoundedRectangle, "Start", 60, 80, 140, 60);
    chart->SetNodeColor("start", Color(200, 255, 200, 255), Color(50, 180, 50, 255));
    chart->SetNodeTextColor("start", Color(0, 60, 0, 255));
    
    chart->AddNode("process1", FlowChartShape::Rectangle, "Process Order", 280, 80, 160, 60);
    chart->SetNodeColor("process1", Color(200, 220, 255, 255), Color(60, 100, 220, 255));
    chart->SetNodeTextColor("process1", Color(0, 0, 80, 255));
    
    chart->AddNode("decision", FlowChartShape::Diamond, "In Stock?", 520, 60, 140, 100);
    chart->SetNodeColor("decision", Color(255, 240, 200, 255), Color(220, 170, 60, 255));
    chart->SetNodeTextColor("decision", Color(80, 50, 0, 255));
    
    chart->AddNode("process3", FlowChartShape::Rectangle, "Backorder", 740, 80, 140, 60);
    chart->SetNodeColor("process3", Color(255, 220, 200, 255), Color(220, 120, 80, 255));
    chart->SetNodeTextColor("process3", Color(100, 30, 0, 255));
    
    chart->AddNode("process2", FlowChartShape::Rectangle, "Ship Item", 520, 240, 140, 60);
    chart->SetNodeColor("process2", Color(220, 200, 255, 255), Color(140, 80, 220, 255));
    chart->SetNodeTextColor("process2", Color(60, 0, 100, 255));
    
    chart->AddNode("end1", FlowChartShape::RoundedRectangle, "Complete", 520, 380, 140, 60);
    chart->SetNodeColor("end1", Color(200, 255, 200, 255), Color(50, 180, 50, 255));
    chart->SetNodeTextColor("end1", Color(0, 60, 0, 255));
    
    chart->AddNode("note1", FlowChartShape::StickyNote, "Check\ninventory\nfirst!", 760, 240, 100, 80);
    chart->SetNodeColor("note1", Color(255, 255, 200, 255), Color(220, 220, 120, 255));
    chart->SetNodeTextColor("note1", Color(80, 80, 0, 255));
    chart->SetNodeFontSize("note1", 10.0f);
    
    chart->AddConnection("c1", "start", "process1", FlowChartConnectionStyle::Orthogonal, FlowChartArrowStyle::Arrow);
    chart->SetConnectionColor("c1", Color(100, 100, 100, 255));
    chart->SetConnectionWidth("c1", 2.5f);
    
    chart->AddConnection("c2", "process1", "decision", FlowChartConnectionStyle::Orthogonal, FlowChartArrowStyle::Arrow);
    chart->SetConnectionColor("c2", Color(100, 100, 100, 255));
    chart->SetConnectionWidth("c2", 2.5f);
    
    chart->AddConnection("c3", "decision", "process2", FlowChartConnectionStyle::Orthogonal, FlowChartArrowStyle::Arrow);
    chart->SetConnectionLabel("c3", "Yes");
    chart->SetConnectionColor("c3", Color(50, 180, 50, 255));
    chart->SetConnectionWidth("c3", 2.5f);
    
    chart->AddConnection("c4", "process2", "end1", FlowChartConnectionStyle::Orthogonal, FlowChartArrowStyle::Arrow);
    chart->SetConnectionColor("c4", Color(100, 100, 100, 255));
    chart->SetConnectionWidth("c4", 2.5f);
    
    chart->AddConnection("c5", "decision", "process3", FlowChartConnectionStyle::Orthogonal, FlowChartArrowStyle::Arrow);
    chart->SetConnectionLabel("c5", "No");
    chart->SetConnectionColor("c5", Color(220, 120, 80, 255));
    chart->SetConnectionWidth("c5", 2.5f);
    
    chart->AddConnection("c6", "process3", "note1", FlowChartConnectionStyle::Straight, FlowChartArrowStyle::None);
    chart->SetConnectionLineStyle("c6", FlowChartLineStyle::Dotted);
    chart->SetConnectionColor("c6", Color(180, 180, 180, 255));
    chart->SetConnectionWidth("c6", 1.5f);
    
    container->AddChild(chart);
    
    int btnY = 800, btnX = 30, btnW = 110, btnH = 38, spacing = 15;
    
    auto btnSelect = std::make_shared<UltraCanvasButton>("btnSelect", btnX, btnY, btnW, btnH);
    btnSelect->SetText("Select");
    btnSelect->SetColors(Color(220, 235, 255, 255), Color(200, 225, 255, 255));
    btnSelect->SetOnClick([chart]() { chart->SetEditMode(UltraCanvasFlowChart::EditMode::Select); });
    container->AddChild(btnSelect);
    btnX += btnW + spacing;
    
    auto btnConnect = std::make_shared<UltraCanvasButton>("btnConnect", btnX, btnY, btnW, btnH);
    btnConnect->SetText("Connect");
    btnConnect->SetOnClick([chart]() { chart->SetEditMode(UltraCanvasFlowChart::EditMode::CreateConnection); });
    container->AddChild(btnConnect);
    btnX += btnW + spacing + 30;
    
    auto btnZoomIn = std::make_shared<UltraCanvasButton>("btnZoomIn", btnX, btnY, 90, btnH);
    btnZoomIn->SetText("Zoom +");
    btnZoomIn->SetOnClick([chart]() {
        float z = chart->GetZoomLevel() * 1.2f;
        if (z <= 3.0f) chart->SetZoomLevel(z);
    });
    container->AddChild(btnZoomIn);
    btnX += 90 + spacing;
    
    auto btnZoomOut = std::make_shared<UltraCanvasButton>("btnZoomOut", btnX, btnY, 90, btnH);
    btnZoomOut->SetText("Zoom -");
    btnZoomOut->SetOnClick([chart]() {
        float z = chart->GetZoomLevel() * 0.8f;
        if (z >= 0.3f) chart->SetZoomLevel(z);
    });
    container->AddChild(btnZoomOut);
    btnX += 90 + spacing;
    
    auto btnReset = std::make_shared<UltraCanvasButton>("btnReset", btnX, btnY, 100, btnH);
    btnReset->SetText("Reset View");
    btnReset->SetOnClick([chart]() { chart->SetZoomLevel(1.0f); chart->SetPanOffset(0, 0); });
    container->AddChild(btnReset);
    
    auto chkGrid = std::make_shared<UltraCanvasCheckbox>("chkGrid", 30, 850, 130, 24);
    chkGrid->SetText("Show Grid");
    chkGrid->SetChecked(true);
    chkGrid->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetGridVisible(newState == CheckedState::Checked, 20.0f);
    };
    container->AddChild(chkGrid);
    
    auto chkSnap = std::make_shared<UltraCanvasCheckbox>("chkSnap", 175, 850, 150, 24);
    chkSnap->SetText("Snap to Grid");
    chkSnap->SetChecked(true);
    chkSnap->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetSnapToGrid(newState == CheckedState::Checked);
    };
    container->AddChild(chkSnap);
    
    auto instrPanel = std::make_shared<UltraCanvasContainer>("instrPanel", 1170, 200, 220, 300);
    instrPanel->SetBackgroundColor(Color(255, 255, 255, 255));
    instrPanel->SetBorders(1.0f, Color(220, 220, 220, 255));
    
    auto instrTitle = std::make_shared<UltraCanvasLabel>("instrTitle", 10, 10, 210, 25);
    instrTitle->SetText("How to Use");
    instrTitle->SetFont("Arial", 14.0f, FontWeight::Bold);
    instrTitle->SetTextColor(Color(40, 40, 40, 255));
    instrPanel->AddChild(instrTitle);
    
    auto instr1 = std::make_shared<UltraCanvasLabel>("instr1", 10, 40, 210, 40);
    instr1->SetText("1. Click 'Select' then drag\n   nodes to reposition.");
    instr1->SetFontSize(11.0f);
    instr1->SetTextColor(Color(80, 80, 80, 255));
    instrPanel->AddChild(instr1);
    
    auto instr2 = std::make_shared<UltraCanvasLabel>("instr2", 10, 80, 210, 50);
    instr2->SetText("2. Click 'Connect' then\n   click two nodes to\n   connect them.");
    instr2->SetFontSize(11.0f);
    instr2->SetTextColor(Color(80, 80, 80, 255));
    instrPanel->AddChild(instr2);
    
    auto instr3 = std::make_shared<UltraCanvasLabel>("instr3", 10, 135, 210, 40);
    instr3->SetText("3. Press Delete to remove\n   the selected node.");
    instr3->SetFontSize(11.0f);
    instr3->SetTextColor(Color(80, 80, 80, 255));
    instrPanel->AddChild(instr3);
    
    auto instr4 = std::make_shared<UltraCanvasLabel>("instr4", 10, 180, 210, 40);
    instr4->SetText("4. Switch to 'Build Your Own'\n   tab to create from scratch.");
    instr4->SetFontSize(11.0f);
    instr4->SetTextColor(Color(80, 80, 80, 255));
    instrPanel->AddChild(instr4);
    
    container->AddChild(instrPanel);
    return container;
}

// =============================================================================
// TAB 2: BUILDER - Rich editor with full node and connection editing
// =============================================================================
//
// Layout (1400 x 860):
//   x=20..200     palette
//   x=210..1080   chart canvas (870 wide, 640 tall)
//   x=1090..1380  properties panel (290 wide, 660 tall)
//   y=730..860    toolbar + checkboxes
//
// Properties panel internals:
//   header (title + status)            y=10..60
//   nodeSection (toggled visible)      y=70..620   when selection is a node
//   connectionSection (toggled)        y=70..620   when selection is a conn
//   noteSection (toggled)              y=70..620   when selection is a note
//
static std::shared_ptr<UltraCanvasContainer> BuildBuilderChart() {
    auto container = std::make_shared<UltraCanvasContainer>("Builder", 0, 0, 1400, 898);
    container->SetBackgroundColor(Color(245, 247, 250, 255));
    
    // --- Title bar ---
    auto title = std::make_shared<UltraCanvasLabel>("BuilderTitle", 20, 15, 800, 30);
    title->SetText("Build Your Own Diagram");
    title->SetFont("Arial", 20.0f, FontWeight::Bold);
    title->SetTextColor(Color(30, 30, 30, 255));
    container->AddChild(title);
    
    auto desc = std::make_shared<UltraCanvasLabel>("BuilderDesc", 20, 47, 1000, 20);
    desc->SetText("Pick a shape, click on the canvas to place it. Click any node or connection to edit its properties.");
    desc->SetFontSize(12.0f);
    desc->SetTextColor(Color(100, 100, 100, 255));
    container->AddChild(desc);
    
    // --- Chart canvas (empty) ---
    auto chart = CreateFlowChart("builderChart", 210, 75, 870, 640);
    chart->SetBackgroundColor(Color(255, 255, 255, 255));
    chart->SetGridVisible(true, 20.0f);
    chart->SetGridColor(Color(235, 235, 235, 255));
    chart->SetSnapToGrid(true);
    container->AddChild(chart);
    
    // --- Palette on the left ---
    auto palette = CreateFlowChartPalette("builderPalette", 20, 75, 180, 640);
    palette->SetTargetDiagram(chart);
    palette->BuildPalette();
    container->AddChild(palette);
    
    // =========================================================================
    // PROPERTIES PANEL
    // =========================================================================
    auto propsPanel = std::make_shared<UltraCanvasContainer>("propsPanel", 1090, 75, 290, 660);
    propsPanel->SetBackgroundColor(Color(255, 255, 255, 255));
    propsPanel->SetBorders(1.0f, Color(220, 220, 220, 255));
    
    // Static header
    auto propsTitle = std::make_shared<UltraCanvasLabel>("propsTitle", 12, 10, 270, 25);
    propsTitle->SetText("Properties");
    propsTitle->SetFont("Arial", 14.0f, FontWeight::Bold);
    propsTitle->SetTextColor(Color(40, 40, 40, 255));
    propsPanel->AddChild(propsTitle);
    
    auto propsHint = std::make_shared<UltraCanvasLabel>("propsHint", 12, 38, 270, 30);
    propsHint->SetText("Select a node or connection.");
    propsHint->SetFontSize(11.0f);
    propsHint->SetTextColor(Color(140, 140, 140, 255));
    propsPanel->AddChild(propsHint);
    
    // -------------------------------------------------------------------------
    // NODE SECTION (visible only when a non-StickyNote node is selected)
    // -------------------------------------------------------------------------
    auto nodeSection = std::make_shared<UltraCanvasContainer>("nodeSection", 0, 70, 290, 580);
    nodeSection->SetBackgroundColor(Color(255, 255, 255, 0));  // transparent
    nodeSection->SetVisible(false);
    
    int ny = 0;
    auto lblShape = std::make_shared<UltraCanvasLabel>("lblShape", 12, ny, 270, 18);
    lblShape->SetText("Shape");
    lblShape->SetFontSize(11.0f);
    lblShape->SetTextColor(Color(80, 80, 80, 255));
    nodeSection->AddChild(lblShape);
    ny += 20;
    
    auto ddShape = std::make_shared<UltraCanvasDropdown>("ddShape", 12, ny, 266, 26);
    for (size_t i = 0; i < kNumShapeOptions; ++i) ddShape->AddItem(kShapeOptions[i].label);
    nodeSection->AddChild(ddShape);
    ny += 34;
    
    auto lblLabel = std::make_shared<UltraCanvasLabel>("lblLabel", 12, ny, 270, 18);
    lblLabel->SetText("Label");
    lblLabel->SetFontSize(11.0f);
    lblLabel->SetTextColor(Color(80, 80, 80, 255));
    nodeSection->AddChild(lblLabel);
    ny += 20;
    
    auto inLabel = std::make_shared<UltraCanvasTextInput>("inLabel", 12, ny, 266, 26);
    inLabel->SetPlaceholder("Node label");
    inLabel->SetShowValidationState(false);
    nodeSection->AddChild(inLabel);
    ny += 34;
    
    // Width / Height side by side
    auto lblW = std::make_shared<UltraCanvasLabel>("lblW", 12, ny, 80, 18);
    lblW->SetText("Width");
    lblW->SetFontSize(11.0f);
    lblW->SetTextColor(Color(80, 80, 80, 255));
    nodeSection->AddChild(lblW);
    
    auto lblH = std::make_shared<UltraCanvasLabel>("lblH", 150, ny, 80, 18);
    lblH->SetText("Height");
    lblH->SetFontSize(11.0f);
    lblH->SetTextColor(Color(80, 80, 80, 255));
    nodeSection->AddChild(lblH);
    ny += 20;
    
    auto inW = std::make_shared<UltraCanvasTextInput>("inW", 12, ny, 126, 26);
    inW->SetInputType(TextInputType::Integer);
    inW->SetShowValidationState(false);
    nodeSection->AddChild(inW);
    
    auto inH = std::make_shared<UltraCanvasTextInput>("inH", 150, ny, 128, 26);
    inH->SetInputType(TextInputType::Integer);
    inH->SetShowValidationState(false);
    nodeSection->AddChild(inH);
    ny += 34;
    
    // Helper to build a row of color preset buttons. Returns the buttons so
    // callers can wire up clicks afterward.
    auto buildColorRow = [&nodeSection](int y, const std::string& heading, int idBase) {
        auto lbl = std::make_shared<UltraCanvasLabel>("lbl_" + heading, 12, y, 270, 18);
        lbl->SetText(heading);
        lbl->SetFontSize(11.0f);
        lbl->SetTextColor(Color(80, 80, 80, 255));
        nodeSection->AddChild(lbl);
        
        std::vector<std::shared_ptr<UltraCanvasButton>> btns;
        btns.reserve(kNumPresets);
        // 6 swatches in one row, 42 wide x 22 tall, gap 2.
        int sw = 42, sh = 22;
        for (size_t i = 0; i < kNumPresets; ++i) {
            int sx = 12 + static_cast<int>(i) * (sw + 2);
            auto b = std::make_shared<UltraCanvasButton>(
                "preset_" + std::to_string(idBase) + "_" + std::to_string(i),
                sx, y + 20, sw, sh);
            b->SetText("");
            b->SetColors(kColorPresets[i].fill, kColorPresets[i].fill);
            b->SetBorder(1.0f, kColorPresets[i].border);
            b->SetCornerRadius(3.0f);
            nodeSection->AddChild(b);
            btns.push_back(b);
        }
        return btns;
    };
    
    auto fillBtns = buildColorRow(ny, "Fill",   7320); ny += 46;
    auto borderBtns = buildColorRow(ny, "Border", 7330); ny += 46;
    auto textBtns = buildColorRow(ny, "Text",   7340); ny += 46;
    
    auto lblFont = std::make_shared<UltraCanvasLabel>("lblFont", 12, ny, 270, 18);
    lblFont->SetText("Font size");
    lblFont->SetFontSize(11.0f);
    lblFont->SetTextColor(Color(80, 80, 80, 255));
    nodeSection->AddChild(lblFont);
    ny += 20;
    
    auto inFont = std::make_shared<UltraCanvasTextInput>("inFont", 12, ny, 126, 26);
    inFont->SetInputType(TextInputType::Integer);
    inFont->SetShowValidationState(false);
    nodeSection->AddChild(inFont);
    
    propsPanel->AddChild(nodeSection);
    
    // -------------------------------------------------------------------------
    // CONNECTION SECTION
    // -------------------------------------------------------------------------
    auto connSection = std::make_shared<UltraCanvasContainer>("connSection", 0, 70, 290, 580);
    connSection->SetBackgroundColor(Color(255, 255, 255, 0));
    connSection->SetVisible(false);
    
    int cy = 0;
    auto lblCLabel = std::make_shared<UltraCanvasLabel>("lblCLabel", 12, cy, 270, 18);
    lblCLabel->SetText("Label");
    lblCLabel->SetFontSize(11.0f);
    lblCLabel->SetTextColor(Color(80, 80, 80, 255));
    connSection->AddChild(lblCLabel);
    cy += 20;
    
    auto inCLabel = std::make_shared<UltraCanvasTextInput>("inCLabel", 12, cy, 266, 26);
    inCLabel->SetPlaceholder("(no label)");
    inCLabel->SetShowValidationState(false);
    connSection->AddChild(inCLabel);
    cy += 34;
    
    auto lblStyle = std::make_shared<UltraCanvasLabel>("lblStyle", 12, cy, 270, 18);
    lblStyle->SetText("Line style");
    lblStyle->SetFontSize(11.0f);
    lblStyle->SetTextColor(Color(80, 80, 80, 255));
    connSection->AddChild(lblStyle);
    cy += 20;
    
    auto ddStyle = std::make_shared<UltraCanvasDropdown>("ddStyle", 12, cy, 266, 26);
    ddStyle->AddItem("Straight");
    ddStyle->AddItem("Orthogonal");
    ddStyle->AddItem("Curved");
    connSection->AddChild(ddStyle);
    cy += 34;
    
    auto lblArrow = std::make_shared<UltraCanvasLabel>("lblArrow", 12, cy, 270, 18);
    lblArrow->SetText("Arrow");
    lblArrow->SetFontSize(11.0f);
    lblArrow->SetTextColor(Color(80, 80, 80, 255));
    connSection->AddChild(lblArrow);
    cy += 20;
    
    auto ddArrow = std::make_shared<UltraCanvasDropdown>("ddArrow", 12, cy, 266, 26);
    ddArrow->AddItem("None");
    ddArrow->AddItem("Arrow");
    ddArrow->AddItem("Arrow Filled");
    ddArrow->AddItem("Diamond");
    connSection->AddChild(ddArrow);
    cy += 34;
    
    auto lblCWidth = std::make_shared<UltraCanvasLabel>("lblCWidth", 12, cy, 270, 18);
    lblCWidth->SetText("Width (1-6)");
    lblCWidth->SetFontSize(11.0f);
    lblCWidth->SetTextColor(Color(80, 80, 80, 255));
    connSection->AddChild(lblCWidth);
    cy += 20;
    
    auto inCWidth = std::make_shared<UltraCanvasTextInput>("inCWidth", 12, cy, 126, 26);
    inCWidth->SetInputType(TextInputType::Integer);
    inCWidth->SetShowValidationState(false);
    connSection->AddChild(inCWidth);
    cy += 34;
    
    auto lblCColor = std::make_shared<UltraCanvasLabel>("lblCColor", 12, cy, 270, 18);
    lblCColor->SetText("Color");
    lblCColor->SetFontSize(11.0f);
    lblCColor->SetTextColor(Color(80, 80, 80, 255));
    connSection->AddChild(lblCColor);
    cy += 20;
    
    std::vector<std::shared_ptr<UltraCanvasButton>> connColorBtns;
    connColorBtns.reserve(kNumPresets);
    int sw = 42, sh = 22;
    for (size_t i = 0; i < kNumPresets; ++i) {
        int sx = 12 + static_cast<int>(i) * (sw + 2);
        auto b = std::make_shared<UltraCanvasButton>(
            "ccolor_" + std::to_string(i),
            sx, cy, sw, sh);
        b->SetText("");
        b->SetColors(kColorPresets[i].fill, kColorPresets[i].fill);
        b->SetBorder(1.0f, kColorPresets[i].border);
        b->SetCornerRadius(3.0f);
        connSection->AddChild(b);
        connColorBtns.push_back(b);
    }
    cy += 30;
    
    // Branch row: visible only when source of selected connection is Decision
    auto lblBranch = std::make_shared<UltraCanvasLabel>("lblBranch", 12, cy, 270, 18);
    lblBranch->SetText("Decision branch");
    lblBranch->SetFontSize(11.0f);
    lblBranch->SetTextColor(Color(80, 80, 80, 255));
    lblBranch->SetVisible(false);
    connSection->AddChild(lblBranch);
    cy += 20;
    
    auto btnYes = std::make_shared<UltraCanvasButton>("btnYes", 12, cy, 80, 26);
    btnYes->SetText("Yes");
    btnYes->SetColors(kColorPresets[0].fill, kColorPresets[0].fill);  // green
    btnYes->SetBorder(1.0f, kColorPresets[0].border);
    btnYes->SetCornerRadius(3.0f);
    btnYes->SetVisible(false);
    connSection->AddChild(btnYes);
    
    auto btnNo = std::make_shared<UltraCanvasButton>("btnNo", 98, cy, 80, 26);
    btnNo->SetText("No");
    btnNo->SetColors(kColorPresets[3].fill, kColorPresets[3].fill);  // orange/red
    btnNo->SetBorder(1.0f, kColorPresets[3].border);
    btnNo->SetCornerRadius(3.0f);
    btnNo->SetVisible(false);
    connSection->AddChild(btnNo);
    
    auto btnCustom = std::make_shared<UltraCanvasButton>("btnCustom", 184, cy, 92, 26);
    btnCustom->SetText("Custom");
    btnCustom->SetColors(Color(240, 240, 240, 255), Color(220, 220, 220, 255));
    btnCustom->SetBorder(1.0f, Color(180, 180, 180, 255));
    btnCustom->SetCornerRadius(3.0f);
    btnCustom->SetVisible(false);
    connSection->AddChild(btnCustom);
    
    propsPanel->AddChild(connSection);
    
    // -------------------------------------------------------------------------
    // STICKY NOTE SECTION (selected node has shape == StickyNote)
    // -------------------------------------------------------------------------
    auto noteSection = std::make_shared<UltraCanvasContainer>("noteSection", 0, 70, 290, 580);
    noteSection->SetBackgroundColor(Color(255, 255, 255, 0));
    noteSection->SetVisible(false);
    
    int sy = 0;
    auto lblNText = std::make_shared<UltraCanvasLabel>("lblNText", 12, sy, 270, 18);
    lblNText->SetText("Note text");
    lblNText->SetFontSize(11.0f);
    lblNText->SetTextColor(Color(80, 80, 80, 255));
    noteSection->AddChild(lblNText);
    sy += 20;
    
    // Sticky note text uses the same single-line input — multi-line authoring
    // works by typing literal "\n" in the text or by future multi-line widget.
    // For now: the user's text replaces the whole label.
    auto inNText = std::make_shared<UltraCanvasTextInput>("inNText", 12, sy, 266, 26);
    inNText->SetPlaceholder("Note text");
    inNText->SetShowValidationState(false);
    noteSection->AddChild(inNText);
    sy += 34;
    
    auto lblNW = std::make_shared<UltraCanvasLabel>("lblNW", 12, sy, 80, 18);
    lblNW->SetText("Width");
    lblNW->SetFontSize(11.0f);
    lblNW->SetTextColor(Color(80, 80, 80, 255));
    noteSection->AddChild(lblNW);
    
    auto lblNH = std::make_shared<UltraCanvasLabel>("lblNH", 150, sy, 80, 18);
    lblNH->SetText("Height");
    lblNH->SetFontSize(11.0f);
    lblNH->SetTextColor(Color(80, 80, 80, 255));
    noteSection->AddChild(lblNH);
    sy += 20;
    
    auto inNW = std::make_shared<UltraCanvasTextInput>("inNW", 12, sy, 126, 26);
    inNW->SetInputType(TextInputType::Integer);
    inNW->SetShowValidationState(false);
    noteSection->AddChild(inNW);
    
    auto inNH = std::make_shared<UltraCanvasTextInput>("inNH", 150, sy, 128, 26);
    inNH->SetInputType(TextInputType::Integer);
    inNH->SetShowValidationState(false);
    noteSection->AddChild(inNH);
    sy += 34;
    
    auto lblNFill = std::make_shared<UltraCanvasLabel>("lblNFill", 12, sy, 270, 18);
    lblNFill->SetText("Fill color");
    lblNFill->SetFontSize(11.0f);
    lblNFill->SetTextColor(Color(80, 80, 80, 255));
    noteSection->AddChild(lblNFill);
    sy += 20;
    
    std::vector<std::shared_ptr<UltraCanvasButton>> noteFillBtns;
    noteFillBtns.reserve(kNumPresets);
    for (size_t i = 0; i < kNumPresets; ++i) {
        int nx = 12 + static_cast<int>(i) * (sw + 2);
        auto b = std::make_shared<UltraCanvasButton>(
            "nfill_" + std::to_string(i), 
            nx, sy, sw, sh);
        b->SetText("");
        b->SetColors(kColorPresets[i].fill, kColorPresets[i].fill);
        b->SetBorder(1.0f, kColorPresets[i].border);
        b->SetCornerRadius(3.0f);
        noteSection->AddChild(b);
        noteFillBtns.push_back(b);
    }
    
    propsPanel->AddChild(noteSection);
    container->AddChild(propsPanel);
    
    // =========================================================================
    // STATE & WIRING
    // =========================================================================
    // Mutable shared state captured by lambdas.
    auto selectedNodeId = std::make_shared<std::string>();
    auto selectedConnId = std::make_shared<std::string>();
    // Guard against feedback loops: when we programmatically SetText/SetSelectedIndex
    // on a control to reflect the chart's state, the change-callback fires and
    // would write back to the chart, possibly stomping the same value or
    // triggering re-entrant updates. We toggle this flag during programmatic
    // updates so callbacks know to noop.
    auto suppressEvents = std::make_shared<bool>(false);
    
    auto showNoSelection = [propsHint, nodeSection, connSection, noteSection,
                            selectedNodeId, selectedConnId]() {
        selectedNodeId->clear();
        selectedConnId->clear();
        propsHint->SetText("Select a node or connection.");
        propsHint->SetTextColor(Color(140, 140, 140, 255));
        nodeSection->SetVisible(false);
        connSection->SetVisible(false);
        noteSection->SetVisible(false);
    };
    
    // Helper: load a node's current state into the editor controls.
    auto loadNodeIntoEditor = [chart, propsHint, nodeSection, noteSection, connSection,
                                ddShape, inLabel, inW, inH, inFont, inNText, inNW, inNH,
                                suppressEvents](const std::string& id) {
        const FlowChartNode* node = chart->GetNode(id);
        if (!node) return;
        *suppressEvents = true;
        bool isNote = (node->shape == FlowChartShape::StickyNote);
        if (isNote) {
            propsHint->SetText("Editing note: " + id);
            propsHint->SetTextColor(Color(180, 140, 0, 255));
            inNText->SetText(node->label);
            inNW->SetText(std::to_string(static_cast<int>(node->width)));
            inNH->SetText(std::to_string(static_cast<int>(node->height)));
            nodeSection->SetVisible(false);
            connSection->SetVisible(false);
            noteSection->SetVisible(true);
        } else {
            propsHint->SetText("Editing node: " + id);
            propsHint->SetTextColor(Color(60, 100, 200, 255));
            ddShape->SetSelectedIndex(IndexForShape(node->shape));
            inLabel->SetText(node->label);
            inW->SetText(std::to_string(static_cast<int>(node->width)));
            inH->SetText(std::to_string(static_cast<int>(node->height)));
            inFont->SetText(std::to_string(static_cast<int>(node->fontSize)));
            nodeSection->SetVisible(true);
            connSection->SetVisible(false);
            noteSection->SetVisible(false);
        }
        *suppressEvents = false;
    };
    
    // Helper: show/hide the Decision-branch row depending on whether the
    // connection's source node is a Decision/Diamond.
    auto updateBranchRow = [chart, lblBranch, btnYes, btnNo, btnCustom](const FlowChartConnection* conn) {
        if (!conn) {
            lblBranch->SetVisible(false);
            btnYes->SetVisible(false);
            btnNo->SetVisible(false);
            btnCustom->SetVisible(false);
            return;
        }
        const FlowChartNode* src = chart->GetNode(conn->sourceNodeId);
        bool isDecision = src && (src->shape == FlowChartShape::Decision ||
                                   src->shape == FlowChartShape::Diamond);
        lblBranch->SetVisible(isDecision);
        btnYes->SetVisible(isDecision);
        btnNo->SetVisible(isDecision);
        btnCustom->SetVisible(isDecision);
    };
    
    auto loadConnIntoEditor = [chart, propsHint, nodeSection, connSection, noteSection,
                                inCLabel, ddStyle, ddArrow, inCWidth, updateBranchRow,
                                suppressEvents](const std::string& id) {
        FlowChartConnection* conn = chart->GetConnection(id);
        if (!conn) return;
        *suppressEvents = true;
        propsHint->SetText("Editing connection: " + id);
        propsHint->SetTextColor(Color(60, 140, 80, 255));
        inCLabel->SetText(conn->label);
        ddStyle->SetSelectedIndex(static_cast<int>(conn->style));
        ddArrow->SetSelectedIndex(static_cast<int>(conn->arrowStyle));
        inCWidth->SetText(std::to_string(static_cast<int>(conn->lineWidth + 0.5f)));
        updateBranchRow(conn);
        nodeSection->SetVisible(false);
        connSection->SetVisible(true);
        noteSection->SetVisible(false);
        *suppressEvents = false;
    };
    
    // -------------------------------------------------------------------------
    // CHART CALLBACKS
    // -------------------------------------------------------------------------
    chart->onNodeSelected = [selectedNodeId, selectedConnId, loadNodeIntoEditor](const std::string& id) {
        *selectedNodeId = id;
        selectedConnId->clear();
        loadNodeIntoEditor(id);
    };
    
    chart->onConnectionClick = [selectedNodeId, selectedConnId, loadConnIntoEditor](const std::string& id) {
        *selectedConnId = id;
        selectedNodeId->clear();
        loadConnIntoEditor(id);
    };
    
    // -------------------------------------------------------------------------
    // NODE EDITOR CALLBACKS
    // -------------------------------------------------------------------------
    ddShape->onSelectionChanged = [chart, selectedNodeId, suppressEvents](int idx, const DropdownItem&) {
        if (*suppressEvents || selectedNodeId->empty()) return;
        if (idx < 0 || idx >= static_cast<int>(kNumShapeOptions)) return;
        chart->SetNodeShape(*selectedNodeId, kShapeOptions[idx].shape);
    };
    
    inLabel->onTextChanged = [chart, selectedNodeId, suppressEvents](const std::string& s) {
        if (*suppressEvents || selectedNodeId->empty()) return;
        chart->UpdateNodeLabel(*selectedNodeId, s);
    };
    
    auto parseDimension = [](const std::string& s, int defVal, int minVal, int maxVal) {
        if (s.empty()) return defVal;
        try {
            int v = std::stoi(s);
            if (v < minVal) return minVal;
            if (v > maxVal) return maxVal;
            return v;
        } catch (...) { return defVal; }
    };
    
    auto applyNodeSize = [chart, selectedNodeId, inW, inH, parseDimension, suppressEvents]() {
        if (selectedNodeId->empty()) return;
        const FlowChartNode* n = chart->GetNode(*selectedNodeId);
        if (!n) return;
        int w = parseDimension(inW->GetText(), static_cast<int>(n->width), 40, 400);
        int h = parseDimension(inH->GetText(), static_cast<int>(n->height), 30, 300);
        // No-op if the user typed the same value (or invalid input parsed
        // back to the current size).
        if (w == static_cast<int>(n->width) && h == static_cast<int>(n->height)) {
            // Still write back so "abc" or "9999" becomes the real value visually.
            *suppressEvents = true;
            inW->SetText(std::to_string(static_cast<int>(n->width)));
            inH->SetText(std::to_string(static_cast<int>(n->height)));
            *suppressEvents = false;
            return;
        }
        chart->SetNodeSize(*selectedNodeId, static_cast<float>(w), static_cast<float>(h));
    };
    inW->onFocusLost = applyNodeSize;
    inH->onFocusLost = applyNodeSize;
    
    inFont->onFocusLost = [chart, selectedNodeId, inFont, parseDimension, suppressEvents]() {
        if (selectedNodeId->empty()) return;
        const FlowChartNode* n = chart->GetNode(*selectedNodeId);
        if (!n) return;
        int fs = parseDimension(inFont->GetText(), static_cast<int>(n->fontSize), 6, 36);
        chart->SetNodeFontSize(*selectedNodeId, static_cast<float>(fs));
        // Write back the clamped/validated value so invalid input is corrected.
        *suppressEvents = true;
        inFont->SetText(std::to_string(fs));
        *suppressEvents = false;
    };
    
    // Color preset wiring: each row applies fill / border / text independently.
    for (size_t i = 0; i < kNumPresets; ++i) {
        ColorPreset p = kColorPresets[i];
        fillBtns[i]->SetOnClick([chart, selectedNodeId, p]() {
            if (selectedNodeId->empty()) return;
            const FlowChartNode* n = chart->GetNode(*selectedNodeId);
            if (!n) return;
            chart->SetNodeColor(*selectedNodeId, p.fill, n->borderColor);
        });
        borderBtns[i]->SetOnClick([chart, selectedNodeId, p]() {
            if (selectedNodeId->empty()) return;
            const FlowChartNode* n = chart->GetNode(*selectedNodeId);
            if (!n) return;
            chart->SetNodeColor(*selectedNodeId, n->fillColor, p.border);
        });
        textBtns[i]->SetOnClick([chart, selectedNodeId, p]() {
            if (selectedNodeId->empty()) return;
            chart->SetNodeTextColor(*selectedNodeId, p.text);
        });
    }
    
    // -------------------------------------------------------------------------
    // CONNECTION EDITOR CALLBACKS
    // -------------------------------------------------------------------------
    inCLabel->onTextChanged = [chart, selectedConnId, suppressEvents](const std::string& s) {
        if (*suppressEvents || selectedConnId->empty()) return;
        chart->SetConnectionLabel(*selectedConnId, s);
    };
    
    ddStyle->onSelectionChanged = [chart, selectedConnId, suppressEvents](int idx, const DropdownItem&) {
        if (*suppressEvents || selectedConnId->empty()) return;
        if (idx < 0 || idx > 2) return;
        chart->SetConnectionStyle(*selectedConnId, static_cast<FlowChartConnectionStyle>(idx));
    };
    
    ddArrow->onSelectionChanged = [chart, selectedConnId, suppressEvents](int idx, const DropdownItem&) {
        if (*suppressEvents || selectedConnId->empty()) return;
        if (idx < 0 || idx > 3) return;
        // Arrow style needs an API; SetConnectionStyle exists but not for
        // arrow. Workaround: rebuild the connection. We only need the
        // arrow change to be visible — the FlowChart core has no public
        // setter for arrowStyle, so we re-AddConnection.
        FlowChartConnection* cp = chart->GetConnection(*selectedConnId);
        if (!cp) return;
        FlowChartConnection snap = *cp;
        chart->RemoveConnection(snap.id);
        chart->AddConnection(snap.id, snap.sourceNodeId, snap.targetNodeId,
                             snap.style, static_cast<FlowChartArrowStyle>(idx));
        chart->SetConnectionLabel(snap.id, snap.label);
        chart->SetConnectionLineStyle(snap.id, snap.lineStyle);
        chart->SetConnectionColor(snap.id, snap.lineColor);
        chart->SetConnectionWidth(snap.id, snap.lineWidth);
    };
    
    inCWidth->onFocusLost = [chart, selectedConnId, inCWidth, parseDimension, suppressEvents]() {
        if (selectedConnId->empty()) return;
        FlowChartConnection* cp = chart->GetConnection(*selectedConnId);
        if (!cp) return;
        int w = parseDimension(inCWidth->GetText(), static_cast<int>(cp->lineWidth + 0.5f), 1, 6);
        chart->SetConnectionWidth(*selectedConnId, static_cast<float>(w));
        *suppressEvents = true;
        inCWidth->SetText(std::to_string(w));
        *suppressEvents = false;
    };
    
    for (size_t i = 0; i < kNumPresets; ++i) {
        ColorPreset p = kColorPresets[i];
        connColorBtns[i]->SetOnClick([chart, selectedConnId, p]() {
            if (selectedConnId->empty()) return;
            chart->SetConnectionColor(*selectedConnId, p.border);
        });
    }
    
    btnYes->SetOnClick([chart, selectedConnId, inCLabel, suppressEvents]() {
        if (selectedConnId->empty()) return;
        chart->SetConnectionLabel(*selectedConnId, "Yes");
        chart->SetConnectionColor(*selectedConnId, kColorPresets[0].border);  // green
        *suppressEvents = true;
        inCLabel->SetText("Yes");
        *suppressEvents = false;
    });
    btnNo->SetOnClick([chart, selectedConnId, inCLabel, suppressEvents]() {
        if (selectedConnId->empty()) return;
        chart->SetConnectionLabel(*selectedConnId, "No");
        chart->SetConnectionColor(*selectedConnId, kColorPresets[3].border);  // orange
        *suppressEvents = true;
        inCLabel->SetText("No");
        *suppressEvents = false;
    });
    btnCustom->SetOnClick([chart, selectedConnId, inCLabel, suppressEvents]() {
        if (selectedConnId->empty()) return;
        chart->SetConnectionLabel(*selectedConnId, "");
        chart->SetConnectionColor(*selectedConnId, Color(100, 100, 100, 255));
        *suppressEvents = true;
        inCLabel->SetText("");
        *suppressEvents = false;
    });
    
    // -------------------------------------------------------------------------
    // STICKY NOTE CALLBACKS
    // -------------------------------------------------------------------------
    inNText->onTextChanged = [chart, selectedNodeId, suppressEvents](const std::string& s) {
        if (*suppressEvents || selectedNodeId->empty()) return;
        chart->UpdateNodeLabel(*selectedNodeId, s);
    };
    
    auto applyNoteSize = [chart, selectedNodeId, inNW, inNH, parseDimension, suppressEvents]() {
        if (selectedNodeId->empty()) return;
        const FlowChartNode* n = chart->GetNode(*selectedNodeId);
        if (!n) return;
        int w = parseDimension(inNW->GetText(), static_cast<int>(n->width), 60, 400);
        int h = parseDimension(inNH->GetText(), static_cast<int>(n->height), 60, 400);
        if (w == static_cast<int>(n->width) && h == static_cast<int>(n->height)) {
            *suppressEvents = true;
            inNW->SetText(std::to_string(static_cast<int>(n->width)));
            inNH->SetText(std::to_string(static_cast<int>(n->height)));
            *suppressEvents = false;
            return;
        }
        chart->SetNodeSize(*selectedNodeId, static_cast<float>(w), static_cast<float>(h));
    };
    inNW->onFocusLost = applyNoteSize;
    inNH->onFocusLost = applyNoteSize;
    
    for (size_t i = 0; i < kNumPresets; ++i) {
        ColorPreset p = kColorPresets[i];
        noteFillBtns[i]->SetOnClick([chart, selectedNodeId, p]() {
            if (selectedNodeId->empty()) return;
            const FlowChartNode* n = chart->GetNode(*selectedNodeId);
            if (!n) return;
            chart->SetNodeColor(*selectedNodeId, p.fill, n->borderColor);
        });
    }
    
    // =========================================================================
    // TOOLBAR
    // =========================================================================
    int btnY = 730, btnX = 210, btnW = 100, btnH = 36, spacing = 10;
    
    auto btnSelect = std::make_shared<UltraCanvasButton>("bSelect", btnX, btnY, btnW, btnH);
    btnSelect->SetText("Select");
    btnSelect->SetColors(Color(220, 235, 255, 255), Color(200, 225, 255, 255));
    btnSelect->SetOnClick([chart]() {
        chart->SetCreateNodeMode(false);
        chart->SetEditMode(UltraCanvasFlowChart::EditMode::Select);
    });
    container->AddChild(btnSelect);
    btnX += btnW + spacing;
    
    auto btnConnect = std::make_shared<UltraCanvasButton>("bConnect", btnX, btnY, btnW, btnH);
    btnConnect->SetText("Connect");
    btnConnect->SetOnClick([chart]() {
        chart->SetCreateNodeMode(false);
        chart->SetEditMode(UltraCanvasFlowChart::EditMode::CreateConnection);
    });
    container->AddChild(btnConnect);
    btnX += btnW + spacing;
    
    auto btnAddNote = std::make_shared<UltraCanvasButton>("bAddNote", btnX, btnY, btnW + 10, btnH);
    btnAddNote->SetText("Add Note");
    btnAddNote->SetColors(Color(255, 255, 200, 255), Color(255, 245, 180, 255));
    btnAddNote->SetBorder(1.0f, Color(220, 220, 120, 255));
    btnAddNote->SetOnClick([chart]() {
        // Drop a sticky note in the visible center of the canvas.
        std::string id = "note_" + std::to_string(std::rand() % 100000);
        chart->AddNode(id, FlowChartShape::StickyNote, "Note", 380, 280, 120, 80);
        chart->SetNodeColor(id, Color(255, 255, 200, 255), Color(220, 220, 120, 255));
        chart->SetNodeTextColor(id, Color(80, 80, 0, 255));
        chart->SetNodeFontSize(id, 11.0f);
        chart->SelectNode(id);
    });
    container->AddChild(btnAddNote);
    btnX += btnW + 10 + spacing;
    
    auto btnDone = std::make_shared<UltraCanvasButton>("bDone", btnX, btnY, btnW, btnH);
    btnDone->SetText("Done");
    btnDone->SetOnClick([chart]() {
        chart->SetCreateNodeMode(false);
        chart->SetEditMode(UltraCanvasFlowChart::EditMode::Select);
    });
    container->AddChild(btnDone);
    btnX += btnW + spacing + 20;
    
    auto btnDelete = std::make_shared<UltraCanvasButton>("bDelete", btnX, btnY, btnW, btnH);
    btnDelete->SetText("Delete");
    btnDelete->SetColors(Color(255, 220, 220, 255), Color(255, 200, 200, 255));
    btnDelete->SetOnClick([chart, showNoSelection]() {
        chart->DeleteSelected();
        showNoSelection();
    });
    container->AddChild(btnDelete);
    btnX += btnW + spacing;
    
    auto btnClear = std::make_shared<UltraCanvasButton>("bClear", btnX, btnY, btnW, btnH);
    btnClear->SetText("Clear All");
    btnClear->SetColors(Color(255, 220, 220, 255), Color(255, 200, 200, 255));
    btnClear->SetOnClick([chart, showNoSelection]() {
        chart->Clear();
        showNoSelection();
    });
    container->AddChild(btnClear);
    
    // Settings checkboxes
    auto chkGrid = std::make_shared<UltraCanvasCheckbox>("bChkGrid", 210, 780, 130, 24);
    chkGrid->SetText("Show Grid");
    chkGrid->SetChecked(true);
    chkGrid->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetGridVisible(newState == CheckedState::Checked, 20.0f);
    };
    container->AddChild(chkGrid);
    
    auto chkSnap = std::make_shared<UltraCanvasCheckbox>("bChkSnap", 355, 780, 150, 24);
    chkSnap->SetText("Snap to Grid");
    chkSnap->SetChecked(true);
    chkSnap->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetSnapToGrid(newState == CheckedState::Checked);
    };
    container->AddChild(chkSnap);
    
    return container;
}

// =============================================================================
// MAIN ENTRY POINT - Two-tab demo
// =============================================================================
std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateFlowChartExamples() {
    auto outer = std::make_shared<UltraCanvasContainer>("FlowChartDemo", 0, 0, 1400, 970);
    outer->SetBackgroundColor(Color(245, 247, 250, 255));
    
    auto heading = std::make_shared<UltraCanvasLabel>("DemoHeading", 30, 12, 1100, 25);
    heading->SetText("Interactive Flow Chart Diagram Editor");
    heading->SetFont("Arial", 18.0f, FontWeight::Bold);
    heading->SetTextColor(Color(30, 30, 30, 255));
    outer->AddChild(heading);
    
    auto tabs = std::make_shared<UltraCanvasTabbedContainer>("FlowChartTabs", 0, 40, 1400, 930);
    tabs->SetTabStyle(TabStyle::Rounded);
    tabs->SetTabHeight(32);
    
    tabs->AddTab("Order Example",  BuildOrderExampleChart());
    tabs->AddTab("Build Your Own", BuildBuilderChart());
    tabs->SetActiveTab(0);
    
    outer->AddChild(tabs);
    return outer;
}

} // namespace UltraCanvas