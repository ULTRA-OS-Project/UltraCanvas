// Apps/DemoApp/UltraCanvasSegmentedControlExamples.cpp
// Segmented Control component demonstration for main demo app
// Version: 1.0.0
// Last Modified: 2025-10-19
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasSegmentedControl.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include <sstream>
#include <iostream>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateSegmentedControlExamples() {
        auto mainContainer = std::make_shared<UltraCanvasContainer>("SegmentedControlExamples", 5000, 0, 0, 1000, 1000);

        // ===== PAGE TITLE =====
        auto title = std::make_shared<UltraCanvasLabel>("SegmentedControlTitle", 5001, 20, 10, 600, 35);
        title->SetText("UltraCanvas Segmented Control Showcase");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        mainContainer->AddChild(title);

        auto subtitle = std::make_shared<UltraCanvasLabel>("SegmentedControlSubtitle", 5002, 20, 45, 800, 25);
        subtitle->SetText("Demonstrating all segmented control styles, modes, and interaction patterns");
        subtitle->SetFontSize(12);
        subtitle->SetTextColor(Color(100, 100, 100, 255));
        mainContainer->AddChild(subtitle);

        // Status label for feedback
        auto statusLabel = std::make_shared<UltraCanvasLabel>("StatusLabel", 5003, 600, 10, 380, 60);
        statusLabel->SetText("Click any segment to see selection feedback");
        statusLabel->SetFontSize(11);
        statusLabel->SetBackgroundColor(Color(245, 245, 245, 255));
        statusLabel->SetBorders(1.0f);
        statusLabel->SetPadding(8.0f);
        mainContainer->AddChild(statusLabel);

        int yOffset = 90;

        // ========================================
        // SECTION 1: BASIC BORDERED STYLE
        // ========================================
        auto section1Label = std::make_shared<UltraCanvasLabel>("Section1", 5010, 20, yOffset, 960, 25);
        section1Label->SetText("1. Basic Bordered Style (Default)");
        section1Label->SetFontWeight(FontWeight::Bold);
        section1Label->SetTextColor(Color(200, 50, 50, 255));
        mainContainer->AddChild(section1Label);
        yOffset += 35;

        auto basicControl = SegmentedControlBuilder("basic", 5011, 50, yOffset, 410, 35)
                .AddSegment("All")
                .AddSegment("Active")
                .AddSegment("Completed")
                .AddSegment("Archived")
                .SetSelectedIndex(0)
                .OnSegmentSelected([statusLabel](int index) {
                    const char* labels[] = {"All", "Active", "Completed", "Archived"};
                    std::ostringstream oss;
                    oss << "Basic Control: Selected '" << labels[index] << "' (index " << index << ")\n"
                        << "Style: Bordered | Mode: Single Selection";
                    statusLabel->SetText(oss.str());
                })
                .Build();
        mainContainer->AddChild(basicControl);

        auto basicDesc = std::make_shared<UltraCanvasLabel>("BasicDesc", 5012, 480, yOffset, 500, 35);
        basicDesc->SetText("• Standard filter/view selector\n• Equal width segments\n• Clear visual separation");
        basicDesc->SetFontSize(10);
        basicDesc->SetTextColor(Color(100, 100, 100, 255));
        mainContainer->AddChild(basicDesc);
        yOffset += 60;

        // ========================================
        // SECTION 2: iOS STYLE
        // ========================================
        auto section2Label = std::make_shared<UltraCanvasLabel>("Section2", 5020, 20, yOffset, 960, 25);
        section2Label->SetText("2. iOS Style (Blue Border, Transparent Background)");
        section2Label->SetFontWeight(FontWeight::Bold);
        section2Label->SetTextColor(Color(200, 50, 50, 255));
        mainContainer->AddChild(section2Label);
        yOffset += 35;

        auto iOSControl = CreateSegmentedControl("ios", 5021, 50, yOffset, 350, 32);
        iOSControl->AddSegment("Map");
        iOSControl->AddSegment("Transit");
        iOSControl->AddSegment("Satellite");
        iOSControl->SetStyle(SegmentedControlStyle::Modern());
        iOSControl->SetSelectedIndex(0);
        iOSControl->onSegmentSelected = [statusLabel](int index) {
            const char* labels[] = {"Map", "Transit", "Satellite"};
            std::ostringstream oss;
            oss << "iOS Control: Selected '" << labels[index] << "' view\n"
                << "Style: Modern Blue theme";
            statusLabel->SetText(oss.str());
        };
        mainContainer->AddChild(iOSControl);

        auto iOSDesc = std::make_shared<UltraCanvasLabel>("iOSDesc", 5022, 420, yOffset, 550, 32);
        iOSDesc->SetText("• Apple Maps-style control | White selected segment\n• Transparent background with blue border");
        iOSDesc->SetFontSize(10);
        iOSDesc->SetTextColor(Color(100, 100, 100, 255));
        mainContainer->AddChild(iOSDesc);
        yOffset += 55;

        // ========================================
        // SECTION 3: FLAT STYLE
        // ========================================
        auto section3Label = std::make_shared<UltraCanvasLabel>("Section3", 5030, 20, yOffset, 960, 25);
        section3Label->SetText("3. Flat Style (No Borders, Spaced Segments)");
        section3Label->SetFontWeight(FontWeight::Bold);
        section3Label->SetTextColor(Color(200, 50, 50, 255));
        mainContainer->AddChild(section3Label);
        yOffset += 35;

        auto flatControl = CreateSegmentedControl("flat", 5031, 50, yOffset, 420, 35);
        flatControl->AddSegment("Day");
        flatControl->AddSegment("Week");
        flatControl->AddSegment("Month");
        flatControl->AddSegment("Year");
        flatControl->SetStyle(SegmentedControlStyle::Flat());
        flatControl->SetSelectedIndex(2);
        flatControl->onSegmentSelected = [statusLabel](int index) {
            const char* labels[] = {"Day", "Week", "Month", "Year"};
            std::ostringstream oss;
            oss << "Flat Control: Viewing " << labels[index] << " view\n"
                << "Style: Modern flat design with spacing";
            statusLabel->SetText(oss.str());
        };
        mainContainer->AddChild(flatControl);

        auto flatDesc = std::make_shared<UltraCanvasLabel>("FlatDesc", 5032, 490, yOffset, 480, 35);
        flatDesc->SetText("• Modern minimal design\n• Individual rounded segments\n• 4px spacing between segments");
        flatDesc->SetFontSize(10);
        flatDesc->SetTextColor(Color(100, 100, 100, 255));
        mainContainer->AddChild(flatDesc);
        yOffset += 60;

        // ========================================
        // SECTION 4: BAR STYLE
        // ========================================
        auto section4Label = std::make_shared<UltraCanvasLabel>("Section4", 5040, 20, yOffset, 960, 25);
        section4Label->SetText("4. Bar Style (Background Bar with Highlight)");
        section4Label->SetFontWeight(FontWeight::Bold);
        section4Label->SetTextColor(Color(200, 50, 50, 255));
        mainContainer->AddChild(section4Label);
        yOffset += 35;

        auto barControl = CreateSegmentedControl("bar", 5041, 50, yOffset, 360, 36);
        barControl->AddSegment("Small");
        barControl->AddSegment("Medium");
        barControl->AddSegment("Large");
        barControl->AddSegment("X-Large");

        // Custom appearance for bar style
        SegmentedControlStyle barStyle = SegmentedControlStyle::Bar();
        barStyle.normalColor = Color(230, 230, 230, 255);
        barStyle.selectedColor = Color(0, 120, 215, 255);
        barStyle.normalTextColor = Color(80, 80, 80, 255);
        barStyle.selectedTextColor = Colors::White;
        barStyle.cornerRadius = 8.0f;
        barStyle.separatorWidth = 0;
        barControl->SetStyle(barStyle);

        barControl->SetSelectedIndex(1);
        barControl->onSegmentSelected = [statusLabel](int index) {
            const char* labels[] = {"Small", "Medium", "Large", "X-Large"};
            std::ostringstream oss;
            oss << "Bar Control: Size set to " << labels[index] << "\n"
                << "Style: Bar with highlighted selection";
            statusLabel->SetText(oss.str());
        };
        mainContainer->AddChild(barControl);

        auto barDesc = std::make_shared<UltraCanvasLabel>("BarDesc", 5042, 430, yOffset, 540, 36);
        barDesc->SetText("• Size selector with background bar\n• Blue highlight for selected segment\n• Custom colors (gray bg, blue selected)");
        barDesc->SetFontSize(10);
        barDesc->SetTextColor(Color(100, 100, 100, 255));
        mainContainer->AddChild(barDesc);
        yOffset += 60;

        // ========================================
        // SECTION 5: TEXT FORMATTING (TOGGLE MODE)
        // ========================================
        auto section5Label = std::make_shared<UltraCanvasLabel>("Section5", 5050, 20, yOffset, 960, 25);
        section5Label->SetText("5. Text Formatting (Allow No Selection - Toggle Mode)");
        section5Label->SetFontWeight(FontWeight::Bold);
        section5Label->SetTextColor(Color(200, 50, 50, 255));
        mainContainer->AddChild(section5Label);
        yOffset += 35;

        auto textStyleControl = CreateSegmentedControl("textStyle", 5051, 50, yOffset, 190, 32);
        textStyleControl->AddSegment("<b>B</b>");   // Bold
        textStyleControl->AddSegment("<i>I</i>");   // Italic
        textStyleControl->AddSegment("<u>U</u>");   // Underline
        textStyleControl->AddSegment("<span strikethrough=\"true\">S</span>");   // Strikethrough
        textStyleControl->SetAllowNoSelection(true);  // Allow toggling off
        textStyleControl->SetWidthMode(SegmentWidthMode::Equal);
        textStyleControl->SetSelectionMode(SegmentSelectionMode::Toggle);
        textStyleControl->onSegmentClick = [statusLabel, textStyleControl](int index) {
                const char* labels[] = {"Bold", "Italic", "Underline", "Strikethrough", "Superscript", "Subscript"};
                std::ostringstream oss;
                if (textStyleControl->IsSegmentSelected(index)) {
                        oss << "Text Style: " << labels[index] << " ENABLED\n";
                } else {
                        oss << "Text Style: " << labels[index] << " DISABLED\n";
                }
                oss << "Mode: Toggle (click again to deselect)";
                statusLabel->SetText(oss.str());
        };

        auto textStyleControl2 = CreateSegmentedControl("textStyle", 5051, 250, yOffset, 80, 32);
        textStyleControl2->SetAllowNoSelection(true);  // Allow toggling off
        textStyleControl2->SetWidthMode(SegmentWidthMode::Equal);
        textStyleControl2->SetSelectionMode(SegmentSelectionMode::Single);
        textStyleControl2->AddSegment("X<sup>2</sup>");   // X^2
        textStyleControl2->AddSegment("X<sub>2</sub>");   // X_2
