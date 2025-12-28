// Apps/DemoApp/UltraCanvasLayoutExamples.cpp
// Layout system demonstration examples for UltraCanvas Demo Application
// Version: 1.0.0
// Last Modified: 2025-11-03
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasBoxLayout.h"
#include "UltraCanvasGridLayout.h"
#include "UltraCanvasFlexLayout.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasContainer.h"
#include <sstream>

namespace UltraCanvas {

// Helper function to create section title
    std::shared_ptr<UltraCanvasLabel> CreateLayoutSectionTitle(long id, long x, long y, const std::string& text) {
        auto title = std::make_shared<UltraCanvasLabel>("LayoutSecTitle" + std::to_string(id), id, x, y, 600, 25);
        title->SetText(text);
        title->SetFontSize(14);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        return title;
    }

// Helper function to create description label
    std::shared_ptr<UltraCanvasLabel> CreateLayoutDescription(long id, long x, long y, long width, const std::string& text) {
        auto desc = std::make_shared<UltraCanvasLabel>("LayoutDesc" + std::to_string(id), id, x, y, width, 0);
        desc->SetText(text);
        desc->SetTextColor(Color(80, 80, 80, 255));
        desc->SetFontSize(12);
        desc->SetWordWrap(true);
        desc->SetAutoResize(true);
        return desc;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateLayoutExamples() {
        auto mainContainer = std::make_shared<UltraCanvasContainer>("LayoutExamples", 1000, 0, 0, 1020, 1670);
        mainContainer->SetBackgroundColor(Colors::White);
        mainContainer->SetPadding(0,0,10,0);

        long currentY = 10;

        // ===== MAIN TITLE =====
        auto mainTitle = std::make_shared<UltraCanvasLabel>("LayoutMainTitle", 1001, 20, currentY, 900, 30);
        mainTitle->SetText("UltraCanvas Layout System Examples");
        mainTitle->SetFontSize(18);
        mainTitle->SetFontWeight(FontWeight::Bold);
        mainContainer->AddChild(mainTitle);

        currentY += 40;

        // Description
        auto description = CreateLayoutDescription(1002, 20, currentY, 960,
                                                   "Comprehensive examples of Box, Grid, and Flex layouts with various configurations and use cases.");
        mainContainer->AddChild(description);

        currentY += 70;

        // ===== SECTION 1: VERTICAL BOX LAYOUT =====
        mainContainer->AddChild(CreateLayoutSectionTitle(1010, 20, currentY, "1. Vertical Box Layout"));
        currentY += 30;

        auto vboxDesc = CreateLayoutDescription(1011, 20, currentY, 960,
                                                "Vertical arrangement with spacing, padding, and stretch. Buttons respond to clicks.");
        mainContainer->AddChild(vboxDesc);
        currentY += 60;

        // Create demo container for vertical layout
        auto vboxDemo = std::make_shared<UltraCanvasContainer>("VBoxDemo", 1012, 20, currentY, 300, 200);
        vboxDemo->SetBackgroundColor(Color(245, 245, 250, 255));
        vboxDemo->SetPadding(15);

        auto vboxLayout = CreateVBoxLayout(vboxDemo.get());
        vboxLayout->SetSpacing(10);
        //vboxLayout->SetPadding(15);

        auto vboxBtn1 = std::make_shared<UltraCanvasButton>("VBtn1", 1013, 0, 0, 150, 35);
        vboxBtn1->SetText("Button 1");

        auto vboxBtn2 = std::make_shared<UltraCanvasButton>("VBtn2", 1014, 0, 0, 150, 35);
        vboxBtn2->SetText("Button 2");

        auto vboxBtn3 = std::make_shared<UltraCanvasButton>("VBtn3", 1015, 0, 0, 150, 35);
        vboxBtn3->SetText("Button 3");

        auto vboxStatus = std::make_shared<UltraCanvasLabel>("VStatus", 1016, 0, 0, 150, 25);
        vboxStatus->SetText("Click any button");
        vboxStatus->SetTextColor(Color(0, 100, 200, 255));
        vboxStatus->SetFontSize(11);

        vboxBtn1->SetOnClick([vboxStatus]() { vboxStatus->SetText("Button 1 clicked!"); });
        vboxBtn2->SetOnClick([vboxStatus]() { vboxStatus->SetText("Button 2 clicked!"); });
        vboxBtn3->SetOnClick([vboxStatus]() { vboxStatus->SetText("Button 3 clicked!"); });

        vboxLayout->AddUIElement(vboxBtn1, 0)->SetCrossAlignment(LayoutAlignment::Center);
        vboxLayout->AddUIElement(vboxBtn2, 0)->SetCrossAlignment(LayoutAlignment::Center);
        vboxLayout->AddUIElement(vboxBtn3, 0)->SetCrossAlignment(LayoutAlignment::Center);
        vboxLayout->AddStretch(1);
        vboxLayout->AddUIElement(vboxStatus, 0)->SetCrossAlignment(LayoutAlignment::Center);

//        vboxDemo->SetLayout(std::move(vboxLayout));
        mainContainer->AddChild(vboxDemo);

        // Code explanation
        auto vboxCode = CreateLayoutDescription(1017, 340, currentY, 640,
                                                "Code: auto layout = CreateVBoxLayout(container);\nlayout->AddUIElement(button1); layout->AddStretch(1);");
        mainContainer->AddChild(vboxCode);

        currentY += 250;

        // ===== SECTION 2: HORIZONTAL BOX LAYOUT =====
        mainContainer->AddChild(CreateLayoutSectionTitle(1020, 20, currentY, "2. Horizontal Box Layout (Toolbar Style)"));
        currentY += 30;

        auto hboxDesc = CreateLayoutDescription(1021, 20, currentY, 960,
                                                "Horizontal toolbar with left-aligned actions and right-aligned utilities using AddStretch.");
        mainContainer->AddChild(hboxDesc);
        currentY += 30;

        // Create demo container for horizontal layout
        auto hboxDemo = std::make_shared<UltraCanvasContainer>("HBoxDemo", 1022, 20, currentY, 960, 50);
        hboxDemo->SetBackgroundColor(Color(245, 245, 250, 255));
        hboxDemo->SetPadding(10);

        auto hboxLayout = CreateHBoxLayout(hboxDemo.get());
        hboxLayout->SetSpacing(5);
        //hboxLayout->SetPadding(10);

        auto newBtn = std::make_shared<UltraCanvasButton>("NewBtn", 1023, 0, 0, 60, 30);
        newBtn->SetText("New");
        auto openBtn = std::make_shared<UltraCanvasButton>("OpenBtn", 1024, 0, 0, 65, 30);
        openBtn->SetText("Open");
        auto saveBtn = std::make_shared<UltraCanvasButton>("SaveBtn", 1025, 0, 0, 60, 30);
        saveBtn->SetText("Save");
        auto settingsBtn = std::make_shared<UltraCanvasButton>("SettingsBtn", 1026, 0, 0, 80, 30);
        settingsBtn->SetText("Settings");
        auto helpBtn = std::make_shared<UltraCanvasButton>("HelpBtn", 1027, 0, 0, 60, 30);
        helpBtn->SetText("Help");

        hboxLayout->AddUIElement(newBtn, 0)->SetCrossAlignment(LayoutAlignment::Center);
        hboxLayout->AddUIElement(openBtn, 0)->SetCrossAlignment(LayoutAlignment::Center);
        hboxLayout->AddUIElement(saveBtn, 0)->SetCrossAlignment(LayoutAlignment::Center);
        hboxLayout->AddSpacing(15);      // Visual separator
        hboxLayout->AddStretch(1);       // Push remaining buttons right
        hboxLayout->AddUIElement(settingsBtn, 0)->SetCrossAlignment(LayoutAlignment::Center);
        hboxLayout->AddUIElement(helpBtn, 0)->SetCrossAlignment(LayoutAlignment::Center);

//        hboxDemo->SetLayout(std::move(hboxLayout));
        mainContainer->AddChild(hboxDemo);

        currentY += 60;

        auto hboxCode = CreateLayoutDescription(1028, 20, currentY, 960,
                                                "Code: auto layout = CreateHBoxLayout(container);\nlayout->AddUIElement(leftBtn);\nlayout->AddSpacing(15);\nlayout->AddStretch(1);\nlayout->AddUIElement(rightBtn);");
        mainContainer->AddChild(hboxCode);

        currentY += 130;

        // ===== SECTION 3: GRID LAYOUT (FORM) =====
        mainContainer->AddChild(CreateLayoutSectionTitle(1030, 20, currentY, "3. Grid Layout (Form Design)"));
        currentY += 30;

        auto gridDesc = CreateLayoutDescription(1031, 20, currentY, 960,
                                                "Grid-based form with auto-sized labels and star-sized inputs. Submit button spans both columns.");
        mainContainer->AddChild(gridDesc);
        currentY += 60;

        // Create demo container for grid layout
        auto gridDemo = std::make_shared<UltraCanvasContainer>("GridDemo", 1032, 20, currentY, 450, 200);
        gridDemo->SetBackgroundColor(Color(245, 245, 250, 255));
        gridDemo->SetPadding(10);

        auto gridLayout = CreateGridLayout(gridDemo.get(), 4, 2);
        gridLayout->SetSpacing(10);
        //gridLayout->SetPadding(15);
        gridLayout->SetColumnDefinition(0, GridRowColumnDefinition::Auto());
        gridLayout->SetColumnDefinition(1, GridRowColumnDefinition::Star(1));

        int row = 0;

        auto nameLabel = std::make_shared<UltraCanvasLabel>("NameLbl", 1033, 0, 0, 70, 25);
        nameLabel->SetText("Name:");
        nameLabel->SetTextColor(Colors::Black);
        auto nameInput = std::make_shared<UltraCanvasTextInput>("NameIn", 1034, 0, 0, 250, 25);
        gridLayout->AddUIElement(nameLabel, row, 0);
        gridLayout->AddUIElement(nameInput, row++, 1);

        auto emailLabel = std::make_shared<UltraCanvasLabel>("EmailLbl", 1035, 0, 0, 70, 25);
        emailLabel->SetText("Email:");
        emailLabel->SetTextColor(Colors::Black);
        auto emailInput = std::make_shared<UltraCanvasTextInput>("EmailIn", 1036, 0, 0, 250, 25);
        gridLayout->AddUIElement(emailLabel, row, 0);
        gridLayout->AddUIElement(emailInput, row++, 1);

        auto phoneLabel = std::make_shared<UltraCanvasLabel>("PhoneLbl", 1037, 0, 0, 70, 25);
        phoneLabel->SetText("Phone:");
        phoneLabel->SetTextColor(Colors::Black);
        auto phoneInput = std::make_shared<UltraCanvasTextInput>("PhoneIn", 1038, 0, 0, 250, 25);
        gridLayout->AddUIElement(phoneLabel, row, 0);
        gridLayout->AddUIElement(phoneInput, row++, 1);

        auto submitBtn = std::make_shared<UltraCanvasButton>("SubmitBtn", 1039, 0, 0, 150, 30);
        submitBtn->SetText("Submit");
        gridLayout->AddUIElement(submitBtn, row, 0, 1, 2);  // Span 2 columns

//        gridDemo->SetLayout(std::move(gridLayout));
        mainContainer->AddChild(gridDemo);

        auto gridCode = CreateLayoutDescription(1040, 490, currentY, 490,
                                                "Code: auto layout = CreateGridLayout(container, rows, cols);\nlayout->SetColumnDefinition(0, GridRowColumnDefinition::Auto());\nlayout->SetColumnDefinition(1, GridRowColumnDefinition::Star(1));\nlayout->AddUIElement(label, row, column);\nlayout->AddUIElement(input, row, column);");
        mainContainer->AddChild(gridCode);

        currentY += 220;

        // ===== SECTION 4: FLEX LAYOUT (CARDS) =====
        mainContainer->AddChild(CreateLayoutSectionTitle(1050, 20, currentY, "4. Flex Layout (Responsive Cards)"));
        currentY += 30;

        auto flexDesc = CreateLayoutDescription(1051, 20, currentY, 960,
                                                "Flexible card layout with wrapping. Cards automatically adjust to available space.");
        mainContainer->AddChild(flexDesc);
        currentY += 60;

        // Create demo container for flex layout
        auto flexDemo = std::make_shared<UltraCanvasContainer>("FlexDemo", 1052, 20, currentY, 960, 260);
        flexDemo->SetBackgroundColor(Color(245, 245, 250, 255));
        flexDemo->SetPadding(15);

        auto flexLayout = CreateFlexLayout(flexDemo.get(), FlexDirection::Row);
        flexLayout->SetFlexWrap(FlexWrap::Wrap);
        flexLayout->SetJustifyContent(FlexJustifyContent::SpaceAround);
        flexLayout->SetAlignItems(FlexAlignItems::Start);
        flexLayout->SetGap(15, 15);
        //flexLayout->SetPadding(15);

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
                    1053 + i * 10, 0, 0, 220, 110
            );
            card->SetBackgroundColor(Color(255, 255, 255, 255));
            card->SetPadding(15);

            auto cardLayout = CreateVBoxLayout(card.get());
            cardLayout->SetSpacing(8);
            //cardLayout->SetPadding(12);

            auto cardTitle = std::make_shared<UltraCanvasLabel>(
                    std::string("CardTitle") + std::to_string(i),
                    1053 + i * 10 + 1, 0, 0, 190, 20
            );
            cardTitle->SetText(cardTitles[i]);
            cardTitle->SetTextColor(Colors::Black);
            cardTitle->SetFontSize(14);
            cardTitle->SetFontWeight(FontWeight::Bold);

            auto cardText = std::make_shared<UltraCanvasLabel>(
                    std::string("CardText") + std::to_string(i),
                    1053 + i * 10 + 2, 0, 0, 190, 35
            );
            cardText->SetText(cardTexts[i]);
            cardText->SetTextColor(Color(80, 80, 80, 255));
            cardText->SetFontSize(11);

            auto cardBtn = std::make_shared<UltraCanvasButton>(
                    std::string("CardBtn") + std::to_string(i),
                    1053 + i * 10 + 3, 0, 0, 80, 25
            );
            cardBtn->SetText("Action");

            cardLayout->AddUIElement(cardTitle, 0);
            cardLayout->AddUIElement(cardText, 0);
            cardLayout->AddStretch(1);
            cardLayout->AddUIElement(cardBtn, 0);

            //card->SetLayout(std::move(cardLayout));
            flexLayout->AddUIElement(card, 0, 1, 220);  // flexBasis = 220
        }

