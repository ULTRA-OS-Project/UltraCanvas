// Apps/DemoApp/UltraCanvasXARExamples.cpp
// xar vector graphics demo examples for UltraCanvas Framework
// Version: 1.0.1
// Last Modified: 2026-05-01
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasWindow.h"
#include "../Plugins/Vector/XAR/UltraCanvasXARPlugin.h"
#include <iostream>
#include <memory>

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

            // Create fullscreen XAR element
            auto fullscreenXAR = std::make_shared<UltraCanvasXARElement>(
                    "FullscreenXAR", 20001, 0, 50, screenWidth, screenHeight - 100);
            //fullscreenXAR->CenterDocument();

            if (!xarFilePath.empty()) {
                fullscreenXAR->LoadFromFile(xarFilePath);
            }

            fullscreenWindow->AddChild(fullscreenXAR);

            // Zoom buttons
            auto btnZoomOut = std::make_shared<UltraCanvasButton>("BtnZoomOut", 20005, 400, 10, 40, 30);
            btnZoomOut->SetText("−");
            btnZoomOut->SetColors(Color(60, 60, 65, 255));
            btnZoomOut->SetTextColors(Colors::White);
            btnZoomOut->onClick = [fullscreenXAR]() {
                fullscreenXAR->SetScale(fullscreenXAR->GetScale() / 1.25f);
            };
            fullscreenWindow->AddChild(btnZoomOut);

            auto btnZoomIn = std::make_shared<UltraCanvasButton>("BtnZoomIn", 20006, 450, 10, 40, 30);
            btnZoomIn->SetText("+");
            btnZoomIn->SetColors(Color(60, 60, 65, 255));
            btnZoomIn->SetTextColors(Colors::White);
            btnZoomIn->onClick = [fullscreenXAR]() {
                fullscreenXAR->SetScale(fullscreenXAR->GetScale() * 1.25f);
            };
            fullscreenWindow->AddChild(btnZoomIn);

            auto btnFitPage = std::make_shared<UltraCanvasButton>("BtnFit", 20007, 500, 10, 80, 30);
            btnFitPage->SetText("Fit Page");
            btnFitPage->SetColors(Color(60, 60, 65, 255));
            btnFitPage->SetTextColors(Colors::White);
            btnFitPage->onClick = [fullscreenXAR]() {
                fullscreenXAR->SetPreserveAspectRatio(true);
            };
            fullscreenWindow->AddChild(btnFitPage);

            // Instructions label
            auto instructionLabel = std::make_shared<UltraCanvasLabel>(
                    "Instructions", 20008, screenWidth - 200, 10, 190, 30);
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
        auto container = std::make_shared<UltraCanvasContainer>("XARExamples", 5000, 0, 0, 1000, 780);
        container->SetBackgroundColor(Color(245, 245, 245, 255));

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("XARTitle", 5001, 10, 10, 600, 30);
        title->SetText("CorelDRAW XAR Graphics Demo - Click to View Fullscreen");
        title->SetFontSize(16);
        title->SetAutoResize(true);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // Description
        auto description = std::make_shared<UltraCanvasLabel>("XARDescription", 5002, 10, 45, 700, 40);
        description->SetText("Click on XAR images to open in fullscreen mode. Use navigation buttons for multi-page files.\nPress ESC to close fullscreen view. Supports XAR, CMX, CCX, CDT formats.");
        description->SetFontSize(12);
        description->SetTextColor(Color(80, 80, 80, 255));
        container->AddChild(description);

        // Status label for feedback
        auto statusLabel = std::make_shared<UltraCanvasLabel>("XARStatus", 5003, 10, 700, 980, 60);
        statusLabel->SetText("Ready. Click on a XAR file to view.");
        statusLabel->SetFontSize(11);
        statusLabel->SetTextColor(Color(60, 60, 60, 255));
        statusLabel->SetBackgroundColor(Color(230, 230, 230, 255));
        container->AddChild(statusLabel);

        // ===== XAR FILE 1 =====
        auto xarContainer1 = std::make_shared<UltraCanvasContainer>("XARContainer1", 5010, 20, 100, 300, 280);
        xarContainer1->SetBackgroundColor(Colors::White);
        xarContainer1->SetBorders(2, Color(180, 180, 180, 255));

        auto xarElement1 = std::make_shared<UltraCanvasXARElement>("XAR1", 5011, 10, 10, 280, 220);
        //xarElement1->SetFitMode(XARFitMode::FitPage);

        std::string xarFile1 = NormalizePath(GetResourcesDir() + "media/xar/demo.xar");
        if (xarElement1->LoadFromFile(xarFile1)) {
            statusLabel->SetText("Loaded: " + xarFile1);
        }

        auto xarLabel1 = std::make_shared<UltraCanvasLabel>("XARLabel1", 5012, 10, 240, 280, 30);
        xarLabel1->SetText("demo.xar");
        xarLabel1->SetAlignment(TextAlignment::Center);
        xarLabel1->SetFontSize(11);
        xarContainer1->AddChild(xarLabel1);

        auto demoHandler1 = std::make_shared<XARDemoHandler>(xarFile1);
        xarElement1->SetEventCallback([demoHandler1, xarContainer1, statusLabel, xarFile1](const UCEvent& event) {
            switch (event.type) {
                case UCEventType::MouseUp:
                    demoHandler1->OnXARClick();
                    statusLabel->SetText("Opened fullscreen: " + xarFile1);
                    return true;
                case UCEventType::MouseEnter:
                    xarContainer1->SetBordersColor(Color(0, 122, 204, 255));
                    return true;
                case UCEventType::MouseLeave:
                    xarContainer1->SetBordersColor(Color(180, 180, 180, 255));
                    return true;
                default:
                    return false;
            }
        });

        xarContainer1->AddChild(xarElement1);
        container->AddChild(xarContainer1);
