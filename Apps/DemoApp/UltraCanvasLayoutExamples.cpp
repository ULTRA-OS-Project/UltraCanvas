// Apps/DemoApp/UltraCanvasLayoutExamples.cpp
// Layout system demonstration examples for UltraCanvas Demo Application
// Version: 2.1.0 - the shared page look (UltraCanvasDemoPageKit): one card per
//                  layout, every text and button sized to fit, the demo
//                  containers keep their own layouts
// Version: 2.0.1 - layouts ask for their stretch; the grid form's inputs have no width of their own
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasSpacer.h"
#include "UltraCanvasTabbedContainer.h"
#include "CSSLayout/CSSLayout.h"
#include <sstream>

namespace UltraCanvas {

namespace {
        // A button as wide as its text, at least the width it was drawn at
        // before, so a longer caption widens it instead of being cut.
        std::shared_ptr<UltraCanvasButton> MakeLayoutButton(const std::string& id, const std::string& text,
                                                            float minWidth, float height) {
            auto button = std::make_shared<UltraCanvasButton>(id, 0, 0, 0, height, text);
            CSSLayout::BoxConstraints limits;
            limits.minWidth = CSSLayout::Dimension::Px(minWidth);
            button->boxConstraints = limits;
            return button;
        }

        // A container that demonstrates a layout keeps its height as a floor,
        // so its content can grow taller with its text.
        void SetMinimumHeight(const std::shared_ptr<UltraCanvasUIElement>& element, float h) {
            CSSLayout::BoxConstraints limits = element->boxConstraints.value_or(CSSLayout::BoxConstraints{});
            limits.minHeight = CSSLayout::Dimension::Px(h);
            element->boxConstraints = limits;
        }