        //flexDemo->SetLayout(std::move(flexLayout));
        mainContainer->AddChild(flexDemo);

        currentY += 270;

        auto flexCode = CreateLayoutDescription(1100, 20, currentY, 960,
                                                "Code: float flexGrow=0, flexShrink=1, flexBasis=220\nauto layout = CreateFlexLayout(container, FlexDirection::Row);\nlayout->SetFlexWrap(FlexWrap::Wrap); layout->SetGap(15, 15);\nlayout->AddUIElement(card, flexGrow, flexShrink, flexBasis);");
        mainContainer->AddChild(flexCode);

        currentY += 130;

        // ===== SUMMARY =====
        auto summaryTitle = CreateLayoutSectionTitle(1110, 20, currentY, "Summary");
        mainContainer->AddChild(summaryTitle);
        currentY += 30;

        auto summaryText = CreateLayoutDescription(1111, 20, currentY, 960,
                                                   "• VBox/HBox: Best for simple linear arrangements (buttons, toolbars, lists)\n• Grid: Perfect for forms, tables, and structured layouts\n• Flex: Ideal for responsive designs, card grids, and adaptive layouts\n• All layouts support: spacing, padding, margins, alignment, and size constraints");
        mainContainer->AddChild(summaryText);

        return mainContainer;
    }

} // namespace UltraCanvas