// UltraCanvasAdjacencyDiagram.cpp
// Architectural space-planning adjacency diagram
// Rooms as area-proportional circles, edges as solid/dashed adjacency links,
// functional zones as dashed bounding regions.
// Version: 1.1.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasAdjacencyDiagram.h"
#include "UltraCanvasLabelPlacement.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <limits>

namespace UltraCanvas {

// ─────────────────────────────────────────────
// CONSTRUCTION
// ─────────────────────────────────────────────

    UltraCanvasAdjacencyDiagram::UltraCanvasAdjacencyDiagram(
            const std::string& id,
            float x, float y, float w, float h)
            : UltraCanvasUIElement(id, x, y, w, h)
    {}

// ─────────────────────────────────────────────
// ROOM API
// ─────────────────────────────────────────────

    int UltraCanvasAdjacencyDiagram::AddRoom(const AdjacencyRoom& room) {
        if (LookupRoom(room.id) >= 0) return LookupRoom(room.id);
        int idx = static_cast<int>(rooms.size());
        rooms.push_back(room);
        InvalidateMatrixLayout();
        RebuildLegend();
        RequestRedraw();
        return idx;
    }

    int UltraCanvasAdjacencyDiagram::AddRoom(
            const std::string& id, const std::string& label,
            float areaSqM, float x, float y,
            RoomFunctionType type)
    {
        AdjacencyRoom r;
        r.id           = id;
        r.label        = label;
        r.areaSqM      = areaSqM;
        r.x            = x;
        r.y            = y;
        r.functionType = type;
        return AddRoom(r);
    }

    bool UltraCanvasAdjacencyDiagram::RemoveRoom(const std::string& id) {
        int idx = LookupRoom(id);
        if (idx < 0) return false;

        rooms.erase(rooms.begin() + idx);
        InvalidateMatrixLayout();
        RebuildLegend();

        links.erase(
                std::remove_if(links.begin(), links.end(),
                               [&id](const AdjacencyLink& l) {
                                   return l.sourceId == id || l.targetId == id;
                               }),
                links.end());

        // Remove from all zones
        for (auto& zone : zones) {
            zone.roomIds.erase(
                    std::remove(zone.roomIds.begin(), zone.roomIds.end(), id),
                    zone.roomIds.end());
        }

        RequestRedraw();
        return true;
    }

    void UltraCanvasAdjacencyDiagram::UpdateRoom(
            const std::string& id, const AdjacencyRoom& updated)
    {
        int idx = LookupRoom(id);
        if (idx < 0) return;
        rooms[idx] = updated;
        rooms[idx].id = id;
        RequestRedraw();
    }

    void UltraCanvasAdjacencyDiagram::MoveRoom(
            const std::string& id, float x, float y)
    {
        int idx = LookupRoom(id);
        if (idx < 0) return;
        rooms[idx].x = x;
        rooms[idx].y = y;
        RequestRedraw();
    }

    const AdjacencyRoom* UltraCanvasAdjacencyDiagram::GetRoom(int index) const {
        if (index < 0 || index >= static_cast<int>(rooms.size())) return nullptr;
        return &rooms[index];
    }

    const AdjacencyRoom* UltraCanvasAdjacencyDiagram::GetRoomById(const std::string& id) const {
        int idx = LookupRoom(id);
        return idx >= 0 ? &rooms[idx] : nullptr;
    }

// ─────────────────────────────────────────────
// LINK API
// ─────────────────────────────────────────────

    int UltraCanvasAdjacencyDiagram::AddLink(const AdjacencyLink& link) {
        int idx = static_cast<int>(links.size());
        links.push_back(link);
        RequestRedraw();
        return idx;
    }

    int UltraCanvasAdjacencyDiagram::AddLink(
            const std::string& sourceId,
            const std::string& targetId,
            AdjacencyLinkType type,
            bool directed)
    {
        AdjacencyLink l;
        l.sourceId = sourceId;
        l.targetId = targetId;
        l.type     = type;
        l.directed = directed;
        return AddLink(l);
    }

    int UltraCanvasAdjacencyDiagram::AddLink(
            const std::string& sourceId,
            const std::string& targetId,
            AdjacencyPriority priority)
    {
        AdjacencyLink l;
        l.sourceId = sourceId;
        l.targetId = targetId;
        l.priority = priority;
        // Give the bubble view a sensible line style for the priority, so a
        // programme expressed as must/should/maybe still reads correctly there.
        l.type = (priority == AdjacencyPriority::Must)   ? AdjacencyLinkType::Direct
               : (priority == AdjacencyPriority::Should) ? AdjacencyLinkType::Secondary
                                                         : AdjacencyLinkType::ServiceOnly;
        return AddLink(l);
    }

