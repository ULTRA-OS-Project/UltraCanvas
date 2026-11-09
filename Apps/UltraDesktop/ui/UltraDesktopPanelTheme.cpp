// Apps/UltraDesktop/ui/UltraDesktopPanelTheme.cpp
// The panels' colours in the two themes. See UltraDesktopPanelTheme.h.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraDesktopPanelTheme.h"

using namespace UltraCanvas;

namespace UltraDesktop {
namespace {

PanelPalette MakeDark() {
    PanelPalette p;
    p.light = false;
    p.panel = Color(31, 31, 31);
    p.inset = Color(20, 20, 20);
    p.raised = Color(46, 46, 46);
    p.raisedHover = Color(58, 58, 58);
    p.edge = Color(74, 74, 74);
    p.text = Color(242, 242, 242);
    p.muted = Color(160, 160, 160);
    p.error = Color(255, 140, 120);
    p.link = Color(147, 197, 253);
    p.linkHover = Color(191, 219, 254);
    p.row = Color(44, 44, 44);
    p.rowHighlighted = Color(30, 64, 110);
    p.tile = Color(43, 43, 46);
    p.tileHover = Color(66, 66, 72);
    p.pictureGround = Color(22, 22, 24);
    p.active = Color(96, 156, 232);
    return p;
}

PanelPalette MakeLight() {
    PanelPalette p;
    p.light = true;
    p.panel = Color(248, 248, 250);
    p.inset = Color(255, 255, 255);
    p.raised = Color(236, 236, 240);
    p.raisedHover = Color(222, 222, 228);
    p.edge = Color(200, 200, 206);
    p.text = Color(30, 30, 32);
    p.muted = Color(96, 96, 104);
    p.error = Color(185, 40, 28);
    p.link = Color(30, 100, 200);
    p.linkHover = Color(20, 80, 170);
    p.row = Color(236, 236, 240);
    p.rowHighlighted = Color(206, 224, 250);
    p.tile = Color(236, 236, 240);
    p.tileHover = Color(220, 222, 230);
    p.pictureGround = Color(255, 255, 255);
    p.active = Color(40, 110, 220);
    return p;
}

const PanelPalette kDarkPanels = MakeDark();
const PanelPalette kLightPanels = MakeLight();
bool gLight = false;   // the UI thread's: set by the desktop window, read by the panels

} // namespace

void SetPanelTheme(const std::string& theme) {
    gLight = theme == "light";
}

const PanelPalette& PanelColors() {
    return gLight ? kLightPanels : kDarkPanels;
}

} // namespace UltraDesktop
