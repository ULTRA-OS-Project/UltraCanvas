# UltraVideo

A video editor for ULTRA OS and the other UltraCanvas desktops: clips,
photos and music on one timeline, the result shown while you edit, and one
button to export. It is built on the VideoFX engine (FFmpeg behind VideoFX's
own API) and the framework's `UltraCanvasVideoTimeline` and
`UltraCanvasTimecodeField` elements.

Source: [`Apps/UltraVideo/`](../../Apps/UltraVideo/). Changelog:
[`CHANGELOG.md`](CHANGELOG.md). The engine:
[`Docs/Modules/VideoFX/README.md`](../Modules/VideoFX/README.md). The
timeline element: [UltraCanvasVideoTimeline](../UltraCanvas/UltraCanvasVideoTimeline.md).
Design and the plan it follows: `Docs/Research/UltraVideoDesignProposal.md`;
the screens and the brand book under `Apps/UltraVideo/screens/`.

## The window

From top to bottom: the **app bar** with the project's name, its size, frame
rate and *Saved* or *Edited* state, undo and redo, open, save, import,
*Make a story*, the light-or-dark switch and **Export**; the **media bin**, the **viewer** and
the **inspector** side by side in panes you can drag; the **timeline** with
its toolbar under them. The design's dark palette is the default; the eye
button in the app bar switches to the light one.

### The media bin

Import videos, photos and songs with the import button, `Ctrl+I`, by
dropping them on the window, or by naming them on the command line (a
folder imports its files). Every file is probed by the engine and shows a
duration badge, *photo*, or *unreadable* with the reason in its tooltip.
Double-click a video or a photo, or press its *Add* button, to put it on
the timeline; a song becomes the music under the whole video. Or drag a
tile onto the timeline: on the main track it goes in at the cut nearest the
pointer, on the overlay lane it goes over the picture at that time, and a
song dropped anywhere becomes the music; the insertion marker shows where,
`Esc` lets go. A search
field filters the bin by name. The *Music* tab lists the songs. The
*Titles* tab holds the title styles - *Opening title*, *Lower third*,
*Caption*, *Closing card* - and a click puts one on the title lane at the
playhead. *Effects* waits for a later release.

### The viewer

The frame at the playhead, rendered by the engine as the export would make
it - effects, overlays and transitions included - on the app ground with
the title-safe area dashed. *Fit* shows the whole frame; *Fill*, *50 %*,
*100 %* and *200 %* are the other views. The transport: to start, a frame
back, play or pause (`Space`), a frame forward, to end, the timecode over
the project's length, and the volume. Play moves the playhead in real time
and shows the frames the engine renders for it; playback with sound comes
with the draft render in a later release.

### The timeline

The `UltraCanvasVideoTimeline` element with four lanes: **T1 Titles**, a
free lane of title spans over the picture; **V2 Overlay video**, a free
lane of files shown over the picture; **V1 Main track**, magnetic -
clips follow each other, gaps close; and **A1 Music**, which shows the
songs and is edited from the inspector. Drag a clip to reorder,
drag an edge to trim, drag a transition's edge for its length, drop files
at a time, right-click for the menu. The toolbar: Select `V`, Blade `B`,
Trim `T`, Slip `Y`; split at the playhead `S`, delete `Del`, a marker `M`;
snapping `N` and beat marks; the playhead's timecode field over the length
(type a time and `Enter`); zoom out, the zoom slider, zoom in, fit
`Shift+Z`. The element's own keys are listed in its document.

### Titles

A title is one span on the title lane: drag it to its time, drag its edges
for its length, split it with `S`, delete it with `Del`. Its text, style,
place on the frame (the nine anchors), size, colour, shadow, band and
fades are in the inspector. When the engine renders - in the viewer and in
the export - the span becomes a text overlay on every clip it crosses,
with the fades only at the span's own ends, so a title runs across a cut
without a seam. The project file keeps the spans and writes the overlays
beside them for `videofx render`.

### Overlay video

The **V2 Overlay video** lane puts another file's picture over the main
track: a video as picture-in-picture, without its sound, or a picture as a
logo. Right-click the lane for *Put a file over the picture here…* and the
bin's files, use *Over the picture* on a bin tile (at the playhead), or
drop files on the lane. An overlay clip is moved, trimmed, split and
deleted like any clip; trimming its left edge moves into the file, and a
video clip cannot be stretched past the file's end. The inspector sets its
length, place (nine positions), size as a share of the frame's height,
opacity and fades. The viewer shows it while editing, and like a title it
is cut into one overlay per main-track clip it crosses when the engine
renders, so it runs across a cut.

