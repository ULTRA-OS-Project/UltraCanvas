// include/UltraCanvasClipboardHistoryPreview.h
// The hover preview of a clipboard history list: resting the pointer on an
// image's thumbnail (or on the thumbnail of copied files whose first file the
// media viewer shows - a picture, a PDF, a drawing, a 3D model, a video's
// first frame) opens the entry at full size in an UltraCanvasMediaViewer laid
// over the list, in a 12 px frame, in one of two styles
// (ClipboardHistoryPolicy::previewStyle): Modern, a pane over the right 60 %
// of the list with the viewer's info line, so the thumbnails and the start of
// every title stay readable beside it; or Classic, the picture alone,
// floating over the middle of the list at the size it fits in 60 % of the
// list's width. It opens after the pointer has rested 150 ms, stays while
// the pointer is on that entry's row or on the preview itself (where the
// wheel zooms), follows the wheel scrolling the list under a still pointer,
// and closes on Escape or when the pointer goes elsewhere. UltraClipboard's
// window and UltraDesktop's quick panel share it:
//
//   preview_ = std::make_unique<ClipboardHistoryPreview>(history, list.get(), model.get(), rows.get());
//   list->onCellHovered = [this](int row, int, const Point2Di& at) {
//       if (rows_->SetHover(row, at)) list_->RequestRedraw();
//       preview_->OnCellHovered(row, at);
//   };
//   window->AddChild(list);
//   ...
//   preview_->Attach(window);                   // after the list: it lies over it
//   // on a reload: preview_->SetStyle(history.GetPolicy().previewStyle);
//   // in the window's key filter: if (preview_->HandleKey(event)) return true;
//
// The viewer shows files: an image entry is written once to
// ClipboardHandOffFolder() and removed when the preview goes. The preview
// holds the list, model, delegate and history raw: they outlive it (the
// host destroys it first, before the window).
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasClipboardHistory.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasTimer.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>

namespace UltraCanvas {

class ClipboardHistoryListModel;
class ClipboardHistoryRowDelegate;
class UltraCanvasContainer;
class UltraCanvasListView;
class UltraCanvasMediaViewer;
class UltraCanvasWindowBase;
struct UCEvent;

class ClipboardHistoryPreview {
public:
    ClipboardHistoryPreview(UltraCanvasClipboardHistory* history, UltraCanvasListView* list,
                            ClipboardHistoryListModel* model, ClipboardHistoryRowDelegate* rows);
    ~ClipboardHistoryPreview();
    ClipboardHistoryPreview(const ClipboardHistoryPreview&) = delete;
    ClipboardHistoryPreview& operator=(const ClipboardHistoryPreview&) = delete;

    // Adds the preview to `window` as its last child, over the list, and
    // installs the pointer filter and the timer it runs on. Once; after the
    // list has been added to the window.
    void Attach(UltraCanvasWindowBase* window);

    // From the list's onCellHovered, on every pointer move over it and again
    // when it scrolls under a still pointer.
    void OnCellHovered(int row, const Point2Di& posInCell);
    // From the window's key filter: Escape closes an open preview. True when
    // the key was taken.
    bool HandleKey(const UCEvent& event);

    void SetStyle(ClipboardPreviewStyle style);
    ClipboardPreviewStyle GetStyle() const { return style_; }
    // The share of the list's width the preview takes (0.6), and the frame
    // around the picture (12 px).
    void SetShare(float share);
    void SetFrame(float pixels);

    void Hide();
    bool IsShowing() const { return previewId_ != 0; }

private:
    void Build();
    bool OnPointerMoved(const UCEvent& event);
    void Tick();
    void RequestPreview(int64_t id);     // after a short rest on the thumbnail
    void Show(const ClipboardHistoryEntry& entry);
    void ApplyStyle();
    void Place();
    // The file the viewer shows for an entry: an image written to the
    // hand-off folder (once per entry), the first of copied files when the
    // viewer can show it; "" when there is nothing to preview.
    std::string FileFor(const ClipboardHistoryEntry& entry);

    UltraCanvasClipboardHistory* history_;
    UltraCanvasListView* list_;
    ClipboardHistoryListModel* model_;
    ClipboardHistoryRowDelegate* rows_;
    UltraCanvasWindowBase* window_ = nullptr;
    std::shared_ptr<UltraCanvasContainer> panel_;
    std::shared_ptr<UltraCanvasMediaViewer> viewer_;

    ClipboardPreviewStyle style_ = ClipboardPreviewStyle::Modern;
    float share_ = 0.6f;
    float frame_ = 12.0f;
    int64_t previewId_ = 0;              // the entry on show; 0 while hidden
    int64_t wantedId_ = 0;               // the thumbnail the pointer rests on, until the delay passes
    bool leaveCheck_ = false;            // the list lost the pointer: on the preview, or gone?
    int pictureWidth_ = 0;               // the picture on show, in pixels; 0 when not known (a file)
    int pictureHeight_ = 0;
    TimerId delayTimer_ = 0;
    TimerId tickTimer_ = 0;
    Rect2Df listBounds_;                 // the list as the preview was last placed over it
    Point2Di pointer_;                   // where the pointer last was, in window coordinates
    std::map<int64_t, std::filesystem::path> files_;   // images written for the viewer
};

} // namespace UltraCanvas
