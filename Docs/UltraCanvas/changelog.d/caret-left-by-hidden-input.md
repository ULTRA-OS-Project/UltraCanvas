- **Hiding or disabling a focused text field left its caret blinking where
  the field had been.** `UltraCanvasTextInput::OnEvent` and
  `UltraCanvasTextArea::OnEvent` returned early for a hidden or disabled
  element - before the FocusLost case that releases the shared caret - and
  `SetVisible(false)` hides the element first and only then drops its focus,
  so the release never ran. FocusLost now always gets through. Found with
  UltraClaude's login-code box, which is hidden when sign-in ends.
