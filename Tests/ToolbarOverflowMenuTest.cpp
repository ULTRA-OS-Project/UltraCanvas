// Tests/ToolbarOverflowMenuTest.cpp
// A toolbar's groups and what does not fit in it.
//
// - ToolbarAppearance::groupSpacing is the gap between two groups, across a
//   separator. It was declared and set by the Ribbon preset, but nothing read
//   it. The default (8 = 2 x itemSpacing) must leave a separator exactly as it
//   was, and a bar that tightened its items but never set groupSpacing must
//   not change either.
// - ToolbarOverflowMode::Menu takes the items that do not fit off the end of
//   the bar and lists them behind a chevron button. CreateOverflowMenu and
//   UpdateOverflowButton were empty TODO stubs, so the mode did nothing: the
//   items were squeezed or cut off as in OverflowNone. What the test pins:
//   which items go, that they come back when there is room, that an item the
//   program hid itself stays hidden, that no separator is left dangling at the
//   end of the bar, that the chevron is never an item, and what the menu
//   offers for each kind of item.
//
// No UI stack and no window: the toolbar is laid out the way a window's flex
// column lays it out, and the menu's entries are read and invoked directly.
// Version: 1.0.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework

#include "UltraCanvasToolbar.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::printf("  [%s] %s\n", condition ? "PASS" : "FAIL", what.c_str());
    if (!condition) ++g_failures;
}

void LayOut(const std::shared_ptr<UltraCanvasToolbar>& toolbar, float width, float height) {
    CSSLayout::LayoutContext ctx;
    ctx.viewportWidth = width;
    ctx.viewportHeight = height;
    CSSLayout::MeasureConstraints mc{ { CSSLayout::ConstraintMode::AtMost, width },
                                      { CSSLayout::ConstraintMode::AtMost, height } };
    toolbar->Measure(mc, ctx);
    toolbar->Arrange(Rect2Df{ 0, 0, toolbar->measured.measuredWidth,
                              toolbar->measured.measuredHeight }, ctx);
}

// A bar `width` wide, with a 1 px border and 5 px padding: its items run from
// x = 6 to x = width - 6.
std::shared_ptr<UltraCanvasToolbar> Bar(float width) {
    auto toolbar = std::make_shared<UltraCanvasToolbar>("tb", 0, 0, width, 40);
    return toolbar;
}

std::shared_ptr<UltraCanvasButton> Button(const std::shared_ptr<UltraCanvasToolbar>& toolbar,
                                          const std::string& id, float width = 30.0f) {
    auto b = toolbar->AddButton(id, "", "");
    b->SetElementSize(Size2Df(width, 30));
    return b;
}

// A new width for the bar, as a window resize gives it.
void Resize(const std::shared_ptr<UltraCanvasToolbar>& toolbar, float width) {
    toolbar->SetElementSize(CSSLayout::Dimension::Px(width), CSSLayout::Dimension::Auto());
    LayOut(toolbar, width, 100);
}

float Right(const std::shared_ptr<UltraCanvasUIElement>& e) {
    return e->GetBounds().x + e->GetBounds().width;
}

