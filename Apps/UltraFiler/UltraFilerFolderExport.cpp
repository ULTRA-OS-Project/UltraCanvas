// Apps/UltraFiler/UltraFilerFolderExport.cpp
// "Extras > Export": the folder content / folder tree as text, and the window
// that shows it with a Save button.
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "UltraFilerFolderExport.h"

#include "UltraCanvasAlert.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasUtils.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace UltraCanvas {

// One entry the walk found, with everything an export may show of it - read
// once, so switching sizes or dates on and off needs no second walk.
struct FolderExportEntry {
    std::string name;          // UTF-8
    fs::path    path;
    std::string linkTarget;    // UTF-8, empty unless isLink
    bool        isDirectory = false;
    bool        isLink = false;
    uint64_t    size = 0;
    std::time_t modified = 0;
};

// One line of the export: an entry, or the note for a folder that could not
// be read. `prefix` is the tree's "│   " / "    " columns of the levels
// above; `last` picks "└── " over "├── ".
struct FolderExportRow {
    FolderExportEntry entry;
    std::string prefix;
    bool last = false;
    bool unreadable = false;
};

struct FolderExportListing {
    std::string folder;
    FolderExportKind kind = FolderExportKind::Content;
    bool includeHidden = false;
    std::time_t readAt = 0;
    bool folderReadable = true;   // false: the folder itself could not be read
    bool truncated = false;       // stopped at kMaxFolderExportEntries
    bool stopped = false;         // cancelled before the end
    std::vector<FolderExportRow> rows;
};

namespace {

    using ExportItem = FolderExportEntry;

    constexpr float kFontSize = 9.0f;   // matches the main window's UI font

    // The tree's line characters, as `tree` draws them.
    const char* const kBranch     = "\xE2\x94\x9C\xE2\x94\x80\xE2\x94\x80 ";  // "├── "
    const char* const kLastBranch = "\xE2\x94\x94\xE2\x94\x80\xE2\x94\x80 ";  // "└── "
    const char* const kPipe       = "\xE2\x94\x82   ";                          // "│   "
    const char* const kGap        = "    ";

    std::string FoldAscii(const std::string& s) {
        std::string out = s;
        for (char& c : out)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        return out;
    }

    // Folders first, then by name ignoring the case of ASCII letters - the
    // order the file display lists them in.
    bool ItemLess(const ExportItem& a, const ExportItem& b) {
        if (a.isDirectory != b.isDirectory) return a.isDirectory;
        const std::string fa = FoldAscii(a.name), fb = FoldAscii(b.name);
        if (fa != fb) return fa < fb;
        return a.name < b.name;
    }

    std::time_t ToTimeT(fs::file_time_type t) {
        // file_clock has no portable to_time_t before every standard library
        // ships clock_cast; both clocks tick in step, so the offset between
        // their "now"s carries a file time across.
        using namespace std::chrono;
        const auto sys = time_point_cast<system_clock::duration>(
                t - fs::file_time_type::clock::now() + system_clock::now());
        return system_clock::to_time_t(sys);
    }

