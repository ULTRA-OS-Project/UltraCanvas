// Apps/DemoApp/UltraCanvasSVGExamples.cpp
// Demo examples implementation for UltraCanvas Framework components
// Version: 1.3.0
// Last Modified: 2025-01-02
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasWindow.h"
#include "Plugins/SVG/UltraCanvasSVGPlugin.h"
#include <iostream>
#include <memory>
#include <functional>

namespace UltraCanvas {

// ===== SVG DEMO IMPLEMENTATION =====
    class SVGDemoHandler {
    private:
        std::shared_ptr<UltraCanvasWindow> fullscreenWindow;
        std::string svgFilePath;

    public:
        SVGDemoHandler(const std::string& filePath)
                : svgFilePath(filePath) {}

        void OnSVGClick() {
            if (!fullscreenWindow) {
                CreateFullscreenWindow();
            }
        }

        void CreateFullscreenWindow() {
            // Get screen dimensions (you may need to implement GetScreenDimensions)
            int screenWidth = 1920;  // Default HD resolution
            int screenHeight = 1080;

            // Create fullscreen window configuration
            WindowConfig config;
            config.title = "SVG Fullscreen Viewer";
            config.width = screenWidth;
            config.height = screenHeight;
            config.x = 0;
            config.y = 0;
            config.type = WindowType::Fullscreen;
            config.resizable = false;

            // Create the fullscreen window
            fullscreenWindow = CreateWindow(config);
            fullscreenWindow->SetBackgroundColor(Color(32, 32, 32, 255));

            // Create fullscreen SVG element
            auto fullscreenSVG = std::make_shared<UltraCanvasImageElement>(
                    "FullscreenSVG",
                    10001,
                    0, 0,
                    1900, 1000
            );

            // Load the same SVG file
            if (!svgFilePath.empty()) {
                fullscreenSVG->LoadFromFile(svgFilePath);
            }

            // Add SVG to fullscreen window
            fullscreenWindow->AddChild(fullscreenSVG);

            // Create instruction label
            auto instructionLabel = std::make_shared<UltraCanvasLabel>(
                    "Instructions",
                    10002,
                    10, 10,
                    300, 30
            );
            instructionLabel->SetText("Press ESC to close");
            instructionLabel->SetTextColor(Color(200, 200, 200, 255));
            instructionLabel->SetBackgroundColor(Color(50, 50, 50, 200));
            instructionLabel->SetFontSize(14);
            fullscreenWindow->AddChild(instructionLabel);

            // Setup keyboard event handler for ESC key
            fullscreenWindow->SetEventCallback([this](const UCEvent& event) {
                if ((event.type == UCEventType::KeyUp && event.virtualKey == UCKeys::Escape) || event.type == UCEventType::WindowClose)  {
                    if (fullscreenWindow) {
                        fullscreenWindow->RequestDelete();
                        fullscreenWindow.reset();
                    }
                    return true;
                }
                return false;
            });

            // Show the window
            fullscreenWindow->Show();
        }
    };

// ===== VECTOR/SVG EXAMPLES IMPLEMENTATION =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateSVGVectorExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("VectorExamples", 900, 0, 0, 1000, 780);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("VectorTitle", 901, 10, 10, 500, 30);
        title->SetText("SVG Graphics Demo - Click to View Fullscreen");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // Description
        auto description = std::make_shared<UltraCanvasLabel>("Description", 902, 10, 45, 600, 40);
        description->SetText("Click on the SVG image below to open it in fullscreen mode.\nPress ESC to close the fullscreen view.");
        description->SetFontSize(12);
        description->SetTextColor(Color(80, 80, 80, 255));
        container->AddChild(description);

        // SVG Container with border
        auto svgContainer = std::make_shared<UltraCanvasContainer>("SVGContainer", 903, 20, 100, 240, 240);
        svgContainer->SetBackgroundColor(Color(250, 250, 250, 255));
        svgContainer->SetBorders(2, Color(180, 180, 180, 255));


        // Create SVG Element (200x200 inside the container with padding)
        auto svgElement = std::make_shared<UltraCanvasImageElement>(
                "DemoSVG",
                904,
                20, 20,  // 20px padding inside container
                200, 200
        );

        // Path to SVG file (adjust this path to your actual SVG file location)
        std::string svgFilePath = "media/demo.svg";

