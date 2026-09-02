// Apps/UltraFiler/UltraFilerWindow.cpp
// UltraFiler main window: Windows Explorer style file manager built from the
// UltraCanvas folder tree (UltraCanvasTreeView), tabbed folder content
// (UltraCanvasTabbedContainer + UltraCanvasFilerWidget per tab), a recursive
// search field and the detail pane: a selected media file shows in the media
// preview (UltraCanvasMediaViewer), a selected folder shows its content
// through the folder preview (a second UltraCanvasFilerWidget in
// small-thumbnail mode) — the two share the pane. The toolbar's
// clock button swaps that whole area for the History view — Files / Folders /
// Apps tabs listing the recently used paths (UltraFilerHistory) as small
// thumbnails; folders get there by being worked in (the filer's
// onFolderModified), not by being browsed. The heart button swaps the same
// area for the Favorites view — the same tabs, but listing what the user
// pinned (UltraFilerFavorites); the folder tree's "Pinned" section holds the
// tree pins, whose bookmark entries navigate on click, and the tree's context
// menu offers Copy / Delete / Paste on folders, a Pin submenu whose
// "To Treeview" / "To Favorites" flags show and toggle where the folder is
// pinned, and Unpin on pinned entries. The filer context menus' Extras
// submenu ends with an app-provided block (extrasMenuProvider): "Open
// prompt", then Pin / Unpin submenus whose "To Treeview" / "To Favorites"
// flags follow the current selection.
// The gear button at the right end of the navigation row opens the settings
// window (UltraFilerSettingsDialog), which also clears the history / the
// favorites; persisted settings load at startup and configure the preview's
// transparent-image backdrop, the width of the page thumbnails in the
// preview's PDF page inventory and the folder tree's colours - the background
// of the drive rows and the highlight of the selected folder. A backdrop
// colour picked from the strip under a transparent image in the preview is
// saved the same way. Esc closes the History or Favorites view, or an open
// media preview.
// Version: 1.15.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework

#include "UltraFilerWindow.h"

#include "UltraCanvasAlert.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasFileAssociations.h"
#include "UltraCanvasNativeDialogs.h"
#include "UltraCanvasUtils.h"
#include "UltraFilerPropertiesDialogs.h"
#include "UltraFilerSettingsDialog.h"
#include "UltraFilerShare.h"
#include "UltraFilerPrompt.h"
#ifdef ULTRACANVAS_HAS_ULTRAWIN
#include "UltraFilerRunWindowsDialog.h"
#include "UltraWin/UltraWin.h"
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <unordered_set>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;

namespace UltraCanvas {

namespace {

    // Suffix marking the lazy placeholder child of an unexpanded folder node.
    constexpr const char* kPlaceholderSuffix = "\n#placeholder";

    // Node id of the folder tree's "Pinned" section header, and the id prefix
    // of its bookmark children ("ufl-pin:" + folder path). The prefix keeps a
    // pinned folder's node id distinct from the same folder's regular node,
    // whose id is the bare path.
    // The folder tree's hidden root. It only carries the two top-level
    // sections ("Pinned" and "Computer") and is never drawn.
    constexpr const char* kTreeRootNodeId = "ufl-tree-root";
    constexpr const char* kPinnedNodeId = "ufl-pinned";
    constexpr const char* kPinnedChildPrefix = "ufl-pin:";
    constexpr size_t kPinnedChildPrefixLen = 8;   // strlen(kPinnedChildPrefix)
    // "Cloud Storage": the section header the OneDrive / Google Drive /
    // Dropbox / iCloud folders hang under. Like "Pinned" it is a header, not a
    // folder, and is never scanned as a path.
    constexpr const char* kCloudNodeId = "ufl-cloud";

    // Single UI font size (pt) used by every element of the window.
    constexpr float kUiFontSize = 9.0f;

    // Split-pane minimum widths (px): the folder display and the detail
    // (preview) pane never get narrower than these.
    constexpr int kFilerMinWidth   = 360;
    // Height of the folder tab strip at the top of the window (one tab high -
    // the strip carries no content area of its own).
    constexpr int kTabStripHeight  = 30;
    constexpr int kPreviewMinWidth = 260;

    // Delay before a clicked folder's content is shown in the detail pane.
    // A double-click on a folder OPENS it, so the pane must not scan the
    // folder in on the first click of that double-click — like the filer's
    // rename-click delay, this must exceed the platform double-click
    // interval.
    constexpr unsigned int kFolderPreviewClickDelayMs = 500;

    void ApplyDropdownFontSize(UltraCanvasDropdown* dropdown) {
        DropdownStyle s = dropdown->GetStyle();
        s.fontSize = kUiFontSize;
        dropdown->SetStyle(s);
    }

    std::string IconPath(const std::string& fileName) {
        return NormalizePath(GetResourcesDir() + "media/icons/" + fileName);
    }

    std::string PlaceholderId(const std::string& folderPath) {
        return folderPath + kPlaceholderSuffix;
    }

    std::string UserHomeDir() {
#ifdef _WIN32
        const char* home = std::getenv("USERPROFILE");
#else
        const char* home = std::getenv("HOME");
#endif
        return home ? std::string(home) : std::string();
    }

    // Is `path` the user's home folder? Compared through FolderIdentityKey, so
    // a differently spelled or differently cased path for it still matches.
    bool IsUserHomeDir(const std::string& path);

    // The home folder shows only the user's *main* folders in the tree, the way
    // the Explorer and Finder sidebars do: Desktop, Documents, Downloads,
    // Music, Pictures and Videos, resolved through the platform
    // (GetWellKnownUserFolders), so a redirected or localized folder -
    // "Bilder", a Documents folder moved into OneDrive - is the one listed.
    // The rest of the profile ("3D Objects", "Saved Games", "Links", the
    // working folders a user drops in their home, and the sync-client folders
    // that now have their own Cloud Storage section) stays out of the tree;
    // the folder display still lists every one of them. Add a kind here to
    // give it a row of its own.
    constexpr UserFolderKind kHomeTreeFolders[] = {
        UserFolderKind::Desktop,
        UserFolderKind::Documents,
        UserFolderKind::Downloads,
        UserFolderKind::Music,
        UserFolderKind::Pictures,
        UserFolderKind::Videos,
    };

    bool IsHomeTreeFolder(UserFolderKind kind) {
        for (UserFolderKind k : kHomeTreeFolders)
            if (k == kind) return true;
        return false;
    }

    // Tree icon for each of the well-known home folders
    // (GetWellKnownUserFolders), Explorer / Finder sidebar style.
    const char* UserFolderIconFile(UserFolderKind kind) {
        switch (kind) {
            case UserFolderKind::Desktop:   return "computer.png";
            case UserFolderKind::Documents: return "document.svg";
            case UserFolderKind::Downloads: return "download.png";
            case UserFolderKind::Music:     return "audio.png";
            case UserFolderKind::Pictures:  return "image.svg";
            case UserFolderKind::Videos:    return "video.png";
            default:                        return "folder-brown.svg";
        }
    }

    // Identity key that makes two spellings of the same folder compare equal
    // (used to keep a curated home folder from listing twice): normalised,
    // no trailing separator, case-folded on Windows.
    std::string FolderIdentityKey(const std::string& path) {
        std::string key = fs::path(path).lexically_normal().string();
        while (key.size() > 1 && (key.back() == '/' || key.back() == '\\'))
            key.pop_back();
#ifdef _WIN32
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
#endif
        return key;
    }

    bool IsUserHomeDir(const std::string& path) {
        const std::string home = UserHomeDir();
        return !home.empty() && FolderIdentityKey(path) == FolderIdentityKey(home);
    }

    // One row of the folder tree, as EnsureTreeChildren is about to add it.
    struct TreeChild { std::string path, label, icon; };

    // The rows the home folder shows: its main user folders (kHomeTreeFolders),
    // and nothing else. A well-known folder that resolves to the home folder
    // itself is skipped, so the home folder is never listed inside itself.
    std::vector<TreeChild> HomeTreeChildren() {
        std::vector<TreeChild> children;
        const std::string homeKey = FolderIdentityKey(UserHomeDir());
        for (const UserFolderInfo& f : GetWellKnownUserFolders()) {
            if (!IsHomeTreeFolder(f.kind)) continue;
            if (FolderIdentityKey(f.path) == homeKey) continue;
            children.push_back({f.path, f.label, UserFolderIconFile(f.kind)});
        }
        return children;
    }

    // Would moving `src` into the folder `dest` be a no-op or copy a folder into
    // itself? Both are resolved to canonical form first (following symlinks and
    // "."/".."), so differently spelled paths for the same folder still match.
    // Rejected: dropping a folder onto itself, onto the folder it already lives
    // in, or into its own subtree (a descendant of itself).
    bool IsInvalidMoveInto(const std::string& src, const std::string& dest) {
        std::error_code ec;
        fs::path s = fs::weakly_canonical(fs::path(src), ec);
        if (ec) { s = fs::path(src).lexically_normal(); ec.clear(); }
        fs::path d = fs::weakly_canonical(fs::path(dest), ec);
        if (ec) d = fs::path(dest).lexically_normal();
        if (s == d) return true;                 // onto itself
        if (s.parent_path() == d) return true;   // already lives in dest
        // dest is src or a descendant of src -> would nest a folder in itself.
        fs::path rel = d.lexically_relative(s);
        return !rel.empty() && *rel.begin() != "..";
    }

    // Does `path` contain at least one visible subfolder? (Cheap check that
    // decides whether a tree node gets an expand button.)
    // "Hidden" is the platform's own notion (IsHiddenFileSystemEntry): dot
    // names everywhere, plus the hidden attribute on Windows - which is what
    // keeps AppData and the "Anwendungsdaten"-style compatibility junctions
    // of a profile folder out of the tree - and UF_HIDDEN on macOS.
    bool HasSubdirectories(const std::string& path) {
        std::error_code ec;
        // Stepped with the error_code overload: the range-for's operator++
        // throws filesystem_error when a read fails part-way through (a
        // removable or network drive going away), and this runs on the probe
        // worker thread, where a throw ends the process.
        for (fs::directory_iterator it(path, fs::directory_options::skip_permission_denied, ec),
                 end; !ec && it != end; it.increment(ec)) {
            std::error_code dec;
            if (it->is_directory(dec) && !dec && !IsHiddenFileSystemEntry(it->path()))
                return true;
        }
        return false;
    }

    // The folder display's curation of the home folder: the same main folders
    // the tree shows (HomeTreeChildren), as paths for
    // UltraCanvasFilerWidget::SetCuratedHomeFolder.
    std::vector<std::string> HomeCurationPaths() {
        std::vector<std::string> paths;
        for (const TreeChild& c : HomeTreeChildren())
            paths.push_back(c.path);
        return paths;
    }

    // Does the tree show anything below `path`? A CURATED home folder answers
    // from its curated list rather than from the disk: it only ever shows the
    // main user folders, so a profile holding none of them is a leaf however
    // many other folders sit in it - and gets no expand button that opens
    // onto nothing. With Settings > Display > Home folder on "Show all
    // content" the home folder answers like any other folder.
    bool TreeFolderHasChildren(const std::string& path, bool curatedHome) {
        if (curatedHome && IsUserHomeDir(path)) return !HomeTreeChildren().empty();
        return HasSubdirectories(path);
    }

    // Visible subfolders of `path`, sorted case-insensitively by name.
    std::vector<fs::path> ListSubdirectories(const std::string& path) {
        std::vector<fs::path> dirs;
        std::error_code ec;
        for (fs::directory_iterator it(path, fs::directory_options::skip_permission_denied, ec),
                 end; !ec && it != end; it.increment(ec)) {
            std::error_code dec;
            if (it->is_directory(dec) && !dec && !IsHiddenFileSystemEntry(it->path()))
                dirs.push_back(it->path());
        }
        std::sort(dirs.begin(), dirs.end(), [](const fs::path& a, const fs::path& b) {
            std::string an = a.filename().string(), bn = b.filename().string();
            std::transform(an.begin(), an.end(), an.begin(), ::tolower);
            std::transform(bn.begin(), bn.end(), bn.begin(), ::tolower);
            return an < bn;
        });
        return dirs;
    }

#ifndef _WIN32
    // True when `path` is an actual mount point - its device differs from its
    // parent's. Keeps unmounted placeholder folders under /media and /mnt from
    // being shown as drives.
    bool IsMountPoint(const std::string& path) {
        struct stat here{}, parent{};
        if (lstat(path.c_str(), &here) != 0) return false;
        const std::string up = fs::path(path).parent_path().string();
        if (up.empty() || lstat(up.c_str(), &parent) != 0) return false;
        return here.st_dev != parent.st_dev;
    }
#endif

    // A plain flex wrapper that must never scroll itself - the filer, the tree
    // and the preview are the only scroll regions of the window.
    std::shared_ptr<UltraCanvasContainer> MakeLayoutBox(const std::string& id) {
        auto c = std::make_shared<UltraCanvasContainer>(id);
        ContainerStyle st;
        st.autoShowScrollbars           = false;
        st.forceShowVerticalScrollbar   = false;
        st.forceShowHorizontalScrollbar = false;
        c->SetContainerStyle(st);
        return c;
    }

    std::shared_ptr<UltraCanvasContainer> MakeToolRow(const std::string& id) {
        auto row = MakeLayoutBox(id);
        row->layout.SetFlexRow().SetFlexGap(4)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        row->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                       .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        row->SetBackgroundColor(Color(249, 249, 251, 255));
        return row;
    }

    // Flat command-bar button (Windows 11 style): quiet until hovered.
    // `label` may be empty for an icon-only button; width 0 = auto-size.
    std::shared_ptr<UltraCanvasButton> MakeToolButton(
            const std::string& id, const std::string& label,
            const std::string& iconFile, int width, std::function<void()> onClick) {
        auto b = std::make_shared<UltraCanvasButton>(id, 0, 0, width, 28, label);
        b->SetFontSize(kUiFontSize);
        b->SetCornerRadius(4.0f);
        b->SetColors(Color(255, 255, 255, 255), Color(233, 238, 244, 255));
        b->SetTextColors(Color(40, 40, 44, 255));
        b->SetBorder(1.0f, Color(0, 0, 0, 60));
        if (!iconFile.empty()) {
            b->SetIcon(IconPath(iconFile));
            b->SetIconSize(15, 15);
            b->SetIconPosition(ButtonIconPosition::Left);
            b->SetIconSpacing(label.empty() ? 0 : 5);
            b->SetUseIconAsMask(true);
            b->SetIconMaskColor(Color(55, 55, 60, 255));
        }
        if (onClick) b->SetOnClick(std::move(onClick));
        b->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        return b;
    }

    // Mark / unmark a toggle-style tool button (the Preview switch).
    void StyleToggleButton(UltraCanvasButton* b, bool active) {
        if (!b) return;
        b->SetColors(active ? Color(208, 228, 250, 255) : Color(249, 249, 251, 255),
                     Color(233, 238, 244, 255));
        b->SetBorder(active ? 1.0f : 0.0f,
                     active ? Color(60, 140, 220, 255) : Color(0, 0, 0, 0));
    }

    BreadcrumbStyle MakePathBreadcrumbStyle() {
        BreadcrumbStyle s = BreadcrumbStyle::Arrow();
        s.overflowMode = BreadcrumbOverflowMode::Collapse;
        s.keepFirstItemOnCollapse = true;
        s.minVisibleAfterCollapse = 2;
        s.arrowSize = 8;
        s.itemPaddingHorizontal = 9;
        s.itemPaddingVertical = 4;
        s.fontStyle.fontSize = kUiFontSize;
        return s;
    }

    TreeNodeData MakeFolderNodeData(const std::string& nodeId, const std::string& label,
                                    const std::string& iconFile) {
        TreeNodeData data;
        data.nodeId = nodeId;
        data.text = label;
        data.leftIcon = TreeNodeIcon(IconPath(iconFile), 16, 16);
        return data;
    }

