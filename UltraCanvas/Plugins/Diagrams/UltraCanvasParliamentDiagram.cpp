// Plugins/Diagrams/UltraCanvasParliamentDiagram.cpp
// Parliament (hemicycle) seat diagram implementation
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasParliamentDiagram.h"
#include "UltraCanvasTooltipManager.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

namespace UltraCanvas {

    namespace {
        constexpr double kPi = 3.14159265358979323846;
        // A handful of seats in a big element would otherwise become discs
        // the size of coins.
        constexpr double kMaxSeatRadius = 24.0;

        std::string FormatPercent(double value) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f%%", value);
            return buf;
        }

        // Seat under construction: the angle is needed for the fill order and
        // the majority marker but is not part of the public seat record.
        struct ArcSeat {
            double angle = 0.0;
            int row = 0;
            Point2Dd center;
        };
    }

// =============================================================================
// CONSTRUCTION & DEFAULTS
// =============================================================================

    UltraCanvasParliamentDiagram::UltraCanvasParliamentDiagram(const std::string& id, int x, int y, int w, int h)
            : UltraCanvasChartElementBase(id, x, y, w, h) {
        enableSelection = true;
        enableTooltips = true;
        showGrid = false;
        showAxes = false;
        backgroundColor = Color(255, 255, 255, 255);
    }

