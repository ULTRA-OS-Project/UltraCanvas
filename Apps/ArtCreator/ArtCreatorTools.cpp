// Apps/ArtCreator/ArtCreatorTools.cpp
// ArtCreator tool implementations: selector, shape editor, pen, freehand,
// line, rectangle, ellipse, quick shape, text, fill, transparency, shadow,
// feather, zoom, push - the line gallery's named choices, and the option
// widget helpers they share with the window.
// Version: 1.1.0
// Last Modified: 2026-09-18
// Author: UltraCanvas Framework

#include "ArtCreatorTools.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasSlider.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace UltraCanvas {

using namespace VectorStorage;
using namespace VectorEdit;

// ===========================================================================
// OPTION WIDGET HELPERS
// ===========================================================================

namespace ArtOptionWidgets {

namespace {
    constexpr float kRowH = 24.0f;
    constexpr float kLabelW = 70.0f;

    std::shared_ptr<UltraCanvasContainer> MakeRow(const std::string& id) {
        auto row = std::make_shared<UltraCanvasContainer>(id, 0, 0, 0, kRowH);
        row->layout.SetFlexRow().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Center);
        row->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        return row;
    }
}

std::string FormatValue(float v, bool integer) {
    char buf[32];
    if (integer) std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(v)));
    else std::snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

void AddSliderRow(UltraCanvasContainer& panel, const std::string& id, const std::string& label,
                  float minVal, float maxVal, float value, float step, bool integer,
                  const std::function<void(float)>& onChange, float labelWidth) {
    auto row = MakeRow(id + "-row");
    auto lbl = CreateLabel(id + "-label", 0, 0, labelWidth > 0 ? labelWidth : kLabelW, kRowH, label);
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

void AddButtonRow(UltraCanvasContainer& panel, const std::string& id,
                  const std::vector<std::pair<std::string, std::function<void()>>>& buttons) {
    auto row = MakeRow(id + "-row");
    int i = 0;
    for (const auto& b : buttons) {
        auto btn = std::make_shared<UltraCanvasButton>(id + "-" + std::to_string(i++), 0, 0, 0, kRowH, b.first);
        ButtonStyle st = btn->GetStyle();
        st.fontSize = 11.0f;
        btn->SetStyle(st);
        btn->onClick = b.second;
        btn->layoutItem.SetFlexGrow(1).SetFlexShrink(1);
        row->AddChild(btn);
    }
    panel.AddChild(row);
}

} // namespace ArtOptionWidgets

// ===========================================================================
// SHARED HELPERS
// ===========================================================================

namespace ArtToolHelpers {

void ApplyNewShapeStyle(VectorElement& element, const ArtToolContext& ctx) {
    const ArtToolOptions& o = *ctx.options;
    if (o.shapeFill && ctx.fillColor) element.Style.Fill = ctx.fillColor();
    else element.Style.Fill.reset();
    if (o.shapeStroke && o.strokeWidth > 0 && ctx.lineColor) {
        StrokeData st;
        st.Fill = ctx.lineColor();
        st.Width = o.strokeWidth;
        st.LineJoin = StrokeLineJoin::Round;
        st.LineCap = StrokeLineCap::Round;
        ArtLineGallery::ApplyToStroke(st, o);
        element.Style.Stroke = st;
    } else {
        element.Style.Stroke.reset();
    }
}

void AddNewElement(ArtToolContext& ctx, const std::string& label, const std::shared_ptr<VectorElement>& element) {
    if (!element || !ctx.history || !ctx.activeLayer) return;
    if (element->Id.empty()) element->Id = GenerateId();
    const std::string id = element->Id;
    ctx.history->Record(label, [&]() {
        if (auto layer = ctx.activeLayer()) layer->AddChild(element);
    });
    // The history's snapshot replaced the objects: select the live one.
    if (ctx.selection && ctx.getDocument) {
        if (auto doc = ctx.getDocument())
            if (auto fresh = doc->FindElementById(id)) ctx.selection->Set(fresh);
    }
}

Rect2Dd DragRect(const Point2Dd& from, const Point2Dd& to, bool square, bool fromCentre) {
    double w = to.x - from.x, h = to.y - from.y;
    if (square) {
        const double s = std::max(std::fabs(w), std::fabs(h));
        w = w < 0 ? -s : s;
        h = h < 0 ? -s : s;
    }
    if (fromCentre) return Rect2Dd(from.x - std::fabs(w), from.y - std::fabs(h), 2 * std::fabs(w), 2 * std::fabs(h));
    return Rect2Dd(std::min(from.x, from.x + w), std::min(from.y, from.y + h), std::fabs(w), std::fabs(h));
}

} // namespace ArtToolHelpers

// ===========================================================================
// LINE GALLERY
// ===========================================================================
namespace ArtLineGallery {

const std::vector<std::string>& ArrowheadNames() {
    static const std::vector<std::string> names = { "None", "Triangle", "Open arrow", "Circle", "Square", "Diamond", "Bar",
                                                    "Xara straight", "Xara angled", "Xara rounded", "Xara spot", "Xara diamond",
                                                    "Xara feather", "Xara feather 2", "Xara hollow diamond" };
    return names;
}
const std::vector<std::string>& ProfileNames() {
    static const std::vector<std::string> names = { "Constant", "Taper to end", "Taper from start", "Taper both ends", "Bulge" };
    return names;
}
const std::vector<std::string>& BrushNames() {
    static const std::vector<std::string> names = { "None", "Dots", "Dashes", "Hearts" };
    return names;
}

std::vector<WidthSample> Profile(int index) {
    switch (index) {
        case 1: return {{0.0f, 1.0f}, {1.0f, 0.0f}};
        case 2: return {{0.0f, 0.0f}, {1.0f, 1.0f}};
        case 3: return {{0.0f, 0.0f}, {0.5f, 1.0f}, {1.0f, 0.0f}};
        case 4: return {{0.0f, 0.35f}, {0.5f, 1.0f}, {1.0f, 0.35f}};
        default: return {};
    }
}

std::optional<BrushData> Brush(int index) {
    if (index <= 0) return std::nullopt;
    BrushData b;
    b.Stamp = std::make_shared<VectorGroup>();
    b.Stamp->Id = "brush-stamp";
    switch (index) {
        case 1: {   // dots
            auto dot = std::make_shared<VectorCircle>();
            dot->Center = Point2Dd(0, 0);
            dot->Radius = 5;
            dot->Style.Fill = Color(0, 0, 0, 255);
            b.Stamp->AddChild(dot);
            b.Spacing = 1.6f;
            break;
        }
        case 2: {   // dashes
            auto dash = std::make_shared<VectorRect>();
            dash->Bounds = Rect2Dd(0, 0, 14, 5);
            dash->RadiusX = dash->RadiusY = 2.5f;
            dash->Style.Fill = Color(0, 0, 0, 255);
            b.Stamp->AddChild(dash);
            b.Spacing = 1.5f;
            break;
        }
        default: {  // hearts
            auto heart = std::make_shared<VectorPath>();
            heart->MoveTo(0, 6);
            heart->CurveTo(-8, -2, -8, -10, -3, -10);
            heart->CurveTo(-1, -10, 0, -8, 0, -7);
            heart->CurveTo(0, -8, 1, -10, 3, -10);
            heart->CurveTo(8, -10, 8, -2, 0, 6);
            heart->ClosePath();
            heart->Style.Fill = Color(0, 0, 0, 255);
            b.Stamp->AddChild(heart);
            b.Spacing = 1.3f;
            break;
        }
    }
    return b;
}

void ApplyToStroke(StrokeData& stroke, const ArtToolOptions& o) {
    stroke.StartArrow.Kind = static_cast<ArrowheadKind>(std::clamp(o.lineStartArrow, 0, ArrowheadKindCount - 1));
    stroke.StartArrow.Scale = o.lineArrowScale;
    stroke.EndArrow.Kind = static_cast<ArrowheadKind>(std::clamp(o.lineEndArrow, 0, ArrowheadKindCount - 1));
    stroke.EndArrow.Scale = o.lineArrowScale;
    stroke.WidthProfile = Profile(o.lineProfile);
    stroke.Brush = Brush(o.lineBrush);
    // The brush stamps take the line's colour.
    if (stroke.Brush && stroke.Brush->Stamp) {
        if (auto* c = std::get_if<Color>(&stroke.Fill))
            for (auto& child : stroke.Brush->Stamp->Children) if (child) child->Style.Fill = *c;
    }
}

void ReadFromStroke(const StrokeData& stroke, ArtToolOptions& o) {
    o.lineStartArrow = static_cast<int>(stroke.StartArrow.Kind);
    o.lineEndArrow = static_cast<int>(stroke.EndArrow.Kind);
    o.lineArrowScale = stroke.StartArrow.IsSet() ? stroke.StartArrow.Scale : stroke.EndArrow.IsSet() ? stroke.EndArrow.Scale : o.lineArrowScale;
    o.lineProfile = 0;
    for (int i = 1; i <= 4; ++i) {
        const auto p = Profile(i);
        if (p.size() != stroke.WidthProfile.size()) continue;
        bool same = true;
        for (size_t k = 0; k < p.size(); ++k)
            if (std::fabs(p[k].T - stroke.WidthProfile[k].T) > 1e-4f || std::fabs(p[k].Factor - stroke.WidthProfile[k].Factor) > 1e-4f) same = false;
        if (same) { o.lineProfile = i; break; }
    }
    o.lineBrush = 0;
    if (stroke.HasBrush() && stroke.Brush->Stamp && !stroke.Brush->Stamp->Children.empty()) {
        const auto& first = stroke.Brush->Stamp->Children.front();
        o.lineBrush = first->Type == VectorElementType::Circle ? 1
                    : first->Type == VectorElementType::Rectangle || first->Type == VectorElementType::RoundedRectangle ? 2 : 3;
    }
}

} // namespace ArtLineGallery

using namespace ArtOptionWidgets;
using namespace ArtToolHelpers;

namespace {

    std::vector<Point2Dd> ToView(const VectorViewTransform& v, const std::vector<Point2Dd>& pts) {
        std::vector<Point2Dd> out;
        out.reserve(pts.size());
        for (const auto& p : pts) out.push_back(v.DocToView(p));
        return out;
    }

    void DrawViewSquare(IRenderContext* ctx, const Point2Dd& c, double size, const Color& fill, const Color& border) {
        const Rect2Dd r(c.x - size / 2, c.y - size / 2, size, size);
        ctx->SetFillPaint(fill);
        ctx->FillRectangle(r);
        ctx->SetStrokePaint(border);
        ctx->SetStrokeWidth(1.0);
        ctx->DrawRectangle(r);
    }

