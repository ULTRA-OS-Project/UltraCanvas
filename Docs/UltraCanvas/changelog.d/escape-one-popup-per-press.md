- **One Escape closed two menus.** With a submenu open (or any popup over
  another: a styles list beside a font list, a palette editor over the
  palette manager), one Escape closed the submenu and then its menu: the
  window closed the topmost popup on every keyboard event carrying the key -
  its press and its release. A press closes one popup now and its release
  closes nothing, on every platform (the backends each send one press and one
  release; the check was the window's). The release of the press that closed
  the last popup is taken too, so a window that closes on an Escape's release
  (the settings pages, the image viewer) no longer closes with the dropdown
  that was open in it. The logic moved to the window:
  `UltraCanvasWindowBase::HandlePopupEscapeKey`, which
  `UltraCanvasApplication::DispatchEvent` hands every key first.
  `UltraCanvasFontPicker` handled Escape itself to get round this; that is
  gone, its lists close on Escape like every popup (the styles list first,
  then the list). Tests: `MenuAndTabBehaviourTest` (a menu with its submenu
  open, one Escape press and release), `FontPickerElementTest` (Escape
  through the application).
