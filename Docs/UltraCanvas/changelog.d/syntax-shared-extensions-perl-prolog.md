- **`.pl` is Perl or Prolog by what the file says, not by chance.** Perl and
  Prolog both claim `.pl`, and the extension lookup walks an unordered map, so
  which language a `.pl` file was highlighted as - and what the Filer called
  it - depended on the hash order of the build.
  - `SyntaxTokenizer::SharedExtensionLanguages(extension)` (new, static) lists
    the languages a shared extension can be, the default first: `.cls` VBA /
    LaTeX, `.m` MATLAB / Objective-C, `.pl` Perl / Prolog.
    `SetLanguageByExtension` picks that default instead of the map's first
    claimant.
  - `SyntaxTokenizer::LanguageFromContent` also tells `.pl` apart: a `#` or
    `#!` line, `use`, `my`, `our`, `sub`, `package`, `require` or POD is Perl;
    a `%` or `/*` comment or a `:-` clause is Prolog.
  - `UltraCanvasFilerWidget` names every shared extension after its content
    ("Prolog Text"), and after the default when the file does not say.
    `GetPreviewableFormats()` labels a shared extension with all of its
    languages, so the Display > Thumbnails > Text switch reads "VBA / LaTeX",
    "MATLAB / Objective-C" and "Perl / Prolog".
