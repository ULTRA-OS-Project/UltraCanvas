// Apps/DemoApp/UltraCanvasDemoBitmapExamples.cpp
// Enhanced bitmap image demonstrations for JPG and PNG formats
// Version: 1.3.0
// Last Modified: 2025-01-09
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasDropdown.h"
#include <sstream>
#include <iomanip>

namespace UltraCanvas {

// ===== HELPER FUNCTION FOR IMAGE INFO DISPLAY =====
    std::shared_ptr<UltraCanvasLabel> CreateImageInfoLabel(const std::string& id, int x, int y, const std::string& format, const std::string& details) {
        auto label = std::make_shared<UltraCanvasLabel>(id, 0, x, y, 350, 110);
        std::ostringstream info;
        info << "Format: " << format << "\n";
        info << details;
        label->SetText(info.str());
        label->SetFontSize(11);
        label->SetAlignment(TextAlignment::Left);
        label->SetBackgroundColor(Color(245, 245, 245, 255));
        label->SetBorderWidth(1.0f);
        label->SetPadding(8.0f);
        return label;
    }

// ===== PNG DEMO PAGE =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreatePNGExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("PNGDemoPage", 1510, 0, 0, 950, 600);
        ContainerStyle style;
        style.backgroundColor = Color(255, 255, 255, 255);
        //style.borderWidth = 2;
        container->SetContainerStyle(style);

        // Page Title
        auto title = std::make_shared<UltraCanvasLabel>("PNGTitle", 1511, 10, 10, 400, 35);
        title->SetText("PNG Format Demonstration");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        container->AddChild(title);

        // Format Description
        auto description = std::make_shared<UltraCanvasLabel>("PNGDesc", 1512, 10, 50, 930, 60);
        description->SetText("PNG (Portable Network Graphics) is a lossless image format that supports transparency. "
                             "It's ideal for logos, screenshots, and images with sharp edges or text. "
                             "PNG uses lossless compression, preserving all image data while reducing file size.");
        description->SetWordWrap(true);
        description->SetFontSize(12);
        description->SetAlignment(TextAlignment::Left);
        container->AddChild(description);

        // Image Display Area
        auto imageContainer = std::make_shared<UltraCanvasContainer>("PNGImageContainer", 1513, 10, 120, 450, 360);
        ContainerStyle imageContainerStyle;
        imageContainerStyle.backgroundColor = Color(240, 240, 240, 255);
        imageContainerStyle.borderWidth = 2;
        imageContainer->SetContainerStyle(imageContainerStyle);

        // Main PNG Image
        auto pngImage = std::make_shared<UltraCanvasImageElement>("PNGMainImage", 1514, 25, 25, 400, 300);
        pngImage->LoadFromFile("assets/images/transparent_overlay.png");
        pngImage->SetScaleMode(ImageScaleMode::Uniform);
        imageContainer->AddChild(pngImage);

        container->AddChild(imageContainer);

        // Image Properties Panel
        auto propsPanel = std::make_shared<UltraCanvasContainer>("PNGPropsPanel", 1515, 480, 120, 450, 360);

        auto propsTitle = std::make_shared<UltraCanvasLabel>("PNGPropsTitle", 1516, 10, 10, 250, 25);
        propsTitle->SetText("PNG Properties & Features");
        propsTitle->SetFontSize(14);
        propsTitle->SetFontWeight(FontWeight::Bold);
        propsPanel->AddChild(propsTitle);

        // Transparency Demonstration
        auto transTitle = std::make_shared<UltraCanvasLabel>("TransTitle", 1517, 10, 45, 250, 20);
        transTitle->SetText("Transparency Support:");
        transTitle->SetFontSize(12);
        propsPanel->AddChild(transTitle);

        // Background Pattern for Transparency Demo
        auto bgPattern = std::make_shared<UltraCanvasContainer>("BGPattern", 1518, 10, 70, 300, 100);
        ContainerStyle bgPatternStyle;
        bgPatternStyle.backgroundColor = Colors::Transparent;
        bgPattern->SetContainerStyle(bgPatternStyle);

