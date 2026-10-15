#### 2026-10-14 *0.5.0*
- **The theme roles.** The design's palette is now the framework's theme
  (`UltraCanvasTheme` 1.0.0, `ThemeFor` in `UltraVideoTheme.h`): the
  surfaces, lines, ink, accent, focus and danger tokens fill the shared
  roles, the clip kinds, waveform, transition badge, guides and playhead
  are the timeline's own roles, and the timeline and the timecode field
  take their colours from it through `FromTheme`. With `UltraCanvasTheme`
  1.1.0 the split panes, the bin's and the inspector's tabs, the search
  and text fields, the albums, the toolbars, the dropdowns and the clip's
  context menu *follow* the theme (`SetFollowTheme`): the window sets the
  theme when the palette switches and they re-style themselves, menus and
  tooltips included; `ApplyPalette` keeps only the app's own looks - the
  ghost and primary buttons, the sliders, the switches, the timeline.
- **Playing with sound, confirmed.** The draft played in the framework's
  player with the GStreamer plugins installed: the playhead follows the
  player's position, the end of the draft stops playback at the end, Play
  again starts over. Two fixes from that run: a player that cannot start
  (no audio sink it can open) now falls back to frame-by-frame playback
  with the message, where before the playhead jumped to the end; and the
  framework's GStreamer backend no longer insists on a PulseAudio sink
  where no PulseAudio server answers, so the picture plays at pace, silent,
  on such a desktop. The sound itself was then measured through a
  PulseAudio null sink: it starts and stops with playback, the music's
  fade and the clip's own sound arrive where the timeline has them, and
  the volume slider at half halves the level. `UltraVideoDraftTest` pins
  it without a recorder: the draft (`Document::DraftSettings`, the
  settings the app renders with) decoded shows its sound stepping in at
  the document's cut within a frame, and the framework's player, playing
  it headless, reports a position within two frames of the frame it
  shows and reads the cut when the clip's first frame appears.
- **The draft in the frame's place.** The draft is rendered at the
  project's shape, 360 lines high: with only a height given the engine took
  the width from the first segment, and a 4:3 photo first made a 4:3 draft
  of a 16:9 project that sat letterboxed and smaller than the frame
  surface. The player keeps the surface's 12 px inset and follows the
  viewer's *Fit* menu (fit, fill, 50, 100, 200 %) as the surface does, so
  the picture does not move when the draft takes over; the title-safe
  guide stays on over the draft too. A percent zoom is of the project's
  pixels on both sides (the surface asks the engine for a frame of that
  size; the player scales the 360-line draft by the project's height), so
  *100 %* shows the same picture before and during playback.
- **Sliders and switches follow the theme.** The zoom, volume and
  inspector sliders and the inspector's switches follow the theme like
  the other controls: the design's ink handle and on-accent thumb are the
  theme's `slider.handle` and `switch.thumb` element roles (framework
  slider 3.7.0, switch 1.6.0).
- **Buttons follow the theme by their variant.** The app bar's icons, the
  transport, the timeline tools, the preset buttons, *Export* and the
  dialogs' buttons are the framework's `Ghost`, `Standard` and `Primary`
  variants (button 2.9.0), the active tool its `Selected` one: the theme
  colours them and a switch of the look reaches them on its own. The
  labels too, by their variant (label 2.13.0): the project's name and the
  section titles are headings, notes and captions muted, the preset hints
  faint. `ApplyPalette` keeps the timeline's token sizes alone. The export
  dialog, the unsaved-changes question and the story wizard take no
  palette any more: the dialogs follow the theme (modal dialog 3.9.0) -
  their surface, message, captions and footer buttons - and the wizard's
  tabs and album like the window's.

#### 2026-10-14 *0.4.2*
- **One UltraVideo at a time.** Opening a project or media files while
  UltraVideo is already running handles them in the running copy instead of
  starting a second one: a `.ultravideo` project opens there - asking first
  whether to save the current project when it has changes, as *Open project*
  does - and media files are imported into the open project, as dropping
  them on the window does; the window, or its save question, comes forward.
  Starting it without a file brings the open window forward. Two starts at
  the same moment cannot both stay, and a copy that does not answer is
  reported with *"UltraVideo is already running"*. `--help` and `--version`
  are unaffected. Framework: `UltraCanvasSingleInstance` (framework
  changelog: `instance-lock`).

