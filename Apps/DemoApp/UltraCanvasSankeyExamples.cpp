// Apps/DemoApp/UltraCanvasSankeyExamples.cpp
// Interactive Sankey diagram demonstration page
// Version: 1.0.0
// Last Modified: 2025-10-16
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Diagrams/UltraCanvasSankey.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasTabbedContainer.h"
#include <memory>
#include <sstream>
#include <random>

namespace UltraCanvas {
    void GenerateEnergySankeyData(UltraCanvasSankeyDiagram* renderer) {
        // Energy flow example
        renderer->AddLink("Coal", "Electricity", 35.0f);
        renderer->AddLink("Natural Gas", "Electricity", 35.0f);
        renderer->AddLink("Nuclear", "Electricity", 10.0f);
        renderer->AddLink("Solar", "Electricity", 3.0f);
        renderer->AddLink("Wind", "Electricity", 2.0f);
        renderer->AddLink("Hydro", "Electricity", 5.0f);

        renderer->AddLink("Electricity", "Residential", 40.0f);
        renderer->AddLink("Electricity", "Commercial", 35.0f);
        renderer->AddLink("Electricity", "Industrial", 45.0f);

        renderer->AddLink("Natural Gas", "Residential Heating", 15.0f);
        renderer->AddLink("Natural Gas", "Commercial Heating", 10.0f);

        renderer->SetTheme(SankeyTheme::Energy);
    }

    void GenerateFinanceSankeyData(UltraCanvasSankeyDiagram* renderer) {
        // Financial flow example
        renderer->AddLink("Revenue", "Product Sales", 65.0f);
        renderer->AddLink("Revenue", "Services", 35.0f);

        renderer->AddLink("Product Sales", "Profit", 20.0f);
        renderer->AddLink("Product Sales", "Manufacturing", 30.0f);
        renderer->AddLink("Product Sales", "Marketing", 15.0f);

        renderer->AddLink("Services", "Profit", 15.0f);
        renderer->AddLink("Services", "Operations", 10.0f);
        renderer->AddLink("Services", "Support", 10.0f);

        renderer->AddLink("Profit", "Dividends", 15.0f);
        renderer->AddLink("Profit", "R&D", 10.0f);
        renderer->AddLink("Profit", "Reserves", 10.0f);

        renderer->SetTheme(SankeyTheme::Finance);
    }

    void GenerateWebTrafficSankeyData(UltraCanvasSankeyDiagram* renderer) {
        // Web traffic flow example

        // Clear existing data
        renderer->ClearAll();

        renderer->AddLink("Search", "Homepage", 30.0f);
        renderer->AddLink("Social Media", "Homepage", 30.0f);
        renderer->AddLink("Direct", "Homepage", 25.0f);
        renderer->AddLink("Referral", "Homepage", 25.0f);

        renderer->AddLink("Homepage", "Product Page", 50.0f);
        renderer->AddLink("Homepage", "About", 20.0f);
        renderer->AddLink("Homepage", "Blog", 20.0f);
        renderer->AddLink("Homepage", "Exit", 20.0f);

        renderer->AddLink("Product Page", "Checkout", 30.0f);
        renderer->AddLink("Product Page", "Exit", 20.0f);

        renderer->AddLink("Checkout", "Purchase", 25.0f);
        renderer->AddLink("Checkout", "Exit", 5.0f);

        renderer->SetTheme(SankeyTheme::WebTraffic);
    }

