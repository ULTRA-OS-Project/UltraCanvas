// Apps/UltraDesktop/ui/UltraDesktopClipboardPanel.h
// The clipboard quick panel: what the right bar's clipboard button and
// Super+V open (Docs/Research/UltraClipboard/UltraClipboard-QuickPanel.svg).
// A search field, the pinned entries and the most recent ones (typing
// searches the whole history), Copy / Edit / Delete on the row under the
// pointer or the keyboard, the recording switch and a way into the
// UltraClipboard application.
//
// An undecorated window of its own beside the bar: the desktop's window lies
// under every other, so a popup inside it would be covered by whatever window
// sits there. It closes when it loses the focus, on Escape, and after a copy.
//
// Keys: typing searches; Up / Down choose; Enter copies; F2 or Ctrl+E edits
// (in UltraClipboard); Delete deletes while the search field is empty
// (Ctrl+Delete always); Escape closes.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasTimer.h"
#include "UltraCanvasWindow.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {
    class UltraCanvasButton;
    class UltraCanvasClipboardHistory;
    class ClipboardHistoryListModel;
    class ClipboardHistoryRowDelegate;
    class UltraCanvasContainer;
    class UltraCanvasLabel;
    class UltraCanvasListView;
    class UltraCanvasSwitch;
    class UltraCanvasTextInput;
    struct UCEvent;
}

namespace UltraDesktop {

class UltraDesktopClipboardPanel {
public:
    struct Actions {
        // Put the entry back on the clipboard (the desktop marks it used).
        std::function<void(int64_t entryId)> copy;
        // Open the entry in UltraClipboard's edit dialog.
        std::function<void(int64_t entryId)> edit;
        // Open UltraClipboard, searching for `text` when it is not empty.
        std::function<void(const std::string& text)> openApplication;
    };

    UltraDesktopClipboardPanel(UltraCanvas::UltraCanvasClipboardHistory* history, Actions actions);
    ~UltraDesktopClipboardPanel();

    // `rightX`, `topY`: the panel's top right corner in screen pixels (the
    // bar's left edge beside the button); centred on the screen when both
    // are negative (Super+V).
    void Open(int rightX, int topY);
    void Close();
    bool IsOpen() const;
    // The history changed (in this process or another): list it again.
    void Refresh();

private:
    void Build();
    void Reload();
    bool OnKey(const UltraCanvas::UCEvent& event);
    int64_t SelectedEntry() const;
    void SelectRow(int row);
    void Move(int step);
    void CopyEntry(int64_t id);
    void EditEntry(int64_t id);
    void DeleteEntry(int64_t id);
    void Undo();
    void CloseSoon();

    UltraCanvas::UltraCanvasClipboardHistory* history_ = nullptr;
    Actions actions_;
    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> search_;
    std::shared_ptr<UltraCanvas::UltraCanvasListView> list_;
    std::shared_ptr<UltraCanvas::ClipboardHistoryListModel> model_;
    std::shared_ptr<UltraCanvas::ClipboardHistoryRowDelegate> rows_;
    std::shared_ptr<UltraCanvas::UltraCanvasSwitch> recording_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> status_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> undo_;
    int64_t removedId_ = 0;
    UltraCanvas::TimerId undoTimer_ = 0;
    bool open_ = false;
    bool closing_ = false;
};

} // namespace UltraDesktop
