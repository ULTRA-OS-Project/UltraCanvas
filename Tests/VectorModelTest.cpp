// Tests/VectorModelTest.cpp
// Tests for the shared vector document model (VectorStorage): matrix
// arithmetic at double precision, group and document bounding boxes that
// honour nested transforms and ignore empty children, and hit-testing that
// carries the document point through each ancestor's transform so children
// of a transformed group (a CAD block insert, a mirrored entity) are found
// where they are drawn.
//
// Usage: VectorModelTest
// Exit code is the number of failed checks.
// Version: 1.1.0
// Last Modified: 2026-09-15
// Author: UltraCanvas Framework

#include "DataFormats/UltraCanvasVectorStorage.h"
#include "DataFormats/UltraCanvasVectorRenderer.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasRenderContext.h"
#ifdef ULTRACANVAS_HAS_VECTOR_PLUGIN
#include "UltraCanvasCADConverters.h"
#endif

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

using namespace UltraCanvas;
using namespace UltraCanvas::VectorStorage;

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

bool Near(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }

bool NearRect(const Rect2Dd& r, double x, double y, double w, double h, double tol = 1e-6) {
    return Near(r.x, x, tol) && Near(r.y, y, tol) && Near(r.width, w, tol) && Near(r.height, h, tol);
}

std::shared_ptr<VectorRect> MakeRect(double x, double y, double w, double h) {
    auto r = std::make_shared<VectorRect>();
    r->Bounds = Rect2Dd{x, y, w, h};
    r->Style.Fill = Color(0, 0, 0, 255);
    return r;
}

// Renders a document into an offscreen context of the given size and
// returns the un-premultiplied RGBA of one pixel. Needs no display.
struct Rgba { int r, g, b, a; };
Rgba RenderAndSample(const VectorDocument& doc, int w, int h, int px, int py) {
    UCPixmap pixmap;
    if (!pixmap.Init(w, h)) return {-1, -1, -1, -1};
    std::unique_ptr<IRenderContext> ctx = CreateRenderContext(Size2Di(w, h), nullptr);
    if (!ctx) return {-1, -1, -1, -1};
    ctx->Clear(Color(0, 0, 0, 0));
    VectorRenderer renderer;
    renderer.RenderDocument(ctx.get(), doc);
    ctx->FlushToSurface(pixmap.GetSurface(), Point2Dd(0, 0));
    pixmap.MarkDirty();
    pixmap.Flush();
    const uint32_t p = pixmap.GetPixel(px, py);
    int a = (p >> 24) & 0xFF, r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
    if (a != 0 && a != 255) {
        r = std::min(255, (r * 255 + a / 2) / a);
        g = std::min(255, (g * 255 + a / 2) / a);
        b = std::min(255, (b * 255 + a / 2) / a);
    }
    return {r, g, b, a};
}

} // namespace

