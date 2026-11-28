// Tests/ODGConverterTest.cpp
// The OpenDocument Drawing converter (Plugins/Vector/UltraCanvasODGConverter):
//  - a drawing written as an .odg package and as flat .fodg XML reads back
//    with its shapes, places, colours, gradient, dashes, arrowhead, text,
//    picture, layers and page;
//  - LibreOffice Draw's own files (media/vector/ODG, written by LibreOffice
//    24.2) read with their custom shapes evaluated from their enhanced
//    geometry, rotation, gradients, hatch, transparency, text and picture
//    where LibreOffice draws them, and its preset shapes with their
//    even-odd fill and shading;
//  - hand-written geometry: the enhanced path's arcs, quadrants, equations,
//    modifiers and stretch points, draw:transform's order and direction;
//  - a written package carries a thumbnail of the page, which the Filer's
//    embedded-preview extractor finds;
//  - the converter is reachable through the plugin's extension dispatch.
//
// Usage: ODGConverterTest                      run the checks
//        ODGConverterTest --render in out.ppm [width]
//                                              draw a drawing (any vector
//                                              format) to a PPM, on white
//        ODGConverterTest --convert in out     read one vector format, write
//                                              another (by extension)
// Exit code is the number of failed checks.
// Version: 1.2.0 - LibreOffice's gradient styles: ellipsoids read elliptical and turned, written back
// Version: 1.1.0 - the package's thumbnail
// Version: 1.0.0
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework

#include "UltraCanvasODGConverter.h"
#include "UltraCanvasVectorFormatsPlugin.h"
#include "UltraCanvasVectorRaster.h"
#include "UltraCanvasRasterLayer.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasEmbeddedPreview.h"
#include "UltraCanvasClipboardDib.h"
#include "UltraCanvasPathUtf8.h"
#include "DataFormats/UltraCanvasVectorStorage.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

using namespace UltraCanvas;
using namespace UltraCanvas::VectorStorage;
using namespace UltraCanvas::VectorConverter;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}

bool Near(double a, double b, double tolerance = 0.05) { return std::fabs(a - b) <= tolerance; }
constexpr double kCm = 72.0 / 2.54;
constexpr double kPi = 3.14159265358979323846;
constexpr size_t kPngSize = 75;

// Every element under `g`, depth first, groups included.
void Collect(const std::shared_ptr<VectorElement>& e, std::vector<std::shared_ptr<VectorElement>>& out) {
    if (!e) return;
    out.push_back(e);
    if (IsGroupType(e->Type))
        for (const auto& c : std::static_pointer_cast<VectorGroup>(e)->Children) Collect(c, out);
}

std::vector<std::shared_ptr<VectorElement>> All(const VectorDocument& doc) {
    std::vector<std::shared_ptr<VectorElement>> out;
    for (const auto& l : doc.Layers) Collect(l, out);
    return out;
}

template <typename T>
std::vector<std::shared_ptr<T>> OfType(const VectorDocument& doc, VectorElementType type) {
    std::vector<std::shared_ptr<T>> out;
    for (const auto& e : All(doc))
        if (e->Type == type) out.push_back(std::static_pointer_cast<T>(e));
    return out;
}

bool SameColour(const std::optional<FillData>& f, const Color& c) {
    if (!f) return false;
    const auto* got = std::get_if<Color>(&*f);
    return got && got->r == c.r && got->g == c.g && got->b == c.b;
}

// The bounds of `e` on the page (its ancestors' transforms applied).
Rect2Dd PageBounds(const std::shared_ptr<VectorElement>& e) {
    return e->GetGlobalTransform().Transform(e->GetBoundingBox());
}

std::string Describe(const Rect2Dd& r) {
    char buf[128];
    std::snprintf(buf, sizeof buf, "(%.2f, %.2f, %.2f x %.2f)", r.x, r.y, r.width, r.height);
    return buf;
}

// ===== A DRAWING WRITTEN AND READ BACK =====

