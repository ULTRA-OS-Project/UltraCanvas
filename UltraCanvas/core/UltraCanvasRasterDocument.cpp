// core/UltraCanvasRasterDocument.cpp
// Layer stack, selection, undo / redo, cached composite and file IO of a
// raster document.
//
// Undo policy: pixel edits snapshot one rectangle of one layer (before /
// after); structural edits record the layer *list* before and after. To
// keep that cheap, structural operations never modify a layer object in
// place — they replace it with a new object — so unchanged layers are shared
// between the snapshots and cost nothing. Attribute edits (name, opacity,
// visibility, blend, lock) record just the attribute set.
//
// Text layers: a pixel edit takes the text off the layer in place and keeps
// it in its undo entry (RasterUndoEntry::textBefore), the same way it keeps
// the pixels; structural operations carry the text with the layer objects
// they share or replace.
// Version: 1.4.0 - *.ucd: the UCD 2.1 layered image (SaveUCD / LoadUCD) - layers, blend
//                 modes and text layers as text frames, with a PNG thumbnail
// Version: 1.3.0 - text layers (RasterizeLayer, ReplaceLayer, in-place edit,
//                 the text kept in .ucraster projects)
// Version: 1.2.0 - SaveToFile without options writes with the user's save
//                 settings for the format (UltraCanvasFileLoader), once set
// Last Modified: 2026-10-16
// Author: UltraCanvas Framework

#include "UltraCanvasRasterDocument.h"
#include "UltraCanvasRichDocument.h"
#include "Plugins/Documents/Word/UltraCanvasWordDocumentIO.h"
#include "UltraCanvasZipPackage.h"
#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasFileLoader.h"   // the user's save settings per format
#include "UltraCanvasUCDContainer.h"

#include "miniz.h"   // the *.ucd thumbnail's PNG

#ifdef HAS_LIBVIPS
#include "PixelFX/PixelFX.h"
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include "UltraCanvasPathUtf8.h"

namespace UltraCanvas {

namespace {
    std::string LowerExt(const std::string& path) {
        const size_t dot = path.find_last_of('.');
        const size_t slash = path.find_last_of("/\\");
        if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
        std::string e = path.substr(dot + 1);
        std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return std::tolower(c); });
        return e;
    }

    // A text layer's words go into a project as an OpenDocument text: the
    // framework's word-processing format, which keeps every run's font,
    // size and colour, the paragraphs' alignment and spacing, lists, tables
    // and pictures - and opens in any word processor. Its reader and writer
    // work on files, so the bytes pass through a temporary one (an internal
    // file of the program's, never one the user chose).
    std::filesystem::path TemporaryOdtPath() {
        static std::atomic<unsigned> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        return std::filesystem::temp_directory_path() /
               PathFromUtf8("ucraster-text-" + std::to_string(stamp) + "-" +
                            std::to_string(counter.fetch_add(1)) + ".odt");
    }

    bool RichDocumentToOdt(const UCRichDocument& document, std::vector<uint8_t>& bytes, std::string& error) {
        std::error_code ec;
        const std::filesystem::path file = TemporaryOdtPath();
        bool ok = UCWordDocumentIO::SaveOdt(PathToUtf8(file), document, error);
        if (ok) {
            std::ifstream in(file, std::ios::binary);
            bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            ok = !bytes.empty();
            if (!ok) error = "Could not read the text back";
        }
        std::filesystem::remove(file, ec);
        return ok;
    }

    bool RichDocumentFromOdt(const std::vector<uint8_t>& bytes, UCRichDocument& document, std::string& error) {
        std::error_code ec;
        const std::filesystem::path file = TemporaryOdtPath();
        bool ok;
        {
            std::ofstream out(file, std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            ok = static_cast<bool>(out);
        }
        ok = ok && UCWordDocumentIO::LoadOdt(PathToUtf8(file), document, error);
        std::filesystem::remove(file, ec);
        return ok;
    }

    Rect2Di UnionRect(const Rect2Di& a, const Rect2Di& b) {
        if (a.width <= 0 || a.height <= 0) return b;
        if (b.width <= 0 || b.height <= 0) return a;
        const int x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
        const int x1 = std::max(a.x + a.width, b.x + b.width);
        const int y1 = std::max(a.y + a.height, b.y + b.height);
        return Rect2Di(x0, y0, x1 - x0, y1 - y0);
    }
}

size_t RasterUndoEntry::Bytes() const {
    size_t n = before.size() + after.size() + sizeof(*this);
    // Shared layer objects are counted once per entry on the assumption
    // that structural entries mostly share; the budget is a guide, not an
    // accounting.
    for (const auto& l : layersBefore) if (l) n += l->DataSize() / 8;
    for (const auto& l : layersAfter)  if (l) n += l->DataSize() / 8;
    if (selectionBefore) n += selectionBefore->GetMask().size();
    if (selectionAfter)  n += selectionAfter->GetMask().size();
    return n;
}

// ===========================================================================
// CONSTRUCTION
// ===========================================================================

UCRasterDocument::UCRasterDocument(int w, int h, const RasterPixel& background)
    : width(std::max(1, w)), height(std::max(1, h)), selection(std::max(1, w), std::max(1, h)) {
    layers.push_back(std::make_shared<UCRasterLayer>(width, height, background, "Background"));
    activeLayer = 0;
}

std::shared_ptr<UCRasterLayer> UCRasterDocument::GetLayer(int index) const {
    if (index < 0 || index >= static_cast<int>(layers.size())) return nullptr;
    return layers[static_cast<size_t>(index)];
}

void UCRasterDocument::SetActiveLayerIndex(int index) {
    if (layers.empty()) { activeLayer = 0; return; }
    index = std::clamp(index, 0, static_cast<int>(layers.size()) - 1);
    if (index == activeLayer) return;
    activeLayer = index;
    EmitStructure();
}

void UCRasterDocument::SetModified(bool m) {
    if (modified == m) return;
    modified = m;
    EmitState();
}

// ===== NOTIFICATIONS =====
// The slot first, then every listener - on a copy of the list, so a
// callback that removes its own listener (or adds one) does not pull the
// list out from under the loop.

UCRasterDocument::ListenerId UCRasterDocument::AddListener(Listener listener) {
    const ListenerId id = nextListenerId++;
    listeners.emplace_back(id, std::move(listener));
    return id;
}

void UCRasterDocument::RemoveListener(ListenerId id) {
    listeners.erase(std::remove_if(listeners.begin(), listeners.end(),
                                   [id](const std::pair<ListenerId, Listener>& l) { return l.first == id; }),
                    listeners.end());
}

void UCRasterDocument::EmitStructure() {
    if (onStructureChanged) onStructureChanged();
    const auto snapshot = listeners;
    for (const auto& l : snapshot) if (l.second.structureChanged) l.second.structureChanged();
}

void UCRasterDocument::EmitPixels(const Rect2Di& rect) {
    if (onPixelsChanged) onPixelsChanged(rect);
    const auto snapshot = listeners;
    for (const auto& l : snapshot) if (l.second.pixelsChanged) l.second.pixelsChanged(rect);
}

void UCRasterDocument::EmitSelection() {
    if (onSelectionChanged) onSelectionChanged();
    const auto snapshot = listeners;
    for (const auto& l : snapshot) if (l.second.selectionChanged) l.second.selectionChanged();
}

void UCRasterDocument::EmitState() {
    if (onStateChanged) onStateChanged();
    const auto snapshot = listeners;
    for (const auto& l : snapshot) if (l.second.stateChanged) l.second.stateChanged();
}

std::vector<std::shared_ptr<UCRasterLayer>> UCRasterDocument::CloneLayers() const {
    std::vector<std::shared_ptr<UCRasterLayer>> out;
    out.reserve(layers.size());
    for (const auto& l : layers) out.push_back(l ? l->Clone() : nullptr);
    return out;
}

// ===========================================================================
// STRUCTURAL UNDO
// ===========================================================================

