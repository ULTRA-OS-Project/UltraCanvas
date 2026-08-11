// Plugins/Charts/UltraCanvasAdjacencyMatrix.cpp
// The matrix view of UltraCanvasAdjacencyDiagram: the same rooms and links the
// bubble view draws, plotted as a half matrix of spaces against themselves.
// Version: 1.0.0
// Last Modified: 2026-08-11
// Author: UltraCanvas Framework
//
// This is a second VIEW of one dataset, not a second dataset. Architects draw
// both: the matrix to decide the adjacencies, the bubble diagram to arrange
// them. Nothing here converts or copies - both views read the same `rooms` and
// `links` vectors, so neither can go stale against the other.
//
// The geometry is deliberately the cheap one. A room adjacency matrix is
// symmetric, so only the upper triangle carries information, and it is drawn
// as an ordinary axis-aligned grid whose populated region is a staircase
// rather than as a rotated 45-degree diamond. That needs no transform, no
// inverse transform for hit testing, and it scales past twenty spaces where
// the rotated form sprawls. See
// Docs/UltraCanvas/UltraCanvasAdjacencyMatrixViewProposal.md section 2.2.

#include "Plugins/Diagrams/UltraCanvasAdjacencyDiagram.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UltraCanvas {

// =============================================================================
// PRIORITY HELPERS
// =============================================================================

    Color UltraCanvasAdjacencyDiagram::PriorityColor(AdjacencyPriority priority) const {
        switch (priority) {
            case AdjacencyPriority::Should: return style.colorShould;
            case AdjacencyPriority::Maybe:  return style.colorMaybe;
            case AdjacencyPriority::Must:
            default:                        return style.colorMust;
        }
    }

    const char* UltraCanvasAdjacencyDiagram::PriorityLabel(AdjacencyPriority priority) {
        switch (priority) {
            case AdjacencyPriority::Should: return "Should be adjacent";
            case AdjacencyPriority::Maybe:  return "Maybe adjacent";
            case AdjacencyPriority::Must:
            default:                        return "Must be adjacent";
        }
    }

// =============================================================================
// VIEW CONFIGURATION
// =============================================================================

    void UltraCanvasAdjacencyDiagram::SetView(AdjacencyView v) {
        if (view == v) return;
        view = v;
        // Hover state belongs to whichever view produced it.
        hoveredRoomIdx = hoveredLinkIdx = -1;
        hoveredMatrixRow = hoveredMatrixCol = -1;
        showingTooltip = false;
        InvalidateMatrixLayout();
        RebuildLegend();
        RequestRedraw();
    }

    void UltraCanvasAdjacencyDiagram::SetMatrixStyle(AdjacencyMatrixStyle s) {
        if (matrixStyle == s) return;
        matrixStyle = s;
        InvalidateMatrixLayout();
        RequestRedraw();
    }

    void UltraCanvasAdjacencyDiagram::SetMatrixOrder(const std::vector<std::string>& roomIds) {
        matrixOrder = roomIds;
        InvalidateMatrixLayout();
        RequestRedraw();
    }

    void UltraCanvasAdjacencyDiagram::SetShowMatrixDiagonal(bool on) {
        if (showMatrixDiagonal == on) return;
        showMatrixDiagonal = on;
        RequestRedraw();
    }

    void UltraCanvasAdjacencyDiagram::SetAttributeColumns(const std::vector<std::string>& headers) {
        attributeColumns = headers;
        InvalidateMatrixLayout();
        RequestRedraw();
    }

    void UltraCanvasAdjacencyDiagram::SetShowLegend(bool on) {
        if (showLegend == on) return;
        showLegend = on;
        RebuildLegend();
        InvalidateMatrixLayout();
        RequestRedraw();
    }

    void UltraCanvasAdjacencyDiagram::SetLegendPosition(ChartLegendPosition position) {
        if (!legend) legend = std::make_unique<ChartLegend>();
        legend->SetPosition(position);
        InvalidateMatrixLayout();
        RequestRedraw();
    }

