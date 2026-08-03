// Plugins/Diagrams/UltraCanvasPacketDiagram.cpp
// Packet structure diagram element implementation.
// Version: 1.0.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasPacketDiagram.h"
#include "Plugins/Diagrams/UltraCanvasPacketParser.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace UltraCanvas {

// =============================================================================
// CONSTRUCTION
// =============================================================================

    UltraCanvasPacketDiagram::UltraCanvasPacketDiagram(
            const std::string& id, float x, float y, float width, float height)
            : UltraCanvasUIElement(id, x, y, width, height) {
        options.mode = PacketLayoutMode::WordGrid;
        options.bitsPerRow = 32;
        options.rowHeight = 34.0;
        options.minCellWidth = 18.0;
        legend.SetPosition(ChartLegendPosition::BottomCenter);
        ApplyTheme();
    }

    std::shared_ptr<UltraCanvasPacketDiagram> CreatePacketDiagram(
            const std::string& id, float x, float y, float width, float height) {
        return std::make_shared<UltraCanvasPacketDiagram>(id, x, y, width, height);
    }

// =============================================================================
// MODEL
// =============================================================================

    void UltraCanvasPacketDiagram::SetModel(const PacketModel& m) {
        model = m;
        if (model.bitsPerRow > 0) options.bitsPerRow = model.bitsPerRow;
        if (!model.title.empty() && title.empty()) title = model.title;
        if (!model.caption.empty() && caption.empty()) caption = model.caption;
        selectedCell = SIZE_MAX;
        hoveredCell = SIZE_MAX;
        Invalidate();
    }

    PacketLayer& UltraCanvasPacketDiagram::AddLayer(const std::string& id,
                                                    const std::string& name) {
        Invalidate();
        return model.AddLayer(id, name);
    }

    void UltraCanvasPacketDiagram::ClearModel() {
        model = PacketModel();
        selectedCell = SIZE_MAX;
        hoveredCell = SIZE_MAX;
        Invalidate();
    }

    bool UltraCanvasPacketDiagram::LoadFromText(const std::string& text) {
        PacketParseResult result = ParsePacketText(text);
        parseErrors.clear();
        for (const auto& e : result.errors) {
            parseErrors.push_back("line " + std::to_string(e.line) + ": " + e.message);
        }
        if (result.Ok()) SetModel(result.model);
        return result.Ok();
    }

