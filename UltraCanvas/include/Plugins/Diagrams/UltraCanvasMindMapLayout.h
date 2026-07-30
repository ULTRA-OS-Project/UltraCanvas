// include/Plugins/Diagrams/UltraCanvasMindMapLayout.h
// Mind map topic model and layout engine
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework
//
// CHANGELOG 1.0.0 (initial):
//  - Topic tree model (MindMapTopic, MindMapModel) and the pure layout engine
//    that turns it into positioned boxes.
//  - Structures: Balanced (root centred, branches split left/right), LogicRight,
//    LogicLeft, OrgChartDown, OrgChartUp and Radial.
//  - Deterministic non-overlap packing of variable-size subtrees via bottom-up
//    extent accumulation (the "tidy tree" shape), so sibling subtrees never
//    collide at any depth.
//
// This header deliberately has NO dependency on IRenderContext or on
// UltraCanvasUIElement: the layout engine is pure geometry over the model and is
// unit-tested headlessly (Tests/MindMapLayoutTest.cpp). Text measurement is
// injected by the caller through MindMapMeasureFn so the engine never needs a
// render context.

#pragma once

#include "UltraCanvasCommonTypes.h"
#include "Plugins/Diagrams/UltraCanvasDiagramViewport.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <optional>

namespace UltraCanvas {

// =============================================================================
// ENUMERATIONS
// =============================================================================

enum class MindMapStructure {
    Balanced,      // Root centred, main topics split left and right (default)
    LogicRight,    // Whole tree grows right
    LogicLeft,     // Whole tree grows left
    OrgChartDown,  // Root at top, children below
    OrgChartUp,    // Root at bottom, children above
    Radial,        // Children distributed over a full 360 degrees
    Manual         // Authored positions honoured; unset ones filled in (L13)
};

enum class MindMapTopicSide {
    Auto,   // Layout decides (only meaningful for depth-1 topics)
    Left,
    Right
};

// How Auto-sided depth-1 topics are distributed in the Balanced structure.
enum class MindMapBalancePolicy {
    AlternateByIndex,     // Right, left, right, left...
    SplitInHalf,          // First half right, second half left
    MinimiseSubtreeWeight // Greedily assign to whichever side is lighter
};

enum class MindMapNodeShape {
    RoundedRect,
    Rect,
    Pill,
    Circle,
    Ellipse,
    Diamond,
    Hexagon,
    Cloud,
    Bang,
    Underline,   // Text with a rule beneath it, no box
    NoShape      // Text only. Not named "None" - X11 defines None as a macro.
};

// =============================================================================
// TOPIC
// =============================================================================

struct MindMapTopicStyle {
    MindMapNodeShape shape = MindMapNodeShape::RoundedRect;
    Color fillColor        = Color(255, 255, 255, 255);
    Color borderColor      = Color(70, 110, 180, 255);
    Color textColor        = Color(40, 40, 50, 255);
    double borderWidth     = 2.0;
    double cornerRadius    = 8.0;
    double fontSize        = 12.0;
    bool   bold            = false;
    double paddingX        = 12.0;
    double paddingY        = 6.0;
    double minWidth        = 0.0;
    double minHeight       = 0.0;
    double maxWidth        = 220.0;   // 0 disables wrapping
    bool   outlineOnly     = false;   // Transparent fill, coloured stroke and text
};

struct MindMapTopic {
    std::string id;
    std::string parentId;      // Empty for the central topic and floating topics
    std::string text;
    std::vector<std::string> childIds;

    MindMapTopicSide side = MindMapTopicSide::Auto;
    bool   collapsed      = false;
    double weight         = 1.0;      // Drives node size when sizeFromWeight is on

    std::string iconPath;             // Inline icon before the label
    std::string imagePath;            // Image as the node body
    std::string note;
    std::string link;
    // Small status/priority badges drawn on the node (C3). Values are icon
    // paths or short names, resolved the same way as iconPath.
    std::vector<std::string> markers;

    // Floating topics (no parent, placed by the app) keep their own position.
    bool   floating = false;
    double floatX   = 0.0;
    double floatY   = 0.0;

    // Manual placement (L13). Honoured by MindMapStructure::Manual; ignored by
    // every other structure, so a map can be switched back and forth without
    // losing the authored positions.
    bool   hasManualPosition = false;
    double manualX = 0.0;
    double manualY = 0.0;

    std::optional<MindMapTopicStyle> styleOverride;
    std::optional<MindMapStructure>  structureOverride;

