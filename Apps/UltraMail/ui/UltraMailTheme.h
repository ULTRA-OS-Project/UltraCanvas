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
// Version: 0.9.0 - the look follows a change of the theme while UltraMail is open: Role (a
//                  colour function), Follow / ReapplyLook (every element styled through
//                  here is styled again from the new theme), SetFill / SetInk / SetFrame;
//                  the cards, fields, buttons and segmented controls take every colour
//                  from the theme (the card group box, the field's face, the text on the
//                  accent and on the danger fill, the disabled fills); Mix, InkOn
// Version: 0.8.0 - the colours and type sizes are the framework theme's roles and text roles,
//                  read as functions (PageBackground(), TextPrimary(), SizeBody() ...): a dark
//                  desktop gives a dark UltraMail (T6 of the host theme proposal)
// Version: 0.7.0 - UltraMail's line icons (media/icons/ultramail/): MailIcon,
//                  SetButtonIcon, the flat ToolButton / MakeToolButton of the
//                  formatting bar, MakeToolSeparator, and MakeDialogHeader (an
//                  icon tile, a title and a line of text on top of a dialog)
// Version: 0.6.0 - the page, card, card caption and list-row colours are the framework's
//                  (Colors::SectionPage / SectionCard* / ListRow*, GroupBoxVisualStyle::
//                  SectionCard), shared with UltraPaint
// Version: 0.5.0 - ClickSurface::PassPointerThroughContent: a click on the
//                  surface's text and icons reaches the surface
// Version: 0.4.0 - StyleSegmented (the signature editor, the compose window)
// Version: 0.3.0 - sender-badge colours (kTrust*) and the badge metric
// Last Modified: 2026-11-14
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasButton.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasConfig.h"   // GetResourcesDir
#include "UltraCanvasContainer.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasGroupBox.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasTheme.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace UltraMail {
namespace Theme {

// ---------------------------------------------------------------------------
// Colour
// ---------------------------------------------------------------------------
// The colours are the framework theme's roles (UltraCanvasTheme.h): the
// desktop's light or dark look, or Appearance.conf's over it, so a dark
// desktop gives a dark UltraMail. Each is a function read when an element
// is styled; an element styled through the Role helpers below (Follow,
// SetFill, SetInk ...) is styled again when the look changes, so an open
// window follows a change of the desktop's look at once.
inline const UltraCanvas::ThemeRoles& Roles() { return UltraCanvas::UltraCanvasTheme::Current().roles; }
inline bool Dark() { return UltraCanvas::UltraCanvasTheme::Current().dark; }
// A colour moved toward white (by > 0) or black (by < 0).
inline UltraCanvas::Color Shade(const UltraCanvas::Color& c, int by) {
    auto step = [by](uint8_t v) { return static_cast<uint8_t>(std::max(0, std::min(255, static_cast<int>(v) + by))); };
    return UltraCanvas::Color(step(c.r), step(c.g), step(c.b), c.a);
}
// A soft surface of a strong colour: the light tint on a light look, the
// colour itself at low alpha over a dark panel.
inline UltraCanvas::Color Tint(const UltraCanvas::Color& strong, const UltraCanvas::Color& lightTint) {
    return Dark() ? strong.WithAlpha(56) : lightTint;
}
// A strong colour that reads on the look's panel: lifted on a dark one.
inline UltraCanvas::Color Strong(const UltraCanvas::Color& light) { return Dark() ? Shade(light, 70) : light; }
// `a` moved toward `b` by t (0 = a, 1 = b), alpha included.
inline UltraCanvas::Color Mix(const UltraCanvas::Color& a, const UltraCanvas::Color& b, float t) {
    auto mix = [t](uint8_t x, uint8_t y) {
        return static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, x + (static_cast<float>(y) - x) * t + 0.5f)));
    };
    return UltraCanvas::Color(mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a));
}
// The text that reads on a filled surface: white, or a near-black ink where
// white does not reach 4.5:1 (the light accent and red of a dark look).
inline UltraCanvas::Color InkOn(const UltraCanvas::Color& fill) {
    const UltraCanvas::Color white(255, 255, 255, 255);
    return UltraCanvas::UltraCanvasTheme::Contrast(white, fill) >= 4.5 ? white
                                                                      : UltraCanvas::Color(16, 20, 28, 255);
}

inline UltraCanvas::Color PageBackground() { return Roles().ground; }    // the window behind the cards
inline UltraCanvas::Color CardBackground() { return Roles().panel; }     // a card, a list, a field
inline UltraCanvas::Color CardBorder()     { return Roles().line; }
inline UltraCanvas::Color Divider()        { return Roles().line; }
inline UltraCanvas::Color Sidebar()        { return Roles().well; }