int main() {
    // ===== Matrix3x3 =====
    {
        Matrix3x3 t = Matrix3x3::Translate(10, 20);
        Point2Dd p = t.Transform(Point2Dd(1, 2));
        Check(Near(p.x, 11) && Near(p.y, 22), "translate moves a point");

        Matrix3x3 s = Matrix3x3::Scale(2, 3);
        Matrix3x3 ts = t * s;   // scale first, then translate
        p = ts.Transform(Point2Dd(1, 1));
        Check(Near(p.x, 12) && Near(p.y, 23), "product applies the right operand first");

        Matrix3x3 inv = ts.Inverse();
        Point2Dd back = inv.Transform(p);
        Check(Near(back.x, 1) && Near(back.y, 1), "inverse undoes the transform");
        Check((ts * inv).IsIdentity(1e-9), "matrix times inverse is identity");
        Check(Matrix3x3::Identity().IsIdentity() && !t.IsIdentity(),
              "IsIdentity distinguishes identity from a translation");

        // CAD drawings live in metres to micrometres: a large offset with
        // a tiny scale must survive a round trip without float rounding.
        Matrix3x3 cad = Matrix3x3::Translate(1234567.891, -987654.321) * Matrix3x3::Scale(0.001, 0.001);
        Point2Dd q = cad.Inverse().Transform(cad.Transform(Point2Dd(123.456, 789.012)));
        Check(Near(q.x, 123.456, 1e-6) && Near(q.y, 789.012, 1e-6),
              "large translation with small scale round-trips at double precision");

        Matrix3x3 rot = Matrix3x3::RotateDegrees(90);
        p = rot.Transform(Point2Dd(1, 0));
        Check(Near(p.x, 0) && Near(p.y, 1), "90 degree rotation maps x axis onto y axis");

        Matrix3x3 f = Matrix3x3::FromValues(1, 2, 3, 4, 5, 6);
        p = f.Transform(Point2Dd(1, 1));
        Check(Near(p.x, 1 + 2 + 5) && Near(p.y, 3 + 4 + 6), "FromValues is row-major (b is the x-row's y term)");
    }

    // ===== Bounding boxes =====
    {
        auto doc = std::make_shared<VectorDocument>();
        doc->Size = Size2Dd{500, 400};
        Check(NearRect(doc->GetBoundingBox(), 0, 0, 500, 400),
              "empty document reports the page as its bounds");

        auto layer = doc->AddLayer("Model");
        auto group = std::make_shared<VectorGroup>();
        group->AddChild(MakeRect(10, 10, 20, 20));
        group->AddChild(MakeRect(50, 30, 10, 10));
        auto empty = std::make_shared<VectorGroup>();   // no children
        group->AddChild(empty);
        layer->AddChild(group);

        Check(NearRect(group->GetBoundingBox(), 10, 10, 50, 30),
              "group bounds are the union of its children");
        Check(NearRect(doc->GetBoundingBox(), 10, 10, 50, 30),
              "an empty child group does not drag the union to the origin");

        group->Transform = Matrix3x3::Translate(100, 200) * Matrix3x3::Scale(2, 2);
        Check(NearRect(group->GetBoundingBox(), 120, 220, 100, 60),
              "group bounds apply the group transform");

        layer->Transform = Matrix3x3::Translate(-20, 0);
        Check(NearRect(doc->GetBoundingBox(), 100, 220, 100, 60),
              "document bounds apply the layer transform on top");

        // A rotated group: the box is the axis-aligned hull of the corners.
        auto rotated = std::make_shared<VectorGroup>();
        rotated->AddChild(MakeRect(0, 0, 10, 10));
        rotated->Transform = Matrix3x3::RotateDegrees(45);
        Rect2Dd rb = rotated->GetBoundingBox();
        double d = 10 * std::sqrt(2.0);
        Check(Near(rb.x, -d / 2, 1e-6) && Near(rb.y, 0, 1e-6) &&
                      Near(rb.width, d, 1e-6) && Near(rb.height, d, 1e-6),
              "rotated group bounds are the hull of the rotated corners");

        Check(NearRect(empty->GetBoundingBox(), 0, 0, 0, 0), "empty group has empty bounds");
        empty->Transform = Matrix3x3::Translate(1000, 1000);
        Check(NearRect(empty->GetBoundingBox(), 0, 0, 0, 0),
              "a transform does not turn an empty box into a point at the offset");
    }

    // ===== Hit testing through transforms =====
    {
        auto doc = std::make_shared<VectorDocument>();
        doc->Size = Size2Dd{500, 400};
        auto layer = doc->AddLayer("Model");

        // A "block insert": geometry stored at the origin, placed by the
        // group transform at (300, 100) and scaled by 2.
        auto insert = std::make_shared<VectorGroup>();
        insert->Id = "insert";
        auto part = MakeRect(0, 0, 10, 10);
        part->Id = "part";
        insert->AddChild(part);
        insert->Transform = Matrix3x3::Translate(300, 100) * Matrix3x3::Scale(2, 2);
        layer->AddChild(insert);

        auto plain = MakeRect(20, 20, 30, 30);
        plain->Id = "plain";
        layer->AddChild(plain);

        auto hidden = MakeRect(200, 200, 50, 50);
        hidden->Id = "hidden";
        hidden->Style.Visible = false;
        layer->AddChild(hidden);

        auto Hit = [&](double x, double y) {
            std::string ids;
            for (const auto* e : HitTestDocument(*doc, Point2Dd(x, y))) {
                if (!ids.empty()) ids += ",";
                ids += e->Id;
            }
            return ids;
        };

        Check(Hit(310, 110) == "insert,part", "point inside the placed insert hits the group and its child");
        Check(Hit(5, 5).empty(), "point at the child's stored (untransformed) position misses");
        Check(Hit(325, 125).empty(), "point just outside the scaled insert misses");
        Check(Hit(30, 30) == "plain", "untransformed element hits in document space");
        Check(Hit(225, 225).empty(), "hidden element never hits");
        Check(Hit(400, 300).empty(), "empty space hits nothing");

        Check(!HitTestElement(*std::make_shared<VectorGroup>(), Point2Dd(0, 0)),
              "an element without bounds never hits at the origin");

        // Layer transform on top of the group transform.
        layer->Transform = Matrix3x3::Translate(0, 100);
        Check(Hit(310, 210) == "insert,part", "hit test composes layer and group transforms");
        Check(Hit(310, 110).empty(), "old position no longer hits once the layer moves");
    }

    // ===== Units =====
    {
        Check(Near(PointsPerUnit(LengthUnit::Inch), 72), "an inch is 72 points");
        Check(Near(PointsPerUnit(LengthUnit::Millimeter) * 25.4, 72), "25.4 mm are 72 points");
        Check(Near(PointsPerUnit(LengthUnit::Meter), 72000 / 25.4), "a metre is 1000 mm");
        Check(PointsPerUnit(LengthUnit::Unspecified) == 0, "unspecified unit has no scale");
        Check(std::string(LengthUnitSymbol(LengthUnit::Centimeter)) == "cm", "unit symbol");

        VectorDocument fresh;
        Check(fresh.SourceUnit == LengthUnit::Unspecified && fresh.PointsPerSourceUnit == 1.0,
              "a new document is unitless in points");
    }


    // ===== Renderer: arcs, gradient bounds, fill opacity, clip =====
    {
        UCImage::InitializeImageSubsysterm("VectorModelTest");

        // A half-disc drawn with one SVG arc, filled red. Before 0.8.50 the
        // renderer drew the arc as a straight chord, so the bulge was empty.
        VectorDocument doc;
        doc.Size = Size2Dd{100, 100};
        doc.ViewBox = Rect2Dd{0, 0, 100, 100};
        auto layer = doc.AddLayer("main");
        auto arc = std::make_shared<VectorPath>();
        arc->MoveTo(10, 50);
        arc->ArcTo(40, 40, 0, false, true, 90, 50, false);   // upper half of a circle at (50,50)
        arc->ClosePath();
        arc->Style.Fill = Color(255, 0, 0, 255);
        layer->AddChild(arc);
        Rgba bulge = RenderAndSample(doc, 100, 100, 50, 20);
        Check(bulge.a > 200 && bulge.r > 200 && bulge.g < 50, "an SVG arc is filled as a curve, not a chord");
        Rgba below = RenderAndSample(doc, 100, 100, 50, 80);
        Check(below.a == 0, "the sweep flag picks the upper half");

        // An objectBoundingBox gradient resolves against the shape it fills:
        // red at the shape's left edge, blue at its right - wherever the
        // shape is. Before 0.8.50 it resolved against {0,0,100,100}.
        VectorDocument gdoc;
        gdoc.Size = Size2Dd{400, 200};
        gdoc.ViewBox = Rect2Dd{0, 0, 400, 200};
        auto glayer = gdoc.AddLayer("main");
        auto rect = MakeRect(300, 100, 100, 100);
        LinearGradientData lg;
        lg.Start = {0, 0};
        lg.End = {1, 0};
        lg.Stops = {{0.0, Color(255, 0, 0, 255)}, {1.0, Color(0, 0, 255, 255)}};
        rect->Style.Fill = GradientData{lg};
        glayer->AddChild(rect);
        Rgba left = RenderAndSample(gdoc, 400, 200, 302, 150);
        Rgba right = RenderAndSample(gdoc, 400, 200, 397, 150);
        Check(left.r > 200 && left.b < 60, "gradient starts red at the shape's own left edge");
        Check(right.b > 200 && right.r < 60, "gradient ends blue at the shape's own right edge");

        // fill-opacity halves the paint's alpha.
        VectorDocument odoc;
        odoc.Size = Size2Dd{100, 100};
        odoc.ViewBox = Rect2Dd{0, 0, 100, 100};
        auto olayer = odoc.AddLayer("main");
        auto half = MakeRect(0, 0, 100, 100);
        half->Style.Fill = Color(0, 255, 0, 255);
        half->Style.FillOpacity = 0.5f;
        olayer->AddChild(half);
        Rgba faded = RenderAndSample(odoc, 100, 100, 50, 50);
        Check(faded.a > 110 && faded.a < 145, "fill-opacity 0.5 renders at half alpha");

        // A clip-path definition confines the fill to the clip's outline.
        VectorDocument cdoc;
        cdoc.Size = Size2Dd{100, 100};
        cdoc.ViewBox = Rect2Dd{0, 0, 100, 100};
        auto clayer = cdoc.AddLayer("main");
        auto clip = std::make_shared<VectorClipPath>();
        clip->Id = "leftHalf";
        clip->Data.Elements.push_back(MakeRect(0, 0, 50, 100));
        cdoc.Definitions["leftHalf"] = clip;
        auto full = MakeRect(0, 0, 100, 100);
        full->Style.Fill = Color(0, 0, 255, 255);
        full->Style.ClipPath = std::string("leftHalf");
        clayer->AddChild(full);
        Rgba inside = RenderAndSample(cdoc, 100, 100, 25, 50);
        Rgba outside = RenderAndSample(cdoc, 100, 100, 75, 50);
        Check(inside.a == 255 && inside.b == 255, "clip-path keeps the fill inside the clip");
        Check(outside.a == 0, "clip-path removes the fill outside the clip");
    }

    // ===== Effects, transparency ramps and the line gallery =====
    {
        auto MakeDoc = [](VectorDocument& d, int w, int h) {
            d.Size = Size2Dd{static_cast<double>(w), static_cast<double>(h)};
            d.ViewBox = Rect2Dd{0, 0, static_cast<double>(w), static_cast<double>(h)};
            return d.AddLayer("main");
        };

        // A wall shadow: black at the offset beside the shape, nothing on
        // the far side, the shape itself untouched.
        VectorDocument sdoc;
        auto slayer = MakeDoc(sdoc, 120, 120);
        auto box = MakeRect(20, 20, 50, 50);
        box->Style.Fill = Color(255, 0, 0, 255);
        ShadowEffect shadow;
        shadow.Kind = ShadowKind::Wall;
        shadow.Offset = Point2Dd(20, 20);
        shadow.Blur = 0;
        shadow.Darkness = 1.0f;
        box->Effects.Shadow = shadow;
        slayer->AddChild(box);
        Rgba onShape = RenderAndSample(sdoc, 120, 120, 45, 45);
        Rgba inShadow = RenderAndSample(sdoc, 120, 120, 80, 80);
        Rgba farSide = RenderAndSample(sdoc, 120, 120, 10, 10);
        Check(onShape.r == 255 && onShape.a == 255, "a shadowed shape still draws its own fill");
        Check(inShadow.a > 200 && inShadow.r < 40 && inShadow.g < 40, "the wall shadow lies at the offset");
        Check(farSide.a == 0, "no shadow on the far side of the shape");

        // A blurred shadow softens: the alpha falls off past the offset edge.
        shadow.Blur = 12;
        shadow.Darkness = 1.0f;
        box->Effects.Shadow = shadow;
        Rgba softEdge = RenderAndSample(sdoc, 120, 120, 95, 80);
        Rgba softCore = RenderAndSample(sdoc, 120, 120, 75, 75);
        Check(softEdge.a > 0 && softEdge.a < 250, "a blurred shadow has a penumbra past its edge");
        Check(softCore.a > softEdge.a, "the penumbra is lighter than the shadow's core");

        // Feather: the shape's centre stays solid, its edge fades.
        VectorDocument fdoc;
        auto flayer = MakeDoc(fdoc, 100, 100);
        auto disc = std::make_shared<VectorCircle>();
        disc->Center = Point2Dd(50, 50);
        disc->Radius = 40;
        disc->Style.Fill = Color(0, 0, 255, 255);
        disc->Effects.Feather = FeatherEffect{10.0f};
        flayer->AddChild(disc);
        Rgba centre = RenderAndSample(fdoc, 100, 100, 50, 50);
        Rgba rim = RenderAndSample(fdoc, 100, 100, 50, 12);
        Check(centre.a == 255 && centre.b == 255, "a feathered shape is solid at its centre");
        Check(rim.a > 0 && rim.a < 250, "a feathered shape fades at its rim");

        // A linear transparency ramp: opaque on the left, clear on the right.
        VectorDocument tdoc;
        auto tlayer = MakeDoc(tdoc, 200, 100);
        auto band = MakeRect(0, 0, 200, 100);
        band->Style.Fill = Color(0, 128, 0, 255);
        TransparencyData ramp;
        ramp.Shape = TransparencyShape::Linear;
        ramp.Start = Point2Dd(0, 50);
        ramp.End = Point2Dd(200, 50);
        ramp.Stops = {{0.0, 0.0f}, {1.0, 1.0f}};
        band->Style.Transparency = ramp;
        tlayer->AddChild(band);
        Rgba leftEnd = RenderAndSample(tdoc, 200, 100, 5, 50);
        Rgba middle = RenderAndSample(tdoc, 200, 100, 100, 50);
        Rgba rightEnd = RenderAndSample(tdoc, 200, 100, 197, 50);
        Check(leftEnd.a > 240, "linear transparency: opaque at the start");
        Check(middle.a > 100 && middle.a < 160, "linear transparency: half way at the middle");
        Check(rightEnd.a < 15, "linear transparency: clear at the end");
        Check(Near(ramp.LevelAt(0.25), 0.25, 1e-6), "LevelAt interpolates between stops");

        // A stained-glass mix multiplies with what is below.
        VectorDocument mdoc;
        auto mlayer = MakeDoc(mdoc, 50, 50);
        auto under = MakeRect(0, 0, 50, 50);
        under->Style.Fill = Color(255, 255, 0, 255);
        mlayer->AddChild(under);
        auto over = MakeRect(0, 0, 50, 50);
        over->Style.Fill = Color(0, 255, 255, 255);
        TransparencyData mix;
        mix.Shape = TransparencyShape::Flat;
        mix.Level = 0.0f;
        mix.Mix = TransparencyMix::StainedGlass;
        over->Style.Transparency = mix;
        mlayer->AddChild(over);
        Rgba multiplied = RenderAndSample(mdoc, 50, 50, 25, 25);
        Check(multiplied.r < 10 && multiplied.g > 240 && multiplied.b < 10, "stained glass multiplies yellow under cyan to green");

        // Arrowheads: a triangle past the end of a stroked line.
        VectorDocument adoc;
        auto alayer = MakeDoc(adoc, 200, 100);
        auto line = std::make_shared<VectorLine>();
        line->Start = Point2Dd(20, 50);
        line->End = Point2Dd(120, 50);
        StrokeData st;
        st.Fill = Color(0, 0, 0, 255);
        st.Width = 4;
        st.EndArrow.Kind = ArrowheadKind::Triangle;
        st.EndArrow.Scale = 3.0f;   // 48 long, 24 wide
        line->Style.Stroke = st;
        alayer->AddChild(line);
        Rgba onLine = RenderAndSample(adoc, 200, 100, 60, 50);
        Rgba arrowBody = RenderAndSample(adoc, 200, 100, 80, 56);   // 40 back from the tip (half-width 10 there), 6 aside: inside the head, outside the line
        Rgba beyondTip = RenderAndSample(adoc, 200, 100, 126, 50);
        Check(onLine.a == 255, "the line itself is stroked");
        Check(arrowBody.a == 255, "the end arrowhead fills beside the line");
        Check(beyondTip.a == 0, "nothing past the arrow's tip");

        // Xara's stock arrowheads keep Xara's geometry: for a 4 wide line at
        // Scale 1 (Xara's size 3) the straight arrow's tip is 39 past the
        // end, its base 3 behind it and 18 high; the spot is a circle of
        // radius 18 centred on the start.
        VectorDocument xdoc;
        auto xlayer = MakeDoc(xdoc, 200, 100);
        auto xline = std::make_shared<VectorLine>();
        xline->Start = Point2Dd(40, 50);
        xline->End = Point2Dd(120, 50);
        StrokeData xst;
        xst.Fill = Color(0, 0, 0, 255);
        xst.Width = 4;
        xst.StartArrow.Kind = ArrowheadKind::Spot;
        xst.EndArrow.Kind = ArrowheadKind::StraightArrow;
        xline->Style.Stroke = xst;
        xlayer->AddChild(xline);
        Rgba headMid = RenderAndSample(xdoc, 200, 100, 150, 50);    // 30 past the end, inside the head
        Rgba headBase = RenderAndSample(xdoc, 200, 100, 118, 62);   // 2 behind the end, 12 aside: in the head, off the line
        Rgba pastTip = RenderAndSample(xdoc, 200, 100, 161, 50);
        Rgba spotEdge = RenderAndSample(xdoc, 200, 100, 25, 50);    // 15 before the start, inside the spot
        Rgba beforeSpot = RenderAndSample(xdoc, 200, 100, 19, 50);
        Check(headMid.a == 255 && headBase.a == 255, "the Xara straight arrow reaches past the line's end at Xara's size");
        Check(pastTip.a == 0, "nothing past the straight arrow's tip (39 beyond the end)");
        Check(spotEdge.a == 255 && beforeSpot.a == 0, "the Xara spot is centred on the start with radius 18");

        // ----- phase 5: ClipView, contour, blend, mould, bevel -----

        // ClipView: the keyhole clips the contents and is not drawn itself.
        VectorDocument cvdoc;
        auto cvlayer = MakeDoc(cvdoc, 200, 200);
        auto clip = std::make_shared<VectorClipView>();
        auto keyhole = MakeRect(50, 50, 100, 100);
        keyhole->Style.Fill = Color(0, 0, 255, 255);
        auto content = MakeRect(50, 50, 50, 100);
        content->Style.Fill = Color(255, 0, 0, 255);
        clip->AddChild(keyhole);
        clip->AddChild(content);
        cvlayer->AddChild(clip);
        Rgba cvIn = RenderAndSample(cvdoc, 200, 200, 75, 100);
        Rgba cvKeyholeOnly = RenderAndSample(cvdoc, 200, 200, 125, 100);
        Rgba cvOut = RenderAndSample(cvdoc, 200, 200, 20, 20);
        Check(cvIn.r == 255 && cvIn.a == 255, "ClipView draws its contents inside the keyhole");
        Check(cvKeyholeOnly.a == 0, "the keyhole shape itself is not drawn");
        Check(cvOut.a == 0, "nothing outside the keyhole");
        const Rect2Dd cvb = clip->GetBoundingBox();
        Check(std::fabs(cvb.x - 50) < 0.01 && std::fabs(cvb.width - 100) < 0.01, "a ClipView's bounds are its keyhole's");

        // Contour: outward rings behind the object run from its colour to
        // the contour colour; inward rings lie over it.
        VectorDocument codoc;
        auto colayer = MakeDoc(codoc, 200, 200);
        auto cobox = MakeRect(80, 80, 40, 40);
        cobox->Style.Fill = Color(255, 0, 0, 255);
        ContourEffect contour;
        contour.Steps = 2;
        contour.Width = 20;
        contour.Colour = Color(0, 0, 255, 255);
        cobox->Effects.Contour = contour;
        colayer->AddChild(cobox);
        Rgba coObj = RenderAndSample(codoc, 200, 200, 100, 100);
        Rgba coRing1 = RenderAndSample(codoc, 200, 200, 75, 100);
        Rgba coRing2 = RenderAndSample(codoc, 200, 200, 65, 100);
        Rgba coPast = RenderAndSample(codoc, 200, 200, 55, 100);
        Check(coObj.r == 255 && coObj.b == 0, "the contoured object draws over its rings");
        Check(coRing1.a == 255 && coRing1.r > 100 && coRing1.r < 160 && coRing1.b > 100 && coRing1.b < 160, "the first outward ring is half way to the contour colour");
        Check(coRing2.a == 255 && coRing2.b == 255 && coRing2.r == 0, "the outermost ring has the contour colour");
        Check(coPast.a == 0, "nothing beyond the contour width");
        contour.Width = -10;
        cobox->Effects.Contour = contour;
        Rgba coInner = RenderAndSample(codoc, 200, 200, 100, 100);
        Rgba coEdge = RenderAndSample(codoc, 200, 200, 83, 100);
        Check(coInner.b == 255 && coInner.r == 0, "an inward contour's innermost ring lies over the middle of the object");
        Check(coEdge.r == 255 && coEdge.b == 0, "the object's own colour shows between its edge and the first inset");

        // Blend: three steps between a red and a blue circle put a purple
        // one half way.
        VectorDocument bldoc;
        auto bllayer = MakeDoc(bldoc, 200, 200);
        auto blend = std::make_shared<VectorBlend>();
        blend->Steps = 3;
        auto ca = std::make_shared<VectorCircle>();
        ca->Center = Point2Dd(40, 100); ca->Radius = 15; ca->Style.Fill = Color(255, 0, 0, 255);
        auto cb = std::make_shared<VectorCircle>();
        cb->Center = Point2Dd(160, 100); cb->Radius = 15; cb->Style.Fill = Color(0, 0, 255, 255);
        blend->AddChild(ca);
        blend->AddChild(cb);
        bllayer->AddChild(blend);
        Rgba blMid = RenderAndSample(bldoc, 200, 200, 100, 100);
        Rgba blGap = RenderAndSample(bldoc, 200, 200, 100, 60);
        Rgba blEnd = RenderAndSample(bldoc, 200, 200, 160, 100);
        Check(blMid.a == 255 && blMid.r > 100 && blMid.r < 160 && blMid.b > 100 && blMid.b < 160, "the middle blend step is half way in colour and position");
        Check(blGap.a == 0, "the blend draws nothing off the steps");
        Check(blEnd.b == 255, "the blend's end object draws as it is");
        blend->Steps = 0;
        Rgba blNone = RenderAndSample(bldoc, 200, 200, 100, 100);
        Check(blNone.a == 0, "no steps, no intermediates");

        // Mould: a square warped into a trapezoid by a perspective mould.
        VectorDocument modoc;
        auto molayer = MakeDoc(modoc, 200, 200);
        auto mould = std::make_shared<VectorMould>();
        mould->Kind = MouldKind::Perspective;
        auto square = MakeRect(0, 0, 100, 100);
        square->Style.Fill = Color(0, 200, 0, 255);
        mould->AddChild(square);
        {
            PathData shape;
            auto cmd = [&](PathCommandType t, std::initializer_list<float> v) { PathCommand c; c.Type = t; c.Parameters = v; shape.commands.push_back(c); };
            cmd(PathCommandType::MoveTo, {50, 20});
            cmd(PathCommandType::LineTo, {150, 20});
            cmd(PathCommandType::LineTo, {190, 180});
            cmd(PathCommandType::LineTo, {10, 180});
            cmd(PathCommandType::ClosePath, {});
            shape.Closed = true;
            mould->Shape = shape;
        }
        molayer->AddChild(mould);
        Rgba moMid = RenderAndSample(modoc, 200, 200, 100, 100);
        Rgba moOutside = RenderAndSample(modoc, 200, 200, 20, 40);
        Rgba moCorner = RenderAndSample(modoc, 200, 200, 20, 170);
        Check(moMid.g == 200 && moMid.a == 255, "the moulded square fills the middle of the shape");
        Check(moOutside.a == 0, "nothing where the unwarped square would have been");
        Check(moCorner.g == 200, "the square reaches the shape's wider bottom corners");
        const Rect2Dd mob = mould->GetBoundingBox();
        Check(std::fabs(mob.x - 10) < 0.01 && std::fabs(mob.width - 180) < 0.01, "a mould's bounds are its shape's");
        mould->Kind = MouldKind::Envelope;
        mould->Shape = VectorMould::IdentityShape(MouldKind::Envelope, Rect2Dd(0, 0, 100, 100));
        Rgba moIdentity = RenderAndSample(modoc, 200, 200, 50, 50);
        Rgba moIdentityOut = RenderAndSample(modoc, 200, 200, 150, 150);
        Check(moIdentity.g == 200 && moIdentityOut.a == 0, "an identity envelope leaves the square where it was");

        // Bevel: lit from the upper left, the left rim brightens and the
        // right rim darkens; the middle keeps its colour.
        VectorDocument bvdoc;
        auto bvlayer = MakeDoc(bvdoc, 200, 200);
        auto bvbox = MakeRect(50, 50, 100, 100);
        bvbox->Style.Fill = Color(128, 128, 128, 255);
        BevelEffect bevel;
        bevel.Indent = 20;
        bevel.LightAngle = 135;
        bevel.Contrast = 0.8f;
        bvbox->Effects.Bevel = bevel;
        bvlayer->AddChild(bvbox);
        Rgba bvLeft = RenderAndSample(bvdoc, 200, 200, 56, 100);
        Rgba bvRight = RenderAndSample(bvdoc, 200, 200, 144, 100);
        Rgba bvMid = RenderAndSample(bvdoc, 200, 200, 100, 100);
        Rgba bvOut = RenderAndSample(bvdoc, 200, 200, 30, 100);
        Check(bvLeft.r > 140, "the bevel's left rim faces the light and brightens");
        Check(bvRight.r < 116, "its right rim faces away and darkens");
        Check(bvMid.r == 128, "the flat middle keeps its colour");
        Check(bvOut.a == 0, "an inner bevel draws nothing outside the shape");
        std::printf("      (bevel left %d, right %d)\n", bvLeft.r, bvRight.r);

        // A width profile: thick at the start, vanishing at the end.
        VectorDocument wdoc;
        auto wlayer = MakeDoc(wdoc, 200, 100);
        auto taper = std::make_shared<VectorLine>();
        taper->Start = Point2Dd(10, 50);
        taper->End = Point2Dd(190, 50);
        StrokeData ts;
        ts.Fill = Color(0, 0, 0, 255);
        ts.Width = 20;
        ts.WidthProfile = {{0.0f, 1.0f}, {1.0f, 0.0f}};
        taper->Style.Stroke = ts;
        wlayer->AddChild(taper);
        Rgba thickEnd = RenderAndSample(wdoc, 200, 100, 20, 42);
        Rgba thinEnd = RenderAndSample(wdoc, 200, 100, 180, 42);
        Check(thickEnd.a == 255, "a width profile is full width at its start");
        Check(thinEnd.a == 0, "a width profile tapers to nothing at its end");
        Check(Near(ts.WidthAt(0.5f), 10.0, 1e-5), "WidthAt interpolates the profile");

        // A brush: stamps along the path replace the stroke.
        VectorDocument bdoc;
        auto blayer = MakeDoc(bdoc, 200, 100);
        auto brushed = std::make_shared<VectorLine>();
        brushed->Start = Point2Dd(10, 50);
        brushed->End = Point2Dd(190, 50);
        StrokeData bs;
        bs.Fill = Color(0, 0, 0, 255);
        bs.Width = 10;
        BrushData brush;
        brush.Stamp = std::make_shared<VectorGroup>();
        auto dot = std::make_shared<VectorCircle>();
        dot->Center = Point2Dd(0, 0);
        dot->Radius = 5;
        dot->Style.Fill = Color(255, 0, 255, 255);
        brush.Stamp->AddChild(dot);
        brush.Spacing = 2.0f;   // one dot every two dot widths
        bs.Brush = brush;
        brushed->Style.Stroke = bs;
        blayer->AddChild(brushed);
        Rgba atStamp = RenderAndSample(bdoc, 200, 100, 10, 50);
        Rgba between = RenderAndSample(bdoc, 200, 100, 20, 50);
        Check(atStamp.a == 255 && atStamp.r == 255 && atStamp.b == 255, "a brush stamps its group at the path start");
        Check(between.a == 0, "a brush leaves the gap between stamps empty");

        // The outline helper feeds the gallery and the editor alike.
        PathData outline;
        Check(BuildOutlinePath(*box, outline) && outline.commands.size() == 6, "BuildOutlinePath: a rectangle is M L L L L Z");
        VectorText label;
        Check(!BuildOutlinePath(label, outline), "BuildOutlinePath: text has no outline");

        // The raster cache keeps one entry per shadowed object and drops it on demand.
        VectorRenderer cached;
        {
            UCPixmap pm;
            pm.Init(120, 120);
            std::unique_ptr<IRenderContext> cctx = CreateRenderContext(Size2Di(120, 120), nullptr);
            cached.RenderDocument(cctx.get(), sdoc);
            cached.RenderDocument(cctx.get(), sdoc);
        }
        Check(cached.EffectCacheSize() == 1, "one raster is cached for the shadowed shape across frames");
        cached.ClearCaches();
        Check(cached.EffectCacheSize() == 0, "ClearCaches drops the effect rasters");
    }

#ifdef ULTRACANVAS_HAS_VECTOR_PLUGIN
    // ===== DXF carries units and layer properties =====
    {
        // A 297 x 210 mm plan: the model holds it in points.
        auto doc = std::make_shared<VectorDocument>();
        double k = PointsPerUnit(LengthUnit::Millimeter);
        doc->Size = Size2Dd{297 * k, 210 * k};
        doc->SourceUnit = LengthUnit::Millimeter;
        doc->PointsPerSourceUnit = k;

        auto walls = doc->AddLayer("Walls");
        walls->DefaultColor = Color(255, 0, 0, 255);
        walls->DefaultStrokeWidth = static_cast<float>(0.5 * k);   // 0.50 mm
        walls->Locked = true;
        // Content fills most of the page: the reader trusts declared
        // extents only when they are not wildly larger than the drawing.
        auto rect = MakeRect(10 * k, 10 * k, 250 * k, 150 * k);
        StrokeData st;
        st.Fill = Color(255, 0, 0, 255);
        st.Width = static_cast<float>(0.5 * k);
        rect->Style.Fill.reset();
        rect->Style.Stroke = st;
        walls->AddChild(rect);

        auto notes = doc->AddLayer("Notes");
        notes->Visible = false;
        notes->Plottable = false;
        notes->AddChild(MakeRect(0, 0, 5 * k, 5 * k));

        VectorConverter::DXFConverter dxf;
        std::string data = dxf.ExportToString(*doc);
        { std::ofstream out("vector_model_test.dxf"); out << data; }
        Check(data.find("$INSUNITS\n 70\n4\n") != std::string::npos, "DXF writes $INSUNITS = 4 (mm)");
        Check(data.find("$EXTMAX\n 10\n297.0\n") != std::string::npos, "DXF page is 297 mm wide");
        Check(data.find("\nWalls\n 70\n4\n") != std::string::npos, "locked layer has flag 4");
        Check(data.find("\nNotes\n 70\n0\n 62\n-7\n") != std::string::npos,
              "hidden layer is written off (negative colour)");
        Check(data.find("\n290\n0\n") != std::string::npos, "non-plottable layer has 290 = 0");
        Check(data.find("\n370\n50\n") != std::string::npos, "0.5 mm layer lineweight");

        auto back = dxf.ImportFromString(data);
        Check(back != nullptr, "DXF re-imports");
        if (back) {
            Check(back->SourceUnit == LengthUnit::Millimeter, "unit survives the round trip");
            Check(Near(back->PointsPerSourceUnit, k, 1e-9), "physical scale survives");
            Check(Near(back->Size.width, 297 * k, 0.5) && Near(back->Size.height, 210 * k, 0.5),
                  "page size in points survives");
            const VectorLayer* w = nullptr;
            for (const auto& l : back->Layers) if (l->Name == "Walls") w = l.get();
            Check(w != nullptr, "Walls layer survives");
            if (w) {
                Check(w->Locked, "Locked survives");
                Check(w->DefaultColor && w->DefaultColor->r == 255 && w->DefaultColor->g == 0,
                      "layer default colour survives");
                Check(Near(w->DefaultStrokeWidth, 0.5 * k, 0.01), "layer lineweight survives");
                Rect2Dd rb = w->GetBoundingBox();
                Check(Near(rb.x, 10 * k, 0.5) && Near(rb.width, 250 * k, 0.5),
                      "geometry lands at the same points after the unit round trip");
            }
        }
    }
#else
    std::printf("  (DXF round trip skipped: Vector plugin not built)\n");
#endif

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
