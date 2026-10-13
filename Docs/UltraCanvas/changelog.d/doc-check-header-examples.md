- **A header's usage example can be compiled too, when it opts in.**
  `scripts/check_doc_examples.py` checked the component docs, but the
  sketch at the top of a header had nothing reading it: the DesktopShell
  header's example called `AddToggleButton` with `...` until someone
  noticed by eye. A
  `// doc-check: example` line above such an example now puts it in the
  same check, reported at the header's own lines. The example is the
  comment lines after the marker indented two spaces or more past the
  `//`, so the prose after it is not read as code, and a
  `// doc-check: <declarations>` line declares the names it takes from the
  application, as a doc's `<!-- doc-check: ... -->` comment does. `--all`,
  which CI runs, checks every marked header under `UltraCanvas/include`,
  `UltraCanvas/Plugins` and `VirtualFS/include`. Marking is a choice, not a
  rule: a sketch with `...` in it stays unmarked.
  - **Ten headers are marked**, the ones whose example compiled as
    written: DesktopShell, FileLock, FormLayout, ListSortFilterProxy (two
    examples), PathUtf8, ProgressDialog, RichTextPrint, WaveSeparator,
    UltraDatabase and UltraMessageUltraCanvas.
  - **Declaring their names found two stale examples.** The ProgressDialog
    and RichTextPrint headers passed a `std::shared_ptr` window where
    `UltraCanvasProgressDialog::Show` and `PrintDocumentWithDialog` take an
    `UltraCanvasWindowBase*`; they pass `window.get()` now, as their docs
    do. Until the names were declared, the undeclared `onCancel` and
    `richEdit` had stopped clang before it reached the window argument.
