// UltraCanvasHEIFExamplesBeautified.cpp
// Beautified HEIF/HEIC format demonstration page - Single page layout
// Version: 2.1.0
// Last Modified: 2025-06-24
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasSlider.h"
#include "PixelFX/PixelFX.h"
#include <memory>
#include <sstream>
#include <filesystem>
#include <fmt/os.h>

namespace UltraCanvas {

    // ===== FULL-SIZE IMAGE VIEWER HANDLER =====
    class FullSizeImageViewerHandler {
    private:
        std::shared_ptr<UltraCanvasWindow> viewerWindow;
        std::shared_ptr<UltraCanvasImageElement> imageElement;
        std::shared_ptr<UltraCanvasSlider> zoomSlider;
        std::string imagePath;
        float currentZoom = 1.0f;
        Point2Di panOffset = {0, 0};
        Point2Di lastMousePos = {0, 0};
        bool isPanning = false;

    public:
        FullSizeImageViewerHandler(const std::string& path) : imagePath(path) {}

        void Show() {
            if (viewerWindow) {
                viewerWindow->Show();
                return;
            }
            CreateViewerWindow();
        }

        void CreateViewerWindow() {
            // Get screen dimensions (default to common resolution)
            int screenWidth = 1920;
            int screenHeight = 1080;

            // Extract filename for title
            std::string filename = imagePath;
            size_t lastSlash = imagePath.find_last_of("/\\");
            if (lastSlash != std::string::npos) {
                filename = imagePath.substr(lastSlash + 1);
            }

            // Create window configuration
            WindowConfig config;
            config.title = "Image Viewer - " + filename;
            config.width = screenWidth;
            config.height = screenHeight;
            config.x = 0;
            config.y = 0;
            config.type = WindowType::Fullscreen;
            config.resizable = false;
            config.backgroundColor = Color(32, 32, 32, 255);

            // Create the viewer window
            viewerWindow = CreateWindow(config);

            // Create dark background container
            auto bgContainer = std::make_shared<UltraCanvasContainer>(
                    "ImageViewerBG", 30000, 0, 0, screenWidth, screenHeight
            );
            bgContainer->SetBackgroundColor(Color(32, 32, 32, 255));
            viewerWindow->AddChild(bgContainer);

            // Create image element (centered, with padding for toolbar)
            int imageAreaHeight = screenHeight - 80;  // Leave space for toolbar
            imageElement = std::make_shared<UltraCanvasImageElement>(
                    "FullSizeImage", 30001,
                    50, 60,
                    screenWidth - 100, imageAreaHeight - 20
            );
            imageElement->LoadFromFile(imagePath);
            imageElement->SetFitMode(ImageFitMode::Contain);
            imageElement->SetBackgroundColor(Color(32, 32, 32, 255));
            bgContainer->AddChild(imageElement);

            // Create top toolbar container
            auto toolbar = std::make_shared<UltraCanvasContainer>(
                    "Toolbar", 30010, 0, 0, screenWidth, 50
            );
            toolbar->SetBackgroundColor(Color(45, 45, 45, 255));
            bgContainer->AddChild(toolbar);

            // Filename label
            auto filenameLabel = std::make_shared<UltraCanvasLabel>(
                    "FilenameLabel", 30011, 20, 12, 400, 26
            );
            filenameLabel->SetText(filename);
            filenameLabel->SetFontSize(14);
            filenameLabel->SetFontWeight(FontWeight::Bold);
            filenameLabel->SetTextColor(Color(255, 255, 255, 255));
            toolbar->AddChild(filenameLabel);

            // Instructions label
            auto instructionLabel = std::make_shared<UltraCanvasLabel>(
                    "Instructions", 30012, screenWidth - 250, 12, 230, 26
            );
            instructionLabel->SetText("Press ESC to close");
            instructionLabel->SetFontSize(12);
            instructionLabel->SetTextColor(Color(180, 180, 180, 255));
            instructionLabel->SetAlignment(TextAlignment::Right);
            toolbar->AddChild(instructionLabel);

            // Zoom controls container (center of toolbar)
            int zoomControlsX = (screenWidth - 300) / 2;

            // Zoom out button
            auto zoomOutBtn = std::make_shared<UltraCanvasButton>(
                    "ZoomOut", 30020, zoomControlsX, 10, 40, 30
            );
            zoomOutBtn->SetText("−");
            zoomOutBtn->SetFontSize(18);
            zoomOutBtn->SetColors(Color(60, 60, 60, 255), Color(80, 80, 80, 255));
            zoomOutBtn->SetTextColors(Color(255, 255, 255, 255));
            zoomOutBtn->SetCornerRadius(4);
            zoomOutBtn->onClick = [this]() {
                AdjustZoom(-0.1f);
            };
            toolbar->AddChild(zoomOutBtn);

            // Zoom slider
            zoomSlider = std::make_shared<UltraCanvasSlider>(
                    "ZoomSlider", 30021, zoomControlsX + 50, 15, 150, 20
            );
            zoomSlider->SetRange(0.25f, 3.0f);
            zoomSlider->SetValue(1.0f);
            zoomSlider->SetStep(0.05f);
            zoomSlider->onValueChanged = [this](float value) {
                SetZoom(value);
            };
            toolbar->AddChild(zoomSlider);

            // Zoom in button
            auto zoomInBtn = std::make_shared<UltraCanvasButton>(
                    "ZoomIn", 30022, zoomControlsX + 210, 10, 40, 30
            );
            zoomInBtn->SetText("+");
            zoomInBtn->SetFontSize(18);
            zoomInBtn->SetColors(Color(60, 60, 60, 255), Color(80, 80, 80, 255));
            zoomInBtn->SetTextColors(Color(255, 255, 255, 255));
            zoomInBtn->SetCornerRadius(4);
            zoomInBtn->onClick = [this]() {
                AdjustZoom(0.1f);
            };
            toolbar->AddChild(zoomInBtn);

            // Fit to window button
            auto fitBtn = std::make_shared<UltraCanvasButton>(
                    "FitBtn", 30023, zoomControlsX + 260, 10, 60, 30
            );
            fitBtn->SetText("Fit");
            fitBtn->SetFontSize(11);
            fitBtn->SetColors(Color(60, 60, 60, 255), Color(80, 80, 80, 255));
            fitBtn->SetTextColors(Color(255, 255, 255, 255));
            fitBtn->SetCornerRadius(4);
            fitBtn->onClick = [this]() {
                ResetView();
            };
            toolbar->AddChild(fitBtn);

            // Close button (top right)
            auto closeBtn = std::make_shared<UltraCanvasButton>(
                    "CloseBtn", 30030, screenWidth - 50, 10, 40, 30
            );
            closeBtn->SetText("✕");
            closeBtn->SetFontSize(14);
            closeBtn->SetColors(Color(180, 60, 60, 255), Color(220, 80, 80, 255));
            closeBtn->SetTextColors(Color(255, 255, 255, 255));
            closeBtn->SetCornerRadius(4);
            closeBtn->onClick = [this]() {
                CloseViewer();
            };
            toolbar->AddChild(closeBtn);

            // Bottom info bar
            auto infoBar = std::make_shared<UltraCanvasContainer>(
                    "InfoBar", 30040, 0, screenHeight - 30, screenWidth, 30
            );
            infoBar->SetBackgroundColor(Color(45, 45, 45, 255));
            bgContainer->AddChild(infoBar);

            // Image info label
            auto infoLabel = std::make_shared<UltraCanvasLabel>(
                    "InfoLabel", 30041, 20, 6, 600, 18
            );
            infoLabel->SetText("Use mouse wheel to zoom, drag to pan");
            infoLabel->SetFontSize(11);
            infoLabel->SetTextColor(Color(150, 150, 150, 255));
            infoBar->AddChild(infoLabel);

            // Setup keyboard and mouse event handling
            viewerWindow->SetEventCallback([this](const UCEvent& event) {
                return HandleEvent(event);
            });

            // Show the window
            viewerWindow->Show();
        }

