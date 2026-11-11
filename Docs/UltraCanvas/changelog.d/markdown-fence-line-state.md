- **A block comment inside a Markdown code fence is coloured on every line.**
  Fenced code (```` ```cpp ````) in Markdown mode was highlighted one line at a
  time with no state, so only the first line of a `/* … */` spanning several
  lines was drawn as a comment and the rest as code. The fence's lines now hand
  each other the line state, as a plain code file's lines have since 0.9.57;
  each opening fence starts empty, so a comment left open in one block ends at
  its closing fence. The raw line under the cursor, which is read with the
  Markdown rules, never takes a code block's state: in those rules it would
  have meant an open `<!-- -->` comment.
