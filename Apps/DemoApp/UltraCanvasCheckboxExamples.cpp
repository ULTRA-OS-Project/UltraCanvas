// UltraCanvasCheckboxExamples.cpp
// Interactive checkbox component demonstration
// Version: 1.3.0 - the shared page look (UltraCanvasDemoPageKit): one card per
//                 section, every toggle sized to its label, hints beside the
//                 switches wrap; the row of radio groups owns the groups
// Version: 1.2.1 - the tri-state section's callbacks name the boxes raw (they held
//                 shared_ptrs to the boxes they were stored on, so the boxes
//                 were never freed); the parent's handler is set once
// Version: 1.2.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasRadio.h"
#include "UltraCanvasSwitch.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasConfig.h"
#include <iostream>
#include <vector>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

// The row holding the radio groups owns them so they outlive CreateCheckboxExamples().
// UltraCanvasRadioGroup is not a UI element and is not stored by the radio buttons,
// so without an owning instance the groups would be destroyed on function return
// and exclusive selection / onSelectionChanged callbacks would stop working.
// It lays out as DemoKit::CreateRow does.
    class CheckboxExamplesContainer : public UltraCanvasContainer {
    public:
        std::shared_ptr<UltraCanvasRadioGroup> themeRadioGroup;
        std::shared_ptr<UltraCanvasRadioGroup> qualityRadioGroup;

        explicit CheckboxExamplesContainer(const std::string& id)
                : UltraCanvasContainer(id) {
            ContainerStyle style;
            style.autoShowScrollbars = false;
            SetContainerStyle(style);
            layout.SetFlexRow()
                  .SetFlexWrap(CSSLayout::FlexWrap::Wrap)
                  .SetFlexGap(40)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Start);
        }
    };

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateCheckboxExamples() {
        auto page = DemoKit::CreatePage("CheckboxMainContainer", "UltraCanvas Checkbox Component Examples",
                "Demonstrates various checkbox styles, states, and configurations");

        // The toggles size themselves to their box and label, so a longer
        // label makes them wider instead of cutting it.

        // ===== SECTION 1: Basic Checkboxes =====
        auto basicCard = DemoKit::AddCard(page, "BasicCheckboxesCard", "Basic Checkboxes");
        auto basicRow = DemoKit::AddRow(basicCard, "BasicCheckboxesRow", 20);

        // Standard checkbox - unchecked
        auto basicCheckbox1 = std::make_shared<UltraCanvasCheckbox>("BasicCheckbox1", "Standard Checkbox");
        basicCheckbox1->SetChecked(false);
        basicCheckbox1->onChecked = []() {
            debugOutput << "Basic checkbox checked!" << std::endl;
        };
        basicCheckbox1->onUnchecked = []() {
            debugOutput << "Basic checkbox unchecked!" << std::endl;
        };
        DemoKit::Add(basicRow, basicCheckbox1);

        // Standard checkbox - checked
        auto basicCheckbox2 = std::make_shared<UltraCanvasCheckbox>("BasicCheckbox2", "Pre-checked Box");
        basicCheckbox2->SetChecked(true);
        DemoKit::Add(basicRow, basicCheckbox2);

        // Disabled checkbox - unchecked
        auto disabledCheckbox1 = std::make_shared<UltraCanvasCheckbox>("DisabledCheckbox1", "Disabled Unchecked");
        disabledCheckbox1->SetChecked(false);
        disabledCheckbox1->SetDisabled(true);
        DemoKit::Add(basicRow, disabledCheckbox1);

        // Disabled checkbox - checked
        auto disabledCheckbox2 = std::make_shared<UltraCanvasCheckbox>("DisabledCheckbox2", "Disabled Checked");
        disabledCheckbox2->SetChecked(true);
        disabledCheckbox2->SetDisabled(true);
        DemoKit::Add(basicRow, disabledCheckbox2);

        // ===== SECTION 2: Tri-State Checkboxes =====
        auto triCard = DemoKit::AddCard(page, "TriStateCard", "Tri-State Checkboxes (Indeterminate)");
        auto triRow = DemoKit::AddRow(triCard, "TriStateRow", 40);
        auto triColumn = DemoKit::CreateColumn("TriStateColumn", 4);
        DemoKit::Add(triRow, triColumn);

        // Tri-state checkbox
        auto triStateCheckbox = std::make_shared<UltraCanvasCheckbox>("TriStateCheckbox", "Select All Items");
        triStateCheckbox->SetAllowIndeterminate(true);
        triStateCheckbox->SetCheckState(CheckedState::Indeterminate);

        // Sub-items for tri-state demonstration
        auto subItem1 = std::make_shared<UltraCanvasCheckbox>("SubItem1", "Item 1");
        auto subItem2 = std::make_shared<UltraCanvasCheckbox>("SubItem2", "Item 2");
        auto subItem3 = std::make_shared<UltraCanvasCheckbox>("SubItem3", "Item 3");

        subItem1->SetChecked(true);
        subItem2->SetChecked(false);
        subItem3->SetChecked(true);

        // The callbacks below are stored on these boxes, and every box lives in
        // the page, so they name the boxes raw: a shared_ptr to a box in
        // its own callback - directly, or through updateParentState - would
        // keep the boxes alive for ever.
        UltraCanvasCheckbox* parentBox = triStateCheckbox.get();
        UltraCanvasCheckbox* item1 = subItem1.get();
        UltraCanvasCheckbox* item2 = subItem2.get();
        UltraCanvasCheckbox* item3 = subItem3.get();

        // Update parent state based on children
        auto updateParentState = [parentBox, item1, item2, item3]() {
            int checkedCount = 0;
            if (item1->IsChecked()) checkedCount++;
            if (item2->IsChecked()) checkedCount++;
            if (item3->IsChecked()) checkedCount++;

            if (checkedCount == 0) {
                parentBox->SetCheckState(CheckedState::Unchecked);
            } else if (checkedCount == 3) {
                parentBox->SetCheckState(CheckedState::Checked);
            } else {
                parentBox->SetCheckState(CheckedState::Indeterminate);
            }
        };

        // Set callbacks for sub-items
        subItem1->onStateChanged = [updateParentState](CheckedState, CheckedState) { updateParentState(); };
        subItem2->onStateChanged = [updateParentState](CheckedState, CheckedState) { updateParentState(); };
        subItem3->onStateChanged = [updateParentState](CheckedState, CheckedState) { updateParentState(); };

        // The items indented under the box that sets them all
        DemoKit::AddFixed(triColumn, triStateCheckbox);
        auto subItems = DemoKit::CreateColumn("TriStateSubItems", 4);
        subItems->SetPadding(0, 0, 0, 30);
        DemoKit::Add(triColumn, subItems);
        DemoKit::AddFixed(subItems, subItem1);
        DemoKit::AddFixed(subItems, subItem2);
        DemoKit::AddFixed(subItems, subItem3);

        // Status display for tri-state
        auto triStateStatus = DemoKit::CreateText("TriStateStatus", "State: Indeterminate (2 of 3 selected)");
        triStateStatus->SetTextColor(Color(100, 100, 100, 255));
        DemoKit::Add(triRow, triStateStatus, 1);

        // Update status label
        auto updateStatusLabel = [status = triStateStatus.get(), parentBox]() {
            switch(parentBox->GetCheckState()) {
                case CheckedState::Unchecked:
                    status->SetText("State: Unchecked (0 selected)");
                    break;
                case CheckedState::Checked:
                    status->SetText("State: Checked (all selected)");
                    break;
                case CheckedState::Indeterminate:
                    status->SetText("State: Indeterminate (partially selected)");
                    break;
            }
        };

        // Parent checkbox callback: checks or clears every item, and reports
        triStateCheckbox->onStateChanged = [item1, item2, item3, updateStatusLabel](CheckedState oldState, CheckedState newState) {
            if (newState == CheckedState::Checked) {
                item1->SetChecked(true);
                item2->SetChecked(true);
                item3->SetChecked(true);
            } else if (newState == CheckedState::Unchecked) {
                item1->SetChecked(false);
                item2->SetChecked(false);
                item3->SetChecked(false);
            }
            updateStatusLabel();
        };

        // ===== SECTION 3: Switch Style Checkboxes =====
        auto switchCard = DemoKit::AddCard(page, "SwitchCard", "Switch Style Toggles");

        // One line per switch: the switch, then a text beside it that wraps
        // under it when the line is too narrow.
        auto addSwitchLine = [&switchCard](const std::string& rowId,
                                           const std::vector<std::shared_ptr<UltraCanvasUIElement>>& switches,
                                           const std::shared_ptr<UltraCanvasLabel>& text) {
            auto row = DemoKit::AddRow(switchCard, rowId, 16);
            row->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
            for (const auto& sw : switches) DemoKit::Add(row, sw);
            DemoKit::Add(row, text, 1);
        };

        // Create switches
        auto switch1 = UltraCanvasSwitch::Create("Switch1", 0, 0, "Enable Notifications", true);
        auto switch2 = UltraCanvasSwitch::Create("Switch2", 0, 0, "Dark Mode", false);
        auto switch3 = UltraCanvasSwitch::Create("Switch3", 0, 0, "Auto-Save", true);

        // Switch status labels
        auto switchStatus1 = DemoKit::CreateText("SwitchStatus1", "ON");
        switchStatus1->SetTextColor(Color(0, 150, 0, 255));

        auto switchStatus2 = DemoKit::CreateText("SwitchStatus2", "OFF");
        switchStatus2->SetTextColor(Color(150, 0, 0, 255));

        auto switchStatus3 = DemoKit::CreateText("SwitchStatus3", "ON");
        switchStatus3->SetTextColor(Color(0, 150, 0, 255));

        // Switch callbacks
        switch1->onStateChanged = [switchStatus1](CheckedState oldState, CheckedState newState) {
            if (newState == CheckedState::Checked) {
                switchStatus1->SetText("ON");
                switchStatus1->SetTextColor(Color(0, 150, 0, 255));
                debugOutput << "Notifications enabled" << std::endl;
            } else {
                switchStatus1->SetText("OFF");
                switchStatus1->SetTextColor(Color(150, 0, 0, 255));
                debugOutput << "Notifications disabled" << std::endl;
            }
        };

        switch2->onStateChanged = [switchStatus2](CheckedState oldState, CheckedState newState) {
            if (newState == CheckedState::Checked) {
                switchStatus2->SetText("ON");
                switchStatus2->SetTextColor(Color(0, 150, 0, 255));
                debugOutput << "Dark mode enabled" << std::endl;
            } else {
                switchStatus2->SetText("OFF");
                switchStatus2->SetTextColor(Color(150, 0, 0, 255));
                debugOutput << "Dark mode disabled" << std::endl;
            }
        };

        switch3->onStateChanged = [switchStatus3](CheckedState oldState, CheckedState newState) {
            if (newState == CheckedState::Checked) {
                switchStatus3->SetText("ON");
                switchStatus3->SetTextColor(Color(0, 150, 0, 255));
                debugOutput << "Auto-save enabled" << std::endl;
            } else {
                switchStatus3->SetText("OFF");
                switchStatus3->SetTextColor(Color(150, 0, 0, 255));
                debugOutput << "Auto-save disabled" << std::endl;
            }
        };

        addSwitchLine("Switch1Row", {switch1}, switchStatus1);
        addSwitchLine("Switch2Row", {switch2}, switchStatus2);
        addSwitchLine("Switch3Row", {switch3}, switchStatus3);

        // The hints under the switches below
        auto makeHint = [](const std::string& id, const std::string& text) {
            auto hint = DemoKit::CreateText(id, text);
            hint->SetTextColor(Color(100, 100, 100, 255));
            return hint;
        };

        // ----- Switch with built-in check icon (Check style) -----
        auto switchCheck = UltraCanvasSwitch::Create("SwitchCheck", 0, 0, "Wi-Fi", true);
        switchCheck->GetVisualStyle().thumbIconStyle = SwitchThumbIconStyle::Check;
        switchCheck->GetVisualStyle().thumbIconOnColor = Color(76, 175, 80, 255);
        switchCheck->onStateChanged = [](CheckedState, CheckedState newState) {
            debugOutput << "Wi-Fi " << (newState == CheckedState::Checked ? "on" : "off") << std::endl;
        };
        addSwitchLine("SwitchCheckRow", {switchCheck},
                      makeHint("SwitchCheckHint", "Built-in checkmark icon (drawn via path API)"));

        // ----- Switch with custom image icons (check.png + x.png) -----
        UCImagePtr iconCheckOn = UCImage::Load(NormalizePath(GetResourcesDir() + "media/icons/check.png"), false);
        UCImagePtr iconCheckOff = UCImage::Load(NormalizePath(GetResourcesDir() + "media/icons/x.png"), false);

        auto switchImg = UltraCanvasSwitch::Create("SwitchImg", 0, 0, "Bluetooth", true);
        switchImg->SetTrackSize(48.0f, 26.0f);
        switchImg->SetThumbIcons(iconCheckOn, iconCheckOff);
        switchImg->onStateChanged = [](CheckedState, CheckedState newState) {
            debugOutput << "Bluetooth " << (newState == CheckedState::Checked ? "on" : "off") << std::endl;
        };
        addSwitchLine("SwitchImgRow", {switchImg},
                      makeHint("SwitchImgHint", "Custom image icons (check.png / x.png) inside thumb"));

        // ----- Switch with custom ON icon only (demonstrates plain-circle fallback for OFF) -----
        auto switchImgFallback = UltraCanvasSwitch::Create("SwitchImgFallback", 0, 0, "Sync", false);
        switchImgFallback->SetTrackSize(48.0f, 26.0f);
        switchImgFallback->SetThumbIcons(iconCheckOn, nullptr);  // OFF icon missing → plain circle
        switchImgFallback->onStateChanged = [](CheckedState, CheckedState newState) {
            debugOutput << "Sync " << (newState == CheckedState::Checked ? "on" : "off") << std::endl;
        };
        addSwitchLine("SwitchImgFallbackRow", {switchImgFallback},
                      makeHint("SwitchImgFallbackHint", "Only ON icon supplied — OFF state falls back to plain circle"));

        // ----- Switch with ON/OFF labels INSIDE the track -----
        auto switchInside = UltraCanvasSwitch::Create("SwitchInside", 0, 0, "Power", true);
        switchInside->SetTrackSize(60.0f, 24.0f);
        switchInside->SetStateLabels("ON", "OFF", SwitchStateLabelPosition::InsideTrack);
        switchInside->onStateChanged = [](CheckedState, CheckedState newState) {
            debugOutput << "Power " << (newState == CheckedState::Checked ? "on" : "off") << std::endl;
        };
        addSwitchLine("SwitchInsideRow", {switchInside},
                      makeHint("SwitchInsideHint", "ON/OFF text inside track, opposite the thumb"));

        // ----- Switch with ON/OFF labels OUTSIDE the track -----
        auto switchOutside = UltraCanvasSwitch::Create("SwitchOutside", 0, 0, "Sound", false);
        switchOutside->SetStateLabels("On", "Off", SwitchStateLabelPosition::OutsideTrack);
        switchOutside->GetVisualStyle().onTextColor = Color(0, 130, 0, 255);
        switchOutside->GetVisualStyle().offTextColor = Color(150, 0, 0, 255);
        switchOutside->onStateChanged = [](CheckedState, CheckedState newState) {
            debugOutput << "Sound " << (newState == CheckedState::Checked ? "on" : "off") << std::endl;
        };
        addSwitchLine("SwitchOutsideRow", {switchOutside},
                      makeHint("SwitchOutsideHint", "State text drawn outside the track (between track and main label)"));

        // ----- Switch flanked by two texts (BothSides): "Backup [switch] Auto-Save" -----
        auto switchSides = UltraCanvasSwitch::CreateWithSideLabels(
                "SwitchSides", 0, 0, "Backup", "Auto-Save", true);
        switchSides->SetTrackSize(44.0f, 22.0f);
        switchSides->onStateChanged = [](CheckedState, CheckedState newState) {
            debugOutput << "Mode: " << (newState == CheckedState::Checked ? "Auto-Save" : "Backup")
                        << std::endl;
        };
        addSwitchLine("SwitchSidesRow", {switchSides},
                      makeHint("SwitchSidesHint", "Two texts with the switch between them — click a text to pick that side"));

        // ----- Vertical orientation (plain + with check icon, side by side) -----
        auto switchVertical = UltraCanvasSwitch::Create("SwitchVertical", 0, 0, "Volume", true);
        switchVertical->SetOrientation(SwitchOrientation::Vertical);
        switchVertical->SetTrackSize(40.0f, 18.0f);  // long axis = 40, short axis = 18
        switchVertical->onStateChanged = [](CheckedState, CheckedState newState) {
            debugOutput << "Volume " << (newState == CheckedState::Checked ? "on" : "off") << std::endl;
        };

        auto switchVerticalCheck = UltraCanvasSwitch::Create("SwitchVerticalCheck", 0, 0, "Mic", true);
        switchVerticalCheck->SetOrientation(SwitchOrientation::Vertical);
        switchVerticalCheck->SetTrackSize(40.0f, 22.0f);
        switchVerticalCheck->GetVisualStyle().thumbIconStyle = SwitchThumbIconStyle::Check;
        switchVerticalCheck->onStateChanged = [](CheckedState, CheckedState newState) {
            debugOutput << "Mic " << (newState == CheckedState::Checked ? "on" : "off") << std::endl;
        };

        addSwitchLine("SwitchVerticalRow", {switchVertical, switchVerticalCheck},
                      makeHint("SwitchVerticalHint", "Vertical orientation (ON = top). Plain on left, with check icon on right."));

        // ===== SECTION 4: Radio Buttons =====
        auto radioCard = DemoKit::AddCard(page, "RadioCard", "Radio Button Groups");
        auto radioRow = std::make_shared<CheckboxExamplesContainer>("RadioGroupsRow");
        DemoKit::Add(radioCard, radioRow);

        // Create radio button group for themes (owned by the row so it survives function exit)
        radioRow->themeRadioGroup = std::make_shared<UltraCanvasRadioGroup>();

        auto radioTheme1 = UltraCanvasRadio::Create("RadioTheme1", 0, 0, "Light Theme", true);
        auto radioTheme2 = UltraCanvasRadio::Create("RadioTheme2", 0, 0, "Dark Theme", false);
        auto radioTheme3 = UltraCanvasRadio::Create("RadioTheme3", 0, 0, "Auto Theme", false);

        radioRow->themeRadioGroup->AddRadioButton(radioTheme1);
        radioRow->themeRadioGroup->AddRadioButton(radioTheme2);
        radioRow->themeRadioGroup->AddRadioButton(radioTheme3);

        // Create radio button group for quality (owned by the row so it survives function exit)
        radioRow->qualityRadioGroup = std::make_shared<UltraCanvasRadioGroup>();

        auto radioQuality1 = UltraCanvasRadio::Create("RadioQuality1", 0, 0, "Low Quality", false);
        auto radioQuality2 = UltraCanvasRadio::Create("RadioQuality2", 0, 0, "Medium Quality", true);
        auto radioQuality3 = UltraCanvasRadio::Create("RadioQuality3", 0, 0, "High Quality", false);

        radioRow->qualityRadioGroup->AddRadioButton(radioQuality1);
        radioRow->qualityRadioGroup->AddRadioButton(radioQuality2);
        radioRow->qualityRadioGroup->AddRadioButton(radioQuality3);

        // Selected option display
        auto selectedTheme = DemoKit::CreateText("SelectedTheme", "Selected Theme: Light");
        selectedTheme->SetTextColor(Color(0, 100, 200, 255));

        auto selectedQuality = DemoKit::CreateText("SelectedQuality", "Selected Quality: Medium");
        selectedQuality->SetTextColor(Color(0, 100, 200, 255));

        // Radio group callbacks
        radioRow->themeRadioGroup->onSelectionChanged = [selectedTheme](std::shared_ptr<UltraCanvasRadio> selected) {
            if (selected) {
                std::string themeName = selected->GetText();
                selectedTheme->SetText("Selected Theme: " + themeName.substr(0, themeName.find(" ")));
                debugOutput << "Theme changed to: " << selected->GetText() << std::endl;
            }
        };

        radioRow->qualityRadioGroup->onSelectionChanged = [selectedQuality](std::shared_ptr<UltraCanvasRadio> selected) {
            if (selected) {
                std::string qualityName = selected->GetText();
                selectedQuality->SetText("Selected Quality: " + qualityName.substr(0, qualityName.find(" ")));
                debugOutput << "Quality changed to: " << selected->GetText() << std::endl;
            }
        };

        // One column per group, then the selections
        auto themeColumn = DemoKit::CreateColumn("RadioThemeColumn", 6);
        DemoKit::Add(radioRow, themeColumn);
        DemoKit::AddFixed(themeColumn, radioTheme1);
        DemoKit::AddFixed(themeColumn, radioTheme2);
        DemoKit::AddFixed(themeColumn, radioTheme3);

        auto qualityColumn = DemoKit::CreateColumn("RadioQualityColumn", 6);
        DemoKit::Add(radioRow, qualityColumn);
        DemoKit::AddFixed(qualityColumn, radioQuality1);
        DemoKit::AddFixed(qualityColumn, radioQuality2);
        DemoKit::AddFixed(qualityColumn, radioQuality3);

        auto selectedColumn = DemoKit::CreateColumn("RadioSelectedColumn", 6);
        DemoKit::Add(radioRow, selectedColumn, 1);
        DemoKit::Add(selectedColumn, selectedTheme);
        DemoKit::Add(selectedColumn, selectedQuality);

        // ===== SECTION 5: Styled Checkboxes =====
        auto styledCard = DemoKit::AddCard(page, "StyledCheckboxesCard", "Custom Styled Checkboxes");
        auto styledRow = DemoKit::AddRow(styledCard, "StyledCheckboxesRow", 20);
        styledRow->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);

        // Material style checkbox
        auto materialCheckbox = std::make_shared<UltraCanvasCheckbox>("MaterialCheckbox", "Material Design");
        materialCheckbox->SetStyle(CheckboxStyle::Material);
        materialCheckbox->GetVisualStyle().boxColor = Color(33, 150, 243, 255);  // Material Blue
        materialCheckbox->GetVisualStyle().checkmarkColor = Color(255, 255, 255, 255);
        materialCheckbox->GetVisualStyle().boxSize = 20.0f;

        // Rounded style checkbox
        auto roundedCheckbox = std::make_shared<UltraCanvasCheckbox>("RoundedCheckbox", "Rounded Corners");
        roundedCheckbox->SetStyle(CheckboxStyle::Rounded);
        roundedCheckbox->GetVisualStyle().cornerRadius = 5.0f;
        roundedCheckbox->GetVisualStyle().boxColor = Color(100, 200, 100, 255);

        // Custom colored checkbox
        auto customColorCheckbox = std::make_shared<UltraCanvasCheckbox>("CustomColorCheckbox", "Custom Colors");
        customColorCheckbox->SetColors(
                Color(255, 100, 100, 255),  // Red box
                Color(255, 255, 0, 255),     // Yellow checkmark
                Color(100, 100, 255, 255)    // Blue text
        );

        // Large checkbox
        auto largeCheckbox = std::make_shared<UltraCanvasCheckbox>("LargeCheckbox", "Large Size");
        largeCheckbox->SetBoxSize(28.0f);
        largeCheckbox->SetFont("Arial", 16.0f, FontWeight::Bold);

        DemoKit::Add(styledRow, materialCheckbox);
        DemoKit::Add(styledRow, roundedCheckbox);
        DemoKit::Add(styledRow, customColorCheckbox);
        DemoKit::Add(styledRow, largeCheckbox);

        // ===== SECTION 6: Interactive Demo =====
        auto interactiveCard = DemoKit::AddCard(page, "InteractiveCard", "Interactive Feature Demo");
        auto interactiveRow = DemoKit::AddRow(interactiveCard, "InteractiveRow", 20);

        // Feature checkboxes
        auto featureContainer = DemoKit::CreateColumn("FeatureContainer", 6);
        featureContainer->SetBackgroundColor(Color(230, 240, 250, 255));
        featureContainer->SetBorders(1, Color(180, 180, 180, 255));
        featureContainer->SetPadding(10);
        DemoKit::Add(interactiveRow, featureContainer);

        auto featureTitle = DemoKit::AddText(featureContainer, "FeatureTitle", "Enable Features:");
        featureTitle->SetFontWeight(FontWeight::Bold);

        auto feature1 = std::make_shared<UltraCanvasCheckbox>("Feature1", "Spell Check");
        auto feature2 = std::make_shared<UltraCanvasCheckbox>("Feature2", "Auto-Complete");
        auto feature3 = std::make_shared<UltraCanvasCheckbox>("Feature3", "Syntax Highlighting");
        auto feature4 = std::make_shared<UltraCanvasCheckbox>("Feature4", "Line Numbers");

        auto feature5 = std::make_shared<UltraCanvasCheckbox>("Feature5", "Word Wrap");
        auto feature6 = std::make_shared<UltraCanvasCheckbox>("Feature6", "Auto-Indent");
        auto feature7 = std::make_shared<UltraCanvasCheckbox>("Feature7", "Show Whitespace");
        auto feature8 = std::make_shared<UltraCanvasCheckbox>("Feature8", "Code Folding");

        // Two columns of four, indented under the title
        auto featureColumns = DemoKit::CreateRow("FeatureColumns", 20);
        featureColumns->SetPadding(0, 0, 0, 10);
        DemoKit::Add(featureContainer, featureColumns);
        auto featureColumn1 = DemoKit::CreateColumn("FeatureColumn1", 2);
        auto featureColumn2 = DemoKit::CreateColumn("FeatureColumn2", 2);
        DemoKit::Add(featureColumns, featureColumn1);
        DemoKit::Add(featureColumns, featureColumn2);
        DemoKit::AddFixed(featureColumn1, feature1);
        DemoKit::AddFixed(featureColumn1, feature2);
        DemoKit::AddFixed(featureColumn1, feature3);
        DemoKit::AddFixed(featureColumn1, feature4);
        DemoKit::AddFixed(featureColumn2, feature5);
        DemoKit::AddFixed(featureColumn2, feature6);
        DemoKit::AddFixed(featureColumn2, feature7);
        DemoKit::AddFixed(featureColumn2, feature8);

        // Control buttons and the selection, beside the features
        auto controlsColumn = DemoKit::CreateColumn("FeatureControls", 12);
        DemoKit::Add(interactiveRow, controlsColumn, 1);
        auto buttonsRow = DemoKit::AddRow(controlsColumn, "FeatureButtons", 10);

        auto selectAllBtn = std::make_shared<UltraCanvasButton>("SelectAllBtn", 0, 0, 120, 30);
        selectAllBtn->SetText("Select All");
        selectAllBtn->SetOnClick([feature1, feature2, feature3, feature4, feature5, feature6, feature7, feature8]() {
            feature1->SetChecked(true);
            feature2->SetChecked(true);
            feature3->SetChecked(true);
            feature4->SetChecked(true);
            feature5->SetChecked(true);
            feature6->SetChecked(true);
            feature7->SetChecked(true);
            feature8->SetChecked(true);
            debugOutput << "All features selected" << std::endl;
        });

        auto clearAllBtn = std::make_shared<UltraCanvasButton>("ClearAllBtn", 0, 0, 120, 30);
        clearAllBtn->SetText("Clear All");
        clearAllBtn->SetOnClick([feature1, feature2, feature3, feature4, feature5, feature6, feature7, feature8]() {
            feature1->SetChecked(false);
            feature2->SetChecked(false);
            feature3->SetChecked(false);
            feature4->SetChecked(false);
            feature5->SetChecked(false);
            feature6->SetChecked(false);
            feature7->SetChecked(false);
            feature8->SetChecked(false);
            debugOutput << "All features cleared" << std::endl;
        });

        auto toggleAllBtn = std::make_shared<UltraCanvasButton>("ToggleAllBtn", 0, 0, 120, 30);
        toggleAllBtn->SetText("Toggle All");
        toggleAllBtn->SetOnClick([feature1, feature2, feature3, feature4, feature5, feature6, feature7, feature8]() {
            feature1->Toggle();
            feature2->Toggle();
            feature3->Toggle();
            feature4->Toggle();
            feature5->Toggle();
            feature6->Toggle();
            feature7->Toggle();
            feature8->Toggle();
            debugOutput << "All features toggled" << std::endl;
        });

        // Feature status display
        auto featureStatus = DemoKit::CreateText("FeatureStatus", "Selected Features:\nNone");
        featureStatus->SetBackgroundColor(Color(255, 255, 255, 255));
        featureStatus->SetBorders(1.0f, Color(200, 200, 200, 255));
        featureStatus->SetPadding(5.0f);

        // Update feature status
        auto updateFeatureStatus = [featureStatus, feature1, feature2, feature3, feature4, feature5, feature6, feature7, feature8]() {
            std::vector<std::string> selectedFeatures;
            if (feature1->IsChecked()) selectedFeatures.push_back("Spell Check");
            if (feature2->IsChecked()) selectedFeatures.push_back("Auto-Complete");
            if (feature3->IsChecked()) selectedFeatures.push_back("Syntax Highlighting");
            if (feature4->IsChecked()) selectedFeatures.push_back("Line Numbers");
            if (feature5->IsChecked()) selectedFeatures.push_back("Word Wrap");
            if (feature6->IsChecked()) selectedFeatures.push_back("Auto-Indent");
            if (feature7->IsChecked()) selectedFeatures.push_back("Show Whitespace");
            if (feature8->IsChecked()) selectedFeatures.push_back("Code Folding");

            if (selectedFeatures.empty()) {
                featureStatus->SetText("Selected Features:\nNone");
            } else {
                std::string statusText = "Selected Features:\n";
                for (size_t i = 0; i < selectedFeatures.size(); ++i) {
                    statusText += selectedFeatures[i];
                    if (i < selectedFeatures.size() - 1) statusText += ", ";
                }
                featureStatus->SetText(statusText);
            }
        };

        // Set callbacks for all feature checkboxes
        feature1->onStateChanged = [updateFeatureStatus](CheckedState, CheckedState) { updateFeatureStatus(); };
        feature2->onStateChanged = [updateFeatureStatus](CheckedState, CheckedState) { updateFeatureStatus(); };
        feature3->onStateChanged = [updateFeatureStatus](CheckedState, CheckedState) { updateFeatureStatus(); };
        feature4->onStateChanged = [updateFeatureStatus](CheckedState, CheckedState) { updateFeatureStatus(); };
        feature5->onStateChanged = [updateFeatureStatus](CheckedState, CheckedState) { updateFeatureStatus(); };
        feature6->onStateChanged = [updateFeatureStatus](CheckedState, CheckedState) { updateFeatureStatus(); };
        feature7->onStateChanged = [updateFeatureStatus](CheckedState, CheckedState) { updateFeatureStatus(); };
        feature8->onStateChanged = [updateFeatureStatus](CheckedState, CheckedState) { updateFeatureStatus(); };

        DemoKit::AddFixed(buttonsRow, selectAllBtn);
        DemoKit::AddFixed(buttonsRow, clearAllBtn);
        DemoKit::AddFixed(buttonsRow, toggleAllBtn);
        DemoKit::Add(controlsColumn, featureStatus);

        // Info panel at bottom
        auto infoPanel = DemoKit::AddCard(page, "InfoPanel");
        infoPanel->SetBackgroundColor(Color(240, 248, 255, 255));
        DemoKit::AddText(infoPanel, "InfoText",
                "ℹ️ This demo showcases the UltraCanvasCheckbox component with various styles and configurations. "
                "Click checkboxes to see console output. Try the interactive controls to manipulate checkbox states programmatically.");

        return page;
    }

} // namespace UltraCanvas