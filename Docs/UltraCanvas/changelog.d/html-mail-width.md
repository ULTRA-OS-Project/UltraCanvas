- **HTML mail no longer runs off the right of the pane, and its centred menus
  are on screen.** A picture sized in % of its column (`width:100%`, the
  image of every mail-template newsletter) reported its natural width as the
  narrowest it could be, so a 2000px photo widened the 600px table around it
  to 2000px. Everything in that table then sat in a box far wider than the
  pane: text ran off the right edge, and a centred row - Kickstarter's ART /
  COMICS / DESIGN … menu - was laid out off screen altogether.
  `UltraCanvasImageElement` now reports no min-content width when its width
  or max-width is a percentage (CSS Sizing 3 §5.2.2, compressible replaced
  elements), so the table keeps its own width and the picture shrinks into
  it. Test: `HTMLImageAlignTest` ("width:100% picture in a 600px mail table").
- **A later `width` replaces an earlier one.** The resolver kept a px and a %
  width side by side, and the px one won: a newsletter's narrow-screen rule
  `.row-content{width:100%!important}` over the table's inline `width:600px`
  left it 600px wide in a narrow pane. `width: 100%`, `width: 300px` and
  `width: auto` now each replace what came before. Test: `HTMLReaderTest`
  (`TestImportantWidthReplacesInlineWidth`).
