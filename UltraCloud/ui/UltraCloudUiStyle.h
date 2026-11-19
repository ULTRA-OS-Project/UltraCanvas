// UltraCloud/ui/UltraCloudUiStyle.h
// The palette and control styles shared by UltraCloud's dialogs (the account
// form, the file picker), matched to the ULTRA OS application look: the
// theme's surfaces and hairlines, one filled accent button per dialog. Values
// only - every element is styled through its own SetStyle API.
// Version: 0.3.0 - the colours are the current theme's roles, read when a dialog is built
//                  (Surface(), TextPrimary() ...): the fixed light palette made the account
//                  and picker dialogs white windows inside a dark UltraFiler or UltraPress,
//                  with the theme-following dropdowns black on them and a checkbox's label
//                  white on white; the buttons are the theme's Primary and Standard parts
// Version: 0.2.0
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasButton.h"
#include "UltraCanvasColumnsTreeView.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasFormLayout.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTheme.h"

#include <memory>

namespace UltraCloud {
namespace UiStyle {

// The colours: the theme's roles, light or dark, so a dialog built under a
// dark look is dark.
inline const UltraCanvas::ThemeRoles& Roles() { return UltraCanvas::UltraCanvasTheme::Current().roles; }
inline UltraCanvas::Color Surface()       { return Roles().panel; }      // the dialog
inline UltraCanvas::Color Border()        { return Roles().line; }
inline UltraCanvas::Color Divider()       { return Roles().line; }
inline UltraCanvas::Color Header()        { return Roles().raised; }     // a list's column header
inline UltraCanvas::Color TextPrimary()   { return Roles().ink; }
inline UltraCanvas::Color TextSecondary() { return Roles().inkMuted; }
inline UltraCanvas::Color TextMuted()     { return Roles().inkFaint; }
inline UltraCanvas::Color Danger()        { return Roles().danger; }
inline UltraCanvas::Color Accent()        { return Roles().accent; }
inline UltraCanvas::Color RowHover()      { return Roles().hover; }
inline UltraCanvas::Color RowSelected()   { return Roles().accentSoft; }

constexpr float kControlHeight = 32.0f;
constexpr float kRadius        = 6.0f;
constexpr float kFontSize      = 13.0f;
constexpr float kPadding       = 20.0f;
constexpr float kGap           = 10.0f;

// The theme's button parts in the dialogs' shape: the accent for the one
// action the dialog is for, the raised surface with the control border for
// the others.
inline UltraCanvas::ButtonStyle ThemedButton(UltraCanvas::ButtonVariant variant) {
    UltraCanvas::ButtonStyle shape;
    shape.cornerRadius = kRadius;
    shape.borderWidth = variant == UltraCanvas::ButtonVariant::Standard ? 1.0f : 0.0f;
    UltraCanvas::ButtonStyle s =
        UltraCanvas::ButtonStyle::FromTheme(UltraCanvas::UltraCanvasTheme::Current(), shape, variant);
    s.fontSize = kFontSize;
    s.fontStyle.fontSize = kFontSize;
    return s;
}
inline UltraCanvas::ButtonStyle PrimaryButton()   { return ThemedButton(UltraCanvas::ButtonVariant::Primary); }
inline UltraCanvas::ButtonStyle SecondaryButton() { return ThemedButton(UltraCanvas::ButtonVariant::Standard); }

inline void StylePrimary(const std::shared_ptr<UltraCanvas::UltraCanvasButton>& b) {
    if (b) b->SetStyle(PrimaryButton());
}
inline void StyleSecondary(const std::shared_ptr<UltraCanvas::UltraCanvasButton>& b) {
    if (b) b->SetStyle(SecondaryButton());
}

inline void StyleInput(const std::shared_ptr<UltraCanvas::UltraCanvasTextInput>& in) {
    if (!in) return;
    // The theme's field (its well, ink, caret and selection) in the dialogs'
    // shape; a default style kept the light look's white field.
    UltraCanvas::TextInputStyle s = UltraCanvas::TextInputStyle::FromTheme();
    s.validBorderColor = s.borderColor;
    s.borderRadius = static_cast<int>(kRadius);
    s.paddingLeft = 10; s.paddingRight = 10;
    s.fontStyle.fontSize = kFontSize;
    in->SetStyle(s);
}

// A form caption: secondary colour, one line, and no width of its own - the
// form grid's `auto` column measures the text and is as wide as the widest
// caption in it. A hard-coded width is what cuts a caption off in the first
// language whose word for it is longer.
inline std::shared_ptr<UltraCanvas::UltraCanvasLabel>
MakeCaption(const std::string& id, const std::string& text) {
    auto label = UltraCanvas::CreateFormCaption(id, text);
    label->SetFontSize(kFontSize);
    label->SetTextColor(TextSecondary());
    return label;
}

// The quiet list header used by the file picker.
inline UltraCanvas::TreeColumnStyle ListHeader() {
    UltraCanvas::TreeColumnStyle s = UltraCanvas::TreeColumnStyle::FromTheme();
    s.headerHeight      = 28;
    s.headerBackground  = Header();
    s.headerTextColor   = TextSecondary();
    s.headerBorderColor = Divider();
    s.columnGap         = 10;
    return s;
}

} // namespace UiStyle
} // namespace UltraCloud
