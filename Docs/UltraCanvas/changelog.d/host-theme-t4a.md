- **Every element follows the theme by default (T4 of the host theme
  proposal, first part).** Under the theme policy (`FollowHost`, the
  default, `Light` or `Dark`) `Initialize` turns on
  `UltraCanvasUIElement::SetFollowThemeByDefault`, and every element with an
  `ApplyTheme` - the button, label, dropdown, slider, switch, text input,
  text area, toolbar, split pane, tabbed container, text format bar, menu,
  album, modal and file dialog, timecode field, video timeline - takes the
  current theme at the end of its constructor and again on every change,
  so a window built after start-up is light or dark with the desktop
  without the application opting in element by element. A style set by
  hand (`SetStyle`, `SetConfig`, `SetAppearance`, `SetVisualStyle`) stops the
  following for that element, so it keeps its look as before; `SetStyle`
  then `SetFollowTheme(true)` keeps both. The `FromTheme` readers set their
  fonts by role as well as their colours: the body size for the button,
  label, dropdown, text input, menu and switch, the heading size for a
  heading label, the caption size for the tooltip, a slider's value, a
  dialog's details and the tab titles, the theme's families alone for the
  album, the timecode field and the timeline. `UltraCanvasThemeTest`
  covers the default, the rule and the fonts.