        bool HandleEvent(const UCEvent& event) {
            switch (event.type) {
                case UCEventType::KeyUp:
                    if (event.virtualKey == UCKeys::Escape) {
                        CloseViewer();
                        return true;
                    }
                    // Zoom shortcuts
                    if (event.virtualKey == UCKeys::Plus || event.virtualKey == UCKeys::NumPadAdd) {
                        AdjustZoom(0.1f);
                        return true;
                    }
                    if (event.virtualKey == UCKeys::Minus || event.virtualKey == UCKeys::NumPadSubtract) {
                        AdjustZoom(-0.1f);
                        return true;
                    }
                    if (event.virtualKey == UCKeys::Key0 || event.virtualKey == UCKeys::NumPad0) {
                        ResetView();
                        return true;
                    }
                    break;

                case UCEventType::MouseWheel:
                    // Zoom with mouse wheel
                    if (event.wheelDelta > 0) {
                        AdjustZoom(0.1f);
                    } else {
                        AdjustZoom(-0.1f);
                    }
                    return true;

                case UCEventType::MouseDown:
                    if (event.button == UCMouseButton::Left || event.button == UCMouseButton::Middle) {
                        isPanning = true;
                        lastMousePos = Point2Di(event.x, event.y);
                        return true;
                    }
                    break;

                case UCEventType::MouseUp:
                    if (isPanning) {
                        isPanning = false;
                        return true;
                    }
                    break;

                case UCEventType::MouseMove:
                    if (isPanning) {
                        int deltaX = event.x - lastMousePos.x;
                        int deltaY = event.y - lastMousePos.y;
                        panOffset.x += deltaX;
                        panOffset.y += deltaY;
                        lastMousePos = Point2Di(event.x, event.y);
                        UpdateImagePosition();
                        return true;
                    }
                    break;

                case UCEventType::WindowClose:
                    CloseViewer();
                    return true;

                default:
                    break;
            }
            return false;
        }

