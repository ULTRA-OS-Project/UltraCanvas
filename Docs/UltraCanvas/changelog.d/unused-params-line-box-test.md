- Tests: `TextInputFontTest` checks `IRenderContext::GetLineBoxHeight`
  headless: it is the font's line height unrounded, `GetTextLineHeight` is
  the same height in whole pixels and never more, a line's descenders end
  inside it, and a second ask comes from the cache.
- The public headers' inline default bodies mark their unused parameters
  `(void)`, as `UltraCanvasRenderContext.h` already did in places: a build
  with `-Wextra` (the Models plugin, the tests, Texter, AnchorPoint) saw 48
  `-Wunused-parameter` warnings from 17 headers - the render context's on
  every file - and now sees none. Signatures and parameter names are
  unchanged.
