// Apps/UltraFiler/UltraFilerWindow.cpp
// UltraFiler main window: Windows Explorer style file manager built from the
// UltraCanvas folder tree (UltraCanvasTreeView), tabbed folder content
// (UltraCanvasTabbedContainer + UltraCanvasFilerWidget per tab), a recursive
// search field and the media preview (UltraCanvasMediaViewer). The toolbar's
// clock button swaps that whole area for the History view — Files / Folders /
// Apps tabs listing the recently used paths (UltraFilerHistory) as small
// thumbnails; folders get there by being worked in (the filer's
// onFolderModified), not by being browsed. The heart button swaps the same
// area for the Favorites view — the same tabs, but listing what the user
// pinned through the menu bar's Pin > Favorites (UltraFilerFavorites).
// Pin > Treeview (folders only) pins into the folder tree's "Pinned" section,
// whose bookmark entries navigate on click; the tree's context menu offers
// Copy / Delete / Paste on folders and Unpin on pinned entries. The Settings
// menu opens the settings window (UltraFilerSettingsDialog) and clears the
// history / the favorites; persisted settings load at startup and configure
// the preview's transparent-image backdrop. Esc closes the History or
// Favorites view, or an open media preview.
// Version: 1.7.0
// Last Modified: 2026-08-10
// Author: UltraCanvas Framework

#include "UltraFilerWindow.h"

#include "UltraCanvasAlert.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasFileAssociations.h"
#include "UltraCanvasUtils.h"
#include "UltraFilerPrompt.h"
#include "UltraFilerSettingsDialog.h"
#ifdef ULTRACANVAS_HAS_ULTRAWIN
#include "UltraWin/UltraWin.h"
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <unordered_set>

namespace fs = std::filesystem;

namespace UltraCanvas {

namespace {

    // Suffix marking the lazy placeholder child of an unexpanded folder node.
    constexpr const char* kPlaceholderSuffix = "\n#placeholder";

    // Node id of the folder tree's "Pinned" section header, and the id prefix
    // of its bookmark children ("ufl-pin:" + folder path). The prefix keeps a
    // pinned folder's node id distinct from the same folder's regular node,
    // whose id is the bare path.
    constexpr const char* kPinnedNodeId = "ufl-pinned";
    constexpr const char* kPinnedChildPrefix = "ufl-pin:";
    constexpr size_t kPinnedChildPrefixLen = 8;   // strlen(kPinnedChildPrefix)

    // Single UI font size (pt) used by every element of the window.
    constexpr float kUiFontSize = 9.0f;

    // Split-pane minimum widths (px): the folder display and the media
    // preview never get narrower than these.
    constexpr int kFilerMinWidth   = 360;
    constexpr int kPreviewMinWidth = 260;

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

    // Does `path` contain at least one visible subfolder? (Cheap check that
    // decides whether a tree node gets an expand button.)
    // "Hidden" is the platform's own notion (IsHiddenFileSystemEntry): dot
    // names everywhere, plus the hidden attribute on Windows - which is what
    // keeps AppData and the "Anwendungsdaten"-style compatibility junctions
    // of a profile folder out of the tree - and UF_HIDDEN on macOS.
    bool HasSubdirectories(const std::string& path) {
        std::error_code ec;
        fs::directory_iterator it(path, fs::directory_options::skip_permission_denied, ec);
        if (ec) return false;
        for (const fs::directory_entry& e : it) {
            std::error_code dec;
            if (e.is_directory(dec) && !dec && !IsHiddenFileSystemEntry(e.path()))
                return true;
        }
        return false;
    }

    // Visible subfolders of `path`, sorted case-insensitively by name.
    std::vector<fs::path> ListSubdirectories(const std::string& path) {
        std::vector<fs::path> dirs;
        std::error_code ec;
        fs::directory_iterator it(path, fs::directory_options::skip_permission_denied, ec);
        if (ec) return dirs;
        for (const fs::directory_entry& e : it) {
            std::error_code dec;
            if (e.is_directory(dec) && !dec && !IsHiddenFileSystemEntry(e.path()))
                dirs.push_back(e.path());
        }
        std::sort(dirs.begin(), dirs.end(), [](const fs::path& a, const fs::path& b) {
            std::string an = a.filename().string(), bn = b.filename().string();
            std::transform(an.begin(), an.end(), an.begin(), ::tolower);
            std::transform(bn.begin(), bn.end(), bn.begin(), ::tolower);
            return an < bn;
        });
        return dirs;
    }

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

