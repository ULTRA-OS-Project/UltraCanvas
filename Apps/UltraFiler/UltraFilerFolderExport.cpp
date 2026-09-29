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

namespace {

    constexpr float kFontSize = 9.0f;   // matches the main window's UI font

    // The tree's line characters, as `tree` draws them.
    const char* const kBranch     = "\xE2\x94\x9C\xE2\x94\x80\xE2\x94\x80 ";  // "├── "
    const char* const kLastBranch = "\xE2\x94\x94\xE2\x94\x80\xE2\x94\x80 ";  // "└── "
    const char* const kPipe       = "\xE2\x94\x82   ";                          // "│   "
    const char* const kGap        = "    ";

    struct ExportItem {
        std::string name;        // UTF-8
        fs::path    path;
        bool        isDirectory = false;
        bool        isLink = false;
        uint64_t    size = 0;
        std::time_t modified = 0;
    };

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

    // The entries of `dir`, sorted; false when the folder cannot be read.
    bool ReadFolder(const fs::path& dir, bool includeHidden, bool withDetails,
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
            if (withDetails) {
                dec.clear();
                if (!item.isDirectory) {
                    item.size = it->file_size(dec);
                    if (dec) item.size = 0;
                }
                dec.clear();
                const fs::file_time_type mt = it->last_write_time(dec);
                if (!dec) item.modified = ToTimeT(mt);
            }
            items.push_back(std::move(item));
        }
        std::sort(items.begin(), items.end(), ItemLess);
        return true;
    }

    std::string LinkSuffix(const ExportItem& item) {
        std::error_code ec;
        const fs::path target = fs::read_symlink(item.path, ec);
        return ec ? std::string() : " -> " + PathToUtf8(target);
    }

    // ----- Folder content -----

    std::string BuildContentText(const fs::path& root, bool includeHidden,
                                 const std::atomic<bool>* cancelled) {
        std::vector<ExportItem> items;
        if (!ReadFolder(root, includeHidden, true, cancelled, items))
            return "The folder cannot be read.\n";

        // The name column is as wide as the longest name, within reason: one
        // very long name should not push every size off the screen.
        size_t nameWidth = 4;
        for (const ExportItem& item : items)
            nameWidth = std::max(nameWidth,
                                 Utf8Length(item.name) + (item.isDirectory ? 1 : 0));
        nameWidth = std::min<size_t>(nameWidth, 60);
        constexpr size_t kSizeWidth = 10;

        std::string text;
        text += PadRight("Name", nameWidth) + "  " + PadLeft("Size", kSizeWidth) +
                "  Modified\n";
        text += std::string(nameWidth, '-') + "  " + std::string(kSizeWidth, '-') +
                "  " + std::string(16, '-') + "\n";

        size_t folders = 0, files = 0;
        uint64_t bytes = 0;
        for (const ExportItem& item : items) {
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
            std::string line = PadRight(name, nameWidth) + "  " +
                               PadLeft(size, kSizeWidth) + "  " +
                               FormatTime(item.modified);
            if (item.isLink) line += LinkSuffix(item);
            while (!line.empty() && line.back() == ' ') line.pop_back();
            text += line + "\n";
        }
        if (cancelled && cancelled->load()) text += "... (stopped)\n";
        text += "\n" + CountText(folders, "folder", "folders") + ", " +
                CountText(files, "file", "files") + ", " +
                FormatFileSize(static_cast<size_t>(bytes)) + "\n";
        return text;
    }

    // ----- Folder tree content -----

    struct TreeWalk {
        bool includeHidden = false;
        const std::atomic<bool>* cancelled = nullptr;
        size_t folders = 0;
        size_t files = 0;
        bool truncated = false;
        std::string text;

        bool Stopped() const {
            return truncated || (cancelled && cancelled->load());
        }

        // Writes the entries of `dir`, each line led by `prefix` - the
        // "│   " / "    " columns of the levels above.
        void Walk(const fs::path& dir, const std::string& prefix) {
            std::vector<ExportItem> items;
            if (!ReadFolder(dir, includeHidden, false, cancelled, items)) {
                text += prefix + kLastBranch + "[cannot read this folder]\n";
                return;
            }
            for (size_t i = 0; i < items.size(); ++i) {
                if (Stopped()) return;
                if (folders + files >= kMaxFolderExportEntries) {
                    truncated = true;
                    return;
                }
                const ExportItem& item = items[i];
                const bool last = i + 1 == items.size();
                std::string line = prefix + (last ? kLastBranch : kBranch) + item.name;
                if (item.isDirectory) {
                    line += "/";
                    ++folders;
                } else {
                    ++files;
                }
                if (item.isLink) line += LinkSuffix(item);
                text += line + "\n";
                if (item.isDirectory && !item.isLink)
                    Walk(item.path, prefix + (last ? kGap : kPipe));
            }
        }
    };

    std::string BuildTreeText(const fs::path& root, bool includeHidden,
                              const std::atomic<bool>* cancelled) {
        TreeWalk walk;
        walk.includeHidden = includeHidden;
        walk.cancelled = cancelled;
        std::string name = PathToUtf8(root.filename());
        if (name.empty()) name = PathToUtf8(root);   // a drive root: "C:\", "/"
        walk.text = name + "\n";
        walk.Walk(root, "");
        if (walk.truncated)
            walk.text += "... (stopped after " +
                         std::to_string(kMaxFolderExportEntries) + " entries)\n";
        else if (cancelled && cancelled->load())
            walk.text += "... (stopped)\n";
        walk.text += "\n" + CountText(walk.folders, "folder", "folders") + ", " +
                     CountText(walk.files, "file", "files") + "\n";
        return walk.text;
    }

} // namespace

