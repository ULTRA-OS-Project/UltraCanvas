# ODT and DOC/DOCX Support — Current State and Proposal

This document surveys the file-format support that exists today in the
TextArea, Bitmap/Image, LaTeX and Spreadsheet elements, and proposes an
architecture and phased plan for adding OpenDocument Text (`.odt`) and
Microsoft Word (`.docx`, legacy `.doc`) support to UltraCanvas.

---

## Part 1 — Current state survey

### 1.1 TextArea (`include/UltraCanvasTextArea.h`, `core/UltraCanvasTextArea*.cpp`)

- **The element itself performs no file I/O.** Its content API is
  `SetText`/`GetText`/`GetTextForSave` (plain UTF-8), `SetRawBytes`/`GetRawBytes`
  (hex mode) and `SetEditingMode` with three modes: `PlainText`,
  `MarkdownHybrid`, `Hex` (`UltraCanvasTextArea.h:267-271`).
- All real load/save lives in the **Texter app**
  (`Apps/Texter/UltraCanvasTextEditor.cpp`): `LoadFileIntoDocument` (~line 2150)
  reads raw bytes, runs encoding detection/conversion
  (`UltraCanvasEncoding.cpp`), and pushes UTF-8 into the TextArea;
  `SaveDocumentAs` (line 2369) re-encodes and writes bytes back. PDF is
  special-cased to `UltraCanvasPDFView` (MuPDF).
- **The document model is plain, line-based text** (`std::string textContent`
  + `std::vector<std::string> lines`). Rich appearance in markdown mode
  (headings, bold/italic, tables, lists, images, links, footnotes) is produced
  by re-parsing the markdown source into transient per-line layouts
  (`LineLayoutBase` subclasses, `InlineRun` spans). There is **no persistent
  styled-run/attribute model** — nothing that could hold "this range is 14pt
  Georgia, red" independently of markup characters in the text.
- **`.odt`/`.docx` today are classified as binary** by the extension list in
  `IsBinaryFile` (`UltraCanvasTextEditor.cpp:2303-2308`) and open in the hex
  editor. There is no RTF, HTML-import, ODT or DOCX parser anywhere in the
  text path.
- Format dispatch is extension-based and hard-coded (`ext == "md"` →
  MarkdownHybrid); there is no importer/exporter registry in the text path.

### 1.2 Bitmap / Image (`include/UltraCanvasImage.h`, `libspecific/Cairo/ImageCairo.cpp`)

- Raster I/O is delegated to **libvips** (plus ImageMagick delegates): load is
  generic via `VImage::new_from_buffer/file`, save is a per-format switch in
  `UCImageRaster::Save()` (`ImageCairo.cpp:548-770`) covering PNG, JPEG,
  WEBP, TIFF, HEIF/AVIF, JXL, EXR, PSD and many more. Runtime capability
  probes (`VipsCanLoad`/`VipsCanSave`) hide unavailable formats.
- The **graphics plugin registry** (`UltraCanvasGraphicsPluginSystem.h:247-467`)
  provides `IGraphicsPlugin` (`GetSupportedExtensions`, `CanHandle`,
  `LoadGraphics` → `UltraCanvasUIElement`) with extension-map dispatch.
  Notably, its format detector **already maps `rtf`, `doc`, `docx`, `odt` to
  `GraphicsFormatType::Text`** (`:120-121`) — the slot exists, no plugin
  handles it.
- Relevance to ODT/DOCX: images embedded in word documents (`Pictures/` in
  ODF, `word/media/` in OOXML) can be decoded from memory with the existing
  `LoadFromMemory` path — **no new image work is needed**, except noting that
  WMF/EMF clip-art requires an ImageMagick delegate and needs a graceful
  fallback.

### 1.3 LaTeX (`Plugins/LaTeX/`, `core/UltraCanvasLaTeXModuleLoader.cpp`)

- `UltraCanvasLaTeXView` renders **math-mode formulas** via the embedded
  MicroTeX engine as vector paths. It is not a document typesetter and has
  **no file I/O at all** — content enters as a string via `SetLaTeX`.