    std::string FormatTime(std::time_t t) {
        if (t == 0) return "";
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv);
        return buf;
    }

    // To the second, the way a spreadsheet reads a date and time.
    std::string FormatTimeSeconds(std::time_t t) {
        if (t == 0) return "";
        std::tm tmv{};
#ifdef _WIN32
        localtime_s(&tmv, &t);
#else
        localtime_r(&t, &tmv);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
        return buf;
    }

    // The field separator. A semicolon, as a spreadsheet set up for a
    // decimal comma (German Excel, for one) expects it - there a comma is
    // the decimal separator, and a comma-separated file opens as one column.
    constexpr char kCsvSeparator = ';';

    // One CSV field (RFC 4180 quoting): quoted when it holds the separator,
    // a comma, a quote or a line break, with its quotes doubled.
    std::string CsvField(const std::string& s) {
        if (s.find_first_of(";,\"\r\n") == std::string::npos) return s;
        std::string out = "\"";
        for (char c : s) {
            if (c == '"') out += '"';
            out += c;
        }
        return out + "\"";
    }

    // Characters, not bytes, so the columns line up behind "Übersicht".
    size_t Utf8Length(const std::string& s) {
        size_t n = 0;
        for (unsigned char c : s)
            if ((c & 0xC0) != 0x80) ++n;
        return n;
    }

    std::string PadRight(const std::string& s, size_t width) {
        const size_t len = Utf8Length(s);
        return len >= width ? s : s + std::string(width - len, ' ');
    }

    std::string PadLeft(const std::string& s, size_t width) {
        const size_t len = Utf8Length(s);
        return len >= width ? s : std::string(width - len, ' ') + s;
    }

    std::string CountText(size_t n, const char* one, const char* many) {
        return std::to_string(n) + " " + (n == 1 ? one : many);
    }

    // The entries of `dir` with their sizes, dates and link targets, sorted;
    // false when the folder cannot be read.
    bool ReadFolder(const fs::path& dir, bool includeHidden,
                    const std::atomic<bool>* cancelled,
                    std::vector<ExportItem>& items) {
        std::error_code ec;
        fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
        if (ec) return false;
        const fs::directory_iterator end;
        for (; it != end; it.increment(ec)) {
            if (ec || (cancelled && cancelled->load())) break;
            ExportItem item;
            item.path = it->path();
            if (!includeHidden && IsHiddenFileSystemEntry(item.path)) continue;
            item.name = PathToUtf8(item.path.filename());
            std::error_code dec;
            // A symbolic link (a junction on Windows) is listed, never
            // entered: a link back up the tree would make it endless.
            item.isLink = it->is_symlink(dec) && !dec;
            dec.clear();
            item.isDirectory = it->is_directory(dec) && !dec;
            dec.clear();
            if (!item.isDirectory) {
                item.size = it->file_size(dec);
                if (dec) item.size = 0;
            }
            dec.clear();
            const fs::file_time_type mt = it->last_write_time(dec);
            if (!dec) item.modified = ToTimeT(mt);
            if (item.isLink) {
                dec.clear();
                const fs::path target = fs::read_symlink(item.path, dec);
                if (!dec) item.linkTarget = PathToUtf8(target);
            }
            items.push_back(std::move(item));
        }
        std::sort(items.begin(), items.end(), ItemLess);
        return true;
    }

    std::string LinkSuffix(const ExportItem& item) {
        return item.linkTarget.empty() ? std::string() : " -> " + item.linkTarget;
    }

    // ----- The walk -----

    struct Walker {
        bool includeHidden = false;
        const std::atomic<bool>* cancelled = nullptr;
        FolderExportListing* listing = nullptr;

        bool Stopped() const {
            return listing->truncated || (cancelled && cancelled->load());
        }

        // The entries of `dir` in output order - each folder followed by
        // what it holds - down to the bottom of the tree when `recurse`.
        // `prefix` is the tree columns of the levels above.
        bool Walk(const fs::path& dir, const std::string& prefix, bool recurse) {
            std::vector<ExportItem> items;
            if (!ReadFolder(dir, includeHidden, cancelled, items)) {
                FolderExportRow note;
                note.prefix = prefix;
                note.last = true;
                note.unreadable = true;
                listing->rows.push_back(std::move(note));
                return false;
            }
            for (size_t i = 0; i < items.size(); ++i) {
                if (Stopped()) return true;
                if (listing->rows.size() >= kMaxFolderExportEntries) {
                    listing->truncated = true;
                    return true;
                }
                FolderExportRow row;
                row.entry = std::move(items[i]);
                row.prefix = prefix;
                row.last = i + 1 == items.size();
                const bool enter = recurse && row.entry.isDirectory && !row.entry.isLink;
                const fs::path sub = row.entry.path;
                const bool last = row.last;
                listing->rows.push_back(std::move(row));
                if (enter) Walk(sub, prefix + (last ? kGap : kPipe), true);
            }
            return true;
        }
    };

    // ----- Writing it out -----

    std::string FormatContent(const FolderExportListing& listing,
                              const FolderExportOptions& options) {
        if (!listing.folderReadable) return "The folder cannot be read.\n";

        // The name column is as wide as the longest name, within reason: one
        // very long name should not push every size off the screen.
        size_t nameWidth = 4;
        for (const FolderExportRow& row : listing.rows)
            nameWidth = std::max(nameWidth, Utf8Length(row.entry.name) +
                                            (row.entry.isDirectory ? 1 : 0));
        nameWidth = std::min<size_t>(nameWidth, 60);
        constexpr size_t kSizeWidth = 10;
        constexpr size_t kDateWidth = 16;

        std::string head = PadRight("Name", nameWidth);
        std::string rule = std::string(nameWidth, '-');
        if (options.showSize) {
            head += "  " + PadLeft("Size", kSizeWidth);
            rule += "  " + std::string(kSizeWidth, '-');
        }
        if (options.showDate) {
            head += "  Modified";
            rule += "  " + std::string(kDateWidth, '-');
        }
        while (!head.empty() && head.back() == ' ') head.pop_back();
        std::string text = head + "\n" + rule + "\n";

        size_t folders = 0, files = 0;
        uint64_t bytes = 0;
        for (const FolderExportRow& row : listing.rows) {
            const ExportItem& item = row.entry;
            std::string name = item.name, size;
            if (item.isDirectory) {
                name += "/";
                size = item.isLink ? "<LINK>" : "<DIR>";
                ++folders;
            } else {
                size = FormatFileSize(static_cast<size_t>(item.size));
                bytes += item.size;
                ++files;
            }
            std::string line = PadRight(name, nameWidth);
            if (options.showSize) line += "  " + PadLeft(size, kSizeWidth);
            if (options.showDate) line += "  " + FormatTime(item.modified);
            if (item.isLink) line += LinkSuffix(item);
            while (!line.empty() && line.back() == ' ') line.pop_back();
            text += line + "\n";
        }
        if (listing.stopped) text += "... (stopped)\n";
        text += "\n" + CountText(folders, "folder", "folders") + ", " +
                CountText(files, "file", "files");
        if (options.showSize) text += ", " + FormatFileSize(static_cast<size_t>(bytes));
        return text + "\n";
    }

    std::string FormatTree(const FolderExportListing& listing,
                           const FolderExportOptions& options) {
        const fs::path root = PathFromUtf8(listing.folder);
        std::string name = PathToUtf8(root.filename());
        if (name.empty()) name = listing.folder;   // a drive root: "C:\", "/"
        std::string text = name + "\n";

        size_t folders = 0, files = 0;
        uint64_t bytes = 0;
        for (const FolderExportRow& row : listing.rows) {
            const char* branch = row.last ? kLastBranch : kBranch;
            if (row.unreadable) {
                text += row.prefix + branch + "[cannot read this folder]\n";
                continue;
            }
            const ExportItem& item = row.entry;
            std::string line = row.prefix + branch + item.name;
            // What the options add, in brackets behind the name: the size of
            // a file, the date of anything.
            std::string details;
            if (item.isDirectory) {
                line += "/";
                ++folders;
            } else {
                ++files;
                bytes += item.size;
                if (options.showSize)
                    details = FormatFileSize(static_cast<size_t>(item.size));
            }
            if (options.showDate && item.modified != 0)
                details += (details.empty() ? "" : ", ") + FormatTime(item.modified);
            if (!details.empty()) line += "  (" + details + ")";
            if (item.isLink) line += LinkSuffix(item);
            text += line + "\n";
        }
        if (listing.truncated)
            text += "... (stopped after " +
                    std::to_string(kMaxFolderExportEntries) + " entries)\n";
        else if (listing.stopped)
            text += "... (stopped)\n";
        text += "\n" + CountText(folders, "folder", "folders") + ", " +
                CountText(files, "file", "files");
        if (options.showSize) text += ", " + FormatFileSize(static_cast<size_t>(bytes));
        return text + "\n";
    }

    std::string CsvTypeOf(const ExportItem& item) {
        if (item.isLink) return "Link";
        if (item.isDirectory) return "Folder";
        std::string ext = PathToUtf8(item.path.extension());
        if (ext.size() <= 1) return "File";
        ext.erase(0, 1);
        for (char& c : ext)
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        return ext + " file";
    }

    // One row per entry, in the order of the tree. Size and Modified are
    // columns the options add or leave out.
    std::string FormatCsv(const FolderExportListing& listing,
                          const FolderExportOptions& options) {
        std::string text = std::string("Name") + kCsvSeparator + "Path" +
                           kCsvSeparator + "Type";
        if (options.showSize) text += std::string(1, kCsvSeparator) + "Size";
        if (options.showDate) text += std::string(1, kCsvSeparator) + "Modified";
        text += "\n";
        for (const FolderExportRow& row : listing.rows) {
            if (row.unreadable) continue;   // a table has no place for a note
            const ExportItem& item = row.entry;
            const std::string path = PathToUtf8(item.path);
            text += CsvField(item.name) + kCsvSeparator +
                    CsvField(options.csvTextMarker ? "'" + path : path) +
                    kCsvSeparator + CsvField(CsvTypeOf(item));
            // The size in plain bytes: a number any spreadsheet sums,
            // whatever its decimal separator. Folders have none.
            if (options.showSize)
                text += kCsvSeparator +
                        (item.isDirectory ? std::string() : std::to_string(item.size));
            if (options.showDate)
                text += kCsvSeparator + FormatTimeSeconds(item.modified);
            text += "\n";
        }
        return text;
    }

} // namespace

