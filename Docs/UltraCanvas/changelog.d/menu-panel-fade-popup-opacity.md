- **A menu fades in as a whole.** With `MenuStyle::enableAnimations` a popup
  menu or submenu faded only its entries in; the panel - background, border,
  shadow - appeared at once. It now fades in whole over `animationDuration`:
  the menu starts its popup at opacity 0 and steps it to 1 on a timer
  (`UltraCanvasWindowBase::SetPopupOpacity`, below). A step only composites
  the window again; the menu is not repainted. Animations stay off by
  default.
- **A popup can be shown at an opacity.** `UltraCanvasWindowBase::SetPopupOpacity()`
  / `GetPopupOpacity()`, with the value kept on the window's `PopupElement`.
  Each popup draws into a surface of its own, and the window copied that
  surface over its content when compositing, so a popup could only be fully
  there or not at all. Below 1 the window now mixes the popup with the
  content and the popups already composited beneath it
  (`IRenderContext::FlushToSurfaceWithOpacity`, new: each pixel moves from
  the destination towards the popup's by the opacity). It works for any
  popup opened with `OpenPopup()` - dropdown and autocomplete lists, date and
  time pickers, menus - and a popup takes input at any opacity. Popups open
  at 1, which is the copy exactly as before, so nothing changes for a popup
  that does not ask. A render context that cannot blend draws the popup
  opaque. A caret inside a popup below full opacity is redrawn with a full
  composite, not restored from the popup's surface alone.
- `Tests/MenuAndTabBehaviourTest.cpp` reads the opacity and the fade off a
  window stand-in whose screen is an offscreen surface, so
  `UpdateAndRender()` composites onto it as onto a real window.
