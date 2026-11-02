// Apps/DemoApp/UltraCanvasSegmentedControlExamples.cpp
// Segmented Control component demonstration for main demo app
// Version: 1.1.1 - the disabled-segments control is wide enough for its captions
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): one card per
//                  example, each control beside its notes, the feedback and the
//                  notes sized to their text
// Version: 1.0.0
// Last Modified: 2026-10-24
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasSegmentedControl.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include <sstream>
#include <iostream>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateSegmentedControlExamples() {
        auto page = DemoKit::CreatePage("SegmentedControlExamples", "UltraCanvas Segmented Control Showcase",
                "Demonstrating all segmented control styles, modes, and interaction patterns");

        // Status label for feedback
        auto statusCard = DemoKit::AddCard(page, "StatusCard", "Selection feedback");
        auto statusLabel = DemoKit::AddText(statusCard, "StatusLabel", "Click any segment to see selection feedback");
        statusLabel->SetFontSize(11);
        statusLabel->SetBackgroundColor(Color(245, 245, 245, 255));
        statusLabel->SetBorders(1.0f);
        statusLabel->SetPadding(8.0f);

        // One card per example: the control, and beside it (or under it when
        // the card is narrow) its grey notes.
        auto addSection = [&page](const std::string& id, const std::string& title) {
            auto card = DemoKit::AddCard(page, id, title);
            return DemoKit::AddRow(card, id + "Row", 20);
        };
        auto addDesc = [](const std::shared_ptr<UltraCanvasContainer>& row, const std::string& id,
                          const std::string& text) {
            auto desc = DemoKit::CreateCaption(id, text);
            DemoKit::Add(row, desc, 1);
        };

        // ========================================
        // SECTION 1: BASIC BORDERED STYLE
        // ========================================
        auto row1 = addSection("Section1", "1. Basic Bordered Style (Default)");

        auto basicControl = SegmentedControlBuilder("basic", 0, 0, 420, 35)
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
        DemoKit::AddFixed(row1, basicControl);

        addDesc(row1, "BasicDesc", "• Standard filter/view selector\n• Equal width segments\n• Clear visual separation");

        // ========================================
        // SECTION 2: iOS STYLE
        // ========================================
        auto row2 = addSection("Section2", "2. iOS Style (Blue Border, Transparent Background)");

        auto iOSControl = CreateSegmentedControl("ios", 0, 0, 350, 32);
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
        DemoKit::AddFixed(row2, iOSControl);

        addDesc(row2, "iOSDesc", "• Apple Maps-style control | White selected segment\n• Transparent background with blue border");

        // ========================================
        // SECTION 3: FLAT STYLE
        // ========================================
        auto row3 = addSection("Section3", "3. Flat Style (No Borders, Spaced Segments)");

        auto flatControl = CreateSegmentedControl("flat", 0, 0, 420, 35);
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
        DemoKit::AddFixed(row3, flatControl);

        addDesc(row3, "FlatDesc", "• Modern minimal design\n• Individual rounded segments\n• 4px spacing between segments");

        // ========================================
        // SECTION 4: BAR STYLE
        // ========================================
        auto row4 = addSection("Section4", "4. Bar Style (Background Bar with Highlight)");

        auto barControl = CreateSegmentedControl("bar", 0, 0, 360, 36);
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
        DemoKit::AddFixed(row4, barControl);

        addDesc(row4, "BarDesc", "• Size selector with background bar\n• Blue highlight for selected segment\n• Custom colors (gray bg, blue selected)");

        // ========================================
        // SECTION 5: TEXT FORMATTING (TOGGLE MODE)
        // ========================================
        auto row5 = addSection("Section5", "5. Text Formatting (Allow No Selection - Toggle Mode)");

        auto textStyleControl = CreateSegmentedControl("textStyle", 0, 0, 200, 32);
        textStyleControl->AddSegment("<b>B</b>");   // Bold
        textStyleControl->AddSegment("<i>I</i>");   // Italic
        textStyleControl->AddSegment("<u>U</u>");   // Underline
        textStyleControl->AddSegment("<span strikethrough=\"true\">S</span>");   // Strikethrough
        textStyleControl->SetAllowNoSelection(true);  // Allow toggling off
        textStyleControl->SetWidthMode(SegmentWidthMode::Equal);
        textStyleControl->SetSelectionMode(SegmentSelectionMode::Toggle);
        textStyleControl->onSegmentClick = [statusLabel, textStyleControl = textStyleControl.get()](int index) {
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

        auto textStyleControl2 = CreateSegmentedControl("textStyle", 0, 0, 80, 32);
        textStyleControl2->SetAllowNoSelection(true);  // Allow toggling off
        textStyleControl2->SetWidthMode(SegmentWidthMode::Equal);
        textStyleControl2->SetSelectionMode(SegmentSelectionMode::Single);
        textStyleControl2->AddSegment("X<sup>2</sup>");   // X^2
        textStyleControl2->AddSegment("X<sub>2</sub>");   // X_2
//        textStyleControl->SetStyle(SegmentedControlStyle::Bordered);
        textStyleControl2->onSegmentClick = [statusLabel, textStyleControl2 = textStyleControl2.get()](int index) {
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

        // The two controls side by side, as one toolbar.
        auto textStyleGroup = DemoKit::CreateRow("TextStyleGroup", 0, false);
        DemoKit::AddFixed(textStyleGroup, textStyleControl);
        DemoKit::AddFixed(textStyleGroup, textStyleControl2);
        DemoKit::Add(row5, textStyleGroup);

        addDesc(row5, "TextStyleDesc", "• Text editor formatting toolbar\n• Click to enable, click again to disable\n• AllowNoSelection = true");

        // ========================================
        // SECTION 6: ALIGNMENT (FIT CONTENT MODE)
        // ========================================
        auto row6 = addSection("Section6", "6. Text Alignment (FitContent Width Mode)");

        auto alignmentControl = CreateSegmentedControl("alignment", 0, 0, 400, 34);
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
        DemoKit::AddFixed(row6, alignmentControl);

        addDesc(row6, "AlignDesc", "• Auto-sized segments based on text width\n• Custom blue theme\n• Perfect for toolbar alignment controls");

        // ========================================
        // SECTION 7: DISABLED SEGMENTS
        // ========================================
        auto row7 = addSection("Section7", "7. Disabled Segments Demonstration");

        // Five equal segments: wide enough for "Also Disabled" on one line
        // (a caption too wide for its segment is shortened with an ellipsis).
        auto disabledControl = CreateSegmentedControl("disabled", 0, 0, 660, 35);
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
        DemoKit::AddFixed(row7, disabledControl);

        addDesc(row7, "DisabledDesc", "• Individual segments can be disabled\n• Disabled segments: grayed out, not clickable\n• Keyboard navigation skips disabled segments");

        // ========================================
        // SECTION 8: CUSTOM WIDTH SEGMENTS
        // ========================================
        auto row8 = addSection("Section8", "8. Custom Width Segments");

        auto customWidthControl = CreateSegmentedControl("customWidth", 0, 0, 500, 36);
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
        DemoKit::AddFixed(row8, customWidthControl);

        addDesc(row8, "CustomWidthDesc", "• Segments auto-size to text length\n• Flat style with green theme\n• Perfect for variable-length options");

        // ========================================
        // KEYBOARD NAVIGATION INFO
        // ========================================
        DemoKit::AddNotes(page, "KeyboardNav", "Keyboard Navigation", {
                "Left/Right Arrow Keys: Navigate between segments",
                "Up/Down Arrow Keys: Also navigate (alternative)",
                "Home Key: Jump to first enabled segment",
                "End Key: Jump to last enabled segment",
                "Focus any control and use keyboard to select"});

        return page;
    }

} // namespace UltraCanvas