    // ---- Computed by the layout engine (never set these by hand) ----
    Rect2Dd bounds;            // World-space box
    int     depth = 0;
    MindMapTopicSide resolvedSide = MindMapTopicSide::Right;
    int     branchIndex = -1;  // Index of the depth-1 ancestor; -1 for the root
    std::string ordinal;       // Outline number, e.g. "2.1" (C8)

    MindMapTopic() = default;
    MindMapTopic(const std::string& topicId, const std::string& topicText)
        : id(topicId), text(topicText) {}

    bool IsLeaf() const { return childIds.empty(); }
};

// Non-hierarchical connection between any two topics (D8).
struct MindMapRelationship {
    std::string id;
    std::string fromTopicId;
    std::string toTopicId;
    std::string label;
    Color color      = Color(120, 120, 130, 255);
    double lineWidth = 1.5;
    bool dashed      = true;
    bool arrowAtEnd  = true;

    MindMapRelationship() = default;
    MindMapRelationship(const std::string& relId,
                        const std::string& from, const std::string& to)
        : id(relId), fromTopicId(from), toTopicId(to) {}
};

// =============================================================================
// MODEL
// =============================================================================

// Owns the topic tree. Pure data plus tree bookkeeping - no rendering, no
// events, no styling decisions.
class MindMapModel {
public:
    // ---- Construction ----
    std::string SetCentralTopic(const std::string& text);
    std::string AddTopic(const std::string& parentId, const std::string& text);
    bool AddTopic(const MindMapTopic& topic);
    std::string AddFloatingTopic(const std::string& text, double x, double y);

    // Removes the topic and its whole subtree. Returns the removed ids.
    std::vector<std::string> RemoveTopic(const std::string& id);

    // Reparents a subtree. index < 0 appends. Rejects cycles (moving a topic
    // under one of its own descendants) and returns false.
    bool MoveTopic(const std::string& id, const std::string& newParentId, int index = -1);

    // Reorders within the current parent. Returns false if already at the end.
    bool MoveTopicToIndex(const std::string& id, int index);

    void Clear();

    // ---- Access ----
    MindMapTopic* GetTopic(const std::string& id);
    const MindMapTopic* GetTopic(const std::string& id) const;
    const std::string& GetRootId() const { return rootId; }
    bool IsEmpty() const { return topics.empty(); }
    size_t GetTopicCount() const { return topics.size(); }

    std::unordered_map<std::string, MindMapTopic>& Topics() { return topics; }
    const std::unordered_map<std::string, MindMapTopic>& Topics() const { return topics; }

    const std::vector<std::string>& FloatingTopicIds() const { return floatingIds; }

    // ---- Queries ----
    // Root-to-topic id chain, inclusive. Empty when the id is unknown.
    std::vector<std::string> GetPath(const std::string& id) const;
    // Pre-order ids of the subtree rooted at id, inclusive.
    std::vector<std::string> GetSubtreeIds(const std::string& id) const;
    bool IsDescendantOf(const std::string& id, const std::string& ancestorId) const;
    int GetDepth(const std::string& id) const;
    // Counts every descendant, collapsed or not.
    int CountDescendants(const std::string& id) const;
    // True when the topic or any ancestor is collapsed (so it must not render).
    bool IsHiddenByCollapse(const std::string& id) const;

    // Pre-order walk from the root. Visiting a collapsed topic does not descend
    // when skipCollapsed is true.
    void ForEachTopic(const std::function<void(MindMapTopic&)>& fn, bool skipCollapsed = false);
    void ForEachTopic(const std::function<void(const MindMapTopic&)>& fn, bool skipCollapsed = false) const;

    // ---- Relationships ----
    bool AddRelationship(const MindMapRelationship& relationship);
    bool RemoveRelationship(const std::string& id);
    std::vector<MindMapRelationship>& Relationships() { return relationships; }
    const std::vector<MindMapRelationship>& Relationships() const { return relationships; }

    // ---- Bulk build ----
    // Builds from (indentLevel, text) pairs. Level 0 becomes the central topic
    // when the model is empty. A level deeper than lastLevel+1 is clamped.
    void BuildFromOutline(const std::vector<std::pair<int, std::string>>& outline);

    // Assigns outline numbers ("1", "1.2", "1.2.3") to every topic (C8).
    void AssignOrdinals();

    // ---- Id generation ----
    std::string GenerateId(const std::string& prefix = "topic");

    // Rebuilds rootId, the floating-topic list and the id counter from the
    // topic map. Used after a deserializer has populated Topics() directly,
    // where the normal Add* path cannot run because parents may be forward
    // references. Drops any topic whose declared parent does not exist.
    void RestoreStructure(const std::string& declaredRootId);

private:
    void RemoveFromParent(const std::string& id);
    void CollectSubtree(const std::string& id, std::vector<std::string>& out) const;

