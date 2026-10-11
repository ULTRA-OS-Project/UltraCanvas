#### 2026-10-11 *0.1.0*
- **The first release: the editor's skeleton.** `Apps/UltraVideo`, on
  VideoFX 0.9.0 and the framework's `UltraCanvasVideoTimeline` and
  `UltraCanvasTimecodeField` elements, as phase 3 of
  `Docs/Research/UltraVideoDesignProposal.md` lays it out:
  - The editor window: the app bar with the project's name, format and
    saved state, undo and redo, open, save, import, light or dark, and
    **Export**; the media bin, the viewer and the inspector in split panes;
    the timeline dock with its toolbar under them. The design's dark
    palette by default, the light one a click away.
  - The media bin: videos, photos and songs imported from a dialog, dropped
    on the window or named on the command line (a folder imports its
    files), probed by the engine, shown as an album with a duration or
    *photo* badge; double-click or *Add* puts a clip on the timeline and a
    song under the whole video as its music.
  - The viewer: the output frame at the playhead rendered by `VideoFXPreview`
    on a worker thread - effects, overlays and transitions included - with
    the title-safe guide, a *Fit* choice, and the transport: to start, a
    frame back, play, a frame forward, to end, the timecode readout over
    the length. Play moves the playhead in real time and shows the frames
    the engine renders for it.
  - The timeline: the element, fed from the document by one mapping
    (`BuildTimelineModel`) and writing every reported change back through
    the other (`ApplyChange`): reorder, trim, split, delete, duplicate, a
    transition's type and length, markers, the work area, drops of files
    at a time. Thumbnails and waveforms come from the engine on the worker.
    The toolbar: the four tools, split, delete, marker, snapping, beat
    marks, the playhead's timecode field over the length, zoom.
  - The inspector: *Clip* (in and out, speed, mute, the transition into
    the clip, a photo's motion, framing and faces), *Audio* (the songs,
    volume, fades, crossfade, loop, ducking), *Project* (name, size, frame
    rate, fit).
  - Undo and redo over every edit, with the clean mark driving the *Saved*
    state and the close question.
  - `.ultravideo` projects: the engine's own project JSON inside, with the
    bin, the markers and the view; media paths relative to the file.
  - Export: a preset (web MP4 at 1080p, 720p or 4K, WebM, GIF, ProRes,
    MP3), the output file, the whole project or the work area; the job
    runs in the background with a progress dialog and can be cancelled.
  - Not yet: the title and overlay-video lanes, the story wizard, effect
    presets, drag from the bin onto the timeline, playback with sound (the
    draft render), and the theme-role mapping (the app colours its
    elements from the design's tokens directly).
