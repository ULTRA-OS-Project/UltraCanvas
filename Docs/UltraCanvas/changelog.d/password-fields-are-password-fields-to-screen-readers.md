- **Screen readers are told a password field is one.** The Windows bridge
  answered UI Automation's `IsPassword` with a constant false, and
  `UltraCanvasTextInput` described nothing about itself, so assistive
  technology treated every password field - UltraPassword's master password
  and entry fields among them - as an unknown element, and a screen reader
  would speak each character typed. `UltraCanvasUIElement` gains
  `IsAccessiblePassword()`; a text input reports itself as a `TextField`, and
  in password mode (revealed or not) as a password. UI Automation gets
  `IsPassword` from it, and AT-SPI the *password text* role (Orca then says
  "password" and speaks no characters). The field's text is still not exposed
  through either bridge. New headless `TextInputAccessibilityTest`;
  `AtspiBridgeTest` now has a password field and checks that a libatspi client
  sees password text with nothing to read. `UltraCanvasAccessibility.md` and
  `UltraCanvasTextInputExamples.md` describe it.
