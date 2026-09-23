// Tests/VectorElementViewTest.cpp
// What UltraCanvasVectorElement has to get right for a drawing to appear on a
// page - the three faults that together made every tile of the DemoApp's
// "DWG / DXF Drawings" page an empty white square:
//
//   1. The element declared no CSS box, so a parent that runs the layout
//      engine arranged it to nothing and skipped it: a container never
//      renders a child that does not intersect its content area.
//   2. ZoomToFit() clamped the fit to options.MinZoom, so a drawing far
//      larger than the element (a site plan in a thumbnail) was pinned at
//      the limit and the view showed an empty patch of its middle.
//   3. Painting used the element's parent-frame origin although the
//      container has already translated the context to it, which doubles
//      the offset as soon as the element is not at (0, 0).
//   4. The fit framed the union of everything, so one forgotten speck far
//      from the rest shrank the drawing into a corner of an empty sheet.
//
// Plus the one that made the rest of the page vanish with it: an element
// carrying a non-invertible transform (a CAD block standing in a vertical
// plane, projected to plan view) must not reach the backend, because Cairo
// latches such a matrix as a permanent error and then draws nothing at all.
//
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-09-22
// Author: UltraCanvas Framework

#include "UltraCanvasContainer.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasVectorElement.h"
#include "DataFormats/UltraCanvasVectorStorage.h"

#include <cairo/cairo.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

using namespace UltraCanvas;
using namespace UltraCanvas::VectorStorage;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

// A white surface; ink is any pixel that is no longer white. Counting ink in a
// rectangle is enough to say "the drawing landed here" and "nothing landed
// there", which is all these tests ask.
struct Canvas {
    std::unique_ptr<IRenderContext> ctx;
    cairo_t* cr = nullptr;

    Canvas(int w, int h) {
        ctx = CreateRenderContext(Size2Di(w, h), nullptr);
        if (!ctx) return;
        cr = static_cast<cairo_t*>(ctx->GetNativeContext());
        if (!cr) return;
        cairo_save(cr);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_paint(cr);
        cairo_restore(cr);
    }

    int InkIn(int x0, int y0, int x1, int y1) const {
        if (!cr) return -1;
        cairo_surface_t* s = cairo_get_target(cr);
        cairo_surface_flush(s);
        if (cairo_image_surface_get_format(s) != CAIRO_FORMAT_ARGB32 &&
            cairo_image_surface_get_format(s) != CAIRO_FORMAT_RGB24) return -1;
        const unsigned char* data = cairo_image_surface_get_data(s);
        const int stride = cairo_image_surface_get_stride(s);
        const int w = cairo_image_surface_get_width(s);
        const int h = cairo_image_surface_get_height(s);
        x1 = std::min(x1, w); y1 = std::min(y1, h);
        int ink = 0;
        for (int y = std::max(0, y0); y < y1; ++y) {
            const uint32_t* row = reinterpret_cast<const uint32_t*>(data + y * stride);
            for (int x = std::max(0, x0); x < x1; ++x) {
                if ((row[x] & 0x00FFFFFFu) != 0x00FFFFFFu) ++ink;
            }
        }
        return ink;
    }

    int Ink() const { return InkIn(0, 0, 1 << 20, 1 << 20); }
};

// A drawing of one black square `side` units across at the origin, in a page
// of the same size.
std::shared_ptr<VectorDocument> SquareDocument(double side) {
    auto doc = std::make_shared<VectorDocument>();
    doc->Size = Size2Dd{side, side};
    auto layer = doc->AddLayer("drawing");
    auto rect = std::make_shared<VectorRect>();
    rect->Bounds = Rect2Dd(0, 0, side, side);
    rect->Style.Fill = Colors::Black;
    layer->Children.push_back(rect);
    return doc;
}

// ===== 1. THE ELEMENT SURVIVES ITS PARENT'S LAYOUT =====
void TestKeepsItsBoxThroughLayout() {
    std::cout << "\nA vector element built with x, y, width and height\n";
    auto tile = std::make_shared<UltraCanvasContainer>("Tile", 0, 0, 300, 240);
    auto element = CreateVectorElement("Drawing", 10, 10, 280, 190);
    tile->AddChild(element);

    CSSLayout::LayoutContext lctx;
    CSSLayout::MeasureConstraints mc{ { CSSLayout::ConstraintMode::Exact, 300.0f },
                                      { CSSLayout::ConstraintMode::Exact, 240.0f } };
    tile->Measure(mc, lctx);
    tile->Arrange(Rect2Df{0, 0, 300, 240}, lctx);

    const Rect2Df b = element->GetBounds();
    Check(b.width == 280 && b.height == 190,
          "keeps that size when its container runs the layout engine");
    Check(b.x == 10 && b.y == 10, "and stays where it was placed");
}

