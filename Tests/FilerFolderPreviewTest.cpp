// Tests/FilerFolderPreviewTest.cpp
// Display > Folder previews: the geometry of the cards that peek out of a
// folder icon (UltraCanvasFilerWidget::FolderPreviewCardRects).
//
// The rule this guards: the cards are the folder's pictures, and they must
// read as pictures standing IN the folder. So there are at most two, every
// card lies inside the icon box (a card leaving the folder is a smudge over
// the caption), the top of every card is above the front flap so part of it
// shows, and with two cards the front one (drawn last) sits to the right of
// and lower than the one behind it, so both show. The draw and the prefetch
// both ask the thumbnail cache at these sizes, so the geometry must also be
// a pure function of the box: the same box gives the same cards.
// Version: 1.0.0
// Last Modified: 2026-09-13
// Author: UltraCanvas Framework

#include "UltraCanvasFilerWidget.h"

#include <iostream>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

bool Inside(const Rect2Di& inner, const Rect2Di& outer) {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}

std::string Describe(const Rect2Di& r) {
    return "(" + std::to_string(r.x) + "," + std::to_string(r.y) + " " +
           std::to_string(r.width) + "x" + std::to_string(r.height) + ")";
}

void CheckBox(const Rect2Di& box) {
    const std::string tag = "box " + Describe(box) + ": ";

    Check(UltraCanvasFilerWidget::FolderPreviewCardRects(box, 0).empty(),
          tag + "no pictures, no cards");

    const auto one = UltraCanvasFilerWidget::FolderPreviewCardRects(box, 1);
    Check(one.size() == 1, tag + "one picture, one card");

    const auto two = UltraCanvasFilerWidget::FolderPreviewCardRects(box, 2);
    Check(two.size() == 2, tag + "two pictures, two cards");

    const auto many = UltraCanvasFilerWidget::FolderPreviewCardRects(box, 7);
    Check(many.size() == 2, tag + "seven pictures, still two cards");
    Check(many == two, tag + "the two cards do not depend on how many "
                             "more pictures the folder holds");

    // The middle of the box is where the front flap starts (a little below
    // it, in fact); a card's top must be above it or nothing of it shows.
    const int flapTop = box.y + box.height / 2;
    for (const auto* cards : {&one, &two}) {
        for (size_t i = 0; i < cards->size(); ++i) {
            const Rect2Di& c = (*cards)[i];
            const std::string which =
                    tag + "card " + std::to_string(i) + " " + Describe(c);
            Check(c.width >= 2 && c.height >= 2, which + " has a size");
            Check(Inside(c, box), which + " lies inside the box");
            Check(c.y < flapTop, which + " shows above the front flap");
        }
    }

    // Front card first: right of and lower than the one behind it, and the
    // two overlap (they stand in the same folder, not side by side).
    const Rect2Di& front = two[0];
    const Rect2Di& back = two[1];
    Check(front.x > back.x, tag + "the front card sits right of the back one");
    Check(front.y > back.y, tag + "the front card sits lower than the back one");
    Check(front.x < back.x + back.width && back.x < front.x + front.width,
          tag + "the two cards overlap");
    Check(front.width == back.width && front.height == back.height,
          tag + "both cards have the same size");

    // Same box, same cards: the prefetch and the draw must agree on the
    // size they ask the thumbnail cache for.
    Check(UltraCanvasFilerWidget::FolderPreviewCardRects(box, 2) == two,
          tag + "the geometry is a pure function of the box");
}

} // namespace

int main() {
    std::cout << "FilerFolderPreviewTest\n";

    // The smallest box that carries previews, the app's shrunk folder glyph
    // in a small tile, the four thumbnail edges, and a box that is not at
    // the origin.
    CheckBox(Rect2Di(0, 0, 32, 32));
    CheckBox(Rect2Di(0, 0, 42, 42));
    CheckBox(Rect2Di(0, 0, 60, 60));
    CheckBox(Rect2Di(0, 0, 92, 92));
    CheckBox(Rect2Di(0, 0, 142, 142));
    CheckBox(Rect2Di(0, 0, 216, 216));
    CheckBox(Rect2Di(310, 1240, 119, 119));
    // A box that is wider than tall (the treemap cells are), and one taller
    // than wide.
    CheckBox(Rect2Di(10, 10, 120, 64));
    CheckBox(Rect2Di(10, 10, 64, 120));

    if (g_failures == 0) {
        std::cout << "All checks passed.\n";
        return 0;
    }
    std::cout << g_failures << " check(s) FAILED.\n";
    return 1;
}
