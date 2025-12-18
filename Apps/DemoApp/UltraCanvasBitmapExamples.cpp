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

    void ShowFullSizeImageViewer(const std::string& imagePath);
//    std::string MeasureImagePerformance(const std::string& imagePath);
    std::string ExtractImageMetadata(const std::string& imagePath);

// ===== PERFORMANCE MEASUREMENT HELPER =====
//    std::string MeasureImagePerformance(const std::string& imagePath) {
//        std::ostringstream result;
//
//        try {
//            // Measure Load Time (×100 iterations)
//            auto loadStart = std::chrono::high_resolution_clock::now();
//            for (int i = 0; i < 100; i++) {
//                auto image = UCImage::Get(imagePath);
//            }
//            auto loadEnd = std::chrono::high_resolution_clock::now();
//            auto loadDuration = std::chrono::duration_cast<std::chrono::milliseconds>(loadEnd - loadStart);
//
//            // Measure Display Time (×100 iterations)
//            auto displayStart = std::chrono::high_resolution_clock::now();
//            for (int i = 0; i < 100; i++) {
//                auto image = UCImage::Get(imagePath);
//                // Simulate display operations
//                int width = image->GetWidth();
//                int height = image->GetHeight();
//            }
//            auto displayEnd = std::chrono::high_resolution_clock::now();
//            auto displayDuration = std::chrono::duration_cast<std::chrono::milliseconds>(displayEnd - displayStart);
//
//            result << "Performance Metrics (×100):\n";
//            result << "Load Time: " << loadDuration.count() << "ms\n";
//            result << "Display Time: " << displayDuration.count() << "ms\n";
//            result << "Avg Load: " << (loadDuration.count() / 100.0) << "ms per image";
//
//        } catch (const std::exception& e) {
//            result << "Performance measurement failed:\n" << e.what();
//        }
//
//        return result.str();
//    }

// ===== METADATA EXTRACTION HELPER =====
    std::string ExtractImageMetadata(const std::string& imagePath) {
        std::ostringstream metadata;

        try {
            // Get file size using std::filesystem
            std::ifstream file(imagePath, std::ios::binary | std::ios::ate);
            if (file.is_open()) {
                std::streamsize fileSize = file.tellg();
                file.close();

                double fileSizeKB = fileSize / 1024.0;
                double fileSizeMB = fileSizeKB / 1024.0;

                if (fileSizeMB >= 1.0) {
                    metadata << "File Size: " << std::fixed << std::setprecision(2) << fileSizeMB << " MB\n";
                } else {
                    metadata << "File Size: " << std::fixed << std::setprecision(2) << fileSizeKB << " KB\n";
                }
            } else {
                metadata << "File not found\n";
                return metadata.str();
            }

            // ===== USE LIBVIPS DIRECTLY =====
            vips::VImage vipsImage = vips::VImage::new_from_file(imagePath.c_str());

            // Image dimensions
            metadata << "Resolution: " << vipsImage.width() << "×" << vipsImage.height() << "\n";

            // Number of bands (channels)
            int bands = vipsImage.bands();
            metadata << "Bands/Channels: " << bands << "\n";

            // Color interpretation
            VipsInterpretation interpretation = vipsImage.interpretation();
            metadata << "Color Space: ";
            switch (interpretation) {
                case VIPS_INTERPRETATION_ERROR:
                    metadata << "Error";
                    break;
                case VIPS_INTERPRETATION_MULTIBAND:
                    metadata << "Multiband";
                    break;
                case VIPS_INTERPRETATION_B_W:
                    metadata << "B&W (1-band)";
                    break;
                case VIPS_INTERPRETATION_RGB:
                    metadata << "RGB";
                    break;
                case VIPS_INTERPRETATION_sRGB:
                    metadata << "sRGB";
                    break;
                case VIPS_INTERPRETATION_CMYK:
                    metadata << "CMYK";
                    break;
                case VIPS_INTERPRETATION_LAB:
                    metadata << "LAB";
                    break;
                case VIPS_INTERPRETATION_HSV:
                    metadata << "HSV";
                    break;
                case VIPS_INTERPRETATION_GREY16:
                    metadata << "Grey16";
                    break;
                case VIPS_INTERPRETATION_RGB16:
                    metadata << "RGB16";
                    break;
                default:
                    metadata << "Other (" << interpretation << ")";
                    break;
            }
            metadata << "\n";

            // Bit depth per band
            VipsBandFormat format = vipsImage.format();
            int bitsPerBand = 0;
            std::string formatName;

            switch (format) {
                case VIPS_FORMAT_UCHAR:
                    bitsPerBand = 8;
                    formatName = "8-bit unsigned";
                    break;
                case VIPS_FORMAT_CHAR:
                    bitsPerBand = 8;
                    formatName = "8-bit signed";
                    break;
                case VIPS_FORMAT_USHORT:
                    bitsPerBand = 16;
                    formatName = "16-bit unsigned";
                    break;
                case VIPS_FORMAT_SHORT:
                    bitsPerBand = 16;
                    formatName = "16-bit signed";
                    break;
                case VIPS_FORMAT_UINT:
                    bitsPerBand = 32;
                    formatName = "32-bit unsigned";
                    break;
                case VIPS_FORMAT_INT:
                    bitsPerBand = 32;
                    formatName = "32-bit signed";
                    break;
                case VIPS_FORMAT_FLOAT:
                    bitsPerBand = 32;
                    formatName = "32-bit float";
                    break;
                case VIPS_FORMAT_DOUBLE:
                    bitsPerBand = 64;
                    formatName = "64-bit double";
                    break;
                default:
                    formatName = "Unknown";
                    break;
            }

            metadata << "Format: " << formatName << "\n";
            metadata << "Bit Depth: " << (bitsPerBand * bands) << "-bit total\n";

            // Check for alpha channel
            bool hasAlpha = vipsImage.has_alpha();
            metadata << "Alpha Channel: " << (hasAlpha ? "Yes" : "No") << "\n";

            // Try to get EXIF orientation if available
            try {
                if (vipsImage.get_typeof("orientation") != 0) {
                    int orientation = vipsImage.get_int("orientation");
                    metadata << "EXIF Orientation: " << orientation << "\n";
                }
            } catch (...) {
                // Orientation not available
            }

            // Try to get DPI/resolution
            try {
                if (vipsImage.get_typeof("xres") != 0) {
                    double xres = vipsImage.get_double("xres");
                    double yres = vipsImage.get_double("yres");
                    metadata << "DPI: " << std::fixed << std::setprecision(0)
                             << (xres * 25.4) << "×" << (yres * 25.4) << "\n";
                }
            } catch (...) {
                // Resolution not available
            }

            // Try to get EXIF data
            try {
                if (vipsImage.get_typeof("exif-data") != 0) {
                    metadata << "EXIF Data: Present\n";
                }
            } catch (...) {
                metadata << "EXIF Data: Not available\n";
            }

            // Try to get GPS location
            try {
                if (vipsImage.get_typeof("exif-ifd2-GPSLatitude") != 0) {
                    metadata << "GPS Location: Available\n";
                }
            } catch (...) {
                // GPS data not available
            }

        } catch (const vips::VError& e) {
            metadata << "Error loading image:\n" << e.what();
        } catch (const std::exception& e) {
            metadata << "Metadata extraction failed:\n" << e.what();
        }

        return metadata.str();
    }

