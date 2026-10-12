#### 2026-10-12 *0.2.0*
- **Effect presets.** The bin's *Effects* tab lists ten presets - *Warm
  film* (temperature, saturation, a vignette), *Cool morning*, *Black and
  white*, *Old photo*, *Vivid*, *Faded*, *Soft focus*, *Crisp*, *Fade in
  and out*, *Quieter sound* - and a click puts one on the selected clip: its
  effects replace any of the same types already there, so a preset applied
  twice is there once and "Cool" after "Warm" turns the warmth round. Under
  them the transitions, one click to put one on the cut into the selected
  clip. The inspector's *Clip* tab gained an **Effects** list: every effect
  on the clip by name and amount ("Warmth +30 %", "Fade in 0.5 s"), a cross
  per row to take it off, *Clear* for all; the clip's context menu offers
  *Remove the effects*. The timeline's *Fx* badge marks a clip with effects.
  The viewer and the export render them as before.
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
  labels too, by their variant (label 2.11.0): the project's name and the
  section titles are headings, notes and captions muted, the preset hints
  faint. `ApplyPalette` keeps the timeline's token sizes alone. The export
  dialog, the unsaved-changes question and the story wizard take no
  palette any more: the dialogs follow the theme (modal dialog 3.7.0) -
  their surface, message, captions and footer buttons - and the wizard's
  tabs and album like the window's.

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
