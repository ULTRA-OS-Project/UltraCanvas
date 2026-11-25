- **A window already open on another virtual desktop comes to the one you
  are on (Linux, X11).** With UOS-Settings, UltraClipboard, UltraSnip or
  UltraToDo open on desktop 1, *Desktop settings...* and the tool buttons did
  nothing on desktop 2: the running application asked the window manager to
  activate its window, and the manager only marked it "wants attention" on
  the desktop it was on. The window now moves to the current desktop and
  comes to the front there - the desktop's own Applications and Task Manager
  windows too. This is the framework's (`RaiseAndFocus`, see its changelog), so every
  ULTRA OS application started a second time does the same. A program that is
  not ours still decides for itself: some bring their window over, some
  switch to its desktop, some open a second window.
- **The Sticker board's button brings the board over instead of hiding it
  where nobody is looking.** With the board open on another desktop, a click
  on its button closed it there - nothing to see - and a second click opened
  it here. One click now brings it to the current desktop; the button hides
  it only when it is on the desktop you are on. Files dropped on the
  wallpaper bring it over the same way, so they are seen to land.
