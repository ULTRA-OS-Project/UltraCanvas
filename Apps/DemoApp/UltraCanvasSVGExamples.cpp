// Apps/DemoApp/UltraCanvasSVGExamples.cpp
// Demo examples implementation for UltraCanvas Framework components
// Version: 1.4.0
// Last Modified: 2026-09-13
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
                    10, 10,
                    300, 30
            );
            instructionLabel->SetText("Press ESC to close");
            instructionLabel->SetTextColor(Color(200, 200, 200, 255));
            instructionLabel->SetBackgroundColor(Color(50, 50, 50, 200));
            instructionLabel->SetFontSize(14);
            fullscreenWindow->AddChild(instructionLabel);

            // Setup keyboard event handler for ESC key
            fullscreenWindow->eventCallback = [this](const UCEvent& event) {
                if (event.type == UCEventType::KeyUp && event.virtualKey == UCKeys::Escape) {
                    if (fullscreenWindow) {
                        fullscreenWindow->Close();
                        fullscreenWindow.reset();
                    }
                    return true;
                }
                return false;
            };

            // Show the window
            fullscreenWindow->Show();
        }
    };

// ===== VECTOR/SVG EXAMPLES IMPLEMENTATION =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateSVGVectorExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("VectorExamples", 0, 0, 1000, 780);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("VectorTitle", 10, 10, 500, 30);
        title->SetText("SVG Graphics Demo - Click to View Fullscreen");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // Description
        auto description = std::make_shared<UltraCanvasLabel>("Description", 10, 45, 600, 40);
        description->SetText("Every .svg sample shipped under media/vector/SVG. Click a drawing to open it in fullscreen mode.\nPress ESC to close the fullscreen view.");
        description->SetFontSize(12);
        description->SetTextColor(Color(80, 80, 80, 255));
        container->AddChild(description);

        // Status label for feedback
        auto statusLabel = std::make_shared<UltraCanvasLabel>("SVGStatus", 10, 700, 980, 60);
        statusLabel->SetText("Ready. Click on an SVG file to view it fullscreen.");
        statusLabel->SetFontSize(11);
        statusLabel->SetTextColor(Color(60, 60, 60, 255));
        statusLabel->SetBackgroundColor(Color(230, 230, 230, 255));
        container->AddChild(statusLabel);

        // One tile per sample drawing in media/vector/SVG/.
        auto makeTile = [&](const std::string& id, int x, int y,
                            const std::string& fileName) {
            auto tile = std::make_shared<UltraCanvasContainer>(id, x, y, 240, 240);
            tile->SetBackgroundColor(Color(250, 250, 250, 255));
            tile->SetBorders(2, Color(180, 180, 180, 255));

            auto element = std::make_shared<UltraCanvasImageElement>(id + "El", 20, 15, 200, 175);
            std::string path = NormalizePath(GetResourcesDir() + "media/vector/SVG/" + fileName);
            if (!element->LoadFromFile(path)) {
                statusLabel->SetText("Failed to load " + path);
            }

            auto label = std::make_shared<UltraCanvasLabel>(id + "Label", 10, 198, 220, 24);
            label->SetText(fileName);
            label->SetAlignment(TextAlignment::Center);
            label->SetFontSize(11);
            tile->AddChild(label);

            auto handler = std::make_shared<SVGDemoHandler>(path);
            element->SetEventCallback([handler, tile, statusLabel, path](const UCEvent& event) {
                switch (event.type) {
                    case UCEventType::MouseUp:
                        handler->OnSVGClick();
                        statusLabel->SetText("Opened fullscreen: " + path);
                        return true;
                    case UCEventType::MouseEnter:
                        tile->SetBordersColor(Color(100, 149, 237, 255));
                        return true;
                    case UCEventType::MouseLeave:
                        tile->SetBordersColor(Color(180, 180, 180, 255));
                        return true;
                    default:
                        return false;
                }
            });

            tile->AddChild(element);
            container->AddChild(tile);
        };

        // Every .svg file shipped under media/vector/SVG, four to a row.
        makeTile("SVGTile1", 20, 100, "demo.svg");
        makeTile("SVGTile2", 270, 100, "demo1.svg");
        makeTile("SVGTile3", 520, 100, "demo2.svg");
        makeTile("SVGTile4", 770, 100, "svg-test.svg");
        makeTile("SVGTile5", 20, 350, "robot.svg");
        makeTile("SVGTile6", 270, 350, "astronaut.svg");
        makeTile("SVGTile7", 520, 350, "photo-camera.svg");
        makeTile("SVGTile8", 770, 350, "Logo_Texter.svg");

        // Information panel
        auto infoPanel = std::make_shared<UltraCanvasContainer>("InfoPanel", 20, 600, 960, 92);
        infoPanel->SetBackgroundColor(Color(245, 245, 245, 255));
        infoPanel->SetBorders(1, Color(200, 200, 200, 255));

        auto infoTitle = std::make_shared<UltraCanvasLabel>("InfoTitle", 12, 6, 930, 22);
        infoTitle->SetText("SVG Features:");
        infoTitle->SetFontSize(14);
        infoTitle->SetFontWeight(FontWeight::Bold);
        infoPanel->AddChild(infoTitle);

        auto infoText = std::make_shared<UltraCanvasLabel>("InfoText", 12, 30, 930, 58);
        infoText->SetText(
                "• Load from file or string   • ViewBox transformation   • Auto-resize   • Basic shapes, paths and curves\n"
                "• Text rendering   • Group hierarchies   • Style attributes\n"
                "• Click any drawing for the fullscreen view, ESC to close"
        );
        infoText->SetFontSize(12);
        infoText->SetTextColor(Color(60, 60, 60, 255));
        //infoText->SetTextLineHeight(1.5f);
        infoPanel->AddChild(infoText);

        container->AddChild(infoPanel);

        return container;
    }
}