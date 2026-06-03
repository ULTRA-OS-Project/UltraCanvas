// Apps/DemoApp/UltraCanvasJitterPlotExamples.cpp
// Enhanced jitter plot examples with professional styling
// Version: 2.1.3
// Last Modified: 2026-05-18
// Author: UltraCanvas Framework
//
// Changelog:
//   2.1.3 - Vertically centered all filter/control row labels to align with
//           adjacent 32px-tall dropdowns, inputs, and buttons.
//   2.1.1 - Brazil example fixes after visual review:
//           * Classic tab: removed quartile lines and mean cross markers —
//             they cluttered the plot. Reverted to the original aesthetic
//             (median line only). Kept grid + thin white point edges as
//             discreet legibility improvements.
//           * Widened tabbed container to 1500px to use available
//             horizontal space (was 1000px, leaving large empty area on
//             the right and forcing horizontal scrollbar).
//           * Reduced overall container height so the example fits in a
//             single viewport without vertical scrollbar.
//           * Beeswarm interactivity now works thanks to the v1.2.1 cache
//             invalidation fix in UltraCanvasJitterPlotElement.cpp.
//   2.1.0 - Brazil example refactored into UltraCanvasTabbedContainer with
//           Classic Jitter + Beeswarm Packing tabs sharing the same dataset.
//   2.0.0 - Initial enhanced examples with Brazil school scores, continents,
//           market cap, box plot overlay, scientific cross means, raincloud.

#include "UltraCanvasDemo.h"
#include "Plugins/Charts/UltraCanvasJitterPlotElement.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasTextInput.h"
#include <sstream>
#include <random>
#include <map>
#include <iomanip>

