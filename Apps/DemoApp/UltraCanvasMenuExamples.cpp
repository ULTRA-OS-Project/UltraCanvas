// Apps/DemoApp/UltraCanvasMenuExamples.cpp
// Implementation of all component example creators
// Version: 1.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
//#include "UltraCanvasButton3Sections.h"
#include "UltraCanvasFormulaEditor.h"
#include "Plugins/Charts/UltraCanvasDivergingBarChart.h"
#include <sstream>
#include <random>
#include <map>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateMenuExamples() {
        std::cerr << "Creating Menu Examples..." << std::endl;

        // Create container for menu examples
        auto container = std::make_shared<UltraCanvasContainer>("MenuContainer", 100, 0, 0, 1000, 630);
        container->SetBackgroundColor(Color(252, 252, 252, 255));
        container->SetPadding(0,0,10,0);

        // Section label for Context Menus
        auto contextLabel = std::make_shared<UltraCanvasLabel>("ContextLabel", 101, 20, 10, 300, 30);
        contextLabel->SetText("Context Menu Examples:");
        contextLabel->SetFontSize(14);
        contextLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(contextLabel);

        // Button to trigger context menu
        auto contextMenuBtn = std::make_shared<UltraCanvasButton>("ContextMenuBtn", 102, 20, 45, 280, 35);
        contextMenuBtn->SetText("Right-Click for Context Menu");
//        contextMenuBtn->SetTooltipText("Right-click anywhere on this button to show context menu");
        container->AddChild(contextMenuBtn);

        // Create context menu
        auto contextMenu = std::make_shared<UltraCanvasMenu>("ContextMenu1", 103, 0, 0, 200, 0);
        contextMenu->SetMenuType(MenuType::PopupMenu);

        // Add context menu items
        contextMenu->AddItem(MenuItemData::ActionWithShortcut("📋 Copy", "Ctrl+C", []() {
            std::cerr << "Copy action triggered" << std::endl;
        }));

        contextMenu->AddItem(MenuItemData::ActionWithShortcut("✂️ Cut", "Ctrl+X", []() {
            std::cerr << "Cut action triggered" << std::endl;
        }));

        contextMenu->AddItem(MenuItemData::ActionWithShortcut("📌 Paste", "Ctrl+V", []() {
            std::cerr << "Paste action triggered" << std::endl;
        }));

        contextMenu->AddItem(MenuItemData::Separator());

        // Submenu example
        MenuItemData formatItem("🎨 Format");
        formatItem.type = MenuItemType::Submenu;
        formatItem.subItems = {
                MenuItemData::ActionWithShortcut("Bold", "Ctrl+B", []() { std::cerr << "Bold" << std::endl; }),
                MenuItemData::ActionWithShortcut("Italic", "Ctrl+I", []() { std::cerr << "Italic" << std::endl; }),
                MenuItemData::ActionWithShortcut("Underline", "Ctrl+U", []() { std::cerr << "Underline" << std::endl; })
        };
        contextMenu->AddItem(formatItem);

        contextMenu->AddItem(MenuItemData::Separator());

        contextMenu->AddItem(MenuItemData::ActionWithShortcut("🗑️ Delete", "Del", []() {
            std::cerr << "Delete action triggered" << std::endl;
        }));

        // Set right-click handler for button
        contextMenuBtn->onClick = [contextMenu, contextMenuBtn, container]() {
            auto ev = UltraCanvasApplication::GetInstance()->GetCurrentEvent();
            if (ev.button == UCMouseButton::Right) {
                // move menu to window container
                container->GetWindow()->AddChild(contextMenu);
                contextMenu->ShowAt(ev.windowX, ev.windowY);
            }
        };
        container->AddChild(contextMenu);

        // Section label for Main Menu Bar
        auto mainMenuLabel = std::make_shared<UltraCanvasLabel>("MainMenuLabel", 104, 20, 100, 250, 30);
        mainMenuLabel->SetText("Main Menu Bar Example:");
        mainMenuLabel->SetFontSize(14);
        mainMenuLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(mainMenuLabel);

