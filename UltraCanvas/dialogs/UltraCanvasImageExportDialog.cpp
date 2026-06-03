// dialogs/UltraCanvasImageExportDialog.cpp
// Implementation of comprehensive bitmap file save dialog
// Version: 2.3.0
// Last Modified: 2026-06-01
// Author: UltraCanvas Framework
//
// ARCHITECTURE: Uses UltraCanvas layout system (VBox, HBox, Grid)
// Container handles event propagation automatically - NO manual OnEvent forwarding
//
// CHANGES v2.1.0: Aligned controls with actual UltraCanvasImage.h export option structures
// - JPEG: subsampling changed from dropdown to checkbox (bool in struct)
// - GIF: Removed maxColors slider (not in GifExportOptions), interlace is bool
// - AVIF: Removed bitDepthDropdown (use colorDepth), removed hdr references
// - QOI: Colorspace changed from dropdown to checkbox (linearColorspace bool)
// - Removed metadata options that are commented out in UltraCanvasImage.h

#include "UltraCanvasContainer.h"
#include "UltraCanvasSpacer.h"
#include "CSSLayout/CSSLayout.h"
#include "UltraCanvasImageExportDialog.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <fmt/os.h>

namespace UltraCanvas {

// ============================================================================
// IMAGE FORMAT INFO IMPLEMENTATION
// ============================================================================

    ImageFormatInfo ImageFormatInfo::GetInfo(UCImageSaveFormat format) {
        ImageFormatInfo info;
        info.format = format;

        switch (format) {
            case UCImageSaveFormat::PNG:
                info.name = "PNG";
                info.extension = "png";
                info.description = "Portable Network Graphics - Lossless with transparency";
                info.supportsTransparency = true;
                info.supportsLossless = true;
                info.supportsLossy = false;
                info.supportedDepths = {UCImageSave::ColorDepth::Indexed_8bit, UCImageSave::ColorDepth::RGB_8bit,
                                        UCImageSave::ColorDepth::RGB_16bit};
                break;

            case UCImageSaveFormat::JPEG:
                info.name = "JPEG";
                info.extension = "jpg";
                info.description = "Lossy compression for photographs";
                info.supportsTransparency = false;
                info.supportsLossless = false;
                info.supportsLossy = true;
//                info.supportedDepths = {UCImageSave::ColorDepth::RGB_24bit};
                break;

            case UCImageSaveFormat::WEBP:
                info.name = "WebP";
                info.extension = "webp";
                info.description = "Modern format with lossy and lossless modes";
                info.supportsTransparency = true;
                info.supportsLossless = true;
                info.supportsLossy = true;
//                info.supportedDepths = {UCImageSave::ColorDepth::RGB_24bit, UCImageSave::ColorDepth::RGBA_32bit};
                break;

            case UCImageSaveFormat::AVIF:
                info.name = "AVIF";
                info.extension = "avif";
                info.description = "AV1 Image Format";
                info.supportsTransparency = true;
                info.supportsLossless = true;
                info.supportsLossy = true;
                info.supportsHDR = false;  // hdr is commented out in AvifExportOptions
//                info.supportedDepths = {UCImageSave::ColorDepth::RGB_24bit, UCImageSave::ColorDepth::RGBA_32bit};
                break;

            case UCImageSaveFormat::HEIF:
                info.name = "HEIF";
                info.extension = "heif";
                info.description = "High Efficiency Image Format";
                info.supportsTransparency = true;
                info.supportsLossless = true;
                info.supportsLossy = true;
                info.supportsHDR = false;
//                info.supportedDepths = {UCImageSave::ColorDepth::RGB_24bit, UCImageSave::ColorDepth::RGBA_32bit};
                break;

            case UCImageSaveFormat::GIF:
                info.name = "GIF";
                info.extension = "gif";
                info.description = "256 colors with animation support";
                info.supportsTransparency = true;  // preserveTransparency is commented out
                info.supportsLossless = true;
                info.supportedDepths = {UCImageSave::ColorDepth::Monochrome_1bit, UCImageSave::ColorDepth::Indexed_4bit, UCImageSave::ColorDepth::Indexed_8bit};
                break;

            case UCImageSaveFormat::BMP:
                info.name = "BMP";
                info.extension = "bmp";
                info.description = "Windows Bitmap - Uncompressed";
                info.supportsTransparency = true;
                info.supportsLossless = true;
//                info.supportedDepths = {UCImageSave::ColorDepth::Indexed_8bit, UCImageSave::ColorDepth::RGB_16bit,
//                                        UCImageSave::ColorDepth::RGB_24bit, UCImageSave::ColorDepth::RGBA_32bit};
                break;

            case UCImageSaveFormat::TIFF:
                info.name = "TIFF";
                info.extension = "tiff";
                info.description = "Professional archival format";
                info.supportsTransparency = true;
                info.supportsLossless = true;
                info.supportsLossy = true;
                info.supportsHDR = false;
                info.supportedDepths = {UCImageSave::ColorDepth::Indexed_8bit, UCImageSave::ColorDepth::RGB_8bit,
                                        UCImageSave::ColorDepth::RGB_16bit};
                break;

            case UCImageSaveFormat::ICO:
                info.name = "ICO";
                info.extension = "ico";
                info.description = "Windows Icon format";
                info.supportsTransparency = true;
                info.supportsLossless = true;
//                info.supportedDepths = {UCImageSave::ColorDepth::Indexed_8bit, UCImageSave::ColorDepth::RGBA_32bit};
                break;

            case UCImageSaveFormat::JPEG2000:
                info.name = "JPEG 2000";
                info.extension = "jp2";
                info.description = "Modern JPEG replacement";
                info.supportsTransparency = true;  // preserveTransparency commented but colorDepth supports RGBA
                info.supportsLossless = true;
                info.supportsLossy = true;
                info.supportsHDR = false;
//                info.supportedDepths = {UCImageSave::ColorDepth::RGB_24bit, UCImageSave::ColorDepth::RGBA_32bit,
//                                        UCImageSave::ColorDepth::RGB_48bit, UCImageSave::ColorDepth::RGBA_64bit};
                break;

            case UCImageSaveFormat::JXL:
                info.name = "JPEG XL";
                info.extension = "jxl";
                info.description = "Next-gen image — lossless and lossy modes";
                info.supportsTransparency = true;
                info.supportsLossless = true;
                info.supportsLossy = true;
                break;

            case UCImageSaveFormat::PPM:
                info.name = "PPM";
                info.extension = "ppm";
                info.description = "Portable Pixmap (ASCII or binary)";
                info.supportsLossless = true;
                break;

            case UCImageSaveFormat::QOI:
                info.name = "QOI";
                info.extension = "qoi";
                info.description = "Quite OK Image — fast lossless";
                info.supportsTransparency = true;
                info.supportsLossless = true;
                break;

            // ===== Phase 1 additions =====
            case UCImageSaveFormat::TGA:
                info.name = "TGA";
                info.extension = "tga";
                info.description = "Truevision TGA — lossless with alpha";
                info.supportsTransparency = true;
                info.supportsLossless = true;
                break;

            case UCImageSaveFormat::PCX:
                info.name = "PCX";
                info.extension = "pcx";
                info.description = "ZSoft Paintbrush";
                info.supportsLossless = true;
                break;

            case UCImageSaveFormat::PGM:
                info.name = "PGM";
                info.extension = "pgm";
                info.description = "Portable Graymap";
                info.supportsLossless = true;
                break;

            case UCImageSaveFormat::PBM:
                info.name = "PBM";
                info.extension = "pbm";
                info.description = "Portable Bitmap (1-bit)";
                info.supportsLossless = true;
                break;

            case UCImageSaveFormat::PFM:
                info.name = "PFM";
                info.extension = "pfm";
                info.description = "Portable Float Map (HDR)";
                info.supportsLossless = true;
                info.supportsHDR = true;
                break;

            case UCImageSaveFormat::HDR:
                info.name = "HDR";
                info.extension = "hdr";
                info.description = "Radiance RGBE — high dynamic range";
                info.supportsLossless = true;
                info.supportsHDR = true;
                break;

            case UCImageSaveFormat::EXR:
                info.name = "EXR";
                info.extension = "exr";
                info.description = "OpenEXR — floating-point HDR with alpha";
                info.supportsTransparency = true;
                info.supportsLossless = true;
                info.supportsHDR = true;
                break;

            case UCImageSaveFormat::DPX:
                info.name = "DPX";
                info.extension = "dpx";
                info.description = "SMPTE DPX — digital cinema";
                info.supportsLossless = true;
                break;

            case UCImageSaveFormat::CIN:
                info.name = "Cineon";
                info.extension = "cin";
                info.description = "Kodak Cineon — film scanning";
                info.supportsLossless = true;
                break;

            case UCImageSaveFormat::FITS:
                info.name = "FITS";
                info.extension = "fits";
                info.description = "Flexible Image Transport System (astronomy)";
                info.supportsLossless = true;
                info.supportsHDR = true;
                break;

            case UCImageSaveFormat::PSD:
                info.name = "PSD";
                info.extension = "psd";
                info.description = "Adobe Photoshop";
                info.supportsTransparency = true;
                info.supportsLossless = true;
                break;

            case UCImageSaveFormat::SGI:
                info.name = "SGI";
                info.extension = "sgi";
                info.description = "Silicon Graphics Image";
                info.supportsTransparency = true;
                info.supportsLossless = true;
                break;

            case UCImageSaveFormat::FARBFELD:
                info.name = "Farbfeld";
                info.extension = "ff";
                info.description = "Farbfeld — lossless 16-bit RGBA";
                info.supportsTransparency = true;
                info.supportsLossless = true;
                break;

            default:
                info.name = "Unknown";
                info.extension = "";
                break;
        }

        return info;
    }

