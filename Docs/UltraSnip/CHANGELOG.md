#### 2026-11-10 *0.1.4*
- **Window mode shows the window it will take, in its own colours.** The
  desktop is darkened further than in Rectangle mode, and the window under
  the pointer is no longer tinted blue: it shows exactly as it will be
  captured, framed, with its title and size beside it. It is lit as soon as
  the selection opens or Window is chosen on the bar, from where the
  pointer rests, rather than at the first movement of the mouse.

#### 2026-11-07 *0.1.3*
- **UltraSnip opens over the screen already dimmed, its bar at the top
  centre.** Started without a shape (from the menu, or a second start), it
  takes the screen first and lays the dimmed picture over it, then puts its
  bar centred across the top of the primary screen: the shape chosen on the
  bar can be dragged out, or a window clicked, at once - no Start needed.
  Changing the shape on the bar changes the selection on the spot. Escape or
  a right click puts the dim away and leaves the bar; Start (with its delay)
  and Settings put it away first and go on as before. The hint line moves
  down below the bar.
- **The delay is a segmented choice like the shape and the kind**: 0, 3, 5
  and 10 seconds, every segment as wide as the widest caption in the
  language shown, each with its own tooltip. It replaces the dropdown.
- **Each segment of the bar says its own name.** The shape, kind and delay
  tooltips all showed the first segment's text (the tooltip was read once,
  when the pointer came into the control); they now follow the segment under
  the pointer - see the framework's `SetSegmentTooltip`.

#### 2026-11-04 *0.1.2*
- **The delay list is exactly as wide as its longest item.** Its width was
  estimated from the number of characters, which counts Thai and
  Devanagari vowel and tone marks as characters of their own, so the list
  ran wide in Thai and Hindi. It is now measured in the list's own font by
  the window (which has its render context before the bar is built), plus
  the room the list's padding and arrow take; the estimate is kept only for
  a window without a render context.

#### 2026-11-04 *0.1.1*
- **One bar at the top, in the settings window's type size.** The window
  had two bars in large type: New and the choices above the picture, Save
  as, Copy, Open folder, Edit in UltraPaint, Settings and the status line
  under it. Everything is now in one bar at the top, at 10 points: *Start*
  (was *New*), the shape (Rectangle, Window, Full screen) and the kind
  (Picture, Video) as icon segments that name themselves in their
  tooltips, the delay, then Save as, Copy, Open folder and Edit in
  UltraPaint as icon buttons, the line saying what was done, and Settings
  (the cog) at the right end. Until there is a snip the window is the bar
  alone; a picture or a recording opens it out below the bar, at the height
  it last had. New icons for the shapes and for video:
  `media/icons/snip-rectangle.svg`, `snip-window.svg`, `snip-screen.svg`,
  `snip-video.svg`.
- **UltraSnip speaks the chosen language.** Every text it shows goes
  through `Tr` / `TrF` (UltraLocalisation): the bar's buttons, tooltips and
  delay list, the status line (its pieces - the size, "saved as", "copied"
  - as whole phrases with `{0}` for the name), the Save as dialogs' titles
  and filters, the error messages of the capture, the recorder and the
  selection overlay, the recording bar, and the whole settings window. Its
  English set is `media/locale/en/UltraSnip.json` (119 entries of its own,
  10 from the *Image* and *Video* base sets and the standard set); the
  language drafts do not carry it yet. *Translate UltraSnip...* on the
  settings window's start page opens the localisation editor on its own set.
  The command line's help stays English.
- **UltraSnip in thirty languages.** Machine drafts of its set
  (`media/locale/<lang>/UltraSnip.json`, marked `machine draft
  2026-11-04, reviewed by -`) for every language the standard set has:
  Arabic, Catalan, Chinese (Simplified and Traditional), Czech, Danish,
  Dutch, Finnish, French, German, Greek, Hebrew, Hindi, Hungarian,
  Indonesian, Italian, Japanese, Korean, Norwegian Bokmål, Polish,
  Portuguese (and Brazilian Portuguese, holding only what Brazil words
  differently), Romanian, Russian, Spanish, Swedish, Thai, Turkish,
  Ukrainian and Vietnamese. The words the standard and base sets already
  have (Settings, Save as, Clipboard, ...) are taken from them. The
  settings page *Saving* is `TrC("settings page", "Saving")`: the standard
  set's "Saving" is the progress word ("Wird gespeichert"), not a page's
  name. The delay list is as wide as its longest item in the language
  shown, and the status line says *saved* rather than *saved as <file>*
  (the whole path is its tooltip), so it fits the bar in every language.
- **The picture type's Settings dialog matches the settings window.**
  *Pictures > File type > Settings* opened the type's settings (JPEG
  quality, Progressive ...) in larger type than the window it came from,
  wider than its controls, with a cramped Defaults button. It now opens in
  the settings window's own 10-point type, sized to its content.

#### 2026-11-01 *0.1.0*
- **UltraSnip, the screenshot and screen-recording tool of ULTRA OS.** New
  takes a *Rectangle* you drag out over the frozen screen, a *Window* you
  click (the one under the pointer is outlined as you move), or the *Full
  screen*, after *No delay* or 3, 5 or 10 seconds; the window hides itself
  meanwhile, so it is never in the picture. A *Picture* goes to the
  clipboard and to `Pictures/Screenshots/Screenshot <date> <time>.png` as it
  is taken (both can be switched off), and shows in the window with *Save
  as* (every picture type this build writes, through the framework's Save
  dialog and its type settings), *Copy*, *Open folder* and *Edit in
  UltraPaint*. A *Video* records the chosen area to
  `Videos/Screen Recordings/Screen recording <date> <time>.mp4` at 10, 15,
  20 or 30 frames a second through VideoFX's frame recorder, with a small
  bar beside the area - the elapsed time, *Pause* / *Resume* (the pause is
  left out of the recording) and *Stop* - placed outside the area where
  there is room; the recording then plays in the window, with *Save as* and
  *Open folder*. No sound yet, and the pointer is not in the recording.
  `UltraSnip --rectangle | --window | --screen [--video] [--delay N]` takes
  that snip at once; a second start hands its request to the running copy,
  which is how UltraDesktop's screenshot button and `Super+Shift+S` reach
  it. The shape, kind, delay, options and the Save-as folder are kept
  between starts.
- **Settings, laid out like UltraFiler's.** The *Settings* button opens a
  window with the sections on the left and the page on the right: *Pictures
  > File type* - the type a picture is written as, PNG, JPEG XL, AVIF, HEIC,
  WebP or JPEG first (each greyed when this build cannot write it) and every
  other type this build writes under *Another type*, with the type's own
  quality settings (the framework's, shared with the other applications);
  *Pictures > Saving* - the automatic save, the clipboard copy and the
  folder; *Videos > Recording* - MP4 / Matroska / WebM, the frame rate from
  5 to 60 and the folder; *Behaviour > After a snip* - whether the window
  appears after a snip taken from the desktop, or the picture is saved and
  copied with nothing shown. Every change applies at once; each page has
  *Restore defaults*.