FolderExportOptions DefaultFolderExportOptions(FolderExportKind kind) {
    FolderExportOptions options;
    options.showSize = true;
    options.showDate = kind != FolderExportKind::Tree;
    return options;
}

std::shared_ptr<const FolderExportListing> ReadFolderForExport(
        const std::string& folder, FolderExportKind kind, bool includeHidden,
        const std::atomic<bool>* cancelled) {
    auto listing = std::make_shared<FolderExportListing>();
    listing->folder = folder;
    listing->kind = kind;
    listing->includeHidden = includeHidden;
    listing->readAt = std::time(nullptr);

    Walker walker;
    walker.includeHidden = includeHidden;
    walker.cancelled = cancelled;
    walker.listing = listing.get();
    const bool recurse = kind != FolderExportKind::Content;
    if (!walker.Walk(PathFromUtf8(folder), "", recurse) && !recurse) {
        // The listing's one folder could not be read: it says so in place of
        // the table, rather than as a row.
        listing->rows.clear();
        listing->folderReadable = false;
    }
    listing->stopped = cancelled && cancelled->load();
    return listing;
}

std::string FormatFolderExport(const FolderExportListing& listing,
                               const FolderExportOptions& options) {
    // A CSV file is a table and nothing else: a heading above it would be
    // read as data rows.
    if (listing.kind == FolderExportKind::Csv) return FormatCsv(listing, options);
    std::string text = (listing.kind == FolderExportKind::Tree ? "Folder tree: "
                                                               : "Folder: ") +
                       listing.folder + "\n" +
                       "Exported: " + FormatTime(listing.readAt) +
                       (listing.includeHidden ? "" : "  (hidden entries left out)") +
                       "\n\n";
    text += listing.kind == FolderExportKind::Tree ? FormatTree(listing, options)
                                                   : FormatContent(listing, options);
    return text;
}