inline UltraCanvas::Color TextPrimary()    { return Roles().ink; }
inline UltraCanvas::Color TextSecondary()  { return Roles().inkMuted; }
inline UltraCanvas::Color TextMuted()      { return Roles().inkFaint; }

inline UltraCanvas::Color Accent()         { return Roles().accent; }        // primary actions, selection
inline UltraCanvas::Color AccentHover()    { return Roles().accentHover; }
inline UltraCanvas::Color AccentPressed()  { return Shade(Roles().accentHover, Dark() ? 20 : -20); }
inline UltraCanvas::Color AccentSoft()     { return Roles().accentSoft; }    // tinted surfaces (selected)
// A list's rows on a card
inline UltraCanvas::Color RowHover()       { return Roles().hover; }
inline UltraCanvas::Color RowSelected()    { return Roles().accentSoft; }
inline UltraCanvas::Color OnAccent()       { return Roles().onAccent; }      // text and icons on the accent
inline UltraCanvas::Color Danger()         { return Roles().danger; }        // destructive actions, errors
inline UltraCanvas::Color OnDanger()       { return InkOn(Roles().danger); }

// Counter tints: a soft background with a strong text colour of the same
// hue. Blue = new today, green = unread from before today, orange = waiting
// for a reply — the same three hues as the original design, toned down.
inline UltraCanvas::Color NewTodayText()   { return Strong(UltraCanvas::Color( 29,  78, 216)); }
inline UltraCanvas::Color NewTodayTint()   { return Tint(NewTodayText(), UltraCanvas::Color(219, 234, 254)); }
inline UltraCanvas::Color UnreadText()     { return Strong(UltraCanvas::Color( 21, 128,  61)); }
inline UltraCanvas::Color UnreadTint()     { return Tint(UnreadText(), UltraCanvas::Color(220, 252, 231)); }
inline UltraCanvas::Color WaitingText()    { return Strong(UltraCanvas::Color(194,  65,  12)); }
inline UltraCanvas::Color WaitingTint()    { return Tint(WaitingText(), UltraCanvas::Color(255, 237, 213)); }

// Sender-badge colours. The badge left of a subject line says, at a glance,
// who a message is from: a filled tile for someone the address book knows, an
// outlined one for a stranger, in a colour that gets warmer the less the
// message can be trusted. The hues are the ones the rest of the app already
// uses for the same ideas (green = good, blue = business/known, orange =
// waiting/caution, red = stop).
inline UltraCanvas::Color TrustFriend()       { return Strong(UltraCanvas::Color( 22, 163,  74)); }   // address book, private
inline UltraCanvas::Color TrustFriendSoft()   { return Tint(TrustFriend(), UltraCanvas::Color(220, 252, 231)); }
inline UltraCanvas::Color TrustBusiness()     { return Strong(UltraCanvas::Color( 37,  99, 235)); }   // address book, work
inline UltraCanvas::Color TrustBusinessSoft() { return Tint(TrustBusiness(), UltraCanvas::Color(219, 234, 254)); }
inline UltraCanvas::Color TrustNew()          { return Roles().ink; }                                  // unknown sender
inline UltraCanvas::Color TrustAdvert()       { return Strong(UltraCanvas::Color( 30,  58, 138)); }   // bulk / marketing
inline UltraCanvas::Color TrustSpam()         { return Strong(UltraCanvas::Color(234,  88,  12)); }   // likely spam
inline UltraCanvas::Color TrustSpamSoft()     { return Tint(TrustSpam(), UltraCanvas::Color(255, 237, 213)); }
inline UltraCanvas::Color TrustScam()         { return Roles().danger; }                               // likely scam / phishing
inline UltraCanvas::Color TrustScamSoft()     { return Tint(TrustScam(), UltraCanvas::Color(254, 226, 226)); }


// ---------------------------------------------------------------------------
// Following the look
// ---------------------------------------------------------------------------
// A Role is one of the colour functions above, handed over uncalled
// (Theme::TextSecondary, not Theme::TextSecondary()), so it can be read again
// when the look changes. An element styled through Follow - and through the
// helpers below that take a Role, which go through it - is styled again by
// ReapplyLook from the new theme; UltraMailApp calls ReapplyLook from its
// theme change handler, so every open window, the composer, the contact
// manager and the dialogs follow a change of the desktop's look at once.
// Each element keeps one entry per slot ("fill", "ink", "style" ...): a later
// Follow of the same slot replaces the earlier one (a row selected, then
// not), and the entries are applied again in the order they were last set,
// so one set after another wins again. An entry ends with its element.
using Role = UltraCanvas::Color (*)();

