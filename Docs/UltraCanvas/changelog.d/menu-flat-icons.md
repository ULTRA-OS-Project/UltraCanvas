- **`MenuStyle::tintIcons`: a menu's icons drawn in the label's colour.**
  On, each item's icon is drawn as a mask in its label's colour - the text
  colour, the highlighted row's, a disabled item's grey - so one flat,
  single-colour icon reads on a light menu and a dark one. `MenuStyle::Dark()`
  turns it on, since an icon drawn for a light menu vanishes on it; the other
  presets still draw an icon in its own colours.
- **The DemoApp's menus show flat icons instead of emoji.** Every entry of the
  Menus page (the context menu, File / Edit / View / Help, the dark and flat
  menus, the list's right-click menu) and the Button page's split-button menu
  wrote an emoji into its label - 📋 ✂️ 💾 🔍 🌙 and the rest - which draws only
  where a colour emoji font is installed; with the bundled fonts alone, as on
  Windows here, each was a box of hex digits. They now carry icons of
  `media/icons/menu/`, the set UltraFiler's menus use, and the split buttons'
  "▼" arrows are the dropdown icon.
- **Nineteen new icons in `media/icons/menu/`**, drawn like the rest of the set
  (24 px grid, 2 px rounded strokes): save, recent, exit, undo, redo, search,
  replace, format, toolbar, status-bar, sidebar, book, tutorial, moon, sun,
  palette, phone, tablet and monitor.
- `Docs/UltraCanvas/UltraCanvasMenuExamples.md` documents `tintIcons`, and its
  context-menu example uses icons instead of emoji labels.
