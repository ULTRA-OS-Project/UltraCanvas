- **A flex, grid or table setter no longer un-hides an element.** Every
  `CSSLayout::Layout` setter of that kind (`SetFlexGap`, `SetFlexRow`,
  `SetFlexAlignItems`, `SetGridGap`, `SetTable`, ...) set `display` outright,
  so calling one on an element hidden with `SetVisible(false)` / `Hide()`
  showed it again. `UltraCanvasToolbar::SetAppearance` sets the bar's gap, so
  restyling a hidden toolbar - as an application does on every theme change -
  brought it back: UltraTexter's Markdown bar appeared beside plain text. On a
  hidden box the setter now changes the kind of box it is shown again as
  (what `Show()` restores), as in CSS, where `gap` never undoes
  `display: none`. New `CSSLayoutHiddenSetterTest`.
