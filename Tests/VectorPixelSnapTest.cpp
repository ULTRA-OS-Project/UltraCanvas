// Tests/VectorPixelSnapTest.cpp
// VectorEdit::SnapToPixels / SnapToPixel: the shapes of an icon onto the
// pixel grid of the size it is shown at. Line widths become whole pixels
// (at least one); a shape with an odd width lands on pixel centres, one
// with an even width or no line on pixel edges; rectangles keep a size of
// at least a pixel; circles and ellipses snap by the edges of their bounds;
// paths are made absolute, their nodes snapped and their control points
// moved with their nodes, a curve inside one pixel straightened; a
// group's transform that scales and moves is followed; rotated shapes and
// text are left as they are.
//
// Usage: VectorPixelSnapTest
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework

#include "DataFormats/UltraCanvasVectorEdit.h"
#include "UltraCanvasImage.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace UltraCanvas;
using namespace UltraCanvas::VectorStorage;
using namespace UltraCanvas::VectorEdit;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) { ++failures; std::printf("FAIL: %s\n", what.c_str()); }
    else std::printf("  ok: %s\n", what.c_str());
}

bool Near(double a, double b, double tol = 1e-4) { return std::fabs(a - b) <= tol; }
bool NearRect(const Rect2Dd& r, double x, double y, double w, double h) {
    return Near(r.x, x) && Near(r.y, y) && Near(r.width, w) && Near(r.height, h);
}

std::shared_ptr<VectorRect> MakeRect(double x, double y, double w, double h, float stroke) {
    auto r = std::make_shared<VectorRect>();
    r->Bounds = Rect2Dd(x, y, w, h);
    r->Style.Fill = Color(0, 0, 0, 255);
    if (stroke > 0) {
        StrokeData s;
        s.Width = stroke;
        r->Style.Stroke = s;
    }
    return r;
}

void AddStroke(VectorElement& e, float width) {
    StrokeData s;
    s.Width = width;
    e.Style.Stroke = s;
}

} // namespace

