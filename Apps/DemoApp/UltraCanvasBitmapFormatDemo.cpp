// UltraCanvasBitmapFormatInfo.h
// Comprehensive bitmap format specifications and capabilities
// Version: 1.0.0
// Last Modified: 2025-06-24
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasTextArea.h"
#include "../dialogs/UltraCanvasImageExportDialog.h"
#include "PixelFX/PixelFX.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <fmt/os.h>

namespace UltraCanvas {

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

// ===== FORMAT INFO STRUCTURES =====

    struct TechnicalSpec {
        std::string label;
        std::string value;
    };

    struct CapabilityItem {
        std::string label;
        std::string value;
        bool isSupported;  // true = green (supported), false = orange (feature/limitation)
    };

    struct BitmapFormatInfo {
        // Basic info
        std::string formatName;
        std::string fullName;
        std::string extensions;

        // About section
        std::string aboutTitle;
        std::string aboutDescription;
        Color accentColor;

        // Technical specifications
        std::vector<TechnicalSpec> containerSpecs;
        std::vector<TechnicalSpec> codecSpecs;

        // Capabilities (6 items for 3x2 grid)
        std::vector<CapabilityItem> capabilities;
    };

// ===== JPEG FORMAT =====
    BitmapFormatInfo GetJPEGFormatInfo() {
        BitmapFormatInfo info;

        info.formatName = "JPEG";
        info.fullName = "Joint Photographic Experts Group";
        info.extensions = ".jpg, .jpeg, .jpe, .jfif";
        info.accentColor = Color(220, 53, 69, 255);  // Red

        // About
        info.aboutTitle = "About JPEG";
        info.aboutDescription =
                "JPEG (Joint Photographic Experts Group) is a lossy compression format optimized for "
                "photographs and complex images with smooth color gradients. It achieves high compression "
                "ratios by selectively discarding image data less noticeable to human vision. JPEG is the "
                "most widely used image format on the web and in digital photography, supported by virtually "
                "all devices and software since its introduction in 1992.";

        // Technical Specifications - Container
        info.containerSpecs = {
                {"Standard", "ISO/IEC 10918-1 (ITU-T T.81)"},
                {"Introduced", "1992"},
                {"Extensions", ".jpg, .jpeg, .jpe, .jfif"}
        };

        // Technical Specifications - Codec
        info.codecSpecs = {
                {"Compression", "DCT-based lossy"},
                {"Color Modes", "YCbCr, Grayscale, CMYK"},
                {"Bit Depth", "8-bit per channel"}
        };

        // Capabilities
        info.capabilities = {
                {"Compression", "Lossy DCT", false},
                {"Quality", "Adjustable 1-100", false},
                {"Alpha Channel", "Not Supported", false},
                {"Animation", "Not Supported", false},
                {"Metadata", "EXIF, IPTC, XMP", true},
                {"Progressive", "Supported", true}
        };

        return info;
    }

// ===== PNG FORMAT =====
    BitmapFormatInfo GetPNGFormatInfo() {
        BitmapFormatInfo info;

        info.formatName = "PNG";
        info.fullName = "Portable Network Graphics";
        info.extensions = ".png";
        info.accentColor = Color(13, 110, 253, 255);  // Blue

        // About
        info.aboutTitle = "About PNG";
        info.aboutDescription =
                "PNG (Portable Network Graphics) is a lossless image format that supports transparency "
                "through an alpha channel. Developed as a patent-free replacement for GIF, PNG excels at "
                "storing images with sharp edges, text, and graphics. It uses DEFLATE compression to reduce "
                "file size without any quality loss, making it ideal for screenshots, logos, icons, and "
                "any image requiring exact color reproduction or transparency.";

        // Technical Specifications - Container
        info.containerSpecs = {
                {"Standard", "ISO/IEC 15948:2004"},
                {"Introduced", "1996"},
                {"Extensions", ".png"}
        };

        // Technical Specifications - Codec
        info.codecSpecs = {
                {"Compression", "DEFLATE (zlib)"},
                {"Color Modes", "RGB, RGBA, Grayscale, Indexed"},
                {"Bit Depth", "1/2/4/8/16-bit per channel"}
        };

        // Capabilities
        info.capabilities = {
                {"Compression", "Lossless", true},
                {"Quality", "Perfect fidelity", true},
                {"Alpha Channel", "8/16-bit", true},
                {"Animation", "APNG extension", true},
                {"Metadata", "tEXt, iTXt, zTXt", true},
                {"Interlacing", "Adam7", true}
        };

        return info;
    }

// ===== GIF FORMAT =====
    BitmapFormatInfo GetGIFFormatInfo() {
        BitmapFormatInfo info;

        info.formatName = "GIF";
        info.fullName = "Graphics Interchange Format";
        info.extensions = ".gif";
        info.accentColor = Color(111, 66, 193, 255);  // Purple

        // About
        info.aboutTitle = "About GIF";
        info.aboutDescription =
                "GIF (Graphics Interchange Format) is one of the oldest image formats, introduced by "
                "CompuServe in 1987. It uses LZW compression and supports up to 256 colors from a 24-bit "
                "palette. GIF's key feature is animation support, allowing multiple frames in a single file. "
                "While limited in color depth, GIF remains popular for short animations, reaction images, "
                "and simple graphics due to universal browser and platform support.";

        // Technical Specifications - Container
        info.containerSpecs = {
                {"Standard", "GIF87a / GIF89a"},
                {"Introduced", "1987 / 1989"},
                {"Extensions", ".gif"}
        };

        // Technical Specifications - Codec
        info.codecSpecs = {
                {"Compression", "LZW (lossless)"},
                {"Color Modes", "Indexed (palette)"},
                {"Bit Depth", "1-8 bit (max 256 colors)"}
        };

        // Capabilities
        info.capabilities = {
                {"Compression", "LZW Lossless", true},
                {"Quality", "256 colors max", false},
                {"Alpha Channel", "1-bit only", false},
                {"Animation", "Multi-frame", true},
                {"Metadata", "Comment blocks", true},
                {"Interlacing", "Supported", true}
        };

        return info;
    }

// ===== BMP FORMAT =====
    BitmapFormatInfo GetBMPFormatInfo() {
        BitmapFormatInfo info;

        info.formatName = "BMP";
        info.fullName = "Windows Bitmap";
        info.extensions = ".bmp, .dib";
        info.accentColor = Color(0, 128, 128, 255);  // Teal

        // About
        info.aboutTitle = "About BMP";
        info.aboutDescription =
                "BMP (Bitmap) is Microsoft's native uncompressed image format for Windows, introduced "
                "with Windows 1.0 in 1985. It stores pixel data in a straightforward manner, typically "
                "without compression, resulting in large file sizes but extremely fast read/write speeds. "
                "BMP supports various color depths and optional RLE compression. While rarely used for "
                "distribution due to size, BMP remains useful as an intermediate format and for Windows "
                "system graphics.";

        // Technical Specifications - Container
        info.containerSpecs = {
                {"Standard", "Microsoft BMP v5"},
                {"Introduced", "1985"},
                {"Extensions", ".bmp, .dib"}
        };

        // Technical Specifications - Codec
        info.codecSpecs = {
                {"Compression", "None, RLE4, RLE8"},
                {"Color Modes", "RGB, RGBA, Indexed"},
                {"Bit Depth", "1/4/8/16/24/32-bit"}
        };

        // Capabilities
        info.capabilities = {
                {"Compression", "None/RLE", false},
                {"Quality", "Uncompressed", true},
                {"Alpha Channel", "32-bit RGBA", true},
                {"Animation", "Not Supported", false},
                {"Metadata", "Limited", false},
                {"Loading Speed", "Very Fast", true}
        };

        return info;
    }

// ===== WebP FORMAT =====
    BitmapFormatInfo GetWebPFormatInfo() {
        BitmapFormatInfo info;

        info.formatName = "WebP";
        info.fullName = "Web Picture Format";
        info.extensions = ".webp";
        info.accentColor = Color(52, 168, 83, 255);  // Google Green

        // About
        info.aboutTitle = "About WebP";
        info.aboutDescription =
                "WebP is a modern image format developed by Google, designed specifically for the web. "
                "It provides superior compression compared to JPEG and PNG, offering both lossy and lossless "
                "modes. WebP supports transparency (alpha channel) and animation, combining the best features "
                "of JPEG, PNG, and GIF in a single format. Files are typically 25-35% smaller than equivalent "
                "JPEG/PNG images, making it ideal for web optimization and faster page loading.";

        // Technical Specifications - Container
        info.containerSpecs = {
                {"Standard", "RIFF container (WebP)"},
                {"Introduced", "2010"},
                {"Extensions", ".webp"}
        };

        // Technical Specifications - Codec
        info.codecSpecs = {
                {"Compression", "VP8 (lossy), VP8L (lossless)"},
                {"Color Modes", "RGB, RGBA, YUV420"},
                {"Bit Depth", "8-bit per channel"}
        };

        // Capabilities
        info.capabilities = {
                {"Compression", "Lossy & Lossless", true},
                {"Quality", "25-35% smaller", true},
                {"Alpha Channel", "8-bit supported", true},
                {"Animation", "Multi-frame", true},
                {"Metadata", "EXIF, XMP, ICC", true},
                {"Browser Support", "All modern", true}
        };

        return info;
    }

// ===== AVIF FORMAT =====
    BitmapFormatInfo GetAVIFFormatInfo() {
        BitmapFormatInfo info;

        info.formatName = "AVIF";
        info.fullName = "AV1 Image File Format";
        info.extensions = ".avif";
        info.accentColor = Color(102, 16, 242, 255);  // Indigo

        // About
        info.aboutTitle = "About AVIF";
        info.aboutDescription =
                "AVIF (AV1 Image File Format) is a next-generation image format based on the AV1 video "
                "codec developed by the Alliance for Open Media. It provides significantly better compression "
                "than JPEG, PNG, and WebP while maintaining excellent image quality. AVIF supports HDR, "
                "wide color gamuts (WCG), and transparency. Files are typically 50% smaller than JPEG at "
                "equivalent quality, making it ideal for bandwidth-conscious web applications.";

        // Technical Specifications - Container
        info.containerSpecs = {
                {"Standard", "ISO/IEC 23000-22 (HEIF)"},
                {"Introduced", "2019"},
                {"Extensions", ".avif"}
        };

        // Technical Specifications - Codec
        info.codecSpecs = {
                {"Compression", "AV1 intra-frame"},
                {"Color Modes", "RGB, YUV, Monochrome"},
                {"Bit Depth", "8/10/12-bit HDR"}
        };

        // Capabilities
        info.capabilities = {
                {"Compression", "AV1-based", true},
                {"Quality", "50% smaller vs JPEG", true},
                {"Alpha Channel", "Supported", true},
                {"Animation", "Image sequences", true},
                {"HDR Support", "10/12-bit", true},
                {"Wide Gamut", "BT.2020, P3", true}
        };

        return info;
    }

// ===== TIFF FORMAT =====
    BitmapFormatInfo GetTIFFFormatInfo() {
        BitmapFormatInfo info;

        info.formatName = "TIFF";
        info.fullName = "Tagged Image File Format";
        info.extensions = ".tiff, .tif";
        info.accentColor = Color(184, 134, 11, 255);  // Dark Goldenrod

        // About
        info.aboutTitle = "About TIFF";
        info.aboutDescription =
                "TIFF (Tagged Image File Format) is a flexible, adaptable format widely used in professional "
                "photography, publishing, and archival applications. Developed by Aldus (now Adobe), TIFF "
                "supports multiple compression schemes including none, LZW, ZIP, and JPEG. It can store "
                "multiple images (pages) in a single file and preserves extensive metadata. While file sizes "
                "are typically large, TIFF maintains maximum image quality and is the preferred format for "
                "print production and long-term archival.";

        // Technical Specifications - Container
        info.containerSpecs = {
                {"Standard", "TIFF 6.0 / BigTIFF"},
                {"Introduced", "1986"},
                {"Extensions", ".tiff, .tif"}
        };

        // Technical Specifications - Codec
        info.codecSpecs = {
                {"Compression", "None, LZW, ZIP, JPEG"},
                {"Color Modes", "RGB, CMYK, LAB, YCbCr"},
                {"Bit Depth", "1/8/16/32-bit, floating point"}
        };

        // Capabilities
        info.capabilities = {
                {"Compression", "Multiple options", true},
                {"Quality", "Archival grade", true},
                {"Alpha Channel", "Multiple channels", true},
                {"Multi-page", "Supported", true},
                {"Metadata", "Extensive EXIF/IPTC", true},
                {"Color Profiles", "Full ICC support", true}
        };

        return info;
    }

// ===== QOI FORMAT =====
    BitmapFormatInfo GetQOIFormatInfo() {
        BitmapFormatInfo info;

        info.formatName = "QOI";
        info.fullName = "Quite OK Image Format";
        info.extensions = ".qoi";
        info.accentColor = Color(255, 107, 107, 255);  // Coral

        // About
        info.aboutTitle = "About QOI";
        info.aboutDescription =
                "QOI (Quite OK Image Format) is a fast, lossless image format designed for simplicity and "
                "speed. Created by Dominic Szablewski in 2021, QOI achieves compression comparable to PNG "
                "but with 20-50x faster encoding and 3-4x faster decoding. The entire specification fits "
                "on a single page, making it extremely easy to implement. QOI is ideal for game development, "
                "real-time applications, and scenarios where encoding/decoding speed is critical.";

        // Technical Specifications - Container
        info.containerSpecs = {
                {"Standard", "QOI Specification 1.0"},
                {"Introduced", "2021"},
                {"Extensions", ".qoi"}
        };

        // Technical Specifications - Codec
        info.codecSpecs = {
                {"Compression", "Run-length + indexing"},
                {"Color Modes", "RGB, RGBA"},
                {"Bit Depth", "8-bit per channel"}
        };

        // Capabilities
        info.capabilities = {
                {"Compression", "Lossless", true},
                {"Quality", "Perfect fidelity", true},
                {"Alpha Channel", "8-bit RGBA", true},
                {"Encode Speed", "20-50x vs PNG", true},
                {"Decode Speed", "3-4x vs PNG", true},
                {"Simplicity", "~300 lines code", true}
        };

        return info;
    }

// ===== RAW FORMAT =====
    BitmapFormatInfo GetRAWFormatInfo() {
        BitmapFormatInfo info;

        info.formatName = "RAW";
        info.fullName = "Camera Raw Image";
        info.extensions = ".raw, .cr2, .cr3, .nef, .arw, .dng, .orf, .rw2";
        info.accentColor = Color(64, 64, 64, 255);  // Dark Gray

        // About
        info.aboutTitle = "About RAW";
        info.aboutDescription =
                "RAW files contain minimally processed data directly from a camera's image sensor. Unlike "
                "JPEG, RAW preserves all captured information, giving photographers maximum flexibility in "
                "post-processing. Each camera manufacturer has proprietary RAW formats (CR2/CR3 for Canon, "
                "NEF for Nikon, ARW for Sony, etc.). Adobe's DNG provides a standardized RAW format. RAW "
                "files are larger but offer superior dynamic range, white balance adjustment, and non-destructive "
                "editing capabilities essential for professional photography.";

        // Technical Specifications - Container
        info.containerSpecs = {
                {"Standard", "Vendor-specific / DNG"},
                {"Common Formats", "CR2, CR3, NEF, ARW, DNG"},
                {"Extensions", ".raw, .cr2, .nef, .arw, .dng"}
        };

        // Technical Specifications - Codec
        info.codecSpecs = {
                {"Compression", "Lossless or uncompressed"},
                {"Color Modes", "Bayer CFA, X-Trans, Foveon"},
                {"Bit Depth", "12/14/16-bit per channel"}
        };

        // Capabilities
        info.capabilities = {
                {"Compression", "Minimal/None", false},
                {"Quality", "Sensor-level data", true},
                {"Dynamic Range", "Maximum preserved", true},
                {"White Balance", "Adjustable post", true},
                {"Metadata", "Full EXIF/MakerNote", true},
                {"Editing", "Non-destructive", true}
        };

        return info;
    }

// ===== PSP FORMAT =====
    BitmapFormatInfo GetPSPFormatInfo() {
        BitmapFormatInfo info;

        info.formatName = "PSP";
        info.fullName = "Paint Shop Pro Image";
        info.extensions = ".psp, .pspimage";
        info.accentColor = Color(0, 102, 204, 255);  // Corel Blue

        // About
        info.aboutTitle = "About PSP";
        info.aboutDescription =
                "PSP (Paint Shop Pro Image) is the native format for Corel Paint Shop Pro, a popular image "
                "editing application. The format preserves all editing information including layers, masks, "
                "adjustment layers, vector objects, and selection data. PSP files support non-destructive "
                "editing workflows, allowing users to return and modify individual elements. While primarily "
                "used within Paint Shop Pro, the format demonstrates advanced features similar to PSD for "
                "Photoshop users.";

        // Technical Specifications - Container
        info.containerSpecs = {
                {"Standard", "Corel proprietary"},
                {"Introduced", "1990s (JASC era)"},
                {"Extensions", ".psp, .pspimage"}
        };

        // Technical Specifications - Codec
        info.codecSpecs = {
                {"Compression", "RLE, LZ77"},
                {"Color Modes", "RGB, CMYK, Grayscale, Indexed"},
                {"Bit Depth", "1/8/16-bit per channel"}
        };

        // Capabilities
        info.capabilities = {
                {"Compression", "Lossless RLE/LZ77", true},
                {"Layers", "Full support", true},
                {"Alpha/Masks", "Multiple channels", true},
                {"Vector Objects", "Supported", true},
                {"Adjustment Layers", "Non-destructive", true},
                {"Selections", "Saved in file", true}
        };

        return info;
    }

// ===== HEIF FORMAT (for reference) =====
    BitmapFormatInfo GetHEIFFormatInfo() {
        BitmapFormatInfo info;

        info.formatName = "HEIF";
        info.fullName = "High Efficiency Image Format";
        info.extensions = ".heif, .heic, .heics, .avci";
        info.accentColor = Color(249, 115, 22, 255);  // Orange

        // About
        info.aboutTitle = "About HEIF/HEIC";
        info.aboutDescription =
                "HEIF/HEIC (High Efficiency Image Format) is an image container format based on HEVC (H.265) "
                "video compression. It provides superior compression efficiency compared to JPEG while "
                "maintaining high image quality. HEIF supports features like image sequences, transparency, "
                "depth maps, and HDR. This format is used by default on Apple devices since iOS 11 and "
                "macOS High Sierra.";

        // Technical Specifications - Container
        info.containerSpecs = {
                {"Standard", "ISO/IEC 23008-12 (HEIF)"},
                {"Introduced", "2015"},
                {"Extensions", ".heif, .heic, .heics, .avci"}
        };

        // Technical Specifications - Codec
        info.codecSpecs = {
                {"Compression", "HEVC (H.265)"},
                {"Color Modes", "RGB, YCbCr"},
                {"Bit Depth", "8/10-bit HDR"}
        };

        // Capabilities
        info.capabilities = {
                {"Compression", "HEVC-based", false},
                {"Quality", "High Efficiency", false},
                {"Alpha Channel", "Supported", true},
                {"Image Sequences", "Supported", true},
                {"Depth Maps", "Supported", true},
                {"HDR", "10-bit support", true}
        };

        return info;
    }

// ===== FORMAT INFO REGISTRY =====
    BitmapFormatInfo GetBitmapFormatInfo(const std::string& format) {
        static std::unordered_map<std::string, BitmapFormatInfo(*)()> formatRegistry = {
                {"JPEG", GetJPEGFormatInfo},
                {"JPG", GetJPEGFormatInfo},
                {"PNG", GetPNGFormatInfo},
                {"GIF", GetGIFFormatInfo},
                {"BMP", GetBMPFormatInfo},
                {"WEBP", GetWebPFormatInfo},
                {"WebP", GetWebPFormatInfo},
                {"AVIF", GetAVIFFormatInfo},
                {"TIFF", GetTIFFFormatInfo},
                {"TIF", GetTIFFFormatInfo},
                {"QOI", GetQOIFormatInfo},
                {"RAW", GetRAWFormatInfo},
                {"CR2", GetRAWFormatInfo},
                {"NEF", GetRAWFormatInfo},
                {"ARW", GetRAWFormatInfo},
                {"DNG", GetRAWFormatInfo},
                {"PSP", GetPSPFormatInfo},
                {"PSPIMAGE", GetPSPFormatInfo},
                {"HEIF", GetHEIFFormatInfo},
                {"HEIC", GetHEIFFormatInfo}
        };

        // Convert to uppercase for lookup
        std::string upperFormat = format;
        std::transform(upperFormat.begin(), upperFormat.end(), upperFormat.begin(), ::toupper);

        auto it = formatRegistry.find(upperFormat);
        if (it != formatRegistry.end()) {
            return it->second();
        }

        // Return empty info if format not found
        return BitmapFormatInfo{};
    }

// ===== HELPER: CREATE FORMAT DEMO PAGE =====
// This helper function creates a demo page for any bitmap format using the format info
    std::shared_ptr<UltraCanvasContainer> UltraCanvasDemoApplication::CreateBitmapFormatDemoPage(
            const std::string& format,
            const std::string& sampleImagePath,
            int baseId
    ) {
        BitmapFormatInfo info = GetBitmapFormatInfo(format);
        if (info.formatName.empty()) {
            return nullptr;
        }

        // Main container
        auto container = std::make_shared<UltraCanvasContainer>(
                info.formatName + "DemoPage", baseId, 0, 0, 950, 750
        );
        container->SetBackgroundColor(Color(255, 251, 235, 255));

        // Layout constants
        const int leftColX = 20;
        const int rightColX = 310;
        const int leftColWidth = 270;
        const int rightColWidth = 620;
        const int row1Y = 20;
        const int row2Y = 340;
        const int row3Y = 580;

        int id = baseId + 1;

        // ===== ROW 1 LEFT: IMAGE PREVIEW CARD =====
        auto imageCard = std::make_shared<UltraCanvasContainer>("ImageCard", id++, leftColX, row1Y, leftColWidth, 300);
        imageCard->SetBackgroundColor(Color(255, 255, 255, 255));
        imageCard->SetBorders(1, Color(230, 230, 230, 255));
        container->AddChild(imageCard);

        auto imageTitle = std::make_shared<UltraCanvasLabel>("ImageTitle", id++, 20, 16, 200, 24);
        imageTitle->SetText("Demo " + info.formatName + " Image");
        imageTitle->SetFontSize(14);
        imageTitle->SetFontWeight(FontWeight::Bold);
        imageTitle->SetTextColor(Color(30, 41, 59, 255));
        imageCard->AddChild(imageTitle);

        auto imageFrame = std::make_shared<UltraCanvasContainer>("ImageFrame", id++, 20, 48, 230, 170);
        imageFrame->SetBackgroundColor(Color(241, 245, 249, 255));
        imageFrame->SetBorders(1, Color(200, 200, 200, 255));
        imageCard->AddChild(imageFrame);

        auto image = std::make_shared<UltraCanvasImageElement>("Image", id++, 4, 4, 222, 162);
        image->LoadFromFile(sampleImagePath);
        image->SetFitMode(ImageFitMode::Contain);
        image->SetClickable(true);
        image->SetMouseCursor(UCMouseCursor::LookingGlass);
        image->onClick = [sampleImagePath]() {
            ShowFullSizeImageViewer(sampleImagePath);
        };
        imageFrame->AddChild(image);

        // Extract filename
        std::string filename = sampleImagePath;
        size_t lastSlash = sampleImagePath.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            filename = sampleImagePath.substr(lastSlash + 1);
        }

