// Apps/DemoApp/UltraCanvasSplitPaneExamples.cpp
// Demonstrates UltraCanvasSplitPane: horizontal, vertical, nested splits,
// splitter handles and action icons on the split line
// Version: 1.1.0
// Last Modified: 2026-08-01
// Author: UltraCanvas Framework

#include "UltraCanvasContainer.h"
#include "UltraCanvasSpacer.h"
#include "CSSLayout/CSSLayout.h"
#include "UltraCanvasDemo.h"
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
        std::shared_ptr<UltraCanvasLabel> MakeSectionTitle(float x, float y, const std::string& text) {
            auto t = std::make_shared<UltraCanvasLabel>("SplitSecTitle" + std::to_string(x) + "_" + std::to_string(y),
                                                       x, y, 900, 25);
            t->SetText(text);
            t->SetFontSize(14);
            t->SetFontWeight(FontWeight::Bold);
            t->SetTextColor(Color(50, 50, 150, 255));
            return t;
        }

        std::shared_ptr<UltraCanvasLabel> MakeDescription(float x, float y, float w, const std::string& text) {
            auto d = std::make_shared<UltraCanvasLabel>("SplitDesc" + std::to_string(x) + "_" + std::to_string(y),
                                                       x, y, w, 0);
            d->SetText(text);
            d->SetTextColor(Color(80, 80, 80, 255));
            d->SetFontSize(12);
            d->SetWrap(TextWrap::WrapWord);
            return d;
        }

        std::shared_ptr<UltraCanvasLabel> MakePaneTitle(const std::string& id, const std::string& text) {
            auto l = std::make_shared<UltraCanvasLabel>(id, 10, 8, 200, 22);
            l->SetText(text);
            l->SetFontSize(13);
            l->SetFontWeight(FontWeight::Bold);
            l->SetTextColor(Color(40, 40, 40, 255));
            return l;
        }

        std::shared_ptr<UltraCanvasLabel> MakeInfoLabel(const std::string& id, float x, float y, float w,
                                                       const std::string& text, int fontSize = 12) {
            auto l = std::make_shared<UltraCanvasLabel>(id, x, y, w, 20);
            l->SetText(text);
            l->SetFontSize(fontSize);
            l->SetTextColor(Color(60, 60, 60, 255));
            return l;
        }

        std::string SplitIcon(const std::string& name) {
            return NormalizePath(GetResourcesDir() + "media/icons/" + name);
        }
    } // namespace

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateSplitPaneExamples() {
        auto root = std::make_shared<UltraCanvasContainer>("SplitPaneExamples", 0, 0, 1020, 1240);
        root->SetBackgroundColor(Colors::White);
        root->SetPadding(0, 0, 10, 0);

        long currentY = 10;

        // ===== MAIN TITLE =====
        auto mainTitle = std::make_shared<UltraCanvasLabel>("SplitMainTitle", 20, currentY, 900, 30);
        mainTitle->SetText("UltraCanvas Split Pane Examples");
        mainTitle->SetFontSize(18);
        mainTitle->SetFontWeight(FontWeight::Bold);
        root->AddChild(mainTitle);
        currentY += 40;

        root->AddChild(MakeDescription(20, currentY, 960,
            "Drag the splitter bars to resize panes. Weights are proportional, so resizing the SplitPane "
            "preserves relative sizes; per-pane min/max sizes clamp the drag."));
        currentY += 50;

        // ============================================================
        // Section A: Horizontal Split (3 panes, weighted)
        // ============================================================
        root->AddChild(MakeSectionTitle(20, currentY, "1. Horizontal Split (3 panes, weights 1 : 2 : 1)"));
        currentY += 30;

        root->AddChild(MakeDescription(20, currentY, 960,
            "Three panes side-by-side. Dragging splitter 0 affects only Sidebar+Editor; dragging splitter 1 "
            "affects only Editor+Inspector. Each pane has a minimum width of 80 px."));
        currentY += 50;

        auto statusA = std::make_shared<UltraCanvasLabel>("SplitAStatus", 20, currentY + 245, 960, 20);
        statusA->SetText("Weights: 1.00 : 2.00 : 1.00");
        statusA->SetFontSize(11);
        statusA->SetTextColor(Color(0, 100, 200, 255));

        auto splitH = CreateHorizontalSplitPane("SplitPaneA", 20, currentY, 960, 240);
        splitH->SetBorders(1, Color(180, 180, 180, 255));

        auto paneSidebar   = splitH->AddPane(1.0);
        auto paneEditor    = splitH->AddPane(2.0);
        auto paneInspector = splitH->AddPane(1.0);

        splitH->SetPaneMinSize(0, 80);
        splitH->SetPaneMinSize(1, 80);
        splitH->SetPaneMinSize(2, 80);

        paneSidebar->SetBackgroundColor(Color(225, 235, 250, 255));
        paneSidebar->SetPadding(8);
        paneSidebar->AddChild(MakePaneTitle("paSidebarTitle", "Sidebar"));
        {
            auto b1 = std::make_shared<UltraCanvasButton>("paSidebarBtn1", 10, 38, 130, 28);
            b1->SetText("Files");
            auto b2 = std::make_shared<UltraCanvasButton>("paSidebarBtn2", 10, 72, 130, 28);
            b2->SetText("Search");
            auto b3 = std::make_shared<UltraCanvasButton>("paSidebarBtn3", 10, 106, 130, 28);
            b3->SetText("Git");
            paneSidebar->AddChild(b1);
            paneSidebar->AddChild(b2);
            paneSidebar->AddChild(b3);
        }

        paneEditor->SetBackgroundColor(Color(252, 252, 252, 255));
        paneEditor->SetPadding(8);
        paneEditor->AddChild(MakePaneTitle("paEditorTitle", "Editor"));
        paneEditor->AddChild(MakeInfoLabel("paEditorCode1", 10, 40,  500, "int main() {",                            12));
        paneEditor->AddChild(MakeInfoLabel("paEditorCode2", 10, 60,  500, "    std::cout << \"Hello, SplitPane\\n\";", 12));
        paneEditor->AddChild(MakeInfoLabel("paEditorCode3", 10, 80,  500, "    return 0;",                            12));
        paneEditor->AddChild(MakeInfoLabel("paEditorCode4", 10, 100, 500, "}",                                        12));
        paneEditor->AddChild(MakeInfoLabel("paEditorHint",  10, 140, 500, "(Drag the splitters at left/right to resize.)", 11));

        paneInspector->SetBackgroundColor(Color(245, 245, 245, 255));
        paneInspector->SetPadding(8);
        paneInspector->AddChild(MakePaneTitle("paInspTitle", "Inspector"));
        paneInspector->AddChild(MakeInfoLabel("paInspRow1", 10, 40,  220, "Type:        Container"));
        paneInspector->AddChild(MakeInfoLabel("paInspRow2", 10, 62,  220, "Children:    3"));
        paneInspector->AddChild(MakeInfoLabel("paInspRow3", 10, 84,  220, "Visible:     true"));
        paneInspector->AddChild(MakeInfoLabel("paInspRow4", 10, 106, 220, "Z-order:     0"));

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

        root->AddChild(splitH);
        root->AddChild(statusA);
        currentY += 240 + 30;

        // ============================================================
        // Section B: Vertical Split (2 panes)
        // ============================================================
        root->AddChild(MakeSectionTitle(20, currentY, "2. Vertical Split (2 panes, weights 3 : 1)"));
        currentY += 30;

        root->AddChild(MakeDescription(20, currentY, 960,
            "An editor area on top and a 'terminal' panel below. Drag the horizontal splitter; "
            "neither pane can shrink below its minimum (100 px / 60 px)."));
        currentY += 50;

        auto splitV = CreateVerticalSplitPane("SplitPaneB", 20, currentY, 960, 280);
        splitV->SetBorders(1, Color(180, 180, 180, 255));

        auto paneEditorV = splitV->AddPane(3.0);
        auto paneTerm    = splitV->AddPane(1.0);
        splitV->SetPaneMinSize(0, 100);
        splitV->SetPaneMinSize(1, 60);

        paneEditorV->SetBackgroundColor(Color(252, 252, 252, 255));
        paneEditorV->SetPadding(8);
        paneEditorV->AddChild(MakePaneTitle("pbEditorTitle", "Editor area"));
        paneEditorV->AddChild(MakeInfoLabel("pbEditorL1", 10, 40, 900, "// Drag the horizontal splitter below.", 12));
        paneEditorV->AddChild(MakeInfoLabel("pbEditorL2", 10, 60, 900, "for (int i = 0; i < 10; ++i) {",          12));
        paneEditorV->AddChild(MakeInfoLabel("pbEditorL3", 10, 80, 900, "    std::cout << i << \"\\n\";",         12));
        paneEditorV->AddChild(MakeInfoLabel("pbEditorL4", 10,100, 900, "}",                                       12));

        paneTerm->SetBackgroundColor(Color(30, 30, 30, 255));
        paneTerm->SetPadding(8);
        {
            auto title = MakePaneTitle("pbTermTitle", "Terminal");
            title->SetTextColor(Color(220, 220, 220, 255));
            paneTerm->AddChild(title);

            auto line1 = MakeInfoLabel("pbTerm1", 10, 36, 900, "$ ./build.sh --release");
            line1->SetTextColor(Color(200, 240, 200, 255));
            auto line2 = MakeInfoLabel("pbTerm2", 10, 56, 900, "[100%] Built target UltraCanvasDemo");
            line2->SetTextColor(Color(200, 200, 200, 255));
            auto line3 = MakeInfoLabel("pbTerm3", 10, 76, 900, "$ _");
            line3->SetTextColor(Color(200, 240, 200, 255));
            paneTerm->AddChild(line1);
            paneTerm->AddChild(line2);
            paneTerm->AddChild(line3);
        }

        root->AddChild(splitV);
        currentY += 280 + 20;

        // ============================================================
        // Section C: Nested Splits (VSCode-like)
        // ============================================================
        root->AddChild(MakeSectionTitle(20, currentY, "3. Nested Splits (horizontal containing a vertical split)"));
        currentY += 30;

        root->AddChild(MakeDescription(20, currentY, 960,
            "Outer horizontal split: left sidebar pane + right pane. The right pane contains a vertical "
            "split (editor on top, output below). Drag outer/inner splitters independently."));
        currentY += 50;

        auto outer = CreateHorizontalSplitPane("SplitPaneC", 20, currentY, 960, 320);
        outer->SetBorders(1, Color(180, 180, 180, 255));

        auto outerLeft  = outer->AddPane(1.0);
        auto outerRight = outer->AddPane(3.0);
        outer->SetPaneMinSize(0, 100);
        outer->SetPaneMinSize(1, 200);

        outerLeft->SetBackgroundColor(Color(225, 235, 250, 255));
        outerLeft->SetPadding(8);
        outerLeft->AddChild(MakePaneTitle("pcSidebar", "Sidebar"));
        outerLeft->AddChild(MakeInfoLabel("pcSidebarA", 10, 40, 200, "Project files"));
        outerLeft->AddChild(MakeInfoLabel("pcSidebarB", 10, 62, 200, "Search"));
        outerLeft->AddChild(MakeInfoLabel("pcSidebarC", 10, 84, 200, "Source control"));

        // Build nested vertical split; let a BoxLayout on outerRight stretch it to fill.
        auto inner = CreateVerticalSplitPane("SplitPaneCInner", 0, 0, 100, 100);
        auto innerTop    = inner->AddPane(3.0);
        auto innerBottom = inner->AddPane(1.0);
        inner->SetPaneMinSize(0, 80);
        inner->SetPaneMinSize(1, 60);

        innerTop->SetBackgroundColor(Color(252, 252, 252, 255));
        innerTop->SetPadding(8);
        innerTop->AddChild(MakePaneTitle("pcInnerEditor", "Editor"));
        innerTop->AddChild(MakeInfoLabel("pcInnerE1", 10, 40, 380, "auto pane = split->AddPane(1.0);",        12));
        innerTop->AddChild(MakeInfoLabel("pcInnerE2", 10, 60, 380, "pane->SetBackgroundColor(Colors::White);", 12));
        innerTop->AddChild(MakeInfoLabel("pcInnerE3", 10, 80, 380, "pane->AddChild(makeWidget());",            12));

        innerBottom->SetBackgroundColor(Color(245, 245, 245, 255));
        innerBottom->SetPadding(8);
        innerBottom->AddChild(MakePaneTitle("pcInnerOut", "Output"));
        innerBottom->AddChild(MakeInfoLabel("pcInnerO1", 10, 36, 380, "[info] split created with 2 panes"));
        innerBottom->AddChild(MakeInfoLabel("pcInnerO2", 10, 56, 380, "[info] weights normalized"));

        outerRight->layout.SetFlexColumn();
        outerRight->layout.SetFlexGap(0);
        outerRight->AddChild(inner);
        inner->layoutItem.SetFlexGrow(1)
                        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        root->AddChild(outer);
        currentY += 320 + 20;

        // ============================================================
        // Section D: Splitter handles
        // ============================================================
        root->AddChild(MakeSectionTitle(20, currentY, "4. Splitter handle (shape and size)"));
        currentY += 30;

        root->AddChild(MakeDescription(20, currentY, 960,
            "An optional grab handle sits on the split line. Pick a shape - square, square with rounded "
            "edges, or round (a circle when square, a capsule when elongated) - and a size across the line. "
            "The splitter strip widens to fit the handle; the split line itself stays as thin as configured. "
            "Use the buttons to switch shape and size live."));
        currentY += 70;

        auto splitHandles = CreateHorizontalSplitPane("SplitPaneD", 20, currentY, 960, 200);
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

        handleLeft->SetBackgroundColor(Color(232, 240, 252, 255));
        handleLeft->SetPadding(8);
        handleLeft->AddChild(MakePaneTitle("pdLeft", "Left"));
        handleLeft->AddChild(MakeInfoLabel("pdLeftInfo", 10, 40, 240, "Grab the handle or the line."));

        handleMid->SetBackgroundColor(Color(252, 252, 252, 255));
        handleMid->SetPadding(8);
        handleMid->AddChild(MakePaneTitle("pdMid", "Middle"));
        handleMid->AddChild(MakeInfoLabel("pdMidInfo1", 10, 40, 240, "handle.shape"));
        handleMid->AddChild(MakeInfoLabel("pdMidInfo2", 10, 60, 240, "handle.crossSize"));
        handleMid->AddChild(MakeInfoLabel("pdMidInfo3", 10, 80, 240, "handle.axisLength"));

        handleRight->SetBackgroundColor(Color(245, 245, 245, 255));
        handleRight->SetPadding(8);
        handleRight->AddChild(MakePaneTitle("pdRight", "Right"));
        handleRight->AddChild(MakeInfoLabel("pdRightInfo", 10, 40, 240, "Both splitters share one style."));

        root->AddChild(splitHandles);
        currentY += 200 + 10;

        {
            auto splitPtr = splitHandles.get();
            auto applyHandle = [splitPtr](SplitterHandleShape shape, int crossSize, int axisLength) {
                SplitterHandleStyle hs = splitPtr->GetSplitterHandleStyle();
                hs.shape = shape;
                hs.crossSize = crossSize;
                hs.axisLength = axisLength;
                splitPtr->SetSplitterHandleStyle(hs);
            };

            struct HandleChoice {
                const char* text;
                SplitterHandleShape shape;
                int crossSize;
                int axisLength;
            };
            const HandleChoice choices[] = {
                { "None",          SplitterHandleShape::NoHandle,      14,  0 },
                { "Square",        SplitterHandleShape::Square,        14, 48 },
                { "Rounded",       SplitterHandleShape::RoundedSquare, 14, 48 },
                { "Capsule",       SplitterHandleShape::Round,         14, 48 },
                { "Circle 22px",   SplitterHandleShape::Round,         22, 22 },
                { "Rounded 20px",  SplitterHandleShape::RoundedSquare, 20, 64 },
            };

            float bx = 20;
            int n = 0;
            for (const auto& c : choices) {
                auto b = std::make_shared<UltraCanvasButton>("pdHandleBtn" + std::to_string(n++),
                                                            bx, currentY, 110, 26);
                b->SetText(c.text);
                SplitterHandleShape shape = c.shape;
                int cs = c.crossSize;
                int al = c.axisLength;
                b->SetOnClick([applyHandle, shape, cs, al]() { applyHandle(shape, cs, al); });
                root->AddChild(b);
                bx += 118;
            }
        }
        currentY += 26 + 25;

        // ============================================================
        // Section E: Action icons on the split line
        // ============================================================
        root->AddChild(MakeSectionTitle(20, currentY, "5. Action icons on the split line"));
        currentY += 30;

        root->AddChild(MakeDescription(20, currentY, 960,
            "Any number of icons can sit on a split line, centred across it and grouped at the middle "
            "along it. Each icon has its own tooltip, enabled state and click handler, and clicking one "
            "never starts a drag. Here splitter 0 collapses/restores the sidebar and splitter 1 carries "
            "three actions. The handle wraps the icon group."));
        currentY += 70;

        auto statusE = std::make_shared<UltraCanvasLabel>("SplitEStatus", 20, currentY + 235, 960, 20);
        statusE->SetText("Click an icon on a split line.");
        statusE->SetFontSize(11);
        statusE->SetTextColor(Color(0, 100, 200, 255));

        auto splitIcons = CreateHorizontalSplitPane("SplitPaneE", 20, currentY, 960, 230);
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

        iconSidebar->SetBackgroundColor(Color(232, 240, 252, 255));
        iconSidebar->SetPadding(8);
        iconSidebar->AddChild(MakePaneTitle("peSidebar", "Sidebar"));
        iconSidebar->AddChild(MakeInfoLabel("peSidebarInfo", 10, 40, 200, "Collapse me with the icon."));

        iconMain->SetBackgroundColor(Color(252, 252, 252, 255));
        iconMain->SetPadding(8);
        iconMain->AddChild(MakePaneTitle("peMain", "Main"));
        iconMain->AddChild(MakeInfoLabel("peMainInfo1", 10, 40, 500, "splitIcons->AddSplitterIcon(0, icon);"));
        iconMain->AddChild(MakeInfoLabel("peMainInfo2", 10, 60, 500, "icon.onClick = [](size_t s, size_t i) { ... };"));

        iconAside->SetBackgroundColor(Color(245, 245, 245, 255));
        iconAside->SetPadding(8);
        iconAside->AddChild(MakePaneTitle("peAside", "Aside"));
        iconAside->AddChild(MakeInfoLabel("peAsideInfo", 10, 40, 200, "Three icons on the right line."));

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

        root->AddChild(splitIcons);
        root->AddChild(statusE);
        currentY += 230 + 30;

        // ============================================================
        // Section F: Vertical split with icons, no handle
        // ============================================================
        root->AddChild(MakeSectionTitle(20, currentY, "6. Vertical split: icons without a handle"));
        currentY += 30;

        root->AddChild(MakeDescription(20, currentY, 960,
            "The same icons on a horizontal split line, with no handle behind them and the group parked "
            "at 15% along the line (SplitterIconStyle::position)."));
        currentY += 50;

        auto splitVIcons = CreateVerticalSplitPane("SplitPaneF", 20, currentY, 960, 200);
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

        vTop->SetBackgroundColor(Color(252, 252, 252, 255));
        vTop->SetPadding(8);
        vTop->AddChild(MakePaneTitle("pfTop", "Document"));
        vTop->AddChild(MakeInfoLabel("pfTopInfo", 10, 40, 500, "The icons below sit directly on the line."));

        vBottom->SetBackgroundColor(Color(240, 240, 240, 255));
        vBottom->SetPadding(8);
        vBottom->AddChild(MakePaneTitle("pfBottom", "Console"));
        vBottom->AddChild(MakeInfoLabel("pfBottomInfo", 10, 36, 500, "One splitter, two icons, no handle."));

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

        root->AddChild(splitVIcons);
        currentY += 200 + 20;

        // Trim the root container to its actual content height
        root->SetHeight(currentY + 20);

        return root;
    }

} // namespace UltraCanvas
