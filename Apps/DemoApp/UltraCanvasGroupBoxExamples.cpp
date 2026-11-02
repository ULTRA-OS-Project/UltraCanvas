// Apps/DemoApp/UltraCanvasGroupBoxExamples.cpp
// Group Box (titled container) demonstration for the main demo app.
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): the boxes in
//                  wrapping rows inside cards, each as tall as its wrapped text
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasGroupBox.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include <sstream>

namespace UltraCanvas {

    // Helper: a small in-flow label that stacks inside a group box's content
    // area; it wraps, so a longer text makes the box taller.
    static std::shared_ptr<UltraCanvasLabel> ContentLabel(const std::string& id,
                                                           const std::string& text) {
        auto lbl = DemoKit::CreateText(id, text);
        lbl->SetTextColor(Color(70, 70, 70, 255));
        lbl->SetMargin(2.0f);
        return lbl;
    }

    // A group box built 300 px wide: it lays its children out in a column
    // (the box folds its caption into its own padding, so they start below
    // it), keeps 300 px as its least width, shares a row's spare width and is
    // as tall as its contents. A collapse sets a pixel height and an expand
    // restores this auto height.
    static std::shared_ptr<UltraCanvasGroupBox> CreateDemoBox(const std::string& id,
                                                             const std::string& title) {
        auto gb = CreateGroupBox(id, 300.0f, -1.0f, title);
        gb->size.height = CSSLayout::Dimension::Auto();
        gb->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        ContainerStyle plain;
        plain.autoShowScrollbars = false;
        gb->SetContainerStyle(plain);
        CSSLayout::BoxConstraints limits;
        limits.minWidth = CSSLayout::Dimension::Px(300.0f);
        gb->boxConstraints = limits;
        return gb;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateGroupBoxExamples() {
        auto page = DemoKit::CreatePage("GroupBoxExamples", "UltraCanvas Group Box Showcase",
                "Titled containers that frame child elements: framed, header and flat "
                "styles, title alignment, checkable and collapsible behaviour");

        // Status label for interaction feedback.
        auto statusLabel = DemoKit::AddText(page, "GroupBoxStatus",
                "Toggle the checkable group's checkbox, or click the collapsible group's title.");
        statusLabel->SetFontSize(11);
        statusLabel->SetBackgroundColor(Color(245, 245, 245, 255));
        statusLabel->SetBorders(1.0f, Color(220, 220, 220, 255));
        statusLabel->SetPadding(6.0f);

        auto stylesCard = DemoKit::AddCard(page, "GroupBoxStylesCard", "Frame styles and title alignment");
        auto stylesRow = DemoKit::AddRow(stylesCard, "GroupBoxStylesRow", 20);

        // ===== 1. FRAMED — TITLE LEFT (default) =====
        {
            auto gb = CreateDemoBox("gbFramedLeft", "Framed (title left)");
            gb->AddChild(ContentLabel("gbFL1", "Classic fieldset frame."));
            gb->AddChild(ContentLabel("gbFL2", "Caption sits in the top border."));
            gb->AddChild(ContentLabel("gbFL3", "Default title alignment: left."));
            DemoKit::Add(stylesRow, gb, 1);
        }

        // ===== 2. FRAMED — TITLE CENTER =====
        {
            auto gb = CreateDemoBox("gbFramedCenter", "Framed (title center)");
            gb->SetTitleAlignment(GroupBoxTitleAlignment::Center);
            gb->SetBorderColor(Color(120, 140, 200, 255));
            gb->AddChild(ContentLabel("gbFC1", "Same frame, centered caption."));
            gb->AddChild(ContentLabel("gbFC2", "Border gap follows the title."));
            DemoKit::Add(stylesRow, gb, 1);
        }

        // ===== 3. FRAMED — TITLE RIGHT =====
        {
            auto gb = CreateDemoBox("gbFramedRight", "Framed (title right)");
            gb->SetTitleAlignment(GroupBoxTitleAlignment::Right);
            gb->SetCornerRadius(10.0f);
            gb->SetGroupBackgroundColor(Color(252, 252, 250, 255));
            gb->AddChild(ContentLabel("gbFR1", "Rounded corners (radius 10)."));
            gb->AddChild(ContentLabel("gbFR2", "Right-aligned caption."));
            gb->AddChild(ContentLabel("gbFR3", "Light filled background."));
            DemoKit::Add(stylesRow, gb, 1);
        }

        // ===== 4. HEADER STYLE =====
        {
            auto gb = CreateDemoBox("gbHeader", "Header style");
            gb->SetFrameStyle(GroupBoxFrameStyle::Header);
            GroupBoxVisualStyle st = gb->GetVisualStyle();
            st.headerBackgroundColor = Color(235, 238, 245, 255);
            st.showHeaderSeparator = true;
            gb->SetVisualStyle(st);
            gb->AddChild(ContentLabel("gbH1", "Caption in a header strip."));
            gb->AddChild(ContentLabel("gbH2", "Full border + separator line."));
            DemoKit::Add(stylesRow, gb, 1);
        }

        // ===== 5. FLAT STYLE =====
        {
            auto gb = CreateDemoBox("gbFlat", "Flat style");
            gb->SetFrameStyle(GroupBoxFrameStyle::Flat);
            gb->SetGroupBackgroundColor(Color(248, 248, 248, 255));
            gb->AddChild(ContentLabel("gbFlat1", "No border, just a caption strip."));
            gb->AddChild(ContentLabel("gbFlat2", "Optional separator under title."));
            DemoKit::Add(stylesRow, gb, 1);
        }

        // ===== 6. CARD PRESET + INFO ICON =====
        {
            auto gb = CreateDemoBox("gbCard", "GROUPBOX");
            gb->SetFrameStyle(GroupBoxFrameStyle::Header);
            gb->SetVisualStyle(GroupBoxVisualStyle::Card());
            // Info icon on the right reveals help text on hover.
            gb->SetHelpText("This card uses the GroupBoxVisualStyle::Card() preset:\n"
                            "filled rounded background, bold header and a separator.");
            gb->AddChild(ContentLabel("gbCard1", "iOS-like grouped card preset."));
            gb->AddChild(ContentLabel("gbCard2", "Hover the info icon for help."));
            DemoKit::Add(stylesRow, gb, 1);
        }

        auto behaviourCard = DemoKit::AddCard(page, "GroupBoxBehaviourCard",
                "Checkable, switch and collapsible groups");
        auto behaviourRow = DemoKit::AddRow(behaviourCard, "GroupBoxBehaviourRow", 20);

        // ===== 7. CHECKABLE — CHECKBOX ACTIVATOR (left) =====
        {
            auto gb = CreateDemoBox("gbCheckable", "Checkable (checkbox, left)");
            gb->SetCheckable(true);
            gb->SetChecked(true);
            // Built with their text only, so each is as wide as its label.
            auto cb1 = std::make_shared<UltraCanvasCheckbox>("gbChkA", "Enable notifications");
            auto cb2 = std::make_shared<UltraCanvasCheckbox>("gbChkB", "Play sound");
            cb1->SetMargin(2.0f);
            cb2->SetMargin(2.0f);
            gb->AddChild(cb1);
            gb->AddChild(cb2);
            gb->onCheckedChanged = [statusLabel](bool checked) {
                std::ostringstream oss;
                oss << "Checkbox group " << (checked ? "enabled" : "disabled")
                    << " — its contents are " << (checked ? "active." : "greyed out.");
                statusLabel->SetText(oss.str());
            };
            DemoKit::Add(behaviourRow, gb, 1);
        }

        // ===== 8. ACTIVATED BY A SWITCH (right) + INFO ICON =====
        {
            auto gb = CreateDemoBox("gbSwitch", "Settings");
            gb->SetFrameStyle(GroupBoxFrameStyle::Header);
            // Switch-style activator on the RIGHT side of the title bar.
            gb->EnableActivatorSwitch(GroupBoxIndicatorSide::Right);
            gb->SetChecked(true);
            // Info icon on the LEFT (so it doesn't collide with the switch).
            gb->SetInfoIconSide(GroupBoxIndicatorSide::Left);
            gb->SetHelpText("Toggle the switch on the right to enable or disable\n"
                            "this section. The contents grey out when off.");
            gb->AddChild(ContentLabel("gbSw1", "Private account"));
            gb->AddChild(ContentLabel("gbSw2", "Show activity status"));
            gb->onCheckedChanged = [statusLabel](bool on) {
                statusLabel->SetText(on ? "Switch ON — settings enabled."
                                        : "Switch OFF — settings disabled.");
            };
            DemoKit::Add(behaviourRow, gb, 1);
        }

        // ===== 9. COLLAPSIBLE =====
        {
            auto gb = CreateDemoBox("gbCollapsible", "Collapsible group");
            gb->SetCollapsible(true);
            gb->AddChild(ContentLabel("gbCol1", "Click the title to collapse."));
            gb->AddChild(ContentLabel("gbCol2", "The arrow shows the state."));
            gb->AddChild(ContentLabel("gbCol3", "Content hides when collapsed."));
            gb->onCollapsedChanged = [statusLabel](bool collapsed) {
                statusLabel->SetText(collapsed ? "Collapsible group collapsed."
                                               : "Collapsible group expanded.");
            };
            // At the top of the row, so a collapsed box shrinks to its title
            // instead of being stretched to its neighbours' height.
            DemoKit::Add(behaviourRow, gb, 1);
            gb->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Start);
        }

        return page;
    }

} // namespace UltraCanvas