// ===== 2. A FIT IS A FIT, NOT A ZOOM LIMIT =====
void TestFitsADrawingLargerThanTheZoomLimit() {
    std::cout << "\nA drawing far larger than the element\n";
    auto element = CreateVectorElement("Thumb", 0, 0, 280, 190);
    VectorElementOptions opts = element->GetOptions();
    opts.MinZoom = 0.1f;                       // the default: 10x, far too big
    element->SetOptions(opts);
    element->SetDocument(SquareDocument(10000));   // needs about 0.017

    Check(element->GetZoom() < opts.MinZoom,
          "is fitted below options.MinZoom instead of being pinned at it");
    Check(element->GetZoom() * 10000 <= 190,
          "so the whole drawing is inside the element");

    Canvas canvas(280, 190);
    if (!canvas.ctx) { Check(false, "no context"); return; }
    element->Render(canvas.ctx.get(), Rect2Df(0, 0, 280, 190));
    const int ink = canvas.Ink();
    Check(ink > 1000, "and the drawing is actually on the surface");
    Check(ink < 280 * 190, "without covering every pixel of it");
}

// ===== 2b. A SPECK FAR FROM THE DRAWING DOES NOT DECIDE THE FIT =====
// Builds a dense drawing plus one stray hairline a long way off, the shape
// of media/vector/DWG/womans hostel.dwg, where four 4 x 0.7 unit drawables a
// quarter of a million units from the plans filled 97% of the page.
std::shared_ptr<VectorDocument> DrawingWithASpeck(bool withSpeck) {
    auto doc = std::make_shared<VectorDocument>();
    doc->Size = Size2Dd{10000, 10000};
    auto layer = doc->AddLayer("plans");
    for (int i = 0; i < 400; ++i) {          // the drawing: a dense 500-unit block
        auto r = std::make_shared<VectorRect>();
        r->Bounds = Rect2Dd(9000 + (i % 20) * 25, 9000 + (i / 20) * 25, 20, 20);
        r->Style.Fill = Colors::Black;
        layer->Children.push_back(r);
    }
    if (withSpeck) {
        auto speck = std::make_shared<VectorRect>();
        speck->Bounds = Rect2Dd(10, 10, 4, 1);
        speck->Style.Fill = Colors::Black;
        layer->Children.push_back(speck);
    }
    return doc;
}

void TestSpeckDoesNotDecideTheFit() {
    std::cout << "\nA drawing with one speck far from everything else\n";
    auto clean = DrawingWithASpeck(false);
    auto speckled = DrawingWithASpeck(true);

    const Rect2Dd all = speckled->GetBoundingBox();
    const Rect2Dd content = ContentBounds(*speckled);
    Check(all.width > 9000, "the union of everything still spans the whole sheet");
    Check(content.width < 1000 && content.height < 1000,
          "ContentBounds frames the drawing, not the speck");
    Check(std::abs(content.width - ContentBounds(*clean).width) < 1.0,
          "and frames exactly what the same drawing without the speck does");

    auto withSpeck = CreateVectorElement("Speckled", 0, 0, 280, 190);
    withSpeck->SetDocument(speckled);
    auto without = CreateVectorElement("Clean", 0, 0, 280, 190);
    without->SetDocument(clean);
    Check(std::abs(withSpeck->GetZoom() - without->GetZoom()) < 0.001f,
          "so the speck does not change the zoom the view fits at");

    // A frame or a title block is not a speck: entities at the edge of a
    // drawing, however few, are part of it because they are not far away.
    auto framed = DrawingWithASpeck(false);
    for (int i = 0; i < 4; ++i) {            // four lines just outside the block
        auto edge = std::make_shared<VectorRect>();
        edge->Bounds = Rect2Dd(8950 + (i % 2) * 600, 8950 + (i / 2) * 600, 5, 5);
        edge->Style.Fill = Colors::Black;
        framed->Layers.front()->Children.push_back(edge);
    }
    const Rect2Dd framedBox = ContentBounds(*framed);
    Check(framedBox.width > 600 && framedBox.height > 600,
          "a frame around the drawing is kept, not trimmed off as an outlier");
}