namespace UltraCanvas {

// =============================================================================
// LAYOUT CONSTANTS FOR BRAZIL EXAMPLE
// =============================================================================
// Centralised so it's easy to tweak the whole example's footprint.

namespace {

constexpr int kOuterWidth      = 1500;   // Use the full horizontal space available
constexpr int kOuterHeight     = 760;    // Fits in a typical viewport without scroll
constexpr int kTabContentW     = kOuterWidth;
constexpr int kTabContentH     = 700;    // Tabs strip uses ~32-40px, content gets the rest
constexpr int kChartLeftMargin = 50;
constexpr int kChartRightMargin = 50;
constexpr int kChartWidth      = kTabContentW - kChartLeftMargin - kChartRightMargin; // = 1400
constexpr int kChartTopY       = 45;     // below the description label
constexpr int kChartHeight     = 480;

} // anonymous namespace

// =============================================================================
// SHARED DATA STRUCTURES FOR BRAZIL EXAMPLE
// =============================================================================
// Both tabs (Classic + Beeswarm) use the exact same dataset so users can
// directly compare jitter rendering vs beeswarm packing on identical data.

namespace {

struct BrazilDataset {
    std::vector<std::string> states;
    std::map<std::string, std::vector<double>> scoresByState;
    std::map<std::string, std::string> regionByState;
    std::map<std::string, Color> regionalColors;
};

BrazilDataset BuildBrazilDataset() {
    BrazilDataset ds;
    
    ds.states = {
        "AL", "BA", "CE", "MA", "PB", "PE", "PI", "RN", "SE",   // NorthEast
        "AC", "AM", "AP", "PA", "RO", "RR", "TO",               // North
        "DF", "GO", "MS", "MT",                                 // CentralWest
        "ES", "MG", "RJ", "SP",                                 // SouthEast
        "PR", "RS", "SC"                                        // South
    };
    
    ds.regionalColors = {
        {"NorthEast",   Color(67, 170, 139, 200)},   // Verde esmeralda
        {"North",       Color(51, 163, 169, 200)},   // Verde azulado/Teal
        {"CentralWest", Color(252, 180, 92, 200)},   // Naranja
        {"SouthEast",   Color(239, 71, 111, 200)},   // Rojo/Rosa
        {"South",       Color(87, 166, 240, 200)}    // Azul
    };
    
    std::map<std::string, std::pair<double, double>> regionalRanges = {
        {"NorthEast",   {2.0, 4.2}},
        {"North",       {2.5, 4.0}},
        {"CentralWest", {3.0, 4.8}},
        {"SouthEast",   {3.5, 6.5}},
        {"South",       {3.8, 6.2}}
    };
    
    // Fixed seed (42) so both tabs render the SAME data
    std::mt19937 gen(42);
    
    auto generateForRegion = [&](int startIdx, int count, const std::string& region) {
        auto range = regionalRanges[region];
        double mean = (range.first + range.second) / 2.0;
        double stddev = (range.second - range.first) / 4.0;
        std::normal_distribution<> scoreDist(mean, stddev);
        std::uniform_int_distribution<> countDist(15, 30);
        
        for (int i = 0; i < count; ++i) {
            if (startIdx + i >= (int)ds.states.size()) break;
            
            const std::string& stateName = ds.states[startIdx + i];
            ds.regionByState[stateName] = region;
            
            std::vector<double> scores;
            int numSchools = countDist(gen);
            for (int j = 0; j < numSchools; ++j) {
                double score = scoreDist(gen);
                score = std::clamp(score, 1.0, 7.0);
                scores.push_back(score);
            }
            ds.scoresByState[stateName] = scores;
        }
    };
    
    generateForRegion(0,  9, "NorthEast");
    generateForRegion(9,  7, "North");
    generateForRegion(16, 4, "CentralWest");
    generateForRegion(20, 4, "SouthEast");
    generateForRegion(24, 3, "South");
    
    return ds;
}

// Common filter strip placed below each Brazil chart.
// Each tab gets its own filter strip wired to its own chart pointer.
void AddBrazilFilterStrip(std::shared_ptr<UltraCanvasContainer> container,
                          std::shared_ptr<UltraCanvasJitterPlotElement> jitter,
                          int baseY,
                          long uidBase) {
    auto filterLabel = std::make_shared<UltraCanvasLabel>(
        "FilterLabel" + std::to_string(uidBase),  50, baseY, 60, 32);
    filterLabel->SetText("Filter:");
    filterLabel->SetFontSize(12);
    filterLabel->SetTextColor(Color(60, 60, 60, 255));
    filterLabel->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
    container->AddChild(filterLabel);
    
    auto regionDropdown = std::make_shared<UltraCanvasDropdown>(
        "RegionFilter" + std::to_string(uidBase),  110, baseY, 140, 32);
    regionDropdown->AddItem("All Regions");
    regionDropdown->AddItem("NorthEast");
    regionDropdown->AddItem("North");
    regionDropdown->AddItem("CentralWest");
    regionDropdown->AddItem("SouthEast");
    regionDropdown->AddItem("South");
    regionDropdown->SetSelectedIndex(0);
    regionDropdown->onSelectionChanged = [jitter](int index, const DropdownItem& item) {
        std::string region;
        if (index == 1) region = "NorthEast";
        else if (index == 2) region = "North";
        else if (index == 3) region = "CentralWest";
        else if (index == 4) region = "SouthEast";
        else if (index == 5) region = "South";
        jitter->SetRegionFilter(region);
    };
    container->AddChild(regionDropdown);
    
    auto scoreLabel = std::make_shared<UltraCanvasLabel>(
        "ScoreLabel" + std::to_string(uidBase),  270, baseY, 80, 32);
    scoreLabel->SetText("Min Score:");
    scoreLabel->SetFontSize(12);
    scoreLabel->SetTextColor(Color(60, 60, 60, 255));
    scoreLabel->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
    container->AddChild(scoreLabel);
    
    auto scoreInput = std::make_shared<UltraCanvasTextInput>(
        "MinScoreFilter" + std::to_string(uidBase),  350, baseY, 60, 32);
    scoreInput->SetPlaceholder("0.0");
    scoreInput->SetText("0.0");
    container->AddChild(scoreInput);
    
    auto applyBtn = std::make_shared<UltraCanvasButton>(
        "ApplyFilter" + std::to_string(uidBase),  420, baseY, 70, 32);
    applyBtn->SetText("Apply");
    applyBtn->SetFontSize(11);
    applyBtn->SetOnClick([jitter, scoreInput]() {
        std::string text = scoreInput->GetText();
        try {
            double minScore = std::stod(text);
            jitter->SetMinScoreFilter(minScore);
        } catch (...) {
            jitter->SetMinScoreFilter(0.0);
        }
    });
    container->AddChild(applyBtn);
    
    auto quickLabel = std::make_shared<UltraCanvasLabel>(
        "QuickLabel" + std::to_string(uidBase),  510, baseY, 50, 32);
    quickLabel->SetText("Quick:");
    quickLabel->SetFontSize(12);
    quickLabel->SetTextColor(Color(60, 60, 60, 255));
    quickLabel->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
    container->AddChild(quickLabel);
    
    auto btnMin3 = std::make_shared<UltraCanvasButton>(
        "btnMin3_" + std::to_string(uidBase),  560, baseY, 40, 32);
    btnMin3->SetText("3+");
    btnMin3->SetFontSize(11);
    btnMin3->SetOnClick([jitter]() { jitter->SetMinScoreFilter(3.0); });
    container->AddChild(btnMin3);
    
    auto btnMin4 = std::make_shared<UltraCanvasButton>(
        "btnMin4_" + std::to_string(uidBase),  605, baseY, 40, 32);
    btnMin4->SetText("4+");
    btnMin4->SetFontSize(11);
    btnMin4->SetOnClick([jitter]() { jitter->SetMinScoreFilter(4.0); });
    container->AddChild(btnMin4);
    
    auto btnMin5 = std::make_shared<UltraCanvasButton>(
        "btnMin5_" + std::to_string(uidBase),  650, baseY, 40, 32);
    btnMin5->SetText("5+");
    btnMin5->SetFontSize(11);
    btnMin5->SetOnClick([jitter]() { jitter->SetMinScoreFilter(5.0); });
    container->AddChild(btnMin5);
    
    auto btnClear = std::make_shared<UltraCanvasButton>(
        "btnClear_" + std::to_string(uidBase),  695, baseY, 70, 32);
    btnClear->SetText("Clear");
    btnClear->SetFontSize(11);
    btnClear->SetOnClick([jitter, scoreInput, regionDropdown]() {
        jitter->SetMinScoreFilter(0.0);
        jitter->SetRegionFilter("");
        scoreInput->SetText("0.0");
        regionDropdown->SetSelectedIndex(0);
    });
    container->AddChild(btnClear);
}

// Loads dataset into a chart in the right order (by region, matching states list)
void LoadBrazilDataIntoChart(std::shared_ptr<UltraCanvasJitterPlotElement> jitter,
                             const BrazilDataset& ds) {
    jitter->SetCategories(ds.states);
    jitter->SetHueVariable("Region");
    jitter->SetHueColorMap(ds.regionalColors);
    
    for (const auto& state : ds.states) {
        auto itScores = ds.scoresByState.find(state);
        auto itRegion = ds.regionByState.find(state);
        if (itScores == ds.scoresByState.end() || itRegion == ds.regionByState.end()) continue;
        jitter->AddCategoryData(state, itScores->second, itRegion->second);
    }
}

} // anonymous namespace

// =============================================================================
// TAB 1 BUILDER: CLASSIC GAUSSIAN JITTER
// =============================================================================
// Original Brazil example aesthetic — Gaussian random jitter + median line.
// Two discreet improvements kept from v2.1.0 (they don't clutter):
//   * Light grid — easier to read scores across 27 categories.
//   * Thin white point edges — separates overlapping dots so density bands
//     stay legible.

static std::shared_ptr<UltraCanvasContainer> BuildBrazilClassicJitterTab(
        const BrazilDataset& ds) {
    auto tab = std::make_shared<UltraCanvasContainer>("BrazilClassicTab", 0, 0,
                                                      kTabContentW, kTabContentH);
    
    auto descLabel = std::make_shared<UltraCanvasLabel>(
        "BrazilDescClassic", 50, 10, kChartWidth, 25);
    descLabel->SetText("Educational performance scores grouped by Brazilian states and colored by region. "
                       "Random Gaussian jitter — black horizontal lines indicate median score per state.");
    descLabel->SetFontSize(11);
    descLabel->SetTextColor(Color(90, 90, 90, 255));
    tab->AddChild(descLabel);
    
    auto jitter = CreateJitterPlotElement("brazilScoresClassic",
                                          kChartLeftMargin, kChartTopY,
                                          kChartWidth, kChartHeight);
    LoadBrazilDataIntoChart(jitter, ds);
    
    // Core jitter settings (same as original example)
    jitter->SetJitterDistribution(JitterDistribution::Gaussian);
    jitter->SetJitterAmount(0.25f);
    jitter->SetPointSize(3.5f);
    jitter->SetPointAlpha(0.55f);
    jitter->SetPointShape(UltraCanvasJitterPlotElement::PointShape::Circle);
    
    // ---- DISCREET IMPROVEMENTS ----
    // Thin white edges → separates overlapping dots without visually shouting
    jitter->SetPointEdgeColor(Color(255, 255, 255, 180));
    jitter->SetPointEdgeWidth(0.5f);
    
    // Light grid → easier to read scores across 27 categories
    jitter->SetGridVisible(true);
    jitter->SetGridColor(Color(220, 220, 220, 120));
    
    // Single statistical overlay — median only, matching the original
    jitter->SetShowMedianMarker(true);
    jitter->SetMedianMarkerStyle(Color(0, 0, 0, 255), 2.0f);
    
    jitter->SetChartTitle("Score");
    jitter->SetEnableTooltips(true);
    
    tab->AddChild(jitter);
    
    // Filter strip just below the chart
    int filterY = kChartTopY + kChartHeight + 25;  // = 550
    AddBrazilFilterStrip(tab, jitter, filterY, 2110);
    
    auto legendLabel = std::make_shared<UltraCanvasLabel>(
        "BrazilLegendClassic", 50, filterY + 50, kChartWidth, 60);
    legendLabel->SetText(
        "Regions: NorthEast (Emerald) | North (Teal) | Central-West (Orange) | SouthEast (Red/Pink) | South (Blue)\n"
        "Black horizontal lines indicate median score per state.\n"
        "Total: 27 states with ~700 schools represented."
    );
    legendLabel->SetFontSize(10);
    legendLabel->SetTextColor(Color(100, 100, 100, 255));
    tab->AddChild(legendLabel);
    
    return tab;
}

// =============================================================================
// TAB 2 BUILDER: BEESWARM PACKING (same Brazil dataset)
// =============================================================================
// Same data as Tab 1 but rendered with non-overlapping beeswarm packing.
// Interactive controls let the user switch method/priority/corral live.
// (Note: the v1.2.1 cache invalidation fix in UltraCanvasJitterPlotElement.cpp
// is required for these dropdowns to actually re-render the layout.)

static std::shared_ptr<UltraCanvasContainer> BuildBrazilBeeswarmTab(
        const BrazilDataset& ds) {
    auto tab = std::make_shared<UltraCanvasContainer>("BrazilBeeswarmTab", 0, 0,
                                                      kTabContentW, kTabContentH);
    
    auto descLabel = std::make_shared<UltraCanvasLabel>(
        "BrazilDescBee", 50, 10, kChartWidth, 25);
    descLabel->SetText("Beeswarm packing — points are placed with no overlap, revealing exact "
                       "density of each state's score distribution. Switch method/priority/corral/side below.");
    descLabel->SetFontSize(11);
    descLabel->SetTextColor(Color(90, 90, 90, 255));
    tab->AddChild(descLabel);
    
    auto jitter = CreateJitterPlotElement("brazilScoresBee",
                                          kChartLeftMargin, kChartTopY,
                                          kChartWidth, kChartHeight);
    LoadBrazilDataIntoChart(jitter, ds);
    
    // Beeswarm settings
    jitter->SetJitterDistribution(JitterDistribution::Beeswarm);
    jitter->SetBeeswarmMethod(BeeswarmMethod::Swarm);
    jitter->SetBeeswarmPriority(BeeswarmPriority::Ascending);
    jitter->SetBeeswarmCorral(BeeswarmCorral::Clamp);
    jitter->SetBeeswarmSide(BeeswarmSide::Both);
    jitter->SetBeeswarmSpacing(0.5f);
    jitter->SetBeeswarmMaxWidth(0.85f);
    
    // Point appearance — slightly larger + edges so swarm structure reads clearly
    jitter->SetPointSize(4.0f);
    jitter->SetPointAlpha(0.85f);   // higher alpha — no overlap, can show solid color
    jitter->SetPointShape(UltraCanvasJitterPlotElement::PointShape::Circle);
    jitter->SetPointEdgeColor(Color(255, 255, 255, 220));
    jitter->SetPointEdgeWidth(0.6f);
    
    // Light grid + median line for reference
    jitter->SetGridVisible(true);
    jitter->SetGridColor(Color(220, 220, 220, 120));
    jitter->SetShowMedianMarker(true);
    jitter->SetMedianMarkerStyle(Color(0, 0, 0, 255), 2.0f);
    
    jitter->SetChartTitle("Score");
    jitter->SetEnableTooltips(true);
    
    tab->AddChild(jitter);
    
    // ===== BEESWARM-SPECIFIC INTERACTIVE CONTROLS (row 1) =====
    int ctrlY = kChartTopY + kChartHeight + 25;  // = 550
    
    auto methodLabel = std::make_shared<UltraCanvasLabel>("BeeMethodLabel", 50, ctrlY, 60, 32);
    methodLabel->SetText("Method:");
    methodLabel->SetFontSize(12);
    methodLabel->SetTextColor(Color(60, 60, 60, 255));
    methodLabel->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
    tab->AddChild(methodLabel);
    
    auto methodDropdown = std::make_shared<UltraCanvasDropdown>("BeeMethodDD", 110, ctrlY, 110, 32);
    methodDropdown->AddItem("Swarm");
    methodDropdown->AddItem("Center");
    methodDropdown->AddItem("Hex");
    methodDropdown->AddItem("Square");
    methodDropdown->SetSelectedIndex(0);
    methodDropdown->onSelectionChanged = [jitter](int index, const DropdownItem& item) {
        BeeswarmMethod m = BeeswarmMethod::Swarm;
        if (index == 1) m = BeeswarmMethod::Center;
        else if (index == 2) m = BeeswarmMethod::Hex;
        else if (index == 3) m = BeeswarmMethod::Square;
        jitter->SetBeeswarmMethod(m);
    };
    tab->AddChild(methodDropdown);
    
    auto prioLabel = std::make_shared<UltraCanvasLabel>("BeePrioLabel", 240, ctrlY, 60, 32);
    prioLabel->SetText("Priority:");
    prioLabel->SetFontSize(12);
    prioLabel->SetTextColor(Color(60, 60, 60, 255));
    prioLabel->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
    tab->AddChild(prioLabel);
    
    auto prioDropdown = std::make_shared<UltraCanvasDropdown>("BeePrioDD", 300, ctrlY, 120, 32);
    prioDropdown->AddItem("Ascending");
    prioDropdown->AddItem("Descending");
    prioDropdown->AddItem("Random");
    prioDropdown->AddItem("Compact");
    prioDropdown->AddItem("Dense");
    prioDropdown->SetSelectedIndex(0);
    prioDropdown->onSelectionChanged = [jitter](int index, const DropdownItem& item) {
        BeeswarmPriority p = BeeswarmPriority::Ascending;
        if (index == 1) p = BeeswarmPriority::Descending;
        else if (index == 2) p = BeeswarmPriority::Random;
        else if (index == 3) p = BeeswarmPriority::Compact;
        else if (index == 4) p = BeeswarmPriority::Dense;
        jitter->SetBeeswarmPriority(p);
    };
    tab->AddChild(prioDropdown);
    
    auto corralLabel = std::make_shared<UltraCanvasLabel>("BeeCorralLabel", 440, ctrlY, 55, 32);
    corralLabel->SetText("Corral:");
    corralLabel->SetFontSize(12);
    corralLabel->SetTextColor(Color(60, 60, 60, 255));
    corralLabel->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
    tab->AddChild(corralLabel);
    
    auto corralDropdown = std::make_shared<UltraCanvasDropdown>("BeeCorralDD", 495, ctrlY, 100, 32);
    corralDropdown->AddItem("None");
    corralDropdown->AddItem("Open");
    corralDropdown->AddItem("Wrap");
    corralDropdown->AddItem("Clamp");
    corralDropdown->SetSelectedIndex(3);  // matches Clamp default
    corralDropdown->onSelectionChanged = [jitter](int index, const DropdownItem& item) {
        BeeswarmCorral c = BeeswarmCorral::NoneCorral;
        if (index == 1) c = BeeswarmCorral::Open;
        else if (index == 2) c = BeeswarmCorral::Wrap;
        else if (index == 3) c = BeeswarmCorral::Clamp;
        jitter->SetBeeswarmCorral(c);
    };
    tab->AddChild(corralDropdown);
    
    auto sideLabel = std::make_shared<UltraCanvasLabel>("BeeSideLabel", 615, ctrlY, 40, 32);
    sideLabel->SetText("Side:");
    sideLabel->SetFontSize(12);
    sideLabel->SetTextColor(Color(60, 60, 60, 255));
    sideLabel->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
    tab->AddChild(sideLabel);
    
    auto sideDropdown = std::make_shared<UltraCanvasDropdown>("BeeSideDD", 655, ctrlY, 90, 32);
    sideDropdown->AddItem("Both");
    sideDropdown->AddItem("Left");
    sideDropdown->AddItem("Right");
    sideDropdown->SetSelectedIndex(0);
    sideDropdown->onSelectionChanged = [jitter](int index, const DropdownItem& item) {
        BeeswarmSide s = BeeswarmSide::Both;
        if (index == 1) s = BeeswarmSide::Left;
        else if (index == 2) s = BeeswarmSide::Right;
        jitter->SetBeeswarmSide(s);
    };
    tab->AddChild(sideDropdown);
    
    // Filter strip on second control row
    AddBrazilFilterStrip(tab, jitter, ctrlY + 40, 2220);
    
    auto legendLabel = std::make_shared<UltraCanvasLabel>(
        "BrazilLegendBee", 50, ctrlY + 80, kChartWidth, 30);
    legendLabel->SetText(
        "Beeswarm methods: Swarm (greedy, organic) | Center (symmetric grid) | Hex (hexagonal grid) | Square (square grid). "
        "Priority controls placement order — try Compact for tightest fit, Dense to fill busy regions first."
    );
    legendLabel->SetFontSize(10);
    legendLabel->SetTextColor(Color(100, 100, 100, 255));
    tab->AddChild(legendLabel);
    
    return tab;
}

// =============================================================================
// EXAMPLE 1: BRAZIL SCHOOL SCORES — TABBED (Classic + Beeswarm)
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateBrazilSchoolScoresExample() {
    auto container = std::make_shared<UltraCanvasContainer>(
        "BrazilExample", 0, 0, kOuterWidth, kOuterHeight);
    
    auto titleLabel = std::make_shared<UltraCanvasLabel>(
        "BrazilTitle", 50, 15, kOuterWidth - 100, 35);
    titleLabel->SetText("2009 Ideb Score of Schools in Brazil");
    titleLabel->SetFontSize(20.0f);
    titleLabel->SetTextColor(Color(30, 30, 30, 255));
    titleLabel->SetFontWeight(FontWeight::Bold);
    container->AddChild(titleLabel);
    
    // Build the dataset ONCE so both tabs render identical data
    BrazilDataset ds = BuildBrazilDataset();
    
    // Tabbed container — Classic on left, Beeswarm on right
    auto tabs = std::make_shared<UltraCanvasTabbedContainer>(
        "BrazilTabs", 0, 55, kOuterWidth, kOuterHeight - 55);
    tabs->SetTabStyle(TabStyle::Rounded);
    tabs->SetTabHeight(32);
    
    tabs->AddTab("Classic Jitter",   BuildBrazilClassicJitterTab(ds));
    tabs->AddTab("Beeswarm Packing", BuildBrazilBeeswarmTab(ds));
    tabs->SetActiveTab(0);
    
    container->AddChild(tabs);
    
    return container;
}

// =============================================================================
// EXAMPLE 2: CONTINENTS POPULATION
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateContinentsPopulationExample() {
    auto container = std::make_shared<UltraCanvasContainer>("ContinentsExample", 0, 0, 900, 700);
    
    auto titleLabel = std::make_shared<UltraCanvasLabel>("ContTitle", 50, 20, 800, 30);
    titleLabel->SetText("Continental Population Distribution");
    titleLabel->SetFontSize(18.0f);
    titleLabel->SetTextColor(Color(40, 40, 40, 255));
    container->AddChild(titleLabel);
    
    auto jitter = CreateJitterPlotElement("continents", 50, 70, 700, 500);
    
    std::vector<std::string> continents = {"Africa", "Americas", "Asia", "Europe", "Oceania"};
    jitter->SetCategories(continents);
    
    std::random_device rd;
    std::mt19937 gen(123);
    
    struct ContinentData {
        double mean;
        double stddev;
        int count;
        Color color;
    };
    
    std::map<std::string, ContinentData> contData = {
        {"Africa",   {45.0, 15.0, 80, Color(100, 150, 250, 180)}},
        {"Americas", {55.0, 20.0, 70, Color(255, 180, 100, 180)}},
        {"Asia",     {65.0, 25.0, 90, Color(255, 100, 100, 180)}},
        {"Europe",   {50.0, 12.0, 75, Color(100, 200, 150, 180)}},
        {"Oceania",  {30.0, 8.0,  50, Color(200, 150, 255, 180)}}
    };
    
    for (const auto& cont : continents) {
        const auto& data = contData[cont];
        std::normal_distribution<> dist(data.mean, data.stddev);
        
        std::vector<double> values;
        for (int i = 0; i < data.count; ++i) {
            values.push_back(std::max(10.0, dist(gen)));
        }
        
        jitter->AddCategoryData(cont, values);
    }
    
    jitter->SetJitterAmount(0.3f);
    jitter->SetPointSize(4.0f);
    jitter->SetPointAlpha(0.6f);
    jitter->SetShowMedianMarker(true);
    jitter->SetChartTitle("Population (millions)");
    jitter->SetEnableTooltips(true);
    
    container->AddChild(jitter);
    
    return container;
}

// =============================================================================
// EXAMPLE 3: MARKET CAPITALIZATION - LOGARITHMIC SCALE
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateMarketCapitalizationExample() {
    auto container = std::make_shared<UltraCanvasContainer>("MarketCapExample", 0, 0, 1000, 750);
    
    auto titleLabel = std::make_shared<UltraCanvasLabel>("MarketTitle", 50, 20, 900, 30);
    titleLabel->SetText("Market Capitalization Jitter Chart");
    titleLabel->SetFontSize(20.0f);
    titleLabel->SetTextColor(Color(220, 220, 220, 255));
    container->AddChild(titleLabel);
    
    auto jitter = CreateJitterPlotElement("marketCap", 50, 70, 900, 550);
    
    std::vector<std::string> marketTiers = {
        "1-Nano", "2-Micro", "3-Small", "4-Mid", "5-Large", "6-Mega"
    };
    
    jitter->SetCategories(marketTiers);
    jitter->SetYAxisScale(AxisScale::Logarithmic);
    jitter->SetLogScaleBase(10.0);
    
    std::vector<Color> tierColors = {
        Color(255, 140, 100, 220),
        Color(255, 100, 120, 220),
        Color(100, 200, 220, 220),
        Color(120, 200, 140, 220),
        Color(255, 220, 100, 220),
        Color(200, 150, 255, 220)
    };
    
    std::random_device rd;
    std::mt19937 gen(456);
    
    std::vector<std::pair<double, double>> ranges = {
        {10, 100},
        {100, 500},
        {500, 5000},
        {5000, 20000},
        {20000, 400000},
        {400000, 10000000}
    };
    
    for (size_t tier = 0; tier < marketTiers.size(); ++tier) {
        double logMin = std::log(ranges[tier].first);
        double logMax = std::log(ranges[tier].second);
        std::uniform_real_distribution<> logDist(logMin, logMax);
        
        int numCompanies = 200 - (tier * 25);
        
        std::vector<double> marketCaps;
        for (int i = 0; i < numCompanies; ++i) {
            marketCaps.push_back(std::exp(logDist(gen)));
        }
        
        jitter->AddCategoryData(marketTiers[tier], marketCaps);
    }
    
    jitter->SetChartTitle("Market Cap ($");
    jitter->SetEnableTooltips(true);
    
    container->AddChild(jitter);
    
    auto infoLabel = std::make_shared<UltraCanvasLabel>("MarketInfo", 50, 640, 900, 80);
    infoLabel->SetText(
        "Logarithmic Y-axis scale displays data spanning 6 orders of magnitude.\n"
        "Total: ~900 companies across all market capitalizations."
    );
    infoLabel->SetFontSize(10);
    infoLabel->SetTextColor(Color(180, 180, 180, 255));
    container->AddChild(infoLabel);
    
    return container;
}

// =============================================================================
// EXAMPLE 4: BOX PLOT OVERLAY - INTERACTIVE HYBRID
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateBoxPlotOverlayExample() {
    auto container = std::make_shared<UltraCanvasContainer>("BoxPlotExample", 0, 0, 900, 750);
    
    auto titleLabel = std::make_shared<UltraCanvasLabel>("BoxTitle", 50, 20, 800, 30);
    titleLabel->SetText("Jittered Box Plot - Statistical Distribution");
    titleLabel->SetFontSize(18.0f);
    titleLabel->SetTextColor(Color(40, 40, 40, 255));
    container->AddChild(titleLabel);
    
    auto jitter = CreateJitterPlotElement("boxPlotJitter", 50, 70, 800, 500);
    
    std::vector<std::string> groups = {"Group A", "Group B", "Group C", "Group D"};
    jitter->SetCategories(groups);
    jitter->SetHybridMode(JitterHybridMode::JitterBoxPlot);
    
    std::random_device rd;
    std::mt19937 gen(789);
    
    std::normal_distribution<> distA(12.0, 4.0);
    std::vector<double> dataA;
    for (int i = 0; i < 60; ++i) {
        dataA.push_back(std::clamp(distA(gen), 0.0, 35.0));
    }
    dataA.push_back(30.0);
    dataA.push_back(32.0);
    
    std::normal_distribution<> distB(8.0, 3.5);
    std::vector<double> dataB;
    for (int i = 0; i < 55; ++i) {
        dataB.push_back(std::clamp(distB(gen), -5.0, 25.0));
    }
    dataB.push_back(-2.0);
    
    std::normal_distribution<> distC(24.0, 6.0);
    std::vector<double> dataC;
    for (int i = 0; i < 50; ++i) {
        dataC.push_back(std::clamp(distC(gen), 5.0, 45.0));
    }
    
    std::normal_distribution<> distD1(18.0, 3.0);
    std::normal_distribution<> distD2(34.0, 3.0);
    std::vector<double> dataD;
    for (int i = 0; i < 40; ++i) {
        dataD.push_back(i % 2 == 0 ? distD1(gen) : distD2(gen));
    }
    dataD.push_back(45.0);
    dataD.push_back(2.0);
    
    jitter->AddCategoryData("Group A", dataA);
    jitter->AddCategoryData("Group B", dataB);
    jitter->AddCategoryData("Group C", dataC);
    jitter->AddCategoryData("Group D", dataD);
    
    jitter->SetBoxPlotSettings(true, true, 0.6f);
    jitter->SetBoxPlotColors(
        Color(150, 180, 220, 120),
        Color(80, 120, 180, 255),
        2.0f
    );
    
    jitter->SetJitterAmount(0.15f);
    jitter->SetPointSize(4.0f);
    jitter->SetPointAlpha(0.6f);
    jitter->SetChartTitle("Data Value");
    jitter->SetEnableTooltips(true);
    
    container->AddChild(jitter);
    
    int btnY = 590;
    int btnX = 50;
    
    auto btnToggleWhiskers = std::make_shared<UltraCanvasButton>("btnWhiskers", btnX, btnY, 150, 35);
    btnToggleWhiskers->SetText("Toggle Whiskers");
    btnToggleWhiskers->SetOnClick([jitter]() {
        static bool showWhiskers = true;
        showWhiskers = !showWhiskers;
        jitter->SetBoxPlotSettings(showWhiskers, true, 0.6f);
    });
    container->AddChild(btnToggleWhiskers);
    
    auto btnToggleOutliers = std::make_shared<UltraCanvasButton>("btnOutliers", btnX + 160, btnY, 150, 35);
    btnToggleOutliers->SetText("Toggle Outliers");
    btnToggleOutliers->SetOnClick([jitter]() {
        static bool showOutliers = true;
        showOutliers = !showOutliers;
        jitter->SetBoxPlotSettings(true, showOutliers, 0.6f);
    });
    container->AddChild(btnToggleOutliers);
    
    auto infoLabel = std::make_shared<UltraCanvasLabel>("BoxInfo", 50, 645, 800, 80);
    infoLabel->SetText(
        "Box plot components: IQR box (Q1-Q3), thick median line, whiskers (1.5xIQR), outlier circles.\n"
        "Jittered points show individual data distribution."
    );
    infoLabel->SetFontSize(10);
    infoLabel->SetTextColor(Color(100, 100, 100, 255));
    container->AddChild(infoLabel);
    
    return container;
}

// =============================================================================
// EXAMPLE 5: SCIENTIFIC CROSS MEANS - PUBLICATION QUALITY
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateScientificCrossMeansExample() {
    auto container = std::make_shared<UltraCanvasContainer>("ScientificExample", 0, 0, 900, 750);
    
    auto titleLabel = std::make_shared<UltraCanvasLabel>("SciTitle", 50, 20, 800, 30);
    titleLabel->SetText("Muscle Contraction Mechanics (Publication Style)");
    titleLabel->SetFontSize(18.0f);
    titleLabel->SetTextColor(Color(40, 40, 40, 255));
    container->AddChild(titleLabel);
    
    auto jitter = CreateJitterPlotElement("muscleData", 50, 70, 700, 500);
    
    std::vector<std::string> species = {"Cheetah", "Impala", "Lion", "Zebra"};
    jitter->SetCategories(species);
    jitter->SetHybridMode(JitterHybridMode::JitterBoxPlot);
    
    std::random_device rd;
    std::mt19937 gen(999);
    
    std::vector<std::pair<double, double>> speciesMeans = {
        {100.0, 15.0},
        {85.0, 12.0},
        {105.0, 18.0},
        {90.0, 10.0}
    };
    
    for (size_t i = 0; i < species.size(); ++i) {
        std::normal_distribution<> dist(speciesMeans[i].first, speciesMeans[i].second);
        std::vector<double> measurements;
        
        int sampleSize = 25 + (rand() % 10);
        for (int j = 0; j < sampleSize; ++j) {
            measurements.push_back(std::max(20.0, dist(gen)));
        }
        
        jitter->AddCategoryData(species[i], measurements);
    }
    
    jitter->SetBoxPlotSettings(true, true, 0.5f);
    jitter->SetBoxPlotColors(
        Color(235, 235, 235, 180),
        Color(120, 120, 120, 255),
        1.5f
    );
    
    jitter->SetShowMeanMarker(true);
    jitter->SetMeanMarkerShape(MeanMarkerShape::Cross);
    jitter->SetMeanMarkerStyle(Color(0, 0, 0, 255), 8.0f);
    
    jitter->SetJitterAmount(0.08f);
    jitter->SetPointSize(3.5f);
    jitter->SetPointAlpha(0.7f);
    jitter->SetChartTitle("Peak Power (W/kg)");
    jitter->SetEnableTooltips(true);
    
    container->AddChild(jitter);
    
    auto legendLabel = std::make_shared<UltraCanvasLabel>("SciLegend", 50, 590, 800, 100);
    legendLabel->SetText(
        "Mean marker: Black cross (+) symbol indicates mean value per species.\n"
        "Box plot: Light gray IQR box, dark borders, whiskers at 1.5xIQR."
    );
    legendLabel->SetFontSize(9);
    legendLabel->SetTextColor(Color(100, 100, 100, 255));
    container->AddChild(legendLabel);
    
    return container;
}

// =============================================================================
// EXAMPLE 6: RAINCLOUD PLOT
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateRaincloudPlotExample() {
    auto container = std::make_shared<UltraCanvasContainer>("RaincloudExample", 0, 0, 900, 700);
    
    auto titleLabel = std::make_shared<UltraCanvasLabel>("RaincloudTitle", 50, 20, 800, 30);
    titleLabel->SetText("Raincloud Plot - Half-Violin + Jitter");
    titleLabel->SetFontSize(18.0f);
    titleLabel->SetTextColor(Color(40, 40, 40, 255));
    container->AddChild(titleLabel);
    
    auto jitter = CreateJitterPlotElement("raincloud", 50, 70, 800, 500);
    
    std::vector<std::string> groups = {"Control", "Treatment A", "Treatment B"};
    jitter->SetCategories(groups);
    jitter->SetHybridMode(JitterHybridMode::RaincloudPlot);
    
    std::random_device rd;
    std::mt19937 gen(111);
    
    for (const auto& group : groups) {
        double baseMean = (group == "Control") ? 50.0 : (group == "Treatment A") ? 65.0 : 75.0;
        double std = 12.0;
        
        std::normal_distribution<> dist(baseMean, std);
        std::vector<double> values;
        
        for (int i = 0; i < 80; ++i) {
            values.push_back(std::max(10.0, std::clamp(dist(gen), 10.0, 100.0)));
        }
        
        jitter->AddCategoryData(group, values);
    }
    
    jitter->SetViolinSettings(
        Color(180, 200, 220, 100),
        Color(80, 100, 140, 200),
        1.5f,
        40
    );
    
    jitter->SetJitterAmount(0.2f);
    jitter->SetPointSize(3.0f);
    jitter->SetPointAlpha(0.6f);
    jitter->SetChartTitle("Response Value");
    jitter->SetEnableTooltips(true);
    
    container->AddChild(jitter);
    
    auto infoLabel = std::make_shared<UltraCanvasLabel>("RaincloudInfo", 50, 590, 800, 80);
    infoLabel->SetText(
        "Raincloud plot combines half-violin (density) on left with jittered points on right.\n"
        "Provides both distribution shape and individual data points in one visualization."
    );
    infoLabel->SetFontSize(10);
    infoLabel->SetTextColor(Color(100, 100, 100, 255));
    container->AddChild(infoLabel);
    
    return container;
}

// =============================================================================
// MAIN ENTRY POINT
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateJitterPlotExamples() {
    return CreateBrazilSchoolScoresExample();
}

} // namespace UltraCanvas