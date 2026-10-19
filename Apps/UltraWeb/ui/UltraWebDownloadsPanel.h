// Apps/UltraWeb/ui/UltraWebDownloadsPanel.h
// The downloads panel: one row per download of this run (DownloadList), each
// with its name, how far it got or how it ended, and what can be done with
// it - Cancel while it runs; Open, Show folder and Remove once it is saved.
// The window places the panel above its status bar and says what Cancel,
// Open and Show folder do (onCancel, onOpen, onShowFolder).
// Version: 0.1.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "../host/UltraWebDownloadList.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"

#include <functional>
#include <map>
#include <memory>

namespace UltraWeb {

class UltraWebDownloadsPanel {
public:
    // `post` runs a task later on the UI thread: rows are rebuilt there, so a
    // row's own Remove button is never destroyed inside its click.
    UltraWebDownloadsPanel(DownloadList& list, std::function<void(std::function<void()>)> post);

    // The panel, for the window to place.
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Root() const { return root_; }

    // The list changed: rows are rebuilt (on a later turn), or - for
    // progress - one row's state line is rewritten at once.
    void Update(int id, bool progressOnly);

    // A row's parts, by download id (tests find the buttons here).
    struct Row {
        std::shared_ptr<UltraCanvas::UltraCanvasContainer> box;
        std::shared_ptr<UltraCanvas::UltraCanvasLabel> name;
        std::shared_ptr<UltraCanvas::UltraCanvasLabel> state;
        std::shared_ptr<UltraCanvas::UltraCanvasButton> cancel;      // running only
        std::shared_ptr<UltraCanvas::UltraCanvasButton> open;        // saved only
        std::shared_ptr<UltraCanvas::UltraCanvasButton> showFolder;  // saved only
        std::shared_ptr<UltraCanvas::UltraCanvasButton> remove;      // finished only
    };
    const Row* RowFor(int id) const;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> ClearButton() const { return clearButton_; }
    std::shared_ptr<UltraCanvas::UltraCanvasButton> CloseButton() const { return closeButton_; }

    std::function<void(int id)> onCancel;
    std::function<void(const DownloadEntry&)> onOpen;
    std::function<void(const DownloadEntry&)> onShowFolder;
    std::function<void()> onClose;

private:
    void Rebuild();
    void ScheduleRebuild();
    Row MakeRow(const DownloadEntry& entry);

    DownloadList& list_;
    std::function<void(std::function<void()>)> post_;
    bool rebuildPending_ = false;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> rows_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> empty_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> clearButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> closeButton_;
    std::map<int, Row> rowById_;
};

} // namespace UltraWeb
