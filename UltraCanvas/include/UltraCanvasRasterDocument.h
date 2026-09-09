// include/UltraCanvasRasterDocument.h
// The model behind a bitmap editor: a stack of UCRasterLayers of one canvas
// size, the selection, an undo / redo history, a cached composite pixmap
// for display, and file load / save through PixelFX.
//
// The document is deliberately free of any UI: UltraCanvasPaintSurface
// displays it, UltraPaint's tools edit it through the brush engine, and a
// batch job could drive it with no window at all. Every edit is bracketed by
// BeginEdit() / EndEdit() (pixel changes on one layer) or recorded with
// PushStructuralUndo() (anything that changes the layer list, the canvas or
// the selection) so Undo() / Redo() can restore it.
//
// Filters are applied with ApplyFilter(): the callback receives the active
// layer as a PixelFX image and returns the processed one; the document
// writes it back only where the selection covers, so every PixelFX
// operation is selection-aware without knowing about selections.
// Version: 1.0.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasRasterLayer.h"
#include "UltraCanvasRasterSelection.h"
#include "UltraCanvasImage.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// ===== UNDO ENTRY =====
// Either a pixel snapshot of one rectangle of one layer (a stroke, a fill,
// a filter) or a full structural snapshot (layer list + selection + canvas
// size). Structural snapshots clone every layer, so they are only taken
// for operations that need them.
struct RasterUndoEntry {
    std::string label;
    bool structural = false;
    // pixel edit
    int layerIndex = -1;
    Rect2Di rect;
    std::vector<uint8_t> before;
    std::vector<uint8_t> after;
    // structural edit
    int width = 0, height = 0;
    int activeLayer = 0;
    std::vector<std::shared_ptr<UCRasterLayer>> layersBefore;
    std::vector<std::shared_ptr<UCRasterLayer>> layersAfter;
    std::shared_ptr<UCRasterSelection> selectionBefore;
    std::shared_ptr<UCRasterSelection> selectionAfter;
    int widthAfter = 0, heightAfter = 0;
    int activeLayerAfter = 0;
    size_t Bytes() const;
};

class UCRasterDocument {
public:
    UCRasterDocument() = default;
    // A document with one layer of `width` x `height`, filled with `background`
    // (transparent for a transparent canvas).
    UCRasterDocument(int width, int height, const RasterPixel& background = RasterPixel(255, 255, 255, 255));

    // ===== CANVAS =====
    int  GetWidth()  const { return width; }
    int  GetHeight() const { return height; }
    bool IsValid()   const { return width > 0 && height > 0 && !layers.empty(); }
    Rect2Di GetRect() const { return Rect2Di(0, 0, width, height); }

    // ===== LAYERS =====
    int GetLayerCount() const { return static_cast<int>(layers.size()); }
    std::shared_ptr<UCRasterLayer> GetLayer(int index) const;
    // Index 0 is the bottom of the stack.
    const std::vector<std::shared_ptr<UCRasterLayer>>& GetLayers() const { return layers; }
    int  GetActiveLayerIndex() const { return activeLayer; }
    void SetActiveLayerIndex(int index);
    std::shared_ptr<UCRasterLayer> GetActiveLayer() const { return GetLayer(activeLayer); }

    // Each of these records a structural undo entry and marks the document
    // modified. They return the index of the affected layer (-1 when refused).
    int  AddLayer(const std::string& name = "", const RasterPixel& fill = RasterPixel(0, 0, 0, 0));
    int  AddLayer(std::shared_ptr<UCRasterLayer> layer, int atIndex = -1);   // -1: above the active layer
    int  DuplicateLayer(int index);
    bool RemoveLayer(int index);
    bool MoveLayer(int fromIndex, int toIndex);
    bool MergeLayerDown(int index);
    bool FlattenImage();
    // Attribute edits (name / visibility / opacity / blend / lock) do not
    // touch pixels; they are undoable structural entries too.
    void SetLayerVisible(int index, bool visible);
    void SetLayerOpacity(int index, float opacity);
    void SetLayerBlendMode(int index, RasterBlendMode mode);
    void SetLayerName(int index, const std::string& name);
    void SetLayerLocked(int index, bool locked);

