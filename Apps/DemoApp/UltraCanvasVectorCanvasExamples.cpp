// Apps/DemoApp/UltraCanvasVectorCanvasExamples.cpp
// Vector editing demo: an UltraCanvasVectorCanvas over a VectorDocument
// with a small set of tools written the way an application writes them -
// a selector (click, shift-click, marquee, move, scale by the handles, a
// second click for rotate mode), a rectangle and an ellipse tool, a
// freehand tool through UltraCanvasBezierPath::FromPolyline - every edit
// recorded in a VectorEdit::VectorHistory, and an UltraCanvasGradientEditor
// bound to the selected shape's fill. The page is the reference for what
// the framework provides (Docs/UltraCanvas/UltraCanvasVectorCanvas.md) and
// what an application adds on top.
// Version: 1.0.0
// Last Modified: 2026-09-15
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasVectorCanvas.h"
#include "UltraCanvasGradientEditor.h"
#include "UltraCanvasBezierPath.h"
#include "UltraCanvasConfig.h"   // GetResourcesDir
#include "UltraCanvasUtils.h"    // NormalizePath
#include "DataFormats/UltraCanvasVectorEdit.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

namespace {

    using namespace VectorStorage;
    using namespace VectorEdit;

    std::shared_ptr<VectorDocument> MakeSampleDocument() {
        auto doc = std::make_shared<VectorDocument>();
        doc->Size = Size2Dd{595, 420};
        doc->ViewBox = Rect2Dd{0, 0, 595, 420};
        auto layer = doc->AddLayer("Layer 1");

        auto back = std::make_shared<VectorRect>();
        back->Bounds = Rect2Dd{40, 40, 260, 160};
        back->RadiusX = back->RadiusY = 14;
        LinearGradientData lg;
        lg.Start = {0, 0}; lg.End = {1, 1};
        lg.Stops = {{0.0, Color(255, 196, 60, 255)}, {1.0, Color(230, 90, 40, 255)}};
        back->Style.Fill = GradientData{lg};
        StrokeData outline; outline.Fill = Color(120, 50, 20, 255); outline.Width = 2;
        back->Style.Stroke = outline;
        layer->AddChild(back);

        auto disc = std::make_shared<VectorEllipse>();
        disc->Center = {400, 130}; disc->RadiusX = 90; disc->RadiusY = 70;
        RadialGradientData rg;
        rg.Stops = {{0.0, Color(200, 240, 255, 255)}, {1.0, Color(30, 110, 200, 255)}};
        rg.FocalPoint = {0.35f, 0.35f};
        disc->Style.Fill = GradientData{rg};
        layer->AddChild(disc);

        auto star = std::make_shared<VectorPath>();
        const int points = 5;
        for (int i = 0; i < points * 2; ++i) {
            const double a = -M_PI / 2 + i * M_PI / points;
            const double r = (i % 2 == 0) ? 70.0 : 30.0;
            const float x = static_cast<float>(150 + r * std::cos(a));
            const float y = static_cast<float>(320 + r * std::sin(a));
            if (i == 0) star->MoveTo(x, y); else star->LineTo(x, y);
        }
        star->ClosePath();
        star->Style.Fill = Color(90, 170, 80, 255);
        StrokeData starStroke; starStroke.Fill = Color(30, 90, 30, 255); starStroke.Width = 3;
        starStroke.LineJoin = StrokeLineJoin::Round;
        star->Style.Stroke = starStroke;
        layer->AddChild(star);

        auto curve = std::make_shared<VectorPath>();
        curve->MoveTo(300, 330);
        curve->CurveTo(340, 240, 420, 400, 470, 300, false);
        curve->CurveTo(500, 240, 540, 320, 560, 280, false);
        curve->Style.Fill.reset();
        StrokeData curveStroke; curveStroke.Fill = Color(120, 40, 160, 255); curveStroke.Width = 6;
        curveStroke.LineCap = StrokeLineCap::Round;
        curve->Style.Stroke = curveStroke;
        layer->AddChild(curve);

        auto text = std::make_shared<VectorText>();
        text->Position = {40, 240};
        text->BaseStyle.FontFamily = "Sans";
        text->BaseStyle.FontSize = 22;
        text->BaseStyle.Weight = FontWeight::Bold;
        text->SetText("UltraCanvasVectorCanvas");
        text->Style.Fill = Color(40, 40, 50, 255);
        layer->AddChild(text);

        EnsureIds(*doc);
        return doc;
    }