void TestGroupSpacing() {
    std::printf("groupSpacing is the gap across a separator\n");

    auto toolbar = Bar(400);
    auto a = Button(toolbar, "a");
    auto sep = toolbar->AddSeparator("sep");
    auto b = Button(toolbar, "b");
    LayOut(toolbar, 400, 100);
    const float sepWidth = sep->GetBounds().width;
    float gap = b->GetBounds().x - Right(a) - sepWidth;
    Check(std::fabs(gap - 8.0f) < 0.01f,
          "the default 8 px is the two 4 px item gaps, as a separator always had (got " +
                  std::to_string(gap) + ")");

    ToolbarAppearance ribbon = toolbar->GetAppearance();
    ribbon.groupSpacing = 16.0f;
    toolbar->SetAppearance(ribbon);
    LayOut(toolbar, 400, 100);
    gap = b->GetBounds().x - Right(a) - sepWidth;
    Check(std::fabs(gap - 16.0f) < 0.01f,
          "16 px sets the groups 16 px apart (got " + std::to_string(gap) + ")");
    const float before = sep->GetBounds().x - Right(a);
    const float after = b->GetBounds().x - Right(sep);
    Check(std::fabs(before - after) < 0.01f, "with the line in the middle");

    ToolbarAppearance tight = toolbar->GetAppearance();
    tight.itemSpacing = 2.0f;
    tight.groupSpacing = 8.0f;
    toolbar->SetAppearance(tight);
    LayOut(toolbar, 400, 100);
    gap = b->GetBounds().x - Right(a) - sepWidth;
    Check(std::fabs(gap - 8.0f) < 0.01f,
          "tighter items still get the group gap asked for (got " + std::to_string(gap) + ")");

    tight.groupSpacing = 2.0f;
    toolbar->SetAppearance(tight);
    LayOut(toolbar, 400, 100);
    gap = b->GetBounds().x - Right(a) - sepWidth;
    Check(std::fabs(gap - 4.0f) < 0.01f,
          "a group gap under two item gaps leaves the item gaps (got " + std::to_string(gap) + ")");

    auto column = std::make_shared<UltraCanvasToolbar>("col", 0, 0, 46, 400);
    column->SetOrientation(ToolbarOrientation::Vertical);
    ToolbarAppearance colLook = column->GetAppearance();
    colLook.groupSpacing = 20.0f;
    column->SetAppearance(colLook);
    auto top = column->AddButton("top", "", "");
    top->SetElementSize(Size2Df(30, 30));
    auto colSep = column->AddSeparator("colsep");
    auto bottom = column->AddButton("bottom", "", "");
    bottom->SetElementSize(Size2Df(30, 30));
    LayOut(column, 46, 400);
    gap = bottom->GetBounds().y - (top->GetBounds().y + top->GetBounds().height) - colSep->GetBounds().height;
    Check(std::fabs(gap - 20.0f) < 0.01f,
          "a vertical bar spaces its groups along its column (got " + std::to_string(gap) + ")");
}

