- **`GetTextIndexForXY` no longer reads a freed font name.** With no font
  family set (the system-default font), the Cairo backend's
  `RenderContextCairo::CreatePangoFont` resolved the default family into a
  string local to an `if` block and handed Pango a pointer to it after the
  block had ended, so `pango_font_description_set_family` read stack memory
  that was already out of scope (AddressSanitizer: stack-use-after-scope).
  Short names such as "Sans" usually survived on the stack by luck; when they
  did not, the hit-test measured text in an arbitrary font. The name now lives
  until Pango has copied it, as it already did in `UCTextLayout`. Nothing in
  this repository calls `GetTextIndexForXY` today, so only callers outside it
  were affected.
