// Tests/DiagramViewportTest.cpp
// Unit tests for UltraCanvasDiagramViewport - the pan/zoom/minimap/controls
// component shared by UltraCanvasNodeDiagram and UltraCanvasCompositorDiagram.
//
// Covers the geometry that both elements depend on: coordinate transforms,
// zoom-at-cursor anchoring, FitView, overlay panel placement, the minimap
// projection and its inverse, control-button role mapping and the snap grid.
// No UI stack and no rendering - only the component under test.
//
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasDiagramViewport.h"

#include <cmath>
#include <cstdio>

using namespace UltraCanvas;

static int g_failures = 0;

#define CHECK(cond, msg)                                                 \
    do {                                                                 \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
        else         { std::printf("  ok:   %s\n", msg); }               \
    } while (0)

static bool Near(double a, double b, double eps = 1e-6) {
    return std::abs(a - b) <= eps;
}

// =============================================================================

static void TestCoordinateTransforms() {
    std::printf("\n[coordinate transforms]\n");

    UltraCanvasDiagramViewport vp;
    vp.SetViewportSize(800, 600);
    vp.SetZoomLevel(2.0);
    vp.SetPanOffset(100, 50);

    // world -> screen: w * zoom + pan
    Point2Dd screen = vp.WorldToScreenD(Point2Dd(10, 20));
    CHECK(Near(screen.x, 120.0) && Near(screen.y, 90.0),
          "WorldToScreen applies zoom then pan");

    Point2Dd world = vp.ScreenToWorld(120.0, 90.0);
    CHECK(Near(world.x, 10.0) && Near(world.y, 20.0),
          "ScreenToWorld inverts WorldToScreen");

    // Round trip over a spread of points.
    bool roundTripOk = true;
    for (double x = -500; x <= 500; x += 137.5) {
        for (double y = -500; y <= 500; y += 111.25) {
            Point2Dd s = vp.WorldToScreenD(Point2Dd(x, y));
            Point2Dd w = vp.ScreenToWorld(s.x, s.y);
            if (!Near(w.x, x, 1e-9) || !Near(w.y, y, 1e-9)) roundTripOk = false;
        }
    }
    CHECK(roundTripOk, "world->screen->world round trips exactly");

    // The visible world rect must match the transform of the viewport corners.
    Rect2Dd visible = vp.GetVisibleWorldRect();
    Point2Dd tl = vp.ScreenToWorld(0.0, 0.0);
    Point2Dd br = vp.ScreenToWorld(800.0, 600.0);
    CHECK(Near(visible.x, tl.x) && Near(visible.y, tl.y) &&
          Near(visible.width, br.x - tl.x) && Near(visible.height, br.y - tl.y),
          "GetVisibleWorldRect spans the viewport corners");
}

static void TestZoomAtPoint() {
    std::printf("\n[zoom at cursor]\n");

    UltraCanvasDiagramViewport vp;
    vp.SetViewportSize(800, 600);
    vp.SetPanOffset(0, 0);
    vp.SetZoomLevel(1.0);

    // This is the property the old NodeDiagram code got wrong: the world point
    // under the cursor must not move when zooming.
    const Point2Di cursor(250, 175);
    Point2Dd before = vp.ScreenToWorld(cursor);
    CHECK(vp.ZoomAtPoint(cursor, 1.25), "ZoomAtPoint reports a change");
    Point2Dd after = vp.ScreenToWorld(cursor);
    CHECK(Near(before.x, after.x, 1e-9) && Near(before.y, after.y, 1e-9),
          "world point under the cursor is unchanged by zooming in");
    CHECK(Near(vp.GetZoomLevel(), 1.25), "zoom level applied");

    // Repeated zooming, including at an off-origin pan, keeps the anchor.
    vp.SetPanOffset(-320, 145);
    bool anchored = true;
    for (int i = 0; i < 12; ++i) {
        Point2Dd w0 = vp.ScreenToWorld(cursor);
        vp.ZoomAtPoint(cursor, (i % 2 == 0) ? 1.3 : (1.0 / 1.1));
        Point2Dd w1 = vp.ScreenToWorld(cursor);
        if (!Near(w0.x, w1.x, 1e-7) || !Near(w0.y, w1.y, 1e-7)) anchored = false;
    }
    CHECK(anchored, "anchor holds across repeated zoom in/out with a panned view");

    // Clamping: no change once the limit is reached, and it says so.
    vp.SetZoomRange(0.5, 2.0);
    vp.SetZoomLevel(2.0);
    CHECK(!vp.ZoomAtPoint(cursor, 1.5), "ZoomAtPoint returns false at the max limit");
    CHECK(Near(vp.GetZoomLevel(), 2.0), "zoom stays clamped at max");
    vp.SetZoomLevel(0.5);
    CHECK(!vp.ZoomAtPoint(cursor, 0.5), "ZoomAtPoint returns false at the min limit");
    CHECK(Near(vp.GetZoomLevel(), 0.5), "zoom stays clamped at min");
}