namespace Detail {
struct LookEntry {
    std::weak_ptr<const void> owner;
    std::function<void()> apply;
    uint64_t order = 0;
};
using LookKey = std::pair<const void*, std::string>;
inline std::map<LookKey, LookEntry>& LookRegistry() {
    static std::map<LookKey, LookEntry> registry;
    return registry;
}
inline uint64_t NextLookOrder() {
    static uint64_t order = 0;
    return ++order;
}
// Entries whose element is gone are dropped.
inline void PruneLook() {
    auto& registry = LookRegistry();
    for (auto it = registry.begin(); it != registry.end();) {
        if (it->second.owner.expired()) it = registry.erase(it);
        else ++it;
    }
}
} // namespace Detail

// Apply `apply` to the element now, and again whenever the look changes.
template <typename E, typename F>
inline void Follow(const std::shared_ptr<E>& element, const std::string& slot, F apply) {
    if (!element) return;
    apply(*element);
    auto& registry = Detail::LookRegistry();
    static size_t pruneAt = 512;
    if (registry.size() >= pruneAt) {
        Detail::PruneLook();
        pruneAt = std::max<size_t>(512, registry.size() * 2);
    }
    std::weak_ptr<E> weak = element;
    Detail::LookEntry entry;
    entry.owner = std::weak_ptr<const void>(std::static_pointer_cast<const void>(element));
    entry.apply = [weak, apply]() mutable {
        if (auto e = weak.lock()) apply(*e);
    };
    entry.order = Detail::NextLookOrder();
    registry[{static_cast<const void*>(element.get()), slot}] = std::move(entry);
}

// Style every followed element again from the theme in force.
inline void ReapplyLook() {
    Detail::PruneLook();
    std::vector<std::pair<uint64_t, std::function<void()>>> todo;
    for (const auto& [key, entry] : Detail::LookRegistry()) todo.emplace_back(entry.order, entry.apply);
    std::sort(todo.begin(), todo.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& [order, apply] : todo) apply();
}

// The common cases: a background, a text colour, a frame.
template <typename E>
inline void SetFill(const std::shared_ptr<E>& e, Role role) {
    Follow(e, "fill", [role](E& x) { x.SetBackgroundColor(role()); });
}
// A label styled here keeps its own size when the look changes: the
// framework's theme pass would set every label that follows the theme to the
// body size (LabelStyle::FromTheme), so a heading or a small caption would
// lose its size; its colour is this registry's to set again.
template <typename E>
inline void KeepLabelSize(const std::shared_ptr<E>& e) {
    if constexpr (std::is_base_of_v<UltraCanvas::UltraCanvasLabel, E>) {
        if (e) e->SetFollowTheme(false);
    }
}
template <typename E>
inline void SetInk(const std::shared_ptr<E>& e, Role role) {
    KeepLabelSize(e);
    Follow(e, "ink", [role](E& x) { x.SetTextColor(role()); });
}
template <typename E>
inline void SetFrame(const std::shared_ptr<E>& e, float width, Role role, float radius = 0.0f) {
    Follow(e, "frame", [width, role, radius](E& x) { x.SetBorders(width, role(), radius); });
}

// ---------------------------------------------------------------------------
// Type
// ---------------------------------------------------------------------------
// The sizes are the theme's text roles - the desktop's interface font size
// and the ratios of UltraCanvasTheme - so UltraMail reads the same size as
// the rest of the desktop, and as UltraFiler beside it.
inline float SizeTitle()     { return static_cast<float>(UltraCanvas::UltraCanvasTheme::Current().FontSize(UltraCanvas::ThemeTextRole::Title)); }
inline float SizeHeading()   { return static_cast<float>(UltraCanvas::UltraCanvasTheme::Current().FontSize(UltraCanvas::ThemeTextRole::Heading)); }
inline float SizeBody()      { return static_cast<float>(UltraCanvas::UltraCanvasTheme::Current().FontSize(UltraCanvas::ThemeTextRole::Body)); }
inline float SizeSecondary() { return static_cast<float>(UltraCanvas::UltraCanvasTheme::Current().FontSize(UltraCanvas::ThemeTextRole::Caption)); }
inline float SizeSmall()     { return static_cast<float>(UltraCanvas::UltraCanvasTheme::Current().FontSize(UltraCanvas::ThemeTextRole::Small)); }

// ---------------------------------------------------------------------------
// Metrics
// ---------------------------------------------------------------------------
constexpr float kPagePadding   = 10.0f;   // gutter between the window edge and content
constexpr float kGap           = 8.0f;    // gap between cards / rows
constexpr float kInnerGap      = 6.0f;    // gap inside a row
inline const float kCardRadius = UltraCanvas::GroupBoxVisualStyle::SectionCard().cornerRadius;
constexpr float kControlRadius = 5.0f;
constexpr float kControlHeight = 24.0f;   // buttons and inputs (UltraFiler: 22)
constexpr float kToolbarHeight = 28.0f;
constexpr float kAvatarSize    = 28.0f;   // the provider / contact initial square
constexpr float kBadgeSize     = 18.0f;   // the sender badge in a message-list row