// ===== 3. PAINTING IS ELEMENT-LOCAL =====
void TestDrawsInElementLocalCoordinates() {
    std::cout << "\nAn element painted by a container that has translated to it\n";
    auto element = CreateVectorElement("Offset", 40, 30, 100, 100);
    element->SetDocument(SquareDocument(100));

    Canvas canvas(200, 200);
    if (!canvas.ctx) { Check(false, "no context"); return; }
    // What UltraCanvasContainer::Render does before it calls a child.
    canvas.ctx->PushState();
    canvas.ctx->Translate(40, 30);
    element->Render(canvas.ctx.get(), Rect2Df(0, 0, 100, 100));
    canvas.ctx->PopState();

    Check(canvas.InkIn(40, 30, 140, 130) > 1000,
          "draws inside the box the container gave it");
    Check(canvas.InkIn(140, 130, 200, 200) == 0,
          "and not a second offset further down the surface");
}

// ===== 4. A COLLAPSED TRANSFORM DOES NOT END ALL DRAWING =====
void TestSingularTransformLeavesTheContextUsable() {
    std::cout << "\nA drawing with a collapsed (non-invertible) transform\n";
    auto doc = SquareDocument(100);
    auto flat = std::make_shared<VectorRect>();
    flat->Bounds = Rect2Dd(0, 0, 100, 100);
    flat->Style.Fill = Colors::Black;
    // A block standing in a vertical plane, projected to plan view: one axis
    // scaled to zero. Cairo would latch this as CAIRO_STATUS_INVALID_MATRIX.
    Matrix3x3 collapsed = Matrix3x3::Identity();
    collapsed.m[0][0] = 0; collapsed.m[0][1] = 0;
    collapsed.m[1][0] = -1; collapsed.m[1][1] = 0;
    flat->Transform = collapsed;
    doc->Layers.front()->Children.push_back(flat);

    auto element = CreateVectorElement("Flat", 0, 0, 200, 200);
    element->SetDocument(doc);

    Canvas canvas(300, 300);
    if (!canvas.ctx) { Check(false, "no context"); return; }
    element->Render(canvas.ctx.get(), Rect2Df(0, 0, 200, 200));
    const int drawingInk = canvas.Ink();
    Check(drawingInk > 1000, "still draws the rest of the drawing");

    // Everything painted after it - the rest of the page, in the demo that
    // found this - has to reach the surface too.
    canvas.ctx->SetFillPaint(Colors::Black);
    canvas.ctx->FillRectangle(Rect2Dd(210, 210, 80, 80));
    Check(canvas.InkIn(210, 210, 290, 290) > 6000,
          "and leaves the context able to draw what comes after it");
}

// ===== 5. THE HOST'S OWN EVENT CALLBACK IS HEARD =====
void TestEventCallbackRuns() {
    std::cout << "\nAn element whose host set an event callback\n";
    auto element = CreateVectorElement("Clickable", 0, 0, 100, 100);
    element->SetDocument(SquareDocument(100));
    bool clicked = false;
    element->SetEventCallback([&clicked](const UCEvent& e) {
        if (e.type == UCEventType::MouseUp) { clicked = true; return true; }
        return false;
    });

    UCEvent up;
    up.type = UCEventType::MouseUp;
    up.pointer.x = 50;
    up.pointer.y = 50;
    Check(element->OnEvent(up), "reports the event as handled");
    Check(clicked, "and the callback ran - a click on a drawing can open it");
}

} // namespace

int main() {
    std::cout << "=== UltraCanvasVectorElement view tests ===\n";
    TestKeepsItsBoxThroughLayout();
    TestFitsADrawingLargerThanTheZoomLimit();
    TestSpeckDoesNotDecideTheFit();
    TestDrawsInElementLocalCoordinates();
    TestSingularTransformLeavesTheContextUsable();
    TestEventCallbackRuns();

    std::cout << (g_failures == 0 ? "\nPASSED: 0 failure(s)\n"
                                  : "\nFAILED: " + std::to_string(g_failures) + " failure(s)\n");
    return g_failures;
}