std::string BuildFolderExportText(const std::string& folder, FolderExportKind kind,
                                  bool includeHidden, const FolderExportOptions& options,
                                  const std::atomic<bool>* cancelled) {
    return FormatFolderExport(*ReadFolderForExport(folder, kind, includeHidden, cancelled),
                              options);
}

std::string FolderExportFileName(const std::string& folder, FolderExportKind kind) {
    std::string name = PathToUtf8(PathFromUtf8(folder).filename());
    if (name.empty()) name = "Folder";
    // A drive root has no name of its own, and what stands in for it ("C:")
    // is not something a file name may carry.
    for (char& c : name)
        if (c == ':' || c == '\\' || c == '/') c = '_';
    switch (kind) {
        case FolderExportKind::Tree: return name + " - tree.txt";
        case FolderExportKind::Csv:  return name + " - files.csv";
        default:                     return name + " - content.txt";
    }
}

// ===== THE EXPORT WINDOW =====

UltraFilerFolderExportWindow::~UltraFilerFolderExportWindow() {
    // The window may be gone first; the worker must not outlive this object,
    // which its result is handed to.
    if (cancelled_) cancelled_->store(true);
    if (worker_.joinable()) worker_.join();
}

void UltraFilerFolderExportWindow::Open(const std::string& folder, FolderExportKind kind,
                                        bool includeHidden,
                                        UltraCanvasWindowBase* parent) {
    folder_ = folder;
    kind_ = kind;
    options_ = DefaultFolderExportOptions(kind);

    const std::string folderName = PathToUtf8(PathFromUtf8(folder).filename());
    WindowConfig wc;
    wc.title = std::string(kind == FolderExportKind::Tree  ? "Folder tree content"
                           : kind == FolderExportKind::Csv ? "Folder tree as CSV"
                                                           : "Folder content") +
               " - " + (folderName.empty() ? folder : folderName) + " - UltraFiler";
    wc.width = 760;
    wc.height = 560;
    wc.resizable = true;
    wc.type = WindowType::Dialog;
    wc.parentWindow = parent;
    window_ = CreateWindow(wc);
    if (!window_ || !window_->IsCreated()) return;

    window_->layout.SetFlexColumn()
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    window_->SetBackgroundColor(Color(249, 249, 251, 255));
    window_->SetEventCallback([](const UCEvent& event) {
        if (event.type == UCEventType::KeyUp && event.virtualKey == UCKeys::Escape) {
            if (auto tw = event.targetWindow.lock())
                static_cast<UltraCanvasWindow*>(tw.get())->Close();
            return true;
        }
        return false;
    });
    // Closing the window ends a walk nobody is waiting for any more.
    auto cancelled = cancelled_;
    window_->onWindowClosed = [cancelled]() { cancelled->store(true); };

    textArea_ = std::make_shared<UltraCanvasTextArea>("uf-export-text", 0, 0, 700, 400);
    // A fixed-width face, so the tree's lines and the listing's columns
    // stand one under the other.
    textArea_->SetFont("monospace", 10.0f);
    textArea_->SetWordWrap(false);
    textArea_->SetShowLineNumbers(false);
    textArea_->SetText(kind == FolderExportKind::Content ? "Reading the folder ..."
                                                         : "Reading the folder tree ...",
                       false);
    textArea_->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                         .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    window_->AddChild(textArea_);

    auto bottom = std::make_shared<UltraCanvasContainer>("uf-export-bottom");
    bottom->layout.SetFlexRow().SetFlexGap(8)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    bottom->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    bottom->SetPadding(8, 12, 8, 12);
    bottom->SetBorderTop(1, Color(225, 225, 230, 255));

    // Weak: the host may drop this object while its window is still open.
    std::weak_ptr<UltraFilerFolderExportWindow> self = weak_from_this();

    // What the text shows besides the names. A change writes the text again
    // from what the walk already read - the disk is not walked a second time.
    auto makeCheck = [](const std::string& id, const std::string& label,
                        float width, bool checked) {
        auto c = UltraCanvasCheckbox::CreateCheckbox(id, -1, -1, width, 22, label, checked);
        c->SetFontSize(kFontSize);
        c->size.width  = CSSLayout::Dimension::Px(width);
        c->size.height = CSSLayout::Dimension::Px(22);
        c->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        return c;
    };
    sizeCheck_ = makeCheck("uf-export-size", "File size", 84, options_.showSize);
    sizeCheck_->onStateChanged = [self](CheckedState, CheckedState now) {
        if (auto exportWindow = self.lock()) {
            exportWindow->options_.showSize = now == CheckedState::Checked;
            exportWindow->Reformat();
        }
    };
    bottom->AddChild(sizeCheck_);
    dateCheck_ = makeCheck("uf-export-date", "Date", 64, options_.showDate);
    dateCheck_->onStateChanged = [self](CheckedState, CheckedState now) {
        if (auto exportWindow = self.lock()) {
            exportWindow->options_.showDate = now == CheckedState::Checked;
            exportWindow->Reformat();
        }
    };
    bottom->AddChild(dateCheck_);
    if (kind == FolderExportKind::Csv) {
        textMarkerCheck_ = makeCheck("uf-export-textmarker", "Add text marker", 124,
                                     options_.csvTextMarker);
        textMarkerCheck_->onStateChanged = [self](CheckedState, CheckedState now) {
            if (auto exportWindow = self.lock()) {
                exportWindow->options_.csvTextMarker = now == CheckedState::Checked;
                exportWindow->Reformat();
            }
        };
        bottom->AddChild(textMarkerCheck_);
    }

    statusLabel_ = std::make_shared<UltraCanvasLabel>("uf-export-status", 0, 0, 0, 20);
    statusLabel_->SetFontSize(kFontSize);
    statusLabel_->SetTextColor(Color(110, 110, 118, 255));
    statusLabel_->SetText("Reading ...");
    statusLabel_->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    bottom->AddChild(statusLabel_);

    auto makeButton = [](const std::string& id, const std::string& label) {
        auto b = std::make_shared<UltraCanvasButton>(id, 0, 0, 90, 28, label);
        b->SetFontSize(kFontSize);
        b->SetCornerRadius(4.0f);
        b->SetColors(Color(255, 255, 255, 255), Color(233, 238, 244, 255));
        b->SetTextColors(Color(40, 40, 44, 255));
        b->SetBorder(1.0f, Color(0, 0, 0, 60));
        b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        return b;
    };
    saveButton_ = makeButton("uf-export-save", "Save...");
    saveButton_->SetOnClick([self]() {
        if (auto exportWindow = self.lock()) exportWindow->Save();
    });
    saveButton_->SetDisabled(true);   // until there is something to save
    bottom->AddChild(saveButton_);

    UltraCanvasWindow* rawWindow = window_.get();
    auto closeButton = makeButton("uf-export-close", "Close");
    closeButton->SetOnClick([rawWindow]() { rawWindow->Close(); });
    bottom->AddChild(closeButton);
    window_->AddChild(bottom);

    window_->Show();

    // The walk runs off the UI thread: the tree of a big folder takes its
    // time, and the file display stays usable meanwhile. The result is handed
    // over on the UI thread, where this object is also destroyed - so the
    // weak pointer either finds it whole or not at all.
    worker_ = std::thread([self, cancelled, folder, kind, includeHidden]() {
        std::shared_ptr<const FolderExportListing> listing =
                ReadFolderForExport(folder, kind, includeHidden, cancelled.get());
        if (cancelled->load()) return;
        UltraCanvasApplicationBase* app = UltraCanvasApplicationBase::GetCurrent();
        if (!app) return;
        app->PostToUIThread([self, listing = std::move(listing)]() {
            if (auto exportWindow = self.lock()) exportWindow->ShowListing(listing);
        });
    });
}

