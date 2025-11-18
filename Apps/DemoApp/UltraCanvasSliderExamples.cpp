// Apps/DemoApp/UltraCanvasRangeSliderDemo.cpp
// Comprehensive demonstration of dual-handle range slider functionality
// Version: 1.0.0
// Last Modified: 2025-11-16
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include <sstream>
#include <iomanip>

namespace UltraCanvas {
//    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateSliderExamples() {
//        auto container = std::make_shared<UltraCanvasContainer>("SliderExamples", 400, 0, 0, 1000, 600);
//
//        // Title
//        auto title = std::make_shared<UltraCanvasLabel>("SliderTitle", 401, 10, 10, 300, 30);
//        title->SetText("Slider Examples");
//        title->SetFontSize(16);
//        title->SetFontWeight(FontWeight::Bold);
//        container->AddChild(title);
//
//        // Horizontal Slider
//        auto hSlider = std::make_shared<UltraCanvasSlider>("HorizontalSlider", 402, 20, 60, 300, 20);
//        hSlider->SetOrientation(SliderOrientation::Horizontal);
//        hSlider->SetRange(0.0f, 100.0f);
//        hSlider->SetValue(50.0f);
//        hSlider->SetStep(10.0f);
////        hSlider->SetShowTicks(true);
//        container->AddChild(hSlider);
//
//        auto hSliderLabel = std::make_shared<UltraCanvasLabel>("HSliderLabel", 403, 20, 85, 200, 20);
//        hSliderLabel->SetText("Horizontal Slider (0-100)");
//        hSliderLabel->SetFontSize(12);
//        container->AddChild(hSliderLabel);
//
//        // Value display for horizontal slider
//        auto hValueLabel = std::make_shared<UltraCanvasLabel>("HValueLabel", 404, 340, 60, 80, 20);
//        hValueLabel->SetText("50");
//        hValueLabel->SetAlignment(TextAlignment::Center);
//        hValueLabel->SetBackgroundColor(Color(240, 240, 240, 255));
//        container->AddChild(hValueLabel);
//
//        hSlider->onValueChanged = [hValueLabel](float value) {
//            hValueLabel->SetText(std::to_string(static_cast<int>(value)));
//        };
//
//        // Vertical Slider
//        auto vSlider = std::make_shared<UltraCanvasSlider>("VerticalSlider", 405, 500, 60, 20, 200);
//        vSlider->SetOrientation(SliderOrientation::Vertical);
//        vSlider->SetRange(0.0f, 10.0f);
//        vSlider->SetValue(5.0f);
//        vSlider->SetStep(0.5f);
//        container->AddChild(vSlider);
//
//        auto vSliderLabel = std::make_shared<UltraCanvasLabel>("VSliderLabel", 406, 530, 60, 150, 20);
//        vSliderLabel->SetText("Vertical Slider");
//        vSliderLabel->SetFontSize(12);
//        container->AddChild(vSliderLabel);
//
//
//        return container;
//    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateSliderExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("SliderExamples", 400, 0, 0, 1000, 1400);

        auto title = std::make_shared<UltraCanvasLabel>("SliderTitle", 401, 20, 10, 300, 30);
        title->SetText("Simple Slider Examples");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // Horizontal Slider
        auto hSlider = std::make_shared<UltraCanvasSlider>("HorizontalSlider", 402, 20, 60, 300, 20);
        hSlider->SetOrientation(SliderOrientation::Horizontal);
        hSlider->SetRange(0.0f, 100.0f);
        hSlider->SetValue(50.0f);
        hSlider->SetStep(10.0f);
//        hSlider->SetShowTicks(true);
        container->AddChild(hSlider);

        auto hSliderLabel = std::make_shared<UltraCanvasLabel>("HSliderLabel", 403, 20, 85, 200, 20);
        hSliderLabel->SetText("Horizontal Slider (0-100)");
        hSliderLabel->SetFontSize(12);
        container->AddChild(hSliderLabel);

        // Value display for horizontal slider
        auto hValueLabel = std::make_shared<UltraCanvasLabel>("HValueLabel", 404, 340, 60, 80, 20);
        hValueLabel->SetText("50");
        hValueLabel->SetAlignment(TextAlignment::Center);
        hValueLabel->SetBackgroundColor(Color(240, 240, 240, 255));
        container->AddChild(hValueLabel);

        hSlider->onValueChanged = [hValueLabel](float value) {
            hValueLabel->SetText(std::to_string(static_cast<int>(value)));
        };

        // Vertical Slider
        auto vSlider = std::make_shared<UltraCanvasSlider>("VerticalSlider", 405, 500, 50, 20, 200);
        vSlider->SetOrientation(SliderOrientation::Vertical);
        vSlider->SetRange(0.0f, 10.0f);
        vSlider->SetValue(5.0f);
        vSlider->SetStep(0.5f);
        container->AddChild(vSlider);