// ===== FULL-SIZE IMAGE VIEWER (Modal Window) =====
    void ShowFullSizeImageViewer(const std::string& imagePath) {
        // TODO: Implement modal window with full-size image display
        // This would create a new window or modal overlay showing the full image
        // with zoom controls and close button
        // For now, this is a placeholder that demonstrates the concept
    }




    // ===== HELPER FUNCTION FOR IMAGE INFO DISPLAY =====
    std::shared_ptr<UltraCanvasLabel> CreateImageInfoLabel(const std::string& id, int x, int y, const std::string& format, const std::string& details) {
        auto label = std::make_shared<UltraCanvasLabel>(id, 0, x, y, 0, 0);
        std::ostringstream info;
        info << "Format: " << format << "\n";
        info << details;
        label->SetText(info.str());
        label->SetFontSize(11);
        label->SetAlignment(TextAlignment::Left);
        label->SetBackgroundColor(Color(245, 245, 245, 255));
        label->SetBorders(1.0f);
        label->SetPadding(8.0f);
        label->SetAutoResize(true);
        return label;
    }

// ===== PNG DEMO PAGE =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreatePNGExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("PNGDemoPage", 1510, 0, 0, 950, 800);
        container->SetBackgroundColor(Color(255, 255, 255, 255));

        // Page Title
        auto title = std::make_shared<UltraCanvasLabel>("PNGTitle", 1511, 10, 10, 600, 35);
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
        imageContainer->SetBackgroundColor(Color(240, 240, 240, 255));
        imageContainer->SetBorders(2, Color(200, 200, 200, 255));

        // Main PNG Image
        auto pngImage = std::make_shared<UltraCanvasImageElement>("PNGMainImage", 1514, 25, 25, 400, 300);
        pngImage->LoadFromFile("media/images/transparent_overlay.png");
        pngImage->SetFitMode(ImageFitMode::Contain);
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
        // Transparent PNG overlay
        auto notransImage = std::make_shared<UltraCanvasImageElement>("NoTransPNG", 1519, 0, 0, 100, 100);
        notransImage->LoadFromFile("media/images/ship.jpg");
        notransImage->SetFitMode(ImageFitMode::Fill);

        auto transImage = std::make_shared<UltraCanvasImageElement>("TransPNG", 1519, 0, 0, 100, 100);
        transImage->LoadFromFile("media/images/transparent_overlay.png");
        bgPattern->AddChild(notransImage);
        bgPattern->AddChild(transImage);


        propsPanel->AddChild(bgPattern);

        // Alpha Channel Control
        auto alphaLabel = std::make_shared<UltraCanvasLabel>("AlphaLabel", 1520, 25, 490, 100, 20);
        alphaLabel->SetText("Opacity:");
        alphaLabel->SetFontSize(12);
        container->AddChild(alphaLabel);

        auto alphaSlider = std::make_shared<UltraCanvasSlider>("AlphaSlider", 1521, 125, 490, 200, 25);
        alphaSlider->SetRange(0, 100);
        alphaSlider->SetValue(100);
        alphaSlider->onValueChanged = [pngImage, transImage](float value) {
            pngImage->SetOpacity(value / 100.0f);
            transImage->SetOpacity(value / 100.0f);
        };
        container->AddChild(alphaSlider);

        // Scale Mode Options
        auto scaleModeLabel = std::make_shared<UltraCanvasLabel>("ScaleModeLabel", 1522, 25, 525, 100, 20);
        scaleModeLabel->SetText("Fit Mode:");
        scaleModeLabel->SetFontSize(12);
        container->AddChild(scaleModeLabel);

        auto scaleModeDropdown = std::make_shared<UltraCanvasDropdown>("ScaleModeDropdown", 1523, 125, 525, 150, 25);
        scaleModeDropdown->AddItem("No Scale");
        scaleModeDropdown->AddItem("Contain");
        scaleModeDropdown->AddItem("Cover");
        scaleModeDropdown->AddItem("Fill");
        scaleModeDropdown->AddItem("Scale Down");
        scaleModeDropdown->SetSelectedIndex(2); // Default to Uniform
        scaleModeDropdown->onSelectionChanged = [pngImage](int index, const DropdownItem& item) {
            pngImage->SetFitMode(static_cast<ImageFitMode>(index));
        };
        container->AddChild(scaleModeDropdown);

        // PNG Format Info
        auto formatInfo = CreateImageInfoLabel("PNGFormatInfo", 10, 190,
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
        auto examplesLabel = std::make_shared<UltraCanvasLabel>("ExamplesLabel", 1524, 10, 595, 200, 20);
        examplesLabel->SetText("PNG Examples:");
        examplesLabel->SetFontSize(12);
        examplesLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(examplesLabel);

        auto btnIcon = std::make_shared<UltraCanvasButton>("BtnIcon", 1525, 10, 620, 100, 30);
        btnIcon->SetText("Load Icon");
        btnIcon->onClick = [pngImage]() {
            pngImage->LoadFromFile("media/images/png_68.png");
        };
        container->AddChild(btnIcon);

        auto btnLogo = std::make_shared<UltraCanvasButton>("BtnLogo", 1526, 120, 620, 100, 30);
        btnLogo->SetText("Load Logo");
        btnLogo->onClick = [pngImage]() {
            pngImage->LoadFromFile("media/images/logo_transparent.png");
        };
        container->AddChild(btnLogo);

        auto btnScreenshot = std::make_shared<UltraCanvasButton>("BtnScreenshot", 1527, 230, 620, 150, 30);
        btnScreenshot->SetText("Load Screenshot");
        btnScreenshot->onClick = [pngImage]() {
            pngImage->LoadFromFile("media/images/screenshot.png");
        };
        container->AddChild(btnScreenshot);

        return container;
    }

