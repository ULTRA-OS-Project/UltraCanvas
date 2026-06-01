// Apps/DemoApp/UltraCanvasLayoutExamples.cpp
// Layout system demonstration examples for UltraCanvas Demo Application
// Version: 2.0.0
// Last Modified: 2026-06-01
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasSpacer.h"
#include "CSSLayout/CSSLayout.h"
#include <sstream>

namespace UltraCanvas {

// Helper function to create section title
    std::shared_ptr<UltraCanvasLabel> CreateLayoutSectionTitle(float x, float y, const std::string& text) {
        auto title = std::make_shared<UltraCanvasLabel>("LayoutSecTitle" + std::to_string(x), x, y, 600, 25);
        title->SetText(text);
        title->SetFontSize(14);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        return title;
    }

// Helper function to create description label
    std::shared_ptr<UltraCanvasLabel> CreateLayoutDescription(float x, float y, float width, const std::string& text) {
        auto desc = std::make_shared<UltraCanvasLabel>("LayoutDesc" + std::to_string(x), x, y, width, 0);
        desc->SetText(text);
        desc->SetTextColor(Color(80, 80, 80, 255));
        desc->SetFontSize(12);
        desc->SetWrap(TextWrap::WrapWord);
        return desc;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateLayoutExamples() {
        auto mainContainer = std::make_shared<UltraCanvasContainer>("LayoutExamples", 0, 0, 1020, 1670);
        mainContainer->SetBackgroundColor(Colors::White);
        mainContainer->SetPadding(0,0,10,0);

        long currentY = 10;

        // ===== MAIN TITLE =====
        auto mainTitle = std::make_shared<UltraCanvasLabel>("LayoutMainTitle", 20, currentY, 900, 30);
        mainTitle->SetText("UltraCanvas Layout System Examples");
        mainTitle->SetFontSize(18);
        mainTitle->SetFontWeight(FontWeight::Bold);
        mainContainer->AddChild(mainTitle);

        currentY += 40;

        // Description
        auto description = CreateLayoutDescription(20, currentY, 960,
                                                   "Comprehensive examples of Box, Grid, and Flex layouts with various configurations and use cases.");
        mainContainer->AddChild(description);

        currentY += 70;

        // ===== SECTION 1: VERTICAL BOX LAYOUT =====
        mainContainer->AddChild(CreateLayoutSectionTitle(20, currentY, "1. Vertical Box Layout"));
        currentY += 30;

        auto vboxDesc = CreateLayoutDescription(20, currentY, 960,
                                                "Vertical arrangement with spacing, padding, and stretch. Buttons respond to clicks.");
        mainContainer->AddChild(vboxDesc);
        currentY += 60;

        // Create demo container for vertical layout
        auto vboxDemo = std::make_shared<UltraCanvasContainer>("VBoxDemo", 20, currentY, 300, 200);
        vboxDemo->SetBackgroundColor(Color(245, 245, 250, 255));
        vboxDemo->SetPadding(15);

        vboxDemo->layout.SetFlexColumn().SetFlexGap(10);

        auto vboxBtn1 = std::make_shared<UltraCanvasButton>("VBtn1", 0, 0, 150, 35);
        vboxBtn1->SetText("Button 1");

        auto vboxBtn2 = std::make_shared<UltraCanvasButton>("VBtn2", 0, 0, 150, 35);
        vboxBtn2->SetText("Button 2");

        auto vboxBtn3 = std::make_shared<UltraCanvasButton>("VBtn3", 0, 0, 150, 35);
        vboxBtn3->SetText("Button 3");

        auto vboxStatus = std::make_shared<UltraCanvasLabel>("VStatus", 0, 0, 150, 25);
        vboxStatus->SetText("Click any button");
        vboxStatus->SetTextColor(Color(0, 100, 200, 255));
        vboxStatus->SetFontSize(11);

        vboxBtn1->SetOnClick([vboxStatus]() { vboxStatus->SetText("Button 1 clicked!"); });
        vboxBtn2->SetOnClick([vboxStatus]() { vboxStatus->SetText("Button 2 clicked!"); });
        vboxBtn3->SetOnClick([vboxStatus]() { vboxStatus->SetText("Button 3 clicked!"); });

        vboxDemo->AddChild(vboxBtn1);
        vboxBtn1->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
        vboxDemo->AddChild(vboxBtn2);
        vboxBtn2->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
        vboxDemo->AddChild(vboxBtn3);
        vboxBtn3->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
        vboxDemo->AddStretchSpacer(1);
        vboxDemo->AddChild(vboxStatus);
        vboxStatus->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);

        mainContainer->AddChild(vboxDemo);

        // Code explanation
        auto vboxCode = CreateLayoutDescription(340, currentY, 640,
                                                "Code: container->layout.SetFlexColumn().SetFlexGap(10);\ncontainer->AddChild(button1); container->AddStretchSpacer(1);");
        mainContainer->AddChild(vboxCode);

        currentY += 250;

        // ===== SECTION 2: HORIZONTAL BOX LAYOUT =====
        mainContainer->AddChild(CreateLayoutSectionTitle(20, currentY, "2. Horizontal Box Layout (Toolbar Style)"));
        currentY += 30;

        auto hboxDesc = CreateLayoutDescription(20, currentY, 960,
                                                "Horizontal toolbar with left-aligned actions and right-aligned utilities using AddStretch.");
        mainContainer->AddChild(hboxDesc);
        currentY += 30;

        // Create demo container for horizontal layout
        auto hboxDemo = std::make_shared<UltraCanvasContainer>("HBoxDemo", 20, currentY, 960, 50);
        hboxDemo->SetBackgroundColor(Color(245, 245, 250, 255));
        hboxDemo->SetPadding(10);

        hboxDemo->layout.SetFlexRow().SetFlexGap(5);

        auto newBtn = std::make_shared<UltraCanvasButton>("NewBtn", 0, 0, 60, 30);
        newBtn->SetText("New");
        auto openBtn = std::make_shared<UltraCanvasButton>("OpenBtn", 0, 0, 65, 30);
        openBtn->SetText("Open");
        auto saveBtn = std::make_shared<UltraCanvasButton>("SaveBtn", 0, 0, 60, 30);
        saveBtn->SetText("Save");
        auto settingsBtn = std::make_shared<UltraCanvasButton>("SettingsBtn", 0, 0, 80, 30);
        settingsBtn->SetText("Settings");
        auto helpBtn = std::make_shared<UltraCanvasButton>("HelpBtn", 0, 0, 60, 30);
        helpBtn->SetText("Help");

        hboxDemo->AddChild(newBtn);
        newBtn->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
        hboxDemo->AddChild(openBtn);
        openBtn->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
        hboxDemo->AddChild(saveBtn);
        saveBtn->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
        hboxDemo->AddSpacer(15);          // Visual separator
        hboxDemo->AddStretchSpacer(1);    // Push remaining buttons right
        hboxDemo->AddChild(settingsBtn);
        settingsBtn->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
        hboxDemo->AddChild(helpBtn);
        helpBtn->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);

