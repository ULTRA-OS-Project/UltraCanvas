// Apps/DemoApp/UltraCanvasXARExamples.cpp
// CDR vector graphics demo examples for UltraCanvas Framework
// Version: 1.0.0
// Last Modified: 2025-12-15
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasWindow.h"
#include "../Plugins/Vector/UltraCanvasVectorElement.h"
#include <iostream>
#include <memory>

namespace UltraCanvas {

// ===== XAR DEMO HANDLER =====
    class XARDemoHandler {
    private:
        std::shared_ptr<UltraCanvasWindow> fullscreenWindow;
        std::string cdrFilePath;

    public:
        XARDemoHandler(const std::string& filePath) : cdrFilePath(filePath) {}

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
            auto fullscreenXAR = std::make_shared<UltraCanvasVectorElement>(
                    "FullscreenXAR", 20001, 0, 50, screenWidth, screenHeight - 100);
            fullscreenXAR->CenterDocument();

            if (!cdrFilePath.empty()) {
                fullscreenXAR->LoadFromFile(cdrFilePath);
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
                if ((event.type == UCEventType::KeyUp && event.virtualKey == UCKeys::Escape) ||
                    event.type == UCEventType::WindowClose) {
                    if (fullscreenWindow) {
                        fullscreenWindow->RequestDelete();
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
        auto cdrContainer1 = std::make_shared<UltraCanvasContainer>("XARContainer1", 5010, 20, 100, 300, 280);
        cdrContainer1->SetBackgroundColor(Colors::White);
        cdrContainer1->SetBorders(2, Color(180, 180, 180, 255));

        auto cdrElement1 = std::make_shared<UltraCanvasXARElement>("XAR1", 5011, 10, 10, 280, 220);
        //cdrElement1->SetFitMode(XARFitMode::FitPage);

        std::string cdrFile1 = "media/xar/demo.xar";
        if (cdrElement1->LoadFromFile(cdrFile1)) {
            statusLabel->SetText("Loaded: " + cdrFile1);
        }

        auto cdrLabel1 = std::make_shared<UltraCanvasLabel>("XARLabel1", 5012, 10, 240, 280, 30);
        cdrLabel1->SetText("demo.xar");
        cdrLabel1->SetAlignment(TextAlignment::Center);
        cdrLabel1->SetFontSize(11);
        cdrContainer1->AddChild(cdrLabel1);

        auto demoHandler1 = std::make_shared<XARDemoHandler>(cdrFile1);
        cdrElement1->SetEventCallback([demoHandler1, cdrContainer1, statusLabel, cdrFile1](const UCEvent& event) {
            switch (event.type) {
                case UCEventType::MouseUp:
                    demoHandler1->OnXARClick();
                    statusLabel->SetText("Opened fullscreen: " + cdrFile1);
                    return true;
                case UCEventType::MouseEnter:
                    cdrContainer1->SetBordersColor(Color(0, 122, 204, 255));
                    return true;
                case UCEventType::MouseLeave:
                    cdrContainer1->SetBordersColor(Color(180, 180, 180, 255));
                    return true;
                default:
                    return false;
            }
        });

        cdrContainer1->AddChild(cdrElement1);
        container->AddChild(cdrContainer1);
/*
        // ===== XAR FILE 2 =====
        auto cdrContainer2 = std::make_shared<UltraCanvasContainer>("XARContainer2", 5020, 340, 100, 300, 280);
        cdrContainer2->SetBackgroundColor(Colors::White);
        cdrContainer2->SetBorders(2, Color(180, 180, 180, 255));

        auto cdrElement2 = std::make_shared<UltraCanvasXARElement>("XAR2", 5021, 10, 10, 280, 220);
        cdrElement2->SetFitMode(XARFitMode::FitPage);

        std::string cdrFile2 = "media/cdr/logo.cdr";
        cdrElement2->LoadFromFile(cdrFile2);

        auto cdrLabel2 = std::make_shared<UltraCanvasLabel>("XARLabel2", 5022, 10, 240, 280, 30);
        cdrLabel2->SetText("logo.cdr");
        cdrLabel2->SetAlignment(TextAlignment::Center);
        cdrLabel2->SetFontSize(11);
        cdrContainer2->AddChild(cdrLabel2);

        auto demoHandler2 = std::make_shared<XARDemoHandler>(cdrFile2);
        cdrElement2->SetEventCallback([demoHandler2, cdrContainer2, statusLabel, cdrFile2](const UCEvent& event) {
            switch (event.type) {
                case UCEventType::MouseUp:
                    demoHandler2->OnXARClick();
                    statusLabel->SetText("Opened fullscreen: " + cdrFile2);
                    return true;
                case UCEventType::MouseEnter:
                    cdrContainer2->SetBordersColor(Color(0, 122, 204, 255));
                    return true;
                case UCEventType::MouseLeave:
                    cdrContainer2->SetBordersColor(Color(180, 180, 180, 255));
                    return true;
                default:
                    return false;
            }
        });

        cdrContainer2->AddChild(cdrElement2);
        container->AddChild(cdrContainer2);
*/
        // ===== XAR FILE 3 (CMX format) =====
//        auto cdrContainer3 = std::make_shared<UltraCanvasContainer>("XARContainer3", 5030, 660, 100, 300, 280);
//        cdrContainer3->SetBackgroundColor(Colors::White);
//        cdrContainer3->SetBorders(2, Color(180, 180, 180, 255));
//
//        auto cdrElement3 = std::make_shared<UltraCanvasXARElement>("XAR3", 5031, 10, 10, 280, 220);
//        cdrElement3->SetFitMode(XARFitMode::FitPage);
//
//        std::string cdrFile3 = "media/cdr/artwork.cmx";
//        cdrElement3->LoadFromFile(cdrFile3);
//
//        auto cdrLabel3 = std::make_shared<UltraCanvasLabel>("XARLabel3", 5032, 10, 240, 280, 30);
//        cdrLabel3->SetText("artwork.cmx");
//        cdrLabel3->SetAlignment(TextAlignment::Center);
//        cdrLabel3->SetFontSize(11);
//        cdrContainer3->AddChild(cdrLabel3);
//
//        auto demoHandler3 = std::make_shared<XARDemoHandler>(cdrFile3);
//        cdrElement3->SetEventCallback([demoHandler3, cdrContainer3, statusLabel, cdrFile3](const UCEvent& event) {
//            switch (event.type) {
//                case UCEventType::MouseUp:
//                    demoHandler3->OnXARClick();
//                    statusLabel->SetText("Opened fullscreen: " + cdrFile3);
//                    return true;
//                case UCEventType::MouseEnter:
//                    cdrContainer3->SetBordersColor(Color(0, 122, 204, 255));
//                    return true;
//                case UCEventType::MouseLeave:
//                    cdrContainer3->SetBordersColor(Color(180, 180, 180, 255));
//                    return true;
//                default:
//                    return false;
//            }
//        });
//
//        cdrContainer3->AddChild(cdrElement3);
//        container->AddChild(cdrContainer3);

        // ===== SECOND ROW =====
/*
        // ===== XAR FILE 4 =====
        auto cdrContainer4 = std::make_shared<UltraCanvasContainer>("XARContainer4", 5040, 20, 400, 300, 280);
        cdrContainer4->SetBackgroundColor(Colors::White);
        cdrContainer4->SetBorders(2, Color(180, 180, 180, 255));

        auto cdrElement4 = std::make_shared<UltraCanvasXARElement>("XAR4", 5041, 10, 10, 280, 220);
        //cdrElement4->SetFitMode(XARFitMode::FitPage);

        std::string cdrFile4 = "media/cdr/logo.cdr";
        cdrElement4->LoadFromFile(cdrFile4);

        // Page navigation for multi-page document
        auto prevBtn4 = std::make_shared<UltraCanvasButton>("Prev4", 5042, 10, 240, 60, 25);
        prevBtn4->SetText("◀");
        prevBtn4->SetFontSize(10);
        prevBtn4->onClick = [cdrElement4]() {
            if (cdrElement4->IsLoaded() && cdrElement4->GetCurrentPage() > 0) {
                cdrElement4->SetCurrentPage(cdrElement4->GetCurrentPage() - 1);
            }
        };
        cdrContainer4->AddChild(prevBtn4);

        auto pageLabel4 = std::make_shared<UltraCanvasLabel>("PageLabel4", 5043, 80, 240, 140, 25);
        pageLabel4->SetText("brochure.cdr");
        pageLabel4->SetAlignment(TextAlignment::Center);
        pageLabel4->SetFontSize(10);
        cdrContainer4->AddChild(pageLabel4);

        cdrElement4->onPageChanged = [pageLabel4, cdrElement4](int page) {
            pageLabel4->SetText("Page " + std::to_string(page + 1) + "/" +
                                std::to_string(cdrElement4->GetPageCount()));
        };

        auto nextBtn4 = std::make_shared<UltraCanvasButton>("Next4", 5044, 230, 240, 60, 25);
        nextBtn4->SetText("▶");
        nextBtn4->SetFontSize(10);
        nextBtn4->onClick = [cdrElement4]() {
            if (cdrElement4->IsLoaded() &&
                cdrElement4->GetCurrentPage() < cdrElement4->GetPageCount() - 1) {
                cdrElement4->SetCurrentPage(cdrElement4->GetCurrentPage() + 1);
            }
        };
        cdrContainer4->AddChild(nextBtn4);

        auto demoHandler4 = std::make_shared<XARDemoHandler>(cdrFile4);
        cdrElement4->SetEventCallback([demoHandler4, cdrContainer4, statusLabel, cdrFile4](const UCEvent& event) {
            switch (event.type) {
                case UCEventType::MouseUp:
                    demoHandler4->OnXARClick();
                    statusLabel->SetText("Opened fullscreen: " + cdrFile4);
                    return true;
                case UCEventType::MouseEnter:
                    cdrContainer4->SetBordersColor(Color(0, 122, 204, 255));
                    return true;
                case UCEventType::MouseLeave:
                    cdrContainer4->SetBordersColor(Color(180, 180, 180, 255));
                    return true;
                default:
                    return false;
            }
        });

        cdrContainer4->AddChild(cdrElement4);
        container->AddChild(cdrContainer4);

        // ===== ZOOM DEMO (XAR FILE 5) =====
        auto cdrContainer5 = std::make_shared<UltraCanvasContainer>("XARContainer5", 5050, 340, 400, 300, 280);
        cdrContainer5->SetBackgroundColor(Colors::White);
        cdrContainer5->SetBorders(2, Color(180, 180, 180, 255));

        auto cdrElement5 = std::make_shared<UltraCanvasXARElement>("XAR5", 5051, 10, 10, 280, 220);
        cdrElement5->SetFitMode(XARFitMode::FitPage);

        std::string cdrFile5 = "media/cdr/detailed.cdr";
        cdrElement5->LoadFromFile(cdrFile5);

        // Zoom controls
        auto zoomOutBtn5 = std::make_shared<UltraCanvasButton>("ZoomOut5", 5052, 10, 240, 50, 25);
        zoomOutBtn5->SetText("−");
        zoomOutBtn5->onClick = [cdrElement5]() {
            cdrElement5->SetFitMode(XARFitMode::FitNone);
            cdrElement5->SetZoom(cdrElement5->GetZoom() / 1.25f);
        };
        cdrContainer5->AddChild(zoomOutBtn5);

        auto zoomLabel5 = std::make_shared<UltraCanvasLabel>("ZoomLabel5", 5053, 70, 240, 100, 25);
        zoomLabel5->SetText("Zoom Demo");
        zoomLabel5->SetAlignment(TextAlignment::Center);
        zoomLabel5->SetFontSize(10);
        cdrContainer5->AddChild(zoomLabel5);

        auto zoomInBtn5 = std::make_shared<UltraCanvasButton>("ZoomIn5", 5054, 180, 240, 50, 25);
        zoomInBtn5->SetText("+");
        zoomInBtn5->onClick = [cdrElement5]() {
            cdrElement5->SetFitMode(XARFitMode::FitNone);
            cdrElement5->SetZoom(cdrElement5->GetZoom() * 1.25f);
        };
        cdrContainer5->AddChild(zoomInBtn5);

        auto fitBtn5 = std::make_shared<UltraCanvasButton>("Fit5", 5055, 240, 240, 50, 25);
        fitBtn5->SetText("Fit");
        fitBtn5->onClick = [cdrElement5]() {
            cdrElement5->SetFitMode(XARFitMode::FitPage);
        };
        cdrContainer5->AddChild(fitBtn5);

        auto demoHandler5 = std::make_shared<XARDemoHandler>(cdrFile5);
        cdrElement5->SetEventCallback([demoHandler5, cdrContainer5, statusLabel, cdrFile5](const UCEvent& event) {
            switch (event.type) {
                case UCEventType::MouseUp:
                    demoHandler5->OnXARClick();
                    statusLabel->SetText("Opened fullscreen: " + cdrFile5);
                    return true;
                case UCEventType::MouseEnter:
                    cdrContainer5->SetBordersColor(Color(0, 122, 204, 255));
                    return true;
                case UCEventType::MouseLeave:
                    cdrContainer5->SetBordersColor(Color(180, 180, 180, 255));
                    return true;
                default:
                    return false;
            }
        });

        cdrContainer5->AddChild(cdrElement5);
        container->AddChild(cdrContainer5);
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