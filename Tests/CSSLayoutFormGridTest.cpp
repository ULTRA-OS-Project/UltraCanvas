// Tests/CSSLayoutFormGridTest.cpp
// The two-column form grid every dialog is built from (UltraCanvasFormLayout):
// columns [auto, 1fr], a caption and a control per row, plus rows that span
// both columns (a checkbox, a section heading, a note).
//
// The rule this guards is CSS Grid §12.5: an item whose span crosses a
// flexible track contributes nothing to the base size of the intrinsic tracks
// it also spans. Without it a single wide full-width row drags the `auto`
// caption column out to its own width, and every control in the dialog is
// pushed across the window - which is exactly what the image export dialog
// looked like.
//
// No UI stack: the layout engine is pure geometry, so the leaf stand-ins below
// publish the intrinsic sizes UltraCanvasLabel publishes at runtime without
// needing a render context to measure real text.
//
// Version: 1.1.0
// Last Modified: 2026-09-22
// Author: UltraCanvas Framework

#include "CSSLayout/CSSLayout.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace UltraCanvas;
using namespace UltraCanvas::CSSLayout;

static int g_failures = 0;

static void CheckTrue(bool ok, const char* what) {
    if (!ok) {
        std::printf("  FAIL: %s\n", what);
        ++g_failures;
    } else {
        std::printf("  ok:   %s\n", what);
    }
}

static void CheckNear(float actual, float expected, const char* what, float tol = 0.01f) {
    if (std::fabs(actual - expected) > tol) {
        std::printf("  FAIL: %s = %.2f (expected %.2f)\n", what, actual, expected);
        ++g_failures;
    } else {
        std::printf("  ok:   %s = %.2f\n", what, actual);
    }
}

// A label-like leaf: its max-content width is its text width, and it reports
// that through the intrinsic-sizing protocol the way UltraCanvasLabel does.
struct TextStub : Element {
    float textWidth = 0.0f;
    float textHeight = 20.0f;

    explicit TextStub(float width, float height = 20.0f) : textWidth(width), textHeight(height) {}

    void ComputeIntrinsicSizes(const LayoutContext&) override {
        intrinsic.valid = true;
        intrinsic.maxContentWidth = textWidth;
        intrinsic.minContentWidth = std::min(textWidth, 20.0f);
        intrinsic.maxContentHeight = textHeight;
        intrinsic.minContentHeight = textHeight;
    }

    Size2Df MeasureOwnContent(std::optional<float> definiteContentWidth, const LayoutContext&) override {
        const float w = definiteContentWidth.value_or(textWidth);
        return { w, textHeight };
    }
};

static std::shared_ptr<TextStub> Text(float width, float height = 20.0f) {
    return std::make_shared<TextStub>(width, height);
}

// Lays a grid out at an exact width, the way a dialog's flex column does.
static void LayOut(const std::shared_ptr<Element>& grid, float width) {
    LayoutContext ctx;
    ctx.viewportWidth = width;
    ctx.viewportHeight = 600.0f;
    MeasureConstraints mc{ { ConstraintMode::Exact, width },
                           { ConstraintMode::AtMost, 600.0f } };
    grid->Measure(mc, ctx);
    grid->Arrange(Rect2Df{ 0, 0, width, grid->measured.measuredHeight }, ctx);
}

// The form grid itself: [auto, 1fr].
static std::shared_ptr<Element> FormGrid(float rowGap, float columnGap) {
    auto grid = std::make_shared<Element>();
    grid->layout.SetGrid();
    GridTrackSize caption;                       // auto
    GridTrackSize control;
    control.kind = GridTrackSizeKind::Fr;
    control.value = Dimension::Fr(1);
    grid->layout.SetGridColumns({caption, control});
    grid->layout.SetGridGap(rowGap, columnGap);
    return grid;
}

// A control stand-in: a definite box, the way a text input or a dropdown
// built with an explicit control height reaches the layout.
static std::shared_ptr<Element> Box(float width, float height) {
    auto box = std::make_shared<Element>();
    if (width  > 0) box->size.width  = Dimension::Px(width);
    if (height > 0) box->size.height = Dimension::Px(height);
    return box;
}

// Lays a dialog's flex column out at an exact size - the case that matters is
// the one where that height is less than the column's children need.
static void LayOutBox(const std::shared_ptr<Element>& element, float width, float height) {
    LayoutContext ctx;
    ctx.viewportWidth = width;
    ctx.viewportHeight = height;
    MeasureConstraints mc{ { ConstraintMode::Exact, width },
                           { ConstraintMode::Exact, height } };
    element->Measure(mc, ctx);
    element->Arrange(Rect2Df{ 0, 0, width, height }, ctx);
}

static void SpanBothColumns(const std::shared_ptr<Element>& element) {
    GridLine autoStart;
    GridLine spanTwo;
    spanTwo.type = GridLineKind::Span;
    spanTwo.index = 2;
    element->layoutItem.SetGridColumn(autoStart, spanTwo);
}