// ---------------------------------------------------------------------------
// Element styles
// ---------------------------------------------------------------------------
// The filled accent button: one per window, for the action the user came for.
// Its text is the theme's text on the accent (white on a light look's blue,
// the dark ink on a dark look's lighter blue).
inline UltraCanvas::ButtonStyle PrimaryButton() {
    UltraCanvas::ButtonStyle s;
    s.normalColor      = Accent();
    s.hoverColor       = AccentHover();
    s.pressedColor     = AccentPressed();
    s.disabledColor    = Dark() ? Mix(Accent(), CardBackground(), 0.6f) : UltraCanvas::Color(191, 205, 235, 255);
    s.normalTextColor  = OnAccent();
    s.hoverTextColor   = OnAccent();
    s.pressedTextColor = OnAccent();
    s.disabledTextColor= Dark() ? Mix(InkOn(s.disabledColor), s.disabledColor, 0.35f) : UltraCanvas::Colors::White;
    s.focusedColor     = Roles().focus;
    s.borderWidth      = 0.0f;
    s.borderColor      = Accent();
    s.cornerRadius     = kControlRadius;
    s.fontSize         = SizeBody();
    return s;
}

// The quiet button with a hairline border on the card's face, for everything else.
inline UltraCanvas::ButtonStyle SecondaryButton() {
    UltraCanvas::ButtonStyle s;
    s.normalColor      = CardBackground();
    s.hoverColor       = RowHover();
    s.pressedColor     = RowSelected();
    s.disabledColor    = PageBackground();
    s.normalTextColor  = TextPrimary();
    s.hoverTextColor   = TextPrimary();
    s.pressedTextColor = TextPrimary();
    s.disabledTextColor= TextMuted();
    s.focusedColor     = Roles().focus;
    s.borderWidth      = 1.0f;
    s.borderColor      = CardBorder();
    s.cornerRadius     = kControlRadius;
    s.fontSize         = SizeBody();
    return s;
}

// The filled red button: destructive actions (removing an account), where the
// colour itself is the warning. One per surface, kept apart from the safe
// buttons in the row.
inline UltraCanvas::ButtonStyle DangerButton() {
    UltraCanvas::ButtonStyle s;
    s.normalColor      = Danger();
    s.hoverColor       = Dark() ? Shade(Danger(), 18) : UltraCanvas::Color(185,  28,  28); // red-700
    s.pressedColor     = Dark() ? Shade(Danger(), -24) : UltraCanvas::Color(153,  27,  27); // red-800
    s.disabledColor    = Dark() ? Mix(Danger(), CardBackground(), 0.6f) : UltraCanvas::Color(240, 180, 180, 255);
    s.normalTextColor  = OnDanger();
    s.hoverTextColor   = InkOn(s.hoverColor);
    s.pressedTextColor = InkOn(s.pressedColor);
    s.disabledTextColor= Dark() ? Mix(InkOn(s.disabledColor), s.disabledColor, 0.35f) : UltraCanvas::Colors::White;
    s.focusedColor     = Roles().focus;
    s.borderWidth      = 0.0f;
    s.borderColor      = Danger();
    s.cornerRadius     = kControlRadius;
    s.fontSize         = SizeBody();
    return s;
}

// The colours of one button style onto another, keeping the rest of it (its
// icon, padding, font and size): what a change of the look sets again.
inline void TakeButtonColors(UltraCanvas::ButtonStyle& to, const UltraCanvas::ButtonStyle& from) {
    to.normalColor       = from.normalColor;
    to.hoverColor        = from.hoverColor;
    to.pressedColor      = from.pressedColor;
    to.disabledColor     = from.disabledColor;
    to.focusedColor      = from.focusedColor;
    to.normalTextColor   = from.normalTextColor;
    to.hoverTextColor    = from.hoverTextColor;
    to.pressedTextColor  = from.pressedTextColor;
    to.disabledTextColor = from.disabledTextColor;
    to.borderColor       = from.borderColor;
}

// A button in one of the styles above, following the look: the style now,
// and its colours again when the look changes. `look` is PrimaryButton,
// SecondaryButton, DangerButton or ToolButton.
inline void StyleButton(const std::shared_ptr<UltraCanvas::UltraCanvasButton>& b,
                        UltraCanvas::ButtonStyle (*look)()) {
    if (!b) return;
    b->SetStyle(look());
    Follow(b, "style", [look](UltraCanvas::UltraCanvasButton& x) {
        UltraCanvas::ButtonStyle s = x.GetStyle();
        TakeButtonColors(s, look());
        x.SetStyle(s);
    });
}

