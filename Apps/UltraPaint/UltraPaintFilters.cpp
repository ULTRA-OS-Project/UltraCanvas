// Apps/UltraPaint/UltraPaintFilters.cpp
// The filter catalogue (PixelFX operations with parameters) and the
// parameter dialog with live preview.
// Version: 1.0.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework

#include "UltraPaintFilters.h"
#include "UltraPaintTools.h"   // PaintOptionWidgets

#include "UltraCanvasLabel.h"

#include <algorithm>
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
              , [](const FX::PFXImage& img, const std::vector<float>&) {
                  return PaintFilterOnRGB(img, [](const FX::PFXImage& rgb) {
                      const FX::Arithmetic::Stats st = FX::Arithmetic::GetStats(rgb);
                      const double lo = st.min, hi = std::max(st.max, st.min + 1.0);
                      return FX::PFXImage(rgb.linear(255.0 / (hi - lo), -lo * 255.0 / (hi - lo)));
                  });
              }
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
    for (const auto& p : filter.params) { values.push_back(p.value); defaults.push_back(p.value); }
    config_.title = filter.name.substr(0, filter.name.find("..."));
    config_.width = 380;
    config_.height = static_cast<int>(120 + 30 * filter.params.size());
    config_.minWidth = 300;
    config_.minHeight = 120;
    config_.deleteOnClose = true;
    config_.resizable = false;
    SetPadding(12);
    BuildLayout(filter);
    onWindowClosed = [this]() { if (!accepted && onCancel) onCancel(); };
}

void UltraPaintFilterDialog::BuildLayout(const PaintFilter& filter) {
    layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    body = std::make_shared<UltraCanvasContainer>("upf-body", 0, 0, 0, static_cast<float>(30 * filter.params.size()));
    body->layout.SetFlexColumn().SetFlexGap(4).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    body->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    for (size_t i = 0; i < filter.params.size(); ++i) {
        const auto& p = filter.params[i];
        PaintOptionWidgets::AddSliderRow(*body, "upf-p" + std::to_string(i), p.name, p.minValue, p.maxValue, p.value,
                                         p.step, p.integer, [this, i](float v) { values[i] = v; EmitPreview(); });
    }
    AddChild(body);

    auto row = std::make_shared<UltraCanvasContainer>("upf-buttons", 0, 0, 0, 32);
    row->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    previewBox = std::make_shared<UltraCanvasCheckbox>("upf-preview", 0, 0, 90, 24, "Preview");
    previewBox->SetChecked(true);
    previewBox->onStateChanged = [this](CheckedState, CheckedState n) {
        previewEnabled = n == CheckedState::Checked;
        if (onPreview) onPreview(previewEnabled ? values : std::vector<float>{});
    };
    previewBox->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    row->AddChild(previewBox);

    resetButton = std::make_shared<UltraCanvasButton>("upf-reset", 0, 0, 70, 28, "Reset");
    resetButton->onClick = [this]() {
        values = defaults;
        // rebuild the sliders with the defaults
        body->ClearChildren();
        const PaintFilter* f = nullptr;
        for (const auto& c : PaintFilterCatalogue()) if (c.params.size() == values.size() && c.name.substr(0, c.name.find("...")) == config_.title) { f = &c; break; }
        if (f) for (size_t i = 0; i < f->params.size(); ++i) {
            const auto& p = f->params[i];
            PaintOptionWidgets::AddSliderRow(*body, "upf-p" + std::to_string(i), p.name, p.minValue, p.maxValue, p.value,
                                             p.step, p.integer, [this, i](float v) { values[i] = v; EmitPreview(); });
        }
        EmitPreview();
    };
    resetButton->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    row->AddChild(resetButton);

    cancelButton = std::make_shared<UltraCanvasButton>("upf-cancel", 0, 0, 80, 28, "Cancel");
    cancelButton->onClick = [this]() { Close(); };
    cancelButton->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    row->AddChild(cancelButton);

    okButton = std::make_shared<UltraCanvasButton>("upf-ok", 0, 0, 80, 28, "OK");
    okButton->onClick = [this]() {
        accepted = true;
        if (onAccept) onAccept(values);
        Close();
    };
    okButton->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    row->AddChild(okButton);
    AddChild(row);
}

void UltraPaintFilterDialog::EmitPreview() {
    if (previewEnabled && onPreview) onPreview(values);
}

} // namespace UltraCanvas
