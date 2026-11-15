- **`FileDialogTest` passes on a display again.** Its check that the
  save-settings dialog opened from a settings window has shorter rows assumed
  the window's type is smaller than the dialog's own 12 - true while
  `SettingsPageKit` drew at a constant 10, but since the kit follows the
  theme (0.9.541) its `TextFontSize()` is the desktop's body size, 12 where
  no desktop names a font, as under Xvfb, and the dialog then rightly draws
  its 28-pixel rows. The dialog's scaling was never at fault (21, 23 and 26
  pixels at 9, 10 and 11 points). The test now checks that a settings window
  opens the dialog at its own type size, and measures the shorter rows and
  narrower window at 10 points against the dialog at its own size, so it
  holds at any desktop's size (run at 10, 12 and 14). The file dialog's
  documentation and the loader's header no longer name the old
  `kTextFontSize`.
