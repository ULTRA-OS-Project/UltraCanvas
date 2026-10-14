// Apps/UltraVideo/UltraVideoTheme.h
// The design's tokens (UltraVideoTokens.h) applied to framework elements:
// colours as UltraCanvas::Color, the ghost and primary buttons of the brand
// book, the toggle look of an active tool, and the timeline's and the
// timecode field's styles filled from a palette. Everything the window
// colours goes through here, so switching the palette is one call per panel.
// Version: 0.1.0
// Last Modified: 2026-10-11
// Author: UltraCanvas Framework
#pragma once

#include "UltraVideoTokens.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasTimecodeField.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasVideoTimeline.h"

#include <memory>
#include <string>

namespace UltraVideo {

using Palette = Tokens::Palette;

inline UltraCanvas::Color ToColor(const Tokens::RGBA& c) {
    return UltraCanvas::Color(c.r, c.g, c.b, c.a);
}

inline const Palette& PaletteFor(bool dark) { return dark ? Tokens::Dark : Tokens::Light; }

// An interface icon from media/icons/ultravideo (the design's set)
inline std::string IconPath(const std::string& name) {
    return UltraCanvas::NormalizePath(UltraCanvas::GetResourcesDir() + "media/icons/ultravideo/" + name + ".svg");
}

// A ghost icon button: no fill at rest, the hover fill, the icon in ink
inline UltraCanvas::ButtonStyle GhostButtonStyle(const Palette& p) {
    UltraCanvas::ButtonStyle s;
    s.normalColor = UltraCanvas::Colors::Transparent;
    s.hoverColor = ToColor(p.Bg400);
    s.pressedColor = ToColor(p.Bg500);
    s.disabledColor = UltraCanvas::Colors::Transparent;
    s.borderColor = UltraCanvas::Colors::Transparent;
    s.borderWidth = 0.0f;
    s.cornerRadius = Tokens::RadiusSm;
    s.useIconAsMask = true;
    s.normalTextColor = ToColor(p.Ink);
    s.hoverTextColor = ToColor(p.Ink);
    s.pressedTextColor = ToColor(p.Ink);
    s.disabledTextColor = ToColor(p.InkFaint);
    s.fontSize = Tokens::Label.size * 0.75f;
    return s;
}

// The one primary button on screen: the accent fill
inline UltraCanvas::ButtonStyle PrimaryButtonStyle(const Palette& p) {
    UltraCanvas::ButtonStyle s = GhostButtonStyle(p);
    s.normalColor = ToColor(p.Accent);
    s.hoverColor = ToColor(p.AccentHover);
    s.pressedColor = ToColor(p.AccentHover);
    s.normalTextColor = ToColor(p.OnAccent);
    s.hoverTextColor = ToColor(p.OnAccent);
    s.pressedTextColor = ToColor(p.OnAccent);
    s.fontWeight = UltraCanvas::FontWeight::Bold;
    return s;
}

// A secondary text button: the raised surface with a control border
inline UltraCanvas::ButtonStyle PlainButtonStyle(const Palette& p) {
    UltraCanvas::ButtonStyle s = GhostButtonStyle(p);
    s.normalColor = ToColor(p.Bg300);
    s.borderColor = ToColor(p.LineStrong);
    s.borderWidth = 1.0f;
    return s;
}

inline std::shared_ptr<UltraCanvas::UltraCanvasButton> MakeIconButton(const std::string& id, const std::string& icon,
                                                                      const std::string& tooltip, const Palette& p,
                                                                      int size = static_cast<int>(Tokens::ControlH)) {
    auto button = std::make_shared<UltraCanvas::UltraCanvasButton>(id, 0, 0, size, size, "");
    button->SetStyle(GhostButtonStyle(p));
    button->SetIcon(IconPath(icon));
    button->SetIconSize(static_cast<int>(Tokens::IconSize), static_cast<int>(Tokens::IconSize));
    button->SetTooltip(tooltip);
    button->layoutItem.SetFlexShrink(0);
    return button;
}

inline std::shared_ptr<UltraCanvas::UltraCanvasButton> MakeTextButton(const std::string& id, const std::string& text,
                                                                      const Palette& p, int width, bool primary = false) {
    auto button = std::make_shared<UltraCanvas::UltraCanvasButton>(id, 0, 0, width, static_cast<int>(Tokens::ControlH), text);
    button->SetStyle(primary ? PrimaryButtonStyle(p) : PlainButtonStyle(p));
    button->layoutItem.SetFlexShrink(0);
    return button;
}

// An active tool or toggle: the soft accent fill with the accent icon
inline void SetToggleLook(UltraCanvas::UltraCanvasButton& button, bool on, const Palette& p) {
    UltraCanvas::ButtonStyle s = GhostButtonStyle(p);
    if (on) {
        s.normalColor = ToColor(p.AccentSoft);
        s.hoverColor = ToColor(p.AccentSoft);
        s.normalTextColor = ToColor(p.Accent);
        s.hoverTextColor = ToColor(p.Accent);
        s.pressedTextColor = ToColor(p.Accent);
    }
    button.SetStyle(s);
}

inline UltraCanvas::VideoTimelineStyle TimelineStyleFor(const Palette& p) {
    UltraCanvas::VideoTimelineStyle s;
    s.background = ToColor(p.Bg100);
    s.lane = ToColor(p.Bg200);
    s.laneAlternate = ToColor(p.Bg200);
    s.header = ToColor(p.Bg300);
    s.headerSelected = ToColor(p.AccentSoft);
    s.line = ToColor(p.Line);
    s.lineStrong = ToColor(p.LineStrong);
    s.ink = ToColor(p.Ink);
    s.inkMuted = ToColor(p.InkMuted);
    s.inkFaint = ToColor(p.InkFaint);
    s.accent = ToColor(p.Accent);
    s.accentSoft = ToColor(p.AccentSoft);
    s.onAccent = ToColor(p.OnAccent);
    s.playhead = ToColor(p.Playhead);
    s.focus = ToColor(p.Focus);
    s.danger = ToColor(p.Danger);
    s.clipVideo = ToColor(p.ClipVideo);
    s.clipVideoEdge = ToColor(p.ClipVideoEdge);
    s.clipImage = ToColor(p.ClipImage);
    s.clipImageEdge = ToColor(p.ClipImageEdge);
    s.clipCard = ToColor(p.ClipCard);
    s.clipCardEdge = ToColor(p.ClipCardEdge);
    s.clipTitle = ToColor(p.ClipTitle);
    s.clipTitleEdge = ToColor(p.ClipTitleEdge);
    s.clipAudio = ToColor(p.ClipAudio);
    s.clipAudioEdge = ToColor(p.ClipAudioEdge);
    s.waveform = ToColor(p.Waveform);
    s.transitionFill = ToColor(p.TransitionFill);
    s.onTransition = ToColor(p.OnTransition);
    s.beat = ToColor(p.Beat);
    s.trimOverflow = ToColor(p.TrimOverflow);
    s.dropSoft = ToColor(p.DropSoft);
    s.rangeFill = ToColor(p.RangeFill);
    s.mutedHatch = ToColor(p.MutedHatch);
    s.scrollbar = ToColor(p.LineStrong);
    return s;
}

inline UltraCanvas::TimecodeFieldStyle TimecodeStyleFor(const Palette& p) {
    UltraCanvas::TimecodeFieldStyle s;
    s.background = ToColor(p.Bg200);
    s.border = ToColor(p.LineStrong);
    s.borderFocused = ToColor(p.Focus);
    s.borderInvalid = ToColor(p.Danger);
    s.ink = ToColor(p.Ink);
    s.inkFaint = ToColor(p.InkFaint);
    s.selection = ToColor(p.Accent);
    s.onSelection = ToColor(p.OnAccent);
    s.sign = ToColor(p.Accent);
    s.fontSize = Tokens::Timecode.size;
    s.readoutSize = Tokens::TimecodeLg.size;
    return s;
}

} // namespace UltraVideo
