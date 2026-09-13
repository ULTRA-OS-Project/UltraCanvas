// dialogs/UltraCanvasImageExportDialog.cpp
// The bitmap save dialog: format list, the knobs each format exposes, the
// size estimate, and the metadata the image carries.
// Version: 3.0.0
// Last Modified: 2026-09-13
// Author: UltraCanvas Framework
//
// Every captioned row is a row of one two-column grid (see the header), so
// the captions share a column and the controls line up under each other
// whatever language the captions are in. Nothing is placed by coordinate.

#include "UltraCanvasContainer.h"
#include "UltraCanvasFormLayout.h"
#include "UltraCanvasSeparator.h"
#include "UltraCanvasSpacer.h"
#include "CSSLayout/CSSLayout.h"
#include "UltraCanvasImageExportDialog.h"
#include "UltraCanvasMetadataDialog.h"
#include "PixelFX/PixelFX.h"
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

        config_.width = 560;
        config_.height = 560;
        // A translated caption or a long file name has to be able to buy
        // itself room, so the dialog resizes; the minimum keeps the two
        // columns and the buttons on their feet.
        config_.minWidth = 460;
        config_.minHeight = 420;
        config_.resizable = true;
        config_.deleteOnClose = true;
        config_.title = "Save image";

        SetPadding(static_cast<int>(style.padding));
        SetBackgroundColor(style.backgroundColor);

        BuildLayout();
        WireCallbacks();
    }

    UltraCanvasImageExportDialog::UltraCanvasImageExportDialog(vips::VImage& img)
            : UltraCanvasImageExportDialog() {
        SetSourceImage(img);
    }

// ============================================================================
// FORM BUILDING
//
// One grid, two columns: captions in an `auto` column, controls in a `1fr`
// one. Rows are auto-placed in child order, so adding a row is adding its two
// children — and a hidden row (display:none) drops out of the grid entirely.
// ============================================================================

    std::shared_ptr<UltraCanvasLabel> UltraCanvasImageExportDialog::MakeFieldLabel(
            const std::string& id, const std::string& text) {
        // No width is given: the label reports its own text width to the
        // layout engine, and the column is as wide as the widest of them.
        auto label = CreateFormCaption(id, text);
        label->SetFontSize(style.labelFontSize);
        label->SetTextColor(style.labelColor);
        return label;
    }

    void UltraCanvasImageExportDialog::AddFieldRow(const std::shared_ptr<UltraCanvasLabel>& label,
                                                   const std::shared_ptr<UltraCanvasUIElement>& control) {
        if (!formGrid || !label || !control) return;
        AddFormRow(formGrid, label, control);
        if (currentFormatRows) {
            currentFormatRows->push_back(label);
            currentFormatRows->push_back(control);
        }
    }

    void UltraCanvasImageExportDialog::AddFieldRow(const std::string& id, const std::string& labelText,
                                                   const std::shared_ptr<UltraCanvasUIElement>& control) {
        AddFieldRow(MakeFieldLabel(id + "Label", labelText), control);
    }

    void UltraCanvasImageExportDialog::AddWideRow(const std::shared_ptr<UltraCanvasUIElement>& element) {
        if (!formGrid || !element) return;
        AddFormWideRow(formGrid, element);
        if (currentFormatRows) currentFormatRows->push_back(element);
    }

    std::shared_ptr<UltraCanvasLabel> UltraCanvasImageExportDialog::AddSectionHeading(
            const std::string& id, const std::string& text,
            std::shared_ptr<UltraCanvasUIElement>* outRule) {
        auto rule = std::make_shared<UltraCanvasSeparator>(false, 1, 0, style.borderColor);
        rule->SetMargin(style.spacing * 0.5f, 0, 0, 0);
        AddWideRow(rule);
        if (outRule) *outRule = rule;

        auto heading = std::make_shared<UltraCanvasLabel>(id, -1, -1, -1, -1, text);
        heading->SetFontSize(style.labelFontSize);
        heading->SetFontWeight(FontWeight::Bold);
        heading->SetTextColor(style.headingColor);
        AddWideRow(heading);
        return heading;
    }

    void UltraCanvasImageExportDialog::BeginFormatRows(UCImageSaveFormat format) {
        currentFormatRows = &formatRows[format];
    }

    void UltraCanvasImageExportDialog::EndFormatRows() {
        currentFormatRows = nullptr;
    }

    std::shared_ptr<UltraCanvasContainer> UltraCanvasImageExportDialog::MakeCellRow(
            const std::string& id, float gap) {
        return CreateFormCellRow(id, gap, static_cast<int>(style.controlHeight));
    }

    void UltraCanvasImageExportDialog::SizeButtonToLabel(const std::shared_ptr<UltraCanvasButton>& button) {
        if (!button) return;
        // The same trick the modal dialog uses: the button hugs its own text
        // (so a longer word in another language still fits) but never falls
        // below a comfortable minimum.
        button->size.width = CSSLayout::Dimension::Auto();
        CSSLayout::BoxConstraints limits = button->boxConstraints.value_or(CSSLayout::BoxConstraints{});
        limits.minWidth = CSSLayout::Dimension::Px(style.minButtonWidth);
        button->boxConstraints = limits;
        button->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    }