    // ===== THE DEMO EDITOR =====
    // What an application's tool classes do, in one object: it owns the
    // document, selection and history, listens to the canvas's tool hooks,
    // and edits through VectorEdit inside history edits.
    class DemoVectorEditor : public std::enable_shared_from_this<DemoVectorEditor> {
    public:
        enum class Tool { Select, Rectangle, Ellipse, Freehand };

        std::shared_ptr<VectorDocument> doc;
        std::shared_ptr<VectorSelection> selection;
        VectorHistory history;
        std::shared_ptr<UltraCanvasVectorCanvas> canvas;
        std::shared_ptr<UltraCanvasGradientEditor> ramp;
        std::shared_ptr<UltraCanvasLabel> status;
        std::vector<std::shared_ptr<UltraCanvasButton>> toolButtons;
        Tool tool = Tool::Select;

        DemoVectorEditor() : selection(std::make_shared<VectorSelection>()) {}

        void Attach() {
            doc = MakeSampleDocument();
            history.SetDocument(doc);
            canvas->SetDocument(doc);
            canvas->SetSelection(selection);
            auto self = weak_from_this();
            history.onChanged = [self]() {
                if (auto e = self.lock()) { e->selection->Rebind(*e->doc); e->canvas->Refresh(); e->UpdateStatus(); }
            };
            selection->AddListener([self]() { if (auto e = self.lock()) e->OnSelectionChanged(); });
            canvas->onToolPress = [self](const VectorPointerEvent& ev) { if (auto e = self.lock()) e->OnPress(ev); };
            canvas->onToolDrag = [self](const VectorPointerEvent& ev) { if (auto e = self.lock()) e->OnDrag(ev); };
            canvas->onToolRelease = [self](const VectorPointerEvent& ev) { if (auto e = self.lock()) e->OnRelease(ev); };
            canvas->onToolHover = [self](const VectorPointerEvent& ev) { if (auto e = self.lock()) e->OnHover(ev); };
            canvas->onToolKey = [self](const UCEvent& k) { auto e = self.lock(); return e && e->OnKey(k); };
            canvas->onDrawOverlay = [self](IRenderContext* ctx, const VectorViewTransform& v) { if (auto e = self.lock()) e->DrawOverlay(ctx, v); };
            canvas->onViewChanged = [self]() { if (auto e = self.lock()) e->UpdateStatus(); };
            ramp->onStopsChanged = [self]() { if (auto e = self.lock()) e->ApplyRamp(true); };
            ramp->onStopsChanging = [self]() { if (auto e = self.lock()) e->ApplyRamp(false); };
            UpdateStatus();
        }

        void SetTool(Tool t) {
            tool = t;
            for (size_t i = 0; i < toolButtons.size(); ++i) {
                const bool on = static_cast<int>(t) == static_cast<int>(i);
                if (toolButtons[i]->IsPressed() != on) toolButtons[i]->SetPressed(on);
            }
            canvas->SetToolCursor(t == Tool::Select ? UCMouseCursor::Arrow : UCMouseCursor::Cross);
            UpdateStatus();
        }

        // ----- selection and the ramp -----
        void OnSelectionChanged() {
            syncingRamp = true;
            if (auto e = selection->Count() == 1 ? selection->First() : nullptr) {
                if (e->Style.Fill.has_value()) {
                    if (auto* g = std::get_if<GradientData>(&*e->Style.Fill)) {
                        if (auto* l = std::get_if<LinearGradientData>(g)) ramp->SetStops(l->Stops);
                        else if (auto* r = std::get_if<RadialGradientData>(g)) ramp->SetStops(r->Stops);
                    } else if (auto* c = std::get_if<Color>(&*e->Style.Fill)) {
                        ramp->SetStops({{0.0, *c}, {1.0, *c}});
                    }
                }
            }
            syncingRamp = false;
            UpdateStatus();
        }