    // Re-finds the elements of a selection after a history snapshot.
    void ReselectByIds(ArtToolContext& ctx, const std::vector<std::string>& ids) {
        if (!ctx.selection || !ctx.getDocument) return;
        auto doc = ctx.getDocument();
        if (!doc) return;
        std::vector<ElementPtr> fresh;
        for (const auto& id : ids) if (auto e = doc->FindElementById(id)) fresh.push_back(e);
        ctx.selection->Set(fresh);
    }

// ===========================================================================
// SELECTOR
// ===========================================================================

class SelectorTool : public ArtTool {
public:
    SelectorTool() : ArtTool(ArtToolId::Selector, "Selector", "selector.svg", 'V',
                             "Click to select, drag to move, drag a handle to scale; click again for rotate / skew") {}

    UCMouseCursor Cursor() const override { return UCMouseCursor::Arrow; }

    void OnPress(ArtToolContext& ctx, const VectorPointerEvent& e) override {
        pressDoc = e.snapped;
        lastDoc = e.snapped;
        dragged = false;
        auto* canvas = ctx.canvas;
        const VectorHandle h = canvas->HitTestHandle(e.view);
        if (h != VectorHandle::NoHandle && h != VectorHandle::Body) {
            activeHandle = h;
            ctx.history->BeginEdit(canvas->GetHandleMode() == VectorHandleMode::Rotate
                                   ? (h == VectorHandle::Center ? "Move Centre" : "Rotate") : "Scale");
            return;
        }
        auto hit = canvas->HitTest(e.doc, 4.0);
        if (hit) {
            ElementPtr target = (e.ctrl && hit->element) ? hit->element : (hit->topLevel ? hit->topLevel : hit->element);
            if (e.shift) ctx.selection->Toggle(target);
            else if (!ctx.selection->Contains(target)) ctx.selection->Set(target);
            else if (h == VectorHandle::Body) toggleModePending = true;
            moving = !ctx.selection->Empty();
            if (moving) ctx.history->BeginEdit("Move");
        } else {
            if (!e.shift) ctx.selection->Clear();
            canvas->SetHandleMode(VectorHandleMode::Scale);
            marquee = true;
        }
    }

    void OnDrag(ArtToolContext& ctx, const VectorPointerEvent& e) override {
        dragged = true;
        if (activeHandle != VectorHandle::NoHandle) { DragHandle(ctx, e); return; }
        if (moving) {
            double dx = e.snapped.x - lastDoc.x, dy = e.snapped.y - lastDoc.y;
            if (e.ctrl) { if (std::fabs(e.snapped.x - pressDoc.x) > std::fabs(e.snapped.y - pressDoc.y)) dy = pressDoc.y - lastDoc.y; else dx = pressDoc.x - lastDoc.x; }
            TranslateElements(ctx.selection->Elements(), dx, dy);
            lastDoc = Point2Dd(lastDoc.x + dx, lastDoc.y + dy);
            ctx.canvas->Refresh();
        } else if (marquee) {
            currentDoc = e.doc;
            ctx.canvas->Refresh();
        }
    }

    void OnRelease(ArtToolContext& ctx, const VectorPointerEvent& e) override {
        auto* canvas = ctx.canvas;
        if (activeHandle != VectorHandle::NoHandle) {
            activeHandle = VectorHandle::NoHandle;
            if (dragged) ctx.history->EndEdit(); else ctx.history->CancelEdit();
        } else if (moving) {
            moving = false;
            if (dragged) ctx.history->EndEdit();
            else {
                ctx.history->CancelEdit();
                if (toggleModePending)
                    canvas->SetHandleMode(canvas->GetHandleMode() == VectorHandleMode::Scale
                                          ? VectorHandleMode::Rotate : VectorHandleMode::Scale);
            }
            toggleModePending = false;
        } else if (marquee) {
            marquee = false;
            if (dragged) {
                const Rect2Dd r = DragRect(pressDoc, e.doc, false, false);
                auto found = canvas->ElementsIn(r, true);
                if (e.shift) for (auto& el : found) ctx.selection->Add(el);
                else ctx.selection->Set(found);
            }
            canvas->Refresh();
        }
    }

    void OnHover(ArtToolContext& ctx, const VectorPointerEvent& e) override {
        const VectorHandle h = ctx.canvas->HitTestHandle(e.view);
        UCMouseCursor c = UCMouseCursor::Arrow;
        const bool rotate = ctx.canvas->GetHandleMode() == VectorHandleMode::Rotate;
        switch (h) {
            case VectorHandle::TopLeft: case VectorHandle::BottomRight: c = rotate ? UCMouseCursor::Cross : UCMouseCursor::SizeNWSE; break;
            case VectorHandle::TopRight: case VectorHandle::BottomLeft: c = rotate ? UCMouseCursor::Cross : UCMouseCursor::SizeNESW; break;
            case VectorHandle::Top: case VectorHandle::Bottom: c = rotate ? UCMouseCursor::SizeWE : UCMouseCursor::SizeNS; break;
            case VectorHandle::Left: case VectorHandle::Right: c = rotate ? UCMouseCursor::SizeNS : UCMouseCursor::SizeWE; break;
            case VectorHandle::Center: c = UCMouseCursor::SizeAll; break;
            case VectorHandle::Body: c = UCMouseCursor::SizeAll; break;
            default: break;
        }
        ctx.canvas->SetToolCursor(c);
    }

    void OnDoubleClick(ArtToolContext& ctx, const VectorPointerEvent& e) override {
        auto hit = ctx.canvas->HitTest(e.doc, 4.0);
        if (!hit || !hit->element) return;
        if (hit->element->Type == VectorElementType::Text) return;
        ctx.selection->Set(hit->element);
        if (ctx.selectTool) ctx.selectTool(ArtToolId::ShapeEditor);
    }

    bool OnKey(ArtToolContext& ctx, const UCEvent& k) override {
        if (k.virtualKey == UCKeys::Escape) { ctx.selection->Clear(); return true; }
        const double step = k.shift ? 10.0 : 1.0;
        double dx = 0, dy = 0;
        if (k.virtualKey == UCKeys::Left) dx = -step; else if (k.virtualKey == UCKeys::Right) dx = step;
        else if (k.virtualKey == UCKeys::Up) dy = -step; else if (k.virtualKey == UCKeys::Down) dy = step;
        if ((dx != 0 || dy != 0) && !ctx.selection->Empty()) {
            const double s = ctx.canvas->PixelsToDoc(1.0);
            ctx.history->Record("Nudge", [&]() { TranslateElements(ctx.selection->Elements(), dx * s, dy * s); }, true);
            return true;
        }
        return false;
    }

    void DrawOverlay(ArtToolContext&, IRenderContext* ctx, const VectorViewTransform& v) override {
        if (!marquee || !dragged) return;
        const Rect2Dd r = v.DocToView(DragRect(pressDoc, currentDoc, false, false));
        ctx->SetFillPaint(Color(30, 100, 220, 30));
        ctx->FillRectangle(r);
        ctx->SetStrokePaint(Color(30, 100, 220, 200));
        ctx->SetStrokeWidth(1.0);
        ctx->SetLineDash(UCDashPattern({4.0, 3.0}, 0.0));
        ctx->DrawRectangle(r);
        ctx->SetLineDash(UCDashPattern());
    }

    void BuildOptions(ArtToolContext& ctx, UltraCanvasContainer& panel, const std::function<void()>&) override {
        AddCaption(panel, "ac-sel-cap", "Selection");
        AddButtonRow(panel, "ac-sel-mode", {
            {"Scale handles", [&ctx]() { ctx.canvas->SetHandleMode(VectorHandleMode::Scale); }},
            {"Rotate / skew", [&ctx]() { ctx.canvas->SetHandleMode(VectorHandleMode::Rotate); }},
        });
        AddCaption(panel, "ac-sel-cap2", "Arrange");
        AddButtonRow(panel, "ac-sel-z", {
            {"To front", [&ctx]() { if (!ctx.selection->Empty()) ctx.history->Record("To Front", [&]() { ReorderElements(ctx.selection->Elements(), ZOrderMove::ToFront); }); }},
            {"Forward", [&ctx]() { if (!ctx.selection->Empty()) ctx.history->Record("Forward", [&]() { ReorderElements(ctx.selection->Elements(), ZOrderMove::Forward); }); }},
        });
        AddButtonRow(panel, "ac-sel-z2", {
            {"Backward", [&ctx]() { if (!ctx.selection->Empty()) ctx.history->Record("Backward", [&]() { ReorderElements(ctx.selection->Elements(), ZOrderMove::Backward); }); }},
            {"To back", [&ctx]() { if (!ctx.selection->Empty()) ctx.history->Record("To Back", [&]() { ReorderElements(ctx.selection->Elements(), ZOrderMove::ToBack); }); }},
        });
        AddCaption(panel, "ac-sel-cap3", "Align to selection");
        AddButtonRow(panel, "ac-sel-al", {
            {"Left", [&ctx]() { Align(ctx, AlignMode::Left); }},
            {"Centre", [&ctx]() { Align(ctx, AlignMode::HorizontalCenter); }},
            {"Right", [&ctx]() { Align(ctx, AlignMode::Right); }},
        });
        AddButtonRow(panel, "ac-sel-al2", {
            {"Top", [&ctx]() { Align(ctx, AlignMode::Top); }},
            {"Middle", [&ctx]() { Align(ctx, AlignMode::VerticalCenter); }},
            {"Bottom", [&ctx]() { Align(ctx, AlignMode::Bottom); }},
        });
    }

private:
    static void Align(ArtToolContext& ctx, AlignMode mode) {
        if (ctx.selection->Count() < 2) return;
        ctx.history->Record("Align", [&]() { AlignElements(ctx.selection->Elements(), mode); });
    }