    std::unordered_map<std::string, MindMapTopic> topics;
    std::vector<std::string> floatingIds;
    std::vector<MindMapRelationship> relationships;
    std::string rootId;
    int nextId = 1;
};

// =============================================================================
// LAYOUT
// =============================================================================

// Measures a label. Supplied by the caller so the engine never needs a render
// context. Returns the ink size in pixels, before padding.
using MindMapMeasureFn =
    std::function<Size2Dd(const std::string& text, double fontSize, bool bold, double maxWidth)>;

struct MindMapLayoutOptions {
    MindMapStructure structure = MindMapStructure::Balanced;
    MindMapBalancePolicy balancePolicy = MindMapBalancePolicy::AlternateByIndex;

    double siblingGap = 12.0;   // Vertical gap between sibling subtrees
    double levelGap   = 46.0;   // Horizontal gap between a parent and its children
    double rootGap    = 60.0;   // Gap either side of the central topic

    // Radial structure
    double radialStartAngle = -3.14159265358979323846 / 2.0;  // Straight up
    double radialSweep      = 2.0 * 3.14159265358979323846;   // Full circle
    double radialRingGap    = 130.0;

    // Node sizing
    bool   sizeFromWeight = false;
    double weightSizeScale = 18.0;  // Extra pixels per unit of weight above 1

    // Alignment (L12) - the infographic look where both sides line up.
    // Columns: every topic at the same depth sits the same distance from the
    // centre, instead of hugging its own parent.
    bool alignLevelColumns = false;
    // Rows: the Nth main topic on the left shares a vertical centre with the
    // Nth on the right. Balanced structure only.
    bool alignSideRows = false;
};

struct MindMapLayoutResult {
    // Positioned boxes for every laid-out topic, keyed by topic id. Topics
    // hidden by a collapsed ancestor are absent.
    std::unordered_map<std::string, Rect2Dd> bounds;
    DiagramContentBounds contentBounds;

    bool Has(const std::string& id) const { return bounds.find(id) != bounds.end(); }
    Rect2Dd Get(const std::string& id) const {
        auto it = bounds.find(id);
        return it == bounds.end() ? Rect2Dd(0, 0, 0, 0) : it->second;
    }
};

// Resolves the style that applies to a topic: per-topic override wins, then the
// level style, then the default. Supplied by the element.
using MindMapStyleFn = std::function<MindMapTopicStyle(const MindMapTopic&)>;

class UltraCanvasMindMapLayout {
public:
    // Computes positions for every visible topic. Writes results into the model
    // (each topic's bounds/depth/resolvedSide/branchIndex) AND into outResult.
    static void ComputeLayout(MindMapModel& model,
                              const MindMapLayoutOptions& options,
                              const MindMapMeasureFn& measure,
                              const MindMapStyleFn& styleOf,
                              MindMapLayoutResult& outResult);

    // Node box size for a topic at its resolved style, before positioning.
    static Size2Dd MeasureTopic(const MindMapTopic& topic,
                                const MindMapTopicStyle& style,
                                const MindMapLayoutOptions& options,
                                const MindMapMeasureFn& measure);

    // Conservative average-glyph-width estimate, used when no real measurement
    // function is available (matches the UltraCanvasNodeDiagram heuristic).
    static Size2Dd EstimateTextSize(const std::string& text, double fontSize,
                                    bool bold, double maxWidth);

private:
    struct LayoutNode {
        std::string id;
        Size2Dd size;
        double extent = 0.0;     // Cross-axis extent of this whole subtree
        std::vector<size_t> children;
        int depth = 0;
    };

    static void AssignSides(MindMapModel& model, const MindMapLayoutOptions& options);
    static double LeafWeight(const MindMapModel& model, const std::string& id);

    static void LayoutHorizontal(MindMapModel& model, const MindMapLayoutOptions& options,
                                 const MindMapMeasureFn& measure, const MindMapStyleFn& styleOf,
                                 MindMapLayoutResult& out);
    static void LayoutVertical(MindMapModel& model, const MindMapLayoutOptions& options,
                               const MindMapMeasureFn& measure, const MindMapStyleFn& styleOf,
                               MindMapLayoutResult& out);
    static void LayoutRadial(MindMapModel& model, const MindMapLayoutOptions& options,
                             const MindMapMeasureFn& measure, const MindMapStyleFn& styleOf,
                             MindMapLayoutResult& out);
};

} // namespace UltraCanvas
