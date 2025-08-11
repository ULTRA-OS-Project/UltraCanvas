// ImageRenderingExample.cpp
// Example demonstrating UltraCanvas Linux image rendering capabilities with menu system
// Version: 2.1.1
// Last Modified: 2025-01-08
// Author: UltraCanvas Framework

#include "UltraCanvasUI.h"
#include "../UltraCanvas/OS/Linux/UltraCanvasLinuxRenderContext.h"
#include "../UltraCanvas/OS/Linux/UltraCanvasLinuxImageLoader.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasMenu.h"
#include <iostream>
#include <vector>
#include <string>
#include <memory>

using namespace UltraCanvas;

class ImageDemoWindow : public UltraCanvasWindow {
private:
    std::vector<std::string> imagePaths;
    int currentImageIndex;
    bool showImageInfo;
    std::shared_ptr<UltraCanvasDropdown> imageDropdown;
    std::shared_ptr<UltraCanvasMenu> mainMenuBar;

public:
    ImageDemoWindow() : UltraCanvasWindow(), currentImageIndex(0), showImageInfo(true) {
        // Sample image paths (you can modify these to point to your test images)
        imagePaths = {
                "./assets/sample.png",
                "./assets/sample1.png",
                "./assets/sample2.jpg",
                "./assets/sample3.png",
                "./assets/sample4.png",
                "./assets/sample5.png",
                "./assets/sample6.png",
                "./assets/sample7.jpg",
        };
    }

    virtual bool Create(const WindowConfig& config) override {
        if (UltraCanvasWindow::Create(config)) {
            CreateMenuSystem();
            CreateUserInterface();
            return true;
        }
        return false;
    }

    void SelectImage(int index) {
        std::cout << "Selected image " << (index + 1) << " from menu" << std::endl;
        if (index >= 0 && index < (int)imagePaths.size()) {
            currentImageIndex = index;

            // Update dropdown to match
            if (imageDropdown) {
                imageDropdown->SetSelectedIndex(index);
            }

            SetNeedsRedraw(true);
        }
    }

    void ShowAboutDialog() {
        std::cout << "=== UltraCanvas Image Demo ===" << std::endl;
        std::cout << "Version: 2.1.0" << std::endl;
        std::cout << "A demonstration of cross-platform image rendering" << std::endl;
        std::cout << "with menu system and UI controls." << std::endl;
        SetNeedsRedraw(true);
    }

    void ShowShortcutsDialog() {
        std::cout << "=== Keyboard Shortcuts ===" << std::endl;
        std::cout << "SPACE       - Cycle through images" << std::endl;
        std::cout << "I           - Toggle image information" << std::endl;
        std::cout << "C           - Clear image cache" << std::endl;
        std::cout << "Q/ESC       - Quit application" << std::endl;
        std::cout << "F1          - Show keyboard shortcuts" << std::endl;
        std::cout << "Alt+F4      - Exit application" << std::endl;
        SetNeedsRedraw(true);
    }

