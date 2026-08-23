// Apps/UltraFiler/UltraFilerWindow.h
// UltraFiler - file manager main window (Windows Explorer style layout):
// a navigation row ("+" new-tab button, Back / Forward / Up / Refresh, the
// History clock toggle, the Favorites heart toggle, folder breadcrumb, and
// the settings gear at the far right opening the settings window), a command
// bar (New folder / New file, Cut / Copy / Paste / Rename / Delete, the
// recursive search field, Sort and
// View dropdowns, video preview mode, Preview toggle), a three-pane split with
// the lazy folder tree (UltraCanvasTreeView), the tabbed folder content display
// (UltraCanvasTabbedContainer hosting one UltraCanvasFilerWidget per tab) and
// the media preview (UltraCanvasMediaViewer, shown only while a previewable
// file is selected; Esc closes it), plus a status bar describing the folder
// and the selection (kept in step with the folder listing through the
// filer's onFolderRefreshed callback).
// The clock button swaps the whole tree + folder area for the History view: a
// tabbed container (Files / Folders / Apps) whose pages are UltraCanvasFilerWidgets
// in small-thumbnail mode showing the recently used paths (UltraFilerHistory)
// as a file list instead of a folder listing. Files and applications enter the
// history when they are opened; a folder enters it when work was done in it
// (a file opened there, or content created / pasted / renamed / deleted / ...,
// reported by the filer's onFolderModified) — not by being browsed.
// The heart button swaps the same area for the Favorites view — the same
// Files / Folders / Apps layout, but showing the deliberately pinned paths
// (UltraFilerFavorites) instead of recent ones. The folder tree's "Pinned"
// section holds the tree pins, whose
// entries navigate like bookmarks; the tree's context menu offers Copy /
// Delete / Paste (folders only), a Pin submenu whose "To Treeview" /
// "To Favorites" flags show and toggle where the folder is pinned, and Unpin
// (pinned entries only). The filer context menus' Extras submenu ends with an
// app-provided block (extrasMenuProvider): "Open prompt", then Pin / Unpin
// submenus with the same flags, acting on the current selection.
// The tree's drive entries (the drive roots on Windows, "File System" and the
// mounted volumes elsewhere) are painted with the configured drive background
// colour, and the selected folder with the configured highlight colour; both
// come from the settings window's Display > Treeview page.
// Version: 1.10.0
// Last Modified: 2026-08-22
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasWindow.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasMediaViewer.h"
#include "UltraCanvasSplitPane.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasBreadcrumb.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasTextInput.h"
#include "UltraFilerFavorites.h"
#include "UltraFilerHistory.h"
#include "UltraFilerSettings.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace UltraCanvas {

class UltraFilerWindow {
public:
    ~UltraFilerWindow();

    // Creates the window and the whole UI; shows `startFolder` (falls back to
    // the user's home directory when empty or not a directory).
    bool Initialize(const std::string& startFolder = "");
    void Show();

private:
    // Per-tab browsing state: the folder widget of the tab plus its own
    // Back / Forward history. The vector below always mirrors the tab order
    // of the tabbed container, so tab indexes match.
    struct FilerTabState {
        std::shared_ptr<UltraCanvasFilerWidget> filer;
        std::shared_ptr<UltraCanvasContainer>   page;  // tab content wrapper
        std::vector<std::string> history;      // visited folders
        size_t historyIndex = 0;               // current position in `history`
        bool navigatingHistory = false;        // Back/Forward in flight - don't push
        std::string searchQuery;               // active search ("" = folder display)
    };

    // The three History tabs, in tab order. The enumerators index
    // `historyFilers` and map onto FilerHistoryKind. The Favorites view uses
    // the same three tabs (indexing `favoritesFilers`, mapping onto
    // FilerFavoriteKind's first three values).
    enum HistoryTab { HistoryFiles = 0, HistoryFolders, HistoryApps, HistoryTabCount };

    // ===== UI CONSTRUCTION =====
    std::shared_ptr<UltraCanvasContainer> BuildNavigationRow();
    std::shared_ptr<UltraCanvasContainer> BuildCommandBar();
    void BuildFolderTree();
    void BuildTabbedContainer();
    void BuildSplitLayout();
    // The History view (hidden until the clock button turns it on): a tabbed
    // container with one small-thumbnail filer per history kind.
    void BuildHistoryView();
    // The Favorites view (hidden until the heart button turns it on): the
    // same layout showing the pinned paths (UltraFilerFavorites).
    void BuildFavoritesView();

