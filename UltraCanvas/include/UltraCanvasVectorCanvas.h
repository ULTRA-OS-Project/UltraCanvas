// include/UltraCanvasVectorCanvas.h
// The editable drawing view of a vector editor: shows a
// VectorStorage::VectorDocument on a pasteboard at any zoom, with the page,
// a grid, rulers, guides, snapping, and the selection's handles, and hands
// pointer events to the active tool in DOCUMENT coordinates (raw and
// snapped). It owns no tool logic and never edits the document: the host
// installs the tool callbacks, draws its rubber bands through
// onDrawOverlay, and edits through VectorEdit with a VectorHistory.
//
// The raster twin is UltraCanvasPaintSurface; the read-only viewer is
// UltraCanvasVectorElement. This one edits.
// Version: 1.1.0 - Rotate mode: one object's own turned frame (SelectionFrame), a rotation
//                  handle from the centre (VectorRotationHandleStyle Dial / Stem), the frame
//                  hidden while turning (SetDraggedHandle), onHandleModeChanged and
//                  onRotationCenterChanged; a moved centre stays while the same objects stay
//                  selected
// Version: 1.0.0
// Last Modified: 2026-10-28
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "DataFormats/UltraCanvasVectorStorage.h"
#include "DataFormats/UltraCanvasVectorRenderer.h"
#include "DataFormats/UltraCanvasVectorEdit.h"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace UltraCanvas {

// ===== VIEW TRANSFORM =====
// Document <-> view mapping: view = origin + doc * zoom (view coordinates
// are local to the element, rulers included).
struct VectorViewTransform {
    double zoom = 1.0;
    double originX = 0.0;
    double originY = 0.0;
    Point2Dd DocToView(const Point2Dd& p) const { return Point2Dd(originX + p.x * zoom, originY + p.y * zoom); }
    Point2Dd ViewToDoc(const Point2Dd& p) const { return Point2Dd((p.x - originX) / zoom, (p.y - originY) / zoom); }
    Rect2Dd DocToView(const Rect2Dd& r) const {
        return Rect2Dd(originX + r.x * zoom, originY + r.y * zoom, r.width * zoom, r.height * zoom);
    }
    Rect2Dd ViewToDoc(const Rect2Dd& r) const {
        return Rect2Dd((r.x - originX) / zoom, (r.y - originY) / zoom, r.width / zoom, r.height / zoom);
    }
    double ViewToDocLength(double viewPixels) const { return viewPixels / zoom; }
    double DocToViewLength(double docUnits) const { return docUnits * zoom; }
};

// ===== WORKSPACE =====
struct VectorGridSpec {
    bool visible = false;
    double spacing = 10.0;        // document units between grid lines
    int subdivisions = 1;         // minor lines per major spacing (1 = none)
    Color majorColor = Color(0, 0, 0, 40);
    Color minorColor = Color(0, 0, 0, 16);
    double minPixelSpacing = 6.0; // below this the minor grid is not drawn
};

struct VectorGuide {
    bool horizontal = false;      // a horizontal guide spans x; its position is a y
    double position = 0.0;        // document units
};

struct VectorSnapOptions {
    bool toGrid = false;
    bool toGuides = true;
    bool toObjects = false;       // bounding-box edges and centres of layer-level elements
    bool toPage = false;          // page edges and centre
    double radiusPixels = 6.0;    // how close, on screen, before a snap takes hold
};

struct VectorSnapResult {
    enum class Kind { NoSnap, Grid, Guide, Object, Page };
    Kind x = Kind::NoSnap;
    Kind y = Kind::NoSnap;
    bool Snapped() const { return x != Kind::NoSnap || y != Kind::NoSnap; }
};

// ===== A POINTER EVENT IN DOCUMENT SPACE =====
struct VectorPointerEvent {
    Point2Dd doc;                 // where the pointer is, in document units
    Point2Dd snapped;             // the same, after the canvas's snapping
    VectorSnapResult snap;
    Point2Di view;                // element-local pixels
    UCMouseButton button = UCMouseButton::NoneButton;
    bool ctrl = false, shift = false, alt = false;
    float pressure = 1.0f;
    bool insidePage = false;
};