    // Is `path` equal to `folder` or somewhere below it?
    bool IsPathInside(const std::string& path, const std::string& folder) {
        if (folder.empty() || path.size() < folder.size()) return false;
        if (path.compare(0, folder.size(), folder) != 0) return false;
        return path.size() == folder.size() || path[folder.size()] == '/' ||
               path[folder.size()] == '\\' ||
               folder.back() == '/' || folder.back() == '\\';
    }

    // First free "name", "name (2)", "name (3)", ... inside `folder` — the
    // same naming the filer widget uses when pasting.
    std::string UniqueNameIn(const std::string& folder, const std::string& baseName) {
        fs::path base(baseName);
        const std::string stem = base.stem().string();
        const std::string ext = base.extension().string();   // includes the dot
        const fs::path dir(folder);
        fs::path candidate = dir / baseName;
        std::error_code ec;
        int n = 2;
        while (fs::exists(candidate, ec)) {
            candidate = dir / (stem + " (" + std::to_string(n++) + ")" + ext);
        }
        return candidate.string();
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
    StopSubfolderProbeWorker();
}

bool UltraFilerWindow::Initialize(const std::string& startFolder) {
    WindowConfig config;
    config.title = "UltraFiler";
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
        SetPreviewEnabled(false);
        return true;
    }, { UCEventType::KeyDown });

    preview = CreateMediaViewer("ufl-preview", 0, 0, 0, 0);
    // The pane is added / removed as the selection changes; the viewer must
    // not steal the keyboard focus from the filer on every appearance.
    preview->SetGrabFocusOnAttach(false);
    // The filer provides the navigation; the preview shows only the media
    // (no breadcrumb / toolbar rows above the image).
    preview->SetTopBarsVisible(false);

    // Persisted settings (transparent-image backdrop of the preview, ...) and
    // the recently used files / folders / applications behind the clock button.
    settings.Load();
    ApplySettings();
    history.Load();
    favorites.Load();

    BuildTabbedContainer();