        void AdjustZoom(float delta) {
            currentZoom = std::clamp(currentZoom + delta, 0.25f, 3.0f);
            if (zoomSlider) {
                zoomSlider->SetValue(currentZoom);
            }
            UpdateImageScale();
        }

        void SetZoom(float zoom) {
            currentZoom = std::clamp(zoom, 0.25f, 3.0f);
            UpdateImageScale();
        }

        void UpdateImageScale() {
            if (imageElement) {
                imageElement->SetScale(currentZoom, currentZoom);
                imageElement->RequestRedraw();
            }
        }

        void UpdateImagePosition() {
            if (imageElement) {
                // Apply pan offset to image position
                int baseX = 50 + panOffset.x;
                int baseY = 60 + panOffset.y;
                imageElement->SetPosition(baseX, baseY);
                imageElement->RequestRedraw();
            }
        }

        void ResetView() {
            currentZoom = 1.0f;
            panOffset = {0, 0};
            if (zoomSlider) {
                zoomSlider->SetValue(1.0f);
            }
            if (imageElement) {
                imageElement->SetScale(1.0f, 1.0f);
                imageElement->SetPosition(50, 60);
                imageElement->SetFitMode(ImageFitMode::Contain);
                imageElement->RequestRedraw();
            }
        }

        void CloseViewer() {
            if (viewerWindow) {
                viewerWindow->RequestDelete();
                viewerWindow.reset();
            }
        }
    };

// ===== FULL-SIZE IMAGE VIEWER FUNCTION =====
// Static map to keep viewer handlers alive
    static std::unordered_map<std::string, std::shared_ptr<FullSizeImageViewerHandler>> g_imageViewers;

    void ShowFullSizeImageViewer(const std::string& imagePath) {
        // Create or reuse viewer handler for this image
        auto it = g_imageViewers.find(imagePath);
        if (it != g_imageViewers.end() && it->second) {
            it->second->Show();
        } else {
            auto handler = std::make_shared<FullSizeImageViewerHandler>(imagePath);
            g_imageViewers[imagePath] = handler;
            handler->Show();
        }
    }

// ===== HEIF FORMAT EXAMPLES - BEAUTIFIED SINGLE PAGE VERSION =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateHEIFExamples() {
        // Main container with warm background
        std::string imageFilename = "fantasycutemonster.heif";
        std::string imagePath = "media/images/"+imageFilename;
        
        auto container = std::make_shared<UltraCanvasContainer>("HEIFDemoPage", 1800, 0, 0, 950, 750);
        container->SetBackgroundColor(Color(255, 251, 235, 255));  // Warm amber background

        // Layout constants
        const int leftColX = 20;
        const int rightColX = 310;
        const int leftColWidth = 270;
        const int rightColWidth = 620;
        const int row1Y = 20;
        const int row2Y = 340;
        const int row3Y = 560;

