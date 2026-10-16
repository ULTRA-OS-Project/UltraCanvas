# UltraCanvas Video Timeline

<!-- doc-check: struct MyDocument { UltraCanvas::TimelineModel model; }; MyDocument document; bool ApplyToDocument(MyDocument& document, const UltraCanvas::TimelineChange& change); std::vector<UltraCanvas::UCPixmapPtr> DecodeThumbnails(const std::string& source, double in, double out, int frames, int height); UltraCanvas::WaveformChannelData DecodePeaks(const std::string& source, double in, double out, double blocksPerSecond); std::shared_ptr<UltraCanvasVideoPlayerElement> player; -->

`UltraCanvasVideoTimeline` — a **media timeline editor**: clips on tracks under
a time ruler, with a playhead, trim handles, reordering, splitting,
transitions on the cuts, markers, beat marks and a work-area range. It is the
timeline of a video editor, and of anything else that sequences media: a
trim view in a viewer, a clip cutter before a post, a sequencer of generated
clips.

Two things make it different from a chart that merely draws spans:

- **It edits a module-free model.** `TimelineModel` (in
  `UltraCanvasVideoTimelineModel.h`) holds tracks, clips, transitions,
  markers, beats and a range, in seconds, and nothing from any media engine.
  The application maps it to and from its engine's own types — VideoFX
  segments, overlays and music, say — and the element never knows them.
- **It reports, it never applies.** Every edit the user makes arrives at
  `onChangeRequested` as a `TimelineChange` with the clips as they were and
  as they would be. The application validates it against its engine's
  limits, applies it to its document, records an undo entry and calls
  `SetModel` with the result. A handler that returns `false` refuses the
  edit and the element shows the model as it is. Engine limits are therefore
  the application's to enforce, and readouts such as "2.0 s · max 2.0 s"
  come from values in the model (`TimelineTransition::maxSeconds`,
  `TimelineClip::sourceLength`), never from an edit being refused.

**Namespace:** `UltraCanvas`
**Header:** `include/UltraCanvasVideoTimeline.h`; model and operations
`include/UltraCanvasVideoTimelineModel.h`
**Source:** `core/UltraCanvasVideoTimeline.cpp`, `core/UltraCanvasVideoTimelineModel.cpp`
**Base class:** `UltraCanvasUIElement` (a self-rendered view; it adds no
child elements of its own)
**Demo:** `Apps/DemoApp/UltraCanvasVideoTimelineExamples.cpp` (Video > Video Timeline)
**Tests:** `Tests/VideoTimelineModelTest.cpp` (the model, without a display)
**Design:** the screens and specifications in `Apps/UltraVideo/screens/`, and
`Docs/Research/UltraVideoDesignProposal.md` §7
**Version:** 1.2.0
**Last Modified:** 2026-10-14
**Author:** UltraCanvas Framework

## Status

First phase. Built: the ruler with adaptive ticks, markers and the range;
track headers with magnet, eye, speaker and lock toggles; title, video,
main-video and audio lanes; clips with a name band, badges (duration, speed,
mute, title, effects, motion), filmstrips from a thumbnail provider,
waveforms with fade ramps from a peaks provider, trim grips, selection ring,
muted scrim and the unused source beyond an edge; transitions as badges on
the cut with a draggable length; the playhead; zoom around the pointer,
scrolling, fit; reorder on the magnetic main track with an insertion
marker, free placement elsewhere; trim with snapping and a live readout;
split with the blade tool or `S`; delete with ripple; duplicate; nudge;
markers; the work area; rubber-band selection; file drop with an insertion
marker; a context-menu hook; keyboard throughout. Light and dark palettes,
and `VideoTimelineStyle::FromTheme` for the framework theme
(`UltraCanvasTheme.md`): the shared roles for surfaces, ink, accent, focus
and danger, the element's own `video-timeline.<role>` colours where the
theme names them.

Not yet: marker dragging, slip trimming (the tool is accepted and trims for
now), retiming by `Ctrl`-drag, vertical scrolling of many tracks, resizing
a track by its edge.

## The model

