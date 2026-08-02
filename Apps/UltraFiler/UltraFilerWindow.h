// Apps/UltraFiler/UltraFilerWindow.h
// UltraFiler - file manager main window (Windows Explorer style layout):
// a navigation row (Back / Forward / Up / Refresh + folder breadcrumb), a
// command bar (New folder / New file, Cut / Copy / Paste / Rename / Delete,
// Sort and View dropdowns, Preview toggle), a three-pane split with the lazy
// folder tree (UltraCanvasTreeView), the folder content display
// (UltraCanvasFilerWidget) and the media preview (UltraCanvasMediaViewer),
// plus a status bar describing the folder and the selection.
// Version: 1.0.0
// Last Modified: 2026-08-01
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasWindow.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTreeView.h"
#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasMediaViewer.h"
#include "UltraCanvasSplitPane.h"
#include "UltraCanvasBreadcrumb.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"

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
    // ===== UI CONSTRUCTION =====
    std::shared_ptr<UltraCanvasContainer> BuildNavigationRow();
    std::shared_ptr<UltraCanvasContainer> BuildCommandBar();
    void BuildFolderTree();
    void BuildSplitLayout();
    void WireFilerCallbacks();

    // ===== FOLDER TREE (lazy) =====
    // Adds a folder node under `parentId`; a placeholder child marks folders
    // with subfolders so the expand button shows before the real scan.
    void AddTreeFolderNode(const std::string& parentId, const std::string& path,
                           const std::string& label, const std::string& iconFile);
    // Replaces the placeholder child with the real subfolder nodes.
    void EnsureTreeChildren(TreeNode* node);
    // Selects (expanding ancestors as needed) the tree node of `path`.
    void SyncTreeSelection(const std::string& path);

    // ===== NAVIGATION =====
    void NavigateTo(const std::string& path);
    void NavigateBack();
    void NavigateForward();
    void NavigateUp();
    void HandlePathChanged(const std::string& path);
    void UpdateNavButtons();

    // ===== SELECTION / PREVIEW / STATUS =====
    void UpdateStatusBar();
    void SetPreviewVisible(bool visible);
    void ShowSelectionInPreview();

    // ===== WIDGETS =====
    std::shared_ptr<UltraCanvasWindow>      window;
    std::shared_ptr<UltraCanvasTreeView>    folderTree;
    std::shared_ptr<UltraCanvasFilerWidget> filer;
    std::shared_ptr<UltraCanvasMediaViewer> preview;
    std::shared_ptr<UltraCanvasSplitPane>   split;
    std::shared_ptr<UltraCanvasContainer>   previewPane;   // split pane hosting the preview
    std::shared_ptr<UltraCanvasBreadcrumb>  breadcrumb;
    std::shared_ptr<UltraCanvasLabel>       statusLabel;
    std::shared_ptr<UltraCanvasButton>      backButton;
    std::shared_ptr<UltraCanvasButton>      forwardButton;
    std::shared_ptr<UltraCanvasButton>      upButton;
    std::shared_ptr<UltraCanvasButton>      previewButton;
    std::shared_ptr<UltraCanvasDropdown>    sortDropdown;
    std::shared_ptr<UltraCanvasDropdown>    viewDropdown;

    // ===== STATE =====
    std::vector<std::string> history;      // visited folders
    size_t historyIndex = 0;               // current position in `history`
    bool navigatingHistory = false;        // Back/Forward in flight - don't push
    bool syncingTree = false;              // tree selection driven by code
    bool syncingControls = false;          // dropdowns driven by filer callbacks
    bool previewVisible = true;
};

} // namespace UltraCanvas