    bool UltraCanvasAdjacencyDiagram::SetLinkPriority(
            const std::string& sourceId,
            const std::string& targetId,
            AdjacencyPriority priority)
    {
        bool found = false;
        for (AdjacencyLink& l : links) {
            if ((l.sourceId == sourceId && l.targetId == targetId) ||
                (l.sourceId == targetId && l.targetId == sourceId)) {
                l.priority = priority;
                found = true;
            }
        }
        if (found) RequestRedraw();
        return found;
    }

    const AdjacencyLink* UltraCanvasAdjacencyDiagram::FindLink(
            const std::string& sourceId,
            const std::string& targetId) const
    {
        for (const AdjacencyLink& l : links) {
            if ((l.sourceId == sourceId && l.targetId == targetId) ||
                (l.sourceId == targetId && l.targetId == sourceId)) {
                return &l;
            }
        }
        return nullptr;
    }

    void UltraCanvasAdjacencyDiagram::RemoveLink(
            const std::string& sourceId,
            const std::string& targetId)
    {
        links.erase(
                std::remove_if(links.begin(), links.end(),
                               [&](const AdjacencyLink& l) {
                                   return (l.sourceId == sourceId && l.targetId == targetId)
                                          || (l.sourceId == targetId && l.targetId == sourceId);
                               }),
                links.end());
        RequestRedraw();
    }

    void UltraCanvasAdjacencyDiagram::ClearLinks() {
        links.clear();
        RequestRedraw();
    }

// ─────────────────────────────────────────────
// ZONE API
// ─────────────────────────────────────────────

    void UltraCanvasAdjacencyDiagram::AddZone(const AdjacencyZone& zone) {
        // Replace if ID already exists
        int idx = LookupZone(zone.id);
        if (idx >= 0) zones[idx] = zone;
        else          zones.push_back(zone);
        RequestRedraw();
    }

    void UltraCanvasAdjacencyDiagram::RemoveZone(const std::string& id) {
        int idx = LookupZone(id);
        if (idx >= 0) {
            zones.erase(zones.begin() + idx);
            RequestRedraw();
        }
    }

    void UltraCanvasAdjacencyDiagram::ClearZones() {
        zones.clear();
        RequestRedraw();
    }

    void UltraCanvasAdjacencyDiagram::Clear() {
        rooms.clear();
        links.clear();
        zones.clear();
        hoveredRoomIdx = hoveredLinkIdx = -1;
        selectedRoomIdx = selectedLinkIdx = -1;
        showingTooltip = false;
        tooltipText.clear();
        panOffsetX = panOffsetY = 0.0f;
        hoveredMatrixRow = hoveredMatrixCol = -1;
        selectedMatrixRow = selectedMatrixCol = -1;
        matrixOrder.clear();
        InvalidateMatrixLayout();
        RebuildLegend();
        RequestRedraw();
    }

// ─────────────────────────────────────────────
// COORDINATE HELPERS
// ─────────────────────────────────────────────

    void UltraCanvasAdjacencyDiagram::DiagramToScreen(
            float dx, float dy, float& sx, float& sy) const
    {
        sx = dx + panOffsetX;
        sy = dy + panOffsetY;
    }

    void UltraCanvasAdjacencyDiagram::ScreenToDiagram(
            float sx, float sy, float& dx, float& dy) const
    {
        dx = sx - panOffsetX;
        dy = sy - panOffsetY;
    }

    void UltraCanvasAdjacencyDiagram::CenterContent() {
        if (rooms.empty()) return;
        float minX =  std::numeric_limits<float>::max();
        float minY =  std::numeric_limits<float>::max();
        float maxX = -std::numeric_limits<float>::max();
        float maxY = -std::numeric_limits<float>::max();
        for (const auto& r : rooms) {
            float rad = RoomRadius(r);
            minX = std::min(minX, r.x - rad);
            minY = std::min(minY, r.y - rad);
            maxX = std::max(maxX, r.x + rad);
            maxY = std::max(maxY, r.y + rad);
        }
        float bboxCx = 0.5f * (minX + maxX);
        float bboxCy = 0.5f * (minY + maxY);
        float widgetCx = 0.5f * static_cast<float>(GetWidth());
        float widgetCy = 0.5f * static_cast<float>(GetHeight());
        panOffsetX = widgetCx - bboxCx;
        panOffsetY = widgetCy - bboxCy;
        RequestRedraw();
    }

// ─────────────────────────────────────────────
// STYLING HELPERS
// ─────────────────────────────────────────────

    float UltraCanvasAdjacencyDiagram::RoomRadius(const AdjacencyRoom& room) const {
        float r = std::sqrt(std::max(1.0f, room.areaSqM)) * style.areaScale;
        return std::max(style.minRadius, std::min(style.maxRadius, r));
    }

