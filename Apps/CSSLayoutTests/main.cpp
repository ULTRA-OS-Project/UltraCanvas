// Apps/CSSLayoutTests/main.cpp
// Non-UI test harness for the new UltraCanvas::CSSLayout engine.
// Builds Element trees, calls Measure+Arrange, and asserts finalBounds.
// Version: 1.3.1
// Last Modified: 2026-06-02
// Author: UltraCanvas Framework

#include "CSSLayout/CSSLayout.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace UltraCanvas;            // for Rect2Df and other common types
using namespace UltraCanvas::CSSLayout;

namespace {

// Leaf element with an explicit intrinsic content size (no real rendering).
class Leaf : public Element {
public:
    float intrinsicW = 0, intrinsicH = 0;

    Leaf(std::string name, float w, float h) {
        id = std::move(name);
        intrinsicW = w;
        intrinsicH = h;
    }

    void ComputeIntrinsicSizes(const LayoutContext&) override {
        intrinsic.minContentWidth  = intrinsicW;
        intrinsic.maxContentWidth  = intrinsicW;
        intrinsic.minContentHeight = intrinsicH;
        intrinsic.maxContentHeight = intrinsicH;
    }

    Size2Df MeasureOwnContent(std::optional<float>, const LayoutContext&) override {
        // Publish the leaf's intrinsic content-box size; the block layout
        // applies size.*/constraints (explicit size, Exact, AtMost, min/max)
        // around it — exactly as the real widgets now do.
        return Size2Df(intrinsicW, intrinsicH);
    }
};

// -------------------- assert helpers --------------------

int g_pass = 0;
int g_fail = 0;
std::string g_currentTest;

bool approx(float a, float b, float eps = 0.05f) {
    return std::fabs(a - b) < eps;
}

void expectInt(const std::string& label, int actual, int expected) {
    if (actual == expected) {
        ++g_pass;
        std::printf("  [PASS] %s = %d\n", label.c_str(), actual);
    } else {
        ++g_fail;
        std::printf("  [FAIL] %s: expected %d got %d\n",
                    label.c_str(), expected, actual);
    }
}

void expectRect(const std::string& label, const Element& e,
                float x, float y, float w, float h) {
    bool ok = approx(e.finalBounds.x, x)
           && approx(e.finalBounds.y, y)
           && approx(e.finalBounds.width,  w)
           && approx(e.finalBounds.height, h);
    if (ok) {
        ++g_pass;
        std::printf("  [PASS] %s = (%.1f, %.1f, %.1f, %.1f)\n",
                    label.c_str(), e.finalBounds.x, e.finalBounds.y,
                    e.finalBounds.width, e.finalBounds.height);
    } else {
        ++g_fail;
        std::printf("  [FAIL] %s: expected (%.1f, %.1f, %.1f, %.1f) got (%.1f, %.1f, %.1f, %.1f)\n",
                    label.c_str(), x, y, w, h,
                    e.finalBounds.x, e.finalBounds.y,
                    e.finalBounds.width, e.finalBounds.height);
    }
}

void expectMeasured(const std::string& label, const Element& e,
                    float w, float h) {
    bool ok = approx(e.measured.measuredWidth,  w)
           && approx(e.measured.measuredHeight, h);
    if (ok) {
        ++g_pass;
        std::printf("  [PASS] %s measured = (%.1f, %.1f)\n",
                    label.c_str(), e.measured.measuredWidth,
                    e.measured.measuredHeight);
    } else {
        ++g_fail;
        std::printf("  [FAIL] %s: expected measured (%.1f, %.1f) got (%.1f, %.1f)\n",
                    label.c_str(), w, h,
                    e.measured.measuredWidth, e.measured.measuredHeight);
    }
}

template <typename... K>
void addAll(Element& parent, K&&... kids) {
    (parent.AddChild(std::forward<K>(kids)), ...);
}

// Measure a container with both axes Unbounded so its measured size is derived
// purely from its children (auto sizing) — the right lens for AbsoluteUI, whose
// whole point is to grow an otherwise auto-sized container.
void measureAuto(Element& root, const LayoutContext& ctx) {
    MeasureConstraints c{
        { ConstraintMode::Unbounded, INFINITY },
        { ConstraintMode::Unbounded, INFINITY }
    };
    root.Measure(c, ctx);
}

// Build a leaf with position:absolute|AbsoluteUI at left/top with an explicit size.
static std::shared_ptr<Leaf> makePositionedChild(const char* name, PositionType pt,
                                                 float left, float top,
                                                 float w, float h) {
    auto child = std::make_shared<Leaf>(name, 0, 0);
    child->layoutItem.positionType = pt;
    // Position field order: top, right, bottom, left.
    child->layoutItem.position = Position{
        Dimension::Px(top), Dimension::Auto(), Dimension::Auto(), Dimension::Px(left)
    };
    child->size.width  = Dimension::Px(w);
    child->size.height = Dimension::Px(h);
    return child;
}

void runRoot(Element& root, float w, float h, const LayoutContext& ctx) {
    MeasureConstraints c{
        { ConstraintMode::Exact, w },
        { ConstraintMode::Exact, h }
    };
    root.Measure(c, ctx);
    root.Arrange(Rect2Df{0, 0, w, h}, ctx);
}

void beginTest(const char* name) {
    g_currentTest = name;
    std::printf("\n== %s ==\n", name);
}

// -------------------- scenarios --------------------

// Three Px(50)-tall leaves stack to y=0, 50, 100 inside a 300x200 block container.
void test_block_stacking() {
    beginTest("block stacking (3 children, fixed heights)");
    LayoutContext ctx;
    auto root = std::make_shared<Leaf>("root", 0, 0);
    root->layout.display = DisplayType::Block;
    for (int i = 0; i < 3; ++i) {
        auto kid = std::make_shared<Leaf>("kid" + std::to_string(i), 100, 50);
        root->AddChild(kid);
    }
    runRoot(*root, 300, 200, ctx);
    expectRect("root",  *root,                       0,   0, 300, 200);
    expectRect("kid0",  *root->Children()[0],        0,   0, 300,  50);
    expectRect("kid1",  *root->Children()[1],        0,  50, 300,  50);
    expectRect("kid2",  *root->Children()[2],        0, 100, 300,  50);
}

// position: absolute; left:10; top:10; width:50; height:50 in 200x200 CB.
void test_absolute_top_left() {
    beginTest("absolute top-left + explicit size");
    LayoutContext ctx;
    auto root = std::make_shared<Leaf>("root", 0, 0);
    root->layout.display = DisplayType::Block;
    auto child = std::make_shared<Leaf>("abs", 0, 0);
    child->layoutItem.positionType = PositionType::Absolute;
    child->layoutItem.position = Position{
        Dimension::Px(10), Dimension::Auto(), Dimension::Auto(), Dimension::Px(10)
    };
    child->size.width  = Dimension::Px(50);
    child->size.height = Dimension::Px(50);
    root->AddChild(child);
    runRoot(*root, 200, 200, ctx);
    expectRect("abs", *child, 10, 10, 50, 50);
}

// position: absolute; left:10; right:10; top:0; bottom:0 in 200x100 CB; size auto → stretched.
void test_absolute_stretched() {
    beginTest("absolute stretched (left+right+top+bottom, size:auto)");
    LayoutContext ctx;
    auto root = std::make_shared<Leaf>("root", 0, 0);
    root->layout.display = DisplayType::Block;
    auto child = std::make_shared<Leaf>("abs", 0, 0);
    child->layoutItem.positionType = PositionType::Absolute;
    // AbsoluteItem field order: left, top, right, bottom.
    child->layoutItem.position = Position{
        Dimension::Px(0), Dimension::Px(10), Dimension::Px(0), Dimension::Px(10)
    };
    root->AddChild(child);
    runRoot(*root, 200, 100, ctx);
    expectRect("abs", *child, 10, 0, 180, 100);
}

// position: absolute; left:0; right:0; width:80; margin auto on both sides → centered.
void test_absolute_centered_margin_auto() {
    beginTest("absolute centered via margin:auto");
    LayoutContext ctx;
    auto root = std::make_shared<Leaf>("root", 0, 0);
    root->layout.display = DisplayType::Block;
    auto child = std::make_shared<Leaf>("abs", 0, 0);
    child->layoutItem.positionType = PositionType::Absolute;
    // left=0, top=0, right=0, bottom=auto; width=80; margin-l/r=auto → centered.
    child->layoutItem.position = Position{
        Dimension::Px(0), Dimension::Px(0), Dimension::Auto(), Dimension::Px(0)
    };
    child->size.width  = Dimension::Px(80);
    child->size.height = Dimension::Px(20);
    child->box.margin.left  = Dimension::Auto();
    child->box.margin.right = Dimension::Auto();
    root->AddChild(child);
    runRoot(*root, 200, 100, ctx);
    expectRect("abs", *child, 60, 0, 80, 20);  // (200-80)/2 = 60
}

// position: fixed resolves against the viewport from LayoutContext, not parent.
void test_fixed_uses_viewport() {
    beginTest("fixed positioned against viewport");
    LayoutContext ctx;
    ctx.viewportWidth = 1000;
    ctx.viewportHeight = 800;
    auto root = std::make_shared<Leaf>("root", 0, 0);
    root->layout.display = DisplayType::Block;
    auto child = std::make_shared<Leaf>("fix", 0, 0);
    child->layoutItem.positionType = PositionType::Fixed;
    // left=20, top=20, right/bottom=auto
    child->layoutItem.position = Position{
        Dimension::Px(20), Dimension::Auto(), Dimension::Auto(), Dimension::Px(20)
    };
    child->size.width  = Dimension::Px(100);
    child->size.height = Dimension::Px(40);
    root->AddChild(child);
    runRoot(*root, 200, 100, ctx);
    // Fixed CB is (0,0,1000,800), so left=20, top=20 → (20, 20, 100, 40).
    expectRect("fix", *child, 20, 20, 100, 40);
}

// position: relative shifts the element but leaves siblings unchanged.
void test_relative_shifts_self_not_siblings() {
    beginTest("relative offset shifts self, not siblings");
    LayoutContext ctx;
    auto root = std::make_shared<Leaf>("root", 0, 0);
    root->layout.display = DisplayType::Block;
    auto a = std::make_shared<Leaf>("a", 100, 30);
    auto b = std::make_shared<Leaf>("b", 100, 30);
    auto c = std::make_shared<Leaf>("c", 100, 30);
    b->layoutItem.positionType = PositionType::Relative;
    // left=15, top=20 (then right/bottom auto)
    b->layoutItem.position = Position{
        Dimension::Px(20), Dimension::Auto(), Dimension::Auto(), Dimension::Px(15)
    };
    addAll(*root, a, b, c);
    runRoot(*root, 200, 200, ctx);
    expectRect("a", *a, 0,  0, 200, 30);
    expectRect("b", *b, 15, 50, 200, 30);   // in-flow at (0,30) + (left:15, top:20)
    expectRect("c", *c, 0, 60, 200, 30);   // c stays where it would have been
}

// -------------------- AbsoluteUI: positions like Absolute, sizes its container --------------------

// AbsoluteUI child (left=10, top=20, 30x40) grows an auto-sized block container
// to (40, 60), AND is positioned exactly like Absolute at (10, 20, 30, 40).
void test_absoluteui_grows_block_container() {
    beginTest("AbsoluteUI grows auto block container to x+w / y+h");
    LayoutContext ctx;
    // Use a plain Element container: the harness's Leaf overrides MeasureOwnContent and
    // short-circuits to its intrinsic size, bypassing MeasureBlock entirely.
    auto root = std::make_shared<Element>();
    root->layout.display = DisplayType::Block;
    auto child = makePositionedChild("ui", PositionType::AbsoluteUI, 10, 20, 30, 40);
    root->AddChild(child);

    measureAuto(*root, ctx);
    expectMeasured("root", *root, 40, 60);   // 10+30, 20+40

    runRoot(*root, 200, 200, ctx);
    expectRect("ui (positioned like absolute)", *child, 10, 20, 30, 40);
}

// Control: a plain Absolute child with identical geometry does NOT grow the
// container (measured stays 0x0) — proving AbsoluteUI is the opt-in difference.
void test_absolute_does_not_grow_container() {
    beginTest("Absolute (control) leaves container measured size unchanged");
    LayoutContext ctx;
    auto root = std::make_shared<Element>();
    root->layout.display = DisplayType::Block;
    auto child = makePositionedChild("abs", PositionType::Absolute, 10, 20, 30, 40);
    root->AddChild(child);

    measureAuto(*root, ctx);
    expectMeasured("root", *root, 0, 0);     // absolute is out-of-flow, no contribution

    runRoot(*root, 200, 200, ctx);
    expectRect("abs", *child, 10, 20, 30, 40);  // still positioned correctly
}

// AbsoluteUI grows a flex container (no in-flow children) the same way.
void test_absoluteui_grows_flex_container() {
    beginTest("AbsoluteUI grows auto flex container");
    LayoutContext ctx;
    auto root = std::make_shared<Element>();
    root->layout.display = DisplayType::Flex;
    root->layout.data = FlexLayout{};
    auto child = makePositionedChild("ui", PositionType::AbsoluteUI, 5, 15, 25, 35);
    root->AddChild(child);

    measureAuto(*root, ctx);
    expectMeasured("flex root", *root, 30, 50);  // 5+25, 15+35

    runRoot(*root, 200, 200, ctx);
    expectRect("ui", *child, 5, 15, 25, 35);
}

// AbsoluteUI grows a grid container (empty grid, no in-flow items).
void test_absoluteui_grows_grid_container() {
    beginTest("AbsoluteUI grows auto grid container");
    LayoutContext ctx;
    auto root = std::make_shared<Element>();
    root->layout.display = DisplayType::Grid;
    root->layout.data = GridLayout{};
    auto child = makePositionedChild("ui", PositionType::AbsoluteUI, 12, 8, 40, 22);
    root->AddChild(child);

    measureAuto(*root, ctx);
    expectMeasured("grid root", *root, 52, 30);  // 12+40, 8+22

    runRoot(*root, 200, 200, ctx);
    expectRect("ui", *child, 12, 8, 40, 22);
}

// An explicit container size wins: AbsoluteUI does not override it even when the
// child's extent (60 tall) exceeds the explicit height (50).
void test_absoluteui_respects_explicit_size() {
    beginTest("AbsoluteUI does not override an explicit container size");
    LayoutContext ctx;
    auto root = std::make_shared<Element>();
    root->layout.display = DisplayType::Block;
    root->size.width  = Dimension::Px(100);
    root->size.height = Dimension::Px(50);
    auto child = makePositionedChild("ui", PositionType::AbsoluteUI, 10, 20, 30, 40);
    root->AddChild(child);

    measureAuto(*root, ctx);
    expectMeasured("root", *root, 100, 50);  // explicit size, not 40x60
}

// In-flow content and an AbsoluteUI child combine via max() on each axis.
void test_absoluteui_combines_with_inflow() {
    beginTest("AbsoluteUI extent combines with in-flow children via max()");
    LayoutContext ctx;
    auto root = std::make_shared<Element>();
    root->layout.display = DisplayType::Block;
    auto flow = std::make_shared<Leaf>("flow", 70, 25);   // in-flow 70x25
    auto ui   = makePositionedChild("ui", PositionType::AbsoluteUI, 10, 20, 30, 40); // extent 40x60
    addAll(*root, flow, ui);

    measureAuto(*root, ctx);
    // width = max(70, 40) = 70; height = max(25 [stacked], 60) = 60.
    expectMeasured("root", *root, 70, 60);
}

// -------------------- Phase 3: Flex --------------------

static std::shared_ptr<Element> makeFlexRoot(FlexDirection dir, FlexWrap wrap,
                                             JustifyContent jc,
                                             AlignItems ai = AlignItems::Stretch,
                                             AlignContent ac = AlignContent::Stretch,
                                             float rowGap = 0, float colGap = 0) {
    auto root = std::make_shared<Leaf>("flex", 0, 0);
    root->layout.display = DisplayType::Flex;
    FlexLayout fl;
    fl.direction = dir;
    fl.wrap = wrap;
    fl.justifyContent = jc;
    fl.alignItems = ai;
    fl.alignContent = ac;
    fl.gap.row = Dimension::Px(rowGap);
    fl.gap.column = Dimension::Px(colGap);
    root->layout.data = fl;
    return root;
}

static std::shared_ptr<Leaf> makeFlexChild(const char* name, float w, float h,
                                           float grow = 0, float shrink = 1,
                                           Dimension basis = Dimension::Auto()) {
    auto k = std::make_shared<Leaf>(name, w, h);
    FlexItem fi;
    fi.grow = grow;
    fi.shrink = shrink;
    fi.basis = basis;
    k->layoutItem.data = fi;
    return k;
}

void test_flex_row_grow_equal() {
    beginTest("flex row: 3 children grow:1 in 300px → equal thirds");
    LayoutContext ctx;
    auto root = makeFlexRoot(FlexDirection::Row, FlexWrap::NoWrap, JustifyContent::Start);
    auto a = makeFlexChild("a", 0, 50, 1);
    auto b = makeFlexChild("b", 0, 50, 1);
    auto c = makeFlexChild("c", 0, 50, 1);
    addAll(*root, a, b, c);
    runRoot(*root, 300, 100, ctx);
    expectRect("a", *a,   0, 0, 100, 100);
    expectRect("b", *b, 100, 0, 100, 100);
    expectRect("c", *c, 200, 0, 100, 100);
}

void test_flex_row_wrap() {
    beginTest("flex row wrap: four 80px children in 200px → 2 lines");
    LayoutContext ctx;
    auto root = makeFlexRoot(FlexDirection::Row, FlexWrap::Wrap,
                             JustifyContent::Start, AlignItems::Start,
                             AlignContent::Start);
    auto a = makeFlexChild("a", 80, 30);
    auto b = makeFlexChild("b", 80, 30);
    auto c = makeFlexChild("c", 80, 30);
    auto d = makeFlexChild("d", 80, 30);
    addAll(*root, a, b, c, d);
    runRoot(*root, 200, 100, ctx);
    // Two per line (80+80=160 ≤ 200; 80+80+80=240 > 200).
    expectRect("a", *a,   0,  0, 80, 30);
    expectRect("b", *b,  80,  0, 80, 30);
    expectRect("c", *c,   0, 30, 80, 30);
    expectRect("d", *d,  80, 30, 80, 30);
}

void test_flex_row_space_between() {
    beginTest("flex row justify=SpaceBetween");
    LayoutContext ctx;
    auto root = makeFlexRoot(FlexDirection::Row, FlexWrap::NoWrap,
                             JustifyContent::SpaceBetween, AlignItems::Start);
    auto a = makeFlexChild("a", 50, 20);
    auto b = makeFlexChild("b", 50, 20);
    auto c = makeFlexChild("c", 50, 20);
    addAll(*root, a, b, c);
    runRoot(*root, 300, 50, ctx);
    // total fixed width = 150, free = 150, between = 150/2 = 75.
    expectRect("a", *a,   0, 0, 50, 20);
    expectRect("b", *b, 125, 0, 50, 20);
    expectRect("c", *c, 250, 0, 50, 20);
}

void test_flex_column_align_center() {
    beginTest("flex column align-items: center");
    LayoutContext ctx;
    auto root = makeFlexRoot(FlexDirection::Column, FlexWrap::NoWrap,
                             JustifyContent::Start, AlignItems::Center);
    auto a = makeFlexChild("a", 80, 20);
    auto b = makeFlexChild("b", 40, 30);
    addAll(*root, a, b);
    runRoot(*root, 200, 100, ctx);
    expectRect("a", *a, (200 - 80) / 2.f,  0, 80, 20);
    expectRect("b", *b, (200 - 40) / 2.f, 20, 40, 30);
}

void test_flex_row_gap() {
    beginTest("flex row with column-gap=10");
    LayoutContext ctx;
    auto root = makeFlexRoot(FlexDirection::Row, FlexWrap::NoWrap,
                             JustifyContent::Start, AlignItems::Start,
                             AlignContent::Stretch, /*rowGap*/ 0, /*colGap*/ 10);
    auto a = makeFlexChild("a", 50, 20);
    auto b = makeFlexChild("b", 50, 20);
    auto c = makeFlexChild("c", 50, 20);
    addAll(*root, a, b, c);
    runRoot(*root, 300, 50, ctx);
    expectRect("a", *a,   0, 0, 50, 20);
    expectRect("b", *b,  60, 0, 50, 20);
    expectRect("c", *c, 120, 0, 50, 20);
}

void test_flex_row_grow_mixed() {
    beginTest("flex row: fixed 100 + grow:1 + grow:2 in 400px");
    LayoutContext ctx;
    auto root = makeFlexRoot(FlexDirection::Row, FlexWrap::NoWrap, JustifyContent::Start);
    auto a = makeFlexChild("a", 100, 30, /*grow*/0);  // fixed 100
    auto b = makeFlexChild("b", 0,   30, /*grow*/1);  // grow 1
    auto c = makeFlexChild("c", 0,   30, /*grow*/2);  // grow 2
    addAll(*root, a, b, c);
    runRoot(*root, 400, 50, ctx);
    // Free = 400 - 100 = 300; b = 100, c = 200.
    expectRect("a", *a,   0, 0, 100, 50);
    expectRect("b", *b, 100, 0, 100, 50);
    expectRect("c", *c, 200, 0, 200, 50);
}

// Regression for the splash-screen overflow: a column flex with two grow:1
// spacers around a fixed child that carries a main-axis (bottom) margin.
// The spacers must absorb only the slack AFTER deducting the child's margin
// and the gaps, so the bottom spacer ends exactly at the content height.
// Previously the margin was omitted from the flex free-space budget, so the
// spacers over-grew and the column overflowed by the margin amount.
void test_flex_column_grow_with_margin() {
    beginTest("flex column: grow spacers + margined child fit exactly (no overflow)");
    LayoutContext ctx;
    auto root = makeFlexRoot(FlexDirection::Column, FlexWrap::NoWrap,
                             JustifyContent::Start, AlignItems::Stretch,
                             AlignContent::Stretch, /*rowGap*/ 4, /*colGap*/ 0);
    auto top = makeFlexChild("top", 0, 0, /*grow*/1);
    auto mid = makeFlexChild("mid", 60, 40, /*grow*/0);
    mid->box.margin.bottom = Dimension::Px(10);   // main-axis margin
    auto bot = makeFlexChild("bot", 0, 0, /*grow*/1);
    addAll(*root, top, mid, bot);
    runRoot(*root, 100, 200, ctx);
    // budget = 200 − margin(10) − gaps(2*4=8) − child(40) = 142; each spacer = 71.
    // Stack: top[0..71] gap mid[75..115]+m10 gap bot[129..200].
    expectRect("top", *top, 0,   0, 100, 71);
    expectRect("mid", *mid, 0,  75, 100, 40);
    expectRect("bot", *bot, 0, 129, 100, 71);
    // The bottom spacer must end exactly at the content height — no overflow.
    expectInt("bot bottom edge",
              (int)(bot->finalBounds.y + bot->finalBounds.height), 200);
}

// Regression: a nested flex column whose inner section has BOTH an explicit
// height AND flex-grow:1. The section grows correctly inside the root, and it
// must then lay out ITS OWN children against its GROWN (used) height, not its
// explicit flex-basis. Before the both-axes-Exact fix, the section re-read its
// specified height (80) when arranging its children, so chartElem only filled
// 80-18 = 62px and a gap opened between the thumbs row and the status row —
// exactly the bug reported in the Image Performance Test demo.
void test_flex_nested_grow_with_explicit_basis() {
    beginTest("flex nested: inner section (explicit h + grow) lays out children against grown height");
    LayoutContext ctx;

    auto root = makeFlexRoot(FlexDirection::Column, FlexWrap::NoWrap, JustifyContent::Start);

    // Inner flex-column container: explicit height 80 (the flex-basis) + grow 1.
    auto chartSection = makeFlexRoot(FlexDirection::Column, FlexWrap::NoWrap, JustifyContent::Start);
    chartSection->size.height = Dimension::Px(80);
    { FlexItem fi; fi.grow = 1; chartSection->layoutItem.data = fi; }

    auto chartElem = makeFlexChild("chartElem", 0,  0, /*grow*/1);   // fills the section
    auto thumbsRow = makeFlexChild("thumbsRow", 0, 18, /*grow*/0);   // fixed 18, at the bottom
    addAll(*chartSection, chartElem, thumbsRow);

    auto statusRow = makeFlexChild("statusRow", 0, 20, /*grow*/0);   // pinned below the section
    addAll(*root, chartSection, statusRow);

    runRoot(*root, 100, 200, ctx);

    // Root: free = 200 - 80(basis) - 20 = 100 → section grows to 180, status at y=180.
    expectRect("chartSection", *chartSection, 0,   0, 100, 180);
    expectRect("statusRow",    *statusRow,    0, 180, 100,  20);

    // Inner: section's USED height is 180, so chartElem fills 180-18 = 162.
    expectRect("chartElem", *chartElem, 0,   0, 100, 162);
    expectRect("thumbsRow", *thumbsRow, 0, 162, 100,  18);
    expectInt("chartElem fills grown section",
              (int)chartElem->finalBounds.height, 162);
    expectInt("thumbs row bottom edge == section used height",
              (int)(thumbsRow->finalBounds.y + thumbsRow->finalBounds.height), 180);
}

// -------------------- Phase 4: Grid --------------------

static GridTrackSize trackPx(float v)  { GridTrackSize t; t.kind = GridTrackSizeKind::Fixed; t.value = Dimension::Px(v); return t; }
static GridTrackSize trackFr(float v)  { GridTrackSize t; t.kind = GridTrackSizeKind::Fr;    t.value = Dimension::Fr(v); return t; }
static GridTrackSize trackAuto()       { GridTrackSize t; t.kind = GridTrackSizeKind::Auto;  return t; }

static GridLine lineAt(int idx) {
    GridLine g; g.type = GridLineKind::Line; g.index = idx; return g;
}
static GridLine spanN(int n) {
    GridLine g; g.type = GridLineKind::Span; g.index = n; return g;
}

void test_grid_basic_3col_fr() {
    beginTest("grid: columns=100px 1fr 100px, one row, 3 children auto-placed");
    LayoutContext ctx;
    auto root = std::make_shared<Leaf>("grid", 0, 0);
    root->layout.display = DisplayType::Grid;
    GridLayout gl;
    gl.columns.tracks = { trackPx(100), trackFr(1), trackPx(100) };
    gl.rows.tracks    = { trackPx(40) };
    root->layout.data = gl;

    auto a = std::make_shared<Leaf>("a", 50, 20);
    auto b = std::make_shared<Leaf>("b", 50, 20);
    auto c = std::make_shared<Leaf>("c", 50, 20);
    addAll(*root, a, b, c);
    runRoot(*root, 400, 40, ctx);
    // Track widths: 100 | (400-100-100)=200 | 100
    expectRect("a", *a,   0, 0, 100, 40);
    expectRect("b", *b, 100, 0, 200, 40);
    expectRect("c", *c, 300, 0, 100, 40);
}

void test_grid_explicit_placement_and_span() {
    beginTest("grid: explicit columnStart=1 + columnEnd=span(2)");
    LayoutContext ctx;
    auto root = std::make_shared<Leaf>("grid", 0, 0);
    root->layout.display = DisplayType::Grid;
    GridLayout gl;
    gl.columns.tracks = { trackPx(100), trackPx(100), trackPx(100) };
    gl.rows.tracks    = { trackPx(40), trackPx(40) };
    root->layout.data = gl;

    auto wide = std::make_shared<Leaf>("wide", 0, 0);
    {
        GridItem gi;
        gi.columnStart = lineAt(1);
        gi.columnEnd   = spanN(2);
        gi.rowStart    = lineAt(1);
        gi.rowEnd      = spanN(1);
        wide->layoutItem.data = gi;
    }
    auto small = std::make_shared<Leaf>("small", 0, 0);
    // small is auto-placed → goes to first free cell after the explicit one.
    addAll(*root, wide, small);
    runRoot(*root, 300, 80, ctx);
    expectRect("wide",  *wide,   0,  0, 200, 40);
    expectRect("small", *small, 200,  0, 100, 40);
}

void test_grid_gap() {
    beginTest("grid: 2x2 with column-gap=10, row-gap=10");
    LayoutContext ctx;
    auto root = std::make_shared<Leaf>("grid", 0, 0);
    root->layout.display = DisplayType::Grid;
    GridLayout gl;
    gl.columns.tracks = { trackPx(100), trackPx(100) };
    gl.rows.tracks    = { trackPx(40),  trackPx(40)  };
    gl.columnGap = Dimension::Px(10);
    gl.rowGap    = Dimension::Px(10);
    root->layout.data = gl;

    auto a = std::make_shared<Leaf>("a", 0, 0);
    auto b = std::make_shared<Leaf>("b", 0, 0);
    auto c = std::make_shared<Leaf>("c", 0, 0);
    auto d = std::make_shared<Leaf>("d", 0, 0);
    addAll(*root, a, b, c, d);
    runRoot(*root, 210, 90, ctx);
    expectRect("a", *a,   0,  0, 100, 40);
    expectRect("b", *b, 110,  0, 100, 40);
    expectRect("c", *c,   0, 50, 100, 40);
    expectRect("d", *d, 110, 50, 100, 40);
}

void test_grid_auto_track_max_content() {
    beginTest("grid: columns=auto 1fr → auto sizes to max-content of label");
    LayoutContext ctx;
    auto root = std::make_shared<Leaf>("grid", 0, 0);
    root->layout.display = DisplayType::Grid;
    GridLayout gl;
    gl.columns.tracks = { trackAuto(), trackFr(1) };
    gl.rows.tracks    = { trackPx(20) };
    root->layout.data = gl;

    auto label = std::make_shared<Leaf>("label", 80, 16);
    auto input = std::make_shared<Leaf>("input", 0, 0);
    addAll(*root, label, input);
    runRoot(*root, 300, 20, ctx);
    // 'auto' col grows to label's 80px max-content; fr col gets 220.
    expectRect("label", *label,  0, 0,  80, 20);
    expectRect("input", *input, 80, 0, 220, 20);
}

// -------------------- Phase 6: invalidation bubbles up --------------------

void test_addchild_invalidates_ancestor_cache() {
    beginTest("AddChild on a deep node invalidates root's cached measure");
    LayoutContext ctx;
    auto root = std::make_shared<Leaf>("root", 0, 0);
    root->layout.display = DisplayType::Block;
    auto mid  = std::make_shared<Leaf>("mid", 0, 0);
    mid->layout.display = DisplayType::Block;
    auto first = std::make_shared<Leaf>("first", 100, 20);
    root->AddChild(mid);
    mid->AddChild(first);

    // Width measured by an Exact constraint, height grows from children.
    runRoot(*root, 300, 1000, ctx);
    // root content height is just `first`'s 20px (height = 1000 is Exact
    // but block layout sums children; with vertical Exact, root takes 1000.)
    expectRect("first (initial)", *first, 0, 0, 300, 20);

    // Append another child to `mid`. Without ancestor invalidation, root's
    // measured.valid would still be true and re-Measure would short-circuit.
    auto second = std::make_shared<Leaf>("second", 100, 30);
    mid->AddChild(second);

    // Re-Arrange from root. If invalidation walked up, root and mid will
    // re-measure and `second` will be placed at y=20 below `first`.
    runRoot(*root, 300, 1000, ctx);
    expectRect("first (after add)",  *first,  0,  0, 300, 20);
    expectRect("second (after add)", *second, 0, 20, 300, 30);
}

// -------------------- Phase 7: algorithm-state cache --------------------

// Helper: measure root with Exact constraints matching `w`/`h`, then arrange
// at exactly the same rect. When the cache works, the algorithm's heavy
// intermediate (FlexState / GridState) is built once for Measure and reused
// by Arrange.
void runRootExact(Element& root, float w, float h, const LayoutContext& ctx) {
    MeasureConstraints c{
        { ConstraintMode::Exact, w },
        { ConstraintMode::Exact, h }
    };
    root.Measure(c, ctx);
    root.Arrange(Rect2Df{0, 0, w, h}, ctx);
}

void test_flex_arrange_reuses_measure_cache() {
    beginTest("flex: Arrange reuses Measure's cached FlexState (recomputeCount == 1)");
    LayoutContext ctx;
    auto root = makeFlexRoot(FlexDirection::Row, FlexWrap::NoWrap,
                             JustifyContent::Start);
    auto a = makeFlexChild("a", 0, 50, 1);
    auto b = makeFlexChild("b", 0, 50, 1);
    addAll(*root, a, b);
    runRootExact(*root, 200, 100, ctx);
    const LayoutComputed* lc = root->ComputedLayout();
    expectInt("flex root has cached layout",       lc ? 1 : 0, 1);
    expectInt("flex recomputeCount after M+A",     lc ? lc->recomputeCount : -1, 1);
}

void test_grid_arrange_reuses_measure_cache() {
    beginTest("grid: Arrange reuses Measure's cached GridState (recomputeCount == 1)");
    LayoutContext ctx;
    auto root = std::make_shared<Leaf>("grid", 0, 0);
    root->layout.display = DisplayType::Grid;
    GridLayout gl;
    gl.columns.tracks = { trackPx(100), trackFr(1) };
    gl.rows.tracks    = { trackPx(40) };
    root->layout.data = gl;
    auto a = std::make_shared<Leaf>("a", 50, 20);
    auto b = std::make_shared<Leaf>("b", 50, 20);
    addAll(*root, a, b);
    runRootExact(*root, 300, 40, ctx);
    const LayoutComputed* lc = root->ComputedLayout();
    expectInt("grid root has cached layout",       lc ? 1 : 0, 1);
    expectInt("grid recomputeCount after M+A",     lc ? lc->recomputeCount : -1, 1);
}

void test_addchild_invalidates_layout_computed() {
    beginTest("AddChild invalidates flex container's cached FlexState (recomputeCount == 2)");
    LayoutContext ctx;
    auto root = makeFlexRoot(FlexDirection::Row, FlexWrap::NoWrap,
                             JustifyContent::Start);
    auto a = makeFlexChild("a", 0, 50, 1);
    addAll(*root, a);
    runRootExact(*root, 200, 100, ctx);
    expectInt("after 1st M+A", root->ComputedLayout()->recomputeCount, 1);

    auto b = makeFlexChild("b", 0, 50, 1);
    root->AddChild(b);   // bubbles InvalidateLayout to self → layoutComputed->valid=false

    runRootExact(*root, 200, 100, ctx);
    expectInt("after AddChild + 2nd M+A", root->ComputedLayout()->recomputeCount, 2);
}

// -------------------- Phase 10: intrinsic-driven flex/grid --------------------

// Leaf children publish their content via MeasureOwnContent (+ intrinsic.* via
// ComputeIntrinsicSizes). The engine's Flex computeBaseSize must size auto-basis
// items to that content width; otherwise they'd collapse to 0.
void test_flex_uses_intrinsic_for_auto_basis() {
    beginTest("flex row: auto-basis pulls from each item's content width");
    LayoutContext ctx;
    auto root = makeFlexRoot(FlexDirection::Row, FlexWrap::NoWrap, JustifyContent::Start);
    auto a = std::make_shared<Leaf>("a", 80, 30);
    auto b = std::make_shared<Leaf>("b", 100, 30);
    auto c = std::make_shared<Leaf>("c", 60, 30);
    addAll(*root, a, b, c);
    runRoot(*root, 400, 40, ctx);
    // No flex-grow set → items use their content widths; positions stack from 0.
    expectRect("a", *a,   0, 0,  80, 40);
    expectRect("b", *b,  80, 0, 100, 40);
    expectRect("c", *c, 180, 0,  60, 40);
}

void test_grid_auto_track_uses_intrinsic() {
    beginTest("grid auto track sizes from leaf child content width");
    LayoutContext ctx;
    auto root = std::make_shared<Leaf>("grid", 0, 0);
    root->layout.display = DisplayType::Grid;
    GridLayout gl;
    gl.columns.tracks = { trackAuto(), trackAuto() };
    gl.rows.tracks    = { trackPx(40) };
    root->layout.data = gl;
    auto a = std::make_shared<Leaf>("a", 50, 20);
    auto b = std::make_shared<Leaf>("b", 80, 20);
    addAll(*root, a, b);
    runRoot(*root, 300, 40, ctx);
    // Auto columns size to each child's content width: 50, then 80.
    expectRect("a", *a,  0, 0, 50, 40);
    expectRect("b", *b, 50, 0, 80, 40);
}

} // namespace

