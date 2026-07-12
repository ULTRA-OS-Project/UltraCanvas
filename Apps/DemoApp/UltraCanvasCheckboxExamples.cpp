// UltraCanvasCheckboxExamples.cpp
// Interactive checkbox component demonstration
// Version: 1.2.0
// Last Modified: 2026-05-07
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
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

// Container that owns the radio groups so they outlive CreateCheckboxExamples().
// UltraCanvasRadioGroup is not a UI element and is not stored by the radio buttons,
// so without an owning instance the groups would be destroyed on function return
// and exclusive selection / onSelectionChanged callbacks would stop working.
    class CheckboxExamplesContainer : public UltraCanvasContainer {
    public:
        std::shared_ptr<UltraCanvasRadioGroup> themeRadioGroup;
        std::shared_ptr<UltraCanvasRadioGroup> qualityRadioGroup;

        CheckboxExamplesContainer(const std::string& id, float x, float y, float w, float h)
                : UltraCanvasContainer(id, x, y, w, h) {}
    };

// Helper function to create a separator line
    std::shared_ptr<UltraCanvasContainer> CreateSeparatorLine(float x, float y, float width) {
        auto separator = std::make_shared<UltraCanvasContainer>("Separator" + std::to_string(x), x, y, width, 2);
        separator->SetBackgroundColor(Color(200, 200, 200, 255));
        return separator;
    }

