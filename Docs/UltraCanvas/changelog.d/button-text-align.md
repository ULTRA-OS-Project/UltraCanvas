- **`UltraCanvasButton::SetTextAlign` is honoured.** The button drew its
  text centred whatever `SetTextAlign` had been given - `style.textAlign`
  was set and never read - so a list of flat, left-aligned buttons (the
  front page demo's FAQ questions, UltraPress's page and block lists) came
  out ragged, short labels centred and long ones flush left. `DrawText` now
  aligns as the style says; buttons that never call it are unchanged.
