// Tests/SlideDeckEditorTest.cpp
// The slide editor's core (UltraCanvasSlideDeckEditor.h) without a window:
// every command applied, undone and redone - slides inserted, duplicated,
// moved, hidden and deleted; shapes moved, nudged (one step for a run),
// resized, turned, reordered, duplicated, deleted, pasted, their text set;
// the selection put back by undo - and hit testing (turned shapes too),
// handles and snapping to the slide and to other shapes.
//
// Usage: SlideDeckEditorTest
// Exit code is the number of failed checks.

#include "UltraCanvasSlideDeckEditor.h"

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

const PptxShape& Shape(const UCSlideDeckEditor& editor, int i) {
    return editor.CurrentSlide()->shapes[static_cast<size_t>(i)];
}

} // namespace

int main() {
    std::printf("=== Slide editor core ===\n");
    auto deck = std::make_shared<PptxPresentation>();
    deck->slideWidth = 12192000;   // 13.33 x 7.5 in
    deck->slideHeight = 6858000;
    UCSlideDeckEditor editor(deck);
    int changes = 0;
    editor.onChange = [&changes]() { ++changes; };
    Check(editor.GetSlideCount() == 0 && editor.GetCurrentSlide() == -1 && !editor.CanUndo(), "an empty deck");

    std::printf("\n-- slides --\n");
    editor.NewSlide(-1);
    Check(editor.GetSlideCount() == 1 && editor.GetCurrentSlide() == 0 && editor.CurrentSlide()->shapes.size() == 2,
          "New slide: Title and Content");
    editor.SetShapeText(0, "First");
    Check(editor.CurrentSlide()->title == "First", "the title placeholder's text is the slide's title");
    editor.NewSlide(-1);
    editor.SetShapeText(0, "Second");
    editor.DuplicateSlide(1);
    Check(editor.GetSlideCount() == 3 && editor.GetCurrentSlide() == 2 && editor.CurrentSlide()->title == "Second",
          "Duplicate slide: after it, made current");
    editor.MoveSlide(2, 0);
    Check(deck->slides[0].title == "Second" && deck->slides[0].number == 1 && editor.GetCurrentSlide() == 0,
          "Move slide 3 to the front, renumbered");
    editor.Undo();
    Check(deck->slides[0].title == "First" && deck->slides[2].title == "Second", "undone");
    editor.SetSlideHidden(1, true);
    Check(deck->slides[1].hidden, "Hide slide");
    editor.DeleteSlide(2);
    Check(editor.GetSlideCount() == 2 && editor.GetCurrentSlide() == 1, "Delete slide: the one before is current");
    editor.Undo();
    Check(editor.GetSlideCount() == 3 && deck->slides[2].title == "Second", "undone: the slide is back");
    editor.SetSlideNotes(0, "Say hello\nThen go on");
    Check(deck->slides[0].notes.size() == 2 && UCSlideDeckEditor::SlideNotesText(deck->slides[0]) == "Say hello\nThen go on",
          "notes, a paragraph a line");

    std::printf("\n-- shapes --\n");
    editor.SetCurrentSlide(0);
    const int box = editor.InsertTextBox(SlideBox{kInch, kInch, 2 * kInch, kInch}, "Hello");
    Check(box == 2 && editor.GetSelection() == std::vector<int>{2} && UCSlideDeckEditor::ShapeText(Shape(editor, 2)) == "Hello",
          "a text box, selected");
    editor.MoveSelection(kInch, 0);
    Check(Shape(editor, 2).x == 2 * kInch, "moved an inch right");
    const std::string before = editor.UndoDescription();
    editor.NudgeSelection(12700, 0);
    editor.NudgeSelection(12700, 0);
    editor.NudgeSelection(0, 12700);
    Check(Shape(editor, 2).x == 2 * kInch + 25400 && Shape(editor, 2).y == kInch + 12700, "three nudges");
    editor.Undo();
    Check(Shape(editor, 2).x == 2 * kInch && Shape(editor, 2).y == kInch, "one undo takes back all three");
    editor.SetShapeBox(2, SlideBox{0, 0, 3 * kInch, 2 * kInch});
    Check(Shape(editor, 2).width == 3 * kInch && Shape(editor, 2).height == 2 * kInch, "resized");
    editor.SetShapeRotation(2, -30);
    Check(Shape(editor, 2).rotationDeg == 330.0, "turned, kept within 0..360");

    std::printf("\n-- finding --\n");
    editor.SetShapeRotation(2, 0);
    Check(editor.ShapeAt(kInch, kInch) == 2, "the topmost shape at a point");
    editor.SetShapeRotation(2, 90);   // 3 x 2 in turned: now 2 wide, 3 tall about its centre (1.5, 1)
    Check(editor.ShapeAt(static_cast<int64_t>(1.5 * kInch), static_cast<int64_t>(2.3 * kInch)) == 2 &&
              editor.ShapeAt(static_cast<int64_t>(2.8 * kInch), static_cast<int64_t>(1.0 * kInch)) != 2,
          "a turned shape is found in its own frame");
    editor.SetShapeRotation(2, 0);
    editor.Select({2});
    const double emuPerPixel = 12700;   // a point a pixel
    Check(editor.HandleAt(3 * kInch, 2 * kInch, emuPerPixel) == SlideHandle::BottomRight, "the bottom-right handle");
    Check(editor.HandleAt(static_cast<int64_t>(1.5 * kInch), -24 * 12700, emuPerPixel) == SlideHandle::Rotate,
          "the turning handle above the frame");
    Check(editor.HandleAt(static_cast<int64_t>(1.5 * kInch), kInch, emuPerPixel) == SlideHandle::NoHandle, "none inside");

    std::printf("\n-- snapping --\n");
    SlideSnap snap = editor.Snap(SlideBox{40000, 3 * kInch, kInch, kInch}, 50000, {2}, true);
    Check(snap.box.x == 0 && snap.verticalGuides == std::vector<int64_t>{0}, "the left edge to the slide's");
    const int64_t centre = editor.SlideWidth() / 2;
    snap = editor.Snap(SlideBox{centre - kInch / 2 + 30000, 4 * kInch, kInch, kInch}, 50000, {2}, true);
    Check(snap.box.x == centre - kInch / 2, "the centre to the slide's centre");
    // The placeholders left out, so the text box (0, 0, 3 x 2 in) is the only shape to line up with.
    snap = editor.Snap(SlideBox{5 * kInch, 2 * kInch + 20000, kInch, kInch}, 50000, {0, 1}, true);
    Check(snap.box.y == 2 * kInch && snap.horizontalGuides == std::vector<int64_t>{2 * kInch},
          "the top edge to another shape's bottom");
    snap = editor.Snap(SlideBox{kInch, 5 * kInch, 2 * kInch - 30000, kInch}, 50000, {0, 1}, false, SlideHandle::Right);
    Check(snap.box.width == 2 * kInch && snap.box.x == kInch, "a resized right edge to the shape's, the left one stays");
    snap = editor.Snap(SlideBox{5 * kInch, 2 * kInch + 20000, kInch, kInch}, 50000, {}, true);
    Check(snap.box.y == editor.CurrentSlide()->shapes[1].y, "the nearest edge wins: the content placeholder's top");

    std::printf("\n-- order, duplicate, delete, paste --\n");
    editor.Select({0});
    editor.BringToFront();
    Check(Shape(editor, 2).placeholder == "title" && editor.GetSelection() == std::vector<int>{2}, "Bring to front");
    editor.SendBackward();
    Check(Shape(editor, 1).placeholder == "title" && editor.GetSelection() == std::vector<int>{1}, "Send backward");
    editor.Undo();
    editor.Undo();
    Check(Shape(editor, 0).placeholder == "title" && editor.GetSelection() == std::vector<int>{0}, "both undone, the selection back");
    editor.Select({2});
    editor.DuplicateSelection();
    Check(editor.CurrentSlide()->shapes.size() == 4 && editor.GetSelection() == std::vector<int>{3} &&
              Shape(editor, 3).x == Shape(editor, 2).x + 152400,
          "Duplicate: offset, the copy selected");
    editor.DeleteSelection();
    Check(editor.CurrentSlide()->shapes.size() == 3 && editor.GetSelection().empty(), "Delete");
    editor.Undo();
    Check(editor.CurrentSlide()->shapes.size() == 4 && editor.GetSelection() == std::vector<int>{3},
          "undone: the shape back, and selected");
    editor.CopySelection();
    editor.SetCurrentSlide(1);
    editor.Paste();
    Check(editor.CurrentSlide()->shapes.size() == 3 && Shape(editor, 2).x == deck->slides[0].shapes[3].x,
          "pasted on another slide in the same place");
    editor.Paste();
    Check(Shape(editor, 3).x == Shape(editor, 2).x + 152400, "pasted again on it: offset");

    std::printf("\n-- a picture --\n");
    const std::vector<uint8_t> png = {0x89, 'P', 'N', 'G'};
    const int picture = editor.InsertPicture(png, "png", 800, 600, "A photo");
    Check(picture >= 0 && Shape(editor, picture).kind == PptxShapeKind::Picture &&
              deck->pictures.count(Shape(editor, picture).pictureTarget) == 1,
          "its bytes kept with the deck");
    Check(Shape(editor, picture).width * 3 == Shape(editor, picture).height * 4, "its proportions kept");

    std::printf("\n-- modified, redo --\n");
    Check(editor.IsModified() && changes > 10, "the deck is modified, and every change was reported");
    editor.Undo();
    Check(editor.CanRedo(), "undo makes redo possible");
    editor.Redo();
    Check(editor.CurrentSlide()->shapes.size() == 5, "redo does it again");

    std::printf("\n%s (%d failed)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
