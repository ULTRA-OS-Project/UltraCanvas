// Tests/SlideEditElementTest.cpp
// The slide editor element driven by pointer and key events, as a person
// uses it (no window: the events go straight to OnEvent):
//  - a click selects the shape under it, a click on nothing clears;
//  - a drag moves the selection and is one undo step, snapping to the
//    slide's centre;
//  - a corner handle resizes, Shift keeping the proportions;
//  - a band selects what it covers;
//  - the turning handle turns, Shift by 15 degrees;
//  - in the strip a click chooses a slide and a drag moves it;
//  - the keys: arrows nudge, Delete, Ctrl+Z, Ctrl+M;
//  - double-click asks for a shape's text (onEditText), and a chart's data
//    (onEditChart);
//  - groups: a band and Ctrl+G group, a click selects the group, a drag
//    moves it, its corner handle resizes it with its shapes, its turning
//    handle turns it as one, a double-click selects inside it, Escape goes
//    back, Ctrl+Shift+G ungroups;
//  - crop mode: the left crop handle crops half the picture away (one
//    step), drawn so; Enter and a change of selection end it;
//  - the slide sorter: a click, Ctrl+click and Shift+click choose slides, a
//    drag moves them together (one step), Ctrl+D duplicates and Delete
//    deletes them, a right-click asks for the menu, a double-click edits.
//
// Usage: SlideEditElementTest
// Exit code is the number of failed checks.

#include "UltraCanvasPptxSlideRender.h"
#include "UltraCanvasSlideEdit.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("%s: %s\n", ok ? "  ok" : "FAIL", what.c_str());
}

constexpr int64_t kInch = 914400;

UCEvent Mouse(UCEventType type, double x, double y, bool shift = false, UCMouseButton button = UCMouseButton::Left) {
    UCEvent e;
    e.type = type;
    e.pointer = Point2Di(static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)));
    e.pointerWindow = e.pointer;
    e.button = button;
    e.shift = shift;
    return e;
}

UCEvent Key(UCKeys key, bool ctrl = false, bool shift = false) {
    UCEvent e;
    e.type = UCEventType::KeyDown;
    e.virtualKey = key;
    e.ctrl = ctrl;
    e.shift = shift;
    return e;
}

void Drag(UltraCanvasSlideEdit& edit, Point2Dd from, Point2Dd to, bool shift = false) {
    edit.OnEvent(Mouse(UCEventType::MouseDown, from.x, from.y, shift));
    for (int step = 1; step <= 4; ++step) {
        const double t = step / 4.0;
        edit.OnEvent(Mouse(UCEventType::MouseMove, from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t, shift));
    }
    edit.OnEvent(Mouse(UCEventType::MouseUp, to.x, to.y, shift));
}


// A 40 x 30 pixel PNG: its left half red-violet, its right half blue (the top
// half; darker below).
std::vector<uint8_t> TestPng() {
    static const uint8_t kBytes[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x1e, 0x08, 0x02, 0x00, 0x00, 0x00, 0xd1, 0xbf, 0xcb,
    0x8a, 0x00, 0x00, 0x00, 0x3a, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0xed, 0xd0, 0x31, 0x11, 0x00,
    0x20, 0x0c, 0xc0, 0xc0, 0x94, 0x43, 0x56, 0x75, 0x30, 0x23, 0xb7, 0xb2, 0xaa, 0x80, 0xa5, 0x0b,
    0x4b, 0x62, 0xe0, 0xef, 0x12, 0x95, 0xc5, 0xb4, 0xc3, 0xbc, 0xc5, 0xa7, 0x84, 0x85, 0x85, 0x85,
    0x85, 0x85, 0x9f, 0x6d, 0xb8, 0xae, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x06, 0xa0, 0x01, 0x95,
    0x44, 0x02, 0xc0, 0x73, 0xc3, 0x46, 0xd0, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae,
    0x42, 0x60, 0x82,
    };
    return std::vector<uint8_t>(std::begin(kBytes), std::end(kBytes));
}

