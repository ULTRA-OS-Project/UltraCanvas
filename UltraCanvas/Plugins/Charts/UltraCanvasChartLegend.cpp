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

        if (!visible || (entries.empty() && !HasCustomArea()) || ctx == nullptr) {
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

        const bool vertical = IsVerticalFlow();
        const double maxContentW = std::max(0.0, availableArea.width - 2.0 * style.paddingX);

        // Flow the rows (G6). Vertical: one entry per row unless it does not
        // fit the height, in which case we add a column. Horizontal: pack
        // entries per row until the width is exhausted.
        std::vector<std::vector<size_t>> rows;
        double contentW = 0.0, contentH = 0.0;

        if (vertical) {
            for (size_t i = 0; i < measured.size(); ++i) {
                rows.push_back({i});
                contentW = std::max(contentW, measured[i].w);
                contentH += measured[i].h;
                if (i + 1 < measured.size()) contentH += style.entrySpacingY;
            }
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

        // Place the box.
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

        double cursorY = by + style.paddingY;
        if (!title.empty()) {
            layout.titleRect = Rect2Dd(bx + style.paddingX, cursorY, contentW, titleH);
            cursorY += titleH + style.titleGap;
        }

        // Place the rows.
        for (const auto& row : rows) {
            double rowW = 0.0;
            for (size_t k = 0; k < row.size(); ++k) {
                rowW += measured[row[k]].w;
                if (k + 1 < row.size()) rowW += style.entrySpacingX;
            }

            double cursorX = bx + style.paddingX;
            if (!vertical) {
                // Centre horizontal rows inside the content width.
                cursorX += (contentW - rowW) / 2.0;
            }

            double thisRowH = 0.0;
            for (size_t idx : row) {
                const double w = measured[idx].w;
                const double h = vertical ? measured[idx].h : rowHeight;
                thisRowH = std::max(thisRowH, h);

                // The overflow row has no swatch.
                const bool isOverflow = (layout.overflowCount > 0 && idx == shown);
                ChartLegendItemRect item;
                item.entryIndex = idx;
                item.bounds = Rect2Dd(cursorX, cursorY, w, h);
                if (isOverflow) {
                    item.swatchRect = Rect2Dd(cursorX, cursorY, 0, 0);
                } else {
                    item.swatchRect = Rect2Dd(
                            cursorX,
                            cursorY + (h - style.swatchHeight) / 2.0,
                            style.swatchWidth, style.swatchHeight);
                }
                layout.items.push_back(item);
                cursorX += w + style.entrySpacingX;
            }
            cursorY += thisRowH + style.entrySpacingY;
        }

        if (HasCustomArea()) {
            layout.customRect = Rect2Dd(bx + style.paddingX, cursorY,
                                        std::min(contentW, customAreaSize.width),
                                        customAreaSize.height);
        }

        layout.valid = true;
        return layout;
    }

    Rect2Dd ChartLegend::RemainingArea(const Rect2Dd& availableArea) const {
        if (!visible || (entries.empty() && !HasCustomArea()) || IsInset() ||
            !layout.valid) {
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
        if (!visible || (entries.empty() && !HasCustomArea()) || ctx == nullptr) return;

        Measure(ctx, availableArea);
        if (layout.items.empty() && !HasCustomArea()) return;

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

// =============================================================================
// INTERACTION
// =============================================================================

    size_t ChartLegend::HitTest(const Point2Dd& point) const {
        if (!layout.valid) return SIZE_MAX;
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