        mainContainer->AddChild(hboxDemo);

        currentY += 60;

        auto hboxCode = CreateLayoutDescription(20, currentY, 960,
                                                "Code: container->layout.SetFlexRow().SetFlexGap(5);\ncontainer->AddChild(leftBtn);\ncontainer->AddSpacer(15);\ncontainer->AddStretchSpacer(1);\ncontainer->AddChild(rightBtn);");
        mainContainer->AddChild(hboxCode);

        currentY += 130;

        // ===== SECTION 3: GRID LAYOUT (FORM) =====
        mainContainer->AddChild(CreateLayoutSectionTitle(20, currentY, "3. Grid Layout (Form Design)"));
        currentY += 30;

        auto gridDesc = CreateLayoutDescription(20, currentY, 960,
                                                "Grid-based form with auto-sized labels and star-sized inputs. Submit button spans both columns.");
        mainContainer->AddChild(gridDesc);
        currentY += 60;

        // Create demo container for grid layout
        auto gridDemo = std::make_shared<UltraCanvasContainer>("GridDemo", 20, currentY, 450, 200);
        gridDemo->SetBackgroundColor(Color(245, 245, 250, 255));
        gridDemo->SetPadding(10);

        gridDemo->layout.SetGrid();
        CSSLayout::GridTrackSize colAuto;  // default kind == Auto
        CSSLayout::GridTrackSize colStar;
        colStar.kind  = CSSLayout::GridTrackSizeKind::Fr;
        colStar.value = CSSLayout::Dimension::Fr(1);
        gridDemo->layout.SetGridColumns({colAuto, colStar});
        gridDemo->layout.SetGridRows(std::vector<CSSLayout::GridTrackSize>(4));  // 4 auto rows
        gridDemo->layout.SetGridGap(10);

