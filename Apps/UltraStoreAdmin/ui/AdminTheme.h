// Apps/UltraStoreAdmin/ui/AdminTheme.h
// The admin window's colours, sizes and the few helpers every page uses: a
// wrapping text, a heading, a caption, a styled button, a plain row or
// column. Nothing here paints; the elements do.
// (The store window's StoreTheme.h, with Field.)
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

inline const UltraCanvas::Color kPageBackground {245, 246, 248};
inline const UltraCanvas::Color kSurface        {255, 255, 255};
inline const UltraCanvas::Color kBorder         {226, 229, 234};
inline const UltraCanvas::Color kTextPrimary    { 24,  28,  35};
inline const UltraCanvas::Color kTextSecondary  {104, 112, 125};
inline const UltraCanvas::Color kTextMuted      {142, 150, 163};
inline const UltraCanvas::Color kAccent         { 37,  99, 235};
inline const UltraCanvas::Color kAccentHover    { 29,  78, 216};
inline const UltraCanvas::Color kAccentPressed  { 30,  64, 175};
inline const UltraCanvas::Color kRowHover       {243, 245, 248};
inline const UltraCanvas::Color kRowSelected    {226, 236, 253};
inline const UltraCanvas::Color kDanger         {220,  38,  38};
inline const UltraCanvas::Color kDangerHover    {185,  28,  28};
inline const UltraCanvas::Color kDangerPressed  {153,  27,  27};
inline const UltraCanvas::Color kSuccess        { 22, 163,  74};
inline const UltraCanvas::Color kTrack          {230, 232, 236};

constexpr float kPagePadding   = 16.0f;
constexpr float kGap           = 10.0f;
constexpr float kInnerGap      = 6.0f;
constexpr float kControlHeight = 26.0f;
constexpr float kControlRadius = 5.0f;
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
                                                           float size = 12.0f,
                                                           UltraCanvas::FontWeight weight = UltraCanvas::FontWeight::Normal,
                                                           const UltraCanvas::Color& color = kTextPrimary) {
    auto label = std::make_shared<UltraCanvas::UltraCanvasLabel>(id, text);
    label->SetFontSize(size);
    label->SetFontWeight(weight);
    label->SetTextColor(color);
    label->SetWrap(UltraCanvas::TextWrap::WrapWordChar);
    label->SetAlignment(UltraCanvas::TextAlignment::Left, UltraCanvas::VerticalAlignment::Top);
    return Pin(label);
}

inline std::shared_ptr<UltraCanvas::UltraCanvasLabel> Heading(const std::string& id, const std::string& text) {
    return Text(id, text, 18.0f, UltraCanvas::FontWeight::Bold);
}

inline std::shared_ptr<UltraCanvas::UltraCanvasLabel> Caption(const std::string& id, const std::string& text) {
    return Text(id, text, 11.0f, UltraCanvas::FontWeight::Normal, kTextSecondary);
}

enum class ButtonKind { Primary, Secondary, Danger };

inline UltraCanvas::ButtonStyle ButtonStyleFor(ButtonKind kind) {
    UltraCanvas::ButtonStyle s;
    s.borderWidth  = kind == ButtonKind::Secondary ? 1.0f : 0.0f;
    s.cornerRadius = kControlRadius;
    s.fontSize     = 12.0f;
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
            s.disabledColor = kPageBackground;
            s.normalTextColor = s.hoverTextColor = s.pressedTextColor = kTextPrimary;
            s.disabledTextColor = kTextMuted;
            s.borderColor = kBorder;
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

// A white rounded surface, a column.
inline std::shared_ptr<UltraCanvas::UltraCanvasContainer> Surface(const std::string& id, float gap = kInnerGap) {
    auto col = Column(id, gap);
    col->SetBackgroundColor(kSurface);
    col->SetBorders(1.0f, kBorder, 8.0f);
    col->SetPadding(12.0f);
    return col;
}

// A labelled control on one line: the label 180 wide, the control after it.
inline std::shared_ptr<UltraCanvas::UltraCanvasContainer> Field(const std::string& id, const std::string& label,
                                                                const std::shared_ptr<UltraCanvas::UltraCanvasUIElement>& control,
                                                                bool grow = false) {
    auto row = Row(id, kGap);
    auto text = Text(id + ".label", label, 12.0f);
    text->SetElementSize(UltraCanvas::CSSLayout::Dimension::Px(180), UltraCanvas::CSSLayout::Dimension::Auto());
    row->AddChild(text);
    Pin(control);
    if (grow) control->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    row->AddChild(control);
    return row;
}

} // namespace UltraStoreAdmin::Ui::Theme