inline void StylePrimary(const std::shared_ptr<UltraCanvas::UltraCanvasButton>& b) {
    StyleButton(b, &PrimaryButton);
}
inline void StyleSecondary(const std::shared_ptr<UltraCanvas::UltraCanvasButton>& b) {
    StyleButton(b, &SecondaryButton);
}
inline void StyleDanger(const std::shared_ptr<UltraCanvas::UltraCanvasButton>& b) {
    StyleButton(b, &DangerButton);
}

// The flat button of a tool strip (the formatting bar): no frame and no fill
// until the pointer is over it, so a row of icons reads as one toolbar rather
// than a wall of boxes.
inline UltraCanvas::ButtonStyle ToolButton() {
    UltraCanvas::ButtonStyle s;
    s.normalColor      = UltraCanvas::Colors::Transparent;
    s.hoverColor       = RowHover();
    s.pressedColor     = AccentSoft();
    s.disabledColor    = UltraCanvas::Colors::Transparent;
    s.normalTextColor  = TextPrimary();
    s.hoverTextColor   = TextPrimary();
    s.pressedTextColor = Accent();
    s.disabledTextColor= TextMuted();
    s.focusedColor     = Roles().focus;
    s.borderWidth      = 0.0f;
    s.borderColor      = UltraCanvas::Colors::Transparent;
    s.cornerRadius     = kControlRadius;
    s.fontSize         = SizeBody();
    return s;
}

// Icons
// ---------------------------------------------------------------------------
// UltraMail's line icons: media/icons/ultramail/<name>.svg, one colour on a
// 16 px grid (see the README there). A button draws them as a mask in its
// text colour, so they follow the primary / secondary / danger styles.
constexpr int kButtonIcon = 14;   // beside a button's label
constexpr int kToolIcon   = 16;   // alone on a tool button

inline std::string MailIcon(const std::string& name) {
    return UltraCanvas::NormalizePath(UltraCanvas::GetResourcesDir()
                                      + "media/icons/ultramail/" + name + ".svg");
}

// A line icon in front of the button's label, in the label's colour. Call it
// after the button's style (which holds the icon's mask and spacing) and after
// FitToLabel: the button then hugs icon and label with even padding, rather
// than keeping FitToLabel's floor - a wider button draws the icon at its left
// edge and the label centred in the rest, so the two drift apart.
inline void SetButtonIcon(const std::shared_ptr<UltraCanvas::UltraCanvasButton>& b,
                          const std::string& name, int size = kButtonIcon) {
    if (!b) return;
    b->SetIcon(MailIcon(name));
    b->SetIconPosition(UltraCanvas::ButtonIconPosition::Left);
    b->SetIconSize(size, size);
    b->SetIconSpacing(6);
    b->SetUseIconAsMask(true);
    b->SetPadding(4, 12, 4, 12);
    if (b->boxConstraints) b->boxConstraints->minWidth = UltraCanvas::CSSLayout::Dimension::Auto();
}

// A square, icon-only tool button. The tooltip is its only label, so it must
// say what the button does.
inline std::shared_ptr<UltraCanvas::UltraCanvasButton>
MakeToolButton(const std::string& id, const std::string& icon, const std::string& tooltip,
               float side = kControlHeight + 4.0f) {
    auto b = UltraCanvas::CreateButton(id, 0, 0, side, side, "");
    StyleButton(b, &ToolButton);
    b->SetIcon(MailIcon(icon));
    b->SetIconPosition(UltraCanvas::ButtonIconPosition::Left);
    b->SetIconSize(kToolIcon, kToolIcon);
    b->SetIconSpacing(0);
    b->SetUseIconAsMask(true);
    b->SetTooltip(tooltip);
    b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return b;
}

// A line icon on its own, drawn in a Role's colour: the icon's shape as a
// mask (a picture element can only darken a black icon), on no fill, taking
// no pointer. The colour follows the look.
inline std::shared_ptr<UltraCanvas::UltraCanvasButton>
MakeIconGlyph(const std::string& id, const std::string& iconPath, float side, Role ink,
              float x = 0.0f, float y = 0.0f) {
    auto glyph = UltraCanvas::CreateButton(id, x, y, side, side, "");
    UltraCanvas::ButtonStyle s;
    s.normalColor = s.hoverColor = s.pressedColor = s.disabledColor = UltraCanvas::Colors::Transparent;
    s.borderColor = UltraCanvas::Colors::Transparent;
    s.borderWidth = 0.0f;
    glyph->SetStyle(s);
    glyph->SetIcon(iconPath);
    glyph->SetIconPosition(UltraCanvas::ButtonIconPosition::Left);
    glyph->SetIconSize(static_cast<int>(side), static_cast<int>(side));
    glyph->SetIconSpacing(0);
    glyph->SetPadding(0, 0, 0, 0);
    glyph->SetUseIconAsMask(true);
    Follow(glyph, "ink", [ink](UltraCanvas::UltraCanvasButton& g) {
        g.SetTextColors(ink());
        g.SetIconMaskColor(ink());
    });
    glyph->SetInteractive(false);
    glyph->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return glyph;
}

