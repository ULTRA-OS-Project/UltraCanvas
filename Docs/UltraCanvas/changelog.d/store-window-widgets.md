- **Four elements for application windows: `UltraCanvasChipGroup`,
  `UltraCanvasCard`, `UltraCanvasCollapsiblePanel`, `UltraCanvasSidebarNavigation`.**
  The store window needed them and the framework had none; UltraPassword had
  composed a card from containers, and the next application would have
  composed it again. The chip group lays real chips out in a wrapping row
  and decides what a click means for the others (single or multiple
  selection, `onSelectionChanged`). The card is a raised surface with a
  header (icon, title, subtitle, a trailing element), a content area and a
  row of real buttons at the foot (`AddActionButton`); the surface is
  clickable, hoverable, highlighted or selected. The collapsible panel folds
  to its header strip (chevron, title, detail text, header buttons), is as
  tall as its body or an expanded height, and resizes by its top edge when
  docked at the bottom. The sidebar navigation is sections of items with
  icon, label and badge, one selected, with Up / Down / Home / End / Return,
  wheel scrolling and a compact icons-only mode with the labels as tooltips.
  Each has a component doc and a row in the element catalogue; the headless
  `UIWidgetsTest` checks their rules and geometry.
