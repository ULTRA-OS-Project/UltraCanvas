- **Every source-text type is a Text format of the file display.** The
  Filer's extension table named 19 text types. Swift, Rust, SQL, Go, Kotlin,
  Java, PHP, Lua, Ruby, C#, CSS, Pascal, the assemblers and the rest of the
  syntax highlighter's languages were "Other": a blank sheet instead of the
  miniature page of their text, and no switch for them under Display >
  Thumbnails.
  - `SyntaxTokenizer::GetLanguageExtensions()` (new) lists every registered
    language with the extensions it claims. The highlighter is the one list
    of source-text extensions in the framework.
  - `UltraCanvasFilerWidget` classifies an extension that neither its table
    nor a registered plugin claims as Text when the highlighter knows it, and
    names its type after the language ("Swift Text").
    `GetPreviewableFormats()` lists each one under Text, so a settings page
    offers a switch per type. Binary members of a language's list (MATLAB
    `.mat` / `.mlx`, gzip `.svgz`) are left out; a binary file under a text
    extension still previews safely, since the reader stops at the first NUL.
  - New test `FilerSourceTextFormatsTest`.
