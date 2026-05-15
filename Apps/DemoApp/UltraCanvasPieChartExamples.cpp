// Apps/DemoApp/UltraCanvasPieChartExamples.cpp
// Comprehensive pie chart demonstration with all features
// Version: 1.0.0
// Last Modified: 2025-01-16
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
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
            std::shared_ptr<UltraCanvasPieChartElement> pieChart1,
            std::shared_ptr<UltraCanvasPieChartElement> pieChart2,
            int x, int y) {

        int yOffset = y;
        int controlHeight = 25;
        int spacing = 8;

        // === CHART TYPE CONTROLS ===
        auto typeLabel = std::make_shared<UltraCanvasLabel>("TypeLabel", x, yOffset, 200, 20);
        typeLabel->SetText("Chart Type:");
        typeLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(typeLabel);
        yOffset += 25;

        auto donutCheckbox = std::make_shared<UltraCanvasCheckbox>("DonutMode", x, yOffset, 150, controlHeight);
        donutCheckbox->SetText("Donut Mode");
        donutCheckbox->SetChecked(pieChartControls.donutMode);
        donutCheckbox->onStateChanged = [pieChart1, pieChart2](CheckedState oldState, CheckedState newState) {
            pieChartControls.donutMode = newState == CheckedState::Checked;
            pieChart1->SetDonutMode(newState == CheckedState::Checked);
            pieChart2->SetDonutMode(newState == CheckedState::Checked);
        };
        container->AddChild(donutCheckbox);
        yOffset += controlHeight + spacing;

        // === 3D EFFECTS ===
        auto effectsLabel = std::make_shared<UltraCanvasLabel>("EffectsLabel", x, yOffset, 200, 20);
        effectsLabel->SetText("Visual Effects:");
        effectsLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(effectsLabel);
        yOffset += 25;

        auto enable3DCheckbox = std::make_shared<UltraCanvasCheckbox>("3DEffects", x, yOffset, 150, controlHeight);
        enable3DCheckbox->SetText("3D Effects");
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
        container->AddChild(enable3DCheckbox);
        yOffset += controlHeight + spacing;

        auto gradientCheckbox = std::make_shared<UltraCanvasCheckbox>("GradientFills", x, yOffset, 180, controlHeight);
        gradientCheckbox->SetText("Radial Gradients");
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
        container->AddChild(gradientCheckbox);
        yOffset += controlHeight + spacing * 2;

        // === EXPLOSION CONTROLS ===
        auto explosionLabel = std::make_shared<UltraCanvasLabel>("ExplosionLabel", x, yOffset, 200, 20);
        explosionLabel->SetText("Slice Explosion:");
        explosionLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(explosionLabel);
        yOffset += 25;

        auto explosionSlider = std::make_shared<UltraCanvasSlider>("ExplosionSlider", x, yOffset, 180, controlHeight);
        explosionSlider->SetRange(0.0, 0.5);
        explosionSlider->SetStep(0.01);
        explosionSlider->SetValue(pieChartControls.globalExplosion);
        explosionSlider->onValueChanged = [pieChart1, pieChart2](double value) {
            pieChartControls.globalExplosion = static_cast<float>(value);
            pieChart1->SetGlobalExplosion(pieChartControls.globalExplosion);
            pieChart2->SetGlobalExplosion(pieChartControls.globalExplosion);
        };
        container->AddChild(explosionSlider);
        yOffset += controlHeight + spacing * 2;

        // === PERSPECTIVE ANGLE (3D) ===
        auto perspectiveLabel = std::make_shared<UltraCanvasLabel>("PerspectiveLabel", x, yOffset, 200, 20);
        perspectiveLabel->SetText("Perspective Angle (3D):");
        perspectiveLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(perspectiveLabel);
        yOffset += 25;

        auto perspectiveSlider = std::make_shared<UltraCanvasSlider>("PerspectiveSlider", x, yOffset, 180, controlHeight);
        perspectiveSlider->SetRange(5.0, 70.0);
        perspectiveSlider->SetStep(1.0);
        perspectiveSlider->SetValue(pieChartControls.perspectiveAngle);
        perspectiveSlider->onValueChanged = [pieChart1, pieChart2](double value) {
            pieChartControls.perspectiveAngle = static_cast<float>(value);
            pieChart1->SetPerspectiveAngle(pieChartControls.perspectiveAngle);
            pieChart2->SetPerspectiveAngle(pieChartControls.perspectiveAngle);
        };
        container->AddChild(perspectiveSlider);
        yOffset += controlHeight + spacing * 2;

        // === LABEL CONTROLS ===
        auto labelLabel = std::make_shared<UltraCanvasLabel>("LabelLabel", x, yOffset, 200, 20);
        labelLabel->SetText("Labels:");
        labelLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(labelLabel);
        yOffset += 25;

        auto labelPosDropdown = std::make_shared<UltraCanvasDropdown>("LabelPosition", x, yOffset, 180, controlHeight);
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
                case 4: pos = LabelPosition::None; break;
                default: pos = LabelPosition::Auto;
            }
            pieChartControls.labelPosition = pos;
            pieChart1->SetLabelPosition(pos);
            pieChart2->SetLabelPosition(pos);
        };
        container->AddChild(labelPosDropdown);
        yOffset += controlHeight + spacing;

        auto leaderLinesCheckbox = std::make_shared<UltraCanvasCheckbox>("LeaderLines", x, yOffset, 180, controlHeight);
        leaderLinesCheckbox->SetText("Leader Lines");
        leaderLinesCheckbox->SetChecked(pieChartControls.leaderLines);
        leaderLinesCheckbox->onStateChanged = [pieChart1, pieChart2](CheckedState oldState, CheckedState newState) {
            pieChartControls.leaderLines = newState == CheckedState::Checked;
            pieChart1->SetLeaderLinesEnabled(newState == CheckedState::Checked);
            pieChart2->SetLeaderLinesEnabled(newState == CheckedState::Checked);
        };
        container->AddChild(leaderLinesCheckbox);
    }

