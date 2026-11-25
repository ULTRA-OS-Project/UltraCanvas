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
// Version: 1.4.0 - groups (GroupSelection, UngroupSelection, EnterGroup, whole groups selected, moved,
//                  resized and turned as one: RotateSelection); AlignSelection, DistributeSelection;
//                  picture crops (SetPictureCrop, ResetPictureCrop, PictureFullBox); a chart's data
//                  (SetShapeChart, ChartDataRows, ChartWithDataRows); SetSlideNotes with paragraphs
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
#include <map>
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

// Where AlignSelection lines the shapes up: their left edges, centres or
// right edges; their tops, middles or bottoms.
enum class SlideAlignment { Left, Centre, Right, Top, Middle, Bottom };

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
    // A shape in a group is selected with the rest of it: Select and
    // ToggleSelected take whole groups - the outermost below the group
    // entered (EnterGroup), if any - and the selection holds every shape of
    // them.
    const std::vector<int>& GetSelection() const { return selection_; }
    void Select(std::vector<int> shapes);   // indices on the current slide
    void SelectAll();
    void ClearSelection();
    void ToggleSelected(int shape);
    bool IsSelected(int shape) const;
    // The shapes' boxes together; width 0 when nothing is selected.
    SlideBox SelectionBox() const;

    // ===== GROUPS =====
    // The shapes selected with `shape`: its group's below the group entered,
    // else itself; empty for no shape.
    std::vector<int> UnitOf(int shape) const;
    // The selection as its groups and single shapes, back to front.
    std::vector<std::vector<int>> SelectionUnits() const;
    // One group is selected (it is framed, resized and turned as one shape).
    bool SelectionIsOneGroup() const;
    // Two groups or shapes or more, none of them a placeholder (a title or a
    // content box stays on its own, as PowerPoint keeps it).
    bool CanGroup() const;
    bool CanUngroup() const;
    // The selected shapes and groups as one group (Ctrl+G), one step: they
    // come together in the z-order where the frontmost of them was, and stay
    // selected. Inside a group entered, the new one is a group in it.
    void GroupSelection();
    // Each selected group taken apart (Ctrl+Shift+G), one step; what was in
    // it stays selected.
    void UngroupSelection();
    // Selecting inside `shape`'s group (a double-click on a group): the shape
    // - or the group inside it that holds it - alone selected. False when the
    // shape is in no group below the one entered.
    bool EnterGroup(int shape);
    // Back to selecting whole groups (a click outside, Escape).
    void ExitGroup();
    // The group selected inside (its path, outermost first); empty: none.
    const std::vector<int>& GetEnteredGroup() const { return entered_; }
    // The selection turned by `degrees` more about its box's centre, as one:
    // each shape's centre goes round it and each shape turns as much (a
    // group turned). One step.
    void RotateSelection(double degrees);

    // ===== ALIGNING =====
    // The selected groups and shapes lined up with each other, or with the
    // slide when one is selected; one step.
    void AlignSelection(SlideAlignment how);
    // The same gap between each two (`horizontally`: across, else down), the
    // outermost two staying where they are; with fewer than three, spread
    // over the slide. One step.
    void DistributeSelection(bool horizontally);

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

    // ===== PICTURES =====
    // Where the whole picture would stand, uncropped, at the size it is
    // shown: the box grown by what the crop takes away from each side.
    static SlideBox PictureFullBox(const PptxShape& picture);
    // Picture `shape` cropped to `box` (on the slide): what it keeps of the
    // picture follows from where the box lies in PictureFullBox, the
    // picture's scale unchanged (a crop handle dragged). One step.
    void SetPictureCropBox(int shape, const SlideBox& box);
    // The crop by its shares (0..1 from each side, as a:srcRect), the box
    // following. One step.
    void SetPictureCrop(int shape, double left, double top, double right, double bottom);
    // The whole picture again, its box grown to it. One step.
    void ResetPictureCrop(int shape);

    // ===== CHARTS =====
    // Chart `shape` showing `chart` (its data edited), one step.
    void SetShapeChart(int shape, const RichChart& chart);
    // A chart's data as a block of cells: the first row the series' names
    // under an empty corner, then a row per category - its name, and each
    // series' value. A scatter or bubble chart's first column is the x values.
    static std::vector<std::vector<std::string>> ChartDataRows(const RichChart& chart);
    // `chart` with the data of `rows` (as ChartDataRows gives them): its
    // kind, title, legend and the series' colours kept; an empty row or
    // column at the end left out; a cell that is no number is 0.
    static RichChart ChartWithDataRows(const RichChart& chart, const std::vector<std::vector<std::string>>& rows);

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
    // Several slides at once (the sorter's selection), each one step: moved
    // together before slide `to` of the deck as it is (the end for the
    // count), in their order; deleted; each duplicated after the last of
    // them; hidden or shown.
    void MoveSlides(std::vector<int> slides, int to);
    void DeleteSlides(std::vector<int> slides);
    void DuplicateSlides(std::vector<int> slides);
    void SetSlidesHidden(std::vector<int> slides, bool hidden);
    void SetSlideNotes(int index, const std::string& text);
    // The notes as paragraphs (the notes pane's rich text), one step; the
    // slide shown stays as it is.
    void SetSlideNotes(int index, const std::vector<PptxParagraph>& notes);
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
    std::vector<int> entered_;   // the group selected inside; empty: none
    std::vector<std::unique_ptr<Command>> undo_, redo_;
    std::vector<PptxShape> clipboard_;
    std::vector<int> clipboardStrip_;                 // the group entered when they were copied
    std::map<int, std::string> clipboardGroupNames_;  // their slide's group names
    bool modified_ = false;
    bool nudging_ = false;   // the last step is a nudge the next one joins

    void Run(std::unique_ptr<Command> command);
    void Changed();
    void SelectionChanged();
    void Reorder(int how);
    int UnitKey(const PptxShape& shape, int index) const;
    void CheckEnteredGroup();
    void RenumberSlides();
};

} // namespace UltraCanvas
