// Plogins/Charts/UltraCanvasPopulationChart.cpp
// Population pyramid chart implementation
// Version: 1.1.0
// Last Modified: 2026-08-20
// V1.1.0: legend: migrated to the shared ChartLegend component
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasPopulationChart.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasWindow.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace UltraCanvas {

    namespace {

        // Parse the stringly typed legend position onto the shared
        // ChartLegendPosition. Case-insensitive; '-', '_' and spaces are
        // interchangeable. Corner names keep the old floating behavior
        // (Inset placements); plain edge names become outside placements
        // that reserve plot space. Unknown strings fall back to the old
        // default, "top-right".
        ChartLegendPosition ParseLegendPosition(const std::string& position) {
            std::string key;
            key.reserve(position.size());
            for (char c : position) {
                if (c == '-' || c == '_' || c == ' ') continue;
                key.push_back(static_cast<char>(
                        std::tolower(static_cast<unsigned char>(c))));
            }
            if (key == "top")    return ChartLegendPosition::TopCenter;
            if (key == "bottom") return ChartLegendPosition::BottomCenter;
            if (key == "left")   return ChartLegendPosition::LeftCenter;
            if (key == "right")  return ChartLegendPosition::RightCenter;
            if (key == "topleft" || key == "lefttop")
                return ChartLegendPosition::InsetTopLeft;
            if (key == "bottomleft" || key == "leftbottom")
                return ChartLegendPosition::InsetBottomLeft;
            if (key == "bottomright" || key == "rightbottom")
                return ChartLegendPosition::InsetBottomRight;
            return ChartLegendPosition::InsetTopRight;   // "top-right" & default
        }

    } // namespace

// ===== CONSTRUCTOR =====
    UltraCanvasPopulationChart::UltraCanvasPopulationChart(
            const std::string& identifier, int x, int y, int w, int h)
            : UltraCanvasUIElement(identifier, x, y, w, h)
            , chartTitle("")
            , chartSubtitle("")
            , axisLabel("Population")
            , maleBaseColor(100, 150, 200)
            , femaleBaseColor(220, 120, 140)
            , maleSurplusColor(70, 110, 160)
            , femaleSurplusColor(190, 90, 110)
            , backgroundColor(250, 250, 250)
            , gridColor(230, 230, 230)
            , axisColor(100, 100, 100)
            , textColor(50, 50, 50)
            , maxAxisValue(10.0)
            , autoScaleAxis(true)
            , showGrid(true)
            , showAxisLabels(true)
            , showCenterLine(true)
            , barSpacing(2)
            , fontSize(10)
            , titleFontSize(16)
            , chartPaddingLeft(80)
            , chartPaddingRight(40)
            , chartPaddingTop(50)
            , chartPaddingBottom(20)
            , centerX(0)
            , plotWidth(200)
            , hoveredGroupIndex(-1)
            , interactionEnabled(true)
    {
        layoutDirty = true;
        plotArea = Rect2Dd(chartPaddingLeft, chartPaddingTop,
                           std::max(0.0, static_cast<double>(w) -
                                         chartPaddingLeft - chartPaddingRight),
                           std::max(0.0, static_cast<double>(h) -
                                         chartPaddingTop - chartPaddingBottom));

        // Shared legend defaults matching the old private implementation:
        // floating top-right box, 15x15 borderless color squares, chart font
        // size and text color.
        legend.SetPosition(ChartLegendPosition::InsetTopRight);
        ChartLegendStyle legendStyle;
        legendStyle.fontSize = static_cast<float>(fontSize);
        legendStyle.textColor = textColor;
        legendStyle.swatchWidth = 15.0f;
        legendStyle.swatchHeight = 15.0f;
        legendStyle.drawSwatchBorder = false;
        legend.SetStyle(legendStyle);
    }

    UltraCanvasPopulationChart::~UltraCanvasPopulationChart() {
    }

// ===== DATA MANAGEMENT =====
    void UltraCanvasPopulationChart::AddAgeGroup(const std::string& ageLabel,
                                                 double malePopulation,
                                                 double femalePopulation) {
        ageGroups.emplace_back(ageLabel, malePopulation, femalePopulation);
        layoutDirty = true;
    }

    void UltraCanvasPopulationChart::ClearAgeGroups() {
        ageGroups.clear();
        hoveredGroupIndex = -1;
        layoutDirty = true;
    }

// ===== CHART CONFIGURATION =====
    void UltraCanvasPopulationChart::SetTitle(const std::string& title) {
        chartTitle = title;
    }

    void UltraCanvasPopulationChart::SetSubtitle(const std::string& subtitle) {
        chartSubtitle = subtitle;
    }

    void UltraCanvasPopulationChart::SetAxisLabel(const std::string& label) {
        axisLabel = label;
    }

    void UltraCanvasPopulationChart::SetMaxAxisValue(double maxValue) {
        maxAxisValue = maxValue;
        autoScaleAxis = false;
    }

    void UltraCanvasPopulationChart::EnableAutoScale(bool enable) {
        autoScaleAxis = enable;
        if (enable) {
            CalculateAutoScale();
        }
    }

// ===== COLOR CONFIGURATION =====
    void UltraCanvasPopulationChart::SetMaleColor(const Color& color) {
        maleBaseColor = color;
    }

    void UltraCanvasPopulationChart::SetFemaleColor(const Color& color) {
        femaleBaseColor = color;
    }

    void UltraCanvasPopulationChart::SetMaleSurplusColor(const Color& color) {
        maleSurplusColor = color;
    }

    void UltraCanvasPopulationChart::SetFemaleSurplusColor(const Color& color) {
        femaleSurplusColor = color;
    }

    void UltraCanvasPopulationChart::SetBackgroundColor(const Color& color) {
        backgroundColor = color;
    }

    void UltraCanvasPopulationChart::SetGridColor(const Color& color) {
        gridColor = color;
    }

    void UltraCanvasPopulationChart::SetAxisColor(const Color& color) {
        axisColor = color;
    }

    void UltraCanvasPopulationChart::SetTextColor(const Color& color) {
        textColor = color;
        legend.GetStyle().textColor = color;
    }

// ===== DISPLAY OPTIONS =====
    void UltraCanvasPopulationChart::EnableGrid(bool enable) {
        showGrid = enable;
    }

    void UltraCanvasPopulationChart::EnableAxisLabels(bool enable) {
        showAxisLabels = enable;
    }

    void UltraCanvasPopulationChart::EnableCenterLine(bool enable) {
        showCenterLine = enable;
    }

    void UltraCanvasPopulationChart::SetBarSpacing(int spacing) {
        barSpacing = spacing;
        layoutDirty = true;
    }

    void UltraCanvasPopulationChart::SetFontSize(int size) {
        fontSize = size;
        legend.GetStyle().fontSize = static_cast<float>(size);
    }

// ===== LEGEND CONFIGURATION =====
    void UltraCanvasPopulationChart::SetLegendPosition(const std::string& position) {
        // Public API stays string-based; parsed onto the shared position enum.
        legend.SetPosition(ParseLegendPosition(position));
        layoutDirty = true;
    }

    void UltraCanvasPopulationChart::EnableLegend(bool enable) {
        legend.SetVisible(enable);
        layoutDirty = true;
    }

    void UltraCanvasPopulationChart::AddLegendItem(const std::string& label, const Color& color) {
        legend.AddEntry(ChartLegendEntry(label, color, LegendSwatch::Square));
        layoutDirty = true;
    }

    void UltraCanvasPopulationChart::ClearLegend() {
        legend.ClearEntries();
        layoutDirty = true;
    }

// ===== LAYOUT CONFIGURATION =====
    void UltraCanvasPopulationChart::SetPadding(int left, int right, int top, int bottom) {
        chartPaddingLeft = left;
        chartPaddingRight = right;
        chartPaddingTop = top;
        chartPaddingBottom = bottom;
        layoutDirty = true;
    }

    void UltraCanvasPopulationChart::SetPlotWidth(int width) {
        plotWidth = width;
    }

// ===== INTERACTION =====
    void UltraCanvasPopulationChart::EnableInteraction(bool enable) {
        interactionEnabled = enable;
    }

    int UltraCanvasPopulationChart::HitTest(int x, int y) {
        return GetAgeGroupIndexAt(x, y);
    }

// ===== DATA ACCESS =====
    const PopulationAgeGroup& UltraCanvasPopulationChart::GetAgeGroup(size_t index) const {
        static PopulationAgeGroup emptyGroup;
        if (index >= ageGroups.size()) {
            return emptyGroup;
        }
        return ageGroups[index];
    }

    double UltraCanvasPopulationChart::GetTotalMalePopulation() const {
        double total = 0.0;
        for (const auto& group : ageGroups) {
            total += group.MalePopulation;
        }
        return total;
    }

    double UltraCanvasPopulationChart::GetTotalFemalePopulation() const {
        double total = 0.0;
        for (const auto& group : ageGroups) {
            total += group.FemalePopulation;
        }
        return total;
    }

    double UltraCanvasPopulationChart::GetTotalPopulation() const {
        return GetTotalMalePopulation() + GetTotalFemalePopulation();
    }

// ===== INTERNAL CALCULATION METHODS =====
    void UltraCanvasPopulationChart::CalculateLayout() {
        // plotArea is the padded content area minus whatever an outside
        // legend placement consumed (see Render). Note: the pyramid is now
        // centered within the padded plot area rather than the full element
        // width, so asymmetric paddings shift the center line accordingly.
        centerX = plotArea.x + plotArea.width / 2;
        plotWidth = plotArea.width / 2;
        if (!ageGroups.empty()) {
            barHeight = static_cast<int>(plotArea.height / ageGroups.size()) - barSpacing;
        } else {
            barHeight = 25;
        }
    }

    void UltraCanvasPopulationChart::CalculateAutoScale() {
        double maxValue = 0.0;
        for (const auto& group : ageGroups) {
            maxValue = std::max(maxValue, std::max(group.MalePopulation, group.FemalePopulation));
        }
        maxAxisValue = maxValue * 1.1;
    }

    double UltraCanvasPopulationChart::ValueToPixels(double value) {
        if (maxAxisValue <= 0.0) return 0.0;
        return (value / maxAxisValue) * plotWidth;
    }

    int UltraCanvasPopulationChart::GetAgeGroupIndexAt(int x, int y) {
        if (barHeight + barSpacing <= 0) return -1;
        int relY = y - static_cast<int>(plotArea.y);
        int index = relY / (barHeight + barSpacing);

        if (index >= 0 && index < static_cast<int>(ageGroups.size())) {
            return index;
        }
        return -1;
    }

// ===== RENDERING METHODS =====
    void UltraCanvasPopulationChart::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        if (!IsVisible()) {
            return;
        }

        // Shared legend: measure first so outside placements reserve plot
        // space (RemainingArea); inset placements float over the plot as the
        // old private legend did.
        Rect2Dd contentArea(chartPaddingLeft, chartPaddingTop,
                            std::max(0.0f, GetWidth() - chartPaddingLeft -
                                           chartPaddingRight),
                            std::max(0.0f, GetHeight() - chartPaddingTop -
                                           chartPaddingBottom));
        ctx->PushState();
        legend.Measure(ctx, contentArea);
        ctx->PopState();
        plotArea = legend.RemainingArea(contentArea);
        CalculateLayout();
        if (layoutDirty) {
            if (autoScaleAxis) {
                CalculateAutoScale();
            }
            layoutDirty = false;
        }

        RenderBackground(ctx);
        RenderTitle(ctx);
        RenderGrid(ctx);
        RenderAxes(ctx);
        RenderAgeGroups(ctx);
        if (showCenterLine) {
            RenderCenterLine(ctx);
        }
        legend.Render(ctx, contentArea);
    }

    void UltraCanvasPopulationChart::RenderBackground(IRenderContext* ctx) {
        ctx->SetFillPaint(backgroundColor);
        ctx->FillRectangle(Rect2Dd(0, 0, GetWidth(), GetHeight()));
    }

    void UltraCanvasPopulationChart::RenderTitle(IRenderContext* ctx) {
        if (chartTitle.empty() && chartSubtitle.empty()) {
            return;
        }

        ctx->SetTextPaint(textColor);
        ctx->SetFontSize(titleFontSize);

        if (!chartTitle.empty()) {
            double titleWidth = ctx->GetTextLineWidth(chartTitle);
            double titleX = (GetWidth() - titleWidth) / 2;
            ctx->DrawText(chartTitle, Point2Dd(titleX, 5));
        }

        if (!chartSubtitle.empty()) {
            ctx->SetFontSize(fontSize);
            double subtitleWidth = ctx->GetTextLineWidth(chartSubtitle);
            double subtitleX = (GetWidth() - subtitleWidth) / 2;
            ctx->DrawText(chartSubtitle, {subtitleX, 30});
        }
    }

    void UltraCanvasPopulationChart::RenderAxes(IRenderContext* ctx) {
        if (!showAxisLabels) {
            return;
        }

        ctx->SetStrokePaint(axisColor);
        ctx->SetStrokeWidth(1.0f);

        // Draw horizontal axis values
        int numTicks = 5;
        ctx->SetFontSize(fontSize - 1);

        for (int i = 0; i <= numTicks; i++) {
            double value = (maxAxisValue / numTicks) * i;
            int pixelPos = static_cast<int>(ValueToPixels(value));

            // Left side (males)
            int leftX = centerX - pixelPos;
            DrawAxisValue(ctx, value, leftX, plotArea.y + plotArea.height);

            // Right side (females)
            int rightX = centerX + pixelPos;
            DrawAxisValue(ctx, value, rightX, plotArea.y + plotArea.height);
        }
    }

    void UltraCanvasPopulationChart::RenderGrid(IRenderContext* ctx) {
        if (!showGrid) {
            return;
        }

        ctx->SetStrokePaint(gridColor);
        ctx->SetStrokeWidth(0.5f);

        int numGridLines = 5;
        for (int i = 1; i <= numGridLines; i++) {
            double value = (maxAxisValue / numGridLines) * i;
            double pixelPos = ValueToPixels(value);

            // Left side grid line
            double leftX = centerX - pixelPos;
            ctx->DrawLine({leftX, plotArea.y},
                          {leftX, plotArea.y + plotArea.height});

            // Right side grid line
            double rightX = centerX + pixelPos;
            ctx->DrawLine({rightX, plotArea.y},
                          {rightX, plotArea.y + plotArea.height});
        }
    }

    void UltraCanvasPopulationChart::RenderAgeGroups(IRenderContext* ctx) {
        int currentY = static_cast<int>(plotArea.y);
        if (ageGroups.empty()) {
            return;
        }
        int i = ageGroups.size();
        do {
            i--;
            RenderAgeGroup(ctx, ageGroups[i], currentY);
            currentY += barHeight + barSpacing;
        } while (i > 0);
    }

    void UltraCanvasPopulationChart::RenderAgeGroup(IRenderContext* ctx,
                                                    const PopulationAgeGroup& group,
                                                    int yPosition) {

        // Draw male bar (left side)
        int maleWidth = static_cast<int>(ValueToPixels(group.MalePopulation));
        int maleX = centerX - maleWidth;
        DrawHorizontalBar(ctx, maleX, yPosition, maleWidth, barHeight, maleBaseColor, false);

        // Draw male surplus if exists
        if (group.MaleSurplus > 0.0) {
            int surplusWidth = static_cast<int>(ValueToPixels(group.MaleSurplus));
            int surplusX = centerX - surplusWidth - maleWidth;
            DrawHorizontalBar(ctx, surplusX, yPosition, surplusWidth, barHeight,
                              maleSurplusColor, false);
        }

        // Draw female bar (right side)
        int femaleWidth = static_cast<int>(ValueToPixels(group.FemalePopulation));
        DrawHorizontalBar(ctx, centerX, yPosition, femaleWidth, barHeight, femaleBaseColor, true);

        // Draw female surplus if exists
        if (group.FemaleSurplus > 0.0) {
            int surplusWidth = static_cast<int>(ValueToPixels(group.FemaleSurplus));
            DrawHorizontalBar(ctx, centerX + femaleWidth, yPosition, surplusWidth, barHeight,
                              femaleSurplusColor, true);
        }
        // Draw age label in center
        Size2Di labelSize = ctx->GetTextLineDimensions(group.AgeLabel);
        double labelX = centerX - labelSize.width / 2;
        double labelY = yPosition + barHeight / 2 - labelSize.height / 2;
        ctx->SetTextPaint(textColor);
        ctx->SetFontSize(fontSize);
        ctx->DrawText(group.AgeLabel, {labelX, labelY});
    }

    void UltraCanvasPopulationChart::RenderCenterLine(IRenderContext* ctx) {
        ctx->SetStrokePaint(axisColor);
        ctx->SetStrokeWidth(2.0f);
        ctx->DrawLine(Point2Dd(centerX, plotArea.y),
                      Point2Dd(centerX, plotArea.y + plotArea.height));
    }

    void UltraCanvasPopulationChart::RenderTooltip(IRenderContext* ctx, int groupIndex,
                                                   int mouseX, int mouseY) {
        // Tooltip implementation for future enhancement
    }

