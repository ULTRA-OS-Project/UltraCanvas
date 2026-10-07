- **`MenuStyle::enableAnimations` animates.** `UltraCanvasMenu` computed an
  opening progress in `UpdateAnimation()` that nothing drew, so the setting
  had no visible effect. A popup menu or submenu now fades its entries in over
  `animationDuration` when it opens, painting them as one group at the
  progress (`IRenderContext::BeginGroup` / `EndGroup(opacity)`) and asking for
  repaints on a timer until the fade is complete. The panel itself appears at
  once: popups are composited onto the window as opaque blocks, so there is
  nothing beneath it to fade over. Animations stay off by default.
- **`MenuItemData::Input()` is gone.** Both overloads were declared with their
  definitions commented out, so a call compiled and failed to link. Nothing
  called them; `MenuItemType::Input` stays, reserved. Escape was checked as
  well: it closes an open menu through the popup system (the application
  closes the topmost popup when `closeByEscapeKey` is set), so the
  commented-out Escape case in `HandleKeyDown` is replaced by a note saying so.
- **The tabbed container's overflow button honours the search settings.**
  `SetDropdownSearchEnabled()` and `SetDropdownSearchThreshold()` were stored
  and never read: the button always opened the "Search tabs..." popup. It now
  opens the search popup only when search is enabled and the visible tabs
  reach the threshold, and otherwise a plain menu of the tabs (the active one
  checked, disabled ones greyed out). `UsesDropdownSearch()` says which.
  Turning search off on a container that is in no window yet - which
  `CreateTabbedContainerWithDropdown(..., false, ...)` does - called
  `ClosePopup` through the null window pointer (undefined behaviour that
  happened not to crash); `HideSearchAutoComplete` checks the window now.
- **`UltraCanvasTextArea::SetCursorPosition(pos, true)` selects.** The
  `selecting` flag was ignored; it now extends the selection from its anchor
  (the start of the current selection, or the caret's old place), as
  Shift+arrow does. `GetCursorPosition()` is `const`.
- **Activating a menu item before the application exists no longer
  crashes.** `UltraCanvasMenu` posted the `MenuClick` event through
  `UltraCanvasApplication::GetInstance()` without checking it, so choosing an
  item in a test or a tool with no application ran the item and then called
  through a null pointer. It runs the item and posts nothing. The
  commented-out `onTextInput` slot in `UltraCanvasMenuRegistry` is gone too.
- `Tests/MenuAndTabBehaviourTest.cpp` covers all of the above headless, with
  popups opened in a window stand-in and the menu drawn offscreen.
