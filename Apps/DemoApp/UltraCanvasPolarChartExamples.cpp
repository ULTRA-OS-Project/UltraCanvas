// Apps/DemoApp/UltraCanvasPolarChartExamples.cpp
// Comprehensive polar chart demonstration
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): the controls
//                  in a card beside the six charts, each chart in its own
//                  card, every label and checkbox sized to its text
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Charts/UltraCanvasPolarChart.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include <cmath>
#include <cstdio>
#include <random>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UltraCanvas {

// ===== SAMPLE DATA BUILDERS =====

    // Three signal clusters scattered over the full circle. A fixed seed keeps
    // the demo reproducible between runs.
    static std::shared_ptr<UltraCanvasPolarChart> BuildSignalScatter(
        const std::string& id, int x, int y, int w, int h)
    {
        std::mt19937 gen(1234);
        std::uniform_real_distribution<double> angleDist(0.0, 360.0);

        struct SignalSpec {
            const char* name;
            double meanRadius;
            double spread;
            Color color;
            PolarMarkerShape marker;
        };
        const SignalSpec specs[] = {
            {"Signal A", 4.5, 2.0, Color(102, 178, 235, 255), PolarMarkerShape::Circle},
            {"Signal B", 6.5, 3.0, Color( 25, 118, 210, 255), PolarMarkerShape::Diamond},
            {"Signal C", 7.5, 2.5, Color(245, 130,  32, 255), PolarMarkerShape::Square}
        };

        auto chart = CreatePolarChart(id, x, y, w, h);
        chart->SetAngleMode(PolarAngleMode::Numeric);
        chart->SetAngleTickInterval(30.0f);
        chart->SetRadialRange(0.0, 16.0);
        chart->SetRadialTickInterval(2.0);

        for (const auto& spec : specs) {
            std::normal_distribution<double> radiusDist(spec.meanRadius, spec.spread);
            PolarSeries series(spec.name, PolarSeriesType::Scatter, spec.color);
            series.marker = spec.marker;
            series.markerSize = 7.0f;
            series.lineWidth = 0.0f;
            series.closed = false;
            for (int i = 0; i < 26; ++i) {
                series.points.emplace_back(angleDist(gen),
                                           std::max(0.5, radiusDist(gen)));
            }
            chart->AddSeries(series);
        }
        return chart;
    }

    // Two analytic rose curves r = a * cos(k * theta), the classic dual angle
    // axis showcase with a negative radial minimum.
    static std::shared_ptr<UltraCanvasPolarChart> BuildRoseCurves(
        const std::string& id, int x, int y, int w, int h)
    {
        auto chart = CreatePolarChart(id, x, y, w, h);
        chart->SetAngleMode(PolarAngleMode::Numeric);
        chart->SetDirection(PolarDirection::CounterClockwise);
        chart->SetZeroAngle(0.0f);                 // 0 degrees points east
        chart->SetAngleTickInterval(20.0f);
        chart->SetRadialRange(-100.0, 200.0);
        chart->SetRadialTickInterval(100.0);
        chart->SetSecondaryAngleAxisEnabled(true);

        PolarSeries first("Series 1", PolarSeriesType::Line, Color(240, 120, 100, 255));
        first.marker = PolarMarkerShape::NoMarker;
        first.lineWidth = 1.6f;
        PolarSeries second("Series 2", PolarSeriesType::Line, Color(150, 150, 150, 255));
        second.marker = PolarMarkerShape::NoMarker;
        second.lineWidth = 1.6f;

        for (int deg = 0; deg <= 360; ++deg) {
            double theta = deg * M_PI / 180.0;
            first.points.emplace_back(deg, 120.0 * std::cos(2.0 * theta));
            second.points.emplace_back(deg, 90.0 * std::cos(3.0 * theta) + 20.0);
        }
        chart->AddSeries(first);
        chart->AddSeries(second);
        return chart;
    }

    // Four overlapping translucent petals — the polar area chart look.
    static std::shared_ptr<UltraCanvasPolarChart> BuildPetalAreas(
        const std::string& id, int x, int y, int w, int h)
    {
        auto chart = CreatePolarChart(id, x, y, w, h);
        chart->SetAngleMode(PolarAngleMode::Numeric);
        chart->SetAngleTickInterval(45.0f);
        chart->SetRadialRange(0.0, 10.0);
        chart->SetRadialTickInterval(5.0);

        struct PetalSpec { const char* name; double centerDeg; Color color; };
        const PetalSpec specs[] = {
            {"North", 0.0,   Color( 66, 165, 245, 190)},
            {"East",  90.0,  Color(236,  64, 122, 170)},
            {"South", 180.0, Color(251, 192,  45, 190)},
            {"West",  270.0, Color( 77, 208, 174, 190)}
        };

        for (const auto& spec : specs) {
            PolarSeries series(spec.name, PolarSeriesType::SplineArea, spec.color);
            series.fillOpacity = 0.55f;
            series.lineWidth = 2.0f;
            series.marker = PolarMarkerShape::NoMarker;
            series.closed = true;
            // A cosine lobe centred on the petal direction, clipped at zero.
            for (int deg = 0; deg < 360; deg += 10) {
                double delta = (deg - spec.centerDeg) * M_PI / 180.0;
                double value = 10.0 * std::max(0.0, std::cos(delta * 0.5));
                series.points.emplace_back(deg, value * value / 10.0);
            }
            chart->AddSeries(series);
        }
        return chart;
    }

    // Sales by category and year — categorical spokes with a filled outline,
    // the shape a radar-style polar area chart produces.
    static std::shared_ptr<UltraCanvasPolarChart> BuildCategorySales(
        const std::string& id, int x, int y, int w, int h)
    {
        auto chart = CreatePolarChart(id, x, y, w, h);
        chart->SetCategories({"Furniture", "Binders", "Phones", "Storage",
                              "Chairs", "Tables", "Machines", "Copiers",
                              "Paper", "Art"});
        chart->SetCategoryPlacement(PolarCategoryPlacement::OnSpokes);
        chart->SetGridShape(PolarGridShape::Circular);
        chart->SetRadialTickCount(6);

        PolarSeries series("2018 Sales", PolarSeriesType::SplineArea,
                           Color(102, 187, 176, 255));
        series.fillOpacity = 0.45f;
        series.lineWidth = 2.0f;
        series.marker = PolarMarkerShape::NoMarker;
        series.closed = true;
        const double values[] = {184.0, 142.0, 210.0, 96.0, 168.0,
                                 74.0, 58.0, 132.0, 88.0, 46.0};
        for (double value : values) series.points.emplace_back(0.0, value);
        chart->AddSeries(series);
        return chart;
    }

    // Monthly chemical concentration against absorption tolerance bands.
    static std::shared_ptr<UltraCanvasPolarChart> BuildConcentrationChart(
        const std::string& id, int x, int y, int w, int h)
    {
        auto chart = CreatePolarChart(id, x, y, w, h);
        chart->SetCategories({"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"});
        chart->SetCategoryPlacement(PolarCategoryPlacement::OnSpokes);
        chart->SetRadialRange(0.0, 100.0);
        chart->SetRadialTickInterval(20.0);
        chart->SetRadialLabelUnit(" ppm");

        // Under / normal / over absorption zones behind the data.
        chart->AddRadialBand(0.0,  20.0,  Color(150, 160, 235, 150), "Under-Absorp");
        chart->AddRadialBand(20.0, 60.0,  Color(150, 230, 160, 150), "Normal");
        chart->AddRadialBand(60.0, 100.0, Color(240, 150, 150, 150), "Over-Absorp");

        PolarSeries series("Concentration", PolarSeriesType::Line,
                           Color(20, 40, 160, 255));
        series.lineWidth = 3.0f;
        series.marker = PolarMarkerShape::Circle;
        series.markerSize = 8.0f;
        series.closed = true;
        const double values[] = {58.0, 21.0, 30.0, 44.0, 28.0, 64.0,
                                 72.0, 34.0, 26.0, 88.0, 40.0, 33.0};
        for (double value : values) {
            // Amber markers read clearly against the blue line and the bands.
            series.points.emplace_back(0.0, value, "", Color(255, 214, 0, 255));
        }
        chart->AddSeries(series);
        return chart;
    }

    // Regional profit by product and year — the stacked polar column (rose) form.
    static std::shared_ptr<UltraCanvasPolarChart> BuildProfitRose(
        const std::string& id, int x, int y, int w, int h)
    {
        std::vector<std::string> products = {
            "Powder", "Nail polish", "Eyebrow pencil", "Rouge", "Lipstick",
            "Eyeshadows", "Eyeliner", "Foundation", "Lip gloss", "Mascara"
        };
        std::vector<std::pair<std::string, std::vector<double>>> years = {
            {"2023", {21, 11,  9,  6, 13, 18, 24, 26, 29, 20}},
            {"2024", {26, 14, 11,  8, 16, 22, 29, 33, 36, 25}},
            {"2025", {33, 18, 14, 10, 21, 28, 37, 41, 45, 32}},
            {"2026", {41, 22, 17, 12, 26, 35, 46, 52, 57, 40}}
        };

        auto chart = CreatePolarRoseChart(id, x, y, w, h, products, years,
                                          PolarStackMode::Stacked);
        chart->SetColorPalette({
            Color(255, 213,  79, 255),   // Amber
            Color(245, 130,  32, 255),   // Orange
            Color( 25, 118, 210, 255),   // Blue
            Color(129, 190, 240, 255)    // Light blue
        });
        chart->SetAngleLabelOrientation(PolarLabelOrientation::Radial);
        chart->SetColumnBorder(Color(255, 255, 255, 120), 1.0f);
        return chart;
    }

