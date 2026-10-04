// Apps/UltraPassword/ui/Theme.h
// UltraPassword's colours, type sizes, metrics and the small styling helpers
// every window uses. The palette and helpers are UltraMail's (UltraMailTheme.h)
// so the two apps look like one family — the start page and the setup wizard
// in particular are built the same way.
//
// Nothing here paints anything: these are values handed to catalogue elements
// through their own SetStyle / SetTextColor / SetBorders APIs.
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

#include <memory>
#include <string>

namespace UltraPassword {
namespace Theme {

// ----- colour -----
inline const UltraCanvas::Color kPageBackground {245, 246, 248};
inline const UltraCanvas::Color kCardBackground {255, 255, 255};
inline const UltraCanvas::Color kCardBorder     {226, 229, 234};
inline const UltraCanvas::Color kDivider        {234, 236, 240};
inline const UltraCanvas::Color kSidebar        {249, 250, 251};

inline const UltraCanvas::Color kTextPrimary    { 24,  28,  35};
inline const UltraCanvas::Color kTextSecondary  {104, 112, 125};
inline const UltraCanvas::Color kTextMuted      {142, 150, 163};

inline const UltraCanvas::Color kAccent         { 37,  99, 235};
inline const UltraCanvas::Color kAccentHover    { 29,  78, 216};
inline const UltraCanvas::Color kAccentPressed  { 30,  64, 175};
inline const UltraCanvas::Color kAccentSoft     {232, 240, 254};
inline const UltraCanvas::Color kRowHover       {243, 245, 248};
inline const UltraCanvas::Color kRowSelected    {226, 236, 253};

inline const UltraCanvas::Color kDanger         {220,  38,  38};
inline const UltraCanvas::Color kDangerHover    {185,  28,  28};
inline const UltraCanvas::Color kDangerPressed  {153,  27,  27};
inline const UltraCanvas::Color kSuccess        { 22, 163,  74};

// One colour per security level, warm to cool: the card's rating badge.
inline UltraCanvas::Color SecurityColor(SecurityLevel level) {
    switch (level) {
        case SecurityLevel::Weak:       return {220,  38,  38};   // red
        case SecurityLevel::Fair:       return {234,  88,  12};   // orange
        case SecurityLevel::Good:       return {202, 138,   4};   // amber
        case SecurityLevel::Strong:     return { 22, 163,  74};   // green
        case SecurityLevel::VeryStrong: return { 21, 128,  61};   // deep green
    }
    return kTextMuted;
}

// ----- type -----
constexpr float kSizeTitle     = 13.0f;
constexpr float kSizeHeading   = 11.0f;
constexpr float kSizeBody      = 9.0f;
constexpr float kSizeSecondary = 8.5f;
constexpr float kSizeSmall     = 8.0f;

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
    s.normalColor       = kAccent;
    s.hoverColor        = kAccentHover;
    s.pressedColor      = kAccentPressed;
    s.disabledColor     = UltraCanvas::Color(191, 205, 235, 255);
    s.normalTextColor   = UltraCanvas::Colors::White;
    s.hoverTextColor    = UltraCanvas::Colors::White;
    s.pressedTextColor  = UltraCanvas::Colors::White;
    s.disabledTextColor = UltraCanvas::Colors::White;
    s.borderWidth       = 0.0f;
    s.borderColor       = kAccent;
    s.cornerRadius      = kControlRadius;
    s.fontSize          = kSizeBody;
    return s;
}

inline UltraCanvas::ButtonStyle SecondaryButton() {
    UltraCanvas::ButtonStyle s;
    s.normalColor       = kCardBackground;
    s.hoverColor        = kRowHover;
    s.pressedColor      = kRowSelected;
    s.disabledColor     = kPageBackground;
    s.normalTextColor   = kTextPrimary;
    s.hoverTextColor    = kTextPrimary;
    s.pressedTextColor  = kTextPrimary;
    s.disabledTextColor = kTextMuted;
    s.borderWidth       = 1.0f;
    s.borderColor       = kCardBorder;
    s.cornerRadius      = kControlRadius;
    s.fontSize          = kSizeBody;
    return s;
}

inline UltraCanvas::ButtonStyle DangerButton() {
    UltraCanvas::ButtonStyle s = PrimaryButton();
    s.normalColor   = kDanger;
    s.hoverColor    = kDangerHover;
    s.pressedColor  = kDangerPressed;
    s.disabledColor = UltraCanvas::Color(240, 180, 180, 255);
    s.borderColor   = kDanger;
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
    c->SetBackgroundColor(kCardBackground);
    c->SetBorders(1.0f, kCardBorder, radius);
}

// ----- inputs -----
inline UltraCanvas::TextInputStyle InputStyle() {
    UltraCanvas::TextInputStyle s;
    s.borderColor        = kCardBorder;
    s.focusBorderColor   = kAccent;
    s.validBorderColor   = kCardBorder;
    s.textColor          = kTextPrimary;
    s.placeholderColor   = kTextMuted;
    s.borderRadius       = static_cast<int>(kControlRadius);
    s.paddingLeft        = 6;
    s.paddingRight       = 6;
    s.fontStyle.fontSize = kSizeBody;
    return s;
}
inline void StyleInput(const std::shared_ptr<UltraCanvas::UltraCanvasTextInput>& in) {
    if (in) in->SetStyle(InputStyle());
}

template <typename DropdownPtr>
inline void StyleDropdown(const DropdownPtr& dd) {
    if (!dd) return;
    auto s = dd->GetStyle();
    s.fontSize = kSizeBody;
    dd->SetStyle(s);
}

// ----- labels -----
inline std::shared_ptr<UltraCanvas::UltraCanvasLabel>
MakeText(const std::string& id, const std::string& text, float size = kSizeBody,
         const UltraCanvas::Color& color = kTextPrimary,
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
         float size = kSizeBody, const UltraCanvas::Color& color = kTextPrimary,
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