// Create main menu bar using MenuType::Menubar and MenuBuilder
        auto mainMenuBar = MenuBuilder("MainMenuBar", 105, 20, 135, 960, 32)
                .SetType(MenuType::Menubar)
                .AddSubmenu("File", {
                        MenuItemData::ActionWithShortcut("📄 New", "Ctrl+N", []() {
                                std::cerr << "New file" << std::endl;
                            }),
                        MenuItemData::ActionWithShortcut("📂 Open...", "Ctrl+O", []() {
                                std::cerr << "Open file" << std::endl;
                            }),
                        MenuItemData::Submenu("📁 Recent Files", {
                                MenuItemData::Action("Document1.txt", []() {
                                            std::cerr << "Open Document1.txt" << std::endl;
                                        }),
                                MenuItemData::Action("Project.cpp", []() {
                                            std::cerr << "Open Project.cpp" << std::endl;
                                        }),
                                MenuItemData::Action("Config.json", []() {
                                            std::cerr << "Open Config.json" << std::endl;
                                        })
                            }),
                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("💾 Save", "Ctrl+S", []() {
                                std::cerr << "Save file" << std::endl;
                            }),
                        MenuItemData::ActionWithShortcut("💾 Save As...", "Ctrl+Shift+S", []() {
                                std::cerr << "Save as" << std::endl;
                            }),
                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("🚪 Exit", "Alt+F4", []() {
                                std::cerr << "Exit application" << std::endl;
                            })
                    })
                .AddSubmenu("Edit", {
                            MenuItemData::ActionWithShortcut("↩️ Undo", "Ctrl+Z", []() {
                                std::cerr << "Undo" << std::endl;
                            }),
                            MenuItemData::ActionWithShortcut("↪️ Redo", "Ctrl+Y", []() {
                                std::cerr << "Redo" << std::endl;
                            }),
                            MenuItemData::Separator(),
                            MenuItemData::ActionWithShortcut("✂️ Cut", "Ctrl+X", []() {
                                std::cerr << "Cut" << std::endl;
                            }),
                            MenuItemData::ActionWithShortcut("📋 Copy", "Ctrl+C", []() {
                                std::cerr << "Copy" << std::endl;
                            }),
                            MenuItemData::ActionWithShortcut("📌 Paste", "Ctrl+V", []() {
                                std::cerr << "Paste" << std::endl;
                            }),
                            MenuItemData::Separator(),
                            MenuItemData::ActionWithShortcut("🔍 Find...", "Ctrl+F", []() {
                                std::cerr << "Find" << std::endl;
                            }),
                            MenuItemData::ActionWithShortcut("🔄 Replace...", "Ctrl+H", []() {
                                std::cerr << "Replace" << std::endl;
                            })
                })
                .AddSubmenu("View", {
                        MenuItemData::Checkbox("🔧 Toolbar", true, [](bool checked) {
                                std::cerr << "Toolbar " << (checked ? "shown" : "hidden") << std::endl;
                            }),
                            MenuItemData::Checkbox("📊 Status Bar", true, [](bool checked) {
                                std::cerr << "Status bar " << (checked ? "shown" : "hidden") << std::endl;
                            }),
                            MenuItemData::Checkbox("📁 Sidebar", false, [](bool checked) {
                                std::cerr << "Sidebar " << (checked ? "shown" : "hidden") << std::endl;
                            }),
                            MenuItemData::Separator(),
                            MenuItemData::Radio("Zoom 50%", 1, false, [](bool checked) {
                                if (checked) std::cerr << "Zoom 50%" << std::endl;
                            }),
                            MenuItemData::Radio("Zoom 100%", 1, true, [](bool checked) {
                                if (checked) std::cerr << "Zoom 100%" << std::endl;
                            }),
                            MenuItemData::Radio("Zoom 150%", 1, false, [](bool checked) {
                                if (checked) std::cerr << "Zoom 150%" << std::endl;
                            })
                })
                .AddSubmenu("Help", {
                            MenuItemData::ActionWithShortcut("📖 Documentation", "F1", []() {
                                std::cerr << "Show documentation" << std::endl;
                            }),
                            MenuItemData::Action("🎓 Tutorials", []() {
                                std::cerr << "Show tutorials" << std::endl;
                            }),
                            MenuItemData::Separator(),
                            MenuItemData::Action("ℹ️ About UltraCanvas", []() {
                                std::cout << "About UltraCanvas Framework" << std::endl;
                            })
                })
                .Build();

        container->AddChild(mainMenuBar);

        // Dark theme menu
        auto darkMenuBtn = std::make_shared<UltraCanvasButton>("DarkMenuBtn", 115, 20, 225, 150, 35);
        darkMenuBtn->SetText("Dark Theme Menu");
        container->AddChild(darkMenuBtn);

        auto darkMenu = std::make_shared<UltraCanvasMenu>("DarkMenu", 116, 0, 0, 200, 0);
        darkMenu->SetMenuType(MenuType::PopupMenu);
        darkMenu->SetStyle(MenuStyle::Dark());

        darkMenu->AddItem(MenuItemData::Action("🌙 Dark Mode", []() {
            std::cout << "Dark mode activated" << std::endl;
        }));

        darkMenu->AddItem(MenuItemData::Action("☀️ Light Mode", []() {
            std::cout << "Light mode activated" << std::endl;
        }));

        darkMenu->AddItem(MenuItemData::Action("🎨 Custom Theme", []() {
            std::cout << "Custom theme" << std::endl;
        }));

        darkMenuBtn->onClick = [darkMenu, darkMenuBtn, container]() {
            container->GetWindow()->AddChild(darkMenu);
            Point2Di pos(darkMenuBtn->GetXInWindow(), darkMenuBtn->GetYInWindow() + darkMenuBtn->GetHeight() + 1);
            darkMenu->ShowAt(pos);
        };

        container->AddChild(darkMenu);

        // Flat style menu
        auto flatMenuBtn = std::make_shared<UltraCanvasButton>("FlatMenuBtn", 117, 180, 225, 150, 35);
        flatMenuBtn->SetText("Flat Style Menu");
        container->AddChild(flatMenuBtn);

        auto flatMenu = std::make_shared<UltraCanvasMenu>("FlatMenu", 118, 0, 0, 200, 0);
        flatMenu->SetMenuType(MenuType::PopupMenu);
        flatMenu->SetStyle(MenuStyle::Flat());

        flatMenu->AddItem(MenuItemData::Action("📱 Mobile View", []() {
            std::cout << "Mobile view" << std::endl;
        }));

        flatMenu->AddItem(MenuItemData::Action("💻 Desktop View", []() {
            std::cout << "Desktop view" << std::endl;
        }));

        flatMenu->AddItem(MenuItemData::Action("Tablet View", "media/icons/tablet48px.png", []() {
            std::cout << "Tablet view" << std::endl;
        }));

        flatMenuBtn->onClick = [flatMenu, flatMenuBtn, container]() {
            container->GetWindow()->AddChild(flatMenu);
            Point2Di pos(flatMenuBtn->GetXInWindow(), flatMenuBtn->GetYInWindow() + flatMenuBtn->GetHeight() + 1);
            flatMenu->ShowAt(pos);
        };

        container->AddChild(flatMenu);

        // Info label about menu features
        auto infoLabel = std::make_shared<UltraCanvasLabel>("InfoLabel", 119, 20, 270, 960, 140);
        infoLabel->SetText("Menu Features:\n"
                           "• Context menus with right-click\n"
                           "• Main menu bar with dropdowns\n"
                           "• Submenus and nested navigation\n"
                           "• Checkbox and radio button items\n"
                           "• Keyboard shortcuts and icons\n"
                           "• Multiple visual styles (Default, Dark, Flat)");
        infoLabel->SetFontSize(11);
        infoLabel->SetTextColor(Color(80, 80, 80, 255));
        container->AddChild(infoLabel);

        // Popup menu example
        auto popupLabel = std::make_shared<UltraCanvasLabel>("PopupLabel", 120, 20, 425, 200, 30);
        popupLabel->SetText("Popup Menu Example:");
        popupLabel->SetFontSize(14);
        popupLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(popupLabel);

        // Create a sample list for popup menu
        auto listContainer = std::make_shared<UltraCanvasContainer>("ListContainer", 121, 20, 460, 300, 150);
        listContainer->SetBackgroundColor(Color(255, 255, 255, 255));
        listContainer->SetBorders(1, Color(200, 200, 200, 255));
        container->AddChild(listContainer);

        // Add sample items to list
        for (int i = 0; i < 5; i++) {
            auto itemLabel = std::make_shared<UltraCanvasLabel>(
                    "ListItem" + std::to_string(i),
                    122 + i,
                    10, 10 + i * 25, 280, 20
            );
            itemLabel->SetText("Item " + std::to_string(i + 1) + " - Right-click for options");
            itemLabel->SetBackgroundColor(Color(250, 250, 250, 255));

            // Create item-specific popup menu
            auto itemMenu = std::make_shared<UltraCanvasMenu>(
                    "ItemMenu" + std::to_string(i),
                    130 + i,
                    0, 0, 150, 0
            );
            itemMenu->SetMenuType(MenuType::PopupMenu);

            itemMenu->AddItem(MenuItemData::Action("✏️ Edit", [i]() {
                std::cout << "Edit item " << (i + 1) << std::endl;
            }));

            itemMenu->AddItem(MenuItemData::Action("📋 Duplicate", [i]() {
                std::cout << "Duplicate item " << (i + 1) << std::endl;
            }));

            itemMenu->AddItem(MenuItemData::Action("🗑️ Delete", [i]() {
                std::cout << "Delete item " << (i + 1) << std::endl;
            }));

            // Set right-click handler
            itemLabel->onClick = [itemMenu, itemLabel, container]() {
                auto ev = UltraCanvasApplication::GetInstance()->GetCurrentEvent();
                if (ev.button == UCMouseButton::Right) {
                    container->GetWindow()->AddChild(itemMenu);
                    //Point2Di pos(itemLabel->GetXInWindow() + 50, itemLabel->GetYInWindow() + itemLabel->GetHeight());
                    itemMenu->ShowAt(ev.windowX, ev.windowY);
                }
            };

            listContainer->AddChild(itemLabel);
            container->AddChild(itemMenu);
        }

        std::cout << "✓ Menu examples created" << std::endl;
        return container;
    }
} // namespace UltraCanvas