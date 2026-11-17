- **`echo $HOME` is detected as a shell command, not PHP.** The content
  language detector gave every line starting `echo $` two points for PHP and
  only one for shell, so on the lower bar a Markdown code block uses for short
  snippets, `echo $HOME` was coloured as PHP. A PHP `echo` is a statement that
  ends in `;`; a shell `echo` almost never does. `echo $x;` still votes PHP,
  and `echo $HOME` without the semicolon now votes shell.
