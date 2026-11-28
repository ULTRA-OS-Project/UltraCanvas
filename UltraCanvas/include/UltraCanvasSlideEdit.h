// include/UltraCanvasSlideEdit.h
// A presentation editor: the slide strip at the side and the slide being
// edited, drawn by DrawPptxSlide (UltraCanvasPptxSlideRender.h), the same
// drawing as the thumbnails, the show and PDF export. The editing itself is
// UCSlideDeckEditor (UltraCanvasSlideDeckEditor.h): this element draws and
// turns the pointer and the keys into its commands, each one undo step.
//
// On the slide:
//   click             select a shape (Shift or Ctrl: add or take away);
//                     on nothing, drag a band to select what it covers
//   drag a shape      move the selection, snapping to the slide's edges and
//                     centre and to the other shapes (guides show where);
//                     Shift: across or down only; Alt: no snapping
//   drag a handle     resize - Shift keeps the proportions, Alt from the
//                     centre; the handle above turns the shape (Shift: by 15°)
//   double-click      a shape's text: onEditText
//   arrows            nudge (Ctrl: by a point); a run of them is one step
//   Delete            the selection        Ctrl+A select all, Escape none
//   Ctrl+D            duplicate            Ctrl+C / X / V the shapes (Copy, Cut, Paste)
//   Ctrl+Z / Y        undo / redo          Page Up / Down the slide before / after
//   Tab               the next shape       Enter, F2 its text (onEditText)
//   Ctrl+wheel        zoom                 Ctrl+M a new slide
//   Ctrl+G            group                Ctrl+Shift+G ungroup
// A group is selected, moved, resized and turned as one; a double-click on
// it selects inside it (a shape of it alone), Escape or a click outside
// goes back. A double-click on a chart calls onEditChart.
// Crop mode (SetCropMode, a picture selected): the handles crop the picture,
// the rest of it shown faintly; Enter, Escape or a click elsewhere ends it.
// In the strip: click a slide, drag it to move it, Delete deletes it;
// right-click (strip or slide) calls onContextMenu.
// The slide sorter (SetView(SlideEditView::Sorter)): every slide as a
// thumbnail in a grid; click, Ctrl+click and Shift+click choose slides, a
// drag moves them all to where it is let go (one step), a double-click or
// Enter edits the slide, Delete deletes them, Ctrl+D duplicates them, and a
// right-click calls onContextMenu (inStrip true) for a host's menu.
//
// Paste takes the shapes copied here while the system clipboard still holds
// what Copy put there (their text and HTML, for other programs); else what
// the system clipboard holds, through UltraCanvasOfficeClipboard.h: a chart
// copied from a spreadsheet as a chart, cells (an HTML table or tab-separated
// text) as a table, a picture as a picture, a document's selection (HTML)
// or plain text as a text box.
//
// To assistive technology it is a canvas (AccessibleRole::Canvas) named by
// the slide shown, whose parts are the slide strip ({0}: a list, a part per
// slide) and the slide's shapes ({1}: a part per shape, named by its text's
// first words, else by its kind - "Title", "Image" ...); the selected shape
// (or, after a click in the strip, the slide) is the focused part, and a
// change of slide or selection is announced (SelectionChanged).
//
// doc-check: std::shared_ptr<PptxPresentation> deck;
// doc-check: example
//   auto edit = CreateSlideEdit("deck", 0, 0, 1200, 700);
//   edit->SetDeck(deck);
//   edit->onEditText = [edit](int shape) { /* ask for the text */ edit->Editor().SetShapeText(shape, "Hello"); };
//
// Version: 1.5.0 - groups (Ctrl+G, Ctrl+Shift+G, a double-click selects inside), a group framed and
//                  turned as one; crop mode (SetCropMode); the slide sorter (SetView); onEditChart
// Version: 1.4.0 - Copy / Cut / Paste through the system clipboard (cells, text, pictures, charts);
//                  a canvas with the slides and shapes as parts to assistive technology
// Version: 1.3.0 - the show mode is gone: UltraCanvasSlideShowPlayer plays the deck
// Version: 1.1.0 - SetEditingShape: a shape's text left to a host's in-place editor
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasSlideDeckEditor.h"
#include "UltraCanvasUIElement.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// The slide being edited with the strip beside it, or every slide in a grid.
enum class SlideEditView { Normal, Sorter };

