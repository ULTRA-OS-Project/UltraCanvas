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
  so nothing is hard-coded in the element. Other elements keep their own
  presets until each gains a `FromTheme`; nothing changes for them.
  `Docs/UltraCanvas/UltraCanvasTheme.md`; `Tests/UltraCanvasThemeTest.cpp`.
