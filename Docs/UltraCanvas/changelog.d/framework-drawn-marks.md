- **Framework elements draw their marks instead of typing symbols the UI
  font lacks.** The bundled Ubuntu font has no "✓", "✗", "✕", "◦", "▪",
  "▲", "▼", arrows or emoji, and with no fallback font for them (a stock
  Windows install) each drew as a box of hex digits. Now:
  - `UltraCanvasDrawnMarks.h` (new; doc `UltraCanvasDrawnMarks.md`) draws a
    tick, a cross, a sort triangle and the list bullets "•", "◦", "▪" as
    shapes;
  - the password rule legend draws its met / unmet icons ("✓" and "✗" by
    default; "✔", "✘", "✕", "×", "○", "●" when set), any other string still
    as text;
  - Markdown lists (`UltraCanvasTextArea`) and `UltraCanvasRichTextEdit`
    draw their nested bullets;
  - a success dialog's badge shows a drawn white check
    (`media/icons/clipboard/check.svg`) instead of "✓";
  - the e-book viewer's Previous, Next and page-colour buttons show flat
    chevron and moon icons, with tooltips, instead of "◀", "▶", "☾";
  - the Filer's details header draws its sort triangle;
  - the menu configurator's Add and Remove buttons show chevrons, and a
    separator row reads "————";
  - the new-document dialog draws its close cross and selection tick, and
    shows a tinted document icon instead of "📄";
  - text says it in a word or a character the font has: the date range
    separator is "–", the chord tooltip "A to B", the sunburst's drill-up
    hint "Back", a Venn intersection "A & B", the curves readout "Input 128,
    output 140", a class diagram's reading direction "›" / "‹", a git tag
    "◊ v1.0", an unread message "•", and a Markdown image placeholder
    "Remote image: …" / "Missing image: …".
