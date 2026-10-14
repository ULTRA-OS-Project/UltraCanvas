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
folder imports its files). The dialogs offer FileLoader's types that the
engine reads: at start-up UltraVideo tells the framework's codec registry
which films and sound VideoFX's FFmpeg reads and writes, and gives the engine
the framework's image reader for the pictures FFmpeg does not open whole - a
phone's HEIC (a grid of tiles libheif puts together), a camera's raw file.
Every file is probed by the engine and shows a duration badge, *photo*, or
*unreadable* with the reason in its tooltip.
Double-click a video or a photo, or press its *Add* button, to put it on
the timeline; a song becomes the music under the whole video. Or drag a
tile onto the timeline: on the main track it goes in at the cut nearest the
pointer, on the overlay lane it goes over the picture at that time, on
*Voice & sound* it is a voice-over at that time, and a song dropped
anywhere else becomes the music; the insertion marker shows where,
`Esc` lets go. A search
field filters the bin by name. The *Music* tab lists the songs, each with
*Voice-over* and *Sound effect* buttons that put it on the *Voice & sound*
lane at the playhead. The
*Titles* tab holds the title styles - *Opening title*, *Lower third*,
*Caption*, *Closing card* - and a click puts one on the title lane at the
playhead. The *Effects* tab holds one-click presets for the selected
clip on the main track: the colour looks *Warm*, *Cool*, *Vivid*, *Faded*,
*Dramatic*, *Black and white*, *Sepia*, *Vintage* and *Brighter*, and under
*Blur and detail* *Soft blur*, *Strong blur*, *Sharpen* and *Reduce noise*.
A preset is the engine's own effects (a look is a few of them: Warm is
warmer colour and a little more saturation); they go after the clip's
other effects, and a second click on the same preset does nothing. A
blur's radius follows the clip's picture height, so it looks alike on a
phone clip and a 4K one. The clip shows the effects badge on the
timeline, and the viewer shows the look at once.

### The viewer

The frame at the playhead, rendered by the engine as the export would make
it - effects, overlays and transitions included - on the app ground with
the title-safe area dashed. *Fit* shows the whole frame; *Fill*, *50 %*,
*100 %* and *200 %* are the other views. The transport: to start, a frame
back, play or pause (`Space`), a frame forward, to end, the timecode over
the project's length, and the volume. Play moves the playhead in real time
and shows the frames the engine renders for it; playback with sound comes
with the draft render in a later release.

**Playing with sound.** Play (`Space`) runs the timeline from the playhead.
The first Play after an edit also renders a *draft* of the whole timeline
in the background - a small, quick MP4 with the music and the clips' own
sound (`VideoFXExportSettings::Draft`) - and plays frame by frame until it
is ready; from then on Play runs the draft in the framework's player with
sound, and the playhead follows it. An edit throws the draft away and the
next Play renders a new one. Where no sound device or player is available
playback stays frame by frame and the status line says so.

**Proxies.** Large clips, such as 4K phone videos, are slow to scrub. So
every video in the bin gets a *proxy* in the background: the same clip at
540 lines, quick to seek in (VideoFX's proxy media). The bin tile shows its
progress, *proxy waiting*, *proxy 40 %*, and *proxy* when it is ready. From
then on the viewer and the playback draft play that clip from its proxy.
The **export always reads the originals**, at full quality.
- Proxies are made one at a time on a thread of their own, so scrubbing
  never waits behind one. They are encoded on the graphics hardware where
  one works (NVENC, Quick Sync, VA-API, AMF, VideoToolbox, Media
  Foundation), and in software where none does.
- A clip no taller than a proxy, which is already quick, gets none.
- Proxies are kept in the user's cache folder (`~/.cache/VideoFX/Proxies`
  and the like), so a project opened again plays from them at once.
- A clip removed from the bin before its proxy is made is dropped from the
  queue.
- *Preview from proxies*, in the inspector's Project tab, turns them off
  and on for the project. It is saved with the project, and is not an
  undo step.

### The timeline

The `UltraCanvasVideoTimeline` element with five lanes: **T1 Titles**, a
free lane of title spans over the picture; **V2 Overlay video**, a free
lane of files shown over the picture; **V1 Main track**, magnetic -
clips follow each other, gaps close; **A2 Voice & sound**, a free lane of
voice-overs and sound effects; and **A1 Music**, which shows the songs and
is edited from the inspector. Drag a clip to reorder,
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