    std::string TabTitleForPath(const std::string& path) {
        const std::string name = fs::path(path).filename().string();
        return name.empty() ? (path.empty() ? "New tab" : path) : name;
    }

    // ===== HISTORY =====

    // Tab captions of the History view, in HistoryTab order.
    constexpr const char* kHistoryTabTitles[] = {"Files", "Folders", "Apps"};

    // Does this entry belong in the History view's Apps tab rather than its
    // Files tab? Program and installer extensions say so outright; on the
    // Unixes a plain executable usually has no extension at all, so the
    // execute bit decides there. FilerFileCategory::Executable is not used on
    // its own because it also covers .so / .dll, which are libraries rather
    // than things the user launches.
    bool IsApplicationEntry(const FilerEntry& e) {
        if (e.isDirectory) return false;
        static const char* const kAppExtensions[] = {
            "exe", "msi", "com", "bat", "cmd", "appimage", "desktop",
            "app", "apk", "deb", "rpm", "flatpakref", "snap", "run"};
        for (const char* ext : kAppExtensions)
            if (e.extension == ext) return true;
        if (!e.extension.empty()) return false;
        std::error_code ec;
        const fs::perms p = fs::status(e.path, ec).permissions();
        if (ec) return false;
        return (p & (fs::perms::owner_exec | fs::perms::group_exec |
                     fs::perms::others_exec)) != fs::perms::none;
    }

    // The favorites list (= Favorites tab) an entry belongs to when pinned.
    FilerFavoriteKind FavoriteKindOf(const FilerEntry& e) {
        return e.isDirectory ? FilerFavoriteKind::Folder
                : IsApplicationEntry(e) ? FilerFavoriteKind::App
                                        : FilerFavoriteKind::File;
    }

    // Is `path` equal to `folder` or somewhere below it?
    bool IsPathInside(const std::string& path, const std::string& folder) {
        if (folder.empty() || path.size() < folder.size()) return false;
        if (path.compare(0, folder.size(), folder) != 0) return false;
        return path.size() == folder.size() || path[folder.size()] == '/' ||
               path[folder.size()] == '\\' ||
               folder.back() == '/' || folder.back() == '\\';
    }

