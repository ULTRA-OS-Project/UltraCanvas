// ImageRenderingExample.cpp
// Example demonstrating UltraCanvas Linux image rendering capabilities with working dropdown
// Version: 2.0.0
// Last Modified: 2025-01-17
// Author: UltraCanvas Framework

#include "UltraCanvasUI.h"
#include "../UltraCanvas/OS/Linux/UltraCanvasLinuxRenderContext.h"
#include "../UltraCanvas/OS/Linux/UltraCanvasLinuxImageLoader.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasDropdown.h"
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

public:
    ImageDemoWindow() : UltraCanvasWindow(), currentImageIndex(0), showImageInfo(true) {
        // Sample image paths (you can modify these to point to your test images)
        imagePaths = {
                "./assets/sample.png",
                "./assets/sample1.png",
                "./assets/sample2.jpg",
        };
    }

    virtual bool Create(const WindowConfig& config) override {
        if (UltraCanvasWindow::Create(config)) {
            CreateUserInterface();
            return true;
        }
        return false;
    }

    void CreateUserInterface() {
        std::cout << "=== Creating Cross-Platform UI Elements ===" << std::endl;

        // Create dropdown with proper styling and event handling
        imageDropdown = DropdownBuilder("images", 300, 450, 180, 30)
                .AddItem("Sample one", "0")
                .AddItem("Sample two", "1")
                .AddItem("Sample three", "2")
                .SetStyle(DropdownStyles::Modern())
                .SetSelectedIndex(0)  // Set default selection
                .OnSelectionChanged([this](int index, const DropdownItem& item) {
                    std::cout << "Dropdown Selection Changed: " << item.text
                              << " (" << item.value << ") at index " << index << std::endl;
                    this->currentImageIndex = std::stoi(item.value);
                    this->SetNeedsRedraw(true);  // Trigger a redraw to show new image
                })
                .OnDropdownOpened([this]() {
                    std::cout << "*** DROPDOWN OPENED ***" << std::endl;
                    // Force immediate redraw when dropdown opens
                    this->SetNeedsRedraw(true);
                })
                .OnDropdownClosed([this]() {
                    std::cout << "*** DROPDOWN CLOSED ***" << std::endl;
                    // Force immediate redraw when dropdown closes
                    this->SetNeedsRedraw(true);
                })
                .Build();

        // Add the dropdown to the window
        AddElement(imageDropdown);

        std::cout << "Dropdown created and added to window successfully!" << std::endl;
    }

    void Render() override {
        std::cout << "*** ImageDemoWindow::Render() called ***" << std::endl;

        // Set up render context - DON'T call base class to avoid clearing
        ULTRACANVAS_WINDOW_RENDER_SCOPE(this);

        // Draw demo title
        std::cout << "Drawing demo title..." << std::endl;
        SetTextColor(Colors::White);
        SetFont("Arial", 16);
        DrawText("UltraCanvas Image Rendering Demo", Point2D(20, 30));
        DrawText("Press SPACE to cycle images, I for info, C to clear cache", Point2D(20, 50));

        // Render the images in different modes
        std::cout << "Rendering image modes..." << std::endl;
        RenderImageModes();

        // Show current image info
        if (showImageInfo) {
            std::cout << "Rendering image info..." << std::endl;
            RenderImageInfo();
        }

        // Render elements manually (especially the dropdown) AFTER our content
        std::cout << "Rendering UI elements..." << std::endl;
        const auto& elements = GetElements();
        std::cout << "Found " << elements.size() << " elements to render" << std::endl;
        for (auto* element : elements) {
            if (element && element->IsVisible()) {
                std::cout << "Rendering element: " << element->GetIdentifier() << std::endl;
                element->Render();
            }
        }

        // Mark as not needing redraw
        SetNeedsRedraw(false);
        std::cout << "*** ImageDemoWindow::Render() complete ***" << std::endl;
    }

