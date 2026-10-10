// Apps/UltraCanvasStart/ui/UltraCanvasStartTheme.h
// UltraCanvasStart's colours, type sizes, metrics and the small styling
// helpers its pages share: the same values as UltraMail's theme
// (Apps/UltraMail/ui/UltraMailTheme.h), so the two applications read the
// same side by side - a near-white page, white cards with a hairline border,
// one filled accent button per page, quiet secondary text.
//
// Nothing here paints anything. These are values handed to catalogue
// elements through their own SetStyle / SetTextColor / SetBorders APIs, per
// the framework rule that applications never hand-roll a widget. The
// helpers that are UltraCanvasStart's own: a read-only Markdown view with
// links that open in the browser (the pages' prose), a console (command
// output and generated files), a key/value row, a link label and a status
// badge for the key/value cards (step 1, step 4).
// Version: 0.2.0 - StyleSegmented, MakeSegmented (the platform and assistant pickers)
// Version: 0.1.1 - the Markdown views' links are not underlined
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasBadge.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSegmentedControl.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasUtils.h"

#include <memory>
#include <string>
#include <vector>

namespace UltraCanvasStart {
namespace Theme {

// ---------------------------------------------------------------------------
// Colour (UltraMail's)
// ---------------------------------------------------------------------------
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

// Status tints: green = present, orange = missing, the hues UltraMail uses
// for "good" and "waiting".
inline const UltraCanvas::Color kGoodText       { 21, 128,  61};
inline const UltraCanvas::Color kGoodTint       {220, 252, 231};
inline const UltraCanvas::Color kWarnText       {194,  65,  12};
inline const UltraCanvas::Color kWarnTint       {255, 237, 213};

// Code: a tinted chip behind a command, the way the Markdown view shows
// inline code, and a dark console for command output.
inline const UltraCanvas::Color kCodeText       { 30,  64, 175};
inline const UltraCanvas::Color kCodeTint       {238, 242, 255};
inline const UltraCanvas::Color kConsoleBack    { 30,  35,  45};
inline const UltraCanvas::Color kConsoleText    {226, 229, 234};

// ---------------------------------------------------------------------------
// Type (UltraMail's scale)
// ---------------------------------------------------------------------------
constexpr float kSizeTitle     = 14.0f;   // the application name in the header
constexpr float kSizeHeading   = 11.0f;   // page and card headings
constexpr float kSizeBody      = 9.0f;
constexpr float kSizeProse     = 10.0f;   // the Markdown views: instructions read better one size up
constexpr float kSizeSecondary = 8.5f;

// ---------------------------------------------------------------------------
// Metrics
// ---------------------------------------------------------------------------
constexpr float kPagePadding   = 10.0f;
constexpr float kGap           = 8.0f;
constexpr float kInnerGap      = 6.0f;
constexpr float kCardRadius    = 8.0f;
constexpr float kControlRadius = 5.0f;
constexpr float kControlHeight = 24.0f;
constexpr float kRowHeight     = 20.0f;   // one key/value line
constexpr float kKeyWidth      = 150.0f;  // the key column of a key/value row
constexpr float kHeaderHeight  = 44.0f;   // the band above the pages
constexpr float kStatusHeight  = 28.0f;   // the band below them

// ---------------------------------------------------------------------------
// Element styles (UltraMail's)
// ---------------------------------------------------------------------------
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

// A button sizes to its label, with `minWidth` as a floor (UltraMail's rule).
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

inline std::shared_ptr<UltraCanvas::UltraCanvasButton>
MakeButton(const std::string& id, const std::string& text, bool primary, float minWidth = 0.0f) {
    auto button = UltraCanvas::CreateButton(id, 0, 0, minWidth > 0 ? minWidth : 100, kControlHeight, text);
    button->SetElementSize(UltraCanvas::Size2Df(minWidth > 0 ? minWidth : 100, kControlHeight));
    FitToLabel(button, minWidth);
    if (primary) StylePrimary(button); else StyleSecondary(button);
    return button;
}

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

// ---------------------------------------------------------------------------
// Labels
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

// A fixed-height, stretch-width line of text for a flex column.
inline std::shared_ptr<UltraCanvas::UltraCanvasLabel>
MakeLine(const std::string& id, const std::string& text, float height = kRowHeight,
         float size = kSizeBody, const UltraCanvas::Color& color = kTextPrimary,
         UltraCanvas::FontWeight weight = UltraCanvas::FontWeight::Normal) {
    auto label = UltraCanvas::CreateLabel(id, 0, 0, 0, height, text);
    label->SetFontSize(size);
    label->SetFontWeight(weight);
    label->SetTextColor(color);
    label->layoutItem.SetFlexShrink(0).SetAlignSelf(UltraCanvas::CSSLayout::AlignSelf::Stretch);
    return label;
}

// A page heading: the question or the subject of the page.
inline std::shared_ptr<UltraCanvas::UltraCanvasLabel>
MakeHeading(const std::string& id, const std::string& text) {
    return MakeLine(id, text, 26, kSizeHeading + 1.0f, kTextPrimary, UltraCanvas::FontWeight::Bold);
}

// A hint under a heading, in the secondary grey, wrapping over two lines.
inline std::shared_ptr<UltraCanvas::UltraCanvasLabel>
MakeHint(const std::string& id, const std::string& text, float height = 30) {
    auto label = MakeLine(id, text, height, kSizeBody, kTextSecondary);
    label->SetWrap(UltraCanvas::TextWrap::WrapWord);
    label->SetAlignment(UltraCanvas::TextAlignment::Left, UltraCanvas::VerticalAlignment::Top);
    return label;
}

inline std::string EscapeMarkup(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        switch (ch) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            default:  out += ch; break;
        }
    }
    return out;
}

