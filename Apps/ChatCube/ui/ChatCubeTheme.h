// Apps/ChatCube/ui/ChatCubeTheme.h
// ChatCube's look as a framework theme: the ChatCube green for the accent on
// warm neutrals, in a light and a dark look (`--dark`), so every element of
// the window that follows the theme (UltraCanvasUIElement::SetFollowTheme)
// takes its colours from one place. What the window paints itself - the
// conversation rows, the chat bubbles, the avatars - reads the same theme
// through Look, with ChatCube's own roles ("chatcube.bubble-own", the
// presence colours, the avatar palette) and a fallback for each, so a theme
// that does not name them still looks right.
//
// Version: 0.2.0 - the look follows a change of the theme while ChatCube runs: L() read again by
//                  RestyleForLook, OnLook for every colour set by hand, SetInk for a label's role;
//                  SetLookChoice / DarkLook: light or dark decided again at every change
// Version: 0.1.3 - InkOn: an avatar's initials in white or the dark ink, whichever reads on its colour
// Version: 0.1.2 - PaintTicks: a bubble's ticks drawn as strokes, not as a glyph a font may lack
// Version: 0.1.1 - an avatar from the person's picture; a picture in a rounded box; a play mark
// Version: 0.1.0
// Last Modified: 2026-11-14
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasTheme.h"

#include <array>
#include <functional>
#include <memory>
#include <string>

namespace ChatCube {

// The theme for the light or the dark look.
UltraCanvas::UltraCanvasTheme ThemeFor(bool dark);

// Which look: the Settings window's choice ("system", "light", "dark") with
// --dark over it. main.cpp's theme provider asks DarkLook with the desktop's
// look, at the start and on every change of the desktop; the window sets
// the choice again when the Settings window changes it, and SetLookChoice
// says whether it changed (the theme is then built again).
bool SetLookChoice(const std::string& look);
void ForceDarkLook();           // --dark
bool DarkLook(bool hostDark);

// The colours the window's own painting reads, from the theme's roles.
struct Look {
    UltraCanvas::Color ground, panel, well, hover, pressed, line, lineStrong;
    UltraCanvas::Color ink, inkMuted, inkFaint;
    UltraCanvas::Color accent, accentHover, accentSoft, onAccent, danger;
    UltraCanvas::Color sidebar;                          // the left column
    UltraCanvas::Color bubbleOwn, bubbleOwnInk;          // our messages
    UltraCanvas::Color bubbleOther, bubbleOtherLine;     // theirs (ink: the theme's)
    UltraCanvas::Color presenceOnline, presenceAway, presenceBusy, presenceOffline;
    UltraCanvas::Color callBar;                          // the bar of a call
    std::array<UltraCanvas::Color, 8> avatars;           // one per person, by their id
};
Look LookOf(const UltraCanvas::UltraCanvasTheme& theme);
Look CurrentLook();

// The colours of the theme in force (main.cpp: ChatCube::ThemeFor, light or
// dark). Kept, and read again by RestyleForLook when the theme changes.
const Look& L();

// A colour of the look, by its role: &Look::inkMuted.
using LookRole = UltraCanvas::Color Look::*;

// The look can change while ChatCube runs - the desktop turns dark, the
// Settings window's Look page is changed. Every colour set by hand is set
// through OnLook: `apply` runs now and again after every change, for as long
// as `element` lives. RestyleForLook reads L() again and runs them all; the
// main window calls it from its theme change handler.
void AddLookRestyler(std::weak_ptr<void> owner, std::function<void()> apply);
void RestyleForLook();
template <class E, class F>
void OnLook(const std::shared_ptr<E>& element, F apply) {
    if (!element) return;
    E* raw = element.get();
    apply(*raw);
    AddLookRestyler(std::weak_ptr<void>(element), [raw, apply]() { apply(*raw); });
}

// A label's text in a role of the look (a status line turning to danger and
// back): set now, and kept in that role when the look changes.
void SetInk(const std::shared_ptr<UltraCanvas::UltraCanvasLabel>& label, LookRole role);

// ChatCube's icon of that name (media/icons/chatcube/<name>.svg), drawn as a
// mask in the button's text colour.
std::string IconPath(const std::string& name);

// Up to two letters for a name ("Bob Smith" -> "BS", "alice" -> "A", a group
// "Team" -> "T"); the first code point of the first two words, so a name in
// any script gets its first letters.
std::string Initials(const std::string& title);
// The words on a colour painted by hand (an avatar's initials): white or a
// near-black ink, whichever reads better on it.
UltraCanvas::Color InkOn(const UltraCanvas::Color& fill);
// The avatar colour of a person or group, by a stable key (their id).
UltraCanvas::Color AvatarColor(const std::string& key, const Look& look);
// A round avatar: the initials on the person's colour, or a group mark.
void PaintAvatar(UltraCanvas::IRenderContext* ctx, const UltraCanvas::Point2Dd& centre, double radius,
                 const std::string& title, const std::string& key, bool isGroup, const Look& look);
// The same with the person's or the group's picture when there is one
// (avatarPath: a file the provider fetched) and it can be read; else as above.
void PaintAvatar(UltraCanvas::IRenderContext* ctx, const UltraCanvas::Point2Dd& centre, double radius,
                 const std::string& title, const std::string& key, bool isGroup, const std::string& avatarPath,
                 const Look& look);
// A picture from a file, fitted to cover `rect` and clipped to its rounded
// corners. False, and nothing painted, when the file cannot be read.
bool PaintPicture(UltraCanvas::IRenderContext* ctx, const std::string& path, const UltraCanvas::Rect2Dd& rect,
                  double radius);
// A play mark - a white triangle on a dark disc - over a video's preview picture.
void PaintPlayMark(UltraCanvas::IRenderContext* ctx, const UltraCanvas::Point2Dd& centre, double radius);
// A message's ticks - one (sent) or two (delivered, read) check marks - as
// strokes, right-aligned in `area` and centred on its height. Drawn, not
// typed: U+2713 is in no font a Windows installation is sure to have, and
// Segoe UI leaves a box where the tick should be. TicksWidth is the room
// they take at that height, for laying the line out.
void PaintTicks(UltraCanvas::IRenderContext* ctx, const UltraCanvas::Rect2Dd& area, int count,
                const UltraCanvas::Color& color);
int  TicksWidth(int count, double height);
// A pin - the mark of a conversation pinned to the top of the list - as
// strokes centred in `area` (the shape of media/icons/clipboard/pin.svg).
void PaintPin(UltraCanvas::IRenderContext* ctx, const UltraCanvas::Rect2Dd& area, const UltraCanvas::Color& color);
// A presence dot at the avatar's lower right, ringed in the surface it sits on.
void PaintPresence(UltraCanvas::IRenderContext* ctx, const UltraCanvas::Point2Dd& avatarCentre, double avatarRadius,
                   const UltraCanvas::Color& presence, const UltraCanvas::Color& ring);

// An element coloured by the theme for its part, now and on every change.
void FollowTheme(const std::shared_ptr<UltraCanvas::UltraCanvasLabel>& label, UltraCanvas::LabelVariant variant);
void FollowTheme(const std::shared_ptr<UltraCanvas::UltraCanvasButton>& button, UltraCanvas::ButtonVariant variant);

} // namespace ChatCube
