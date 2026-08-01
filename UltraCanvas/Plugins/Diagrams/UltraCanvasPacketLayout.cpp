// Plugins/Diagrams/UltraCanvasPacketLayout.cpp
// Packet structure layout engine + model validation.
// Version: 1.0.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasPacketLayout.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace UltraCanvas {

// =============================================================================
// HELPERS
// =============================================================================

    double PacketLengthWeight(uint64_t bits, PacketLengthScale scale) {
        const double b = static_cast<double>(bits);
        if (b <= 0.0) return 0.0;
        switch (scale) {
            case PacketLengthScale::Sqrt: return std::sqrt(b);
            case PacketLengthScale::Log:  return std::log2(b + 1.0);
            case PacketLengthScale::Linear:
            case PacketLengthScale::Clamped:
            default:                      return b;
        }
    }

    std::string FormatPacketOffset(uint64_t bitOffset, PacketSizeUnit unit, bool hex) {
        char buf[32];
        uint64_t value = bitOffset;
        if (unit == PacketSizeUnit::Bytes ||
            (unit == PacketSizeUnit::Auto && bitOffset % 8 == 0)) {
            value = bitOffset / 8;
        }
        if (hex) {
            std::snprintf(buf, sizeof(buf), "0x%02llX",
                          static_cast<unsigned long long>(value));
        } else {
            std::snprintf(buf, sizeof(buf), "%llu",
                          static_cast<unsigned long long>(value));
        }
        return std::string(buf);
    }

// =============================================================================
// RESULT QUERIES
// =============================================================================

    const PacketCell* PacketLayoutResult::FindCellAt(const Point2Dd& p) const {
        // Iterate in reverse so deeper sub-cells (added after their parents)
        // win over the parent cell they are drawn inside.
        for (size_t i = cells.size(); i-- > 0; ) {
            const PacketCell& c = cells[i];
            if (p.x >= c.bounds.x && p.x <= c.bounds.x + c.bounds.width &&
                p.y >= c.bounds.y && p.y <= c.bounds.y + c.bounds.height) {
                return &cells[i];
            }
        }
        return nullptr;
    }

    std::vector<size_t> PacketLayoutResult::CellsOfField(size_t layerIndex,
                                                         size_t fieldIndex) const {
        std::vector<size_t> out;
        for (size_t i = 0; i < cells.size(); ++i) {
            if (cells[i].layerIndex == layerIndex &&
                cells[i].fieldIndex == fieldIndex &&
                cells[i].depth == 0) {
                out.push_back(i);
            }
        }
        return out;
    }

// =============================================================================
// WORD GRID
// =============================================================================

namespace {

    struct GridContext {
        const PacketModel* model = nullptr;
        const PacketLayoutOptions* opt = nullptr;
        double gridX = 0.0;
        double gridY = 0.0;
        double bitWidth = 0.0;
        double rowHeight = 0.0;
        uint32_t bitsPerRow = 32;
    };