- It is packaged as a **dlopen'd on-demand module** (`libUltraCanvasLaTeX.so`)
  with a stable C ABI (`UltraCanvasLaTeXModuleABI.h`) — the cleanest plugin
  pattern in the framework and the model to copy for a heavyweight optional
  document module.
- Relevance to ODT/DOCX: both formats can embed math (ODF: MathML; OOXML:
  OMML). A MathML/OMML → LaTeX string converter feeding
  `UltraCanvasLaTeXView` is the natural (optional, late-phase) way to render
  embedded formulas.

### 1.4 Spreadsheet (`core/UltraCanvasSpreadsheetFileIO.cpp`)

- **The most advanced file support in the framework, and the template for
  this proposal.** `LoadFromFile`/`SaveToFile` dispatch on extension:
  - **ODS: fully implemented, both directions** — `ODSLoader`/`ODSSaver`
    classes read/write the ZIP container with **miniz** (vendored,
    `third_party/miniz/`) and parse XML with **tinyxml2** (system dep,
    `CMakeLists.txt:100`), including the mandatory uncompressed `mimetype`
    first entry, `content.xml`, `styles.xml`, `meta.xml`,
    `META-INF/manifest.xml`.
  - **CSV/TSV: fully implemented** with encoding/separator auto-detection and
    import/export dialogs.
  - **XLSX: stubs only** — `LoadXLSX`/`SaveXLSX` (`FileIO.cpp:1131-1145`)
    return "not supported yet".
- The internal model (cells with typed values, formulas, `CellStyle`, merged
  cells, freeze panes, named ranges, conditional formatting, validation) is
  richer than what the ODS saver currently persists — `WriteStyleDefinition`
  emits empty style bodies, and comments/hyperlinks/validation are not
  written. Style round-trip is an open gap.

### 1.5 FileLoader (`include/UltraCanvasFileLoader.h`, `core/UltraCanvasFileLoader.cpp`)

- **Implemented today:** a facade over the native file-selection dialogs
  (`OpenFileDialog`/`SaveFileDialog`/`OpenMultipleFilesDialog`/
  `SelectFolderDialog`), OS recent-documents integration (`NotifyRecentFile`,
  per-platform in `OS/<platform>/UltraCanvas*FileLoader.cpp`), and two
  **typed object loaders** that combine dialog + decode in one call:
  `OpenImage` (→ `UCImageRaster`) and `OpenAudio` (→ `UCAudio` PCM).
- **Documented vision** (`Docs/Modules/FileLoader/README.md` + architecture
  diagram): a universal `FileLoader::LoadFile` (auto-detect decode),
  `ConvertFile` (format-to-format), `LoadMultipleFiles`, `RegisterPlugin`
  (custom format handlers), `ScanFileForThreats` and cache tuning. The
  documented format families **already list "Documents: PDF, DOCX, ODT, RTF,
  TXT, Markdown"** — i.e. ODT/DOCX support is a promised part of the
  FileLoader roadmap, but none of that universal API exists in code yet.
- Consequence for this proposal: FileLoader is the intended **front door**
  for document loading. The ODT/DOCX readers/writers must be exposed through
  it (a typed loader now, `LoadFile`/`ConvertFile` dispatch as that API
  materializes), not buried in app-level code the way Texter's current
  load/save path is.

### 1.6 Reusable infrastructure already in the tree

| Building block | Where | Relevance |
|---|---|---|
| ZIP read/write (miniz) | `third_party/miniz/`, used by `SpreadsheetFileIO.cpp`, eBook, XAR/SVG plugins | ODT, DOCX, XLSX are all ZIP containers |
| XML DOM (tinyxml2) | system dep, used by ODS loader, SVG, `.ucd` | parse `content.xml` / `document.xml` |
| ODF namespace handling & ODS date/time/value parsing | `SpreadsheetFileIO.cpp:33-160` | ODT shares the same ODF namespaces and conventions |
| HTML/XHTML → element-tree converter with CSS cascade | `UltraCanvasHTMLConverter.h` | rich read-only rendering target |
| Document-engine plugin pattern (`IEBookEngine` + factory, extension dispatch) | `Plugins/Documents/eBook/IEBookEngine.h` | slot for a read-only ODT/DOCX viewer engine |
| Markdown hybrid editing + inline-run parser | `UltraCanvasTextArea_Markdown.cpp` | editable representation for imported documents |
| dlopen module pattern with C ABI | LaTeX module loader | packaging for an optional heavyweight document module |
| Encoding conversion | `Apps/Texter/UltraCanvasEncoding.*` | legacy `.doc` text extraction (CP1252 etc.) |
| Typed dialog+decode loaders (`OpenImage`, `OpenAudio`) | `UltraCanvasFileLoader` | pattern for an `OpenTextDocument` entry point |