        // ===== ROW 1 LEFT: IMAGE PREVIEW CARD =====
        auto imageCard = std::make_shared<UltraCanvasContainer>("ImageCard", 1820, leftColX, row1Y, leftColWidth, 300);
        imageCard->SetBackgroundColor(Color(255, 255, 255, 255));
        imageCard->SetBorders(1, Color(230, 230, 230, 255));
        container->AddChild(imageCard);

        // Card Title
        auto imageTitle = std::make_shared<UltraCanvasLabel>("ImageTitle", 1821, 20, 16, 200, 24);
        imageTitle->SetText("Demo HEIF Image");
        imageTitle->SetFontSize(14);
        imageTitle->SetFontWeight(FontWeight::Bold);
        imageTitle->SetTextColor(Color(30, 41, 59, 255));
        imageCard->AddChild(imageTitle);

        // Image Container with border
        auto imageFrame = std::make_shared<UltraCanvasContainer>("ImageFrame", 1822, 20, 48, 230, 170);
        imageFrame->SetBackgroundColor(Color(241, 245, 249, 255));  // Slate 100
        imageFrame->SetBorders(1, Color(200, 200, 200, 255));
        imageCard->AddChild(imageFrame);

        // Actual Image
        auto heifImage = std::make_shared<UltraCanvasImageElement>("HEIFImage", 1823, 4, 4, 222, 162);
        heifImage->LoadFromFile(imagePath);
        heifImage->SetFitMode(ImageFitMode::Contain);
        heifImage->SetMousePointer(MousePointer::Hand);
        heifImage->SetClickable(true);
        heifImage->onClick = [imagePath]() {
            ShowFullSizeImageViewer(imagePath);
        };
        imageFrame->AddChild(heifImage);

        // Filename label below image
        auto filenameLabel = std::make_shared<UltraCanvasLabel>("Filename", 1825, 20, 224, 230, 20);
        filenameLabel->SetText(imageFilename);
        filenameLabel->SetFontSize(10);
        filenameLabel->SetTextColor(Color(100, 116, 139, 255));
        filenameLabel->SetAlignment(TextAlignment::Center);
        imageCard->AddChild(filenameLabel);

        // Action Buttons
        auto viewBtn = std::make_shared<UltraCanvasButton>("ViewBtn", 1827, 20, 250, 108, 32);
        viewBtn->SetText("🔍 View Full");
        viewBtn->SetFontSize(10);
        viewBtn->SetColors(Color(249, 115, 22, 255), Color(234, 88, 12, 255));  // Orange
        viewBtn->SetTextColors(Color(255, 255, 255, 255));
        viewBtn->SetCornerRadius(6);
        viewBtn->onClick = [imagePath]() {
            ShowFullSizeImageViewer(imagePath);
        };
        imageCard->AddChild(viewBtn);

        auto exportBtn = std::make_shared<UltraCanvasButton>("ExportBtn", 1828, 138, 250, 108, 32);
        exportBtn->SetText("📤 Export");
        exportBtn->SetFontSize(10);
        exportBtn->SetColors(Color(241, 245, 249, 255), Color(226, 232, 240, 255));  // Slate
        exportBtn->SetTextColors(Color(71, 85, 105, 255));
        exportBtn->SetCornerRadius(6);
        imageCard->AddChild(exportBtn);

        // ===== ROW 1 RIGHT: IMAGE PROPERTIES CARD =====
        auto propertiesCard = std::make_shared<UltraCanvasContainer>("PropertiesCard", 1830, rightColX, row1Y, rightColWidth, 300);
        propertiesCard->SetBackgroundColor(Color(255, 255, 255, 255));
        propertiesCard->SetBorders(1, Color(230, 230, 230, 255));
        container->AddChild(propertiesCard);

        // Properties Header with icon
        auto propHeader = std::make_shared<UltraCanvasLabel>("PropHeader", 1831, 20, 16, 300, 24);
        propHeader->SetText("📊  Image Properties");
        propHeader->SetFontSize(14);
        propHeader->SetFontWeight(FontWeight::Bold);
        propHeader->SetTextColor(Color(30, 41, 59, 255));
        propertiesCard->AddChild(propHeader);

        // Property Items Grid (2 columns x 4 rows)
        struct PropertyItem {
            std::string label;
            std::string value;
        };
        auto imgInfo = PixelFX::ExtractImageInfo(imagePath);