    void CreateMenuSystem() {
        std::cout << "=== Creating Unified Menu System ===" << std::endl;

        // Create main menu bar with proper height (32px) and ensure horizontal orientation
        mainMenuBar = MenuBuilder("main_menu", 3000, 0, 0, 800, 32)
                .SetType(MenuType::MainMenu)
                .Build();

        // Ensure the main menu bar uses horizontal orientation
        mainMenuBar->SetOrientation(MenuOrientation::Horizontal);

        // Apply proper style for main menu
        MenuStyle mainMenuStyle = MenuStyle::Default();
        mainMenuStyle.itemHeight = 24.0f;  // Standard menu bar height
        mainMenuStyle.paddingTop = 4.0f;
        mainMenuStyle.paddingBottom = 4.0f;
        mainMenuStyle.backgroundColor = Color(248, 248, 248, 255);  // Light gray background
        mainMenuBar->SetStyle(mainMenuStyle);

        // Create Images submenu items
        std::vector<MenuItemData> imagesMenuItems;
        for (int i = 0; i < 4; ++i) {
            MenuItemData item = MenuItemData::Action(
                    "Example " + std::to_string(i + 1),
                    [this, i]() { SelectImage(i); }
            );
            item.iconPath = "./assets/icons/image" + std::to_string(i + 1) + ".png";
            imagesMenuItems.push_back(item);
        }

        // Create Help submenu items
        std::vector<MenuItemData> helpMenuItems;
        {
            MenuItemData aboutItem = MenuItemData::Action("About", [this]() {
                ShowAboutDialog();
            });
            aboutItem.iconPath = "./assets/icons/about.png";
            helpMenuItems.push_back(aboutItem);
        }

        {
            MenuItemData shortcutsItem = MenuItemData::Action("Keyboard Shortcuts", [this]() {
                ShowShortcutsDialog();
            });
            shortcutsItem.iconPath = "./assets/icons/keyboard.png";
            shortcutsItem.shortcut = "F1";
            helpMenuItems.push_back(shortcutsItem);
        }

        // Build the unified main menu using MenuBuilder::AddSubmenu
        mainMenuBar = MenuBuilder("main_menu", 3000, 0, 0, 800, 32)
                .SetType(MenuType::MainMenu)

                        // Add File submenu with Images submenu and Exit
                .AddSubmenu("File", {
                        MenuItemData::Submenu("Images", "./assets/icons/images.png", imagesMenuItems),
                        MenuItemData::Separator(),
                        MenuItemData::Action("Exit", "./assets/icons/exit.png",  [this]() { Close(); })
                })

                        // Add Help submenu
                .AddSubmenu("Help", helpMenuItems)

                .Build();

        // Apply style and orientation
        mainMenuBar->SetOrientation(MenuOrientation::Horizontal);
        mainMenuBar->SetStyle(mainMenuStyle);

        // Add menu to window
        mainMenuBar->Show();
        AddElement(mainMenuBar);

        std::cout << "Unified menu system created successfully!" << std::endl;
    }

    void CreateUserInterface() {
        std::cout << "=== Creating Cross-Platform UI Elements ===" << std::endl;

        // Create dropdown with proper styling and event handling (positioned below menu bar)
        imageDropdown = DropdownBuilder("images_dropdown", 700, 70, 180, 30)
                .AddItem("Sample one", "0")
                .AddItem("Sample two", "1")
                .AddItem("Sample three", "2")
                .AddItem("Sample 4", "3")
                .AddItem("Sample 5", "4")
                .AddItem("Sample 6", "5")
                .AddItem("Sample 7", "6")
                .AddItem("Sample 8", "7")
                .SetStyle(DropdownStyles::Modern())
                .SetSelectedIndex(0)
                .OnSelectionChanged([this](int index, const DropdownItem& item) {
                    std::cout << "Dropdown Selection Changed: " << item.text
                              << " (" << item.value << ") at index " << index << std::endl;
                    this->currentImageIndex = std::stoi(item.value);
                    this->SetNeedsRedraw(true);
                })
                .OnDropdownOpened([this]() {
                    std::cout << "*** DROPDOWN OPENED ***" << std::endl;
                    this->SetNeedsRedraw(true);
                })
                .OnDropdownClosed([this]() {
                    std::cout << "*** DROPDOWN CLOSED ***" << std::endl;
                    this->SetNeedsRedraw(true);
                })
                .Build();

        // Add the dropdown to the window
        AddElement(imageDropdown);

        std::cout << "UI elements created and added to window successfully!" << std::endl;
    }