class UltraCanvasSlideEdit : public UltraCanvasUIElement {
public:
    UltraCanvasSlideEdit(const std::string& id, int x, int y, int w, int h);
    ~UltraCanvasSlideEdit() override;

    // ===== THE DECK =====
    void SetDeck(std::shared_ptr<PptxPresentation> deck);
    UCSlideDeckEditor& Editor() { return editor_; }
    const UCSlideDeckEditor& Editor() const { return editor_; }

    // ===== VIEW =====
    void SetShowSlideStrip(bool show);
    bool GetShowSlideStrip() const { return showStrip_; }
    void SetStripWidth(int width);
    // 1.0: the slide fits the space; more magnifies it (the wheel scrolls).
    void SetZoom(double zoom);
    double GetZoom() const { return zoom_; }
    void ZoomToFit() { SetZoom(1.0); }
    void SetSnapping(bool snap) { snapping_ = snap; }
    bool GetSnapping() const { return snapping_; }
    // Shows slide `index` and scrolls the strip to it.
    void GoToSlide(int index);

    // The slide sorter, or the slide being edited.
    void SetView(SlideEditView view);
    SlideEditView GetView() const { return view_; }
    // The slides chosen in the sorter, in order (the current one when none
    // was chosen); and choosing them.
    std::vector<int> GetSorterSelection() const;
    void SetSorterSelection(std::vector<int> slides);

    // Crop mode for the selected picture: its handles crop it. Ends by
    // itself when the selection changes. False when no picture is selected.
    bool SetCropMode(bool crop);
    bool IsCropping() const { return cropping_; }

    // ===== CLIPBOARD =====
    // The selected shapes (Ctrl+C / X), kept for a paste here and put on the
    // system clipboard as text and HTML; Paste (Ctrl+V) as described above.
    void Copy();
    void Cut();
    void Paste();

    // ===== CALLBACKS =====
    std::function<void()> onChange;              // the deck changed (and undo / redo)
    std::function<void()> onSelectionChange;     // the slide or the selection changed
    // A shape's text is to be edited (double-click, Enter, F2).
    std::function<void(int shape)> onEditText;
    // A table's cell is to be edited (double-click on a table).
    std::function<void(int shape, int row, int col)> onEditTableCell;
    // A chart's data is to be edited (double-click on a chart).
    std::function<void(int shape)> onEditChart;
    // The view or crop mode changed (SetView, a double-click in the sorter,
    // the end of cropping).
    std::function<void()> onModeChange;
    // Right-click: `slide` the slide under it in the strip (or the slide
    // shown), `shape` the shape under it on the slide (-1: none, or in the
    // strip); `windowX`, `windowY` where, for a menu.
    std::function<void(bool inStrip, int slide, int shape, int windowX, int windowY)> onContextMenu;

    // ===== ELEMENT =====
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;
    bool AcceptsFocus() const override { return true; }

    // ===== ACCESSIBILITY =====
    AccessibleRole GetAccessibleRole() const override { return AccessibleRole::Canvas; }
    std::string GetAccessibleName() const override;
    IAccessibleParts* GetAccessiblePartsInterface() override;

    // The shape whose text a host edits in place over the slide: drawn
    // without its text meanwhile, its frame shown; -1: none.
    void SetEditingShape(int shape);
    int GetEditingShape() const { return editingShape_; }
    // The table cell a host edits in place (its text left out); shape -1: none.
    void SetEditingCell(int shape, int row, int col);

    // Where the slide is drawn, in the element's coordinates, and the
    // slide's EMU per pixel there (for a host placing something over it).
    Rect2Dd SlideRect() const;
    double EmuPerPixel() const;

private:
    enum class Drag { NoDrag, Band, Move, Resize, Rotate, StripSlide, Crop, SorterSlides };