// =============================================================================
// LEGEND
// =============================================================================

    void UltraCanvasAdjacencyDiagram::RebuildLegend() {
        if (!legend) {
            legend = std::make_unique<ChartLegend>();
            legend->SetPosition(ChartLegendPosition::BottomCenter);
            legend->SetOrientation(LegendOrientation::Horizontal);
        }

        std::vector<ChartLegendEntry> entries;

        if (view == AdjacencyView::Matrix) {
            // The matrix plots priority, so that is what the key explains.
            const AdjacencyPriority levels[] = {AdjacencyPriority::Must,
                                                AdjacencyPriority::Should,
                                                AdjacencyPriority::Maybe};
            for (AdjacencyPriority priority : levels) {
                ChartLegendEntry entry;
                entry.label = PriorityLabel(priority);
                entry.color = PriorityColor(priority);
                entry.swatch = (priority == AdjacencyPriority::Maybe)
                        ? LegendSwatch::Ring     // keeps the scale readable in greyscale
                        : LegendSwatch::Circle;
                entries.push_back(entry);
            }
            legend->SetTitle("Adjacency");
        } else {
            // The bubble view colours rooms by function type and has never said
            // so anywhere on screen. Only list the types actually in use.
            struct FunctionEntry { RoomFunctionType type; const char* label; Color color; };
            const FunctionEntry all[] = {
                {RoomFunctionType::Public,      "Public",      style.colorPublic},
                {RoomFunctionType::Private,     "Private",     style.colorPrivate},
                {RoomFunctionType::Service,     "Service",     style.colorService},
                {RoomFunctionType::Support,     "Support",     style.colorSupport},
                {RoomFunctionType::Circulation, "Circulation", style.colorCirculation},
            };
            for (const FunctionEntry& candidate : all) {
                const bool used = std::any_of(rooms.begin(), rooms.end(),
                        [&candidate](const AdjacencyRoom& room) {
                            return room.functionType == candidate.type;
                        });
                if (!used) continue;

                ChartLegendEntry entry;
                entry.label = candidate.label;
                entry.color = candidate.color;
                entry.swatch = LegendSwatch::Circle;
                entries.push_back(entry);
            }
            legend->SetTitle("Room function");
        }

        legend->SetEntries(entries);
        legend->SetVisible(showLegend && !entries.empty());
    }

// =============================================================================
// ORDER
// =============================================================================

    std::vector<int> UltraCanvasAdjacencyDiagram::BuildMatrixOrder() const {
        std::vector<int> order;
        order.reserve(rooms.size());

        std::vector<bool> placed(rooms.size(), false);

        for (const std::string& id : matrixOrder) {
            const int idx = LookupRoom(id);
            if (idx < 0 || placed[static_cast<size_t>(idx)]) continue;
            placed[static_cast<size_t>(idx)] = true;
            order.push_back(idx);
        }

        // Anything the caller did not name keeps its insertion position.
        for (size_t i = 0; i < rooms.size(); ++i) {
            if (!placed[i]) order.push_back(static_cast<int>(i));
        }
        return order;
    }

