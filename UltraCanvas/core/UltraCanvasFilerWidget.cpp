// core/UltraCanvasFilerWidget.cpp
// Filer folder widget: displays one folder's content with selectable view types
// (details, list, thumbnails, size bars, treemap), sorting, an inline rename
// editor, a hover icon menu and the full file context menu.
// Version: 1.0.0
// Last Modified: 2026-07-12
// Author: UltraCanvas Framework

// VirtualFS + bridge must be included before the UI headers: X11 (pulled in
// via UltraCanvasApplication.h) #defines None/Success, which collide with the
// None enumerators in VirtualFS and UCVFSCompressionType.
#ifdef ULTRACANVAS_HAS_VIRTUALFS
#include "VirtualFS/VirtualFS.h"
#include "UltraCanvasVirtualFSBridge.h"
#endif

#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasWindow.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sys/stat.h>

namespace fs = std::filesystem;

namespace UltraCanvas {

    std::vector<std::string> UltraCanvasFilerWidget::clipboardPaths;
    bool UltraCanvasFilerWidget::clipboardCut = false;

    namespace {
        constexpr int kWheelStep = 64;                 // px per wheel notch
        constexpr uint64_t kDirSizeEntryCap = 50000;   // recursive-size safety cap

        int clampi(int v, int lo, int hi) {
            return v < lo ? lo : (v > hi ? hi : v);
        }

        int CompareNoCase(const std::string& a, const std::string& b) {
            size_t n = std::min(a.size(), b.size());
            for (size_t i = 0; i < n; ++i) {
                int ca = std::tolower(static_cast<unsigned char>(a[i]));
                int cb = std::tolower(static_cast<unsigned char>(b[i]));
                if (ca != cb) return ca < cb ? -1 : 1;
            }
            if (a.size() == b.size()) return 0;
            return a.size() < b.size() ? -1 : 1;
        }

        // Extension -> (type label, category). The label is completed to
        // "<LABEL> <category noun>" ("PNG Image") in ApplyEntryTypeInfo.
        struct TypeInfo {
            const char* label;
            FilerFileCategory category;
        };

        const std::map<std::string, TypeInfo>& ExtensionTypeMap() {
            static const std::map<std::string, TypeInfo> m = {
                {"png",  {"PNG",  FilerFileCategory::Image}},
                {"jpg",  {"JPEG", FilerFileCategory::Image}},
                {"jpeg", {"JPEG", FilerFileCategory::Image}},
                {"gif",  {"GIF",  FilerFileCategory::Image}},
                {"bmp",  {"BMP",  FilerFileCategory::Image}},
                {"webp", {"WebP", FilerFileCategory::Image}},
                {"avif", {"AVIF", FilerFileCategory::Image}},
                {"heif", {"HEIF", FilerFileCategory::Image}},
                {"heic", {"HEIC", FilerFileCategory::Image}},
                {"tif",  {"TIFF", FilerFileCategory::Image}},
                {"tiff", {"TIFF", FilerFileCategory::Image}},
                {"qoi",  {"QOI",  FilerFileCategory::Image}},
                {"ico",  {"Icon", FilerFileCategory::Image}},
                {"svg",  {"SVG",  FilerFileCategory::Vector}},
                {"eps",  {"EPS",  FilerFileCategory::Vector}},
                {"cdr",  {"CorelDRAW", FilerFileCategory::Vector}},
                {"xar",  {"Xara", FilerFileCategory::Vector}},
                {"mp3",  {"MP3",  FilerFileCategory::Audio}},
                {"wav",  {"WAV",  FilerFileCategory::Audio}},
                {"flac", {"FLAC", FilerFileCategory::Audio}},
                {"ogg",  {"OGG",  FilerFileCategory::Audio}},
                {"m4a",  {"M4A",  FilerFileCategory::Audio}},
                {"aac",  {"AAC",  FilerFileCategory::Audio}},
                {"opus", {"Opus", FilerFileCategory::Audio}},
                {"mp4",  {"MP4",  FilerFileCategory::Video}},
                {"mkv",  {"MKV",  FilerFileCategory::Video}},
                {"avi",  {"AVI",  FilerFileCategory::Video}},
                {"mov",  {"QuickTime", FilerFileCategory::Video}},
                {"webm", {"WebM", FilerFileCategory::Video}},
                {"wmv",  {"WMV",  FilerFileCategory::Video}},
                {"pdf",  {"PDF",  FilerFileCategory::Document}},
                {"odt",  {"OpenDocument", FilerFileCategory::Document}},
                {"doc",  {"Word", FilerFileCategory::Document}},
                {"docx", {"Word", FilerFileCategory::Document}},
                {"rtf",  {"RTF",  FilerFileCategory::Document}},
                {"md",   {"Markdown", FilerFileCategory::Document}},
                {"html", {"HTML", FilerFileCategory::Document}},
                {"htm",  {"HTML", FilerFileCategory::Document}},
                {"tex",  {"LaTeX", FilerFileCategory::Document}},
                {"epub", {"EPUB", FilerFileCategory::Document}},
                {"txt",  {"Text", FilerFileCategory::Text}},
                {"log",  {"Log",  FilerFileCategory::Text}},
                {"ini",  {"Config", FilerFileCategory::Text}},
                {"conf", {"Config", FilerFileCategory::Text}},
                {"json", {"JSON", FilerFileCategory::Text}},
                {"xml",  {"XML",  FilerFileCategory::Text}},
                {"yaml", {"YAML", FilerFileCategory::Text}},
                {"yml",  {"YAML", FilerFileCategory::Text}},
                {"csv",  {"CSV",  FilerFileCategory::Text}},
                {"cpp",  {"C++ Source", FilerFileCategory::Text}},
                {"cc",   {"C++ Source", FilerFileCategory::Text}},
                {"h",    {"C Header", FilerFileCategory::Text}},
                {"hpp",  {"C++ Header", FilerFileCategory::Text}},
                {"c",    {"C Source", FilerFileCategory::Text}},
                {"py",   {"Python", FilerFileCategory::Text}},
                {"js",   {"JavaScript", FilerFileCategory::Text}},
                {"ts",   {"TypeScript", FilerFileCategory::Text}},
                {"sh",   {"Shell Script", FilerFileCategory::Text}},
                {"ods",  {"OpenDocument", FilerFileCategory::Spreadsheet}},
                {"xls",  {"Excel", FilerFileCategory::Spreadsheet}},
                {"xlsx", {"Excel", FilerFileCategory::Spreadsheet}},
                {"zip",  {"ZIP",  FilerFileCategory::Archive}},
                {"7z",   {"7-Zip", FilerFileCategory::Archive}},
                {"rar",  {"RAR",  FilerFileCategory::Archive}},
                {"tar",  {"TAR",  FilerFileCategory::Archive}},
                {"gz",   {"GZip", FilerFileCategory::Archive}},
                {"tgz",  {"TAR GZip", FilerFileCategory::Archive}},
                {"bz2",  {"BZip2", FilerFileCategory::Archive}},
                {"xz",   {"XZ",   FilerFileCategory::Archive}},
                {"zst",  {"Zstandard", FilerFileCategory::Archive}},
                {"jar",  {"Java Archive", FilerFileCategory::Archive}},
                {"exe",  {"Executable", FilerFileCategory::Executable}},
                {"appimage", {"AppImage", FilerFileCategory::Executable}},
                {"deb",  {"Debian Package", FilerFileCategory::Executable}},
                {"rpm",  {"RPM Package", FilerFileCategory::Executable}},
                {"so",   {"Shared Library", FilerFileCategory::Executable}},
                {"dll",  {"Library", FilerFileCategory::Executable}},
            };
            return m;
        }

        const char* CategoryNoun(FilerFileCategory c) {
            switch (c) {
                case FilerFileCategory::Folder:      return "Folder";
                case FilerFileCategory::Image:       return "Image";
                case FilerFileCategory::Vector:      return "Vector";
                case FilerFileCategory::Audio:       return "Audio";
                case FilerFileCategory::Video:       return "Video";
                case FilerFileCategory::Document:    return "Document";
                case FilerFileCategory::Text:        return "Text";
                case FilerFileCategory::Spreadsheet: return "Spreadsheet";
                case FilerFileCategory::Archive:     return "Archive";
                case FilerFileCategory::Executable:  return "Program";
                default:                             return "File";
            }
        }

        Color CategoryColor(FilerFileCategory c) {
            switch (c) {
                case FilerFileCategory::Folder:      return Color(247, 190, 80, 255);
                case FilerFileCategory::Image:       return Color(76, 175, 130, 255);
                case FilerFileCategory::Vector:      return Color(0, 150, 167, 255);
                case FilerFileCategory::Audio:       return Color(156, 89, 182, 255);
                case FilerFileCategory::Video:       return Color(230, 106, 86, 255);
                case FilerFileCategory::Document:    return Color(66, 133, 244, 255);
                case FilerFileCategory::Text:        return Color(120, 144, 156, 255);
                case FilerFileCategory::Spreadsheet: return Color(46, 125, 50, 255);
                case FilerFileCategory::Archive:     return Color(141, 110, 99, 255);
                case FilerFileCategory::Executable:  return Color(84, 110, 122, 255);
                default:                             return Color(158, 158, 158, 255);
            }
        }

