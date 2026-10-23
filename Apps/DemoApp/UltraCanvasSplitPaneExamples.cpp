// Apps/DemoApp/UltraCanvasSplitPaneExamples.cpp
// Demonstrates UltraCanvasSplitPane: horizontal, vertical, nested splits,
// splitter handles and action icons on the split line
// Version: 1.2.0 - the shared page look (UltraCanvasDemoPageKit): one card per
//                  example, the split panes as wide as the card, every pane a
//                  column whose labels size to their text (and scrolls when a
//                  dragged-narrow pane wraps them taller than it is)
// Version: 1.1.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasContainer.h"
#include "UltraCanvasSpacer.h"
#include "CSSLayout/CSSLayout.h"
#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasSplitPane.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasDebug.h"
#include <sstream>

namespace UltraCanvas {

    namespace {
        // A pane's contents are a column: every label as wide as the pane and
        // as tall as its wrapped lines. A pane dragged narrow wraps its text
        // taller than itself, so it scrolls rather than cutting the text off.
        void SetupPane(const std::shared_ptr<UltraCanvasContainer>& pane, const Color& background) {
            pane->SetBackgroundColor(background);
            pane->SetPadding(8);
            pane->layout.SetFlexColumn().SetFlexGap(2)
                        .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
            ContainerStyle style = pane->GetContainerStyle();
            style.autoShowScrollbars = true;
            pane->SetContainerStyle(style);
        }

        std::shared_ptr<UltraCanvasLabel> AddPaneTitle(const std::shared_ptr<UltraCanvasContainer>& pane,
                                                       const std::string& id, const std::string& text) {
            auto l = DemoKit::AddHeading(pane, id, text);
            l->SetTextColor(Color(40, 40, 40, 255));
            l->SetMargin(0, 0, 8, 0);
            return l;
        }

        std::shared_ptr<UltraCanvasLabel> AddInfoLabel(const std::shared_ptr<UltraCanvasContainer>& pane,
                                                       const std::string& id, const std::string& text,
                                                       int fontSize = 12) {
            auto l = DemoKit::AddText(pane, id, text);
            l->SetFontSize(fontSize);
            l->SetTextColor(Color(60, 60, 60, 255));
            return l;
        }

        // A button as wide as its text, at least `minWidth` (the width it had).
        std::shared_ptr<UltraCanvasButton> MakeSplitButton(const std::string& id, const std::string& text,
                                                           float minWidth, float height) {
            auto b = std::make_shared<UltraCanvasButton>(id, 0, 0, 0, height, text);
            CSSLayout::BoxConstraints limits;
            limits.minWidth = CSSLayout::Dimension::Px(minWidth);
            b->boxConstraints = limits;
            return b;
        }