    window->AddChild(BuildMenuBar());
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

// ===== MENU BAR =====

std::shared_ptr<UltraCanvasMenu> UltraFilerWindow::BuildMenuBar() {
    MenuStyle style = MenuStyle::Default();
    style.backgroundColor = Color(249, 249, 251, 255);
    style.font.fontSize = kUiFontSize;

    menuBar = MenuBuilder("ufl-menubar", 0, 0, 0, 24)
            .SetType(MenuType::Menubar)
            .SetStyle(style)
            .AddSubmenu("Settings", {
                    MenuItemData::Action("Settings...",
                            [this]() { OpenSettingsDialog(); }),
                    MenuItemData::Separator(),
                    MenuItemData::Action("Clear History", [this]() {
                        history.ClearAll();
                        if (historyShown) {
                            RefreshHistoryTabs();
                            UpdateStatusBar();
                        }
                    }),
                    MenuItemData::Action("Clear Favorites", [this]() {
                        favorites.ClearAll();
                        RefreshPinnedTreeNodes();
                        if (favoritesShown) {
                            RefreshFavoritesTabs();
                            UpdateStatusBar();
                        }
                    }),
            })
            // The submenu is provided by a lambda so the two items' enabled
            // state can follow the selection at the moment the menu opens.
            .AddSubmenu("Pin", [this]() { return BuildPinMenuItems(); })
            .AddSubmenu("Extras", {
                    MenuItemData::Action("Open prompt",
                            [this]() { OpenSystemPrompt(); }),
            })
            .Build();
    menuBar->size.height = CSSLayout::Dimension::Px(24);
    menuBar->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                       .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    return menuBar;
}

// ===== SETTINGS =====

void UltraFilerWindow::ApplySettings() {
    if (!preview) return;
    preview->SetTransparentBackground(settings.previewCheckeredBackground
            ? TransparentImageBackground::Checkered
            : TransparentImageBackground::SolidColor);
    preview->SetTransparentColor(settings.previewTransparentColor);
}

void UltraFilerWindow::OpenSettingsDialog() {
    UltraFilerSettingsDialog::Show(window.get(), &settings,
                                   [this]() { ApplySettings(); });
}

// ===== EXTRAS =====

void UltraFilerWindow::OpenSystemPrompt() {
    // The prompt opens in the folder the active tab is showing, so the shell
    // starts where the user is looking.
    std::string folder = filer ? filer->GetPath() : std::string();
    if (folder.empty()) folder = UserHomeDir();

    std::string error;
    if (!UltraFilerPrompt::Launch(settings.promptApplication, folder, error))
        UltraCanvasAlert::Error(error, "Open prompt", nullptr, window.get());
}

// ===== PIN MENU (menu bar: Pin > Favorites / Treeview) =====

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

std::vector<MenuItemData> UltraFilerWindow::BuildPinMenuItems() {
    const std::vector<FilerEntry> targets = PinTargets();
    const bool allFolders = !targets.empty() &&
            std::all_of(targets.begin(), targets.end(),
                        [](const FilerEntry& e) { return e.isDirectory; });

    MenuItemData favoritesItem = MenuItemData::Action(
            "Favorites", [this]() { PinTargetsToFavorites(); });
    favoritesItem.enabled = !targets.empty();

    // Only a folder can live in the folder tree.
    MenuItemData treeItem = MenuItemData::Action(
            "Treeview", [this]() { PinTargetsToTree(); });
    treeItem.enabled = allFolders;

    return {favoritesItem, treeItem};
}

void UltraFilerWindow::PinTargetsToFavorites() {
    bool changed = false;
    for (const FilerEntry& e : PinTargets()) {
        const FilerFavoriteKind kind = e.isDirectory ? FilerFavoriteKind::Folder
                : IsApplicationEntry(e) ? FilerFavoriteKind::App
                                        : FilerFavoriteKind::File;
        changed |= favorites.Pin(kind, e.path);
    }
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
    // Show the new entry rather than leaving it behind a collapsed header.
    if (TreeNode* pinned = folderTree ? folderTree->FindNode(kPinnedNodeId) : nullptr) {
        folderTree->ExpandNode(pinned);
        folderTree->RequestRedraw();
    }
}

// ===== NAVIGATION ROW ("+" / Back / Forward / Up / Refresh + breadcrumb) =====

std::shared_ptr<UltraCanvasContainer> UltraFilerWindow::BuildNavigationRow() {
    auto row = MakeToolRow("ufl-nav-row");
    row->SetPadding(6, 8, 2, 8);

    // "+" on the left side of the toolbar opens an additional tab showing the
    // current folder.
    auto newTabButton = MakeToolButton("ufl-new-tab", "+", "", 30,
            [this]() {
        std::string path = filer ? filer->GetPath() : std::string();
        if (path.empty()) path = UserHomeDir();
        AddNewTab(path, true);
    });
    newTabButton->SetFontSize(kUiFontSize);
    row->AddChild(newTabButton);

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

    // Recursive name search under the current folder; Enter runs it, an
    // empty query returns to the normal folder display.
    searchInput = CreateTextInput("ufl-search", 0, 0, 200, 26);
    searchInput->SetFontSize(kUiFontSize);
    searchInput->SetPlaceholder("Search");
    searchInput->onEnterPressed = [this](const std::string& text) {
        RunSearch(text);
        return true;
    };
    searchInput->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                           .SetAlignSelf(CSSLayout::AlignSelf::Center);
    row->AddChild(searchInput);

    return row;
}

// ===== SEARCH =====

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

// ===== COMMAND BAR (New / clipboard / rename / delete / sort / view / preview) =====

std::shared_ptr<UltraCanvasContainer> UltraFilerWindow::BuildCommandBar() {
    auto row = MakeToolRow("ufl-command-bar");
    row->SetPadding(2, 8, 6, 8);
    row->SetBorderBottom(1, Color(225, 225, 230, 255));

    // The file commands work on the folder display and its selection, so each
    // of them leaves the History / Favorites views first - the change they
    // make has to be visible (an inline rename editor especially).
    row->AddChild(MakeToolButton("ufl-new-folder", "New folder", "add-folder.svg", 0,
            [this]() {
        ShowBrowsingView();
        if (!filer) return;
        const fs::path folder(filer->GetPath());
        fs::path candidate = folder / "New folder";
        int n = 2;
        std::error_code ec;
        while (fs::exists(candidate, ec))
            candidate = folder / ("New folder (" + std::to_string(n++) + ")");
        fs::create_directory(candidate, ec);
        if (ec) {
            if (statusLabel) statusLabel->SetText("Error: cannot create folder");
            return;
        }
        // The folder is created here rather than by the widget, so the
        // History record the widget would fire has to be made here too.
        RecordFolderInHistory(folder.string());
        filer->Refresh();
        const auto& entries = filer->GetEntries();
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].path == candidate.string()) { filer->StartRename(i); break; }
        }
    }));
    row->AddChild(MakeToolButton("ufl-new-file", "New file", "add-document.svg", 0,
            [this]() {
        ShowBrowsingView();
        if (filer) filer->CreateNewDocument({"Text", "txt", ""});
    }));

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

    // Sort field + direction. The dropdown mirrors FilerSortField order.
    auto sortLbl = std::make_shared<UltraCanvasLabel>("ufl-sort-lbl", 0, 0, 42, 24);
    sortLbl->SetText("Sort");
    sortLbl->SetFontSize(kUiFontSize);
    sortLbl->SetAlignment(TextAlignment::Right, VerticalAlignment::Middle);
    row->AddChild(sortLbl);

    sortDropdown = CreateDropdown("ufl-sort", 0, 0, 104, 26);
    ApplyDropdownFontSize(sortDropdown.get());
    sortDropdown->AddItem("Name");
    sortDropdown->AddItem("Size");
    sortDropdown->AddItem("Type");
    sortDropdown->AddItem("Modified");
    sortDropdown->AddItem("Created");
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

    auto orderButton = MakeToolButton("ufl-sort-order", "", "sort-alpha-down.svg", 30,
            [this]() {
        if (filer) filer->SetSortAscending(!filer->IsSortAscending());
    });
    row->AddChild(orderButton);

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

    TreeNode* root = folderTree->SetRootNode(
            MakeFolderNodeData("ufl-computer", "Computer", "computer.png"));
    // The root's children are the roots below, not a folder listing - never
    // let EnsureTreeChildren try to scan "ufl-computer" as a path.
    treeChildrenLoaded.insert("ufl-computer");

    // The "Pinned" section on top: bookmark entries for the folders pinned
    // through Pin > Treeview. Its children are managed by
    // RefreshPinnedTreeNodes, never by a folder scan.
    folderTree->AddNode("ufl-computer",
            MakeFolderNodeData(kPinnedNodeId, "Pinned", "rating-heart-on.svg"));
    treeChildrenLoaded.insert(kPinnedNodeId);
    RefreshPinnedTreeNodes();

    const std::string home = UserHomeDir();
    if (!home.empty()) {
        AddTreeFolderNode("ufl-computer", home, "Home", "home-icon.png");
    }