// ===== CONTROL PANEL =====

    static void CreatePolarControlPanel(
        std::shared_ptr<UltraCanvasContainer> container,
        std::vector<std::shared_ptr<UltraCanvasPolarChart>> charts)
    {
        const int controlHeight = 25;

        // A bold group heading of the panel
        auto addGroupLabel = [&container](const std::string& id, const std::string& text) {
            DemoKit::AddText(container, id, text)->SetFontWeight(FontWeight::Bold);
        };

        addGroupLabel("PoGridHeader", "Grid Shape:");

        auto gridDropdown = std::make_shared<UltraCanvasDropdown>("PoGrid", 0, 0, 180, controlHeight);
        gridDropdown->AddItem("Circular");
        gridDropdown->AddItem("Polygonal (spider)");
        gridDropdown->SetSelectedIndex(0);
        gridDropdown->onSelectionChanged = [charts](int index, const DropdownItem&) {
            PolarGridShape shape = index == 1 ? PolarGridShape::Polygonal
                                              : PolarGridShape::Circular;
            for (auto& c : charts) c->SetGridShape(shape);
        };
        DemoKit::AddFixed(container, gridDropdown);

        addGroupLabel("PoLabelHeader", "Angle Label Orientation:");

        auto labelDropdown = std::make_shared<UltraCanvasDropdown>("PoLabels", 0, 0, 180, controlHeight);
        labelDropdown->AddItem("Horizontal");
        labelDropdown->AddItem("Tangential");
        labelDropdown->AddItem("Radial");
        labelDropdown->SetSelectedIndex(0);
        labelDropdown->onSelectionChanged = [charts](int index, const DropdownItem&) {
            PolarLabelOrientation orientation;
            switch (index) {
                case 1:  orientation = PolarLabelOrientation::Tangential; break;
                case 2:  orientation = PolarLabelOrientation::Radial; break;
                default: orientation = PolarLabelOrientation::Horizontal; break;
            }
            for (auto& c : charts) c->SetAngleLabelOrientation(orientation);
        };
        DemoKit::AddFixed(container, labelDropdown);

        addGroupLabel("PoScaleHeader", "Radial Scale:");

        auto scaleDropdown = std::make_shared<UltraCanvasDropdown>("PoScale", 0, 0, 180, controlHeight);
        scaleDropdown->AddItem("Linear");
        scaleDropdown->AddItem("Square Root");
        scaleDropdown->AddItem("Logarithmic");
        scaleDropdown->SetSelectedIndex(0);
        scaleDropdown->onSelectionChanged = [charts](int index, const DropdownItem&) {
            PolarRadialScale scale;
            switch (index) {
                case 1:  scale = PolarRadialScale::SquareRoot; break;
                case 2:  scale = PolarRadialScale::Logarithmic; break;
                default: scale = PolarRadialScale::Linear; break;
            }
            for (auto& c : charts) c->SetRadialScale(scale);
        };
        DemoKit::AddFixed(container, scaleDropdown);

        addGroupLabel("PoRotHeader", "Zero Angle (degrees):");

        auto rotationSlider = std::make_shared<UltraCanvasSlider>("PoRotation", 0, 0, 180, controlHeight);
        rotationSlider->SetRange(-180.0, 180.0);
        rotationSlider->SetStep(5.0);
        rotationSlider->SetValue(-90.0);
        rotationSlider->onValueChanged = [charts](double value) {
            for (auto& c : charts) c->SetZeroAngle(static_cast<float>(value));
        };
        DemoKit::AddFixed(container, rotationSlider);

        addGroupLabel("PoHoleHeader", "Inner Radius Fraction:");

        auto holeSlider = std::make_shared<UltraCanvasSlider>("PoHole", 0, 0, 180, controlHeight);
        holeSlider->SetRange(0.0, 0.6);
        holeSlider->SetStep(0.02);
        holeSlider->SetValue(0.0);
        holeSlider->onValueChanged = [charts](double value) {
            for (auto& c : charts) c->SetInnerRadiusFraction(static_cast<float>(value));
        };
        DemoKit::AddFixed(container, holeSlider);

        addGroupLabel("PoSweepHeader", "Sweep Angle (degrees):");

        auto sweepSlider = std::make_shared<UltraCanvasSlider>("PoSweep", 0, 0, 180, controlHeight);
        sweepSlider->SetRange(90.0, 360.0);
        sweepSlider->SetStep(10.0);
        sweepSlider->SetValue(360.0);
        sweepSlider->onValueChanged = [charts](double value) {
            for (auto& c : charts) c->SetSweepAngle(static_cast<float>(value));
        };
        DemoKit::AddFixed(container, sweepSlider);

        addGroupLabel("PoExtrasHeader", "Extras:");

        auto bandsCheckbox = std::make_shared<UltraCanvasCheckbox>("PoBands", 0, 0, 0, controlHeight, "Alternating Bands");
        bandsCheckbox->SetChecked(false);
        bandsCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
            for (auto& c : charts) c->SetAlternatingBands(newState == CheckedState::Checked);
        };
        DemoKit::Add(container, bandsCheckbox);

        auto spokesCheckbox = std::make_shared<UltraCanvasCheckbox>("PoSpokes", 0, 0, 0, controlHeight, "Angular Grid Lines");
        spokesCheckbox->SetChecked(true);
        spokesCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
            for (auto& c : charts) c->SetShowAngularGrid(newState == CheckedState::Checked);
        };
        DemoKit::Add(container, spokesCheckbox);

        auto radialLabelsCheckbox = std::make_shared<UltraCanvasCheckbox>("PoRadialLabels", 0, 0, 0, controlHeight, "Radial Tick Labels");
        radialLabelsCheckbox->SetChecked(true);
        radialLabelsCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
            for (auto& c : charts) c->SetShowRadialLabels(newState == CheckedState::Checked);
        };
        DemoKit::Add(container, radialLabelsCheckbox);

        auto rotateDragCheckbox = std::make_shared<UltraCanvasCheckbox>("PoDrag", 0, 0, 0, controlHeight, "Drag to Rotate");
        rotateDragCheckbox->SetChecked(false);
        rotateDragCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
            for (auto& c : charts) c->SetRotateOnDrag(newState == CheckedState::Checked);
        };
        DemoKit::Add(container, rotateDragCheckbox);

        auto hoverCheckbox = std::make_shared<UltraCanvasCheckbox>("PoHover", 0, 0, 0, controlHeight, "Hover Highlight");
        hoverCheckbox->SetChecked(true);
        hoverCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
            for (auto& c : charts) c->SetHoverHighlightEnabled(newState == CheckedState::Checked);
        };
        DemoKit::Add(container, hoverCheckbox);
    }

