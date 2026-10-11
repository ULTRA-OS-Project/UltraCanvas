# UltraVideo — a video editor for ULTRA OS on VideoFX, and the timeline element it needs (Investigation and Proposal)

**Date:** 2026-10-11
**Status:** Proposal. The one engine change the timeline needed before anything
else, waveforms, is built (VideoFX 0.8.0, `VideoFX_ExtractWaveform`). The
element, the application and the other engine changes in §8 are not written.
**Scope:** a new application, `Apps/UltraVideo`, that makes impressive videos
out of clips, photos and music with little effort; a new public framework
element, `UltraCanvasVideoTimeline`, that any application can put a media
timeline into; and the changes the VideoFX engine needs so the two can show
the user what they are making. The design was done first: the screens and
component specifications in [`Apps/UltraVideo/screens/`](../../Apps/UltraVideo/screens/)
are the reference, and this document says how they map onto the framework,
the engine and the application.

This document follows the other investigations in this directory.
[`UltraClipboardDesignProposal.md`](UltraClipboardDesignProposal.md) and
[`UltraCalendarDesignProposal.md`](UltraCalendarDesignProposal.md) are the
closest in shape: each asks which parts belong in the framework and which in
the app, and each puts the reusable views into the framework before the
application is written. Everything said about the repository was checked
against the tree on this date. What is said about other editors comes from
their published documentation as the author knows it and is marked
**[unverified]** where it could not be checked from this environment.

The screens are SVG files exported from the design tool, with the text as
outlines, so their labels cannot be searched: §6 therefore describes each
screen in prose. The specifications are short Markdown files from the same
export, one per component, and the brand book holds the tokens, the type,
the spacing and the interaction rules they share.

| File | Shows |
|---|---|
| [`EditorLayout-light.svg`](../../Apps/UltraVideo/screens/EditorLayout-light.svg), [`EditorLayout.md`](../../Apps/UltraVideo/screens/EditorLayout.md) | The whole window at the 1440 × 860 reference: app bar, media bin, viewer, inspector, timeline dock (§6.1) |
| [`Timeline-light.svg`](../../Apps/UltraVideo/screens/Timeline-light.svg), [`Timeline.md`](../../Apps/UltraVideo/screens/Timeline.md) | The timeline on its own: ruler, title lane, overlay video track, main track, markers, playhead (§6.6, §7) |
| [`TimelineToolbar-light.svg`](../../Apps/UltraVideo/screens/TimelineToolbar-light.svg), [`TimelineToolbar.md`](../../Apps/UltraVideo/screens/TimelineToolbar.md) | Tools, edit actions, mode toggles, timecode, zoom (§6.5) |
| [`TimeRuler-light.svg`](../../Apps/UltraVideo/screens/TimeRuler-light.svg), [`TimeRuler.md`](../../Apps/UltraVideo/screens/TimeRuler.md) | The ruler at three zoom levels, with markers, work area, beats and the hover readout (§7.4) |
| [`TrackHeader-light.svg`](../../Apps/UltraVideo/screens/TrackHeader-light.svg) | Track headers: kind strip, name, magnet, eye, speaker, lock, and their states (§6.6) |
| [`Clip-light.svg`](../../Apps/UltraVideo/screens/Clip-light.svg), [`Clip.md`](../../Apps/UltraVideo/screens/Clip.md) | One clip in every state: rest, hover, selected, speed and effects, muted, locked, still image, colour card, trimming, collapsed (§6.7) |
| [`Transition-light.svg`](../../Apps/UltraVideo/screens/Transition-light.svg), [`Transition.md`](../../Apps/UltraVideo/screens/Transition.md) | The transition badge: rest, selected, dragged to the engine's limit, dropped onto a cut (§6.7) |
| [`MusicTrack-light.svg`](../../Apps/UltraVideo/screens/MusicTrack-light.svg), [`MusicTrack.md`](../../Apps/UltraVideo/screens/MusicTrack.md) | The music lane with waveform, fades, song boundaries and a fade readout (§6.7) |
| [`EditStates-light.svg`](../../Apps/UltraVideo/screens/EditStates-light.svg), [`EditStates.md`](../../Apps/UltraVideo/screens/EditStates.md) | Reorder, trim with snapping, file drop, rubber band, blade (§6.8) |
| [`TimecodeField-light.svg`](../../Apps/UltraVideo/screens/TimecodeField-light.svg), [`TimecodeField.md`](../../Apps/UltraVideo/screens/TimecodeField.md) | The timecode field and the large transport readout (§6.9, §7.6) |
| [`ContextMenu-light.svg`](../../Apps/UltraVideo/screens/ContextMenu-light.svg), [`ContextMenu.md`](../../Apps/UltraVideo/screens/ContextMenu.md) | Right-click menus for a clip and for a transition (§6.9) |
| [`Tooltip-light.svg`](../../Apps/UltraVideo/screens/Tooltip-light.svg), [`Tooltip.md`](../../Apps/UltraVideo/screens/Tooltip.md) | Help tooltips and the live readout that follows a drag (§6.9) |
| [`Button-light.svg`](../../Apps/UltraVideo/screens/Button-light.svg), [`Button.md`](../../Apps/UltraVideo/screens/Button.md) | Primary, standard, ghost, icon and toggle buttons and their states (§6.9) |
| [`EmptyState-light.svg`](../../Apps/UltraVideo/screens/EmptyState-light.svg), [`EmptyState.md`](../../Apps/UltraVideo/screens/EmptyState.md) | The timeline with no clips, idle and during a file drag (§6.9) |
| [`BrandBook.md`](../../Apps/UltraVideo/screens/BrandBook.md) | Principles, colour tokens, type, spacing, layout, the interaction rules, icons, motion, and the implementation notes for UltraCanvas |
| [`UltraVideoTokens.h`](../../Apps/UltraVideo/UltraVideoTokens.h) | The tokens as C++: both palettes (dark and light, 41 roles each), shadows, spacing, radii, geometry, opacities, durations and the eight text styles, generated from the design's `tokens.json` (§5.8) |
| [`Icons/`](../../Apps/UltraVideo/Icons/) | The 46 interface icons the screens use, one path each on a 16 px grid (§5.8) |
| [`screens/components/`](../../Apps/UltraVideo/screens/components/) | `index.d.ts`, the design's neutral timeline model as TypeScript types; `bundle.js`, the reference renderer that draws that model as HTML and SVG (a visual specification, not production code); `bundle.css`, its stylesheet (§7.1) |
| [`screens/components/pages/`](../../Apps/UltraVideo/screens/components/pages/) | The live pages, one per component plus `Cover.html` and an `index.html` that frames them all with a theme toggle. They draw through the renderer and expect `screens/tokens/tokens.css` (the tokens as CSS custom properties) and `screens/fonts/fonts.css`, which are not in the tree yet, so they render without colours until those two files arrive |

---

## 1. Recommendation

**Build the timeline as a framework element first, drive it from the engine's
own types through a thin neutral model, and give the editor a "story" mode
that makes a finished video from a folder of photos and a song before the
user ever opens the timeline.** VideoFX already renders everything the
screens show except an overlay video track; what it cannot do yet is show the
user the result while they edit. So the order is: the element with its demo,
the engine's render-a-frame call so the viewer can scrub the real output,
then the application around both, then the overlay track when the engine
grows picture-in-picture.

Why each choice was made, in one line each; the rest of the document is the
evidence:

