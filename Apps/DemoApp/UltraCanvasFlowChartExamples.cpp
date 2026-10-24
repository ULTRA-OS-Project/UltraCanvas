// Apps/DemoApp/UltraCanvasFlowChartExamples.cpp
// Interactive flow chart demo - tabbed: Order Example + rich diagram builder
// Version: 2.4.0 - the shared page look (UltraCanvasDemoPageKit): kit title, the
//                  tabs in a card that fills the area, every tab a flex page,
//                  every label sized to its text
// Version: 2.3.2
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework
//
// Changelog:
//   2.4.0 - The kit's page holds the tabs in one card and fills the display
//           area. Each tab is a flex page that scrolls when the window is
//           smaller than its canvas: the canvases keep their size, the
//           instructions and the properties panel sit beside them (under them
//           when the tab is narrow), toolbars and checkboxes are rows of
//           controls as wide as their text. The panel's labels wrap.
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
#include "UltraCanvasDemoPageKit.h"
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

// A tab's page: a column on the demo's light grey. The tab gives it its exact
// size; a canvas larger than that scrolls instead of being cut.
std::shared_ptr<UltraCanvasContainer> MakeTabPage(const std::string& id) {
    auto page = std::make_shared<UltraCanvasContainer>(id);
    page->SetBackgroundColor(Color(245, 247, 250, 255));
    page->SetPadding(14, 20, 14, 20);
    page->layout.SetFlexColumn().SetFlexGap(10)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    ContainerStyle style;
    style.autoShowScrollbars = true;
    page->SetContainerStyle(style);
    return page;
}

// A grey field caption in the properties panel.
std::shared_ptr<UltraCanvasLabel> AddFieldLabel(const std::shared_ptr<UltraCanvasContainer>& parent,
                                                const std::string& id, const std::string& text) {
    auto lbl = DemoKit::AddText(parent, id, text);
    lbl->SetFontSize(11.0f);
    lbl->SetTextColor(Color(80, 80, 80, 255));
    return lbl;
}
}