        auto vSliderLabel = std::make_shared<UltraCanvasLabel>("VSliderLabel", 406, 530, 50, 150, 20);
        vSliderLabel->SetText("Vertical Slider");
        vSliderLabel->SetFontSize(12);
        container->AddChild(vSliderLabel);

        auto rangeSlidersContainer = std::make_shared<UltraCanvasContainer>("RangeSliderDemos", 5000, 0, 260, 1000, 1050);
        //rangeSlidersContainer->SetBackgroundColor(Color(245, 245, 245));

        int yPos = 20;
        int sliderWidth = 400;
        int labelX = 450;

        // ===== TITLE =====
        auto rangeTitle = std::make_shared<UltraCanvasLabel>("Title", 5001, 20, yPos, 600, 40);
        rangeTitle->SetText("Range Slider Demonstrations");
        rangeTitle->SetFontSize(18);
        rangeTitle->SetFontWeight(FontWeight::Bold);
        rangeSlidersContainer->AddChild(rangeTitle);
        yPos += 60;

        // ===== EXAMPLE 1: PRICE RANGE SLIDER =====
        auto priceLabel = std::make_shared<UltraCanvasLabel>("PriceLabel", 5002, 20, yPos, 300, 25);
        priceLabel->SetText("Price Range Selector ($0 - $1000)");
        priceLabel->SetFontWeight(FontWeight::Bold);
        rangeSlidersContainer->AddChild(priceLabel);
        yPos += 35;

        auto priceRange = CreateRangeSlider("priceRange", 5003, 20, yPos, sliderWidth, 30,
                                            0.0f, 1000.0f, 200.0f, 800.0f);
        priceRange->SetValueDisplay(SliderValueDisplay::AlwaysVisible);
        priceRange->SetValueFormat("$%.0f");
        priceRange->SetHandleCollisionMargin(50.0f);

        // Price range display labels
        auto priceLowerLabel = std::make_shared<UltraCanvasLabel>("PriceLower", 5004, labelX, yPos, 150, 25);
        priceLowerLabel->SetText("Min: $200");
        priceLowerLabel->SetBackgroundColor(Color(220, 240, 255));
        priceLowerLabel->SetPadding(3);
        rangeSlidersContainer->AddChild(priceLowerLabel);

        auto priceUpperLabel = std::make_shared<UltraCanvasLabel>("PriceUpper", 5005, labelX + 160, yPos, 150, 25);
        priceUpperLabel->SetText("Max: $800");
        priceUpperLabel->SetBackgroundColor(Color(220, 240, 255));
        priceUpperLabel->SetPadding(3);
        rangeSlidersContainer->AddChild(priceUpperLabel);

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

        rangeSlidersContainer->AddChild(priceRange);
        yPos += 60;

        // ===== EXAMPLE 2: AGE RANGE SELECTOR =====
        auto ageLabel = std::make_shared<UltraCanvasLabel>("AgeLabel", 5006, 20, yPos, 300, 25);
        ageLabel->SetText("Age Range Filter (18 - 100 years)");
        ageLabel->SetFontWeight(FontWeight::Bold);
        rangeSlidersContainer->AddChild(ageLabel);
        yPos += 35;

        auto ageRange = CreateRangeSlider("ageRange", 5007, 20, yPos, sliderWidth, 30,
                                          18.0f, 100.0f, 25.0f, 65.0f);
        ageRange->SetValueDisplay(SliderValueDisplay::Number);
        ageRange->SetValueFormat("%.0f");
        ageRange->SetStep(1.0f);
        ageRange->SetHandleCollisionMargin(1.0f);

        auto ageDisplay = std::make_shared<UltraCanvasLabel>("AgeDisplay", 5008, labelX, yPos, 300, 25);
        ageDisplay->SetText("Age Range: 25 - 65 years");
        ageDisplay->SetBackgroundColor(Color(255, 240, 220));
        ageDisplay->SetPadding(3);
        rangeSlidersContainer->AddChild(ageDisplay);

        ageRange->onRangeChanged = [ageDisplay](float lower, float upper) {
            std::ostringstream oss;
            oss << "Age Range: " << static_cast<int>(lower) << " - " << static_cast<int>(upper) << " years";
            ageDisplay->SetText(oss.str());
        };

        rangeSlidersContainer->AddChild(ageRange);
        yPos += 60;