        std::vector<PropertyItem> properties = {
                {"FILE SIZE", FormatFileSize(imgInfo.fileSize)},
                {"RESOLUTION", fmt::format("{}x{}", imgInfo.width, imgInfo.height)},
                {"CHANNELS", fmt::format("{}", imgInfo.channels)},
                {"COLOR SPACE", imgInfo.colorSpace},
                {"LOADER", imgInfo.loader},
                {"BIT DEPTH", fmt::format("{}", imgInfo.bitsPerChannel * imgInfo.channels)},
                {"ALPHA CHANNEL", imgInfo.hasAlpha ? "Yes" : "No"},
                {"DPI", fmt::format("{}×{}", imgInfo.dpiX, imgInfo.dpiY)}
        };

        int propStartY = 56;
        int propColWidth = 290;
        int propHeight = 52;
        int propGapX = 16;
        int propGapY = 8;

        for (size_t i = 0; i < properties.size(); ++i) {
            int col = i % 2;
            int row = i / 2;

            // Property container
            auto propContainer = std::make_shared<UltraCanvasContainer>(
                    "Prop" + std::to_string(i), 1840 + i,
                    20 + col * (propColWidth + propGapX),
                    propStartY + row * (propHeight + propGapY),
                    propColWidth, propHeight
            );
            propContainer->SetBackgroundColor(Color(248, 250, 252, 255));  // Slate 50
            propContainer->SetBorders(1, Color(226, 232, 240, 255));  // Slate 200
            propertiesCard->AddChild(propContainer);

            // Property label (uppercase)
            auto propLabel = std::make_shared<UltraCanvasLabel>(
                    "PropLabel" + std::to_string(i), 1860 + i,
                    16, 8, 150, 16
            );
            propLabel->SetText(properties[i].label);
            propLabel->SetFontSize(9);
            propLabel->SetFontWeight(FontWeight::Normal);
            propLabel->SetTextColor(Color(100, 116, 139, 255));  // Slate 500
            propContainer->AddChild(propLabel);

            // Property value
            auto propValue = std::make_shared<UltraCanvasLabel>(
                    "PropValue" + std::to_string(i), 1880 + i,
                    16, 28, 260, 18
            );
            propValue->SetText(properties[i].value);
            propValue->SetFontSize(13);
            propValue->SetFontWeight(FontWeight::Bold);
            propValue->SetTextColor(Color(30, 41, 59, 255));  // Slate 800
            propContainer->AddChild(propValue);
        }

        // ===== ROW 2 LEFT: ABOUT HEIF/HEIC CARD =====
        auto aboutCard = std::make_shared<UltraCanvasContainer>("AboutCard", 1900, leftColX, row2Y, leftColWidth, 200);
        aboutCard->SetBackgroundColor(Color(255, 255, 255, 255));
        aboutCard->SetBorders(1, Color(230, 230, 230, 255));
        container->AddChild(aboutCard);

        // About Icon Box (using label with background)
        auto aboutIconLabel = std::make_shared<UltraCanvasLabel>("AboutIcon", 1901, 20, 16, 36, 36);
        aboutIconLabel->SetText("📄");
        aboutIconLabel->SetFontSize(16);
        aboutIconLabel->SetBackgroundColor(Color(255, 237, 213, 255));  // Orange 100
        aboutIconLabel->SetAlignment(TextAlignment::Center);
        aboutIconLabel->SetPadding(4);
        aboutCard->AddChild(aboutIconLabel);

        // About Title
        auto aboutTitle = std::make_shared<UltraCanvasLabel>("AboutTitle", 1903, 20, 56, 230, 20);
        aboutTitle->SetText("About HEIF/HEIC");
        aboutTitle->SetFontSize(13);
        aboutTitle->SetFontWeight(FontWeight::Bold);
        aboutTitle->SetTextColor(Color(30, 41, 59, 255));
        aboutCard->AddChild(aboutTitle);

        // About Description
        auto aboutDesc = std::make_shared<UltraCanvasLabel>("AboutDesc", 1904, 20, 80, 230, 110);
        aboutDesc->SetText(
                "HEIF/HEIC (High Efficiency Image Format) is an image container format based on "
                "HEVC (H.265) video compression. It provides superior compression efficiency compared "
                "to JPEG while maintaining high image quality. HEIF supports features like image "
                "sequences, transparency, depth maps, and HDR. This format is used by default on "
                "Apple devices since iOS 11 and macOS High Sierra."
        );
        aboutDesc->SetFontSize(10);
        aboutDesc->SetTextColor(Color(71, 85, 105, 255));  // Slate 600
        aboutDesc->SetWordWrap(true);
        aboutDesc->SetAlignment(TextAlignment::Left);
        aboutCard->AddChild(aboutDesc);

