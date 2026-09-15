// Apps/ArtCreator/ArtCreatorTools.h
// The tools of ArtCreator: one class per tool, each turning the vector
// canvas's pointer events (document coordinates, raw and snapped) into
// edits of the VectorDocument through VectorEdit inside a VectorHistory
// edit, plus the option widgets it shows in the Tool Options panel and the
// preview it draws over the canvas.
//
// The tools own no document state; ArtToolOptions is the shared,
// user-editable state (line width, shape sides, text font, ...) that
// survives tool switches, and ArtToolContext is what the window lets a
// tool reach.
// Version: 1.0.0
// Last Modified: 2026-09-15
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasVectorCanvas.h"
#include "UltraCanvasBezierPath.h"
#include "DataFormats/UltraCanvasVectorEdit.h"
#include "UltraCanvasContainer.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

enum class ArtToolId {
    Selector = 0, ShapeEditor, Pen, Freehand, Line, Rectangle, Ellipse, QuickShape,
    Text, Fill, Transparency, Zoom, Push,
    Count
};

// ===== USER-EDITABLE TOOL STATE =====
struct ArtToolOptions {
    bool  shapeFill = true;
    bool  shapeStroke = true;
    float strokeWidth = 2.0f;
    float cornerRadius = 0.0f;         // rectangles
    int   quickShapeSides = 5;
    bool  quickShapeStar = true;
    float quickShapeInner = 0.5f;      // star inner radius as a fraction
    float freehandSmoothing = 2.0f;    // simplification tolerance in screen pixels
    int   fillKind = 1;                // 0 flat, 1 linear, 2 radial
    float transparency = 0.0f;         // 0..100 %
    std::string textFont = "Sans";
    int   textSize = 24;
    bool  textBold = false;
};

// ===== WHAT A TOOL CAN REACH =====
struct ArtToolContext {
    std::function<std::shared_ptr<VectorStorage::VectorDocument>()> getDocument;
    std::function<std::shared_ptr<VectorStorage::VectorLayer>()> activeLayer;
    VectorEdit::VectorSelection* selection = nullptr;
    VectorEdit::VectorHistory* history = nullptr;
    UltraCanvasVectorCanvas* canvas = nullptr;
    ArtToolOptions* options = nullptr;
    std::function<Color()> fillColor;      // the colour picker's foreground
    std::function<Color()> lineColor;      // its background
    std::function<void(const std::string&)> setStatus;
    // The text tool asks the window to show its dialog for a document point.
    std::function<void(double, double)> requestText;
    // A tool wants another tool (the selector hands a double-clicked path
    // to the shape editor).
    std::function<void(ArtToolId)> selectTool;
    std::function<void()> refreshOptions;
};

class ArtTool {
public:
    ArtTool(ArtToolId toolId, std::string toolName, std::string icon, char key, std::string toolHint)
        : id(toolId), name(std::move(toolName)), iconFile(std::move(icon)), shortcutKey(key), hint(std::move(toolHint)) {}
    virtual ~ArtTool() = default;

    virtual void Activate(ArtToolContext&) {}
    virtual void Deactivate(ArtToolContext&) {}
    virtual void OnPress(ArtToolContext&, const VectorPointerEvent&) {}
    virtual void OnDrag(ArtToolContext&, const VectorPointerEvent&) {}
    virtual void OnRelease(ArtToolContext&, const VectorPointerEvent&) {}
    virtual void OnHover(ArtToolContext&, const VectorPointerEvent&) {}
    virtual void OnDoubleClick(ArtToolContext&, const VectorPointerEvent&) {}
    virtual bool OnKey(ArtToolContext&, const UCEvent&) { return false; }
    virtual void DrawOverlay(ArtToolContext&, IRenderContext*, const VectorViewTransform&) {}
    // Add option widgets to `panel` (a flex column); `changed` runs after
    // any option changes.
    virtual void BuildOptions(ArtToolContext&, UltraCanvasContainer& panel, const std::function<void()>& changed) {
        (void)panel; (void)changed;
    }
    virtual UCMouseCursor Cursor() const { return UCMouseCursor::Cross; }
    // The selection changed while this tool is active (panels, the shape
    // editor's node set).
    virtual void OnSelectionChanged(ArtToolContext&) {}

    const ArtToolId id;
    const std::string name;
    const std::string iconFile;     // relative to media/icons/artcreator/
    const char shortcutKey;         // single letter, 0 for none
    const std::string hint;         // status-bar text while active
};

// Every tool in palette order.
std::vector<std::unique_ptr<ArtTool>> CreateArtTools();

// ===== SHARED HELPERS =====
namespace ArtToolHelpers {
    // Applies the options' fill / line to a new element: the fill colour,
    // or no fill; the line colour and width, or no line.
    void ApplyNewShapeStyle(VectorStorage::VectorElement& element, const ArtToolContext& ctx);
    // Adds a new element to the active layer inside a history edit, gives
    // it an Id and selects it.
    void AddNewElement(ArtToolContext& ctx, const std::string& label, const std::shared_ptr<VectorStorage::VectorElement>& element);
    // The rectangle between two points, optionally square / from the centre.
    Rect2Dd DragRect(const Point2Dd& from, const Point2Dd& to, bool square, bool fromCentre);
}

// ===== OPTION WIDGET HELPERS =====
namespace ArtOptionWidgets {
    std::string FormatValue(float v, bool integer);
    void AddSliderRow(UltraCanvasContainer& panel, const std::string& id, const std::string& label,
                      float minVal, float maxVal, float value, float step, bool integer,
                      const std::function<void(float)>& onChange, float labelWidth = 0);
    void AddCheckbox(UltraCanvasContainer& panel, const std::string& id, const std::string& label,
                     bool checked, const std::function<void(bool)>& onChange);
    void AddDropdown(UltraCanvasContainer& panel, const std::string& id, const std::string& label,
                     const std::vector<std::string>& items, int selected,
                     const std::function<void(int)>& onChange);
    void AddCaption(UltraCanvasContainer& panel, const std::string& id, const std::string& text);
    void AddButtonRow(UltraCanvasContainer& panel, const std::string& id,
                      const std::vector<std::pair<std::string, std::function<void()>>>& buttons);
}

} // namespace UltraCanvas
