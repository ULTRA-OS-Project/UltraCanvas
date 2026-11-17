// Apps/UltraToDo/ui/ToDoTheme.h
// UltraToDo's look: panels with round corners, a pastel border round a
// faint tint of the same pastel. A task's panel takes its colour from how
// urgent the task is - rose for overdue, peach for "start
// now", butter for the next day, mint for later, sky for a task without a
// date, mist for a finished one - so the list reads at a glance before a
// word of it is read.
//
// The pastels are the light look's. The fill is always the theme's panel
// colour with a little of the pastel in it, which keeps the theme's ink
// readable on it; under a dark theme the border is mixed into the panel
// colour too; the inks, the lines and the sizes are the theme's roles
// (UltraCanvasTheme.h) throughout.
// Version: 0.5.0 - UrgencyColor: the marker from green to orange to red
// Version: 0.4.0 - MakeIconButton: a round button with an icon alone, following the theme
// Version: 0.3.0 - the pastel is the panel's border; the fill a faint tint of it
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
#include "UltraCanvasUtils.h"   // GetResourcesDir, NormalizePath

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

// A panel's border: the pastel itself - its main colour - under the light
// look; under a dark one the pastel mixed into the panel colour, still
// clearly that hue.
inline UltraCanvas::Color PanelBorder(const UltraCanvas::Color& light) {
    return Dark() ? Mix(Roles().panel, light, 0.62f) : light;
}
// A panel's fill: a faint tint of the same hue over the theme's panel
// colour, so the text sits on an almost plain ground and the border carries
// the colour.
inline UltraCanvas::Color PanelFill(const UltraCanvas::Color& light) {
    return Mix(Roles().panel, light, Dark() ? 0.12f : 0.30f);
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

// The marker's colour for an UrgencyLevel: green at 0, orange at 1, red at
// 2, blended in between. The three are strong enough to read on any panel
// in either look.
inline UltraCanvas::Color MarkerGreen()  { return Dark() ? UltraCanvas::Color(74, 200, 120) : UltraCanvas::Color(34, 160, 84); }
inline UltraCanvas::Color MarkerOrange() { return Dark() ? UltraCanvas::Color(255, 170, 70) : UltraCanvas::Color(240, 135, 20); }
inline UltraCanvas::Color MarkerRed()    { return Dark() ? UltraCanvas::Color(255, 95, 95) : UltraCanvas::Color(214, 45, 45); }
inline UltraCanvas::Color UrgencyColor(double level) {
    if (level < 0) return Roles().inkFaint;
    if (level <= 1.0) return Mix(MarkerGreen(), MarkerOrange(), static_cast<float>(level));
    return Mix(MarkerOrange(), MarkerRed(), static_cast<float>(std::min(1.0, level - 1.0)));
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
constexpr float kPanelBorder   = 3.0f;    // wide enough for a pastel to read as a colour
constexpr float kControlHeight = 28.0f;
constexpr float kAddButtonSize = 48.0f;
constexpr float kColumnMinWidth = 440.0f;   // a panel is never narrower than this in a second column
constexpr int   kMaxColumns    = 4;
constexpr float kListPaddingX  = 18.0f;    // the list's padding: 2 left, 16 right (the scrollbar)
constexpr float kMarkerSize    = 26.0f;    // the urgency ring in a panel's button line
constexpr unsigned kBlinkMs    = 600;      // one half of a blink
constexpr float kThumbWidth    = 96.0f;
constexpr float kThumbHeight   = 68.0f;

// A panel with round corners: the pastel as its border, a faint tint of it
// as its fill. Any element - a container, the reminder line's label.
inline void ApplyPanel(const std::shared_ptr<UltraCanvas::UltraCanvasUIElement>& panel,
                       const UltraCanvas::Color& pastel, float radius = kPanelRadius) {
    if (!panel) return;
    panel->SetBackgroundColor(PanelFill(pastel));
    panel->SetBorders(kPanelBorder, PanelBorder(pastel), radius);
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
// primary colour. The framework centres a one-character label on its ink,
// so the "+" sits in the middle of the circle.
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

// A round button showing an icon alone (media/icons/<iconFile>), what it
// does in its tooltip. The icon is drawn as a mask in the button's text
// colour, so it follows the theme like a label would.
inline std::shared_ptr<UltraCanvas::UltraCanvasButton>
MakeIconButton(const std::string& id, const std::string& iconFile, const std::string& tooltip) {
    auto button = UltraCanvas::CreateButton(id, 0, 0, kControlHeight, kControlHeight, "");
    button->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    button->SetIcon(UltraCanvas::NormalizePath(UltraCanvas::GetResourcesDir() + "media/icons/" + iconFile));
    button->SetIconPosition(UltraCanvas::ButtonIconPosition::Center);
    button->SetIconSize(16, 16);
    button->SetUseIconAsMask(true);
    button->SetTooltip(tooltip);
    MakeRound(button, kControlHeight, UltraCanvas::ButtonVariant::Standard);
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
