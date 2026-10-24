- **The copy button sits right after the code on every platform.** Its place
  was worked out from a character width measured for one Linux font, so with
  another monospaced face - Consolas on Windows is narrower - it stood off
  from the digits, and after the dots of a hidden code it could crowd them.
  It is placed from the code label's own measurement now
  (`UltraCanvasLabel::GetNaturalTextWidth`, framework changelog:
  `label-natural-text-width`), and follows the text whenever it changes
  between digits, dots and the dashes before a counter-based card's first
  code.