    // "12 items    |    1 item selected (3.4 MB)" for a filer's current
    // display — the status bar text of both the folder and the History views.
    std::string DescribeFilerContent(const UltraCanvasFilerWidget* f) {
        const size_t total = f->GetEntries().size();
        std::string text = std::to_string(total) + (total == 1 ? " item" : " items");
        const std::vector<FilerEntry> sel = f->GetSelectedEntries();
        if (!sel.empty()) {
            uint64_t bytes = 0;
            for (const FilerEntry& e : sel)
                if (!e.isDirectory) bytes += e.size;
            text += "    |    " + std::to_string(sel.size())
                  + (sel.size() == 1 ? " item selected" : " items selected");
            if (bytes > 0) text += " (" + FormatFileSize(bytes) + ")";
        }
        return text;
    }

} // namespace

// ===== INITIALIZATION =====

UltraFilerWindow::~UltraFilerWindow() {
    probeAlive->store(false);   // neutralize queued cross-thread tree updates
    CancelFolderPreviewTimer(); // its callback captures `this`
    StopSubfolderProbeWorker();
    StopCloudStorageDiscovery();
}

bool UltraFilerWindow::Initialize(const std::string& startFolder) {
    WindowConfig config;
    config.title = "UltraFiler " ULTRAFILER_VERSION;
    config.width = 1280;
    config.height = 800;
    config.resizable = true;
    config.type = WindowType::Standard;

    window = CreateWindow(config);
    if (!window || !window->IsCreated()) return false;

    window->layout.SetFlexColumn()
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    window->SetBackgroundColor(Color(249, 249, 251, 255));

    // Esc leaves the History or Favorites view, and otherwise closes the media preview,
    // wherever the keyboard focus sits (the filer usually holds it after the
    // click that selected the file). The filter steps back while another
    // interaction claims the key: an open popup / menu, a filer's inline
    // rename, item drag or compress dialog, and the search field.
    window->InstallEventFilter("ufl-preview-escape",
            [this](const UCEvent& e) -> bool {
        if (e.virtualKey != UCKeys::Escape) return false;
        if (window->GetActivePopupElement()) return false;
        if (window->GetFocusedElement() == searchInput.get()) return false;
        if (historyShown) {
            UltraCanvasFilerWidget* hf = ActiveHistoryFiler();
            if (hf && hf->WantsEscapeKey()) return false;
            SetHistoryVisible(false);
            return true;
        }
        if (favoritesShown) {
            UltraCanvasFilerWidget* ff = ActiveFavoritesFiler();
            if (ff && ff->WantsEscapeKey()) return false;
            SetFavoritesVisible(false);
            return true;
        }
        if (!previewShown) return false;
        if (filer && filer->WantsEscapeKey()) return false;
        // The folder preview's own interactions (a rename, a drag) keep
        // their cancel key too.
        if (previewShowsFolder && folderPreview && folderPreview->WantsEscapeKey())
            return false;
        SetPreviewEnabled(false);
        return true;
    }, { UCEventType::KeyDown });

    // A letter typed anywhere in the window — outside a text field — walks
    // the visible folder listing Explorer-style: the first entry starting
    // with it, then, on the same letter again, the next such entry (the
    // filer's type-ahead). This routes the key to the visible filer while
    // the keyboard focus sits on some other control; the filer handles its
    // own keys when it is focused itself.
    window->InstallEventFilter("ufl-typeahead",
            [this](const UCEvent& e) -> bool {
        if (e.ctrl || e.alt || e.meta) return false;
        if (e.character <= 32 ||
            static_cast<unsigned char>(e.character) >= 127) return false;
        if (window->GetActivePopupElement()) return false;
        UltraCanvasUIElement* focused = window->GetFocusedElement();
        // Text entry keeps its characters (the search field, a rename
        // editor, a dialog's name field, ...), and a focused filer — the
        // folder display, the folder preview, a History page — handles its
        // own type-ahead in its OnEvent.
        if (dynamic_cast<UltraCanvasTextInput*>(focused)) return false;
        if (dynamic_cast<UltraCanvasFilerWidget*>(focused)) return false;
        UltraCanvasFilerWidget* f = VisibleFiler();
        if (!f) return false;
        if (f->WantsEscapeKey()) return false;  // rename / drag / dialog run
        return f->SelectNextEntryStartingWith(e.character);
    }, { UCEventType::KeyDown });

    preview = CreateMediaViewer("ufl-preview", 0, 0, 0, 0);
    // The pane is added / removed as the selection changes; the viewer must
    // not steal the keyboard focus from the filer on every appearance.
    preview->SetGrabFocusOnAttach(false);
    // The filer provides the navigation; the preview shows only the media
    // (no breadcrumb / toolbar rows above the image).
    preview->SetTopBarsVisible(false);
    // Picking a backdrop colour from the strip under a transparent image is a
    // setting like any other: it is kept, so the next preview opens with it.
    preview->onTransparentBackgroundChanged =
            [this](TransparentImageBackground mode, const Color& color) {
        settings.previewCheckeredBackground =
                (mode == TransparentImageBackground::Checkered);
        settings.previewTransparentColor = color;
        settings.Save();
    };

    // Folder preview: clicking a folder in the file display shows that
    // folder's content in the same detail pane a file shows its media in —
    // a second filer widget in small-thumbnail mode. It is for looking, so
    // the hover icon menu stays off (the context menu still offers
    // everything); activating a file in it opens that file with the OS
    // default application, and a double-clicked subfolder is entered right
    // in the pane.
    folderPreview = CreateFilerWidget("ufl-folder-preview", 0, 0, 0, 0);
    // Created before settings.Load(); ApplySettings() right after it applies
    // the Display > Home folder mode here.
    FilerStyle folderPreviewStyle = folderPreview->GetStyle();
    folderPreviewStyle.fontSize = kUiFontSize;
    folderPreviewStyle.smallFontSize = kUiFontSize;
    folderPreviewStyle.folderIconScale = 0.7f;
    folderPreview->SetStyle(folderPreviewStyle);
    folderPreview->SetViewType(FilerViewType::ThumbnailsSmall);
    folderPreview->SetHoverIconMenuEnabled(false);
    // The pane is narrow; prefetching every subfolder of a merely previewed
    // folder is disk work the user rarely follows up on.
    folderPreview->SetFolderPrefetchEnabled(false);
    folderPreview->SetActivateOpensWithDefaultApp(true);
    // Work done through the pane's context menu (a paste, a delete, ...) is
    // work done in that folder, exactly as in the main folder display.
    folderPreview->onFolderModified = [this](const std::string& folder) {
        RecordFolderInHistory(folder);
    };
    folderPreview->onError = [this](const std::string& message) {
        if (statusLabel) statusLabel->SetText("Error: " + message);
    };

    // Persisted settings (transparent-image backdrop of the preview, ...) and
    // the recently used files / folders / applications behind the clock button.
    settings.Load();
    ApplySettings();
    history.Load();
    favorites.Load();
    folderViews.Load();

    BuildTabbedContainer();

    // The tab strip is the window's top bar: the tabs name the folders, the
    // toolbars below them act on whichever one is selected.
    window->AddChild(tabbedContainer);
    window->AddChild(BuildNavigationRow());
    window->AddChild(BuildCommandBar());

    BuildFolderTree();
    BuildSplitLayout();
    BuildHistoryView();
    BuildFavoritesView();

    // Status bar under the split.
    statusLabel = std::make_shared<UltraCanvasLabel>("ufl-status", 0, 0, 0, 24);
    statusLabel->SetFontSize(kUiFontSize);
    statusLabel->SetTextColor(Color(70, 70, 76, 255));
    statusLabel->SetBackgroundColor(Color(243, 243, 246, 255));
    statusLabel->SetPadding(4, 10, 4, 10);
    statusLabel->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
    statusLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    window->AddChild(statusLabel);

    std::string start = startFolder;
    std::error_code ec;
    if (start.empty() || !fs::is_directory(start, ec)) start = UserHomeDir();
    if (start.empty()) start = fs::current_path(ec).string();

    AddNewTab(start, true);

    return true;
}

void UltraFilerWindow::Show() {
    if (window) window->Show();
}

// ===== EXTRAS (context menu: Print / Share / Attributes / Access) =====

void UltraFilerWindow::HandlePrint(const std::vector<FilerEntry>& targets) {
    // Plain text goes through the OS print dialog; other file kinds have no
    // renderer yet and are skipped with a note.
    constexpr uint64_t kMaxPrintBytes = 4ull * 1024 * 1024;
    constexpr size_t   kMaxPrintJobs  = 5;   // one dialog per file

    std::vector<FilerEntry> printable;
    size_t skipped = 0;
    for (const FilerEntry& e : targets) {
        if (e.isDirectory) continue;
        if (e.category == FilerFileCategory::Text) printable.push_back(e);
        else ++skipped;
    }
    if (printable.empty()) {
        if (statusLabel)
            statusLabel->SetText("Print: no text files selected - only text "
                                 "files can be printed yet.");
        return;
    }

    size_t printed = 0;
    for (size_t i = 0; i < printable.size() && i < kMaxPrintJobs; ++i) {
        const FilerEntry& e = printable[i];
        if (e.size > kMaxPrintBytes) {
            if (statusLabel)
                statusLabel->SetText("Print: \"" + e.name + "\" is too large ("
                                     + FormatFileSize(e.size) + ").");
            continue;
        }
        std::ifstream in(fs::path(e.path), std::ios::binary);
        if (!in) {
            if (statusLabel)
                statusLabel->SetText("Print: cannot read \"" + e.name + "\".");
            continue;
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();
        // A cancelled dialog cancels the remaining queue too.
        if (!UltraCanvasNativeDialogs::ShowPrintDialog(e.name, buffer.str(),
                                                       window.get()))
            break;
        ++printed;
    }

    if (printed > 0 && statusLabel) {
        std::string text = printed == 1
                ? "Sent \"" + printable.front().name + "\" to the printer."
                : "Sent " + std::to_string(printed) + " files to the printer.";
        if (printable.size() > kMaxPrintJobs)
            text += " First " + std::to_string(kMaxPrintJobs) + " of "
                  + std::to_string(printable.size()) + " only.";
        if (skipped > 0)
            text += " " + std::to_string(skipped) + " non-text item(s) skipped.";
        statusLabel->SetText(text);
    }
}

void UltraFilerWindow::HandleShare(const std::vector<FilerEntry>& targets) {
    std::vector<std::string> paths;
    for (const FilerEntry& e : targets)
        if (!e.isDirectory) paths.push_back(e.path);
    if (paths.empty()) {
        if (statusLabel)
            statusLabel->SetText("Share: select files - folders cannot be "
                                 "sent as e-mail attachments.");
        return;
    }
    std::string error;
    if (!UltraFilerShare::ShareByEmail(paths, error))
        UltraCanvasAlert::Error(error, "Share", nullptr, window.get());
    else if (statusLabel)
        statusLabel->SetText("Share: handed " + std::to_string(paths.size())
                             + (paths.size() == 1 ? " file" : " files")
                             + " to the e-mail composer.");
}

void UltraFilerWindow::HandleAttributes(const std::vector<FilerEntry>& targets) {
    if (targets.empty()) return;
    UltraFilerPropertiesDialogs::ShowAttributes(window.get(), targets);
}

void UltraFilerWindow::HandleAccess(const std::vector<FilerEntry>& targets) {
    if (targets.empty()) return;
    UltraFilerPropertiesDialogs::ShowAccess(window.get(), targets,
            [this]() { RefreshVisibleListing(); });
}

void UltraFilerWindow::RefreshVisibleListing() {
    if (historyShown) RefreshHistoryTabs();
    else if (favoritesShown) RefreshFavoritesTabs();
    else if (filer) filer->Refresh();
}

// ===== SETTINGS =====

void UltraFilerWindow::ApplySettings() {
    if (preview) {
        preview->SetTransparentBackground(settings.previewCheckeredBackground
                ? TransparentImageBackground::Checkered
                : TransparentImageBackground::SolidColor);
        preview->SetTransparentColor(settings.previewTransparentColor);
        // Display > PDF Inventory: the width of the page thumbnails beside a
        // shown PDF, either fixed or a share of the preview pane's width.
        if (settings.pdfThumbnailAbsoluteWidth)
            preview->SetPDFThumbnailWidth(settings.pdfThumbnailWidth);
        else
            preview->SetPDFThumbnailWidthFraction(
                    settings.pdfThumbnailWidthPercent / 100.0f);
    }
    // Handling > Drag & Drop: every tab's folder display, so the choice holds
    // for tabs that were already open when it changed. The folder preview
    // accepts drops too, so it follows the same setting.
    for (auto& state : tabStates)
        if (state->filer) state->filer->SetDropOnFolderCopies(settings.dropOnFolderCopies);
    if (folderPreview)
        folderPreview->SetDropOnFolderCopies(settings.dropOnFolderCopies);
    // Display > Home folder: curate the home folder's display - every tab and
    // the folder preview - or show it whole, and keep the tree's Home entry in
    // step. The widget ignores a SetCuratedHomeFolder that changes nothing, so
    // re-applying on every unrelated settings change costs no rescans.
    const bool curatedHome = settings.homeShowPredefinedOnly;
    const std::string home = UserHomeDir();
    const std::string curatedPath = curatedHome ? home : std::string();
    std::vector<std::string> curatedFolders =
            curatedHome ? HomeCurationPaths() : std::vector<std::string>();
    for (auto& state : tabStates)
        if (state->filer)
            state->filer->SetCuratedHomeFolder(curatedPath, curatedFolders);
    if (folderPreview)
        folderPreview->SetCuratedHomeFolder(curatedPath, curatedFolders);
    if (curatedHomeActive.exchange(curatedHome) != curatedHome)
        RefreshHomeTreeChildren();
    // The tree is built after the settings are loaded, so this is a no-op on
    // the first call and does the work on every later one (BuildFolderTree
    // applies the colours itself).
    ApplyTreeColors();
}

// Re-derives the tree's Home children after the Display > Home folder setting
// flips. The loaded-state of Home AND of everything below it is forgotten:
// the child nodes are recreated, and a stale "already loaded" entry for one
// of them would suppress its placeholder and leave it inexpandable.
void UltraFilerWindow::RefreshHomeTreeChildren() {
    if (!folderTree) return;
    const std::string home = UserHomeDir();
    TreeNode* node = folderTree->FindNode(home);
    if (!node) return;
    const bool wasExpanded = node->IsExpanded();
    while (!node->children.empty())
        folderTree->RemoveNode(node->children.front()->data.nodeId);
    for (auto it = treeChildrenLoaded.begin(); it != treeChildrenLoaded.end();) {
        // Home itself, or a path below it ("/home/me/x", not "/home/mexico").
        const std::string& key = *it;
        const bool underHome = key.size() > home.size() &&
                key.compare(0, home.size(), home) == 0 &&
                (key[home.size()] == '/' || key[home.size()] == '\\');
        if (key == home || underHome) it = treeChildrenLoaded.erase(it);
        else ++it;
    }
    if (wasExpanded) {
        // Repopulate right away: removing the last child demoted the node to
        // a collapsed leaf, so load the children first, then expand through
        // the notifying path (its callback early-returns, already loaded).
        EnsureTreeChildren(node);
        folderTree->ExpandNode(node);
    } else {
        QueueSubfolderProbe(home);
    }
    folderTree->RequestRedraw();
}

void UltraFilerWindow::OpenSettingsDialog() {
    UltraFilerSettingsDialog::Show(window.get(), &settings,
            [this]() { ApplySettings(); },
            [this]() {   // Clear History
        history.ClearAll();
        if (historyShown) {
            RefreshHistoryTabs();
            UpdateStatusBar();
        }
    },
            [this]() {   // Clear Favorites
        favorites.ClearAll();
        RefreshPinnedTreeNodes();
        if (favoritesShown) {
            RefreshFavoritesTabs();
            UpdateStatusBar();
        }
    },
            [this]() {   // Clear Folder views
        folderViews.ClearAll();
    });
}

// ===== EXTRAS EXTENSION (filer context menus: Open prompt + Pin / Unpin) =====

void UltraFilerWindow::OpenSystemPrompt() {
    // The prompt opens in the folder the active tab is showing, so the shell
    // starts where the user is looking.
    std::string folder = filer ? filer->GetPath() : std::string();
    if (folder.empty()) folder = UserHomeDir();

    std::string error;
    if (!UltraFilerPrompt::Launch(settings.promptApplication, folder, error))
        UltraCanvasAlert::Error(error, "Open prompt", nullptr, window.get());
}

UltraCanvasFilerWidget* UltraFilerWindow::VisibleFiler() const {
    if (favoritesShown) return ActiveFavoritesFiler();
    if (historyShown) return ActiveHistoryFiler();
    return filer.get();
}

std::vector<FilerEntry> UltraFilerWindow::PinTargets() const {
    UltraCanvasFilerWidget* f = VisibleFiler();
    if (!f) return {};
    std::vector<FilerEntry> sel = f->GetSelectedEntries();
    if (!sel.empty()) return sel;
    // Nothing selected: in the browsing view the shown folder itself is the
    // content, so that is what gets pinned.
    if (!historyShown && !favoritesShown) {
        const std::string path = f->GetPath();
        std::error_code ec;
        if (!path.empty() && fs::is_directory(path, ec) && !ec) {
            FilerEntry folder;
            folder.path = path;
            folder.name = fs::path(path).filename().string();
            folder.isDirectory = true;
            return {folder};
        }
    }
    return {};
}

std::vector<MenuItemData> UltraFilerWindow::BuildExtrasMenuItems() {
    const std::vector<FilerEntry> targets = PinTargets();
    const bool allFolders = !targets.empty() &&
            std::all_of(targets.begin(), targets.end(),
                        [](const FilerEntry& e) { return e.isDirectory; });

    // The flags show where the selection is pinned right now: checked when
    // every target is pinned there. Pin stays enabled while something is
    // still unpinned, Unpin while something is pinned - so the pair also
    // reads as "partly pinned" when a flag is off but Unpin is enabled.
    bool allInFavorites = !targets.empty(), anyInFavorites = false;
    for (const FilerEntry& e : targets) {
        const bool pinned = favorites.IsPinned(FavoriteKindOf(e), e.path);
        allInFavorites = allInFavorites && pinned;
        anyInFavorites = anyInFavorites || pinned;
    }
    bool allInTree = allFolders, anyInTree = false;
    for (const FilerEntry& e : targets) {
        if (!e.isDirectory) continue;
        const bool pinned = favorites.IsPinned(FilerFavoriteKind::Tree, e.path);
        allInTree = allInTree && pinned;
        anyInTree = anyInTree || pinned;
    }

    // Only a folder can live in the folder tree.
    MenuItemData pinTree = MenuItemData::Checkbox("To Treeview", allInTree,
            [this](bool) { PinTargetsToTree(); });
    pinTree.enabled = allFolders && !allInTree;

    MenuItemData pinFavorites = MenuItemData::Checkbox("To Favorites", allInFavorites,
            [this](bool) { PinTargetsToFavorites(); });
    pinFavorites.enabled = !targets.empty() && !allInFavorites;

    MenuItemData unpinTree = MenuItemData::Checkbox("To Treeview", allInTree,
            [this](bool) { UnpinTargetsFromTree(); });
    unpinTree.enabled = anyInTree;

    MenuItemData unpinFavorites = MenuItemData::Checkbox("To Favorites", allInFavorites,
            [this](bool) { UnpinTargetsFromFavorites(); });
    unpinFavorites.enabled = anyInFavorites;

    return {
            MenuItemData::Action("Open prompt", [this]() { OpenSystemPrompt(); }),
            MenuItemData::Separator(),
            MenuItemData::Submenu("Pin", {pinTree, pinFavorites}),
            MenuItemData::Submenu("Unpin", {unpinTree, unpinFavorites}),
    };
}

void UltraFilerWindow::PinTargetsToFavorites() {
    bool changed = false;
    for (const FilerEntry& e : PinTargets())
        changed |= favorites.Pin(FavoriteKindOf(e), e.path);
    if (changed && favoritesShown) {
        RefreshFavoritesTabs();
        UpdateStatusBar();
    }
}

void UltraFilerWindow::PinTargetsToTree() {
    bool changed = false;
    for (const FilerEntry& e : PinTargets()) {
        if (!e.isDirectory) continue;
        changed |= favorites.Pin(FilerFavoriteKind::Tree, e.path);
    }
    if (!changed) return;
    RefreshPinnedTreeNodes();
    RevealPinnedTreeSection();
}

void UltraFilerWindow::UnpinTargetsFromFavorites() {
    bool changed = false;
    for (const FilerEntry& e : PinTargets())
        changed |= favorites.Unpin(FavoriteKindOf(e), e.path);
    if (changed && favoritesShown) {
        RefreshFavoritesTabs();
        UpdateStatusBar();
    }
}

void UltraFilerWindow::UnpinTargetsFromTree() {
    bool changed = false;
    for (const FilerEntry& e : PinTargets()) {
        if (!e.isDirectory) continue;
        changed |= favorites.Unpin(FilerFavoriteKind::Tree, e.path);
    }
    if (changed) RefreshPinnedTreeNodes();
}

// ===== NAVIGATION ROW ("+" / Back / Forward / Up / Refresh + breadcrumb + settings) =====

std::shared_ptr<UltraCanvasContainer> UltraFilerWindow::BuildNavigationRow() {
    auto row = MakeToolRow("ufl-nav-row");
    row->SetPadding(6, 8, 2, 8);

    // No new-tab button here: the tab strip above carries its own "+" at the
    // end of the tab list.
    backButton = MakeToolButton("ufl-back", "", "arrow-left.svg", 30,
                                [this]() { NavigateBack(); });
    forwardButton = MakeToolButton("ufl-forward", "", "arrow-right.svg", 30,
                                   [this]() { NavigateForward(); });
    upButton = MakeToolButton("ufl-up", "", "arrow-up.svg", 30,
                              [this]() { NavigateUp(); });
    // Refresh re-reads whatever is on screen: the History or Favorites lists
    // while they are shown (dropping what has meanwhile been deleted), else
    // the folder.
    auto refresh = MakeToolButton("ufl-refresh", "", "reload.svg", 30,
            [this]() {
        if (historyShown) RefreshHistoryTabs();
        else if (favoritesShown) RefreshFavoritesTabs();
        else if (filer) filer->Refresh();
    });
    row->AddChild(backButton);
    row->AddChild(forwardButton);
    row->AddChild(upButton);
    row->AddChild(refresh);

    // The clock: shows the History view (Files / Folders / Apps) in place of
    // the folder tree and folder display, and hides it again.
    historyButton = MakeToolButton("ufl-history", "", "clock-five.svg", 30,
            [this]() { SetHistoryVisible(!historyShown); });
    historyButton->SetTooltip("History");
    StyleToggleButton(historyButton.get(), historyShown);
    row->AddChild(historyButton);

    // The heart: shows the Favorites view (pinned Files / Folders / Apps) in
    // place of the folder tree and folder display, and hides it again.
    favoritesButton = MakeToolButton("ufl-favorites", "", "rating-heart-on.svg", 30,
            [this]() { SetFavoritesVisible(!favoritesShown); });
    favoritesButton->SetTooltip("Favorites");
    StyleToggleButton(favoritesButton.get(), favoritesShown);
    row->AddChild(favoritesButton);

    breadcrumb = std::make_shared<UltraCanvasBreadcrumb>("ufl-breadcrumb", 0, 0, 0, 28);
    breadcrumb->SetStyle(MakePathBreadcrumbStyle());
    breadcrumb->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                          .SetAlignSelf(CSSLayout::AlignSelf::Center);
    row->AddChild(breadcrumb);

    // The gear at the far right opens the settings window.
    auto settingsButton = MakeToolButton("ufl-settings", "", "settings.svg", 30,
            [this]() { OpenSettingsDialog(); });
    settingsButton->SetTooltip("Settings");
    row->AddChild(settingsButton);

    return row;
}

// ===== SEARCH =====

void UltraFilerWindow::ApplyLiveSearchFilter(const std::string& text) {
    // The filter narrows the folder display, so typing leaves the History /
    // Favorites views.
    ShowBrowsingView();
    if (!filer) return;
    // Typing filters the folder itself: a recursive-result display from an
    // earlier Enter ends first (SetPath leaves file-list mode and drops the
    // old name filter with it).
    if (filer->IsShowingFileList()) {
        if (FilerTabState* tab = ActiveTabState()) tab->searchQuery.clear();
        filer->SetOpenPathMenuItemVisible(false);
        filer->SetPath(filer->GetPath());
    }
    filer->SetNameFilter(text);
}

void UltraFilerWindow::ResetSearchState() {
    // Programmatic SetText fires no onTextChanged, so clearing the field
    // does not re-enter the filter path.
    if (searchInput) searchInput->SetText("");
    if (FilerTabState* tab = ActiveTabState()) tab->searchQuery.clear();
    if (!filer) return;
    if (filer->IsShowingFileList()) {
        filer->SetOpenPathMenuItemVisible(false);
        filer->SetPath(filer->GetPath());   // also drops the name filter
    } else {
        filer->SetNameFilter("");
    }
}

// ===== NEW ENTRY (the command bar's "New folder ▾" split button) =====

void UltraFilerWindow::CreateNewFolderCommand() {
    ShowBrowsingView();
    ResetSearchState();
    if (filer) filer->CreateNewFolder();
}

void UltraFilerWindow::CreateNewDocumentCommand(const FilerNewDocumentType& type) {
    ShowBrowsingView();
    ResetSearchState();
    if (filer) filer->CreateNewDocument(type);
}

void UltraFilerWindow::ShowNewEntryMenu() {
    if (!window || !newButton) return;
    MenuStyle style = MenuStyle::Default();
    style.font.fontSize = kUiFontSize;
    newEntryMenu = std::make_shared<UltraCanvasMenu>("ufl-new-menu", 0, 0, 160, 0);
    newEntryMenu->SetMenuType(MenuType::PopupMenu);
    newEntryMenu->SetStyle(style);
    // Mirror of the filer context menu's "New >" submenu: the folder first,
    // set apart from the document kinds.
    newEntryMenu->AddItem(MenuItemData::ActionWithShortcut(
            "Folder", "Ctrl+F", [this]() { CreateNewFolderCommand(); }));
    if (filer) {
        newEntryMenu->AddItem(MenuItemData::Separator());
        for (const FilerNewDocumentType& t : filer->GetNewDocumentTypes()) {
            FilerNewDocumentType copy = t;
            newEntryMenu->AddItem(MenuItemData::Action(
                    t.label, [this, copy]() { CreateNewDocumentCommand(copy); }));
        }
    }
    newEntryMenu->OpenMenu(
            Point2Di(newButton->GetXInWindow(),
                     newButton->GetYInWindow() + (int)newButton->GetHeight() + 1),
            *window, PopupElementSettings());
}

void UltraFilerWindow::RunSearch(const std::string& query) {
    // The results are shown in the folder display, so a search leaves the
    // History / Favorites views.
    ShowBrowsingView();
    if (!filer) return;

    if (query.empty()) {
        // Back to the normal folder display (SetPath leaves file-list mode).
        if (filer->IsShowingFileList()) filer->SetPath(filer->GetPath());
        return;
    }

    const std::string root = filer->GetPath();
    if (root.empty()) return;

    // The recursive results replace the as-you-type folder filter — they are
    // an explicit file list, not a narrowed folder listing.
    filer->SetNameFilter("");

    std::string needle = query;
    std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);

    // Bounded so a match-everything query on a huge tree stays responsive.
    constexpr size_t kMaxResults = 1000;
    std::vector<std::string> results;
    std::error_code ec;
    fs::recursive_directory_iterator it(
            root, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end && results.size() < kMaxResults; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (IsHiddenFileSystemEntry(it->path())) {
            // Consistent with the folder tree: hidden folders are not entered.
            std::error_code dec;
            if (it->is_directory(dec) && !dec) it.disable_recursion_pending();
            continue;
        }
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower.find(needle) != std::string::npos)
            results.push_back(it->path().string());
    }

    if (FilerTabState* tab = ActiveTabState()) tab->searchQuery = query;
    filer->SetOpenPathMenuItemVisible(true, "Open path (in new tab)");
    filer->ShowFileList(results);
    if (statusLabel) {
        std::string text = std::to_string(results.size())
                + (results.size() == 1 ? " result for \"" : " results for \"")
                + query + "\"";
        if (results.size() >= kMaxResults) text += " (first 1000 shown)";
        statusLabel->SetText(text);
    }
}

// ===== COMMAND BAR (New / clipboard / rename / delete / search / sort / view / preview) =====