// =============================================================================
// LOOKUP
// =============================================================================

    const AdjacencyLink* UltraCanvasAdjacencyDiagram::MatrixLinkAt(int row, int col) const {
        if (row < 0 || col < 0) return nullptr;
        if (row >= static_cast<int>(matrixLayout.order.size())) return nullptr;
        if (col >= static_cast<int>(matrixLayout.order.size())) return nullptr;

        const std::string& rowId = rooms[static_cast<size_t>(matrixLayout.order[static_cast<size_t>(row)])].id;
        const std::string& colId = rooms[static_cast<size_t>(matrixLayout.order[static_cast<size_t>(col)])].id;

        // The relation is symmetric, so a link counts in either direction. When
        // several links join the same pair, the strongest priority wins - the
        // matrix answers "how badly", and the strongest requirement is the one
        // that constrains the plan.
        const AdjacencyLink* best = nullptr;
        for (const AdjacencyLink& link : links) {
            const bool matches = (link.sourceId == rowId && link.targetId == colId) ||
                                 (link.sourceId == colId && link.targetId == rowId);
            if (!matches) continue;
            if (!best || link.priority < best->priority) best = &link;
        }
        return best;
    }

    bool UltraCanvasAdjacencyDiagram::IsMatrixCellVisible(int row, int col) const {
        if (row < 0 || col < 0) return false;
        if (row >= matrixLayout.count || col >= matrixLayout.count) return false;
        return showMatrixDiagonal ? (col >= row) : (col > row);
    }

// =============================================================================
// LAYOUT
// =============================================================================

    void UltraCanvasAdjacencyDiagram::UpdateMatrixLayout(IRenderContext* ctx) {
        if (matrixLayout.valid || !ctx) return;

        matrixLayout.order = BuildMatrixOrder();
        matrixLayout.count = static_cast<int>(matrixLayout.order.size());

        Rect2Df local = GetLocalBounds();
        const double margin = 12.0;
        Rect2Dd area(local.x + margin, local.y + margin,
                     std::max(0.0, local.width - margin * 2.0),
                     std::max(0.0, local.height - margin * 2.0));

        // Render() draws the legend against the pre-bite rectangle; handing it
        // the remainder would put it back over the grid it just made room for.
        matrixLayout.legendArea = area;
        if (legend && legend->IsVisible()) {
            legend->Measure(ctx, area);
            area = legend->RemainingArea(area);
        }
        matrixLayout.content = area;

        if (matrixLayout.count == 0 || area.width <= 0.0 || area.height <= 0.0) {
            matrixLayout.valid = true;
            return;
        }

        // Row labels
        ctx->SetFontSize(style.matrixRowLabelFontSize);
        double rowLabelWidth = 0.0;
        for (int idx : matrixLayout.order) {
            rowLabelWidth = std::max(rowLabelWidth, static_cast<double>(
                    ctx->GetTextLineWidth(rooms[static_cast<size_t>(idx)].label)));
        }
        rowLabelWidth = std::min(rowLabelWidth, static_cast<double>(style.matrixMaxRowLabelWidth)) + 14.0;

        const double attributeWidth = attributeColumns.empty()
                ? 0.0
                : attributeColumns.size() * style.matrixAttributeColWidth;

        // Column headers are always rotated here: a room name never fits a cell
        // barely wider than a dot, and every printed example rotates them.
        ctx->SetFontSize(style.matrixColLabelFontSize);
        double longestLabel = 0.0;
        for (int idx : matrixLayout.order) {
            longestLabel = std::max(longestLabel, static_cast<double>(
                    ctx->GetTextLineWidth(rooms[static_cast<size_t>(idx)].label)));
        }

        // The grid has first claim on the height: the header may take only what
        // is left once every row has its minimum.
        const double gridFloor = matrixLayout.count * style.matrixMinCellSize;
        const double headerBand = std::clamp(area.height - gridFloor,
                                             20.0,
                                             std::min<double>(style.matrixMaxHeaderHeight,
                                                              longestLabel + 12.0));

        const double gridLeft = area.x + attributeWidth + rowLabelWidth;
        const double availW = area.x + area.width - gridLeft;
        const double availH = area.y + area.height - (area.y + headerBand);

        // Cells are square: the matrix crosses one set with itself, so a
        // non-square cell would make the same relation read differently by axis.
        double cell = std::min(availW, availH) / std::max(1, matrixLayout.count);
        cell = std::clamp(cell, static_cast<double>(style.matrixMinCellSize),
                          static_cast<double>(style.matrixMaxCellSize));
        matrixLayout.cellSize = cell;

        const double gridSize = cell * matrixLayout.count;
        matrixLayout.grid = Rect2Dd(gridLeft, area.y + headerBand, gridSize, gridSize);
        matrixLayout.rowLabels = Rect2Dd(gridLeft - rowLabelWidth, matrixLayout.grid.y,
                                         rowLabelWidth, gridSize);
        matrixLayout.colLabels = Rect2Dd(gridLeft, area.y, gridSize, headerBand);
        matrixLayout.attributes = attributeColumns.empty()
                ? Rect2Dd(0, 0, 0, 0)
                : Rect2Dd(matrixLayout.rowLabels.x - attributeWidth, matrixLayout.grid.y,
                          attributeWidth, gridSize);
        matrixLayout.rotateHeaders = true;
        matrixLayout.valid = true;
    }