#ifdef _WIN32
    // ListDriveRoots() reads the mount table in one call. Probing every letter
    // with is_directory() instead spins up empty optical drives and waits out
    // the timeout of each disconnected network mapping before the window shows.
    for (const std::string& drive : ListDriveRoots()) {
        std::string label = fs::path(drive).root_name().string();  // "C:"
        if (label.empty()) label = drive;
        AddTreeFolderNode("ufl-computer", drive, label, "drive.png");
    }
#else
    AddTreeFolderNode("ufl-computer", "/", "File System", "drive.png");
    // Removable / additional volumes.
    for (const std::string base : {std::string("/media"), std::string("/mnt")}) {
        for (const fs::path& mount : ListSubdirectories(base)) {
            // /media holds one folder per user with the volumes below it.
            if (base == "/media") {
                auto volumes = ListSubdirectories(mount.string());
                for (const fs::path& vol : volumes)
                    AddTreeFolderNode("ufl-computer", vol.string(),
                                      vol.filename().string(), "drive.png");
            } else {
                AddTreeFolderNode("ufl-computer", mount.string(),
                                  mount.filename().string(), "drive.png");
            }
        }
    }
#endif

    if (root) root->Expand();

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

void UltraFilerWindow::EnsureTreeChildren(TreeNode* node) {
    if (!node) return;
    const std::string path = node->data.nodeId;
    // Once per node: the placeholder is only a hint that a scan is due, and a
    // node may reach this before its probe has even added one.
    if (!treeChildrenLoaded.insert(path).second) return;
    // The home folder leads with its well-known folders - Desktop, Documents,
    // Downloads, ... resolved through the platform (SHGetKnownFolderPath /
    // xdg-user-dirs), so a redirected Documents folder is found too - each
    // with its own icon, like the Explorer and Finder sidebars. The remaining
    // visible folders follow alphabetically; a well-known folder that
    // physically sits in the home folder is not listed a second time.
    std::unordered_set<std::string> curated;
    if (path == UserHomeDir()) {
        for (const UserFolderInfo& f : GetWellKnownUserFolders()) {
            AddTreeFolderNode(path, f.path, f.label, UserFolderIconFile(f.kind));
            curated.insert(FolderIdentityKey(f.path));
        }
    }
    // Add the real children before removing the placeholder: a node whose
    // last child is removed is demoted to a leaf, which drops its expanded
    // state and made the first expansion of a folder appear to do nothing.
    for (const fs::path& dir : ListSubdirectories(path)) {
        if (!curated.empty() && curated.count(FolderIdentityKey(dir.string())))
            continue;
        AddTreeFolderNode(path, dir.string(), dir.filename().string(), "folder-brown.svg");
    }
    folderTree->RemoveNode(PlaceholderId(path));
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

        // The directory open - outside the lock, off the UI thread.
        const bool has = HasSubdirectories(path);
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
    if (id == "ufl-computer" || id == kPinnedNodeId) return {};
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
    folderTree->RequestRedraw();
}

