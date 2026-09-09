// Apps/UltraPaint/UltraPaintTools.cpp
// UltraPaint tool implementations: selection (rectangle, ellipse, lasso,
// magic wand), move, crop, eyedropper, the brush family (pencil, brush,
// airbrush, eraser, clone, smudge, dodge, burn), fill, gradient, shapes
// (line, rectangle, ellipse), text, zoom and pan.
// Version: 1.0.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework

#include "UltraPaintTools.h"

#include "UltraCanvasCheckbox.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasSpinner.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace UltraCanvas {

// ===========================================================================
// DEFAULT OPTIONS
// ===========================================================================

PaintToolOptions::PaintToolOptions() {
    brush.size = 16.0f;    brush.hardness = 0.75f; brush.opacity = 1.0f; brush.flow = 1.0f;
    pencil.size = 1.0f;    pencil.hardness = 1.0f; pencil.antialias = false; pencil.spacing = 0.1f;
    airbrush.size = 40.0f; airbrush.hardness = 0.0f; airbrush.flow = 0.08f; airbrush.spacing = 0.08f;
    eraser.size = 20.0f;   eraser.hardness = 0.9f;
    clone.size = 30.0f;    clone.hardness = 0.6f;
    smudge.size = 24.0f;   smudge.hardness = 0.5f; smudge.flow = 0.6f; smudge.spacing = 0.1f;
    dodgeBurn.size = 30.0f; dodgeBurn.hardness = 0.3f; dodgeBurn.flow = 0.3f;
}

// ===========================================================================
// OPTION WIDGET HELPERS
// ===========================================================================

namespace PaintOptionWidgets {

namespace {
    constexpr float kRowH = 24.0f;
    constexpr float kLabelW = 66.0f;

    std::shared_ptr<UltraCanvasContainer> MakeRow(const std::string& id) {
        auto row = std::make_shared<UltraCanvasContainer>(id, 0, 0, 0, kRowH);
        row->layout.SetFlexRow().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Center);
        row->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        return row;
    }

    std::string FormatValue(float v, bool integer) {
        char buf[32];
        if (integer) std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(v)));
        else std::snprintf(buf, sizeof(buf), "%.2f", v);
        return buf;
    }
}

void AddSliderRow(UltraCanvasContainer& panel, const std::string& id, const std::string& label,
                  float minVal, float maxVal, float value, float step, bool integer,
                  const std::function<void(float)>& onChange) {
    auto row = MakeRow(id + "-row");
    auto lbl = CreateLabel(id + "-label", 0, 0, kLabelW, kRowH, label);
    lbl->SetFontSize(11);
    lbl->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    row->AddChild(lbl);
    auto slider = CreateHorizontalSlider(id + "-slider", 0, 0, 100, kRowH, minVal, maxVal);
    slider->SetStep(step);
    slider->SetValue(value);
    slider->SetValueDisplay(SliderValueDisplay::NoDisplay);
    slider->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    row->AddChild(slider);
    auto val = CreateLabel(id + "-value", 0, 0, 40, kRowH, FormatValue(value, integer));
    val->SetFontSize(11);
    val->SetAlignment(TextAlignment::Right);
    val->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    row->AddChild(val);
    auto valPtr = val.get();
    slider->onValueChanging = [valPtr, integer](float v) { valPtr->SetText(FormatValue(v, integer)); };
    slider->onValueChanged = [valPtr, integer, onChange](float v) {
        valPtr->SetText(FormatValue(v, integer));
        if (onChange) onChange(integer ? std::round(v) : v);
    };
    panel.AddChild(row);
}

void AddCheckbox(UltraCanvasContainer& panel, const std::string& id, const std::string& label,
                 bool checked, const std::function<void(bool)>& onChange) {
    auto cb = std::make_shared<UltraCanvasCheckbox>(id, 0, 0, 0, kRowH, label);
    cb->SetChecked(checked);
    cb->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    cb->onStateChanged = [onChange](CheckedState, CheckedState n) {
        if (onChange) onChange(n == CheckedState::Checked);
    };
    panel.AddChild(cb);
}

void AddDropdown(UltraCanvasContainer& panel, const std::string& id, const std::string& label,
                 const std::vector<std::string>& items, int selected,
                 const std::function<void(int)>& onChange) {
    auto row = MakeRow(id + "-row");
    auto lbl = CreateLabel(id + "-label", 0, 0, kLabelW, kRowH, label);
    lbl->SetFontSize(11);
    lbl->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    row->AddChild(lbl);
    auto dd = CreateDropdown(id + "-dropdown", 0, 0, 120, kRowH);
    for (const auto& it : items) dd->AddItem(it);
    dd->SetSelectedIndex(std::clamp(selected, 0, static_cast<int>(items.size()) - 1), false);
    dd->onSelectionChanged = [onChange](int index, const DropdownItem&) { if (onChange) onChange(index); };
    dd->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
    row->AddChild(dd);
    panel.AddChild(row);
}

void AddCaption(UltraCanvasContainer& panel, const std::string& id, const std::string& text) {
    auto lbl = CreateLabel(id, 0, 0, 0, 20, text);
    lbl->SetFontSize(11);
    lbl->SetFontWeight(FontWeight::Bold);
    lbl->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    panel.AddChild(lbl);
}

void AddBrushOptions(UltraCanvasContainer& panel, const std::string& p, UCBrushSettings& s,
                     bool showHardness, bool showFlow, const std::function<void()>& changed) {
    AddSliderRow(panel, p + "-size", "Size", 1, 300, s.size, 1, true,
                 [&s, changed](float v) { s.size = v; if (changed) changed(); });
    if (showHardness)
        AddSliderRow(panel, p + "-hard", "Hardness", 0, 1, s.hardness, 0.05f, false,
                     [&s, changed](float v) { s.hardness = v; if (changed) changed(); });
    AddSliderRow(panel, p + "-opacity", "Opacity", 0, 1, s.opacity, 0.05f, false,
                 [&s, changed](float v) { s.opacity = v; if (changed) changed(); });
    if (showFlow)
        AddSliderRow(panel, p + "-flow", "Flow", 0.01f, 1, s.flow, 0.01f, false,
                     [&s, changed](float v) { s.flow = v; if (changed) changed(); });
    AddSliderRow(panel, p + "-spacing", "Spacing", 0.02f, 1, s.spacing, 0.02f, false,
                 [&s, changed](float v) { s.spacing = v; if (changed) changed(); });
    AddDropdown(panel, p + "-shape", "Shape", { "Round", "Square" }, s.shape == BrushShape::Square ? 1 : 0,
                [&s, changed](int i) { s.shape = i == 1 ? BrushShape::Square : BrushShape::Round; if (changed) changed(); });
    AddCheckbox(panel, p + "-aa", "Anti-aliased edge", s.antialias,
                [&s, changed](bool v) { s.antialias = v; if (changed) changed(); });
    AddCheckbox(panel, p + "-psize", "Pressure controls size", s.pressureSize,
                [&s](bool v) { s.pressureSize = v; });
    AddCheckbox(panel, p + "-popacity", "Pressure controls opacity", s.pressureOpacity,
                [&s](bool v) { s.pressureOpacity = v; });
}

} // namespace PaintOptionWidgets

