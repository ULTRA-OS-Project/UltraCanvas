// Apps/UltraStoreAdmin/ui/AdminTheme.h
// The admin window's look, after the UltraStore shop design (the canvas
// "UltraStore Shop Design", 2026-11-05, its admin artboards): the same
// tokens as the store's StoreTheme.h - a white ground, ink text, the
// UltraStore blue as the one accent, Manrope for display text over Inter
// for body text (both in media/fonts), rounded surfaces and pill buttons -
// at the denser sizes a form of sixty fields needs. The few helpers every
// page uses: a wrapping text, a heading, a caption, a styled button, a
// plain row or column, a surface, and Field (a labelled control). Nothing
// here paints; the elements do.
// Version: 0.2.0 - the shop design's tokens, fonts and pills
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasButton.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasUtils.h"

#include <memory>
#include <string>

namespace UltraStoreAdmin::Ui::Theme {

// ===== colours (the design's tokens) =====
inline const UltraCanvas::Color kPageBackground {255, 255, 255};   // the ground
inline const UltraCanvas::Color kSurface        {255, 255, 255};
inline const UltraCanvas::Color kSurfaceSoft    {248, 250, 252};   // #F8FAFC
inline const UltraCanvas::Color kSurfaceMuted   {241, 245, 249};   // #F1F5F9
inline const UltraCanvas::Color kBorder         {226, 232, 240};   // #E2E8F0
inline const UltraCanvas::Color kBorderStrong   {203, 213, 225};   // #CBD5E1
inline const UltraCanvas::Color kTextPrimary    { 15,  23,  42};   // #0F172A, the ink
inline const UltraCanvas::Color kTextBody       { 51,  65,  85};   // #334155
inline const UltraCanvas::Color kTextSecondary  {100, 116, 139};   // #64748B
inline const UltraCanvas::Color kTextMuted      {148, 163, 184};   // #94A3B8
inline const UltraCanvas::Color kAccent         { 37,  99, 235};   // #2563EB
inline const UltraCanvas::Color kAccentHover    { 29,  78, 216};
inline const UltraCanvas::Color kAccentPressed  { 30,  64, 175};
inline const UltraCanvas::Color kAccentSoft     {219, 234, 254};   // #DBEAFE
inline const UltraCanvas::Color kAccentSoftText { 29,  78, 216};
inline const UltraCanvas::Color kRowHover       {241, 245, 249};
inline const UltraCanvas::Color kRowSelected    {219, 234, 254};
inline const UltraCanvas::Color kDanger         {220,  38,  38};
inline const UltraCanvas::Color kDangerHover    {185,  28,  28};
inline const UltraCanvas::Color kDangerPressed  {153,  27,  27};
inline const UltraCanvas::Color kSuccess        { 21, 128,  61};
inline const UltraCanvas::Color kTrack          {226, 232, 240};

// ===== type =====
inline const char* kDisplayFont = "Manrope";
inline const char* kBodyFont    = "Inter";

// ===== sizes: the store's, a notch denser for the forms =====
constexpr float kPagePadding   = 24.0f;
constexpr float kGap           = 12.0f;
constexpr float kInnerGap      = 8.0f;
constexpr float kControlHeight = 34.0f;   // a pill button, a field
constexpr float kControlRadius = 17.0f;   // half the height: a pill
constexpr float kSurfaceRadius = 16.0f;
constexpr float kHeaderHeight  = 44.0f;
constexpr float kStatusHeight  = 24.0f;

// In the flow of its container and never squeezed by it: a column's child
// keeps its measured height when the column is a viewport onto taller content
// (the scrollbar is the answer to that, not a shorter card), and an element
// made with x, y arguments is laid out, not pinned at them.
template <class T>
std::shared_ptr<T> Pin(std::shared_ptr<T> element) {
    if (!element) return element;
    if (element->layoutItem.positionType == UltraCanvas::CSSLayout::PositionType::AbsoluteUI ||
        element->layoutItem.positionType == UltraCanvas::CSSLayout::PositionType::Absolute) {
        element->layoutItem.SetPositionType(UltraCanvas::CSSLayout::PositionType::Static);
        element->layoutItem.position.reset();
    }
    element->layoutItem.SetFlexShrink(0);
    return element;
}

inline std::string Icon(const char* name) {
    return UltraCanvas::NormalizePath(UltraCanvas::GetResourcesDir() + "media/icons/" + name);
}

// A label that wraps to the width it is given and sizes to its lines.
inline std::shared_ptr<UltraCanvas::UltraCanvasLabel> Text(const std::string& id, const std::string& text,
                                                           float size = 13.0f,
                                                           UltraCanvas::FontWeight weight = UltraCanvas::FontWeight::Normal,
                                                           const UltraCanvas::Color& color = kTextPrimary,
                                                           const char* family = kBodyFont) {
    auto label = std::make_shared<UltraCanvas::UltraCanvasLabel>(id, text);
    label->SetFont(family, size, weight);
    label->SetTextColor(color);
    label->SetWrap(UltraCanvas::TextWrap::WrapWordChar);
    label->SetAlignment(UltraCanvas::TextAlignment::Left, UltraCanvas::VerticalAlignment::Top);
    return Pin(label);
}

// A page's heading, in Manrope.
inline std::shared_ptr<UltraCanvas::UltraCanvasLabel> Heading(const std::string& id, const std::string& text) {
    return Text(id, text, 24.0f, UltraCanvas::FontWeight::Bold, kTextPrimary, kDisplayFont);
}

inline std::shared_ptr<UltraCanvas::UltraCanvasLabel> Caption(const std::string& id, const std::string& text) {
    return Text(id, text, 12.0f, UltraCanvas::FontWeight::Normal, kTextSecondary);
}

// Primary: the accent, white text. Secondary: white with a border. Danger:
// red. All pills, in Inter.
enum class ButtonKind { Primary, Secondary, Danger };

inline UltraCanvas::ButtonStyle ButtonStyleFor(ButtonKind kind) {
    UltraCanvas::ButtonStyle s;
    s.borderWidth  = kind == ButtonKind::Secondary ? 1.0f : 0.0f;
    s.cornerRadius = kControlRadius;
    s.fontFamily   = kBodyFont;
    s.fontSize     = 13.0f;
    s.fontWeight   = UltraCanvas::FontWeight::Bold;
    s.focusedColor = kAccent;
    switch (kind) {
        case ButtonKind::Primary:
            s.normalColor = kAccent; s.hoverColor = kAccentHover; s.pressedColor = kAccentPressed;
            s.disabledColor = UltraCanvas::Color(191, 205, 235, 255);
            s.normalTextColor = s.hoverTextColor = s.pressedTextColor = s.disabledTextColor = UltraCanvas::Colors::White;
            s.borderColor = kAccent;
            break;
        case ButtonKind::Danger:
            s.normalColor = kDanger; s.hoverColor = kDangerHover; s.pressedColor = kDangerPressed;
            s.disabledColor = UltraCanvas::Color(240, 180, 180, 255);
            s.normalTextColor = s.hoverTextColor = s.pressedTextColor = s.disabledTextColor = UltraCanvas::Colors::White;
            s.borderColor = kDanger;
            break;
        case ButtonKind::Secondary:
            s.normalColor = kSurface; s.hoverColor = kRowHover; s.pressedColor = kRowSelected;
            s.disabledColor = kSurfaceMuted;
            s.normalTextColor = s.hoverTextColor = s.pressedTextColor = kTextPrimary;
            s.disabledTextColor = kTextMuted;
            s.borderColor = kBorderStrong;
            break;
    }
    return s;
}

inline std::shared_ptr<UltraCanvas::UltraCanvasButton> Button(const std::string& id, const std::string& text,
                                                              float width, ButtonKind kind = ButtonKind::Secondary) {
    auto b = UltraCanvas::CreateButton(id, 0, 0, width, kControlHeight, text);
    b->SetStyle(ButtonStyleFor(kind));
    return Pin(b);
}

inline void Style(const std::shared_ptr<UltraCanvas::UltraCanvasButton>& b, ButtonKind kind) {
    if (b) b->SetStyle(ButtonStyleFor(kind));
}

// A container that arranges and never scrolls.
inline std::shared_ptr<UltraCanvas::UltraCanvasContainer> Row(const std::string& id, float gap = kInnerGap,
                                                              bool wrap = false) {
    auto row = std::make_shared<UltraCanvas::UltraCanvasContainer>(id);
    UltraCanvas::ContainerStyle style;
    style.autoShowScrollbars = false;
    row->SetContainerStyle(style);
    row->layout.SetFlexRow()
               .SetFlexWrap(wrap ? UltraCanvas::CSSLayout::FlexWrap::Wrap : UltraCanvas::CSSLayout::FlexWrap::NoWrap)
               .SetFlexGap(gap)
               .SetFlexAlignItems(UltraCanvas::CSSLayout::AlignItems::Center);
    return Pin(row);
}

inline std::shared_ptr<UltraCanvas::UltraCanvasContainer> Column(const std::string& id, float gap = kInnerGap) {
    auto col = std::make_shared<UltraCanvas::UltraCanvasContainer>(id);
    UltraCanvas::ContainerStyle style;
    style.autoShowScrollbars = false;
    col->SetContainerStyle(style);
    col->layout.SetFlexColumn()
               .SetFlexGap(gap)
               .SetFlexAlignItems(UltraCanvas::CSSLayout::AlignItems::Stretch);
    return Pin(col);
}

// A white rounded surface with the design's corners, a column.
inline std::shared_ptr<UltraCanvas::UltraCanvasContainer> Surface(const std::string& id, float gap = kInnerGap) {
    auto col = Column(id, gap);
    col->SetBackgroundColor(kSurface);
    col->SetBorders(1.0f, kBorder, kSurfaceRadius);
    col->SetPadding(20.0f);
    return col;
}

// A labelled control on one line: the label 180 wide, the control after it.
inline std::shared_ptr<UltraCanvas::UltraCanvasContainer> Field(const std::string& id, const std::string& label,
                                                                const std::shared_ptr<UltraCanvas::UltraCanvasUIElement>& control,
                                                                bool grow = false) {
    auto row = Row(id, kGap);
    auto text = Text(id + ".label", label, 13.0f, UltraCanvas::FontWeight::Normal, kTextBody);
    text->SetElementSize(UltraCanvas::CSSLayout::Dimension::Px(180), UltraCanvas::CSSLayout::Dimension::Auto());
    row->AddChild(text);
    Pin(control);
    if (grow) control->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    row->AddChild(control);
    return row;
}

} // namespace UltraStoreAdmin::Ui::Theme
