// Apps/UltraFiler/UltraFilerWindow.cpp
// UltraFiler main window: Windows Explorer style file manager built from the
// UltraCanvas folder tree (UltraCanvasTreeView), tabbed folder content
// (UltraCanvasTabbedContainer + UltraCanvasFilerWidget per tab), a recursive
// search field and the media preview (UltraCanvasMediaViewer). The Settings
// menu opens the settings window (UltraFilerSettingsDialog); persisted
// settings load at startup and configure the preview's transparent-image
// backdrop. Esc closes an open media preview.
// Version: 1.4.2
// Last Modified: 2026-08-08
// Author: UltraCanvas Framework

#include "UltraFilerWindow.h"

#include "UltraCanvasAlert.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasUtils.h"
#include "UltraFilerSettingsDialog.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace UltraCanvas {

namespace {

    // Suffix marking the lazy placeholder child of an unexpanded folder node.
    constexpr const char* kPlaceholderSuffix = "\n#placeholder";

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

    bool IsHiddenName(const std::string& name) {
        return !name.empty() && name.front() == '.';
    }

    // Does `path` contain at least one visible subfolder? (Cheap check that
    // decides whether a tree node gets an expand button.)
    bool HasSubdirectories(const std::string& path) {
        std::error_code ec;
        fs::directory_iterator it(path, fs::directory_options::skip_permission_denied, ec);
        if (ec) return false;
        for (const fs::directory_entry& e : it) {
            std::error_code dec;
            if (e.is_directory(dec) && !dec && !IsHiddenName(e.path().filename().string()))
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
            if (e.is_directory(dec) && !dec && !IsHiddenName(e.path().filename().string()))
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

    // Esc closes the media preview, wherever the keyboard focus sits (the
    // filer usually holds it after the click that selected the file). The
    // filter steps back while another interaction claims the key: an open
    // popup / menu, the filer's inline rename, item drag or compress dialog,
    // and the search field.
    window->InstallEventFilter("ufl-preview-escape",
            [this](const UCEvent& e) -> bool {
        if (e.virtualKey != UCKeys::Escape || !previewShown) return false;
        if (window->GetActivePopupElement()) return false;
        if (filer && filer->WantsEscapeKey()) return false;
        if (window->GetFocusedElement() == searchInput.get()) return false;
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

    // Persisted settings (transparent-image backdrop of the preview, ...).
    settings.Load();
    ApplySettings();

    BuildTabbedContainer();

    window->AddChild(BuildMenuBar());
    window->AddChild(BuildNavigationRow());
    window->AddChild(BuildCommandBar());

    BuildFolderTree();
    BuildSplitLayout();

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
    auto refresh = MakeToolButton("ufl-refresh", "", "reload.svg", 30,
                                  [this]() { if (filer) filer->Refresh(); });
    row->AddChild(backButton);
    row->AddChild(forwardButton);
    row->AddChild(upButton);
    row->AddChild(refresh);

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
        if (IsHiddenName(name)) {
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

    row->AddChild(MakeToolButton("ufl-new-folder", "New folder", "add-folder.svg", 0,
            [this]() {
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
        filer->Refresh();
        const auto& entries = filer->GetEntries();
        for (size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].path == candidate.string()) { filer->StartRename(i); break; }
        }
    }));
    row->AddChild(MakeToolButton("ufl-new-file", "New file", "add-document.svg", 0,
            [this]() {
        if (filer) filer->CreateNewDocument({"Text", "txt", ""});
    }));

    auto sep1 = std::make_shared<UltraCanvasLabel>("ufl-sep1", 0, 0, 9, 24);
    sep1->SetText("|");
    sep1->SetFontSize(kUiFontSize);
    sep1->SetTextColor(Color(200, 200, 206, 255));
    row->AddChild(sep1);

    row->AddChild(MakeToolButton("ufl-cut", "", "scissors.svg", 30,
            [this]() { if (filer) filer->CutSelection(); }));
    row->AddChild(MakeToolButton("ufl-copy", "", "copy.svg", 30,
            [this]() { if (filer) filer->CopySelection(); }));
    row->AddChild(MakeToolButton("ufl-paste", "", "clipboard-list.svg", 30,
            [this]() { if (filer) filer->Paste(); }));
    row->AddChild(MakeToolButton("ufl-rename", "", "edit.svg", 30,
            [this]() {
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

    const std::string home = UserHomeDir();
    if (!home.empty()) {
        AddTreeFolderNode("ufl-computer", home, "Home", "folder-open.svg");
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
        const std::string& path = node->data.nodeId;
        std::error_code ec;
        if (fs::is_directory(path, ec) && !ec) NavigateTo(path);
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
    // Add the real children before removing the placeholder: a node whose
    // last child is removed is demoted to a leaf, which drops its expanded
    // state and made the first expansion of a folder appear to do nothing.
    for (const fs::path& dir : ListSubdirectories(path))
        AddTreeFolderNode(path, dir.string(), dir.filename().string(), "folder.png");
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
        if (!IsActiveTab(tab) || entry.isDirectory) return;
        if (!UltraCanvasMediaViewer::IsSupportedMedia(entry.path)) return;
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
    if (window && !path.empty()) {
        const std::string name = fs::path(path).filename().string();
        window->SetWindowTitle((name.empty() ? path : name) + " - UltraFiler");
    }

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

    window->AddChild(split);
}

// ===== NAVIGATION =====

void UltraFilerWindow::NavigateTo(const std::string& path) {
    if (!filer || path.empty() || path == filer->GetPath()) return;
    filer->SetPath(path);
}

void UltraFilerWindow::NavigateBack() {
    FilerTabState* tab = ActiveTabState();
    if (!tab || tab->historyIndex == 0 || tab->history.empty()) return;
    tab->navigatingHistory = true;
    --tab->historyIndex;
    tab->filer->SetPath(tab->history[tab->historyIndex]);
    tab->navigatingHistory = false;
}

void UltraFilerWindow::NavigateForward() {
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
    if (window) {
        const std::string name = fs::path(path).filename().string();
        window->SetWindowTitle((name.empty() ? path : name) + " - UltraFiler");
    }
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
    if (!statusLabel || !filer) return;
    // Indices, not GetSelectedEntries(): this runs on every selection change
    // (each rubber-band mouse move included) and the entry copy scaled with
    // the selection size.
    const std::vector<FilerEntry>& entries = filer->GetEntries();
    const std::vector<size_t>& sel = filer->GetSelectionIndices();
    std::string text = std::to_string(entries.size())
                     + (entries.size() == 1 ? " item" : " items");
    if (!sel.empty()) {
        uint64_t bytes = 0;
        for (size_t idx : sel)
            if (idx < entries.size() && !entries[idx].isDirectory)
                bytes += entries[idx].size;
        text += "    |    " + std::to_string(sel.size())
              + (sel.size() == 1 ? " item selected" : " items selected");
        if (bytes > 0) text += " (" + FormatFileSize(bytes) + ")";
    }
    statusLabel->SetText(text);
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
    UpdatePreviewPane();
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
