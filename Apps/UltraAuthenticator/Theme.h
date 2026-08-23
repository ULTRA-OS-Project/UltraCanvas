// Apps/UltraAuthenticator/Theme.h
// One place for the app's colours, type sizes and metrics.
//
// This exists so the window and the dialogs cannot drift apart. The first
// build styled each screen where it was built, and the result looked like
// three different programs: the same "secondary text" was three greys, and
// every dialog picked its own margin.
//
// Nothing here paints anything. These are values handed to catalogue elements
// through their own SetFont/SetTextColor/SetBackgroundColor APIs, per the
// framework rule that applications never hand-roll a widget.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef AUTHENTICATOR_THEME_H
#define AUTHENTICATOR_THEME_H

#include "UltraCanvasCommonTypes.h"

#include <string>

namespace UltraCanvas {
namespace Authenticator {
namespace Theme {

// ---------------------------------------------------------------------------
// Colour
// ---------------------------------------------------------------------------
// A near-white page with white cards, so a card reads as raised without a
// shadow — the catalogue has no shadow primitive and faking one would mean
// painting.
inline const Color kPageBackground   {245, 246, 248};
inline const Color kCardBackground   {255, 255, 255};
inline const Color kCardBorder       {226, 229, 234};

inline const Color kTextPrimary      { 24,  28,  35};
inline const Color kTextSecondary    {104, 112, 125};
inline const Color kTextMuted        {142, 150, 163};

inline const Color kAccent           { 37,  99, 235};   // links, focus, title
inline const Color kDanger           {197,  48,  48};   // destructive actions

// The countdown runs through these as the step expires. Colour is the second
// signal, not the only one: the number is always shown, because a user who
// cannot distinguish these greens and reds still needs to know.
inline const Color kCountdownCalm    { 22, 128,  93};   // plenty of time
inline const Color kCountdownWarn    {181, 111,  16};   // getting short
inline const Color kCountdownUrgent  {197,  48,  48};   // about to roll over

// Seconds at or below which the countdown escalates.
constexpr uint32_t kCountdownWarnAt   = 10;
constexpr uint32_t kCountdownUrgentAt = 5;

inline Color CountdownColor(uint32_t secondsRemaining) {
    if (secondsRemaining <= kCountdownUrgentAt) return kCountdownUrgent;
    if (secondsRemaining <= kCountdownWarnAt)   return kCountdownWarn;
    return kCountdownCalm;
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
constexpr float kSizeCode      = 30.0f;
constexpr float kSizeBody      = 13.0f;
constexpr float kSizeSecondary = 12.0f;
constexpr float kSizeSmall     = 11.0f;

// ---------------------------------------------------------------------------
// Metrics
// ---------------------------------------------------------------------------
constexpr long kMargin      = 22;   // page gutter
constexpr long kCardPadding = 14;
constexpr long kCardGap     = 10;
// Tall enough for three stacked bands — identity, subtitle, code — each with
// its own padding. At 86 the code label ran flush into the bottom border and
// the HOTP button was clipped against it.
constexpr long kCardHeight  = 104;

// Button widths. These are sized to their longest label: the catalogue button
// clips rather than shrinking its text, so "Show key" in an 84px button
// renders as "Show ...", which reads as a menu that opens something.
constexpr long kButtonEdit   = 68;
constexpr long kButtonReveal = 96;
constexpr long kButtonRemove = 86;
constexpr long kButtonNext   = 116;
constexpr long kButtonGap    = 8;

} // namespace Theme
} // namespace Authenticator
} // namespace UltraCanvas

#endif // AUTHENTICATOR_THEME_H