// ===== JPEG/JPG DEMO PAGE =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateJPEGExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("JPEGDemoPage", 1530, 0, 0, 950, 560);
        //container->SetBackgroundColor(Colors::LightGray);

        // Page Title
        auto title = std::make_shared<UltraCanvasLabel>("JPEGTitle", 1531, 20, 10, 600, 35);
        title->SetText("JPEG/JPG Format Demonstration");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(150, 50, 50, 255));
        container->AddChild(title);

        // Format Description
        auto description = std::make_shared<UltraCanvasLabel>("JPEGDesc", 1532, 20, 50, 920, 60);
        description->SetText("JPEG (Joint Photographic Experts Group) is a lossy compression format optimized for photographs. "
                             "It achieves small file sizes by selectively discarding image data that's less noticeable to the human eye. "
                             "JPEG is ideal for photos and complex images with gradients but not for images with sharp edges or text.");
        description->SetWordWrap(true);
        description->SetFontSize(12);
        description->SetAlignment(TextAlignment::Left);
        container->AddChild(description);

        // Image Display Area
        auto imageContainer = std::make_shared<UltraCanvasContainer>("JPEGImageContainer", 1533, 20, 120, 450, 350);
//        container->SetBorders(2, Colors::LightGray);
//        container->SetBackgroundColor(Color(240, 240, 240, 255));

        // Main JPEG Image
        auto jpegImage = std::make_shared<UltraCanvasImageElement>("JPEGMainImage", 1534, 0, 25, 420, 320);
        jpegImage->LoadFromFile("media/images/sample_photo.jpg");
        jpegImage->SetFitMode(ImageFitMode::Contain);
        imageContainer->AddChild(jpegImage);

        container->AddChild(imageContainer);

        // Image Properties Panel
        auto propsPanel = std::make_shared<UltraCanvasContainer>("JPEGPropsPanel", 1535, 480, 120, 450, 400);

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
        auto formatInfo = CreateImageInfoLabel("JPEGFormatInfo", 10, 180,
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
        auto examplesLabel = std::make_shared<UltraCanvasLabel>("JPEGExamplesLabel", 1550, 20, 480, 200, 20);
        examplesLabel->SetText("JPEG Examples:");
        examplesLabel->SetFontSize(12);
        examplesLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(examplesLabel);

        auto btnPhoto = std::make_shared<UltraCanvasButton>("BtnPhoto", 1551, 20, 505, 120, 30);
        btnPhoto->SetText("Load Photo");
        btnPhoto->onClick = [jpegImage]() {
            jpegImage->LoadFromFile("media/images/landscape.jpg");
        };
        container->AddChild(btnPhoto);

        auto btnPortrait = std::make_shared<UltraCanvasButton>("BtnPortrait", 1552, 160, 505, 120, 30);
        btnPortrait->SetText("Load Portrait");
        btnPortrait->onClick = [jpegImage]() {
            jpegImage->LoadFromFile("media/images/portrait.jpg");
        };
        container->AddChild(btnPortrait);

        return container;
    }



