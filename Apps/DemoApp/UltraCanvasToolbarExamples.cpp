// Apps/DemoApp/UltraCanvasToolbarExamples.cpp
// Comprehensive demonstration of toolbar component functionality
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): one card per
//                  example, every text sized to fit, toolbars as wide as the page
// Version: 1.0.2
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasButton.h"
#include <sstream>
#include <iostream>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateToolbarExamples() {
        debugOutput << "Creating Toolbar Examples..." << std::endl;

        auto page = DemoKit::CreatePage("ToolbarExamples", "Toolbar",
                "Flexible toolbar system with buttons, dropdowns, separators, and various styles");

        // ===== EXAMPLE 1: STANDARD HORIZONTAL TOOLBAR =====
        auto card1 = DemoKit::AddCard(page, "ToolbarCard1", "1. Standard Horizontal Toolbar",
                "Classic toolbar with buttons, separators, and flexible spacing");

        // Create standard toolbar using builder
        auto standardToolbar = UltraCanvasToolbarBuilder("StandardToolbar")
                .SetOrientation(ToolbarOrientation::Horizontal)
                .SetAppearance(ToolbarAppearance::Default())
                .SetDimensions(0, 0, 960, 48)
                .AddButton("new", "", NormalizePath(GetResourcesDir() + "media/icons/add-document.svg"), []() {
                    debugOutput << "New button clicked" << std::endl;
                })
                .AddButton("open", "", NormalizePath(GetResourcesDir() + "media/icons/folder-open.svg"), []() {
                    debugOutput << "Open button clicked" << std::endl;
                })
                .AddButton("save", "", NormalizePath(GetResourcesDir() + "media/icons/save.svg"), []() {
                    debugOutput << "Save button clicked" << std::endl;
                })
                .AddSeparator()
                .AddButton("cut", "", NormalizePath(GetResourcesDir() + "media/icons/scissors.svg"), []() {
                    debugOutput << "Cut button clicked" << std::endl;
                })
                .AddButton("copy", "", NormalizePath(GetResourcesDir() + "media/icons/copy.svg"), []() {
                    debugOutput << "Copy button clicked" << std::endl;
                })
                .AddButton("paste", "", NormalizePath(GetResourcesDir() + "media/icons/paste.svg"), []() {
                    debugOutput << "Paste button clicked" << std::endl;
                })
                .AddSeparator()
                .AddStretch(1.0f)
                .AddLabel("status", "Ready")
                .Build();

        DemoKit::AddStretched(card1, standardToolbar);

        // ===== EXAMPLE 2: TOOLBAR WITH DROPDOWNS =====
        auto card2 = DemoKit::AddCard(page, "ToolbarCard2", "2. Toolbar with Dropdown Menus",
                "Toolbar combining buttons and dropdown selectors");

        auto dropdownToolbar = std::make_shared<UltraCanvasToolbar>(
                "DropdownToolbar", 0, 0, 960, 48
        );
        dropdownToolbar->SetOrientation(ToolbarOrientation::Horizontal);
        dropdownToolbar->SetAppearance(ToolbarAppearance::Default());

        // Add buttons and dropdowns
        dropdownToolbar->AddButton("undo", "", NormalizePath(GetResourcesDir() + "media/icons/undo.svg"), []() {
            debugOutput << "Undo clicked" << std::endl;
        });
        dropdownToolbar->AddButton("redo", "", NormalizePath(GetResourcesDir() + "media/icons/redo.svg"), []() {
            debugOutput << "Redo clicked" << std::endl;
        });
        dropdownToolbar->AddSeparator();

        // Add dropdown for font selection
        std::vector<std::string> fonts = {"Arial", "Times New Roman", "Courier New", "Verdana", "Georgia"};
        dropdownToolbar->AddDropdownButton("font", "Arial", fonts, [](const std::string& selected) {
            debugOutput << "Font selected: " << selected << std::endl;
        });

        dropdownToolbar->AddSpacer(10);

        // Add dropdown for font size
        std::vector<std::string> sizes = {"8", "10", "12", "14", "16", "18", "20", "24"};
        dropdownToolbar->AddDropdownButton("size", "12", sizes, [](const std::string& selected) {
            debugOutput << "Size selected: " << selected << std::endl;
        });

        dropdownToolbar->AddSeparator();

        dropdownToolbar->AddToggleButton("bold", "B", "", [](bool checked) {
            debugOutput << "Bold: " << (checked ? "ON" : "OFF") << std::endl;
        });
        dropdownToolbar->AddToggleButton("italic", "I", "", [](bool checked) {
            debugOutput << "Italic: " << (checked ? "ON" : "OFF") << std::endl;
        });
        dropdownToolbar->AddToggleButton("underline", "U", "", [](bool checked) {
            debugOutput << "Underline: " << (checked ? "ON" : "OFF") << std::endl;
        });

        DemoKit::AddStretched(card2, dropdownToolbar);

        // ===== EXAMPLE 3: FLAT STYLE TOOLBAR =====
        auto card3 = DemoKit::AddCard(page, "ToolbarCard3", "3. Flat Style Toolbar",
                "Modern flat design without borders");

        auto flatToolbar = UltraCanvasToolbarBuilder("FlatToolbar")
                .SetOrientation(ToolbarOrientation::Horizontal)
                .SetAppearance(ToolbarAppearance::Flat())
                .SetDimensions(0, 0, 960, 48)
                .AddButton("home", "", NormalizePath(GetResourcesDir() + "media/icons/house.chimney.svg"), []() {
                    debugOutput << "Home clicked" << std::endl;
                })
                .AddButton("profile", "", NormalizePath(GetResourcesDir() + "media/icons/face-awesome.svg"), []() {
                    debugOutput << "Profile clicked" << std::endl;
                })
                .AddButton("settings", "", NormalizePath(GetResourcesDir() + "media/icons/settings.svg"), []() {
                    debugOutput << "Settings clicked" << std::endl;
                })
                .AddStretch(1.0f)
                .AddButton("notifications", "", NormalizePath(GetResourcesDir() + "media/icons/bell-icon.svg"), []() {
                    debugOutput << "Notifications clicked" << std::endl;
                })
                .AddButton("messages", "", NormalizePath(GetResourcesDir() + "media/icons/envelope.svg"), []() {
                    debugOutput << "Messages clicked" << std::endl;
                })
                .Build();

        DemoKit::AddStretched(card3, flatToolbar);

        // ===== EXAMPLE 4: VERTICAL SIDEBAR TOOLBAR =====
        auto card4 = DemoKit::AddCard(page, "ToolbarCard4", "4. Vertical Sidebar Toolbar",
                "Vertical orientation for sidebar navigation");

        // The sidebar beside a content column, which takes the rest of the
        // width and is as tall as its text.
        auto sidebarContainer = DemoKit::CreateRow("SidebarDemo", 16, false);
        sidebarContainer->SetBackgroundColor(Color(245, 245, 245, 255));
        sidebarContainer->SetBorders(1, Color(220, 220, 220, 255));
        sidebarContainer->SetPadding(10);
        DemoKit::Add(card4, sidebarContainer);

        // Toolbar height is sized to its content so no free space is left below
        // the last icon: 5 buttons x 32 + separator 1 + 5 gaps x 4 = 181, plus
        // 3+3 vertical padding and 1+1 borders = 189. Width 44 = 32 (button) +
        // 5+5 horizontal padding + 1+1 borders, so icons get equal side margins.
        auto verticalToolbar = UltraCanvasToolbarBuilder("VerticalToolbar")
                .SetOrientation(ToolbarOrientation::Vertical)
                .SetAppearance(ToolbarAppearance::Sidebar())
                .SetDimensions(0, 0, 44, 189)
                .AddButton("dashboard", "", DemoIconPath("menu/view-type.svg"), []() {
                    debugOutput << "Dashboard clicked" << std::endl;
                })
                .AddButton("files", "", DemoIconPath("menu/folder.svg"), []() {
                    debugOutput << "Files clicked" << std::endl;
                })
                .AddButton("users", "", DemoIconPath("menu/users.svg"), []() {
                    debugOutput << "Users clicked" << std::endl;
                })
                .AddSeparator()
                .AddButton("analytics", "", DemoIconPath("menu/chart.svg"), []() {
                    debugOutput << "Analytics clicked" << std::endl;
                })
                .AddButton("reports", "", DemoIconPath("menu/paste.svg"), []() {
                    debugOutput << "Reports clicked" << std::endl;
                })
                .Build();

        // The buttons are created with auto width, so each one would size to its
        // icon and the icons would sit at slightly different distances
        // from the toolbar edges. Force a uniform 32x32 square for every button
        // so all icons show with the same margins inside the toolbar.
        for (const char* buttonId : {"dashboard", "files", "users", "analytics", "reports"}) {
            if (auto widget = verticalToolbar->GetWidget(buttonId)) {
                widget->SetElementSize({32.0f, 32.0f});
            }
        }
        DemoKit::AddFixed(sidebarContainer, verticalToolbar);

        auto contentColumn = DemoKit::CreateColumn("SidebarContent", 8);
        auto contentLabel = DemoKit::CreateHeading("ContentLabel", "Main Content Area");
        DemoKit::Add(contentColumn, contentLabel);
        DemoKit::AddText(contentColumn, "ContentDesc",
                "The vertical toolbar on the left provides navigation controls.\n\n"
                "This is a common pattern in modern applications where the sidebar "
                "contains primary navigation options while the main content area "
                "displays the selected content.");
        DemoKit::Add(sidebarContainer, contentColumn, 1);

        // ===== EXAMPLE 5: RIBBON STYLE TOOLBAR =====
        auto card5 = DemoKit::AddCard(page, "ToolbarCard5", "5. Ribbon Style Toolbar",
                "Office-style ribbon with grouped controls");

        auto ribbonToolbar = UltraCanvasToolbarBuilder("RibbonToolbar")
                .SetOrientation(ToolbarOrientation::Horizontal)
                .SetAppearance(ToolbarAppearance::Ribbon())
                .SetDimensions(0, 0, 960, 58)
                .AddButton("clipboardIcon", "", NormalizePath(GetResourcesDir() + "media/icons/clipboard-list.svg"), []() {})
                .AddLabel("clipboard", "Clipboard")
                .AddSpacer(5)
                .AddButton("cut", "", NormalizePath(GetResourcesDir() + "media/icons/scissors.svg"), []() {})
                .AddButton("copy", "", NormalizePath(GetResourcesDir() + "media/icons/copy.svg"), []() {})
                .AddButton("paste", "", NormalizePath(GetResourcesDir() + "media/icons/paste.svg"), []() {})
                .AddSpacer(20)
                .AddLabel("format", "Format")
                .AddSpacer(5)
                .AddButton("bold", "B", "", []() {})
                .AddButton("italic", "I", "", []() {})
                .AddButton("underline", "U", "", []() {})
                .AddSpacer(20)
                .AddLabel("insert", "Insert")
                .AddSpacer(5)
                .AddButton("image", "", NormalizePath(GetResourcesDir() + "media/icons/image.svg"), []() {})
                .AddButton("table", "", NormalizePath(GetResourcesDir() + "media/icons/table.svg"), []() {})
                .AddButton("chart", "", NormalizePath(GetResourcesDir() + "media/icons/chart.svg"), []() {})
                .Build();

        DemoKit::AddStretched(card5, ribbonToolbar);

        // ===== USAGE NOTES =====
        DemoKit::AddNotes(page, "ToolbarNotes", "Implementation Notes", {
                "Use UltraCanvasToolbarBuilder for fluent construction",
                "AddSpacer(size) creates fixed spacing between items",
                "AddFlexSpacer(stretch) creates flexible space that grows",
                "AddSeparator() adds visual dividers",
                "Supports both horizontal and vertical orientations",
                "Multiple built-in styles: Standard, Flat, Docked, Ribbon, Sidebar"});

        return page;
    }

} // namespace UltraCanvas