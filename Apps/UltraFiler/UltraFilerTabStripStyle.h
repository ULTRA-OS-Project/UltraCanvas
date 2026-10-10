// Apps/UltraFiler/UltraFilerTabStripStyle.h
// Settings > Display > Tab style, applied to any tab strip UltraFiler shows:
// the window's own strip, the three tabs of the History and Favorites views
// and the two of the Connection log window. One place holds the three
// colourways, so every strip in the application agrees, and every field a
// style sets is set by all three, so switching leaves nothing of the
// previous style behind.
// Version: 1.0.0
// Last Modified: 2026-10-10
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasTabbedContainer.h"
#include "UltraFilerSettings.h"

#include <algorithm>

namespace UltraCanvas {

inline void ApplyFilerTabStripStyle(UltraCanvasTabbedContainer& t, FilerTabStripStyle style) {
    const Color ink(30, 37, 46, 255);
    const Color mutedInk(84, 96, 112, 255);
    const Color accent(96, 146, 224, 255);
    switch (style) {
        case FilerTabStripStyle::Modern:
            // Capsules floating in a blue-grey strip: the open tab white with
            // the accent outline, the others text only until hovered. The
            // capsule is 24px tall whatever the strip's height (30px in the
            // window, 28px in the Connection log).
            t.SetTabStyle(TabStyle::Pill);
            t.SetPillInset(2, std::max(2, (t.GetTabHeight() - 24) / 2));
            t.SetTabBarColor(Color(229, 234, 241, 255));
            t.SetActiveTabBackgroundColor(Colors::White);
            t.SetActiveTabBorderColor(accent);
            t.SetActiveTabTextColor(ink);
            t.SetInactiveTabBackgroundColor(Colors::Transparent);
            t.SetInactiveTabTextColor(mutedInk);
            t.SetHoveredTabBackgroundColor(Color(255, 255, 255, 140));
            t.SetCloseButtonColor(mutedInk);
            t.SetCloseButtonHoverColor(ink);
            t.SetNewTabButtonShape(NewTabButtonShape::Circle);
            t.newTabButtonHoverColor = Color(255, 255, 255, 140);
            t.newTabButtonIconColor = mutedInk;
            break;
        case FilerTabStripStyle::SimpleModern:
            // Flat tabs on the light strip, the open one white with the
            // accent line under it.
            t.SetTabStyle(TabStyle::Modern);
            t.SetTabBarColor(Color(249, 249, 251, 255));
            t.SetActiveTabBackgroundColor(Colors::White);
            t.SetActiveTabIndicatorColor(accent);
            t.SetActiveTabTextColor(ink);
            t.SetInactiveTabBackgroundColor(Colors::Transparent);
            t.SetInactiveTabTextColor(mutedInk);
            t.SetHoveredTabBackgroundColor(Color(236, 237, 241, 255));
            t.SetCloseButtonColor(mutedInk);
            t.SetCloseButtonHoverColor(ink);
            t.SetNewTabButtonShape(NewTabButtonShape::RoundedSquare);
            t.newTabButtonHoverColor = Color(228, 228, 232, 255);
            t.newTabButtonIconColor = mutedInk;
            break;
        case FilerTabStripStyle::Classic:
        default:
            // The strips as every release before 1.71.0 drew them: the
            // framework's rounded tabs in its default colours.
            t.SetTabStyle(TabStyle::Rounded);
            t.SetTabBarColor(Color(249, 249, 251, 255));
            t.SetActiveTabBackgroundColor(Colors::White);
            t.SetActiveTabTextColor(Colors::Black);
            t.SetInactiveTabBackgroundColor(Color(236, 236, 236, 255));
            t.SetInactiveTabTextColor(Color(80, 80, 80, 255));
            t.SetHoveredTabBackgroundColor(Color(240, 240, 255, 255));
            t.SetCloseButtonColor(Color(120, 120, 120, 255));
            t.SetCloseButtonHoverColor(Color(200, 50, 50, 255));
            t.SetNewTabButtonShape(NewTabButtonShape::RoundedSquare);
            t.newTabButtonHoverColor = Color(228, 228, 232, 255);
            t.newTabButtonIconColor = Color(100, 100, 100, 255);
            break;
    }
    t.InvalidateTabbar();
}

} // namespace UltraCanvas
