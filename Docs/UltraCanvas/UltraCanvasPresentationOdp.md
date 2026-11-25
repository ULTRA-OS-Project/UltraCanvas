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

It reads text, tables, pictures and charts with the look they are drawn
with - fills, gradients, outlines, shapes' outlines, turns, fonts, spacing,
bullets, cell looks and backgrounds (*The look as drawn*, below) - and
`RenderPptxSlide` draws what it reads, as it draws a `.pptx`. `WriteOdpPresentation` writes the model back as an Impress deck -
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
#include "UltraCanvasPptxSlideRender.h"

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
// For drawing the slides: with the pictures' bytes too.
PptxPresentation drawn;
if (ReadOdpPresentation(path, drawn, error, "", /*withPictures*/ true)) {
    auto first = RenderPptxSlide(drawn, 0, 1280);   // UltraCanvasPptxSlideRender.h
}
```

| Function | Does |
|---|---|
| `IsOdpPresentationFile(path)` | true for a ZIP package whose `mimetype` (or manifest) says `application/vnd.oasis.opendocument.presentation` or `-template`, or a flat XML document whose `office:mimetype` says so |
| `IsOdpExtension(extOrPath)` | `odp`, `otp`, `fodp`, with or without a dot, any case |
| `ReadOdpPresentation(path, out, error, password, withPictures)` | the deck; `path` is UTF-8, `password` (optional) opens a protected one, `withPictures` (default false) also keeps the pictures' bytes in `out.pictures`. False with a sentence in `error` |
| `ReadOdpPresentationFromMemory(bytes, out, error, password, withPictures)` | the same for bytes: a package, or a flat file's XML |

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
| `PptxShape::pictureTarget` | the picture's part (`Pictures/...png`); for a flat file's inline picture `Pictures/inlineN` when read `withPictures`, else empty |
| `PptxShape::altText` | the frame's `svg:desc`, else its `svg:title` |
| `PptxParagraph::level` | how deep the paragraph's `text:list` is nested (0-based) |
| `PptxParagraph::bullet` / `numbered` | the list style at that level: a bullet or a number format; a `text:list-header`, a level with no number format or bullet character, and a paragraph outside a list have none |
| `PptxRun::bold` / `italic` / `underline` | `fo:font-weight` (bold, or 600 and more), `fo:font-style`, `style:text-underline-style` of the span's style, its parent styles, the paragraph's and the shape's |

## The look as drawn

Every property is looked up the way ODF inherits it: the span's text style,
the paragraph's style, the shape's text style (`draw:text-style-name`), the
shape's own style (`draw:style-name`, or `presentation:style-name` for a
presentation frame) - each through its `style:parent-style-name` chain - and
last the family's `style:default-style`. A slide sees the common styles and
`content.xml`'s automatic styles; a master page sees the common styles and
`styles.xml`'s own. An outline's paragraph at level *n* takes the
presentation style for its level (`...-outline<n+1>`) in place of the first
level's.

| Field | From |
|---|---|
| `PptxSlide::background` | the page's `draw:fill` / `draw:fill-color` (a gradient's start colour), else its master page's drawing-page style |
| `PptxPresentation::themeColors` | LibreOffice's `loext:theme` colours on the master page, by Office's names (`dk1`, `lt1`, `accent1` ... `hlink`) |
| `PptxShape::x` / `y` / `rotationDeg` | `draw:transform`'s `rotate` (radians, counter-clockwise about the top-left corner) and `translate` (that corner), turned back about the centre; `matrix` too |
| `PptxShape::flipH` / `flipV` | `draw:mirror-horizontal` / `-vertical` of a custom shape; ODF mirrors after the turn, the model before, so a mirror in one direction turns the other way. A line's direction |
| `PptxShape::geometry` | a custom shape's `draw:type`: an Office preset LibreOffice writes as `ooxml-<preset>`, or ODF's own type as the preset it draws (`rectangle`, `round-rectangle`, `ellipse`, `diamond`, `isosceles-triangle`, `right-triangle`, `parallelogram`, `trapezoid`, `hexagon`, `octagon`, `cross`, `star4` ... `star24`, the arrows, `pentagon-right`, `chevron`, `can`, `cube`, the callouts, the flowchart shapes); `draw:rect` with a corner radius, `draw:ellipse` / `draw:circle`; a standard connector stored with only its ends as `bentConnector3` |
| `PptxShape::customPath` | an enhanced path in plain numbers (`M`, `L`, `C`, `Q`, `Z`; `F` leaves it unfilled) over the `svg:viewBox`, or LibreOffice's `drawooo:sub-view-size`; `draw:polygon` / `draw:polyline` points and `draw:path`'s SVG path over their view box; a `draw:regular-polygon`'s corners (concave: a star). An open one is a line (`Connector`) |
| `PptxShape::fill` / `gradientStops` / `gradientAngleDeg` / `gradientRadial` | `draw:fill` solid (`draw:fill-color`), gradient (`draw:fill-gradient-name`: `loext:gradient-stop`s, else the start and end colours at their intensities; axial mirrored; radial, ellipsoid, square and rectangular from the centre, ODF's end colour; the angle - degrees, radians, grads or LibreOffice's bare tenths - back in the shape, its turn and mirroring taken out), a hatch on a solid ground its ground |
| `PptxShape::line` / `lineWidthPt` / `dash` / `arrowStart` / `arrowEnd` | `draw:stroke` solid or dash, `svg:stroke-color`, `svg:stroke-width`, the `draw:stroke-dash` (dots up to 100 % or a millimetre: dotted, else dashed), `draw:marker-start` / `-end` |
| `PptxShape::anchor` / `inset*` / `wrap` / `textRotation` / `fontScale` | `draw:textarea-vertical-align`, `fo:padding(-side)`, `fo:wrap-option="no-wrap"`, `writing-mode` (`tb-rl`: 90, `bt-lr`: 270) or the enhanced geometry's `draw:text-rotate-angle`, `style:shrink-to-fit` (a scale just under 1, so the renderer shrinks it where it does not fit) |
| `PptxShape::placeholder` | `title`, `subTitle`, `body` for the title, subtitle and outline frames (which the renderer shrinks to fit) |
| `PptxRun::sizePt` / `color` / `font` | `fo:font-size` (points, or a share of the size under it), `fo:color` (`style:use-window-font-color`: none of its own), `style:font-name` through the font-face declarations, else `fo:font-family` |
| `PptxRun::strike` / `baseline` / `allCaps` / `smallCaps` / `highlight` / `hyperlink` | `style:text-line-through-style`, `style:text-position` (`super`, `sub`, or the raise in per cent), `fo:text-transform="uppercase"`, `fo:font-variant="small-caps"`, `fo:background-color`, a `text:a`'s `xlink:href` (`#name` of a page: `#slideN`) |
| `PptxParagraph::align` / `spaceBefore` / `spaceAfter` / `lineSpacing` / `marginLeft` / `indent` | `fo:text-align`, `fo:margin-top` / `-bottom`, `fo:line-height` (per cent or a length; `style:line-height-at-least`), `fo:margin-left`, `fo:text-indent` - a list label hanging by `text:space-before` + `text:min-label-width`, or `style:list-level-label-alignment`'s margin and indent |
| `PptxParagraph::bulletChar` / `bulletFont` / `bulletColor` / `bulletSizePercent` | the list level's `text:bullet-char` (a symbol font's private-use character U+F0xx as its code, as PowerPoint keeps it), its text properties' font, colour and share of the size |
| `PptxParagraph::numberScheme` / `numberStartAt` | the level's `style:num-format`, `num-prefix` and `num-suffix` (`alphaLcParenR` ...); the number each item shows - the level's `text:start-value`, an item's own, counted on through the list and a list that continues another - set so that the renderer, counting on, shows it |
| `PptxShape::columnWidths` / `rowHeights` | the columns' and rows' styles (`style:column-width`, `style:row-height` or `min-row-height`) |
| `PptxShape::cellStyles` | each cell's style (else its row's or column's default cell style) over the table template's (`table:template-name`, with the first and last row and column and banding flags): `draw:fill-color` in its graphic properties or `fo:background-color`, `fo:border(-side)` in its table-cell or paragraph properties, padding, `draw:textarea-vertical-align` or `style:vertical-align`, `table:number-columns-spanned` / `-rows-spanned`; a covered cell `mergedAway`. The cell's text properties reach its runs |