```cpp
TimelineModel m;
m.frameRate = 25.0;

TimelineTrack main;
main.id = "V1";
main.kind = TimelineTrackKind::MainVideo;   // the magnetic track: clips are a sequence
main.name = "Main track";

TimelineClip beach;
beach.id = "beach";                         // the app's key; the element never changes it
beach.kind = TimelineClipKind::Video;
beach.name = "Beach_wide.mp4";
beach.source = "/media/Beach_wide.mp4";     // for the providers
beach.duration = 6.5;                       // seconds on the timeline, after speed
beach.sourceIn = 2.0;                       // seconds into the source
beach.sourceLength = 12.0;                  // what exists, so trimming knows its limit
main.clips.push_back(beach);

TimelineClip photo;
photo.id = "photo";
photo.kind = TimelineClipKind::Image;
photo.name = "IMG_2041.jpg";
photo.source = "/media/IMG_2041.jpg";
photo.duration = 4.0;
photo.hasMotion = true;                     // a badge; the motion itself is the app's
main.clips.push_back(photo);

main.transitions.push_back({"photo", "crossfade", 1.0, 4.0});   // into `photo`, 1 s, at most 4 s

TimelineTrack music;
music.id = "A1";
music.kind = TimelineTrackKind::MainAudio;
music.name = "Music";
TimelineClip song;
song.id = "song";
song.kind = TimelineClipKind::Audio;
song.name = "Summer Drive";
song.source = "/media/song.mp3";
song.duration = 10.5;
song.sourceLength = 212.0;
song.fadeIn = 2.0;
song.fadeOut = 3.0;
music.clips.push_back(song);

m.tracks = {main, music};                   // top to bottom
m.markers.push_back({"m1", 8.0, 0xA65A00, "Chorus"});
m.beats = {0.5, 1.0, 1.5};                  // from beat detection; drawn when SetShowBeats(true)
m.rangeIn = 2.0;                            // the work area; both 0 = none
m.rangeOut = 9.0;
```

| Type | Holds |
|---|---|
| `TimelineClip` | `id`, `kind` (`Video`, `Image`, `Card`, `Title`, `Audio`), `name`, `source`, `start`, `duration`, `sourceIn`, `sourceLength`, `speed`, `muted`, the badge flags `hasEffects`, `hasTitle`, `hasMotion`, a card's `color`, an audio clip's `fadeIn` / `fadeOut`; `End()`, `SourceOut()`, `SourceBefore()`, `SourceAfter()` |
| `TimelineTransition` | On the cut *into* `clipId`: `type`, `seconds`, `maxSeconds` |
| `TimelineMarker` | `id`, `seconds`, `color`, `label` |
| `TimelineTrack` | `id`, `kind` (`Title`, `Video`, `MainVideo`, `Audio`, `MainAudio`), `name`, `magnetic`, `visible`, `muted`, `locked`, `collapsed`, `height`, `clips`, `transitions`; `IsMain()`, `IsMagnetic()`, `FindClip()`, `FindTransition()`, `ClipIndex()`, `End()` |
| `TimelineModel` | `frameRate`, `length`, `tracks`, `markers`, `beats`, `rangeIn`, `rangeOut`; `Length()`, `FindTrack()`, `TrackOfClip()` |

**Magnetic and free tracks.** The one `MainVideo` track (and the one
`MainAudio` track) is *magnetic* while its `magnetic` flag is on: its clips
are a sequence, each starting where the one before ends less the transition
into it, so a clip is reordered and never placed at a free time, deleting
closes the gap, and trimming moves the clips after it. `TimelineDeriveStarts`
computes those starts; `SetModel` runs it, so an app need only keep the
order. Every other track places clips at their `start`, and a move or a
trim stops at the neighbours.

## Public API surface

```cpp
auto timeline = CreateVideoTimeline("timeline", 0, 300, 1200, 320);

timeline->SetModel(m);                       // and again after every applied change
timeline->SetPixelsPerSecond(80.0);          // zoom, 2..2000
timeline->ZoomToFit();
timeline->ZoomBy(1.25, 10.0);                // around 10 s
timeline->SetScrollSeconds(4.0);             // the time at the body's left edge
timeline->SetPlayhead(11.2);                 // from the player; no callback
timeline->SetFollowPlayhead(true);           // page the view during playback
timeline->SetTool(TimelineTool::Blade);      // Select, Blade, Trim, Slip
timeline->SetSnapping(true);
timeline->SetShowBeats(true);
timeline->SetStyle(VideoTimelineStyle::DarkPalette());
timeline->SetSelection({"beach"});
timeline->SetSelectedTransition("photo");    // the badge on the cut into `photo`
timeline->SetSelectedTrack("V1");

double seconds = timeline->XToSeconds(400.0f);   // local x -> time
float x = timeline->SecondsToX(seconds);
std::string trackId;
std::string clipId = timeline->ClipAt(400.0f, 120.0f, &trackId);
timeline->SetDropTargetClip(clipId);         // a dashed focus ring: where a drop the app carries lands
timeline->SetDropTargetClip("");             // ... cleared
```