    SlideEditView view_ = SlideEditView::Normal;
    bool cropping_ = false;
    std::vector<int> sorterSelection_;   // the slides chosen in the sorter
    int sorterAnchor_ = -1;               // where a Shift+click's range starts
    double sorterScroll_ = 0;
    int sorterDropIndex_ = -1;
    Rect2Dd SorterThumbRect(int index) const;
    int SorterColumns() const;
    int SorterSlideAt(const Point2Df& p) const;
    int SorterDropAt(const Point2Df& p) const;
    void DrawSorter(IRenderContext* ctx);
    void DrawCrop(IRenderContext* ctx, const PptxSlide& slide);
    bool OnSorterPress(const UCEvent& event);
    bool OnSorterKey(const UCEvent& event);
    void EnsureSorterVisible(int index);
    void ModeChanged();

    int editingShape_ = -1;
    std::string copiedText_;         // what Copy put on the system clipboard as text
    class AccessibleSlides;
    std::shared_ptr<AccessibleSlides> accessibleSlides_;
    int announcedSlide_ = -1;
    void AnnounceAccessibleSelection();
    bool PasteFromSystemClipboard();
    int editingCellShape_ = -1, editingCellRow_ = -1, editingCellCol_ = -1;

    UCSlideDeckEditor editor_;
    bool showStrip_ = true;
    int stripWidth_ = 196;
    double zoom_ = 1.0;
    double panX_ = 0, panY_ = 0;     // pixels the zoomed slide is scrolled by
    double stripScroll_ = 0;
    bool snapping_ = true;
    bool stripActive_ = false;       // the keys go to the strip (after a click in it)

    // A drag in progress: the slide drawn is a preview until the button is let go.
    Drag drag_ = Drag::NoDrag;
    Point2Df pressAt_;
    bool dragStarted_ = false;
    SlideHandle handle_ = SlideHandle::NoHandle;
    std::unique_ptr<PptxSlide> preview_;
    SlideBox startBox_;              // the selection's (or the one shape's) box at the press
    double startRotation_ = 0;
    double dragAngle_ = 0;           // a group's turn while its turning handle is dragged
    SlideBox bandBox_;
    SlideSnap guides_;
    int stripDropIndex_ = -1;
    int pressedStripSlide_ = -1;

    Rect2Dd StripRect() const;
    Rect2Dd CanvasRect() const;
    Rect2Dd ThumbRect(int index) const;
    int ThumbHeight() const;
    int StripSlideAt(const Point2Df& p) const;
    int StripDropAt(const Point2Df& p) const;
    double FitScale() const;
    Point2Dd ToSlide(const Point2Df& p) const;    // element pixels -> EMU
    Point2Dd ToView(double x, double y) const;     // EMU -> element pixels

    void DrawStrip(IRenderContext* ctx);
    void DrawCanvas(IRenderContext* ctx);
    void DrawSelection(IRenderContext* ctx, const PptxSlide& slide);
    void DrawFrame(IRenderContext* ctx, const PptxShape& shape, bool handles);
    void DrawHandles(IRenderContext* ctx, const std::vector<Point2Dd>& corners, bool rotate);

    bool OnPress(const UCEvent& event);
    bool OnMove(const UCEvent& event);
    bool OnRelease(const UCEvent& event);
    bool OnKey(const UCEvent& event);
    void UpdatePreview(const Point2Df& p, bool shift, bool alt);
    void UpdateCursor(const Point2Df& p);
    void EnsureThumbVisible(int index);
    void ClampPan();
    void EditTextOf(int shape);
};

inline std::shared_ptr<UltraCanvasSlideEdit> CreateSlideEdit(const std::string& id, int x, int y, int w, int h) {
    return std::make_shared<UltraCanvasSlideEdit>(id, x, y, w, h);
}

} // namespace UltraCanvas
