// Tests/RenderContextTest.cpp
// The render context's compositing and geometry API on an offscreen
// surface: blend modes, groups with opacity, masks, geometric hit testing,
// stroke extents, transform readback, conic / mesh / pixmap patterns,
// pattern placement and text outlines. Every check samples pixels back
// from the surface, so it needs no display.
//
// Usage: RenderContextTest
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-09-15
// Author: UltraCanvas Framework

#include "UltraCanvasRenderContext.h"
#include "UltraCanvasImage.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

using namespace UltraCanvas;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what.c_str());
    } else {
        std::printf("  ok: %s\n", what.c_str());
    }
}

struct Rgba { int r, g, b, a; };

// An offscreen context and the pixmap its pixels are read back through.
struct Canvas {
    UCPixmap pixmap;
    std::unique_ptr<IRenderContext> ctx;
    int w, h;
    Canvas(int width, int height) : w(width), h(height) {
        pixmap.Init(w, h);
        ctx = CreateRenderContext(Size2Di(w, h), nullptr);
        ctx->Clear(Color(0, 0, 0, 0));
    }
    Rgba Sample(int x, int y) {
        ctx->FlushToSurface(pixmap.GetSurface(), Point2Dd(0, 0));
        pixmap.MarkDirty();
        pixmap.Flush();
        const uint32_t p = pixmap.GetPixel(x, y);
        int a = (p >> 24) & 0xFF, r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
        if (a != 0 && a != 255) {
            r = std::min(255, (r * 255 + a / 2) / a);
            g = std::min(255, (g * 255 + a / 2) / a);
            b = std::min(255, (b * 255 + a / 2) / a);
        }
        return {r, g, b, a};
    }
};

bool Near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

} // namespace