void TestOverflowCut() {
    std::printf("ToolbarOverflowMode::Menu takes what does not fit off the end\n");

    // Items from x = 6 to 194; eight 30 px buttons with 4 px gaps need 268.
    auto toolbar = Bar(200);
    toolbar->SetOverflowMode(ToolbarOverflowMode::Menu);
    std::vector<std::shared_ptr<UltraCanvasButton>> buttons;
    for (int i = 0; i < 8; ++i) buttons.push_back(Button(toolbar, "b" + std::to_string(i)));
    auto chevron = toolbar->GetOverflowButton();
    Check(chevron != nullptr, "the Menu mode has a chevron button");
    if (!chevron) return;
    chevron->SetElementSize(Size2Df(30, 30));
    LayOut(toolbar, 200, 100);

    // The chevron and a gap leave 194 - 30 - 4 = 160: b0..b3 end at 36, 70,
    // 104, 138; b4 would end at 172.
    const std::vector<std::string> expected = { "b4", "b5", "b6", "b7" };
    Check(toolbar->GetOverflowItemIds() == expected, "b4 to b7 go into the menu");
    Check(buttons[3]->IsVisible() && !buttons[4]->IsVisible() && !buttons[7]->IsVisible(),
          "they are hidden from the bar, the ones that fit are not");
    Check(toolbar->IsItemInOverflowMenu("b5") && !toolbar->IsItemInOverflowMenu("b2"),
          "IsItemInOverflowMenu says which");
    Check(chevron->IsVisible() && Right(chevron) <= 194.5f && chevron->GetBounds().x >= Right(buttons[3]),
          "the chevron shows after the last item that fits, inside the bar");
    Check(buttons[0]->GetBounds().width == 30.0f, "nothing was squeezed to fit");
    Check(toolbar->GetItemCount() == 8, "the chevron is not an item");
    Check(toolbar->GetItemOrder().size() == 8 && toolbar->GetItemIndex("b7") == 7,
          "the items keep their places in the order");

    // Room for everything: the items come back and the chevron goes.
    Resize(toolbar, 400);
    Check(toolbar->GetOverflowItemIds().empty(), "a wider bar has nothing in the menu");
    Check(buttons[7]->IsVisible() && !chevron->IsVisible(), "every item is back, the chevron hidden");

    // An item the program hid is not the bar's to show.
    buttons[1]->SetVisible(false);
    Resize(toolbar, 200);
    Check(!toolbar->IsItemInOverflowMenu("b1"), "a hidden item is not put in the menu");
    Resize(toolbar, 400);
    Check(!buttons[1]->IsVisible(), "and stays hidden when the bar has room again");
    buttons[1]->SetVisible(true);

    // A button added later still comes before the chevron.
    auto late = Button(toolbar, "late");
    auto items = toolbar->GetItems();
    Check(!items.empty() && items.back() == late, "an item added later is the last item");
    Resize(toolbar, 200);
    Check(toolbar->IsItemInOverflowMenu("late"), "and goes into the menu with the others");

    // Back to another mode: everything on the bar, no chevron.
    toolbar->SetOverflowMode(ToolbarOverflowMode::Scroll);
    Resize(toolbar, 200);
    Check(toolbar->GetOverflowButton() == nullptr && buttons[6]->IsVisible(),
          "leaving the Menu mode puts every item back and drops the chevron");
    Check(toolbar->GetItemCount() == 9, "and leaves the items as they were");
}

void TestNoDanglingSeparator() {
    std::printf("No separator is left at the end of the bar\n");

    // b0 6..46, b1 50..90, b2 94..134, the line at 138, b3 143..183: b3 does
    // not fit before 160, and the separator before it would end the bar.
    auto toolbar = Bar(200);
    toolbar->SetOverflowMode(ToolbarOverflowMode::Menu);
    Button(toolbar, "b0", 40);
    Button(toolbar, "b1", 40);
    Button(toolbar, "b2", 40);
    auto sep = toolbar->AddSeparator("sep");
    Button(toolbar, "b3", 40);
    Button(toolbar, "b4", 40);
    toolbar->GetOverflowButton()->SetElementSize(Size2Df(30, 30));
    LayOut(toolbar, 200, 100);
    const std::vector<std::string> expected = { "sep", "b3", "b4" };
    Check(toolbar->GetOverflowItemIds() == expected, "the separator goes with the group after it");
    Check(!sep->IsVisible(), "and is off the bar");
    auto entries = toolbar->GetOverflowMenuItems();
    Check(!entries.empty() && entries.front().type != MenuItemType::Separator,
          "the menu does not start with a separator");

    toolbar->ClearItems();
    Check(toolbar->GetOverflowButton() != nullptr && toolbar->GetItemCount() == 0,
          "ClearItems keeps the chevron for the items to come, and leaves no item");
}

