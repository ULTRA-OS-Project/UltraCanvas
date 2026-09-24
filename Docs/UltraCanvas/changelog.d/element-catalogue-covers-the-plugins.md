- **The element catalogue was missing seventy elements, and now cannot be
  again.** `Docs/UltraCanvas/UltraCanvasUIElements.md` answers the question
  that comes before every piece of new UI - *does an element for this already
  exist?* - and it listed only the ~60 elements in `UltraCanvas/include/`. The
  ~70 under `include/Plugins/` got one sentence: "charts, diagrams and
  document views live under `UltraCanvas/Plugins/` with their own docs". That
  is not an answer to anyone searching the page for what they need: in
  2026-09 a second progress bar was written from scratch for UltraFiler's
  status strip because `UltraCanvasGaugeDiagramElement` - the framework's
  progress bar, in `GaugeMode::LinearBar` - was in
  `include/Plugins/Diagrams/` and in no table. The duplicate was found and
  deleted, and the gauge got a row; the audit behind this entry shows it was
  three of seventy-five.
  - **Every plugin element is now catalogued**, in three tables under
    *Charts, diagrams and codes*: 34 chart elements (from line/bar/scatter/area
    through contour surfaces, spectrograms, Gantt and Kanban to the engine you
    derive a new chart type from), 26 diagram elements (flow, node and
    compositor graphs, UML, ER, SysML, mind map, Sankey, Venn, word cloud,
    packet layout and the rest) and the codes and document views -
    `UltraCanvasQRCode`, `UltraCanvasBarcodeElement`, `UltraCanvasPDFView`,
    `UltraCanvasMarkdownDisplay`. Each row says what the element is FOR,
    because the reader knows the need and not the name.
  - The vector format decoders (`UltraCanvasSVGElement`,
    `UltraCanvasCDRElement`, `UltraCanvasEPSElement`, `UltraCanvasXARElement`)
    are named in a closing note rather than given rows: they work behind
    `UltraCanvasVectorElement` and `UltraCanvasImageElement`, which are what a
    caller reaches for. Saying so is worth more than silence.
  - `UltraCanvasNewDocumentDialog`, missing from the dialogs table, turned up
    in the same audit and was added.
- **`scripts/check_element_catalogue.py` keeps it complete.** An element in
  the tree that is not named on that page fails the check - the moment the
  author is best placed to write the one row that saves the next reader a
  week. A page with holes in it is worse than no page: it is read as a
  complete answer to "does this already exist?", and a hole reads as "no".
  Deliberate omissions (a base class, a decoder behind a catalogued facade, a
  platform implementation) go in `scripts/element_catalogue_exempt.txt` with
  their reason; there are six. Runs in CI as `element-catalogue.yml`.
