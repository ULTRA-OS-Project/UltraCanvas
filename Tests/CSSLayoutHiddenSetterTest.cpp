// Tests/CSSLayoutHiddenSetterTest.cpp
// A flex or grid setter on a hidden box keeps it hidden.
//
// The case this pins is UltraTexter's Markdown side bar. The application hides
// it on a plain-text tab, and on a theme change restyles it with
// UltraCanvasToolbar::SetAppearance, which sets the bar's flex gap. Every flex
// setter used to set display: flex outright, so the restyle un-hid the bar on
// a tab it does not belong to. In CSS, setting gap or flex-direction never
// undoes display: none; the setter changes the kind of box the element comes
// back as when it is shown again, and that is what is tested here.
//
// No UI stack: struct Layout alone.
//
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "CSSLayout/CSSLayout.h"

#include <cstdio>

using namespace UltraCanvas::CSSLayout;

static int g_failures = 0;

#define CHECK(cond, msg)                                                 \
    do {                                                                 \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
        else std::printf("  ok: %s\n", msg);                             \
    } while (0)

int main() {
    std::printf("CSSLayoutHiddenSetterTest\n");

    {   // The toolbar case: a flex column, hidden, then its gap set.
        Layout L;
        L.SetFlexColumn().SetFlexGap(4.0f);
        L.Hide();
        L.SetFlexGap(6.0f);
        CHECK(L.display == DisplayType::NoDisplay, "SetFlexGap on a hidden flex box keeps it hidden");
        L.Show();
        CHECK(L.display == DisplayType::Flex, "shown again it is a flex box");
    }

    {   // Every flex setter, on a box hidden while it was a block.
        Layout L;
        L.Hide();
        L.SetFlexRow();
        L.SetFlexAlignItems(AlignItems::Center);
        L.SetFlexJustifyContent(JustifyContent::Center);
        L.SetFlexWrap(FlexWrap::Wrap);
        CHECK(L.display == DisplayType::NoDisplay, "flex setters on a hidden block keep it hidden");
        L.Show();
        CHECK(L.display == DisplayType::Flex, "shown again it is the flex box it was made into");
    }

    {   // Grid and table setters alike.
        Layout grid;
        grid.Hide();
        grid.SetGridGap(3.0f);
        CHECK(grid.display == DisplayType::NoDisplay, "SetGridGap on a hidden box keeps it hidden");
        grid.Show();
        CHECK(grid.display == DisplayType::Grid, "shown again it is a grid");

        Layout table;
        table.Hide();
        table.SetTable().SetTableSpacing(2.0f, 2.0f);
        CHECK(table.display == DisplayType::NoDisplay, "SetTable on a hidden box keeps it hidden");
        table.Show();
        CHECK(table.display == DisplayType::Table, "shown again it is a table");
    }

    {   // A visible box is changed as before.
        Layout L;
        CHECK(L.display == DisplayType::Block, "a new box is a block");
        L.SetFlexGap(2.0f);
        CHECK(L.display == DisplayType::Flex, "SetFlexGap on a visible block makes it a flex box");
        L.SetTable().SetTableSpacing(1.0f, 1.0f);
        CHECK(L.display == DisplayType::Table, "SetTableSpacing keeps a visible table a table");
    }

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASSED" : "FAILED",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
