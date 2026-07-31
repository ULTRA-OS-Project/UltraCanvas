// Plugins/Diagrams/UltraCanvasTimelineDiagram.cpp
// Narrative timeline infographic with nine design presets
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasTimelineDiagram.h"
#include "UltraCanvasTooltipManager.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace UltraCanvas {

// =============================================================================
// LOCAL HELPERS
// =============================================================================

    static Color MixColors(const Color& a, const Color& b, float t) {
        auto mix = [t](int ca, int cb) {
            return static_cast<uint8_t>(std::clamp(
                    static_cast<int>(ca + (cb - ca) * t + 0.5f), 0, 255));
        };
        return Color(mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), 255);
    }

    static Color DarkenColor(const Color& c, float t) {
        return MixColors(c, Color(0, 0, 0, 255), t);
    }

    static Color LightenColor(const Color& c, float t) {
        return MixColors(c, Color(255, 255, 255, 255), t);
    }

    static Color WithAlpha(const Color& c, int alpha) {
        return Color(c.r, c.g, c.b, static_cast<uint8_t>(std::clamp(alpha, 0, 255)));
    }

    // Readable text color on a filled shape
    static Color ContrastText(const Color& background) {
        double luminance = (0.299 * background.r + 0.587 * background.g + 0.114 * background.b) / 255.0;
        return luminance > 0.62 ? Color(38, 42, 51, 255) : Color(255, 255, 255, 255);
    }

    static Rect2Dd UnionRect(const Rect2Dd& a, const Rect2Dd& b) {
        if (a.width <= 0 || a.height <= 0) return b;
        if (b.width <= 0 || b.height <= 0) return a;
        double x0 = std::min(a.x, b.x);
        double y0 = std::min(a.y, b.y);
        double x1 = std::max(a.x + a.width, b.x + b.width);
        double y1 = std::max(a.y + a.height, b.y + b.height);
        return Rect2Dd(x0, y0, x1 - x0, y1 - y0);
    }

    // Polyline with rounded corners, used by the serpentine ribbon
    static void BuildRoundedPolyline(IRenderContext* ctx, const std::vector<Point2Dd>& pts,
                                     double radius) {
        if (pts.size() < 2) return;
        ctx->ClearPath();
        ctx->MoveTo(pts[0].x, pts[0].y);
        for (size_t i = 1; i + 1 < pts.size(); ++i) {
            ctx->ArcTo(pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, radius);
        }
        ctx->LineTo(pts.back().x, pts.back().y);
    }

// =============================================================================
// CONSTRUCTION & DEFAULTS
// =============================================================================

    UltraCanvasTimelineDiagram::UltraCanvasTimelineDiagram(const std::string& id, int x, int y, int w, int h)
            : UltraCanvasChartElementBase(id, x, y, w, h) {
        enableSelection = true;
        enableTooltips = true;
        showGrid = false;
        showAxes = false;
        backgroundColor = Color(252, 252, 253, 255);
        style = TimelineDiagramStyles::CreateForDesign(design);
    }