// ===== AVIF FORMAT EXAMPLES =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateAVIFExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("AVIFDemoPage", 1600, 0, 0, 950, 800);
        container->SetBackgroundColor(Color(255, 255, 255, 255));

        // Page Title
        auto title = std::make_shared<UltraCanvasLabel>("AVIFTitle", 1601, 10, 10, 600, 35);
        title->SetText("AVIF Format Demonstration");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(70, 130, 180, 255));
        container->AddChild(title);

        // Format Description
        auto description = std::make_shared<UltraCanvasLabel>("AVIFDesc", 1602, 10, 50, 930, 80);
        description->SetText(
                "AVIF (AV1 Image File Format) is a modern image format based on the AV1 video codec. "
                "It provides significantly better compression than JPEG and WebP while maintaining high quality. "
                "AVIF supports HDR, wide color gamuts, and transparency, making it ideal for next-generation web images. "
                "File sizes are typically 50% smaller than JPEG at equivalent quality.");
        description->SetAlignment(TextAlignment::Left);
        //description->SetTextWrapping(true);
        description->SetFontSize(11);
        container->AddChild(description);

        // Image Display Area
        auto avifImage = std::make_shared<UltraCanvasImageElement>("AVIFImage", 1603, 20, 140, 450, 350);
        avifImage->LoadFromFile("media/images/Cat.avif");
        avifImage->SetFitMode(ImageFitMode::Contain);
        avifImage->SetBorders(1.0f, Color(200, 200, 200, 255));
        avifImage->SetBackgroundColor(Color(240, 240, 240, 255));

        // ✨ Hand cursor and clickable
        avifImage->SetMousePointer(MousePointer::Hand);
        avifImage->SetClickable(true);
        avifImage->onClick = []() {
            ShowFullSizeImageViewer("media/Cat.avif");
        };

        container->AddChild(avifImage);

//        // Performance Metrics Panel
//        auto perfPanel = std::make_shared<UltraCanvasLabel>("AVIFPerf", 1604, 490, 140, 440, 120);
//        perfPanel->SetText(MeasureImagePerformance("media/Cat.avif"));
//        perfPanel->SetAlignment(TextAlignment::Left);
//        perfPanel->SetBackgroundColor(Color(250, 250, 250, 255));
//        perfPanel->SetBorders(1.0f);
//        perfPanel->SetPadding(10.0f);
//        perfPanel->SetFontSize(10);
//        container->AddChild(perfPanel);

        // Metadata Panel
        auto metadataPanel = std::make_shared<UltraCanvasLabel>("AVIFMetadata", 1605, 490, 140, 440, 180);
        metadataPanel->SetText(ExtractImageMetadata("media/images/Cat.avif"));
        metadataPanel->SetAlignment(TextAlignment::Left);
        metadataPanel->SetBackgroundColor(Color(245, 250, 255, 255));
        metadataPanel->SetBorders(1.0f);
        metadataPanel->SetPadding(10.0f);
        metadataPanel->SetFontSize(10);
        container->AddChild(metadataPanel);

        // Format Options Panel
        auto optionsPanel = std::make_shared<UltraCanvasLabel>("AVIFOptions", 1606, 490, 400, 440, 90);
        optionsPanel->SetText(
                "Format Options:\n"
                "• Compression: Lossy/Lossless\n"
                "• Quality Range: 0-100\n"
                "• Alpha Channel: Supported\n"
                "• HDR: 10-bit/12-bit support\n"
                "• Animation: Supported");
        optionsPanel->SetAlignment(TextAlignment::Left);
        optionsPanel->SetBackgroundColor(Color(255, 250, 245, 255));
        optionsPanel->SetBorders(1.0f);
        optionsPanel->SetPadding(10.0f);
        optionsPanel->SetFontSize(10);
        container->AddChild(optionsPanel);

        // Format Info
        auto formatInfo = CreateImageInfoLabel("AVIFFormatInfo", 20, 500,
                                               "AVIF (AV1 Image File Format)",
                                               "• Next-generation codec\n"
                                               "• 50% smaller than JPEG\n"
                                               "• HDR and wide color gamut\n"
                                               "• Transparency support\n"
                                               "• Ideal for: web images, photos\n"
                                               "• Browser support: Chrome, Firefox, Safari");
        container->AddChild(formatInfo);

        return container;
    }

// ===== WEBP FORMAT EXAMPLES =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateWEBPExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("WEBPDemoPage", 1700, 0, 0, 950, 800);
        container->SetBackgroundColor(Color(255, 255, 255, 255));

        // Page Title
        auto title = std::make_shared<UltraCanvasLabel>("WEBPTitle", 1701, 10, 10, 600, 35);
        title->SetText("WEBP Format Demonstration");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(34, 139, 34, 255));
        container->AddChild(title);

        // Format Description
        auto description = std::make_shared<UltraCanvasLabel>("WEBPDesc", 1702, 10, 50, 930, 80);
        description->SetText(
                "WebP is a modern image format developed by Google that provides superior compression for images on the web. "
                "It supports both lossy and lossless compression, as well as transparency and animation. "
                "WebP files are typically 25-35% smaller than JPEG and PNG while maintaining comparable quality. "
                "Ideal for web applications where bandwidth and loading speed are critical.");
        description->SetAlignment(TextAlignment::Left);
        //description->SetTextWrapping(true);
        description->SetFontSize(11);
        container->AddChild(description);

        // Image Display Area
        auto webpImage = std::make_shared<UltraCanvasImageElement>("WEBPImage", 1703, 20, 140, 450, 350);
        webpImage->LoadFromFile("media/images/Kindigs-1953-Corvette-TwelveAir.webp");
        webpImage->SetFitMode(ImageFitMode::Contain);
        webpImage->SetBorders(1.0f, Color(200, 200, 200, 255));
        webpImage->SetBackgroundColor(Color(240, 240, 240, 255));

        // ✨ Hand cursor and clickable
        webpImage->SetMousePointer(MousePointer::Hand);
        webpImage->SetClickable(true);
        webpImage->onClick = []() {
            ShowFullSizeImageViewer("media/images/Kindigs-1953-Corvette-TwelveAir.webp");
        };

        container->AddChild(webpImage);

        // Performance Metrics Panel
