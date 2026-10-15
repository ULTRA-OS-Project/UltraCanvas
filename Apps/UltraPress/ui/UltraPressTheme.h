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
// The look is remembered in the program's settings folder
// (<settings folder>/UltraPress/look), one word: "dark" or "light".
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

// Which look the user chose last, in `settingsDir` (the program's folder
// under the settings folder); false - the light look - when nothing was
// chosen yet or the folder cannot be read.
bool LoadLook(const std::string& settingsDir);
// Remembers the look; false when the folder cannot be written.
bool SaveLook(const std::string& settingsDir, bool dark);
// <settings folder>/UltraPress, empty without a home.
std::string LookSettingsDirectory();

// A label coloured by the theme for its part, now and on every change.
void FollowTheme(const std::shared_ptr<UltraCanvas::UltraCanvasLabel>& label, UltraCanvas::LabelVariant variant);
// A button coloured by the theme for its part, now and on every change.
void FollowTheme(const std::shared_ptr<UltraCanvas::UltraCanvasButton>& button, UltraCanvas::ButtonVariant variant);

} // namespace UltraPress
