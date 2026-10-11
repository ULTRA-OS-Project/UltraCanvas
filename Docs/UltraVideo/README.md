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
rate and *Saved* or *Edited* state, undo and redo, open, save, import, the
light-or-dark switch and **Export**; the **media bin**, the **viewer** and
the **inspector** side by side in panes you can drag; the **timeline** with
its toolbar under them. The design's dark palette is the default; the eye
button in the app bar switches to the light one.

### The media bin

Import videos, photos and songs with the import button, `Ctrl+I`, by
dropping them on the window, or by naming them on the command line (a
folder imports its files). Every file is probed by the engine and shows a
duration badge, *photo*, or *unreadable* with the reason in its tooltip.
Double-click a video or a photo, or press its *Add* button, to put it on
the timeline; a song becomes the music under the whole video. A search
field filters the bin by name. The *Music* tab lists the songs; *Titles*
and *Effects* wait for a later release.

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

The `UltraCanvasVideoTimeline` element with two lanes: **V1 Main track**,
magnetic - clips follow each other, gaps close - and **A1 Music**, which
shows the songs and is edited from the inspector. Drag a clip to reorder,
drag an edge to trim, drag a transition's edge for its length, drop files
at a time, right-click for the menu. The toolbar: Select `V`, Blade `B`,
Trim `T`, Slip `Y`; split at the playhead `S`, delete `Del`, a marker `M`;
snapping `N` and beat marks; the playhead's timecode field over the length
(type a time and `Enter`); zoom out, the zoom slider, zoom in, fit
`Shift+Z`. The element's own keys are listed in its document.

### The inspector

- **Clip**: the selected clip's name and what it is; for a video its *In*
  and *Out* as timecode fields and its *Speed*; for a photo or a card its
  *Length*, and for a photo the *Motion* (auto, still, zoom, pan), the
  *Framing* and *Keep faces in shot*; *Mute*; the **transition into** the
  clip, by type and length with the maximum shown.
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

The title lane and overlay-video lane, the story wizard, effect presets,
drag from the bin onto the timeline, playback with sound, and the mapping
of the design's tokens onto the framework's theme roles (the app colours
its elements from the tokens directly). The proposal's roadmap (§11) has
the order.