// ===========================================================================
// SHARED HELPERS
// ===========================================================================

namespace {

using namespace PaintOptionWidgets;

RasterSelectionMode ModeFromModifiers(const PaintPointerEvent& e) {
    if (e.shift && e.alt) return RasterSelectionMode::Intersect;
    if (e.shift) return RasterSelectionMode::Add;
    if (e.alt) return RasterSelectionMode::Subtract;
    return RasterSelectionMode::Replace;
}

Rect2Di NormalizedRect(double x0, double y0, double x1, double y1, bool square) {
    double w = x1 - x0, h = y1 - y0;
    if (square) {
        const double s = std::max(std::fabs(w), std::fabs(h));
        w = w < 0 ? -s : s;
        h = h < 0 ? -s : s;
    }
    const int ax = static_cast<int>(std::floor(std::min(x0, x0 + w)));
    const int ay = static_cast<int>(std::floor(std::min(y0, y0 + h)));
    const int bx = static_cast<int>(std::ceil(std::max(x0, x0 + w)));
    const int by = static_cast<int>(std::ceil(std::max(y0, y0 + h)));
    return Rect2Di(ax, ay, bx - ax, by - ay);
}

Rect2Df NormalizedRectF(double x0, double y0, double x1, double y1, bool square) {
    double w = x1 - x0, h = y1 - y0;
    if (square) {
        const double s = std::max(std::fabs(w), std::fabs(h));
        w = w < 0 ? -s : s;
        h = h < 0 ? -s : s;
    }
    return Rect2Df(static_cast<float>(std::min(x0, x0 + w)), static_cast<float>(std::min(y0, y0 + h)),
                   static_cast<float>(std::fabs(w)), static_cast<float>(std::fabs(h)));
}

void StrokeViewRect(IRenderContext* ctx, const Rect2Dd& r) {
    ctx->SetStrokeWidth(1.0);
    ctx->SetStrokePaint(Colors::White);
    ctx->SetLineDash(UCDashPattern());
    ctx->DrawRectangle(Rect2Dd(std::floor(r.x) + 0.5, std::floor(r.y) + 0.5, std::round(r.width), std::round(r.height)));
    ctx->SetStrokePaint(Colors::Black);
    ctx->SetLineDash(UCDashPattern({4.0, 4.0}));
    ctx->DrawRectangle(Rect2Dd(std::floor(r.x) + 0.5, std::floor(r.y) + 0.5, std::round(r.width), std::round(r.height)));
    ctx->SetLineDash(UCDashPattern());
}

// Active layer if it can be painted on; reports why not otherwise.
std::shared_ptr<UCRasterLayer> PaintableLayer(PaintToolContext& ctx, std::shared_ptr<UCRasterDocument>& doc) {
    doc = ctx.getDocument ? ctx.getDocument() : nullptr;
    if (!doc || !doc->IsValid()) return nullptr;
    auto layer = doc->GetActiveLayer();
    if (!layer) return nullptr;
    if (layer->locked) { if (ctx.setStatus) ctx.setStatus("The active layer is locked"); return nullptr; }
    if (!layer->visible) { if (ctx.setStatus) ctx.setStatus("The active layer is hidden"); return nullptr; }
    return layer;
}

// Run a one-shot edit on the active layer with proper undo.
template <typename F>
void EditActiveLayer(PaintToolContext& ctx, const std::string& label, F&& fn) {
    std::shared_ptr<UCRasterDocument> doc;
    auto layer = PaintableLayer(ctx, doc);
    if (!layer) return;
    auto before = layer->Clone();
    const Rect2Di changed = fn(*doc, *layer, &doc->GetSelection());
    if (changed.width > 0 && changed.height > 0) doc->RecordEdit(label, doc->GetActiveLayerIndex(), changed, *before);
}

// ===========================================================================
// SELECTION TOOLS
// ===========================================================================

class MarqueeTool : public PaintTool {
public:
    MarqueeTool(PaintToolId id, const std::string& name, const std::string& icon, char key, bool ellipse)
        : PaintTool(id, name, icon, key,
                    "Drag to select. Shift adds, Alt subtracts, Shift+Alt intersects; Ctrl for a square"),
          isEllipse(ellipse) {}

