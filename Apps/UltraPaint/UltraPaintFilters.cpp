// Apps/UltraPaint/UltraPaintFilters.cpp
// The filter catalogue (PixelFX operations with parameters) and the
// parameter dialog with live preview.
// Version: 1.0.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework

#include "UltraPaintFilters.h"
#include "UltraPaintTools.h"    // PaintOptionWidgets::FormatValue
#include "UltraPaintDialogs.h"  // UltraPaintDialogParts::SizeButtonToText
#include "UltraCanvasFormLayout.h"

#include "UltraCanvasLabel.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <random>

namespace UltraCanvas {

// ===========================================================================
// PIXELFX HELPERS
// ===========================================================================

#ifdef HAS_LIBVIPS
namespace FX = PixelFX;

PixelFX::PFXImage PaintFilterOnRGB(const FX::PFXImage& rgba,
                                   const std::function<FX::PFXImage(const FX::PFXImage&)>& fn) {
    FX::PFXImage rgb = FX::Colour::ExtractBand(rgba, 0, 3);
    FX::PFXImage alpha = FX::Colour::ExtractBand(rgba, 3);
    FX::PFXImage out = fn(rgb);
    out = FX::Conversion::CastUchar(out);
    if (out.Bands() == 1) out = FX::Colour::Bandjoin({ out, out, out });
    else if (out.Bands() > 3) out = FX::Colour::ExtractBand(out, 0, 3);
    return FX::Conversion::CastUchar(FX::Colour::Bandjoin(out, alpha));
}

PixelFX::PFXImage PaintFilterPremultiplied(const FX::PFXImage& rgba,
                                           const std::function<FX::PFXImage(const FX::PFXImage&)>& fn) {
    FX::PFXImage pre = FX::Colour::Premultiply(rgba);
    FX::PFXImage out = fn(pre);
    out = FX::Colour::Unpremultiply(out);
    return FX::Conversion::CastUchar(out);
}

PixelFX::PFXImage PaintFilterLut(const FX::PFXImage& rgba, const std::vector<uint8_t>& lut) {
    return PaintFilterOnRGB(rgba, [&](const FX::PFXImage& rgb) {
        return FX::Colour::MapLut(rgb, { lut, lut, lut });
    });
}

namespace {

std::vector<uint8_t> MakeLut(const std::function<double(double)>& f) {
    std::vector<uint8_t> lut(256);
    for (int i = 0; i < 256; ++i)
        lut[static_cast<size_t>(i)] = static_cast<uint8_t>(std::clamp(std::lround(f(i / 255.0) * 255.0), 0L, 255L));
    return lut;
}

// Auto contrast as a photo editor means it: per channel, the darkest and
// lightest fraction of the pixels are allowed off the end of the scale and
// what is left is stretched to fill it.
//
// Stretching between the absolute minimum and maximum instead - which is what
// this used to do - is a no-op on any photograph that contains one fully black
// and one fully white pixel, which is most photographs. That is why the menu
// entry appeared to do nothing at all.
constexpr double kAutoContrastClip = 0.002;   // 0.2% off each end, per channel

FX::PFXImage AutoContrast(const FX::PFXImage& rgba) {
    return PaintFilterOnRGB(rgba, [](const FX::PFXImage& rgb) {
        const int bands = rgb.Bands();
        if (bands < 1) return rgb;
        const std::vector<uint8_t> bytes = FX::Conversion::ToMemory(FX::Conversion::CastUchar(rgb));
        const size_t pixels = bytes.size() / static_cast<size_t>(bands);
        if (pixels == 0) return rgb;

        // One histogram per channel, straight off the pixels: a 256-bin count
        // is cheaper than any second pass through the pipeline, and it is the
        // only thing a percentile needs.
        std::vector<std::array<uint64_t, 256>> histogram(static_cast<size_t>(bands));
        for (auto& h : histogram) h.fill(0);
        for (size_t i = 0; i + static_cast<size_t>(bands) <= bytes.size(); i += static_cast<size_t>(bands))
            for (int b = 0; b < bands; ++b)
                ++histogram[static_cast<size_t>(b)][bytes[i + static_cast<size_t>(b)]];

        const uint64_t clip = static_cast<uint64_t>(static_cast<double>(pixels) * kAutoContrastClip);
        std::vector<std::vector<uint8_t>> tables;
        tables.reserve(static_cast<size_t>(bands));
        for (int b = 0; b < bands; ++b) {
            const auto& h = histogram[static_cast<size_t>(b)];
            int low = 0, high = 255;
            uint64_t seen = 0;
            while (low < 255 && seen + h[static_cast<size_t>(low)] <= clip) { seen += h[static_cast<size_t>(low)]; ++low; }
            seen = 0;
            while (high > low && seen + h[static_cast<size_t>(high)] <= clip) { seen += h[static_cast<size_t>(high)]; --high; }
            // A flat channel (one value everywhere) has nothing to stretch;
            // leave it rather than dividing by its own width.
            const double lo = low, hi = std::max<double>(high, low + 1);
            tables.push_back(MakeLut([lo, hi](double v) { return (v * 255.0 - lo) / (hi - lo); }));
        }
        return FX::Colour::MapLut(rgb, tables);
    });
}

FX::PFXImage Kernel3(const FX::PFXImage& rgba, const std::vector<double>& k, double scale = 1.0, double offset = 0.0) {
    return PaintFilterPremultiplied(rgba, [&](const FX::PFXImage& pre) {
        vips::VImage m = vips::VImage::new_matrix(3, 3, const_cast<double*>(k.data()), 9);
        m.set("scale", scale);
        m.set("offset", offset);
        return FX::PFXImage(pre.conv(m));
    });
}

// Hue rotation + saturation + lightness through HSV.
FX::PFXImage HueSaturation(const FX::PFXImage& rgba, double hueDeg, double sat, double light) {
    return PaintFilterOnRGB(rgba, [&](const FX::PFXImage& rgb) {
        FX::PFXImage hsv = FX::Colour::SrgbToHsv(rgb);
        FX::PFXImage h = FX::Colour::ExtractBand(hsv, 0);
        FX::PFXImage s = FX::Colour::ExtractBand(hsv, 1);
        FX::PFXImage v = FX::Colour::ExtractBand(hsv, 2);
        // libvips HSV hue is 0..255 for 0..360°
        h = FX::PFXImage(h.cast(VIPS_FORMAT_FLOAT).linear(1.0, hueDeg * 255.0 / 360.0));
        h = FX::PFXImage(h.remainder_const({256.0}));
        s = FX::PFXImage(s.cast(VIPS_FORMAT_FLOAT).linear(sat, 0.0));
        v = FX::PFXImage(v.cast(VIPS_FORMAT_FLOAT).linear(1.0, light * 255.0));
        FX::PFXImage joined = FX::Conversion::CastUchar(FX::Colour::Bandjoin({ h, s, v }));
        joined = FX::PFXImage(joined.copy(vips::VImage::option()->set("interpretation", VIPS_INTERPRETATION_HSV)));
        return FX::Colour::HsvToSrgb(joined);
    });
}

} // namespace
#endif // HAS_LIBVIPS

// ===========================================================================
// CATALOGUE
// ===========================================================================

const std::vector<PaintFilter>& PaintFilterCatalogue() {
    static const std::vector<PaintFilter> catalogue = [] {
        std::vector<PaintFilter> c;
        auto add = [&](PaintFilter f) { c.push_back(std::move(f)); };
        using P = PaintFilterParam;

        // ----- ADJUST -----
        add({ "brightness-contrast", "Brightness / Contrast...", "Adjust",
              { P{ "Brightness", -100, 100, 0, 1, true }, P{ "Contrast", -100, 100, 0, 1, true } }
#ifdef HAS_LIBVIPS
              , [](const FX::PFXImage& img, const std::vector<float>& p) {
                  const double b = p[0] / 100.0, ct = 1.0 + p[1] / 100.0;
                  return PaintFilterLut(img, MakeLut([&](double v) { return (v - 0.5) * ct + 0.5 + b; }));
              }
#endif
            });
        add({ "hue-saturation", "Hue / Saturation...", "Adjust",
              { P{ "Hue", -180, 180, 0, 1, true }, P{ "Saturation", 0, 3, 1, 0.05f, false }, P{ "Lightness", -1, 1, 0, 0.05f, false } }
#ifdef HAS_LIBVIPS
              , [](const FX::PFXImage& img, const std::vector<float>& p) { return HueSaturation(img, p[0], p[1], p[2]); }
#endif
            });
        add({ "gamma", "Gamma...", "Adjust", { P{ "Gamma", 0.1f, 5, 1, 0.05f, false } }
#ifdef HAS_LIBVIPS
              , [](const FX::PFXImage& img, const std::vector<float>& p) {
                  return PaintFilterLut(img, MakeLut([&](double v) { return std::pow(v, 1.0 / p[0]); }));
              }
#endif
            });
        add({ "levels", "Levels...", "Adjust",
              { P{ "Input black", 0, 254, 0, 1, true }, P{ "Input white", 1, 255, 255, 1, true },
                P{ "Gamma", 0.1f, 5, 1, 0.05f, false }, P{ "Output black", 0, 255, 0, 1, true }, P{ "Output white", 0, 255, 255, 1, true } }
#ifdef HAS_LIBVIPS
              , [](const FX::PFXImage& img, const std::vector<float>& p) {
                  const double ib = p[0] / 255.0, iw = std::max(p[1] / 255.0, ib + 1.0 / 255.0);
                  const double ob = p[3] / 255.0, ow = p[4] / 255.0;
                  return PaintFilterLut(img, MakeLut([&](double v) {
                      double t = std::clamp((v - ib) / (iw - ib), 0.0, 1.0);
                      t = std::pow(t, 1.0 / p[2]);
                      return ob + (ow - ob) * t;
                  }));
              }
#endif
            });
        add({ "posterize", "Posterize...", "Adjust", { P{ "Levels", 2, 32, 4, 1, true } }
#ifdef HAS_LIBVIPS
              , [](const FX::PFXImage& img, const std::vector<float>& p) {
                  const double n = std::max(2.0, static_cast<double>(std::round(p[0])));
                  return PaintFilterLut(img, MakeLut([&](double v) { return std::floor(v * n) / (n - 1) > 1.0 ? 1.0 : std::floor(v * (n - 0.0001)) / (n - 1); }));
              }
#endif
            });
        add({ "threshold", "Threshold...", "Adjust", { P{ "Level", 0, 255, 128, 1, true } }
#ifdef HAS_LIBVIPS
              , [](const FX::PFXImage& img, const std::vector<float>& p) {
                  return PaintFilterOnRGB(img, [&](const FX::PFXImage& rgb) {
                      FX::PFXImage grey = FX::Colour::Bandmean(rgb);
                      return FX::PFXImage(grey.relational_const(VIPS_OPERATION_RELATIONAL_MORE, {static_cast<double>(p[0])}));
                  });
              }
#endif
            });
        add({ "solarize", "Solarize...", "Adjust", { P{ "Threshold", 0, 255, 128, 1, true } }
#ifdef HAS_LIBVIPS
              , [](const FX::PFXImage& img, const std::vector<float>& p) {
                  const double t = p[0] / 255.0;
                  return PaintFilterLut(img, MakeLut([&](double v) { return v > t ? 1.0 - v : v; }));
              }
#endif
            });
        add({ "invert", "Invert", "Adjust", {}
#ifdef HAS_LIBVIPS
              , [](const FX::PFXImage& img, const std::vector<float>&) {
                  return PaintFilterOnRGB(img, [](const FX::PFXImage& rgb) { return FX::Colour::Invert(rgb); });
              }
#endif
            });
        add({ "desaturate", "Desaturate", "Adjust", {}
#ifdef HAS_LIBVIPS
              , [](const FX::PFXImage& img, const std::vector<float>&) {
                  return PaintFilterOnRGB(img, [](const FX::PFXImage& rgb) { return FX::Colour::ToSrgb(FX::Colour::Grayscale(rgb)); });
              }
#endif
            });
        add({ "sepia", "Sepia", "Adjust", {}
#ifdef HAS_LIBVIPS
              , [](const FX::PFXImage& img, const std::vector<float>&) {
                  return PaintFilterOnRGB(img, [](const FX::PFXImage& rgb) { return FX::Colour::Sepia(rgb); });
              }
#endif
            });
        add({ "equalize", "Equalize", "Adjust", {}
#ifdef HAS_LIBVIPS
              , [](const FX::PFXImage& img, const std::vector<float>&) {
                  return PaintFilterOnRGB(img, [](const FX::PFXImage& rgb) { return FX::Colour::HistEqual(rgb); });
              }
#endif
            });
        add({ "auto-contrast", "Auto Contrast", "Adjust", {}
#ifdef HAS_LIBVIPS
              , [](const FX::PFXImage& img, const std::vector<float>&) { return AutoContrast(img); }
#endif
            });

        // ----- BLUR -----
        add({ "gaussian-blur", "Gaussian Blur...", "Blur", { P{ "Radius", 0.3f, 50, 3, 0.1f, false } }
#ifdef HAS_LIBVIPS
              , [](const FX::PFXImage& img, const std::vector<float>& p) {
                  return PaintFilterPremultiplied(img, [&](const FX::PFXImage& pre) { return FX::Convolution::GaussianBlur(pre, p[0]); });
              }
#endif
            });
        add({ "box-blur", "Box Blur...", "Blur", { P{ "Radius", 1, 30, 2, 1, true } }
#ifdef HAS_LIBVIPS
              , [](const FX::PFXImage& img, const std::vector<float>& p) {
                  return PaintFilterPremultiplied(img, [&](const FX::PFXImage& pre) { return FX::Convolution::BoxBlur(pre, static_cast<int>(p[0])); });
              }
#endif
            });
        add({ "median", "Median (Despeckle)...", "Blur", { P{ "Size", 3, 15, 3, 2, true } }
#ifdef HAS_LIBVIPS
              , [](const FX::PFXImage& img, const std::vector<float>& p) {
                  int n = static_cast<int>(p[0]); if (n % 2 == 0) ++n;
                  return PaintFilterPremultiplied(img, [&](const FX::PFXImage& pre) { return FX::Morphology::Median(pre, n); });
              }
#endif
            });
        add({ "motion-blur", "Motion Blur...", "Blur", { P{ "Length", 2, 60, 10, 1, true }, P{ "Angle", 0, 180, 0, 1, true } }
#ifdef HAS_LIBVIPS
              , [](const FX::PFXImage& img, const std::vector<float>& p) {
                  return PaintFilterPremultiplied(img, [&](const FX::PFXImage& pre) {
                      const int len = std::max(2, static_cast<int>(p[0]));
                      const int size = len | 1;
                      std::vector<double> k(static_cast<size_t>(size) * size, 0.0);
                      const double a = p[1] * 3.14159265358979 / 180.0;
                      const double cx = size / 2, cy = size / 2;
                      for (int i = 0; i < len; ++i) {
                          const double t = i - (len - 1) / 2.0;
                          const int x = static_cast<int>(std::lround(cx + std::cos(a) * t));
                          const int y = static_cast<int>(std::lround(cy - std::sin(a) * t));
                          if (x >= 0 && y >= 0 && x < size && y < size) k[static_cast<size_t>(y) * size + x] = 1.0;
                      }
                      double sum = 0; for (double v : k) sum += v;
                      vips::VImage m = vips::VImage::new_matrix(size, size, k.data(), static_cast<int>(k.size()));
                      m.set("scale", std::max(1.0, sum));
                      return FX::PFXImage(pre.conv(m));
                  });
              }
#endif
            });

        // ----- SHARPEN -----
        add({ "sharpen", "Sharpen...", "Sharpen", { P{ "Amount", 0.1f, 5, 1, 0.1f, false } }
#ifdef HAS_LIBVIPS
              , [](const FX::PFXImage& img, const std::vector<float>& p) {
                  return PaintFilterOnRGB(img, [&](const FX::PFXImage& rgb) { return FX::Convolution::Sharpen(rgb, 0.5 + p[0] * 0.5, 2.0, p[0]); });
              }
#endif
            });
        add({ "unsharp-mask", "Unsharp Mask...", "Sharpen", { P{ "Radius", 0.5f, 20, 2, 0.1f, false }, P{ "Amount", 0.1f, 5, 1, 0.1f, false } }
#ifdef HAS_LIBVIPS
              , [](const FX::PFXImage& img, const std::vector<float>& p) {
                  return PaintFilterOnRGB(img, [&](const FX::PFXImage& rgb) { return FX::Convolution::UnsharpMask(rgb, p[0], p[1]); });
              }
#endif
            });

        // ----- EDGES -----
        add({ "sobel", "Sobel Edges", "Edges", {}
#ifdef HAS_LIBVIPS
              , [](const FX::PFXImage& img, const std::vector<float>&) {
                  return PaintFilterOnRGB(img, [](const FX::PFXImage& rgb) { return FX::Convolution::Sobel(rgb); });
              }
#endif
            });
        add({ "laplacian", "Laplacian Edges", "Edges", {}
#ifdef HAS_LIBVIPS
              , [](const FX::PFXImage& img, const std::vector<float>&) {
                  return PaintFilterOnRGB(img, [](const FX::PFXImage& rgb) { return FX::PFXImage(FX::Convolution::Laplacian(rgb).abs()); });
              }
#endif
            });
        add({ "canny", "Canny Edges...", "Edges", { P{ "Sigma", 0.5f, 5, 1.4f, 0.1f, false } }
#ifdef HAS_LIBVIPS
              , [](const FX::PFXImage& img, const std::vector<float>& p) {
                  return PaintFilterOnRGB(img, [&](const FX::PFXImage& rgb) {
                      return FX::PFXImage(FX::Convolution::Canny(FX::Colour::Bandmean(rgb), p[0]).linear(64.0, 0.0));
                  });
              }
#endif
            });
        add({ "emboss", "Emboss", "Edges", {}
#ifdef HAS_LIBVIPS
              , [](const FX::PFXImage& img, const std::vector<float>&) {
                  return Kernel3(img, { -2, -1, 0, -1, 1, 1, 0, 1, 2 }, 1.0, 0.0);
              }
#endif
            });
        add({ "find-edges", "Find Edges", "Edges", {}
#ifdef HAS_LIBVIPS
              , [](const FX::PFXImage& img, const std::vector<float>&) {
                  return PaintFilterOnRGB(img, [](const FX::PFXImage& rgb) { return FX::Colour::Invert(FX::Convolution::Sobel(rgb)); });
              }
#endif
            });

        // ----- NOISE -----
        add({ "add-noise", "Add Noise...", "Noise", { P{ "Amount", 1, 100, 20, 1, true } }
#ifdef HAS_LIBVIPS
              , [](const FX::PFXImage& img, const std::vector<float>& p) {
                  return PaintFilterOnRGB(img, [&](const FX::PFXImage& rgb) {
                      FX::PFXImage noise = FX::Generate::Gaussnoise(rgb.Width(), rgb.Height(), 0.0, p[0]);
                      return FX::PFXImage(rgb.cast(VIPS_FORMAT_FLOAT).add(noise));
                  });
              }
#endif
            });
        add({ "despeckle", "Despeckle", "Noise", {}
#ifdef HAS_LIBVIPS
              , [](const FX::PFXImage& img, const std::vector<float>&) {
                  return PaintFilterPremultiplied(img, [](const FX::PFXImage& pre) { return FX::Morphology::Median(pre, 3); });
              }
#endif
            });

        // ----- STYLIZE -----
        add({ "pixelate", "Pixelate...", "Stylize", { P{ "Block size", 2, 100, 8, 1, true } }
#ifdef HAS_LIBVIPS
              , [](const FX::PFXImage& img, const std::vector<float>& p) {
                  const int n = std::max(2, static_cast<int>(p[0]));
                  return PaintFilterPremultiplied(img, [&](const FX::PFXImage& pre) {
                      FX::PFXImage small = FX::Resample::Shrink(pre, n, n);
                      FX::PFXImage big = FX::Conversion::Zoom(FX::Conversion::CastUchar(small), n, n);
                      return FX::Conversion::Embed(big, 0, 0, pre.Width(), pre.Height(), FX::Extend::Copy);
                  });
              }
#endif
            });
        add({ "oil-paint", "Oil Paint...", "Stylize", { P{ "Size", 3, 15, 5, 2, true } }
#ifdef HAS_LIBVIPS
              , [](const FX::PFXImage& img, const std::vector<float>& p) {
                  int n = static_cast<int>(p[0]); if (n % 2 == 0) ++n;
                  return PaintFilterPremultiplied(img, [&](const FX::PFXImage& pre) {
                      // rank at the 80th percentile gives the painted, blotchy look
                      return FX::Morphology::Rank(pre, n, n, static_cast<int>(n * n * 0.8));
                  });
              }
#endif
            });
        add({ "erode", "Erode (Shrink)...", "Morphology", { P{ "Size", 3, 15, 3, 2, true } }
#ifdef HAS_LIBVIPS
              , [](const FX::PFXImage& img, const std::vector<float>& p) {
                  int n = static_cast<int>(p[0]); if (n % 2 == 0) ++n;
                  return PaintFilterPremultiplied(img, [&](const FX::PFXImage& pre) { return FX::Morphology::Rank(pre, n, n, 0); });
              }
#endif
            });
        add({ "dilate", "Dilate (Grow)...", "Morphology", { P{ "Size", 3, 15, 3, 2, true } }
#ifdef HAS_LIBVIPS
              , [](const FX::PFXImage& img, const std::vector<float>& p) {
                  int n = static_cast<int>(p[0]); if (n % 2 == 0) ++n;
                  return PaintFilterPremultiplied(img, [&](const FX::PFXImage& pre) { return FX::Morphology::Rank(pre, n, n, n * n - 1); });
              }
#endif
            });
        return c;
    }();
    return catalogue;
}

const PaintFilter* FindPaintFilter(const std::string& id) {
    for (const auto& f : PaintFilterCatalogue()) if (f.id == id) return &f;
    return nullptr;
}

std::vector<std::string> PaintFilterCategories() {
    std::vector<std::string> cats;
    for (const auto& f : PaintFilterCatalogue())
        if (std::find(cats.begin(), cats.end(), f.category) == cats.end()) cats.push_back(f.category);
    return cats;
}

// ===========================================================================
// PARAMETER DIALOG
// ===========================================================================

UltraPaintFilterDialog::UltraPaintFilterDialog(const PaintFilter& filter) : UltraCanvasWindow() {
    for (const auto& p : filter.params) {
        values.push_back(p.value);
        defaults.push_back(p.value);
        integerParam.push_back(p.integer);
    }
    config_.title = filter.name.substr(0, filter.name.find("..."));
    config_.width = 420;
    config_.height = static_cast<int>(96 + 30 * filter.params.size());
    config_.minWidth = 320;
    config_.minHeight = 120;
    config_.deleteOnClose = true;
    // Resizable: the caption column is as wide as the longest parameter name,
    // and a translation that needs more room than this window opened with has
    // to be able to get it.
    config_.resizable = true;
    SetPadding(12);
    BuildLayout(filter);
    onWindowClosed = [this]() { if (!accepted && onCancel) onCancel(); };
}

void UltraPaintFilterDialog::BuildLayout(const PaintFilter& filter) {
    layout.SetFlexColumn().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    // One grid for every parameter, so the sliders all start where the longest
    // parameter name ends instead of behind a caption column hard-coded to a
    // width that fits "Gamma" and cuts "Brightness" in half.
    body = CreateFormGrid("upf-form", 6.0f, 10.0f);
    AddChild(body);

    sliders.clear();
    valueLabels.clear();
    for (size_t i = 0; i < filter.params.size(); ++i) {
        const auto& p = filter.params[i];
        auto cell = CreateFormCellRow("upf-p" + std::to_string(i) + "-row", 8.0f);

        auto slider = CreateHorizontalSlider("upf-p" + std::to_string(i) + "-slider",
                                             0, 0, 120, 24, p.minValue, p.maxValue);
        slider->SetStep(p.step);
        slider->SetValue(p.value);
        slider->SetValueDisplay(SliderValueDisplay::NoDisplay);
        slider->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        cell->AddChild(slider);

        // The number the slider stands at, in a column of its own: fixed width
        // so the sliders all end at the same x whatever the values read.
        auto value = std::make_shared<UltraCanvasLabel>("upf-p" + std::to_string(i) + "-value",
                                                        0, 0, 52, 24, PaintOptionWidgets::FormatValue(p.value, p.integer));
        value->SetAlignment(TextAlignment::Right, VerticalAlignment::Middle);
        value->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        cell->AddChild(value);

        auto* valuePtr = value.get();
        const bool integer = p.integer;
        slider->onValueChanging = [valuePtr, integer](float v) { valuePtr->SetText(PaintOptionWidgets::FormatValue(v, integer)); };
        slider->onValueChanged = [this, i, valuePtr, integer](float v) {
            valuePtr->SetText(PaintOptionWidgets::FormatValue(v, integer));
            values[i] = integer ? std::round(v) : v;
            EmitPreview();
        };

        AddFormRow(body, "upf-p" + std::to_string(i), p.name, cell);
        sliders.push_back(slider);
        valueLabels.push_back(value);
    }

    auto row = std::make_shared<UltraCanvasContainer>("upf-buttons", 0, 0, 0, 34);
    row->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    previewBox = std::make_shared<UltraCanvasCheckbox>("upf-preview", 0, 0, 0, 24, "Preview");
    previewBox->SetChecked(true);
    previewBox->onStateChanged = [this](CheckedState, CheckedState n) {
        previewEnabled = n == CheckedState::Checked;
        if (onPreview) onPreview(previewEnabled ? values : std::vector<float>{});
    };
    previewBox->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    row->AddChild(previewBox);
    row->AddStretchSpacer(1);

    resetButton = std::make_shared<UltraCanvasButton>("upf-reset", 0, 0, 80, 30, "Reset");
    resetButton->onClick = [this]() { ResetValues(); };
    UltraPaintDialogParts::SizeButtonToText(resetButton);
    row->AddChild(resetButton);

    cancelButton = std::make_shared<UltraCanvasButton>("upf-cancel", 0, 0, 80, 30, "Cancel");
    cancelButton->onClick = [this]() { Close(); };
    UltraPaintDialogParts::SizeButtonToText(cancelButton);
    row->AddChild(cancelButton);

    okButton = std::make_shared<UltraCanvasButton>("upf-ok", 0, 0, 80, 30, "OK");
    okButton->SetStyle(ButtonStyles::PrimaryStyle());
    okButton->onClick = [this]() {
        accepted = true;
        if (onAccept) onAccept(values);
        Close();
    };
    UltraPaintDialogParts::SizeButtonToText(okButton);
    row->AddChild(okButton);
    AddChild(row);
}

void UltraPaintFilterDialog::ResetValues() {
    values = defaults;
    for (size_t i = 0; i < sliders.size(); ++i) {
        if (sliders[i]) sliders[i]->SetValue(defaults[i]);
        if (valueLabels[i])
            valueLabels[i]->SetText(PaintOptionWidgets::FormatValue(defaults[i],
                                                i < integerParam.size() && integerParam[i]));
    }
    EmitPreview();
}

void UltraPaintFilterDialog::EmitPreview() {
    if (previewEnabled && onPreview) onPreview(values);
}

} // namespace UltraCanvas