// The selection's handles, as the selector tool asks about them.
enum class VectorHandle {
    NoHandle,
    TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left,   // scale (or rotate / skew in rotate mode)
    Center,                                                                  // the rotation centre
    Rotation,                                                                // the rotation handle (rotate mode)
    Body                                                                     // inside the bounds, on no handle
};
// Xara's two-state selector: one click shows scale handles, a second
// click on the selection shows rotate (corners) and skew (edges) handles.
enum class VectorHandleMode { Scale, Rotate };
// How rotate mode shows the rotation handle, which starts at the rotation
// centre and points the way the selection is turned (up for 0 degrees):
// Dial - a ring of degree ticks around the centre, with a wedge from 0 to
// the angle and the knob on the ring; Stem - a line from the centre to a
// knob just beyond the frame.
enum class VectorRotationHandleStyle { Dial, Stem };

class UltraCanvasVectorCanvas : public UltraCanvasUIElement {
public:
    // Sizeless: the parent's layout (flex grow / grid cell / stretch) decides
    // the canvas's box. This is what a full-window editor wants.
    explicit UltraCanvasVectorCanvas(const std::string& elemId = "VectorCanvas");
    // Fixed box at (x, y): for absolutely-placed pages that position their
    // widgets themselves. SetBounds() alone does NOT do this - it moves
    // finalBounds without stamping the CSS size or position, so the next
    // layout pass collapses the canvas to a zero-height in-flow child and
    // the drawing area disappears.
    UltraCanvasVectorCanvas(const std::string& elemId, float x, float y, float w, float h);
    ~UltraCanvasVectorCanvas() override;

    // ===== DOCUMENT AND SELECTION =====
    void SetDocument(std::shared_ptr<VectorStorage::VectorDocument> doc);
    std::shared_ptr<VectorStorage::VectorDocument> GetDocument() const { return document; }
    // The selection the canvas draws handles for; the host and its tools
    // edit it. A canvas makes its own if none is given.
    void SetSelection(std::shared_ptr<VectorEdit::VectorSelection> sel);
    std::shared_ptr<VectorEdit::VectorSelection> GetSelection() const { return selection; }

    // ===== VIEW =====
    const VectorViewTransform& GetView() const { return view; }
    double GetZoom() const { return view.zoom; }
    void SetZoom(double zoom);                               // about the centre of the canvas area
    void SetZoomAt(double zoom, const Point2Di& viewPoint);  // about a view point (the wheel)
    void ZoomIn()  { ZoomStep(1); }
    void ZoomOut() { ZoomStep(-1); }
    void ZoomStep(int direction);                            // through the preset ladder
    void ZoomToPage();
    void ZoomToDrawing();
    void ZoomToSelection();
    void ZoomToActual() { SetZoom(1.0); }
    // Fits a document rectangle into the canvas area with a margin in pixels.
    void ZoomToRect(const Rect2Dd& docRect, double marginPixels = 24.0);
    void CenterOn(const Point2Dd& docPoint);
    void PanBy(double dx, double dy);                        // view pixels
    Point2Dd ViewToDoc(const Point2Di& p) const { return view.ViewToDoc(Point2Dd(p.x, p.y)); }
    Point2Dd DocToView(const Point2Dd& p) const { return view.DocToView(p); }
    double PixelsToDoc(double pixels) const { return view.ViewToDocLength(pixels); }
    // The document rectangle the canvas area currently shows.
    Rect2Dd VisibleDocRect() const;
    // The canvas area (the element minus the rulers), element-local.
    Rect2Dd CanvasArea() const;

    // ===== WORKSPACE =====
    void SetShowRulers(bool show);
    bool GetShowRulers() const { return showRulers; }
    // Ruler numbers in this many points per unit (1 = points, 72 = inches,
    // 72/25.4 = millimetres); the unit's symbol labels the corner.
    void SetRulerUnit(double pointsPerUnit, const std::string& symbol);
    void SetGrid(const VectorGridSpec& spec);
    const VectorGridSpec& GetGrid() const { return grid; }
    void SetShowGuides(bool show);
    bool GetShowGuides() const { return showGuides; }
    const std::vector<VectorGuide>& GetGuides() const { return guides; }
    void SetGuides(const std::vector<VectorGuide>& list);
    int AddGuide(const VectorGuide& guide);                  // returns its index
    void RemoveGuide(int index);
    void ClearGuides();
    // Guides can be dragged out of the rulers with the mouse and dragged
    // back onto a ruler to delete them; off by default for tools that own
    // the whole drag.
    void SetGuidesDraggable(bool draggable) { guidesDraggable = draggable; }
    void SetSnapOptions(const VectorSnapOptions& options) { snapOptions = options; }
    const VectorSnapOptions& GetSnapOptions() const { return snapOptions; }
    // Snaps a document point per the options; says what it snapped to.
    Point2Dd Snap(const Point2Dd& doc, VectorSnapResult* result = nullptr) const;
    void SetShowPage(bool show) { showPage = show; RequestRedraw(); }
    void SetPasteboardColor(const Color& c) { pasteboardColor = c; RequestRedraw(); }
    void SetPageColor(const Color& c) { pageColor = c; RequestRedraw(); }