std::shared_ptr<VectorDocument> BuildDocument() {
    auto doc = std::make_shared<VectorDocument>();
    doc->Size = Size2Dd{500, 400};
    doc->ViewBox = Rect2Dd(0, 0, 500, 400);
    doc->Title = "ODG round trip";
    doc->Author = "UltraCanvas test";
    doc->BackgroundColor = Color(250, 250, 240, 255);
    auto layer = doc->AddLayer("Artwork");

    auto rect = std::make_shared<VectorRect>();
    rect->Id = "red box";
    rect->Bounds = Rect2Dd(40, 40, 120, 60);
    rect->RadiusX = rect->RadiusY = 8;
    rect->Style.Fill = Color(220, 20, 30, 255);
    StrokeData outline;
    outline.Fill = Color(0, 0, 160, 255);
    outline.Width = 3;
    rect->Style.Stroke = outline;
    layer->AddChild(rect);

    auto ellipse = std::make_shared<VectorEllipse>();
    ellipse->Center = Point2Dd(300, 70);
    ellipse->RadiusX = 80;
    ellipse->RadiusY = 40;
    LinearGradientData lg;
    lg.Units = GradientUnits::UserSpaceOnUse;
    lg.Start = Point2Dd(300, 30);
    lg.End = Point2Dd(300, 110);
    lg.Stops = {GradientStop(0, Color(255, 0, 0, 255)), GradientStop(1, Color(0, 0, 255, 255))};
    ellipse->Style.Fill = FillData(GradientData(lg));
    layer->AddChild(ellipse);

    auto line = std::make_shared<VectorLine>();
    line->Start = Point2Dd(40, 150);
    line->End = Point2Dd(200, 190);
    StrokeData dashed;
    dashed.Fill = Color(0, 128, 0, 255);
    dashed.Width = 2;
    dashed.DashArray = {8, 4};
    dashed.EndArrow.Kind = ArrowheadKind::Triangle;
    line->Style.Stroke = dashed;
    layer->AddChild(line);

    auto path = std::make_shared<VectorPath>();
    path->MoveTo(250, 200);
    path->CurveTo(280, 140, 340, 140, 370, 200);
    path->ClosePath();
    path->Style.Fill = Color(128, 0, 255, 255);
    path->Style.FillOpacity = 0.5f;
    layer->AddChild(path);

    // A rotated square: written as a path in page coordinates.
    auto turned = std::make_shared<VectorRect>();
    turned->Bounds = Rect2Dd(-20, -20, 40, 40);
    turned->Transform = Matrix3x3::Translate(430, 180) * Matrix3x3::RotateDegrees(45);
    turned->Style.Fill = Color(0, 160, 160, 255);
    layer->AddChild(turned);

    auto text = std::make_shared<VectorText>();
    text->Position = Point2Dd(250, 300);
    text->BaseStyle.FontFamily = "Liberation Sans";
    text->BaseStyle.FontSize = 20;
    text->BaseStyle.Anchor = TextAnchor::Middle;
    TextSpanData plain;
    plain.Text = "Hello ";
    plain.Style = text->BaseStyle;
    TextSpanData bold = plain;
    bold.Text = "Draw";
    bold.Style.Weight = FontWeight::Bold;
    bold.Style.FillColor = Color(200, 0, 0, 255);
    text->Spans = {plain, bold};
    text->Style.Fill = Color(0, 0, 0, 255);
    layer->AddChild(text);

    // A 2 x 2 PNG: red, green / blue, yellow.
    static const unsigned char png[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
        0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x02, 0x00, 0x00, 0x00, 0xFD, 0xD4, 0x9A, 0x73, 0x00, 0x00, 0x00,
        0x12, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x63, 0xF8, 0xCF, 0x00, 0x04, 0xFF, 0x41, 0xE8, 0xFF, 0x7F, 0x06,
        0x00, 0x1E, 0xEF, 0x04, 0xFC, 0x13, 0x24, 0x17, 0xC2, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE,
        0x42, 0x60, 0x82};
    static_assert(sizeof png == kPngSize, "the test PNG's size");
    auto image = std::make_shared<VectorImage>();
    image->Bounds = Rect2Dd(40, 320, 60, 40);
    image->EmbeddedData.assign(png, png + sizeof png);
    image->MimeType = "image/png";
    layer->AddChild(image);

    auto hidden = doc->AddLayer("Notes");
    hidden->Visible = false;
    auto note = std::make_shared<VectorCircle>();
    note->Center = Point2Dd(450, 350);
    note->Radius = 20;
    note->Style.Fill = Color(255, 255, 0, 255);
    hidden->AddChild(note);
    return doc;
}