//        auto perfPanel = std::make_shared<UltraCanvasLabel>("WEBPPerf", 1704, 490, 140, 440, 120);
//        perfPanel->SetText(MeasureImagePerformance("media/Kindigs-1953-Corvette-TwelveAir.webp"));
//        perfPanel->SetAlignment(TextAlignment::Left);
//        perfPanel->SetBackgroundColor(Color(250, 250, 250, 255));
//        perfPanel->SetBorders(1.0f);
//        perfPanel->SetPadding(10.0f);
//        perfPanel->SetFontSize(10);
//        container->AddChild(perfPanel);

        // Metadata Panel
        auto metadataPanel = std::make_shared<UltraCanvasLabel>("WEBPMetadata", 1705, 490, 140, 440, 180);
        metadataPanel->SetText(ExtractImageMetadata("media/images/Kindigs-1953-Corvette-TwelveAir.webp"));
        metadataPanel->SetAlignment(TextAlignment::Left);
        metadataPanel->SetBackgroundColor(Color(245, 250, 255, 255));
        metadataPanel->SetBorders(1.0f);
        metadataPanel->SetPadding(10.0f);
        metadataPanel->SetFontSize(10);
        container->AddChild(metadataPanel);

        // Format Options Panel
        auto optionsPanel = std::make_shared<UltraCanvasLabel>("WEBPOptions", 1706, 490, 400, 440, 90);
        optionsPanel->SetText(
                "Format Options:\n"
                "• Compression: Lossy/Lossless\n"
                "• Quality Range: 0-100\n"
                "• Alpha Channel: Supported\n"
                "• Animation: Supported\n"
                "• Metadata: EXIF, XMP, ICC");
        optionsPanel->SetAlignment(TextAlignment::Left);
        optionsPanel->SetBackgroundColor(Color(255, 250, 245, 255));
        optionsPanel->SetBorders(1.0f);
        optionsPanel->SetPadding(10.0f);
        optionsPanel->SetFontSize(10);
        container->AddChild(optionsPanel);

        // Format Info
        auto formatInfo = CreateImageInfoLabel("WEBPFormatInfo", 20, 500,
                                               "WEBP (Web Picture Format)",
                                               "• Google's modern format\n"
                                               "• 25-35% smaller than JPEG/PNG\n"
                                               "• Lossy and lossless modes\n"
                                               "• Transparency and animation\n"
                                               "• Ideal for: web images, responsive design\n"
                                               "• Wide browser support");
        container->AddChild(formatInfo);

        return container;
    }

// ===== HEIF FORMAT EXAMPLES =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateHEIFExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("HEIFDemoPage", 1800, 0, 0, 950, 800);
        container->SetBackgroundColor(Color(255, 255, 255, 255));

        // Page Title
        auto title = std::make_shared<UltraCanvasLabel>("HEIFTitle", 1801, 10, 10, 400, 35);
        title->SetText("HEIF/HEIC Format Demonstration");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(255, 140, 0, 255));
        title->SetAutoResize(true);
        container->AddChild(title);

        // Format Description
        auto description = std::make_shared<UltraCanvasLabel>("HEIFDesc", 1802, 10, 50, 930, 80);
        description->SetText(
                "HEIF/HEIC (High Efficiency Image Format) is an image container format based on HEVC (H.265) video compression. "
                "It provides superior compression efficiency compared to JPEG while maintaining high image quality. "
                "HEIF supports features like image sequences, transparency, depth maps, and HDR. "
                "This format is used by default on Apple devices since iOS 11 and macOS High Sierra.");
        description->SetAlignment(TextAlignment::Left);
        //description->SetTextWrapping(true);
        description->SetFontSize(11);
        container->AddChild(description);

        // Image Display Area
        auto heifImage = std::make_shared<UltraCanvasImageElement>("HEIFImage", 1803, 20, 140, 450, 350);
        heifImage->LoadFromFile("media/images/fantasycutemonster.heif");
        heifImage->SetFitMode(ImageFitMode::Contain);
        heifImage->SetBorders(1.0f, Color(200, 200, 200, 255));
        heifImage->SetBackgroundColor(Color(240, 240, 240, 255));

        // ✨ Hand cursor and clickable
        heifImage->SetMousePointer(MousePointer::Hand);
        heifImage->SetClickable(true);
        heifImage->onClick = []() {
            ShowFullSizeImageViewer("media/images/fantasycutemonster.heif");
        };

        container->AddChild(heifImage);

        // Performance Metrics Panel
