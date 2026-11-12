// Apps/UltraStoreOperator/ui/OperatorTheme.h
// The operator window's look: the admin backend's theme (AdminTheme.h -
// the shop design's tokens, Manrope over Inter, pills and rounded surfaces)
// with the operator tier's marks from the design's Super* pages: the dark
// sidebar that says "this is the operator, not a merchant", the stat
// cards, the state pills (over target, check failed, urgent, ready). The
// helpers the pages share; nothing here paints.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "AdminTheme.h"

#include "UltraCanvasSidebarNavigation.h"
#include "UltraCanvasTextArea.h"

#include <memory>
#include <string>

namespace UltraStoreOperator::Ui::Theme {

using namespace UltraStoreAdmin::Ui::Theme;   // NOLINT: the admin's tokens and helpers are the base

// ===== the dark sidebar (the design's #0F172A column) =====
inline const UltraCanvas::Color kSidebar          { 15,  23,  42};   // #0F172A
inline const UltraCanvas::Color kSidebarHover     { 30,  41,  59};   // #1E293B
inline const UltraCanvas::Color kSidebarSelected  { 30,  58, 138};   // #1E3A8A
inline const UltraCanvas::Color kSidebarText      {203, 213, 225};   // #CBD5E1
inline const UltraCanvas::Color kSidebarMuted     {148, 163, 184};   // #94A3B8
inline const UltraCanvas::Color kInk              { 15,  23,  42};

// ===== the pills' tints =====
inline const UltraCanvas::Color kAmberSoft  {254, 243, 199};   // over target, waiting
inline const UltraCanvas::Color kAmberText  {146,  64,  14};
inline const UltraCanvas::Color kRedSoft    {254, 226, 226};   // failed, urgent
inline const UltraCanvas::Color kRedText    {153,  27,  27};
inline const UltraCanvas::Color kGreenSoft  {220, 252, 231};   // released, active
inline const UltraCanvas::Color kGreenText  { 22, 101,  52};
inline const UltraCanvas::Color kBlueSoft   {219, 234, 254};   // ready, in review
inline const UltraCanvas::Color kBlueText   { 30,  64, 175};
inline const UltraCanvas::Color kGreySoft   {241, 245, 249};   // needs info, closed
inline const UltraCanvas::Color kGreyText   { 71,  85, 105};

inline UltraCanvas::SidebarStyle SidebarStyle() {
    UltraCanvas::SidebarStyle s = UltraCanvas::SidebarStyle::Default();
    s.backgroundColor = kSidebar;
    s.textColor = kSidebarText;
    s.disabledTextColor = kSidebarMuted;
    s.sectionTextColor = kSidebarMuted;
    s.hoverBackgroundColor = kSidebarHover;
    s.selectedBackgroundColor = kSidebarSelected;
    s.selectedTextColor = UltraCanvas::Colors::White;
    s.indicatorColor = kAccent;
    s.badgeBackgroundColor = kAccent;
    s.badgeTextColor = UltraCanvas::Colors::White;
    s.focusColor = kAccent;
    s.itemHeight = 40.0f;
    s.cornerRadius = 10.0f;
    s.paddingH = 12.0f;
    s.paddingV = 20.0f;
    s.itemFont.fontFamily = kBodyFont;
    s.itemFont.fontSize = 14.0f;
    s.sectionFont.fontFamily = kBodyFont;
    s.badgeFont.fontFamily = kBodyFont;
    return s;
}

enum class Tint { Amber, Red, Green, Blue, Grey };

// A small rounded pill with a word in it: a state, a count.
inline std::shared_ptr<UltraCanvas::UltraCanvasLabel> Pill(const std::string& id, const std::string& text, Tint tint) {
    UltraCanvas::Color back = kGreySoft, fore = kGreyText;
    switch (tint) {
        case Tint::Amber: back = kAmberSoft; fore = kAmberText; break;
        case Tint::Red: back = kRedSoft; fore = kRedText; break;
        case Tint::Green: back = kGreenSoft; fore = kGreenText; break;
        case Tint::Blue: back = kBlueSoft; fore = kBlueText; break;
        case Tint::Grey: break;
    }
    auto label = std::make_shared<UltraCanvas::UltraCanvasLabel>(id, text);
    label->SetFont(kBodyFont, 12.0f, UltraCanvas::FontWeight::Bold);
    label->SetTextColor(fore);
    label->SetBackgroundColor(back);
    label->SetBorders(0.0f, back, 12.0f);
    label->SetPadding(3.0f, 10.0f);
    label->SetWrap(UltraCanvas::TextWrap::WrapNone);
    label->SetAlignment(UltraCanvas::TextAlignment::Center, UltraCanvas::VerticalAlignment::Middle);
    return Pin(label);
}

// The tint a state wears, the way the design's pills do.
inline Tint TintFor(const std::string& state) {
    if (state == "released" || state == "active" || state == "resolved" || state == "ok") return Tint::Green;
    if (state == "failed_checks" || state == "rejected" || state == "suspended" || state == "urgent" || state == "failed") return Tint::Red;
    if (state == "in_review" || state == "checked" || state == "assigned" || state == "open") return Tint::Blue;
    if (state == "changes_requested" || state == "paused" || state == "waiting" || state == "not run") return Tint::Amber;
    return Tint::Grey;
}

// A stat card: the small upper-case label, the big number in Manrope, a note.
inline std::shared_ptr<UltraCanvas::UltraCanvasContainer> Stat(const std::string& id, const std::string& label, const std::string& value,
                                                               const std::string& note, bool dark = false) {
    auto card = Surface(id, 4.0f);
    card->SetPadding(18.0f, 20.0f);
    card->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    card->SetElementSize(UltraCanvas::CSSLayout::Dimension::Px(180), UltraCanvas::CSSLayout::Dimension::Auto());
    if (dark) { card->SetBackgroundColor(kSidebar); card->SetBorders(1.0f, kSidebar, kSurfaceRadius); }
    card->AddChild(Text(id + ".label", label, 12.0f, UltraCanvas::FontWeight::Bold, dark ? kSidebarMuted : kTextSecondary));
    card->AddChild(Text(id + ".value", value, 30.0f, UltraCanvas::FontWeight::Bold, dark ? UltraCanvas::Colors::White : kTextPrimary, kDisplayFont));
    card->AddChild(Text(id + ".note", note, 13.0f, UltraCanvas::FontWeight::Normal, dark ? kSidebarMuted : kTextBody));
    return card;
}

// A surface with a bold title line.
inline std::shared_ptr<UltraCanvas::UltraCanvasContainer> Section(const std::string& id, const std::string& title) {
    auto surface = Surface(id, kInnerGap);
    surface->AddChild(Text(id + ".title", title, 14.0f, UltraCanvas::FontWeight::Bold));
    return surface;
}

// A multi-line text box of the given height, in the shop look.
inline std::shared_ptr<UltraCanvas::UltraCanvasTextArea> Multiline(const std::string& id, float height, const std::string& placeholder = {}) {
    auto area = Pin(std::make_shared<UltraCanvas::UltraCanvasTextArea>(id, 0.0f, height));
    area->SetElementSize(UltraCanvas::CSSLayout::Dimension::Auto(), UltraCanvas::CSSLayout::Dimension::Px(height));
    area->SetWordWrap(true);
    area->SetShowLineNumbers(false);
    area->SetHighlightSyntax(false);
    area->SetFontSize(12.0f);
    area->SetBackgroundColor(kPageBackground);
    area->SetBorders(1.0f, kBorder, 4.0f);
    if (!placeholder.empty()) area->SetPlaceholder(placeholder);
    return area;
}

// A thin rule between the rows of a list.
inline std::shared_ptr<UltraCanvas::UltraCanvasContainer> Rule(const std::string& id) {
    auto rule = Row(id, 0);
    rule->SetElementSize(UltraCanvas::CSSLayout::Dimension::Auto(), UltraCanvas::CSSLayout::Dimension::Px(1));
    rule->SetBackgroundColor(kSurfaceMuted);
    return rule;
}

} // namespace UltraStoreOperator::Ui::Theme
