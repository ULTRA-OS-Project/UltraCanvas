- **Indented Markdown code blocks are highlighted.** A code block indented
  four spaces has no language tag, and the text area drew it plain. Each
  block's language is now found once per block: a clear guess from its own
  text (`DetectLanguageFromContent`); else the language the nearest code fence
  above it names; else a guess on a lower bar, since the text is known to be
  code - so a one-line `console.log(x);` or `print(x)` is placed too. A block
  none of them places stays plain, so indented prose is never coloured as
  code. `DetectLanguageFromContent` gains an overload taking
  `LanguageDetectOptions` (the minimum score and lead; the one-argument form
  is unchanged). An application that knows the
  language sets it for every indented block with
  `UltraCanvasTextArea::SetIndentedCodeLanguage("C++")`. A blank line between
  two indented code lines now belongs to the block, as CommonMark has it, so
  the line state carries through it: a `/* comment */` open across the blank
  line stays a comment. Fenced and indented blocks share one highlighting
  routine, which takes a tokenizer name as the detector gives it
  ("Common Lisp") as well as a fence tag.