    std::vector<ImageFormatInfo> ImageFormatInfo::GetAllFormats() {
        return {
                GetInfo(UCImageSaveFormat::PNG),
                GetInfo(UCImageSaveFormat::JPEG),
                GetInfo(UCImageSaveFormat::WEBP),
                GetInfo(UCImageSaveFormat::AVIF),
                GetInfo(UCImageSaveFormat::HEIF),
                GetInfo(UCImageSaveFormat::GIF),
                GetInfo(UCImageSaveFormat::BMP),
                GetInfo(UCImageSaveFormat::TIFF),
                GetInfo(UCImageSaveFormat::ICO),
                GetInfo(UCImageSaveFormat::JXL),
                GetInfo(UCImageSaveFormat::JPEG2000),
                GetInfo(UCImageSaveFormat::QOI),
                GetInfo(UCImageSaveFormat::PPM),
                // ===== Phase 1 additions =====
                GetInfo(UCImageSaveFormat::TGA),
                GetInfo(UCImageSaveFormat::PCX),
                GetInfo(UCImageSaveFormat::PGM),
                GetInfo(UCImageSaveFormat::PBM),
                GetInfo(UCImageSaveFormat::PFM),
                GetInfo(UCImageSaveFormat::HDR),
                GetInfo(UCImageSaveFormat::EXR),
                GetInfo(UCImageSaveFormat::DPX),
                GetInfo(UCImageSaveFormat::CIN),
                GetInfo(UCImageSaveFormat::FITS),
                GetInfo(UCImageSaveFormat::PSD),
                GetInfo(UCImageSaveFormat::SGI),
                GetInfo(UCImageSaveFormat::FARBFELD),
        };
    }

    std::string ImageFormatInfo::GetExtension(UCImageSaveFormat format) {
        return GetInfo(format).extension;
    }

    UCImageSaveFormat ImageFormatInfo::FromExtension(const std::string& ext) {
        std::string lower = ext;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        if (lower == "png") return UCImageSaveFormat::PNG;
        if (lower == "jpg" || lower == "jpeg") return UCImageSaveFormat::JPEG;
        if (lower == "webp") return UCImageSaveFormat::WEBP;
        if (lower == "avif") return UCImageSaveFormat::AVIF;
        if (lower == "heif" || lower == "heic") return UCImageSaveFormat::HEIF;
        if (lower == "gif") return UCImageSaveFormat::GIF;
        if (lower == "bmp") return UCImageSaveFormat::BMP;
        if (lower == "tiff" || lower == "tif") return UCImageSaveFormat::TIFF;
        if (lower == "ico") return UCImageSaveFormat::ICO;
        if (lower == "jp2" || lower == "j2k" || lower == "jpf") return UCImageSaveFormat::JPEG2000;
        if (lower == "jxl") return UCImageSaveFormat::JXL;
        if (lower == "qoi") return UCImageSaveFormat::QOI;
        if (lower == "ppm") return UCImageSaveFormat::PPM;
        // ===== Phase 1 additions =====
        if (lower == "tga" || lower == "targa" || lower == "icb" || lower == "vda" || lower == "vst")
            return UCImageSaveFormat::TGA;
        if (lower == "pcx") return UCImageSaveFormat::PCX;
        if (lower == "pgm") return UCImageSaveFormat::PGM;
        if (lower == "pbm") return UCImageSaveFormat::PBM;
        if (lower == "pfm") return UCImageSaveFormat::PFM;
        if (lower == "hdr" || lower == "rgbe" || lower == "pic") return UCImageSaveFormat::HDR;
        if (lower == "exr") return UCImageSaveFormat::EXR;
        if (lower == "dpx") return UCImageSaveFormat::DPX;
        if (lower == "cin") return UCImageSaveFormat::CIN;
        if (lower == "fits" || lower == "fit" || lower == "fts") return UCImageSaveFormat::FITS;
        if (lower == "psd") return UCImageSaveFormat::PSD;
        if (lower == "sgi" || lower == "rgb" || lower == "rgba" || lower == "bw")
            return UCImageSaveFormat::SGI;
        if (lower == "ff" || lower == "farbfeld") return UCImageSaveFormat::FARBFELD;

        return UCImageSaveFormat::PNG;
    }

// ============================================================================
// DIALOG STYLE
// ============================================================================

    ImageExportDialogStyle ImageExportDialogStyle::Dark() {
        ImageExportDialogStyle s;
        s.backgroundColor = Color(40, 40, 45, 255);
        s.borderColor = Color(70, 70, 75, 255);
        s.accentColor = Color(0, 150, 255, 255);
        s.textColor = Color(230, 230, 230, 255);
        s.labelColor = Color(180, 180, 180, 255);
        return s;
    }

// ============================================================================
// CONSTRUCTOR
// ============================================================================

    UltraCanvasImageExportDialog::UltraCanvasImageExportDialog()
            : UltraCanvasWindow() {

        config_.width = 520;
        config_.height = 580;
        config_.deleteOnClose = true;
        config_.title = "Save image";

        SetPadding(static_cast<int>(style.padding));

        BuildLayout();
        WireCallbacks();
    }

    UltraCanvasImageExportDialog::UltraCanvasImageExportDialog(vips::VImage& img)
            : UltraCanvasImageExportDialog() {
        SetSourceImage(img);
    }

// ============================================================================
// LAYOUT BUILDING - Using VBox, HBox, Grid layouts
// No manual coordinate calculations!
// ============================================================================

    void UltraCanvasImageExportDialog::BuildLayout() {
        this->layout.SetFlexColumn();
        this->layout.SetFlexGap(static_cast<int>(style.spacing));

        CreateHeaderSection();
        CreateOptionsSection();
        CreateFormatOptionsSection();
        CreateMetadataSection();
        CreateFooterSection();

        AddChild(headerSection);
        headerSection->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        AddChild(optionsSection);
        optionsSection->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        AddChild(formatOptionsSection);
        formatOptionsSection->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        AddChild(metadataSection);
        metadataSection->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        AddChild(footerSection);
        footerSection->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        UpdateFormatOptions();
        UpdateFileSizeEstimate();
    }

    void UltraCanvasImageExportDialog::CreateHeaderSection() {
        headerSection = std::make_shared<UltraCanvasContainer>("HeaderSection", 0, 0, 0, 70);

        headerSection->layout.SetGrid();
        CSSLayout::GridTrackSize colAuto;
        CSSLayout::GridTrackSize colStar; colStar.kind = CSSLayout::GridTrackSizeKind::Fr; colStar.value = CSSLayout::Dimension::Fr(1);
        headerSection->layout.SetGridColumns({colAuto, colStar});
        headerSection->layout.SetGridRows(std::vector<CSSLayout::GridTrackSize>(2));
        headerSection->layout.SetGridGap(static_cast<int>(style.spacing));

        fileNameLabel = std::make_shared<UltraCanvasLabel>("FileNameLabel", 0, 0, 80, 24);
        fileNameLabel->SetText("Name:");
        fileNameLabel->SetFontSize(style.labelFontSize);
        fileNameLabel->SetTextColor(style.labelColor);

        fileNameInput = std::make_shared<UltraCanvasTextInput>("FileNameInput", 0, 0, 200, 28);
        fileNameInput->SetPlaceholder("Enter file name...");

        headerSection->AddChild(fileNameLabel); fileNameLabel->layoutItem.SetGridRowColSimplified(0, 0);
        headerSection->AddChild(fileNameInput); fileNameInput->layoutItem.SetGridRowColSimplified(0, 1);

        formatLabel = std::make_shared<UltraCanvasLabel>("FormatLabel", 0, 0, 80, 24);
        formatLabel->SetText("Format:");
        formatLabel->SetFontSize(style.labelFontSize);
        formatLabel->SetTextColor(style.labelColor);

        formatDropdown = std::make_shared<UltraCanvasDropdown>("FormatDropdown", 0, 0, 200, 28);

        // Filter the format list against the installed libvips build —
        // hide entries whose saver isn't compiled in. PNG is always kept
        // as a safe fallback so the dropdown is never empty.
        availableFormats.clear();
        for (const auto& fmt : ImageFormatInfo::GetAllFormats()) {
            const std::string dotExt = "." + fmt.extension;
            if (VipsCanSave(dotExt) || fmt.format == UCImageSaveFormat::PNG) {
                availableFormats.push_back(fmt);
            }
        }
        for (const auto& fmt : availableFormats) {
            formatDropdown->AddItem(fmt.name + " (." + fmt.extension + ")");
        }
        formatDropdown->SetSelectedIndex(0);

        headerSection->AddChild(formatLabel); formatLabel->layoutItem.SetGridRowColSimplified(1, 0);
        headerSection->AddChild(formatDropdown); formatDropdown->layoutItem.SetGridRowColSimplified(1, 1);

        AddChild(headerSection);
    }

