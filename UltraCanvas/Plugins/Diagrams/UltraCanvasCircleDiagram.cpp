// Plugins/Diagrams/UltraCanvasCircleDiagram.cpp
// Hub-and-spoke circle diagram infographic implementation.
// Version: 1.0.0
// Last Modified: 2026-08-08
// Author: UltraCanvas Framework
#include "Plugins/Diagrams/UltraCanvasCircleDiagram.h"
#include "UltraCanvasTooltipManager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UltraCanvas {

// =============================================================================
// LOCAL HELPERS
// =============================================================================

    namespace {

        // Fraction of the chord between neighbouring nodes a node disc may take
        // when its radius is auto-fitted. Leaves a readable gap at every N.
        constexpr double kChordShare = 0.40;
        // Auto-fitted node radius never exceeds this fraction of the backbone
        // radius, or three nodes would produce discs that swallow the hub.
        constexpr double kMaxNodeRadiusFraction = 0.30;
        constexpr double kSatelliteRadiusFactor = 0.55;
        // Radial room an outside-placed node label needs, as a multiple of the
        // node radius: the label box is offset clear of the rim and then
        // reserves its own half-diagonal so a corner cannot cross the edge.
        constexpr double kOutsideLabelReach = 3.4;
        // Text box inside a disc, as a fraction of its diameter. The strict
        // inscribed square is 0.707 each way; a centred text block does not
        // reach its own corners (the first and last lines are short), so the
        // box is allowed to be a little wider than tall. Being stingy here is
        // what makes short labels ellipsize inside roomy discs.
        constexpr double kDiscTextWidthFactor  = 0.78;
        constexpr double kDiscTextHeightFactor = 0.72;

        Color ColorFromHSV(double h, double s, double v, uint8_t alpha = 255) {
            h = std::fmod(h, 360.0);
            if (h < 0.0) h += 360.0;
            s = std::max(0.0, std::min(1.0, s));
            v = std::max(0.0, std::min(1.0, v));

            double c = v * s;
            double x = c * (1.0 - std::fabs(std::fmod(h / 60.0, 2.0) - 1.0));
            double m = v - c;
            double r = 0.0, g = 0.0, b = 0.0;

            if      (h <  60.0) { r = c; g = x; }
            else if (h < 120.0) { r = x; g = c; }
            else if (h < 180.0) { g = c; b = x; }
            else if (h < 240.0) { g = x; b = c; }
            else if (h < 300.0) { r = x; b = c; }
            else                { r = c; b = x; }

            auto to8 = [](double v01) {
                return static_cast<uint8_t>(std::lround(std::max(0.0, std::min(1.0, v01)) * 255.0));
            };
            return Color(to8(r + m), to8(g + m), to8(b + m), alpha);
        }

        // Perceptual luminance, used to pick readable label colours over an
        // arbitrary palette fill.
        double Luminance(const Color& c) {
            return (0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b) / 255.0;
        }

        Color ReadableTextOn(const Color& fill) {
            return Luminance(fill) > 0.58 ? Color(38, 42, 56, 255)
                                          : Color(255, 255, 255, 255);
        }

        bool IsUnset(const Color& c) { return c.a == 0; }

        // Longest whitespace-delimited token. Word wrapping cannot break inside
        // one, so it is the token that decides whether a label can fit a box.
        std::string LongestWord(const std::string& text) {
            std::string longest, current;
            for (char c : text) {
                if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
                    if (current.size() > longest.size()) longest = current;
                    current.clear();
                } else {
                    current.push_back(c);
                }
            }
            if (current.size() > longest.size()) longest = current;
            return longest;
        }

        std::string TrimNumber(double value) {
            char buf[48];
            if (std::floor(value) == value && std::fabs(value) < 1e12) {
                std::snprintf(buf, sizeof(buf), "%.0f", value);
            } else {
                std::snprintf(buf, sizeof(buf), "%g", value);
            }
            return std::string(buf);
        }

    } // namespace

// =============================================================================
// CONSTRUCTION
// =============================================================================

    UltraCanvasCircleDiagram::UltraCanvasCircleDiagram(const std::string& id,
                                                       int x, int y, int w, int h)
        : UltraCanvasChartElementBase(id, x, y, w, h) {
        // The base class paints a plot-area rectangle and axis furniture that a
        // radial graphic never wants.
        showBackground = false;
        showGrid = false;
        showAxes = false;
        SetDesign(CircleDiagramDesign::SatelliteWheel);
    }

