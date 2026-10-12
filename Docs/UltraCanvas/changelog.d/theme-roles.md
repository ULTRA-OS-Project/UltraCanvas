- **`UltraCanvasTheme` 1.0.0: the framework's colour roles.** One object a
  window's light or dark look lives in - surfaces (`ground`, `panel`,
  `well`, `raised`, `hover`, `pressed`), lines, ink, the accent, focus and
  danger - with two presets (`Light` from the colours the elements default
  to, `Dark` from the greys the dark menu, tooltip and text area use), a
  current theme (`SetCurrent`, `Current`), and named element roles
  (`SetRole`, `Role`, `HasRole`) for colours an element has beyond the
  shared ones. `VideoTimelineStyle::FromTheme` and
  `TimecodeFieldStyle::FromTheme` build the two elements' styles from it:
  the shared roles for surfaces, ink, accent, focus and danger, the
  timeline's own `video-timeline.<role>` colours where the theme names them
  and its light or dark palette where it does not. UltraVideo maps its
  design tokens onto the theme (`ThemeFor`) and sets it as the current one,
  so nothing is hard-coded in the element.
  `Docs/UltraCanvas/UltraCanvasTheme.md`; `Tests/UltraCanvasThemeTest.cpp`.
- **`UltraCanvasTheme` 1.1.0: the elements follow it.** `FromTheme` on every
  style struct with colours - `ButtonStyle`, `LabelStyle`, `DropdownStyle`,
  `SliderVisualStyle`, `SwitchVisualStyle`, `TextInputStyle`,
  `ToolbarAppearance`, `SplitPaneStyle`, `AlbumConfig`, `MenuStyle`,
  `TooltipStyle` - puts the roles over a base style whose shape (sizes,
  fonts, flags) stays; the menu and the tooltip start from their `Dark()`
  or `Light()` preset by the theme's `dark` flag, so a dark menu or tooltip
  agrees with the rest of a dark window. `UltraCanvasUIElement` gained
  `SetFollowTheme` / `GetFollowTheme`, `ApplyTheme` and `OnThemeChanged`:
  an element that follows styles itself from the current theme at once
  and again on every change, over its own style (a radius or a font set
  before survives a switch); the tabbed container and the text area
  (its light or dark token colours, the surfaces and ink from the roles)
  override `ApplyTheme` directly; a container passes a change on to its
  children. Off by default: an element styled by hand keeps its look.
  `UltraCanvasTheme::AddChangeHandler` / `RemoveChangeHandler` are called
  after `SetCurrent`; the application installs one in `Initialize` that
  walks every window and sets `UltraCanvasTooltipManager::SetStyle` (now
  static, the style every tooltip shown without one takes; `GetStyle`)
  from the theme, so an application switches its look with one call.
  UltraVideo's split panes, tabs, fields, albums, toolbars, dropdowns and
  context menu follow the theme; its own looks stay in `ApplyPalette`.
  The slider's handle (`slider.handle`, `slider.handle-border`) and the
  switch's thumb (`switch.thumb`, `switch.thumb-border`) are element
  roles, the raised surface and the control border where a theme does not
  name them, so UltraVideo's ink handle and on-accent thumb follow the
  theme as well. `Tests/UltraCanvasThemeTest.cpp`: 45 checks.