**Bottom line:** every low-level building block needed for ODT and DOCX
(ZIP, XML, image decoding, a rendering pipeline, an editor) already exists.
What is missing is (a) a **document representation** between the formats and
the UI elements, and (b) the **format readers/writers** themselves. No new
third-party dependencies are required for ODT/DOCX/XLSX.

---

## Part 2 — Proposal

### 2.1 Format primer

| Format | Container | Main content | Effort |
|---|---|---|---|
| `.odt` (ODF Text) | ZIP, `mimetype` first entry | `content.xml`, `styles.xml`, `Pictures/` — same ODF namespaces as our ODS code | Low-medium: we already speak ODF |
| `.docx` (OOXML/OPC) | ZIP | `[Content_Types].xml`, `_rels/.rels`, `word/document.xml` (WordprocessingML), `word/styles.xml`, `word/numbering.xml`, `word/media/`, relationship files | Medium |
| `.doc` (legacy Word 97–2003) | OLE2/CFB **binary** (magic `D0 CF 11 E0`) | `WordDocument` stream + piece table (CLX) | High for fidelity; medium for text-only extraction |

### 2.2 Core architectural decision: an intermediate document model

TextArea has no rich-text model, so we should **not** couple format parsers
directly to it. Instead, introduce one shared in-memory representation and
make every format and every consumer an adapter around it:

```
                    readers                          consumers
  .odt  ──ODTReader──┐                 ┌──ToMarkdown──> TextArea (MarkdownHybrid, editable)
  .docx ─DOCXReader──┼─> UCRichDocument┼──ToHTML──────> HTMLConverter (rich read-only view)
  .doc  ─DOCTextReader┘   (new)        └──ToPlainText─> TextArea (PlainText)
                                       ┌──FromMarkdown─ (edited text back in)
  .odt  <─ODTWriter──┐                 │
  .docx <─DOCXWriter─┴─ UCRichDocument<┘
```

**`UCRichDocument`** (new, `Plugins/Documents/Word/`): a deliberately small
block/run model —

- Blocks: paragraph (with style: heading level, alignment, spacing),
  list item (ordered/unordered, nesting level), table (rows/cells with spans),
  image (bytes + MIME + size), page break, horizontal rule.
- Inline runs: text with attributes {bold, italic, underline, strike,
  sub/superscript, code, font family/size, foreground/background color,
  hyperlink target}.
- Document-level: metadata (title/author/dates), embedded media store
  (reuse the `UCMediaResource` shape from `UltraCanvasDocument.h`),
  named styles table (so DOCX/ODT style inheritance can be resolved once at
  load and flattened into runs).

This mirrors how the spreadsheet already works (one rich model,
`ODSLoader`/`ODSSaver`/`CSVLoader` as adapters) and keeps ODT and DOCX
readers/writers symmetric.

### 2.3 Shared packaging layer (also unblocks XLSX)

Extract the miniz/tinyxml2 container code that today is private to
`UltraCanvasSpreadsheetFileIO.cpp` into a small reusable unit, e.g.
`core/UCZipPackage.{h,cpp}`:

- `UCZipPackage::OpenRead(path)` / `ReadEntry(name)` / `ListEntries()`
- `UCZipPackage::OpenWrite(path)` with ODF rule support (store the
  `mimetype` entry first, uncompressed) and OPC helpers
  (`[Content_Types].xml`, `_rels` relationship resolution: rId → target part)
- shared XML escape/format helpers currently duplicated in the ODS saver

The ODS code becomes the first consumer (pure refactor, behavior unchanged);
ODT, DOCX and the future XLSX implementation are the next three consumers.