void TestMenuEntries() {
    std::printf("The menu stands in for each kind of item\n");

    // Items from 6 to 94, less the chevron and a gap: 60. Only the first
    // fits; the rest go into the menu.
    auto toolbar = Bar(100);
    toolbar->SetOverflowMode(ToolbarOverflowMode::Menu);
    Button(toolbar, "first");
    int clicks = 0;
    auto undo = toolbar->AddButton("undo", "", "", [&clicks]() { ++clicks; });
    undo->SetElementSize(Size2Df(30, 30));
    undo->SetTooltip("Undo Move (Ctrl+Z)");
    auto off = toolbar->AddButton("off", "", "", [&clicks]() { ++clicks; });
    off->SetElementSize(Size2Df(30, 30));
    off->SetTooltip("Nothing to do");
    off->SetDisabled(true);
    toolbar->AddSeparator("s1");
    toolbar->AddSeparator("s2");
    bool toggled = false;
    auto grid = toolbar->AddToggleButton("grid", "", "", [&toggled](bool on) { toggled = on; });
    grid->SetElementSize(Size2Df(30, 30));
    grid->SetTooltip("Grid");
    std::string picked;
    auto group = toolbar->AddToolGroup("shapes", {
            { "rect", "", "Rectangle (R)" },
            { "ellipse", "", "Ellipse (E)" } },
            [&picked](const std::string& id) { picked = id; });
    group->SetElementSize(Size2Df(30, 30));
    std::string chosen;
    auto zoom = toolbar->AddDropdownButton("zoom", "100%", { "50%", "100%", "200%" },
                                           [&chosen](const std::string& s) { chosen = s; });
    zoom->SetElementSize(Size2Df(60, 24));
    zoom->SetTooltip("Zoom");
    toolbar->AddLabel("caption", "Quality");
    toolbar->AddSeparator("trailing");
    toolbar->GetOverflowButton()->SetElementSize(Size2Df(30, 30));
    LayOut(toolbar, 100, 100);

    Check(!toolbar->IsItemInOverflowMenu("first") && toolbar->IsItemInOverflowMenu("undo"),
          "the bar keeps what fits and the menu has the rest");
    auto entries = toolbar->GetOverflowMenuItems();
    // undo, off, separator (one for two), grid, shapes, zoom - no label, no
    // trailing separator.
    Check(entries.size() == 6, "one entry per item a menu can stand in for (got " +
                                       std::to_string(entries.size()) + ")");
    if (entries.size() != 6) return;

    Check(entries[0].label == "Undo Move" && entries[0].shortcut == "Ctrl+Z",
          "a tooltip's \"(Ctrl+Z)\" becomes the entry's shortcut");
    entries[0].onClick();
    Check(clicks == 1, "the entry runs the button's onClick");
    Check(!entries[1].enabled, "a disabled button is a disabled entry");
    Check(entries[2].type == MenuItemType::Separator, "two separators in a row are one");
    Check(entries[3].type == MenuItemType::Checkbox && !entries[3].checked && entries[3].label == "Grid",
          "a toggle button is a check");
    entries[3].onToggle(true);
    Check(toggled && grid->IsPressed(), "checking it presses the button and fires onToggle");
    Check(entries[4].type == MenuItemType::Submenu && entries[4].subItems.size() == 2 &&
                  entries[4].subItems[1].label == "Ellipse" && entries[4].subItems[1].shortcut == "E",
          "a tool group is a submenu of its tools");
    entries[4].subItems[1].onClick();
    Check(picked == "ellipse" && group->GetCurrentItem() == "ellipse" && group->IsPressed(),
          "picking a tool there picks it in the group");
    Check(entries[5].type == MenuItemType::Submenu && entries[5].label == "Zoom" &&
                  entries[5].subItems.size() == 3 && entries[5].subItems[1].checked,
          "a dropdown is a submenu of its choices, the shown one checked");
    entries[5].subItems[2].onClick();
    Check(chosen == "200%" && zoom->GetSelectedIndex() == 2, "choosing one selects it in the dropdown");

    // The entries hold the items weakly: one removed while the menu is open
    // does nothing when picked.
    toolbar->RemoveItem("undo");
    undo.reset();
    entries[0].onClick();
    Check(clicks == 1, "an entry for an item removed meanwhile does nothing");
}

}  // namespace

int main() {
    TestGroupSpacing();
    TestOverflowCut();
    TestNoDanglingSeparator();
    TestMenuEntries();
    if (g_failures == 0) {
        std::printf("All toolbar group and overflow checks passed.\n");
        return 0;
    }
    std::printf("%d check(s) FAILED.\n", g_failures);
    return 1;
}
