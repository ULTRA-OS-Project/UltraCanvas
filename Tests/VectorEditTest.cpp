// Tests/VectorEditTest.cpp
// The vector editing layer: UltraCanvasBezierPath (storage round trip,
// de Casteljau split, node-type rules, outline and node hit tests, polyline
// fitting), VectorEdit::VectorSelection (bounds, rebinding after undo),
// VectorEdit::VectorHistory (undo / redo / cancel / coalesce / budget),
// VectorEdit::VectorHitTester (fill, stroke, tolerance, transforms,
// locked layers) and the operations (transform about a pivot, z-order,
// group / ungroup with placement preserved, align, distribute, duplicate,
// convert to path, bake transform).
//
// Usage: VectorEditTest
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-09-15
// Author: UltraCanvas Framework

#include "UltraCanvasBezierPath.h"
#include "DataFormats/UltraCanvasVectorEdit.h"
#include "DataFormats/UltraCanvasVectorGeometry.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasVectorCanvas.h"
#include "UltraCanvasGradientEditor.h"

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

bool Near(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }
bool NearPt(const Point2Dd& p, double x, double y, double tol = 1e-6) { return Near(p.x, x, tol) && Near(p.y, y, tol); }
bool NearRect(const Rect2Dd& r, double x, double y, double w, double h, double tol = 1e-6) {
    return Near(r.x, x, tol) && Near(r.y, y, tol) && Near(r.width, w, tol) && Near(r.height, h, tol);
}

std::shared_ptr<VectorRect> MakeRect(double x, double y, double w, double h, const std::string& id = "") {
    auto r = std::make_shared<VectorRect>();
    r->Bounds = Rect2Dd{x, y, w, h};
    r->Style.Fill = Color(0, 0, 0, 255);
    r->Id = id;
    return r;
}

} // namespace

