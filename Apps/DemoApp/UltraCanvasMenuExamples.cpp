// Apps/DemoApp/UltraCanvasMenuExamples.cpp
// Implementation of all component example creators
// Version: 1.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
//#include "UltraCanvasButton3Sections.h"
#include "Plugins/Charts/UltraCanvasDivergingBarChart.h"
#include <sstream>
#include <random>
#include <map>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateMenuExamples() {
        debugOutput << "Creating Menu Examples..." << std::endl;

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
            debugOutput << "Copy action triggered" << std::endl;
        }));

        contextMenu->AddItem(MenuItemData::ActionWithShortcut("✂️ Cut", "Ctrl+X", []() {
            debugOutput << "Cut action triggered" << std::endl;
        }));

        contextMenu->AddItem(MenuItemData::ActionWithShortcut("📌 Paste", "Ctrl+V", []() {
            debugOutput << "Paste action triggered" << std::endl;
        }));

        contextMenu->AddItem(MenuItemData::Separator());

        // Submenu example
        MenuItemData formatItem("🎨 Format");
        formatItem.type = MenuItemType::Submenu;
        formatItem.subItems = {
                MenuItemData::ActionWithShortcut("Bold", "Ctrl+B", []() { debugOutput << "Bold" << std::endl; }),
                MenuItemData::ActionWithShortcut("Italic", "Ctrl+I", []() { debugOutput << "Italic" << std::endl; }),
                MenuItemData::ActionWithShortcut("Underline", "Ctrl+U", []() { debugOutput << "Underline" << std::endl; })
        };
        contextMenu->AddItem(formatItem);

        contextMenu->AddItem(MenuItemData::Separator());

        contextMenu->AddItem(MenuItemData::ActionWithShortcut("🗑️ Delete", "Del", []() {
            debugOutput << "Delete action triggered" << std::endl;
        }));

        // Set right-click handler for button
        contextMenuBtn->onClick = [contextMenu, contextMenuBtn, container]() {
            auto ev = UltraCanvasApplication::GetInstance()->GetCurrentEvent();
            if (ev.button == UCMouseButton::Right) {
                // move menu to window container
//                container->GetWindow()->AddChild(contextMenu);
                contextMenu->OpenMenu(Point2Di(ev.windowX, ev.windowY), *container->GetWindow(), PopupElementSettings());
            }
        };

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
                                debugOutput << "New file" << std::endl;
                            }),
                        MenuItemData::ActionWithShortcut("📂 Open...", "Ctrl+O", []() {
                                debugOutput << "Open file" << std::endl;
                            }),
                        MenuItemData::Submenu("📁 Recent Files", {
                                MenuItemData::Action("Document1.txt", []() {
                                            debugOutput << "Open Document1.txt" << std::endl;
                                        }),
                                MenuItemData::Action("Project.cpp", []() {
                                            debugOutput << "Open Project.cpp" << std::endl;
                                        }),
                                MenuItemData::Action("Config.json", []() {
                                            debugOutput << "Open Config.json" << std::endl;
                                        })
                            }),
                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("💾 Save", "Ctrl+S", []() {
                                debugOutput << "Save file" << std::endl;
                            }),
                        MenuItemData::ActionWithShortcut("💾 Save As...", "Ctrl+Shift+S", []() {
                                debugOutput << "Save as" << std::endl;
                            }),
                        MenuItemData::Separator(),
                        MenuItemData::ActionWithShortcut("🚪 Exit", "Alt+F4", []() {
                                debugOutput << "Exit application" << std::endl;
                            })
                    })
                .AddSubmenu("Edit", {
                            MenuItemData::ActionWithShortcut("↩️ Undo", "Ctrl+Z", []() {
                                debugOutput << "Undo" << std::endl;
                            }),
                            MenuItemData::ActionWithShortcut("↪️ Redo", "Ctrl+Y", []() {
                                debugOutput << "Redo" << std::endl;
                            }),
                            MenuItemData::Separator(),
                            MenuItemData::ActionWithShortcut("✂️ Cut", "Ctrl+X", []() {
                                debugOutput << "Cut" << std::endl;
                            }),
                            MenuItemData::ActionWithShortcut("📋 Copy", "Ctrl+C", []() {
                                debugOutput << "Copy" << std::endl;
                            }),
                            MenuItemData::ActionWithShortcut("📌 Paste", "Ctrl+V", []() {
                                debugOutput << "Paste" << std::endl;
                            }),
                            MenuItemData::Separator(),
                            MenuItemData::ActionWithShortcut("🔍 Find...", "Ctrl+F", []() {
                                debugOutput << "Find" << std::endl;
                            }),
                            MenuItemData::ActionWithShortcut("🔄 Replace...", "Ctrl+H", []() {
                                debugOutput << "Replace" << std::endl;
                            })
                })
                .AddSubmenu("View", {
                        MenuItemData::Checkbox("🔧 Toolbar", true, [](bool checked) {
                                debugOutput << "Toolbar " << (checked ? "shown" : "hidden") << std::endl;
                            }),
                            MenuItemData::Checkbox("📊 Status Bar", true, [](bool checked) {
                                debugOutput << "Status bar " << (checked ? "shown" : "hidden") << std::endl;
                            }),
                            MenuItemData::Checkbox("📁 Sidebar", false, [](bool checked) {
                                debugOutput << "Sidebar " << (checked ? "shown" : "hidden") << std::endl;
                            }),
                            MenuItemData::Separator(),
                            MenuItemData::Radio("Zoom 50%", 1, false, [](bool checked) {
                                if (checked) debugOutput << "Zoom 50%" << std::endl;
                            }),
                            MenuItemData::Radio("Zoom 100%", 1, true, [](bool checked) {
                                if (checked) debugOutput << "Zoom 100%" << std::endl;
                            }),
                            MenuItemData::Radio("Zoom 150%", 1, false, [](bool checked) {
                                if (checked) debugOutput << "Zoom 150%" << std::endl;
                            })
                })
                .AddSubmenu("Help", {
                            MenuItemData::ActionWithShortcut("📖 Documentation", "F1", []() {
                                debugOutput << "Show documentation" << std::endl;
                            }),
                            MenuItemData::Action("🎓 Tutorials", []() {
                                debugOutput << "Show tutorials" << std::endl;
                            }),
                            MenuItemData::Separator(),
                            MenuItemData::Action("ℹ️ About UltraCanvas", []() {
                                debugOutput << "About UltraCanvas Framework" << std::endl;
                            })
                })
                .Build();

        container->AddChild(mainMenuBar);

        // Dark theme menu
        auto darkMenuBtn = std::make_shared<UltraCanvasButton>("DarkMenuBtn", 115, 20, 225, 170, 35);
        darkMenuBtn->SetText("Dark Theme Menu");
        container->AddChild(darkMenuBtn);

        auto darkMenu = std::make_shared<UltraCanvasMenu>("DarkMenu", 116, 0, 0, 200, 0);
        darkMenu->SetMenuType(MenuType::PopupMenu);
        darkMenu->SetStyle(MenuStyle::Dark());

        darkMenu->AddItem(MenuItemData::Action("🌙 Dark Mode", []() {
            debugOutput << "Dark mode activated" << std::endl;
        }));

        darkMenu->AddItem(MenuItemData::Action("☀️ Light Mode", []() {
            debugOutput << "Light mode activated" << std::endl;
        }));

        darkMenu->AddItem(MenuItemData::Action("🎨 Custom Theme", []() {
            debugOutput << "Custom theme" << std::endl;
        }));

        darkMenuBtn->onClick = [darkMenu, darkMenuBtn, container]() {
            darkMenu->OpenMenu(Point2Di(darkMenuBtn->GetXInWindow(), darkMenuBtn->GetYInWindow() + darkMenuBtn->GetHeight() + 1),
                               *container->GetWindow(), PopupElementSettings());
        };

        // Flat style menu
        auto flatMenuBtn = std::make_shared<UltraCanvasButton>("FlatMenuBtn", 117, 200, 225, 170, 35);
        flatMenuBtn->SetText("Flat Style Menu");
        container->AddChild(flatMenuBtn);

        auto flatMenu = std::make_shared<UltraCanvasMenu>("FlatMenu", 118, 0, 0, 200, 0);
        flatMenu->SetMenuType(MenuType::PopupMenu);
        flatMenu->SetStyle(MenuStyle::Flat());

        flatMenu->AddItem(MenuItemData::Action("📱 Mobile View", []() {
            debugOutput << "Mobile view" << std::endl;
        }));

        flatMenu->AddItem(MenuItemData::Action("💻 Desktop View", []() {
            debugOutput << "Desktop view" << std::endl;
        }));

        flatMenu->AddItem(MenuItemData::Action("Tablet View", GetResourcesDir() + "media/icons/tablet48px.png", []() {
            debugOutput << "Tablet view" << std::endl;
        }));

        flatMenuBtn->onClick = [flatMenu, flatMenuBtn, container]() {
            flatMenu->OpenMenu(Point2Di(flatMenuBtn->GetXInWindow(), flatMenuBtn->GetYInWindow() + flatMenuBtn->GetHeight() + 1),
                               *container->GetWindow(), PopupElementSettings());
        };

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
                debugOutput << "Edit item " << (i + 1) << std::endl;
            }));

            itemMenu->AddItem(MenuItemData::Action("📋 Duplicate", [i]() {
                debugOutput << "Duplicate item " << (i + 1) << std::endl;
            }));

            itemMenu->AddItem(MenuItemData::Action("🗑️ Delete", [i]() {
                debugOutput << "Delete item " << (i + 1) << std::endl;
            }));

            // Set right-click handler
            itemLabel->onClick = [itemMenu, itemLabel, container]() {
                auto ev = UltraCanvasApplication::GetInstance()->GetCurrentEvent();
                if (ev.button == UCMouseButton::Right) {
                    itemMenu->OpenMenu(Point2Di (ev.windowX, ev.windowY), *container->GetWindow(), PopupElementSettings());
                }
            };

            listContainer->AddChild(itemLabel);
        }

        debugOutput << "✓ Menu examples created" << std::endl;
        return container;
    }
} // namespace UltraCanvas