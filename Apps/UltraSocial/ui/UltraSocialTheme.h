// Apps/UltraSocial/ui/UltraSocialTheme.h
// One place for UltraSocial's colours, type sizes, metrics and the small
// styling helpers its windows use, so the main window, the account wizard and
// the dialogs cannot drift apart.
//
// The values are UltraMail's (Apps/UltraMail/ui/UltraMailTheme.h) on purpose:
// the two apps sit side by side on the same desktop and should read as one
// family - the same near-white page, white cards, blue primary button and
// type scale. The header is a copy rather than a shared include because an
// app never links another app's sources.
//
// Nothing here paints anything. These are values handed to catalogue elements
// through their own SetStyle / SetTextColor / SetBorders APIs, per the
// framework rule that applications never hand-roll a widget.
// Version: 0.1.0 - UltraMail's look, plus the per-network avatar colours
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraSocialTypes.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasUtils.h"   // GetResourcesDir

#include <memory>
#include <string>

namespace UltraSocial {
namespace Theme {

// ---------------------------------------------------------------------------
// Colour
// ---------------------------------------------------------------------------
// A near-white page with white cards, so a card reads as raised without a
// shadow (the catalogue has no shadow primitive).
inline const UltraCanvas::Color kPageBackground {245, 246, 248};
inline const UltraCanvas::Color kCardBackground {255, 255, 255};
inline const UltraCanvas::Color kCardBorder     {226, 229, 234};
inline const UltraCanvas::Color kDivider        {234, 236, 240};

inline const UltraCanvas::Color kTextPrimary    { 24,  28,  35};
inline const UltraCanvas::Color kTextSecondary  {104, 112, 125};
inline const UltraCanvas::Color kTextMuted      {142, 150, 163};

inline const UltraCanvas::Color kAccent         { 37,  99, 235};   // primary actions, selection
inline const UltraCanvas::Color kAccentHover    { 29,  78, 216};
inline const UltraCanvas::Color kAccentPressed  { 30,  64, 175};
inline const UltraCanvas::Color kAccentSoft     {232, 240, 254};   // tinted surfaces
inline const UltraCanvas::Color kRowHover       {243, 245, 248};
inline const UltraCanvas::Color kRowSelected    {226, 236, 253};

// Adaptation warnings ("text shortened to 300 characters"): UltraMail's
// "waiting" orange, a caution rather than an error.
inline const UltraCanvas::Color kWarningText    {194,  65,  12};
inline const UltraCanvas::Color kWarningTint    {255, 237, 213};

// ---------------------------------------------------------------------------
// Type
// ---------------------------------------------------------------------------
// UltraMail's scale (UltraFiler's UI font: 9pt for controls and body text).
constexpr float kSizeAppTitle  = 22.0f;   // the start page's app name
constexpr float kSizeTitle     = 13.0f;   // the toolbar's app name
constexpr float kSizeHeading   = 11.0f;   // card headings
constexpr float kSizeBody      = 9.0f;
constexpr float kSizeSecondary = 8.5f;
constexpr float kSizeSmall     = 8.0f;

// ---------------------------------------------------------------------------
// Metrics
// ---------------------------------------------------------------------------
constexpr float kPagePadding   = 10.0f;   // gutter between the window edge and content
constexpr float kGap           = 8.0f;    // gap between cards / rows
constexpr float kInnerGap      = 6.0f;    // gap inside a row
constexpr float kCardPadV      = 12.0f;   // a card's inner padding, top/bottom
constexpr float kCardPadH      = 14.0f;   // ... and left/right
constexpr float kCardRadius    = 8.0f;
constexpr float kControlRadius = 5.0f;
constexpr float kControlHeight = 24.0f;   // buttons and inputs
constexpr float kToolbarHeight = 28.0f;
constexpr float kAvatarSize    = 26.0f;   // the network initial in an account row
constexpr int   kButtonIcon    = 14;      // icon size inside a toolbar/card button

// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------
// media/icons/<name> under the resources folder (the build tree links it).
inline std::string IconPath(const std::string& name) {
    return UltraCanvas::NormalizePath(UltraCanvas::GetResourcesDir() + "media/icons/" + name);
}
// The application's own mark: the window icon, the taskbar entry, the start
// page logo and the toolbar logo all read this one file.
inline std::string AppIconPath() {
    return UltraCanvas::NormalizePath(UltraCanvas::GetResourcesDir() +
                                      "media/appicon/UltraSocial.png");
}

// ---------------------------------------------------------------------------
// Element styles
// ---------------------------------------------------------------------------
// The filled accent button: one per surface, for the action the user came for.
inline UltraCanvas::ButtonStyle PrimaryButton() {
    UltraCanvas::ButtonStyle s;
    s.normalColor      = kAccent;
    s.hoverColor       = kAccentHover;
    s.pressedColor     = kAccentPressed;
    s.disabledColor    = UltraCanvas::Color(191, 205, 235, 255);
    s.normalTextColor  = UltraCanvas::Colors::White;
    s.hoverTextColor   = UltraCanvas::Colors::White;
    s.pressedTextColor = UltraCanvas::Colors::White;
    s.disabledTextColor= UltraCanvas::Colors::White;
    s.borderWidth      = 0.0f;
    s.borderColor      = kAccent;
    s.cornerRadius     = kControlRadius;
    s.fontSize         = kSizeBody;
    return s;
}

// The quiet white button with a hairline border, for everything else.
inline UltraCanvas::ButtonStyle SecondaryButton() {
    UltraCanvas::ButtonStyle s;
    s.normalColor      = kCardBackground;
    s.hoverColor       = kRowHover;
    s.pressedColor     = kRowSelected;
    s.disabledColor    = kPageBackground;
    s.normalTextColor  = kTextPrimary;
    s.hoverTextColor   = kTextPrimary;
    s.pressedTextColor = kTextPrimary;
    s.disabledTextColor= kTextMuted;
    s.borderWidth      = 1.0f;
    s.borderColor      = kCardBorder;
    s.cornerRadius     = kControlRadius;
    s.fontSize         = kSizeBody;
    return s;
}

inline void StylePrimary(const std::shared_ptr<UltraCanvas::UltraCanvasButton>& b) {
    if (b) b->SetStyle(PrimaryButton());
}
inline void StyleSecondary(const std::shared_ptr<UltraCanvas::UltraCanvasButton>& b) {
    if (b) b->SetStyle(SecondaryButton());
}

// Every button sizes to its label: its width follows the text (and icon), so
// a longer label - a translation, "Posting…" - is never cut short. `minWidth`
// keeps a floor so short labels still look like buttons. The same rule as
// UltraMail's Theme::FitToLabel.
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

// A glyph from media/icons left of the label, tinted with the label's colour.
inline void SetButtonIcon(const std::shared_ptr<UltraCanvas::UltraCanvasButton>& b,
                          const std::string& iconName, int size = kButtonIcon) {
    if (!b) return;
    b->SetIcon(IconPath(iconName));
    b->SetIconPosition(UltraCanvas::ButtonIconPosition::Left);
    b->SetIconSize(size, size);
    b->SetIconSpacing(6);
    b->SetUseIconAsMask(true);
}

// One call for the common case: a themed button that fits its label, with an
// optional glyph.
inline std::shared_ptr<UltraCanvas::UltraCanvasButton>
MakeButton(const std::string& id, const std::string& text, bool primary,
           const std::string& iconName = "", float minWidth = 0.0f,
           float height = kControlHeight) {
    auto button = UltraCanvas::CreateButton(id, 0, 0, minWidth, height, text);
    FitToLabel(button, minWidth);
    if (primary) StylePrimary(button); else StyleSecondary(button);
    if (!iconName.empty()) SetButtonIcon(button, iconName);
    return button;
}

// A white card: the surface every content block sits on, as a flex column.
inline void ApplyCard(const std::shared_ptr<UltraCanvas::UltraCanvasContainer>& c,
                      float gap = kGap) {
    if (!c) return;
    c->SetBackgroundColor(kCardBackground);
    c->SetBorders(1.0f, kCardBorder, kCardRadius);
    c->SetPadding(kCardPadV, kCardPadH);
    c->layout.SetFlexColumn()
             .SetFlexGap(gap)
             .SetFlexAlignItems(UltraCanvas::CSSLayout::AlignItems::Stretch);
}

// Text inputs: hairline border, accent focus ring, body-size text.
inline UltraCanvas::TextInputStyle InputStyle() {
    UltraCanvas::TextInputStyle s;
    s.borderColor      = kCardBorder;
    s.focusBorderColor = kAccent;
    s.validBorderColor = kCardBorder;
    s.textColor        = kTextPrimary;
    s.placeholderColor = kTextMuted;
    s.borderRadius     = static_cast<int>(kControlRadius);
    s.paddingLeft      = 6;
    s.paddingRight     = 6;
    s.fontStyle.fontSize = kSizeBody;
    return s;
}
inline void StyleInput(const std::shared_ptr<UltraCanvas::UltraCanvasTextInput>& in) {
    if (in) in->SetStyle(InputStyle());
}

// Multi-line text: the same hairline frame as an input, more inner padding.
// The frame is the element's own border (the style's borderColor is only the
// line-number gutter's rule). A template so this header need not pull in
// UltraCanvasTextArea.h (which drags X11 macros into every header included
// after it).
template <typename TextAreaPtr>
inline void StyleTextArea(const TextAreaPtr& ta) {
    if (!ta) return;
    ta->SetBorders(1.0f, kCardBorder, kControlRadius);
    auto& s = ta->GetStyle();
    s.borderColor      = kCardBorder;
    s.backgroundColor  = kCardBackground;
    s.fontColor        = kTextPrimary;
    s.placeholderColor = kTextMuted;
    s.textPadding      = 8.0f;
    ta->SetFontSize(kSizeBody + 2.0f);
}

// Dropdowns: body-size text (the element's default is larger).
template <typename DropdownPtr>
inline void StyleDropdown(const DropdownPtr& dd) {
    if (!dd) return;
    auto s = dd->GetStyle();
    s.fontSize = kSizeBody;
    dd->SetStyle(s);
}

// Checkboxes: a white rounded box with the accent tick, body-size text.
inline void StyleCheckbox(const std::shared_ptr<UltraCanvas::UltraCanvasCheckbox>& box) {
    if (!box) return;
    box->SetStyle(UltraCanvas::CheckboxStyle::Rounded);
    UltraCanvas::CheckboxVisualStyle s = box->GetVisualStyle();
    s.boxColor            = kCardBackground;
    s.boxBorderColor      = kTextMuted;
    s.boxHoverColor       = kAccentSoft;
    s.checkmarkColor      = kAccent;
    s.checkmarkHoverColor = kAccentHover;
    s.cornerRadius        = 4.0f;
    s.checkmarkThickness  = 2.0f;
    s.base.textColor      = kTextPrimary;
    s.base.textHoverColor = kTextPrimary;
    s.base.fontFamily.clear();   // the UI font, like every label (the element's default is Arial)
    s.base.fontSize       = kSizeBody + 0.5f;
    box->SetVisualStyle(s);
}

// ---------------------------------------------------------------------------
// Label helpers
// ---------------------------------------------------------------------------
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

// A card heading: bold, the heading size.
inline std::shared_ptr<UltraCanvas::UltraCanvasLabel>
MakeHeading(const std::string& id, const std::string& text) {
    return MakeText(id, text, kSizeHeading, kTextPrimary, UltraCanvas::FontWeight::Bold);
}

// A line of text that wraps at the width its column gives it.
inline std::shared_ptr<UltraCanvas::UltraCanvasLabel>
MakeWrapped(const std::string& id, const std::string& text, float size = kSizeBody,
            const UltraCanvas::Color& color = kTextSecondary) {
    auto label = MakeText(id, text, size, color);
    label->SetWrap(UltraCanvas::TextWrap::WrapWord);
    label->layoutItem.SetAlignSelf(UltraCanvas::CSSLayout::AlignSelf::Stretch);
    return label;
}

// A 1px horizontal rule for flex columns.
inline std::shared_ptr<UltraCanvas::UltraCanvasContainer> MakeDivider(const std::string& id) {
    auto rule = UltraCanvas::CreateContainer(id, 0, 0, 0, 1);
    rule->SetBackgroundColor(kDivider);
    rule->layoutItem.SetFlexShrink(0).SetAlignSelf(UltraCanvas::CSSLayout::AlignSelf::Stretch);
    return rule;
}

// ---------------------------------------------------------------------------
// Networks
// ---------------------------------------------------------------------------
// What a network is called on screen.
inline std::string NetworkDisplayName(SocialNetwork network) {
    switch (network) {
        case SocialNetwork::Mastodon: return "Mastodon";
        case SocialNetwork::Bluesky:  return "Bluesky";
        case SocialNetwork::Telegram: return "Telegram";
        case SocialNetwork::Reddit:   return "Reddit";
        case SocialNetwork::X:        return "X";
        case SocialNetwork::LinkedIn: return "LinkedIn";
        case SocialNetwork::Facebook: return "Facebook Page";
    }
    return "Network";
}

// The network's own colour and initial, for the avatar beside an account.
// Letters, not logos: the trademarks are the networks' to draw.
inline UltraCanvas::Color NetworkColor(SocialNetwork network) {
    switch (network) {
        case SocialNetwork::Mastodon: return UltraCanvas::Color( 99, 100, 255);
        case SocialNetwork::Bluesky:  return UltraCanvas::Color( 17, 133, 254);
        case SocialNetwork::Telegram: return UltraCanvas::Color( 34, 158, 217);
        case SocialNetwork::Reddit:   return UltraCanvas::Color(255,  69,   0);
        case SocialNetwork::X:        return UltraCanvas::Color( 15,  20,  25);
        case SocialNetwork::LinkedIn: return UltraCanvas::Color( 10, 102, 194);
        case SocialNetwork::Facebook: return UltraCanvas::Color( 24, 119, 242);
    }
    return kAccent;
}

inline std::string NetworkInitial(SocialNetwork network) {
    switch (network) {
        case SocialNetwork::Mastodon: return "M";
        case SocialNetwork::Bluesky:  return "B";
        case SocialNetwork::Telegram: return "T";
        case SocialNetwork::Reddit:   return "R";
        case SocialNetwork::X:        return "X";
        case SocialNetwork::LinkedIn: return "in";
        case SocialNetwork::Facebook: return "f";
    }
    return "?";
}

// The network's initial, white in a round tile of its colour.
inline std::shared_ptr<UltraCanvas::UltraCanvasContainer>
MakeNetworkAvatar(const std::string& id, SocialNetwork network, float side = kAvatarSize) {
    auto box = UltraCanvas::CreateContainer(id, 0, 0, side, side);
    box->SetBackgroundColor(NetworkColor(network));
    box->SetBorders(0.0f, UltraCanvas::Colors::Transparent, side * 0.5f);
    box->layout.SetFlexRow()
               .SetFlexJustifyContent(UltraCanvas::CSSLayout::JustifyContent::Center)
               .SetFlexAlignItems(UltraCanvas::CSSLayout::AlignItems::Center);
    box->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    auto letter = MakeText(id + ".letter", NetworkInitial(network), side * 0.42f,
                           UltraCanvas::Colors::White, UltraCanvas::FontWeight::Bold);
    letter->SetAlignment(UltraCanvas::TextAlignment::Center);
    box->AddChild(letter);
    box->SetTooltip(NetworkDisplayName(network));
    return box;
}

// Containers that hold one-line chrome must never raise a scrollbar (which
// would paint over the row when space is tight).
inline void NoScrollbars(const std::shared_ptr<UltraCanvas::UltraCanvasContainer>& c) {
    if (!c) return;
    auto s = c->GetContainerStyle();
    s.autoShowScrollbars = false;
    c->SetContainerStyle(s);
}

} // namespace Theme
} // namespace UltraSocial