        auto filenameLabel = std::make_shared<UltraCanvasLabel>("Filename", id++, 20, 224, 230, 20);
        filenameLabel->SetText(filename);
        filenameLabel->SetFontSize(10);
        filenameLabel->SetTextColor(Color(100, 116, 139, 255));
        filenameLabel->SetAlignment(TextAlignment::Center);
        imageCard->AddChild(filenameLabel);

        auto viewBtn = std::make_shared<UltraCanvasButton>("ViewBtn", id++, 20, 250, 108, 32);
        viewBtn->SetText("🔍 Full Size");
        viewBtn->SetFontSize(10);
        viewBtn->SetColors(info.accentColor, info.accentColor);
        viewBtn->SetTextColors(Color(255, 255, 255, 255));
        viewBtn->SetCornerRadius(6);
        viewBtn->SetMouseCursor(UCMouseCursor::LookingGlass);
        viewBtn->onClick = [sampleImagePath]() {
            ShowFullSizeImageViewer(sampleImagePath);
        };
        imageCard->AddChild(viewBtn);

        auto exportBtn = std::make_shared<UltraCanvasButton>("ExportBtn", id++, 138, 250, 108, 32);
        exportBtn->SetText("📤 Export");
        exportBtn->SetFontSize(10);
        exportBtn->SetColors(Color(241, 245, 249, 255), Color(226, 232, 240, 255));
        exportBtn->SetTextColors(Color(71, 85, 105, 255));
        exportBtn->SetCornerRadius(6);
        exportBtn->onClick = [sampleImagePath]() {
            auto vimg = vips::VImage::new_from_file(sampleImagePath.c_str());
            auto win = CreateImageExportDialog(vimg);
            win->Create();
            win->Show();
        };
        imageCard->AddChild(exportBtn);

