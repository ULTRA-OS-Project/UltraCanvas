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
//   Ctrl+D            duplicate            Ctrl+C / X / V the shapes
//   Ctrl+Z / Y        undo / redo          Page Up / Down the slide before / after
//   Tab               the next shape       Enter, F2 its text (onEditText)
//   Ctrl+wheel        zoom                 Ctrl+M a new slide
// In the strip: click a slide, drag it to move it, Delete deletes it;
// right-click (strip or slide) calls onContextMenu.
//
// doc-check: std::shared_ptr<PptxPresentation> deck;
// doc-check: example
//   auto edit = CreateSlideEdit("deck", 0, 0, 1200, 700);
//   edit->SetDeck(deck);
//   edit->onEditText = [edit](int shape) { /* ask for the text */ edit->Editor().SetShapeText(shape, "Hello"); };
//
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

    // ===== THE SHOW =====
    // The slides one after another, each as large as the element, on black,
    // the hidden ones left out, and a black end screen after the last: a
    // click, Space, Enter, the right and down arrows and Page Down go on;
    // Backspace, the left and up arrows and Page Up go back; Home and End;
    // Escape ends it (onShowEnd). The host puts the element in a window of
    // its own, full screen (Phase 4 brings transitions).
    void StartShow(int fromSlide = 0);
    void StopShow();
    bool IsShowing() const { return showing_; }
    int GetShowSlide() const { return showSlide_; }
    std::function<void()> onShowEnd;

    // ===== CALLBACKS =====
    std::function<void()> onChange;              // the deck changed (and undo / redo)
    std::function<void()> onSelectionChange;     // the slide or the selection changed
    // A shape's text is to be edited (double-click, Enter, F2).
    std::function<void(int shape)> onEditText;
    // A table's cell is to be edited (double-click on a table).
    std::function<void(int shape, int row, int col)> onEditTableCell;
    // Right-click: `slide` the slide under it in the strip (or the slide
    // shown), `shape` the shape under it on the slide (-1: none, or in the
    // strip); `windowX`, `windowY` where, for a menu.
    std::function<void(bool inStrip, int slide, int shape, int windowX, int windowY)> onContextMenu;

    // ===== ELEMENT =====
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    bool OnEvent(const UCEvent& event) override;
    bool AcceptsFocus() const override { return true; }

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
    enum class Drag { NoDrag, Band, Move, Resize, Rotate, StripSlide };

    int editingShape_ = -1;
    int editingCellShape_ = -1, editingCellRow_ = -1, editingCellCol_ = -1;
    bool showing_ = false;
    int showSlide_ = -1;             // -1 in the show: the end screen
    void ShowStep(int direction);
    void DrawShow(IRenderContext* ctx);
    bool OnShowEvent(const UCEvent& event);

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
