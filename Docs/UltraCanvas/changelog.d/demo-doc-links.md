- **Every demo page's Documentation button opens a document.** Five paths in
  `Apps/DemoApp` named files that did not exist, so the button opened
  nothing:
  - Chord Chart and Compositor Diagram had no document at all; they now have
    `UltraCanvasChordChartExamples.md` and
    `UltraCanvasCompositorDiagramExamples.md`.
  - Circular info graphic pointed at `UltraCanvasCircularInfoGraphicExamples.md`;
    its document is `UltraCanvasCircularInfoGraphic.md`.
  - The Gource Tree snippet in `WindowsCDrive_TabAddition.cpp` (not built)
    and the List View's sample file row named `Docs/UltraCanvasGourceTree.md`
    and `Docs/CHANGELOG.md`; both now name the files that exist.
- **CI refuses a demo page whose document is missing.**
  `scripts/generate_doc_toc.py` reports every `Docs/....md` path in
  `Apps/DemoApp/*.cpp` / `*.h` that does not exist, with its file and line,
  and fails; `llms-txt.yml` runs it.