### Music

The music lane shows the songs under the video in play order, with the
waveform, the fade ramps and the crossfades between songs. Hover a song
and drag the knob at the top of a fade ramp for the first song's fade in
or the last song's fade out; drag the crossfade badge's edge for its
length; drag the first song's left edge to start the music further into
the song; drag songs to reorder them; `Del` removes one. Selecting a song
opens the *Audio* tab, where volume, the fades, the crossfade, repeat and
the ducking under speech are set. *Beat marks* in the timeline toolbar
asks the engine for the first song's beats and draws them across the
lanes, so cuts can land on them.

### The story wizard

The *Make a story* button in the app bar (`Ctrl+Shift+N`) builds a video
from photos and clips in three steps. **1 Pictures**: add files, drag them
into the order they play, pick how long each photo stays. **2 Music**: a
song or none; with a song, *Fit the video to the song's length* paces the
photos so the show ends with it and *Change pictures on the beat* cuts on
the engine's beats; a dropdown names the clips' own sound so the music
dips under it the right way. **3 Style**: the transition between pictures
and its length, *Keep faces in shot* while the camera pans over photos,
photos and clips of another shape whole on a blurred background (clips:
whole, with bars), filling the frame, or whole with bars, *Captions from
the file names*, a fade in and out. *Make video* asks the engine for the
photos' plan (`VideoFX_PlanSlideshow`: pan and zoom, faces, beats) and
opens the result as an ordinary project: the photos and clips in order on
the main track with the transition between them, the captions as title
spans, the song on the music lane, the fades on the first and last clip.
Everything the wizard chose can then be changed. Files already in the bin
are offered first; a song in the bin becomes the music.

### The inspector

- **Clip**: the selected clip's name and what it is; for a video its *In*
  and *Out* as timecode fields and its *Speed*; for a photo or a card its
  *Length*, and for a photo the *Motion* (auto, still, zoom, pan), the
  *Framing* and *Keep faces in shot*; *Mute*; the **transition into** the
  clip, by type and length with the maximum shown. For a title: its text
  (type it and press `Enter`), length, style, place, size, colour, shadow,
  band, fade in and fade out.
- **Audio**: the music - the songs, *Add song*, *Remove music*, *Volume*,
  *Fade in*, *Fade out*, *Crossfade* between songs, repeat, and the ducking
  under the clips' own sound by preset (speech, outdoor, loud event) and
  level.
- **Project**: the name, the output size (from the first clip, Full HD, HD,
  4K, portrait, square), the frame rate, and how clips of another shape are
  fitted.

Every field is an edit like any other: it goes through undo and shows on
the timeline and in the viewer at once.

## Undo, projects and export

`Ctrl+Z` and `Ctrl+Shift+Z` (or `Ctrl+Y`) undo and redo every edit; the
app bar's buttons name what they would undo. `Ctrl+S` saves the project as
a `.ultravideo` file, `Ctrl+Shift+S` under a new name, `Ctrl+O` opens one,
`Ctrl+N` starts afresh; closing with unsaved changes asks. The file is
JSON: the engine's own project (what `videofx render` takes) with the bin,
the markers and the view around it, media paths relative to the file.

**Export** (`Ctrl+E`) asks for a preset - web MP4 at 1080p, 720p or 4K,
WebM, an animated GIF, a ProRes master, or sound only as MP3 - the output
file, and whether to export the whole project or the work area set with
`I` and `O` on the timeline. The export runs in the background with a
progress dialog and can be cancelled; a cancelled or failed export leaves
no file.

## Command line

```
UltraVideo                        an empty project
UltraVideo trip.ultravideo        open a project
UltraVideo clip.mp4 photo.jpg     a new project with these on the timeline
UltraVideo ~/Pictures/Trip        every readable file of a folder
UltraVideo --version
```

## Building

`BUILD_ULTRAVIDEO` (default ON) builds it whenever the framework and the
VideoFX module build. Without FFmpeg the engine is a stub and the editor
starts but can read no media. The icon set the window uses is
`media/icons/ultravideo/`, the design's; the app icon is
`media/appicon/UltraVideo.{svg,png}`.

Tests: `UltraVideoDocumentTest` (the document, the mapping onto the
timeline model and back, the history, the file round trip; headless, needs
the engine).

## Not yet

Effect presets, playback with sound, and the mapping
of the design's tokens onto the framework's theme roles (the app colours
its elements from the tokens directly). The proposal's roadmap (§11) has
the order.
