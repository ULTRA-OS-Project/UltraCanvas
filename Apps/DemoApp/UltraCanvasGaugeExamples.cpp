// Apps/DemoApp/UltraCanvasGaugeExamples.cpp
// Comprehensive examples of gauge element modes using UltraCanvas layout managers
// Version: 2.4.0
// Last Modified: 2026-06-18
// Author: UltraCanvas Framework
// V2.4.0 changelog: Added a dedicated "Round Gauges" tab — an interactive
//   playground (value/width/segment-count sliders + indicator-style,
//   segment-style and surface-fill dropdowns) plus a 2x3 showcase of round-gauge
//   style presets (solid arc, segmented blocks, dashed bars, dots, straight &
//   waved liquid fills).
// V2.3.0 changelog: Removed Speed sub-dial config (V2.6.1 dark renderer omits it).
// V2.2.0 changelog: Speedometer demo tuned for the V2.6.0 dark automotive look —
//   set value to 109, dropped colored range arcs (renderer now uses graduated
//   tick colors), sub-dial relabeled MPH and recentred.

#include "UltraCanvasDemo.h"
#include "Plugins/Diagrams/UltraCanvasGaugeDiagramElement.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasApplication.h"
#include <sstream>
#include <iomanip>

namespace UltraCanvas {

// =============================================================================
// LAYOUT CONSTANTS (V2.1)
// =============================================================================

namespace {
    constexpr float kCardW = 272;
    constexpr float kCardH = 374;
    constexpr float kSliderH = 22;
    constexpr float kValueLabelH = 20;
    constexpr float kCardPadding = 10;