        std::string FormatSize(uint64_t bytes) {
            char buf[32];
            if (bytes < 1024) {
                snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
            } else if (bytes < 1024ull * 1024) {
                snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
            } else if (bytes < 1024ull * 1024 * 1024) {
                snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024));
            } else {
                snprintf(buf, sizeof(buf), "%.2f GB", bytes / (1024.0 * 1024 * 1024));
            }
            return buf;
        }

        std::string FormatTime(std::time_t t) {
            if (t == 0) return "";
            char buf[32];
            std::tm tmv{};
#ifdef _WIN32
            localtime_s(&tmv, &t);
#else
            localtime_r(&t, &tmv);
#endif
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tmv);
            return buf;
        }

        std::time_t ParseIso8601(const std::string& s) {
            int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
            if (sscanf(s.c_str(), "%d-%d-%d%*c%d:%d:%d", &y, &mo, &d, &h, &mi, &se) < 3)
                return 0;
            std::tm tmv{};
            tmv.tm_year = y - 1900; tmv.tm_mon = mo - 1; tmv.tm_mday = d;
            tmv.tm_hour = h; tmv.tm_min = mi; tmv.tm_sec = se;
            tmv.tm_isdst = -1;
            return std::mktime(&tmv);
        }

        std::string LowerExtension(const std::string& name) {
            size_t dot = name.find_last_of('.');
            if (dot == std::string::npos || dot == 0 || dot + 1 >= name.size())
                return "";
            std::string ext = name.substr(dot + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            return ext;
        }

        bool IsBrowsableArchiveExt(const std::string& ext) {
            static const char* exts[] = {"zip", "7z", "rar", "tar", "gz", "tgz",
                                         "bz2", "xz", "zst", "jar"};
            for (const char* e : exts) if (ext == e) return true;
            return false;
        }

        const char* SortFieldLabel(FilerSortField f) {
            switch (f) {
                case FilerSortField::Name:         return "Name";
                case FilerSortField::Size:         return "Size";
                case FilerSortField::Type:         return "Type";
                case FilerSortField::ModifiedDate: return "Modified";
                case FilerSortField::CreatedDate:  return "Created";
            }
            return "";
        }

        const char* ViewTypeLabel(FilerViewType v) {
            switch (v) {
                case FilerViewType::Details:             return "Details";
                case FilerViewType::List:                return "List";
                case FilerViewType::ThumbnailsSmall:     return "Thumbnails, small";
                case FilerViewType::ThumbnailsMedium:    return "Thumbnails, medium";
                case FilerViewType::ThumbnailsBig:       return "Thumbnails, big";
                case FilerViewType::ThumbnailsMaximized: return "Thumbnails, maximized";
                case FilerViewType::BarSize:             return "Bar size";
                case FilerViewType::TreeMap:             return "Treemap";
                case FilerViewType::GourceTree:          return "Force-directed tree";
                case FilerViewType::View3D:              return "3D";
            }
            return "";
        }
    }

    // ===== CONSTRUCTION =====
    UltraCanvasFilerWidget::UltraCanvasFilerWidget(const std::string& identifier,
                                                   float x, float y, float w, float h)
            : UltraCanvasContainer(identifier, x, y, w, h) {
        SetMouseCursor(UCMouseCursor::Default);
        SetBackgroundColor(style.backgroundColor);
        // The filer paints its own content; hide the container's child scrollbars.
        ContainerStyle cs = GetContainerStyle();
        cs.autoShowScrollbars = false;
        cs.forceShowVerticalScrollbar = false;
        cs.forceShowHorizontalScrollbar = false;
        SetContainerStyle(cs);

        newDocumentTypes = {
            {"Text",        "txt", ""},
            {"Doc",         "odt", ""},
            {"Spreadsheet", "ods", ""},
            {"Bitmap",      "png", ""},
            {"Vector",      "svg", ""},
            {"Audio",       "wav", ""},
            {"Video",       "mp4", ""},
        };
    }

    UltraCanvasFilerWidget::~UltraCanvasFilerWidget() = default;

    // ===== FOLDER =====
    void UltraCanvasFilerWidget::SetPath(const std::string& folderPath) {
        currentPath = folderPath;
        scrollOffsetX = scrollOffsetY = 0;
        CancelRename();
        ClearSelection();
        ScanFolder();
        if (onPathChanged) onPathChanged(currentPath);
    }

    void UltraCanvasFilerWidget::Refresh() {
        CancelRename();
        ScanFolder();
    }

    void UltraCanvasFilerWidget::ApplyEntryTypeInfo(FilerEntry& e) const {
        if (e.isDirectory) {
            e.category = FilerFileCategory::Folder;
            e.typeName = "Folder";
            return;
        }
        const auto& m = ExtensionTypeMap();
        auto it = m.find(e.extension);
        if (it != m.end()) {
            e.category = it->second.category;
            e.typeName = std::string(it->second.label) + " "
                         + CategoryNoun(it->second.category);
        } else {
            e.category = FilerFileCategory::Other;
            std::string upper = e.extension;
            std::transform(upper.begin(), upper.end(), upper.begin(),
                           [](unsigned char c) { return std::toupper(c); });
            e.typeName = upper.empty() ? "File" : upper + " File";
        }
        if (e.category == FilerFileCategory::Archive) e.isArchive = true;
    }

    void UltraCanvasFilerWidget::ScanFolder() {
        entries.clear();
        effectiveSizesValid = false;
        hoveredIndex = -1;
        lastClickedIndex = -1;

        std::error_code ec;
        bool isRealDir = !currentPath.empty() && fs::is_directory(currentPath, ec);

        if (isRealDir) {
            for (fs::directory_iterator it(currentPath, ec), end; it != end;
                 it.increment(ec)) {
                if (ec) break;
                FilerEntry e;
                e.name = it->path().filename().string();
                e.path = it->path().string();
                e.isDirectory = it->is_directory(ec);
                e.isSymlink = it->is_symlink(ec);
                e.isHidden = !e.name.empty() && e.name[0] == '.';
                if (!e.isDirectory) {
                    std::error_code sec;
                    e.size = it->file_size(sec);
                    if (sec) e.size = 0;
                }
                e.extension = e.isDirectory ? "" : LowerExtension(e.name);

                struct stat st{};
                if (::stat(e.path.c_str(), &st) == 0) {
                    e.modifiedTime = st.st_mtime;
                    e.createdTime = st.st_ctime;
                    e.isReadOnly = (st.st_mode & S_IWUSR) == 0;
                }
                ApplyEntryTypeInfo(e);
                if (e.isHidden && !showHiddenFiles) continue;
                entries.push_back(std::move(e));
            }
        }
#ifdef ULTRACANVAS_HAS_VIRTUALFS
        else if (!currentPath.empty()) {
            // Not a real directory: let VirtualFS list it (an archive interior —
            // "/path/archive.zip" or a path inside one).
            for (const VirtualFS::VirtualFSEntry& v
                 : VirtualFS::VirtualFS_ListDirectory(currentPath)) {
                FilerEntry e;
                e.name = v.name;
                e.path = v.path;
                e.isDirectory = v.IsDirectory();
                e.isSymlink = v.IsSymlink();
                e.isHidden = v.isHidden;
                e.isReadOnly = v.isReadOnly;
                e.isArchive = v.isArchive;
                e.size = v.size;
                e.compressedSize = v.compressedSize;
                e.modifiedTime = ParseIso8601(v.modifiedTime);
                e.createdTime = ParseIso8601(v.createdTime);
                e.extension = e.isDirectory ? "" : LowerExtension(e.name);
                ApplyEntryTypeInfo(e);
                if (e.isHidden && !showHiddenFiles) continue;
                entries.push_back(std::move(e));
            }
        }
#endif

        for (FilerEntry& e : entries) {
            e.effectiveSize = e.size;

            std::string attr;
            if (e.isDirectory) attr += 'D';
            if (e.isSymlink)   attr += 'L';
            if (e.isReadOnly)  attr += 'R';
            if (e.isHidden)    attr += 'H';
            if (e.isArchive)   attr += 'A';
            e.attributes = attr;

            if (e.compressedSize > 0 && e.size > 0 && e.compressedSize <= e.size) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%.0f%% compressed",
                         100.0 * (1.0 - double(e.compressedSize) / double(e.size)));
                e.info = buf;
            }
            if (infoProvider) {
                std::string s = infoProvider(e);
                if (!s.empty()) e.info = s;
            }
        }

        SortEntries();
        InvalidateFilerLayout();
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::SortEntries() {
        const FilerSortField field = sortField;
        const bool asc = sortAscending;
        std::stable_sort(entries.begin(), entries.end(),
                         [field, asc](const FilerEntry& a, const FilerEntry& b) {
            // Folders always list before files, regardless of direction.
            if (a.isDirectory != b.isDirectory) return a.isDirectory;
            int cmp = 0;
            switch (field) {
                case FilerSortField::Name:
                    cmp = CompareNoCase(a.name, b.name);
                    break;
                case FilerSortField::Size:
                    cmp = a.size < b.size ? -1 : (a.size > b.size ? 1 : 0);
                    break;
                case FilerSortField::Type:
                    cmp = CompareNoCase(a.typeName, b.typeName);
                    break;
                case FilerSortField::ModifiedDate:
                    cmp = a.modifiedTime < b.modifiedTime ? -1
                        : (a.modifiedTime > b.modifiedTime ? 1 : 0);
                    break;
                case FilerSortField::CreatedDate:
                    cmp = a.createdTime < b.createdTime ? -1
                        : (a.createdTime > b.createdTime ? 1 : 0);
                    break;
            }
            if (cmp == 0) cmp = CompareNoCase(a.name, b.name);
            return asc ? cmp < 0 : cmp > 0;
        });
    }

    void UltraCanvasFilerWidget::EnsureEffectiveSizes() {
        if (effectiveSizesValid) return;
        effectiveSizesValid = true;
        for (FilerEntry& e : entries) {
            e.effectiveSize = e.size;
            if (!e.isDirectory) continue;
            std::error_code ec;
            if (!fs::is_directory(e.path, ec)) continue;
            uint64_t total = 0, count = 0;
            for (fs::recursive_directory_iterator it(
                     e.path, fs::directory_options::skip_permission_denied, ec), end;
                 it != end && count < kDirSizeEntryCap; it.increment(ec)) {
                if (ec) break;
                std::error_code fec;
                if (it->is_regular_file(fec)) total += it->file_size(fec);
                ++count;
            }
            e.effectiveSize = total;
        }
    }

    // ===== VIEW SETTINGS =====
    void UltraCanvasFilerWidget::SetViewType(FilerViewType type) {
        if (viewType == type) return;
        viewType = type;
        scrollOffsetX = scrollOffsetY = 0;
        CancelRename();
        InvalidateFilerLayout();
        RequestRedraw();
        if (onViewTypeChanged) onViewTypeChanged(viewType);
    }

    void UltraCanvasFilerWidget::SetSort(FilerSortField field, bool ascending) {
        if (sortField == field && sortAscending == ascending) return;
        sortField = field;
        sortAscending = ascending;
        SortEntries();
        InvalidateFilerLayout();
        RequestRedraw();
        if (onSortChanged) onSortChanged(sortField, sortAscending);
    }

    void UltraCanvasFilerWidget::SetShowHiddenFiles(bool show) {
        if (showHiddenFiles == show) return;
        showHiddenFiles = show;
        Refresh();
    }

    void UltraCanvasFilerWidget::SetHoverIconMenuEnabled(bool enabled) {
        hoverIconMenu = enabled;
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::SetStyle(const FilerStyle& s) {
        style = s;
        SetBackgroundColor(style.backgroundColor);
        InvalidateFilerLayout();
        RequestRedraw();
    }

    // ===== SELECTION =====
    std::vector<FilerEntry> UltraCanvasFilerWidget::GetSelectedEntries() const {
        std::vector<FilerEntry> out;
        for (size_t idx : selection)
            if (idx < entries.size()) out.push_back(entries[idx]);
        return out;
    }

    void UltraCanvasFilerWidget::ClearSelection() {
        if (selection.empty()) return;
        selection.clear();
        FireSelectionChanged();
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::SelectAll() {
        selection.clear();
        for (size_t i = 0; i < entries.size(); ++i) selection.push_back(i);
        FireSelectionChanged();
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::FireSelectionChanged() {
        if (onSelectionChanged) onSelectionChanged(GetSelectedEntries());
    }

    std::vector<size_t> UltraCanvasFilerWidget::SelectionOrItem(int index) const {
        if (!selection.empty()) return selection;
        if (index >= 0 && index < (int)entries.size())
            return {static_cast<size_t>(index)};
        return {};
    }

    std::vector<FilerEntry> UltraCanvasFilerWidget::SelectionOrAll() const {
        std::vector<FilerEntry> out = GetSelectedEntries();
        if (out.empty()) out = entries;
        return out;
    }

    // ===== CLIPBOARD / FILE OPERATIONS =====
    bool UltraCanvasFilerWidget::ClipboardHasContent() {
        return !clipboardPaths.empty();
    }

    void UltraCanvasFilerWidget::SelectionToClipboard(bool cut) {
        clipboardPaths.clear();
        for (const FilerEntry& e : GetSelectedEntries()) clipboardPaths.push_back(e.path);
        clipboardCut = cut && !clipboardPaths.empty();
    }

    void UltraCanvasFilerWidget::CopySelection() { SelectionToClipboard(false); }
    void UltraCanvasFilerWidget::CutSelection()  { SelectionToClipboard(true); }

    std::string UltraCanvasFilerWidget::UniqueChildPath(const std::string& baseName) const {
        fs::path base(baseName);
        std::string stem = base.stem().string();
        std::string ext = base.extension().string();   // includes the dot
        fs::path dir(currentPath);
        fs::path candidate = dir / baseName;
        std::error_code ec;
        int n = 2;
        while (fs::exists(candidate, ec)) {
            candidate = dir / (stem + " (" + std::to_string(n++) + ")" + ext);
        }
        return candidate.string();
    }

    void UltraCanvasFilerWidget::Paste() {
        if (clipboardPaths.empty()) return;
        std::error_code ec;
        if (!fs::is_directory(currentPath, ec)) {
            ReportError("Paste target is not a writable folder: " + currentPath);
            return;
        }
        for (const std::string& src : clipboardPaths) {
            fs::path from(src);
            if (!fs::exists(from, ec)) continue;
            std::string dest = UniqueChildPath(from.filename().string());
            if (clipboardCut) {
                fs::rename(from, dest, ec);
                if (ec) {   // cross-device move: copy + delete
                    ec.clear();
                    fs::copy(from, dest, fs::copy_options::recursive, ec);
                    if (!ec) fs::remove_all(from, ec);
                }
            } else {
                fs::copy(from, dest, fs::copy_options::recursive, ec);
            }
            if (ec) ReportError("Paste failed for " + src + ": " + ec.message());
        }
        if (clipboardCut) { clipboardPaths.clear(); clipboardCut = false; }
        Refresh();
    }

    void UltraCanvasFilerWidget::DeleteSelection() {
        std::vector<FilerEntry> victims = GetSelectedEntries();
        if (victims.empty()) return;
        if (confirmDelete && !confirmDelete(victims)) return;
        std::error_code ec;
        for (const FilerEntry& e : victims) {
            fs::remove_all(e.path, ec);
            if (ec) ReportError("Delete failed for " + e.path + ": " + ec.message());
        }
        ClearSelection();
        Refresh();
    }

    void UltraCanvasFilerWidget::DuplicateSelection() {
        std::vector<FilerEntry> sources = GetSelectedEntries();
        if (sources.empty()) return;
        std::error_code ec;
        for (const FilerEntry& e : sources) {
            std::string dest = UniqueChildPath(e.name);
            fs::copy(e.path, dest, fs::copy_options::recursive, ec);
            if (ec) ReportError("Duplicate failed for " + e.path + ": " + ec.message());
        }
        Refresh();
    }

    void UltraCanvasFilerWidget::StartRename(size_t entryIndex) {
        if (entryIndex >= entries.size()) return;
        renamingIndex = static_cast<int>(entryIndex);
        renameBuffer = entries[entryIndex].name;
        EnsureVisible(entryIndex);
        SetFocus(true);
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::CommitRename() {
        if (renamingIndex < 0 || renamingIndex >= (int)entries.size()) {
            renamingIndex = -1;
            return;
        }
        const FilerEntry& e = entries[renamingIndex];
        std::string newName = renameBuffer;
        renamingIndex = -1;
        if (newName.empty() || newName == e.name ||
            newName.find('/') != std::string::npos) {
            RequestRedraw();
            return;
        }
        std::error_code ec;
        fs::path target = fs::path(currentPath) / newName;
        if (fs::exists(target, ec)) {
            ReportError("Rename failed: \"" + newName + "\" already exists");
            RequestRedraw();
            return;
        }
        fs::rename(e.path, target, ec);
        if (ec) ReportError("Rename failed for " + e.path + ": " + ec.message());
        Refresh();
    }

    void UltraCanvasFilerWidget::CancelRename() {
        if (renamingIndex == -1) return;
        renamingIndex = -1;
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::CompressSelection() {
#ifdef ULTRACANVAS_HAS_VIRTUALFS
        std::vector<FilerEntry> targets = SelectionOrAll();
        if (targets.empty()) return;
        std::vector<std::string> paths;
        for (const FilerEntry& e : targets) paths.push_back(e.path);
        std::string base = (targets.size() == 1)
                ? fs::path(targets[0].name).stem().string()
                : fs::path(currentPath).filename().string();
        if (base.empty()) base = "archive";
        std::string dest = UniqueChildPath(base + ".zip");
        if (!UCVFSBridge::CreateArchive(dest, paths)) {
            ReportError("Compression failed for " + dest);
            return;
        }
        Refresh();
#else
        ReportError("Compress requires the VirtualFS module");
#endif
    }

    void UltraCanvasFilerWidget::ExtractSelection() {
#ifdef ULTRACANVAS_HAS_VIRTUALFS
        bool any = false;
        for (const FilerEntry& e : GetSelectedEntries()) {
            if (!e.isArchive) continue;
            any = true;
            std::string destDir = UniqueChildPath(fs::path(e.name).stem().string());
            std::error_code ec;
            fs::create_directories(destDir, ec);
            if (!UCVFSBridge::ExtractArchive(e.path, destDir)) {
                ReportError("Extraction failed for " + e.path);
            }
        }
        if (any) Refresh();
#else
        ReportError("Extract requires the VirtualFS module");
#endif
    }

    void UltraCanvasFilerWidget::SetNewDocumentTypes(
            const std::vector<FilerNewDocumentType>& types) {
        newDocumentTypes = types;
    }

    void UltraCanvasFilerWidget::CreateNewDocument(const FilerNewDocumentType& type) {
        if (onNewDocument && onNewDocument(type, currentPath)) {
            Refresh();
            return;
        }
        std::error_code ec;
        if (!fs::is_directory(currentPath, ec)) {
            ReportError("Cannot create a document here: " + currentPath);
            return;
        }
        std::string dest = UniqueChildPath("New " + type.label + "." + type.extension);
        if (!type.templatePath.empty() && fs::exists(type.templatePath, ec)) {
            fs::copy_file(type.templatePath, dest, ec);
            if (ec) { ReportError("New document failed: " + ec.message()); return; }
        } else {
            std::ofstream out(dest, std::ios::binary);
            if (!out) { ReportError("New document failed: " + dest); return; }
        }
        Refresh();
        // Put the fresh file straight into rename mode.
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].path == dest) { StartRename(i); break; }
        }
    }

    void UltraCanvasFilerWidget::AddOpenWithApp(const FilerOpenWithApp& app) {
        openWithApps.push_back(app);
    }

    void UltraCanvasFilerWidget::ClearOpenWithApps() {
        openWithApps.clear();
    }

    void UltraCanvasFilerWidget::ReportError(const std::string& message) {
        if (onError) onError(message);
        else std::cerr << "UltraCanvasFilerWidget: " << message << std::endl;
    }

    // ===== LAYOUT =====
    Rect2Di UltraCanvasFilerWidget::ContentBounds() const {
        auto b = GetLocalBounds();
        int pad = style.outerPadding;
        return Rect2Di(static_cast<int>(b.x) + pad, static_cast<int>(b.y) + pad,
                       std::max(0, static_cast<int>(b.width) - 2 * pad),
                       std::max(0, static_cast<int>(b.height) - 2 * pad));
    }

    int UltraCanvasFilerWidget::ThumbnailEdge() const {
        switch (viewType) {
            case FilerViewType::ThumbnailsSmall:     return style.thumbnailSmall;
            case FilerViewType::ThumbnailsMedium:    return style.thumbnailMedium;
            case FilerViewType::ThumbnailsBig:       return style.thumbnailBig;
            case FilerViewType::ThumbnailsMaximized: return style.thumbnailMaximized;
            default:                                 return style.thumbnailMedium;
        }
    }

    void UltraCanvasFilerWidget::EnsureLayout() {
        auto b = GetLocalBounds();
        int w = static_cast<int>(b.width), h = static_cast<int>(b.height);
        if (!layoutValid || w != lastAreaW || h != lastAreaH) {
            lastAreaW = w;
            lastAreaH = h;
            RecomputeLayout();
            layoutValid = true;
        }
    }

    void UltraCanvasFilerWidget::RecomputeLayout() {
        items.clear();
        detailsColumns.clear();
        contentWidth = contentHeight = 0;
        Rect2Di area = ContentBounds();
        if (area.width <= 0 || area.height <= 0) return;

        switch (viewType) {
            case FilerViewType::Details:   LayoutDetails(area); break;
            case FilerViewType::List:      LayoutList(area); break;
            case FilerViewType::ThumbnailsSmall:
            case FilerViewType::ThumbnailsMedium:
            case FilerViewType::ThumbnailsBig:
            case FilerViewType::ThumbnailsMaximized:
                LayoutThumbnails(area);
                break;
            case FilerViewType::BarSize:   LayoutBarSize(area); break;
            case FilerViewType::TreeMap:   LayoutTreeMap(area); break;
            case FilerViewType::GourceTree:
            case FilerViewType::View3D:
                contentHeight = area.height;   // placeholder page, no scrolling
                break;
        }
        ClampScroll();
    }

    void UltraCanvasFilerWidget::LayoutDetails(const Rect2Di& area) {
        // Fixed columns; the name column absorbs the remaining width.
        struct Fixed { FilerSortField field; const char* title; int width;
                       bool right; bool sortable; };
        static const Fixed fixedCols[] = {
            {FilerSortField::Size,         "Size",     90,  true,  true},
            {FilerSortField::Type,         "Type",     130, false, true},
            {FilerSortField::ModifiedDate, "Modified", 150, false, true},
            {FilerSortField::CreatedDate,  "Created",  150, false, true},
            {FilerSortField::Name,         "Attr",     55,  false, false},
            {FilerSortField::Name,         "Info",     120, false, false},
        };
        int fixedTotal = 0;
        for (const Fixed& f : fixedCols) fixedTotal += f.width;
        int scrollbarGutter = 10;
        int nameWidth = std::max(180, area.width - fixedTotal - scrollbarGutter);

        int x = area.x;
        DetailsColumn name;
        name.field = FilerSortField::Name;
        name.title = "Name";
        name.x = x; name.width = nameWidth;
        detailsColumns.push_back(name);
        x += nameWidth;
        for (const Fixed& f : fixedCols) {
            DetailsColumn c;
            c.field = f.field; c.title = f.title;
            c.x = x; c.width = f.width;
            c.rightAligned = f.right;
            c.sortable = f.sortable;
            detailsColumns.push_back(c);
            x += f.width;
        }

        int rowH = style.detailsRowHeight;
        int y = area.y + detailsHeaderHeight;
        for (size_t i = 0; i < entries.size(); ++i) {
            ItemLayout it;
            it.entryIndex = i;
            it.rect = Rect2Di(area.x, y, x - area.x, rowH);
            it.imageRect = Rect2Di(area.x + 4, y + 3, rowH - 6, rowH - 6);
            items.push_back(it);
            y += rowH;
        }
        contentWidth = area.width;
        contentHeight = y + style.outerPadding;
    }

    void UltraCanvasFilerWidget::LayoutList(const Rect2Di& area) {
        int rowH = style.listRowHeight;
        int colW = style.listColumnWidth;
        int gap = 12;
        int rowsPerColumn = std::max(1, area.height / rowH);
        for (size_t i = 0; i < entries.size(); ++i) {
            int col = static_cast<int>(i) / rowsPerColumn;
            int row = static_cast<int>(i) % rowsPerColumn;
            ItemLayout it;
            it.entryIndex = i;
            it.rect = Rect2Di(area.x + col * (colW + gap), area.y + row * rowH,
                              colW, rowH);
            it.imageRect = Rect2Di(it.rect.x + 2, it.rect.y + 2,
                                   rowH - 4, rowH - 4);
            items.push_back(it);
        }
        int cols = (static_cast<int>(entries.size()) + rowsPerColumn - 1)
                   / std::max(1, rowsPerColumn);
        contentWidth = cols * (colW + gap) - gap + 2 * style.outerPadding;
        contentHeight = area.height;
    }

    void UltraCanvasFilerWidget::LayoutThumbnails(const Rect2Di& area) {
        int edge = ThumbnailEdge();
        int gap = style.tileGap;
        int capH = style.captionHeight;
        int tileW = edge;
        int tileH = edge + capH;
        int scrollbarGutter = 10;
        int availW = area.width - scrollbarGutter;
        int cols = std::max(1, (availW + gap) / (tileW + gap));
        for (size_t i = 0; i < entries.size(); ++i) {
            int col = static_cast<int>(i) % cols;
            int row = static_cast<int>(i) / cols;
            ItemLayout it;
            it.entryIndex = i;
            it.rect = Rect2Di(area.x + col * (tileW + gap),
                              area.y + row * (tileH + gap), tileW, tileH);
            it.imageRect = Rect2Di(it.rect.x, it.rect.y, tileW, edge);
            items.push_back(it);
        }
        int rows = (static_cast<int>(entries.size()) + cols - 1) / cols;
        contentWidth = area.width;
        contentHeight = area.y + rows * (tileH + gap) - gap + style.outerPadding;
        if (rows == 0) contentHeight = area.height;
    }

    void UltraCanvasFilerWidget::LayoutBarSize(const Rect2Di& area) {
        EnsureEffectiveSizes();
        int rowH = style.detailsRowHeight;
        int y = area.y;
        for (size_t i = 0; i < entries.size(); ++i) {
            ItemLayout it;
            it.entryIndex = i;
            it.rect = Rect2Di(area.x, y, area.width - 10, rowH);
            it.imageRect = Rect2Di(area.x + 4, y + 3, rowH - 6, rowH - 6);
            items.push_back(it);
            y += rowH;
        }
        contentWidth = area.width;
        contentHeight = y + style.outerPadding;
    }

    // Squarified treemap: lay the weighted entries into `area` so every cell's
    // aspect ratio stays close to 1 (Bruls / Huizing / van Wijk).
    void UltraCanvasFilerWidget::LayoutTreeMap(const Rect2Di& area) {
        EnsureEffectiveSizes();
        if (entries.empty()) { contentHeight = area.height; return; }

        std::vector<std::pair<size_t, double>> weighted;
        weighted.reserve(entries.size());
        for (size_t i = 0; i < entries.size(); ++i) {
            weighted.emplace_back(i, double(std::max<uint64_t>(1, entries[i].effectiveSize)));
        }
        std::stable_sort(weighted.begin(), weighted.end(),
                         [](const auto& a, const auto& b) { return a.second > b.second; });

        // Normalize weights to the pixel area.
        double totalWeight = 0;
        for (auto& wp : weighted) totalWeight += wp.second;
        double areaPixels = double(area.width) * double(area.height);
        for (auto& wp : weighted) wp.second *= areaPixels / totalWeight;

        SquarifyRange(weighted, 0, weighted.size(), area);
        contentWidth = area.width;
        contentHeight = area.height;   // treemap always fits the viewport
    }

    void UltraCanvasFilerWidget::SquarifyRange(
            std::vector<std::pair<size_t, double>>& weighted,
            size_t first, size_t last, Rect2Di rect) {
        while (first < last && rect.width > 0 && rect.height > 0) {
            bool horizontal = rect.width >= rect.height;   // row along the short side
            double side = horizontal ? rect.height : rect.width;
            side = std::max(1.0, side);

            // Grow the current row while the worst aspect ratio improves.
            size_t rowEnd = first;
            double rowSum = 0, rowMax = 0, rowMin = 1e300, worst = 1e300;
            while (rowEnd < last) {
                double w = weighted[rowEnd].second;
                double ns = rowSum + w;
                double nMax = std::max(rowMax, w);
                double nMin = std::min(rowMin, w);
                double nWorst = std::max(side * side * nMax / (ns * ns),
                                         ns * ns / (side * side * nMin));
                if (rowEnd > first && nWorst > worst) break;
                rowSum = ns; rowMax = nMax; rowMin = nMin; worst = nWorst;
                ++rowEnd;
            }

            double thickness = rowSum / side;   // row depth in pixels
            double offset = 0;
            for (size_t k = first; k < rowEnd; ++k) {
                double len = weighted[k].second / std::max(1e-9, thickness);
                ItemLayout it;
                it.entryIndex = weighted[k].first;
                if (horizontal) {
                    it.rect = Rect2Di(rect.x, rect.y + int(std::floor(offset)),
                                      std::max(1, int(std::lround(thickness))),
                                      std::max(1, int(std::lround(len))));
                } else {
                    it.rect = Rect2Di(rect.x + int(std::floor(offset)), rect.y,
                                      std::max(1, int(std::lround(len))),
                                      std::max(1, int(std::lround(thickness))));
                }
                it.imageRect = it.rect;
                items.push_back(it);
                offset += len;
            }

            int t = std::max(1, int(std::lround(thickness)));
            if (horizontal) {
                rect.x += t;
                rect.width = std::max(0, rect.width - t);
            } else {
                rect.y += t;
                rect.height = std::max(0, rect.height - t);
            }
            first = rowEnd;
        }
    }

    // ===== SCROLLING =====
    int UltraCanvasFilerWidget::MaxScrollY() const {
        auto b = GetLocalBounds();
        return std::max(0, contentHeight - static_cast<int>(b.height));
    }

    int UltraCanvasFilerWidget::MaxScrollX() const {
        auto b = GetLocalBounds();
        return std::max(0, contentWidth - static_cast<int>(b.width));
    }

    void UltraCanvasFilerWidget::ClampScroll() {
        scrollOffsetY = clampi(scrollOffsetY, 0, MaxScrollY());
        scrollOffsetX = clampi(scrollOffsetX, 0, MaxScrollX());
    }

    UltraCanvasFilerWidget::ScrollbarGeom UltraCanvasFilerWidget::ScrollbarGeometry() const {
        ScrollbarGeom g;
        auto b = GetLocalBounds();
        constexpr int kBarThickness = 6;
        constexpr int kMinThumb = 30;
        if (IsHorizontal()) {
            int maxX = MaxScrollX();
            if (maxX <= 0) return g;
            int y = static_cast<int>(b.y + b.height) - kBarThickness - 2;
            double frac = static_cast<double>(b.width) / std::max(1, contentWidth);
            int thumbW = std::max(kMinThumb, static_cast<int>(b.width * frac));
            int travel = std::max(0, static_cast<int>(b.width) - thumbW);
            int tx = static_cast<int>(b.x) + (maxX > 0 ? travel * scrollOffsetX / maxX : 0);
            g.active = true; g.horizontal = true; g.travel = travel; g.maxScroll = maxX;
            g.track = Rect2Di(static_cast<int>(b.x), y, static_cast<int>(b.width), kBarThickness);
            g.thumb = Rect2Di(tx, y, thumbW, kBarThickness);
        } else {
            int maxY = MaxScrollY();
            if (maxY <= 0) return g;
            int x = static_cast<int>(b.x + b.width) - kBarThickness - 2;
            double frac = static_cast<double>(b.height) / std::max(1, contentHeight);
            int thumbH = std::max(kMinThumb, static_cast<int>(b.height * frac));
            int travel = std::max(0, static_cast<int>(b.height) - thumbH);
            int ty = static_cast<int>(b.y) + (maxY > 0 ? travel * scrollOffsetY / maxY : 0);
            g.active = true; g.horizontal = false; g.travel = travel; g.maxScroll = maxY;
            g.track = Rect2Di(x, static_cast<int>(b.y), kBarThickness, static_cast<int>(b.height));
            g.thumb = Rect2Di(x, ty, kBarThickness, thumbH);
        }
        return g;
    }

    void UltraCanvasFilerWidget::ScrollThumbTo(int thumbLeadPx) {
        ScrollbarGeom g = ScrollbarGeometry();
        if (!g.active || g.travel <= 0) return;
        auto b = GetLocalBounds();
        if (g.horizontal) {
            int rel = clampi(thumbLeadPx - static_cast<int>(b.x), 0, g.travel);
            scrollOffsetX = rel * g.maxScroll / g.travel;
        } else {
            int rel = clampi(thumbLeadPx - static_cast<int>(b.y), 0, g.travel);
            scrollOffsetY = rel * g.maxScroll / g.travel;
        }
        ClampScroll();
    }

    void UltraCanvasFilerWidget::EnsureVisible(size_t entryIndex) {
        EnsureLayout();
        for (const ItemLayout& it : items) {
            if (it.entryIndex != entryIndex) continue;
            auto b = GetLocalBounds();
            if (IsHorizontal()) {
                if (it.rect.x - scrollOffsetX < 0)
                    scrollOffsetX = it.rect.x;
                else if (it.rect.x + it.rect.width - scrollOffsetX > (int)b.width)
                    scrollOffsetX = it.rect.x + it.rect.width - (int)b.width;
            } else {
                int top = (viewType == FilerViewType::Details) ? detailsHeaderHeight : 0;
                if (it.rect.y - scrollOffsetY < top)
                    scrollOffsetY = it.rect.y - top;
                else if (it.rect.y + it.rect.height - scrollOffsetY > (int)b.height)
                    scrollOffsetY = it.rect.y + it.rect.height - (int)b.height;
            }
            ClampScroll();
            break;
        }
    }

    // ===== RENDER =====
    void UltraCanvasFilerWidget::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        UltraCanvasUIElement::Render(ctx, dirtyRect);
        EnsureLayout();

        auto lb = GetLocalBounds();
        Rect2Di bounds(static_cast<int>(lb.x), static_cast<int>(lb.y),
                       static_cast<int>(lb.width), static_cast<int>(lb.height));

        ctx->PushState();
        ctx->ClipRect(Rect2Dd(bounds));
        ctx->SetFillPaint(style.backgroundColor);
        ctx->FillRectangle(Rect2Dd(bounds));

        iconMenuHits.clear();

        if (viewType == FilerViewType::GourceTree) {
            DrawPlaceholderView(ctx, bounds,
                                "Force-directed tree view (Gource) — to be implemented");
            ctx->PopState();
            return;
        }
        if (viewType == FilerViewType::View3D) {
            DrawPlaceholderView(ctx, bounds, "3D view — to be implemented");
            ctx->PopState();
            return;
        }

        if (entries.empty()) {
            ctx->SetTextPaint(style.secondaryTextColor);
            FontStyle fsty;
            fsty.fontFamily = style.fontFamily;
            fsty.fontSize = style.fontSize;
            ctx->SetFontStyle(fsty);
            ctx->DrawTextInRect(currentPath.empty() ? "(no folder)" : "(empty folder)",
                                Rect2Dd(bounds));
            ctx->PopState();
            return;
        }

        // Scrolled content.
        ctx->PushState();
        ctx->Translate(-scrollOffsetX, -scrollOffsetY);
        for (const ItemLayout& item : items) {
            int top = item.rect.y - scrollOffsetY;
            int left = item.rect.x - scrollOffsetX;
            if (top + item.rect.height < bounds.y || top > bounds.y + bounds.height) continue;
            if (left + item.rect.width < bounds.x || left > bounds.x + bounds.width) continue;
            bool hov = (static_cast<int>(item.entryIndex) == hoveredIndex);
            switch (viewType) {
                case FilerViewType::Details: DrawDetailsRow(ctx, item, hov); break;
                case FilerViewType::List:    DrawListItem(ctx, item, hov); break;
                case FilerViewType::BarSize: DrawBarSizeRow(ctx, item, hov); break;
                case FilerViewType::TreeMap: DrawTreeMapCell(ctx, item, hov); break;
                default:                     DrawThumbnailTile(ctx, item, hov); break;
            }
        }
        // Hover icon menu on top of its item, inside the scrolled space so it
        // tracks the item; hit rects are recorded in content space.
        if (hoverIconMenu && hoveredIndex >= 0 && renamingIndex < 0 &&
            viewType != FilerViewType::TreeMap) {
            for (const ItemLayout& item : items) {
                if (static_cast<int>(item.entryIndex) == hoveredIndex) {
                    DrawHoverIconMenu(ctx, item);
                    break;
                }
            }
        }
        if (renamingIndex >= 0) {
            for (const ItemLayout& item : items) {
                if (static_cast<int>(item.entryIndex) == renamingIndex) {
                    DrawRenameEditor(ctx, item);
                    break;
                }
            }
        }
        ctx->PopState();

        if (viewType == FilerViewType::Details) DrawDetailsHeader(ctx, bounds);
        DrawScrollbar(ctx);
        ctx->PopState();
    }

    void UltraCanvasFilerWidget::DrawPlaceholderView(IRenderContext* ctx,
                                                     const Rect2Di& bounds,
                                                     const std::string& message) {
        ctx->SetTextPaint(style.secondaryTextColor);
        FontStyle fsty;
        fsty.fontFamily = style.fontFamily;
        fsty.fontSize = style.fontSize + 2;
        ctx->SetFontStyle(fsty);
        ctx->DrawTextInRect(message, Rect2Dd(bounds));
    }

    std::string UltraCanvasFilerWidget::EllipsizeText(IRenderContext* ctx,
                                                      const std::string& text,
                                                      int maxWidth) const {
        if (maxWidth <= 0) return "";
        Size2Di ts = ctx->GetTextLineDimensions(text);
        if (ts.width <= maxWidth) return text;
        std::string s = text;
        while (s.size() > 1) {
            // Trim whole UTF-8 code points so multibyte names stay valid.
            size_t cut = s.size() - 1;
            while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
            s.erase(cut);
            ts = ctx->GetTextLineDimensions(s + "…");
            if (ts.width <= maxWidth) return s + "…";
        }
        return "…";
    }

    void UltraCanvasFilerWidget::DrawSelectionState(IRenderContext* ctx,
                                                    const ItemLayout& item,
                                                    bool hovered) {
        bool selected = std::find(selection.begin(), selection.end(),
                                  item.entryIndex) != selection.end();
        if (selected) {
            ctx->SetFillPaint(style.selectionColor);
            ctx->FillRectangle(Rect2Dd(item.rect));
        } else if (hovered) {
            ctx->SetFillPaint(style.hoverColor);
            ctx->FillRectangle(Rect2Dd(item.rect));
        }
    }

    void UltraCanvasFilerWidget::DrawEntryIcon(IRenderContext* ctx, const FilerEntry& e,
                                               const Rect2Di& rect) {
        if (rect.width <= 2 || rect.height <= 2) return;

        // Real image thumbnails (explicit thumbnail, else the bitmap itself).
        std::string thumb = e.thumbnailPath;
        if (thumb.empty() &&
            (e.category == FilerFileCategory::Image ||
             e.category == FilerFileCategory::Vector)) {
            thumb = e.path;
        }
        if (!thumb.empty()) {
            auto img = UCImage::Get(thumb);
            if (img && img->GetWidth() > 0 && img->GetHeight() > 0) {
                ctx->DrawImage(*img, Rect2Dd(rect), ImageFitMode::Contain);
                return;
            }
        }

        Color color = CategoryColor(e.category);
        if (e.isDirectory) {
            // Folder shape: a tab above the body.
            double tabW = rect.width * 0.45, tabH = std::max(2.0, rect.height * 0.18);
            ctx->SetFillPaint(color);
            ctx->FillRoundedRectangle(Rect2Dd(rect.x, rect.y, tabW, tabH * 2), 2);
            Color body(std::min(255, color.r + 20), std::min(255, color.g + 20),
                       std::min(255, color.b + 25), 255);
            ctx->SetFillPaint(body);
            ctx->FillRoundedRectangle(Rect2Dd(rect.x, rect.y + tabH,
                                              rect.width, rect.height - tabH), 2);
            return;
        }

        // Generic file glyph: a colored "sheet" with the extension on it.
        ctx->SetFillPaint(Color(252, 252, 253, 255));
        ctx->FillRoundedRectangle(Rect2Dd(rect), 2);
        ctx->SetStrokePaint(Color(0, 0, 0, 40));
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRoundedRectangle(Rect2Dd(rect), 2);
        double bandH = std::max(3.0, rect.height * 0.30);
        ctx->SetFillPaint(color);
        ctx->FillRoundedRectangle(Rect2Dd(rect.x, rect.y + rect.height - bandH,
                                          rect.width, bandH), 2);
        if (rect.height >= 26 && !e.extension.empty()) {
            std::string ext = e.extension.substr(0, 4);
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return std::toupper(c); });
            FontStyle fsty;
            fsty.fontFamily = style.fontFamily;
            fsty.fontSize = std::max(8.0, rect.height * 0.24);
            fsty.fontWeight = FontWeight::Bold;
            ctx->SetFontStyle(fsty);
            ctx->SetTextPaint(Color(60, 60, 66, 255));
            Size2Di ts = ctx->GetTextLineDimensions(ext);
            ctx->DrawText(ext, Point2Dd(rect.x + (rect.width - ts.width) / 2.0,
                                        rect.y + (rect.height - bandH - ts.height) / 2.0));
        }
    }

    void UltraCanvasFilerWidget::DrawDetailsHeader(IRenderContext* ctx,
                                                   const Rect2Di& bounds) {
        Rect2Di area = ContentBounds();
        Rect2Di header(bounds.x, bounds.y, bounds.width,
                       area.y - bounds.y + detailsHeaderHeight);
        ctx->SetFillPaint(style.headerBackground);
        ctx->FillRectangle(Rect2Dd(header));
        ctx->SetStrokePaint(style.gridLineColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine(Point2Dd(header.x, header.y + header.height),
                      Point2Dd(header.x + header.width, header.y + header.height));

        FontStyle fsty;
        fsty.fontFamily = style.fontFamily;
        fsty.fontSize = style.fontSize;
        fsty.fontWeight = FontWeight::Bold;
        ctx->SetFontStyle(fsty);
        for (const DetailsColumn& c : detailsColumns) {
            std::string title = c.title;
            if (c.sortable && c.field == sortField &&
                (c.title != "Attr" && c.title != "Info")) {
                title += sortAscending ? " ▲" : " ▼";
            }
            ctx->SetTextPaint(style.headerTextColor);
            Size2Di ts = ctx->GetTextLineDimensions(title);
            int tx = c.rightAligned ? c.x + c.width - ts.width - 8 : c.x + 6;
            int ty = area.y + (detailsHeaderHeight - ts.height) / 2;
            ctx->DrawText(title, Point2Dd(tx, ty));
        }
    }

    void UltraCanvasFilerWidget::DrawDetailsRow(IRenderContext* ctx,
                                                const ItemLayout& item, bool hovered) {
        const FilerEntry& e = entries[item.entryIndex];
        DrawSelectionState(ctx, item, hovered);
        DrawEntryIcon(ctx, e, item.imageRect);

        FontStyle fsty;
        fsty.fontFamily = style.fontFamily;
        fsty.fontSize = style.fontSize;
        ctx->SetFontStyle(fsty);

        int textInsetTop = 0;
        for (const DetailsColumn& c : detailsColumns) {
            std::string value;
            Color color = style.textColor;
            if (c.title == "Name") {
                value = e.name;
            } else if (c.title == "Size") {
                value = e.isDirectory ? "" : FormatSize(e.size);
                color = style.secondaryTextColor;
            } else if (c.title == "Type") {
                value = e.typeName;
                color = style.secondaryTextColor;
            } else if (c.title == "Modified") {
                value = FormatTime(e.modifiedTime);
                color = style.secondaryTextColor;
            } else if (c.title == "Created") {
                value = FormatTime(e.createdTime);
                color = style.secondaryTextColor;
            } else if (c.title == "Attr") {
                value = e.attributes;
                color = style.secondaryTextColor;
            } else if (c.title == "Info") {
                value = e.info;
                color = style.secondaryTextColor;
            }
            if (value.empty()) continue;

            int pad = 6;
            int textX = c.x + pad;
            int avail = c.width - 2 * pad;
            if (c.title == "Name") {
                textX = item.imageRect.x + item.imageRect.width + 6;
                avail = c.x + c.width - textX - pad;
            }
            std::string shown = EllipsizeText(ctx, value, avail);
            ctx->SetTextPaint(color);
            Size2Di ts = ctx->GetTextLineDimensions(shown);
            if (textInsetTop == 0) textInsetTop = (item.rect.height - ts.height) / 2;
            int tx = c.rightAligned ? c.x + c.width - ts.width - 8 : textX;
            ctx->DrawText(shown, Point2Dd(tx, item.rect.y + textInsetTop));
        }

        ctx->SetStrokePaint(style.gridLineColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine(Point2Dd(item.rect.x, item.rect.y + item.rect.height),
                      Point2Dd(item.rect.x + item.rect.width,
                               item.rect.y + item.rect.height));
    }

    void UltraCanvasFilerWidget::DrawListItem(IRenderContext* ctx,
                                              const ItemLayout& item, bool hovered) {
        const FilerEntry& e = entries[item.entryIndex];
        DrawSelectionState(ctx, item, hovered);
        DrawEntryIcon(ctx, e, item.imageRect);

        FontStyle fsty;
        fsty.fontFamily = style.fontFamily;
        fsty.fontSize = style.fontSize;
        ctx->SetFontStyle(fsty);
        ctx->SetTextPaint(style.textColor);
        int textX = item.imageRect.x + item.imageRect.width + 6;
        int avail = item.rect.x + item.rect.width - textX - 4;
        std::string shown = EllipsizeText(ctx, e.name, avail);
        Size2Di ts = ctx->GetTextLineDimensions(shown);
        ctx->DrawText(shown, Point2Dd(textX,
                item.rect.y + (item.rect.height - ts.height) / 2));
    }

    void UltraCanvasFilerWidget::DrawThumbnailTile(IRenderContext* ctx,
                                                   const ItemLayout& item, bool hovered) {
        const FilerEntry& e = entries[item.entryIndex];
        bool selected = std::find(selection.begin(), selection.end(),
                                  item.entryIndex) != selection.end();
        if (selected || hovered) {
            ctx->SetFillPaint(selected ? style.selectionColor : style.hoverColor);
            ctx->FillRoundedRectangle(Rect2Dd(item.rect), 5);
        }
        int inset = std::max(4, item.imageRect.width / 12);
        Rect2Di img(item.imageRect.x + inset, item.imageRect.y + inset,
                    item.imageRect.width - 2 * inset,
                    item.imageRect.height - 2 * inset);
        DrawEntryIcon(ctx, e, img);

        FontStyle fsty;
        fsty.fontFamily = style.fontFamily;
        fsty.fontSize = style.smallFontSize;
        ctx->SetFontStyle(fsty);
        ctx->SetTextPaint(style.textColor);
        std::string shown = EllipsizeText(ctx, e.name, item.rect.width - 8);
        Size2Di ts = ctx->GetTextLineDimensions(shown);
        int capTop = item.imageRect.y + item.imageRect.height;
        ctx->DrawText(shown, Point2Dd(
                item.rect.x + (item.rect.width - ts.width) / 2.0,
                capTop + (style.captionHeight - ts.height) / 2.0));

        if (selected) {
            ctx->SetStrokePaint(style.selectionBorderColor);
            ctx->SetStrokeWidth(2.0f);
            ctx->DrawRoundedRectangle(Rect2Dd(item.rect), 5);
        }
    }

    void UltraCanvasFilerWidget::DrawBarSizeRow(IRenderContext* ctx,
                                                const ItemLayout& item, bool hovered) {
        const FilerEntry& e = entries[item.entryIndex];
        DrawSelectionState(ctx, item, hovered);
        DrawEntryIcon(ctx, e, item.imageRect);

        uint64_t maxSize = 1;
        for (const FilerEntry& x : entries) maxSize = std::max(maxSize, x.effectiveSize);

        FontStyle fsty;
        fsty.fontFamily = style.fontFamily;
        fsty.fontSize = style.fontSize;
        ctx->SetFontStyle(fsty);

        int nameX = item.imageRect.x + item.imageRect.width + 6;
        int nameW = std::min(220, item.rect.width / 3);
        std::string shown = EllipsizeText(ctx, e.name, nameW - 8);
        ctx->SetTextPaint(style.textColor);
        Size2Di ts = ctx->GetTextLineDimensions(shown);
        int textY = item.rect.y + (item.rect.height - ts.height) / 2;
        ctx->DrawText(shown, Point2Dd(nameX, textY));

        // Size bar scaled against the folder's largest entry.
        std::string sizeText = FormatSize(e.effectiveSize);
        Size2Di sts = ctx->GetTextLineDimensions(sizeText);
        int barX = item.rect.x + nameW + item.imageRect.width + 12;
        int barMaxW = item.rect.x + item.rect.width - barX - sts.width - 14;
        if (barMaxW > 20) {
            int barH = std::max(6, item.rect.height - 12);
            int barY = item.rect.y + (item.rect.height - barH) / 2;
            ctx->SetFillPaint(style.barBackground);
            ctx->FillRoundedRectangle(Rect2Dd(barX, barY, barMaxW, barH), 3);
            double frac = double(e.effectiveSize) / double(maxSize);
            int w = std::max(e.effectiveSize > 0 ? 2 : 0,
                             int(std::lround(barMaxW * frac)));
            if (w > 0) {
                Color bar = e.isDirectory ? CategoryColor(FilerFileCategory::Folder)
                                          : style.barColor;
                ctx->SetFillPaint(bar);
                ctx->FillRoundedRectangle(Rect2Dd(barX, barY, w, barH), 3);
            }
            ctx->SetTextPaint(style.secondaryTextColor);
            ctx->DrawText(sizeText, Point2Dd(barX + barMaxW + 8, textY));
        }
    }

    void UltraCanvasFilerWidget::DrawTreeMapCell(IRenderContext* ctx,
                                                 const ItemLayout& item, bool hovered) {
        const FilerEntry& e = entries[item.entryIndex];
        bool selected = std::find(selection.begin(), selection.end(),
                                  item.entryIndex) != selection.end();
        Color base = CategoryColor(e.category);
        // Vary the shade a little by index so equal categories stay separable.
        int delta = int(item.entryIndex % 5) * 6 - 12;
        Color fill(clampi(base.r + delta, 0, 255), clampi(base.g + delta, 0, 255),
                   clampi(base.b + delta, 0, 255), 255);
        if (hovered) fill = Color(clampi(fill.r + 24, 0, 255),
                                  clampi(fill.g + 24, 0, 255),
                                  clampi(fill.b + 24, 0, 255), 255);
        ctx->SetFillPaint(fill);
        ctx->FillRectangle(Rect2Dd(item.rect));
        ctx->SetStrokePaint(Color(255, 255, 255, 200));
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRectangle(Rect2Dd(item.rect));

        if (item.rect.width >= 46 && item.rect.height >= 26) {
            ctx->PushState();
            ctx->ClipRect(Rect2Dd(item.rect));
            FontStyle fsty;
            fsty.fontFamily = style.fontFamily;
            fsty.fontSize = style.smallFontSize;
            fsty.fontWeight = FontWeight::Bold;
            ctx->SetFontStyle(fsty);
            ctx->SetTextPaint(Color(255, 255, 255, 235));
            std::string shown = EllipsizeText(ctx, e.name, item.rect.width - 8);
            ctx->DrawText(shown, Point2Dd(item.rect.x + 4, item.rect.y + 3));
            if (item.rect.height >= 42) {
                fsty.fontWeight = FontWeight::Normal;
                ctx->SetFontStyle(fsty);
                ctx->SetTextPaint(Color(255, 255, 255, 190));
                ctx->DrawText(FormatSize(e.effectiveSize),
                              Point2Dd(item.rect.x + 4,
                                       item.rect.y + 3 + style.smallFontSize + 4));
            }
            ctx->PopState();
        }

        if (selected) {
            ctx->SetStrokePaint(style.selectionBorderColor);
            ctx->SetStrokeWidth(2.0f);
            ctx->DrawRectangle(Rect2Dd(item.rect));
        }
    }

    void UltraCanvasFilerWidget::DrawHoverIconMenu(IRenderContext* ctx,
                                                   const ItemLayout& item) {
        static const IconMenuAction actions[] = {
            IconMenuAction::Copy, IconMenuAction::Cut,
            IconMenuAction::Rename, IconMenuAction::Delete,
        };
        int sz = style.iconMenuButtonSize;
        int gap = 2;
        int total = 4 * sz + 3 * gap;
        int x = item.rect.x + item.rect.width - total - 4;
        int y = item.rect.y + 2;
        for (IconMenuAction a : actions) {
            Rect2Di button(x, y, sz, sz);
            ctx->SetFillPaint(style.iconMenuBackground);
            ctx->FillRoundedRectangle(Rect2Dd(button), 4);
            DrawIconMenuGlyph(ctx, a, button);
            iconMenuHits.push_back({button, item.entryIndex, a});
            x += sz + gap;
        }
    }

    void UltraCanvasFilerWidget::DrawIconMenuGlyph(IRenderContext* ctx,
                                                   IconMenuAction action,
                                                   const Rect2Di& button) {
        // Small vector glyphs so no icon assets are required.
        double cx = button.x + button.width / 2.0;
        double cy = button.y + button.height / 2.0;
        double r = button.width * 0.26;
        ctx->SetStrokePaint(style.iconMenuGlyphColor);
        ctx->SetStrokeWidth(1.4f);
        switch (action) {
            case IconMenuAction::Copy: {
                // Two overlapping sheets.
                ctx->DrawRectangle(Rect2Dd(cx - r, cy - r * 1.2, r * 1.4, r * 1.7));
                ctx->DrawRectangle(Rect2Dd(cx - r * 0.4, cy - r * 0.5, r * 1.4, r * 1.7));
                break;
            }
            case IconMenuAction::Cut: {
                // Scissors: crossing blades + finger rings.
                ctx->DrawLine(Point2Dd(cx - r, cy - r), Point2Dd(cx + r * 0.7, cy + r));
                ctx->DrawLine(Point2Dd(cx + r, cy - r), Point2Dd(cx - r * 0.7, cy + r));
                ctx->DrawCircle(Point2Dd(cx - r * 0.85, cy + r * 1.05), r * 0.35);
                ctx->DrawCircle(Point2Dd(cx + r * 0.85, cy + r * 1.05), r * 0.35);
                break;
            }
            case IconMenuAction::Rename: {
                // Pencil over a baseline.
                ctx->DrawLine(Point2Dd(cx - r, cy + r), Point2Dd(cx + r * 0.9, cy - r * 0.9));
                ctx->DrawLine(Point2Dd(cx - r, cy + r), Point2Dd(cx - r * 0.5, cy + r));
                ctx->DrawLine(Point2Dd(cx - r, cy + r), Point2Dd(cx - r, cy + r * 0.5));
                break;
            }
            case IconMenuAction::Delete: {
                // Trash can: body, lid and handle.
                ctx->DrawRectangle(Rect2Dd(cx - r * 0.8, cy - r * 0.5, r * 1.6, r * 1.6));
                ctx->DrawLine(Point2Dd(cx - r * 1.1, cy - r * 0.5),
                              Point2Dd(cx + r * 1.1, cy - r * 0.5));
                ctx->DrawLine(Point2Dd(cx - r * 0.35, cy - r * 0.5),
                              Point2Dd(cx - r * 0.35, cy - r * 0.95));
                ctx->DrawLine(Point2Dd(cx - r * 0.35, cy - r * 0.95),
                              Point2Dd(cx + r * 0.35, cy - r * 0.95));
                ctx->DrawLine(Point2Dd(cx + r * 0.35, cy - r * 0.95),
                              Point2Dd(cx + r * 0.35, cy - r * 0.5));
                break;
            }
        }
    }

    void UltraCanvasFilerWidget::DrawRenameEditor(IRenderContext* ctx,
                                                  const ItemLayout& item) {
        // The editable field sits where the item's name is shown.
        Rect2Di field;
        if (viewType == FilerViewType::Details || viewType == FilerViewType::List ||
            viewType == FilerViewType::BarSize) {
            int x = item.imageRect.x + item.imageRect.width + 4;
            field = Rect2Di(x, item.rect.y + 1,
                            std::max(80, item.rect.width
                                     - (x - item.rect.x) - 8),
                            item.rect.height - 2);
            if (viewType == FilerViewType::Details && !detailsColumns.empty()) {
                field.width = std::max(80, detailsColumns[0].x
                                       + detailsColumns[0].width - x - 4);
            }
        } else {   // thumbnails / treemap
            int capTop = item.imageRect.y + item.imageRect.height;
            field = Rect2Di(item.rect.x + 2, capTop,
                            item.rect.width - 4, style.captionHeight);
        }

        ctx->SetFillPaint(style.renameFieldColor);
        ctx->FillRectangle(Rect2Dd(field));
        ctx->SetStrokePaint(style.renameBorderColor);
        ctx->SetStrokeWidth(1.5f);
        ctx->DrawRectangle(Rect2Dd(field));

        FontStyle fsty;
        fsty.fontFamily = style.fontFamily;
        fsty.fontSize = style.fontSize;
        ctx->SetFontStyle(fsty);
        ctx->SetTextPaint(style.textColor);
        ctx->PushState();
        ctx->ClipRect(Rect2Dd(field));
        Size2Di ts = ctx->GetTextLineDimensions(renameBuffer);
        int tx = field.x + 4;
        if (ts.width > field.width - 12) tx = field.x + field.width - 12 - ts.width;
        int ty = field.y + (field.height - ts.height) / 2;
        ctx->DrawText(renameBuffer, Point2Dd(tx, ty));
        // Caret at the end of the text.
        ctx->SetStrokePaint(style.textColor);
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawLine(Point2Dd(tx + ts.width + 1, field.y + 3),
                      Point2Dd(tx + ts.width + 1, field.y + field.height - 3));
        ctx->PopState();
    }

    void UltraCanvasFilerWidget::DrawScrollbar(IRenderContext* ctx) {
        ScrollbarGeom g = ScrollbarGeometry();
        if (!g.active) return;
        Color thumbColor = draggingScrollbar ? Color(70, 70, 78, 220)
                                             : Color(90, 90, 96, 180);
        if (g.horizontal) {
            ctx->SetFillPaint(Color(0, 0, 0, 30));
            ctx->FillRoundedRectangle(Rect2Dd(g.track.x + 2, g.track.y,
                                              g.track.width - 4, g.track.height), 3);
        } else {
            ctx->SetFillPaint(Color(0, 0, 0, 30));
            ctx->FillRoundedRectangle(Rect2Dd(g.track.x, g.track.y + 2,
                                              g.track.width, g.track.height - 4), 3);
        }
        ctx->SetFillPaint(thumbColor);
        ctx->FillRoundedRectangle(Rect2Dd(g.thumb), 3);
    }

    // ===== HIT TESTING =====
    Point2Di UltraCanvasFilerWidget::ToContentPoint(const Point2Di& localPoint) const {
        return Point2Di(localPoint.x + scrollOffsetX, localPoint.y + scrollOffsetY);
    }

    int UltraCanvasFilerWidget::ItemAt(const Point2Di& contentPoint) const {
        for (const ItemLayout& it : items) {
            if (it.rect.Contains(contentPoint))
                return static_cast<int>(it.entryIndex);
        }
        return -1;
    }

    int UltraCanvasFilerWidget::IconMenuActionAt(const Point2Di& localPoint,
                                                 size_t& outEntry) const {
        for (const IconMenuHit& h : iconMenuHits) {
            Rect2Di screenRect(h.rect.x - scrollOffsetX, h.rect.y - scrollOffsetY,
                               h.rect.width, h.rect.height);
            if (screenRect.Contains(localPoint)) {
                outEntry = h.entryIndex;
                return static_cast<int>(h.action);
            }
        }
        return -1;
    }

    int UltraCanvasFilerWidget::DetailsHeaderColumnAt(const Point2Di& localPoint) const {
        if (viewType != FilerViewType::Details) return -1;
        Rect2Di area = ContentBounds();
        if (localPoint.y < area.y || localPoint.y > area.y + detailsHeaderHeight)
            return -1;
        for (size_t i = 0; i < detailsColumns.size(); ++i) {
            const DetailsColumn& c = detailsColumns[i];
            if (c.sortable && localPoint.x >= c.x && localPoint.x < c.x + c.width)
                return static_cast<int>(i);
        }
        return -1;
    }

    // ===== INTERACTION =====
    void UltraCanvasFilerWidget::HandleItemClick(int index, bool ctrl, bool shift) {
        if (index < 0) {
            ClearSelection();
            return;
        }
        size_t idx = static_cast<size_t>(index);
        if (shift && lastClickedIndex >= 0) {
            size_t from = std::min<size_t>(lastClickedIndex, idx);
            size_t to = std::max<size_t>(lastClickedIndex, idx);
            if (!ctrl) selection.clear();
            for (size_t i = from; i <= to; ++i) {
                if (std::find(selection.begin(), selection.end(), i) == selection.end())
                    selection.push_back(i);
            }
        } else if (ctrl) {
            auto it = std::find(selection.begin(), selection.end(), idx);
            if (it != selection.end()) selection.erase(it);
            else selection.push_back(idx);
            lastClickedIndex = index;
        } else {
            selection.clear();
            selection.push_back(idx);
            lastClickedIndex = index;
        }
        FireSelectionChanged();
        RequestRedraw();
    }

    void UltraCanvasFilerWidget::ActivateEntry(size_t index) {
        if (index >= entries.size()) return;
        const FilerEntry e = entries[index];   // copy: SetPath frees `entries`
        if (e.isDirectory || e.isArchive) {
            SetPath(e.path);
            return;
        }
        if (onFileActivated) onFileActivated(e);
    }

    void UltraCanvasFilerWidget::OpenContextMenu(const Point2Di& localPoint) {
        auto win = GetWindow();
        if (!win) return;

        std::vector<FilerEntry> targets = GetSelectedEntries();
        bool hasSel = !targets.empty();
        bool singleSel = targets.size() == 1;
        bool anyArchive = false;
        for (const FilerEntry& t : targets) if (t.isArchive) anyArchive = true;

        activePopupMenu = std::make_shared<UltraCanvasMenu>(
                GetIdentifier() + "_ctx", 0, 0, 230, 0);
        activePopupMenu->SetMenuType(MenuType::PopupMenu);
        auto& menu = *activePopupMenu;

        auto addAction = [&menu](const std::string& label, bool enabled,
                                 std::function<void()> cb) {
            MenuItemData item = MenuItemData::Action(label, std::move(cb));
            item.enabled = enabled;
            menu.AddItem(item);
        };

        addAction("Copy", hasSel, [this]() { CopySelection(); });
        addAction("Cut", hasSel, [this]() { CutSelection(); });
        addAction("Paste", ClipboardHasContent(), [this]() { Paste(); });
        addAction("Delete", hasSel, [this]() { DeleteSelection(); });
        addAction("Duplicate", hasSel, [this]() { DuplicateSelection(); });
        {
            size_t renameIdx = singleSel ? selection.front() : 0;
            addAction("Rename", singleSel,
                      [this, renameIdx]() { StartRename(renameIdx); });
        }
        menu.AddItem(MenuItemData::Separator());

        // New >
        {
            std::vector<MenuItemData> newItems;
            for (const FilerNewDocumentType& t : newDocumentTypes) {
                FilerNewDocumentType copy = t;
                newItems.push_back(MenuItemData::Action(
                        t.label, [this, copy]() { CreateNewDocument(copy); }));
            }
            menu.AddItem(MenuItemData::Submenu("New", newItems));
        }
        menu.AddItem(MenuItemData::Separator());

        // Display > Sort / Type / Icon-Menu
        {
            std::vector<MenuItemData> sortItems;
            static const FilerSortField fields[] = {
                FilerSortField::Name, FilerSortField::Size, FilerSortField::Type,
                FilerSortField::ModifiedDate, FilerSortField::CreatedDate,
            };
            for (FilerSortField f : fields) {
                sortItems.push_back(MenuItemData::Radio(
                        SortFieldLabel(f), 1, sortField == f,
                        [this, f]() { SetSortField(f); }));
            }
            sortItems.push_back(MenuItemData::Separator());
            sortItems.push_back(MenuItemData::Radio(
                    "Ascending", 2, sortAscending,
                    [this]() { SetSortAscending(true); }));
            sortItems.push_back(MenuItemData::Radio(
                    "Descending", 2, !sortAscending,
                    [this]() { SetSortAscending(false); }));

            std::vector<MenuItemData> typeItems;
            static const FilerViewType views[] = {
                FilerViewType::Details, FilerViewType::List,
                FilerViewType::ThumbnailsSmall, FilerViewType::ThumbnailsMedium,
                FilerViewType::ThumbnailsBig, FilerViewType::ThumbnailsMaximized,
                FilerViewType::BarSize, FilerViewType::TreeMap,
                FilerViewType::GourceTree, FilerViewType::View3D,
            };
            for (FilerViewType v : views) {
                typeItems.push_back(MenuItemData::Radio(
                        ViewTypeLabel(v), 3, viewType == v,
                        [this, v]() { SetViewType(v); }));
            }

            std::vector<MenuItemData> displayItems;
            displayItems.push_back(MenuItemData::Submenu("Sort", sortItems));
            displayItems.push_back(MenuItemData::Submenu("Type", typeItems));
            displayItems.push_back(MenuItemData::Checkbox(
                    "Icon-Menu", hoverIconMenu,
                    [this](bool on) { SetHoverIconMenuEnabled(on); }));
            menu.AddItem(MenuItemData::Submenu("Display", displayItems));
        }
        menu.AddItem(MenuItemData::Separator());

        // Open with >
        {
            std::vector<MenuItemData> openItems;
            if (openWithApps.empty()) {
                MenuItemData none = MenuItemData::Action("(no applications)", []() {});
                none.enabled = false;
                openItems.push_back(none);
            }
            for (const FilerOpenWithApp& app : openWithApps) {
                auto onOpen = app.onOpen;
                auto cb = [this, onOpen]() {
                    if (onOpen) onOpen(GetSelectedEntries());
                };
                if (!app.iconPath.empty()) {
                    openItems.push_back(MenuItemData::Action(app.label, app.iconPath, cb));
                } else {
                    openItems.push_back(MenuItemData::Action(app.label, cb));
                }
            }
            menu.AddItem(MenuItemData::Submenu("Open with", openItems));
        }
        menu.AddItem(MenuItemData::Separator());

        addAction("Compress", !entries.empty(), [this]() { CompressSelection(); });
        addAction("Extract", anyArchive, [this]() { ExtractSelection(); });
        menu.AddItem(MenuItemData::Separator());

        addAction("Print", static_cast<bool>(onPrint), [this]() {
            if (onPrint) onPrint(SelectionOrAll());
        });
        menu.AddItem(MenuItemData::Separator());

        if (showOpenPathItem) {
            size_t openIdx = hasSel ? selection.front() : 0;
            addAction("Open Path", hasSel, [this, openIdx]() {
                if (openIdx >= entries.size()) return;
                const FilerEntry e = entries[openIdx];
                if (onOpenPath) onOpenPath(e);
                else SetPath(fs::path(e.path).parent_path().string());
            });
        }

        // Extras >
        {
            std::vector<MenuItemData> extraItems;
            MenuItemData share = MenuItemData::Action("Share", [this]() {
                if (onShare) onShare(SelectionOrAll());
            });
            share.enabled = static_cast<bool>(onShare);
            extraItems.push_back(share);

            MenuItemData attrs = MenuItemData::Action("Attributes", [this]() {
                if (onAttributes) onAttributes(SelectionOrAll());
            });
            attrs.enabled = static_cast<bool>(onAttributes);
            extraItems.push_back(attrs);

            extraItems.push_back(MenuItemData::Action("Copy path", [this]() {
                std::string text;
                std::vector<FilerEntry> sel = GetSelectedEntries();
                if (sel.empty()) text = currentPath;
                else for (const FilerEntry& e : sel) {
                    if (!text.empty()) text += '\n';
                    text += e.path;
                }
                SetClipboardText(text);
            }));

            MenuItemData access = MenuItemData::Action("Access", [this]() {
                if (onAccess) onAccess(SelectionOrAll());
            });
            access.enabled = static_cast<bool>(onAccess);
            extraItems.push_back(access);

            menu.AddItem(MenuItemData::Submenu("Extras", extraItems));
        }

        addAction("Settings", static_cast<bool>(onSettings), [this]() {
            if (onSettings) onSettings();
        });

        Point2Di winPos(static_cast<int>(GetXInWindow()) + localPoint.x,
                        static_cast<int>(GetYInWindow()) + localPoint.y);
        PopupElementSettings settings;
        settings.popupOwner = weak_from_this();
        activePopupMenu->OpenMenu(winPos, *win, settings);
    }

    bool UltraCanvasFilerWidget::HandleRenameKey(const UCEvent& event) {
        if (renamingIndex < 0) return false;
        if (event.type == UCEventType::KeyDown) {
            switch (event.virtualKey) {
                case UCKeys::Return:
                    CommitRename();
                    return true;
                case UCKeys::Escape:
                    CancelRename();
                    return true;
                case UCKeys::Backspace: {
                    if (!renameBuffer.empty()) {
                        size_t cut = renameBuffer.size() - 1;
                        while (cut > 0 &&
                               (static_cast<unsigned char>(renameBuffer[cut]) & 0xC0) == 0x80)
                            --cut;
                        renameBuffer.erase(cut);
                        RequestRedraw();
                    }
                    return true;
                }
                default:
                    return false;   // characters arrive via KeyChar / TextInput
            }
        }
        if (event.type == UCEventType::TextInput) {
            std::string in = event.text;
            if (in.empty() && event.character >= 32) in.assign(1, event.character);
            // Strip control characters and the path separator.
            std::string filtered;
            for (char c : in) {
                if (static_cast<unsigned char>(c) >= 32 && c != '/') filtered += c;
            }
            if (!filtered.empty()) {
                renameBuffer += filtered;
                RequestRedraw();
            }
            return true;
        }
        return false;
    }

    // ===== EVENTS =====
    bool UltraCanvasFilerWidget::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseLeave: {
                if (hoveredIndex != -1) { hoveredIndex = -1; RequestRedraw(); }
                draggingScrollbar = false;
                return true;
            }
            case UCEventType::MouseWheel: {
                if (IsHorizontal()) {
                    if (MaxScrollX() <= 0) return false;
                    scrollOffsetX -= event.wheelDelta * kWheelStep;
                } else {
                    if (MaxScrollY() <= 0) return false;
                    scrollOffsetY -= event.wheelDelta * kWheelStep;
                }
                ClampScroll();
                RequestRedraw();
                return true;
            }
            case UCEventType::MouseMove: {
                Point2Di local(event.pointer.x, event.pointer.y);
                if (draggingScrollbar) {
                    ScrollThumbTo((IsHorizontal() ? local.x : local.y)
                                  - scrollbarGrabOffset);
                    RequestRedraw();
                    return true;
                }
                int newHover = ItemAt(ToContentPoint(local));
                if (newHover != hoveredIndex) {
                    hoveredIndex = newHover;
                    RequestRedraw();
                }
                return false;
            }
            case UCEventType::MouseDown: {
                Point2Di local(event.pointer.x, event.pointer.y);
                SetFocus(true);

                if (event.button == UCMouseButton::Right) {
                    if (renamingIndex >= 0) CommitRename();
                    int index = ItemAt(ToContentPoint(local));
                    // Right-click keeps an existing multi-selection when it hits
                    // one of its items; otherwise it selects what's under it.
                    if (index >= 0 &&
                        std::find(selection.begin(), selection.end(),
                                  static_cast<size_t>(index)) == selection.end()) {
                        HandleItemClick(index, false, false);
                    } else if (index < 0) {
                        ClearSelection();
                    }
                    OpenContextMenu(local);
                    return true;
                }
                if (event.button != UCMouseButton::Left) return false;

                if (renamingIndex >= 0) CommitRename();

                // Scrollbar first: it sits above the content.
                {
                    ScrollbarGeom g = ScrollbarGeometry();
                    if (g.active) {
                        Rect2Di hit = g.track;
                        if (g.horizontal) { hit.y -= 6; hit.height += 8; }
                        else              { hit.x -= 6; hit.width  += 8; }
                        if (hit.Contains(local)) {
                            if (g.thumb.Contains(local)) {
                                scrollbarGrabOffset = g.horizontal
                                        ? (local.x - g.thumb.x) : (local.y - g.thumb.y);
                            } else {
                                scrollbarGrabOffset =
                                        (g.horizontal ? g.thumb.width : g.thumb.height) / 2;
                                ScrollThumbTo((g.horizontal ? local.x : local.y)
                                              - scrollbarGrabOffset);
                            }
                            draggingScrollbar = true;
                            RequestRedraw();
                            return true;
                        }
                    }
                }

                // Hover icon-menu buttons take precedence over the item.
                {
                    size_t entryIdx = 0;
                    int action = IconMenuActionAt(local, entryIdx);
                    if (action >= 0) {
                        if (std::find(selection.begin(), selection.end(), entryIdx)
                            == selection.end()) {
                            HandleItemClick(static_cast<int>(entryIdx), false, false);
                        }
                        switch (static_cast<IconMenuAction>(action)) {
                            case IconMenuAction::Copy:   CopySelection(); break;
                            case IconMenuAction::Cut:    CutSelection(); break;
                            case IconMenuAction::Rename: StartRename(entryIdx); break;
                            case IconMenuAction::Delete: DeleteSelection(); break;
                        }
                        return true;
                    }
                }

                // Details header: click toggles / switches the sort column.
                {
                    int col = DetailsHeaderColumnAt(local);
                    if (col >= 0) {
                        FilerSortField f = detailsColumns[col].field;
                        if (sortField == f) SetSortAscending(!sortAscending);
                        else SetSort(f, true);
                        return true;
                    }
                }

                HandleItemClick(ItemAt(ToContentPoint(local)),
                                event.ctrl, event.shift);
                return true;
            }
            case UCEventType::MouseUp: {
                if (draggingScrollbar) {
                    draggingScrollbar = false;
                    RequestRedraw();
                    return true;
                }
                return false;
            }
            case UCEventType::MouseDoubleClick: {
                Point2Di local(event.pointer.x, event.pointer.y);
                int index = ItemAt(ToContentPoint(local));
                if (index >= 0) {
                    ActivateEntry(static_cast<size_t>(index));
                    return true;
                }
                return false;
            }
            case UCEventType::KeyDown: {
                if (HandleRenameKey(event)) return true;
                if (renamingIndex >= 0) return true;   // swallow while editing

                if (event.ctrl) {
                    switch (event.virtualKey) {
                        case 'a': case 'A': SelectAll(); return true;
                        case 'c': case 'C': CopySelection(); return true;
                        case 'x': case 'X': CutSelection(); return true;
                        case 'v': case 'V': Paste(); return true;
                        default: break;
                    }
                    return false;
                }
                switch (event.virtualKey) {
                    case UCKeys::Return:
                        if (!selection.empty()) ActivateEntry(selection.front());
                        return true;
                    case UCKeys::Delete:
                        DeleteSelection();
                        return true;
                    case UCKeys::F2:
                        if (selection.size() == 1) StartRename(selection.front());
                        return true;
                    case UCKeys::Up:
                    case UCKeys::Down:
                    case UCKeys::Left:
                    case UCKeys::Right: {
                        if (entries.empty()) return true;
                        // Per-view arrow steps: rows step by 1; grids by column
                        // count; the list flows down first, so left/right jump
                        // a whole column.
                        int step = 1;
                        bool vertical = (event.virtualKey == UCKeys::Up ||
                                         event.virtualKey == UCKeys::Down);
                        bool negative = (event.virtualKey == UCKeys::Up ||
                                         event.virtualKey == UCKeys::Left);
                        Rect2Di area = ContentBounds();
                        if (viewType == FilerViewType::List) {
                            int rowsPerColumn =
                                    std::max(1, area.height / style.listRowHeight);
                            step = vertical ? 1 : rowsPerColumn;
                        } else if (viewType == FilerViewType::Details ||
                                   viewType == FilerViewType::BarSize) {
                            if (!vertical) return true;
                            step = 1;
                        } else {
                            int edge = ThumbnailEdge();
                            int cols = std::max(1, (area.width - 10 + style.tileGap)
                                                   / (edge + style.tileGap));
                            step = vertical ? cols : 1;
                        }
                        int current = selection.empty()
                                ? (negative ? (int)entries.size() : -1)
                                : (int)selection.front();
                        int next = clampi(current + (negative ? -step : step),
                                          0, (int)entries.size() - 1);
                        selection.clear();
                        selection.push_back(static_cast<size_t>(next));
                        lastClickedIndex = next;
                        EnsureVisible(static_cast<size_t>(next));
                        FireSelectionChanged();
                        RequestRedraw();
                        return true;
                    }
                    default:
                        break;
                }
                return false;
            }
            case UCEventType::TextInput:
                return HandleRenameKey(event);
            default:
                break;
        }
        return UltraCanvasContainer::OnEvent(event);
    }

} // namespace UltraCanvas