    void DragHandle(ArtToolContext& ctx, const VectorPointerEvent& e) {
        auto* canvas = ctx.canvas;
        const Rect2Dd cur = canvas->SelectionBounds();
        if (cur.width <= 0 || cur.height <= 0) return;
        if (canvas->GetHandleMode() == VectorHandleMode::Rotate) {
            if (activeHandle == VectorHandle::Center) { canvas->SetRotationCenter(e.snapped); return; }
            const Point2Dd c = canvas->GetRotationCenter();
            const bool corner = activeHandle == VectorHandle::TopLeft || activeHandle == VectorHandle::TopRight ||
                                activeHandle == VectorHandle::BottomLeft || activeHandle == VectorHandle::BottomRight;
            if (corner) {
                const double a0 = std::atan2(lastDoc.y - c.y, lastDoc.x - c.x);
                double a1 = std::atan2(e.doc.y - c.y, e.doc.x - c.x);
                if (e.ctrl) {   // constrain to 15 degree steps of the total angle
                    const double total = std::atan2(e.doc.y - c.y, e.doc.x - c.x) - std::atan2(pressDoc.y - c.y, pressDoc.x - c.x);
                    const double stepped = std::round(total / (M_PI / 12)) * (M_PI / 12);
                    a1 = a0 + (stepped - accumulated);
                    accumulated = stepped;
                }
                RotateElements(ctx.selection->Elements(), a1 - a0, c);
            } else {
                // Edge handles skew: top / bottom shear in x, left / right in y.
                const bool horizontal = activeHandle == VectorHandle::Top || activeHandle == VectorHandle::Bottom;
                const double delta = horizontal ? (e.doc.x - lastDoc.x) : (e.doc.y - lastDoc.y);
                const double extent = horizontal ? cur.height : cur.width;
                if (extent > 1e-6) {
                    const double angle = std::atan(delta / extent) * ((activeHandle == VectorHandle::Top || activeHandle == VectorHandle::Left) ? -1 : 1);
                    SkewElements(ctx.selection->Elements(), horizontal ? angle : 0, horizontal ? 0 : angle, c);
                }
            }
            lastDoc = e.doc;
            canvas->Refresh();
            return;
        }
        double sx = 1, sy = 1;
        Point2Dd pivot(cur.x, cur.y);
        const bool left = activeHandle == VectorHandle::TopLeft || activeHandle == VectorHandle::Left || activeHandle == VectorHandle::BottomLeft;
        const bool right = activeHandle == VectorHandle::TopRight || activeHandle == VectorHandle::Right || activeHandle == VectorHandle::BottomRight;
        const bool top = activeHandle == VectorHandle::TopLeft || activeHandle == VectorHandle::Top || activeHandle == VectorHandle::TopRight;
        const bool bottom = activeHandle == VectorHandle::BottomLeft || activeHandle == VectorHandle::Bottom || activeHandle == VectorHandle::BottomRight;
        if (right) { pivot.x = cur.x; sx = (e.snapped.x - cur.x) / cur.width; }
        if (left) { pivot.x = cur.x + cur.width; sx = (pivot.x - e.snapped.x) / cur.width; }
        if (bottom) { pivot.y = cur.y; sy = (e.snapped.y - cur.y) / cur.height; }
        if (top) { pivot.y = cur.y + cur.height; sy = (pivot.y - e.snapped.y) / cur.height; }
        if (e.shift && (left || right) && (top || bottom)) sx = sy = std::max(sx, sy);
        if (e.ctrl) {   // about the centre
            pivot = Point2Dd(cur.x + cur.width / 2, cur.y + cur.height / 2);
            if (left || right) sx = 2 * sx - 1;
            if (top || bottom) sy = 2 * sy - 1;
        }
        if (std::fabs(sx) < 0.01 || std::fabs(sy) < 0.01 || !std::isfinite(sx) || !std::isfinite(sy)) return;
        ScaleElements(ctx.selection->Elements(), sx, sy, pivot);
        canvas->Refresh();
    }

    bool moving = false, marquee = false, dragged = false, toggleModePending = false;
    VectorHandle activeHandle = VectorHandle::NoHandle;
    double accumulated = 0;
    Point2Dd pressDoc, lastDoc, currentDoc;
};

// ===========================================================================
// SHAPE EDITOR (nodes and handles)
// ===========================================================================

class ShapeEditorTool : public ArtTool {
public:
    ShapeEditorTool() : ArtTool(ArtToolId::ShapeEditor, "Shape Editor", "shape-editor.svg", 'F',
                                "Drag nodes and handles; click a line to select it, double-click it to add a node") {}

    void Activate(ArtToolContext& ctx) override { Load(ctx); }
    void Deactivate(ArtToolContext&) override { path = nullptr; bezier = UltraCanvasBezierPath(); selectedNodes.clear(); }
    void OnSelectionChanged(ArtToolContext& ctx) override { Load(ctx); }

    void OnPress(ArtToolContext& ctx, const VectorPointerEvent& e) override {
        dragged = false;
        dragMode = DragMode::NoDrag;
        if (!path) {
            // Pick a shape to edit.
            auto hit = ctx.canvas->HitTest(e.doc, 4.0);
            if (hit && hit->element && hit->element->Type != VectorElementType::Text && hit->element->Type != VectorElementType::Image)
                ctx.selection->Set(hit->element);
            Load(ctx);
            return;
        }
        const double tol = ctx.canvas->PixelsToDoc(6.0);
        const Point2Dd local = path->GlobalToLocal(e.doc);
        lastLocal = local;
        // Handles of the selected nodes first.
        if (selectedNodes.size() == 1) {
            const auto [si, ni] = selectedNodes.front();
            const BezierNode& n = bezier.subpaths[si].nodes[ni];
            if (n.handleOutActive && std::hypot(n.handleOut.x - local.x, n.handleOut.y - local.y) <= tol) { dragMode = DragMode::HandleOut; Begin(ctx, "Move Handle"); return; }
            if (n.handleInActive && std::hypot(n.handleIn.x - local.x, n.handleIn.y - local.y) <= tol) { dragMode = DragMode::HandleIn; Begin(ctx, "Move Handle"); return; }
        }
        if (auto node = bezier.HitTestNode(local, tol)) {
            if (e.shift) {
                auto it = std::find(selectedNodes.begin(), selectedNodes.end(), *node);
                if (it == selectedNodes.end()) selectedNodes.push_back(*node); else selectedNodes.erase(it);
            } else if (std::find(selectedNodes.begin(), selectedNodes.end(), *node) == selectedNodes.end()) {
                selectedNodes = {*node};
            }
            dragMode = DragMode::Nodes;
            Begin(ctx, "Move Node");
            ctx.canvas->Refresh();
            return;
        }
        if (auto seg = bezier.HitTestOutline(local, tol)) {
            selectedSegment = {seg->subpath, seg->segment};
            segmentT = seg->t;
            selectedNodes.clear();
            dragMode = DragMode::Segment;
            Begin(ctx, "Drag Line");
            ctx.canvas->Refresh();
            return;
        }
        // Elsewhere: another shape, or nothing.
        auto hit = ctx.canvas->HitTest(e.doc, 4.0);
        if (hit && hit->element && hit->element != path) { ctx.selection->Set(hit->element); Load(ctx); }
        else { selectedNodes.clear(); selectedSegment = {-1, -1}; ctx.canvas->Refresh(); }
    }

    void OnDrag(ArtToolContext& ctx, const VectorPointerEvent& e) override {
        if (!path || dragMode == DragMode::NoDrag) return;
        dragged = true;
        const Point2Dd local = path->GlobalToLocal(e.snapped);
        const Point2Dd delta(local.x - lastLocal.x, local.y - lastLocal.y);
        switch (dragMode) {
            case DragMode::Nodes:
                for (const auto& [si, ni] : selectedNodes) {
                    BezierNode& n = bezier.subpaths[si].nodes[ni];
                    bezier.subpaths[si].MoveAnchor(ni, Point2Dd(n.anchor.x + delta.x, n.anchor.y + delta.y));
                }
                break;
            case DragMode::HandleIn:
            case DragMode::HandleOut: {
                const auto [si, ni] = selectedNodes.front();
                bezier.subpaths[si].MoveHandle(ni, dragMode == DragMode::HandleOut, local);
                break;
            }
            case DragMode::Segment:
                if (selectedSegment.first >= 0)
                    bezier.subpaths[selectedSegment.first].DragSegment(selectedSegment.second, segmentT, delta);
                break;
            default: break;
        }
        lastLocal = local;
        Store();
        ctx.canvas->Refresh();
    }

    void OnRelease(ArtToolContext& ctx, const VectorPointerEvent&) override {
        if (!path || dragMode == DragMode::NoDrag) return;
        dragMode = DragMode::NoDrag;
        if (dragged) End(ctx); else ctx.history->CancelEdit();
    }

    void OnDoubleClick(ArtToolContext& ctx, const VectorPointerEvent& e) override {
        if (!path) return;
        const double tol = ctx.canvas->PixelsToDoc(6.0);
        const Point2Dd local = path->GlobalToLocal(e.doc);
        if (bezier.HitTestNode(local, tol)) return;
        if (auto seg = bezier.HitTestOutline(local, tol)) {
            Begin(ctx, "Add Node");
            const int idx = bezier.subpaths[seg->subpath].InsertNodeAt(seg->segment, seg->t);
            selectedNodes = {{seg->subpath, idx}};
            Store();
            End(ctx);
        }
    }

    bool OnKey(ArtToolContext& ctx, const UCEvent& k) override {
        if (!path) return false;
        if ((k.virtualKey == UCKeys::Delete || k.virtualKey == UCKeys::Backspace) && !selectedNodes.empty()) {
            DeleteSelectedNodes(ctx);
            return true;
        }
        if (k.virtualKey == UCKeys::Escape) { selectedNodes.clear(); ctx.canvas->Refresh(); return true; }
        return false;
    }

    void DrawOverlay(ArtToolContext&, IRenderContext* ctx, const VectorViewTransform& v) override {
        if (!path) return;
        const Color nodeFill(255, 255, 255, 255), nodeSel(30, 100, 220, 255), handleCol(120, 120, 130, 255);
        ctx->SetStrokeWidth(1.0);
        ctx->SetLineDash(UCDashPattern());
        for (size_t si = 0; si < bezier.subpaths.size(); ++si) {
            const auto& sp = bezier.subpaths[si];
            // Handles of the selected nodes.
            for (const auto& [ssi, ni] : selectedNodes) {
                if (static_cast<size_t>(ssi) != si || ni < 0 || ni >= static_cast<int>(sp.nodes.size())) continue;
                const BezierNode& n = sp.nodes[ni];
                const Point2Dd a = v.DocToView(path->LocalToGlobal(n.anchor));
                ctx->SetStrokePaint(handleCol);
                if (n.handleInActive) {
                    const Point2Dd h = v.DocToView(path->LocalToGlobal(n.handleIn));
                    ctx->DrawLine(a, h);
                    ctx->SetFillPaint(nodeFill); ctx->FillCircle(h, 3.5); ctx->DrawCircle(h, 3.5);
                }
                if (n.handleOutActive) {
                    const Point2Dd h = v.DocToView(path->LocalToGlobal(n.handleOut));
                    ctx->DrawLine(a, h);
                    ctx->SetFillPaint(nodeFill); ctx->FillCircle(h, 3.5); ctx->DrawCircle(h, 3.5);
                }
            }
            for (size_t ni = 0; ni < sp.nodes.size(); ++ni) {
                const bool sel = std::find(selectedNodes.begin(), selectedNodes.end(),
                                           std::make_pair(static_cast<int>(si), static_cast<int>(ni))) != selectedNodes.end();
                const Point2Dd a = v.DocToView(path->LocalToGlobal(sp.nodes[ni].anchor));
                DrawViewSquare(ctx, a, sel ? 8.0 : 6.0, sel ? nodeSel : nodeFill, nodeSel);
            }
        }
    }

