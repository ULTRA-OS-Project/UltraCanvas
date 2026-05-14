// UltraCanvasAdjacencyDiagram.cpp
// Architectural space-planning adjacency diagram
// Rooms as area-proportional circles, edges as solid/dashed adjacency links,
// functional zones as dashed bounding regions.
// Version: 1.0.1
// Last Modified: 2026-05-14
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasAdjacencyDiagram.h"
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
            long x, long y, long w, long h)
            : UltraCanvasUIElement(id, x, y, w, h)
    {}

// ─────────────────────────────────────────────
// ROOM API
// ─────────────────────────────────────────────

    int UltraCanvasAdjacencyDiagram::AddRoom(const AdjacencyRoom& room) {
        if (LookupRoom(room.id) >= 0) return LookupRoom(room.id);
        int idx = static_cast<int>(rooms.size());
        rooms.push_back(room);
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

// ─────────────────────────────────────────────
// ZONE BOUNDS
// ─────────────────────────────────────────────

    Rect2Df UltraCanvasAdjacencyDiagram::ComputeZoneBounds(const AdjacencyZone& zone) const {
        if (zone.roomIds.empty()) return Rect2Df(0, 0, 0, 0);

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
        return Rect2Df(
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
                ctx->DrawLine(Point2Df(sx, sy), Point2Df(ex, ey));
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
            ctx->DrawLine(Point2Df(lx1, ly1), Point2Df(lx2, ly2));
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
            ctx->SetTextPaint(style.zoneLabelColor);
            auto dims = ctx->GetTextLineDimensions(link.label);
            int tw = dims.width, th = dims.height;
            ctx->DrawText(link.label, Point2Df(midX - tw * 0.5f, midY - th - 2.0f));
        }
    }

    void UltraCanvasAdjacencyDiagram::DrawZones(IRenderContext* ctx) const {
        for (const auto& zone : zones) {
            Rect2Df bnds = ComputeZoneBounds(zone);
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
                ctx->SetTextPaint(style.zoneLabelColor);
                ctx->SetFontSize(style.zoneLabelFontSize);
                ctx->SetFontFace("Sans", FontWeight::Normal, FontSlant::Normal);
                ctx->DrawText(zone.label,
                              Point2Df(x + 8.0f, y + style.zoneLabelFontSize + 4.0f));
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
            ctx->FillCircle(Point2Df(sx, sy), r);

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
            ctx->DrawCircle(Point2Df(sx, sy), r);
        }
    }

    void UltraCanvasAdjacencyDiagram::DrawLabels(IRenderContext* ctx) const {
        for (const auto& room : rooms) {
            float r = RoomRadius(room);
            Color fill = RoomColor(room);
            float sx, sy;
            DiagramToScreen(room.x, room.y, sx, sy);

            // Choose text color for contrast
            Color textCol = IsLightColor(fill) ? style.labelColorDark : style.labelColor;

            // Room name
            ctx->SetFontSize(style.labelFontSize);
            ctx->SetFontFace("Sans", FontWeight::Normal, FontSlant::Normal);
            ctx->SetTextPaint(textCol);

            auto dims = ctx->GetTextLineDimensions(room.label);
            int tw = dims.width, th = dims.height;

            // If label wider than circle, clip by drawing only if r > 12
            float labelY = sy - th * 0.5f;
            if (!room.note.empty()) {
                // Two-line: shift main label up
                dims = ctx->GetTextLineDimensions(room.note);
                int nw = dims.width, nh = dims.height;

                labelY = sy - th - 1.0f;
                float noteX = sx - nw * 0.5f;
                float noteY = sy + 2.0f;
                ctx->SetFontSize(style.noteFontSize);
                ctx->DrawText(room.note, Point2Df(noteX, noteY));
                ctx->SetFontSize(style.labelFontSize);
            }

            ctx->DrawText(room.label, Point2Df(sx - tw * 0.5f, labelY));
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
        ctx->FillRectangle(Rect2Df(bx, by, boxW, boxH));

        ctx->SetTextPaint(style.tooltipText);
        ctx->DrawText(tooltipText, Point2Df(bx + pad, by + pad));
    }

// ─────────────────────────────────────────────
// RENDER
// ─────────────────────────────────────────────

    void UltraCanvasAdjacencyDiagram::Render(IRenderContext* ctx, const Rect2Di& dirtyrects) {
        if (rooms.empty() && zones.empty()) return;

        DrawZones(ctx);     // zones first (background)
        DrawLinks(ctx);     // links above zones
        DrawRooms(ctx);     // rooms above links
        DrawLabels(ctx);    // labels on top of rooms
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
            long x, long y, long width, long height)
    {
        return std::make_shared<UltraCanvasAdjacencyDiagram>(id, x, y, width, height);
    }

} // namespace UltraCanvas