void CheckRoundTrip(const VectorDocument& back, const std::string& form) {
    const std::string p = form + ": ";
    Check(Near(back.Size.width, 500, 0.01) && Near(back.Size.height, 400, 0.01),
          p + "the page is the document's size (" + std::to_string(back.Size.width) + " x " +
              std::to_string(back.Size.height) + ")");
    Check(back.Title == "ODG round trip" && back.Author == "UltraCanvas test", p + "title and author");
    Check(back.BackgroundColor && back.BackgroundColor->r == 250 && back.BackgroundColor->b == 240,
          p + "the page background");
    Check(back.GetLayer("Artwork") != nullptr, p + "the layer keeps its name");
    auto notes = back.GetLayer("Notes");
    Check(notes && !notes->Visible, p + "a hidden layer stays hidden");

    auto rects = OfType<VectorRect>(back, VectorElementType::Rectangle);
    std::shared_ptr<VectorRect> red;
    for (auto& r : rects) if (SameColour(r->Style.Fill, Color(220, 20, 30, 255))) red = r;
    Check(red != nullptr, p + "the red rectangle is a rectangle");
    if (red) {
        const Rect2Dd b = PageBounds(red);
        Check(Near(b.x, 40) && Near(b.y, 40) && Near(b.width, 120) && Near(b.height, 60),
              p + "at its place " + Describe(b));
        Check(Near(red->RadiusX, 8), p + "with its corner radius");
        Check(red->Style.Stroke && Near(red->Style.Stroke->Width, 3, 0.01) &&
                  std::get<Color>(red->Style.Stroke->Fill).b == 160,
              p + "and its outline");
        Check(red->Id == "red box", p + "and its name");
    }

    auto ellipses = OfType<VectorEllipse>(back, VectorElementType::Ellipse);
    std::shared_ptr<VectorEllipse> grad;
    for (auto& e : ellipses) if (e->Style.Fill && std::holds_alternative<GradientData>(*e->Style.Fill)) grad = e;
    Check(grad != nullptr, p + "the ellipse keeps a gradient");
    if (grad) {
        const auto& g = std::get<LinearGradientData>(std::get<GradientData>(*grad->Style.Fill));
        Check(Near(grad->Center.x, 300) && Near(grad->RadiusX, 80) && Near(grad->RadiusY, 40), p + "ellipse geometry");
        Check(g.Stops.size() >= 2 && g.Stops.front().color.r == 255 && g.Stops.back().color.b == 255,
              p + "red to blue");
        Check(Near(g.Start.x, g.End.x, 0.5) && g.End.y > g.Start.y, p + "top to bottom");
    }

    auto lines = OfType<VectorLine>(back, VectorElementType::Line);
    Check(lines.size() == 1, p + "one line");
    if (!lines.empty()) {
        const auto& l = *lines.front();
        Check(Near(l.Start.x, 40) && Near(l.Start.y, 150) && Near(l.End.x, 200) && Near(l.End.y, 190), p + "line ends");
        Check(l.Style.Stroke && l.Style.Stroke->DashArray.size() >= 2 && Near(l.Style.Stroke->DashArray[0], 8, 0.01) &&
                  Near(l.Style.Stroke->DashArray[1], 4, 0.01),
              p + "its dashes");
        Check(l.Style.Stroke && l.Style.Stroke->EndArrow.Kind == ArrowheadKind::Triangle &&
                  Near(l.Style.Stroke->EndArrow.Scale, 1, 0.01) && !l.Style.Stroke->StartArrow.IsSet(),
              p + "its arrowhead at the end only");
    }

    int purple = 0, teal = 0;
    for (auto& path : OfType<VectorPath>(back, VectorElementType::Path)) {
        if (SameColour(path->Style.Fill, Color(128, 0, 255, 255))) {
            ++purple;
            const Rect2Dd b = PageBounds(path);
            Check(Near(b.x, 250, 0.1) && Near(b.width, 120, 0.1) && Near(b.y + b.height, 200, 0.1),
                  p + "the curve's bounds " + Describe(b));
            Check(Near(path->Style.FillOpacity, 0.5, 0.01), p + "its fill opacity");
        }
        if (SameColour(path->Style.Fill, Color(0, 160, 160, 255))) {
            ++teal;
            const Rect2Dd b = PageBounds(path);
            const double d = 40 * std::sqrt(2.0);
            Check(Near(b.x + b.width / 2, 430, 0.1) && Near(b.width, d, 0.1) && Near(b.height, d, 0.1),
                  p + "the rotated square, turned on the page " + Describe(b));
        }
    }
    Check(purple == 1 && teal == 1, p + "the curve and the turned square are paths");

    auto texts = OfType<VectorText>(back, VectorElementType::Text);
    Check(texts.size() == 1, p + "one text");
    if (!texts.empty()) {
        const auto& t = *texts.front();
        const Point2Dd at = t.GetGlobalTransform().Transform(t.Position);
        Check(t.GetPlainText() == "Hello Draw", p + "its words (" + t.GetPlainText() + ")");
        Check(t.BaseStyle.Anchor == TextAnchor::Middle && Near(at.x, 250, 0.1) && Near(at.y, 300, 0.1),
              p + "centred on its anchor with its baseline in place");
        Check(t.Spans.size() == 2 && t.Spans[1].Style.Weight == FontWeight::Bold && t.Spans[1].Style.FillColor &&
                  t.Spans[1].Style.FillColor->r == 200 && Near(t.Spans[0].Style.FontSize, 20, 0.01) &&
                  t.Spans[0].Style.FontFamily == "Liberation Sans",
              p + "its spans' font, size, weight and colour");
    }

    auto images = OfType<VectorImage>(back, VectorElementType::Image);
    Check(images.size() == 1 && images.front()->EmbeddedData.size() == kPngSize &&
              images.front()->Source.rfind("data:image/png;base64,", 0) == 0,
          p + "the picture comes back embedded");
    if (!images.empty()) {
        const Rect2Dd b = images.front()->Bounds;
        Check(Near(b.x, 40) && Near(b.y, 320) && Near(b.width, 60) && Near(b.height, 40), p + "in its frame");
    }
}

