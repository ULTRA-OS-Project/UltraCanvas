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