    void UltraCanvasImageExportDialog::CreateOptionsSection() {
        optionsSection = std::make_shared<UltraCanvasContainer>("OptionsSection", 0, 0, 0, 160);

        optionsSection->layout.SetGrid();
        CSSLayout::GridTrackSize colAuto;
        CSSLayout::GridTrackSize colStar; colStar.kind = CSSLayout::GridTrackSizeKind::Fr; colStar.value = CSSLayout::Dimension::Fr(1);
        optionsSection->layout.SetGridColumns({colAuto, colStar});
        optionsSection->layout.SetGridRows(std::vector<CSSLayout::GridTrackSize>(4));
        optionsSection->layout.SetGridGap(static_cast<int>(style.spacing));

        int row = 0;

        // ----- Row 0: Dimensions -----
        sizeLabel = std::make_shared<UltraCanvasLabel>("SizeLabel", 0, 0, 80, 24);
        sizeLabel->SetText("Size:");
        sizeLabel->SetFontSize(style.labelFontSize);
        sizeLabel->SetTextColor(style.labelColor);

        auto sizeRow = std::make_shared<UltraCanvasContainer>("SizeRow", 0, 0, 280, 28);
        sizeRow->layout.SetFlexRow();
        sizeRow->layout.SetFlexGap(5);

        widthInput = std::make_shared<UltraCanvasTextInput>("WidthInput", 0, 0, 70, 28);
        widthInput->SetText("1920");

        xLabel = std::make_shared<UltraCanvasLabel>("XLabel", 0, 0, 20, 28);
        xLabel->SetText("×");
        xLabel->SetAlignment(TextAlignment::Center);

        heightInput = std::make_shared<UltraCanvasTextInput>("HeightInput", 0, 0, 70, 28);
        heightInput->SetText("1080");

        aspectRatioCheckbox = UltraCanvasCheckbox::CreateCheckbox("AspectLock", 0, 0, 100, 24, "Lock", true);

        sizeRow->AddChild(widthInput);
        sizeRow->AddChild(xLabel);
        sizeRow->AddChild(heightInput);
        sizeRow->AddSpacer(10);
        sizeRow->AddChild(aspectRatioCheckbox);

        optionsSection->AddChild(sizeLabel); sizeLabel->layoutItem.SetGridRowColSimplified(row, 0);
        optionsSection->AddChild(sizeRow); sizeRow->layoutItem.SetGridRowColSimplified(row, 1);
        row++;

        // ----- Row 1: Color depth -----
        colorDepthLabel = std::make_shared<UltraCanvasLabel>("ColorDepthLabel", 0, 0, 80, 24);
        colorDepthLabel->SetText("Depth:");
        colorDepthLabel->SetFontSize(style.labelFontSize);
        colorDepthLabel->SetTextColor(style.labelColor);

        colorDepthDropdown = std::make_shared<UltraCanvasDropdown>("ColorDepthDropdown", 0, 0, 200, 28);

        optionsSection->AddChild(colorDepthLabel); colorDepthLabel->layoutItem.SetGridRowColSimplified(row, 0);
        optionsSection->AddChild(colorDepthDropdown); colorDepthDropdown->layoutItem.SetGridRowColSimplified(row, 1);
        row++;

        // ----- Row 2: Transparency -----
        transparencyLabel = std::make_shared<UltraCanvasLabel>("TransparencyLabel", 0, 0, 80, 24);
        transparencyLabel->SetText("Alpha:");
        transparencyLabel->SetFontSize(style.labelFontSize);
        transparencyLabel->SetTextColor(style.labelColor);

        transparencyCheckbox = UltraCanvasCheckbox::CreateCheckbox("TransparencyCheck", 0, 0, 200, 24, "Preserve transparency", true);

        optionsSection->AddChild(transparencyLabel); transparencyLabel->layoutItem.SetGridRowColSimplified(row, 0);
        optionsSection->AddChild(transparencyCheckbox); transparencyCheckbox->layoutItem.SetGridRowColSimplified(row, 1);
        row++;

        // ----- Row 3: Quality slider -----
        qualityLabel = std::make_shared<UltraCanvasLabel>("QualityLabel", 0, 0, 80, 24);
        qualityLabel->SetText("Quality:");
        qualityLabel->SetFontSize(style.labelFontSize);
        qualityLabel->SetTextColor(style.labelColor);

        auto qualityRow = std::make_shared<UltraCanvasContainer>("QualityRow", 0, 0, 280, 28);
        qualityRow->layout.SetFlexRow();
        qualityRow->layout.SetFlexGap(10);

        qualitySlider = std::make_shared<UltraCanvasSlider>("QualitySlider", 0, 0, 180, 24);
        qualitySlider->SetRange(0, 100);
        qualitySlider->SetValue(85);

        qualityValueLabel = std::make_shared<UltraCanvasLabel>("QualityValue", 0, 0, 50, 24);
        qualityValueLabel->SetText("85%");
        qualityValueLabel->SetFontSize(style.valueFontSize);

        qualityRow->AddChild(qualitySlider); qualitySlider->layoutItem.SetFlexGrow(1);
        qualityRow->AddChild(qualityValueLabel);

        optionsSection->AddChild(qualityLabel); qualityLabel->layoutItem.SetGridRowColSimplified(row, 0);
        optionsSection->AddChild(qualityRow); qualityRow->layoutItem.SetGridRowColSimplified(row, 1);

        AddChild(optionsSection);
        UpdateColorDepthOptions();
    }

    void UltraCanvasImageExportDialog::CreateFormatOptionsSection() {
        formatOptionsSection = std::make_shared<UltraCanvasContainer>("FormatOptionsSection", 0, 0, 300, 120);

        CreatePngOptions();
        CreateJpegOptions();
        CreateWebpOptions();
        CreateAvifOptions();
        CreateGifOptions();
        CreateTiffOptions();
        CreateQoiOptions();
        // ===== Phase 1 additions =====
        CreateTgaOptions();
        CreatePcxOptions();
        CreatePnmOptions();
        CreateExrOptions();
        CreateDpxOptions();
        CreateCinOptions();
        CreatePsdOptions();
        CreateSgiOptions();

        AddChild(formatOptionsSection);
        HideAllFormatOptions();
    }

    // PNG options - matches PngExportOptions: compressionLevel, interlace, preserveTransparency, colorDepth
    void UltraCanvasImageExportDialog::CreatePngOptions() {
        pngOptionsContainer = std::make_shared<UltraCanvasContainer>("PngOptions", 0, 0, 300, 40);
        pngOptionsContainer->layout.SetFlexColumn();
        pngOptionsContainer->layout.SetFlexGap(8);

        pngInterlaceCheckbox = UltraCanvasCheckbox::CreateCheckbox("PngInterlace", 0, 0, 250, 24, "Interlacing", false);

        pngOptionsContainer->AddChild(pngInterlaceCheckbox);

        formatOptionsSection->AddChild(pngOptionsContainer);
    }

    // JPEG options - matches JpegExportOptions: quality, progressive, subsampling (bool!), optimizeHuffman
    void UltraCanvasImageExportDialog::CreateJpegOptions() {
        jpegOptionsContainer = std::make_shared<UltraCanvasContainer>("JpegOptions", 0, 0, 0, 90);
        jpegOptionsContainer->layout.SetFlexColumn();
        jpegOptionsContainer->layout.SetFlexGap(8);

        jpegProgressiveCheckbox = UltraCanvasCheckbox::CreateCheckbox("JpegProgressive", 0, 0, 250, 24, "Progressive encoding", false);
        jpegOptimizeHuffmanCheckbox = UltraCanvasCheckbox::CreateCheckbox("JpegOptHuffman", 0, 0, 250, 24, "Optimize Huffman tables", true);
        // subsampling is a bool in JpegExportOptions, not ChromaSubsampling enum
        jpegSubsamplingCheckbox = UltraCanvasCheckbox::CreateCheckbox("JpegSubsampling", 0, 0, 250, 24, "Chroma subsampling", false);

        jpegOptionsContainer->AddChild(jpegProgressiveCheckbox);
        jpegOptionsContainer->AddChild(jpegOptimizeHuffmanCheckbox);
        jpegOptionsContainer->AddChild(jpegSubsamplingCheckbox);

        formatOptionsSection->AddChild(jpegOptionsContainer);
    }

