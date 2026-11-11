// Tests/UIWidgetsTest.cpp
// The four elements the store window needed and the framework lacked
// (2026-10-24): a chip group with one selection rule, a card with real
// button children, a collapsible panel and a sidebar navigation. Headless:
// the elements are laid out by the CSSLayout engine and fed events, no
// window. Where an element needs a window (focus, tooltips) the test stays
// on the side that does not.
// Version: 1.0.0
// Author: UltraCanvas Framework
#include "UltraCanvasCard.h"
#include "UltraCanvasChipGroup.h"
#include "UltraCanvasCollapsiblePanel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasSidebarNavigation.h"
#include "CSSLayout/CSSLayout.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("  %s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
    if (!ok) ++g_failures;
}

void CheckNear(float actual, float expected, const std::string& what, float tolerance = 0.5f) {
    const bool ok = std::fabs(actual - expected) <= tolerance;
    std::printf("  %s %s = %.1f (expected %.1f)\n", ok ? "ok:  " : "FAIL:", what.c_str(), actual, expected);
    if (!ok) ++g_failures;
}

// Lays an element out as the only child of a w x h column (or row, for an
// element that keeps its own width, like the sidebar in a window's row).
std::shared_ptr<UltraCanvasContainer> LayOut(const std::shared_ptr<UltraCanvasUIElement>& element, float w = 600, float h = 600,
                                             bool row = false) {
    auto root = std::make_shared<UltraCanvasContainer>("root", 0, 0, w, h);
    if (row) root->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    else root->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    root->AddChild(element);
    CSSLayout::LayoutContext ctx;
    ctx.viewportWidth = w;
    ctx.viewportHeight = h;
    CSSLayout::MeasureConstraints mc{ { CSSLayout::ConstraintMode::Exact, w }, { CSSLayout::ConstraintMode::Exact, h } };
    root->Measure(mc, ctx);
    root->Arrange(Rect2Df{ 0, 0, w, h }, ctx);
    return root;
}

UCEvent Mouse(UCEventType type, float x, float y) {
    UCEvent e;
    e.type = type;
    e.button = UCMouseButton::Left;
    e.pointer.x = static_cast<int>(x);
    e.pointer.y = static_cast<int>(y);
    return e;
}

void Click(UltraCanvasUIElement& element, float x, float y) {
    element.OnEvent(Mouse(UCEventType::MouseDown, x, y));
    element.OnEvent(Mouse(UCEventType::MouseUp, x, y));
}

void TestChipGroup() {
    std::printf("chip group: one selection rule over real chips\n");
    auto group = CreateChipGroup("cats");
    int fired = -2;
    std::vector<int> firedIndices;
    group->onSelectionChanged = [&](int i) { fired = i; };
    group->onSelectedIndicesChanged = [&](const std::vector<int>& v) { firedIndices = v; };
    Check(group->AddChip("All") == 0 && group->AddChip("Video") == 1 && group->AddChip("Audio", "") == 2,
          "three chips, indices 0..2");
    Check(group->GetChipCount() == 3 && group->GetSelectedIndex() == -1, "nothing selected at first");
    Check(group->GetChip(1)->IsSelectable() && group->GetChip(1)->GetLabel() == "Video", "chips are selectable and labelled");
    Check(group->IndexOf("Audio") == 2 && group->IndexOf("Nope") == -1, "IndexOf by label");

    group->SetSelectedIndex(1);
    Check(group->GetSelectedIndex() == 1 && fired == 1, "SetSelectedIndex selects and fires");
    // The user clicks another chip: the chip selects itself, the group deselects the rest.
    group->GetChip(2)->SetSelected(true);
    Check(group->GetSelectedIndex() == 2 && !group->IsSelected(1) && fired == 2, "selecting a chip deselects the other");
    // Clicking the selected chip again keeps it (no selection is not allowed by default).
    group->GetChip(2)->SetSelected(false);
    Check(group->GetSelectedIndex() == 2 && group->GetChip(2)->IsSelected(), "the selected chip stays selected");
    group->SetAllowNoSelection(true);
    group->GetChip(2)->SetSelected(false);
    Check(group->GetSelectedIndex() == -1 && fired == -1, "with AllowNoSelection a second click clears");

    group->SetSelectionMode(ChipGroupSelectionMode::Multiple);
    group->GetChip(0)->SetSelected(true);
    group->GetChip(2)->SetSelected(true);
    Check(group->GetSelectedIndices() == std::vector<int>{0, 2} && firedIndices == std::vector<int>{0, 2},
          "multiple mode keeps both");
    group->SetSelectionMode(ChipGroupSelectionMode::Single);
    Check(group->GetSelectedIndices() == std::vector<int>{0}, "back to single keeps the first");

    group->RemoveChip(0);
    Check(group->GetChipCount() == 2 && group->GetSelectedIndex() == -1 && group->IndexOf("Video") == 0, "RemoveChip renumbers and clears");

    // Laid out: the chips sit in a row, each as wide as its content.
    auto root = LayOut(group, 400, 100);
    const Rect2Df a = group->GetChip(0)->GetBounds(), b = group->GetChip(1)->GetBounds();
    Check(a.width > 0 && b.x > a.x + a.width - 1, "chips laid out left to right");
    Check(std::fabs(a.y - b.y) < 0.5f, "on one row");
    (void)root;
}