    Color UltraCanvasAdjacencyDiagram::RoomColor(const AdjacencyRoom& room) const {
        switch (room.functionType) {
            case RoomFunctionType::Public:      return style.colorPublic;
            case RoomFunctionType::Private:     return style.colorPrivate;
            case RoomFunctionType::Service:     return style.colorService;
            case RoomFunctionType::Support:     return style.colorSupport;
            case RoomFunctionType::Circulation: return style.colorCirculation;
            default:                            return room.color;
        }
    }

    bool UltraCanvasAdjacencyDiagram::IsLightColor(const Color& c) const {
        float lum = (c.r * 0.299f + c.g * 0.587f + c.b * 0.114f) / 255.0f;
        return lum > 0.55f;
    }

    void UltraCanvasAdjacencyDiagram::DrawTextWithHalo(
            IRenderContext* ctx, const std::string& text,
            float x, float y, const Color& textColor) const
    {
        // A label is drawn centered on its circle, but small circles are often
        // narrower than the label — the text spills onto the surrounding zone
        // fill or diagram background, where it may match the text color and
        // become unreadable. Draw a contrasting halo behind the text so it
        // stays legible regardless of what is behind it.
        if (style.labelHalo && style.labelHaloWidth > 0.0f) {
            // Halo contrasts the text itself: light text gets a dark halo,
            // dark text gets a light halo.
            const Color& halo = IsLightColor(textColor)
                                ? style.labelHaloDark
                                : style.labelHaloLight;
            ctx->SetTextPaint(halo);

            float w = style.labelHaloWidth;
            static const float dirs[8][2] = {
                {-1, 0}, {1, 0}, {0, -1}, {0, 1},
                {-1, -1}, {1, -1}, {-1, 1}, {1, 1}
            };
            for (const auto& d : dirs) {
                ctx->DrawText(text, Point2Dd(x + d[0] * w, y + d[1] * w));
            }
        }

        ctx->SetTextPaint(textColor);
        ctx->DrawText(text, Point2Dd(x, y));
    }

// ─────────────────────────────────────────────
// ZONE BOUNDS
// ─────────────────────────────────────────────

    Rect2Dd UltraCanvasAdjacencyDiagram::ComputeZoneBounds(const AdjacencyZone& zone) const {
        if (zone.roomIds.empty()) return Rect2Dd(0, 0, 0, 0);

        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float maxY = std::numeric_limits<float>::lowest();

        for (const auto& rid : zone.roomIds) {
            int idx = LookupRoom(rid);
            if (idx < 0) continue;
            const AdjacencyRoom& room = rooms[idx];
            float r = RoomRadius(room);
            float sx, sy;
            DiagramToScreen(room.x, room.y, sx, sy);

            minX = std::min(minX, sx - r);
            minY = std::min(minY, sy - r);
            maxX = std::max(maxX, sx + r);
            maxY = std::max(maxY, sy + r);
        }

        float pad = zone.padding;
        return Rect2Dd(
                minX - pad,
                minY - pad,
                (maxX - minX) + pad * 2.0f,
                (maxY - minY) + pad * 2.0f);
    }

// ─────────────────────────────────────────────
// DRAW HELPERS
// ─────────────────────────────────────────────

    void UltraCanvasAdjacencyDiagram::DrawDashedLine(
            IRenderContext* ctx,
            float x1, float y1, float x2, float y2,
            float dashLen, float gapLen) const
    {
        float dx = x2 - x1;
        float dy = y2 - y1;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 0.1f) return;

        float ux = dx / dist;
        float uy = dy / dist;
        float total = dashLen + gapLen;
        float traveled = 0.0f;
        bool drawing = true;

