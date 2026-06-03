// Apps/DemoApp/UltraCanvasSplitPaneExamples.cpp
// Demonstrates UltraCanvasSplitPane: horizontal, vertical, and nested splits
// Version: 1.0.1
// Last Modified: 2026-06-01
// Author: UltraCanvas Framework

#include "UltraCanvasContainer.h"
#include "UltraCanvasSpacer.h"
#include "CSSLayout/CSSLayout.h"
#include "UltraCanvasDemo.h"
#include "UltraCanvasSplitPane.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
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

        // Trim the root container to its actual content height
        root->SetHeight(currentY + 20);

        return root;
    }

} // namespace UltraCanvas
