- **Editing a `$$` math block in a Markdown text area hid its LaTeX.** The
  opening fence and the source lines of a display-math block collapse in
  rendered form, and only the caret's own line was shown raw - so with the
  caret on a fence the formula source vanished from view.
  - With the caret on any line of the block, every line of it is now shown
    as raw source, one under the other; moving the caret out renders the
    formula again. `InlineMathTest` covers each caret position.
