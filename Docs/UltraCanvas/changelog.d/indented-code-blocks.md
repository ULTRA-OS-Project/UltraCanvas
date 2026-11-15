- **Indented Markdown code blocks are highlighted.** A code block indented
  four spaces has no language tag, and the text area drew it plain. Each
  block's language is now guessed from its own text (`DetectLanguageFromContent`,
  once per block); when the guess is not clear the block stays plain, so
  indented prose is never coloured as code. An application that knows the
  language sets it for every indented block with
  `UltraCanvasTextArea::SetIndentedCodeLanguage("C++")`. A blank line between
  two indented code lines now belongs to the block, as CommonMark has it, so
  the line state carries through it: a `/* comment */` open across the blank
  line stays a comment. Fenced and indented blocks share one highlighting
  routine, which takes a tokenizer name as the detector gives it
  ("Common Lisp") as well as a fence tag.
