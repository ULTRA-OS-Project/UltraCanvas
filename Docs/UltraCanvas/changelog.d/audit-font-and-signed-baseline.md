- **The system fonts can be pinned.**
  `UltraCanvasApplication::SetSystemFontStyle()` and
  `SetDefaultMonospacedFontStyle()` set what `GetSystemFontStyle()` and
  `GetDefaultMonospacedFontStyle()` answer, instead of the desktop's fonts -
  for a program that brings its own, or a test that measures text and must
  not depend on whatever font the machine has. The demo's layout audit
  (`UltraCanvasDemo --audit-layout`, both forms) now runs in the bundled
  Ubuntu 12 and Ubuntu Mono 12 whatever the runner's desktop font is, so
  the pages are measured in the font they were laid out for.
- **`scripts/check_doc_examples.py` 1.9.0: the baseline is signed.**
  In `scripts/doc_examples_baseline.txt` each doc's entries end in a line
  that signs them, and `--update-baseline` is the only way to write the
  file: a block edited by hand, spliced from another run or merged line by
  line fails `--strict` with a message naming the docs to regenerate. One
  signature per doc rather than per file, so a merge that brings another
  doc's rewritten block from `main` stays valid. With docs named,
  `--update-baseline` replaces those docs' entries and keeps the rest, so
  the file can be updated from a partial run.