    // ===== SELECTION =====
    UCRasterSelection&       GetSelection()       { return selection; }
    const UCRasterSelection& GetSelection() const { return selection; }
    // Call after changing the selection through GetSelection() so the change
    // is undoable and views refresh. `label` names the undo entry.
    void CommitSelectionChange(const std::string& label = "Selection");

    // ===== WHOLE-IMAGE GEOMETRY (structural, undoable) =====
    // Resample every layer to the new size (PixelFX Lanczos when available,
    // bilinear otherwise).
    void ScaleImage(int newWidth, int newHeight);
    // Change the canvas without resampling; existing pixels are placed at
    // (offsetX, offsetY) of the new canvas.
    void ResizeCanvas(int newWidth, int newHeight, int offsetX, int offsetY);
    // Crop the canvas to `r` (every layer, the selection is cleared).
    void CropTo(const Rect2Di& r);
    void FlipHorizontal();
    void FlipVertical();
    void Rotate90(bool clockwise);
    void Rotate180();

    // ===== PIXEL EDITS =====
    // Bracket a pixel change of `rect` on `layerIndex` (clipped to the
    // canvas). Nested calls are not supported; a second BeginEdit before
    // EndEdit ends the first one. EndEdit takes the "after" snapshot,
    // pushes the undo entry and notifies views for the rectangle.
    void BeginEdit(const std::string& label, int layerIndex, const Rect2Di& rect);
    bool IsEditing() const { return editing; }
    void EndEdit();
    void CancelEdit();   // restore the "before" pixels, no undo entry
    // While an edit is open, tell views the rectangle changed (a brush
    // stroke calls this per dab so the surface repaints incrementally).
    void NotifyChanged(const Rect2Di& rect);
    // Record an edit whose "before" pixels the caller kept itself (a brush
    // stroke keeps a copy of the layer it started on): `rect` of
    // `layerIndex` is snapshotted from `before` and from the layer as it is
    // now. Cheaper than BeginEdit() over the whole layer when the extent of
    // the change is only known at the end.
    void RecordEdit(const std::string& label, int layerIndex, const Rect2Di& rect, const UCRasterLayer& before);

    // Run a PixelFX operation over the active layer, writing the result back
    // where the selection covers. `op` gets a 4-band uchar sRGB image and
    // must return an image of the SAME size (size-changing operations go
    // through ScaleImage / ResizeCanvas). Returns false and leaves the layer
    // untouched when the op throws or returns an incompatible image. Only
    // in builds with libvips (HAS_LIBVIPS).
#ifdef HAS_LIBVIPS
    using PixelFXOp = std::function<PixelFX::PFXImage(const PixelFX::PFXImage&)>;
    bool ApplyFilter(const std::string& label, const PixelFXOp& op);
    // The same, over an explicit layer.
    bool ApplyFilterToLayer(const std::string& label, int layerIndex, const PixelFXOp& op);
    // Run `op` over a copy of the active layer and return the result as a
    // layer, without touching the document — the live preview of a filter
    // dialog. Null when the op fails.
    std::shared_ptr<UCRasterLayer> PreviewFilter(const PixelFXOp& op) const;
#endif
    // Copy `processed`'s pixels into `layerIndex` where the selection covers
    // (both must be canvas-sized). This is what ApplyFilter does after the op;
    // exposed for callers that compute a result without PixelFX.
    void ApplyProcessedLayer(const std::string& label, int layerIndex, const UCRasterLayer& processed);

    // ===== CLIPBOARD-STYLE HELPERS =====
    // The selected pixels of the active layer as a layer of the selection's
    // bounding size (with the selection applied as alpha); null when there is
    // no active layer. `outOrigin` receives the bounds' top-left.
    std::shared_ptr<UCRasterLayer> CopySelection(Point2Di& outOrigin) const;
    // The same, from the composite of all visible layers.
    std::shared_ptr<UCRasterLayer> CopySelectionMerged(Point2Di& outOrigin) const;
    // Erase the selected pixels of the active layer (transparent).
    void DeleteSelection();
    // Fill the selection on the active layer with a colour.
    void FillSelection(const RasterPixel& colour);

