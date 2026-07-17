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
#include "UltraCanvasColorPicker.h"
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

    // Extra, non-slider inputs an effect can consume: the flood-fill colour and
    // start position (picked interactively) and the index of the selected
    // dropdown choice (e.g. a blend mode or watermark corner).
    struct EffectContext {
        Color fillColor = Color(230, 60, 50, 255);
        float posX = 0.5f;      // normalised 0..1 within the image
        float posY = 0.5f;
        int choice = 0;
    };

    // A single PixelFX processing function: its parameters plus the call into
    // the PixelFX API. `apply` receives the source image, the current slider
    // values (same order as `params`) and the shared EffectContext. Effects
    // with a dropdown list their entries in `choices`; `usesFillColor` adds the
    // colour picker and the click-to-set fill position flow to the options panel.
    struct Effect {
        std::string id;
        std::string name;
        std::string description;
        std::vector<EffectParam> params;
        std::function<PixelFX::PFXImage(const PixelFX::PFXImage&, const std::vector<float>&, const EffectContext&)> apply;
        std::string choiceLabel;
        std::vector<std::string> choices;
        bool usesFillColor = false;
    };

    struct EffectCategory {
        std::string id;
        std::string name;
        std::vector<Effect> effects;
    };

    // Blend modes offered by the "Blend modes" effect. Arithmetic::Blend wraps
    // vips composite2: Blend(top, bottom, mode) draws `top` onto `bottom`.
    const std::vector<std::pair<std::string, PixelFX::BlendMode>>& BlendModeChoices() {
        static const std::vector<std::pair<std::string, PixelFX::BlendMode>> modes = {
            { "Over",          PixelFX::BlendMode::Over },
            { "Multiply",      PixelFX::BlendMode::Multiply },
            { "Screen",        PixelFX::BlendMode::Screen },
            { "Overlay",       PixelFX::BlendMode::Overlay },
            { "Darken",        PixelFX::BlendMode::Darken },
            { "Lighten",       PixelFX::BlendMode::Lighten },
            { "Colour dodge",  PixelFX::BlendMode::ColourDodge },
            { "Colour burn",   PixelFX::BlendMode::ColourBurn },
            { "Hard light",    PixelFX::BlendMode::HardLight },
            { "Soft light",    PixelFX::BlendMode::SoftLight },
            { "Difference",    PixelFX::BlendMode::Difference },
            { "Exclusion",     PixelFX::BlendMode::Exclusion },
            { "Add",           PixelFX::BlendMode::Add },
        };
        return modes;
    }

    std::vector<std::string> BlendModeNames() {
        std::vector<std::string> names;
        for (const auto& m : BlendModeChoices()) names.push_back(m.first);
        return names;
    }

    // The processing functions offered by the playground, grouped the same way
    // as the PixelFX namespaces (Colour, Convolution, Resample, Morphology, …).
    const std::vector<EffectCategory>& EffectCategories() {
        using PixelFX::PFXImage;
        namespace FX = PixelFX;

        static const std::vector<EffectCategory> categories = {
            { "colour", "Colour adjustments", {
                { "brightness", "Brightness", "Multiplies every pixel by the chosen factor (Colour::Brightness).",
                  { { "Factor", 0.1f, 3.0f, 1.4f, 0.05f } },
                  [](const PFXImage& src, const std::vector<float>& p, const EffectContext&) { return FX::Colour::Brightness(src, p[0]); } },
                { "contrast", "Contrast", "Scales the distance of every pixel from the image mean (Colour::Contrast).",
                  { { "Factor", 0.1f, 3.0f, 1.5f, 0.05f } },
                  [](const PFXImage& src, const std::vector<float>& p, const EffectContext&) { return FX::Colour::Contrast(src, p[0]); } },
                { "saturation", "Saturation", "Scales chroma in LCh colour space (Colour::Saturation).",
                  { { "Factor", 0.0f, 3.0f, 1.8f, 0.05f } },
                  [](const PFXImage& src, const std::vector<float>& p, const EffectContext&) { return FX::Colour::Saturation(src, p[0]); } },
                { "gamma", "Gamma", "Applies a gamma curve to all channels (Colour::Gamma).",
                  { { "Gamma", 0.2f, 3.0f, 2.2f, 0.05f } },
                  [](const PFXImage& src, const std::vector<float>& p, const EffectContext&) { return FX::Colour::Gamma(src, p[0]); } },
                { "invert", "Invert", "Photographic negative (Colour::Invert).", {},
                  [](const PFXImage& src, const std::vector<float>&, const EffectContext&) { return FX::Colour::Invert(src); } },
                { "grayscale", "Grayscale", "Converts to single-band black & white (Colour::Grayscale).", {},
                  [](const PFXImage& src, const std::vector<float>&, const EffectContext&) { return FX::Colour::Grayscale(src); } },
                { "sepia", "Sepia", "Classic warm sepia tone matrix (Colour::Sepia).", {},
                  [](const PFXImage& src, const std::vector<float>&, const EffectContext&) { return FX::Colour::Sepia(src); } },
                { "histequal", "Histogram equalise", "Spreads the histogram for maximum global contrast (Colour::HistEqual).", {},
                  [](const PFXImage& src, const std::vector<float>&, const EffectContext&) { return FX::Colour::HistEqual(src); } },
            } },
            { "blur", "Blur & sharpen", {
                { "gaussblur", "Gaussian blur", "Smooths the image with a Gaussian kernel (Convolution::GaussianBlur).",
                  { { "Sigma", 0.5f, 20.0f, 5.0f, 0.5f } },
                  [](const PFXImage& src, const std::vector<float>& p, const EffectContext&) { return FX::Convolution::GaussianBlur(src, p[0]); } },
                { "boxblur", "Box blur", "Averages pixels inside a square window (Convolution::BoxBlur).",
                  { { "Radius", 1.0f, 25.0f, 8.0f, 1.0f } },
                  [](const PFXImage& src, const std::vector<float>& p, const EffectContext&) { return FX::Convolution::BoxBlur(src, static_cast<int>(p[0])); } },
                { "median", "Median filter", "Rank filter that removes noise while keeping edges (Morphology::Median).",
                  { { "Window size", 3.0f, 15.0f, 5.0f, 2.0f } },
                  [](const PFXImage& src, const std::vector<float>& p, const EffectContext&) {
                      int size = static_cast<int>(p[0]) | 1;   // vips needs an odd window
                      return FX::Morphology::Median(src, size); } },
                { "sharpen", "Sharpen", "Unsharp masking in LAB space (Convolution::Sharpen).",
                  { { "Sigma", 0.5f, 10.0f, 2.0f, 0.5f } },
                  [](const PFXImage& src, const std::vector<float>& p, const EffectContext&) { return FX::Convolution::Sharpen(src, p[0], 2.0, 1.0); } },
                { "unsharp", "Unsharp mask", "Boosts local contrast by subtracting a blurred copy (Convolution::UnsharpMask).",
                  { { "Sigma", 1.0f, 10.0f, 3.0f, 0.5f },
                    { "Amount", 0.1f, 3.0f, 1.0f, 0.1f } },
                  [](const PFXImage& src, const std::vector<float>& p, const EffectContext&) { return FX::Convolution::UnsharpMask(src, p[0], p[1]); } },
            } },
            { "edges", "Edge detection", {
                { "sobel", "Sobel", "Classic gradient edge detector (Convolution::Sobel).", {},
                  [](const PFXImage& src, const std::vector<float>&, const EffectContext&) { return FX::Convolution::Sobel(src); } },
                { "canny", "Canny", "Thin, precise edges after Gaussian smoothing (Convolution::Canny).",
                  { { "Sigma", 0.5f, 5.0f, 1.4f, 0.1f } },
                  [](const PFXImage& src, const std::vector<float>& p, const EffectContext&) {
                      // canny returns small float magnitudes — scale them into 0..255
                      return FX::Arithmetic::Multiply(FX::Convolution::Canny(src, p[0]), 64.0); } },
                { "laplacian", "Laplacian", "Second-derivative edge detector (Convolution::Laplacian).", {},
                  [](const PFXImage& src, const std::vector<float>&, const EffectContext&) { return FX::Convolution::Laplacian(src); } },
                { "prewitt", "Prewitt", "Gradient magnitude from Prewitt kernels (Convolution::Prewitt).", {},
                  [](const PFXImage& src, const std::vector<float>&, const EffectContext&) { return FX::Convolution::Prewitt(src); } },
                { "scharr", "Scharr", "Rotation-optimised gradient kernels (Convolution::Scharr).", {},
                  [](const PFXImage& src, const std::vector<float>&, const EffectContext&) {
                      // Scharr weights are ±10 — bring the magnitude back into range
                      return FX::Arithmetic::Multiply(FX::Convolution::Scharr(src), 0.1); } },
            } },
            { "geometry", "Geometry & resample", {
                { "rotate", "Rotate", "Rotates around the centre with bicubic resampling (Resample::Rotate).",
                  { { "Angle °", -180.0f, 180.0f, 30.0f, 1.0f } },
                  [](const PFXImage& src, const std::vector<float>& p, const EffectContext&) { return FX::Resample::Rotate(src, p[0]); } },
                { "fliph", "Flip horizontal", "Mirrors the image left ↔ right (Resample::FlipHorizontal).", {},
                  [](const PFXImage& src, const std::vector<float>&, const EffectContext&) { return FX::Resample::FlipHorizontal(src); } },
                { "flipv", "Flip vertical", "Mirrors the image top ↔ bottom (Resample::FlipVertical).", {},
                  [](const PFXImage& src, const std::vector<float>&, const EffectContext&) { return FX::Resample::FlipVertical(src); } },
                { "scale", "Scale", "Resizes with a Lanczos3 kernel (Resample::Resize). The "
                  "preview grows or shrinks with the factor relative to the frame: 1.0 fits "
                  "the frame, below 1.0 shrinks centred, above 1.0 pins the width and grows "
                  "taller (centre-cropped) up to the frame border.",
                  { { "Factor", 0.1f, 3.0f, 1.0f, 0.05f } },
                  [](const PFXImage& src, const std::vector<float>& p, const EffectContext&) { return FX::Resample::Resize(src, p[0]); } },
                { "pixelate", "Pixelate", "Subsamples then zooms back up for a mosaic look (Conversion::Subsample + Zoom).",
                  { { "Block size", 2.0f, 32.0f, 8.0f, 1.0f } },
                  [](const PFXImage& src, const std::vector<float>& p, const EffectContext&) {
                      int f = std::max(2, static_cast<int>(p[0]));
                      return FX::Conversion::Zoom(FX::Conversion::Subsample(src, f), f); } },
                { "centercrop", "Center crop", "Keeps the chosen percentage of the image around its centre (Conversion::Crop).",
                  { { "Keep %", 10.0f, 100.0f, 60.0f, 1.0f } },
                  [](const PFXImage& src, const std::vector<float>& p, const EffectContext&) {
                      int w = std::max(1, static_cast<int>(src.Width()  * p[0] / 100.0f));
                      int h = std::max(1, static_cast<int>(src.Height() * p[0] / 100.0f));
                      return FX::Conversion::Crop(src, (src.Width() - w) / 2, (src.Height() - h) / 2, w, h); } },
                { "smartcrop", "Smart crop", "Attention-based crop that keeps the most interesting region (Conversion::SmartCrop).",
                  { { "Width %",  10.0f, 100.0f, 50.0f, 1.0f },
                    { "Height %", 10.0f, 100.0f, 50.0f, 1.0f } },
                  [](const PFXImage& src, const std::vector<float>& p, const EffectContext&) {
                      int w = std::max(1, static_cast<int>(src.Width()  * p[0] / 100.0f));
                      int h = std::max(1, static_cast<int>(src.Height() * p[1] / 100.0f));
                      return FX::Conversion::SmartCrop(src, w, h); } },
                { "similarity", "Similarity transform", "Combined scale + rotation around the centre in one resampling pass (Resample::Similarity).",
                  { { "Scale", 0.2f, 2.0f, 0.8f, 0.05f },
                    { "Angle °", -180.0f, 180.0f, 20.0f, 1.0f } },
                  [](const PFXImage& src, const std::vector<float>& p, const EffectContext&) {
                      return FX::Resample::Similarity(src, p[0], p[1]); } },
            } },
            // Greyscale morphology via rank filters: Morphology::Erode/Dilate wrap
            // vips_morph, which only operates on binary (0/255) images. A rank
            // filter taking the neighbourhood minimum / maximum is the greyscale
            // equivalent and works on photographs.
            { "morphology", "Morphology", {
                { "erode", "Erode", "Shrinks bright regions — neighbourhood minimum (Morphology::Rank).",
                  { { "Radius", 1.0f, 6.0f, 2.0f, 1.0f } },
                  [](const PFXImage& src, const std::vector<float>& p, const EffectContext&) {
                      int size = static_cast<int>(p[0]) * 2 + 1;
                      return FX::Morphology::Rank(src, size, size, 0); } },
                { "dilate", "Dilate", "Grows bright regions — neighbourhood maximum (Morphology::Rank).",
                  { { "Radius", 1.0f, 6.0f, 2.0f, 1.0f } },
                  [](const PFXImage& src, const std::vector<float>& p, const EffectContext&) {
                      int size = static_cast<int>(p[0]) * 2 + 1;
                      return FX::Morphology::Rank(src, size, size, size * size - 1); } },
                { "open", "Open", "Erode then dilate — removes small bright specks (Morphology::Rank).",
                  { { "Radius", 1.0f, 6.0f, 2.0f, 1.0f } },
                  [](const PFXImage& src, const std::vector<float>& p, const EffectContext&) {
                      int size = static_cast<int>(p[0]) * 2 + 1;
                      PFXImage eroded = FX::Morphology::Rank(src, size, size, 0);
                      return FX::Morphology::Rank(eroded, size, size, size * size - 1); } },
                { "close", "Close", "Dilate then erode — fills small dark holes (Morphology::Rank).",
                  { { "Radius", 1.0f, 6.0f, 2.0f, 1.0f } },
                  [](const PFXImage& src, const std::vector<float>& p, const EffectContext&) {
                      int size = static_cast<int>(p[0]) * 2 + 1;
                      PFXImage dilated = FX::Morphology::Rank(src, size, size, size * size - 1);
                      return FX::Morphology::Rank(dilated, size, size, 0); } },
            } },
            { "draw", "Draw", {
                { "floodfill", "Flood fill",
                  "Paint-bucket fill around the picked point (Draw::MagicWandMask). The Fill "
                  "mode chooses how the region grows: Gradient spreads while each pixel stays "
                  "within Tolerance of its neighbour (magic wand — follows smooth shading); "
                  "Global keeps pixels within Tolerance of the clicked colour; Exact takes "
                  "only the connected patch of the identical colour. Raising Tolerance always "
                  "enlarges the area. Choose the ink below, press “Set fill position” and "
                  "click a spot in the image.",
                  { { "Tolerance", 1.0f, 48.0f, 10.0f, 1.0f } },
                  [](const PFXImage& src, const std::vector<float>& p, const EffectContext& ctx) {
                      PFXImage base = FX::Conversion::CastUchar(src);
                      if (FX::Colour::HasAlpha(base)) base = FX::Colour::Flatten(base);
                      int x = std::clamp(static_cast<int>(ctx.posX * (base.Width()  - 1)), 0, base.Width()  - 1);
                      int y = std::clamp(static_cast<int>(ctx.posY * (base.Height() - 1)), 0, base.Height() - 1);
                      FX::Draw::FloodMatch match =
                          ctx.choice == 1 ? FX::Draw::FloodMatch::Seed  :
                          ctx.choice == 2 ? FX::Draw::FloodMatch::Exact :
                                            FX::Draw::FloodMatch::Neighbour;
                      // The tolerance modes run on a median-smoothed guide so sensor noise
                      // doesn't block or leak the spread; Exact matches the true pixels.
                      // The ink is painted over the pristine original either way, so
                      // untouched areas keep their detail.
                      PFXImage guide = (match == FX::Draw::FloodMatch::Exact)
                                           ? base : FX::Morphology::Median(base, 3);
                      PFXImage mask  = FX::Draw::MagicWandMask(guide, x, y, std::max(0.0f, p[0]), match);
                      PFXImage ink   = FX::Generate::NewFromImage(base,
                          { static_cast<double>(ctx.fillColor.r),
                            static_cast<double>(ctx.fillColor.g),
                            static_cast<double>(ctx.fillColor.b) });
                      return FX::Arithmetic::Ifthenelse(mask, ink, base); },
                  "Fill mode", { "Gradient (neighbour)", "Global (seed colour)", "Exact colour" }, true },
            } },
            { "compositing", "Compositing", {
                { "blend", "Blend modes",
                  "Blends a generated colour gradient over the image with the selected "
                  "vips blend mode (Arithmetic::Blend + Generate::LinearGradient).",
                  { { "Overlay opacity", 0.1f, 1.0f, 0.75f, 0.05f } },
                  [](const PFXImage& src, const std::vector<float>& p, const EffectContext& ctx) {
                      PFXImage base = FX::Conversion::CastUchar(src);
                      if (FX::Colour::HasAlpha(base)) base = FX::Colour::Flatten(base);
                      int w = base.Width(), h = base.Height();
                      PFXImage gh = FX::Generate::LinearGradient(w, h, PixelFX::Direction::Horizontal);
                      PFXImage gv = FX::Generate::LinearGradient(w, h, PixelFX::Direction::Vertical);
                      PFXImage overlay = FX::Colour::Bandjoin({ gh, gv, FX::Colour::Invert(gh) });
                      overlay = FX::Colour::BandjoinConst(overlay, { p[0] * 255.0 });
                      // Band-joined gradients come back tagged "multiband"; composite
                      // needs a colourspace it can route to sRGB.
                      overlay = PFXImage(overlay.copy(vips::VImage::option()
                                             ->set("interpretation", VIPS_INTERPRETATION_sRGB)));
                      const auto& modes = BlendModeChoices();
                      int m = std::clamp(ctx.choice, 0, static_cast<int>(modes.size()) - 1);
                      return FX::Arithmetic::Blend(overlay, base, modes[m].second); },
                  "Blend mode", BlendModeNames() },
                { "watermark", "Watermark",
                  "Stamps the ULTRA OS logo onto the image with adjustable size and "
                  "opacity (Arithmetic::Blend in Over mode).",
                  { { "Size %",  10.0f, 60.0f, 25.0f, 1.0f },
                    { "Opacity", 0.1f,  1.0f,  0.8f,  0.05f } },
                  [](const PFXImage& src, const std::vector<float>& p, const EffectContext& ctx) {
                      PFXImage base = FX::Conversion::CastUchar(src);
                      if (FX::Colour::HasAlpha(base)) base = FX::Colour::Flatten(base);
                      PFXImage logo = FX::FileIO::Load(
                          NormalizePath(GetResourcesDir() + "media/images/UOS_logo_white.png"));
                      if (!FX::Colour::HasAlpha(logo)) logo = FX::Colour::AddAlpha(logo);
                      int margin = std::max(8, base.Width() / 50);
                      // scale to the requested width, capped so the logo always fits
                      double scale = (base.Width() * p[0] / 100.0) / logo.Width();
                      scale = std::min(scale, static_cast<double>(base.Width()  - 2 * margin) / logo.Width());
                      scale = std::min(scale, static_cast<double>(base.Height() - 2 * margin) / logo.Height());
                      logo = FX::Conversion::CastUchar(FX::Resample::Resize(logo, std::max(0.01, scale)));
                      PFXImage rgb   = FX::Colour::ExtractBand(logo, 0, 3);
                      PFXImage alpha = FX::Arithmetic::Multiply(FX::Colour::ExtractBand(logo, 3), p[1]);
                      logo = FX::Conversion::CastUchar(FX::Colour::Bandjoin(rgb, alpha));
                      int lw = logo.Width(), lh = logo.Height(), x = 0, y = 0;
                      switch (ctx.choice) {
                          case 0: x = base.Width() - lw - margin; y = base.Height() - lh - margin; break;
                          case 1: x = margin;                     y = base.Height() - lh - margin; break;
                          case 2: x = base.Width() - lw - margin; y = margin;                      break;
                          case 3: x = margin;                     y = margin;                      break;
                          default: x = (base.Width() - lw) / 2;   y = (base.Height() - lh) / 2;    break;
                      }
                      PFXImage canvas = FX::Conversion::Embed(logo, std::max(0, x), std::max(0, y),
                                                              base.Width(), base.Height(),
                                                              PixelFX::Extend::Black);
                      return FX::Arithmetic::Blend(canvas, base, PixelFX::BlendMode::Over); },
                  "Position", { "Bottom-right", "Bottom-left", "Top-right", "Top-left", "Centre" } },
            } },
            { "fourier", "Fourier", {
                { "spectrum", "Power spectrum",
                  "Log-scaled magnitude of the Fourier transform, wrapped so the DC "
                  "component sits in the centre (Fourier::Spectrum).", {},
                  [](const PFXImage& src, const std::vector<float>&, const EffectContext&) {
                      return FX::Fourier::Spectrum(FX::Colour::Grayscale(FX::Conversion::CastUchar(src))); } },
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
        bool infoMode = false;          // the "Others" tree entry is selected
        std::vector<float> params;
        EffectContext ctx;
        std::vector<uint8_t> displayBuffer;
    };

    // Image element that can additionally report a click position inside the
    // displayed (Contain-fitted) image as normalised 0..1 coordinates — used by
    // the flood-fill "Set fill position" flow.
    class PixelFXImageView : public UltraCanvasImageElement {
    public:
        using UltraCanvasImageElement::UltraCanvasImageElement;

        std::function<void(float, float)> onPixelPicked;
        int contentW = 0;              // pixel size of the image on display
        int contentH = 0;
        bool pickArmed = false;

        bool OnEvent(const UCEvent& event) override {
            // While armed, a left click inside the fitted image reports a fill
            // position. Picking stays armed so the user can keep re-placing the
            // fill; Esc or the right mouse button end the mode (handled by a
            // window-level event filter installed alongside this view).
            if (pickArmed && event.type == UCEventType::MouseDown &&
                event.button == UCMouseButton::Left &&
                Contains(event.pointer) && contentW > 0 && contentH > 0) {
                // Reproduce the ImageFitMode::Contain letterboxing of the renderer
                float ew = GetWidth(), eh = GetHeight();
                float scale = std::min(ew / contentW, eh / contentH);
                float dw = contentW * scale, dh = contentH * scale;
                float ox = (ew - dw) / 2.0f, oy = (eh - dh) / 2.0f;
                float nx = (event.pointer.x - ox) / dw;
                float ny = (event.pointer.y - oy) / dh;
                if (nx >= 0.0f && nx <= 1.0f && ny >= 0.0f && ny <= 1.0f) {
                    if (onPixelPicked) onPixelPicked(nx, ny);
                    return true;
                }
            }
            return UltraCanvasImageElement::OnEvent(event);
        }
    };

    // Markdown shown when the "Others" tree entry is selected: the PixelFX
    // functions the playground does not demo (FileIO excluded — the File
    // Loader and Bitmap pages already cover it).
    std::string OthersInfoMarkdown() {
        return
            "**Not demoed here**\n\n"
            "The PixelFX API offers many more functions than this playground shows:\n\n"
            "* **Arithmetic** — per-pixel add / subtract / multiply / divide, pow, exp, "
            "log, trigonometry, comparisons, boolean ops, statistics (min / max / avg / "
            "deviation), complex numbers, if-then-else compositing\n"
            "* **Colour** — colour-space conversions (sRGB, Lab, LCh, XYZ, CMC, HSV, "
            "scRGB), ICC profile import / export / transform, histogram find / "
            "normalise / match / plot / entropy, band extract / join / fold, "
            "premultiply / flatten and alpha management\n"
            "* **Draw** — circle, rectangle, line, point, smudge, image insert, masked "
            "stamping (flood fill is demoed)\n"
            "* **Convolution** — custom user kernels, separable / integer convolution, "
            "Gaussian / LoG / sharpen kernel builders, correlation (Fastcor, Spcor)\n"
            "* **Conversion** — bit-depth casts, byte-swap, array join / grid / wrap, "
            "replicate, embed / gravity borders, band folding, image joins\n"
            "* **Resample** — reduce / shrink variants, thumbnail-to-size, arbitrary "
            "affine matrices, quadratic warp, EXIF auto-rotate, index-image warping "
            "(Mapim)\n"
            "* **Generate** — solid fills, zone / sines / eye test patterns, Gaussian "
            "noise, Perlin & Worley noise, identity LUTs, radial gradients, text "
            "rendering, frequency-domain masks\n"
            "* **Morphology** — generic rank filter, line counting, structuring-element "
            "builders (disk, ring, cross)\n"
            "* **Fourier** — forward / inverse FFT, frequency multiply, phase "
            "correlation (the power spectrum is demoed)\n"
            "* **Utility** — cache and concurrency tuning, version and operation "
            "introspection, image-info extraction\n";
    }

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
        // This is a fixed-height strip holding the sample dropdown and the upload
        // button; its contents are meant to stay put. Disable scrollbars so a
        // narrow column never spawns a (horizontal) scrollbar across the block.
        ContainerStyle sourceRowStyle;
        sourceRowStyle.autoShowScrollbars = false;
        sourceRow->SetContainerStyle(sourceRowStyle);
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

        auto imageView = std::make_shared<PixelFXImageView>("PixelFXImage", 0.0f, 0.0f);
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
        // Expanding a category jumps the selection to its first function entry.
        tree->SetShowFirstChildOnExpand(true);

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
        TreeNodeData othersData("others", "Others");
        othersData.leftIcon = TreeNodeIcon(iconsDir + "text.png", 16, 16);
        tree->AddNode("pixelfx_root", othersData);
        tree->ExpandAll();
        treeCol->AddChild(tree);

        // ----- Right column: options for the selected function -----
        auto optionsCol = std::make_shared<UltraCanvasContainer>("PixelFXOptionsCol", 0, 0, 310, 0);
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

        // Arm / disarm the flood-fill position picker. Arming shows a crosshair
        // cursor over the image and keeps the mode active until the user cancels
        // with Esc or the right mouse button; a window-level event filter (installed
        // lazily on first arm) delivers those cancels regardless of which widget
        // currently holds focus.
        auto setPick = std::make_shared<std::function<void(bool)>>();
        auto escInstalled = std::make_shared<bool>(false);
        std::weak_ptr<std::function<void(bool)>> setPickWeak = setPick;
        *setPick = [imageView, setStatus, escInstalled, setPickWeak](bool on) {
            if (imageView->pickArmed == on) return;   // nothing to do
            imageView->pickArmed = on;
            imageView->SetMouseCursor(on ? UCMouseCursor::Cross : UCMouseCursor::Default);
            if (auto* win = imageView->GetWindow()) {
                if (on && !*escInstalled) {
                    *escInstalled = true;
                    PixelFXImageView* iv = imageView.get();
                    win->InstallEventFilter("PixelFXFloodFillPick",
                        [iv, setPickWeak](const UCEvent& e) -> bool {
                            // This filter lives on the window, which outlives the
                            // PixelFX page. `setPick` is owned by the page, so once
                            // the page (and `iv`) is destroyed the weak pointer
                            // expires — bail out before touching the dangling `iv`.
                            auto keepAlive = setPickWeak.lock();
                            if (!keepAlive) return false;
                            if (!iv->pickArmed) return false;
                            bool esc   = e.type == UCEventType::KeyDown &&
                                         e.virtualKey == UCKeys::Escape;
                            bool right = e.type == UCEventType::MouseDown &&
                                         e.button == UCMouseButton::Right;
                            if (esc || right) {
                                (*keepAlive)(false);
                                return true;   // consume so no context menu / other action fires
                            }
                            return false;
                        }, { UCEventType::KeyDown, UCEventType::MouseDown });
                }
                win->SelectMouseCursor(on ? UCMouseCursor::Cross : UCMouseCursor::Default);
            }
            setStatus(on ? "Click in the image to fill from that point — click again to "
                           "move the fill. Press Esc or right-click to finish."
                         : "Fill-position picking finished.");
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
                imageView->contentW = display.Width();
                imageView->contentH = display.Height();
                imageView->LoadFromImage(raster);
                return true;
            } catch (const std::exception& e) {
                setStatus(std::string("Display failed: ") + e.what());
                return false;
            }
        };

        // Build the on-screen image for the Scale demo so the display size tracks
        // the factor instead of always fitting the frame. `factor` 1.0 fits the
        // image into the frame (with a margin); smaller values shrink it centred on
        // a neutral canvas so the reduced area is visible; larger values grow it
        // proportionally (aspect preserved). Once the width reaches the frame it is
        // pinned and the image keeps growing in height — a centred crop — until it
        // meets the frame border minus the margin. Returns a canvas exactly the size
        // of the frame so the Contain fit maps it 1:1.
        auto frameScaled = [](const PixelFX::PFXImage& original, float factor,
                              int frameW, int frameH) -> PixelFX::PFXImage {
            namespace FX = PixelFX;
            const int margin = 10;
            PixelFX::PFXImage base = FX::Conversion::CastUchar(original);
            if (FX::Colour::HasAlpha(base)) base = FX::Colour::Flatten(base);
            const int W = base.Width(), H = base.Height();
            const int availW = std::max(1, frameW - 2 * margin);
            const int availH = std::max(1, frameH - 2 * margin);
            // On-screen scale: fit the original into the frame at factor 1.0, then
            // multiply by the requested factor.
            const double fitScale = std::min(static_cast<double>(availW) / W,
                                             static_cast<double>(availH) / H);
            const double dispScale = std::max(0.01, fitScale * std::max(0.01f, factor));
            PixelFX::PFXImage scaled = FX::Conversion::CastUchar(FX::Resample::Resize(base, dispScale));
            const int sw = scaled.Width(), sh = scaled.Height();
            // Pin to the available box, centre-cropping whatever overflows.
            const int cropW = std::min(sw, availW);
            const int cropH = std::min(sh, availH);
            PixelFX::PFXImage piece = (cropW < sw || cropH < sh)
                ? FX::Conversion::Crop(scaled, (sw - cropW) / 2, (sh - cropH) / 2, cropW, cropH)
                : scaled;
            PixelFX::PFXImage canvas = PixelFX::PFXImage::CreateSolid(frameW, frameH, { 246, 246, 248 });
            return FX::Conversion::Insert(canvas, piece, (frameW - cropW) / 2, (frameH - cropH) / 2);
        };

        // Run the selected effect (or none) on the loaded source and display it.
        auto applyCurrent = [state, showImage, setStatus, imageView, frameScaled]() {
            if (!state->originalValid) return;
            if (!state->effect) {
                if (showImage(state->original)) {
                    setStatus("Original image — pick a function from the tree to process it.");
                }
                return;
            }
            try {
                // The Scale demo frames its result to the display so the on-screen
                // size tracks the factor (see frameScaled); every other effect just
                // shows its processed image fitted to the frame.
                if (state->effect->id == "scale" && !state->params.empty()) {
                    int fw = static_cast<int>(imageView->GetWidth());
                    int fh = static_cast<int>(imageView->GetHeight());
                    if (fw < 32 || fh < 32) { fw = 640; fh = 480; }   // pre-layout fallback
                    if (!showImage(frameScaled(state->original, state->params[0], fw, fh))) return;
                    setStatus(state->effect->name + " (Factor = " + FormatParamValue(state->params[0]) +
                              ") — display grows with the factor: width pinned to the frame, then "
                              "taller to the border; below 1.0 it shrinks centred.");
                    return;
                }
                PixelFX::PFXImage result = state->effect->apply(state->original, state->params, state->ctx);
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
                // Materialise the source into a flat memory buffer. Effects such as
                // Flood fill read the pixels back in an arbitrary (non top-to-bottom)
                // order via write_to_memory; doing that straight off a lazy
                // file-backed pipeline can trip libvips' "out of order read" guard,
                // which on some builds aborts the process instead of throwing. A
                // memory-resident source supports random access safely and also
                // keeps the per-slider-tick reprocessing fast.
                img = PixelFX::PFXImage(img.copy_memory());
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
        // value label per parameter, an optional dropdown (blend mode /
        // watermark position), the flood-fill colour picker + position flow,
        // and a "Show original" reset button. When the "Others" entry is
        // selected the panel shows the not-demoed function list instead.
        auto rebuildOptions = std::make_shared<std::function<void()>>();
        *rebuildOptions = [state, optionsBox, imageView, applyCurrent, showImage, setStatus, iconsDir, setPick]() {
            optionsBox->ClearChildren();

            if (state->infoMode) {
                auto info = std::make_shared<UltraCanvasTextArea>("PixelFXOthersInfo");
                info->SetText(OthersInfoMarkdown());
                info->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
                info->SetReadOnly(true);
                info->SetWordWrap(true);
                info->SetCursorPosition(LineColumnIndex::INVALID);
                info->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                                .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
                optionsBox->AddChild(info);
            } else if (!state->effect) {
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

                // Optional dropdown (blend mode, watermark position, …)
                if (!fx.choices.empty()) {
                    auto choiceLabel = std::make_shared<UltraCanvasLabel>("PixelFXOptChoiceLabel", 0, 0, 0, 18);
                    choiceLabel->SetText(fx.choiceLabel);
                    choiceLabel->SetFontSize(12);
                    choiceLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                    optionsBox->AddChild(choiceLabel);

                    auto choiceDropdown = std::make_shared<UltraCanvasDropdown>("PixelFXOptChoice", 0, 0, 0, 30);
                    for (const auto& c : fx.choices) choiceDropdown->AddItem(c);
                    choiceDropdown->SetSelectedIndex(state->ctx.choice, false);
                    choiceDropdown->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                                              .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
                    choiceDropdown->onSelectionChanged = [state, applyCurrent](int index, const DropdownItem&) {
                        state->ctx.choice = index;
                        applyCurrent();
                    };
                    optionsBox->AddChild(choiceDropdown);
                }

                // Flood-fill extras: ink colour picker + click-to-set position
                if (fx.usesFillColor) {
                    auto colorLabel = std::make_shared<UltraCanvasLabel>("PixelFXOptColorLabel", 0, 0, 0, 18);
                    colorLabel->SetText("Fill colour");
                    colorLabel->SetFontSize(12);
                    colorLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                    optionsBox->AddChild(colorLabel);

                    auto picker = CreateColorPicker("PixelFXFillColor", state->ctx.fillColor, 0, 0, 286, 400);
                    picker->SetShowAlpha(false);
                    picker->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
                    picker->onColorChanged = [state, applyCurrent](const Color& c) {
                        state->ctx.fillColor = c;
                        applyCurrent();
                    };
                    optionsBox->AddChild(picker);

                    auto pickBtn = std::make_shared<UltraCanvasButton>(
                        "PixelFXPickPos", 0, 0, 170, 28, "Set fill position");
                    pickBtn->SetIcon(iconsDir + "circle-empty.svg");
                    pickBtn->SetIconSize(14, 14);
                    pickBtn->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
                    pickBtn->onClick = [setPick]() { (*setPick)(true); };
                    optionsBox->AddChild(pickBtn);
                }

                if (fx.params.empty() && fx.choices.empty() && !fx.usesFillColor) {
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

        tree->onNodeSelected = [state, rebuildOptions, applyCurrent, showImage, setStatus, setPick](TreeNode* node) {
            if (!node) return;
            (*setPick)(false);   // leaving the current function ends any active fill picking
            if (node->data.nodeId == "others") {
                state->effect = nullptr;
                state->infoMode = true;
                (*rebuildOptions)();
                if (state->originalValid && showImage(state->original)) {
                    setStatus("Functions without a playground demo — see the panel on the right.");
                }
                return;
            }
            const Effect* fx = FindEffect(node->data.nodeId);
            if (!fx) return;   // root / category nodes just expand
            state->effect = fx;
            state->infoMode = false;
            state->params.clear();
            for (const auto& param : fx->params) state->params.push_back(param.defValue);
            state->ctx.choice = 0;
            (*rebuildOptions)();
            applyCurrent();
        };

        // Flood-fill position picking: armed by the "Set fill position" button and
        // kept active (via setPick) until Esc or a right-click. Each left click
        // re-runs the fill from the new point.
        imageView->onPixelPicked = [state, applyCurrent](float nx, float ny) {
            state->ctx.posX = nx;
            state->ctx.posY = ny;
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