// =============================================================================
// CONFIGURATION
// =============================================================================

    void UltraCanvasPacketDiagram::SetLayoutMode(PacketLayoutMode mode) {
        options.mode = mode;
        Invalidate();
    }

    void UltraCanvasPacketDiagram::SetBitsPerRow(uint32_t bits) {
        options.bitsPerRow = bits > 0 ? bits : 32;
        model.bitsPerRow = options.bitsPerRow;
        Invalidate();
    }

    void UltraCanvasPacketDiagram::SetRowHeight(double h) {
        options.rowHeight = std::max(8.0, h);
        Invalidate();
    }

    void UltraCanvasPacketDiagram::SetMinCellWidth(double w) {
        options.minCellWidth = std::max(1.0, w);
        Invalidate();
    }

    void UltraCanvasPacketDiagram::SetLengthScale(PacketLengthScale scale,
                                                  double minCellPx) {
        options.lengthScale = scale;
        if (minCellPx > 0.0) options.minCellWidth = minCellPx;
        Invalidate();
    }

    void UltraCanvasPacketDiagram::SetSizeUnit(PacketSizeUnit unit) {
        sizeUnit = unit;
        Invalidate();
    }

    void UltraCanvasPacketDiagram::SetLanes(const std::vector<std::string>& names) {
        laneNames = names;
        options.laneCount = static_cast<uint32_t>(std::max<size_t>(1, names.size()));
        Invalidate();
    }

    void UltraCanvasPacketDiagram::SetRulerMode(PacketRulerMode mode) {
        rulerMode = mode;
        Invalidate();
    }

    void UltraCanvasPacketDiagram::SetRulerLabelStyle(PacketRulerLabelStyle style) {
        rulerLabelStyle = style;
        Invalidate();
    }

    void UltraCanvasPacketDiagram::SetRulerTitle(const std::string& t) {
        rulerTitle = t;
        Invalidate();
    }

    void UltraCanvasPacketDiagram::SetRulerTicks(const std::vector<uint32_t>& bits) {
        rulerTicks = bits;
        Invalidate();
    }

    void UltraCanvasPacketDiagram::SetOffsetGutter(PacketRowIndexMode mode,
                                                   const std::string& t) {
        offsetGutterMode = mode;
        offsetGutterTitle = t;
        Invalidate();
    }

    void UltraCanvasPacketDiagram::SetRowIndexGutter(PacketRowIndexMode mode,
                                                     const std::string& t) {
        wordGutterMode = mode;
        wordGutterTitle = t;
        Invalidate();
    }

    void UltraCanvasPacketDiagram::AddDimensionRail(PacketEdge edge, uint64_t startBit,
                                                    uint64_t endBit,
                                                    const std::string& label) {
        rails.push_back({edge, startBit, endBit, label});
        Invalidate();
    }

    void UltraCanvasPacketDiagram::AddBrace(PacketEdge edge, const std::string& layerId,
                                            const std::string& label) {
        braces.push_back({edge, layerId, label});
        Invalidate();
    }

    void UltraCanvasPacketDiagram::SetLayerBrackets(PacketEdge edge) {
        for (const auto& layer : model.layers) {
            const std::string label =
                    layer.bracketLabel.empty() ? layer.name : layer.bracketLabel;
            if (!label.empty()) braces.push_back({edge, layer.id, label});
        }
        Invalidate();
    }

    void UltraCanvasPacketDiagram::ClearAnnotations() {
        rails.clear();
        braces.clear();
        Invalidate();
    }

    void UltraCanvasPacketDiagram::SetTitle(const std::string& t) {
        title = t; Invalidate();
    }
    void UltraCanvasPacketDiagram::SetCaption(const std::string& t) {
        caption = t; Invalidate();
    }
    void UltraCanvasPacketDiagram::SetTrailerText(const std::string& t) {
        model.trailerText = t; Invalidate();
    }

    void UltraCanvasPacketDiagram::ApplyTheme() {
        switch (theme) {
            case PacketTheme::Dark:
                bgColor = Color(28, 30, 36, 255);
                cellFill = Color(52, 58, 70, 255);
                cellBorder = Color(120, 130, 145, 255);
                textColor = Color(228, 232, 238, 255);
                rulerColor = Color(180, 188, 200, 255);
                gutterColor = Color(150, 158, 172, 255);
                reservedColor = Color(70, 74, 84, 255);
                bandColorA = Color(44, 48, 58, 255);
                bandColorB = Color(52, 57, 68, 255);
                legend.SetStyle(ChartLegendStyle::Dark());
                break;
            case PacketTheme::Monochrome:
                bgColor = Colors::White;
                cellFill = Color(232, 232, 232, 255);
                cellBorder = Colors::Black;
                textColor = Colors::Black;
                rulerColor = Colors::Black;
                gutterColor = Colors::Black;
                reservedColor = Color(200, 200, 200, 255);
                bandColorA = Color(240, 240, 240, 255);
                bandColorB = Color(220, 220, 220, 255);
                highlightColor = Color(180, 180, 180, 255);
                legend.SetStyle(ChartLegendStyle::Monochrome());
                break;
            case PacketTheme::Light:
            default:
                bgColor = Colors::White;
                cellFill = Color(232, 240, 248, 255);
                cellBorder = Color(70, 80, 95, 255);
                textColor = Color(25, 30, 40, 255);
                rulerColor = Color(60, 65, 75, 255);
                gutterColor = Color(90, 95, 105, 255);
                reservedColor = Color(210, 210, 210, 255);
                legend.SetStyle(ChartLegendStyle::Light());
                break;
        }
    }

    void UltraCanvasPacketDiagram::SetTheme(PacketTheme t) {
        theme = t;
        ApplyTheme();
        Invalidate();
    }

    void UltraCanvasPacketDiagram::SetColorMode(PacketColorMode mode) {
        colorMode = mode; Invalidate();
    }
    void UltraCanvasPacketDiagram::SetPayloadStyle(PacketPayloadStyle style) {
        payloadStyle = style; Invalidate();
    }

    void UltraCanvasPacketDiagram::SetRowBanding(bool enabled, const Color& a,
                                                 const Color& b) {
        rowBanding = enabled;
        bandColorA = a;
        bandColorB = b;
        Invalidate();
    }

    void UltraCanvasPacketDiagram::SetHighlightFields(
            const std::vector<std::string>& names, const Color& color) {
        highlightNames = names;
        highlightColor = color;
        Invalidate();
    }

    void UltraCanvasPacketDiagram::ClearHighlights() {
        highlightNames.clear();
        Invalidate();
    }

    void UltraCanvasPacketDiagram::SetShowLegend(bool show) {
        showLegend = show;
        legend.SetVisible(show);
        Invalidate();
    }

    void UltraCanvasPacketDiagram::SetShowFieldValues(bool show) {
        showFieldValues = show; Invalidate();
    }

    void UltraCanvasPacketDiagram::SetShowValidationBadges(bool show) {
        showValidationBadges = show; Invalidate();
    }

    void UltraCanvasPacketDiagram::SetFieldValueText(const std::string& nameOrId,
                                                     const std::string& text) {
        for (auto& layer : model.layers) {
            for (auto& f : layer.fields) {
                if (f.id == nameOrId || f.name == nameOrId) {
                    f.valueText = text;
                    Invalidate();
                    return;
                }
            }
        }
    }

    PacketValidationResult UltraCanvasPacketDiagram::Validate(
            uint64_t expectedTotalBits) const {
        return ValidatePacketModel(model, expectedTotalBits);
    }

