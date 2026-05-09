// dialogs/UltraCanvasImageExportDialog.h
// Comprehensive bitmap file save dialog with dynamic format-specific options
// Version: 2.1.0
// Last Modified: 2025-01-06
// Author: UltraCanvas Framework
//
// ARCHITECTURE: Uses UltraCanvas layout system for automatic positioning
// and event propagation. No manual coordinate calculations or event forwarding.
// - VBoxLayout for vertical sections
// - HBoxLayout for horizontal rows
// - GridLayout for label+control pairs
// Container handles all child event propagation automatically.
//
// CHANGES v2.1.0: Aligned controls with actual UltraCanvasImage.h export option structures

#pragma once

#include "../include/UltraCanvasUIElement.h"
#include "../include/UltraCanvasWindow.h"
#include "../include/UltraCanvasCommonTypes.h"
#include "../include/UltraCanvasRenderContext.h"
#include "../include/UltraCanvasEvent.h"
#include "../include/UltraCanvasButton.h"
#include "../include/UltraCanvasLabel.h"
#include "../include/UltraCanvasTextInput.h"
#include "../include/UltraCanvasDropdown.h"
#include "../include/UltraCanvasCheckbox.h"
#include "../include/UltraCanvasSlider.h"
#include "../include/UltraCanvasContainer.h"
#include "../include/UltraCanvasImageElement.h"
#include "../include/UltraCanvasBoxLayout.h"
#include "../include/UltraCanvasGridLayout.h"

#include <functional>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace UltraCanvas {

// ============================================================================
// IMAGE FORMAT INFO
// ============================================================================

    struct ImageFormatInfo {
        UCImageSaveFormat format;
        std::string name;
        std::string extension;
        std::string description;
        bool supportsTransparency = false;
        bool supportsLossless = false;
        bool supportsLossy = false;
        bool supportsHDR = false;
        std::vector<UCImageSave::ColorDepth> supportedDepths;

        static ImageFormatInfo GetInfo(UCImageSaveFormat format);
        static std::vector<ImageFormatInfo> GetAllFormats();
        static std::string GetExtension(UCImageSaveFormat format);
        static UCImageSaveFormat FromExtension(const std::string& ext);
    };

// ============================================================================
// DIALOG STYLE
// ============================================================================

    struct ImageExportDialogStyle {
        Color backgroundColor = Color(250, 250, 250, 255);
        Color borderColor = Color(200, 200, 200, 255);
        Color accentColor = Color(0, 120, 212, 255);
        Color textColor = Color(30, 30, 30, 255);
        Color labelColor = Color(100, 100, 100, 255);

        float padding = 16.0f;
        float spacing = 12.0f;
        float labelFontSize = 12.0f;
        float valueFontSize = 11.0f;

        static ImageExportDialogStyle Default() { return ImageExportDialogStyle(); }
        static ImageExportDialogStyle Dark();
    };

