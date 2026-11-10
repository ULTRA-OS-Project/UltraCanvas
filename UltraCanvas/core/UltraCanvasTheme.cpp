// core/UltraCanvasTheme.cpp
// Version: 1.3.0 - ThemeFonts and the text roles' sizes; LightHighContrast / DarkHighContrast;
//                  FromHost with DeriveAccent and the contrast measure (T3 of the host theme proposal)
// Version: 1.2.0 - Light's lineStrong meets 3:1 on panel and raised (a mid grey, not ButtonShadow)
// Version: 1.1.0 - change handlers
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#include "UltraCanvasTheme.h"

#include <algorithm>
#include <cmath>
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

UltraCanvasTheme UltraCanvasTheme::LightHighContrast() {
    UltraCanvasTheme t;
    t.name = "Light High Contrast";
    t.dark = false;
    t.highContrast = true;
    ThemeRoles& r = t.roles;
    r.ground = Colors::White;
    r.panel = Colors::White;
    r.well = Colors::White;
    r.raised = Colors::White;
    r.hover = Color(230, 230, 230);
    r.pressed = Color(200, 200, 200);
    r.line = Color(64, 64, 64);
    r.lineStrong = Colors::Black;
    r.ink = Colors::Black;
    r.inkMuted = Color(51, 51, 51);     // 12.6:1 on white
    r.inkFaint = Color(85, 85, 85);     // 7.5:1 on white
    r.accent = Color(0, 80, 160);       // white reads 7.3:1 on it
    r.accentHover = Color(0, 60, 130);
    r.accentSoft = Color(210, 225, 245);
    r.onAccent = Colors::White;
    r.focus = Colors::Black;
    r.danger = Color(180, 0, 0);
    return t;
}

UltraCanvasTheme UltraCanvasTheme::DarkHighContrast() {
    UltraCanvasTheme t;
    t.name = "Dark High Contrast";
    t.dark = true;
    t.highContrast = true;
    ThemeRoles& r = t.roles;
    r.ground = Colors::Black;
    r.panel = Colors::Black;
    r.well = Color(10, 10, 10);
    r.raised = Color(20, 20, 20);
    r.hover = Color(50, 50, 50);
    r.pressed = Color(80, 80, 80);
    r.line = Color(160, 160, 160);
    r.lineStrong = Colors::White;
    r.ink = Colors::White;
    r.inkMuted = Color(220, 220, 220);
    r.inkFaint = Color(190, 190, 190);
    r.accent = Color(90, 180, 255);     // black reads 9:1 on it
    r.accentHover = Color(130, 200, 255);
    r.accentSoft = Color(20, 50, 80);
    r.onAccent = Colors::Black;
    r.focus = Colors::White;
    r.danger = Color(255, 120, 120);
    return t;
}

// ===== FONTS =====

ThemeFonts::ThemeFonts() {
    ui.fontFamily = "Ubuntu";
    ui.fontSize = 12.0;
    mono.fontFamily = "Ubuntu Mono";
    mono.fontSize = 12.0;
}

double UltraCanvasTheme::FontSize(ThemeTextRole role) const {
    double ratio = 1.0;
    switch (role) {
        case ThemeTextRole::Body:    ratio = 1.0; break;
        case ThemeTextRole::Caption: ratio = 0.92; break;
        case ThemeTextRole::Small:   ratio = 0.85; break;
        case ThemeTextRole::Heading: ratio = 1.25; break;
        case ThemeTextRole::Title:   ratio = 1.5; break;
        case ThemeTextRole::Mono:    ratio = 1.0; break;
    }
    const double base = role == ThemeTextRole::Mono ? fonts.mono.fontSize : fonts.ui.fontSize;
    const double scale = fonts.scale > 0 ? fonts.scale : 1.0;
    // to a quarter point: the sizes a text engine renders distinctly
    return std::round(base * ratio * scale * 4.0) / 4.0;
}

FontStyle UltraCanvasTheme::Font(ThemeTextRole role) const {
    FontStyle font = role == ThemeTextRole::Mono ? fonts.mono : fonts.ui;
    font.fontSize = FontSize(role);
    return font;
}

// ===== FROM THE HOST =====

double UltraCanvasTheme::Luminance(const Color& color) {
    auto channel = [](uint8_t v) {
        const double s = v / 255.0;
        return s <= 0.03928 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(color.r) + 0.7152 * channel(color.g) + 0.0722 * channel(color.b);
}

double UltraCanvasTheme::Contrast(const Color& a, const Color& b) {
    const double la = Luminance(a), lb = Luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

namespace {

Color Mix(const Color& from, const Color& to, double amount) {
    auto channel = [amount](uint8_t a, uint8_t b) {
        return static_cast<uint8_t>(std::lround(a + (static_cast<double>(b) - a) * amount));
    };
    return Color(channel(from.r, to.r), channel(from.g, to.g), channel(from.b, to.b), 255);
}

}  // namespace

ThemeRoles UltraCanvasTheme::DeriveAccent(ThemeRoles roles, const Color& accent, bool dark, double onAccentContrast) {
    const Color darkInk = Light().roles.ink;
    Color base = Color(accent.r, accent.g, accent.b, 255);
    // Text on the accent: white or the dark ink, whichever reads better;
    // the accent moved toward black or white, a step at a time, until the
    // better one reaches the contrast asked for (a mid-grey or a yellow
    // accent reaches neither as it is).
    // White when it reads (the conventional look on a blue or a red), else
    // the dark ink when it reads, else whichever reads better.
    auto better = [&](const Color& c) {
        if (Contrast(Colors::White, c) >= onAccentContrast) return Colors::White;
        if (Contrast(darkInk, c) >= onAccentContrast) return darkInk;
        return Contrast(Colors::White, c) >= Contrast(darkInk, c) ? Colors::White : darkInk;
    };
    for (int step = 0; step < 64 && Contrast(better(base), base) < onAccentContrast; ++step) {
        const bool towardWhite = Contrast(Colors::White, base) < Contrast(darkInk, base);
        base = Mix(base, towardWhite ? Colors::White : Colors::Black, 0.06);
    }
    roles.accent = base;
    roles.onAccent = better(base);
    roles.accentHover = Mix(base, roles.ink, 0.08);
    roles.accentSoft = Mix(roles.panel, base, dark ? 0.18 : 0.14);
    roles.focus = Contrast(base, roles.ground) >= 3.0 ? base : roles.lineStrong;
    return roles;
}

UltraCanvasTheme UltraCanvasTheme::FromHost(const HostAppearance& host) {
    UltraCanvasTheme t = host.highContrast ? (host.dark ? DarkHighContrast() : LightHighContrast())
                                           : (host.dark ? Dark() : Light());
    t.name = "Host " + t.name;
    if (host.accent) t.roles = DeriveAccent(t.roles, *host.accent, host.dark, host.highContrast ? 7.0 : 4.5);
    if (!host.uiFont.fontFamily.empty() && host.uiFont.fontSize > 0) t.fonts.ui = host.uiFont;
    if (!host.monoFont.fontFamily.empty() && host.monoFont.fontSize > 0) t.fonts.mono = host.monoFont;
    t.fonts.scale = host.textScale > 0 ? host.textScale : 1.0;
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