    // WebP options - matches WebpExportOptions: quality, lossless, effort, targetSize, preserveTransparency, alphaQuality
    void UltraCanvasImageExportDialog::CreateWebpOptions() {
        webpOptionsContainer = std::make_shared<UltraCanvasContainer>("WebpOptions", 0, 0, 0, 100);
        webpOptionsContainer->layout.SetFlexColumn();
        webpOptionsContainer->layout.SetFlexGap(8);

        webpLosslessCheckbox = UltraCanvasCheckbox::CreateCheckbox("WebpLossless", 0, 0, 250, 24, "Lossless compression", false);

        // Effort row
        auto effortRow = std::make_shared<UltraCanvasContainer>("EffortRow", 0, 0, 350, 28);
        effortRow->layout.SetFlexRow();
        effortRow->layout.SetFlexGap(10);

        auto effortLabel = std::make_shared<UltraCanvasLabel>("EffortLabel", 0, 0, 100, 24);
        effortLabel->SetText("Effort (0-6):");
        effortLabel->SetFontSize(style.labelFontSize);
        effortLabel->SetTextColor(style.labelColor);

        webpEffortSlider = std::make_shared<UltraCanvasSlider>("WebpEffort", 0, 0, 150, 24);
        webpEffortSlider->SetRange(0, 6);
        webpEffortSlider->SetValue(4);

        effortRow->AddChild(effortLabel);
        effortRow->AddChild(webpEffortSlider); webpEffortSlider->layoutItem.SetFlexGrow(1);

        // Alpha quality row
        auto alphaRow = std::make_shared<UltraCanvasContainer>("AlphaRow", 0, 0, 350, 28);
        alphaRow->layout.SetFlexRow();
        alphaRow->layout.SetFlexGap(10);

        auto alphaLabel = std::make_shared<UltraCanvasLabel>("AlphaLabel", 0, 0, 100, 24);
        alphaLabel->SetText("Alpha quality:");
        alphaLabel->SetFontSize(style.labelFontSize);
        alphaLabel->SetTextColor(style.labelColor);

        webpAlphaQualitySlider = std::make_shared<UltraCanvasSlider>("WebpAlphaQuality", 0, 0, 150, 24);
        webpAlphaQualitySlider->SetRange(0, 100);
        webpAlphaQualitySlider->SetValue(100);

        alphaRow->AddChild(alphaLabel);
        alphaRow->AddChild(webpAlphaQualitySlider); webpAlphaQualitySlider->layoutItem.SetFlexGrow(1);

        webpOptionsContainer->AddChild(webpLosslessCheckbox);
        webpOptionsContainer->AddChild(effortRow);
        webpOptionsContainer->AddChild(alphaRow);

        formatOptionsSection->AddChild(webpOptionsContainer);
    }

    // AVIF options - matches AvifExportOptions: quality, lossless, speed, preserveTransparency, colorDepth
    // Note: bitDepth and hdr are commented out in UltraCanvasImage.h
    void UltraCanvasImageExportDialog::CreateAvifOptions() {
        avifOptionsContainer = std::make_shared<UltraCanvasContainer>("AvifOptions", 0, 0, 0, 70);
        avifOptionsContainer->layout.SetFlexColumn();
        avifOptionsContainer->layout.SetFlexGap(8);

        avifLosslessCheckbox = UltraCanvasCheckbox::CreateCheckbox("AvifLossless", 0, 0, 250, 24, "Lossless compression", false);

        // Speed row
        auto speedRow = std::make_shared<UltraCanvasContainer>("SpeedRow", 0, 0, 350, 28);
        speedRow->layout.SetFlexRow();
        speedRow->layout.SetFlexGap(10);

        auto speedLabel = std::make_shared<UltraCanvasLabel>("SpeedLabel", 0, 0, 100, 24);
        speedLabel->SetText("Speed (0-10):");
        speedLabel->SetFontSize(style.labelFontSize);
        speedLabel->SetTextColor(style.labelColor);

        avifSpeedSlider = std::make_shared<UltraCanvasSlider>("AvifSpeed", 0, 0, 150, 24);
        avifSpeedSlider->SetRange(0, 10);
        avifSpeedSlider->SetValue(6);

        speedRow->AddChild(speedLabel);
        speedRow->AddChild(avifSpeedSlider); avifSpeedSlider->layoutItem.SetFlexGrow(1);

        avifOptionsContainer->AddChild(avifLosslessCheckbox);
        avifOptionsContainer->AddChild(speedRow);
        // Note: colorDepth is handled via the common colorDepthDropdown

        formatOptionsSection->AddChild(avifOptionsContainer);
    }

    // GIF options - matches GifExportOptions: colorDepth, interlace, dithering
    // Note: maxColors is NOT in the struct, preserveTransparency is commented out
    void UltraCanvasImageExportDialog::CreateGifOptions() {
        gifOptionsContainer = std::make_shared<UltraCanvasContainer>("GifOptions", 0, 0, 0, 60);
        gifOptionsContainer->layout.SetFlexColumn();
        gifOptionsContainer->layout.SetFlexGap(8);

        gifDitheringCheckbox = UltraCanvasCheckbox::CreateCheckbox("GifDithering", 0, 0, 250, 24, "Enable dithering", true);
        gifInterlaceCheckbox = UltraCanvasCheckbox::CreateCheckbox("GifInterlace", 0, 0, 250, 24, "Interlaced", false);

        gifOptionsContainer->AddChild(gifDitheringCheckbox);
        gifOptionsContainer->AddChild(gifInterlaceCheckbox);

        formatOptionsSection->AddChild(gifOptionsContainer);
    }

    // TIFF options - matches TiffExportOptions: compression, colorDepth, multiPage
    void UltraCanvasImageExportDialog::CreateTiffOptions() {
        tiffOptionsContainer = std::make_shared<UltraCanvasContainer>("TiffOptions", 0, 0, 0, 70);
        tiffOptionsContainer->layout.SetFlexColumn();
        tiffOptionsContainer->layout.SetFlexGap(8);

        // Compression row
        auto compressionRow = std::make_shared<UltraCanvasContainer>("CompressionRow", 0, 0, 350, 28);
        compressionRow->layout.SetFlexRow();
        compressionRow->layout.SetFlexGap(10);

        auto compressionLabel = std::make_shared<UltraCanvasLabel>("CompressionLabel", 0, 0, 100, 24);
        compressionLabel->SetText("Compression:");
        compressionLabel->SetFontSize(style.labelFontSize);
        compressionLabel->SetTextColor(style.labelColor);

        tiffCompressionDropdown = std::make_shared<UltraCanvasDropdown>("TiffCompression", 0, 0, 150, 28);
        // Match TiffCompression enum order: NoCompression, JPEGCompression, DeflateCompression, PackBitsCompression, LZWCompression, ZSTDCompression, WEBPCompression
        tiffCompressionDropdown->AddItem("None");
        tiffCompressionDropdown->AddItem("JPEG");
        tiffCompressionDropdown->AddItem("Deflate/ZIP");
        tiffCompressionDropdown->AddItem("PackBits");
        tiffCompressionDropdown->AddItem("LZW");
        tiffCompressionDropdown->AddItem("ZSTD");
        tiffCompressionDropdown->AddItem("WebP");
        tiffCompressionDropdown->SetSelectedIndex(4);  // Default to LZW

        compressionRow->AddChild(compressionLabel);
        compressionRow->AddChild(tiffCompressionDropdown);

        tiffMultiPageCheckbox = UltraCanvasCheckbox::CreateCheckbox("TiffMultiPage", 0, 0, 250, 24, "Multi-page TIFF", false);

        tiffOptionsContainer->AddChild(compressionRow);
        tiffOptionsContainer->AddChild(tiffMultiPageCheckbox);

        formatOptionsSection->AddChild(tiffOptionsContainer);
    }

    // QOI options - matches QoiExportOptions: hasAlpha, linearColorspace
    void UltraCanvasImageExportDialog::CreateQoiOptions() {
        qoiOptionsContainer = std::make_shared<UltraCanvasContainer>("QoiOptions", 0, 0, 0, 100);
        qoiOptionsContainer->layout.SetFlexColumn();
        qoiOptionsContainer->layout.SetFlexGap(8);

        qoiAlphaCheckbox = UltraCanvasCheckbox::CreateCheckbox("QoiAlpha", 0, 0, 250, 24, "Include alpha channel", true);
        // linearColorspace is a bool, use checkbox instead of dropdown
        qoiLinearColorspaceCheckbox = UltraCanvasCheckbox::CreateCheckbox("QoiLinear", 0, 0, 250, 24, "Linear colorspace (default: sRGB)", false);

        qoiInfoLabel = std::make_shared<UltraCanvasLabel>("QoiInfo", 0, 0, 350, 40);
        qoiInfoLabel->SetText("QOI: Fast lossless compression\n20-50x faster encoding than PNG");
        qoiInfoLabel->SetFontSize(style.valueFontSize);
        qoiInfoLabel->SetTextColor(style.labelColor);

        qoiOptionsContainer->AddChild(qoiAlphaCheckbox);
        qoiOptionsContainer->AddChild(qoiLinearColorspaceCheckbox);
        qoiOptionsContainer->AddChild(qoiInfoLabel);

        formatOptionsSection->AddChild(qoiOptionsContainer);
    }