    // ===== TABS =====
    // Creates a tab with its own filer widget showing `path`.
    void AddNewTab(const std::string& path, bool activate);
    void WireFilerCallbacks(FilerTabState* tab);
    // Refreshes breadcrumb, nav buttons, tree, dropdowns, status bar, window
    // title and preview from the newly active tab.
    void HandleTabSwitched(int index);
    FilerTabState* ActiveTabState() const;
    bool IsActiveTab(const FilerTabState* tab) const;
    int  TabIndexOf(const FilerTabState* tab) const;

    // ===== FOLDER TREE (lazy) =====
    // Adds a folder node under `parentId`. Whether the folder has subfolders —
    // and so gets an expand button, shown as a placeholder child — is answered
    // by the background probe below, not here.
    void AddTreeFolderNode(const std::string& parentId, const std::string& path,
                           const std::string& label, const std::string& iconFile);
    // Scans `node`'s subfolders into real child nodes (once per node) and drops
    // the placeholder that stood for them.
    void EnsureTreeChildren(TreeNode* node);
    // Adds a drive node (a drive root on Windows, "File System" or a mounted
    // volume elsewhere) under `parentId` and remembers it as a drive, so the
    // configured drive background colour reaches it - now and after every
    // change in the settings.
    void AddTreeDriveNode(const std::string& path, const std::string& label);
    // Pushes the configured tree colours (drive row background, selected
    // folder highlight) into the folder tree.
    void ApplyTreeColors();
    // Selects (expanding ancestors as needed) the tree node of `path`.
    void SyncTreeSelection(const std::string& path);

    // "Does this folder contain subfolders?" costs a directory open each, and a
    // single expansion asks it once per child — on a slow or network volume
    // that froze the window for seconds. The question is answered on a worker
    // thread instead; each answer posts back to the UI thread and only then
    // gives the node its expand button.
    void QueueSubfolderProbe(const std::string& path);
    void ApplySubfolderProbe(const std::string& path, bool hasSubfolders);
    void StartSubfolderProbeWorkerLocked();
    void StopSubfolderProbeWorker();
    void SubfolderProbeWorkerMain();

    // ===== SEARCH =====
    // Searches the active tab's folder (recursively) for names containing
    // `query` and shows the matches in the tab's current view mode; an empty
    // query returns the tab to its normal folder display.
    void RunSearch(const std::string& query);

    // ===== HISTORY (the clock button) =====
    // Swaps the tree + folder area for the History view and back. Showing it
    // rebuilds the three lists from the recorded paths.
    void SetHistoryVisible(bool visible);
    // Re-reads the recorded paths into the three history filers (dropping the
    // ones that no longer exist).
    void RefreshHistoryTabs();
    // Remembers an entry the user just opened, in the list its kind belongs to.
    void RecordEntryInHistory(const FilerEntry& entry);
#ifdef ULTRACANVAS_HAS_ULTRAWIN
    // Double-clicked Windows executable: runs it through UltraWin (Wine
    // tier) in a per-app environment, off the UI thread — first launches
    // create the environment, which takes a while. Status-bar feedback only;
    // when Wine is missing the status bar says how to get it.
    void LaunchWindowsExecutable(const FilerEntry& entry);
#endif
    // Remembers a folder the user did something in (opened a file there,
    // created / pasted / renamed / deleted something, ...). Browsing a folder
    // is not enough - the Folders tab lists folders that were worked in.
    void RecordFolderInHistory(const std::string& folder);
    // A history tile was activated: leaves the History view and shows the path
    // in the browsing view (a folder is opened, a file's folder is opened).
    // The Favorites view's tiles go through it too.
    void OpenHistoryEntry(const std::string& path, bool isFolder);
    UltraCanvasFilerWidget* ActiveHistoryFiler() const;

    // ===== FAVORITES (the heart button) =====
    // Swaps the tree + folder area for the Favorites view and back; showing
    // it and showing the History view are mutually exclusive.
    void SetFavoritesVisible(bool visible);
    // Leaves whichever of the History / Favorites views is shown - the folder
    // browsing has to be visible for navigation and file commands.
    void ShowBrowsingView();
    // Re-reads the pinned paths into the three favorites filers (dropping the
    // ones that no longer exist).
    void RefreshFavoritesTabs();
    UltraCanvasFilerWidget* ActiveFavoritesFiler() const;
    // The filer the user is looking at: a Favorites / History page while one
    // of those views is shown, else the active tab's filer.
    UltraCanvasFilerWidget* VisibleFiler() const;
    // What the Pin / Unpin entries act on: the visible filer's selection, or
    // the shown folder itself while nothing is selected in the browsing view.
    std::vector<FilerEntry> PinTargets() const;
    // Pin > To Favorites: each target goes into the tab its kind belongs to.
    void PinTargetsToFavorites();
    // Pin > To Treeview: each target folder appears under the tree's Pinned
    // node.
    void PinTargetsToTree();
    // Unpin > To Favorites / To Treeview: the reverse of the two Pin actions.
    void UnpinTargetsFromFavorites();
    void UnpinTargetsFromTree();
    // The app's tail of the filer context menus' Extras submenu (wired into
    // every filer's extrasMenuProvider, rebuilt on each open): "Open prompt",
    // then Pin / Unpin submenus whose "To Treeview" / "To Favorites" flags
    // show whether the current selection is pinned there.
    std::vector<MenuItemData> BuildExtrasMenuItems();