## What is read, and what is not

- Slides in document order; every `draw:page` is one, the hidden ones marked.
- Text frames, drawing shapes (custom shapes, rectangles, ellipses, polygons,
  paths, ...), lines and connectors and shapes behind a hyperlink
  (`draw:a`) are read through. A group (`draw:g`) is kept: its shapes, on
  the page already, each with the group in its `groupPath` and its
  `draw:name` in the slide's `groupNames`. A shape with neither text, fill
  nor outline is left out.
- A picture's crop: its graphic style's `fo:clip="rect(top, right, bottom,
  left)"`, lengths of the picture at its own size - its pixels at the
  resolution it states, else 96 to the inch (`PptxPictureNaturalSize`), as
  LibreOffice measures it - as the shares `cropLeft` ... `cropBottom`.
- White space as ODF has it: a run of spaces, tabs and line ends is one
  space, none at a paragraph's start or end; `text:s`, `text:tab` and
  `text:line-break` stay. A field (page number, date, ...) gives the text it
  holds, a link its text.
- An empty title, subtitle or outline frame, and one still showing its
  prompt (`presentation:placeholder="true"`, whose text is the prompt, not
  content), is kept as an empty placeholder - an editor types into it - in
  its place, with one empty paragraph whose run has the look its styles give.
- Left out: the header, footer, date and page-number frames, a notes page's
  slide picture, embedded objects other than charts
  (and the picture that stands in for them), comments on a page
  (`officeooo:annotation`), and a table's preview picture.