        // ===== ROW 1 RIGHT: IMAGE PROPERTIES (populated from actual image) =====
        auto propertiesCard = std::make_shared<UltraCanvasContainer>("PropertiesCard", id++, rightColX, row1Y, rightColWidth, 300);
        propertiesCard->SetBackgroundColor(Color(255, 255, 255, 255));
        propertiesCard->SetBorders(1, Color(230, 230, 230, 255));
        container->AddChild(propertiesCard);

        auto propHeader = std::make_shared<UltraCanvasLabel>("PropHeader", id++, 20, 16, 300, 24);
        propHeader->SetText("📊  Image Properties");
        propHeader->SetFontSize(14);
        propHeader->SetFontWeight(FontWeight::Bold);
        propHeader->SetTextColor(Color(30, 41, 59, 255));
        propertiesCard->AddChild(propHeader);

        auto imgInfo = PixelFX::ExtractImageInfo(sampleImagePath);

        // Property placeholders (would be populated from actual image metadata)
        std::vector<std::pair<std::string, std::string>> defaultProps = {
                {"FILE SIZE", FormatFileSize(imgInfo.fileSize)},
                {"RESOLUTION", fmt::format("{}x{}", imgInfo.width, imgInfo.height)},
                {"CHANNELS", fmt::format("{}", imgInfo.channels)},
                {"COLOR SPACE", imgInfo.colorSpace},
                {"LOADER", imgInfo.loader},
                {"BITS PER CHANNEL", fmt::format("{}", imgInfo.bitsPerChannel)},
                {"ALPHA CHANNEL", imgInfo.hasAlpha ? "Yes" : "No"},
                {"DPI", fmt::format("{}", round(imgInfo.dpiX))}
        };