### Voice & sound

The **A2 Voice & sound** lane puts a file's sound at any time, over the
clips' own sound and the music: a narration, a voice-over, a door slam, a
cheer. A clip there is a **voice-over**, which the music dips under as it
does under speech in the clips, or a **sound effect**, which plays over
the music without changing it. Right-click the lane for *Add a voice-over
here…*, *Add a sound effect here…* and the bin's files with sound (songs,
and videos for their sound only); use the buttons on a song in the
*Music* tab (at the playhead); or drop files on the lane - a dropped file
is a voice-over. A sound clip is moved, trimmed, split and deleted like
any clip, shows its waveform, and its fade handles set its fades; trimming
its left edge moves into the file. The inspector sets its *Volume* (up to
200 %), *Fade in*, *Fade out* and *The music dips under it*, which turns a
voice-over into a sound effect and back. The playback draft and the export
mix the lane (VideoFX's audio clips); the viewer's frame-by-frame scrubbing
is silent, as for every lane.

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
  *Framing* and *Keep faces in shot*; *Mute*; the clip's **Effects**, one
  row per effect by what it does (*Warmer 30 %*, *Blur 3.5 px*) with a
  strength slider (none for a switch such as black and white or a flip)
  and a button that removes it, and *Remove all effects*; the **transition into** the
  clip, by type and length with the maximum shown. For a title: its text
  (type it and press `Enter`), length, style, place, size, colour, shadow,
  band, fade in and fade out. For a voice-over or sound effect: volume,
  fade in, fade out and whether the music dips under it.
- **Audio**: the music - the songs, *Add song*, *Remove music*, *Volume*,
  *Fade in*, *Fade out*, *Crossfade* between songs, repeat, and the ducking
  under the clips' own sound by preset (speech, outdoor, loud event) and
  level.
- **Project**: the name, the output size (from the first clip, Full HD, HD,
  4K, portrait, square), the frame rate, how clips of another shape are
  fitted, and *Preview from proxies* (on by default; see *Proxies* above).

Every field is an edit like any other: it goes through undo and shows on
the timeline and in the viewer at once. *Preview from proxies* is the one
exception: it changes how the project is previewed, not the project, so it
is no undo step.

## Undo, projects and export

`Ctrl+Z` and `Ctrl+Shift+Z` (or `Ctrl+Y`) undo and redo every edit; the
app bar's buttons name what they would undo. `Ctrl+S` saves the project as
a `.ultravideo` file, `Ctrl+Shift+S` under a new name, `Ctrl+O` opens one,
`Ctrl+N` starts afresh; closing with unsaved changes asks. The file is
JSON: the engine's own project (what `videofx render` takes) with the bin,
the markers and the view around it, media paths relative to the file. The
lanes' spans (titles, overlays, sounds) are kept beside it, and the engine's
project carries what they become - overlays and audio clips - so `videofx
render` gives the same video. A VideoFX project's audio clips, opened in
UltraVideo, appear on the *Voice & sound* lane.

**Export** (`Ctrl+E`) asks for a preset - web MP4 at 1080p, 720p or 4K,
WebM, an animated GIF, a ProRes master, or sound only as MP3 - the output
file (*Browse* offers the presets' types; picking one there picks its
preset), and whether to export the whole project or the work area set with
`I` and `O` on the timeline. The export runs in the background with a
progress dialog and can be cancelled; a cancelled or failed export leaves
no file. Exporting the work area keeps the part of each sound clip inside
it.

## Command line

```
UltraVideo                        an empty project
UltraVideo trip.ultravideo        open a project
UltraVideo clip.mp4 photo.jpg     a new project with these on the timeline
UltraVideo ~/Pictures/Trip        every readable file of a folder
UltraVideo --version
```

One UltraVideo runs per user. Starting it again while it runs hands the
files to the running copy and quits: a project opens there (asking first
whether to save the current one when it has changes, as *Open project*
does), media files are imported into the open project as a drop on the
window would, and the window comes forward. Started without a file, it
just brings the running window forward.

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

Effect presets on overlay clips and stills of the overlay lane, effects
dragged from the bin onto a clip, and the mapping of the design's tokens onto the framework's theme roles (the app colours
its elements from the tokens directly). The proposal's roadmap (§11) has
the order.
