// include/UltraCanvasFileDragIcon.h
// The icon of a file that is about to be written, which the user picks up and
// drops on a folder to say where it goes - the save box's file icon, as
// RISC OS has always had it. The framework's Save dialog puts a big one left
// of its name and type rows, "Drag to Folder" under it: dropped on a folder of
// the listing or of the folder tree, it makes that folder the one the file is
// saved in.
//
// It carries no file - there is none yet - so it is not a file drag: while
// it is dragged a badge (the icon and the name) follows the pointer over the
// window, and the element under the pointer is sent DragEnter / DragOver /
// DragLeave with dragMimeType kDragMimeType, the name in dragData and no
// droppedFiles. That is enough for a file display (UltraCanvasFilerWidget) to
// frame the folder a drop would land in, and for a folder tree whose
// onFilesDragAccept says yes to highlight the row. Nothing is sent a Drop:
// the release goes to onDropped instead, which asks the target where that is
// (UltraCanvasFilerWidget::GetIncomingDropFolder, the tree's row under the
// pointer) before the target is told the drag has left.
//
// The drag stays inside its window: beyond the edge the badge stops at the
// border, and a release there drops on nothing.
//
// Version: 1.1.0 - DrawDocumentGlyph: a type's logo, a page with a folded corner
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {

    class UltraCanvasFileDragIcon : public UltraCanvasUIElement {
    public:
        // The mime type of the drag events the icon sends.
        static constexpr const char* kDragMimeType = "application/x-ultracanvas-file-icon";

        // Draws the icon of `name` (a file name or a path; a folder when
        // `isFolder`) into `rect`, in the coordinates of the context it is
        // given. See SetIconPainter.
        using IconPainter = std::function<void(IRenderContext* ctx, const std::string& name,
                                               bool isFolder, const Rect2Di& rect)>;

        UltraCanvasFileDragIcon(const std::string& identifier, long x = 0, long y = 0,
                                long w = 28, long h = 28);
        ~UltraCanvasFileDragIcon() override;

        // ===== WHAT IT STANDS FOR =====
        // A file name or a path ("photo.png"), or a folder. The icon and the
        // badge that follows the pointer are drawn for it.
        void SetFileName(const std::string& name, bool isFolder = false);
        const std::string& GetFileName() const { return fileName; }
        bool IsFolder() const { return folder; }

        // The icon of the type. Without a painter: DrawDocumentGlyph in a
        // neutral blue (a folder shape for a folder). The file dialog draws
        // that page in the colour its listing gives the type, or the listing's
        // own icon when it shows this desktop's (UltraCanvasFilerWidget::
        // GetTypeColors, DrawTypeIcon).
        void SetIconPainter(IconPainter painter);

        // A file type's logo, as the Save dialog shows it big: a page with a
        // folded corner and the type's extension (upper case, up to four
        // letters) on a band of `band` in `ink` across its lower half. The
        // extension is left off below 28 pixels, where it could not be read.
        static void DrawDocumentGlyph(IRenderContext* ctx, const Rect2Di& rect,
                                      const std::string& name, const Color& band,
                                      const Color& ink);

        // ===== DRAGGING =====
        // How far (in pixels) the pointer moves with the button down before
        // a press becomes a drag. A press that never gets that far is a
        // click (onClick).
        void SetDragThreshold(int pixels) { dragThreshold = pixels < 1 ? 1 : pixels; }
        int GetDragThreshold() const { return dragThreshold; }
        bool IsDragging() const { return dragging; }
        // Ends a running drag as Escape does: nothing is dropped.
        void CancelDrag();

        // ===== CALLBACKS =====
        std::function<void()> onDragStarted;
        // The icon was released over `target` - the element under the pointer,
        // never the icon itself; null over nothing - at `windowPoint`. Runs
        // while the target still shows where the drop lands, before it is
        // sent DragLeave.
        std::function<void(UltraCanvasUIElement* target, const Point2Di& windowPoint)> onDropped;
        // Escape, or the pointer was taken away: nothing dropped.
        std::function<void()> onDragCancelled;
        // Pressed and released without dragging.
        std::function<void()> onClick;

        // ===== OVERRIDES =====
        bool AcceptsFocus() const override { return false; }
        bool OnEvent(const UCEvent& event) override;
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;

    private:
        std::string fileName;
        bool folder = false;
        IconPainter iconPainter;

        int dragThreshold = 4;
        bool pressed = false;          // left button down on the icon
        bool dragging = false;         // the press became a drag
        bool mouseCaptured = false;
        Point2Di pressPoint;           // window coordinates
        Point2Di dragPoint;            // window coordinates, while dragging
        // The element the badge is over and has been sent DragEnter. Weak:
        // it can leave the window while the drag runs.
        std::weak_ptr<UltraCanvasUIElement> hoverTarget;
        Size2Di badgeSize;
        bool overlayShown = false;

        void BeginDrag(const Point2Di& windowPoint);
        void UpdateDrag(const Point2Di& windowPoint);
        void FinishDrag(const Point2Di& windowPoint);
        // Releases the pointer, takes the badge away and tells the target the
        // drag has left; clears the gesture.
        void EndGesture();

        UltraCanvasUIElement* TargetAt(const Point2Di& windowPoint) const;
        void SendDragEvent(UltraCanvasUIElement* element, UCEventType type,
                           const Point2Di& windowPoint);
        void LeaveHoverTarget(const Point2Di& windowPoint);
        Point2Di ClampToWindow(const Point2Di& windowPoint) const;

        void MeasureBadge();
        Rect2Di BadgeRect(const Point2Di& windowPoint) const;
        void ShowBadge(const Point2Di& windowPoint);
        void HideBadge();
        void DrawBadge(IRenderContext* ctx, const Rect2Di& rect);
        // The icon into `rect`: the painter, or the plain sheet / folder.
        void DrawIcon(IRenderContext* ctx, const Rect2Di& rect);
        std::string BadgeLabel() const;
    };

    inline std::shared_ptr<UltraCanvasFileDragIcon> CreateFileDragIcon(
            const std::string& identifier, const std::string& fileName,
            long x = 0, long y = 0, long w = 28, long h = 28) {
        auto icon = std::make_shared<UltraCanvasFileDragIcon>(identifier, x, y, w, h);
        icon->SetFileName(fileName);
        return icon;
    }

} // namespace UltraCanvas