    // ===== CSS-layout helpers (replace the old box/grid layout managers) =====
    inline void SetVBox(const std::shared_ptr<UltraCanvasContainer>& c, float gap) {
        c->layout.SetFlexColumn().SetFlexGap(gap)
                 .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    }
    inline void SetHBox(const std::shared_ptr<UltraCanvasContainer>& c, float gap) {
        c->layout.SetFlexRow().SetFlexGap(gap)
                 .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    }
    // Add a flex child with the given grow factor, stretched on the cross axis.
    inline void AddFlex(const std::shared_ptr<UltraCanvasContainer>& parent,
                        const std::shared_ptr<UltraCanvasUIElement>& child, float grow) {
        child->layoutItem.SetFlexGrow(grow).SetFlexShrink(grow > 0 ? 1.f : 0.f)
                         .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        parent->AddChild(child);
    }
    // 3-column x 2-row equal (1fr) grid.
    inline void SetGrid2x3(const std::shared_ptr<UltraCanvasContainer>& c, float gap) {
        using namespace CSSLayout;
        auto fr = [] { return GridTrackSize{GridTrackSizeKind::Fr, Dimension::Fr(1)}; };
        c->layout.SetGrid()
                 .SetGridColumns({fr(), fr(), fr()})
                 .SetGridRows({fr(), fr()})
                 .SetGridGap(gap);
    }
    // 3-column x 3-row equal (1fr) grid.
    inline void SetGrid3x3(const std::shared_ptr<UltraCanvasContainer>& c, float gap) {
        using namespace CSSLayout;
        auto fr = [] { return GridTrackSize{GridTrackSizeKind::Fr, Dimension::Fr(1)}; };
        c->layout.SetGrid()
                 .SetGridColumns({fr(), fr(), fr()})
                 .SetGridRows({fr(), fr(), fr()})
                 .SetGridGap(gap);
    }
    inline void AddGrid(const std::shared_ptr<UltraCanvasContainer>& parent,
                        const std::shared_ptr<UltraCanvasUIElement>& child, int row, int col) {
        child->layoutItem.SetGridRowColSimplified(row, col);
        parent->AddChild(child);
    }
}

// =============================================================================
// HELPER: Create gauge + slider + label card
// =============================================================================

static std::shared_ptr<UltraCanvasContainer> CreateGaugeCard(
    const std::string& id,
    float w, float h,
    std::shared_ptr<UltraCanvasGaugeDiagramElement> gauge,
    float sliderMin, float sliderMax, float sliderInit,
    const std::string& valueSuffix,
    int decimals = 0,
    float sliderStep = -1.0f) {

    auto card = std::make_shared<UltraCanvasContainer>(id, 0, 0, w, h);
    card->SetBackgroundColor(Color(255, 255, 255, 255));
    card->SetBorders(1.0f, Color(218, 219, 228, 255));
    card->SetPadding(kCardPadding);

    SetVBox(card, 8);

    // Gauge takes the upper portion (grows); the engine sizes it to its flex cell.
    auto gaugeWrap = std::make_shared<UltraCanvasContainer>(id + "_GW", 0, 0, 0, 0);
    SetVBox(gaugeWrap, 0);
    AddFlex(gaugeWrap, gauge, 1);
    AddFlex(card, gaugeWrap, 1);

    // Slider for interactive control
    auto slider = std::make_shared<UltraCanvasSlider>(id + "_Sl", 0, 0, 0, kSliderH);
    slider->SetOrientation(SliderOrientation::Horizontal);
    slider->SetRange(sliderMin, sliderMax);
    slider->SetValue(sliderInit);
    if (sliderStep >= 0.0f) slider->SetStep(sliderStep);
    AddFlex(card, slider, 0);

    // Value display label below slider
    auto valueLabel = std::make_shared<UltraCanvasLabel>(id + "_V", 0, 0, 0, kValueLabelH);
    valueLabel->SetAlignment(TextAlignment::Center);
    valueLabel->SetFontSize(11);
    valueLabel->SetTextColor(Color(100, 100, 115, 255));
    std::ostringstream initVal;
    initVal << std::fixed << std::setprecision(decimals) << sliderInit << " " << valueSuffix;
    valueLabel->SetText(initVal.str());
    AddFlex(card, valueLabel, 0);

    auto gaugePtr = gauge.get();
    auto labelPtr = valueLabel.get();
    slider->onValueChanged = [gaugePtr, labelPtr, valueSuffix, decimals](float val) {
        gaugePtr->SetValue(static_cast<double>(val));
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(decimals) << val << " " << valueSuffix;
        labelPtr->SetText(oss.str());
    };

    gauge->SetValue(static_cast<double>(sliderInit));

    return card;
}

// =============================================================================
// TAB 1: ANALOG GAUGES
// =============================================================================

static std::shared_ptr<UltraCanvasContainer> BuildAnalogTab(float w, float h) {
    auto tab = std::make_shared<UltraCanvasContainer>("AnalogTab", 0, 0, w, h);
    tab->SetBackgroundColor(Color(240, 241, 248, 255));
    tab->SetPadding(12);

    SetVBox(tab, 10);

    auto title = std::make_shared<UltraCanvasLabel>("AnaTitle", 0, 0, w - 24, 28);
    title->SetText("Analog Gauges - Speedometer, Semicircular, Compass, Clock, Stopwatch");
    title->SetFontSize(16);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(Color(50, 50, 75, 255));
    AddFlex(tab, title, 0);

    auto gridContainer = std::make_shared<UltraCanvasContainer>("AnaGrid", 0, 0, w - 24, h - 60);
    SetGrid2x3(gridContainer, 12);

    // --- Speedometer with RPM sub-dial ---
    auto speedo = CreateGaugeDiagramElement("speedo", 0, 0, kCardW, kCardH);
    speedo->SetMode(GaugeMode::Speedometer);
    speedo->SetTitle("Speed");
    speedo->SetUnit("km/h");
    speedo->SetMaxValue(240.0);
    speedo->SetMajorTickCount(6);
    speedo->SetValue(109.0);  // V2.2: non-zero so the orange needle + LCD read like the photo
    // V2.3: sub-dial removed for the dark Speedometer (V2.6.1 renderer omits it).

    auto speedoCard = CreateGaugeCard("speedo_c", kCardW, kCardH, speedo, 0.0f, 240.0f, 0.0f, "km/h");
    AddGrid(gridContainer, speedoCard, 0, 0);

    // --- Semicircular Sales Assessment ---
    auto semi = CreateGaugeDiagramElement("semi", 0, 0, kCardW, kCardH);
    semi->SetMode(GaugeMode::Semicircular);
    semi->SetTitle("Sales Assessment");
    semi->SetUnit("k$");
    semi->SetMinValue(0.0);
    semi->SetMaxValue(100.0);
    semi->SetMajorTickCount(5);
    semi->AddRange(GaugeRangeSegment(0.0, 50.0, Color(100, 180, 255, 255), ""));
    semi->AddRange(GaugeRangeSegment(50.0, 100.0, Color(80, 200, 120, 255), ""));

    auto semiCard = CreateGaugeCard("semi_c", kCardW, kCardH, semi, 0.0f, 100.0f, 73.0f, "k$");
    AddGrid(gridContainer, semiCard, 0, 1);

    auto compass = CreateGaugeDiagramElement("compass", 0, 0, kCardW, kCardH);
    compass->SetMode(GaugeMode::Compass);
    compass->SetTitle("Wind Direction");
    compass->SetMaxValue(360.0);
    compass->SetNeedleStyle(GaugeNeedleStyle::Thin);

    GaugeSubDial wind;
    wind.enabled = true;
    wind.mode = GaugeMode::Semicircular;
    wind.title = "Wind";
    wind.minValue = 0.0;
    wind.maxValue = 25.0;
    wind.currentValue = 12.0;
    wind.unit = "m/s";
    compass->SetSubDial(wind);

    auto compassCard = CreateGaugeCard("compass_c", kCardW, kCardH, compass, 0.0f, 360.0f, 120.0f, "\xC2\xB0");
    AddGrid(gridContainer, compassCard, 0, 2);

    // --- Analog Clock ---
    auto clock = CreateGaugeDiagramElement("clock", 0, 0, kCardW, kCardH);
    clock->SetMode(GaugeMode::AnalogClock);
    clock->SetTitle("Clock");

    auto clockCard = std::make_shared<UltraCanvasContainer>("clock_c", 0, 0, kCardW, kCardH);
    clockCard->SetBackgroundColor(Color(255, 255, 255, 255));
    clockCard->SetBorders(1.0f, Color(218, 219, 228, 255));
    clockCard->SetPadding(kCardPadding);
    SetVBox(clockCard, 6);
    AddFlex(clockCard, clock, 1);
    auto clockInfo = std::make_shared<UltraCanvasLabel>("clockI", 0, 0, 0, kValueLabelH);
    clockInfo->SetText("Live system time");
    clockInfo->SetAlignment(TextAlignment::Center);
    clockInfo->SetFontSize(11);
    clockInfo->SetTextColor(Color(100, 100, 115, 255));
    AddFlex(clockCard, clockInfo, 0);
    AddGrid(gridContainer, clockCard, 1, 0);

    // --- Stopwatch ---
    auto stopwatch = CreateGaugeDiagramElement("sw", 0, 0, kCardW, kCardH);
    stopwatch->SetMode(GaugeMode::Stopwatch);
    stopwatch->SetTitle("Stopwatch");
    stopwatch->SetMaxValue(60.0);
    stopwatch->SetMajorTickCount(12);

    GaugeSubDial mins;
    mins.enabled = true;
    mins.mode = GaugeMode::Semicircular;
    mins.title = "min";
    mins.minValue = 0.0;
    mins.maxValue = 30.0;
    mins.currentValue = 0.0;
    mins.unit = "m";
    stopwatch->SetSubDial(mins);

    auto swCard = std::make_shared<UltraCanvasContainer>("sw_c", 0, 0, kCardW, kCardH);
    swCard->SetBackgroundColor(Color(255, 255, 255, 255));
    swCard->SetBorders(1.0f, Color(218, 219, 228, 255));
    swCard->SetPadding(kCardPadding);
    {
        SetVBox(swCard, 6);
        auto swGaugeWrap = std::make_shared<UltraCanvasContainer>("sw_GW", 0, 0, 0, 0);
        SetVBox(swGaugeWrap, 0);
        AddFlex(swGaugeWrap, stopwatch, 1);
        AddFlex(swCard, swGaugeWrap, 1);

        // Start/Stop/Reset buttons
        auto swBtnContainer = std::make_shared<UltraCanvasContainer>("sw_Btns", 0, 0, 0, 28);
        SetHBox(swBtnContainer, 6);
        swBtnContainer->size.height = CSSLayout::Dimension::Px(28);
        auto swStart = std::make_shared<UltraCanvasButton>("sw_Start", 0, 0, 80, 26, "Start");
        auto swStop = std::make_shared<UltraCanvasButton>("sw_Stop", 0, 0, 80, 26, "Stop");
        auto swReset = std::make_shared<UltraCanvasButton>("sw_Reset", 0, 0, 80, 26, "Reset");
        AddFlex(swBtnContainer, swStart, 1);
        AddFlex(swBtnContainer, swStop, 1);
        AddFlex(swBtnContainer, swReset, 1);
        AddFlex(swCard, swBtnContainer, 0);

        // Value label
        auto swValue = std::make_shared<UltraCanvasLabel>("sw_V", 0, 0, 0, kValueLabelH);
        swValue->SetAlignment(TextAlignment::Center);
        swValue->SetFontSize(11);
        swValue->SetTextColor(Color(100, 100, 115, 255));
        swValue->SetText("14.90 s");
        AddFlex(swCard, swValue, 0);

        auto swGaugePtr = stopwatch.get();
        auto swLabelPtr = swValue.get();
        swStart->SetOnClick([swGaugePtr] { swGaugePtr->StopwatchStart(); });
        swStop->SetOnClick([swGaugePtr] { swGaugePtr->StopwatchStop(); });
        swReset->SetOnClick([swGaugePtr] {
            swGaugePtr->StopwatchReset();
        });
        stopwatch->onGaugeValueChange = [swLabelPtr](double val) {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2) << val << " s";
            swLabelPtr->SetText(oss.str());
        };
        stopwatch->SetValue(14.9);
    }
    AddGrid(gridContainer, swCard, 1, 1);

