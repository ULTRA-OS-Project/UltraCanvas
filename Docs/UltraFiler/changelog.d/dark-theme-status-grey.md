- **The status line's grey half reads under a dark theme.** The text after
  the bold part ("3 drives, 424.5 GB free of 1.3 TB in total · 3 items",
  a folder's counts, a History page's) was a fixed `#6b6b73`, which almost
  vanished on the dark status strip. It now comes from the theme
  (`FilerLook::statusQuiet`): the same `#6b6b73` under the standard light
  theme, the theme's faint text colour under Dark and the high-contrast
  themes. The line is written again when the theme changes while the filer
  runs. The scroll bars, the hidden-items notice's gear and the copy / move
  dialogs' list caption and note follow the theme too, through the
  framework's file display.
- **The right-hand display of the split view is dark under a dark theme.**
  It was built at start-up, still under the light look, and stays out of
  the window until the split view is turned on - and a theme change only
  reaches what is in a window, so it kept the light colours. The right-hand
  pane now takes the current theme each time it joins the window, and the
  preview's viewers do the same when the preview pane is put up again. The
  folder preview (a folder selected with the preview on) follows the theme
  too: its own sizes set by hand had switched the following off.