### Providers

Thumbnails and peaks arrive later, from the app. The element asks once per
clip and zoom step, caches what it gets, and draws the clip's fill alone
until then — never a spinner inside a clip. The callback may run at once or
after a decode on another thread; call `done` on the UI thread.

```cpp
timeline->thumbnailProvider = [](const std::string& source, double in, double out, int frames, int height,
                                 std::function<void(std::vector<UCPixmapPtr>)> done) {
    done(DecodeThumbnails(source, in, out, frames, height));   // VideoFX_ExtractThumbnails, say
};
timeline->peaksProvider = [](const std::string& source, double in, double out, double blocksPerSecond,
                             std::function<void(WaveformChannelData)> done) {
    done(DecodePeaks(source, in, out, blocksPerSecond));       // VideoFX_ExtractWaveform's envelope fits as it is
};
timeline->ClearCaches();                     // after media changed on disk
```

### Changes

```cpp
timeline->onChangeRequested = [&](const TimelineChange& change) {   // capture the element raw, never a shared_ptr to it
    if (!ApplyToDocument(document, change)) return false;   // refused: the element shows the model as it is
    timeline->SetModel(document.model);
    return true;
};
```

| `TimelineChangeKind` | `before` / `after` | Also |
|---|---|---|
| `Move` | Magnetic track: every clip of the track, in the old and the new order, with derived starts. Free track: the one clip, at its old and new `start` | `ripple` |
| `Trim` | As for `Move`; the trimmed clip carries its new `start`, `duration` and `sourceIn` | `at` the edge's time |
| `Split` | The one clip; the two parts, the right one with the id `<id>:2` (the app may rename it on apply) | `at` |
| `Delete` | The removed clips; on a magnetic track `after` is what remains | `ripple` |
| `Duplicate` | The clip; the copy (`<id>:copy`), or the whole magnetic track with the copy in place | `at` |
| `TransitionSet` | The clip the cut goes into | `transitionBefore` (if any), `transitionAfter` |
| `TransitionRemove` | The clip | `transitionBefore` |
| `FadeSet` | An audio clip, before and after its fade knob was dragged: `after[0]` carries the new `fadeIn` / `fadeOut` | `at` the new fade's seconds |
| `MarkerAdd`, `MarkerRemove` | — | `markerAfter` / `markerBefore`, `at` |

Every change names its `trackId`. A magnetic track's `after` is the whole
sequence, so the app can replace the track's clips with it outright.

### Other callbacks

```cpp
timeline->onSelectionChanged = [](const std::vector<std::string>& clipIds) { /* drive the inspector */ };
timeline->onPlayheadChanged = [&](double seconds) { player->Seek(seconds); };
timeline->onRangeChanged = [](double in, double out) { /* I and O keys */ };
timeline->onClipActivated = [](const std::string& clipId) { /* double-click: open the inspector */ };
timeline->onTransitionActivated = [](const std::string& clipId) { /* double-click: choose a type */ };
timeline->onMarkerActivated = [](const TimelineMarker& marker) { /* edit label and colour */ };
timeline->onFilesDropped = [](const std::vector<std::string>& files, const std::string& trackId, double seconds) {
    /* import, then insert at `seconds` (the nearest cut on a magnetic track) */
};
timeline->onContextMenuRequested = [](const std::string& trackId, double seconds, const std::string& clipId,
                                      int windowX, int windowY) {
    /* build an UltraCanvasMenu for the clip, the lane or the transition at (windowX, windowY) */
};
timeline->onTrackToggled = [](const std::string& trackId, TimelineTrackToggle toggle, bool on) {
    /* Magnetic, Visible, Muted, Locked: set it in the document and SetModel */
};
timeline->onZoomChanged = [](double pixelsPerSecond) { /* mirror in a zoom slider */ };
timeline->onToolChanged = [](TimelineTool tool) { /* mirror in the toolbar */ };
```

