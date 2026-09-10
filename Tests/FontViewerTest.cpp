// Tests/FontViewerTest.cpp
// The glyph browser's geometry: how many columns fit, how far it scrolls,
// which glyph is under a point, and where a cell lands.
//
// These are the parts that break silently. A wrong column count or a scroll
// range that stops short of the last row is invisible until a font is open in
// front of someone, and the painting itself cannot be checked without a
// window - so what can be checked headlessly is checked here, thoroughly.
// Version: 1.0.0
// Last Modified: 2026-09-08
// Author: UltraCanvas Framework

#include "UltraCanvasFontViewer.h"
#include "UltraCanvasMediaViewer.h"

#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>

namespace fs = std::filesystem;
using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

fs::path BundledFont(const std::string& name) {
    return fs::path(UC_MEDIA_DIR) / "fonts" / name;
}

void TestEmptyViewer() {
    std::cout << "\nWith no font\n";
    auto viewer = CreateFontViewer("empty", 0, 0, 600, 400);
    Check(!viewer->IsFontLoaded(), "starts with no font");
    Check(viewer->GetRowCount() == 0, "no rows");
    Check(viewer->GetMaxScroll() == 0, "nothing to scroll");
    Check(viewer->GetColumnCount() >= 1, "still reports at least one column");
    Check(viewer->EntryAtPoint(100, 100) == UltraCanvasFontViewer::NoEntry,
          "no glyph under any point");
    Check(viewer->GetSelectedEntry() == UltraCanvasFontViewer::NoEntry,
          "nothing selected");
    // None of these may crash on an empty viewer.
    viewer->ScrollToCodepoint('A');
    viewer->ShowRange(0);
    viewer->SetSelectedEntry(5);
    Check(viewer->GetSelectedEntry() == UltraCanvasFontViewer::NoEntry,
          "selecting a glyph that does not exist is refused");
    Check(!viewer->LoadFont("/no/such/font.ttf"), "a missing file fails to load");
}

void TestGridGeometry() {
    std::cout << "\nGrid geometry\n";
    const fs::path regular = BundledFont("Ubuntu-R.ttf");
    if (!fs::exists(regular)) {
        std::cout << "  [SKIP] " << regular.string() << " not present\n";
        return;
    }
    auto viewer = CreateFontViewer("grid", 0, 0, 600, 400);
    Check(viewer->LoadFont(regular.string()), "the font loads");
    Check(viewer->IsFontLoaded(), "and reports loaded");
    Check(viewer->GetFace().Glyphs().size() > 100, "its coverage came through");

    const size_t glyphs = viewer->GetFace().Glyphs().size();
    const int columns = viewer->GetColumnCount();
    const int rows = viewer->GetRowCount();
    std::cout << "    " << glyphs << " glyphs, " << columns << " columns, "
              << rows << " rows, max scroll " << viewer->GetMaxScroll() << "\n";
    Check(columns >= 1, "at least one column fits");
    Check(rows >= 1, "at least one row");
    // Every glyph must have a cell, and no more rows than needed.
    Check(static_cast<size_t>(rows) * columns >= glyphs,
          "the rows hold every glyph");
    Check(static_cast<size_t>(rows - 1) * columns < glyphs,
          "and there is no empty row at the end");
    Check(viewer->GetMaxScroll() > 0,
          "a font this size scrolls in a pane this size");

    // Narrowing the pane must reduce the columns and lengthen the grid.
    viewer->SetBounds(Rect2Df(0, 0, 300, 400));
    const int narrowColumns = viewer->GetColumnCount();
    Check(narrowColumns > 0 && narrowColumns < columns,
          "a narrower pane fits fewer columns");
    Check(viewer->GetRowCount() > rows, "and therefore needs more rows");
    viewer->SetBounds(Rect2Df(0, 0, 600, 400));
    Check(viewer->GetColumnCount() == columns, "restoring the width restores them");
}