static void TestFitView() {
    std::printf("\n[fit view]\n");

    UltraCanvasDiagramViewport vp;
    vp.SetViewportSize(800, 600);

    DiagramContentBounds content(0, 0, 400, 200);
    CHECK(vp.FitView(content, 40.0), "FitView succeeds for usable content");

    // Content is 400x200, available area is 720x520 -> limited by width.
    CHECK(Near(vp.GetZoomLevel(), 720.0 / 400.0), "zoom fits the limiting axis");

    // Content centre must land at the viewport centre.
    Point2Dd centreOnScreen = vp.WorldToScreenD(Point2Dd(200, 100));
    CHECK(Near(centreOnScreen.x, 400.0) && Near(centreOnScreen.y, 300.0),
          "content centre maps to the viewport centre");

    // Degenerate inputs are rejected rather than producing NaNs.
    DiagramContentBounds empty;
    CHECK(!vp.FitView(empty, 40.0), "FitView rejects empty content");
    DiagramContentBounds degenerate(10, 10, 10, 10);
    CHECK(!vp.FitView(degenerate, 40.0), "FitView rejects zero-area content");

    UltraCanvasDiagramViewport tiny;
    tiny.SetViewportSize(50, 50);
    CHECK(!tiny.FitView(content, 40.0),
          "FitView rejects padding larger than the viewport");

    // CenterOn puts an arbitrary world point at the viewport centre.
    vp.SetZoomLevel(1.5);
    vp.CenterOn(-37.5, 88.25);
    Point2Dd centred = vp.WorldToScreenD(Point2Dd(-37.5, 88.25));
    CHECK(Near(centred.x, 400.0) && Near(centred.y, 300.0),
          "CenterOn centres the requested world point");
}

static void TestPanelPlacement() {
    std::printf("\n[overlay placement]\n");

    UltraCanvasDiagramViewport vp;
    vp.SetViewportSize(1000, 800);

    auto& mm = vp.MinimapConfig();
    mm.visible = true;
    mm.width = 180;
    mm.height = 130;
    mm.padding = 10;

    mm.position = DiagramPanelPosition::TopLeft;
    Rect2Dd r = vp.GetMinimapRect();
    CHECK(Near(r.x, 10) && Near(r.y, 10), "minimap TopLeft placement");

    mm.position = DiagramPanelPosition::TopRight;
    r = vp.GetMinimapRect();
    CHECK(Near(r.x, 1000 - 180 - 10) && Near(r.y, 10), "minimap TopRight placement");

    mm.position = DiagramPanelPosition::BottomLeft;
    r = vp.GetMinimapRect();
    CHECK(Near(r.x, 10) && Near(r.y, 800 - 130 - 10), "minimap BottomLeft placement");

    mm.position = DiagramPanelPosition::BottomRight;
    r = vp.GetMinimapRect();
    CHECK(Near(r.x, 1000 - 180 - 10) && Near(r.y, 800 - 130 - 10),
          "minimap BottomRight placement");

    // Hit testing is in element-local space: a point inside the panel rect hits
    // regardless of where the host element sits on screen. This is the
    // regression guard for the finalBounds double-count bug.
    CHECK(vp.PointInMinimap(Point2Di(1000 - 180 - 10 + 5, 800 - 130 - 10 + 5)),
          "point inside the minimap panel hits");
    CHECK(!vp.PointInMinimap(Point2Di(5, 5)),
          "point far outside the minimap panel misses");

    mm.visible = false;
    CHECK(!vp.PointInMinimap(Point2Di(1000 - 100, 800 - 100)),
          "hidden minimap never hits");
}

