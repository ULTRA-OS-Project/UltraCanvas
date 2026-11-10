- **The list elements follow the theme (T4 of the host theme proposal,
  third part).** `FromTheme` on `ScrollbarStyle`, `ListViewStyle`,
  `TreeColumnStyle` and `FilerStyle`, and `ApplyTheme` on the scrollbar, the
  list view (its `UltraCanvasDefaultListDelegate` taking ink, text on the
  accent and the body font with it), the tree view (the roles onto its
  colours, its floating buttons and its scrollbar; it has no style struct, so
  a colour set by hand does not stop the following), the columns tree view
  (constructors of its own, so a view that follows by default is themed
  whole) and the filer widget: the roles mapped onto their colours and the
  interface font at the body size (a list's header and the filer's small
  text at the caption size) over the struct's own shape. Element roles with
  their fallbacks: `scrollbar.track` and `scrollbar.thumb` (the panel and the
  control border), `columns-tree.group-header` / `columns-tree.on-group-header`
  (the black bar), `filer.icon-menu`, `filer.on-icon-menu`,
  `filer.icon-menu-done`, `filer.extension-bar`, `filer.extension-tag` and
  `filer.on-extension-tag` (the dark overlays). The bars an element owns take
  their owner's style, so they follow through the owner.
  `UltraCanvasThemeTest` covers every mapping and the following by default.
