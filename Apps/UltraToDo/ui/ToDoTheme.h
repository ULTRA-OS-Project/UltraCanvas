// Apps/UltraToDo/ui/ToDoTheme.h
// UltraToDo's look: pastel panels with round corners. A task's panel takes
// its colour from how urgent the task is - rose for overdue, peach for "start
// now", butter for the next day, mint for later, sky for a task without a
// date, mist for a finished one - so the list reads at a glance before a
// word of it is read.
//
// The pastels are the light look's. Under a dark theme each is mixed into
// the theme's panel colour, which keeps the hue and the theme's ink readable
// on it; the inks, the lines and the sizes are the theme's roles
// (UltraCanvasTheme.h) throughout.
// Version: 0.2.0 - buttons and dialog texts by their role in the theme, following it
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "core/ToDoTask.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTheme.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>

namespace UltraToDo {
namespace Theme {

inline const UltraCanvas::ThemeRoles& Roles() { return UltraCanvas::UltraCanvasTheme::Current().roles; }
inline bool Dark() { return UltraCanvas::UltraCanvasTheme::Current().dark; }

inline UltraCanvas::Color Mix(const UltraCanvas::Color& a, const UltraCanvas::Color& b, float share) {
    auto channel = [share](uint8_t x, uint8_t y) {
        const float v = static_cast<float>(x) * (1.0f - share) + static_cast<float>(y) * share;
        return static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, v + 0.5f)));
    };
    return UltraCanvas::Color(channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b), 255);
}
inline UltraCanvas::Color Shade(const UltraCanvas::Color& c, int by) {
    auto step = [by](uint8_t v) {
        return static_cast<uint8_t>(std::max(0, std::min(255, static_cast<int>(v) + by)));
    };
    return UltraCanvas::Color(step(c.r), step(c.g), step(c.b), c.a);
}

// A pastel as it is under the light look, mixed into the panel under a dark one.
inline UltraCanvas::Color Pastel(const UltraCanvas::Color& light) {
    return Dark() ? Mix(Roles().panel, light, 0.26f) : light;
}
// The line around a pastel panel: the same hue, a step stronger.
inline UltraCanvas::Color PastelLine(const UltraCanvas::Color& light) {
    return Dark() ? Mix(Roles().panel, light, 0.48f) : Shade(light, -30);
}
// A full-strength mark in a pastel's hue (the urgency pill).
inline UltraCanvas::Color PastelMark(const UltraCanvas::Color& light) {
    return Dark() ? Mix(Roles().panel, light, 0.62f) : Shade(light, -96);
}

inline UltraCanvas::Color Rose()     { return UltraCanvas::Color(255, 214, 221); }
inline UltraCanvas::Color Peach()    { return UltraCanvas::Color(255, 226, 199); }
inline UltraCanvas::Color Butter()   { return UltraCanvas::Color(255, 244, 194); }
inline UltraCanvas::Color Mint()     { return UltraCanvas::Color(207, 240, 218); }
inline UltraCanvas::Color Sky()      { return UltraCanvas::Color(212, 229, 252); }
inline UltraCanvas::Color Mist()     { return UltraCanvas::Color(233, 235, 240); }
inline UltraCanvas::Color Lavender() { return UltraCanvas::Color(226, 217, 250); }

// The pastel of a task's panel, as the light look has it.
inline UltraCanvas::Color UrgencyPastel(ToDoUrgency urgency) {
    switch (urgency) {
        case ToDoUrgency::Overdue:     return Rose();
        case ToDoUrgency::StartNow:    return Peach();
        case ToDoUrgency::Soon:        return Butter();
        case ToDoUrgency::Later:       return Mint();
        case ToDoUrgency::Unscheduled: return Sky();
        case ToDoUrgency::Finished:    return Mist();
    }
    return Sky();
}

inline UltraCanvas::Color PageBackground() {
    return Dark() ? Roles().ground : UltraCanvas::Color(250, 248, 253);
}
inline UltraCanvas::Color Ink()      { return Roles().ink; }
inline UltraCanvas::Color InkMuted() { return Roles().inkMuted; }
inline UltraCanvas::Color InkFaint() { return Roles().inkFaint; }
inline UltraCanvas::Color Danger()   { return Roles().danger; }

inline float FontSizeOf(UltraCanvas::ThemeTextRole role) {
    return static_cast<float>(UltraCanvas::UltraCanvasTheme::Current().FontSize(role));
}
inline float SizeTitle()   { return FontSizeOf(UltraCanvas::ThemeTextRole::Title); }
inline float SizeHeading() { return FontSizeOf(UltraCanvas::ThemeTextRole::Heading); }
inline float SizeBody()    { return FontSizeOf(UltraCanvas::ThemeTextRole::Body); }
inline float SizeSmall()   { return FontSizeOf(UltraCanvas::ThemeTextRole::Small); }