// =============================================================================
// RENDERING
// =============================================================================

    void UltraCanvasAdjacencyDiagram::RenderMatrix(IRenderContext* ctx) {
        UpdateMatrixLayout(ctx);
        if (matrixLayout.count == 0) return;

        ctx->PushState();
        ctx->ClipRect(matrixLayout.content);
        DrawMatrixGrid(ctx);
        DrawMatrixHighlight(ctx);
        DrawMatrixMarks(ctx);
        DrawMatrixLabels(ctx);
        DrawMatrixAttributes(ctx);
        ctx->PopState();

        if (legend && legend->IsVisible()) {
            legend->Render(ctx, matrixLayout.legendArea);
        }
    }

    void UltraCanvasAdjacencyDiagram::DrawMatrixGrid(IRenderContext* ctx) const {
        const MatrixLayout& ml = matrixLayout;

        ctx->SetFillPaint(style.matrixCellBackground);
        ctx->FillRectangle(ml.grid);

        // Shade the unused half so the reader sees at once that the relation is
        // symmetric and only the upper triangle carries information.
        ctx->SetFillPaint(style.matrixBlockedCell);
        for (int row = 0; row < ml.count; ++row) {
            const int firstVisible = showMatrixDiagonal ? row : row + 1;
            if (firstVisible <= 0) continue;
            ctx->FillRectangle(Rect2Dd(ml.grid.x, ml.grid.y + row * ml.cellSize,
                                       firstVisible * ml.cellSize, ml.cellSize));
        }

        ctx->SetStrokePaint(style.matrixGridLineColor);
        ctx->SetStrokeWidth(style.matrixGridLineWidth);
        for (int i = 0; i <= ml.count; ++i) {
            const double x = ml.grid.x + i * ml.cellSize;
            const double y = ml.grid.y + i * ml.cellSize;
            ctx->DrawLine(Point2Dd(x, ml.grid.y), Point2Dd(x, ml.grid.y + ml.grid.height));
            ctx->DrawLine(Point2Dd(ml.grid.x, y), Point2Dd(ml.grid.x + ml.grid.width, y));
        }
    }

    void UltraCanvasAdjacencyDiagram::DrawMatrixHighlight(IRenderContext* ctx) const {
        const MatrixLayout& ml = matrixLayout;

        if (hoveredMatrixRow >= 0 || hoveredMatrixCol >= 0) {
            ctx->SetFillPaint(style.matrixHoverBand);
            if (hoveredMatrixRow >= 0 && hoveredMatrixRow < ml.count) {
                ctx->FillRectangle(Rect2Dd(ml.grid.x, ml.grid.y + hoveredMatrixRow * ml.cellSize,
                                           ml.grid.width, ml.cellSize));
            }
            if (hoveredMatrixCol >= 0 && hoveredMatrixCol < ml.count) {
                ctx->FillRectangle(Rect2Dd(ml.grid.x + hoveredMatrixCol * ml.cellSize, ml.grid.y,
                                           ml.cellSize, ml.grid.height));
            }
        }

        if (selectedMatrixRow >= 0 && selectedMatrixCol >= 0 &&
            IsMatrixCellVisible(selectedMatrixRow, selectedMatrixCol)) {
            ctx->SetFillPaint(style.matrixSelectedCell);
            ctx->FillRectangle(Rect2Dd(ml.grid.x + selectedMatrixCol * ml.cellSize,
                                       ml.grid.y + selectedMatrixRow * ml.cellSize,
                                       ml.cellSize, ml.cellSize));
        }
    }

    void UltraCanvasAdjacencyDiagram::DrawMatrixMarks(IRenderContext* ctx) const {
        const MatrixLayout& ml = matrixLayout;
        const double radius = ml.cellSize * style.matrixMarkSize * 0.5;
        if (radius <= 0.0) return;

        for (int row = 0; row < ml.count; ++row) {
            for (int col = 0; col < ml.count; ++col) {
                if (!IsMatrixCellVisible(row, col)) continue;

                const AdjacencyLink* link = MatrixLinkAt(row, col);
                if (!link) continue;

                const Point2Dd centre(ml.grid.x + (col + 0.5) * ml.cellSize,
                                      ml.grid.y + (row + 0.5) * ml.cellSize);
                const Color color = PriorityColor(link->priority);

                if (link->priority == AdjacencyPriority::Maybe) {
                    ctx->SetStrokePaint(color);
                    ctx->SetStrokeWidth(2.0);
                    ctx->DrawCircle(centre, radius);
                } else {
                    ctx->SetFillPaint(color);
                    ctx->FillCircle(centre, radius);
                }
            }
        }
    }

    void UltraCanvasAdjacencyDiagram::DrawMatrixLabels(IRenderContext* ctx) const {
        const MatrixLayout& ml = matrixLayout;

        // Row labels, right-aligned against the grid.
        ctx->SetFontSize(style.matrixRowLabelFontSize);
        ctx->SetTextPaint(style.matrixLabelColor);
        for (int row = 0; row < ml.count; ++row) {
            const AdjacencyRoom& room = rooms[static_cast<size_t>(ml.order[static_cast<size_t>(row)])];
            const std::string label = EllipsizeToWidth(ctx, room.label, ml.rowLabels.width - 10.0);
            Size2Di size = ctx->GetTextLineDimensions(label);
            ctx->DrawText(label,
                          Point2Dd(ml.rowLabels.x + ml.rowLabels.width - 8.0 - size.width,
                                   ml.grid.y + row * ml.cellSize + (ml.cellSize - size.height) / 2.0));
        }

        // Column labels, rotated up from the grid's top edge.
        ctx->SetFontSize(style.matrixColLabelFontSize);
        const double maxHeaderText = ml.colLabels.height - 8.0;
        for (int col = 0; col < ml.count; ++col) {
            const AdjacencyRoom& room = rooms[static_cast<size_t>(ml.order[static_cast<size_t>(col)])];
            const std::string label = EllipsizeToWidth(ctx, room.label, maxHeaderText);
            Size2Di size = ctx->GetTextLineDimensions(label);

            ctx->PushState();
            ctx->Translate(ml.grid.x + (col + 0.5) * ml.cellSize, ml.grid.y - 5.0);
            ctx->Rotate(-M_PI / 2.0);
            ctx->SetTextPaint(style.matrixLabelColor);
            ctx->DrawText(label, Point2Dd(0.0, -size.height / 2.0));
            ctx->PopState();
        }
    }

    void UltraCanvasAdjacencyDiagram::DrawMatrixAttributes(IRenderContext* ctx) const {
        if (attributeColumns.empty()) return;
        const MatrixLayout& ml = matrixLayout;

        ctx->SetStrokePaint(style.matrixGridLineColor);
        ctx->SetStrokeWidth(style.matrixGridLineWidth);
        ctx->DrawRectangle(ml.attributes);

        const double colWidth = style.matrixAttributeColWidth;

        // Headers, rotated to match the matrix's own column labels.
        ctx->SetFontSize(style.matrixColLabelFontSize);
        for (size_t c = 0; c < attributeColumns.size(); ++c) {
            const std::string header = EllipsizeToWidth(ctx, attributeColumns[c],
                                                        ml.colLabels.height - 8.0);
            Size2Di size = ctx->GetTextLineDimensions(header);
            ctx->PushState();
            ctx->Translate(ml.attributes.x + (c + 0.5) * colWidth, ml.grid.y - 5.0);
            ctx->Rotate(-M_PI / 2.0);
            ctx->SetTextPaint(style.matrixLabelColor);
            ctx->DrawText(header, Point2Dd(0.0, -size.height / 2.0));
            ctx->PopState();
        }

        // Values, centred per cell.
        ctx->SetFontSize(style.matrixRowLabelFontSize);
        ctx->SetTextPaint(style.matrixLabelColor);
        for (int row = 0; row < ml.count; ++row) {
            const AdjacencyRoom& room = rooms[static_cast<size_t>(ml.order[static_cast<size_t>(row)])];
            for (size_t c = 0; c < attributeColumns.size(); ++c) {
                if (c >= room.attributes.size()) continue;
                const std::string value = EllipsizeToWidth(ctx, room.attributes[c], colWidth - 6.0);
                if (value.empty()) continue;
                Size2Di size = ctx->GetTextLineDimensions(value);
                ctx->DrawText(value,
                              Point2Dd(ml.attributes.x + (c + 0.5) * colWidth - size.width / 2.0,
                                       ml.grid.y + row * ml.cellSize + (ml.cellSize - size.height) / 2.0));
            }
        }

        for (size_t c = 1; c < attributeColumns.size(); ++c) {
            const double x = ml.attributes.x + c * colWidth;
            ctx->SetStrokePaint(style.matrixGridLineColor);
            ctx->DrawLine(Point2Dd(x, ml.attributes.y),
                          Point2Dd(x, ml.attributes.y + ml.attributes.height));
        }
    }