void TestRoundTrip() {
    std::printf("--- round trip ---\n");
    auto doc = BuildDocument();
    ODGConverter odg;
    std::vector<std::string> notes;
    ConversionOptions options;
    options.WarningCallback = [&](const std::string& m) { notes.push_back(m); };

    const std::string package = odg.ExportToString(*doc, options);
    Check(package.size() > 100 && package.compare(0, 2, "PK") == 0, "the package is a ZIP");
    Check(package.find("mimetypeapplication/vnd.oasis.opendocument.graphics") != std::string::npos,
          "its first entry is the stored media type");
    Check(odg.ValidateData(package), "the converter recognises its package");
    auto back = odg.ImportFromString(package, options);
    Check(back != nullptr, "the package reads back");
    if (back) CheckRoundTrip(*back, ".odg");

    // The package's thumbnail: what the Filer shows without a reader for the
    // drawing, found by the same extractor, the page at 256 pixels on its
    // long side, and the drawing on it (the red box at 40..160 x 40..100 of
    // the 500 x 400 page).
    {
        const std::string path = PathToUtf8(std::filesystem::temp_directory_path() / "uc-odg-thumbnail-test.odg");
        Check(odg.Export(*doc, path, options), "the package is written to a file");
        const std::vector<uint8_t> thumb = ExtractEmbeddedPreviewBytes(path);
        ClipboardDib::RgbaImage image;
        const bool decoded = !thumb.empty() && ClipboardDib::DecodePng(thumb.data(), thumb.size(), image);
        Check(decoded && image.width == 256 && image.height == 205,
              "it carries a 256 x 205 thumbnail of the page (" + std::to_string(image.width) + " x " +
                  std::to_string(image.height) + ")");
        if (decoded && image.IsValid()) {
            auto at = [&](double x, double y) {
                const size_t i = (static_cast<size_t>(y * 205 / 400) * 256 + static_cast<size_t>(x * 256 / 500)) * 4;
                return Color(image.pixels[i], image.pixels[i + 1], image.pixels[i + 2], image.pixels[i + 3]);
            };
            const Color box = at(100, 70), page = at(220, 250);
            Check(box.r > 180 && box.g < 80 && box.b < 80, "the red box is on it");
            Check(page.r > 230 && page.g > 230 && page.b > 200, "on the page's light background");
        }
        Check(package.find("Thumbnails/thumbnail.png") != std::string::npos &&
                  odg.ExportFlatXml(*doc, options).find("thumbnail") == std::string::npos,
              "the package lists it; the flat form has none");
        std::error_code ec;
        std::filesystem::remove(PathFromUtf8(path), ec);
    }

    const std::string flat = odg.ExportFlatXml(*doc, options);
    Check(flat.find("office:mimetype=\"application/vnd.oasis.opendocument.graphics\"") != std::string::npos,
          "the flat form names its media type");
    Check(odg.ValidateData(flat), "the converter recognises the flat form");
    auto flatBack = odg.ImportFromString(flat, options);
    Check(flatBack != nullptr, "the flat form reads back");
    if (flatBack) CheckRoundTrip(*flatBack, ".fodg");
    for (const auto& n : notes) std::printf("    note: %s\n", n.c_str());
}

// ===== LIBREOFFICE'S OWN FILE =====

void TestLibreOfficeSample(const std::string& dir) {
    for (const char* name : {"shapes.odg", "shapes.fodg"}) {
        std::printf("--- LibreOffice Draw: %s ---\n", name);
        ODGConverter odg;
        std::vector<std::string> notes;
        ConversionOptions options;
        options.WarningCallback = [&](const std::string& m) { notes.push_back(m); };
        const std::string path = dir + "/" + name;
        Check(odg.ValidateFile(path), std::string(name) + " is recognised");
        auto doc = odg.Import(path, options);
        Check(doc != nullptr, std::string(name) + " reads");
        if (!doc) continue;
        Check(Near(doc->Size.width, 21 * kCm, 0.1) && Near(doc->Size.height, 29.7 * kCm, 0.1), "an A4 page");
        Check(doc->SourceUnit == LengthUnit::Centimeter, "measured in centimetres");
        Check(doc->Layers.size() == 1 && doc->Layers[0]->Name == "layout", "LibreOffice's layout layer");

        // The custom-shape rectangle at 1 cm, 1 cm, 5 cm wide: orange with
        // a navy 1 mm outline, and its text.
        bool orange = false;
        for (auto& p : OfType<VectorPath>(*doc, VectorElementType::Path)) {
            if (!SameColour(p->Style.Fill, Color(0xff, 0x80, 0x00, 255))) continue;
            const Rect2Dd b = PageBounds(p);
            orange = Near(b.x, kCm, 0.1) && Near(b.y, kCm, 0.1) && Near(b.width, 5 * kCm, 0.1);
            Check(orange, "the orange custom rectangle at its place " + Describe(b));
            Check(p->Style.Stroke && Near(p->Style.Stroke->Width, 0.1 * kCm, 0.01) &&
                      std::get<Color>(p->Style.Stroke->Fill).b == 0x80,
                  "with its 1 mm navy outline");
        }
        Check(orange, "the custom rectangle is read");
        bool hello = false, firstLine = false, secondLine = false;
        for (auto& t : OfType<VectorText>(*doc, VectorElementType::Text)) {
            const std::string s = t->GetPlainText();
            if (s == "Hello ODG") hello = true;
            if (s == "First line") firstLine = Near(t->Spans[0].Style.FontSize, 20, 0.01);
            if (s == "Second line") secondLine = true;
        }
        Check(hello, "the shape's text");
        Check(firstLine && secondLine, "the text box's two paragraphs at 20 pt");

        // The rotated rounded rectangle: rotate(30 degrees) then translate.
        for (auto& r : OfType<VectorRect>(*doc, VectorElementType::Rectangle)) {
            if (!SameColour(r->Style.Fill, Color(0x00, 0xa0, 0x00, 255))) continue;
            const Matrix3x3 m = r->GetGlobalTransform();
            const Point2Dd topRight = m.Transform(Point2Dd(r->Bounds.x + r->Bounds.width, r->Bounds.y));
            // LibreOffice turns it counter-clockwise: the top-right corner rises.
            Check(Near(topRight.x, (12.585 + 5 * std::cos((kPi / 6))) * kCm, 0.2) &&
                      Near(topRight.y, (2.451 - 5 * std::sin((kPi / 6))) * kCm, 0.2),
                  "the green rectangle turned 30 degrees counter-clockwise");
            Check(Near(r->RadiusX, 0.5 * kCm, 0.01), "with its 5 mm corners");
        }

        // The star: ten corners from the enhanced path, inside its 3 cm frame.
        bool star = false;
        for (auto& p : OfType<VectorPath>(*doc, VectorElementType::Path)) {
            if (!SameColour(p->Style.Fill, Color(0xff, 0xd7, 0x00, 255))) continue;
            const Rect2Dd b = PageBounds(p);
            star = Near(b.x, 11 * kCm, 0.1) && Near(b.width, 3 * kCm, 0.1) && p->Path.commands.size() >= 11;
        }
        Check(star, "the five-pointed star from its enhanced path");

        // The round-rectangle custom shape keeps round corners (stretch
        // points): its first corner's radius is the same both ways.
        for (auto& p : OfType<VectorPath>(*doc, VectorElementType::Path)) {
            if (!SameColour(p->Style.Fill, Color(0xa0, 0xa0, 0xff, 255))) continue;
            const auto& c0 = p->Path.commands.front();
            const auto& c1 = p->Path.commands[1];
            const double rx = c0.Parameters[0] - c1.Parameters[4];
            const double ry = c1.Parameters[5] - c0.Parameters[1];
            Check(Near(rx, ry, 0.05) && rx > 5, "round-rectangle corners stay round (" + std::to_string(rx) + " x " +
                                                    std::to_string(ry) + ")");
        }

        // The smiley: the face, two eyes (filled) and the mouth (F: unfilled).
        int smileyParts = 0, unfilled = 0;
        for (auto& p : OfType<VectorPath>(*doc, VectorElementType::Path)) {
            const Rect2Dd b = PageBounds(p);
            if (!(b.x >= kCm - 0.5 && b.x + b.width <= 4 * kCm + 0.5 && b.y >= 20 * kCm - 0.5 && b.y + b.height <= 23 * kCm + 0.5))
                continue;
            ++smileyParts;
            if (p->Style.Fill && std::holds_alternative<std::monostate>(*p->Style.Fill)) ++unfilled;
        }
        Check(smileyParts == 4 && unfilled == 1, "the smiley's four parts, its mouth unfilled (" +
                                                     std::to_string(smileyParts) + ", " + std::to_string(unfilled) + ")");

        // Gradient, hatch, transparency and the picture.
        int gradients = 0, clips = 0, halfOpaque = 0;
        for (auto& e : All(*doc)) {
            if (e->Style.Fill && std::holds_alternative<GradientData>(*e->Style.Fill)) ++gradients;
            if (e->Type == VectorElementType::ClipView) ++clips;
            if (Near(e->Style.FillOpacity, 0.5, 0.01)) ++halfOpaque;
        }
        Check(gradients == 2, "the linear and the radial gradient (" + std::to_string(gradients) + ")");
        Check(clips == 1, "the hatch, clipped to its rectangle");
        Check(halfOpaque == 1, "the 50 % transparent rectangle");
        auto images = OfType<VectorImage>(*doc, VectorElementType::Image);
        Check(images.size() == 1 && !images.front()->EmbeddedData.empty() && Near(images.front()->Bounds.width, 4 * kCm, 0.1),
              "the picture, embedded, 4 cm wide");
        auto groups = OfType<VectorGroup>(*doc, VectorElementType::Group);
        bool pair = false;
        for (auto& g : groups)
            if (g->Children.size() == 2 && g->Children[0]->Type == VectorElementType::Rectangle &&
                g->Children[1]->Type == VectorElementType::Ellipse)
                pair = true;
        Check(pair, "the group of a rectangle and a circle");
        for (const auto& n : notes) std::printf("    note: %s\n", n.c_str());
    }
}