    // Emit the cells for one field, splitting it at every row boundary.
    // absStart is the field's offset from the start of the PDU.
    void EmitWordGridField(const GridContext& g,
                           const PacketField& field,
                           uint64_t absStart,
                           uint64_t bitLength,
                           size_t layerIndex,
                           size_t fieldIndex,
                           int depth,
                           size_t parentCell,
                           PacketLayoutResult& out) {
        if (bitLength == 0) return;

        // How many fragments will this produce? Compute up front so each cell
        // can report cellCountInField (renderers use it to decide whether the
        // field reads as one logical box).
        size_t fragments = 0;
        {
            uint64_t remaining = bitLength;
            uint64_t cursor = absStart;
            while (remaining > 0) {
                const uint64_t col = cursor % g.bitsPerRow;
                const uint64_t take = std::min<uint64_t>(remaining, g.bitsPerRow - col);
                remaining -= take;
                cursor += take;
                ++fragments;
            }
        }

        uint64_t remaining = bitLength;
        uint64_t cursor = absStart;
        size_t fragIndex = 0;

        while (remaining > 0) {
            const uint64_t col = cursor % g.bitsPerRow;
            const uint64_t row = cursor / g.bitsPerRow;
            const uint64_t take = std::min<uint64_t>(remaining, g.bitsPerRow - col);

            PacketCell cell;
            cell.layerIndex = layerIndex;
            cell.fieldIndex = fieldIndex;
            cell.cellIndexInField = fragIndex;
            cell.cellCountInField = fragments;
            cell.depth = depth;
            cell.parentCell = parentCell;
            cell.bitOffset = cursor;
            cell.bitLength = take;
            cell.row = static_cast<uint32_t>(row);
            cell.name = field.name;
            cell.kind = field.kind;
            cell.hasColorOverride = field.hasColorOverride;
            cell.color = field.color;

            cell.bounds = Rect2Dd(
                    g.gridX + static_cast<double>(col) * g.bitWidth,
                    g.gridY + static_cast<double>(row) * g.rowHeight,
                    static_cast<double>(take) * g.bitWidth,
                    g.rowHeight);

            // A cut edge means "this field continues" - NOT "this row ends".
            cell.continuesLeft  = (fragIndex > 0);
            cell.continuesRight = (fragIndex + 1 < fragments);

            const size_t myIndex = out.cells.size();
            out.cells.push_back(cell);

            // Sub-fields are drawn inside their parent's first fragment only;
            // a nested field that itself spans rows is vanishingly rare in real
            // protocols and would make the parent unreadable anyway.
            if (depth == 0 && fragIndex == 0 && !field.children.empty()) {
                const double childH = g.rowHeight / 2.0;
                for (size_t ci = 0; ci < field.children.size(); ++ci) {
                    const PacketField& child = field.children[ci];
                    if (child.bitLength == 0) continue;
                    const double childX = cell.bounds.x +
                            static_cast<double>(child.bitOffset - field.bitOffset) * g.bitWidth;
                    PacketCell cc;
                    cc.layerIndex = layerIndex;
                    cc.fieldIndex = fieldIndex;
                    cc.cellIndexInField = ci;
                    cc.cellCountInField = field.children.size();
                    cc.depth = 1;
                    cc.parentCell = myIndex;
                    cc.bitOffset = absStart + (child.bitOffset - field.bitOffset);
                    cc.bitLength = child.bitLength;
                    cc.row = cell.row;
                    cc.name = child.name;
                    cc.kind = child.kind;
                    cc.hasColorOverride = child.hasColorOverride;
                    cc.color = child.color;
                    cc.bounds = Rect2Dd(childX, cell.bounds.y + childH,
                                        static_cast<double>(child.bitLength) * g.bitWidth,
                                        childH);
                    out.cells.push_back(cc);
                }
            }

            remaining -= take;
            cursor += take;
            ++fragIndex;
        }
    }

    // Total rows the model needs, accounting for open-ended payloads.
    uint32_t CountWordGridRows(const PacketModel& model,
                               const PacketLayoutOptions& opt) {
        uint64_t cursor = 0;
        double extraRows = 0.0;
        for (const auto& layer : model.layers) {
            for (const auto& f : layer.fields) {
                if (f.IsVariableLength() && f.bitLength == 0) {
                    // Open-ended: start a fresh row and reserve payloadRowCount.
                    if (cursor % opt.bitsPerRow != 0) {
                        cursor += opt.bitsPerRow - (cursor % opt.bitsPerRow);
                    }
                    extraRows += opt.payloadRowCount;
                } else {
                    cursor += f.bitLength;
                }
            }
        }
        const uint32_t base = static_cast<uint32_t>(
                (cursor + opt.bitsPerRow - 1) / opt.bitsPerRow);
        return base + static_cast<uint32_t>(std::ceil(extraRows));
    }

} // anonymous namespace