### 2.4 Consumer wiring per element

**TextArea / Texter (editable path — the main deliverable)**
1. Remove `odt`/`docx` from the `IsBinaryFile` extension list
   (`UltraCanvasTextEditor.cpp:2306-2308`) and add an import branch in
   `LoadFileIntoDocument`: parse → `UCRichDocument` → `ToMarkdown()` →
   `SetEditingMode(MarkdownHybrid)` + `SetText(...)`.
2. On save to `.odt`/`.docx`: `FromMarkdown(GetText())` → writer. When the
   original file contained constructs Markdown cannot hold (exact fonts,
   colors, page geometry, footnote layout), warn once per document —
   the same "formatting will be reduced" pattern word processors use for
   cross-format saves.
3. Keep the original `UCRichDocument` attached to the Texter `DocumentTab`
   so unedited blocks (e.g. complex tables, images) can be re-emitted
   losslessly on save even though the editing surface is Markdown.

What survives the Markdown round-trip: headings, bold/italic/strike, inline
code, lists (incl. nesting), simple tables, images, hyperlinks, block quotes.
What is reduced: explicit fonts/sizes/colors, character-level spacing, page
layout, headers/footers, footnote positioning, tracked changes. That
trade-off is acceptable for a Phase-1 text editor; full fidelity needs the
Phase-5 rich model below.

**Read-only rich view (eBook viewer / MediaViewer / DemoApp document tab)**
- Implement a thin `IEBookEngine` (or feed the existing `HTMLConverter`) with
  `UCRichDocument::ToHTML()`. This reuses the pager, TOC and text-extraction
  machinery the eBook stack already has, and fills the DemoApp's "Text
  Document Support — Not Implemented" placeholder
  (`UltraCanvasDemoExamples.cpp:187`, which already lists ODT/RTF as planned).
- Optionally register an `IGraphicsPlugin` wrapper so
  `LoadGraphicsFile("x.odt")` returns the rendered view — the extension map
  already classifies these extensions as `GraphicsFormatType::Text`.

**FileLoader (framework front door)**
- Add a typed loader following the existing `OpenImage`/`OpenAudio` pattern:
  `UltraCanvasFileLoader::OpenTextDocument(opts, onResult)` — native dialog
  (default filters: odt/docx/doc/md/txt), then dispatch by
  extension/signature to the matching reader and deliver a
  `shared_ptr<UCRichDocument>` in the callback, with errors reported through
  `FileLoadResult::loadError` exactly like the image/audio loaders. Texter
  and any other app then consume documents through FileLoader instead of
  hand-rolled `ifstream` code.
- The reader/writer pairs double as the first **`ConvertFile` handlers** for
  the documented universal FileLoader API: `ConvertFile("a.docx", "a.odt")`
  (and ↔ Markdown/HTML/plain text) becomes read-to-`UCRichDocument` +
  write, satisfying the Documents row ("PDF, DOCX, ODT, RTF, TXT, Markdown")
  that `Docs/Modules/FileLoader/README.md` already advertises. When the
  planned `FileLoader::RegisterPlugin` registry is built, the ODT/DOCX
  readers/writers should be its first registrants — designing them now as
  self-describing handlers (supported extensions + magic-byte check + read +
  write) keeps that migration mechanical.
- Signature-based detection belongs here rather than in each app: ZIP magic
  (`PK\x03\x04`) + `mimetype`/`[Content_Types].xml` probe distinguishes
  ODT/DOCX/renamed files; CFB magic (`D0 CF 11 E0`) identifies legacy `.doc`.

**Bitmap element** — no changes required. Embedded images are decoded from
memory through the existing libvips path. Add a placeholder glyph fallback
for WMF/EMF when the ImageMagick delegate is absent (probe with
`VipsCanLoad`).

**LaTeX element** — no changes required for Phases 1–4. Phase 5 option:
OMML (DOCX) / MathML (ODT) → LaTeX-string converter so embedded equations
render through `UltraCanvasLaTeXView` instead of appearing as flat text.

