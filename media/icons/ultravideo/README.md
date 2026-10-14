# UltraVideo icons

The interface icon set the screens in `Apps/UltraVideo/screens/` use: 46 icons on a
16 px grid, drawn as one path each with a 1.5 px stroke, round caps and
joins, no fill, in `currentColor`, so each can become a Cairo path or an
SVG for UltraCanvas. The names are the ones the component specifications
and `../UltraVideoTokens.h` refer to (`magnet`, `snap`, `beat`, `blade`,
`slip`, `transition`, ...). UltraVideo loads them from here
(`Apps/UltraVideo/UltraVideoTheme.h`, `IconPath`). `logo.svg` is the
application mark, not the app icon: the packaged icon is
`media/appicon/UltraVideo.{png,svg}`.

The brand book (`../screens/BrandBook.md`, *Iconography*) notes that this
set was drawn for the design as a starting point and may be replaced by
one consistent open set before release, keeping the same names.