// =============================================================================
// LOOKUPS
// =============================================================================

    const PacketField* UltraCanvasPacketDiagram::FieldOfCell(
            const PacketCell& cell) const {
        if (cell.layerIndex >= model.layers.size()) return nullptr;
        const PacketLayer& layer = model.layers[cell.layerIndex];
        if (cell.depth == 1) {
            // Sub-cells index into the parent's children.
            if (cell.fieldIndex >= layer.fields.size()) return nullptr;
            const PacketField& parent = layer.fields[cell.fieldIndex];
            if (cell.cellIndexInField >= parent.children.size()) return nullptr;
            return &parent.children[cell.cellIndexInField];
        }
        if (cell.fieldIndex >= layer.fields.size()) return nullptr;
        return &layer.fields[cell.fieldIndex];
    }

    PacketFieldRef UltraCanvasPacketDiagram::MakeFieldRef(const PacketCell& cell) const {
        PacketFieldRef ref;
        ref.name = cell.name;
        ref.layerIndex = cell.layerIndex;
        ref.fieldIndex = cell.fieldIndex;
        if (cell.layerIndex < model.layers.size()) {
            ref.layerId = model.layers[cell.layerIndex].id;
        }
        const PacketField* f = FieldOfCell(cell);
        if (f) {
            ref.fieldId = f->id.empty() ? f->name : f->id;
            ref.bitLength = f->bitLength;
            // Report the offset of the whole field, not of this fragment.
            uint64_t base = 0;
            for (size_t i = 0; i < cell.layerIndex && i < model.layers.size(); ++i) {
                base += model.layers[i].TotalBits();
            }
            ref.bitOffset = base + f->bitOffset;
        } else {
            ref.bitOffset = cell.bitOffset;
            ref.bitLength = cell.bitLength;
        }
        return ref;
    }

    bool UltraCanvasPacketDiagram::IsHighlighted(const PacketCell& cell) const {
        for (const auto& n : highlightNames) {
            if (cell.name == n) return true;
        }
        return false;
    }

    Color UltraCanvasPacketDiagram::CellColor(const PacketCell& cell) const {
        if (IsHighlighted(cell)) return highlightColor;
        if (cell.hasColorOverride) return cell.color;

        switch (cell.kind) {
            case PacketFieldKind::Reserved:
            case PacketFieldKind::Padding:
                return reservedColor;
            default:
                break;
        }

        switch (colorMode) {
            case PacketColorMode::ByLayer: {
                if (cell.layerIndex < model.layers.size()) {
                    const PacketLayer& l = model.layers[cell.layerIndex];
                    if (l.hasColor) return l.color;
                }
                return cellFill;
            }
            case PacketColorMode::BySourceByte: {
                // Quantised ramp over the byte index.
                const uint64_t byteIndex = cell.bitOffset / 8;
                static const Color ramp[6] = {
                        Color(141, 211, 199, 255), Color(255, 255, 179, 255),
                        Color(190, 186, 218, 255), Color(251, 128, 114, 255),
                        Color(128, 177, 211, 255), Color(253, 180, 98, 255)
                };
                return ramp[byteIndex % 6];
            }
            case PacketColorMode::ByKind: {
                switch (cell.kind) {
                    case PacketFieldKind::Payload:  return Color(222, 235, 247, 255);
                    case PacketFieldKind::Checksum: return Color(253, 208, 162, 255);
                    case PacketFieldKind::Flag:     return Color(218, 218, 235, 255);
                    case PacketFieldKind::Marker:   return Color(199, 233, 192, 255);
                    default:                        return cellFill;
                }
            }
            case PacketColorMode::Uniform:
            default:
                return cellFill;
        }
    }

    std::string UltraCanvasPacketDiagram::CellLabel(const PacketCell& cell) const {
        const PacketField* f = FieldOfCell(cell);
        std::string label = cell.name;
        if (f && !f->constantText.empty()) {
            label = label.empty() ? f->constantText : (label + "=" + f->constantText);
        }
        if (showFieldValues && f && !f->valueText.empty()) {
            label += ": " + f->valueText;
        }
        return label;
    }

