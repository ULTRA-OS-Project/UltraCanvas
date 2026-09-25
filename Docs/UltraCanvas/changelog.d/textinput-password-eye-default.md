- **Password fields show the eye button by default.** `UltraCanvasTextInput`
  (1.6.0) now starts with `showPasswordToggle = true`, and so does
  `TextInputBuilder`: every field in password mode — `CreatePasswordInput()`,
  `SetInputType(TextInputType::Password)` and the framework's own password
  input dialog — carries the in-field "view hidden text" button, where before
  each caller had to ask for it and most did not. A masked field with no way
  to read it back turned every typo into a blind retry. Plain fields are
  unaffected, since the button is only painted in password mode; a field that
  must never show its text still calls `SetShowPasswordToggle(false)`.
  `CreateRevealablePasswordInput()` is kept and is now the same as
  `CreatePasswordInput()`.
