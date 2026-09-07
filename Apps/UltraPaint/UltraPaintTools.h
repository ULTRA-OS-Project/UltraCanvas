// Apps/UltraPaint/UltraPaintTools.h
// The tools of UltraPaint: one class per tool, each turning the paint
// surface's pointer events (image coordinates) into edits of the raster
// document through the brush engine, plus the option widgets it shows in
// the Tool Options panel and the preview it draws over the canvas.
//
// Tools are stateless between activations except for what their options
// hold; PaintToolOptions is the shared, user-editable state (brush sizes,
// tolerances, shape style, …) that survives tool switches.
// Version: 1.0.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasBrushEngine.h"
#include "UltraCanvasPaintSurface.h"
#include "UltraCanvasRasterDocument.h"
#include "UltraCanvasContainer.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

enum class PaintToolId {
    Move = 0, RectSelect, EllipseSelect, Lasso, MagicWand, Crop, Eyedropper,
    Pencil, Brush, Airbrush, Eraser, Clone, Smudge, Dodge, Burn,
    Fill, Gradient, Line, Rectangle, Ellipse, Text, Zoom, Pan,
    Count
};

// ===== USER-EDITABLE TOOL STATE =====
struct PaintToolOptions {
    UCBrushSettings brush;
    UCBrushSettings pencil;
    UCBrushSettings airbrush;
    UCBrushSettings eraser;
    UCBrushSettings clone;
    UCBrushSettings smudge;
    UCBrushSettings dodgeBurn;

    int   fillTolerance = 32;
    bool  fillContiguous = true;
    bool  fillSampleMerged = false;
    float fillOpacity = 1.0f;

    int   wandTolerance = 32;
    bool  wandContiguous = true;
    bool  wandSampleMerged = true;

    RasterPaint::GradientKind gradientKind = RasterPaint::GradientKind::Linear;
    float gradientOpacity = 1.0f;

    float shapeStrokeWidth = 2.0f;
    bool  shapeStroke = true;
    bool  shapeFill = false;
    bool  shapeAntialias = true;

    bool  selectionAntialias = true;
    int   selectionFeather = 0;

    int   eyedropperRadius = 0;
    bool  eyedropperMerged = true;

    std::string textFont = "Sans";
    int   textSize = 32;
    bool  textBold = false;

    bool  moveSelectionOnly = true;

    PaintToolOptions();
};

// ===== WHAT A TOOL CAN REACH =====
struct PaintToolContext {
    std::function<std::shared_ptr<UCRasterDocument>()> getDocument;
    UltraCanvasPaintSurface* surface = nullptr;
    PaintToolOptions* options = nullptr;
    std::function<RasterPixel()> foreground;
    std::function<RasterPixel()> background;
    std::function<void(const RasterPixel&)> setForeground;
    std::function<void(const RasterPixel&)> setBackground;
    std::function<void(const std::string&)> setStatus;
    // The text tool asks the window to show its dialog at an image point.
    std::function<void(double, double)> requestText;
    // A tool changed something the option panel shows (e.g. brush size by
    // bracket keys): rebuild it.
    std::function<void()> refreshOptions;
};

class PaintTool {
public:
    PaintTool(PaintToolId toolId, std::string toolName, std::string icon, char key, std::string toolHint)
        : id(toolId), name(std::move(toolName)), iconFile(std::move(icon)), shortcutKey(key), hint(std::move(toolHint)) {}
    virtual ~PaintTool() = default;

    virtual void Activate(PaintToolContext&) {}
    virtual void Deactivate(PaintToolContext&) {}
    virtual void OnPress(PaintToolContext&, const PaintPointerEvent&) {}
    virtual void OnDrag(PaintToolContext&, const PaintPointerEvent&) {}
    virtual void OnRelease(PaintToolContext&, const PaintPointerEvent&) {}
    virtual void OnHover(PaintToolContext&, const PaintPointerEvent&) {}
    virtual void OnDoubleClick(PaintToolContext&, const PaintPointerEvent&) {}
    virtual bool OnKey(PaintToolContext&, const UCEvent&) { return false; }
    virtual void DrawOverlay(PaintToolContext&, IRenderContext*, const PaintViewTransform&) {}
    // Add option widgets to `panel` (a flex column). `changed` is called
    // after any option changes so the host can refresh cursors.
    virtual void BuildOptions(PaintToolContext&, UltraCanvasContainer& panel, const std::function<void()>& changed) {
        (void)panel; (void)changed;
    }
    virtual UCMouseCursor Cursor() const { return UCMouseCursor::Cross; }
    // Radius of the brush outline to draw at the pointer (image pixels), 0 = none.
    virtual double CursorRadius(PaintToolContext&) const { return 0.0; }
    virtual bool CursorSquare(PaintToolContext&) const { return false; }

    const PaintToolId id;
    const std::string name;
    const std::string iconFile;     // relative to media/icons/ultrapaint/
    const char shortcutKey;         // single letter, 0 for none
    const std::string hint;         // status-bar text while active
};

// Every tool in palette order.
std::vector<std::unique_ptr<PaintTool>> CreatePaintTools();

// ===== OPTION WIDGET HELPERS (shared by the tools and the window) =====
namespace PaintOptionWidgets {
    // A "Label  [slider]  value" row; `onChange` gets the new value.
    void AddSliderRow(UltraCanvasContainer& panel, const std::string& id, const std::string& label,
                      float minVal, float maxVal, float value, float step, bool integer,
                      const std::function<void(float)>& onChange);
    void AddCheckbox(UltraCanvasContainer& panel, const std::string& id, const std::string& label,
                     bool checked, const std::function<void(bool)>& onChange);
    void AddDropdown(UltraCanvasContainer& panel, const std::string& id, const std::string& label,
                     const std::vector<std::string>& items, int selected,
                     const std::function<void(int)>& onChange);
    void AddCaption(UltraCanvasContainer& panel, const std::string& id, const std::string& text);
    // The rows every brush-like tool shares.
    void AddBrushOptions(UltraCanvasContainer& panel, const std::string& idPrefix, UCBrushSettings& s,
                         bool showHardness, bool showFlow, const std::function<void()>& changed);
}

} // namespace UltraCanvas