// ===== HELPER METHODS =====
    void UltraCanvasPopulationChart::DrawHorizontalBar(IRenderContext* ctx, float x, float y,
                                                       float width, float height,
                                                       const Color& color, bool rightSide) {
        ctx->SetFillPaint(color);
        ctx->FillRectangle(Rect2Dd(x, y, width, height));

        // Draw subtle border
        ctx->SetStrokePaint(Color(color.r * 0.8, color.g * 0.8, color.b * 0.8));
        ctx->SetStrokeWidth(1.0f);
        ctx->DrawRectangle(Rect2Dd(x, y, width, height));
    }

    void UltraCanvasPopulationChart::DrawAgeLabel(IRenderContext* ctx,
                                                  const std::string& label, float y) {
        ctx->SetTextPaint(textColor);
        ctx->SetFontSize(fontSize);
        ctx->DrawText(label, {chartPaddingLeft - 50, y});
    }

    void UltraCanvasPopulationChart::DrawAxisValue(IRenderContext* ctx, double value,
                                                   float x, float y) {
        std::string valueStr = PopulationChartUtils::FormatPopulation(value);
        int textWidth = ctx->GetTextLineWidth(valueStr);
        ctx->DrawText(valueStr, {x - textWidth / 2, y});
    }

// ===== EVENT HANDLING =====
    bool UltraCanvasPopulationChart::OnEvent(const UCEvent& event) {
        if (UltraCanvasUIElement::OnEvent(event)) {
            return true;
        }        

        if (!interactionEnabled) {
            return false;
        }

        if (event.type == UCEventType::MouseMove) {
            int newHoveredIndex = GetAgeGroupIndexAt(event.pointer.x, event.pointer.y);

            if (newHoveredIndex != hoveredGroupIndex) {
                hoveredGroupIndex = newHoveredIndex;
                RequestRedraw();
            }
            return true;
        }

        return false;
    }