    void BuildOptions(ArtToolContext& ctx, UltraCanvasContainer& panel, const std::function<void()>&) override {
        AddCaption(panel, "ac-se-cap", "Selected nodes");
        AddButtonRow(panel, "ac-se-type", {
            {"Corner", [this, &ctx]() { SetType(ctx, BezierNodeType::Corner); }},
            {"Smooth", [this, &ctx]() { SetType(ctx, BezierNodeType::Smooth); }},
            {"Symmetric", [this, &ctx]() { SetType(ctx, BezierNodeType::Symmetric); }},
        });
        AddButtonRow(panel, "ac-se-ops", {
            {"Delete node", [this, &ctx]() { DeleteSelectedNodes(ctx); }},
            {"Line ⇄ curve", [this, &ctx]() { ToggleSegmentCurve(ctx); }},
        });
        AddCaption(panel, "ac-se-cap2", "Path");
        AddButtonRow(panel, "ac-se-path", {
            {"Close / open", [this, &ctx]() { ToggleClosed(ctx); }},
            {"Reverse", [this, &ctx]() { if (!path) return; Begin(ctx, "Reverse"); for (auto& s : bezier.subpaths) s.Reverse(); Store(); End(ctx); }},
        });
    }

private:
    enum class DragMode { NoDrag, Nodes, HandleIn, HandleOut, Segment };

    // The selected element as an editable path (a shape is converted once
    // the user edits it).
    void Load(ArtToolContext& ctx) {
        path = nullptr;
        bezier = UltraCanvasBezierPath();
        selectedNodes.clear();
        selectedSegment = {-1, -1};
        if (!ctx.selection || ctx.selection->Count() != 1) { if (ctx.canvas) ctx.canvas->Refresh(); return; }
        ElementPtr e = ctx.selection->First();
        if (!e) return;
        if (e->Type != VectorElementType::Path) {
            if (!OutlineOf(*e)) { if (ctx.canvas) ctx.canvas->Refresh(); return; }
            const std::string id = e->Id;
            ctx.history->Record("Convert to Editable Shape", [&]() { ConvertToPath(e); });
            auto doc = ctx.getDocument ? ctx.getDocument() : nullptr;
            e = doc ? doc->FindElementById(id) : nullptr;
            if (e) ctx.selection->Set(e);   // re-enters through OnSelectionChanged
            return;
        }
        path = std::dynamic_pointer_cast<VectorPath>(e);
        if (path) bezier = UltraCanvasBezierPath::FromPathData(path->Path);
        if (ctx.canvas) ctx.canvas->Refresh();
    }

    void Begin(ArtToolContext& ctx, const std::string& label) { ctx.history->BeginEdit(label); editing = true; }

    void Store() {
        if (!path) return;
        path->Path = bezier.ToPathData();
        path->Path.InvalidateCache();
    }

    void End(ArtToolContext& ctx) {
        if (!editing) return;
        editing = false;
        const std::string id = path ? path->Id : "";
        auto keepNodes = selectedNodes;
        ctx.history->EndEdit();
        // The snapshot replaced the element: find it again, keep the nodes.
        auto doc = ctx.getDocument ? ctx.getDocument() : nullptr;
        if (doc && !id.empty()) {
            if (auto fresh = doc->FindElementById(id)) {
                if (!ctx.selection->Contains(fresh)) ctx.selection->Set(fresh);
                path = std::dynamic_pointer_cast<VectorPath>(fresh);
                if (path) bezier = UltraCanvasBezierPath::FromPathData(path->Path);
                selectedNodes = keepNodes;
            }
        }
        ctx.canvas->Refresh();
    }

    void SetType(ArtToolContext& ctx, BezierNodeType type) {
        if (!path || selectedNodes.empty()) return;
        Begin(ctx, "Node Type");
        for (const auto& [si, ni] : selectedNodes) bezier.subpaths[si].SetNodeType(ni, type);
        Store();
        End(ctx);
    }

    void DeleteSelectedNodes(ArtToolContext& ctx) {
        if (!path || selectedNodes.empty()) return;
        Begin(ctx, "Delete Node");
        auto nodes = selectedNodes;
        std::sort(nodes.begin(), nodes.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first < b.first : a.second > b.second; });
        for (const auto& [si, ni] : nodes) bezier.subpaths[si].RemoveNode(ni);
        for (auto it = bezier.subpaths.begin(); it != bezier.subpaths.end();)
            if (it->nodes.size() < 2) it = bezier.subpaths.erase(it); else ++it;
        selectedNodes.clear();
        Store();
        End(ctx);
    }

    void ToggleSegmentCurve(ArtToolContext& ctx) {
        if (!path || selectedSegment.first < 0) return;
        auto& sp = bezier.subpaths[selectedSegment.first];
        const int seg = selectedSegment.second;
        if (seg < 0 || seg >= sp.SegmentCount()) return;
        Begin(ctx, "Line / Curve");
        BezierNode& a = sp.nodes[seg];
        BezierNode& b = sp.nodes[(seg + 1) % sp.nodes.size()];
        if (sp.SegmentIsLine(seg)) {
            a.handleOut = Point2Dd(a.anchor.x + (b.anchor.x - a.anchor.x) / 3, a.anchor.y + (b.anchor.y - a.anchor.y) / 3);
            b.handleIn = Point2Dd(b.anchor.x - (b.anchor.x - a.anchor.x) / 3, b.anchor.y - (b.anchor.y - a.anchor.y) / 3);
            a.handleOutActive = b.handleInActive = true;
        } else {
            a.handleOutActive = b.handleInActive = false;
        }
        Store();
        End(ctx);
    }

    void ToggleClosed(ArtToolContext& ctx) {
        if (!path || bezier.subpaths.empty()) return;
        Begin(ctx, "Close / Open Path");
        for (auto& s : bezier.subpaths) s.closed = !s.closed;
        Store();
        End(ctx);
    }

    std::shared_ptr<VectorPath> path;
    UltraCanvasBezierPath bezier;
    std::vector<std::pair<int, int>> selectedNodes;
    std::pair<int, int> selectedSegment{-1, -1};
    double segmentT = 0.5;
    DragMode dragMode = DragMode::NoDrag;
    bool dragged = false, editing = false;
    Point2Dd lastLocal;
};

// ===========================================================================
// PEN
// ===========================================================================

class PenTool : public ArtTool {
public:
    PenTool() : ArtTool(ArtToolId::Pen, "Pen", "pen.svg", 'P',
                        "Click for corners, drag for curves; click the first node to close, Enter or double-click to finish") {}

    void Deactivate(ArtToolContext& ctx) override { Finish(ctx, false); }

    void OnPress(ArtToolContext& ctx, const VectorPointerEvent& e) override {
        const double tol = ctx.canvas->PixelsToDoc(6.0);
        if (!drawing.nodes.empty() && std::hypot(e.doc.x - drawing.nodes.front().anchor.x, e.doc.y - drawing.nodes.front().anchor.y) <= tol && drawing.nodes.size() > 1) {
            drawing.closed = true;
            Finish(ctx, true);
            return;
        }
        BezierNode n;
        n.anchor = n.handleIn = n.handleOut = e.snapped;
        drawing.nodes.push_back(n);
        dragging = true;
        ctx.canvas->Refresh();
    }

    void OnDrag(ArtToolContext& ctx, const VectorPointerEvent& e) override {
        if (!dragging || drawing.nodes.empty()) return;
        BezierNode& n = drawing.nodes.back();
        n.type = BezierNodeType::Symmetric;
        n.handleOut = e.snapped;
        n.handleOutActive = true;
        n.handleIn = Point2Dd(2 * n.anchor.x - e.snapped.x, 2 * n.anchor.y - e.snapped.y);
        n.handleInActive = drawing.nodes.size() > 1;
        ctx.canvas->Refresh();
    }

    void OnRelease(ArtToolContext& ctx, const VectorPointerEvent&) override { dragging = false; ctx.canvas->Refresh(); }
    void OnHover(ArtToolContext& ctx, const VectorPointerEvent& e) override { hover = e.snapped; if (!drawing.nodes.empty()) ctx.canvas->Refresh(); }
    void OnDoubleClick(ArtToolContext& ctx, const VectorPointerEvent&) override { Finish(ctx, true); }

    bool OnKey(ArtToolContext& ctx, const UCEvent& k) override {
        if (k.virtualKey == UCKeys::Escape) { Finish(ctx, false); return true; }
        if (k.virtualKey == UCKeys::Enter || k.virtualKey == UCKeys::Return) { Finish(ctx, true); return true; }
        if (k.virtualKey == UCKeys::Backspace && !drawing.nodes.empty()) { drawing.nodes.pop_back(); ctx.canvas->Refresh(); return true; }
        return false;
    }

    void DrawOverlay(ArtToolContext&, IRenderContext* ctx, const VectorViewTransform& v) override {
        if (drawing.nodes.empty()) return;
        UltraCanvasBezierSubpath preview = drawing;
        if (!dragging) {
            BezierNode n; n.anchor = n.handleIn = n.handleOut = hover;
            preview.nodes.push_back(n);
        }
        ctx->PushState();
        ctx->Translate(v.originX, v.originY);
        ctx->Scale(v.zoom, v.zoom);
        ctx->ClearPath();
        preview.BuildPath(ctx);
        ctx->SetStrokePaint(Color(30, 100, 220, 220));
        ctx->SetStrokeWidth(1.5 / v.zoom);
        ctx->StrokePathPreserve();
        ctx->ClearPath();
        ctx->PopState();
        for (const auto& n : drawing.nodes) {
            const Point2Dd a = v.DocToView(n.anchor);
            DrawViewSquare(ctx, a, 6.0, Colors::White, Color(30, 100, 220, 255));
            if (n.handleOutActive) { ctx->SetStrokePaint(Color(120, 120, 130, 255)); ctx->DrawLine(a, v.DocToView(n.handleOut)); ctx->DrawLine(a, v.DocToView(n.handleIn)); }
        }
    }