    // --- Quadrant Power ---
    auto quad = CreateGaugeDiagramElement("quad", 0, 0, kCardW, kCardH);
    quad->SetMode(GaugeMode::Quadrant);
    quad->SetTitle("Power");
    quad->SetUnit("kW");
    quad->SetMaxValue(50.0);
    quad->SetMajorTickCount(5);
    quad->AddRange(GaugeRangeSegment(0.0, 30.0, Color(0, 200, 140, 255)));
    quad->AddRange(GaugeRangeSegment(30.0, 50.0, Color(255, 160, 60, 255)));

    auto quadCard = CreateGaugeCard("quad_c", kCardW, kCardH, quad, 0.0f, 50.0f, 0.0f, "kW", 1);
    AddGrid(gridContainer, quadCard, 1, 2);

    AddFlex(tab, gridContainer, 1);
    return tab;
}

// =============================================================================
// TAB 2: PROGRESS & LINEAR GAUGES
// =============================================================================

static std::shared_ptr<UltraCanvasContainer> BuildProgressTab(float w, float h) {
    auto tab = std::make_shared<UltraCanvasContainer>("ProgTab", 0, 0, w, h);
    tab->SetBackgroundColor(Color(240, 241, 248, 255));
    tab->SetPadding(12);

    SetVBox(tab, 10);

    auto title = std::make_shared<UltraCanvasLabel>("ProgTitle", 0, 0, w - 24, 28);
    title->SetText("Progress & Linear Gauges - Bar, LED, Segmented, Multi-Pointer, With Arrow");
    title->SetFontSize(16);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(Color(50, 50, 75, 255));
    AddFlex(tab, title, 0);

    auto gridContainer = std::make_shared<UltraCanvasContainer>("ProgGrid", 0, 0, w - 24, h - 60);
    SetGrid2x3(gridContainer, 12);

    // --- Linear Bar download progress ---
    auto bar = CreateGaugeDiagramElement("bar", 0, 0, kCardW, kCardH);
    bar->SetMode(GaugeMode::LinearBar);
    bar->SetTitle("Download Progress");
    bar->SetUnit("%");
    bar->SetGaugeColor(Color(0, 140, 255, 255));
    auto barCard = CreateGaugeCard("bar_c", kCardW, kCardH, bar, 0.0f, 100.0f, 65.0f, "%");
    AddGrid(gridContainer, barCard, 0, 0);

    // --- LED Segmented VU Meter ---
    auto led = CreateGaugeDiagramElement("led", 0, 0, kCardW, kCardH);
    led->SetMode(GaugeMode::LinearLED);
    led->SetOrientation(GaugeOrientation::Vertical);
    led->SetTitle("VU Meter");
    led->SetSegmentCount(20);
    led->SetUnit("%");
    led->SetSegmentCount(16);
    led->SetGaugeColor(Color(0, 200, 255, 255));
    led->AddRange(GaugeRangeSegment(0.0, 80.0, Color(0, 200, 255, 255)));
    led->AddRange(GaugeRangeSegment(80.0, 100.0, Color(255, 100, 80, 255)));
    auto ledCard = CreateGaugeCard("led_c", kCardW, kCardH, led, 0.0f, 100.0f, 65.0f, "%");
    AddGrid(gridContainer, ledCard, 0, 1);

    // --- Segmented Brick Revenue ---
    auto brick = CreateGaugeDiagramElement("brick", 0, 0, kCardW, kCardH);
    brick->SetMode(GaugeMode::LinearSegmented);
    brick->SetTitle("Revenue");
    brick->SetUnit("M$");
    brick->SetMaxValue(20.0);
    brick->SetSegmentCount(20);
    brick->AddRange(GaugeRangeSegment(0.0, 10.0, Color(255, 140, 80, 255)));
    brick->AddRange(GaugeRangeSegment(10.0, 20.0, Color(0, 180, 220, 255)));
    brick->SetDecimalPlaces(2);
    auto brickCard = CreateGaugeCard("brick_c", kCardW, kCardH, brick, 0.0f, 20.0f, 12.45f, "M$", 2);
    AddGrid(gridContainer, brickCard, 0, 2);

    // --- Multi-pointer Recipe Layers ---
    auto multi = CreateGaugeDiagramElement("multi", 0, 0, kCardW, kCardH);
    multi->SetMode(GaugeMode::LinearMultiPointer);
    multi->SetOrientation(GaugeOrientation::Vertical);
    multi->SetTitle("Recipe Layers");
    multi->SetUnit("ml");
    multi->SetMaxValue(2000.0);
    multi->SetGaugeColor(Color(0, 160, 255, 255));
    multi->AddExternalPointer(GaugeExternalPointer(1500.0, Color(0, 160, 255, 255), "Base"));
    multi->AddExternalPointer(GaugeExternalPointer(1660.0, Color(255, 180, 60, 255), "Fill"));
    multi->AddExternalPointer(GaugeExternalPointer(2000.0, Color(180, 180, 190, 255), "Max"));
    auto multiCard = CreateGaugeCard("multi_c", kCardW, kCardH, multi, 0.0f, 2000.0f, 1500.0f, "ml");
    AddGrid(gridContainer, multiCard, 1, 0);

    // --- Linear With Arrow Glucose ---
    auto arrow = CreateGaugeDiagramElement("arrow", 0, 0, kCardW, kCardH);
    arrow->SetMode(GaugeMode::LinearWithArrow);
    arrow->SetOrientation(GaugeOrientation::Vertical);
    arrow->SetTitle("Glucose Level");
    arrow->SetUnit("mmol/l");
    arrow->SetMaxValue(15.0);
    arrow->SetDecimalPlaces(1);
    arrow->AddRange(GaugeRangeSegment(0.0, 5.6, Color(80, 200, 120, 255), "Normal"));
    arrow->AddRange(GaugeRangeSegment(5.6, 7.0, Color(255, 190, 60, 255), "Borderline"));
    arrow->AddRange(GaugeRangeSegment(7.0, 15.0, Color(255, 100, 80, 255), "Elevated"));
    auto arrowCard = CreateGaugeCard("arrow_c", kCardW, kCardH, arrow, 0.0f, 15.0f, 5.7f, "mmol/l", 1);
    AddGrid(gridContainer, arrowCard, 1, 1);

    // --- Linear Scale Radio Tuner ---
    auto scale = CreateGaugeDiagramElement("scale", 0, 0, kCardW, kCardH);
    scale->SetMode(GaugeMode::LinearScale);
    scale->SetTitle("Radio Tuner");
    scale->SetUnit("MHz");
    scale->SetMinValue(180.0);
    scale->SetMaxValue(970.0);
    scale->AddThreshold(GaugeThreshold(180.0, Color(180, 180, 190, 255), "Galaxy"));
    scale->AddThreshold(GaugeThreshold(571.0, Color(180, 180, 190, 255), "Dukes"));
    scale->AddThreshold(GaugeThreshold(780.0, Color(180, 180, 190, 255), "Frasier"));
    scale->AddThreshold(GaugeThreshold(970.0, Color(180, 180, 190, 255), "Simpsons"));
    auto scaleCard = CreateGaugeCard("scale_c", kCardW, kCardH, scale, 180.0f, 970.0f, 571.0f, "MHz");
    AddGrid(gridContainer, scaleCard, 1, 2);

    AddFlex(tab, gridContainer, 1);
    return tab;
}