// The thin upright line between groups of tool buttons.
inline std::shared_ptr<UltraCanvas::UltraCanvasContainer>
MakeToolSeparator(const std::string& id, float height = kControlHeight - 6.0f) {
    auto line = UltraCanvas::CreateContainer(id, 0, 0, 1, height);
    SetFill(line, CardBorder);
    line->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    // A little air on both sides on top of the row's own gap.
    line->SetMargin(0, 3, 0, 3);
    return line;
}

// Every button sizes to its label: its width follows the text (and icon), so
// a longer label - a translation, "Reloading…" - is never cut to "Reloa…".
// `minWidth` keeps the old fixed width as a floor, so short English labels
// look as they did. The same rule as UltraCanvasModalDialog::SizeButtonToLabel.
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

// A card: the surface every content block sits on.
inline void ApplyCard(const std::shared_ptr<UltraCanvas::UltraCanvasContainer>& c,
                      float radius = kCardRadius) {
    if (!c) return;
    SetFill(c, CardBackground);
    SetFrame(c, 1.0f, CardBorder, radius);
}

// A group box drawn as a card with a quiet caption: the framework's
// SectionCard, with the caption at the body size and the colours the look's
// (the framework's SectionCard is a white card on a light page).
inline UltraCanvas::GroupBoxVisualStyle CardGroupBoxColors(UltraCanvas::GroupBoxVisualStyle s) {
    s.backgroundColor = CardBackground();
    s.borderColor     = CardBorder();
    s.titleColor      = TextSecondary();
    s.disabledTitleColor = TextMuted();
    return s;
}
inline UltraCanvas::GroupBoxVisualStyle CardGroupBox(float contentPadding = kGap) {
    UltraCanvas::GroupBoxVisualStyle s = UltraCanvas::GroupBoxVisualStyle::SectionCard();
    s.titleFont.fontSize    = SizeBody();
    s.contentPadding        = contentPadding;
    return CardGroupBoxColors(s);
}
template <typename GroupBoxPtr>
inline void StyleCardGroupBox(const GroupBoxPtr& box, float contentPadding = kGap) {
    if (!box) return;
    box->SetVisualStyle(CardGroupBox(contentPadding));
    Follow(box, "style", [](auto& g) { g.SetVisualStyle(CardGroupBoxColors(g.GetVisualStyle())); });
}

// Text inputs: hairline border, accent focus ring, body-size text, on the
// theme's field face (its caret, selection and disabled colours too).
inline UltraCanvas::TextInputStyle InputColors(const UltraCanvas::TextInputStyle& base) {
    UltraCanvas::TextInputStyle s =
        UltraCanvas::TextInputStyle::FromTheme(UltraCanvas::UltraCanvasTheme::Current(), base);
    s.backgroundColor  = CardBackground();
    s.borderColor      = CardBorder();
    s.focusBorderColor = Accent();
    s.validBorderColor = CardBorder();
    s.textColor        = TextPrimary();
    s.placeholderColor = TextMuted();
    s.caretColor       = TextPrimary();
    return s;
}
inline UltraCanvas::TextInputStyle InputStyle() {
    UltraCanvas::TextInputStyle s = InputColors(UltraCanvas::TextInputStyle());
    s.borderRadius     = static_cast<int>(kControlRadius);
    s.paddingLeft      = 6;
    s.paddingRight     = 6;
    s.fontStyle.fontSize = SizeBody();
    return s;
}
inline void StyleInput(const std::shared_ptr<UltraCanvas::UltraCanvasTextInput>& in) {
    if (!in) return;
    in->SetStyle(InputStyle());
    Follow(in, "style", [](UltraCanvas::UltraCanvasTextInput& x) {
        UltraCanvas::TextInputStyle s = InputColors(x.GetStyle());
        s.fontStyle.fontSize = SizeBody();
        x.SetStyle(s);
    });
}

// Multi-line text: the same hairline border, more inner padding. A template
// so this header need not pull in UltraCanvasTextArea.h (which drags X11
// macros into every engine header included after it).
template <typename TextAreaPtr>
inline void StyleTextArea(const TextAreaPtr& ta, bool bordered = true) {
    if (!ta) return;
    ta->GetStyle().textPadding = 6.0f;
    ta->SetFontSize(SizeBody() + 1.0f);
    Follow(ta, "style", [bordered](auto& x) {
        auto& s = x.GetStyle();
        s.borderColor     = bordered ? CardBorder() : UltraCanvas::Colors::Transparent;
        s.backgroundColor = CardBackground();
        s.fontColor       = TextPrimary();
        x.RequestRedraw();
    });
}

