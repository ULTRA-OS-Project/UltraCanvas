// Apps/UltraPaint/UltraPaintWindow.h
// UltraPaint main window: menu bar, main toolbar, the tool palette on the
// left, the paint surface in the middle, the colour / tool options / layers
// panels on the right and a status bar at the bottom. Owns the document,
// the tools and every command (file, edit, image, layer, select, adjust,
// filter, view).
//
// The editor is multi-window: the class also holds the registry of open
// windows, so File > New Window and "Open new window" on a dropped image add
// to it and the application exits with the last of them.
// Version: 1.1.0
// Last Modified: 2026-09-12
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
#include "UltraCanvasSlider.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasPaintSurface.h"
#include "UltraCanvasRasterDocument.h"

#include "UltraPaintTools.h"
#include "UltraPaintFilters.h"
#include "UltraPaintDialogs.h"

#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

class UltraPaintWindow {
public:
    UltraPaintWindow();
    ~UltraPaintWindow();

    // Creates the window; opens `paths[0]` when given, else a blank canvas.
    bool Initialize(const std::vector<std::string>& paths);
    void Show();

    // Creates, registers and shows a further editor window on `paths` (empty:
    // a blank canvas). The windows own themselves — the registry drops one
    // when it closes, and the application exits with the last of them — so
    // the returned pointer may be ignored.
    static std::shared_ptr<UltraPaintWindow> OpenWindow(const std::vector<std::string>& paths);
    static int WindowCount();

private:
    // ----- window registry -----
    static std::vector<std::shared_ptr<UltraPaintWindow>>& OpenWindows();
    // Drops `window` from the registry on the next turn of the event loop:
    // the call arrives from inside that window's own close, so the object
    // cannot be destroyed yet.
    static void RetireWindow(UltraPaintWindow* closed);

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
    void SetDocument(std::shared_ptr<UCRasterDocument> doc, const std::string& title);
    void NewImage(const UltraPaintNewImageResult& r);
    void OpenFile(const std::string& path);
    // The straight "replace what is open with this file" path; OpenFile()
    // sends vector artwork through the import dialog first.
    bool LoadIntoWindow(const std::string& path);

    // ----- import: dropped files and vector artwork -----
    // Files dropped on the canvas: asks once what to do with them, then does
    // it to all of them.
    void HandleDroppedFiles(const std::vector<std::string>& files);
    // Asks what to do with `path` — open it here, open a new window for it,
    // or merge it into the open image — and carries the answer out.
    // `fromDrop` decides which of those the dialog offers; `alsoFiles` are the
    // rest of a multi-file drop, which follow the same answer.
    void ImportFile(const std::string& path, bool fromDrop,
                    const std::vector<std::string>& alsoFiles = {});
    // Carries out the dialog's answer. `preloaded` is the document already
    // read for the dialog (a dropped bitmap is decoded to measure it), so the
    // file is not read a second time; null means read it now.
    void ApplyImport(const std::string& path, const UltraPaintImportResult& result,
                     std::shared_ptr<UCRasterDocument> preloaded = nullptr);
    // Reads a file into a document, rasterizing vector artwork at `result`'s
    // size and page. Null with the reason already shown when it cannot be read.
    std::shared_ptr<UCRasterDocument> LoadDocument(const std::string& path,
                                                   const UltraPaintImportResult& result);
    // Merges a document's flattened pixels into the open image as a new layer.
    void MergeDocument(const std::shared_ptr<UCRasterDocument>& incoming,
                       const std::string& path, bool scaleToFit);
    bool SaveToPath(const std::string& path);
    void ConfirmDiscard(const std::string& question, const std::function<void()>& proceed);
    void UpdateTitle();
    void UpdateStatus();
    void UpdateUndoButtons();

