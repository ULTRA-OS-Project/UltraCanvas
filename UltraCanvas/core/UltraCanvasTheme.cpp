// core/UltraCanvasTheme.cpp
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#include "UltraCanvasTheme.h"

namespace UltraCanvas {

namespace {
UltraCanvasTheme& CurrentStorage() {
    static UltraCanvasTheme current = UltraCanvasTheme::Light();
    return current;
}
}

UltraCanvasTheme UltraCanvasTheme::Light() {
    UltraCanvasTheme t;
    t.name = "Light";
    t.dark = false;
    ThemeRoles& r = t.roles;
    r.ground = Colors::WindowBackground;
    r.panel = Colors::White;
    r.well = Color(248, 248, 248);
    r.raised = Color(250, 250, 250);
    r.hover = Colors::SelectionHover;
    r.pressed = Color(204, 228, 247);
    r.line = Color(222, 222, 222);
    r.lineStrong = Colors::ButtonShadow;
    r.ink = Colors::TextDefault;
    r.inkMuted = Color(90, 90, 90);
    r.inkFaint = Colors::TextDisabled;
    r.accent = Colors::Selection;
    r.accentHover = Color(16, 110, 190);
    r.accentSoft = Colors::SelectionHover;
    r.onAccent = Colors::White;
    r.focus = Colors::Selection;
    r.danger = Color(196, 43, 28);
    return t;
}

UltraCanvasTheme UltraCanvasTheme::Dark() {
    UltraCanvasTheme t;
    t.name = "Dark";
    t.dark = true;
    ThemeRoles& r = t.roles;
    r.ground = Color(24, 24, 24);
    r.panel = Color(30, 30, 30);
    r.well = Color(36, 36, 36);
    r.raised = Color(44, 44, 44);
    r.hover = Color(58, 58, 58);
    r.pressed = Color(70, 70, 70);
    r.line = Color(52, 52, 52);
    r.lineStrong = Color(120, 120, 120);
    r.ink = Color(235, 235, 235);
    r.inkMuted = Color(190, 190, 190);
    r.inkFaint = Color(150, 150, 150);
    r.accent = Color(77, 166, 255);
    r.accentHover = Color(102, 180, 255);
    r.accentSoft = Color(36, 62, 88);
    r.onAccent = Color(10, 20, 30);
    r.focus = Color(77, 166, 255);
    r.danger = Color(255, 111, 97);
    return t;
}

const UltraCanvasTheme& UltraCanvasTheme::Current() { return CurrentStorage(); }

void UltraCanvasTheme::SetCurrent(const UltraCanvasTheme& theme) { CurrentStorage() = theme; }

} // namespace UltraCanvas