        // ===== EXAMPLE 3: PERCENTAGE RANGE (0-100%) =====
        auto percentLabel = std::make_shared<UltraCanvasLabel>("PercentLabel", 5009, 20, yPos, 300, 25);
        percentLabel->SetText("Percentage Range (0% - 100%)");
        percentLabel->SetFontWeight(FontWeight::Bold);
        rangeSlidersContainer->AddChild(percentLabel);
        yPos += 35;

        auto percentRange = CreateRangeSlider("percentRange", 5010, 20, yPos, sliderWidth, 30,
                                              0.0f, 100.0f, 30.0f, 70.0f);
        percentRange->SetValueDisplay(SliderValueDisplay::Percentage);
        percentRange->SetStep(5.0f);
        percentRange->SetHandleShape(SliderHandleShape::Square);

        auto percentDisplay = std::make_shared<UltraCanvasLabel>("PercentDisplay", 5011, labelX, yPos, 300, 25);
        percentDisplay->SetText("Range: 30% - 70%");
        percentDisplay->SetBackgroundColor(Color(240, 255, 220));
        percentDisplay->SetPadding(3);
        rangeSlidersContainer->AddChild(percentDisplay);

        percentRange->onRangeChanged = [percentDisplay](float lower, float upper) {
            std::ostringstream oss;
            oss << "Range: " << static_cast<int>(lower) << "% - " << static_cast<int>(upper) << "%";
            percentDisplay->SetText(oss.str());
        };

        rangeSlidersContainer->AddChild(percentRange);
        yPos += 60;

        // ===== EXAMPLE 4: TEMPERATURE RANGE (-50°C to +50°C) =====
        auto tempLabel = std::make_shared<UltraCanvasLabel>("TempLabel", 5012, 20, yPos, 300, 25);
        tempLabel->SetText("Temperature Range (-50°C to +50°C)");
        tempLabel->SetFontWeight(FontWeight::Bold);
        rangeSlidersContainer->AddChild(tempLabel);
        yPos += 35;

        auto tempRange = CreateRangeSlider("tempRange", 5013, 20, yPos, sliderWidth, 30,
                                           -50.0f, 50.0f, 10.0f, 30.0f);
        tempRange->SetValueDisplay(SliderValueDisplay::AlwaysVisible);
        tempRange->SetValueFormat("%.1f°C");
        tempRange->SetStep(0.5f);

        // Custom colors for temperature range
        auto& tempStyle = tempRange->GetStyle();
        tempStyle.rangeTrackColor = Color(255, 150, 100, 180);  // Warm orange
        tempStyle.activeTrackColor = Color(255, 100, 50);

        auto tempDisplay = std::make_shared<UltraCanvasLabel>("TempDisplay", 5014, labelX, yPos, 300, 25);
        tempDisplay->SetText("Temp: 10.0°C - 30.0°C");
        tempDisplay->SetBackgroundColor(Color(255, 220, 220));
        tempDisplay->SetPadding(3);
        rangeSlidersContainer->AddChild(tempDisplay);

        tempRange->onRangeChanged = [tempDisplay](float lower, float upper) {
            std::ostringstream oss;
            oss << "Temp: " << std::fixed << std::setprecision(1) << lower << "°C - " << upper << "°C";
            tempDisplay->SetText(oss.str());
        };

        rangeSlidersContainer->AddChild(tempRange);
        yPos += 60;

        // ===== EXAMPLE 5: TIME RANGE (24-hour format) =====
        auto timeLabel = std::make_shared<UltraCanvasLabel>("TimeLabel", 5015, 20, yPos, 300, 25);
        timeLabel->SetText("Time Range Selector (0:00 - 24:00)");
        timeLabel->SetFontWeight(FontWeight::Bold);
        rangeSlidersContainer->AddChild(timeLabel);
        yPos += 35;

        auto timeRange = CreateRangeSlider("timeRange", 5016, 20, yPos, sliderWidth, 30,
                                           0.0f, 24.0f, 9.0f, 17.0f);
        timeRange->SetValueDisplay(SliderValueDisplay::NoDisplay);
        timeRange->SetStep(0.5f);  // 30-minute increments
        timeRange->SetHandleShape(SliderHandleShape::Triangle);

        auto timeDisplay = std::make_shared<UltraCanvasLabel>("TimeDisplay", 5017, labelX, yPos, 300, 25);
        timeDisplay->SetText("Work Hours: 09:00 - 17:00");
        timeDisplay->SetBackgroundColor(Color(220, 220, 255));
        timeDisplay->SetPadding(3);
        rangeSlidersContainer->AddChild(timeDisplay);

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

        rangeSlidersContainer->AddChild(timeRange);
        yPos += 60;

        // ===== EXAMPLE 6: VERTICAL VOLUME RANGE =====
        auto volumeLabel = std::make_shared<UltraCanvasLabel>("VolumeLabel", 5018, 20, yPos, 300, 25);
        volumeLabel->SetText("Vertical Volume Range (0-100)");
        volumeLabel->SetFontWeight(FontWeight::Bold);
        rangeSlidersContainer->AddChild(volumeLabel);
        yPos += 20;

