// core/DataFormats/UltraCanvasVectorEdit.cpp
// The editing layer over VectorStorage::VectorDocument: see the header.
// Version: 1.0.0
// Last Modified: 2026-09-15
// Author: UltraCanvas Framework

#include "DataFormats/UltraCanvasVectorEdit.h"
#include "DataFormats/UltraCanvasVectorPathOps.h"
#include "DataFormats/UltraCanvasVectorRenderer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <map>
#include <set>

namespace UltraCanvas {
namespace VectorEdit {

using namespace VectorStorage;

namespace {
    bool EmptyBox(const Rect2Dd& b) { return b.width <= 0 && b.height <= 0 && b.x == 0 && b.y == 0; }

    Rect2Dd UnionRect(const Rect2Dd& a, const Rect2Dd& b) {
        if (EmptyBox(a)) return b;
        if (EmptyBox(b)) return a;
        const double x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
        const double x1 = std::max(a.x + a.width, b.x + b.width);
        const double y1 = std::max(a.y + a.height, b.y + b.height);
        return Rect2Dd(x0, y0, x1 - x0, y1 - y0);
    }

    bool HasFill(const VectorStyle& style) {
        return style.Fill.has_value() && !std::holds_alternative<std::monostate>(style.Fill.value());
    }

    bool HasStroke(const VectorStyle& style) {
        return style.Stroke.has_value() && style.Stroke->Width > 0 &&
               !std::holds_alternative<std::monostate>(style.Stroke->Fill);
    }

    void EraseFromParent(const ElementPtr& element) {
        auto parent = ParentOf(element);
        if (!parent) return;
        auto& kids = parent->Children;
        kids.erase(std::remove(kids.begin(), kids.end(), element), kids.end());
        element->Parent.reset();
    }

    void InsertChild(const std::shared_ptr<VectorGroup>& parent, const ElementPtr& element, int index) {
        if (!parent || !element) return;
        if (index < 0 || index > static_cast<int>(parent->Children.size())) parent->Children.push_back(element);
        else parent->Children.insert(parent->Children.begin() + index, element);
        element->Parent = parent;
    }

    // Composes a document-space matrix into the element: T' = P^-1 M P T.
    void ComposeDocumentMatrix(const ElementPtr& element, const Matrix3x3& M) {
        const Matrix3x3 P = ParentToDocument(element);
        const Matrix3x3 T = element->Transform.value_or(Matrix3x3::Identity());
        Matrix3x3 result = P.Inverse() * M * P * T;
        if (result.IsIdentity()) element->Transform.reset();
        else element->Transform = result;
    }