void UCRasterDocument::BeginStructuralUndo(const std::string& label) {
    if (structuralOpen) EndStructuralUndo();
    pendingStructural = RasterUndoEntry();
    pendingStructural.label = label;
    pendingStructural.structural = true;
    pendingStructural.width = width;
    pendingStructural.height = height;
    pendingStructural.activeLayer = activeLayer;
    pendingStructural.layersBefore = layers;   // shared: ops replace, never mutate
    pendingStructural.selectionBefore = std::make_shared<UCRasterSelection>(selection);
    structuralSelectionVersion = selection.GetVersion();
    structuralOpen = true;
}

void UCRasterDocument::EndStructuralUndo() {
    if (!structuralOpen) return;
    structuralOpen = false;
    pendingStructural.widthAfter = width;
    pendingStructural.heightAfter = height;
    pendingStructural.activeLayerAfter = activeLayer;
    pendingStructural.layersAfter = layers;
    pendingStructural.selectionAfter = std::make_shared<UCRasterSelection>(selection);
    // A canvas-geometry change re-shapes the selection (CropTo, ScaleImage,
    // ResizeCanvas and Rotate90 all drop it). That is a selection change like
    // any other: the committed baseline has to move with it, or the next
    // CommitSelectionChange() would record a "before" of the old canvas size
    // and an undo would restore a selection that no longer fits the image -
    // after which the selection's bounds lie outside the canvas and every
    // crop to them quietly does nothing.
    const bool selectionChanged = selection.GetVersion() != structuralSelectionVersion;
    lastCommittedSelection = pendingStructural.selectionAfter;
    PushUndo(std::move(pendingStructural));
    SetModified(true);
    EmitStructure();
    if (selectionChanged) EmitSelection();
}

void UCRasterDocument::ApplyStructural(const RasterUndoEntry& e, bool toAfter) {
    const int newW = toAfter ? e.widthAfter : e.width;
    const int newH = toAfter ? e.heightAfter : e.height;
    const bool sizeChanged = newW != width || newH != height;
    width = newW; height = newH;
    layers = toAfter ? e.layersAfter : e.layersBefore;
    activeLayer = std::clamp(toAfter ? e.activeLayerAfter : e.activeLayer, 0,
                             std::max(0, static_cast<int>(layers.size()) - 1));
    // A selection only means anything on the canvas it was made on, so one
    // recorded at another size is dropped rather than restored.
    const auto& sel = toAfter ? e.selectionAfter : e.selectionBefore;
    if (sel && sel->GetWidth() == width && sel->GetHeight() == height) selection.Assign(*sel);
    else selection.Resize(width, height);
    if (sizeChanged) ResetComposite(); else InvalidateComposite();
    EmitStructure();
    EmitSelection();
}

// ===========================================================================
// LAYERS
// ===========================================================================

int UCRasterDocument::AddLayer(const std::string& name, const RasterPixel& fill) {
    std::string n = name;
    if (n.empty()) n = "Layer " + std::to_string(layers.size() + 1);
    return AddLayer(std::make_shared<UCRasterLayer>(width, height, fill, n), -1);
}

int UCRasterDocument::AddLayer(std::shared_ptr<UCRasterLayer> layer, int atIndex, const std::string& label) {
    if (!layer) return -1;
    if (layer->GetWidth() != width || layer->GetHeight() != height) {
        auto fitted = std::make_shared<UCRasterLayer>(width, height, layer->name);
        fitted->visible = layer->visible; fitted->opacity = layer->opacity;
        fitted->blendMode = layer->blendMode; fitted->locked = layer->locked;
        fitted->text = layer->text;
        fitted->CopyFrom(*layer, 0, 0);
        layer = fitted;
    }
    BeginStructuralUndo(label);
    int idx = atIndex < 0 ? activeLayer + 1 : atIndex;
    idx = std::clamp(idx, 0, static_cast<int>(layers.size()));
    layers.insert(layers.begin() + idx, layer);
    activeLayer = idx;
    InvalidateComposite();
    EndStructuralUndo();
    return idx;
}

int UCRasterDocument::DuplicateLayer(int index) {
    auto src = GetLayer(index);
    if (!src) return -1;
    auto copy = src->Clone();
    copy->name = src->name + " copy";
    BeginStructuralUndo("Duplicate Layer");
    layers.insert(layers.begin() + index + 1, copy);
    activeLayer = index + 1;
    InvalidateComposite();
    EndStructuralUndo();
    return activeLayer;
}

bool UCRasterDocument::RemoveLayer(int index) {
    if (index < 0 || index >= static_cast<int>(layers.size()) || layers.size() <= 1) return false;
    BeginStructuralUndo("Delete Layer");
    layers.erase(layers.begin() + index);
    activeLayer = std::clamp(activeLayer >= index ? activeLayer - 1 : activeLayer, 0,
                             static_cast<int>(layers.size()) - 1);
    InvalidateComposite();
    EndStructuralUndo();
    return true;
}

