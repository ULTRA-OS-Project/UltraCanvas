// Apps/UltraFiler/UltraFilerWindow.h
// UltraFiler - file manager main window (Windows Explorer style layout):
// a menu bar (Settings menu opening the settings window),
// a navigation row ("+" new-tab button, Back / Forward / Up / Refresh, folder
// breadcrumb + recursive search field), a command bar (New folder / New file,
// Cut / Copy / Paste / Rename / Delete, Sort and View dropdowns, video preview
// mode, Preview toggle), a three-pane split with the lazy folder tree
// (UltraCanvasTreeView), the tabbed folder content display
// (UltraCanvasTabbedContainer hosting one UltraCanvasFilerWidget per tab) and
// the media preview (UltraCanvasMediaViewer, shown only while a previewable
// file is selected; Esc closes it), plus a status bar describing the folder
// and the selection (kept in step with the folder listing through the
// filer's onFolderRefreshed callback).
// Version: 1.4.0
// Last Modified: 2026-08-06
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
#include "UltraFilerSettings.h"

#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

class UltraFilerWindow {
public:
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

    // ===== UI CONSTRUCTION =====
    std::shared_ptr<UltraCanvasMenu>      BuildMenuBar();
    std::shared_ptr<UltraCanvasContainer> BuildNavigationRow();
    std::shared_ptr<UltraCanvasContainer> BuildCommandBar();
    void BuildFolderTree();
    void BuildTabbedContainer();
    void BuildSplitLayout();

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
    // Adds a folder node under `parentId`; a placeholder child marks folders
    // with subfolders so the expand button shows before the real scan.
    void AddTreeFolderNode(const std::string& parentId, const std::string& path,
                           const std::string& label, const std::string& iconFile);
    // Replaces the placeholder child with the real subfolder nodes.
    void EnsureTreeChildren(TreeNode* node);
    // Selects (expanding ancestors as needed) the tree node of `path`.
    void SyncTreeSelection(const std::string& path);

    // ===== SEARCH =====
    // Searches the active tab's folder (recursively) for names containing
    // `query` and shows the matches in the tab's current view mode; an empty
    // query returns the tab to its normal folder display.
    void RunSearch(const std::string& query);

    // ===== NAVIGATION =====
    void NavigateTo(const std::string& path);
    void NavigateBack();
    void NavigateForward();
    void NavigateUp();
    void HandlePathChanged(FilerTabState* tab, const std::string& path);
    void UpdateNavButtons();

    // ===== SELECTION / PREVIEW / STATUS =====
    void UpdateStatusBar();
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

    // ===== SETTINGS =====
    // Pushes the persisted settings into the widgets they configure (the
    // preview's transparent-image backdrop). Called at startup and by the
    // settings dialog after every change.
    void ApplySettings();
    void OpenSettingsDialog();

    // ===== WIDGETS =====
    std::shared_ptr<UltraCanvasWindow>          window;
    std::shared_ptr<UltraCanvasMenu>            menuBar;
    std::shared_ptr<UltraCanvasTreeView>        folderTree;
    std::shared_ptr<UltraCanvasTabbedContainer> tabbedContainer;
    std::shared_ptr<UltraCanvasFilerWidget>     filer;   // active tab's filer
    std::shared_ptr<UltraCanvasMediaViewer>     preview;
    std::shared_ptr<UltraCanvasSplitPane>       split;
    std::shared_ptr<UltraCanvasContainer>       previewPane;   // split pane hosting the preview
    std::shared_ptr<UltraCanvasBreadcrumb>      breadcrumb;
    std::shared_ptr<UltraCanvasTextInput>       searchInput;
    std::shared_ptr<UltraCanvasLabel>           statusLabel;
    std::shared_ptr<UltraCanvasButton>          backButton;
    std::shared_ptr<UltraCanvasButton>          forwardButton;
    std::shared_ptr<UltraCanvasButton>          upButton;
    std::shared_ptr<UltraCanvasButton>          previewButton;
    std::shared_ptr<UltraCanvasDropdown>        sortDropdown;
    std::shared_ptr<UltraCanvasDropdown>        viewDropdown;
    std::shared_ptr<UltraCanvasDropdown>        videoModeDropdown;

    // ===== STATE =====
    std::vector<std::unique_ptr<FilerTabState>> tabStates;  // mirrors tab order
    int tabCounter = 0;                    // unique widget ids for new tabs
    bool syncingTree = false;              // tree selection driven by code
    bool syncingControls = false;          // dropdowns driven by filer callbacks
    bool previewEnabled = true;            // the command bar toggle state
    bool previewShown = false;             // preview pane currently in the split
    int  previewPaneWidth = 0;             // last shown width (px), restored on reopen
    UltraFilerSettings settings;           // persisted application settings
};

} // namespace UltraCanvas