    // ===== FOLDER TREE: PINNED SECTION + CONTEXT MENU =====
    // Rebuilds the children of the tree's "Pinned" node from the pinned
    // folder paths (dropping the ones that no longer exist).
    void RefreshPinnedTreeNodes();
    // Expands the tree's "Pinned" node, so a fresh pin is seen rather than
    // left behind a collapsed header.
    void RevealPinnedTreeSection();
    // The folder a tree node stands for: the node id itself, or the target of
    // a pinned entry; "" for the Computer root, the Pinned header and the
    // lazy "..." placeholders.
    std::string TreeNodeTargetPath(const TreeNode* node) const;
    // Drag-and-drop of files/folders onto the tree. `IsTreeDropTarget` reports
    // whether a node accepts a drop (the Pinned section, or a folder node);
    // `DropFilesOnTreeNode` pins onto the Pinned section or moves the files into
    // a folder node, returning whether it handled the drop.
    bool IsTreeDropTarget(const TreeNode* node) const;
    bool DropFilesOnTreeNode(TreeNode* target,
                             const std::vector<std::string>& files);
    // The tree's context menu (Copy / Delete / Paste / Pin / Unpin) at the
    // pointer.
    void ShowTreeContextMenu(TreeNode* node, const UCEvent& event);
    // Paste the clipboard files into `folder` (tree context menu's Paste).
    void PasteIntoFolder(const std::string& folder);
    // Delete `path` from disk (after the confirm dialog) and take it out of
    // the tree, the pins and every tab that was showing it.
    void ConfirmDeleteTreeFolder(const std::string& path);

    // ===== NAVIGATION =====
    void NavigateTo(const std::string& path);
    void NavigateBack();
    void NavigateForward();
    void NavigateUp();
    void HandlePathChanged(FilerTabState* tab, const std::string& path);
    void UpdateNavButtons();

    // ===== SELECTION / PREVIEW / STATUS =====
    void UpdateStatusBar();
    // Window title: app name + version + what is on screen (the active
    // tab's folder path, or the History/Favorites view while shown).
    void UpdateWindowTitle();
    // Turns the preview feature on/off (the command bar toggle; Esc while
    // the preview is shown turns it off the same way).
    void SetPreviewEnabled(bool enabled);
    // Shows the preview pane only while the preview is enabled AND a
    // previewable file is selected; otherwise the folder display gets the
    // whole width. Adds / removes the split pane accordingly. The preview
    // takes its width from the folder display only — the tree pane (and the
    // splitter the user dragged) stays where it is — and the selected file
    // is kept scrolled into view when the pane narrows the folder display.
    void UpdatePreviewPane();
    // Path of the single selected previewable file, or "" when there is
    // nothing to preview.
    std::string PreviewablePathForSelection() const;
    // Mirrors the preview toggle into every tab's filer: while the preview is
    // on, deleting the previewed file hands the selection to the neighbouring
    // entry, so the pane shows that file instead of folding away.
    void ApplyPreviewSelectionPolicy();

    // ===== EXTRAS (context menu: Print / Share / Attributes / Access) =====
    // Prints the text files among `targets` through the OS print dialog.
    void HandlePrint(const std::vector<FilerEntry>& targets);
    // Hands the files among `targets` to the platform's e-mail composer as
    // attachments (UltraFilerShare).
    void HandleShare(const std::vector<FilerEntry>& targets);
    // Opens the Attributes window for `targets` (UltraFilerPropertiesDialogs).
    void HandleAttributes(const std::vector<FilerEntry>& targets);
    // Opens the Access (permissions) window for `targets`; applying refreshes
    // the visible listing so the attribute column follows.
    void HandleAccess(const std::vector<FilerEntry>& targets);
    // Re-reads whatever listing is on screen: the History / Favorites lists
    // while one of those views is shown, else the active tab's folder.
    void RefreshVisibleListing();
    // Extras > Open prompt: starts the OS command line program in the folder
    // of the active tab. Which application that is comes from the settings
    // (Extras > Open prompt); an empty setting uses the platform default.
    void OpenSystemPrompt();

