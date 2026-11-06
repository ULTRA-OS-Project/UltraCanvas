- **The theme follows the desktop (T3 of the host theme proposal).**
  `UltraCanvasTheme` gains the font half of a look - `fonts.ui`,
  `fonts.mono`, the host's text scale, and a size per text role (Body,
  Caption, Small, Heading, Title, Mono) - and two high-contrast presets,
  black on white and white on black with every ink at 7:1.
  `UltraCanvasTheme::FromHost` builds the theme a desktop asks for from the
  host appearance: Light or Dark, the high-contrast variant, the desktop's
  accent derived into the five accent roles with the readability the test
  enforces (text on the accent at 4.5:1, 7:1 for high contrast, the focus
  ring 3:1 on the ground), its fonts. The application's theme policy
  (`SetThemePolicy`: FollowHost by default, Light, Dark, Own with a
  provider) applies it in `Initialize` before the first window and again
  on every host change, so switching the desktop to dark re-styles every
  element that follows the theme and every window built with the default
  background takes the theme's ground; an application that sets its own
  theme is left alone. `UltraCanvasThemeTest` covers the sizes, the
  presets, the derivation over fourteen accents and `FromHost`.