std::shared_ptr<UltraCanvasContainer> UltraFilerWindow::BuildCommandBar() {
    auto row = MakeToolRow("ufl-command-bar");
    row->SetPadding(2, 8, 6, 8);
    row->SetBorderBottom(1, Color(225, 225, 230, 255));

    // The file commands work on the folder display and its selection, so each
    // of them leaves the History / Favorites views first - the change they
    // make has to be visible (an inline rename editor especially).
    {
        // "New folder ▾" split button (replaces the New folder / New file
        // pair): the primary section is the folder — the same action as the
        // folder display's "New > Folder" (Ctrl+F) — and the arrow opens a
        // menu with the same entries as the context menu's "New >" submenu.
        // Either way the search ends first (ResetSearchState inside the
        // commands): the fresh entry has to be visible and its rename editor
        // reachable, which a filtered listing or a result display cannot
        // guarantee.
        newButton = MakeToolButton("ufl-new", "New folder", "add-folder.svg",
                138, [this]() { CreateNewFolderCommand(); });
        newButton->SetSplitEnabled(true);
        newButton->SetSplitRatio(0.8f);
        newButton->SetSplitSecondaryText("▾");
        // The same quiet flat look as the primary section.
        newButton->SetSplitColors(Color(255, 255, 255, 255),
                                  Color(55, 55, 60, 255),
                                  Color(233, 238, 244, 255),
                                  Color(208, 228, 250, 255));
        newButton->SetSplitSeparator(true, Color(0, 0, 0, 60), 1.0f);
        newButton->onSecondaryClick = [this]() { ShowNewEntryMenu(); };
        newButton->SetTooltip("New folder (Ctrl+F) — the arrow lists more kinds");
        row->AddChild(newButton);
    }

    auto sep1 = std::make_shared<UltraCanvasLabel>("ufl-sep1", 0, 0, 9, 24);
    sep1->SetText("|");
    sep1->SetFontSize(kUiFontSize);
    sep1->SetTextColor(Color(200, 200, 206, 255));
    row->AddChild(sep1);

    row->AddChild(MakeToolButton("ufl-cut", "", "scissors.svg", 30,
            [this]() { ShowBrowsingView(); if (filer) filer->CutSelection(); }));
    row->AddChild(MakeToolButton("ufl-copy", "", "copy.svg", 30,
            [this]() { ShowBrowsingView(); if (filer) filer->CopySelection(); }));
    row->AddChild(MakeToolButton("ufl-paste", "", "clipboard-list.svg", 30,
            [this]() { ShowBrowsingView(); if (filer) filer->Paste(); }));
    row->AddChild(MakeToolButton("ufl-rename", "", "edit.svg", 30,
            [this]() {
        ShowBrowsingView();
        if (!filer) return;
        auto sel = filer->GetSelectedEntries();
        if (sel.empty()) return;
        const auto& entries = filer->GetEntries();
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].path == sel.front().path) { filer->StartRename(i); break; }
        }
    }));
    row->AddChild(MakeToolButton("ufl-delete", "", "delete.svg", 30,
            [this]() {
        ShowBrowsingView();
        if (!filer) return;
        auto sel = filer->GetSelectedEntries();
        if (sel.empty()) return;
        const std::string message = sel.size() == 1
                ? "Delete \"" + sel.front().name + "\"?"
                : "Delete " + std::to_string(sel.size()) + " items?";
        UltraCanvasAlert::Confirm(message, "Delete",
                [this](bool confirmed) { if (confirmed && filer) filer->DeleteSelection(); },
                window.get());
    }));

    auto sep2 = std::make_shared<UltraCanvasLabel>("ufl-sep2", 0, 0, 9, 24);
    sep2->SetText("|");
    sep2->SetFontSize(kUiFontSize);
    sep2->SetTextColor(Color(200, 200, 206, 255));
    row->AddChild(sep2);

    // Search field. Typing filters the shown folder as-you-type (the
    // filer's name filter); Enter runs the recursive search under the
    // current folder — as does the "Search in sub folders" button the
    // filer centers when the filter matches nothing. An empty field
    // returns to the normal folder display.
    searchInput = CreateTextInput("ufl-search", 0, 0, 200, 26);
    searchInput->SetFontSize(kUiFontSize);
    searchInput->SetPlaceholder("Search");
    searchInput->onTextChanged = [this](const std::string& text) {
        ApplyLiveSearchFilter(text);
    };
    searchInput->onEnterPressed = [this](const std::string& text) {
        RunSearch(text);
        return true;
    };
    // May give way (shrink) when the bar gets tight - the dropdowns cannot.
    searchInput->layoutItem.SetFlexGrow(0).SetFlexShrink(1)
                           .SetAlignSelf(CSSLayout::AlignSelf::Center);
    row->AddChild(searchInput);

    // Sort field + direction. The dropdown mirrors FilerSortField order.
    auto sortLbl = std::make_shared<UltraCanvasLabel>("ufl-sort-lbl", 0, 0, 42, 24);
    sortLbl->SetText("Sort");
    sortLbl->SetFontSize(kUiFontSize);
    sortLbl->SetAlignment(TextAlignment::Right, VerticalAlignment::Middle);
    row->AddChild(sortLbl);

    sortDropdown = CreateDropdown("ufl-sort", 0, 0, 128, 26);
    ApplyDropdownFontSize(sortDropdown.get());
    sortDropdown->AddItem("Name");
    sortDropdown->AddItem("Size");
    sortDropdown->AddItem("Type");
    sortDropdown->AddItem("Date modified");
    sortDropdown->AddItem("Date created");
    sortDropdown->SetSelectedIndex(0, false);
    sortDropdown->onSelectionChanged = [this](int index, const DropdownItem&) {
        if (syncingControls || !filer) return;
        static const FilerSortField fields[] = {
            FilerSortField::Name, FilerSortField::Size, FilerSortField::Type,
            FilerSortField::ModifiedDate, FilerSortField::CreatedDate};
        if (index >= 0 && index < 5) filer->SetSortField(fields[index]);
    };
    sortDropdown->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    row->AddChild(sortDropdown);

    // Sort direction. The icon IS the state, so it is repainted from the
    // filer's own direction (UpdateSortOrderButton) rather than toggled here -
    // the direction also changes from the context menu and from a folder's
    // stored view, and the arrow has to follow all of them.
    sortOrderButton = MakeToolButton("ufl-sort-order", "", "sort-up.svg", 30,
            [this]() {
        if (filer) filer->SetSortAscending(!filer->IsSortAscending());
    });
    row->AddChild(sortOrderButton);
    UpdateSortOrderButton();

    // View type; defaults to medium thumbnails like the Explorer screenshot.
    auto viewLbl = std::make_shared<UltraCanvasLabel>("ufl-view-lbl", 0, 0, 44, 24);
    viewLbl->SetText("View");
    viewLbl->SetFontSize(kUiFontSize);
    viewLbl->SetAlignment(TextAlignment::Right, VerticalAlignment::Middle);
    row->AddChild(viewLbl);

    viewDropdown = CreateDropdown("ufl-view", 0, 0, 130, 26);
    ApplyDropdownFontSize(viewDropdown.get());
    viewDropdown->AddItem("Details");
    viewDropdown->AddItem("List");
    viewDropdown->AddItem("Small icons");
    viewDropdown->AddItem("Medium icons");
    viewDropdown->AddItem("Large icons");
    viewDropdown->AddItem("Extra large icons");
    viewDropdown->AddItem("Size bars");
    viewDropdown->AddItem("Treemap");
    viewDropdown->SetSelectedIndex(3, false);
    viewDropdown->onSelectionChanged = [this](int index, const DropdownItem&) {
        if (syncingControls || !filer) return;
        static const FilerViewType types[] = {
            FilerViewType::Details, FilerViewType::List,
            FilerViewType::ThumbnailsSmall, FilerViewType::ThumbnailsMedium,
            FilerViewType::ThumbnailsBig, FilerViewType::ThumbnailsMaximized,
            FilerViewType::BarSize, FilerViewType::TreeMap};
        if (index >= 0 && index < 8) filer->SetViewType(types[index]);
    };
    viewDropdown->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    row->AddChild(viewDropdown);

    // Everything after this stretch sits at the right edge of the bar.
    auto stretch = MakeLayoutBox("ufl-cmd-stretch");
    stretch->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    row->AddChild(stretch);

    // How the preview treats video files: full playback with sound (default),
    // a 5 second muted clip (album hover-preview style) or a still frame.
    auto videoLbl = std::make_shared<UltraCanvasLabel>("ufl-video-lbl", 0, 0, 44, 24);
    videoLbl->SetText("Video");
    videoLbl->SetFontSize(kUiFontSize);
    videoLbl->SetAlignment(TextAlignment::Right, VerticalAlignment::Middle);
    row->AddChild(videoLbl);

    videoModeDropdown = CreateDropdown("ufl-video-mode", 0, 0, 104, 26);
    ApplyDropdownFontSize(videoModeDropdown.get());
    videoModeDropdown->AddItem("Autoplay");
    videoModeDropdown->AddItem("5 s clip");
    videoModeDropdown->AddItem("Still image");
    videoModeDropdown->SetSelectedIndex(0, false);   // autostart is the default
    videoModeDropdown->onSelectionChanged = [this](int index, const DropdownItem&) {
        if (!preview) return;
        static const VideoPreviewMode modes[] = {
            VideoPreviewMode::Autoplay, VideoPreviewMode::PreviewClip,
            VideoPreviewMode::Still};
        if (index >= 0 && index < 3) preview->SetVideoPreviewMode(modes[index]);
    };
    videoModeDropdown->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    row->AddChild(videoModeDropdown);

    previewButton = MakeToolButton("ufl-preview-toggle", "Preview", "split-screen.svg", 0,
            [this]() { SetPreviewEnabled(!previewEnabled); });
    StyleToggleButton(previewButton.get(), previewEnabled);
    row->AddChild(previewButton);

    return row;
}

// ===== FOLDER TREE =====

void UltraFilerWindow::BuildFolderTree() {
    folderTree = std::make_shared<UltraCanvasTreeView>("ufl-tree");
    folderTree->SetFontSize(kUiFontSize);
    folderTree->SetRowHeight(24);
    folderTree->SetSelectionMode(TreeSelectionMode::Single);
    folderTree->SetLineStyle(TreeLineStyle::NoLine);
    folderTree->SetBackgroundColor(Color(249, 249, 251, 255));

    // Slim the vertical scrollbar to half its default width (6px) so it reads
    // as a thin sidebar accent rather than a full control.
    ScrollbarStyle treeScrollbarStyle = folderTree->GetVerticalScrollbarStyle();
    treeScrollbarStyle.trackSize = 6;
    folderTree->SetVerticalScrollbarStyle(treeScrollbarStyle);

    // A hidden root carries the two top-level sections, so "Pinned" can sit
    // ABOVE "Computer" instead of inside it. Neither the root nor the section
    // headers are folders - never let EnsureTreeChildren scan them as paths.
    folderTree->SetRootVisible(false);
    folderTree->SetRootNode(MakeFolderNodeData(kTreeRootNodeId, "", ""));
    treeChildrenLoaded.insert(kTreeRootNodeId);

    // "Pinned" first: bookmark entries for the folders pinned through
    // Pin > Treeview. Its children are managed by RefreshPinnedTreeNodes,
    // never by a folder scan, and the whole section is hidden while nothing
    // is pinned.
    folderTree->AddNode(kTreeRootNodeId,
            MakeFolderNodeData(kPinnedNodeId, "Pinned", "rating-heart-on.svg"));
    treeChildrenLoaded.insert(kPinnedNodeId);

    TreeNode* root = folderTree->AddNode(kTreeRootNodeId,
            MakeFolderNodeData("ufl-computer", "Computer", "computer.png"));
    treeChildrenLoaded.insert("ufl-computer");

    RefreshPinnedTreeNodes();

    const std::string home = UserHomeDir();
    if (!home.empty()) {
        AddTreeFolderNode("ufl-computer", home, "Home", "home-icon.png");
    }

    // "Cloud Storage" sits between Home and the drives: OneDrive, Google Drive,
    // Dropbox and iCloud Drive collected into one section instead of scattered
    // through the profile (and, for a Google Drive that mounted as a virtual
    // drive letter, instead of hiding among the real drives). The header is
    // created here so the section keeps its place in the order; which folders
    // exist is answered off the UI thread (QueueCloudStorageDiscovery), and,
    // exactly like the Pinned section, an empty one stays hidden.
    folderTree->AddNode("ufl-computer",
            MakeFolderNodeData(kCloudNodeId, "Cloud Storage", "cloud.svg"));
    treeChildrenLoaded.insert(kCloudNodeId);
    if (TreeNode* cloud = folderTree->FindNode(kCloudNodeId))
        cloud->data.visible = false;
    QueueCloudStorageDiscovery();

#ifdef _WIN32
    // ListDriveRoots() reads the mount table in one call. Probing every letter
    // with is_directory() instead spins up empty optical drives and waits out
    // the timeout of each disconnected network mapping before the window shows.
    for (const std::string& drive : ListDriveRoots()) {
        std::string label = fs::path(drive).root_name().string();  // "C:"
        if (label.empty()) label = drive;
        AddTreeDriveNode(drive, label);
    }
#else
    AddTreeDriveNode("/", "File System");
    // Removable / additional volumes. Only entries that are really mounted are
    // shown - an empty placeholder folder left behind under /media or /mnt is
    // not a drive.
    for (const std::string base : {std::string("/media"), std::string("/mnt")}) {
        for (const fs::path& mount : ListSubdirectories(base)) {
            // /media holds one folder per user with the volumes below it.
            if (base == "/media") {
                auto volumes = ListSubdirectories(mount.string());
                for (const fs::path& vol : volumes)
                    if (IsMountPoint(vol.string()))
                        AddTreeDriveNode(vol.string(), vol.filename().string());
            } else if (IsMountPoint(mount.string())) {
                AddTreeDriveNode(mount.string(), mount.filename().string());
            }
        }
    }
#endif

    if (root) root->Expand();
    ApplyTreeColors();

    folderTree->onNodeExpanded = [this](TreeNode* node) {
        EnsureTreeChildren(node);
    };
    folderTree->onNodeSelected = [this](TreeNode* node) {
        if (syncingTree || !node) return;
        // A pinned entry navigates to its target folder, like a bookmark.
        const std::string path = TreeNodeTargetPath(node);
        if (path.empty()) return;
        std::error_code ec;
        if (fs::is_directory(path, ec) && !ec) NavigateTo(path);
    };
    folderTree->onNodeRightClicked = [this](TreeNode* node, const UCEvent& event) {
        ShowTreeContextMenu(node, event);
    };
    // Drag a folder from the file list onto the tree: dropping on the Pinned
    // section pins it, dropping on a folder node moves the files into it.
    folderTree->onFilesDragAccept = [this](TreeNode* node) {
        return IsTreeDropTarget(node);
    };
    folderTree->onFilesDroppedOnNode =
            [this](TreeNode* target, const std::vector<std::string>& files) {
        return DropFilesOnTreeNode(target, files);
    };
}

bool UltraFilerWindow::IsTreeDropTarget(const TreeNode* node) const {
    if (!node) return false;
    const std::string& id = node->data.nodeId;
    if (id == kPinnedNodeId ||
        id.compare(0, kPinnedChildPrefixLen, kPinnedChildPrefix) == 0)
        return true;
    // A regular folder node accepts a move into the folder it stands for.
    const std::string path = TreeNodeTargetPath(node);
    if (path.empty()) return false;
    std::error_code ec;
    return fs::is_directory(path, ec) && !ec;
}

bool UltraFilerWindow::DropFilesOnTreeNode(TreeNode* target,
                                           const std::vector<std::string>& files) {
    if (!target || files.empty()) return false;
    const std::string& id = target->data.nodeId;
    const bool pinnedSection =
            id == kPinnedNodeId ||
            id.compare(0, kPinnedChildPrefixLen, kPinnedChildPrefix) == 0;

    if (pinnedSection) {
        bool changed = false;
        for (const std::string& f : files) {
            std::error_code ec;
            if (fs::is_directory(f, ec) && !ec)
                changed = favorites.Pin(FilerFavoriteKind::Tree, f) || changed;
        }
        if (changed) {
            RefreshPinnedTreeNodes();
            RevealPinnedTreeSection();
        }
        return true;
    }

    // Otherwise a move into the folder the node represents.
    const std::string dest = TreeNodeTargetPath(target);
    if (dest.empty()) return false;
    std::error_code ec;
    if (!fs::is_directory(dest, ec) || ec) return false;

    // Skip sources that would be a no-op or a copy of a folder into itself: the
    // target itself, a folder already living in the target, or a folder that
    // contains the target (dropping it into its own subtree).
    std::vector<std::string> sources;
    for (const std::string& f : files) {
        if (IsInvalidMoveInto(f, dest)) continue;
        sources.push_back(f);
    }
    if (sources.empty()) return true;
    if (!filer) return false;

    // The filer widget runs the move, so name conflicts go through its
    // Keep both / Replace / Skip dialog exactly like a paste in the view.
    filer->PasteFilesInto(dest, std::move(sources), /*cut=*/true,
                          [this, dest](bool changed) {
        if (!changed) return;
        RecordFolderInHistory(dest);
        for (auto& state : tabStates) {
            if (state->filer && state->filer->GetPath() == dest)
                state->filer->Refresh();
        }
    });
    return true;
}