        void ApplyRamp(bool commit) {
            if (syncingRamp || selection->Count() != 1) return;
            auto e = selection->First();
            if (!e || !e->Style.Fill.has_value()) return;
            auto apply = [&]() {
                if (auto* g = std::get_if<GradientData>(&*e->Style.Fill)) {
                    if (auto* l = std::get_if<LinearGradientData>(g)) l->Stops = ramp->GetStops();
                    else if (auto* r = std::get_if<RadialGradientData>(g)) r->Stops = ramp->GetStops();
                } else {
                    LinearGradientData lg;
                    lg.Start = {0, 0}; lg.End = {1, 0};
                    lg.Stops = ramp->GetStops();
                    e->Style.Fill = GradientData{lg};
                }
            };
            if (commit) history.Record("Gradient", apply, true);
            else apply();
            canvas->Refresh();
        }

        // ----- tools -----
        void OnPress(const VectorPointerEvent& ev) {
            pressDoc = ev.snapped;
            lastDoc = ev.snapped;
            dragged = false;
            switch (tool) {
                case Tool::Select: {
                    const VectorHandle h = canvas->HitTestHandle(ev.view);
                    if (h != VectorHandle::NoHandle && h != VectorHandle::Body) {
                        activeHandle = h;
                        startBounds = canvas->SelectionBounds();
                        history.BeginEdit(canvas->GetHandleMode() == VectorHandleMode::Rotate
                                          ? (h == VectorHandle::Center ? "Move Centre" : "Rotate") : "Scale");
                        return;
                    }
                    auto hit = canvas->HitTest(ev.doc, 4.0);
                    if (hit) {
                        ElementPtr target = hit->topLevel ? hit->topLevel : hit->element;
                        if (ev.shift) selection->Toggle(target);
                        else if (!selection->Contains(target)) selection->Set(target);
                        else if (h == VectorHandle::Body && !ev.shift) toggleModePending = true;   // second click: rotate mode
                        moving = !selection->Empty();
                        if (moving) history.BeginEdit("Move");
                    } else {
                        if (!ev.shift) selection->Clear();
                        canvas->SetHandleMode(VectorHandleMode::Scale);
                        marquee = true;
                    }
                    break;
                }
                case Tool::Rectangle:
                case Tool::Ellipse:
                    drawing = true;
                    break;
                case Tool::Freehand:
                    strokePoints.clear();
                    strokePoints.push_back(ev.doc);
                    drawing = true;
                    break;
            }
        }

        void OnDrag(const VectorPointerEvent& ev) {
            dragged = true;
            switch (tool) {
                case Tool::Select:
                    if (activeHandle != VectorHandle::NoHandle) DragHandle(ev);
                    else if (moving) {
                        TranslateElements(selection->Elements(), ev.snapped.x - lastDoc.x, ev.snapped.y - lastDoc.y);
                        lastDoc = ev.snapped;
                        canvas->Refresh();
                    } else if (marquee) {
                        currentDoc = ev.doc;
                        canvas->Refresh();
                    }
                    break;
                case Tool::Freehand:
                    strokePoints.push_back(ev.doc);
                    canvas->Refresh();
                    break;
                default:
                    currentDoc = ev.snapped;
                    canvas->Refresh();
                    break;
            }
            UpdateStatus();
        }