// LibreOffice's preset shapes (customshapes.odg): 36 custom shapes, 17 of
// them named after presets LibreOffice does not have (the OOXML names among
// them) - it draws those as nothing, and so does the reader.
void TestPresetShapes(const std::string& dir) {
    std::printf("--- LibreOffice Draw: customshapes.odg ---\n");
    ODGConverter odg;
    std::vector<std::string> notes;
    ConversionOptions options;
    options.WarningCallback = [&](const std::string& m) { notes.push_back(m); };
    auto doc = odg.Import(dir + "/customshapes.odg", options);
    Check(doc != nullptr, "customshapes.odg reads");
    if (!doc) return;
    bool unknownNoted = false;
    for (const auto& n : notes) unknownNoted = unknownNoted || n.find("without a path") != std::string::npos;
    Check(unknownNoted, "presets without a path are reported");
    // Shapes sit on a grid: 2.8 x 3.2 cm cells from (0.8, 1) cm, 3.3 x 4.5 cm apart.
    auto inCell = [&](const std::shared_ptr<VectorElement>& e, int col, int row) {
        const Rect2Dd b = PageBounds(e);
        const double x0 = (0.8 + 3.3 * col) * kCm, y0 = (1.0 + 4.5 * row) * kCm;
        const double cx = b.x + b.width / 2, cy = b.y + b.height / 2;   // a callout's tail leaves its cell
        return cx >= x0 && cy >= y0 && cx <= x0 + 2.8 * kCm && cy <= y0 + 3.2 * kCm;
    };
    int cells[6][7] = {};
    for (auto& p : OfType<VectorPath>(*doc, VectorElementType::Path))
        for (int r = 0; r < 7; ++r)
            for (int c = 0; c < 6; ++c)
                if (inCell(p, c, r)) ++cells[c][r];
    int drawn = 0;
    for (int r = 0; r < 7; ++r)
        for (int c = 0; c < 6; ++c)
            if (cells[c][r] > 0) ++drawn;
    Check(drawn == 19, "19 of the 36 presets drawn, each in its cell (" + std::to_string(drawn) + ")");
    Check(cells[4][1] == 0 && cells[0][2] == 0, "the unknown 'frame' and 'flower' stay blank, as in LibreOffice");

    // The ring: two circles in one path, filled even-odd so the middle is open.
    for (auto& p : OfType<VectorPath>(*doc, VectorElementType::Path))
        if (inCell(p, 3, 1))
            Check(p->Style.ShapeFillRule == VectorStorage::FillRule::EvenOdd, "the ring is filled even-odd");
    // The cube: front, lighter top, darker side (0x93c47d +0.2 / -0.2).
    std::vector<Color> faces;
    for (auto& p : OfType<VectorPath>(*doc, VectorElementType::Path))
        if (inCell(p, 3, 0) && p->Style.Fill)
            if (auto* c = std::get_if<Color>(&*p->Style.Fill)) faces.push_back(*c);
    Check(faces.size() == 3 && faces[0].g == 0xc4 && faces[1].g > faces[0].g && faces[2].g < faces[0].g,
          "the cube's top lighter and its side darker, as LibreOffice shades them");
}