    void GenerateOilSales2024Sankey(UltraCanvasSankeyDiagram* sankey) {

        // ===== MAIN CONTAINER (1920 x 1080) =====
//        auto mainContainer = std::make_shared<UltraCanvasContainer>(
//                "OilSales2024Container", 10000, 0, 0, 1920, 1080
//        );
//        mainContainer->SetBackgroundColor(Color(250, 250, 252));
//
//        // ===== TITLE LABEL =====
//        auto titleLabel = std::make_shared<UltraCanvasLabel>(
//                "OilSalesTitle", 10001, 40, 20, 1840, 45
//        );
//        titleLabel->SetText("Oil Sales 2024");
//        titleLabel->SetFontSize(32);
//        titleLabel->SetFontWeight(FontWeight::Bold);
//        titleLabel->SetTextColor(Color(40, 60, 80));
//        titleLabel->SetAlignment(TextAlignment::Center);
//        mainContainer->AddChild(titleLabel);
//
//        // ===== SUBTITLE LABEL =====
//        auto subtitleLabel = std::make_shared<UltraCanvasLabel>(
//                "OilSalesSubtitle", 10002, 40, 70, 1840, 30
//        );
//        subtitleLabel->SetText("Worldwide application of oil regarding production, region and consumption sectors");
//        subtitleLabel->SetFontSize(16);
//        subtitleLabel->SetTextColor(Color(80, 100, 120));
//        subtitleLabel->SetAlignment(TextAlignment::Center);
//        mainContainer->AddChild(subtitleLabel);
//
//        // ===== SANKEY DIAGRAM =====
//        // Position: below title/subtitle with margins
//        // Size: 1920 - 80 (margins) = 1840 width, 1080 - 120 (header) - 40 (bottom margin) = 920 height
//        auto sankey = std::make_shared<UltraCanvasSankeyRenderer>(
//                "OilSalesSankey", 10010, 40, 115, 1840, 920
//        );

        // Configure Sankey appearance
        sankey->ClearAll();
        sankey->SetTheme(SankeyTheme::Custom);
        sankey->SetAlignment(SankeyAlignment::Justify);
        sankey->SetNodeWidth(20.0f);
        sankey->SetNodePadding(12.0f);
        sankey->SetLinkCurvature(0.5f);
        sankey->SetFontSize(13.0f);
        sankey->SetMaxLabelWidth(250.0f);

        // ===== COLUMN 1: WORLD PRODUCTION (Source) =====
        // Total: 104.7 mb/day
        sankey->AddNode("World Oil Production", "World\nOil Production\n(104.7 mb/d)", Color(147, 180, 220));

        // ===== COLUMN 2: REGIONAL DISTRIBUTION =====
        // Links from World Production to Regions (values from CSV)
        sankey->AddLink("World Oil Production", "North America", 24.5f, Color(255, 182, 193));
        sankey->AddLink("World Oil Production", "Central America & Caribbean", 1.1f, Color(255, 218, 185));
        sankey->AddLink("World Oil Production", "South America", 6.0f, Color(255, 239, 186));
        sankey->AddLink("World Oil Production", "Europe + CIS", 18.5f, Color(186, 225, 255));
        sankey->AddLink("World Oil Production", "Middle East", 8.7f, Color(255, 204, 153));
        sankey->AddLink("World Oil Production", "China", 16.4f, Color(255, 153, 153));
        sankey->AddLink("World Oil Production", "India", 5.55f, Color(204, 229, 255));
        sankey->AddLink("World Oil Production", "Rest of Asia", 10.5f, Color(229, 204, 255));
        sankey->AddLink("World Oil Production", "Africa", 4.25f, Color(204, 255, 204));
        sankey->AddLink("World Oil Production", "Oceania", 1.15f, Color(179, 229, 252));

        // ===== COLUMN 3: CONSUMPTION SECTORS =====
        // Calculate proportional distribution from each region to sectors
        // Based on global sector percentages applied proportionally

        // Global sector percentages (from CSV total 104.7 mb/d):
        // Road Transport: 45.5 / 104.7 = 43.46%
        // Aviation: 7.5 / 104.7 = 7.16%
        // Maritime: 7.5 / 104.7 = 7.16%
        // Rail & Buses: 2.5 / 104.7 = 2.39%
        // Petrochemicals: 16.0 / 104.7 = 15.28%
        // Industry: 9.0 / 104.7 = 8.60%
        // Buildings: 5.5 / 104.7 = 5.25%
        // Power Generation: 3.5 / 104.7 = 3.34%
        // Agriculture: 2.5 / 104.7 = 2.39%
        // Non-energy/Other: 5.5 / 104.7 = 5.25%

        // North America (24.5 mb/d) to sectors
        sankey->AddLink("North America", "Road Transport", 10.65f, Color(255, 179, 186));
        sankey->AddLink("North America", "Aviation", 1.75f, Color(186, 255, 201));
        sankey->AddLink("North America", "Maritime Shipping", 1.75f, Color(186, 225, 255));
        sankey->AddLink("North America", "Rail & Buses", 0.59f, Color(255, 223, 186));
        sankey->AddLink("North America", "Petrochemicals", 3.74f, Color(219, 186, 255));
        sankey->AddLink("North America", "Industry", 2.11f, Color(186, 255, 255));
        sankey->AddLink("North America", "Buildings/Residential", 1.29f, Color(255, 255, 186));
        sankey->AddLink("North America", "Power Generation", 0.82f, Color(255, 204, 204));
        sankey->AddLink("North America", "Agriculture", 0.59f, Color(204, 255, 179));
        sankey->AddLink("North America", "Non-energy/Other", 1.21f, Color(224, 224, 224));

        // Central America & Caribbean (1.1 mb/d) to sectors
        sankey->AddLink("Central America & Caribbean", "Road Transport", 0.48f);
        sankey->AddLink("Central America & Caribbean", "Aviation", 0.08f);
        sankey->AddLink("Central America & Caribbean", "Maritime Shipping", 0.08f);
        sankey->AddLink("Central America & Caribbean", "Petrochemicals", 0.17f);
        sankey->AddLink("Central America & Caribbean", "Industry", 0.09f);
        sankey->AddLink("Central America & Caribbean", "Buildings/Residential", 0.06f);
        sankey->AddLink("Central America & Caribbean", "Non-energy/Other", 0.14f);

        // South America (6.0 mb/d) to sectors
        sankey->AddLink("South America", "Road Transport", 2.61f);
        sankey->AddLink("South America", "Aviation", 0.43f);
        sankey->AddLink("South America", "Maritime Shipping", 0.43f);
        sankey->AddLink("South America", "Rail & Buses", 0.14f);
        sankey->AddLink("South America", "Petrochemicals", 0.92f);
        sankey->AddLink("South America", "Industry", 0.52f);
        sankey->AddLink("South America", "Buildings/Residential", 0.31f);
        sankey->AddLink("South America", "Power Generation", 0.20f);
        sankey->AddLink("South America", "Agriculture", 0.14f);
        sankey->AddLink("South America", "Non-energy/Other", 0.30f);

        // Europe + CIS (18.5 mb/d) to sectors
        sankey->AddLink("Europe + CIS", "Road Transport", 8.04f);
        sankey->AddLink("Europe + CIS", "Aviation", 1.32f);
        sankey->AddLink("Europe + CIS", "Maritime Shipping", 1.32f);
        sankey->AddLink("Europe + CIS", "Rail & Buses", 0.44f);
        sankey->AddLink("Europe + CIS", "Petrochemicals", 2.83f);
        sankey->AddLink("Europe + CIS", "Industry", 1.59f);
        sankey->AddLink("Europe + CIS", "Buildings/Residential", 0.97f);
        sankey->AddLink("Europe + CIS", "Power Generation", 0.62f);
        sankey->AddLink("Europe + CIS", "Agriculture", 0.44f);
        sankey->AddLink("Europe + CIS", "Non-energy/Other", 0.93f);

        // Middle East (8.7 mb/d) to sectors
        sankey->AddLink("Middle East", "Road Transport", 3.78f);
        sankey->AddLink("Middle East", "Aviation", 0.62f);
        sankey->AddLink("Middle East", "Maritime Shipping", 0.62f);
        sankey->AddLink("Middle East", "Rail & Buses", 0.21f);
        sankey->AddLink("Middle East", "Petrochemicals", 1.33f);
        sankey->AddLink("Middle East", "Industry", 0.75f);
        sankey->AddLink("Middle East", "Buildings/Residential", 0.46f);
        sankey->AddLink("Middle East", "Power Generation", 0.29f);
        sankey->AddLink("Middle East", "Agriculture", 0.21f);
        sankey->AddLink("Middle East", "Non-energy/Other", 0.43f);

        // China (16.4 mb/d) to sectors
        sankey->AddLink("China", "Road Transport", 7.13f);
        sankey->AddLink("China", "Aviation", 1.17f);
        sankey->AddLink("China", "Maritime Shipping", 1.17f);
        sankey->AddLink("China", "Rail & Buses", 0.39f);
        sankey->AddLink("China", "Petrochemicals", 2.51f);
        sankey->AddLink("China", "Industry", 1.41f);
        sankey->AddLink("China", "Buildings/Residential", 0.86f);
        sankey->AddLink("China", "Power Generation", 0.55f);
        sankey->AddLink("China", "Agriculture", 0.39f);
        sankey->AddLink("China", "Non-energy/Other", 0.82f);

        // India (5.55 mb/d) to sectors
        sankey->AddLink("India", "Road Transport", 2.41f);
        sankey->AddLink("India", "Aviation", 0.40f);
        sankey->AddLink("India", "Maritime Shipping", 0.40f);
        sankey->AddLink("India", "Rail & Buses", 0.13f);
        sankey->AddLink("India", "Petrochemicals", 0.85f);
        sankey->AddLink("India", "Industry", 0.48f);
        sankey->AddLink("India", "Buildings/Residential", 0.29f);
        sankey->AddLink("India", "Power Generation", 0.19f);
        sankey->AddLink("India", "Agriculture", 0.13f);
        sankey->AddLink("India", "Non-energy/Other", 0.27f);

        // Rest of Asia (10.5 mb/d) to sectors
        sankey->AddLink("Rest of Asia", "Road Transport", 4.56f);
        sankey->AddLink("Rest of Asia", "Aviation", 0.75f);
        sankey->AddLink("Rest of Asia", "Maritime Shipping", 0.75f);
        sankey->AddLink("Rest of Asia", "Rail & Buses", 0.25f);
        sankey->AddLink("Rest of Asia", "Petrochemicals", 1.60f);
        sankey->AddLink("Rest of Asia", "Industry", 0.90f);
        sankey->AddLink("Rest of Asia", "Buildings/Residential", 0.55f);
        sankey->AddLink("Rest of Asia", "Power Generation", 0.35f);
        sankey->AddLink("Rest of Asia", "Agriculture", 0.25f);
        sankey->AddLink("Rest of Asia", "Non-energy/Other", 0.54f);

        // Africa (4.25 mb/d) to sectors
        sankey->AddLink("Africa", "Road Transport", 1.85f);
        sankey->AddLink("Africa", "Aviation", 0.30f);
        sankey->AddLink("Africa", "Maritime Shipping", 0.30f);
        sankey->AddLink("Africa", "Rail & Buses", 0.10f);
        sankey->AddLink("Africa", "Petrochemicals", 0.65f);
        sankey->AddLink("Africa", "Industry", 0.37f);
        sankey->AddLink("Africa", "Buildings/Residential", 0.22f);
        sankey->AddLink("Africa", "Power Generation", 0.14f);
        sankey->AddLink("Africa", "Agriculture", 0.10f);
        sankey->AddLink("Africa", "Non-energy/Other", 0.22f);

        // Oceania (1.15 mb/d) to sectors
        sankey->AddLink("Oceania", "Road Transport", 0.50f);
        sankey->AddLink("Oceania", "Aviation", 0.08f);
        sankey->AddLink("Oceania", "Maritime Shipping", 0.08f);
        sankey->AddLink("Oceania", "Rail & Buses", 0.03f);
        sankey->AddLink("Oceania", "Petrochemicals", 0.18f);
        sankey->AddLink("Oceania", "Industry", 0.10f);
        sankey->AddLink("Oceania", "Buildings/Residential", 0.06f);
        sankey->AddLink("Oceania", "Power Generation", 0.04f);
        sankey->AddLink("Oceania", "Agriculture", 0.03f);
        sankey->AddLink("Oceania", "Non-energy/Other", 0.05f);
}

// ===== SANKEY DEMO IMPLEMENTATION =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateSankeyExamples() {
        // Main container for all Sankey examples
        auto mainContainer = std::make_shared<UltraCanvasContainer>(
                "SankeyExamples", 5000, 0, 0, 1030, 800
        );