        void OnRelease(const VectorPointerEvent& ev) {
            switch (tool) {
                case Tool::Select:
                    if (activeHandle != VectorHandle::NoHandle) {
                        activeHandle = VectorHandle::NoHandle;
                        history.EndEdit();
                    } else if (moving) {
                        moving = false;
                        if (dragged) history.EndEdit();
                        else {
                            history.CancelEdit();
                            if (toggleModePending)
                                canvas->SetHandleMode(canvas->GetHandleMode() == VectorHandleMode::Scale
                                                      ? VectorHandleMode::Rotate : VectorHandleMode::Scale);
                        }
                        toggleModePending = false;
                    } else if (marquee) {
                        marquee = false;
                        if (dragged) {
                            const Rect2Dd r = NormalizedRect(pressDoc, ev.doc);
                            auto found = canvas->ElementsIn(r, true);
                            if (ev.shift) for (auto& e : found) selection->Add(e);
                            else selection->Set(found);
                        }
                        canvas->Refresh();
                    }
                    break;
                case Tool::Rectangle:
                case Tool::Ellipse: {
                    drawing = false;
                    const Rect2Dd r = NormalizedRect(pressDoc, ev.snapped);
                    if (r.width < 2 || r.height < 2) { canvas->Refresh(); break; }
                    ElementPtr made;
                    history.Record(tool == Tool::Rectangle ? "Rectangle" : "Ellipse", [&]() {
                        if (tool == Tool::Rectangle) {
                            auto rect = std::make_shared<VectorRect>();
                            rect->Bounds = r;
                            made = rect;
                        } else {
                            auto ell = std::make_shared<VectorEllipse>();
                            ell->Center = {r.x + r.width / 2, r.y + r.height / 2};
                            ell->RadiusX = static_cast<float>(r.width / 2);
                            ell->RadiusY = static_cast<float>(r.height / 2);
                            made = ell;
                        }
                        made->Id = GenerateId();
                        made->Style.Fill = Color(80, 140, 220, 255);
                        StrokeData st; st.Fill = Color(30, 60, 120, 255); st.Width = 2;
                        made->Style.Stroke = st;
                        doc->Layers.front()->AddChild(made);
                    });
                    if (auto fresh = doc->FindElementById(made->Id)) selection->Set(fresh);
                    break;
                }
                case Tool::Freehand: {
                    drawing = false;
                    if (strokePoints.size() < 2) break;
                    auto bezier = UltraCanvasBezierPath::FromPolyline(strokePoints, false, canvas->PixelsToDoc(2.0));
                    std::string id;
                    history.Record("Freehand", [&]() {
                        auto path = std::make_shared<VectorPath>();
                        path->Path = bezier.ToPathData();
                        path->Id = id = GenerateId();
                        path->Style.Fill.reset();
                        StrokeData st; st.Fill = Color(40, 40, 40, 255); st.Width = 3;
                        st.LineCap = StrokeLineCap::Round; st.LineJoin = StrokeLineJoin::Round;
                        path->Style.Stroke = st;
                        doc->Layers.front()->AddChild(path);
                    });
                    strokePoints.clear();
                    if (auto fresh = doc->FindElementById(id)) selection->Set(fresh);
                    break;
                }
            }
            UpdateStatus();
        }

        void OnHover(const VectorPointerEvent& ev) {
            hoverDoc = ev.doc;
            if (tool == Tool::Select) {
                const VectorHandle h = canvas->HitTestHandle(ev.view);
                UCMouseCursor c = UCMouseCursor::Arrow;
                switch (h) {
                    case VectorHandle::TopLeft: case VectorHandle::BottomRight: c = UCMouseCursor::SizeNWSE; break;
                    case VectorHandle::TopRight: case VectorHandle::BottomLeft: c = UCMouseCursor::SizeNESW; break;
                    case VectorHandle::Top: case VectorHandle::Bottom: c = UCMouseCursor::SizeNS; break;
                    case VectorHandle::Left: case VectorHandle::Right: c = UCMouseCursor::SizeWE; break;
                    case VectorHandle::Body: c = UCMouseCursor::SizeAll; break;
                    default: break;
                }
                canvas->SetToolCursor(c);
            }
            UpdateStatus();
        }