// LibreOffice's gradient styles (gradients.odg): five 6 x 3 cm rectangles
// filled ellipsoid at 0 and 30 degrees, rectangular, square and radial. An
// ellipsoid is read as a unit circle its Transform stretches to the box's
// width and height over the square root of two and turns counter-clockwise,
// and written back as an ellipsoid at the same angle.
void TestGradientStyles(const std::string& dir) {
    std::printf("--- LibreOffice Draw: gradients.odg ---\n");
    ODGConverter odg;
    ConversionOptions quiet;
    auto doc = odg.Import(dir + "/gradients.odg", quiet);
    Check(doc != nullptr, "gradients.odg reads");
    if (!doc) return;
    std::vector<const RadialGradientData*> radials;
    for (auto& e : All(*doc))
        if (e->Style.Fill)
            if (auto* g = std::get_if<GradientData>(&*e->Style.Fill))
                if (auto* r = std::get_if<RadialGradientData>(g)) radials.push_back(r);
    Check(radials.size() == 5, "five radial-family gradients (" + std::to_string(radials.size()) + ")");
    if (radials.size() != 5) return;
    auto axes = [](const RadialGradientData& r, double& rx, double& ry, double& turn) {
        const Matrix3x3 t = r.Transform.value_or(Matrix3x3::Identity());
        rx = std::hypot(t.m[0][0], t.m[1][0]) * r.Radius;
        ry = std::hypot(t.m[0][1], t.m[1][1]) * r.Radius;
        turn = -std::atan2(t.m[1][0], t.m[0][0]) * 180.0 / kPi;   // counter-clockwise on the page
    };
    double rx, ry, turn;
    axes(*radials[0], rx, ry, turn);
    Check(radials[0]->Transform && Near(rx, 6 * kCm / std::sqrt(2.0), 0.05) && Near(ry, 3 * kCm / std::sqrt(2.0), 0.05) &&
              Near(turn, 0, 0.01),
          "the ellipsoid is the ellipse of the box over the square root of two");
    axes(*radials[1], rx, ry, turn);
    Check(radials[1]->Transform && Near(rx / ry, 2, 0.001) && Near(turn, 30, 0.01),
          "the second turned 30 degrees counter-clockwise (" + std::to_string(turn) + ")");
    Check(!radials[4]->Transform && Near(radials[4]->Radius, std::hypot(6.0, 3.0) / 2 * kCm, 0.05),
          "the radial one a circle of half the diagonal");

    const std::string flat = odg.ExportFlatXml(*doc, quiet);
    Check(flat.find("draw:style=\"ellipsoid\" draw:angle=\"0deg\"") != std::string::npos &&
              flat.find("draw:style=\"ellipsoid\" draw:angle=\"30deg\"") != std::string::npos &&
              flat.find("draw:style=\"radial\"") != std::string::npos,
          "written back as ellipsoids at 0 and 30 degrees and a radial");
}

// ===== HAND-WRITTEN GEOMETRY =====

std::string FlatDocument(const std::string& shapes, const std::string& styles = {}) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
           "<office:document xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\""
           " xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\""
           " xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\""
           " xmlns:d=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\""
           " xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\""
           " xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\""
           " office:mimetype=\"application/vnd.oasis.opendocument.graphics\">"
           "<office:styles>" + styles + "</office:styles>"
           "<office:automatic-styles><style:page-layout style:name=\"PM\"><style:page-layout-properties"
           " fo:page-width=\"10cm\" fo:page-height=\"10cm\"/></style:page-layout></office:automatic-styles>"
           "<office:master-styles><style:master-page style:name=\"M\" style:page-layout-name=\"PM\"/></office:master-styles>"
           "<office:body><office:drawing><d:page d:master-page-name=\"M\">" + shapes +
           "</d:page><d:page/></office:drawing></office:body></office:document>";
}