    // ===== Phase 1: simple checkbox-only formats =====
    void UltraCanvasImageExportDialog::CreateTgaOptions() {
        tgaOptionsContainer = std::make_shared<UltraCanvasContainer>("TgaOptions", 0, 0, 0, 40);
        tgaOptionsContainer->layout.SetFlexColumn();
        tgaOptionsContainer->layout.SetFlexGap(8);

        tgaRleCheckbox = UltraCanvasCheckbox::CreateCheckbox("TgaRle", 0, 0, 250, 24, "RLE compression", true);
        tgaOptionsContainer->AddChild(tgaRleCheckbox);

        formatOptionsSection->AddChild(tgaOptionsContainer);
    }

    void UltraCanvasImageExportDialog::CreatePcxOptions() {
        pcxOptionsContainer = std::make_shared<UltraCanvasContainer>("PcxOptions", 0, 0, 0, 40);
        pcxOptionsContainer->layout.SetFlexColumn();
        pcxOptionsContainer->layout.SetFlexGap(8);

        pcxRleCheckbox = UltraCanvasCheckbox::CreateCheckbox("PcxRle", 0, 0, 250, 24, "RLE compression", true);
        pcxOptionsContainer->AddChild(pcxRleCheckbox);

        formatOptionsSection->AddChild(pcxOptionsContainer);
    }

    void UltraCanvasImageExportDialog::CreatePnmOptions() {
        pnmOptionsContainer = std::make_shared<UltraCanvasContainer>("PnmOptions", 0, 0, 0, 40);
        pnmOptionsContainer->layout.SetFlexColumn();
        pnmOptionsContainer->layout.SetFlexGap(8);

        pnmBinaryCheckbox = UltraCanvasCheckbox::CreateCheckbox("PnmBinary", 0, 0, 280, 24, "Binary encoding (uncheck for ASCII)", true);
        pnmOptionsContainer->AddChild(pnmBinaryCheckbox);

        formatOptionsSection->AddChild(pnmOptionsContainer);
    }

    void UltraCanvasImageExportDialog::CreateExrOptions() {
        exrOptionsContainer = std::make_shared<UltraCanvasContainer>("ExrOptions", 0, 0, 0, 40);
        exrOptionsContainer->layout.SetFlexRow();
        exrOptionsContainer->layout.SetFlexGap(10);

        auto label = std::make_shared<UltraCanvasLabel>("ExrCompLabel", 0, 0, 110, 24);
        label->SetText("Compression:");
        label->SetFontSize(style.labelFontSize);
        label->SetTextColor(style.labelColor);

        exrCompressionDropdown = std::make_shared<UltraCanvasDropdown>("ExrCompression", 0, 0, 150, 28);
        exrCompressionDropdown->AddItem("None");
        exrCompressionDropdown->AddItem("RLE");
        exrCompressionDropdown->AddItem("ZIP");
        exrCompressionDropdown->AddItem("PIZ");
        exrCompressionDropdown->AddItem("PXR24");
        exrCompressionDropdown->AddItem("B44");
        exrCompressionDropdown->SetSelectedIndex(2);  // ZIP default

        exrOptionsContainer->AddChild(label);
        exrOptionsContainer->AddChild(exrCompressionDropdown);

        formatOptionsSection->AddChild(exrOptionsContainer);
    }

    void UltraCanvasImageExportDialog::CreateDpxOptions() {
        dpxOptionsContainer = std::make_shared<UltraCanvasContainer>("DpxOptions", 0, 0, 0, 40);
        dpxOptionsContainer->layout.SetFlexRow();
        dpxOptionsContainer->layout.SetFlexGap(10);

        auto label = std::make_shared<UltraCanvasLabel>("DpxDepthLabel", 0, 0, 110, 24);
        label->SetText("Bit depth:");
        label->SetFontSize(style.labelFontSize);
        label->SetTextColor(style.labelColor);

        dpxBitDepthDropdown = std::make_shared<UltraCanvasDropdown>("DpxBitDepth", 0, 0, 100, 28);
        dpxBitDepthDropdown->AddItem("8");
        dpxBitDepthDropdown->AddItem("10");
        dpxBitDepthDropdown->AddItem("12");
        dpxBitDepthDropdown->AddItem("16");
        dpxBitDepthDropdown->SetSelectedIndex(1);  // 10-bit default

        dpxOptionsContainer->AddChild(label);
        dpxOptionsContainer->AddChild(dpxBitDepthDropdown);

        formatOptionsSection->AddChild(dpxOptionsContainer);
    }

    void UltraCanvasImageExportDialog::CreateCinOptions() {
        cinOptionsContainer = std::make_shared<UltraCanvasContainer>("CinOptions", 0, 0, 0, 40);
        cinOptionsContainer->layout.SetFlexRow();
        cinOptionsContainer->layout.SetFlexGap(10);

        auto label = std::make_shared<UltraCanvasLabel>("CinDepthLabel", 0, 0, 110, 24);
        label->SetText("Bit depth:");
        label->SetFontSize(style.labelFontSize);
        label->SetTextColor(style.labelColor);

        cinBitDepthDropdown = std::make_shared<UltraCanvasDropdown>("CinBitDepth", 0, 0, 100, 28);
        cinBitDepthDropdown->AddItem("8");
        cinBitDepthDropdown->AddItem("10");
        cinBitDepthDropdown->AddItem("12");
        cinBitDepthDropdown->AddItem("16");
        cinBitDepthDropdown->SetSelectedIndex(1);

        cinOptionsContainer->AddChild(label);
        cinOptionsContainer->AddChild(cinBitDepthDropdown);

        formatOptionsSection->AddChild(cinOptionsContainer);
    }

    void UltraCanvasImageExportDialog::CreatePsdOptions() {
        psdOptionsContainer = std::make_shared<UltraCanvasContainer>("PsdOptions", 0, 0, 0, 40);
        psdOptionsContainer->layout.SetFlexColumn();
        psdOptionsContainer->layout.SetFlexGap(8);

        psdCompressedCheckbox = UltraCanvasCheckbox::CreateCheckbox("PsdCompressed", 0, 0, 250, 24, "RLE compression", true);
        psdOptionsContainer->AddChild(psdCompressedCheckbox);

        formatOptionsSection->AddChild(psdOptionsContainer);
    }

    void UltraCanvasImageExportDialog::CreateSgiOptions() {
        sgiOptionsContainer = std::make_shared<UltraCanvasContainer>("SgiOptions", 0, 0, 0, 40);
        sgiOptionsContainer->layout.SetFlexColumn();
        sgiOptionsContainer->layout.SetFlexGap(8);

        sgiRleCheckbox = UltraCanvasCheckbox::CreateCheckbox("SgiRle", 0, 0, 250, 24, "RLE compression", false);
        sgiOptionsContainer->AddChild(sgiRleCheckbox);

        formatOptionsSection->AddChild(sgiOptionsContainer);
    }

    void UltraCanvasImageExportDialog::CreateMetadataSection() {
        metadataSection = std::make_shared<UltraCanvasContainer>("MetadataSection", 0, 0, 0, 35);
        metadataSection->layout.SetFlexRow();
        metadataSection->layout.SetFlexGap(20);

        // Note: Most format-specific metadata options (preserveExif, embedICCProfile) are commented out
        // in UltraCanvasImage.h, so we only keep the general preserveMetadata option
        preserveMetadataCheckbox = UltraCanvasCheckbox::CreateCheckbox("PreserveMetadata", 0, 0, 180, 24, "Preserve metadata", true);

        metadataSection->AddChild(preserveMetadataCheckbox);

        AddChild(metadataSection);
    }

    void UltraCanvasImageExportDialog::CreateFooterSection() {
        footerSection = std::make_shared<UltraCanvasContainer>("FooterSection", 0, 0, 0, 45);
        footerSection->layout.SetFlexRow();
        footerSection->layout.SetFlexGap(10);

        fileSizeEstimateLabel = std::make_shared<UltraCanvasLabel>("FileSizeEstimate", 0, 0, 180, 24);
        fileSizeEstimateLabel->SetText("Estimated: ~2.5 MB");
        fileSizeEstimateLabel->SetFontSize(style.valueFontSize);
        fileSizeEstimateLabel->SetTextColor(style.labelColor);

        cancelButton = std::make_shared<UltraCanvasButton>("CancelButton", 0, 0, 90, 32);
        cancelButton->SetText("Cancel");

        saveButton = std::make_shared<UltraCanvasButton>("SaveButton", 0, 0, 90, 32);
        saveButton->SetText("Save");

        footerSection->AddChild(fileSizeEstimateLabel);
        footerSection->AddStretchSpacer(1);
        footerSection->AddChild(cancelButton);
        footerSection->AddChild(saveButton);

        AddChild(footerSection);
    }

// ============================================================================
// CALLBACK WIRING
// All event handling via callbacks - NO manual OnEvent forwarding!
// Container propagates events to children automatically.
// ============================================================================