// Whether the pixel at `x`, `y` is near the colour.
bool Colour(const UCPixmap& pixmap, int x, int y, int r, int g, int b, int tolerance = 45) {
    if (x < 0 || y < 0 || x >= pixmap.GetRawWidth() || y >= pixmap.GetRawHeight()) return false;
    const uint32_t argb = pixmap.GetPixel(x, y);
    return std::abs(static_cast<int>((argb >> 16) & 0xFF) - r) <= tolerance &&
           std::abs(static_cast<int>((argb >> 8) & 0xFF) - g) <= tolerance && std::abs(static_cast<int>(argb & 0xFF) - b) <= tolerance;
}

} // namespace

int main() {
    std::printf("=== Slide editor element, by events ===\n");
    auto deck = std::make_shared<PptxPresentation>();
    deck->slideWidth = 12192000;
    deck->slideHeight = 6858000;
    auto edit = CreateSlideEdit("deck", 0, 0, 1400, 800);
    edit->SetDeck(deck);
    UCSlideDeckEditor& editor = edit->Editor();
    editor.NewSlide(-1);
    editor.NewSlide(-1);
    editor.SetCurrentSlide(0);
    const int box = editor.InsertTextBox(SlideBox{kInch, 5 * kInch, 2 * kInch, kInch}, "Box");
    editor.ClearSelection();
    edit->SetFocus(true);

    // Where a slide point is in the element.
    auto view = [&](double x, double y) {
        const Rect2Dd r = edit->SlideRect();
        return Point2Dd(r.x + x / edit->EmuPerPixel(), r.y + y / edit->EmuPerPixel());
    };
    const auto& shape = [&]() -> const PptxShape& { return editor.CurrentSlide()->shapes[static_cast<size_t>(box)]; };

    std::printf("\n-- click, drag --\n");
    const Point2Dd inside = view(2 * kInch, 5.5 * kInch);
    edit->OnEvent(Mouse(UCEventType::MouseDown, inside.x, inside.y));
    edit->OnEvent(Mouse(UCEventType::MouseUp, inside.x, inside.y));
    Check(editor.GetSelection() == std::vector<int>{box}, "a click selects the shape under it");
    const Point2Dd nowhere = view(9 * kInch, 0.2 * kInch);
    edit->OnEvent(Mouse(UCEventType::MouseDown, nowhere.x, nowhere.y));
    edit->OnEvent(Mouse(UCEventType::MouseUp, nowhere.x, nowhere.y));
    Check(editor.GetSelection().empty(), "a click on nothing clears it");
    // Its centre (2 in) dragged to within a few pixels of the slide's centre (6.67 in): it snaps there.
    const double centre = static_cast<double>(deck->slideWidth) / 2;
    Drag(*edit, inside, view(centre + 3 * edit->EmuPerPixel(), 4.5 * kInch));
    Check(shape().x + shape().width / 2 == static_cast<int64_t>(centre), "dragged, its centre snapped to the slide's");
    Check(editor.UndoDescription() == "Move", "one undo step");
    editor.Undo();
    Check(shape().x == kInch && shape().y == 5 * kInch, "undone");

    std::printf("\n-- handles --\n");
    editor.Select({box});
    Drag(*edit, view(3 * kInch, 6 * kInch), view(4 * kInch, 6.2 * kInch), true);   // bottom-right, Shift
    Check(shape().width == 3 * kInch && std::llabs(shape().height - static_cast<int64_t>(1.5 * kInch)) < 20000 &&
              shape().x == kInch,
          "a corner with Shift: the proportions kept [" + std::to_string(shape().width) + " x " + std::to_string(shape().height) + "]");
    editor.Undo();
    // The turning handle, above the middle of the top edge, pulled round to the right.
    const Point2Dd knob = view(2 * kInch, 5 * kInch);
    const Point2Dd knobAt(knob.x, knob.y - UCSlideDeckEditor::kRotateHandleOffset);
    Drag(*edit, knobAt, view(4 * kInch, 5.5 * kInch + 0.1 * kInch), true);
    Check(std::fmod(shape().rotationDeg, 15.0) == 0.0 && shape().rotationDeg > 45 && shape().rotationDeg < 135,
          "turned by the handle, by 15 degrees with Shift [" + std::to_string(shape().rotationDeg) + "]");
    editor.Undo();

    std::printf("\n-- a band --\n");
    Drag(*edit, view(0.5 * kInch, 4.5 * kInch), view(3.5 * kInch, 6.5 * kInch));
    Check(editor.GetSelection() == std::vector<int>{box}, "a band selects what it covers");

    std::printf("\n-- keys --\n");
    edit->OnEvent(Key(UCKeys::Right));
    edit->OnEvent(Key(UCKeys::Right));
    Check(shape().x == kInch + 2 * 91440, "two nudges right");
    edit->OnEvent(Key(UCKeys::Z, true));
    Check(shape().x == kInch, "one Ctrl+Z takes both back");
    edit->OnEvent(Key(UCKeys::Delete));
    Check(editor.CurrentSlide()->shapes.size() == 2, "Delete");
    edit->OnEvent(Key(UCKeys::Z, true));
    edit->OnEvent(Key(UCKeys::M, true));
    Check(editor.GetSlideCount() == 3 && editor.GetCurrentSlide() == 1, "Ctrl+M: a new slide after this one");

    std::printf("\n-- the strip --\n");
    // Slide 3's thumbnail, dragged above slide 1's.
    const double thumbW = 196 - 30 - 12;
    const double thumbH = thumbW * 6858000.0 / 12192000.0;
    auto thumb = [&](int i) { return Point2Dd(30 + thumbW / 2, 12 + i * (thumbH + 14) + thumbH / 2); };
    edit->OnEvent(Mouse(UCEventType::MouseDown, thumb(2).x, thumb(2).y));
    edit->OnEvent(Mouse(UCEventType::MouseUp, thumb(2).x, thumb(2).y));
    Check(editor.GetCurrentSlide() == 2, "a click in the strip chooses the slide");
    deck->slides[2].title = "Last";
    Drag(*edit, thumb(2), Point2Dd(thumb(0).x, thumb(0).y - thumbH / 2 + 2));
    Check(deck->slides[0].title == "Last" && editor.GetCurrentSlide() == 0, "dragged to the top, the slide moved there");

    std::printf("\n-- text --\n");
    editor.SetCurrentSlide(1);
    int asked = -1;
    edit->onEditText = [&asked](int s) { asked = s; };
    const Point2Dd title = view(6 * kInch, 1 * kInch);
    edit->OnEvent(Mouse(UCEventType::MouseDoubleClick, title.x, title.y));
    Check(asked == 0, "a double-click on the title asks for its text");


    std::printf("\n-- groups --\n");
    {
        auto groupDeck = std::make_shared<PptxPresentation>();
        groupDeck->slideWidth = 12192000;
        groupDeck->slideHeight = 6858000;
        auto g = CreateSlideEdit("groups", 0, 0, 1400, 800);
        g->SetDeck(groupDeck);
        UCSlideDeckEditor& e = g->Editor();
        e.InsertSlide(0, PptxSlide());
        e.InsertGeometry(SlideBox{kInch, kInch, kInch, kInch}, "rect");
        e.InsertGeometry(SlideBox{3 * kInch, kInch, kInch, kInch}, "ellipse");
        e.ClearSelection();
        auto at = [&](double x, double y) {
            const Rect2Dd r = g->SlideRect();
            return Point2Dd(r.x + x / g->EmuPerPixel(), r.y + y / g->EmuPerPixel());
        };
        auto part = [&](int i) -> const PptxShape& { return e.CurrentSlide()->shapes[static_cast<size_t>(i)]; };
        Drag(*g, at(0.5 * kInch, 0.5 * kInch), at(4.5 * kInch, 2.5 * kInch));
        g->OnEvent(Key(UCKeys::G, true));
        Check(part(0).groupPath.size() == 1 && part(0).groupPath == part(1).groupPath && e.SelectionIsOneGroup(),
              "a band, then Ctrl+G: one group, selected");
        const Point2Dd away = at(8 * kInch, 5 * kInch);
        g->OnEvent(Mouse(UCEventType::MouseDown, away.x, away.y));
        g->OnEvent(Mouse(UCEventType::MouseUp, away.x, away.y));
        const Point2Dd onRect = at(1.5 * kInch, 1.5 * kInch);
        g->OnEvent(Mouse(UCEventType::MouseDown, onRect.x, onRect.y));
        g->OnEvent(Mouse(UCEventType::MouseUp, onRect.x, onRect.y));
        Check(e.GetSelection() == std::vector<int>({0, 1}), "a click on one of its shapes selects the group");
        Drag(*g, onRect, at(2.5 * kInch, 1.5 * kInch));
        // (The pointer moves by whole pixels: an inch give or take one.)
        Check(std::llabs(part(0).x - 2 * kInch) < 20000 && part(1).x - part(0).x == 2 * kInch && part(1).y == part(0).y &&
                  e.UndoDescription() == "Move",
              "dragged: the group moves as one, one step [" + std::to_string(part(0).x) + ", " + std::to_string(part(1).x) +
                  ", " + std::to_string(part(1).y) + ", " + e.UndoDescription() + "]");
        e.Undo();
        // Its corner handle (4 in, 2 in) to (7 in, 3 in): twice as wide and as tall.
        Drag(*g, at(4 * kInch, 2 * kInch), at(7 * kInch, 3 * kInch));
        Check(std::llabs(part(0).width - 2 * kInch) < 20000 && std::llabs(part(1).x - 5 * kInch) < 20000 &&
                  std::llabs(part(1).height - 2 * kInch) < 20000,
              "its corner handle resizes it, its shapes scaled [" + std::to_string(part(1).x) + "]");
        e.Undo();
        // Its turning handle: above the middle of its top edge, pulled round to the right.
        const Point2Dd top = at(2.5 * kInch, kInch);
        Drag(*g, Point2Dd(top.x, top.y - UCSlideDeckEditor::kRotateHandleOffset), at(4.5 * kInch, 1.5 * kInch), true);
        Check(std::fabs(part(0).rotationDeg - 90) < 1e-6 && std::fabs(part(1).rotationDeg - 90) < 1e-6 &&
                  std::llabs(part(0).y + part(0).height / 2 - kInch / 2) < 20000 && e.UndoDescription() == "Rotate",
              "its turning handle turns the whole group about its centre [" + std::to_string(part(0).rotationDeg) + "]");
        e.Undo();
        g->OnEvent(Mouse(UCEventType::MouseDoubleClick, onRect.x, onRect.y));
        Check(e.GetSelection() == std::vector<int>({0}) && !e.GetEnteredGroup().empty(),
              "a double-click selects inside the group");
        g->OnEvent(Key(UCKeys::Escape));
        Check(e.GetSelection() == std::vector<int>({0, 1}) && e.GetEnteredGroup().empty(), "Escape: the group again");
        g->OnEvent(Key(UCKeys::G, true, true));
        Check(part(0).groupPath.empty() && part(1).groupPath.empty() && e.UndoDescription() == "Ungroup", "Ctrl+Shift+G ungroups");
    }

    std::printf("\n-- crop --\n");
    {
        auto cropDeck = std::make_shared<PptxPresentation>();
        cropDeck->slideWidth = 12192000;
        cropDeck->slideHeight = 6858000;
        auto c = CreateSlideEdit("crop", 0, 0, 1400, 800);
        c->SetDeck(cropDeck);
        UCSlideDeckEditor& e = c->Editor();
        e.InsertSlide(0, PptxSlide());
        Check(!c->SetCropMode(true), "no crop mode without a picture");
        const int pic = e.InsertPicture(TestPng(), "png", 40, 30);
        const PptxShape before = e.CurrentSlide()->shapes[static_cast<size_t>(pic)];
        // At 1000 pixels across: a point a quarter into the picture (its red half).
        const double scale = 1000.0 / static_cast<double>(cropDeck->slideWidth);
        const int probeX = static_cast<int>((static_cast<double>(before.x) + static_cast<double>(before.width) * 0.25) * scale);
        const int probeY = static_cast<int>((static_cast<double>(before.y) + static_cast<double>(before.height) * 0.25) * scale);
        const auto uncropped = RenderPptxSlide(*cropDeck, 0, 1000);
        Check(uncropped && Colour(*uncropped, probeX, probeY, 200, 60, 200), "the picture drawn whole");
        Check(c->SetCropMode(true) && c->IsCropping(), "crop mode for the selected picture");
        auto at = [&](double x, double y) {
            const Rect2Dd r = c->SlideRect();
            return Point2Dd(r.x + x / c->EmuPerPixel(), r.y + y / c->EmuPerPixel());
        };
        const double middleY = static_cast<double>(before.y) + static_cast<double>(before.height) / 2;
        // The left crop handle dragged to the middle: half the picture cropped away.
        Drag(*c, at(static_cast<double>(before.x), middleY),
             at(static_cast<double>(before.x) + static_cast<double>(before.width) / 2, middleY));
        const PptxShape& after = e.CurrentSlide()->shapes[static_cast<size_t>(pic)];
        Check(std::fabs(after.cropLeft - 0.5) < 0.02 && after.cropRight == 0 && std::llabs(after.x + after.width - before.x - before.width) < 2 &&
                  std::llabs(after.width - before.width / 2) < 20000 && e.UndoDescription() == "Crop",
              "the left crop handle crops the left half, the right edge staying [" + std::to_string(after.cropLeft) + "]");
        Check(c->IsCropping(), "still cropping");
        const auto cropped = RenderPptxSlide(*cropDeck, 0, 1000);
        Check(cropped && Colour(*cropped, probeX, probeY, 255, 255, 255) &&
                  Colour(*cropped, probeX + static_cast<int>(before.width * 0.5 * scale), probeY, 30, 60, 200),
              "drawn cropped: the left half gone, the right half where it was");
        c->OnEvent(Key(UCKeys::Return));
        Check(!c->IsCropping(), "Enter ends crop mode");
        // Crop mode: the drag of a corner handle on a picture resizes again.
        c->SetCropMode(true);
        e.ClearSelection();
        Check(!c->IsCropping(), "a change of selection ends crop mode");
    }

    std::printf("\n-- the slide sorter --\n");
    {
        auto sortDeck = std::make_shared<PptxPresentation>();
        sortDeck->slideWidth = 12192000;
        sortDeck->slideHeight = 6858000;
        auto s = CreateSlideEdit("sorter", 0, 0, 1400, 800);
        s->SetDeck(sortDeck);
        UCSlideDeckEditor& e = s->Editor();
        for (const char* title : {"A", "B", "C", "D"}) {
            PptxSlide slide;
            slide.title = title;
            e.InsertSlide(-1, slide);
        }
        auto titles = [&sortDeck]() {
            std::string t;
            for (const PptxSlide& slide : sortDeck->slides) t += slide.title;
            return t;
        };
        int modes = 0;
        s->onModeChange = [&modes]() { ++modes; };
        s->SetView(SlideEditView::Sorter);
        Check(s->GetView() == SlideEditView::Sorter && modes == 1, "the sorter view");
        // Five thumbnails of 220 pixels a row, 28 apart, centred in 1400.
        const double thumbH = 220.0 * 6858000.0 / 12192000.0;
        auto thumb = [&](int i) { return Point2Dd(94 + i * 248 + 110, 24 + thumbH / 2); };
        s->OnEvent(Mouse(UCEventType::MouseDown, thumb(0).x, thumb(0).y));
        s->OnEvent(Mouse(UCEventType::MouseUp, thumb(0).x, thumb(0).y));
        UCEvent ctrlClick = Mouse(UCEventType::MouseDown, thumb(2).x, thumb(2).y);
        ctrlClick.ctrl = true;
        s->OnEvent(ctrlClick);
        ctrlClick.type = UCEventType::MouseUp;
        s->OnEvent(ctrlClick);
        Check(s->GetSorterSelection() == std::vector<int>({0, 2}), "a click and a Ctrl+click choose two slides");
        // The two dragged past the last one.
        Drag(*s, thumb(2), Point2Dd(thumb(3).x + 100, thumb(3).y));
        Check(titles() == "BDAC" && s->GetSorterSelection() == std::vector<int>({2, 3}) && e.UndoDescription() == "Move slides",
              "dragged: both moved to the end together, one step, still chosen");
        e.Undo();
        Check(titles() == "ABCD", "undone");
        UCEvent shiftClick = Mouse(UCEventType::MouseDown, thumb(3).x, thumb(3).y, true);
        s->OnEvent(Mouse(UCEventType::MouseDown, thumb(1).x, thumb(1).y));
        s->OnEvent(Mouse(UCEventType::MouseUp, thumb(1).x, thumb(1).y));
        s->OnEvent(shiftClick);
        shiftClick.type = UCEventType::MouseUp;
        s->OnEvent(shiftClick);
        Check(s->GetSorterSelection() == std::vector<int>({1, 2, 3}), "Shift+click: the range");
        s->OnEvent(Key(UCKeys::D, true));
        Check(titles() == "ABCDBCD" && e.UndoDescription() == "Duplicate slides", "Ctrl+D duplicates them, one step");
        e.Undo();
        bool menu = false;
        int menuSlide = -1;
        s->onContextMenu = [&](bool inStrip, int slide, int, int, int) { menu = inStrip; menuSlide = slide; };
        s->OnEvent(Mouse(UCEventType::MouseDown, thumb(0).x, thumb(0).y, false, UCMouseButton::Right));
        Check(menu && menuSlide == 0 && s->GetSorterSelection() == std::vector<int>({0}),
              "a right-click on a slide not chosen chooses it and asks for the menu");
        s->OnEvent(Key(UCKeys::Delete));
        Check(titles() == "BCD", "Delete deletes the slides chosen");
        e.Undo();
        s->OnEvent(Mouse(UCEventType::MouseDoubleClick, thumb(3).x, thumb(3).y));
        Check(s->GetView() == SlideEditView::Normal && e.GetCurrentSlide() == 3, "a double-click edits that slide");
    }

    std::printf("\n-- a chart --\n");
    {
        int chartAsked = -1;
        editor.SetCurrentSlide(1);
        RichChart chart;
        chart.kind = RichChart::Kind::Column;
        chart.categories = {"a", "b"};
        RichChartSeries series;
        series.name = "s";
        series.values = {1, 2};
        chart.series = {series};
        const int shape = editor.InsertChart(SlideBox{7 * kInch, 4 * kInch, 3 * kInch, 2 * kInch}, chart);
        edit->onEditChart = [&chartAsked](int s) { chartAsked = s; };
        const Point2Dd onChart = view(8.5 * kInch, 5 * kInch);
        edit->OnEvent(Mouse(UCEventType::MouseDoubleClick, onChart.x, onChart.y));
        Check(chartAsked == shape, "a double-click on a chart asks for its data");
    }

    std::printf("\n%s (%d failed)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