int main() {
    std::printf("=== CSSLayout form grid ===\n");

    constexpr float kGridWidth = 500.0f;
    constexpr float kColumnGap = 10.0f;

    // ----- captions and controls -----
    auto grid = FormGrid(8.0f, kColumnGap);

    auto shortCaption = Text(50.0f);     // "Size:"
    auto shortControl = Text(120.0f);
    auto longCaption  = Text(96.0f);     // "Transparency:" — the widest caption
    auto longControl  = Text(120.0f);
    grid->AddChild(shortCaption);
    grid->AddChild(shortControl);
    grid->AddChild(longCaption);
    grid->AddChild(longControl);

    // A full-width note far wider than any caption — the case that used to
    // wreck the layout.
    auto note = Text(420.0f);
    SpanBothColumns(note);
    grid->AddChild(note);

    LayOut(grid, kGridWidth);

    CheckNear(shortCaption->finalBounds.width, 96.0f,
              "caption column is as wide as the widest caption");
    CheckNear(longCaption->finalBounds.width, 96.0f,
              "both captions get that same column width");
    CheckNear(shortControl->finalBounds.x, 96.0f + kColumnGap,
              "control starts where the caption column ends");
    CheckNear(longControl->finalBounds.x, shortControl->finalBounds.x,
              "every control starts at the same x");
    CheckNear(shortControl->finalBounds.width, kGridWidth - 96.0f - kColumnGap,
              "the control column takes the rest of the row");
    CheckNear(note->finalBounds.width, kGridWidth,
              "a spanning row covers both columns");
    CheckNear(note->finalBounds.x, 0.0f, "a spanning row starts at the grid's left edge");

    // ----- a hidden row leaves the grid entirely -----
    auto hiddenGrid = FormGrid(8.0f, kColumnGap);
    auto visibleCaption = Text(50.0f);
    auto visibleControl = Text(120.0f);
    auto hiddenCaption = Text(300.0f);    // would dominate the column if counted
    auto hiddenControl = Text(120.0f);
    hiddenGrid->AddChild(visibleCaption);
    hiddenGrid->AddChild(visibleControl);
    hiddenGrid->AddChild(hiddenCaption);
    hiddenGrid->AddChild(hiddenControl);
    hiddenCaption->layout.Hide();
    hiddenControl->layout.Hide();
    LayOut(hiddenGrid, kGridWidth);

    CheckNear(visibleCaption->finalBounds.width, 50.0f,
              "a hidden row does not size the caption column");
    CheckNear(visibleControl->finalBounds.x, 50.0f + kColumnGap,
              "nor push the controls across");

    // ----- no flexible track: a spanning item may still grow the columns -----
    // (The §12.5 exemption is about flexible tracks; a grid of two auto
    // columns still distributes a wide spanning item over them.)
    auto autoGrid = std::make_shared<Element>();
    autoGrid->layout.SetGrid();
    GridTrackSize autoColumn;
    autoGrid->layout.SetGridColumns({autoColumn, autoColumn});
    autoGrid->layout.SetGridGap(0.0f, 0.0f);
    auto a = Text(40.0f);
    auto b = Text(40.0f);
    auto wide = Text(400.0f);
    autoGrid->AddChild(a);
    autoGrid->AddChild(b);
    SpanBothColumns(wide);
    autoGrid->AddChild(wide);
    LayOut(autoGrid, kGridWidth);
    CheckNear(a->finalBounds.width, 200.0f,
              "with no fr track the spanning item shares itself over the auto columns");

    // ----- a dialog too short for its form does not squeeze the rows -----
    // What this guards, from UltraCloud's add-account dialog: the form was a
    // flex container per field, and the dialog's column was two pixels short
    // of the rows it held. Every row was shrunk below the 32 px control inside
    // it, so every row raised a vertical scrollbar; that narrowed the viewport
    // enough to raise a horizontal one as well, and the pair was painted
    // straight across the caption and the field. A form grid is flex-shrink: 0
    // (CreateFormGrid), so its rows keep their own height and the dialog is
    // what has to be tall enough.
    constexpr float kControlHeight = 32.0f;
    constexpr float kShortDialog   = 280.0f;   // 7 rows + buttons need 318

    auto dialog = std::make_shared<Element>();
    dialog->layout.SetFlexColumn().SetFlexGap(10.0f);

    auto tightForm = FormGrid(8.0f, kColumnGap);
    tightForm->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    std::vector<std::shared_ptr<Element>> fields;
    for (int i = 0; i < 7; ++i) {
        tightForm->AddChild(Text(70.0f));
        auto field = Box(0.0f, kControlHeight);
        fields.push_back(field);
        tightForm->AddChild(field);
    }
    dialog->AddChild(tightForm);
    auto buttonRow = Box(0.0f, 36.0f);
    buttonRow->layoutItem.SetFlexShrink(0);
    dialog->AddChild(buttonRow);

    LayOutBox(dialog, kGridWidth, kShortDialog);

    bool everyFieldKeptItsHeight = true;
    for (const auto& field : fields)
        if (std::fabs(field->finalBounds.height - kControlHeight) > 0.01f)
            everyFieldKeptItsHeight = false;
    CheckTrue(everyFieldKeptItsHeight,
              "a dialog shorter than its form leaves every row at its own height");

    // The shape it replaced, for the contrast: a flex row per field, shrinking
    // with the column until it is shorter than the control it contains.
    auto oldDialog = std::make_shared<Element>();
    oldDialog->layout.SetFlexColumn().SetFlexGap(10.0f);
    std::vector<std::shared_ptr<Element>> oldRows;
    for (int i = 0; i < 7; ++i) {
        auto row = Box(0.0f, kControlHeight);
        row->layout.SetFlexRow().SetFlexGap(8.0f);
        row->AddChild(Text(70.0f));
        row->AddChild(Box(0.0f, kControlHeight));
        oldRows.push_back(row);
        oldDialog->AddChild(row);
    }
    oldDialog->AddChild(Box(0.0f, 36.0f));
    LayOutBox(oldDialog, kGridWidth, kShortDialog);

    bool someRowWasSqueezed = false;
    for (const auto& row : oldRows)
        if (row->finalBounds.height < kControlHeight - 0.01f) someRowWasSqueezed = true;
    CheckTrue(someRowWasSqueezed,
              "a flex row per field is squeezed below the control inside it");

    std::printf(g_failures == 0 ? "=== PASSED ===\n" : "=== FAILED ===\n");
    return g_failures == 0 ? 0 : 1;
}