// =============================================================================
// TAB 3: SPECIALIZED GAUGES
// =============================================================================

static std::shared_ptr<UltraCanvasContainer> BuildSpecializedTab(float w, float h) {
    auto tab = std::make_shared<UltraCanvasContainer>("SpecTab", 0, 0, w, h);
    tab->SetBackgroundColor(Color(240, 241, 248, 255));
    tab->SetPadding(12);

    SetVBox(tab, 10);

    auto title = std::make_shared<UltraCanvasLabel>("SpecTitle", 0, 0, w - 24, 28);
    title->SetText("Specialized Gauges - Battery, Thermometer, Cylinder, Digital Clock, Digital");
    title->SetFontSize(16);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(Color(50, 50, 75, 255));
    AddFlex(tab, title, 0);

    auto gridContainer = std::make_shared<UltraCanvasContainer>("SpecGrid", 0, 0, w - 24, h - 60);
    SetGrid2x3(gridContainer, 12);

    // --- Battery Bar pointer ---
    auto bat1 = CreateGaugeDiagramElement("bat1", 0, 0, kCardW, kCardH);
    bat1->SetMode(GaugeMode::Battery);
    bat1->SetTitle("Battery (Bar)");
    bat1->SetBatteryStyle(GaugeBatteryStyle::BarPointer);
    bat1->SetShowBolt(true);
    auto bat1Card = CreateGaugeCard("bat1_c", kCardW, kCardH, bat1, 0.0f, 100.0f, 75.0f, "%");
    AddGrid(gridContainer, bat1Card, 0, 0);

    // --- Battery LED ---
    auto bat2 = CreateGaugeDiagramElement("bat2", 0, 0, kCardW, kCardH);
    bat2->SetMode(GaugeMode::Battery);
    bat2->SetTitle("Battery (LED)");
    bat2->SetBatteryStyle(GaugeBatteryStyle::LedPointer);
    bat2->SetSegmentCount(10);
    auto bat2Card = CreateGaugeCard("bat2_c", kCardW, kCardH, bat2, 0.0f, 100.0f, 100.0f, "%");
    AddGrid(gridContainer, bat2Card, 0, 1);

    // --- Thermometer ---
    auto thermo = CreateGaugeDiagramElement("thermo", 0, 0, kCardW, kCardH);
    thermo->SetMode(GaugeMode::Thermometer);
    thermo->SetTitle("Temperature");
    thermo->SetUnit("C");
    thermo->SetMinValue(-25.0);
    thermo->SetMaxValue(25.0);
    thermo->SetGaugeColor(Color(0, 180, 255, 255));
    thermo->SetDecimalPlaces(0);
    auto thermoCard = CreateGaugeCard("thermo_c", kCardW, kCardH, thermo, -25.0f, 25.0f, 12.0f, "C");
    AddGrid(gridContainer, thermoCard, 0, 2);

    // --- Cylinder Water ---
    auto cyl = CreateGaugeDiagramElement("cyl", 0, 0, kCardW, kCardH);
    cyl->SetMode(GaugeMode::Cylinder);
    cyl->SetTitle("Water");
    cyl->SetUnit("ml");
    cyl->SetMaxValue(1000.0);
    cyl->SetGaugeColor(Color(0, 200, 200, 255));
    auto cylCard = CreateGaugeCard("cyl_c", kCardW, kCardH, cyl, 0.0f, 1000.0f, 1000.0f, "ml");
    AddGrid(gridContainer, cylCard, 1, 0);

    // --- Digital Clock (LED-style live clock) ---
    auto dclock = CreateGaugeDiagramElement("dclock", 0, 0, kCardW, kCardH);
    dclock->SetMode(GaugeMode::Digital);
    dclock->SetTitle("Clock");
    dclock->SetDigitalClock(true);
    // Prefer an LED-style font if installed; fall back to Monospace otherwise.
    dclock->SetDigitalFontFamily("DSEG7 Classic,DSEG7 Modern,Digital-7,Monospace");
    dclock->SetGaugeColor(Color(0, 230, 80, 255)); // classic LED green
    auto dclockCard = std::make_shared<UltraCanvasContainer>("dclock_c", 0, 0, kCardW, kCardH);
    dclockCard->SetBackgroundColor(Color(255, 255, 255, 255));
    dclockCard->SetBorders(1.0f, Color(218, 219, 228, 255));
    dclockCard->SetPadding(kCardPadding);
    SetVBox(dclockCard, 8);
    {
        auto dclockWrap = std::make_shared<UltraCanvasContainer>("dclock_GW", 0, 0, 0, 0);
        SetVBox(dclockWrap, 0);
        AddFlex(dclockWrap, dclock, 1);
        AddFlex(dclockCard, dclockWrap, 1);
        auto dclockInfo = std::make_shared<UltraCanvasLabel>("dclock_V", 0, 0, 0, kValueLabelH);
        dclockInfo->SetAlignment(TextAlignment::Center);
        dclockInfo->SetFontSize(11);
        dclockInfo->SetTextColor(Color(100, 100, 115, 255));
        dclockInfo->SetText("Live HH:MM:SS");
        AddFlex(dclockCard, dclockInfo, 0);
    }
    AddGrid(gridContainer, dclockCard, 1, 1);

    // --- Digital LED ---
    auto digital = CreateGaugeDiagramElement("digital", 0, 0, kCardW, kCardH);
    digital->SetMode(GaugeMode::Digital);
    digital->SetTitle("LED Display");
    digital->SetUnit("Hz");
    digital->SetMaxValue(9999.0);
    digital->SetDecimalPlaces(1);
    digital->SetShowGlow(true);
    digital->AddRange(GaugeRangeSegment(0.0, 5000.0, Color(0, 180, 255, 255)));
    digital->AddRange(GaugeRangeSegment(5000.0, 8000.0, Color(255, 200, 60, 255)));
    digital->AddRange(GaugeRangeSegment(8000.0, 9999.0, Color(255, 80, 80, 255)));
    auto digCard = CreateGaugeCard("dig_c", kCardW, kCardH, digital, 0.0f, 9999.0f, 1234.5f, "Hz", 1);
    AddGrid(gridContainer, digCard, 1, 2);

    AddFlex(tab, gridContainer, 1);
    return tab;
}