        while (traveled < dist) {
            float segLen = drawing ? dashLen : gapLen;
            segLen = std::min(segLen, dist - traveled);

            if (drawing) {
                float sx = x1 + ux * traveled;
                float sy = y1 + uy * traveled;
                float ex = x1 + ux * (traveled + segLen);
                float ey = y1 + uy * (traveled + segLen);
                ctx->DrawLine(Point2Dd(sx, sy), Point2Dd(ex, ey));
            }

            traveled += segLen;
            drawing = !drawing;
        }
    }

    void UltraCanvasAdjacencyDiagram::DrawArrowhead(
            IRenderContext* ctx,
            float tipX, float tipY, float angle,
            const Color& col) const
    {
        float sz = style.arrowSize;
        float spread = 0.4f;

        float ax = tipX - sz * std::cos(angle - spread);
        float ay = tipY - sz * std::sin(angle - spread);
        float bx = tipX - sz * std::cos(angle + spread);
        float by = tipY - sz * std::sin(angle + spread);

        ctx->SetFillPaint(col);
        ctx->ClearPath();
        ctx->MoveTo(tipX, tipY);
        ctx->LineTo(ax, ay);
        ctx->LineTo(bx, by);
        ctx->ClosePath();
        ctx->Fill();
    }

    void UltraCanvasAdjacencyDiagram::DrawLink(
            IRenderContext* ctx,
            const AdjacencyLink& link,
            int srcIdx, int tgtIdx,
            bool hovered, bool selected) const
    {
        const AdjacencyRoom& src = rooms[srcIdx];
        const AdjacencyRoom& tgt = rooms[tgtIdx];

        float sx, sy, tx, ty;
        DiagramToScreen(src.x, src.y, sx, sy);
        DiagramToScreen(tgt.x, tgt.y, tx, ty);

        // Shorten line to stop at circle edges
        float srcR = RoomRadius(src);
        float tgtR = RoomRadius(tgt);
        float dx = tx - sx;
        float dy = ty - sy;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 1.0f) return;

        float ux = dx / dist;
        float uy = dy / dist;
        float lx1 = sx + ux * srcR;
        float ly1 = sy + uy * srcR;
        float lx2 = tx - ux * tgtR;
        float ly2 = ty - uy * tgtR;

        // Select style parameters
        Color col;
        float lw;
        float dashLen = 0.0f;
        float gapLen  = 0.0f;

        switch (link.type) {
            case AdjacencyLinkType::Secondary:
                col     = style.secondaryLinkColor;
                lw      = style.secondaryLinkWidth * link.weight;
                dashLen = style.dashLength;
                gapLen  = style.dashGap;
                break;
            case AdjacencyLinkType::ServiceOnly:
                col     = style.serviceLinkColor;
                lw      = style.serviceLinkWidth * link.weight;
                dashLen = style.dotLength;
                gapLen  = style.dashGap;
                break;
            default: // Direct
                col     = style.directLinkColor;
                lw      = style.directLinkWidth * link.weight;
                break;
        }

        // Brighten on hover / selected
        if (hovered || selected) {
            col.r = static_cast<uint8_t>(std::min(255, static_cast<int>(col.r) + 50));
            col.g = static_cast<uint8_t>(std::min(255, static_cast<int>(col.g) + 30));
            col.a = 255;
            lw   += 1.0f;
        }

        ctx->SetStrokePaint(col);
        ctx->SetStrokeWidth(lw);

        if (dashLen > 0.0f) {
            DrawDashedLine(ctx, lx1, ly1, lx2, ly2, dashLen, gapLen);
        } else {
            ctx->DrawLine(Point2Dd(lx1, ly1), Point2Dd(lx2, ly2));
        }

        // Arrowhead
        if (link.directed && style.arrowSize > 0.0f) {
            float angle = std::atan2(ly2 - ly1, lx2 - lx1);
            DrawArrowhead(ctx, lx2, ly2, angle, col);
        }

        // Link label at midpoint
        if (!link.label.empty()) {
            float midX = (lx1 + lx2) * 0.5f;
            float midY = (ly1 + ly2) * 0.5f;
            ctx->SetFontSize(style.tooltipFontSize - 1.0f);
            ctx->SetFontFace("Sans", FontWeight::Normal, FontSlant::Normal);
            auto dims = ctx->GetTextLineDimensions(link.label);
            int tw = dims.width, th = dims.height;
            DrawTextWithHalo(ctx, link.label,
                             midX - tw * 0.5f, midY - th - 2.0f,
                             style.zoneLabelColor);
        }
    }

    void UltraCanvasAdjacencyDiagram::DrawZones(IRenderContext* ctx) const {
        for (const auto& zone : zones) {
            Rect2Dd bnds = ComputeZoneBounds(zone);
            if (bnds.width <= 0 || bnds.height <= 0) continue;

            // Translucent fill
            ctx->SetFillPaint(zone.fillColor);
            ctx->FillRoundedRectangle(bnds, zone.cornerRadius);

            // Dashed border — draw as four dashed sides
            ctx->SetStrokePaint(zone.borderColor);
            ctx->SetStrokeWidth(style.zoneBorderWidth);

            float x = bnds.x;
            float y = bnds.y;
            float w = bnds.width;
            float h = bnds.height;
            float d = style.dashLength + 1.0f;
            float g = style.dashGap;

            DrawDashedLine(ctx, x, y, x + w, y, d, g);           // top
            DrawDashedLine(ctx, x + w, y, x + w, y + h, d, g);   // right
            DrawDashedLine(ctx, x + w, y + h, x, y + h, d, g);   // bottom
            DrawDashedLine(ctx, x, y + h, x, y, d, g);            // left

            // Zone label at top-left inside the bounding box
            if (!zone.label.empty()) {
                ctx->SetFontSize(style.zoneLabelFontSize);
                ctx->SetFontFace("Sans", FontWeight::Normal, FontSlant::Normal);
                DrawTextWithHalo(ctx, zone.label,
                                 x + 8.0f, y + style.zoneLabelFontSize + 4.0f,
                                 style.zoneLabelColor);
            }
        }
    }

    void UltraCanvasAdjacencyDiagram::DrawLinks(IRenderContext* ctx) const {
        for (int li = 0; li < static_cast<int>(links.size()); ++li) {
            const AdjacencyLink& link = links[li];
            int srcIdx = LookupRoom(link.sourceId);
            int tgtIdx = LookupRoom(link.targetId);
            if (srcIdx < 0 || tgtIdx < 0 || srcIdx == tgtIdx) continue;

            bool hovered  = (li == hoveredLinkIdx);
            bool selected = (li == selectedLinkIdx);
            DrawLink(ctx, link, srcIdx, tgtIdx, hovered, selected);
        }
    }

    void UltraCanvasAdjacencyDiagram::DrawRooms(IRenderContext* ctx) const {
        for (int i = 0; i < static_cast<int>(rooms.size()); ++i) {
            const AdjacencyRoom& room = rooms[i];
            float r = RoomRadius(room);
            Color fill = RoomColor(room);

            float sx, sy;
            DiagramToScreen(room.x, room.y, sx, sy);

            // Fill
            ctx->SetFillPaint(fill);
            ctx->FillCircle(Point2Dd(sx, sy), r);

            // Stroke — thicker + colored on hover/select
            Color strokeCol = style.roomStrokeColor;
            float strokeW   = style.roomStrokeWidth;
            if (i == selectedRoomIdx) {
                strokeCol = style.roomSelectedStroke;
                strokeW   = 3.0f;
            } else if (i == hoveredRoomIdx) {
                strokeCol = style.roomHoverStroke;
                strokeW   = 2.5f;
            }

            ctx->SetStrokePaint(strokeCol);
            ctx->SetStrokeWidth(strokeW);
            ctx->DrawCircle(Point2Dd(sx, sy), r);
        }
    }

    void UltraCanvasAdjacencyDiagram::DrawLabels(IRenderContext* ctx) const {
        if (rooms.empty()) return;

        ctx->SetFontFace("Sans", FontWeight::Normal, FontSlant::Normal);

        // Every room circle becomes a shape for the shared placement solver.
        // A room name is meant to sit centred on its circle and may spill over
        // the outline - the halo keeps it readable, so the overflow is not
        // scored against. What must not happen is two names landing on top of
        // each other, so a blocked label steps to another anchor inside its
        // circle and, failing that, off the circle entirely.
        struct RoomLabel {
            size_t roomIndex = 0;
            std::string label, note;
            Size2Dd labelSize, noteSize;
            Color textColor;
        };
        std::vector<LabelShape> shapes(rooms.size());
        std::vector<RoomLabel> pending;
        std::vector<ShapeLabel> labels;

        for (size_t i = 0; i < rooms.size(); ++i) {
            const AdjacencyRoom& room = rooms[i];
            float sx, sy;
            DiagramToScreen(room.x, room.y, sx, sy);

            shapes[i].type = LabelShapeType::Circle;
            shapes[i].center = Point2Dd(sx, sy);
            shapes[i].radius = RoomRadius(room);
            shapes[i].keepLabelInside = false;

            if (room.label.empty() && room.note.empty()) continue;

            RoomLabel rl;
            rl.roomIndex = i;
            rl.label = room.label;
            rl.note = room.note;
            rl.textColor = IsLightColor(RoomColor(room)) ? style.labelColorDark
                                                         : style.labelColor;
            ctx->SetFontSize(style.labelFontSize);
            if (!rl.label.empty()) {
                auto d = ctx->GetTextLineDimensions(rl.label);
                rl.labelSize = Size2Dd(d.width, d.height);
            }
            if (!rl.note.empty()) {
                ctx->SetFontSize(style.noteFontSize);
                auto d = ctx->GetTextLineDimensions(rl.note);
                rl.noteSize = Size2Dd(d.width, d.height);
            }

            ShapeLabel l;
            l.text = rl.label.empty() ? rl.note : rl.label;
            l.shapeIndex = i;
            l.preferredSide = LabelSide::Inside;
            // Name (and note) are placed as one block.
            l.textSize = Size2Dd(std::max(rl.labelSize.width, rl.noteSize.width),
                                 rl.labelSize.height + rl.noteSize.height);
            l.tolerateShapeOverflow = true;   // text over the outline is the look
            l.allowOutsideFallback = true;    // but never text over text
            labels.push_back(l);
            pending.push_back(rl);
        }
        if (pending.empty()) return;

        // Smallest rooms first: their labels are the ones with no room to
        // spare, so they get first pick of the free space.
        std::vector<size_t> order(pending.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            return shapes[pending[a].roomIndex].radius < shapes[pending[b].roomIndex].radius;
        });
        std::vector<ShapeLabel> orderedLabels;
        std::vector<RoomLabel> orderedPending;
        orderedLabels.reserve(order.size());
        orderedPending.reserve(order.size());
        for (size_t idx : order) {
            orderedLabels.push_back(labels[idx]);
            orderedPending.push_back(pending[idx]);
        }

        LabelPlacementOptions opts;
        opts.bounds = Rect2Dd(0.0, 0.0, GetWidth(), GetHeight());
        opts.shapeMargin = 4.0;
        opts.labelMargin = 2.0;

        std::vector<PlacedShapeLabel> placed =
                PlaceShapeLabels(shapes, orderedLabels, opts);

        for (size_t i = 0; i < placed.size(); ++i) {
            const RoomLabel& rl = orderedPending[i];
            const Rect2Dd& box = placed[i].bounds;
            double y = box.y;
            if (!rl.label.empty()) {
                ctx->SetFontSize(style.labelFontSize);
                DrawTextWithHalo(ctx, rl.label,
                                 static_cast<float>(box.x + (box.width - rl.labelSize.width) * 0.5),
                                 static_cast<float>(y), rl.textColor);
                y += rl.labelSize.height;
            }
            if (!rl.note.empty()) {
                ctx->SetFontSize(style.noteFontSize);
                DrawTextWithHalo(ctx, rl.note,
                                 static_cast<float>(box.x + (box.width - rl.noteSize.width) * 0.5),
                                 static_cast<float>(y), rl.textColor);
            }
        }
    }

    void UltraCanvasAdjacencyDiagram::DrawTooltip(IRenderContext* ctx) const {
        if (!showingTooltip || tooltipText.empty()) return;

        ctx->SetFontSize(style.tooltipFontSize);
        ctx->SetFontFace("Sans", FontWeight::Normal, FontSlant::Normal);

        auto dims = ctx->GetTextLineDimensions(tooltipText);
        int tw = dims.width, th = dims.height;

        float pad  = 6.0f;
        float boxW = tw + pad * 2.0f;
        float boxH = th + pad * 2.0f;

        float bx = tooltipX + 14.0f;
        float by = tooltipY - boxH - 4.0f;
        if (bx + boxW > GetWidth())  bx = tooltipX - boxW - 4.0f;
        if (by < 0)                  by = tooltipY + 14.0f;

        ctx->SetFillPaint(style.tooltipBackground);
        ctx->FillRectangle(Rect2Dd(bx, by, boxW, boxH));

        ctx->SetTextPaint(style.tooltipText);
        ctx->DrawText(tooltipText, Point2Dd(bx + pad, by + pad));
    }

