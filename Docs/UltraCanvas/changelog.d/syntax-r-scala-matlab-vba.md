- **R, Scala, MATLAB and VBA are switched on in the syntax highlighter.**
  Their rules were written but their `RegisterLanguage` lines in the
  `SyntaxTokenizer` constructor were commented out, and the four factory
  functions were never declared. They are declared and registered now, so
  the text editor highlights them and the Filer shows their files as text.
  - MATLAB claims only `.m`: `.mlx` (a ZIP) and `.mat` (binary data) are not
    source text.
  - VBA claims `.vba`, `.cls` and `.frm`, and no longer `.bas`. BASIC claimed
    `.bas` as well, and the extension lookup walks an unordered map, so
    which of the two won was left to chance. `.bas` is BASIC.
  - Scala also claims `.sbt` build files.
  - The demo app's text samples add `sample.r`, `sample.scala`, `sample.m`
    and `sample.vba`.
  - `.cls` and `.m` are shared: `.cls` is also a LaTeX class, `.m` also
    Objective-C. `SyntaxTokenizer::LanguageFromContent(extension, text)`
    (new, static) reads the head of such a file and names the language it
    really is, or "" when the extension is not shared or the text does not
    say. `SyntaxTokenizer::ClearLanguage()` (new) returns to plain text.
  - `UltraCanvasTextArea::SetProgrammingLanguageForFile(filename, text)`
    (new): the filename / extension match, with a shared extension settled
    by the text. A language without rules (LaTeX, Objective-C) leaves the
    text plain rather than coloured as the other language.
  - The Filer names such a file after its content ("LaTeX Text",
    "Objective-C Text"), reading the first 8 KB of a local `.cls` / `.m` once
    per size and modification time; the media viewer's text preview picks
    its highlighting the same way.
