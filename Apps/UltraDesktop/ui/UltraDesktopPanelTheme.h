// Apps/UltraDesktop/ui/UltraDesktopPanelTheme.h
// The colours of the panels and windows the bars open - the clock, volume,
// Wi-Fi, Bluetooth, device and clipboard panels and the window previews -
// in the desktop's theme (DesktopSettings::theme): dark grey with light
// text, or near white with dark text. A panel reads them when it builds
// itself, which it does every time it opens, so a theme change shows the
// next time a panel opens. The desktop window sets the theme
// (SetPanelTheme) whenever it builds its bars.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasCommonTypes.h"

#include <string>

namespace UltraDesktop {

struct PanelPalette {
    bool light = false;
    UltraCanvas::Color panel;            // the panel's ground
    UltraCanvas::Color inset;            // a field set into it (the search box)
    UltraCanvas::Color raised;           // a button on it
    UltraCanvas::Color raisedHover;
    UltraCanvas::Color edge;             // borders, the footer's rule
    UltraCanvas::Color text;
    UltraCanvas::Color muted;            // secondary text
    UltraCanvas::Color error;
    UltraCanvas::Color link;             // a text-only button (Undo)
    UltraCanvas::Color linkHover;
    UltraCanvas::Color row;              // a list row (a network, a device)
    UltraCanvas::Color rowHighlighted;   // the joined network, the connected device
    UltraCanvas::Color tile;             // a window preview's tile
    UltraCanvas::Color tileHover;
    UltraCanvas::Color pictureGround;    // behind a window's picture
    UltraCanvas::Color active;           // the active window's frame
};

// "dark" or "light"; anything else is dark.
void SetPanelTheme(const std::string& theme);
const PanelPalette& PanelColors();

} // namespace UltraDesktop
