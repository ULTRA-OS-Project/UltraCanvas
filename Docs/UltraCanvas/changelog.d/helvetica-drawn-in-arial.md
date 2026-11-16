- **A document naming Helvetica was reported as missing a font on the Windows
  computer it was written on.** Windows has no Helvetica: it maps the name
  onto Arial for every program (its `FontSubstitutes`), which is why Writer
  shows nothing amiss. The framework makes its own fontconfig set there, with
  no such mapping, so the text fell to the interface font and
  `UltraCanvasRichTextEdit`'s notice said "Not installed, drawn in a
  substitute: Helvetica".
  - `GetFontFamilySubstitutes` / `FontFamilySubstitute`
    (`UltraCanvasFontCoverage.h`): the families with stand-ins of another
    name - Helvetica, Helv, Times, Tms Rmn and Courier as Windows maps them,
    and fontconfig's metric-compatible pairs (Arial and Liberation Sans,
    Calibri and Carlito, Cambria and Caladea) - and the first of them that is
    installed.
  - The generated fontconfig aliases (`GenerateFontAliasConfig`, the
    generated `fonts.conf`) carry them, so Helvetica is drawn in Arial on
    Windows; an installed family still wins.
  - The font report leaves out a family with an installed stand-in.
  - The table is the framework's own: a substitution a user added to the
    Windows registry by hand is not read.