// =============================================================================
// HIT TESTING
// =============================================================================

    bool UltraCanvasAdjacencyDiagram::HitTestMatrixCell(float localX, float localY,
                                                        int& outRow, int& outCol) const {
        const MatrixLayout& ml = matrixLayout;
        if (!ml.valid || ml.count == 0 || ml.cellSize <= 0.0) return false;

        const double x = static_cast<double>(localX);
        const double y = static_cast<double>(localY);
        if (x < ml.grid.x || x >= ml.grid.x + ml.grid.width) return false;
        if (y < ml.grid.y || y >= ml.grid.y + ml.grid.height) return false;

        outRow = std::clamp(static_cast<int>((y - ml.grid.y) / ml.cellSize), 0, ml.count - 1);
        outCol = std::clamp(static_cast<int>((x - ml.grid.x) / ml.cellSize), 0, ml.count - 1);
        return true;
    }

// =============================================================================
// SHARED HELPERS
// =============================================================================

    std::string UltraCanvasAdjacencyDiagram::EllipsizeToWidth(IRenderContext* ctx,
                                                              const std::string& text,
                                                              double maxWidth) const {
        if (maxWidth <= 0.0 || text.empty()) return text;
        if (ctx->GetTextLineWidth(text) <= maxWidth) return text;

        std::string trimmed = text;
        while (!trimmed.empty()) {
            // Trim whole UTF-8 sequences, never half of one.
            do {
                trimmed.pop_back();
            } while (!trimmed.empty() &&
                     (static_cast<unsigned char>(trimmed.back()) & 0xC0) == 0x80);

            if (ctx->GetTextLineWidth(trimmed + "...") <= maxWidth) break;
        }
        return trimmed.empty() ? std::string("...") : trimmed + "...";
    }

} // namespace UltraCanvas