void UltraFilerWindow::AddTreeFolderNode(const std::string& parentId,
                                         const std::string& path,
                                         const std::string& label,
                                         const std::string& iconFile) {
    if (!folderTree->AddNode(parentId, MakeFolderNodeData(path, label, iconFile)))
        return;
    // The placeholder child that gives the node its expand button is added
    // once the background probe reports that the folder has subfolders.
    QueueSubfolderProbe(path);
}

void UltraFilerWindow::AddTreeDriveNode(const std::string& path,
                                        const std::string& label) {
    AddTreeFolderNode("ufl-computer", path, label, "drive.png");
    if (folderTree->FindNode(path)) treeDriveNodeIds.push_back(path);
}

void UltraFilerWindow::ApplyTreeColors() {
    if (!folderTree) return;
    folderTree->SetSelectionColor(settings.treeSelectedFolderColor);
    // A dark drive background needs light text: the drive rows are the only
    // ones painted with a user-chosen colour, and black on dark blue is
    // unreadable. Colors::Black means "use the tree's default colour".
    const Color& drive = settings.treeDriveBackgroundColor;
    const int luminance = (drive.r * 299 + drive.g * 587 + drive.b * 114) / 1000;
    const Color driveTextColor = luminance < 128 ? Colors::White : Colors::Black;
    for (const std::string& nodeId : treeDriveNodeIds) {
        if (TreeNode* node = folderTree->FindNode(nodeId)) {
            node->data.backgroundColor = drive;
            node->data.textColor = driveTextColor;
        }
    }
    folderTree->RequestRedraw();
}

void UltraFilerWindow::EnsureTreeChildren(TreeNode* node) {
    if (!node) return;
    const std::string path = node->data.nodeId;
    // Once per node: the placeholder is only a hint that a scan is due, and a
    // node may reach this before its probe has even added one.
    if (!treeChildrenLoaded.insert(path).second) return;
    // Settings > Display > Home folder decides what the Home entry shows.
    // Curated ("Show only predefined folders", the Windows default): the main
    // user folders (kHomeTreeFolders) and nothing else, so a profile does not
    // spill "3D Objects", "Saved Games" and every working folder into the
    // tree. The paths come from the platform (SHGetKnownFolderPath /
    // xdg-user-dirs), so a redirected or localized folder - "Bilder", a
    // Documents folder moved into OneDrive - is the one listed, under its own
    // icon. "Show all content" (the Linux / macOS default) lists every
    // subfolder, with the main folders still carrying their icons and a
    // redirected one listed once, by its real path.
    std::vector<TreeChild> children;
    const bool isHome = IsUserHomeDir(path);
    if (isHome && settings.homeShowPredefinedOnly) {
        children = HomeTreeChildren();
    } else if (isHome) {
        std::unordered_set<std::string> curated;
        for (const TreeChild& c : HomeTreeChildren()) {
            curated.insert(FolderIdentityKey(c.path));
            children.push_back(c);
        }
        for (const fs::path& dir : ListSubdirectories(path)) {
            if (curated.count(FolderIdentityKey(dir.string()))) continue;
            children.push_back({dir.string(), dir.filename().string(), "folder-brown.svg"});
        }
    } else {
        for (const fs::path& dir : ListSubdirectories(path))
            children.push_back({dir.string(), dir.filename().string(), "folder-brown.svg"});
    }
    std::sort(children.begin(), children.end(),
              [](const TreeChild& a, const TreeChild& b) {
        std::string an = a.label, bn = b.label;
        std::transform(an.begin(), an.end(), an.begin(), ::tolower);
        std::transform(bn.begin(), bn.end(), bn.begin(), ::tolower);
        return an < bn;
    });
    // Add the real children before removing the placeholder: a node whose
    // last child is removed is demoted to a leaf, which drops its expanded
    // state and made the first expansion of a folder appear to do nothing.
    for (const TreeChild& c : children)
        AddTreeFolderNode(path, c.path, c.label, c.icon);
    folderTree->RemoveNode(PlaceholderId(path));
}

// ===== FOLDER TREE: CLOUD STORAGE SECTION =====

void UltraFilerWindow::QueueCloudStorageDiscovery() {
    // Asking every provider where it put its folder is cheap but not free -
    // a registry read, a JSON file, a volume label per fixed drive on Windows,
    // a GVFS mount listing on Linux - and one wedged mount would hold up the
    // window. It runs on its own thread and the section appears when it
    // answers, the way the expand buttons do.
    if (cloudWorker.joinable()) return;   // runs exactly once, at tree build
    auto alive = probeAlive;
    cloudWorker = std::thread([this, alive]() {
        // An exception leaving a std::thread ends the process; a provider
        // whose registry or config cannot be read costs the Cloud section.
        std::vector<CloudStorageInfo> found;
        try {
            found = GetCloudStorageFolders();
        } catch (const std::exception& e) {
            debugOutput << "UltraFiler: cloud storage discovery failed: "
                        << e.what() << std::endl;
            return;
        } catch (...) {
            debugOutput << "UltraFiler: cloud storage discovery failed" << std::endl;
            return;
        }
        if (found.empty()) return;
        UltraCanvasApplicationBase* app = UltraCanvasApplicationBase::GetCurrent();
        if (!app) return;
        app->PostToUIThread([this, alive, found = std::move(found)]() {
            if (!alive->load()) return;   // window destroyed meanwhile
            ApplyCloudStorageFolders(found);
        });
    });
}

void UltraFilerWindow::StopCloudStorageDiscovery() {
    // Joined rather than detached, like the subfolder probe: the thread posts
    // back into the window, so it must not outlive it - nor the application it
    // posts through.
    if (cloudWorker.joinable()) cloudWorker.join();
}

void UltraFilerWindow::ApplyCloudStorageFolders(
        const std::vector<CloudStorageInfo>& found) {
    if (found.empty() || !folderTree) return;
    TreeNode* cloud = folderTree->FindNode(kCloudNodeId);
    if (!cloud) return;
    for (const CloudStorageInfo& c : found)
        AddTreeFolderNode(kCloudNodeId, c.path, c.label, "cloud.svg");
    if (cloud->children.empty()) return;   // every folder vanished meanwhile
    // Shown open: a section of two or three entries that has to be unfolded
    // first hides exactly what it was added to surface.
    cloud->data.visible = true;
    folderTree->ExpandNode(cloud);
    folderTree->RequestRedraw();
}

// ===== FOLDER TREE: BACKGROUND "HAS SUBFOLDERS?" PROBE =====

void UltraFilerWindow::QueueSubfolderProbe(const std::string& path) {
    std::lock_guard<std::mutex> lk(probeMutex);
    if (probeShutdown) return;
    // Newest first: the folder the user just expanded is answered before the
    // backlog of whatever was expanded earlier.
    probeQueue.push_front(path);
    StartSubfolderProbeWorkerLocked();
    probeCond.notify_one();
}

void UltraFilerWindow::StartSubfolderProbeWorkerLocked() {
    if (probeWorker.joinable() || probeShutdown) return;
    probeWorker = std::thread([this]() { SubfolderProbeWorkerMain(); });
}

void UltraFilerWindow::StopSubfolderProbeWorker() {
    {
        std::lock_guard<std::mutex> lk(probeMutex);
        probeShutdown = true;
        probeQueue.clear();
    }
    probeCond.notify_all();
    if (probeWorker.joinable()) probeWorker.join();
}

void UltraFilerWindow::SubfolderProbeWorkerMain() {
    for (;;) {
        std::string path;
        {
            std::unique_lock<std::mutex> lk(probeMutex);
            probeCond.wait(lk, [this]() { return probeShutdown || !probeQueue.empty(); });
            if (probeShutdown) return;
            path = std::move(probeQueue.front());
            probeQueue.pop_front();
        }

        // The directory open - outside the lock, off the UI thread. The home
        // mode comes through the atomic: `settings` belongs to the UI thread.
        // A throw here would end the process (nothing catches what leaves a
        // std::thread); a folder that cannot be read is shown as a leaf.
        bool has = false;
        try {
            has = TreeFolderHasChildren(path, curatedHomeActive.load());
        } catch (const std::exception& e) {
            debugOutput << "UltraFiler: subfolder probe failed for \"" << path
                        << "\": " << e.what() << std::endl;
        } catch (...) {
            debugOutput << "UltraFiler: subfolder probe failed for \"" << path
                        << "\"" << std::endl;
        }
        if (!has) continue;   // leaf folder: nothing to change on the node

        UltraCanvasApplicationBase* app = UltraCanvasApplicationBase::GetCurrent();
        if (!app) continue;
        auto alive = probeAlive;
        app->PostToUIThread([this, alive, path]() {
            if (!alive->load()) return;   // window destroyed meanwhile
            ApplySubfolderProbe(path, true);
        });
    }
}

void UltraFilerWindow::ApplySubfolderProbe(const std::string& path,
                                           bool hasSubfolders) {
    if (!hasSubfolders || !folderTree) return;
    // A node that has since been expanded already holds its real children.
    if (treeChildrenLoaded.count(path)) return;
    TreeNode* node = folderTree->FindNode(path);
    if (!node || !node->children.empty()) return;
    TreeNodeData placeholder;
    placeholder.nodeId = PlaceholderId(path);
    placeholder.text = "...";
    folderTree->AddNode(path, placeholder);
    folderTree->RequestRedraw();   // the row gained its expand button
}

void UltraFilerWindow::SyncTreeSelection(const std::string& path) {
    if (!folderTree) return;
    TreeNode* node = folderTree->FindNode(path);
    if (!node) {
        // Expand the deepest known ancestor down towards the target folder.
        std::vector<std::string> chain;    // [path, parent, ..., root]
        fs::path p(path);
        while (true) {
            chain.push_back(p.string());
            const fs::path parent = p.parent_path();
            if (parent.empty() || parent == p) break;
            p = parent;
        }
        size_t idx = 0;
        TreeNode* anchor = nullptr;
        for (size_t i = 0; i < chain.size() && !anchor; ++i) {
            anchor = folderTree->FindNode(chain[i]);
            idx = i;
        }
        if (!anchor) return;
        while (idx > 0) {
            EnsureTreeChildren(anchor);
            anchor->Expand();
            --idx;
            anchor = folderTree->FindNode(chain[idx]);
            if (!anchor) return;   // e.g. a hidden folder that the tree skips
        }
        node = anchor;
    }
    syncingTree = true;
    folderTree->SelectNode(node);
    syncingTree = false;
}

// ===== FOLDER TREE: PINNED SECTION + CONTEXT MENU =====

std::string UltraFilerWindow::TreeNodeTargetPath(const TreeNode* node) const {
    if (!node) return {};
    const std::string& id = node->data.nodeId;
    if (id == kTreeRootNodeId || id == "ufl-computer" || id == kPinnedNodeId ||
        id == kCloudNodeId)
        return {};
    if (id.compare(0, kPinnedChildPrefixLen, kPinnedChildPrefix) == 0)
        return id.substr(kPinnedChildPrefixLen);
    // The lazy "..." placeholder children are not folders.
    if (id.find(kPlaceholderSuffix) != std::string::npos) return {};
    return id;
}

void UltraFilerWindow::RefreshPinnedTreeNodes() {
    if (!folderTree) return;
    TreeNode* pinned = folderTree->FindNode(kPinnedNodeId);
    if (!pinned) return;
    while (!pinned->children.empty())
        folderTree->RemoveNode(pinned->children.front()->data.nodeId);
    // Paths() drops pins whose folder no longer exists, so the section heals
    // itself like the History lists do.
    for (const std::string& path : favorites.Paths(FilerFavoriteKind::Tree)) {
        std::string label = fs::path(path).filename().string();
        if (label.empty()) label = path;   // a filesystem root
        folderTree->AddNode(kPinnedNodeId,
                MakeFolderNodeData(kPinnedChildPrefix + path, label,
                                   "folder-brown.svg"));
    }
    // An empty section is just a header over nothing: hide it entirely while
    // nothing is pinned, and show it open — its entries are the point of it.
    pinned->data.visible = !pinned->children.empty();
    if (pinned->data.visible) folderTree->ExpandNode(pinned);
    folderTree->RequestRedraw();
}

void UltraFilerWindow::RevealPinnedTreeSection() {
    // Show the new entry rather than leaving it behind a collapsed header.
    if (TreeNode* pinned = folderTree ? folderTree->FindNode(kPinnedNodeId) : nullptr) {
        folderTree->ExpandNode(pinned);
        folderTree->RequestRedraw();
    }
}

void UltraFilerWindow::ShowTreeContextMenu(TreeNode* node, const UCEvent& event) {
    if (!node || !window) return;
    const std::string target = TreeNodeTargetPath(node);
    const bool isFolder = !target.empty();
    const std::string& id = node->data.nodeId;
    const bool isPinnedEntry =
            id.compare(0, kPinnedChildPrefixLen, kPinnedChildPrefix) == 0;
    // Home, File System, the drive roots and the cloud folders are the roots of
    // the tree - the first sit directly under Computer, the others under Cloud
    // Storage. Deleting one of those from a context menu would be a
    // catastrophe (a cloud folder syncs the deletion to every other device), so
    // they keep Delete disabled.
    const bool isTopLevelRoot = !isPinnedEntry && node->parent &&
            (node->parent->data.nodeId == "ufl-computer" ||
             node->parent->data.nodeId == kCloudNodeId);

    std::vector<std::string> clipboardFiles;
    bool clipboardCut = false;
    if (UltraCanvasClipboard* cb = GetClipboard())
        cb->GetFiles(clipboardFiles, clipboardCut);

    MenuItemData copyItem = MenuItemData::Action("Copy", [this, target]() {
        if (UltraCanvasClipboard* cb = GetClipboard())
            cb->SetFiles({target}, false);
    });
    copyItem.enabled = isFolder;

    MenuItemData deleteItem = MenuItemData::Action("Delete", [this, target]() {
        ConfirmDeleteTreeFolder(target);
    });
    deleteItem.enabled = isFolder && !isTopLevelRoot;

    // Paste needs a target folder under the cursor - and files to paste.
    MenuItemData pasteItem = MenuItemData::Action("Paste", [this, target]() {
        PasteIntoFolder(target);
    });
    pasteItem.enabled = isFolder && !clipboardFiles.empty();

    // Pin: the flags show where this folder is pinned right now; toggling a
    // flag pins it there / unpins it from there.
    MenuItemData pinToTree = MenuItemData::Checkbox("To Treeview",
            isFolder && favorites.IsPinned(FilerFavoriteKind::Tree, target),
            [this, target](bool checked) {
        if (checked) {
            favorites.Pin(FilerFavoriteKind::Tree, target);
            RefreshPinnedTreeNodes();
            RevealPinnedTreeSection();
        } else {
            favorites.Unpin(FilerFavoriteKind::Tree, target);
            RefreshPinnedTreeNodes();
        }
    });
    pinToTree.enabled = isFolder;

    MenuItemData pinToFavorites = MenuItemData::Checkbox("To Favorites",
            isFolder && favorites.IsPinned(FilerFavoriteKind::Folder, target),
            [this, target](bool checked) {
        if (checked) favorites.Pin(FilerFavoriteKind::Folder, target);
        else favorites.Unpin(FilerFavoriteKind::Folder, target);
        if (favoritesShown) {
            RefreshFavoritesTabs();
            UpdateStatusBar();
        }
    });
    pinToFavorites.enabled = isFolder;

    MenuItemData pinSubmenu = MenuItemData::Submenu("Pin",
            {pinToTree, pinToFavorites});
    pinSubmenu.enabled = isFolder;

    MenuItemData unpinItem = MenuItemData::Action("Unpin", [this, target]() {
        favorites.Unpin(FilerFavoriteKind::Tree, target);
        RefreshPinnedTreeNodes();
    });
    unpinItem.enabled = isPinnedEntry;

    MenuStyle style = MenuStyle::Default();
    style.font.fontSize = kUiFontSize;
    treeContextMenu = std::make_shared<UltraCanvasMenu>("ufl-tree-menu", 0, 0, 160, 0);
    treeContextMenu->SetMenuType(MenuType::PopupMenu);
    treeContextMenu->SetStyle(style);
    treeContextMenu->AddItem(copyItem);
    treeContextMenu->AddItem(deleteItem);
    treeContextMenu->AddItem(pasteItem);
    treeContextMenu->AddItem(MenuItemData::Separator());
    treeContextMenu->AddItem(pinSubmenu);
    treeContextMenu->AddItem(unpinItem);
    treeContextMenu->OpenMenu(event.pointerWindow, *window, PopupElementSettings());
}

