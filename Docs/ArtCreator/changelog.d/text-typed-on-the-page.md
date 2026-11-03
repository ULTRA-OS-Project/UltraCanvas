- **Text is typed on the page, not in a window.** The Text tool's click
  puts a caret where the text starts, and the words appear there as they
  are typed - in the text's font, size, style, fill colour, alignment and
  spacing, at the canvas's zoom, following it as the view zooms or scrolls -
  inside a dashed frame. Enter starts a new line (each line is a span on its
  own baseline, so the drawing and its SVG keep them). Escape, Ctrl+Enter, a
  click outside, another tool or any command that changes the drawing
  finishes it, as one undo step (*Text*, or *Edit Text*); the editor's own
  Ctrl+Z takes back typing until then. The Text dialog is gone.
- **Click a text to edit it in place** with the Text tool, or double-click it
  with the Selector (which used to do nothing on a text). The text is left
  out of the drawing while its words are typed over it; emptied, it is
  deleted. A text looked at and not changed adds no undo step.
- **The text bar.** While the Text tool is active, the bar under the toolbar
  sets a text's **font** (every family installed, not three; type its first
  letters to find one), **size**, **bold**, **italic**, **underline**,
  **strikethrough**, **alignment** (the lines start at, centre on or end at
  the text's point), **line spacing** (% of the size) and **letter spacing**
  (points between letters, below 0 closer). While a text is typed, the
  first six go to the selected words - spans of their own - or, with none
  selected, to the whole text; the others always to the whole text. With no
  text being typed they go to the selected texts, as one undo step, and are
  what the next new text starts in. The bar shows what is at the caret.
  It replaces the Text tool's card in the right panel.
- A colour picked while a text is typed colours that text. Zooming while
  typing (Ctrl+wheel over the words, or the View commands) keeps the keys
  with the text.
