# UltraCanvasPresentationPptx — PowerPoint decks (.pptx) as text and slides

`UltraCanvasPresentationPptx.h` reads a PowerPoint presentation - `.pptx`,
and `.pptm` / `.ppsx` / `.ppsm` / `.potx` / `.potm`, which are the same
package - into a UI-free
model of its slides' text: titles, the paragraphs of every placeholder and
text box with their outline levels and bullets, tables, the pictures' names
and descriptions, speaker notes, and which slides are hidden. A
password-protected deck is decrypted on the way in, through the same code as
a protected `.xlsx` or `.docx` ([UltraCanvasOfficeEncryption](UltraCanvasOfficeEncryption.md)).

For drawing a slide it also resolves what each shape takes from its layout,
master and theme - place, the text's size, colour, font, alignment, spacing,
indents and bullets, anchor and insets, fill, outline, outline shape, turn
and mirroring, the background - and reads connectors, styled tables with
merged cells, cropped pictures, charts and SmartArt.
`UltraCanvasPptxSlideRender.h` draws a slide from that as a picture
(*Drawing a slide*, below), and `WritePptxPresentation` writes the model
back (*Writing a deck*, below).

An OpenDocument presentation (`.odp`, `.otp`, `.fodp`) is read into the same
model by [UltraCanvasPresentationOdp](UltraCanvasPresentationOdp.md), so the
outline, the preview and the text extraction below serve both kinds of deck.

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
the same outline in the detail pane; the file's thumbnail is its first slide
as drawn. A protected deck keeps its type glyph, since the preview has no
password to ask for. UltraTexter opens a deck as the outline in its document
editor; saved as a new `.pptx` the outline comes back as its slides, and it
is never written over the deck it was opened from. UltraViewer (MediaViewer)
shows a deck as its slides, through
[UltraCanvasPresentationPages](UltraCanvasPresentationPages.md), with a
slide show.

