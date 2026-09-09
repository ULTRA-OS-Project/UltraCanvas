// Apps/UltraPaint/UltraPaintWindow.h
// UltraPaint main window: menu bar, main toolbar, the tool palette on the
// left, the paint surface in the middle, the colour / tool options / layers
// panels on the right and a status bar at the bottom. Owns the document,
// the tools and every command (file, edit, image, layer, select, adjust,
// filter, view).
// Version: 1.0.0
// Last Modified: 2026-09-06
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

private:
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
    bool SaveToPath(const std::string& path);
    void ConfirmDiscard(const std::string& question, const std::function<void()>& proceed);
    void UpdateTitle();
    void UpdateStatus();
    void UpdateUndoButtons();

    // ----- commands -----
    void CmdNew();
    void CmdOpen();
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
    double pointerX = 0, pointerY = 0;
    bool pointerInside = false;
};

} // namespace UltraCanvas