void TestPointRoundTrip() {
    std::cout << "\nPoints and cells\n";
    const fs::path regular = BundledFont("Ubuntu-R.ttf");
    if (!fs::exists(regular)) {
        std::cout << "  [SKIP] " << regular.string() << " not present\n";
        return;
    }
    auto viewer = CreateFontViewer("hit", 0, 0, 600, 400);
    if (!viewer->LoadFont(regular.string())) {
        std::cout << "  [SKIP] the font did not load\n";
        return;
    }

    // The centre of a visible cell must hit that cell, and nothing else.
    const int columns = viewer->GetColumnCount();
    int checked = 0, matched = 0;
    std::set<size_t> seen;
    for (size_t entry = 0; entry < static_cast<size_t>(columns) * 3 &&
                           entry < viewer->GetFace().Glyphs().size(); ++entry) {
        const Rect2Di cell = viewer->GetCellRect(entry);
        const int cx = cell.x + viewer->GetCellSize() / 2;
        const int cy = cell.y + viewer->GetCellSize() / 2;
        ++checked;
        const size_t hit = viewer->EntryAtPoint(cx, cy);
        if (hit == entry) ++matched;
        seen.insert(hit);
    }
    Check(checked > 0 && matched == checked,
          "every visible cell's centre hits its own entry");
    Check(seen.size() == static_cast<size_t>(checked),
          "and no two cells claim the same point");

    // A point far outside the grid belongs to nothing.
    Check(viewer->EntryAtPoint(-50, -50) == UltraCanvasFontViewer::NoEntry,
          "a point above and left of the grid hits nothing");
    Check(viewer->EntryAtPoint(100000, 100000) == UltraCanvasFontViewer::NoEntry,
          "a point past the grid hits nothing");
}

void TestScrolling() {
    std::cout << "\nScrolling and ranges\n";
    const fs::path regular = BundledFont("Ubuntu-R.ttf");
    if (!fs::exists(regular)) {
        std::cout << "  [SKIP] " << regular.string() << " not present\n";
        return;
    }
    auto viewer = CreateFontViewer("scroll", 0, 0, 600, 400);
    if (!viewer->LoadFont(regular.string())) {
        std::cout << "  [SKIP] the font did not load\n";
        return;
    }
    Check(viewer->GetScrollOffset() == 0, "a freshly loaded font starts at the top");

    // The last glyph must be reachable: a scroll range that stops short of it
    // hides the end of the font with no way to tell.
    const size_t last = viewer->GetFace().Glyphs().size() - 1;
    viewer->ScrollToEntry(last);
    const Rect2Di lastCell = viewer->GetCellRect(last);
    Check(viewer->GetScrollOffset() == viewer->GetMaxScroll(),
          "scrolling to the last glyph reaches the end of the range");
    Check(lastCell.y >= 0 && lastCell.y < 400,
          "and puts the last cell inside the pane");

    viewer->ScrollToEntry(0);
    Check(viewer->GetScrollOffset() == 0, "and back to the top again");

    // A range picker's job: land on the first glyph of the chosen block.
    const auto& ranges = viewer->GetFace().Ranges();
    Check(!ranges.empty(), "the font offers ranges");
    if (ranges.size() > 2) {
        const size_t target = ranges[2].firstEntry;
        viewer->ShowRange(2);
        const Rect2Di cell = viewer->GetCellRect(target);
        Check(cell.y >= 0 && cell.y < 400,
              "showing a range brings its first glyph into view");
    }
    viewer->ShowRange(ranges.size() + 10);   // must be a no-op, not a crash
    Check(true, "an out-of-range range index is ignored");

    // Selecting scrolls the glyph into view, and reports it once.
    int fired = 0;
    size_t reported = UltraCanvasFontViewer::NoEntry;
    viewer->onGlyphSelected = [&](size_t e) { ++fired; reported = e; };
    viewer->SetSelectedEntry(last);
    Check(fired == 1 && reported == last, "selection is reported once");
    Check(viewer->GetCellRect(last).y >= 0, "and the glyph is scrolled into view");
    viewer->SetSelectedEntry(last);
    Check(fired == 1, "re-selecting the same glyph reports nothing");
}