        // The code sample beside a demo: shares the row's spare width and
        // moves under the demo when the row is narrower than both.
        void AddCodeBeside(const std::shared_ptr<UltraCanvasContainer>& row, const std::string& id,
                           const std::string& text) {
            auto code = DemoKit::CreateText(id, text);
            DemoKit::Add(row, code, 1);
            code->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(DemoKit::kCardMinWidth));
        }
    } // namespace

    // Provided by UltraCanvasLabelPlacementExamples.cpp
    std::shared_ptr<UltraCanvasUIElement> CreateLabelPlacementLayoutTab();

    static std::shared_ptr<UltraCanvasUIElement> CreateBoxGridFlexLayoutTab() {
        // The tab's content: the page look, scrolled inside the tab (the
        // tabbed container sizes it to its content area).
        auto mainContainer = std::make_shared<UltraCanvasContainer>("LayoutExamples");
        mainContainer->SetBackgroundColor(DemoKit::PageBackground());
        mainContainer->SetPadding(DemoKit::kPagePadding);
        mainContainer->layout.SetFlexColumn().SetFlexGap(DemoKit::kPageGap)
                             .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

        // ===== SECTION 1: VERTICAL BOX LAYOUT =====
        auto vboxCard = DemoKit::AddCard(mainContainer, "LayoutVBoxCard", "1. Vertical Box Layout",
                "Vertical arrangement with spacing, padding, and stretch. Buttons respond to clicks.");
        auto vboxRow = DemoKit::AddRow(vboxCard, "LayoutVBoxRow", 20);

        // Create demo container for vertical layout
        auto vboxDemo = std::make_shared<UltraCanvasContainer>("VBoxDemo", 0, 0, 300, 0);
        SetMinimumHeight(vboxDemo, 200);
        vboxDemo->SetBackgroundColor(Color(245, 245, 250, 255));
        vboxDemo->SetPadding(15);

        vboxDemo->layout.SetFlexColumn().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

        auto vboxBtn1 = MakeLayoutButton("VBtn1", "Button 1", 150, 35);
        auto vboxBtn2 = MakeLayoutButton("VBtn2", "Button 2", 150, 35);
        auto vboxBtn3 = MakeLayoutButton("VBtn3", "Button 3", 150, 35);

        auto vboxStatus = std::make_shared<UltraCanvasLabel>("VStatus", "Click any button");
        vboxStatus->SetTextColor(Color(0, 100, 200, 255));
        vboxStatus->SetFontSize(11);
        vboxStatus->SetWrap(TextWrap::WrapWord);

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

        DemoKit::AddFixed(vboxRow, vboxDemo);

        // Code explanation
        AddCodeBeside(vboxRow, "LayoutVBoxCode",
                "Code: container->layout.SetFlexColumn().SetFlexGap(10);\ncontainer->AddChild(button1); container->AddStretchSpacer(1);");

        // ===== SECTION 2: HORIZONTAL BOX LAYOUT =====
        auto hboxCard = DemoKit::AddCard(mainContainer, "LayoutHBoxCard", "2. Horizontal Box Layout (Toolbar Style)",
                "Horizontal toolbar with left-aligned actions and right-aligned utilities using AddStretch.");

        // Create demo container for horizontal layout
        auto hboxDemo = std::make_shared<UltraCanvasContainer>("HBoxDemo", 0, 0, 960, 50);
        hboxDemo->SetBackgroundColor(Color(245, 245, 250, 255));
        hboxDemo->SetPadding(10);

        hboxDemo->layout.SetFlexRow().SetFlexGap(5).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

        auto newBtn = MakeLayoutButton("NewBtn", "New", 60, 30);
        auto openBtn = MakeLayoutButton("OpenBtn", "Open", 65, 30);
        auto saveBtn = MakeLayoutButton("SaveBtn", "Save", 60, 30);
        auto settingsBtn = MakeLayoutButton("SettingsBtn", "Settings", 80, 30);
        auto helpBtn = MakeLayoutButton("HelpBtn", "Help", 60, 30);

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

        // As wide as the card, so the stretch spacer has room to show.
        DemoKit::AddStretched(hboxCard, hboxDemo);

        DemoKit::AddText(hboxCard, "LayoutHBoxCode",
                "Code: container->layout.SetFlexRow().SetFlexGap(5);\ncontainer->AddChild(leftBtn);\ncontainer->AddSpacer(15);\ncontainer->AddStretchSpacer(1);\ncontainer->AddChild(rightBtn);");

        // ===== SECTION 3: GRID LAYOUT (FORM) =====
        auto gridCard = DemoKit::AddCard(mainContainer, "LayoutGridCard", "3. Grid Layout (Form Design)",
                "Grid-based form with auto-sized labels and star-sized inputs. Submit button spans both columns.");
        auto gridRow = DemoKit::AddRow(gridCard, "LayoutGridRow", 20);

        // Create demo container for grid layout
        auto gridDemo = std::make_shared<UltraCanvasContainer>("GridDemo", 0, 0, 450, 0);
        SetMinimumHeight(gridDemo, 200);
        gridDemo->SetBackgroundColor(Color(245, 245, 250, 255));
        gridDemo->SetPadding(10);

        gridDemo->layout.SetGrid()
                .SetGridJustifyItems(CSSLayout::JustifyItems::Stretch)
                .SetGridAlignItems(CSSLayout::AlignItems::Stretch);
        CSSLayout::GridTrackSize colAuto;  // default kind == Auto
        CSSLayout::GridTrackSize colStar;
        colStar.kind  = CSSLayout::GridTrackSizeKind::Fr;
        colStar.value = CSSLayout::Dimension::Fr(1);
        gridDemo->layout.SetGridColumns({colAuto, colStar});
        gridDemo->layout.SetGridRows(std::vector<CSSLayout::GridTrackSize>(4));  // 4 auto rows
        gridDemo->layout.SetGridGap(10);

        int row = 0;

        // The labels, the inputs and the button have no width of their own:
        // the auto column is as wide as the widest label, and the grid
        // stretches the inputs across the fr column (and the button across both).
        auto makeFormLabel = [](const std::string& id, const std::string& text) {
            auto label = std::make_shared<UltraCanvasLabel>(id, text);
            label->SetTextColor(Colors::Black);
            label->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
            return label;
        };

        auto nameLabel = makeFormLabel("NameLbl", "Name:");
        auto nameInput = std::make_shared<UltraCanvasTextInput>("NameIn", 0, 0, 0, 25);
        nameInput->SetShowValidationState(false);
        gridDemo->AddChild(nameLabel);
        nameLabel->layoutItem.SetGridRowColSimplified(row, 0);
        gridDemo->AddChild(nameInput);
        nameInput->layoutItem.SetGridRowColSimplified(row++, 1);

        auto emailLabel = makeFormLabel("EmailLbl", "Email:");
        auto emailInput = std::make_shared<UltraCanvasTextInput>("EmailIn", 0, 0, 0, 25);
        gridDemo->AddChild(emailLabel);
        emailLabel->layoutItem.SetGridRowColSimplified(row, 0);
        gridDemo->AddChild(emailInput);
        emailInput->layoutItem.SetGridRowColSimplified(row++, 1);

        auto phoneLabel = makeFormLabel("PhoneLbl", "Phone:");
        auto phoneInput = std::make_shared<UltraCanvasTextInput>("PhoneIn", 0, 0, 0, 25);
        gridDemo->AddChild(phoneLabel);
        phoneLabel->layoutItem.SetGridRowColSimplified(row, 0);
        gridDemo->AddChild(phoneInput);
        phoneInput->layoutItem.SetGridRowColSimplified(row++, 1);

        auto submitBtn = std::make_shared<UltraCanvasButton>("SubmitBtn", 0, 0, 0, 30);
        submitBtn->SetText("Submit");
        gridDemo->AddChild(submitBtn);
        submitBtn->layoutItem.SetGridRowColSimplified(row, 0, 1, 2);  // Span 2 columns

        DemoKit::AddFixed(gridRow, gridDemo);

        AddCodeBeside(gridRow, "LayoutGridCode",
                "Code: container->layout.SetGrid();\ncontainer->layout.SetGridColumns({autoTrack, frTrack});\ncontainer->layout.SetGridRows(std::vector<GridTrackSize>(rows));\ncontainer->AddChild(label); label->layoutItem.SetGridRowColSimplified(row, column);\ncontainer->AddChild(input); input->layoutItem.SetGridRowColSimplified(row, column);");

        // ===== SECTION 4: FLEX LAYOUT (CARDS) =====
        auto flexCard = DemoKit::AddCard(mainContainer, "LayoutFlexCard", "4. Flex Layout (Responsive Cards)",
                "Flexible card layout with wrapping. Cards automatically adjust to available space.");

        // Create demo container for flex layout. As wide as the card and as
        // tall as its wrapped lines of cards.
        auto flexDemo = std::make_shared<UltraCanvasContainer>("FlexDemo", 0, 0, 0, 0);
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
            // 220 wide (the flex basis); at least 110 tall, taller when the
            // text wraps.
            auto card = std::make_shared<UltraCanvasContainer>(
                    std::string("Card") + std::to_string(i),
                    0, 0, 220, 0
            );
            SetMinimumHeight(card, 110);
            card->SetBackgroundColor(Color(255, 255, 255, 255));
            card->SetPadding(15);

            card->layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

            auto cardTitle = std::make_shared<UltraCanvasLabel>(
                    std::string("CardTitle") + std::to_string(i), cardTitles[i]);
            cardTitle->SetTextColor(Colors::Black);
            cardTitle->SetFontSize(14);
            cardTitle->SetFontWeight(FontWeight::Bold);
            cardTitle->SetWrap(TextWrap::WrapWord);

            auto cardText = std::make_shared<UltraCanvasLabel>(
                    std::string("CardText") + std::to_string(i), cardTexts[i]);
            cardText->SetTextColor(Color(80, 80, 80, 255));
            cardText->SetFontSize(11);
            cardText->SetWrap(TextWrap::WrapWord);

            auto cardBtn = MakeLayoutButton(std::string("CardBtn") + std::to_string(i), "Action", 80, 25);

            card->AddChild(cardTitle);
            card->AddChild(cardText);
            card->AddStretchSpacer(1);
            card->AddChild(cardBtn);
            // At the start, as wide as its text - not stretched across the card.
            cardBtn->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Start);

            flexDemo->AddChild(card);
            card->layoutItem.SetFlexGrow(0).SetFlexShrink(1)
                    .SetFlexBasis(CSSLayout::Dimension::Px(220));  // flexBasis = 220
        }

        DemoKit::Add(flexCard, flexDemo);

        DemoKit::AddText(flexCard, "LayoutFlexCode",
                "Code: float flexGrow=0, flexShrink=1, flexBasis=220\ncontainer->layout.SetFlexDirection(FlexDirection::Row);\ncontainer->layout.SetFlexWrap(FlexWrap::Wrap).SetFlexGap(15, 15);\ncontainer->AddChild(card); card->layoutItem.SetFlexGrow(0).SetFlexShrink(1).SetFlexBasis(Dimension::Px(220));");

        // ===== SUMMARY =====
        DemoKit::AddNotes(mainContainer, "LayoutSummary", "Summary", {
                "VBox/HBox: Best for simple linear arrangements (buttons, toolbars, lists)",
                "Grid: Perfect for forms, tables, and structured layouts",
                "Flex: Ideal for responsive designs, card grids, and adaptive layouts",
                "All layouts support: spacing, padding, margins, alignment, and size constraints"});

        return mainContainer;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateLayoutExamples() {
        // The page fills the area; each tab scrolls its own content.
        auto page = DemoKit::CreatePage("LayoutExamplesPage", "UltraCanvas Layout System Examples",
                "Comprehensive examples of Box, Grid, and Flex layouts with various configurations and use cases.");
        page->SetFillsArea(true);

        auto card = DemoKit::CreateCard("LayoutTabsCard");
        card->SetPadding(4);
        DemoKit::Add(page, card, 1);

        auto tabs = std::make_shared<UltraCanvasTabbedContainer>("LayoutExamplesTabs");
        tabs->SetTabPosition(TabPosition::Top);
        tabs->SetTabStyle(TabStyle::Modern);
        tabs->AddTab("Box / Grid / Flex", CreateBoxGridFlexLayoutTab());
        tabs->AddTab("Label placement", CreateLabelPlacementLayoutTab());
        DemoKit::Add(card, tabs, 1);
        tabs->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
        return page;
    }

} // namespace UltraCanvas