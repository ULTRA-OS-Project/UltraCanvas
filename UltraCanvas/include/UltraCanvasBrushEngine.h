// include/UltraCanvasBrushEngine.h
// The painting primitives of a bitmap editor, working on UCRasterLayer
// pixels and honouring a UCRasterSelection:
//
//   UCBrushStroke  — an interactive stroke: dabs of a round or square brush
//                    (size, hardness, opacity, flow, spacing) laid along the
//                    pointer path with sub-pixel interpolation, in paint /
//                    erase / clone / smudge modes. Opacity caps the whole
//                    stroke (dabs do not stack past it), flow is the amount
//                    each dab deposits — the Photoshop convention.
//   RasterPaint    — one-shot operations: anti-aliased lines, rectangles and
//                    ellipses (outline and fill), flood fill with tolerance,
//                    magic-wand mask, linear / radial gradients, stamping a
//                    coverage mask in a colour (text), eyedropper sampling.
//
// None of this needs libvips; PixelFX is for whole-image filters, this is
// for the pixels under the cursor. Everything is clipped to the layer and
// scaled by the selection's coverage, so callers never test either.
// Version: 1.0.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasRasterLayer.h"
#include "UltraCanvasRasterSelection.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace UltraCanvas {

// ===== BRUSH SETTINGS =====
enum class BrushShape { Round = 0, Square };

struct UCBrushSettings {
    float      size      = 12.0f;   // diameter in pixels (>= 1)
    float      hardness  = 0.8f;    // 0 = fully soft edge, 1 = hard edge
    float      opacity   = 1.0f;    // 0..1, cap for the whole stroke
    float      flow      = 1.0f;    // 0..1, per-dab deposit
    float      spacing   = 0.15f;   // distance between dabs as a fraction of size
    bool       antialias = true;    // false: pixel-hard edges (the pencil)
    BrushShape shape     = BrushShape::Round;
    // Use pointer pressure (tablets) to scale size / opacity.
    bool pressureSize    = false;
    bool pressureOpacity = false;
};

enum class BrushMode {
    Paint = 0,   // deposit colour (blend mode applies)
    Erase,       // reduce alpha
    Clone,       // copy pixels from an offset source
    Smudge,      // drag colour along
    Dodge,       // lighten (photographic dodge)
    Burn         // darken (photographic burn)
};

// ===== A STROKE =====
class UCBrushStroke {
public:
    UCBrushStroke() = default;

    // Start painting on `layer`. The engine keeps a copy of the layer for the
    // stroke's duration ("before"): the stroke is always re-composited from
    // it, which is what keeps overlapping dabs within `opacity`. `selection`
    // may be null (everything selected).
    void Begin(std::shared_ptr<UCRasterLayer> layer, const UCRasterSelection* selection,
               const UCBrushSettings& settings, BrushMode mode, const RasterPixel& colour,
               RasterBlendMode blend = RasterBlendMode::Normal);
    // Clone mode: the source offset (source = target + offset) and the layer
    // sampled (may be the same layer; the "before" copy is used then).
    void SetCloneSource(std::shared_ptr<UCRasterLayer> sourceLayer, int offsetX, int offsetY);
    // Add a point in image coordinates (sub-pixel allowed). Dabs are laid
    // from the previous point at the spacing interval; the first point is
    // always a dab. Returns the rectangle of pixels changed by this call
    // (empty when none), already clipped to the layer.
    Rect2Di AddPoint(float x, float y, float pressure = 1.0f);
    // Finish: returns the union rectangle of everything the stroke touched.
    Rect2Di End();
    bool IsActive() const { return active; }
    // Union of every dab so far (what the undo snapshot must cover).
    Rect2Di GetDirtyBounds() const { return dirtyBounds; }
    // The layer as it was at Begin() — valid until End(). Hand it to
    // UCRasterDocument::RecordEdit() together with GetDirtyBounds().
    std::shared_ptr<const UCRasterLayer> GetBefore() const { return before; }
    // Radius in pixels the stroke can reach (for pre-sizing the undo rect).
    float GetMaxRadius() const { return settings.size * 0.5f + 2.0f; }

private:
    Rect2Di Dab(float cx, float cy, float pressure);
    void Recomposite(const Rect2Di& r);
    void ApplySmudge(float cx, float cy, float radius);

