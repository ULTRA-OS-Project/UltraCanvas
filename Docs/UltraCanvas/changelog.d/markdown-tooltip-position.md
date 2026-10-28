- **Markdown tooltips in `UltraCanvasTextArea` open at the pointer.** The
  footnote, abbreviation and heading-anchor tooltips of Markdown mode were
  placed with the pointer's position in the text area, which the tooltip
  manager reads as a position in the window - so they opened up and to the
  left of the pointer by the text area's distance from the window's corner
  (in UltraTexter, over the toolbar). `HandleMarkdownHover` now takes the
  mouse event (`HandleMarkdownHover(const UCEvent&)`, was `(int, int)`):
  the pointer in the element finds what is hovered, the pointer in the
  window places the tooltip.
