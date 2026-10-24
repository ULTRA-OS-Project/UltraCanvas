- **`UltraCanvasLabel::GetNaturalTextWidth()`: where a label's text ends.** A
  label given a fixed box is usually wider than its text, and something meant
  to sit right after the text - a copy button after a code, a badge after a
  name - had to guess the text's width from a character count and a font it
  hoped the platform would use. The label now says it: the width its text
  takes, as it lays it out (font, markup, inline images, line breaks kept),
  without padding or border; 0 for empty text or before the label can reach a
  render context. It was the private measurement behind the label's own
  max-content size. Documented under *Where the text ends* in
  `Docs/UltraCanvas/UltraCanvasLabelExamples.md`; UltraAuthenticator is the
  first user.