A file named `.pptx` (or `.odp`) that is no deck - damaged, or something else
renamed - is refused by `LoadTextDocument` ("The file is not a valid
PowerPoint presentation") rather than read as text.

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
| `PptxPresentation` | `slideWidth` / `slideHeight` (EMU: 914 400 per inch; 16:9 is 12 192 000 x 6 858 000), `slides` in presentation order, `themeColors` (`dk1`, `lt1`, `accent1` ... as `"RRGGBB"`), `majorFont` / `minorFont` (the theme's heading and body typefaces), `masters` and `layouts` (the design, in the file's order), `pictures` (bytes by part, read `withPictures`) |
| `PptxSlide` | `number` (1-based), `layoutIndex` (into `layouts`; -1: none known), `hidden`, `title`, `shapes` back to front, `notes`, `background` and `textColor` (`"RRGGBB"`, empty: white / black), `transition` |
| `PptxMaster` | `name`, `placeholders` (title, body, date, footer, number), `background` |
| `PptxLayout` | `name` ("Title and Content"), `type` (`p:sldLayout/@type`: `title`, `obj`, `twoObj`, `titleOnly` ...), `masterIndex`, `placeholders` (each with its `placeholder` type and prompt text), `background` (empty: the master's) |
| `PptxTransition` | `kind` (`PptxTransitionKind`: `NoTransition`, `Fade`, `Push`, `Wipe`, `Split`, `Cover`, `Zoom`), `durationSec`, `advanceOnClick`, `advanceAfterSec` (-1: never by itself) |
| `PptxShape` | `kind` (`Title`, `Subtitle`, `Body`, `Text`, `Table`, `Picture`, `Connector`, `Chart`), `name`, `x` / `y` / `width` / `height` in EMU (`placeInherited` when from the layout or master), `rotationDeg`, `flipH` / `flipV`, `placeholder` (its type), `anchor`, `fontScale` (autofit), the text's `insetLeft` ... `insetBottom` (EMU, -1: PowerPoint's own), `wrap`, `textRotation` (90 / 270 for vertical text), `fill` (and a gradient's `gradientStops`, `gradientAngleDeg`, `gradientRadial`), `line`, `lineWidthPt`, `dash`, `arrowStart` / `arrowEnd`, `geometry` (the preset) or `customPath` (a freeform's outline, 0..1 across the box), `paragraphs`; a table's `rows` (cells, each a list of paragraphs), `cellStyles` (`PptxCellStyle` each), `columnWidths` and `rowHeights`; a picture's `pictureTarget` (its part, e.g. `ppt/media/image1.png`), `altText` and crop (`cropLeft` ... `cropBottom`, a share of the picture); a chart's `chart` (`RichChart`) |
| `PptxCellStyle` | a table cell's `fill`, `textColor` and `bold` (from the table style), its four borders (`color`, `widthPt`), `gridSpan` / `rowSpan`, `mergedAway` (covered by a merged cell), insets and `anchor` |
| `PptxParagraph` | `runs`, `level` (0-based), `bullet`, `numbered`, `align`, `spaceBefore` / `spaceAfter` / `lineSpacing` (`PptxSpacing`: a share of a line or points), `marginLeft` and `indent` (EMU), `bulletChar`, `bulletFont`, `bulletColor`, `bulletSizePercent`, `numberScheme` (`a:buAutoNum`'s type) and `numberStartAt`; `Text()` joins the runs |
| `PptxRun` | `text` (`"\n"` for a line break), `bold`, `italic`, `underline`, `strike`, `baseline` (per cent: > 0 superscript, < 0 subscript), `allCaps` / `smallCaps`, `highlight`, `hyperlink` (a URL, or `#slide3` for a jump within the deck), `sizePt`, `color` and `font` (resolved; 0 / empty when nothing says) |

`PptxNumberLabel(scheme, n)` gives the label PowerPoint shows for item `n`
of a numbering scheme: `"3."` (`arabicPeriod`), `"c)"` (`alphaLcParenR`),
`"(iv)"` (`romanLcParenBoth`), `"BB."` for the 28th of `alphaUcPeriod`.

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
  furniture and left out. A placeholder's `placeholder` is its `p:ph` type;
  one that names none (`<p:ph idx="1"/>`, ECMA's `obj`) reads as `"body"`.
  An empty placeholder is kept - an editor shows its prompt and types into
  it - in its place, with one empty paragraph whose run keeps the size
  `a:endParaRPr` (else its layout and master) gives.
- Groups are read through, their shapes in order and placed on the slide (the
  group's offset and scaling applied). Connectors (`p:cxnSp`) are read as
  lines: their outline (`line`, a bent or curved connector's preset), dash
  and arrowheads. A placeholder that names no place
  takes its layout's, then its master's (matched by `idx`, then by type; a
  centred title and a subtitle fall back to the master's title and body);
  `placeInherited` says so. One found nowhere keeps 0, 0, 0, 0.
- A run's size, colour, typeface, bold and italic, and its paragraph's
  alignment, spacing, margin, indent and bullet (`buNone`, `buChar` with its
  `buFont`, `buClr` and `buSzPct`, `buAutoNum` with its scheme and start)
  resolve from the run and paragraph, then the shape's list style, the
  layout's and master's placeholder, the master's title or body text style
  (a text box: the deck's default text style), the colour of a shape's style
  (`fontRef`) coming before the master's. `+mj-lt` and `+mn-lt` name the
  theme's heading and body fonts; a run with no typeface anywhere takes the
  body font.
  Theme colours (`schemeClr`) go through the master's colour map and the
  theme, with `lumMod`, `lumOff`, `tint` and `shade` applied. A shape with a
  fill or outline and no text is kept (a coloured bar, a circle).
- A shape's turn and mirroring (`a:xfrm` `rot`, `flipH`, `flipV`), its
  preset outline or its custom one (`a:custGeom`: lines, curves and arcs, as
  one path), a gradient fill (`a:gradFill`: its stops, a linear angle or a
  round one), its outline's dash (`a:prstDash`) and arrowheads (`headEnd`,
  `tailEnd`), and its text's insets, `wrap="none"` and vertical text
  (`vert`).
- Tables: the table style the table names (`a:tableStyleId`, read from
  `ppt/tableStyles.xml`; one the package does not define is PowerPoint's
  built-in *Medium Style 2* in the accent its id names), its parts as the
  table's flags (`firstRow`, `bandRow` ...) switch them on - whole table,
  bands, edge columns, edge rows, the narrower over the wider - then each
  cell's own fill, borders (`a:lnL` ...), margins and anchor; `gridSpan`,
  `rowSpan`, `hMerge` and `vMerge` for merged cells.
- Pictures: their crop (`a:srcRect`), outline and outline shape. A picture
  in a placeholder, or the picture an embedded object (OLE) shows, too.
- Charts (`c:chart` in a graphic frame): the chart part read by
  [UltraCanvasDrawingML](UltraCanvasDrawingML.md), as the `.docx` reader
  reads its charts - kind, series, categories, title, legend, axis ranges.
- SmartArt: the drawing PowerPoint saves beside a diagram's data (the
  `dsp:drawing` part the data model's `dsp:dataModelExt` names), read as
  ordinary shapes placed in the diagram's frame. A diagram saved without
  that drawing shows nothing.
- Hyperlinks (`a:hlinkClick`): an external target as its URL, a jump to
  another slide as `#slide<n>`.
- Speaker notes: the body placeholder of the slide's notes slide.
- `mc:AlternateContent` (Markup Compatibility, ECMA-376 Part 3), which
  PowerPoint wraps around what an older reader may not know - an equation,
  a newer chart, a 3D model, ink: the first `mc:Choice` that gives a shape
  (or, inside a paragraph, text) is read, else the `mc:Fallback`; never both.
  An equation (`a14:m`) reads as the text of its `m:t` runs ("p=r−c"), a
  chart the reader cannot read as the picture its Fallback holds.
- Not read: picture and pattern fills, effects (shadows, glow, 3D), comments,
  animations, a transition's direction and sound, embedded objects beyond
  the picture they show, video and audio beyond the poster frame. Pictures are named; their bytes
  are kept with `withPictures`, not decoded.

## Masters, layouts and transitions

The reader reads the deck's design as well as its slides:

- every slide master (`p:sldMasterIdLst`) into `masters` and every layout
  it lists into `layouts`, in the file's order: the name (`p:cSld/@name`),
  the layout's `type`, its master, the background, and the placeholders
  resolved as a slide's are - place, kind, text look - keeping their prompt
  text ("Click to edit Master title style") and the date, footer and slide
  number;
- each slide's `layoutIndex`, from its relationship to its layout;
- `p:transition`, also inside `mc:AlternateContent` (PowerPoint 2010's
  `p14` kinds and lengths): the kind (one the model has none for, such as
  `p14:vortex`, reads as `Fade`; `p:cut` as `NoTransition`), the length
  (`p14:dur` in milliseconds, else `spd`: slow 1 s, med 0.75 s, fast 0.5 s),
  `advClick` and `advTm`.

`PptxSlideFromLayout(deck, layoutIndex)` makes a new slide from a layout:
its placeholders without the date, footer and number, emptied but keeping
their look and place (a placeholder with no place of its own takes its
master's), and the layout's background:

```cpp
#include "UltraCanvasPresentationPptx.h"
using namespace UltraCanvas;

void AddSlide(PptxPresentation& deck, int layoutIndex) {
    PptxSlide slide = PptxSlideFromLayout(deck, layoutIndex);
    slide.number = static_cast<int>(deck.slides.size()) + 1;
    slide.transition.kind = PptxTransitionKind::Fade;
    slide.transition.durationSec = 2.0;
    slide.transition.advanceAfterSec = 5.0;   // moves on by itself after 5 s
    deck.slides.push_back(slide);
}
```

## Drawing a slide

`RenderPptxSlide(deck, index, widthPx)` (`UltraCanvasPptxSlideRender.h`)
draws a slide of a deck read `withPictures` as a `UCPixmap`, `widthPx` wide
and as high as the deck's slide shape: the background, then the shapes back
to front, each turned and mirrored as it says:

- outlines from `RichShape::ApplyPreset` (rectangles, rounded rectangles,
  ellipses, triangles, arrows, stars, polygons, callouts, flowchart shapes,
  bent and curved connectors) or the shape's custom path, in a solid or
  gradient fill, outlined solid, dashed or dotted, with arrowheads on a
  line's ends;
- pictures cropped as the deck crops them, clipped to their outline (a grey
  box with the description when one cannot be decoded);
- tables in their cells' fills and borders, merged cells spanning;
- charts drawn by the chart engine (`DrawRichDocumentChart`), laid out at
  their size on the slide and scaled; a build without the engine draws a box
  with the chart's title;
- text in its box's insets at its sizes, colours and typefaces, aligned,
  spaced and indented as its paragraphs say, the bullet in its character
  (a Wingdings or Symbol code drawn as the Unicode character it shows), colour
  and size hanging in the indent, numbers in their scheme; strike, super- and
  subscript, capitals, highlight, and links in the theme's link colour,
  underlined; vertical text turned, `wrap="none"` text on one line. Text in a
  placeholder too tall for its box is shrunk until it fits, down to 40 %.

A typeface the system does not have falls back as the text layout's font
matching chooses. UltraClipboard previews a copied deck's slides with it,
UltraViewer shows them, and the Filer's thumbnail of a deck is its first
slide.

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

## Writing a deck

<!-- doc-check: std::string path; std::string outPath; void ShowError(const std::string& message); -->
```cpp
#include "UltraCanvasPresentationPptx.h"
#include "UltraCanvasFileLoader.h"

using namespace UltraCanvas;

PptxPresentation deck;
std::string error;
if (ReadPptxPresentation(path, deck, error, "", true)) {   // withPictures: they are written back
    deck.slides[0].title = "Revised";
    deck.slides[0].hidden = false;
    if (!UltraCanvasFileLoader::SavePresentation(deck, outPath, error))   // or WritePptxPresentation
        ShowError(error);
}

// A document as a deck: a slide per first-level heading.
UCRichDocument document;
PptxPresentation laidOut = PptxPresentationFromRichDocument(document);
```

`WritePptxPresentation(deck, path, error, password)` writes the model as a
package PowerPoint, LibreOffice and the reader open:

- the slides in order, their size, background and hidden flag, and their
  transitions (`p:transition` with `advClick` / `advTm`; a length no speed
  names in `p14:dur`, inside `mc:AlternateContent` with the speed alone as
  its Fallback, as PowerPoint writes it); a theme with the deck's colours
  and heading and body fonts;
- a deck with `layouts`: its own masters and layouts - names, types,
  backgrounds, placeholders with their look as a list style and their prompt
  text, the date and slide number as fields, a blank layout for a master
  that has none - each slide linked to its `layoutIndex` (-1: the first)
  with its placeholders naming the layout's (`p:ph` type and `idx`); a deck
  without (one made in memory, `PptxPresentationFromRichDocument`): one
  slide master and a "Title and Content" layout;
- every shape kind at its place, turned and mirrored, in its preset or custom
  outline, solid or gradient fill and line (width, dash, arrowheads);
  titles, subtitles and content as placeholders (with their place written
  out, so they stand where they stood whatever layout they were read with);
- text with its insets, wrap, vertical text, anchor and autofit scale;
  paragraphs with level, alignment, spacing, margin, indent and bullet
  (character, font, colour, size) or numbering (scheme, start); runs with
  size, colour, typeface, bold, italic, underline, strike, baseline,
  capitals, highlight and hyperlinks (`#slideN` as a jump to that slide);
- tables with their cells' merges (`gridSpan`, `rowSpan`, `hMerge`, `vMerge`),
  fills, borders, margins and anchors; a table without cell looks (from the
  OpenDocument reader) takes PowerPoint's default table style;
- pictures from `deck.pictures` with their crop - read the deck
  `withPictures`, or a picture is written as a grey box with its
  description; charts as chart parts ([UltraCanvasDrawingML](UltraCanvasDrawingML.md)'s
  `WriteDrawingMLChart`, values as literals, no workbook); speaker notes.

The extension names the package kind: `.pptx`, `.potx` (a template),
`.ppsx` (a show), `.pptm` / `.potm` / `.ppsm` (macro-enabled, with no
macros). With a password the package is encrypted as PowerPoint's *Encrypt
with Password* saves it ([UltraCanvasOfficeEncryption](UltraCanvasOfficeEncryption.md)).
`WritePptxPresentationToPackage` gives the package in memory.
`UltraCanvasFileLoader::SavePresentation` writes a deck through the file
loader, and `SaveTextDocument` with a `.pptx` path lays the document out
first.

`PptxPresentationFromRichDocument(document)` lays a document out as a 16:9
deck: each first-level heading starts a slide and is its title (a rule or a
page break starts an untitled one; the document's title heads the first);
paragraphs, list items (levels; numbering in its scheme; a list's own bullet
character), lower headings (bold), code (Courier New) and block quotes
(italic) fill the content placeholder - set smaller when they would not fit,
as PowerPoint's autofit sets them - and tables and pictures stand one under
the other below it. A deck's own outline reads back as its slides: "1. Title
(hidden)" as the title of a hidden slide, a "Notes:" quote as its speaker
notes.

Not written: effects, animations, a transition's direction, comments,
embedded objects and media (they are not in the model).

## Test

`Tests/PresentationLayoutsRoundTripTest.cpp` saves a deck with one master,
two layouts and transitions (Fade over 2 s moving on after 5 s, Push) as
`.pptx` and `.odp` and reads it back - names, types, placeholder kinds, each
slide's layout, the transitions, the package's parts and ids - checks the
standard set for a deck without layouts, `PptxSlideFromLayout` on a read
deck, the LibreOffice-made `pptx-features` fixtures, and (with `soffice`
installed) LibreOffice converting our `.pptx` to `.odp` and our `.odp` to
`.pptx`.

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
640 x 360; and the same deck rebuilt in memory with an equation shape, a
chart with a picture as its fallback and an equation inside a paragraph,
each read once. `office-agile-encrypted.pptx` is the same deck encrypted;
it is refused without the password, read with it, and opened by
`UltraCanvasFileLoader::LoadTextDocument`. The same test checks UltraFiler's
preview page: the plain deck's slides, titles first, and no page for the
protected deck or a protected `.docx`.

`Tests/PresentationPptxTest.cpp` reads `Tests/fixtures/pptx-features.pptx`,
which python-pptx writes (`Tests/fixtures/make-pptx-features-fixture.py`):
typefaces and the theme's body font, strike, superscript, capitals, a
highlight, a hyperlink, a Wingdings bullet with its colour and size, an
alphabetic numbering from 3, spacing, margins and hanging indents, insets,
vertical text; a star turned, mirrored, filled with a gradient and outlined in
dashes; a connector with an arrowhead and a bent one; a freeform outline; a
table with merged cells, a filled cell with its own border and its style's
header row; a chart; a cropped picture. A deck rebuilt in memory adds a
SmartArt diagram with its drawing, a hyperlink to another slide and a table
style from `ppt/tableStyles.xml`. Each slide is drawn and looked at where the
shapes are (`UC_PPTX_TEST_DUMP=<dir>` writes the drawn slides out).

The same test writes decks and reads them back: the feature deck field by
field (fonts, effects, bullets, spacing, insets, vertical text, the star's
turn, gradient and dashes, the connector's arrowhead, the freeform, the
merged and styled table cells, the chart, the cropped picture's bytes) and
drawn - each slide drawn from the written deck is the same picture; the
python-pptx sample's titles, hidden slide, notes and outline; a copy saved
with a password (refused without it); a template's content type; an empty
deck; a document laid out as a deck, and a deck's outline laid out as its
slides again; `UltraCanvasFileLoader::SaveTextDocument` writing a `.pptx`.
The decks it writes open in python-pptx and LibreOffice
(`UC_PPTX_TEST_DUMP=<dir>` keeps them).