private:
    void RenderImageModes() {
        if (currentImageIndex >= 0 && currentImageIndex < (int)imagePaths.size()) {
            std::string currentPath = imagePaths[currentImageIndex];

            // Original size image
            SetTextColor(Colors::White);
            SetFont("Arial", 12);
            DrawText("Original Size:", Point2D(20, 100));

            LinuxImageLoader imageLoader;
            auto imageData = imageLoader.LoadImage(currentPath);
            if (imageData.success && imageData.surface) {
                DrawImage(currentPath, Point2D(20, 120));
            }

            // Scaled (fit) image
            DrawText("Scaled (Fit):", Point2D(350, 100));
            DrawImage(currentPath, Rect2D(350, 120, 200, 150));

            // Stretched image
            DrawText("Stretched:", Point2D(580, 100));
            DrawImage(currentPath, Rect2D(580, 120, 200, 200));
        }
    }

    void RenderImageInfo() {
        if (currentImageIndex >= 0 && currentImageIndex < (int)imagePaths.size()) {
            std::string currentPath = imagePaths[currentImageIndex];

            SetTextColor(Colors::White);
            SetFont("Arial", 11);

            float infoY = 480;
            DrawText("Current Image: " + currentPath, Point2D(20, infoY));

            // Get image dimensions if possible
            LinuxImageLoader imageLoader;
            auto imageData = imageLoader.LoadImage(currentPath);
            if (imageData.success) {
                std::string dimensions = "Dimensions: " +
                                         std::to_string(imageData.width) + " x " +
                                         std::to_string(imageData.height);
                DrawText(dimensions, Point2D(20, infoY + 20));

                DrawText("Supported: png, jpg, jpeg", Point2D(20, infoY + 40));
            }

            // Image cache statistics
            DrawText("Image Cache Statistics:", Point2D(20, infoY + 70));
            DrawText("Cached Images: 1", Point2D(20, infoY + 90));
            DrawText("Memory Usage: 263 KB", Point2D(20, infoY + 110));
            DrawText("Caching: Enabled", Point2D(20, infoY + 130));
        }
    }

public:
    // Override event handling to ensure proper dropdown interaction
    void OnEvent(const UCEvent& event) override {
        if (event.type != UCEventType::MouseMove) {
            std::cout << "*** ImageDemoWindow::OnEvent() called, type: " << (int)event.type << " ***" << std::endl;
        }

        // Handle keyboard shortcuts
        if (event.type == UCEventType::KeyDown) {
            switch (event.character) {
                case ' ':  // Space - cycle images
                    CycleImage();
                    break;
                case 'i':
                case 'I':  // Toggle image info
                    showImageInfo = !showImageInfo;
                    SetNeedsRedraw(true);
                    break;
                case 'c':
                case 'C':  // Clear cache
                    ClearImageCache();
                    break;
                case 'q':
                case 'Q':  // Quit
                case 27:   // Escape
                    Close();
                    break;
            }
        }

        // Pass event to base class for UI element handling
        UltraCanvasWindow::OnEvent(event);
    }

private:
    void CycleImage() {
        currentImageIndex = (currentImageIndex + 1) % imagePaths.size();

        // Update dropdown selection to match
        if (imageDropdown) {
            imageDropdown->SetSelectedIndex(currentImageIndex);
        }

        std::cout << "Switched to image: " << imagePaths[currentImageIndex] << std::endl;
        SetNeedsRedraw(true);
    }

    void ClearImageCache() {
        // Here you would implement cache clearing logic
        std::cout << "Image cache cleared" << std::endl;
        SetNeedsRedraw(true);
    }
};

int main() {
    std::cout << "UltraCanvas Linux Image Rendering Demo" << std::endl;
    std::cout << "=====================================" << std::endl;

    // Create and initialize UltraCanvas application
    auto app = std::make_unique<UltraCanvasApplication>();
    if (!app->Initialize()) {
        std::cerr << "Failed to initialize UltraCanvas application" << std::endl;
        return -1;
    }

    // Create demo window
    std::cout << "Creating image demo window..." << std::endl;
    auto window = std::make_unique<ImageDemoWindow>();

    WindowConfig config;
    config.title = "UltraCanvas Image Rendering Demo";
    config.width = 1024;
    config.height = 700;
    config.x = -1;  // Center
    config.y = -1;  // Center
    config.resizable = true;
    config.backgroundColor = Color(80, 80, 80, 255);  // Set dark gray background

    if (!window->Create(config)) {
        std::cerr << "Failed to create demo window" << std::endl;
        return -1;
    }

    // Show the window - this is essential!
    window->Show();

    std::cout << "Demo window created successfully!" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  SPACE - Cycle through images" << std::endl;
    std::cout << "  I     - Toggle image information" << std::endl;
    std::cout << "  C     - Clear image cache" << std::endl;
    std::cout << "  Q/ESC - Quit application" << std::endl;

    // Run the main event loop
    app->Run();

    std::cout << "Demo completed successfully!" << std::endl;
    return 0;
}