// =============================================================================
// TAB 4: ROUND GAUGES (configurable circular rings)
// =============================================================================

namespace {
    // A small captioned label above a control, packed into a fixed-height block.
    inline std::shared_ptr<UltraCanvasLabel> MakeCaption(const std::string& id,
                                                         const std::string& text) {
        auto lbl = std::make_shared<UltraCanvasLabel>(id, 0, 0, 0, 14);
        lbl->SetText(text);
        lbl->SetFontSize(11);
        lbl->SetTextColor(Color(90, 90, 110, 255));
        return lbl;
    }

    // Apply a named style preset to a CircularRing gauge (used by the showcase cards).
    void ApplyRoundPreset(const std::shared_ptr<UltraCanvasGaugeDiagramElement>& g,
                          GaugeRingStyle rs, GaugeRingSegmentStyle ss,
                          GaugeFillStyle fs, float thickness, int segCount,
                          const Color& color) {
        g->SetMode(GaugeMode::CircularRing);
        g->SetRingStyle(rs);
        g->SetRingSegmentStyle(ss);
        g->SetFillStyle(fs);
        g->SetRingThickness(thickness);
        g->SetRingSegmentCount(segCount);
        g->SetGaugeColor(color);
    }
}

static std::shared_ptr<UltraCanvasContainer> BuildRoundGaugesTab(float w, float h) {
    auto tab = std::make_shared<UltraCanvasContainer>("RoundTab", 0, 0, w, h);
    tab->SetBackgroundColor(Color(240, 241, 248, 255));
    tab->SetPadding(12);
    SetVBox(tab, 10);

    auto title = std::make_shared<UltraCanvasLabel>("RoundTitle", 0, 0, w - 24, 28);
    title->SetText("Round Gauges - ring width, indicator/segment style, surface fill, centre content & faded colours");
    title->SetFontSize(16);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(Color(50, 50, 75, 255));
    AddFlex(tab, title, 0);

    // Body: interactive playground (left) + style showcase grid (right).
    auto body = std::make_shared<UltraCanvasContainer>("RoundBody", 0, 0, w - 24, h - 60);
    SetHBox(body, 12);

    // ---------------------------------------------------------------------
    // LEFT: interactive playground card with full controls
    // ---------------------------------------------------------------------
    auto panel = std::make_shared<UltraCanvasContainer>("RoundPanel", 0, 0, 320, 0);
    panel->size.width = CSSLayout::Dimension::Px(320);
    panel->SetBackgroundColor(Color(255, 255, 255, 255));
    panel->SetBorders(1.0f, Color(218, 219, 228, 255));
    panel->SetPadding(kCardPadding);
    SetVBox(panel, 8);

    auto playGauge = CreateGaugeDiagramElement("round_play", 0, 0, kCardW, kCardH);
    playGauge->SetMode(GaugeMode::CircularRing);
    // No title on the playground gauge: it lets the ring use the full panel
    // height so the main gauge stays clearly the largest one on the tab.
    playGauge->SetUnit("%");
    playGauge->SetGaugeColor(Color(0, 200, 140, 255));
    playGauge->SetRingThickness(10.0f);
    playGauge->SetRingStyle(GaugeRingStyle::SolidArc);
    playGauge->SetValue(74.0);

    auto gaugeWrap = std::make_shared<UltraCanvasContainer>("round_play_GW", 0, 0, 0, 0);
    SetVBox(gaugeWrap, 0);
    AddFlex(gaugeWrap, playGauge, 1);
    AddFlex(panel, gaugeWrap, 1);

    auto gPtr = playGauge.get();

    // --- Value slider ---
    auto valSlider = std::make_shared<UltraCanvasSlider>("round_val", 0, 0, 0, kSliderH);
    valSlider->SetOrientation(SliderOrientation::Horizontal);
    valSlider->SetRange(0.0f, 100.0f);
    valSlider->SetValue(74.0f);
    auto valCap = MakeCaption("round_val_cap", "Value: 74 %");
    auto valCapPtr = valCap.get();
    valSlider->onValueChanged = [gPtr, valCapPtr](float v) {
        gPtr->SetValue(static_cast<double>(v));
        std::ostringstream oss; oss << "Value: " << std::fixed << std::setprecision(0) << v << " %";
        valCapPtr->SetText(oss.str());
    };
    AddFlex(panel, valCap, 0);
    AddFlex(panel, valSlider, 0);

    // --- Indicator width slider ---
    auto widthSlider = std::make_shared<UltraCanvasSlider>("round_width", 0, 0, 0, kSliderH);
    widthSlider->SetOrientation(SliderOrientation::Horizontal);
    widthSlider->SetRange(2.0f, 24.0f);
    widthSlider->SetValue(10.0f);
    auto widthCap = MakeCaption("round_width_cap", "Indicator width: 10 px");
    auto widthCapPtr = widthCap.get();
    widthSlider->onValueChanged = [gPtr, widthCapPtr](float v) {
        gPtr->SetRingThickness(v);
        std::ostringstream oss; oss << "Indicator width: " << std::fixed << std::setprecision(0) << v << " px";
        widthCapPtr->SetText(oss.str());
    };
    AddFlex(panel, widthCap, 0);
    AddFlex(panel, widthSlider, 0);

    // --- Segment count slider ---
    auto countSlider = std::make_shared<UltraCanvasSlider>("round_count", 0, 0, 0, kSliderH);
    countSlider->SetOrientation(SliderOrientation::Horizontal);
    countSlider->SetRange(4.0f, 72.0f);
    countSlider->SetValue(36.0f);
    countSlider->SetStep(1.0f);
    auto countCap = MakeCaption("round_count_cap", "Segment count: 36");
    auto countCapPtr = countCap.get();
    countSlider->onValueChanged = [gPtr, countCapPtr](float v) {
        gPtr->SetRingSegmentCount(static_cast<int>(v));
        std::ostringstream oss; oss << "Segment count: " << static_cast<int>(v);
        countCapPtr->SetText(oss.str());
    };
    AddFlex(panel, countCap, 0);
    AddFlex(panel, countSlider, 0);

    // --- Indicator style dropdown ---
    auto styleCap = MakeCaption("round_style_cap", "Indicator style");
    AddFlex(panel, styleCap, 0);
    auto styleDrop = std::make_shared<UltraCanvasDropdown>("round_style", 0, 0, 0, 26);
    styleDrop->AddItem("Solid line arc");
    styleDrop->AddItem("Segmented");
    styleDrop->AddItem("Dashed / ticks");
    styleDrop->AddItem("Segmented ring (chunky)");
    styleDrop->SetSelectedIndex(0);
    styleDrop->onSelectionChanged = [gPtr](int idx, const DropdownItem&) {
        switch (idx) {
            case 1: gPtr->SetRingStyle(GaugeRingStyle::Segmented); break;
            case 2: gPtr->SetRingStyle(GaugeRingStyle::Dashed); break;
            case 3: gPtr->SetRingStyle(GaugeRingStyle::SegmentedRing); break;
            default: gPtr->SetRingStyle(GaugeRingStyle::SolidArc); break;
        }
    };
    AddFlex(panel, styleDrop, 0);

    // --- Segment style dropdown (Segmented / Dashed) ---
    auto segCap = MakeCaption("round_seg_cap", "Segment style (Segmented / Dashed)");
    AddFlex(panel, segCap, 0);
    auto segDrop = std::make_shared<UltraCanvasDropdown>("round_seg", 0, 0, 0, 26);
    segDrop->AddItem("Blocks (rounded chunks)");
    segDrop->AddItem("Bars (radial ticks)");
    segDrop->AddItem("Dots");
    segDrop->SetSelectedIndex(0);
    segDrop->onSelectionChanged = [gPtr](int idx, const DropdownItem&) {
        switch (idx) {
            case 1: gPtr->SetRingSegmentStyle(GaugeRingSegmentStyle::Bars); break;
            case 2: gPtr->SetRingSegmentStyle(GaugeRingSegmentStyle::Dots); break;
            default: gPtr->SetRingSegmentStyle(GaugeRingSegmentStyle::Blocks); break;
        }
    };
    AddFlex(panel, segDrop, 0);

    // --- Segment ends dropdown (Segmented ring) ---
    auto endsCap = MakeCaption("round_ends_cap", "Segment ends (Segmented ring)");
    AddFlex(panel, endsCap, 0);
    auto endsDrop = std::make_shared<UltraCanvasDropdown>("round_ends", 0, 0, 0, 26);
    endsDrop->AddItem("Rounded");
    endsDrop->AddItem("Sharp");
    endsDrop->SetSelectedIndex(0);
    endsDrop->onSelectionChanged = [gPtr](int idx, const DropdownItem&) {
        gPtr->SetRingSegmentRounded(idx == 0);
    };
    AddFlex(panel, endsDrop, 0);

    // --- Border dropdown (Segmented ring) ---
    auto borderCap = MakeCaption("round_border_cap", "Segment border (Segmented ring)");
    AddFlex(panel, borderCap, 0);
    auto borderDrop = std::make_shared<UltraCanvasDropdown>("round_border", 0, 0, 0, 26);
    borderDrop->AddItem("Off");
    borderDrop->AddItem("On");
    borderDrop->SetSelectedIndex(0);
    borderDrop->onSelectionChanged = [gPtr](int idx, const DropdownItem&) {
        gPtr->SetRingBorder(idx == 1);
    };
    AddFlex(panel, borderDrop, 0);

    // --- Surface fill dropdown ---
    auto fillCap = MakeCaption("round_fill_cap", "Surface fill");
    AddFlex(panel, fillCap, 0);
    auto fillDrop = std::make_shared<UltraCanvasDropdown>("round_fill", 0, 0, 0, 26);
    fillDrop->AddItem("None (open centre)");
    fillDrop->AddItem("Straight liquid line");
    fillDrop->AddItem("Waved liquid line");
    fillDrop->SetSelectedIndex(0);
    fillDrop->onSelectionChanged = [gPtr](int idx, const DropdownItem&) {
        switch (idx) {
            case 1: gPtr->SetFillStyle(GaugeFillStyle::StraightLevel); break;
            case 2: gPtr->SetFillStyle(GaugeFillStyle::WavedLevel); break;
            default: gPtr->SetFillStyle(GaugeFillStyle::NoFill); break;
        }
    };
    AddFlex(panel, fillDrop, 0);

    // --- Centre content dropdown ---
    auto centerCap = MakeCaption("round_center_cap", "Centre content");
    AddFlex(panel, centerCap, 0);
    auto centerDrop = std::make_shared<UltraCanvasDropdown>("round_center", 0, 0, 0, 26);
    centerDrop->AddItem("Value only");
    centerDrop->AddItem("Text label");
    centerDrop->AddItem("Battery icon");
    centerDrop->AddItem("Bolt icon");
    centerDrop->SetSelectedIndex(0);
    centerDrop->onSelectionChanged = [gPtr](int idx, const DropdownItem&) {
        switch (idx) {
            case 1:
                gPtr->SetRingCenterLabel("Battery");
                gPtr->SetRingCenterContent(GaugeRingCenterContent::TextLabel);
                break;
            case 2:
                gPtr->SetRingCenterIcon(GaugeRingIcon::Battery);
                gPtr->SetRingCenterContent(GaugeRingCenterContent::Icon);
                break;
            case 3:
                gPtr->SetRingCenterIcon(GaugeRingIcon::Bolt);
                gPtr->SetRingCenterContent(GaugeRingCenterContent::Icon);
                break;
            default:
                gPtr->SetRingCenterContent(GaugeRingCenterContent::NoContent);
                break;
        }
    };
    AddFlex(panel, centerDrop, 0);

    // --- Faded colours dropdown ---
    auto fadeCap = MakeCaption("round_fade_cap", "Faded colours");
    AddFlex(panel, fadeCap, 0);
    auto fadeDrop = std::make_shared<UltraCanvasDropdown>("round_fade", 0, 0, 0, 26);
    fadeDrop->AddItem("Off");
    fadeDrop->AddItem("Ring");
    fadeDrop->AddItem("Fill");
    fadeDrop->AddItem("Ring + Fill");
    fadeDrop->SetSelectedIndex(0);
    fadeDrop->onSelectionChanged = [gPtr](int idx, const DropdownItem&) {
        gPtr->SetRingFaded(idx == 1 || idx == 3);
        gPtr->SetFillFaded(idx == 2 || idx == 3);
    };
    AddFlex(panel, fadeDrop, 0);

    AddFlex(body, panel, 0);

    // ---------------------------------------------------------------------
    // RIGHT: showcase grid of style presets (each a value-controllable card)
    // ---------------------------------------------------------------------
    auto grid = std::make_shared<UltraCanvasContainer>("RoundGrid", 0, 0, 0, 0);
    SetGrid3x3(grid, 12);

    // 1) Thick solid round gauge
    auto g1 = CreateGaugeDiagramElement("rp1", 0, 0, kCardW, kCardH);
    ApplyRoundPreset(g1, GaugeRingStyle::SolidArc, GaugeRingSegmentStyle::Blocks,
                     GaugeFillStyle::NoFill, 16.0f, 36, Color(160, 230, 40, 255));
    g1->SetTitle("Solid Arc");
    g1->SetUnit("%");
    g1->SetTrackColor(Color(50, 60, 30, 255));
    AddGrid(grid, CreateGaugeCard("rp1_c", kCardW, kCardH, g1, 0.0f, 100.0f, 74.0f, "%"), 0, 0);

    // 2) Segmented rounded blocks (clearly separated chunks)
    auto g2 = CreateGaugeDiagramElement("rp2", 0, 0, kCardW, kCardH);
    ApplyRoundPreset(g2, GaugeRingStyle::Segmented, GaugeRingSegmentStyle::Blocks,
                     GaugeFillStyle::NoFill, 16.0f, 12, Color(160, 230, 40, 255));
    g2->SetTitle("Segmented");
    g2->SetUnit("%");
    g2->SetTrackColor(Color(50, 60, 30, 255));
    AddGrid(grid, CreateGaugeCard("rp2_c", kCardW, kCardH, g2, 0.0f, 100.0f, 47.0f, "%"), 0, 1);

    // 3) Dashed radial bars (tachymeter)
    auto g3 = CreateGaugeDiagramElement("rp3", 0, 0, kCardW, kCardH);
    ApplyRoundPreset(g3, GaugeRingStyle::Dashed, GaugeRingSegmentStyle::Bars,
                     GaugeFillStyle::NoFill, 14.0f, 60, Color(40, 130, 245, 255));
    g3->SetTitle("Dashed Bars");
    g3->SetUnit("%");
    g3->SetTrackColor(Color(40, 46, 60, 255));
    AddGrid(grid, CreateGaugeCard("rp3_c", kCardW, kCardH, g3, 0.0f, 100.0f, 62.0f, "%"), 0, 2);

    // 4) Dots ring
    auto g4 = CreateGaugeDiagramElement("rp4", 0, 0, kCardW, kCardH);
    ApplyRoundPreset(g4, GaugeRingStyle::Segmented, GaugeRingSegmentStyle::Dots,
                     GaugeFillStyle::NoFill, 12.0f, 40, Color(120, 90, 240, 255));
    g4->SetTitle("Dots");
    g4->SetUnit("%");
    AddGrid(grid, CreateGaugeCard("rp4_c", kCardW, kCardH, g4, 0.0f, 100.0f, 55.0f, "%"), 1, 0);

    // 5) Straight liquid fill with faded fill colour
    auto g5 = CreateGaugeDiagramElement("rp5", 0, 0, kCardW, kCardH);
    ApplyRoundPreset(g5, GaugeRingStyle::SolidArc, GaugeRingSegmentStyle::Blocks,
                     GaugeFillStyle::StraightLevel, 8.0f, 36, Color(0, 150, 230, 255));
    g5->SetFillFaded(true);
    g5->SetTitle("Straight Fill (faded)");
    g5->SetUnit("%");
    AddGrid(grid, CreateGaugeCard("rp5_c", kCardW, kCardH, g5, 0.0f, 100.0f, 60.0f, "%"), 1, 1);

    // 6) Waved liquid fill (battery look) with faded fill colour
    auto g6 = CreateGaugeDiagramElement("rp6", 0, 0, kCardW, kCardH);
    ApplyRoundPreset(g6, GaugeRingStyle::SolidArc, GaugeRingSegmentStyle::Blocks,
                     GaugeFillStyle::WavedLevel, 8.0f, 36, Color(255, 90, 30, 255));
    g6->SetFillFaded(true);
    g6->SetTitle("Waved Fill (faded)");
    g6->SetUnit("%");
    AddGrid(grid, CreateGaugeCard("rp6_c", kCardW, kCardH, g6, 0.0f, 100.0f, 18.0f, "%"), 1, 2);

    // 7) Segmented ring, rounded ends + battery icon centre (reference image 1)
    auto g7 = CreateGaugeDiagramElement("rp7", 0, 0, kCardW, kCardH);
    ApplyRoundPreset(g7, GaugeRingStyle::SegmentedRing, GaugeRingSegmentStyle::Blocks,
                     GaugeFillStyle::NoFill, 18.0f, 8, Color(0, 220, 0, 255));
    g7->SetRingSegmentRounded(true);
    g7->SetRingCenterIcon(GaugeRingIcon::Battery);
    g7->SetRingCenterContent(GaugeRingCenterContent::Icon);
    g7->SetTitle("Segmented Ring + Icon");
    AddGrid(grid, CreateGaugeCard("rp7_c", kCardW, kCardH, g7, 0.0f, 100.0f, 75.0f, ""), 2, 0);

    // 8) Segmented ring, sharp ends + text label centre (reference image 2)
    auto g8 = CreateGaugeDiagramElement("rp8", 0, 0, kCardW, kCardH);
    ApplyRoundPreset(g8, GaugeRingStyle::SegmentedRing, GaugeRingSegmentStyle::Blocks,
                     GaugeFillStyle::NoFill, 18.0f, 8, Color(0, 220, 0, 255));
    g8->SetRingSegmentRounded(false);
    g8->SetRingCenterLabel("Battery");
    g8->SetRingCenterContent(GaugeRingCenterContent::TextLabel);
    g8->SetTitle("Segmented Ring + Label");
    AddGrid(grid, CreateGaugeCard("rp8_c", kCardW, kCardH, g8, 0.0f, 100.0f, 75.0f, ""), 2, 1);

    // 9) Faded ring colour (soft two-tone gradient sweep)
    auto g9 = CreateGaugeDiagramElement("rp9", 0, 0, kCardW, kCardH);
    ApplyRoundPreset(g9, GaugeRingStyle::SolidArc, GaugeRingSegmentStyle::Blocks,
                     GaugeFillStyle::NoFill, 14.0f, 36, Color(120, 200, 40, 255));
    g9->SetRingFaded(true);
    g9->SetTrackColor(Color(60, 70, 35, 255));
    g9->SetTitle("Faded Ring");
    g9->SetUnit("%");
    AddGrid(grid, CreateGaugeCard("rp9_c", kCardW, kCardH, g9, 0.0f, 100.0f, 47.0f, "%"), 2, 2);

    AddFlex(body, grid, 1);
    AddFlex(tab, body, 1);
    return tab;
}

// =============================================================================
// MAIN ENTRY POINT
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateGaugeExamples() {
    // V2.1: Larger overall size to fit 3 columns of 320-wide cards comfortably
    auto tabs = std::make_shared<UltraCanvasTabbedContainer>(
        "GaugeTabs", 0, 0, 920, 860);
    tabs->SetTabStyle(TabStyle::Rounded);
    tabs->SetTabHeight(32);

    tabs->AddTab("Round Gauges", BuildRoundGaugesTab(920, 810));
    tabs->AddTab("Progress & Linear", BuildProgressTab(920, 810));
    tabs->AddTab("Specialized", BuildSpecializedTab(920, 810));
    tabs->AddTab("Analog", BuildAnalogTab(920, 810));
    tabs->SetActiveTab(0);

    return tabs;
}

} // namespace UltraCanvas