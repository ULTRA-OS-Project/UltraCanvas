// include/DataFormats/UltraCanvasVectorEdit.h
// The editing layer over VectorStorage::VectorDocument, with no UI in it:
// a selection (ordered elements, their document-space bounds, listeners),
// a history (labelled undo / redo by document snapshots under a memory
// budget), geometric hit testing (fill and stroke, through every ancestor
// transform, with a tolerance), and the operations an editor's commands
// are made of (transform about a pivot, z-order, group / ungroup, align /
// distribute, duplicate, delete, convert to path). UltraCanvasVectorCanvas
// displays a document and a selection; an application's tools call these.
//
// The raster counterpart is UCRasterDocument / UCRasterSelection; the
// split is the same: the model edits without a window, the element shows
// it, the application owns the tools.
// Version: 1.0.0
// Last Modified: 2026-09-15
// Author: UltraCanvas Framework
#pragma once

#include "DataFormats/UltraCanvasVectorStorage.h"
#include "UltraCanvasRenderContext.h"

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace VectorEdit {

using VectorStorage::VectorDocument;
using VectorStorage::VectorElement;
using VectorStorage::VectorGroup;
using VectorStorage::VectorLayer;
using VectorStorage::Matrix3x3;
using ElementPtr = std::shared_ptr<VectorElement>;

// ===== TREE HELPERS =====

// Gives every element in the document a unique, non-empty Id. Selection
// and history re-bind elements by Id after an undo, so an editor calls
// this once on a loaded document and on every element it creates.
void EnsureIds(VectorDocument& doc);
std::string GenerateId(const std::string& prefix = "el");

std::shared_ptr<VectorGroup> ParentOf(const ElementPtr& element);
int IndexInParent(const ElementPtr& element);
// The layer an element belongs to (itself, if it is one); null if detached.
std::shared_ptr<VectorLayer> LayerOf(const ElementPtr& element);
// The child of the layer that contains the element (the element itself
// when it sits directly in a layer) - what the selector picks by default.
ElementPtr TopLevelOf(const ElementPtr& element);
// parent space -> document space (every ancestor's Transform composed).
Matrix3x3 ParentToDocument(const ElementPtr& element);
// Bounding box in document space (the parent-space box mapped through the
// ancestors; a transformed box's bounding box, so conservative).
Rect2Dd DocumentBounds(const ElementPtr& element);
// All elements of the document in drawing order, layers included when
// `includeLayers` is set.
std::vector<ElementPtr> AllElements(const VectorDocument& doc, bool includeLayers = false);
// Removes the element from its parent's children (the layer list for a
// layer). Returns false if it had no parent.
bool Detach(const ElementPtr& element);

// ===== SELECTION =====

class VectorSelection {
public:
    const std::vector<ElementPtr>& Elements() const { return elements; }
    bool Empty() const { return elements.empty(); }
    size_t Count() const { return elements.size(); }
    bool Contains(const ElementPtr& element) const;
    ElementPtr First() const { return elements.empty() ? nullptr : elements.front(); }

    void Set(const ElementPtr& element);
    void Set(const std::vector<ElementPtr>& list);
    void Add(const ElementPtr& element);
    void Remove(const ElementPtr& element);
    void Toggle(const ElementPtr& element);
    void Clear();

    // Union of the elements' document-space bounds.
    Rect2Dd Bounds() const;
    std::vector<std::string> Ids() const;
    // After an undo / redo the document holds new element objects: the
    // selection finds them again by Id and drops the ones that are gone.
    void Rebind(const VectorDocument& doc);

    int AddListener(std::function<void()> listener);
    void RemoveListener(int id);

private:
    void Notify();
    std::vector<ElementPtr> elements;
    std::vector<std::pair<int, std::function<void()>>> listeners;
    int nextListener = 1;
};

// ===== HISTORY =====
// Undo / redo by document snapshots: BeginEdit() clones the document,
// EndEdit() clones it again and keeps the pair. Undo restores the "before"
// clone into the live document object (its identity - the shared_ptr the
// canvas holds - never changes; the elements inside are new objects, hence
// VectorSelection::Rebind). A drag calls BeginEdit() once on press and
// EndEdit() on release; a series of nudges passes coalesce = true so they
// merge into one entry. Whole-document snapshots are the simple, always-
// correct choice; a per-subtree scheme can replace them behind this same
// API when documents grow large enough to need it.
class VectorHistory {
public:
    explicit VectorHistory(std::shared_ptr<VectorDocument> document = nullptr);

    void SetDocument(std::shared_ptr<VectorDocument> document);   // clears the history
    std::shared_ptr<VectorDocument> GetDocument() const { return doc; }

    void BeginEdit(const std::string& label);
    // coalesce: merge with the previous entry when it has the same label,
    // so repeated nudges or a slider drag are one undo step.
    void EndEdit(bool coalesce = false);
    void CancelEdit();                    // restores the state at BeginEdit
    bool IsEditing() const { return editing; }
    // BeginEdit + edit() + EndEdit in one call.
    void Record(const std::string& label, const std::function<void()>& edit, bool coalesce = false);

    bool CanUndo() const { return !undoStack.empty(); }
    bool CanRedo() const { return !redoStack.empty(); }
    std::string UndoLabel() const { return undoStack.empty() ? "" : undoStack.back().label; }
    std::string RedoLabel() const { return redoStack.empty() ? "" : redoStack.back().label; }
    void Undo();
    void Redo();
    void Clear();
    size_t UndoCount() const { return undoStack.size(); }

    void SetMemoryLimit(size_t bytes) { memoryLimit = bytes; Trim(); }
    size_t GetMemoryLimit() const { return memoryLimit; }
    size_t MemoryUsed() const { return memoryUsed; }

    // Fired after EndEdit, Undo, Redo, CancelEdit and Clear: the document
    // changed under everyone holding it.
    std::function<void()> onChanged;

    // A rough size of a document's content, what the budget counts.
    static size_t EstimateBytes(const VectorDocument& doc);

private:
    struct Entry {
        std::string label;
        std::shared_ptr<VectorDocument> before;
        std::shared_ptr<VectorDocument> after;
        size_t bytes = 0;
    };
    void Apply(const VectorDocument& snapshot);
    void Trim();
    void Fire() { if (onChanged) onChanged(); }

    std::shared_ptr<VectorDocument> doc;
    std::deque<Entry> undoStack;
    std::deque<Entry> redoStack;
    std::shared_ptr<VectorDocument> pending;
    std::string pendingLabel;
    bool editing = false;
    size_t memoryLimit = 256u * 1024u * 1024u;
    size_t memoryUsed = 0;
};

// ===== HIT TESTING =====

struct VectorHit {
    ElementPtr element;      // the deepest element under the point
    ElementPtr topLevel;     // its layer-level ancestor (TopLevelOf)
    std::shared_ptr<VectorLayer> layer;
    bool onFill = false;
    bool onStroke = false;
};

// Geometric hit testing with a tolerance in document units. Fill tests use
// the element's fill rule; stroke tests use its stroke width grown by the
// tolerance; text and images test their boxes. Locked and hidden layers
// are skipped unless asked otherwise. Keeps a small geometry-only render
// context, so it works without a window.
class VectorHitTester {
public:
    VectorHitTester();
    ~VectorHitTester();

    std::optional<VectorHit> HitTest(const VectorDocument& doc, const Point2Dd& documentPoint,
                                     double tolerance, bool skipLocked = true) const;
    // Layer-level elements whose document bounds intersect (or, with
    // fullyInside, lie within) the rectangle, in drawing order.
    std::vector<ElementPtr> ElementsIn(const VectorDocument& doc, const Rect2Dd& documentRect,
                                       bool fullyInside, bool skipLocked = true) const;
    // One element in its own coordinate space.
    bool HitElement(const VectorElement& element, const Point2Dd& localPoint, double tolerance,
                    bool& onFill, bool& onStroke) const;

private:
    std::unique_ptr<IRenderContext> geometry;
};

// ===== OPERATIONS =====
// Every operation edits the model in place and returns what it made; the
// caller wraps it in VectorHistory::BeginEdit / EndEdit.

// Applies a document-space matrix to each element (about the document
// origin: compose with Translate for a pivot). The element's Transform
// absorbs it: T' = P^-1 * M * P * T, P being its parent-to-document map.
void TransformElements(const std::vector<ElementPtr>& elements, const Matrix3x3& documentMatrix);
void TranslateElements(const std::vector<ElementPtr>& elements, double dx, double dy);
void ScaleElements(const std::vector<ElementPtr>& elements, double sx, double sy, const Point2Dd& documentPivot);
void RotateElements(const std::vector<ElementPtr>& elements, double radians, const Point2Dd& documentPivot);
void SkewElements(const std::vector<ElementPtr>& elements, double radiansX, double radiansY, const Point2Dd& documentPivot);
// Replaces the element's Transform with the identity by writing the
// transform into its geometry (paths, polygons, lines; rects, circles and
// ellipses only for axis-aligned scaling and translation - others are
// converted to a path first). Returns the element that now holds the
// geometry (the same one, or its path replacement).
ElementPtr BakeTransform(const ElementPtr& element);

enum class ZOrderMove { ToFront, Forward, Backward, ToBack };
void ReorderElements(const std::vector<ElementPtr>& elements, ZOrderMove move);
// The selected elements' places in drawing order, sorted by z.
std::vector<ElementPtr> SortByDrawingOrder(const std::vector<ElementPtr>& elements);

// Groups the elements into a new VectorGroup in the parent of the topmost
// of them, at that element's z position. Elements from other parents are
// re-parented with their document-space placement preserved.
std::shared_ptr<VectorGroup> GroupElements(const std::vector<ElementPtr>& elements);
// Replaces every group in the list by its children (transforms composed);
// non-groups pass through. Returns the resulting elements.
std::vector<ElementPtr> UngroupElements(const std::vector<ElementPtr>& elements);
// Moves an element to another parent at an index (-1 = end), keeping its
// document-space placement.
void ReparentElement(const ElementPtr& element, const std::shared_ptr<VectorGroup>& newParent, int index = -1);

void DeleteElements(const std::vector<ElementPtr>& elements);
// Clones each element above its original, offset in document space; the
// clones get fresh Ids.
std::vector<ElementPtr> DuplicateElements(const std::vector<ElementPtr>& elements, double dx = 0, double dy = 0);

enum class AlignMode { Left, HorizontalCenter, Right, Top, VerticalCenter, Bottom };
// Aligns the elements' document bounds to the reference rectangle (the
// selection's own bounds when none is given, the page for "align to page").
void AlignElements(const std::vector<ElementPtr>& elements, AlignMode mode,
                   const std::optional<Rect2Dd>& reference = std::nullopt);
enum class DistributeMode { HorizontalCenters, VerticalCenters, HorizontalGaps, VerticalGaps };
void DistributeElements(const std::vector<ElementPtr>& elements, DistributeMode mode);

// The element's outline as a VectorPath with the same style and
// transform, replacing it in its parent. Text and images are left alone
// (returns null).
std::shared_ptr<VectorStorage::VectorPath> ConvertToPath(const ElementPtr& element);
// The outline geometry alone, in the element's own space (no replacement).
std::optional<VectorStorage::PathData> OutlineOf(const VectorElement& element);

} // namespace VectorEdit
} // namespace UltraCanvas