        int propY = 56;
        for (size_t i = 0; i < defaultProps.size(); ++i) {
            int col = i % 2;
            int row = i / 2;

            auto propContainer = std::make_shared<UltraCanvasContainer>(
                    "Prop" + std::to_string(i), id++,
                    20 + col * 306, propY + row * 60,
                    290, 52
            );
            propContainer->SetBackgroundColor(Color(248, 250, 252, 255));
            propContainer->SetBorders(1, Color(226, 232, 240, 255));
            propertiesCard->AddChild(propContainer);

            auto propLabel = std::make_shared<UltraCanvasLabel>("PropLabel" + std::to_string(i), id++, 16, 8, 150, 16);
            propLabel->SetText(defaultProps[i].first);
            propLabel->SetFontSize(9);
            propLabel->SetFontWeight(FontWeight::Normal);
            propLabel->SetTextColor(Color(100, 116, 139, 255));
            propContainer->AddChild(propLabel);

            auto propValue = std::make_shared<UltraCanvasLabel>("PropValue" + std::to_string(i), id++, 16, 28, 260, 18);
            propValue->SetText(defaultProps[i].second);
            propValue->SetFontSize(13);
            propValue->SetFontWeight(FontWeight::Bold);
            propValue->SetTextColor(Color(30, 41, 59, 255));
            propContainer->AddChild(propValue);
        }