// =============================================================================
// TAB 1: ORDER EXAMPLE - Pre-built chart, unchanged from 2.2.0
// =============================================================================
static std::shared_ptr<UltraCanvasContainer> BuildOrderExampleChart() {
    auto container = MakeTabPage("OrderExample");

    DemoKit::AddHeading(container, "Title", "Order Processing Flow Chart")->SetFontSize(22.0f);
    auto desc = DemoKit::AddText(container, "Desc",
            "Pre-built example. Click Select to move/connect, drag nodes to reposition.");
    desc->SetFontSize(13);
    desc->SetTextColor(Color(100, 100, 100, 255));

    // The canvas and, beside it (under it when the tab is narrow), the
    // instructions.
    auto workRow = DemoKit::AddRow(container, "OrderWorkRow", 10);
    workRow->layout.SetFlexAlignItems(CSSLayout::AlignItems::Start);

    auto chart = CreateFlowChart("flowChart", 0, 0, 1130, 680);
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
    
    chart->AddConnection("c6", "process3", "note1", FlowChartConnectionStyle::Straight, FlowChartArrowStyle::NoArrow);
    chart->SetConnectionLineStyle("c6", FlowChartLineStyle::Dotted);
    chart->SetConnectionColor("c6", Color(180, 180, 180, 255));
    chart->SetConnectionWidth("c6", 1.5f);
    
    // The node positions above are laid out for this canvas size.
    DemoKit::AddFixed(workRow, chart);

    auto instrPanel = DemoKit::CreateColumn("instrPanel", 8);
    instrPanel->SetBackgroundColor(Color(255, 255, 255, 255));
    instrPanel->SetBorders(1.0f, Color(220, 220, 220, 255));
    instrPanel->SetPadding(10);
    DemoKit::Add(workRow, instrPanel, 1);
    instrPanel->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(220));

    auto instrTitle = DemoKit::AddHeading(instrPanel, "instrTitle", "How to Use");
    instrTitle->SetFontSize(14.0f);
    instrTitle->SetTextColor(Color(40, 40, 40, 255));

    const char* instructions[][2] = {
        {"instr1", "1. Click 'Select' then drag nodes to reposition."},
        {"instr2", "2. Click 'Connect' then click two nodes to connect them."},
        {"instr3", "3. Press Delete to remove the selected node."},
        {"instr4", "4. Switch to 'Build Your Own' tab to create from scratch."}};
    for (const auto& entry : instructions) {
        auto instr = DemoKit::AddText(instrPanel, entry[0], entry[1]);
        instr->SetFontSize(11.0f);
        instr->SetTextColor(Color(80, 80, 80, 255));
    }

    // Buttons as wide as their text, in a row that wraps.
    const int btnH = 38;
    auto buttonRow = DemoKit::AddRow(container, "OrderButtons", 15);

    auto btnSelect = std::make_shared<UltraCanvasButton>("btnSelect", 0, 0, 110, btnH);
    btnSelect->SetText("Select");
    btnSelect->SetColors(Color(220, 235, 255, 255), Color(200, 225, 255, 255));
    btnSelect->SetOnClick([chart]() { chart->SetEditMode(UltraCanvasFlowChart::EditMode::Select); });
    DemoKit::Add(buttonRow, btnSelect);

    auto btnConnect = std::make_shared<UltraCanvasButton>("btnConnect", 0, 0, 110, btnH);
    btnConnect->SetText("Connect");
    btnConnect->SetOnClick([chart]() { chart->SetEditMode(UltraCanvasFlowChart::EditMode::CreateConnection); });
    DemoKit::Add(buttonRow, btnConnect);

    auto btnZoomIn = std::make_shared<UltraCanvasButton>("btnZoomIn", 0, 0, 90, btnH);
    btnZoomIn->SetText("Zoom +");
    btnZoomIn->SetOnClick([chart]() {
        float z = chart->GetZoomLevel() * 1.2f;
        if (z <= 3.0f) chart->SetZoomLevel(z);
    });
    btnZoomIn->SetMargin(0, 0, 0, 30);   // the view buttons are a group of their own
    DemoKit::Add(buttonRow, btnZoomIn);

    auto btnZoomOut = std::make_shared<UltraCanvasButton>("btnZoomOut", 0, 0, 90, btnH);
    btnZoomOut->SetText("Zoom -");
    btnZoomOut->SetOnClick([chart]() {
        float z = chart->GetZoomLevel() * 0.8f;
        if (z >= 0.3f) chart->SetZoomLevel(z);
    });
    DemoKit::Add(buttonRow, btnZoomOut);

    auto btnReset = std::make_shared<UltraCanvasButton>("btnReset", 0, 0, 120, btnH);
    btnReset->SetText("Reset View");
    btnReset->SetOnClick([chart]() { chart->SetZoomLevel(1.0f); chart->SetPanOffset(0, 0); });
    DemoKit::Add(buttonRow, btnReset);

    auto checkRow = DemoKit::AddRow(container, "OrderChecks", 15);

    auto chkGrid = std::make_shared<UltraCanvasCheckbox>("chkGrid", 0, 0, 0, 24);
    chkGrid->SetText("Show Grid");
    chkGrid->SetChecked(true);
    chkGrid->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetGridVisible(newState == CheckedState::Checked, 20.0f);
    };
    DemoKit::Add(checkRow, chkGrid);

    auto chkSnap = std::make_shared<UltraCanvasCheckbox>("chkSnap", 0, 0, 0, 24);
    chkSnap->SetText("Snap to Grid");
    chkSnap->SetChecked(true);
    chkSnap->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetSnapToGrid(newState == CheckedState::Checked);
    };
    DemoKit::Add(checkRow, chkSnap);

    return container;
}