**Spreadsheet element** — ODT itself is not a spreadsheet concern, but two
items belong in the same work stream because they share `UCZipPackage`:
1. Implement the existing `LoadXLSX`/`SaveXLSX` stubs (SpreadsheetML is
   OPC-packaged XML, structurally a sibling of the DOCX reader/writer).
2. Complete ODS style serialization (`WriteStyleDefinition` currently emits
   empty style bodies), so ODF round-trip stops losing formatting.

### 2.5 Legacy `.doc` strategy

Writing a full Word 97 binary parser is not proportionate to demand.
Recommended tiering:

- **Import, text-only (recommended):** a compact CFB (Compound File Binary)
  reader + `WordDocument` stream piece-table walk extracts the full text,
  including the CP1252/UTF-16 mixed pieces (our `UltraCanvasEncoding` handles
  the conversion). This is a bounded, dependency-free effort and covers the
  dominant use case ("open this old file"). Formatting is discarded; the file
  opens as plain text with an info banner.
- **Export: never.** Offer "Save as .docx" instead — this matches what
  modern Word itself encourages.
- Until the CFB reader lands, detect the `D0 CF 11 E0` magic and show a clear
  "Legacy .doc — please convert to .docx" message rather than the hex view.
- External converters (LibreOffice headless, antiword/wv) are rejected as
  runtime dependencies: heavyweight, GPL-licensing friction, and not
  portable across our target platforms.

### 2.6 Packaging

Place readers/writers + `UCRichDocument` in a new
`Plugins/Documents/Word/` module (working name; covers ODT+DOCX+DOC).
Given the code size is moderate and the only dependencies (miniz, tinyxml2)
are already core dependencies, building it into the main library behind a
CMake option (`ULTRACANVAS_PLUGIN_WORDDOC`, default ON) is simpler than a
dlopen module; the LaTeX-style C-ABI dlopen pattern remains available if
binary-size pressure appears later.

### 2.7 Phased plan

| Phase | Scope | Notes |
|---|---|---|
| **1. Foundations** | Extract `UCZipPackage` + XML helpers from `SpreadsheetFileIO.cpp` (pure refactor); define `UCRichDocument`; ODT/DOCX extension + MIME registration; stop hex-opening `.odt`/`.docx` in Texter | Small; unblocks everything incl. XLSX |
| **2. Import** | `ODTReader` (reuse ODF conventions from ODS loader), then `DOCXReader` (OPC rels, `document.xml`, styles flattening, `word/media/`); `ToMarkdown` + `ToHTML` serializers; `UltraCanvasFileLoader::OpenTextDocument` typed loader with signature-based detection; Texter open path; read-only rich view | ODT first — closest to existing code |
| **3. Export** | `ODTWriter` (mimetype-first ZIP, manifest, `content.xml`/`styles.xml`, `Pictures/`), `DOCXWriter` (`[Content_Types].xml`, rels, `document.xml`); `FromMarkdown`; Texter save path with formatting-reduction warning; `FileLoader::ConvertFile` document conversions | Round-trip tests against LibreOffice & Word |
| **4. Legacy `.doc`** | CFB reader + piece-table text extraction, import only; friendly unsupported-message fallback ships earlier in Phase 1 | Text-only by design |
| **5. Fidelity (optional)** | Styled-run editable document model (rich TextArea or new RichTextArea) editing `UCRichDocument` directly; OMML/MathML → MicroTeX math rendering; XLSX stubs implementation + ODS style round-trip completion | Turns Texter from "text editor that opens documents" into a word processor |

### 2.8 Validation

- Round-trip fixture corpus: documents authored in LibreOffice Writer and
  Microsoft Word exercising headings, nested lists, tables with spans,
  embedded PNG/JPEG, hyperlinks, footnotes, math.
- Invariants: `Read(Write(Read(f)))` stable; files must open cleanly in
  LibreOffice and Word after our writers produce them (ODF validator and
  OOXML schema checks in CI where feasible).
- Negative tests: encrypted OOXML (`EncryptedPackage` OLE wrapper), corrupt
  ZIP central directory, `.doc` renamed to `.docx` — all must fail with a
  clear `GetLastError()`-style message, mirroring the spreadsheet's existing
  error-reporting convention.
