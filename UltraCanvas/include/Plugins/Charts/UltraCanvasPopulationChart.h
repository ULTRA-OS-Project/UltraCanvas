// include/Plugins/Charts/UltraCanvasPopulationChart.h
// Population pyramid chart component for demographic visualization
// Version: 1.1.0
// Last Modified: 2026-08-20
// V1.1.0: legend: migrated to the shared ChartLegend component
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasCommonTypes.h"
#include "Plugins/Charts/UltraCanvasChartLegend.h"
#include <string>
#include <vector>

namespace UltraCanvas {

// Forward declaration
    class IRenderContext;

// Population pyramid age group data
    struct PopulationAgeGroup {
        std::string AgeLabel;
        double MalePopulation;
        double FemalePopulation;
        double MaleSurplus;
        double FemaleSurplus;
        int CenterY;

        PopulationAgeGroup()
                : MalePopulation(0.0), FemalePopulation(0.0),
                  MaleSurplus(0.0), FemaleSurplus(0.0), CenterY(0) {}

        PopulationAgeGroup(const std::string& label, double males, double females)
                : AgeLabel(label), MalePopulation(males), FemalePopulation(females),
                  CenterY(0) {
            CalculateSurplus();
        }

        void CalculateSurplus() {
            if (MalePopulation > FemalePopulation) {
                MaleSurplus = MalePopulation - FemalePopulation;
                FemaleSurplus = 0.0;
            } else {
                FemaleSurplus = FemalePopulation - MalePopulation;
                MaleSurplus = 0.0;
            }
        }
    };

// Legend item for population chart. Kept for API compatibility; the chart
// now stores its legend content in the shared ChartLegend component.
    struct PopulationLegendItem {
        std::string Label;
        Color ItemColor;
        bool IsLine;   // rendered as a dashed line sample instead of a color box

        PopulationLegendItem(const std::string& label, const Color& color, bool isLine = false)
                : Label(label), ItemColor(color), IsLine(isLine) {}
    };

// Population pyramid chart class - inherits from UltraCanvasUIElement
    class UltraCanvasPopulationChart : public UltraCanvasUIElement {
    private:
        std::vector<PopulationAgeGroup> ageGroups;

        // Chart properties
        std::string chartTitle;
        std::string chartSubtitle;
        std::string axisLabel;

        // Colors
        Color maleBaseColor;
        Color femaleBaseColor;
        Color maleSurplusColor;
        Color femaleSurplusColor;
        Color backgroundColor;
        Color gridColor;
        Color axisColor;
        Color textColor;

        // Display settings
        double maxAxisValue;
        bool autoScaleAxis;
        bool showGrid;
        bool showAxisLabels;
        bool showCenterLine;
        bool showAverageAgeLine;
        Color maleAverageAgeLineColor;
        Color femaleAverageAgeLineColor;
        bool layoutDirty = true;
        int barHeight;
        int barSpacing;
        int fontSize;
        int titleFontSize;

        // Layout
        float chartPaddingLeft;
        float chartPaddingRight;
        float chartPaddingTop;
        float chartPaddingBottom;
        float centerX;
        float plotWidth;
        // Padded content area minus whatever the legend consumed; resolved
        // each Render() via legend.RemainingArea().
        Rect2Dd plotArea;

        // Legend: the shared ChartLegend component. The public string-based
        // SetLegendPosition() API is parsed onto ChartLegendPosition.
        ChartLegend legend;

        // Interaction
        int hoveredGroupIndex;
        bool interactionEnabled;

        // Internal calculation methods
        void CalculateLayout();
        void CalculateAutoScale();
        double ValueToPixels(double value);
        int GetAgeGroupIndexAt(int x, int y);
        bool GetYPositionForAge(double age, float& outY, int& outGroupIndex);

        // Rendering methods
        void RenderBackground(IRenderContext* ctx);
        void RenderTitle(IRenderContext* ctx);
        void RenderAxes(IRenderContext* ctx);
        void RenderGrid(IRenderContext* ctx);
        void RenderAgeGroups(IRenderContext* ctx);
        void RenderAgeGroup(IRenderContext* ctx, const PopulationAgeGroup& group, int yPosition);
        void RenderCenterLine(IRenderContext* ctx);
        void RenderAverageAgeLines(IRenderContext* ctx);
        void RenderLegend(IRenderContext* ctx);
        void RenderTooltip(IRenderContext* ctx, int groupIndex, int mouseX, int mouseY);

        // Helper methods
        void DrawHorizontalBar(IRenderContext* ctx, float x, float y, float width, float height,
                               const Color& color, bool rightSide);
        void DrawAgeLabel(IRenderContext* ctx, const std::string& label, float y);
        void DrawAxisValue(IRenderContext* ctx, double value, float  x, float y);

