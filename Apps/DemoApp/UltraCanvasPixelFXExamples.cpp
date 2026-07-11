// Apps/DemoApp/UltraCanvasPixelFXExamples.cpp
// PixelFX module demo page. A horizontal tab bar splits the module into
// three views (same structure as the FileLoader demo):
//   * Overview  – the short intro, the architecture diagram and a Markdown
//                 table of the processing categories exposed by the playground.
//   * Details   – the full README documentation.
//   * Examples  – a live playground: pick a sample image (portrait.jpg by default)
//                 or upload a custom one, choose a processing function from the
//                 category tree in the middle, tune its options on the right
//                 and watch the processed result on the left.
// Version: 1.0.0
// Last Modified: 2026-07-11
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"

#ifdef HAS_LIBVIPS

#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasConfig.h"   // GetResourcesDir
#include "UltraCanvasUtils.h"    // NormalizePath, LoadFile
#include "PixelFX/PixelFX.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

namespace {

    // ===== EFFECT MODEL =====
    // One adjustable parameter of a processing function, rendered as a slider.
    struct EffectParam {
        std::string label;
        float minValue;
        float maxValue;
        float defValue;
        float step;
    };

    // A single PixelFX processing function: its parameters plus the call into
    // the PixelFX API. `apply` receives the source image and the current
    // slider values (same order as `params`).
    struct Effect {
        std::string id;
        std::string name;
        std::string description;
        std::vector<EffectParam> params;
        std::function<PixelFX::PFXImage(const PixelFX::PFXImage&, const std::vector<float>&)> apply;
    };

    struct EffectCategory {
        std::string id;
        std::string name;
        std::vector<Effect> effects;
    };