        // ===== ROW 2 LEFT: ABOUT SECTION =====
        auto aboutCard = std::make_shared<UltraCanvasContainer>("AboutCard", id++, leftColX, row2Y, leftColWidth, 220);
        aboutCard->SetBackgroundColor(Color(255, 255, 255, 255));
        aboutCard->SetBorders(1, Color(230, 230, 230, 255));
        container->AddChild(aboutCard);

        auto aboutIconLabel = std::make_shared<UltraCanvasLabel>("AboutIcon", id++, 16, 10, 36, 36);
        aboutIconLabel->SetText("📄");
        aboutIconLabel->SetFontSize(16);
        aboutIconLabel->SetBackgroundColor(Color(
                std::min(255, info.accentColor.r + 100),
                std::min(255, info.accentColor.g + 100),
                std::min(255, info.accentColor.b + 100),
                255
        ));
        aboutIconLabel->SetAlignment(TextAlignment::Center);
        aboutIconLabel->SetPadding(4);
        aboutCard->AddChild(aboutIconLabel);

        auto aboutTitle = std::make_shared<UltraCanvasLabel>("AboutTitle", id++, 64, 16, 180, 20);
        aboutTitle->SetText(info.aboutTitle);
        aboutTitle->SetFontSize(13);
        aboutTitle->SetFontWeight(FontWeight::Bold);
        aboutTitle->SetTextColor(Color(30, 41, 59, 255));
        aboutCard->AddChild(aboutTitle);

