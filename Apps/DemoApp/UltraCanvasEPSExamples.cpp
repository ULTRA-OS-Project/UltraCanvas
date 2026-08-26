// Apps/DemoApp/UltraCanvasEPSExamples.cpp
// EPS (Encapsulated PostScript) vector graphics demo examples
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasWindow.h"
#include "../Plugins/Vector/EPS/UltraCanvasEPSPlugin.h"
#include <memory>
#include <string>

namespace UltraCanvas {

// ===== EPS DEMO HANDLER =====
    class EPSDemoHandler {
    private:
        std::shared_ptr<UltraCanvasWindow> fullscreenWindow;
        std::string epsFilePath;

    public:
        EPSDemoHandler(const std::string& filePath) : epsFilePath(filePath) {}

        void OnEPSClick() {
            if (!fullscreenWindow) {
                CreateFullscreenWindow();
            }
        }

        void CreateFullscreenWindow() {
            int screenWidth = 1920;
            int screenHeight = 1080;

            WindowConfig config;
            config.title = "EPS Fullscreen Viewer";
            config.width = screenWidth;
            config.height = screenHeight;
            config.x = 0;
            config.y = 0;
            config.type = WindowType::Fullscreen;
            config.resizable = false;

            fullscreenWindow = CreateWindow(config);
            fullscreenWindow->SetBackgroundColor(Color(32, 32, 32, 255));

            auto fullscreenEPS = std::make_shared<UltraCanvasEPSElement>(
                    "FullscreenEPS", 0, 50, screenWidth, screenHeight - 100);
            if (!epsFilePath.empty()) {
                fullscreenEPS->LoadFromFile(epsFilePath);
            }
            fullscreenWindow->AddChild(fullscreenEPS);

            // Zoom buttons
            auto btnZoomOut = std::make_shared<UltraCanvasButton>("BtnZoomOut", 400, 10, 40, 30);
            btnZoomOut->SetText("−");
            btnZoomOut->SetColors(Color(60, 60, 65, 255));
            btnZoomOut->SetTextColors(Colors::White);
            btnZoomOut->onClick = [fullscreenEPS]() {
                fullscreenEPS->SetScale(fullscreenEPS->GetScale() / 1.25f);
            };
            fullscreenWindow->AddChild(btnZoomOut);

            auto btnZoomIn = std::make_shared<UltraCanvasButton>("BtnZoomIn", 450, 10, 40, 30);
            btnZoomIn->SetText("+");
            btnZoomIn->SetColors(Color(60, 60, 65, 255));
            btnZoomIn->SetTextColors(Colors::White);
            btnZoomIn->onClick = [fullscreenEPS]() {
                fullscreenEPS->SetScale(fullscreenEPS->GetScale() * 1.25f);
            };
            fullscreenWindow->AddChild(btnZoomIn);

            auto btnFitPage = std::make_shared<UltraCanvasButton>("BtnFit", 500, 10, 80, 30);
            btnFitPage->SetText("Fit Page");
            btnFitPage->SetColors(Color(60, 60, 65, 255));
            btnFitPage->SetTextColors(Colors::White);
            btnFitPage->onClick = [fullscreenEPS]() {
                fullscreenEPS->SetScale(1.0f);
                fullscreenEPS->SetPreserveAspectRatio(true);
            };
            fullscreenWindow->AddChild(btnFitPage);

            auto instructionLabel = std::make_shared<UltraCanvasLabel>(
                    "Instructions", screenWidth - 200, 10, 190, 30);
            instructionLabel->SetText("Press ESC to close");
            instructionLabel->SetTextColor(Color(200, 200, 200, 255));
            fullscreenWindow->AddChild(instructionLabel);

            fullscreenWindow->SetEventCallback([this](const UCEvent& event) {
                if (event.type == UCEventType::KeyUp && event.virtualKey == UCKeys::Escape) {
                    if (fullscreenWindow) {
                        fullscreenWindow->Close();
                        fullscreenWindow.reset();
                    }
                    return true;
                }
                return false;
            });

            fullscreenWindow->Show();
        }
    };

// ===== EPS VECTOR EXAMPLES IMPLEMENTATION =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateEPSVectorExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("EPSExamples", 0, 0, 1000, 780);
        container->SetBackgroundColor(Color(245, 245, 245, 255));

