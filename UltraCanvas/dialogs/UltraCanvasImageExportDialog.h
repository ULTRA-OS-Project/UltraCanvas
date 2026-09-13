// dialogs/UltraCanvasImageExportDialog.h
// Bitmap file save dialog: name, format, size, depth, transparency, the
// format's own knobs, and what to do with the metadata.
// Version: 3.0.0
// Last Modified: 2026-09-13
// Author: UltraCanvas Framework
//
// LAYOUT: every captioned row of the dialog - the common ones and the
// format-specific ones alike - is a row of ONE two-column grid whose first
// column is `auto` and whose second is `1fr`. That is what keeps the controls
// on a single line down the whole dialog: the column is as wide as the widest
// caption in it, so a translated caption widens the column instead of being
// cut off or leaving the fields ragged. Nothing here measures text or places
// anything by coordinate.
//
// Format-specific rows live in that same grid and are shown a set at a time
// (formatRows); hidden rows are display:none, so they cost no space.

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

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

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
        Color backgroundColor = Color(250, 250, 252, 255);
        Color panelColor = Color(255, 255, 255, 255);
        Color borderColor = Color(222, 222, 228, 255);
        Color accentColor = Color(0, 120, 212, 255);
        Color textColor = Color(30, 30, 35, 255);
        Color labelColor = Color(100, 100, 110, 255);
        Color headingColor = Color(70, 70, 80, 255);

        float padding = 18.0f;
        float spacing = 12.0f;
        // Grid gaps: rows breathe a little less than columns so a caption
        // never looks detached from the control it belongs to.
        float rowGap = 9.0f;
        float columnGap = 14.0f;
        float controlHeight = 28.0f;
        float labelFontSize = 12.0f;
        float valueFontSize = 11.0f;
        // Buttons size to their own text (so "Speichern" fits) but never
        // shrink below this.
        float minButtonWidth = 96.0f;

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

        // ===== SECTIONS =====
        // One grid holds every captioned row; the footer is the only other
        // block. See the layout note at the top of this file.
        std::shared_ptr<UltraCanvasContainer> formGrid;
        std::shared_ptr<UltraCanvasContainer> footerSection;

        // ===== FILE =====
        std::shared_ptr<UltraCanvasLabel> fileNameLabel;
        std::shared_ptr<UltraCanvasTextInput> fileNameInput;
        std::shared_ptr<UltraCanvasLabel> formatLabel;
        std::shared_ptr<UltraCanvasDropdown> formatDropdown;
        // One grey line under the format picker saying what the format is for.
        std::shared_ptr<UltraCanvasLabel> formatDescriptionLabel;

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

        // ===== FORMAT-SPECIFIC ROWS =====
        // The rows each format contributes to the shared grid, so a format
        // change hides one set and shows another. Labels and controls are
        // both listed: hiding a row means hiding its caption too.
        std::map<UCImageSaveFormat, std::vector<std::shared_ptr<UltraCanvasUIElement>>> formatRows;
        // Formats whose options are the same controls (PPM/PGM/PBM/PFM share
        // the PNM row) map onto the format that owns them.
        static UCImageSaveFormat FormatRowsKeyFor(UCImageSaveFormat format);
        // Set while a CreateXxxOptions() runs; every row added lands here.
        std::vector<std::shared_ptr<UltraCanvasUIElement>>* currentFormatRows = nullptr;
        // The "<FORMAT> options" caption above whichever set is showing.
        std::shared_ptr<UltraCanvasLabel> formatOptionsHeading;
        std::shared_ptr<UltraCanvasUIElement> formatOptionsRule;

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

        // ===== Phase 1 widgets =====
        // TGA - rleCompression
        std::shared_ptr<UltraCanvasCheckbox> tgaRleCheckbox;
        // PCX - rleCompression
        std::shared_ptr<UltraCanvasCheckbox> pcxRleCheckbox;
        // PNM/PGM/PBM/PFM - binary vs ASCII
        std::shared_ptr<UltraCanvasCheckbox> pnmBinaryCheckbox;
        // EXR - compression dropdown
        std::shared_ptr<UltraCanvasDropdown> exrCompressionDropdown;
        // DPX - bit-depth dropdown
        std::shared_ptr<UltraCanvasDropdown> dpxBitDepthDropdown;
        // CIN - bit-depth dropdown
        std::shared_ptr<UltraCanvasDropdown> cinBitDepthDropdown;
        // PSD - compression
        std::shared_ptr<UltraCanvasCheckbox> psdCompressedCheckbox;
        // SGI - rleCompression
        std::shared_ptr<UltraCanvasCheckbox> sgiRleCheckbox;

        // ===== METADATA =====
        std::shared_ptr<UltraCanvasCheckbox> preserveMetadataCheckbox;
        // Shown only when the source image actually carries metadata: there is
        // nothing to open otherwise.
        std::shared_ptr<UltraCanvasButton> showMetadataButton;
        std::shared_ptr<UltraCanvasLabel> metadataSummaryLabel;

        // ===== FOOTER =====
        std::shared_ptr<UltraCanvasLabel> fileSizeEstimateLabel;
        std::shared_ptr<UltraCanvasButton> cancelButton;
        std::shared_ptr<UltraCanvasButton> saveButton;

        // ===== STATE =====
        UCImageSaveFormat currentFormat = UCImageSaveFormat::PNG;
        // Format entries actually shown in the dropdown, filtered against
        // the installed libvips build via VipsCanSave. The dropdown's
        // selection index maps directly into this vector.
        std::vector<ImageFormatInfo> availableFormats;

        // Guards against the width/height inputs echoing each other while the
        // aspect ratio is locked.
        bool syncingSize = false;

        // ===== FORM BUILDING =====
        // Every row of the dialog goes through these, which is what keeps the
        // captions in one column and the controls in another.
        std::shared_ptr<UltraCanvasLabel> MakeFieldLabel(const std::string& id, const std::string& text);
        // "caption:  [control]" — caption in the auto column, control in the
        // 1fr one. Both are registered when a format's rows are being built.
        void AddFieldRow(const std::shared_ptr<UltraCanvasLabel>& label,
                         const std::shared_ptr<UltraCanvasUIElement>& control);
        void AddFieldRow(const std::string& id, const std::string& labelText,
                         const std::shared_ptr<UltraCanvasUIElement>& control);
        // A control that is its own caption (a checkbox, a note), across both
        // columns.
        void AddWideRow(const std::shared_ptr<UltraCanvasUIElement>& element);
        // A small bold caption with a rule above it, grouping what follows.
        std::shared_ptr<UltraCanvasLabel> AddSectionHeading(const std::string& id, const std::string& text,
                                                            std::shared_ptr<UltraCanvasUIElement>* outRule = nullptr);
        // Rows created until EndFormatRows() belong to `format`.
        void BeginFormatRows(UCImageSaveFormat format);
        void EndFormatRows();
        // A row container for controls that share one cell (w × h, slider +
        // value, checkbox + button).
        std::shared_ptr<UltraCanvasContainer> MakeCellRow(const std::string& id, float gap = 8.0f);
        void SizeButtonToLabel(const std::shared_ptr<UltraCanvasButton>& button);

        // ===== INTERNAL METHODS =====
        void BuildLayout();
        void CreateFileSection();
        void CreateImageSection();
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
        // ===== Phase 1 additions =====
        void CreateTgaOptions();
        void CreatePcxOptions();
        void CreatePnmOptions();
        void CreateExrOptions();
        void CreateDpxOptions();
        void CreateCinOptions();
        void CreatePsdOptions();
        void CreateSgiOptions();

        void WireCallbacks();
        void UpdateFormatOptions();
        // Shows the metadata controls when the source image has metadata to
        // show, and reports how much.
        void UpdateMetadataControls();
        void ShowMetadataPopup();
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