/*
        // ===== XAR FILE 2 =====
        auto xarContainer2 = std::make_shared<UltraCanvasContainer>("XARContainer2", 5020, 340, 100, 300, 280);
        xarContainer2->SetBackgroundColor(Colors::White);
        xarContainer2->SetBorders(2, Color(180, 180, 180, 255));

        auto xarElement2 = std::make_shared<UltraCanvasXARElement>("XAR2", 5021, 10, 10, 280, 220);
        xarElement2->SetFitMode(XARFitMode::FitPage);

        std::string xarFile2 = NormalizePath(GetResourcesDir() + "media/xar/logo.xar");
        xarElement2->LoadFromFile(xarFile2);

        auto xarLabel2 = std::make_shared<UltraCanvasLabel>("XARLabel2", 5022, 10, 240, 280, 30);
        xarLabel2->SetText("logo.xar");
        xarLabel2->SetAlignment(TextAlignment::Center);
        xarLabel2->SetFontSize(11);
        xarContainer2->AddChild(xarLabel2);

        auto demoHandler2 = std::make_shared<XARDemoHandler>(xarFile2);
        xarElement2->SetEventCallback([demoHandler2, xarContainer2, statusLabel, xarFile2](const UCEvent& event) {
            switch (event.type) {
                case UCEventType::MouseUp:
                    demoHandler2->OnXARClick();
                    statusLabel->SetText("Opened fullscreen: " + xarFile2);
                    return true;
                case UCEventType::MouseEnter:
                    xarContainer2->SetBordersColor(Color(0, 122, 204, 255));
                    return true;
                case UCEventType::MouseLeave:
                    xarContainer2->SetBordersColor(Color(180, 180, 180, 255));
                    return true;
                default:
                    return false;
            }
        });

        xarContainer2->AddChild(xarElement2);
        container->AddChild(xarContainer2);
*/
        // ===== XAR FILE 3 (CMX format) =====
//        auto xarContainer3 = std::make_shared<UltraCanvasContainer>("XARContainer3", 5030, 660, 100, 300, 280);
//        xarContainer3->SetBackgroundColor(Colors::White);
//        xarContainer3->SetBorders(2, Color(180, 180, 180, 255));
//
//        auto xarElement3 = std::make_shared<UltraCanvasXARElement>("XAR3", 5031, 10, 10, 280, 220);
//        xarElement3->SetFitMode(XARFitMode::FitPage);
//
//        std::string xarFile3 = NormalizePath(GetResourcesDir() + "media/xar/artwork.cmx");
//        xarElement3->LoadFromFile(xarFile3);
//
//        auto xarLabel3 = std::make_shared<UltraCanvasLabel>("XARLabel3", 5032, 10, 240, 280, 30);
//        xarLabel3->SetText("artwork.cmx");
//        xarLabel3->SetAlignment(TextAlignment::Center);
//        xarLabel3->SetFontSize(11);
//        xarContainer3->AddChild(xarLabel3);
//
//        auto demoHandler3 = std::make_shared<XARDemoHandler>(xarFile3);
//        xarElement3->SetEventCallback([demoHandler3, xarContainer3, statusLabel, xarFile3](const UCEvent& event) {
//            switch (event.type) {
//                case UCEventType::MouseUp:
//                    demoHandler3->OnXARClick();
//                    statusLabel->SetText("Opened fullscreen: " + xarFile3);
//                    return true;
//                case UCEventType::MouseEnter:
//                    xarContainer3->SetBordersColor(Color(0, 122, 204, 255));
//                    return true;
//                case UCEventType::MouseLeave:
//                    xarContainer3->SetBordersColor(Color(180, 180, 180, 255));
//                    return true;
//                default:
//                    return false;
//            }
//        });
//
//        xarContainer3->AddChild(xarElement3);
//        container->AddChild(xarContainer3);

        // ===== SECOND ROW =====
