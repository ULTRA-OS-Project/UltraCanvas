// Plugins/Charts/UltraCanvasChartLegend.cpp
// Shared legend component implementation.
// Version: 1.1.0
// Last Modified: 2026-08-20
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasChartLegend.h"
#include "UltraCanvasRenderContext.h"
#include <algorithm>
#include <cstdio>
#include <cmath>

namespace UltraCanvas {

// =============================================================================
// STYLE PRESETS
// =============================================================================

    ChartLegendStyle ChartLegendStyle::Light() {
        return ChartLegendStyle();
    }

    ChartLegendStyle ChartLegendStyle::Dark() {
        ChartLegendStyle s;
        s.textColor = Color(225, 225, 225, 255);
        s.disabledTextColor = Color(110, 110, 110, 255);
        s.titleColor = Color(245, 245, 245, 255);
        s.swatchBorderColor = Color(150, 150, 150, 255);
        s.backgroundColor = Color(35, 35, 40, 230);
        s.borderColor = Color(80, 80, 88, 255);
        return s;
    }

    ChartLegendStyle ChartLegendStyle::Monochrome() {
        ChartLegendStyle s;
        s.textColor = Colors::Black;
        s.disabledTextColor = Color(150, 150, 150, 255);
        s.titleColor = Colors::Black;
        s.swatchBorderColor = Colors::Black;
        s.drawSwatchBorder = true;
        s.drawBorder = true;
        s.borderColor = Colors::Black;
        s.cornerRadius = 0.0f;
        return s;
    }

// =============================================================================
// INTERVAL FORMATTING
// =============================================================================

    static std::string FormatNumber(double v, int decimals) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
        return std::string(buf);
    }

    static std::string DefaultLegendNumber(double v) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%g", v);
        return std::string(buf);
    }

    std::string FormatLegendInterval(double low, double high, bool openHigh,
                                     LegendIntervalFormat format, int decimals) {
        const std::string a = FormatNumber(low, decimals);
        if (openHigh) return "> " + a;
        const std::string b = FormatNumber(high, decimals);
        switch (format) {
            case LegendIntervalFormat::Dash:   return a + " - " + b;
            case LegendIntervalFormat::ToWord: return a + " to " + b;
            case LegendIntervalFormat::Brackets:
            default:                           return "[" + a + ", " + b + "]";
        }
    }

// =============================================================================
// CONTENT
// =============================================================================

    void ChartLegend::AddEntry(const ChartLegendEntry& entry) {
        entries.push_back(entry);
        Invalidate();
    }

    void ChartLegend::SetEntries(const std::vector<ChartLegendEntry>& list) {
        entries = list;
        Invalidate();
    }

    void ChartLegend::ClearEntries() {
        entries.clear();
        highlightedIndex = SIZE_MAX;
        Invalidate();
    }

    void ChartLegend::AddIntervalEntry(double low, double high, const Color& color) {
        ChartLegendEntry e;
        e.color = color;
        e.isInterval = true;
        e.intervalLow = low;
        e.intervalHigh = high;
        entries.push_back(e);
        Invalidate();
    }

    void ChartLegend::AddOpenIntervalEntry(double low, const Color& color) {
        ChartLegendEntry e;
        e.color = color;
        e.isInterval = true;
        e.intervalLow = low;
        e.openHigh = true;
        entries.push_back(e);
        Invalidate();
    }

    std::string ChartLegend::EntryText(const ChartLegendEntry& entry) const {
        std::string text = entry.label;
        if (entry.isInterval) {
            text = FormatLegendInterval(entry.intervalLow, entry.intervalHigh,
                                        entry.openHigh, intervalFormat, intervalDecimals);
        }
        if (labelFormatter) {
            ChartLegendEntry copy = entry;
            copy.label = text;
            text = labelFormatter(copy);
        }
        if (!entry.valueText.empty()) {
            text += "  " + entry.valueText;
        }
        return text;
    }

