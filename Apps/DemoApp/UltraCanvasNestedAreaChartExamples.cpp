// Apps/DemoApp/UltraCanvasNestedAreaChartExamples.cpp
// Nested Proportional Area Chart examples for the UltraCanvas Demo App
// Programmer's guide: Docs/UltraCanvas/UltraCanvasNestedAreaChartExamples.md
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): the controls,
//                  each chart and the features in cards that wrap, every
//                  label sized to its text
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include "Plugins/Charts/UltraCanvasNestedAreaChart.h"
#include <memory>
#include <vector>

namespace UltraCanvas {

namespace {

    // A chart card in a row asks for the chart's width plus its own padding
    // and border before it wraps under its neighbour.
    constexpr float kNestedChartCardWidth = 480 + 2 * DemoKit::kCardPadding + 2;

    // "Theme:" and its dropdown, kept together when the controls wrap.
    void AddLabelledControl(const std::shared_ptr<UltraCanvasContainer>& row, const std::string& id,
                            const std::string& text, const std::shared_ptr<UltraCanvasUIElement>& control) {
        auto pair = DemoKit::CreateRow(id + "Pair", 6, false);
        pair->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
        DemoKit::AddText(pair, id, text)->SetFontSize(11);
        DemoKit::Add(pair, control);
        DemoKit::Add(row, pair);
    }

} // namespace

// =============================================================================
// NESTED AREA CHART EXAMPLES PAGE
// =============================================================================

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateNestedAreaChartExamples() {
        auto container = DemoKit::CreatePage("NestedAreaChartDemo", "Nested Proportional Area Chart Examples", "");

        auto controlsCard = DemoKit::AddCard(container, "NestedControlsCard", "Interactive controls");
        auto controlsRow = DemoKit::AddRow(controlsCard, "NestedControlsRow", 20);
        controlsRow->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);

        auto topRow = DemoKit::AddRow(container, "NestedTopRow");
        auto bottomRow = DemoKit::AddRow(container, "NestedBottomRow");

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

        auto rectangleCard = DemoKit::AddCard(topRow, "nestedRectCard", "Rectangle mode",
                "Country land areas, aligned bottom-left", kNestedChartCardWidth);
        auto rectangleChart = CreateNestedAreaChartElement(
                "nestedRectChart", 0, 0, 480, 320);
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
        DemoKit::AddFixed(rectangleCard, rectangleChart);

        // =============================================================================
        // EXAMPLE 2: CIRCLE MODE - MARKET CAPITALIZATION
        // =============================================================================

        auto circleCard = DemoKit::AddCard(topRow, "nestedCircleCard", "Circle mode",
                "Market capitalization as concentric circles", kNestedChartCardWidth);
        auto circleChart = CreateNestedAreaChartElement(
                "nestedCircleChart", 0, 0, 480, 320);
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
        DemoKit::AddFixed(circleCard, circleChart);

        // =============================================================================
        // EXAMPLE 3: THEME SHOWCASE - ROUNDED RECTANGLES, POPULATION DATA
        // =============================================================================

        auto themeCard = DemoKit::AddCard(bottomRow, "nestedThemeCard", "Theme showcase",
                "Rounded rectangles over population data", kNestedChartCardWidth);
        auto themeChart = CreateNestedAreaChartElement(
                "nestedThemeChart", 0, 0, 480, 330);
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
        DemoKit::AddFixed(themeCard, themeChart);

        // =============================================================================
        // INTERACTIVE CONTROLS
        // =============================================================================

        auto themeDropdown = std::make_shared<UltraCanvasDropdown>(
                "nestedThemeDropdown", 0, 0, 160, 28);
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
        AddLabelledControl(controlsRow, "nestedThemeLabel", "Theme:", themeDropdown);

        auto shapeDropdown = std::make_shared<UltraCanvasDropdown>(
                "nestedShapeDropdown", 0, 0, 120, 28);
        shapeDropdown->AddItem("Rectangle");
        shapeDropdown->AddItem("Circle");
        shapeDropdown->AddItem("Rounded");
        shapeDropdown->SetSelectedIndex(2);  // RoundedRect - matches the showcase chart
        AddLabelledControl(controlsRow, "nestedShapeLabel", "Shape:", shapeDropdown);

        auto alignmentDropdown = std::make_shared<UltraCanvasDropdown>(
                "nestedAlignDropdown", 0, 0, 130, 28);
        alignmentDropdown->AddItem("Bottom Left");
        alignmentDropdown->AddItem("Bottom Right");
        alignmentDropdown->AddItem("Top Left");
        alignmentDropdown->AddItem("Top Right");
        alignmentDropdown->AddItem("Center");
        alignmentDropdown->SetSelectedIndex(0);
        AddLabelledControl(controlsRow, "nestedAlignLabel", "Align:", alignmentDropdown);

        DemoKit::AddCaption(controlsCard, "nestedHintLabel",
                "Theme/Shape apply to the population chart. Hover & click shapes!");

        // Status label - shows hover/click feedback from the charts
        auto statusLabel = DemoKit::AddText(controlsCard, "nestedStatusLabel",
                "Click a shape to select it, hover for tooltips.");
        statusLabel->SetFontSize(11);
        statusLabel->SetTextColor(Color(60, 60, 60, 255));

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

        auto infoCard = DemoKit::AddNotes(bottomRow, "NestedAreaInfo", "Nested Area Chart Features:", {
                "Shape area is PROPORTIONAL to value "
                "(side/radius = sqrt(value/max) - mathematically correct)",
                "Shape modes: Rectangle, Circle, RoundedRect",
                "7 alignment modes (corners, centers, concentric)",
                "14 predefined color themes + custom colors",
                "Inside/outside labels with automatic contrast",
                "Legend with values, optional reference grid",
                "Hover highlighting and tooltips",
                "Click selection with callbacks: "
                "onShapeHover, onShapeClick, onShapeSelect",
                "Fluent NestedAreaChartBuilder for quick setup"});
        DemoKit::AddCaption(infoCard, "NestedAreaInfoSee",
                "See the Programmer's Guide (doc icon above) and the "
                "example source (C++ icon above) for API details.");

        return container;
    }

} // namespace UltraCanvas