void TestCard() {
    std::printf("card: header, content, actions, a clickable surface\n");
    auto card = CreateCard("tile", "UltraVideo", "The video editor");
    Check(card->GetTitle() == "UltraVideo" && card->GetSubtitle() == "The video editor", "title and subtitle");
    int surfaceClicks = 0, buttonClicks = 0;
    card->onClick = [&] { ++surfaceClicks; };
    auto button = card->AddActionButton("tile.install", "Install", [&] { ++buttonClicks; });
    Check(button != nullptr && card->GetActions()->IsVisible(), "an action button shows the actions row");
    Check(!card->GetContent()->IsVisible(), "no content yet: the content area is hidden");
    card->AddContent(CreateLabel("tile.summary", 0, 0, 0, 40, "Cut and trim"));
    Check(card->GetContent()->IsVisible(), "content shows the content area");

    auto root = LayOut(card, 400, 600);
    const Rect2Df cb = card->GetBounds();
    Check(cb.height > 60 && cb.height < 200, "the card is as tall as its parts (" + std::to_string(int(cb.height)) + ")");
    const Rect2Df headerB = card->GetTitleLabel()->GetBounds();
    const Rect2Df buttonB = button->GetBounds();
    Check(headerB.width > 0, "the title has a width");
    Check(buttonB.width >= 72 && buttonB.height > 0, "the button keeps its minimum width");
    // The button is right-aligned in the actions row.
    const Rect2Df actionsB = card->GetActions()->GetBounds();
    Check(buttonB.x + buttonB.width >= actionsB.width - 1, "the action is right-aligned");

    // A press on the surface (not on a child) is a click of the card.
    Check(card->IsClickable(), "a card with onClick is clickable");
    Click(*card, 5, 5);
    Check(surfaceClicks == 1, "press + release on the surface fires onClick");
    card->OnEvent(Mouse(UCEventType::MouseDown, 5, 5));
    card->OnEvent(Mouse(UCEventType::MouseUp, 1000, 1000));
    Check(surfaceClicks == 1, "a release outside does not");
    card->onClick = nullptr;
    card->SetClickable(false);
    Check(!card->IsClickable(), "without onClick and clickable off, no hand cursor");

    card->SetHighlighted(true);
    Check(card->IsHighlighted() && !card->IsSelected(), "highlighted");
    card->SetSelected(true);
    Check(card->IsSelected(), "selected");
    card->SetSubtitle("");
    Check(!card->GetSubtitleLabel()->IsVisible(), "an empty subtitle hides its line");
    card->ClearActions();
    Check(!card->GetActions()->IsVisible(), "no actions: the row is hidden");
    (void)root;
}

