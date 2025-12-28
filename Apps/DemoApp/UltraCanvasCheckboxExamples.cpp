// UltraCanvasCheckboxExamples.cpp
// Interactive checkbox component demonstration
// Version: 1.0.0
// Last Modified: 2025-01-12
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasButton.h"
#include <iostream>
#include <vector>

namespace UltraCanvas {

// Helper function to create a separator line
    std::shared_ptr<UltraCanvasContainer> CreateSeparatorLine(long id, long x, long y, long width) {
        auto separator = std::make_shared<UltraCanvasContainer>("Separator" + std::to_string(id), id, x, y, width, 2);
        separator->SetBackgroundColor(Color(200, 200, 200, 255));
        return separator;
    }

// Helper function to create a section title
    std::shared_ptr<UltraCanvasLabel> CreateSectionTitle(long id, long x, long y, const std::string& text) {
        auto title = std::make_shared<UltraCanvasLabel>("SectionTitle" + std::to_string(id), id, x, y, 600, 25);
        title->SetText(text);
        title->SetFontSize(14);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        return title;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateCheckboxExamples() {
        // Main container for all checkbox examples
        auto mainContainer = std::make_shared<UltraCanvasContainer>("CheckboxMainContainer", 3000, 0, 0, 1020, 1300);
        mainContainer->SetBackgroundColor(Colors::White);
        mainContainer->SetPadding(0,0,10,0);

        // Title
        auto mainTitle = std::make_shared<UltraCanvasLabel>("CheckboxMainTitle", 3001, 20, 10, 900, 30);
        mainTitle->SetText("UltraCanvas Checkbox Component Examples");
        mainTitle->SetFontSize(18);
        mainTitle->SetFontWeight(FontWeight::Bold);
        mainContainer->AddChild(mainTitle);

        // Description
        auto description = std::make_shared<UltraCanvasLabel>("CheckboxDescription", 3002, 20, 45, 960, 40);
        description->SetText("Demonstrates various checkbox styles, states, and configurations");
        description->SetWordWrap(true);
        description->SetTextColor(Color(80, 80, 80, 255));
        mainContainer->AddChild(description);

        long currentY = 100;

        // ===== SECTION 1: Basic Checkboxes =====
        mainContainer->AddChild(CreateSectionTitle(3010, 20, currentY, "Basic Checkboxes"));
        currentY += 35;

        // Standard checkbox - unchecked
        auto basicCheckbox1 = std::make_shared<UltraCanvasCheckbox>("BasicCheckbox1", 3011, 30, currentY, 200, 24, "Standard Checkbox");
        basicCheckbox1->SetChecked(false);
        basicCheckbox1->onChecked = []() {
            std::cout << "Basic checkbox checked!" << std::endl;
        };
        basicCheckbox1->onUnchecked = []() {
            std::cout << "Basic checkbox unchecked!" << std::endl;
        };
        mainContainer->AddChild(basicCheckbox1);

        // Standard checkbox - checked
        auto basicCheckbox2 = std::make_shared<UltraCanvasCheckbox>("BasicCheckbox2", 3012, 250, currentY, 200, 24, "Pre-checked Box");
        basicCheckbox2->SetChecked(true);
        mainContainer->AddChild(basicCheckbox2);

        // Disabled checkbox - unchecked
        auto disabledCheckbox1 = std::make_shared<UltraCanvasCheckbox>("DisabledCheckbox1", 3013, 470, currentY, 200, 24, "Disabled Unchecked");
        disabledCheckbox1->SetChecked(false);
        disabledCheckbox1->SetDisabled(true);
        mainContainer->AddChild(disabledCheckbox1);

        // Disabled checkbox - checked
        auto disabledCheckbox2 = std::make_shared<UltraCanvasCheckbox>("DisabledCheckbox2", 3014, 690, currentY, 200, 24, "Disabled Checked");
        disabledCheckbox2->SetChecked(true);
        disabledCheckbox2->SetDisabled(true);
        mainContainer->AddChild(disabledCheckbox2);

        currentY += 40;
        mainContainer->AddChild(CreateSeparatorLine(3015, 20, currentY, 960));
        currentY += 20;

        // ===== SECTION 2: Tri-State Checkboxes =====
        mainContainer->AddChild(CreateSectionTitle(3020, 20, currentY, "Tri-State Checkboxes (Indeterminate)"));
        currentY += 35;

        // Tri-state checkbox
        auto triStateCheckbox = std::make_shared<UltraCanvasCheckbox>("TriStateCheckbox", 3021, 30, currentY, 250, 24, "Select All Items");
        triStateCheckbox->SetAllowIndeterminate(true);
        triStateCheckbox->SetCheckState(CheckboxState::Indeterminate);

        // Sub-items for tri-state demonstration
        auto subItem1 = std::make_shared<UltraCanvasCheckbox>("SubItem1", 3022, 60, currentY + 30, 200, 24, "Item 1");
        auto subItem2 = std::make_shared<UltraCanvasCheckbox>("SubItem2", 3023, 60, currentY + 55, 200, 24, "Item 2");
        auto subItem3 = std::make_shared<UltraCanvasCheckbox>("SubItem3", 3024, 60, currentY + 80, 200, 24, "Item 3");

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
                triStateCheckbox->SetCheckState(CheckboxState::Unchecked);
            } else if (checkedCount == 3) {
                triStateCheckbox->SetCheckState(CheckboxState::Checked);
            } else {
                triStateCheckbox->SetCheckState(CheckboxState::Indeterminate);
            }
        };