| Decision | Reason |
|---|---|
| The timeline is a public element, `UltraCanvasVideoTimeline`, in `UltraCanvas/{include,core}` (§5.1, §7) | The framework rule: anything that takes input or shows a picture is an element, in the catalogue, so the next caller finds it. VideoFX's own roadmap names "a timeline editor element in UltraCanvas on top of the engine" as its stage 5. UltraViewer (trim), UltraSocial (cut a clip before posting) and the UltraAI dashboard (sequence generated clips) are callers waiting. |
| The element has its own model and never includes a VideoFX header (§5.2, §9.1) | The UltraCanvas core does not link `libVideoFX`, which is a shared library of its own so that applications without video never load FFmpeg (`Masterfile_modules.md` §16). The waveform element's module-free `WaveformChannelData` is the precedent, and `VideoFXWaveform` was shaped to fit it. |
| A hybrid timeline: one magnetic main track that reorders, free tracks above and below it (§5.3) | The engine's timeline *is* a sequence: `VideoFX_Export` plays segments one after another with no free positions. The main track shows that truth. Overlay lanes and the music lane are free because the engine's overlays and music are placed by time. iMovie's magnetic timeline **[unverified]** shows this is the model beginners get right first time. |
| Every edit is reported with before and after states and applied only after the app validates it (§5.4, §7.3) | Engine limits (speed 0.25×–4×, transition length 0.04–5 s and shorter than both neighbours, the title font available or not) live in the engine. The element shows them as readouts ("max 2.0 s"), never discovers them silently, and the app's undo stack records what it validated. |
| The viewer scrubs the *rendered* frame through a new `VideoFX_RenderFrame` (§5.5, §8.2) | Today the engine can pull a frame from a *source* file, not the output frame at a timeline time with effects, overlays and transitions applied. A draft export played in the player is the fallback, not the design: it is seconds stale after every edit. |
| The application saves a `.ultravideo` JSON file that embeds the VideoFX project (§5.6, §9.2) | `.vfxproj` holds exactly what the engine renders and nothing else; `VideoFX_SaveProject` writes every field, so app keys would be dropped on a round trip. The app's file holds the lanes, markers, media bin, zoom and the embedded `.vfxproj` text from `VideoFX_ProjectToJson`, so `videofx render` can still render what the app saved. |
| A "story" mode in front of the timeline (§5.9, §6.11) | The brief is *impressive videos with little effort*. The engine's automatic features — photos with pan and zoom that keep faces in shot, slideshows cut on the beat, music that dips under speech — produce that from a folder and a song in one call (`VideoFX_CreateSlideshow`). The timeline is for the user who then wants to change something. |
| The design's tokens map onto the framework theme; nothing is hard-coded in the element (§5.8) | The brand book says so itself. The framework themes every element; a second colour system in one app would break the dark theme and the DPI scaling the rest of the suite gets for free. |
| The overlay video track is in the model from day one and disabled in the app until the engine renders it (§5.3, §8.3) | Picture-in-picture is planned, not built, in VideoFX. Leaving the lane out of the element's model would cost an API change later; leaving it visible would promise what the export cannot do. |

---

## 2. What was asked, restated

A new application, UltraVideo, based on VideoFX. A user-friendly interface to
create impressive videos. The two questions raised with it:

1. *Should the UI be designed in Claude Design?* It was, and the result is
   the reference in `Apps/UltraVideo/screens/`. The design tool exported the
   screens as SVG with outlined text and the specifications as Markdown;
   this document is the searchable record of both, and it maps every region
   to the UltraCanvas element that builds it, because the framework rule is
   that nothing is painted from scratch.
2. *Does it make sense to create a public UltraCanvas element to edit a video
   timeline before starting the main app?* Yes, and §1 says why. §7 is that
   element's specification.

The design settles a third question this investigation would otherwise have
had to ask: what the editor looks like and how it behaves. It targets a
prosumer on a desktop with a mouse and keyboard, keeps the chrome neutral so
the footage carries the colour, and uses one accent for "what the user acts
on" and one colour for the playhead. The brand book's *Principles* section
holds the five rules; the one that shapes the architecture is *report, never
apply* (§5.4).

---

## 3. What the tree has today

### 3.1 The engine: VideoFX

`VideoFX/` is a headless editing and conversion engine on FFmpeg, built as a
shared library of its own, with a stub that returns `NotAvailable` when
FFmpeg is missing (`Docs/Modules/VideoFX/README.md`). As of 0.8.0 it has:

| Area | What exists | Where in the design it is used |
|---|---|---|
| Inspection | `VideoFX_Probe` (streams, duration, rotation), `VideoFX_ExtractFrame`, `VideoFX_ExtractThumbnails` (a filmstrip), `VideoFX_ExtractWaveform` (the loudness envelope, 0.8.0) | Media bin badges and thumbnails, clip filmstrips, the music waveform, clip waveforms |
| Timeline | `VideoFXSegment`: a file range, a still image, a colour card or a test pattern, with `start`/`end`, `speed` 0.25–4, `mute`, an ordered effect list, overlays, `transitionIn`, image motion, image fit, regions to keep in view | The main track's clips and everything on them |
| Effects | 26 typed effects: colour, geometry, blur, LUT, fades, volume, loudness | The inspector's *Effects* list, the "Warm film" preset |
| Transitions | 30 types, 0.04–5 s, picture via `xfade`, sound cross-faded | The transition badge |
| Overlays | Text and image overlays per segment, anchored on the output frame, with start, end, fades, opacity, box, shadow, font | The title lane |
| Photos | Ken Burns motion with sub-pixel resampling, EXIF orientation, blurred-background fit, faces kept in shot by the built-in detector | Still-image clips, the story mode |
| Music | One bed track with a playlist, crossfades, loop, fades, ducking with three presets; beat detection; slideshows cut on the beat | The music lane, beat marks, the story mode |
| Export | Presets (web MP4, WebM, GIF, ProRes, audio-only), codec availability queries, `VideoFXExportJob` with progress and cancel, lossless cut | The export dialog and its progress |
| Projects | `.vfxproj` JSON with relative media paths and a missing-media list, atomic save, `VideoFX_ProjectToJson` / `FromJson` | Embedded in the app's project file |
| Tooling | `videofx` command-line tool, `VideoFXTest` on generated clips | Scripting and tests |

What it does **not** have, and the design needs (§8): a way to render the
*output* frame at a timeline time; picture-in-picture; several free audio
tracks; keyframed parameters; a draft or proxy render for playback.

The engine's timeline is strictly sequential: a segment has no position, only
a place in the list, and the export's length is the sum of the segments less
the transition overlaps. Overlays belong to a segment and are timed in that
segment's seconds. Music belongs to the export settings and runs under the
whole timeline. These three facts decide the mapping in §5.3.

### 3.2 The UI side: what the framework already draws

| Need | Element | Notes |
|---|---|---|
| Play the result, seek, show a frame | `UltraCanvasVideoPlayerElement` on `UltraCanvasVideoPlayer` | Platform media frameworks (GStreamer, Media Foundation, AVFoundation). `Seek`, `onSeek`, and the player's `onPositionChanged` drive the playhead both ways. |
| A poster frame of a file | `CaptureVideoThumbnailPixmap` (`UltraCanvasVideoThumbnail.h`) | For the media bin; the filmstrip inside a clip comes from VideoFX instead, because it needs many frames at once |
| A waveform | `UltraCanvasWaveformElement` | Takes `WaveformChannelData`; `SetFromAudioFX(x)` copies any struct with the same member names, which `VideoFXWaveform` has |
| A gallery of media | `UltraCanvasAlbum`, `UltraCanvasFilerWidget` | The media bin is an album with a search field above it |
| Panes, toolbars, tabs | `UltraCanvasSplitPane`, `UltraCanvasToolbar`, `UltraCanvasTabbedContainer`, `UltraCanvasSegmentedControl` | The editor layout |
| Fields, sliders, pickers | `UltraCanvasTextInput`, `UltraCanvasSlider`, `UltraCanvasDropdown`, `UltraCanvasSpinner`, `UltraCanvasCheckbox`, `UltraCanvasSwitch`, `UltraCanvasColorPicker` | The inspector and dialogs |
| Menus, dialogs, tooltips, progress | `UltraCanvasMenu`, `UltraCanvasModalDialog`, `UltraCanvasTooltipManager`, `UltraCanvasGaugeDiagramElement` in `LinearBar` mode | Context menus, export dialog, help and readouts, export progress |
| Native file dialogs | `UltraCanvasNativeDialogs` (`OpenFile`, `OpenMultipleFiles`, `SaveFile`) | Import, open, save, export |
| File drop onto an element | `UCEvent` of type `Drop` carrying `droppedFiles` | Drop from the file manager onto the timeline and the bin |
| JSON | `UltraCanvasJSON` (wraps yyjson) | The app's project file |

What the framework does **not** have: a timeline of any kind that edits
media (`UltraCanvasTimelineChart` places dated milestones to scale and
`UltraCanvasTimelineDiagram` tells a story along a path; neither has clips,
trimming or a playhead), a timecode field, and an undo stack an application
can reuse (the spreadsheet and the text area each carry their own).