        auto aboutDescCont = std::make_shared<UltraCanvasContainer>("AboutDescCont", id++, 20, 50, 248, 168);
        aboutDescCont->SetPadding(0,16,6,0);

        auto aboutDesc = std::make_shared<UltraCanvasLabel>("AboutDesc", id++, 0, 0, 220, 300);
        aboutDesc->SetText(info.aboutDescription);
        aboutDesc->SetFontSize(10);
        aboutDesc->SetTextColor(Color(71, 85, 105, 255));
        aboutDesc->SetWordWrap(true);
        aboutDesc->SetAlignment(TextAlignment::Left);
        aboutDescCont->AddChild(aboutDesc);
        aboutCard->AddChild(aboutDescCont);

        // ===== ROW 2 RIGHT: CAPABILITIES =====
        auto capCard = std::make_shared<UltraCanvasContainer>("CapCard", id++, rightColX, row2Y, rightColWidth, 220);
        capCard->SetBackgroundColor(Color(255, 255, 255, 255));
        capCard->SetBorders(1, Color(230, 230, 230, 255));
        container->AddChild(capCard);

        auto capIconLabel = std::make_shared<UltraCanvasLabel>("CapIcon", id++, 20, 16, 36, 36);
        capIconLabel->SetText("⚙️");
        capIconLabel->SetFontSize(16);
        capIconLabel->SetBackgroundColor(Color(236, 253, 245, 255));
        capIconLabel->SetAlignment(TextAlignment::Center);
        capIconLabel->SetPadding(4);
        capCard->AddChild(capIconLabel);