/*
        // ===== XAR FILE 4 =====
        auto xarContainer4 = std::make_shared<UltraCanvasContainer>("XARContainer4", 5040, 20, 400, 300, 280);
        xarContainer4->SetBackgroundColor(Colors::White);
        xarContainer4->SetBorders(2, Color(180, 180, 180, 255));

        auto xarElement4 = std::make_shared<UltraCanvasXARElement>("XAR4", 5041, 10, 10, 280, 220);
        //xarElement4->SetFitMode(XARFitMode::FitPage);

        std::string xarFile4 = NormalizePath(GetResourcesDir() + "media/xar/logo.xar");
        xarElement4->LoadFromFile(xarFile4);

        // Page navigation for multi-page document
        auto prevBtn4 = std::make_shared<UltraCanvasButton>("Prev4", 5042, 10, 240, 60, 25);
        prevBtn4->SetText("◀");
        prevBtn4->SetFontSize(10);
        prevBtn4->onClick = [xarElement4]() {
            if (xarElement4->IsLoaded() && xarElement4->GetCurrentPage() > 0) {
                xarElement4->SetCurrentPage(xarElement4->GetCurrentPage() - 1);
            }
        };
        xarContainer4->AddChild(prevBtn4);

        auto pageLabel4 = std::make_shared<UltraCanvasLabel>("PageLabel4", 5043, 80, 240, 140, 25);
        pageLabel4->SetText("brochure.xar");
        pageLabel4->SetAlignment(TextAlignment::Center);
        pageLabel4->SetFontSize(10);
        xarContainer4->AddChild(pageLabel4);

        xarElement4->onPageChanged = [pageLabel4, xarElement4](int page) {
            pageLabel4->SetText("Page " + std::to_string(page + 1) + "/" +
                                std::to_string(xarElement4->GetPageCount()));
        };

        auto nextBtn4 = std::make_shared<UltraCanvasButton>("Next4", 5044, 230, 240, 60, 25);
        nextBtn4->SetText("▶");
        nextBtn4->SetFontSize(10);
        nextBtn4->onClick = [xarElement4]() {
            if (xarElement4->IsLoaded() &&
                xarElement4->GetCurrentPage() < xarElement4->GetPageCount() - 1) {
                xarElement4->SetCurrentPage(xarElement4->GetCurrentPage() + 1);
            }
        };
        xarContainer4->AddChild(nextBtn4);

        auto demoHandler4 = std::make_shared<XARDemoHandler>(xarFile4);
        xarElement4->SetEventCallback([demoHandler4, xarContainer4, statusLabel, xarFile4](const UCEvent& event) {
            switch (event.type) {
                case UCEventType::MouseUp:
                    demoHandler4->OnXARClick();
                    statusLabel->SetText("Opened fullscreen: " + xarFile4);
                    return true;
                case UCEventType::MouseEnter:
                    xarContainer4->SetBordersColor(Color(0, 122, 204, 255));
                    return true;
                case UCEventType::MouseLeave:
                    xarContainer4->SetBordersColor(Color(180, 180, 180, 255));
                    return true;
                default:
                    return false;
            }
        });

        xarContainer4->AddChild(xarElement4);
        container->AddChild(xarContainer4);

        // ===== ZOOM DEMO (XAR FILE 5) =====
        auto xarContainer5 = std::make_shared<UltraCanvasContainer>("XARContainer5", 5050, 340, 400, 300, 280);
        xarContainer5->SetBackgroundColor(Colors::White);
        xarContainer5->SetBorders(2, Color(180, 180, 180, 255));

        auto xarElement5 = std::make_shared<UltraCanvasXARElement>("XAR5", 5051, 10, 10, 280, 220);
        xarElement5->SetFitMode(XARFitMode::FitPage);

        std::string xarFile5 = NormalizePath(GetResourcesDir() + "media/xar/detailed.xar");
        xarElement5->LoadFromFile(xarFile5);

        // Zoom controls
        auto zoomOutBtn5 = std::make_shared<UltraCanvasButton>("ZoomOut5", 5052, 10, 240, 50, 25);
        zoomOutBtn5->SetText("−");
        zoomOutBtn5->onClick = [xarElement5]() {
            xarElement5->SetFitMode(XARFitMode::FitNone);
            xarElement5->SetZoom(xarElement5->GetZoom() / 1.25f);
        };
        xarContainer5->AddChild(zoomOutBtn5);

        auto zoomLabel5 = std::make_shared<UltraCanvasLabel>("ZoomLabel5", 5053, 70, 240, 100, 25);
        zoomLabel5->SetText("Zoom Demo");
        zoomLabel5->SetAlignment(TextAlignment::Center);
        zoomLabel5->SetFontSize(10);
        xarContainer5->AddChild(zoomLabel5);

        auto zoomInBtn5 = std::make_shared<UltraCanvasButton>("ZoomIn5", 5054, 180, 240, 50, 25);
        zoomInBtn5->SetText("+");
        zoomInBtn5->onClick = [xarElement5]() {
            xarElement5->SetFitMode(XARFitMode::FitNone);
            xarElement5->SetZoom(xarElement5->GetZoom() * 1.25f);
        };
        xarContainer5->AddChild(zoomInBtn5);

        auto fitBtn5 = std::make_shared<UltraCanvasButton>("Fit5", 5055, 240, 240, 50, 25);
        fitBtn5->SetText("Fit");
        fitBtn5->onClick = [xarElement5]() {
            xarElement5->SetFitMode(XARFitMode::FitPage);
        };
        xarContainer5->AddChild(fitBtn5);

        auto demoHandler5 = std::make_shared<XARDemoHandler>(xarFile5);
        xarElement5->SetEventCallback([demoHandler5, xarContainer5, statusLabel, xarFile5](const UCEvent& event) {
            switch (event.type) {
                case UCEventType::MouseUp:
                    demoHandler5->OnXARClick();
                    statusLabel->SetText("Opened fullscreen: " + xarFile5);
                    return true;
                case UCEventType::MouseEnter:
                    xarContainer5->SetBordersColor(Color(0, 122, 204, 255));
                    return true;
                case UCEventType::MouseLeave:
                    xarContainer5->SetBordersColor(Color(180, 180, 180, 255));
                    return true;
                default:
                    return false;
            }
        });

        xarContainer5->AddChild(xarElement5);
        container->AddChild(xarContainer5);
*/
        // ===== INFO PANEL =====
        auto infoContainer = std::make_shared<UltraCanvasContainer>("InfoPanel", 5060, 660, 400, 300, 280);
        infoContainer->SetBackgroundColor(Color(240, 248, 255, 255));
        infoContainer->SetBorders(2, Color(100, 149, 237, 255));

        auto infoTitle = std::make_shared<UltraCanvasLabel>("InfoTitle", 5061, 10, 10, 280, 25);
        infoTitle->SetText("XAR Plugin Features");
        infoTitle->SetFontWeight(FontWeight::Bold);
        infoTitle->SetFontSize(13);
        infoContainer->AddChild(infoTitle);

        auto infoText = std::make_shared<UltraCanvasLabel>("InfoText", 5062, 10, 40, 280, 200);
        infoText->SetText(
                "✓ XAR format\n"
                "✓ Vector paths and shapes\n"
                "✓ Text with styling\n"
                "✓ Transformations (rotate, scale)\n"
                "✓ Groups and layers\n"
                "✓ Stroke and fill styles\n"
                "✓ Zoom and pan controls\n"
                "✓ Fit modes (page, width, height)\n"
        );
        infoText->SetFontSize(11);
        infoText->SetTextColor(Color(50, 50, 50, 255));
        infoContainer->AddChild(infoText);

        container->AddChild(infoContainer);

        return container;
    }

} // namespace UltraCanvas