        std::string SplitIcon(const std::string& name) {
            return NormalizePath(GetResourcesDir() + "media/icons/" + name);
        }
    } // namespace

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateSplitPaneExamples() {
        auto root = DemoKit::CreatePage("SplitPaneExamples", "UltraCanvas Split Pane Examples",
            "Drag the splitter bars to resize panes. Weights are proportional, so resizing the SplitPane "
            "preserves relative sizes; per-pane min/max sizes clamp the drag.");

        // ============================================================
        // Section A: Horizontal Split (3 panes, weighted)
        // ============================================================
        auto cardA = DemoKit::AddCard(root, "SplitCardA", "1. Horizontal Split (3 panes, weights 1 : 2 : 1)",
            "Three panes side-by-side. Dragging splitter 0 affects only Sidebar+Editor; dragging splitter 1 "
            "affects only Editor+Inspector. Each pane has a minimum width of 80 px.");

        auto statusA = DemoKit::CreateText("SplitAStatus", "Weights: 1.00 : 2.00 : 1.00");
        statusA->SetFontSize(11);
        statusA->SetTextColor(Color(0, 100, 200, 255));

        auto splitH = CreateHorizontalSplitPane("SplitPaneA", 0, 0, 960, 240);
        splitH->SetBorders(1, Color(180, 180, 180, 255));

        auto paneSidebar   = splitH->AddPane(1.0);
        auto paneEditor    = splitH->AddPane(2.0);
        auto paneInspector = splitH->AddPane(1.0);

        splitH->SetPaneMinSize(0, 80);
        splitH->SetPaneMinSize(1, 80);
        splitH->SetPaneMinSize(2, 80);

        SetupPane(paneSidebar, Color(225, 235, 250, 255));
        AddPaneTitle(paneSidebar, "paSidebarTitle", "Sidebar");
        {
            // At the start of the line, not stretched: as wide as the text,
            // at least the old 130 px; a narrower pane scrolls sideways.
            const char* names[] = {"Files", "Search", "Git"};
            for (int i = 0; i < 3; ++i) {
                auto b = MakeSplitButton("paSidebarBtn" + std::to_string(i + 1), names[i], 130, 28);
                DemoKit::AddFixed(paneSidebar, b);
                b->SetMargin(0, 0, 4, 0);
            }
        }

        SetupPane(paneEditor, Color(252, 252, 252, 255));
        AddPaneTitle(paneEditor, "paEditorTitle", "Editor");
        AddInfoLabel(paneEditor, "paEditorCode1", "int main() {", 12);
        AddInfoLabel(paneEditor, "paEditorCode2", "    std::cout << \"Hello, SplitPane\\n\";", 12);
        AddInfoLabel(paneEditor, "paEditorCode3", "    return 0;", 12);
        AddInfoLabel(paneEditor, "paEditorCode4", "}", 12);
        AddInfoLabel(paneEditor, "paEditorHint", "(Drag the splitters at left/right to resize.)", 11)
                ->SetMargin(18, 0, 0, 0);

        SetupPane(paneInspector, Color(245, 245, 245, 255));
        AddPaneTitle(paneInspector, "paInspTitle", "Inspector");
        AddInfoLabel(paneInspector, "paInspRow1", "Type:        Container");
        AddInfoLabel(paneInspector, "paInspRow2", "Children:    3");
        AddInfoLabel(paneInspector, "paInspRow3", "Visible:     true");
        AddInfoLabel(paneInspector, "paInspRow4", "Z-order:     0");

        // Live weight readout
        auto statusPtr = statusA.get();
        splitH->onWeightsChanged = [statusPtr](const std::vector<double>& weights) {
            std::ostringstream oss;
            oss << "Weights:";
            oss.setf(std::ios::fixed);
            oss.precision(2);
            for (size_t i = 0; i < weights.size(); ++i) {
                oss << (i == 0 ? " " : " : ") << weights[i];
            }
            statusPtr->SetText(oss.str());
        };

        DemoKit::AddStretched(cardA, splitH);
        DemoKit::Add(cardA, statusA);

        // ============================================================
        // Section B: Vertical Split (2 panes)
        // ============================================================
        auto cardB = DemoKit::AddCard(root, "SplitCardB", "2. Vertical Split (2 panes, weights 3 : 1)",
            "An editor area on top and a 'terminal' panel below. Drag the horizontal splitter; "
            "neither pane can shrink below its minimum (100 px / 60 px).");

        auto splitV = CreateVerticalSplitPane("SplitPaneB", 0, 0, 960, 280);
        splitV->SetBorders(1, Color(180, 180, 180, 255));

        auto paneEditorV = splitV->AddPane(3.0);
        auto paneTerm    = splitV->AddPane(1.0);
        splitV->SetPaneMinSize(0, 100);
        splitV->SetPaneMinSize(1, 60);

        SetupPane(paneEditorV, Color(252, 252, 252, 255));
        AddPaneTitle(paneEditorV, "pbEditorTitle", "Editor area");
        AddInfoLabel(paneEditorV, "pbEditorL1", "// Drag the horizontal splitter below.", 12);
        AddInfoLabel(paneEditorV, "pbEditorL2", "for (int i = 0; i < 10; ++i) {", 12);
        AddInfoLabel(paneEditorV, "pbEditorL3", "    std::cout << i << \"\\n\";", 12);
        AddInfoLabel(paneEditorV, "pbEditorL4", "}", 12);

        SetupPane(paneTerm, Color(30, 30, 30, 255));
        {
            auto title = AddPaneTitle(paneTerm, "pbTermTitle", "Terminal");
            title->SetTextColor(Color(220, 220, 220, 255));
            title->SetMargin(0, 0, 4, 0);

            auto line1 = AddInfoLabel(paneTerm, "pbTerm1", "$ ./build.sh --release");
            line1->SetTextColor(Color(200, 240, 200, 255));
            auto line2 = AddInfoLabel(paneTerm, "pbTerm2", "[100%] Built target UltraCanvasDemo");
            line2->SetTextColor(Color(200, 200, 200, 255));
            auto line3 = AddInfoLabel(paneTerm, "pbTerm3", "$ _");
            line3->SetTextColor(Color(200, 240, 200, 255));
        }

        DemoKit::AddStretched(cardB, splitV);

        // ============================================================
        // Section C: Nested Splits (VSCode-like)
        // ============================================================
        auto cardC = DemoKit::AddCard(root, "SplitCardC", "3. Nested Splits (horizontal containing a vertical split)",
            "Outer horizontal split: left sidebar pane + right pane. The right pane contains a vertical "
            "split (editor on top, output below). Drag outer/inner splitters independently.");

        auto outer = CreateHorizontalSplitPane("SplitPaneC", 0, 0, 960, 320);
        outer->SetBorders(1, Color(180, 180, 180, 255));

        auto outerLeft  = outer->AddPane(1.0);
        auto outerRight = outer->AddPane(3.0);
        outer->SetPaneMinSize(0, 100);
        outer->SetPaneMinSize(1, 200);

        SetupPane(outerLeft, Color(225, 235, 250, 255));
        AddPaneTitle(outerLeft, "pcSidebar", "Sidebar");
        AddInfoLabel(outerLeft, "pcSidebarA", "Project files");
        AddInfoLabel(outerLeft, "pcSidebarB", "Search");
        AddInfoLabel(outerLeft, "pcSidebarC", "Source control");

        // Build nested vertical split; let a BoxLayout on outerRight stretch it to fill.
        auto inner = CreateVerticalSplitPane("SplitPaneCInner", 0, 0, 100, 100);
        auto innerTop    = inner->AddPane(3.0);
        auto innerBottom = inner->AddPane(1.0);
        inner->SetPaneMinSize(0, 80);
        inner->SetPaneMinSize(1, 60);

        SetupPane(innerTop, Color(252, 252, 252, 255));
        AddPaneTitle(innerTop, "pcInnerEditor", "Editor");
        AddInfoLabel(innerTop, "pcInnerE1", "auto pane = split->AddPane(1.0);", 12);
        AddInfoLabel(innerTop, "pcInnerE2", "pane->SetBackgroundColor(Colors::White);", 12);
        AddInfoLabel(innerTop, "pcInnerE3", "pane->AddChild(makeWidget());", 12);

        SetupPane(innerBottom, Color(245, 245, 245, 255));
        AddPaneTitle(innerBottom, "pcInnerOut", "Output");
        AddInfoLabel(innerBottom, "pcInnerO1", "[info] split created with 2 panes");
        AddInfoLabel(innerBottom, "pcInnerO2", "[info] weights normalized");

        outerRight->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        outerRight->layout.SetFlexGap(0);
        outerRight->AddChild(inner);
        inner->layoutItem.SetFlexGrow(1)
                        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        DemoKit::AddStretched(cardC, outer);

        // ============================================================
        // Section D: Splitter handles
        // ============================================================
        auto cardD = DemoKit::AddCard(root, "SplitCardD", "4. Splitter handle (shape and size)",
            "An optional grab handle sits on the split line. Pick a shape - square, square with rounded "
            "edges, round (a circle when square, a capsule when elongated), or an SVG/raster image - and a "
            "size across the line. The splitter strip widens to fit the handle; the split line itself stays "
            "as thin as configured. An image handle with no explicit length takes its proportions from the "
            "asset. Use the buttons to switch shape and size live.");

        auto splitHandles = CreateHorizontalSplitPane("SplitPaneD", 0, 0, 960, 200);
        splitHandles->SetBorders(1, Color(180, 180, 180, 255));

        auto handleLeft  = splitHandles->AddPane(1.0);
        auto handleMid   = splitHandles->AddPane(1.0);
        auto handleRight = splitHandles->AddPane(1.0);
        splitHandles->SetPaneMinSize(0, 80);
        splitHandles->SetPaneMinSize(1, 80);
        splitHandles->SetPaneMinSize(2, 80);

        {
            SplitPaneStyle st = splitHandles->GetSplitPaneStyle();
            st.splitterThickness = 3;
            st.handle.shape = SplitterHandleShape::RoundedSquare;
            st.handle.crossSize = 14;
            st.handle.axisLength = 48;
            st.handle.cornerRadius = 4.0f;
            splitHandles->SetSplitPaneStyle(st);
        }

        SetupPane(handleLeft, Color(232, 240, 252, 255));
        AddPaneTitle(handleLeft, "pdLeft", "Left");
        AddInfoLabel(handleLeft, "pdLeftInfo", "Grab the handle or the line.");

        SetupPane(handleMid, Color(252, 252, 252, 255));
        AddPaneTitle(handleMid, "pdMid", "Middle");
        AddInfoLabel(handleMid, "pdMidInfo1", "handle.shape");
        AddInfoLabel(handleMid, "pdMidInfo2", "handle.crossSize");
        AddInfoLabel(handleMid, "pdMidInfo3", "handle.axisLength");

        SetupPane(handleRight, Color(245, 245, 245, 255));
        AddPaneTitle(handleRight, "pdRight", "Right");
        AddInfoLabel(handleRight, "pdRightInfo", "Both splitters share one style.");

        DemoKit::AddStretched(cardD, splitHandles);

        {
            auto splitPtr = splitHandles.get();
            auto applyHandle = [splitPtr](SplitterHandleShape shape, int crossSize, int axisLength,
                                          const std::string& imagePath) {
                SplitterHandleStyle hs = splitPtr->GetSplitterHandleStyle();
                hs.shape = shape;
                hs.crossSize = crossSize;
                hs.axisLength = axisLength;
                hs.imagePath = imagePath;
                splitPtr->SetSplitterHandleStyle(hs);
            };

            struct HandleChoice {
                const char* text;
                SplitterHandleShape shape;
                int crossSize;
                int axisLength;
                const char* image;   // empty for the drawn shapes
            };
            const HandleChoice choices[] = {
                { "None",         SplitterHandleShape::NoHandle,      14,  0, "" },
                { "Square",       SplitterHandleShape::Square,        14, 48, "" },
                { "Rounded",      SplitterHandleShape::RoundedSquare, 14, 48, "" },
                { "Capsule",      SplitterHandleShape::Round,         14, 48, "" },
                { "Circle 16px",  SplitterHandleShape::Round,         16, 16, "" },
                { "Rounded 20px", SplitterHandleShape::RoundedSquare, 20, 64, "" },
                // Image handle: axisLength 0 lets the 14x48 asset set its own
                // proportions from the 14 px cross size.
                { "SVG image",    SplitterHandleShape::Image,         14,  0, "scrollbar-handle-v.svg" },
            };

            auto buttonRow = DemoKit::AddRow(cardD, "pdHandleButtons", 8);
            int n = 0;
            for (const auto& c : choices) {
                auto b = MakeSplitButton("pdHandleBtn" + std::to_string(n++), c.text, 110, 26);
                SplitterHandleShape shape = c.shape;
                int cs = c.crossSize;
                int al = c.axisLength;
                std::string img = (c.image[0] != '\0') ? SplitIcon(c.image) : std::string();
                b->SetOnClick([applyHandle, shape, cs, al, img]() { applyHandle(shape, cs, al, img); });
                DemoKit::Add(buttonRow, b);
            }
        }

        // ============================================================
        // Section E: Action icons on the split line
        // ============================================================
        auto cardE = DemoKit::AddCard(root, "SplitCardE", "5. Action icons on the split line",
            "Any number of icons can sit on a split line, centred across it and grouped at the middle "
            "along it. Each icon has its own tooltip, enabled state and click handler, and clicking one "
            "never starts a drag. Here splitter 0 collapses/restores the sidebar and splitter 1 carries "
            "three actions. The handle wraps the icon group.");

        auto statusE = DemoKit::CreateText("SplitEStatus", "Click an icon on a split line.");
        statusE->SetFontSize(11);
        statusE->SetTextColor(Color(0, 100, 200, 255));

        auto splitIcons = CreateHorizontalSplitPane("SplitPaneE", 0, 0, 960, 230);
        splitIcons->SetBorders(1, Color(180, 180, 180, 255));

        auto iconSidebar = splitIcons->AddPane(1.0);
        auto iconMain    = splitIcons->AddPane(3.0);
        auto iconAside   = splitIcons->AddPane(1.0);
        splitIcons->SetPaneMinSize(1, 120);

        {
            SplitPaneStyle st = splitIcons->GetSplitPaneStyle();
            st.splitterThickness = 3;
            st.handle.shape = SplitterHandleShape::Round;   // capsule around the icon group
            st.handle.crossSize = 22;
            st.handle.color = Color(244, 244, 244, 255);
            st.handle.borderColor = Color(170, 170, 170, 255);
            st.icons.size = 14;
            st.icons.spacing = 6;
            st.icons.padding = 5;
            splitIcons->SetSplitPaneStyle(st);
        }

        SetupPane(iconSidebar, Color(232, 240, 252, 255));
        AddPaneTitle(iconSidebar, "peSidebar", "Sidebar");
        AddInfoLabel(iconSidebar, "peSidebarInfo", "Collapse me with the icon.");

        SetupPane(iconMain, Color(252, 252, 252, 255));
        AddPaneTitle(iconMain, "peMain", "Main");
        AddInfoLabel(iconMain, "peMainInfo1", "splitIcons->AddSplitterIcon(0, icon);");
        AddInfoLabel(iconMain, "peMainInfo2", "icon.onClick = [](size_t s, size_t i) { ... };");

        SetupPane(iconAside, Color(245, 245, 245, 255));
        AddPaneTitle(iconAside, "peAside", "Aside");
        AddInfoLabel(iconAside, "peAsideInfo", "Three icons on the right line.");

        {
            auto* splitPtr = splitIcons.get();
            auto* statusPtrE = statusE.get();

            // --- splitter 0: collapse / restore the sidebar ---
            SplitPaneIcon collapse;
            collapse.id = "collapse-sidebar";
            collapse.iconPath = SplitIcon("angle-left.svg");
            collapse.tooltip = "Collapse the sidebar";
            collapse.onClick = [splitPtr, statusPtrE](size_t s, size_t i) {
                splitPtr->SetPaneWeight(0, 0.02);
                splitPtr->SetSplitterIconVisible(s, i, false);
                splitPtr->SetSplitterIconVisible(s, i + 1, true);
                statusPtrE->SetText("Sidebar collapsed.");
            };
            splitIcons->AddSplitterIcon(0, collapse);

            SplitPaneIcon restore;
            restore.id = "restore-sidebar";
            restore.iconPath = SplitIcon("angle-right.svg");
            restore.tooltip = "Restore the sidebar";
            restore.visible = false;
            restore.onClick = [splitPtr, statusPtrE](size_t s, size_t i) {
                splitPtr->SetPaneWeight(0, 1.0);
                splitPtr->SetSplitterIconVisible(s, i, false);
                splitPtr->SetSplitterIconVisible(s, i - 1, true);
                statusPtrE->SetText("Sidebar restored.");
            };
            splitIcons->AddSplitterIcon(0, restore);

            // --- splitter 1: three actions ---
            SplitPaneIcon grow;
            grow.id = "grow-aside";
            grow.iconPath = SplitIcon("angle-left.svg");
            grow.tooltip = "Give the aside more room";
            grow.onClick = [splitPtr, statusPtrE](size_t, size_t) {
                splitPtr->SetPaneWeight(2, splitPtr->GetPaneWeight(2) * 1.4);
                statusPtrE->SetText("Aside enlarged.");
            };
            splitIcons->AddSplitterIcon(1, grow);

            SplitPaneIcon even;
            even.id = "even-panes";
            even.iconPath = SplitIcon("maximise.svg");
            even.tooltip = "Even out all three panes";
            even.onClick = [splitPtr, statusPtrE](size_t, size_t) {
                for (size_t p = 0; p < splitPtr->PaneCount(); ++p) splitPtr->SetPaneWeight(p, 1.0);
                statusPtrE->SetText("Panes evened out.");
            };
            splitIcons->AddSplitterIcon(1, even);

            SplitPaneIcon shrink;
            shrink.id = "shrink-aside";
            shrink.iconPath = SplitIcon("angle-right.svg");
            shrink.tooltip = "Give the aside less room";
            shrink.onClick = [splitPtr, statusPtrE](size_t, size_t) {
                splitPtr->SetPaneWeight(2, splitPtr->GetPaneWeight(2) / 1.4);
                statusPtrE->SetText("Aside shrunk.");
            };
            splitIcons->AddSplitterIcon(1, shrink);

            // Fires after every icon's own handler - handy for logging.
            splitIcons->onSplitterIconClicked =
                [](size_t splitterIndex, size_t iconIndex, const SplitPaneIcon& icon) {
                    debugOutput << "SplitPane icon '" << icon.id << "' clicked (splitter "
                                << splitterIndex << ", icon " << iconIndex << ")" << std::endl;
                };
        }

        DemoKit::AddStretched(cardE, splitIcons);
        DemoKit::Add(cardE, statusE);

        // ============================================================
        // Section F: Vertical split with icons, no handle
        // ============================================================
        auto cardF = DemoKit::AddCard(root, "SplitCardF", "6. Vertical split: icons without a handle",
            "The same icons on a horizontal split line, with no handle behind them and the group parked "
            "at 15% along the line (SplitterIconStyle::position).");

        auto splitVIcons = CreateVerticalSplitPane("SplitPaneF", 0, 0, 960, 200);
        splitVIcons->SetBorders(1, Color(180, 180, 180, 255));

        auto vTop    = splitVIcons->AddPane(2.0);
        auto vBottom = splitVIcons->AddPane(1.0);
        splitVIcons->SetPaneMinSize(0, 60);
        splitVIcons->SetPaneMinSize(1, 40);

        {
            SplitPaneStyle st = splitVIcons->GetSplitPaneStyle();
            st.splitterThickness = 2;
            st.splitterColor = Color(150, 150, 150, 255);
            st.icons.size = 14;
            st.icons.spacing = 8;
            st.icons.position = 0.15f;
            splitVIcons->SetSplitPaneStyle(st);
        }

        SetupPane(vTop, Color(252, 252, 252, 255));
        AddPaneTitle(vTop, "pfTop", "Document");
        AddInfoLabel(vTop, "pfTopInfo", "The icons below sit directly on the line.");

        SetupPane(vBottom, Color(240, 240, 240, 255));
        AddPaneTitle(vBottom, "pfBottom", "Console");
        AddInfoLabel(vBottom, "pfBottomInfo", "One splitter, two icons, no handle.");

        {
            auto* splitPtr = splitVIcons.get();
            SplitPaneIcon up;
            up.id = "console-grow";
            up.iconPath = SplitIcon("arrow-up.svg");
            up.tooltip = "Grow the console";
            up.onClick = [splitPtr](size_t, size_t) {
                splitPtr->SetPaneWeight(1, splitPtr->GetPaneWeight(1) * 1.5);
            };
            splitVIcons->AddSplitterIcon(up);

            SplitPaneIcon down;
            down.id = "console-shrink";
            down.iconPath = SplitIcon("arrow-down.svg");
            down.tooltip = "Shrink the console";
            down.onClick = [splitPtr](size_t, size_t) {
                splitPtr->SetPaneWeight(1, splitPtr->GetPaneWeight(1) / 1.5);
            };
            splitVIcons->AddSplitterIcon(down);
        }

        DemoKit::AddStretched(cardF, splitVIcons);

        return root;
    }

} // namespace UltraCanvas