static void TestMinimapProjection() {
    std::printf("\n[minimap projection]\n");

    UltraCanvasDiagramViewport vp;
    vp.SetViewportSize(1000, 800);
    auto& mm = vp.MinimapConfig();
    mm.visible = true;
    mm.position = DiagramPanelPosition::BottomRight;

    DiagramContentBounds content(-200, -100, 600, 300);
    DiagramMinimapProjection projection;
    CHECK(vp.GetMinimapProjection(content, projection),
          "projection computed for usable content");

    Rect2Dd panel = vp.GetMinimapRect();

    // Every projected content corner must land inside the panel.
    Point2Dd tl = projection.WorldToMinimap(content.minX, content.minY);
    Point2Dd br = projection.WorldToMinimap(content.maxX, content.maxY);
    CHECK(tl.x >= panel.x && tl.y >= panel.y &&
          br.x <= panel.x + panel.width && br.y <= panel.y + panel.height,
          "projected content stays inside the minimap panel");

    // Aspect ratio must be preserved (uniform scale on both axes).
    double projectedW = br.x - tl.x;
    double projectedH = br.y - tl.y;
    CHECK(Near(projectedW / content.GetWidth(), projectedH / content.GetHeight(), 1e-9),
          "projection scale is uniform on both axes");

    // HandleMinimapDrag inverts the projection: clicking a panel point must
    // centre the view on the world point that maps to it.
    Point2Di click(static_cast<int>(panel.x + panel.width * 0.25),
                   static_cast<int>(panel.y + panel.height * 0.75));
    vp.HandleMinimapDrag(click, content);

    double expectedWorldX = projection.worldMinX + (click.x - projection.originX) / projection.scale;
    double expectedWorldY = projection.worldMinY + (click.y - projection.originY) / projection.scale;
    Point2Dd centred = vp.WorldToScreenD(Point2Dd(expectedWorldX, expectedWorldY));
    CHECK(Near(centred.x, 500.0, 1e-6) && Near(centred.y, 400.0, 1e-6),
          "minimap click centres the view on the clicked world point");

    // A non-pannable minimap must not move the view.
    Point2Dd panBefore = vp.GetPanOffset();
    mm.pannable = false;
    vp.HandleMinimapDrag(click, content);
    Point2Dd panAfter = vp.GetPanOffset();
    CHECK(Near(panBefore.x, panAfter.x) && Near(panBefore.y, panAfter.y),
          "non-pannable minimap ignores drags");

    DiagramContentBounds empty;
    CHECK(!vp.GetMinimapProjection(empty, projection),
          "projection rejects empty content");
}