        bool OnKey(const UCEvent& k) {
            if (k.virtualKey == UCKeys::Delete || k.virtualKey == UCKeys::Backspace) { DeleteSelection(); return true; }
            if (k.virtualKey == UCKeys::Escape) { selection->Clear(); return true; }
            if (k.ctrl && (k.character == 'z' || k.character == 'Z')) { if (k.shift) history.Redo(); else history.Undo(); return true; }
            if (k.ctrl && (k.character == 'y' || k.character == 'Y')) { history.Redo(); return true; }
            if (k.ctrl && (k.character == 'g' || k.character == 'G')) { if (k.shift) Ungroup(); else Group(); return true; }
            if (k.ctrl && (k.character == 'd' || k.character == 'D')) { Duplicate(); return true; }
            const double step = k.shift ? 10.0 : 1.0;
            double dx = 0, dy = 0;
            if (k.virtualKey == UCKeys::Left) dx = -step; else if (k.virtualKey == UCKeys::Right) dx = step;
            else if (k.virtualKey == UCKeys::Up) dy = -step; else if (k.virtualKey == UCKeys::Down) dy = step;
            if ((dx != 0 || dy != 0) && !selection->Empty()) {
                history.Record("Nudge", [&]() { TranslateElements(selection->Elements(), dx, dy); }, true);
                return true;
            }
            return false;
        }

        void DrawOverlay(IRenderContext* ctx, const VectorViewTransform& v) {
            ctx->SetStrokeWidth(1.0);
            if (marquee && dragged) {
                const Rect2Dd r = v.DocToView(NormalizedRect(pressDoc, currentDoc));
                ctx->SetFillPaint(Color(30, 100, 220, 30));
                ctx->FillRectangle(r);
                ctx->SetStrokePaint(Color(30, 100, 220, 200));
                ctx->SetLineDash(UCDashPattern({4.0, 3.0}, 0.0));
                ctx->DrawRectangle(r);
                ctx->SetLineDash(UCDashPattern());
            }
            if (drawing && (tool == Tool::Rectangle || tool == Tool::Ellipse) && dragged) {
                const Rect2Dd r = v.DocToView(NormalizedRect(pressDoc, currentDoc));
                ctx->SetStrokePaint(Color(30, 60, 120, 220));
                if (tool == Tool::Rectangle) ctx->DrawRectangle(r);
                else ctx->DrawEllipse(r);
            }
            if (drawing && tool == Tool::Freehand && strokePoints.size() > 1) {
                ctx->SetStrokePaint(Color(40, 40, 40, 200));
                ctx->SetStrokeWidth(2.0);
                std::vector<Point2Dd> pts;
                for (const auto& p : strokePoints) pts.push_back(v.DocToView(p));
                ctx->DrawLinePath(pts, false);
            }
        }

        // ----- commands -----
        void DeleteSelection() {
            if (selection->Empty()) return;
            history.Record("Delete", [&]() { DeleteElements(selection->Elements()); });
            selection->Clear();
        }
        void Group() {
            if (selection->Count() < 2) return;
            std::string id;
            history.Record("Group", [&]() { if (auto g = GroupElements(selection->Elements())) id = g->Id; });
            if (auto g = doc->FindElementById(id)) selection->Set(g);
        }
        void Ungroup() {
            if (selection->Empty()) return;
            std::vector<std::string> ids;
            history.Record("Ungroup", [&]() { for (auto& e : UngroupElements(selection->Elements())) ids.push_back(e->Id); });
            std::vector<ElementPtr> fresh;
            for (auto& id : ids) if (auto e = doc->FindElementById(id)) fresh.push_back(e);
            selection->Set(fresh);
        }
        void Duplicate() {
            if (selection->Empty()) return;
            std::vector<std::string> ids;
            history.Record("Duplicate", [&]() { for (auto& e : DuplicateElements(selection->Elements(), 12, 12)) ids.push_back(e->Id); });
            std::vector<ElementPtr> fresh;
            for (auto& id : ids) if (auto e = doc->FindElementById(id)) fresh.push_back(e);
            selection->Set(fresh);
        }
        void Reorder(ZOrderMove m) {
            if (selection->Empty()) return;
            history.Record(m == ZOrderMove::ToFront ? "To Front" : "To Back", [&]() { ReorderElements(selection->Elements(), m); });
        }