// =============================================================================
// TAB 2: BUILDER - Rich editor with full node and connection editing
// =============================================================================
//
// Layout (a flex page):
//   title, description
//   row: palette (180 x 640) | chart canvas (870 x 640) | properties panel
//        (290 wide, under the canvas when the tab is narrow)
//   toolbar row, checkbox row
//
// Properties panel internals (a column):
//   header (title + status)
//   nodeSection (toggled visible)      when selection is a node
//   connectionSection (toggled)        when selection is a conn
//   noteSection (toggled)              when selection is a note
// A hidden section leaves the column, so the visible one sits under the header.
//
static std::shared_ptr<UltraCanvasContainer> BuildBuilderChart() {
    auto container = MakeTabPage("Builder");

    // --- Title bar ---
    DemoKit::AddHeading(container, "BuilderTitle", "Build Your Own Diagram")->SetFontSize(20.0f);
    auto desc = DemoKit::AddText(container, "BuilderDesc",
            "Pick a shape, click on the canvas to place it. Click any node or connection to edit its properties.");
    desc->SetTextColor(Color(100, 100, 100, 255));

    auto workRow = DemoKit::AddRow(container, "BuilderWorkRow", 10);

    // --- Chart canvas (empty) ---
    auto chart = CreateFlowChart("builderChart", 0, 0, 870, 640);
    chart->SetBackgroundColor(Color(255, 255, 255, 255));
    chart->SetGridVisible(true, 20.0f);
    chart->SetGridColor(Color(235, 235, 235, 255));
    chart->SetSnapToGrid(true);

    // --- Palette on the left ---
    auto palette = CreateFlowChartPalette("builderPalette", 0, 0, 180, 640);
    palette->SetTargetDiagram(chart);
    palette->BuildPalette();
    DemoKit::AddFixed(workRow, palette);
    DemoKit::AddFixed(workRow, chart);

    // =========================================================================
    // PROPERTIES PANEL
    // =========================================================================
    // As tall as the canvas row, or taller when its fields need it.
    auto propsPanel = DemoKit::CreateColumn("propsPanel", 4);
    propsPanel->SetBackgroundColor(Color(255, 255, 255, 255));
    propsPanel->SetBorders(1.0f, Color(220, 220, 220, 255));
    propsPanel->SetPadding(10, 12, 10, 12);
    propsPanel->size.width = CSSLayout::Dimension::Px(290);
    DemoKit::Add(workRow, propsPanel);

    // Static header
    auto propsTitle = DemoKit::AddHeading(propsPanel, "propsTitle", "Properties");
    propsTitle->SetFontSize(14.0f);
    propsTitle->SetTextColor(Color(40, 40, 40, 255));

    auto propsHint = DemoKit::AddText(propsPanel, "propsHint", "Select a node or connection.");
    propsHint->SetFontSize(11.0f);
    propsHint->SetTextColor(Color(140, 140, 140, 255));

    // A Width / Height pair side by side: two columns sharing the row.
    auto addSizePair = [](const std::shared_ptr<UltraCanvasContainer>& section, const std::string& rowId,
                          const std::string& lblWId, const std::string& lblHId,
                          const std::shared_ptr<UltraCanvasTextInput>& inWidth,
                          const std::shared_ptr<UltraCanvasTextInput>& inHeight) {
        auto row = DemoKit::CreateRow(rowId, 12, false);
        DemoKit::Add(section, row);
        auto colW = DemoKit::CreateColumn(rowId + "W", 2);
        DemoKit::Add(row, colW, 1);
        AddFieldLabel(colW, lblWId, "Width");
        DemoKit::AddStretched(colW, inWidth);
        auto colH = DemoKit::CreateColumn(rowId + "H", 2);
        DemoKit::Add(row, colH, 1);
        AddFieldLabel(colH, lblHId, "Height");
        DemoKit::AddStretched(colH, inHeight);
    };

    // A row of colour preset swatches, 42 x 22 with a 2 px gap.
    auto addSwatchRow = [](const std::shared_ptr<UltraCanvasContainer>& section,
                           const std::string& rowId, const std::string& idPrefix) {
        const int sw = 42, sh = 22;
        auto row = DemoKit::AddRow(section, rowId, 2);
        std::vector<std::shared_ptr<UltraCanvasButton>> btns;
        btns.reserve(kNumPresets);
        for (size_t i = 0; i < kNumPresets; ++i) {
            auto b = std::make_shared<UltraCanvasButton>(idPrefix + std::to_string(i), 0, 0, sw, sh);
            b->SetText("");
            b->SetColors(kColorPresets[i].fill, kColorPresets[i].fill);
            b->SetBorder(1.0f, kColorPresets[i].border);
            b->SetCornerRadius(3.0f);
            DemoKit::AddFixed(row, b);
            btns.push_back(b);
        }
        return btns;
    };

    // -------------------------------------------------------------------------
    // NODE SECTION (visible only when a non-StickyNote node is selected)
    // -------------------------------------------------------------------------
    auto nodeSection = DemoKit::CreateColumn("nodeSection", 4);
    nodeSection->SetBackgroundColor(Color(255, 255, 255, 0));  // transparent
    nodeSection->SetMargin(6, 0, 0, 0);
    DemoKit::Add(propsPanel, nodeSection);
    nodeSection->SetVisible(false);

    AddFieldLabel(nodeSection, "lblShape", "Shape");
    auto ddShape = std::make_shared<UltraCanvasDropdown>("ddShape", 0, 0, 266, 26);
    for (size_t i = 0; i < kNumShapeOptions; ++i) ddShape->AddItem(kShapeOptions[i].label);
    DemoKit::AddStretched(nodeSection, ddShape);

    AddFieldLabel(nodeSection, "lblLabel", "Label");
    auto inLabel = std::make_shared<UltraCanvasTextInput>("inLabel", 0, 0, 266, 26);
    inLabel->SetPlaceholder("Node label");
    inLabel->SetShowValidationState(false);
    DemoKit::AddStretched(nodeSection, inLabel);

    // Width / Height side by side
    auto inW = std::make_shared<UltraCanvasTextInput>("inW", 0, 0, 126, 26);
    inW->SetInputType(TextInputType::Integer);
    inW->SetShowValidationState(false);
    auto inH = std::make_shared<UltraCanvasTextInput>("inH", 0, 0, 128, 26);
    inH->SetInputType(TextInputType::Integer);
    inH->SetShowValidationState(false);
    addSizePair(nodeSection, "nodeSizeRow", "lblW", "lblH", inW, inH);

    // Fill / border / text colour preset rows; the buttons are wired up below.
    AddFieldLabel(nodeSection, "lbl_Fill", "Fill");
    auto fillBtns = addSwatchRow(nodeSection, "fillRow", "preset_7320_");
    AddFieldLabel(nodeSection, "lbl_Border", "Border");
    auto borderBtns = addSwatchRow(nodeSection, "borderRow", "preset_7330_");
    AddFieldLabel(nodeSection, "lbl_Text", "Text");
    auto textBtns = addSwatchRow(nodeSection, "textRow", "preset_7340_");

    AddFieldLabel(nodeSection, "lblFont", "Font size");
    auto inFont = std::make_shared<UltraCanvasTextInput>("inFont", 0, 0, 126, 26);
    inFont->SetInputType(TextInputType::Integer);
    inFont->SetShowValidationState(false);
    DemoKit::AddFixed(nodeSection, inFont);

    // -------------------------------------------------------------------------
    // CONNECTION SECTION
    // -------------------------------------------------------------------------
    auto connSection = DemoKit::CreateColumn("connSection", 4);
    connSection->SetBackgroundColor(Color(255, 255, 255, 0));
    connSection->SetMargin(6, 0, 0, 0);
    DemoKit::Add(propsPanel, connSection);
    connSection->SetVisible(false);

    AddFieldLabel(connSection, "lblCLabel", "Label");
    auto inCLabel = std::make_shared<UltraCanvasTextInput>("inCLabel", 0, 0, 266, 26);
    inCLabel->SetPlaceholder("(no label)");
    inCLabel->SetShowValidationState(false);
    DemoKit::AddStretched(connSection, inCLabel);

    AddFieldLabel(connSection, "lblStyle", "Line style");
    auto ddStyle = std::make_shared<UltraCanvasDropdown>("ddStyle", 0, 0, 266, 26);
    ddStyle->AddItem("Straight");
    ddStyle->AddItem("Orthogonal");
    ddStyle->AddItem("Curved");
    DemoKit::AddStretched(connSection, ddStyle);

    AddFieldLabel(connSection, "lblArrow", "Arrow");
    auto ddArrow = std::make_shared<UltraCanvasDropdown>("ddArrow", 0, 0, 266, 26);
    ddArrow->AddItem("None");
    ddArrow->AddItem("Arrow");
    ddArrow->AddItem("Arrow Filled");
    ddArrow->AddItem("Diamond");
    DemoKit::AddStretched(connSection, ddArrow);

    AddFieldLabel(connSection, "lblCWidth", "Width (1-6)");
    auto inCWidth = std::make_shared<UltraCanvasTextInput>("inCWidth", 0, 0, 126, 26);
    inCWidth->SetInputType(TextInputType::Integer);
    inCWidth->SetShowValidationState(false);
    DemoKit::AddFixed(connSection, inCWidth);

    AddFieldLabel(connSection, "lblCColor", "Color");
    auto connColorBtns = addSwatchRow(connSection, "connColorRow", "ccolor_");

    // Branch row: visible only when source of selected connection is Decision
    auto lblBranch = AddFieldLabel(connSection, "lblBranch", "Decision branch");
    lblBranch->SetVisible(false);
    auto branchRow = DemoKit::AddRow(connSection, "branchRow", 6);

    auto btnYes = std::make_shared<UltraCanvasButton>("btnYes", 0, 0, 80, 26);
    btnYes->SetText("Yes");
    btnYes->SetColors(kColorPresets[0].fill, kColorPresets[0].fill);  // green
    btnYes->SetBorder(1.0f, kColorPresets[0].border);
    btnYes->SetCornerRadius(3.0f);
    DemoKit::Add(branchRow, btnYes);
    btnYes->SetVisible(false);

    auto btnNo = std::make_shared<UltraCanvasButton>("btnNo", 0, 0, 80, 26);
    btnNo->SetText("No");
    btnNo->SetColors(kColorPresets[3].fill, kColorPresets[3].fill);  // orange/red
    btnNo->SetBorder(1.0f, kColorPresets[3].border);
    btnNo->SetCornerRadius(3.0f);
    DemoKit::Add(branchRow, btnNo);
    btnNo->SetVisible(false);

    auto btnCustom = std::make_shared<UltraCanvasButton>("btnCustom", 0, 0, 92, 26);
    btnCustom->SetText("Custom");
    btnCustom->SetColors(Color(240, 240, 240, 255), Color(220, 220, 220, 255));
    btnCustom->SetBorder(1.0f, Color(180, 180, 180, 255));
    btnCustom->SetCornerRadius(3.0f);
    DemoKit::Add(branchRow, btnCustom);
    btnCustom->SetVisible(false);

    // -------------------------------------------------------------------------
    // STICKY NOTE SECTION (selected node has shape == StickyNote)
    // -------------------------------------------------------------------------
    auto noteSection = DemoKit::CreateColumn("noteSection", 4);
    noteSection->SetBackgroundColor(Color(255, 255, 255, 0));
    noteSection->SetMargin(6, 0, 0, 0);
    DemoKit::Add(propsPanel, noteSection);
    noteSection->SetVisible(false);

    AddFieldLabel(noteSection, "lblNText", "Note text");
    // Sticky note text uses the same single-line input — multi-line authoring
    // works by typing literal "\n" in the text or by future multi-line widget.
    // For now: the user's text replaces the whole label.
    auto inNText = std::make_shared<UltraCanvasTextInput>("inNText", 0, 0, 266, 26);
    inNText->SetPlaceholder("Note text");
    inNText->SetShowValidationState(false);
    DemoKit::AddStretched(noteSection, inNText);

    auto inNW = std::make_shared<UltraCanvasTextInput>("inNW", 0, 0, 126, 26);
    inNW->SetInputType(TextInputType::Integer);
    inNW->SetShowValidationState(false);
    auto inNH = std::make_shared<UltraCanvasTextInput>("inNH", 0, 0, 128, 26);
    inNH->SetInputType(TextInputType::Integer);
    inNH->SetShowValidationState(false);
    addSizePair(noteSection, "noteSizeRow", "lblNW", "lblNH", inNW, inNH);

    AddFieldLabel(noteSection, "lblNFill", "Fill color");
    auto noteFillBtns = addSwatchRow(noteSection, "noteFillRow", "nfill_");

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
    
    inFont->onFocusLost = [chart, selectedNodeId, inFont = inFont.get(), parseDimension, suppressEvents]() {
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
    
    inCWidth->onFocusLost = [chart, selectedConnId, inCWidth = inCWidth.get(), parseDimension, suppressEvents]() {
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
    // Buttons as wide as their text, in a row that wraps.
    const int btnW = 100, btnH = 36;
    auto toolbar = DemoKit::AddRow(container, "BuilderToolbar", 10);
    
    auto btnSelect = std::make_shared<UltraCanvasButton>("bSelect", 0, 0, btnW, btnH);
    btnSelect->SetText("Select");
    btnSelect->SetColors(Color(220, 235, 255, 255), Color(200, 225, 255, 255));
    btnSelect->SetOnClick([chart]() {
        chart->SetCreateNodeMode(false);
        chart->SetEditMode(UltraCanvasFlowChart::EditMode::Select);
    });
    DemoKit::Add(toolbar, btnSelect);
    
    auto btnConnect = std::make_shared<UltraCanvasButton>("bConnect", 0, 0, btnW, btnH);
    btnConnect->SetText("Connect");
    btnConnect->SetOnClick([chart]() {
        chart->SetCreateNodeMode(false);
        chart->SetEditMode(UltraCanvasFlowChart::EditMode::CreateConnection);
    });
    DemoKit::Add(toolbar, btnConnect);
    
    auto btnAddNote = std::make_shared<UltraCanvasButton>("bAddNote", 0, 0, btnW + 10, btnH);
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
    DemoKit::Add(toolbar, btnAddNote);
    
    auto btnDone = std::make_shared<UltraCanvasButton>("bDone", 0, 0, btnW, btnH);
    btnDone->SetText("Done");
    btnDone->SetOnClick([chart]() {
        chart->SetCreateNodeMode(false);
        chart->SetEditMode(UltraCanvasFlowChart::EditMode::Select);
    });
    DemoKit::Add(toolbar, btnDone);
    
    auto btnDelete = std::make_shared<UltraCanvasButton>("bDelete", 0, 0, btnW, btnH);
    btnDelete->SetText("Delete");
    btnDelete->SetColors(Color(255, 220, 220, 255), Color(255, 200, 200, 255));
    btnDelete->SetOnClick([chart, showNoSelection]() {
        chart->DeleteSelected();
        showNoSelection();
    });
    btnDelete->SetMargin(0, 0, 0, 20);   // the destructive buttons stand apart
    DemoKit::Add(toolbar, btnDelete);
    
    auto btnClear = std::make_shared<UltraCanvasButton>("bClear", 0, 0, btnW, btnH);
    btnClear->SetText("Clear All");
    btnClear->SetColors(Color(255, 220, 220, 255), Color(255, 200, 200, 255));
    btnClear->SetOnClick([chart, showNoSelection]() {
        chart->Clear();
        showNoSelection();
    });
    DemoKit::Add(toolbar, btnClear);
    
    // Settings checkboxes
    auto checkRow = DemoKit::AddRow(container, "BuilderChecks", 15);

    auto chkGrid = std::make_shared<UltraCanvasCheckbox>("bChkGrid", 0, 0, 0, 24);
    chkGrid->SetText("Show Grid");
    chkGrid->SetChecked(true);
    chkGrid->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetGridVisible(newState == CheckedState::Checked, 20.0f);
    };
    DemoKit::Add(checkRow, chkGrid);
    
    auto chkSnap = std::make_shared<UltraCanvasCheckbox>("bChkSnap", 0, 0, 0, 24);
    chkSnap->SetText("Snap to Grid");
    chkSnap->SetChecked(true);
    chkSnap->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetSnapToGrid(newState == CheckedState::Checked);
    };
    DemoKit::Add(checkRow, chkSnap);
    
    return container;
}

// =============================================================================
// MAIN ENTRY POINT - Two-tab demo
// =============================================================================
std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateFlowChartExamples() {
    auto page = DemoKit::CreatePage("FlowChartDemo", "Interactive Flow Chart Diagram Editor", "");
    // The page is the display area's height and the tabs take what the title
    // leaves; each tab page scrolls inside.
    page->SetFillsArea(true);

    auto card = DemoKit::CreateCard("FlowChartCard");
    card->SetPadding(4);
    DemoKit::Add(page, card, 1);
    
    auto tabs = std::make_shared<UltraCanvasTabbedContainer>("FlowChartTabs", 0, 0, 0, 0);
    tabs->SetTabStyle(TabStyle::Rounded);
    tabs->SetTabHeight(32);
    tabs->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    
    tabs->AddTab("Order Example",  BuildOrderExampleChart());
    tabs->AddTab("Build Your Own", BuildBuilderChart());
    tabs->SetActiveTab(0);
    
    DemoKit::Add(card, tabs, 1);
    return page;
}

} // namespace UltraCanvas
