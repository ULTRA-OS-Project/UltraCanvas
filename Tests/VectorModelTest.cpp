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
// Version: 1.0.0
// Last Modified: 2026-09-08
// Author: UltraCanvas Framework

#include "../UltraCanvas/Plugins/Vector/UltraCanvasVectorStorage.h"
#include "../UltraCanvas/Plugins/Vector/UltraCanvasVectorRenderer.h"
#include "../UltraCanvas/Plugins/Vector/UltraCanvasCADConverters.h"

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

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