    void BuildOptions(ArtToolContext& ctx, UltraCanvasContainer& panel, const std::function<void()>&) override {
        ArtToolOptions& o = *ctx.options;
        AddCheckbox(panel, "ac-pen-fill", "Fill with the fill colour", o.shapeFill, [&o](bool v) { o.shapeFill = v; });
        AddCheckbox(panel, "ac-pen-stroke", "Line with the line colour", o.shapeStroke, [&o](bool v) { o.shapeStroke = v; });
        AddSliderRow(panel, "ac-pen-width", "Line width", 0.25f, 40, o.strokeWidth, 0.25f, false, [&o](float v) { o.strokeWidth = v; });
    }

private:
    void Finish(ArtToolContext& ctx, bool keep) {
        dragging = false;
        if (keep && drawing.nodes.size() >= 2) {
            UltraCanvasBezierPath bp;
            bp.subpaths.push_back(drawing);
            auto p = std::make_shared<VectorPath>();
            p->Path = bp.ToPathData();
            ApplyNewShapeStyle(*p, ctx);
            if (!drawing.closed) p->Style.Fill.reset();
            AddNewElement(ctx, "Pen", p);
        }
        drawing = UltraCanvasBezierSubpath();
        if (ctx.canvas) ctx.canvas->Refresh();
    }

    UltraCanvasBezierSubpath drawing;
    bool dragging = false;
    Point2Dd hover;
};

// ===========================================================================
// FREEHAND
// ===========================================================================

class FreehandTool : public ArtTool {
public:
    FreehandTool() : ArtTool(ArtToolId::Freehand, "Freehand", "freehand.svg", 'N', "Draw a line by hand; it is smoothed on release") {}

    void OnPress(ArtToolContext&, const VectorPointerEvent& e) override { points.clear(); points.push_back(e.doc); }
    void OnDrag(ArtToolContext& ctx, const VectorPointerEvent& e) override { points.push_back(e.doc); ctx.canvas->Refresh(); }
    void OnRelease(ArtToolContext& ctx, const VectorPointerEvent& e) override {
        if (points.size() < 2) { points.clear(); return; }
        const double tol = ctx.canvas->PixelsToDoc(std::max(0.1f, ctx.options->freehandSmoothing));
        const bool close = std::hypot(e.doc.x - points.front().x, e.doc.y - points.front().y) <= ctx.canvas->PixelsToDoc(8.0) && points.size() > 5;
        auto bp = UltraCanvasBezierPath::FromPolyline(points, close, tol);
        points.clear();
        if (bp.Empty()) { ctx.canvas->Refresh(); return; }
        auto p = std::make_shared<VectorPath>();
        p->Path = bp.ToPathData();
        ApplyNewShapeStyle(*p, ctx);
        if (!close) p->Style.Fill.reset();
        AddNewElement(ctx, "Freehand", p);
    }
    void DrawOverlay(ArtToolContext&, IRenderContext* ctx, const VectorViewTransform& v) override {
        if (points.size() < 2) return;
        ctx->SetStrokePaint(Color(40, 40, 40, 200));
        ctx->SetStrokeWidth(1.5);
        ctx->SetLineDash(UCDashPattern());
        ctx->DrawLinePath(ToView(v, points), false);
    }
    void BuildOptions(ArtToolContext& ctx, UltraCanvasContainer& panel, const std::function<void()>&) override {
        ArtToolOptions& o = *ctx.options;
        AddSliderRow(panel, "ac-fh-smooth", "Smoothing", 0, 12, o.freehandSmoothing, 0.5f, false, [&o](float v) { o.freehandSmoothing = v; });
        AddSliderRow(panel, "ac-fh-width", "Line width", 0.25f, 40, o.strokeWidth, 0.25f, false, [&o](float v) { o.strokeWidth = v; });
        AddCheckbox(panel, "ac-fh-fill", "Fill a closed stroke", o.shapeFill, [&o](bool v) { o.shapeFill = v; });
    }
private:
    std::vector<Point2Dd> points;
};

// ===========================================================================
// LINE, RECTANGLE, ELLIPSE, QUICK SHAPE
// ===========================================================================

class DragShapeTool : public ArtTool {
public:
    using ArtTool::ArtTool;
    void OnPress(ArtToolContext&, const VectorPointerEvent& e) override { start = e.snapped; current = start; dragging = true; }
    void OnDrag(ArtToolContext& ctx, const VectorPointerEvent& e) override { current = e.snapped; shift = e.shift; ctrl = e.ctrl; ctx.canvas->Refresh(); }
    void OnRelease(ArtToolContext& ctx, const VectorPointerEvent& e) override {
        dragging = false;
        current = e.snapped; shift = e.shift; ctrl = e.ctrl;
        if (std::hypot(current.x - start.x, current.y - start.y) < ctx.canvas->PixelsToDoc(3.0)) { ctx.canvas->Refresh(); return; }
        Create(ctx);
        ctx.canvas->Refresh();
    }
    void DrawOverlay(ArtToolContext& tctx, IRenderContext* ctx, const VectorViewTransform& v) override {
        if (!dragging) return;
        ctx->SetStrokePaint(Color(30, 100, 220, 220));
        ctx->SetStrokeWidth(1.0);
        ctx->SetLineDash(UCDashPattern({4.0, 3.0}, 0.0));
        Preview(tctx, ctx, v);
        ctx->SetLineDash(UCDashPattern());
    }
    void BuildOptions(ArtToolContext& ctx, UltraCanvasContainer& panel, const std::function<void()>&) override {
        ArtToolOptions& o = *ctx.options;
        AddCheckbox(panel, "ac-shape-fill", "Fill with the fill colour", o.shapeFill, [&o](bool v) { o.shapeFill = v; });
        AddCheckbox(panel, "ac-shape-stroke", "Line with the line colour", o.shapeStroke, [&o](bool v) { o.shapeStroke = v; });
        AddSliderRow(panel, "ac-shape-width", "Line width", 0.25f, 40, o.strokeWidth, 0.25f, false, [&o](float v) { o.strokeWidth = v; });
        ExtraOptions(ctx, panel);
    }
protected:
    virtual void Create(ArtToolContext& ctx) = 0;
    virtual void Preview(ArtToolContext& tctx, IRenderContext* ctx, const VectorViewTransform& v) = 0;
    virtual void ExtraOptions(ArtToolContext&, UltraCanvasContainer&) {}
    Rect2Dd Box() const { return DragRect(start, current, shift, ctrl); }
    Point2Dd start, current;
    bool dragging = false, shift = false, ctrl = false;
};

class LineTool : public DragShapeTool {
public:
    LineTool() : DragShapeTool(ArtToolId::Line, "Straight Line", "line.svg", 'L', "Drag a straight line; shift constrains to 45°") {}
protected:
    Point2Dd EndPoint() const {
        if (!shift) return current;
        const double dx = current.x - start.x, dy = current.y - start.y;
        const double len = std::hypot(dx, dy);
        const double a = std::round(std::atan2(dy, dx) / (M_PI / 4)) * (M_PI / 4);
        return Point2Dd(start.x + len * std::cos(a), start.y + len * std::sin(a));
    }
    void Create(ArtToolContext& ctx) override {
        auto l = std::make_shared<VectorLine>();
        l->Start = start; l->End = EndPoint();
        ApplyNewShapeStyle(*l, ctx);
        l->Style.Fill.reset();
        if (!l->Style.Stroke.has_value()) { StrokeData st; st.Fill = ctx.lineColor ? ctx.lineColor() : Colors::Black; st.Width = std::max(0.5f, ctx.options->strokeWidth); l->Style.Stroke = st; }
        AddNewElement(ctx, "Line", l);
    }
    void Preview(ArtToolContext&, IRenderContext* ctx, const VectorViewTransform& v) override {
        ctx->DrawLine(v.DocToView(start), v.DocToView(EndPoint()));
    }
    void BuildOptions(ArtToolContext& ctx, UltraCanvasContainer& panel, const std::function<void()>&) override {
        ArtToolOptions& o = *ctx.options;
        AddSliderRow(panel, "ac-line-width", "Line width", 0.25f, 40, o.strokeWidth, 0.25f, false, [&o](float v) { o.strokeWidth = v; });
    }
};

class RectangleTool : public DragShapeTool {
public:
    RectangleTool() : DragShapeTool(ArtToolId::Rectangle, "Rectangle", "rectangle.svg", 'R', "Drag a rectangle; shift for a square, ctrl from the centre") {}
protected:
    void Create(ArtToolContext& ctx) override {
        auto r = std::make_shared<VectorRect>();
        r->Bounds = Box();
        r->RadiusX = r->RadiusY = ctx.options->cornerRadius;
        ApplyNewShapeStyle(*r, ctx);
        AddNewElement(ctx, "Rectangle", r);
    }
    void Preview(ArtToolContext& tctx, IRenderContext* ctx, const VectorViewTransform& v) override {
        const Rect2Dd r = v.DocToView(Box());
        if (tctx.options->cornerRadius > 0) ctx->DrawRoundedRectangle(r, tctx.options->cornerRadius * v.zoom);
        else ctx->DrawRectangle(r);
    }
    void ExtraOptions(ArtToolContext& ctx, UltraCanvasContainer& panel) override {
        ArtToolOptions& o = *ctx.options;
        AddSliderRow(panel, "ac-rect-radius", "Corner radius", 0, 100, o.cornerRadius, 1, true, [&o](float v) { o.cornerRadius = v; });
    }
};

class EllipseTool : public DragShapeTool {
public:
    EllipseTool() : DragShapeTool(ArtToolId::Ellipse, "Ellipse", "ellipse.svg", 'E', "Drag an ellipse; shift for a circle, ctrl from the centre") {}
protected:
    void Create(ArtToolContext& ctx) override {
        const Rect2Dd b = Box();
        auto e = std::make_shared<VectorEllipse>();
        e->Center = {b.x + b.width / 2, b.y + b.height / 2};
        e->RadiusX = static_cast<float>(b.width / 2);
        e->RadiusY = static_cast<float>(b.height / 2);
        ApplyNewShapeStyle(*e, ctx);
        AddNewElement(ctx, "Ellipse", e);
    }
    void Preview(ArtToolContext&, IRenderContext* ctx, const VectorViewTransform& v) override {
        ctx->DrawEllipse(v.DocToView(Box()));
    }
};

