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
//  - double-click asks for a shape's text (onEditText).
//
// Usage: SlideEditElementTest
// Exit code is the number of failed checks.

#include "UltraCanvasSlideEdit.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

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

    std::printf("\n%s (%d failed)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