void UltraFilerWindow::ShowTreeContextMenu(TreeNode* node, const UCEvent& event) {
    if (!node || !window) return;
    const std::string target = TreeNodeTargetPath(node);
    const bool isFolder = !target.empty();
    const std::string& id = node->data.nodeId;
    const bool isPinnedEntry =
            id.compare(0, kPinnedChildPrefixLen, kPinnedChildPrefix) == 0;
    // Home, File System and the drive roots sit directly under Computer;
    // deleting one of those from a context menu would be a catastrophe, so
    // they keep Delete disabled.
    const bool isTopLevelRoot = !isPinnedEntry && node->parent &&
            node->parent->data.nodeId == "ufl-computer";

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
    treeContextMenu->AddItem(unpinItem);
    treeContextMenu->OpenMenu(event.pointerWindow, *window, PopupElementSettings());
}

void UltraFilerWindow::PasteIntoFolder(const std::string& folder) {
    std::vector<std::string> paths;
    bool cut = false;
    if (UltraCanvasClipboard* cb = GetClipboard()) cb->GetFiles(paths, cut);
    if (paths.empty()) return;

    std::error_code ec;
    if (!fs::is_directory(folder, ec) || ec) {
        if (statusLabel) statusLabel->SetText("Error: paste target is not a folder: " + folder);
        return;
    }
    bool changed = false;
    for (const std::string& src : paths) {
        const fs::path from(src);
        if (!fs::exists(from, ec)) continue;
        // Cut-pasting into the folder the file already lives in is a no-op,
        // and a folder must never be pasted into itself.
        if (cut && from.parent_path() == fs::path(folder)) continue;
        if (fs::is_directory(from, ec) && IsPathInside(folder, src)) {
            if (statusLabel) statusLabel->SetText("Error: cannot paste a folder into itself: " + src);
            continue;
        }
        const std::string dest = UniqueNameIn(folder, from.filename().string());
        if (cut) {
            fs::rename(from, dest, ec);
            if (ec) {   // cross-device move: copy + delete
                ec.clear();
                fs::copy(from, dest, fs::copy_options::recursive, ec);
                if (!ec) fs::remove_all(from, ec);
            }
        } else {
            fs::copy(from, dest, fs::copy_options::recursive, ec);
        }
        if (ec) {
            if (statusLabel) statusLabel->SetText("Error: paste failed for " + src + ": " + ec.message());
            ec.clear();
        } else {
            changed = true;
        }
    }
    if (!changed) return;
    // Pasting is work done in the folder, exactly like a paste in the filer.
    RecordFolderInHistory(folder);
    for (auto& state : tabStates) {
        if (state->filer && state->filer->GetPath() == folder)
            state->filer->Refresh();
    }
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
    tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>("ufl-tabs");
    tabbedContainer->fontSize = static_cast<int>(kUiFontSize);
    tabbedContainer->SetTabHeight(30);
    tabbedContainer->SetTabMinWidth(90);
    tabbedContainer->SetCloseMode(TabCloseMode::Closable);
    tabbedContainer->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                               .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

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

    state->filer = CreateFilerWidget("ufl-filer-" + suffix, 0, 0, 0, 0);
    FilerStyle filerStyle = state->filer->GetStyle();
    filerStyle.fontSize = kUiFontSize;
    filerStyle.smallFontSize = kUiFontSize;
    filerStyle.folderIconScale = 0.7f;
    state->filer->SetStyle(filerStyle);
    state->filer->SetViewType(FilerViewType::ThumbnailsMedium);
    // With the preview up, a delete of the previewed file moves the selection
    // (and with it the preview) on to the next entry instead of emptying it.
    state->filer->SetSelectNextAfterDelete(previewEnabled);
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
        // Windows executables go to the UltraWin emulation layer, not to the
        // host's file associations.
        if (entry.extension == "exe") {
            LaunchWindowsExecutable(entry);
            return;
        }
#endif
        if (!UltraCanvasMediaViewer::IsSupportedMedia(entry.path)) {
            // Not previewable: launch it with the OS default application
            // (Explorer semantics). Only real files — an entry inside an
            // archive is a virtual path no external application can read.
            std::error_code ec;
            if (!fs::is_regular_file(entry.path, ec) || ec) return;
            std::string error;
            if (!FileAssociations::OpenWithDefaultApplication({entry.path}, error)) {
                if (statusLabel) statusLabel->SetText("Error: " + error);
            }
            return;
        }
        // Double-click / Enter opens the file in the preview, un-hiding it
        // when needed.
        if (!previewEnabled) SetPreviewEnabled(true);
        else UpdatePreviewPane();
    };
    tab->filer->onSortChanged = [this, tab](FilerSortField field, bool /*ascending*/) {
        if (!IsActiveTab(tab) || !sortDropdown) return;
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
}