        // Set callbacks for sub-items
        subItem1->onStateChanged = [updateParentState](CheckboxState, CheckboxState) { updateParentState(); };
        subItem2->onStateChanged = [updateParentState](CheckboxState, CheckboxState) { updateParentState(); };
        subItem3->onStateChanged = [updateParentState](CheckboxState, CheckboxState) { updateParentState(); };

        // Parent checkbox callback
        triStateCheckbox->onStateChanged = [subItem1, subItem2, subItem3](CheckboxState oldState, CheckboxState newState) {
            if (newState == CheckboxState::Checked) {
                subItem1->SetChecked(true);
                subItem2->SetChecked(true);
                subItem3->SetChecked(true);
            } else if (newState == CheckboxState::Unchecked) {
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
        auto triStateStatus = std::make_shared<UltraCanvasLabel>("TriStateStatus", 3025, 300, currentY + 40, 300, 24);
        triStateStatus->SetText("State: Indeterminate (2 of 3 selected)");
        triStateStatus->SetTextColor(Color(100, 100, 100, 255));
        mainContainer->AddChild(triStateStatus);

        // Update status label
        auto updateStatusLabel = [triStateStatus, triStateCheckbox]() {
            switch(triStateCheckbox->GetCheckState()) {
                case CheckboxState::Unchecked:
                    triStateStatus->SetText("State: Unchecked (0 selected)");
                    break;
                case CheckboxState::Checked:
                    triStateStatus->SetText("State: Checked (all selected)");
                    break;
                case CheckboxState::Indeterminate:
                    triStateStatus->SetText("State: Indeterminate (partially selected)");
                    break;
            }
        };

        triStateCheckbox->onStateChanged = [subItem1, subItem2, subItem3, updateStatusLabel](CheckboxState oldState, CheckboxState newState) {
            if (newState == CheckboxState::Checked) {
                subItem1->SetChecked(true);
                subItem2->SetChecked(true);
                subItem3->SetChecked(true);
            } else if (newState == CheckboxState::Unchecked) {
                subItem1->SetChecked(false);
                subItem2->SetChecked(false);
                subItem3->SetChecked(false);
            }
            updateStatusLabel();
        };

        currentY += 120;
        mainContainer->AddChild(CreateSeparatorLine(3026, 20, currentY, 960));
        currentY += 20;

        // ===== SECTION 3: Switch Style Checkboxes =====
        mainContainer->AddChild(CreateSectionTitle(3030, 20, currentY, "Switch Style Toggles"));
        currentY += 35;

        // Create switches
        auto switch1 = UltraCanvasCheckbox::CreateSwitch("Switch1", 3031, 30, currentY, "Enable Notifications", true);
        auto switch2 = UltraCanvasCheckbox::CreateSwitch("Switch2", 3032, 30, currentY + 35, "Dark Mode", false);
        auto switch3 = UltraCanvasCheckbox::CreateSwitch("Switch3", 3033, 30, currentY + 70, "Auto-Save", true);

        // Switch status labels
        auto switchStatus1 = std::make_shared<UltraCanvasLabel>("SwitchStatus1", 3034, 250, currentY, 100, 24);
        switchStatus1->SetText("ON");
        switchStatus1->SetTextColor(Color(0, 150, 0, 255));

        auto switchStatus2 = std::make_shared<UltraCanvasLabel>("SwitchStatus2", 3035, 250, currentY + 35, 100, 24);
        switchStatus2->SetText("OFF");
        switchStatus2->SetTextColor(Color(150, 0, 0, 255));

        auto switchStatus3 = std::make_shared<UltraCanvasLabel>("SwitchStatus3", 3036, 250, currentY + 70, 100, 24);
        switchStatus3->SetText("ON");
        switchStatus3->SetTextColor(Color(0, 150, 0, 255));

        // Switch callbacks
        switch1->onStateChanged = [switchStatus1](CheckboxState oldState, CheckboxState newState) {
            if (newState == CheckboxState::Checked) {
                switchStatus1->SetText("ON");
                switchStatus1->SetTextColor(Color(0, 150, 0, 255));
                std::cout << "Notifications enabled" << std::endl;
            } else {
                switchStatus1->SetText("OFF");
                switchStatus1->SetTextColor(Color(150, 0, 0, 255));
                std::cout << "Notifications disabled" << std::endl;
            }
        };

        switch2->onStateChanged = [switchStatus2](CheckboxState oldState, CheckboxState newState) {
            if (newState == CheckboxState::Checked) {
                switchStatus2->SetText("ON");
                switchStatus2->SetTextColor(Color(0, 150, 0, 255));
                std::cout << "Dark mode enabled" << std::endl;
            } else {
                switchStatus2->SetText("OFF");
                switchStatus2->SetTextColor(Color(150, 0, 0, 255));
                std::cout << "Dark mode disabled" << std::endl;
            }
        };

        switch3->onStateChanged = [switchStatus3](CheckboxState oldState, CheckboxState newState) {
            if (newState == CheckboxState::Checked) {
                switchStatus3->SetText("ON");
                switchStatus3->SetTextColor(Color(0, 150, 0, 255));
                std::cout << "Auto-save enabled" << std::endl;
            } else {
                switchStatus3->SetText("OFF");
                switchStatus3->SetTextColor(Color(150, 0, 0, 255));
                std::cout << "Auto-save disabled" << std::endl;
            }
        };

        mainContainer->AddChild(switch1);
        mainContainer->AddChild(switch2);
        mainContainer->AddChild(switch3);
        mainContainer->AddChild(switchStatus1);
        mainContainer->AddChild(switchStatus2);
        mainContainer->AddChild(switchStatus3);

        currentY += 115;
        mainContainer->AddChild(CreateSeparatorLine(3037, 20, currentY, 960));
        currentY += 20;

        // ===== SECTION 4: Radio Buttons =====
        mainContainer->AddChild(CreateSectionTitle(3040, 20, currentY, "Radio Button Groups"));
        currentY += 35;

        // Create radio button group for themes
        auto radioGroup1 = std::make_shared<UltraCanvasRadioGroup>();

        auto radioTheme1 = UltraCanvasCheckbox::CreateRadioButton("RadioTheme1", 3041, 30, currentY, "Light Theme", true);
        auto radioTheme2 = UltraCanvasCheckbox::CreateRadioButton("RadioTheme2", 3042, 30, currentY + 30, "Dark Theme", false);
        auto radioTheme3 = UltraCanvasCheckbox::CreateRadioButton("RadioTheme3", 3043, 30, currentY + 60, "Auto Theme", false);

        radioGroup1->AddRadioButton(radioTheme1);
        radioGroup1->AddRadioButton(radioTheme2);
        radioGroup1->AddRadioButton(radioTheme3);

        // Create radio button group for quality
        auto radioGroup2 = std::make_shared<UltraCanvasRadioGroup>();

        auto radioQuality1 = UltraCanvasCheckbox::CreateRadioButton("RadioQuality1", 3044, 250, currentY, "Low Quality", false);
        auto radioQuality2 = UltraCanvasCheckbox::CreateRadioButton("RadioQuality2", 3045, 250, currentY + 30, "Medium Quality", true);
        auto radioQuality3 = UltraCanvasCheckbox::CreateRadioButton("RadioQuality3", 3046, 250, currentY + 60, "High Quality", false);

        radioGroup2->AddRadioButton(radioQuality1);
        radioGroup2->AddRadioButton(radioQuality2);
        radioGroup2->AddRadioButton(radioQuality3);

        // Selected option display
        auto selectedTheme = std::make_shared<UltraCanvasLabel>("SelectedTheme", 3047, 470, currentY + 20, 300, 24);
        selectedTheme->SetText("Selected Theme: Light");
        selectedTheme->SetTextColor(Color(0, 100, 200, 255));

        auto selectedQuality = std::make_shared<UltraCanvasLabel>("SelectedQuality", 3048, 470, currentY + 50, 300, 24);
        selectedQuality->SetText("Selected Quality: Medium");
        selectedQuality->SetTextColor(Color(0, 100, 200, 255));

        // Radio group callbacks
        radioGroup1->onSelectionChanged = [selectedTheme](std::shared_ptr<UltraCanvasCheckbox> selected) {
            if (selected) {
                std::string themeName = selected->GetText();
                selectedTheme->SetText("Selected Theme: " + themeName.substr(0, themeName.find(" ")));
                std::cout << "Theme changed to: " << selected->GetText() << std::endl;
            }
        };

        radioGroup2->onSelectionChanged = [selectedQuality](std::shared_ptr<UltraCanvasCheckbox> selected) {
            if (selected) {
                std::string qualityName = selected->GetText();
                selectedQuality->SetText("Selected Quality: " + qualityName.substr(0, qualityName.find(" ")));
                std::cout << "Quality changed to: " << selected->GetText() << std::endl;
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
        mainContainer->AddChild(CreateSeparatorLine(3049, 20, currentY, 960));
        currentY += 20;

        // ===== SECTION 5: Styled Checkboxes =====
        mainContainer->AddChild(CreateSectionTitle(3050, 20, currentY, "Custom Styled Checkboxes"));
        currentY += 35;

        // Material style checkbox
        auto materialCheckbox = std::make_shared<UltraCanvasCheckbox>("MaterialCheckbox", 3051, 30, currentY, 200, 30, "Material Design");
        materialCheckbox->SetStyle(CheckboxStyle::Material);
        materialCheckbox->GetVisualStyle().boxColor = Color(33, 150, 243, 255);  // Material Blue
        materialCheckbox->GetVisualStyle().checkmarkColor = Color(255, 255, 255, 255);
        materialCheckbox->GetVisualStyle().boxSize = 20.0f;

        // Rounded style checkbox
        auto roundedCheckbox = std::make_shared<UltraCanvasCheckbox>("RoundedCheckbox", 3052, 250, currentY, 200, 30, "Rounded Corners");
        roundedCheckbox->SetStyle(CheckboxStyle::Rounded);
        roundedCheckbox->GetVisualStyle().cornerRadius = 5.0f;
        roundedCheckbox->GetVisualStyle().boxColor = Color(100, 200, 100, 255);

        // Custom colored checkbox
        auto customColorCheckbox = std::make_shared<UltraCanvasCheckbox>("CustomColorCheckbox", 3053, 470, currentY, 200, 30, "Custom Colors");
        customColorCheckbox->SetColors(
                Color(255, 100, 100, 255),  // Red box
                Color(255, 255, 0, 255),     // Yellow checkmark
                Color(100, 100, 255, 255)    // Blue text
        );

        // Large checkbox
        auto largeCheckbox = std::make_shared<UltraCanvasCheckbox>("LargeCheckbox", 3054, 690, currentY, 250, 40, "Large Size");
        largeCheckbox->SetBoxSize(28.0f);
        largeCheckbox->SetFont("Arial", 16.0f, FontWeight::Bold);

        mainContainer->AddChild(materialCheckbox);
        mainContainer->AddChild(roundedCheckbox);
        mainContainer->AddChild(customColorCheckbox);
        mainContainer->AddChild(largeCheckbox);

        currentY += 50;
        mainContainer->AddChild(CreateSeparatorLine(3055, 20, currentY, 960));
        currentY += 20;

        // ===== SECTION 6: Interactive Demo =====
        mainContainer->AddChild(CreateSectionTitle(3060, 20, currentY, "Interactive Feature Demo"));
        currentY += 35;

        // Feature checkboxes
        auto featureContainer = std::make_shared<UltraCanvasContainer>("FeatureContainer", 3061, 30, currentY, 400, 150);
        featureContainer->SetBackgroundColor(Color(230, 240, 250, 255));
        featureContainer->SetBorders(1, Color(180, 180, 180, 255));

        auto featureTitle = std::make_shared<UltraCanvasLabel>("FeatureTitle", 3062, 10, 10, 200, 20);
        featureTitle->SetText("Enable Features:");
        featureTitle->SetFontWeight(FontWeight::Bold);
        featureContainer->AddChild(featureTitle);

        auto feature1 = std::make_shared<UltraCanvasCheckbox>("Feature1", 3063, 20, 35, 180, 24, "Spell Check");
        auto feature2 = std::make_shared<UltraCanvasCheckbox>("Feature2", 3064, 20, 60, 180, 24, "Auto-Complete");
        auto feature3 = std::make_shared<UltraCanvasCheckbox>("Feature3", 3065, 20, 85, 180, 24, "Syntax Highlighting");
        auto feature4 = std::make_shared<UltraCanvasCheckbox>("Feature4", 3066, 20, 110, 180, 24, "Line Numbers");

        auto feature5 = std::make_shared<UltraCanvasCheckbox>("Feature5", 3067, 210, 35, 180, 24, "Word Wrap");
        auto feature6 = std::make_shared<UltraCanvasCheckbox>("Feature6", 3068, 210, 60, 180, 24, "Auto-Indent");
        auto feature7 = std::make_shared<UltraCanvasCheckbox>("Feature7", 3069, 210, 85, 180, 24, "Show Whitespace");
        auto feature8 = std::make_shared<UltraCanvasCheckbox>("Feature8", 3070, 210, 110, 180, 24, "Code Folding");

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
        auto selectAllBtn = std::make_shared<UltraCanvasButton>("SelectAllBtn", 3071, 450, currentY, 120, 30);
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
            std::cout << "All features selected" << std::endl;
        });

        auto clearAllBtn = std::make_shared<UltraCanvasButton>("ClearAllBtn", 3072, 580, currentY, 120, 30);
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
            std::cout << "All features cleared" << std::endl;
        });