class QuickShapeTool : public DragShapeTool {
public:
    QuickShapeTool() : DragShapeTool(ArtToolId::QuickShape, "Quick Shape", "quickshape.svg", 'Q', "Drag from the centre outwards for a polygon or a star") {}
protected:
    std::vector<Point2Dd> Points(const ArtToolOptions& o) const {
        std::vector<Point2Dd> pts;
        const double r = std::hypot(current.x - start.x, current.y - start.y);
        const double a0 = shift ? -M_PI / 2 : std::atan2(current.y - start.y, current.x - start.x);
        const int n = std::max(3, o.quickShapeSides);
        const int count = o.quickShapeStar ? 2 * n : n;
        for (int i = 0; i < count; ++i) {
            const double a = a0 + i * 2 * M_PI / count;
            const double rr = (o.quickShapeStar && (i % 2 == 1)) ? r * o.quickShapeInner : r;
            pts.emplace_back(start.x + rr * std::cos(a), start.y + rr * std::sin(a));
        }
        return pts;
    }
    void Create(ArtToolContext& ctx) override {
        auto p = std::make_shared<VectorPolygon>();
        p->Points = Points(*ctx.options);
        ApplyNewShapeStyle(*p, ctx);
        AddNewElement(ctx, ctx.options->quickShapeStar ? "Star" : "Polygon", p);
    }
    void Preview(ArtToolContext& tctx, IRenderContext* ctx, const VectorViewTransform& v) override {
        ctx->DrawLinePath(ToView(v, Points(*tctx.options)), true);
    }
    void ExtraOptions(ArtToolContext& ctx, UltraCanvasContainer& panel) override {
        ArtToolOptions& o = *ctx.options;
        AddSliderRow(panel, "ac-qs-sides", "Sides", 3, 24, static_cast<float>(o.quickShapeSides), 1, true, [&o](float v) { o.quickShapeSides = static_cast<int>(v); });
        AddCheckbox(panel, "ac-qs-star", "Star", o.quickShapeStar, [&o](bool v) { o.quickShapeStar = v; });
        AddSliderRow(panel, "ac-qs-inner", "Inner radius", 0.1f, 0.95f, o.quickShapeInner, 0.05f, false, [&o](float v) { o.quickShapeInner = v; });
    }
};

// ===========================================================================
// TEXT
// ===========================================================================

class TextTool : public ArtTool {
public:
    TextTool() : ArtTool(ArtToolId::Text, "Text", "text.svg", 'T', "Click where the text should start") {}
    UCMouseCursor Cursor() const override { return UCMouseCursor::Text; }
    void OnPress(ArtToolContext& ctx, const VectorPointerEvent& e) override {
        auto hit = ctx.canvas->HitTest(e.doc, 4.0);
        if (hit && hit->element && hit->element->Type == VectorElementType::Text) { ctx.selection->Set(hit->element); }
        if (ctx.requestText) ctx.requestText(e.snapped.x, e.snapped.y);
    }
    void BuildOptions(ArtToolContext& ctx, UltraCanvasContainer& panel, const std::function<void()>&) override {
        ArtToolOptions& o = *ctx.options;
        AddDropdown(panel, "ac-text-font", "Font", { "Sans", "Serif", "Monospace" },
                    o.textFont == "Serif" ? 1 : o.textFont == "Monospace" ? 2 : 0,
                    [&o](int i) { o.textFont = i == 1 ? "Serif" : i == 2 ? "Monospace" : "Sans"; });
        AddSliderRow(panel, "ac-text-size", "Size", 6, 200, static_cast<float>(o.textSize), 1, true, [&o](float v) { o.textSize = static_cast<int>(v); });
        AddCheckbox(panel, "ac-text-bold", "Bold", o.textBold, [&o](bool v) { o.textBold = v; });
    }
};

// ===========================================================================
// FILL
// ===========================================================================

class FillTool : public ArtTool {
public:
    FillTool() : ArtTool(ArtToolId::Fill, "Fill", "fill.svg", 'G', "Drag across a shape for a gradient from the fill colour; drag its ends to adjust") {}

    void OnPress(ArtToolContext& ctx, const VectorPointerEvent& e) override {
        target = nullptr;
        const double tol = ctx.canvas->PixelsToDoc(7.0);
        // The ends of the selected shape's gradient first.
        if (auto sel = SingleSelected(ctx)) {
            Point2Dd a, b;
            if (GradientEnds(*sel, a, b)) {
                if (std::hypot(e.doc.x - b.x, e.doc.y - b.y) <= tol) { target = sel; draggingEnd = 1; ctx.history->BeginEdit("Fill"); return; }
                if (std::hypot(e.doc.x - a.x, e.doc.y - a.y) <= tol) { target = sel; draggingEnd = 0; ctx.history->BeginEdit("Fill"); return; }
            }
        }
        auto hit = ctx.canvas->HitTest(e.doc, 4.0);
        if (!hit || !hit->element) return;
        ElementPtr el = hit->element;
        if (el->Type == VectorElementType::Group || el->Type == VectorElementType::Layer) return;
        ctx.selection->Set(el);
        target = el;
        draggingEnd = -1;
        startDoc = e.snapped;
        ctx.history->BeginEdit("Fill");
        if (ctx.options->fillKind == 0) {
            if (ctx.fillColor) target->Style.Fill = ctx.fillColor();
            ctx.canvas->Refresh();
        }
    }

    void OnDrag(ArtToolContext& ctx, const VectorPointerEvent& e) override {
        if (!target) return;
        moved = true;
        if (draggingEnd >= 0) {
            Point2Dd a, b;
            if (GradientEnds(*target, a, b)) {
                if (draggingEnd == 0) a = e.snapped; else b = e.snapped;
                SetGradientEnds(*target, a, b);
            }
        } else if (ctx.options->fillKind != 0) {
            ApplyGradient(ctx, *target, startDoc, e.snapped);
        }
        ctx.canvas->Refresh();
    }

    void OnRelease(ArtToolContext& ctx, const VectorPointerEvent&) override {
        if (!target) return;
        const std::string id = target->Id;
        if (moved || ctx.options->fillKind == 0) ctx.history->EndEdit(); else ctx.history->CancelEdit();
        moved = false;
        target = nullptr;
        ReselectByIds(ctx, {id});
    }

    void DrawOverlay(ArtToolContext& tctx, IRenderContext* ctx, const VectorViewTransform& v) override {
        auto sel = target ? target : SingleSelected(tctx);
        if (!sel) return;
        Point2Dd a, b;
        if (!GradientEnds(*sel, a, b)) return;
        const Point2Dd va = v.DocToView(a), vb = v.DocToView(b);
        ctx->SetStrokePaint(Color(30, 100, 220, 220));
        ctx->SetStrokeWidth(1.0);
        ctx->SetLineDash(UCDashPattern());
        ctx->DrawLine(va, vb);
        DrawViewSquare(ctx, va, 8.0, Colors::White, Color(30, 100, 220, 255));
        ctx->SetFillPaint(Colors::White);
        ctx->FillCircle(vb, 4.5);
        ctx->DrawCircle(vb, 4.5);
    }

    void BuildOptions(ArtToolContext& ctx, UltraCanvasContainer& panel, const std::function<void()>&) override {
        ArtToolOptions& o = *ctx.options;
        AddDropdown(panel, "ac-fill-kind", "Fill type", { "Flat", "Linear", "Radial" }, o.fillKind, [&o](int i) { o.fillKind = i; });
        AddCaption(panel, "ac-fill-cap", "The gradient's stops are edited in the Fill panel.");
        AddButtonRow(panel, "ac-fill-ops", {
            {"No fill", [&ctx]() { if (ctx.selection->Empty()) return; ctx.history->Record("No Fill", [&]() { for (auto& e : ctx.selection->Elements()) e->Style.Fill.reset(); }); }},
            {"Flat fill", [&ctx]() { if (ctx.selection->Empty()) return; ctx.history->Record("Flat Fill", [&]() { for (auto& e : ctx.selection->Elements()) e->Style.Fill = ctx.fillColor ? ctx.fillColor() : Colors::Black; }); }},
        });
    }

private:
    static ElementPtr SingleSelected(ArtToolContext& ctx) {
        return ctx.selection && ctx.selection->Count() == 1 ? ctx.selection->First() : nullptr;
    }

    // The gradient's start and end in document space (linear: start / end;
    // radial: centre / a point on the radius).
    static bool GradientEnds(const VectorElement& e, Point2Dd& a, Point2Dd& b) {
        if (!e.Style.Fill.has_value()) return false;
        auto* g = std::get_if<GradientData>(&*e.Style.Fill);
        if (!g) return false;
        if (auto* l = std::get_if<LinearGradientData>(g)) {
            if (l->Units != GradientUnits::UserSpaceOnUse) return false;
            a = e.LocalToGlobal(l->Start); b = e.LocalToGlobal(l->End);
            return true;
        }
        if (auto* r = std::get_if<RadialGradientData>(g)) {
            if (r->Units != GradientUnits::UserSpaceOnUse) return false;
            a = e.LocalToGlobal(r->Center);
            b = e.LocalToGlobal(Point2Dd(r->Center.x + r->Radius, r->Center.y));
            return true;
        }
        return false;
    }

    static void SetGradientEnds(VectorElement& e, const Point2Dd& a, const Point2Dd& b) {
        auto* g = std::get_if<GradientData>(&*e.Style.Fill);
        if (!g) return;
        const Point2Dd la = e.GlobalToLocal(a), lb = e.GlobalToLocal(b);
        if (auto* l = std::get_if<LinearGradientData>(g)) { l->Start = la; l->End = lb; }
        else if (auto* r = std::get_if<RadialGradientData>(g)) {
            r->Center = la; r->FocalPoint = la;
            r->Radius = static_cast<float>(std::hypot(lb.x - la.x, lb.y - la.y));
        }
    }

