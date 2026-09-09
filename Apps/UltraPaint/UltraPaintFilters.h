// Apps/UltraPaint/UltraPaintFilters.h
// UltraPaint's catalogue of adjustments and filters, every one a PixelFX
// operation with a small parameter list, plus the dialog that edits the
// parameters with a live preview on the canvas. The catalogue is data:
// the Adjust and Filter menus are generated from it, so adding a filter is
// one entry here.
// Version: 1.0.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasWindow.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasButton.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

#ifdef HAS_LIBVIPS
#include "PixelFX/PixelFX.h"
#endif

namespace UltraCanvas {

struct PaintFilterParam {
    std::string name;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float value = 0.0f;
    float step = 0.01f;
    bool integer = false;
};

struct PaintFilter {
    std::string id;         // stable key
    std::string name;       // menu label
    std::string category;   // "Adjust", "Blur", "Sharpen", "Edges", "Noise", "Stylize", "Morphology"
    std::vector<PaintFilterParam> params;
#ifdef HAS_LIBVIPS
    // The operation: a 4-band uchar sRGB image in, same size out.
    std::function<PixelFX::PFXImage(const PixelFX::PFXImage&, const std::vector<float>&)> apply;
#endif
    bool HasParams() const { return !params.empty(); }
};

// Every filter in menu order; categories group them.
const std::vector<PaintFilter>& PaintFilterCatalogue();
const PaintFilter* FindPaintFilter(const std::string& id);
std::vector<std::string> PaintFilterCategories();

#ifdef HAS_LIBVIPS
// PixelFX helpers shared by the catalogue and the window's own adjustments:
// run `fn` on the RGB bands and keep the alpha band as it was.
PixelFX::PFXImage PaintFilterOnRGB(const PixelFX::PFXImage& rgba,
                                   const std::function<PixelFX::PFXImage(const PixelFX::PFXImage&)>& fn);
// Run `fn` on the premultiplied image (blurs and convolutions, so
// transparent pixels do not bleed their colour).
PixelFX::PFXImage PaintFilterPremultiplied(const PixelFX::PFXImage& rgba,
                                           const std::function<PixelFX::PFXImage(const PixelFX::PFXImage&)>& fn);
// Apply a 256-entry lookup table to the RGB bands.
PixelFX::PFXImage PaintFilterLut(const PixelFX::PFXImage& rgba, const std::vector<uint8_t>& lut);
#endif

// ===== PARAMETER DIALOG =====
// One slider per parameter, a Preview toggle and OK / Cancel. The host
// previews by re-running the filter on every change (onPreview) and commits
// on OK (onAccept). Closing the window cancels.
class UltraPaintFilterDialog : public UltraCanvasWindow {
public:
    explicit UltraPaintFilterDialog(const PaintFilter& filter);
    ~UltraPaintFilterDialog() override = default;

    std::vector<float> GetValues() const { return values; }
    bool IsPreviewEnabled() const { return previewEnabled; }

    std::function<void(const std::vector<float>&)> onPreview;   // live, while Preview is ticked
    std::function<void(const std::vector<float>&)> onAccept;
    std::function<void()> onCancel;

private:
    void BuildLayout(const PaintFilter& filter);
    void EmitPreview();

    std::vector<float> values;
    std::vector<float> defaults;
    bool previewEnabled = true;
    bool accepted = false;
    std::shared_ptr<UltraCanvasContainer> body;
    std::shared_ptr<UltraCanvasCheckbox> previewBox;
    std::shared_ptr<UltraCanvasButton> okButton, cancelButton, resetButton;
};

} // namespace UltraCanvas
