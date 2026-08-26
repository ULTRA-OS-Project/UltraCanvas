// Apps/DemoApp/UltraCanvasXARExamples.cpp
// Xara (.xar) vector graphics demo examples for UltraCanvas Framework
// Version: 1.1.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasWindow.h"
#include "../Plugins/Vector/XAR/UltraCanvasXARPlugin.h"
#include <memory>
#include <string>

namespace UltraCanvas {

// ===== XAR DEMO HANDLER =====
    class XARDemoHandler {
    private:
        std::shared_ptr<UltraCanvasWindow> fullscreenWindow;
        std::string xarFilePath;

    public:
        XARDemoHandler(const std::string& filePath) : xarFilePath(filePath) {}

        void OnXARClick() {
            if (!fullscreenWindow) {
                CreateFullscreenWindow();
            }
        }

        void CreateFullscreenWindow() {
            int screenWidth = 1920;
            int screenHeight = 1080;

            WindowConfig config;
            config.title = "XAR Fullscreen Viewer";
            config.width = screenWidth;
            config.height = screenHeight;
            config.x = 0;
            config.y = 0;
            config.type = WindowType::Fullscreen;
            config.resizable = false;

            fullscreenWindow = CreateWindow(config);
            fullscreenWindow->SetBackgroundColor(Color(32, 32, 32, 255));

            auto fullscreenXAR = std::make_shared<UltraCanvasXARElement>(
                    "FullscreenXAR", 0, 50, screenWidth, screenHeight - 100);
            if (!xarFilePath.empty()) {
                fullscreenXAR->LoadFromFile(xarFilePath);
            }
            fullscreenWindow->AddChild(fullscreenXAR);

            // Page navigation (multi-page documents render one spread at a time)
            auto btnPrev = std::make_shared<UltraCanvasButton>("BtnPrev", 10, 10, 80, 30);
            btnPrev->SetText("◀ Prev");
            btnPrev->SetColors(Color(60, 60, 65, 255));
            btnPrev->SetTextColors(Colors::White);
            btnPrev->onClick = [fullscreenXAR]() {
                if (fullscreenXAR->IsLoaded()) {
                    fullscreenXAR->SetCurrentPage(fullscreenXAR->GetCurrentPage() - 1);
                }
            };
            fullscreenWindow->AddChild(btnPrev);

            auto btnNext = std::make_shared<UltraCanvasButton>("BtnNext", 100, 10, 80, 30);
            btnNext->SetText("Next ▶");
            btnNext->SetColors(Color(60, 60, 65, 255));
            btnNext->SetTextColors(Colors::White);
            btnNext->onClick = [fullscreenXAR]() {
                if (fullscreenXAR->IsLoaded()) {
                    fullscreenXAR->SetCurrentPage(fullscreenXAR->GetCurrentPage() + 1);
                }
            };
            fullscreenWindow->AddChild(btnNext);

            auto pageLabel = std::make_shared<UltraCanvasLabel>("PageLabel", 200, 10, 150, 30);
            pageLabel->SetTextColor(Colors::White);
            if (fullscreenXAR->IsLoaded()) {
                pageLabel->SetText("Page 1/" + std::to_string(fullscreenXAR->GetPageCount()));
            }
            fullscreenWindow->AddChild(pageLabel);

            fullscreenXAR->onPageChanged = [pageLabel, fullscreenXAR](int page) {
                pageLabel->SetText("Page " + std::to_string(page + 1) + "/" +
                                   std::to_string(fullscreenXAR->GetPageCount()));
            };

            // Zoom buttons
            auto btnZoomOut = std::make_shared<UltraCanvasButton>("BtnZoomOut", 400, 10, 40, 30);
            btnZoomOut->SetText("−");
            btnZoomOut->SetColors(Color(60, 60, 65, 255));
            btnZoomOut->SetTextColors(Colors::White);
            btnZoomOut->onClick = [fullscreenXAR]() {
                fullscreenXAR->SetScale(fullscreenXAR->GetScale() / 1.25f);
            };
            fullscreenWindow->AddChild(btnZoomOut);

            auto btnZoomIn = std::make_shared<UltraCanvasButton>("BtnZoomIn", 450, 10, 40, 30);
            btnZoomIn->SetText("+");
            btnZoomIn->SetColors(Color(60, 60, 65, 255));
            btnZoomIn->SetTextColors(Colors::White);
            btnZoomIn->onClick = [fullscreenXAR]() {
                fullscreenXAR->SetScale(fullscreenXAR->GetScale() * 1.25f);
            };
            fullscreenWindow->AddChild(btnZoomIn);

            auto btnFitPage = std::make_shared<UltraCanvasButton>("BtnFit", 500, 10, 80, 30);
            btnFitPage->SetText("Fit Page");
            btnFitPage->SetColors(Color(60, 60, 65, 255));
            btnFitPage->SetTextColors(Colors::White);
            btnFitPage->onClick = [fullscreenXAR]() {
                fullscreenXAR->SetScale(1.0f);
                fullscreenXAR->SetPreserveAspectRatio(true);
            };
            fullscreenWindow->AddChild(btnFitPage);

            // Instructions label
            auto instructionLabel = std::make_shared<UltraCanvasLabel>(
                    "Instructions", screenWidth - 200, 10, 190, 30);
            instructionLabel->SetText("Press ESC to close");
            instructionLabel->SetTextColor(Color(200, 200, 200, 255));
            fullscreenWindow->AddChild(instructionLabel);

            // ESC key handler
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

// ===== XAR VECTOR EXAMPLES IMPLEMENTATION =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateXARVectorExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("XARExamples", 0, 0, 1000, 780);
        container->SetBackgroundColor(Color(245, 245, 245, 255));

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("XARTitle", 10, 10, 600, 30);
        title->SetText("Xara (.xar) Graphics Demo - Click to View Fullscreen");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // Description
        auto description = std::make_shared<UltraCanvasLabel>("XARDescription", 10, 45, 700, 40);
        description->SetText("Click a drawing to open it in fullscreen; use the page buttons for multi-page documents\nand the zoom buttons to inspect details. Press ESC to close the fullscreen view.");
        description->SetFontSize(12);
        description->SetTextColor(Color(80, 80, 80, 255));
        container->AddChild(description);

        // Status label for feedback
        auto statusLabel = std::make_shared<UltraCanvasLabel>("XARStatus", 10, 700, 980, 60);
        statusLabel->SetText("Ready. Click on a XAR file to view.");
        statusLabel->SetFontSize(11);
        statusLabel->SetTextColor(Color(60, 60, 60, 255));
        statusLabel->SetBackgroundColor(Color(230, 230, 230, 255));
        container->AddChild(statusLabel);

        // One tile per sample drawing in media/xar/
        auto makeTile = [&](const std::string& id, int x, int y,
                            const std::string& fileName) {
            auto tile = std::make_shared<UltraCanvasContainer>(id, x, y, 300, 280);
            tile->SetBackgroundColor(Colors::White);
            tile->SetBorders(2, Color(180, 180, 180, 255));

            auto element = std::make_shared<UltraCanvasXARElement>(id + "El", 10, 10, 280, 220);
            std::string path = NormalizePath(GetResourcesDir() + "media/xar/" + fileName);
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

            auto handler = std::make_shared<XARDemoHandler>(path);
            element->SetEventCallback([handler, tile, statusLabel, path](const UCEvent& event) {
                switch (event.type) {
                    case UCEventType::MouseUp:
                        handler->OnXARClick();
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

        makeTile("XARContainer1", 20, 100, "demo.xar");
        makeTile("XARContainer2", 340, 100, "backside.xar");

        // ===== INFO PANEL =====
        auto infoContainer = std::make_shared<UltraCanvasContainer>("InfoPanel", 660, 100, 300, 280);
        infoContainer->SetBackgroundColor(Color(240, 248, 255, 255));
        infoContainer->SetBorders(2, Color(100, 149, 237, 255));

        auto infoTitle = std::make_shared<UltraCanvasLabel>("InfoTitle", 10, 10, 280, 25);
        infoTitle->SetText("XAR Plugin Features");
        infoTitle->SetFontWeight(FontWeight::Bold);
        infoTitle->SetFontSize(13);
        infoContainer->AddChild(infoTitle);

        auto infoText = std::make_shared<UltraCanvasLabel>("InfoText", 10, 40, 280, 200);
        infoText->SetText(
                "✓ Xara .xar drawings (compressed too)\n"
                "✓ Paths, QuickShapes, groups, layers\n"
                "✓ Flat, gradient and bitmap fills\n"
                "✓ Contone fills and soft shadows\n"
                "✓ Text: fonts, justification, lists\n"
                "✓ Embedded bitmaps with transparency\n"
                "✓ Multi-page documents (spreads)\n"
                "✓ Zoom and fullscreen viewing\n"
        );
        infoText->SetFontSize(11);
        infoText->SetTextColor(Color(50, 50, 50, 255));
        infoContainer->AddChild(infoText);

        container->AddChild(infoContainer);

        return container;
    }

} // namespace UltraCanvas