// =============================================================================
// LAYOUT
// =============================================================================

    Rect2Dd UltraCanvasPacketDiagram::ContentArea() const {
        const Rect2Df b = GetBounds();
        return Rect2Dd(b.x, b.y, b.width, b.height);
    }

    double UltraCanvasPacketDiagram::MeasureRulerHeight(IRenderContext* ctx) const {
        if (rulerMode == PacketRulerMode::NoRuler) return 0.0;
        const double lineH = std::max(10.0, static_cast<double>(fontSize) + 3.0);
        double h = lineH + 6.0;                      // labels + tick marks
        if (rulerLabelStyle == PacketRulerLabelStyle::StackedDigits) {
            h = lineH * 2.0 + 6.0;                   // two stacked digits
        }
        if (!rulerTitle.empty()) h += lineH + 4.0;   // "Bits" caption above
        return h;
    }

    double UltraCanvasPacketDiagram::MeasureGutterWidth(IRenderContext* ctx,
                                                        PacketRowIndexMode mode) const {
        if (mode == PacketRowIndexMode::NoIndex) return 0.0;
        if (!ctx) return 34.0;
        // Widest plausible label for this model.
        const uint64_t maxBits = std::max<uint64_t>(model.TotalBits(), 1);
        std::string sample;
        switch (mode) {
            case PacketRowIndexMode::HexOffset:
                sample = FormatPacketOffset(maxBits, PacketSizeUnit::Bytes, true); break;
            case PacketRowIndexMode::WordNumber:
                sample = std::to_string(maxBits / std::max<uint32_t>(1, options.bitsPerRow) + 1);
                break;
            case PacketRowIndexMode::BitOffset:
                sample = std::to_string(maxBits); break;
            case PacketRowIndexMode::ByteOffset:
            default:
                sample = std::to_string(maxBits / 8); break;
        }
        return static_cast<double>(ctx->GetTextLineWidth(sample)) + 14.0;
    }

    double UltraCanvasPacketDiagram::MeasureBraceWidth(IRenderContext* ctx,
                                                       PacketEdge edge) const {
        double w = 0.0;
        for (const auto& b : braces) {
            if (b.edge != edge) continue;
            // Brace glyph plus rotated label.
            const double labelH = ctx ? static_cast<double>(ctx->GetTextLineHeight(b.label))
                                      : static_cast<double>(fontSize) + 3.0;
            w = std::max(w, labelH + 14.0);
        }
        return w;
    }

    void UltraCanvasPacketDiagram::EnsureLayout(IRenderContext* ctx) {
        const Rect2Dd area = ContentArea();
        const bool areaChanged =
                std::fabs(lastLayoutArea.width - area.width) > 0.01 ||
                std::fabs(lastLayoutArea.height - area.height) > 0.01 ||
                std::fabs(lastLayoutArea.x - area.x) > 0.01 ||
                std::fabs(lastLayoutArea.y - area.y) > 0.01;
        if (layoutValid && !areaChanged) return;

        lastLayoutArea = area;

        if (ctx) {
            ctx->SetFontFamily(fontFamily);
            ctx->SetFontSize(fontSize);
        }

        // Reserve space for chrome before laying the grid out.
        double topInset = 0.0, bottomInset = 0.0;
        const double lineH = std::max(10.0, static_cast<double>(fontSize) + 3.0);
        if (!title.empty())   topInset += static_cast<double>(titleFontSize) + 10.0;
        if (!caption.empty()) bottomInset += lineH + 8.0;
        for (const auto& r : rails) {
            if (r.edge == PacketEdge::Top)    topInset += lineH + 10.0;
            if (r.edge == PacketEdge::Bottom) bottomInset += lineH + 10.0;
        }

        Rect2Dd gridArea(area.x + 4.0, area.y + topInset + 4.0,
                         std::max(1.0, area.width - 8.0),
                         std::max(1.0, area.height - topInset - bottomInset - 8.0));

        // Legend consumes space before the grid is measured.
        if (showLegend && legend.GetEntryCount() > 0 && ctx) {
            legend.Measure(ctx, gridArea);
            gridArea = legend.RemainingArea(gridArea);
        }

        options.rulerHeight = MeasureRulerHeight(ctx);
        options.offsetGutterWidth = MeasureGutterWidth(ctx, offsetGutterMode);
        options.wordGutterWidth = MeasureGutterWidth(ctx, wordGutterMode);
        options.leftBraceWidth = MeasureBraceWidth(ctx, PacketEdge::Left);
        options.rightBraceWidth = MeasureBraceWidth(ctx, PacketEdge::Right);
        options.laneGutterWidth = laneNames.empty() ? 0.0 : [&] {
            double w = 0.0;
            for (const auto& n : laneNames) {
                w = std::max(w, ctx ? static_cast<double>(ctx->GetTextLineWidth(n)) : 40.0);
            }
            return w + 12.0;
        }();

        // Fit the rows into the available height when the model is small enough
        // to need it; otherwise keep the configured row height and let the
        // element scroll/clip.
        layout = ComputePacketLayout(model, options, gridArea);

        if (layout.rowCount > 0) {
            const double neededH = options.rulerHeight +
                                   layout.rowCount * options.rowHeight;
            if (neededH > gridArea.height && gridArea.height > options.rulerHeight) {
                PacketLayoutOptions fitted = options;
                fitted.rowHeight = std::max(
                        12.0, (gridArea.height - options.rulerHeight) /
                              static_cast<double>(layout.rowCount));
                layout = ComputePacketLayout(model, fitted, gridArea);
            }
        }

        layoutValid = true;
    }

// =============================================================================
// DRAWING HELPERS
// =============================================================================

    void UltraCanvasPacketDiagram::DrawArrowLine(IRenderContext* ctx,
                                                 const Point2Dd& from,
                                                 const Point2Dd& to,
                                                 bool arrowStart, bool arrowEnd) {
        ctx->DrawLine(from, to);

        const double dx = to.x - from.x, dy = to.y - from.y;
        const double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-6) return;
        const double ux = dx / len, uy = dy / len;
        const double head = 5.0;

        auto Head = [&](const Point2Dd& tip, double sx, double sy) {
            const double px = -sy, py = sx;   // perpendicular
            std::vector<Point2Dd> tri = {
                    tip,
                    Point2Dd(tip.x - sx * head + px * head * 0.5,
                             tip.y - sy * head + py * head * 0.5),
                    Point2Dd(tip.x - sx * head - px * head * 0.5,
                             tip.y - sy * head - py * head * 0.5)
            };
            ctx->FillLinePath(tri);
        };

        if (arrowEnd)   Head(to, ux, uy);
        if (arrowStart) Head(from, -ux, -uy);
    }

    void UltraCanvasPacketDiagram::DrawStackedDigits(IRenderContext* ctx,
                                                     const std::string& digits,
                                                     double cx, double topY) {
        const double lineH = std::max(10.0, static_cast<double>(fontSize) + 1.0);
        double y = topY;
        for (char c : digits) {
            const std::string s(1, c);
            const double w = ctx->GetTextLineWidth(s);
            ctx->DrawText(s, Point2Dd(cx - w / 2.0, y));
            y += lineH;
        }
    }

    void UltraCanvasPacketDiagram::DrawVerticalText(IRenderContext* ctx,
                                                    const std::string& text,
                                                    const Rect2Dd& cell) {
        // One glyph per line, centred - the image-1 treatment for 1-bit flags.
        const double lineH = std::max(8.0, static_cast<double>(fontSize) + 1.0);
        const double totalH = lineH * static_cast<double>(text.size());
        double y = cell.y + (cell.height - totalH) / 2.0;
        const double cx = cell.x + cell.width / 2.0;
        for (char c : text) {
            const std::string s(1, c);
            const double w = ctx->GetTextLineWidth(s);
            ctx->DrawText(s, Point2Dd(cx - w / 2.0, y));
            y += lineH;
        }
    }

