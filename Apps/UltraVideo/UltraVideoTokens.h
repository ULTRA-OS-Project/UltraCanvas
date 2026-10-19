// UltraVideo design tokens — generated from tokens.json. Do not edit by hand; regenerate.
// Values are logical pixels; multiply by the UltraCanvas DPI scale.
#pragma once
#include <cstdint>

namespace UltraVideo::Tokens {

struct RGBA { std::uint8_t r, g, b, a; };

struct Palette {
    RGBA Bg000;   // App ground: the gutters between panels and the area behind the viewer image.
    RGBA Bg100;   // Panel surface: media bin, inspector, timeline body, menus.
    RGBA Bg200;   // Track lanes in the timeline; inset wells such as fields and the waveform lane.
    RGBA Bg300;   // Raised surfaces: track headers, the ruler, toolbar buttons at rest, popovers.
    RGBA Bg400;   // Hover fill for rows, buttons and track headers.
    RGBA Bg500;   // Pressed or active-toggle fill for controls.
    RGBA Line;   // Hairline dividers between tracks, panels and menu sections. Decorative only, never the sole boundary of a cont
    RGBA LineStrong;   // Control borders, splitter grips, ruler ticks. Meets 3:1 on bg-100 and bg-300.
    RGBA Ink;   // Primary text and icons on every bg-* surface and on every clip-* fill.
    RGBA InkMuted;   // Secondary text: track names at rest, inspector labels, duration badges. On bg-000…bg-400.
    RGBA InkFaint;   // Ruler timecodes, placeholders, disabled labels. On bg-100…bg-300 only.
    RGBA Accent;   // Identity amber: primary button fill, selection outlines, active tool, work-area range, the transition drag han
    RGBA AccentHover;   // Hover state of accent fills.
    RGBA AccentSoft;   // Tinted background behind selected list rows and the active tool; text on it stays ink.
    RGBA OnAccent;   // Text and icons on an accent fill.
    RGBA Playhead;   // The playhead line and its ruler handle. Never used for anything else.
    RGBA Focus;   // Keyboard focus ring (2px solid), snap line, file-drop target outline. Meets 3:1 on every surface and clip fill
    RGBA Danger;   // Destructive menu items and error text; always paired with a word.
    RGBA ClipVideo;   // Body of a video clip on a video track.
    RGBA ClipVideoEdge;   // Top band and border of a video clip; trim-handle grip on video clips.
    RGBA ClipImage;   // Body of a still-image clip (photo with motion).
    RGBA ClipImageEdge;   // Top band and border of a still-image clip.
    RGBA ClipCard;   // Body of a colour-card clip when the card colour itself is not shown (collapsed tracks).
    RGBA ClipCardEdge;   // Top band and border of a colour-card clip.
    RGBA ClipTitle;   // Title and overlay spans on the overlay lane.
    RGBA ClipTitleEdge;   // Top band and border of title and overlay spans.
    RGBA ClipAudio;   // Body of the music bar and audio clips.
    RGBA ClipAudioEdge;   // Top band, border and fade ramps of audio.
    RGBA Waveform;   // Waveform peaks drawn on clip-audio.
    RGBA TransitionFill;   // Transition badge over a cut: deliberately inverted so it reads above any clip colour.
    RGBA OnTransition;   // Icon and label on transition-fill.
    RGBA Beat;   // Beat-mark ticks drawn across all lanes. Decorative guide, never interactive.
    RGBA TrimOverflow;   // Ghost extension beyond a clip edge showing unused source media while hovering or trimming.
    RGBA DropSoft;   // Fill of a valid drop zone during a file or media-bin drag.
    RGBA RangeFill;   // Work-area in/out range on the ruler and the lanes beneath it.
    RGBA MutedHatch;   // Scrim laid over a muted clip together with the crossed-speaker icon.
    RGBA MarkerAmber;   // User marker colour 1 (default).
    RGBA MarkerCoral;   // User marker colour 2.
    RGBA MarkerGreen;   // User marker colour 3.
    RGBA MarkerCyan;   // User marker colour 4.
    RGBA MarkerViolet;   // User marker colour 5.
};

inline constexpr Palette Dark {
    /* Bg000 */ {13, 14, 16, 255},
    /* Bg100 */ {21, 23, 26, 255},
    /* Bg200 */ {27, 30, 34, 255},
    /* Bg300 */ {35, 39, 44, 255},
    /* Bg400 */ {46, 51, 57, 255},
    /* Bg500 */ {58, 64, 71, 255},
    /* Line */ {42, 46, 52, 255},
    /* LineStrong */ {115, 123, 133, 255},
    /* Ink */ {236, 238, 240, 255},
    /* InkMuted */ {166, 173, 181, 255},
    /* InkFaint */ {138, 145, 154, 255},
    /* Accent */ {242, 165, 58, 255},
    /* AccentHover */ {255, 185, 87, 255},
    /* AccentSoft */ {58, 44, 20, 255},
    /* OnAccent */ {27, 19, 5, 255},
    /* Playhead */ {255, 93, 79, 255},
    /* Focus */ {91, 196, 245, 255},
    /* Danger */ {255, 111, 97, 255},
    /* ClipVideo */ {35, 74, 95, 255},
    /* ClipVideoEdge */ {79, 147, 181, 255},
    /* ClipImage */ {77, 63, 34, 255},
    /* ClipImageEdge */ {168, 135, 63, 255},
    /* ClipCard */ {52, 56, 62, 255},
    /* ClipCardEdge */ {122, 130, 139, 255},
    /* ClipTitle */ {74, 47, 79, 255},
    /* ClipTitleEdge */ {165, 113, 173, 255},
    /* ClipAudio */ {31, 74, 51, 255},
    /* ClipAudioEdge */ {79, 158, 114, 255},
    /* Waveform */ {143, 214, 170, 255},
    /* TransitionFill */ {217, 221, 226, 255},
    /* OnTransition */ {21, 23, 26, 255},
    /* Beat */ {255, 255, 255, 41},
    /* TrimOverflow */ {255, 255, 255, 26},
    /* DropSoft */ {91, 196, 245, 41},
    /* RangeFill */ {242, 165, 58, 41},
    /* MutedHatch */ {13, 14, 16, 140},
    /* MarkerAmber */ {242, 165, 58, 255},
    /* MarkerCoral */ {255, 138, 122, 255},
    /* MarkerGreen */ {111, 207, 143, 255},
    /* MarkerCyan */ {91, 196, 245, 255},
    /* MarkerViolet */ {195, 156, 245, 255},
};

inline constexpr Palette Light {
    /* Bg000 */ {223, 226, 230, 255},
    /* Bg100 */ {244, 245, 247, 255},
    /* Bg200 */ {236, 238, 241, 255},
    /* Bg300 */ {255, 255, 255, 255},
    /* Bg400 */ {227, 230, 234, 255},
    /* Bg500 */ {212, 216, 221, 255},
    /* Line */ {211, 215, 220, 255},
    /* LineStrong */ {122, 130, 139, 255},
    /* Ink */ {22, 24, 27, 255},
    /* InkMuted */ {74, 81, 89, 255},
    /* InkFaint */ {95, 102, 110, 255},
    /* Accent */ {166, 90, 0, 255},
    /* AccentHover */ {138, 74, 0, 255},
    /* AccentSoft */ {246, 227, 198, 255},
    /* OnAccent */ {255, 255, 255, 255},
    /* Playhead */ {210, 52, 31, 255},
    /* Focus */ {11, 106, 166, 255},
    /* Danger */ {192, 50, 31, 255},
    /* ClipVideo */ {205, 226, 238, 255},
    /* ClipVideoEdge */ {61, 124, 156, 255},
    /* ClipImage */ {239, 224, 191, 255},
    /* ClipImageEdge */ {143, 106, 29, 255},
    /* ClipCard */ {225, 228, 232, 255},
    /* ClipCardEdge */ {124, 131, 139, 255},
    /* ClipTitle */ {234, 217, 238, 255},
    /* ClipTitleEdge */ {135, 80, 143, 255},
    /* ClipAudio */ {207, 232, 216, 255},
    /* ClipAudioEdge */ {47, 122, 77, 255},
    /* Waveform */ {47, 122, 77, 255},
    /* TransitionFill */ {42, 46, 52, 255},
    /* OnTransition */ {244, 245, 247, 255},
    /* Beat */ {0, 0, 0, 41},
    /* TrimOverflow */ {0, 0, 0, 20},
    /* DropSoft */ {11, 106, 166, 31},
    /* RangeFill */ {166, 90, 0, 36},
    /* MutedHatch */ {244, 245, 247, 153},
    /* MarkerAmber */ {166, 90, 0, 255},
    /* MarkerCoral */ {196, 59, 42, 255},
    /* MarkerGreen */ {47, 122, 77, 255},
    /* MarkerCyan */ {11, 106, 166, 255},
    /* MarkerViolet */ {122, 79, 192, 255},
};

// Shadows (CSS syntax; map to UltraCanvas drop shadows)
inline constexpr const char* ShadowMenuDark = "0 8px 24px rgba(0,0,0,0.5), 0 0 0 1px rgba(255,255,255,0.06)";
inline constexpr const char* ShadowMenuLight = "0 8px 24px rgba(20,24,30,0.16), 0 0 0 1px rgba(20,24,30,0.08)";
inline constexpr const char* ShadowDragDark = "0 6px 16px rgba(0,0,0,0.55)";
inline constexpr const char* ShadowDragLight = "0 6px 16px rgba(20,24,30,0.22)";

// Spacing
inline constexpr float Space1 = 4.0f;   // Icon-to-label gap, clip inner padding.
inline constexpr float Space2 = 8.0f;   // Control padding, gap between toolbar buttons.
inline constexpr float Space3 = 12.0f;   // Panel inner padding, menu item horizontal padding.
inline constexpr float Space4 = 16.0f;   // Panel section gaps.
inline constexpr float Space6 = 24.0f;   // Empty-state and dialog padding.
inline constexpr float Space8 = 32.0f;   // Large separations, cover insets.

// Radius
inline constexpr float RadiusXs = 2.0f;   // Markers, badges inside clips, ruler handles.
inline constexpr float RadiusSm = 4.0f;   // Clips, buttons, fields, transition badges.
inline constexpr float RadiusMd = 6.0f;   // Panels, menus, tooltips, thumbnails in the media bin.
inline constexpr float RadiusLg = 10.0f;   // Dialogs and the empty-state drop zone.
inline constexpr float RadiusPill = 999.0f;   // Zoom slider track, toggle chips.

// Geometry
inline constexpr float AppbarH = 40.0f;   // Top application bar.
inline constexpr float ToolbarH = 36.0f;   // Timeline toolbar and viewer transport bar.
inline constexpr float ControlH = 28.0f;   // Buttons, fields, icon buttons.
inline constexpr float RulerH = 28.0f;   // Time ruler height including the marker strip.
inline constexpr float TrackHeaderW = 200.0f;   // Width of the track header column.
inline constexpr float TrackHVideo = 64.0f;   // Default video track height (filmstrip visible).
inline constexpr float TrackHAudio = 48.0f;   // Default audio and music track height.
inline constexpr float TrackHOverlay = 28.0f;   // Overlay/title lane height.
inline constexpr float TrackHMin = 24.0f;   // Collapsed track height (name only, no thumbnails).
inline constexpr float ClipBandH = 16.0f;   // Clip top band holding name and badges.
inline constexpr float HandleW = 8.0f;   // Hit width of a clip trim handle (drawn 3px wide).
inline constexpr float PlayheadW = 2.0f;   // Playhead line width.
inline constexpr float IconSize = 16.0f;   // All UI icons.
inline constexpr float BinW = 280.0f;   // Default media-bin width (min 220px).
inline constexpr float InspectorW = 300.0f;   // Default inspector width (min 260px).
inline constexpr float SplitterW = 4.0f;   // Hit width of panel splitters; drawn as a 1px line.

inline constexpr float OpacityGhost = 0.7f;   // Dragged clip ghost.
inline constexpr float OpacityDisabled = 0.4f;   // Disabled controls and locked-track clips.
inline constexpr int DurationFastMs = 80;   // Hover and press feedback.
inline constexpr int DurationBaseMs = 160;   // Neighbour clips sliding aside during reorder, panel collapse.

// Typography. Families: "IBM Plex Sans" (UI), "IBM Plex Mono" (timecode, tabular figures).
struct TextStyle { const char* family; float size; float lineHeight; int weight; };
inline constexpr TextStyle Display {"IBM Plex Sans", 24.0f, 30.0f, 600};   // Empty-state headline, export dialog title.
inline constexpr TextStyle Title {"IBM Plex Sans", 14.0f, 20.0f, 600};   // Panel titles (Media, Inspector), dialog section heads.
inline constexpr TextStyle Body {"IBM Plex Sans", 13.0f, 18.0f, 400};   // Default UI text: inspector values, menu items, tooltips.
inline constexpr TextStyle Label {"IBM Plex Sans", 12.0f, 16.0f, 500};   // Buttons, track names, clip names, tab labels.
inline constexpr TextStyle Caption {"IBM Plex Sans", 11.0f, 14.0f, 400};   // Badges on clips, secondary metadata, keyboard hints in menus.
inline constexpr TextStyle TimecodeLg {"IBM Plex Mono", 18.0f, 24.0f, 500};   // Current playhead time in the viewer transport and timeline toolbar.
inline constexpr TextStyle Timecode {"IBM Plex Mono", 12.0f, 16.0f, 400};   // Timecode fields, hover readout, inspector in/out values.
inline constexpr TextStyle TimecodeSm {"IBM Plex Mono", 10.0f, 12.0f, 400};   // Ruler labels and duration badges where space is tight.

} // namespace UltraVideo::Tokens