bool UCRasterDocument::MoveLayer(int from, int to) {
    const int n = static_cast<int>(layers.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return false;
    BeginStructuralUndo("Move Layer");
    auto l = layers[static_cast<size_t>(from)];
    layers.erase(layers.begin() + from);
    layers.insert(layers.begin() + to, l);
    if (activeLayer == from) activeLayer = to;
    InvalidateComposite();
    EndStructuralUndo();
    return true;
}

bool UCRasterDocument::MergeLayerDown(int index) {
    if (index <= 0 || index >= static_cast<int>(layers.size())) return false;
    auto upper = layers[static_cast<size_t>(index)];
    auto lower = layers[static_cast<size_t>(index - 1)];
    BeginStructuralUndo("Merge Down");
    auto merged = lower->Clone();
    merged->text.reset();   // its pixels are no longer what its text draws
    if (upper->visible) merged->BlendFrom(*upper, 0, 0, upper->opacity, upper->blendMode);
    layers[static_cast<size_t>(index - 1)] = merged;
    layers.erase(layers.begin() + index);
    activeLayer = index - 1;
    InvalidateComposite();
    EndStructuralUndo();
    return true;
}

bool UCRasterDocument::FlattenImage() {
    if (layers.size() <= 1) return false;
    BeginStructuralUndo("Flatten Image");
    auto flat = Flatten();
    flat->name = "Background";
    layers.clear();
    layers.push_back(flat);
    activeLayer = 0;
    InvalidateComposite();
    EndStructuralUndo();
    return true;
}

bool UCRasterDocument::ReplaceLayer(int index, std::shared_ptr<UCRasterLayer> replacement, const std::string& label) {
    if (index < 0 || index >= static_cast<int>(layers.size()) || !replacement) return false;
    if (replacement->GetWidth() != width || replacement->GetHeight() != height) return false;
    BeginStructuralUndo(label);
    layers[static_cast<size_t>(index)] = std::move(replacement);
    InvalidateComposite();
    EndStructuralUndo();
    return true;
}

bool UCRasterDocument::RasterizeLayer(int index) {
    auto l = GetLayer(index);
    if (!l || !l->IsTextLayer()) return false;
    auto copy = l->Clone();
    copy->text.reset();
    return ReplaceLayer(index, copy, "Rasterize Layer");
}

void UCRasterDocument::SetInPlaceEditLayer(const std::shared_ptr<UCRasterLayer>& layer) {
    const auto previous = inPlaceEditLayer.lock();
    if (previous == layer) return;
    inPlaceEditLayer = layer;
    // Only the pixels the two layers cover change in the composite: a text
    // layer's box, or what a bitmap layer paints.
    Rect2Di changed;
    for (const auto& l : { previous, layer }) {
        if (!l) continue;
        Rect2Di covered;
        if (l->text) {
            const Rect2Dd box = l->text->GetBox();
            const int x0 = static_cast<int>(std::floor(box.x)), y0 = static_cast<int>(std::floor(box.y));
            covered = Rect2Di(x0, y0, static_cast<int>(std::ceil(box.x + box.width)) - x0,
                              static_cast<int>(std::ceil(box.y + box.height)) - y0);
        } else {
            covered = l->GetOpaqueBounds();
        }
        changed = UnionRect(changed, covered);
    }
    if (changed.width <= 0 || changed.height <= 0) return;
    InvalidateComposite(changed);
    EmitPixels(changed);
}

// Attribute edits replace the layer object with a copy carrying the new
// attribute: structural snapshots share layer objects, so an in-place edit
// would change history too.
namespace {
    template <typename F>
    std::shared_ptr<UCRasterLayer> WithAttribute(const UCRasterLayer& l, F&& apply) {
        auto copy = l.Clone();
        apply(*copy);
        return copy;
    }
}

void UCRasterDocument::SetLayerVisible(int index, bool visible) {
    auto l = GetLayer(index);
    if (!l || l->visible == visible) return;
    ReplaceLayer(index, WithAttribute(*l, [&](UCRasterLayer& c) { c.visible = visible; }), visible ? "Show Layer" : "Hide Layer");
}

void UCRasterDocument::SetLayerOpacity(int index, float opacity) {
    auto l = GetLayer(index);
    opacity = std::clamp(opacity, 0.0f, 1.0f);
    if (!l || std::fabs(l->opacity - opacity) < 1e-4f) return;
    ReplaceLayer(index, WithAttribute(*l, [&](UCRasterLayer& c) { c.opacity = opacity; }), "Layer Opacity");
}

void UCRasterDocument::SetLayerBlendMode(int index, RasterBlendMode mode) {
    auto l = GetLayer(index);
    if (!l || l->blendMode == mode) return;
    ReplaceLayer(index, WithAttribute(*l, [&](UCRasterLayer& c) { c.blendMode = mode; }), "Layer Blend Mode");
}

void UCRasterDocument::SetLayerName(int index, const std::string& name) {
    auto l = GetLayer(index);
    if (!l || l->name == name) return;
    ReplaceLayer(index, WithAttribute(*l, [&](UCRasterLayer& c) { c.name = name; }), "Rename Layer");
}

void UCRasterDocument::SetLayerLocked(int index, bool locked) {
    auto l = GetLayer(index);
    if (!l || l->locked == locked) return;
    ReplaceLayer(index, WithAttribute(*l, [&](UCRasterLayer& c) { c.locked = locked; }), locked ? "Lock Layer" : "Unlock Layer");
}

// ===========================================================================
// SELECTION
// ===========================================================================

void UCRasterDocument::CommitSelectionChange(const std::string& label) {
    // The selection was already changed in place; record before (the last
    // committed state, kept in `lastSelection`) and after.
    RasterUndoEntry e;
    e.label = label;
    e.structural = true;
    e.width = e.widthAfter = width;
    e.height = e.heightAfter = height;
    e.activeLayer = e.activeLayerAfter = activeLayer;
    e.layersBefore = e.layersAfter = layers;
    e.selectionBefore = lastCommittedSelection ? lastCommittedSelection
                                               : std::make_shared<UCRasterSelection>(width, height);
    e.selectionAfter = std::make_shared<UCRasterSelection>(selection);
    lastCommittedSelection = e.selectionAfter;
    PushUndo(std::move(e));
    EmitSelection();
    EmitState();
}

// ===========================================================================
// WHOLE-IMAGE GEOMETRY
// ===========================================================================

void UCRasterDocument::ScaleImage(int newWidth, int newHeight) {
    newWidth = std::max(1, newWidth); newHeight = std::max(1, newHeight);
    if (newWidth == width && newHeight == height) return;
    BeginStructuralUndo("Scale Image");
    std::vector<std::shared_ptr<UCRasterLayer>> next;
    for (const auto& l : layers) {
        auto copy = l->Clone();
        bool done = false;
#ifdef HAS_LIBVIPS
        try {
            PixelFX::PFXImage img = l->ToPixelFX();
            img = PixelFX::Colour::Premultiply(img);
            img = PixelFX::Resample::ResizeTo(img, newWidth, newHeight);
            img = PixelFX::Colour::Unpremultiply(img);
            img = PixelFX::Conversion::CastUchar(img);
            done = copy->FromPixelFX(img) && copy->GetWidth() == newWidth && copy->GetHeight() == newHeight;
        } catch (...) { done = false; }
#endif
        if (!done) copy->ResampleBilinear(newWidth, newHeight);
        next.push_back(copy);
    }
    layers.swap(next);
    width = newWidth; height = newHeight;
    selection.Resize(width, height);
    ResetComposite();
    EndStructuralUndo();
}

void UCRasterDocument::ResizeCanvas(int newWidth, int newHeight, int offsetX, int offsetY) {
    newWidth = std::max(1, newWidth); newHeight = std::max(1, newHeight);
    BeginStructuralUndo("Canvas Size");
    std::vector<std::shared_ptr<UCRasterLayer>> next;
    for (const auto& l : layers) {
        auto copy = l->Clone();
        copy->ResizeCanvas(newWidth, newHeight, offsetX, offsetY);
        next.push_back(copy);
    }
    layers.swap(next);
    width = newWidth; height = newHeight;
    selection.Resize(width, height);
    ResetComposite();
    EndStructuralUndo();
}

void UCRasterDocument::CropTo(const Rect2Di& rIn) {
    const Rect2Di r = Rect2Di(std::max(0, rIn.x), std::max(0, rIn.y),
                              std::min(width - std::max(0, rIn.x), rIn.width),
                              std::min(height - std::max(0, rIn.y), rIn.height));
    if (r.width <= 0 || r.height <= 0) return;
    BeginStructuralUndo("Crop");
    std::vector<std::shared_ptr<UCRasterLayer>> next;
    for (const auto& l : layers) next.push_back(l->CropCopy(r));
    layers.swap(next);
    width = r.width; height = r.height;
    selection.Resize(width, height);
    ResetComposite();
    EndStructuralUndo();
}

void UCRasterDocument::FlipHorizontal() {
    BeginStructuralUndo("Flip Horizontal");
    for (auto& l : layers) { auto c = l->Clone(); c->FlipHorizontal(); l = c; }
    InvalidateComposite();
    EndStructuralUndo();
}

void UCRasterDocument::FlipVertical() {
    BeginStructuralUndo("Flip Vertical");
    for (auto& l : layers) { auto c = l->Clone(); c->FlipVertical(); l = c; }
    InvalidateComposite();
    EndStructuralUndo();
}

void UCRasterDocument::Rotate90(bool clockwise) {
    BeginStructuralUndo(clockwise ? "Rotate 90° CW" : "Rotate 90° CCW");
    for (auto& l : layers) { auto c = l->Clone(); c->Rotate90(clockwise); l = c; }
    std::swap(width, height);
    selection.Resize(width, height);
    ResetComposite();
    EndStructuralUndo();
}

void UCRasterDocument::Rotate180() {
    BeginStructuralUndo("Rotate 180°");
    for (auto& l : layers) { auto c = l->Clone(); c->Rotate180(); l = c; }
    InvalidateComposite();
    EndStructuralUndo();
}

// ===========================================================================
// PIXEL EDITS
// ===========================================================================

std::vector<uint8_t> UCRasterDocument::SnapshotRect(const UCRasterLayer& layer, const Rect2Di& r) {
    std::vector<uint8_t> out(static_cast<size_t>(std::max(0, r.width)) * std::max(0, r.height) * 4);
    for (int y = 0; y < r.height; ++y)
        std::memcpy(out.data() + static_cast<size_t>(y) * r.width * 4,
                    layer.Row(r.y + y) + static_cast<size_t>(r.x) * 4,
                    static_cast<size_t>(r.width) * 4);
    return out;
}

void UCRasterDocument::RestoreRect(UCRasterLayer& layer, const Rect2Di& r, const std::vector<uint8_t>& data) {
    if (data.size() < static_cast<size_t>(r.width) * r.height * 4) return;
    for (int y = 0; y < r.height; ++y)
        std::memcpy(layer.Row(r.y + y) + static_cast<size_t>(r.x) * 4,
                    data.data() + static_cast<size_t>(y) * r.width * 4,
                    static_cast<size_t>(r.width) * 4);
}

void UCRasterDocument::BeginEdit(const std::string& label, int layerIndex, const Rect2Di& rect) {
    if (editing) EndEdit();
    auto layer = GetLayer(layerIndex);
    if (!layer) return;
    pendingEdit = RasterUndoEntry();
    pendingEdit.label = label;
    pendingEdit.layerIndex = layerIndex;
    pendingEdit.rect = layer->ClipRect(rect);
    if (pendingEdit.rect.width <= 0 || pendingEdit.rect.height <= 0) return;
    pendingEdit.before = SnapshotRect(*layer, pendingEdit.rect);
    // The pixels are about to stop being what the text draws.
    pendingEdit.textBefore = layer->text;
    layer->text.reset();
    editing = true;
}

void UCRasterDocument::EndEdit() {
    if (!editing) return;
    editing = false;
    auto layer = GetLayer(pendingEdit.layerIndex);
    if (!layer) return;
    pendingEdit.after = SnapshotRect(*layer, pendingEdit.rect);
    if (pendingEdit.after == pendingEdit.before) {   // nothing changed
        if (pendingEdit.textBefore) layer->text = pendingEdit.textBefore;
        return;
    }
    const Rect2Di r = pendingEdit.rect;
    const bool rasterized = pendingEdit.textBefore != nullptr;
    PushUndo(std::move(pendingEdit));
    SetModified(true);
    NotifyChanged(r);
    if (rasterized) EmitStructure();
    EmitState();
}

void UCRasterDocument::RecordEdit(const std::string& label, int layerIndex, const Rect2Di& rect,
                                  const UCRasterLayer& before) {
    auto layer = GetLayer(layerIndex);
    if (!layer || before.GetWidth() != layer->GetWidth() || before.GetHeight() != layer->GetHeight()) return;
    RasterUndoEntry e;
    e.label = label;
    e.layerIndex = layerIndex;
    e.rect = layer->ClipRect(rect);
    if (e.rect.width <= 0 || e.rect.height <= 0) return;
    e.before = SnapshotRect(before, e.rect);
    e.after = SnapshotRect(*layer, e.rect);
    if (e.before == e.after) return;
    e.textBefore = layer->text;
    layer->text.reset();
    const Rect2Di r = e.rect;
    const bool rasterized = e.textBefore != nullptr;
    PushUndo(std::move(e));
    SetModified(true);
    NotifyChanged(r);
    if (rasterized) EmitStructure();
    EmitState();
}

void UCRasterDocument::CancelEdit() {
    if (!editing) return;
    editing = false;
    auto layer = GetLayer(pendingEdit.layerIndex);
    if (layer) {
        RestoreRect(*layer, pendingEdit.rect, pendingEdit.before);
        if (pendingEdit.textBefore) layer->text = pendingEdit.textBefore;
        NotifyChanged(pendingEdit.rect);
    }
}

void UCRasterDocument::NotifyChanged(const Rect2Di& rect) {
    InvalidateComposite(rect);
    EmitPixels(rect);
}

void UCRasterDocument::ApplyPixels(const RasterUndoEntry& e, bool toAfter) {
    auto layer = GetLayer(e.layerIndex);
    if (!layer) return;
    RestoreRect(*layer, e.rect, toAfter ? e.after : e.before);
    NotifyChanged(e.rect);
    if (e.textBefore) {
        layer->text = toAfter ? nullptr : e.textBefore;
        EmitStructure();
    }
}

void UCRasterDocument::ApplyProcessedLayer(const std::string& label, int layerIndex, const UCRasterLayer& processed) {
    auto layer = GetLayer(layerIndex);
    if (!layer || processed.GetWidth() != width || processed.GetHeight() != height) return;
    const Rect2Di area = selection.IsActive() ? selection.GetBounds() : GetRect();
    if (area.width <= 0) return;
    BeginEdit(label, layerIndex, area);
    for (int y = area.y; y < area.y + area.height; ++y) {
        const uint8_t* s = processed.Row(y);
        uint8_t* d = layer->Row(y);
        for (int x = area.x; x < area.x + area.width; ++x) {
            const int cov = selection.Coverage(x, y);
            if (cov == 0) continue;
            const uint8_t* sp = s + static_cast<size_t>(x) * 4;
            uint8_t* dp = d + static_cast<size_t>(x) * 4;
            if (cov == 255) { dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3]; continue; }
            for (int c = 0; c < 4; ++c) dp[c] = static_cast<uint8_t>(dp[c] + ((sp[c] - dp[c]) * cov + 127) / 255);
        }
    }
    EndEdit();
}