void TestGeometry() {
    std::printf("--- hand-written geometry ---\n");
    ODGConverter odg;
    std::vector<std::string> notes;
    ConversionOptions options;
    options.WarningCallback = [&](const std::string& m) { notes.push_back(m); };

    // The drawing namespace bound to "d:", not "draw:"; an ellipse from the
    // U command; a stretch-pointed bar; a quadrant path; equations with
    // functions and a modifier; and a rotated, translated rectangle.
    const std::string shapes =
        "<d:custom-shape svg:x=\"1cm\" svg:y=\"1cm\" svg:width=\"2cm\" svg:height=\"1cm\">"
        "<d:enhanced-geometry svg:viewBox=\"0 0 21600 21600\" d:enhanced-path=\"U 10800 10800 10800 10800 0 360 Z N\"/>"
        "</d:custom-shape>"
        "<d:custom-shape svg:x=\"1cm\" svg:y=\"3cm\" svg:width=\"4cm\" svg:height=\"1cm\">"
        "<d:enhanced-geometry svg:viewBox=\"0 0 21600 21600\" d:path-stretchpoint-x=\"10800\" d:path-stretchpoint-y=\"10800\""
        " d:modifiers=\"5400\" d:enhanced-path=\"M ?f0 0 L ?f1 0 21600 21600 0 21600 Z N\">"
        "<d:equation d:name=\"f0\" d:formula=\"$0 \"/><d:equation d:name=\"f1\" d:formula=\"right-max(?f0 ,0)+if(-1,5,0)\"/>"
        "</d:enhanced-geometry></d:custom-shape>"
        "<d:custom-shape svg:x=\"6cm\" svg:y=\"1cm\" svg:width=\"2cm\" svg:height=\"2cm\">"
        "<d:enhanced-geometry svg:viewBox=\"0 0 100 100\" d:mirror-horizontal=\"true\" d:enhanced-path=\"M 0 0 X 100 100 L 0 100 Z N\"/>"
        "</d:custom-shape>"
        "<d:rect svg:width=\"2cm\" svg:height=\"1cm\" d:transform=\"rotate (1.5707963267949) translate (5cm 8cm)\"/>"
        "<d:path svg:x=\"1cm\" svg:y=\"6cm\" svg:width=\"2cm\" svg:height=\"2cm\" svg:viewBox=\"0 0 1000 1000\" svg:d=\"M0 0 L1000 1000\"/>";
    auto doc = odg.ImportFromString(FlatDocument(shapes), options);
    Check(doc != nullptr, "the hand-written document reads (a document with its namespace under another prefix)");
    if (!doc) return;
    Check(Near(doc->Size.width, 10 * kCm, 0.01), "its 10 cm page");
    bool pagesNoted = false;
    for (const auto& n : notes) pagesNoted = pagesNoted || n.find("2 pages") != std::string::npos;
    Check(pagesNoted, "the second page is reported, not read");

    auto paths = OfType<VectorPath>(*doc, VectorElementType::Path);
    Check(paths.size() == 4, "four paths (" + std::to_string(paths.size()) + ")");
    if (paths.size() < 4) return;
    const Rect2Dd ellipse = PageBounds(paths[0]);
    Check(Near(ellipse.x, kCm, 0.05) && Near(ellipse.width, 2 * kCm, 0.05) && Near(ellipse.height, kCm, 0.05),
          "U draws the ellipse filling its frame " + Describe(ellipse));

    // Stretch points: a 4 x 1 cm frame scales by the height (1 cm per 21600);
    // x past 10800 moves by the extra width. ?f0 = 5400 -> 0.25 cm,
    // ?f1 = 21600 - 5400 = 16200 -> 0.75 cm + 3 cm.
    const auto& bar = paths[1]->Path.commands;
    Check(Near(bar[0].Parameters[0], 1.25 * kCm, 0.05) && Near(bar[1].Parameters[0], 4.75 * kCm, 0.05),
          "stretch points move the far coordinates instead of stretching them (" +
              std::to_string(bar[0].Parameters[0] / kCm) + ", " + std::to_string(bar[1].Parameters[0] / kCm) + " cm)");

    // X: a quarter ellipse leaving horizontally, mirrored: it starts at the
    // frame's right edge.
    const auto& quad = paths[2]->Path.commands;
    Check(quad.size() >= 2 && quad[1].Type == PathCommandType::CurveTo && Near(quad[0].Parameters[0], 8 * kCm, 0.05) &&
              Near(quad[1].Parameters[1], 1 * kCm, 0.05),
          "the X quadrant leaves horizontally, mirrored to the right edge");

    // The plain path through its viewBox.
    const Rect2Dd diag = PageBounds(paths[3]);
    Check(Near(diag.x, kCm, 0.05) && Near(diag.width, 2 * kCm, 0.05) && Near(diag.y, 6 * kCm, 0.05),
          "a path is mapped from its viewBox to its frame");
    Check(paths[3]->Style.Fill && std::holds_alternative<std::monostate>(*paths[3]->Style.Fill),
          "an open path is not filled");

    // rotate(pi/2) turns counter-clockwise, then translate: the 2 x 1 cm
    // rectangle stands up with its top-left corner at (5, 8) cm and its
    // top-right corner 2 cm above it.
    auto rects = OfType<VectorRect>(*doc, VectorElementType::Rectangle);
    Check(rects.size() == 1, "one rectangle");
    if (!rects.empty()) {
        const Matrix3x3 m = rects[0]->GetGlobalTransform();
        const Point2Dd tl = m.Transform(Point2Dd(0, 0)), tr = m.Transform(Point2Dd(2 * kCm, 0));
        Check(Near(tl.x, 5 * kCm, 0.05) && Near(tl.y, 8 * kCm, 0.05) && Near(tr.x, 5 * kCm, 0.05) &&
                  Near(tr.y, 6 * kCm, 0.05),
              "draw:transform: rotate then translate, counter-clockwise");
        Check(SameColour(rects[0]->Style.Fill, Color(0x72, 0x9f, 0xcf, 255)),
              "a shape with no style takes LibreOffice's default fill");
    }

    // Styles: inheritance, a dash in percent of the line width, and a
    // hatch.
    const std::string styles =
        "<d:stroke-dash d:name=\"D\" d:style=\"rect\" d:dots1=\"2\" d:dots1-length=\"200%\" d:distance=\"100%\"/>"
        "<style:style style:name=\"base\" style:family=\"graphic\"><style:graphic-properties svg:stroke-width=\"2pt\""
        " svg:stroke-color=\"#112233\" d:fill=\"solid\" d:fill-color=\"#445566\"/></style:style>"
        "<style:style style:name=\"dashed\" style:family=\"graphic\" style:parent-style-name=\"base\">"
        "<style:graphic-properties d:stroke=\"dash\" d:stroke-dash=\"D\"/></style:style>";
    auto styled = odg.ImportFromString(
        FlatDocument("<d:rect d:style-name=\"dashed\" svg:x=\"0cm\" svg:y=\"0cm\" svg:width=\"1cm\" svg:height=\"1cm\"/>",
                     styles),
        options);
    auto sr = styled ? OfType<VectorRect>(*styled, VectorElementType::Rectangle) : std::vector<std::shared_ptr<VectorRect>>{};
    Check(sr.size() == 1, "the styled rectangle reads");
    if (!sr.empty()) {
        const auto& s = sr[0]->Style;
        Check(SameColour(s.Fill, Color(0x44, 0x55, 0x66, 255)), "its fill comes from the parent style");
        Check(s.Stroke && Near(s.Stroke->Width, 2, 0.01) && std::get<Color>(s.Stroke->Fill).r == 0x11,
              "and its line too");
        Check(s.Stroke && s.Stroke->DashArray == std::vector<double>({4, 2, 4, 2}),
              "dash lengths in percent of the line width, twice");
    }
}

