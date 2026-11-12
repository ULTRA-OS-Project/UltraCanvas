// Apps/UltraFiler/UltraFilerTabStripStyle.h
// Settings > Display > Tab style, applied to any tab strip UltraFiler shows:
// the window's own strip, the three tabs of the History and Favorites views
// and the two of the Connection log window. One place holds the three
// colourways, so every strip in the application agrees, and every field a
// style sets is set by all three, so switching leaves nothing of the
// previous style behind.
// Version: 1.1.0 - the colours are the theme's roles
// Version: 1.0.0
// Last Modified: 2026-11-11
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasTheme.h"
#include "UltraFilerSettings.h"

#include <algorithm>

namespace UltraCanvas {

// The colours are the current theme's roles, so every strip is dark on a
// dark desktop; the window applies the style again when the theme changes.
inline void ApplyFilerTabStripStyle(UltraCanvasTabbedContainer& t, FilerTabStripStyle style) {
    const ThemeRoles& r = UltraCanvasTheme::Current().roles;
    const Color ink = r.ink;
    const Color mutedInk = r.inkMuted;
    const Color accent = r.accent;
    const Color strip = r.ground;                 // the light strip the tabs sit on
    const Color open = r.panel;                   // the open tab
    const Color hovered = r.hover;                // a tab under the pointer
    const Color capsuleHover = r.panel.WithAlpha(140);   // a capsule under the pointer
    switch (style) {
        case FilerTabStripStyle::Modern:
            // Capsules floating in a blue-grey strip: the open tab white with
            // the accent outline, the others text only until hovered. The
            // capsule is 24px tall whatever the strip's height (30px in the
            // window, 28px in the Connection log).
            t.SetTabStyle(TabStyle::Pill);
            t.SetPillInset(2, std::max(2, (t.GetTabHeight() - 24) / 2));
            t.SetTabBarColor(r.well);
            t.SetActiveTabBackgroundColor(open);
            t.SetActiveTabBorderColor(accent);
            t.SetActiveTabTextColor(ink);
            t.SetInactiveTabBackgroundColor(Colors::Transparent);
            t.SetInactiveTabTextColor(mutedInk);
            t.SetHoveredTabBackgroundColor(capsuleHover);
            t.SetCloseButtonColor(mutedInk);
            t.SetCloseButtonHoverColor(ink);
            t.SetNewTabButtonShape(NewTabButtonShape::Circle);
            t.newTabButtonHoverColor = capsuleHover;
            t.newTabButtonIconColor = mutedInk;
            break;
        case FilerTabStripStyle::SimpleModern:
            // Flat tabs on the light strip, the open one white with the
            // accent line under it.
            t.SetTabStyle(TabStyle::Modern);
            t.SetTabBarColor(strip);
            t.SetActiveTabBackgroundColor(open);
            t.SetActiveTabIndicatorColor(accent);
            t.SetActiveTabTextColor(ink);
            t.SetInactiveTabBackgroundColor(Colors::Transparent);
            t.SetInactiveTabTextColor(mutedInk);
            t.SetHoveredTabBackgroundColor(hovered);
            t.SetCloseButtonColor(mutedInk);
            t.SetCloseButtonHoverColor(ink);
            t.SetNewTabButtonShape(NewTabButtonShape::RoundedSquare);
            t.newTabButtonHoverColor = hovered;
            t.newTabButtonIconColor = mutedInk;
            break;
        case FilerTabStripStyle::Classic:
        default:
            // The strips as every release before 1.71.0 drew them: the
            // framework's rounded tabs in its default colours.
            t.SetTabStyle(TabStyle::Rounded);
            t.SetTabBarColor(strip);
            t.SetActiveTabBackgroundColor(open);
            t.SetActiveTabTextColor(ink);
            t.SetInactiveTabBackgroundColor(r.raised);
            t.SetInactiveTabTextColor(mutedInk);
            t.SetHoveredTabBackgroundColor(r.accentSoft);
            t.SetCloseButtonColor(mutedInk);
            t.SetCloseButtonHoverColor(r.danger);
            t.SetNewTabButtonShape(NewTabButtonShape::RoundedSquare);
            t.newTabButtonHoverColor = hovered;
            t.newTabButtonIconColor = mutedInk;
            break;
    }
    t.InvalidateTabbar();
}

} // namespace UltraCanvas