// =============================================================================
// DESIGN, STYLE & THEME
// =============================================================================

    void UltraCanvasTimelineDiagram::SetDesign(TimelineDesign d) {
        if (design == d) return;
        design = d;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasTimelineDiagram::ApplyDesign(TimelineDesign d) {
        design = d;
        style = TimelineDiagramStyles::CreateForDesign(d);
        if (darkTheme) {
            // Re-apply the dark palette over the fresh preset
            darkTheme = false;
            SetDarkTheme(true);
        }
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasTimelineDiagram::SetStyle(const TimelineDiagramStyle& s) {
        style = s;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasTimelineDiagram::StyleChanged() {
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasTimelineDiagram::SetDarkTheme(bool dark) {
        if (darkTheme == dark) return;
        darkTheme = dark;
        if (dark) {
            backgroundColor = Color(30, 33, 40, 255);
            style.textColor = Color(235, 238, 243, 255);
            style.mutedTextColor = Color(155, 162, 176, 255);
            style.cardColor = Color(44, 48, 57, 255);
            style.cardBorderColor = Color(66, 71, 82, 255);
            style.spineColor = Color(70, 76, 88, 255);
            style.connectorColor = Color(88, 95, 108, 255);
        } else {
            TimelineDiagramStyle preset = TimelineDiagramStyles::CreateForDesign(design);
            backgroundColor = Color(252, 252, 253, 255);
            style.textColor = preset.textColor;
            style.mutedTextColor = preset.mutedTextColor;
            style.cardColor = preset.cardColor;
            style.cardBorderColor = preset.cardBorderColor;
            style.spineColor = preset.spineColor;
            style.connectorColor = preset.connectorColor;
        }
        RequestRedraw();
    }

// =============================================================================
// PALETTE & COLOR
// =============================================================================

    void UltraCanvasTimelineDiagram::SetPalette(TimelinePalette p) {
        palette = p;
        RequestRedraw();
    }

    void UltraCanvasTimelineDiagram::SetCustomPalette(const std::vector<Color>& colors) {
        customPalette = colors;
        palette = TimelinePalette::Custom;
        RequestRedraw();
    }

    const std::vector<Color>& UltraCanvasTimelineDiagram::GetPaletteColors() const {
        static std::vector<Color> cached;
        static TimelinePalette cachedKind = TimelinePalette::Custom;
        if (palette == TimelinePalette::Custom && !customPalette.empty()) {
            return customPalette;
        }
        if (cached.empty() || cachedKind != palette) {
            cached = TimelineDiagramStyles::GetPaletteColors(palette);
            cachedKind = palette;
        }
        return cached;
    }

    void UltraCanvasTimelineDiagram::SetColorMode(TimelineColorMode mode) {
        if (colorMode == mode) return;
        colorMode = mode;
        InvalidateLayout();
        RequestRedraw();
    }

    Color UltraCanvasTimelineDiagram::ItemColor(size_t index) const {
        if (index < items.size() && items[index].accentColor.a > 0) {
            return items[index].accentColor;
        }
        const std::vector<Color>& colors = GetPaletteColors();
        if (colors.empty()) return Color(52, 152, 219, 255);
        if (colorMode == TimelineColorMode::Single) return colors[0];
        return colors[index % colors.size()];
    }

    Color UltraCanvasTimelineDiagram::CardFillColor() const { return style.cardColor; }
    Color UltraCanvasTimelineDiagram::TextColor() const { return style.textColor; }
    Color UltraCanvasTimelineDiagram::MutedTextColor() const { return style.mutedTextColor; }

    bool UltraCanvasTimelineDiagram::IsPending(size_t index) const {
        return currentIndex >= 0 && static_cast<int>(index) > currentIndex;
    }

// =============================================================================
// LAYOUT OPTIONS
// =============================================================================

    void UltraCanvasTimelineDiagram::SetSidePolicy(TimelineSidePolicy policy) {
        if (sidePolicy == policy) return;
        sidePolicy = policy;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasTimelineDiagram::SetPlacement(TimelinePlacement p) {
        if (placement == p) return;
        placement = p;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasTimelineDiagram::SetReverseOrder(bool reverse) {
        if (reverseOrder == reverse) return;
        reverseOrder = reverse;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasTimelineDiagram::SetCurrentIndex(int index) {
        if (currentIndex == index) return;
        currentIndex = index;
        RequestRedraw();
    }

    void UltraCanvasTimelineDiagram::SetSubtitle(const std::string& text) {
        subtitle = text;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasTimelineDiagram::SetScaleLabels(const std::vector<std::string>& labels) {
        scaleLabels = labels;
        InvalidateLayout();
        RequestRedraw();
    }

// =============================================================================
// DATA MANAGEMENT
// =============================================================================

    void UltraCanvasTimelineDiagram::AddItem(const TimelineItem& item) {
        items.push_back(item);
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasTimelineDiagram::AddItem(const std::string& caption, const std::string& title,
                                             const std::string& body) {
        items.emplace_back(caption, title, body);
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasTimelineDiagram::InsertItem(size_t index, const TimelineItem& item) {
        index = std::min(index, items.size());
        items.insert(items.begin() + index, item);
        hoveredItem = TimelineItemRef();
        selectedItem = TimelineItemRef();
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasTimelineDiagram::SetItems(const std::vector<TimelineItem>& list) {
        items = list;
        hoveredItem = TimelineItemRef();
        selectedItem = TimelineItemRef();
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasTimelineDiagram::RemoveItem(size_t index) {
        if (index >= items.size()) return;
        items.erase(items.begin() + index);

        // Keep hover/selection consistent with the shifted indices
        auto fix = [&](TimelineItemRef& ref) {
            if (ref.index == static_cast<int>(index)) ref = TimelineItemRef();
            else if (ref.index > static_cast<int>(index)) ref.index--;
        };
        fix(selectedItem);
        fix(hoveredItem);

        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasTimelineDiagram::MoveItem(size_t from, size_t to) {
        if (from >= items.size() || to >= items.size() || from == to) return;
        TimelineItem moved = items[from];
        items.erase(items.begin() + from);
        items.insert(items.begin() + to, moved);
        hoveredItem = TimelineItemRef();
        selectedItem = TimelineItemRef();
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasTimelineDiagram::ClearItems() {
        items.clear();
        hoveredItem = TimelineItemRef();
        selectedItem = TimelineItemRef();
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasTimelineDiagram::ItemsChanged() {
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasTimelineDiagram::LoadSampleData() {
        SetItems(TimelineDiagramSamples::CompanyHistory());
    }

// =============================================================================
// SELECTION & GEOMETRY QUERIES
// =============================================================================

    void UltraCanvasTimelineDiagram::SetSelectedIndex(int index) {
        TimelineItemRef next;
        if (index >= 0 && index < static_cast<int>(items.size())) next.index = index;
        if (next == selectedItem) return;
        selectedItem = next;
        if (onSelectionChange) onSelectionChange();
        RequestRedraw();
    }

    void UltraCanvasTimelineDiagram::ClearSelection() {
        if (!selectedItem.IsValid()) return;
        selectedItem = TimelineItemRef();
        RequestRedraw();
    }

    bool UltraCanvasTimelineDiagram::GetItemNodeCenter(size_t index, Point2Dd& outCenter) const {
        if (!layout.valid || index >= layout.itemLayouts.size()) return false;
        outCenter = layout.itemLayouts[index].nodeCenter;
        return true;
    }

    bool UltraCanvasTimelineDiagram::GetItemContentRect(size_t index, Rect2Dd& outRect) const {
        if (!layout.valid || index >= layout.itemLayouts.size()) return false;
        outRect = layout.itemLayouts[index].contentRect;
        return true;
    }

// =============================================================================
// LAYOUT - SHARED
// =============================================================================

    double UltraCanvasTimelineDiagram::TitleBandHeight() const {
        double h = 0.0;
        if (!chartTitle.empty()) h += 32.0;
        if (!subtitle.empty()) h += 20.0;
        if (h > 0.0) h += 8.0;
        return h;
    }

    int UltraCanvasTimelineDiagram::SideForItem(size_t index) const {
        switch (sidePolicy) {
            case TimelineSidePolicy::AllAbove: return -1;
            case TimelineSidePolicy::AllBelow: return 1;
            case TimelineSidePolicy::PerItem:
                if (index < items.size() && items[index].side != 0) {
                    return items[index].side < 0 ? -1 : 1;
                }
                return (index % 2 == 0) ? -1 : 1;
            case TimelineSidePolicy::Alternate:
            default:
                return (index % 2 == 0) ? -1 : 1;
        }
    }

    std::vector<double> UltraCanvasTimelineDiagram::ItemPositions() const {
        const size_t n = items.size();
        std::vector<double> t(n, 0.0);
        if (n == 0) return t;
        if (n == 1) { t[0] = 0.5; return t; }

        bool proportional = (placement == TimelinePlacement::Proportional);
        if (proportional) {
            for (const auto& item : items) {
                if (!item.hasDate) { proportional = false; break; }
            }
        }

        if (proportional) {
            long lo = items[0].dateSerial, hi = items[0].dateSerial;
            for (const auto& item : items) {
                lo = std::min(lo, item.dateSerial);
                hi = std::max(hi, item.dateSerial);
            }
            const double span = static_cast<double>(hi - lo);
            if (span <= 0.0) {
                proportional = false;
            } else {
                for (size_t i = 0; i < n; ++i) {
                    t[i] = static_cast<double>(items[i].dateSerial - lo) / span;
                }
                // Enforce a minimum gap so near-simultaneous items stay legible,
                // then renormalize into [0, 1].
                const double minGap = 0.6 / static_cast<double>(n);
                for (size_t i = 1; i < n; ++i) {
                    t[i] = std::max(t[i], t[i - 1] + minGap);
                }
                const double last = t[n - 1];
                if (last > 1.0) {
                    for (double& v : t) v /= last;
                }
                // Keep the extremes off the edge so end labels stay inside
                for (double& v : t) v = 0.06 + v * 0.88;
            }
        }

        if (!proportional) {
            for (size_t i = 0; i < n; ++i) {
                t[i] = (static_cast<double>(i) + 0.5) / static_cast<double>(n);
            }
        }

        if (reverseOrder) {
            for (double& v : t) v = 1.0 - v;
        }
        return t;
    }

    float UltraCanvasTimelineDiagram::ScaledFont(float size) const {
        return std::max(style.minFontSize, static_cast<float>(size * layout.contentScale));
    }

    void UltraCanvasTimelineDiagram::UpdateLayout(IRenderContext* ctx) {
        layout.itemLayouts.assign(items.size(), ItemLayout());
        layout.spinePoints.clear();
        layout.spineRect = Rect2Dd(0, 0, 0, 0);
        layout.contentScale = 1.0;

        const double top = TitleBandHeight() + 10.0;
        layout.diagramArea = Rect2Dd(
                16.0, top,
                std::max(60.0, static_cast<double>(GetWidth()) - 32.0),
                std::max(60.0, static_cast<double>(GetHeight()) - top - 16.0));

        for (size_t i = 0; i < items.size(); ++i) {
            layout.itemLayouts[i].color = ItemColor(i);
            layout.itemLayouts[i].side = SideForItem(i);
            layout.itemLayouts[i].nodeRadius =
                    items[i].nodeSize > 0.0 ? items[i].nodeSize : style.nodeRadius;
        }

        if (!items.empty()) {
            switch (design) {
                case TimelineDesign::Bar:         LayoutBar(ctx); break;
                case TimelineDesign::Line:        LayoutLine(ctx); break;
                case TimelineDesign::Alternating: LayoutAlternating(ctx); break;
                case TimelineDesign::Cards:       LayoutCards(ctx); break;
                case TimelineDesign::Vertical:    LayoutVertical(ctx); break;
                case TimelineDesign::Serpentine:  LayoutSerpentine(ctx); break;
                case TimelineDesign::Hanging:     LayoutHanging(ctx); break;
                case TimelineDesign::Chevron:     LayoutChevron(ctx); break;
                case TimelineDesign::Steps:       LayoutSteps(ctx); break;
            }
        }

        // Every design contributes its own pieces; the hit rect is their union
        for (auto& il : layout.itemLayouts) {
            Rect2Dd node(il.nodeCenter.x - il.nodeRadius, il.nodeCenter.y - il.nodeRadius,
                         il.nodeRadius * 2.0, il.nodeRadius * 2.0);
            il.hitRect = UnionRect(UnionRect(il.contentRect, il.captionRect),
                                   UnionRect(node, il.blockRect));
        }

        layout.valid = true;
    }

// =============================================================================
// LAYOUT - HORIZONTAL SEGMENTED BAR (image: "Timeline Infographic")
// =============================================================================

    void UltraCanvasTimelineDiagram::LayoutBar(IRenderContext*) {
        const Rect2Dd& area = layout.diagramArea;
        const size_t n = items.size();
        const double segW = area.width / static_cast<double>(n);
        const double centerY = area.y + area.height / 2.0;
        const double half = style.spineThickness / 2.0;
        const double captionH = style.captionFontSize * 1.4;

        layout.contentScale = std::clamp(segW / 150.0, 0.62, 1.0);
        layout.spineRect = Rect2Dd(area.x, centerY - half, area.width, style.spineThickness);

        for (size_t i = 0; i < n; ++i) {
            ItemLayout& il = layout.itemLayouts[i];
            const size_t slot = reverseOrder ? (n - 1 - i) : i;
            const double x0 = area.x + static_cast<double>(slot) * segW;

            il.blockRect = Rect2Dd(x0 + style.segmentGap / 2.0, centerY - half,
                                   segW - style.segmentGap, style.spineThickness);
            il.nodeCenter.x = x0 + segW / 2.0;
            il.nodeCenter.y = (il.side < 0) ? (centerY - half) : (centerY + half);
            il.hasConnector = false;
            il.hasBlock = false;   // the segment is drawn as part of the spine

            const double contentGap = il.nodeRadius + 12.0;
            if (il.side < 0) {
                il.captionRect = Rect2Dd(x0, centerY + half + 8.0, segW, captionH);
                double bottom = il.nodeCenter.y - contentGap;
                il.contentRect = Rect2Dd(x0 + 6.0, area.y, segW - 12.0,
                                         std::max(10.0, bottom - area.y));
            } else {
                il.captionRect = Rect2Dd(x0, centerY - half - 8.0 - captionH, segW, captionH);
                double topY = il.nodeCenter.y + contentGap;
                il.contentRect = Rect2Dd(x0 + 6.0, topY, segW - 12.0,
                                         std::max(10.0, area.y + area.height - topY));
            }
        }
    }

// =============================================================================
// LAYOUT - THIN HORIZONTAL AXIS (Line / Alternating)
// =============================================================================

    void UltraCanvasTimelineDiagram::LayoutLine(IRenderContext* ctx) {
        const Rect2Dd& area = layout.diagramArea;
        const size_t n = items.size();
        const std::vector<double> pos = ItemPositions();
        const double segW = area.width / static_cast<double>(n);
        const double contentW = std::max(60.0, segW - style.itemGap);

        double axisY = area.y + area.height / 2.0;
        if (sidePolicy == TimelineSidePolicy::AllBelow) {
            axisY = area.y + std::max(style.nodeRadius + 10.0, area.height * 0.18);
        } else if (sidePolicy == TimelineSidePolicy::AllAbove) {
            axisY = area.y + area.height - std::max(style.nodeRadius + 10.0, area.height * 0.18);
        }
        layout.contentScale = std::clamp(contentW / 150.0, 0.62, 1.0);
        layout.spineRect = Rect2Dd(area.x, axisY - style.spineThickness / 2.0,
                                   area.width, style.spineThickness);

        const double stem = (style.connectorStyle == TimelineConnectorStyle::Hidden)
                            ? 6.0 : style.connectorLength;

        const bool boxed = (style.contentContainer == TimelineContentContainer::Card ||
                            style.contentContainer == TimelineContentContainer::OutlinedCard);
        const double pad = boxed ? style.contentPadding : 0.0;
        const double textW = std::max(30.0, contentW - pad * 2.0);

        std::vector<std::string> capLines, titleLines, bodyLines;
        for (size_t i = 0; i < n; ++i) {
            ItemLayout& il = layout.itemLayouts[i];
            il.nodeCenter = Point2Dd(area.x + pos[i] * area.width, axisY);
            il.hasConnector = (style.connectorStyle != TimelineConnectorStyle::Hidden);
            il.captionRect = Rect2Dd(0, 0, 0, 0);   // caption drawn inside the content block

            const double edge = (il.side < 0) ? (axisY - il.nodeRadius - stem)
                                              : (axisY + il.nodeRadius + stem);
            const double available = (il.side < 0) ? std::max(10.0, edge - area.y)
                                                   : std::max(10.0, area.y + area.height - edge);
            // Cards size themselves to their text instead of filling the half
            const double textH = std::min(available - pad * 2.0,
                                          BuildContentLines(ctx, i, textW, available - pad * 2.0,
                                                            true, capLines, titleLines, bodyLines));
            const double boxH = std::max(16.0, textH + pad * 2.0);

            // Clamped so the end items' text stays inside the frame
            const double x = std::clamp(il.nodeCenter.x - contentW / 2.0,
                                        area.x, area.x + area.width - contentW);
            const double boxY = (il.side < 0) ? (edge - boxH) : edge;
            il.blockRect = Rect2Dd(x, boxY, contentW, boxH);
            il.contentRect = Rect2Dd(x + pad, boxY + pad, textW, std::max(10.0, textH));
            il.connectorFrom = Point2Dd(il.nodeCenter.x,
                                        (il.side < 0) ? axisY - il.nodeRadius : axisY + il.nodeRadius);
            il.connectorTo = Point2Dd(il.nodeCenter.x, edge);
            il.hasBlock = boxed;
        }
    }

    void UltraCanvasTimelineDiagram::LayoutAlternating(IRenderContext* ctx) {
        // Same geometry as Line; the presets differ (cards + zigzag by default)
        LayoutLine(ctx);
    }

// =============================================================================
// LAYOUT - CARD ROW
// =============================================================================

    void UltraCanvasTimelineDiagram::LayoutCards(IRenderContext* ctx) {
        const Rect2Dd& area = layout.diagramArea;
        const size_t n = items.size();
        const double gap = style.itemGap;
        const double cardW = (area.width - gap * static_cast<double>(n - 1)) / static_cast<double>(n);
        const double nodeR = style.nodeRadius;
        const double captionH = style.captionFontSize * 1.4;

        layout.contentScale = std::clamp(cardW / 160.0, 0.62, 1.0);
        std::vector<std::string> capLines, titleLines, bodyLines;

        // First pass: how tall does the tallest card need to be? The row is
        // then centred vertically instead of hanging from the top edge.
        const double headerH = nodeR * 2.0 + 6.0 + captionH + 4.0;
        double tallest = 0.0;
        for (size_t i = 0; i < n; ++i) {
            const double room = area.height - headerH - style.contentPadding;
            tallest = std::max(tallest,
                               BuildContentLines(ctx, i, cardW - style.contentPadding * 2.0,
                                                 room, false, capLines, titleLines, bodyLines));
        }
        const double rowH = std::min(area.height,
                                     headerH + tallest + style.contentPadding - nodeR);
        const double rowTop = area.y + std::max(0.0, (area.height - rowH - nodeR) / 2.0);

        for (size_t i = 0; i < n; ++i) {
            ItemLayout& il = layout.itemLayouts[i];
            const size_t slot = reverseOrder ? (n - 1 - i) : i;
            const double x = area.x + static_cast<double>(slot) * (cardW + gap);

            il.nodeCenter = Point2Dd(x + cardW / 2.0, rowTop + nodeR);
            il.nodeRadius = nodeR;
            il.hasConnector = false;
            il.hasBlock = true;
            il.captionRect = Rect2Dd(x, rowTop + nodeR * 2.0 + 6.0, cardW, captionH);

            // The card sizes itself to its text rather than filling the frame
            const double contentTop = il.captionRect.y + il.captionRect.height + 4.0;
            const double textW = cardW - style.contentPadding * 2.0;
            const double roomBelow = rowTop + rowH + nodeR - contentTop - style.contentPadding;
            const double textH = std::min(roomBelow,
                                          BuildContentLines(ctx, i, textW, roomBelow, false,
                                                            capLines, titleLines, bodyLines));
            il.contentRect = Rect2Dd(x + style.contentPadding, contentTop, textW,
                                     std::max(10.0, textH));
            const double cardBottom = std::min(area.y + area.height,
                                               contentTop + textH + style.contentPadding);
            il.blockRect = Rect2Dd(x, rowTop + nodeR, cardW,
                                   std::max(60.0, cardBottom - rowTop - nodeR));
        }
    }

// =============================================================================
// LAYOUT - VERTICAL SPINE
// =============================================================================

    void UltraCanvasTimelineDiagram::LayoutVertical(IRenderContext* ctx) {
        const Rect2Dd& area = layout.diagramArea;
        const size_t n = items.size();
        const std::vector<double> pos = ItemPositions();
        const double rowH = area.height / static_cast<double>(n);
        const double stem = (style.connectorStyle == TimelineConnectorStyle::Hidden)
                            ? 6.0 : style.connectorLength;

        double spineX = area.x + area.width / 2.0;
        if (sidePolicy == TimelineSidePolicy::AllBelow) {
            spineX = area.x + style.nodeRadius + 12.0;
        } else if (sidePolicy == TimelineSidePolicy::AllAbove) {
            spineX = area.x + area.width - style.nodeRadius - 12.0;
        }
        layout.spineRect = Rect2Dd(spineX - style.spineThickness / 2.0, area.y,
                                   style.spineThickness, area.height);

        const double leftW = std::max(40.0, spineX - area.x - style.nodeRadius - stem - 6.0);
        const double rightW = std::max(40.0, area.x + area.width - spineX - style.nodeRadius - stem - 6.0);
        layout.contentScale = std::clamp(std::max(leftW, rightW) / 220.0, 0.68, 1.0);

        const bool boxed = (style.contentContainer == TimelineContentContainer::Card ||
                            style.contentContainer == TimelineContentContainer::OutlinedCard);
        const double pad = boxed ? style.contentPadding : 0.0;
        std::vector<std::string> capLines, titleLines, bodyLines;

        for (size_t i = 0; i < n; ++i) {
            ItemLayout& il = layout.itemLayouts[i];
            const double y = area.y + pos[i] * area.height;
            il.nodeCenter = Point2Dd(spineX, y);
            il.hasConnector = (style.connectorStyle != TimelineConnectorStyle::Hidden);

            const double maxH = std::max(24.0, rowH - style.itemGap);
            const double boxW = (il.side < 0) ? leftW : rightW;
            const double textW = std::max(30.0, boxW - pad * 2.0);
            const double textH = std::min(maxH - pad * 2.0,
                                          BuildContentLines(ctx, i, textW, maxH - pad * 2.0,
                                                            true, capLines, titleLines, bodyLines));
            const double boxH = std::max(16.0, textH + pad * 2.0);
            const double boxY = y - boxH / 2.0;

            if (il.side < 0) {   // left
                double right = spineX - il.nodeRadius - stem;
                il.blockRect = Rect2Dd(right - boxW, boxY, boxW, boxH);
                il.connectorFrom = Point2Dd(spineX - il.nodeRadius, y);
                il.connectorTo = Point2Dd(right, y);
            } else {             // right
                double left = spineX + il.nodeRadius + stem;
                il.blockRect = Rect2Dd(left, boxY, boxW, boxH);
                il.connectorFrom = Point2Dd(spineX + il.nodeRadius, y);
                il.connectorTo = Point2Dd(left, y);
            }
            il.contentRect = Rect2Dd(il.blockRect.x + pad, boxY + pad, textW, std::max(10.0, textH));
            il.captionRect = Rect2Dd(0, 0, 0, 0);
            il.hasBlock = boxed;
        }
    }

// =============================================================================
// LAYOUT - SERPENTINE RIBBON
// =============================================================================

    void UltraCanvasTimelineDiagram::LayoutSerpentine(IRenderContext* ctx) {
        const Rect2Dd& area = layout.diagramArea;
        const size_t n = items.size();
        const int perRun = std::max(1, style.serpentineItemsPerRun);
        const int rows = static_cast<int>((n + perRun - 1) / perRun);
        const double runH = area.height / static_cast<double>(rows);
        const double turnR = std::max(8.0, std::min(style.serpentineTurnRadius, runH / 2.0 - 6.0));
        const double inset = turnR + 8.0;
        const double leftX = area.x + inset;
        const double rightX = area.x + area.width - inset;

        layout.contentScale = std::clamp((rightX - leftX) / (perRun * 190.0), 0.6, 1.0);

        const bool boxed = (style.contentContainer == TimelineContentContainer::Card ||
                            style.contentContainer == TimelineContentContainer::OutlinedCard);
        const double pad = boxed ? style.contentPadding : 0.0;
        std::vector<std::string> capLines, titleLines, bodyLines;

        // Ribbon polyline: alternating runs joined by vertical drops
        for (int r = 0; r < rows; ++r) {
            const double y = area.y + runH * (static_cast<double>(r) + 0.5);
            const bool leftToRight = (r % 2 == 0);
            const double startX = leftToRight ? leftX : rightX;
            const double endX = leftToRight ? rightX : leftX;
            layout.spinePoints.push_back(Point2Dd(startX, y));
            layout.spinePoints.push_back(Point2Dd(endX, y));
            if (r + 1 < rows) {
                const double nextY = area.y + runH * (static_cast<double>(r) + 1.5);
                layout.spinePoints.push_back(Point2Dd(endX, nextY));
            }
        }

        for (size_t i = 0; i < n; ++i) {
            ItemLayout& il = layout.itemLayouts[i];
            const size_t slot = reverseOrder ? (n - 1 - i) : i;
            const int row = static_cast<int>(slot) / perRun;
            const int col = static_cast<int>(slot) % perRun;
            const int inThisRow = std::min(perRun, static_cast<int>(n) - row * perRun);
            const double y = area.y + runH * (static_cast<double>(row) + 0.5);
            const bool leftToRight = (row % 2 == 0);

            const double frac = (static_cast<double>(col) + 0.5) / static_cast<double>(inThisRow);
            const double x = leftToRight ? (leftX + frac * (rightX - leftX))
                                         : (rightX - frac * (rightX - leftX));

            il.nodeCenter = Point2Dd(x, y);
            il.hasConnector = false;
            il.captionRect = Rect2Dd(0, 0, 0, 0);

            const double colW = std::max(60.0, (rightX - leftX) / static_cast<double>(inThisRow) - 8.0);
            const double maxH = std::max(18.0, runH / 2.0 - il.nodeRadius - 8.0);
            const double textW = std::max(30.0, colW - pad * 2.0);
            const double textH = std::min(maxH - pad * 2.0,
                                          BuildContentLines(ctx, i, textW, maxH - pad * 2.0,
                                                            true, capLines, titleLines, bodyLines));
            const double boxH = std::max(16.0, textH + pad * 2.0);
            const double boxY = (il.side < 0) ? (y - il.nodeRadius - 6.0 - boxH)
                                              : (y + il.nodeRadius + 6.0);
            const double boxX = std::clamp(x - colW / 2.0, area.x, area.x + area.width - colW);
            il.blockRect = Rect2Dd(boxX, boxY, colW, boxH);
            il.contentRect = Rect2Dd(boxX + pad, boxY + pad, textW, std::max(10.0, textH));
            il.hasBlock = boxed;
        }
    }

// =============================================================================
// LAYOUT - HANGING BUBBLES (image: month axis with dropped circles)
// =============================================================================

    void UltraCanvasTimelineDiagram::LayoutHanging(IRenderContext*) {
        const Rect2Dd& area = layout.diagramArea;
        const size_t n = items.size();
        const std::vector<double> pos = ItemPositions();
        const int tiers = std::max(1, style.hangingTiers);

        const double scaleH = (style.showScaleRow && !scaleLabels.empty())
                              ? style.scaleFontSize * 2.2 : 0.0;
        const double axisY = area.y + scaleH + style.captionFontSize * 1.6;
        layout.spineRect = Rect2Dd(area.x, axisY - style.spineThickness / 2.0,
                                   area.width, style.spineThickness);

        const double segW = area.width / static_cast<double>(n);
        const double available = area.y + area.height - axisY;
        layout.contentScale = std::clamp(segW / 130.0, 0.6, 1.0);

        // Radius from the amount of body text, clamped by the space available
        const double maxRadius = std::min(segW * 1.05, (available - 40.0) / 2.2);
        std::vector<double> radius(n, 0.0);
        for (size_t i = 0; i < n; ++i) {
            if (items[i].nodeSize > 0.0) {
                radius[i] = items[i].nodeSize;
            } else {
                const double chars = static_cast<double>(items[i].body.size() + items[i].title.size());
                radius[i] = 30.0 + std::sqrt(std::max(0.0, chars)) * 3.6;
            }
            radius[i] = std::clamp(radius[i], 22.0, std::max(24.0, maxRadius));
        }

        // Greedy tier assignment: the first tier where this bubble clears the
        // previous bubble already placed there
        std::vector<double> lastX(tiers, -1e9);
        std::vector<double> lastR(tiers, 0.0);
        std::vector<int> tierOf(n, 0);
        for (size_t i = 0; i < n; ++i) {
            const double x = area.x + pos[i] * area.width;
            int chosen = -1;
            for (int t = 0; t < tiers; ++t) {
                if (x - lastX[t] >= radius[i] + lastR[t] + 8.0) { chosen = t; break; }
            }
            if (chosen < 0) chosen = static_cast<int>(i) % tiers;
            tierOf[i] = chosen;
            lastX[chosen] = x;
            lastR[chosen] = radius[i];
        }

        const double tierStep = (tiers > 1)
                ? std::max(20.0, (available - 2.0 * maxRadius - 30.0) / static_cast<double>(tiers - 1))
                : 0.0;

        for (size_t i = 0; i < n; ++i) {
            ItemLayout& il = layout.itemLayouts[i];
            const double x = area.x + pos[i] * area.width;
            double stem = items[i].stemLength > 0.0
                          ? items[i].stemLength
                          : 26.0 + tierStep * static_cast<double>(tierOf[i]);

            double r = radius[i];
            // Keep the bubble inside the diagram area
            const double bottomLimit = area.y + area.height - 4.0;
            if (axisY + stem + 2.0 * r > bottomLimit) {
                stem = std::max(14.0, bottomLimit - axisY - 2.0 * r);
                if (axisY + stem + 2.0 * r > bottomLimit) {
                    r = std::max(18.0, (bottomLimit - axisY - stem) / 2.0);
                }
            }

            // Keep the bubble inside the frame; the stem then leans slightly
            const double bubbleX = std::clamp(x, area.x + r, area.x + area.width - r);
            il.nodeCenter = Point2Dd(bubbleX, axisY + stem + r);
            il.nodeRadius = r;
            il.hasConnector = true;
            il.connectorFrom = Point2Dd(x, axisY);
            il.connectorTo = Point2Dd(bubbleX, axisY + stem);
            il.blockRect = Rect2Dd(x - 11.0, axisY - 6.0, 22.0, 12.0);   // tick on the axis
            il.hasBlock = false;
            il.contentRect = Rect2Dd(bubbleX - r, il.nodeCenter.y - r, r * 2.0, r * 2.0);
            il.captionRect = Rect2Dd(x - segW / 2.0, area.y + scaleH, segW,
                                     style.captionFontSize * 1.4);
        }
    }

// =============================================================================
// LAYOUT - CHEVRON BLOCKS
// =============================================================================

    void UltraCanvasTimelineDiagram::LayoutChevron(IRenderContext* ctx) {
        const Rect2Dd& area = layout.diagramArea;
        const size_t n = items.size();
        const double gap = std::max(2.0, style.itemGap * 0.4);
        const double blockW = (area.width - gap * static_cast<double>(n - 1)) / static_cast<double>(n);
        const double blockH = std::min(area.height, std::max(70.0, area.height * 0.6));
        const double y = area.y + (area.height - blockH) / 2.0;
        const double notch = std::min(blockW * 0.16, blockH * 0.34);

        layout.contentScale = std::clamp((blockW - notch * 2.0) / 150.0, 0.6, 1.0);

        // Text must clear the notch on the left and the arrow point on the right
        const double textW = std::max(30.0, blockW - notch * 2.0 - 14.0);
        const double captionH = ScaledFont(style.captionFontSize) * 1.4;
        std::vector<std::string> capLines, titleLines, bodyLines;

        for (size_t i = 0; i < n; ++i) {
            ItemLayout& il = layout.itemLayouts[i];
            const size_t slot = reverseOrder ? (n - 1 - i) : i;
            const double x = area.x + static_cast<double>(slot) * (blockW + gap);

            il.blockRect = Rect2Dd(x, y, blockW, blockH);
            il.hasBlock = true;
            il.hasConnector = false;
            il.nodeCenter = Point2Dd(x + notch + style.nodeRadius + 6.0, y + blockH / 2.0);

            // Caption + text are centred vertically inside the arrow
            const double room = blockH - captionH - 16.0;
            const double textH = std::min(room,
                                          BuildContentLines(ctx, i, textW, room, false,
                                                            capLines, titleLines, bodyLines));
            const double groupTop = y + std::max(8.0, (blockH - captionH - textH) / 2.0);
            il.captionRect = Rect2Dd(x + notch + 8.0, groupTop, textW, captionH);
            il.contentRect = Rect2Dd(x + notch + 8.0, groupTop + captionH, textW,
                                     std::max(10.0, textH));
        }
    }

// =============================================================================
// LAYOUT - ASCENDING STEPS
// =============================================================================

    void UltraCanvasTimelineDiagram::LayoutSteps(IRenderContext* ctx) {
        const Rect2Dd& area = layout.diagramArea;
        const size_t n = items.size();
        const double gap = style.itemGap;
        const double blockW = (area.width - gap * static_cast<double>(n - 1)) / static_cast<double>(n);
        const double baseY = area.y + area.height;
        const double minH = std::max(48.0, area.height * 0.28);
        const double rise = style.stepRise > 0.0
                ? style.stepRise
                : std::max(10.0, (area.height - minH - 10.0) / static_cast<double>(std::max<size_t>(1, n)));

        layout.contentScale = std::clamp(blockW / 150.0, 0.6, 1.0);
        const double captionH = ScaledFont(style.captionFontSize) * 1.4;
        std::vector<std::string> capLines, titleLines, bodyLines;

        for (size_t i = 0; i < n; ++i) {
            ItemLayout& il = layout.itemLayouts[i];
            const size_t slot = reverseOrder ? (n - 1 - i) : i;
            const double x = area.x + static_cast<double>(slot) * (blockW + gap);
            const double h = std::min(area.height, minH + rise * static_cast<double>(slot));
            const double topY = baseY - h;

            il.blockRect = Rect2Dd(x, topY, blockW, h);
            il.hasBlock = true;
            il.hasConnector = false;
            il.nodeCenter = Point2Dd(x + blockW / 2.0, topY);
            const double textW = std::max(30.0, blockW - 20.0);
            const double room = h - captionH - 24.0;
            const double textH = std::min(room,
                                          BuildContentLines(ctx, i, textW, room, false,
                                                            capLines, titleLines, bodyLines));
            const double groupTop = topY + std::max(12.0, (h - captionH - textH) / 2.0);
            il.captionRect = Rect2Dd(x + 10.0, groupTop, textW, captionH);
            il.contentRect = Rect2Dd(x + 10.0, groupTop + captionH, textW,
                                     std::max(10.0, textH));
        }
    }

// =============================================================================
// TEXT HELPERS
// =============================================================================

    double UltraCanvasTimelineDiagram::BuildContentLines(
            IRenderContext* ctx, size_t index, double width, double maxHeight,
            bool captionInside,
            std::vector<std::string>& captionLines,
            std::vector<std::string>& titleLines,
            std::vector<std::string>& bodyLines) const {
        captionLines.clear();
        titleLines.clear();
        bodyLines.clear();
        if (index >= items.size() || width < 20.0) return 0.0;

        const TimelineItem& item = items[index];
        const float captionSize = ScaledFont(style.captionFontSize);
        const float titleSize = ScaledFont(style.itemTitleFontSize);
        const float bodySize = ScaledFont(style.bodyFontSize);
        const double captionLine = captionSize * 1.4;
        const double titleLine = titleSize * 1.3;
        const double bodyLine = bodySize * 1.35;

        if (style.showCaptions && captionInside && !item.caption.empty()) {
            ctx->SetFontSize(captionSize);
            ctx->SetFontWeight(FontWeight::Bold);
            captionLines = WrapText(ctx, item.caption, width, 1);
            ctx->SetFontWeight(FontWeight::Normal);
        }
        if (style.showTitles && !item.title.empty()) {
            ctx->SetFontSize(titleSize);
            ctx->SetFontWeight(FontWeight::Bold);
            titleLines = WrapText(ctx, item.title, width, 2);
            ctx->SetFontWeight(FontWeight::Normal);
        }

        double used = static_cast<double>(captionLines.size()) * captionLine +
                      static_cast<double>(titleLines.size()) * titleLine;

        if (style.showBodies && !item.body.empty()) {
            ctx->SetFontSize(bodySize);
            // + epsilon: without it a rect sized to exactly N lines measures as
            // N-1 and the last line is ellipsized away
            int maxLines = static_cast<int>((maxHeight - used) / bodyLine + 0.01);
            maxLines = std::clamp(maxLines, 0, style.maxBodyLines);
            if (maxLines > 0) bodyLines = WrapText(ctx, item.body, width, maxLines);
        }

        return used + static_cast<double>(bodyLines.size()) * bodyLine;
    }

    std::vector<std::string> UltraCanvasTimelineDiagram::WrapText(
            IRenderContext* ctx, const std::string& text, double maxWidth, int maxLines) const {
        std::vector<std::string> lines;
        if (text.empty() || maxWidth < 8.0) return lines;

        std::stringstream paragraphs(text);
        std::string paragraph;
        while (std::getline(paragraphs, paragraph, '\n')) {
            std::stringstream words(paragraph);
            std::string word, current;
            while (words >> word) {
                std::string candidate = current.empty() ? word : current + " " + word;
                if (!current.empty() && ctx->GetTextLineDimensions(candidate).width > maxWidth) {
                    lines.push_back(current);
                    current = word;
                } else {
                    current = candidate;
                }
            }
            lines.push_back(current);
        }

        if (maxLines > 0 && static_cast<int>(lines.size()) > maxLines) {
            lines.resize(maxLines);
            std::string& last = lines.back();
            while (!last.empty() &&
                   ctx->GetTextLineDimensions(last + "...").width > maxWidth) {
                last.pop_back();
            }
            last += "...";
        }
        return lines;
    }

    std::vector<std::string> UltraCanvasTimelineDiagram::WrapTextInCircle(
            IRenderContext* ctx, const std::string& text, double radius, double lineHeight) const {
        std::vector<std::string> lines;
        if (text.empty() || radius < 12.0) return lines;

        // The available width depends on how many lines there are, so wrap
        // repeatedly until the line count stops growing.
        int assumed = 1;
        for (int attempt = 0; attempt < 8; ++attempt) {
            lines.clear();
            std::stringstream words(text);
            std::string word, current;
            int lineIndex = 0;

            auto widthForLine = [&](int index, int total) {
                const double offset = (static_cast<double>(index) - (total - 1) / 2.0) * lineHeight;
                const double edge = std::abs(offset) + lineHeight / 2.0;
                if (edge >= radius) return 0.0;
                return 2.0 * std::sqrt(radius * radius - edge * edge) * 0.92;
            };

            while (words >> word) {
                double maxWidth = widthForLine(lineIndex, assumed);
                if (maxWidth < 10.0) maxWidth = radius;   // degenerate; keep going
                std::string candidate = current.empty() ? word : current + " " + word;
                if (!current.empty() && ctx->GetTextLineDimensions(candidate).width > maxWidth) {
                    lines.push_back(current);
                    current = word;
                    lineIndex++;
                } else {
                    current = candidate;
                }
            }
            if (!current.empty()) lines.push_back(current);

            if (static_cast<int>(lines.size()) == assumed) break;
            assumed = static_cast<int>(lines.size());
        }
        return lines;
    }

    void UltraCanvasTimelineDiagram::DrawCenteredLines(
            IRenderContext* ctx, const std::vector<std::string>& lines,
            const Point2Dd& center, double lineHeight) const {
        if (lines.empty()) return;
        double y = center.y - static_cast<double>(lines.size()) * lineHeight / 2.0;
        for (const auto& line : lines) {
            Size2Di s = ctx->GetTextLineDimensions(line);
            ctx->DrawText(line, Point2Dd(center.x - s.width / 2.0, y));
            y += lineHeight;
        }
    }

// =============================================================================
// DRAWING - TOP LEVEL
// =============================================================================

    void UltraCanvasTimelineDiagram::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        if (!ctx) return;

        UpdateLayout(ctx);

        if (showBackground) {
            ctx->DrawFilledRectangle(GetLocalBounds(), backgroundColor);
        }
        DrawTitles(ctx);
        RenderChart(ctx);
    }

    void UltraCanvasTimelineDiagram::RenderChart(IRenderContext* ctx) {
        if (!ctx) return;
        if (!layout.valid) UpdateLayout(ctx);
        if (items.empty()) return;

        DrawSpine(ctx);
        if (style.showScaleRow && !scaleLabels.empty()) DrawScaleRow(ctx);
        DrawItems(ctx);
    }

    void UltraCanvasTimelineDiagram::DrawTitles(IRenderContext* ctx) {
        if (chartTitle.empty() && subtitle.empty()) return;

        double y = 8.0;
        if (!chartTitle.empty()) {
            ctx->SetFontSize(20.0f);
            ctx->SetFontWeight(FontWeight::Bold);
            ctx->SetTextPaint(TextColor());
            Size2Di s = ctx->GetTextLineDimensions(chartTitle);
            ctx->DrawText(chartTitle, Point2Dd(GetWidth() / 2.0 - s.width / 2.0, y));
            ctx->SetFontWeight(FontWeight::Normal);
            y += 30.0;
        }
        if (!subtitle.empty()) {
            ctx->SetFontSize(12.0f);
            ctx->SetTextPaint(MutedTextColor());
            Size2Di s = ctx->GetTextLineDimensions(subtitle);
            ctx->DrawText(subtitle, Point2Dd(GetWidth() / 2.0 - s.width / 2.0, y));
        }
    }

// =============================================================================
// DRAWING - SPINE
// =============================================================================

    void UltraCanvasTimelineDiagram::DrawSpine(IRenderContext* ctx) {
        if (!style.showSpine) return;

        if (design == TimelineDesign::Serpentine) {
            DrawSerpentineRibbon(ctx);
            return;
        }
        if (design == TimelineDesign::Cards || design == TimelineDesign::Chevron ||
            design == TimelineDesign::Steps) {
            return;   // These designs have no spine
        }

        if (design == TimelineDesign::Bar) {
            // The spine is the item segments themselves; the outer ends are rounded
            const double radius = std::min(style.spineCornerRadius,
                                           style.spineThickness / 2.0);
            for (size_t i = 0; i < layout.itemLayouts.size(); ++i) {
                const ItemLayout& il = layout.itemLayouts[i];
                Color fill = IsPending(i) ? LightenColor(il.color, 0.62f) : il.color;
                ctx->SetFillPaint(fill);

                const bool isLeftEnd = std::abs(il.blockRect.x - layout.spineRect.x) < 1.5;
                const bool isRightEnd = std::abs((il.blockRect.x + il.blockRect.width) -
                                                 (layout.spineRect.x + layout.spineRect.width)) < 1.5;
                if ((isLeftEnd || isRightEnd) && radius > 1.0) {
                    ctx->FillRoundedRectangle(il.blockRect, radius);
                    // Square off the inner side again
                    if (isLeftEnd && !isRightEnd) {
                        ctx->FillRectangle(Rect2Dd(il.blockRect.x + il.blockRect.width / 2.0,
                                                   il.blockRect.y, il.blockRect.width / 2.0,
                                                   il.blockRect.height));
                    } else if (isRightEnd && !isLeftEnd) {
                        ctx->FillRectangle(Rect2Dd(il.blockRect.x, il.blockRect.y,
                                                   il.blockRect.width / 2.0, il.blockRect.height));
                    }
                } else {
                    ctx->FillRectangle(il.blockRect);
                }
            }
            return;
        }

        // Line / Alternating / Vertical / Hanging: a plain thin spine
        Rect2Dd spine = layout.spineRect;
        if (spine.width <= 0.0 || spine.height <= 0.0) return;

        if (colorMode == TimelineColorMode::GradientAlongPath && layout.itemLayouts.size() > 1) {
            std::vector<GradientStop> stops;
            const size_t n = layout.itemLayouts.size();
            for (size_t i = 0; i < n; ++i) {
                stops.emplace_back(static_cast<double>(i) / static_cast<double>(n - 1),
                                   layout.itemLayouts[i].color);
            }
            auto pattern = ctx->CreateLinearGradientPattern(
                    spine.x, spine.y, spine.x + spine.width, spine.y + spine.height, stops);
            ctx->SetFillPaint(pattern);
        } else {
            ctx->SetFillPaint(style.spineColor);
        }
        const double radius = std::min(style.spineCornerRadius,
                                       std::min(spine.width, spine.height) / 2.0);
        if (radius > 1.0) ctx->FillRoundedRectangle(spine, radius);
        else ctx->FillRectangle(spine);

        // Ticks on the axis for the hanging design
        if (design == TimelineDesign::Hanging) {
            for (size_t i = 0; i < layout.itemLayouts.size(); ++i) {
                const ItemLayout& il = layout.itemLayouts[i];
                if (il.blockRect.width <= 0.0) continue;
                ctx->SetFillPaint(IsPending(i) ? LightenColor(il.color, 0.6f) : il.color);
                ctx->FillRoundedRectangle(il.blockRect, 3.0);
            }
        }

        if (style.showSpineArrow && design != TimelineDesign::Vertical) {
            const double tipX = spine.x + spine.width + 10.0;
            const double cy = spine.y + spine.height / 2.0;
            const double h = std::max(7.0, style.spineThickness * 1.6);
            ctx->SetFillPaint(style.spineColor);
            ctx->FillLinePath({Point2Dd(spine.x + spine.width, cy - h),
                               Point2Dd(tipX, cy),
                               Point2Dd(spine.x + spine.width, cy + h)});
        }
    }

    void UltraCanvasTimelineDiagram::DrawSerpentineRibbon(IRenderContext* ctx) {
        if (layout.spinePoints.size() < 2) return;

        if (colorMode == TimelineColorMode::GradientAlongPath && layout.itemLayouts.size() > 1) {
            std::vector<GradientStop> stops;
            const size_t n = layout.itemLayouts.size();
            for (size_t i = 0; i < n; ++i) {
                stops.emplace_back(static_cast<double>(i) / static_cast<double>(n - 1),
                                   layout.itemLayouts[i].color);
            }
            const Rect2Dd& area = layout.diagramArea;
            auto pattern = ctx->CreateLinearGradientPattern(
                    area.x, area.y, area.x + area.width, area.y + area.height, stops);
            ctx->SetStrokePaint(pattern);
        } else {
            ctx->SetStrokePaint(style.spineColor);
        }

        ctx->SetLineCap(LineCap::Round);
        ctx->SetLineJoin(LineJoin::Round);
        ctx->SetStrokeWidth(style.spineThickness);
        BuildRoundedPolyline(ctx, layout.spinePoints, style.serpentineTurnRadius);
        ctx->Stroke();
        ctx->SetLineCap(LineCap::Butt);
        ctx->SetLineJoin(LineJoin::Miter);
    }

    void UltraCanvasTimelineDiagram::DrawScaleRow(IRenderContext* ctx) {
        const Rect2Dd& area = layout.diagramArea;
        const size_t n = scaleLabels.size();
        if (n == 0) return;

        ctx->SetFontSize(ScaledFont(style.scaleFontSize));
        ctx->SetFontWeight(FontWeight::Bold);

        const double y = area.y;
        for (size_t i = 0; i < n; ++i) {
            const double x = area.x + (static_cast<double>(i) + 0.5) *
                                      (area.width / static_cast<double>(n));
            const std::vector<Color>& colors = GetPaletteColors();
            Color c = colors.empty() ? style.mutedTextColor : colors[i % colors.size()];
            ctx->SetTextPaint(c);
            Size2Di s = ctx->GetTextLineDimensions(scaleLabels[i]);
            ctx->DrawText(scaleLabels[i], Point2Dd(x - s.width / 2.0, y));
            ctx->SetFillPaint(c);
            ctx->FillCircle(Point2Dd(x, y - 5.0), 2.5);
        }
        ctx->SetFontWeight(FontWeight::Normal);
    }

// =============================================================================
// DRAWING - ITEMS
// =============================================================================

    void UltraCanvasTimelineDiagram::DrawItems(IRenderContext* ctx) {
        for (size_t i = 0; i < layout.itemLayouts.size(); ++i) {
            const ItemLayout& il = layout.itemLayouts[i];
            DrawHighlight(ctx, i, il);
            if (il.hasBlock) DrawBlock(ctx, i, il);
            if (il.hasConnector) DrawConnector(ctx, il);
            if (style.showNodes) DrawNode(ctx, i, il);
            if (style.showCaptions && il.captionRect.width > 0.0) DrawCaption(ctx, i, il);
            if (style.contentContainer == TimelineContentContainer::Bubble) {
                DrawBubbleContent(ctx, i, il);
            } else {
                DrawContent(ctx, i, il);
            }
        }
    }

    void UltraCanvasTimelineDiagram::DrawHighlight(IRenderContext* ctx, size_t index,
                                                   const ItemLayout& il) {
        const bool isSelected = (selectedItem.index == static_cast<int>(index));
        const bool isHovered = (hoveredItem.index == static_cast<int>(index));
        if (!isSelected && !isHovered && !items[index].highlighted) return;
        if (il.hitRect.width <= 0.0 || il.hitRect.height <= 0.0) return;

        Rect2Dd rect(il.hitRect.x - 4.0, il.hitRect.y - 4.0,
                     il.hitRect.width + 8.0, il.hitRect.height + 8.0);
        ctx->SetFillPaint(WithAlpha(il.color, isSelected ? 48 : (isHovered ? 26 : 18)));
        ctx->FillRoundedRectangle(rect, style.cornerRadius);
        if (isSelected) {
            ctx->SetStrokePaint(il.color);
            ctx->SetStrokeWidth(1.5);
            ctx->DrawRoundedRectangle(rect, style.cornerRadius);
        }
    }

    void UltraCanvasTimelineDiagram::DrawShadowRect(IRenderContext* ctx, const Rect2Dd& rect,
                                                    double radius) {
        if (!style.showShadows) return;
        ctx->SetFillPaint(Color(0, 0, 0, static_cast<uint8_t>(std::clamp(style.shadowAlpha, 0, 255))));
        Rect2Dd shadow(rect.x + style.shadowOffset, rect.y + style.shadowOffset,
                       rect.width, rect.height);
        if (radius > 1.0) ctx->FillRoundedRectangle(shadow, radius);
        else ctx->FillRectangle(shadow);
    }

    void UltraCanvasTimelineDiagram::DrawBlock(IRenderContext* ctx, size_t index,
                                               const ItemLayout& il) {
        const Rect2Dd& rect = il.blockRect;
        if (rect.width <= 0.0 || rect.height <= 0.0) return;
        const bool pending = IsPending(index);
        const Color accent = pending ? LightenColor(il.color, 0.55f) : il.color;

        if (design == TimelineDesign::Chevron) {
            const double notch = std::min(rect.width * 0.16, rect.height * 0.34);
            const double x0 = rect.x, x1 = rect.x + rect.width;
            const double y0 = rect.y, y1 = rect.y + rect.height, ym = rect.y + rect.height / 2.0;
            DrawShadowRect(ctx, rect, 0.0);
            ctx->SetFillPaint(accent);
            ctx->FillLinePath({Point2Dd(x0, y0), Point2Dd(x1 - notch, y0), Point2Dd(x1, ym),
                               Point2Dd(x1 - notch, y1), Point2Dd(x0, y1), Point2Dd(x0 + notch, ym)});
            return;
        }

        if (design == TimelineDesign::Steps) {
            DrawShadowRect(ctx, rect, style.cornerRadius);
            ctx->SetFillPaint(accent);
            ctx->FillRoundedRectangle(rect, style.cornerRadius);
            return;
        }

        if (design == TimelineDesign::Cards) {
            DrawShadowRect(ctx, rect, style.cornerRadius);
            ctx->SetFillPaint(CardFillColor());
            ctx->FillRoundedRectangle(rect, style.cornerRadius);
            // Colored header band behind the caption
            const double headerH = std::min(rect.height,
                                            style.nodeRadius + style.captionFontSize * 1.9);
            ctx->SetFillPaint(WithAlpha(accent, 42));
            ctx->FillRoundedRectangle(Rect2Dd(rect.x, rect.y, rect.width, headerH),
                                      style.cornerRadius);
            ctx->SetStrokePaint(style.cardBorderColor);
            ctx->SetStrokeWidth(1.0);
            ctx->DrawRoundedRectangle(rect, style.cornerRadius);
            return;
        }

        // Generic content card (Line / Alternating / Vertical / Serpentine)
        DrawShadowRect(ctx, rect, style.cornerRadius);
        if (style.contentContainer == TimelineContentContainer::Card) {
            ctx->SetFillPaint(CardFillColor());
            ctx->FillRoundedRectangle(rect, style.cornerRadius);
        }
        ctx->SetStrokePaint(style.contentContainer == TimelineContentContainer::OutlinedCard
                            ? accent : style.cardBorderColor);
        ctx->SetStrokeWidth(1.0);
        ctx->DrawRoundedRectangle(rect, style.cornerRadius);
    }

    void UltraCanvasTimelineDiagram::DrawConnector(IRenderContext* ctx, const ItemLayout& il) {
        if (style.connectorStyle == TimelineConnectorStyle::Hidden) return;

        ctx->SetStrokePaint(style.connectorStyle == TimelineConnectorStyle::Dashed
                            ? style.connectorColor : WithAlpha(il.color, 220));
        ctx->SetStrokeWidth(style.connectorWidth);
        if (style.connectorStyle == TimelineConnectorStyle::Dashed) {
            ctx->SetLineDash(UCDashPattern(std::vector<double>{5.0, 4.0}));
        }

        switch (style.connectorStyle) {
            case TimelineConnectorStyle::Curved: {
                const double mx = (il.connectorFrom.x + il.connectorTo.x) / 2.0;
                const double my = (il.connectorFrom.y + il.connectorTo.y) / 2.0;
                ctx->DrawBezierCurve(il.connectorFrom,
                                     Point2Dd(il.connectorFrom.x, my),
                                     Point2Dd(mx, il.connectorTo.y),
                                     il.connectorTo);
                break;
            }
            case TimelineConnectorStyle::Elbow: {
                const double mid = (il.connectorFrom.y + il.connectorTo.y) / 2.0;
                ctx->DrawLine(il.connectorFrom, Point2Dd(il.connectorFrom.x, mid));
                ctx->DrawLine(Point2Dd(il.connectorFrom.x, mid), Point2Dd(il.connectorTo.x, mid));
                ctx->DrawLine(Point2Dd(il.connectorTo.x, mid), il.connectorTo);
                break;
            }
            default:
                ctx->DrawLine(il.connectorFrom, il.connectorTo);
                break;
        }

        if (style.connectorStyle == TimelineConnectorStyle::Dashed) {
            ctx->SetLineDash(UCDashPattern());
        }
    }

    void UltraCanvasTimelineDiagram::DrawNodeShape(IRenderContext* ctx, const Point2Dd& center,
                                                   double radius, TimelineNodeShape shape) {
        switch (shape) {
            case TimelineNodeShape::Circle:
                ctx->FillCircle(center, radius);
                break;
            case TimelineNodeShape::Square:
                ctx->FillRectangle(Rect2Dd(center.x - radius, center.y - radius,
                                           radius * 2.0, radius * 2.0));
                break;
            case TimelineNodeShape::RoundedSquare:
                ctx->FillRoundedRectangle(Rect2Dd(center.x - radius, center.y - radius,
                                                  radius * 2.0, radius * 2.0), radius * 0.35);
                break;
            case TimelineNodeShape::Diamond:
                ctx->FillLinePath({Point2Dd(center.x, center.y - radius),
                                   Point2Dd(center.x + radius, center.y),
                                   Point2Dd(center.x, center.y + radius),
                                   Point2Dd(center.x - radius, center.y)});
                break;
            case TimelineNodeShape::Hexagon: {
                std::vector<Point2Dd> pts;
                for (int k = 0; k < 6; ++k) {
                    const double a = M_PI / 6.0 + k * M_PI / 3.0;
                    pts.emplace_back(center.x + radius * std::cos(a), center.y + radius * std::sin(a));
                }
                ctx->FillLinePath(pts);
                break;
            }
            case TimelineNodeShape::Pin: {
                // Teardrop: circle body with a point at the bottom
                ctx->FillCircle(Point2Dd(center.x, center.y - radius * 0.15), radius * 0.85);
                ctx->FillLinePath({Point2Dd(center.x - radius * 0.55, center.y + radius * 0.25),
                                   Point2Dd(center.x + radius * 0.55, center.y + radius * 0.25),
                                   Point2Dd(center.x, center.y + radius * 1.15)});
                break;
            }
            case TimelineNodeShape::Hidden:
            default:
                break;
        }
    }

    void UltraCanvasTimelineDiagram::DrawNode(IRenderContext* ctx, size_t index,
                                              const ItemLayout& il) {
        if (style.nodeShape == TimelineNodeShape::Hidden || il.nodeRadius <= 0.0) return;
        // The hanging design draws its bubble as the item content instead
        if (style.contentContainer == TimelineContentContainer::Bubble) return;

        const bool pending = IsPending(index);
        const Color accent = il.color;

        if (style.showShadows) {
            ctx->SetFillPaint(Color(0, 0, 0, static_cast<uint8_t>(std::clamp(style.shadowAlpha, 0, 255))));
            DrawNodeShape(ctx, Point2Dd(il.nodeCenter.x + style.shadowOffset,
                                        il.nodeCenter.y + style.shadowOffset),
                          il.nodeRadius, style.nodeShape);
        }

        // Border ring in the background color separates the node from the spine
        if (style.nodeBorderWidth > 0.0) {
            ctx->SetFillPaint(backgroundColor);
            DrawNodeShape(ctx, il.nodeCenter, il.nodeRadius + style.nodeBorderWidth,
                          style.nodeShape);
        }

        ctx->SetFillPaint((style.filledNodes && !pending) ? accent : CardFillColor());
        DrawNodeShape(ctx, il.nodeCenter, il.nodeRadius, style.nodeShape);

        if (!style.filledNodes || pending) {
            ctx->SetStrokePaint(pending ? LightenColor(accent, 0.35f) : accent);
            ctx->SetStrokeWidth(2.0);
            ctx->DrawCircle(il.nodeCenter, il.nodeRadius);
        }

        // Glyph or ordinal number inside the node
        std::string glyph = items[index].iconGlyph;
        if (glyph.empty() && style.showNodeNumbers) {
            glyph = std::to_string(index + 1);
        }
        if (!glyph.empty() && il.nodeRadius >= 9.0) {
            ctx->SetFontSize(ScaledFont(style.nodeFontSize));
            ctx->SetFontWeight(FontWeight::Bold);
            ctx->SetTextPaint((style.filledNodes && !pending)
                              ? ContrastText(accent) : accent);
            Size2Di s = ctx->GetTextLineDimensions(glyph);
            ctx->DrawText(glyph, Point2Dd(il.nodeCenter.x - s.width / 2.0,
                                          il.nodeCenter.y - s.height / 2.0));
            ctx->SetFontWeight(FontWeight::Normal);
        }
    }

    void UltraCanvasTimelineDiagram::DrawCaption(IRenderContext* ctx, size_t index,
                                                 const ItemLayout& il) {
        const std::string& caption = items[index].caption;
        if (caption.empty()) return;

        const bool onFilledBlock = (design == TimelineDesign::Chevron ||
                                    design == TimelineDesign::Steps);
        ctx->SetFontSize(ScaledFont(style.captionFontSize));
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetTextPaint(onFilledBlock ? ContrastText(il.color)
                                        : (IsPending(index) ? MutedTextColor() : il.color));

        Size2Di s = ctx->GetTextLineDimensions(caption);
        double x = il.captionRect.x;
        if (design == TimelineDesign::Bar || design == TimelineDesign::Cards ||
            design == TimelineDesign::Hanging) {
            x = il.captionRect.x + il.captionRect.width / 2.0 - s.width / 2.0;
        }
        ctx->DrawText(caption, Point2Dd(x, il.captionRect.y));
        ctx->SetFontWeight(FontWeight::Normal);
    }

    void UltraCanvasTimelineDiagram::DrawContent(IRenderContext* ctx, size_t index,
                                                 const ItemLayout& il) {
        Rect2Dd rect = il.contentRect;
        if (rect.width < 24.0 || rect.height < 12.0) return;

        const bool onFilledBlock = (design == TimelineDesign::Chevron ||
                                    design == TimelineDesign::Steps);
        const bool centered = (design == TimelineDesign::Bar ||
                               design == TimelineDesign::Cards ||
                               design == TimelineDesign::Serpentine ||
                               design == TimelineDesign::Line ||
                               design == TimelineDesign::Alternating);
        const bool pending = IsPending(index);

        // Bar-design content grows away from the spine, so bottom-align the
        // block when it sits above the bar.
        std::vector<std::string> captionLines;
        std::vector<std::string> titleLines;
        std::vector<std::string> bodyLines;

        const float titleSize = ScaledFont(style.itemTitleFontSize);
        const float bodySize = ScaledFont(style.bodyFontSize);
        const double captionLineH = ScaledFont(style.captionFontSize) * 1.4;
        const double titleLine = titleSize * 1.3;
        const double bodyLine = bodySize * 1.35;

        // A caption without its own rect is rendered as the first content line
        const bool captionInside = (il.captionRect.width <= 0.0);
        const double totalH = BuildContentLines(ctx, index, rect.width, rect.height,
                                                captionInside, captionLines,
                                                titleLines, bodyLines);

        double y = rect.y;
        if (design == TimelineDesign::Bar && il.side < 0) {
            y = rect.y + rect.height - totalH;             // grow upwards from the bar
        } else if (design == TimelineDesign::Vertical ||
                   design == TimelineDesign::Serpentine) {
            y = rect.y + std::max(0.0, (rect.height - totalH) / 2.0);
        } else if (design == TimelineDesign::Line || design == TimelineDesign::Alternating) {
            y = (il.side < 0) ? rect.y + rect.height - totalH : rect.y;
        }

        auto drawLines = [&](const std::vector<std::string>& lines, double lineHeight) {
            for (const auto& line : lines) {
                double x = rect.x;
                if (centered) {
                    Size2Di s = ctx->GetTextLineDimensions(line);
                    x = rect.x + rect.width / 2.0 - s.width / 2.0;
                }
                ctx->DrawText(line, Point2Dd(x, y));
                y += lineHeight;
            }
        };

        if (!captionLines.empty()) {
            ctx->SetFontSize(ScaledFont(style.captionFontSize));
            ctx->SetFontWeight(FontWeight::Bold);
            ctx->SetTextPaint(pending ? MutedTextColor() : il.color);
            drawLines(captionLines, captionLineH);
            ctx->SetFontWeight(FontWeight::Normal);
        }
        if (!titleLines.empty()) {
            ctx->SetFontSize(titleSize);
            ctx->SetFontWeight(FontWeight::Bold);
            ctx->SetTextPaint(onFilledBlock ? ContrastText(il.color)
                                            : (pending ? MutedTextColor() : TextColor()));
            drawLines(titleLines, titleLine);
            ctx->SetFontWeight(FontWeight::Normal);
        }
        if (!bodyLines.empty()) {
            ctx->SetFontSize(bodySize);
            ctx->SetTextPaint(onFilledBlock ? WithAlpha(ContrastText(il.color), 220)
                                            : MutedTextColor());
            drawLines(bodyLines, bodyLine);
        }
    }

    void UltraCanvasTimelineDiagram::DrawBubbleContent(IRenderContext* ctx, size_t index,
                                                       const ItemLayout& il) {
        const TimelineItem& item = items[index];
        const double r = il.nodeRadius;
        if (r < 10.0) return;

        const bool pending = IsPending(index);
        const Color fill = pending ? LightenColor(il.color, 0.55f) : il.color;

        if (style.showShadows) {
            ctx->SetFillPaint(Color(0, 0, 0, static_cast<uint8_t>(std::clamp(style.shadowAlpha, 0, 255))));
            ctx->FillCircle(Point2Dd(il.nodeCenter.x + style.shadowOffset,
                                     il.nodeCenter.y + style.shadowOffset + 2.0), r);
        }
        ctx->SetFillPaint(fill);
        ctx->FillCircle(il.nodeCenter, r);

        const Color textColor = ContrastText(fill);
        double glyphOffset = 0.0;

        // Icon/glyph sits in the upper part of the bubble
        if (!item.iconGlyph.empty()) {
            ctx->SetFontSize(ScaledFont(style.nodeFontSize * 1.3f));
            ctx->SetFontWeight(FontWeight::Bold);
            ctx->SetTextPaint(textColor);
            Size2Di s = ctx->GetTextLineDimensions(item.iconGlyph);
            ctx->DrawText(item.iconGlyph, Point2Dd(il.nodeCenter.x - s.width / 2.0,
                                                   il.nodeCenter.y - r * 0.72));
            ctx->SetFontWeight(FontWeight::Normal);
            glyphOffset = r * 0.22;
        }

        std::string text = item.title;
        if (!item.body.empty()) {
            text = text.empty() ? item.body : text + " " + item.body;
        }
        if (text.empty()) return;

        const float bodySize = ScaledFont(style.bodyFontSize);
        ctx->SetFontSize(bodySize);
        const double lineHeight = bodySize * 1.3;
        std::vector<std::string> lines =
                WrapTextInCircle(ctx, text, r - 4.0, lineHeight);
        if (style.maxBodyLines > 0 && static_cast<int>(lines.size()) > style.maxBodyLines) {
            lines.resize(style.maxBodyLines);
            lines.back() += "...";
        }
        ctx->SetTextPaint(textColor);
        DrawCenteredLines(ctx, lines,
                          Point2Dd(il.nodeCenter.x, il.nodeCenter.y + glyphOffset), lineHeight);
    }

// =============================================================================
// EVENT HANDLING
// =============================================================================

    TimelineItemRef UltraCanvasTimelineDiagram::FindItemAt(const Point2Di& pos) const {
        Point2Dd p(pos.x, pos.y);
        // Iterate backwards so the item drawn last wins on overlap
        for (size_t i = layout.itemLayouts.size(); i-- > 0;) {
            const Rect2Dd& rect = layout.itemLayouts[i].hitRect;
            if (rect.width > 0.0 && rect.height > 0.0 && rect.Contains(p)) {
                TimelineItemRef ref;
                ref.index = static_cast<int>(i);
                return ref;
            }
        }
        return TimelineItemRef();
    }

    std::string UltraCanvasTimelineDiagram::BuildItemTooltip(size_t index) const {
        const TimelineItem& item = items[index];
        if (!item.tooltip.empty()) return item.tooltip;

        std::string text = item.caption;
        if (!item.title.empty()) {
            text = text.empty() ? item.title : text + " - " + item.title;
        }
        if (!item.body.empty()) {
            text = text.empty() ? item.body : text + "\n" + item.body;
        }
        return text;
    }

    bool UltraCanvasTimelineDiagram::HandleChartMouseMove(const Point2Di& mousePos) {
        if (!layout.valid) return false;

        TimelineItemRef found = FindItemAt(mousePos);
        if (found != hoveredItem) {
            hoveredItem = found;
            if (hoveredItem.IsValid() && onItemHover) {
                onItemHover(static_cast<size_t>(hoveredItem.index), items[hoveredItem.index]);
            }
            RequestRedraw();
        }

        if (enableTooltips) {
            if (hoveredItem.IsValid()) {
                Point2Di tipPos(mousePos.x + 15, mousePos.y - 20);
                auto windowPos = MapFromLocal(tipPos, nullptr);
                UltraCanvasTooltipManager::UpdateAndShowTooltip(
                        GetWindow(), BuildItemTooltip(static_cast<size_t>(hoveredItem.index)),
                        windowPos);
            } else {
                UltraCanvasTooltipManager::HideTooltip();
            }
        }

        return hoveredItem.IsValid();
    }

    bool UltraCanvasTimelineDiagram::HandleItemClick(const UCEvent& event) {
        if (!enableSelection || !layout.valid) return false;

        TimelineItemRef clicked = FindItemAt(Point2Di(event.pointer.x, event.pointer.y));
        if (clicked.IsValid()) {
            selectedItem = (clicked == selectedItem) ? TimelineItemRef() : clicked;
            if (selectedItem.IsValid() && onItemSelect) {
                onItemSelect(static_cast<size_t>(selectedItem.index), items[selectedItem.index]);
            }
            if (onSelectionChange) onSelectionChange();
            RequestRedraw();
            return true;
        }

        if (selectedItem.IsValid()) {
            ClearSelection();
            if (onSelectionChange) onSelectionChange();
            return true;
        }
        return false;
    }

    bool UltraCanvasTimelineDiagram::HandleItemDoubleClick(const UCEvent& event) {
        if (!layout.valid) return false;
        TimelineItemRef clicked = FindItemAt(Point2Di(event.pointer.x, event.pointer.y));
        if (clicked.IsValid() && onItemDoubleClick) {
            onItemDoubleClick(static_cast<size_t>(clicked.index), items[clicked.index]);
            return true;
        }
        return false;
    }

    bool UltraCanvasTimelineDiagram::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseDown:
                if (event.button == UCMouseButton::Left && HandleItemClick(event)) {
                    return true;
                }
                return UltraCanvasChartElementBase::OnEvent(event);

            case UCEventType::MouseDoubleClick:
                return HandleItemDoubleClick(event);

            case UCEventType::MouseLeave:
                if (hoveredItem.IsValid()) {
                    hoveredItem = TimelineItemRef();
                    RequestRedraw();
                }
                UltraCanvasTooltipManager::HideTooltip();
                return false;

            default:
                return UltraCanvasChartElementBase::OnEvent(event);
        }
    }

// =============================================================================
// STYLE PRESETS
// =============================================================================

    namespace TimelineDiagramStyles {

        TimelineDiagramStyle CreateForDesign(TimelineDesign design) {
            TimelineDiagramStyle s;
            switch (design) {
                case TimelineDesign::Bar:
                    s.spineThickness = 26.0;
                    s.spineCornerRadius = 13.0;
                    s.nodeRadius = 17.0;
                    s.nodeBorderWidth = 3.0;
                    s.connectorStyle = TimelineConnectorStyle::Hidden;
                    s.contentContainer = TimelineContentContainer::Plain;
                    s.captionFontSize = 22.0f;
                    s.showShadows = false;
                    break;

                case TimelineDesign::Line:
                    s.spineThickness = 3.0;
                    s.spineCornerRadius = 1.5;
                    s.nodeRadius = 9.0;
                    s.nodeBorderWidth = 3.0;
                    s.connectorStyle = TimelineConnectorStyle::Straight;
                    s.connectorLength = 26.0;
                    s.captionFontSize = 17.0f;
                    s.showSpineArrow = true;
                    break;

                case TimelineDesign::Alternating:
                    s.spineThickness = 4.0;
                    s.spineCornerRadius = 2.0;
                    s.nodeRadius = 10.0;
                    s.connectorStyle = TimelineConnectorStyle::Straight;
                    s.connectorLength = 30.0;
                    s.contentContainer = TimelineContentContainer::Card;
                    s.captionFontSize = 16.0f;
                    s.showShadows = true;
                    break;

                case TimelineDesign::Cards:
                    s.showSpine = false;
                    s.nodeRadius = 16.0;
                    s.nodeBorderWidth = 0.0;
                    s.connectorStyle = TimelineConnectorStyle::Hidden;
                    s.contentContainer = TimelineContentContainer::Plain;
                    s.captionFontSize = 18.0f;
                    s.showNodeNumbers = true;
                    s.showShadows = true;
                    s.itemGap = 16.0;
                    break;

                case TimelineDesign::Vertical:
                    s.spineThickness = 4.0;
                    s.spineCornerRadius = 2.0;
                    s.nodeRadius = 11.0;
                    s.connectorStyle = TimelineConnectorStyle::Straight;
                    s.connectorLength = 22.0;
                    s.contentContainer = TimelineContentContainer::Card;
                    s.captionFontSize = 15.0f;
                    s.showShadows = true;
                    break;

                case TimelineDesign::Serpentine:
                    s.spineThickness = 22.0;
                    s.nodeRadius = 20.0;
                    s.nodeBorderWidth = 3.0;
                    s.connectorStyle = TimelineConnectorStyle::Hidden;
                    s.contentContainer = TimelineContentContainer::Plain;
                    s.captionFontSize = 14.0f;
                    s.itemTitleFontSize = 12.0f;
                    s.serpentineItemsPerRun = 4;
                    s.serpentineTurnRadius = 44.0;
                    s.showNodeNumbers = true;
                    break;

                case TimelineDesign::Hanging:
                    s.spineThickness = 5.0;
                    s.spineCornerRadius = 2.5;
                    s.contentContainer = TimelineContentContainer::Bubble;
                    s.connectorStyle = TimelineConnectorStyle::Straight;
                    s.connectorWidth = 2.0;
                    s.captionFontSize = 16.0f;
                    s.bodyFontSize = 9.5f;
                    s.showShadows = true;
                    s.shadowAlpha = 30;
                    s.hangingTiers = 2;
                    s.maxBodyLines = 8;
                    s.scaleFontSize = 15.0f;
                    break;

                case TimelineDesign::Chevron:
                    s.showSpine = false;
                    s.showNodes = false;
                    s.connectorStyle = TimelineConnectorStyle::Hidden;
                    s.captionFontSize = 17.0f;
                    s.itemGap = 6.0;
                    break;

                case TimelineDesign::Steps:
                    s.showSpine = false;
                    s.showNodes = false;
                    s.connectorStyle = TimelineConnectorStyle::Hidden;
                    s.captionFontSize = 17.0f;
                    s.cornerRadius = 6.0;
                    s.itemGap = 8.0;
                    s.showShadows = true;
                    break;
            }
            return s;
        }

        std::vector<Color> GetPaletteColors(TimelinePalette palette) {
            switch (palette) {
                case TimelinePalette::Vibrant:
                    return {Color(52, 152, 219, 255), Color(46, 204, 113, 255),
                            Color(241, 196, 15, 255), Color(230, 126, 34, 255),
                            Color(231, 76, 60, 255), Color(155, 89, 182, 255),
                            Color(26, 188, 156, 255), Color(52, 73, 94, 255)};
                case TimelinePalette::Pastel:
                    return {Color(129, 199, 212, 255), Color(160, 214, 180, 255),
                            Color(247, 214, 141, 255), Color(244, 177, 131, 255),
                            Color(240, 152, 152, 255), Color(190, 168, 220, 255),
                            Color(150, 206, 199, 255), Color(176, 186, 200, 255)};
                case TimelinePalette::Ocean:
                    return {Color(13, 71, 111, 255), Color(21, 101, 152, 255),
                            Color(31, 133, 176, 255), Color(46, 162, 186, 255),
                            Color(78, 190, 189, 255), Color(122, 209, 190, 255),
                            Color(163, 223, 197, 255), Color(200, 235, 213, 255)};
                case TimelinePalette::Sunset:
                    return {Color(122, 40, 96, 255), Color(176, 52, 92, 255),
                            Color(219, 80, 74, 255), Color(238, 124, 62, 255),
                            Color(247, 165, 60, 255), Color(250, 200, 88, 255),
                            Color(252, 222, 133, 255), Color(253, 237, 183, 255)};
                case TimelinePalette::Forest:
                    return {Color(27, 67, 50, 255), Color(45, 106, 79, 255),
                            Color(64, 145, 108, 255), Color(82, 183, 136, 255),
                            Color(116, 198, 157, 255), Color(149, 213, 178, 255),
                            Color(183, 228, 199, 255), Color(216, 243, 220, 255)};
                case TimelinePalette::Slate:
                    return {Color(46, 52, 64, 255), Color(59, 66, 82, 255),
                            Color(76, 86, 106, 255), Color(94, 107, 130, 255),
                            Color(115, 129, 152, 255), Color(136, 150, 171, 255),
                            Color(160, 172, 190, 255), Color(184, 194, 208, 255)};
                case TimelinePalette::Mono:
                    return {Color(33, 37, 43, 255), Color(60, 66, 76, 255),
                            Color(90, 97, 109, 255), Color(120, 128, 141, 255),
                            Color(150, 158, 171, 255), Color(180, 187, 198, 255)};
                case TimelinePalette::CorporateBlue:
                case TimelinePalette::Custom:
                default:
                    return {Color(31, 97, 176, 255), Color(41, 128, 185, 255),
                            Color(52, 152, 219, 255), Color(93, 173, 226, 255),
                            Color(26, 188, 156, 255), Color(22, 160, 133, 255),
                            Color(72, 100, 132, 255), Color(120, 144, 168, 255)};
            }
        }

    } // namespace TimelineDiagramStyles

// =============================================================================
// SAMPLE DATA
// =============================================================================

    namespace TimelineDiagramSamples {

        std::vector<TimelineItem> CompanyHistory() {
            std::vector<TimelineItem> list;
            list.emplace_back("2015", "Founded",
                              "Three engineers start the company in a rented workshop.");
            list.emplace_back("2016", "First product",
                              "The first hardware controller ships to twelve pilot customers.");
            list.emplace_back("2017", "Series A",
                              "Funding round closes and the team grows to forty people.");
            list.emplace_back("2018", "Going global",
                              "Offices open in three countries and support moves to 24/7.");
            list.emplace_back("2019", "Platform launch",
                              "The cloud platform replaces the on-premise toolchain.");
            list.emplace_back("2020", "One million users",
                              "The platform passes a million registered accounts.");

            const char* glyphs[] = {"1", "2", "3", "4", "5", "6"};
            for (size_t i = 0; i < list.size(); ++i) {
                list[i].iconGlyph = glyphs[i];
                list[i].SetDate(2015 + static_cast<int>(i), 6, 1);
            }
            return list;
        }

        std::vector<TimelineItem> ProjectYear(int year) {
            static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
            static const char* titles[] = {
                    "Kick-off", "Research", "Concept", "Prototype",
                    "Field test", "Review", "Redesign", "Pilot",
                    "Certification", "Ramp-up", "Launch", "Retrospective"};
            static const char* bodies[] = {
                    "Scope agreed with all stakeholders and the budget signed off.",
                    "User interviews and a survey of the competing products.",
                    "Two concept directions worked up and compared.",
                    "A working prototype assembled from off-the-shelf parts.",
                    "Four weeks of testing at three customer sites.",
                    "Findings reviewed and the roadmap adjusted.",
                    "Housing and control layout reworked after the field test.",
                    "Limited pilot with the twenty most active customers.",
                    "Safety and EMC certification completed.",
                    "Production ramped to full volume.",
                    "Public launch with press and partner events.",
                    "Lessons learned written up for the next cycle."};

            std::vector<TimelineItem> list;
            for (int m = 0; m < 12; ++m) {
                TimelineItem item(months[m], titles[m], bodies[m]);
                item.SetDate(year, m + 1, 15);
                list.push_back(item);
            }
            return list;
        }

    } // namespace TimelineDiagramSamples

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

    std::shared_ptr<UltraCanvasTimelineDiagram> CreateTimelineDiagramElement(
            const std::string& id, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasTimelineDiagram>(id, x, y, width, height);
    }

    std::shared_ptr<UltraCanvasTimelineDiagram> CreateTimelineDiagramElement(
            const std::string& id, int x, int y, int width, int height,
            TimelineDesign design) {
        auto diagram = std::make_shared<UltraCanvasTimelineDiagram>(id, x, y, width, height);
        diagram->ApplyDesign(design);
        return diagram;
    }

    std::shared_ptr<UltraCanvasTimelineDiagram> CreateTimelineDiagramWithData(
            const std::string& id, int x, int y, int width, int height,
            const std::vector<TimelineItem>& items, TimelineDesign design) {
        auto diagram = std::make_shared<UltraCanvasTimelineDiagram>(id, x, y, width, height);
        diagram->ApplyDesign(design);
        diagram->SetItems(items);
        return diagram;
    }

} // namespace UltraCanvas