// Dropdowns: body-size text (the element's default is larger), the colours
// the theme's - it follows the look by itself (the theme's body size is the
// one set here). A template for the same reason as StyleTextArea.
template <typename DropdownPtr>
inline void StyleDropdown(const DropdownPtr& dd) {
    if (!dd) return;
    auto s = dd->GetStyle();
    s.fontSize = SizeBody();
    dd->SetStyle(s);
    dd->SetFollowTheme(true);
}

// The editor of a formatted (HTML) message - the composer's, the signature's:
// the message's own page, white in either look, as the reading pane shows a
// received one. The colours a writer picks (the formatting bar's palette, a
// signature's dark blue) are made for that page and read on it; on the dark
// look's panel they would not. Only the size of the text and whether the
// editor draws a frame are kept from the editor's style. A template for the
// same reason as StyleTextArea (RichTextEditStyle is the style's type).
template <typename RichStyle, typename RichPtr>
inline void StyleMessagePaper(const RichPtr& rich, float fontSize, bool drawBorder) {
    if (!rich) return;
    RichStyle paper;                               // the editor's own page: white, dark ink
    paper.baseFont.fontSize = fontSize;
    paper.drawBorder        = drawBorder;
    rich->SetStyle(paper);                         // a style set by hand: it keeps the page
}

// Tree views (the folder tree, the settings pages): the rows' colours - the
// soft accent behind the selected row with the ink on it, the hover fill,
// the expand boxes and the guide lines - again when the look changes. A
// template for the same reason as StyleTextArea.
template <typename TreePtr>
inline void StyleTreeRows(const TreePtr& tree) {
    Follow(tree, "rows", [](auto& t) {
        t.SetTextColor(TextPrimary());
        t.SetSelectionColor(RowSelected());
        t.SetSelectionTextColor(TextPrimary());
        t.SetHoverColor(RowHover());
        t.SetLineColor(TextMuted());
        t.SetExpandButtonColors(CardBackground(), TextMuted(), TextPrimary());
        t.RequestRedraw();
    });
}

// Segmented controls (None | Plain text | HTML, Plain text | Formatted): the
// accent for the chosen segment with the text that reads on it, hairline
// borders, body-size text. A template for the same reason as StyleTextArea.
template <typename SegmentedPtr>
inline void StyleSegmented(const SegmentedPtr& sc) {
    if (!sc) return;
    auto style = sc->GetStyle();
    style.fontSize        = SizeBody();
    style.cornerRadius    = kControlRadius;
    style.paddingVertical = 3;
    sc->SetStyle(style);
    Follow(sc, "style", [](auto& x) {
        auto s = x.GetStyle();
        s.selectedColor     = Accent();
        s.selectedTextColor = OnAccent();
        s.hoverColor        = AccentSoft();
        s.hoverTextColor    = TextPrimary();
        s.normalColor       = CardBackground();
        s.normalTextColor   = TextPrimary();
        s.disabledTextColor = TextMuted();
        s.borderColor       = CardBorder();
        s.separatorColor    = CardBorder();
        x.SetStyle(s);
    });
}

// ---------------------------------------------------------------------------
// Label helpers
// ---------------------------------------------------------------------------
// The text colour is a Role, read again when the look changes; a fixed
// colour (a computed one) is taken as it is.
inline std::shared_ptr<UltraCanvas::UltraCanvasLabel>
MakeText(const std::string& id, const std::string& text, float size, const UltraCanvas::Color& color,
         UltraCanvas::FontWeight weight = UltraCanvas::FontWeight::Normal) {
    auto label = UltraCanvas::CreateLabel(id, text);
    KeepLabelSize(label);
    label->SetFontSize(size);
    label->SetFontWeight(weight);
    label->SetTextColor(color);
    return label;
}
inline std::shared_ptr<UltraCanvas::UltraCanvasLabel>
MakeText(const std::string& id, const std::string& text, float size = SizeBody(),
         Role color = TextPrimary,
         UltraCanvas::FontWeight weight = UltraCanvas::FontWeight::Normal) {
    auto label = MakeText(id, text, size, color(), weight);
    SetInk(label, color);
    return label;
}

// A fixed-height, stretch-width line of text (for flex columns).
inline std::shared_ptr<UltraCanvas::UltraCanvasLabel>
MakeLine(const std::string& id, const std::string& text, float height,
         float size, const UltraCanvas::Color& color,
         UltraCanvas::FontWeight weight = UltraCanvas::FontWeight::Normal) {
    auto label = UltraCanvas::CreateLabel(id, 0, 0, 0, height, text);
    KeepLabelSize(label);
    label->SetFontSize(size);
    label->SetFontWeight(weight);
    label->SetTextColor(color);
    return label;
}
inline std::shared_ptr<UltraCanvas::UltraCanvasLabel>
MakeLine(const std::string& id, const std::string& text, float height,
         float size = SizeBody(), Role color = TextPrimary,
         UltraCanvas::FontWeight weight = UltraCanvas::FontWeight::Normal) {
    auto label = MakeLine(id, text, height, size, color(), weight);
    SetInk(label, color);
    return label;
}