static void TestControlButtons() {
    std::printf("\n[controls overlay]\n");

    UltraCanvasDiagramViewport vp;
    vp.SetViewportSize(1000, 800);
    auto& cc = vp.ControlsConfig();
    cc.visible = true;
    cc.position = DiagramPanelPosition::BottomLeft;

    // All groups on: zoom in, zoom out, fit, lock.
    CHECK(vp.GetControlButtonCount() == 4, "four buttons when all groups enabled");
    CHECK(vp.GetControlButtonRole(0) == DiagramControlButton::ZoomIn, "index 0 is ZoomIn");
    CHECK(vp.GetControlButtonRole(1) == DiagramControlButton::ZoomOut, "index 1 is ZoomOut");
    CHECK(vp.GetControlButtonRole(2) == DiagramControlButton::FitView, "index 2 is FitView");
    CHECK(vp.GetControlButtonRole(3) == DiagramControlButton::ToggleLock, "index 3 is ToggleLock");
    CHECK(vp.GetControlButtonRole(4) == DiagramControlButton::NoAction, "index past the end is NoAction");
    CHECK(vp.GetControlButtonRole(-1) == DiagramControlButton::NoAction, "negative index is NoAction");

    // Disabling the zoom group must shift the remaining roles down - this is
    // why callers resolve roles instead of hard-coding indices.
    cc.showZoom = false;
    CHECK(vp.GetControlButtonCount() == 2, "two buttons when zoom is disabled");
    CHECK(vp.GetControlButtonRole(0) == DiagramControlButton::FitView,
          "FitView moves to index 0 when zoom is hidden");
    CHECK(vp.GetControlButtonRole(1) == DiagramControlButton::ToggleLock,
          "ToggleLock moves to index 1 when zoom is hidden");

    cc.showZoom = true;
    cc.showFit = false;
    CHECK(vp.GetControlButtonRole(2) == DiagramControlButton::ToggleLock,
          "ToggleLock moves up when fit is hidden");
    cc.showFit = true;

    // Hit testing: each button's centre resolves to its own index.
    Rect2Dd panel = vp.GetControlsRect();
    bool hitOk = true;
    for (int i = 0; i < vp.GetControlButtonCount(); ++i) {
        double cx = panel.x + cc.padding + cc.buttonSize * 0.5;
        double cy = panel.y + cc.padding + i * (cc.buttonSize + cc.gap) + cc.buttonSize * 0.5;
        if (vp.FindControlButtonAt(Point2Di(static_cast<int>(cx), static_cast<int>(cy))) != i) {
            hitOk = false;
        }
    }
    CHECK(hitOk, "each button centre hit-tests to its own index");
    CHECK(vp.FindControlButtonAt(Point2Di(900, 100)) == -1,
          "point outside the controls panel misses");

    cc.visible = false;
    CHECK(vp.FindControlButtonAt(Point2Di(static_cast<int>(panel.x + 5),
                                          static_cast<int>(panel.y + 5))) == -1,
          "hidden controls never hit");
    cc.visible = true;

    // ApplyControlButton performs the standard action and reports the role.
    DiagramContentBounds content(0, 0, 400, 200);
    vp.SetZoomLevel(1.0);
    CHECK(vp.ApplyControlButton(0, content) == DiagramControlButton::ZoomIn,
          "ApplyControlButton reports ZoomIn");
    CHECK(vp.GetZoomLevel() > 1.0, "ZoomIn increased the zoom level");

    vp.SetZoomLevel(1.0);
    vp.ApplyControlButton(1, content);
    CHECK(vp.GetZoomLevel() < 1.0, "ZoomOut decreased the zoom level");

    vp.ApplyControlButton(2, content);
    CHECK(Near(vp.GetZoomLevel(), (1000.0 - 80.0) / 400.0),
          "FitView button fits the content");

    // The lock role is reported but never acted on here - lock state belongs to
    // the host element.
    double zoomBefore = vp.GetZoomLevel();
    CHECK(vp.ApplyControlButton(3, content) == DiagramControlButton::ToggleLock,
          "ApplyControlButton reports ToggleLock");
    CHECK(Near(vp.GetZoomLevel(), zoomBefore),
          "ToggleLock does not change viewport state");
}