        void UpdateStatus() {
            if (!status) return;
            char buf[200];
            const char* toolName = tool == Tool::Select ? "Select" : tool == Tool::Rectangle ? "Rectangle"
                                 : tool == Tool::Ellipse ? "Ellipse" : "Freehand";
            std::snprintf(buf, sizeof buf, "%s   zoom %.0f%%   pointer %.0f, %.0f pt   selected %zu   undo: %s",
                          toolName, canvas->GetZoom() * 100.0, hoverDoc.x, hoverDoc.y, selection->Count(),
                          history.CanUndo() ? history.UndoLabel().c_str() : "-");
            status->SetText(buf);
        }

    private:
        static Rect2Dd NormalizedRect(const Point2Dd& a, const Point2Dd& b) {
            return Rect2Dd(std::min(a.x, b.x), std::min(a.y, b.y), std::fabs(b.x - a.x), std::fabs(b.y - a.y));
        }

        void DragHandle(const VectorPointerEvent& ev) {
            const Rect2Dd cur = canvas->SelectionBounds();
            if (canvas->GetHandleMode() == VectorHandleMode::Rotate) {
                if (activeHandle == VectorHandle::Center) { canvas->SetRotationCenter(ev.snapped); return; }
                const Point2Dd c = canvas->GetRotationCenter();
                const double a0 = std::atan2(lastDoc.y - c.y, lastDoc.x - c.x);
                const double a1 = std::atan2(ev.doc.y - c.y, ev.doc.x - c.x);
                RotateElements(selection->Elements(), a1 - a0, c);
                lastDoc = ev.doc;
                canvas->Refresh();
                return;
            }
            // Scale about the opposite edge / corner.
            double sx = 1, sy = 1;
            Point2Dd pivot(cur.x, cur.y);
            const bool left = activeHandle == VectorHandle::TopLeft || activeHandle == VectorHandle::Left || activeHandle == VectorHandle::BottomLeft;
            const bool right = activeHandle == VectorHandle::TopRight || activeHandle == VectorHandle::Right || activeHandle == VectorHandle::BottomRight;
            const bool top = activeHandle == VectorHandle::TopLeft || activeHandle == VectorHandle::Top || activeHandle == VectorHandle::TopRight;
            const bool bottom = activeHandle == VectorHandle::BottomLeft || activeHandle == VectorHandle::Bottom || activeHandle == VectorHandle::BottomRight;
            if (right) { pivot.x = cur.x; sx = (ev.snapped.x - cur.x) / cur.width; }
            if (left) { pivot.x = cur.x + cur.width; sx = (pivot.x - ev.snapped.x) / cur.width; }
            if (bottom) { pivot.y = cur.y; sy = (ev.snapped.y - cur.y) / cur.height; }
            if (top) { pivot.y = cur.y + cur.height; sy = (pivot.y - ev.snapped.y) / cur.height; }
            if (ev.shift && (left || right) && (top || bottom)) sx = sy = std::max(sx, sy);
            if (std::fabs(sx) < 0.01 || std::fabs(sy) < 0.01 || !std::isfinite(sx) || !std::isfinite(sy)) return;
            ScaleElements(selection->Elements(), sx, sy, pivot);
            canvas->Refresh();
        }

        bool syncingRamp = false;
        bool moving = false, marquee = false, drawing = false, dragged = false, toggleModePending = false;
        VectorHandle activeHandle = VectorHandle::NoHandle;
        Rect2Dd startBounds;
        Point2Dd pressDoc, lastDoc, currentDoc, hoverDoc;
        std::vector<Point2Dd> strokePoints;
    };

} // namespace