    bool active = false;
    std::shared_ptr<UCRasterLayer> layer;
    std::shared_ptr<UCRasterLayer> before;      // layer at Begin()
    std::shared_ptr<UCRasterLayer> cloneSource; // Clone mode
    int cloneDx = 0, cloneDy = 0;
    const UCRasterSelection* selection = nullptr;
    UCBrushSettings settings;
    BrushMode mode = BrushMode::Paint;
    RasterPixel colour;
    RasterBlendMode blend = RasterBlendMode::Normal;
    // Accumulated coverage of the stroke, 0..255 per pixel (lazily sized).
    std::vector<uint8_t> strokeMask;
    Rect2Di dirtyBounds;
    bool hasLast = false;
    float lastX = 0, lastY = 0;
    float distanceCarry = 0;   // distance since the last dab
    // Smudge carries a small colour buffer between dabs.
    std::vector<float> smudgeBuffer;
    int smudgeSize = 0;
    bool smudgeLoaded = false;
};

// ===== ONE-SHOT PAINT OPERATIONS =====
namespace RasterPaint {

    struct ShapeStyle {
        RasterPixel stroke      = RasterPixel(0, 0, 0, 255);
        RasterPixel fill        = RasterPixel(0, 0, 0, 0);
        float       strokeWidth = 1.0f;     // 0 = no outline
        bool        antialias   = true;
        RasterBlendMode blend   = RasterBlendMode::Normal;
    };

    // A straight line with round caps. Returns the changed rectangle.
    Rect2Di DrawLine(UCRasterLayer& layer, const UCRasterSelection* sel,
                     Point2Df a, Point2Df b, const ShapeStyle& style);
    Rect2Di DrawRectangle(UCRasterLayer& layer, const UCRasterSelection* sel,
                          const Rect2Df& r, const ShapeStyle& style);
    Rect2Di DrawEllipse(UCRasterLayer& layer, const UCRasterSelection* sel,
                        const Rect2Df& bounds, const ShapeStyle& style);
    Rect2Di DrawPolygon(UCRasterLayer& layer, const UCRasterSelection* sel,
                        const std::vector<Point2Df>& points, bool closed, const ShapeStyle& style);

    // Coverage-based fill: `inside(x, y)` for sub-pixel sample positions;
    // the bounding box is `bbox`. 4x4 supersampling when `antialias`.
    Rect2Di FillCoverage(UCRasterLayer& layer, const UCRasterSelection* sel, const Rect2Di& bbox,
                         const std::function<bool(float, float)>& inside,
                         const RasterPixel& colour, bool antialias, RasterBlendMode blend);

    // Stamp a width*height coverage map (255 = full) at (x, y) in `colour`.
    Rect2Di StampMask(UCRasterLayer& layer, const UCRasterSelection* sel,
                      const uint8_t* mask, int maskWidth, int maskHeight, int x, int y,
                      const RasterPixel& colour, float opacity = 1.0f,
                      RasterBlendMode blend = RasterBlendMode::Normal);

    // Flood fill from (x, y): `tolerance` 0..255 is the mean per-channel RGBA
    // distance a pixel may have from the seed; `contiguous` false fills every
    // matching pixel in the layer. `sampleMerged` lets the caller pass a
    // different layer to read colours from (the composite). Returns the
    // changed rectangle.
    Rect2Di FloodFill(UCRasterLayer& layer, const UCRasterSelection* sel, int x, int y,
                      const RasterPixel& colour, int tolerance, bool contiguous,
                      const UCRasterLayer* sampleLayer = nullptr,
                      RasterBlendMode blend = RasterBlendMode::Normal, float opacity = 1.0f);

    // Magic wand: the same region test as FloodFill, returned as a
    // canvas-sized coverage map for UCRasterSelection::SetMask().
    std::vector<uint8_t> MagicWandMask(const UCRasterLayer& sample, int x, int y,
                                       int tolerance, bool contiguous);

    enum class GradientKind { Linear = 0, Radial, Reflected };
    // A two-colour gradient from `a` to `b` across the selection / layer.
    Rect2Di FillGradient(UCRasterLayer& layer, const UCRasterSelection* sel,
                         Point2Df a, Point2Df b, const RasterPixel& colourA, const RasterPixel& colourB,
                         GradientKind kind, float opacity = 1.0f,
                         RasterBlendMode blend = RasterBlendMode::Normal);

    // Eyedropper: the pixel at (x, y), or the average of a (2r+1)^2 block.
    RasterPixel SampleColour(const UCRasterLayer& layer, int x, int y, int radius = 0);

    // Distance between two pixels as FloodFill measures it (0..255).
    int PixelDistance(const RasterPixel& a, const RasterPixel& b);

} // namespace RasterPaint

} // namespace UltraCanvas