//        textStyleControl->SetStyle(SegmentedControlStyle::Bordered);
        textStyleControl2->onSegmentClick = [statusLabel, textStyleControl2](int index) {
                const char* labels[] = {"Superscript", "Subscript"};
                std::ostringstream oss;
                if (textStyleControl2->IsSegmentSelected(index)) {
                        oss << "Text Style: " << labels[index] << " ENABLED\n";
                } else {
                        oss << "Text Style: " << labels[index] << " DISABLED\n";
                }
                oss << "Mode: Toggle (click again to deselect)";
                statusLabel->SetText(oss.str());
        };

        mainContainer->AddChild(textStyleControl);
        mainContainer->AddChild(textStyleControl2);

        auto textStyleDesc = std::make_shared<UltraCanvasLabel>("TextStyleDesc", 5052, 340, yOffset, 660, 32);
        textStyleDesc->SetText("• Text editor formatting toolbar\n• Click to enable, click again to disable\n• AllowNoSelection = true");
        textStyleDesc->SetFontSize(10);
        textStyleDesc->SetTextColor(Color(100, 100, 100, 255));
        mainContainer->AddChild(textStyleDesc);
        yOffset += 55;

        // ========================================
        // SECTION 6: ALIGNMENT (FIT CONTENT MODE)
        // ========================================
        auto section6Label = std::make_shared<UltraCanvasLabel>("Section6", 5060, 20, yOffset, 960, 25);
        section6Label->SetText("6. Text Alignment (FitContent Width Mode)");
        section6Label->SetFontWeight(FontWeight::Bold);
        section6Label->SetTextColor(Color(200, 50, 50, 255));
        mainContainer->AddChild(section6Label);
        yOffset += 35;

        auto alignmentControl = CreateSegmentedControl("alignment", 5061, 50, yOffset, 400, 34);
        alignmentControl->AddSegment("Left", TextAlignment::Left);
        alignmentControl->AddSegment("Center", TextAlignment::Center);
        alignmentControl->AddSegment("Right", TextAlignment::Right);