        // Try to load from file, fallback to inline SVG if file not found
        if (!svgElement->LoadFromFile(svgFilePath)) {
            // Create a sample inline SVG as fallback
            std::string sampleSVG = R"("
            "<?xml version="1.0" encoding="UTF-8"?>"
            <svg width="200" height="200" xmlns="http://www.w3.org/2000/svg">
                <!-- Background -->
                <rect x="0" y="0" width="200" height="200" fill="#f0f8ff"/>

                <!-- UltraCanvas Logo Placeholder -->
                <g transform=\"translate(100, 100)\">
                                    <!-- Outer circle -->
                                               <circle r="80" fill="none" stroke="#4169e1" stroke-width="4"/>

                                                                                                        <!-- Inner circles -->
                                                                                                                   <circle r="60" fill="none" stroke="#6495ed" stroke-width="3"/>
                                                                                                                                                                            <circle r="40" fill="none" stroke="#87ceeb" stroke-width="2"/>

                                                                                                                                                                                                                                     <!-- Center star -->
                                                                                                                                                                                                                                                 <path d="M 0,-30 L 8.8,9.5 L -28.5,-9.3 L 28.5,-9.3 L -8.8,9.5 Z"
            fill="#4169e1" stroke="#2e5fc7" stroke-width="2"/>

                                                         <!-- Text -->
                                                         <text y="50" text-anchor="middle" font-family="Arial, sans-serif"
            font-size="16" font-weight="bold" fill="#2e5fc7">UltraCanvas</text>
                                                   </g>

                                                     <!-- Corner decorations -->
                                                                 <circle cx="20" cy="20" r="5" fill="#6495ed"/>
                                                                                                    <circle cx="180" cy="20" r="5" fill="#6495ed"/>
                                                                                                                                        <circle cx="20" cy="180" r="5" fill="#6495ed"/>
                                                                                                                                                                            <circle cx="180" cy="180" r="5" fill="#6495ed"/>
                                                                                                                                             </svg>
            )";

