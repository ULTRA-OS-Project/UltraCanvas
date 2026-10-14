// Apps/UltraClipboard/ui/UltraClipboardWindow.h
// UltraClipboard's main window: the whole clipboard history as a list
// (Docs/Research/UltraClipboard/UltraClipboard-Overview.svg). A search field,
// a filter by kind, the recording switch, settings; pinned entries first,
// then by day; a thumbnail and Copy / Edit / Delete on every row; a status
// line with the counts, and Undo after a removal.
//
// The history is the one UltraDesktop records into; this window shows it and
// edits it, and reloads when the desktop records (the history's generation
// number). Where no desktop records - another desktop, Windows, macOS - the
// window records itself while it is open.
//
// Keys: typing in the search field filters; Up / Down choose; Enter copies;
// F2 or Ctrl+E edits; Delete deletes; Ctrl+P pins; Ctrl+F finds; Escape
// closes the preview, then clears the search.
//
// The preview: resting the pointer on an image's thumbnail (or on the
// thumbnail of copied files whose first file the media viewer shows - a
// picture, a PDF, a drawing, a 3D model, a video's first frame) opens the
// entry at full size in an UltraCanvasMediaViewer, in a 12 px frame, in one
// of two styles (Settings, "Image preview"; ClipboardHistoryPolicy::
// previewStyle): Modern, a pane over the right 60 % of the list with the
// viewer's info line, so the thumbnails and the start of every title stay
// readable beside it; or Classic, the picture alone, floating over the
// middle of the list at the size it fits in 60 % of the list's width. It
// stays while the pointer is on that row or on the preview itself, where
// the wheel zooms; it follows the wheel scrolling the list; Escape or
// moving elsewhere closes it.
// Version: 0.1.3
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasApplication.h"
#include "UltraCanvasClipboardHistory.h"   // ClipboardPreviewStyle
#include "UltraCanvasTimer.h"
#include "UltraCanvasWindow.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {
    class UltraCanvasButton;
    class UltraCanvasClipboardHistory;
    class UltraCanvasClipboardRecorder;
    class ClipboardHistoryListModel;
    class ClipboardHistoryRowDelegate;
    class UltraCanvasContainer;
    class UltraCanvasLabel;
    class UltraCanvasListView;
    class UltraCanvasMediaViewer;
    class UltraCanvasMenu;
    class UltraCanvasSegmentedControl;
    class UltraCanvasSwitch;
    class UltraCanvasTextInput;
    struct ClipboardHistoryEntry;
    struct UCEvent;
}

namespace UltraClipboard {

class UltraClipboardWindow {
public:
    explicit UltraClipboardWindow(UltraCanvas::UltraCanvasApplication& app);
    ~UltraClipboardWindow();

    // `search`: start with this search; `editId`: open that entry's edit
    // dialog at once (the desktop's quick panel asks for both).
    bool Create(const std::string& search, int64_t editId);
    void Show();
    // Another start of UltraClipboard handed this over (UltraCanvasSingleInstance): the
    // window comes forward with that search, or that entry's edit dialog.
    void Present(const std::string& search, int64_t editId);

private:
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildToolbar();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildStatusBar();
    void Reload();
    void UpdateStatus();
    void Tick();
    bool OnKey(const UltraCanvas::UCEvent& event);

    const UltraCanvas::ClipboardHistoryEntry* SelectedEntry() const;
    void SelectRow(int row);
    void Move(int step);

    void CopyEntry(int64_t id);
    void EditEntry(int64_t id);
    void DeleteEntry(int64_t id);
    void Undo();
    void TogglePin(int64_t id);
    void OpenInUltraPaint(int64_t id);
    void ShowRowMenu(int row, const UltraCanvas::UCEvent& event);
    void ShowMoreMenu();
    void OpenSettings();
    void ConfirmClear();
    void SetRecording(bool on);
    void Message(const std::string& text);

    // The hover preview (see the top of this file).
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildPreview();
    void OnCellHovered(int row, const UltraCanvas::Point2Di& posInCell);
    bool OnPointerMoved(const UltraCanvas::UCEvent& event);
    void RequestPreview(int64_t id);     // after a short rest on the thumbnail
    void ShowPreview(const UltraCanvas::ClipboardHistoryEntry& entry);
    void HidePreview();
    void ApplyPreviewStyle();            // the policy's style onto the viewer and the panel
    void PlacePreview();                 // Modern: the right 60 % of the list; Classic: centred, sized to the picture
    // The file the viewer shows for an entry: an image written to the
    // hand-off folder (once per entry), the first of copied files when the
    // viewer can show it; "" when there is nothing to preview.
    std::string PreviewFileFor(const UltraCanvas::ClipboardHistoryEntry& entry);

    UltraCanvas::UltraCanvasApplication& app_;
    std::unique_ptr<UltraCanvas::UltraCanvasClipboardHistory> history_;
    std::unique_ptr<UltraCanvas::UltraCanvasClipboardRecorder> recorder_;
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> search_;
    std::shared_ptr<UltraCanvas::UltraCanvasSegmentedControl> kinds_;
    std::shared_ptr<UltraCanvas::UltraCanvasSwitch> recording_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> moreButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView> list_;
    std::shared_ptr<UltraCanvas::ClipboardHistoryListModel> model_;
    std::shared_ptr<UltraCanvas::ClipboardHistoryRowDelegate> rows_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> status_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> message_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> undo_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> hint_;
    std::shared_ptr<UltraCanvas::UltraCanvasMenu> menu_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> preview_;
    std::shared_ptr<UltraCanvas::UltraCanvasMediaViewer> previewViewer_;

    UltraCanvas::TimerId timer_ = 0;
    uint64_t generation_ = 0;
    int64_t removedId_ = 0;
    int generationTicks_ = 0;
    int messageTicks_ = 0;
    int undoTicks_ = 0;
    bool copiedShowing_ = false;
    bool updatingRecording_ = false;
    int64_t pendingEdit_ = 0;

    int64_t previewId_ = 0;              // the entry on show; 0 while hidden
    int64_t previewWantedId_ = 0;        // the thumbnail the pointer rests on, until the delay passes
    bool previewLeaveCheck_ = false;     // the list lost the pointer: on the preview, or gone?
    UltraCanvas::ClipboardPreviewStyle previewStyle_ = UltraCanvas::ClipboardPreviewStyle::Modern;
    int previewWidth_ = 0;               // the picture on show, in pixels; 0 when not known (a file)
    int previewHeight_ = 0;
    UltraCanvas::TimerId previewTimer_ = 0;
    UltraCanvas::Rect2Df previewListBounds_;   // the list as the preview was last placed over it
    UltraCanvas::Point2Di pointer_;      // where the pointer last was, in window coordinates
    std::filesystem::path previewFolder_;
    std::map<int64_t, std::filesystem::path> previewFiles_;   // images written for the viewer
};

} // namespace UltraClipboard