        int row = 0;

        auto nameLabel = std::make_shared<UltraCanvasLabel>("NameLbl", 0, 0, 70, 25);
        nameLabel->SetText("Name:");
        nameLabel->SetTextColor(Colors::Black);
        auto nameInput = std::make_shared<UltraCanvasTextInput>("NameIn", 0, 0, 250, 25);
        nameInput->SetShowValidationState(false);
        gridDemo->AddChild(nameLabel);
        nameLabel->layoutItem.SetGridRowColSimplified(row, 0);
        gridDemo->AddChild(nameInput);
        nameInput->layoutItem.SetGridRowColSimplified(row++, 1);

        auto emailLabel = std::make_shared<UltraCanvasLabel>("EmailLbl", 0, 0, 70, 25);
        emailLabel->SetText("Email:");
        emailLabel->SetTextColor(Colors::Black);
        auto emailInput = std::make_shared<UltraCanvasTextInput>("EmailIn", 0, 0, 250, 25);
        gridDemo->AddChild(emailLabel);
        emailLabel->layoutItem.SetGridRowColSimplified(row, 0);
        gridDemo->AddChild(emailInput);
        emailInput->layoutItem.SetGridRowColSimplified(row++, 1);

        auto phoneLabel = std::make_shared<UltraCanvasLabel>("PhoneLbl", 0, 0, 70, 25);
        phoneLabel->SetText("Phone:");
        phoneLabel->SetTextColor(Colors::Black);
        auto phoneInput = std::make_shared<UltraCanvasTextInput>("PhoneIn", 0, 0, 250, 25);
        gridDemo->AddChild(phoneLabel);
        phoneLabel->layoutItem.SetGridRowColSimplified(row, 0);
        gridDemo->AddChild(phoneInput);
        phoneInput->layoutItem.SetGridRowColSimplified(row++, 1);

        auto submitBtn = std::make_shared<UltraCanvasButton>("SubmitBtn", 0, 0, 150, 30);
        submitBtn->SetText("Submit");
        gridDemo->AddChild(submitBtn);
        submitBtn->layoutItem.SetGridRowColSimplified(row, 0, 1, 2);  // Span 2 columns

        mainContainer->AddChild(gridDemo);

        auto gridCode = CreateLayoutDescription(490, currentY, 490,
                                                "Code: container->layout.SetGrid();\ncontainer->layout.SetGridColumns({autoTrack, frTrack});\ncontainer->layout.SetGridRows(std::vector<GridTrackSize>(rows));\ncontainer->AddChild(label); label->layoutItem.SetGridRowColSimplified(row, column);\ncontainer->AddChild(input); input->layoutItem.SetGridRowColSimplified(row, column);");
        mainContainer->AddChild(gridCode);

        currentY += 220;

        // ===== SECTION 4: FLEX LAYOUT (CARDS) =====
        mainContainer->AddChild(CreateLayoutSectionTitle(20, currentY, "4. Flex Layout (Responsive Cards)"));
        currentY += 30;