//        auto perfPanel = std::make_shared<UltraCanvasLabel>("HEIFPerf", 1804, 490, 140, 440, 120);
//        perfPanel->SetText(MeasureImagePerformance("media/fantasycutemonster.heif"));
//        perfPanel->SetAlignment(TextAlignment::Left);
//        perfPanel->SetBackgroundColor(Color(250, 250, 250, 255));
//        perfPanel->SetBorders(1.0f);
//        perfPanel->SetPadding(10.0f);
//        perfPanel->SetFontSize(10);
//        container->AddChild(perfPanel);

        // Metadata Panel
        auto metadataPanel = std::make_shared<UltraCanvasLabel>("HEIFMetadata", 1805, 490, 140, 440, 180);
        metadataPanel->SetText(ExtractImageMetadata("media/images/fantasycutemonster.heif"));
        metadataPanel->SetAlignment(TextAlignment::Left);
        metadataPanel->SetBackgroundColor(Color(245, 250, 255, 255));
        metadataPanel->SetBorders(1.0f);
        metadataPanel->SetPadding(10.0f);
        metadataPanel->SetFontSize(10);
        container->AddChild(metadataPanel);

        // Format Options Panel
        auto optionsPanel = std::make_shared<UltraCanvasLabel>("HEIFOptions", 1806, 490, 400, 440, 90);
        optionsPanel->SetText(
                "Format Options:\n"
                "• Compression: HEVC-based\n"
                "• Quality: High efficiency\n"
                "• Alpha Channel: Supported\n"
                "• Image Sequences: Supported\n"
                "• Depth Maps: Supported\n"
                "• HDR: 10-bit support");
        optionsPanel->SetAlignment(TextAlignment::Left);
        optionsPanel->SetBackgroundColor(Color(255, 250, 245, 255));
        optionsPanel->SetBorders(1.0f);
        optionsPanel->SetPadding(10.0f);
        optionsPanel->SetFontSize(10);
        container->AddChild(optionsPanel);

        // Format Info
        auto formatInfo = CreateImageInfoLabel("HEIFFormatInfo", 20, 500,
                                               "HEIF (High Efficiency Image Format)",
                                               "• HEVC-based compression\n"
                                               "• 50% smaller than JPEG\n"
                                               "• HDR and depth map support\n"
                                               "• Image sequences\n"
                                               "• Ideal for: mobile photography\n"
                                               "• Default on Apple devices");
        container->AddChild(formatInfo);

        return container;
    }

// ===== GIF FORMAT EXAMPLES =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateGIFExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("GIFDemoPage", 2000, 0, 0, 950, 800);
        container->SetBackgroundColor(Color(255, 255, 255, 255));

        // Page Title
        auto title = std::make_shared<UltraCanvasLabel>("GIFTitle", 2001, 10, 10, 400, 35);
        title->SetText("GIF Format Demonstration");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(138, 43, 226, 255));
        title->SetAutoResize(true);
        container->AddChild(title);

        // Format Description
        auto description = std::make_shared<UltraCanvasLabel>("GIFDesc", 2002, 10, 50, 930, 80);
        description->SetText(
                "GIF (Graphics Interchange Format) is one of the oldest image formats, introduced in 1987. "
                "It supports animation and transparency through 8-bit indexed color (256 colors max). "
                "While limited in color depth, GIF remains popular for simple animations, logos, and icons. "
                "Modern formats like AVIF and WebP offer better quality and smaller file sizes for animations, "
                "but GIF maintains universal compatibility across all platforms and browsers.");
        description->SetAlignment(TextAlignment::Left);
        //description->SetTextWrapping(true);
        description->SetFontSize(11);
        container->AddChild(description);

        // Image Display Area (animated GIF generated from fordGT.jpg)
        auto gifImage = std::make_shared<UltraCanvasImageElement>("GIFImage", 2003, 20, 140, 450, 350);
        // Note: In real implementation, would generate animated GIF from fordGT.jpg
        gifImage->LoadFromFile("media/images/images/fordGT_animated.gif");
        gifImage->SetFitMode(ImageFitMode::Contain);
        gifImage->SetBorders(1.0f, Color(200, 200, 200, 255));
        gifImage->SetBackgroundColor(Color(240, 240, 240, 255));

        // ✨ Hand cursor and clickable
        gifImage->SetMousePointer(MousePointer::Hand);
        gifImage->SetClickable(true);
        gifImage->onClick = []() {
            ShowFullSizeImageViewer("media/images/fordGT_animated.gif");
        };

        container->AddChild(gifImage);

        // Performance Metrics Panel
//        auto perfPanel = std::make_shared<UltraCanvasLabel>("GIFPerf", 2004, 490, 140, 440, 120);
//        perfPanel->SetText(
//                "Performance Metrics (×100):\n"
//                "Load Time: ~150ms\n"
//                "Display Time: ~80ms\n"
//                "Animation: 24 FPS\n"
//                "Note: Generated from fordGT.jpg");
//        perfPanel->SetAlignment(TextAlignment::Left);
//        perfPanel->SetBackgroundColor(Color(250, 250, 250, 255));
//        perfPanel->SetBorders(1.0f);
//        perfPanel->SetPadding(10.0f);
//        perfPanel->SetFontSize(10);
//        container->AddChild(perfPanel);

        // Metadata Panel
        auto metadataPanel = std::make_shared<UltraCanvasLabel>("GIFMetadata", 2005, 490, 140, 440, 180);
        metadataPanel->SetText(
                "Format Metadata:\n"
                "File Size: Variable (animation-dependent)\n"
                "Color Depth: 8-bit (256 colors)\n"
                "Frames: Multiple (animation)\n"
                "Transparency: 1-bit (on/off)\n"
                "Loop: Infinite support");
        metadataPanel->SetAlignment(TextAlignment::Left);
        metadataPanel->SetBackgroundColor(Color(245, 250, 255, 255));
        metadataPanel->SetBorders(1.0f);
        metadataPanel->SetPadding(10.0f);
        metadataPanel->SetFontSize(10);
        container->AddChild(metadataPanel);

        // Format Options Panel
        auto optionsPanel = std::make_shared<UltraCanvasLabel>("GIFOptions", 2006, 490, 400, 440, 90);
        optionsPanel->SetText(
                "Format Options:\n"
                "• Compression: LZW (lossless)\n"
                "• Color Palette: 256 colors max\n"
                "• Transparency: 1-bit alpha\n"
                "• Animation: Multi-frame support\n"
                "• Interlacing: Progressive display\n"
                "• Loop Count: Configurable");
        optionsPanel->SetAlignment(TextAlignment::Left);
        optionsPanel->SetBackgroundColor(Color(255, 250, 245, 255));
        optionsPanel->SetBorders(1.0f);
        optionsPanel->SetPadding(10.0f);
        optionsPanel->SetFontSize(10);
        container->AddChild(optionsPanel);

        // Format Info
        auto formatInfo = CreateImageInfoLabel("GIFFormatInfo", 20, 500,
                                               "GIF (Graphics Interchange Format)",
                                               "• Introduced in 1987\n"
                                               "• 256 color limitation\n"
                                               "• Animation support\n"
                                               "• 1-bit transparency\n"
                                               "• Ideal for: simple animations, icons\n"
                                               "• Universal compatibility");
        container->AddChild(formatInfo);

        return container;
    }

