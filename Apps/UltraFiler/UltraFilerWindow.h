// Apps/UltraFiler/UltraFilerWindow.h
// UltraFiler - file manager main window (Windows Explorer style layout):
// a menu bar (Settings menu opening the settings window),
// a navigation row ("+" new-tab button, Back / Forward / Up / Refresh, the
// History clock toggle, folder breadcrumb + recursive search field), a command
// bar (New folder / New file, Cut / Copy / Paste / Rename / Delete, Sort and
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
// Version: 1.6.0
// Last Modified: 2026-08-08
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
    // `historyFilers` and map onto FilerHistoryKind.
    enum HistoryTab { HistoryFiles = 0, HistoryFolders, HistoryApps, HistoryTabCount };

    // ===== UI CONSTRUCTION =====
    std::shared_ptr<UltraCanvasMenu>      BuildMenuBar();
    std::shared_ptr<UltraCanvasContainer> BuildNavigationRow();
    std::shared_ptr<UltraCanvasContainer> BuildCommandBar();
    void BuildFolderTree();
    void BuildTabbedContainer();
    void BuildSplitLayout();
    // The History view (hidden until the clock button turns it on): a tabbed
    // container with one small-thumbnail filer per history kind.
    void BuildHistoryView();

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
    // Remembers a folder the user did something in (opened a file there,
    // created / pasted / renamed / deleted something, ...). Browsing a folder
    // is not enough - the Folders tab lists folders that were worked in.
    void RecordFolderInHistory(const std::string& folder);
    // A history tile was activated: leaves the History view and shows the path
    // in the browsing view (a folder is opened, a file's folder is opened).
    void OpenHistoryEntry(const std::string& path, bool isFolder);
    UltraCanvasFilerWidget* ActiveHistoryFiler() const;

    // ===== NAVIGATION =====
    void NavigateTo(const std::string& path);
    void NavigateBack();
    void NavigateForward();
    void NavigateUp();
    void HandlePathChanged(FilerTabState* tab, const std::string& path);
    void UpdateNavButtons();

    // ===== SELECTION / PREVIEW / STATUS =====
    void UpdateStatusBar();
    // Window title from what is on screen (the active tab's folder, or the
    // History view while it is shown).
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

    // ===== SETTINGS =====
    // Pushes the persisted settings into the widgets they configure (the
    // preview's transparent-image backdrop). Called at startup and by the
    // settings dialog after every change.
    void ApplySettings();
    void OpenSettingsDialog();

    // ===== EXTRAS =====
    // "Extras > Open prompt": starts the OS command line program in the folder
    // of the active tab. Which application that is comes from the settings
    // (Extras > Open prompt); an empty setting uses the platform default.
    void OpenSystemPrompt();

    // ===== WIDGETS =====
    std::shared_ptr<UltraCanvasWindow>          window;
    std::shared_ptr<UltraCanvasMenu>            menuBar;
    std::shared_ptr<UltraCanvasTreeView>        folderTree;
    std::shared_ptr<UltraCanvasTabbedContainer> tabbedContainer;
    std::shared_ptr<UltraCanvasFilerWidget>     filer;   // active tab's filer
    std::shared_ptr<UltraCanvasMediaViewer>     preview;
    std::shared_ptr<UltraCanvasSplitPane>       split;
    std::shared_ptr<UltraCanvasContainer>       contentBox;    // holds the split OR the History view
    std::shared_ptr<UltraCanvasContainer>       historyPane;   // History view root
    std::shared_ptr<UltraCanvasTabbedContainer> historyTabs;   // Files / Folders / Apps
    std::shared_ptr<UltraCanvasFilerWidget>     historyFilers[HistoryTabCount];
    std::shared_ptr<UltraCanvasContainer>       previewPane;   // split pane hosting the preview
    std::shared_ptr<UltraCanvasBreadcrumb>      breadcrumb;
    std::shared_ptr<UltraCanvasTextInput>       searchInput;
    std::shared_ptr<UltraCanvasLabel>           statusLabel;
    std::shared_ptr<UltraCanvasButton>          backButton;
    std::shared_ptr<UltraCanvasButton>          forwardButton;
    std::shared_ptr<UltraCanvasButton>          upButton;
    std::shared_ptr<UltraCanvasButton>          previewButton;
    std::shared_ptr<UltraCanvasButton>          historyButton;
    std::shared_ptr<UltraCanvasDropdown>        sortDropdown;
    std::shared_ptr<UltraCanvasDropdown>        viewDropdown;
    std::shared_ptr<UltraCanvasDropdown>        videoModeDropdown;

    // ===== STATE =====
    std::vector<std::unique_ptr<FilerTabState>> tabStates;  // mirrors tab order
    int tabCounter = 0;                    // unique widget ids for new tabs

    // Tree nodes whose real children have been scanned (EnsureTreeChildren runs
    // once per node); keyed by node id, which is the folder path.
    std::set<std::string> treeChildrenLoaded;
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
    UltraFilerSettings settings;           // persisted application settings
    UltraFilerHistory  history;            // recently used files / folders / apps
};

} // namespace UltraCanvas