std::string BuildFolderExportText(const std::string& folder, FolderExportKind kind,
                                  bool includeHidden,
                                  const std::atomic<bool>* cancelled) {
    const fs::path root = PathFromUtf8(folder);
    std::string text = (kind == FolderExportKind::Tree ? "Folder tree: " : "Folder: ") +
                       folder + "\n" +
                       "Exported: " + FormatTime(std::time(nullptr)) +
                       (includeHidden ? "" : "  (hidden entries left out)") + "\n\n";
    text += kind == FolderExportKind::Tree
                    ? BuildTreeText(root, includeHidden, cancelled)
                    : BuildContentText(root, includeHidden, cancelled);
    return text;
}

std::string FolderExportFileName(const std::string& folder, FolderExportKind kind) {
    std::string name = PathToUtf8(PathFromUtf8(folder).filename());
    if (name.empty()) name = "Folder";
    // A drive root has no name of its own, and what stands in for it ("C:")
    // is not something a file name may carry.
    for (char& c : name)
        if (c == ':' || c == '\\' || c == '/') c = '_';
    return name + (kind == FolderExportKind::Tree ? " - tree.txt" : " - content.txt");
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

    const std::string folderName = PathToUtf8(PathFromUtf8(folder).filename());
    WindowConfig wc;
    wc.title = std::string(kind == FolderExportKind::Tree ? "Folder tree content"
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
    textArea_->SetText(kind == FolderExportKind::Tree ? "Reading the folder tree ..."
                                                      : "Reading the folder ...",
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
    // Weak: the host may drop this object while its window is still open.
    std::weak_ptr<UltraFilerFolderExportWindow> self = weak_from_this();
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
        std::string text = BuildFolderExportText(folder, kind, includeHidden,
                                                 cancelled.get());
        if (cancelled->load()) return;
        UltraCanvasApplicationBase* app = UltraCanvasApplicationBase::GetCurrent();
        if (!app) return;
        app->PostToUIThread([self, text = std::move(text)]() {
            if (auto exportWindow = self.lock()) exportWindow->ShowText(text);
        });
    });
}

void UltraFilerFolderExportWindow::ShowText(const std::string& text) {
    if (worker_.joinable()) worker_.join();   // it posted this as its last act
    text_ = text;
    if (textArea_) textArea_->SetText(text, false);
    if (saveButton_) saveButton_->SetDisabled(false);
    if (statusLabel_) {
        size_t lines = 0;
        for (char c : text)
            if (c == '\n') ++lines;
        statusLabel_->SetText(std::to_string(lines) + " lines");
    }
}

void UltraFilerFolderExportWindow::Save() {
    if (text_.empty() || !window_) return;
    // What is in the text area, so an edit made before saving is kept.
    const std::string text = textArea_ ? textArea_->GetText() : text_;

    FileDialogOptions opts;
    opts.SetTitle("Save export")
        .SetInitialDirectory(folder_)
        .SetDefaultFileName(FolderExportFileName(folder_, kind_))
        .SetParentWindow(window_.get());
    opts.AddFilter("Text files", std::vector<std::string>{"txt"});
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
