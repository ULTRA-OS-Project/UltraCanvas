- **Eleven charts and diagrams pass a layout change on to their parent.**
  The chord, circular progress, polar, radial bar and timeline charts and
  the fishbone, matrix, parliament, SWOT, timeline and word-cloud diagrams
  each declared a private `InvalidateLayout()` that only cleared their own
  layout cache. It had the name of the layout engine's virtual
  (`CSSLayout::Element::InvalidateLayout`), so it overrode it without
  calling it: whenever the framework invalidated one of these elements - a
  new size, `SetVisible`, a style change - the element's measure stayed
  valid and the change never reached its parent. Clang warned about it as
  a missing `override`. The cache-clearing helper is `DropLayoutCache()`
  now, used where the charts called it themselves, and `InvalidateLayout()`
  is a real override that drops the cache and calls the engine's.
  `ChartElementBehaviourTest` checks all eleven; each fails without it.
- `UltraCanvasSyntaxTokenizer.h` writes Dart's `??=` operator as `"?\?="`,
  so the string no longer reads as a trigraph and Clang stops warning in
  every file that includes the header.