        // Transparent PNG overlay
        auto transImage = std::make_shared<UltraCanvasImageElement>("TransPNG", 1519, 0, 0, 100, 100);
        transImage->LoadFromFile("assets/images/transparent_overlay.png");
        bgPattern->AddChild(transImage);

        auto notransImage = std::make_shared<UltraCanvasImageElement>("NoTransPNG", 1519, 120, 0, 100, 100);
        notransImage->LoadFromFile("assets/images/ship.jpg");
        bgPattern->AddChild(notransImage);

        propsPanel->AddChild(bgPattern);

        // Alpha Channel Control
        auto alphaLabel = std::make_shared<UltraCanvasLabel>("AlphaLabel", 1520, 10, 180, 100, 20);
        alphaLabel->SetText("Opacity:");
        alphaLabel->SetFontSize(12);
        propsPanel->AddChild(alphaLabel);

        auto alphaSlider = std::make_shared<UltraCanvasSlider>("AlphaSlider", 1521, 80, 180, 200, 25);
        alphaSlider->SetRange(0, 100);
        alphaSlider->SetValue(100);
        alphaSlider->onValueChanged = [pngImage, transImage](float value) {
            pngImage->SetOpacity(value / 100.0f);
            transImage->SetOpacity(value / 100.0f);
        };
        propsPanel->AddChild(alphaSlider);

        // Scale Mode Options
        auto scaleModeLabel = std::make_shared<UltraCanvasLabel>("ScaleModeLabel", 1522, 10, 215, 100, 20);
        scaleModeLabel->SetText("Scale Mode:");
        scaleModeLabel->SetFontSize(12);
        propsPanel->AddChild(scaleModeLabel);

        auto scaleModeDropdown = std::make_shared<UltraCanvasDropdown>("ScaleModeDropdown", 1523, 100, 215, 150, 25);
        scaleModeDropdown->AddItem("No Scale");
        scaleModeDropdown->AddItem("Stretch");
        scaleModeDropdown->AddItem("Uniform");
        scaleModeDropdown->AddItem("Uniform Fill");
        scaleModeDropdown->AddItem("Center");
        scaleModeDropdown->SetSelectedIndex(2); // Default to Uniform
        scaleModeDropdown->onSelectionChanged = [pngImage](int index, const DropdownItem& item) {
            pngImage->SetScaleMode(static_cast<ImageScaleMode>(index));
        };
        propsPanel->AddChild(scaleModeDropdown);

        // PNG Format Info
        auto formatInfo = CreateImageInfoLabel("PNGFormatInfo", 10, 250,
                                               "PNG (Portable Network Graphics)",
                                               "• Lossless compression\n"
                                               "• Full alpha channel support\n"
                                               "• 24-bit RGB / 32-bit RGBA\n"
                                               "• Ideal for: logos, icons, screenshots\n"
                                               "• Larger file size than JPEG\n"
                                               "• No quality loss on save");
        propsPanel->AddChild(formatInfo);

        container->AddChild(propsPanel);

        // Load Different PNG Examples
        auto examplesLabel = std::make_shared<UltraCanvasLabel>("ExamplesLabel", 1524, 10, 480, 200, 20);
        examplesLabel->SetText("PNG Examples:");
        examplesLabel->SetFontSize(12);
        examplesLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(examplesLabel);

        auto btnIcon = std::make_shared<UltraCanvasButton>("BtnIcon", 1525, 10, 505, 100, 30);
        btnIcon->SetText("Load Icon");
        btnIcon->onClick = [pngImage]() {
            pngImage->LoadFromFile("assets/images/png_68.png");
        };
        container->AddChild(btnIcon);

        auto btnLogo = std::make_shared<UltraCanvasButton>("BtnLogo", 1526, 120, 505, 100, 30);
        btnLogo->SetText("Load Logo");
        btnLogo->onClick = [pngImage]() {
            pngImage->LoadFromFile("assets/images/logo_transparent.png");
        };
        container->AddChild(btnLogo);

