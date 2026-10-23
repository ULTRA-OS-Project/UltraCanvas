// Apps/DemoApp/UltraCanvasSliderExamples.cpp
// Comprehensive demonstration of dual-handle range slider functionality
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): one card per
//                  slider, each beside its live readout, every text sized to fit
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include <sstream>
#include <iomanip>

namespace UltraCanvas {
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateSliderExamples() {
        auto container = DemoKit::CreatePage("SliderExamples", "Simple Slider Examples", "");

        // A live readout on a coloured background, as wide as its text.
        auto addReadout = [](const std::shared_ptr<UltraCanvasContainer>& row, const std::string& id,
                             const std::string& text, const Color& background) {
            auto label = DemoKit::CreateText(id, text);
            label->SetBackgroundColor(background);
            label->SetPadding(3);
            label->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
            DemoKit::Add(row, label);
            return label;
        };

        auto basicCards = DemoKit::AddRow(container, "SliderBasicCards");

        // Horizontal Slider
        auto hCard = DemoKit::AddCard(basicCards, "HSliderLabel", "Horizontal Slider (0-100)");
        auto hRow = DemoKit::AddRow(hCard, "HSliderRow", 20);
        auto hSlider = CreateSlider("HorizontalSlider", 0, 0, 300, 20);
        hSlider->SetOrientation(SliderOrientation::Horizontal);
        hSlider->SetRange(0.0f, 100.0f);
        hSlider->SetValue(50.0f);
        hSlider->SetStep(10.0f);
//        hSlider->SetShowTicks(true);
        DemoKit::AddFixed(hRow, hSlider);

        // Value display for horizontal slider: at least its old 80 px, so the
        // slider does not jump while the number changes length.
        auto hValueLabel = addReadout(hRow, "HValueLabel", "50", Color(240, 240, 240, 255));
        hValueLabel->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
        CSSLayout::BoxConstraints valueLimits;
        valueLimits.minWidth = CSSLayout::Dimension::Px(80);
        hValueLabel->boxConstraints = valueLimits;

        hSlider->onValueChanged = [hValueLabel](float value) {
            hValueLabel->SetText(std::to_string(static_cast<int>(value)));
        };

        // Vertical Slider
        auto vCard = DemoKit::AddCard(basicCards, "VSliderLabel", "Vertical Slider");
        auto vSlider = CreateSlider("VerticalSlider", 0, 0, 20, 200);
        vSlider->SetOrientation(SliderOrientation::Vertical);
        vSlider->SetRange(0.0f, 10.0f);
        vSlider->SetValue(5.0f);
        vSlider->SetStep(0.5f);
        DemoKit::AddFixed(vCard, vSlider);

        // Vertical dual-handle (range) slider, beside the simple vertical slider.
        auto volumeCard = DemoKit::AddCard(basicCards, "VolumeLabel", "Vertical Volume Range (0-100)");
        auto volumeRow = DemoKit::AddRow(volumeCard, "VolumeRow", 12);
        volumeRow->layout.SetFlexAlignItems(CSSLayout::AlignItems::Start);
        auto volumeRange = CreateRangeSlider("volumeRange", 0, 0, 40, 200,
                                             0.0f, 100.0f, 30.0f, 80.0f);
        volumeRange->SetOrientation(SliderOrientation::Vertical);
        volumeRange->SetRangeMode(true);
        volumeRange->SetValueDisplay(SliderValueDisplay::Tooltip);
        volumeRange->SetStep(5.0f);

        // Custom colors for volume
        auto& volumeStyle = volumeRange->GetStyle();
        volumeStyle.rangeTrackColor = Color(100, 200, 100, 180);  // Green
        volumeStyle.trackHeight = 8.0f;
        volumeStyle.handleSize = 20.0f;

        DemoKit::AddFixed(volumeRow, volumeRange);
        auto volumeDisplay = addReadout(volumeRow, "VolumeDisplay", "Volume Range: 30 - 80",
                                        Color(220, 255, 220));

        volumeRange->onRangeChanged = [volumeDisplay](float lower, float upper) {
            std::ostringstream oss;
            oss << "Volume Range: " << static_cast<int>(lower) << " - " << static_cast<int>(upper);
            volumeDisplay->SetText(oss.str());
        };

        // ===== HORIZONTAL COLOUR PALETTE SLIDER =====
        // A hue palette painted inside the bar (colour-picker style) with a small
        // handle that fits within the bar. The field on the right previews the
        // selected colour and shows its RGB value.
        auto paletteCard = DemoKit::AddCard(basicCards, "PaletteLabel", "Colour Palette Slider (Hue)");
        auto paletteRow = DemoKit::AddRow(paletteCard, "PaletteRow", 20);
        auto paletteSlider = CreateSlider("PaletteSlider", 0, 0, 300, 22);
        paletteSlider->SetOrientation(SliderOrientation::Horizontal);
        paletteSlider->SetRange(0.0f, 360.0f);
        paletteSlider->SetValue(0.0f);
        paletteSlider->SetStep(1.0f);
        // Bar just thick enough to wrap the handle (handle size plus the bar
        // border on each side); its rounded end caps enclose the handle at the
        // extremes so the handle appears to travel inside the bar.
        paletteSlider->SetTrackHeight(20.0f);
        paletteSlider->SetHandleSize(18.0f);
        // Full hue palette inside the bar.
        std::vector<GradientStop> hueStops;
        for (int i = 0; i <= 6; ++i) {
            hueStops.emplace_back(i / 6.0, HSV(i * 60.0f, 1.0f, 1.0f));
        }
        paletteSlider->SetTrackGradient(hueStops);
        DemoKit::AddFixed(paletteRow, paletteSlider);

        // Selected-colour preview field with RGB readout beside the bar, at
        // least its old 140 px and as tall as the row.
        auto paletteSwatch = DemoKit::CreateText("PaletteSwatch", "");
        paletteSwatch->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
        paletteSwatch->SetPadding(3);
        CSSLayout::BoxConstraints swatchLimits;
        swatchLimits.minWidth = CSSLayout::Dimension::Px(140);
        paletteSwatch->boxConstraints = swatchLimits;
        DemoKit::Add(paletteRow, paletteSwatch);

        auto updateSwatch = [paletteSwatch](float hue) {
            Color c = HSV(hue, 1.0f, 1.0f);
            paletteSwatch->SetBackgroundColor(c);
            // Choose readable text colour based on perceived luminance.
            float luma = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
            paletteSwatch->SetTextColor(luma > 140.0f ? Colors::Black : Colors::White);
            std::ostringstream oss;
            oss << "RGB " << static_cast<int>(c.r) << ", "
                << static_cast<int>(c.g) << ", " << static_cast<int>(c.b);
            paletteSwatch->SetText(oss.str());
        };
        updateSwatch(0.0f);
        paletteSlider->onValueChanged = [updateSwatch](float value) {
            updateSwatch(value);
        };

        auto rangeSlidersContainer = DemoKit::CreateColumn("RangeSliderDemos", DemoKit::kPageGap);
        DemoKit::Add(container, rangeSlidersContainer);

        const int sliderWidth = 400;
        // A card is at least as wide as its slider; two share a line when they fit.
        const float rangeCardWidth = sliderWidth + 2 * DemoKit::kCardPadding + 2;

        // ===== TITLE =====
        auto rangeTitle = DemoKit::AddHeading(rangeSlidersContainer, "Title", "Range Slider Demonstrations");
        rangeTitle->SetFontSize(18);

        auto rangeCards = DemoKit::AddRow(rangeSlidersContainer, "RangeSliderCards");
        // One card per range slider: its title, then the slider beside its readouts.
        auto addRangeCard = [&rangeCards, rangeCardWidth](const std::string& id, const std::string& title) {
            auto card = DemoKit::AddCard(rangeCards, id, title, "", rangeCardWidth);
            auto row = DemoKit::AddRow(card, id + "Row", 10);
            row->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
            return row;
        };

        // ===== EXAMPLE 1: PRICE RANGE SLIDER =====
        auto priceRow = addRangeCard("PriceLabel", "Price Range Selector ($0 - $1000)");

        auto priceRange = CreateRangeSlider("priceRange", 0, 0, sliderWidth, 40,
                                            0.0f, 1000.0f, 200.0f, 800.0f);
        priceRange->SetValueDisplay(SliderValueDisplay::AlwaysVisible);
        priceRange->SetValueFormat("$%.0f");
        priceRange->SetHandleCollisionMargin(50.0f);

        DemoKit::AddFixed(priceRow, priceRange);
        // Price range display labels
        auto priceLowerLabel = addReadout(priceRow, "PriceLower", "Min: $200", Color(220, 240, 255));

        auto priceUpperLabel = addReadout(priceRow, "PriceUpper", "Max: $800", Color(220, 240, 255));

        priceRange->onLowerValueChanged = [priceLowerLabel](float value) {
            std::ostringstream oss;
            oss << "Min: $" << static_cast<int>(value);
            priceLowerLabel->SetText(oss.str());
        };

        priceRange->onUpperValueChanged = [priceUpperLabel](float value) {
            std::ostringstream oss;
            oss << "Max: $" << static_cast<int>(value);
            priceUpperLabel->SetText(oss.str());
        };

        // ===== EXAMPLE 2: AGE RANGE SELECTOR =====
        auto ageRow = addRangeCard("AgeLabel", "Age Range Filter (18 - 100 years)");

        auto ageRange = CreateRangeSlider("ageRange", 0, 0, sliderWidth, 40,
                                          18.0f, 100.0f, 25.0f, 65.0f);
        ageRange->SetValueDisplay(SliderValueDisplay::Number);
        ageRange->SetValueFormat("%.0f");
        ageRange->SetStep(1.0f);
        ageRange->SetHandleCollisionMargin(1.0f);

        DemoKit::AddFixed(ageRow, ageRange);
        auto ageDisplay = addReadout(ageRow, "AgeDisplay", "Age Range: 25 - 65 years", Color(255, 240, 220));

        ageRange->onRangeChanged = [ageDisplay](float lower, float upper) {
            std::ostringstream oss;
            oss << "Age Range: " << static_cast<int>(lower) << " - " << static_cast<int>(upper) << " years";
            ageDisplay->SetText(oss.str());
        };

        // ===== EXAMPLE 3: PERCENTAGE RANGE (0-100%) =====
        auto percentRow = addRangeCard("PercentLabel", "Percentage Range (0% - 100%)");

        auto percentRange = CreateRangeSlider("percentRange", 0, 0, sliderWidth, 40,
                                              0.0f, 100.0f, 30.0f, 70.0f);
        percentRange->SetValueDisplay(SliderValueDisplay::Percentage);
        percentRange->SetStep(5.0f);
        percentRange->SetHandleShape(SliderHandleShape::Square);

        DemoKit::AddFixed(percentRow, percentRange);
        auto percentDisplay = addReadout(percentRow, "PercentDisplay", "Range: 30% - 70%", Color(240, 255, 220));

        percentRange->onRangeChanged = [percentDisplay](float lower, float upper) {
            std::ostringstream oss;
            oss << "Range: " << static_cast<int>(lower) << "% - " << static_cast<int>(upper) << "%";
            percentDisplay->SetText(oss.str());
        };

        // ===== EXAMPLE 4: TEMPERATURE RANGE (-50°C to +50°C) =====
        auto tempRow = addRangeCard("TempLabel", "Temperature Range (-50°C to +50°C)");

        auto tempRange = CreateRangeSlider("tempRange", 0, 0, sliderWidth, 40,
                                           -50.0f, 50.0f, 10.0f, 30.0f);
        tempRange->SetValueDisplay(SliderValueDisplay::AlwaysVisible);
        tempRange->SetValueFormat("%.1f°C");
        tempRange->SetStep(0.5f);

        // Custom colors for temperature range
        auto& tempStyle = tempRange->GetStyle();
        tempStyle.rangeTrackColor = Color(255, 150, 100, 180);  // Warm orange
        tempStyle.activeTrackColor = Color(255, 100, 50);

        DemoKit::AddFixed(tempRow, tempRange);
        auto tempDisplay = addReadout(tempRow, "TempDisplay", "Temp: 10.0°C - 30.0°C", Color(255, 220, 220));

        tempRange->onRangeChanged = [tempDisplay](float lower, float upper) {
            std::ostringstream oss;
            oss << "Temp: " << std::fixed << std::setprecision(1) << lower << "°C - " << upper << "°C";
            tempDisplay->SetText(oss.str());
        };

        // ===== EXAMPLE 5: TIME RANGE (24-hour format) =====
        auto timeRow = addRangeCard("TimeLabel", "Time Range Selector (0:00 - 24:00)");

        auto timeRange = CreateRangeSlider("timeRange", 0, 0, sliderWidth, 20,
                                           0.0f, 24.0f, 9.0f, 17.0f);
        timeRange->SetValueDisplay(SliderValueDisplay::NoDisplay);
        timeRange->SetStep(0.5f);  // 30-minute increments
        timeRange->SetHandleShape(SliderHandleShape::Triangle);

        DemoKit::AddFixed(timeRow, timeRange);
        auto timeDisplay = addReadout(timeRow, "TimeDisplay", "Work Hours: 09:00 - 17:00", Color(220, 220, 255));

        timeRange->onRangeChanged = [timeDisplay](float lower, float upper) {
            auto formatTime = [](float hours) -> std::string {
                int h = static_cast<int>(hours);
                int m = static_cast<int>((hours - h) * 60);
                std::ostringstream oss;
                oss << std::setfill('0') << std::setw(2) << h << ":" << std::setw(2) << m;
                return oss.str();
            };

            std::ostringstream oss;
            oss << "Work Hours: " << formatTime(lower) << " - " << formatTime(upper);
            timeDisplay->SetText(oss.str());
        };

        // ===== EXAMPLE 6: DATE RANGE (Days 1-31) =====
        auto dateRow = addRangeCard("DateLabel", "Date Range Selector (Days 1-31)");

        auto dateRange = CreateRangeSlider("dateRange", 0, 0, sliderWidth, 40,
                                           1.0f, 31.0f, 5.0f, 25.0f);
        dateRange->SetValueDisplay(SliderValueDisplay::AlwaysVisible);
        dateRange->SetValueFormat("Day %.0f");
        dateRange->SetStep(1.0f);

        DemoKit::AddFixed(dateRow, dateRange);
        auto dateDisplay = addReadout(dateRow, "DateDisplay", "Selected: Day 5 - Day 25", Color(255, 240, 255));

        dateRange->onRangeChanged = [dateDisplay](float lower, float upper) {
            std::ostringstream oss;
            oss << "Selected: Day " << static_cast<int>(lower) << " - Day " << static_cast<int>(upper);
            dateDisplay->SetText(oss.str());
        };

        // ===== EXAMPLE 7: ROUNDED STYLE RANGE SLIDER =====
        auto roundedRow = addRangeCard("RoundedLabel", "Diamond Style handle");

        auto roundedRange = CreateRangeSlider("roundedRange", 0, 0, sliderWidth, 40,
                                              0.0f, 100.0f, 25.0f, 75.0f);
        roundedRange->SetValueDisplay(SliderValueDisplay::Number);
        roundedRange->SetStep(1.0f);

        // Custom modern styling
        auto& roundedStyle = roundedRange->GetStyle();
        roundedStyle.trackHeight = 10.0f;
        roundedStyle.handleSize = 24.0f;
        roundedStyle.cornerRadius = 12.0f;
        roundedStyle.rangeTrackColor = Color(120, 120, 255, 200);
        roundedStyle.trackColor = Color(230, 230, 230);
        roundedStyle.handleColor = Color(255, 255, 255);
        roundedStyle.handleBorderColor = Color(120, 120, 255);
        roundedStyle.handleShape = SliderHandleShape::Diamond;
        roundedRange->SetStyle(roundedStyle);

        DemoKit::AddFixed(roundedRow, roundedRange);
        auto roundedDisplay = addReadout(roundedRow, "RoundedDisplay", "Range: 25 - 75", Color(240, 240, 255));

        roundedRange->onRangeChanged = [roundedDisplay](float lower, float upper) {
            std::ostringstream oss;
            oss << "Range: " << static_cast<int>(lower) << " - " << static_cast<int>(upper);
            roundedDisplay->SetText(oss.str());
        };

        // ===== INSTRUCTIONS =====
        DemoKit::AddNotes(container, "Instructions", "Instructions", {
                "Drag handles to adjust range",
                "Click Tab to switch between handles (when focused)",
                "Use arrow keys to adjust active handle",
                "Handles cannot overlap (enforced by collision margin)"});

        return container;
    }
} // namespace UltraCanvas
