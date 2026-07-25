// Apps/DemoApp/UltraCanvasNestedAreaChartExamples.cpp
// Nested Proportional Area Chart examples for the UltraCanvas Demo App
// Programmer's guide: Docs/UltraCanvas/UltraCanvasNestedAreaChartExamples.md
// Version: 1.0.0
// Last Modified: 2026-07-25
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include "Plugins/Charts/UltraCanvasNestedAreaChart.h"
#include <memory>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// NESTED AREA CHART EXAMPLES PAGE
// =============================================================================

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateNestedAreaChartExamples() {
        // Main container
        auto container = std::make_shared<UltraCanvasContainer>(
                "NestedAreaChartDemo", 0, 0, 1000, 780
        );

        // Title label
        auto titleLabel = std::make_shared<UltraCanvasLabel>(
                "NestedAreaTitle", 20, 10, 960, 30
        );
        titleLabel->SetText("Nested Proportional Area Chart Examples");
        titleLabel->SetFontSize(16);
        titleLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(titleLabel);

        // =============================================================================
        // SAMPLE DATA
        // =============================================================================

        // Country land area comparison (in million km²) - classic use case for
        // nested area charts: comparing relative magnitudes at a glance
        std::vector<NestedAreaDataPoint> countrySizeData = {
                {"Russia", 17.1}, {"Canada", 10.0}, {"USA", 9.8}, {"China", 9.6},
                {"Brazil", 8.5}, {"Australia", 7.7}, {"India", 3.3}
        };
        for (auto& point : countrySizeData) {
            point.unit = "M km²";
            point.category = "Land Area";
        }

        // Company market capitalization (in trillion $)
        std::vector<NestedAreaDataPoint> marketCapData = {
                {"Apple", 3.0}, {"Microsoft", 2.8}, {"Saudi Aramco", 2.1},
                {"Alphabet", 1.7}, {"Amazon", 1.5}, {"NVIDIA", 1.2}
        };
        for (auto& point : marketCapData) {
            point.unit = "T$";
            point.category = "Market Cap";
        }

        // Population comparison (in billions)
        std::vector<NestedAreaDataPoint> populationData = {
                {"World", 8.0}, {"Asia", 4.7}, {"China", 1.4},
                {"EU", 0.45}, {"USA", 0.33}, {"Japan", 0.12}
        };
        for (auto& point : populationData) {
            point.unit = "B";
            point.category = "Population";
        }

        // =============================================================================
        // EXAMPLE 1: RECTANGLE MODE - COUNTRY LAND AREAS
        // =============================================================================

        auto rectangleChart = CreateNestedAreaChartElement(
                "nestedRectChart", 10, 90, 480, 320);
        rectangleChart->SetChartTitle("Country Land Area (M km²)");
        rectangleChart->SetShapeMode(NestedAreaShapeMode::Rectangle);
        rectangleChart->SetAlignmentMode(NestedAreaAlignmentMode::BottomLeft);
        rectangleChart->SetColorTheme(NestedAreaColorTheme::Default);
        rectangleChart->SetShowLabels(true);
        rectangleChart->SetShowValues(true);
        rectangleChart->SetShowLegend(true);
        rectangleChart->SetShowBorders(true);
        rectangleChart->SetBorderColor(Color(100, 100, 100, 200));
        rectangleChart->SetDataPoints(countrySizeData);
        container->AddChild(rectangleChart);

        // =============================================================================
        // EXAMPLE 2: CIRCLE MODE - MARKET CAPITALIZATION
        // =============================================================================

        auto circleChart = CreateNestedAreaChartElement(
                "nestedCircleChart", 510, 90, 480, 320);
        circleChart->SetChartTitle("Market Capitalization (T$)");
        circleChart->SetShapeMode(NestedAreaShapeMode::Circle);
        circleChart->SetAlignmentMode(NestedAreaAlignmentMode::Center);
        circleChart->SetColorTheme(NestedAreaColorTheme::Categorical);
        circleChart->SetShowLabels(false);   // Concentric circles - legend carries the labels
        circleChart->SetShowValues(false);
        circleChart->SetShowLegend(true);
        circleChart->SetShowBorders(true);
        circleChart->SetBorderColor(Color(60, 60, 60, 200));
        circleChart->SetDataPoints(marketCapData);
        container->AddChild(circleChart);

        // =============================================================================
        // EXAMPLE 3: THEME SHOWCASE - ROUNDED RECTANGLES, POPULATION DATA
        // =============================================================================

        auto themeChart = CreateNestedAreaChartElement(
                "nestedThemeChart", 10, 425, 480, 330);
        themeChart->SetChartTitle("World Population (B)");
        themeChart->SetShapeMode(NestedAreaShapeMode::RoundedRect);
        themeChart->SetAlignmentMode(NestedAreaAlignmentMode::BottomLeft);
        themeChart->SetColorTheme(NestedAreaColorTheme::Sunset);
        themeChart->SetShowLabels(true);
        themeChart->SetShowValues(true);
        themeChart->SetShowLegend(true);
        themeChart->SetCornerRadius(8.0f);
        themeChart->SetShowGridLines(true);
        themeChart->SetDataPoints(populationData);
        container->AddChild(themeChart);

        // =============================================================================
        // INTERACTIVE CONTROLS
        // =============================================================================

        auto themeLabelCtrl = std::make_shared<UltraCanvasLabel>(
                "nestedThemeLabel", 20, 52, 50, 22);
        themeLabelCtrl->SetText("Theme:");
        themeLabelCtrl->SetFontSize(11);
        container->AddChild(themeLabelCtrl);

        auto themeDropdown = std::make_shared<UltraCanvasDropdown>(
                "nestedThemeDropdown", 72, 48, 160, 28);
        themeDropdown->AddItem("Default (Blue)");
        themeDropdown->AddItem("Categorical");
        themeDropdown->AddItem("Sequential");
        themeDropdown->AddItem("Warm");
        themeDropdown->AddItem("Cool");
        themeDropdown->AddItem("Earth");
        themeDropdown->AddItem("Pastel");
        themeDropdown->AddItem("Vibrant");
        themeDropdown->AddItem("Monochrome");
        themeDropdown->AddItem("Ocean");
        themeDropdown->AddItem("Sunset");
        themeDropdown->AddItem("Forest");
        themeDropdown->AddItem("Corporate");
        themeDropdown->AddItem("Colorblind");
        themeDropdown->SetSelectedIndex(10);  // Sunset - matches the showcase chart
        container->AddChild(themeDropdown);

        auto shapeLabelCtrl = std::make_shared<UltraCanvasLabel>(
                "nestedShapeLabel", 252, 52, 45, 22);
        shapeLabelCtrl->SetText("Shape:");
        shapeLabelCtrl->SetFontSize(11);
        container->AddChild(shapeLabelCtrl);

        auto shapeDropdown = std::make_shared<UltraCanvasDropdown>(
                "nestedShapeDropdown", 300, 48, 120, 28);
        shapeDropdown->AddItem("Rectangle");
        shapeDropdown->AddItem("Circle");
        shapeDropdown->AddItem("Rounded");
        shapeDropdown->SetSelectedIndex(2);  // RoundedRect - matches the showcase chart
        container->AddChild(shapeDropdown);

        auto alignLabelCtrl = std::make_shared<UltraCanvasLabel>(
                "nestedAlignLabel", 440, 52, 40, 22);
        alignLabelCtrl->SetText("Align:");
        alignLabelCtrl->SetFontSize(11);
        container->AddChild(alignLabelCtrl);

        auto alignmentDropdown = std::make_shared<UltraCanvasDropdown>(
                "nestedAlignDropdown", 482, 48, 130, 28);
        alignmentDropdown->AddItem("Bottom Left");
        alignmentDropdown->AddItem("Bottom Right");
        alignmentDropdown->AddItem("Top Left");
        alignmentDropdown->AddItem("Top Right");
        alignmentDropdown->AddItem("Center");
        alignmentDropdown->SetSelectedIndex(0);
        container->AddChild(alignmentDropdown);

        auto hintLabel = std::make_shared<UltraCanvasLabel>(
                "nestedHintLabel", 630, 52, 360, 22);
        hintLabel->SetText("Theme/Shape apply to the population chart. Hover & click shapes!");
        hintLabel->SetFontSize(10);
        hintLabel->SetTextColor(Color(120, 120, 120, 255));
        container->AddChild(hintLabel);

        // Status label - shows hover/click feedback from the charts
        auto statusLabel = std::make_shared<UltraCanvasLabel>(
                "nestedStatusLabel", 510, 730, 480, 22);
        statusLabel->SetText("Click a shape to select it, hover for tooltips.");
        statusLabel->SetFontSize(11);
        statusLabel->SetTextColor(Color(60, 60, 60, 255));
        container->AddChild(statusLabel);

        // =============================================================================
        // EVENT HANDLERS
        // =============================================================================

        themeDropdown->onSelectionChanged = [themeChart, statusLabel](int index, const DropdownItem& item) {
            static const NestedAreaColorTheme themes[] = {
                    NestedAreaColorTheme::Default,
                    NestedAreaColorTheme::Categorical,
                    NestedAreaColorTheme::Sequential,
                    NestedAreaColorTheme::Warm,
                    NestedAreaColorTheme::Cool,
                    NestedAreaColorTheme::Earth,
                    NestedAreaColorTheme::Pastel,
                    NestedAreaColorTheme::Vibrant,
                    NestedAreaColorTheme::Monochrome,
                    NestedAreaColorTheme::Ocean,
                    NestedAreaColorTheme::Sunset,
                    NestedAreaColorTheme::Forest,
                    NestedAreaColorTheme::Corporate,
                    NestedAreaColorTheme::Colorblind
            };
            if (index >= 0 && index < 14) {
                themeChart->SetColorTheme(themes[index]);
                statusLabel->SetText("Theme changed to: " + item.text);
            }
        };

        shapeDropdown->onSelectionChanged = [themeChart, statusLabel](int index, const DropdownItem& item) {
            static const NestedAreaShapeMode modes[] = {
                    NestedAreaShapeMode::Rectangle,
                    NestedAreaShapeMode::Circle,
                    NestedAreaShapeMode::RoundedRect
            };
            if (index >= 0 && index < 3) {
                themeChart->SetShapeMode(modes[index]);
                statusLabel->SetText("Shape mode changed to: " + item.text);
            }
        };

        alignmentDropdown->onSelectionChanged =
                [rectangleChart, circleChart, themeChart, statusLabel](int index, const DropdownItem& item) {
            static const NestedAreaAlignmentMode aligns[] = {
                    NestedAreaAlignmentMode::BottomLeft,
                    NestedAreaAlignmentMode::BottomRight,
                    NestedAreaAlignmentMode::TopLeft,
                    NestedAreaAlignmentMode::TopRight,
                    NestedAreaAlignmentMode::Center
            };
            if (index >= 0 && index < 5) {
                rectangleChart->SetAlignmentMode(aligns[index]);
                circleChart->SetAlignmentMode(aligns[index]);
                themeChart->SetAlignmentMode(aligns[index]);
                statusLabel->SetText("Alignment changed to: " + item.text);
            }
        };

        // Chart interaction feedback
        auto reportClick = [statusLabel](const std::shared_ptr<UltraCanvasNestedAreaChart>& chart) {
            return [chart, statusLabel](size_t index) {
                const auto& point = chart->GetDataPoint(index);
                statusLabel->SetText("Clicked: " + point.label + " (" +
                                     std::to_string(point.value) + " " + point.unit + ")");
            };
        };
        rectangleChart->onShapeClick = reportClick(rectangleChart);
        circleChart->onShapeClick = reportClick(circleChart);
        themeChart->onShapeClick = reportClick(themeChart);

        // =============================================================================
        // INFO PANEL
        // =============================================================================

        auto infoLabel = std::make_shared<UltraCanvasLabel>(
                "NestedAreaInfo", 510, 425, 480, 295
        );
        infoLabel->SetText(
                "Nested Area Chart Features:\n\n"
                "• Shape area is PROPORTIONAL to value\n"
                "  (side/radius = sqrt(value/max) - mathematically correct)\n"
                "• Shape modes: Rectangle, Circle, RoundedRect\n"
                "• 7 alignment modes (corners, centers, concentric)\n"
                "• 14 predefined color themes + custom colors\n"
                "• Inside/outside labels with automatic contrast\n"
                "• Legend with values, optional reference grid\n"
                "• Hover highlighting and tooltips\n"
                "• Click selection with callbacks:\n"
                "  onShapeHover, onShapeClick, onShapeSelect\n"
                "• Fluent NestedAreaChartBuilder for quick setup\n\n"
                "See the Programmer's Guide (doc icon above) and the\n"
                "example source (C++ icon above) for API details."
        );
        infoLabel->SetFontSize(11);
        infoLabel->SetTextColor(Color(60, 60, 60, 255));
        infoLabel->SetBackgroundColor(Color(245, 245, 245, 255));
        infoLabel->SetBorders(1.0f, Color(200, 200, 200, 255));
        infoLabel->SetPadding(10.0f);
        container->AddChild(infoLabel);

        return container;
    }

} // namespace UltraCanvas
