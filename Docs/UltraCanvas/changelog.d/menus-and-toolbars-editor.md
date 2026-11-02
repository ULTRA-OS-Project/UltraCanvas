- **One editor for a program's menus and toolbars.** The menu editor became
  `UltraCanvasCommandConfigWidget`: its tree holds the program's menus and
  its toolbars, each toolbar's groups and their commands with a check box
  each - shown or not. Add, Remove, Move Up / Down work on both; New Group
  makes a submenu in a menu or a group of the user's on a toolbar; Fold folds
  a toolbar group into one button; the name field renames a submenu or a
  group of the user's; icon size and labels are the selected toolbar's; a
  filter narrows the command catalogue; a preview shows the selected toolbar
  as edited. Remove hides what is the program's on a toolbar rather than
  dropping it. `ShowCommandConfigDialog` opens the editor in a window of its
  own. `onApply` / `onSave` carry a `CommandLayoutSet` (menus and toolbars);
  `UltraCanvasMenuConfigWidget` and `CreateMenuConfigWidget` remain as
  aliases. Phase 2 of the toolbar configuration investigation; doc
  `Docs/UltraCanvas/UltraCanvasCommandConfigWidget.md`.
- **The command registry is `UltraCanvasCommandRegistry`.** It builds
  toolbars as well as menus; `UltraCanvasMenuRegistry` (and its header) is an
  alias. A program with several windows gives each its own registry.
- **A toolbar's right-click menu.** `UltraCanvasToolbar::SetOnContextMenu`
  reports a right click with the item under the pointer;
  `InstallToolbarContextMenu` gives a toolbar built from a `ToolbarLayout`
  the standard menu: a check per group, Fold / Unfold and Hide for the group
  and command under the pointer, and "Customise Toolbars".
- **A program's toolbar group keeps the user's order and additions** across
  saving and loading (`SaveToolbarLayout` writes its commands when they
  differ from the program's); a command a later release adds still appears,
  after the one it follows in the defaults.
- The DemoApp's "Menus and Toolbars" page (was "Menu Configurator") edits
  the Filer model's context menu and toolbar in one editor.
