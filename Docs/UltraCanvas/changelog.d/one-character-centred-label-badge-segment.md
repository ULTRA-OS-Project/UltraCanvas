- **A one-character text is centred in labels, badges and segmented controls
  too**, as on the button. Centred on the font's line, a lone "+", "×" or
  "‹" sat 1.5 to 2 px low - an "×" in a 32 px label, a "+" in a badge, a
  pager's "‹" in a segment, and UltraDesktop's 18 px window-preview close
  mark 1.5 px low - so an application had to nudge it with padding tuned to
  one font and size, or draw an icon instead. The rule is now one helper,
  `DrawTextCentredOnInk` with `IsSingleCharacterText`
  (`UltraCanvasTextLayoutCache.h`): the character's ink in the middle, on
  whole pixels. The button uses it too. A label centres one character so
  when it is centred both ways and is plain text (no markup, shadow, inline
  images or boxes, or selection); a badge in its pill; a segment when the
  character is its only content. Words are drawn as before.
  `Tests/OneCharacterCentringTest.cpp` (was `ButtonTextCentringTest`)
  measures all four from the drawn pixels.