// The head of a dialog: the icon in a tinted tile, a bold title and, below
// it, a line of explaining text that wraps. One row for the top of a flex
// column; the text keeps the height it is given (`textHeight`).
inline std::shared_ptr<UltraCanvas::UltraCanvasContainer>
MakeDialogHeader(const std::string& id, const std::string& icon, const std::string& title,
                 const std::string& text, float textHeight = 32.0f,
                 Role tint = AccentSoft, Role ink = Accent) {
    constexpr float kTile = 40.0f;
    constexpr float kTitleHeight = 20.0f;
    auto header = UltraCanvas::CreateContainer(id, 0, 0, 0, 0);
    header->layout.SetFlexRow()
                  .SetFlexGap(12)
                  .SetFlexAlignItems(UltraCanvas::CSSLayout::AlignItems::Start);
    UltraCanvas::ContainerStyle style;
    style.autoShowScrollbars = false;
    header->SetContainerStyle(style);

    auto tile = UltraCanvas::CreateContainer(id + ".tile", 0, 0, kTile, kTile);
    SetFill(tile, tint);
    tile->SetBorders(0.0f, UltraCanvas::Colors::Transparent, 10.0f);
    tile->layout.SetFlexRow()
                .SetFlexJustifyContent(UltraCanvas::CSSLayout::JustifyContent::Center)
                .SetFlexAlignItems(UltraCanvas::CSSLayout::AlignItems::Center);
    tile->layoutItem.SetFlexShrink(0);
    tile->AddChild(MakeIconGlyph(id + ".icon", MailIcon(icon), 22.0f, ink));
    header->AddChild(tile);

    auto words = UltraCanvas::CreateContainer(id + ".words", 0, 0, 0, 0);
    words->layout.SetFlexColumn()
                 .SetFlexGap(2)
                 .SetFlexAlignItems(UltraCanvas::CSSLayout::AlignItems::Stretch);
    words->SetContainerStyle(style);
    auto heading = MakeLine(id + ".title", title, kTitleHeight, SizeHeading(), TextPrimary,
                            UltraCanvas::FontWeight::Bold);
    words->AddChild(heading);
    if (!text.empty()) {
        auto line = MakeLine(id + ".text", text, textHeight, SizeBody(), TextSecondary);
        line->SetWrap(UltraCanvas::TextWrap::WrapWord);
        words->AddChild(line);
    }
    header->AddChild(words);
    words->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    header->SetElementSize(UltraCanvas::Size2Df(
        0, std::max(kTile, kTitleHeight + (text.empty() ? 0.0f : 2.0f + textHeight))));
    header->layoutItem.SetFlexShrink(0).SetAlignSelf(UltraCanvas::CSSLayout::AlignSelf::Stretch);
    return header;
}

// A 1px horizontal rule for flex columns.
inline std::shared_ptr<UltraCanvas::UltraCanvasContainer> MakeDivider(const std::string& id) {
    auto rule = UltraCanvas::CreateContainer(id, 0, 0, 0, 1);
    SetFill(rule, Divider);
    rule->layoutItem.SetAlignSelf(UltraCanvas::CSSLayout::AlignSelf::Stretch);
    return rule;
}

// The initial-in-a-tinted-square avatar used for accounts and contacts.
inline std::shared_ptr<UltraCanvas::UltraCanvasContainer>
MakeAvatar(const std::string& id, const std::string& initial, float side = kAvatarSize,
           Role tint = AccentSoft, Role text = Accent) {
    auto box = UltraCanvas::CreateContainer(id, 0, 0, side, side);
    SetFill(box, tint);
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
// (account tiles).
//
// A click on a label inside it reaches it either way: the window hands a
// press the label does not take on to the elements around it. (It did not
// until October 2026, and on an account tile only the padding answered.)
// PassPointerThroughContent() goes further and makes the surface one target
// for the pointer, hover included, so its own tooltip is the one shown.
class ClickSurface : public UltraCanvas::UltraCanvasContainer {
public:
    ClickSurface(const std::string& id, std::function<void()> onActivate)
        : UltraCanvas::UltraCanvasContainer(id, 0, 0, 0, 0), onActivate_(std::move(onActivate)) {}

    // Make the content transparent to the pointer, as an attachment chip's
    // labels are, so every click and hover over the surface is the surface's
    // and lands on it directly. Hit-testing skips a non-interactive element
    // with everything inside it, so the direct children are enough. Their own
    // tooltips are no longer reached: the surface's tooltip has to say what
    // they said.
    void PassPointerThroughContent() {
        for (const auto& child : GetChildren()) child->SetInteractive(false);
    }

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