The player link runs both ways: the player's `onPositionChanged` into
`SetPlayhead`, and `onPlayheadChanged` into the player's `Seek`.

### The operations on their own

The functions the element previews with are free functions on the model,
for an app that wants the same arithmetic in a command, a test or a script:

```cpp
TimelineDeriveStarts(main);                              // starts from the order, less transition overlaps
std::vector<double> cuts = TimelineCuts(main);
TimelineSplitClip(main, "beach", 5.5, 25.0, "beach:2");
double applied = 0.0;
TimelineTrimClip(main, "beach", false, 9.0, 25.0, &applied);   // the right edge to 9 s, clamped to the source
TimelineMoveClip(main, "photo", 0, 0.0);                 // magnetic: to the front
TimelineDeleteClips(main, {"photo"}, true);
TimelineDuplicateClip(main, "beach", "beach:copy");
double minor = 0.0, major = 0.0;
TimelineRulerSteps(80.0, 25.0, minor, major);            // 0.2 s ticks, labels every second
std::string tc = TimelineTimecode(11.2, 25.0);           // "00:00:11:05"
std::optional<double> snapped = TimelineSnap(6.9, cuts, 0.1);
double onGrid = TimelineToFrame(1.013, 25.0);
```

## Keyboard and mouse

| Key | Does |
|---|---|
| `←` / `→`, `Shift` + | Step the playhead a frame, a second |
| `↑` / `↓` | The previous, the next cut |
| `Home` / `End` | Start, end |
| `=` / `-`, `Shift+Z` | Zoom in, out; fit the project |
| `V` `B` `T` `Y` | Select, Blade, Trim, Slip |
| `S` | Split the selected clip, or the one on the selected track, at the playhead |
| `Del`, `Shift+Del` | Delete the selection (ripple on a magnetic track, a gap elsewhere; `Shift` inverts); a selected transition reverts to a cut |
| `Ctrl+D` | Duplicate |
| `,` / `.` | Nudge a free-track clip a frame |
| `M` | A marker at the playhead |
| `I` / `O` | The work area's in and out |
| `Ctrl+A` | Select every clip of the selected track |
| `N`, `P` | Snapping; the main track's magnet |
| `Esc` | Cancel a drag, or clear the selection |

The wheel zooms around the pointer; `Shift`+wheel and a horizontal wheel
scroll. Click a clip to select it, `Shift`-click for a range, `Ctrl`-click
to toggle; drag on empty lane space for a rubber band. Drag a clip to
reorder (magnetic) or move (free); drag an edge to trim; drag a transition
badge's edge for its length; on an audio clip, hover and drag the knob at
the top of a fade ramp for the fade (the readout says "Fade in 2.0 s" and
the change is a `FadeSet`); click or drag the ruler for the playhead;
double-click a clip, a transition or a marker to activate it; right-click
for the context-menu hook. A hidden or locked track draws dimmed and a
locked one takes no edits.

## Style

`VideoTimelineStyle` holds every colour, size and font. The defaults are the
light palette of the UltraVideo design; `VideoTimelineStyle::DarkPalette()`
is the dark one, `LightPalette()` the light one again. Clip kinds pair a
fill with an edge (`clipVideo` / `clipVideoEdge`, …), the transition badge
is inverted (`transitionFill` / `onTransition`), guides are translucent
(`beat`, `trimOverflow`, `dropSoft`, `rangeFill`, `mutedHatch`). Sizes:
`rulerHeight` 28, `headerWidth` 200, track heights 64 / 48 / 28 / 24,
`clipBandHeight` 16, `handleWidth` 8. Fonts: `fontFamily` for labels,
`monoFamily` for every number that changes, so timecode does not jitter.

```cpp
VideoTimelineStyle style = VideoTimelineStyle::DarkPalette();
style.accent = Color(242, 165, 58);
style.headerWidth = 180.0f;
timeline->SetStyle(style);
```

## Notes

- The element draws its content itself (a self-rendered view) and adds no
  child elements; the application supplies menus, tooltips and fields
  through the callbacks.
- A provider's `done` may run after the element is gone; the element
  ignores it then, so a decode need not be cancelled.
- The model keeps seconds; the element rounds edits to the frame grid of
  `frameRate` and shows `HH:MM:SS:FF`.
- Thumbnail requests are keyed by source, range, frame count and height,
  with the frame count rounded up to a multiple of four so that zooming
  does not decode at every step.
