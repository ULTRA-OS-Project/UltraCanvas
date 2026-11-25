- **Windows: a menu stayed open where it was when its window was moved.** A
  menu opened from the menu bar (or a dropdown's list, any popup) is shown in
  a top-level window of its own, and the Windows layer never handled
  `WM_MOVE`, so neither that window nor the framework learned that the
  window it belonged to had been dragged away. A press on the frame - the
  title bar, a border - is now a click outside the popups
  (`UltraCanvasWindowBase::ClosePopupsOnFramePress`, on `WM_NCLBUTTONDOWN`
  and its right / middle / X siblings and `WM_ENTERSIZEMOVE`), so the menu
  closes before the window moves, as a native menu does. A popup that does
  not close on a click outside follows the window instead, live during the
  drag (`MovePopupHostsWithWindow` on `WM_MOVE`, since Windows' move loop
  draws no frame until the button is released). `GetConfig().x / y` and
  `onWindowMove` follow a move on Windows too, which they never did.
