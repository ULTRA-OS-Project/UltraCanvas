# UltraCanvas Slide Editing: SlideEdit, SlideDeckEditor

<!-- doc-check: std::shared_ptr<PptxPresentation> deck; UltraCanvasWindow* window; -->

## Contents

## Overview

The **slide editor** edits a presentation: the deck model that the
PowerPoint and OpenDocument readers and writers share
([`UltraCanvasPresentationPptx.h`](../../UltraCanvas/include/UltraCanvasPresentationPptx.h)),
drawn by the same slide painter as the thumbnails, the show and PDF export.
It follows the rich-text editor's split: a core without a window holds the
deck, the selection and the history, and an element draws it and turns the
pointer and the keys into the core's commands. The design is
[`Docs/Research/UltraOfficeDesignProposal.md`](../Research/UltraOfficeDesignProposal.md),
§5.3; UltraOffice's presentation tab is its first host.

| Class | What it is | Files |
|---|---|---|
| `PptxPresentation` (`UCPresentation`) | The deck: slides of shapes in EMU, masters and layouts, transitions, the pictures' bytes | `include/UltraCanvasPresentationPptx.h` |
| `UCSlideDeckEditor` | The editing core: the current slide, the selection, every change as a command with undo and redo, hit testing, handles, snapping | `include/UltraCanvasSlideDeckEditor.h`, `core/UltraCanvasSlideDeckEditor.cpp` |
| `UltraCanvasSlideEdit` | The element: the slide strip, the slide, the selection's handles, dragging, the keys | `include/UltraCanvasSlideEdit.h`, `core/UltraCanvasSlideEdit.cpp` |
| `DrawPptxSlide` | A slide on any render context, into a rectangle; prompt text for empty placeholders | `include/UltraCanvasPptxSlideRender.h` |
| `PresentationPrintPages` | A page per slide for PDF and printers | `include/UltraCanvasPresentationPrint.h` |

## The core

```cpp
#include "UltraCanvasSlideDeckEditor.h"

auto deck = std::make_shared<PptxPresentation>();
UCSlideDeckEditor editor(deck);
editor.NewSlide(-1);                       // Title and Content (or the current slide's layout)
editor.SetShapeText(0, "Quarterly report");
const int box = editor.InsertTextBox(SlideBox{914400, 914400, 4 * 914400, 914400}, "Revenue grew");
editor.Select({box});
editor.MoveSelection(914400 / 2, 0);       // half an inch to the right
editor.Undo();
```

Every change is one undo step. A change to the slide being edited keeps that
slide before and after (its shapes, not the pictures' bytes, which stay in
the deck), with the selection; the slide list's changes - insert, duplicate,
move, delete - keep the slide they move. A run of arrow-key nudges is one
step. `ChangeSlide(description, change, selectAfter)` makes any other change
a step: `change` edits a copy of the slide.