int main() {
    UCImage::InitializeImageSubsysterm("VectorEditTest");

    // ===== Bezier path: storage round trip =====
    {
        auto bp = UltraCanvasBezierPath::FromSVGPathData("M 10 10 L 100 10 C 120 10 140 30 140 50 Q 140 90 100 90 Z");
        Check(bp.has_value(), "SVG path data parses into a Bezier path");
        if (bp) {
            Check(bp->subpaths.size() == 1 && bp->subpaths[0].closed, "one closed subpath");
            Check(bp->subpaths[0].nodes.size() == 4, "four nodes (the quadratic became a cubic)");
            Check(bp->subpaths[0].SegmentIsLine(0) && !bp->subpaths[0].SegmentIsLine(1), "line and curve segments are told apart");
            Check(bp->subpaths[0].nodes[2].handleInActive && bp->subpaths[0].nodes[1].handleOutActive, "cubic handles are active");
            PathData back = bp->ToPathData();
            Check(back.commands.size() == 5, "writes back as M L C C Z");
            auto again = UltraCanvasBezierPath::FromPathData(back);
            Check(again.subpaths.size() == 1 && again.subpaths[0].nodes.size() == 4, "round trip keeps the node count");
        }
    }

    // ===== Bezier path: split, node types, hit tests =====
    {
        UltraCanvasBezierSubpath sp;
        BezierNode a; a.anchor = {0, 0}; a.handleOut = {0, 50}; a.handleOutActive = true;
        BezierNode b; b.anchor = {100, 0}; b.handleIn = {100, 50}; b.handleInActive = true;
        sp.nodes = {a, b};
        const Point2Dd mid = sp.EvaluateSegment(0, 0.5);
        Check(NearPt(mid, 50, 37.5), "cubic evaluates (midpoint of a symmetric arch)");
        const int idx = sp.InsertNodeAt(0, 0.5);
        Check(idx == 1 && sp.nodes.size() == 3, "InsertNodeAt adds a node between");
        Check(NearPt(sp.nodes[1].anchor, 50, 37.5), "the new node sits on the curve");
        // Shape preserved: the point at t=0.25 of the original equals t=0.5 of the first half.
        UltraCanvasBezierSubpath orig; orig.nodes = {a, b};
        Check(NearPt(sp.EvaluateSegment(0, 0.5), orig.EvaluateSegment(0, 0.25).x, orig.EvaluateSegment(0, 0.25).y, 1e-6),
              "the split preserves the curve's shape");
        Check(sp.nodes[1].type == BezierNodeType::Smooth, "a split node is smooth");
        sp.SetNodeType(1, BezierNodeType::Symmetric);
        const Point2Dd& n = sp.nodes[1].anchor;
        const double lin = std::hypot(sp.nodes[1].handleIn.x - n.x, sp.nodes[1].handleIn.y - n.y);
        const double lout = std::hypot(sp.nodes[1].handleOut.x - n.x, sp.nodes[1].handleOut.y - n.y);
        Check(Near(lin, lout, 1e-6), "Symmetric makes the handles equal");
        sp.MoveHandle(1, true, Point2Dd(n.x + 30, n.y));
        Check(NearPt(sp.nodes[1].handleIn, n.x - 30, n.y, 1e-6), "moving one handle of a symmetric node mirrors the other");
        sp.SetNodeType(1, BezierNodeType::Corner);
        sp.MoveHandle(1, true, Point2Dd(n.x + 30, n.y + 30));
        Check(NearPt(sp.nodes[1].handleIn, n.x - 30, n.y, 1e-6), "a corner node's handles are independent");
        auto hit = sp.HitTestOutline(Point2Dd(50, 40), 5);
        Check(hit.has_value() && hit->distance < 5, "HitTestOutline finds the curve near a point");
        Check(!sp.HitTestOutline(Point2Dd(50, 200), 5).has_value(), "HitTestOutline misses far away");
        UltraCanvasBezierSubpath square;
        for (auto p : {Point2Dd(0, 0), Point2Dd(10, 0), Point2Dd(10, 10), Point2Dd(0, 10)}) { BezierNode q; q.anchor = p; square.nodes.push_back(q); }
        square.closed = true;
        Check(square.ContainsPoint(Point2Dd(5, 5)) && !square.ContainsPoint(Point2Dd(15, 5)), "ContainsPoint on a closed polygon");
        Check(NearRect(square.Bounds(), 0, 0, 10, 10), "Bounds of a square");
    }

    // ===== Bezier path: polyline fitting =====
    {
        std::vector<Point2Dd> pts;
        for (int i = 0; i <= 100; ++i) pts.emplace_back(i, std::sin(i / 10.0) * 20 + (i % 2) * 0.2);
        auto fitted = UltraCanvasBezierPath::FromPolyline(pts, false, 1.0);
        Check(fitted.subpaths.size() == 1 && fitted.subpaths[0].nodes.size() < 40 && fitted.subpaths[0].nodes.size() > 4,
              "FromPolyline simplifies a noisy stroke to a handful of smooth nodes");
        Check(fitted.subpaths[0].nodes[1].type == BezierNodeType::Smooth, "fitted interior nodes are smooth");
        auto simple = SimplifyPolyline({{0, 0}, {5, 0.1}, {10, 0}}, 0.5);
        Check(simple.size() == 2, "Douglas-Peucker drops a point within tolerance");
    }

    // ===== Document, selection, hit testing =====
    auto doc = std::make_shared<VectorDocument>();
    doc->Size = Size2Dd{400, 300};
    doc->ViewBox = Rect2Dd{0, 0, 400, 300};
    auto layer = doc->AddLayer("Layer 1");
    auto r1 = MakeRect(10, 10, 100, 50, "r1");
    auto r2 = MakeRect(50, 30, 100, 50, "r2");
    r2->Style.Fill = Color(255, 0, 0, 255);
    auto circle = std::make_shared<VectorCircle>();
    circle->Center = {300, 150}; circle->Radius = 40; circle->Id = "c1";
    circle->Style.Fill.reset();
    StrokeData st; st.Fill = Color(0, 0, 255, 255); st.Width = 4;
    circle->Style.Stroke = st;
    auto group = std::make_shared<VectorGroup>();
    group->Id = "g1";
    group->Transform = Matrix3x3::Translate(200, 200);
    auto inner = MakeRect(0, 0, 20, 20, "inner");
    group->AddChild(inner);
    layer->AddChild(r1);
    layer->AddChild(r2);
    layer->AddChild(circle);
    layer->AddChild(group);
    EnsureIds(*doc);

    {
        Check(TopLevelOf(inner) == group, "TopLevelOf climbs to the layer's child");
        Check(LayerOf(inner) == layer, "LayerOf finds the layer");
        Check(NearRect(DocumentBounds(inner), 200, 200, 20, 20), "DocumentBounds maps through the group's transform");

        VectorSelection sel;
        int fired = 0;
        sel.AddListener([&]() { ++fired; });
        sel.Set({r1, r2});
        Check(sel.Count() == 2 && fired == 1, "Set selects and notifies once");
        Check(NearRect(sel.Bounds(), 10, 10, 140, 70), "selection bounds are the union");
        sel.Toggle(r2);
        Check(sel.Count() == 1 && !sel.Contains(r2), "Toggle removes");

        VectorHitTester tester;
        auto h = tester.HitTest(*doc, Point2Dd(60, 40), 1.0);
        Check(h && h->element == r2 && h->onFill, "the topmost filled shape wins the hit");
        h = tester.HitTest(*doc, Point2Dd(20, 20), 1.0);
        Check(h && h->element == r1, "a shape below is hit where it is not covered");
        h = tester.HitTest(*doc, Point2Dd(300, 150), 1.0);
        Check(!h.has_value(), "an unfilled circle's interior does not hit");
        h = tester.HitTest(*doc, Point2Dd(340, 150), 1.0);
        Check(h && h->element == circle && h->onStroke, "an unfilled circle's stroke hits");
        h = tester.HitTest(*doc, Point2Dd(344, 150), 1.0);
        Check(!h.has_value(), "outside the stroke plus tolerance misses");
        h = tester.HitTest(*doc, Point2Dd(344, 150), 4.0);
        Check(h && h->element == circle, "tolerance widens the stroke hit zone");
        h = tester.HitTest(*doc, Point2Dd(210, 210), 1.0);
        Check(h && h->element == inner && h->topLevel == group, "hits go through a group's transform and report the top-level group");
        layer->Locked = true;
        Check(!tester.HitTest(*doc, Point2Dd(60, 40), 1.0).has_value(), "a locked layer is skipped");
        Check(tester.HitTest(*doc, Point2Dd(60, 40), 1.0, false).has_value(), "unless asked to include it");
        layer->Locked = false;
        auto in = tester.ElementsIn(*doc, Rect2Dd(0, 0, 160, 100), true);
        Check(in.size() == 2, "ElementsIn(fullyInside) finds the two rectangles");
        in = tester.ElementsIn(*doc, Rect2Dd(0, 0, 60, 40), false);
        Check(in.size() == 2, "ElementsIn(overlap) finds both rectangles touched");
    }

    // ===== History =====
    {
        VectorHistory history(doc);
        VectorSelection sel;
        sel.Set(r1);
        int changes = 0;
        history.onChanged = [&]() { ++changes; sel.Rebind(*doc); };
        history.Record("Move", [&]() { TranslateElements({r1}, 30, 0); });
        Check(NearRect(DocumentBounds(r1), 40, 10, 100, 50), "TranslateElements moves the element");
        Check(history.CanUndo() && history.UndoLabel() == "Move" && changes == 1, "history has one labelled entry");
        history.Undo();
        Check(!history.CanUndo() && history.CanRedo(), "Undo empties the undo stack and fills redo");
        auto r1Again = doc->FindElementById("r1");
        Check(r1Again && NearRect(DocumentBounds(r1Again), 10, 10, 100, 50), "the document is back where it was");
        Check(sel.Count() == 1 && sel.First() == r1Again, "the selection rebinds to the restored element by Id");
        history.Redo();
        r1Again = doc->FindElementById("r1");
        Check(NearRect(DocumentBounds(r1Again), 40, 10, 100, 50), "Redo re-applies");
        history.Record("Nudge", [&]() { TranslateElements({doc->FindElementById("r1")}, 1, 0); });
        history.Record("Nudge", [&]() { TranslateElements({doc->FindElementById("r1")}, 1, 0); }, true);
        Check(history.UndoCount() == 2, "coalesced nudges are one entry");
        history.Undo();
        Check(NearRect(DocumentBounds(doc->FindElementById("r1")), 40, 10, 100, 50), "undoing the coalesced entry removes both nudges");
        history.BeginEdit("Cancelled");
        TranslateElements({doc->FindElementById("r1")}, 500, 500);
        history.CancelEdit();
        Check(NearRect(DocumentBounds(doc->FindElementById("r1")), 40, 10, 100, 50), "CancelEdit restores the start of the edit");
        history.SetMemoryLimit(1);
        Check(history.UndoCount() == 1, "the memory budget keeps at least one entry");
        // Re-take the live pointers: the snapshots replaced the objects.
        r1 = std::dynamic_pointer_cast<VectorRect>(doc->FindElementById("r1"));
        r2 = std::dynamic_pointer_cast<VectorRect>(doc->FindElementById("r2"));
        circle = std::dynamic_pointer_cast<VectorCircle>(doc->FindElementById("c1"));
        group = std::dynamic_pointer_cast<VectorGroup>(doc->FindElementById("g1"));
        inner = std::dynamic_pointer_cast<VectorRect>(doc->FindElementById("inner"));
        layer = doc->Layers.front();
        Check(r1 && r2 && circle && group && inner && ParentOf(inner) == group, "restored elements keep their tree (parents re-linked)");
    }

    // ===== Operations =====
    {
        // Rotate about a pivot: the box's centre stays put.
        RotateElements({r2}, M_PI / 2, Point2Dd(100, 55));
        Rect2Dd b = DocumentBounds(r2);
        Check(Near(b.x + b.width / 2, 100, 1e-6) && Near(b.y + b.height / 2, 55, 1e-6) && Near(b.width, 50, 1e-6),
              "RotateElements turns about the pivot");
        RotateElements({r2}, -M_PI / 2, Point2Dd(100, 55));
        Check(NearRect(DocumentBounds(r2), 50, 30, 100, 50, 1e-6), "rotating back restores the box");
        ScaleElements({r2}, 2, 2, Point2Dd(50, 30));
        Check(NearRect(DocumentBounds(r2), 50, 30, 200, 100, 1e-6), "ScaleElements about the top-left corner");
        BakeTransform(r2);
        Check(!r2->Transform.has_value() && NearRect(r2->Bounds, 50, 30, 200, 100, 1e-6), "BakeTransform writes a scale into the rectangle");
        ScaleElements({r2}, 0.5, 0.5, Point2Dd(50, 30));
        BakeTransform(r2);

        // Inside a translated group, a document-space move is compensated.
        TranslateElements({inner}, 10, 10);
        Check(NearRect(DocumentBounds(inner), 210, 210, 20, 20, 1e-6), "moving a child of a transformed group lands in document space");

        // Z-order.
        ReorderElements({r1}, ZOrderMove::ToFront);
        Check(layer->Children.back() == r1, "ToFront");
        ReorderElements({r1}, ZOrderMove::Backward);
        Check(layer->Children[2] == r1, "Backward one step");
        ReorderElements({r1}, ZOrderMove::ToBack);
        Check(layer->Children.front() == r1, "ToBack");
        ReorderElements({r1}, ZOrderMove::Forward);
        Check(layer->Children[1] == r1, "Forward one step");

        // Group / ungroup keeps placement.
        const Rect2Dd innerBefore = DocumentBounds(inner);
        auto g2 = GroupElements({r1, inner});
        Check(g2 && ParentOf(r1) == g2 && ParentOf(inner) == g2 && ParentOf(g2) == group, "GroupElements gathers elements from different parents into the topmost member's parent");
        Check(NearRect(DocumentBounds(inner), innerBefore.x, innerBefore.y, innerBefore.width, innerBefore.height, 1e-6),
              "grouping preserves document placement");
        auto freed = UngroupElements({g2});
        Check(freed.size() == 2 && ParentOf(r1) == group && ParentOf(inner) == group, "UngroupElements returns the children to the group's parent (the topmost member's)");
        Check(NearRect(DocumentBounds(inner), innerBefore.x, innerBefore.y, innerBefore.width, innerBefore.height, 1e-6),
              "ungrouping preserves document placement");

        // Align and distribute.
        auto a1 = MakeRect(0, 0, 10, 10), a2 = MakeRect(50, 20, 10, 10), a3 = MakeRect(200, 40, 10, 10);
        layer->AddChild(a1); layer->AddChild(a2); layer->AddChild(a3);
        AlignElements({a1, a2, a3}, AlignMode::Top);
        Check(Near(DocumentBounds(a2).y, 0) && Near(DocumentBounds(a3).y, 0), "AlignElements to the top of the selection");
        AlignElements({a1, a2, a3}, AlignMode::Right, Rect2Dd(0, 0, 400, 300));
        Check(Near(DocumentBounds(a1).x, 390), "AlignElements to a reference (the page)");
        AlignElements({a1}, AlignMode::Left, Rect2Dd(0, 0, 400, 300));
        AlignElements({a2}, AlignMode::HorizontalCenter, Rect2Dd(0, 0, 400, 300));
        DistributeElements({a1, a2, a3}, DistributeMode::HorizontalCenters);
        Check(Near(DocumentBounds(a2).x + 5, (5 + 395) / 2.0), "DistributeElements spaces centres evenly");

        // Duplicate, delete, convert.
        auto dups = DuplicateElements({a1}, 5, 5);
        Check(dups.size() == 1 && dups[0]->Id != a1->Id && NearRect(DocumentBounds(dups[0]), 5, 5, 10, 10), "DuplicateElements clones with an offset and a fresh Id");
        Check(IndexInParent(dups[0]) == IndexInParent(a1) + 1, "the duplicate sits just above the original");
        DeleteElements({dups[0], a2, a3});
        Check(!doc->FindElementById(dups[0]->Id) && ParentOf(a2) == nullptr, "DeleteElements detaches");
        auto asPath = ConvertToPath(circle);
        Check(asPath && asPath->Type == VectorElementType::Path && ParentOf(asPath) == layer && !doc->FindElementById("c1")->Style.Fill.has_value(),
              "ConvertToPath replaces the circle in place, keeping style and Id");
        Check(asPath->Path.commands.size() >= 5, "the circle became four cubics");
        auto outline = OutlineOf(*r1);
        Check(outline && outline->commands.size() == 6 && outline->commands.back().Type == PathCommandType::ClosePath, "OutlineOf a rectangle is M L L L L Z");
    }


    // ===== Canvas: view mapping, snapping, handles, guides (no window) =====
    {
        auto cdoc = std::make_shared<VectorDocument>();
        cdoc->Size = Size2Dd{400, 300};
        auto clayer = cdoc->AddLayer("L");
        auto box = MakeRect(100, 100, 100, 50, "box");
        clayer->AddChild(box);
        auto canvas = CreateVectorCanvas("test");
        canvas->SetBounds(0, 0, 840, 620);          // 20 px rulers -> an 820 x 600 canvas area
        canvas->SetDocument(cdoc);
        canvas->ZoomToPage();
        const auto& v = canvas->GetView();
        Check(Near(v.zoom, (600 - 48) / 300.0, 1e-9), "ZoomToPage fits the page height with a margin");
        Rect2Dd area = canvas->CanvasArea();
        Check(NearRect(area, 20, 20, 820, 600), "CanvasArea excludes the rulers");
        Point2Dd pageCentreView = canvas->DocToView(Point2Dd(200, 150));
        Check(Near(pageCentreView.x, 20 + 410, 1e-6) && Near(pageCentreView.y, 20 + 300, 1e-6), "the page is centred in the canvas area");
        Point2Dd back = canvas->ViewToDoc(Point2Di(static_cast<int>(pageCentreView.x), static_cast<int>(pageCentreView.y)));
        Check(Near(back.x, 200, 1.0) && Near(back.y, 150, 1.0), "ViewToDoc inverts DocToView");
        canvas->SetZoom(2.0);
        Check(Near(canvas->GetZoom(), 2.0, 1e-9), "SetZoom");
        Check(Near(canvas->PixelsToDoc(4), 2.0, 1e-9), "PixelsToDoc divides by the zoom");
        canvas->SetZoomAt(4.0, Point2Di(420, 320));
        Point2Dd under = canvas->ViewToDoc(Point2Di(420, 320));
        canvas->SetZoomAt(8.0, Point2Di(420, 320));
        Point2Dd underAfter = canvas->ViewToDoc(Point2Di(420, 320));
        Check(Near(under.x, underAfter.x, 1e-6) && Near(under.y, underAfter.y, 1e-6), "SetZoomAt keeps the point under the cursor");
        canvas->ZoomToPage();

        VectorGridSpec g; g.visible = true; g.spacing = 10; g.subdivisions = 2;
        canvas->SetGrid(g);
        VectorSnapOptions so; so.toGrid = true; so.toGuides = true; so.toPage = false; so.radiusPixels = 6;
        canvas->SetSnapOptions(so);
        VectorSnapResult sr;
        Point2Dd snapped = canvas->Snap(Point2Dd(13.2, 47.1), &sr);
        Check(Near(snapped.x, 15, 1e-9) && Near(snapped.y, 45, 1e-9) && sr.x == VectorSnapResult::Kind::Grid,
              "Snap to the grid's subdivisions");
        canvas->AddGuide({false, 14.0});
        snapped = canvas->Snap(Point2Dd(13.2, 47.1), &sr);
        Check(Near(snapped.x, 14, 1e-9) && sr.x == VectorSnapResult::Kind::Guide && sr.y == VectorSnapResult::Kind::Grid,
              "a guide beats the grid on its axis");
        canvas->RemoveGuide(0);
        so.toGrid = false; so.toObjects = true; so.toGuides = false;
        canvas->SetSnapOptions(so);
        snapped = canvas->Snap(Point2Dd(101.5, 126.0), &sr);
        Check(Near(snapped.x, 100, 1e-9) && Near(snapped.y, 125, 1e-9) && sr.x == VectorSnapResult::Kind::Object,
              "Snap to an object's edge and centre");
        Point2Dd far = canvas->Snap(Point2Dd(60, 60), &sr);
        Check(Near(far.x, 60, 1e-9) && !sr.Snapped(), "nothing within the radius: no snap");

        canvas->GetSelection()->Set(box);
        Check(NearRect(canvas->SelectionBounds(), 100, 100, 100, 50), "SelectionBounds follows the selection");
        Point2Dd tl = canvas->DocToView(Point2Dd(100, 100));
        Check(canvas->HitTestHandle(Point2Di(static_cast<int>(tl.x), static_cast<int>(tl.y))) == VectorHandle::TopLeft, "HitTestHandle finds the top-left handle");
        Point2Dd mid = canvas->DocToView(Point2Dd(150, 125));
        Check(canvas->HitTestHandle(Point2Di(static_cast<int>(mid.x), static_cast<int>(mid.y))) == VectorHandle::Body, "inside the bounds is Body");
        Check(canvas->HitTestHandle(Point2Di(25, 25)) == VectorHandle::NoHandle, "outside is no handle");
        canvas->SetHandleMode(VectorHandleMode::Rotate);
        Check(canvas->HitTestHandle(Point2Di(static_cast<int>(mid.x), static_cast<int>(mid.y))) == VectorHandle::Center, "in rotate mode the centre handle is live");
        Check(NearPt(canvas->GetRotationCenter(), 150, 125), "the rotation centre defaults to the selection's centre");
        auto hit = canvas->HitTest(Point2Dd(150, 125));
        Check(hit && hit->element == box, "the canvas hit-tests the document");
        canvas->GetSelection()->Clear();
        Check(canvas->HitTestHandle(Point2Di(static_cast<int>(mid.x), static_cast<int>(mid.y))) == VectorHandle::NoHandle, "no selection, no handles");
    }


    // ===== Gradient editor (no window) =====
    {
        auto ge = CreateGradientEditor("ramp", 0, 0, 240, 44);
        int changes = 0, selections = 0;
        ge->onStopsChanged = [&]() { ++changes; };
        ge->onSelectionChanged = [&](int) { ++selections; };
        ge->SetStops({{1.0, Color(0, 0, 255, 255)}, {0.0, Color(255, 0, 0, 255)}});
        Check(ge->GetStops().size() == 2 && Near(ge->GetStops()[0].position, 0.0), "SetStops sorts by position");
        Color mid = ge->ColorAt(0.5);
        Check(mid.r > 120 && mid.r < 136 && mid.b > 120 && mid.b < 136, "ColorAt interpolates");
        int idx = ge->AddStopAt(0.25);
        Check(idx == 1 && ge->GetStops().size() == 3 && changes == 1 && ge->GetSelectedStop() == 1, "AddStopAt inserts in order, selects and notifies");
        Check(ge->GetStops()[1].color.r > 180 && ge->GetStops()[1].color.b < 80, "the added stop takes the ramp's colour");
        ge->SetStopPosition(1, 0.9);
        Check(Near(ge->GetStops()[1].position, 0.9) && ge->GetSelectedStop() == 1, "SetStopPosition keeps the stops sorted and the selection tracked");
        ge->SetStopPosition(1, 0.5);
        ge->Reverse();
        Check(Near(ge->GetStops()[0].position, 0.0) && ge->GetStops()[0].color.b == 255, "Reverse flips the ramp");
        Check(ge->RemoveStop(1) && ge->GetStops().size() == 2, "a middle stop can be removed");
        Check(!ge->RemoveStop(0) && ge->GetStops().size() == 2, "two stops is the minimum: no stop can be removed");
        Check(selections >= 1, "selection changes notify");
    }

    // ===== Geometry: booleans, offsetting, combine shapes =====
    {
        auto square = [](double x, double y, double s) {
            PolygonSet set;
            set.push_back({Point2Dd(x, y), Point2Dd(x + s, y), Point2Dd(x + s, y + s), Point2Dd(x, y + s)});
            return set;
        };
        auto area = [](const PolygonSet& s) { return std::fabs(PolygonSetArea(s)); };
        const PolygonSet a = square(0, 0, 100), b = square(50, 0, 100);
        const PolygonSet u = PolygonBoolean(a, VectorStorage::FillRule::NonZero, b, VectorStorage::FillRule::NonZero, PathBooleanOp::Union);
        const PolygonSet in = PolygonBoolean(a, VectorStorage::FillRule::NonZero, b, VectorStorage::FillRule::NonZero, PathBooleanOp::Intersect);
        const PolygonSet sub = PolygonBoolean(a, VectorStorage::FillRule::NonZero, b, VectorStorage::FillRule::NonZero, PathBooleanOp::Subtract);
        const PolygonSet ex = PolygonBoolean(a, VectorStorage::FillRule::NonZero, b, VectorStorage::FillRule::NonZero, PathBooleanOp::Exclude);
        Check(u.size() == 1 && Near(area(u), 15000, 1e-6), "union of two overlapping squares: one ring, area 15000");
        Check(in.size() == 1 && Near(area(in), 5000, 1e-6), "intersection: one ring, area 5000");
        Check(sub.size() == 1 && Near(area(sub), 5000, 1e-6), "subtraction: one ring, area 5000");
        Check(ex.size() == 2 && Near(area(ex), 10000, 1e-6), "exclusion: two rings, area 10000");
        Check(PolygonSetContains(u, VectorStorage::FillRule::NonZero, Point2Dd(75, 50)) && !PolygonSetContains(u, VectorStorage::FillRule::NonZero, Point2Dd(-1, 50)),
              "containment follows the union");
        // Disjoint squares: the union keeps both.
        const PolygonSet far = PolygonBoolean(a, VectorStorage::FillRule::NonZero, square(200, 0, 100), VectorStorage::FillRule::NonZero, PathBooleanOp::Union);
        Check(far.size() == 2 && Near(area(far), 20000, 1e-6), "union of disjoint squares keeps both rings");
        // A square with a hole, both rings wound the same way, read even-odd:
        // normalising makes the winding consistent (the hole counts negative).
        PolygonSet holed = square(0, 0, 100);
        holed.push_back(square(25, 25, 50).front());
        const PolygonSet norm = PolygonBoolean(holed, VectorStorage::FillRule::EvenOdd, PolygonSet(), VectorStorage::FillRule::NonZero, PathBooleanOp::Union);
        Check(norm.size() == 2 && Near(std::fabs(PolygonSetArea(norm)), 7500, 1e-6), "a holed square normalises to outer minus hole");
        Check(!PolygonSetContains(norm, VectorStorage::FillRule::NonZero, Point2Dd(50, 50)) && PolygonSetContains(norm, VectorStorage::FillRule::NonZero, Point2Dd(10, 10)),
              "the hole is empty under a non-zero fill after normalising");
        // Offsetting a square: mitre grows it to 120 square, bevel cuts the
        // corners, round adds a quarter disc per corner; inset shrinks it.
        Check(Near(area(OffsetPolygons(a, VectorStorage::FillRule::NonZero, 10, StrokeLineJoin::Miter)), 14400, 1e-3), "mitre offset of a square by 10 is a 120 square");
        Check(Near(area(OffsetPolygons(a, VectorStorage::FillRule::NonZero, 10, StrokeLineJoin::Bevel)), 14200, 1e-3), "bevel offset cuts the four corners");
        const double roundArea = area(OffsetPolygons(a, VectorStorage::FillRule::NonZero, 10, StrokeLineJoin::Round));
        Check(roundArea > 14290 && roundArea < 14320, "round offset adds about a disc's worth at the corners");
        Check(Near(area(OffsetPolygons(a, VectorStorage::FillRule::NonZero, -10)), 6400, 1e-3), "inset by 10 is an 80 square");
        Check(OffsetPolygons(a, VectorStorage::FillRule::NonZero, -60).empty(), "inset past the middle leaves nothing");
        // Over path data: a circle grows to a circle.
        VectorCircle circle;
        circle.Center = Point2Dd(0, 0);
        circle.Radius = 40;
        auto circleOutline = OutlineOf(circle);
        Check(circleOutline.has_value(), "a circle has an outline");
        if (circleOutline) {
            const double grown = std::fabs(PolygonSetArea(FlattenToPolygons(OffsetPath(*circleOutline, 5))));
            Check(std::fabs(grown - M_PI * 45 * 45) < M_PI * 45 * 45 * 0.01, "a circle offset by 5 has the area of the larger circle");
        }
        auto slices = SlicePath(PolygonsToPath(a), PolygonsToPath(b));
        Check(Near(std::fabs(PolygonSetArea(FlattenToPolygons(slices.first))), 5000, 1e-6) &&
              Near(std::fabs(PolygonSetArea(FlattenToPolygons(slices.second))), 5000, 1e-6), "SlicePath splits a square into its inside and outside halves");

        // CombineShapes on a document.
        auto makeDoc = [](std::shared_ptr<VectorDocument>& doc, std::shared_ptr<VectorLayer>& layer) {
            doc = std::make_shared<VectorDocument>();
            doc->Size = Size2Dd(300, 200);
            layer = doc->AddLayer("L");
            auto r1 = std::make_shared<VectorRect>();
            r1->Bounds = Rect2Dd(0, 0, 100, 100);
            r1->Style.Fill = Color(255, 0, 0, 255);
            auto r2 = std::make_shared<VectorRect>();
            r2->Bounds = Rect2Dd(50, 0, 100, 100);
            r2->Style.Fill = Color(0, 0, 255, 255);
            layer->AddChild(r1);
            layer->AddChild(r2);
            EnsureIds(*doc);
        };
        std::shared_ptr<VectorDocument> cdoc;
        std::shared_ptr<VectorLayer> clayer;
        makeDoc(cdoc, clayer);
        auto added = CombineShapes({clayer->Children[1], clayer->Children[0]}, CombineOp::Add);
        Check(added.size() == 1 && clayer->Children.size() == 1 && clayer->Children[0] == added[0], "Add replaces both rects with one path");
        if (!added.empty()) {
            const Rect2Dd bb = added[0]->GetBoundingBox();
            Check(Near(bb.x, 0) && Near(bb.width, 150, 1e-3) && Near(bb.height, 100, 1e-3), "the union spans both rects");
            Check(added[0]->Style.Fill.has_value() && std::get<Color>(*added[0]->Style.Fill).r == 255, "the union takes the back shape's style");
        }
        makeDoc(cdoc, clayer);
        auto cut = CombineShapes({clayer->Children[0], clayer->Children[1]}, CombineOp::Subtract);
        Check(cut.size() == 1 && clayer->Children.size() == 1, "Subtract cuts the front rect out of the back one and removes it");
        if (!cut.empty()) {
            const Rect2Dd bb = cut[0]->GetBoundingBox();
            Check(Near(bb.x, 0) && Near(bb.width, 50, 1e-3), "what is left is the back rect's uncovered half");
        }
        makeDoc(cdoc, clayer);
        auto sliced = CombineShapes({clayer->Children[0], clayer->Children[1]}, CombineOp::Slice);
        Check(sliced.size() == 2 && clayer->Children.size() == 2, "Slice leaves the back rect's outside and inside pieces");
        makeDoc(cdoc, clayer);
        auto common = CombineShapes({clayer->Children[0], clayer->Children[1]}, CombineOp::Intersect);
        Check(common.size() == 1 && clayer->Children.size() == 1 && Near(common[0]->GetBoundingBox().width, 50, 1e-3),
              "Intersect keeps the shared strip");
    }

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
