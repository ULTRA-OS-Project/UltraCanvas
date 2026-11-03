// Plugins/Charts/Engine/Charts/UltraCanvasEngineDivergingBarChart.cpp
// Diverging bar chart (Likert / population pyramid / tornado / opposing bars)
// on the chart engine
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasEngineDivergingBarChart.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <numeric>
#include <sstream>

namespace UltraCanvas {

namespace {

// Hit region ids: row-major, room for the surplus split of every category.
int64_t SegmentRegionId(size_t row, size_t category, bool surplus) {
    const int64_t c = (category == SIZE_MAX) ? 0 : static_cast<int64_t>(category) + 1;
    return static_cast<int64_t>(row) * 1024 + c * 2 + (surplus ? 1 : 0);
}

// "20-24" -> [20, 25), "100+" -> [100, open). Integer ages only, read with
// from_chars - locale-free, unlike strtod.
bool ParseAgeBand(const std::string& label, double& lower, double& upper) {
    const char* p = label.data();
    const char* end = p + label.size();
    while (p < end && *p == ' ') ++p;
    int lo = 0;
    auto first = std::from_chars(p, end, lo);
    if (first.ec != std::errc()) return false;
    p = first.ptr;
    while (p < end && *p == ' ') ++p;
    if (p < end && *p == '+') {
        lower = lo;
        upper = -1.0;
        return true;
    }
    if (p >= end || *p != '-') return false;
    ++p;
    int hi = 0;
    auto second = std::from_chars(p, end, hi);
    if (second.ec != std::errc() || hi < lo) return false;
    lower = lo;
    upper = hi + 1.0;          // "25-29" covers 25 up to just under 30
    return true;
}

double Luminance(const Color& c) {
    return (0.299 * c.r + 0.587 * c.g + 0.114 * c.b) / 255.0;
}

} // namespace

UltraCanvasEngineDivergingBarChart::UltraCanvasEngineDivergingBarChart(
        const std::string& id, int x, int y, int w, int h)
    : UltraCanvasChartEngineElement(id, x, y, w, h) {
    SetProjectionKind(ChartProjectionKind::Horizontal);
    SetAnimateOnDataChange(true, 0.7f);
    SetLegendPosition(ChartLegendPosition::TopCenter);
    RefreshLimiters();
}

// =============================================================================
// DATA
// =============================================================================

void UltraCanvasEngineDivergingBarChart::SetCategories(
        const std::vector<DivergingBarCategory>& newCategories) {
    categories = newCategories;
    categoryEnabled.assign(categories.size(), true);
    RefreshLegend();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineDivergingBarChart::SetRows(const std::vector<DivergingBarRow>& newRows) {
    rows = newRows;
    RefreshLimiters();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineDivergingBarChart::AddRow(const std::string& label,
                                                const std::vector<double>& values) {
    auto existing = std::find_if(rows.begin(), rows.end(),
                                 [&label](const DivergingBarRow& r) { return r.label == label; });
    if (existing != rows.end()) {
        existing->values = values;
    } else {
        rows.emplace_back(label, values);
    }
    RefreshLimiters();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineDivergingBarChart::ClearRows() {
    rows.clear();
    RefreshLimiters();
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineDivergingBarChart::SetPopulationData(
        const std::vector<DivergingPopulationGroup>& groups,
        const std::string& leftName, const Color& leftColor,
        const std::string& rightName, const Color& rightColor) {
    categories = {DivergingBarCategory(leftName, leftColor, DivergingSide::Negative),
                  DivergingBarCategory(rightName, rightColor, DivergingSide::Positive)};
    categoryEnabled.assign(categories.size(), true);
    rows.clear();
    for (const DivergingPopulationGroup& g : groups) {
        rows.emplace_back(g.label, std::vector<double>{g.left, g.right});
    }
    style = DivergingBarStyle::PopulationPyramid;
    rowOrder = DivergingRowOrder::BottomUp;
    RefreshLegend();
    RefreshLimiters();
    MarkEngineDirty(ChartDirty::Data);
}

// =============================================================================
// STYLE
// =============================================================================

void UltraCanvasEngineDivergingBarChart::SetStyle(DivergingBarStyle newStyle) {
    style = newStyle;
    RefreshLegend();
    RefreshLimiters();
    MarkEngineDirty(ChartDirty::Data);     // the value range follows the style
}

void UltraCanvasEngineDivergingBarChart::SetRowOrder(DivergingRowOrder order) {
    rowOrder = order;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineDivergingBarChart::SetBarFill(double fill) {
    barFill = std::clamp(fill, 0.2, 1.0);
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineDivergingBarChart::SetSplitNeutral(bool split) {
    splitNeutral = split;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineDivergingBarChart::SetPercentOfRow(bool percent) {
    percentOfRow = percent;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineDivergingBarChart::SetSortByMagnitude(bool sort) {
    sortByMagnitude = sort;
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineDivergingBarChart::SetSideColors(const Color& negative,
                                                       const Color& positive) {
    negativeColor = negative;
    positiveColor = positive;
    RefreshLegend();
    RequestRedraw();
}

void UltraCanvasEngineDivergingBarChart::SetHighlightSurplus(bool highlight) {
    highlightSurplus = highlight;
    RefreshLegend();
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineDivergingBarChart::SetShowAverageLines(bool show) {
    showAverageLines = show;
    RefreshLegend();
    RefreshLimiters();
}

void UltraCanvasEngineDivergingBarChart::SetShowCenterLine(bool show) {
    showCenterLine = show;
    RefreshLimiters();
}

void UltraCanvasEngineDivergingBarChart::SetShowValueLabels(bool show) {
    showValues = show;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineDivergingBarChart::SetValueFormat(const std::string& prefix,
                                                        const std::string& suffix,
                                                        bool compact, int decimals) {
    valuePrefix = prefix;
    valueSuffix = suffix;
    compactValues = compact;
    valueDecimals = decimals;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineDivergingBarChart::SetValueLimit(double limit) {
    valueLimit = std::max(0.0, limit);
    MarkEngineDirty(ChartDirty::Data);
}

void UltraCanvasEngineDivergingBarChart::SetValueAxisTitle(const std::string& title) {
    valueAxisTitle = title;
    MarkEngineDirty(ChartDirty::Style);
}

void UltraCanvasEngineDivergingBarChart::OnChartThemeChanged() {
    RefreshLegend();
    RefreshLimiters();
}

void UltraCanvasEngineDivergingBarChart::OnLegendEntryToggled(size_t entryIndex, bool enabled) {
    // Only the category-keyed styles map legend entries onto categories; the
    // surplus and average-line keys of a pyramid are not toggles.
    if (style == DivergingBarStyle::Tornado || style == DivergingBarStyle::OpposingBars) return;
    const bool pair = IsPopulationPair() && highlightSurplus;
    const size_t category = pair ? entryIndex / 2 : entryIndex;
    if (category >= categoryEnabled.size()) return;
    if (pair && entryIndex % 2 == 1) return;            // a surplus key
    categoryEnabled[category] = enabled;
    RefreshLimiters();
    MarkEngineDirty(ChartDirty::Data);
}

// =============================================================================
// HELPERS
// =============================================================================

Color UltraCanvasEngineDivergingBarChart::CategoryColor(size_t category) const {
    if (category < categories.size() && categories[category].color.a > 0) {
        return categories[category].color;
    }
    return Palette().ColorAt(category, categories.size());
}

bool UltraCanvasEngineDivergingBarChart::CategoryActive(size_t category) const {
    return category < categoryEnabled.size() ? categoryEnabled[category] : true;
}

bool UltraCanvasEngineDivergingBarChart::IsPopulationPair() const {
    if (style != DivergingBarStyle::PopulationPyramid || categories.size() != 2) return false;
    return categories[0].side != DivergingSide::Positive &&
           categories[1].side == DivergingSide::Positive;
}

void UltraCanvasEngineDivergingBarChart::RefreshLegend() {
    std::vector<ChartLegendEntry> entries;
    if (style == DivergingBarStyle::Tornado || style == DivergingBarStyle::OpposingBars) {
        std::string left = "Negative side", right = "Positive side";
        for (const DivergingBarCategory& c : categories) {
            if (c.side == DivergingSide::Negative && left == "Negative side") left = c.name;
            if (c.side == DivergingSide::Positive && right == "Positive side") right = c.name;
        }
        if (style == DivergingBarStyle::Tornado) {
            entries.emplace_back("Net towards " + left, negativeColor);
            entries.emplace_back("Net towards " + right, positiveColor);
        } else {
            entries.emplace_back(left + " total", negativeColor);
            entries.emplace_back(right + " total", positiveColor);
        }
    } else if (IsPopulationPair() && highlightSurplus) {
        for (size_t c = 0; c < categories.size(); ++c) {
            entries.emplace_back(categories[c].name, CategoryColor(c));
            entries.emplace_back(categories[c].name + " surplus", CategoryColor(c).Darken(0.3f));
        }
    } else {
        for (size_t c = 0; c < categories.size(); ++c) {
            ChartLegendEntry e(categories[c].name, CategoryColor(c));
            e.enabled = CategoryActive(c);
            entries.push_back(e);
        }
    }
    if (style == DivergingBarStyle::PopulationPyramid && showAverageLines && IsPopulationPair()) {
        for (size_t c = 0; c < categories.size(); ++c) {
            entries.emplace_back("Average " + categories[c].name, CategoryColor(c).Darken(0.4f),
                                 LegendSwatch::DashedLine);
        }
    }
    SetLegendEntries(entries);
    SetShowLegend(!entries.empty());
}

std::vector<size_t> UltraCanvasEngineDivergingBarChart::RowOrder() const {
    std::vector<size_t> order(rows.size());
    std::iota(order.begin(), order.end(), 0);
    if (style != DivergingBarStyle::Tornado || !sortByMagnitude) return order;

    std::vector<double> net(rows.size(), 0.0);
    for (const Segment& s : BuildSegments()) net[s.row] = std::abs(s.to - s.from);
    std::stable_sort(order.begin(), order.end(),
                     [&net](size_t a, size_t b) { return net[a] > net[b]; });
    return order;
}

std::vector<UltraCanvasEngineDivergingBarChart::Segment>
UltraCanvasEngineDivergingBarChart::BuildSegments() const {
    std::vector<Segment> out;
    auto amountOf = [this](const DivergingBarRow& row, size_t c) {
        if (c >= row.values.size() || !CategoryActive(c)) return 0.0;
        const double v = row.values[c];
        return std::isfinite(v) ? std::abs(v) : 0.0;
    };

    for (size_t r = 0; r < rows.size(); ++r) {
        const DivergingBarRow& row = rows[r];
        double negativeTotal = 0.0, neutralTotal = 0.0, positiveTotal = 0.0;
        for (size_t c = 0; c < categories.size(); ++c) {
            const double a = amountOf(row, c);
            switch (categories[c].side) {
                case DivergingSide::Negative: negativeTotal += a; break;
                case DivergingSide::Neutral:  neutralTotal += a; break;
                case DivergingSide::Positive: positiveTotal += a; break;
            }
        }

        if (style == DivergingBarStyle::Tornado) {
            Segment s;
            s.row = r;
            const double net = positiveTotal - negativeTotal;
            s.side = net >= 0.0 ? 1 : -1;
            s.from = 0.0;
            s.to = net;
            s.amount = net;
            s.color = net >= 0.0 ? positiveColor : negativeColor;
            out.push_back(s);
            continue;
        }
        if (style == DivergingBarStyle::OpposingBars) {
            Segment left;
            left.row = r;
            left.side = -1;
            left.to = -(negativeTotal + neutralTotal);
            left.amount = negativeTotal + neutralTotal;
            left.offset = -0.25;
            left.fill = 0.5;
            left.color = negativeColor;
            Segment right = left;
            right.side = 1;
            right.to = positiveTotal;
            right.amount = positiveTotal;
            right.offset = 0.25;
            right.color = positiveColor;
            out.push_back(left);
            out.push_back(right);
            continue;
        }

        const bool likert = (style == DivergingBarStyle::Likert);
        const double total = negativeTotal + neutralTotal + positiveTotal;
        const double scale = (likert && percentOfRow) ? (total > 0.0 ? 100.0 / total : 0.0) : 1.0;
        double left = 0.0, right = 0.0;     // running edges, both as magnitudes

        auto push = [&](size_t c, int side, double amount, bool surplus, const Color& color) {
            if (amount <= 0.0) return;
            Segment s;
            s.row = r;
            s.category = c;
            s.side = side;
            s.amount = amount;
            s.surplus = surplus;
            s.color = color;
            if (side < 0) {
                s.from = -left;
                left += amount;
                s.to = -left;
            } else {
                s.from = right;
                right += amount;
                s.to = right;
            }
            out.push_back(s);
        };

        // The neutral share straddles zero first, so every other segment
        // grows outward from its halves.
        if (likert && splitNeutral) {
            for (size_t c = 0; c < categories.size(); ++c) {
                if (categories[c].side != DivergingSide::Neutral) continue;
                const double half = amountOf(row, c) * scale * 0.5;
                push(c, -1, half, false, CategoryColor(c));
                push(c, 1, half, false, CategoryColor(c));
            }
        }

        if (IsPopulationPair() && highlightSurplus) {
            const double l = amountOf(row, 0), rr = amountOf(row, 1);
            const double shared = std::min(l, rr);
            push(0, -1, shared, false, CategoryColor(0));
            push(0, -1, l - shared, true, CategoryColor(0).Darken(0.3f));
            push(1, 1, shared, false, CategoryColor(1));
            push(1, 1, rr - shared, true, CategoryColor(1).Darken(0.3f));
            continue;
        }

        // Negative side: under Likert the category nearest neutral (listed
        // last among the negatives) sits innermost; a pyramid stacks its
        // categories outward in list order. A neutral category that is not
        // split sits innermost on the negative side.
        std::vector<size_t> negatives;
        for (size_t c = 0; c < categories.size(); ++c) {
            const DivergingSide side = categories[c].side;
            if (side == DivergingSide::Negative) negatives.push_back(c);
        }
        if (likert) std::reverse(negatives.begin(), negatives.end());
        if (!(likert && splitNeutral)) {
            for (size_t c = 0; c < categories.size(); ++c) {
                if (categories[c].side == DivergingSide::Neutral) {
                    push(c, -1, amountOf(row, c) * scale, false, CategoryColor(c));
                }
            }
        }
        for (size_t c : negatives) push(c, -1, amountOf(row, c) * scale, false, CategoryColor(c));
        for (size_t c = 0; c < categories.size(); ++c) {
            if (categories[c].side == DivergingSide::Positive) {
                push(c, 1, amountOf(row, c) * scale, false, CategoryColor(c));
            }
        }
    }
    return out;
}

double UltraCanvasEngineDivergingBarChart::AverageOfSide(bool negativeSide,
                                                         bool* inAgeUnits) const {
    // Ages when every label parses as a band, row indices otherwise.
    bool ages = !rows.empty();
    std::vector<double> lower(rows.size()), upper(rows.size());
    double previousSpan = 5.0;
    for (size_t r = 0; r < rows.size() && ages; ++r) {
        ages = ParseAgeBand(rows[r].label, lower[r], upper[r]);
        if (!ages) break;
        if (upper[r] < 0.0) upper[r] = lower[r] + previousSpan;   // open top band
        else previousSpan = upper[r] - lower[r];
    }
    if (inAgeUnits) *inAgeUnits = ages;

    double weighted = 0.0, total = 0.0;
    for (size_t r = 0; r < rows.size(); ++r) {
        double amount = 0.0;
        for (size_t c = 0; c < categories.size(); ++c) {
            if (!CategoryActive(c) || c >= rows[r].values.size()) continue;
            const bool negative = categories[c].side != DivergingSide::Positive;
            if (negative != negativeSide) continue;
            const double v = rows[r].values[c];
            if (std::isfinite(v)) amount += std::abs(v);
        }
        const double at = ages ? (lower[r] + upper[r]) * 0.5 : static_cast<double>(r);
        weighted += at * amount;
        total += amount;
    }
    if (total <= 0.0) return std::nan("");
    return weighted / total;
}

void UltraCanvasEngineDivergingBarChart::RefreshLimiters() {
    ClearLimiters();
    if (showCenterLine) {
        ChartLimiter zero;
        zero.kind = ChartLimiterKind::Threshold;
        zero.axisIndex = 0;
        zero.value = 0.0;
        zero.color = GetTheme().axisLineColor;
        zero.width = 1.0f;
        zero.dashed = false;
        AddLimiter(zero);
    }
    if (style != DivergingBarStyle::PopulationPyramid || !showAverageLines ||
        !IsPopulationPair() || rows.empty()) return;

    for (int side = 0; side < 2; ++side) {
        const bool negative = (side == 0);
        bool ages = false;
        const double average = AverageOfSide(negative, &ages);
        if (!std::isfinite(average)) continue;

        // The average in row units: rows are slots centred on their index,
        // each spanning index-0.5 .. index+0.5.
        double position = average;
        if (ages) {
            double previousSpan = 5.0;
            position = static_cast<double>(rows.size()) - 0.5;
            for (size_t r = 0; r < rows.size(); ++r) {
                double lo = 0.0, hi = 0.0;
                ParseAgeBand(rows[r].label, lo, hi);
                if (hi < 0.0) hi = lo + previousSpan; else previousSpan = hi - lo;
                if (average < hi || r + 1 == rows.size()) {
                    position = static_cast<double>(r) - 0.5 +
                               std::clamp((average - lo) / std::max(hi - lo, 1e-9), 0.0, 1.0);
                    break;
                }
            }
        }

        ChartLimiter line;
        line.kind = ChartLimiterKind::Average;
        line.axisIndex = 1;                                // the row axis
        line.value = position;
        line.color = CategoryColor(negative ? 0 : 1).Darken(0.4f);
        line.width = 1.5f;
        line.dashed = true;
        line.captionColor = line.color;
        // One side each: the value axis is symmetric, so zero is its middle.
        line.extentStart = negative ? 0.0 : 0.5;
        line.extentEnd = negative ? 0.5 : 1.0;
        std::ostringstream caption;
        caption.setf(std::ios::fixed);
        caption.precision(1);
        caption << "Avg " << average;      // locale-ok: a caption the user reads
        line.caption = ages ? caption.str() : std::string("Avg");
        AddLimiter(line);
    }
}

std::string UltraCanvasEngineDivergingBarChart::FormatAmount(double value) const {
    if (Axes().Count() > 0) return Axes().At(0).FormatValue(value);
    ChartAxis format("value");
    format.unitPrefix = valuePrefix;
    format.unitSuffix = valueSuffix;
    format.compactNumbers = compactValues;
    format.decimals = valueDecimals;
    format.absoluteLabels = true;
    return format.FormatValue(value);
}

std::string UltraCanvasEngineDivergingBarChart::TooltipFor(const Segment& segment) const {
    std::string text = rows[segment.row].label;
    if (segment.category != SIZE_MAX && segment.category < categories.size()) {
        text += "\n" + categories[segment.category].name;
        if (segment.surplus) text += " surplus";
        text += ": " + FormatAmount(segment.amount);
    } else if (style == DivergingBarStyle::Tornado) {
        text += "\nNet: " + std::string(segment.amount < 0.0 ? "-" : "+") +
                FormatAmount(segment.amount);
    } else {
        text += "\n" + std::string(segment.side < 0 ? "Left" : "Right") + " total: " +
                FormatAmount(segment.amount);
    }
    return text;
}

void UltraCanvasEngineDivergingBarChart::SegmentSpan(const ChartAxis& rowAxis, size_t slot,
                                                     const Segment& segment,
                                                     double& u0, double& u1) const {
    const double center = rowAxis.Normalize(static_cast<double>(slot));
    const double width = std::abs(rowAxis.Normalize(1.0) - rowAxis.Normalize(0.0)) * barFill;
    const double mid = center + segment.offset * width;
    u0 = mid - width * segment.fill * 0.5;
    u1 = mid + width * segment.fill * 0.5;
}

// =============================================================================
// ENGINE CONTRACT
// =============================================================================

void UltraCanvasEngineDivergingBarChart::DescribeAxes(ChartAxisSet& axes) {
    const bool horizontal = GetProjectionKind() == ChartProjectionKind::Horizontal;

    ChartAxis value("value");
    value.side = horizontal ? ChartAxisSide::Bottom : ChartAxisSide::Left;
    value.symmetric = true;               // zero stays in the middle
    value.absoluteLabels = true;          // both sides read as amounts
    value.unitPrefix = valuePrefix;
    value.unitSuffix = valueSuffix;
    if (style == DivergingBarStyle::Likert && percentOfRow && valueSuffix.empty()) {
        value.unitSuffix = "%";
    }
    value.compactNumbers = compactValues;
    value.decimals = valueDecimals;
    value.title = valueAxisTitle;
    if (valueLimit > 0.0) {
        value.SetRange(-valueLimit, valueLimit);
    } else {
        value.Observe(0.0);
        for (const Segment& s : BuildSegments()) {
            value.Observe(s.from);
            value.Observe(s.to);
        }
    }
    axes.Add(value);

    ChartAxis rowAxis("rows");
    rowAxis.scale = ChartScale::Category;
    rowAxis.side = horizontal ? ChartAxisSide::Left : ChartAxisSide::Bottom;
    for (size_t r : RowOrder()) rowAxis.categories.push_back(rows[r].label);
    rowAxis.categoryPadding = 0.5;
    // The horizontal projection runs the domain downward; bottom-up rows
    // (youngest age group lowest) invert it.
    rowAxis.inverted = horizontal && rowOrder == DivergingRowOrder::BottomUp;
    axes.Add(rowAxis);

    SetGridAxis(0);
}

void UltraCanvasEngineDivergingBarChart::RenderChartContent(IRenderContext* ctx,
                                                            const ChartEngineFrame& frame) {
    if (rows.empty() || !frame.axes || frame.axes->Count() < 2) return;
    const ChartAxis& value = frame.axes->At(0);
    const ChartAxis& rowAxis = frame.axes->At(1);
    const IChartProjection& projection = *frame.projection;
    const double progress = frame.animationProgress;

    const std::vector<size_t> order = RowOrder();
    std::vector<size_t> slotOf(rows.size(), 0);
    for (size_t s = 0; s < order.size(); ++s) slotOf[order[s]] = s;

    const bool insideLabels = showValues && (style == DivergingBarStyle::Likert ||
                                             (style == DivergingBarStyle::PopulationPyramid &&
                                              !IsPopulationPair()));
    ctx->SetFontSize(axisFontSize);

    for (const Segment& segment : BuildSegments()) {
        double u0, u1;
        SegmentSpan(rowAxis, slotOf[segment.row], segment, u0, u1);
        // Every segment grows out of zero together, so a stack never parts.
        const double v0 = value.Normalize(segment.from * progress);
        const double v1 = value.Normalize(segment.to * progress);
        const std::vector<Point2Dd> outline = BuildBarOutline(projection, u0, v0, u1, v1, 4, 0.0);
        if (outline.size() < 3) continue;

        const int64_t id = SegmentRegionId(segment.row, segment.category, segment.surplus);
        const bool hovered = HoveredRegionId() == id;
        ctx->SetFillPaint(hovered ? segment.color.Lighten(0.25f) : segment.color);
        ctx->FillLinePath(outline);
        ctx->SetStrokePaint(GetTheme().plotAreaColor);      // a hairline between segments
        ctx->SetStrokeWidth(hovered ? 2.0f : 1.0f);
        ctx->DrawLinePath(outline, true);
        AddHitRegion(outline, id, TooltipFor(segment));

        // Amounts inside the segments: in-mark text cannot collide with
        // anything else, so it skips the solver and is simply dropped when
        // the segment is too small to hold it.
        if (insideLabels && progress >= 1.0) {
            const std::string text = FormatAmount(segment.amount);
            const Size2Di size = ctx->GetTextLineDimensions(text);
            const Point2Dd a = projection.ToScreen(ChartNormalizedPoint(u0, v0));
            const Point2Dd b = projection.ToScreen(ChartNormalizedPoint(u1, v1));
            const double w = std::abs(b.x - a.x), h = std::abs(b.y - a.y);
            if (size.width + 4.0 <= w && size.height + 2.0 <= h) {
                ctx->SetTextPaint(Luminance(segment.color) > 0.6 ? Color(40, 40, 40, 255)
                                                                 : Color(255, 255, 255, 255));
                ctx->DrawText(text, Point2Dd((a.x + b.x - size.width) * 0.5,
                                             (a.y + b.y - size.height) * 0.5));
            }
        }
    }
}

void UltraCanvasEngineDivergingBarChart::MeasureContent(IRenderContext* ctx,
                                                        ChartLayoutRequest& request) {
    if (!showValues) return;
    // End labels (tornado, opposing, pyramid totals) spill past the longest
    // bar on both sides of the value direction.
    ctx->SetFontSize(axisFontSize);
    double widest = 0.0;
    for (const Segment& s : BuildSegments()) {
        widest = std::max(widest, static_cast<double>(
            ctx->GetTextLineWidth(FormatAmount(s.to))));
    }
    if (GetProjectionKind() == ChartProjectionKind::Horizontal) {
        request.Reserve(ChartAxisEdge::Right, widest + 8.0);
        request.Reserve(ChartAxisEdge::Left, widest + 8.0);
    } else {
        request.Reserve(ChartAxisEdge::Top, ctx->GetTextLineHeight("Ag") + 6.0);
    }
}

void UltraCanvasEngineDivergingBarChart::CollectChartLabels(IRenderContext* ctx,
                                                            ChartLabelBroker& broker) {
    if (!showValues || rows.empty() || !Frame().axes || Frame().axes->Count() < 2 ||
        !Frame().projection) return;
    // Likert (and multi-category pyramids) label inside the segments.
    if (style == DivergingBarStyle::Likert ||
        (style == DivergingBarStyle::PopulationPyramid && !IsPopulationPair())) return;

    const ChartAxis& value = Frame().axes->At(0);
    const ChartAxis& rowAxis = Frame().axes->At(1);
    const bool horizontal = GetProjectionKind() == ChartProjectionKind::Horizontal;
    const std::vector<size_t> order = RowOrder();
    std::vector<size_t> slotOf(rows.size(), 0);
    for (size_t s = 0; s < order.size(); ++s) slotOf[order[s]] = s;
    ctx->SetFontSize(axisFontSize);

    // One label per bar end: the outermost segment of each side of a row.
    std::vector<const Segment*> ends;
    const std::vector<Segment> segments = BuildSegments();
    for (const Segment& s : segments) {
        bool outermost = true;
        for (const Segment& other : segments) {
            if (&other != &s && other.row == s.row && other.side == s.side &&
                other.offset == s.offset && std::abs(other.to) > std::abs(s.to)) {
                outermost = false;
                break;
            }
        }
        if (outermost) ends.push_back(&s);
    }

    for (const Segment* s : ends) {
        double u0, u1;
        SegmentSpan(rowAxis, slotOf[s->row], *s, u0, u1);
        // A pyramid's end label is the side's total, not the outer segment.
        const double shown = (style == DivergingBarStyle::PopulationPyramid) ? std::abs(s->to)
                                                                             : s->amount;
        ChartLabelRequest r;
        r.text = (style == DivergingBarStyle::Tornado)
                     ? std::string(shown < 0.0 ? "-" : "+") + FormatAmount(shown)
                     : FormatAmount(shown);
        r.klass = ChartLabelClass::ValueLabel;
        r.anchor = Frame().projection->ToScreen(
            ChartNormalizedPoint((u0 + u1) * 0.5, value.Normalize(s->to)));
        const Size2Di size = ctx->GetTextLineDimensions(r.text);
        r.textSize = Size2Dd(size.width, size.height);
        const bool right = s->to >= 0.0;
        r.preferredSide = horizontal ? (right ? LabelSide::Right : LabelSide::Left)
                                     : (right ? LabelSide::Top : LabelSide::Bottom);
        r.allowSuppress = true;
        broker.Add(r);
    }
}

} // namespace UltraCanvas
