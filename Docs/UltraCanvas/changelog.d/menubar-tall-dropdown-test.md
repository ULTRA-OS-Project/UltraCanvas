- **`MenuAndTabBehaviourTest` checks a menu bar drop-down taller than the
  window.** It opens under its bar and scrolls, off the pointer; a cascading
  submenu that tall moves up to the window's top, beside its parent. Before
  menu bar drop-downs were anchored to their item (0.9.249), such a
  drop-down was moved up over the bar. The release of the click that opened
  it then picked a row and closed the menu, so ArtCreator's Arrange menu
  (about 40 rows) never stayed open.