    void OnPress(PaintToolContext&, const PaintPointerEvent& e) override {
        dragging = true; x0 = x1 = e.x; y0 = y1 = e.y; square = e.ctrl;
    }
    void OnDrag(PaintToolContext& ctx, const PaintPointerEvent& e) override {
        if (!dragging) return;
        x1 = e.x; y1 = e.y; square = e.ctrl;
        const Rect2Di r = NormalizedRect(x0, y0, x1, y1, square);
        if (ctx.setStatus) ctx.setStatus("Selection: " + std::to_string(r.width) + " x " + std::to_string(r.height));
        ctx.surface->Refresh();
    }
    void OnRelease(PaintToolContext& ctx, const PaintPointerEvent& e) override {
        if (!dragging) return;
        dragging = false;
        auto doc = ctx.getDocument();
        if (!doc) return;
        const Rect2Di r = NormalizedRect(x0, y0, e.x, e.y, square);
        UCRasterSelection& sel = doc->GetSelection();
        if (r.width < 1 || r.height < 1) {
            if (ModeFromModifiers(e) == RasterSelectionMode::Replace) { sel.SelectNone(); doc->CommitSelectionChange("Deselect"); }
            ctx.surface->Refresh();
            return;
        }
        if (isEllipse) sel.SetEllipse(r, ModeFromModifiers(e), ctx.options->selectionAntialias);
        else sel.SetRectangle(r, ModeFromModifiers(e));
        if (ctx.options->selectionFeather > 0) sel.Feather(ctx.options->selectionFeather);
        doc->CommitSelectionChange(isEllipse ? "Ellipse Select" : "Rectangle Select");
        ctx.surface->Refresh();
    }
    void DrawOverlay(PaintToolContext&, IRenderContext* ctx, const PaintViewTransform& v) override {
        if (!dragging) return;
        const Rect2Di r = NormalizedRect(x0, y0, x1, y1, square);
        const Rect2Dd vr = v.ImageToView(Rect2Dd(r.x, r.y, r.width, r.height));
        if (isEllipse) {
            ctx->SetStrokeWidth(1.0);
            ctx->SetStrokePaint(Colors::White);
            ctx->SetLineDash(UCDashPattern());
            ctx->DrawEllipse(vr);
            ctx->SetStrokePaint(Colors::Black);
            ctx->SetLineDash(UCDashPattern({4.0, 4.0}));
            ctx->DrawEllipse(vr);
            ctx->SetLineDash(UCDashPattern());
        } else {
            StrokeViewRect(ctx, vr);
        }
    }
    void BuildOptions(PaintToolContext& ctx, UltraCanvasContainer& panel, const std::function<void()>&) override {
        AddSliderRow(panel, "sel-feather", "Feather", 0, 50, static_cast<float>(ctx.options->selectionFeather), 1, true,
                     [o = ctx.options](float v) { o->selectionFeather = static_cast<int>(v); });
        if (isEllipse)
            AddCheckbox(panel, "sel-aa", "Anti-aliased", ctx.options->selectionAntialias,
                        [o = ctx.options](bool v) { o->selectionAntialias = v; });
        AddCaption(panel, "sel-hint", "Shift: add   Alt: subtract   Ctrl: square");
    }
private:
    bool isEllipse;
    bool dragging = false, square = false;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

class LassoTool : public PaintTool {
public:
    LassoTool() : PaintTool(PaintToolId::Lasso, "Lasso", "lasso.svg", 'L',
                            "Drag a free-hand outline. Shift adds, Alt subtracts") {}
    void OnPress(PaintToolContext&, const PaintPointerEvent& e) override {
        points.clear(); points.emplace_back(static_cast<float>(e.x), static_cast<float>(e.y)); dragging = true;
    }
    void OnDrag(PaintToolContext& ctx, const PaintPointerEvent& e) override {
        if (!dragging) return;
        const Point2Df p(static_cast<float>(e.x), static_cast<float>(e.y));
        if (points.empty() || std::fabs(p.x - points.back().x) + std::fabs(p.y - points.back().y) > 0.5f) points.push_back(p);
        ctx.surface->Refresh();
    }
    void OnRelease(PaintToolContext& ctx, const PaintPointerEvent& e) override {
        if (!dragging) return;
        dragging = false;
        auto doc = ctx.getDocument();
        if (!doc) return;
        if (points.size() < 3) {
            if (ModeFromModifiers(e) == RasterSelectionMode::Replace) { doc->GetSelection().SelectNone(); doc->CommitSelectionChange("Deselect"); }
            points.clear(); ctx.surface->Refresh();
            return;
        }
        doc->GetSelection().SetPolygon(points, ModeFromModifiers(e), ctx.options->selectionAntialias);
        if (ctx.options->selectionFeather > 0) doc->GetSelection().Feather(ctx.options->selectionFeather);
        doc->CommitSelectionChange("Lasso Select");
        points.clear();
        ctx.surface->Refresh();
    }
    void DrawOverlay(PaintToolContext&, IRenderContext* ctx, const PaintViewTransform& v) override {
        if (!dragging || points.size() < 2) return;
        std::vector<Point2Dd> vp;
        vp.reserve(points.size());
        for (const auto& p : points) vp.push_back(v.ImageToView(p.x, p.y));
        ctx->SetStrokeWidth(1.0);
        ctx->SetStrokePaint(Colors::White);
        ctx->SetLineDash(UCDashPattern());
        ctx->DrawLinePath(vp, true);
        ctx->SetStrokePaint(Colors::Black);
        ctx->SetLineDash(UCDashPattern({4.0, 4.0}));
        ctx->DrawLinePath(vp, true);
        ctx->SetLineDash(UCDashPattern());
    }
    void BuildOptions(PaintToolContext& ctx, UltraCanvasContainer& panel, const std::function<void()>&) override {
        AddSliderRow(panel, "lasso-feather", "Feather", 0, 50, static_cast<float>(ctx.options->selectionFeather), 1, true,
                     [o = ctx.options](float v) { o->selectionFeather = static_cast<int>(v); });
        AddCheckbox(panel, "lasso-aa", "Anti-aliased", ctx.options->selectionAntialias,
                    [o = ctx.options](bool v) { o->selectionAntialias = v; });
    }
private:
    std::vector<Point2Df> points;
    bool dragging = false;
};

class MagicWandTool : public PaintTool {
public:
    MagicWandTool() : PaintTool(PaintToolId::MagicWand, "Magic Wand", "wand.svg", 'W',
                                "Click a colour to select the region of similar colour") {}
    void OnPress(PaintToolContext& ctx, const PaintPointerEvent& e) override {
        auto doc = ctx.getDocument();
        if (!doc || !e.insideImage) return;
        std::shared_ptr<UCRasterLayer> sample = ctx.options->wandSampleMerged ? doc->Flatten() : doc->GetActiveLayer();
        if (!sample) return;
        std::vector<uint8_t> mask = RasterPaint::MagicWandMask(*sample, static_cast<int>(e.x), static_cast<int>(e.y),
                                                               ctx.options->wandTolerance, ctx.options->wandContiguous);
        doc->GetSelection().SetMask(mask, ModeFromModifiers(e));
        if (ctx.options->selectionFeather > 0) doc->GetSelection().Feather(ctx.options->selectionFeather);
        doc->CommitSelectionChange("Magic Wand");
        ctx.surface->Refresh();
    }
    void BuildOptions(PaintToolContext& ctx, UltraCanvasContainer& panel, const std::function<void()>&) override {
        auto o = ctx.options;
        AddSliderRow(panel, "wand-tol", "Tolerance", 0, 255, static_cast<float>(o->wandTolerance), 1, true,
                     [o](float v) { o->wandTolerance = static_cast<int>(v); });
        AddCheckbox(panel, "wand-contig", "Contiguous", o->wandContiguous, [o](bool v) { o->wandContiguous = v; });
        AddCheckbox(panel, "wand-merged", "Sample merged", o->wandSampleMerged, [o](bool v) { o->wandSampleMerged = v; });
        AddSliderRow(panel, "wand-feather", "Feather", 0, 50, static_cast<float>(o->selectionFeather), 1, true,
                     [o](float v) { o->selectionFeather = static_cast<int>(v); });
    }
};

// ===========================================================================
// MOVE
// ===========================================================================

class MoveTool : public PaintTool {
public:
    MoveTool() : PaintTool(PaintToolId::Move, "Move", "move.svg", 'M',
                           "Drag to move the selection (or the whole layer)") {}
    UCMouseCursor Cursor() const override { return UCMouseCursor::SizeAll; }
    void OnPress(PaintToolContext& ctx, const PaintPointerEvent& e) override {
        auto layer = PaintableLayer(ctx, doc);
        if (!layer) return;
        snapshot = layer->Clone();
        floating.reset();
        const UCRasterSelection& sel = doc->GetSelection();
        movingSelection = sel.IsActive() && ctx.options->moveSelectionOnly;
        if (movingSelection) {
            floating = doc->CopySelection(floatOrigin);
        }
        startX = e.x; startY = e.y; dx = dy = 0;
        active = true;
    }
    void OnDrag(PaintToolContext& ctx, const PaintPointerEvent& e) override {
        if (!active) return;
        dx = static_cast<int>(std::lround(e.x - startX));
        dy = static_cast<int>(std::lround(e.y - startY));
        Apply(ctx);
        if (ctx.setStatus) ctx.setStatus("Move: " + std::to_string(dx) + ", " + std::to_string(dy));
    }
    void OnRelease(PaintToolContext& ctx, const PaintPointerEvent& e) override {
        if (!active) return;
        active = false;
        dx = static_cast<int>(std::lround(e.x - startX));
        dy = static_cast<int>(std::lround(e.y - startY));
        Apply(ctx);
        auto layer = doc->GetActiveLayer();
        if (layer && snapshot && (dx != 0 || dy != 0)) {
            doc->RecordEdit("Move", doc->GetActiveLayerIndex(), layer->GetRect(), *snapshot);
            if (movingSelection) {
                doc->GetSelection().Translate(dx, dy);
                doc->CommitSelectionChange("Move Selection");
            }
        }
        snapshot.reset(); floating.reset();
    }
    void BuildOptions(PaintToolContext& ctx, UltraCanvasContainer& panel, const std::function<void()>&) override {
        AddCheckbox(panel, "move-sel", "Move selected pixels only", ctx.options->moveSelectionOnly,
                    [o = ctx.options](bool v) { o->moveSelectionOnly = v; });
        AddCaption(panel, "move-hint", "Without a selection the whole layer moves");
    }
private:
    void Apply(PaintToolContext&) {
        auto layer = doc->GetActiveLayer();
        if (!layer || !snapshot) return;
        layer->CopyFrom(*snapshot, 0, 0);
        if (movingSelection && floating) {
            // clear the pixels being lifted, then drop them at the offset
            const UCRasterSelection& sel = doc->GetSelection();
            const Rect2Di b = sel.GetBounds();
            for (int y = b.y; y < b.y + b.height; ++y) {
                uint8_t* row = layer->Row(y);
                for (int x = b.x; x < b.x + b.width; ++x) {
                    const int cov = sel.Coverage(x, y);
                    if (!cov) continue;
                    uint8_t* p = row + static_cast<size_t>(x) * 4;
                    p[3] = static_cast<uint8_t>((p[3] * (255 - cov) + 127) / 255);
                }
            }
            layer->BlendFrom(*floating, floatOrigin.x + dx, floatOrigin.y + dy);
        } else {
            layer->Clear();
            layer->CopyFrom(*snapshot, dx, dy);
        }
        doc->NotifyChanged(layer->GetRect());
    }
    std::shared_ptr<UCRasterDocument> doc;
    std::shared_ptr<UCRasterLayer> snapshot, floating;
    Point2Di floatOrigin;
    bool active = false, movingSelection = false;
    double startX = 0, startY = 0;
    int dx = 0, dy = 0;
};

// ===========================================================================
// CROP
// ===========================================================================

class CropTool : public PaintTool {
public:
    CropTool() : PaintTool(PaintToolId::Crop, "Crop", "crop.svg", 'C',
                           "Drag the area to keep, then press Enter (Escape cancels)") {}
    void OnPress(PaintToolContext&, const PaintPointerEvent& e) override {
        dragging = true; x0 = x1 = e.x; y0 = y1 = e.y; hasRect = false;
    }
    void OnDrag(PaintToolContext& ctx, const PaintPointerEvent& e) override {
        if (!dragging) return;
        x1 = e.x; y1 = e.y;
        rect = NormalizedRect(x0, y0, x1, y1, e.ctrl);
        hasRect = rect.width > 0 && rect.height > 0;
        if (ctx.setStatus) ctx.setStatus("Crop: " + std::to_string(rect.width) + " x " + std::to_string(rect.height) + "  (Enter to apply)");
        ctx.surface->Refresh();
    }
    void OnRelease(PaintToolContext& ctx, const PaintPointerEvent&) override {
        dragging = false;
        ctx.surface->Refresh();
    }
    void OnDoubleClick(PaintToolContext& ctx, const PaintPointerEvent&) override { Commit(ctx); }
    bool OnKey(PaintToolContext& ctx, const UCEvent& ev) override {
        if (ev.virtualKey == UCKeys::Return) { Commit(ctx); return true; }
        if (ev.virtualKey == UCKeys::Escape) { hasRect = false; ctx.surface->Refresh(); return true; }
        return false;
    }
    void Deactivate(PaintToolContext& ctx) override { hasRect = false; dragging = false; ctx.surface->Refresh(); }
    void DrawOverlay(PaintToolContext& tc, IRenderContext* ctx, const PaintViewTransform& v) override {
        if (!hasRect) return;
        auto doc = tc.getDocument();
        if (!doc) return;
        const Rect2Dd img = v.ImageToView(Rect2Dd(0, 0, doc->GetWidth(), doc->GetHeight()));
        const Rect2Dd keep = v.ImageToView(Rect2Dd(rect.x, rect.y, rect.width, rect.height));
        // darken outside the crop
        ctx->SetFillPaint(Color(0, 0, 0, 110));
        ctx->ClearPath();
        ctx->Rect(img.x, img.y, img.width, keep.y - img.y);
        ctx->Rect(img.x, keep.y + keep.height, img.width, img.y + img.height - keep.y - keep.height);
        ctx->Rect(img.x, keep.y, keep.x - img.x, keep.height);
        ctx->Rect(keep.x + keep.width, keep.y, img.x + img.width - keep.x - keep.width, keep.height);
        ctx->Fill();
        StrokeViewRect(ctx, keep);
    }
private:
    void Commit(PaintToolContext& ctx) {
        if (!hasRect) return;
        auto doc = ctx.getDocument();
        if (!doc) return;
        doc->CropTo(rect);
        hasRect = false;
        ctx.surface->ZoomToFit();
        if (ctx.setStatus) ctx.setStatus("Cropped to " + std::to_string(doc->GetWidth()) + " x " + std::to_string(doc->GetHeight()));
    }
    bool dragging = false, hasRect = false;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    Rect2Di rect;
};

// ===========================================================================
// EYEDROPPER
// ===========================================================================

class EyedropperTool : public PaintTool {
public:
    EyedropperTool() : PaintTool(PaintToolId::Eyedropper, "Eyedropper", "eyedropper.svg", 'I',
                                 "Click to pick the foreground colour; right-click for the background") {}
    void OnPress(PaintToolContext& ctx, const PaintPointerEvent& e) override { Pick(ctx, e); }
    void OnDrag(PaintToolContext& ctx, const PaintPointerEvent& e) override { Pick(ctx, e); }
    void BuildOptions(PaintToolContext& ctx, UltraCanvasContainer& panel, const std::function<void()>&) override {
        auto o = ctx.options;
        AddDropdown(panel, "eye-size", "Sample", { "Point", "3 x 3 average", "5 x 5 average", "11 x 11 average" },
                    o->eyedropperRadius == 0 ? 0 : o->eyedropperRadius == 1 ? 1 : o->eyedropperRadius == 2 ? 2 : 3,
                    [o](int i) { o->eyedropperRadius = i == 0 ? 0 : i == 1 ? 1 : i == 2 ? 2 : 5; });
        AddCheckbox(panel, "eye-merged", "Sample merged", o->eyedropperMerged, [o](bool v) { o->eyedropperMerged = v; });
    }
private:
    void Pick(PaintToolContext& ctx, const PaintPointerEvent& e) {
        auto doc = ctx.getDocument();
        if (!doc || !e.insideImage) return;
        std::shared_ptr<UCRasterLayer> sample = ctx.options->eyedropperMerged ? doc->Flatten() : doc->GetActiveLayer();
        if (!sample) return;
        const RasterPixel p = RasterPaint::SampleColour(*sample, static_cast<int>(e.x), static_cast<int>(e.y), ctx.options->eyedropperRadius);
        if (e.button == UCMouseButton::Right) { if (ctx.setBackground) ctx.setBackground(p); }
        else if (ctx.setForeground) ctx.setForeground(p);
        if (ctx.setStatus) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Picked #%02X%02X%02X  alpha %d", p.r, p.g, p.b, p.a);
            ctx.setStatus(buf);
        }
    }
};

// ===========================================================================
// BRUSH FAMILY
// ===========================================================================

class BrushTool : public PaintTool {
public:
    BrushTool(PaintToolId id, const std::string& name, const std::string& icon, char key, const std::string& hint,
              BrushMode m, UCBrushSettings PaintToolOptions::* settingsMember, bool hardness, bool flow)
        : PaintTool(id, name, icon, key, hint), mode(m), member(settingsMember),
          showHardness(hardness), showFlow(flow) {}

