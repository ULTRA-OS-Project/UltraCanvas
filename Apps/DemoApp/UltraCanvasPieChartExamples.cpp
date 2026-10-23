// Apps/DemoApp/UltraCanvasPieChartExamples.cpp
// Comprehensive pie chart demonstration with all features
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): the controls
//                  in a card beside the four charts, each chart in its own
//                  card, every label and checkbox sized to its text
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Charts/UltraCanvasSpecificChartElements.h"
#include "Plugins/Charts/UltraCanvasPieChart.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include <random>
#include <sstream>
#include <iomanip>

namespace UltraCanvas {

// ===== PIE CHART CONTROLS STATE =====
    static struct PieChartControls {
        bool donutMode = false;
        float innerRadius = 0.4f;
        float globalExplosion = 0.0f;
        bool gradientFills = false;
        bool enable3DEffects = false;
        float depthHeight = 25.0f;
        float perspectiveAngle = 20.0f;

        LabelPosition labelPosition = LabelPosition::Auto;
        LabelContent labelContent = LabelContent::NamePercentage;
        bool leaderLines = true;
        bool labelBackground = false;

        int selectedSliceIndex = -1;
        float sliceExplosion = 0.15f;
    } pieChartControls;

// ===== SAMPLE DATA GENERATORS =====
    std::shared_ptr<ChartDataVector> GenerateMarketShareData() {
        auto data = std::make_shared<ChartDataVector>();
        std::vector<ChartDataPoint> marketShare = {
                ChartDataPoint(1, 350, 0, "Product A", 350),
                ChartDataPoint(2, 280, 0, "Product B", 280),
                ChartDataPoint(3, 210, 0, "Product C", 210),
                ChartDataPoint(4, 160, 0, "Product D", 160),
                ChartDataPoint(5, 120, 0, "Others", 120)
        };
        data->LoadFromArray(marketShare);
        return data;
    }