    // ===== UNDO / REDO =====
    bool CanUndo() const { return !undoStack.empty(); }
    bool CanRedo() const { return !redoStack.empty(); }
    std::string GetUndoLabel() const { return undoStack.empty() ? "" : undoStack.back().label; }
    std::string GetRedoLabel() const { return redoStack.empty() ? "" : redoStack.back().label; }
    void Undo();
    void Redo();
    void ClearHistory();
    // Memory budget for the history; the oldest entries go first.
    void SetUndoMemoryLimit(size_t bytes) { undoMemoryLimit = bytes; TrimHistory(); }
    size_t GetUndoMemoryUsed() const { return undoMemoryUsed; }
    // Record a structural snapshot around a change made directly to the
    // layers (BeginStructuralUndo before, EndStructuralUndo after).
    void BeginStructuralUndo(const std::string& label);
    void EndStructuralUndo();

    // ===== COMPOSITE FOR DISPLAY =====
    // Every visible layer composited over transparency into a premultiplied
    // ARGB32 pixmap of canvas size. Cached: only rectangles invalidated
    // since the last call are recomposited.
    std::shared_ptr<UCPixmap> GetCompositePixmap();
    // Same as a straight-RGBA layer (used for save and "copy merged").
    std::shared_ptr<UCRasterLayer> Flatten() const;
    void InvalidateComposite(const Rect2Di& rect);
    void InvalidateComposite() { InvalidateComposite(GetRect()); }

    // ===== FILES =====
    // Load an image file as a single-layer document (PixelFX). Replaces the
    // current contents and history. Returns false with `error` filled.
    bool LoadFromFile(const std::string& path, std::string& error);
    // Save the flattened image through PixelFX with format from the
    // extension (`options` for the format-specific knobs when given).
    bool SaveToFile(const std::string& path, std::string& error,
                    const UCImageSave::ImageExportOptions* options = nullptr);
    // Layered project file (*.ucraster): a ZIP holding document.json and one
    // PNG per layer. Keeps layers, names, opacity, blend modes.
    bool SaveProject(const std::string& path, std::string& error);
    bool LoadProject(const std::string& path, std::string& error);
    static bool IsProjectFile(const std::string& path);

    const std::string& GetFilePath() const { return filePath; }
    void SetFilePath(const std::string& p) { filePath = p; }
    bool IsModified() const { return modified; }
    void SetModified(bool m);

    // ===== NOTIFICATIONS =====
    // Pixels of `rect` (canvas coordinates) changed — views repaint it.
    std::function<void(const Rect2Di&)> onPixelsChanged;
    // Layer list / attributes / active layer / canvas size changed.
    std::function<void()> onStructureChanged;
    // Selection changed.
    std::function<void()> onSelectionChanged;
    // Modified flag flipped, undo/redo availability changed.
    std::function<void()> onStateChanged;

private:
    void PushUndo(RasterUndoEntry&& e);
    void TrimHistory();
    void ApplyStructural(const RasterUndoEntry& e, bool toAfter);
    void ApplyPixels(const RasterUndoEntry& e, bool toAfter);
    void ResetComposite();
    void EmitStructure();
    static std::vector<uint8_t> SnapshotRect(const UCRasterLayer& layer, const Rect2Di& r);
    static void RestoreRect(UCRasterLayer& layer, const Rect2Di& r, const std::vector<uint8_t>& data);
    std::vector<std::shared_ptr<UCRasterLayer>> CloneLayers() const;
    // Swap in a replacement layer object (attribute edits), undoably.
    void ReplaceLayer(int index, std::shared_ptr<UCRasterLayer> replacement, const std::string& label);

    int width = 0;
    int height = 0;
    std::vector<std::shared_ptr<UCRasterLayer>> layers;
    int activeLayer = 0;
    UCRasterSelection selection;
    // The selection as last recorded in the history: the "before" of the
    // next CommitSelectionChange().
    std::shared_ptr<UCRasterSelection> lastCommittedSelection;

    // open pixel edit
    bool editing = false;
    RasterUndoEntry pendingEdit;
    // open structural edit
    bool structuralOpen = false;
    RasterUndoEntry pendingStructural;

    std::deque<RasterUndoEntry> undoStack;
    std::deque<RasterUndoEntry> redoStack;
    size_t undoMemoryLimit = 768ull * 1024 * 1024;
    size_t undoMemoryUsed = 0;

    std::shared_ptr<UCPixmap> composite;
    std::vector<Rect2Di> compositeDirty;
    bool compositeAllDirty = true;

    std::string filePath;
    bool modified = false;
};

} // namespace UltraCanvas