    // The processing functions offered by the playground, grouped the same way
    // as the PixelFX namespaces (Colour, Convolution, Resample, Morphology, …).
    const std::vector<EffectCategory>& EffectCategories() {
        using PixelFX::PFXImage;
        namespace FX = PixelFX;

        static const std::vector<EffectCategory> categories = {
            { "colour", "Colour adjustments", {
                { "brightness", "Brightness", "Multiplies every pixel by the chosen factor (Colour::Brightness).",
                  { { "Factor", 0.1f, 3.0f, 1.4f, 0.05f } },
                  [](const PFXImage& src, const std::vector<float>& p) { return FX::Colour::Brightness(src, p[0]); } },
                { "contrast", "Contrast", "Scales the distance of every pixel from the image mean (Colour::Contrast).",
                  { { "Factor", 0.1f, 3.0f, 1.5f, 0.05f } },
                  [](const PFXImage& src, const std::vector<float>& p) { return FX::Colour::Contrast(src, p[0]); } },
                { "saturation", "Saturation", "Scales chroma in LCh colour space (Colour::Saturation).",
                  { { "Factor", 0.0f, 3.0f, 1.8f, 0.05f } },
                  [](const PFXImage& src, const std::vector<float>& p) { return FX::Colour::Saturation(src, p[0]); } },
                { "gamma", "Gamma", "Applies a gamma curve to all channels (Colour::Gamma).",
                  { { "Gamma", 0.2f, 3.0f, 2.2f, 0.05f } },
                  [](const PFXImage& src, const std::vector<float>& p) { return FX::Colour::Gamma(src, p[0]); } },
                { "invert", "Invert", "Photographic negative (Colour::Invert).", {},
                  [](const PFXImage& src, const std::vector<float>&) { return FX::Colour::Invert(src); } },
                { "grayscale", "Grayscale", "Converts to single-band black & white (Colour::Grayscale).", {},
                  [](const PFXImage& src, const std::vector<float>&) { return FX::Colour::Grayscale(src); } },
                { "sepia", "Sepia", "Classic warm sepia tone matrix (Colour::Sepia).", {},
                  [](const PFXImage& src, const std::vector<float>&) { return FX::Colour::Sepia(src); } },
                { "histequal", "Histogram equalise", "Spreads the histogram for maximum global contrast (Colour::HistEqual).", {},
                  [](const PFXImage& src, const std::vector<float>&) { return FX::Colour::HistEqual(src); } },
            } },
            { "blur", "Blur & sharpen", {
                { "gaussblur", "Gaussian blur", "Smooths the image with a Gaussian kernel (Convolution::GaussianBlur).",
                  { { "Sigma", 0.5f, 20.0f, 5.0f, 0.5f } },
                  [](const PFXImage& src, const std::vector<float>& p) { return FX::Convolution::GaussianBlur(src, p[0]); } },
                { "boxblur", "Box blur", "Averages pixels inside a square window (Convolution::BoxBlur).",
                  { { "Radius", 1.0f, 25.0f, 8.0f, 1.0f } },
                  [](const PFXImage& src, const std::vector<float>& p) { return FX::Convolution::BoxBlur(src, static_cast<int>(p[0])); } },
                { "median", "Median filter", "Rank filter that removes noise while keeping edges (Morphology::Median).",
                  { { "Window size", 3.0f, 15.0f, 5.0f, 2.0f } },
                  [](const PFXImage& src, const std::vector<float>& p) {
                      int size = static_cast<int>(p[0]) | 1;   // vips needs an odd window
                      return FX::Morphology::Median(src, size); } },
                { "sharpen", "Sharpen", "Unsharp masking in LAB space (Convolution::Sharpen).",
                  { { "Sigma", 0.5f, 10.0f, 2.0f, 0.5f } },
                  [](const PFXImage& src, const std::vector<float>& p) { return FX::Convolution::Sharpen(src, p[0], 2.0, 1.0); } },
                { "unsharp", "Unsharp mask", "Boosts local contrast by subtracting a blurred copy (Convolution::UnsharpMask).",
                  { { "Sigma", 1.0f, 10.0f, 3.0f, 0.5f },
                    { "Amount", 0.1f, 3.0f, 1.0f, 0.1f } },
                  [](const PFXImage& src, const std::vector<float>& p) { return FX::Convolution::UnsharpMask(src, p[0], p[1]); } },
            } },
            { "edges", "Edge detection", {
                { "sobel", "Sobel", "Classic gradient edge detector (Convolution::Sobel).", {},
                  [](const PFXImage& src, const std::vector<float>&) { return FX::Convolution::Sobel(src); } },
                { "canny", "Canny", "Thin, precise edges after Gaussian smoothing (Convolution::Canny).",
                  { { "Sigma", 0.5f, 5.0f, 1.4f, 0.1f } },
                  [](const PFXImage& src, const std::vector<float>& p) {
                      // canny returns small float magnitudes — scale them into 0..255
                      return FX::Arithmetic::Multiply(FX::Convolution::Canny(src, p[0]), 64.0); } },
                { "laplacian", "Laplacian", "Second-derivative edge detector (Convolution::Laplacian).", {},
                  [](const PFXImage& src, const std::vector<float>&) { return FX::Convolution::Laplacian(src); } },
                { "prewitt", "Prewitt", "Gradient magnitude from Prewitt kernels (Convolution::Prewitt).", {},
                  [](const PFXImage& src, const std::vector<float>&) { return FX::Convolution::Prewitt(src); } },
                { "scharr", "Scharr", "Rotation-optimised gradient kernels (Convolution::Scharr).", {},
                  [](const PFXImage& src, const std::vector<float>&) {
                      // Scharr weights are ±10 — bring the magnitude back into range
                      return FX::Arithmetic::Multiply(FX::Convolution::Scharr(src), 0.1); } },
            } },
            { "geometry", "Geometry & resample", {
                { "rotate", "Rotate", "Rotates around the centre with bicubic resampling (Resample::Rotate).",
                  { { "Angle °", -180.0f, 180.0f, 30.0f, 1.0f } },
                  [](const PFXImage& src, const std::vector<float>& p) { return FX::Resample::Rotate(src, p[0]); } },
                { "fliph", "Flip horizontal", "Mirrors the image left ↔ right (Resample::FlipHorizontal).", {},
                  [](const PFXImage& src, const std::vector<float>&) { return FX::Resample::FlipHorizontal(src); } },
                { "flipv", "Flip vertical", "Mirrors the image top ↔ bottom (Resample::FlipVertical).", {},
                  [](const PFXImage& src, const std::vector<float>&) { return FX::Resample::FlipVertical(src); } },
                { "scale", "Scale", "Resizes with a Lanczos3 kernel (Resample::Resize).",
                  { { "Factor", 0.1f, 2.0f, 0.5f, 0.05f } },
                  [](const PFXImage& src, const std::vector<float>& p) { return FX::Resample::Resize(src, p[0]); } },
                { "pixelate", "Pixelate", "Subsamples then zooms back up for a mosaic look (Conversion::Subsample + Zoom).",
                  { { "Block size", 2.0f, 32.0f, 8.0f, 1.0f } },
                  [](const PFXImage& src, const std::vector<float>& p) {
                      int f = std::max(2, static_cast<int>(p[0]));
                      return FX::Conversion::Zoom(FX::Conversion::Subsample(src, f), f); } },
            } },
            // Greyscale morphology via rank filters: Morphology::Erode/Dilate wrap
            // vips_morph, which only operates on binary (0/255) images. A rank
            // filter taking the neighbourhood minimum / maximum is the greyscale
            // equivalent and works on photographs.
            { "morphology", "Morphology", {
                { "erode", "Erode", "Shrinks bright regions — neighbourhood minimum (Morphology::Rank).",
                  { { "Radius", 1.0f, 6.0f, 2.0f, 1.0f } },
                  [](const PFXImage& src, const std::vector<float>& p) {
                      int size = static_cast<int>(p[0]) * 2 + 1;
                      return FX::Morphology::Rank(src, size, size, 0); } },
                { "dilate", "Dilate", "Grows bright regions — neighbourhood maximum (Morphology::Rank).",
                  { { "Radius", 1.0f, 6.0f, 2.0f, 1.0f } },
                  [](const PFXImage& src, const std::vector<float>& p) {
                      int size = static_cast<int>(p[0]) * 2 + 1;
                      return FX::Morphology::Rank(src, size, size, size * size - 1); } },
                { "open", "Open", "Erode then dilate — removes small bright specks (Morphology::Rank).",
                  { { "Radius", 1.0f, 6.0f, 2.0f, 1.0f } },
                  [](const PFXImage& src, const std::vector<float>& p) {
                      int size = static_cast<int>(p[0]) * 2 + 1;
                      PFXImage eroded = FX::Morphology::Rank(src, size, size, 0);
                      return FX::Morphology::Rank(eroded, size, size, size * size - 1); } },
                { "close", "Close", "Dilate then erode — fills small dark holes (Morphology::Rank).",
                  { { "Radius", 1.0f, 6.0f, 2.0f, 1.0f } },
                  [](const PFXImage& src, const std::vector<float>& p) {
                      int size = static_cast<int>(p[0]) * 2 + 1;
                      PFXImage dilated = FX::Morphology::Rank(src, size, size, size * size - 1);
                      return FX::Morphology::Rank(dilated, size, size, 0); } },
            } },
        };
        return categories;
    }