    std::shared_ptr<ChartDataVector> GenerateBudgetData() {
        auto data = std::make_shared<ChartDataVector>();
        std::vector<ChartDataPoint> budget = {
                ChartDataPoint(1, 4500, 0, "Salaries", 4500),
                ChartDataPoint(2, 2300, 0, "Marketing", 2300),
                ChartDataPoint(3, 1800, 0, "R&D", 1800),
                ChartDataPoint(4, 1200, 0, "Operations", 1200),
                ChartDataPoint(5, 800, 0, "Infrastructure", 800),
                ChartDataPoint(6, 400, 0, "Other", 400)
        };
        data->LoadFromArray(budget);
        return data;
    }

// ===== CONTROL PANEL CREATION =====
    void CreatePieChartControlPanel(
            std::shared_ptr<UltraCanvasContainer> container,
            std::vector<std::shared_ptr<UltraCanvasPieChartElement>> charts,
            std::shared_ptr<UltraCanvasPieChartElement> pieChart1,
            std::shared_ptr<UltraCanvasPieChartElement> pieChart2) {

        int controlHeight = 25;

        // A bold group heading of the panel
        auto addGroupLabel = [&container](const std::string& id, const std::string& text) {
            DemoKit::AddText(container, id, text)->SetFontWeight(FontWeight::Bold);
        };

        // === CHART TYPE CONTROLS ===
        addGroupLabel("TypeLabel", "Chart Type:");

        auto donutCheckbox = std::make_shared<UltraCanvasCheckbox>("DonutMode", 0, 0, 0, controlHeight, "Donut Mode");
        donutCheckbox->SetChecked(pieChartControls.donutMode);
        donutCheckbox->onStateChanged = [pieChart1, pieChart2](CheckedState oldState, CheckedState newState) {
            pieChartControls.donutMode = newState == CheckedState::Checked;
            pieChart1->SetDonutMode(newState == CheckedState::Checked);
            pieChart2->SetDonutMode(newState == CheckedState::Checked);
        };
        DemoKit::Add(container, donutCheckbox);

        // === 3D EFFECTS ===
        addGroupLabel("EffectsLabel", "Visual Effects:");

        auto enable3DCheckbox = std::make_shared<UltraCanvasCheckbox>("3DEffects", 0, 0, 0, controlHeight, "3D Effects");
        enable3DCheckbox->SetChecked(pieChartControls.enable3DEffects);
        enable3DCheckbox->onStateChanged = [pieChart1, pieChart2](CheckedState oldState, CheckedState newState) {
            pieChartControls.enable3DEffects = newState == CheckedState::Checked;
            if (newState == CheckedState::Checked) {
                pieChart1->Enable3DMode(pieChartControls.depthHeight, pieChartControls.perspectiveAngle);
                pieChart2->Enable3DMode(pieChartControls.depthHeight, pieChartControls.perspectiveAngle);
            } else {
                pieChart1->Disable3DMode();
                pieChart2->Disable3DMode();
            }
        };
        DemoKit::Add(container, enable3DCheckbox);

        auto gradientCheckbox = std::make_shared<UltraCanvasCheckbox>("GradientFills", 0, 0, 0, controlHeight, "Radial Gradients");
        gradientCheckbox->SetChecked(pieChartControls.gradientFills);
        gradientCheckbox->onStateChanged = [pieChart1, pieChart2](CheckedState oldState, CheckedState newState) {
            pieChartControls.gradientFills = newState == CheckedState::Checked;
            if (newState == CheckedState::Checked) {
                pieChart1->SetAutoRadialGradients();
                pieChart2->SetAutoRadialGradients();
            } else {
                pieChart1->ClearSliceGradients();
                pieChart2->ClearSliceGradients();
            }
        };
        DemoKit::Add(container, gradientCheckbox);

        // === EXPLOSION CONTROLS ===
        addGroupLabel("ExplosionLabel", "Slice Explosion:");

        auto explosionSlider = std::make_shared<UltraCanvasSlider>("ExplosionSlider", 0, 0, 180, controlHeight);
        explosionSlider->SetRange(0.0, 0.5);
        explosionSlider->SetStep(0.01);
        explosionSlider->SetValue(pieChartControls.globalExplosion);
        explosionSlider->onValueChanged = [pieChart1, pieChart2](double value) {
            pieChartControls.globalExplosion = static_cast<float>(value);
            pieChart1->SetGlobalExplosion(pieChartControls.globalExplosion);
            pieChart2->SetGlobalExplosion(pieChartControls.globalExplosion);
        };
        DemoKit::AddFixed(container, explosionSlider);

        // === PERSPECTIVE ANGLE (3D) ===
        addGroupLabel("PerspectiveLabel", "Perspective Angle (3D):");

        auto perspectiveSlider = std::make_shared<UltraCanvasSlider>("PerspectiveSlider", 0, 0, 180, controlHeight);
        perspectiveSlider->SetRange(5.0, 70.0);
        perspectiveSlider->SetStep(1.0);
        perspectiveSlider->SetValue(pieChartControls.perspectiveAngle);
        perspectiveSlider->onValueChanged = [charts](double value) {
            pieChartControls.perspectiveAngle = static_cast<float>(value);
            for (auto& c : charts) c->SetPerspectiveAngle(pieChartControls.perspectiveAngle);
        };
        DemoKit::AddFixed(container, perspectiveSlider);

        // === LABEL CONTROLS ===
        addGroupLabel("LabelLabel", "Labels:");

        auto labelPosDropdown = std::make_shared<UltraCanvasDropdown>("LabelPosition", 0, 0, 180, controlHeight);
        labelPosDropdown->AddItem("Auto");
        labelPosDropdown->AddItem("Inside");
        labelPosDropdown->AddItem("Outside");
        labelPosDropdown->AddItem("Edge");
        labelPosDropdown->AddItem("None");
        labelPosDropdown->SetSelectedIndex(0);
        labelPosDropdown->onSelectionChanged = [pieChart1, pieChart2](int index, const DropdownItem& item) {
            LabelPosition pos;
            switch (index) {
                case 0: pos = LabelPosition::Auto; break;
                case 1: pos = LabelPosition::Inside; break;
                case 2: pos = LabelPosition::Outside; break;
                case 3: pos = LabelPosition::Edge; break;
                case 4: pos = LabelPosition::NoLabels; break;
                default: pos = LabelPosition::Auto;
            }
            pieChartControls.labelPosition = pos;
            pieChart1->SetLabelPosition(pos);
            pieChart2->SetLabelPosition(pos);
        };
        DemoKit::AddFixed(container, labelPosDropdown);

        auto leaderLinesCheckbox = std::make_shared<UltraCanvasCheckbox>("LeaderLines", 0, 0, 0, controlHeight, "Leader Lines");
        leaderLinesCheckbox->SetChecked(pieChartControls.leaderLines);
        leaderLinesCheckbox->onStateChanged = [pieChart1, pieChart2](CheckedState oldState, CheckedState newState) {
            pieChartControls.leaderLines = newState == CheckedState::Checked;
            pieChart1->SetLeaderLinesEnabled(newState == CheckedState::Checked);
            pieChart2->SetLeaderLinesEnabled(newState == CheckedState::Checked);
        };
        DemoKit::Add(container, leaderLinesCheckbox);
    }

// ===== MAIN PIE CHART EXAMPLES CREATOR =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreatePieChartExamples() {
        auto container = DemoKit::CreatePage("PieChartContainer",
                "Pie Chart Examples - Enhanced with 3D & Gradients",
                "Comprehensive pie chart visualization with donut mode, exploded slices, radial gradients, "
                "3D visual effects, and advanced labeling. Features intelligent label positioning and "
                "interactive tooltips.");