    void TransformPathData(PathData& data, const Matrix3x3& m) {
        // Normalise first so every coordinate is absolute, then map.
        auto segs = VectorConverter::PathOps::NormalizePath(data);
        for (auto& s : segs)
            for (auto& p : s.p) p = m.Transform(p);
        data = VectorConverter::PathOps::SegsToPathData(segs);
    }
}

// ===========================================================================
// TREE HELPERS
// ===========================================================================

std::string GenerateId(const std::string& prefix) {
    static std::atomic<unsigned long long> counter{1};
    return prefix + "_" + std::to_string(counter++);
}

void EnsureIds(VectorDocument& doc) {
    std::set<std::string> seen;
    std::function<void(const ElementPtr&)> visit = [&](const ElementPtr& e) {
        if (!e) return;
        if (e->Id.empty() || seen.count(e->Id)) {
            std::string id;
            do { id = GenerateId(e->Type == VectorElementType::Layer ? "layer" : "el"); } while (seen.count(id));
            e->Id = id;
        }
        seen.insert(e->Id);
        if (auto* g = dynamic_cast<VectorGroup*>(e.get()))
            for (const auto& c : g->Children) visit(c);
    };
    for (const auto& layer : doc.Layers) visit(layer);
}

std::shared_ptr<VectorGroup> ParentOf(const ElementPtr& element) {
    return element ? element->Parent.lock() : nullptr;
}

int IndexInParent(const ElementPtr& element) {
    auto parent = ParentOf(element);
    if (!parent) return -1;
    auto it = std::find(parent->Children.begin(), parent->Children.end(), element);
    return it == parent->Children.end() ? -1 : static_cast<int>(it - parent->Children.begin());
}

std::shared_ptr<VectorLayer> LayerOf(const ElementPtr& element) {
    ElementPtr cur = element;
    while (cur) {
        if (cur->Type == VectorElementType::Layer) return std::dynamic_pointer_cast<VectorLayer>(cur);
        cur = ParentOf(cur);
    }
    return nullptr;
}

ElementPtr TopLevelOf(const ElementPtr& element) {
    ElementPtr cur = element;
    while (cur) {
        auto parent = ParentOf(cur);
        if (!parent || parent->Type == VectorElementType::Layer) return cur;
        cur = parent;
    }
    return nullptr;
}

Matrix3x3 ParentToDocument(const ElementPtr& element) {
    auto parent = ParentOf(element);
    return parent ? parent->GetGlobalTransform() : Matrix3x3::Identity();
}

Rect2Dd DocumentBounds(const ElementPtr& element) {
    if (!element) return Rect2Dd(0, 0, 0, 0);
    const Rect2Dd local = element->GetBoundingBox();    // parent space
    if (EmptyBox(local)) return local;
    return ParentToDocument(element).Transform(local);
}

std::vector<ElementPtr> AllElements(const VectorDocument& doc, bool includeLayers) {
    std::vector<ElementPtr> out;
    std::function<void(const ElementPtr&)> visit = [&](const ElementPtr& e) {
        if (!e) return;
        if (e->Type != VectorElementType::Layer || includeLayers) out.push_back(e);
        if (auto* g = dynamic_cast<VectorGroup*>(e.get()))
            for (const auto& c : g->Children) visit(c);
    };
    for (const auto& layer : doc.Layers) visit(layer);
    return out;
}

bool Detach(const ElementPtr& element) {
    if (!element || !ParentOf(element)) return false;
    EraseFromParent(element);
    return true;
}

// ===========================================================================
// SELECTION
// ===========================================================================

bool VectorSelection::Contains(const ElementPtr& element) const {
    return std::find(elements.begin(), elements.end(), element) != elements.end();
}

void VectorSelection::Set(const ElementPtr& element) {
    elements.clear();
    if (element) elements.push_back(element);
    Notify();
}

void VectorSelection::Set(const std::vector<ElementPtr>& list) {
    elements.clear();
    for (const auto& e : list) if (e && !Contains(e)) elements.push_back(e);
    Notify();
}

void VectorSelection::Add(const ElementPtr& element) {
    if (!element || Contains(element)) return;
    elements.push_back(element);
    Notify();
}

void VectorSelection::Remove(const ElementPtr& element) {
    auto it = std::find(elements.begin(), elements.end(), element);
    if (it == elements.end()) return;
    elements.erase(it);
    Notify();
}

void VectorSelection::Toggle(const ElementPtr& element) {
    if (Contains(element)) Remove(element); else Add(element);
}

void VectorSelection::Clear() {
    if (elements.empty()) return;
    elements.clear();
    Notify();
}

Rect2Dd VectorSelection::Bounds() const {
    Rect2Dd r(0, 0, 0, 0);
    for (const auto& e : elements) r = UnionRect(r, DocumentBounds(e));
    return r;
}

std::vector<std::string> VectorSelection::Ids() const {
    std::vector<std::string> ids;
    for (const auto& e : elements) ids.push_back(e->Id);
    return ids;
}

void VectorSelection::Rebind(const VectorDocument& doc) {
    std::vector<ElementPtr> fresh;
    for (const auto& e : elements) {
        if (!e || e->Id.empty()) continue;
        if (auto found = doc.FindElementById(e->Id)) fresh.push_back(found);
    }
    const bool changed = fresh.size() != elements.size() ||
                         !std::equal(fresh.begin(), fresh.end(), elements.begin());
    elements = fresh;
    if (changed) Notify();
}

int VectorSelection::AddListener(std::function<void()> listener) {
    const int id = nextListener++;
    listeners.emplace_back(id, std::move(listener));
    return id;
}

void VectorSelection::RemoveListener(int id) {
    listeners.erase(std::remove_if(listeners.begin(), listeners.end(),
                                   [id](const auto& l) { return l.first == id; }), listeners.end());
}

void VectorSelection::Notify() {
    auto copy = listeners;   // a listener may change the list
    for (auto& l : copy) if (l.second) l.second();
}

// ===========================================================================
// HISTORY
// ===========================================================================

VectorHistory::VectorHistory(std::shared_ptr<VectorDocument> document) : doc(std::move(document)) {}

void VectorHistory::SetDocument(std::shared_ptr<VectorDocument> document) {
    doc = std::move(document);
    undoStack.clear();
    redoStack.clear();
    pending.reset();
    editing = false;
    memoryUsed = 0;
}

size_t VectorHistory::EstimateBytes(const VectorDocument& doc) {
    size_t bytes = sizeof(VectorDocument);
    std::function<void(const VectorElement&)> visit = [&](const VectorElement& e) {
        bytes += 256;
        if (auto* p = dynamic_cast<const VectorPath*>(&e))
            for (const auto& c : p->Path.commands) bytes += 16 + c.Parameters.size() * sizeof(float);
        else if (auto* t = dynamic_cast<const VectorText*>(&e))
            for (const auto& s : t->Spans) bytes += 64 + s.Text.size();
        else if (auto* i = dynamic_cast<const VectorImage*>(&e))
            bytes += i->EmbeddedData.size();
        if (auto* g = dynamic_cast<const VectorGroup*>(&e))
            for (const auto& c : g->Children) if (c) visit(*c);
    };
    for (const auto& layer : doc.Layers) if (layer) visit(*layer);
    for (const auto& [id, def] : doc.Definitions) if (def) visit(*def);
    return bytes;
}

void VectorHistory::BeginEdit(const std::string& label) {
    if (!doc || editing) return;
    pending = doc->Clone();
    pendingLabel = label;
    editing = true;
}

void VectorHistory::EndEdit(bool coalesce) {
    if (!doc || !editing) return;
    editing = false;
    Entry entry;
    entry.label = pendingLabel;
    entry.before = pending;
    entry.after = doc->Clone();
    pending.reset();
    redoStack.clear();
    if (coalesce && !undoStack.empty() && undoStack.back().label == entry.label) {
        Entry& last = undoStack.back();
        memoryUsed -= last.bytes;
        last.after = entry.after;
        last.bytes = EstimateBytes(*last.before) + EstimateBytes(*last.after);
        memoryUsed += last.bytes;
    } else {
        entry.bytes = EstimateBytes(*entry.before) + EstimateBytes(*entry.after);
        memoryUsed += entry.bytes;
        undoStack.push_back(std::move(entry));
    }
    Trim();
    Fire();
}

void VectorHistory::CancelEdit() {
    if (!doc || !editing) return;
    editing = false;
    if (pending) Apply(*pending);
    pending.reset();
    Fire();
}

void VectorHistory::Record(const std::string& label, const std::function<void()>& edit, bool coalesce) {
    BeginEdit(label);
    if (edit) edit();
    EndEdit(coalesce);
}

void VectorHistory::Apply(const VectorDocument& snapshot) {
    if (!doc) return;
    auto fresh = snapshot.Clone();
    *doc = *fresh;   // the document object keeps its identity; its content is the snapshot's
}

void VectorHistory::Undo() {
    if (!doc || editing || undoStack.empty()) return;
    Entry entry = std::move(undoStack.back());
    undoStack.pop_back();
    Apply(*entry.before);
    redoStack.push_back(std::move(entry));
    Fire();
}

void VectorHistory::Redo() {
    if (!doc || editing || redoStack.empty()) return;
    Entry entry = std::move(redoStack.back());
    redoStack.pop_back();
    Apply(*entry.after);
    undoStack.push_back(std::move(entry));
    Fire();
}

void VectorHistory::Clear() {
    undoStack.clear();
    redoStack.clear();
    memoryUsed = 0;
    Fire();
}

void VectorHistory::Trim() {
    while (undoStack.size() > 1 && memoryUsed > memoryLimit) {
        memoryUsed -= undoStack.front().bytes;
        undoStack.pop_front();
    }
}

// ===========================================================================
// HIT TESTING
// ===========================================================================

VectorHitTester::VectorHitTester() : geometry(CreateRenderContext(Size2Di(4, 4), nullptr)) {}
VectorHitTester::~VectorHitTester() = default;

bool VectorHitTester::HitElement(const VectorElement& element, const Point2Dd& p, double tolerance,
                                 bool& onFill, bool& onStroke) const {
    onFill = onStroke = false;
    switch (element.Type) {
        case VectorElementType::Text:
        case VectorElementType::Image: {
            Rect2Dd b = element.GetBoundingBox();
            // GetBoundingBox includes the element's own Transform, but the
            // point is in the element's local space: undo it.
            if (element.Transform.has_value()) b = element.Transform->Inverse().Transform(b);
            onFill = p.x >= b.x - tolerance && p.x <= b.x + b.width + tolerance &&
                     p.y >= b.y - tolerance && p.y <= b.y + b.height + tolerance;
            return onFill;
        }
        default:
            break;
    }
    if (!geometry) return false;
    IRenderContext* ctx = geometry.get();
    ctx->PushState();
    ctx->ResetTransform();
    ctx->ClearPath();
    const bool hasOutline = BuildVectorElementOutline(ctx, element);
    if (!hasOutline) { ctx->ClearPath(); ctx->PopState(); return false; }
    if (HasFill(element.Style)) {
        ctx->SetFillRule(UltraCanvas::FillRule::NonZero);
        onFill = ctx->IsPointInFill(p.x, p.y);
    }
    const double strokeWidth = HasStroke(element.Style) ? element.Style.Stroke->Width : 0.0;
    ctx->SetStrokeWidth(std::max(strokeWidth, 0.0) + 2.0 * tolerance);
    ctx->SetLineDash(UCDashPattern());
    ctx->SetLineCap(LineCap::Round);
    ctx->SetLineJoin(LineJoin::Round);
    onStroke = ctx->IsPointInStroke(p.x, p.y);
    // An unfilled, unstroked outline (a construction shape) still hits on
    // its edge so it can be selected; an unfilled shape's interior does not.
    ctx->ClearPath();
    ctx->PopState();
    return onFill || onStroke;
}

std::optional<VectorHit> VectorHitTester::HitTest(const VectorDocument& doc, const Point2Dd& documentPoint,
                                                  double tolerance, bool skipLocked) const {
    std::optional<VectorHit> result;
    std::function<bool(const ElementPtr&, const Point2Dd&, double)> test =
        [&](const ElementPtr& e, const Point2Dd& parentPoint, double tol) -> bool {
            if (!e || !e->Style.Visible || !e->Style.Display) return false;
            // Into the element's own space.
            Point2Dd local = parentPoint;
            double localTol = tol;
            if (e->Transform.has_value()) {
                const Matrix3x3 inv = e->Transform->Inverse();
                local = inv.Transform(parentPoint);
                // Scale the tolerance by the transform's mean scale.
                const double sx = std::hypot(inv.m[0][0], inv.m[1][0]);
                const double sy = std::hypot(inv.m[0][1], inv.m[1][1]);
                localTol = tol * (sx + sy) / 2.0;
            }
            if (auto* g = dynamic_cast<VectorGroup*>(e.get())) {
                if (e->Type == VectorElementType::Layer) {
                    auto* layer = static_cast<VectorLayer*>(g);
                    if (!layer->Visible || (skipLocked && layer->Locked)) return false;
                }
                for (auto it = g->Children.rbegin(); it != g->Children.rend(); ++it)
                    if (test(*it, local, localTol)) return true;
                return false;
            }
            bool onFill = false, onStroke = false;
            if (!HitElement(*e, local, localTol, onFill, onStroke)) return false;
            VectorHit hit;
            hit.element = e;
            hit.topLevel = TopLevelOf(e);
            hit.layer = LayerOf(e);
            hit.onFill = onFill;
            hit.onStroke = onStroke;
            result = hit;
            return true;
        };
    for (auto it = doc.Layers.rbegin(); it != doc.Layers.rend(); ++it)
        if (test(*it, documentPoint, tolerance)) break;
    return result;
}

std::vector<ElementPtr> VectorHitTester::ElementsIn(const VectorDocument& doc, const Rect2Dd& r,
                                                    bool fullyInside, bool skipLocked) const {
    std::vector<ElementPtr> out;
    const double rx1 = r.x + r.width, ry1 = r.y + r.height;
    for (const auto& layer : doc.Layers) {
        if (!layer || !layer->Visible || (skipLocked && layer->Locked)) continue;
        for (const auto& child : layer->Children) {
            if (!child || !child->Style.Visible) continue;
            const Rect2Dd b = DocumentBounds(child);
            if (EmptyBox(b)) continue;
            const double bx1 = b.x + b.width, by1 = b.y + b.height;
            const bool inside = b.x >= r.x && b.y >= r.y && bx1 <= rx1 && by1 <= ry1;
            const bool overlaps = !(bx1 < r.x || b.x > rx1 || by1 < r.y || b.y > ry1);
            if (fullyInside ? inside : overlaps) out.push_back(child);
        }
    }
    return out;
}

// ===========================================================================
// OPERATIONS
// ===========================================================================

void TransformElements(const std::vector<ElementPtr>& elements, const Matrix3x3& M) {
    for (const auto& e : elements) if (e) ComposeDocumentMatrix(e, M);
}

void TranslateElements(const std::vector<ElementPtr>& elements, double dx, double dy) {
    TransformElements(elements, Matrix3x3::Translate(dx, dy));
}

void ScaleElements(const std::vector<ElementPtr>& elements, double sx, double sy, const Point2Dd& pivot) {
    const Matrix3x3 M = Matrix3x3::Translate(pivot.x, pivot.y) * Matrix3x3::Scale(sx, sy) *
                        Matrix3x3::Translate(-pivot.x, -pivot.y);
    TransformElements(elements, M);
}

void RotateElements(const std::vector<ElementPtr>& elements, double radians, const Point2Dd& pivot) {
    const Matrix3x3 M = Matrix3x3::Translate(pivot.x, pivot.y) * Matrix3x3::Rotate(radians) *
                        Matrix3x3::Translate(-pivot.x, -pivot.y);
    TransformElements(elements, M);
}

void SkewElements(const std::vector<ElementPtr>& elements, double radiansX, double radiansY, const Point2Dd& pivot) {
    const Matrix3x3 M = Matrix3x3::Translate(pivot.x, pivot.y) * Matrix3x3::SkewX(radiansX) *
                        Matrix3x3::SkewY(radiansY) * Matrix3x3::Translate(-pivot.x, -pivot.y);
    TransformElements(elements, M);
}

std::optional<PathData> OutlineOf(const VectorElement& element) {
    PathData d;
    if (!BuildOutlinePath(element, d)) return std::nullopt;
    return d;
}

std::shared_ptr<VectorPath> ConvertToPath(const ElementPtr& element) {
    if (!element) return nullptr;
    if (element->Type == VectorElementType::Path) return std::dynamic_pointer_cast<VectorPath>(element);
    auto outline = OutlineOf(*element);
    if (!outline) return nullptr;
    auto path = std::make_shared<VectorPath>();
    path->Path = *outline;
    path->Id = element->Id;
    path->Classes = element->Classes;
    path->Style = element->Style;
    path->Transform = element->Transform;
    auto parent = ParentOf(element);
    const int index = IndexInParent(element);
    if (parent) {
        EraseFromParent(element);
        InsertChild(parent, path, index);
    }
    return path;
}

ElementPtr BakeTransform(const ElementPtr& element) {
    if (!element || !element->Transform.has_value()) return element;
    const Matrix3x3 T = element->Transform.value();
    const bool axisAligned = std::fabs(T.m[0][1]) < 1e-12 && std::fabs(T.m[1][0]) < 1e-12 &&
                             T.m[0][0] > 0 && T.m[1][1] > 0;
    switch (element->Type) {
        case VectorElementType::Rectangle:
        case VectorElementType::RoundedRectangle:
            if (axisAligned) {
                auto& r = static_cast<VectorRect&>(*element);
                r.Bounds = T.Transform(r.Bounds);
                r.RadiusX *= static_cast<float>(T.m[0][0]);
                r.RadiusY *= static_cast<float>(T.m[1][1]);
                element->Transform.reset();
                return element;
            }
            break;
        case VectorElementType::Circle:
            if (axisAligned && std::fabs(T.m[0][0] - T.m[1][1]) < 1e-9) {
                auto& c = static_cast<VectorCircle&>(*element);
                c.Center = T.Transform(c.Center);
                c.Radius *= static_cast<float>(T.m[0][0]);
                element->Transform.reset();
                return element;
            }
            break;
        case VectorElementType::Ellipse:
            if (axisAligned) {
                auto& e = static_cast<VectorEllipse&>(*element);
                e.Center = T.Transform(e.Center);
                e.RadiusX *= static_cast<float>(T.m[0][0]);
                e.RadiusY *= static_cast<float>(T.m[1][1]);
                element->Transform.reset();
                return element;
            }
            break;
        case VectorElementType::Line: {
            auto& l = static_cast<VectorLine&>(*element);
            l.Start = T.Transform(l.Start);
            l.End = T.Transform(l.End);
            element->Transform.reset();
            return element;
        }
        case VectorElementType::Polyline: {
            auto& p = static_cast<VectorPolyline&>(*element);
            for (auto& pt : p.Points) pt = T.Transform(pt);
            element->Transform.reset();
            return element;
        }
        case VectorElementType::Polygon: {
            auto& p = static_cast<VectorPolygon&>(*element);
            for (auto& pt : p.Points) pt = T.Transform(pt);
            element->Transform.reset();
            return element;
        }
        case VectorElementType::Path: {
            auto& p = static_cast<VectorPath&>(*element);
            TransformPathData(p.Path, T);
            p.Path.InvalidateCache();
            element->Transform.reset();
            return element;
        }
        case VectorElementType::Group:
        case VectorElementType::Layer: {
            // Push the group's transform down into its children.
            auto* g = static_cast<VectorGroup*>(element.get());
            for (auto& c : g->Children) {
                if (!c) continue;
                c->Transform = T * c->Transform.value_or(Matrix3x3::Identity());
            }
            element->Transform.reset();
            return element;
        }
        default:
            return element;   // text, image: keep the transform
    }
    // A shape a transform cannot be written into stays a shape unless it
    // becomes a path.
    auto path = ConvertToPath(element);
    if (!path) return element;
    return BakeTransform(path);
}

std::vector<ElementPtr> SortByDrawingOrder(const std::vector<ElementPtr>& elements) {
    // Drawing order: layer index, then the index path down the tree.
    std::vector<std::pair<std::vector<int>, ElementPtr>> keyed;
    for (const auto& e : elements) {
        if (!e) continue;
        std::vector<int> key;
        ElementPtr cur = e;
        while (cur) {
            auto parent = ParentOf(cur);
            if (!parent) break;
            key.push_back(IndexInParent(cur));
            cur = parent;
        }
        // The layer's own position in the document is unknown here (a
        // layer has no parent); layers are compared by their pointers'
        // order in the caller's document, which drawing order preserves
        // within one layer - the common case for a selection.
        std::reverse(key.begin(), key.end());
        keyed.emplace_back(key, e);
    }
    std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<ElementPtr> out;
    for (auto& k : keyed) out.push_back(k.second);
    return out;
}

void ReorderElements(const std::vector<ElementPtr>& elements, ZOrderMove move) {
    const auto ordered = SortByDrawingOrder(elements);
    std::set<VectorElement*> moving;
    for (const auto& e : ordered) moving.insert(e.get());
    // Group by parent, keep relative order.
    std::map<VectorGroup*, std::vector<ElementPtr>> byParent;
    std::vector<VectorGroup*> parentsInOrder;
    for (const auto& e : ordered) {
        auto parent = ParentOf(e);
        if (!parent) continue;
        if (!byParent.count(parent.get())) parentsInOrder.push_back(parent.get());
        byParent[parent.get()].push_back(e);
    }
    for (auto* parentRaw : parentsInOrder) {
        auto& kids = parentRaw->Children;
        const auto& group = byParent[parentRaw];
        switch (move) {
            case ZOrderMove::ToFront: {
                for (const auto& e : group) kids.erase(std::remove(kids.begin(), kids.end(), e), kids.end());
                for (const auto& e : group) kids.push_back(e);
                break;
            }
            case ZOrderMove::ToBack: {
                for (const auto& e : group) kids.erase(std::remove(kids.begin(), kids.end(), e), kids.end());
                kids.insert(kids.begin(), group.begin(), group.end());
                break;
            }
            case ZOrderMove::Forward: {
                // From the top down: swap each moving element with the next
                // non-moving one above it.
                for (int i = static_cast<int>(kids.size()) - 2; i >= 0; --i) {
                    if (!moving.count(kids[i].get())) continue;
                    if (moving.count(kids[i + 1].get())) continue;
                    std::swap(kids[i], kids[i + 1]);
                }
                break;
            }
            case ZOrderMove::Backward: {
                for (size_t i = 1; i < kids.size(); ++i) {
                    if (!moving.count(kids[i].get())) continue;
                    if (moving.count(kids[i - 1].get())) continue;
                    std::swap(kids[i], kids[i - 1]);
                }
                break;
            }
        }
    }
}

void ReparentElement(const ElementPtr& element, const std::shared_ptr<VectorGroup>& newParent, int index) {
    if (!element || !newParent) return;
    // Keep the document-space placement: T' = Pnew^-1 * Pold * T.
    const Matrix3x3 Pold = ParentToDocument(element);
    const Matrix3x3 Pnew = newParent->GetGlobalTransform();
    const Matrix3x3 T = element->Transform.value_or(Matrix3x3::Identity());
    EraseFromParent(element);
    Matrix3x3 result = Pnew.Inverse() * Pold * T;
    if (result.IsIdentity()) element->Transform.reset();
    else element->Transform = result;
    InsertChild(newParent, element, index);
}

std::shared_ptr<VectorGroup> GroupElements(const std::vector<ElementPtr>& elements) {
    const auto ordered = SortByDrawingOrder(elements);
    if (ordered.empty()) return nullptr;
    const ElementPtr top = ordered.back();
    auto parent = ParentOf(top);
    if (!parent) return nullptr;
    const int index = IndexInParent(top);
    auto group = std::make_shared<VectorGroup>();
    group->Id = GenerateId("group");
    // Insert the group where the topmost member was, then move members in.
    InsertChild(parent, group, index + 1);
    for (const auto& e : ordered) ReparentElement(e, group, -1);
    return group;
}

std::vector<ElementPtr> UngroupElements(const std::vector<ElementPtr>& elements) {
    std::vector<ElementPtr> out;
    for (const auto& e : elements) {
        if (!e) continue;
        if (e->Type != VectorElementType::Group && e->Type != VectorElementType::Symbol) { out.push_back(e); continue; }
        auto group = std::dynamic_pointer_cast<VectorGroup>(e);
        auto parent = ParentOf(e);
        if (!parent) { out.push_back(e); continue; }
        int index = IndexInParent(e);
        std::vector<ElementPtr> kids = group->Children;
        for (const auto& k : kids) {
            ReparentElement(k, parent, index++);
            out.push_back(k);
        }
        EraseFromParent(e);
    }
    return out;
}

void DeleteElements(const std::vector<ElementPtr>& elements) {
    for (const auto& e : elements) if (e) EraseFromParent(e);
}

std::vector<ElementPtr> DuplicateElements(const std::vector<ElementPtr>& elements, double dx, double dy) {
    std::vector<ElementPtr> clones;
    for (const auto& e : SortByDrawingOrder(elements)) {
        auto parent = ParentOf(e);
        if (!parent) continue;
        auto clone = e->Clone();
        std::function<void(const ElementPtr&)> freshIds = [&](const ElementPtr& x) {
            x->Id = GenerateId(x->Type == VectorElementType::Group ? "group" : "el");
            if (auto* g = dynamic_cast<VectorGroup*>(x.get()))
                for (auto& c : g->Children) if (c) { c->Parent = std::dynamic_pointer_cast<VectorGroup>(x); freshIds(c); }
        };
        freshIds(clone);
        InsertChild(parent, clone, IndexInParent(e) + 1);
        if (dx != 0 || dy != 0) TranslateElements({clone}, dx, dy);
        clones.push_back(clone);
    }
    return clones;
}

void AlignElements(const std::vector<ElementPtr>& elements, AlignMode mode, const std::optional<Rect2Dd>& reference) {
    if (elements.empty()) return;
    Rect2Dd ref;
    if (reference) ref = *reference;
    else {
        ref = Rect2Dd(0, 0, 0, 0);
        for (const auto& e : elements) ref = UnionRect(ref, DocumentBounds(e));
    }
    for (const auto& e : elements) {
        const Rect2Dd b = DocumentBounds(e);
        if (EmptyBox(b)) continue;
        double dx = 0, dy = 0;
        switch (mode) {
            case AlignMode::Left: dx = ref.x - b.x; break;
            case AlignMode::HorizontalCenter: dx = (ref.x + ref.width / 2) - (b.x + b.width / 2); break;
            case AlignMode::Right: dx = (ref.x + ref.width) - (b.x + b.width); break;
            case AlignMode::Top: dy = ref.y - b.y; break;
            case AlignMode::VerticalCenter: dy = (ref.y + ref.height / 2) - (b.y + b.height / 2); break;
            case AlignMode::Bottom: dy = (ref.y + ref.height) - (b.y + b.height); break;
        }
        if (dx != 0 || dy != 0) TranslateElements({e}, dx, dy);
    }
}

void DistributeElements(const std::vector<ElementPtr>& elements, DistributeMode mode) {
    if (elements.size() < 3) return;
    const bool horizontal = mode == DistributeMode::HorizontalCenters || mode == DistributeMode::HorizontalGaps;
    const bool gaps = mode == DistributeMode::HorizontalGaps || mode == DistributeMode::VerticalGaps;
    std::vector<std::pair<Rect2Dd, ElementPtr>> items;
    for (const auto& e : elements) {
        const Rect2Dd b = DocumentBounds(e);
        if (!EmptyBox(b)) items.emplace_back(b, e);
    }
    if (items.size() < 3) return;
    std::sort(items.begin(), items.end(), [&](const auto& a, const auto& b) {
        return horizontal ? a.first.x < b.first.x : a.first.y < b.first.y;
    });
    auto pos = [&](const Rect2Dd& r) { return horizontal ? r.x : r.y; };
    auto size = [&](const Rect2Dd& r) { return horizontal ? r.width : r.height; };
    const Rect2Dd& first = items.front().first;
    const Rect2Dd& last = items.back().first;
    if (gaps) {
        double total = 0;
        for (const auto& it : items) total += size(it.first);
        const double span = (pos(last) + size(last)) - pos(first);
        const double gap = (span - total) / (items.size() - 1);
        double cursor = pos(first);
        for (auto& it : items) {
            const double delta = cursor - pos(it.first);
            if (std::fabs(delta) > 1e-9) TranslateElements({it.second}, horizontal ? delta : 0, horizontal ? 0 : delta);
            cursor += size(it.first) + gap;
        }
    } else {
        const double c0 = pos(first) + size(first) / 2;
        const double c1 = pos(last) + size(last) / 2;
        const double step = (c1 - c0) / (items.size() - 1);
        for (size_t i = 1; i + 1 < items.size(); ++i) {
            const double target = c0 + step * i;
            const double delta = target - (pos(items[i].first) + size(items[i].first) / 2);
            if (std::fabs(delta) > 1e-9) TranslateElements({items[i].second}, horizontal ? delta : 0, horizontal ? 0 : delta);
        }
    }
}

} // namespace VectorEdit
} // namespace UltraCanvas
