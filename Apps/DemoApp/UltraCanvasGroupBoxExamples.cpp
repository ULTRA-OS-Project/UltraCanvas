// Apps/DemoApp/UltraCanvasGroupBoxExamples.cpp
// Group Box (titled container) demonstration for the main demo app.
// Version: 1.0.0
// Last Modified: 2026-06-15
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasGroupBox.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include <sstream>

namespace UltraCanvas {

    // Helper: a small in-flow label that stacks inside a group box's content area.
    static std::shared_ptr<UltraCanvasLabel> ContentLabel(const std::string& id,
                                                           const std::string& text) {
        auto lbl = std::make_shared<UltraCanvasLabel>(id, 240.0f, 22.0f, text);
        lbl->SetFontSize(12);
        lbl->SetTextColor(Color(70, 70, 70, 255));
        lbl->SetMargin(2.0f);
        return lbl;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateGroupBoxExamples() {
        auto mainContainer = std::make_shared<UltraCanvasContainer>("GroupBoxExamples", 0, 0, 1000, 1000);

        // ===== PAGE TITLE =====
        auto title = std::make_shared<UltraCanvasLabel>("GroupBoxTitle", 20, 10, 700, 35);
        title->SetText("UltraCanvas Group Box Showcase");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        mainContainer->AddChild(title);

        auto subtitle = std::make_shared<UltraCanvasLabel>("GroupBoxSubtitle", 20, 45, 900, 25);
        subtitle->SetText("Titled containers that frame child elements: framed, header and flat "
                          "styles, title alignment, checkable and collapsible behaviour");
        subtitle->SetFontSize(12);
        subtitle->SetTextColor(Color(100, 100, 100, 255));
        mainContainer->AddChild(subtitle);

        // Status label for interaction feedback.
        auto statusLabel = std::make_shared<UltraCanvasLabel>("GroupBoxStatus", 20, 75, 920, 24);
        statusLabel->SetText("Toggle the checkable group's checkbox, or click the collapsible group's title.");
        statusLabel->SetFontSize(11);
        statusLabel->SetBackgroundColor(Color(245, 245, 245, 255));
        statusLabel->SetBorders(1.0f, Color(220, 220, 220, 255));
        statusLabel->SetPadding(6.0f);
        mainContainer->AddChild(statusLabel);

        const float boxW = 300.0f;
        const float boxH = 130.0f;
        const float colX[2] = {30.0f, 360.0f};
        float rowY = 115.0f;

        // ===== 1. FRAMED — TITLE LEFT (default) =====
        {
            auto gb = CreateGroupBox("gbFramedLeft", colX[0], rowY, boxW, boxH, "Framed (title left)");
            gb->AddChild(ContentLabel("gbFL1", "Classic fieldset frame."));
            gb->AddChild(ContentLabel("gbFL2", "Caption sits in the top border."));
            gb->AddChild(ContentLabel("gbFL3", "Default title alignment: left."));
            mainContainer->AddChild(gb);
        }

        // ===== 2. FRAMED — TITLE CENTER =====
        {
            auto gb = CreateGroupBox("gbFramedCenter", colX[1], rowY, boxW, boxH, "Framed (title center)");
            gb->SetTitleAlignment(GroupBoxTitleAlignment::Center);
            gb->SetBorderColor(Color(120, 140, 200, 255));
            gb->AddChild(ContentLabel("gbFC1", "Same frame, centered caption."));
            gb->AddChild(ContentLabel("gbFC2", "Border gap follows the title."));
            mainContainer->AddChild(gb);
        }
        rowY += boxH + 20.0f;

        // ===== 3. FRAMED — TITLE RIGHT =====
        {
            auto gb = CreateGroupBox("gbFramedRight", colX[0], rowY, boxW, boxH, "Framed (title right)");
            gb->SetTitleAlignment(GroupBoxTitleAlignment::Right);
            gb->SetCornerRadius(10.0f);
            gb->SetGroupBackgroundColor(Color(252, 252, 250, 255));
            gb->AddChild(ContentLabel("gbFR1", "Rounded corners (radius 10)."));
            gb->AddChild(ContentLabel("gbFR2", "Right-aligned caption."));
            gb->AddChild(ContentLabel("gbFR3", "Light filled background."));
            mainContainer->AddChild(gb);
        }

        // ===== 4. HEADER STYLE =====
        {
            auto gb = CreateGroupBox("gbHeader", colX[1], rowY, boxW, boxH, "Header style");
            gb->SetFrameStyle(GroupBoxFrameStyle::Header);
            GroupBoxVisualStyle st = gb->GetVisualStyle();
            st.headerBackgroundColor = Color(235, 238, 245, 255);
            st.showHeaderSeparator = true;
            gb->SetVisualStyle(st);
            gb->AddChild(ContentLabel("gbH1", "Caption in a header strip."));
            gb->AddChild(ContentLabel("gbH2", "Full border + separator line."));
            mainContainer->AddChild(gb);
        }
        rowY += boxH + 20.0f;

        // ===== 5. FLAT STYLE =====
        {
            auto gb = CreateGroupBox("gbFlat", colX[0], rowY, boxW, boxH, "Flat style");
            gb->SetFrameStyle(GroupBoxFrameStyle::Flat);
            gb->SetGroupBackgroundColor(Color(248, 248, 248, 255));
            gb->AddChild(ContentLabel("gbFlat1", "No border, just a caption strip."));
            gb->AddChild(ContentLabel("gbFlat2", "Optional separator under title."));
            mainContainer->AddChild(gb);
        }

        // ===== 6. CARD PRESET + INFO ICON =====
        {
            auto gb = CreateGroupBox("gbCard", colX[1], rowY, boxW, boxH, "GROUPBOX");
            gb->SetFrameStyle(GroupBoxFrameStyle::Header);
            gb->SetVisualStyle(GroupBoxVisualStyle::Card());
            // Info icon on the right reveals help text on hover.
            gb->SetHelpText("This card uses the GroupBoxVisualStyle::Card() preset:\n"
                            "filled rounded background, bold header and a separator.");
            gb->AddChild(ContentLabel("gbCard1", "iOS-like grouped card preset."));
            gb->AddChild(ContentLabel("gbCard2", "Hover the ⓘ icon for help."));
            mainContainer->AddChild(gb);
        }
        rowY += boxH + 20.0f;

        // ===== 7. CHECKABLE — CHECKBOX ACTIVATOR (left) =====
        {
            auto gb = CreateGroupBox("gbCheckable", colX[0], rowY, boxW, boxH, "Checkable (checkbox, left)");
            gb->SetCheckable(true);
            gb->SetChecked(true);
            auto cb1 = std::make_shared<UltraCanvasCheckbox>("gbChkA", 240.0f, 24.0f, "Enable notifications");
            auto cb2 = std::make_shared<UltraCanvasCheckbox>("gbChkB", 240.0f, 24.0f, "Play sound");
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
            mainContainer->AddChild(gb);
        }

        // ===== 8. ACTIVATED BY A SWITCH (right) + INFO ICON =====
        {
            auto gb = CreateGroupBox("gbSwitch", colX[1], rowY, boxW, boxH, "Settings");
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
            mainContainer->AddChild(gb);
        }
        rowY += boxH + 20.0f;

        // ===== 9. COLLAPSIBLE =====
        {
            auto gb = CreateGroupBox("gbCollapsible", colX[0], rowY, boxW, boxH, "Collapsible group");
            gb->SetCollapsible(true);
            gb->AddChild(ContentLabel("gbCol1", "Click the title to collapse."));
            gb->AddChild(ContentLabel("gbCol2", "The arrow shows the state."));
            gb->AddChild(ContentLabel("gbCol3", "Content hides when collapsed."));
            gb->onCollapsedChanged = [statusLabel](bool collapsed) {
                statusLabel->SetText(collapsed ? "Collapsible group collapsed."
                                               : "Collapsible group expanded.");
            };
            mainContainer->AddChild(gb);
        }

        return mainContainer;
    }

} // namespace UltraCanvas
