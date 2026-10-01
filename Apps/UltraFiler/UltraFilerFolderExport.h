// Apps/UltraFiler/UltraFilerFolderExport.h
// "Extras > Export > Folder content / Folder tree content" — the folder as
// text: the listing of what one folder holds (name, size, modified), or the
// whole tree below it drawn with line characters the way the `tree` command
// draws it (├── └── │), or that tree as a ';'-separated CSV table (name,
// path, type, size, modified) for a spreadsheet. Sizes and dates are
// switched on and off in the window. UltraFilerFolderExportWindow reads the
// folder on a worker thread and shows the text in a window whose Save
// button writes it to a file of the user's choosing.
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
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
    Csv,       // the folder and everything below it, one CSV row per entry,
               // ';'-separated: Name;Path;Type;Size (bytes);Modified
};

// How far a tree export goes before it stops: a tree of a whole drive would
// otherwise grow a text nobody can scroll through, in memory, without end.
constexpr size_t kMaxFolderExportEntries = 200000;

// What the export shows besides the names - the window's "File size" and
// "Date" checkboxes.
struct FolderExportOptions {
    bool showSize = true;    // a file's size (and the files' total)
    bool showDate = false;   // the modification date
    // CSV only: a ' in front of each path, the spreadsheet's mark for "this
    // is text" - it shows the path as written instead of reading it.
    bool csvTextMarker = false;
};

// What each kind shows at first: the listing and the CSV both, the tree only
// the sizes, so its lines stay short.
FolderExportOptions DefaultFolderExportOptions(FolderExportKind kind);

// What a walk of the folder found: the entries in output order, with their
// sizes, dates and link targets, so an export can be written again with
// other options without reading the disk a second time.
struct FolderExportListing;

// Reads `folder` (a local folder, UTF-8) for an export of `kind`: its
// entries for Content, the whole tree below it otherwise. Hidden entries are
// left out unless `includeHidden`; symbolic links and junctions are listed
// but never entered, so a link loop cannot make the tree endless. The walk
// ends early - and the export says so - when `cancelled` becomes true or
// kMaxFolderExportEntries entries are listed. Safe to call on any thread.
std::shared_ptr<const FolderExportListing> ReadFolderForExport(
        const std::string& folder, FolderExportKind kind, bool includeHidden,
        const std::atomic<bool>* cancelled = nullptr);

// The export text of `listing` with `options`. Touches no file - quick
// enough to call on the UI thread whenever an option changes.
std::string FormatFolderExport(const FolderExportListing& listing,
                               const FolderExportOptions& options);

// Both in one: read and write the export of `folder`.
std::string BuildFolderExportText(const std::string& folder, FolderExportKind kind,
                                  bool includeHidden, const FolderExportOptions& options,
                                  const std::atomic<bool>* cancelled = nullptr);

// The file name Save proposes: "<folder name> - content.txt" /
// "<folder name> - tree.txt" / "<folder name> - files.csv".
std::string FolderExportFileName(const std::string& folder, FolderExportKind kind);

// The text window of one export: the text (editable, in a fixed-width face),
// and along the bottom the "File size" / "Date" checkboxes, a status line and
// Save / Close. Ticking a checkbox writes the text again at once, from what
// the walk already read. Keep it in a shared_ptr and call Open once;
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
    // On the UI thread: the finished walk, its text, and Save enabled.
    void ShowListing(std::shared_ptr<const FolderExportListing> listing);
    // Writes the text again from listing_ with the checkboxes' options.
    void Reformat();
    // Asks where to save and writes the text there (UTF-8).
    void Save();

    std::string folder_;
    FolderExportKind kind_ = FolderExportKind::Content;
    std::string text_;
    std::shared_ptr<UltraCanvasWindow>   window_;
    std::shared_ptr<UltraCanvasTextArea> textArea_;
    std::shared_ptr<UltraCanvasButton>   saveButton_;
    std::shared_ptr<UltraCanvasLabel>    statusLabel_;
    std::shared_ptr<UltraCanvasCheckbox> sizeCheck_;
    std::shared_ptr<UltraCanvasCheckbox> dateCheck_;
    std::shared_ptr<UltraCanvasCheckbox> textMarkerCheck_;   // CSV only
    FolderExportOptions options_;
    std::shared_ptr<const FolderExportListing> listing_;   // null while reading
    std::shared_ptr<std::atomic<bool>>   cancelled_ =
            std::make_shared<std::atomic<bool>>(false);
    std::thread worker_;
};

}  // namespace UltraCanvas