    UCBrushSettings& Settings(PaintToolContext& ctx) const { return ctx.options->*member; }
    double CursorRadius(PaintToolContext& ctx) const override { return Settings(ctx).size * 0.5; }
    bool CursorSquare(PaintToolContext& ctx) const override { return Settings(ctx).shape == BrushShape::Square; }

    void OnPress(PaintToolContext& ctx, const PaintPointerEvent& e) override {
        if (mode == BrushMode::Clone && e.ctrl) {
            cloneSourceSet = true; cloneSrcX = e.x; cloneSrcY = e.y; cloneOffsetKnown = false;
            if (ctx.setStatus) ctx.setStatus("Clone source set — paint to clone from it");
            ctx.surface->Refresh();
            return;
        }
        auto layer = PaintableLayer(ctx, doc);
        if (!layer) return;
        if (mode == BrushMode::Clone) {
            if (!cloneSourceSet) { if (ctx.setStatus) ctx.setStatus("Ctrl+click to set the clone source first"); return; }
            if (!cloneOffsetKnown) {
                cloneDx = static_cast<int>(std::lround(cloneSrcX - e.x));
                cloneDy = static_cast<int>(std::lround(cloneSrcY - e.y));
                cloneOffsetKnown = true;
            }
        }
        RasterPixel colour = e.button == UCMouseButton::Right ? ctx.background() : ctx.foreground();
        stroke.Begin(layer, &doc->GetSelection(), Settings(ctx), mode, colour, RasterBlendMode::Normal);
        if (mode == BrushMode::Clone) stroke.SetCloneSource(layer, cloneDx, cloneDy);
        painting = true;
        const Rect2Di r = stroke.AddPoint(static_cast<float>(e.x), static_cast<float>(e.y), e.pressure);
        if (r.width > 0) doc->NotifyChanged(r);
    }
    void OnDrag(PaintToolContext&, const PaintPointerEvent& e) override {
        if (!painting) return;
        const Rect2Di r = stroke.AddPoint(static_cast<float>(e.x), static_cast<float>(e.y), e.pressure);
        if (r.width > 0) doc->NotifyChanged(r);
    }
    void OnRelease(PaintToolContext&, const PaintPointerEvent& e) override {
        if (!painting) return;
        painting = false;
        const Rect2Di r = stroke.AddPoint(static_cast<float>(e.x), static_cast<float>(e.y), e.pressure);
        if (r.width > 0) doc->NotifyChanged(r);
        const Rect2Di dirty = stroke.GetDirtyBounds();
        auto before = stroke.GetBefore();
        if (before && dirty.width > 0) doc->RecordEdit(name, doc->GetActiveLayerIndex(), dirty, *before);
        stroke.End();
    }
    void OnHover(PaintToolContext& ctx, const PaintPointerEvent& e) override {
        if (mode == BrushMode::Clone && cloneSourceSet && cloneOffsetKnown) {
            hoverX = e.x; hoverY = e.y; ctx.surface->Refresh();
        }
    }
    bool OnKey(PaintToolContext& ctx, const UCEvent& ev) override {
        // [ and ] change the size, as in every editor
        if (ev.character == '[' || ev.virtualKey == UCKeys::LeftBracket) {
            Settings(ctx).size = std::max(1.0f, Settings(ctx).size * 0.85f - 0.5f);
        } else if (ev.character == ']' || ev.virtualKey == UCKeys::RightBracket) {
            Settings(ctx).size = std::min(300.0f, Settings(ctx).size * 1.18f + 0.5f);
        } else return false;
        ctx.surface->SetCursorRadius(CursorRadius(ctx));
        if (ctx.refreshOptions) ctx.refreshOptions();
        return true;
    }
    void DrawOverlay(PaintToolContext&, IRenderContext* ctx, const PaintViewTransform& v) override {
        if (mode != BrushMode::Clone || !cloneSourceSet) return;
        Point2Dd s = v.ImageToView(cloneSrcX, cloneSrcY);
        if (cloneOffsetKnown && !painting) s = v.ImageToView(hoverX + cloneDx, hoverY + cloneDy);
        ctx->SetStrokeWidth(1.5);
        ctx->SetLineDash(UCDashPattern());
        ctx->SetStrokePaint(Color(255, 80, 80, 230));
        ctx->DrawLine(Point2Dd(s.x - 8, s.y), Point2Dd(s.x + 8, s.y));
        ctx->DrawLine(Point2Dd(s.x, s.y - 8), Point2Dd(s.x, s.y + 8));
    }
    void BuildOptions(PaintToolContext& ctx, UltraCanvasContainer& panel, const std::function<void()>& changed) override {
        AddBrushOptions(panel, "brush-" + std::to_string(static_cast<int>(id)), Settings(ctx), showHardness, showFlow, changed);
        if (mode == BrushMode::Clone) AddCaption(panel, "clone-hint", "Ctrl+click sets the source point");
        AddCaption(panel, "brush-keys", "[ and ] change the size");
    }
    void Deactivate(PaintToolContext& ctx) override { cloneOffsetKnown = false; ctx.surface->Refresh(); }

private:
    BrushMode mode;
    UCBrushSettings PaintToolOptions::* member;
    bool showHardness, showFlow;
    UCBrushStroke stroke;
    std::shared_ptr<UCRasterDocument> doc;
    bool painting = false;
    bool cloneSourceSet = false, cloneOffsetKnown = false;
    double cloneSrcX = 0, cloneSrcY = 0, hoverX = 0, hoverY = 0;
    int cloneDx = 0, cloneDy = 0;
};

// ===========================================================================
// FILL
// ===========================================================================

class FillTool : public PaintTool {
public:
    FillTool() : PaintTool(PaintToolId::Fill, "Fill", "fill.svg", 'F',
                           "Click to flood-fill with the foreground colour (right-click: background)") {}
    void OnPress(PaintToolContext& ctx, const PaintPointerEvent& e) override {
        if (!e.insideImage) return;
        const RasterPixel colour = e.button == UCMouseButton::Right ? ctx.background() : ctx.foreground();
        EditActiveLayer(ctx, "Fill", [&](UCRasterDocument& doc, UCRasterLayer& layer, const UCRasterSelection* sel) {
            std::shared_ptr<UCRasterLayer> merged = ctx.options->fillSampleMerged ? doc.Flatten() : nullptr;
            return RasterPaint::FloodFill(layer, sel, static_cast<int>(e.x), static_cast<int>(e.y), colour,
                                          ctx.options->fillTolerance, ctx.options->fillContiguous, merged.get(),
                                          RasterBlendMode::Normal, ctx.options->fillOpacity);
        });
    }
    void BuildOptions(PaintToolContext& ctx, UltraCanvasContainer& panel, const std::function<void()>&) override {
        auto o = ctx.options;
        AddSliderRow(panel, "fill-tol", "Tolerance", 0, 255, static_cast<float>(o->fillTolerance), 1, true,
                     [o](float v) { o->fillTolerance = static_cast<int>(v); });
        AddSliderRow(panel, "fill-opacity", "Opacity", 0, 1, o->fillOpacity, 0.05f, false, [o](float v) { o->fillOpacity = v; });
        AddCheckbox(panel, "fill-contig", "Contiguous", o->fillContiguous, [o](bool v) { o->fillContiguous = v; });
        AddCheckbox(panel, "fill-merged", "Sample merged", o->fillSampleMerged, [o](bool v) { o->fillSampleMerged = v; });
    }
};

// ===========================================================================
// GRADIENT
// ===========================================================================

class GradientTool : public PaintTool {
public:
    GradientTool() : PaintTool(PaintToolId::Gradient, "Gradient", "gradient.svg", 'G',
                               "Drag from the foreground colour to the background colour") {}
    void OnPress(PaintToolContext&, const PaintPointerEvent& e) override { dragging = true; x0 = x1 = e.x; y0 = y1 = e.y; }
    void OnDrag(PaintToolContext& ctx, const PaintPointerEvent& e) override { if (dragging) { x1 = e.x; y1 = e.y; ctx.surface->Refresh(); } }
    void OnRelease(PaintToolContext& ctx, const PaintPointerEvent& e) override {
        if (!dragging) return;
        dragging = false;
        x1 = e.x; y1 = e.y;
        if (std::fabs(x1 - x0) + std::fabs(y1 - y0) < 1.0) { ctx.surface->Refresh(); return; }
        EditActiveLayer(ctx, "Gradient", [&](UCRasterDocument&, UCRasterLayer& layer, const UCRasterSelection* sel) {
            return RasterPaint::FillGradient(layer, sel, Point2Df(static_cast<float>(x0), static_cast<float>(y0)),
                                             Point2Df(static_cast<float>(x1), static_cast<float>(y1)),
                                             ctx.foreground(), ctx.background(), ctx.options->gradientKind,
                                             ctx.options->gradientOpacity);
        });
        ctx.surface->Refresh();
    }
    void DrawOverlay(PaintToolContext&, IRenderContext* ctx, const PaintViewTransform& v) override {
        if (!dragging) return;
        const Point2Dd a = v.ImageToView(x0, y0), b = v.ImageToView(x1, y1);
        ctx->SetStrokeWidth(1.0);
        ctx->SetLineDash(UCDashPattern());
        ctx->SetStrokePaint(Colors::White);
        ctx->DrawLine(a, b);
        ctx->SetStrokePaint(Colors::Black);
        ctx->SetLineDash(UCDashPattern({4.0, 4.0}));
        ctx->DrawLine(a, b);
        ctx->SetLineDash(UCDashPattern());
        ctx->DrawFilledCircle(a, 4.0f, Colors::White, Colors::Black, 1.0f);
        ctx->DrawFilledCircle(b, 4.0f, Colors::Black, Colors::White, 1.0f);
    }
    void BuildOptions(PaintToolContext& ctx, UltraCanvasContainer& panel, const std::function<void()>&) override {
        auto o = ctx.options;
        AddDropdown(panel, "grad-kind", "Type", { "Linear", "Radial", "Reflected" }, static_cast<int>(o->gradientKind),
                    [o](int i) { o->gradientKind = static_cast<RasterPaint::GradientKind>(i); });
        AddSliderRow(panel, "grad-opacity", "Opacity", 0, 1, o->gradientOpacity, 0.05f, false, [o](float v) { o->gradientOpacity = v; });
    }
private:
    bool dragging = false;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

// ===========================================================================
// SHAPES
// ===========================================================================

class ShapeTool : public PaintTool {
public:
    enum class Kind { Line, Rectangle, Ellipse };
    ShapeTool(PaintToolId id, const std::string& name, const std::string& icon, char key, Kind k)
        : PaintTool(id, name, icon, key,
                    k == Kind::Line ? "Drag to draw a line (Shift constrains the angle)"
                                    : "Drag to draw; Shift constrains to a square / circle. Foreground = outline, background = fill"),
          kind(k) {}
    void OnPress(PaintToolContext&, const PaintPointerEvent& e) override { dragging = true; x0 = x1 = e.x; y0 = y1 = e.y; constrain = e.shift; }
    void OnDrag(PaintToolContext& ctx, const PaintPointerEvent& e) override {
        if (!dragging) return;
        x1 = e.x; y1 = e.y; constrain = e.shift;
        ctx.surface->Refresh();
    }
    void OnRelease(PaintToolContext& ctx, const PaintPointerEvent& e) override {
        if (!dragging) return;
        dragging = false;
        x1 = e.x; y1 = e.y; constrain = e.shift;
        RasterPaint::ShapeStyle style;
        style.stroke = ctx.options->shapeStroke ? ctx.foreground() : RasterPixel(0, 0, 0, 0);
        style.fill = ctx.options->shapeFill ? ctx.background() : RasterPixel(0, 0, 0, 0);
        style.strokeWidth = ctx.options->shapeStroke ? ctx.options->shapeStrokeWidth : 0.0f;
        style.antialias = ctx.options->shapeAntialias;
        if (kind == Kind::Line) { style.stroke = ctx.foreground(); style.strokeWidth = ctx.options->shapeStrokeWidth; }
        EditActiveLayer(ctx, name, [&](UCRasterDocument&, UCRasterLayer& layer, const UCRasterSelection* sel) {
            switch (kind) {
                case Kind::Line: {
                    Point2Df b = ConstrainedEnd();
                    return RasterPaint::DrawLine(layer, sel, Point2Df(static_cast<float>(x0), static_cast<float>(y0)), b, style);
                }
                case Kind::Rectangle:
                    return RasterPaint::DrawRectangle(layer, sel, NormalizedRectF(x0, y0, x1, y1, constrain), style);
                case Kind::Ellipse:
                default:
                    return RasterPaint::DrawEllipse(layer, sel, NormalizedRectF(x0, y0, x1, y1, constrain), style);
            }
        });
        ctx.surface->Refresh();
    }
    void DrawOverlay(PaintToolContext& tc, IRenderContext* ctx, const PaintViewTransform& v) override {
        if (!dragging) return;
        ctx->SetLineDash(UCDashPattern());
        ctx->SetStrokeWidth(std::max(1.0, tc.options->shapeStrokeWidth * v.zoom));
        ctx->SetStrokePaint(tc.foreground().ToColor());
        if (kind == Kind::Line) {
            const Point2Df b = ConstrainedEnd();
            ctx->DrawLine(v.ImageToView(x0, y0), v.ImageToView(b.x, b.y));
            return;
        }
        const Rect2Df r = NormalizedRectF(x0, y0, x1, y1, constrain);
        const Rect2Dd vr = v.ImageToView(Rect2Dd(r.x, r.y, r.width, r.height));
        if (tc.options->shapeFill) {
            ctx->SetFillPaint(tc.background().ToColor());
            ctx->ClearPath();
            if (kind == Kind::Rectangle) ctx->Rect(vr.x, vr.y, vr.width, vr.height);
            else ctx->Ellipse(vr.x + vr.width / 2, vr.y + vr.height / 2, vr.width / 2, vr.height / 2, 0);
            ctx->Fill();
        }
        if (tc.options->shapeStroke) {
            if (kind == Kind::Rectangle) ctx->DrawRectangle(vr); else ctx->DrawEllipse(vr);
        } else {
            StrokeViewRect(ctx, vr);
        }
    }
    void BuildOptions(PaintToolContext& ctx, UltraCanvasContainer& panel, const std::function<void()>&) override {
        auto o = ctx.options;
        AddSliderRow(panel, "shape-width", "Line width", 1, 100, o->shapeStrokeWidth, 1, true, [o](float v) { o->shapeStrokeWidth = v; });
        if (kind != Kind::Line) {
            AddCheckbox(panel, "shape-stroke", "Outline (foreground)", o->shapeStroke, [o](bool v) { o->shapeStroke = v; });
            AddCheckbox(panel, "shape-fill", "Fill (background)", o->shapeFill, [o](bool v) { o->shapeFill = v; });
        }
        AddCheckbox(panel, "shape-aa", "Anti-aliased", o->shapeAntialias, [o](bool v) { o->shapeAntialias = v; });
    }
private:
    Point2Df ConstrainedEnd() const {
        if (!constrain) return Point2Df(static_cast<float>(x1), static_cast<float>(y1));
        const double dx = x1 - x0, dy = y1 - y0;
        const double len = std::sqrt(dx * dx + dy * dy);
        constexpr double kQuarterPi = 0.78539816339744830962;
        const double ang = std::round(std::atan2(dy, dx) / kQuarterPi) * kQuarterPi;
        return Point2Df(static_cast<float>(x0 + std::cos(ang) * len), static_cast<float>(y0 + std::sin(ang) * len));
    }
    Kind kind;
    bool dragging = false, constrain = false;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

// ===========================================================================
// TEXT / ZOOM / PAN
// ===========================================================================

class TextTool : public PaintTool {
public:
    TextTool() : PaintTool(PaintToolId::Text, "Text", "text.svg", 'T', "Click where the text should start") {}
    UCMouseCursor Cursor() const override { return UCMouseCursor::Text; }
    void OnPress(PaintToolContext& ctx, const PaintPointerEvent& e) override {
        if (ctx.requestText) ctx.requestText(e.x, e.y);
    }
    void BuildOptions(PaintToolContext& ctx, UltraCanvasContainer& panel, const std::function<void()>&) override {
        auto o = ctx.options;
        AddDropdown(panel, "text-font", "Font", { "Sans", "Serif", "Monospace" },
                    o->textFont == "Serif" ? 1 : o->textFont == "Monospace" ? 2 : 0,
                    [o](int i) { o->textFont = i == 1 ? "Serif" : i == 2 ? "Monospace" : "Sans"; });
        AddSliderRow(panel, "text-size", "Size", 6, 300, static_cast<float>(o->textSize), 1, true, [o](float v) { o->textSize = static_cast<int>(v); });
        AddCheckbox(panel, "text-bold", "Bold", o->textBold, [o](bool v) { o->textBold = v; });
    }
};

class ZoomTool : public PaintTool {
public:
    ZoomTool() : PaintTool(PaintToolId::Zoom, "Zoom", "zoom.svg", 'Z', "Click to zoom in; Alt or right-click to zoom out") {}
    UCMouseCursor Cursor() const override { return UCMouseCursor::LookingGlass; }
    void OnPress(PaintToolContext& ctx, const PaintPointerEvent& e) override {
        const bool out = e.alt || e.button == UCMouseButton::Right;
        ctx.surface->SetZoomAt(ctx.surface->GetZoom() * (out ? 0.5 : 2.0), e.view);
    }
};

class PanTool : public PaintTool {
public:
    PanTool() : PaintTool(PaintToolId::Pan, "Pan", "pan.svg", 'H', "Drag to scroll the view (Space+drag works with any tool)") {}
    UCMouseCursor Cursor() const override { return UCMouseCursor::Hand; }
    void Activate(PaintToolContext& ctx) override { ctx.surface->SetPanMode(true); }
    void Deactivate(PaintToolContext& ctx) override { ctx.surface->SetPanMode(false); }
};

} // namespace

// ===========================================================================
// THE PALETTE
// ===========================================================================

std::vector<std::unique_ptr<PaintTool>> CreatePaintTools() {
    std::vector<std::unique_ptr<PaintTool>> tools;
    tools.push_back(std::make_unique<MoveTool>());
    tools.push_back(std::make_unique<MarqueeTool>(PaintToolId::RectSelect, "Rectangle Select", "select-rect.svg", 'R', false));
    tools.push_back(std::make_unique<MarqueeTool>(PaintToolId::EllipseSelect, "Ellipse Select", "select-ellipse.svg", 'E', true));
    tools.push_back(std::make_unique<LassoTool>());
    tools.push_back(std::make_unique<MagicWandTool>());
    tools.push_back(std::make_unique<CropTool>());
    tools.push_back(std::make_unique<EyedropperTool>());
    tools.push_back(std::make_unique<BrushTool>(PaintToolId::Pencil, "Pencil", "pencil.svg", 'N',
                    "Hard-edged pixel drawing", BrushMode::Paint, &PaintToolOptions::pencil, false, false));
    tools.push_back(std::make_unique<BrushTool>(PaintToolId::Brush, "Paintbrush", "brush.svg", 'B',
                    "Paint with the foreground colour (right button: background)", BrushMode::Paint, &PaintToolOptions::brush, true, true));
    tools.push_back(std::make_unique<BrushTool>(PaintToolId::Airbrush, "Airbrush", "airbrush.svg", 'A',
                    "Soft, low-flow spray", BrushMode::Paint, &PaintToolOptions::airbrush, true, true));
    tools.push_back(std::make_unique<BrushTool>(PaintToolId::Eraser, "Eraser", "eraser.svg", 'X',
                    "Erase to transparency", BrushMode::Erase, &PaintToolOptions::eraser, true, true));
    tools.push_back(std::make_unique<BrushTool>(PaintToolId::Clone, "Clone Stamp", "clone.svg", 'S',
                    "Ctrl+click the source, then paint", BrushMode::Clone, &PaintToolOptions::clone, true, true));
    tools.push_back(std::make_unique<BrushTool>(PaintToolId::Smudge, "Smudge", "smudge.svg", 'U',
                    "Drag colour along the stroke", BrushMode::Smudge, &PaintToolOptions::smudge, true, true));
    tools.push_back(std::make_unique<BrushTool>(PaintToolId::Dodge, "Dodge", "dodge.svg", 'O',
                    "Lighten", BrushMode::Dodge, &PaintToolOptions::dodgeBurn, true, true));
    tools.push_back(std::make_unique<BrushTool>(PaintToolId::Burn, "Burn", "burn.svg", 'K',
                    "Darken", BrushMode::Burn, &PaintToolOptions::dodgeBurn, true, true));
    tools.push_back(std::make_unique<FillTool>());
    tools.push_back(std::make_unique<GradientTool>());
    tools.push_back(std::make_unique<ShapeTool>(PaintToolId::Line, "Line", "line.svg", 'D', ShapeTool::Kind::Line));
    tools.push_back(std::make_unique<ShapeTool>(PaintToolId::Rectangle, "Rectangle", "rect.svg", 'Q', ShapeTool::Kind::Rectangle));
    tools.push_back(std::make_unique<ShapeTool>(PaintToolId::Ellipse, "Ellipse", "ellipse.svg", 'P', ShapeTool::Kind::Ellipse));
    tools.push_back(std::make_unique<TextTool>());
    tools.push_back(std::make_unique<ZoomTool>());
    tools.push_back(std::make_unique<PanTool>());
    return tools;
}

} // namespace UltraCanvas
