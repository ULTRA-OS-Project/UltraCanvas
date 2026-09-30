// Apps/UltraFiler/UltraFilerFolderExport.h
// "Extras > Export > Folder content / Folder tree content" — the folder as
// text: the listing of what one folder holds (name, size, modified), or the
// whole tree below it drawn with line characters the way the `tree` command
// draws it (├── └── │), or that tree as a CSV table (name, path, type,
// size, modified) for a spreadsheet. UltraFilerFolderExportWindow builds the text on a
// worker thread and shows it in a text window whose Save button writes it to
// a file of the user's choosing.
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasWindow.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace UltraCanvas {

enum class FolderExportKind {
    Content,   // the entries directly in the folder, with size and date
    Tree,      // the folder and everything below it, drawn as a tree
    Csv,       // the folder and everything below it, one CSV row per entry:
               // Name, Path, Type, Size (bytes), Modified
};

// How far a tree export goes before it stops: a tree of a whole drive would
// otherwise grow a text nobody can scroll through, in memory, without end.
constexpr size_t kMaxFolderExportEntries = 200000;

// The export text of `folder` (a local folder, UTF-8). Hidden entries are
// left out unless `includeHidden`; symbolic links and junctions are listed
// but never entered, so a link loop cannot make the tree endless. The walk
// ends early - saying so in the text - when `cancelled` becomes true or
// kMaxFolderExportEntries entries are listed. Safe to call on any thread.
std::string BuildFolderExportText(const std::string& folder, FolderExportKind kind,
                                  bool includeHidden,
                                  const std::atomic<bool>* cancelled = nullptr);

// The file name Save proposes: "<folder name> - content.txt" /
// "<folder name> - tree.txt" / "<folder name> - files.csv".
std::string FolderExportFileName(const std::string& folder, FolderExportKind kind);

// The text window of one export: the text (editable, in a fixed-width face),
// a status line and Save / Close. Keep it in a shared_ptr and call Open once;
// the export is built on a worker thread this object owns, and destroying the
// object (or closing the window) stops that walk.
class UltraFilerFolderExportWindow
        : public std::enable_shared_from_this<UltraFilerFolderExportWindow> {
public:
    UltraFilerFolderExportWindow() = default;
    ~UltraFilerFolderExportWindow();
    UltraFilerFolderExportWindow(const UltraFilerFolderExportWindow&) = delete;
    UltraFilerFolderExportWindow& operator=(const UltraFilerFolderExportWindow&) = delete;

    // Shows the window with a "reading" note and starts building the export
    // of `folder`; the text replaces the note once it is ready.
    void Open(const std::string& folder, FolderExportKind kind, bool includeHidden,
              UltraCanvasWindowBase* parent);

    // Whether the window has been closed (the host may then let it go).
    bool IsClosed() const { return cancelled_->load(); }

private:
    // On the UI thread: the finished export, and Save enabled.
    void ShowText(const std::string& text);
    // Asks where to save and writes the text there (UTF-8).
    void Save();

    std::string folder_;
    FolderExportKind kind_ = FolderExportKind::Content;
    std::string text_;
    std::shared_ptr<UltraCanvasWindow>   window_;
    std::shared_ptr<UltraCanvasTextArea> textArea_;
    std::shared_ptr<UltraCanvasButton>   saveButton_;
    std::shared_ptr<UltraCanvasLabel>    statusLabel_;
    std::shared_ptr<std::atomic<bool>>   cancelled_ =
            std::make_shared<std::atomic<bool>>(false);
    std::thread worker_;
};

}  // namespace UltraCanvas