### 3.3 How an application is put together here

An app is a directory under `Apps/`, a `BUILD_<APP>` option and an
`add_executable` block in the root `CMakeLists.txt`, a changelog under
`Docs/<App>/CHANGELOG.md` declared with one `_ultracanvas_declare_product`
line in `cmake/UltraCanvasVersion.cmake`, a `<App>.desktop` file, an icon in
`media/appicon/`, a `build_app_bundle` call in `package-macos.sh`, and a
window title that shows the version from the compile definition. UltraViewer
is the smallest complete example and the template for §10.

### 3.4 Defects and gaps found while reading

- Nothing in the tree produced the data `UltraCanvasWaveformElement` draws
  from a media file. Fixed on this branch: `VideoFX_ExtractWaveform`.
- The DemoApp's module list still described VideoFX as stage 1 with no
  transitions. Fixed on this branch.
- `UltraCanvasVideoPlayerElement` has `onSeek` but no position callback of
  its own; the app reaches through `GetPlayer()->onPositionChanged`. Fine
  for the app; worth a forwarding callback on the element later (§13).

---

## 4. Survey — what the editors people know do

Brief, because the design is already made; this records why its choices are
the common ones. All of this is from the products' published material as the
author knows it, **[unverified]** from this environment.

| Editor | Timeline model | Taken from it |
|---|---|---|
| Apple iMovie | One magnetic primary storyline; clips attach to it; no gaps possible | The main track: reorder, never place; ripple on delete and trim |
| Apple Final Cut Pro | Magnetic timeline with connected clips and roles | Free lanes above the main track for titles and overlay video |
| DaVinci Resolve, Adobe Premiere | Free tracks everywhere, a ripple tool when wanted | `Alt` inverts ripple; the free tracks V2 and up |
| Kdenlive, Shotcut (open source) | Free tracks, a timeline toolbar with tool modes, a clip monitor and a project monitor | The toolbar's tool modes (select, blade, trim, slip); one viewer, not two, because the engine composes one output |
| CapCut, Canva (consumer) | Templates and automatic edits first; the timeline second | The story mode: pick photos and a song, get a video, then edit |

Two things every one of them does and the design keeps: timecode in
`HH:MM:SS:FF` with tabular figures so it never jitters, and a live readout
that follows every drag.

---

## 5. Design decisions

### 5.1 Three parts: framework, engine, application

| Part | Holds | Why there |
|---|---|---|
| Framework (`UltraCanvas/{include,core}`) | `UltraCanvasVideoTimeline` (§7), `UltraCanvasTimecodeField` (§7.6), a catalogue row each, a doc each, a DemoApp page | Reusable, themed, DPI-scaled, tested headless; the rule of the repository |
| Engine (`VideoFX/`) | `VideoFX_RenderFrame` (§8.2), later picture-in-picture, free audio tracks, a draft render | Everything that decodes, composes or encodes stays behind the engine's types |
| Application (`Apps/UltraVideo`) | The window and its panels, the project file, undo, the mapping between the element's model and the engine's segments, the story mode, export | The glue, and the only part that knows both the element and the engine |

The application is the only translator. The element never sees a
`VideoFXSegment`; the engine never sees a lane.

### 5.2 A neutral model in the element

The element edits *clips on tracks* (§9.1): a clip has an id, a kind (video,
image, card, title, audio), a source path, a position and a duration on the
timeline, the source range it uses, a speed, flags, and a list of badges to
draw. A transition sits on a cut with a type name and a length. A marker has
a time, a colour and a label. The model carries nothing the element does not
draw. The app keeps the engine's richer values (the effect list, the overlay
anchors, the image motion) in its own document and only hands the element
what it shows.

This is the waveform element's pattern: `WaveformChannelData` is a plain
struct, and the module that produces the data shapes its own struct to
match. `VideoFXWaveform` was written that way on purpose.

### 5.3 The hybrid timeline, and how lanes map to the engine

| Lane | In the element | In the engine today | Mapping |
|---|---|---|---|
| **V1 main track** | Magnetic: clips reorder, gaps close, `Del` ripples | `std::vector<VideoFXSegment>` in order | One clip = one segment. Position on the timeline is *derived*: the sum of the earlier clips' durations less the transition overlaps. The element is told the derived positions and draws them; it never lets a main-track clip be dropped at a free time. |
| **Transitions on V1** | A badge on a cut with a length | `segment.transitionIn` on the later segment | Direct. The maximum the element shows is the engine's: the shorter neighbour's length, capped at 5 s. |
| **T1 titles** | Free lane of spans with a text or logo | `segment.overlays`, timed in segment seconds | A span is cut at every segment boundary it crosses into one overlay per segment, with the same text and anchor and the fades only on the outer ends. The user sees one span; the engine gets several overlays. Invisible, and lossless on the way back because the app's document keeps the span. |
| **V2 overlay video** | Free lane of video clips | Not rendered yet (picture-in-picture is planned) | Present in the model; the app hides the lane until the engine renders it (§8.3). |
| **A1 music** | One bar: song or playlist, fades, crossfades, ducking | `settings.music` | Direct. Volume, fades, ducking preset and the playlist are the bar's inspector. |
| **A2… free audio** | Free lane of audio clips | Not rendered yet | In the model; hidden in the app until §8.4. |
| **Markers, beats, work area** | On the ruler | Beats from `VideoFX_DetectBeats`; markers and the range are the app's | The work area becomes the export's trim: the app exports only the segments inside it, trimming the first and last. |

Speed changes the clip's timeline duration (source range ÷ speed), and the
element shows the speed badge. Mute maps to `segment.mute`. A still image's
duration is `segment.duration`; its motion and fit are inspector values. A
colour card is `VideoFXSegment::SolidColor`. Captions in a slideshow are
title spans like any other.

### 5.4 Report, never apply

