- **Text is typed on the page, not in a window.** The Text tool's click
  puts a caret where the text starts, and the words appear there as they
  are typed - in the text's font, size, weight and fill colour, at the
  canvas's zoom, following it as the view zooms or scrolls - inside a dashed
  frame. Enter starts a new line (each line is a span on its own baseline,
  so the drawing and its SVG keep them); Ctrl+B and Ctrl+I set part of the
  text bold or italic, which becomes a span of its own. Escape, Ctrl+Enter,
  a click outside, another tool or any command that changes the drawing
  finishes it, as one undo step (*Text*, or *Edit Text*); the editor's own
  Ctrl+Z takes back typing until then. The Text dialog is gone.
- **Click a text to edit it in place** with the Text tool, or double-click it
  with the Selector (which used to do nothing on a text). The text is left
  out of the drawing while its words are typed over it; emptied, it is
  deleted. A text looked at and not changed adds no undo step.
- The Text tool's panel has **Italic** beside Bold, and its Font, Size, Bold
  and Italic apply at once: to the whole of the text being typed, or - with
  no text being typed - to the selected texts, as one undo step. A colour
  picked while typing colours the text.