        // ===== ROW 2 RIGHT: FORMAT CAPABILITIES CARD =====
        auto capabilitiesCard = std::make_shared<UltraCanvasContainer>("CapabilitiesCard", 1910, rightColX, row2Y, rightColWidth, 200);
        capabilitiesCard->SetBackgroundColor(Color(255, 255, 255, 255));
        capabilitiesCard->SetBorders(1, Color(230, 230, 230, 255));
        container->AddChild(capabilitiesCard);

        // Capabilities Header with icon
        auto capIconLabel = std::make_shared<UltraCanvasLabel>("CapIcon", 1911, 20, 16, 36, 36);
        capIconLabel->SetText("⚙️");
        capIconLabel->SetFontSize(16);
        capIconLabel->SetBackgroundColor(Color(236, 253, 245, 255));  // Emerald 50
        capIconLabel->SetAlignment(TextAlignment::Center);
        capIconLabel->SetPadding(4);
        capabilitiesCard->AddChild(capIconLabel);

        auto capTitle = std::make_shared<UltraCanvasLabel>("CapTitle", 1913, 60, 20, 200, 24);
        capTitle->SetText("Format Capabilities");
        capTitle->SetFontSize(14);
        capTitle->SetFontWeight(FontWeight::Bold);
        capTitle->SetTextColor(Color(30, 41, 59, 255));
        capTitle->SetAutoResize(true);
        capabilitiesCard->AddChild(capTitle);

        // Capability Items (3 columns x 2 rows)
        struct CapabilityItem {
            std::string label;
            std::string value;
            bool isGreen;  // true = green (supported), false = orange (feature)
        };

        std::vector<CapabilityItem> capabilities = {
                {"Compression", "HEVC-based", false},
                {"Quality", "High Efficiency", false},
                {"Alpha Channel", "Supported", true},
                {"Image Sequences", "Supported", true},
                {"Depth Maps", "Supported", true},
                {"HDR", "10-bit support", true}
        };

        int capStartX = 20;
        int capStartY = 60;
        int capWidth = 190;
        int capHeight = 60;
        int capGapX = 12;
        int capGapY = 10;

        for (size_t i = 0; i < capabilities.size(); ++i) {
            int col = i % 3;
            int row = i / 3;

            // Capability container
            auto capContainer = std::make_shared<UltraCanvasContainer>(
                    "Cap" + std::to_string(i), 1920 + i,
                    capStartX + col * (capWidth + capGapX),
                    capStartY + row * (capHeight + capGapY),
                    capWidth + 2, capHeight + 2
            );
            capContainer->SetPadding(0,6,0,6);
            if (capabilities[i].isGreen) {
                capContainer->SetBackgroundColor(Color(236, 253, 245, 255));  // Emerald 50
                capContainer->SetBorders(1, Color(167, 243, 208, 255));       // Emerald 200
            } else {
                capContainer->SetBackgroundColor(Color(255, 247, 237, 255));  // Orange 50
                capContainer->SetBorders(1, Color(254, 215, 170, 255));       // Orange 200
            }
            capabilitiesCard->AddChild(capContainer);

            // Capability label
            auto capLabel = std::make_shared<UltraCanvasLabel>(
                    "CapLabel" + std::to_string(i), 1940 + i,
                    0, 10, 0, 0
            );
            capLabel->SetText(capabilities[i].label);
            capLabel->SetFontSize(10);
            capLabel->SetTextColor(Color(100, 116, 139, 255));  // Slate 500
            capLabel->SetAlignment(TextAlignment::Center);
            capLabel->SetAutoResize(true);
            capContainer->AddChild(capLabel);

            // Capability value
            auto capValue = std::make_shared<UltraCanvasLabel>(
                    "CapValue" + std::to_string(i), 1960 + i,
                    0, 30, 0, 0
            );
            capValue->SetText(capabilities[i].value);
            capValue->SetFontSize(12);
            capValue->SetFontWeight(FontWeight::Bold);
            capValue->SetTextColor(capabilities[i].isGreen
                                   ? Color(5, 150, 105, 255)     // Emerald 600
                                   : Color(234, 88, 12, 255));   // Orange 600
            capValue->SetAlignment(TextAlignment::Center);
            capValue->SetAutoResize(true);
            capContainer->AddChild(capValue);
        }