// ============================================================================
// ULTRACANVAS IMAGE EXPORT DIALOG
// ============================================================================

    class UltraCanvasImageExportDialog : public UltraCanvasWindow {
    private:
        // ===== CONFIGURATION =====
        UCImageSave::ImageExportOptions options;
        ImageExportDialogStyle style;
        vips::VImage sourceImage;
        // ===== SOURCE IMAGE INFO =====
        int sourceWidth = 0;
        int sourceHeight = 0;
        int sourceChannels = 4;

        // ===== SECTION CONTAINERS =====
        std::shared_ptr<UltraCanvasContainer> headerSection;
        std::shared_ptr<UltraCanvasContainer> optionsSection;
        std::shared_ptr<UltraCanvasContainer> formatOptionsSection;
        std::shared_ptr<UltraCanvasContainer> metadataSection;
        std::shared_ptr<UltraCanvasContainer> footerSection;

        // ===== HEADER COMPONENTS =====
        std::shared_ptr<UltraCanvasLabel> fileNameLabel;
        std::shared_ptr<UltraCanvasTextInput> fileNameInput;
        std::shared_ptr<UltraCanvasLabel> formatLabel;
        std::shared_ptr<UltraCanvasDropdown> formatDropdown;

        // ===== OPTIONS COMPONENTS =====
        std::shared_ptr<UltraCanvasLabel> sizeLabel;
        std::shared_ptr<UltraCanvasTextInput> widthInput;
        std::shared_ptr<UltraCanvasLabel> xLabel;
        std::shared_ptr<UltraCanvasTextInput> heightInput;
        std::shared_ptr<UltraCanvasCheckbox> aspectRatioCheckbox;

        std::shared_ptr<UltraCanvasLabel> colorDepthLabel;
        std::shared_ptr<UltraCanvasDropdown> colorDepthDropdown;

        std::shared_ptr<UltraCanvasLabel> transparencyLabel;
        std::shared_ptr<UltraCanvasCheckbox> transparencyCheckbox;

        std::shared_ptr<UltraCanvasLabel> qualityLabel;
        std::shared_ptr<UltraCanvasSlider> qualitySlider;
        std::shared_ptr<UltraCanvasLabel> qualityValueLabel;

        // ===== FORMAT-SPECIFIC CONTAINERS =====
        std::shared_ptr<UltraCanvasContainer> pngOptionsContainer;
        std::shared_ptr<UltraCanvasContainer> jpegOptionsContainer;
        std::shared_ptr<UltraCanvasContainer> webpOptionsContainer;
        std::shared_ptr<UltraCanvasContainer> avifOptionsContainer;
        std::shared_ptr<UltraCanvasContainer> gifOptionsContainer;
        std::shared_ptr<UltraCanvasContainer> tiffOptionsContainer;
        std::shared_ptr<UltraCanvasContainer> qoiOptionsContainer;

        // PNG - matches PngExportOptions: compressionLevel, interlace, preserveTransparency, colorDepth
        std::shared_ptr<UltraCanvasCheckbox> pngInterlaceCheckbox;

        // JPEG - matches JpegExportOptions: quality, progressive, subsampling (bool), optimizeHuffman
        std::shared_ptr<UltraCanvasCheckbox> jpegProgressiveCheckbox;
        std::shared_ptr<UltraCanvasCheckbox> jpegSubsamplingCheckbox;  // Changed from dropdown to checkbox
        std::shared_ptr<UltraCanvasCheckbox> jpegOptimizeHuffmanCheckbox;

        // WebP - matches WebpExportOptions: quality, lossless, effort, targetSize, preserveTransparency, alphaQuality
        std::shared_ptr<UltraCanvasCheckbox> webpLosslessCheckbox;
        std::shared_ptr<UltraCanvasSlider> webpEffortSlider;
        std::shared_ptr<UltraCanvasSlider> webpAlphaQualitySlider;

        // AVIF - matches AvifExportOptions: quality, lossless, speed, preserveTransparency, colorDepth
        std::shared_ptr<UltraCanvasCheckbox> avifLosslessCheckbox;
        std::shared_ptr<UltraCanvasSlider> avifSpeedSlider;
        // Note: colorDepth handled via common colorDepthDropdown, no separate bitDepth

        // GIF - matches GifExportOptions: colorDepth, interlace, dithering
        std::shared_ptr<UltraCanvasCheckbox> gifDitheringCheckbox;
        std::shared_ptr<UltraCanvasCheckbox> gifInterlaceCheckbox;
        // Note: Removed maxColors slider (not in GifExportOptions)

        // TIFF - matches TiffExportOptions: compression, colorDepth, multiPage
        std::shared_ptr<UltraCanvasDropdown> tiffCompressionDropdown;
        std::shared_ptr<UltraCanvasCheckbox> tiffMultiPageCheckbox;

        // QOI - matches QoiExportOptions: hasAlpha, linearColorspace
        std::shared_ptr<UltraCanvasCheckbox> qoiAlphaCheckbox;
        std::shared_ptr<UltraCanvasCheckbox> qoiLinearColorspaceCheckbox;  // Changed from dropdown to checkbox
        std::shared_ptr<UltraCanvasLabel> qoiInfoLabel;

        // ===== METADATA COMPONENTS =====
        // Note: Many format-specific metadata options are commented out in UltraCanvasImage.h
        std::shared_ptr<UltraCanvasCheckbox> preserveMetadataCheckbox;

        // ===== FOOTER COMPONENTS =====
        std::shared_ptr<UltraCanvasLabel> fileSizeEstimateLabel;
        std::shared_ptr<UltraCanvasButton> cancelButton;
        std::shared_ptr<UltraCanvasButton> saveButton;

        // ===== STATE =====
        UCImageSaveFormat currentFormat = UCImageSaveFormat::PNG;

        // ===== INTERNAL METHODS =====
        void BuildLayout();
        void CreateHeaderSection();
        void CreateOptionsSection();
        void CreateFormatOptionsSection();
        void CreateMetadataSection();
        void CreateFooterSection();

        void CreatePngOptions();
        void CreateJpegOptions();
        void CreateWebpOptions();
        void CreateAvifOptions();
        void CreateGifOptions();
        void CreateTiffOptions();
        void CreateQoiOptions();

        void WireCallbacks();
        void UpdateFormatOptions();
        void UpdateColorDepthOptions();
        void UpdateQualityRange();
        void UpdateFileSizeEstimate();
        void HideAllFormatOptions();

        void ApplyOptionsFromUI();
        size_t EstimateFileSize();
        std::string FormatFileSize(size_t bytes);

    public:
        // ===== CONSTRUCTOR =====
        UltraCanvasImageExportDialog();
        UltraCanvasImageExportDialog(vips::VImage& img);

        virtual ~UltraCanvasImageExportDialog() = default;

        // ===== SOURCE IMAGE =====
        void SetSourceImage(vips::VImage& img);

        // ===== OPTIONS =====
        void SetOptions(const UCImageSave::ImageExportOptions& opts);
        UCImageSave::ImageExportOptions GetOptions() const;

        void SetFormat(UCImageSaveFormat format);
        UCImageSaveFormat GetFormat() const;

        void SetFileName(const std::string& name);
        std::string GetFileName() const;

        void SetTargetSize(int width, int height);

        // ===== STYLE =====
        void SetStyle(const ImageExportDialogStyle& dialogStyle);

        // ===== CALLBACKS =====
        std::function<void(const UCImageSave::ImageExportOptions&)> onSave;
        std::function<void()> onCancel;
        std::function<void(UCImageSaveFormat)> onFormatChange;
        std::function<void(const UCImageSave::ImageExportOptions&)> onOptionsChange;
    };

// ============================================================================
// FACTORY FUNCTION
// ============================================================================

    inline std::shared_ptr<UltraCanvasImageExportDialog> CreateImageExportDialog() {
        return std::make_shared<UltraCanvasImageExportDialog>();
    }
    inline std::shared_ptr<UltraCanvasImageExportDialog> CreateImageExportDialog(vips::VImage& img) {
        return std::make_shared<UltraCanvasImageExportDialog>(img);
    }

} // namespace UltraCanvas