        auto capTitle = std::make_shared<UltraCanvasLabel>("CapTitle", id++, 60, 20, 200, 24);
        capTitle->SetText("Format Capabilities");
        capTitle->SetFontSize(14);
        capTitle->SetFontWeight(FontWeight::Bold);
        capTitle->SetTextColor(Color(30, 41, 59, 255));
        capTitle->SetAutoResize(true);
        capCard->AddChild(capTitle);

        int capY = 60;
        for (size_t i = 0; i < info.capabilities.size() && i < 6; ++i) {
            int col = i % 3;
            int row = i / 3;

            auto capContainer = std::make_shared<UltraCanvasContainer>(
                    "Cap" + std::to_string(i), id++,
                    20 + col * 202, capY + row * 70,
                    192, 62
            );

            if (info.capabilities[i].isSupported) {
                capContainer->SetBackgroundColor(Color(236, 253, 245, 255));
                capContainer->SetBorders(1, Color(167, 243, 208, 255));
            } else {
                capContainer->SetBackgroundColor(Color(255, 247, 237, 255));
                capContainer->SetBorders(1, Color(254, 215, 170, 255));
            }
            capCard->AddChild(capContainer);

            auto capLabel = std::make_shared<UltraCanvasLabel>("CapLabel" + std::to_string(i), id++, 6, 10, 180, 16);
            capLabel->SetText(info.capabilities[i].label);
            capLabel->SetFontSize(10);
            capLabel->SetTextColor(Color(100, 116, 139, 255));
            capLabel->SetAlignment(TextAlignment::Center);
            capLabel->SetAutoResize(true);
            capContainer->AddChild(capLabel);

            auto capValue = std::make_shared<UltraCanvasLabel>("CapValue" + std::to_string(i), id++, 6, 32, 180, 20);
            capValue->SetText(info.capabilities[i].value);
            capValue->SetFontSize(12);
            capValue->SetFontWeight(FontWeight::Bold);
            capValue->SetTextColor(info.capabilities[i].isSupported
                                   ? Color(5, 150, 105, 255)
                                   : Color(234, 88, 12, 255));
            capValue->SetAlignment(TextAlignment::Center);
            capValue->SetAutoResize(true);
            capContainer->AddChild(capValue);
        }