    // ===== SELECTION DISPLAY =====
    void SetShowSelection(bool show) { showSelection = show; RequestRedraw(); }
    // How much of the drawing is painted: outlines only, fills and lines
    // without antialiasing, or everything antialiased (the default). The
    // selection handles, guides and rulers are drawn the same in each.
    void SetDisplayQuality(VectorDisplayQuality quality);
    VectorDisplayQuality GetDisplayQuality() const;
    void SetHandleMode(VectorHandleMode mode);
    VectorHandleMode GetHandleMode() const { return handleMode; }
    void SetHandleSizePixels(double px) { handleSize = px; RequestRedraw(); }
    // The rotation centre, in document units; the middle of the selection's
    // frame when unset. The selector tool moves it by dragging the Center
    // handle. A centre that was set stays while the same objects stay
    // selected (an edit that selects them again keeps it) and is dropped
    // when the selection changes.
    void SetRotationCenter(const std::optional<Point2Dd>& docPoint);
    Point2Dd GetRotationCenter() const;
    bool HasRotationCenter() const { return rotationCenter.has_value(); }
    // The selection's frame in document units, corners top left, top right,
    // bottom right, bottom left. In rotate mode one selected object shows
    // its own box turned (and sheared) as the object is; otherwise, and for
    // several objects, it is the upright bounds. The handles sit on it.
    std::array<Point2Dd, 4> SelectionFrame() const;
    // A point across the frame from fractions of it (0, 0 its top left
    // corner, 1, 1 its bottom right), and the fractions of a point.
    Point2Dd SelectionFramePoint(double fx, double fy) const;
    Point2Dd SelectionFrameFraction(const Point2Dd& docPoint) const;
    // The rotation handle's look; a dial by default.
    void SetRotationHandleStyle(VectorRotationHandleStyle style);
    VectorRotationHandleStyle GetRotationHandleStyle() const { return rotationHandleStyle; }
    // The way the rotation handle points, in degrees counter-clockwise
    // from up: the frame's turn, or what SetRotationHandleAngle set.
    double RotationHandleAngle() const;
    // While a tool turns the selection it says where the handle points (the
    // pointer's way, or the turn so far: several objects have no angle of
    // their own); nullopt hands it back to the frame.
    void SetRotationHandleAngle(const std::optional<double>& degrees);
    // The handle a tool is dragging (NoHandle when none). While one that
    // turns the selection is dragged - a corner in rotate mode, or the
    // rotation handle - the frame and the other handles are not drawn: only
    // the centre and the rotation handle stay over the turning objects.
    void SetDraggedHandle(VectorHandle handle);
    VectorHandle GetDraggedHandle() const { return draggedHandle; }
    // Which handle a view point is on, for the selection as drawn.
    VectorHandle HitTestHandle(const Point2Di& viewPoint) const;
    // The selection's bounds in document units (empty when nothing is selected).
    Rect2Dd SelectionBounds() const;

    // ===== HIT TESTING (document units, tolerance in screen pixels) =====
    std::optional<VectorEdit::VectorHit> HitTest(const Point2Dd& doc, double tolerancePixels = 4.0) const;
    std::vector<VectorEdit::ElementPtr> ElementsIn(const Rect2Dd& docRect, bool fullyInside) const;

    // ===== TOOL CALLBACKS (document coordinates) =====
    std::function<void(const VectorPointerEvent&)> onToolPress;
    std::function<void(const VectorPointerEvent&)> onToolDrag;
    std::function<void(const VectorPointerEvent&)> onToolRelease;
    std::function<void(const VectorPointerEvent&)> onToolHover;
    std::function<void(const VectorPointerEvent&)> onToolDoubleClick;
    std::function<bool(const UCEvent&)> onToolKey;
    // Draw tool previews in VIEW coordinates after the document and the
    // selection handles, before the rulers.
    std::function<void(IRenderContext*, const VectorViewTransform&)> onDrawOverlay;
    std::function<void()> onViewChanged;
    // The handles switched between scale and rotate.
    std::function<void(VectorHandleMode)> onHandleModeChanged;
    // The rotation centre was moved, or dropped with the selection.
    std::function<void()> onRotationCenterChanged;
    std::function<void()> onGuidesChanged;
    std::function<void(const std::vector<std::string>&)> onFilesDropped;