void UltraFilerWindow::PasteIntoFolder(const std::string& folder) {
    std::vector<std::string> paths;
    bool cut = false;
    if (UltraCanvasClipboard* cb = GetClipboard()) cb->GetFiles(paths, cut);
    if (paths.empty()) return;

    if (!filer) return;
    // The filer widget runs the paste, so name conflicts go through its
    // Keep both / Replace / Skip dialog exactly like a paste in the view.
    filer->PasteFilesInto(folder, std::move(paths), cut,
                          [this, folder](bool changed) {
        if (!changed) return;
        // Pasting is work done in the folder, exactly like a paste in the filer.
        RecordFolderInHistory(folder);
        for (auto& state : tabStates) {
            if (state->filer && state->filer->GetPath() == folder)
                state->filer->Refresh();
        }
    });
}

void UltraFilerWindow::ConfirmDeleteTreeFolder(const std::string& path) {
    std::string name = fs::path(path).filename().string();
    if (name.empty()) name = path;
    UltraCanvasAlert::Confirm(
            "Delete \"" + name + "\" and everything in it?", "Delete",
            [this, path](bool confirmed) {
        if (!confirmed) return;
        const std::string parent = fs::path(path).parent_path().string();
        std::error_code ec;
        fs::remove_all(path, ec);
        if (ec) {
            if (statusLabel) statusLabel->SetText("Error: cannot delete " + path + ": " + ec.message());
            return;
        }
        // Take the folder out of the tree, its pins, and the bookkeeping of
        // scanned nodes (it may be recreated and scanned again later).
        if (folderTree) folderTree->RemoveNode(path);
        for (auto it = treeChildrenLoaded.begin(); it != treeChildrenLoaded.end();) {
            if (IsPathInside(*it, path)) it = treeChildrenLoaded.erase(it);
            else ++it;
        }
        RefreshPinnedTreeNodes();
        // Tabs that were inside the deleted folder move to its parent; tabs
        // showing the parent re-list it without the deleted entry.
        for (auto& state : tabStates) {
            if (!state->filer) continue;
            const std::string shown = state->filer->GetPath();
            if (IsPathInside(shown, path)) state->filer->SetPath(parent);
            else if (shown == parent) state->filer->Refresh();
        }
        RecordFolderInHistory(parent);
        UpdateStatusBar();
    }, window.get());
}

// ===== TABS =====

void UltraFilerWindow::BuildTabbedContainer() {
    // The strip alone is this element - it is exactly one tab high and sits at
    // the top of the window; the pages go into `tabContentHost`, which
    // BuildSplitLayout puts in the folder pane of the split.
    tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>(
            "ufl-tabs", 0, 0, 0, kTabStripHeight);
    tabbedContainer->fontSize = static_cast<int>(kUiFontSize);
    tabbedContainer->SetTabHeight(kTabStripHeight);
    tabbedContainer->SetTabMinWidth(90);
    tabbedContainer->SetCloseMode(TabCloseMode::Closable);
    tabbedContainer->tabBarColor = Color(249, 249, 251, 255);
    tabbedContainer->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                               .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    tabContentHost = MakeLayoutBox("ufl-tab-content");
    tabContentHost->layout.SetFlexColumn()
                          .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    tabContentHost->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                              .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    tabbedContainer->SetContentHost(tabContentHost);

    // "+" at the end of the tab list opens another tab on the current folder.
    tabbedContainer->SetNewTabButtonPosition(NewTabButtonPosition::AfterTabs);
    tabbedContainer->SetShowNewTabButton(true);
    tabbedContainer->SetNewButtonColor(Color(249, 249, 251, 255));
    tabbedContainer->onNewTabRequest = [this]() {
        std::string path = filer ? filer->GetPath() : std::string();
        if (path.empty()) path = UserHomeDir();
        AddNewTab(path, true);
    };

    tabbedContainer->onTabClose = [this](int index) {
        // The last remaining tab stays open.
        if (tabStates.size() <= 1) return false;
        if (index >= 0 && index < (int)tabStates.size())
            tabStates.erase(tabStates.begin() + index);
        return true;
    };
    tabbedContainer->onTabChange = [this](int /*oldIndex*/, int newIndex) {
        HandleTabSwitched(newIndex);
    };
    tabbedContainer->onTabReorder = [this](int from, int to) {
        if (from < 0 || to < 0 || from >= (int)tabStates.size() ||
            to >= (int)tabStates.size() || from == to)
            return;
        auto st = std::move(tabStates[from]);
        tabStates.erase(tabStates.begin() + from);
        tabStates.insert(tabStates.begin() + to, std::move(st));
    };
}

void UltraFilerWindow::AddNewTab(const std::string& path, bool activate) {
    // The new tab has to be visible, so opening one leaves the History /
    // Favorites views.
    if (activate) ShowBrowsingView();

    auto state = std::make_unique<FilerTabState>();
    const std::string suffix = std::to_string(++tabCounter);

    state->page = MakeLayoutBox("ufl-tab-page-" + suffix);
    state->page->layout.SetFlexColumn()
                       .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    // The pages are children of the content host, which shows one at a time:
    // the visible page takes the whole host, the hidden ones are out of flow.
    state->page->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    state->filer = CreateFilerWidget("ufl-filer-" + suffix, 0, 0, 0, 0);
    // Settings > Display > Home folder: when curated (the Windows default),
    // the home folder's display shows its main folders and its files, not the
    // whole profile - like the tree's Home entry. Display > Hidden files
    // always reveals the full listing.
    if (settings.homeShowPredefinedOnly)
        state->filer->SetCuratedHomeFolder(UserHomeDir(), HomeCurationPaths());
    FilerStyle filerStyle = state->filer->GetStyle();
    filerStyle.fontSize = kUiFontSize;
    filerStyle.smallFontSize = kUiFontSize;
    filerStyle.folderIconScale = 0.7f;
    state->filer->SetStyle(filerStyle);
    state->filer->SetViewType(FilerViewType::ThumbnailsMedium);
    // With the preview up, a delete of the previewed file moves the selection
    // (and with it the preview) on to the next entry instead of emptying it.
    state->filer->SetSelectNextAfterDelete(previewEnabled);
    // Handling > Drag & Drop: move or copy on a plain drop onto a folder.
    state->filer->SetDropOnFolderCopies(settings.dropOnFolderCopies);
    state->filer->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                            .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    state->page->AddChild(state->filer);

    FilerTabState* raw = state.get();
    tabStates.push_back(std::move(state));
    WireFilerCallbacks(raw);

    const int index = tabbedContainer->AddTab(TabTitleForPath(path), raw->page);
    if (activate) {
        // Fires onTabChange, which points `filer` at the new tab.
        tabbedContainer->SetActiveTab(index);
    }
    raw->filer->SetPath(path);
}

void UltraFilerWindow::WireFilerCallbacks(FilerTabState* tab) {
    tab->filer->onPathChanged = [this, tab](const std::string& path) {
        HandlePathChanged(tab, path);
    };
    // When the as-you-type filter matches nothing in the folder, the filer
    // centers this escalation: the same recursive search Enter runs.
    tab->filer->SetFilterEmptyAction("Search in sub folders", [this, tab]() {
        if (!IsActiveTab(tab) || !tab->filer) return;
        // Copied: RunSearch clears the filer's filter, which would otherwise
        // empty the query out from under the search.
        const std::string query = tab->filer->GetNameFilter();
        RunSearch(query);
    });
    tab->filer->onSelectionChanged = [this, tab](const std::vector<FilerEntry>&) {
        if (!IsActiveTab(tab)) return;
        UpdateStatusBar();
        UpdatePreviewPane();
    };
    tab->filer->onFolderRefreshed = [this, tab]() {
        // The folder listing changed (a file operation, a drop, a rename):
        // the item counts in the status bar describe it, and a previewed file
        // may have moved away.
        if (!IsActiveTab(tab)) return;
        // The widget can end its own name filter (creating an entry does, so
        // the fresh one is visible) — the search field follows it.
        if (searchInput && tab->searchQuery.empty() && tab->filer &&
            searchInput->GetText() != tab->filer->GetNameFilter()) {
            searchInput->SetText(tab->filer->GetNameFilter());
        }
        UpdateStatusBar();
        UpdatePreviewPane();
    };
    tab->filer->onFileActivated = [this, tab](const FilerEntry& entry) {
        if (entry.isDirectory) return;
        // Opening a file (or launching an application) puts it at the top of
        // the matching History list, and counts as work done in its folder.
        RecordEntryInHistory(entry);
        RecordFolderInHistory(fs::path(entry.path).parent_path().string());
        if (!IsActiveTab(tab)) return;
#ifdef ULTRACANVAS_HAS_ULTRAWIN
        // Windows executables and installers go to the UltraWin emulation
        // layer, not to the host's file associations (.msi runs through
        // msiexec inside the environment).
        if (entry.extension == "exe" || entry.extension == "msi") {
            LaunchWindowsExecutable(entry);
            return;
        }
#endif
        if (!UltraCanvasMediaViewer::IsSupportedMedia(entry.path)) {
            // Not previewable: run it / open it, Explorer-style. The widget
            // launches executables directly (scripts through its Run-or-Open
            // dialog) and everything else with the OS default application;
            // archive entries (virtual paths) are ignored there.
            if (tab->filer) tab->filer->OpenEntryWithOS(entry);
            return;
        }
        // Double-click / Enter opens the file in the preview, un-hiding it
        // when needed.
        if (!previewEnabled) SetPreviewEnabled(true);
        else UpdatePreviewPane();
    };
    tab->filer->onSortChanged = [this, tab](FilerSortField field, bool /*ascending*/) {
        RememberFolderView(tab);
        if (!IsActiveTab(tab)) return;
        UpdateSortOrderButton();
        if (!sortDropdown) return;
        syncingControls = true;
        switch (field) {
            case FilerSortField::Name:         sortDropdown->SetSelectedIndex(0, false); break;
            case FilerSortField::Size:         sortDropdown->SetSelectedIndex(1, false); break;
            case FilerSortField::Type:         sortDropdown->SetSelectedIndex(2, false); break;
            case FilerSortField::ModifiedDate: sortDropdown->SetSelectedIndex(3, false); break;
            case FilerSortField::CreatedDate:  sortDropdown->SetSelectedIndex(4, false); break;
        }
        syncingControls = false;
    };
    tab->filer->onViewTypeChanged = [this, tab](FilerViewType type) {
        RememberFolderView(tab);
        if (!IsActiveTab(tab) || !viewDropdown) return;
        syncingControls = true;
        switch (type) {
            case FilerViewType::Details:             viewDropdown->SetSelectedIndex(0, false); break;
            case FilerViewType::List:                viewDropdown->SetSelectedIndex(1, false); break;
            case FilerViewType::ThumbnailsSmall:     viewDropdown->SetSelectedIndex(2, false); break;
            case FilerViewType::ThumbnailsMedium:    viewDropdown->SetSelectedIndex(3, false); break;
            case FilerViewType::ThumbnailsBig:       viewDropdown->SetSelectedIndex(4, false); break;
            case FilerViewType::ThumbnailsMaximized: viewDropdown->SetSelectedIndex(5, false); break;
            case FilerViewType::BarSize:             viewDropdown->SetSelectedIndex(6, false); break;
            case FilerViewType::TreeMap:             viewDropdown->SetSelectedIndex(7, false); break;
            default: break;
        }
        syncingControls = false;
    };
    // Work done in a folder - a file created, pasted, dropped in or out,
    // renamed, duplicated, deleted, packed or extracted - is what puts it in
    // the History view's Folders tab. Merely looking at a folder does not.
    tab->filer->onFolderModified = [this](const std::string& folder) {
        RecordFolderInHistory(folder);
    };
    tab->filer->onError = [this](const std::string& message) {
        if (statusLabel) statusLabel->SetText("Error: " + message);
    };
    tab->filer->onOpenPath = [this](const FilerEntry& entry) {
        const std::string parent = fs::path(entry.path).parent_path().string();
        if (!parent.empty()) AddNewTab(parent, true);
    };
    // The context menu's Settings item opens the same settings window as the
    // navigation row's gear button.
    tab->filer->onSettings = [this]() { OpenSettingsDialog(); };
    tab->filer->onPrint = [this](const std::vector<FilerEntry>& t) { HandlePrint(t); };
    tab->filer->onShare = [this](const std::vector<FilerEntry>& t) { HandleShare(t); };
    tab->filer->onAttributes = [this](const std::vector<FilerEntry>& t) { HandleAttributes(t); };
    tab->filer->onAccess = [this](const std::vector<FilerEntry>& t) { HandleAccess(t); };
    tab->filer->extrasMenuProvider = [this]() { return BuildExtrasMenuItems(); };
}

void UltraFilerWindow::HandleTabSwitched(int index) {
    // The strip stays visible while the History / Favorites views replace the
    // folder display, so picking a tab means going back to browsing.
    ShowBrowsingView();
    if (index < 0 || index >= (int)tabStates.size()) return;
    FilerTabState* tab = tabStates[index].get();
    if (!tab->filer) return;
    filer = tab->filer;

    const std::string path = filer->GetPath();
    if (breadcrumb && !path.empty()) {
        BuildFolderBreadcrumb(breadcrumb.get(), path,
                              [this](const std::string& folder) { NavigateTo(folder); });
    }
    // The field shows whatever search state the tab is in: the recursive
    // query while its results are displayed, else the tab's live filter.
    if (searchInput) {
        searchInput->SetText(!tab->searchQuery.empty()
                                     ? tab->searchQuery
                                     : (tab->filer ? tab->filer->GetNameFilter()
                                                   : std::string()));
    }
    UpdateNavButtons();
    if (!path.empty()) SyncTreeSelection(path);
    UpdateStatusBar();
    UpdateWindowTitle();

    // Mirror the tab's sort / view settings into the command bar.
    UpdateSortOrderButton();
    syncingControls = true;
    switch (filer->GetSortField()) {
        case FilerSortField::Name:         sortDropdown->SetSelectedIndex(0, false); break;
        case FilerSortField::Size:         sortDropdown->SetSelectedIndex(1, false); break;
        case FilerSortField::Type:         sortDropdown->SetSelectedIndex(2, false); break;
        case FilerSortField::ModifiedDate: sortDropdown->SetSelectedIndex(3, false); break;
        case FilerSortField::CreatedDate:  sortDropdown->SetSelectedIndex(4, false); break;
    }
    switch (filer->GetViewType()) {
        case FilerViewType::Details:             viewDropdown->SetSelectedIndex(0, false); break;
        case FilerViewType::List:                viewDropdown->SetSelectedIndex(1, false); break;
        case FilerViewType::ThumbnailsSmall:     viewDropdown->SetSelectedIndex(2, false); break;
        case FilerViewType::ThumbnailsMedium:    viewDropdown->SetSelectedIndex(3, false); break;
        case FilerViewType::ThumbnailsBig:       viewDropdown->SetSelectedIndex(4, false); break;
        case FilerViewType::ThumbnailsMaximized: viewDropdown->SetSelectedIndex(5, false); break;
        case FilerViewType::BarSize:             viewDropdown->SetSelectedIndex(6, false); break;
        case FilerViewType::TreeMap:             viewDropdown->SetSelectedIndex(7, false); break;
        default: break;
    }
    syncingControls = false;

    UpdatePreviewPane();
}