    static std::vector<GradientStop> StopsFor(const ArtToolContext& ctx, const VectorElement& e) {
        if (e.Style.Fill.has_value()) {
            if (auto* g = std::get_if<GradientData>(&*e.Style.Fill)) {
                if (auto* l = std::get_if<LinearGradientData>(g)) if (l->Stops.size() >= 2) return l->Stops;
                if (auto* r = std::get_if<RadialGradientData>(g)) if (r->Stops.size() >= 2) return r->Stops;
            }
        }
        const Color c = ctx.fillColor ? ctx.fillColor() : Colors::Black;
        return {{0.0, c}, {1.0, Color(255, 255, 255, 255)}};
    }

    void ApplyGradient(ArtToolContext& ctx, VectorElement& e, const Point2Dd& from, const Point2Dd& to) {
        const auto stops = StopsFor(ctx, e);
        const Point2Dd la = e.GlobalToLocal(from), lb = e.GlobalToLocal(to);
        if (ctx.options->fillKind == 2) {
            RadialGradientData r;
            r.Units = GradientUnits::UserSpaceOnUse;
            r.Center = r.FocalPoint = la;
            r.Radius = static_cast<float>(std::max(1.0, std::hypot(lb.x - la.x, lb.y - la.y)));
            r.Stops = stops;
            e.Style.Fill = GradientData{r};
        } else {
            LinearGradientData l;
            l.Units = GradientUnits::UserSpaceOnUse;
            l.Start = la; l.End = lb;
            l.Stops = stops;
            e.Style.Fill = GradientData{l};
        }
    }

    ElementPtr target;
    int draggingEnd = -1;
    bool moved = false;
    Point2Dd startDoc;
};

// ===========================================================================
// TRANSPARENCY
// ===========================================================================

class TransparencyTool : public ArtTool {
public:
    TransparencyTool() : ArtTool(ArtToolId::Transparency, "Transparency", "transparency.svg", 'Y',
                                 "Click a shape and drag right for flat transparency, or drag across it for a ramp; the mix is in the options") {}

    void OnPress(ArtToolContext& ctx, const VectorPointerEvent& e) override {
        auto hit = ctx.canvas->HitTest(e.doc, 4.0);
        if (!hit || !hit->element) { target = nullptr; return; }
        target = hit->topLevel ? hit->topLevel : hit->element;
        ctx.selection->Set(target);
        startX = e.view.x;
        startDoc = e.snapped;
        startOpacity = target->Style.Opacity;
        ctx.history->BeginEdit("Transparency");
        moved = false;
    }
    void OnDrag(ArtToolContext& ctx, const VectorPointerEvent& e) override {
        if (!target) return;
        moved = true;
        const int shape = ctx.options->transparencyShape;
        if (shape == 0) {
            const float t = std::clamp(startOpacity - static_cast<float>(e.view.x - startX) / 200.0f, 0.0f, 1.0f);
            ApplyFlat(ctx, *target, 1.0f - t);
        } else {
            ApplyRamp(ctx, *target, startDoc, e.snapped);
        }
        ctx.canvas->Refresh();
    }
    void OnRelease(ArtToolContext& ctx, const VectorPointerEvent&) override {
        if (!target) return;
        const std::string id = target->Id;
        if (moved) ctx.history->EndEdit(); else ctx.history->CancelEdit();
        target = nullptr;
        ReselectByIds(ctx, {id});
        if (ctx.refreshOptions) ctx.refreshOptions();
    }
    void OnSelectionChanged(ArtToolContext& ctx) override {
        if (ctx.selection && ctx.selection->Count() == 1 && ctx.selection->First()) {
            const auto& st = ctx.selection->First()->Style;
            ArtToolOptions& o = *ctx.options;
            if (st.Transparency.has_value()) {
                o.transparencyShape = static_cast<int>(st.Transparency->Shape);
                o.transparencyMix = static_cast<int>(st.Transparency->Mix);
                o.transparency = st.Transparency->IsGradient() ? st.Transparency->Stops.back().Level * 100.0f
                                                               : st.Transparency->Level * 100.0f;
            } else {
                o.transparency = (1.0f - st.Opacity) * 100.0f;
            }
        }
        if (ctx.refreshOptions) ctx.refreshOptions();
    }
    void DrawOverlay(ArtToolContext& tctx, IRenderContext* ctx, const VectorViewTransform& v) override {
        auto sel = target ? target : (tctx.selection && tctx.selection->Count() == 1 ? tctx.selection->First() : nullptr);
        if (!sel || !sel->Style.Transparency.has_value() || !sel->Style.Transparency->IsGradient()) return;
        const auto& t = *sel->Style.Transparency;
        const Point2Dd va = v.DocToView(sel->LocalToGlobal(t.Start)), vb = v.DocToView(sel->LocalToGlobal(t.End));
        ctx->SetStrokePaint(Color(120, 120, 130, 220));
        ctx->SetStrokeWidth(1.0);
        ctx->SetLineDash(UCDashPattern());
        ctx->DrawLine(va, vb);
        DrawViewSquare(ctx, va, 8.0, Colors::White, Color(120, 120, 130, 255));
        ctx->SetFillPaint(Colors::White);
        ctx->FillCircle(vb, 4.5);
        ctx->DrawCircle(vb, 4.5);
    }
    void BuildOptions(ArtToolContext& ctx, UltraCanvasContainer& panel, const std::function<void()>&) override {
        ArtToolOptions& o = *ctx.options;
        AddDropdown(panel, "ac-tr-shape", "Shape", { "Flat", "Linear", "Radial", "Conical" }, o.transparencyShape, [&o](int i) { o.transparencyShape = i; });
        AddDropdown(panel, "ac-tr-mix", "Mix", { "Mix", "Stained glass", "Bleach", "Contrast", "Saturation", "Darken", "Lighten", "Brightness", "Luminosity", "Hue" },
                    o.transparencyMix, [&ctx, &o](int i) {
            o.transparencyMix = i;
            if (ctx.selection->Empty()) return;
            auto ids = ctx.selection->Ids();
            ctx.history->Record("Transparency Mix", [&]() {
                for (auto& e : ctx.selection->Elements()) {
                    if (!e->Style.Transparency.has_value()) {
                        TransparencyData d;
                        d.Shape = TransparencyShape::Flat;
                        d.Level = 1.0f - e->Style.Opacity;
                        e->Style.Opacity = 1.0f;
                        e->Style.Transparency = d;
                    }
                    e->Style.Transparency->Mix = static_cast<TransparencyMix>(i);
                }
            }, true);
            ReselectByIds(ctx, ids);
        });
        AddSliderRow(panel, "ac-tr-amount", "Level", 0, 100, o.transparency, 1, true, [&ctx, &o](float v) {
            o.transparency = v;
            if (ctx.selection->Empty()) return;
            auto ids = ctx.selection->Ids();
            ctx.history->Record("Transparency", [&]() {
                for (auto& e : ctx.selection->Elements()) {
                    if (e->Style.Transparency.has_value() && e->Style.Transparency->IsGradient())
                        e->Style.Transparency->Stops.back().Level = v / 100.0f;   // the ramp's far end
                    else ApplyFlat(ctx, *e, v / 100.0f);
                }
            }, true);
            ReselectByIds(ctx, ids);
        });
        AddButtonRow(panel, "ac-tr-ops", {
            {"Opaque", [&ctx]() {
                if (ctx.selection->Empty()) return;
                auto ids = ctx.selection->Ids();
                ctx.history->Record("Opaque", [&]() { for (auto& e : ctx.selection->Elements()) { e->Style.Opacity = 1.0f; e->Style.Transparency.reset(); } });
                ReselectByIds(ctx, ids);
                if (ctx.refreshOptions) ctx.refreshOptions();
            }},
        });
    }
private:
    // A flat level: the normal mix lives in Opacity, any other mix in a
    // flat TransparencyData.
    static void ApplyFlat(ArtToolContext& ctx, VectorElement& e, float level) {
        level = std::clamp(level, 0.0f, 1.0f);
        const int mix = ctx.options->transparencyMix;
        if (mix == 0 && !(e.Style.Transparency.has_value() && e.Style.Transparency->IsGradient())) {
            e.Style.Transparency.reset();
            e.Style.Opacity = 1.0f - level;
            return;
        }
        TransparencyData d;
        d.Shape = TransparencyShape::Flat;
        d.Level = level;
        d.Mix = static_cast<TransparencyMix>(mix);
        e.Style.Opacity = 1.0f;
        e.Style.Transparency = d;
    }
    static void ApplyRamp(ArtToolContext& ctx, VectorElement& e, const Point2Dd& from, const Point2Dd& to) {
        TransparencyData d;
        d.Shape = static_cast<TransparencyShape>(std::clamp(ctx.options->transparencyShape, 1, 3));
        d.Mix = static_cast<TransparencyMix>(ctx.options->transparencyMix);
        d.Start = e.GlobalToLocal(from);
        d.End = e.GlobalToLocal(to);
        if (std::hypot(d.End.x - d.Start.x, d.End.y - d.Start.y) < 0.5) d.End = Point2Dd(d.Start.x + 1, d.Start.y);
        const float far = std::clamp(ctx.options->transparency / 100.0f, 0.0f, 1.0f);
        d.Stops = {{0.0, 0.0f}, {1.0, far > 0.0f ? far : 1.0f}};
        e.Style.Opacity = 1.0f;
        e.Style.Transparency = d;
    }
    ElementPtr target;
    int startX = 0;
    Point2Dd startDoc;
    float startOpacity = 1.0f;
    bool moved = false;
};

// ===========================================================================
// SHADOW AND FEATHER
// ===========================================================================

class ShadowTool : public ArtTool {
public:
    ShadowTool() : ArtTool(ArtToolId::Shadow, "Shadow", "shadow.svg", 'W',
                           "Click a shape for a shadow, then drag to place it; kind, blur and darkness are in the options") {}

