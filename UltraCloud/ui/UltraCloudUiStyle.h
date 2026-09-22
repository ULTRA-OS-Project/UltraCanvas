// UltraCloud/ui/UltraCloudUiStyle.h
// The palette and control styles shared by UltraCloud's dialogs, matched to
// the ULTRA OS application look (white surfaces, hairline borders, one
// filled accent button per dialog). Values only — every element is styled
// through its own SetStyle API.
// Version: 0.2.0
// Last Modified: 2026-09-22
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasButton.h"
#include "UltraCanvasColumnsTreeView.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasFormLayout.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"

#include <memory>

namespace UltraCloud {
namespace UiStyle {

inline const UltraCanvas::Color kSurface       {255, 255, 255};
inline const UltraCanvas::Color kBorder        {226, 229, 234};
inline const UltraCanvas::Color kDivider       {234, 236, 240};
inline const UltraCanvas::Color kHeader        {249, 250, 251};
inline const UltraCanvas::Color kTextPrimary   { 24,  28,  35};
inline const UltraCanvas::Color kTextSecondary {104, 112, 125};
inline const UltraCanvas::Color kTextMuted     {142, 150, 163};
inline const UltraCanvas::Color kDanger        {197,  48,  48};
inline const UltraCanvas::Color kAccent        { 37,  99, 235};
inline const UltraCanvas::Color kAccentHover   { 29,  78, 216};
inline const UltraCanvas::Color kAccentPressed { 30,  64, 175};
inline const UltraCanvas::Color kRowHover      {243, 245, 248};
inline const UltraCanvas::Color kRowSelected   {226, 236, 253};

constexpr float kControlHeight = 32.0f;
constexpr float kRadius        = 6.0f;
constexpr float kFontSize      = 13.0f;
constexpr float kPadding       = 20.0f;
constexpr float kGap           = 10.0f;

inline UltraCanvas::ButtonStyle PrimaryButton() {
    UltraCanvas::ButtonStyle s;
    s.normalColor = kAccent; s.hoverColor = kAccentHover; s.pressedColor = kAccentPressed;
    s.disabledColor = UltraCanvas::Color(191, 205, 235, 255);
    s.normalTextColor = s.hoverTextColor = s.pressedTextColor = s.disabledTextColor =
        UltraCanvas::Colors::White;
    s.borderWidth = 0.0f; s.borderColor = kAccent;
    s.cornerRadius = kRadius; s.fontSize = kFontSize;
    return s;
}

inline UltraCanvas::ButtonStyle SecondaryButton() {
    UltraCanvas::ButtonStyle s;
    s.normalColor = kSurface; s.hoverColor = kRowHover; s.pressedColor = kRowSelected;
    s.disabledColor = kHeader;
    s.normalTextColor = s.hoverTextColor = s.pressedTextColor = kTextPrimary;
    s.disabledTextColor = kTextMuted;
    s.borderWidth = 1.0f; s.borderColor = kBorder;
    s.cornerRadius = kRadius; s.fontSize = kFontSize;
    return s;
}

inline void StylePrimary(const std::shared_ptr<UltraCanvas::UltraCanvasButton>& b) {
    if (b) b->SetStyle(PrimaryButton());
}
inline void StyleSecondary(const std::shared_ptr<UltraCanvas::UltraCanvasButton>& b) {
    if (b) b->SetStyle(SecondaryButton());
}

inline void StyleInput(const std::shared_ptr<UltraCanvas::UltraCanvasTextInput>& in) {
    if (!in) return;
    UltraCanvas::TextInputStyle s;
    s.borderColor = kBorder; s.focusBorderColor = kAccent; s.validBorderColor = kBorder;
    s.textColor = kTextPrimary; s.placeholderColor = kTextMuted;
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
    label->SetTextColor(kTextSecondary);
    return label;
}

// The quiet list header used by the file picker.
inline UltraCanvas::TreeColumnStyle ListHeader() {
    UltraCanvas::TreeColumnStyle s;
    s.headerHeight      = 28;
    s.headerBackground  = kHeader;
    s.headerTextColor   = kTextSecondary;
    s.headerBorderColor = kDivider;
    s.columnGap         = 10;
    return s;
}

} // namespace UiStyle
} // namespace UltraCloud