    // ----- commands -----
    static std::vector<std::string> OpenableExtensions();
    void CmdNew();
    void CmdNewWindow();
    void CmdOpen();
    void CmdImport();
    void CmdSave();
    void CmdSaveAs();
    void CmdExport();
    void CmdUndo();
    void CmdRedo();
    void CmdCut();
    void CmdCopy(bool merged);
    void CmdPaste();
    void CmdPasteAsNew();
    void CmdDelete();
    void CmdFill(bool foreground);
    void CmdScaleImage();
    void CmdCanvasSize();
    void CmdCropToSelection();
    void CmdFlatten();
    void CmdLayerAdd();
    void CmdLayerDuplicate();
    void CmdLayerDelete();
    void CmdLayerMergeDown();
    void CmdLayerMove(int delta);
    void CmdLayerProperties();
    void CmdSelectAll();
    void CmdSelectNone();
    void CmdSelectInvert();
    void CmdSelectModify(int kind);   // 0 feather, 1 grow, 2 shrink
    void CmdSelectFromAlpha();
    void CmdCurves();
    void CmdColourToAlpha();
    void CmdFilter(const PaintFilter& filter);
    void CmdAbout();
    void CmdQuit();

    // ----- tools -----
    void SelectTool(PaintToolId id);
    void RebuildToolOptions();
    void RebuildLayerPanel();
    PaintTool* ActiveTool() const;
    void PlaceText(double x, double y, const UltraPaintTextResult& r);

    // ----- filters with preview -----
#ifdef HAS_LIBVIPS
    void BeginPreview();
    void ShowPreview(const std::function<PixelFX::PFXImage(const PixelFX::PFXImage&)>& op);
    void EndPreview(bool commit, const std::string& label,
                    const std::function<PixelFX::PFXImage(const PixelFX::PFXImage&)>& op);
#endif

    // ----- members -----
    std::shared_ptr<UltraCanvasWindow>         window;
    std::shared_ptr<UltraCanvasMenu>           menuBar;
    std::shared_ptr<UltraCanvasToolbar>        toolbar;
    std::shared_ptr<UltraCanvasContainer>      body;          // palette | surface | right panel
    std::shared_ptr<UltraCanvasToolbar>        palette;
    std::shared_ptr<UltraCanvasPaintSurface>   surface;
    std::shared_ptr<UltraCanvasContainer>      rightPanel;
    std::shared_ptr<UltraCanvasColorPicker>    colorPicker;
    std::shared_ptr<UltraCanvasColorSwatchBar> swatches;
    std::shared_ptr<UltraCanvasContainer>      optionsPanel;
    std::shared_ptr<UltraCanvasLabel>          optionsTitle;
    std::shared_ptr<UltraCanvasContainer>      layersPanel;
    std::shared_ptr<UltraCanvasContainer>      layerRows;
    std::shared_ptr<UltraCanvasSlider>         layerOpacity;
    std::shared_ptr<UltraCanvasDropdown>       layerBlend;
    std::shared_ptr<UltraCanvasContainer>      statusBar;
    std::shared_ptr<UltraCanvasLabel>          statusPos, statusZoom, statusSize, statusHint;
    std::shared_ptr<UltraCanvasButton>         undoButton, redoButton;
    std::vector<std::shared_ptr<UltraCanvasButton>> toolButtons;

    std::shared_ptr<UCRasterDocument> document;
    std::vector<std::unique_ptr<PaintTool>> tools;
    PaintToolOptions toolOptions;
    PaintToolContext toolContext;
    PaintToolId activeTool = PaintToolId::Brush;
    RasterPixel foreground = RasterPixel(0, 0, 0, 255);
    RasterPixel background = RasterPixel(255, 255, 255, 255);

    // in-app clipboard (the system clipboard gets a PNG too when it can)
    std::shared_ptr<UCRasterLayer> clipboardLayer;
    Point2Di clipboardOrigin;

    // filter preview state
    std::shared_ptr<UCRasterLayer> previewOriginal;
    int previewLayerIndex = -1;
    bool previewActive = false;
    std::weak_ptr<UltraCanvasWindow> activeDialog;

    UltraPaintNewImageResult lastNewImage;
    // A command-line vector drawing: its size dialog waits until the window
    // is on screen rather than opening behind it.
    std::string pendingImport;
    double pointerX = 0, pointerY = 0;
    bool pointerInside = false;
};

} // namespace UltraCanvas