// =============================================================================
// LAYOUT
// =============================================================================

    bool ChartLegend::HasContent() const {
        switch (mode) {
            case ChartLegendMode::ColorBar:   return true;
            case ChartLegendMode::SizeLegend: return sizeScale.sampleCount > 0;
            case ChartLegendMode::Discrete:
            default:                          return !entries.empty() || HasCustomArea();
        }
    }

    bool ChartLegend::IsInset() const {
        switch (position) {
            case ChartLegendPosition::InsetTopLeft:
            case ChartLegendPosition::InsetTopRight:
            case ChartLegendPosition::InsetBottomLeft:
            case ChartLegendPosition::InsetBottomRight:
                return true;
            default:
                return false;
        }
    }

    bool ChartLegend::IsVerticalFlow() const {
        if (orientation == LegendOrientation::Horizontal) return false;
        if (orientation == LegendOrientation::Vertical)   return true;

        // Auto: side placements stack vertically, top/bottom flow horizontally,
        // insets stack vertically (they are narrow by nature).
        switch (position) {
            case ChartLegendPosition::TopStart:
            case ChartLegendPosition::TopCenter:
            case ChartLegendPosition::TopEnd:
            case ChartLegendPosition::BottomStart:
            case ChartLegendPosition::BottomCenter:
            case ChartLegendPosition::BottomEnd:
                return false;
            default:
                return true;
        }
    }

    void ChartLegend::PlaceBox(const Rect2Dd& availableArea, double boxW, double boxH) {
        double bx = availableArea.x;
        double by = availableArea.y;
        switch (position) {
            case ChartLegendPosition::TopStart:
                bx = availableArea.x; by = availableArea.y; break;
            case ChartLegendPosition::TopCenter:
                bx = availableArea.x + (availableArea.width - boxW) / 2.0;
                by = availableArea.y; break;
            case ChartLegendPosition::TopEnd:
                bx = availableArea.x + availableArea.width - boxW;
                by = availableArea.y; break;
            case ChartLegendPosition::BottomStart:
                bx = availableArea.x;
                by = availableArea.y + availableArea.height - boxH; break;
            case ChartLegendPosition::BottomCenter:
                bx = availableArea.x + (availableArea.width - boxW) / 2.0;
                by = availableArea.y + availableArea.height - boxH; break;
            case ChartLegendPosition::BottomEnd:
                bx = availableArea.x + availableArea.width - boxW;
                by = availableArea.y + availableArea.height - boxH; break;
            case ChartLegendPosition::LeftStart:
                bx = availableArea.x; by = availableArea.y; break;
            case ChartLegendPosition::LeftCenter:
                bx = availableArea.x;
                by = availableArea.y + (availableArea.height - boxH) / 2.0; break;
            case ChartLegendPosition::LeftEnd:
                bx = availableArea.x;
                by = availableArea.y + availableArea.height - boxH; break;
            case ChartLegendPosition::RightStart:
                bx = availableArea.x + availableArea.width - boxW;
                by = availableArea.y; break;
            case ChartLegendPosition::RightCenter:
                bx = availableArea.x + availableArea.width - boxW;
                by = availableArea.y + (availableArea.height - boxH) / 2.0; break;
            case ChartLegendPosition::RightEnd:
                bx = availableArea.x + availableArea.width - boxW;
                by = availableArea.y + availableArea.height - boxH; break;
            case ChartLegendPosition::InsetTopLeft:
                bx = availableArea.x + style.hostGap;
                by = availableArea.y + style.hostGap; break;
            case ChartLegendPosition::InsetTopRight:
                bx = availableArea.x + availableArea.width - boxW - style.hostGap;
                by = availableArea.y + style.hostGap; break;
            case ChartLegendPosition::InsetBottomLeft:
                bx = availableArea.x + style.hostGap;
                by = availableArea.y + availableArea.height - boxH - style.hostGap; break;
            case ChartLegendPosition::InsetBottomRight:
                bx = availableArea.x + availableArea.width - boxW - style.hostGap;
                by = availableArea.y + availableArea.height - boxH - style.hostGap; break;
        }


        layout.box = Rect2Dd(bx, by, boxW, boxH);
    }

    const ChartLegendLayout& ChartLegend::Measure(IRenderContext* ctx,
                                                  const Rect2Dd& availableArea) {
        const bool areaChanged =
                std::fabs(lastArea.x - availableArea.x) > 0.01 ||
                std::fabs(lastArea.y - availableArea.y) > 0.01 ||
                std::fabs(lastArea.width - availableArea.width) > 0.01 ||
                std::fabs(lastArea.height - availableArea.height) > 0.01;

        if (layout.valid && !areaChanged) return layout;

        lastArea = availableArea;
        layout = ChartLegendLayout();

        if (!visible || !HasContent() || ctx == nullptr) {
            layout.box = Rect2Dd(availableArea.x, availableArea.y, 0, 0);
            layout.valid = true;
            return layout;
        }

        ctx->SetFontFamily(style.fontFamily);
        ctx->SetFontSize(style.fontSize);

        // How many entries do we actually render?
        const size_t total = entries.size();
        size_t shown = total;
        if (maxEntries > 0 && total > maxEntries) {
            shown = maxEntries;
            layout.overflowCount = total - maxEntries;
        }
        layout.visibleCount = shown;

        // Measure each entry row.
        struct Measured { double w, h; };
        std::vector<Measured> measured;
        measured.reserve(shown + 1);

        double rowHeight = style.swatchHeight;
        for (size_t i = 0; i < shown; ++i) {
            const std::string text = EntryText(entries[i]);
            Size2Di ts = ctx->GetTextLineDimensions(text);
            const double w = style.swatchWidth + style.swatchTextGap + ts.width;
            const double h = std::max<double>(style.swatchHeight, ts.height);
            measured.push_back({w, h});
            rowHeight = std::max(rowHeight, h);
        }
        if (layout.overflowCount > 0) {
            const std::string more = "...and " + std::to_string(layout.overflowCount) + " more";
            Size2Di ts = ctx->GetTextLineDimensions(more);
            measured.push_back({static_cast<double>(ts.width), static_cast<double>(ts.height)});
            rowHeight = std::max<double>(rowHeight, ts.height);
        }

        // Title.
        double titleW = 0.0, titleH = 0.0;
        if (!title.empty()) {
            ctx->SetFontSize(style.titleFontSize);
            Size2Di ts = ctx->GetTextLineDimensions(title);
            titleW = ts.width;
            titleH = ts.height;
            ctx->SetFontSize(style.fontSize);
        }

        // The continuous modes lay themselves out and ignore the entry list.
        if (mode == ChartLegendMode::ColorBar) {
            MeasureColorBar(ctx, availableArea, titleH);
            layout.valid = true;
            return layout;
        }
        if (mode == ChartLegendMode::SizeLegend) {
            MeasureSizeScale(ctx, availableArea, titleH);
            layout.valid = true;
            return layout;
        }

        const bool vertical = IsVerticalFlow();
        const double maxContentW = std::max(0.0, availableArea.width - 2.0 * style.paddingX);

        // Flow the rows (G6). Vertical: one entry per row, wrapped into
        // further columns when the available height is exhausted, so a tall
        // legend on a side placement grows sideways instead of silently
        // clipping. Horizontal: pack entries per row until the width is
        // exhausted.
        std::vector<std::vector<size_t>> rows;       // horizontal flow
        std::vector<std::vector<size_t>> columns;    // vertical flow
        std::vector<double> columnWidths;
        double contentW = 0.0, contentH = 0.0;
        double verticalEntriesH = 0.0;

        if (vertical) {
            double maxContentH = availableArea.height - 2.0 * style.paddingY;
            if (!title.empty()) maxContentH -= titleH + style.titleGap;
            if (HasCustomArea()) {
                maxContentH -= customAreaSize.height + style.entrySpacingY;
            }
            maxContentH = std::max(maxContentH, rowHeight);  // >= one per column

            std::vector<size_t> column;
            double columnH = 0.0, columnW = 0.0;
            for (size_t i = 0; i < measured.size(); ++i) {
                const double add = column.empty()
                                       ? measured[i].h
                                       : style.entrySpacingY + measured[i].h;
                if (!column.empty() && columnH + add > maxContentH) {
                    columns.push_back(column);
                    columnWidths.push_back(columnW);
                    verticalEntriesH = std::max(verticalEntriesH, columnH);
                    column.clear();
                    column.push_back(i);
                    columnH = measured[i].h;
                    columnW = measured[i].w;
                } else {
                    column.push_back(i);
                    columnH += add;
                    columnW = std::max(columnW, measured[i].w);
                }
            }
            if (!column.empty()) {
                columns.push_back(column);
                columnWidths.push_back(columnW);
                verticalEntriesH = std::max(verticalEntriesH, columnH);
            }
            for (size_t c = 0; c < columnWidths.size(); ++c) {
                contentW += columnWidths[c];
                if (c + 1 < columnWidths.size()) contentW += style.entrySpacingX;
            }
            contentH = verticalEntriesH;
        } else {
            std::vector<size_t> current;
            double currentW = 0.0;
            for (size_t i = 0; i < measured.size(); ++i) {
                const double add = current.empty() ? measured[i].w
                                                   : style.entrySpacingX + measured[i].w;
                if (!current.empty() && currentW + add > maxContentW) {
                    rows.push_back(current);
                    contentW = std::max(contentW, currentW);
                    contentH += rowHeight + style.entrySpacingY;
                    current.clear();
                    currentW = measured[i].w;
                    current.push_back(i);
                } else {
                    currentW += add;
                    current.push_back(i);
                }
            }
            if (!current.empty()) {
                rows.push_back(current);
                contentW = std::max(contentW, currentW);
                contentH += rowHeight;
            }
        }

        contentW = std::max(contentW, titleW);
        if (HasCustomArea()) {
            contentW = std::max(contentW, customAreaSize.width);
            if (contentH > 0.0) contentH += style.entrySpacingY;
            contentH += customAreaSize.height;
        }
        double boxW = contentW + 2.0 * style.paddingX;
        double boxH = contentH + 2.0 * style.paddingY;
        if (!title.empty()) boxH += titleH + style.titleGap;

        boxW = std::min(boxW, availableArea.width);
        boxH = std::min(boxH, availableArea.height);

        PlaceBox(availableArea, boxW, boxH);
        const double bx = layout.box.x;
        const double by = layout.box.y;

        double cursorY = by + style.paddingY;
        if (!title.empty()) {
            layout.titleRect = Rect2Dd(bx + style.paddingX, cursorY, contentW, titleH);
            cursorY += titleH + style.titleGap;
        }

        // Place the entries: columns for vertical flow, rows for horizontal.
        auto placeItem = [&](size_t idx, double x, double y, double w, double h) {
            // The overflow row has no swatch.
            const bool isOverflow = (layout.overflowCount > 0 && idx == shown);
            ChartLegendItemRect item;
            item.entryIndex = idx;
            item.bounds = Rect2Dd(x, y, w, h);
            if (isOverflow) {
                item.swatchRect = Rect2Dd(x, y, 0, 0);
            } else {
                item.swatchRect = Rect2Dd(x, y + (h - style.swatchHeight) / 2.0,
                                          style.swatchWidth, style.swatchHeight);
            }
            layout.items.push_back(item);
        };

        if (vertical) {
            double columnX = bx + style.paddingX;
            for (size_t c = 0; c < columns.size(); ++c) {
                double y = cursorY;
                for (size_t idx : columns[c]) {
                    placeItem(idx, columnX, y, columnWidths[c], measured[idx].h);
                    y += measured[idx].h + style.entrySpacingY;
                }
                columnX += columnWidths[c] + style.entrySpacingX;
            }
            cursorY += verticalEntriesH + style.entrySpacingY;
        } else {
            for (const auto& row : rows) {
                double rowW = 0.0;
                for (size_t k = 0; k < row.size(); ++k) {
                    rowW += measured[row[k]].w;
                    if (k + 1 < row.size()) rowW += style.entrySpacingX;
                }

                // Centre horizontal rows inside the content width.
                double cursorX = bx + style.paddingX + (contentW - rowW) / 2.0;

                double thisRowH = 0.0;
                for (size_t idx : row) {
                    thisRowH = std::max(thisRowH, rowHeight);
                    placeItem(idx, cursorX, cursorY, measured[idx].w, rowHeight);
                    cursorX += measured[idx].w + style.entrySpacingX;
                }
                cursorY += thisRowH + style.entrySpacingY;
            }
        }

        if (HasCustomArea()) {
            layout.customRect = Rect2Dd(bx + style.paddingX, cursorY,
                                        std::min(contentW, customAreaSize.width),
                                        customAreaSize.height);
        }

        layout.valid = true;
        return layout;
    }

    void ChartLegend::MeasureColorBar(IRenderContext* ctx, const Rect2Dd& availableArea,
                                      double titleH) {
        const bool vertical = IsVerticalFlow();
        const auto format = colorBar.formatter ? colorBar.formatter
                                               : DefaultLegendNumber;
        const int ticks = std::max(2, colorBar.tickCount);

        double maxLabelW = 0.0, maxLabelH = 0.0;
        for (int i = 0; i < ticks; ++i) {
            const double v = colorBar.minValue +
                             (colorBar.maxValue - colorBar.minValue) * i / (ticks - 1);
            const Size2Di ts = ctx->GetTextLineDimensions(format(v));
            maxLabelW = std::max(maxLabelW, static_cast<double>(ts.width));
            maxLabelH = std::max(maxLabelH, static_cast<double>(ts.height));
        }

        const double labelGap = 4.0;
        double barLen = colorBar.barLength;
        if (vertical) {
            double maxLen = availableArea.height - 2.0 * style.paddingY;
            if (titleH > 0.0) maxLen -= titleH + style.titleGap;
            barLen = std::max(24.0, std::min(barLen, maxLen));
        } else {
            barLen = std::max(24.0,
                              std::min(barLen, availableArea.width - 2.0 * style.paddingX));
        }

        const double contentW = vertical
                                    ? colorBar.barThickness + labelGap + maxLabelW
                                    : barLen;
        const double contentH = vertical
                                    ? barLen
                                    : colorBar.barThickness + labelGap + maxLabelH;

        double boxW = contentW + 2.0 * style.paddingX;
        double boxH = contentH + 2.0 * style.paddingY;
        if (titleH > 0.0) boxH += titleH + style.titleGap;
        boxW = std::min(boxW, availableArea.width);
        boxH = std::min(boxH, availableArea.height);

        PlaceBox(availableArea, boxW, boxH);

        double cursorY = layout.box.y + style.paddingY;
        if (titleH > 0.0) {
            layout.titleRect = Rect2Dd(layout.box.x + style.paddingX, cursorY,
                                       contentW, titleH);
            cursorY += titleH + style.titleGap;
        }
        layout.barRect = vertical
                             ? Rect2Dd(layout.box.x + style.paddingX, cursorY,
                                       colorBar.barThickness, barLen)
                             : Rect2Dd(layout.box.x + style.paddingX, cursorY,
                                       barLen, colorBar.barThickness);
    }

    void ChartLegend::MeasureSizeScale(IRenderContext* ctx, const Rect2Dd& availableArea,
                                       double titleH) {
        const auto format = sizeScale.formatter ? sizeScale.formatter
                                                : DefaultLegendNumber;
        const int samples = std::max(1, sizeScale.sampleCount);

        layout.sizeRadii.clear();
        layout.sizeLabels.clear();
        double maxLabelW = 0.0, maxLabelH = 0.0;
        for (int i = 0; i < samples; ++i) {
            // Largest first, the way printed size keys read.
            const double t = (samples == 1)
                                 ? 1.0
                                 : 1.0 - static_cast<double>(i) / (samples - 1);
            const double v = sizeScale.minValue +
                             (sizeScale.maxValue - sizeScale.minValue) * t;
            layout.sizeRadii.push_back(
                sizeScale.minRadius + (sizeScale.maxRadius - sizeScale.minRadius) * t);
            layout.sizeLabels.push_back(format(v));
            const Size2Di ts = ctx->GetTextLineDimensions(layout.sizeLabels.back());
            maxLabelW = std::max(maxLabelW, static_cast<double>(ts.width));
            maxLabelH = std::max(maxLabelH, static_cast<double>(ts.height));
        }

        const double diameterColumn = 2.0 * sizeScale.maxRadius;
        const double contentW = diameterColumn + style.swatchTextGap + maxLabelW;
        double contentH = 0.0;
        std::vector<double> rowHeights;
        for (int i = 0; i < samples; ++i) {
            const double h = std::max(2.0 * layout.sizeRadii[i], maxLabelH);
            rowHeights.push_back(h);
            contentH += h;
            if (i + 1 < samples) contentH += style.entrySpacingY;
        }

        double boxW = contentW + 2.0 * style.paddingX;
        double boxH = contentH + 2.0 * style.paddingY;
        if (titleH > 0.0) boxH += titleH + style.titleGap;
        boxW = std::min(boxW, availableArea.width);
        boxH = std::min(boxH, availableArea.height);

        PlaceBox(availableArea, boxW, boxH);

        double cursorY = layout.box.y + style.paddingY;
        if (titleH > 0.0) {
            layout.titleRect = Rect2Dd(layout.box.x + style.paddingX, cursorY,
                                       contentW, titleH);
            cursorY += titleH + style.titleGap;
        }
        for (int i = 0; i < samples; ++i) {
            ChartLegendItemRect item;
            item.entryIndex = static_cast<size_t>(i);
            item.bounds = Rect2Dd(layout.box.x + style.paddingX, cursorY,
                                  contentW, rowHeights[i]);
            item.swatchRect = Rect2Dd(layout.box.x + style.paddingX, cursorY,
                                      diameterColumn, rowHeights[i]);
            layout.items.push_back(item);
            cursorY += rowHeights[i] + style.entrySpacingY;
        }
    }

    Rect2Dd ChartLegend::RemainingArea(const Rect2Dd& availableArea) const {
        if (!visible || !HasContent() || IsInset() || !layout.valid) {
            return availableArea;
        }

        const double consumedW = layout.box.width + style.hostGap;
        const double consumedH = layout.box.height + style.hostGap;

        switch (position) {
            case ChartLegendPosition::TopStart:
            case ChartLegendPosition::TopCenter:
            case ChartLegendPosition::TopEnd:
                return Rect2Dd(availableArea.x, availableArea.y + consumedH,
                               availableArea.width,
                               std::max(0.0, availableArea.height - consumedH));
            case ChartLegendPosition::BottomStart:
            case ChartLegendPosition::BottomCenter:
            case ChartLegendPosition::BottomEnd:
                return Rect2Dd(availableArea.x, availableArea.y,
                               availableArea.width,
                               std::max(0.0, availableArea.height - consumedH));
            case ChartLegendPosition::LeftStart:
            case ChartLegendPosition::LeftCenter:
            case ChartLegendPosition::LeftEnd:
                return Rect2Dd(availableArea.x + consumedW, availableArea.y,
                               std::max(0.0, availableArea.width - consumedW),
                               availableArea.height);
            default:
                return Rect2Dd(availableArea.x, availableArea.y,
                               std::max(0.0, availableArea.width - consumedW),
                               availableArea.height);
        }
    }