    const Effect* FindEffect(const std::string& id) {
        for (const auto& cat : EffectCategories())
            for (const auto& fx : cat.effects)
                if (fx.id == id) return &fx;
        return nullptr;
    }

    // ===== PLAYGROUND STATE =====
    // Shared by every callback of the Examples tab. The PNG buffer backing the
    // displayed image must outlive the UCImageRaster created from it
    // (LoadFromMemory references the caller's bytes instead of copying), so it
    // lives here next to the rest of the state.
    struct PlaygroundState {
        std::string imagePath;
        PixelFX::PFXImage original;
        bool originalValid = false;
        const Effect* effect = nullptr;
        std::vector<float> params;
        std::vector<uint8_t> displayBuffer;
    };

    // Longest edge the playground processes. Uploaded photos can be huge and
    // every slider tick reprocesses the image, so keep it interactive.
    constexpr int kMaxProcessingSize = 1200;

    std::string FormatParamValue(float value) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2f", value);
        std::string s(buf);
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
        return s;
    }

    // Raster image extensions offered in the sample dropdown / upload dialog.
    bool IsSampleImageExtension(std::string ext) {
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        static const std::vector<std::string> exts =
            { "jpg", "jpeg", "png", "webp", "bmp", "gif", "tif", "tiff", "avif", "heic" };
        return std::find(exts.begin(), exts.end(), ext) != exts.end();
    }

    // All raster images in media/images, sorted by name — the dropdown content.
    std::vector<std::string> CollectSampleImages(const std::string& dir) {
        std::vector<std::string> names;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            if (!ext.empty()) ext.erase(0, 1);
            if (IsSampleImageExtension(ext)) names.push_back(entry.path().filename().string());
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    // ---- Overview tab: intro + diagram + playground category summary. ----
    std::shared_ptr<UltraCanvasUIElement> BuildOverviewTab() {
        const std::string base    = NormalizePath(GetResourcesDir() + "Docs/Modules/PixelFX/");
        const std::string svgName = "PixelFX.svg";

        std::string combined;
        std::string intro = LoadFile(base + "intro.md");
        if (intro.find_first_not_of(" \t\r\n") != std::string::npos) {
            combined += intro + "\n\n";
        }
        if (std::ifstream(base + svgName).good()) {
            combined += "![PixelFX architecture](" + svgName + ")\n\n";
        }

        combined += "## Playground functions\n\n"
                    "| Category | Functions |\n|---|---|\n";
        for (const auto& cat : EffectCategories()) {
            combined += "| **" + cat.name + "** | ";
            for (size_t i = 0; i < cat.effects.size(); ++i) {
                if (i) combined += ", ";
                combined += cat.effects[i].name;
            }
            combined += " |\n";
        }
        combined += "\nSwitch to the *Examples* tab to try them on a sample or uploaded image.\n";

        auto text = std::make_shared<UltraCanvasTextArea>("PixelFXOverview");
        text->size.width  = CSSLayout::Dimension::Pct(100);
        text->size.height = CSSLayout::Dimension::Pct(100);
        text->SetMarkdownBaseDirectory(base);
        text->SetText(combined);
        text->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
        text->SetReadOnly(true);
        text->SetWordWrap(true);
        text->SetCursorPosition(LineColumnIndex::INVALID);
        text->SetPadding(0, 5, 0, 7);
        return text;
    }

    // ---- Details tab: the full README documentation. ----
    std::shared_ptr<UltraCanvasUIElement> BuildDetailsTab() {
        const std::string base = NormalizePath(GetResourcesDir() + "Docs/Modules/PixelFX/");

        auto text = std::make_shared<UltraCanvasTextArea>("PixelFXDetails");
        text->size.width  = CSSLayout::Dimension::Pct(100);
        text->size.height = CSSLayout::Dimension::Pct(100);
        text->SetMarkdownBaseDirectory(base);
        text->SetText(LoadFile(base + "README.md"));
        text->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
        text->SetReadOnly(true);
        text->SetWordWrap(true);
        text->SetCursorPosition(LineColumnIndex::INVALID);
        text->SetPadding(0, 5, 0, 7);
        return text;
    }

    // ---- Examples tab: the interactive processing playground. ----
    std::shared_ptr<UltraCanvasUIElement> BuildExamplesTab() {
        const std::string imagesDir = NormalizePath(GetResourcesDir() + "media/images/");
        const std::string iconsDir  = NormalizePath(GetResourcesDir() + "media/icons/");

        auto state = std::make_shared<PlaygroundState>();

        auto root = std::make_shared<UltraCanvasContainer>("PixelFXDemo", 0, 0, 1000, 700);
        root->layout.SetFlexColumn().SetFlexGap(8)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        root->SetPadding(10, 12, 10, 12);
        root->SetBackgroundColor(Colors::White);

        auto title = std::make_shared<UltraCanvasLabel>("PixelFXDemoTitle", 0, 0, 0, 26);
        title->SetText("PixelFX playground");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        title->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        root->AddChild(title);

        // ===== Three columns: image | function tree | options =====
        auto columns = std::make_shared<UltraCanvasContainer>("PixelFXColumns", 0, 0, 0, 0);
        columns->layout.SetFlexRow().SetFlexGap(12)
                       .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        columns->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        root->AddChild(columns);

        // ----- Left column: sample selector + upload + image display -----
        auto leftCol = std::make_shared<UltraCanvasContainer>("PixelFXLeft", 0, 0, 0, 0);
        leftCol->layout.SetFlexColumn().SetFlexGap(8)
                       .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        leftCol->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        columns->AddChild(leftCol);

        auto sourceRow = std::make_shared<UltraCanvasContainer>("PixelFXSourceRow", 0, 0, 0, 34);
        sourceRow->layout.SetFlexRow().SetFlexGap(8)
                         .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        sourceRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        leftCol->AddChild(sourceRow);

        auto sourceLabel = std::make_shared<UltraCanvasLabel>("PixelFXSourceLabel", 0, 0, 96, 24);
        sourceLabel->SetText("Sample image");
        sourceLabel->SetFontSize(12);
        sourceLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        sourceRow->AddChild(sourceLabel);

        auto sampleDropdown = std::make_shared<UltraCanvasDropdown>("PixelFXSample", 0, 0, 210, 30);
        sampleDropdown->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        sourceRow->AddChild(sampleDropdown);

        auto uploadBtn = std::make_shared<UltraCanvasButton>("PixelFXUpload", 0, 0, 130, 30, "Upload image…");
        uploadBtn->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        sourceRow->AddChild(uploadBtn);

        auto imageFrame = std::make_shared<UltraCanvasContainer>("PixelFXImageFrame", 0, 0, 0, 0);
        imageFrame->layout.SetFlexColumn()
                          .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        imageFrame->SetBorders(1.0f, Color(200, 200, 205, 255), 4.0f);
        imageFrame->SetBackgroundColor(Color(246, 246, 248, 255));
        imageFrame->SetPadding(6);
        imageFrame->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                              .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        leftCol->AddChild(imageFrame);

        auto imageView = std::make_shared<UltraCanvasImageElement>("PixelFXImage", 0.0f, 0.0f);
        imageView->SetFitMode(ImageFitMode::Contain);
        imageView->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                             .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        imageFrame->AddChild(imageView);

        auto status = std::make_shared<UltraCanvasLabel>("PixelFXStatus", 0, 0, 0, 32);
        status->SetFontSize(11);
        status->SetTextColor(Color(90, 90, 90, 255));
        status->SetWrap(TextWrap::WrapWord);
        status->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        leftCol->AddChild(status);

        // ----- Middle column: the processing-function tree -----
        auto treeCol = std::make_shared<UltraCanvasContainer>("PixelFXTreeCol", 0, 0, 250, 0);
        treeCol->layout.SetFlexColumn().SetFlexGap(6)
                       .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        treeCol->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        columns->AddChild(treeCol);

        auto treeTitle = std::make_shared<UltraCanvasLabel>("PixelFXTreeTitle", 0, 0, 0, 22);
        treeTitle->SetText("PixelFX functions");
        treeTitle->SetFontSize(13);
        treeTitle->SetFontWeight(FontWeight::Bold);
        treeTitle->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        treeCol->AddChild(treeTitle);

        auto tree = std::make_shared<UltraCanvasTreeView>("PixelFXTree", 0, 0, 250, 0);
        tree->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        TreeNodeData rootData("pixelfx_root", "PixelFX processing");
        rootData.leftIcon = TreeNodeIcon(iconsDir + "image.png", 16, 16);
        tree->SetRootNode(rootData);
        for (const auto& cat : EffectCategories()) {
            TreeNodeData catData("cat_" + cat.id, cat.name);
            catData.leftIcon = TreeNodeIcon(iconsDir + "folder.png", 16, 16);
            tree->AddNode("pixelfx_root", catData);
            for (const auto& fx : cat.effects) {
                TreeNodeData fxData(fx.id, fx.name);
                fxData.leftIcon = TreeNodeIcon(iconsDir + "text.png", 16, 16);
                tree->AddNode("cat_" + cat.id, fxData);
            }
        }
        tree->ExpandAll();
        treeCol->AddChild(tree);

        // ----- Right column: options for the selected function -----
        auto optionsCol = std::make_shared<UltraCanvasContainer>("PixelFXOptionsCol", 0, 0, 280, 0);
        optionsCol->layout.SetFlexColumn().SetFlexGap(6)
                          .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        optionsCol->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                              .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        columns->AddChild(optionsCol);

        auto optionsTitle = std::make_shared<UltraCanvasLabel>("PixelFXOptionsTitle", 0, 0, 0, 22);
        optionsTitle->SetText("Options");
        optionsTitle->SetFontSize(13);
        optionsTitle->SetFontWeight(FontWeight::Bold);
        optionsTitle->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        optionsCol->AddChild(optionsTitle);

        auto optionsBox = std::make_shared<UltraCanvasContainer>("PixelFXOptionsBox", 0, 0, 0, 0);
        optionsBox->layout.SetFlexColumn().SetFlexGap(8)
                          .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        optionsBox->SetBorders(1.0f, Color(200, 200, 205, 255), 4.0f);
        optionsBox->SetPadding(10);
        optionsBox->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                              .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        optionsCol->AddChild(optionsBox);

        // ===== Behaviour =====

        auto setStatus = [status](const std::string& text) {
            status->SetText(text);
            status->RequestRedraw();
        };

        // Encode a PFXImage to PNG and hand it to the image element. The PNG
        // bytes stay alive in `state` because the raster references them.
        auto showImage = [state, imageView, setStatus](const PixelFX::PFXImage& img) -> bool {
            try {
                PixelFX::PFXImage display = img;
                if (display.GetFormat() != PixelFX::BandFormat::FmtUChar) {
                    display = PixelFX::Conversion::CastUchar(display);
                }
                state->displayBuffer = PixelFX::FileIO::SaveToBuffer(display, ".png");
                auto raster = UCImageRaster::LoadFromMemory(state->displayBuffer);
                if (!raster || !raster->errorMessage.empty()) {
                    setStatus("Could not decode processed image" +
                              (raster ? ": " + raster->errorMessage : std::string(".")));
                    return false;
                }
                imageView->LoadFromImage(raster);
                return true;
            } catch (const std::exception& e) {
                setStatus(std::string("Display failed: ") + e.what());
                return false;
            }
        };

        // Run the selected effect (or none) on the loaded source and display it.
        auto applyCurrent = [state, showImage, setStatus]() {
            if (!state->originalValid) return;
            if (!state->effect) {
                if (showImage(state->original)) {
                    setStatus("Original image — pick a function from the tree to process it.");
                }
                return;
            }
            try {
                PixelFX::PFXImage result = state->effect->apply(state->original, state->params);
                if (!showImage(result)) return;
                std::string text = state->effect->name;
                if (!state->effect->params.empty()) {
                    text += " (";
                    for (size_t i = 0; i < state->effect->params.size(); ++i) {
                        if (i) text += ", ";
                        text += state->effect->params[i].label + " = " +
                                FormatParamValue(state->params[i]);
                    }
                    text += ")";
                }
                setStatus(text + " — " + state->effect->description);
            } catch (const std::exception& e) {
                setStatus(std::string("Processing failed: ") + e.what());
            }
        };

        // Load a source image, capped to a size that keeps slider drags fluid.
        auto loadSource = [state, applyCurrent, setStatus](const std::string& path) {
            try {
                PixelFX::PFXImage img = PixelFX::FileIO::Load(path);
                if (std::max(img.Width(), img.Height()) > kMaxProcessingSize) {
                    img = PixelFX::Resample::Thumbnail(img, kMaxProcessingSize);
                }
                state->original = img;
                state->originalValid = true;
                state->imagePath = path;
                applyCurrent();
            } catch (const std::exception& e) {
                state->originalValid = false;
                setStatus("Could not load " + path + "\n" + e.what());
            }
        };

        // Rebuild the options panel for the selected effect: one slider +
        // value label per parameter and a "Show original" reset button.
        auto rebuildOptions = std::make_shared<std::function<void()>>();
        *rebuildOptions = [state, optionsBox, applyCurrent, showImage, setStatus]() {
            optionsBox->ClearChildren();

            if (!state->effect) {
                auto hint = std::make_shared<UltraCanvasLabel>("PixelFXOptHint", 0, 0, 0, 60);
                hint->SetText("Select a processing function in the tree to see its options.");
                hint->SetFontSize(12);
                hint->SetTextColor(Color(120, 120, 120, 255));
                hint->SetWrap(TextWrap::WrapWord);
                hint->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                optionsBox->AddChild(hint);
            } else {
                const Effect& fx = *state->effect;

                auto name = std::make_shared<UltraCanvasLabel>("PixelFXOptName", 0, 0, 0, 22);
                name->SetText(fx.name);
                name->SetFontSize(13);
                name->SetFontWeight(FontWeight::Bold);
                name->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                optionsBox->AddChild(name);

                auto desc = std::make_shared<UltraCanvasLabel>("PixelFXOptDesc", 0, 0, 0, 56);
                desc->SetText(fx.description);
                desc->SetFontSize(11);
                desc->SetTextColor(Color(110, 110, 110, 255));
                desc->SetWrap(TextWrap::WrapWord);
                desc->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                optionsBox->AddChild(desc);

                for (size_t i = 0; i < fx.params.size(); ++i) {
                    const EffectParam& param = fx.params[i];

                    auto valueLabel = std::make_shared<UltraCanvasLabel>(
                        "PixelFXOptLabel" + std::to_string(i), 0, 0, 0, 18);
                    valueLabel->SetText(param.label + ": " + FormatParamValue(state->params[i]));
                    valueLabel->SetFontSize(12);
                    valueLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                    optionsBox->AddChild(valueLabel);

                    auto slider = CreateHorizontalSlider(
                        "PixelFXOptSlider" + std::to_string(i), 0, 0, 0, 24,
                        param.minValue, param.maxValue);
                    slider->SetStep(param.step);
                    slider->SetValue(state->params[i]);
                    slider->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
                    slider->onValueChanged = [state, applyCurrent, valueLabel, param, i](float value) {
                        if (i >= state->params.size()) return;
                        state->params[i] = value;
                        valueLabel->SetText(param.label + ": " + FormatParamValue(value));
                        valueLabel->RequestRedraw();
                        applyCurrent();
                    };
                    optionsBox->AddChild(slider);
                }

                if (fx.params.empty()) {
                    auto none = std::make_shared<UltraCanvasLabel>("PixelFXOptNone", 0, 0, 0, 20);
                    none->SetText("This function has no adjustable options.");
                    none->SetFontSize(11);
                    none->SetTextColor(Color(120, 120, 120, 255));
                    none->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                    optionsBox->AddChild(none);
                }

                auto originalBtn = std::make_shared<UltraCanvasButton>(
                    "PixelFXShowOriginal", 0, 0, 130, 28, "Show original");
                originalBtn->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                originalBtn->onClick = [state, showImage, setStatus]() {
                    if (!state->originalValid) return;
                    if (showImage(state->original)) {
                        setStatus("Original image (function still selected — move a "
                                  "slider or re-select it to process again).");
                    }
                };
                optionsBox->AddChild(originalBtn);
            }

            optionsBox->InvalidateLayout();
            optionsBox->RequestRedraw();
        };

        tree->onNodeSelected = [state, rebuildOptions, applyCurrent](TreeNode* node) {
            if (!node) return;
            const Effect* fx = FindEffect(node->data.nodeId);
            if (!fx) return;   // root / category nodes just expand
            state->effect = fx;
            state->params.clear();
            for (const auto& param : fx->params) state->params.push_back(param.defValue);
            (*rebuildOptions)();
            applyCurrent();
        };

        // ----- Sample image dropdown (value = full path) -----
        const std::vector<std::string> samples = CollectSampleImages(imagesDir);
        int defaultIndex = 0;
        for (size_t i = 0; i < samples.size(); ++i) {
            sampleDropdown->AddItem(DropdownItem(samples[i], imagesDir + samples[i]));
            if (samples[i] == "portrait.jpg") defaultIndex = static_cast<int>(i);
        }
        sampleDropdown->onSelectionChanged =
            [loadSource](int, const DropdownItem& item) { loadSource(item.value); };

        // ----- Upload a custom image through the native file dialog -----
        uploadBtn->onClick = [state, sampleDropdown, loadSource, setStatus]() {
            FileDialogOptions opts;
            opts.SetTitle("Open image for PixelFX")
                .AddFilter("Bitmap images",
                           { "png", "jpg", "jpeg", "webp", "bmp", "gif", "tif", "tiff", "avif", "heic" })
                .AddFilter("All files", "*");

            UltraCanvasFileLoader::OpenFileDialog(opts,
                [state, sampleDropdown, loadSource, setStatus](DialogResult result, const std::string& path) {
                    if (result != DialogResult::OK || path.empty()) {
                        setStatus("Upload cancelled.");
                        return;
                    }
                    std::string name = std::filesystem::path(path).filename().string();
                    sampleDropdown->AddItem(DropdownItem("Custom: " + name, path));
                    sampleDropdown->SetSelectedIndex(sampleDropdown->GetItemCount() - 1, false);
                    loadSource(path);
                });
        };

        // Prime with cat.jpg (or the first sample) and an empty options panel.
        (*rebuildOptions)();
        if (!samples.empty()) {
            sampleDropdown->SetSelectedIndex(defaultIndex, false);
            loadSource(imagesDir + samples[defaultIndex]);
        } else {
            setStatus("No sample images found in " + imagesDir);
        }

        return root;
    }

} // anonymous namespace

// ===== PIXELFX MODULE DEMO =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreatePixelFXExamples() {
        auto root = std::make_shared<UltraCanvasContainer>("PixelFXExamples", 0, 0, 1000, 720);
        root->size.width  = CSSLayout::Dimension::Pct(100);
        root->size.height = CSSLayout::Dimension::Pct(100);
        root->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

        auto tabs = std::make_shared<UltraCanvasTabbedContainer>("PixelFXTabs", 0, 0, 0, 0);
        tabs->SetTabPosition(TabPosition::Top);
        tabs->SetTabStyle(TabStyle::Modern);
        tabs->SetCloseMode(TabCloseMode::NoClose);
        tabs->SetTabHeight(30);
        tabs->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        tabs->AddTab("Overview", BuildOverviewTab());
        tabs->AddTab("Details",  BuildDetailsTab());
        tabs->AddTab("Examples", BuildExamplesTab());
        tabs->SetActiveTab(0);

        root->AddChild(tabs);
        return root;
    }

} // namespace UltraCanvas

#endif // HAS_LIBVIPS