            //svgElement->LoadFromString(sampleSVG);
            svgFilePath = ""; // Clear file path since we're using inline SVG
        }

        // Create demo handler for click interaction
        auto demoHandler = std::make_shared<SVGDemoHandler>(svgFilePath);

        // Set click handler on the SVG element
        svgElement->SetEventCallback([demoHandler, svgContainer](const UCEvent& event) {
            switch (event.type) {
                case UCEventType::MouseUp:
                    demoHandler->OnSVGClick();
                    return true;
                case UCEventType::MouseEnter: {
                    svgContainer->SetBordersColor(Color(100, 149, 237, 255));
                    return true;
                }
                case UCEventType::MouseLeave: {
                    svgContainer->SetBordersColor(Color(180, 180, 180, 255));
                    return true;
                }
                default:
                    return false;
            }
        });

        auto svgContainer2 = std::make_shared<UltraCanvasContainer>("SVGContainer", 903, 280, 100, 240, 240);
        svgContainer2->SetBackgroundColor(Color(250, 250, 250, 255));
        svgContainer2->SetBorders(2, Color(180, 180, 180, 255));

        // Create SVG Element (200x200 inside the container with padding)
        auto svgElement2 = std::make_shared<UltraCanvasImageElement>(
                "DemoSVG2",
                904,
                20, 20,  // 20px padding inside container
                200, 200
        );

        // Try to load from file, fallback to inline SVG if file not found
        svgElement2->LoadFromFile("media/robot.svg");
        // Create demo handler for click interaction
        auto demoHandler2 = std::make_shared<SVGDemoHandler>("media/robot.svg");

        // Set click handler on the SVG element
        svgElement2->SetEventCallback([demoHandler2, svgContainer2](const UCEvent& event) {
            switch (event.type) {
                case UCEventType::MouseUp:
                    demoHandler2->OnSVGClick();
                    return true;
                case UCEventType::MouseEnter: {
                    svgContainer2->SetBordersColor(Color(100, 149, 237, 255));
                    return true;
                }
                case UCEventType::MouseLeave: {
                    svgContainer2->SetBordersColor(Color(180, 180, 180, 255));
                    return true;
                }
                default:
                    return false;
            }
        });

        auto svgContainer3 = std::make_shared<UltraCanvasContainer>("SVGContainer", 903, 540, 100, 240, 240);
        svgContainer3->SetBackgroundColor(Color(250, 250, 250, 255));
        svgContainer3->SetBorders(2, Color(180, 180, 180, 255));

        // Create SVG Element (200x200 inside the container with padding)
        auto svgElement3 = std::make_shared<UltraCanvasImageElement>(
                "DemoSVG2",
                904,
                18, 18,  // 20px padding inside container
                200, 200
        );

        // Try to load from file, fallback to inline SVG if file not found
        svgElement3->LoadFromFile("media/astronaut.svg");
        // Create demo handler for click interaction
        auto demoHandler3 = std::make_shared<SVGDemoHandler>("media/astronaut.svg");

        // Set click handler on the SVG element
        svgElement3->SetEventCallback([demoHandler3, svgContainer3](const UCEvent& event) {
            switch (event.type) {
                case UCEventType::MouseUp:
                    demoHandler3->OnSVGClick();
                    return true;
                case UCEventType::MouseEnter: {
                    svgContainer3->SetBordersColor(Color(100, 149, 237, 255));
                    return true;
                }
                case UCEventType::MouseLeave: {
                    svgContainer3->SetBordersColor(Color(180, 180, 180, 255));
                    return true;
                }
                default:
                    return false;
            }
        });

        auto svgContainer4 = std::make_shared<UltraCanvasContainer>("SVGContainer", 903, 20, 360, 240, 240);
        svgContainer4->SetBackgroundColor(Color(250, 250, 250, 255));
        svgContainer4->SetBorders(2, Color(180, 180, 180, 255));

        // Create SVG Element (200x200 inside the container with padding)
        auto svgElement4 = std::make_shared<UltraCanvasImageElement>(
                "DemoSVG2",
                904,
                20, 20,  // 20px padding inside container
                200, 200
        );

        // Try to load from file, fallback to inline SVG if file not found
        svgElement4->LoadFromFile("media/photo-camera.svg");
        // Create demo handler for click interaction
        auto demoHandler4 = std::make_shared<SVGDemoHandler>("media/photo-camera.svg");

        // Set click handler on the SVG element
        svgElement4->SetEventCallback([demoHandler4, svgContainer4](const UCEvent& event) {
            switch (event.type) {
                case UCEventType::MouseUp:
                    demoHandler4->OnSVGClick();
                    return true;
                case UCEventType::MouseEnter: {
                    svgContainer4->SetBordersColor(Color(100, 149, 237, 255));
                    return true;
                }
                case UCEventType::MouseLeave: {
                    svgContainer4->SetBordersColor(Color(180, 180, 180, 255));
                    return true;
                }
                default:
                    return false;
            }
        });

        // Set hover cursor
        //svgElement->SetCursor(CursorType::Hand);

        // Add SVG to container
        svgContainer->AddChild(svgElement);
        svgContainer2->AddChild(svgElement2);
        svgContainer3->AddChild(svgElement3);
        svgContainer4->AddChild(svgElement4);
        container->AddChild(svgContainer);
        container->AddChild(svgContainer2);
        container->AddChild(svgContainer3);
        container->AddChild(svgContainer4);

        // Information panel
        auto infoPanel = std::make_shared<UltraCanvasContainer>("InfoPanel", 905, 540, 360, 320, 320);
        infoPanel->SetBackgroundColor(Color(245, 245, 245, 255));
        infoPanel->SetPadding(15);
        infoPanel->SetBorders(1, Color(200, 200, 200, 255));

        auto infoTitle = std::make_shared<UltraCanvasLabel>("InfoTitle", 906, 10, 10, 250, 25);
        infoTitle->SetText("SVG Features:");
        infoTitle->SetFontSize(14);
        infoTitle->SetFontWeight(FontWeight::Bold);
        infoPanel->AddChild(infoTitle);

        auto infoText = std::make_shared<UltraCanvasLabel>("InfoText", 907, 10, 40, 240, 230);
        infoText->SetText(
                "• Scalable Vector Graphics support\n"
                "• Load from file or string\n"
                "• Auto-resize capability\n"
                "• ViewBox transformation\n"
                "• Basic shape rendering\n"
                "• Path and curve support\n"
                "• Text rendering\n"
                "• Group hierarchies\n"
                "• Style attributes\n"
                "• Click for fullscreen view\n"
                "• ESC to close fullscreen"
        );
        infoText->SetFontSize(12);
        infoText->SetTextColor(Color(60, 60, 60, 255));
        //infoText->SetTextLineHeight(1.5f);
        infoPanel->AddChild(infoText);

        container->AddChild(infoPanel);

        return container;
    }
}