    public:
        // Constructor
        UltraCanvasPopulationChart(const std::string& identifier = "PopulationChart",
                                   int x = 0, int y = 0,
                                   int w = 600, int h = 700);

        virtual ~UltraCanvasPopulationChart();

        // Data management
        void AddAgeGroup(const std::string& ageLabel,
                         double malePopulation,
                         double femalePopulation);
        void ClearAgeGroups();
        size_t GetAgeGroupCount() const { return ageGroups.size(); }

        // Chart configuration
        void SetTitle(const std::string& title);
        void SetSubtitle(const std::string& subtitle);
        std::string GetTitle() const { return chartTitle; }
        std::string GetSubtitle() const { return chartSubtitle; }

        // Axis configuration
        void SetAxisLabel(const std::string& label);
        void SetMaxAxisValue(double maxValue);
        void EnableAutoScale(bool enable);
        double GetMaxAxisValue() const { return maxAxisValue; }

        // Color configuration
        void SetMaleColor(const Color& color);
        void SetFemaleColor(const Color& color);
        void SetMaleSurplusColor(const Color& color);
        void SetFemaleSurplusColor(const Color& color);
        Color GetMaleColor() const { return maleBaseColor; }
        Color GetFemaleColor() const { return femaleBaseColor; }

        // Visual settings
        void SetBackgroundColor(const Color& color);
        void SetGridColor(const Color& color);
        void SetAxisColor(const Color& color);
        void SetTextColor(const Color& color);

        // Display options
        void EnableGrid(bool enable);
        void EnableAxisLabels(bool enable);
        void EnableCenterLine(bool enable);
        void EnableAverageAgeLine(bool enable);
        void SetAverageAgeLineColors(const Color& maleColor, const Color& femaleColor);
        void SetBarHeight(int height);
        void SetBarSpacing(int spacing);
        void SetFontSize(int size);

        // Legend configuration
        // Position is stringly typed for API compatibility, case-insensitive:
        // corner names ("top-right" — the default —, "top-left",
        // "bottom-right", "bottom-left") float over the plot; edge names
        // ("top", "bottom", "left", "right") reserve plot space. Unknown
        // strings fall back to the old default, "top-right".
        void SetLegendPosition(const std::string& position);
        void EnableLegend(bool enable);
        void AddLegendItem(const std::string& label, const Color& color);
        void AddLegendLineItem(const std::string& label, const Color& color);
        void ClearLegend();

        // Layout configuration
        void SetPadding(int left, int right, int top, int bottom);
        void SetPlotWidth(int width);

        // Interaction
        void EnableInteraction(bool enable);
        int HitTest(int x, int y);

        // Data access
        const PopulationAgeGroup& GetAgeGroup(size_t index) const;
        double GetTotalMalePopulation() const;
        double GetTotalFemalePopulation() const;
        double GetTotalPopulation() const;
        double GetAverageMaleAge() const;
        double GetAverageFemaleAge() const;

        // Override base class rendering
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;
    };

// Utility functions for population charts
    namespace PopulationChartUtils {
        // Format population values with appropriate units (K, M, B)
        std::string FormatPopulation(double value);

        // Calculate gender ratio
        double CalculateGenderRatio(double males, double females);

        // Generate age group labels
        std::vector<std::string> GenerateAgeLabels(int minAge, int maxAge,
                                                   int groupSize);

        // Parse an age group label like "25-29" or "100+".
        // "25-29" yields lowerAge=25, upperAge=30 (exclusive upper bound);
        // open-ended labels ("100+") yield a negative upperAge.
        bool ParseAgeRange(const std::string& label, double& lowerAge, double& upperAge);

        // Population-weighted average age of one gender.
        // Returns -1.0 when the labels cannot be parsed or the population is zero.
        double CalculateAverageAge(const std::vector<PopulationAgeGroup>& ageGroups,
                                   bool males);

        // Calculate demographic statistics
        struct DemographicStats {
            double TotalPopulation;
            double MalePopulation;
            double FemalePopulation;
            double MalePercentage;
            double FemalePercentage;
            double GenderRatio;
            double MedianAgeGroup;
            int YoungestGroupIndex;
            int OldestGroupIndex;
        };

        DemographicStats CalculateStatistics(
                const std::vector<PopulationAgeGroup>& ageGroups
        );

        // Color interpolation for gradient effects
        Color InterpolateGenderColor(const Color& baseColor,
                                     double intensity);
    }

} // namespace UltraCanvas