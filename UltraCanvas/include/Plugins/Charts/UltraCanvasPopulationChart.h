// include/UltraCanvasPopulationChart.h
// Population pyramid chart component for demographic visualization
// Version: 1.0.0
// Last Modified: 2025-01-19
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasCommonTypes.h"
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

// Legend item for population chart
    struct PopulationLegendItem {
        std::string Label;
        Color ItemColor;

        PopulationLegendItem(const std::string& label, const Color& color)
                : Label(label), ItemColor(color) {}
    };

// Population pyramid chart class - inherits from UltraCanvasUIElement
    class UltraCanvasPopulationChart : public UltraCanvasUIElement {
    private:
        std::vector<PopulationAgeGroup> ageGroups;
        std::vector<PopulationLegendItem> legendItems;

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
        bool layoutDirty = true;
        int barHeight;
        int barSpacing;
        int fontSize;
        int titleFontSize;

        // Layout
        int chartPaddingLeft;
        int chartPaddingRight;
        int chartPaddingTop;
        int chartPaddingBottom;
        int centerX;
        int plotWidth;

        // Legend
        std::string legendPosition;
        bool showLegend;

        // Interaction
        int hoveredGroupIndex;
        bool interactionEnabled;

        // Internal calculation methods
        void CalculateLayout();
        void CalculateAutoScale();
        double ValueToPixels(double value);
        int GetAgeGroupIndexAt(int x, int y);

        // Rendering methods
        void RenderBackground(IRenderContext* ctx);
        void RenderTitle(IRenderContext* ctx);
        void RenderAxes(IRenderContext* ctx);
        void RenderGrid(IRenderContext* ctx);
        void RenderAgeGroups(IRenderContext* ctx);
        void RenderAgeGroup(IRenderContext* ctx, const PopulationAgeGroup& group, int yPosition);
        void RenderCenterLine(IRenderContext* ctx);
        void RenderLegend(IRenderContext* ctx);
        void RenderTooltip(IRenderContext* ctx, int groupIndex, int mouseX, int mouseY);

        // Helper methods
        void DrawHorizontalBar(IRenderContext* ctx, int x, int y, int width, int height,
                               const Color& color, bool rightSide);
        void DrawAgeLabel(IRenderContext* ctx, const std::string& label, int y);
        void DrawAxisValue(IRenderContext* ctx, double value, int x, int y);

    public:
        // Constructor
        UltraCanvasPopulationChart(const std::string& identifier = "PopulationChart",
                                   long id = 0,
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
        void SetBarHeight(int height);
        void SetBarSpacing(int spacing);
        void SetFontSize(int size);

        // Legend configuration
        void SetLegendPosition(const std::string& position);
        void EnableLegend(bool enable);
        void AddLegendItem(const std::string& label, const Color& color);
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

        // Override base class rendering
        void Render() override;
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