#ifdef HAS_LIBVIPS
std::shared_ptr<UCRasterLayer> UCRasterDocument::PreviewFilter(const PixelFXOp& op) const {
    auto layer = GetActiveLayer();
    if (!layer || !op) return nullptr;
    try {
        PixelFX::PFXImage out = op(layer->ToPixelFX());
        auto result = std::make_shared<UCRasterLayer>();
        if (!result->FromPixelFX(out)) return nullptr;
        if (result->GetWidth() != width || result->GetHeight() != height) return nullptr;
        result->name = layer->name; result->visible = layer->visible; result->opacity = layer->opacity;
        result->blendMode = layer->blendMode; result->locked = layer->locked;
        return result;
    } catch (...) {
        return nullptr;
    }
}

bool UCRasterDocument::ApplyFilter(const std::string& label, const PixelFXOp& op) {
    return ApplyFilterToLayer(label, activeLayer, op);
}

bool UCRasterDocument::ApplyFilterToLayer(const std::string& label, int layerIndex, const PixelFXOp& op) {
    auto layer = GetLayer(layerIndex);
    if (!layer || !op) return false;
    try {
        PixelFX::PFXImage out = op(layer->ToPixelFX());
        UCRasterLayer result;
        if (!result.FromPixelFX(out)) return false;
        if (result.GetWidth() != width || result.GetHeight() != height) return false;
        ApplyProcessedLayer(label, layerIndex, result);
        return true;
    } catch (...) {
        return false;
    }
}
#endif

// ===========================================================================
// CLIPBOARD HELPERS
// ===========================================================================

namespace {
    std::shared_ptr<UCRasterLayer> ExtractSelected(const UCRasterLayer& src, const UCRasterSelection& sel,
                                                   Point2Di& origin) {
        const Rect2Di b = sel.IsActive() ? sel.GetBounds() : src.GetRect();
        origin = Point2Di(b.x, b.y);
        auto out = src.CropCopy(b);
        out->text.reset();   // a piece of the picture, not the words
        if (sel.IsActive()) {
            for (int y = 0; y < out->GetHeight(); ++y) {
                uint8_t* row = out->Row(y);
                for (int x = 0; x < out->GetWidth(); ++x) {
                    const int cov = sel.Coverage(b.x + x, b.y + y);
                    row[static_cast<size_t>(x) * 4 + 3] = static_cast<uint8_t>((row[static_cast<size_t>(x) * 4 + 3] * cov + 127) / 255);
                }
            }
        }
        return out;
    }
}

std::shared_ptr<UCRasterLayer> UCRasterDocument::CopySelection(Point2Di& origin) const {
    auto layer = GetActiveLayer();
    if (!layer) return nullptr;
    return ExtractSelected(*layer, selection, origin);
}

std::shared_ptr<UCRasterLayer> UCRasterDocument::CopySelectionMerged(Point2Di& origin) const {
    auto flat = Flatten();
    if (!flat) return nullptr;
    return ExtractSelected(*flat, selection, origin);
}

void UCRasterDocument::DeleteSelection() {
    auto layer = GetActiveLayer();
    if (!layer || layer->locked) return;
    const Rect2Di area = selection.IsActive() ? selection.GetBounds() : GetRect();
    BeginEdit("Delete", activeLayer, area);
    for (int y = area.y; y < area.y + area.height; ++y) {
        uint8_t* row = layer->Row(y);
        for (int x = area.x; x < area.x + area.width; ++x) {
            const int cov = selection.Coverage(x, y);
            if (!cov) continue;
            uint8_t* p = row + static_cast<size_t>(x) * 4;
            p[3] = static_cast<uint8_t>((p[3] * (255 - cov) + 127) / 255);
            if (p[3] == 0) p[0] = p[1] = p[2] = 0;
        }
    }
    EndEdit();
}

void UCRasterDocument::FillSelection(const RasterPixel& colour) {
    auto layer = GetActiveLayer();
    if (!layer || layer->locked) return;
    const Rect2Di area = selection.IsActive() ? selection.GetBounds() : GetRect();
    BeginEdit("Fill", activeLayer, area);
    for (int y = area.y; y < area.y + area.height; ++y) {
        uint8_t* row = layer->Row(y);
        for (int x = area.x; x < area.x + area.width; ++x) {
            const int cov = selection.Coverage(x, y);
            if (!cov) continue;
            uint8_t* p = row + static_cast<size_t>(x) * 4;
            const RasterPixel out = RasterBlendPixel(colour, RasterPixel(p[0], p[1], p[2], p[3]), cov / 255.0f);
            p[0] = out.r; p[1] = out.g; p[2] = out.b; p[3] = out.a;
        }
    }
    EndEdit();
}

