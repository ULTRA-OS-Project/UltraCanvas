- **TextArea: the scrollbar's thickness and rounding are styleable.** The
  text area drew its own scrollbars as fixed 15px square bars, so an app could
  not match them to the thin, rounded `ScrollbarStyle::Modern()` bars of the
  list views beside it. `TextAreaStyle` now has `scrollbarWidth` (default 15),
  `scrollbarCornerRadius` (default 0, square) and `scrollbarThumbInset`
  (default 2); drawing, hit-testing, thumb dragging, the text's reserved
  width and the hex view's row width all use them. The defaults draw exactly
  what was drawn before. UltraMail's plain-text reading pane is the first user.
