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
