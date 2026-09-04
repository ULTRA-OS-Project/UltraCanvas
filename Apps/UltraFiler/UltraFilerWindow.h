// Apps/UltraFiler/UltraFilerWindow.h
// UltraFiler - file manager main window (Windows Explorer style layout):
// the folder tab strip at the very top of the window (an
// UltraCanvasTabbedContainer detached from its pages: the strip is the
// window's top bar, the pages live in the folder pane of the split below,
// and the "+" at the end of the tab list opens another tab), then
// a navigation row (Back / Forward / Up / Refresh, the
// History clock toggle, the Favorites heart toggle, folder breadcrumb, and
// the settings gear at the far right opening the settings window), a command
// bar (the "New folder ▾" split button — its arrow lists the same kinds as
// the context menu's "New >" submenu —, Cut / Copy / Paste / Rename / Delete, the
// search field — typing filters the shown folder as-you-type; when nothing
// matches, a centered "Scan sub folder" button (and the Enter key, and the
// button inside the search field) escalates to the background sub-folder
// scan whose matches appear while it runs —, Sort and
// View dropdowns, video preview mode, Preview toggle), a three-pane split with
// the lazy folder tree (UltraCanvasTreeView), the folder content display
// (the tab strip's content host, showing the active tab's
// UltraCanvasFilerWidget - one per tab) and
// the detail pane (shown only while a single previewable file OR a folder is
// selected — a folder only once the double-click interval has passed, so a
// double-click that opens it never flashes its preview; Esc closes it): a
// selected media file shows in the
// UltraCanvasMediaViewer, a selected folder shows its content through a second
// small-thumbnail UltraCanvasFilerWidget — the two share the one pane. Plus a
// status bar describing the folder and the selection (kept in step with the
// folder listing through the filer's onFolderRefreshed callback).
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
// Version: 1.15.0
// Last Modified: 2026-09-03
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
#include "UltraCanvasTimer.h"
#include "UltraCanvasCloudStorage.h"   // CloudStorageInfo (the Cloud Storage section)
#include "UltraCanvasVolumeMonitor.h" // mounted volumes + mount/unmount notification
#include "UltraFilerFavorites.h"
#include "UltraFilerFolderViews.h"
#include "UltraFilerHistory.h"
#include "UltraFilerSettings.h"
#include "UltraFilerSettingsDialog.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
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
    // Creates a tab with its own filer widget showing `path`. Wired to the
    // tab strip's "+" button at the end of the tab list.
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
    // Brings the drive rows back in line with what is actually mounted: a row
    // is added for every volume that appeared and removed for every one that
    // is gone, together with everything the tree remembers about it. Any tab
    // that was inside a volume that went away is moved to the home folder, so
    // nothing keeps showing a folder that no longer exists.
    //
    // Driven by the volume monitor (StartVolumeMonitor), which is what makes a
    // USB stick plugged in while UltraFiler is running appear without a
    // restart - the tree used to be enumerated exactly once, at start-up.
    // Cheap and idempotent: calling it when nothing changed does nothing.
    void RefreshDriveNodes();
    // Takes one drive row out of the tree and out of the bookkeeping that
    // would otherwise keep it from ever being scanned again.
    void DropDriveNode(const std::string& path);
    // Starts watching for mounts and unmounts. The monitor reports from its
    // own thread, so the refresh is posted back to the UI thread, and a burst
    // of notifications from one insertion is collapsed into a single pass.
    void StartVolumeMonitor();
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

    // Fills the "Cloud Storage" section. Which cloud folders exist is asked
    // off the UI thread (GetCloudStorageFolders touches the registry, a config
    // file and the mount table); the answer posts back and reveals the
    // section, which stays hidden while there is nothing in it.
    void QueueCloudStorageDiscovery();
    void StopCloudStorageDiscovery();
    void ApplyCloudStorageFolders(const std::vector<CloudStorageInfo>& found);

    // Re-derives the tree's Home children after Settings > Display > Home
    // folder flips between "Show all content" and "Show only predefined
    // folders" (ApplySettings calls it on a change).
    void RefreshHomeTreeChildren();

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
    // Starts a sub-folder scan of the active tab's folder for names containing
    // `query`; an empty query returns the tab to its normal folder display.
    // The walk runs on a worker thread and its matches reach the display in
    // batches while it goes on (see SubfolderSearchWorkerMain) — the scan used
    // to run on the UI thread, which froze the window for as long as the tree
    // took and, on a large volume, long enough for the user to conclude the
    // application had died. Wired to the search field's Enter, to its in-field
    // "Scan sub folder" button and to the filer's centered "Scan sub folder"
    // button.
    void RunSearch(const std::string& query);
    // Filter-as-you-type: every edit of the search field narrows the active
    // tab's folder listing to the names containing the text (the filer's
    // name filter — no disk walk). When nothing matches, the filer shows the
    // centered "Scan sub folder" button, which escalates to RunSearch.
    // An empty text ends the filter (and leaves an earlier recursive-result
    // display).
    void ApplyLiveSearchFilter(const std::string& text);
    // Ends every search state of the active tab — the field's text, the live
    // filter, a running scan and a result display — used by the file-creation
    // commands, whose fresh entry has to be visible in the folder display.
    void ResetSearchState();

    // ===== SUB-FOLDER SEARCH (background, incremental) =====
    // Everything the worker and the UI thread share about one scan. Held by
    // shared_ptr so a cancelled or superseded scan can be dropped without
    // waiting for its thread, and so a batch still queued for the UI thread
    // can tell which scan it belongs to.
    struct SubfolderSearchState {
        std::mutex mutex;
        std::vector<std::string> pending;      // matches not yet on screen
        std::atomic<bool> cancelled{false};
        std::atomic<bool> drainPosted{false};  // one batch in flight at a time
        std::atomic<bool> finished{false};     // the walk ran out of folders
        std::atomic<bool> truncated{false};    // stopped at kMaxSearchResults
        std::atomic<size_t> matches{0};
        std::atomic<size_t> foldersScanned{0};
    };
    // The walk itself: an explicit folder stack (no recursive iterator, whose
    // errors are awkward to contain), symlinks and junctions never entered so
    // a reparse-point loop cannot run forever, and a depth cap on top of that.
    void SubfolderSearchWorkerMain(std::shared_ptr<SubfolderSearchState> state,
                                   std::shared_ptr<std::atomic<bool>> alive,
                                   std::string root, std::string needle,
                                   uint64_t generation);
    // Moves what the worker has found onto the display (UI thread), refreshes
    // the status line and, when the walk is done, retires the worker.
    void DrainSubfolderSearch(std::shared_ptr<SubfolderSearchState> state,
                              uint64_t generation);
    // Cancels a running scan; the results found so far stay on screen. The
    // worker is set aside rather than waited for (see StopSubfolderSearch).
    void StopSubfolderSearch();
    // Joins the workers of cancelled scans: the ones that have finished, or
    // all of them when `waitForAll` (window shutdown).
    void ReapSearchWorkers(bool waitForAll);
    // The search field's in-field button: "Scan sub folder" while there is
    // something to search for, "Stop" while a scan runs.
    void UpdateScanButton();

    // ===== NEW ENTRY (the command bar's "New folder ▾" split button) =====
    // The primary section creates a folder (the old "New folder" button);
    // the arrow opens ShowNewEntryMenu below. Both creation commands end the
    // search first (ResetSearchState), so the fresh entry and its rename
    // editor are on screen.
    void CreateNewFolderCommand();
    void CreateNewDocumentCommand(const FilerNewDocumentType& type);
    // The arrow's menu under the button: the same entries as the filer
    // context menu's "New >" submenu — Folder (Ctrl+F), then the filer's
    // document kinds (Text, Doc, Spreadsheet, Bitmap, Vector, Audio, Video).
    void ShowNewEntryMenu();

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
    // Runs the entry in the given environment on a worker thread, with
    // status-bar feedback (called once the environment is decided).
    void StartWindowsLaunch(const FilerEntry& entry,
                            const std::string& environment);
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
    // Per-folder display state (UltraFilerFolderViews). Entering a folder puts
    // back the view type and sort order it was last looked at with; changing
    // either while a folder is shown records it against that folder.
    void ApplyFolderView(FilerTabState* tab, const std::string& path);
    void RememberFolderView(FilerTabState* tab);
    void UpdateNavButtons();
    // Repaints the sort-direction button from the filer's own direction:
    // sort-up.svg while ascending, sort-down.svg while descending.
    void UpdateSortOrderButton();

    // ===== SELECTION / PREVIEW / STATUS =====
    void UpdateStatusBar();
    // Window title: app name + version + what is on screen (the active
    // tab's folder path, or the History/Favorites view while shown).
    void UpdateWindowTitle();
    // Turns the preview feature on/off (the command bar toggle; Esc while
    // the preview is shown turns it off the same way).
    void SetPreviewEnabled(bool enabled);
    // Shows the detail pane only while the preview is enabled AND a single
    // previewable file or a folder is selected; otherwise the folder display
    // gets the whole width. Adds / removes the split pane accordingly, and
    // swaps the pane's content between the media viewer (a file) and the
    // folder preview filer (a folder — its content is shown, not a blank
    // pane). The pane takes its width from the folder display only — the
    // tree pane (and the splitter the user dragged) stays where it is — and
    // the selected file is kept scrolled into view when the pane narrows the
    // folder display.
    void UpdatePreviewPane();
    // The single selected entry of the active tab's filer, or nullptr while
    // the selection is empty or holds several entries. The pointer is into
    // the filer's entry vector — use it immediately, don't keep it.
    const FilerEntry* SingleSelectedEntry() const;
    // A single click on a folder shows its content in the detail pane, but a
    // double-click on it OPENS the folder — so the folder preview first
    // waits out the double-click interval: the click arms this timer, and
    // only its firing (with the folder still the single selection) scans
    // the folder into the pane. Opening the folder — or any selection
    // change — in the meantime cancels it, so a double-click never flashes
    // the preview. A shown media/file preview is untouched while the timer
    // runs.
    void ArmFolderPreviewTimer(const std::string& folderPath);
    void CancelFolderPreviewTimer();
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
    // Every file display of the window - the tabs, the folder preview and the
    // History / Favorites lists - so a setting that governs all of them is
    // applied in one pass.
    std::vector<UltraCanvasFilerWidget*> AllFilers() const;
    // Display > Thumbnails / Detail view: the switches of `source` (the file
    // display whose menu was used) become the persisted setting and are
    // mirrored into every other file display. Guarded against the echo of its
    // own writes - each SetThumbnailKinds() fires the change hook again.
    void AdoptDisplayFormats(UltraCanvasFilerWidget* source);
    // Whether the detail pane can show this entry: the Display > Detail view
    // switches allow it AND the media viewer has a view for the file.
    bool CanShowInDetailView(const FilerEntry& entry) const;
    // The tail the file display hangs under Display > Thumbnails and
    // Display > Detail view: "File formats...", which opens the matching
    // settings page.
    std::vector<MenuItemData> BuildFormatListMenuItems(FilerPreviewTarget target);
    // Installs the display-format callbacks (menu tail + change hook) on a
    // freshly created file display.
    void WireDisplayFormatCallbacks(UltraCanvasFilerWidget* target);
    // Opens the settings window (the navigation row's gear button and the
    // filer context menus' Settings item), which also hosts the Clear
    // History / Clear Favorites actions. `page` points it straight at one
    // settings page - the Display menu's "File formats..." entries do.
    void OpenSettingsDialog(UltraFilerSettingsDialog::Page page =
                                    UltraFilerSettingsDialog::Page::Default);

    // ===== WIDGETS =====
    std::shared_ptr<UltraCanvasWindow>          window;
    std::shared_ptr<UltraCanvasTreeView>        folderTree;
    // The folder tab strip: the window's topmost bar. Its pages are detached
    // into `tabContentHost` (in the split's folder pane), so the tabs sit above
    // the toolbars while the folder they select is displayed below them.
    std::shared_ptr<UltraCanvasTabbedContainer> tabbedContainer;
    std::shared_ptr<UltraCanvasContainer>       tabContentHost;  // shows the active tab's page
    std::shared_ptr<UltraCanvasFilerWidget>     filer;   // active tab's filer
    std::shared_ptr<UltraCanvasMediaViewer>     preview;
    // Folder preview: shows the content of a selected folder in the detail
    // pane, the way `preview` shows a selected file. The two share the pane;
    // UpdatePreviewPane swaps whichever the selection calls for into it.
    std::shared_ptr<UltraCanvasFilerWidget>     folderPreview;
    std::shared_ptr<UltraCanvasSplitPane>       split;
    std::shared_ptr<UltraCanvasContainer>       contentBox;    // holds the split OR the History view
    std::shared_ptr<UltraCanvasContainer>       historyPane;   // History view root
    std::shared_ptr<UltraCanvasTabbedContainer> historyTabs;   // Files / Folders / Apps
    std::shared_ptr<UltraCanvasFilerWidget>     historyFilers[HistoryTabCount];
    std::shared_ptr<UltraCanvasContainer>       favoritesPane; // Favorites view root
    std::shared_ptr<UltraCanvasTabbedContainer> favoritesTabs; // Files / Folders / Apps
    std::shared_ptr<UltraCanvasFilerWidget>     favoritesFilers[HistoryTabCount];
    std::shared_ptr<UltraCanvasMenu>            treeContextMenu; // folder tree right-click
    std::shared_ptr<UltraCanvasButton>          newButton;       // "New folder ▾" split button
    std::shared_ptr<UltraCanvasMenu>            newEntryMenu;    // its arrow's dropdown menu
    std::shared_ptr<UltraCanvasContainer>       previewPane;   // split pane hosting the preview
    std::shared_ptr<UltraCanvasBreadcrumb>      breadcrumb;
    std::shared_ptr<UltraCanvasContainer>       searchBox;    // field + in-field button
    std::shared_ptr<UltraCanvasTextInput>       searchInput;
    std::shared_ptr<UltraCanvasButton>          scanButton;   // "Scan sub folder" / "Stop"
    std::shared_ptr<UltraCanvasLabel>           statusLabel;
    std::shared_ptr<UltraCanvasButton>          backButton;
    std::shared_ptr<UltraCanvasButton>          forwardButton;
    std::shared_ptr<UltraCanvasButton>          upButton;
    std::shared_ptr<UltraCanvasButton>          previewButton;
    std::shared_ptr<UltraCanvasButton>          sortOrderButton;  // ascending / descending
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
    // "Which cloud folders exist?" lookup (QueueCloudStorageDiscovery). Re-run
    // when a volume appears - a Google Drive mounted as a virtual drive letter
    // arrives with it - so the flag, not the thread's joinability, is what
    // keeps two lookups from running at once.
    std::thread cloudWorker;
    std::atomic<bool> cloudWorkerBusy{false};
    // Background sub-folder search (see RunSearch). `searchState` is null while
    // no scan runs; `searchGeneration` is bumped for every scan started or
    // stopped, so batches queued by an abandoned one are dropped on arrival.
    std::thread searchWorker;
    std::shared_ptr<SubfolderSearchState> searchState;
    uint64_t searchGeneration = 0;
    FilerTabState* searchTab = nullptr;    // tab the results belong to
    std::string searchQueryText;           // query of the running / last scan
    std::string searchStatus;              // what the status bar says about it
    bool searchResultsShown = false;       // first batch already on display
    bool scanButtonStops = false;          // the in-field button reads "Stop"
    // Workers of cancelled scans, waiting to be joined (ReapSearchWorkers).
    struct RetiredSearch {
        std::thread thread;
        std::shared_ptr<SubfolderSearchState> state;
    };
    std::vector<RetiredSearch> retiredSearches;
    // Display > Home folder mode, mirrored for the probe worker: `settings`
    // belongs to the UI thread, the "has subfolders?" probe does not.
    std::atomic<bool> curatedHomeActive{false};
    // Set by the monitor's thread, cleared by the UI thread that acts on it:
    // one tree pass per burst, however many notifications an insertion makes.
    std::atomic<bool> volumeRefreshPending{false};
    // Mounts and unmounts (see RefreshDriveNodes). Stopped first thing in the
    // destructor: its callback captures the window, and Stop() joins, so
    // nothing can report into a window that is going away. Declared after the
    // flag its callback touches, so even the implicit Stop() in its own
    // destructor runs while that flag is still alive.
    UltraCanvasVolumeMonitor volumeMonitor;
    // Cleared on destruction so results still in flight drop instead of
    // reaching a half-destroyed window.
    std::shared_ptr<std::atomic<bool>> probeAlive =
            std::make_shared<std::atomic<bool>>(true);

    bool syncingTree = false;              // tree selection driven by code
    bool syncingControls = false;          // dropdowns driven by filer callbacks
    // Set while the display-format switches are pushed into the file
    // displays, so their change hooks do not write the setting back.
    bool applyingDisplayFormats = false;
    bool previewEnabled = true;            // the command bar toggle state
    bool previewShown = false;             // preview pane currently in the split
    bool previewShowsFolder = false;       // pane holds folderPreview, not preview
    int  previewPaneWidth = 0;             // last shown width (px), restored on reopen
    // Folder-preview double-click guard (see ArmFolderPreviewTimer): the
    // folder waiting out the delay, and the folder whose delay has elapsed —
    // UpdatePreviewPane shows a folder only once it is the "ready" one.
    TimerId folderPreviewDelayTimer = InvalidTimerId;
    std::string pendingFolderPreviewPath;
    std::string folderPreviewReadyPath;
    bool historyShown = false;             // History view replaces the split
    bool favoritesShown = false;           // Favorites view replaces the split
    UltraFilerSettings settings;           // persisted application settings
    UltraFilerHistory  history;            // recently used files / folders / apps
    UltraFilerFavorites favorites;         // pinned files / folders / apps + tree pins
    UltraFilerFolderViews folderViews;     // per-folder view type + sort order
    // Set while a stored folder state is being pushed into a filer, so the
    // widget's own onViewTypeChanged / onSortChanged do not record it straight
    // back (and a folder never overwrites its own entry with what it just got).
    bool applyingFolderView = false;
};

} // namespace UltraCanvas