// ============================================================================
// LAYOUT
// ============================================================================

    void UltraCanvasImageExportDialog::BuildLayout() {
        this->layout.SetFlexColumn();
        this->layout.SetFlexGap(static_cast<int>(style.spacing));

        formGrid = CreateFormGrid("ExportForm", style.rowGap, style.columnGap);
        AddChild(formGrid);

        CreateFileSection();
        CreateImageSection();
        CreateFormatOptionsSection();
        CreateMetadataSection();

        // The form sits at the top and the footer at the bottom; the slack in
        // between belongs to neither.
        AddStretchSpacer(1);

        CreateFooterSection();

        UpdateFormatOptions();
        UpdateMetadataControls();
        UpdateFileSizeEstimate();
    }

    void UltraCanvasImageExportDialog::CreateFileSection() {
        fileNameLabel = MakeFieldLabel("FileNameLabel", "Name:");
        fileNameInput = std::make_shared<UltraCanvasTextInput>("FileNameInput", 0, 0, 200,
                                                               static_cast<int>(style.controlHeight));
        fileNameInput->SetPlaceholder("Enter file name...");
        AddFieldRow(fileNameLabel, fileNameInput);

        formatLabel = MakeFieldLabel("FormatLabel", "Format:");
        formatDropdown = std::make_shared<UltraCanvasDropdown>("FormatDropdown", 0, 0, 200,
                                                               static_cast<int>(style.controlHeight));

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
        AddFieldRow(formatLabel, formatDropdown);

        // What the chosen format is good for, in one grey line under it.
        formatDescriptionLabel = std::make_shared<UltraCanvasLabel>("FormatDescription", -1, -1, -1, -1, "");
        formatDescriptionLabel->SetFontSize(style.valueFontSize);
        formatDescriptionLabel->SetTextColor(style.labelColor);
        formatDescriptionLabel->SetWrap(TextWrap::WrapWordChar);
        AddWideRow(formatDescriptionLabel);
    }

    void UltraCanvasImageExportDialog::CreateImageSection() {
        AddSectionHeading("ImageHeading", "Image");

        // ----- Size -----
        sizeLabel = MakeFieldLabel("SizeLabel", "Size:");

        auto sizeRow = MakeCellRow("SizeRow");
        widthInput = std::make_shared<UltraCanvasTextInput>("WidthInput", 0, 0, 74,
                                                            static_cast<int>(style.controlHeight));
        widthInput->SetText("1920");
        widthInput->layoutItem.SetFlexGrow(0).SetFlexShrink(0);

        xLabel = std::make_shared<UltraCanvasLabel>("XLabel", -1, -1, -1, -1, "x");
        xLabel->SetTextColor(style.labelColor);
        xLabel->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);

        heightInput = std::make_shared<UltraCanvasTextInput>("HeightInput", 0, 0, 74,
                                                             static_cast<int>(style.controlHeight));
        heightInput->SetText("1080");
        heightInput->layoutItem.SetFlexGrow(0).SetFlexShrink(0);

        aspectRatioCheckbox = UltraCanvasCheckbox::CreateCheckbox("AspectLock", 0, 0, -1, -1,
                                                                  "Keep proportions", true);
        aspectRatioCheckbox->layoutItem.SetFlexGrow(0).SetFlexShrink(0);

        sizeRow->AddChild(widthInput);
        sizeRow->AddChild(xLabel);
        sizeRow->AddChild(heightInput);
        sizeRow->AddSpacer(10);
        sizeRow->AddChild(aspectRatioCheckbox);
        AddFieldRow(sizeLabel, sizeRow);

        // ----- Colour depth -----
        colorDepthLabel = MakeFieldLabel("ColorDepthLabel", "Colour depth:");
        colorDepthDropdown = std::make_shared<UltraCanvasDropdown>("ColorDepthDropdown", 0, 0, 200,
                                                                   static_cast<int>(style.controlHeight));
        AddFieldRow(colorDepthLabel, colorDepthDropdown);

        // ----- Transparency -----
        transparencyLabel = MakeFieldLabel("TransparencyLabel", "Transparency:");
        transparencyCheckbox = UltraCanvasCheckbox::CreateCheckbox("TransparencyCheck", 0, 0, -1, -1,
                                                                   "Preserve the alpha channel", true);
        AddFieldRow(transparencyLabel, transparencyCheckbox);

        // ----- Quality / compression -----
        qualityLabel = MakeFieldLabel("QualityLabel", "Quality:");

        auto qualityRow = MakeCellRow("QualityRow", 12.0f);
        qualitySlider = std::make_shared<UltraCanvasSlider>("QualitySlider", 0, 0, 180, 24);
        qualitySlider->SetRange(0, 100);
        // Quality / compression levels are whole numbers in every format, and
        // UpdateQualityRange() narrows the range to 0..9 for PNG — an explicit
        // step keeps the snapping integral there too.
        qualitySlider->SetStep(1.0f);
        qualitySlider->SetValue(85);
        qualitySlider->layoutItem.SetFlexGrow(1).SetFlexShrink(1);

        qualityValueLabel = std::make_shared<UltraCanvasLabel>("QualityValue", 0, 0, 44, 24, "85%");
        qualityValueLabel->SetFontSize(style.valueFontSize);
        qualityValueLabel->SetTextColor(style.textColor);
        qualityValueLabel->SetAlignment(TextAlignment::Right, VerticalAlignment::Middle);
        qualityValueLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);

        qualityRow->AddChild(qualitySlider);
        qualityRow->AddChild(qualityValueLabel);
        AddFieldRow(qualityLabel, qualityRow);

        UpdateColorDepthOptions();
    }