        // ===== PAGE HEADER =====
        auto title = std::make_shared<UltraCanvasLabel>(
                "SankeyTitle", 5001, 20, 10, 500, 35
        );
        title->SetText("Sankey Diagram Visualization");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150));
        mainContainer->AddChild(title);

        auto subtitle = std::make_shared<UltraCanvasLabel>(
                "SankeySubtitle", 5002, 20, 45, 960, 25
        );
        subtitle->SetText("Interactive flow diagrams showing relationships and value distributions between entities");
        subtitle->SetFontSize(12);
        subtitle->SetTextColor(Color(100, 100, 100));
        mainContainer->AddChild(subtitle);

        // ===== STATUS LABEL =====
        auto statusLabel = std::make_shared<UltraCanvasLabel>(
                "SankeyStatus", 5003, 670, 10, 320, 60
        );
        statusLabel->SetText("Hover over nodes and links to see details\nDrag nodes to adjust layout");
        statusLabel->SetFontSize(11);
        statusLabel->SetBackgroundColor(Color(245, 245, 245));
        statusLabel->SetBorders(1.0f);
        statusLabel->SetPadding(8.0f);
        statusLabel->SetAlignment(TextAlignment::Center);
        mainContainer->AddChild(statusLabel);

        // ===== TABBED CONTAINER FOR DIFFERENT EXAMPLES =====
        auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>(
                "SankeyTabs", 5004, 10, 85, 1020, 710
        );
        tabbedContainer->SetTabPosition(TabPosition::Top);
        tabbedContainer->SetTabStyle(TabStyle::Modern);

        auto oilContainer = std::make_shared<UltraCanvasContainer>(
                "OilTab", 5200, 0, 0, 1020, 700
        );

        // Finance Sankey diagram
        auto oilSankey = std::make_shared<UltraCanvasSankeyDiagram>(
                "OilSankey", 5250, 5, 5, 1000, 650
        );

        // Add financial flow data
        GenerateOilSales2024Sankey(oilSankey.get());

        oilContainer->AddChild(oilSankey);


        // ========================================
        // TAB 1: ENERGY FLOW EXAMPLE
        // ========================================
        auto energyContainer = std::make_shared<UltraCanvasContainer>(
                "EnergyTab", 5100, 0, 0, 970, 640
        );

        // Description
        auto energyDesc = std::make_shared<UltraCanvasLabel>(
                "EnergyDesc", 5101, 10, 10, 950, 40
        );
        energyDesc->SetText(
                "Energy Flow Analysis: This Sankey diagram shows the flow of energy from sources (coal, gas, nuclear, renewables) "
                "through electricity generation to end consumers (residential, commercial, industrial)."
        );
        energyDesc->SetFontSize(11);
        energyDesc->SetWordWrap(true);
        energyContainer->AddChild(energyDesc);

        // Control panel
        auto energyControls = std::make_shared<UltraCanvasContainer>(
                "EnergyControls", 5102, 10, 55, 950, 45
        );
        energyControls->SetBackgroundColor(Color(250, 250, 250));

        // Theme selector
        auto themeLabel = std::make_shared<UltraCanvasLabel>(
                "ThemeLabel", 5103, 10, 12, 60, 20
        );
        themeLabel->SetText("Theme:");
        themeLabel->SetFontSize(11);
        energyControls->AddChild(themeLabel);

        auto themeDropdown = std::make_shared<UltraCanvasDropdown>(
                "ThemeDropdown", 5104, 75, 10, 120, 25
        );
        themeDropdown->AddItem("Default", "0");
        themeDropdown->AddItem("Energy", "1");
        themeDropdown->AddItem("Finance", "2");
        themeDropdown->AddItem("Web Traffic", "3");
        themeDropdown->SetSelectedIndex(1); // Energy theme
        energyControls->AddChild(themeDropdown);

        // Alignment selector
        auto alignLabel = std::make_shared<UltraCanvasLabel>(
                "AlignLabel", 5105, 210, 12, 70, 20
        );
        alignLabel->SetText("Alignment:");
        alignLabel->SetFontSize(11);
        energyControls->AddChild(alignLabel);

        auto alignDropdown = std::make_shared<UltraCanvasDropdown>(
                "AlignDropdown", 5106, 285, 10, 100, 25
        );
        alignDropdown->AddItem("Left", "0");
        alignDropdown->AddItem("Right", "1");
        alignDropdown->AddItem("Center", "2");
        alignDropdown->AddItem("Justify", "3");
        alignDropdown->SetSelectedIndex(3); // Justify
        energyControls->AddChild(alignDropdown);

        // Curvature slider
        auto curveLabel = std::make_shared<UltraCanvasLabel>(
                "CurveLabel", 5107, 400, 12, 70, 20
        );
        curveLabel->SetText("Curvature:");
        curveLabel->SetFontSize(11);
        energyControls->AddChild(curveLabel);

        auto curveSlider = std::make_shared<UltraCanvasSlider>(
                "CurveSlider", 5108, 475, 15, 150, 15
        );
        curveSlider->SetOrientation(SliderOrientation::Horizontal);
        curveSlider->SetRange(0.0f, 1.0f);
        curveSlider->SetValue(0.5f);
        curveSlider->SetStep(0.1f);
        energyControls->AddChild(curveSlider);

        auto curveValue = std::make_shared<UltraCanvasLabel>(
                "CurveValue", 5109, 635, 12, 40, 20
        );
        curveValue->SetText("0.5");
        curveValue->SetFontSize(11);
        curveValue->SetAlignment(TextAlignment::Center);
        energyControls->AddChild(curveValue);

        // Export buttons
        auto exportSVG = std::make_shared<UltraCanvasButton>(
                "ExportSVG", 5110, 700, 10, 100, 25
        );
        exportSVG->SetText("Export SVG");
        exportSVG->SetStyle(ButtonStyles::PrimaryStyle());
        energyControls->AddChild(exportSVG);

        auto loadCSV = std::make_shared<UltraCanvasButton>(
                "LoadCSV", 5111, 810, 10, 100, 25
        );
        loadCSV->SetText("Load CSV");
        loadCSV->SetStyle(ButtonStyles::SecondaryStyle());
        energyControls->AddChild(loadCSV);

        energyContainer->AddChild(energyControls);

        // Create Energy Sankey diagram
        auto energySankey = std::make_shared<UltraCanvasSankeyDiagram>(
                "EnergySankey", 5150, 10, 110, 950, 520
        );

        // Add energy flow data
        GenerateEnergySankeyData(energySankey.get());

        // Set up event handlers
        energySankey->onNodeClick = [statusLabel](const std::string& nodeId) {
            statusLabel->SetText("Clicked node: " + nodeId);
        };

        energySankey->onNodeHover = [statusLabel](const std::string& nodeId) {
            statusLabel->SetText("Hovering over: " + nodeId + "\nDrag to reposition");
        };

        themeDropdown->onSelectionChanged = [energySankey](int index, const DropdownItem& item) {
            energySankey->SetTheme(static_cast<SankeyTheme>(index));
        };

        alignDropdown->onSelectionChanged = [energySankey](int index, const DropdownItem& item) {
            energySankey->SetAlignment(static_cast<SankeyAlignment>(index));
        };

        curveSlider->onValueChanged = [energySankey, curveValue](float value) {
            energySankey->SetLinkCurvature(value);
            std::stringstream ss;
//            ss << std::fixed << std::setprecision(1) << value;
            ss << std::fixed <<  value;
            curveValue->SetText(ss.str());
        };


        loadCSV->onClick = [energySankey, statusLabel]() {
            // In real implementation, this would open a file dialog
            statusLabel->SetText("Load CSV feature requires file dialog implementation");
        };

        energyContainer->AddChild(energySankey);

        // ========================================
        // TAB 2: FINANCIAL FLOW EXAMPLE
        // ========================================
        auto financeContainer = std::make_shared<UltraCanvasContainer>(
                "FinanceTab", 5200, 0, 0, 970, 640
        );

        // Finance Sankey diagram
        auto financeSankey = std::make_shared<UltraCanvasSankeyDiagram>(
                "FinanceSankey", 5250, 10, 55, 950, 575
        );

        // Add financial flow data
        GenerateFinanceSankeyData(financeSankey.get());

        financeSankey->onNodeClick = [statusLabel](const std::string& nodeId) {
            std::stringstream ss;
            ss << "Financial Entity: " << nodeId;
            statusLabel->SetText(ss.str());
        };

        financeContainer->AddChild(financeSankey);

        // ========================================
        // TAB 3: WEB TRAFFIC FLOW EXAMPLE
        // ========================================
        auto webContainer = std::make_shared<UltraCanvasContainer>(
                "WebTab", 5300, 0, 0, 970, 640
        );

        // Description
        auto webDesc = std::make_shared<UltraCanvasLabel>(
                "WebDesc", 5301, 10, 10, 950, 40
        );
        webDesc->SetText(
                "Website User Flow: Visualize how users navigate through your website from various traffic sources "
                "to conversion or exit points."
        );
        webDesc->SetFontSize(11);
        webDesc->SetWordWrap(true);
        webContainer->AddChild(webDesc);

        // Web traffic Sankey diagram
        auto webSankey = std::make_shared<UltraCanvasSankeyDiagram>(
                "WebSankey", 5350, 10, 55, 950, 575
        );

        // Add web traffic flow data
        GenerateWebTrafficSankeyData(webSankey.get());

        webSankey->onNodeClick = [statusLabel](const std::string& nodeId) {
            std::stringstream ss;
            ss << "Page/Source: " << nodeId;
            statusLabel->SetText(ss.str());
        };

        webContainer->AddChild(webSankey);

        // ========================================
        // TAB 4: CUSTOM DATA EXAMPLE
        // ========================================
        auto customContainer = std::make_shared<UltraCanvasContainer>(
                "CustomTab", 5400, 0, 0, 970, 640
        );

        // Description
        auto customDesc = std::make_shared<UltraCanvasLabel>(
                "CustomDesc", 5401, 10, 10, 950, 25
        );
        customDesc->SetText(
                "Build Your Own: Create custom Sankey diagrams by adding nodes and links dynamically"
        );
        customDesc->SetFontSize(11);
        customContainer->AddChild(customDesc);

        // Control panel for custom diagram
        auto customControls = std::make_shared<UltraCanvasContainer>(
                "CustomControls", 5402, 10, 40, 950, 80
        );
        customControls->SetBackgroundColor(Color(250, 250, 250));

        // Input fields for adding links
        auto sourceLabel = std::make_shared<UltraCanvasLabel>(
                "SourceLabel", 5403, 10, 12, 70, 20
        );
        sourceLabel->SetText("Source:");
        sourceLabel->SetFontSize(11);
        customControls->AddChild(sourceLabel);

        auto sourceInput = std::make_shared<UltraCanvasTextInput>(
                "SourceInput", 5404, 75, 10, 120, 25
        );
        sourceInput->SetPlaceholder("Node A");
        customControls->AddChild(sourceInput);

        auto targetLabel = std::make_shared<UltraCanvasLabel>(
                "TargetLabel", 5405, 210, 12, 70, 20
        );
        targetLabel->SetText("Target:");
        targetLabel->SetFontSize(11);
        customControls->AddChild(targetLabel);

        auto targetInput = std::make_shared<UltraCanvasTextInput>(
                "TargetInput", 5406, 270, 10, 120, 25
        );
        targetInput->SetPlaceholder("Node B");
        customControls->AddChild(targetInput);

        auto valueLabel = std::make_shared<UltraCanvasLabel>(
                "ValueLabel", 5407, 410, 12, 50, 20
        );
        valueLabel->SetText("Value:");
        valueLabel->SetFontSize(11);
        customControls->AddChild(valueLabel);

        auto valueInput = std::make_shared<UltraCanvasTextInput>(
                "ValueInput", 5408, 465, 10, 80, 25
        );
        valueInput->SetPlaceholder("100");
        valueInput->SetInputType(TextInputType::Number);
        customControls->AddChild(valueInput);

        auto addLinkBtn = std::make_shared<UltraCanvasButton>(
                "AddLink", 5409, 570, 10, 80, 25
        );
        addLinkBtn->SetText("Add Link");
        addLinkBtn->SetStyle(ButtonStyles::PrimaryStyle());
        customControls->AddChild(addLinkBtn);

        auto clearBtn = std::make_shared<UltraCanvasButton>(
                "ClearAll", 5410, 660, 10, 80, 25
        );
        clearBtn->SetText("Clear All");
        clearBtn->SetStyle(ButtonStyles::DangerStyle());
        customControls->AddChild(clearBtn);

        // Preset examples
        auto presetLabel = std::make_shared<UltraCanvasLabel>(
                "PresetLabel", 5411, 10, 47, 70, 20
        );
        presetLabel->SetText("Presets:");
        presetLabel->SetFontSize(11);
        customControls->AddChild(presetLabel);

        auto presetBtn1 = std::make_shared<UltraCanvasButton>(
                "PresetSupply", 5412, 85, 45, 110, 25
        );
        presetBtn1->SetText("Supply Chain");
        presetBtn1->SetStyle(ButtonStyles::SecondaryStyle());
        customControls->AddChild(presetBtn1);

        auto presetBtn2 = std::make_shared<UltraCanvasButton>(
                "PresetBudget", 5413, 205, 45, 110, 25
        );
        presetBtn2->SetText("Budget Flow");
        presetBtn2->SetStyle(ButtonStyles::SecondaryStyle());
        customControls->AddChild(presetBtn2);

        auto presetBtn3 = std::make_shared<UltraCanvasButton>(
                "PresetProcess", 5414, 325, 45, 110, 25
        );
        presetBtn3->SetText("Process Flow");
        presetBtn3->SetStyle(ButtonStyles::SecondaryStyle());
        customControls->AddChild(presetBtn3);

        customContainer->AddChild(customControls);

        // Custom Sankey diagram
        auto customSankey = std::make_shared<UltraCanvasSankeyDiagram>(
                "CustomSankey", 5450, 10, 130, 950, 500
        );

        // Add link button handler
        addLinkBtn->onClick = [customSankey, sourceInput, targetInput, valueInput, statusLabel]() {
            std::string source = sourceInput->GetText();
            std::string target = targetInput->GetText();
            std::string valueStr = valueInput->GetText();

            if (!source.empty() && !target.empty() && !valueStr.empty()) {
                try {
                    float value = std::stof(valueStr);
                    customSankey->AddLink(source, target, value);

                    // Clear inputs
                    sourceInput->SetText("");
                    targetInput->SetText("");
                    valueInput->SetText("");

                    statusLabel->SetText("Added link: " + source + " → " + target + " (" + valueStr + ")");
                } catch (...) {
                    statusLabel->SetText("Invalid value entered!");
                }
            } else {
                statusLabel->SetText("Please fill all fields!");
            }
        };

        clearBtn->onClick = [customSankey, statusLabel]() {
            customSankey->ClearAll();
            statusLabel->SetText("Diagram cleared");
        };

        // Preset: Supply Chain
        presetBtn1->onClick = [customSankey, statusLabel]() {
            customSankey->ClearAll();
            customSankey->AddLink("Suppliers", "Manufacturing", 200);
            customSankey->AddLink("Manufacturing", "Quality Control", 180);
            customSankey->AddLink("Quality Control", "Warehouse", 170);
            customSankey->AddLink("Quality Control", "Rejected", 10);
            customSankey->AddLink("Warehouse", "Distribution", 150);
            customSankey->AddLink("Warehouse", "Storage", 20);
            customSankey->AddLink("Distribution", "Retail", 100);
            customSankey->AddLink("Distribution", "Online", 50);
            customSankey->AddLink("Retail", "Customers", 95);
            customSankey->AddLink("Online", "Customers", 48);
            customSankey->AddLink("Retail", "Returns", 5);
            customSankey->AddLink("Online", "Returns", 2);
            statusLabel->SetText("Loaded Supply Chain example");
        };

        // Preset: Budget Flow
        presetBtn2->onClick = [customSankey, statusLabel]() {
            customSankey->ClearAll();
            customSankey->AddLink("Budget", "Operations", 400);
            customSankey->AddLink("Budget", "Marketing", 150);
            customSankey->AddLink("Budget", "R&D", 200);
            customSankey->AddLink("Budget", "Admin", 100);
            customSankey->AddLink("Operations", "Salaries", 250);
            customSankey->AddLink("Operations", "Equipment", 100);
            customSankey->AddLink("Operations", "Utilities", 50);
            customSankey->AddLink("Marketing", "Digital", 80);
            customSankey->AddLink("Marketing", "Traditional", 40);
            customSankey->AddLink("Marketing", "Events", 30);
            customSankey->AddLink("R&D", "Research", 120);
            customSankey->AddLink("R&D", "Development", 80);
            customSankey->AddLink("Admin", "Office", 60);
            customSankey->AddLink("Admin", "Legal", 40);
            statusLabel->SetText("Loaded Budget Flow example");
        };

        // Preset: Process Flow
        presetBtn3->onClick = [customSankey, statusLabel]() {
            customSankey->ClearAll();
            customSankey->AddLink("Raw Input", "Processing", 100);
            customSankey->AddLink("Processing", "Stage 1", 95);
            customSankey->AddLink("Processing", "Waste", 5);
            customSankey->AddLink("Stage 1", "Stage 2", 90);
            customSankey->AddLink("Stage 1", "Rework", 5);
            customSankey->AddLink("Stage 2", "Stage 3", 85);
            customSankey->AddLink("Stage 2", "Rework", 5);
            customSankey->AddLink("Stage 3", "Final QC", 80);
            customSankey->AddLink("Stage 3", "Scrap", 5);
            customSankey->AddLink("Final QC", "Output", 75);
            customSankey->AddLink("Final QC", "Rework", 5);
            customSankey->AddLink("Rework", "Return to processing", 15);
            statusLabel->SetText("Loaded Process Flow example");
        };

        customContainer->AddChild(customSankey);

        // ========================================
        // TAB 5: PERFORMANCE TEST
        // ========================================
        auto perfContainer = std::make_shared<UltraCanvasContainer>(
                "PerfTab", 5500, 0, 0, 970, 640
        );

        // Description
        auto perfDesc = std::make_shared<UltraCanvasLabel>(
                "PerfDesc", 5501, 10, 10, 950, 40
        );
        perfDesc->SetText(
                "Performance Testing: Generate large random Sankey diagrams to test rendering performance "
                "and interaction responsiveness with varying numbers of nodes and links."
        );
        perfDesc->SetFontSize(11);
        perfDesc->SetWordWrap(true);
        perfContainer->AddChild(perfDesc);

        // Performance controls
        auto perfControls = std::make_shared<UltraCanvasContainer>(
                "PerfControls", 5502, 10, 55, 950, 45
        );
        perfControls->SetBackgroundColor(Color(250, 250, 250));

        auto nodesLabel = std::make_shared<UltraCanvasLabel>(
                "NodesLabel", 5503, 10, 12, 60, 20
        );
        nodesLabel->SetText("Nodes:");
        nodesLabel->SetFontSize(11);
        perfControls->AddChild(nodesLabel);

        auto nodesSlider = std::make_shared<UltraCanvasSlider>(
                "NodesSlider", 5504, 65, 15, 150, 15
        );
        nodesSlider->SetOrientation(SliderOrientation::Horizontal);
        nodesSlider->SetRange(5.0f, 50.0f);
        nodesSlider->SetValue(20.0f);
        nodesSlider->SetStep(5.0f);
        perfControls->AddChild(nodesSlider);

        auto nodesValue = std::make_shared<UltraCanvasLabel>(
                "NodesValue", 5505, 225, 12, 30, 20
        );
        nodesValue->SetText("20");
        nodesValue->SetFontSize(11);
        perfControls->AddChild(nodesValue);

        auto linksLabel = std::make_shared<UltraCanvasLabel>(
                "LinksLabel", 5506, 270, 12, 50, 20
        );
        linksLabel->SetText("Links:");
        linksLabel->SetFontSize(11);
        perfControls->AddChild(linksLabel);

        auto linksSlider = std::make_shared<UltraCanvasSlider>(
                "LinksSlider", 5507, 315, 15, 150, 15
        );
        linksSlider->SetOrientation(SliderOrientation::Horizontal);
        linksSlider->SetRange(10.0f, 200.0f);
        linksSlider->SetValue(50.0f);
        linksSlider->SetStep(10.0f);
        perfControls->AddChild(linksSlider);

        auto linksValue = std::make_shared<UltraCanvasLabel>(
                "LinksValue", 5508, 475, 12, 40, 20
        );
        linksValue->SetText("50");
        linksValue->SetFontSize(11);
        perfControls->AddChild(linksValue);

        auto generateBtn = std::make_shared<UltraCanvasButton>(
                "GenerateBtn", 5509, 520, 10, 140, 25
        );
        generateBtn->SetText("Generate Random");
        generateBtn->SetStyle(ButtonStyles::PrimaryStyle());
        perfControls->AddChild(generateBtn);

        auto perfStats = std::make_shared<UltraCanvasLabel>(
                "PerfStats", 5510, 670, 12, 270, 20
        );
        perfStats->SetText("Ready to generate");
        perfStats->SetFontSize(11);
        perfStats->SetAlignment(TextAlignment::Right);
        perfControls->AddChild(perfStats);

        perfContainer->AddChild(perfControls);

        // Performance Sankey diagram
        auto perfSankey = std::make_shared<UltraCanvasSankeyDiagram>(
                "PerfSankey", 5550, 10, 110, 950, 520
        );

        // Slider handlers
        nodesSlider->onValueChanged = [nodesValue](float value) {
            nodesValue->SetText(std::to_string(static_cast<int>(value)));
        };

        linksSlider->onValueChanged = [linksValue](float value) {
            linksValue->SetText(std::to_string(static_cast<int>(value)));
        };

        // Generate random diagram
        generateBtn->onClick = [perfSankey, nodesSlider, linksSlider, perfStats, statusLabel]() {
            perfSankey->ClearAll();

            int nodeCount = static_cast<int>(nodesSlider->GetValue());
            int linkCount = static_cast<int>(linksSlider->GetValue());

            // Start timing
            auto startTime = std::chrono::high_resolution_clock::now();

            // Generate nodes
            std::vector<std::string> nodeNames;
            for (int i = 0; i < nodeCount; ++i) {
                std::string nodeName = "Node_" + std::to_string(i);
                nodeNames.push_back(nodeName);
            }

            // Generate random links
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> nodeDist(0, nodeCount - 1);
            std::uniform_real_distribution<> valueDist(10.0, 200.0);

            int actualLinks = 0;
            for (int i = 0; i < linkCount; ++i) {
                int sourceIdx = nodeDist(gen);
                int targetIdx = nodeDist(gen);

                // Avoid self-loops and ensure forward flow
                if (sourceIdx != targetIdx) {
                    if (sourceIdx > targetIdx) {
                        std::swap(sourceIdx, targetIdx);
                    }

                    float value = valueDist(gen);
                    perfSankey->AddLink(nodeNames[sourceIdx], nodeNames[targetIdx], value);
                    actualLinks++;
                }
            }

            // End timing
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

            // Update stats
            std::stringstream ss;
            ss << "Generated " << nodeCount << " nodes, " << actualLinks << " links in "
               << duration.count() << "ms";
            perfStats->SetText(ss.str());

            statusLabel->SetText("Performance test complete: " + std::to_string(duration.count()) + "ms");
        };

        perfContainer->AddChild(perfSankey);

        // ========================================
        // ADD TABS TO CONTAINER
        // ========================================
        tabbedContainer->AddTab("Worldwide Oil Flow", oilContainer);
        tabbedContainer->AddTab("Energy Flow", energyContainer);
        tabbedContainer->AddTab("Financial", financeContainer);
        tabbedContainer->AddTab("Web Traffic", webContainer);
        tabbedContainer->AddTab("Custom Data", customContainer);
        tabbedContainer->AddTab("Performance", perfContainer);

        // Set default active tab
        tabbedContainer->SetActiveTab(0);

        mainContainer->AddChild(tabbedContainer);

        return mainContainer;
    }
} // namespace UltraCanvas