    void OnPress(ArtToolContext& ctx, const VectorPointerEvent& e) override {
        auto hit = ctx.canvas->HitTest(e.doc, 4.0);
        if (!hit || !hit->element) { target = nullptr; return; }
        target = hit->topLevel ? hit->topLevel : hit->element;
        ctx.selection->Set(target);
        startDoc = e.doc;
        ctx.history->BeginEdit("Shadow");
        if (!target->Effects.Shadow.has_value()) target->Effects.Shadow = FromOptions(*ctx.options);
        startOffset = target->Effects.Shadow->Offset;
        ctx.canvas->Refresh();
    }
    void OnDrag(ArtToolContext& ctx, const VectorPointerEvent& e) override {
        if (!target || !target->Effects.Shadow.has_value()) return;
        target->Effects.Shadow->Offset = Point2Dd(startOffset.x + (e.doc.x - startDoc.x), startOffset.y + (e.doc.y - startDoc.y));
        ctx.canvas->Refresh();
    }
    void OnRelease(ArtToolContext& ctx, const VectorPointerEvent&) override {
        if (!target) return;
        const std::string id = target->Id;
        ctx.history->EndEdit();
        target = nullptr;
        ReselectByIds(ctx, {id});
        if (ctx.refreshOptions) ctx.refreshOptions();
    }
    void OnSelectionChanged(ArtToolContext& ctx) override {
        if (ctx.selection && ctx.selection->Count() == 1 && ctx.selection->First() && ctx.selection->First()->Effects.Shadow.has_value()) {
            const auto& sh = *ctx.selection->First()->Effects.Shadow;
            ctx.options->shadowKind = static_cast<int>(sh.Kind);
            ctx.options->shadowBlur = sh.Blur;
            ctx.options->shadowDarkness = sh.Darkness * 100.0f;
        }
        if (ctx.refreshOptions) ctx.refreshOptions();
    }
    void BuildOptions(ArtToolContext& ctx, UltraCanvasContainer& panel, const std::function<void()>&) override {
        ArtToolOptions& o = *ctx.options;
        auto update = [&ctx](const std::string& label, const std::function<void(ShadowEffect&)>& fn) {
            if (ctx.selection->Empty()) return;
            auto ids = ctx.selection->Ids();
            ctx.history->Record(label, [&]() {
                for (auto& e : ctx.selection->Elements()) {
                    if (!e->Effects.Shadow.has_value()) e->Effects.Shadow = FromOptions(*ctx.options);
                    fn(*e->Effects.Shadow);
                }
            }, true);
            ReselectByIds(ctx, ids);
        };
        AddDropdown(panel, "ac-sh-kind", "Kind", { "Wall", "Floor", "Glow" }, o.shadowKind, [&o, update](int i) {
            o.shadowKind = i;
            update("Shadow Kind", [i](ShadowEffect& s) { s.Kind = static_cast<ShadowKind>(i); });
        });
        AddSliderRow(panel, "ac-sh-blur", "Blur", 0, 40, o.shadowBlur, 0.5f, false, [&o, update](float v) {
            o.shadowBlur = v;
            update("Shadow Blur", [v](ShadowEffect& s) { s.Blur = v; });
        });
        AddSliderRow(panel, "ac-sh-dark", "Darkness", 0, 100, o.shadowDarkness, 1, true, [&o, update](float v) {
            o.shadowDarkness = v;
            update("Shadow Darkness", [v](ShadowEffect& s) { s.Darkness = v / 100.0f; });
        });
        AddButtonRow(panel, "ac-sh-ops", {
            {"Line colour", [&ctx, update]() {
                const Color c = ctx.lineColor ? ctx.lineColor() : Colors::Black;
                update("Shadow Colour", [c](ShadowEffect& s) { s.Colour = Color(c.r, c.g, c.b, 255); });
            }},
            {"Remove", [&ctx]() {
                if (ctx.selection->Empty()) return;
                auto ids = ctx.selection->Ids();
                ctx.history->Record("Remove Shadow", [&]() { for (auto& e : ctx.selection->Elements()) e->Effects.Shadow.reset(); });
                ReselectByIds(ctx, ids);
            }},
        });
    }
private:
    static ShadowEffect FromOptions(const ArtToolOptions& o) {
        ShadowEffect s;
        s.Kind = static_cast<ShadowKind>(std::clamp(o.shadowKind, 0, 2));
        s.Blur = o.shadowBlur;
        s.Darkness = std::clamp(o.shadowDarkness / 100.0f, 0.0f, 1.0f);
        s.Offset = Point2Dd(4, 4);
        return s;
    }
    ElementPtr target;
    Point2Dd startDoc, startOffset;
};

class FeatherTool : public ArtTool {
public:
    FeatherTool() : ArtTool(ArtToolId::Feather, "Feather", "feather.svg", 'K',
                            "Click a shape to feather its edges, then drag right for a wider fade") {}

    void OnPress(ArtToolContext& ctx, const VectorPointerEvent& e) override {
        auto hit = ctx.canvas->HitTest(e.doc, 4.0);
        if (!hit || !hit->element) { target = nullptr; return; }
        target = hit->topLevel ? hit->topLevel : hit->element;
        ctx.selection->Set(target);
        startX = e.view.x;
        ctx.history->BeginEdit("Feather");
        if (!target->Effects.Feather.has_value()) target->Effects.Feather = FeatherEffect{ctx.options->featherRadius};
        startRadius = target->Effects.Feather->Radius;
        ctx.canvas->Refresh();
    }
    void OnDrag(ArtToolContext& ctx, const VectorPointerEvent& e) override {
        if (!target || !target->Effects.Feather.has_value()) return;
        const float r = std::clamp(startRadius + static_cast<float>(ctx.canvas->PixelsToDoc(e.view.x - startX)), 0.0f, 200.0f);
        target->Effects.Feather->Radius = r;
        ctx.options->featherRadius = r;
        ctx.canvas->Refresh();
    }
    void OnRelease(ArtToolContext& ctx, const VectorPointerEvent&) override {
        if (!target) return;
        const std::string id = target->Id;
        ctx.history->EndEdit();
        target = nullptr;
        ReselectByIds(ctx, {id});
        if (ctx.refreshOptions) ctx.refreshOptions();
    }
    void OnSelectionChanged(ArtToolContext& ctx) override {
        if (ctx.selection && ctx.selection->Count() == 1 && ctx.selection->First() && ctx.selection->First()->Effects.Feather.has_value())
            ctx.options->featherRadius = ctx.selection->First()->Effects.Feather->Radius;
        if (ctx.refreshOptions) ctx.refreshOptions();
    }
    void BuildOptions(ArtToolContext& ctx, UltraCanvasContainer& panel, const std::function<void()>&) override {
        ArtToolOptions& o = *ctx.options;
        AddSliderRow(panel, "ac-fe-radius", "Radius", 0, 60, o.featherRadius, 0.5f, false, [&ctx, &o](float v) {
            o.featherRadius = v;
            if (ctx.selection->Empty()) return;
            auto ids = ctx.selection->Ids();
            ctx.history->Record("Feather", [&]() { for (auto& e : ctx.selection->Elements()) e->Effects.Feather = FeatherEffect{v}; }, true);
            ReselectByIds(ctx, ids);
        });
        AddButtonRow(panel, "ac-fe-ops", {
            {"Remove", [&ctx]() {
                if (ctx.selection->Empty()) return;
                auto ids = ctx.selection->Ids();
                ctx.history->Record("Remove Feather", [&]() { for (auto& e : ctx.selection->Elements()) e->Effects.Feather.reset(); });
                ReselectByIds(ctx, ids);
            }},
        });
    }
private:
    ElementPtr target;
    int startX = 0;
    float startRadius = 0;
};

// ===========================================================================
// ZOOM AND PUSH
// ===========================================================================

class ZoomTool : public ArtTool {
public:
    ZoomTool() : ArtTool(ArtToolId::Zoom, "Zoom", "zoom.svg", 'Z', "Click to zoom in, shift-click to zoom out, drag a rectangle to zoom to it") {}
    UCMouseCursor Cursor() const override { return UCMouseCursor::LookingGlass; }
    void OnPress(ArtToolContext&, const VectorPointerEvent& e) override { start = e.doc; current = start; startView = e.view; dragging = true; }
    void OnDrag(ArtToolContext& ctx, const VectorPointerEvent& e) override { current = e.doc; ctx.canvas->Refresh(); }
    void OnRelease(ArtToolContext& ctx, const VectorPointerEvent& e) override {
        dragging = false;
        const Rect2Dd r = DragRect(start, e.doc, false, false);
        if (r.width > ctx.canvas->PixelsToDoc(6) && r.height > ctx.canvas->PixelsToDoc(6)) ctx.canvas->ZoomToRect(r, 12.0);
        else if (e.shift || e.button == UCMouseButton::Right) ctx.canvas->SetZoomAt(ctx.canvas->GetZoom() / 1.5, startView);
        else ctx.canvas->SetZoomAt(ctx.canvas->GetZoom() * 1.5, startView);
        ctx.canvas->Refresh();
    }
    void DrawOverlay(ArtToolContext&, IRenderContext* ctx, const VectorViewTransform& v) override {
        if (!dragging) return;
        ctx->SetStrokePaint(Color(30, 100, 220, 220));
        ctx->SetStrokeWidth(1.0);
        ctx->SetLineDash(UCDashPattern({4.0, 3.0}, 0.0));
        ctx->DrawRectangle(v.DocToView(DragRect(start, current, false, false)));
        ctx->SetLineDash(UCDashPattern());
    }
private:
    Point2Dd start, current;
    Point2Di startView;
    bool dragging = false;
};

class PushTool : public ArtTool {
public:
    PushTool() : ArtTool(ArtToolId::Push, "Push", "push.svg", 'H', "Drag to move the view (or hold space with any tool)") {}
    UCMouseCursor Cursor() const override { return UCMouseCursor::Hand; }
    void Activate(ArtToolContext& ctx) override { ctx.canvas->SetPanMode(true); }
    void Deactivate(ArtToolContext& ctx) override { ctx.canvas->SetPanMode(false); }
};

} // namespace

// ===========================================================================
// FACTORY
// ===========================================================================

std::vector<std::unique_ptr<ArtTool>> CreateArtTools() {
    std::vector<std::unique_ptr<ArtTool>> tools;
    tools.push_back(std::make_unique<SelectorTool>());
    tools.push_back(std::make_unique<ShapeEditorTool>());
    tools.push_back(std::make_unique<PenTool>());
    tools.push_back(std::make_unique<FreehandTool>());
    tools.push_back(std::make_unique<LineTool>());
    tools.push_back(std::make_unique<RectangleTool>());
    tools.push_back(std::make_unique<EllipseTool>());
    tools.push_back(std::make_unique<QuickShapeTool>());
    tools.push_back(std::make_unique<TextTool>());
    tools.push_back(std::make_unique<FillTool>());
    tools.push_back(std::make_unique<TransparencyTool>());
    tools.push_back(std::make_unique<ShadowTool>());
    tools.push_back(std::make_unique<FeatherTool>());
    tools.push_back(std::make_unique<ZoomTool>());
    tools.push_back(std::make_unique<PushTool>());
    return tools;
}

} // namespace UltraCanvas