int main() {
    std::printf("CSSLayout test harness (Phase 0-10)\n");

    test_block_stacking();
    test_absolute_top_left();
    test_absolute_stretched();
    test_absolute_centered_margin_auto();
    test_fixed_uses_viewport();
    test_relative_shifts_self_not_siblings();

    test_absoluteui_grows_block_container();
    test_absolute_does_not_grow_container();
    test_absoluteui_grows_flex_container();
    test_absoluteui_grows_grid_container();
    test_absoluteui_respects_explicit_size();
    test_absoluteui_combines_with_inflow();

    test_flex_row_grow_equal();
    test_flex_row_wrap();
    test_flex_row_space_between();
    test_flex_column_align_center();
    test_flex_row_gap();
    test_flex_row_grow_mixed();
    test_flex_column_grow_with_margin();
    test_flex_nested_grow_with_explicit_basis();

    test_grid_basic_3col_fr();
    test_grid_explicit_placement_and_span();
    test_grid_gap();
    test_grid_auto_track_max_content();

    test_addchild_invalidates_ancestor_cache();

    test_flex_arrange_reuses_measure_cache();
    test_grid_arrange_reuses_measure_cache();
    test_addchild_invalidates_layout_computed();

    test_flex_uses_intrinsic_for_auto_basis();
    test_grid_auto_track_uses_intrinsic();

    std::printf("\n----- %d passed, %d failed -----\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
