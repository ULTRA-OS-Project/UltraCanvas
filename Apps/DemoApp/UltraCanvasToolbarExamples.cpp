// Apps/DemoApp/UltraCanvasToolbarExamples.cpp
// Comprehensive demonstration of toolbar component functionality
// Version: 1.0.0
// Last Modified: 2025-11-18
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasButton.h"
#include <sstream>
#include <iostream>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateToolbarExamples() {
        std::cout << "Creating Toolbar Examples..." << std::endl;

        // Main container for all toolbar examples
        auto mainContainer = std::make_shared<UltraCanvasContainer>(
                "ToolbarExamples", 800, 0, 0, 1000, 1000
        );
        mainContainer->SetBackgroundColor(Color(252, 252, 252, 255));
        mainContainer->SetPadding(0,0,10,0);

        int currentY = 10;

        // ===== PAGE HEADER =====
        auto title = std::make_shared<UltraCanvasLabel>(
                "ToolbarTitle", 801, 20, currentY, 500, 35
        );
        title->SetText("Toolbar Component Examples");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150));
        mainContainer->AddChild(title);
        currentY += 40;

        auto subtitle = std::make_shared<UltraCanvasLabel>(
                "ToolbarSubtitle", 802, 20, currentY, 960, 25
        );
        subtitle->SetText("Flexible toolbar system with buttons, dropdowns, separators, and various styles");
        subtitle->SetFontSize(12);
        subtitle->SetTextColor(Color(100, 100, 100));
        mainContainer->AddChild(subtitle);
        currentY += 40;

        // ===== EXAMPLE 1: STANDARD HORIZONTAL TOOLBAR =====
        auto section1Label = std::make_shared<UltraCanvasLabel>(
                "Section1Label", 803, 20, currentY, 400, 25
        );
        section1Label->SetText("1. Standard Horizontal Toolbar");
        section1Label->SetFontSize(14);
        section1Label->SetFontWeight(FontWeight::Bold);
        mainContainer->AddChild(section1Label);
        currentY += 30;

        auto desc1 = std::make_shared<UltraCanvasLabel>(
                "Desc1", 804, 20, currentY, 960, 20
        );
        desc1->SetText("Classic toolbar with buttons, separators, and flexible spacing");
        desc1->SetFontSize(11);
        desc1->SetTextColor(Color(100, 100, 100));
        mainContainer->AddChild(desc1);
        currentY += 25;

        // Create standard toolbar using builder
        auto standardToolbar = UltraCanvasToolbarBuilder("StandardToolbar", 805)
                .SetOrientation(ToolbarOrientation::Horizontal)
                .SetStyle(ToolbarStyle::Standard)
                .SetDimensions(20, currentY, 960, 48)
                .AddButton("new", "", "media/icons/new-icon.png", []() {
                    std::cout << "New button clicked" << std::endl;
                })
                .AddButton("open", "", "media/icons/open-icon.png", []() {
                    std::cout << "Open button clicked" << std::endl;
                })
                .AddButton("save", "", "media/icons/save-icon.png", []() {
                    std::cout << "Save button clicked" << std::endl;
                })
                .AddSeparator()
                .AddButton("cut", "", "media/icons/cut-icon.png", []() {
                    std::cout << "Cut button clicked" << std::endl;
                })
                .AddButton("copy", "", "media/icons/copy-icon.png", []() {
                    std::cout << "Copy button clicked" << std::endl;
                })
                .AddButton("paste", "", "media/icons/paste-icon.png", []() {
                    std::cout << "Paste button clicked" << std::endl;
                })
                .AddSeparator()
                .AddStretch(1.0f)
                .AddLabel("status", "Ready")
                .Build();

        mainContainer->AddChild(standardToolbar);
        currentY += 65;

        // ===== EXAMPLE 2: TOOLBAR WITH DROPDOWNS =====
        auto section2Label = std::make_shared<UltraCanvasLabel>(
                "Section2Label", 806, 20, currentY, 400, 25
        );
        section2Label->SetText("2. Toolbar with Dropdown Menus");
        section2Label->SetFontSize(14);
        section2Label->SetFontWeight(FontWeight::Bold);
        mainContainer->AddChild(section2Label);
        currentY += 30;

        auto desc2 = std::make_shared<UltraCanvasLabel>(
                "Desc2", 807, 20, currentY, 960, 20
        );
        desc2->SetText("Toolbar combining buttons and dropdown selectors");
        desc2->SetFontSize(11);
        desc2->SetTextColor(Color(100, 100, 100));
        mainContainer->AddChild(desc2);
        currentY += 25;

        auto dropdownToolbar = std::make_shared<UltraCanvasToolbar>(
                "DropdownToolbar", 808, 20, currentY, 960, 48
        );
        dropdownToolbar->SetOrientation(ToolbarOrientation::Horizontal);
        dropdownToolbar->SetStyle(ToolbarStyle::Standard);

        // Add buttons and dropdowns
        dropdownToolbar->AddButton("undo", "", "media/icons/undo-icon.png", []() {
            std::cout << "Undo clicked" << std::endl;
        });
        dropdownToolbar->AddButton("redo", "", "media/icons/redo-icon.png", []() {
            std::cout << "Redo clicked" << std::endl;
        });
        dropdownToolbar->AddSeparator();

        // Add dropdown for font selection
        std::vector<std::string> fonts = {"Arial", "Times New Roman", "Courier New", "Verdana", "Georgia"};
        dropdownToolbar->AddDropdownButton("font", "Arial", fonts, [](const std::string& selected) {
            std::cout << "Font selected: " << selected << std::endl;
        });

        dropdownToolbar->AddSpacer(10);

        // Add dropdown for font size
        std::vector<std::string> sizes = {"8", "10", "12", "14", "16", "18", "20", "24"};
        dropdownToolbar->AddDropdownButton("size", "12", sizes, [](const std::string& selected) {
            std::cout << "Size selected: " << selected << std::endl;
        });

        dropdownToolbar->AddSeparator();

        dropdownToolbar->AddToggleButton("bold", "B", "", [](bool checked) {
            std::cout << "Bold: " << (checked ? "ON" : "OFF") << std::endl;
        });
        dropdownToolbar->AddToggleButton("italic", "I", "", [](bool checked) {
            std::cout << "Italic: " << (checked ? "ON" : "OFF") << std::endl;
        });
        dropdownToolbar->AddToggleButton("underline", "U", "", [](bool checked) {
            std::cout << "Underline: " << (checked ? "ON" : "OFF") << std::endl;
        });

        mainContainer->AddChild(dropdownToolbar);
        currentY += 55;

        // ===== EXAMPLE 3: FLAT STYLE TOOLBAR =====
        auto section3Label = std::make_shared<UltraCanvasLabel>(
                "Section3Label", 809, 20, currentY, 400, 25
        );
        section3Label->SetText("3. Flat Style Toolbar");
        section3Label->SetFontSize(14);
        section3Label->SetFontWeight(FontWeight::Bold);
        mainContainer->AddChild(section3Label);
        currentY += 30;

        auto desc3 = std::make_shared<UltraCanvasLabel>(
                "Desc3", 810, 20, currentY, 960, 20
        );
        desc3->SetText("Modern flat design without borders");
        desc3->SetFontSize(11);
        desc3->SetTextColor(Color(100, 100, 100));
        mainContainer->AddChild(desc3);
        currentY += 25;

        auto flatToolbar = UltraCanvasToolbarBuilder("FlatToolbar", 811)
                .SetOrientation(ToolbarOrientation::Horizontal)
                .SetStyle(ToolbarStyle::Flat)
                .SetAppearance(ToolbarAppearance::Flat())
                .SetDimensions(20, currentY, 960, 48)
                .AddButton("home", "", "media/icons/home-icon.png", []() {
                    std::cout << "Home clicked" << std::endl;
                })
                .AddButton("profile", "", "media/icons/profile-icon.png", []() {
                    std::cout << "Profile clicked" << std::endl;
                })
                .AddButton("settings", "", "media/icons/settings.png", []() {
                    std::cout << "Settings clicked" << std::endl;
                })
                .AddStretch(1.0f)
                .AddButton("notifications", "", "media/icons/bell-icon.png", []() {
                    std::cout << "Notifications clicked" << std::endl;
                })
                .AddButton("messages", "", "media/icons/envelope-icon.png", []() {
                    std::cout << "Messages clicked" << std::endl;
                })
                .Build();

        mainContainer->AddChild(flatToolbar);
        currentY += 55;

        // ===== EXAMPLE 4: VERTICAL SIDEBAR TOOLBAR =====
        auto section4Label = std::make_shared<UltraCanvasLabel>(
                "Section4Label", 812, 20, currentY, 400, 25
        );
        section4Label->SetText("4. Vertical Sidebar Toolbar");
        section4Label->SetFontSize(14);
        section4Label->SetFontWeight(FontWeight::Bold);
        mainContainer->AddChild(section4Label);
        currentY += 30;

        auto desc4 = std::make_shared<UltraCanvasLabel>(
                "Desc4", 813, 20, currentY, 960, 20
        );
        desc4->SetText("Vertical orientation for sidebar navigation");
        desc4->SetFontSize(11);
        desc4->SetTextColor(Color(100, 100, 100));
        mainContainer->AddChild(desc4);
        currentY += 25;

        // Container to demonstrate vertical toolbar
        auto sidebarContainer = std::make_shared<UltraCanvasContainer>(
                "SidebarDemo", 814, 20, currentY, 960, 240
        );
        sidebarContainer->SetBackgroundColor(Color(245, 245, 245, 255));
        sidebarContainer->SetBorders(1, Color(220, 220, 220, 255));

        auto verticalToolbar = UltraCanvasToolbarBuilder("VerticalToolbar", 815)
                .SetOrientation(ToolbarOrientation::Vertical)
                .SetStyle(ToolbarStyle::Sidebar)
                .SetDimensions(10, 10, 50, 220)
                .AddButton("dashboard", "📊", "", []() {
                    std::cout << "Dashboard clicked" << std::endl;
                })
                .AddButton("files", "📁", "", []() {
                    std::cout << "Files clicked" << std::endl;
                })
                .AddButton("users", "👥", "", []() {
                    std::cout << "Users clicked" << std::endl;
                })
                .AddSeparator()
                .AddButton("analytics", "📈", "", []() {
                    std::cout << "Analytics clicked" << std::endl;
                })
                .AddButton("reports", "📋", "", []() {
                    std::cout << "Reports clicked" << std::endl;
                })
                .Build();

        sidebarContainer->AddChild(verticalToolbar);

        // Content area label
        auto contentLabel = std::make_shared<UltraCanvasLabel>(
                "ContentLabel", 816, 80, 10, 860, 30
        );
        contentLabel->SetText("Main Content Area");
        contentLabel->SetFontSize(16);
        contentLabel->SetFontWeight(FontWeight::Bold);
        contentLabel->SetTextColor(Color(100, 100, 100));
        sidebarContainer->AddChild(contentLabel);

        auto contentDesc = std::make_shared<UltraCanvasLabel>(
                "ContentDesc", 817, 80, 45, 860, 140
        );
        contentDesc->SetText("The vertical toolbar on the left provides navigation controls.\n\n"
                             "This is a common pattern in modern applications where the sidebar "
                             "contains primary navigation options while the main content area "
                             "displays the selected content.");
        contentDesc->SetFontSize(11);
        contentDesc->SetTextColor(Color(120, 120, 120));
        sidebarContainer->AddChild(contentDesc);

        mainContainer->AddChild(sidebarContainer);
        currentY += 255;

        // ===== EXAMPLE 5: RIBBON STYLE TOOLBAR =====
        auto section5Label = std::make_shared<UltraCanvasLabel>(
                "Section5Label", 818, 20, currentY, 400, 25
        );
        section5Label->SetText("5. Ribbon Style Toolbar");
        section5Label->SetFontSize(14);
        section5Label->SetFontWeight(FontWeight::Bold);
        mainContainer->AddChild(section5Label);
        currentY += 30;

        auto desc5 = std::make_shared<UltraCanvasLabel>(
                "Desc5", 819, 20, currentY, 960, 20
        );
        desc5->SetText("Office-style ribbon with grouped controls");
        desc5->SetFontSize(11);
        desc5->SetTextColor(Color(100, 100, 100));
        mainContainer->AddChild(desc5);
        currentY += 25;

        auto ribbonToolbar = UltraCanvasToolbarBuilder("RibbonToolbar", 820)
                .SetOrientation(ToolbarOrientation::Horizontal)
                .SetStyle(ToolbarStyle::Ribbon)
                .SetAppearance(ToolbarAppearance::Ribbon())
                .SetDimensions(20, currentY, 960, 58)
                .AddLabel("clipboard", "Clipboard")
                .AddSpacer(5)
                .AddButton("cut", "", "media/icons/cut-icon.png", []() {})
                .AddButton("copy", "", "media/icons/copy-icon.png", []() {})
                .AddButton("paste", "", "media/icons/paste-icon.png", []() {})
                .AddSpacer(20)
                .AddLabel("format", "Format")
                .AddSpacer(5)
                .AddButton("bold", "B", "", []() {})
                .AddButton("italic", "I", "", []() {})
                .AddButton("underline", "U", "", []() {})
                .AddSpacer(20)
                .AddLabel("insert", "Insert")
                .AddSpacer(5)
                .AddButton("image", "", "media/icons/image-icon.png", []() {})
                .AddButton("table", "", "media/icons/table-icon.png", []() {})
                .AddButton("chart", "", "media/icons/chart-icon.png", []() {})
                .Build();

        mainContainer->AddChild(ribbonToolbar);
        currentY += 65;

        // ===== USAGE NOTES =====
        auto notesLabel = std::make_shared<UltraCanvasLabel>(
                "NotesLabel", 821, 20, currentY, 960, 25
        );
        notesLabel->SetText("Implementation Notes:");
        notesLabel->SetFontSize(12);
        notesLabel->SetFontWeight(FontWeight::Bold);
        mainContainer->AddChild(notesLabel);
        currentY += 25;

        auto notes = std::make_shared<UltraCanvasLabel>(
                "Notes", 822, 20, currentY, 960, 80
        );
        notes->SetText(
                "• Use UltraCanvasToolbarBuilder for fluent construction\n"
                "• AddSpacer(size) creates fixed spacing between items\n"
                "• AddFlexSpacer(stretch) creates flexible space that grows\n"
                "• AddSeparator() adds visual dividers\n"
                "• Supports both horizontal and vertical orientations\n"
                "• Multiple built-in styles: Standard, Flat, Docked, Ribbon, Sidebar"
        );
        notes->SetFontSize(11);
        notes->SetTextColor(Color(80, 80, 80));
        mainContainer->AddChild(notes);

        return mainContainer;
    }

} // namespace UltraCanvas