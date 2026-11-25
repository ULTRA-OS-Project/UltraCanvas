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
| Slides | `NewSlide(layout)`, `InsertSlide`, `DuplicateSlide`, `DeleteSlide`, `MoveSlide`, `SetSlideHidden`, `SetSlideNotes`, `SetCurrentSlide` |
| Selection | `Select`, `SelectAll`, `ClearSelection`, `ToggleSelected`, `SelectionBox` |
| Shapes | `MoveSelection`, `NudgeSelection`, `SetShapeBox`, `SetSelectionBox`, `SetShapeRotation`, `DeleteSelection`, `DuplicateSelection`, `BringToFront`, `SendToBack`, `BringForward`, `SendBackward`, `SetShapeText` |
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
| The slide | Click selects a shape (Shift or Ctrl adds or takes away), a band selects what it covers; dragging moves the selection, snapping (Alt: not; Shift: across or down only); the handles resize (Shift keeps the proportions, Alt from the centre) and the one above turns (Shift: by 15°) |
| Keys | Arrows nudge (Ctrl: by a point), Delete, Ctrl+A, Escape, Ctrl+D, Ctrl+C / X / V, Ctrl+Z / Y, Tab the next shape, Enter or F2 its text, Page Up / Down, Ctrl+M a new slide, Ctrl+wheel zoom |

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

- Grouping, alignment and distribution commands, guides of one's own.
- Merging and splitting table cells, resizing a table's columns by dragging.
- Animations of single shapes in the show (the slides' transitions play).

## Tests

- `Tests/SlideDeckEditorTest.cpp` - every command with undo and redo, the
  selection put back, nudges joined, hit testing of turned shapes, handles,
  snapping; the text's round trip through the rich document (looks, line
  breaks, bullets, numbering, an empty paragraph's look); formatting the
  selected shapes; tables (cells, rows, columns, undo).
- `Tests/SlideEditElementTest.cpp` - the element driven by pointer and key
  events: a click selects, a drag moves (one step, snapping to the slide's
  centre), a corner with Shift keeps the proportions, the turning handle
  turns by 15° with Shift, a band selects, the strip chooses and moves
  slides, the keys nudge, delete, undo and add a slide, a double-click asks
  for the text.
- `Tests/PresentationPrintTest.cpp` - a slide drawn into a rectangle and
  nothing around it, prompt text, pages and the PDF.