int main() {
    UCImage::InitializeImageSubsysterm("RenderContextTest");

    // ===== Blend modes =====
    {
        Canvas c(40, 40);
        c.ctx->SetFillPaint(Color(255, 255, 0, 255));         // yellow
        c.ctx->FillRectangle(Rect2Dd(0, 0, 40, 40));
        c.ctx->SetBlendMode(BlendMode::Multiply);
        Check(c.ctx->GetBlendMode() == BlendMode::Multiply, "GetBlendMode reports the mode set");
        c.ctx->SetFillPaint(Color(0, 255, 255, 255));         // cyan
        c.ctx->FillRectangle(Rect2Dd(0, 0, 20, 40));
        Rgba mixed = c.Sample(10, 20);
        Rgba plain = c.Sample(30, 20);
        Check(mixed.r < 10 && mixed.g > 245 && mixed.b < 10, "multiply: yellow x cyan = green");
        Check(plain.r > 245 && plain.g > 245 && plain.b < 10, "multiply leaves the uncovered part yellow");
        c.ctx->PushState();
        c.ctx->SetBlendMode(BlendMode::Screen);
        c.ctx->PopState();
        Check(c.ctx->GetBlendMode() == BlendMode::Multiply, "PopState restores the blend mode");
    }

    // ===== Group opacity =====
    {
        Canvas c(40, 40);
        c.ctx->SetBlendMode(BlendMode::Normal);
        c.ctx->BeginGroup();
        c.ctx->SetFillPaint(Color(255, 0, 0, 255));
        c.ctx->FillRectangle(Rect2Dd(0, 0, 40, 40));
        c.ctx->FillRectangle(Rect2Dd(0, 0, 40, 40));   // overlapping twice inside the group
        c.ctx->EndGroup(0.5);
        Rgba p = c.Sample(20, 20);
        Check(p.r > 245 && p.a > 118 && p.a < 138, "EndGroup(0.5) composites the group once at half alpha");
    }

    // ===== Mask =====
    {
        Canvas c(40, 40);
        c.ctx->BeginGroup();
        c.ctx->SetFillPaint(Color(255, 255, 255, 255));
        c.ctx->FillRectangle(Rect2Dd(0, 0, 20, 40));      // left half opaque
        auto mask = c.ctx->EndGroupAsPattern();
        Check(mask != nullptr, "EndGroupAsPattern returns the group");
        c.ctx->BeginGroup();
        c.ctx->SetFillPaint(Color(0, 0, 255, 255));
        c.ctx->FillRectangle(Rect2Dd(0, 0, 40, 40));
        c.ctx->EndGroupMasked(mask);
        Rgba in = c.Sample(10, 20), out = c.Sample(30, 20);
        Check(in.b > 245 && in.a == 255, "masked group paints where the mask is opaque");
        Check(out.a == 0, "masked group paints nothing where the mask is clear");
    }

    // ===== Hit testing and stroke extents =====
    {
        Canvas c(40, 40);
        c.ctx->Rect(10, 10, 20, 20);
        Check(c.ctx->IsPointInFill(15, 15), "IsPointInFill inside");
        Check(!c.ctx->IsPointInFill(5, 5), "IsPointInFill outside");
        c.ctx->SetStrokeWidth(6);
        Check(c.ctx->IsPointInStroke(10, 20), "IsPointInStroke on the edge");
        Check(c.ctx->IsPointInStroke(8, 20), "IsPointInStroke within half the width outside");
        Check(!c.ctx->IsPointInStroke(20, 20), "IsPointInStroke not in the interior");
        Rect2Dd se = c.ctx->GetStrokeExtents();
        Check(Near(se.x, 7, 0.01) && Near(se.width, 26, 0.01), "GetStrokeExtents grows the box by half the width");
        c.ctx->ClearPath();
    }

    // ===== Transform readback =====
    {
        Canvas c(10, 10);
        c.ctx->Translate(10, 20);
        c.ctx->Scale(2, 4);
        double a, b, cc, d, e, f;
        c.ctx->GetTransform(a, b, cc, d, e, f);
        Check(Near(a, 2, 1e-9) && Near(d, 4, 1e-9) && Near(e, 10, 1e-9) && Near(f, 20, 1e-9),
              "GetTransform returns the CTM");
        Point2Dd dev = c.ctx->UserToDevice(Point2Dd(1, 1));
        Check(Near(dev.x, 12, 1e-9) && Near(dev.y, 24, 1e-9), "UserToDevice");
        Point2Dd usr = c.ctx->DeviceToUser(Point2Dd(12, 24));
        Check(Near(usr.x, 1, 1e-9) && Near(usr.y, 1, 1e-9), "DeviceToUser");
        Check(Near(c.ctx->DeviceToUserDistance(4), (2.0 + 1.0) / 2.0, 1e-9),
              "DeviceToUserDistance averages the axes");
    }

    // ===== Conic gradient =====
    {
        Canvas c(100, 100);
        std::vector<GradientStop> stops = {{0.0, Color(255, 0, 0, 255)}, {1.0, Color(0, 0, 255, 255)}};
        auto conic = c.ctx->CreateConicGradientPattern(50, 50, 0, 2 * M_PI, stops);
        Check(conic != nullptr, "conic gradient pattern is created");
        c.ctx->SetFillPaint(conic);
        c.ctx->FillRectangle(Rect2Dd(0, 0, 100, 100));
        Rgba start = c.Sample(90, 51);   // just past angle 0
        Rgba half = c.Sample(10, 50);    // angle pi
        Rgba end = c.Sample(90, 49);     // just before 2 pi
        Check(start.r > 230 && start.b < 25, "conic: red at the start angle");
        Check(std::abs(half.r - half.b) < 30 && half.r > 90, "conic: halfway is the mid colour");
        Check(end.b > 230 && end.r < 25, "conic: blue just before the end angle");
    }

    // ===== Mesh gradient =====
    {
        Canvas c(100, 100);
        MeshGradientPatch patch;
        patch.corners = {Point2Dd(0, 0), Point2Dd(100, 0), Point2Dd(100, 100), Point2Dd(0, 100)};
        patch.colors = {Color(255, 0, 0, 255), Color(0, 255, 0, 255), Color(0, 0, 255, 255), Color(255, 255, 255, 255)};
        auto mesh = c.ctx->CreateMeshGradientPattern({patch});
        Check(mesh != nullptr, "mesh gradient pattern is created");
        c.ctx->SetFillPaint(mesh);
        c.ctx->FillRectangle(Rect2Dd(0, 0, 100, 100));
        Rgba tl = c.Sample(2, 2), tr = c.Sample(97, 2), br = c.Sample(97, 97);
        Check(tl.r > 230 && tl.g < 30, "mesh: corner 0 colour");
        Check(tr.g > 230 && tr.r < 30, "mesh: corner 1 colour");
        Check(br.b > 230 && br.r < 30, "mesh: corner 2 colour");
    }

    // ===== Pixmap pattern with tiling and placement =====
    {
        UCPixmap tile;
        tile.Init(2, 2);
        uint32_t* px = tile.GetPixelData();
        px[0] = 0xFFFF0000; px[1] = 0xFF00FF00;   // red, green
        px[2] = 0xFF0000FF; px[3] = 0xFFFFFFFF;   // blue, white
        tile.MarkDirty();
        tile.Flush();
        Canvas c(40, 40);
        c.ctx->SetImageSmoothing(false);
        auto pat = c.ctx->CreatePixmapPattern(tile, Rect2Dd(0, 0, 20, 20), PatternExtend::Repeat);
        Check(pat != nullptr, "pixmap pattern is created");
        c.ctx->SetFillPaint(pat);
        c.ctx->FillRectangle(Rect2Dd(0, 0, 40, 40));
        Rgba a = c.Sample(5, 5), b = c.Sample(15, 5), d = c.Sample(35, 35);
        Check(a.r > 245 && a.g < 10, "pixmap pattern: pixel (0,0) stretched to the anchor");
        Check(b.g > 245 && b.r < 10, "pixmap pattern: pixel (1,0)");
        Check(d.r > 245 && d.g > 245 && d.b > 245, "pixmap pattern repeats past the anchor");
        pat->SetMatrix(1, 0, 0, 1, 10, 0);   // shift the pattern right by 10
        c.ctx->SetFillPaint(pat);
        c.ctx->FillRectangle(Rect2Dd(0, 0, 40, 40));
        Rgba shifted = c.Sample(15, 5);
        Check(shifted.r > 245 && shifted.g < 10, "SetMatrix moves the pattern in user space");
    }

    // ===== Antialias off =====
    {
        Canvas c(20, 20);
        c.ctx->SetAntialias(AntialiasMode::NoAntialias);
        c.ctx->SetFillPaint(Color(0, 0, 0, 255));
        c.ctx->FillRectangle(Rect2Dd(5.5, 0, 5, 20));
        Rgba edge = c.Sample(5, 10);
        Check(edge.a == 0 || edge.a == 255, "AntialiasMode::NoAntialias leaves no partial edge pixel");
    }

    // ===== Text outlines =====
    {
        Canvas c(200, 60);
        c.ctx->SetFontFace("Sans", FontWeight::Bold, FontSlant::Normal);
        c.ctx->SetFontSize(32);
        c.ctx->AppendTextPath("Hello", Point2Dd(10, 5));
        Rect2Dd ext = c.ctx->GetPathExtents();
        Check(ext.width > 40 && ext.height > 15, "AppendTextPath yields glyph outlines with extents");
        c.ctx->SetFillPaint(Color(0, 0, 0, 255));
        c.ctx->FillPathPreserve();
        c.ctx->ClearPath();
        bool anyInk = false;
        for (int y = 5; y < 55 && !anyInk; ++y)
            for (int x = 10; x < 190 && !anyInk; ++x)
                if (c.Sample(x, y).a > 128) anyInk = true;
        Check(anyInk, "filling the text path paints glyphs");
    }

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
