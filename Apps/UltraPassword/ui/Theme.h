// Apps/UltraPassword/ui/Theme.h
// UltraPassword's colours, type sizes, metrics and the small styling helpers
// every window uses. The palette and helpers are UltraMail's (UltraMailTheme.h)
// so the two apps look like one family — the start page and the setup wizard
// in particular are built the same way.
//
// Nothing here paints anything: these are values handed to catalogue elements
// through their own SetStyle / SetTextColor / SetBorders APIs.
// Version: 0.2.0 - the colours and type sizes are the framework theme's roles and text roles, read as
//                  functions: a dark desktop gives a dark UltraPassword (T6 of the host theme proposal)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "core/PasswordVault.h"

#include "UltraCanvasBadge.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTheme.h"

#include <algorithm>
#include <memory>
#include <string>

namespace UltraPassword {
namespace Theme {

// ----- colour -----
// The colours are the framework theme's roles (UltraCanvasTheme.h): the
// desktop's light or dark look, or Appearance.conf's over it, read as
// functions when an element is styled, so a dark desktop gives a dark
// UltraPassword (T6 of the host theme proposal). A window built after the
// theme changed takes the new look.
inline const UltraCanvas::ThemeRoles& Roles() { return UltraCanvas::UltraCanvasTheme::Current().roles; }
inline bool Dark() { return UltraCanvas::UltraCanvasTheme::Current().dark; }
inline UltraCanvas::Color Shade(const UltraCanvas::Color& c, int by) {
    auto step = [by](uint8_t v) { return static_cast<uint8_t>(std::max(0, std::min(255, static_cast<int>(v) + by))); };
    return UltraCanvas::Color(step(c.r), step(c.g), step(c.b), c.a);
}
// A strong colour that reads on the look's panel: lifted on a dark one.
inline UltraCanvas::Color Strong(const UltraCanvas::Color& light) { return Dark() ? Shade(light, 70) : light; }

inline UltraCanvas::Color PageBackground() { return Roles().ground; }
inline UltraCanvas::Color CardBackground() { return Roles().panel; }
inline UltraCanvas::Color CardBorder()     { return Roles().line; }
inline UltraCanvas::Color Divider()        { return Roles().line; }
inline UltraCanvas::Color Sidebar()        { return Roles().well; }

inline UltraCanvas::Color TextPrimary()    { return Roles().ink; }
inline UltraCanvas::Color TextSecondary()  { return Roles().inkMuted; }
inline UltraCanvas::Color TextMuted()      { return Roles().inkFaint; }

inline UltraCanvas::Color Accent()         { return Roles().accent; }
inline UltraCanvas::Color AccentHover()    { return Roles().accentHover; }
inline UltraCanvas::Color AccentPressed()  { return Shade(Roles().accentHover, Dark() ? 20 : -20); }
inline UltraCanvas::Color AccentSoft()     { return Roles().accentSoft; }
inline UltraCanvas::Color RowHover()       { return Roles().hover; }
inline UltraCanvas::Color RowSelected()    { return Roles().accentSoft; }

inline UltraCanvas::Color Danger()         { return Roles().danger; }
inline UltraCanvas::Color DangerHover()    { return Shade(Roles().danger, Dark() ? 24 : -24); }
inline UltraCanvas::Color DangerPressed()  { return Shade(Roles().danger, Dark() ? 48 : -48); }
inline UltraCanvas::Color Successful()        { return Strong(UltraCanvas::Color( 22, 163,  74)); }

// One colour per security level, warm to cool: the card's rating badge.
inline UltraCanvas::Color SecurityColor(SecurityLevel level) {
    switch (level) {
        case SecurityLevel::Weak:       return Roles().danger;                                // red
        case SecurityLevel::Fair:       return Strong(UltraCanvas::Color(234,  88,  12));   // orange
        case SecurityLevel::Good:       return Strong(UltraCanvas::Color(202, 138,   4));   // amber
        case SecurityLevel::Strong:     return Strong(UltraCanvas::Color( 22, 163,  74));   // green
        case SecurityLevel::VeryStrong: return Strong(UltraCanvas::Color( 21, 128,  61));   // deep green
    }
    return TextMuted();
}

// ----- type -----
// The theme's text roles: the desktop's interface font size and the
// ratios of UltraCanvasTheme, as UltraMail beside it.
inline float SizeTitle()     { return static_cast<float>(UltraCanvas::UltraCanvasTheme::Current().FontSize(UltraCanvas::ThemeTextRole::Title)); }
inline float SizeHeading()   { return static_cast<float>(UltraCanvas::UltraCanvasTheme::Current().FontSize(UltraCanvas::ThemeTextRole::Heading)); }
inline float SizeBody()      { return static_cast<float>(UltraCanvas::UltraCanvasTheme::Current().FontSize(UltraCanvas::ThemeTextRole::Body)); }
inline float SizeSecondary() { return static_cast<float>(UltraCanvas::UltraCanvasTheme::Current().FontSize(UltraCanvas::ThemeTextRole::Caption)); }
inline float SizeSmall()     { return static_cast<float>(UltraCanvas::UltraCanvasTheme::Current().FontSize(UltraCanvas::ThemeTextRole::Small)); }

// ----- metrics -----
constexpr float kPagePadding   = 10.0f;
constexpr float kGap           = 8.0f;
constexpr float kInnerGap      = 6.0f;
constexpr float kCardRadius    = 8.0f;
constexpr float kControlRadius = 5.0f;
constexpr float kControlHeight = 24.0f;
constexpr float kToolbarHeight = 28.0f;

// ----- buttons -----
inline UltraCanvas::ButtonStyle PrimaryButton() {
    UltraCanvas::ButtonStyle s;
    s.normalColor       = Accent();
    s.hoverColor        = AccentHover();
    s.pressedColor      = AccentPressed();
    s.disabledColor     = UltraCanvas::Color(191, 205, 235, 255);
    s.normalTextColor   = UltraCanvas::Colors::White;
    s.hoverTextColor    = UltraCanvas::Colors::White;
    s.pressedTextColor  = UltraCanvas::Colors::White;
    s.disabledTextColor = UltraCanvas::Colors::White;
    s.borderWidth       = 0.0f;
    s.borderColor       = Accent();
    s.cornerRadius      = kControlRadius;
    s.fontSize          = SizeBody();
    return s;
}

inline UltraCanvas::ButtonStyle SecondaryButton() {
    UltraCanvas::ButtonStyle s;
    s.normalColor       = CardBackground();
    s.hoverColor        = RowHover();
    s.pressedColor      = RowSelected();
    s.disabledColor     = PageBackground();
    s.normalTextColor   = TextPrimary();
    s.hoverTextColor    = TextPrimary();
    s.pressedTextColor  = TextPrimary();
    s.disabledTextColor = TextMuted();
    s.borderWidth       = 1.0f;
    s.borderColor       = CardBorder();
    s.cornerRadius      = kControlRadius;
    s.fontSize          = SizeBody();
    return s;
}

inline UltraCanvas::ButtonStyle DangerButton() {
    UltraCanvas::ButtonStyle s = PrimaryButton();
    s.normalColor   = Danger();
    s.hoverColor    = DangerHover();
    s.pressedColor  = DangerPressed();
    s.disabledColor = UltraCanvas::Color(240, 180, 180, 255);
    s.borderColor   = Danger();
    return s;
}

inline void StylePrimary(const std::shared_ptr<UltraCanvas::UltraCanvasButton>& b) {
    if (b) b->SetStyle(PrimaryButton());
}
inline void StyleSecondary(const std::shared_ptr<UltraCanvas::UltraCanvasButton>& b) {
    if (b) b->SetStyle(SecondaryButton());
}
inline void StyleDanger(const std::shared_ptr<UltraCanvas::UltraCanvasButton>& b) {
    if (b) b->SetStyle(DangerButton());
}

// Buttons size to their label, with the old fixed width as a floor (the same
// rule as UltraMail's Theme::FitToLabel).
inline void FitToLabel(const std::shared_ptr<UltraCanvas::UltraCanvasButton>& b,
                       float minWidth = 0.0f) {
    if (!b) return;
    b->size.width = UltraCanvas::CSSLayout::Dimension::Auto();
    UltraCanvas::CSSLayout::BoxConstraints limits =
        b->boxConstraints.value_or(UltraCanvas::CSSLayout::BoxConstraints{});
    if (minWidth > 0.0f) limits.minWidth = UltraCanvas::CSSLayout::Dimension::Px(minWidth);
    b->boxConstraints = limits;
    b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
}

// A compact secondary button for a row of actions, sized to its label.
inline std::shared_ptr<UltraCanvas::UltraCanvasButton>
MakeButton(const std::string& id, const std::string& text, float minWidth = 60.0f,
           bool primary = false) {
    auto b = UltraCanvas::CreateButton(id, 0, 0, minWidth, kControlHeight, text);
    FitToLabel(b, minWidth);
    if (primary) StylePrimary(b); else StyleSecondary(b);
    return b;
}

// ----- surfaces -----
inline void ApplyCard(const std::shared_ptr<UltraCanvas::UltraCanvasContainer>& c,
                      float radius = kCardRadius) {
    if (!c) return;
    c->SetBackgroundColor(CardBackground());
    c->SetBorders(1.0f, CardBorder(), radius);
}

// ----- inputs -----
inline UltraCanvas::TextInputStyle InputStyle() {
    UltraCanvas::TextInputStyle s;
    s.borderColor        = CardBorder();
    s.focusBorderColor   = Accent();
    s.validBorderColor   = CardBorder();
    s.textColor          = TextPrimary();
    s.placeholderColor   = TextMuted();
    s.borderRadius       = static_cast<int>(kControlRadius);
    s.paddingLeft        = 6;
    s.paddingRight       = 6;
    s.fontStyle.fontSize = SizeBody();
    return s;
}
inline void StyleInput(const std::shared_ptr<UltraCanvas::UltraCanvasTextInput>& in) {
    if (in) in->SetStyle(InputStyle());
}

template <typename DropdownPtr>
inline void StyleDropdown(const DropdownPtr& dd) {
    if (!dd) return;
    auto s = dd->GetStyle();
    s.fontSize = SizeBody();
    dd->SetStyle(s);
}

// ----- labels -----
inline std::shared_ptr<UltraCanvas::UltraCanvasLabel>
MakeText(const std::string& id, const std::string& text, float size = SizeBody(),
         const UltraCanvas::Color& color = TextPrimary(),
         UltraCanvas::FontWeight weight = UltraCanvas::FontWeight::Normal) {
    auto label = UltraCanvas::CreateLabel(id, text);
    label->SetFontSize(size);
    label->SetFontWeight(weight);
    label->SetTextColor(color);
    return label;
}

// A fixed-height, stretch-width line of text (for flex columns).
inline std::shared_ptr<UltraCanvas::UltraCanvasLabel>
MakeLine(const std::string& id, const std::string& text, float height,
         float size = SizeBody(), const UltraCanvas::Color& color = TextPrimary(),
         UltraCanvas::FontWeight weight = UltraCanvas::FontWeight::Normal) {
    auto label = UltraCanvas::CreateLabel(id, 0, 0, 0, height, text);
    label->SetFontSize(size);
    label->SetFontWeight(weight);
    label->SetTextColor(color);
    return label;
}

// A status pill with a solid colour and white text.
inline std::shared_ptr<UltraCanvas::UltraCanvasBadge>
MakePill(const std::string& id, const std::string& text, const UltraCanvas::Color& color) {
    auto badge = UltraCanvas::CreateBadge(id, 0, 0, text);
    badge->SetColor(color);
    badge->layoutItem.SetFlexShrink(0);
    return badge;
}

} // namespace Theme
} // namespace UltraPassword