UltraFilerWindow::FilerTabState* UltraFilerWindow::ActiveTabState() const {
    if (!tabbedContainer) return nullptr;
    const int index = tabbedContainer->GetActiveTab();
    if (index < 0 || index >= (int)tabStates.size()) return nullptr;
    return tabStates[index].get();
}

bool UltraFilerWindow::IsActiveTab(const FilerTabState* tab) const {
    return tab && tab == ActiveTabState();
}

int UltraFilerWindow::TabIndexOf(const FilerTabState* tab) const {
    for (size_t i = 0; i < tabStates.size(); ++i)
        if (tabStates[i].get() == tab) return (int)i;
    return -1;
}

// ===== SPLIT LAYOUT (tree | tabbed filer | preview) =====

void UltraFilerWindow::BuildSplitLayout() {
    // Everything between the command bar and the status bar lives in this box:
    // the split below, and the History view that replaces it (only one of the
    // two is visible at a time).
    contentBox = MakeLayoutBox("ufl-content");
    contentBox->layout.SetFlexColumn()
                      .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    contentBox->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                          .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    split = std::make_shared<UltraCanvasSplitPane>("ufl-split", SplitOrientation::Horizontal);
    split->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                     .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto treePane = split->AddPane(1.0);
    split->SetPaneMinSize(0, 170);
    // The tree keeps an absolute width: 280px at startup, then whatever the
    // user drags the splitter to. Maximizing or resizing the window changes
    // only the folder display's share — the tree stays as wide as it is.
    split->SetPaneFixedSize(0, 280);
    treePane->layout.SetFlexColumn()
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    folderTree->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                          .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    treePane->AddChild(folderTree);

    auto filerPane = split->AddPane(1.0);
    split->SetPaneMinSize(1, kFilerMinWidth);
    filerPane->layout.SetFlexColumn()
                     .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    // The tab strip itself is the window's top bar; this pane shows the page
    // of whichever tab is active.
    filerPane->AddChild(tabContentHost);

    preview->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                       .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    folderPreview->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                             .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    // The detail pane is added by UpdatePreviewPane once a previewable file
    // or a folder is selected; until then the folder display uses the whole
    // width.

    contentBox->AddChild(split);
    window->AddChild(contentBox);
}

// ===== HISTORY VIEW (Files | Folders | Apps) =====

void UltraFilerWindow::BuildHistoryView() {
    historyPane = MakeLayoutBox("ufl-history-pane");
    historyPane->layout.SetFlexColumn()
                       .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    historyPane->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    historyTabs = std::make_shared<UltraCanvasTabbedContainer>("ufl-history-tabs");
    historyTabs->fontSize = static_cast<int>(kUiFontSize);
    historyTabs->SetTabHeight(30);
    historyTabs->SetTabMinWidth(90);
    historyTabs->SetCloseMode(TabCloseMode::NoClose);
    historyTabs->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    historyTabs->onTabChange = [this](int /*oldIndex*/, int /*newIndex*/) {
        UpdateStatusBar();
    };

    for (int i = 0; i < HistoryTabCount; ++i) {
        const std::string suffix = std::to_string(i);

        auto page = MakeLayoutBox("ufl-history-page-" + suffix);
        page->layout.SetFlexColumn()
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

        auto histFiler = CreateFilerWidget("ufl-history-filer-" + suffix, 0, 0, 0, 0);
        FilerStyle filerStyle = histFiler->GetStyle();
        filerStyle.fontSize = kUiFontSize;
        filerStyle.smallFontSize = kUiFontSize;
        filerStyle.folderIconScale = 0.7f;
        histFiler->SetStyle(filerStyle);
        histFiler->SetViewType(FilerViewType::ThumbnailsSmall);
        // The lists are handed over most recently used first, and that order
        // is what a history is about - so it is kept instead of sorted.
        histFiler->SetFileListOrderPreserved(true);
        // A remembered path was opened deliberately, so it belongs in the list
        // even when it is a dotfile / a folder under one.
        histFiler->SetShowHiddenFiles(true);
        // The entries come from all over the filesystem, so the context menu
        // offers to open the folder an entry lives in.
        histFiler->SetOpenPathMenuItemVisible(true, "Open path (in new tab)");
        histFiler->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                             .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        histFiler->onSelectionChanged = [this](const std::vector<FilerEntry>&) {
            UpdateStatusBar();
        };
        histFiler->onFolderRefreshed = [this]() { UpdateStatusBar(); };
        histFiler->onFileActivated = [this](const FilerEntry& entry) {
            RecordEntryInHistory(entry);
            RecordFolderInHistory(fs::path(entry.path).parent_path().string());
            OpenHistoryEntry(entry.path, false);
        };
        // A folder tile is activated by the widget itself (it navigates into
        // the folder); hand that folder to the browsing view instead of
        // browsing it inside the History view.
        histFiler->onPathChanged = [this](const std::string& path) {
            OpenHistoryEntry(path, true);
        };
        histFiler->onOpenPath = [this](const FilerEntry& entry) {
            const std::string parent = fs::path(entry.path).parent_path().string();
            if (parent.empty()) return;
            SetHistoryVisible(false);
            AddNewTab(parent, true);
        };
        histFiler->onError = [this](const std::string& message) {
            if (statusLabel) statusLabel->SetText("Error: " + message);
        };
        histFiler->onSettings = [this]() { OpenSettingsDialog(); };
        histFiler->onPrint = [this](const std::vector<FilerEntry>& t) { HandlePrint(t); };
        histFiler->onShare = [this](const std::vector<FilerEntry>& t) { HandleShare(t); };
        histFiler->onAttributes = [this](const std::vector<FilerEntry>& t) { HandleAttributes(t); };
        histFiler->onAccess = [this](const std::vector<FilerEntry>& t) { HandleAccess(t); };
        histFiler->extrasMenuProvider = [this]() { return BuildExtrasMenuItems(); };

        page->AddChild(histFiler);
        historyFilers[i] = histFiler;
        historyTabs->AddTab(kHistoryTabTitles[i], page);
    }
    historyTabs->SetActiveTab(HistoryFiles);

    historyPane->AddChild(historyTabs);
    historyPane->SetVisible(false);   // the clock button turns it on
    contentBox->AddChild(historyPane);
}

void UltraFilerWindow::SetHistoryVisible(bool visible) {
    if (visible == historyShown || !historyPane || !split) return;
    // Only one of the History / Favorites views can replace the split.
    if (visible) SetFavoritesVisible(false);
    historyShown = visible;
    StyleToggleButton(historyButton.get(), historyShown);

    if (visible) {
        RefreshHistoryTabs();
        split->SetVisible(false);
        historyPane->SetVisible(true);
    } else {
        historyPane->SetVisible(false);
        split->SetVisible(true);
    }
    UpdateStatusBar();
    UpdateWindowTitle();
}

void UltraFilerWindow::RefreshHistoryTabs() {
    static const FilerHistoryKind kinds[HistoryTabCount] = {
        FilerHistoryKind::File, FilerHistoryKind::Folder, FilerHistoryKind::App};
    for (int i = 0; i < HistoryTabCount; ++i) {
        if (historyFilers[i]) historyFilers[i]->ShowFileList(history.Paths(kinds[i]));
    }
}

void UltraFilerWindow::RecordEntryInHistory(const FilerEntry& entry) {
    if (entry.isDirectory) {
        RecordFolderInHistory(entry.path);
        return;
    }
    history.Record(IsApplicationEntry(entry) ? FilerHistoryKind::App
                                             : FilerHistoryKind::File,
                   entry.path);
}

#ifdef ULTRACANVAS_HAS_ULTRAWIN
void UltraFilerWindow::LaunchWindowsExecutable(const FilerEntry& entry) {
    if (!UltraWin_IsInitialized()) UltraWin_Initialize();
    if (!UltraWin_GetCapabilities().wineTierAvailable) {
        if (statusLabel)
            statusLabel->SetText(
                "Wine is not installed — install it (e.g. 'sudo apt install "
                "wine') to run Windows applications");
        return;
    }

    // Already decided? A program inside an environment's prefix runs
    // there, and a remembered association covers everything a picker
    // answered before — both resolve inside UltraWin_RunApp.
    std::string decided = UltraWin_EnvironmentForPath(entry.path);
    if (decided.empty()) decided = UltraWin_GetAssociation(entry.path);
    if (!decided.empty()) {
        StartWindowsLaunch(entry, decided);
        return;
    }

    // First launch of an unknown program: ask once. The suggestion is a
    // sibling program's environment when one is associated (multi-exe
    // applications share), else a name derived from folder/file.
    std::vector<std::string> names;
    for (const auto& env : UltraWin_ListEnvironments())
        names.push_back(env.name);
    ShowRunWindowsDialog(
        entry.name, names, UltraWin_SuggestEnvironment(entry.path),
        [this, entry](const std::string& environment, bool remember) {
            if (remember) UltraWin_SetAssociation(entry.path, environment);
            StartWindowsLaunch(entry, environment);
        },
        window.get());
}

void UltraFilerWindow::StartWindowsLaunch(const FilerEntry& entry,
                                          const std::string& environment) {
    const bool firstLaunch = !UltraWin_EnvironmentExists(environment);
    const char* verb =
        entry.extension == "msi" ? "Installing " : "Launching ";
    if (statusLabel)
        statusLabel->SetText(
            verb + entry.name +
            (firstLaunch ? " — first launch prepares its Windows environment, "
                           "this can take a minute…"
                         : "…"));

    // Launch off the UI thread: a first launch runs wineboot. The thread
    // only touches the window through PostToUIThread, guarded by the same
    // alive flag the subfolder probe worker uses.
    auto alive = probeAlive;
    std::thread([this, alive, path = entry.path, name = entry.name,
                 environment]() {
        UltraWinRunOptions options;
        options.environment = environment;
        UltraWinHandle handle = UltraWinInvalidHandle;
        UltraWinResult result;
        try {
            result = UltraWin_RunApp(path, options, &handle);
        } catch (const std::exception& e) {
            // Off the UI thread: reported through the status line below
            // rather than by ending the process.
            result = UltraWinResult::Error(UltraWinResultCode::LaunchFailed, e.what());
        }

        UltraCanvasApplicationBase* app = UltraCanvasApplicationBase::GetCurrent();
        if (!app) return;
        app->PostToUIThread([this, alive, name, result]() {
            if (!alive->load() || !statusLabel) return;
            statusLabel->SetText(result ? "Launched " + name
                                        : "Could not launch " + name + ": " +
                                              result.message);
        });
    }).detach();
}
#endif

void UltraFilerWindow::RecordFolderInHistory(const std::string& folder) {
    // Archive interiors are not real directories - they would only be pruned
    // from the list again on the next read.
    std::error_code ec;
    if (folder.empty() || !fs::is_directory(folder, ec) || ec) return;
    history.Record(FilerHistoryKind::Folder, folder);
    // The Folders tab is stale now if it is on screen.
    if (historyShown && historyFilers[HistoryFolders]) {
        historyFilers[HistoryFolders]->ShowFileList(
                history.Paths(FilerHistoryKind::Folder));
    }
}

void UltraFilerWindow::OpenHistoryEntry(const std::string& path, bool isFolder) {
    if (path.empty()) return;
    ShowBrowsingView();
    // A folder is opened; a file (or application) is shown selected inside the
    // folder it lives in, which also hands it to the preview when it is media.
    const std::string target = isFolder ? path
                                        : fs::path(path).parent_path().string();
    if (target.empty()) return;
    NavigateTo(target);
    if (!isFolder && filer) filer->SelectPath(path);
}

UltraCanvasFilerWidget* UltraFilerWindow::ActiveHistoryFiler() const {
    if (!historyTabs) return nullptr;
    const int index = historyTabs->GetActiveTab();
    if (index < 0 || index >= HistoryTabCount) return nullptr;
    return historyFilers[index].get();
}

// ===== FAVORITES VIEW (Files | Folders | Apps) =====

void UltraFilerWindow::BuildFavoritesView() {
    favoritesPane = MakeLayoutBox("ufl-favorites-pane");
    favoritesPane->layout.SetFlexColumn()
                         .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    favoritesPane->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                             .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    favoritesTabs = std::make_shared<UltraCanvasTabbedContainer>("ufl-favorites-tabs");
    favoritesTabs->fontSize = static_cast<int>(kUiFontSize);
    favoritesTabs->SetTabHeight(30);
    favoritesTabs->SetTabMinWidth(90);
    favoritesTabs->SetCloseMode(TabCloseMode::NoClose);
    favoritesTabs->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                             .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    favoritesTabs->onTabChange = [this](int /*oldIndex*/, int /*newIndex*/) {
        UpdateStatusBar();
    };

    for (int i = 0; i < HistoryTabCount; ++i) {
        const std::string suffix = std::to_string(i);

        auto page = MakeLayoutBox("ufl-favorites-page-" + suffix);
        page->layout.SetFlexColumn()
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

        auto favFiler = CreateFilerWidget("ufl-favorites-filer-" + suffix, 0, 0, 0, 0);
        FilerStyle filerStyle = favFiler->GetStyle();
        filerStyle.fontSize = kUiFontSize;
        filerStyle.smallFontSize = kUiFontSize;
        filerStyle.folderIconScale = 0.7f;
        favFiler->SetStyle(filerStyle);
        favFiler->SetViewType(FilerViewType::ThumbnailsSmall);
        // The lists are handed over in pin order — the order the user made.
        favFiler->SetFileListOrderPreserved(true);
        // A pinned path was chosen deliberately, so it belongs in the list
        // even when it is a dotfile / a folder under one.
        favFiler->SetShowHiddenFiles(true);
        // The entries come from all over the filesystem, so the context menu
        // offers to open the folder an entry lives in.
        favFiler->SetOpenPathMenuItemVisible(true, "Open path (in new tab)");
        favFiler->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                            .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        favFiler->onSelectionChanged = [this](const std::vector<FilerEntry>&) {
            UpdateStatusBar();
        };
        favFiler->onFolderRefreshed = [this]() { UpdateStatusBar(); };
        // Opening a favorite is a real use, so it enters the History too.
        favFiler->onFileActivated = [this](const FilerEntry& entry) {
            RecordEntryInHistory(entry);
            RecordFolderInHistory(fs::path(entry.path).parent_path().string());
            OpenHistoryEntry(entry.path, false);
        };
        // A folder tile is activated by the widget itself (it navigates into
        // the folder); hand that folder to the browsing view instead of
        // browsing it inside the Favorites view.
        favFiler->onPathChanged = [this](const std::string& path) {
            OpenHistoryEntry(path, true);
        };
        favFiler->onOpenPath = [this](const FilerEntry& entry) {
            const std::string parent = fs::path(entry.path).parent_path().string();
            if (parent.empty()) return;
            ShowBrowsingView();
            AddNewTab(parent, true);
        };
        favFiler->onError = [this](const std::string& message) {
            if (statusLabel) statusLabel->SetText("Error: " + message);
        };
        favFiler->onSettings = [this]() { OpenSettingsDialog(); };
        favFiler->onPrint = [this](const std::vector<FilerEntry>& t) { HandlePrint(t); };
        favFiler->onShare = [this](const std::vector<FilerEntry>& t) { HandleShare(t); };
        favFiler->onAttributes = [this](const std::vector<FilerEntry>& t) { HandleAttributes(t); };
        favFiler->onAccess = [this](const std::vector<FilerEntry>& t) { HandleAccess(t); };
        favFiler->extrasMenuProvider = [this]() { return BuildExtrasMenuItems(); };

        page->AddChild(favFiler);
        favoritesFilers[i] = favFiler;
        favoritesTabs->AddTab(kHistoryTabTitles[i], page);
    }
    favoritesTabs->SetActiveTab(HistoryFiles);

    favoritesPane->AddChild(favoritesTabs);
    favoritesPane->SetVisible(false);   // the heart button turns it on
    contentBox->AddChild(favoritesPane);
}

