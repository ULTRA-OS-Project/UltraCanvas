// Apps/UltraMail/ui/UltraMailTheme.h
// One place for UltraMail's colours, type sizes, metrics and the small
// styling helpers every window uses. It exists so the main window, the
// composer, the contact manager and the dialogs cannot drift apart: the same
// "secondary text" is one grey everywhere, every card has the same corner
// radius, every primary button looks the same.
//
// Nothing here paints anything. These are values handed to catalogue elements
// through their own SetStyle / SetTextColor / SetBorders APIs, per the
// framework rule that applications never hand-roll a widget.
// Version: 0.2.0 - type scale and metrics matched to UltraFiler's 9pt UI font
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasButton.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasGroupBox.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraMail {
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
inline const UltraCanvas::Color kSidebar        {249, 250, 251};

inline const UltraCanvas::Color kTextPrimary    { 24,  28,  35};
inline const UltraCanvas::Color kTextSecondary  {104, 112, 125};
inline const UltraCanvas::Color kTextMuted      {142, 150, 163};

inline const UltraCanvas::Color kAccent         { 37,  99, 235};   // primary actions, selection
inline const UltraCanvas::Color kAccentHover    { 29,  78, 216};
inline const UltraCanvas::Color kAccentPressed  { 30,  64, 175};
inline const UltraCanvas::Color kAccentSoft     {232, 240, 254};   // tinted surfaces (selected)
inline const UltraCanvas::Color kRowHover       {243, 245, 248};
inline const UltraCanvas::Color kRowSelected    {226, 236, 253};

// Counter tints: a soft background with a strong text colour of the same
// hue. Blue = new today, green = unread from before today, orange = waiting
// for a reply — the same three hues as the original design, toned down.
inline const UltraCanvas::Color kNewTodayTint   {219, 234, 254};
inline const UltraCanvas::Color kNewTodayText   { 29,  78, 216};
inline const UltraCanvas::Color kUnreadTint     {220, 252, 231};
inline const UltraCanvas::Color kUnreadText     { 21, 128,  61};
inline const UltraCanvas::Color kWaitingTint    {255, 237, 213};
inline const UltraCanvas::Color kWaitingText    {194,  65,  12};

// ---------------------------------------------------------------------------
// Type
// ---------------------------------------------------------------------------
// The scale matches UltraFiler's UI font (9pt for controls, lists and body
// text), so the two apps read the same size side by side.
constexpr float kSizeTitle     = 13.0f;   // window / pane titles
constexpr float kSizeHeading   = 11.0f;   // card headings, account name
constexpr float kSizeBody      = 9.0f;
constexpr float kSizeSecondary = 8.5f;
constexpr float kSizeSmall     = 8.0f;

// ---------------------------------------------------------------------------
// Metrics
// ---------------------------------------------------------------------------
constexpr float kPagePadding   = 10.0f;   // gutter between the window edge and content
constexpr float kGap           = 8.0f;    // gap between cards / rows
constexpr float kInnerGap      = 6.0f;    // gap inside a row
constexpr float kCardRadius    = 8.0f;
constexpr float kControlRadius = 5.0f;
constexpr float kControlHeight = 24.0f;   // buttons and inputs (UltraFiler: 22)
constexpr float kToolbarHeight = 28.0f;
constexpr float kAvatarSize    = 28.0f;   // the provider / contact initial square

// ---------------------------------------------------------------------------
// Element styles
// ---------------------------------------------------------------------------
// The filled accent button: one per window, for the action the user came for.
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

// A white card: the surface every content block sits on.
inline void ApplyCard(const std::shared_ptr<UltraCanvas::UltraCanvasContainer>& c,
                      float radius = kCardRadius) {
    if (!c) return;
    c->SetBackgroundColor(kCardBackground);
    c->SetBorders(1.0f, kCardBorder, radius);
}

// A group box drawn as a card with a quiet caption.
inline UltraCanvas::GroupBoxVisualStyle CardGroupBox(float contentPadding = kGap) {
    UltraCanvas::GroupBoxVisualStyle s = UltraCanvas::GroupBoxVisualStyle::Card();
    s.backgroundColor       = kCardBackground;
    s.borderColor           = kCardBorder;
    s.cornerRadius          = kCardRadius;
    s.titleColor            = kTextSecondary;
    s.titleFont.fontSize    = kSizeBody;
    s.titleFont.fontWeight  = UltraCanvas::FontWeight::Bold;
    s.titleIndent           = 8.0f;
    s.titleVerticalPadding  = 4.0f;
    s.contentPadding        = contentPadding;
    return s;
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

// Multi-line text: the same hairline border, more inner padding. A template
// so this header need not pull in UltraCanvasTextArea.h (which drags X11
// macros into every engine header included after it).
template <typename TextAreaPtr>
inline void StyleTextArea(const TextAreaPtr& ta, bool bordered = true) {
    if (!ta) return;
    auto& s = ta->GetStyle();
    s.borderColor     = bordered ? kCardBorder : UltraCanvas::Colors::Transparent;
    s.backgroundColor = kCardBackground;
    s.fontColor       = kTextPrimary;
    s.textPadding     = 6.0f;
    ta->SetFontSize(kSizeBody + 1.0f);
}

// Dropdowns: body-size text (the element's default is larger). A template for
// the same reason as StyleTextArea.
template <typename DropdownPtr>
inline void StyleDropdown(const DropdownPtr& dd) {
    if (!dd) return;
    auto s = dd->GetStyle();
    s.fontSize = kSizeBody;
    dd->SetStyle(s);
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

// A 1px horizontal rule for flex columns.
inline std::shared_ptr<UltraCanvas::UltraCanvasContainer> MakeDivider(const std::string& id) {
    auto rule = UltraCanvas::CreateContainer(id, 0, 0, 0, 1);
    rule->SetBackgroundColor(kDivider);
    rule->layoutItem.SetAlignSelf(UltraCanvas::CSSLayout::AlignSelf::Stretch);
    return rule;
}

// The initial-in-a-tinted-square avatar used for accounts and contacts.
inline std::shared_ptr<UltraCanvas::UltraCanvasContainer>
MakeAvatar(const std::string& id, const std::string& initial, float side = kAvatarSize,
           const UltraCanvas::Color& tint = kAccentSoft,
           const UltraCanvas::Color& text = kAccent) {
    auto box = UltraCanvas::CreateContainer(id, 0, 0, side, side);
    box->SetBackgroundColor(tint);
    box->SetBorders(0.0f, UltraCanvas::Colors::Transparent, side * 0.25f);
    box->layout.SetFlexRow()
               .SetFlexJustifyContent(UltraCanvas::CSSLayout::JustifyContent::Center)
               .SetFlexAlignItems(UltraCanvas::CSSLayout::AlignItems::Center);
    auto letter = MakeText(id + ".letter", initial, side * 0.45f, text,
                           UltraCanvas::FontWeight::Bold);
    letter->SetAlignment(UltraCanvas::TextAlignment::Center);
    box->AddChild(letter);
    return box;
}

// A container that fires onActivate on a left click anywhere inside it
// (account tiles, sidebar entries, contact rows).
class ClickSurface : public UltraCanvas::UltraCanvasContainer {
public:
    ClickSurface(const std::string& id, std::function<void()> onActivate)
        : UltraCanvas::UltraCanvasContainer(id, 0, 0, 0, 0), onActivate_(std::move(onActivate)) {}

    bool OnEvent(const UltraCanvas::UCEvent& event) override {
        if (!IsVisible() || IsDisabled()) return false;
        if (event.type == UltraCanvas::UCEventType::MouseDown &&
            event.button == UltraCanvas::UCMouseButton::Left) {
            if (onActivate_) onActivate_();
            return true;
        }
        return UltraCanvas::UltraCanvasContainer::OnEvent(event);
    }

private:
    std::function<void()> onActivate_;
};

} // namespace Theme
} // namespace UltraMail
