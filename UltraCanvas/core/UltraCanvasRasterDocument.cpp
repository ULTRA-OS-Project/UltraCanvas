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
// Version: 1.0.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework

#include "UltraCanvasRasterDocument.h"
#include "UltraCanvasZipPackage.h"
#include "DataFormats/UltraCanvasJSON.h"

#ifdef HAS_LIBVIPS
#include "PixelFX/PixelFX.h"
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

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
    if (onStateChanged) onStateChanged();
}

void UCRasterDocument::EmitStructure() {
    if (onStructureChanged) onStructureChanged();
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
    PushUndo(std::move(pendingStructural));
    SetModified(true);
    EmitStructure();
}

void UCRasterDocument::ApplyStructural(const RasterUndoEntry& e, bool toAfter) {
    const int newW = toAfter ? e.widthAfter : e.width;
    const int newH = toAfter ? e.heightAfter : e.height;
    const bool sizeChanged = newW != width || newH != height;
    width = newW; height = newH;
    layers = toAfter ? e.layersAfter : e.layersBefore;
    activeLayer = std::clamp(toAfter ? e.activeLayerAfter : e.activeLayer, 0,
                             std::max(0, static_cast<int>(layers.size()) - 1));
    const auto& sel = toAfter ? e.selectionAfter : e.selectionBefore;
    if (sel) selection.Assign(*sel);
    else selection.Resize(width, height);
    if (sizeChanged) ResetComposite(); else InvalidateComposite();
    EmitStructure();
    if (onSelectionChanged) onSelectionChanged();
}

// ===========================================================================
// LAYERS
// ===========================================================================

int UCRasterDocument::AddLayer(const std::string& name, const RasterPixel& fill) {
    std::string n = name;
    if (n.empty()) n = "Layer " + std::to_string(layers.size() + 1);
    return AddLayer(std::make_shared<UCRasterLayer>(width, height, fill, n), -1);
}

int UCRasterDocument::AddLayer(std::shared_ptr<UCRasterLayer> layer, int atIndex) {
    if (!layer) return -1;
    if (layer->GetWidth() != width || layer->GetHeight() != height) {
        auto fitted = std::make_shared<UCRasterLayer>(width, height, layer->name);
        fitted->visible = layer->visible; fitted->opacity = layer->opacity;
        fitted->blendMode = layer->blendMode; fitted->locked = layer->locked;
        fitted->CopyFrom(*layer, 0, 0);
        layer = fitted;
    }
    BeginStructuralUndo("Add Layer");
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

void UCRasterDocument::ReplaceLayer(int index, std::shared_ptr<UCRasterLayer> replacement, const std::string& label) {
    if (index < 0 || index >= static_cast<int>(layers.size()) || !replacement) return;
    BeginStructuralUndo(label);
    layers[static_cast<size_t>(index)] = std::move(replacement);
    InvalidateComposite();
    EndStructuralUndo();
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
    if (onSelectionChanged) onSelectionChanged();
    if (onStateChanged) onStateChanged();
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
    editing = true;
}

void UCRasterDocument::EndEdit() {
    if (!editing) return;
    editing = false;
    auto layer = GetLayer(pendingEdit.layerIndex);
    if (!layer) return;
    pendingEdit.after = SnapshotRect(*layer, pendingEdit.rect);
    if (pendingEdit.after == pendingEdit.before) return;   // nothing changed
    const Rect2Di r = pendingEdit.rect;
    PushUndo(std::move(pendingEdit));
    SetModified(true);
    NotifyChanged(r);
    if (onStateChanged) onStateChanged();
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
    const Rect2Di r = e.rect;
    PushUndo(std::move(e));
    SetModified(true);
    NotifyChanged(r);
    if (onStateChanged) onStateChanged();
}

void UCRasterDocument::CancelEdit() {
    if (!editing) return;
    editing = false;
    auto layer = GetLayer(pendingEdit.layerIndex);
    if (layer) {
        RestoreRect(*layer, pendingEdit.rect, pendingEdit.before);
        NotifyChanged(pendingEdit.rect);
    }
}

void UCRasterDocument::NotifyChanged(const Rect2Di& rect) {
    InvalidateComposite(rect);
    if (onPixelsChanged) onPixelsChanged(rect);
}

void UCRasterDocument::ApplyPixels(const RasterUndoEntry& e, bool toAfter) {
    auto layer = GetLayer(e.layerIndex);
    if (!layer) return;
    RestoreRect(*layer, e.rect, toAfter ? e.after : e.before);
    NotifyChanged(e.rect);
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
    if (e.selectionBefore) lastCommittedSelection = e.selectionBefore;
    redoStack.push_back(std::move(e));
    SetModified(true);
    if (onStateChanged) onStateChanged();
}

void UCRasterDocument::Redo() {
    if (redoStack.empty()) return;
    RasterUndoEntry e = std::move(redoStack.back());
    redoStack.pop_back();
    if (e.structural) ApplyStructural(e, true); else ApplyPixels(e, true);
    if (e.selectionAfter) lastCommittedSelection = e.selectionAfter;
    undoStack.push_back(std::move(e));
    SetModified(true);
    if (onStateChanged) onStateChanged();
}

void UCRasterDocument::ClearHistory() {
    undoStack.clear();
    redoStack.clear();
    undoMemoryUsed = 0;
    lastCommittedSelection = std::make_shared<UCRasterSelection>(selection);
    if (onStateChanged) onStateChanged();
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
    for (const Rect2Di& r : rects) {
        for (int y = r.y; y < r.y + r.height; ++y)
            std::memset(px + static_cast<size_t>(y) * stride + r.x, 0, static_cast<size_t>(r.width) * 4);
        for (const auto& l : layers) {
            if (!l || !l->visible || l->opacity <= 0.0f) continue;
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
    return LowerExt(path) == "ucraster";
}

bool UCRasterDocument::LoadFromFile(const std::string& path, std::string& error) {
    if (IsProjectFile(path)) return LoadProject(path, error);
#ifdef HAS_LIBVIPS
    try {
        PixelFX::PFXImage img = PixelFX::FileIO::Load(path);
        try { img = PixelFX::Resample::Autorot(img); } catch (...) {}
        auto layer = std::make_shared<UCRasterLayer>();
        if (!layer->FromPixelFX(img)) { error = "Could not decode " + path; return false; }
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
        if (onSelectionChanged) onSelectionChanged();
        if (onStateChanged) onStateChanged();
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
    if (IsProjectFile(path)) return SaveProject(path, error);
#ifdef HAS_LIBVIPS
    try {
        auto flat = Flatten();
        PixelFX::PFXImage img = flat->ToPixelFX();
        const std::string ext = LowerExt(path);
        if (ext == "jpg" || ext == "jpeg" || ext == "bmp" || ext == "ppm" || ext == "pgm") {
            img = PixelFX::Colour::Flatten(img);
        }
        if (options) {
            const std::string err = ExportVImage(img, path, *options);
            if (!err.empty()) { error = err; return false; }
        } else if (!PixelFX::FileIO::Save(img, path)) {
            error = PixelFX::GetLastError();
            if (error.empty()) error = "Could not write " + path;
            return false;
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
    if (onSelectionChanged) onSelectionChanged();
    if (onStateChanged) onStateChanged();
    return true;
#else
    (void)path;
    error = "Project loading needs libvips (HAS_LIBVIPS)";
    return false;
#endif
}

} // namespace UltraCanvas
