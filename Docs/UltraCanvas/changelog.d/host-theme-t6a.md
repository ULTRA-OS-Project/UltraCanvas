- **`UltraCanvasTheme::TakeHostFonts`, and the four applications with a
  theme of their own on the policy (T6 of the host theme proposal, first
  part).** `TakeHostFonts(host)` puts the host's interface and monospace
  fonts and its text scale onto a theme, as `FromHost` does (which now goes
  through it), so an `Own` provider's colours come with the desktop's
  fonts. UltraVideo, UltraPress, ChatCube and UltraDBAdmin set
  `ThemePolicy::Own` with their `ThemeFor` / `AppTheme` as the provider
  instead of `SetCurrent` at start: a change of the desktop reaches them;
  UltraPress and ChatCube gain `system` as their look setting's default
  (the desktop's look), UltraVideo's palette follows the desktop until the
  eye picks the project's. `UltraCanvasThemeTest` covers `TakeHostFonts`;
  `UltraCanvasTheme.md` 1.15.0.
