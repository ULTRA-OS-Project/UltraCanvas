// include/UltraCanvasBezierPath.h
// The editing model of a Bézier path: anchors that own their two handles,
// grouped into subpaths. VectorStorage::PathData is an instruction stream
// (relative / absolute, H / V, smooth shorthands, arcs) - right for files,
// miserable to edit. A shape editor converts a path into this on entry,
// edits nodes and handles under the node-type rules (corner / smooth /
// symmetric), and writes it back; every segment is a line or a cubic, so
// nothing here approximates. No UI, no image library.
//
// Proposed in Docs/UltraCanvas/UltraCanvasBezierEditorProposal.md; the
// storage side is VectorStorage::PathData in core, so there is one path
// type on disk and one in the editor.
// Version: 1.0.0
// Last Modified: 2026-09-15
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include "DataFormats/UltraCanvasVectorStorage.h"

#include <optional>
#include <string>
#include <vector>

namespace UltraCanvas {

// How a node's two handles relate. Moving one handle of a Smooth node
// rotates the other to stay opposite; of a Symmetric node, also to the same
// length; a Corner's handles are independent.
enum class BezierNodeType { Corner, Smooth, Symmetric };

struct BezierNode {
    Point2Dd anchor;                       // the on-curve point
    Point2Dd handleIn;                     // control point of the incoming segment
    Point2Dd handleOut;                    // control point of the outgoing segment
    BezierNodeType type = BezierNodeType::Corner;
    bool handleInActive = false;           // false: the incoming segment is straight at this end
    bool handleOutActive = false;          // false: the outgoing segment is straight at this end
};

class UltraCanvasBezierSubpath {
public:
    std::vector<BezierNode> nodes;
    bool closed = false;

    // Segments run node i -> node i+1; a closed subpath has one more, from
    // the last node back to the first.
    int SegmentCount() const;
    bool SegmentIsLine(int segment) const;
    // The cubic's four control points (a line is the degenerate cubic whose
    // controls sit on its ends).
    void SegmentControls(int segment, Point2Dd& p0, Point2Dd& p1, Point2Dd& p2, Point2Dd& p3) const;
    Point2Dd EvaluateSegment(int segment, double t) const;
    Point2Dd TangentAt(int segment, double t) const;

    Rect2Dd Bounds(bool includeHandles = false) const;
    std::vector<Point2Dd> Flatten(double tolerance = 0.25) const;
    double Length(double tolerance = 0.25) const;

    // ===== EDITING =====
    // Splits a segment at t, keeping the curve's shape (de Casteljau), and
    // returns the new node's index.
    int InsertNodeAt(int segment, double t);
    // Removes a node; its neighbours keep their outer handles.
    bool RemoveNode(int index);
    // Re-aligns the handles to the rule of the new type.
    void SetNodeType(int index, BezierNodeType type);
    // Moves the anchor and both handles with it.
    void MoveAnchor(int index, const Point2Dd& to);
    // Moves one handle; the other follows per the node type. Activates the
    // handle if it was straight.
    void MoveHandle(int index, bool outgoing, const Point2Dd& to);
    // Drags a point on a segment: the two handles of that segment move so
    // the curve at `t` follows the pointer (the shape editor's "drag the
    // line" gesture).
    void DragSegment(int segment, double t, const Point2Dd& delta);
    void Reverse();
    void Translate(double dx, double dy);

    // ===== QUERIES =====
    struct Hit {
        int segment = -1;
        double t = 0;
        double distance = 0;
        Point2Dd point;
    };
    std::optional<Hit> HitTestOutline(const Point2Dd& p, double maxDistance) const;
    bool ContainsPoint(const Point2Dd& p, bool evenOdd = false) const;

    void BuildPath(IRenderContext* ctx) const;
};

class UltraCanvasBezierPath {
public:
    std::vector<UltraCanvasBezierSubpath> subpaths;

    bool Empty() const { return subpaths.empty(); }
    int NodeCount() const;
    Rect2Dd Bounds(bool includeHandles = false) const;
    void Transform(const VectorStorage::Matrix3x3& m);
    void Translate(double dx, double dy);

    // ===== STORAGE INTEROP =====
    // Every command kind is accepted (PathOps normalises H / V, smooth,
    // quadratic and arc forms to lines and cubics on the way in); the
    // result writes back as absolute M / L / C / Z commands.
    static UltraCanvasBezierPath FromPathData(const VectorStorage::PathData& data);
    VectorStorage::PathData ToPathData() const;
    static std::optional<UltraCanvasBezierPath> FromSVGPathData(const std::string& d);
    std::string ToSVGPathData() const;

    // A polyline (a freehand stroke) as a smooth path: the points are
    // simplified with the Douglas-Peucker tolerance, then joined by cubics
    // whose handles follow Catmull-Rom tangents, so the curve passes
    // through the kept points. tolerance 0 keeps every point.
    static UltraCanvasBezierPath FromPolyline(const std::vector<Point2Dd>& points, bool closed,
                                              double tolerance = 1.0, double smoothing = 1.0 / 3.0);

    // ===== RENDER =====
    // Emits the geometry (MoveTo / LineTo / BezierCurveTo / ClosePath),
    // leaving fill and stroke to the caller.
    void BuildPath(IRenderContext* ctx) const;

    // ===== QUERIES =====
    struct Hit {
        int subpath = -1;
        int segment = -1;
        double t = 0;
        double distance = 0;
        Point2Dd point;
    };
    std::optional<Hit> HitTestOutline(const Point2Dd& p, double maxDistance) const;
    bool ContainsPoint(const Point2Dd& p, bool evenOdd = false) const;
    // The nearest node within maxDistance, as (subpath, node).
    std::optional<std::pair<int, int>> HitTestNode(const Point2Dd& p, double maxDistance) const;
};

// The points of a polyline reduced with the Douglas-Peucker algorithm.
std::vector<Point2Dd> SimplifyPolyline(const std::vector<Point2Dd>& points, double tolerance);

} // namespace UltraCanvas