        auto btnScreenshot = std::make_shared<UltraCanvasButton>("BtnScreenshot", 1527, 230, 505, 150, 30);
        btnScreenshot->SetText("Load Screenshot");
        btnScreenshot->onClick = [pngImage]() {
            pngImage->LoadFromFile("assets/images/screenshot.png");
        };
        container->AddChild(btnScreenshot);

        return container;
    }

// ===== JPEG/JPG DEMO PAGE =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateJPEGExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("JPEGDemoPage", 1530, 0, 0, 950, 600);
        ContainerStyle containerStyle;
        containerStyle.backgroundColor = Color(200, 200, 200, 255);
        container->SetContainerStyle(containerStyle);

        // Page Title
        auto title = std::make_shared<UltraCanvasLabel>("JPEGTitle", 1531, 10, 10, 400, 35);
        title->SetText("JPEG/JPG Format Demonstration");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(150, 50, 50, 255));
        container->AddChild(title);

        // Format Description
        auto description = std::make_shared<UltraCanvasLabel>("JPEGDesc", 1532, 10, 50, 930, 60);
        description->SetText("JPEG (Joint Photographic Experts Group) is a lossy compression format optimized for photographs. "
                             "It achieves small file sizes by selectively discarding image data that's less noticeable to the human eye. "
                             "JPEG is ideal for photos and complex images with gradients but not for images with sharp edges or text.");
        description->SetWordWrap(true);
        description->SetFontSize(12);
        description->SetAlignment(TextAlignment::Left);
        container->AddChild(description);

        // Image Display Area
        auto imageContainer = std::make_shared<UltraCanvasContainer>("JPEGImageContainer", 1533, 10, 120, 450, 350);
        ContainerStyle imageContainerStyle;
        imageContainerStyle.backgroundColor = Color(240, 240, 240, 255);
        imageContainerStyle.borderWidth = 2.0f;
        container->SetContainerStyle(imageContainerStyle);

        // Main JPEG Image
        auto jpegImage = std::make_shared<UltraCanvasImageElement>("JPEGMainImage", 1534, 25, 25, 400, 300);
        jpegImage->LoadFromFile("assets/images/sample_photo.jpg");
        jpegImage->SetScaleMode(ImageScaleMode::Uniform);
        imageContainer->AddChild(jpegImage);

        container->AddChild(imageContainer);

        // Image Properties Panel
        auto propsPanel = std::make_shared<UltraCanvasContainer>("JPEGPropsPanel", 1535, 480, 120, 450, 350);

        auto propsTitle = std::make_shared<UltraCanvasLabel>("JPEGPropsTitle", 1536, 10, 10, 250, 25);
        propsTitle->SetText("JPEG Properties & Features");
        propsTitle->SetFontSize(14);
        propsTitle->SetFontWeight(FontWeight::Bold);
        propsPanel->AddChild(propsTitle);

        // Rotation Control
        auto rotationLabel = std::make_shared<UltraCanvasLabel>("RotationLabel", 1544, 10, 60, 100, 20);
        rotationLabel->SetText("Rotation:");
        rotationLabel->SetFontSize(12);
        propsPanel->AddChild(rotationLabel);

        auto rotationSlider = std::make_shared<UltraCanvasSlider>("RotationSlider", 1545, 80, 60, 200, 25);
        rotationSlider->SetRange(0, 360);
        rotationSlider->SetValue(0);
        rotationSlider->onValueChanged = [jpegImage](float value) {
            jpegImage->SetRotation(value);
        };
        propsPanel->AddChild(rotationSlider);

        auto rotationValue = std::make_shared<UltraCanvasLabel>("RotationValue", 1546, 285, 60, 50, 20);
        rotationValue->SetText("0°");
        rotationValue->SetFontSize(11);
        rotationSlider->onValueChanged = [jpegImage, rotationValue](float value) {
            jpegImage->SetRotation(value);
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(0) << value << "°";
            rotationValue->SetText(oss.str());
        };
        propsPanel->AddChild(rotationValue);

        // Scale Control
        auto scaleLabel = std::make_shared<UltraCanvasLabel>("ScaleLabel", 1547, 10, 95, 100, 20);
        scaleLabel->SetText("Scale:");
        scaleLabel->SetFontSize(12);
        propsPanel->AddChild(scaleLabel);

        auto scaleSlider = std::make_shared<UltraCanvasSlider>("ScaleSlider", 1548, 80, 95, 200, 25);
        scaleSlider->SetRange(50, 200);
        scaleSlider->SetValue(100);

        auto scaleValue = std::make_shared<UltraCanvasLabel>("ScaleValue", 1549, 285, 95, 50, 20);
        scaleValue->SetText("100%");
        scaleValue->SetFontSize(11);
        propsPanel->AddChild(scaleValue);

        scaleSlider->onValueChanged = [jpegImage, scaleValue](float value) {
            float scale = value / 100.0f;
            jpegImage->SetScale(scale, scale);
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(0) << value << "%";
            scaleValue->SetText(oss.str());
        };
        propsPanel->AddChild(scaleSlider);

        // JPEG Format Info
        auto formatInfo = CreateImageInfoLabel("JPEGFormatInfo", 10, 230,
                                               "JPEG/JPG (Joint Photographic Experts Group)",
                                               "• Lossy compression\n"
                                               "• No transparency support\n"
                                               "• 24-bit RGB color\n"
                                               "• Ideal for: photos, complex images\n"
                                               "• Smaller file size than PNG\n"
                                               "• Quality loss on each save");
        propsPanel->AddChild(formatInfo);

        container->AddChild(propsPanel);

        // Load Different JPEG Examples
        auto examplesLabel = std::make_shared<UltraCanvasLabel>("JPEGExamplesLabel", 1550, 10, 480, 200, 20);
        examplesLabel->SetText("JPEG Examples:");
        examplesLabel->SetFontSize(12);
        examplesLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(examplesLabel);

        auto btnPhoto = std::make_shared<UltraCanvasButton>("BtnPhoto", 1551, 10, 505, 100, 30);
        btnPhoto->SetText("Load Photo");
        btnPhoto->onClick = [jpegImage]() {
            jpegImage->LoadFromFile("assets/images/landscape.jpg");
        };
        container->AddChild(btnPhoto);

        auto btnPortrait = std::make_shared<UltraCanvasButton>("BtnPortrait", 1552, 120, 505, 100, 30);
        btnPortrait->SetText("Load Portrait");
        btnPortrait->onClick = [jpegImage]() {
            jpegImage->LoadFromFile("assets/images/portrait.jpg");
        };
        container->AddChild(btnPortrait);

        return container;
    }