// ============================================================================
// FORMAT-SPECIFIC ROWS
//
// Each creator adds its rows to the same grid between BeginFormatRows() and
// EndFormatRows(); UpdateFormatOptions() then shows one set at a time.
// ============================================================================

    void UltraCanvasImageExportDialog::CreateFormatOptionsSection() {
        formatOptionsHeading = AddSectionHeading("FormatOptionsHeading", "Format options",
                                                 &formatOptionsRule);

        CreatePngOptions();
        CreateJpegOptions();
        CreateWebpOptions();
        CreateAvifOptions();
        CreateGifOptions();
        CreateTiffOptions();
        CreateQoiOptions();
        CreateTgaOptions();
        CreatePcxOptions();
        CreatePnmOptions();
        CreateExrOptions();
        CreateDpxOptions();
        CreateCinOptions();
        CreatePsdOptions();
        CreateSgiOptions();
    }

    // PNG - matches PngExportOptions: compressionLevel, interlace, preserveTransparency, colorDepth
    void UltraCanvasImageExportDialog::CreatePngOptions() {
        BeginFormatRows(UCImageSaveFormat::PNG);
        pngInterlaceCheckbox = UltraCanvasCheckbox::CreateCheckbox("PngInterlace", 0, 0, -1, -1,
                                                                   "Interlacing (progressive display)", false);
        AddWideRow(pngInterlaceCheckbox);
        EndFormatRows();
    }

    // JPEG - matches JpegExportOptions: quality, progressive, subsampling (bool!), optimizeHuffman
    void UltraCanvasImageExportDialog::CreateJpegOptions() {
        BeginFormatRows(UCImageSaveFormat::JPEG);
        jpegProgressiveCheckbox = UltraCanvasCheckbox::CreateCheckbox("JpegProgressive", 0, 0, -1, -1,
                                                                      "Progressive encoding", false);
        jpegOptimizeHuffmanCheckbox = UltraCanvasCheckbox::CreateCheckbox("JpegOptHuffman", 0, 0, -1, -1,
                                                                          "Optimize Huffman tables", true);
        // subsampling is a bool in JpegExportOptions, not ChromaSubsampling enum
        jpegSubsamplingCheckbox = UltraCanvasCheckbox::CreateCheckbox("JpegSubsampling", 0, 0, -1, -1,
                                                                      "Chroma subsampling", false);
        AddWideRow(jpegProgressiveCheckbox);
        AddWideRow(jpegOptimizeHuffmanCheckbox);
        AddWideRow(jpegSubsamplingCheckbox);
        EndFormatRows();
    }

    // WebP - matches WebpExportOptions: quality, lossless, effort, targetSize, preserveTransparency, alphaQuality
    void UltraCanvasImageExportDialog::CreateWebpOptions() {
        BeginFormatRows(UCImageSaveFormat::WEBP);
        webpLosslessCheckbox = UltraCanvasCheckbox::CreateCheckbox("WebpLossless", 0, 0, -1, -1,
                                                                   "Lossless compression", false);
        AddWideRow(webpLosslessCheckbox);

        webpEffortSlider = std::make_shared<UltraCanvasSlider>("WebpEffort", 0, 0, 150, 24);
        webpEffortSlider->SetRange(0, 6);
        webpEffortSlider->SetStep(1.0f);   // effort is a whole number 0..6
        webpEffortSlider->SetValue(4);
        AddFieldRow("WebpEffort", "Effort (0-6):", webpEffortSlider);

        webpAlphaQualitySlider = std::make_shared<UltraCanvasSlider>("WebpAlphaQuality", 0, 0, 150, 24);
        webpAlphaQualitySlider->SetRange(0, 100);
        webpAlphaQualitySlider->SetStep(1.0f);
        webpAlphaQualitySlider->SetValue(100);
        AddFieldRow("WebpAlphaQuality", "Alpha quality:", webpAlphaQualitySlider);
        EndFormatRows();
    }

    // AVIF - matches AvifExportOptions: quality, lossless, speed, preserveTransparency, colorDepth
    // Note: bitDepth and hdr are commented out in UltraCanvasImage.h; colorDepth
    // is handled by the common depth dropdown.
    void UltraCanvasImageExportDialog::CreateAvifOptions() {
        BeginFormatRows(UCImageSaveFormat::AVIF);
        avifLosslessCheckbox = UltraCanvasCheckbox::CreateCheckbox("AvifLossless", 0, 0, -1, -1,
                                                                   "Lossless compression", false);
        AddWideRow(avifLosslessCheckbox);

        avifSpeedSlider = std::make_shared<UltraCanvasSlider>("AvifSpeed", 0, 0, 150, 24);
        avifSpeedSlider->SetRange(0, 10);
        avifSpeedSlider->SetStep(1.0f);    // speed is a whole number 0..10
        avifSpeedSlider->SetValue(6);
        AddFieldRow("AvifSpeed", "Speed (0-10):", avifSpeedSlider);
        EndFormatRows();
    }

    // GIF - matches GifExportOptions: colorDepth, interlace, dithering
    void UltraCanvasImageExportDialog::CreateGifOptions() {
        BeginFormatRows(UCImageSaveFormat::GIF);
        gifDitheringCheckbox = UltraCanvasCheckbox::CreateCheckbox("GifDithering", 0, 0, -1, -1,
                                                                   "Enable dithering", true);
        gifInterlaceCheckbox = UltraCanvasCheckbox::CreateCheckbox("GifInterlace", 0, 0, -1, -1,
                                                                   "Interlaced", false);
        AddWideRow(gifDitheringCheckbox);
        AddWideRow(gifInterlaceCheckbox);
        EndFormatRows();
    }

    // TIFF - matches TiffExportOptions: compression, colorDepth, multiPage
    void UltraCanvasImageExportDialog::CreateTiffOptions() {
        BeginFormatRows(UCImageSaveFormat::TIFF);
        tiffCompressionDropdown = std::make_shared<UltraCanvasDropdown>("TiffCompression", 0, 0, 150,
                                                                        static_cast<int>(style.controlHeight));
        // Match TiffCompression enum order: NoCompression, JPEGCompression, DeflateCompression,
        // PackBitsCompression, LZWCompression, ZSTDCompression, WEBPCompression
        for (const char* name : { "None", "JPEG", "Deflate/ZIP", "PackBits", "LZW", "ZSTD", "WebP" }) {
            tiffCompressionDropdown->AddItem(name);
        }
        tiffCompressionDropdown->SetSelectedIndex(4);  // Default to LZW
        AddFieldRow("TiffCompression", "Compression:", tiffCompressionDropdown);

        tiffMultiPageCheckbox = UltraCanvasCheckbox::CreateCheckbox("TiffMultiPage", 0, 0, -1, -1,
                                                                    "Multi-page TIFF", false);
        AddWideRow(tiffMultiPageCheckbox);
        EndFormatRows();
    }

    // QOI - matches QoiExportOptions: hasAlpha, linearColorspace
    void UltraCanvasImageExportDialog::CreateQoiOptions() {
        BeginFormatRows(UCImageSaveFormat::QOI);
        qoiAlphaCheckbox = UltraCanvasCheckbox::CreateCheckbox("QoiAlpha", 0, 0, -1, -1,
                                                               "Include alpha channel", true);
        // linearColorspace is a bool, use checkbox instead of dropdown
        qoiLinearColorspaceCheckbox = UltraCanvasCheckbox::CreateCheckbox("QoiLinear", 0, 0, -1, -1,
                                                                          "Linear colorspace (default: sRGB)", false);
        AddWideRow(qoiAlphaCheckbox);
        AddWideRow(qoiLinearColorspaceCheckbox);

        qoiInfoLabel = std::make_shared<UltraCanvasLabel>("QoiInfo", -1, -1, -1, -1,
                                                          "Fast lossless compression - encodes 20-50x faster than PNG.");
        qoiInfoLabel->SetFontSize(style.valueFontSize);
        qoiInfoLabel->SetTextColor(style.labelColor);
        qoiInfoLabel->SetWrap(TextWrap::WrapWordChar);
        AddWideRow(qoiInfoLabel);
        EndFormatRows();
    }

    void UltraCanvasImageExportDialog::CreateTgaOptions() {
        BeginFormatRows(UCImageSaveFormat::TGA);
        tgaRleCheckbox = UltraCanvasCheckbox::CreateCheckbox("TgaRle", 0, 0, -1, -1, "RLE compression", true);
        AddWideRow(tgaRleCheckbox);
        EndFormatRows();
    }

    void UltraCanvasImageExportDialog::CreatePcxOptions() {
        BeginFormatRows(UCImageSaveFormat::PCX);
        pcxRleCheckbox = UltraCanvasCheckbox::CreateCheckbox("PcxRle", 0, 0, -1, -1, "RLE compression", true);
        AddWideRow(pcxRleCheckbox);
        EndFormatRows();
    }

    void UltraCanvasImageExportDialog::CreatePnmOptions() {
        // PPM / PGM / PBM / PFM all land on this row set.
        BeginFormatRows(UCImageSaveFormat::PPM);
        pnmBinaryCheckbox = UltraCanvasCheckbox::CreateCheckbox("PnmBinary", 0, 0, -1, -1,
                                                                "Binary encoding (uncheck for ASCII)", true);
        AddWideRow(pnmBinaryCheckbox);
        EndFormatRows();
    }

    void UltraCanvasImageExportDialog::CreateExrOptions() {
        BeginFormatRows(UCImageSaveFormat::EXR);
        exrCompressionDropdown = std::make_shared<UltraCanvasDropdown>("ExrCompression", 0, 0, 150,
                                                                       static_cast<int>(style.controlHeight));
        for (const char* name : { "None", "RLE", "ZIP", "PIZ", "PXR24", "B44" }) {
            exrCompressionDropdown->AddItem(name);
        }
        exrCompressionDropdown->SetSelectedIndex(2);  // ZIP default
        AddFieldRow("ExrCompression", "Compression:", exrCompressionDropdown);
        EndFormatRows();
    }

    void UltraCanvasImageExportDialog::CreateDpxOptions() {
        BeginFormatRows(UCImageSaveFormat::DPX);
        dpxBitDepthDropdown = std::make_shared<UltraCanvasDropdown>("DpxBitDepth", 0, 0, 110,
                                                                    static_cast<int>(style.controlHeight));
        for (const char* name : { "8", "10", "12", "16" }) dpxBitDepthDropdown->AddItem(name);
        dpxBitDepthDropdown->SetSelectedIndex(1);  // 10-bit default
        AddFieldRow("DpxBitDepth", "Bit depth:", dpxBitDepthDropdown);
        EndFormatRows();
    }

    void UltraCanvasImageExportDialog::CreateCinOptions() {
        BeginFormatRows(UCImageSaveFormat::CIN);
        cinBitDepthDropdown = std::make_shared<UltraCanvasDropdown>("CinBitDepth", 0, 0, 110,
                                                                    static_cast<int>(style.controlHeight));
        for (const char* name : { "8", "10", "12", "16" }) cinBitDepthDropdown->AddItem(name);
        cinBitDepthDropdown->SetSelectedIndex(1);
        AddFieldRow("CinBitDepth", "Bit depth:", cinBitDepthDropdown);
        EndFormatRows();
    }

    void UltraCanvasImageExportDialog::CreatePsdOptions() {
        BeginFormatRows(UCImageSaveFormat::PSD);
        psdCompressedCheckbox = UltraCanvasCheckbox::CreateCheckbox("PsdCompressed", 0, 0, -1, -1,
                                                                    "RLE compression", true);
        AddWideRow(psdCompressedCheckbox);
        EndFormatRows();
    }

    void UltraCanvasImageExportDialog::CreateSgiOptions() {
        BeginFormatRows(UCImageSaveFormat::SGI);
        sgiRleCheckbox = UltraCanvasCheckbox::CreateCheckbox("SgiRle", 0, 0, -1, -1, "RLE compression", false);
        AddWideRow(sgiRleCheckbox);
        EndFormatRows();
    }