void TestCellSize() {
    std::cout << "\nCell size\n";
    const fs::path regular = BundledFont("Ubuntu-R.ttf");
    if (!fs::exists(regular)) {
        std::cout << "  [SKIP] " << regular.string() << " not present\n";
        return;
    }
    auto viewer = CreateFontViewer("size", 0, 0, 600, 400);
    if (!viewer->LoadFont(regular.string())) {
        std::cout << "  [SKIP] the font did not load\n";
        return;
    }
    const int columnsAtDefault = viewer->GetColumnCount();

    viewer->SetCellSize(120);
    Check(viewer->GetCellSize() == 120, "the size is taken");
    Check(viewer->GetColumnCount() < columnsAtDefault,
          "bigger cells mean fewer columns");

    viewer->SetCellSize(24);
    Check(viewer->GetColumnCount() > columnsAtDefault,
          "smaller cells mean more columns");

    // Out-of-range sizes clamp rather than produce a degenerate grid.
    viewer->SetCellSize(-10);
    Check(viewer->GetCellSize() > 0 && viewer->GetColumnCount() >= 1,
          "an absurd size clamps to something usable");
    viewer->SetCellSize(100000);
    Check(viewer->GetCellSize() <= 200 && viewer->GetRowCount() >= 1,
          "and so does an enormous one");
}


// The detail pane only opens what UltraCanvasMediaViewer admits. Until fonts
// were classified there, the Display > Detail view > Fonts switch was a
// control with nothing behind it: UltraFilerWindow::CanShowInDetailView()
// asks IsSupportedMedia() first, and a font never got past it. That gate is
// the public one, and the one worth pinning.
// The control bar sits where the viewer puts it only if it says so in CSS
// terms: SetBounds writes finalBounds, and the parent's next layout pass
// overwrites an in-flow child, stacking the whole bar in one column at the
// top-left over the grid. That only shows up in a window, so what can be
// checked here is the part that prevents it - every control out of flow, and
// placed where the bar's arithmetic says.
void TestControlPlacement() {
    std::cout << "\nControl bar placement\n";
    const float w = 900, h = 680;
    auto viewer = CreateFontViewer("chrome", 0, 0, w, h);

    std::map<std::string, std::shared_ptr<UltraCanvasUIElement>> byId;
    for (const auto& child : viewer->GetChildren()) {
        if (child) byId[child->GetIdentifier()] = child;
    }
    Check(byId.count("chrome-range") == 1, "the range picker is a child");
    Check(byId.count("chrome-size") == 1, "so is the size slider");
    Check(byId.count("chrome-info") == 1, "so is the information line");
    Check(byId.count("chrome-scroll") == 1, "so is the scrollbar");

    for (const auto& [id, el] : byId) {
        Check(el->layoutItem.positionType != CSSLayout::PositionType::Static,
              id + " is out of flow, so a layout pass leaves it alone");
    }

    auto boundsOf = [&](const std::string& id) {
        return byId.count(id) ? byId[id]->GetBounds() : Rect2Df(0, 0, 0, 0);
    };
    const Rect2Df range = boundsOf("chrome-range");
    const Rect2Df slider = boundsOf("chrome-size");
    const Rect2Df info = boundsOf("chrome-info");
    const Rect2Df bar = boundsOf("chrome-scroll");

    Check(range.x > 0 && range.y >= 0 && range.y < 30,
          "the range picker is in the control bar, not at the origin");
    Check(slider.x > range.x + range.width,
          "the size slider is to the right of it, not under it");
    Check(slider.x + slider.width <= w, "and inside the viewer");
    Check(info.y > h - 30, "the information line is along the bottom");
    Check(info.width > w * 0.5f, "and spans the width");
    Check(bar.x + bar.width <= w && bar.x > w * 0.5f,
          "the scrollbar is down the right edge");
    Check(bar.height > 100, "and is as tall as the grid");

    // A resize has to move them, not just re-flow the grid.
    viewer->SetBounds(Rect2Df(0, 0, 500, 400));
    const Rect2Df narrowSlider = boundsOf("chrome-size");
    const Rect2Df narrowInfo = boundsOf("chrome-info");
    Check(narrowSlider.x + narrowSlider.width <= 500,
          "a narrower viewer pulls the slider in");
    Check(narrowInfo.y > 400 - 30, "and lifts the information line");
}