// ===== VECTOR CANVAS EXAMPLES =====
std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateVectorCanvasExamples() {
    auto container = std::make_shared<UltraCanvasContainer>("VectorCanvasExamples", 0, 0, 1000, 780);
    container->SetBackgroundColor(Color(245, 245, 245, 255));

    auto title = std::make_shared<UltraCanvasLabel>("VCTitle", 10, 10, 700, 30);
    title->SetText("Vector Editing - UltraCanvasVectorCanvas");
    title->SetFontSize(16);
    title->SetFontWeight(FontWeight::Bold);
    container->AddChild(title);

    auto description = std::make_shared<UltraCanvasLabel>("VCDescription", 10, 42, 970, 40);
    description->SetText("Select: click, shift-click, marquee; drag to move, drag a handle to scale (shift keeps the aspect), click a selected shape again for rotate / skew handles.\n"
                         "Rectangle, Ellipse and Freehand draw; Delete, Ctrl+Z / Ctrl+Y, Ctrl+G / Ctrl+Shift+G, Ctrl+D, arrows nudge. Wheel zooms, space pans, drag a guide out of a ruler.");
    description->SetFontSize(11);
    description->SetTextColor(Color(80, 80, 80, 255));
    container->AddChild(description);

    auto editor = std::make_shared<DemoVectorEditor>();

    // The toolbar carries icons, not words - the same set ArtCreator uses
    // (media/icons/artcreator and media/icons/texter), with the name and its
    // shortcut in the tooltip. Labels are off, so a button is its icon.
    auto toolbar = std::make_shared<UltraCanvasToolbar>("VCToolbar", 10, 88, 980, 36);
    {
        ToolbarAppearance app = toolbar->GetAppearance();
        app.showIconLabels = false;
        toolbar->SetAppearance(app);
    }

    const std::string artIcons = NormalizePath(GetResourcesDir() + "media/icons/artcreator/");
    const std::string textIcons = NormalizePath(GetResourcesDir() + "media/icons/texter/");

    auto addTool = [&](const std::string& id, const std::string& icon, const std::string& tip,
                       DemoVectorEditor::Tool t) {
        auto b = toolbar->AddToggleButton(id, "", artIcons + icon, [editor, t](bool) { editor->SetTool(t); });
        b->SetTooltip(tip);
        editor->toolButtons.push_back(b);
    };
    addTool("vcSelect", "selector.svg", "Select - click, shift-click, marquee; drag to move", DemoVectorEditor::Tool::Select);
    addTool("vcRect", "rectangle.svg", "Rectangle - drag to draw", DemoVectorEditor::Tool::Rectangle);
    addTool("vcEllipse", "ellipse.svg", "Ellipse - drag to draw", DemoVectorEditor::Tool::Ellipse);
    addTool("vcFreehand", "freehand.svg", "Freehand - drag to draw a curve", DemoVectorEditor::Tool::Freehand);
    toolbar->AddSeparator();
    toolbar->AddButton("vcUndo", "", textIcons + "undo.svg", [editor]() { editor->history.Undo(); })->SetTooltip("Undo (Ctrl+Z)");
    toolbar->AddButton("vcRedo", "", textIcons + "redo.svg", [editor]() { editor->history.Redo(); })->SetTooltip("Redo (Ctrl+Y)");
    toolbar->AddSeparator();
    toolbar->AddButton("vcDelete", "", artIcons + "delete.svg", [editor]() { editor->DeleteSelection(); })->SetTooltip("Delete (Del)");
    toolbar->AddButton("vcDuplicate", "", artIcons + "duplicate.svg", [editor]() { editor->Duplicate(); })->SetTooltip("Duplicate (Ctrl+D)");
    toolbar->AddButton("vcGroup", "", artIcons + "group.svg", [editor]() { editor->Group(); })->SetTooltip("Group (Ctrl+G)");
    toolbar->AddButton("vcUngroup", "", artIcons + "ungroup.svg", [editor]() { editor->Ungroup(); })->SetTooltip("Ungroup (Ctrl+Shift+G)");
    toolbar->AddButton("vcFront", "", artIcons + "to-front.svg", [editor]() { editor->Reorder(ZOrderMove::ToFront); })->SetTooltip("Bring to front");
    toolbar->AddButton("vcBack", "", artIcons + "to-back.svg", [editor]() { editor->Reorder(ZOrderMove::ToBack); })->SetTooltip("Send to back");
    toolbar->AddSeparator();
    toolbar->AddButton("vcZoomPage", "", artIcons + "zoom-page.svg", [editor]() { editor->canvas->ZoomToPage(); })->SetTooltip("Fit page");
    toolbar->AddToggleButton("vcGrid", "", artIcons + "grid.svg", [editor](bool on) {
        VectorGridSpec g = editor->canvas->GetGrid();
        g.visible = on;
        editor->canvas->SetGrid(g);
    })->SetTooltip("Show the grid");
    toolbar->AddToggleButton("vcSnap", "", artIcons + "snap.svg", [editor](bool on) {
        VectorSnapOptions s = editor->canvas->GetSnapOptions();
        s.toGrid = on; s.toObjects = on; s.toPage = on;
        editor->canvas->SetSnapOptions(s);
    })->SetTooltip("Snap to the grid, other objects and the page");
    container->AddChild(toolbar);

    // The canvas is built WITH its box. SetBounds() on a sizeless element
    // only moves finalBounds: the next layout pass gives an in-flow child
    // with no CSS width/height a zero height, and the drawing area vanishes.
    auto canvas = CreateVectorCanvas("VCCanvas", 10, 130, 720, 600);
    VectorGridSpec grid;
    grid.visible = false; grid.spacing = 20; grid.subdivisions = 2;
    canvas->SetGrid(grid);
    VectorSnapOptions snap;
    snap.toGuides = true; snap.toGrid = false; snap.toObjects = false;
    canvas->SetSnapOptions(snap);
    canvas->SetRulerUnit(1.0, "pt");
    container->AddChild(canvas);
    editor->canvas = canvas;

    auto panel = std::make_shared<UltraCanvasContainer>("VCPanel", 740, 130, 250, 600);
    panel->SetBackgroundColor(Color(255, 255, 255, 255));
    panel->SetBorders(1, Color(200, 200, 200, 255));

    auto fillTitle = std::make_shared<UltraCanvasLabel>("VCFillTitle", 10, 8, 230, 22);
    fillTitle->SetText("Fill of the selected shape");
    fillTitle->SetFontWeight(FontWeight::Bold);
    fillTitle->SetFontSize(12);
    panel->AddChild(fillTitle);

    auto ramp = CreateGradientEditor("VCRamp", 10, 34, 230, 46);
    panel->AddChild(ramp);
    editor->ramp = ramp;

    auto rampHint = std::make_shared<UltraCanvasLabel>("VCRampHint", 10, 84, 230, 60);
    rampHint->SetText("Drag a stop to move it, double-click the strip to add one, drag a stop away to remove it. A flat fill becomes a linear gradient.");
    rampHint->SetFontSize(10);
    rampHint->SetTextColor(Color(90, 90, 90, 255));
    panel->AddChild(rampHint);

    auto layersTitle = std::make_shared<UltraCanvasLabel>("VCLayersTitle", 10, 160, 230, 22);
    layersTitle->SetText("What the framework provides");
    layersTitle->SetFontWeight(FontWeight::Bold);
    layersTitle->SetFontSize(12);
    panel->AddChild(layersTitle);

    auto info = std::make_shared<UltraCanvasLabel>("VCInfo", 10, 186, 230, 400);
    info->SetText(
            "UltraCanvasVectorCanvas\n"
            "  page, rulers, guides, grid, snapping,\n"
            "  selection handles, tool hooks\n\n"
            "VectorEdit\n"
            "  VectorSelection, VectorHistory,\n"
            "  VectorHitTester, transform / z-order /\n"
            "  group / align / duplicate / convert\n\n"
            "UltraCanvasBezierPath\n"
            "  nodes and handles, FromPolyline\n\n"
            "UltraCanvasGradientEditor\n"
            "  the stops of a fill\n\n"
            "This page's tools are ~250 lines of\n"
            "application code over that.");
    info->SetFontSize(10);
    info->SetTextColor(Color(60, 60, 60, 255));
    panel->AddChild(info);
    container->AddChild(panel);

    auto status = std::make_shared<UltraCanvasLabel>("VCStatus", 10, 738, 980, 24);
    status->SetFontSize(11);
    status->SetTextColor(Color(60, 60, 60, 255));
    status->SetBackgroundColor(Color(230, 230, 230, 255));
    container->AddChild(status);
    editor->status = status;

    editor->Attach();
    editor->SetTool(DemoVectorEditor::Tool::Select);
    return container;
}

} // namespace UltraCanvas
