- **A rich document's HTML lost its font sizes on a comma-decimal desktop.**
  `UCRichDocument::ToHTML` wrote a run's size with `std::to_string`, which
  renders through `LC_NUMERIC` - set from the desktop by the Linux backend - so
  on a German or French desktop a 24.5 pt run was written
  `font-size:24,500000pt`, which no CSS reader takes: a rich copy, a mail's
  HTML body or an export read back without its sizes. It is written with
  `FormatFloatClassic` now (`font-size:24.5pt`). `HTMLRichImportTest` checks
  it under a comma-decimal locale where one is installed.
- **`check_locale_numbers.py` sees more writers and more floats.** A file that
  defines a serializer (`X::ToHTML(`, `ToMarkdown`, `ToJSON`, `ToSVG`,
  `ToXML`, `ToCSS`) is checked as a format writer whatever it is called, and
  `std::to_string` of a name declared `float` or `double` - in the file, or
  for a member access (`run.fontSizePt`) in a repository header it includes -
  counts as a float; an argument cast to an integer does not. The ToHTML site
  is what it had missed; two display formatters it now reads are marked
  `// locale-ok`.
- **The other writers it found write dot-decimal too.** The node and
  compositor diagrams' JSON (the saved diagram and the compositor's clipboard
  copy) and the mind map's FreeMind, OPML and SVG exports pin their streams to
  the classic locale, so a host that sets the C++ global locale no longer
  turns `"zoom": 1.5` into `"zoom": 1,5` or an SVG's `x="10.5"` into
  `x="10,5"`. The 3DS reader keeps a material's shininess strength and a
  camera's target and bank as text, and the MilkShape reader its frame rate:
  written with `std::to_string`, they read `0,350000` on a comma-decimal
  desktop and `0.35` now. None of them is in `locale_numbers_baseline.txt`
  any more. `DiagramJsonLocaleTest` (new), `MindMapIOTest`, `Model3DSTest`
  (a file made in the test with a camera and a shininess strength) and
  `ModelMS3DTest` each check theirs under a comma-decimal locale.
- **The locale tests run in CI and in cloud sessions.** They hold file formats
  to dot-decimal numbers under `de_DE.UTF-8` and skip without it, and neither
  CI's runner image nor the cloud image was known to have it. CI's Linux rows
  now generate it (the step fails if it does not take), and
  `.claude/hooks/session-start.sh` generates it in a cloud session.
