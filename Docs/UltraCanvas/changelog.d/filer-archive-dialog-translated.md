- **The Filer's Compress / Extract dialog showed English beside a translated
  hint.** Its title, the Compress / Extract and Cancel buttons, the
  "Location:" line and the "N archives" label were bare literals, so a German
  desktop read "Extract" and "Location:  C:\…" above "Ziehen Sie das Symbol auf
  einen Ordner…". They go through `Tr` / `TrF` / `TrN` now
  (`UltraCanvasFilerWidget`), and the default names a person reads in the
  name field and keeps on disk - `archive` for a selection with no name to
  borrow, `Extracted` for several archives unpacked at once - are translated
  as file and folder names (`TrC("file name", ...)`, `TrC("folder name", ...)`),
  the same in the dialog, its commit and the direct Compress.
  - Standard set: `Extract` moves up from the Computer base set (every
    language keeps its translation), and `Location: {0}`, `{n} archive` and
    the two default names arrive drafted in every language under
    `media/locale/` (machine draft, reviewed by -).