    void UltraCanvasImageExportDialog::WireCallbacks() {

        // ----- Format dropdown -----
        formatDropdown->onSelectionChanged = [this](int index, const DropdownItem&) {
            if (index >= 0 && index < static_cast<int>(availableFormats.size())) {
                currentFormat = availableFormats[index].format;
                options.format = currentFormat;
                UpdateFormatOptions();
                UpdateFileSizeEstimate();
                if (onFormatChange) onFormatChange(currentFormat);
                if (onOptionsChange) onOptionsChange(options);
            }
        };

        // ----- Quality slider -----
        qualitySlider->onValueChanged = [this](float value) {
            int intVal = static_cast<int>(value);
            qualityValueLabel->SetText(std::to_string(intVal) + "%");

            switch (currentFormat) {
                case UCImageSaveFormat::JPEG: options.jpeg.quality = intVal; break;
                case UCImageSaveFormat::WEBP: options.webp.quality = intVal; break;
                case UCImageSaveFormat::AVIF: options.avif.quality = intVal; break;
                case UCImageSaveFormat::HEIF: options.heif.quality = intVal; break;
                case UCImageSaveFormat::PNG: options.png.compressionLevel = intVal; break;
                case UCImageSaveFormat::JPEG2000: options.jpeg2000.quality = intVal; break;
                default: break;
            }
            UpdateFileSizeEstimate();
            if (onOptionsChange) onOptionsChange(options);
        };

        // ----- Width/Height inputs with aspect ratio lock -----
        bool widthInputCallbackRinning = false;
        widthInput->onTextChanged = [this, &widthInputCallbackRinning](const std::string& text) {
            if (widthInputCallbackRinning) return;
            widthInputCallbackRinning = true;
            try {
                int w = std::stoi(text);
                options.targetWidth = w;
                if (options.maintainAspectRatio && sourceWidth > 0 && sourceHeight > 0) {
                    int h = static_cast<int>(w * static_cast<float>(sourceHeight) / sourceWidth);
                    heightInput->SetText(std::to_string(h));
                    options.targetHeight = h;
                }
                UpdateFileSizeEstimate();
            } catch (...) {}
            widthInputCallbackRinning = false;
        };

        bool heightInputCallbackRinning = false;
        heightInput->onTextChanged = [this, &heightInputCallbackRinning](const std::string& text) {
            if (heightInputCallbackRinning) return;
            heightInputCallbackRinning = true;
            try {
                int h = std::stoi(text);
                options.targetHeight = h;
                if (options.maintainAspectRatio && sourceWidth > 0 && sourceHeight > 0) {
                    int w = static_cast<int>(h * static_cast<float>(sourceWidth) / sourceHeight);
                    widthInput->SetText(std::to_string(w));
                    options.targetWidth = w;
                }
                UpdateFileSizeEstimate();
            } catch (...) {}
            heightInputCallbackRinning = false;
        };

        aspectRatioCheckbox->onStateChanged = [this](CheckedState, CheckedState newState) {
            options.maintainAspectRatio = (newState == CheckedState::Checked);
        };

        // ----- Transparency -----
        transparencyCheckbox->onStateChanged = [this](CheckedState, CheckedState newState) {
            bool preserve = (newState == CheckedState::Checked);
            options.preserveTransparency = preserve;
            UpdateFileSizeEstimate();
            if (onOptionsChange) onOptionsChange(options);
        };

        // ----- PNG options -----
        pngInterlaceCheckbox->onStateChanged = [this](CheckedState, CheckedState newState) {
            options.png.interlace = (newState == CheckedState::Checked);
        };

        // ----- JPEG options -----
        jpegProgressiveCheckbox->onStateChanged = [this](CheckedState, CheckedState newState) {
            options.jpeg.progressive = (newState == CheckedState::Checked);
        };
        jpegOptimizeHuffmanCheckbox->onStateChanged = [this](CheckedState, CheckedState newState) {
            options.jpeg.optimizeHuffman = (newState == CheckedState::Checked);
        };
        // subsampling is a bool in JpegExportOptions
        jpegSubsamplingCheckbox->onStateChanged = [this](CheckedState, CheckedState newState) {
            options.jpeg.subsampling = (newState == CheckedState::Checked);
            UpdateFileSizeEstimate();
        };

        // ----- WebP options -----
        webpLosslessCheckbox->onStateChanged = [this](CheckedState, CheckedState newState) {
            options.webp.lossless = (newState == CheckedState::Checked);
            qualityLabel->SetText(options.webp.lossless ? "Compress:" : "Quality:");
            UpdateFileSizeEstimate();
        };
        webpEffortSlider->onValueChanged = [this](float value) {
            options.webp.effort = static_cast<int>(value);
        };
        webpAlphaQualitySlider->onValueChanged = [this](float value) {
            options.webp.alphaQuality = static_cast<int>(value);
        };

        // ----- AVIF options -----
        avifLosslessCheckbox->onStateChanged = [this](CheckedState, CheckedState newState) {
            options.avif.lossless = (newState == CheckedState::Checked);
            UpdateFileSizeEstimate();
        };
        avifSpeedSlider->onValueChanged = [this](float value) {
            options.avif.speed = static_cast<int>(value);
        };
        // Note: colorDepth is handled via colorDepthDropdown, no bitDepth/hdr in struct

        // ----- GIF options -----
        // Note: maxColors removed (not in GifExportOptions struct)
        gifDitheringCheckbox->onStateChanged = [this](CheckedState, CheckedState newState) {
            options.gif.dithering = (newState == CheckedState::Checked);
        };
        gifInterlaceCheckbox->onStateChanged = [this](CheckedState, CheckedState newState) {
            options.gif.interlace = (newState == CheckedState::Checked);
        };

        // ----- TIFF options -----
        tiffCompressionDropdown->onSelectionChanged = [this](int index, const DropdownItem&) {
            options.tiff.compression = static_cast<UCImageSave::TiffCompression>(index);
            UpdateFileSizeEstimate();
        };
        tiffMultiPageCheckbox->onStateChanged = [this](CheckedState, CheckedState newState) {
            options.tiff.multiPage = (newState == CheckedState::Checked);
        };

        // ----- QOI options -----
        qoiAlphaCheckbox->onStateChanged = [this](CheckedState, CheckedState newState) {
            options.qoi.hasAlpha = (newState == CheckedState::Checked);
            UpdateFileSizeEstimate();
        };
        qoiLinearColorspaceCheckbox->onStateChanged = [this](CheckedState, CheckedState newState) {
            options.qoi.linearColorspace = (newState == CheckedState::Checked);
        };

        // ----- Metadata options -----
        preserveMetadataCheckbox->onStateChanged = [this](CheckedState, CheckedState newState) {
            options.preserveMetadata = (newState == CheckedState::Checked);
        };

        // ----- Phase 1 format option callbacks -----
        tgaRleCheckbox->onStateChanged = [this](CheckedState, CheckedState newState) {
            options.tga.rleCompression = (newState == CheckedState::Checked);
            UpdateFileSizeEstimate();
        };
        pcxRleCheckbox->onStateChanged = [this](CheckedState, CheckedState newState) {
            options.pcx.rleCompression = (newState == CheckedState::Checked);
        };
        pnmBinaryCheckbox->onStateChanged = [this](CheckedState, CheckedState newState) {
            options.pnm.binary = (newState == CheckedState::Checked);
            UpdateFileSizeEstimate();
        };
        exrCompressionDropdown->onSelectionChanged = [this](int index, const DropdownItem&) {
            options.exr.compression = static_cast<UCImageSave::ExrExportOptions::Compression>(index);
            UpdateFileSizeEstimate();
        };
        dpxBitDepthDropdown->onSelectionChanged = [this](int index, const DropdownItem&) {
            static const int depths[] = {8, 10, 12, 16};
            if (index >= 0 && index < 4) options.dpx.bitDepth = depths[index];
        };
        cinBitDepthDropdown->onSelectionChanged = [this](int index, const DropdownItem&) {
            static const int depths[] = {8, 10, 12, 16};
            if (index >= 0 && index < 4) options.cin.bitDepth = depths[index];
        };
        psdCompressedCheckbox->onStateChanged = [this](CheckedState, CheckedState newState) {
            options.psd.compressed = (newState == CheckedState::Checked);
            UpdateFileSizeEstimate();
        };
        sgiRleCheckbox->onStateChanged = [this](CheckedState, CheckedState newState) {
            options.sgi.rleCompression = (newState == CheckedState::Checked);
            UpdateFileSizeEstimate();
        };

        // ----- Footer buttons -----
        cancelButton->onClick = [this]() {
            if (onCancel) onCancel();
            PerformClose();
        };

        saveButton->onClick = [this]() {
            ApplyOptionsFromUI();
            if (onSave) {
                onSave(options);
            } else {
                std::string outFilename = fmt::format("{}.{}", GetFileName(), ImageFormatInfo::GetExtension(options.format));
                ExportVImage(sourceImage, outFilename, options);
            }
            PerformClose();
        };
    }

// ============================================================================
// FORMAT OPTIONS MANAGEMENT
// ============================================================================

