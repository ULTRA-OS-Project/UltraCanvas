- **Pages and menus are edited from the window.** *+ Add a page…* in the
  side column makes a page - a heading and a text block, dated today, in
  the main menu - and shows it with its title ready to type in the pane;
  a page's row has the arrows and the cross a block's has, so a page is
  moved in the list (the pinned pages stay first), with the header's and
  the footer's menus following the list's order, or removed with every menu entry that named it, at any
  depth (an entry's children move up). The front page stays, since the
  site is addressed by it. The pane shows the page's address and two
  boxes, *The main menu* and *The footer menu*, that put the page into
  that menu or take it out; a title typed there retitles the menu entries
  that carried the old one. Nothing is written until *Save site*. The
  rules are the model's (`UniqueSlug`, `AddPage`, `RenamePage`,
  `MovePage`, `RemovePage`, `PageInMenu`, `SetPageInMenu` in
  `UltraPressModel.h`), so the engine tests pin them and the render tests
  drive the editor through them.
- **`site.json` keeps the pages' order** (`"pages": [...]`, the slugs as
  the list has them), which the page files could not hold: a page moved
  in the window stays moved after a save. A page file the list does not
  name comes after, in file order, as before.