        // ===== ROW 3: TECHNICAL SPECIFICATIONS CARD (FULL WIDTH) =====
        auto techCard = std::make_shared<UltraCanvasContainer>("TechCard", 1980, leftColX, row3Y, 910, 170);
        techCard->SetBackgroundColor(Color(255, 255, 255, 255));
        techCard->SetBorders(1, Color(230, 230, 230, 255));
        container->AddChild(techCard);

        // Technical Specifications Title
        auto techTitle = std::make_shared<UltraCanvasLabel>("TechTitle", 1981, 20, 20, 300, 24);
        techTitle->SetText("Technical Specifications");
        techTitle->SetFontSize(16);
        techTitle->SetFontWeight(FontWeight::Bold);
        techTitle->SetTextColor(Color(30, 41, 59, 255));
        techCard->AddChild(techTitle);

        // Container Format Section Title
        auto containerTitle = std::make_shared<UltraCanvasLabel>("ContainerTitle", 1982, 20, 56, 200, 20);
        containerTitle->SetText("Container Format");
        containerTitle->SetFontSize(12);
        containerTitle->SetFontWeight(FontWeight::Bold);
        containerTitle->SetTextColor(Color(249, 115, 22, 255));  // Orange 500
        techCard->AddChild(containerTitle);

        // Container Format Items
        std::vector<std::string> containerItems = {
                "ISO Base Media File Format (ISOBMFF)",
                "MPEG-H Part 12 compliant",
                "Extensions: .heif, .heic, .heics, .avci"
        };

        for (size_t i = 0; i < containerItems.size(); ++i) {
            // Bullet point
            auto bullet = std::make_shared<UltraCanvasLabel>(
                    "ContainerBullet" + std::to_string(i), 1990 + i,
                    28, 80 + i * 24, 16, 16
            );
            bullet->SetText("●");
            bullet->SetFontSize(8);
            bullet->SetTextColor(Color(251, 146, 60, 255));  // Orange 400
            techCard->AddChild(bullet);

            // Item text
            auto itemLabel = std::make_shared<UltraCanvasLabel>(
                    "ContainerItem" + std::to_string(i), 2000 + i,
                    44, 78 + i * 24, 380, 18
            );
            itemLabel->SetText(containerItems[i]);
            itemLabel->SetFontSize(11);
            itemLabel->SetTextColor(Color(71, 85, 105, 255));  // Slate 600
            techCard->AddChild(itemLabel);
        }

        // Codec Support Section Title
        auto codecTitle = std::make_shared<UltraCanvasLabel>("CodecTitle", 2010, 470, 56, 200, 20);
        codecTitle->SetText("Codec Support");
        codecTitle->SetFontSize(12);
        codecTitle->SetFontWeight(FontWeight::Bold);
        codecTitle->SetTextColor(Color(249, 115, 22, 255));  // Orange 500
        techCard->AddChild(codecTitle);

        // Codec Support Items
        std::vector<std::string> codecItems = {
                "HEVC (H.265) - Primary codec",
                "AV1 - Emerging support",
                "JPEG 2000, H.264 compatible"
        };

        for (size_t i = 0; i < codecItems.size(); ++i) {
            // Bullet point
            auto bullet = std::make_shared<UltraCanvasLabel>(
                    "CodecBullet" + std::to_string(i), 2020 + i,
                    478, 80 + i * 24, 16, 16
            );
            bullet->SetText("●");
            bullet->SetFontSize(8);
            bullet->SetTextColor(Color(251, 191, 36, 255));  // Amber 400
            techCard->AddChild(bullet);

            // Item text
            auto itemLabel = std::make_shared<UltraCanvasLabel>(
                    "CodecItem" + std::to_string(i), 2030 + i,
                    494, 78 + i * 24, 380, 18
            );
            itemLabel->SetText(codecItems[i]);
            itemLabel->SetFontSize(11);
            itemLabel->SetTextColor(Color(71, 85, 105, 255));  // Slate 600
            techCard->AddChild(itemLabel);
        }

        return container;
    }

} // namespace UltraCanvas