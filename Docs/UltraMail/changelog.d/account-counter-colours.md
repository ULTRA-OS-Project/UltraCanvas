- **The account tiles' counters lost their coloured backgrounds.** Since the
  theme work the theme's colours are functions that return a colour, and the
  tile's `CounterTint` / `CounterText` handed back a reference to that
  temporary, which was gone before the pill used it. They return the colour
  now: blue, green and orange pills again.
