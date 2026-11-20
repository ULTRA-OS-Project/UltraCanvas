# UltraCanvasPresentationOdp — OpenDocument decks (.odp), read and written

`UltraCanvasPresentationOdp.h` reads an OpenDocument presentation - an
Impress `.odp`, a template `.otp`, or the flat single-XML `.fodp` - into the
same UI-free model as a PowerPoint deck
([UltraCanvasPresentationPptx](UltraCanvasPresentationPptx.md)): the slides in
order, their titles, the paragraphs of every placeholder, text box and shape
with their list levels, bullets and numbering, bold / italic / underline,
tables, the pictures' parts and descriptions, speaker notes, and which slides
are hidden. One model means one outline, one preview and one text extractor
for both kinds of deck.

It reads text, tables, pictures and charts; `RenderPptxSlide` draws what it
reads. `WriteOdpPresentation` writes the model back as an Impress deck -
whether it was read from an `.odp`, from a `.pptx`, or laid out from a
document (*Writing a deck*, below). A
password-protected `.odp` opens with its password
([UltraCanvasOdfEncryption](UltraCanvasOdfEncryption.md)); without it, or
with a wrong one, it is refused with a reason.

## Opening one as a document

`UltraCanvasFileLoader::LoadTextDocument` and `OpenTextDocument` open a deck
as an outline, by its content, whatever its name; the Open dialog's
"Documents" (`AddTextDocumentLoadTypes`) offer `.odp`, `.otp` and `.fodp`:

<!-- doc-check: std::string path; void ShowDocument(std::shared_ptr<UltraCanvas::UCRichDocument> document); void ShowError(const std::string& message); -->
```cpp
#include "UltraCanvasFileLoader.h"

using namespace UltraCanvas;

std::string error;
if (auto outline = UltraCanvasFileLoader::LoadTextDocument(path, error)) {
    ShowDocument(outline);   // e.g. in an UltraCanvasRichTextEdit
} else {
    ShowError(error);        // "The file is password-protected; ...", ...
}
```

