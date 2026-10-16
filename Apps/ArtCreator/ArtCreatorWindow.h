// Apps/ArtCreator/ArtCreatorWindow.h
// ArtCreator main window: menu bar, main toolbar, the tool palette on the
// left, the vector canvas in the middle, the colour / fill / tool options /
// layers panels on the right and a status bar at the bottom. Owns the
// document, its selection and history, the tools and every command.
//
// The editor is multi-window: the class also holds the registry of open
// windows, so File > New Window and a second file on the command line add
// to it and the application exits with the last of them.
// Version: 1.7.0 - icon mode: an icon size, a pixel grid, pixel snapping aware of the
//                  line width, Snap to Pixels and the pixel preview; the palette's
//                  tools in groups (toolSlots)
// Version: 1.6.0 - BringForward: restores and raises the window (a file handed over by a
//                  second start opens in a window of its own and comes to the front)
// Version: 1.5.0 - SaveToPath takes the bitmap size: Export saves every type through FileLoader
// Version: 1.4.0 - ToolIconPath / SharedIconPath; MenuIconPath is the app's 16 px menu set
// Version: 1.3.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasWindow.h"
#include "UltraCanvasMenu.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasToolGroupButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasColorPicker.h"
#include "UltraCanvasColorSwatchBar.h"
#include "UltraCanvasGradientEditor.h"
#include "UltraCanvasVectorCanvas.h"
#include "DataFormats/UltraCanvasVectorStorage.h"
#include "DataFormats/UltraCanvasVectorEdit.h"
#include "UltraCanvasFileSaveSettings.h"   // FileSaveImageSize

#include "ArtCreatorTools.h"
#include "ArtCreatorDialogs.h"
#include "ArtCreatorIcon.h"

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
    // Restores the window when minimised and raises it with the keyboard
    // focus: what a file a second start handed over opened in comes forward.
    void BringForward();

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
    // The app's icons come in three sizes, each drawn on its own pixel grid
    // so its lines land on whole pixels at the size it is shown at:
    // media/icons/artcreator/ (24 px: toolbar and panel buttons), .../28/
    // (the tool palette) and .../16/ (the menus).
    std::string IconPath(const std::string& file) const;
    std::string ToolIconPath(const std::string& file) const;
    std::string MenuIconPath(const std::string& file) const;
    // The framework's shared line icons (media/icons/menu/, 24 px): the
    // toolbar's New, Open, Save, Undo, Redo and zoom buttons.
    std::string SharedIconPath(const std::string& file) const;

    // ----- document -----
    void NewDrawing(const ArtCreatorPageResult& page);
    void SetDocument(std::shared_ptr<VectorStorage::VectorDocument> doc, const std::string& path);
    bool OpenFile(const std::string& path);
    // Saves through UltraCanvasFileLoader::SaveVectorDocument: a vector type
    // through its writer, a bitmap one with the page drawn at `size`
    // (IsOriginal: the page at 96 DPI). False, after saying why, on failure.
    bool SaveToPath(const std::string& path, const FileSaveImageSize& size = FileSaveImageSize());
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
    // ----- the system clipboard (copy and paste with other programs) -----
    void PutPictureOnSystemClipboard(const std::vector<std::shared_ptr<VectorStorage::VectorElement>>& elements);
    bool PasteFromSystemClipboard();
    void PasteCopiedElements();
    bool PlacePicture(std::vector<uint8_t> bytes, std::string mimeType, const std::string& what);
    bool PlaceDrawingFile(const std::string& path);
    void PlaceElement(const std::shared_ptr<VectorStorage::VectorElement>& element, const std::string& label,
                      const std::function<void()>& alongside = {});
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
    // `pixels` icon pixels in icon mode, 96 DPI pixels (0.75 pt) otherwise.
    void CmdLineWidthPixels(int pixels);
    void CmdLayerAdd();
    void CmdLayerDelete();
    void CmdLayerMove(int delta);
    void CmdLayerToggle(bool visible);
    void CmdAbout();
    void CmdQuit();

    // ----- icon mode -----
    // An icon is drawn for the size it is shown at (16 to 256 px). Icon mode
    // lays a grid of that many pixels across the page, snaps the pointer to
    // it - onto pixel centres for a line an odd number of pixels wide, onto
    // pixel edges otherwise - and shows the page drawn at that size with the
    // pixels it leaves soft (ArtCreatorIcon.h). Off is 0.
    void CmdNewIcon(int pixels);
    void CmdSnapToPixels();
    void SetIconSize(int pixels);
    // The width of one icon pixel in document units; 0 when icon mode is off.
    double IconPixel() const;
    int IconRasterHeight() const;
    // Whether the pointer should go to pixel centres for what the active
    // tool does: a new line of an odd number of pixels, or a selection whose
    // first line is one.
    bool IconCentred() const;
    VectorPointerEvent IconEvent(const VectorPointerEvent& e) const;
    void BuildIconPanel();
    void LayoutIconPanel();
    void RefreshIconPreview();
    void DrawIconOverlay(IRenderContext* ctx, const VectorViewTransform& view);

    // ----- tools and panels -----
    void SelectTool(ArtToolId id);
    ArtTool* ActiveTool() const;
    // Display quality from the toolbar slider: 0 outlines, 1 fills and
    // lines, 2 antialiased (VectorDisplayQuality).
    void SetDisplayQuality(int level);
    static const char* DisplayQualityName(VectorDisplayQuality quality);
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
    std::shared_ptr<UltraCanvasSlider>          qualitySlider;
    std::shared_ptr<UltraCanvasLabel>           qualityLabel;
    // The palette's slots: one per group of tools (see PaletteGroups()).
    std::vector<std::shared_ptr<UltraCanvasToolGroupButton>> toolSlots;
    std::shared_ptr<UltraCanvasContainer>       iconPanel;
    std::shared_ptr<UltraCanvasDropdown>        iconSizeBox;
    std::shared_ptr<ArtIconPreview>             iconPreview;
    std::shared_ptr<UltraCanvasLabel>           iconCaption;

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
    int iconSize = 0;
    bool iconGrid = true, iconSnap = true, iconMarkSoft = true;
    VectorGridSpec gridBeforeIcon;
    VectorSnapOptions snapBeforeIcon;
    std::shared_ptr<const ArtIconRaster> iconRaster;
    ArtCreatorTextResult lastText;

    // in-app clipboard: clones of the copied elements
    std::vector<std::shared_ptr<VectorStorage::VectorElement>> clipboard;
    int pasteCount = 0;
    // the picture of them the last copy put on the system clipboard: while
    // the clipboard still holds exactly these bytes, a paste is this
    // window's own copy and comes back as the objects themselves
    std::vector<uint8_t> clipboardPng;

    double pointerX = 0, pointerY = 0;
    bool pointerInside = false;
};

} // namespace UltraCanvas
