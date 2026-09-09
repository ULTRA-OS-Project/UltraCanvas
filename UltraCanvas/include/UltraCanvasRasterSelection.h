// include/UltraCanvasRasterSelection.h
// The selection of a raster document: a soft 8-bit coverage mask the size
// of the canvas (255 = fully selected), plus the shape operations a bitmap
// editor's selection tools produce (rectangle, ellipse, polygon / lasso,
// an arbitrary mask from the magic wand) and the set algebra between them
// (replace, add, subtract, intersect). Every painting and filter operation
// consults it through Coverage(): a pixel outside the selection is left
// alone, a feathered edge is affected proportionally.
// Version: 1.0.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"

#include <cstdint>
#include <vector>

namespace UltraCanvas {

// How a new shape combines with the existing selection.
enum class RasterSelectionMode {
    Replace = 0,   // the shape becomes the selection
    Add,           // union (Shift)
    Subtract,      // difference (Alt)
    Intersect      // intersection (Shift+Alt)
};

class UCRasterSelection {
public:
    UCRasterSelection() = default;
    UCRasterSelection(int width, int height);

    // ===== GEOMETRY =====
    int  GetWidth()  const { return width; }
    int  GetHeight() const { return height; }
    // Re-shape the mask to a new canvas size; the selection is dropped.
    void Resize(int width, int height);

    // ===== STATE =====
    // True while any pixel is selected. An empty (inactive) selection means
    // "everything": tools and filters affect the whole layer.
    bool IsActive() const { return active; }
    // Bounding rectangle of the selected pixels; the full canvas when the
    // selection is inactive.
    Rect2Di GetBounds() const;
    // Coverage of one pixel: 0..255; 255 everywhere when inactive.
    uint8_t Coverage(int x, int y) const {
        if (!active) return 255;
        if (x < 0 || y < 0 || x >= width || y >= height) return 0;
        return mask[static_cast<size_t>(y) * width + x];
    }
    bool Contains(int x, int y) const { return Coverage(x, y) > 0; }
    // Raw mask, width*height bytes (valid whether or not the selection is
    // active; inactive means every byte is 0).
    const std::vector<uint8_t>& GetMask() const { return mask; }
    // Bumps on every change; lets a view cache its outline per version.
    uint64_t GetVersion() const { return version; }

    // ===== SHAPES =====
    void SelectAll();
    void SelectNone();
    void Invert();
    void SetRectangle(const Rect2Di& r, RasterSelectionMode mode = RasterSelectionMode::Replace);
    void SetEllipse(const Rect2Di& bounds, RasterSelectionMode mode = RasterSelectionMode::Replace,
                    bool antialias = true);
    // Closed polygon in image coordinates (the lasso), even-odd fill.
    void SetPolygon(const std::vector<Point2Df>& points,
                    RasterSelectionMode mode = RasterSelectionMode::Replace, bool antialias = true);
    // An arbitrary coverage map the size of the canvas (the magic wand).
    void SetMask(const std::vector<uint8_t>& coverage,
                 RasterSelectionMode mode = RasterSelectionMode::Replace);
    // Replace the whole selection from another (same size) selection.
    void Assign(const UCRasterSelection& other);

    // ===== MODIFIERS =====
    // Soften the edge: box-blur of the mask with `radius` pixels.
    void Feather(int radius);
    // Grow / shrink by `pixels` (morphological dilate / erode on the mask).
    void Grow(int pixels);
    void Shrink(int pixels);
    // Move the whole mask by (dx, dy); pixels shifted out are lost.
    void Translate(int dx, int dy);

    // ===== OUTLINE =====
    // Unit-length edge segments between selected and unselected pixels, in
    // image coordinates, for marching-ants display. Each entry is (x0, y0,
    // x1, y1) of one horizontal or vertical pixel edge run. Recomputed when
    // the version changed since the last call.
    struct OutlineSegment { int x0, y0, x1, y1; };
    const std::vector<OutlineSegment>& GetOutline() const;

private:
    void Combine(const std::vector<uint8_t>& shape, RasterSelectionMode mode);
    void Touch();
    void UpdateActive();

    int width = 0;
    int height = 0;
    bool active = false;
    std::vector<uint8_t> mask;
    uint64_t version = 1;
    mutable uint64_t outlineVersion = 0;
    mutable std::vector<OutlineSegment> outline;
};

} // namespace UltraCanvas