// Helper function to create a section title
    std::shared_ptr<UltraCanvasLabel> CreateSectionTitle(float x, float y, const std::string& text) {
        auto title = std::make_shared<UltraCanvasLabel>("SectionTitle" + std::to_string(x), x, y, 600, 25);
        title->SetText(text);
        title->SetFontSize(14);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        return title;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateCheckboxExamples() {
        // Main container for all checkbox examples (custom subclass owns the radio groups)
        auto mainContainer = std::make_shared<CheckboxExamplesContainer>("CheckboxMainContainer", 0, 0, 1020, 1600);
        mainContainer->SetBackgroundColor(Colors::White);
        mainContainer->SetPadding(0,0,10,0);

        // Title
        auto mainTitle = std::make_shared<UltraCanvasLabel>("CheckboxMainTitle", 20, 10, 900, 30);
        mainTitle->SetText("UltraCanvas Checkbox Component Examples");
        mainTitle->SetFontSize(18);
        mainTitle->SetFontWeight(FontWeight::Bold);
        mainContainer->AddChild(mainTitle);

        // Description
        auto description = std::make_shared<UltraCanvasLabel>("CheckboxDescription", 20, 45, 960, 40);
        description->SetText("Demonstrates various checkbox styles, states, and configurations");
        description->SetWrap(TextWrap::WrapWord);
        description->SetTextColor(Color(80, 80, 80, 255));
        mainContainer->AddChild(description);

        long currentY = 100;

        // ===== SECTION 1: Basic Checkboxes =====
        mainContainer->AddChild(CreateSectionTitle(20, currentY, "Basic Checkboxes"));
        currentY += 35;

        // Standard checkbox - unchecked
        auto basicCheckbox1 = std::make_shared<UltraCanvasCheckbox>("BasicCheckbox1", 30, currentY, 200, 24, "Standard Checkbox");
        basicCheckbox1->SetChecked(false);
        basicCheckbox1->onChecked = []() {
            debugOutput << "Basic checkbox checked!" << std::endl;
        };
        basicCheckbox1->onUnchecked = []() {
            debugOutput << "Basic checkbox unchecked!" << std::endl;
        };
        mainContainer->AddChild(basicCheckbox1);

        // Standard checkbox - checked
        auto basicCheckbox2 = std::make_shared<UltraCanvasCheckbox>("BasicCheckbox2", 250, currentY, 200, 24, "Pre-checked Box");
        basicCheckbox2->SetChecked(true);
        mainContainer->AddChild(basicCheckbox2);

        // Disabled checkbox - unchecked
        auto disabledCheckbox1 = std::make_shared<UltraCanvasCheckbox>("DisabledCheckbox1", 470, currentY, 200, 24, "Disabled Unchecked");
        disabledCheckbox1->SetChecked(false);
        disabledCheckbox1->SetDisabled(true);
        mainContainer->AddChild(disabledCheckbox1);

        // Disabled checkbox - checked
        auto disabledCheckbox2 = std::make_shared<UltraCanvasCheckbox>("DisabledCheckbox2", 690, currentY, 200, 24, "Disabled Checked");
        disabledCheckbox2->SetChecked(true);
        disabledCheckbox2->SetDisabled(true);
        mainContainer->AddChild(disabledCheckbox2);

        currentY += 40;
        mainContainer->AddChild(CreateSeparatorLine(20, currentY, 960));
        currentY += 20;

        // ===== SECTION 2: Tri-State Checkboxes =====
        mainContainer->AddChild(CreateSectionTitle(20, currentY, "Tri-State Checkboxes (Indeterminate)"));
        currentY += 35;

        // Tri-state checkbox
        auto triStateCheckbox = std::make_shared<UltraCanvasCheckbox>("TriStateCheckbox", 30, currentY, 250, 24, "Select All Items");
        triStateCheckbox->SetAllowIndeterminate(true);
        triStateCheckbox->SetCheckState(CheckedState::Indeterminate);

        // Sub-items for tri-state demonstration
        auto subItem1 = std::make_shared<UltraCanvasCheckbox>("SubItem1", 60, currentY + 30, 200, 24, "Item 1");
        auto subItem2 = std::make_shared<UltraCanvasCheckbox>("SubItem2", 60, currentY + 55, 200, 24, "Item 2");
        auto subItem3 = std::make_shared<UltraCanvasCheckbox>("SubItem3", 60, currentY + 80, 200, 24, "Item 3");

        subItem1->SetChecked(true);
        subItem2->SetChecked(false);
        subItem3->SetChecked(true);

        // Update parent state based on children
        auto updateParentState = [triStateCheckbox, subItem1, subItem2, subItem3]() {
            int checkedCount = 0;
            if (subItem1->IsChecked()) checkedCount++;
            if (subItem2->IsChecked()) checkedCount++;
            if (subItem3->IsChecked()) checkedCount++;

            if (checkedCount == 0) {
                triStateCheckbox->SetCheckState(CheckedState::Unchecked);
            } else if (checkedCount == 3) {
                triStateCheckbox->SetCheckState(CheckedState::Checked);
            } else {
                triStateCheckbox->SetCheckState(CheckedState::Indeterminate);
            }
        };

        // Set callbacks for sub-items
        subItem1->onStateChanged = [updateParentState](CheckedState, CheckedState) { updateParentState(); };
        subItem2->onStateChanged = [updateParentState](CheckedState, CheckedState) { updateParentState(); };
        subItem3->onStateChanged = [updateParentState](CheckedState, CheckedState) { updateParentState(); };

        // Parent checkbox callback
        triStateCheckbox->onStateChanged = [subItem1, subItem2, subItem3](CheckedState oldState, CheckedState newState) {
            if (newState == CheckedState::Checked) {
                subItem1->SetChecked(true);
                subItem2->SetChecked(true);
                subItem3->SetChecked(true);
            } else if (newState == CheckedState::Unchecked) {
                subItem1->SetChecked(false);
                subItem2->SetChecked(false);
                subItem3->SetChecked(false);
            }
        };

        mainContainer->AddChild(triStateCheckbox);
        mainContainer->AddChild(subItem1);
        mainContainer->AddChild(subItem2);
        mainContainer->AddChild(subItem3);

        // Status display for tri-state
        auto triStateStatus = std::make_shared<UltraCanvasLabel>("TriStateStatus", 300, currentY + 40, 300, 24);
        triStateStatus->SetText("State: Indeterminate (2 of 3 selected)");
        triStateStatus->SetTextColor(Color(100, 100, 100, 255));
        mainContainer->AddChild(triStateStatus);

        // Update status label
        auto updateStatusLabel = [triStateStatus, triStateCheckbox]() {
            switch(triStateCheckbox->GetCheckState()) {
                case CheckedState::Unchecked:
                    triStateStatus->SetText("State: Unchecked (0 selected)");
                    break;
                case CheckedState::Checked:
                    triStateStatus->SetText("State: Checked (all selected)");
                    break;
                case CheckedState::Indeterminate:
                    triStateStatus->SetText("State: Indeterminate (partially selected)");
                    break;
            }
        };

        triStateCheckbox->onStateChanged = [subItem1, subItem2, subItem3, updateStatusLabel](CheckedState oldState, CheckedState newState) {
            if (newState == CheckedState::Checked) {
                subItem1->SetChecked(true);
                subItem2->SetChecked(true);
                subItem3->SetChecked(true);
            } else if (newState == CheckedState::Unchecked) {
                subItem1->SetChecked(false);
                subItem2->SetChecked(false);
                subItem3->SetChecked(false);
            }
            updateStatusLabel();
        };

        currentY += 120;
        mainContainer->AddChild(CreateSeparatorLine(20, currentY, 960));
        currentY += 20;

        // ===== SECTION 3: Switch Style Checkboxes =====
        mainContainer->AddChild(CreateSectionTitle(20, currentY, "Switch Style Toggles"));
        currentY += 35;

        // Create switches
        auto switch1 = UltraCanvasSwitch::Create("Switch1", 30, currentY, "Enable Notifications", true);
        auto switch2 = UltraCanvasSwitch::Create("Switch2", 30, currentY + 35, "Dark Mode", false);
        auto switch3 = UltraCanvasSwitch::Create("Switch3", 30, currentY + 70, "Auto-Save", true);

        // Switch status labels
        auto switchStatus1 = std::make_shared<UltraCanvasLabel>("SwitchStatus1", 250, currentY, 100, 24);
        switchStatus1->SetText("ON");
        switchStatus1->SetTextColor(Color(0, 150, 0, 255));

        auto switchStatus2 = std::make_shared<UltraCanvasLabel>("SwitchStatus2", 250, currentY + 35, 100, 24);
        switchStatus2->SetText("OFF");
        switchStatus2->SetTextColor(Color(150, 0, 0, 255));

        auto switchStatus3 = std::make_shared<UltraCanvasLabel>("SwitchStatus3", 250, currentY + 70, 100, 24);
        switchStatus3->SetText("ON");
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

        mainContainer->AddChild(switch1);
        mainContainer->AddChild(switch2);
        mainContainer->AddChild(switch3);
        mainContainer->AddChild(switchStatus1);
        mainContainer->AddChild(switchStatus2);
        mainContainer->AddChild(switchStatus3);

        currentY += 115;

        // ----- Switch with built-in check icon (Check style) -----
        auto switchCheck = UltraCanvasSwitch::Create("SwitchCheck", 30, currentY, "Wi-Fi", true);
        switchCheck->GetVisualStyle().thumbIconStyle = SwitchThumbIconStyle::Check;
        switchCheck->GetVisualStyle().thumbIconOnColor = Color(76, 175, 80, 255);
        switchCheck->onStateChanged = [](CheckedState, CheckedState newState) {
            debugOutput << "Wi-Fi " << (newState == CheckedState::Checked ? "on" : "off") << std::endl;
        };
        auto switchCheckHint = std::make_shared<UltraCanvasLabel>("SwitchCheckHint", 250, currentY, 400, 24);
        switchCheckHint->SetText("Built-in checkmark icon (drawn via path API)");
        switchCheckHint->SetTextColor(Color(100, 100, 100, 255));
        mainContainer->AddChild(switchCheck);
        mainContainer->AddChild(switchCheckHint);
        currentY += 35;

        // ----- Switch with custom image icons (check.png + x.png) -----
        UCImagePtr iconCheckOn = UCImage::Load(NormalizePath(GetResourcesDir() + "media/icons/check.png"), false);
        UCImagePtr iconCheckOff = UCImage::Load(NormalizePath(GetResourcesDir() + "media/icons/x.png"), false);

        auto switchImg = UltraCanvasSwitch::Create("SwitchImg", 30, currentY, "Bluetooth", true);
        switchImg->SetTrackSize(48.0f, 26.0f);
        switchImg->SetThumbIcons(iconCheckOn, iconCheckOff);
        switchImg->onStateChanged = [](CheckedState, CheckedState newState) {
            debugOutput << "Bluetooth " << (newState == CheckedState::Checked ? "on" : "off") << std::endl;
        };
        auto switchImgHint = std::make_shared<UltraCanvasLabel>("SwitchImgHint", 250, currentY + 3, 400, 24);
        switchImgHint->SetText("Custom image icons (check.png / x.png) inside thumb");
        switchImgHint->SetTextColor(Color(100, 100, 100, 255));
        mainContainer->AddChild(switchImg);
        mainContainer->AddChild(switchImgHint);
        currentY += 40;

        // ----- Switch with custom ON icon only (demonstrates plain-circle fallback for OFF) -----
        auto switchImgFallback = UltraCanvasSwitch::Create("SwitchImgFallback", 30, currentY, "Sync", false);
        switchImgFallback->SetTrackSize(48.0f, 26.0f);
        switchImgFallback->SetThumbIcons(iconCheckOn, nullptr);  // OFF icon missing → plain circle
        switchImgFallback->onStateChanged = [](CheckedState, CheckedState newState) {
            debugOutput << "Sync " << (newState == CheckedState::Checked ? "on" : "off") << std::endl;
        };
        auto switchImgFallbackHint = std::make_shared<UltraCanvasLabel>("SwitchImgFallbackHint", 250, currentY + 3, 500, 24);
        switchImgFallbackHint->SetText("Only ON icon supplied — OFF state falls back to plain circle");
        switchImgFallbackHint->SetTextColor(Color(100, 100, 100, 255));
        mainContainer->AddChild(switchImgFallback);
        mainContainer->AddChild(switchImgFallbackHint);
        currentY += 40;

        // ----- Switch with ON/OFF labels INSIDE the track -----
        auto switchInside = UltraCanvasSwitch::Create("SwitchInside", 30, currentY, "Power", true);
        switchInside->SetTrackSize(60.0f, 24.0f);
        switchInside->SetStateLabels("ON", "OFF", SwitchStateLabelPosition::InsideTrack);
        switchInside->onStateChanged = [](CheckedState, CheckedState newState) {
            debugOutput << "Power " << (newState == CheckedState::Checked ? "on" : "off") << std::endl;
        };
        auto switchInsideHint = std::make_shared<UltraCanvasLabel>("SwitchInsideHint", 280, currentY + 2, 400, 24);
        switchInsideHint->SetText("ON/OFF text inside track, opposite the thumb");
        switchInsideHint->SetTextColor(Color(100, 100, 100, 255));
        mainContainer->AddChild(switchInside);
        mainContainer->AddChild(switchInsideHint);
        currentY += 40;

        // ----- Switch with ON/OFF labels OUTSIDE the track -----
        auto switchOutside = UltraCanvasSwitch::Create("SwitchOutside", 30, currentY, "Sound", false);
        switchOutside->SetStateLabels("On", "Off", SwitchStateLabelPosition::OutsideTrack);
        switchOutside->GetVisualStyle().onTextColor = Color(0, 130, 0, 255);
        switchOutside->GetVisualStyle().offTextColor = Color(150, 0, 0, 255);
        switchOutside->onStateChanged = [](CheckedState, CheckedState newState) {
            debugOutput << "Sound " << (newState == CheckedState::Checked ? "on" : "off") << std::endl;
        };
        auto switchOutsideHint = std::make_shared<UltraCanvasLabel>("SwitchOutsideHint", 280, currentY, 400, 24);
        switchOutsideHint->SetText("State text drawn outside the track (between track and main label)");
        switchOutsideHint->SetTextColor(Color(100, 100, 100, 255));
        mainContainer->AddChild(switchOutside);
        mainContainer->AddChild(switchOutsideHint);
        currentY += 40;

        // ----- Vertical orientation (plain + with check icon, side by side) -----
        auto switchVertical = UltraCanvasSwitch::Create("SwitchVertical", 30, currentY, "Volume", true);
        switchVertical->SetOrientation(SwitchOrientation::Vertical);
        switchVertical->SetTrackSize(40.0f, 18.0f);  // long axis = 40, short axis = 18
        switchVertical->onStateChanged = [](CheckedState, CheckedState newState) {
            debugOutput << "Volume " << (newState == CheckedState::Checked ? "on" : "off") << std::endl;
        };

        auto switchVerticalCheck = UltraCanvasSwitch::Create("SwitchVerticalCheck", 180, currentY, "Mic", true);
        switchVerticalCheck->SetOrientation(SwitchOrientation::Vertical);
        switchVerticalCheck->SetTrackSize(40.0f, 22.0f);
        switchVerticalCheck->GetVisualStyle().thumbIconStyle = SwitchThumbIconStyle::Check;
        switchVerticalCheck->onStateChanged = [](CheckedState, CheckedState newState) {
            debugOutput << "Mic " << (newState == CheckedState::Checked ? "on" : "off") << std::endl;
        };

        auto switchVerticalHint = std::make_shared<UltraCanvasLabel>("SwitchVerticalHint", 320, currentY + 12, 600, 24);
        switchVerticalHint->SetText("Vertical orientation (ON = top). Plain on left, with check icon on right.");
        switchVerticalHint->SetTextColor(Color(100, 100, 100, 255));

        mainContainer->AddChild(switchVertical);
        mainContainer->AddChild(switchVerticalCheck);
        mainContainer->AddChild(switchVerticalHint);
        currentY += 65;

        mainContainer->AddChild(CreateSeparatorLine(20, currentY, 960));
        currentY += 20;

        // ===== SECTION 4: Radio Buttons =====
        mainContainer->AddChild(CreateSectionTitle(20, currentY, "Radio Button Groups"));
        currentY += 35;

        // Create radio button group for themes (owned by the container so it survives function exit)
        mainContainer->themeRadioGroup = std::make_shared<UltraCanvasRadioGroup>();

        auto radioTheme1 = UltraCanvasRadio::Create("RadioTheme1", 30, currentY, "Light Theme", true);
        auto radioTheme2 = UltraCanvasRadio::Create("RadioTheme2", 30, currentY + 30, "Dark Theme", false);
        auto radioTheme3 = UltraCanvasRadio::Create("RadioTheme3", 30, currentY + 60, "Auto Theme", false);

        mainContainer->themeRadioGroup->AddRadioButton(radioTheme1);
        mainContainer->themeRadioGroup->AddRadioButton(radioTheme2);
        mainContainer->themeRadioGroup->AddRadioButton(radioTheme3);

        // Create radio button group for quality (owned by the container so it survives function exit)
        mainContainer->qualityRadioGroup = std::make_shared<UltraCanvasRadioGroup>();

        auto radioQuality1 = UltraCanvasRadio::Create("RadioQuality1", 250, currentY, "Low Quality", false);
        auto radioQuality2 = UltraCanvasRadio::Create("RadioQuality2", 250, currentY + 30, "Medium Quality", true);
        auto radioQuality3 = UltraCanvasRadio::Create("RadioQuality3", 250, currentY + 60, "High Quality", false);

        mainContainer->qualityRadioGroup->AddRadioButton(radioQuality1);
        mainContainer->qualityRadioGroup->AddRadioButton(radioQuality2);
        mainContainer->qualityRadioGroup->AddRadioButton(radioQuality3);

        // Selected option display
        auto selectedTheme = std::make_shared<UltraCanvasLabel>("SelectedTheme", 470, currentY + 20, 300, 24);
        selectedTheme->SetText("Selected Theme: Light");
        selectedTheme->SetTextColor(Color(0, 100, 200, 255));

        auto selectedQuality = std::make_shared<UltraCanvasLabel>("SelectedQuality", 470, currentY + 50, 300, 24);
        selectedQuality->SetText("Selected Quality: Medium");
        selectedQuality->SetTextColor(Color(0, 100, 200, 255));

        // Radio group callbacks
        mainContainer->themeRadioGroup->onSelectionChanged = [selectedTheme](std::shared_ptr<UltraCanvasRadio> selected) {
            if (selected) {
                std::string themeName = selected->GetText();
                selectedTheme->SetText("Selected Theme: " + themeName.substr(0, themeName.find(" ")));
                debugOutput << "Theme changed to: " << selected->GetText() << std::endl;
            }
        };

        mainContainer->qualityRadioGroup->onSelectionChanged = [selectedQuality](std::shared_ptr<UltraCanvasRadio> selected) {
            if (selected) {
                std::string qualityName = selected->GetText();
                selectedQuality->SetText("Selected Quality: " + qualityName.substr(0, qualityName.find(" ")));
                debugOutput << "Quality changed to: " << selected->GetText() << std::endl;
            }
        };

        mainContainer->AddChild(radioTheme1);
        mainContainer->AddChild(radioTheme2);
        mainContainer->AddChild(radioTheme3);
        mainContainer->AddChild(radioQuality1);
        mainContainer->AddChild(radioQuality2);
        mainContainer->AddChild(radioQuality3);
        mainContainer->AddChild(selectedTheme);
        mainContainer->AddChild(selectedQuality);

        currentY += 100;
        mainContainer->AddChild(CreateSeparatorLine(20, currentY, 960));
        currentY += 20;

        // ===== SECTION 5: Styled Checkboxes =====
        mainContainer->AddChild(CreateSectionTitle(20, currentY, "Custom Styled Checkboxes"));
        currentY += 35;

        // Material style checkbox
        auto materialCheckbox = std::make_shared<UltraCanvasCheckbox>("MaterialCheckbox", 30, currentY, 200, 30, "Material Design");
        materialCheckbox->SetStyle(CheckboxStyle::Material);
        materialCheckbox->GetVisualStyle().boxColor = Color(33, 150, 243, 255);  // Material Blue
        materialCheckbox->GetVisualStyle().checkmarkColor = Color(255, 255, 255, 255);
        materialCheckbox->GetVisualStyle().boxSize = 20.0f;

        // Rounded style checkbox
        auto roundedCheckbox = std::make_shared<UltraCanvasCheckbox>("RoundedCheckbox", 250, currentY, 200, 30, "Rounded Corners");
        roundedCheckbox->SetStyle(CheckboxStyle::Rounded);
        roundedCheckbox->GetVisualStyle().cornerRadius = 5.0f;
        roundedCheckbox->GetVisualStyle().boxColor = Color(100, 200, 100, 255);

        // Custom colored checkbox
        auto customColorCheckbox = std::make_shared<UltraCanvasCheckbox>("CustomColorCheckbox", 470, currentY, 200, 30, "Custom Colors");
        customColorCheckbox->SetColors(
                Color(255, 100, 100, 255),  // Red box
                Color(255, 255, 0, 255),     // Yellow checkmark
                Color(100, 100, 255, 255)    // Blue text
        );

        // Large checkbox
        auto largeCheckbox = std::make_shared<UltraCanvasCheckbox>("LargeCheckbox", 690, currentY, 250, 40, "Large Size");
        largeCheckbox->SetBoxSize(28.0f);
        largeCheckbox->SetFont("Arial", 16.0f, FontWeight::Bold);

        mainContainer->AddChild(materialCheckbox);
        mainContainer->AddChild(roundedCheckbox);
        mainContainer->AddChild(customColorCheckbox);
        mainContainer->AddChild(largeCheckbox);

        currentY += 50;
        mainContainer->AddChild(CreateSeparatorLine(20, currentY, 960));
        currentY += 20;

        // ===== SECTION 6: Interactive Demo =====
        mainContainer->AddChild(CreateSectionTitle(20, currentY, "Interactive Feature Demo"));
        currentY += 35;

        // Feature checkboxes
        auto featureContainer = std::make_shared<UltraCanvasContainer>("FeatureContainer", 30, currentY, 400, 150);
        featureContainer->SetBackgroundColor(Color(230, 240, 250, 255));
        featureContainer->SetBorders(1, Color(180, 180, 180, 255));

        auto featureTitle = std::make_shared<UltraCanvasLabel>("FeatureTitle", 10, 10, 200, 20);
        featureTitle->SetText("Enable Features:");
        featureTitle->SetFontWeight(FontWeight::Bold);
        featureContainer->AddChild(featureTitle);

        auto feature1 = std::make_shared<UltraCanvasCheckbox>("Feature1", 20, 35, 180, 24, "Spell Check");
        auto feature2 = std::make_shared<UltraCanvasCheckbox>("Feature2", 20, 60, 180, 24, "Auto-Complete");
        auto feature3 = std::make_shared<UltraCanvasCheckbox>("Feature3", 20, 85, 180, 24, "Syntax Highlighting");
        auto feature4 = std::make_shared<UltraCanvasCheckbox>("Feature4", 20, 110, 180, 24, "Line Numbers");

        auto feature5 = std::make_shared<UltraCanvasCheckbox>("Feature5", 210, 35, 180, 24, "Word Wrap");
        auto feature6 = std::make_shared<UltraCanvasCheckbox>("Feature6", 210, 60, 180, 24, "Auto-Indent");
        auto feature7 = std::make_shared<UltraCanvasCheckbox>("Feature7", 210, 85, 180, 24, "Show Whitespace");
        auto feature8 = std::make_shared<UltraCanvasCheckbox>("Feature8", 210, 110, 180, 24, "Code Folding");

        featureContainer->AddChild(feature1);
        featureContainer->AddChild(feature2);
        featureContainer->AddChild(feature3);
        featureContainer->AddChild(feature4);
        featureContainer->AddChild(feature5);
        featureContainer->AddChild(feature6);
        featureContainer->AddChild(feature7);
        featureContainer->AddChild(feature8);

        mainContainer->AddChild(featureContainer);

        // Control buttons
        auto selectAllBtn = std::make_shared<UltraCanvasButton>("SelectAllBtn", 450, currentY, 120, 30);
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

        auto clearAllBtn = std::make_shared<UltraCanvasButton>("ClearAllBtn", 580, currentY, 120, 30);
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

        auto toggleAllBtn = std::make_shared<UltraCanvasButton>("ToggleAllBtn", 710, currentY, 120, 30);
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
        auto featureStatus = std::make_shared<UltraCanvasLabel>("FeatureStatus", 450, currentY + 45, 380, 60);
        featureStatus->SetText("Selected Features:\nNone");
        featureStatus->SetWrap(TextWrap::WrapWord);
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

        mainContainer->AddChild(selectAllBtn);
        mainContainer->AddChild(clearAllBtn);
        mainContainer->AddChild(toggleAllBtn);
        mainContainer->AddChild(featureStatus);

        // Info panel at bottom
        currentY += 170;
        auto infoPanel = std::make_shared<UltraCanvasContainer>("InfoPanel", 20, currentY, 960, 60);
        infoPanel->SetBackgroundColor(Color(240, 248, 255, 255));
        infoPanel->SetBorders(1.0f);
        //infoPanel->SetBorderColor(Color(100, 150, 200, 255));

        auto infoText = std::make_shared<UltraCanvasLabel>("InfoText", 10, 10, 940, 40);
        infoText->SetText("ℹ️ This demo showcases the UltraCanvasCheckbox component with various styles and configurations. "
                          "Click checkboxes to see console output. Try the interactive controls to manipulate checkbox states programmatically.");
        infoText->SetWrap(TextWrap::WrapWord);
        infoText->SetTextColor(Color(50, 50, 50, 255));
        infoPanel->AddChild(infoText);

        mainContainer->AddChild(infoPanel);

        return mainContainer;
    }

} // namespace UltraCanvas