#### 2026-10-14 *0.4.1*
- **Import and Export offer FileLoader's file types.** UltraVideo kept lists
  of its own, so its dialogs offered types the engine could not read and
  missed ones it could.
  - At start-up UltraVideo now tells the framework's codec registry which
    films and sound VideoFX (FFmpeg) reads and writes
    (`VideoFX_GetFileFormats`). FileLoader's lists then include them; on
    macOS, for example, the registry alone knew only MP4, MOV and 3GP.
  - Import media, the file over the picture, a voice-over or sound effect
    on A2, and the story's photos, clips and songs offer FileLoader's types
    that the engine reads.
  - **HEIC photos import whole.** Picture types FileLoader reads and FFmpeg
    does not are read through the framework's image reader
    (`VideoFX_SetPictureReader`): HEIC, a camera's raw files, FITS and so on.
    For HEIC this is libheif, which puts a phone photo's grid of tiles
    together and turns it upright. FFmpeg 6 refused HEIC, and FFmpeg 7 would
    have given one tile of the photo.
  - Whether a file is a picture or sound is decided by the same lists, so a
    picture this build cannot decode (a HEIC where libheif lacks its HEVC
    decoder) still shows as a photo, marked unreadable.
  - Export's Browse offers the presets' types under FileLoader's names.
    Picking a type there selects the preset that writes it, so an MP4 can
    no longer be saved under a `.wav` name.
  - The bare `.ts` extension is no longer offered: the framework decides it
    by the file's contents, since it is far more often TypeScript. `.mts` and
    `.m2ts` are offered.
  - New file `UltraVideoFormats.{h,cpp}`, checked in `UltraVideoDocumentTest`.
#### 2026-10-13 *0.4.0*
- **Effect presets in the bin.** The bin's *Effects* tab is no longer a
  placeholder. It holds one-click presets for the selected main-track clip.
  - Nine colour looks: *Warm*, *Cool*, *Vivid*, *Faded*, *Dramatic*,
    *Black and white*, *Sepia*, *Vintage* and *Brighter*.
  - Four presets under *Blur and detail*: *Soft blur*, *Strong blur*,
    *Sharpen* and *Reduce noise*.
  - A preset is made of the engine's own effects (temperature, saturation,
    contrast, vignette, blur and so on). They are added after the clip's
    other effects, and a second click on the same preset changes nothing.
  - A blur's radius is a share of the clip's picture height, so it looks
    the same on a phone clip and a 4K one.
  - The viewer, the draft and the export show the look, and the clip gets
    its effects badge on the timeline.
  - The inspector's Clip tab lists the clip's effects, one row per effect
    named by what it does (*Warmer 30 %*, *Blur 3.5 px*), each with a
    button that removes it, plus *Remove all effects*.
  - **Each effect has a strength slider** on its row, so *Warm* can be made
    warmer or gentler and a blur stronger. The name follows the slider as
    it moves.
    - The range is the engine's own units for that effect: -1 to 1 for a
      look's amounts, up to 50 px for a blur, -180° to 180° for a hue.
    - Switches (black and white, sepia, a flip, a quarter turn) and a crop
      have no slider.
  - Every change is an undo step.
  - (`EffectPresets`, `Document::ApplyEffectPreset`, `DescribeEffect`,
    `RangeOfEffect`, `Document::SetEffectAmount`; tested in
    `UltraVideoDocumentTest`. The test renders a frame with every preset
    and at both ends of every slider's range, and checks that *Black and
    white* turns the picture grey.)

#### 2026-10-13 *0.3.0*
- **Voice-overs and sound effects at any time.** A new free lane on the
  timeline, **A2 Voice & sound**, sits between the main track and the
  music. It holds sound clips placed at any time, over the clips' own
  sound and the music, mixed by VideoFX's audio clips (VideoFX 0.13.0).
  - Two kinds of clip. A **voice-over** makes the music dip under it. A
    **sound effect** plays over the music without changing it. A switch
    in the inspector turns one into the other, as does the clip's menu.
  - Adding a clip:
    - right-click the lane for *Add a voice-over here…*, *Add a sound
      effect here…*, or one of the bin's files with sound (songs, and
      videos for their sound);
    - use the *Voice-over* and *Sound effect* buttons on a song in the
      bin's *Music* tab, which place it at the playhead;
    - or drop files on the lane; a dropped file becomes a voice-over.
  - A sound clip is moved, trimmed, split, duplicated and deleted like any
    clip, and shows its waveform. Its fade handles set its fades, and
    trimming its left edge moves into the file.
  - The inspector sets *Volume* (up to 200 %), *Fade in*, *Fade out* and
    *The music dips under it*.
  - The playback draft and the export mix the lane. Exporting the work
    area keeps the part of each clip that lies inside it.
  - The `.ultravideo` file keeps the lane's spans (`sounds`). The engine's
    project inside the file carries them as audio clips, so `videofx
    render` gives the same sound. A VideoFX project's audio clips, opened
    in UltraVideo, appear on the lane.
  - (`SoundSpan`, `Document::AddSound`, `AudioClipsForEngine`, the `A2`
    mapping; tested in `UltraVideoDocumentTest`.)
- A selected overlay clip is no longer deselected whenever the timeline
  is redrawn.

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