// ─────────────────────────────────────────────
// RENDER
// ─────────────────────────────────────────────

    void UltraCanvasAdjacencyDiagram::Render(IRenderContext* ctx, const Rect2Df& dirtyrects) {
        if (rooms.empty() && zones.empty()) return;

        if (view == AdjacencyView::Matrix) {
            RenderMatrix(ctx);
            DrawTooltip(ctx);
            return;
        }

        DrawZones(ctx);     // zones first (background)
        DrawLinks(ctx);     // links above zones
        DrawRooms(ctx);     // rooms above links
        DrawLabels(ctx);    // labels on top of rooms

        if (legend && legend->IsVisible()) {
            Rect2Df local = GetLocalBounds();
            legend->Render(ctx, Rect2Dd(local.x, local.y, local.width, local.height));
        }

        DrawTooltip(ctx);
    }

// ─────────────────────────────────────────────
// HIT TESTING
// ─────────────────────────────────────────────

    int UltraCanvasAdjacencyDiagram::HitTestRoom(float localX, float localY) const {
        // Test in reverse order (last added = topmost)
        for (int i = static_cast<int>(rooms.size()) - 1; i >= 0; --i) {
            const AdjacencyRoom& room = rooms[i];
            float sx, sy;
            DiagramToScreen(room.x, room.y, sx, sy);
            float r = RoomRadius(room) + 4.0f; // expand slightly
            float dx = localX - sx;
            float dy = localY - sy;
            if (dx * dx + dy * dy <= r * r) return i;
        }
        return -1;
    }

    int UltraCanvasAdjacencyDiagram::HitTestLink(float localX, float localY) const {
        for (int li = 0; li < static_cast<int>(links.size()); ++li) {
            const AdjacencyLink& link = links[li];
            int srcIdx = LookupRoom(link.sourceId);
            int tgtIdx = LookupRoom(link.targetId);
            if (srcIdx < 0 || tgtIdx < 0) continue;

            float sx, sy, tx, ty;
            DiagramToScreen(rooms[srcIdx].x, rooms[srcIdx].y, sx, sy);
            DiagramToScreen(rooms[tgtIdx].x, rooms[tgtIdx].y, tx, ty);

            // Point-to-segment distance test
            float dx = tx - sx;
            float dy = ty - sy;
            float lenSq = dx*dx + dy*dy;
            if (lenSq < 0.1f) continue;

            float t = ((localX - sx) * dx + (localY - sy) * dy) / lenSq;
            t = std::max(0.0f, std::min(1.0f, t));

            float projX = sx + t * dx;
            float projY = sy + t * dy;
            float distSq = (localX - projX)*(localX - projX) + (localY - projY)*(localY - projY);

            float hitRadius = (link.type == AdjacencyLinkType::Direct)
                              ? style.directLinkWidth + 6.0f
                              : style.secondaryLinkWidth + 6.0f;

            if (distSq <= hitRadius * hitRadius) return li;
        }
        return -1;
    }