// =============================================================================
// MAIN ENTRY POINT
// =============================================================================

    static PacketLayoutResult LayoutWordGrid(const PacketModel& model,
                                             const PacketLayoutOptions& opt,
                                             const Rect2Dd& area) {
        PacketLayoutResult out;

        const double leftInset = opt.leftBraceWidth + opt.wordGutterWidth +
                                 opt.offsetGutterWidth;
        const double gridX = area.x + leftInset;
        const double gridY = area.y + opt.rulerHeight;
        const double gridW = std::max(1.0, area.width - leftInset - opt.rightBraceWidth);

        const uint32_t bitsPerRow = opt.bitsPerRow > 0 ? opt.bitsPerRow : 32;
        const double bitWidth = gridW / static_cast<double>(bitsPerRow);

        GridContext g;
        g.model = &model;
        g.opt = &opt;
        g.gridX = gridX;
        g.gridY = gridY;
        g.bitWidth = bitWidth;
        g.rowHeight = opt.rowHeight;
        g.bitsPerRow = bitsPerRow;

        uint64_t cursor = 0;
        for (size_t li = 0; li < model.layers.size(); ++li) {
            const PacketLayer& layer = model.layers[li];
            const double spanTop = gridY +
                    static_cast<double>(cursor / bitsPerRow) * opt.rowHeight;

            for (size_t fi = 0; fi < layer.fields.size(); ++fi) {
                const PacketField& f = layer.fields[fi];

                uint64_t bits = f.bitLength;
                if (f.IsVariableLength() && bits == 0) {
                    // Align to the next row, then give it payloadRowCount rows.
                    if (cursor % bitsPerRow != 0) {
                        cursor += bitsPerRow - (cursor % bitsPerRow);
                    }
                    bits = static_cast<uint64_t>(
                            std::ceil(opt.payloadRowCount)) * bitsPerRow;
                }

                EmitWordGridField(g, f, cursor, bits, li, fi, 0, SIZE_MAX, out);
                cursor += bits;
            }

            const double spanBottom = gridY +
                    static_cast<double>((cursor + bitsPerRow - 1) / bitsPerRow) * opt.rowHeight;
            PacketBracketSpan span;
            span.layerIndex = li;
            span.label = layer.bracketLabel.empty() ? layer.name : layer.bracketLabel;
            span.y = spanTop;
            span.height = std::max(0.0, spanBottom - spanTop);
            if (!span.label.empty()) out.layerSpans.push_back(span);
        }

        out.rowCount = static_cast<uint32_t>((cursor + bitsPerRow - 1) / bitsPerRow);
        out.bitWidth = bitWidth;
        out.gridArea = Rect2Dd(gridX, gridY, gridW,
                               out.rowCount * opt.rowHeight);
        out.totalArea = Rect2Dd(area.x, area.y, area.width,
                                opt.rulerHeight + out.rowCount * opt.rowHeight);

        // Row index entries (offset gutter + word gutter).
        for (uint32_t r = 0; r < out.rowCount; ++r) {
            PacketRowIndexEntry e;
            e.row = r;
            e.y = gridY + r * opt.rowHeight;
            e.height = opt.rowHeight;
            e.offsetLabel = FormatPacketOffset(
                    static_cast<uint64_t>(r) * bitsPerRow, PacketSizeUnit::Auto, false);
            e.wordLabel = std::to_string(r + 1);
            out.rowIndex.push_back(e);
        }

        out.valid = true;
        return out;
    }

    static PacketLayoutResult LayoutProportional(const PacketModel& model,
                                                 const PacketLayoutOptions& opt,
                                                 const Rect2Dd& area) {
        PacketLayoutResult out;

        const double leftInset = opt.leftBraceWidth + opt.offsetGutterWidth;
        const double stripX = area.x + leftInset;
        const double stripY = area.y + opt.rulerHeight;
        const double stripW = std::max(1.0, area.width - leftInset - opt.rightBraceWidth);

        // Collect every top-level field across layers, in order.
        struct Item { size_t layerIndex, fieldIndex; const PacketField* field; };
        std::vector<Item> items;
        for (size_t li = 0; li < model.layers.size(); ++li) {
            for (size_t fi = 0; fi < model.layers[li].fields.size(); ++fi) {
                items.push_back({li, fi, &model.layers[li].fields[fi]});
            }
        }
        if (items.empty()) {
            out.valid = true;
            return out;
        }

        // Two-pass width assignment. Clamped mode gives every cell at least
        // minCellWidth, then distributes what remains by weight, so a 1-byte
        // field beside a 96-byte field is still readable (image 3).
        std::vector<double> weights(items.size(), 0.0);
        double totalWeight = 0.0;
        for (size_t i = 0; i < items.size(); ++i) {
            weights[i] = PacketLengthWeight(items[i].field->bitLength, opt.lengthScale);
            totalWeight += weights[i];
        }
        if (totalWeight <= 0.0) totalWeight = 1.0;

        std::vector<double> widths(items.size(), 0.0);
        if (opt.lengthScale == PacketLengthScale::Clamped) {
            const double floorTotal = opt.minCellWidth * static_cast<double>(items.size());
            const double flexible = std::max(0.0, stripW - floorTotal);
            for (size_t i = 0; i < items.size(); ++i) {
                widths[i] = opt.minCellWidth + flexible * (weights[i] / totalWeight);
            }
        } else {
            for (size_t i = 0; i < items.size(); ++i) {
                widths[i] = std::max(opt.minCellWidth,
                                     stripW * (weights[i] / totalWeight));
            }
        }

        double x = stripX;
        uint64_t bitCursor = 0;
        for (size_t i = 0; i < items.size(); ++i) {
            const PacketField& f = *items[i].field;

            PacketCell cell;
            cell.layerIndex = items[i].layerIndex;
            cell.fieldIndex = items[i].fieldIndex;
            cell.cellIndexInField = 0;
            cell.cellCountInField = 1;
            cell.depth = 0;
            cell.bitOffset = bitCursor;
            cell.bitLength = f.bitLength;
            cell.row = 0;
            cell.name = f.name;
            cell.kind = f.kind;
            cell.hasColorOverride = f.hasColorOverride;
            cell.color = f.color;
            cell.bounds = Rect2Dd(x, stripY, widths[i], opt.rowHeight);
            out.cells.push_back(cell);

            x += widths[i];
            bitCursor += f.bitLength;
        }

        out.rowCount = 1;
        out.bitWidth = stripW / std::max<double>(1.0, static_cast<double>(bitCursor));
        out.gridArea = Rect2Dd(stripX, stripY, stripW, opt.rowHeight);
        out.totalArea = Rect2Dd(area.x, area.y, area.width,
                                opt.rulerHeight + opt.rowHeight);
        out.valid = true;
        return out;
    }

    static PacketLayoutResult LayoutLanes(const PacketModel& model,
                                          const PacketLayoutOptions& opt,
                                          const Rect2Dd& area) {
        PacketLayoutResult out;

        const uint32_t lanes = std::max<uint32_t>(1, opt.laneCount);
        const double gridX = area.x + opt.laneGutterWidth;
        const double gridY = area.y + opt.rulerHeight;
        const double gridW = std::max(1.0, area.width - opt.laneGutterWidth);

        // Total bits across all fields, distributed over the lanes.
        uint64_t totalBits = 0;
        for (const auto& layer : model.layers) {
            for (const auto& f : layer.fields) totalBits += f.bitLength;
        }
        if (totalBits == 0) {
            out.valid = true;
            return out;
        }

        const uint64_t bitsPerLane = (totalBits + lanes - 1) / lanes;
        const double bitWidth = gridW / static_cast<double>(bitsPerLane);

        uint64_t cursor = 0;
        for (size_t li = 0; li < model.layers.size(); ++li) {
            const PacketLayer& layer = model.layers[li];
            for (size_t fi = 0; fi < layer.fields.size(); ++fi) {
                const PacketField& f = layer.fields[fi];
                if (f.bitLength == 0) continue;

                // Split at lane boundaries, exactly as the word grid splits at
                // row boundaries.
                uint64_t remaining = f.bitLength;
                uint64_t pos = cursor;
                size_t frag = 0;

                const size_t fragments = [&] {
                    size_t n = 0;
                    uint64_t rem = f.bitLength, p = cursor;
                    while (rem > 0) {
                        const uint64_t col = p % bitsPerLane;
                        const uint64_t take = std::min<uint64_t>(rem, bitsPerLane - col);
                        rem -= take; p += take; ++n;
                    }
                    return n;
                }();

                while (remaining > 0) {
                    const uint64_t col = pos % bitsPerLane;
                    const uint64_t lane = pos / bitsPerLane;
                    const uint64_t take = std::min<uint64_t>(remaining, bitsPerLane - col);

                    PacketCell cell;
                    cell.layerIndex = li;
                    cell.fieldIndex = fi;
                    cell.cellIndexInField = frag;
                    cell.cellCountInField = fragments;
                    cell.bitOffset = pos;
                    cell.bitLength = take;
                    cell.row = static_cast<uint32_t>(lane);
                    cell.name = f.name;
                    cell.kind = f.kind;
                    cell.hasColorOverride = f.hasColorOverride;
                    cell.color = f.color;
                    cell.bounds = Rect2Dd(
                            gridX + static_cast<double>(col) * bitWidth,
                            gridY + static_cast<double>(lane) * opt.rowHeight,
                            static_cast<double>(take) * bitWidth,
                            opt.rowHeight);
                    cell.continuesLeft = (frag > 0);
                    cell.continuesRight = (frag + 1 < fragments);
                    out.cells.push_back(cell);

                    remaining -= take;
                    pos += take;
                    ++frag;
                }
                cursor += f.bitLength;
            }
        }

        out.rowCount = lanes;
        out.bitWidth = bitWidth;
        out.gridArea = Rect2Dd(gridX, gridY, gridW, lanes * opt.rowHeight);
        out.totalArea = Rect2Dd(area.x, area.y, area.width,
                                opt.rulerHeight + lanes * opt.rowHeight);

        for (uint32_t l = 0; l < lanes; ++l) {
            PacketRowIndexEntry e;
            e.row = l;
            e.y = gridY + l * opt.rowHeight;
            e.height = opt.rowHeight;
            out.rowIndex.push_back(e);
        }

        out.valid = true;
        return out;
    }

    PacketLayoutResult ComputePacketLayout(const PacketModel& model,
                                           const PacketLayoutOptions& options,
                                           const Rect2Dd& area) {
        PacketLayoutOptions opt = options;
        if (opt.bitsPerRow == 0) opt.bitsPerRow = model.bitsPerRow;
        if (opt.bitsPerRow == 0) opt.bitsPerRow = 32;

        if (model.IsEmpty() || area.width <= 0.0 || area.height <= 0.0) {
            PacketLayoutResult empty;
            empty.totalArea = area;
            empty.valid = true;
            return empty;
        }

        switch (opt.mode) {
            case PacketLayoutMode::ProportionalLinear:
                return LayoutProportional(model, opt, area);
            case PacketLayoutMode::Lanes:
                return LayoutLanes(model, opt, area);
            case PacketLayoutMode::Encapsulation:
            case PacketLayoutMode::WordGrid:
            default:
                return LayoutWordGrid(model, opt, area);
        }
    }

