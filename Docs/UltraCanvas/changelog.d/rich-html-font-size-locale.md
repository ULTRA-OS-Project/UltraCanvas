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
  is what it had missed. Four pre-existing writer sites it now also sees are
  recorded in `locale_numbers_baseline.txt` as debt (the JSON of the
  compositor and node diagrams, the mind map's FreeMind / OPML / SVG export,
  a 3DS material extra); two display formatters it now reads are marked
  `// locale-ok`.