void UltraFilerWindow::SetFavoritesVisible(bool visible) {
    if (visible == favoritesShown || !favoritesPane || !split) return;
    // Only one of the History / Favorites views can replace the split.
    if (visible) SetHistoryVisible(false);
    favoritesShown = visible;
    StyleToggleButton(favoritesButton.get(), favoritesShown);

    if (visible) {
        RefreshFavoritesTabs();
        split->SetVisible(false);
        favoritesPane->SetVisible(true);
    } else {
        favoritesPane->SetVisible(false);
        split->SetVisible(true);
    }
    UpdateStatusBar();
    UpdateWindowTitle();
}

void UltraFilerWindow::ShowBrowsingView() {
    SetHistoryVisible(false);
    SetFavoritesVisible(false);
}

void UltraFilerWindow::RefreshFavoritesTabs() {
    static const FilerFavoriteKind kinds[HistoryTabCount] = {
        FilerFavoriteKind::File, FilerFavoriteKind::Folder, FilerFavoriteKind::App};
    for (int i = 0; i < HistoryTabCount; ++i) {
        if (favoritesFilers[i]) favoritesFilers[i]->ShowFileList(favorites.Paths(kinds[i]));
    }
}

UltraCanvasFilerWidget* UltraFilerWindow::ActiveFavoritesFiler() const {
    if (!favoritesTabs) return nullptr;
    const int index = favoritesTabs->GetActiveTab();
    if (index < 0 || index >= HistoryTabCount) return nullptr;
    return favoritesFilers[index].get();
}

// ===== NAVIGATION =====

void UltraFilerWindow::NavigateTo(const std::string& path) {
    // Navigating shows a folder, so it always brings the folder display back.
    ShowBrowsingView();
    if (!filer || path.empty() || path == filer->GetPath()) return;
    filer->SetPath(path);
}

void UltraFilerWindow::NavigateBack() {
    ShowBrowsingView();
    FilerTabState* tab = ActiveTabState();
    if (!tab || tab->historyIndex == 0 || tab->history.empty()) return;
    tab->navigatingHistory = true;
    --tab->historyIndex;
    tab->filer->SetPath(tab->history[tab->historyIndex]);
    tab->navigatingHistory = false;
}

void UltraFilerWindow::NavigateForward() {
    ShowBrowsingView();
    FilerTabState* tab = ActiveTabState();
    if (!tab || tab->history.empty() || tab->historyIndex + 1 >= tab->history.size()) return;
    tab->navigatingHistory = true;
    ++tab->historyIndex;
    tab->filer->SetPath(tab->history[tab->historyIndex]);
    tab->navigatingHistory = false;
}

void UltraFilerWindow::NavigateUp() {
    if (!filer) return;
    const fs::path p(filer->GetPath());
    if (p.has_parent_path() && p.parent_path() != p)
        NavigateTo(p.parent_path().string());
}

void UltraFilerWindow::HandlePathChanged(FilerTabState* tab, const std::string& path) {
    if (!tab->navigatingHistory) {
        if (tab->historyIndex + 1 < tab->history.size())
            tab->history.erase(tab->history.begin() + tab->historyIndex + 1,
                               tab->history.end());
        if (tab->history.empty() || tab->history.back() != path) {
            tab->history.push_back(path);
            tab->historyIndex = tab->history.size() - 1;
        }
    }
    const int index = TabIndexOf(tab);
    if (index >= 0) tabbedContainer->SetTabTitle(index, TabTitleForPath(path));

    // Entering a folder ends a search-result display (SetPath leaves it).
    tab->searchQuery.clear();
    tab->filer->SetOpenPathMenuItemVisible(false);

    // Put back how this folder was last looked at. Done for every tab, not
    // only the active one, so a background tab is already right when it is
    // brought forward.
    ApplyFolderView(tab, path);

    if (!IsActiveTab(tab)) return;

    if (searchInput) searchInput->SetText("");

    if (breadcrumb) {
        BuildFolderBreadcrumb(breadcrumb.get(), path,
                              [this](const std::string& folder) { NavigateTo(folder); });
    }
    UpdateNavButtons();
    SyncTreeSelection(path);
    UpdateStatusBar();
    // Entering a folder clears the selection - fold the preview away.
    UpdatePreviewPane();
    UpdateWindowTitle();
}

void UltraFilerWindow::ApplyFolderView(FilerTabState* tab, const std::string& path) {
    if (!tab || !tab->filer || path.empty()) return;
    const FilerFolderView* stored = folderViews.Find(path);
    // A folder nobody has set up yet keeps whatever the previous one used —
    // carrying the last view forward is what makes browsing feel continuous.
    if (!stored) return;
    applyingFolderView = true;
    tab->filer->SetViewType(stored->view);
    tab->filer->SetSort(stored->sort, stored->ascending);
    applyingFolderView = false;
    // SetViewType / SetSort fire onViewTypeChanged / onSortChanged themselves,
    // so the command bar's dropdowns follow; the flag above only stops those
    // callbacks from recording the state straight back.
}

void UltraFilerWindow::RememberFolderView(FilerTabState* tab) {
    if (applyingFolderView || !tab || !tab->filer) return;
    // A search-result display is not a folder: it lists entries from many of
    // them, so how it is sorted belongs to no folder in particular.
    if (!tab->searchQuery.empty()) return;
    folderViews.Remember(tab->filer->GetPath(), tab->filer->GetViewType(),
                         tab->filer->GetSortField(),
                         tab->filer->IsSortAscending());
}

void UltraFilerWindow::UpdateSortOrderButton() {
    if (!sortOrderButton) return;
    const bool ascending = filer ? filer->IsSortAscending() : true;
    sortOrderButton->SetIcon(IconPath(ascending ? "sort-up.svg" : "sort-down.svg"));
    // The tooltip names the order that IS in effect (the icon shows it too) and
    // says what the click does, so neither reading can be mistaken for the other.
    sortOrderButton->SetTooltip(ascending
            ? "Ascending (A to Z, oldest first) - click to reverse"
            : "Descending (Z to A, newest first) - click to reverse");
    sortOrderButton->RequestRedraw();
}

void UltraFilerWindow::UpdateNavButtons() {
    FilerTabState* tab = ActiveTabState();
    if (backButton) backButton->SetDisabled(!tab || tab->historyIndex == 0);
    if (forwardButton)
        forwardButton->SetDisabled(!tab || tab->history.empty() ||
                                   tab->historyIndex + 1 >= tab->history.size());
    if (upButton && filer) {
        const fs::path p(filer->GetPath());
        upButton->SetDisabled(!p.has_parent_path() || p.parent_path() == p);
    }
}

// ===== STATUS BAR / PREVIEW =====

void UltraFilerWindow::UpdateStatusBar() {
    if (!statusLabel) return;
    if (historyShown) {
        const int index = historyTabs ? historyTabs->GetActiveTab() : -1;
        std::string text = "History";
        if (index >= 0 && index < HistoryTabCount)
            text += " - " + std::string(kHistoryTabTitles[index]);
        if (const UltraCanvasFilerWidget* hf = ActiveHistoryFiler())
            text += "    |    " + DescribeFilerContent(hf);
        statusLabel->SetText(text);
        return;
    }
    if (favoritesShown) {
        const int index = favoritesTabs ? favoritesTabs->GetActiveTab() : -1;
        std::string text = "Favorites";
        if (index >= 0 && index < HistoryTabCount)
            text += " - " + std::string(kHistoryTabTitles[index]);
        if (const UltraCanvasFilerWidget* ff = ActiveFavoritesFiler())
            text += "    |    " + DescribeFilerContent(ff);
        statusLabel->SetText(text);
        return;
    }
    if (!filer) return;
    std::string text = DescribeFilerContent(filer.get());
    // A live filter changes what the counts describe — say so.
    if (!filer->GetNameFilter().empty())
        text += "    |    filtered by \"" + filer->GetNameFilter() + "\"";
    statusLabel->SetText(text);
}

void UltraFilerWindow::UpdateWindowTitle() {
    if (!window) return;
    std::string title = "UltraFiler " ULTRAFILER_VERSION;
    if (historyShown) {
        title += " - History";
    } else if (favoritesShown) {
        title += " - Favorites";
    } else if (filer && !filer->GetPath().empty()) {
        title += " - " + filer->GetPath();
    }
    window->SetWindowTitle(title);
}

const FilerEntry* UltraFilerWindow::SingleSelectedEntry() const {
    if (!filer) return nullptr;
    const std::vector<size_t>& sel = filer->GetSelectionIndices();
    if (sel.size() != 1) return nullptr;
    const std::vector<FilerEntry>& entries = filer->GetEntries();
    if (sel.front() >= entries.size()) return nullptr;
    return &entries[sel.front()];
}

void UltraFilerWindow::SetPreviewEnabled(bool enabled) {
    if (enabled == previewEnabled) return;
    previewEnabled = enabled;
    StyleToggleButton(previewButton.get(), previewEnabled);
    ApplyPreviewSelectionPolicy();
    UpdatePreviewPane();
}

void UltraFilerWindow::ApplyPreviewSelectionPolicy() {
    for (auto& state : tabStates)
        if (state->filer) state->filer->SetSelectNextAfterDelete(previewEnabled);
}

void UltraFilerWindow::ArmFolderPreviewTimer(const std::string& folderPath) {
    if (pendingFolderPreviewPath == folderPath &&
        folderPreviewDelayTimer != InvalidTimerId) {
        return;   // already waiting for exactly this folder
    }
    auto* app = UltraCanvasApplication::GetInstance();
    if (!app) {
        // No timer source: show at once rather than never. The re-entry
        // proceeds because the folder is marked ready.
        folderPreviewReadyPath = folderPath;
        UpdatePreviewPane();
        return;
    }
    if (folderPreviewDelayTimer != InvalidTimerId)
        app->StopTimer(folderPreviewDelayTimer);
    pendingFolderPreviewPath = folderPath;
    folderPreviewDelayTimer = app->StartTimer(kFolderPreviewClickDelayMs, false,
            [this](TimerId) {
        folderPreviewDelayTimer = InvalidTimerId;
        const std::string path = pendingFolderPreviewPath;
        pendingFolderPreviewPath.clear();
        // Show only while the folder is STILL the single selection — a
        // double-click opened it (or the selection moved on) meanwhile.
        const FilerEntry* e = SingleSelectedEntry();
        if (!previewEnabled || !e || !e->isDirectory || e->path != path) return;
        folderPreviewReadyPath = path;
        UpdatePreviewPane();
    });
}

void UltraFilerWindow::CancelFolderPreviewTimer() {
    pendingFolderPreviewPath.clear();
    if (folderPreviewDelayTimer == InvalidTimerId) return;
    if (auto* app = UltraCanvasApplication::GetInstance())
        app->StopTimer(folderPreviewDelayTimer);
    folderPreviewDelayTimer = InvalidTimerId;
}

void UltraFilerWindow::UpdatePreviewPane() {
    if (!split || !preview || !folderPreview) return;
    // What the selection calls for: a single media file fills the pane with
    // the media viewer, a single folder with the folder preview filer
    // (showing that folder's content), anything else folds the pane away.
    std::string mediaPath;
    std::string folderPath;
    if (previewEnabled) {
        if (const FilerEntry* e = SingleSelectedEntry()) {
            if (e->isDirectory) folderPath = e->path;
            else if (UltraCanvasMediaViewer::IsSupportedMedia(e->path))
                mediaPath = e->path;
        }
    }
    const bool wantFolder = !folderPath.empty();
    if (wantFolder) {
        const bool alreadyShown = previewShown && previewShowsFolder &&
                                  folderPreview->GetPath() == folderPath;
        if (!alreadyShown && folderPath != folderPreviewReadyPath) {
            // This may be the first click of a double-click that OPENS the
            // folder: wait out the double-click interval before scanning the
            // folder into the pane. The pane keeps whatever it shows
            // meanwhile; the timer's firing comes back here with the folder
            // marked ready (see ArmFolderPreviewTimer).
            ArmFolderPreviewTimer(folderPath);
            return;
        }
    } else {
        // The selection moved off the folder — a pending or elapsed delay
        // belongs to something no longer selected.
        CancelFolderPreviewTimer();
        folderPreviewReadyPath.clear();
    }
    if (wantFolder || !mediaPath.empty()) {
        if (!previewShown) {
            // Pane sizing is weight-proportional, so plain AddPane would
            // shrink every pane — visibly moving the tree | filer splitter.
            // Capture the arranged widths first and hand the preview its
            // width from the filer pane only; the tree keeps its position.
            const int treeW  = static_cast<int>(split->GetPane(0)->GetWidth());
            const int filerW = static_cast<int>(split->GetPane(1)->GetWidth());

            previewShown = true;
            previewShowsFolder = wantFolder;
            previewPane = split->AddPane(1.4);
            split->SetPaneMinSize(split->PaneCount() - 1, kPreviewMinWidth);
            previewPane->layout.SetFlexColumn()
                               .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
            if (wantFolder) previewPane->AddChild(folderPreview);
            else            previewPane->AddChild(preview);

            if (treeW > 0 && filerW > 0) {
                // The new split line takes its thickness from the filer side
                // too, so the three sizes sum to exactly the available axis.
                const int line = split->EffectiveSplitterThickness();
                int previewW = previewPaneWidth > 0
                        ? previewPaneWidth
                        : static_cast<int>(std::lround(filerW * 1.4 / 4.1));
                previewW = std::max(previewW, kPreviewMinWidth);
                previewW = std::min(previewW, filerW - line - kFilerMinWidth);
                if (previewW >= kPreviewMinWidth)
                    split->SetPaneSizes({treeW, filerW - line - previewW, previewW});
                // else: too narrow for both minimums - let the weight
                // distribution and the min-size clamps sort it out.
            }
            // The narrowed folder display may now cut off the selected file
            // (the preview covers its spot) - keep it scrolled into view.
            if (filer) filer->EnsureSelectionVisible();
        } else if (previewShowsFolder != wantFolder) {
            // The pane is up but holds the wrong content — the selection
            // moved between a file and a folder. Swap the child; the pane
            // (and the width the user dragged it to) stays.
            if (wantFolder) {
                preview->CloseFile();
                previewPane->RemoveChild(preview);
                previewPane->AddChild(folderPreview);
            } else {
                previewPane->RemoveChild(folderPreview);
                previewPane->AddChild(preview);
            }
            previewShowsFolder = wantFolder;
        }
        if (wantFolder) {
            // SetPath rescans unconditionally, so only a real change goes
            // through it (the folder watch keeps an unchanged one fresh).
            if (folderPreview->GetPath() != folderPath)
                folderPreview->SetPath(folderPath);
        } else {
            if (preview->GetCurrentPath() != mediaPath) preview->OpenFile(mediaPath);
        }
    } else if (previewShown) {
        // Nothing to preview - give the folder display the whole width.
        previewShown = false;
        const int treeW  = static_cast<int>(split->GetPane(0)->GetWidth());
        const int filerW = static_cast<int>(split->GetPane(1)->GetWidth());
        const int prevW  = static_cast<int>(previewPane->GetWidth());
        if (prevW > 0) previewPaneWidth = prevW;   // restored on reopen
        if (previewShowsFolder) {
            previewPane->RemoveChild(folderPreview);
            previewShowsFolder = false;
        } else {
            // Let go of the file, not just of the playback: a document engine
            // that still holds the previewed file open blocks moving, renaming
            // or deleting it (on Windows an open handle refuses the rename
            // outright).
            preview->CloseFile();
            previewPane->RemoveChild(preview);
        }
        split->RemovePane(previewPane.get());
        previewPane.reset();
        // Return the preview's width (and its split line) to the filer pane
        // only, keeping the tree | filer splitter where the user put it.
        if (treeW > 0 && filerW > 0 && prevW > 0) {
            const int line = split->EffectiveSplitterThickness();
            split->SetPaneSizes({treeW, filerW + line + prevW});
        }
    }
}

} // namespace UltraCanvas
