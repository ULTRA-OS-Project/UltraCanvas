// include/UltraCanvasSlideDeckEditor.h
// The editing core of a presentation, without a user interface: the deck
// (UltraCanvasPresentationPptx.h's model), the slide being edited, the shapes
// selected on it, and every change as a command that undo takes back and redo
// does again. UltraCanvasSlideEdit is its element - it draws and turns the
// pointer and the keys into these calls - and a test drives it directly, as
// UCRichDocumentEditor sits under UltraCanvasRichTextEdit.
//
// Units are the deck's: EMU (914400 to the inch), on the slide.
//
// Undo never copies the deck, which holds the pictures' bytes: a change to a
// slide keeps that slide before and after (its shapes and text, not the
// pictures), and the slide list's changes - insert, delete, move - keep the
// slide they move.
//
// doc-check: example
//   auto deck = std::make_shared<PptxPresentation>();
//   UCSlideDeckEditor editor(deck);
//   editor.InsertSlide(0, PptxSlideFromLayout(*deck, -1));   // Title and Content
//   editor.SetShapeText(0, "Quarterly report");                  // the title
//   editor.Select({1});
//   editor.MoveSelection(914400, 0);                             // an inch to the right
//   editor.Undo();
//
// Version: 1.3.0 - pasting from other kinds: InsertTable with the cells' text, InsertFormattedTextBox,
//                  InsertChart; CopiedShapes
// Version: 1.2.0 - tables: InsertTable, TableCellBox, TableCellAt, rows and columns inserted and deleted
// Version: 1.1.0 - FormatSelectedText, AllSelectedText
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasPresentationPptx.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// A box on the slide, EMU.
struct SlideBox {
    int64_t x = 0, y = 0, width = 0, height = 0;
    int64_t Right() const { return x + width; }
    int64_t Bottom() const { return y + height; }
    int64_t CentreX() const { return x + width / 2; }
    int64_t CentreY() const { return y + height / 2; }
};

// What snapping a moved or resized box did: the box, and the guide lines it
// lined up with (to draw while dragging).
struct SlideSnap {
    SlideBox box;
    std::vector<int64_t> verticalGuides;     // x positions
    std::vector<int64_t> horizontalGuides;   // y positions
};

// The eight handles round a box, and the turning handle above it.
enum class SlideHandle { NoHandle, TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left, Rotate };

class UCSlideDeckEditor {
public:
    explicit UCSlideDeckEditor(std::shared_ptr<PptxPresentation> deck = nullptr);
    ~UCSlideDeckEditor();
    UCSlideDeckEditor(const UCSlideDeckEditor&) = delete;
    UCSlideDeckEditor& operator=(const UCSlideDeckEditor&) = delete;

    // ===== THE DECK =====
    // A deck to edit; the history starts again.
    void SetDeck(std::shared_ptr<PptxPresentation> deck);
    const std::shared_ptr<PptxPresentation>& GetDeck() const { return deck_; }
    int64_t SlideWidth() const;
    int64_t SlideHeight() const;
    int GetSlideCount() const;

    // The slide being edited; -1 when the deck has none.
    int GetCurrentSlide() const { return current_; }
    void SetCurrentSlide(int index);
    const PptxSlide* CurrentSlide() const;

    // ===== SELECTION =====
    const std::vector<int>& GetSelection() const { return selection_; }
    void Select(std::vector<int> shapes);   // indices on the current slide
    void SelectAll();
    void ClearSelection();
    void ToggleSelected(int shape);
    bool IsSelected(int shape) const;
    // The shapes' boxes together; width 0 when nothing is selected.
    SlideBox SelectionBox() const;

    // ===== FINDING =====
    // The topmost shape at `x`, `y` on the current slide, turned shapes
    // tested in their own frame; -1: none.
    int ShapeAt(int64_t x, int64_t y) const;
    // The shapes whose boxes lie inside `area` (a rubber band).
    std::vector<int> ShapesInside(const SlideBox& area) const;
    // The handle of the selection under `x`, `y`, the slide shown at
    // `emuPerPixel`: with one shape selected its own turned frame's; else the
    // selection box's. A handle is found within kHandleReach pixels.
    SlideHandle HandleAt(int64_t x, int64_t y, double emuPerPixel) const;
    static constexpr double kHandleReach = 6.0;
    // How far above the frame the turning handle sits, in pixels.
    static constexpr double kRotateHandleOffset = 24.0;

    // `box` moved to line up its edges and centre with the slide's edges and
    // centre and with the other shapes' (those not in `ignore`), when they
    // are within `tolerance` EMU. `moving`: the whole box moves; else only
    // the edges `handle` drags.
    SlideSnap Snap(const SlideBox& box, int64_t tolerance, const std::vector<int>& ignore, bool moving,
                   SlideHandle handle = SlideHandle::NoHandle) const;

