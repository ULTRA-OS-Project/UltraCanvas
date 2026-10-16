// core/UltraCanvasTheme.cpp
// Version: 1.2.0 - Light's lineStrong meets 3:1 on panel and raised (a mid grey, not ButtonShadow)
// Version: 1.1.0 - change handlers
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#include "UltraCanvasTheme.h"

#include <utility>
#include <vector>

namespace UltraCanvas {

namespace {
UltraCanvasTheme& CurrentStorage() {
    static UltraCanvasTheme current = UltraCanvasTheme::Light();
    return current;
}
struct Handler { int id; UltraCanvasTheme::ChangeHandler fn; };
std::vector<Handler>& Handlers() {
    static std::vector<Handler> handlers;
    return handlers;
}
int& NextHandlerId() {
    static int next = 1;
    return next;
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
    // Not ButtonShadow (160): that is 2.6:1 on white, and a control border
    // a following element draws is to reach 3:1 on panel and raised
    r.lineStrong = Color(128, 128, 128);
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

void UltraCanvasTheme::SetCurrent(const UltraCanvasTheme& theme) {
    CurrentStorage() = theme;
    // a copy: a handler may add or remove handlers
    const std::vector<Handler> handlers = Handlers();
    for (const Handler& h : handlers) if (h.fn) h.fn(CurrentStorage());
}

int UltraCanvasTheme::AddChangeHandler(ChangeHandler handler) {
    const int id = NextHandlerId()++;
    Handlers().push_back({id, std::move(handler)});
    return id;
}

void UltraCanvasTheme::RemoveChangeHandler(int id) {
    std::vector<Handler>& handlers = Handlers();
    for (auto it = handlers.begin(); it != handlers.end(); ++it) {
        if (it->id == id) { handlers.erase(it); return; }
    }
}

} // namespace UltraCanvas
