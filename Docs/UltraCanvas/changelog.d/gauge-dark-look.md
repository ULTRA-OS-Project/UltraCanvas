- **`UltraCanvasGaugeDiagramElement` follows the theme.** It drew its value
  text, captions, ticks, unlit segments and faces in fixed light-look greys,
  so under a dark theme the value in the middle of a ring was dark grey on a
  dark card. Under a dark theme those greys and the default text,
  background, track, ring border and clock colours now take the dark look;
  a colour set with its setter is drawn as given, and the value colours
  (gauge colour, ranges, warnings) are kept. The speedometer, whose face is
  dark in both looks, draws its value light unless the application chose a
  text colour.
- **`UltraCanvasTheme::ForDarkLook` / `ForThisLook`**: a colour chosen for
  the light look, its lightness turned over into the dark look's range with
  its hue kept - what the gauge draws its greys through, and the demo app's
  `DemoKit::Themed` now calls.