    // ===== CHANGES TO SHAPES (each one undo step) =====
    void MoveSelection(int64_t dx, int64_t dy);
    // A nudge (the arrow keys): one step that the next nudge joins.
    void NudgeSelection(int64_t dx, int64_t dy);
    void SetShapeBox(int shape, const SlideBox& box);
    // `box` for the whole selection: each shape scaled within it.
    void SetSelectionBox(const SlideBox& from, const SlideBox& to);
    void SetShapeRotation(int shape, double degrees);
    void DeleteSelection();
    void DuplicateSelection();             // copies offset a little, then selected
    void BringToFront();
    void SendToBack();
    void BringForward();
    void SendBackward();
    // A new shape on the current slide, selected; its index.
    int InsertShape(const PptxShape& shape);
    // A text box at `box` with `text` in the deck's body font.
    int InsertTextBox(const SlideBox& box, const std::string& text = std::string());
    // A text box at `box` holding `paragraphs` as they are (text pasted
    // with its look); a run without a size or typeface takes the deck's
    // body font at 18 points.
    int InsertFormattedTextBox(const SlideBox& box, const std::vector<PptxParagraph>& paragraphs);
    // A drawn shape (a preset outline: "rect", "ellipse", "roundRect", "triangle", "rightArrow" ...).
    int InsertGeometry(const SlideBox& box, const std::string& geometry, const std::string& fill = "4472C4");
    // A chart at `box` showing `chart` (a static copy of its data: a chart
    // copied from a spreadsheet), selected; -1 when it has nothing to show.
    int InsertChart(const SlideBox& box, const RichChart& chart);
    // A picture from its bytes, `mimeType` "image/png" ...; its box keeps
    // `widthPx` x `heightPx`'s proportions, centred, at most half the slide.
    int InsertPicture(const std::vector<uint8_t>& bytes, const std::string& extension, int widthPx, int heightPx,
                      const std::string& altText = std::string());
    // The shape's text as plain lines (a paragraph per line), the first run's
    // look kept for all of it.
    void SetShapeText(int shape, const std::string& text);
    static std::string ShapeText(const PptxShape& shape);
    // Any change to the current slide's shapes, as one step: `change` edits
    // a copy of the slide, which then replaces it; `selectAfter`, when given,
    // is the selection after it (undo brings back the one before).
    void ChangeSlide(const std::string& description, const std::function<void(PptxSlide&)>& change,
                     const std::vector<int>* selectAfter = nullptr);

    // The text of every selected shape - its paragraphs, and a table's cells
    // - changed run by run and paragraph by paragraph, as one step (Bold on
    // a shape with no text editor open).
    void FormatSelectedText(const std::string& description, const std::function<void(PptxRun&)>& run,
                            const std::function<void(PptxParagraph&)>& paragraph = nullptr);
    // Whether every run of the selected shapes' text passes `test` (a toggle's state).
    bool AllSelectedText(const std::function<bool(const PptxRun&)>& test) const;

    // ===== TABLES =====
    // A table of `rows` x `columns` at `box`, in PowerPoint's banded look (a
    // coloured header row, alternating rows), selected; its index.
    int InsertTable(const SlideBox& box, int rows, int columns);
    // The same table holding `cells` (rows of cells, each cell's paragraphs,
    // as PptxShape::rows): as many rows and columns as the longest, a run
    // without a size, colour or weight taking the banded look's (a pasted
    // block of cells). -1 when there are none.
    int InsertTable(const SlideBox& box, const std::vector<std::vector<std::vector<PptxParagraph>>>& cells);
    // Cell (`row`, `col`)'s box on the slide: the table's grid scaled to its
    // box, a merged cell spanning its columns and rows.
    static SlideBox TableCellBox(const PptxShape& table, int row, int col);
    // The cell of table `shape` at `x`, `y` (a merged-away cell gives the one
    // covering it); false when the point is outside the table.
    bool TableCellAt(int shape, int64_t x, int64_t y, int& row, int& col) const;
    // A row before (`below` false) or after row `row`, copying its look; a
    // column likewise; each one step. A table keeps at least one of each.
    void InsertTableRow(int shape, int row, bool below);
    void DeleteTableRow(int shape, int row);
    void InsertTableColumn(int shape, int col, bool right);
    void DeleteTableColumn(int shape, int col);

    // The shapes copied and pasted, inside the program.
    void CopySelection();
    void CutSelection();
    bool CanPaste() const { return !clipboard_.empty(); }
    const std::vector<PptxShape>& CopiedShapes() const { return clipboard_; }
    void Paste();

    // ===== CHANGES TO THE SLIDE LIST =====
    // `slide` before `index` (at the end for -1), made current.
    void InsertSlide(int index, PptxSlide slide);
    void NewSlide(int layoutIndex);        // after the current one, from the layout
    void DuplicateSlide(int index);
    void DeleteSlide(int index);
    void MoveSlide(int from, int to);
    void SetSlideHidden(int index, bool hidden);
    void SetSlideNotes(int index, const std::string& text);
    static std::string SlideNotesText(const PptxSlide& slide);

    // ===== UNDO =====
    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }
    std::string UndoDescription() const;
    std::string RedoDescription() const;
    void Undo();
    void Redo();
    bool IsModified() const { return modified_; }
    void SetModified(bool modified) { modified_ = modified; }

    // Every change, undo and redo; and a change of slide or selection.
    std::function<void()> onChange;
    std::function<void()> onSelectionChange;

    struct Command;

private:
    std::shared_ptr<PptxPresentation> deck_;
    int current_ = -1;
    std::vector<int> selection_;
    std::vector<std::unique_ptr<Command>> undo_, redo_;
    std::vector<PptxShape> clipboard_;
    bool modified_ = false;
    bool nudging_ = false;   // the last step is a nudge the next one joins

    void Run(std::unique_ptr<Command> command);
    void Changed();
    void SelectionChanged();
    void Reorder(int how);
    void RenumberSlides();
};

} // namespace UltraCanvas
