// Apps/UltraAuthenticator/Theme.h
// One place for the app's colours, type sizes, metrics and the small styling
// helpers every screen uses.
//
// This exists so the window and the dialogs cannot drift apart. The first
// build styled each screen where it was built, and the result looked like
// three different programs: the same "secondary text" was three greys, and
// every dialog picked its own margin.
//
// Nothing here paints anything. These are values handed to catalogue elements
// through their own SetStyle/SetFont/SetTextColor/SetBorders APIs, per the
// framework rule that applications never hand-roll a widget. The palette is
// UltraPassword's (Apps/UltraPassword/ui/Theme.h), so the two vault apps read
// as one family.
//
// Version: 0.2.0 - button, input and card helpers; icons; monogram colours
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef AUTHENTICATOR_THEME_H
#define AUTHENTICATOR_THEME_H

#include "UltraCanvasButton.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasUtils.h"

#include <cstdint>
#include <memory>
#include <string>

namespace UltraCanvas {
namespace Authenticator {
namespace Theme {

// ---------------------------------------------------------------------------
// Colour
// ---------------------------------------------------------------------------
// A near-white page with white cards, so a card reads as raised; a hairline
// border and rounded corners do the rest of the lifting.
inline const Color kPageBackground   {245, 246, 248};
inline const Color kCardBackground   {255, 255, 255};
inline const Color kCardBorder       {226, 229, 234};
inline const Color kDivider          {234, 236, 240};

inline const Color kTextPrimary      { 24,  28,  35};
inline const Color kTextSecondary    {104, 112, 125};
inline const Color kTextMuted        {142, 150, 163};

inline const Color kAccent           { 37,  99, 235};   // links, focus, primary action
inline const Color kAccentHover      { 29,  78, 216};
inline const Color kAccentPressed    { 30,  64, 175};
inline const Color kAccentSoft       {232, 240, 254};
inline const Color kHover            {241, 243, 246};   // a ghost button under the pointer
inline const Color kPressed          {226, 232, 240};

inline const Color kDanger           {197,  48,  48};   // destructive actions
inline const Color kDangerSoft       {254, 236, 236};

// The code itself: near-black with a touch of the accent, so the one thing
// the user came for is the strongest ink on the card without shouting.
inline const Color kCodeText         { 22,  38,  84};

// The countdown runs through these as the step expires. Colour is the second
// signal, not the only one: the number is always shown, because a user who
// cannot distinguish these greens and reds still needs to know.
inline const Color kCountdownCalm    { 22, 128,  93};   // plenty of time
inline const Color kCountdownWarn    {181, 111,  16};   // getting short
inline const Color kCountdownUrgent  {197,  48,  48};   // about to roll over
inline const Color kCountdownTrack   {230, 233, 238};   // the part already spent

// Seconds at or below which the countdown escalates.
constexpr uint32_t kCountdownWarnAt   = 10;
constexpr uint32_t kCountdownUrgentAt = 5;

inline Color CountdownColor(uint32_t secondsRemaining) {
    if (secondsRemaining <= kCountdownUrgentAt) return kCountdownUrgent;
    if (secondsRemaining <= kCountdownWarnAt)   return kCountdownWarn;
    return kCountdownCalm;
}

// The round monogram at the left of a card. The colour is picked from the
// issuer's name, so one service keeps one colour across launches and two
// services next to each other are told apart at a glance - before the name is
// read. Mid-tone hues, all dark enough for white text on them.
inline Color MonogramColor(const std::string& name) {
    static const Color kPalette[] = {
        { 37,  99, 235},   // blue
        {124,  58, 237},   // violet
        {219,  39, 119},   // pink
        {220,  38,  38},   // red
        {234,  88,  12},   // orange
        {161,  98,   7},   // amber
        { 22, 163,  74},   // green
        { 13, 148, 136},   // teal
        {  8, 145, 178},   // cyan
        { 71,  85, 105},   // slate
    };
    // FNV-1a: stable across runs and platforms, unlike std::hash.
    uint32_t hash = 2166136261u;
    for (unsigned char c : name) {
        hash ^= c;
        hash *= 16777619u;
    }
    return kPalette[hash % (sizeof(kPalette) / sizeof(kPalette[0]))];
}

// ---------------------------------------------------------------------------
// Type
// ---------------------------------------------------------------------------
// The code is the one thing a user is here to read, usually while typing it
// into another window, so it is by far the largest text on screen.
inline const std::string kUiFont   = "Sans";
// Digit shapes must not shift as the code changes, or the row jitters once a
// second. A monospaced face is the fix.
inline const std::string kCodeFont = "Monospace";

constexpr float kSizeTitle     = 19.0f;
constexpr float kSizeHeading   = 15.0f;
constexpr float kSizeCode      = 28.0f;
constexpr float kSizeBody      = 13.0f;
constexpr float kSizeSecondary = 12.0f;
constexpr float kSizeSmall     = 11.0f;

// ---------------------------------------------------------------------------
// Metrics
// ---------------------------------------------------------------------------
constexpr long kMargin      = 22;   // page gutter
constexpr long kCardPadding = 16;
constexpr long kCardGap     = 10;
// Tall enough for three stacked bands — identity, subtitle, code — each with
// its own padding. At 86 the code label ran flush into the bottom border and
// the HOTP button was clipped against it.
constexpr long kCardHeight  = 104;
constexpr float kCardRadius = 10.0f;

constexpr long  kButtonHeight  = 32;
constexpr long  kButtonGap     = 8;
constexpr float kButtonRadius  = 6.0f;
constexpr long  kIconButton    = 30;   // square, icon-only (a card's actions)
constexpr int   kIconSize      = 16;
constexpr long  kMonogramSize  = 40;
// The countdown ring. The gauge keeps its ring at least 40 px across and pads
// it, so the element is larger than the ring it draws.
constexpr long  kRingBox       = 64;

// Button widths. These are sized to their longest label: the catalogue button
// clips rather than shrinking its text, so "Show key" in an 84px button
// renders as "Show ...", which reads as a menu that opens something.
constexpr long kButtonScan     = 146;
constexpr long kButtonEnterKey = 116;
constexpr long kButtonBackup   = 110;
constexpr long kButtonRestore  = 110;
constexpr long kButtonPassword = 118;
constexpr long kButtonLock     = 84;
constexpr long kButtonNext     = 122;
constexpr long kButtonDialog   = 100;   // OK / Cancel at the foot of a dialog

// ---------------------------------------------------------------------------
// Icons
// ---------------------------------------------------------------------------
// media/icons/authenticator/<name>.svg — one-colour line icons, drawn as a
// mask in the button's text colour (see that folder's README).
inline std::string IconPath(const std::string& name) {
    return NormalizePath(GetResourcesDir() + "media/icons/authenticator/" + name + ".svg");
}

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------
// Primary: the one action a screen is for (Scan, Unlock, Add, Save).
inline ButtonStyle PrimaryButton() {
    ButtonStyle s;
    s.normalColor       = kAccent;
    s.hoverColor        = kAccentHover;
    s.pressedColor      = kAccentPressed;
    s.disabledColor     = Color(191, 205, 235, 255);
    s.normalTextColor   = Colors::White;
    s.hoverTextColor    = Colors::White;
    s.pressedTextColor  = Colors::White;
    s.disabledTextColor = Colors::White;
    s.borderWidth       = 0.0f;
    s.borderColor       = kAccent;
    s.cornerRadius      = kButtonRadius;
    s.fontFamily        = kUiFont;
    s.fontSize          = kSizeSecondary;
    s.fontWeight        = FontWeight::Bold;
    return s;
}

// Secondary: everything else that sits in a row of buttons.
inline ButtonStyle SecondaryButton() {
    ButtonStyle s;
    s.normalColor       = kCardBackground;
    s.hoverColor        = kHover;
    s.pressedColor      = kPressed;
    s.disabledColor     = kPageBackground;
    s.normalTextColor   = kTextPrimary;
    s.hoverTextColor    = kTextPrimary;
    s.pressedTextColor  = kTextPrimary;
    s.disabledTextColor = kTextMuted;
    s.borderWidth       = 1.0f;
    s.borderColor       = kCardBorder;
    s.cornerRadius      = kButtonRadius;
    s.fontFamily        = kUiFont;
    s.fontSize          = kSizeSecondary;
    return s;
}

// Ghost: no fill until the pointer is over it — the icon buttons on a card,
// which would otherwise put three boxes on every row.
inline ButtonStyle GhostButton() {
    ButtonStyle s = SecondaryButton();
    s.normalColor     = Colors::Transparent;
    s.borderWidth     = 0.0f;
    s.normalTextColor = kTextSecondary;
    s.hoverTextColor  = kTextPrimary;
    return s;
}

// A ghost button whose action destroys something: red ink, red wash on hover.
inline ButtonStyle GhostDangerButton() {
    ButtonStyle s = GhostButton();
    s.hoverColor       = kDangerSoft;
    s.pressedColor     = Color(252, 215, 215, 255);
    s.normalTextColor  = kTextSecondary;
    s.hoverTextColor   = kDanger;
    s.pressedTextColor = kDanger;
    return s;
}

inline void StyleButton(const std::shared_ptr<UltraCanvasButton>& b,
                        const ButtonStyle& style,
                        const std::string& icon = std::string()) {
    if (!b) return;
    b->SetStyle(style);
    if (!icon.empty()) {
        b->SetIcon(IconPath(icon));
        b->SetIconPosition(ButtonIconPosition::Left);
        b->SetIconSize(kIconSize, kIconSize);
        b->SetIconSpacing(8);
        b->SetUseIconAsMask(true);
    }
}

inline void StylePrimary(const std::shared_ptr<UltraCanvasButton>& b,
                         const std::string& icon = std::string()) {
    StyleButton(b, PrimaryButton(), icon);
}
inline void StyleSecondary(const std::shared_ptr<UltraCanvasButton>& b,
                           const std::string& icon = std::string()) {
    StyleButton(b, SecondaryButton(), icon);
}

// An icon-only button. The tooltip carries the words the button has no room
// for, so the action is still named on hover and to a screen reader.
inline void StyleIconButton(const std::shared_ptr<UltraCanvasButton>& b,
                            const std::string& icon, const std::string& tooltip,
                            bool danger = false) {
    if (!b) return;
    b->SetStyle(danger ? GhostDangerButton() : GhostButton());
    b->SetText("");
    b->SetIcon(IconPath(icon));
    b->SetIconPosition(ButtonIconPosition::Center);
    b->SetIconSize(kIconSize, kIconSize);
    b->SetUseIconAsMask(true);
    b->SetTooltip(tooltip);
}

// ---------------------------------------------------------------------------
// Inputs
// ---------------------------------------------------------------------------
inline TextInputStyle InputStyle(const TextInputStyle& base) {
    TextInputStyle s = base;
    s.borderColor      = kCardBorder;
    s.focusBorderColor = kAccent;
    s.validBorderColor = kCardBorder;
    s.textColor        = kTextPrimary;
    s.placeholderColor = kTextMuted;
    s.selectionColor   = Color(kAccent.r, kAccent.g, kAccent.b, 70);
    s.caretColor       = kTextPrimary;
    s.borderRadius     = static_cast<int>(kButtonRadius);
    s.paddingLeft      = 10;
    s.paddingRight     = 10;
    return s;
}
inline void StyleInput(const std::shared_ptr<UltraCanvasTextInput>& in) {
    if (in) in->SetStyle(InputStyle(in->GetStyle()));
}

inline void StyleDropdown(const std::shared_ptr<UltraCanvasDropdown>& dd) {
    if (!dd) return;
    DropdownStyle s = dd->GetStyle();
    s.borderColor        = kCardBorder;
    s.focusBorderColor   = kAccent;
    s.listBorderColor    = kCardBorder;
    s.hoverColor         = kHover;
    s.pressedColor       = kPressed;
    s.itemHoverColor     = kHover;
    s.itemSelectedColor  = kAccentSoft;
    s.normalTextColor    = kTextPrimary;
    dd->SetStyle(s);
}

// ---------------------------------------------------------------------------
// Surfaces
// ---------------------------------------------------------------------------
inline void ApplyCard(const std::shared_ptr<UltraCanvasContainer>& c,
                      float radius = kCardRadius) {
    if (!c) return;
    c->SetBackgroundColor(kCardBackground);
    c->SetBorders(1.0f, kCardBorder, radius);
}

} // namespace Theme
} // namespace Authenticator
} // namespace UltraCanvas

#endif // AUTHENTICATOR_THEME_H