        auto volumeRange = CreateRangeSlider("volumeRange", 5019, 50, yPos, 40, 200,
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

        auto volumeDisplay = std::make_shared<UltraCanvasLabel>("VolumeDisplay", 5020, 100, yPos + 80, 250, 25);
        volumeDisplay->SetText("Volume Range: 30 - 80");
        volumeDisplay->SetBackgroundColor(Color(220, 255, 220));
        volumeDisplay->SetPadding(3);
        rangeSlidersContainer->AddChild(volumeDisplay);

        volumeRange->onRangeChanged = [volumeDisplay](float lower, float upper) {
            std::ostringstream oss;
            oss << "Volume Range: " << static_cast<int>(lower) << " - " << static_cast<int>(upper);
            volumeDisplay->SetText(oss.str());
        };

        rangeSlidersContainer->AddChild(volumeRange);
        yPos += 220;

        // ===== EXAMPLE 7: DATE RANGE (Days 1-31) =====
        auto dateLabel = std::make_shared<UltraCanvasLabel>("DateLabel", 5021, 20, yPos, 300, 25);
        dateLabel->SetText("Date Range Selector (Days 1-31)");
        dateLabel->SetFontWeight(FontWeight::Bold);
        rangeSlidersContainer->AddChild(dateLabel);
        yPos += 35;

        auto dateRange = CreateRangeSlider("dateRange", 5022, 20, yPos, sliderWidth, 30,
                                           1.0f, 31.0f, 5.0f, 25.0f);
        dateRange->SetValueDisplay(SliderValueDisplay::AlwaysVisible);
        dateRange->SetValueFormat("Day %.0f");
        dateRange->SetStep(1.0f);

        auto dateDisplay = std::make_shared<UltraCanvasLabel>("DateDisplay", 5023, labelX, yPos, 300, 25);
        dateDisplay->SetText("Selected: Day 5 - Day 25");
        dateDisplay->SetBackgroundColor(Color(255, 240, 255));
        dateDisplay->SetPadding(3);
        rangeSlidersContainer->AddChild(dateDisplay);

        dateRange->onRangeChanged = [dateDisplay](float lower, float upper) {
            std::ostringstream oss;
            oss << "Selected: Day " << static_cast<int>(lower) << " - Day " << static_cast<int>(upper);
            dateDisplay->SetText(oss.str());
        };

        rangeSlidersContainer->AddChild(dateRange);
        yPos += 60;

        // ===== EXAMPLE 8: ROUNDED STYLE RANGE SLIDER =====
        auto roundedLabel = std::make_shared<UltraCanvasLabel>("RoundedLabel", 5024, 20, yPos, 300, 25);
        roundedLabel->SetText("Diamond Style handle");
        roundedLabel->SetFontWeight(FontWeight::Bold);
        rangeSlidersContainer->AddChild(roundedLabel);
        yPos += 35;

        auto roundedRange = CreateRangeSlider("roundedRange", 5025, 20, yPos, sliderWidth, 30,
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

        auto roundedDisplay = std::make_shared<UltraCanvasLabel>("RoundedDisplay", 5026, labelX, yPos, 300, 25);
        roundedDisplay->SetText("Range: 25 - 75");
        roundedDisplay->SetBackgroundColor(Color(240, 240, 255));
        roundedDisplay->SetPadding(3);
        rangeSlidersContainer->AddChild(roundedDisplay);

        roundedRange->onRangeChanged = [roundedDisplay](float lower, float upper) {
            std::ostringstream oss;
            oss << "Range: " << static_cast<int>(lower) << " - " << static_cast<int>(upper);
            roundedDisplay->SetText(oss.str());
        };

        rangeSlidersContainer->AddChild(roundedRange);
        yPos += 60;

        // ===== INSTRUCTIONS LABEL =====
        auto instructionsLabel = std::make_shared<UltraCanvasLabel>("Instructions", 5027, 20, yPos, 700, 60);
        instructionsLabel->SetText(
                "Instructions:\n"
                "• Drag handles to adjust range\n"
                "• Click Tab to switch between handles (when focused)\n"
                "• Use arrow keys to adjust active handle\n"
                "• Handles cannot overlap (enforced by collision margin)"
        );
        instructionsLabel->SetFontSize(11);
        instructionsLabel->SetBackgroundColor(Color(255, 255, 240));
        rangeSlidersContainer->AddChild(instructionsLabel);

        container->AddChild(rangeSlidersContainer);
        return container;
    }
} // namespace UltraCanvas
