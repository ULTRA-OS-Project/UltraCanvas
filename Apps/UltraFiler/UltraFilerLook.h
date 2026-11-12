// Apps/UltraFiler/UltraFilerLook.h
// The filer's own surfaces from the theme (T6 of the host theme proposal):
// every colour the window, its dialogs and its tab strips set by hand is
// a role of the current theme read here, so a dark desktop - or Dark on
// UOS-Settings' Look page - gives a dark filer, and a change while it
// runs is taken at once (UltraFilerWindow::ApplyLook). The elements that
// follow the theme by themselves (the file displays, the tree, the
// dropdowns, the menus, the dialogs' buttons) need nothing from here; the
// look is for the containers and the buttons the filer styles itself.
//
//   const FilerLook look = Look();
//   row->SetBackgroundColor(look.ground);
//
// Version: 1.0.0
// Last Modified: 2026-11-11
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasTheme.h"

#include <algorithm>

namespace UltraCanvas {

struct FilerLook {
    Color ground;        // the window, the tool rows, the folder tree: the theme's ground
    Color panel;         // a page, a list, a search field: the panel
    Color statusBar;     // the status strip: the well
    Color button;        // a tool button at rest: the raised surface
    Color hover;         // under the pointer
    Color pressed;       // held down
    Color active;        // a toggle that is on: the soft accent
    Color activeHover;   // ... under the pointer: the pressed fill
    Color activeBorder;  // ... its outline: the accent
    Color ink;           // text, and the icons drawn as masks
    Color inkMuted;      // captions, the status line
    Color inkFaint;      // a separator, a mount path
    Color line;          // hairlines between the bars, a field's border
    Color border;        // a button's thin outline: the line
    Color accent;        // the progress gauge, the scan button
    Color accentHover;
    Color onAccent;      // text on the scan button
    Color danger;        // an error in the status line, the Stop button, the alarm badge
    Color dangerHover;
    Color freeSlice;     // the free part of a volume's pie: the line colour

    // A colour moved toward white (by > 0) or black (by < 0).
    static Color Shade(const Color& c, int by) {
        auto step = [by](uint8_t v) { return static_cast<uint8_t>(std::max(0, std::min(255, static_cast<int>(v) + by))); };
        return Color(step(c.r), step(c.g), step(c.b), c.a);
    }

    static FilerLook FromTheme(const UltraCanvasTheme& theme) {
        const ThemeRoles& r = theme.roles;
        FilerLook l;
        l.ground = r.ground;
        l.panel = r.panel;
        l.statusBar = r.well;
        l.button = r.raised;
        l.hover = r.hover;
        l.pressed = r.pressed;
        l.active = r.accentSoft;
        l.activeHover = r.pressed;
        l.activeBorder = r.accent;
        l.ink = r.ink;
        l.inkMuted = r.inkMuted;
        l.inkFaint = r.inkFaint;
        l.line = r.line;
        l.border = r.line;
        l.accent = r.accent;
        l.accentHover = r.accentHover;
        l.onAccent = r.onAccent;
        l.danger = r.danger;
        l.dangerHover = Shade(r.danger, theme.dark ? 24 : -24);
        l.freeSlice = r.line;
        return l;
    }
};

// The look of the current theme.
inline FilerLook Look() { return FilerLook::FromTheme(UltraCanvasTheme::Current()); }

} // namespace UltraCanvas