// =============================================================================
// VALIDATION
// =============================================================================

    PacketValidationResult ValidatePacketModel(const PacketModel& model,
                                               uint64_t expectedTotalBits) {
        PacketValidationResult result;

        std::map<std::string, int> idCounts;

        for (const auto& layer : model.layers) {
            // V1 overlap + V2 gap, per layer.
            std::vector<const PacketField*> sorted;
            for (const auto& f : layer.fields) sorted.push_back(&f);
            std::sort(sorted.begin(), sorted.end(),
                      [](const PacketField* a, const PacketField* b) {
                          return a->bitOffset < b->bitOffset;
                      });

            for (size_t i = 0; i < sorted.size(); ++i) {
                const PacketField& f = *sorted[i];

                // V5 diagnostics.
                if (f.name.empty() && f.kind != PacketFieldKind::Reserved &&
                    f.kind != PacketFieldKind::Padding) {
                    PacketValidationIssue issue;
                    issue.severity = PacketValidationSeverity::Warning;
                    issue.message = "Field has no name";
                    issue.layerId = layer.id;
                    issue.bitOffset = f.bitOffset;
                    result.issues.push_back(issue);
                }
                if (f.bitLength == 0 && !f.IsVariableLength()) {
                    PacketValidationIssue issue;
                    issue.severity = PacketValidationSeverity::Error;
                    issue.message = "Field '" + f.name + "' has zero length";
                    issue.layerId = layer.id;
                    issue.fieldId = f.id.empty() ? f.name : f.id;
                    issue.bitOffset = f.bitOffset;
                    result.issues.push_back(issue);
                }
                if (!f.id.empty()) idCounts[f.id]++;

                if (i + 1 < sorted.size()) {
                    const PacketField& next = *sorted[i + 1];
                    if (f.bitLength > 0 && f.BitEnd() > next.bitOffset) {
                        PacketValidationIssue issue;
                        issue.severity = PacketValidationSeverity::Error;
                        issue.message = "Field '" + f.name + "' overlaps '" +
                                        next.name + "' at bit " +
                                        std::to_string(next.bitOffset);
                        issue.layerId = layer.id;
                        issue.fieldId = f.id.empty() ? f.name : f.id;
                        issue.bitOffset = next.bitOffset;
                        result.issues.push_back(issue);
                    } else if (f.bitLength > 0 && f.BitEnd() < next.bitOffset) {
                        PacketValidationIssue issue;
                        issue.severity = PacketValidationSeverity::Warning;
                        issue.message = "Gap of " +
                                        std::to_string(next.bitOffset - f.BitEnd()) +
                                        " bits after '" + f.name + "'";
                        issue.layerId = layer.id;
                        issue.fieldId = f.id.empty() ? f.name : f.id;
                        issue.bitOffset = f.BitEnd();
                        result.issues.push_back(issue);
                    }
                }
            }
        }

        for (const auto& kv : idCounts) {
            if (kv.second > 1) {
                PacketValidationIssue issue;
                issue.severity = PacketValidationSeverity::Error;
                issue.message = "Duplicate field id '" + kv.first + "'";
                issue.fieldId = kv.first;
                result.issues.push_back(issue);
            }
        }

        // V3 total length.
        if (expectedTotalBits > 0) {
            const uint64_t actual = model.TotalBits();
            if (actual != expectedTotalBits) {
                PacketValidationIssue issue;
                issue.severity = PacketValidationSeverity::Error;
                issue.message = "Total length is " + std::to_string(actual) +
                                " bits, expected " + std::to_string(expectedTotalBits);
                result.issues.push_back(issue);
            }
        }

        return result;
    }

    size_t FillPacketModelGaps(PacketModel& model) {
        size_t added = 0;
        for (auto& layer : model.layers) {
            std::vector<PacketField> filled;
            std::vector<PacketField> sorted = layer.fields;
            std::sort(sorted.begin(), sorted.end(),
                      [](const PacketField& a, const PacketField& b) {
                          return a.bitOffset < b.bitOffset;
                      });

            uint64_t cursor = 0;
            for (const auto& f : sorted) {
                if (f.bitOffset > cursor) {
                    PacketField gap("", f.bitOffset - cursor, PacketFieldKind::Reserved);
                    gap.bitOffset = cursor;
                    filled.push_back(gap);
                    ++added;
                }
                filled.push_back(f);
                cursor = std::max(cursor, f.BitEnd());
            }
            layer.fields = filled;
        }
        return added;
    }

} // namespace UltraCanvas
