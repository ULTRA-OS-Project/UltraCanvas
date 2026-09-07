// Tests/RasterEditingTest.cpp
// Headless checks of the raster-editing layer: UCRasterLayer blend and
// composite arithmetic, UCRasterSelection shapes and algebra, the brush
// engine (strokes, opacity cap, fills, shapes, gradients, wand),
// UCRasterDocument layers / undo / redo / selection-aware filters, and the
// PNG and .ucraster round trips (the last two only with libvips).
// Version: 1.0.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework

#include "UltraCanvasRasterLayer.h"
#include "UltraCanvasRasterSelection.h"
#include "UltraCanvasRasterDocument.h"
#include "UltraCanvasBrushEngine.h"

#ifdef HAS_LIBVIPS
#include "PixelFX/PixelFX.h"
#include <vips/vips8>
#endif

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

using namespace UltraCanvas;

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { ++checks; if (!(cond)) { ++failures; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_NEAR(a, b, tol) do { ++checks; if (std::abs(static_cast<int>(a) - static_cast<int>(b)) > (tol)) { ++failures; std::printf("FAIL %s:%d: %s=%d vs %s=%d\n", __FILE__, __LINE__, #a, static_cast<int>(a), #b, static_cast<int>(b)); } } while (0)

static void TestLayerBasics() {
    UCRasterLayer l(8, 4, RasterPixel(10, 20, 30, 255), "t");
    CHECK(l.IsValid());
    CHECK(l.GetPixel(0, 0) == RasterPixel(10, 20, 30, 255));
    CHECK(l.GetPixel(-1, 0) == RasterPixel(0, 0, 0, 0));
    l.SetPixel(7, 3, RasterPixel(1, 2, 3, 4));
    CHECK(l.GetPixel(7, 3) == RasterPixel(1, 2, 3, 4));
    l.FlipHorizontal();
    CHECK(l.GetPixel(0, 3) == RasterPixel(1, 2, 3, 4));
    l.FlipVertical();
    CHECK(l.GetPixel(0, 0) == RasterPixel(1, 2, 3, 4));
    l.Rotate90(true);
    CHECK(l.GetWidth() == 4 && l.GetHeight() == 8);
    CHECK(l.GetPixel(3, 0) == RasterPixel(1, 2, 3, 4));   // top-left goes to top-right
    l.Rotate90(false);
    CHECK(l.GetPixel(0, 0) == RasterPixel(1, 2, 3, 4));
    l.ResizeCanvas(10, 6, 1, 1);
    CHECK(l.GetWidth() == 10 && l.GetPixel(1, 1) == RasterPixel(1, 2, 3, 4) && l.GetPixel(0, 0).a == 0);
    auto crop = l.CropCopy(Rect2Di(1, 1, 2, 2));
    CHECK(crop->GetWidth() == 2 && crop->GetPixel(0, 0) == RasterPixel(1, 2, 3, 4));
    CHECK(!l.IsFullyOpaque());
    CHECK(l.GetOpaqueBounds() == Rect2Di(1, 1, 8, 4) || l.GetOpaqueBounds().width == 8);
}

static void TestBlend() {
    // Normal source-over: 50 % red over opaque white -> pink
    RasterPixel out = RasterBlendPixel(RasterPixel(255, 0, 0, 128), RasterPixel(255, 255, 255, 255), 1.0f);
    CHECK_NEAR(out.r, 255, 1); CHECK_NEAR(out.g, 127, 2); CHECK_NEAR(out.b, 127, 2); CHECK(out.a == 255);
    // over transparent: colour is the source, alpha the source alpha
    out = RasterBlendPixel(RasterPixel(10, 20, 30, 100), RasterPixel(0, 0, 0, 0), 1.0f);
    CHECK(out == RasterPixel(10, 20, 30, 100));
    // coverage scales alpha
    out = RasterBlendPixel(RasterPixel(0, 0, 0, 255), RasterPixel(255, 255, 255, 255), 0.5f);
    CHECK_NEAR(out.r, 127, 2);
    // multiply of 50 % grey over 50 % grey -> 25 %
    CHECK_NEAR(RasterBlendChannel(RasterBlendMode::Multiply, 128, 128), 64, 1);
    CHECK_NEAR(RasterBlendChannel(RasterBlendMode::Screen, 128, 128), 191, 1);
    CHECK(RasterBlendChannel(RasterBlendMode::Difference, 200, 50) == 150);
    CHECK(RasterBlendChannel(RasterBlendMode::Addition, 200, 100) == 255);

    // composite onto premultiplied ARGB32
    UCRasterLayer l(2, 1, RasterPixel(255, 0, 0, 128));
    uint32_t dst[2] = { 0xFFFFFFFFu, 0x00000000u };
    l.CompositeOnto(dst, 2, 1, 2, Rect2Di(0, 0, 2, 1), 1.0f, RasterBlendMode::Normal);
    CHECK(((dst[0] >> 24) & 0xFF) == 255);
    CHECK_NEAR((dst[0] >> 16) & 0xFF, 255, 1);
    CHECK_NEAR((dst[0] >> 8) & 0xFF, 127, 2);
    CHECK_NEAR((dst[1] >> 24) & 0xFF, 128, 1);
    CHECK_NEAR((dst[1] >> 16) & 0xFF, 128, 1);   // premultiplied red
}

static void TestSelection() {
    UCRasterSelection s(20, 20);
    CHECK(!s.IsActive());
    CHECK(s.Coverage(5, 5) == 255);   // inactive == everything
    s.SetRectangle(Rect2Di(2, 2, 4, 4));
    CHECK(s.IsActive());
    CHECK(s.Coverage(3, 3) == 255 && s.Coverage(10, 10) == 0);
    CHECK(s.GetBounds() == Rect2Di(2, 2, 4, 4));
    s.SetRectangle(Rect2Di(4, 4, 4, 4), RasterSelectionMode::Add);
    CHECK(s.Coverage(7, 7) == 255 && s.Coverage(2, 2) == 255);
    s.SetRectangle(Rect2Di(0, 0, 5, 5), RasterSelectionMode::Subtract);
    CHECK(s.Coverage(2, 2) == 0 && s.Coverage(7, 7) == 255);
    s.SetRectangle(Rect2Di(6, 6, 10, 10), RasterSelectionMode::Intersect);
    CHECK(s.Coverage(7, 7) == 255 && s.Coverage(5, 5) == 0);
    s.Invert();
    CHECK(s.Coverage(7, 7) == 0 && s.Coverage(15, 15) == 255);
    s.SelectNone();
    CHECK(!s.IsActive());
    s.SetEllipse(Rect2Di(0, 0, 20, 20));
    CHECK(s.Coverage(10, 10) == 255 && s.Coverage(0, 0) == 0);
    CHECK(s.Coverage(10, 0) > 0);   // edge pixel partially covered / on the rim
    s.SetPolygon({ Point2Df(0, 0), Point2Df(10, 0), Point2Df(10, 10), Point2Df(0, 10) });
    CHECK(s.Coverage(5, 5) == 255 && s.Coverage(15, 15) == 0);
    const auto& outline = s.GetOutline();
    CHECK(!outline.empty());
    s.Feather(2);
    CHECK(s.Coverage(5, 5) == 255 && s.Coverage(10, 5) > 0 && s.Coverage(10, 5) < 255);
    s.SetRectangle(Rect2Di(5, 5, 4, 4));
    s.Grow(1);
    CHECK(s.Coverage(4, 5) == 255 && s.Coverage(3, 5) == 0);
    s.Shrink(2);
    CHECK(s.Coverage(4, 5) == 0 && s.Coverage(6, 6) == 255);
    s.Translate(3, 0);
    CHECK(s.Coverage(9, 6) == 255 && s.Coverage(6, 6) == 0);
}

static void TestBrush() {
    auto layer = std::make_shared<UCRasterLayer>(64, 64, RasterPixel(255, 255, 255, 255));
    UCBrushSettings s;
    s.size = 10; s.hardness = 1.0f; s.opacity = 0.5f; s.flow = 1.0f; s.spacing = 0.1f;
    UCBrushStroke stroke;
    stroke.Begin(layer, nullptr, s, BrushMode::Paint, RasterPixel(0, 0, 0, 255));
    stroke.AddPoint(10, 32);
    stroke.AddPoint(50, 32);
    // go back over the same pixels: opacity must cap, not stack
    stroke.AddPoint(10, 32);
    const Rect2Di dirty = stroke.GetDirtyBounds();
    CHECK(dirty.width > 30 && dirty.height >= 10);
    CHECK_NEAR(layer->GetPixel(30, 32).r, 127, 3);   // 50 % black over white
    CHECK(layer->GetPixel(30, 5).r == 255);           // untouched
    auto before = stroke.GetBefore();
    CHECK(before && before->GetPixel(30, 32).r == 255);
    stroke.End();

    // erase to transparency
    stroke.Begin(layer, nullptr, s, BrushMode::Erase, RasterPixel());
    stroke.AddPoint(30, 32);
    stroke.End();
    CHECK_NEAR(layer->GetPixel(30, 32).a, 127, 3);

    // selection restricts the stroke
    UCRasterSelection sel(64, 64);
    sel.SetRectangle(Rect2Di(0, 0, 32, 64));
    auto layer2 = std::make_shared<UCRasterLayer>(64, 64, RasterPixel(255, 255, 255, 255));
    s.opacity = 1.0f;
    stroke.Begin(layer2, &sel, s, BrushMode::Paint, RasterPixel(0, 0, 0, 255));
    stroke.AddPoint(28, 32);
    stroke.AddPoint(36, 32);
    stroke.End();
    CHECK(layer2->GetPixel(29, 32).r == 0 && layer2->GetPixel(35, 32).r == 255);

    // one-pixel pencil paints exactly one pixel per dab
    auto layer3 = std::make_shared<UCRasterLayer>(16, 16, RasterPixel(255, 255, 255, 255));
    UCBrushSettings pencil; pencil.size = 1; pencil.hardness = 1; pencil.antialias = false;
    stroke.Begin(layer3, nullptr, pencil, BrushMode::Paint, RasterPixel(0, 0, 0, 255));
    stroke.AddPoint(5.5f, 5.5f);
    stroke.End();
    CHECK(layer3->GetPixel(5, 5).r == 0 && layer3->GetPixel(6, 5).r == 255 && layer3->GetPixel(5, 6).r == 255);
}

static void TestRasterPaint() {
    UCRasterLayer l(40, 40, RasterPixel(255, 255, 255, 255));
    RasterPaint::ShapeStyle st;
    st.stroke = RasterPixel(0, 0, 255, 255); st.strokeWidth = 2; st.fill = RasterPixel(255, 0, 0, 255);
    Rect2Di r = RasterPaint::DrawRectangle(l, nullptr, Rect2Df(10, 10, 20, 20), st);
    CHECK(r.width > 0);
    CHECK(l.GetPixel(20, 20) == RasterPixel(255, 0, 0, 255));   // filled
    CHECK(l.GetPixel(10, 20).b == 255 && l.GetPixel(10, 20).r < 128); // outline
    CHECK(l.GetPixel(2, 2) == RasterPixel(255, 255, 255, 255));

    RasterPaint::DrawEllipse(l, nullptr, Rect2Df(0, 0, 40, 40), st);
    CHECK(l.GetPixel(20, 0).b == 255 && l.GetPixel(20, 0).r < 128);   // on the 2 px ring
    CHECK(l.GetPixel(20, 3) == RasterPixel(255, 0, 0, 255));           // inside: red fill

    UCRasterLayer g(10, 10, RasterPixel(0, 0, 0, 0));
    RasterPaint::FillGradient(g, nullptr, Point2Df(0, 5), Point2Df(10, 5), RasterPixel(0, 0, 0, 255),
                              RasterPixel(255, 255, 255, 255), RasterPaint::GradientKind::Linear);
    CHECK(g.GetPixel(0, 5).r < 30 && g.GetPixel(9, 5).r > 225 && g.GetPixel(5, 5).r > 100 && g.GetPixel(5, 5).r < 160);

    // flood fill: a white square with a black frame, fill inside
    UCRasterLayer f(20, 20, RasterPixel(255, 255, 255, 255));
    RasterPaint::ShapeStyle frame; frame.stroke = RasterPixel(0, 0, 0, 255); frame.strokeWidth = 2; frame.antialias = false;
    RasterPaint::DrawRectangle(f, nullptr, Rect2Df(5, 5, 10, 10), frame);
    Rect2Di changed = RasterPaint::FloodFill(f, nullptr, 10, 10, RasterPixel(0, 255, 0, 255), 0, true);
    CHECK(changed.width > 0 && changed.width < 12);
    CHECK(f.GetPixel(10, 10) == RasterPixel(0, 255, 0, 255));
    CHECK(f.GetPixel(1, 1) == RasterPixel(255, 255, 255, 255));    // outside untouched
    // global fill replaces every white pixel
    RasterPaint::FloodFill(f, nullptr, 1, 1, RasterPixel(0, 0, 255, 255), 0, false);
    CHECK(f.GetPixel(1, 1).b == 255 && f.GetPixel(18, 18).b == 255);
    std::vector<uint8_t> wand = RasterPaint::MagicWandMask(f, 10, 10, 0, true);
    CHECK(wand[10 * 20 + 10] == 255 && wand[1 * 20 + 1] == 0);

    CHECK(RasterPaint::SampleColour(f, 10, 10, 0) == RasterPixel(0, 255, 0, 255));
    CHECK(RasterPaint::PixelDistance(RasterPixel(0, 0, 0, 255), RasterPixel(255, 255, 255, 255)) == 191);

    uint8_t mask[4] = { 255, 0, 0, 255 };
    UCRasterLayer m(2, 2, RasterPixel(255, 255, 255, 255));
    RasterPaint::StampMask(m, nullptr, mask, 2, 2, 0, 0, RasterPixel(0, 0, 0, 255));
    CHECK(m.GetPixel(0, 0).r == 0 && m.GetPixel(1, 0).r == 255 && m.GetPixel(1, 1).r == 0);
}

static void TestDocument() {
    UCRasterDocument doc(30, 20, RasterPixel(255, 255, 255, 255));
    CHECK(doc.IsValid() && doc.GetLayerCount() == 1);
    int structureEvents = 0, pixelEvents = 0;
    doc.onStructureChanged = [&]() { ++structureEvents; };
    doc.onPixelsChanged = [&](const Rect2Di&) { ++pixelEvents; };

    const int idx = doc.AddLayer("Top", RasterPixel(0, 0, 0, 0));
    CHECK(idx == 1 && doc.GetActiveLayerIndex() == 1 && structureEvents > 0);
    CHECK(doc.CanUndo());

    // pixel edit with undo
    doc.BeginEdit("Dot", 1, Rect2Di(0, 0, 30, 20));
    doc.GetLayer(1)->SetPixel(5, 5, RasterPixel(255, 0, 0, 255));
    doc.EndEdit();
    CHECK(pixelEvents > 0);
    CHECK(doc.GetLayer(1)->GetPixel(5, 5).a == 255);
    auto pm = doc.GetCompositePixmap();
    CHECK(pm && pm->GetWidth() == 30);
    CHECK((pm->GetPixel(5, 5) & 0x00FF0000u) == 0x00FF0000u);
    CHECK((pm->GetPixel(6, 5) & 0x00FFFFFFu) == 0x00FFFFFFu);   // white below shows through
    doc.Undo();
    CHECK(doc.GetLayer(1)->GetPixel(5, 5).a == 0);
    doc.Redo();
    CHECK(doc.GetLayer(1)->GetPixel(5, 5).a == 255);

    // structural undo restores the layer list
    doc.Undo(); doc.Undo();
    CHECK(doc.GetLayerCount() == 1);
    doc.Redo();
    CHECK(doc.GetLayerCount() == 2);
    doc.Redo();
    CHECK(doc.GetLayer(1)->GetPixel(5, 5).a == 255);

    // attribute edit is undoable and does not touch pixels
    doc.SetLayerOpacity(1, 0.5f);
    CHECK(doc.GetLayer(1)->opacity == 0.5f);
    doc.Undo();
    CHECK(doc.GetLayer(1)->opacity == 1.0f && doc.GetLayer(1)->GetPixel(5, 5).a == 255);

    // selection-aware fill + delete
    doc.GetSelection().SetRectangle(Rect2Di(0, 0, 10, 20));
    doc.CommitSelectionChange("sel");
    doc.FillSelection(RasterPixel(0, 255, 0, 255));
    CHECK(doc.GetLayer(1)->GetPixel(2, 2).g == 255 && doc.GetLayer(1)->GetPixel(20, 2).a == 0);
    doc.DeleteSelection();
    CHECK(doc.GetLayer(1)->GetPixel(2, 2).a == 0);
    // put the dot back (DeleteSelection erased it) and copy it out
    doc.BeginEdit("Dot again", 1, Rect2Di(5, 5, 1, 1));
    doc.GetLayer(1)->SetPixel(5, 5, RasterPixel(255, 0, 0, 255));
    doc.EndEdit();
    Point2Di origin;
    doc.GetSelection().SelectNone();
    doc.GetSelection().SetRectangle(Rect2Di(4, 4, 3, 3));
    doc.CommitSelectionChange("sel2");
    auto copied = doc.CopySelection(origin);
    CHECK(copied && copied->GetWidth() == 3 && origin.x == 4 && copied->GetPixel(1, 1).a == 255);

    // geometry
    doc.Rotate90(true);
    CHECK(doc.GetWidth() == 20 && doc.GetHeight() == 30);
    doc.Undo();
    CHECK(doc.GetWidth() == 30);
    doc.CropTo(Rect2Di(2, 2, 10, 10));
    CHECK(doc.GetWidth() == 10 && doc.GetLayer(1)->GetPixel(3, 3).a == 255);   // the (5,5) dot moved to (3,3)
    doc.ScaleImage(20, 20);
    CHECK(doc.GetWidth() == 20);
    doc.MergeLayerDown(1);
    CHECK(doc.GetLayerCount() == 1);
    doc.Undo();
    CHECK(doc.GetLayerCount() == 2);
    doc.FlattenImage();
    CHECK(doc.GetLayerCount() == 1);
    auto flat = doc.Flatten();
    CHECK(flat->GetWidth() == 20);
}

#ifdef HAS_LIBVIPS
static void TestPixelFXAndFiles() {
    UCRasterLayer l(16, 8, RasterPixel(200, 100, 50, 255));
    l.SetPixel(0, 0, RasterPixel(1, 2, 3, 4));
    PixelFX::PFXImage img = l.ToPixelFX();
    CHECK(img.Width() == 16 && img.Height() == 8 && img.Bands() == 4);
    UCRasterLayer back;
    CHECK(back.FromPixelFX(img));
    CHECK(back.GetPixel(0, 0) == RasterPixel(1, 2, 3, 4) && back.GetPixel(5, 5) == RasterPixel(200, 100, 50, 255));
    // a 3-band result gets an opaque alpha
    CHECK(back.FromPixelFX(PixelFX::Colour::ExtractBand(img, 0, 3)));
    CHECK(back.GetPixel(5, 5).a == 255);

    UCRasterDocument doc(16, 16, RasterPixel(100, 100, 100, 255));
    doc.GetSelection().SetRectangle(Rect2Di(0, 0, 8, 16));
    doc.CommitSelectionChange("half");
    CHECK(doc.ApplyFilter("Invert", [](const PixelFX::PFXImage& i) {
        PixelFX::PFXImage rgb = PixelFX::Colour::Invert(PixelFX::Colour::ExtractBand(i, 0, 3));
        return PixelFX::Colour::Bandjoin(rgb, PixelFX::Colour::ExtractBand(i, 3));
    }));
    CHECK(doc.GetLayer(0)->GetPixel(2, 2).r == 155 && doc.GetLayer(0)->GetPixel(12, 2).r == 100);
    doc.Undo();
    CHECK(doc.GetLayer(0)->GetPixel(2, 2).r == 100);
    auto preview = doc.PreviewFilter([](const PixelFX::PFXImage& i) { return PixelFX::Colour::Invert(i); });
    CHECK(preview && doc.GetLayer(0)->GetPixel(2, 2).r == 100);

    const std::string dir = (std::filesystem::temp_directory_path() / "ultracanvas-raster-test").string();
    std::filesystem::create_directories(dir);
    std::string err;
    const std::string png = dir + "/a.png";
    CHECK(doc.SaveToFile(png, err));
    UCRasterDocument loaded;
    CHECK(loaded.LoadFromFile(png, err));
    CHECK(loaded.GetWidth() == 16 && loaded.GetLayer(0)->GetPixel(2, 2) == RasterPixel(100, 100, 100, 255));

    doc.AddLayer("Over", RasterPixel(0, 0, 0, 0));
    doc.GetLayer(1)->SetPixel(3, 3, RasterPixel(9, 8, 7, 200));
    doc.SetLayerOpacity(1, 0.25f);
    doc.SetLayerBlendMode(1, RasterBlendMode::Screen);
    const std::string proj = dir + "/a.ucraster";
    CHECK(UCRasterDocument::IsProjectFile(proj));
    CHECK(doc.SaveProject(proj, err));
    UCRasterDocument p2;
    CHECK(p2.LoadProject(proj, err));
    CHECK(p2.GetLayerCount() == 2 && p2.GetLayer(1)->name == "Over");
    CHECK(p2.GetLayer(1)->GetPixel(3, 3) == RasterPixel(9, 8, 7, 200));
    CHECK(p2.GetLayer(1)->opacity == 0.25f && p2.GetLayer(1)->blendMode == RasterBlendMode::Screen);
    std::filesystem::remove_all(dir);
}
#endif

int main() {
#ifdef HAS_LIBVIPS
    if (VIPS_INIT("RasterEditingTest")) { std::printf("vips init failed\n"); return 1; }
#endif
    TestLayerBasics();
    TestBlend();
    TestSelection();
    TestBrush();
    TestRasterPaint();
    TestDocument();
#ifdef HAS_LIBVIPS
    TestPixelFXAndFiles();
#endif
    std::printf("RasterEditingTest: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