// =============================================================================
// RENDER PASSES
// =============================================================================

    void UltraCanvasPacketDiagram::RenderBackground(IRenderContext* ctx) {
        const Rect2Dd area = ContentArea();
        ctx->SetFillPaint(bgColor);
        ctx->FillRectangle(area);
    }

    void UltraCanvasPacketDiagram::RenderBanding(IRenderContext* ctx) {
        if (!rowBanding) return;
        for (const auto& row : layout.rowIndex) {
            ctx->SetFillPaint((row.row % 2 == 0) ? bandColorA : bandColorB);
            ctx->FillRectangle(Rect2Dd(layout.gridArea.x, row.y,
                                       layout.gridArea.width, row.height));
        }
    }

    void UltraCanvasPacketDiagram::RenderCellText(IRenderContext* ctx,
                                                  const PacketCell& cell) {
        if (!cell.IsLabelCell() && cell.depth == 0) return;   // continuation fragment

        const std::string label = CellLabel(cell);
        if (label.empty()) return;

        ctx->SetTextPaint(textColor);
        ctx->SetFontSize(cell.depth > 0 ? fontSize - 2.0f : fontSize);

        const Size2Di ts = ctx->GetTextLineDimensions(label);
        const double avail = cell.bounds.width - 4.0;

        // A parent whose sub-fields are drawn in the lower half must keep its
        // own label in the upper half, or the two overlap.
        const PacketField* self = FieldOfCell(cell);
        const bool hasChildren = (cell.depth == 0 && self && !self->children.empty());
        Rect2Dd textArea = cell.bounds;
        if (hasChildren) {
            textArea.height = cell.bounds.height / 2.0;
        }

        if (static_cast<double>(ts.width) <= avail) {
            // Fits horizontally.
            ctx->DrawText(label,
                          Point2Dd(textArea.x + (textArea.width - ts.width) / 2.0,
                                   textArea.y + (textArea.height - ts.height) / 2.0));
            return;
        }

        // Too narrow: for a 1-bit flag cell, stack the glyphs vertically
        // (image 1's U A P R S F column). Otherwise clip with an ellipsis.
        if (cell.kind == PacketFieldKind::Flag || label.size() <= 3) {
            DrawVerticalText(ctx, label, textArea);
            return;
        }

        std::string clipped = label;
        while (!clipped.empty() &&
               static_cast<double>(ctx->GetTextLineWidth(clipped + "...")) > avail) {
            clipped.pop_back();
        }
        if (clipped.empty()) return;   // hopeless: tooltip only
        clipped += "...";
        const Size2Di cs = ctx->GetTextLineDimensions(clipped);
        ctx->DrawText(clipped,
                      Point2Dd(textArea.x + (textArea.width - cs.width) / 2.0,
                               textArea.y + (textArea.height - cs.height) / 2.0));
    }

    void UltraCanvasPacketDiagram::RenderCell(IRenderContext* ctx,
                                              const PacketCell& cell, size_t index) {
        const Color fill = CellColor(cell);
        const bool isPayload = (cell.kind == PacketFieldKind::Payload);

        // Fill.
        ctx->SetFillPaint(fill);
        if (isPayload && payloadStyle == PacketPayloadStyle::GradientCurved) {
            // Curved top-left corner: "variable length, continues beyond here".
            const Rect2Dd& b = cell.bounds;
            const double r = std::min(24.0, std::min(b.width, b.height) / 2.0);
            ctx->ClearPath();
            ctx->MoveTo(b.x, b.y + r);
            ctx->BezierCurveTo(b.x, b.y, b.x, b.y, b.x + r, b.y);
            ctx->LineTo(b.x + b.width, b.y);
            ctx->LineTo(b.x + b.width, b.y + b.height);
            ctx->LineTo(b.x, b.y + b.height);
            ctx->ClosePath();
            ctx->Fill();
        } else {
            ctx->FillRectangle(cell.bounds);
        }

        // Selection / hover overlay.
        if (index == selectedCell) {
            ctx->SetFillPaint(Color(255, 200, 60, 90));
            ctx->FillRectangle(cell.bounds);
        } else if (index == hoveredCell) {
            ctx->SetFillPaint(Color(255, 255, 255, 60));
            ctx->FillRectangle(cell.bounds);
        }

        // Borders. A cut edge is dashed and thinner, so a wrapped field reads
        // as ONE field rather than two adjacent ones.
        ctx->SetStrokePaint(cellBorder);
        ctx->SetStrokeWidth(cell.depth > 0 ? 0.8 : 1.2);

        const Rect2Dd& b = cell.bounds;
        const Point2Dd tl(b.x, b.y), tr(b.x + b.width, b.y);
        const Point2Dd bl(b.x, b.y + b.height), br(b.x + b.width, b.y + b.height);

        ctx->DrawLine(tl, tr);
        ctx->DrawLine(bl, br);

        if (cell.continuesLeft) {
            UCDashPattern dash({2.0, 3.0});
            ctx->SetLineDash(dash);
            ctx->SetStrokeWidth(0.6);
            ctx->DrawLine(tl, bl);
            ctx->SetLineDash(UCDashPattern());
            ctx->SetStrokeWidth(cell.depth > 0 ? 0.8 : 1.2);
        } else {
            ctx->DrawLine(tl, bl);
        }

        if (cell.continuesRight) {
            UCDashPattern dash({2.0, 3.0});
            ctx->SetLineDash(dash);
            ctx->SetStrokeWidth(0.6);
            ctx->DrawLine(tr, br);
            ctx->SetLineDash(UCDashPattern());
        } else {
            ctx->DrawLine(tr, br);
        }

        RenderCellText(ctx, cell);
    }

    void UltraCanvasPacketDiagram::RenderCells(IRenderContext* ctx) {
        for (size_t i = 0; i < layout.cells.size(); ++i) {
            RenderCell(ctx, layout.cells[i], i);
        }
    }

    void UltraCanvasPacketDiagram::RenderRuler(IRenderContext* ctx) {
        if (rulerMode == PacketRulerMode::NoRuler || layout.gridArea.width <= 0) return;

        const double lineH = std::max(10.0, static_cast<double>(fontSize) + 3.0);
        const double gridTop = layout.gridArea.y;
        double labelTop = gridTop - options.rulerHeight;
        if (!rulerTitle.empty()) {
            ctx->SetTextPaint(rulerColor);
            ctx->SetFontSize(fontSize);
            const double w = ctx->GetTextLineWidth(rulerTitle);
            ctx->DrawText(rulerTitle,
                          Point2Dd(layout.gridArea.x + (layout.gridArea.width - w) / 2.0,
                                   labelTop));
            labelTop += lineH + 4.0;
        }

        ctx->SetStrokePaint(rulerColor);
        ctx->SetStrokeWidth(1.0);
        ctx->SetTextPaint(rulerColor);
        ctx->SetFontSize(fontSize);

        if (rulerMode == PacketRulerMode::ByteColumns) {
            const uint32_t byteCount = std::max<uint32_t>(1, options.bitsPerRow / 8);
            const double colW = layout.gridArea.width / static_cast<double>(byteCount);
            for (uint32_t i = 0; i < byteCount; ++i) {
                const std::string label = "Byte " + std::to_string(i + 1);
                const double w = ctx->GetTextLineWidth(label);
                const double cx = layout.gridArea.x + colW * (i + 0.5);
                ctx->DrawText(label, Point2Dd(cx - w / 2.0, labelTop));
            }
            return;
        }

        // Bit ruler. Explicit ticks win; otherwise every 4 bits plus the last.
        std::vector<uint32_t> ticks = rulerTicks;
        if (ticks.empty()) {
            for (uint32_t b = 0; b < options.bitsPerRow; b += 4) ticks.push_back(b);
            if (options.bitsPerRow > 0) ticks.push_back(options.bitsPerRow - 1);
        }

        for (uint32_t bit : ticks) {
            if (bit >= options.bitsPerRow) continue;
            // Centre the label on the bit cell, matching how the RFC figures
            // align a tick with the bit it names.
            const double cx = layout.gridArea.x +
                              (static_cast<double>(bit) + 0.5) * layout.bitWidth;
            const std::string label = std::to_string(bit);

            if (rulerLabelStyle == PacketRulerLabelStyle::StackedDigits &&
                label.size() > 1) {
                DrawStackedDigits(ctx, label, cx, labelTop);
            } else {
                const double w = ctx->GetTextLineWidth(label);
                ctx->DrawText(label, Point2Dd(cx - w / 2.0, labelTop));
            }

            // Tick mark down to the grid.
            ctx->DrawLine(Point2Dd(cx, gridTop - 4.0), Point2Dd(cx, gridTop));
        }
    }

    void UltraCanvasPacketDiagram::RenderGutters(IRenderContext* ctx) {
        if (layout.rowIndex.empty()) return;

        ctx->SetTextPaint(gutterColor);
        ctx->SetFontSize(fontSize);

        const double wordX = layout.gridArea.x - options.offsetGutterWidth -
                             options.wordGutterWidth;
        const double offsetX = layout.gridArea.x - options.offsetGutterWidth;

        for (const auto& row : layout.rowIndex) {
            if (wordGutterMode != PacketRowIndexMode::NoIndex) {
                const std::string label =
                        (wordGutterMode == PacketRowIndexMode::WordNumber)
                        ? row.wordLabel : row.offsetLabel;
                const Size2Di ts = ctx->GetTextLineDimensions(label);
                ctx->DrawText(label,
                              Point2Dd(wordX + (options.wordGutterWidth - ts.width) / 2.0,
                                       row.y + (row.height - ts.height) / 2.0));
            }
            if (offsetGutterMode != PacketRowIndexMode::NoIndex) {
                std::string label = row.offsetLabel;
                if (offsetGutterMode == PacketRowIndexMode::HexOffset) {
                    label = FormatPacketOffset(
                            static_cast<uint64_t>(row.row) * options.bitsPerRow,
                            PacketSizeUnit::Bytes, true);
                } else if (offsetGutterMode == PacketRowIndexMode::BitOffset) {
                    label = std::to_string(
                            static_cast<uint64_t>(row.row) * options.bitsPerRow);
                }
                const Size2Di ts = ctx->GetTextLineDimensions(label);
                ctx->DrawText(label,
                              Point2Dd(offsetX + (options.offsetGutterWidth - ts.width) / 2.0,
                                       row.y + (row.height - ts.height) / 2.0));
            }
        }

        // Lane names (image 5's DAT3..DAT0 gutter).
        if (!laneNames.empty() && options.mode == PacketLayoutMode::Lanes) {
            for (size_t i = 0; i < laneNames.size() && i < layout.rowIndex.size(); ++i) {
                const auto& row = layout.rowIndex[i];
                const Size2Di ts = ctx->GetTextLineDimensions(laneNames[i]);
                ctx->DrawText(laneNames[i],
                              Point2Dd(layout.gridArea.x - options.laneGutterWidth + 4.0,
                                       row.y + (row.height - ts.height) / 2.0));
            }
        }
    }

    void UltraCanvasPacketDiagram::RenderBraces(IRenderContext* ctx) {
        if (braces.empty()) return;

        ctx->SetStrokePaint(gutterColor);
        ctx->SetFillPaint(gutterColor);
        ctx->SetTextPaint(gutterColor);
        ctx->SetStrokeWidth(1.0);
        ctx->SetFontSize(fontSize);

        for (const auto& brace : braces) {
            // Find the vertical span this brace covers.
            double top = layout.gridArea.y;
            double bottom = layout.gridArea.y + layout.gridArea.height;
            if (!brace.layerId.empty()) {
                bool found = false;
                for (const auto& span : layout.layerSpans) {
                    if (span.layerIndex < model.layers.size() &&
                        model.layers[span.layerIndex].id == brace.layerId) {
                        top = span.y;
                        bottom = span.y + span.height;
                        found = true;
                        break;
                    }
                }
                if (!found) continue;
            }
            if (bottom - top < 2.0) continue;

            const bool isLeft = (brace.edge == PacketEdge::Left);
            const double x = isLeft
                             ? layout.gridArea.x - options.offsetGutterWidth -
                               options.wordGutterWidth - 6.0
                             : layout.gridArea.x + layout.gridArea.width + 6.0;

            // A simple square brace with end ticks reads cleanly at any size.
            const double tick = isLeft ? 5.0 : -5.0;
            ctx->DrawLine(Point2Dd(x, top), Point2Dd(x, bottom));
            ctx->DrawLine(Point2Dd(x, top), Point2Dd(x + tick, top));
            ctx->DrawLine(Point2Dd(x, bottom), Point2Dd(x + tick, bottom));

            if (!brace.label.empty()) {
                // Rotate the label to run along the brace.
                ctx->PushState();
                const double cy = (top + bottom) / 2.0;
                const double labelW = ctx->GetTextLineWidth(brace.label);
                const double labelH = ctx->GetTextLineHeight(brace.label);
                ctx->Translate(x + (isLeft ? -4.0 : 4.0), cy);
                ctx->Rotate(-3.14159265358979 / 2.0);
                ctx->DrawText(brace.label,
                              Point2Dd(-labelW / 2.0, isLeft ? -labelH : 0.0));
                ctx->PopState();
            }
        }
    }

    void UltraCanvasPacketDiagram::RenderRails(IRenderContext* ctx) {
        if (rails.empty() || layout.bitWidth <= 0.0) return;

        ctx->SetStrokePaint(gutterColor);
        ctx->SetFillPaint(gutterColor);
        ctx->SetTextPaint(gutterColor);
        ctx->SetStrokeWidth(1.0);
        ctx->SetFontSize(fontSize);

        const double lineH = std::max(10.0, static_cast<double>(fontSize) + 3.0);
        double topCursor = layout.gridArea.y - options.rulerHeight - lineH - 6.0;
        double bottomCursor = layout.gridArea.y + layout.gridArea.height + lineH + 6.0;

        for (const auto& rail : rails) {
            const double x1 = layout.gridArea.x +
                              static_cast<double>(rail.startBit) * layout.bitWidth;
            const double x2 = layout.gridArea.x +
                              static_cast<double>(rail.endBit + 1) * layout.bitWidth;
            const bool isTop = (rail.edge == PacketEdge::Top);
            const double y = isTop ? topCursor : bottomCursor;

            DrawArrowLine(ctx, Point2Dd(x1, y), Point2Dd(x2, y), true, true);
            // End ticks.
            ctx->DrawLine(Point2Dd(x1, y - 4.0), Point2Dd(x1, y + 4.0));
            ctx->DrawLine(Point2Dd(x2, y - 4.0), Point2Dd(x2, y + 4.0));

            if (!rail.label.empty()) {
                const double w = ctx->GetTextLineWidth(rail.label);
                const double h = ctx->GetTextLineHeight(rail.label);
                const double cx = (x1 + x2) / 2.0;
                // Clear a gap in the arrow so the label is legible.
                ctx->SetFillPaint(bgColor);
                ctx->FillRectangle(Rect2Dd(cx - w / 2.0 - 3.0, y - h / 2.0,
                                           w + 6.0, h));
                ctx->SetTextPaint(gutterColor);
                ctx->DrawText(rail.label, Point2Dd(cx - w / 2.0, y - h / 2.0));
                ctx->SetFillPaint(gutterColor);
            }

            if (isTop) topCursor -= lineH + 8.0;
            else       bottomCursor += lineH + 8.0;
        }
    }

    void UltraCanvasPacketDiagram::RenderTitleAndCaption(IRenderContext* ctx) {
        const Rect2Dd area = ContentArea();
        if (!title.empty()) {
            ctx->SetFontSize(titleFontSize);
            ctx->SetFontWeight(FontWeight::Bold);
            ctx->SetTextPaint(textColor);
            const double w = ctx->GetTextLineWidth(title);
            ctx->DrawText(title, Point2Dd(area.x + (area.width - w) / 2.0, area.y + 4.0));
            ctx->SetFontWeight(FontWeight::Normal);
        }
        if (!caption.empty()) {
            ctx->SetFontSize(fontSize);
            ctx->SetTextPaint(textColor);
            const double w = ctx->GetTextLineWidth(caption);
            const double h = ctx->GetTextLineHeight(caption);
            ctx->DrawText(caption,
                          Point2Dd(area.x + (area.width - w) / 2.0,
                                   area.y + area.height - h - 4.0));
        }
        if (!model.trailerText.empty() && layout.rowCount > 0) {
            ctx->SetFontSize(fontSize);
            ctx->SetTextPaint(textColor);
            const double w = ctx->GetTextLineWidth(model.trailerText);
            const double y = layout.gridArea.y + layout.gridArea.height + 4.0;
            ctx->DrawText(model.trailerText,
                          Point2Dd(layout.gridArea.x +
                                   (layout.gridArea.width - w) / 2.0, y));
        }
    }