The outline is the PowerPoint one: per slide a heading (its number and
title, "(hidden)" after a hidden slide's), its bulleted paragraphs as list
items at their level - numbered where the list numbers them - its other text
as paragraphs, a subtitle in italics, each table as a table, a line
`[Picture: <description>]` for each picture, and the speaker notes as a block
quote opening "Notes:".

`UltraCanvasFileLoader::ExtractText` reads a deck's text the same way, and
UltraFiler's preview page (`UltraCanvasFilerWidget::TextPreviewLines`) shows
the first page of the outline. UltraTexter opens a deck as the outline in its
document editor.

## Reading the model

<!-- doc-check: std::string path; std::vector<uint8_t> bytes; void ShowError(const std::string& message); -->
```cpp
#include "UltraCanvasPresentationOdp.h"

using namespace UltraCanvas;

PptxPresentation deck;   // the PowerPoint reader's model
std::string error;
if (!IsOdpPresentationFile(path)) {
    ShowError("not an OpenDocument presentation");
} else if (!ReadOdpPresentation(path, deck, error)) {
    ShowError(error);
} else {
    for (const PptxSlide& slide : deck.slides) {
        for (const PptxShape& shape : slide.shapes) {
            if (shape.kind == PptxShapeKind::Picture) (void)shape.altText;
            for (const PptxParagraph& paragraph : shape.paragraphs)
                (void)(std::string(paragraph.level * 2, ' ') + paragraph.Text());
        }
    }
}
// A package (or a flat file's XML) already in memory:
PptxPresentation fromMemory;
ReadOdpPresentationFromMemory(bytes, fromMemory, error);
UCRichDocument outline = PptxPresentationToRichDocument(deck);
```

| Function | Does |
|---|---|
| `IsOdpPresentationFile(path)` | true for a ZIP package whose `mimetype` (or manifest) says `application/vnd.oasis.opendocument.presentation` or `-template`, or a flat XML document whose `office:mimetype` says so |
| `IsOdpExtension(extOrPath)` | `odp`, `otp`, `fodp`, with or without a dot, any case |
| `ReadOdpPresentation(path, out, error, password)` | the deck; `path` is UTF-8, `password` (optional) opens a protected one. False with a sentence in `error` |
| `ReadOdpPresentationFromMemory(bytes, out, error, password)` | the same for bytes: a package, or a flat file's XML |

The model's types are described in
[UltraCanvasPresentationPptx](UltraCanvasPresentationPptx.md#reading-the-model).
For an `.odp` they hold:

| Field | From |
|---|---|
| `PptxPresentation::slideWidth` / `slideHeight` | the page layout of the first slide's master page (`fo:page-width` / `fo:page-height`), in EMU |
| `PptxSlide::hidden` | `presentation:visibility="hidden"` on the slide's drawing-page style |
| `PptxSlide::title` | the `presentation:class="title"` frame's text |
| `PptxSlide::notes` | the `presentation:class="notes"` frame of the slide's `presentation:notes` |
| `PptxShape::kind` | `title` → `Title`, `subtitle` → `Subtitle`, `outline` → `Body`; any other text frame or shape with text → `Text`; a frame holding a `table:table` → `Table`, a `draw:image` → `Picture` |
| `PptxShape::x` / `y` / `width` / `height` | `svg:x` / `svg:y` / `svg:width` / `svg:height` (cm, mm, in, pt, pc, px) in EMU - on the page, also inside a group, since ODF places a grouped shape on the page |
| `PptxShape::pictureTarget` | the picture's part (`Pictures/...png`); empty for a flat file's inline picture |
| `PptxShape::altText` | the frame's `svg:desc`, else its `svg:title` |
| `PptxParagraph::level` | how deep the paragraph's `text:list` is nested (0-based) |
| `PptxParagraph::bullet` / `numbered` | the list style at that level: a bullet or a number format; a `text:list-header`, a level with no number format or bullet character, and a paragraph outside a list have none |
| `PptxRun::bold` / `italic` / `underline` | `fo:font-weight` (bold, or 600 and more), `fo:font-style`, `style:text-underline-style` of the span's style, its parent styles and the paragraph's |

## What is read, and what is not

- Slides in document order; every `draw:page` is one, the hidden ones marked.
- Text frames, the drawing shapes that carry text (custom shapes, rectangles,
  ellipses, ...), groups (`draw:g`) and shapes behind a hyperlink (`draw:a`)
  are read through. A shape without text is no shape of the outline.
- White space as ODF has it: a run of spaces, tabs and line ends is one
  space, none at a paragraph's start or end; `text:s`, `text:tab` and
  `text:line-break` stay. A field (page number, date, ...) gives the text it
  holds, a link its text.
- Left out: the header, footer, date and page-number frames, a notes page's
  slide picture, a placeholder still showing its prompt
  (`presentation:placeholder="true"`), embedded objects other than charts
  (and the picture that stands in for them), comments on a page
  (`officeooo:annotation`), and a table's preview picture.
- Charts: a chart object (`draw:object`) read through
  [UltraCanvasOdfChart](UltraCanvasOdfChart.md) - the package's
  `Object N/content.xml`, or a flat file's chart document inline - into the
  shape's `chart`.
- Not read: master pages and styles as drawn (fonts, sizes, colours, fills)
  beyond bold, italic and underline, animations and transitions. Pictures are
  named, not decoded.
- A password-protected deck (`manifest:encryption-data` in the manifest) is
  decrypted with the password `ReadOdpPresentation` and
  `ReadOdpPresentationFromMemory` take (and `LoadTextDocument`'s, which
  `OpenTextDocument` asks for). Without it: "The file is password-protected;
  enter its password to open it"; with a wrong one: "The password is not
  correct". Another OpenDocument type (an `.odt` named `.odp`) is refused
  naming its type.

## Writing a deck

<!-- doc-check: std::string pptxPath; std::string odpPath; void ShowError(const std::string& message); -->
```cpp
#include "UltraCanvasPresentationOdp.h"
#include "UltraCanvasFileLoader.h"

using namespace UltraCanvas;

PptxPresentation deck;
std::string error;
if (ReadPptxPresentation(pptxPath, deck, error, "", true)) {     // a PowerPoint deck, its pictures kept
    if (!WriteOdpPresentation(deck, odpPath, error))            // ... as an Impress deck
        ShowError(error);
}
// Or through the file loader: SavePresentation(deck, path, error), or
// SaveTextDocument(document, "talk.odp", error) for a document laid out as a deck.
```

`WriteOdpPresentation(deck, path, error)` writes:

- one master page of the deck's slide size; each slide a `draw:page` with
  its background and hidden flag (a drawing-page style);
- titles, subtitles and content as presentation frames (`title`,
  `subtitle`, `outline`) at their places; text boxes as frames; any other
  shape as a custom shape - a rectangle, ellipse or rounded rectangle by its
  ODF type, every other preset (`RichShape::ApplyPreset`) and a freeform as
  an enhanced path - with its fill (solid, or a two-colour gradient from the
  first and last stop), outline (width, dash, arrowheads as markers), text
  insets, anchor, wrap, vertical text (the text area turned) and autofit;
- turned shapes as `draw:transform`; a mirrored one mirrored in its own
  outline and gradient before the turn, as PowerPoint mirrors it (ODF's
  `draw:mirror-*` applies after the turn and leaves the fill alone);
- connectors as lines between their ends, or paths, mirrored and turned
  into place;
- pictures from `deck.pictures` (`Pictures/imageN.*`; a picture whose bytes
  were not kept is a grey box with its description) - not cropped: ODF crops
  by the picture's own size, which the model does not carry;
- tables with merged cells (`table:number-columns-spanned` /
  `-rows-spanned`, covered cells), fills, borders, padding and vertical
  alignment; one without cell looks in PowerPoint's default colours;
- charts as chart objects (`Object N/content.xml`,
  [UltraCanvasOdfChart](UltraCanvasOdfChart.md)'s `WriteOdfChartObject`), a
  series without a colour of its own in the Office palette's;
- text: each paragraph's alignment, spacing, margins and line height; a
  bulleted one in a list with a list style of its own (the bullet character
  - a Wingdings or Symbol code as the Unicode character it shows - its
  colour, size and hanging indent; or the number's format, punctuation and
  the number the list has reached); runs with size, colour, typeface, bold,
  italic, underline, strike, super- and subscript, capitals, highlight and
  links (`#slideN` as a jump to that page);
- speaker notes as the page's `presentation:notes`.

The extension names the form: `.odp`, `.otp` (a template, its own media
type) or `.fodp` (one XML file, pictures as `office:binary-data` and charts
inline). `WriteOdpPresentationToMemory` gives the bytes. There is no
password: ODF encryption is read ([UltraCanvasOdfEncryption](UltraCanvasOdfEncryption.md)),
not written, so `UltraCanvasFileLoader::SavePresentation` refuses one for
an `.odp` rather than save it unprotected. Not written: effects, animations,
transitions, comments.

## Test

`Tests/PresentationOdpTest.cpp` reads two fixtures:

- `Tests/fixtures/office-pptx-sample.odp` - the PowerPoint test deck as
  LibreOffice Impress converts it (`Tests/fixtures/make-odp-fixtures.py`), an
  ODF producer independent of UltraCanvas: the 16:9 size, the five slides'
  titles, the subtitle, bullets on two levels with a bold run, the numbered
  pair, the text box at its place, the speaker notes, the table, the
  picture's description and part, the hidden slide and the sixty bullets -
  and, slide by slide, the same titles, notes and bullets the `.pptx` reader
  finds in the original.
- `Tests/fixtures/presentation-features.fodp` - a flat deck written by hand,
  which LibreOffice opens: white space, units, inherited text styles, nested,
  header and unnumbered lists, a line break, a link and a tab, a group, a
  hyperlinked frame, the footer and a prompting placeholder left out, a hidden
  slide, a table with header rows and a covered cell, an embedded chart left
  out, an inline picture, a field, and a page comment left out.

It also checks the refusals (a protected package, an `.odt`, a broken
package, a missing file), that a template is read, and that
`LoadTextDocument`, the Open dialog's types, `ExtractText` and UltraFiler's
preview page take the new types - and that an `.odp` or `.pptx` that is no
deck is refused rather than read as text.

`Tests/PresentationPptxTest.cpp` checks the writer: the PowerPoint feature
deck and the python-pptx sample written as `.odp` and read back (titles,
text and its spaces, bullets and numbers, the table and its covered cell,
the chart object, the hidden slide, the notes, the same outline), the
package's media type and manifest, a flat `.fodp` with its chart inline, a
template's media type, a mirrored arrow's outline and gradient, a document
saved as `.odp` through `SaveTextDocument`, and a password refused. The
decks it writes open in LibreOffice and draw as the originals
(`UC_PPTX_TEST_DUMP=<dir>` keeps them).