// ===========================================================================
// UNDO / REDO
// ===========================================================================

void UCRasterDocument::PushUndo(RasterUndoEntry&& e) {
    undoMemoryUsed += e.Bytes();
    undoStack.push_back(std::move(e));
    for (auto& r : redoStack) undoMemoryUsed -= std::min(undoMemoryUsed, r.Bytes());
    redoStack.clear();
    TrimHistory();
}

void UCRasterDocument::TrimHistory() {
    while (undoStack.size() > 1 && undoMemoryUsed > undoMemoryLimit) {
        undoMemoryUsed -= std::min(undoMemoryUsed, undoStack.front().Bytes());
        undoStack.pop_front();
    }
}

void UCRasterDocument::Undo() {
    if (editing) EndEdit();
    if (structuralOpen) EndStructuralUndo();
    if (undoStack.empty()) return;
    RasterUndoEntry e = std::move(undoStack.back());
    undoStack.pop_back();
    if (e.structural) ApplyStructural(e, false); else ApplyPixels(e, false);
    // What was actually restored, which is not the recorded selection when
    // that one belonged to a canvas of another size.
    lastCommittedSelection = std::make_shared<UCRasterSelection>(selection);
    redoStack.push_back(std::move(e));
    SetModified(true);
    EmitState();
}

void UCRasterDocument::Redo() {
    if (redoStack.empty()) return;
    RasterUndoEntry e = std::move(redoStack.back());
    redoStack.pop_back();
    if (e.structural) ApplyStructural(e, true); else ApplyPixels(e, true);
    lastCommittedSelection = std::make_shared<UCRasterSelection>(selection);
    undoStack.push_back(std::move(e));
    SetModified(true);
    EmitState();
}

void UCRasterDocument::ClearHistory() {
    undoStack.clear();
    redoStack.clear();
    undoMemoryUsed = 0;
    lastCommittedSelection = std::make_shared<UCRasterSelection>(selection);
    EmitState();
}

// ===========================================================================
// COMPOSITE
// ===========================================================================

void UCRasterDocument::ResetComposite() {
    composite.reset();
    compositeDirty.clear();
    compositeAllDirty = true;
}

void UCRasterDocument::InvalidateComposite(const Rect2Di& rect) {
    if (compositeAllDirty) return;
    const Rect2Di r = Rect2Di(std::max(0, rect.x), std::max(0, rect.y),
                              std::min(width - std::max(0, rect.x), rect.width),
                              std::min(height - std::max(0, rect.y), rect.height));
    if (r.width <= 0 || r.height <= 0) return;
    if (r.width * static_cast<long>(r.height) * 2 >= static_cast<long>(width) * height || compositeDirty.size() > 64) {
        compositeAllDirty = true;
        compositeDirty.clear();
        return;
    }
    compositeDirty.push_back(r);
}

std::shared_ptr<UCPixmap> UCRasterDocument::GetCompositePixmap() {
    if (width <= 0 || height <= 0) return nullptr;
    if (!composite || composite->GetWidth() != width || composite->GetHeight() != height) {
        composite = std::make_shared<UCPixmap>(width, height);
        compositeAllDirty = true;
    }
    if (!composite->IsValid()) return nullptr;
    std::vector<Rect2Di> rects;
    if (compositeAllDirty) rects.push_back(GetRect());
    else rects = compositeDirty;
    compositeDirty.clear();
    compositeAllDirty = false;
    if (rects.empty()) return composite;
    uint32_t* px = composite->GetPixelData();
    const int stride = composite->GetRawWidth();
    const auto inPlace = inPlaceEditLayer.lock();
    for (const Rect2Di& r : rects) {
        for (int y = r.y; y < r.y + r.height; ++y)
            std::memset(px + static_cast<size_t>(y) * stride + r.x, 0, static_cast<size_t>(r.width) * 4);
        for (const auto& l : layers) {
            if (!l || !l->visible || l->opacity <= 0.0f || l == inPlace) continue;
            l->CompositeOnto(px, width, height, stride, r, l->opacity, l->blendMode);
        }
    }
    composite->MarkDirty();
    return composite;
}

std::shared_ptr<UCRasterLayer> UCRasterDocument::Flatten() const {
    auto out = std::make_shared<UCRasterLayer>(width, height, "Flattened");
    for (const auto& l : layers) {
        if (!l || !l->visible || l->opacity <= 0.0f) continue;
        out->BlendFrom(*l, 0, 0, l->opacity, l->blendMode);
    }
    return out;
}

// ===========================================================================
// FILES
// ===========================================================================

bool UCRasterDocument::IsProjectFile(const std::string& path) {
    const std::string ext = LowerExt(path);
    return ext == "ucraster" || ext == "ucd";
}

bool UCRasterDocument::IsUCDFile(const std::string& path) {
    return LowerExt(path) == "ucd";
}