    void UltraCanvasImageExportDialog::HideAllFormatOptions() {
        if (pngOptionsContainer) pngOptionsContainer->SetVisible(false);
        if (jpegOptionsContainer) jpegOptionsContainer->SetVisible(false);
        if (webpOptionsContainer) webpOptionsContainer->SetVisible(false);
        if (avifOptionsContainer) avifOptionsContainer->SetVisible(false);
        if (gifOptionsContainer) gifOptionsContainer->SetVisible(false);
        if (tiffOptionsContainer) tiffOptionsContainer->SetVisible(false);
        if (qoiOptionsContainer) qoiOptionsContainer->SetVisible(false);
        if (tgaOptionsContainer) tgaOptionsContainer->SetVisible(false);
        if (pcxOptionsContainer) pcxOptionsContainer->SetVisible(false);
        if (pnmOptionsContainer) pnmOptionsContainer->SetVisible(false);
        if (exrOptionsContainer) exrOptionsContainer->SetVisible(false);
        if (dpxOptionsContainer) dpxOptionsContainer->SetVisible(false);
        if (cinOptionsContainer) cinOptionsContainer->SetVisible(false);
        if (psdOptionsContainer) psdOptionsContainer->SetVisible(false);
        if (sgiOptionsContainer) sgiOptionsContainer->SetVisible(false);
    }

    void UltraCanvasImageExportDialog::UpdateFormatOptions() {
        HideAllFormatOptions();

        auto info = ImageFormatInfo::GetInfo(currentFormat);

        // Update transparency visibility
        bool supportsAlpha = info.supportsTransparency;
        if (transparencyLabel) transparencyLabel->SetVisible(supportsAlpha);
        if (transparencyCheckbox) transparencyCheckbox->SetVisible(supportsAlpha);
        options.preserveTransparency = supportsAlpha && transparencyCheckbox->IsChecked();

        // Update quality slider visibility and range. The slider is shown
        // only for formats whose saver exposes a quality/compression-level
        // knob via this control. PNG hijacks it for compressionLevel; WebP
        // for either quality or compression depending on lossless mode.
        bool hasQuality = false;
        switch (currentFormat) {
            case UCImageSaveFormat::PNG:
            case UCImageSaveFormat::JPEG:
            case UCImageSaveFormat::WEBP:
            case UCImageSaveFormat::AVIF:
            case UCImageSaveFormat::HEIF:
            case UCImageSaveFormat::JPEG2000:
            case UCImageSaveFormat::JXL:
            case UCImageSaveFormat::TIFF:
                hasQuality = true;
                break;
            default:
                hasQuality = false;
                break;
        }
        if (qualityLabel) qualityLabel->SetVisible(hasQuality);
        if (qualitySlider) qualitySlider->SetVisible(hasQuality);
        if (qualityValueLabel) qualityValueLabel->SetVisible(hasQuality);

        UpdateQualityRange();
        if (empty(info.supportedDepths)) {
            colorDepthDropdown->SetVisible(false);
            colorDepthLabel->SetVisible(false);
        } else {
            colorDepthLabel->SetVisible(true);
            colorDepthDropdown->SetVisible(true);
            UpdateColorDepthOptions();
        }

        // Show format-specific options
        switch (currentFormat) {
            case UCImageSaveFormat::PNG:
                if (pngOptionsContainer) pngOptionsContainer->SetVisible(true);
                break;
            case UCImageSaveFormat::JPEG:
                if (jpegOptionsContainer) jpegOptionsContainer->SetVisible(true);
                break;
            case UCImageSaveFormat::WEBP:
                if (webpOptionsContainer) webpOptionsContainer->SetVisible(true);
                break;
            case UCImageSaveFormat::AVIF:
                if (avifOptionsContainer) avifOptionsContainer->SetVisible(true);
                break;
            case UCImageSaveFormat::GIF:
                if (gifOptionsContainer) gifOptionsContainer->SetVisible(true);
                break;
            case UCImageSaveFormat::TIFF:
                if (tiffOptionsContainer) tiffOptionsContainer->SetVisible(true);
                break;
            case UCImageSaveFormat::QOI:
                if (qoiOptionsContainer) qoiOptionsContainer->SetVisible(true);
                break;
            case UCImageSaveFormat::TGA:
                if (tgaOptionsContainer) tgaOptionsContainer->SetVisible(true);
                break;
            case UCImageSaveFormat::PCX:
                if (pcxOptionsContainer) pcxOptionsContainer->SetVisible(true);
                break;
            case UCImageSaveFormat::PPM:
            case UCImageSaveFormat::PGM:
            case UCImageSaveFormat::PBM:
            case UCImageSaveFormat::PFM:
                if (pnmOptionsContainer) pnmOptionsContainer->SetVisible(true);
                break;
            case UCImageSaveFormat::EXR:
                if (exrOptionsContainer) exrOptionsContainer->SetVisible(true);
                break;
            case UCImageSaveFormat::DPX:
                if (dpxOptionsContainer) dpxOptionsContainer->SetVisible(true);
                break;
            case UCImageSaveFormat::CIN:
                if (cinOptionsContainer) cinOptionsContainer->SetVisible(true);
                break;
            case UCImageSaveFormat::PSD:
                if (psdOptionsContainer) psdOptionsContainer->SetVisible(true);
                break;
            case UCImageSaveFormat::SGI:
                if (sgiOptionsContainer) sgiOptionsContainer->SetVisible(true);
                break;
            // HDR, FITS, FARBFELD, BMP, ICO have no per-format knobs
            default:
                break;
        }
    }

    void UltraCanvasImageExportDialog::UpdateQualityRange() {
        if (!qualitySlider || !qualityLabel) return;

        switch (currentFormat) {
            case UCImageSaveFormat::PNG:
                qualityLabel->SetText("Compress:");
                qualitySlider->SetRange(0, 9);
                qualitySlider->SetValue(static_cast<float>(options.png.compressionLevel));
                qualityValueLabel->SetText(std::to_string(options.png.compressionLevel));
                break;
            case UCImageSaveFormat::JPEG:
                qualityLabel->SetText("Quality:");
                qualitySlider->SetRange(1, 100);
                qualitySlider->SetValue(static_cast<float>(options.jpeg.quality));
                qualityValueLabel->SetText(std::to_string(options.jpeg.quality) + "%");
                break;
            case UCImageSaveFormat::WEBP:
                qualityLabel->SetText(options.webp.lossless ? "Compress:" : "Quality:");
                qualitySlider->SetRange(0, 100);
                qualitySlider->SetValue(static_cast<float>(options.webp.quality));
                qualityValueLabel->SetText(std::to_string(options.webp.quality) + "%");
                break;
            case UCImageSaveFormat::AVIF:
                qualityLabel->SetText("Quality:");
                qualitySlider->SetRange(0, 100);
                qualitySlider->SetValue(static_cast<float>(options.avif.quality));
                qualityValueLabel->SetText(std::to_string(options.avif.quality) + "%");
                break;
            case UCImageSaveFormat::HEIF:
                qualityLabel->SetText("Quality:");
                qualitySlider->SetRange(0, 100);
                qualitySlider->SetValue(static_cast<float>(options.heif.quality));
                qualityValueLabel->SetText(std::to_string(options.heif.quality) + "%");
                break;
            case UCImageSaveFormat::JPEG2000:
                qualityLabel->SetText("Quality:");
                qualitySlider->SetRange(0, 100);
                qualitySlider->SetValue(static_cast<float>(options.jpeg2000.quality));
                qualityValueLabel->SetText(std::to_string(options.jpeg2000.quality) + "%");
                break;
            default:
                qualityLabel->SetText("Quality:");
                qualitySlider->SetRange(0, 100);
                qualitySlider->SetValue(85);
                qualityValueLabel->SetText("85%");
                break;
        }
    }

