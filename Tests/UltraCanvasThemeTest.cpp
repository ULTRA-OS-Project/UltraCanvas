// Tests/UltraCanvasThemeTest.cpp
// The framework theme without a display: the two presets, element roles,
// the current theme, and the styles the timeline and the timecode field
// take from a theme - shared roles mapped, element roles honoured, the
// palette's colour where a theme is silent.
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#include "UltraCanvasTheme.h"
#include "UltraCanvasTimecodeField.h"
#include "UltraCanvasVideoTimeline.h"

#include <cstdio>

using namespace UltraCanvas;

static int testCount = 0, failCount = 0;
#define CHECK(cond, name) do { ++testCount; if (cond) std::printf("  PASS  %s\n", name); else { ++failCount; std::printf("  FAIL  %s (line %d)\n", name, __LINE__); } } while (0)

static bool Same(const Color& a, const Color& b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

int main() {
    std::printf("UltraCanvasTheme\n");
    const UltraCanvasTheme light = UltraCanvasTheme::Light();
    const UltraCanvasTheme dark = UltraCanvasTheme::Dark();
    CHECK(!light.dark && dark.dark && light.name == "Light" && dark.name == "Dark", "the two presets");
    CHECK(Same(light.roles.ground, Colors::WindowBackground) && Same(light.roles.accent, Colors::Selection) && Same(light.roles.ink, Colors::TextDefault),
          "Light is the framework's default colours");
    CHECK(dark.roles.panel.r < 60 && dark.roles.ink.r > 200, "Dark is dark");
    CHECK(Same(UltraCanvasTheme::Current().roles.ground, light.roles.ground) && !UltraCanvasTheme::Current().dark, "Light until an app sets another");

    UltraCanvasTheme own = dark;
    own.name = "Own";
    CHECK(!own.HasRole("video-timeline.playhead") && Same(own.Role("video-timeline.playhead", Colors::Red), Colors::Red), "an unnamed element role falls back");
    own.SetRole("video-timeline.playhead", Color(1, 2, 3));
    own.SetRole("video-timeline.clip-video", Color(4, 5, 6));
    CHECK(own.HasRole("video-timeline.playhead") && Same(own.Role("video-timeline.playhead", Colors::Red), Color(1, 2, 3)) && own.ElementRoles().size() == 2,
          "a named element role is read");
    UltraCanvasTheme::SetCurrent(own);
    CHECK(UltraCanvasTheme::Current().name == "Own" && UltraCanvasTheme::Current().dark, "SetCurrent makes it the one in force");

    std::printf("VideoTimelineStyle::FromTheme\n");
    const VideoTimelineStyle fromOwn = VideoTimelineStyle::FromTheme(own);
    CHECK(Same(fromOwn.background, own.roles.panel) && Same(fromOwn.lane, own.roles.well) && Same(fromOwn.header, own.roles.raised) &&
          Same(fromOwn.headerSelected, own.roles.accentSoft) && Same(fromOwn.ink, own.roles.ink) && Same(fromOwn.focus, own.roles.focus),
          "the shared roles fill the surfaces, ink, accent and focus");
    CHECK(Same(fromOwn.playhead, Color(1, 2, 3)) && Same(fromOwn.clipVideo, Color(4, 5, 6)), "the element roles the theme names");
    const VideoTimelineStyle darkPalette = VideoTimelineStyle::DarkPalette();
    CHECK(Same(fromOwn.clipAudio, darkPalette.clipAudio) && Same(fromOwn.waveform, darkPalette.waveform) && Same(fromOwn.beat, darkPalette.beat),
          "the dark palette's colours where a dark theme is silent");
    CHECK(fromOwn.dropSoft.r == own.roles.focus.r && fromOwn.dropSoft.a == 31 && fromOwn.rangeFill.r == own.roles.accent.r && fromOwn.rangeFill.a == 36,
          "the drop and range tints are the focus and accent roles, translucent");
    const VideoTimelineStyle fromLight = VideoTimelineStyle::FromTheme(light);
    CHECK(Same(fromLight.clipVideo, VideoTimelineStyle::LightPalette().clipVideo) && Same(fromLight.background, Colors::White),
          "a light theme: the light palette's clip colours, the panel role behind the lanes");
    CHECK(Same(VideoTimelineStyle::FromTheme().playhead, Color(1, 2, 3)), "FromTheme() reads the current theme");
    CHECK(fromOwn.rulerHeight == darkPalette.rulerHeight && fromOwn.fontFamily == darkPalette.fontFamily, "sizes and fonts stay the defaults");

    std::printf("TimecodeFieldStyle::FromTheme\n");
    const TimecodeFieldStyle field = TimecodeFieldStyle::FromTheme(own);
    CHECK(Same(field.background, own.roles.well) && Same(field.border, own.roles.lineStrong) && Same(field.borderFocused, own.roles.focus) &&
          Same(field.borderInvalid, own.roles.danger), "the well, the border, the focus ring, danger");
    CHECK(Same(field.ink, own.roles.ink) && Same(field.inkFaint, own.roles.inkFaint) && Same(field.selection, own.roles.accent) &&
          Same(field.onSelection, own.roles.onAccent) && Same(field.sign, own.roles.accent), "ink, the selected segment and the sign from the accent");
    CHECK(field.fontFamily == TimecodeFieldStyle().fontFamily && field.fontSize == TimecodeFieldStyle().fontSize, "fonts stay the defaults");

    UltraCanvasTheme::SetCurrent(light);
    std::printf("\n%d of %d checks passed\n", testCount - failCount, testCount);
    return failCount ? 1 : 0;
}