bool UCRasterDocument::LoadFromFile(const std::string& path, std::string& error) {
    if (IsUCDFile(path)) return LoadUCD(path, error);
    if (IsProjectFile(path)) return LoadProject(path, error);
#ifdef HAS_LIBVIPS
    try {
        auto layer = std::make_shared<UCRasterLayer>();
        bool decoded = false;
        {
            PixelFX::PFXImage img = PixelFX::FileIO::Load(path);
            try { img = PixelFX::Resample::Autorot(img); } catch (...) {}
            decoded = layer->FromPixelFX(img);
        }
        // Every pixel is now the document's own, so the file has nothing left
        // to give - and an open document must not keep its source file held.
        // libvips caches the finished loader, which for a JPEG keeps the file
        // memory-mapped; on Windows a mapping makes the file impossible to
        // truncate, so saving back over it fails with "unable to open for
        // write / system error: Invalid argument" (ERROR_USER_MAPPED_FILE,
        // which the C runtime reports as EINVAL).
        PixelFX::ReleaseCachedFiles();
        if (!decoded) { error = "Could not decode " + path; return false; }
        layer->name = "Background";
        width = layer->GetWidth(); height = layer->GetHeight();
        layers.clear();
        layers.push_back(layer);
        activeLayer = 0;
        selection.Resize(width, height);
        ResetComposite();
        filePath = path;
        modified = false;
        ClearHistory();
        EmitStructure();
        EmitSelection();
        EmitState();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
#else
    (void)path;
    error = "Image loading needs libvips (HAS_LIBVIPS)";
    return false;
#endif
}

bool UCRasterDocument::SaveToFile(const std::string& path, std::string& error,
                                  const UCImageSave::ImageExportOptions* options) {
    if (IsUCDFile(path)) return SaveUCD(path, error);
    if (IsProjectFile(path)) return SaveProject(path, error);
    // The extension picks the format, so a name without one (saved under
    // "All files") has none to pick; libvips would only say it is "not a
    // known file format".
    if (LowerExt(path).empty()) {
        error = "The file name has no extension, so there is no format to save it in. "
                "Add one, such as .png, or choose the file type.";
        return false;
    }
#ifdef HAS_LIBVIPS
    try {
        auto flat = Flatten();
        PixelFX::PFXImage img = flat->ToPixelFX();
        const std::string ext = LowerExt(path);
        if (ext == "jpg" || ext == "jpeg" || ext == "bmp" || ext == "ppm" || ext == "pgm") {
            img = PixelFX::Colour::Flatten(img);
        }

        // Nothing in this process may still be holding the destination: an
        // image opened earlier - this document's own file, most of the time -
        // is still held by the libvips operation cache, and on Windows that
        // is what makes saving over it fail. See LoadFromFile().
        PixelFX::ReleaseCachedFiles();

        // Both paths stage the write: ExportVImage and PixelFX::FileIO::Save
        // encode into a temporary file and move it over the caller's only once
        // it is complete. A failure - no space left, a codec error, a
        // destination another program holds - therefore leaves the image the
        // user had, not the remains of the write.
        // No options from the caller: the user's save settings for the
        // format, once they have chosen any (the Save dialog's Settings
        // button). Until then the writer keeps its own defaults.
        UCImageSave::ImageExportOptions stored;
        if (!options && UltraCanvasFileLoader::HasStoredSaveSettings(path) &&
            UltraCanvasFileLoader::GetImageExportOptions(path, stored)) {
            options = &stored;
        }
        if (options) {
            const std::string err = ExportVImage(img, path, *options);
            if (!err.empty()) { error = err; return false; }
        } else {
            // FileIO::Save throws with the reason; the catch below reports it.
            PixelFX::FileIO::Save(img, path);
        }
        filePath = path;
        SetModified(false);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
#else
    (void)path; (void)options;
    error = "Image saving needs libvips (HAS_LIBVIPS)";
    return false;
#endif
}

bool UCRasterDocument::SaveProject(const std::string& path, std::string& error) {
#ifdef HAS_LIBVIPS
    UCZipPackageWriter zip;
    if (!zip.Open(path)) { error = "Could not create " + path; return false; }
    JSONValue doc = JSONValue::MakeObject();
    doc.Set("format", "ucraster");
    doc.Set("version", 1);
    doc.Set("width", width);
    doc.Set("height", height);
    doc.Set("activeLayer", activeLayer);
    JSONValue arr = JSONValue::MakeArray();
    for (size_t i = 0; i < layers.size(); ++i) {
        const auto& l = layers[i];
        JSONValue jl = JSONValue::MakeObject();
        jl.Set("name", l->name);
        jl.Set("visible", l->visible);
        jl.Set("locked", l->locked);
        jl.Set("opacity", static_cast<double>(l->opacity));
        jl.Set("blend", static_cast<int>(l->blendMode));
        jl.Set("file", "layers/" + std::to_string(i) + ".png");
        if (l->text && l->text->document) {
            // The words stay words: the box and the default font here, the
            // rich text beside the PNG as an OpenDocument text.
            const RasterTextContent& t = *l->text;
            const std::string textFile = "layers/" + std::to_string(i) + ".odt";
            JSONValue jt = JSONValue::MakeObject();
            jt.Set("x", t.x);
            jt.Set("y", t.y);
            jt.Set("width", t.width);
            jt.Set("height", t.height);
            jt.Set("font", t.fontFamily);
            jt.Set("sizePx", static_cast<double>(t.fontSizePx));
            JSONValue colour = JSONValue::MakeArray();
            for (int c : { t.colour.r, t.colour.g, t.colour.b, t.colour.a }) colour.Append(c);
            jt.Set("colour", colour);
            jt.Set("file", textFile);
            jl.Set("text", jt);
            std::vector<uint8_t> odt;
            std::string odtError;
            if (!RichDocumentToOdt(*t.document, odt, odtError) ||
                !zip.AddEntry(textFile, odt.data(), odt.size(), false)) {
                error = "Could not write the text of layer " + l->name +
                        (odtError.empty() ? std::string() : ": " + odtError);
                return false;
            }
        }
        arr.Append(jl);
        try {
            std::vector<uint8_t> png = PixelFX::FileIO::SaveToBuffer(l->ToPixelFX(), ".png");
            if (png.empty() || !zip.AddEntry("layers/" + std::to_string(i) + ".png", png.data(), png.size(), false)) {
                error = "Could not encode layer " + l->name;
                return false;
            }
        } catch (const std::exception& e) {
            error = e.what();
            return false;
        }
    }
    doc.Set("layers", arr);
    if (!zip.AddEntry("document.json", JSON::Serialize(doc)) || !zip.Finalize()) {
        error = "Could not write " + path;
        return false;
    }
    filePath = path;
    SetModified(false);
    return true;
#else
    (void)path;
    error = "Project saving needs libvips (HAS_LIBVIPS)";
    return false;
#endif
}

bool UCRasterDocument::LoadProject(const std::string& path, std::string& error) {
#ifdef HAS_LIBVIPS
    UCZipPackageReader zip;
    if (!zip.Open(path)) { error = "Could not open " + path; return false; }
    std::string text;
    if (!zip.ReadEntry("document.json", text)) { error = "Not an UltraPaint project: " + path; return false; }
    JSONParseResult pr;
    JSONValue doc = JSON::Parse(text, &pr);
    if (!pr.success) { error = "Corrupt project: " + pr.errorMessage; return false; }
    const int w = static_cast<int>(doc.Get("width").GetInteger());
    const int h = static_cast<int>(doc.Get("height").GetInteger());
    if (w <= 0 || h <= 0) { error = "Corrupt project: bad canvas size"; return false; }
    std::vector<std::shared_ptr<UCRasterLayer>> loaded;
    const JSONValue& arr = doc.Get("layers");
    for (size_t i = 0; i < arr.GetSize(); ++i) {
        const JSONValue& jl = arr.At(i);
        std::vector<uint8_t> png;
        if (!zip.ReadEntry(jl.Get("file").GetString(), png)) { error = "Corrupt project: missing layer data"; return false; }
        auto layer = std::make_shared<UCRasterLayer>();
        try {
            if (!layer->FromPixelFX(PixelFX::FileIO::LoadFromMemory(png, ""))) {
                error = "Corrupt project: undecodable layer"; return false;
            }
        } catch (const std::exception& e) { error = e.what(); return false; }
        if (layer->GetWidth() != w || layer->GetHeight() != h) layer->ResizeCanvas(w, h, 0, 0);
        layer->name = jl.Get("name").GetString("Layer");
        layer->visible = jl.Get("visible").GetBoolean(true);
        layer->locked = jl.Get("locked").GetBoolean(false);
        layer->opacity = static_cast<float>(jl.Get("opacity").GetNumber(1.0));
        layer->blendMode = static_cast<RasterBlendMode>(std::clamp<int64_t>(jl.Get("blend").GetInteger(0), 0,
                                                        static_cast<int64_t>(RasterBlendMode::HardLight)));
        // A text layer: its pixels are the PNG, its words come back from the
        // OpenDocument text. A project whose text cannot be read still opens,
        // the layer as the bitmap it shows.
        const JSONValue& jt = jl.Get("text");
        std::vector<uint8_t> odt;
        auto words = std::make_shared<UCRichDocument>();
        std::string odtError;
        if (jt.IsObject() && zip.ReadEntry(jt.Get("file").GetString(), odt) &&
            RichDocumentFromOdt(odt, *words, odtError)) {
            auto content = std::make_shared<RasterTextContent>();
            content->document = words;
            content->x = jt.Get("x").GetNumber(0.0);
            content->y = jt.Get("y").GetNumber(0.0);
            content->width = std::max(1.0, jt.Get("width").GetNumber(static_cast<double>(w)));
            content->height = std::max(0.0, jt.Get("height").GetNumber(0.0));
            content->fontFamily = jt.Get("font").GetString("Sans");
            content->fontSizePx = static_cast<float>(std::max(1.0, jt.Get("sizePx").GetNumber(32.0)));
            const JSONValue& colour = jt.Get("colour");
            if (colour.IsArray() && colour.GetSize() == 4) {
                auto channel = [&](size_t c) {
                    return static_cast<uint8_t>(std::clamp<int64_t>(colour.At(c).GetInteger(0), 0, 255));
                };
                content->colour = RasterPixel(channel(0), channel(1), channel(2), channel(3));
            }
            layer->text = content;
        }
        loaded.push_back(layer);
    }
    if (loaded.empty()) { error = "Project has no layers"; return false; }
    width = w; height = h;
    layers.swap(loaded);
    activeLayer = std::clamp(static_cast<int>(doc.Get("activeLayer").GetInteger(0)), 0, static_cast<int>(layers.size()) - 1);
    selection.Resize(width, height);
    ResetComposite();
    filePath = path;
    modified = false;
    ClearHistory();
    EmitStructure();
    EmitSelection();
    EmitState();
    return true;
#else
    (void)path;
    error = "Project loading needs libvips (HAS_LIBVIPS)";
    return false;
#endif
}

// ===========================================================================
// LAYERED UCD (*.ucd: UCD 2.1, descriptor UCBitmap - spec section 4.7)
// ===========================================================================

namespace {
    constexpr uint32_t kNoSection = 0xFFFFFFFFu;
    constexpr int kThumbnailEdge = 256;
    // UCRW stores its size as uint16.
    constexpr int kMaxUCDSide = 65535;

    // The composite at most kThumbnailEdge px on its longer side, as a PNG:
    // what a filer shows without decoding a layer. Each thumbnail pixel is
    // the average of the pixels it covers, weighted by their alpha, so a
    // transparent edge does not darken the colours beside it.
    std::vector<uint8_t> ThumbnailPng(const UCRasterLayer& flat) {
        const int w = flat.GetWidth(), h = flat.GetHeight();
        if (w <= 0 || h <= 0) return {};
        const double scale = std::min(1.0, static_cast<double>(kThumbnailEdge) / std::max(w, h));
        const int tw = std::max(1, static_cast<int>(std::lround(w * scale)));
        const int th = std::max(1, static_cast<int>(std::lround(h * scale)));
        std::vector<uint8_t> rgba(static_cast<size_t>(tw) * th * 4);
        for (int ty = 0; ty < th; ++ty) {
            const int y0 = static_cast<int>(static_cast<int64_t>(ty) * h / th);
            const int y1 = std::max(y0 + 1, static_cast<int>(static_cast<int64_t>(ty + 1) * h / th));
            for (int tx = 0; tx < tw; ++tx) {
                const int x0 = static_cast<int>(static_cast<int64_t>(tx) * w / tw);
                const int x1 = std::max(x0 + 1, static_cast<int>(static_cast<int64_t>(tx + 1) * w / tw));
                uint64_t r = 0, g = 0, b = 0, a = 0, count = 0;
                for (int y = y0; y < y1; ++y) {
                    const uint8_t* row = flat.Row(y);
                    for (int x = x0; x < x1; ++x) {
                        const uint8_t* p = row + static_cast<size_t>(x) * 4;
                        r += static_cast<uint64_t>(p[0]) * p[3];
                        g += static_cast<uint64_t>(p[1]) * p[3];
                        b += static_cast<uint64_t>(p[2]) * p[3];
                        a += p[3];
                        ++count;
                    }
                }
                uint8_t* d = rgba.data() + (static_cast<size_t>(ty) * tw + tx) * 4;
                if (a > 0) {
                    d[0] = static_cast<uint8_t>((r + a / 2) / a);
                    d[1] = static_cast<uint8_t>((g + a / 2) / a);
                    d[2] = static_cast<uint8_t>((b + a / 2) / a);
                }
                d[3] = static_cast<uint8_t>((a + count / 2) / count);
            }
        }
        size_t length = 0;
        void* png = tdefl_write_image_to_png_file_in_memory_ex(rgba.data(), tw, th, 4, &length, MZ_DEFAULT_LEVEL, MZ_FALSE);
        if (!png) return {};
        std::vector<uint8_t> out(static_cast<uint8_t*>(png), static_cast<uint8_t*>(png) + length);
        mz_free(png);
        return out;
    }

    // A layer's pixels as a BMAP payload: 'UCRW', RGBA8, straight alpha.
    std::vector<uint8_t> BitmapPayload(const UCRasterLayer& layer) {
        std::vector<uint8_t> out;
        out.reserve(UCD::kPreambleSize + 8 + layer.DataSize());
        UCD::AppendPreamble(out, "UCRW", 1);
        UCD::ByteWriter w(out);
        w.U16(static_cast<uint16_t>(layer.GetWidth()));
        w.U16(static_cast<uint16_t>(layer.GetHeight()));
        w.U8(0);                       // RGBA8
        w.Zeros(3);
        w.Bytes(layer.Data(), layer.DataSize());
        return out;
    }

    // A BMAP payload as a layer of the bitmap's own size: UCRW in any of its
    // pixel formats, or an embedded image file through PixelFX.
    std::shared_ptr<UCRasterLayer> BitmapFromPayload(const std::vector<uint8_t>& payload, std::string& error) {
        std::string fourcc;
        uint8_t version = 0;
        if (!UCD::ReadPreamble(payload, fourcc, version)) { error = "a bitmap section is empty"; return nullptr; }
        if (fourcc == "UCRW") {
            UCD::ByteReader r(payload.data() + UCD::kPreambleSize, payload.size() - UCD::kPreambleSize);
            uint16_t w = 0, h = 0;
            uint8_t format = 0;
            if (!r.U16(w) || !r.U16(h) || !r.U8(format) || !r.Skip(3) || w == 0 || h == 0) {
                error = "a bitmap section is damaged";
                return nullptr;
            }
            static const size_t kBytesPerPixel[] = { 4, 3, 1, 8 };
            if (format > 3) { error = "a bitmap uses a pixel format this program does not read"; return nullptr; }
            const size_t pixels = static_cast<size_t>(w) * h;
            if (r.Remaining() != pixels * kBytesPerPixel[format]) { error = "a bitmap section is damaged"; return nullptr; }
            auto layer = std::make_shared<UCRasterLayer>(w, h);
            const uint8_t* src = r.Here();
            uint8_t* dst = layer->Data();
            for (size_t i = 0; i < pixels; ++i, dst += 4) {
                switch (format) {
                    case 0: std::memcpy(dst, src + i * 4, 4); break;
                    case 1: std::memcpy(dst, src + i * 3, 3); dst[3] = 255; break;
                    case 2: dst[0] = dst[1] = dst[2] = src[i]; dst[3] = 255; break;
                    case 3:   // RGBA16, little-endian: the high byte of each sample
                        for (int c = 0; c < 4; ++c) dst[c] = src[i * 8 + static_cast<size_t>(c) * 2 + 1];
                        break;
                }
            }
            return layer;
        }
#ifdef HAS_LIBVIPS
        // An embedded image file ('PNG ', 'JPEG', 'WEBP' ...).
        try {
            std::vector<uint8_t> file(payload.begin() + UCD::kPreambleSize, payload.end());
            auto layer = std::make_shared<UCRasterLayer>();
            if (layer->FromPixelFX(PixelFX::FileIO::LoadFromMemory(file, ""))) return layer;
        } catch (const std::exception&) {}
        error = "a bitmap (" + fourcc + ") could not be decoded";
#else
        error = "a bitmap (" + fourcc + ") needs an image decoder this build does not have";
#endif
        return nullptr;
    }

    // A text layer's words as a VECT payload: 'UCTF', the box, the defaults
    // and the rich text as an OpenDocument text.
    std::vector<uint8_t> TextFramePayload(const RasterTextContent& t, const std::vector<uint8_t>& odt) {
        std::vector<uint8_t> out;
        UCD::AppendPreamble(out, "UCTF", 1);
        UCD::ByteWriter w(out);
        w.F64(t.x);
        w.F64(t.y);
        w.F64(t.width);
        w.F64(t.height);
        w.U8(t.colour.r); w.U8(t.colour.g); w.U8(t.colour.b); w.U8(t.colour.a);
        w.F32(t.fontSizePx);
        w.String16(t.fontFamily);
        w.FourCC("ODT ");
        w.U32(static_cast<uint32_t>(odt.size()));
        w.Bytes(odt.data(), odt.size());
        return out;
    }

    // Null when the frame is not one this reads - the layer then stays the
    // bitmap it shows (spec 4.7.3).
    std::shared_ptr<RasterTextContent> TextFrameFromPayload(const std::vector<uint8_t>& payload) {
        std::string fourcc;
        uint8_t version = 0;
        if (!UCD::ReadPreamble(payload, fourcc, version) || fourcc != "UCTF" || version != 1) return nullptr;
        UCD::ByteReader r(payload.data() + UCD::kPreambleSize, payload.size() - UCD::kPreambleSize);
        auto content = std::make_shared<RasterTextContent>();
        uint8_t rgba[4] = {};
        std::string font, encoding;
        uint32_t length = 0;
        if (!r.F64(content->x) || !r.F64(content->y) || !r.F64(content->width) || !r.F64(content->height) ||
            !r.Bytes(rgba, 4) || !r.F32(content->fontSizePx) || !r.String16(font) ||
            !r.FourCC(encoding) || !r.U32(length) || length > r.Remaining() || encoding != "ODT ") {
            return nullptr;
        }
        const auto finite = [](double v) { return std::isfinite(v); };
        if (!finite(content->x) || !finite(content->y) || !finite(content->width) || !finite(content->height) ||
            !std::isfinite(content->fontSizePx)) {
            return nullptr;
        }
        std::vector<uint8_t> odt(r.Here(), r.Here() + length);
        auto words = std::make_shared<UCRichDocument>();
        std::string odtError;
        if (!RichDocumentFromOdt(odt, *words, odtError)) return nullptr;
        content->document = words;
        content->width = std::max(1.0, content->width);
        content->height = std::max(0.0, content->height);
        content->fontFamily = font.empty() ? std::string("Sans") : font;
        content->fontSizePx = std::max(1.0f, content->fontSizePx);
        content->colour = RasterPixel(rgba[0], rgba[1], rgba[2], rgba[3]);
        return content;
    }
}

bool UCRasterDocument::SaveUCD(const std::string& path, std::string& error) {
    if (width > kMaxUCDSide || height > kMaxUCDSide) {
        error = "An UltraCanvas document holds images up to 65535 pixels on a side";
        return false;
    }
    // Every text layer's words first: a text that cannot be written fails
    // the save before anything is.
    std::vector<std::vector<uint8_t>> frames(layers.size());
    for (size_t i = 0; i < layers.size(); ++i) {
        const auto& l = layers[i];
        if (!l->text || !l->text->document) continue;
        std::vector<uint8_t> odt;
        std::string odtError;
        if (!RichDocumentToOdt(*l->text->document, odt, odtError)) {
            error = "Could not write the text of layer " + l->name +
                    (odtError.empty() ? std::string() : ": " + odtError);
            return false;
        }
        frames[i] = TextFramePayload(*l->text, odt);
    }

    // The stack: every layer bottom first, naming its BMAP (one per layer,
    // in order) and its text frame (numbered among the text layers).
    std::vector<uint8_t> stack;
    UCD::ByteWriter s(stack);
    s.U8(1);
    s.Zeros(3);
    s.U32(static_cast<uint32_t>(width));
    s.U32(static_cast<uint32_t>(height));
    s.I32(activeLayer);
    s.U32(static_cast<uint32_t>(layers.size()));
    uint32_t nextFrame = 0;
    for (size_t i = 0; i < layers.size(); ++i) {
        const auto& l = layers[i];
        std::vector<uint8_t> record;
        UCD::ByteWriter r(record);
        r.U8(static_cast<uint8_t>((l->visible ? 0x01 : 0) | (l->locked ? 0x02 : 0)));
        r.U8(static_cast<uint8_t>(l->blendMode));
        r.U16(0);
        r.F32(l->opacity);
        r.I32(0);
        r.I32(0);
        r.U32(static_cast<uint32_t>(i));
        r.U32(frames[i].empty() ? kNoSection : nextFrame++);
        r.String16(l->name);
        s.U32(static_cast<uint32_t>(record.size()));
        s.Bytes(record.data(), record.size());
    }

    UCD::Writer writer("UCBitmap");
    if (auto flat = Flatten()) writer.SetThumbnailPng(ThumbnailPng(*flat));
    writer.AddSection("LAYR", std::move(stack));
    for (size_t i = 0; i < layers.size(); ++i) {
        writer.AddSection("BMAP", BitmapPayload(*layers[i]));
        // The OpenDocument text inside is a ZIP already.
        if (!frames[i].empty()) writer.AddSection("VECT", std::move(frames[i]), UCD::Compression::Uncompressed);
    }
    if (!writer.WriteToFile(path, error)) return false;
    filePath = path;
    SetModified(false);
    return true;
}

bool UCRasterDocument::LoadUCD(const std::string& path, std::string& error) {
    UCD::Reader reader;
    if (!reader.Open(path, error)) return false;
    if (reader.GetHeader().IsEncrypted()) {
        error = "This UltraCanvas document is password-protected, which this program cannot open yet";
        return false;
    }
    const auto bitmaps = reader.FindSections("BMAP");
    const auto frames = reader.FindSections("VECT");
    const auto stacks = reader.FindSections("LAYR");
    if (bitmaps.empty()) {
        error = "This UltraCanvas document (" + reader.GetHeader().descriptor + ") holds no image";
        return false;
    }
    auto decodeBitmap = [&](uint32_t index) -> std::shared_ptr<UCRasterLayer> {
        if (index >= bitmaps.size()) { error = "The document is damaged: a layer names a missing bitmap"; return nullptr; }
        std::vector<uint8_t> payload;
        if (!reader.Decode(*bitmaps[index], payload, error)) return nullptr;
        std::string why;
        auto layer = BitmapFromPayload(payload, why);
        if (!layer) error = "The document is damaged: " + why;
        return layer;
    };

    int w = 0, h = 0, active = 0;
    std::vector<std::shared_ptr<UCRasterLayer>> loaded;
    if (stacks.empty()) {
        // A single image (a UCBitmap without a layer stack): its first BMAP.
        auto layer = decodeBitmap(0);
        if (!layer) return false;
        layer->name = "Background";
        w = layer->GetWidth();
        h = layer->GetHeight();
        loaded.push_back(layer);
    } else {
        std::vector<uint8_t> stack;
        if (!reader.Decode(*stacks.front(), stack, error)) return false;
        UCD::ByteReader r(stack);
        uint8_t version = 0;
        uint32_t cw = 0, ch = 0, count = 0;
        int32_t activeIndex = 0;
        if (!r.U8(version) || !r.Skip(3) || !r.U32(cw) || !r.U32(ch) || !r.I32(activeIndex) || !r.U32(count) ||
            version < 1 || cw == 0 || ch == 0 || cw > static_cast<uint32_t>(kMaxUCDSide) ||
            ch > static_cast<uint32_t>(kMaxUCDSide) || count == 0) {
            error = "The document's layer stack is damaged or of a later kind";
            return false;
        }
        w = static_cast<int>(cw);
        h = static_cast<int>(ch);
        active = activeIndex;
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t length = 0;
            if (!r.U32(length) || length > r.Remaining()) {
                error = "The document's layer stack is damaged";
                return false;
            }
            UCD::ByteReader rec(r.Here(), length);
            r.Skip(length);
            uint8_t flags = 0, blend = 0;
            uint16_t reserved = 0;
            float opacity = 1.0f;
            int32_t x = 0, y = 0;
            uint32_t bitmap = kNoSection, frame = kNoSection;
            std::string name;
            if (!rec.U8(flags) || !rec.U8(blend) || !rec.U16(reserved) || !rec.F32(opacity) || !rec.I32(x) ||
                !rec.I32(y) || !rec.U32(bitmap) || !rec.U32(frame) || !rec.String16(name)) {
                error = "The document's layer stack is damaged";
                return false;
            }
            auto layer = std::make_shared<UCRasterLayer>(w, h, name);
            if (bitmap != kNoSection) {
                auto pixels = decodeBitmap(bitmap);
                if (!pixels) return false;
                layer->CopyFrom(*pixels, x, y);
            }
            layer->visible = (flags & 0x01) != 0;
            layer->locked = (flags & 0x02) != 0;
            layer->opacity = std::isfinite(opacity) ? std::clamp(opacity, 0.0f, 1.0f) : 1.0f;
            // A blend mode this does not know composites as Normal (spec 4.7.2).
            layer->blendMode = blend <= static_cast<uint8_t>(RasterBlendMode::HardLight)
                                   ? static_cast<RasterBlendMode>(blend) : RasterBlendMode::Normal;
            // The words, when this can read them; else the layer stays the
            // bitmap it shows.
            std::vector<uint8_t> payload;
            std::string ignored;
            if (frame != kNoSection && frame < frames.size() && reader.Decode(*frames[frame], payload, ignored)) {
                if (auto content = TextFrameFromPayload(payload)) layer->text = content;
            }
            loaded.push_back(layer);
        }
    }

    width = w;
    height = h;
    layers.swap(loaded);
    activeLayer = std::clamp(active, 0, static_cast<int>(layers.size()) - 1);
    selection.Resize(width, height);
    ResetComposite();
    filePath = path;
    modified = false;
    ClearHistory();
    EmitStructure();
    EmitSelection();
    EmitState();
    return true;
}

} // namespace UltraCanvas