        // ===== ROW 3: TECHNICAL SPECIFICATIONS =====
        auto techCard = std::make_shared<UltraCanvasContainer>("TechCard", id++, leftColX, row3Y, 910, 170);
        techCard->SetBackgroundColor(Color(255, 255, 255, 255));
        techCard->SetBorders(1, Color(230, 230, 230, 255));
        container->AddChild(techCard);

        auto techTitle = std::make_shared<UltraCanvasLabel>("TechTitle", id++, 20, 20, 300, 24);
        techTitle->SetText("Technical Specifications");
        techTitle->SetFontSize(16);
        techTitle->SetFontWeight(FontWeight::Bold);
        techTitle->SetTextColor(Color(30, 41, 59, 255));
        techCard->AddChild(techTitle);

        // Container/Standard specs (left column)
        auto containerTitle = std::make_shared<UltraCanvasLabel>("ContainerTitle", id++, 20, 56, 200, 20);
        containerTitle->SetText("Container Format");
        containerTitle->SetFontSize(12);
        containerTitle->SetFontWeight(FontWeight::Bold);
        containerTitle->SetTextColor(info.accentColor);
        techCard->AddChild(containerTitle);

        for (size_t i = 0; i < info.containerSpecs.size(); ++i) {
            auto bullet = std::make_shared<UltraCanvasLabel>("ContBullet" + std::to_string(i), id++, 28, 80 + i * 24, 16, 16);
            bullet->SetText("●");
            bullet->SetFontSize(8);
            bullet->SetTextColor(info.accentColor);
            techCard->AddChild(bullet);

            auto itemLabel = std::make_shared<UltraCanvasLabel>("ContItem" + std::to_string(i), id++, 44, 78 + i * 24, 380, 18);
            itemLabel->SetText(info.containerSpecs[i].label + ": " + info.containerSpecs[i].value);
            itemLabel->SetFontSize(11);
            itemLabel->SetTextColor(Color(71, 85, 105, 255));
            techCard->AddChild(itemLabel);
        }

        // Codec specs (right column)
        auto codecTitle = std::make_shared<UltraCanvasLabel>("CodecTitle", id++, 470, 56, 200, 20);
        codecTitle->SetText("Codec Support");
        codecTitle->SetFontSize(12);
        codecTitle->SetFontWeight(FontWeight::Bold);
        codecTitle->SetTextColor(info.accentColor);
        techCard->AddChild(codecTitle);

        for (size_t i = 0; i < info.codecSpecs.size(); ++i) {
            auto bullet = std::make_shared<UltraCanvasLabel>("CodecBullet" + std::to_string(i), id++, 478, 80 + i * 24, 16, 16);
            bullet->SetText("●");
            bullet->SetFontSize(8);
            bullet->SetTextColor(Color(info.accentColor.r, info.accentColor.g, std::min(255, info.accentColor.b + 50), 255));
            techCard->AddChild(bullet);

            auto itemLabel = std::make_shared<UltraCanvasLabel>("CodecItem" + std::to_string(i), id++, 494, 78 + i * 24, 380, 18);
            itemLabel->SetText(info.codecSpecs[i].label + ": " + info.codecSpecs[i].value);
            itemLabel->SetFontSize(11);
            itemLabel->SetTextColor(Color(71, 85, 105, 255));
            techCard->AddChild(itemLabel);
        }

        return container;
    }

} // namespace UltraCanvas