        // The controls beside the charts; the charts two by two while they
        // fit, one under the other when not.
        auto mainRow = DemoKit::AddRow(container, "PieMainRow");
        auto controlsCard = DemoKit::AddCard(mainRow, "PieControlsCard", "", "",
                                             180 + 2 * DemoKit::kCardPadding + 2);
        controlsCard->layoutItem.SetFlexGrow(0).SetFlexShrink(0);

        // Each chart card is at least as wide as its 460 px chart.
        const float chartCardW = 460 + 2 * DemoKit::kCardPadding + 2;
        auto chartGrid = DemoKit::CreateRow("PieChartGrid");
        DemoKit::Add(mainRow, chartGrid, 1);
        chartGrid->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(chartCardW));

        // ===== EXAMPLE 1: MARKET SHARE ANALYSIS =====
        auto marketCard = DemoKit::AddCard(chartGrid, "PieMarketCard", "", "", chartCardW);
        DemoKit::AddHeading(marketCard, "MarketLabel", "Market Share Distribution");

        auto marketChart = CreatePieChartElement("MarketPieChart", 0, 0, 460, 290);
        marketChart->SetDataSource(GenerateMarketShareData());
        marketChart->SetColorPalette({
                                             Color(54, 162, 235, 255),   // Blue
                                             Color(255, 99, 132, 255),   // Red
                                             Color(255, 205, 86, 255),   // Yellow
                                             Color(75, 192, 192, 255),   // Teal
                                             Color(153, 102, 255, 255)   // Purple
                                     });
        marketChart->SetBorderColor(Colors::White);
        marketChart->SetBorderWidth(2.0f);
        marketChart->SetLabelContent(LabelContent::NamePercentage);
        marketChart->SetLabelPosition(LabelPosition::Auto);
        marketChart->SetTooltipsEnabled(true);
        DemoKit::AddFixed(marketCard, marketChart);

        // ===== EXAMPLE 2: BUDGET ALLOCATION =====
        auto budgetCard = DemoKit::AddCard(chartGrid, "PieBudgetCard", "", "", chartCardW);
        DemoKit::AddHeading(budgetCard, "BudgetLabel", "Annual Budget Allocation");

        auto budgetChart = CreatePieChartElement("BudgetPieChart", 0, 0, 460, 290);
        budgetChart->SetDataSource(GenerateBudgetData());
        budgetChart->SetColorPalette({
                                             Color(255, 159, 64, 255),   // Orange
                                             Color(54, 162, 235, 255),   // Blue
                                             Color(255, 99, 132, 255),   // Red
                                             Color(75, 192, 192, 255),   // Teal
                                             Color(153, 102, 255, 255),  // Purple
                                             Color(199, 199, 199, 255)   // Grey
                                     });
        budgetChart->SetBorderColor(Colors::White);
        budgetChart->SetBorderWidth(2.0f);
        budgetChart->SetLabelContent(LabelContent::NameValue);
        budgetChart->SetLabelPosition(LabelPosition::Outside);
        budgetChart->SetLeaderLinesEnabled(true);
        budgetChart->SetTooltipsEnabled(true);
        DemoKit::AddFixed(budgetCard, budgetChart);

        // ===== EXAMPLE 3: 3D DONUT WITH GRADIENTS =====
        auto donutCard = DemoKit::AddCard(chartGrid, "PieDonutCard", "", "", chartCardW);
        DemoKit::AddHeading(donutCard, "DonutLabel", "3D Pie with Highlighted Slice");

        auto donutChart = CreatePieChartElement("DonutChart", 0, 0, 460, 290);
        donutChart->SetDataSource(GenerateMarketShareData());
        donutChart->SetColorPalette({
                                            Color(255, 99, 132, 255),
                                            Color(54, 162, 235, 255),
                                            Color(255, 205, 86, 255),
                                            Color(75, 192, 192, 255),
                                            Color(153, 102, 255, 255)
                                    });

        // Enable 3D mode
        donutChart->Enable3DMode(30.0f, 22.0f);
        donutChart->SetLightDirection(Point2Dd(-0.6f, -0.8f));
        donutChart->SetAmbientLight(0.35f);
        donutChart->SetDiffuseLight(0.65f);

        // Solid pie (no center hole) per the design — the 3D shading reads
        // better on a full sector than on a thin donut ring.
        donutChart->SetDonutMode(false);

        // Apply radial gradients
        donutChart->SetAutoRadialGradients();

        // Explode first slice
        donutChart->SetSliceExplosion(0, 0.18f);

        // Highlight the largest slice by raising it 60% above the others
        // (uses the new per-slice height extrusion API).
        donutChart->SetSliceHeight(0, 1.6f);

        // Configure labels
        donutChart->SetLabelContent(LabelContent::NamePercentage);
        donutChart->SetLabelPosition(LabelPosition::Outside);
        donutChart->SetLeaderLinesEnabled(true);
        donutChart->SetLeaderLineStyle(Color(100, 100, 100, 255), 1.5f);
        donutChart->SetBorderColor(Colors::White);
        donutChart->SetBorderWidth(2.5f);
        donutChart->SetTooltipsEnabled(true);

        DemoKit::AddFixed(donutCard, donutChart);

        // ===== EXAMPLE 4: STYLIZED SHOWCASE =====
        auto styledCard = DemoKit::AddCard(chartGrid, "PieStyledCard", "", "", chartCardW);
        DemoKit::AddHeading(styledCard, "StyledLabel", "Stylized Pie Chart");

        auto styledChart = CreatePieChartElement("StyledChart", 0, 0, 460, 290);
        styledChart->SetDataSource(GenerateBudgetData());
        styledChart->SetColorPalette({
                                             Color(255, 159, 64, 255),
                                             Color(54, 162, 235, 255),
                                             Color(255, 99, 132, 255),
                                             Color(75, 192, 192, 255),
                                             Color(153, 102, 255, 255),
                                             Color(199, 199, 199, 255)
                                     });

        // Showcase styling: gradients, bold labels, thick white borders.
        styledChart->SetAutoRadialGradients();
        styledChart->SetLabelContent(LabelContent::NameValue);
        styledChart->SetLabelPosition(LabelPosition::Auto);
        styledChart->SetLabelFont("Arial", 12.0f, FontWeight::Bold);
        styledChart->SetBorderColor(Color(255, 255, 255, 255));
        styledChart->SetBorderWidth(3.0f);
        styledChart->SetTooltipsEnabled(true);

        DemoKit::AddFixed(styledCard, styledChart);

        // === CONTROL PANEL (left column) ===
        CreatePieChartControlPanel(controlsCard,
                                   {marketChart, budgetChart, donutChart, styledChart},
                                   marketChart, budgetChart);

        return container;
    }

} // namespace UltraCanvas