        auto title = std::make_shared<UltraCanvasLabel>("EPSTitle", 10, 10, 600, 30);
        title->SetText("EPS (PostScript) Graphics Demo - Click to View Fullscreen");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto description = std::make_shared<UltraCanvasLabel>("EPSDescription", 10, 45, 700, 40);
        description->SetText("EPS files are PostScript programs, interpreted by the plugin's built-in interpreter.\nClick a drawing for fullscreen; use the zoom buttons there. Press ESC to close.");
        description->SetFontSize(12);
        description->SetTextColor(Color(80, 80, 80, 255));
        container->AddChild(description);

        auto statusLabel = std::make_shared<UltraCanvasLabel>("EPSStatus", 10, 700, 980, 60);
        statusLabel->SetText("Ready. Click on an EPS file to view.");
        statusLabel->SetFontSize(11);
        statusLabel->SetTextColor(Color(60, 60, 60, 255));
        statusLabel->SetBackgroundColor(Color(230, 230, 230, 255));
        container->AddChild(statusLabel);

        // One tile per sample drawing in media/eps/
        auto makeTile = [&](const std::string& id, int x, int y,
                            const std::string& fileName) {
            auto tile = std::make_shared<UltraCanvasContainer>(id, x, y, 300, 280);
            tile->SetBackgroundColor(Colors::White);
            tile->SetBorders(2, Color(180, 180, 180, 255));

            auto element = std::make_shared<UltraCanvasEPSElement>(id + "El", 10, 10, 280, 220);
            std::string path = NormalizePath(GetResourcesDir() + "media/eps/" + fileName);
            if (element->LoadFromFile(path)) {
                statusLabel->SetText("Loaded: " + path);
            } else {
                statusLabel->SetText("Failed to load " + path + ": " + element->GetLastError());
            }

            auto label = std::make_shared<UltraCanvasLabel>(id + "Label", 10, 240, 280, 30);
            label->SetText(fileName);
            label->SetAlignment(TextAlignment::Center);
            label->SetFontSize(11);
            tile->AddChild(label);

            auto handler = std::make_shared<EPSDemoHandler>(path);
            element->SetEventCallback([handler, tile, statusLabel, path](const UCEvent& event) {
                switch (event.type) {
                    case UCEventType::MouseUp:
                        handler->OnEPSClick();
                        statusLabel->SetText("Opened fullscreen: " + path);
                        return true;
                    case UCEventType::MouseEnter:
                        tile->SetBordersColor(Color(0, 122, 204, 255));
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

        makeTile("EPSContainer1", 20, 100, "demo.eps");
        makeTile("EPSContainer2", 340, 100, "gears.eps");

        // ===== INFO PANEL =====
        auto infoContainer = std::make_shared<UltraCanvasContainer>("InfoPanel", 660, 100, 300, 280);
        infoContainer->SetBackgroundColor(Color(240, 248, 255, 255));
        infoContainer->SetBorders(2, Color(100, 149, 237, 255));

        auto infoTitle = std::make_shared<UltraCanvasLabel>("InfoTitle", 10, 10, 280, 25);
        infoTitle->SetText("EPS Plugin Features");
        infoTitle->SetFontWeight(FontWeight::Bold);
        infoTitle->SetFontSize(13);
        infoContainer->AddChild(infoTitle);

        auto infoText = std::make_shared<UltraCanvasLabel>("InfoText", 10, 40, 280, 200);
        infoText->SetText(
                "✓ PostScript interpreter (level 1/2 core)\n"
                "✓ Procedures, loops, dictionaries\n"
                "✓ Paths, arcs, beziers, clipping\n"
                "✓ Gray / RGB / CMYK / HSB color\n"
                "✓ Dashes, caps, joins, transforms\n"
                "✓ Sampled images (hex data)\n"
                "✓ Text via mapped system fonts\n"
                "✓ DOS EPS binary preview headers\n"
        );
        infoText->SetFontSize(11);
        infoText->SetTextColor(Color(50, 50, 50, 255));
        infoContainer->AddChild(infoText);

        container->AddChild(infoContainer);

        return container;
    }

} // namespace UltraCanvas
