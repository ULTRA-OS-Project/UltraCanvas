- **Black text on the blue buttons and selections, hard to read in every
  app.** `UltraCanvasTheme::DeriveAccent` (the host theme every app follows)
  picked white or black by WCAG 2 contrast alone; on the Windows accent blue
  `0,120,215` white measures 4.499:1 - a hair under 4.5 - and black 4.67:1,
  so primary buttons (the file dialog's *Open*), selected tree / list rows,
  segmented controls and every other `onAccent` text turned black on dark
  blue. Text on the accent is now white on any accent white already reaches
  3:1 on (blues, reds, purples, oranges, mid greens), the accent darkened a
  few percent until white reaches 4.5:1 (7:1 high contrast) - that blue
  becomes `0,113,202`; the dark ink is kept only for light accents (yellow,
  light cyan, pastels). `UltraCanvasThemeTest` checks the Windows blue on
  Light and Dark and the mid-tone accents.
