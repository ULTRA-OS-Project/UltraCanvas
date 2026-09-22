// Apps/ArtCreator/ArtCreatorWindow.h
// ArtCreator main window: menu bar, main toolbar, the tool palette on the
// left, the vector canvas in the middle, the colour / fill / tool options /
// layers panels on the right and a status bar at the bottom. Owns the
// document, its selection and history, the tools and every command.
//
// The editor is multi-window: the class also holds the registry of open
// windows, so File > New Window and a second file on the command line add
// to it and the application exits with the last of them.
// Version: 1.2.0
// Last Modified: 2026-09-18
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasWindow.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasColorPicker.h"
#include "UltraCanvasColorSwatchBar.h"
#include "UltraCanvasGradientEditor.h"
#include "UltraCanvasVectorCanvas.h"
#include "DataFormats/UltraCanvasVectorStorage.h"
#include "DataFormats/UltraCanvasVectorEdit.h"

#include "ArtCreatorTools.h"
#include "ArtCreatorDialogs.h"

#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

class ArtCreatorWindow {
public:
    ArtCreatorWindow();
    ~ArtCreatorWindow();

    // Creates the window; opens `paths[0]` when given, else a new A4 drawing.
    bool Initialize(const std::vector<std::string>& paths);
    void Show();

    // Creates, registers and shows a further editor window on `paths`
    // (empty: a new drawing). One drawing per window; further paths each
    // get a window of their own. The windows own themselves.
    static std::shared_ptr<ArtCreatorWindow> OpenWindow(const std::vector<std::string>& paths);
    static int WindowCount();

    // The extensions this build reads and writes (empty without the Vector
    // plugin).
    static std::vector<std::string> OpenableExtensions();
    static std::vector<std::string> SaveableExtensions();

private:
    // ----- window registry -----
    static std::vector<std::shared_ptr<ArtCreatorWindow>>& OpenWindows();
    static void RetireWindow(ArtCreatorWindow* closed);

    // ----- construction -----
    void BuildMenuBar();
    void BuildToolbar();
    void BuildToolPalette();
    void BuildRightPanel();
    void BuildStatusBar();
    void InstallShortcuts();
    std::string IconPath(const std::string& file) const;
    std::string TexterIconPath(const std::string& file) const;

    // ----- document -----
    void NewDrawing(const ArtCreatorPageResult& page);
    void SetDocument(std::shared_ptr<VectorStorage::VectorDocument> doc, const std::string& path);
    bool OpenFile(const std::string& path);
    bool SaveToPath(const std::string& path);
    void ShowDialog(const std::shared_ptr<UltraCanvasWindow>& dialog);
    void ConfirmDiscard(const std::string& question, const std::function<void()>& proceed);
    void OnDocumentChanged();
    void OnSelectionChanged();
    void UpdateTitle();
    void UpdateStatus();
    void UpdateUndoButtons();
    std::shared_ptr<VectorStorage::VectorLayer> ActiveLayer() const;
    void SetActiveLayer(int index);

    // ----- commands -----
    void CmdNew();
    void CmdNewWindow();
    void CmdOpen();
    void CmdSave();
    void CmdSaveAs();
    void CmdExport();
    void CmdDocumentSetup();
    void CmdUndo();
    void CmdRedo();
    void CmdCut();
    void CmdCopy();
    void CmdPaste();
    void CmdDuplicate();
    void CmdDelete();
    void CmdSelectAll();
    void CmdSelectNone();
    void CmdReorder(VectorEdit::ZOrderMove move);
    void CmdGroup();
    void CmdUngroup();
    void CmdMirror(bool horizontal);
    void CmdAlign(VectorEdit::AlignMode mode, bool toPage);
    void CmdDistribute(VectorEdit::DistributeMode mode);
    void CmdConvertToPath();
    void CmdCombine(VectorEdit::CombineOp op);
    void CmdApplyClipView();
    void CmdRemoveContainer();
    void CmdRemoveFill();
    void CmdRemoveLine();
    void CmdLineWidth(float width);
    void CmdLayerAdd();
    void CmdLayerDelete();
    void CmdLayerMove(int delta);
    void CmdLayerToggle(bool visible);
    void CmdAbout();
    void CmdQuit();

    // ----- tools and panels -----
    void SelectTool(ArtToolId id);
    ArtTool* ActiveTool() const;
    void RebuildToolOptions();
    void RebuildLayerPanel();
    void RebuildLinePanel();
    void SyncColourPanelFromSelection();
    void SyncLinePanelFromSelection();
    // Applies the options' line gallery (arrowheads, profile, brush) to the
    // selection's strokes.
    void ApplyLineGallery(const std::string& label);
    void ApplyFillColour(const Color& c, bool commit);
    void ApplyLineColour(const Color& c, bool commit);
    void ApplyRamp(bool commit);
    void PlaceText(double x, double y, const ArtCreatorTextResult& r);
    // Re-selects elements by Id after a history edit replaced them.
    void Reselect(const std::vector<std::string>& ids);

    // ----- members -----
    std::shared_ptr<UltraCanvasWindow>          window;
    std::shared_ptr<UltraCanvasMenu>            menuBar;
    std::shared_ptr<UltraCanvasToolbar>         toolbar;
    std::shared_ptr<UltraCanvasContainer>       body;
    std::shared_ptr<UltraCanvasToolbar>         palette;
    std::shared_ptr<UltraCanvasVectorCanvas>    canvas;
    std::shared_ptr<UltraCanvasContainer>       rightPanel;
    std::shared_ptr<UltraCanvasColorPicker>     colorPicker;
    std::shared_ptr<UltraCanvasColorSwatchBar>  swatches;
    std::shared_ptr<UltraCanvasGradientEditor>  ramp;
    std::shared_ptr<UltraCanvasContainer>       optionsPanel;
    std::shared_ptr<UltraCanvasLabel>           optionsTitle;
    std::shared_ptr<UltraCanvasContainer>       linePanel;
    std::shared_ptr<UltraCanvasContainer>       layersPanel;
    std::shared_ptr<UltraCanvasContainer>       layerRows;
    std::shared_ptr<UltraCanvasContainer>       statusBar;
    std::shared_ptr<UltraCanvasLabel>           statusPos, statusZoom, statusSelection, statusHint;
    std::shared_ptr<UltraCanvasButton>          undoButton, redoButton;
    std::vector<std::shared_ptr<UltraCanvasButton>> toolButtons;

    std::shared_ptr<VectorStorage::VectorDocument> document;
    std::shared_ptr<VectorEdit::VectorSelection>   selection;
    VectorEdit::VectorHistory                      history;
    std::string documentPath;
    bool modified = false;
    int activeLayerIndex = 0;

    std::vector<std::unique_ptr<ArtTool>> tools;
    ArtToolOptions toolOptions;
    ArtToolContext toolContext;
    ArtToolId activeTool = ArtToolId::Selector;
    Color fillColour = Color(60, 120, 220, 255);
    Color lineColour = Color(30, 30, 40, 255);
    bool syncingColours = false;
    ArtCreatorPageResult lastPage;
    ArtCreatorTextResult lastText;

    // in-app clipboard: clones of the copied elements
    std::vector<std::shared_ptr<VectorStorage::VectorElement>> clipboard;
    int pasteCount = 0;

    double pointerX = 0, pointerY = 0;
    bool pointerInside = false;
};

} // namespace UltraCanvas
