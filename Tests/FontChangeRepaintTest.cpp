// Tests/FontChangeRepaintTest.cpp
// UltraCanvasWindowBase::RefreshAfterFontChange() lays out and draws the
// whole window and its popups again.
//
// When the fonts change while windows are open - on Windows the system fonts
// arrive after a cold start (SwitchToSystemFonts), or an application registers
// a font file - every text must be shaped and drawn again. The switch laid the
// windows out again and stopped there; a frame draws only its dirty
// rectangles, so text drawn with the start's fonts stayed on screen - a box of
// hex digits for each character those fonts lacked - until something else
// happened to repaint it.
//
// Runs headless: a window stand-in whose screen is an offscreen surface, so
// UpdateAndRender() lays out, paints and composites exactly as a real window
// does. The element's colour changes without a redraw request, so only a
// repaint the window was asked for shows it.
// Version: 1.0.0
// Last Modified: 2026-10-11
// Author: UltraCanvas Framework

#include "UltraCanvasRenderContext.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasWindow.h"

#include <cairo/cairo.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

using namespace UltraCanvas;

static int testCount = 0;
static int failCount = 0;

#define TEST(name, condition)                                                 \
    do {                                                                      \
        bool passed = (condition);                                            \
        std::cerr << (passed ? "PASS" : "FAIL") << ": " << name << std::endl; \
        if (!passed) failCount++;                                             \
        testCount++;                                                          \
    } while (0)

namespace {

class ScreenWindow : public UltraCanvasWindowBase {
public:
    static constexpr int kWidth = 200;
    static constexpr int kHeight = 120;
    cairo_surface_t* screen = nullptr;

    ScreenWindow() {
        config_.width = kWidth;
        config_.height = kHeight;
        SetBounds(Rect2Df(0, 0, kWidth, kHeight));
        screen = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, kWidth, kHeight);
        nativeSurface = screen;
        renderContext = CreateRenderContext(Size2Di(kWidth, kHeight), nativeSurface);
        _created = renderContext != nullptr;
        _windowVisible = true;
        AddDirtyRectangle(Rect2Di(0, 0, kWidth, kHeight));
    }
    ~ScreenWindow() override {
        CloseAllPopups();
        renderContext.reset();
        cairo_surface_destroy(screen);
    }

    bool Ready() const { return _created; }
    void Frame() { UpdateAndRender(); }

    // The screen's pixel at (x, y), as 0xRRGGBB.
    uint32_t Pixel(int x, int y) const {
        cairo_surface_flush(screen);
        const unsigned char* data = cairo_image_surface_get_data(screen);
        const int stride = cairo_image_surface_get_stride(screen);
        return *reinterpret_cast<const uint32_t*>(data + y * stride + x * 4) & 0xFFFFFF;
    }

    void SetFocusedElement(UltraCanvasUIElement*) override {}
    void Show() override {}
    void Hide() override {}
    void RaiseAndFocus() override {}
    void SetWindowTitle(const std::string&) override {}
    void SetWindowIcon(const std::string&) override {}
    void SetWindowPosition(int, int) override {}
    void SetWindowSize(int, int) override {}
    void Minimize() override {}
    void Maximize() override {}
    void Restore() override {}
    void SetFullscreen(bool) override {}
    void SetResizable(bool) override {}
    void GetScreenSize(int& width, int& height) const override { width = kWidth; height = kHeight; }
    NativeWindowHandle GetNativeHandle() const override { return NativeWindowHandle{}; }
    void InvalidateWindowNative() override {}

protected:
    bool CreateNative() override { return true; }
    void DestroyNative() override {}
    void DoResizeNative() override {}
    bool RecreateNativeSurface() override { return true; }
};

// Paints itself blue or red, and counts its layout passes. Its colour stands
// for text shaped with one set of fonts or another: it changes without the
// element asking to be redrawn.
class Swatch : public UltraCanvasUIElement {
public:
    bool red = false;
    int arranged = 0;

    Swatch(const std::string& id, float x, float y) : UltraCanvasUIElement(id, x, y, 60, 40) {}

    void Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) override {
        UltraCanvasUIElement::Arrange(finalRect, ctx);
        ++arranged;
    }
    void Render(IRenderContext* ctx, const Rect2Df&) override {
        ctx->SetFillPaint(red ? Color(255, 0, 0, 255) : Color(0, 0, 255, 255));
        ctx->FillRectangle(Rect2Df(0, 0, 60, 40));
    }
};

constexpr uint32_t kBlue = 0x0000FF;
constexpr uint32_t kRed = 0xFF0000;

} // namespace

int main() {
    ScreenWindow win;
    if (!win.Ready()) {
        TEST("a window stand-in with a screen to draw into", false);
        return 1;
    }

    auto content = std::make_shared<Swatch>("content", 10, 10);
    win.AddChild(content);
    win.Frame();
    TEST("the element is drawn", win.Pixel(30, 30) == kBlue);

    // ---- A frame draws only what is dirty ----
    content->red = true;
    win.Frame();
    TEST("a frame with nothing dirty leaves the old pixels (why a new layout alone is not enough)",
         win.Pixel(30, 30) == kBlue);

    // ---- RefreshAfterFontChange: a new layout and the whole window drawn ----
    const int arrangedBefore = content->arranged;
    win.RefreshAfterFontChange();
    win.Frame();
    TEST("after RefreshAfterFontChange the element is laid out again", content->arranged > arrangedBefore);
    TEST("and drawn again", win.Pixel(30, 30) == kRed);

    // ---- An open popup is drawn again too ----
    auto popup = std::make_shared<Swatch>("popup", 0, 0);
    win.OpenPopup(Point2Di(120, 60), *popup, PopupElementSettings());
    win.Frame();
    TEST("the popup is drawn over the window", win.Pixel(140, 80) == kBlue);
    popup->red = true;
    win.Frame();
    TEST("a frame with nothing dirty leaves the popup's old pixels", win.Pixel(140, 80) == kBlue);
    win.RefreshAfterFontChange();
    win.Frame();
    TEST("after RefreshAfterFontChange the popup is drawn again", win.Pixel(140, 80) == kRed);
    win.ClosePopup(*popup, ClosePopupReason::Manual);

    std::cerr << "\nFontChangeRepaintTest: " << testCount << " checks, " << failCount << " failures" << std::endl;
    return failCount;
}