// ===== MAIN POLAR CHART EXAMPLES CREATOR =====

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreatePolarChartExamples() {
        auto container = DemoKit::CreatePage("PolarChartContainer",
            "Polar Chart Examples - Angle / Radius Coordinate Plots",
            "Every series plots an angular position against a radial value. Scatter, line, spline, area and column "
            "types share one coordinate system with configurable direction, zero angle, sweep, categorical or numeric "
            "angle axes, linear / sqrt / log radial scales, tolerance bands, stacking, legend toggling and tooltips.");

        // The controls beside the charts; the charts two by two while they
        // fit, one under the other when not.
        auto mainRow = DemoKit::AddRow(container, "PoMainRow");
        auto controlsCard = DemoKit::AddCard(mainRow, "PoControlsCard", "", "",
                                             180 + 2 * DemoKit::kCardPadding + 2);
        controlsCard->layoutItem.SetFlexGrow(0).SetFlexShrink(0);

        // Each chart card is at least as wide as its 460 px chart.
        const float chartCardW = 460 + 2 * DemoKit::kCardPadding + 2;
        auto chartGrid = DemoKit::CreateRow("PoChartGrid");
        DemoKit::Add(mainRow, chartGrid, 1);
        chartGrid->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(chartCardW));

        // ===== EXAMPLE 1: POLAR SCATTER =====
        auto scatterCard = DemoKit::AddCard(chartGrid, "PoScatterCard", "", "", chartCardW);
        DemoKit::AddHeading(scatterCard, "PoScatterLabel", "Polar Scatter (3 Signals, Numeric Angle Axis)");

        auto scatterChart = BuildSignalScatter("PolarScatter", 0, 0, 460, 380);
        scatterChart->SetLegendPosition(PolarLegendPosition::Top);
        scatterChart->SetTooltipsEnabled(true);
        DemoKit::AddFixed(scatterCard, scatterChart);

        // ===== EXAMPLE 2: DUAL ANGLE AXIS ROSE CURVES =====
        auto curveCard = DemoKit::AddCard(chartGrid, "PoCurveCard", "", "", chartCardW);
        DemoKit::AddHeading(curveCard, "PoCurveLabel", "Analytic Curves (Dual Angle Axis, Negative Minimum)");

        auto curveChart = BuildRoseCurves("PolarCurves", 0, 0, 460, 380);
        curveChart->SetChartTitle("Polar Angle Axis Position");
        curveChart->SetLegendPosition(PolarLegendPosition::Right);
        curveChart->SetTooltipsEnabled(true);
        DemoKit::AddFixed(curveCard, curveChart);

        // ===== EXAMPLE 3: OVERLAPPING POLAR AREAS =====
        auto petalCard = DemoKit::AddCard(chartGrid, "PoPetalCard", "", "", chartCardW);
        DemoKit::AddHeading(petalCard, "PoPetalLabel", "Polar Area (Overlapping Translucent Lobes)");

        auto petalChart = BuildPetalAreas("PolarPetals", 0, 0, 460, 340);
        petalChart->SetLegendPosition(PolarLegendPosition::Bottom);
        petalChart->SetPlotBackgroundColor(Color(250, 250, 252, 255));
        petalChart->SetTooltipsEnabled(true);
        DemoKit::AddFixed(petalCard, petalChart);

        // ===== EXAMPLE 4: CATEGORICAL AREA =====
        auto salesCard = DemoKit::AddCard(chartGrid, "PoSalesCard", "", "", chartCardW);
        DemoKit::AddHeading(salesCard, "PoSalesLabel", "Categorical Spline Area (Sales by Sub-Category)");

        auto salesChart = BuildCategorySales("PolarSales", 0, 0, 460, 340);
        salesChart->SetLegendPosition(PolarLegendPosition::NoLegend);
        salesChart->SetAngleLabelOrientation(PolarLabelOrientation::Horizontal);
        salesChart->SetTooltipsEnabled(true);
        DemoKit::AddFixed(salesCard, salesChart);

        // ===== EXAMPLE 5: TOLERANCE BANDS =====
        auto chemCard = DemoKit::AddCard(chartGrid, "PoChemCard", "", "", chartCardW);
        DemoKit::AddHeading(chemCard, "PoChemLabel", "Chemical Concentration (Radial Tolerance Bands)");

        auto chemChart = BuildConcentrationChart("PolarChem", 0, 0, 460, 210);
        chemChart->SetLegendPosition(PolarLegendPosition::NoLegend);
        chemChart->SetTooltipsEnabled(true);
        DemoKit::AddFixed(chemCard, chemChart);

        // ===== EXAMPLE 6: STACKED POLAR COLUMNS (ROSE) =====
        auto roseCard = DemoKit::AddCard(chartGrid, "PoRoseCard", "", "", chartCardW);
        DemoKit::AddHeading(roseCard, "PoRoseLabel", "Stacked Polar Columns (Profit by Product and Year)");

        auto roseChart = BuildProfitRose("PolarRose", 0, 0, 460, 210);
        roseChart->SetLegendPosition(PolarLegendPosition::NoLegend);
        roseChart->SetShowRadialLabels(false);
        roseChart->SetTooltipsEnabled(true);
        DemoKit::AddFixed(roseCard, roseChart);

        // === CONTROL PANEL (left column) ===
        CreatePolarControlPanel(controlsCard,
                                {scatterChart, curveChart, petalChart,
                                 salesChart, chemChart, roseChart});

        return container;
    }

} // namespace UltraCanvas
