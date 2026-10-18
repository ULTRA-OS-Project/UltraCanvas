# UltraCanvasPresentationPptx — PowerPoint decks (.pptx) as text

`UltraCanvasPresentationPptx.h` reads a PowerPoint presentation - `.pptx`,
and `.pptm` / `.ppsx` / `.potx`, which are the same package - into a UI-free
model of its slides' text: titles, the paragraphs of every placeholder and
text box with their outline levels and bullets, tables, the pictures' names
and descriptions, speaker notes, and which slides are hidden. A
password-protected deck is decrypted on the way in, through the same code as
a protected `.xlsx` or `.docx` ([UltraCanvasOfficeEncryption](UltraCanvasOfficeEncryption.md)).

It is a reader of text, not a slide renderer. There is no writer.

## Opening one as a document

`UltraCanvasFileLoader::LoadTextDocument` and `OpenTextDocument` open a deck
as an outline, and `OpenTextDocument` asks for the password of a protected
one first:

<!-- doc-check: std::string path; std::string password; void ShowDocument(std::shared_ptr<UltraCanvas::UCRichDocument> document); void ShowError(const std::string& message); -->
```cpp
#include "UltraCanvasFileLoader.h"

using namespace UltraCanvas;

std::string error;
if (auto outline = UltraCanvasFileLoader::LoadTextDocument(path, error, password)) {
    ShowDocument(outline);   // e.g. in an UltraCanvasRichTextEdit
} else {
    ShowError(error);        // "The file is password-protected; ...", ...
}
```

The outline: per slide a heading (its number and title, "(hidden)" after a
hidden slide's), its bulleted paragraphs as list items at their level - a
numbered list where the deck numbers them - and its other text as
paragraphs, a subtitle in italics, each table as a table (the first row as
its header), a line `[Picture: <description>]` for each picture, and the
speaker notes as a block quote opening "Notes:"; a rule between slides.

## Reading the model

<!-- doc-check: std::string path; std::string password; void ShowError(const std::string& message); -->
```cpp
#include "UltraCanvasPresentationPptx.h"

using namespace UltraCanvas;

PptxPresentation deck;
std::string error;
if (!ReadPptxPresentation(path, deck, error, password)) {
    ShowError(error);
} else {
    for (const PptxSlide& slide : deck.slides) {
        if (slide.hidden) continue;
        for (const PptxShape& shape : slide.shapes) {
            for (const PptxParagraph& paragraph : shape.paragraphs) {
                std::string line = std::string(paragraph.level * 2, ' ') +
                                   (paragraph.bullet ? "- " : "") + paragraph.Text();
                (void)line;
            }
        }
        for (const PptxParagraph& note : slide.notes) (void)note.Text();
    }
}
UCRichDocument outline = PptxPresentationToRichDocument(deck);
```

| Type | Holds |
|---|---|
| `PptxPresentation` | `slideWidth` / `slideHeight` (EMU: 914 400 per inch; 16:9 is 12 192 000 x 6 858 000), `slides` in presentation order |
| `PptxSlide` | `number` (1-based), `hidden`, `title`, `shapes` back to front, `notes` |
| `PptxShape` | `kind` (`Title`, `Subtitle`, `Body`, `Text`, `Table`, `Picture`), `name`, `x` / `y` / `width` / `height` in EMU, `paragraphs`; a table's `rows` (cells, each a list of paragraphs); a picture's `pictureTarget` (its part, e.g. `ppt/media/image1.png`) and `altText` |
| `PptxParagraph` | `runs`, `level` (0-based), `bullet`, `numbered`; `Text()` joins the runs |
| `PptxRun` | `text` (`"\n"` for a line break), `bold`, `italic`, `underline` |

`IsPptxPackage(path)` tells a deck by its content (a ZIP holding
`ppt/presentation.xml`); a protected deck is a compound file instead, which
`IsEncryptedOfficePackage` tells, and whose type shows only once decrypted -
`IsPptxExtension` names the extensions to go by. `ReadPptxPresentationFromPackage`
reads a package already in memory.

## What is read, and what is not

- Slides in the order `p:sldIdLst` gives them, through the presentation's
  relationships; a slide whose part is missing is skipped.
- Placeholders by type: `title` and `ctrTitle` are the title, `subTitle` the
  subtitle, any other (`body`, `obj`, or none) content, bulleted unless a
  paragraph says `buNone`. A text box is not bulleted unless it says `buChar`
  or `buAutoNum`. The date, footer and slide-number placeholders are slide
  furniture and left out.
- Groups are read through, their shapes in order; their positions stay in the
  group's own coordinates. A shape that takes its place from the layout has
  none (0, 0, 0, 0).
- Speaker notes: the body placeholder of the slide's notes slide.
- Not read: masters, layouts and themes as drawn (fonts, sizes, colours,
  fills), charts, SmartArt, embedded objects, comments, animations and
  transitions. Pictures are named, not decoded.

## Test

`Tests/OfficeEncryptionTest.cpp` reads `Tests/fixtures/office-pptx-sample.pptx`,
which python-pptx - an independent writer - builds from its default template
(`Tests/fixtures/make-office-encrypted-fixtures.py`): the slide size and
order, titles, a subtitle, bullets on two levels with a bold run, a numbered
pair, a text box at its place, speaker notes, a table, a picture's
description and part, a hidden slide, sixty bullets, no slide-number text,
and the outline. `office-agile-encrypted.pptx` is the same deck encrypted;
it is refused without the password, read with it, and opened by
`UltraCanvasFileLoader::LoadTextDocument`.