void UltraFilerWindow::HandleTabSwitched(int index) {
    if (index < 0 || index >= (int)tabStates.size()) return;
    FilerTabState* tab = tabStates[index].get();
    if (!tab->filer) return;
    filer = tab->filer;

    const std::string path = filer->GetPath();
    if (breadcrumb && !path.empty()) {
        BuildFolderBreadcrumb(breadcrumb.get(), path,
                              [this](const std::string& folder) { NavigateTo(folder); });
    }
    if (searchInput) searchInput->SetText(tab->searchQuery);
    UpdateNavButtons();
    if (!path.empty()) SyncTreeSelection(path);
    UpdateStatusBar();
    UpdateWindowTitle();

    // Mirror the tab's sort / view settings into the command bar.
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
    treePane->layout.SetFlexColumn()
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    folderTree->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                          .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    treePane->AddChild(folderTree);

    auto filerPane = split->AddPane(2.7);
    split->SetPaneMinSize(1, kFilerMinWidth);
    filerPane->layout.SetFlexColumn()
                     .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    filerPane->AddChild(tabbedContainer);

    preview->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                       .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    // The preview pane is added by UpdatePreviewPane once a previewable file
    // is selected; until then the folder display uses the whole width.

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

    // Per-app environment named after the executable, so each program keeps
    // its own isolated prefix (UltraWin creates it on first launch).
    std::string envName = fs::path(entry.name).stem().string();
    for (char& c : envName) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' &&
            c != '_' && c != '-')
            c = '_';
    }
    while (!envName.empty() && envName.front() == '.') envName.erase(0, 1);
    if (envName.size() > 64) envName.resize(64);
    if (envName.empty()) envName = "Default";

    const bool firstLaunch = !UltraWin_EnvironmentExists(envName);
    if (statusLabel)
        statusLabel->SetText(
            "Launching " + entry.name +
            (firstLaunch ? " — first launch prepares its Windows environment, "
                           "this can take a minute…"
                         : "…"));

    // Launch off the UI thread: a first launch runs wineboot. The thread
    // only touches the window through PostToUIThread, guarded by the same
    // alive flag the subfolder probe worker uses.
    auto alive = probeAlive;
    std::thread([this, alive, path = entry.path, name = entry.name,
                 envName]() {
        UltraWinRunOptions options;
        options.environment = envName;
        UltraWinHandle handle = UltraWinInvalidHandle;
        auto result = UltraWin_RunApp(path, options, &handle);

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
    statusLabel->SetText(DescribeFilerContent(filer.get()));
}

void UltraFilerWindow::UpdateWindowTitle() {
    if (!window) return;
    if (historyShown) {
        window->SetWindowTitle("History - UltraFiler");
        return;
    }
    if (favoritesShown) {
        window->SetWindowTitle("Favorites - UltraFiler");
        return;
    }
    const std::string path = filer ? filer->GetPath() : std::string();
    if (path.empty()) return;
    const std::string name = fs::path(path).filename().string();
    window->SetWindowTitle((name.empty() ? path : name) + " - UltraFiler");
}

std::string UltraFilerWindow::PreviewablePathForSelection() const {
    if (!filer) return {};
    const std::vector<size_t>& sel = filer->GetSelectionIndices();
    if (sel.size() != 1) return {};
    const std::vector<FilerEntry>& entries = filer->GetEntries();
    if (sel.front() >= entries.size()) return {};
    const FilerEntry& e = entries[sel.front()];
    if (e.isDirectory) return {};
    if (!UltraCanvasMediaViewer::IsSupportedMedia(e.path)) return {};
    return e.path;
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

void UltraFilerWindow::UpdatePreviewPane() {
    if (!split || !preview) return;
    const std::string path = previewEnabled ? PreviewablePathForSelection()
                                            : std::string();
    if (!path.empty()) {
        if (!previewShown) {
            // Pane sizing is weight-proportional, so plain AddPane would
            // shrink every pane — visibly moving the tree | filer splitter.
            // Capture the arranged widths first and hand the preview its
            // width from the filer pane only; the tree keeps its position.
            const int treeW  = static_cast<int>(split->GetPane(0)->GetWidth());
            const int filerW = static_cast<int>(split->GetPane(1)->GetWidth());

            previewShown = true;
            previewPane = split->AddPane(1.4);
            split->SetPaneMinSize(split->PaneCount() - 1, kPreviewMinWidth);
            previewPane->layout.SetFlexColumn()
                               .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
            previewPane->AddChild(preview);

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
        }
        if (preview->GetCurrentPath() != path) preview->OpenFile(path);
    } else if (previewShown) {
        // Nothing to preview - give the folder display the whole width.
        previewShown = false;
        const int treeW  = static_cast<int>(split->GetPane(0)->GetWidth());
        const int filerW = static_cast<int>(split->GetPane(1)->GetWidth());
        const int prevW  = static_cast<int>(previewPane->GetWidth());
        if (prevW > 0) previewPaneWidth = prevW;   // restored on reopen
        preview->StopPlayback();
        previewPane->RemoveChild(preview);
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
