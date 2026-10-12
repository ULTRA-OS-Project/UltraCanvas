// Apps/UltraVideo/UltraVideoTheme.h
// The design's tokens (UltraVideoTokens.h) applied to framework elements:
// colours as UltraCanvas::Color, the ghost and primary buttons of the brand
// book, the toggle look of an active tool, and the palette as a framework
// theme (ThemeFor) - the shared roles from the surfaces, ink, accent, focus
// and danger tokens, the timeline's own roles from the clip, guide and
// playhead tokens - which the timeline and the timecode field read through
// their FromTheme. Everything the window colours goes through here, so
// switching the palette is one call per panel.
// Version: 0.2.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#pragma once

#include "UltraVideoTokens.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasTheme.h"
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

// The palette as the framework theme (proposal §5.8): the surfaces, lines,
// ink, accent, focus and danger tokens fill the shared roles every themed
// element reads; the clip kinds, waveform, transition badge, guides and
// playhead are the timeline's own roles, named as its FromTheme reads them.
// The window sets it as the current theme and re-applies its elements.
inline UltraCanvas::UltraCanvasTheme ThemeFor(const Palette& p) {
    UltraCanvas::UltraCanvasTheme t;
    t.name = &p == &Tokens::Dark ? "UltraVideo dark" : "UltraVideo light";
    t.dark = &p == &Tokens::Dark;
    UltraCanvas::ThemeRoles& r = t.roles;
    r.ground = ToColor(p.Bg000);
    r.panel = ToColor(p.Bg100);
    r.well = ToColor(p.Bg200);
    r.raised = ToColor(p.Bg300);
    r.hover = ToColor(p.Bg400);
    r.pressed = ToColor(p.Bg500);
    r.line = ToColor(p.Line);
    r.lineStrong = ToColor(p.LineStrong);
    r.ink = ToColor(p.Ink);
    r.inkMuted = ToColor(p.InkMuted);
    r.inkFaint = ToColor(p.InkFaint);
    r.accent = ToColor(p.Accent);
    r.accentHover = ToColor(p.AccentHover);
    r.accentSoft = ToColor(p.AccentSoft);
    r.onAccent = ToColor(p.OnAccent);
    r.focus = ToColor(p.Focus);
    r.danger = ToColor(p.Danger);
    // the controls' own roles: an ink handle on the slider, the on-accent thumb on the switch
    t.SetRole("slider.handle", ToColor(p.Ink));
    t.SetRole("slider.handle-border", ToColor(p.Bg100));
    t.SetRole("switch.thumb", ToColor(p.OnAccent));
    t.SetRole("switch.thumb-border", ToColor(p.LineStrong));
    t.SetRole("video-timeline.playhead", ToColor(p.Playhead));
    t.SetRole("video-timeline.clip-video", ToColor(p.ClipVideo));
    t.SetRole("video-timeline.clip-video-edge", ToColor(p.ClipVideoEdge));
    t.SetRole("video-timeline.clip-image", ToColor(p.ClipImage));
    t.SetRole("video-timeline.clip-image-edge", ToColor(p.ClipImageEdge));
    t.SetRole("video-timeline.clip-card", ToColor(p.ClipCard));
    t.SetRole("video-timeline.clip-card-edge", ToColor(p.ClipCardEdge));
    t.SetRole("video-timeline.clip-title", ToColor(p.ClipTitle));
    t.SetRole("video-timeline.clip-title-edge", ToColor(p.ClipTitleEdge));
    t.SetRole("video-timeline.clip-audio", ToColor(p.ClipAudio));
    t.SetRole("video-timeline.clip-audio-edge", ToColor(p.ClipAudioEdge));
    t.SetRole("video-timeline.waveform", ToColor(p.Waveform));
    t.SetRole("video-timeline.transition-fill", ToColor(p.TransitionFill));
    t.SetRole("video-timeline.on-transition", ToColor(p.OnTransition));
    t.SetRole("video-timeline.beat", ToColor(p.Beat));
    t.SetRole("video-timeline.trim-overflow", ToColor(p.TrimOverflow));
    t.SetRole("video-timeline.drop-soft", ToColor(p.DropSoft));
    t.SetRole("video-timeline.range-fill", ToColor(p.RangeFill));
    t.SetRole("video-timeline.muted-hatch", ToColor(p.MutedHatch));
    t.SetRole("video-timeline.scrollbar", ToColor(p.LineStrong));
    return t;
}

// The timeline's and the timecode field's styles: the theme's colours, the
// design's type sizes
inline UltraCanvas::VideoTimelineStyle TimelineStyleFor(const Palette& p) {
    UltraCanvas::VideoTimelineStyle s = UltraCanvas::VideoTimelineStyle::FromTheme(ThemeFor(p));
    s.labelSize = Tokens::Label.size;
    s.captionSize = Tokens::Caption.size;
    s.rulerSize = Tokens::TimecodeSm.size;
    s.readoutSize = Tokens::Timecode.size;
    return s;
}

inline UltraCanvas::TimecodeFieldStyle TimecodeStyleFor(const Palette& p) {
    UltraCanvas::TimecodeFieldStyle s = UltraCanvas::TimecodeFieldStyle::FromTheme(ThemeFor(p));
    s.fontSize = Tokens::Timecode.size;
    s.readoutSize = Tokens::TimecodeLg.size;
    return s;
}

} // namespace UltraVideo