constexpr float kPagePadding   = 18.0f;
constexpr float kGap           = 12.0f;
constexpr float kInnerGap      = 8.0f;
constexpr float kPanelRadius   = 16.0f;
constexpr float kControlHeight = 28.0f;
constexpr float kAddButtonSize = 48.0f;
constexpr float kThumbWidth    = 96.0f;
constexpr float kThumbHeight   = 68.0f;

// A pastel panel with round corners.
inline void ApplyPanel(const std::shared_ptr<UltraCanvas::UltraCanvasContainer>& panel,
                       const UltraCanvas::Color& pastel, float radius = kPanelRadius) {
    if (!panel) return;
    panel->SetBackgroundColor(Pastel(pastel));
    panel->SetBorders(1.0f, PastelLine(pastel), radius);
}

// A button with fully round ends, coloured by its role in the theme
// (UltraCanvasTheme: Standard, or Primary for the main action) and following
// it: the radius is set first, and FromTheme keeps it while the colours
// change with the look.
inline void MakeRound(const std::shared_ptr<UltraCanvas::UltraCanvasButton>& button, float height,
                      UltraCanvas::ButtonVariant variant) {
    UltraCanvas::ButtonStyle style = button->GetStyle();
    style.cornerRadius = height / 2.0f;
    button->SetStyle(style);
    button->SetThemeVariant(variant);
    button->SetFollowTheme(true);
}

// The round button that adds a task: a circle with a plus, the theme's
// primary colour.
inline std::shared_ptr<UltraCanvas::UltraCanvasButton> MakeAddButton(const std::string& id) {
    auto button = UltraCanvas::CreateButton(id, 0, 0, kAddButtonSize, kAddButtonSize, "+");
    MakeRound(button, kAddButtonSize, UltraCanvas::ButtonVariant::Primary);
    button->SetFontSize(26.0f);   // kept through every change of theme
    return button;
}

// A button as wide as its text, never narrower than `minWidth`.
inline std::shared_ptr<UltraCanvas::UltraCanvasButton>
MakePill(const std::string& id, const std::string& text, float minWidth = 64.0f, bool main = false) {
    auto button = UltraCanvas::CreateButton(id, 0, 0, minWidth, kControlHeight, text);
    button->size.width = UltraCanvas::CSSLayout::Dimension::Auto();
    UltraCanvas::CSSLayout::BoxConstraints limits =
        button->boxConstraints.value_or(UltraCanvas::CSSLayout::BoxConstraints{});
    limits.minWidth = UltraCanvas::CSSLayout::Dimension::Px(minWidth);
    button->boxConstraints = limits;
    button->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    MakeRound(button, kControlHeight, main ? UltraCanvas::ButtonVariant::Primary : UltraCanvas::ButtonVariant::Standard);
    return button;
}

// One line of text in one of the theme's inks (Body, Muted, Faint, Heading),
// following it. The size is the label's own and is kept.
inline std::shared_ptr<UltraCanvas::UltraCanvasLabel>
MakeRoleLine(const std::string& id, const std::string& text, float height, float size,
             UltraCanvas::LabelVariant variant = UltraCanvas::LabelVariant::Body,
             UltraCanvas::FontWeight weight = UltraCanvas::FontWeight::Normal) {
    auto label = UltraCanvas::CreateLabel(id, 0, 0, 0, height, text);
    label->SetFontSize(size);
    label->SetFontWeight(weight);
    label->SetThemeVariant(variant);
    label->SetFollowTheme(true);
    return label;
}

// The same, wrapping, as tall as it needs to be.
inline std::shared_ptr<UltraCanvas::UltraCanvasLabel>
MakeRoleWrapped(const std::string& id, const std::string& text, float size,
                UltraCanvas::LabelVariant variant = UltraCanvas::LabelVariant::Body) {
    auto label = MakeRoleLine(id, text, 20, size, variant);
    label->SetWrap(UltraCanvas::TextWrap::WrapWord);
    label->size.height = UltraCanvas::CSSLayout::Dimension::Auto();
    return label;
}

// One line of text at a fixed height.
inline std::shared_ptr<UltraCanvas::UltraCanvasLabel>
MakeLine(const std::string& id, const std::string& text, float height, float size,
         const UltraCanvas::Color& color,
         UltraCanvas::FontWeight weight = UltraCanvas::FontWeight::Normal) {
    auto label = UltraCanvas::CreateLabel(id, 0, 0, 0, height, text);
    label->SetFontSize(size);
    label->SetFontWeight(weight);
    label->SetTextColor(color);
    return label;
}

// Text that wraps, as tall as it needs to be.
inline std::shared_ptr<UltraCanvas::UltraCanvasLabel>
MakeWrapped(const std::string& id, const std::string& text, float size, const UltraCanvas::Color& color) {
    auto label = MakeLine(id, text, 20, size, color);
    label->SetWrap(UltraCanvas::TextWrap::WrapWord);
    label->size.height = UltraCanvas::CSSLayout::Dimension::Auto();
    return label;
}

} // namespace Theme
} // namespace UltraToDo