- Charts: a chart object (`draw:object`) read through
  [UltraCanvasOdfChart](UltraCanvasOdfChart.md) - the package's
  `Object N/content.xml`, or a flat file's chart document inline - into the
  shape's `chart`.
- The design: each master page into `PptxPresentation::masters` (its
  display name, background and placeholder frames - title, subtitle,
  outline, date, footer, number; an empty one, as Impress leaves its own,
  gets Impress's prompt text); each `style:presentation-page-layout` into
  `layouts`, on the master page its slides use it with (one layout per
  master when slides use it on several; the first master when none does),
  its placeholders where the page layout puts them with the look of the
  master's frame of their class, named and typed from what they hold
  ("Title and Content", `obj`) unless the file gives a display name; a deck
  with no page layouts gets a layout per master page from its frames, and a
  slide on no page layout a blank one. Each slide's `layoutIndex` follows its
  `presentation:presentation-page-layout-name` and master page.
- Transitions: the drawing-page style's `smil:type` (`fade`, `pushWipe` ->
  Push, `barWipe` -> Wipe, `barnDoorWipe` -> Split, `slideWipe` -> Cover,
  `zoom`; any other as Fade; ODF 1.0's `presentation:transition-style` as
  Fade), `presentation:transition-speed`, the length from the page's
  `anim:transitionFilter` `smil:dur` where Impress keeps it, and
  `presentation:transition-type="automatic"` with `presentation:duration`
  (`PT5S`) as `advanceAfterSec`. ODF has no "only after the time": a slide
  always moves on with a click as well.
- Not read: master pages' shapes other than placeholders (a logo on every slide), shadows and
  other effects, bitmap and hatch fills (a hatch on a solid ground keeps the
  ground), outlines built from formulas (an Office preset still draws by its
  name; an ODF type the list above does not name draws as a box),
  animations, a transition's direction. Pictures are kept as bytes
  (`withPictures`), not decoded.
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

- the deck's masters as master pages of the deck's slide size (one plain
  `Default` for a deck without masters): name and display name, background,
  placeholder frames in the master's styles (the date, footer and number as
  Impress's fields), and `<master>-title` / `-subtitle` / `-outline1..9`
  presentation styles carrying their look, which the slides' frames take as
  parents;
- the deck's layouts as presentation page layouts (`UCL1` ..., with the
  layout's name as display name), each slide on its layout (-1: the first)
  and that layout's master page;
- each slide's transition in its drawing-page style (smil type and subtype,
  speed, `presentation:transition-type="automatic"` with
  `presentation:duration`) and its exact length as the page's
  `anim:transitionFilter`, as Impress writes it;
- each slide a `draw:page` with
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
- groups: a `draw:g` round the shapes of each `groupPath`, nested, its
  `draw:name` from `groupNames`;
- pictures from `deck.pictures` (`Pictures/imageN.*`; a picture whose bytes
  were not kept is a grey box with its description), cropped by `fo:clip`
  in lengths of the picture's own size (`PptxPictureNaturalSize`);
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
a transition's direction, comments.

## Test

`Tests/PresentationOdpTest.cpp` reads two fixtures and a deck of its own:

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
  hyperlinked frame, the footer left out and a prompting placeholder kept
  empty, a hidden
  slide, a table with header rows and a covered cell, an embedded chart left
  out, an inline picture, a field, and a page comment left out.
- a flat deck written in the test for the look as drawn: a style through its
  parent and the family default, a share of the default's size, a font face,
  ODF's own shape types, a turn about the corner, a mirrored enhanced path, a
  rounded rectangle, an ellipse, a line drawn right to left with a marker, a
  polyline, an SVG path with a quadratic curve, a regular polygon, a dotted
  outline, radial and linear gradients (bare tenths of a degree), numbering
  placed by label alignment and continued across lists, a link to a page by
  its name, a table template, backgrounds from the page and the master page.

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
template's media type, a mirrored arrow's outline and gradient (turned with
it), a document saved as `.odp` through `SaveTextDocument`, and a password
refused; the feature deck read back with its look (the star's turn, outline,
gradient and dashes; typefaces, strike, superscript, the link, the bullet's
colour, numbering from c)) and drawn as the original. The decks it writes
open in LibreOffice and draw as the originals (`UC_PPTX_TEST_DUMP=<dir>`
keeps them).

It also reads `Tests/fixtures/pptx-features.odp` - LibreOffice's own
conversion of the PowerPoint feature deck (`make-odp-fixtures.py`) - against
what the `.pptx` reader makes of the original, field by field (the title's
size and font, runs' typefaces, strike, superscript, capitals, highlight and
link, the Wingdings bullet, numbering from c), vertical text, the star's
preset, turn, mirroring, gradient and dash, the arrowed and the bent
connector, the freeform, the table's widths, fills, borders and merge, the
picture's bytes, the theme's colours) and slide by slide as drawn.
