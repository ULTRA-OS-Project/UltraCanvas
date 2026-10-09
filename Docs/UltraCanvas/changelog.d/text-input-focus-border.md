- **A focused `UltraCanvasTextInput` shows its focus border again.**
  `Render` computed the frame colour from the state and then drew
  `style.borderColor` regardless, so `focusBorderColor` never appeared: a
  field with the keyboard looked like every other one, unlike the dropdown,
  spinner, pickers and chip, and the focus colours UltraFiler's rename field
  and UltraDesktop's clipboard search set had no effect. The frame now takes
  the disabled border, `focusBorderColor` while focused, or `borderColor`;
  validation still draws its own coloured border over it. The unused
  private `GetBorderColor()` is gone.