The element emits a change with the clip's state before and after and waits.
The app validates against the engine's limits and its own rules, applies the
change to its document, pushes an undo entry, and re-feeds the element's
model. The element shows readouts for limits it is told about ("1.5× · max
4×", "2.0 s · max 2.0 s") and never finds a limit by having an edit refused.
A refused edit snaps back in `duration-fast` with no dialog.

This is why undo lives in the application: every change the element reports
is already in the form an undo entry needs.

### 5.5 Showing the result while editing

Three levels, in the order they are built:

1. **Scrub a source frame.** Day one: the viewer shows
   `VideoFX_ExtractFrame` of the clip under the playhead at its source time,
   with no effects. Correct for cuts, trims and order; wrong for colour,
   titles and transitions.
2. **Scrub the output frame.** `VideoFX_RenderFrame` (§8.2) renders the
   output frame at a timeline time with the segment's effects, its overlays
   and, inside an overlap, the transition. The viewer calls it on every
   playhead move, debounced to the frame rate, from a worker thread, and
   drops a result that arrives after a newer request. This is the design's
   intent and the first engine change after this document.
3. **Play the result.** Playback runs the player element on a *draft
   render*: a low-resolution export (`VideoFXExportJob`, 360p, fast preset)
   of the work area, started after an edit settles for half a second and
   cancelled by the next edit. Until it is ready, Play scrubs level 2 at a
   reduced rate. A proxy render in the engine (its roadmap's stage 4) would
   make this cheap; the app does not wait for it.

### 5.6 The project file

`Summer trip.ultravideo` is a JSON file written through `UltraCanvasJSON`:

- `format`, `version`: the app's own, checked on load like `.vfxproj`'s.
- `project`: the text `VideoFX_ProjectToJson` produced, as a nested object,
  so `videofx project` and `videofx render` work on it after extraction and
  the engine's own missing-media logic applies.
- `lanes`: the title spans, the overlay clips, the free audio clips, in the
  element's model shape, with ids.
- `media`: the bin: every imported file, its probe summary, its thumbnail
  cache key, its display name.
- `view`: zoom, scroll, playhead, work area, markers, track heights, panel
  sizes.

Media paths follow the engine's rule: relative inside the project's folder,
absolute elsewhere, UTF-8. Saving is atomic as `VideoFX_SaveProject`'s is.
Thumbnails and waveforms are not saved; they live in a cache folder under
the user's cache directory keyed by path, size and modification time.

### 5.7 Undo

A command stack in the app, `UltraVideoHistory`: each entry holds the
before and after state of what the element reported (§5.4), or the before
and after of an inspector field. Grouped while a drag is in progress, so
one trim is one entry. The app bar's undo and redo buttons and `Ctrl+Z` /
`Ctrl+Shift+Z` drive it; the project's *Saved* state in the app bar follows
the stack's clean mark.

### 5.8 Theme, type and DPI

The brand book defines tokens (`bg-000`…`bg-500`, `ink`, `accent`,
`playhead`, `focus`, `danger`, the clip kind fills and edges, the guide
tints) and says to map every one onto the UltraCanvas theme and hard-code
nothing in the element. So:

- The tokens exist as code: `Apps/UltraVideo/UltraVideoTokens.h`, generated
  from the design's `tokens.json`, holds a `Palette` struct with 41 colour
  roles filled twice (`Dark`, `Light`), the shadows, the spacing and radius
  scale, the geometry (`AppbarH` 40, `ToolbarH` 36, `ControlH` 28, `RulerH`
  28, `TrackHeaderW` 200, the track heights, `ClipBandH` 16, `HandleW` 8,
  `PlayheadW` 2, `BinW` 280, `InspectorW` 300), the opacities, the two
  durations and the eight text styles. It is the application's source of
  truth for the design and is regenerated, never edited.
- The element reads its colours from the framework theme through named
  roles, and the application fills those roles from the `Palette` for the
  active theme. The clip kind fills and edges, the waveform, the transition
  badge, the guides and the playhead are the roles the element adds; the
  surfaces, ink, accent, focus and danger map onto roles the theme already
  has. Light and dark follow the window's theme; the design's dark palette
  is the editor's default.
- Type: the design names IBM Plex Sans and IBM Plex Mono. The framework
  ships Ubuntu and Ubuntu Mono in `media/fonts/` and every app shares them.
  Ubuntu Mono has tabular figures, so timecode does not jitter. Decision:
  use the framework's fonts and add no font files; the style names
  (`display`, `title`, `body`, `label`, `caption`, `timecode`,
  `timecode-lg`, `timecode-sm`) become sizes and weights in the element's
  style struct.
- Sizes come from the design's `size` family (control 28, ruler 28, track
  heights 64 / 48 / 28 / 24, header column 200, toolbar 36) multiplied by
  the framework's DPI scale, as every element does.
- Icons: the set is in `media/icons/ultravideo/`, 46 icons on a 16 px grid
  with a 1.5 px stroke, one path each in `currentColor`, named as the specs
  name them (`magnet`, `snap`, `beat`, `blade`, `slip`, `transition`,
  `speed`, `motion`, `wave`, the transport and the eye, speaker and lock
  pairs). The element takes icons as path data through its style, so the
  app hands these in and a later swap for an open set, which the brand book
  leaves open, touches no code.

### 5.9 The story mode

The first screen after *New* offers two ways in: **Story** and **Timeline**.
Story is a three-step wizard (§6.11): choose photos and clips, choose a song
or none, choose a style. It calls `VideoFX_CreateSlideshow` with face-aware
motion, beat sync when a song is chosen, captions from file names if asked,
and the ducking preset for the footage. The result opens in the timeline as
an ordinary project, so every automatic choice can be changed. That is the
"impressive with little effort" of the brief, and it is the engine's
strongest feature.

### 5.10 Data that arrives later

Thumbnails, waveforms and rendered frames come from providers the app
installs on the element. They run on a worker pool, cache by key, and the
element redraws the clip when its data lands. Until then a clip draws its
fill without a filmstrip, and never a spinner inside a clip. A filmstrip is
requested at the frame count the clip's width needs at the current zoom,
re-requested when the zoom changes by more than a step, and kept per key so
zooming out and in does not decode twice.

---

## 6. The UI

### 6.1 The editor window (`EditorLayout`)

Reference 1440 × 860, from top to bottom:

1. **App bar**, 40 px: the project name with its format in muted text
   ("Summer trip · 1920 × 1080 · 25 fps · Saved"), undo and redo, and on the
   right the one primary button on screen, **Export**.
2. **Work area**, three columns in two `UltraCanvasSplitPane`s: the **media
   bin** (280 px, min 220) with its tabs *Media*, *Music*, *Titles*,
   *Effects* and a search field over a grid of thumbnails with duration
   badges; the **viewer** with a *Fit* dropdown, the 16:9 frame on the dark
   ground with the title-safe area dashed, and the transport (to start,
   back a frame, play, forward a frame, to end, the large timecode over the
   total, volume, full screen); the **inspector** (300 px, min 260) with
   tabs *Clip*, *Audio*, *Project*.
3. **Timeline dock**: a horizontal splitter, the timeline toolbar (36 px),
   the timeline. It starts at 35 % of the height and the splitter remembers
   its position. Below 1100 px of width the inspector collapses into a tab
   beside the media bin; either side panel can collapse to zero.

Panels sit on the panel background, separated by 1 px gutters of the app
ground; the viewer frame sits on the app ground. Borders, not shadows;
only menus, tooltips and a dragged clip cast one.

### 6.2 The media bin

An `UltraCanvasAlbum` under an `UltraCanvasTextInput` search field, in an
`UltraCanvasTabbedContainer`. *Media* lists imported files: a poster frame,
the name, a duration badge for video and audio, a *photo* badge for images.
*Music* lists songs with their length and detected tempo. *Titles* lists
title styles (opening title, lower third, logo). *Effects* lists effect
presets ("Warm film" = temperature, saturation and a vignette) and the
transition types by family. Everything in the bin is a drag source: media
to the main or overlay track, songs to the music lane, titles to the title
lane, transitions to a cut, effects to a clip. Double-click opens the
file in the viewer. A probe runs on import on a worker thread and the
badge appears when it is done; a file the engine cannot read shows a
warning badge and the reason in its tooltip.

### 6.3 The viewer

`UltraCanvasVideoPlayerElement` for playback of the draft render (§5.5),
and a frame surface under it that shows the rendered or extracted frame
while scrubbing. The transport bar is the app's `UltraCanvasToolbar` with
the `UltraCanvasTimecodeField` readout (§7.6), not the player element's
auto-hiding overlay, because the timeline's playhead and the transport must
be one control. *Fit* offers fit, fill, 50 %, 100 %, 200 %.

### 6.4 The inspector

Three tabs, each a form of catalogue elements:

- **Clip** (for the selected clip): name; *In* and *Out* as timecode fields;
  *Duration* "5.5 s on timeline"; *Speed* slider 0.25–4× with its value;
  *Volume* slider in dB; *Mute* toggle; the **Effects** list with an eye per
  row and *Add effect*; **Transition in**: type dropdown and length
  timecode field with the maximum shown. For a still image: *Motion*
  dropdown (auto, zoom in, zoom out, the pans, custom, still), *Fit*
  dropdown, *Keep faces in shot* switch. For a title span: text, style,
  anchor, size, colours, box, fades. For a colour card: colour picker and
  duration.
- **Audio** (for the music bar, or the project's sound): song list with
  reorder, *Volume*, *Fade in* and *Fade out*, *Crossfade*, *Loop*,
  *Ducking* preset (speech, outdoor, loud event) and level, *Normalise
  loudness* with the LUFS target.
- **Project**: name, size and frame rate, frame fit mode, the default
  transition, the output folder.

Every field reports through the same validate-then-apply path as the
element (§5.4), so a typed speed of 6 is clamped to 4 with the field
outlined in the danger colour until it is corrected.

### 6.5 The timeline toolbar (`TimelineToolbar`)

An `UltraCanvasToolbar` of ghost icon buttons in groups: tools (Select `V`,
Blade `B`, Trim `T`, Slip `Y`), edit actions (Split `S`, Delete, Marker
`M`), modes (Magnet `P`, Snapping `N`, Beat marks), the current timecode
over the project length in the centre, zoom out, a zoom slider, zoom in
and fit on the right. An active tool or toggle shows the soft accent fill
with the accent icon. Every button has a tooltip with its name and key.

### 6.6 The timeline (`Timeline`, `TrackHeader`)

The element of §7. Lanes from top to bottom: title lanes, free video
tracks, the main track, then audio. The header column (200 px) holds, per
track, a kind strip in the track's clip edge colour, the name ("T1 Titles",
"V2 Overlay video", "V1 Main track", "A1 Music"), and the toggles the
track has: magnet (main track only), eye, speaker, lock. The selected
track's header carries the soft accent fill; a hidden track shows the eye
struck through in the danger colour; a muted track its speaker likewise.
The ruler sits over the body, the playhead runs through the ruler and
every lane, and the header column stays fixed while the body scrolls.

### 6.7 Clips, transitions, the music bar (`Clip`, `Transition`, `MusicTrack`)

A **clip** is a rounded block with a 3 px top stripe in its kind's edge
colour, a name band (name, effect and title icons, mute icon, speed badge,
duration badge "5.5 s"), then its content: a filmstrip for video, the
photo with a motion icon for a still, the colour for a card. States: rest;
hover shows the trim grips; selected has the 2 px accent ring; muted gets
a hatched scrim and the crossed speaker; a locked track's clips are dimmed;
the ghost while dragging is lifted with a shadow; while trimming, the
active edge turns accent and the unused source beyond it shows as the
overflow tint with "+2.3 s". The duration badge hides under 110 px, the
icons under 70 px; under 6 px the clip is a 2 px tick in its edge colour.
Frames keep 16:9 at the lane height and are clipped at the clip's right
edge, never stretched.

A **transition** is an inverted badge on the cut, as wide as its length
(min 22 px), with the type icon and "1.5 s". Selected: the accent ring and
4 px accent edges; dragging an edge shows "2.0 s · max 2.0 s". Dropping one
from the bin shows a focus insertion line and a translucent preview.
Double-click changes the type; `Del` reverts to a cut.

The **music bar** spans the music lane with the song or playlist name, the
waveform mirrored around the centre in the waveform colour on the audio
fill, the volume line, fade ramps shaded at both ends with 8 px knobs on
hover, song boundaries with their crossfades shaded, and beat marks when
the toggle is on. Dragging a knob shows "Fade in 2.0 s".

### 6.8 Edit states (`EditStates`)

- **Reorder** on the main track: the ghost lifts, neighbours slide aside in
  160 ms, an accent insertion marker with triangles shows the landing
  place, the readout says "Move before IMG_2041.jpg".
- **Trim with snapping**: the edge snaps to the playhead, a cut, a marker,
  the work area or a beat; a 1 px focus line runs through every lane with
  a diamond on the ruler while snapped; the readout says
  "Out 00:00:03:12 · −00:00:00:12". On the magnetic track the clips after
  the edge ripple; `Alt` leaves a gap instead.
- **File drop**: the lane gets the soft drop fill and a dashed focus
  outline, a focus insertion marker at the nearest cut, and a tooltip
  "Insert 3 files at 00:00:08:00".
- **Rubber band**: a 1 px focus border on the soft fill; clips it touches
  select live; `Shift` extends, `Ctrl` toggles.
- **Blade**: an ink hairline follows the pointer over the clip it would cut;
  `S` splits at the playhead without changing the tool.

### 6.9 The small elements

- **Timecode field** (`TimecodeField`, §7.6): `HH:MM:SS:FF` in mono with
  tabular figures; click selects a segment, `↑`/`↓` step it, `Tab` moves
  on; typed digits fill from the right with the leading zeros dimmed;
  `+12` means twelve frames later; an invalid value gets the danger border
  and `Esc` restores the last valid one. The transport uses the large
  variant as a readout.
- **Context menus** (`ContextMenu`): `UltraCanvasMenu` with a muted header
  naming the target ("Forest_walk.mp4", "Transition · Cross dissolve"),
  grouped items with icons and shortcuts, submenus with a `›` hint,
  destructive items last in the danger colour ("Remove (back to cut)").
- **Tooltips** (`Tooltip`): help after 500 ms with the shortcut in a key
  cap ("Blade B"); the readout variant is inverted, mono, instant, and sits
  6 px from the pointer, never over the edited edge.
- **Buttons** (`Button`): one primary per window; ghost icon buttons in
  toolbars; a toggle that is on uses the soft accent fill; destructive
  buttons use danger text, never a red fill. All 28 px tall.
- **Empty state** (`EmptyState`): a dashed drop zone with a headline ("Drop
  clips here to start"), one sentence and two buttons, *Import media*
  primary and *Add music*; during a drag, a solid focus border, the soft
  fill and "Release to add 3 files".

### 6.10 The export dialog

An `UltraCanvasModalDialog`: a preset dropdown (web MP4 1080p, web MP4
720p, WebM, GIF, ProRes master, audio only, custom), size and frame rate
fields, quality slider, a *whole project* / *work area* choice, the output
path with a browse button, and the estimated length. The codec choices
offered are those `VideoFX_IsVideoEncoderAvailable` and its audio twin
report. *Export* starts a `VideoFXExportJob`; the dialog shows a
`UltraCanvasGaugeDiagramElement` linear bar from `GetProgress()`, a cancel
button, and on success a *Show in folder* button. A failed or cancelled
export leaves no file, which the engine guarantees.

### 6.11 The story wizard

Three steps in a modal, each a form of catalogue elements:

1. **Pictures and clips**: a drop zone and *Add* button, the chosen files
   as a reorderable album, a *Seconds per photo* spinner.
2. **Music**: a song or a playlist, or none; *Fit the video to the song*;
   *Change pictures on the beat*; the ducking preset for footage with
   sound.
3. **Style**: transition family and length; *Keep faces in shot*; portrait
   photos whole on a blurred background or filling the frame; *Captions
   from file names*; fade in and out.

*Make video* builds the slideshow options and runs `VideoFX_CreateSlideshow`
through the export job to a draft, then opens the result as a project on
the timeline with every photo a still-image clip and the song on the
music lane. The user exports from there.

### 6.12 Built from elements

| Region | Element | Notes |
|---|---|---|
| App bar | `UltraCanvasToolbar`, `UltraCanvasLabel`, `UltraCanvasButton` | Export is the primary button |
| Columns and dock | `UltraCanvasSplitPane` × 3 | Positions persist in the project's `view` |
| Media bin | `UltraCanvasTabbedContainer`, `UltraCanvasTextInput`, `UltraCanvasAlbum` | Drag source |
| Viewer | `UltraCanvasVideoPlayerElement`, a frame surface, `UltraCanvasToolbar`, `UltraCanvasTimecodeField`, `UltraCanvasDropdown`, `UltraCanvasSlider` | |
| Inspector | `UltraCanvasTabbedContainer`, `UltraCanvasTextInput`, `UltraCanvasTimecodeField`, `UltraCanvasSlider`, `UltraCanvasDropdown`, `UltraCanvasSwitch`, `UltraCanvasListView`, `UltraCanvasColorPicker`, `UltraCanvasButton` | |
| Timeline toolbar | `UltraCanvasToolbar`, `UltraCanvasTimecodeField`, `UltraCanvasSlider` | Toggles are toolbar buttons with a pressed state |
| Timeline | `UltraCanvasVideoTimeline` (new) | Self-rendered view; child elements for its menus and fields |
| Context menus, tooltips | `UltraCanvasMenu`, `UltraCanvasTooltipManager` | |
| Export, story, settings | `UltraCanvasModalDialog`, `UltraCanvasGaugeDiagramElement`, `UltraCanvasSpinner`, `UltraCanvasCheckbox` | |
| File pickers | `UltraCanvasNativeDialogs` | |

Nothing else is painted. The timeline paints its own content under the
self-rendered-view exception and adds real elements for anything that takes
text.

---

## 7. The timeline element: `UltraCanvasVideoTimeline`

**Header:** `UltraCanvas/include/UltraCanvasVideoTimeline.h`.
**Source:** `UltraCanvas/core/UltraCanvasVideoTimeline.cpp`, with the
model and its pure operations in `UltraCanvasVideoTimelineModel.{h,cpp}`
so they build and test without a display.
**Catalogue row:** *Text, images and media* — "Edit a sequence of media
clips on tracks: trim, reorder, split, transitions, markers, playhead".
**Doc:** `Docs/UltraCanvas/UltraCanvasVideoTimeline.md`, examples compiled
by `check_doc_examples.py`.
**Demo:** `Apps/DemoApp/UltraCanvasVideoTimelineExamples.cpp`, on generated
clips so it runs without media files.

### 7.1 Model (`UltraCanvasVideoTimelineModel.h`)

```cpp
namespace UltraCanvas {

enum class TimelineClipKind { Video, Image, Card, Title, Audio };
enum class TimelineTrackKind { Title, Video, MainVideo, Audio, MainAudio };

struct TimelineClip {
    std::string id;                 // the app's key; never changed by the element
    TimelineClipKind kind = TimelineClipKind::Video;
    std::string name;               // shown in the band, truncated with an ellipsis
    std::string source;             // path, for the providers; empty for a card or title
    double start = 0.0;             // seconds on the timeline (derived for the main track)
    double duration = 1.0;          // seconds on the timeline
    double sourceIn = 0.0;          // seconds into the source used
    double sourceOut = 0.0;         // 0 = to the end; sourceLength says how much exists
    double sourceLength = 0.0;      // 0 = unknown (a card, a title)
    double speed = 1.0;
    bool muted = false;
    bool hasEffects = false;        // badges only: the app keeps the real lists
    bool hasTitle = false;
    bool hasMotion = false;
    uint32_t color = 0;             // Card: 0xRRGGBB
};

struct TimelineTransition {        // on the cut *into* clip `clipId`
    std::string clipId;
    std::string type;               // the engine's name, shown as its icon
    double seconds = 1.0;
    double maxSeconds = 5.0;        // what the readout shows as the limit
};

struct TimelineMarker { std::string id; double seconds; uint32_t color; std::string label; };

struct TimelineTrack {
    std::string id;
    TimelineTrackKind kind;
    std::string name;
    bool magnetic = false;          // MainVideo only
    bool visible = true, muted = false, locked = false, collapsed = false;
    int height = 0;                 // 0 = the default for the kind
    std::vector<TimelineClip> clips;
    std::vector<TimelineTransition> transitions;
};

struct TimelineModel {
    double frameRate = 25.0;
    double length = 0.0;            // seconds; 0 = the end of the last clip
    std::vector<TimelineTrack> tracks;       // top to bottom
    std::vector<TimelineMarker> markers;
    std::vector<double> beats;               // seconds; drawn when beat marks are on
    double rangeIn = 0.0, rangeOut = 0.0;    // work area; 0,0 = none
};

} // namespace UltraCanvas
```

The model mirrors the design's own,
[`screens/components/index.d.ts`](../../Apps/UltraVideo/screens/components/index.d.ts),
which the reference renderer draws: the same tracks, clips, overlay spans,
transitions, markers, beats, range and playhead, with the same units
(seconds on the timeline, duration after speed, pixels per second for the
zoom). Two deliberate differences. The design's model carries view state on
its records (`hover`, `selected`, `ghost`, `trim`, `drop`, the insertion
markers, the snap line, the rubber band) because its renderer is stateless
and redraws from one object; the element keeps that state inside itself and
its model stays what the app owns. And the design's `overlay` track kind
becomes the `Title` kind here, with the overlay *video* lane a `Video` track
that is not the main one, because the two map onto different engine features
(§5.3). The renderer's `timecode`, `ruler` step choice and `peaksPath` are
the behaviours §7.4 and §7.6 specify, and the C++ ports of them are what the
model tests check.

Pure operations on the model, each a free function that returns the
resulting model or a change (§7.3) and is unit-tested: `MoveClip`,
`TrimClip`, `SplitClip`, `DeleteClips`, `DuplicateClip`, `SetSpeed`,
`SetTransition`, `RippleMainTrack` (re-derives main-track starts),
`SnapCandidates`, `ClipAt(track, seconds)`, `CutsNear(seconds)`.

### 7.2 Public API

```cpp
class UltraCanvasVideoTimeline : public UltraCanvasUIElement {
public:
    UltraCanvasVideoTimeline(const std::string& id, int x, int y, int w, int h);

    // ===== MODEL =====
    void SetModel(const TimelineModel& model);      // the app re-feeds after every applied change
    const TimelineModel& GetModel() const;

    // ===== VIEW =====
    void SetPixelsPerSecond(double pps);            // zoom; the wheel and keys call this too
    double GetPixelsPerSecond() const;
    void ZoomToFit();
    void SetScrollSeconds(double seconds);
    void SetPlayhead(double seconds);               // from the player; does not fire onPlayheadChanged
    double GetPlayhead() const;
    void SetFollowPlayhead(bool follow);            // page the view during playback
    void SetTool(TimelineTool tool);                // Select, Blade, Trim, Slip
    void SetSnapping(bool on);
    void SetShowBeats(bool on);
    void SetStyle(const VideoTimelineStyle& style); // sizes, fonts, icon paths; colours from the theme
    void SetSelection(const std::vector<std::string>& clipIds);
    std::vector<std::string> GetSelection() const;

    // ===== PROVIDERS (asynchronous; the element redraws when a result lands) =====
    std::function<void(const std::string& source, double in, double out, int frames, int height,
                       std::function<void(std::vector<std::shared_ptr<UCPixmap>>)>)> thumbnailProvider;
    std::function<void(const std::string& source, double in, double out, double blocksPerSecond,
                       std::function<void(WaveformChannelData)>)> peaksProvider;

    // ===== CHANGES (report, never apply) =====
    // The app validates, applies to its document and calls SetModel; returning false snaps back.
    std::function<bool(const TimelineChange&)> onChangeRequested;
    std::function<void(const std::vector<std::string>& clipIds)> onSelectionChanged;
    std::function<void(double seconds)> onPlayheadChanged;    // user moved it
    std::function<void(double in, double out)> onRangeChanged;
    std::function<void(const TimelineMarker&)> onMarkerActivated;
    std::function<void(const std::string& clipId)> onClipActivated;          // double-click
    std::function<void(const std::string& clipId)> onTransitionActivated;    // double-click the badge
    std::function<void(const std::vector<std::string>& files, const std::string& trackId, double seconds)> onFilesDropped;
    std::function<void(const std::string& trackId, double seconds, const std::string& clipId)> onContextMenuRequested;
    std::function<void(const std::string& trackId, TimelineTrackToggle toggle, bool on)> onTrackToggled;

    // ===== UIElement OVERRIDES =====
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;
    bool AcceptsFocus() const override { return true; }
};
```

### 7.3 Changes

```cpp
enum class TimelineChangeKind { Move, Trim, Slip, Split, Delete, Duplicate, Speed,
                                TransitionSet, TransitionRemove, MarkerAdd, MarkerMove,
                                MarkerRemove, FadeSet, TrackReorder };

struct TimelineChange {
    TimelineChangeKind kind;
    std::string trackId;
    std::vector<TimelineClip> before;        // the clips the change touches, as they were
    std::vector<TimelineClip> after;         // as they would be
    std::optional<TimelineTransition> transitionBefore, transitionAfter;
    std::optional<TimelineMarker> markerBefore, markerAfter;
    double at = 0.0;                         // split time, drop time, marker time
    bool ripple = false;                     // what the magnet and Alt decided
    bool preview = false;                    // true during a drag: the app may validate without applying
};
```

During a drag the element sends previews so the app can answer with the
limit to show in the readout; on release it sends the final change. The
main track's `after` positions are the element's proposal; the app
re-derives them from the engine's rule and re-feeds the model, which is
what the user sees land.

### 7.4 Rendering

Back to front: lane fill, the work-area tint, beat marks, the trim overflow
of the clip being edited, clips (stripe, band, content, badges), transitions,
insertion and snap lines, the playhead. The ruler chooses its minor step from
1 frame, 5 frames, 0.5, 1, 2, 5, 10, 30 or 60 s so minor ticks are at least
8 px apart and major ticks at least 64 px apart, labels `MM:SS` at a major
step of 1 s or more and full timecode below that. Markers sit in the ruler's
top strip with their label and a 1 px line down the ruler. Three zoom levels
are the reference: overview at 8 px/s, edit at 80 px/s, frame at 600 px/s.
Everything is laid out in logical pixels and scaled by the DPI factor; lines
sit on whole pixels so a 1 px line is 1 px.

### 7.5 Keyboard

As the brand book lists them: `←`/`→` a frame, `Shift` a second, `↑`/`↓`
the previous and next cut, `Home`/`End`; `Space`, `J`/`K`/`L`; `+`/`−`
zoom, `Shift+Z` fit; `V`/`B`/`T`/`Y` tools; `S` split; `Del` and
`Shift+Del`; `Ctrl+D`; `,`/`.` nudge; `M` marker; `I`/`O` work area;
`Ctrl+A`; `N` snapping; `P` magnet. The element handles the ones that
change its own view and reports the rest as changes. Modifier reading goes
through the framework's event so macOS gets `Cmd` where the list says
`Ctrl`.

### 7.6 The timecode field: `UltraCanvasTimecodeField`

A second new element, because a timecode entry is input and nothing in the
catalogue types it: `UltraCanvasTextInput` would accept any text, and the
segment selection, stepping and relative entry are the whole point. Header
`UltraCanvasTimecodeField.h`; a catalogue row under *Text entry*; a doc; it
appears in the DemoApp with the timeline. API: `SetFrames(int64_t)`,
`SetFrameRate(double)`, `GetFrames()`, `SetReadout(bool)` for the large
transport variant, `onCommit(int64_t frames)`, `onStep(int64_t)`. It uses
the framework's text input internally for the caret, IME and clipboard, and
adds the segment logic over it.

### 7.7 Tests and docs

- `Tests/VideoTimelineModelTest.cpp`: the pure operations on the model, the
  main-track derivation, snapping, splitting, ripple with and without
  `Alt`, the ruler's step choice.
- `Tests/VideoTimelineElementTest.cpp` under Xvfb: a drag reorders, a trim
  reports before and after, a drop reports files and the nearest cut, the
  readout text.
- The two docs' examples compile under the doc check; both elements get
  their rows in `UltraCanvasUIElements.md` in the same change.

---

## 8. Engine changes (VideoFX)

| # | Change | Needed by | Size |
|---|---|---|---|
| 8.1 | **Waveforms**: `VideoFX_ExtractWaveform` | The music bar, clip waveforms | **Done** in 0.8.0 |
| 8.2 | **Render a frame**: `VideoFXPreview` (`Open` once, `RenderFrame(seconds, frame, maxW, maxH)` per position) and the one-call `VideoFX_RenderFrame` — the output frame at a timeline time with the segment's effects and overlays and, inside an overlap, the transition; music ignored. `VideoFX_PlanSlideshow` gives a slideshow's segments for the same | The viewer while scrubbing (§5.5 level 2) | **Done** in 0.9.0 |
| 8.3 | **Picture-in-picture**: a segment-level list of overlay *video* clips with position, size, opacity and a time range, composed over the output frame | The overlay video lane | Roadmap stage 3; a week |
| 8.4 | **Free audio tracks**: sound effects and voice-over placed at timeline times, mixed before the music | A2 and up | Roadmap stage 3; days |
| 8.5 | **Draft render preset**: `VideoFXExportSettings::Draft(height)`, fast preset, low bit rate, for the playback proxy (§5.5 level 3) | Play | An hour |
| 8.6 | **Export a range**: `start`/`end` on `VideoFX_Export` so the work area exports without the app trimming segments itself | Export dialog's *work area* choice | Small |

None of these change existing calls. 8.2 is the only one the first
release of the editor needs; 8.3 and 8.4 unlock lanes that the element
already carries.

---

## 9. Data model

### 9.1 In the element

§7.1. The element's model is the view's truth and nothing else: it holds
what is drawn and what a change reports, and it is re-fed by the app after
every applied change.

### 9.2 In the application

`UltraVideoDocument`: the VideoFX project (`VideoFXProject` with its
segments, settings and music) as the engine's truth; a map from main-track
clip id to segment index; the title spans, overlay clips and free audio
clips with their full values; the media bin; markers; the view state. Two
functions do all translation: `BuildTimelineModel(document)` derives the
element's model, and `ApplyChange(document, change)` turns a reported
change into edits of the document and an undo entry. `.ultravideo` (§5.6)
is this document serialised; `project` inside it is
`VideoFX_ProjectToJson(document.project)`.

---

## 10. Application skeleton and file layout

```
Apps/UltraVideo/
  main.cpp                      bootstrap, as Apps/UltraClipboard/main.cpp
  UltraVideoWindow.{h,cpp}      the editor window: app bar, panes, media bin, dock, playback,
                                the change loop, files, export
  UltraVideoDocument.{h,cpp}    the document, the .ultravideo file, media bin
  UltraVideoMapping.{h,cpp}     BuildTimelineModel / ApplyChange
  UltraVideoHistory.{h,cpp}     undo stack
  UltraVideoPreview.{h,cpp}     the engine's worker: frame requests, thumbnails, peaks
                                (draft renders and caches later)
  UltraVideoViewer.{h,cpp}      the frame surface, Fit, the transport
  UltraVideoInspector.{h,cpp}   the inspector forms
  UltraVideoDialogs.{h,cpp}     export, the unsaved-changes question (story wizard later)
  UltraVideoTheme.h             the tokens applied to elements
  UltraVideoStory.{h,cpp}       slideshow options from the wizard (phase 4)
  UltraVideoTokens.h            the design tokens as C++ (generated)
  UltraVideo.desktop
  CMakeLists.txt                its own, added from the root with BUILD_ULTRAVIDEO
  README.md
  screens/                      the design: SVG screens, component specs, brand book
Docs/UltraVideo/CHANGELOG.md    "#### <date> *0.1.0*" is the version
Docs/UltraVideo/README.md       how to use it
Tests/UltraVideoDocumentTest.cpp  the document, mapping and history, headless
media/icons/ultravideo/         the interface icon set, loaded at run time
media/appicon/UltraVideo.{png,svg}
```

Build: `option(BUILD_ULTRAVIDEO ...)` and `add_subdirectory(Apps/UltraVideo)`
in the root `CMakeLists.txt` after UltraClipboard's, the app's own
`CMakeLists.txt` linking `${ULTRACANVAS_LIBRARY}` and `VideoFX::VideoFX`,
with `ULTRAVIDEO_VERSION` from
`_ultracanvas_declare_product(ULTRAVIDEO "Docs/UltraVideo/CHANGELOG.md")`
guarded by `#ifndef … #error`; the window title "Summer trip - UltraVideo
0.1.0". Packaging: one `build_app_bundle` call in `package-macos.sh`
(VideoFX's dylib goes to the shared `Frameworks/` with the rest), the
Linux package ships `libVideoFX` now that an app links it, and
`UltraVideo.desktop` registers the `.ultravideo` type and the video, image
and sound types for *Open with*. All of it is in place (§14).

---

## 11. Roadmap

| Phase | Contents | Deliverable |
|---|---|---|
| 0 | This proposal; the screens and specs in `Apps/UltraVideo/screens/`; waveforms in VideoFX 0.8.0 | PR #794 |
| 1 | `UltraCanvasVideoTimeline` and `UltraCanvasTimecodeField`: model, operations, rendering of ruler, main track, clips with filmstrips, transitions, playhead, zoom, selection, keyboard, drop, context menu hook; docs, catalogue rows, DemoApp page, model tests | Framework release |
| 2 | `VideoFX_RenderFrame` (8.2, **done** in 0.9.0), the draft preset (8.5), export range (8.6) | VideoFX 0.9.0 |
| 3 | `Apps/UltraVideo` 0.1.0: window, media bin, viewer with scrubbing, inspector (clip, audio, project), timeline wired through the mapping, undo, `.ultravideo`, export dialog (**built**, §14) | First app release |
| 4 | Title lane and music lane (**built**, §14: spans, fades, song boundaries, beats) in the element; markers, work area, snapping, ripple built in phase 1; the story wizard | App 0.2.0 |
| 5 | Picture-in-picture (8.3) and free audio tracks (8.4) in the engine; the overlay and free audio lanes switched on | VideoFX 0.10, app 0.3 |
| 6 | Keyframes in the engine and keyframe markers on clips; proxy media; hardware encoder choice | Later |

---

## 12. Gap analysis

| Needed | Exists | Gap |
|---|---|---|
| Engine that trims, grades, joins, titles, pans photos, ducks music, exports | VideoFX 0.8.0 | None |
| Waveform data from a media file | `VideoFX_ExtractWaveform` | None (this branch) |
| The composed frame at a timeline time | `VideoFXPreview` / `VideoFX_RenderFrame` (8.2, this branch) | None |
| Picture-in-picture, free audio tracks | — | Planned in the engine; lanes hidden until then |
| A media timeline element | — | `UltraCanvasVideoTimeline` (§7) |
| A timecode entry | — | `UltraCanvasTimecodeField` (§7.6) |
| Player, thumbnails, waveform drawing, album, panes, toolbar, menus, dialogs, gauge, file dialogs, JSON | All in the core | None |
| A reusable undo stack | Per-element only | The app's own `UltraVideoHistory`; a framework one is a candidate (§13) |
| Theme roles for clip kinds and the playhead | Theme has none | Added with the element |
| Fonts with tabular figures | Ubuntu Mono shipped | None |
| An icon set | `Apps/UltraVideo/Icons/`, 46 icons | None; an open set may replace it before release, same names |
| The design's tokens as code | `Apps/UltraVideo/UltraVideoTokens.h` | The mapping onto theme roles, written with the element |

---

## 13. Open questions

1. **A framework undo stack.** Three elements and now an application carry
   their own. A generic `UltraCanvasCommandHistory` would serve UltraPaint
   and ArtCreator too. Not needed for this proposal; a candidate for its own
   change.
2. **A position callback on the player element.** The app reaches through
   `GetPlayer()->onPositionChanged`. A forwarding `onPositionChanged` on
   `UltraCanvasVideoPlayerElement` is a small framework addition.
3. **Dark by default.** The design makes dark the editor's default for
   judging pictures while the rest of the suite follows the system theme.
   Decide whether UltraVideo overrides the theme on its window or follows
   the suite; the proposal assumes it follows the suite and offers dark in
   its view menu.
4. **Where generated media goes.** UltraAI can generate video and images
   (`IVideoGen`, `IImageGen`, the ComfyUI adapter). A *Generate* tab in the
   media bin is an obvious later addition; it is not in this design and
   nothing here blocks it.
5. **Frame-accurate scrubbing cost.** `VideoFXPreview` keeps every file
   open between frames and seeks only when the time asked for is behind
   the decoder or more than two seconds ahead of it, so scrubbing forward
   within a GOP decodes no frame twice. If that is still too slow on long
   GOP footage, a frame cache in the engine is the next step.

## 14. What was built (2026-10-11)

Phase 1 of §11, on the branch of this proposal, with the departures from §7
that writing it showed:

- **`UltraCanvasVideoTimeline`** (`UltraCanvas/include/UltraCanvasVideoTimeline.h`,
  `core/UltraCanvasVideoTimeline.cpp`) and its model and operations
  (`UltraCanvasVideoTimelineModel.{h,cpp}`), the catalogue row, the component
  doc `Docs/UltraCanvas/UltraCanvasVideoTimeline.md`, the DemoApp page
  (*Video > Video Timeline*), the headless `VideoTimelineModelTest` and the
  `VideoTimelineElementTest` under Xvfb.
- The model matches §7.1 with two simplifications: a clip's source range is
  `sourceIn` plus `duration × speed`, with `sourceLength` as the limit, in
  place of a separate `sourceOut`; and `TimelineChange` carries no `preview`
  flag. The element previews a drag on a private copy of the track with the
  model operations and reports one change on release, so the application
  sees nothing until the gesture ends. A readout's limit ("max 2.0 s", the
  unused source beyond an edge) comes from the model's own fields, as §5.4
  intended, not from a refused edit.
- Built: the ruler with adaptive steps, markers and the range; track
  headers with the four toggles; title, free video, main video and audio
  lanes; clips with band, badges, filmstrips and waveforms from providers,
  trim grips, selection, muted scrim, trim overflow; transition badges with
  a draggable length; the playhead; zoom, scroll, fit; reorder on the
  magnetic track with the insertion marker, free placement elsewhere; trim
  with snapping and readout; blade and `S`; delete with ripple; duplicate;
  nudge; markers; the work area; rubber band; file drop with the insertion
  marker; the context-menu hook; the keyboard map of the brand book; light
  and dark palettes.
- **`UltraCanvasTimecodeField`** (§7.6), built the same day: segments
  stepped by the arrow keys and the wheel, digits filling from the right
  with the untyped zeros dimmed, `+`/`-` offsets, validation with the danger
  border, `Esc`, `Tab`, a maximum, copy and paste, the readout variant; the
  frame arithmetic in the model header, tested headless;
  `TimecodeFieldElementTest` under Xvfb; the DemoApp's timeline page drives
  its playhead with one. It owns its entry rather than wrapping the text
  input, because four segments with no caret is not a text field.
- **The preview in the engine** (§8.2), VideoFX 0.9.0: `VideoFXPreview`
  plans a timeline once with the exporter's own planner, keeps the files
  open, and renders one frame per call through the segment's filter chain
  with the frame stamped at its segment time, so fades and timed overlays
  fall where the export puts them; inside an overlap it renders both sides
  and blends them with the transition's own xfade. `VideoFX_RenderFrame`
  is the one-call form, `VideoFX_PlanSlideshow` the slideshow's segments
  without a render, and `videofx preview` writes a project's frame. Tested
  in `VideoFXTest` against the export's own frames.
- **The application** (§10, phase 3 of §11), `Apps/UltraVideo` 0.1.0: the
  window of §6.1 with the app bar, the three panes and the dock; the media
  bin as an `UltraCanvasAlbum` under a search field in tabs; the viewer
  with its own frame surface (the one self-rendered piece: it shows a
  decoded frame from memory, which no catalogue element does), the Fit
  choice and the transport; the inspector's three tabs; the timeline fed
  through `BuildTimelineModel` and written back through `ApplyChange`,
  thumbnails and peaks decoded on the engine's worker; undo with the clean
  mark; the `.ultravideo` file; the export dialog and the background job.
  Departures from the plan: the app colours its elements from the tokens
  directly (`UltraVideoTheme.h`) rather than through theme roles, which
  wait for the framework change of §5.8; the music lane is shown on the
  timeline but edited in the inspector; the main track is always magnetic;
  playback is level 2 of §5.5 (the engine renders each frame the playhead
  passes) with no sound yet. A headless test covers the document, the
  mapping and the history. Writing it found both new elements reading the
  pointer as window coordinates where the dispatcher hands them
  element-local ones; fixed, with the display tests mapping the same way.
- **The title lane** (§5.3, the first part of phase 4): the document keeps
  title spans (`TitleSpan`: text and look, start, length, fades), the bin's
  *Titles* tab offers four styles, the mapping shows them as a free `T1`
  lane of `Title` clips and writes moves, trims, splits, deletes and
  copies back, and `Document::SegmentsForEngine` cuts each span into one
  text overlay per segment it crosses with the fades on the outer ends
  only - exactly the table's row. The project file keeps the spans and
  writes the derived overlays beside them, dropping them again on load.
  The inspector edits a title's text, style, place, size, colour, shadow,
  band and fades.
- **The music lane** (§5.3, §6.7): the element gained fade knobs on
  audio clips (`FadeSet` changes with a "Fade in 2.0 s" readout), and the
  app's lane is unlocked: the first song's fade in and the last song's
  fade out, the crossfade badge's length, the first song's left edge as
  `music.start`, the songs' order by dragging, and `Del` on a song all map
  onto `settings.music`; selecting a song opens the *Audio* tab. Beat marks
  come from `VideoFX_DetectBeats` on the first song, run on the worker when
  the toggle is first switched on. Songs are not split or copied.
- Not built, and left for the next phase: marker dragging, a real slip trim
  (the Slip tool trims for now), retiming by `Ctrl`-drag, vertical scrolling
  of many tracks, resizing a track by its edge, and the mapping onto the
  framework's theme roles (the element carries its colours in its style
  struct, as the other elements do; the app fills it from
  `UltraVideoTokens.h`).

## See also

- `Docs/Modules/VideoFX/README.md` — the engine, with its roadmap
- `Docs/UltraCanvas/UltraCanvasVideo.md` — playback and recording in the UI
- `Docs/UltraCanvas/UltraCanvasWaveformElement.md` — the waveform drawing,
  and its adapter from `VideoFXWaveform`
- `Docs/UltraCanvas/UltraCanvasUIElements.md` — the element catalogue
- `Apps/UltraVideo/screens/BrandBook.md` — the design's tokens and rules
- `UltraClipboardDesignProposal.md`, `UltraCalendarDesignProposal.md` — the
  proposals this one follows