//        alignmentControl->SetStyle(SegmentedControlStyle::Bordered);
        alignmentControl->SetWidthMode(SegmentWidthMode::FitContent);

        // Custom colors
        SegmentedControlStyle alignAppearance;
        alignAppearance.selectedColor = Color(52, 152, 219, 255);
        alignAppearance.hoverColor = Color(52, 152, 219, 64);
        alignAppearance.cornerRadius = 6.0f;
        alignmentControl->SetStyle(alignAppearance);

        alignmentControl->SetSelectedIndex(0);
        alignmentControl->onSegmentSelected = [statusLabel](int index) {
            const char* labels[] = {"Left", "Center", "Right", "Justify"};
            std::ostringstream oss;
            oss << "Alignment: Text aligned " << labels[index] << "\n"
                << "Width Mode: FitContent (auto-sized segments)";
            statusLabel->SetText(oss.str());
        };
        mainContainer->AddChild(alignmentControl);

        auto alignDesc = std::make_shared<UltraCanvasLabel>("AlignDesc", 5062, 470, yOffset, 500, 34);
        alignDesc->SetText("• Auto-sized segments based on text width\n• Custom blue theme\n• Perfect for toolbar alignment controls");
        alignDesc->SetFontSize(10);
        alignDesc->SetTextColor(Color(100, 100, 100, 255));
        mainContainer->AddChild(alignDesc);
        yOffset += 60;

        // ========================================
        // SECTION 7: DISABLED SEGMENTS
        // ========================================
        auto section7Label = std::make_shared<UltraCanvasLabel>("Section7", 5070, 20, yOffset, 960, 25);
        section7Label->SetText("7. Disabled Segments Demonstration");
        section7Label->SetFontWeight(FontWeight::Bold);
        section7Label->SetTextColor(Color(200, 50, 50, 255));
        mainContainer->AddChild(section7Label);
        yOffset += 35;

        auto disabledControl = CreateSegmentedControl("disabled", 5071, 50, yOffset, 600, 35);
        disabledControl->AddSegment("Enabled 1");
        disabledControl->AddSegment("Disabled");
        disabledControl->AddSegment("Enabled 2");
        disabledControl->AddSegment("Also Disabled");
        disabledControl->AddSegment("Enabled 3");
        disabledControl->SetSegmentEnabled(1, false);  // Disable 2nd segment
        disabledControl->SetSegmentEnabled(3, false);  // Disable 4th segment
