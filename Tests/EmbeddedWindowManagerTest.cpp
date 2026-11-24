// Tests/EmbeddedWindowManagerTest.cpp
// The in-program window manager (UltraCanvasEmbeddedWindowManager): placement,
// stacking bands, focus, a title bar kept with its window, maximize into the
// work area the desktop's bars leave, minimize and the focus moving on, hit
// testing and pointer routing with the button grab, moving by the title bar
// and resizing by an edge, damage, and the pixels Compose draws - windows
// bottom first at the output's scale, the pointer on top.
// No platform and no display: the model and cairo image surfaces only.

#include "UltraCanvasEmbeddedWindowManager.h"

#include <cairo/cairo.h>

#include <cstdint>
#include <iostream>
#include <map>
#include <string>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!condition) ++g_failures;
}

EmbeddedWindowSpec Spec(int x, int y, int w, int h, bool decorated = true,
                        EmbeddedWindowLayer layer = EmbeddedWindowLayer::Normal) {
    EmbeddedWindowSpec s;
    s.rect = Rect2Di(x, y, w, h);
    s.decorated = decorated;
    s.layer = layer;
    s.minSize = Size2Di(100, 60);
    return s;
}

Rect2Di RectOf(const UltraCanvasEmbeddedWindowManager& wm, uint64_t id) {
    EmbeddedWindowInfo info;
    wm.GetInfo(id, info);
    return info.rect;
}

uint32_t PixelAt(cairo_surface_t* s, int x, int y) {
    cairo_surface_flush(s);
    const unsigned char* data = cairo_image_surface_get_data(s);
    const int stride = cairo_image_surface_get_stride(s);
    return reinterpret_cast<const uint32_t*>(data + y * stride)[x] & 0x00FFFFFFu;
}

cairo_surface_t* Solid(int w, int h, double scale, double r, double g, double b) {
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
                                                    static_cast<int>(w * scale), static_cast<int>(h * scale));
    cairo_surface_set_device_scale(s, scale, scale);
    cairo_t* cr = cairo_create(s);
    cairo_set_source_rgb(cr, r, g, b);
    cairo_paint(cr);
    cairo_destroy(cr);
    return s;
}

void TestPlacementAndStacking() {
    std::cout << "placement and stacking" << std::endl;
    UltraCanvasEmbeddedWindowManager wm;
    wm.SetOutputSize(1920, 1080);

    EmbeddedWindowSpec desktop;
    desktop.fillsOutput = true;
    desktop.layer = EmbeddedWindowLayer::Desktop;
    desktop.decorated = false;
    const Rect2Di d = wm.AddWindow(1, desktop);
    Check(d.x == 0 && d.y == 0 && d.width == 1920 && d.height == 1080, "a desktop fills the output");

    const Rect2Di a = wm.AddWindow(2, Spec(-1, -1, 800, 600));
    const Rect2Di b = wm.AddWindow(3, Spec(-1, -1, 800, 600));
    Check(a.y >= wm.titleBarHeight, "a placed window leaves room for its title bar");
    Check(b.x > a.x && b.y > a.y, "the next window is cascaded");

    EmbeddedWindowSpec popup = Spec(100, 100, 200, 150, false, EmbeddedWindowLayer::Popup);
    popup.focusable = false;
    wm.AddWindow(4, popup);
    for (uint64_t id : {1, 2, 3, 4}) wm.Show(id);

    auto order = wm.ListWindows();
    Check(order.size() == 4 && order.front().id == 1 && order.back().id == 4,
          "the desktop is at the bottom, the popup's band on top");
    Check(wm.GetFocused() == 3, "the last window shown has the focus");

    wm.Activate(2);
    order = wm.ListWindows();
    Check(order[2].id == 2 && order[3].id == 4, "activating raises within the band, under the popup");
    Check(wm.GetFocused() == 2, "and focuses it");

    wm.Activate(1);
    order = wm.ListWindows();
    Check(order.front().id == 1, "activating the desktop never brings it above the windows");

    auto apps = wm.ListApplicationWindows();
    Check(apps.size() == 2, "a taskbar lists the two windows, not the desktop or the popup");

    // A window larger than the output is kept within it.
    const Rect2Di big = wm.AddWindow(5, Spec(-1, -1, 4000, 3000));
    Check(big.width <= 1920 && big.height <= 1080 - wm.titleBarHeight, "a window too big for the output is cut to it");
}

