- **The vector converter doc's examples compile again.** The C++ in
  `UltraCanvasVectorConverters.md` used the converter classes
  (`XARConverter`, `EPSConverter`, `CDRConverter`, `EMFConverter`,
  `WMFConverter`, `AIConverter`, `DXFConverter`, `DWGConverter`) and
  `AutoRegisteredFormatPlugins()` without saying where they come from. Those
  live in the plugin headers (`UltraCanvas/Plugins/Vector/`,
  `UltraCanvas/Plugins/UltraCanvasAllFormats.h`), not the public ones, so a
  reader copying an example got undeclared names and
  `scripts/check_doc_examples.py` reported 18 errors. The examples now open
  with the `#include`s they need, and the checker passes the doc.
- **`llms.txt` no longer shows a `doc-check` note as a document's
  description.** `scripts/generate_llms_txt.py` took the first paragraph after
  the title, and in `UltraCanvasHTMLReader.md` and `UltraCanvasSVGExamples.md`
  that was the hidden `<!-- doc-check: ... -->` note for the example checker.
  HTML comments are now skipped, so both index entries describe the document.