// ─────────────────────────────────────────────
// EVENT HANDLING
// ─────────────────────────────────────────────

    bool UltraCanvasAdjacencyDiagram::OnEvent(const UCEvent& event) {
        float localX = static_cast<float>(event.pointer.x);
        float localY = static_cast<float>(event.pointer.y);

        // The matrix view has its own hit testing and no panning; handle it
        // first and fall through to the bubble view's handling only when the
        // bubble view is the one on screen.
        if (view == AdjacencyView::Matrix) {
            switch (event.type) {
                case UCEventType::MouseMove: {
                    int row = -1, col = -1;
                    const bool inGrid = HitTestMatrixCell(localX, localY, row, col);
                    if (!inGrid) { row = -1; col = -1; }

                    const bool changed = (row != hoveredMatrixRow || col != hoveredMatrixCol);
                    hoveredMatrixRow = row;
                    hoveredMatrixCol = col;

                    showingTooltip = false;
                    tooltipText.clear();

                    if (style.showTooltip && inGrid && IsMatrixCellVisible(row, col)) {
                        const AdjacencyRoom& rowRoom =
                                rooms[static_cast<size_t>(matrixLayout.order[static_cast<size_t>(row)])];
                        const AdjacencyRoom& colRoom =
                                rooms[static_cast<size_t>(matrixLayout.order[static_cast<size_t>(col)])];
                        const AdjacencyLink* link = MatrixLinkAt(row, col);

                        std::ostringstream ss;
                        ss << rowRoom.label << " — " << colRoom.label << "\n"
                           << (link ? PriorityLabel(link->priority) : "no requirement");
                        tooltipText    = ss.str();
                        tooltipX       = localX;
                        tooltipY       = localY;
                        showingTooltip = true;
                    }

                    if (changed) RequestRedraw();
                    return inGrid;
                }

                case UCEventType::MouseDown: {
                    if (event.button != UCMouseButton::Left) break;

                    int row = -1, col = -1;
                    if (!HitTestMatrixCell(localX, localY, row, col)) break;
                    if (!IsMatrixCellVisible(row, col)) break;

                    if (row == selectedMatrixRow && col == selectedMatrixCol) {
                        selectedMatrixRow = selectedMatrixCol = -1;
                    } else {
                        selectedMatrixRow = row;
                        selectedMatrixCol = col;
                    }

                    if (onMatrixCellClick) {
                        const AdjacencyRoom& rowRoom =
                                rooms[static_cast<size_t>(matrixLayout.order[static_cast<size_t>(row)])];
                        const AdjacencyRoom& colRoom =
                                rooms[static_cast<size_t>(matrixLayout.order[static_cast<size_t>(col)])];
                        onMatrixCellClick(rowRoom.id, colRoom.id, MatrixLinkAt(row, col));
                    }
                    RequestRedraw();
                    return true;
                }

                case UCEventType::MouseLeave:
                    if (hoveredMatrixRow >= 0 || hoveredMatrixCol >= 0 || showingTooltip) {
                        hoveredMatrixRow = hoveredMatrixCol = -1;
                        showingTooltip = false;
                        tooltipText.clear();
                        RequestRedraw();
                    }
                    return false;

                default:
                    break;
            }
            return false;
        }

        switch (event.type) {

            case UCEventType::MouseMove: {
                if (isPanning) {
                    panOffsetX = panStartOffX + (localX - panStartX);
                    panOffsetY = panStartOffY + (localY - panStartY);
                    RequestRedraw();
                    return true;
                }

                int roomIdx = HitTestRoom(localX, localY);
                int linkIdx = (roomIdx < 0) ? HitTestLink(localX, localY) : -1;

                bool changed = (roomIdx != hoveredRoomIdx || linkIdx != hoveredLinkIdx);
                hoveredRoomIdx = roomIdx;
                hoveredLinkIdx = linkIdx;

                showingTooltip = false;
                tooltipText.clear();

                if (style.showTooltip) {
                    if (roomIdx >= 0) {
                        const AdjacencyRoom& room = rooms[roomIdx];
                        std::ostringstream ss;
                        ss << room.label;
                        ss << "  " << std::fixed << std::setprecision(0) << room.areaSqM << " m²";
                        if (!room.floorId.empty()) ss << "  [" << room.floorId << "]";
                        tooltipText    = ss.str();
                        tooltipX       = localX;
                        tooltipY       = localY;
                        showingTooltip = true;

                        if (onRoomHover && changed)
                            onRoomHover(roomIdx, room);

                    } else if (linkIdx >= 0) {
                        const AdjacencyLink& link = links[linkIdx];
                        int si = LookupRoom(link.sourceId);
                        int ti = LookupRoom(link.targetId);
                        std::string typeStr =
                                (link.type == AdjacencyLinkType::Secondary) ? "secondary" :
                                (link.type == AdjacencyLinkType::ServiceOnly) ? "service" : "direct";
                        std::ostringstream ss;
                        ss << (si >= 0 ? rooms[si].label : link.sourceId)
                           << " \u2014 "
                           << (ti >= 0 ? rooms[ti].label : link.targetId)
                           << " (" << typeStr << ")";
                        tooltipText    = ss.str();
                        tooltipX       = localX;
                        tooltipY       = localY;
                        showingTooltip = true;
                    }
                }

                if (changed) RequestRedraw();
                return true;
            }

            case UCEventType::MouseDown: {
                if (event.button == UCMouseButton::Left) {
                    int roomIdx = HitTestRoom(localX, localY);
                    if (roomIdx < 0 && enablePan) {
                        // Begin pan only when not clicking a room
                        isPanning    = true;
                        panStartX    = localX;
                        panStartY    = localY;
                        panStartOffX = panOffsetX;
                        panStartOffY = panOffsetY;
                        return true;
                    }
                }
                return false;
            }

            case UCEventType::MouseUp: {
                if (isPanning) {
                    isPanning = false;
                    return true;
                }

                if (event.button != UCMouseButton::Left) return false;

                int roomIdx = HitTestRoom(localX, localY);
                if (roomIdx >= 0) {
                    selectedRoomIdx = roomIdx;
                    selectedLinkIdx = -1;
                    if (onRoomClick) onRoomClick(roomIdx, rooms[roomIdx]);
                    RequestRedraw();
                    return true;
                }

                int linkIdx = HitTestLink(localX, localY);
                if (linkIdx >= 0) {
                    selectedLinkIdx = linkIdx;
                    selectedRoomIdx = -1;
                    if (onLinkClick) onLinkClick(linkIdx, links[linkIdx]);
                    RequestRedraw();
                    return true;
                }

                if (selectedRoomIdx >= 0 || selectedLinkIdx >= 0) {
                    selectedRoomIdx = selectedLinkIdx = -1;
                    RequestRedraw();
                }
                return false;
            }

            case UCEventType::MouseLeave: {
                isPanning = false;
                hoveredRoomIdx = hoveredLinkIdx = -1;
                showingTooltip = false;
                tooltipText.clear();
                RequestRedraw();
                return true;
            }

            default:
                return false;
        }
    }

// ─────────────────────────────────────────────
// UTILITY
// ─────────────────────────────────────────────

    int UltraCanvasAdjacencyDiagram::LookupRoom(const std::string& id) const {
        for (int i = 0; i < static_cast<int>(rooms.size()); ++i) {
            if (rooms[i].id == id) return i;
        }
        return -1;
    }

    int UltraCanvasAdjacencyDiagram::LookupZone(const std::string& id) const {
        for (int i = 0; i < static_cast<int>(zones.size()); ++i) {
            if (zones[i].id == id) return i;
        }
        return -1;
    }

// ─────────────────────────────────────────────
// FACTORY
// ─────────────────────────────────────────────

    std::shared_ptr<UltraCanvasAdjacencyDiagram> CreateAdjacencyDiagram(
            const std::string& id,
            float x, float y, float width, float height)
    {
        return std::make_shared<UltraCanvasAdjacencyDiagram>(id, x, y, width, height);
    }

} // namespace UltraCanvas