// ===== TIFF FORMAT EXAMPLES =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTIFFExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("TIFFDemoPage", 2100, 0, 0, 950, 800);
        container->SetBackgroundColor(Color(255, 255, 255, 255));

        // Page Title
        auto title = std::make_shared<UltraCanvasLabel>("TIFFTitle", 2101, 10, 10, 400, 35);
        title->SetText("TIFF Format Demonstration");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(184, 134, 11, 255));
        title->SetAutoResize(true);
        container->AddChild(title);

        // Format Description
        auto description = std::make_shared<UltraCanvasLabel>("TIFFDesc", 2102, 10, 50, 930, 80);
        description->SetText(
                "TIFF (Tagged Image File Format) is a flexible, adaptable file format for handling images and data. "
                "It's widely used in professional photography, desktop publishing, and archival applications. "
                "TIFF supports multiple compression schemes (including none), various color depths, and metadata. "
                "While file sizes are typically large, TIFF preserves maximum image quality and detail, "
                "making it ideal for professional workflows and archival purposes.");
        description->SetAlignment(TextAlignment::Left);
        //description->SetTextWrapping(true);
        description->SetFontSize(11);
        container->AddChild(description);

        // Image Display Area (converted from artist.jpg)
        auto tiffImage = std::make_shared<UltraCanvasImageElement>("TIFFImage", 2103, 20, 140, 450, 350);
        // Note: In real implementation, would convert artist.jpg to TIFF
        tiffImage->LoadFromFile("media/images/artist_converted.tiff");
        tiffImage->SetFitMode(ImageFitMode::Contain);
        tiffImage->SetBorders(1.0f, Color(200, 200, 200, 255));
        tiffImage->SetBackgroundColor(Color(240, 240, 240, 255));

        // ✨ Hand cursor and clickable
        tiffImage->SetMousePointer(MousePointer::Hand);
        tiffImage->SetClickable(true);
        tiffImage->onClick = []() {
            ShowFullSizeImageViewer("media/images/artist_converted.tiff");
        };

        container->AddChild(tiffImage);

        // Performance Metrics Panel
//        auto perfPanel = std::make_shared<UltraCanvasLabel>("TIFFPerf", 2104, 490, 140, 440, 120);
//        perfPanel->SetText(
//                "Performance Metrics (×100):\n"
//                "Load Time: ~200ms (uncompressed)\n"
//                "Display Time: ~90ms\n"
//                "Save Time: ~250ms\n"
//                "Note: Converted from artist.jpg\n"
//                "Compression: LZW");
//        perfPanel->SetAlignment(TextAlignment::Left);
//        perfPanel->SetBackgroundColor(Color(250, 250, 250, 255));
//        perfPanel->SetBorders(1.0f);
//        perfPanel->SetPadding(10.0f);
//        perfPanel->SetFontSize(10);
//        container->AddChild(perfPanel);

        // Metadata Panel
        auto metadataPanel = std::make_shared<UltraCanvasLabel>("TIFFMetadata", 2105, 490, 140, 440, 180);
        metadataPanel->SetText(
                "Format Metadata:\n"
                "File Size: Large (uncompressed)\n"
                "Color Depth: Up to 32-bit\n"
                "Resolution: DPI metadata\n"
                "Layers: Multi-page support\n"
                "EXIF: Full metadata support\n"
                "ICC Profiles: Color management");
        metadataPanel->SetAlignment(TextAlignment::Left);
        metadataPanel->SetBackgroundColor(Color(245, 250, 255, 255));
        metadataPanel->SetBorders(1.0f);
        metadataPanel->SetPadding(10.0f);
        metadataPanel->SetFontSize(10);
        container->AddChild(metadataPanel);

        // Format Options Panel
        auto optionsPanel = std::make_shared<UltraCanvasLabel>("TIFFOptions", 2106, 490, 400, 440, 90);
        optionsPanel->SetText(
                "Format Options:\n"
                "• Compression: None, LZW, ZIP, JPEG\n"
                "• Color Depth: 8/16/32-bit\n"
                "• Alpha Channel: Supported\n"
                "• Multi-page: Multiple images\n"
                "• Metadata: Extensive EXIF/IPTC\n"
                "• Color Profiles: ICC support");
        optionsPanel->SetAlignment(TextAlignment::Left);
        optionsPanel->SetBackgroundColor(Color(255, 250, 245, 255));
        optionsPanel->SetBorders(1.0f);
        optionsPanel->SetPadding(10.0f);
        optionsPanel->SetFontSize(10);
        container->AddChild(optionsPanel);

        // Format Info
        auto formatInfo = CreateImageInfoLabel("TIFFFormatInfo", 20, 500,
                                               "TIFF (Tagged Image File Format)",
                                               "• Professional standard\n"
                                               "• Lossless quality preservation\n"
                                               "• Extensive metadata support\n"
                                               "• Multi-page capability\n"
                                               "• Ideal for: archival, professional photo\n"
                                               "• Large file sizes");
        container->AddChild(formatInfo);

        return container;
    }