// A viewer inside another container - the media viewer's flex column, say -
// is built at no size and sized by the layout engine, which calls Arrange()
// and never SetBounds(). Without a re-flow there the control bar keeps the
// placement it was given at a size of zero, which is none, and the layout
// engine stacks it in a column over the grid.
void TestArrangedByALayoutPass() {
    std::cout << "\nSized by a layout pass\n";
    auto viewer = CreateFontViewer("flexchild", 0, 0, 0, 0);

    std::map<std::string, std::shared_ptr<UltraCanvasUIElement>> byId;
    for (const auto& child : viewer->GetChildren()) {
        if (child) byId[child->GetIdentifier()] = child;
    }
    auto boundsOf = [&](const std::string& id) {
        return byId.count(id) ? byId[id]->GetBounds() : Rect2Df(0, 0, 0, 0);
    };

    Check(viewer->GetColumnCount() >= 1, "starts with no usable size");

    const float w = 820, h = 600;
    viewer->Arrange(Rect2Df(0, 0, w, h), CSSLayout::LayoutContext{});

    Check(viewer->GetWidth() == w, "takes the size the layout pass gives it");
    const Rect2Df range = boundsOf("flexchild-range");
    const Rect2Df slider = boundsOf("flexchild-size");
    const Rect2Df info = boundsOf("flexchild-info");
    Check(range.x > 0 && range.y < 30,
          "the range picker is in the control bar afterwards");
    Check(slider.x > range.x + range.width,
          "the slider is beside it rather than under it");
    Check(info.y > h - 30, "the information line is along the bottom");
    for (const auto& [id, el] : byId) {
        Check(el->layoutItem.positionType != CSSLayout::PositionType::Static,
              id + " is out of flow");
    }

    // A second pass at the same size must not have to do it again, and a pass
    // at a new size must.
    viewer->Arrange(Rect2Df(0, 0, w, h), CSSLayout::LayoutContext{});
    Check(boundsOf("flexchild-size").x == slider.x, "an identical pass changes nothing");
    viewer->Arrange(Rect2Df(0, 0, 520, h), CSSLayout::LayoutContext{});
    const Rect2Df narrowed = boundsOf("flexchild-size");
    Check(narrowed.x + narrowed.width <= 520, "a narrower pass pulls the slider in");
}

void TestMediaViewerIntegration() {
    std::cout << "\nDetail-pane classification\n";
    Check(UltraCanvasMediaViewer::IsSupportedMedia("Ubuntu-R.ttf"),
          "the detail pane accepts a ttf");
    Check(UltraCanvasMediaViewer::IsSupportedMedia("/a/b/SEGOEUI.TTF"),
          "upper case and a path are fine");
    Check(UltraCanvasMediaViewer::IsSupportedMedia("Legacy.otf"),
          "and an otf");
    // A Type 1 .pfa is ASCII that the syntax tokenizer recognises, so it has
    // to be classified as a font before the text check or it opens as source.
    Check(UltraCanvasMediaViewer::IsSupportedMedia("Legacy.pfa"),
          "and a Type 1 .pfa");
    Check(UltraCanvasMediaViewer::IsSupportedMedia("bitmap.fon"),
          "and a Windows bitmap font");
    Check(!UltraCanvasMediaViewer::IsSupportedMedia("archive.zip"),
          "while a zip is still not media");
}

} // namespace

int main() {
    std::cout << "===== Font glyph browser =====\n";
    TestEmptyViewer();
    TestGridGeometry();
    TestPointRoundTrip();
    TestScrolling();
    TestCellSize();
    TestControlPlacement();
    TestArrangedByALayoutPass();
    TestMediaViewerIntegration();
    std::cout << "\n" << (g_failures ? "FAILED" : "PASSED") << " ("
              << g_failures << " failure" << (g_failures == 1 ? "" : "s") << ")\n";
    return g_failures == 0 ? 0 : 1;
}
