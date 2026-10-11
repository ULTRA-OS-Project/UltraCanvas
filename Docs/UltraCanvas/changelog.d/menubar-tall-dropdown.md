- **A menu bar's dropdown taller than the room below it opens and stays
  open.** `UltraCanvasMenu::GetPositionSubmenu` moved a dropdown that ran
  past the window's bottom up until it fitted, to the window's top at most.
  One taller than the window was put over the menu bar, under the pointer.
  The release of the click that opened it then landed on one of its rows, ran
  that command and closed the menu, so ArtCreator's Arrange menu (about 40
  rows) never stayed open. A menu bar's dropdown now stays under its bar,
  and `ClampMenuToWindow` gives it a scrollbar (or flips it above the bar
  when it fits there). A cascading submenu still moves up to fit. Tested in
  `MenuAndTabBehaviourTest`.