// A line that is one link: accent, underlined, the hand cursor, and the
// address opened in the browser on a click. The text may differ from the
// address ("the release page").
inline std::shared_ptr<UltraCanvas::UltraCanvasLabel>
MakeLink(const std::string& id, const std::string& text, const std::string& url,
         float height = kRowHeight) {
    auto label = MakeLine(id, "", height, kSizeBody, kAccent);
    label->SetTextIsMarkup(true);
    label->SetText("<u>" + EscapeMarkup(text) + "</u>");
    UltraCanvas::LabelTextLink link;
    link.startByte = 0;
    link.endByte = static_cast<int>(text.size());
    link.href = url;
    label->SetTextLinks({ link });
    label->SetShowLinkTooltips(text != url);
    label->onLinkActivated = [](const std::string& href) { UltraCanvas::OpenURL(href); };
    return label;
}

// A line of text with every word in `code` shown as a command: monospace on
// a tinted chip, the way the Markdown view shows `code`. `code` are
// substrings of `text`, matched once each in order.
inline std::shared_ptr<UltraCanvas::UltraCanvasLabel>
MakeCodeLine(const std::string& id, const std::string& text,
             const std::vector<std::string>& code, float height = kRowHeight) {
    std::string markup;
    size_t pos = 0;
    for (const auto& piece : code) {
        const size_t at = text.find(piece, pos);
        if (at == std::string::npos) continue;
        markup += EscapeMarkup(text.substr(pos, at - pos));
        markup += "<span font_family=\"monospace\" foreground=\"#1E40AF\" background=\"#EEF2FF\">" +
                  EscapeMarkup(piece) + "</span>";
        pos = at + piece.size();
    }
    markup += EscapeMarkup(text.substr(pos));
    auto label = MakeLine(id, "", height);
    label->SetTextIsMarkup(true);
    label->SetText(markup);
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
// Cards and rows
// ---------------------------------------------------------------------------
// A white card: the surface every content block sits on. A flex column with
// the card's padding; `title`, when given, is its first line.
inline std::shared_ptr<UltraCanvas::UltraCanvasContainer>
MakeCard(const std::string& id, const std::string& title = "") {
    auto card = UltraCanvas::CreateContainer(id, 0, 0, 0, 0);
    card->layout.SetFlexColumn()
                .SetFlexGap(kInnerGap)
                .SetFlexAlignItems(UltraCanvas::CSSLayout::AlignItems::Stretch);
    card->SetPadding(kGap, kGap + 4.0f);
    card->SetBackgroundColor(kCardBackground);
    card->SetBorders(1.0f, kCardBorder, kCardRadius);
    card->SetElementSize(UltraCanvas::CSSLayout::Dimension::Auto(), UltraCanvas::CSSLayout::Dimension::Auto());
    card->layoutItem.SetFlexShrink(0).SetAlignSelf(UltraCanvas::CSSLayout::AlignSelf::Stretch);
    UltraCanvas::ContainerStyle plain;
    plain.autoShowScrollbars = false;
    card->SetContainerStyle(plain);
    if (!title.empty()) {
        card->AddChild(MakeLine(id + ".title", title, 22, kSizeHeading, kTextPrimary,
                                UltraCanvas::FontWeight::Bold));
        card->AddChild(MakeDivider(id + ".rule"));
    }
    return card;
}

// A card whose height is a fixed share of the page: `grow` of the column's
// free space, from a basis of 0, so two such cards split the page.
inline void GrowCard(const std::shared_ptr<UltraCanvas::UltraCanvasContainer>& card, float grow = 1.0f) {
    if (!card) return;
    card->layoutItem.SetFlexGrow(grow).SetFlexShrink(1)
                    .SetFlexBasis(UltraCanvas::CSSLayout::Dimension::Px(0));
}

// A row of controls (buttons, a label beside them), fixed height.
inline std::shared_ptr<UltraCanvas::UltraCanvasContainer>
MakeRow(const std::string& id, float height = kControlHeight + 4.0f) {
    auto row = UltraCanvas::CreateContainer(id, 0, 0, 0, 0);
    row->layout.SetFlexRow().SetFlexGap(kGap).SetFlexAlignItems(UltraCanvas::CSSLayout::AlignItems::Center);
    row->SetElementSize(UltraCanvas::CSSLayout::Dimension::Auto(), UltraCanvas::CSSLayout::Dimension::Px(height));
    row->layoutItem.SetFlexShrink(0).SetAlignSelf(UltraCanvas::CSSLayout::AlignSelf::Stretch);
    UltraCanvas::ContainerStyle plain;
    plain.autoShowScrollbars = false;
    row->SetContainerStyle(plain);
    return row;
}

// One key/value line of a card: the key in the secondary grey on a
// fixed column, then the value elements the caller adds (a text, a link, a
// badge). Returns the row; the caller appends the value.
inline std::shared_ptr<UltraCanvas::UltraCanvasContainer>
MakeKeyRow(const std::string& id, const std::string& key) {
    auto row = MakeRow(id, kRowHeight);
    row->layout.SetFlexGap(kInnerGap);
    auto label = MakeText(id + ".key", key, kSizeBody, kTextSecondary);
    label->SetElementSize(UltraCanvas::Size2Df(kKeyWidth, kRowHeight));
    label->layoutItem.SetFlexShrink(0);
    row->AddChild(label);
    return row;
}

// A value of a key/value row: selectable, so a path can be copied.
inline std::shared_ptr<UltraCanvas::UltraCanvasLabel>
MakeValue(const std::string& id, const std::string& text,
          const UltraCanvas::Color& color = kTextPrimary) {
    auto label = MakeText(id, text, kSizeBody, color);
    label->SetElementSize(UltraCanvas::Size2Df(0, kRowHeight));
    label->SetSelectable(true);
    label->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    return label;
}

// A key/value row with a plain value, in one call.
inline std::shared_ptr<UltraCanvas::UltraCanvasLabel>
AddKeyValue(const std::shared_ptr<UltraCanvas::UltraCanvasContainer>& card,
            const std::string& id, const std::string& key, const std::string& value) {
    auto row = MakeKeyRow(id, key);
    auto label = MakeValue(id + ".value", value);
    row->AddChild(label);
    card->AddChild(row);
    return label;
}

// "found" in green, "not found" in orange: the status beside a tool.
inline std::shared_ptr<UltraCanvas::UltraCanvasBadge>
MakeStatusBadge(const std::string& id, bool ok, const std::string& text = "") {
    auto badge = UltraCanvas::CreateBadge(id, 0, 0, text.empty() ? (ok ? "found" : "not found") : text,
                                          ok ? UltraCanvas::BadgeVariant::Successful
                                             : UltraCanvas::BadgeVariant::Warning);
    auto style = badge->GetStyle();
    style.fontSize = kSizeBody;
    style.height = 16.0f;
    badge->SetStyle(style);
    badge->layoutItem.SetFlexShrink(0);
    return badge;
}

// ---------------------------------------------------------------------------
// Text views
// ---------------------------------------------------------------------------
// The pages' prose: a read-only Markdown view. Headings, numbered steps,
// **bold** for what matters, `code` on a tinted chip for what is typed, and
// links that open in the browser. Scrolls when the text is longer than the
// view.
inline std::shared_ptr<UltraCanvas::UltraCanvasTextArea>
MakeMarkdownView(const std::string& id, float height) {
    auto view = std::make_shared<UltraCanvas::UltraCanvasTextArea>(id, 0, 0, 600, height);
    view->SetEditingMode(UltraCanvas::TextAreaEditingMode::MarkdownHybrid);
    view->SetReadOnly(true);
    view->SetWordWrap(true);
    view->SetShowLineNumbers(false);
    view->SetHighlightCurrentLine(false);
    view->SetFontSize(kSizeProse);
    auto& style = view->GetStyle();
    style.borderColor     = UltraCanvas::Colors::Transparent;
    style.backgroundColor = kCardBackground;
    style.fontColor       = kTextPrimary;
    style.textPadding     = 4.0f;
    style.scrollbarWidth  = 12;
    style.scrollbarCornerRadius = 6.0f;
    style.scrollbarThumbInset = 0;
    auto& md = view->GetMarkdownStyleMutable();
    md.headerColors = { kTextPrimary, kTextPrimary, kTextPrimary, kTextPrimary, kTextPrimary, kTextPrimary };
    md.headerSizeMultipliers = { 1.4f, 1.25f, 1.15f, 1.05f, 1.0f, 1.0f };
    md.codeTextColor = kCodeText;
    md.codeBackgroundColor = kCodeTint;
    md.codeBlockBackgroundColor = kSidebar;
    md.codeBlockBorderColor = kCardBorder;
    md.codeBlockTextColor = kTextPrimary;
    md.linkColor = kAccent;
    md.linkHoverColor = kAccentHover;
    // The links here are archive names and addresses full of hyphens and
    // underscores, which an underline runs through; the accent colour and
    // the hand cursor mark them.
    md.linkUnderline = false;
    md.bulletColor = kTextSecondary;
    view->onMarkdownLinkClick = [](const std::string& url) { UltraCanvas::OpenURL(url); };
    view->SetElementSize(UltraCanvas::CSSLayout::Dimension::Auto(), UltraCanvas::CSSLayout::Dimension::Px(height));
    view->layoutItem.SetAlignSelf(UltraCanvas::CSSLayout::AlignSelf::Stretch);
    return view;
}

// Command output, generated files, the report: monospace on a dark ground,
// read-only, selectable.
inline std::shared_ptr<UltraCanvas::UltraCanvasTextArea>
MakeConsole(const std::string& id, float height, bool dark = true) {
    auto view = std::make_shared<UltraCanvas::UltraCanvasTextArea>(id, 0, 0, 600, height);
    view->SetReadOnly(true);
    view->SetWordWrap(true);
    view->SetShowLineNumbers(false);
    view->SetHighlightCurrentLine(false);
    view->SetFontFamily("monospace");
    view->SetFontSize(kSizeBody);
    auto& style = view->GetStyle();
    style.borderColor     = dark ? UltraCanvas::Colors::Transparent : kCardBorder;
    style.backgroundColor = dark ? kConsoleBack : kSidebar;
    style.fontColor       = dark ? kConsoleText : kTextPrimary;
    style.textPadding     = 6.0f;
    style.scrollbarWidth  = 12;
    style.scrollbarCornerRadius = 6.0f;
    style.scrollbarThumbInset = 0;
    view->SetBorders(0.0f, UltraCanvas::Colors::Transparent, kControlRadius);
    view->SetElementSize(UltraCanvas::CSSLayout::Dimension::Auto(), UltraCanvas::CSSLayout::Dimension::Px(height));
    view->layoutItem.SetAlignSelf(UltraCanvas::CSSLayout::AlignSelf::Stretch);
    return view;
}

// The view takes the column's free space (from a basis of 0, so two views
// share it by their `grow`).
template <typename ElementPtr>
inline void Grow(const ElementPtr& element, float grow = 1.0f) {
    if (!element) return;
    element->layoutItem.SetFlexGrow(grow).SetFlexShrink(1)
                       .SetFlexBasis(UltraCanvas::CSSLayout::Dimension::Px(0));
}

// Segmented controls (Linux | macOS | Windows; Claude Code | Codex | ...):
// the accent for the chosen segment, white for the others, hairline
// borders, body-size text - UltraMail's StyleSegmented with this theme's
// values.
inline void StyleSegmented(const std::shared_ptr<UltraCanvas::UltraCanvasSegmentedControl>& sc) {
    if (!sc) return;
    auto style = sc->GetStyle();
    style.fontSize          = kSizeBody;
    style.selectedColor     = kAccent;
    style.selectedTextColor = UltraCanvas::Colors::White;
    style.hoverColor        = kAccentSoft;
    style.normalColor       = kCardBackground;
    style.normalTextColor   = kTextPrimary;
    style.hoverTextColor    = kTextPrimary;
    style.borderColor       = kCardBorder;
    style.separatorColor    = kCardBorder;
    style.cornerRadius      = kControlRadius;
    style.paddingVertical   = 3;
    sc->SetStyle(style);
}

// A single-choice segmented control with `labels`, `selected` chosen,
// every segment the same width.
inline std::shared_ptr<UltraCanvas::UltraCanvasSegmentedControl>
MakeSegmented(const std::string& id, const std::vector<std::string>& labels, int selected, float width) {
    auto control = UltraCanvas::CreateSegmentedControl(id, 0, 0, width, kControlHeight);
    StyleSegmented(control);
    for (const auto& label : labels) control->AddSegment(label);
    control->SetWidthMode(UltraCanvas::SegmentWidthMode::Equal);
    control->SetSelectedIndex(selected);
    control->SetElementSize(UltraCanvas::Size2Df(width, kControlHeight));
    control->layoutItem.SetFlexShrink(0);
    return control;
}

inline void StyleCheckbox(const std::shared_ptr<UltraCanvas::UltraCanvasCheckbox>& box) {
    if (box) box->SetFontSize(kSizeBody);
}

} // namespace Theme
} // namespace UltraCanvasStart