        auto toggleAllBtn = std::make_shared<UltraCanvasButton>("ToggleAllBtn", 3073, 710, currentY, 120, 30);
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
            std::cout << "All features toggled" << std::endl;
        });

        // Feature status display
        auto featureStatus = std::make_shared<UltraCanvasLabel>("FeatureStatus", 3074, 450, currentY + 45, 380, 60);
        featureStatus->SetText("Selected Features:\nNone");
        featureStatus->SetWordWrap(true);
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
        feature1->onStateChanged = [updateFeatureStatus](CheckboxState, CheckboxState) { updateFeatureStatus(); };
        feature2->onStateChanged = [updateFeatureStatus](CheckboxState, CheckboxState) { updateFeatureStatus(); };
        feature3->onStateChanged = [updateFeatureStatus](CheckboxState, CheckboxState) { updateFeatureStatus(); };
        feature4->onStateChanged = [updateFeatureStatus](CheckboxState, CheckboxState) { updateFeatureStatus(); };
        feature5->onStateChanged = [updateFeatureStatus](CheckboxState, CheckboxState) { updateFeatureStatus(); };
        feature6->onStateChanged = [updateFeatureStatus](CheckboxState, CheckboxState) { updateFeatureStatus(); };
        feature7->onStateChanged = [updateFeatureStatus](CheckboxState, CheckboxState) { updateFeatureStatus(); };
        feature8->onStateChanged = [updateFeatureStatus](CheckboxState, CheckboxState) { updateFeatureStatus(); };

        mainContainer->AddChild(selectAllBtn);
        mainContainer->AddChild(clearAllBtn);
        mainContainer->AddChild(toggleAllBtn);
        mainContainer->AddChild(featureStatus);

        // Info panel at bottom
        currentY += 170;
        auto infoPanel = std::make_shared<UltraCanvasContainer>("InfoPanel", 3075, 20, currentY, 960, 60);
        infoPanel->SetBackgroundColor(Color(240, 248, 255, 255));
        infoPanel->SetBorders(1.0f);
        //infoPanel->SetBorderColor(Color(100, 150, 200, 255));

        auto infoText = std::make_shared<UltraCanvasLabel>("InfoText", 3076, 10, 10, 940, 40);
        infoText->SetText("ℹ️ This demo showcases the UltraCanvasCheckbox component with various styles and configurations. "
                          "Click checkboxes to see console output. Try the interactive controls to manipulate checkbox states programmatically.");
        infoText->SetWordWrap(true);
        infoText->SetTextColor(Color(50, 50, 50, 255));
        infoPanel->AddChild(infoText);

        mainContainer->AddChild(infoPanel);

        return mainContainer;
    }

} // namespace UltraCanvas