        auto flexDesc = CreateLayoutDescription(20, currentY, 960,
                                                "Flexible card layout with wrapping. Cards automatically adjust to available space.");
        mainContainer->AddChild(flexDesc);
        currentY += 60;

        // Create demo container for flex layout
        auto flexDemo = std::make_shared<UltraCanvasContainer>("FlexDemo", 20, currentY, 960, 260);
        flexDemo->SetBackgroundColor(Color(245, 245, 250, 255));
        flexDemo->SetPadding(15);

        flexDemo->layout.SetFlexDirection(CSSLayout::FlexDirection::Row)
                .SetFlexWrap(CSSLayout::FlexWrap::Wrap)
                .SetFlexJustifyContent(CSSLayout::JustifyContent::SpaceAround)
                .SetFlexAlignItems(CSSLayout::AlignItems::Start)
                .SetFlexGap(15, 15);

        const char* cardTitles[] = {"Card 1", "Card 2", "Card 3", "Card 4"};
        const char* cardTexts[] = {
                "Flexible layout",
                "Wraps automatically",
                "Responsive design",
                "Modern pattern"
        };

        for (int i = 0; i < 4; i++) {
            auto card = std::make_shared<UltraCanvasContainer>(
                    std::string("Card") + std::to_string(i),
                    0, 0, 220, 110
            );
            card->SetBackgroundColor(Color(255, 255, 255, 255));
            card->SetPadding(15);

            card->layout.SetFlexColumn().SetFlexGap(8);

            auto cardTitle = std::make_shared<UltraCanvasLabel>(
                    std::string("CardTitle") + std::to_string(i),
                    0, 0, 190, 20
            );
            cardTitle->SetText(cardTitles[i]);
            cardTitle->SetTextColor(Colors::Black);
            cardTitle->SetFontSize(14);
            cardTitle->SetFontWeight(FontWeight::Bold);

            auto cardText = std::make_shared<UltraCanvasLabel>(
                    std::string("CardText") + std::to_string(i),
                    0, 0, 190, 35
            );
            cardText->SetText(cardTexts[i]);
            cardText->SetTextColor(Color(80, 80, 80, 255));
            cardText->SetFontSize(11);

            auto cardBtn = std::make_shared<UltraCanvasButton>(
                    std::string("CardBtn") + std::to_string(i),
                    0, 0, 80, 25
            );
            cardBtn->SetText("Action");

            card->AddChild(cardTitle);
            card->AddChild(cardText);
            card->AddStretchSpacer(1);
            card->AddChild(cardBtn);

            flexDemo->AddChild(card);
            card->layoutItem.SetFlexGrow(0).SetFlexShrink(1)
                    .SetFlexBasis(CSSLayout::Dimension::Px(220));  // flexBasis = 220
        }

        mainContainer->AddChild(flexDemo);

        currentY += 270;

        auto flexCode = CreateLayoutDescription(20, currentY, 960,
                                                "Code: float flexGrow=0, flexShrink=1, flexBasis=220\ncontainer->layout.SetFlexDirection(FlexDirection::Row);\ncontainer->layout.SetFlexWrap(FlexWrap::Wrap).SetFlexGap(15, 15);\ncontainer->AddChild(card); card->layoutItem.SetFlexGrow(0).SetFlexShrink(1).SetFlexBasis(Dimension::Px(220));");
        mainContainer->AddChild(flexCode);

        currentY += 130;

        // ===== SUMMARY =====
        auto summaryTitle = CreateLayoutSectionTitle(20, currentY, "Summary");
        mainContainer->AddChild(summaryTitle);
        currentY += 30;

        auto summaryText = CreateLayoutDescription(20, currentY, 960,
                                                   "• VBox/HBox: Best for simple linear arrangements (buttons, toolbars, lists)\n• Grid: Perfect for forms, tables, and structured layouts\n• Flex: Ideal for responsive designs, card grids, and adaptive layouts\n• All layouts support: spacing, padding, margins, alignment, and size constraints");
        mainContainer->AddChild(summaryText);

        return mainContainer;
    }

} // namespace UltraCanvas