void UltraFilerFolderExportWindow::ShowListing(
        std::shared_ptr<const FolderExportListing> listing) {
    if (worker_.joinable()) worker_.join();   // it posted this as its last act
    listing_ = std::move(listing);
    if (saveButton_) saveButton_->SetDisabled(false);
    Reformat();
}

void UltraFilerFolderExportWindow::Reformat() {
    // Still reading: the walk's result is written with whatever the
    // checkboxes say by the time it arrives.
    if (!listing_) return;
    // The text is written anew, so an edit made in it is replaced too.
    text_ = FormatFolderExport(*listing_, options_);
    if (textArea_) textArea_->SetText(text_, false);
    if (statusLabel_) {
        size_t lines = 0;
        for (char c : text_)
            if (c == '\n') ++lines;
        statusLabel_->SetText(std::to_string(lines) + " lines");
    }
}

void UltraFilerFolderExportWindow::Save() {
    if (text_.empty() || !window_) return;
    // What is in the text area, so an edit made before saving is kept.
    std::string text = textArea_ ? textArea_->GetText() : text_;
    const bool csv = kind_ == FolderExportKind::Csv;
    if (csv) {
        // The way spreadsheets expect it: CRLF rows (RFC 4180), and a UTF-8
        // byte order mark - without it Excel reads "Übersicht" as ANSI.
        std::string crlf = "\xEF\xBB\xBF";
        crlf.reserve(text.size() + text.size() / 16 + 3);
        for (char c : text) {
            if (c == '\n') crlf += '\r';
            crlf += c;
        }
        text = std::move(crlf);
    }

    FileDialogOptions opts;
    opts.SetTitle("Save export")
        .SetInitialDirectory(folder_)
        .SetDefaultFileName(FolderExportFileName(folder_, kind_))
        .SetParentWindow(window_.get());
    if (csv) opts.AddFilter("CSV files", std::vector<std::string>{"csv"});
    else     opts.AddFilter("Text files", std::vector<std::string>{"txt"});
    opts.AddFilter("All files", "*");

    std::weak_ptr<UltraFilerFolderExportWindow> self = weak_from_this();
    UltraCanvasFileLoader::SaveFileDialog(opts,
            [self, text](DialogResult result, const std::string& path) {
        if (result != DialogResult::OK || path.empty()) return;
        auto exportWindow = self.lock();
        UltraCanvasWindowBase* parent = exportWindow ? exportWindow->window_.get() : nullptr;
        std::FILE* f = OpenFileUtf8(path, "wb");
        bool ok = f != nullptr;
        if (f) {
            ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
            ok = std::fclose(f) == 0 && ok;
        }
        if (!ok) {
            UltraCanvasAlert::Error("Cannot write the file\n" + path, "Save export",
                                    nullptr, parent);
            return;
        }
        if (exportWindow && exportWindow->statusLabel_)
            exportWindow->statusLabel_->SetText("Saved to " + path);
    });
}

}  // namespace UltraCanvas