// =============================================================================
// DESIGN & PALETTE
// =============================================================================

    void UltraCanvasCircleDiagram::SetDesign(CircleDiagramDesign d) {
        design = d;

        // A design fills in defaults; every one of these stays individually
        // settable afterwards, so Custom is a starting point and not the only
        // escape hatch.
        switch (design) {
            case CircleDiagramDesign::SatelliteWheel:
                backboneStyle     = CircleBackboneStyle::Hairline;
                backboneThickness = 1.5f;
                backboneColor     = Color(170, 174, 186, 255);
                leaderStyle       = CircleLeaderStyle::Solid;
                leaderColor       = Color(150, 154, 166, 255);
                leaderWidth       = 1.0f;
                nodeLabelPlacement = CircleNodeLabelPlacement::Inside;
                nodeBorderWidth   = 0.0f;
                nodeFontWeight    = FontWeight::Bold;
                satelliteFanDeg   = 110.0f;
                satelliteOffset   = 14.0f;
                showHubDisc       = false;
                satelliteDefaultFill   = Color(255, 255, 255, 255);
                satelliteDefaultBorder = Color(176, 180, 190, 255);
                satelliteDefaultText   = Color(45, 48, 62, 255);
                break;

            case CircleDiagramDesign::BandedWheel:
                backboneStyle     = CircleBackboneStyle::Band;
                backboneThickness = 18.0f;
                backboneColor     = Color(23, 47, 74, 255);
                leaderStyle       = CircleLeaderStyle::Dotted;
                leaderColor       = Color(150, 154, 166, 255);
                leaderWidth       = 1.0f;
                nodeLabelPlacement = CircleNodeLabelPlacement::Inside;
                nodeBorderWidth   = 0.0f;
                nodeFontWeight    = FontWeight::Bold;
                // One satellite per node in this design reads as a straight
                // radial tag rather than a fan.
                satelliteFanDeg   = 0.0f;
                satelliteOffset   = 20.0f;
                showHubDisc       = false;
                satelliteDefaultFill   = Color(18, 26, 38, 255);
                satelliteDefaultBorder = Color(0, 0, 0, 0);
                satelliteDefaultText   = Color(240, 244, 250, 255);
                break;

            case CircleDiagramDesign::Custom:
            default:
                break;
        }
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasCircleDiagram::SetPalette(CircleDiagramPaletteKind kind) {
        paletteKind = kind;
        RequestRedraw();
    }

    void UltraCanvasCircleDiagram::SetCustomPalette(const std::vector<Color>& colors) {
        customPalette = colors;
        paletteKind = CircleDiagramPaletteKind::Custom;
        RequestRedraw();
    }

    Color UltraCanvasCircleDiagram::PaletteColor(size_t index, size_t total) const {
        if (paletteKind == CircleDiagramPaletteKind::Custom) {
            if (customPalette.empty()) return Color(120, 130, 150, 255);
            return customPalette[index % customPalette.size()];
        }

        if (total == 0) total = 1;
        // Every named palette is a ramp sampled at `total` points rather than a
        // fixed list, so the same palette stays coherent at any node count.
        double t = static_cast<double>(index) / static_cast<double>(total);

        switch (paletteKind) {
            case CircleDiagramPaletteKind::Vibrant:
                return ColorFromHSV(28.0 + t * 360.0, 0.74, 0.92);
            case CircleDiagramPaletteKind::Ocean:
                return ColorFromHSV(186.0 + t * 84.0, 0.52 + t * 0.22, 0.86 - t * 0.28);
            case CircleDiagramPaletteKind::Mint:
                return ColorFromHSV(150.0 + t * 36.0, 0.46 + t * 0.16, 0.84 - t * 0.10);
            case CircleDiagramPaletteKind::Pastel:
                return ColorFromHSV(15.0 + t * 360.0, 0.34, 0.97);
            case CircleDiagramPaletteKind::Midnight:
                return ColorFromHSV(212.0 + t * 46.0, 0.52, 0.44 - t * 0.10);
            case CircleDiagramPaletteKind::Dark:
                return ColorFromHSV(200.0 + t * 60.0, 0.30, 0.30 - t * 0.06);
            case CircleDiagramPaletteKind::Monochrome:
                return ColorFromHSV(0.0, 0.0, 0.34 + t * 0.42);
            default:
                break;
        }
        return Color(120, 130, 150, 255);
    }

// =============================================================================
// NODES
// =============================================================================

    size_t UltraCanvasCircleDiagram::AddNode(const CircleNode& node) {
        nodes.push_back(node);
        InvalidateCache();
        RequestRedraw();
        return nodes.size() - 1;
    }

    size_t UltraCanvasCircleDiagram::AddNode(const std::string& text) {
        return AddNode(CircleNode(text));
    }

    bool UltraCanvasCircleDiagram::SetNode(size_t index, const CircleNode& node) {
        if (index >= nodes.size()) return false;
        nodes[index] = node;
        InvalidateCache();
        RequestRedraw();
        return true;
    }

    const CircleNode* UltraCanvasCircleDiagram::GetNode(size_t index) const {
        if (index >= nodes.size()) return nullptr;
        return &nodes[index];
    }

    void UltraCanvasCircleDiagram::ClearNodes() {
        nodes.clear();
        hoveredNode = SIZE_MAX;
        hoveredSatellite = SIZE_MAX;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasCircleDiagram::SetNodeCount(size_t count) {
        // Clamp rather than degrade silently: below the minimum the ring reads
        // as a line, above the maximum labels collide at any radius.
        count = std::max(kMinNodeCount, std::min(kMaxNodeCount, count));
        nodes.resize(count);
        if (hoveredNode >= nodes.size()) {
            hoveredNode = SIZE_MAX;
            hoveredSatellite = SIZE_MAX;
        }
        InvalidateCache();
        RequestRedraw();
    }

    bool UltraCanvasCircleDiagram::UpdateNodeText(size_t index, const std::string& text) {
        if (index >= nodes.size()) return false;
        nodes[index].text = text;
        RequestRedraw();
        return true;
    }

// =============================================================================
// SATELLITES
// =============================================================================

    bool UltraCanvasCircleDiagram::AddSatellite(size_t nodeIndex,
                                                const CircleSatellite& satellite) {
        if (nodeIndex >= nodes.size()) return false;
        nodes[nodeIndex].satellites.push_back(satellite);
        InvalidateCache();
        RequestRedraw();
        return true;
    }

    bool UltraCanvasCircleDiagram::AddSatellite(size_t nodeIndex, const std::string& text) {
        return AddSatellite(nodeIndex, CircleSatellite(text));
    }

    size_t UltraCanvasCircleDiagram::GetSatelliteCount(size_t nodeIndex) const {
        if (nodeIndex >= nodes.size()) return 0;
        return nodes[nodeIndex].satellites.size();
    }

    const CircleSatellite* UltraCanvasCircleDiagram::GetSatellite(size_t nodeIndex,
                                                                  size_t satelliteIndex) const {
        if (nodeIndex >= nodes.size()) return nullptr;
        const auto& sats = nodes[nodeIndex].satellites;
        if (satelliteIndex >= sats.size()) return nullptr;
        return &sats[satelliteIndex];
    }

    bool UltraCanvasCircleDiagram::ClearSatellites(size_t nodeIndex) {
        if (nodeIndex >= nodes.size()) return false;
        nodes[nodeIndex].satellites.clear();
        InvalidateCache();
        RequestRedraw();
        return true;
    }

// =============================================================================
// HUB
// =============================================================================

    void UltraCanvasCircleDiagram::SetHubText(const std::string& text) {
        hubText = text;
        RequestRedraw();
    }

    void UltraCanvasCircleDiagram::SetHubIcon(const std::string& imagePath) {
        hubIcon = imagePath;
        RequestRedraw();
    }

    void UltraCanvasCircleDiagram::SetShowHubDisc(bool show) {
        showHubDisc = show;
        RequestRedraw();
    }

    void UltraCanvasCircleDiagram::SetHubColor(const Color& fill, const Color& border) {
        hubFillColor = fill;
        hubBorderColor = border;
        RequestRedraw();
    }

    void UltraCanvasCircleDiagram::SetHubTextColor(const Color& c) {
        hubTextColor = c;
        RequestRedraw();
    }

    void UltraCanvasCircleDiagram::SetHubRadiusFraction(float fraction) {
        hubRadiusFraction = std::max(0.05f, std::min(0.90f, fraction));
        RequestRedraw();
    }

// =============================================================================
// GEOMETRY CONFIGURATION
// =============================================================================

    void UltraCanvasCircleDiagram::SetNodeRadius(float pixels) {
        explicitNodeRadius = std::max(0.0f, pixels);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasCircleDiagram::SetSatelliteRadius(float pixels) {
        explicitSatelliteRadius = std::max(0.0f, pixels);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasCircleDiagram::SetSatelliteOffset(float pixels) {
        satelliteOffset = std::max(0.0f, pixels);
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasCircleDiagram::SetSatelliteFanAngle(float degrees) {
        satelliteFanDeg = std::max(0.0f, std::min(300.0f, degrees));
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasCircleDiagram::SetAngleOffset(float degrees) {
        angleOffsetDeg = degrees;
        RequestRedraw();
    }

    void UltraCanvasCircleDiagram::SetBackboneStyle(CircleBackboneStyle style) {
        backboneStyle = style;
        RequestRedraw();
    }

    void UltraCanvasCircleDiagram::SetBackboneThickness(float pixels) {
        backboneThickness = std::max(0.0f, pixels);
        RequestRedraw();
    }

    void UltraCanvasCircleDiagram::SetBackboneColor(const Color& c) {
        backboneColor = c;
        RequestRedraw();
    }

// =============================================================================
// APPEARANCE
// =============================================================================

    void UltraCanvasCircleDiagram::SetLeaderStyle(CircleLeaderStyle style) {
        leaderStyle = style;
        RequestRedraw();
    }

    void UltraCanvasCircleDiagram::SetLeaderColor(const Color& c) {
        leaderColor = c;
        RequestRedraw();
    }

    void UltraCanvasCircleDiagram::SetLeaderWidth(float pixels) {
        leaderWidth = std::max(0.0f, pixels);
        RequestRedraw();
    }

    void UltraCanvasCircleDiagram::SetNodeLabelPlacement(CircleNodeLabelPlacement placement) {
        nodeLabelPlacement = placement;
        InvalidateCache();
        RequestRedraw();
    }

    void UltraCanvasCircleDiagram::SetNodeFont(const std::string& family, float size,
                                               FontWeight weight) {
        nodeFontFamily = family;
        nodeFontSize = std::max(4.0f, size);
        nodeFontWeight = weight;
        RequestRedraw();
    }

    void UltraCanvasCircleDiagram::SetSatelliteFont(const std::string& family, float size,
                                                    FontWeight weight) {
        satelliteFontFamily = family;
        satelliteFontSize = std::max(4.0f, size);
        satelliteFontWeight = weight;
        RequestRedraw();
    }

    void UltraCanvasCircleDiagram::SetHubFont(const std::string& family, float size,
                                              FontWeight weight) {
        hubFontFamily = family;
        hubFontSize = std::max(4.0f, size);
        hubFontWeight = weight;
        RequestRedraw();
    }

    void UltraCanvasCircleDiagram::SetNodeBorderWidth(float pixels) {
        nodeBorderWidth = std::max(0.0f, pixels);
        RequestRedraw();
    }

    void UltraCanvasCircleDiagram::SetShowNodeIcons(bool show) {
        showNodeIcons = show;
        RequestRedraw();
    }

    void UltraCanvasCircleDiagram::SetHoverHighlightEnabled(bool on) {
        hoverHighlightEnabled = on;
        RequestRedraw();
    }

// =============================================================================
// LAYOUT
// =============================================================================

    size_t UltraCanvasCircleDiagram::MaxSatelliteCount() const {
        size_t most = 0;
        for (const auto& node : nodes) most = std::max(most, node.satellites.size());
        return most;
    }

    float UltraCanvasCircleDiagram::OuterReach() const {
        float reach = cachedNodeRadius;
        if (MaxSatelliteCount() > 0) {
            reach += satelliteOffset + 2.0f * cachedSatelliteRadius;
        }
        return reach;
    }

    ChartPlotArea UltraCanvasCircleDiagram::CalculatePlotArea() {
        // Only the title needs reserving here. Everything outside the backbone
        // - node discs and their satellite fans - is solved for inside
        // ComputeGeometry(), which shrinks the backbone radius so the whole
        // graphic including the fans fits the plot area.
        float marginSide   = 8.0f;
        float marginTop    = chartTitle.empty() ? 8.0f : 32.0f;
        float marginBottom = 8.0f;

        ChartPlotArea area;
        area.x = marginSide;
        area.y = marginTop;
        area.width  = std::max(40.0, static_cast<double>(GetWidth())  - 2.0 * marginSide);
        area.height = std::max(40.0, static_cast<double>(GetHeight()) - marginTop - marginBottom);
        return area;
    }

    void UltraCanvasCircleDiagram::ComputeGeometry() {
        cachedCenter = Point2Dd(cachedPlotArea.x + cachedPlotArea.width / 2.0,
                                cachedPlotArea.y + cachedPlotArea.height / 2.0);

        double available = std::min(cachedPlotArea.width, cachedPlotArea.height) * 0.5 - 2.0;
        available = std::max(16.0, available);

        size_t n = nodes.size();
        if (n == 0) {
            cachedBackboneRadius = static_cast<float>(available);
            cachedNodeRadius = 0.0f;
            cachedSatelliteRadius = 0.0f;
            return;
        }

        bool hasSatellites = MaxSatelliteCount() > 0;
        bool outsideLabels = (nodeLabelPlacement == CircleNodeLabelPlacement::Outside);

        if (explicitNodeRadius > 0.0f) {
            cachedNodeRadius = explicitNodeRadius;
            cachedSatelliteRadius = explicitSatelliteRadius > 0.0f
                                  ? explicitSatelliteRadius
                                  : static_cast<float>(cachedNodeRadius * kSatelliteRadiusFactor);
            double reach = cachedNodeRadius;
            if (hasSatellites) reach += satelliteOffset + 2.0 * cachedSatelliteRadius;
            if (outsideLabels) reach += kOutsideLabelReach * cachedNodeRadius + 6.0;
            cachedBackboneRadius = static_cast<float>(std::max(8.0, available - reach));
        } else {
            // Auto-fit. The node radius is a fraction k of the backbone radius:
            // the chord between neighbours is 2*R*sin(pi/n), and a disc may take
            // kChordShare of it, giving k = 2*kChordShare*sin(pi/n) - capped so
            // a 3-node diagram does not swallow the hub.
            double k = 2.0 * kChordShare * std::sin(M_PI / static_cast<double>(n));
            k = std::min(k, kMaxNodeRadiusFraction);

            // Outer extent = R + k*R + (offset + 2*satR) when satellites exist,
            // with satR = kSatelliteRadiusFactor * k * R when auto. Solving
            // R * (1 + k * (1 + 2*factor)) = available - offset.
            double satTerm = hasSatellites ? (2.0 * kSatelliteRadiusFactor) : 0.0;
            double offsetTerm = hasSatellites ? satelliteOffset : 0.0;
            if (explicitSatelliteRadius > 0.0f && hasSatellites) {
                // Satellite radius is fixed, so it leaves the ratio and joins
                // the constant term instead.
                satTerm = 0.0;
                offsetTerm += 2.0 * explicitSatelliteRadius;
            }
            // Labels parked outside the discs need reserving too, or they are
            // laid out past the element edge and clipped.
            if (outsideLabels) {
                satTerm += kOutsideLabelReach;
                offsetTerm += 6.0;
            }
            double denom = 1.0 + k * (1.0 + satTerm);
            double radius = (available - offsetTerm) / denom;
            radius = std::max(10.0, radius);

            cachedBackboneRadius = static_cast<float>(radius);
            cachedNodeRadius = static_cast<float>(std::max(6.0, k * radius));
            cachedSatelliteRadius = explicitSatelliteRadius > 0.0f
                                  ? explicitSatelliteRadius
                                  : static_cast<float>(std::max(4.0,
                                        cachedNodeRadius * kSatelliteRadiusFactor));
        }

        FitSatelliteFan();
    }

    void UltraCanvasCircleDiagram::FitSatelliteFan() {
        size_t n = nodes.size();
        size_t k = MaxSatelliteCount();
        cachedFanDeg = satelliteFanDeg;
        if (n == 0 || k == 0) return;

        // Each node owns a wedge of 2*pi/n; half of it, either side of its own
        // radius, is the room a fan may use before it reaches into the
        // neighbour's. Everything below is derived from that wedge rather than
        // from constants tuned to one node count.
        double halfWedge = M_PI / static_cast<double>(n);
        double orbit = cachedNodeRadius + satelliteOffset + cachedSatelliteRadius;

        // Lateral room at the satellites' orbit, leaving a 10% gutter between
        // one node's fan and the next.
        double lateral = 0.90 * (cachedBackboneRadius + orbit) * std::sin(halfWedge);

        // Shrink the satellite discs when k of them cannot fit that room side
        // by side. Only auto-sized satellites give way; an explicit radius is
        // the caller's decision and is left alone.
        if (explicitSatelliteRadius <= 0.0f) {
            double maxSatRadius = lateral / (1.1 * static_cast<double>(k));
            if (maxSatRadius < cachedSatelliteRadius) {
                cachedSatelliteRadius = static_cast<float>(std::max(3.0, maxSatRadius));
                orbit = cachedNodeRadius + satelliteOffset + cachedSatelliteRadius;
                lateral = 0.90 * (cachedBackboneRadius + orbit) * std::sin(halfWedge);
            }
        }

        // Widest fan whose outermost satellite still sits inside the wedge.
        double room = (lateral - cachedSatelliteRadius) / std::max(1.0, orbit);
        room = std::max(0.0, std::min(1.0, room));
        double fanMax = 2.0 * std::asin(room);

        double fan = std::min(satelliteFanDeg * M_PI / 180.0, fanMax);

        // With more than one satellite the fan must also be wide enough to keep
        // adjacent discs from overlapping each other along the arc.
        if (k > 1) {
            double needed = (2.2 * cachedSatelliteRadius * static_cast<double>(k - 1)) / orbit;
            fan = std::max(fan, std::min(needed, fanMax));
        }
        cachedFanDeg = static_cast<float>(fan * 180.0 / M_PI);
    }

    double UltraCanvasCircleDiagram::NodeAngle(size_t index) const {
        size_t n = nodes.size();
        if (n == 0) return 0.0;
        double per = 2.0 * M_PI / static_cast<double>(n);
        return angleOffsetDeg * M_PI / 180.0 + static_cast<double>(index) * per;
    }

    Point2Dd UltraCanvasCircleDiagram::NodeCenter(size_t index) const {
        double a = NodeAngle(index);
        return Point2Dd(cachedCenter.x + cachedBackboneRadius * std::cos(a),
                        cachedCenter.y + cachedBackboneRadius * std::sin(a));
    }

    Point2Dd UltraCanvasCircleDiagram::SatelliteCenter(size_t nodeIndex,
                                                        size_t satelliteIndex) const {
        if (nodeIndex >= nodes.size()) return cachedCenter;
        const auto& sats = nodes[nodeIndex].satellites;
        size_t k = sats.size();
        if (satelliteIndex >= k) return NodeCenter(nodeIndex);

        double outward = NodeAngle(nodeIndex);
        double fan = cachedFanDeg * M_PI / 180.0;
        // A single satellite sits straight out along the node's own radius; the
        // rest spread evenly across the fan, centred on that same direction.
        double angle = outward;
        if (k > 1) {
            angle = outward - fan * 0.5
                  + fan * (static_cast<double>(satelliteIndex) / static_cast<double>(k - 1));
        }

        double dist = cachedNodeRadius + satelliteOffset + cachedSatelliteRadius;
        Point2Dd nc = NodeCenter(nodeIndex);
        return Point2Dd(nc.x + dist * std::cos(angle), nc.y + dist * std::sin(angle));
    }

// =============================================================================
// COLOUR RESOLUTION
// =============================================================================

    Color UltraCanvasCircleDiagram::ResolveNodeFill(size_t index) const {
        if (index >= nodes.size()) return Color(120, 130, 150, 255);
        const CircleNode& node = nodes[index];
        if (!IsUnset(node.fillColor)) return node.fillColor;
        return PaletteColor(index, nodes.size());
    }

    Color UltraCanvasCircleDiagram::ResolveNodeText(size_t index) const {
        if (index >= nodes.size()) return Colors::White;
        const CircleNode& node = nodes[index];
        if (!IsUnset(node.textColor)) return node.textColor;
        return ReadableTextOn(ResolveNodeFill(index));
    }

    Color UltraCanvasCircleDiagram::ResolveSatelliteFill(size_t nodeIndex,
                                                          size_t satelliteIndex) const {
        const CircleSatellite* sat = GetSatellite(nodeIndex, satelliteIndex);
        if (!sat) return satelliteDefaultFill;
        if (!IsUnset(sat->fillColor)) return sat->fillColor;
        return satelliteDefaultFill;
    }

    Color UltraCanvasCircleDiagram::ResolveSatelliteText(size_t nodeIndex,
                                                          size_t satelliteIndex) const {
        const CircleSatellite* sat = GetSatellite(nodeIndex, satelliteIndex);
        if (sat && !IsUnset(sat->textColor)) return sat->textColor;
        Color fill = ResolveSatelliteFill(nodeIndex, satelliteIndex);
        // The design's default text colour is the intent when the fill is also
        // the design's; once a satellite carries its own fill, contrast wins.
        if (sat && !IsUnset(sat->fillColor)) return ReadableTextOn(fill);
        return satelliteDefaultText;
    }

// =============================================================================
// TOOLTIPS
// =============================================================================

    std::string UltraCanvasCircleDiagram::BuildNodeTooltip(size_t index) const {
        if (index >= nodes.size()) return std::string();
        const CircleNode& node = nodes[index];
        if (customNodeTooltipGenerator) return customNodeTooltipGenerator(node);
        if (!node.tooltip.empty()) return node.tooltip;

        std::string text = node.text.empty() ? std::string("(unnamed)") : node.text;
        if (node.value != 0.0) text += "\nValue: " + TrimNumber(node.value);
        if (!node.satellites.empty()) {
            text += "\n" + TrimNumber(static_cast<double>(node.satellites.size()))
                  + (node.satellites.size() == 1 ? " item" : " items");
        }
        return text;
    }

    std::string UltraCanvasCircleDiagram::BuildSatelliteTooltip(size_t nodeIndex,
                                                                size_t satelliteIndex) const {
        const CircleSatellite* sat = GetSatellite(nodeIndex, satelliteIndex);
        if (!sat) return std::string();
        if (!sat->tooltip.empty()) return sat->tooltip;

        std::string text;
        if (nodeIndex < nodes.size() && !nodes[nodeIndex].text.empty()) {
            text = nodes[nodeIndex].text + "\n";
        }
        text += sat->text.empty() ? std::string("(unnamed)") : sat->text;
        if (sat->value != 0.0) text += "\nValue: " + TrimNumber(sat->value);
        return text;
    }

// =============================================================================
// RENDERING
// =============================================================================

    void UltraCanvasCircleDiagram::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        if (!ctx) return;
        UpdateRenderingCache();

        if (nodes.empty()) {
            DrawEmptyState(ctx);
            return;
        }
        RenderChart(ctx);
    }

    void UltraCanvasCircleDiagram::RenderChart(IRenderContext* ctx) {
        if (!ctx) return;
        UpdateRenderingCache();
        ComputeGeometry();

        if (nodes.empty()) {
            DrawEmptyState(ctx);
            return;
        }

        if (backgroundColor.a > 0) {
            ctx->SetFillPaint(backgroundColor);
            ctx->FillRectangle(GetLocalBounds());
        }

        RenderTitle(ctx);
        DrawBackbone(ctx);
        DrawHub(ctx);

        // Leaders and satellites first so a node disc always paints over the
        // line that reaches it, then the nodes on top.
        for (size_t i = 0; i < nodes.size(); ++i) DrawSatellites(ctx, i);
        for (size_t i = 0; i < nodes.size(); ++i) DrawNode(ctx, i);
    }

    void UltraCanvasCircleDiagram::RenderTitle(IRenderContext* ctx) {
        if (chartTitle.empty()) return;
        ctx->PushState();
        ctx->SetFontFamily(hubFontFamily);
        ctx->SetFontSize(hubFontSize + 3.0f);
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetTextPaint(hubTextColor);
        Size2Di ts = ctx->GetTextLineDimensions(chartTitle);
        ctx->DrawText(chartTitle,
                      Point2Dd(cachedPlotArea.x + (cachedPlotArea.width - ts.width) / 2.0,
                               std::max(2.0, cachedPlotArea.y - ts.height - 6.0)));
        ctx->PopState();
    }

    void UltraCanvasCircleDiagram::DrawBackbone(IRenderContext* ctx) {
        if (backboneStyle == CircleBackboneStyle::NoRing) return;
        if (backboneThickness <= 0.0f || backboneColor.a == 0) return;

        ctx->PushState();
        ctx->SetLineDash(UCDashPattern());
        ctx->SetStrokePaint(backboneColor);
        ctx->SetStrokeWidth(backboneStyle == CircleBackboneStyle::Band
                            ? backboneThickness
                            : std::min(backboneThickness, 3.0f));
        ctx->DrawCircle(cachedCenter, cachedBackboneRadius);
        ctx->PopState();
    }

    void UltraCanvasCircleDiagram::DrawHub(IRenderContext* ctx) {
        double hubRadius = cachedBackboneRadius * hubRadiusFraction;
        if (hubRadius <= 2.0) return;

        if (showHubDisc) {
            ctx->DrawFilledCircle(cachedCenter, static_cast<float>(hubRadius),
                                  hubFillColor, hubBorderColor,
                                  hubBorderColor.a > 0 ? 1.5f : 0.0f);
        }
        if (hubText.empty() && hubIcon.empty()) return;

        DrawDiscLabel(ctx, hubText, cachedCenter, hubRadius, hubIcon, hubTextColor,
                      hubFontFamily, hubFontSize, hubFontWeight);
    }

    void UltraCanvasCircleDiagram::DrawNode(IRenderContext* ctx, size_t index) {
        Point2Dd center = NodeCenter(index);
        Color fill = ResolveNodeFill(index);

        bool hovered = hoverHighlightEnabled && hoveredNode == index &&
                       hoveredSatellite == SIZE_MAX;
        if (hovered) fill = fill.Lighten(0.18f);

        const CircleNode& node = nodes[index];
        Color border = IsUnset(node.borderColor) ? Colors::Transparent : node.borderColor;
        float borderW = nodeBorderWidth;
        if (hovered && borderW <= 0.0f) {
            border = fill.Darken(0.35f);
            borderW = 2.0f;
        }

        ctx->DrawFilledCircle(center, cachedNodeRadius, fill, border, borderW);

        if (nodeLabelPlacement == CircleNodeLabelPlacement::Inside) {
            DrawDiscLabel(ctx, node.text, center, cachedNodeRadius,
                          showNodeIcons ? node.iconImage : std::string(),
                          ResolveNodeText(index),
                          nodeFontFamily, nodeFontSize, nodeFontWeight);
            return;
        }

        // Outside placement: the label is parked just beyond the disc, on the
        // far side from the hub, in a box wide enough to wrap into.
        double a = NodeAngle(index);
        double boxW = std::max(48.0, cachedNodeRadius * 3.0);
        double boxH = std::max(24.0, cachedNodeRadius * 1.8);
        double dist = cachedNodeRadius + 6.0 + boxH * 0.5;
        Point2Dd anchor(center.x + dist * std::cos(a), center.y + dist * std::sin(a));

        ctx->PushState();
        ctx->SetFontFamily(nodeFontFamily);
        ctx->SetFontSize(nodeFontSize);
        ctx->SetFontWeight(nodeFontWeight);
        ctx->SetTextPaint(IsUnset(node.textColor) ? Color(45, 48, 62, 255) : node.textColor);
        ctx->SetTextWrap(TextWrap::WrapWord);
        ctx->SetTextAlignment(TextAlignment::Center);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        ctx->DrawTextInRect(node.text,
                            Rect2Dd(anchor.x - boxW * 0.5, anchor.y - boxH * 0.5, boxW, boxH));
        ctx->PopState();
    }

    void UltraCanvasCircleDiagram::DrawSatellites(IRenderContext* ctx, size_t nodeIndex) {
        if (nodeIndex >= nodes.size()) return;
        const auto& sats = nodes[nodeIndex].satellites;
        if (sats.empty()) return;

        Point2Dd nodeCenter = NodeCenter(nodeIndex);

        for (size_t s = 0; s < sats.size(); ++s) {
            Point2Dd satCenter = SatelliteCenter(nodeIndex, s);

            // Trim the leader to the two rims so it never shows through a disc.
            double dx = satCenter.x - nodeCenter.x;
            double dy = satCenter.y - nodeCenter.y;
            double len = std::sqrt(dx * dx + dy * dy);
            if (len > 1e-6) {
                double ux = dx / len, uy = dy / len;
                Point2Dd from(nodeCenter.x + ux * cachedNodeRadius,
                              nodeCenter.y + uy * cachedNodeRadius);
                Point2Dd to(satCenter.x - ux * cachedSatelliteRadius,
                            satCenter.y - uy * cachedSatelliteRadius);
                DrawLeader(ctx, from, to, leaderColor);
            }

            Color fill = ResolveSatelliteFill(nodeIndex, s);
            bool hovered = hoverHighlightEnabled && hoveredNode == nodeIndex &&
                           hoveredSatellite == s;
            if (hovered) fill = fill.Lighten(0.18f);

            const CircleSatellite& sat = sats[s];
            Color border = IsUnset(sat.borderColor) ? satelliteDefaultBorder : sat.borderColor;
            ctx->DrawFilledCircle(satCenter, cachedSatelliteRadius, fill, border,
                                  border.a > 0 ? 1.0f : 0.0f);

            DrawDiscLabel(ctx, sat.text, satCenter, cachedSatelliteRadius,
                          showNodeIcons ? sat.iconImage : std::string(),
                          ResolveSatelliteText(nodeIndex, s),
                          satelliteFontFamily, satelliteFontSize, satelliteFontWeight);
        }
    }

    void UltraCanvasCircleDiagram::ApplyLeaderDash(IRenderContext* ctx) const {
        switch (leaderStyle) {
            case CircleLeaderStyle::Dashed:
                ctx->SetLineDash(UCDashPattern({6.0, 4.0}));
                break;
            case CircleLeaderStyle::Dotted:
                ctx->SetLineDash(UCDashPattern({1.5, 3.5}));
                break;
            case CircleLeaderStyle::Solid:
            case CircleLeaderStyle::NoLeader:
            default:
                ctx->SetLineDash(UCDashPattern());
                break;
        }
    }

    void UltraCanvasCircleDiagram::DrawLeader(IRenderContext* ctx, const Point2Dd& from,
                                               const Point2Dd& to, const Color& color) {
        if (leaderStyle == CircleLeaderStyle::NoLeader) return;
        if (leaderWidth <= 0.0f || color.a == 0) return;

        ctx->PushState();
        ApplyLeaderDash(ctx);
        ctx->SetStrokePaint(color);
        ctx->SetStrokeWidth(leaderWidth);
        ctx->DrawLine(from, to);
        ctx->PopState();
    }

    void UltraCanvasCircleDiagram::DrawDiscLabel(IRenderContext* ctx, const std::string& text,
                                                  const Point2Dd& center, double radius,
                                                  const std::string& iconPath,
                                                  const Color& textColor,
                                                  const std::string& family, float size,
                                                  FontWeight weight) {
        if (text.empty() && iconPath.empty()) return;

        // Text wraps inside the disc's inscribed square, so a long label breaks
        // over several lines instead of spilling past the rim.
        double box = radius * 2.0 * kDiscTextWidthFactor;
        double boxHeight = radius * 2.0 * kDiscTextHeightFactor;
        double boxX = center.x - box * 0.5;
        double boxY = center.y - boxHeight * 0.5;

        double textTop = boxY;
        double textHeight = boxHeight;

        if (!iconPath.empty()) {
            // Icon takes the top of the box, the label the remainder.
            double iconSize = std::min(boxHeight * 0.42, radius * 0.7);
            if (text.empty()) {
                iconSize = std::min(box, radius * 1.2);
                ctx->DrawImage(iconPath,
                               Rect2Dd(center.x - iconSize * 0.5, center.y - iconSize * 0.5,
                                       iconSize, iconSize),
                               ImageFitMode::Contain);
                return;
            }
            ctx->DrawImage(iconPath,
                           Rect2Dd(center.x - iconSize * 0.5, boxY, iconSize, iconSize),
                           ImageFitMode::Contain);
            textTop = boxY + iconSize + 1.0;
            textHeight = boxHeight - iconSize - 1.0;
        }

        if (text.empty() || textHeight <= 2.0) return;

        ctx->PushState();
        ctx->SetFontFamily(family);
        ctx->SetFontWeight(weight);
        ctx->SetTextPaint(textColor);
        ctx->SetTextWrap(TextWrap::WrapWord);
        ctx->SetTextAlignment(TextAlignment::Center);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);

        // Shrink to fit: step the font down until the wrapped text fits the box
        // both ways, so a long label stays inside its disc at any node count.
        // The width test is on the longest single word, not on the wrapped
        // block: word wrapping already caps the block at the box width, and a
        // word too wide to break would silently ellipsize instead.
        std::string longestWord = LongestWord(text);
        float fontSize = size;
        const float minFontSize = 5.0f;
        while (true) {
            ctx->SetFontSize(fontSize);
            Size2Di block = ctx->GetTextDimensions(text, Size2Di(static_cast<int>(box), 0));
            Size2Di word = ctx->GetTextLineDimensions(longestWord);
            bool fits = block.height <= textHeight - 1.0 && word.width <= box - 1.0;
            if (fits || fontSize <= minFontSize) break;
            fontSize -= 0.5f;
        }

        ctx->DrawTextInRect(text, Rect2Dd(boxX, textTop, box, textHeight));
        ctx->PopState();
    }

// =============================================================================
// HIT TESTING & EVENTS
// =============================================================================

    bool UltraCanvasCircleDiagram::HitTest(const Point2Dd& localPos, size_t& nodeIndex,
                                            size_t& satelliteIndex) const {
        nodeIndex = SIZE_MAX;
        satelliteIndex = SIZE_MAX;
        if (nodes.empty()) return false;

        for (size_t i = 0; i < nodes.size(); ++i) {
            Point2Dd nc = NodeCenter(i);
            double dx = localPos.x - nc.x, dy = localPos.y - nc.y;
            if (dx * dx + dy * dy <= static_cast<double>(cachedNodeRadius) * cachedNodeRadius) {
                nodeIndex = i;
                return true;
            }

            for (size_t s = 0; s < nodes[i].satellites.size(); ++s) {
                Point2Dd sc = SatelliteCenter(i, s);
                double sdx = localPos.x - sc.x, sdy = localPos.y - sc.y;
                if (sdx * sdx + sdy * sdy <=
                    static_cast<double>(cachedSatelliteRadius) * cachedSatelliteRadius) {
                    nodeIndex = i;
                    satelliteIndex = s;
                    return true;
                }
            }
        }
        return false;
    }

    bool UltraCanvasCircleDiagram::HandleChartMouseMove(const Point2Di& mousePos) {
        if (nodes.empty()) return false;

        Point2Dd local(static_cast<double>(mousePos.x), static_cast<double>(mousePos.y));
        size_t ni = SIZE_MAX, si = SIZE_MAX;
        bool hit = HitTest(local, ni, si);

        if (ni != hoveredNode || si != hoveredSatellite) {
            hoveredNode = ni;
            hoveredSatellite = si;
            if (hit) {
                if (si != SIZE_MAX) {
                    if (onSatelliteHover) onSatelliteHover(ni, si);
                } else if (onNodeHover) {
                    onNodeHover(ni);
                }
            }
            RequestRedraw();
        }

        if (hit) {
            if (enableTooltips) {
                std::string text = (si != SIZE_MAX) ? BuildSatelliteTooltip(ni, si)
                                                    : BuildNodeTooltip(ni);
                if (!text.empty()) {
                    auto windowMousePos = MapFromLocal(mousePos, nullptr);
                    UltraCanvasTooltipManager::UpdateAndShowTooltip(
                        this->window, text, windowMousePos);
                    isTooltipActive = true;
                }
            }
            return true;
        }

        if (isTooltipActive) HideTooltip();
        return false;
    }

    bool UltraCanvasCircleDiagram::HandleClick(const UCEvent& event) {
        Point2Dd local(static_cast<double>(event.pointer.x),
                       static_cast<double>(event.pointer.y));
        size_t ni = SIZE_MAX, si = SIZE_MAX;
        if (!HitTest(local, ni, si)) return false;

        if (si != SIZE_MAX) {
            if (onSatelliteClick) onSatelliteClick(ni, si);
        } else if (onNodeClick) {
            onNodeClick(ni);
        }
        return true;
    }

    bool UltraCanvasCircleDiagram::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseDown:
                if (event.button == UCMouseButton::Left && HandleClick(event)) {
                    return true;
                }
                break;
            case UCEventType::MouseLeave:
                if (hoveredNode != SIZE_MAX) {
                    hoveredNode = SIZE_MAX;
                    hoveredSatellite = SIZE_MAX;
                    RequestRedraw();
                }
                HideTooltip();
                return true;
            default:
                break;
        }
        return UltraCanvasChartElementBase::OnEvent(event);
    }

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

    std::shared_ptr<UltraCanvasCircleDiagram> CreateCircleDiagram(
        const std::string& id, int x, int y, int w, int h) {
        return std::make_shared<UltraCanvasCircleDiagram>(id, x, y, w, h);
    }

    std::shared_ptr<UltraCanvasCircleDiagram> CreateCircleDiagramFromGroups(
        const std::string& id, int x, int y, int w, int h,
        const std::vector<std::pair<std::string, std::vector<std::string>>>& groupedData) {

        auto diagram = std::make_shared<UltraCanvasCircleDiagram>(id, x, y, w, h);
        for (const auto& group : groupedData) {
            size_t nodeIndex = diagram->AddNode(group.first);
            for (const auto& satelliteText : group.second) {
                diagram->AddSatellite(nodeIndex, satelliteText);
            }
        }
        return diagram;
    }

} // namespace UltraCanvas
