// Apps/UltraStore/ui/StoreTheme.h
// The store window's look, after the UltraStore shop design (the canvas
// "UltraStore Shop Design", 2026-11-05): a white ground, ink text, the
// UltraStore blue of the app icon as the one accent, Manrope for display
// text over Inter for body text (both in media/fonts, registered with
// every application), 20 px card corners, pill buttons and chips. The few
// helpers every page uses: a wrapping text, the headings, a caption, a
// styled button, a plain row or column, a card surface. Nothing here
// paints; the elements do.
// Version: 0.2.0 - the shop design: tokens, fonts, pills, the section helpers
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasButton.h"
#include "UltraCanvasChip.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasUtils.h"

#include <memory>
#include <string>

namespace UltraStore::Ui::Theme {

// ===== colours (the design's tokens) =====
inline const UltraCanvas::Color kPageBackground {255, 255, 255};   // the ground
inline const UltraCanvas::Color kSurface        {255, 255, 255};
inline const UltraCanvas::Color kSurfaceSoft    {248, 250, 252};   // #F8FAFC: the hero, the footer
inline const UltraCanvas::Color kSurfaceMuted   {241, 245, 249};   // #F1F5F9: the search pill, segments
inline const UltraCanvas::Color kBorder         {226, 232, 240};   // #E2E8F0
inline const UltraCanvas::Color kBorderStrong   {203, 213, 225};   // #CBD5E1: outlined buttons
inline const UltraCanvas::Color kTextPrimary    { 15,  23,  42};   // #0F172A, the ink
inline const UltraCanvas::Color kTextBody       { 51,  65,  85};   // #334155
inline const UltraCanvas::Color kTextSecondary  {100, 116, 139};   // #64748B
inline const UltraCanvas::Color kTextMuted      {148, 163, 184};   // #94A3B8
inline const UltraCanvas::Color kAccent         { 37,  99, 235};   // #2563EB, from the app icon
inline const UltraCanvas::Color kAccentHover    { 29,  78, 216};   // #1D4ED8
inline const UltraCanvas::Color kAccentPressed  { 30,  64, 175};   // #1E40AF
inline const UltraCanvas::Color kAccentSoft     {219, 234, 254};   // #DBEAFE: the soft pill, the selected nav
inline const UltraCanvas::Color kAccentSoftText { 29,  78, 216};
inline const UltraCanvas::Color kAccentFaint    {239, 246, 255};   // #EFF6FF: the hero's ground
inline const UltraCanvas::Color kRowHover       {241, 245, 249};
inline const UltraCanvas::Color kRowSelected    {219, 234, 254};
inline const UltraCanvas::Color kDanger         {220,  38,  38};
inline const UltraCanvas::Color kDangerHover    {185,  28,  28};
inline const UltraCanvas::Color kDangerPressed  {153,  27,  27};
inline const UltraCanvas::Color kSuccess        { 21, 128,  61};   // #15803D: "Free", "Installed"
inline const UltraCanvas::Color kTrack          {226, 232, 240};

// ===== type =====
// Manrope carries the display text (the wordmark, headings, card titles),
// Inter everything else. Both ship in media/fonts; a machine without them
// falls back to its sans-serif through fontconfig.
inline const char* kDisplayFont = "Manrope";
inline const char* kBodyFont    = "Inter";

// ===== sizes =====
constexpr float kPagePadding   = 24.0f;   // the gutter of a page
constexpr float kSectionGap    = 28.0f;   // between the sections of a page
constexpr float kGap           = 16.0f;   // between the parts of a section
constexpr float kInnerGap      = 8.0f;    // between the lines of a part
constexpr float kControlHeight = 40.0f;   // a pill button, a chip, a field
constexpr float kCardRadius    = 20.0f;
constexpr float kCardPadding   = 20.0f;
constexpr float kRowRadius     = 14.0f;
constexpr float kHeaderHeight  = 72.0f;
constexpr float kStatusHeight  = 24.0f;
constexpr float kCardWidth     = 300.0f;  // a card in the grids (they wrap)
constexpr float kAsideWidth    = 240.0f;  // the filter column of Categories
constexpr float kInfoWidth     = 320.0f;  // the information column of an app page

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

inline std::string AppIcon(const char* name) {
    return UltraCanvas::NormalizePath(UltraCanvas::GetResourcesDir() + "media/appicon/" + name);
}

// ===== text =====
// A label that wraps to the width it is given and sizes to its lines.
inline std::shared_ptr<UltraCanvas::UltraCanvasLabel> Text(const std::string& id, const std::string& text,
                                                           float size = 14.0f,
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

// Display text in Manrope: the page title, a section heading, a card title.
inline std::shared_ptr<UltraCanvas::UltraCanvasLabel> Display(const std::string& id, const std::string& text, float size = 22.0f,
                                                              const UltraCanvas::Color& color = kTextPrimary) {
    return Text(id, text, size, UltraCanvas::FontWeight::Bold, color, kDisplayFont);
}

inline std::shared_ptr<UltraCanvas::UltraCanvasLabel> Title(const std::string& id, const std::string& text) {
    return Display(id, text, 36.0f);
}

inline std::shared_ptr<UltraCanvas::UltraCanvasLabel> Heading(const std::string& id, const std::string& text) {
    return Display(id, text, 22.0f);
}

// A line under a heading, or a note.
inline std::shared_ptr<UltraCanvas::UltraCanvasLabel> Caption(const std::string& id, const std::string& text) {
    return Text(id, text, 13.0f, UltraCanvas::FontWeight::Normal, kTextSecondary);
}

// The small bold uppercase label over a group ("CATEGORY", "FEATURED APP").
inline std::shared_ptr<UltraCanvas::UltraCanvasLabel> Eyebrow(const std::string& id, const std::string& text,
                                                              const UltraCanvas::Color& color = kTextSecondary) {
    std::string upper;
    for (char c : text) upper += static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
    return Text(id, upper, 12.0f, UltraCanvas::FontWeight::Bold, color);
}

// ===== buttons =====
// Primary: the accent, white text. Soft: the pale accent with blue text -
// the card actions (Install, Try). Secondary: white with a border - Open,
// Settings. Ghost: no surface until hovered - the navigation, the links.
// Dark: the ink - a counter. Danger: red - Remove.
enum class ButtonKind { Primary, Soft, Secondary, Ghost, Dark, Danger };

inline UltraCanvas::ButtonStyle ButtonStyleFor(ButtonKind kind, float height = kControlHeight) {
    UltraCanvas::ButtonStyle s;
    s.borderWidth  = kind == ButtonKind::Secondary ? 1.0f : 0.0f;
    s.cornerRadius = height / 2.0f;   // a pill
    s.fontFamily   = kBodyFont;
    s.fontSize     = 14.0f;
    s.fontWeight   = UltraCanvas::FontWeight::Bold;
    s.focusedColor = kAccent;
    switch (kind) {
        case ButtonKind::Primary:
            s.normalColor = kAccent; s.hoverColor = kAccentHover; s.pressedColor = kAccentPressed;
            s.disabledColor = UltraCanvas::Color(191, 205, 235, 255);
            s.normalTextColor = s.hoverTextColor = s.pressedTextColor = s.disabledTextColor = UltraCanvas::Colors::White;
            s.borderColor = kAccent;
            break;
        case ButtonKind::Soft:
            s.normalColor = kAccentSoft; s.hoverColor = UltraCanvas::Color(191, 219, 254, 255); s.pressedColor = UltraCanvas::Color(147, 197, 253, 255);
            s.disabledColor = kSurfaceMuted;
            s.normalTextColor = s.hoverTextColor = s.pressedTextColor = kAccentSoftText;
            s.disabledTextColor = kTextMuted;
            s.borderColor = kAccentSoft;
            break;
        case ButtonKind::Secondary:
            s.normalColor = kSurface; s.hoverColor = kRowHover; s.pressedColor = kRowSelected;
            s.disabledColor = kSurfaceMuted;
            s.normalTextColor = s.hoverTextColor = s.pressedTextColor = kTextPrimary;
            s.disabledTextColor = kTextMuted;
            s.borderColor = kBorderStrong;
            break;
        case ButtonKind::Ghost:
            s.normalColor = UltraCanvas::Colors::Transparent; s.hoverColor = kRowHover; s.pressedColor = kRowSelected;
            s.disabledColor = UltraCanvas::Colors::Transparent;
            s.normalTextColor = s.hoverTextColor = s.pressedTextColor = kTextBody;
            s.disabledTextColor = kTextMuted;
            s.borderColor = UltraCanvas::Colors::Transparent;
            s.fontWeight = UltraCanvas::FontWeight::Normal;
            break;
        case ButtonKind::Dark:
            s.normalColor = kTextPrimary; s.hoverColor = UltraCanvas::Color(30, 41, 59, 255); s.pressedColor = UltraCanvas::Color(51, 65, 85, 255);
            s.disabledColor = kTextMuted;
            s.normalTextColor = s.hoverTextColor = s.pressedTextColor = s.disabledTextColor = UltraCanvas::Colors::White;
            s.borderColor = kTextPrimary;
            break;
        case ButtonKind::Danger:
            s.normalColor = kDanger; s.hoverColor = kDangerHover; s.pressedColor = kDangerPressed;
            s.disabledColor = UltraCanvas::Color(240, 180, 180, 255);
            s.normalTextColor = s.hoverTextColor = s.pressedTextColor = s.disabledTextColor = UltraCanvas::Colors::White;
            s.borderColor = kDanger;
            break;
    }
    return s;
}

inline std::shared_ptr<UltraCanvas::UltraCanvasButton> Button(const std::string& id, const std::string& text,
                                                              float width, ButtonKind kind = ButtonKind::Secondary,
                                                              float height = kControlHeight) {
    auto b = UltraCanvas::CreateButton(id, 0, 0, width, height, text);
    b->SetStyle(ButtonStyleFor(kind, height));
    return Pin(b);
}

inline void Style(const std::shared_ptr<UltraCanvas::UltraCanvasButton>& b, ButtonKind kind, float height = kControlHeight) {
    if (b) b->SetStyle(ButtonStyleFor(kind, height));
}

// A pill chip: white with a border, the accent when selected.
inline UltraCanvas::ChipStyle ChipStyleFor() {
    UltraCanvas::ChipStyle s;
    s.fillColor = kSurface;
    s.textColor = kTextPrimary;
    s.borderColor = kBorder;
    s.hoverFillColor = kRowHover;
    s.pressedFillColor = kRowSelected;
    s.selectedFillColor = kAccent;
    s.selectedTextColor = UltraCanvas::Colors::White;
    s.height = kControlHeight;
    s.cornerRadius = kControlHeight / 2.0f;
    s.paddingH = 14.0f;
    return s;
}

// The search field: a grey pill without a border.
inline UltraCanvas::TextInputStyle SearchStyle(const UltraCanvas::TextInputStyle& base) {
    UltraCanvas::TextInputStyle s = base;
    s.backgroundColor = kSurfaceMuted;
    s.borderColor = kSurfaceMuted;
    s.focusBorderColor = kAccent;
    s.textColor = kTextPrimary;
    s.placeholderColor = kTextSecondary;
    s.caretColor = kTextPrimary;
    s.borderWidth = 1;
    s.borderRadius = static_cast<int>(kControlHeight / 2.0f);
    return s;
}

// ===== containers =====
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

// A white card surface with the design's corners, a column.
inline std::shared_ptr<UltraCanvas::UltraCanvasContainer> Surface(const std::string& id, float gap = kInnerGap,
                                                                  const UltraCanvas::Color& fill = kSurface) {
    auto col = Column(id, gap);
    col->SetBackgroundColor(fill);
    col->SetBorders(1.0f, kBorder, kCardRadius);
    col->SetPadding(kCardPadding);
    return col;
}

// A grey row surface with smaller corners: a list entry.
inline std::shared_ptr<UltraCanvas::UltraCanvasContainer> RowSurface(const std::string& id, float gap = kGap) {
    auto row = Row(id, gap);
    row->SetBackgroundColor(kSurface);
    row->SetBorders(1.0f, kBorder, kRowRadius);
    row->SetPadding(12.0f, 16.0f);
    return row;
}

// A section: a heading with its line under it, and whatever follows.
inline std::shared_ptr<UltraCanvas::UltraCanvasContainer> Section(const std::string& id, const std::string& title,
                                                                  const std::string& subtitle = {}) {
    auto section = Column(id, kGap);
    auto head = Column(id + ".head", 4.0f);
    head->AddChild(Heading(id + ".title", title));
    if (!subtitle.empty()) head->AddChild(Caption(id + ".subtitle", subtitle));
    section->AddChild(head);
    return section;
}

// A 1 px line between two parts.
inline std::shared_ptr<UltraCanvas::UltraCanvasContainer> Rule(const std::string& id) {
    auto line = Row(id, 0);
    line->SetElementSize(UltraCanvas::CSSLayout::Dimension::Auto(), UltraCanvas::CSSLayout::Dimension::Px(1));
    line->SetBackgroundColor(kBorder);
    return line;
}

} // namespace UltraStore::Ui::Theme