// =============================================================================
// RENDERING
// =============================================================================

    void ChartLegend::DrawSwatch(IRenderContext* ctx, const ChartLegendEntry& entry,
                                 const Rect2Dd& rect) const {
        if (rect.width <= 0.0 || rect.height <= 0.0) return;

        Color fill = entry.color;
        if (!entry.enabled) {
            fill.a = static_cast<uint8_t>(fill.a / 3);
        }

        switch (entry.swatch) {
            case LegendSwatch::Circle: {
                const double r = std::min(rect.width, rect.height) / 2.0;
                const Point2Dd c(rect.x + rect.width / 2.0, rect.y + rect.height / 2.0);
                ctx->SetFillPaint(fill);
                ctx->FillCircle(c, r);
                if (style.drawSwatchBorder) {
                    ctx->SetStrokePaint(style.swatchBorderColor);
                    ctx->SetStrokeWidth(style.swatchBorderWidth);
                    ctx->DrawCircle(c, r);
                }
                break;
            }
            case LegendSwatch::Ring: {
                // Stroked rather than filled, so an ordinal scale keeps a step
                // that survives greyscale printing and colour-blind readers.
                const double r = std::min(rect.width, rect.height) / 2.0 - 1.0;
                if (r <= 0.0) break;
                const Point2Dd c(rect.x + rect.width / 2.0, rect.y + rect.height / 2.0);
                ctx->SetStrokePaint(fill);
                ctx->SetStrokeWidth(2.0);
                ctx->DrawCircle(c, r);
                break;
            }
            case LegendSwatch::Glyph: {
                if (entry.glyph.empty()) break;
                ctx->SetFontSize(std::max(8.0, std::min(rect.width, rect.height)));
                ctx->SetTextPaint(fill);
                Size2Di s = ctx->GetTextLineDimensions(entry.glyph);
                ctx->DrawText(entry.glyph,
                              Point2Dd(rect.x + (rect.width - s.width) / 2.0,
                                       rect.y + (rect.height - s.height) / 2.0));
                ctx->SetFontSize(style.fontSize);
                break;
            }
            case LegendSwatch::Line:
            case LegendSwatch::DashedLine: {
                const double cy = rect.y + rect.height / 2.0;
                if (entry.swatch == LegendSwatch::DashedLine) {
                    UCDashPattern dash;
                    dash.dashes = {3.0, 2.0};
                    ctx->SetLineDash(dash);
                }
                ctx->SetStrokePaint(fill);
                ctx->SetStrokeWidth(2.0);
                ctx->DrawLine(Point2Dd(rect.x, cy), Point2Dd(rect.x + rect.width, cy));
                if (entry.swatch == LegendSwatch::DashedLine) {
                    ctx->SetLineDash(UCDashPattern());
                }
                break;
            }
            case LegendSwatch::Marker: {
                const double cx = rect.x + rect.width / 2.0;
                const double cy = rect.y + rect.height / 2.0;
                const double r = std::min(rect.width, rect.height) / 2.0;
                std::vector<Point2Dd> diamond = {
                        Point2Dd(cx, cy - r), Point2Dd(cx + r, cy),
                        Point2Dd(cx, cy + r), Point2Dd(cx - r, cy)
                };
                ctx->SetFillPaint(fill);
                ctx->FillLinePath(diamond);
                break;
            }
            case LegendSwatch::Gradient: {
                Color light = fill;
                light.r = static_cast<uint8_t>(std::min(255, light.r + 70));
                light.g = static_cast<uint8_t>(std::min(255, light.g + 70));
                light.b = static_cast<uint8_t>(std::min(255, light.b + 70));
                auto gradient = ctx->CreateLinearGradientPattern(
                        rect.x, rect.y, rect.x, rect.y + rect.height,
                        {GradientStop(0.0, light), GradientStop(1.0, fill)});
                if (gradient) ctx->SetFillPaint(gradient);
                else ctx->SetFillPaint(fill);
                ctx->FillRectangle(rect);
                if (style.drawSwatchBorder) {
                    ctx->SetStrokePaint(style.swatchBorderColor);
                    ctx->SetStrokeWidth(style.swatchBorderWidth);
                    ctx->DrawRectangle(rect);
                }
                break;
            }
            case LegendSwatch::Outline: {
                Color ghost = fill;
                ghost.a = 36;
                ctx->SetFillPaint(ghost);
                ctx->FillRectangle(rect);
                ctx->SetStrokePaint(fill);
                ctx->SetStrokeWidth(1.5f);
                ctx->DrawRectangle(rect);
                break;
            }
            case LegendSwatch::Hatched: {
                Color background = fill;
                background.r = static_cast<uint8_t>(std::min(255, background.r + 90));
                background.g = static_cast<uint8_t>(std::min(255, background.g + 90));
                background.b = static_cast<uint8_t>(std::min(255, background.b + 90));
                ctx->SetFillPaint(background);
                ctx->FillRectangle(rect);
                ctx->PushState();
                ctx->ClipRect(rect);
                ctx->SetStrokePaint(fill);
                ctx->SetStrokeWidth(1.0f);
                for (double offset = 0.0; offset < rect.width + rect.height; offset += 4.0) {
                    ctx->DrawLine(Point2Dd(rect.x + offset, rect.y),
                                  Point2Dd(rect.x + offset - rect.height,
                                           rect.y + rect.height));
                }
                ctx->PopState();
                break;
            }
            case LegendSwatch::Image: {
                if (!entry.imagePath.empty()) {
                    ctx->PushState();
                    ctx->ClipRect(rect);
                    ctx->DrawImage(entry.imagePath, rect, ImageFitMode::Cover);
                    ctx->PopState();
                    ctx->SetStrokePaint(style.swatchBorderColor);
                    ctx->SetStrokeWidth(style.swatchBorderWidth);
                    ctx->DrawRectangle(rect);
                } else {
                    ctx->SetFillPaint(fill);
                    ctx->FillRectangle(rect);
                }
                break;
            }
            case LegendSwatch::Square:
            default: {
                ctx->SetFillPaint(fill);
                ctx->FillRectangle(rect);
                if (style.drawSwatchBorder) {
                    ctx->SetStrokePaint(style.swatchBorderColor);
                    ctx->SetStrokeWidth(style.swatchBorderWidth);
                    ctx->DrawRectangle(rect);
                }
                break;
            }
        }
    }

    void ChartLegend::Render(IRenderContext* ctx, const Rect2Dd& availableArea) {
        if (!visible || !HasContent() || ctx == nullptr) return;

        Measure(ctx, availableArea);
        if (mode == ChartLegendMode::Discrete && layout.items.empty() &&
            !HasCustomArea()) return;

        ctx->PushState();

        if (style.drawBackground) {
            ctx->SetFillPaint(style.backgroundColor);
            if (style.cornerRadius > 0.0f) {
                ctx->FillRoundedRectangle(layout.box, style.cornerRadius);
            } else {
                ctx->FillRectangle(layout.box);
            }
        }
        if (style.drawBorder) {
            ctx->SetStrokePaint(style.borderColor);
            ctx->SetStrokeWidth(style.borderWidth);
            if (style.cornerRadius > 0.0f) {
                ctx->DrawRoundedRectangle(layout.box, style.cornerRadius);
            } else {
                ctx->DrawRectangle(layout.box);
            }
        }

        ctx->SetFontFamily(style.fontFamily);

        if (!title.empty()) {
            ctx->SetFontSize(style.titleFontSize);
            ctx->SetTextPaint(style.titleColor);
            ctx->DrawText(title, Point2Dd(layout.titleRect.x, layout.titleRect.y));
        }

        ctx->SetFontSize(style.fontSize);

        if (mode == ChartLegendMode::ColorBar) {
            RenderColorBar(ctx);
            ctx->PopState();
            return;
        }
        if (mode == ChartLegendMode::SizeLegend) {
            RenderSizeScale(ctx);
            ctx->PopState();
            return;
        }

        for (const auto& item : layout.items) {
            const bool isOverflow = (layout.overflowCount > 0 &&
                                     item.entryIndex == layout.visibleCount);
            if (isOverflow) {
                ctx->SetTextPaint(style.disabledTextColor);
                const std::string more =
                        "...and " + std::to_string(layout.overflowCount) + " more";
                ctx->DrawText(more, Point2Dd(item.bounds.x, item.bounds.y));
                continue;
            }

            const ChartLegendEntry& entry = entries[item.entryIndex];
            DrawSwatch(ctx, entry, item.swatchRect);

            const std::string text = EntryText(entry);
            Size2Di ts = ctx->GetTextLineDimensions(text);
            const double textX = item.swatchRect.x + style.swatchWidth + style.swatchTextGap;
            const double textY = item.bounds.y + (item.bounds.height - ts.height) / 2.0;

            Color tc = entry.enabled ? style.textColor : style.disabledTextColor;
            if (entry.highlighted || item.entryIndex == highlightedIndex) {
                ctx->SetFontWeight(FontWeight::Bold);
            }
            ctx->SetTextPaint(tc);
            ctx->DrawText(text, Point2Dd(textX, textY));
            if (entry.highlighted || item.entryIndex == highlightedIndex) {
                ctx->SetFontWeight(FontWeight::Normal);
            }
        }

        if (HasCustomArea() && layout.customRect.width > 0.0) {
            ctx->PushState();
            ctx->ClipRect(layout.customRect);
            customDraw(ctx, layout.customRect);
            ctx->PopState();
        }

        ctx->PopState();
    }

    void ChartLegend::RenderColorBar(IRenderContext* ctx) const {
        const Rect2Dd& bar = layout.barRect;
        if (bar.width <= 0.0 || bar.height <= 0.0) return;
        const bool vertical = bar.height >= bar.width;
        const auto format = colorBar.formatter ? colorBar.formatter
                                               : DefaultLegendNumber;
        auto colorAt = [this](double t) {
            return SampleColormap(colorBar.colormap, colorBar.customColormap,
                                  t, colorBar.reverse);
        };

        // The ramp. minValue sits at the bottom (vertical) / left (horizontal).
        if (colorBar.quantizeLevels >= 2) {
            const int levels = colorBar.quantizeLevels;
            for (int k = 0; k < levels; ++k) {
                const double t0 = static_cast<double>(k) / levels;
                const double t1 = static_cast<double>(k + 1) / levels;
                const Rect2Dd band =
                    vertical ? Rect2Dd(bar.x, bar.Bottom() - t1 * bar.height,
                                       bar.width, (t1 - t0) * bar.height)
                             : Rect2Dd(bar.x + t0 * bar.width, bar.y,
                                       (t1 - t0) * bar.width, bar.height);
                ctx->SetFillPaint(colorAt((t0 + t1) / 2.0));
                ctx->FillRectangle(band);
            }
        } else {
            const int stops = 24;
            std::vector<GradientStop> gradientStops;
            gradientStops.reserve(stops + 1);
            for (int s = 0; s <= stops; ++s) {
                const double t = static_cast<double>(s) / stops;
                gradientStops.emplace_back(t, colorAt(t));
            }
            auto gradient =
                vertical ? ctx->CreateLinearGradientPattern(
                               bar.x, bar.Bottom(), bar.x, bar.y, gradientStops)
                         : ctx->CreateLinearGradientPattern(
                               bar.x, bar.y, bar.Right(), bar.y, gradientStops);
            if (gradient) {
                ctx->SetFillPaint(gradient);
                ctx->FillRectangle(bar);
            } else {
                // No gradient support: draw the ramp as thin slices.
                for (int s = 0; s < stops; ++s) {
                    const double t0 = static_cast<double>(s) / stops;
                    const double t1 = static_cast<double>(s + 1) / stops;
                    const Rect2Dd slice =
                        vertical ? Rect2Dd(bar.x, bar.Bottom() - t1 * bar.height,
                                           bar.width, (t1 - t0) * bar.height + 0.5)
                                 : Rect2Dd(bar.x + t0 * bar.width, bar.y,
                                           (t1 - t0) * bar.width + 0.5, bar.height);
                    ctx->SetFillPaint(colorAt((t0 + t1) / 2.0));
                    ctx->FillRectangle(slice);
                }
            }
        }
        ctx->SetStrokePaint(style.swatchBorderColor);
        ctx->SetStrokeWidth(style.swatchBorderWidth);
        ctx->DrawRectangle(bar);

        // Ticks and labels beside (vertical) / below (horizontal) the ramp.
        const int ticks = std::max(2, colorBar.tickCount);
        const double labelGap = 4.0;
        ctx->SetFontSize(style.fontSize);
        ctx->SetTextPaint(style.textColor);
        for (int i = 0; i < ticks; ++i) {
            const double frac = static_cast<double>(i) / (ticks - 1);
            const double v = colorBar.minValue +
                             (colorBar.maxValue - colorBar.minValue) * frac;
            const std::string label = format(v);
            const Size2Di ts = ctx->GetTextLineDimensions(label);
            ctx->SetStrokePaint(style.swatchBorderColor);
            ctx->SetStrokeWidth(1.0f);
            if (vertical) {
                const double y = bar.Bottom() - frac * bar.height;
                ctx->DrawLine(Point2Dd(bar.Right(), y),
                              Point2Dd(bar.Right() + 3.0, y));
                ctx->DrawText(label, Point2Dd(bar.Right() + labelGap,
                                              y - ts.height / 2.0));
            } else {
                const double x = bar.x + frac * bar.width;
                ctx->DrawLine(Point2Dd(x, bar.Bottom()),
                              Point2Dd(x, bar.Bottom() + 3.0));
                ctx->DrawText(label, Point2Dd(x - ts.width / 2.0,
                                              bar.Bottom() + labelGap));
            }
        }
    }

    void ChartLegend::RenderSizeScale(IRenderContext* ctx) const {
        ctx->SetFontSize(style.fontSize);
        for (size_t i = 0; i < layout.items.size() &&
                           i < layout.sizeRadii.size(); ++i) {
            const ChartLegendItemRect& item = layout.items[i];
            const double r = layout.sizeRadii[i];
            const Point2Dd centre(item.swatchRect.x + item.swatchRect.width / 2.0,
                                  item.swatchRect.y + item.swatchRect.height / 2.0);
            ctx->SetFillPaint(sizeScale.fillColor);
            ctx->FillCircle(centre, r);
            ctx->SetStrokePaint(sizeScale.strokeColor);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawCircle(centre, r);

            const std::string& label = layout.sizeLabels[i];
            const Size2Di ts = ctx->GetTextLineDimensions(label);
            ctx->SetTextPaint(style.textColor);
            ctx->DrawText(label,
                          Point2Dd(item.swatchRect.Right() + style.swatchTextGap,
                                   centre.y - ts.height / 2.0));
        }
    }

// =============================================================================
// INTERACTION
// =============================================================================

    size_t ChartLegend::HitTest(const Point2Dd& point) const {
        if (!layout.valid || mode != ChartLegendMode::Discrete) return SIZE_MAX;
        for (const auto& item : layout.items) {
            if (layout.overflowCount > 0 && item.entryIndex == layout.visibleCount) continue;
            if (point.x >= item.bounds.x && point.x <= item.bounds.x + item.bounds.width &&
                point.y >= item.bounds.y && point.y <= item.bounds.y + item.bounds.height) {
                return item.entryIndex;
            }
        }
        return SIZE_MAX;
    }

    void ChartLegend::SetHighlightedEntry(size_t index) {
        highlightedIndex = (index < entries.size()) ? index : SIZE_MAX;
    }

    void ChartLegend::ToggleEntryEnabled(size_t index) {
        if (index >= entries.size()) return;
        entries[index].enabled = !entries[index].enabled;
        Invalidate();
    }

} // namespace UltraCanvas