int main() {
    UCImage::InitializeImageSubsysterm("VectorPixelSnapTest");

    // ===== one coordinate =====
    Check(Near(SnapToPixel(2.3, 1.0), 2.0), "a value goes to the nearest pixel edge");
    Check(Near(SnapToPixel(2.3, 1.0, 0.0, true), 2.5), "with centre, to the nearest pixel centre");
    Check(Near(SnapToPixel(2.9, 0.75), 3.0), "pixels of 0.75 units (a px page measured in points)");
    Check(Near(SnapToPixel(10.2, 1.0, 0.4), 10.4), "the grid starts at the origin given");
    Check(Near(SnapToPixel(7.3, 0.0), 7.3), "no pixel size leaves the value alone");

    // ===== rectangles =====
    {
        auto r = MakeRect(3.2, 4.7, 10.1, 8.6, 2.0f);
        const auto res = SnapToPixels({r}, 1.0);
        Check(res.aligned == 1 && res.skipped == 0, "a rectangle is aligned");
        Check(NearRect(r->Bounds, 3, 5, 10, 8), "a 2 px line puts the edges on pixel edges");
        Check(Near(r->Style.Stroke->Width, 2.0), "and keeps its width");
    }
    {
        auto r = MakeRect(3.2, 4.7, 10.1, 8.6, 1.2f);
        SnapToPixels({r}, 1.0);
        Check(Near(r->Style.Stroke->Width, 1.0), "a 1.2 unit line becomes one pixel");
        Check(NearRect(r->Bounds, 3.5, 4.5, 10, 9), "a 1 px line puts the edges on pixel centres");
    }
    {
        auto r = MakeRect(3.2, 4.7, 10.1, 8.6, 0.0f);
        SnapToPixels({r}, 1.0);
        Check(NearRect(r->Bounds, 3, 5, 10, 8), "a fill alone goes to pixel edges");
    }
    {
        auto r = MakeRect(1.0, 1.0, 5.0, 5.0, 0.3f);
        SnapToPixels({r}, 1.0);
        Check(Near(r->Style.Stroke->Width, 1.0), "a hairline is widened to one pixel");
    }
    {
        auto r = MakeRect(2.0, 2.0, 0.2, 0.2, 0.0f);
        SnapToPixels({r}, 1.0);
        Check(r->Bounds.width >= 1.0 - 1e-6 && r->Bounds.height >= 1.0 - 1e-6, "a rectangle never shrinks below a pixel");
    }
    {
        auto r = MakeRect(1.1, 1.1, 10.0, 10.0, 0.0f);
        r->RadiusX = r->RadiusY = 2.4f;
        SnapToPixels({r}, 1.0);
        Check(Near(r->RadiusX, 2.0) && Near(r->RadiusY, 2.0), "corner radii become whole pixels");
    }
    {
        // a 24 px page set up in points: one pixel is 0.75 units
        auto r = MakeRect(2.0, 2.0, 10.0, 10.0, 1.5f);
        SnapToPixels({r}, 0.75);
        Check(Near(r->Style.Stroke->Width, 1.5), "1.5 units is two 0.75 unit pixels");
        Check(Near(std::fmod(r->Bounds.x / 0.75, 1.0), 0.0) && Near(std::fmod((r->Bounds.x + r->Bounds.width) / 0.75, 1.0), 0.0),
              "an even width lands on edges of 0.75 unit pixels");
    }

    // ===== circles and ellipses =====
    {
        auto c = std::make_shared<VectorCircle>();
        c->Center = Point2Dd(10.3, 10.3);
        c->Radius = 5.2f;
        AddStroke(*c, 2.0f);
        SnapToPixels({c}, 1.0);
        const double left = c->Center.x - c->Radius, right = c->Center.x + c->Radius;
        const double top = c->Center.y - c->Radius, bottom = c->Center.y + c->Radius;
        Check(Near(left, std::round(left)) && Near(right, std::round(right)) &&
              Near(top, std::round(top)) && Near(bottom, std::round(bottom)),
              "a circle's bounds land on pixel edges, whatever its diameter");
    }
    {
        auto e = std::make_shared<VectorEllipse>();
        e->Center = Point2Dd(12.0, 12.0);
        e->RadiusX = 8.2f;
        e->RadiusY = 6.1f;
        AddStroke(*e, 1.0f);
        SnapToPixels({e}, 1.0);
        Check(Near(e->Center.x - e->RadiusX, 3.5) && Near(e->Center.x + e->RadiusX, 20.5) &&
              Near(e->Center.y - e->RadiusY, 5.5) && Near(e->Center.y + e->RadiusY, 18.5),
              "an ellipse with a 1 px line has its bounds on pixel centres");
    }

    // ===== lines and polygons =====
    {
        auto l = std::make_shared<VectorLine>();
        l->Start = Point2Dd(2.0, 2.2);
        l->End = Point2Dd(20.0, 2.2);
        AddStroke(*l, 1.0f);
        SnapToPixels({l}, 1.0);
        Check(Near(l->Start.x, 2.5) && Near(l->Start.y, 2.5) && Near(l->End.x, 20.5) && Near(l->End.y, 2.5),
              "a 1 px line runs along pixel centres");
    }
    {
        auto p = std::make_shared<VectorPolygon>();
        p->Points = {Point2Dd(1.2, 1.2), Point2Dd(9.7, 1.4), Point2Dd(5.1, 8.8)};
        p->Style.Fill = Color(0, 0, 0, 255);
        SnapToPixels({p}, 1.0);
        Check(Near(p->Points[0].x, 1) && Near(p->Points[1].x, 10) && Near(p->Points[2].y, 9),
              "a filled polygon's points go to pixel corners");
    }

    // ===== paths =====
    {
        auto path = std::make_shared<VectorPath>();
        path->MoveTo(2.2f, 3.1f);
        path->LineTo(10.3f, 0.0f, true);    // relative
        path->AddCommand(PathCommand{PathCommandType::VerticalLineTo, {5.4f}, true});
        path->ClosePath();
        AddStroke(*path, 2.0f);
        SnapToPixels({path}, 1.0);
        const auto& cmds = path->Path.commands;
        bool absolute = true;
        for (const auto& c : cmds) absolute = absolute && !c.Relative;
        Check(absolute, "every command of a path is made absolute");
        Check(Near(cmds[0].Parameters[0], 2) && Near(cmds[0].Parameters[1], 3), "the move goes to a pixel edge");
        Check(Near(cmds[1].Parameters[0], 13) && Near(cmds[1].Parameters[1], 3), "a relative line's end is snapped where it lands");
        Check(Near(cmds[2].Parameters[0], 9), "a relative vertical line too");
    }
    {
        auto path = std::make_shared<VectorPath>();
        path->MoveTo(0.0f, 0.0f);
        path->CurveTo(1.2f, 0.3f, 5.2f, 3.3f, 6.3f, 4.4f);
        path->Style.Fill = Color(0, 0, 0, 255);
        SnapToPixels({path}, 1.0);
        const auto& v = path->Path.commands[1].Parameters;
        Check(Near(v[4], 6) && Near(v[5], 4), "a curve's end node is snapped");
        Check(Near(v[2], 4.9) && Near(v[3], 2.9), "its second control point moves with it");
        Check(Near(v[0], 1.2) && Near(v[1], 0.3), "its first control point stays with the unmoved start");
    }

    {
        // a bar a pixel high with round ends of half a pixel (Texter's old
        // list icon): the ends cannot be round at this size
        auto path = std::make_shared<VectorPath>();
        path->MoveTo(7.0f, 5.0f);
        path->CurveTo(7.0f, 4.724f, 7.224f, 4.5f, 7.5f, 4.5f);
        path->AddCommand(PathCommand{PathCommandType::HorizontalLineTo, {23.5f}, false});
        path->Style.Fill = Color(0, 0, 0, 255);
        SnapToPixels({path}, 1.0);
        const auto& end = path->Path.commands[1];
        Check(end.Type == PathCommandType::LineTo, "a curve inside one pixel becomes a straight line");
        Check(Near(end.Parameters[0], 8) && Near(end.Parameters[1], 5), "to its snapped end");
    }
    {
        auto path = std::make_shared<VectorPath>();
        path->MoveTo(2.0f, 12.0f);
        path->CurveTo(2.0f, 6.0f, 6.0f, 2.0f, 12.0f, 2.0f);
        path->Style.Fill = Color(0, 0, 0, 255);
        SnapToPixels({path}, 1.0);
        Check(path->Path.commands[1].Type == PathCommandType::CurveTo, "a curve over several pixels stays a curve");
    }

    // ===== transforms, groups and what is left alone =====
    {
        auto group = std::make_shared<VectorGroup>();
        group->Transform = Matrix3x3::Translate(0.4, 0.4);
        auto r = MakeRect(1.0, 1.0, 10.0, 10.0, 0.0f);
        group->AddChild(r);
        const auto res = SnapToPixels({group}, 1.0);
        Check(res.aligned == 1, "a group's children are aligned");
        Check(NearRect(r->Bounds, 0.6, 0.6, 10, 10), "through the group's move, onto the document's grid");
    }
    {
        auto r = MakeRect(1.3, 1.3, 10.0, 10.0, 2.0f);
        r->Transform = Matrix3x3::Rotate(0.5);
        const auto res = SnapToPixels({r}, 1.0);
        Check(res.skipped == 1 && res.aligned == 0, "a rotated shape is skipped");
        Check(NearRect(r->Bounds, 1.3, 1.3, 10, 10), "and left as it was");
    }
    {
        auto t = std::make_shared<VectorText>();
        t->Position = Point2Dd(3.3, 4.4);
        const auto res = SnapToPixels({t}, 1.0);
        Check(res.skipped == 1 && Near(t->Position.x, 3.3), "text is left as it is");
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all checks passed", failures, failures == 1 ? "" : "s");
    return failures;
}
