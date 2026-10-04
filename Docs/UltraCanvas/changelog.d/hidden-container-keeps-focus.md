- **Hiding a container now takes the keyboard focus from the fields inside
  it.** `UltraCanvasUIElement::SetVisible(false)` dropped the focus only when
  the hidden element itself held it, so hiding a panel left a text field in it
  focused: keys still went to the invisible field and its caret blinked where
  it had been. It now clears the window's focus when the focused element is
  the hidden element or anywhere inside it (together with
  `caret-left-by-hidden-input.md`, the caret goes too). UltraClaude's
  sign-in page drops its own workaround for this.
