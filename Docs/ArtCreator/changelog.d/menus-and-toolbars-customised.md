- **Customise the menus and toolbars.** Every command is registered once
  (`ArtCreatorCommands.cpp`) and the menu bar, the toolbar, the tool palette
  and the keyboard are built from it, so a command has one label, icon,
  shortcut and state everywhere. Undo / Redo name their step and are greyed
  with nothing to undo - in the menu too, which never was - and what needs a
  selection is greyed without one.
- **Right-click the toolbar or the palette** to show or hide its groups -
  the toolbar offers Edit, Align, Distribute, Combine, Layer, Line, Guides
  and Snap besides the six it shows - to fold a group into one button, or to
  hide a command; View > Toolbars offers the same. **View > Customise Menus
  and Toolbars** edits the menu bar, the toolbar and the palette in one
  editor. The arrangement is kept in the settings folder (`menus.txt`,
  `settings.json`). A toolbar wider than the window puts what does not fit
  into a chevron's menu.
- **24 more toolbar icons**, drawn on the 24 px grid: Save As, Export,
  Document Setup, New Icon, Quit, About, Rulers, Guides, Clear Guides, the
  four snaps and Snap to Pixels, Show / Hide Layer, Remove ClipView, and the
  line widths. A command's icon on the toolbar was its 16 px menu drawing
  scaled up, which blurs.
- A version that adds a menu command shows it to a user who customised the
  menus too: `menus.txt` records ArtCreator's commands and is merged with the
  next version's menus.