//        disabledControl->SetStyle(SegmentedControlStyle::Bordered);
        disabledControl->SetSelectedIndex(0);
        disabledControl->onSegmentSelected = [statusLabel](int index) {
            std::ostringstream oss;
            oss << "Disabled Demo: Selected segment " << index << "\n"
                << "Segments 1 and 3 are disabled (cannot be selected)";
            statusLabel->SetText(oss.str());
        };
        mainContainer->AddChild(disabledControl);

        auto disabledDesc = std::make_shared<UltraCanvasLabel>("DisabledDesc", 5072, 660, yOffset, 450, 35);
        disabledDesc->SetText("• Individual segments can be disabled\n• Disabled segments: grayed out, not clickable\n• Keyboard navigation skips disabled segments");
        disabledDesc->SetFontSize(10);
        disabledDesc->SetTextColor(Color(100, 100, 100, 255));
        mainContainer->AddChild(disabledDesc);
        yOffset += 60;

        // ========================================
        // SECTION 8: CUSTOM WIDTH SEGMENTS
        // ========================================
        auto section8Label = std::make_shared<UltraCanvasLabel>("Section8", 5080, 20, yOffset, 960, 25);
        section8Label->SetText("8. Custom Width Segments");
        section8Label->SetFontWeight(FontWeight::Bold);
        section8Label->SetTextColor(Color(200, 50, 50, 255));
        mainContainer->AddChild(section8Label);
        yOffset += 35;

        auto customWidthControl = CreateSegmentedControl("customWidth", 5081, 50, yOffset, 500, 36);
        customWidthControl->AddSegment("Short");
        customWidthControl->AddSegment("Medium Length");
        customWidthControl->AddSegment("Very Long Segment Name");
        customWidthControl->SetWidthMode(SegmentWidthMode::FitContent);

        // Custom green theme
        SegmentedControlStyle greenAppearance = SegmentedControlStyle::Flat();
        greenAppearance.selectedColor = Color(46, 204, 113, 255);
        greenAppearance.hoverColor = Color(46, 204, 113, 64);
        customWidthControl->SetStyle(greenAppearance);

        customWidthControl->SetSelectedIndex(1);
        customWidthControl->onSegmentSelected = [statusLabel](int index) {
            std::ostringstream oss;
            oss << "Custom Width: Selected segment " << index << "\n"
                << "Width Mode: FitContent adapts to text length";
            statusLabel->SetText(oss.str());
        };
        mainContainer->AddChild(customWidthControl);

        auto customWidthDesc = std::make_shared<UltraCanvasLabel>("CustomWidthDesc", 5082, 570, yOffset, 400, 36);
        customWidthDesc->SetText("• Segments auto-size to text length\n• Flat style with green theme\n• Perfect for variable-length options");
        customWidthDesc->SetFontSize(10);
        customWidthDesc->SetTextColor(Color(100, 100, 100, 255));
        mainContainer->AddChild(customWidthDesc);
        yOffset += 65;

        // ========================================
        // KEYBOARD NAVIGATION INFO
        // ========================================
        auto keyboardLabel = std::make_shared<UltraCanvasLabel>("KeyboardNav", 5090, 20, yOffset, 960, 25);
        keyboardLabel->SetText("⌨️ Keyboard Navigation");
        keyboardLabel->SetFontWeight(FontWeight::Bold);
        keyboardLabel->SetTextColor(Color(50, 100, 200, 255));
        mainContainer->AddChild(keyboardLabel);
        yOffset += 30;

        auto keyboardDesc = std::make_shared<UltraCanvasLabel>("KeyboardDesc", 5091, 50, yOffset, 900, 70);
        keyboardDesc->SetText(
                "• Left/Right Arrow Keys: Navigate between segments\n"
                "• Up/Down Arrow Keys: Also navigate (alternative)\n"
                "• Home Key: Jump to first enabled segment\n"
                "• End Key: Jump to last enabled segment\n"
                "• Focus any control and use keyboard to select"
        );
        keyboardDesc->SetFontSize(11);
        keyboardDesc->SetTextColor(Color(80, 80, 80, 255));
        keyboardDesc->SetBackgroundColor(Color(240, 245, 255, 255));
        keyboardDesc->SetBorders(1.0f, Color(200, 210, 230, 255));
        keyboardDesc->SetPadding(8.0f);
        mainContainer->AddChild(keyboardDesc);

        return mainContainer;
    }

} // namespace UltraCanvas
