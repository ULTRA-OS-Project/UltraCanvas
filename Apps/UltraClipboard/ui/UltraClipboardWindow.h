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
// clears the search.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasApplication.h"
#include "UltraCanvasTimer.h"
#include "UltraCanvasWindow.h"

#include <cstdint>
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

    UltraCanvas::TimerId timer_ = 0;
    uint64_t generation_ = 0;
    int64_t removedId_ = 0;
    int generationTicks_ = 0;
    int messageTicks_ = 0;
    int undoTicks_ = 0;
    bool copiedShowing_ = false;
    bool updatingRecording_ = false;
    int64_t pendingEdit_ = 0;
};

} // namespace UltraClipboard