    void UltraCanvasImageExportDialog::UpdateColorDepthOptions() {
        if (!colorDepthDropdown) return;

        colorDepthDropdown->ClearItems();
        auto info = ImageFormatInfo::GetInfo(currentFormat);

        for (const auto& depth : info.supportedDepths) {
            std::string depthStr;
            switch (depth) {
                case UCImageSave::ColorDepth::Monochrome_1bit: depthStr = "1-bit (Monochrome)"; break;
                case UCImageSave::ColorDepth::Indexed_4bit: depthStr = "4-bit (16 colors)"; break;
                case UCImageSave::ColorDepth::Indexed_8bit: depthStr = "8-bit (256 colors)"; break;
//                case UCImageSave::ColorDepth::RGB_16bit: depthStr = "16-bit (65K colors)"; break;
                case UCImageSave::ColorDepth::RGB_8bit: depthStr = "8-bit/channel RGB"; break;
                case UCImageSave::ColorDepth::RGB_16bit: depthStr = "16-bit/channel RGB"; break;
//                case UCImageSave::ColorDepth::HDR_96bit: depthStr = "96-bit HDR"; break;
//                case UCImageSave::ColorDepth::HDR_128bit: depthStr = "128-bit HDR"; break;
            }
            colorDepthDropdown->AddItem(depthStr);
        }

        if (colorDepthDropdown->GetItemCount() > 0) {
            colorDepthDropdown->SetSelectedIndex(0);
        }
    }

// ============================================================================
// FILE SIZE ESTIMATION
// ============================================================================

    void UltraCanvasImageExportDialog::UpdateFileSizeEstimate() {
        size_t estimated = EstimateFileSize();
        if (fileSizeEstimateLabel) {
            fileSizeEstimateLabel->SetText("Estimated: ~" + FormatFileSize(estimated));
        }
    }

    size_t UltraCanvasImageExportDialog::EstimateFileSize() {
        int w = options.targetWidth > 0 ? options.targetWidth : sourceWidth;
        int h = options.targetHeight > 0 ? options.targetHeight : sourceHeight;
        if (w <= 0) w = 1920;
        if (h <= 0) h = 1080;

        size_t rawSize = static_cast<size_t>(w) * h * sourceChannels;
        float ratio = 0.5f;

        switch (currentFormat) {
            case UCImageSaveFormat::PNG:
                ratio = 0.3f + (9 - options.png.compressionLevel) * 0.05f;
                break;
            case UCImageSaveFormat::JPEG:
                ratio = 0.05f + (100 - options.jpeg.quality) * 0.003f;
                break;
            case UCImageSaveFormat::WEBP:
                ratio = options.webp.lossless ? 0.25f : (0.04f + (100 - options.webp.quality) * 0.003f);
                break;
            case UCImageSaveFormat::AVIF:
                ratio = options.avif.lossless ? 0.2f : (0.03f + (100 - options.avif.quality) * 0.002f);
                break;
            case UCImageSaveFormat::HEIF:
                ratio = options.heif.lossless ? 0.25f : (0.05f + (100 - options.heif.quality) * 0.003f);
                break;
            case UCImageSaveFormat::JXL:
                ratio = options.jxl.lossless ? 0.3f : (0.04f + (100 - options.jxl.quality) * 0.002f);
                break;
            case UCImageSaveFormat::JPEG2000:
                ratio = options.jpeg2000.lossless ? 0.4f : (0.05f + (100 - options.jpeg2000.quality) * 0.003f);
                break;
            case UCImageSaveFormat::GIF:
                ratio = 0.15f;
                break;
            case UCImageSaveFormat::BMP:
                ratio = 1.0f;
                break;
            case UCImageSaveFormat::TIFF:
                ratio = (options.tiff.compression == UCImageSave::TiffCompression::NoCompression) ? 1.0f : 0.4f;
                break;
            case UCImageSaveFormat::ICO:
                ratio = 0.6f;
                break;
            case UCImageSaveFormat::QOI:
                ratio = 0.45f;
                break;
            case UCImageSaveFormat::TGA:
                ratio = options.tga.rleCompression ? 0.6f : 1.0f;
                break;
            case UCImageSaveFormat::PCX:
                ratio = 0.55f;
                break;
            case UCImageSaveFormat::PPM:
            case UCImageSaveFormat::PGM:
            case UCImageSaveFormat::PBM:
                ratio = options.pnm.binary ? 1.0f : 3.0f;  // ASCII is ~3× bigger
                break;
            case UCImageSaveFormat::PFM:
                ratio = 4.0f;  // 32-bit float per channel
                break;
            case UCImageSaveFormat::HDR:
                ratio = 1.3f;  // RGBE: 4 bytes per pixel float-encoded
                break;
            case UCImageSaveFormat::EXR:
                ratio = (options.exr.compression == UCImageSave::ExrExportOptions::Compression::NoCompression) ? 8.0f : 2.5f;
                break;
            case UCImageSaveFormat::DPX:
            case UCImageSaveFormat::CIN:
                ratio = 1.4f;  // 10-bit packed, uncompressed
                break;
            case UCImageSaveFormat::FITS:
                ratio = 1.0f;
                break;
            case UCImageSaveFormat::PSD:
                ratio = options.psd.compressed ? 0.7f : 1.0f;
                break;
            case UCImageSaveFormat::SGI:
                ratio = options.sgi.rleCompression ? 0.6f : 1.0f;
                break;
            case UCImageSaveFormat::FARBFELD:
                ratio = 2.0f;  // 16-bit per channel RGBA uncompressed
                break;
            default:
                ratio = 0.5f;
                break;
        }

        return static_cast<size_t>(rawSize * ratio);
    }

    std::string UltraCanvasImageExportDialog::FormatFileSize(size_t bytes) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1);

        if (bytes >= 1024 * 1024 * 1024) {
            oss << (bytes / (1024.0 * 1024.0 * 1024.0)) << " GB";
        } else if (bytes >= 1024 * 1024) {
            oss << (bytes / (1024.0 * 1024.0)) << " MB";
        } else if (bytes >= 1024) {
            oss << (bytes / 1024.0) << " KB";
        } else {
            oss << bytes << " B";
        }

        return oss.str();
    }

// ============================================================================
// OPTIONS APPLICATION
// ============================================================================

    void UltraCanvasImageExportDialog::ApplyOptionsFromUI() {
        options.format = currentFormat;

        if (fileNameInput) {
            // Store filename - note: ImageExportOptions doesn't have fileName field
            // This would need to be handled by the caller via GetFileName()
        }

        try {
            if (widthInput) options.targetWidth = std::stoi(widthInput->GetText());
            if (heightInput) options.targetHeight = std::stoi(heightInput->GetText());
        } catch (...) {}

        options.maintainAspectRatio = aspectRatioCheckbox && aspectRatioCheckbox->IsChecked();
        options.preserveMetadata = preserveMetadataCheckbox && preserveMetadataCheckbox->IsChecked();
    }

// ============================================================================
// PUBLIC API
// ============================================================================

    void UltraCanvasImageExportDialog::SetSourceImage(vips::VImage & vimg) {
        sourceImage = vimg;
        sourceWidth = vimg.width();
        sourceHeight = vimg.height();
        sourceChannels = vimg.bands();

        options.targetWidth = sourceWidth;
        options.targetHeight = sourceHeight;

        if (widthInput) widthInput->SetText(std::to_string(sourceWidth));
        if (heightInput) heightInput->SetText(std::to_string(sourceHeight));

        UpdateFileSizeEstimate();
    }

    void UltraCanvasImageExportDialog::SetOptions(const UCImageSave::ImageExportOptions& opts) {
        options = opts;
        currentFormat = opts.format;

        for (size_t i = 0; i < availableFormats.size(); ++i) {
            if (availableFormats[i].format == currentFormat) {
                if (formatDropdown) formatDropdown->SetSelectedIndex(static_cast<int>(i));
                break;
            }
        }

        UpdateFormatOptions();
        UpdateFileSizeEstimate();
    }

    UCImageSave::ImageExportOptions UltraCanvasImageExportDialog::GetOptions() const {
        return options;
    }

    void UltraCanvasImageExportDialog::SetFormat(UCImageSaveFormat format) {
        currentFormat = format;
        options.format = format;

        for (size_t i = 0; i < availableFormats.size(); ++i) {
            if (availableFormats[i].format == format) {
                if (formatDropdown) formatDropdown->SetSelectedIndex(static_cast<int>(i));
                break;
            }
        }

        UpdateFormatOptions();
        UpdateFileSizeEstimate();
    }

    UCImageSaveFormat UltraCanvasImageExportDialog::GetFormat() const {
        return currentFormat;
    }

    void UltraCanvasImageExportDialog::SetFileName(const std::string& name) {
        if (fileNameInput) fileNameInput->SetText(name);
    }

    std::string UltraCanvasImageExportDialog::GetFileName() const {
        return fileNameInput ? fileNameInput->GetText() : "";
    }

    void UltraCanvasImageExportDialog::SetTargetSize(int width, int height) {
        options.targetWidth = width;
        options.targetHeight = height;
        if (widthInput) widthInput->SetText(std::to_string(width));
        if (heightInput) heightInput->SetText(std::to_string(height));
        UpdateFileSizeEstimate();
    }

    void UltraCanvasImageExportDialog::SetStyle(const ImageExportDialogStyle& dialogStyle) {
        style = dialogStyle;
    }

} // namespace UltraCanvas