// =============================================================================
// PARTY MANAGEMENT
// =============================================================================

    void UltraCanvasParliamentDiagram::AddParty(const ParliamentParty& party) {
        parties.push_back(party);
        if (parties.back().seats < 0) parties.back().seats = 0;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasParliamentDiagram::AddParty(const std::string& name, int seats, const Color& color, bool government) {
        AddParty(ParliamentParty(name, seats, color, government));
    }

    void UltraCanvasParliamentDiagram::SetParties(const std::vector<ParliamentParty>& list) {
        parties = list;
        for (auto& p : parties) {
            if (p.seats < 0) p.seats = 0;
        }
        hoveredParty = SIZE_MAX;
        hoveredSeat = SIZE_MAX;
        selectedParty = SIZE_MAX;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasParliamentDiagram::RemoveParty(size_t index) {
        if (index >= parties.size()) return;
        parties.erase(parties.begin() + static_cast<std::ptrdiff_t>(index));
        hoveredParty = SIZE_MAX;
        hoveredSeat = SIZE_MAX;
        if (selectedParty == index) selectedParty = SIZE_MAX;
        else if (selectedParty != SIZE_MAX && selectedParty > index) --selectedParty;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasParliamentDiagram::ClearParties() {
        parties.clear();
        hoveredParty = SIZE_MAX;
        hoveredSeat = SIZE_MAX;
        selectedParty = SIZE_MAX;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasParliamentDiagram::SetPartySeats(size_t index, int seats) {
        if (index >= parties.size()) return;
        parties[index].seats = std::max(0, seats);
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasParliamentDiagram::SetPartyColor(size_t index, const Color& color) {
        if (index >= parties.size()) return;
        parties[index].color = color;
        RequestRedraw();
    }

    void UltraCanvasParliamentDiagram::SetPartyGovernment(size_t index, bool government) {
        if (index >= parties.size()) return;
        parties[index].government = government;
        // The Westminster blocks are filled by side, so this moves seats.
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasParliamentDiagram::SetPartyVacant(size_t index, bool vacant) {
        if (index >= parties.size()) return;
        parties[index].vacant = vacant;
        RequestRedraw();
    }

    int UltraCanvasParliamentDiagram::GetTotalSeats() const {
        int total = 0;
        for (const auto& p : parties) total += p.seats;
        return total;
    }

    int UltraCanvasParliamentDiagram::GetGovernmentSeats() const {
        int total = 0;
        for (const auto& p : parties) {
            if (p.government) total += p.seats;
        }
        return total;
    }

// =============================================================================
// LAYOUT & APPEARANCE SETTERS
// =============================================================================

    void UltraCanvasParliamentDiagram::SetLayout(ParliamentLayout l) {
        if (layout == l) return;
        layout = l;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasParliamentDiagram::SetArcSpan(double degrees) {
        double clamped = std::clamp(degrees, 90.0, 360.0);
        if (arcSpanDegrees == clamped) return;
        arcSpanDegrees = clamped;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasParliamentDiagram::SetRowCount(int rows) {
        int clamped = std::max(0, rows);
        if (rowCount == clamped) return;
        rowCount = clamped;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasParliamentDiagram::SetInnerRadiusRatio(double ratio) {
        double clamped = (ratio <= 0.0) ? 0.0 : std::clamp(ratio, 0.05, 0.9);
        if (innerRadiusRatio == clamped) return;
        innerRadiusRatio = clamped;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasParliamentDiagram::SetSeatGap(double fraction) {
        double clamped = std::clamp(fraction, 0.0, 0.8);
        if (seatGap == clamped) return;
        seatGap = clamped;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasParliamentDiagram::SetGridColumns(int columns) {
        int clamped = std::max(0, columns);
        if (gridColumns == clamped) return;
        gridColumns = clamped;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasParliamentDiagram::SetSeatShape(ParliamentSeatShape shape) {
        if (seatShape == shape) return;
        seatShape = shape;
        RequestRedraw();
    }

    void UltraCanvasParliamentDiagram::SetDarkTheme(bool dark) {
        if (darkTheme == dark) return;
        darkTheme = dark;
        RequestRedraw();
    }

    void UltraCanvasParliamentDiagram::SetShowTotalLabel(bool show) {
        if (showTotalLabel == show) return;
        showTotalLabel = show;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasParliamentDiagram::SetTotalCaption(const std::string& caption) {
        totalCaption = caption;
        RequestRedraw();
    }

    void UltraCanvasParliamentDiagram::SetShowMajorityMarker(bool show) {
        if (showMajorityMarker == show) return;
        showMajorityMarker = show;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasParliamentDiagram::SetMajorityCaption(const std::string& caption) {
        majorityCaption = caption;
        RequestRedraw();
    }

    void UltraCanvasParliamentDiagram::SetHighlightGovernment(bool highlight) {
        if (highlightGovernment == highlight) return;
        highlightGovernment = highlight;
        RequestRedraw();
    }

    void UltraCanvasParliamentDiagram::SetDimOnHover(bool dim) {
        if (dimOnHover == dim) return;
        dimOnHover = dim;
        RequestRedraw();
    }

    void UltraCanvasParliamentDiagram::SetShowSpeakerChair(bool show) {
        if (showSpeakerChair == show) return;
        showSpeakerChair = show;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasParliamentDiagram::SetLegendPosition(ParliamentLegendPosition position) {
        if (legendPosition == position) return;
        legendPosition = position;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasParliamentDiagram::SetTitleFontSize(float size) {
        titleFontSize = std::max(6.0f, size);
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasParliamentDiagram::SetTotalFontSize(float size) {
        totalFontSize = std::max(6.0f, size);
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasParliamentDiagram::SetLegendFontSize(float size) {
        legendFontSize = std::max(6.0f, size);
        InvalidateLayout();
        RequestRedraw();
    }

// =============================================================================
// SELECTION
// =============================================================================

    void UltraCanvasParliamentDiagram::SetSelectedParty(size_t index) {
        size_t next = (index < parties.size()) ? index : SIZE_MAX;
        if (selectedParty == next) return;
        selectedParty = next;
        if (onSelectionChange) onSelectionChange();
        RequestRedraw();
    }

    void UltraCanvasParliamentDiagram::ClearSelection() {
        if (selectedParty == SIZE_MAX) return;
        selectedParty = SIZE_MAX;
        RequestRedraw();
    }

// =============================================================================
// THEME HELPERS
// =============================================================================

    Color UltraCanvasParliamentDiagram::BackgroundFill() const {
        return darkTheme ? Color(30, 33, 40, 255) : backgroundColor;
    }

    Color UltraCanvasParliamentDiagram::TextColor() const {
        return darkTheme ? Color(235, 238, 242, 255) : Color(40, 44, 52, 255);
    }

    Color UltraCanvasParliamentDiagram::MutedTextColor() const {
        return darkTheme ? Color(150, 156, 166, 255) : Color(120, 126, 136, 255);
    }

    bool UltraCanvasParliamentDiagram::IsPartyActive(size_t party) const {
        if (!dimOnHover) return true;
        size_t focus = (hoveredParty != SIZE_MAX) ? hoveredParty : selectedParty;
        if (focus == SIZE_MAX) return true;
        return party == focus;
    }

    Color UltraCanvasParliamentDiagram::SeatFillColor(size_t seatIndex) const {
        const ParliamentSeat& seat = cache.seats[seatIndex];
        if (seat.party >= parties.size()) return MutedTextColor();
        const ParliamentParty& party = parties[seat.party];
        Color color = party.color;
        Color bg = BackgroundFill();
        if (!IsPartyActive(seat.party)) {
            return color.Blend(bg, 0.78f);
        }
        if (highlightGovernment && !party.government) {
            return color.Blend(bg, 0.6f);
        }
        return color;
    }

    double UltraCanvasParliamentDiagram::EffectiveInnerRatio() const {
        if (innerRadiusRatio > 0.0) return innerRadiusRatio;
        if (layout == ParliamentLayout::Circle) return 0.55;
        // A wide horseshoe needs a bigger hole or its inner rows crowd the centre
        return (arcSpanDegrees > 240.0) ? 0.5 : 0.42;
    }

// =============================================================================
// LAYOUT
// =============================================================================

    std::string UltraCanvasParliamentDiagram::LegendText(const ParliamentParty& party) const {
        return party.LegendName() + "  " + std::to_string(party.seats);
    }

    // Places the legend entries along the bottom (rows, centred) or down the
    // right edge (one column) of bounds. Returns the height (horizontal) or
    // width (vertical) the legend takes.
    double UltraCanvasParliamentDiagram::MeasureLegend(IRenderContext* ctx, const Rect2Dd& bounds,
                                                       std::vector<LegendEntry>& entries, bool vertical) const {
        entries.clear();
        if (parties.empty()) return 0.0;

        ctx->SetFontSize(legendFontSize);
        ctx->SetFontWeight(FontWeight::Normal);
        ctx->SetFontSlant(FontSlant::Normal);

        const double swatch = legendFontSize * 1.1;
        const double lineH = legendFontSize * 1.75;
        const double padX = 10.0;

        std::vector<double> widths;
        widths.reserve(parties.size());
        for (const auto& p : parties) {
            Size2Di s = ctx->GetTextLineDimensions(LegendText(p));
            widths.push_back(swatch + 6.0 + s.width + padX * 2.0);
        }

        if (vertical) {
            double colW = 0.0;
            for (double w : widths) colW = std::max(colW, w);
            colW = std::min(colW, bounds.width * 0.45);
            double x = bounds.x + bounds.width - colW;
            double totalH = lineH * static_cast<double>(parties.size());
            double y = bounds.y + std::max(0.0, (bounds.height - totalH) / 2.0);
            for (size_t i = 0; i < parties.size(); ++i) {
                LegendEntry e;
                e.party = i;
                e.rect = Rect2Dd(x, y, colW, lineH);
                entries.push_back(e);
                y += lineH;
            }
            return colW;
        }

        // Greedy row packing, then each row centred in bounds
        std::vector<std::vector<size_t>> rows(1);
        std::vector<double> rowWidths(1, 0.0);
        for (size_t i = 0; i < parties.size(); ++i) {
            double w = std::min(widths[i], bounds.width);
            if (rowWidths.back() > 0.0 && rowWidths.back() + w > bounds.width) {
                rows.emplace_back();
                rowWidths.push_back(0.0);
            }
            rows.back().push_back(i);
            rowWidths.back() += w;
        }

        double totalH = lineH * static_cast<double>(rows.size());
        double y = bounds.y + bounds.height - totalH;
        for (size_t r = 0; r < rows.size(); ++r) {
            double x = bounds.x + (bounds.width - rowWidths[r]) / 2.0;
            for (size_t i : rows[r]) {
                LegendEntry e;
                e.party = i;
                e.rect = Rect2Dd(x, y, std::min(widths[i], bounds.width), lineH);
                entries.push_back(e);
                x += e.rect.width;
            }
            y += lineH;
        }
        return totalH;
    }

    void UltraCanvasParliamentDiagram::UpdateLayout(IRenderContext* ctx) {
        const int w = GetWidth();
        const int h = GetHeight();
        // The title comes through the base class, which does not know about
        // this cache, so its presence is part of the cache key.
        const bool hasTitle = !chartTitle.empty();
        if (cache.valid && cache.width == w && cache.height == h && cache.hadTitle == hasTitle) return;

        cache = DiagramLayout();
        cache.width = w;
        cache.height = h;
        cache.hadTitle = hasTitle;

        const double pad = 12.0;
        Rect2Dd area(pad, pad, std::max(0.0, w - pad * 2.0), std::max(0.0, h - pad * 2.0));

        if (!chartTitle.empty()) {
            double band = titleFontSize * 1.8;
            area.y += band;
            area.height -= band;
        }

        if (legendPosition == ParliamentLegendPosition::Bottom) {
            double legendH = MeasureLegend(ctx, area, cache.legend, false);
            if (legendH > 0.0) area.height -= legendH + 8.0;
        } else if (legendPosition == ParliamentLegendPosition::Right) {
            double legendW = MeasureLegend(ctx, area, cache.legend, true);
            if (legendW > 0.0) area.width -= legendW + 12.0;
        }

        const bool arcLayout = (layout == ParliamentLayout::Hemicycle || layout == ParliamentLayout::Circle);

        if (showTotalLabel && !arcLayout) {
            double band = totalFontSize * 1.15 + legendFontSize * 1.4 + 10.0;
            cache.totalLabelCenter = Point2Dd(area.x + area.width / 2.0, area.y + area.height - band / 2.0);
            area.height -= band;
        }

        if (showMajorityMarker && arcLayout) {
            double margin = majorityFontSize * 1.5 + 10.0;
            area.y += margin;
            area.height -= margin * 2.0;
        }

        cache.valid = true;
        if (area.width < 10.0 || area.height < 10.0 || GetTotalSeats() <= 0) return;
        cache.chamberArea = area;

        switch (layout) {
            case ParliamentLayout::Hemicycle:
                LayoutArc(area, arcSpanDegrees * kPi / 180.0);
                break;
            case ParliamentLayout::Circle:
                LayoutArc(area, 2.0 * kPi);
                break;
            case ParliamentLayout::Westminster:
                LayoutWestminster(area);
                break;
            case ParliamentLayout::Grid:
                LayoutGrid(area);
                break;
        }
    }

    // Hands the seats to the parties in order: the first party takes the first
    // seats of the fill order, the next party the following ones, and so on.
    void UltraCanvasParliamentDiagram::AssignPartiesInOrder(std::vector<ParliamentSeat>& seats) const {
        size_t party = 0;
        int left = parties.empty() ? 0 : parties[0].seats;
        for (size_t i = 0; i < seats.size(); ++i) {
            while (party < parties.size() && left <= 0) {
                ++party;
                left = (party < parties.size()) ? parties[party].seats : 0;
            }
            seats[i].party = (party < parties.size()) ? party : SIZE_MAX;
            seats[i].ordinal = i;
            --left;
        }
    }

    void UltraCanvasParliamentDiagram::LayoutArc(const Rect2Dd& area, double span) {
        const int total = GetTotalSeats();
        if (total <= 0) return;

        const bool fullCircle = span >= 2.0 * kPi - 1e-6;
        const double ratio = EffectiveInnerRatio();

        // Seats run from aStart down to aEnd. The hemicycle is centred on
        // "up" (angle pi/2); the full circle starts at the top and goes
        // clockwise.
        double aStart, aEnd;
        if (fullCircle) {
            aStart = kPi / 2.0;
            aEnd = aStart - 2.0 * kPi;
        } else {
            aStart = kPi / 2.0 + span / 2.0;
            aEnd = kPi / 2.0 - span / 2.0;
        }

        // Bounding box of the annulus sector in units of the outer radius,
        // screen orientation (y down)
        double uxMin = 1e9, uxMax = -1e9, uyMin = 1e9, uyMax = -1e9;
        auto addPoint = [&](double rho, double a) {
            double ux = rho * std::cos(a);
            double uy = -rho * std::sin(a);
            uxMin = std::min(uxMin, ux); uxMax = std::max(uxMax, ux);
            uyMin = std::min(uyMin, uy); uyMax = std::max(uyMax, uy);
        };
        for (double rho : {ratio, 1.0}) {
            addPoint(rho, aStart);
            addPoint(rho, aEnd);
            for (int k = -4; k <= 8; ++k) {
                double c = k * kPi / 2.0;
                if (c <= aStart + 1e-9 && c >= aEnd - 1e-9) addPoint(rho, c);
            }
        }
        const double bw = uxMax - uxMin;
        const double bh = uyMax - uyMin;
        if (bw <= 0.0 || bh <= 0.0) return;

        const double R = std::min(area.width / bw, area.height / bh);
        if (R <= 1.0) return;
        const double cx = area.x + (area.width - bw * R) / 2.0 - uxMin * R;
        const double cy = area.y + (area.height - bh * R) / 2.0 - uyMin * R;
        const double innerR = ratio * R;

        // Seats per arc that fit when the spacing along the arc equals the
        // spacing between arcs
        auto capacity = [&](int rows) -> long {
            double pitch = (R - innerR) / rows;
            long sum = 0;
            for (int i = 0; i < rows; ++i) {
                double rho = innerR + pitch * (i + 0.5);
                sum += static_cast<long>(std::floor(span * rho / pitch)) + (fullCircle ? 0 : 1);
            }
            return sum;
        };

        int rows = (rowCount > 0) ? rowCount : 1;
        if (rowCount <= 0) {
            while (rows < 80 && capacity(rows) < total) ++rows;
        }
        const double pitch = (R - innerR) / rows;

        // Seats per arc in proportion to its radius (an outer arc is longer)
        std::vector<double> rho(rows);
        double sumRho = 0.0;
        for (int i = 0; i < rows; ++i) {
            rho[i] = innerR + pitch * (i + 0.5);
            sumRho += rho[i];
        }
        std::vector<int> count(rows, 0);
        std::vector<double> frac(rows, 0.0);
        int assigned = 0;
        for (int i = 0; i < rows; ++i) {
            double exact = total * rho[i] / sumRho;
            count[i] = static_cast<int>(std::floor(exact));
            frac[i] = exact - count[i];
            assigned += count[i];
        }
        std::vector<int> order(rows);
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return frac[a] > frac[b]; });
        for (int k = 0; assigned < total; ++k) {
            ++count[order[k % rows]];
            ++assigned;
        }

        // Seat radius: half the arc pitch, or less when an arc is crowded
        double seatR = pitch * 0.5;
        for (int i = 0; i < rows; ++i) {
            if (count[i] <= 1) continue;
            double step = fullCircle ? span * rho[i] / count[i] : span * rho[i] / (count[i] - 1);
            seatR = std::min(seatR, step * 0.5);
        }
        seatR *= (1.0 - seatGap);
        seatR = std::clamp(seatR, 0.75, kMaxSeatRadius);

        std::vector<ArcSeat> arcSeats;
        arcSeats.reserve(static_cast<size_t>(total));
        for (int i = 0; i < rows; ++i) {
            for (int j = 0; j < count[i]; ++j) {
                double a;
                if (fullCircle) {
                    a = aStart - (j + 0.5) * span / count[i];
                } else if (count[i] == 1) {
                    a = kPi / 2.0;
                } else {
                    a = aStart - j * span / (count[i] - 1);
                }
                ArcSeat s;
                s.angle = a;
                s.row = i;
                s.center = Point2Dd(cx + rho[i] * std::cos(a), cy - rho[i] * std::sin(a));
                arcSeats.push_back(s);
            }
        }

        // Fill order: sweep the angle, inner arc first where seats line up
        std::stable_sort(arcSeats.begin(), arcSeats.end(), [](const ArcSeat& a, const ArcSeat& b) {
            if (std::fabs(a.angle - b.angle) > 1e-9) return a.angle > b.angle;
            return a.row < b.row;
        });

        cache.seats.clear();
        cache.seats.reserve(arcSeats.size());
        for (const auto& s : arcSeats) {
            ParliamentSeat seat;
            seat.center = s.center;
            seat.radius = seatR;
            seat.row = s.row;
            cache.seats.push_back(seat);
        }
        AssignPartiesInOrder(cache.seats);

        if (total >= 2) {
            size_t k = static_cast<size_t>(total) / 2;
            cache.majorityAngle = (arcSeats[k - 1].angle + arcSeats[k].angle) / 2.0;
            cache.hasMajorityAngle = true;
        }

        cache.center = Point2Dd(cx, cy);
        cache.innerRadius = innerR;
        cache.outerRadius = R;
        cache.rowsUsed = rows;
        cache.seatRadius = seatR;
        if (fullCircle || span > kPi + 1e-6) {
            cache.totalLabelCenter = Point2Dd(cx, cy);
        } else {
            cache.totalLabelCenter = Point2Dd(cx, cy - innerR * 0.5);
        }
    }

    void UltraCanvasParliamentDiagram::LayoutWestminster(const Rect2Dd& area) {
        const int total = GetTotalSeats();
        if (total <= 0) return;
        const int gov = GetGovernmentSeats();
        const int opp = total - gov;

        const double speakerH = showSpeakerChair ? 36.0 : 0.0;
        const double aisle = std::max(28.0, area.width * 0.07);
        const double blockW = (area.width - aisle) / 2.0;
        const double blockH = area.height - speakerH;
        if (blockW < 4.0 || blockH < 4.0) return;

        const int n = std::max(1, std::max(gov, opp));
        int benches;
        if (rowCount > 0) {
            benches = rowCount;
        } else {
            double p = std::sqrt(blockW * blockH / n);
            benches = std::clamp(static_cast<int>(std::floor(blockW / p)), 1, std::max(1, n));
        }
        int perBench = (n + benches - 1) / benches;
        const double pitch = std::min(blockW / benches, blockH / perBench);
        const double seatR = std::clamp(pitch * 0.5 * (1.0 - seatGap), 0.75, kMaxSeatRadius);

        const double usedW = benches * pitch;
        const double usedH = perBench * pitch;
        const double aisleX = area.x + area.width / 2.0;
        const double top = area.y + speakerH + (blockH - usedH) / 2.0;
        (void)usedW;

        // Bench b = 0 is the front bench next to the aisle; seats run top to
        // bottom along it.
        auto placeBlock = [&](int seats, bool left, size_t firstOrdinal) {
            for (int i = 0; i < seats; ++i) {
                int bench = i / perBench;
                int pos = i % perBench;
                double x = left ? aisleX - aisle / 2.0 - (bench + 0.5) * pitch
                                : aisleX + aisle / 2.0 + (bench + 0.5) * pitch;
                ParliamentSeat seat;
                seat.center = Point2Dd(x, top + (pos + 0.5) * pitch);
                seat.radius = seatR;
                seat.row = bench;
                seat.ordinal = firstOrdinal + static_cast<size_t>(i);
                cache.seats.push_back(seat);
            }
        };

        cache.seats.clear();
        cache.seats.reserve(static_cast<size_t>(total));
        placeBlock(gov, true, 0);
        placeBlock(opp, false, static_cast<size_t>(gov));

        // Governing parties fill the left block in order, the rest the right
        size_t seatIdx = 0;
        for (int pass = 0; pass < 2; ++pass) {
            bool wantGov = (pass == 0);
            for (size_t p = 0; p < parties.size(); ++p) {
                if (parties[p].government != wantGov) continue;
                for (int s = 0; s < parties[p].seats && seatIdx < cache.seats.size(); ++s) {
                    cache.seats[seatIdx++].party = p;
                }
            }
        }

        if (showSpeakerChair) {
            double chairW = std::max(aisle + pitch, 64.0);
            cache.speakerRect = Rect2Dd(aisleX - chairW / 2.0, area.y + 4.0, chairW, speakerH - 12.0);
        }
        cache.rowsUsed = benches;
        cache.seatRadius = seatR;
    }

    void UltraCanvasParliamentDiagram::LayoutGrid(const Rect2Dd& area) {
        const int total = GetTotalSeats();
        if (total <= 0) return;

        int cols;
        if (gridColumns > 0) {
            cols = gridColumns;
        } else if (rowCount > 0) {
            cols = (total + rowCount - 1) / rowCount;
        } else {
            double aspect = area.width / std::max(1.0, area.height);
            cols = static_cast<int>(std::lround(std::sqrt(total * aspect)));
        }
        cols = std::clamp(cols, 1, total);
        const int rows = (total + cols - 1) / cols;
        const double pitch = std::min(area.width / cols, area.height / rows);
        const double seatR = std::clamp(pitch * 0.5 * (1.0 - seatGap), 0.75, kMaxSeatRadius);

        const double x0 = area.x + (area.width - cols * pitch) / 2.0;
        const double y0 = area.y + (area.height - rows * pitch) / 2.0;

        cache.seats.clear();
        cache.seats.reserve(static_cast<size_t>(total));
        for (int i = 0; i < total; ++i) {
            int r = i / cols;
            int c = i % cols;
            ParliamentSeat seat;
            seat.center = Point2Dd(x0 + (c + 0.5) * pitch, y0 + (r + 0.5) * pitch);
            seat.radius = seatR;
            seat.row = r;
            cache.seats.push_back(seat);
        }
        AssignPartiesInOrder(cache.seats);
        cache.rowsUsed = rows;
        cache.seatRadius = seatR;
    }

// =============================================================================
// HIT TESTING
// =============================================================================

    size_t UltraCanvasParliamentDiagram::FindSeatAt(const Point2Di& pos) const {
        if (!cache.valid) return SIZE_MAX;
        const bool round = (seatShape == ParliamentSeatShape::Circle);
        for (size_t i = 0; i < cache.seats.size(); ++i) {
            const ParliamentSeat& s = cache.seats[i];
            double dx = pos.x - s.center.x;
            double dy = pos.y - s.center.y;
            // A little slack so tiny seats stay hoverable
            double r = std::max(s.radius, 2.5);
            if (round) {
                if (dx * dx + dy * dy <= r * r) return i;
            } else if (std::fabs(dx) <= r && std::fabs(dy) <= r) {
                return i;
            }
        }
        return SIZE_MAX;
    }

    size_t UltraCanvasParliamentDiagram::FindLegendEntryAt(const Point2Di& pos) const {
        if (!cache.valid) return SIZE_MAX;
        for (const auto& e : cache.legend) {
            if (e.rect.Contains(Point2Dd(pos.x, pos.y))) return e.party;
        }
        return SIZE_MAX;
    }

// =============================================================================
// RENDERING - TOP LEVEL
// =============================================================================

    void UltraCanvasParliamentDiagram::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        if (!ctx) return;
        UpdateLayout(ctx);

        ctx->DrawFilledRectangle(GetLocalBounds(), BackgroundFill());
        DrawTitle(ctx);
        RenderChart(ctx);
    }

    void UltraCanvasParliamentDiagram::RenderChart(IRenderContext* ctx) {
        if (!ctx || !cache.valid) return;

        if (cache.seats.empty()) {
            DrawEmptyChamber(ctx);
        } else {
            if (layout == ParliamentLayout::Westminster && showSpeakerChair) DrawSpeakerChair(ctx);
            DrawSeats(ctx);
            if (showMajorityMarker && cache.hasMajorityAngle) DrawMajorityMarker(ctx);
            if (showTotalLabel) DrawTotalLabel(ctx);
        }
        if (legendPosition != ParliamentLegendPosition::Hidden) DrawLegend(ctx);
    }

    void UltraCanvasParliamentDiagram::DrawTitle(IRenderContext* ctx) {
        if (chartTitle.empty()) return;
        ctx->SetTextPaint(TextColor());
        ctx->SetFontSize(titleFontSize);
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetFontSlant(FontSlant::Normal);
        Size2Di s = ctx->GetTextLineDimensions(chartTitle);
        ctx->DrawText(chartTitle, Point2Dd((GetWidth() - s.width) / 2.0, 12.0));
        ctx->SetFontWeight(FontWeight::Normal);
    }

    void UltraCanvasParliamentDiagram::DrawEmptyChamber(IRenderContext* ctx) {
        ctx->SetTextPaint(MutedTextColor());
        ctx->SetFontSize(legendFontSize + 2.0f);
        ctx->SetFontWeight(FontWeight::Normal);
        ctx->SetFontSlant(FontSlant::Normal);
        const std::string text = "No seats";
        Size2Di s = ctx->GetTextLineDimensions(text);
        ctx->DrawText(text, Point2Dd((GetWidth() - s.width) / 2.0, (GetHeight() - s.height) / 2.0));
    }

// =============================================================================
// RENDERING - SEATS
// =============================================================================

    void UltraCanvasParliamentDiagram::DrawSeatShape(IRenderContext* ctx, const Point2Dd& center, double radius, bool fill) {
        switch (seatShape) {
            case ParliamentSeatShape::Circle:
                if (fill) ctx->FillCircle(center, radius);
                else ctx->DrawCircle(center, radius);
                break;
            case ParliamentSeatShape::Square: {
                Rect2Dd r(center.x - radius, center.y - radius, radius * 2.0, radius * 2.0);
                if (fill) ctx->FillRectangle(r);
                else ctx->DrawRectangle(r);
                break;
            }
            case ParliamentSeatShape::RoundedSquare: {
                Rect2Dd r(center.x - radius, center.y - radius, radius * 2.0, radius * 2.0);
                double corner = radius * 0.4;
                if (fill) ctx->FillRoundedRectangle(r, corner);
                else ctx->DrawRoundedRectangle(r, corner);
                break;
            }
        }
    }

    void UltraCanvasParliamentDiagram::DrawSeats(IRenderContext* ctx) {
        const double outlineW = std::clamp(cache.seatRadius * 0.28, 1.0, 2.5);

        for (size_t i = 0; i < cache.seats.size(); ++i) {
            const ParliamentSeat& seat = cache.seats[i];
            Color color = SeatFillColor(i);
            bool vacant = (seat.party < parties.size()) && parties[seat.party].vacant;

            if (vacant) {
                ctx->SetStrokePaint(color);
                ctx->SetStrokeWidth(outlineW);
                DrawSeatShape(ctx, seat.center, std::max(0.5, seat.radius - outlineW / 2.0), false);
            } else {
                ctx->SetFillPaint(color);
                DrawSeatShape(ctx, seat.center, seat.radius, true);
            }

            if (seat.party != SIZE_MAX && seat.party == selectedParty && IsPartyActive(seat.party)) {
                ctx->SetStrokePaint(parties[seat.party].color.Darken(0.4f));
                ctx->SetStrokeWidth(std::max(1.0, outlineW * 0.7));
                DrawSeatShape(ctx, seat.center, seat.radius, false);
            }
        }

        if (hoveredSeat < cache.seats.size()) {
            const ParliamentSeat& seat = cache.seats[hoveredSeat];
            ctx->SetStrokePaint(TextColor());
            ctx->SetStrokeWidth(1.5);
            DrawSeatShape(ctx, seat.center, seat.radius + 2.0, false);
        }
    }

    void UltraCanvasParliamentDiagram::DrawSpeakerChair(IRenderContext* ctx) {
        const Rect2Dd& r = cache.speakerRect;
        if (r.width <= 0.0 || r.height <= 0.0) return;

        ctx->SetFillPaint(darkTheme ? Color(48, 52, 62, 255) : Color(238, 240, 244, 255));
        ctx->FillRoundedRectangle(r, 5.0);
        ctx->SetStrokePaint(MutedTextColor());
        ctx->SetStrokeWidth(1.0);
        ctx->DrawRoundedRectangle(r, 5.0);

        ctx->SetTextPaint(MutedTextColor());
        ctx->SetFontSize(std::max(7.0f, legendFontSize * 0.85f));
        ctx->SetFontWeight(FontWeight::Normal);
        ctx->SetFontSlant(FontSlant::Normal);
        const std::string text = "Speaker";
        Size2Di s = ctx->GetTextLineDimensions(text);
        if (s.width + 8 <= r.width && s.height <= r.height) {
            ctx->DrawText(text, Point2Dd(r.x + (r.width - s.width) / 2.0, r.y + (r.height - s.height) / 2.0));
        }
    }

// =============================================================================
// RENDERING - MARKERS, LABELS, LEGEND
// =============================================================================

    void UltraCanvasParliamentDiagram::DrawMajorityMarker(IRenderContext* ctx) {
        const double a = cache.majorityAngle;
        const double ca = std::cos(a);
        const double sa = std::sin(a);
        const double r0 = std::max(0.0, cache.innerRadius - 8.0);
        const double r1 = cache.outerRadius + 8.0;
        const Point2Dd from(cache.center.x + r0 * ca, cache.center.y - r0 * sa);
        const Point2Dd to(cache.center.x + r1 * ca, cache.center.y - r1 * sa);

        ctx->SetStrokePaint(MutedTextColor());
        ctx->SetStrokeWidth(1.5);
        ctx->SetLineDash(UCDashPattern({4.0, 3.0}));
        ctx->DrawLine(from, to);
        ctx->SetLineDash(UCDashPattern());

        std::string text = majorityCaption;
        if (!text.empty()) text += " ";
        text += std::to_string(GetMajorityThreshold());

        ctx->SetTextPaint(MutedTextColor());
        ctx->SetFontSize(majorityFontSize);
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetFontSlant(FontSlant::Normal);
        Size2Di s = ctx->GetTextLineDimensions(text);

        // Push the label outwards along the marker so it clears the seats
        const double r2 = cache.outerRadius + 12.0;
        const double px = cache.center.x + r2 * ca;
        const double py = cache.center.y - r2 * sa;
        double tx = px + ca * s.width / 2.0 - s.width / 2.0;
        double ty = py - sa * s.height / 2.0 - s.height / 2.0;
        tx = std::clamp(tx, 2.0, std::max(2.0, GetWidth() - s.width - 2.0));
        ctx->DrawText(text, Point2Dd(tx, ty));
        ctx->SetFontWeight(FontWeight::Normal);
    }

    void UltraCanvasParliamentDiagram::DrawTotalLabel(IRenderContext* ctx) {
        const std::string number = std::to_string(GetTotalSeats());
        const bool arcLayout = (layout == ParliamentLayout::Hemicycle || layout == ParliamentLayout::Circle);

        float numberSize = totalFontSize;
        float captionSize = std::max(7.0f, legendFontSize);
        if (arcLayout) {
            // Fit the block into the hole in the middle
            double room = cache.innerRadius * (layout == ParliamentLayout::Circle ? 1.6 : 0.95);
            double blockH = numberSize * 1.15 + (totalCaption.empty() ? 0.0 : captionSize * 1.4);
            if (blockH > room && blockH > 0.0) {
                double k = room / blockH;
                numberSize = std::max(7.0f, static_cast<float>(numberSize * k));
                captionSize = std::max(6.0f, static_cast<float>(captionSize * k));
            }
        }

        ctx->SetTextPaint(TextColor());
        ctx->SetFontSize(numberSize);
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetFontSlant(FontSlant::Normal);
        Size2Di ns = ctx->GetTextLineDimensions(number);

        Size2Di cs(0, 0);
        if (!totalCaption.empty()) {
            ctx->SetFontSize(captionSize);
            ctx->SetFontWeight(FontWeight::Normal);
            cs = ctx->GetTextLineDimensions(totalCaption);
        }

        const double blockH = ns.height + (cs.height > 0 ? cs.height + 2.0 : 0.0);
        const double top = cache.totalLabelCenter.y - blockH / 2.0;

        ctx->SetFontSize(numberSize);
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->DrawText(number, Point2Dd(cache.totalLabelCenter.x - ns.width / 2.0, top));

        if (cs.height > 0) {
            ctx->SetTextPaint(MutedTextColor());
            ctx->SetFontSize(captionSize);
            ctx->SetFontWeight(FontWeight::Normal);
            ctx->DrawText(totalCaption, Point2Dd(cache.totalLabelCenter.x - cs.width / 2.0, top + ns.height + 2.0));
        }
        ctx->SetFontWeight(FontWeight::Normal);
    }

    void UltraCanvasParliamentDiagram::DrawLegend(IRenderContext* ctx) {
        if (cache.legend.empty()) return;

        ctx->SetFontSize(legendFontSize);
        ctx->SetFontWeight(FontWeight::Normal);
        ctx->SetFontSlant(FontSlant::Normal);

        const double swatch = legendFontSize * 1.1;
        const double padX = 10.0;
        const Color bg = BackgroundFill();
        const size_t focus = (hoveredParty != SIZE_MAX) ? hoveredParty : selectedParty;

        for (const auto& e : cache.legend) {
            if (e.party >= parties.size()) continue;
            const ParliamentParty& party = parties[e.party];
            const bool active = IsPartyActive(e.party);

            if (e.party == focus) {
                ctx->SetFillPaint(darkTheme ? Color(255, 255, 255, 28) : Color(0, 0, 0, 18));
                ctx->FillRoundedRectangle(Rect2Dd(e.rect.x + 2.0, e.rect.y + 1.0, e.rect.width - 4.0, e.rect.height - 2.0), 4.0);
            }

            Color color = active ? party.color : party.color.Blend(bg, 0.7f);
            if (highlightGovernment && !party.government && active) color = color.Blend(bg, 0.5f);
            Point2Dd sc(e.rect.x + padX + swatch / 2.0, e.rect.y + e.rect.height / 2.0);
            if (party.vacant) {
                ctx->SetStrokePaint(color);
                ctx->SetStrokeWidth(1.5);
                DrawSeatShape(ctx, sc, swatch / 2.0 - 0.75, false);
            } else {
                ctx->SetFillPaint(color);
                DrawSeatShape(ctx, sc, swatch / 2.0, true);
            }

            ctx->SetTextPaint(active ? TextColor() : MutedTextColor());
            std::string text = LegendText(party);
            Size2Di s = ctx->GetTextLineDimensions(text);
            double maxW = e.rect.width - padX * 2.0 - swatch - 6.0;
            while (s.width > maxW && text.size() > 4) {
                text = text.substr(0, text.size() - 4) + "...";
                s = ctx->GetTextLineDimensions(text);
            }
            ctx->DrawText(text, Point2Dd(e.rect.x + padX + swatch + 6.0, e.rect.y + (e.rect.height - s.height) / 2.0));
        }
    }

// =============================================================================
// EVENT HANDLING
// =============================================================================

    std::string UltraCanvasParliamentDiagram::BuildPartyTooltip(size_t party) const {
        if (party >= parties.size()) return "";
        const ParliamentParty& p = parties[party];
        std::string text = p.name;
        if (!p.abbreviation.empty() && p.abbreviation != p.name) text += " (" + p.abbreviation + ")";
        int total = GetTotalSeats();
        double pct = (total > 0) ? 100.0 * p.seats / total : 0.0;
        text += "\n" + std::to_string(p.seats) + (p.seats == 1 ? " seat (" : " seats (") + FormatPercent(pct) + ")";
        if (p.government) text += "\nGovernment";
        if (p.vacant) text += "\nVacant";
        return text;
    }

    void UltraCanvasParliamentDiagram::SetHoveredParty(size_t party, size_t seat) {
        if (party == hoveredParty && seat == hoveredSeat) return;
        hoveredParty = party;
        hoveredSeat = seat;
        if (hoveredParty < parties.size() && onPartyHover) {
            onPartyHover(hoveredParty, parties[hoveredParty]);
        }
        RequestRedraw();
    }

    bool UltraCanvasParliamentDiagram::HandleChartMouseMove(const Point2Di& mousePos) {
        if (!cache.valid) return false;

        size_t seat = FindSeatAt(mousePos);
        size_t party = (seat != SIZE_MAX) ? cache.seats[seat].party : FindLegendEntryAt(mousePos);
        if (party >= parties.size()) {
            party = SIZE_MAX;
            seat = SIZE_MAX;
        }
        SetHoveredParty(party, seat);

        if (enableTooltips) {
            if (hoveredParty != SIZE_MAX) {
                Point2Di tipPos(mousePos.x + 15, mousePos.y - 20);
                auto windowPos = MapFromLocal(tipPos, nullptr);
                UltraCanvasTooltipManager::UpdateAndShowTooltip(
                        GetWindow(), BuildPartyTooltip(hoveredParty), windowPos);
            } else {
                UltraCanvasTooltipManager::HideTooltip();
            }
        }
        return hoveredParty != SIZE_MAX;
    }

    bool UltraCanvasParliamentDiagram::HandleClick(const UCEvent& event) {
        if (!cache.valid) return false;

        Point2Di pos(event.pointer.x, event.pointer.y);
        size_t seat = FindSeatAt(pos);
        size_t party = (seat != SIZE_MAX) ? cache.seats[seat].party : FindLegendEntryAt(pos);

        if (party < parties.size()) {
            if (seat != SIZE_MAX && onSeatClick) onSeatClick(seat, cache.seats[seat]);
            if (onPartyClick) onPartyClick(party, parties[party]);
            if (enableSelection) {
                selectedParty = (selectedParty == party) ? SIZE_MAX : party;
                if (onSelectionChange) onSelectionChange();
                RequestRedraw();
            }
            return true;
        }

        if (enableSelection && selectedParty != SIZE_MAX) {
            ClearSelection();
            if (onSelectionChange) onSelectionChange();
            return true;
        }
        return false;
    }

    bool UltraCanvasParliamentDiagram::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseDown:
                if (event.button == UCMouseButton::Left && HandleClick(event)) {
                    return true;
                }
                return UltraCanvasChartElementBase::OnEvent(event);

            case UCEventType::MouseLeave:
                if (hoveredParty != SIZE_MAX || hoveredSeat != SIZE_MAX) {
                    hoveredParty = SIZE_MAX;
                    hoveredSeat = SIZE_MAX;
                    RequestRedraw();
                }
                UltraCanvasTooltipManager::HideTooltip();
                return false;

            default:
                return UltraCanvasChartElementBase::OnEvent(event);
        }
    }

// =============================================================================
// SAMPLE DATA
// =============================================================================

    namespace ParliamentDiagramSamples {

        std::vector<ParliamentParty> Bundestag2021() {
            return {
                ParliamentParty("Die Linke", "Linke", 39, Color(190, 30, 110, 255)),
                ParliamentParty("Social Democratic Party", "SPD", 206, Color(226, 0, 26, 255), true),
                ParliamentParty("Alliance 90/The Greens", "Greens", 118, Color(70, 150, 40, 255), true),
                ParliamentParty("South Schleswig Voters' Association", "SSW", 1, Color(0, 60, 120, 255)),
                ParliamentParty("Free Democratic Party", "FDP", 92, Color(255, 204, 0, 255), true),
                ParliamentParty("CDU/CSU", "CDU/CSU", 197, Color(50, 50, 50, 255)),
                ParliamentParty("Alternative for Germany", "AfD", 83, Color(0, 150, 220, 255))
            };
        }

        std::vector<ParliamentParty> EuropeanParliament2024() {
            return {
                ParliamentParty("The Left in the European Parliament", "The Left", 46, Color(150, 20, 70, 255)),
                ParliamentParty("Progressive Alliance of Socialists and Democrats", "S&D", 136, Color(230, 30, 30, 255)),
                ParliamentParty("Greens/European Free Alliance", "Greens/EFA", 53, Color(80, 160, 50, 255)),
                ParliamentParty("Renew Europe", "Renew", 77, Color(255, 200, 0, 255)),
                ParliamentParty("European People's Party", "EPP", 188, Color(40, 110, 210, 255)),
                ParliamentParty("European Conservatives and Reformists", "ECR", 78, Color(20, 60, 130, 255)),
                ParliamentParty("Patriots for Europe", "PfE", 84, Color(30, 30, 90, 255)),
                ParliamentParty("Europe of Sovereign Nations", "ESN", 25, Color(100, 70, 160, 255)),
                ParliamentParty("Non-Inscrits", "NI", 33, Color(150, 150, 150, 255))
            };
        }

        std::vector<ParliamentParty> HouseOfCommons2024() {
            std::vector<ParliamentParty> list = {
                ParliamentParty("Labour", "Lab", 411, Color(228, 0, 59, 255), true),
                ParliamentParty("Conservative", "Con", 121, Color(0, 135, 220, 255)),
                ParliamentParty("Liberal Democrats", "LD", 72, Color(250, 160, 0, 255)),
                ParliamentParty("Scottish National Party", "SNP", 9, Color(255, 220, 0, 255)),
                ParliamentParty("Reform UK", "Reform", 5, Color(18, 180, 210, 255)),
                ParliamentParty("Green Party", "Green", 4, Color(100, 180, 60, 255)),
                ParliamentParty("Plaid Cymru", "PC", 4, Color(0, 128, 80, 255)),
                ParliamentParty("Sinn Fein", "SF", 7, Color(50, 140, 60, 255)),
                ParliamentParty("Independent", "Ind", 6, Color(150, 150, 150, 255)),
                ParliamentParty("Democratic Unionist Party", "DUP", 5, Color(140, 0, 30, 255)),
                ParliamentParty("Social Democratic and Labour Party", "SDLP", 2, Color(60, 150, 80, 255)),
                ParliamentParty("Alliance", "APNI", 1, Color(200, 170, 0, 255)),
                ParliamentParty("Ulster Unionist Party", "UUP", 1, Color(70, 110, 180, 255)),
                ParliamentParty("Traditional Unionist Voice", "TUV", 1, Color(20, 40, 90, 255)),
                ParliamentParty("Speaker", "Speaker", 1, Color(120, 120, 120, 255))
            };
            list.back().vacant = true;
            return list;
        }

    } // namespace ParliamentDiagramSamples

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

    std::shared_ptr<UltraCanvasParliamentDiagram> CreateParliamentDiagram(
            const std::string& id, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasParliamentDiagram>(id, x, y, width, height);
    }

    std::shared_ptr<UltraCanvasParliamentDiagram> CreateParliamentDiagram(
            const std::string& id, int x, int y, int width, int height,
            ParliamentLayout layout) {
        auto element = std::make_shared<UltraCanvasParliamentDiagram>(id, x, y, width, height);
        element->SetLayout(layout);
        return element;
    }

} // namespace UltraCanvas