void TestCollapsiblePanel() {
    std::printf("collapsible panel: folds to its header, header buttons stay buttons\n");
    auto panel = CreateCollapsiblePanel("downloads", "Downloads");
    panel->SetDetail("2 running");
    int toggles = 0;
    bool last = true;
    panel->onExpandedChanged = [&](bool e) { ++toggles; last = e; };
    int clears = 0;
    auto clear = panel->AddHeaderButton("downloads.clear", "Clear", [&] { ++clears; });
    panel->GetBody()->AddChild(CreateLabel("downloads.row", 0, 0, 0, 24, "ultravideo-0.2.1.7z"));
    panel->SetExpandedHeight(200);

    auto root = LayOut(panel, 500, 600);
    const float headerH = panel->GetStyle().headerHeight;
    CheckNear(panel->GetBounds().height, 200, "expanded: the expanded height");
    Check(panel->GetBody()->IsVisible(), "the body shows");
    Check(clear->GetBounds().width >= 64, "the header button is laid out");
    const Rect2Df hr = panel->HeaderRect();
    CheckNear(hr.height, headerH, "the header strip is headerHeight tall");

    // A press on the header strip (not on the button) folds the panel.
    panel->OnEvent(Mouse(UCEventType::MouseDown, hr.x + hr.width / 2, hr.y + hr.height / 2));
    Check(!panel->IsExpanded() && toggles == 1 && !last, "a header press collapses");
    root = LayOut(panel, 500, 600);   // a new root before the old one goes: the panel's parent stays valid
    CheckNear(panel->GetBounds().height, headerH + 2 * panel->GetStyle().borderWidth, "collapsed: header only");
    Check(!panel->GetBody()->IsVisible(), "the body is hidden");
    panel->Toggle();
    Check(panel->IsExpanded() && toggles == 2, "Toggle expands again");
    Check(clears == 0, "the header button was never clicked by the fold");

    // Not collapsible: the header press does nothing.
    panel->SetCollapsible(false);
    panel->OnEvent(Mouse(UCEventType::MouseDown, hr.x + hr.width / 2, hr.y + hr.height / 2));
    Check(panel->IsExpanded() && toggles == 2, "not collapsible: the header is inert");
    panel->SetCollapsible(true);

    // Dragging the top edge of a bottom-docked panel resizes it.
    float reported = 0;
    panel->onExpandedHeightChanged = [&](float h) { reported = h; };
    root = LayOut(panel, 500, 600);
    panel->OnEvent(Mouse(UCEventType::MouseDown, 100, 2));
    panel->OnEvent(Mouse(UCEventType::MouseMove, 100, -50));
    panel->OnEvent(Mouse(UCEventType::MouseUp, 100, -50));
    CheckNear(panel->GetExpandedHeight(), 252, "dragged 50 px up: 50 px taller", 1.0f);
    CheckNear(reported, 252, "the new height is reported", 1.0f);
    panel->SetExpandedHeight(10);
    CheckNear(panel->GetExpandedHeight(), panel->GetStyle().minExpandedHeight, "never below the minimum");
    panel->SetExpandedHeight(0);
    Check(panel->GetExpandedHeight() == 0.0f, "0 means as tall as the body");
    (void)root;
}