// ===== MAIN BITMAP EXAMPLES FUNCTION (UPDATED) =====
//    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateBitmapExamples() {
//        auto container = std::make_shared<UltraCanvasContainer>("BitmapExamples", 1500, 0, 0, 1000, 600);
//
//        // Main Title
//        auto title = std::make_shared<UltraCanvasLabel>("BitmapTitle", 1501, 10, 10, 400, 30);
//        title->SetText("Bitmap Graphics Examples");
//        title->SetFontSize(16);
//        title->SetFontWeight(FontWeight::Bold);
//        container->AddChild(title);
//
//        // Create Tabbed Container for different bitmap formats
//        auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>("BitmapTabs", 1502, 10, 50, 980, 540);
//        tabbedContainer->SetTabPosition(TabPosition::Top);
//        tabbedContainer->SetTabStyle(TabStyle::Modern);
//
//        // Add PNG Demo Tab
//        auto pngPage = CreatePNGDemoPage();
//        tabbedContainer->AddTab("PNG Format", pngPage);
//
//        // Add JPEG Demo Tab
//        auto jpegPage = CreateJPEGDemoPage();
//        tabbedContainer->AddTab("JPEG/JPG Format", jpegPage);
//
//        // Set default active tab
//        tabbedContainer->SetActiveTab(0);
//
//        container->AddChild(tabbedContainer);
//
//        return container;
//    }

} // namespace UltraCanvas