// ===== BMP FORMAT EXAMPLES =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateBMPExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("BMPDemoPage", 2200, 0, 0, 950, 800);
        container->SetBackgroundColor(Color(255, 255, 255, 255));

        // Page Title
        auto title = std::make_shared<UltraCanvasLabel>("BMPTitle", 2201, 10, 10, 400, 35);
        title->SetText("BMP Format Demonstration");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(0, 128, 128, 255));
        container->AddChild(title);

        // Format Description
        auto description = std::make_shared<UltraCanvasLabel>("BMPDesc", 2202, 10, 50, 930, 80);
        description->SetText(
                "BMP (Bitmap) is Microsoft's native image format for Windows. "
                "It's one of the simplest image formats, typically storing uncompressed pixel data. "
                "BMP files are large but offer maximum compatibility with Windows applications and fast loading. "
                "While rarely used for web or mobile applications due to large file sizes, "
                "BMP remains useful for Windows-specific applications and as an intermediate format.");
        description->SetAlignment(TextAlignment::Left);
        //description->SetTextWrapping(true);
        description->SetFontSize(11);
        container->AddChild(description);

        // Image Display Area (converted from fantasy.jpg)
        auto bmpImage = std::make_shared<UltraCanvasImageElement>("BMPImage", 2203, 20, 140, 450, 350);
        // Note: In real implementation, would convert fantasy.jpg to BMP
        bmpImage->LoadFromFile("media/images/fantasy_converted.bmp");
        bmpImage->SetFitMode(ImageFitMode::Contain);
        bmpImage->SetBorders(1.0f, Color(200, 200, 200, 255));
        bmpImage->SetBackgroundColor(Color(240, 240, 240, 255));

        // ✨ Hand cursor and clickable
        bmpImage->SetMousePointer(MousePointer::Hand);
        bmpImage->SetClickable(true);
        bmpImage->onClick = []() {
            ShowFullSizeImageViewer("media/images/fantasy_converted.bmp");
        };

        container->AddChild(bmpImage);

        // Performance Metrics Panel
//        auto perfPanel = std::make_shared<UltraCanvasLabel>("BMPPerf", 2204, 490, 140, 440, 120);
//        perfPanel->SetText(
//                "Performance Metrics (×100):\n"
//                "Load Time: ~50ms (very fast)\n"
//                "Display Time: ~40ms\n"
//                "Save Time: ~60ms\n"
//                "Note: Converted from fantasy.jpg\n"
//                "Fastest format for read/write");
//        perfPanel->SetAlignment(TextAlignment::Left);
//        perfPanel->SetBackgroundColor(Color(250, 250, 250, 255));
//        perfPanel->SetBorders(1.0f);
//        perfPanel->SetPadding(10.0f);
//        perfPanel->SetFontSize(10);
//        container->AddChild(perfPanel);

        // Metadata Panel
        auto metadataPanel = std::make_shared<UltraCanvasLabel>("BMPMetadata", 2205, 490, 140, 440, 180);
        metadataPanel->SetText(
                "Format Metadata:\n"
                "File Size: Very large (uncompressed)\n"
                "Color Depth: Up to 32-bit\n"
                "Channels: RGB/RGBA\n"
                "Compression: Usually none\n"
                "Metadata: Minimal\n"
                "Resolution: DPI info available");
        metadataPanel->SetAlignment(TextAlignment::Left);
        metadataPanel->SetBackgroundColor(Color(245, 250, 255, 255));
        metadataPanel->SetBorders(1.0f);
        metadataPanel->SetPadding(10.0f);
        metadataPanel->SetFontSize(10);
        container->AddChild(metadataPanel);

        // Format Options Panel
        auto optionsPanel = std::make_shared<UltraCanvasLabel>("BMPOptions", 2206, 490, 400, 440, 90);
        optionsPanel->SetText(
                "Format Options:\n"
                "• Compression: None (typical), RLE\n"
                "• Color Depth: 1/4/8/16/24/32-bit\n"
                "• Alpha Channel: Optional (32-bit)\n"
                "• Color Profiles: Limited support\n"
                "• Metadata: Very minimal\n"
                "• Compatibility: Maximum on Windows");
        optionsPanel->SetAlignment(TextAlignment::Left);
        optionsPanel->SetBackgroundColor(Color(255, 250, 245, 255));
        optionsPanel->SetBorders(1.0f);
        optionsPanel->SetPadding(10.0f);
        optionsPanel->SetFontSize(10);
        container->AddChild(optionsPanel);

        // Format Info
        auto formatInfo = CreateImageInfoLabel("BMPFormatInfo", 20, 500,
                                               "BMP (Bitmap)",
                                               "• Windows native format\n"
                                               "• Typically uncompressed\n"
                                               "• Very large file sizes\n"
                                               "• Fast read/write operations\n"
                                               "• Ideal for: Windows apps, temp files\n"
                                               "• Maximum Windows compatibility");
        container->AddChild(formatInfo);

        return container;
    }

} // namespace UltraCanvas