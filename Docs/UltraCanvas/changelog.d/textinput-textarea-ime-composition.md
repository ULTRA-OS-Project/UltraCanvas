- **Text fields and text areas show what an input method is composing where
  it will go.** `UltraCanvasTextInput` and `UltraCanvasTextArea` now draw an
  input method's pre-edit text (Japanese, Chinese, Korean, ...) inline at the
  caret, underlined, with the caret inside it, as `UltraCanvasRichTextEdit`
  already did; before, the text being composed appeared in the input method's
  own floating window, away from the text it was meant for. The single-line
  field scrolls to keep the composition's caret in view and the text area lays
  it out in the caret's line. The composition is never part of the text:
  `GetText()`, `onTextChanged`, validation and the undo stack see only the
  committed result, which arrives as typing; an empty composition, losing the
  focus or turning the widget read-only ends it, and composing over a
  selection replaces the selection as typing would. `GetCompositionText()`
  returns it. Read-only fields and areas, display-only and hex-mode areas and
  password fields keep the input method's own window - a password field
  because a masked composition cannot be read and an unmasked one would show
  the secret.