// ===== UTILITY FUNCTIONS =====
    namespace PopulationChartUtils {

        std::string FormatPopulation(double value) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1);

            if (value >= 1000000000.0) {
                oss << (value / 1000000000.0) << "B";
            } else if (value >= 1000000.0) {
                oss << (value / 1000000.0) << "M";
            } else if (value >= 1000.0) {
                oss << (value / 1000.0) << "K";
            } else {
                oss << value;
            }

            return oss.str();
        }

        double CalculateGenderRatio(double males, double females) {
            if (females == 0.0) return 0.0;
            return males / females;
        }

        std::vector<std::string> GenerateAgeLabels(int minAge, int maxAge, int groupSize) {
            std::vector<std::string> labels;
            for (int age = minAge; age <= maxAge; age += groupSize) {
                std::ostringstream oss;
                oss << age << "-" << (age + groupSize - 1);
                labels.push_back(oss.str());
            }
            return labels;
        }

        DemographicStats CalculateStatistics(const std::vector<PopulationAgeGroup>& ageGroups) {
            DemographicStats stats;
            stats.MalePopulation = 0.0;
            stats.FemalePopulation = 0.0;

            for (const auto& group : ageGroups) {
                stats.MalePopulation += group.MalePopulation;
                stats.FemalePopulation += group.FemalePopulation;
            }

            stats.TotalPopulation = stats.MalePopulation + stats.FemalePopulation;

            if (stats.TotalPopulation > 0.0) {
                stats.MalePercentage = (stats.MalePopulation / stats.TotalPopulation) * 100.0;
                stats.FemalePercentage = (stats.FemalePopulation / stats.TotalPopulation) * 100.0;
            }

            stats.GenderRatio = CalculateGenderRatio(stats.MalePopulation, stats.FemalePopulation);

            return stats;
        }

        Color InterpolateGenderColor(const Color& baseColor, double intensity) {
            intensity = std::max(0.0, std::min(1.0, intensity));
            return Color(
                    static_cast<int>(baseColor.r * intensity),
                    static_cast<int>(baseColor.g * intensity),
                    static_cast<int>(baseColor.b * intensity),
                    baseColor.a
            );
        }

    } // namespace PopulationChartUtils

} // namespace UltraCanvas