    // ===== SETTINGS =====
    // Pushes the persisted settings into the widgets they configure (the
    // preview's transparent-image backdrop). Called at startup and by the
    // settings dialog after every change.
    void ApplySettings();
    // Opens the settings window (the navigation row's gear button and the
    // filer context menus' Settings item), which also hosts the Clear
    // History / Clear Favorites actions.
    void OpenSettingsDialog();

    // ===== WIDGETS =====
    std::shared_ptr<UltraCanvasWindow>          window;
    std::shared_ptr<UltraCanvasTreeView>        folderTree;
    std::shared_ptr<UltraCanvasTabbedContainer> tabbedContainer;
    std::shared_ptr<UltraCanvasFilerWidget>     filer;   // active tab's filer
    std::shared_ptr<UltraCanvasMediaViewer>     preview;
    std::shared_ptr<UltraCanvasSplitPane>       split;
    std::shared_ptr<UltraCanvasContainer>       contentBox;    // holds the split OR the History view
    std::shared_ptr<UltraCanvasContainer>       historyPane;   // History view root
    std::shared_ptr<UltraCanvasTabbedContainer> historyTabs;   // Files / Folders / Apps
    std::shared_ptr<UltraCanvasFilerWidget>     historyFilers[HistoryTabCount];
    std::shared_ptr<UltraCanvasContainer>       favoritesPane; // Favorites view root
    std::shared_ptr<UltraCanvasTabbedContainer> favoritesTabs; // Files / Folders / Apps
    std::shared_ptr<UltraCanvasFilerWidget>     favoritesFilers[HistoryTabCount];
    std::shared_ptr<UltraCanvasMenu>            treeContextMenu; // folder tree right-click
    std::shared_ptr<UltraCanvasContainer>       previewPane;   // split pane hosting the preview
    std::shared_ptr<UltraCanvasBreadcrumb>      breadcrumb;
    std::shared_ptr<UltraCanvasTextInput>       searchInput;
    std::shared_ptr<UltraCanvasLabel>           statusLabel;
    std::shared_ptr<UltraCanvasButton>          backButton;
    std::shared_ptr<UltraCanvasButton>          forwardButton;
    std::shared_ptr<UltraCanvasButton>          upButton;
    std::shared_ptr<UltraCanvasButton>          previewButton;
    std::shared_ptr<UltraCanvasButton>          historyButton;
    std::shared_ptr<UltraCanvasButton>          favoritesButton;
    std::shared_ptr<UltraCanvasDropdown>        sortDropdown;
    std::shared_ptr<UltraCanvasDropdown>        viewDropdown;
    std::shared_ptr<UltraCanvasDropdown>        videoModeDropdown;

    // ===== STATE =====
    std::vector<std::unique_ptr<FilerTabState>> tabStates;  // mirrors tab order
    int tabCounter = 0;                    // unique widget ids for new tabs

    // Tree nodes whose real children have been scanned (EnsureTreeChildren runs
    // once per node); keyed by node id, which is the folder path.
    std::set<std::string> treeChildrenLoaded;
    // Node ids (folder paths) of the drive entries, in the order they were
    // added; ApplyTreeColors repaints exactly these rows.
    std::vector<std::string> treeDriveNodeIds;
    // Background "has subfolders?" probe (see QueueSubfolderProbe).
    std::deque<std::string> probeQueue;
    std::mutex probeMutex;
    std::condition_variable probeCond;
    std::thread probeWorker;
    bool probeShutdown = false;
    // Cleared on destruction so results still in flight drop instead of
    // reaching a half-destroyed window.
    std::shared_ptr<std::atomic<bool>> probeAlive =
            std::make_shared<std::atomic<bool>>(true);

    bool syncingTree = false;              // tree selection driven by code
    bool syncingControls = false;          // dropdowns driven by filer callbacks
    bool previewEnabled = true;            // the command bar toggle state
    bool previewShown = false;             // preview pane currently in the split
    int  previewPaneWidth = 0;             // last shown width (px), restored on reopen
    bool historyShown = false;             // History view replaces the split
    bool favoritesShown = false;           // Favorites view replaces the split
    UltraFilerSettings settings;           // persisted application settings
    UltraFilerHistory  history;            // recently used files / folders / apps
    UltraFilerFavorites favorites;         // pinned files / folders / apps + tree pins
};

} // namespace UltraCanvas
