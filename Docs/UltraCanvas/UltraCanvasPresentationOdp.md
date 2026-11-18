# UltraCanvasPresentationOdp — OpenDocument decks (.odp) as text

`UltraCanvasPresentationOdp.h` reads an OpenDocument presentation - an
Impress `.odp`, a template `.otp`, or the flat single-XML `.fodp` - into the
same UI-free model as a PowerPoint deck
([UltraCanvasPresentationPptx](UltraCanvasPresentationPptx.md)): the slides in
order, their titles, the paragraphs of every placeholder, text box and shape
with their list levels, bullets and numbering, bold / italic / underline,
tables, the pictures' parts and descriptions, speaker notes, and which slides
are hidden. One model means one outline, one preview and one text extractor
for both kinds of deck.

It is a reader of text, not a slide renderer. There is no writer, and a
password-protected `.odp` is refused with a reason (ODF's own encryption is not
implemented; a protected `.pptx` is read).

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
    ShowError(error);        // "The presentation is password-protected; ...", ...
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
| `ReadOdpPresentation(path, out, error)` | the deck; `path` is UTF-8. False with a sentence in `error` |
| `ReadOdpPresentationFromMemory(bytes, out, error)` | the same for bytes: a package, or a flat file's XML |

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
  (`presentation:placeholder="true"`), embedded objects such as charts (and
  the picture that stands in for them), comments on a page
  (`officeooo:annotation`), and a table's preview picture.
- Not read: master pages and styles as drawn (fonts, sizes, colours, fills)
  beyond bold, italic and underline, animations and transitions. Pictures are
  named, not decoded.
- A password-protected deck (`manifest:encryption-data` on `content.xml`) is
  refused: "The presentation is password-protected; password-protected
  OpenDocument files cannot be opened yet". Another OpenDocument type (an
  `.odt` named `.odp`) is refused naming its type.

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