| Group | Calls |
|---|---|
| Slides | `NewSlide(layout)`, `InsertSlide`, `DuplicateSlide`, `DeleteSlide`, `MoveSlide`, `SetSlideHidden`, `SetSlideNotes` (plain text, or paragraphs), `SetCurrentSlide`; several at once: `MoveSlides`, `DuplicateSlides`, `DeleteSlides`, `SetSlidesHidden` |
| Selection | `Select`, `SelectAll`, `ClearSelection`, `ToggleSelected`, `SelectionBox` - a group whole |
| Groups | `GroupSelection`, `UngroupSelection`, `CanGroup`, `CanUngroup`, `EnterGroup`, `ExitGroup`, `GetEnteredGroup`, `UnitOf`, `SelectionUnits`, `SelectionIsOneGroup`, `RotateSelection` |
| Aligning | `AlignSelection(SlideAlignment)` (left, centre, right, top, middle, bottom), `DistributeSelection(horizontally)` |
| Shapes | `MoveSelection`, `NudgeSelection`, `SetShapeBox`, `SetSelectionBox`, `SetShapeRotation`, `DeleteSelection`, `DuplicateSelection`, `BringToFront`, `SendToBack`, `BringForward`, `SendBackward`, `SetShapeText` |
| Pictures | `SetPictureCropBox`, `SetPictureCrop`, `ResetPictureCrop`, `PictureFullBox` |
| Charts | `SetShapeChart`, `ChartDataRows`, `ChartWithDataRows` |
| Inserting | `InsertTextBox`, `InsertGeometry` (a preset outline), `InsertPicture` (bytes, kept in `deck.pictures`), `InsertShape`, `InsertTable` |
| Text | `SetShapeText` (plain lines), `FormatSelectedText` (every run and paragraph of the selected shapes, table cells too), `AllSelectedText` (a toggle's state) |
| Tables | `TableCellBox`, `TableCellAt`, `InsertTableRow`, `DeleteTableRow`, `InsertTableColumn`, `DeleteTableColumn` |
| Clipboard | `CopySelection`, `CutSelection`, `Paste` (in place, or offset on the slide the shapes came from) |
| Finding | `ShapeAt` (a turned shape in its own frame), `ShapesInside`, `HandleAt`, `Snap` |
| History | `Undo`, `Redo`, `UndoDescription`, `IsModified`, `onChange`, `onSelectionChange` |

`Snap(box, tolerance, ignore, moving, handle)` lines a moved box's edges and
centre (or a resized box's dragged edges) up with the slide's edges and
centre and with the other shapes' edges and centres, and says which guide
lines it used.

`PptxSlideFromLayout(deck, layout)` makes a new slide from one of the deck's
layouts: its placeholders, emptied, keeping their look; Title and Content
when the deck has no layouts.

## The element

```cpp
#include "UltraCanvasSlideEdit.h"

auto edit = CreateSlideEdit("deck", 0, 0, 1200, 700);
edit->SetDeck(deck);
edit->onEditText = [edit](int shape) {
    edit->Editor().SetShapeText(shape, "New text");   // a host opens its own editor here
};
edit->onChange = []() { /* mark the document modified */ };
```

| Where | What |
|---|---|
| The strip | Click a slide to show it; drag it to move it; Delete deletes it, Ctrl+D duplicates it; hidden slides are dimmed |
| The slide | Click selects a shape - a group whole (Shift or Ctrl adds or takes away), a band selects what it covers; dragging moves the selection, snapping (Alt: not; Shift: across or down only); the handles resize (Shift keeps the proportions, Alt from the centre) and the one above turns (Shift: by 15°) - a group's turn it as one; a double-click selects inside a group, or asks for a chart's data (`onEditChart`) |
| Crop mode | `SetCropMode(true)` with a picture selected: the handles crop it, the rest of it shown faintly; Enter, Escape or a click elsewhere ends it |
| The sorter | `SetView(SlideEditView::Sorter)`: every slide in a grid; click, Ctrl+click, Shift+click choose slides, a drag moves them all (one step), a double-click or Enter edits one, Delete, Ctrl+D, Ctrl+A, the arrows; a right-click calls `onContextMenu` |
| Keys | Arrows nudge (Ctrl: by a point), Delete, Ctrl+A, Escape (out of a group, then none), Ctrl+D, Ctrl+C / X / V, Ctrl+Z / Y, Ctrl+G group, Ctrl+Shift+G ungroup, Tab the next shape or group, Enter or F2 its text, Page Up / Down, Ctrl+M a new slide, Ctrl+wheel zoom |

A drag draws a preview copy of the slide and becomes one command when the
button is let go. Empty placeholders show their prompt ("Click to add title")
and a dashed outline. `onContextMenu` reports a right-click in the strip or on
the slide, for a host's menu. `SlideRect()` and `EmuPerPixel()` say where the
slide is drawn, for a host that lays its own editor over a shape - UltraOffice
edits a shape's text in a rich text editor placed there (below).

The show is `UltraCanvasSlideShowPlayer`
([UltraCanvasSlideShowPlayer.md](UltraCanvasSlideShowPlayer.md)): the slides'
transitions, auto-advance, the presenter view on a second monitor
(`StartSlideShow(deck, fromSlide)`).

## Groups

A group (PowerPoint's `p:grpSp`, OpenDocument's `draw:g`) is kept in the
model as each shape's `groupPath` - the groups it is in, outermost first -
and the slide's `groupNames`; its shapes stand together in the z-order and
each keeps its place on the slide, so everything that draws, prints or
exports a slide needs nothing more. The editor chooses groups whole: a
click, Shift or Ctrl+click, a band and Tab take a group with all its
shapes, and moving, resizing (its shapes scale with it), turning (each
shape goes round the group's centre and turns as much), deleting, copying
and the z-order commands act on it as on one shape; a copy of a group is a
group of its own. A double-click enters a group (`EnterGroup`): one of its
shapes - or a group inside it - is chosen alone, until Escape or a click
outside it (`ExitGroup`). Grouping inside a group entered makes a group in
it.

```cpp
const int a = edit->Editor().InsertGeometry(SlideBox{914400, 914400, 914400, 914400}, "rect");
const int b = edit->Editor().InsertGeometry(SlideBox{3 * 914400, 914400, 914400, 914400}, "ellipse");
edit->Editor().Select({a, b});
edit->Editor().GroupSelection();             // Ctrl+G: one step
edit->Editor().RotateSelection(45);          // the group turned as one
edit->Editor().AlignSelection(SlideAlignment::Centre);   // one group: centred on the slide
edit->Editor().UngroupSelection();           // Ctrl+Shift+G
```

`AlignSelection` lines the selected groups and shapes up with each other
(their left edges, centres, right edges, tops, middles or bottoms), or with
the slide when one is selected; `DistributeSelection` gives each two the
same gap, the outermost two staying, or spreads fewer than three over the
slide. Each is one step.

## Pictures and charts

`SetCropMode(true)` crops the selected picture with its handles: what a
handle takes away is a share of the picture (`cropLeft` ... `cropBottom`,
`a:srcRect` in .pptx, `fo:clip` in .odp), the picture's scale unchanged.
`SetPictureCropBox(shape, box)` does it from code, `SetPictureCrop` by the
shares, `ResetPictureCrop` shows the whole picture again, and
`PictureFullBox` says where the whole picture would stand.

A chart's data - from the file, or a chart pasted from a spreadsheet - is
edited as cells: `ChartDataRows(chart)` gives the series' names along the
top and a row per category (a scatter chart's x values first),
`ChartWithDataRows(chart, rows)` takes them back keeping the chart's kind,
title, legend and colours, and `SetShapeChart` puts it into the shape as one
step. UltraOffice shows the cells in a small `UltraCanvasSpreadsheet` in a
dialog when a chart is double-clicked (`onEditChart`).

```cpp
const PptxShape& chart = edit->Editor().CurrentSlide()->shapes[2];
auto rows = UCSlideDeckEditor::ChartDataRows(chart.chart);
rows[1][1] = "42";                             // the first series' first value
edit->Editor().SetShapeChart(2, UCSlideDeckEditor::ChartWithDataRows(chart.chart, rows));
```

## The slide sorter and the notes

`SetView(SlideEditView::Sorter)` shows every slide as a thumbnail in a
grid, drawn by the slide painter as the strip is (`UltraCanvasAlbum` was
looked at first: it shows files from disk and moves one tile at a time;
`UltraCanvasListView` is rows). `GetSorterSelection` says which slides are
chosen; a host's context menu acts on them with `DuplicateSlides`,
`SetSlidesHidden` and `DeleteSlides`, each one step.

The speaker notes are `PptxSlide::notes`, paragraphs like a shape's text: a
host edits them in an `UltraCanvasRichTextEdit` through
`PptxParagraphsToRichDocument` / `PptxParagraphsFromRichDocument` and puts
them back with `SetSlideNotes(index, paragraphs)`, one step that leaves the
slide shown as it is. UltraOffice keeps them in a pane under the slide (a
splitter between, shown or hidden from the View tab) and commits them on
leaving the pane or on a change of slide.

## Copy and paste

`Copy()` / `Cut()` (Ctrl+C / X) keep the selected shapes for a paste here
and put them on the system clipboard for other programs and the other office
elements: their text (a table's rows a line each, a tab between cells) and
HTML (a table as a table, the rest as paragraphs with their look).
`Paste()` (Ctrl+V) takes those shapes back while the clipboard still holds
what their copy put there - a little down and to the right on the slide they
came from. Anything copied since pastes through
`UltraCanvasOfficeClipboard.h`:

| On the clipboard | Pasted as |
|---|---|
| a chart copied from a spreadsheet (the office clipboard) | a chart shape holding its data, as large as it was (`InsertChart`) |
| cells: an HTML table that is the whole fragment, or tab-separated text | a table in the banded look holding the cells' text, a bold, italic or right-aligned cell kept (`InsertTable(box, cells)`) |
| a document's selection (HTML) | a text box keeping bold, italic, underline, colour, size, alignment and list items (`InsertFormattedTextBox`) |
| a picture (an image flavour and no text) | a picture, its proportions kept (`InsertPicture`) |
| plain text | a text box, a paragraph a line, in the deck's body font |

Each is one undo step and lands in the middle of the slide.

```cpp
#include "UltraCanvasOfficeClipboard.h"
// Cells a test (or a host) hands the slide directly:
std::vector<std::vector<std::string>> rows;
if (ClipboardTableFromTabbedText("Item\tPrice\nTea\t2.50", rows))
    edit->Editor().InsertTable(SlideBox{914400, 914400, 4 * 914400, 2 * 370840}, PptxTableCellsFromRows(rows));
```

## Accessibility

A canvas (`AccessibleRole::Canvas`) named by the slide shown ("Slide 2 of 5:
Results"); its parts are the strip - a list of the slides - and the slide's
shapes, named by their text's first words or else their kind, the selected
shape focused, and a change of slide or selection announced. See
[UltraCanvasAccessibility](UltraCanvasAccessibility.md#parts-an-element-draws-itself).

## Drawing and print

```cpp
#include "UltraCanvasPresentationPrint.h"

PresentationPrintPages pages(*deck);          // hidden slides left out
std::string error;
pages.ExportPdf("Talk.pdf", error);
```

`DrawPptxSlide(ctx, deck, slide, rect, options)` draws a slide into any
rectangle of any render context, filling only that rectangle with the slide's
background; `PptxSlideDrawOptions` adds prompt text for empty placeholders
and leaves one shape's text out while a host edits it. A picture is decoded
once and drawn from then on.

## Text and tables in place

A shape's text - or a table cell's - is edited in an `UltraCanvasRichTextEdit`
the host lays over it, at the slide's zoom (`SlideRect()`, `EmuPerPixel()`),
transparent, with the shape's text left out meanwhile (`SetEditingShape`,
`SetEditingCell`). A double-click asks for it: `onEditText(shape)`, or
`onEditTableCell(shape, row, col)` on a table. The text goes there and back
through

```cpp
UCRichDocument rich = PptxParagraphsToRichDocument(shape.paragraphs);
// ... edited ...
shape.paragraphs = PptxParagraphsFromRichDocument(rich, shape.paragraphs);
```

The document keeps each run's look (bold, italic, underline, strike, super-
and subscript, typeface, size, colour, highlight, link), line breaks,
alignment, bullets at their levels (a Wingdings square as the square it is:
`PptxBulletText`) and numbering (PowerPoint's `alphaLcParenR` as lower
letters with a parenthesis, the start number on a list's first item). What it
does not hold - the bullet's typeface, indents, spacing, capitals - comes back
from the paragraphs before the edit. UltraOffice commits the edit as one step
on a click elsewhere or Ctrl+Enter, and drops it on Escape.

`InsertTable(box, rows, columns)` makes a table in PowerPoint's banded look; a
row inserted under the header takes the body rows' look.

## Not yet

- Guides of one's own; a group's frame turned with it (a turned group is
  framed by the box round its shapes' unturned boxes) and a group's
  mirroring.
- Moving a picture inside its crop (the crop handles move the edges only);
  cropping a turned picture keeps its centre where the crop puts it.
- Merging and splitting table cells, resizing a table's columns by dragging.
- Animations of single shapes in the show (the slides' transitions play).

## Tests

- `Tests/SlideDeckEditorTest.cpp` - every command with undo and redo, the
  selection put back, nudges joined, hit testing of turned shapes, handles,
  snapping; the text's round trip through the rich document (looks, line
  breaks, bullets, numbering, an empty paragraph's look); formatting the
  selected shapes; tables (cells, rows, columns, undo); groups (grouped,
  selected whole, moved, resized and turned with their shapes, entered, kept
  together by the z-order commands, copied as new groups, ungrouped, nested,
  each undone); align and distribute; picture crops and their reset; notes as
  paragraphs (one step, kept in .pptx and .odp); a chart's data as cells and
  back; several slides moved, duplicated, hidden and deleted as one step;
  groups and crops through .pptx, .odp and .fodp - a turned group's shapes
  too - and through LibreOffice both ways when soffice is installed.
- `Tests/SlideEditElementTest.cpp` - the element driven by pointer and key
  events: a click selects, a drag moves (one step, snapping to the slide's
  centre), a corner with Shift keeps the proportions, the turning handle
  turns by 15° with Shift, a band selects, the strip chooses and moves
  slides, the keys nudge, delete, undo and add a slide, a double-click asks
  for the text; a group made with Ctrl+G, clicked, dragged, resized and
  turned as one, entered with a double-click, left with Escape, taken apart
  with Ctrl+Shift+G; crop mode's left handle cropping half a picture (one
  step, drawn so); the sorter's click, Ctrl+click, Shift+click, a drag of two
  slides, Ctrl+D, Delete, a right-click and a double-click; a double-click on
  a chart asking for its data.
- `Tests/PresentationPrintTest.cpp` - a slide drawn into a rectangle and
  nothing around it, prompt text, pages and the PDF.