    // Space-bar / middle-button panning is built in; a Push tool turns it
    // on permanently.
    void SetPanMode(bool enabled);
    void SetToolCursor(UCMouseCursor cursor);

    // ===== ELEMENT =====
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;
    bool AcceptsFocus() const override { return true; }
    // Something in the document changed: redraw that document rectangle
    // (or everything).
    void InvalidateDoc(const Rect2Dd& docRect);
    void Refresh() { RequestRedraw(); }

private:
    VectorPointerEvent MakePointerEvent(const UCEvent& e) const;
    void ClampView();
    void HookSelection();
    void UnhookSelection();
    void DrawPage(IRenderContext* ctx);
    void DrawGrid(IRenderContext* ctx, const Rect2Dd& area);
    void DrawDocument(IRenderContext* ctx, const Rect2Dd& area);
    void DrawGuides(IRenderContext* ctx, const Rect2Dd& area);
    void DrawSelectionHandles(IRenderContext* ctx);
    void DrawRotationHandle(IRenderContext* ctx);
    // The rotation handle in view coordinates: the centre, the knob and the
    // way it points (radians counter-clockwise from up). False when nothing
    // is selected or the handles are not in rotate mode.
    bool RotationHandleGeometry(Point2Dd& centreView, Point2Dd& knobView, double& angle) const;
    bool TurningHandleDragged() const;
    void DrawRulers(IRenderContext* ctx);
    Rect2Dd HandleRect(VectorHandle handle) const;      // view coordinates
    Point2Dd HandlePoint(VectorHandle handle) const;    // document coordinates
    bool PointInRuler(const Point2Di& p, bool& horizontalRuler) const;
    int GuideNear(const Point2Di& viewPoint, double tolerancePx) const;

    std::shared_ptr<VectorStorage::VectorDocument> document;
    std::shared_ptr<VectorEdit::VectorSelection> selection;
    int selectionListener = 0;
    std::unique_ptr<VectorRenderer> renderer;
    VectorEdit::VectorHitTester hitTester;
    VectorViewTransform view;
    bool fitPending = true;

    // workspace
    bool showRulers = true;
    double rulerSize = 20.0;
    double rulerPointsPerUnit = 1.0;
    std::string rulerUnitSymbol = "pt";
    VectorGridSpec grid;
    bool showGuides = true;
    bool guidesDraggable = true;
    std::vector<VectorGuide> guides;
    VectorSnapOptions snapOptions;
    bool showPage = true;
    Color pasteboardColor = Color(226, 228, 232, 255);
    Color pageColor = Color(255, 255, 255, 255);
    Color guideColor = Color(0, 160, 200, 255);
    Color handleColor = Color(255, 255, 255, 255);
    Color handleBorder = Color(30, 100, 220, 255);

    // selection display
    bool showSelection = true;
    VectorHandleMode handleMode = VectorHandleMode::Scale;
    double handleSize = 8.0;
    std::optional<Point2Dd> rotationCenter;
    std::vector<std::string> rotationCenterIds;   // the selection the centre was set for
    VectorRotationHandleStyle rotationHandleStyle = VectorRotationHandleStyle::Dial;
    std::optional<double> rotationHandleAngle;    // degrees, while a tool turns the selection
    VectorHandle draggedHandle = VectorHandle::NoHandle;

    // interaction
    bool alwaysPan = false;
    bool spaceDown = false;
    bool panning = false;
    bool toolDragging = false;
    int draggingGuide = -1;        // index of a guide being dragged, -1 none
    bool creatingGuide = false;    // a guide being pulled out of a ruler
    UCMouseButton dragButton = UCMouseButton::NoneButton;
    UCMouseCursor toolCursor = UCMouseCursor::Arrow;
    Point2Di lastPointer;
    Point2Di hoverPointer;
    bool hasHover = false;
};

inline std::shared_ptr<UltraCanvasVectorCanvas> CreateVectorCanvas(const std::string& id = "VectorCanvas") {
    return std::make_shared<UltraCanvasVectorCanvas>(id);
}

inline std::shared_ptr<UltraCanvasVectorCanvas> CreateVectorCanvas(
        const std::string& id, float x, float y, float w, float h) {
    return std::make_shared<UltraCanvasVectorCanvas>(id, x, y, w, h);
}

} // namespace UltraCanvas