// ===== MAIN PIE CHART EXAMPLES CREATOR =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreatePieChartExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("PieChartContainer", 0, 0, 1200, 810);

        // === TITLE ===
        auto titleLabel = std::make_shared<UltraCanvasLabel>("TitleLabel", 20, 10, 1160, 35);
        titleLabel->SetText("Pie Chart Examples - Enhanced with 3D & Gradients");
        titleLabel->SetFontSize(18);
        titleLabel->SetFontWeight(FontWeight::Bold);
        titleLabel->SetAlignment(TextAlignment::Center);
        titleLabel->SetBackgroundColor(Color(240, 240, 250, 255));
        container->AddChild(titleLabel);

        // === DESCRIPTION ===
        auto descLabel = std::make_shared<UltraCanvasLabel>("DescLabel", 20, 55, 760, 60);
        descLabel->SetText(
                "Comprehensive pie chart visualization with donut mode, exploded slices, radial gradients,\n"
                "3D visual effects, and advanced labeling. Features intelligent label positioning and interactive tooltips."
        );
        descLabel->SetFontSize(11);
        descLabel->SetWrap(TextWrap::WrapWord);
        descLabel->SetAutoResize(true);
        container->AddChild(descLabel);

        // ===== EXAMPLE 1: MARKET SHARE ANALYSIS =====
        auto marketLabel = std::make_shared<UltraCanvasLabel>("MarketLabel", 240, 130, 460, 25);
        marketLabel->SetText("Market Share Distribution");
        marketLabel->SetFontSize(13);
        marketLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(marketLabel);

        auto marketChart = CreatePieChartElement("MarketPieChart", 240, 160, 460, 290);
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
        container->AddChild(marketChart);

        // ===== EXAMPLE 2: BUDGET ALLOCATION =====
        auto budgetLabel = std::make_shared<UltraCanvasLabel>("BudgetLabel", 720, 130, 460, 25);
        budgetLabel->SetText("Annual Budget Allocation");
        budgetLabel->SetFontSize(13);
        budgetLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(budgetLabel);

        auto budgetChart = CreatePieChartElement("BudgetPieChart", 720, 160, 460, 290);
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
        container->AddChild(budgetChart);

        // ===== EXAMPLE 3: 3D DONUT WITH GRADIENTS =====
        auto donutLabel = std::make_shared<UltraCanvasLabel>("DonutLabel", 240, 470, 460, 25);
        donutLabel->SetText("3D Donut with Highlighted Slice");
        donutLabel->SetFontSize(13);
        donutLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(donutLabel);

        auto donutChart = CreatePieChartElement("DonutChart", 240, 500, 460, 290);
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
        donutChart->SetLightDirection(Point2Df(-0.6f, -0.8f));
        donutChart->SetAmbientLight(0.35f);
        donutChart->SetDiffuseLight(0.65f);

        // Enable donut mode
        donutChart->SetDonutMode(true);
        donutChart->SetInnerRadius(0.45f);

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

        container->AddChild(donutChart);

        // ===== EXAMPLE 4: STYLIZED SHOWCASE =====
        auto styledLabel = std::make_shared<UltraCanvasLabel>("StyledLabel", 720, 470, 460, 25);
        styledLabel->SetText("Stylized Pie Chart");
        styledLabel->SetFontSize(13);
        styledLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(styledLabel);

        auto styledChart = CreatePieChartElement("StyledChart", 720, 500, 460, 290);
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

        container->AddChild(styledChart);

        // === CONTROL PANEL (left column) ===
        CreatePieChartControlPanel(container, marketChart, budgetChart, 20, 130);

        return container;
    }

} // namespace UltraCanvas