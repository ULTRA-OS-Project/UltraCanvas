- **New text layouts wrap a long word between characters.** A layout from
  `IRenderContext::CreateTextLayout` wrapped at word boundaries only - Pango's
  own default - so a URL, a file path or a hash given a width ran on past it.
  Everything drawn through `DrawText` / `DrawTextInRect` already wrapped words
  first and characters second (`TextStyle::wrap` is `WrapWordChar`), and every
  element that wraps on purpose (the text area, the rich-text editor, the
  Markdown view, the tooltip) set that mode itself; a layout made directly
  was the one place that did not. `UCTextLayout` now starts in `WrapWordChar`,
  so the two paths agree, and a layout whose words all fit wraps exactly as
  before. A caller that wants a long word kept whole still asks for
  `TextWrap::WrapWord`. See *Wrapping* in `UltraCanvasRenderContext.md`.
