# UltraCanvasPresentationPptx — PowerPoint decks (.pptx) as text and slides

`UltraCanvasPresentationPptx.h` reads a PowerPoint presentation - `.pptx`,
and `.pptm` / `.ppsx` / `.potx`, which are the same package - into a UI-free
model of its slides' text: titles, the paragraphs of every placeholder and
text box with their outline levels and bullets, tables, the pictures' names
and descriptions, speaker notes, and which slides are hidden. A
password-protected deck is decrypted on the way in, through the same code as
a protected `.xlsx` or `.docx` ([UltraCanvasOfficeEncryption](UltraCanvasOfficeEncryption.md)).

For drawing a slide it also resolves what each shape takes from its layout,
master and theme - place, text sizes, colours and alignment, anchor, fill,
outline, the background - and `UltraCanvasPptxSlideRender.h` draws a slide
from that as a picture (*Drawing a slide*, below). There is no writer.

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

UltraFiler's preview page (`UltraCanvasFilerWidget::TextPreviewLines`) shows
the same outline: the first page of a deck's slide text on its tile and in
the detail pane. A protected deck keeps its type glyph, since the preview has
no password to ask for.

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
| `PptxPresentation` | `slideWidth` / `slideHeight` (EMU: 914 400 per inch; 16:9 is 12 192 000 x 6 858 000), `slides` in presentation order, `themeColors` (`dk1`, `lt1`, `accent1` ... as `"RRGGBB"`), `pictures` (bytes by part, read `withPictures`) |
| `PptxSlide` | `number` (1-based), `hidden`, `title`, `shapes` back to front, `notes`, `background` and `textColor` (`"RRGGBB"`, empty: white / black) |
| `PptxShape` | `kind` (`Title`, `Subtitle`, `Body`, `Text`, `Table`, `Picture`), `name`, `x` / `y` / `width` / `height` in EMU (`placeInherited` when from the layout or master), `placeholder` (its type), `anchor`, `fontScale` (autofit), `fill`, `line` and `lineWidthPt`, `geometry`, `paragraphs`; a table's `rows` (cells, each a list of paragraphs), `columnWidths` and `rowHeights`; a picture's `pictureTarget` (its part, e.g. `ppt/media/image1.png`) and `altText` |
| `PptxParagraph` | `runs`, `level` (0-based), `bullet`, `numbered`, `align`; `Text()` joins the runs |
| `PptxRun` | `text` (`"\n"` for a line break), `bold`, `italic`, `underline`, `sizePt` and `color` (resolved; 0 / empty when nothing says) |

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
- Groups are read through, their shapes in order and placed on the slide (the
  group's offset and scaling applied). A placeholder that names no place
  takes its layout's, then its master's (matched by `idx`, then by type; a
  centred title and a subtitle fall back to the master's title and body);
  `placeInherited` says so. One found nowhere keeps 0, 0, 0, 0.
- A run's size, colour and its paragraph's alignment resolve from the run,
  then the shape's list style, the layout's and master's placeholder, the
  master's title or body text style (a text box: the deck's default text
  style), the colour of a shape's style (`fontRef`) coming before the master's.
  Theme colours (`schemeClr`) go through the master's colour map and the
  theme, with `lumMod`, `lumOff`, `tint` and `shade` applied. A shape with a
  fill or outline and no text is kept (a coloured bar, a circle).
- Speaker notes: the body placeholder of the slide's notes slide.
- Not read: theme fonts, gradient, picture and pattern fills, effects,
  charts, SmartArt, embedded objects, comments, animations and transitions.
  Pictures are named; their bytes are kept with `withPictures`, not decoded.

## Drawing a slide

`RenderPptxSlide(deck, index, widthPx)` (`UltraCanvasPptxSlideRender.h`)
draws a slide of a deck read `withPictures` as a `UCPixmap`, `widthPx` wide
and as high as the deck's slide shape: the background, then the shapes back
to front - rectangles, rounded rectangles and ellipses in their fill and
outline, pictures stretched to their box (a grey box with the description
when one cannot be decoded), tables in the theme's first accent (a header row
and banded rows), and text in its box's insets at its sizes, colours and
alignment, with bullets ("•", "–" on every second level) and numbers, at its
anchor. Text in a placeholder too tall for its box is shrunk until it fits,
down to 40 %. The text is set in the system font. UltraClipboard previews a
copied deck's slides with it.

<!-- doc-check: std::string path; void Show(std::shared_ptr<UltraCanvas::UCPixmap> slide); -->
```cpp
#include "UltraCanvasPptxSlideRender.h"

using namespace UltraCanvas;

PptxPresentation deck;
std::string error;
if (ReadPptxPresentation(path, deck, error, "", true)) {   // withPictures
    for (size_t i = 0; i < deck.slides.size(); ++i)
        Show(RenderPptxSlide(deck, i, 1280));                 // 1280 x 720 at 16:9
}
```

It is callable from a worker thread: the drawing is serialized inside, as the
file previews' pages are.

## Test

`Tests/OfficeEncryptionTest.cpp` reads `Tests/fixtures/office-pptx-sample.pptx`,
which python-pptx - an independent writer - builds from its default template
(`Tests/fixtures/make-office-encrypted-fixtures.py`): the slide size and
order, titles, a subtitle, bullets on two levels with a bold run, a numbered
pair, a text box at its place, speaker notes, a table, a picture's
description and part, a hidden slide, sixty bullets, no slide-number text,
and the outline; then what a slide is drawn with - the subtitle's place from
its layout, the title's 44 pt and centring and the body's 32 / 28 pt from the
master, the theme's colours, the master's background and text colour, the
subtitle's tinted colour - and `RenderPptxSlide` drawing the first slide at
640 x 360. `office-agile-encrypted.pptx` is the same deck encrypted;
it is refused without the password, read with it, and opened by
`UltraCanvasFileLoader::LoadTextDocument`. The same test checks UltraFiler's
preview page: the plain deck's slides, titles first, and no page for the
protected deck or a protected `.docx`.