void TestSidebar() {
    std::printf("sidebar navigation: sections, items, selection, badges, compact\n");
    auto nav = CreateSidebarNavigation("nav");
    std::string selected = "none";
    int selections = 0;
    nav->onSelectionChanged = [&](const std::string& id) { selected = id; ++selections; };
    nav->AddItem("discover", "Discover", "");
    const int lib = nav->AddSection("Library");
    nav->AddItem(lib, "installed", "Installed", "");
    nav->AddItem(lib, "updates", "Updates", "");
    nav->AddItem(lib, "disabled", "Nothing here", "");
    nav->SetItemEnabled("disabled", false);
    Check(nav->GetSectionCount() == 2 && nav->GetItemCount() == 4, "an untitled section before the titled one");
    Check(nav->GetItemIds() == std::vector<std::string>{"discover", "installed", "updates", "disabled"}, "display order");
    Check(nav->AddItem("installed", "Installed apps", "") == "installed" && nav->GetItemCount() == 4,
          "the same id replaces in place");
    Check(nav->FindItem("installed")->label == "Installed apps", "the replaced label");

    Check(nav->Select("updates") && selected == "updates" && selections == 1, "Select fires once");
    Check(nav->Select("updates") && selections == 1, "selecting the selected item fires nothing");
    Check(!nav->Select("disabled") && nav->GetSelectedId() == "updates", "a disabled item cannot be selected");
    Check(!nav->Select("missing"), "an unknown id cannot be selected");
    Check(nav->SelectPrevious() && nav->GetSelectedId() == "installed", "Previous");
    Check(nav->SelectPrevious() && nav->GetSelectedId() == "discover", "Previous across the section header");
    Check(!nav->SelectPrevious() && nav->GetSelectedId() == "discover", "at the top Previous stays");
    Check(nav->SelectNext() && nav->SelectNext() && nav->GetSelectedId() == "updates", "Next twice");
    Check(!nav->SelectNext() && nav->GetSelectedId() == "updates", "Next skips the disabled last item and stays");

    nav->SetBadge("updates", 3);
    Check(nav->FindItem("updates")->badgeText == "3", "a count badge");
    nav->SetBadge("updates", 0);
    Check(nav->FindItem("updates")->badgeText.empty(), "0 clears it");

    // Geometry: rows stack, the section header takes its own row.
    auto root = LayOut(nav, 300, 600, true);
    const SidebarStyle& st = nav->GetStyle();
    const Rect2Df discover = nav->ItemRect("discover"), installed = nav->ItemRect("installed");
    CheckNear(discover.y, st.paddingV, "the first item starts after the top padding");
    CheckNear(installed.y, st.paddingV + st.itemHeight + st.sectionGap + st.sectionHeight, "the next section's first item below its header");
    CheckNear(nav->ContentHeight(), st.paddingV * 2 + st.itemHeight * 4 + st.sectionGap + st.sectionHeight, "content height");
    CheckNear(nav->GetBounds().width, st.expandedWidth, "the expanded width");

    // A click selects (no window: the focus request is refused, the selection is not).
    Click(*nav, discover.x + 20, discover.y + discover.height / 2);
    Check(nav->GetSelectedId() == "discover", "a click selects the item under it");
    Click(*nav, 20, nav->ItemRect("disabled").y + 5);
    Check(nav->GetSelectedId() == "discover", "a click on a disabled item changes nothing");

    bool compactReported = false;
    nav->onCompactChanged = [&](bool c) { compactReported = c; };
    nav->SetCompact(true);
    Check(nav->IsCompact() && compactReported, "compact mode reported");
    root = LayOut(nav, 300, 600, true);
    CheckNear(nav->GetBounds().width, st.compactWidth, "the compact width");
    Check(nav->ItemRect("installed").y < installed.y, "compact: the section header shrinks to a separator");

    Check(nav->RemoveItem("updates") && nav->GetItemCount() == 3 && !nav->RemoveItem("updates"), "RemoveItem");
    nav->Clear();
    Check(nav->GetItemCount() == 0 && nav->GetSelectedId().empty() && selected.empty(), "Clear empties and reports no selection");
    (void)root;
}

} // namespace

int main() {
    TestChipGroup();
    TestCard();
    TestCollapsiblePanel();
    TestSidebar();
    if (g_failures) { std::printf("\n%d failure(s)\n", g_failures); return 1; }
    std::printf("\nall UI widget checks passed\n");
    return 0;
}