void TestFrameAndMaximize() {
    std::cout << "title bar, maximize, minimize" << std::endl;
    UltraCanvasEmbeddedWindowManager wm;
    wm.SetOutputSize(1280, 720);
    std::map<uint64_t, Rect2Di> told;
    wm.onGeometryChanged = [&](uint64_t id, const Rect2Di& r) { told[id] = r; };

    wm.AddWindow(10, Spec(200, 150, 600, 400));
    EmbeddedWindowSpec frameSpec = Spec(0, 0, 600, 32, false);
    frameSpec.focusable = false;
    wm.AddWindow(11, frameSpec);
    wm.AttachFrame(10, 11);
    wm.Show(10);

    Rect2Di f = RectOf(wm, 11);
    Check(f.x == 200 && f.y == 150 - wm.titleBarHeight && f.width == 600 && f.height == wm.titleBarHeight,
          "the frame sits just above its window, as wide as it");
    EmbeddedWindowInfo fi;
    wm.GetInfo(11, fi);
    Check(fi.visible && fi.frameOf == 10, "and is shown with it");
    auto order = wm.ListWindows();
    Check(order.size() == 2 && order[1].id == 11, "the frame is stacked above its window");

    wm.ReserveEdges(99, 0, 64, 0, 48);   // a right bar and a bottom bar
    const Rect2Di work = wm.GetWorkArea();
    Check(work.width == 1216 && work.height == 672, "the work area leaves out the reserved strips");

    wm.Maximize(10);
    const Rect2Di m = RectOf(wm, 10);
    Check(m.x == 0 && m.y == wm.titleBarHeight && m.width == 1216 && m.height == 672 - wm.titleBarHeight,
          "maximized: the work area less the title bar");
    Check(told.count(10) && told[10].width == 1216, "the window heard its new size");
    Check(RectOf(wm, 11).y == 0, "its title bar went with it");

    wm.Restore(10);
    Check(RectOf(wm, 10).x == 200 && RectOf(wm, 10).width == 600, "restore brings the old rectangle back");

    wm.AddWindow(20, Spec(50, 80, 300, 200));
    wm.Show(20);
    Check(wm.GetFocused() == 20, "a new window takes the focus");
    wm.Minimize(20);
    Check(wm.GetFocused() == 10, "minimizing passes the focus to the window beneath");
    EmbeddedWindowInfo info;
    wm.GetInfo(20, info);
    Check(info.minimized, "the window is minimized");
    wm.Activate(20);
    wm.GetInfo(20, info);
    Check(!info.minimized && wm.GetFocused() == 20, "activating restores it");

    // A dialog is centred over its window and raised with it.
    EmbeddedWindowSpec dlg = Spec(-1, -1, 200, 100);
    dlg.transientFor = 10;
    const Rect2Di dr = wm.AddWindow(30, dlg);
    wm.Show(30);
    const Rect2Di parent = RectOf(wm, 10);
    Check(dr.x == parent.x + (parent.width - 200) / 2, "a dialog is centred over its window");
    wm.Activate(20);
    wm.Activate(10);
    order = wm.ListWindows();
    Check(order.back().id == 30, "raising a window raises its dialog above it");
    wm.RemoveWindow(30);
    Check(wm.GetFocused() == 10, "a dialog closed gives the focus back to its window");
}

