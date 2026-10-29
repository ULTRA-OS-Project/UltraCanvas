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
// Version: 0.1.1 - an avatar from the person's picture; a picture in a rounded box; a play mark
// Version: 0.1.0
// Last Modified: 2026-10-29
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasTheme.h"

#include <array>
#include <memory>
#include <string>

namespace ChatCube {

// The theme for the light or the dark look.
UltraCanvas::UltraCanvasTheme ThemeFor(bool dark);

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

// ChatCube's icon of that name (media/icons/chatcube/<name>.svg), drawn as a
// mask in the button's text colour.
std::string IconPath(const std::string& name);

// Up to two letters for a name ("Bob Smith" -> "BS", "alice" -> "A", a group
// "Team" -> "T"); the first code point of the first two words, so a name in
// any script gets its first letters.
std::string Initials(const std::string& title);
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
// A presence dot at the avatar's lower right, ringed in the surface it sits on.
void PaintPresence(UltraCanvas::IRenderContext* ctx, const UltraCanvas::Point2Dd& avatarCentre, double avatarRadius,
                   const UltraCanvas::Color& presence, const UltraCanvas::Color& ring);

// An element coloured by the theme for its part, now and on every change.
void FollowTheme(const std::shared_ptr<UltraCanvas::UltraCanvasLabel>& label, UltraCanvas::LabelVariant variant);
void FollowTheme(const std::shared_ptr<UltraCanvas::UltraCanvasButton>& button, UltraCanvas::ButtonVariant variant);

} // namespace ChatCube