// =============================================================================
// RENDER
// =============================================================================

    void UltraCanvasPacketDiagram::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        if (!ctx || !IsVisible()) return;

        EnsureLayout(ctx);

        ctx->PushState();
        ctx->SetFontFamily(fontFamily);
        ctx->SetFontSize(fontSize);

        RenderBackground(ctx);

        if (model.IsEmpty()) {
            ctx->SetTextPaint(textColor);
            const std::string msg = parseErrors.empty()
                                    ? "No packet model"
                                    : parseErrors.front();
            const Rect2Dd area = ContentArea();
            const double w = ctx->GetTextLineWidth(msg);
            const double h = ctx->GetTextLineHeight(msg);
            ctx->DrawText(msg, Point2Dd(area.x + (area.width - w) / 2.0,
                                        area.y + (area.height - h) / 2.0));
            ctx->PopState();
            return;
        }

        RenderBanding(ctx);
        RenderCells(ctx);
        RenderRuler(ctx);
        RenderGutters(ctx);
        RenderBraces(ctx);
        RenderRails(ctx);
        RenderTitleAndCaption(ctx);

        if (showLegend && legend.GetEntryCount() > 0) {
            legend.Render(ctx, lastLayoutArea);
        }

        ctx->PopState();
    }

// =============================================================================
// EVENTS
// =============================================================================

    bool UltraCanvasPacketDiagram::OnEvent(const UCEvent& event) {
        if (!IsVisible() || IsDisabled()) return false;

        switch (event.type) {
            case UCEventType::MouseMove: {
                const Point2Dd p(event.pointer.x, event.pointer.y);
                const PacketCell* cell = layout.FindCellAt(p);
                size_t newHover = SIZE_MAX;
                if (cell) {
                    newHover = static_cast<size_t>(cell - layout.cells.data());
                }
                if (newHover != hoveredCell) {
                    hoveredCell = newHover;
                    if (cell) {
                        if (showTooltips) {
                            const PacketFieldRef ref = MakeFieldRef(*cell);
                            const PacketField* f = FieldOfCell(*cell);
                            std::string tip = ref.name;
                            char buf[160];
                            std::snprintf(buf, sizeof(buf),
                                          "\nbit %llu, byte %llu, %llu bits",
                                          (unsigned long long)ref.bitOffset,
                                          (unsigned long long)ref.ByteOffset(),
                                          (unsigned long long)ref.bitLength);
                            tip += buf;
                            if (f && !f->description.empty()) tip += "\n" + f->description;
                            if (f && !f->valueText.empty())   tip += "\n= " + f->valueText;
                            SetTooltip(tip);
                        }
                        if (onFieldHover) onFieldHover(MakeFieldRef(*cell));
                    } else {
                        SetTooltip("");
                    }
                    RequestRedraw();
                }
                return cell != nullptr;
            }

            case UCEventType::MouseLeave: {
                if (hoveredCell != SIZE_MAX) {
                    hoveredCell = SIZE_MAX;
                    SetTooltip("");
                    RequestRedraw();
                }
                return false;
            }

            case UCEventType::MouseDown: {
                const Point2Dd p(event.pointer.x, event.pointer.y);
                const PacketCell* cell = layout.FindCellAt(p);
                if (!cell) return false;
                selectedCell = static_cast<size_t>(cell - layout.cells.data());
                if (onFieldClick) onFieldClick(MakeFieldRef(*cell));
                RequestRedraw();
                return true;
            }

            default:
                break;
        }
        return false;
    }

    void UltraCanvasPacketDiagram::SetSelectedField(const std::string& nameOrId) {
        selectedCell = SIZE_MAX;
        for (size_t i = 0; i < layout.cells.size(); ++i) {
            const PacketField* f = FieldOfCell(layout.cells[i]);
            if (f && (f->id == nameOrId || f->name == nameOrId)) {
                selectedCell = i;
                break;
            }
        }
        RequestRedraw();
    }

    void UltraCanvasPacketDiagram::ClearSelection() {
        selectedCell = SIZE_MAX;
        RequestRedraw();
    }

    PacketFieldRef UltraCanvasPacketDiagram::GetSelectedField() const {
        if (selectedCell < layout.cells.size()) {
            return MakeFieldRef(layout.cells[selectedCell]);
        }
        return PacketFieldRef();
    }

} // namespace UltraCanvas
