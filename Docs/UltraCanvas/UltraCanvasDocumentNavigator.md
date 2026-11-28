# UltraCanvasDocumentNavigator

<!-- doc-check: std::shared_ptr<UltraCanvasRichTextEdit> edit; std::shared_ptr<UltraCanvasSplitPane> split; void OpenMyCommentEditor(int index); -->

A word processor's navigator for any application that edits with
[`UltraCanvasRichTextEdit`](UltraCanvasRichTextEdit.md): a side panel that
lists what the document holds, read from its model, and goes there on a
click. UltraOffice shows it at the left of a document tab, UltraTexter beside
a word-processing tab.

- Header: `UltraCanvas/include/UltraCanvasDocumentNavigator.h`
- The outline it shows (UI-free): `UltraCanvas/include/UltraCanvasRichDocumentOutline.h`
- The section moves (UI-free, with undo): `UCRichDocumentEditor::MoveHeadingSection`,
  `ChangeHeadingSectionLevel` in `UltraCanvasRichDocumentEditor.h`
- Tests: `Tests/RichDocumentOutlineTest.cpp` (no display),
  `Tests/DocumentNavigatorElementTest.cpp` (Xvfb)

## What it lists

From top to bottom: a search box, four buttons, and one tree
(`UltraCanvasTreeView`) with a row per list that has entries.

| List | Rows | A click | A double-click |
|---|---|---|---|
| Headings | the headings, as a tree by level; the one the caret is under is the selected row and follows the caret | the caret to the heading | ...and the keyboard to the document |
| Tables | a table's caption, else "Table *n*" and its first cell's words; the tooltip gives its rows and columns | the caret into its first cell | |
| Pictures | a picture's caption, else its alt text, else "Picture *n*" (paragraph pictures, pictures in the line and in table cells) | selects the picture | |
| Charts | a chart's caption, else its title | selects the chart | |
| Comments | "author: text"; a resolved one dimmed; the tooltip gives the words it is on | selects the commented words | opens the list of comments (`ShowCommentsDialog`), or calls `onCommentActivated` |
| Bookmarks | the document's bookmarks (not the "_Toc" marks a table of contents gives headings) | the caret to it | |
| Footnotes and endnotes | the mark and the note's words | the caret to the reference | opens the note for editing |
| Tracked changes | "Inserted: words" / "Deleted: words"; the tooltip gives who and when | selects the change | |
| Found in the text | while the search box holds something: the places the document says it, with the words round them | selects the find | |

The search box filters every list by its words in any case and any script
(`utf8_find`); a heading stays when one under it matches, so the tree keeps
its shape.

## Moving a chapter

A heading's **section** is the heading, its text and its subheadings: the
blocks up to the next heading of its level or a higher one. The buttons above
the tree act on the selected heading (or the caret's):

| Button | Icon | What it does |
|---|---|---|
| Promote | `office/outdent.svg` | the heading and every heading in its section one level up (Heading 2 becomes 1, its 3s become 2s) |
| Demote | `office/indent.svg` | one level down |
| Move up | `menu/chevron-up.svg` | the section before the sibling heading of its level before it |
| Move down | `menu/chevron-down.svg` | after the sibling's section after it |

A heading row is also dragged onto another heading row (its section goes
before that heading, keeping its level) or onto the Headings row (to the end
of the document); a drop into its own section is refused. Each is one undo
step of the document. Promote is refused at level 1, demote at level 6, and
the moves when there is no sibling under the same parent.

## Following the document

```cpp
#include "UltraCanvasDocumentNavigator.h"

auto navigator = std::make_shared<UltraCanvasDocumentNavigator>("navigator", 0, 0, 260, 600);
navigator->SetEdit(edit);                 // held weakly; listens through AddDocumentObserver
navigator->SetRefreshDelay(400);          // read again 400 ms after the typing pauses (0: after every edit)
navigator->onCommentActivated = [](int index) { OpenMyCommentEditor(index); };
split->GetPane(0)->AddChild(navigator);   // a resizable side panel
```

The navigator listens to the edit with `AddDocumentObserver`, so the host's
own `onDocumentChanged` and `onSelectionChanged` stay the host's. An edit is
read again once the typing pauses (`SetRefreshDelay`, 400 ms by default;
every new edit puts it off again), and the tree is rebuilt only when the
outline changed - typing in body text leaves the rows as they are, rows the
user closed stay closed. A caret move selects the current heading at once.
`Refresh()` reads the document now.

What a host or a test drives without the mouse:

| Call | |
|---|---|
| `SetFilterText(text)` / `GetFilterText()` | what the search box does; `GetFoundRanges()` the finds |
| `GoToEntry(kind, index)` / `ActivateEntry(kind, index)` | a click / a double-click on a row (`EntryKind`, index into the outline's list) |
| `GetCurrentHeading()` | the heading the caret is under (index into `GetOutline().headings`) |
| `PromoteHeading()`, `DemoteHeading()`, `MoveHeadingUp()`, `MoveHeadingDown()` | the four buttons |
| `MoveHeadingBefore(dragged, target)` | a drop (target -1: the Headings row) |
| `EntryId(kind, index)`, `ParseEntryId(id, kind, index)`, `IsEntryShown(kind, index)` | a row's id in `GetTree()`, and whether the search shows it |

## The outline without a view

`BuildRichDocumentOutline(document)` is what the navigator reads, and needs
no display: `RichDocumentOutline` holds `headings` (level, words, parent,
section end), `tables` (rows, columns, caption, first cell), `pictures` and
`charts` (position, caption, alt text or title), `comments` (author, text,
the range and words it is on; one no text refers to is left out),
`bookmarks`, `notes` (mark, words, the reference's position) and `changes`
(inserted or deleted, author, date, range). `HeadingAt(block)` is the heading
a block is under; two outlines compare with `==`.

```cpp
#include "UltraCanvasRichDocumentOutline.h"

UCRichDocument document = UCRichDocument::FromMarkdown("# Plan\n\n## Costs\n\n## Dates\n");
RichDocumentOutline outline = BuildRichDocumentOutline(document);
int parent = outline.headings[1].parent;                      // 0: "Costs" is under "Plan"
UCRichDocumentEditor editor(std::make_shared<UCRichDocument>(document));
editor.MoveHeadingSectionUp(outline.headings[2].blockIndex);  // "Dates" before "Costs", one undo step
(void)parent;
```

## Not done yet

- Headings, footnotes and changes in headers, footers and text frames are
  not listed: the outline walks the body.
- A heading in a style of its own (not *Heading n*) changes level but keeps
  its style when promoted or demoted.
- The finds are the editing core's (`FindAll`), which does not yet look into
  table cells.
