#### 2026-10-13 *0.2.1*
- **Proxies are made on the graphics hardware.** UltraVideo now makes its
  proxies with VideoFX's `Hardware` acceleration (VideoFX 0.12.0). Each
  proxy is encoded by the first hardware encoder that works on the machine:
  NVENC, Quick Sync, VA-API, AMF, VideoToolbox or Media Foundation. That
  leaves the processor free for the editing while the bin's videos are
  being made smaller. Where no hardware encoder works it falls back to
  software, as before. The encoder is not part of a proxy's name, so
  proxies already in the cache are used as they are.
  (`ProxyMaker::EditorSettings()`, checked in `UltraVideoDocumentTest`.)

#### 2026-10-12 *0.2.0*
- **Proxies: large clips scrub smoothly.** Every video in the media bin gets
  a proxy in the background, made by VideoFX: the same clip at 540 lines,
  quick to seek in. The viewer and the playback draft play the timeline
  from the proxies, and the **export reads the originals**.
  - The proxies are made one at a time on a thread of their own
    (`UltraVideoProxies.{h,cpp}`), not on the preview's worker, so
    scrubbing never waits behind one.
  - The bin tile shows *proxy waiting*, *proxy 40 %* and *proxy*. When a
    clip on the timeline gets its proxy, the viewer switches to it at once.
  - Clips no taller than a proxy get none.
  - Proxies made in an earlier session are ready at once, from the user's
    cache folder.
  - A clip removed from the bin leaves the queue. One that cannot be made
    is reported and plays from the original.
  - *Preview from proxies*, in the inspector's Project tab, is on by
    default. It is saved with the project (`view.useProxies`) and is not an
    undo step.
  - Tested in `UltraVideoDocumentTest`: a clip's proxy made and played, a
    small clip left as it is, the cache reused, a clip dropped before its
    turn, and a missing file.

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
  - The title lane: *T1 Titles* over the main track, a free lane of title
    spans. The bin's *Titles* tab offers four styles - opening title, lower
    third, caption, closing card - and puts one at the playhead; the span
    is dragged, trimmed, split and deleted like a clip, and the inspector
    sets its text, style, place, size, colour, shadow, band and fades. The
    engine gets one text overlay per clip the span crosses, with the fades
    at the span's own ends only, so a title runs across a cut; the viewer
    shows it while editing. The project file keeps the spans and writes
    the derived overlays beside them for `videofx render`.
  - The music lane: the songs under the video in play order with their
    waveforms, fades and crossfades, edited on the timeline - the fade
    knobs at the top of the ramps (the element gained them), the crossfade
    badge's length, the first song's left edge for where the music starts,
    reorder by dragging, `Del` to remove a song - and in the *Audio* tab,
    which opens when a song is selected. *Beat marks* in the toolbar asks
    the engine for the first song's beats and draws them across the lanes.
  - The design's token files: `screens/tokens/tokens.json` as the source
    of truth, `scripts/generate_ultravideo_tokens.py` writing
    `UltraVideoTokens.h` and the live pages' `tokens.css` from it, and
    `screens/fonts/fonts.css`, so the design pages render as designed.
  - Playback with sound: the first Play after an edit renders a draft of
    the timeline in the background (`VideoFXExportSettings::Draft`, 360p,
    with the music and the clips' sound) into a temporary file and plays
    frame by frame until it is ready; then the viewer's player element
    plays the draft with sound and the playhead follows it. An edit
    discards the draft. Without a sound device or player it stays frame by
    frame and says so.
  - Drag from the bin: a tile dragged out of the album (`UltraCanvasAlbum`
    1.9.0's `onItemDraggedOut`) follows the pointer to the timeline as the
    element's own file drop - the insertion marker, the nearest cut on the
    main track, a time on the overlay lane, a song as the music; `Esc`
    lets go.
  - The overlay-video lane: *V2 Overlay video* over the main track, a free
    lane of other files' pictures - a video as picture-in-picture without
    its sound (VideoFX 0.10.0's video overlay), a picture as a logo - added
    from the lane's menu, a bin tile's *Over the picture* or a drop on the
    lane; moved, trimmed (the left edge moves into the file), split and
    deleted like a clip; the inspector sets length, place, size, opacity
    and fades. Cut into one overlay per main-track clip it crosses when
    the engine renders, like a title; the viewer shows it while editing.
  - The story wizard: *Make a story* in the app bar (`Ctrl+Shift+N`), three
    steps in a modal - pictures and clips in order with the seconds per
    photo, a song or none with fit-to-song, beat sync and the ducking
    preset, the style with the transition and its length, faces kept in
    shot over photos, the framing of photos and clips of another shape
    (the engine frames clips with the export's fit mode: filling the frame
    or whole with bars), captions from file names, fade in and out. The
    engine plans the photos (`VideoFX_PlanSlideshow`) on the worker and the
    result opens as an ordinary project: clips and photos in order with
    the transition between them, captions as title spans, the song as the
    music, the fades on the first and last clip.
  - The inspector: *Clip* (in and out, speed, mute, the transition into
    the clip, a photo's motion, framing and faces, a title's text and
    look), *Audio* (the songs, volume, fades, crossfade, loop, ducking),
    *Project* (name, size, frame rate, fit).
  - Undo and redo over every edit, with the clean mark driving the *Saved*
    state and the close question.
  - `.ultravideo` projects: the engine's own project JSON inside, with the
    bin, the markers and the view; media paths relative to the file.
  - Export: a preset (web MP4 at 1080p, 720p or 4K, WebM, GIF, ProRes,
    MP3), the output file, the whole project or the work area; the job
    runs in the background with a progress dialog and can be cancelled.
  - Not yet: effect presets, and the theme-role mapping (the app colours
    its elements from the design's tokens directly).