void TestPointer() {
    std::cout << "pointer routing, move, resize" << std::endl;
    UltraCanvasEmbeddedWindowManager wm;
    wm.SetOutputSize(1280, 720);
    wm.AddWindow(1, Spec(100, 100, 400, 300));
    EmbeddedWindowSpec frameSpec = Spec(0, 0, 400, 32, false);
    frameSpec.focusable = false;
    wm.AddWindow(2, frameSpec);
    wm.AttachFrame(1, 2);
    wm.AddWindow(3, Spec(700, 100, 400, 300));
    wm.Show(1);
    wm.Show(3);

    EmbeddedPointerRoute r = wm.PointerMove(Point2Di(150, 160));
    Check(r.windowId == 1 && r.local.x == 50 && r.local.y == 60, "a move over a window goes to it, in its coordinates");
    r = wm.PointerMove(Point2Di(800, 200));
    Check(r.windowId == 3 && r.leftWindowId == 1, "moving to another window says the first was left");

    r = wm.PointerButton(Point2Di(150, 160), true);
    Check(r.windowId == 1 && wm.GetFocused() == 1, "a press focuses the window it lands on");
    r = wm.PointerMove(Point2Di(900, 200));
    Check(r.windowId == 1 && r.local.x == 800, "while held, the pointer stays with that window");
    r = wm.PointerButton(Point2Di(900, 200), false);
    Check(r.windowId == 1, "the release goes there too");
    r = wm.PointerMove(Point2Di(900, 200));
    Check(r.windowId == 3, "afterwards the pointer goes to what is under it");

    // The title bar: the frame window hears the press and asks for a move.
    r = wm.PointerButton(Point2Di(200, 100 - 10), true);
    Check(r.windowId == 2, "a press on the title bar goes to the frame window");
    Check(wm.GetFocused() == 1, "and focuses the window it decorates");
    wm.BeginMove(1);
    wm.PointerMove(Point2Di(260, 150));
    Check(RectOf(wm, 1).x == 160 && RectOf(wm, 1).y == 160, "the window follows the pointer");
    Check(RectOf(wm, 2).y == 160 - wm.titleBarHeight, "and its title bar with it");
    wm.PointerButton(Point2Di(260, 150), false);
    Check(!wm.IsMovingOrResizing(), "the release ends the move");

    // The bottom-right corner, just outside the window.
    const Rect2Di before = RectOf(wm, 1);
    const Point2Di corner(before.x + before.width + 2, before.y + before.height + 2);
    uint64_t over = 0;
    Check(wm.HitTest(corner, &over) == EmbeddedWindowPart::ResizeBottomRight && over == 1,
          "just outside the corner is its resize edge");
    r = wm.PointerButton(corner, true);
    Check(r.windowId == 0 && r.managerCursor && r.cursor == UCMouseCursor::SizeNWSE,
          "the manager takes the press, with the resize pointer");
    wm.PointerMove(Point2Di(corner.x + 100, corner.y + 50));
    Check(RectOf(wm, 1).width == before.width + 100 && RectOf(wm, 1).height == before.height + 50,
          "dragging the corner resizes");
    wm.PointerMove(Point2Di(corner.x - 1000, corner.y - 1000));
    Check(RectOf(wm, 1).width == 100 && RectOf(wm, 1).height == 60, "never below the minimum size");
    wm.PointerButton(Point2Di(0, 0), false);

    const Point2Di p = wm.MovePointerBy(-5000, 20000);
    Check(p.x == 0 && p.y == 719, "relative motion keeps the pointer on the output");
}

void TestCompose() {
    std::cout << "compositing" << std::endl;
    const float scale = 2.0f;
    UltraCanvasEmbeddedWindowManager wm;
    wm.SetOutputSize(400, 300);
    wm.backgroundRgb = 0x000000;
    wm.AddWindow(1, Spec(20, 40, 200, 150, false));
    wm.AddWindow(2, Spec(120, 100, 200, 150, false));
    wm.Show(1);
    wm.Show(2);
    wm.SetPointerPosition(Point2Di(390, 290));

    cairo_surface_t* red = Solid(200, 150, scale, 1, 0, 0);
    cairo_surface_t* blue = Solid(200, 150, scale, 0, 0, 1);
    cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 800, 600);
    auto provider = [&](uint64_t id) -> cairo_surface_t* { return id == 1 ? red : blue; };

    wm.TakeDamage();
    wm.DamageAll();
    const Rect2Di drawn = wm.Compose(out, scale, provider, wm.TakeDamage());
    Check(drawn.width == 800 && drawn.height == 600, "the whole output was drawn");
    Check(PixelAt(out, 5, 5) == 0x000000, "the background where no window is");
    Check(PixelAt(out, 60, 100) == 0xFF0000, "the lower window at its place, at twice the size");
    Check(PixelAt(out, 300, 260) == 0x0000FF, "the upper window covers the lower one");
    Check(PixelAt(out, 240, 200) == 0x0000FF && PixelAt(out, 239, 199) == 0xFF0000,
          "the upper window starts exactly at its corner, pixel for pixel");
    Check(PixelAt(out, 781, 585) != 0x000000, "the pointer is drawn on top");

    // Only what changed is drawn again.
    wm.Activate(1);
    auto damage = wm.TakeDamage();
    Check(!damage.empty(), "raising a window damages it");
    cairo_t* cr = cairo_create(out);
    cairo_set_source_rgb(cr, 0, 1, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    wm.Compose(out, scale, provider, damage);
    Check(PixelAt(out, 300, 260) == 0xFF0000, "the raised window is drawn over the other");
    Check(PixelAt(out, 700, 50) == 0x00FF00, "outside the damage nothing was drawn");

    wm.WindowPresented(2);
    damage = wm.TakeDamage();
    Check(damage.size() == 1 && damage[0].x == 120 && damage[0].width == 200, "a presented window damages its rectangle");

    cairo_surface_destroy(red);
    cairo_surface_destroy(blue);
    cairo_surface_destroy(out);
}

} // namespace

int main() {
    TestPlacementAndStacking();
    TestFrameAndMaximize();
    TestPointer();
    TestCompose();
    if (g_failures) {
        std::cout << g_failures << " check(s) failed" << std::endl;
        return 1;
    }
    std::cout << "all checks passed" << std::endl;
    return 0;
}
