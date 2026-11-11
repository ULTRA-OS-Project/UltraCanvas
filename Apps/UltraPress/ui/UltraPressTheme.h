// Apps/UltraPress/ui/UltraPressTheme.h
// UltraPress's look as a framework theme: the brand's red accent, its ink
// and paper, on the framework's light or dark preset, so that every
// element of the window and the dialogs that follows the theme
// (UltraCanvasUIElement::SetFollowTheme) takes its colours from one place
// and switches with it. The side column keeps the brand's night blue in
// both looks, as a web builder's admin column does - its roles are the
// theme's own ("press.side", "press.on-side", ...), read where the column
// is built. The page in the middle is the site's, in the site's colours,
// and does not follow.
//
// The look is remembered with the program's other settings
// (UltraPressSettings.h).
// Version: 0.8.4 - the look's file is UltraPressSettings' now
// Version: 0.8.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTheme.h"

#include <memory>
#include <string>

namespace UltraPress {

// The theme for the light or the dark look.
UltraCanvas::UltraCanvasTheme ThemeFor(bool dark);

// The colours of the side column, from the theme's own roles.
struct SideColors {
    UltraCanvas::Color panel, ink, inkStrong, accent, hover, active, strong;
};
SideColors SideColorsOf(const UltraCanvas::UltraCanvasTheme& theme);

// A label coloured by the theme for its part, now and on every change.
void FollowTheme(const std::shared_ptr<UltraCanvas::UltraCanvasLabel>& label, UltraCanvas::LabelVariant variant);
// A button coloured by the theme for its part, now and on every change.
void FollowTheme(const std::shared_ptr<UltraCanvas::UltraCanvasButton>& button, UltraCanvas::ButtonVariant variant);

} // namespace UltraPress