    virtual void RenderCustomContent() override {
        auto context = GetRenderContext();
        if (!context) return;

        // Draw demo title
        std::cout << "Drawing demo title..." << std::endl;
        context->SetTextColor(Colors::White);
        context->SetFont("Arial", 24.0f);
        context->DrawText("UltraCanvas Linux Image Rendering Demo", Point2D(20, 60));

        context->SetFont("Arial", 14.0f);
        context->DrawText("Use dropdown or menu to select images", Point2D(20, 90));

        // Render current image with different modes
        std::cout << "Rendering image modes..." << std::endl;
        if (currentImageIndex >= 0 && currentImageIndex < (int)imagePaths.size()) {
            const std::string& imagePath = imagePaths[currentImageIndex];

            // Normal rendering
            context->DrawImage(imagePath, Rect2D(50, 150, 200, 200));

            // Scaled rendering
            context->DrawImage(imagePath, Rect2D(300, 150, 100, 100));

            // Stretched rendering
            context->DrawImage(imagePath, Rect2D(450, 150, 250, 150));

            // Tiled rendering (simulated with multiple draws)
            for (int y = 0; y < 2; ++y) {
                for (int x = 0; x < 3; ++x) {
                    context->DrawImage(imagePath,
                                       Rect2D(750 + x * 50, 150 + y * 50, 50, 50));
                }
            }
        }

        // Show image info if enabled
        if (showImageInfo) {
            std::cout << "Rendering image info..." << std::endl;
            context->SetStrokeColor(Color(200, 200, 200, 255));
            context->SetStrokeWidth(1.0f);
            context->DrawRectangle(Rect2D(50, 380, 900, 100));

            context->SetTextColor(Colors::White);
            context->SetFont("Arial", 12.0f);
            context->DrawText("Image Information:", Point2D(60, 400));

            if (currentImageIndex >= 0 && currentImageIndex < (int)imagePaths.size()) {
                std::string info = "Current: " + imagePaths[currentImageIndex];
                context->DrawText(info, Point2D(60, 420));
                context->DrawText("Index: " + std::to_string(currentImageIndex + 1) +
                                  " of " + std::to_string(imagePaths.size()), Point2D(60, 440));
                context->DrawText("Press SPACE to cycle, I to toggle info", Point2D(60, 460));
            }
        }
    }

    virtual bool OnEvent(const UCEvent& event) override {
        std::cout << "*** ImageDemoWindow::OnEvent() called, type: " << (int)event.type
                  << " pos: (" << event.x << "," << event.y << ") ***" << std::endl;

        // Handle keyboard shortcuts
        if (event.type == UCEventType::KeyDown) {
            std::cout << "KeyDown event: virtualKey=" << (int)event.virtualKey << std::endl;

            switch (event.virtualKey) {
                case UCKeys::Space:
                    // Cycle through images
                    currentImageIndex = (currentImageIndex + 1) % imagePaths.size();
                    if (imageDropdown) {
                        imageDropdown->SetSelectedIndex(currentImageIndex);
                    }
                    SetNeedsRedraw(true);
                    return true;

                case UCKeys::Escape:
                    // Quit application
                    Close();
                    return true;

                case UCKeys::F1:
                    // Show keyboard shortcuts
                    ShowShortcutsDialog();
                    return true;

                default:
                    break;
            }

            // Check for Alt+F4
            if (event.alt && event.virtualKey == UCKeys::F4) {
                Close();
                return true;
            }
            return false;
        }

        // Forward event to base class for UI element handling
        std::cout << "*** Forwarding event to base class for UI handling ***" << std::endl;
        return UltraCanvasWindow::OnEvent(event);
        std::cout << "*** Event handling complete ***" << std::endl;
    }
};

// Main application entry point
int main(int argc, char* argv[]) {
    std::cout << "=== UltraCanvas Linux Image Rendering Demo with Menu System ===" << std::endl;

    try {
        // Create application instance
        auto app = UltraCanvasApplication::GetInstance();
        if (!app) {
            std::cerr << "Failed to create application instance!" << std::endl;
            return -1;
        }

        // Initialize the application
        if (!app->Initialize()) {
            std::cerr << "Failed to initialize application!" << std::endl;
            return -1;
        }

        // Create the demo window
        std::cout << "Creating main demo window..." << std::endl;
        auto demoWindow = std::make_shared<ImageDemoWindow>();

        WindowConfig config;
        config.title = "UltraCanvas Linux Image Rendering Demo";
        config.width = 1024;
        config.height = 700;
        config.backgroundColor = Color(80, 80, 80, 255);  // Dark gray background
        config.resizable = true;

        if (!demoWindow->Create(config)) {
            std::cerr << "Failed to create demo window!" << std::endl;
            return -1;
        }

        // Show the window and start the main loop
        demoWindow->Show();
        std::cout << "Demo window created and shown. Starting main loop..." << std::endl;

        // Run the application main loop
        app->Run();

        std::cout << "Application finished successfully." << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Exception in main: " << e.what() << std::endl;
        return -1;
    } catch (...) {
        std::cerr << "Unknown exception in main!" << std::endl;
        return -1;
    }
}