// ============================================================================
// METADATA AND FOOTER
// ============================================================================

    void UltraCanvasImageExportDialog::CreateMetadataSection() {
        AddSectionHeading("MetadataHeading", "Metadata");

        // No caption of its own: the section heading above already says
        // "Metadata", and the row reads like the format checkboxes under
        // theirs.
        auto metadataRow = MakeCellRow("MetadataRow", 12.0f);
        preserveMetadataCheckbox = UltraCanvasCheckbox::CreateCheckbox("PreserveMetadata", 0, 0, -1, -1,
                                                                       "Preserve metadata", true);
        preserveMetadataCheckbox->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        metadataRow->AddChild(preserveMetadataCheckbox);

        // Only worth a button when there is something behind it — see
        // UpdateMetadataControls().
        showMetadataButton = std::make_shared<UltraCanvasButton>("ShowMetadata", 0, 0, 110,
                                                                 static_cast<int>(style.controlHeight));
        showMetadataButton->SetText("Show...");
        showMetadataButton->SetTooltip("List the metadata this image carries");
        showMetadataButton->onClick = [this]() { ShowMetadataPopup(); };
        SizeButtonToLabel(showMetadataButton);
        metadataRow->AddChild(showMetadataButton);

        metadataSummaryLabel = std::make_shared<UltraCanvasLabel>("MetadataSummary", -1, -1, -1, -1, "");
        metadataSummaryLabel->SetFontSize(style.valueFontSize);
        metadataSummaryLabel->SetTextColor(style.labelColor);
        metadataSummaryLabel->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        metadataRow->AddChild(metadataSummaryLabel);

        AddWideRow(metadataRow);
    }

    void UltraCanvasImageExportDialog::CreateFooterSection() {
        footerSection = std::make_shared<UltraCanvasContainer>("FooterSection", 0, 0, 0, 38);
        footerSection->layout.SetFlexRow().SetFlexGap(10).SetFlexAlignItems(CSSLayout::AlignItems::Center);
        footerSection->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        fileSizeEstimateLabel = std::make_shared<UltraCanvasLabel>("FileSizeEstimate", -1, -1, -1, -1, "");
        fileSizeEstimateLabel->SetFontSize(style.valueFontSize);
        fileSizeEstimateLabel->SetTextColor(style.labelColor);
        footerSection->AddChild(fileSizeEstimateLabel);
        footerSection->AddStretchSpacer(1);

        cancelButton = std::make_shared<UltraCanvasButton>("CancelButton", 0, 0, 96, 32);
        cancelButton->SetText("Cancel");
        SizeButtonToLabel(cancelButton);
        footerSection->AddChild(cancelButton);

        saveButton = std::make_shared<UltraCanvasButton>("SaveButton", 0, 0, 96, 32);
        saveButton->SetText("Save");
        // The one action the dialog exists for, so it reads as the default.
        saveButton->SetStyle(ButtonStyles::PrimaryStyle());
        SizeButtonToLabel(saveButton);
        footerSection->AddChild(saveButton);

        AddChild(footerSection);
    }