void TestDispatch() {
    std::printf("--- plugin dispatch ---\n");
    for (const char* ext : {"odg", "otg", "fodg"}) {
        auto c = UltraCanvasVectorFormatsPlugin::CreateConverterForExtension(ext);
        Check(c && c->GetFormat() == VectorFormat::ODG && c->CanImport() && c->CanExport(),
              std::string(".") + ext + " goes to the ODG converter");
    }
    UltraCanvasVectorFormatsPlugin plugin;
    auto has = [](const std::vector<std::string>& v, const char* e) { return std::find(v.begin(), v.end(), e) != v.end(); };
    Check(has(plugin.GetSupportedExtensions(), "odg") && has(plugin.GetSaveExtensions(), "odg") &&
              has(plugin.GetSaveExtensions(), "fodg"),
          "the plugin lists .odg for loading and saving");
}

int Render(const std::string& in, const std::string& out, int width) {
    auto doc = UltraCanvasVectorFormatsPlugin::LoadVectorDocument(in);
    if (!doc) { std::fprintf(stderr, "cannot read %s\n", in.c_str()); return 1; }
    VectorRasterOptions options;
    options.width = width;
    options.background = RasterPixel(255, 255, 255, 255);
    std::string error;
    auto layer = RasterizeVectorDocument(*doc, options, error);
    if (!layer) { std::fprintf(stderr, "cannot draw: %s\n", error.c_str()); return 1; }
    std::ofstream f(out, std::ios::binary);
    f << "P6\n" << layer->GetWidth() << " " << layer->GetHeight() << "\n255\n";
    for (int y = 0; y < layer->GetHeight(); ++y)
        for (int x = 0; x < layer->GetWidth(); ++x) {
            const RasterPixel p = layer->GetPixel(x, y);
            auto over = [&](uint8_t c) { return static_cast<char>((c * p.a + 255 * (255 - p.a)) / 255); };
            f.put(over(p.r)).put(over(p.g)).put(over(p.b));
        }
    return 0;
}

}   // namespace

int main(int argc, char** argv) {
    // Pictures are decoded through libvips, which wants its start-up call.
    UCImage::InitializeImageSubsysterm("ODGConverterTest");
    if (argc >= 4 && std::string(argv[1]) == "--convert") {
        auto doc = UltraCanvasVectorFormatsPlugin::LoadVectorDocument(argv[2]);
        if (!doc) { std::fprintf(stderr, "cannot read %s\n", argv[2]); return 1; }
        return UltraCanvasVectorFormatsPlugin::SaveVectorDocument(*doc, argv[3]) ? 0 : 1;
    }
    if (argc >= 4 && std::string(argv[1]) == "--render")
        return Render(argv[2], argv[3], argc >= 5 ? std::atoi(argv[4]) : 1050);
    TestRoundTrip();
    TestLibreOfficeSample(std::string(VECTOR_SAMPLES_DIR) + "/ODG");
    TestPresetShapes(std::string(VECTOR_SAMPLES_DIR) + "/ODG");
    TestGradientStyles(std::string(VECTOR_SAMPLES_DIR) + "/ODG");
    TestGeometry();
    TestDispatch();
    std::printf("%d failure(s)\n", failures);
    return failures;
}
