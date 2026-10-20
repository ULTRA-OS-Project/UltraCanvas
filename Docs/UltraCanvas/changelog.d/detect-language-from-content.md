- **Text with no file name can be given its language.**
  `DetectLanguageFromContent(text)` (`UltraCanvasLanguageDetect.h`) returns
  the language a piece of text is written in - a name `SetProgrammingLanguage`
  takes, `"Markdown"`, or `""` when the text does not clearly say - from its
  content alone: a `#!` line, a text that is wholly PHP / XML (SVG, RSS, KML,
  POM) / HTML / JSON (JSONC, GeoJSON), or otherwise the lines that can only be
  one language (C, C++, C#, Java, Python, JavaScript, TypeScript, Go, Rust,
  Ruby, Shell, SQL, CSS, YAML, Lua, Kotlin, Swift, Perl, Pascal, Elixir,
  Fortran, VBA, R, Lisp, x86 assembly). Until now a language could only be
  picked from a file name or extension. Standard library only; tested by
  `LanguageDetectTest`. UltraTexter 1.69 uses it for text pasted into a new
  tab.
  - `UltraCanvasMediaViewer` asks it for a `.txt` / `.text` file, whose name
    names no language: code is highlighted as its language, Markdown
    rendered, prose left plain (UltraViewer 1.0.9).
  - The clipboard history files a copy as `ClipboardEntryKind::Code` also when
    the detector names its language (a SQL query, a Python block, YAML) and
    `LooksLikeCode` missed it for want of brackets; Markdown stays `Text`
    (UltraClipboard 0.1.6, whose *Edit* and *Save as* use the language too). See *Picking a language for text with no file name* in
  `Docs/UltraCanvas/UltraCanvasTextAreaExamples.md`.