// ============================================================================
// METADATA
// ============================================================================

    void UltraCanvasImageExportDialog::UpdateMetadataControls() {
        size_t count = 0;
        bool present = false;
        if (sourceImage.get_image()) {
            PixelFX::PFXImage image(sourceImage);
            present = PixelFX::Header::HasMetadata(image);
            if (present) count = PixelFX::Header::ReadMetadata(image).size();
        }

        // Nothing to show, nothing to offer: the button and the summary only
        // exist when the image actually carries metadata.
        if (showMetadataButton) showMetadataButton->SetVisible(present);
        if (metadataSummaryLabel) {
            metadataSummaryLabel->SetVisible(present);
            if (present) {
                metadataSummaryLabel->SetText(std::to_string(count) +
                                              (count == 1 ? " entry" : " entries"));
            }
        }
        if (preserveMetadataCheckbox) {
            preserveMetadataCheckbox->SetTooltip(present
                    ? "Copy the image's metadata into the saved file"
                    : "This image carries no metadata to copy");
        }
    }

    void UltraCanvasImageExportDialog::ShowMetadataPopup() {
        if (!sourceImage.get_image()) return;
        PixelFX::PFXImage image(sourceImage);
        // Markdown: the popup's text area renders the per-group tables, so
        // neither dialog has to lay anything out itself.
        const std::string listing =
                PixelFX::Header::MetadataToText(image, PixelFX::Header::MetadataTextFormat::Markdown);
        ShowMetadataDialog(GetFileName(), listing, true, this);
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
        // The guard is a member: it has to outlive WireCallbacks(), and while
        // it was a local captured by reference each keystroke re-entered
        // through a dangling one.
        widthInput->onTextChanged = [this](const std::string& text) {
            if (syncingSize) return;
            syncingSize = true;
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
            syncingSize = false;
        };

        heightInput->onTextChanged = [this](const std::string& text) {
            if (syncingSize) return;
            syncingSize = true;
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
            syncingSize = false;
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

    UCImageSaveFormat UltraCanvasImageExportDialog::FormatRowsKeyFor(UCImageSaveFormat format) {
        switch (format) {
            // One row set serves the whole PNM family.
            case UCImageSaveFormat::PGM:
            case UCImageSaveFormat::PBM:
            case UCImageSaveFormat::PFM:
                return UCImageSaveFormat::PPM;
            default:
                return format;
        }
    }

    void UltraCanvasImageExportDialog::HideAllFormatOptions() {
        for (auto& entry : formatRows) {
            for (auto& element : entry.second) {
                if (element) element->SetVisible(false);
            }
        }
    }

    void UltraCanvasImageExportDialog::UpdateFormatOptions() {
        HideAllFormatOptions();

        auto info = ImageFormatInfo::GetInfo(currentFormat);

        if (formatDescriptionLabel) formatDescriptionLabel->SetText(info.description);

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

        // Show this format's own rows, and the heading only when there are
        // any: an empty "Format options" caption is worse than none.
        const auto rows = formatRows.find(FormatRowsKeyFor(currentFormat));
        const bool hasRows = rows != formatRows.end() && !rows->second.empty();
        if (hasRows) {
            for (auto& element : rows->second) {
                if (element) element->SetVisible(true);
            }
        }
        if (formatOptionsHeading) {
            formatOptionsHeading->SetText(info.name + " options");
            formatOptionsHeading->SetVisible(hasRows);
        }
        if (formatOptionsRule) formatOptionsRule->SetVisible(hasRows);
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

        // A new image may or may not carry metadata; the controls that offer
        // to show it follow.
        UpdateMetadataControls();
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