static void TestSnapGrid() {
    std::printf("\n[snap grid]\n");

    UltraCanvasDiagramViewport vp;
    Point2Dd p(37.0, 62.0);

    CHECK(!vp.IsSnapToGridEnabled(), "snapping is off by default");
    Point2Dd unsnapped = vp.SnapPoint(p);
    CHECK(Near(unsnapped.x, 37.0) && Near(unsnapped.y, 62.0),
          "disabled snap returns the point untouched");

    vp.SetSnapToGrid(true);
    vp.SetSnapGrid(25.0, 25.0);
    // 37 -> 25 (nearer than 50); 62 -> 50 (12 away, vs 13 to 75).
    Point2Dd snapped = vp.SnapPoint(p);
    CHECK(Near(snapped.x, 25.0) && Near(snapped.y, 50.0),
          "point snaps to the nearest grid intersection");

    // Negative coordinates round to nearest too, symmetrically.
    Point2Dd negative = vp.SnapPoint(Point2Dd(-37.0, -62.0));
    CHECK(Near(negative.x, -25.0) && Near(negative.y, -50.0),
          "negative coordinates snap to the nearest intersection");

    // A degenerate grid must not divide by zero.
    vp.SetSnapGrid(0.0, 0.0);
    Point2Dd safe = vp.SnapPoint(p);
    CHECK(Near(safe.x, 37.0) && Near(safe.y, 62.0),
          "zero grid spacing is ignored rather than producing NaN");
}

static void TestZoomClamping() {
    std::printf("\n[zoom clamping]\n");

    UltraCanvasDiagramViewport vp;
    vp.SetViewportSize(800, 600);
    vp.SetZoomRange(0.25, 4.0);

    vp.SetZoomLevel(100.0);
    CHECK(Near(vp.GetZoomLevel(), 4.0), "SetZoomLevel clamps to max");
    vp.SetZoomLevel(0.0001);
    CHECK(Near(vp.GetZoomLevel(), 0.25), "SetZoomLevel clamps to min");

    vp.SetZoomLevel(1.0);
    for (int i = 0; i < 50; ++i) vp.ZoomIn(1.2);
    CHECK(Near(vp.GetZoomLevel(), 4.0), "repeated ZoomIn saturates at max");
    for (int i = 0; i < 100; ++i) vp.ZoomOut(1.2);
    CHECK(Near(vp.GetZoomLevel(), 0.25), "repeated ZoomOut saturates at min");

    // Narrowing the range must re-clamp the current level immediately.
    vp.SetZoomLevel(0.25);
    vp.SetZoomRange(1.0, 2.0);
    CHECK(Near(vp.GetZoomLevel(), 1.0), "SetZoomRange re-clamps the current zoom");
}

static void TestContentBounds() {
    std::printf("\n[content bounds]\n");

    DiagramContentBounds bounds;
    CHECK(!bounds.IsUsable(), "default-constructed bounds are unusable");

    bounds.Include(10.0, 20.0);
    CHECK(bounds.valid && Near(bounds.minX, 10.0) && Near(bounds.maxX, 10.0),
          "first Include seeds both min and max");
    CHECK(!bounds.IsUsable(), "a single point has no area");

    bounds.Include(-5.0, 40.0);
    CHECK(Near(bounds.minX, -5.0) && Near(bounds.maxX, 10.0) &&
          Near(bounds.minY, 20.0) && Near(bounds.maxY, 40.0),
          "Include expands in both directions");
    CHECK(bounds.IsUsable(), "two distinct points give usable bounds");
    CHECK(Near(bounds.GetWidth(), 15.0) && Near(bounds.GetHeight(), 20.0),
          "width and height reflect the extent");

    DiagramContentBounds rectBounds;
    rectBounds.Include(Rect2Dd(100, 50, 30, 20));
    CHECK(Near(rectBounds.minX, 100.0) && Near(rectBounds.minY, 50.0) &&
          Near(rectBounds.maxX, 130.0) && Near(rectBounds.maxY, 70.0),
          "Include(Rect2Dd) covers the whole rectangle");
}

int main() {
    std::printf("UltraCanvasDiagramViewport tests\n");
    std::printf("================================\n");

    TestCoordinateTransforms();
    TestZoomAtPoint();
    TestFitView();
    TestPanelPlacement();
    TestMinimapProjection();
    TestControlButtons();
    TestSnapGrid();
    TestZoomClamping();
    TestContentBounds();

    std::printf("\n================================\n");
    if (g_failures == 0) {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test(s) FAILED.\n", g_failures);
    return 1;
}
