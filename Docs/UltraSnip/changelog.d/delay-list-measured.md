- **The delay list is exactly as wide as its longest item.** Its width was
  estimated from the number of characters, which counts Thai and
  Devanagari vowel and tone marks as characters of their own, so the list
  ran wide in Thai and Hindi. It is now measured in the list's own font by
  the window (which has its render context before the bar is built), plus
  the room the list's padding and arrow take; the estimate is kept only for
  a window without a render context.
