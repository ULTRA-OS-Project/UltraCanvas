// Apps/DemoApp/UltraCanvasDemoExamples.cpp
// Implementation of all component example creators
// Version: 1.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
//#include "UltraCanvasButton3Sections.h"
#include "UltraCanvasFormulaEditor.h"
#include "Plugins/Charts/UltraCanvasDivergingBarChart.h"
#include <sstream>
#include <random>
#include <map>

namespace UltraCanvas {
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateSliderExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("SliderExamples", 400, 0, 0, 1000, 600);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("SliderTitle", 401, 10, 10, 300, 30);
        title->SetText("Slider Examples");
        title->SetFontSize(16);
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
        auto vSlider = std::make_shared<UltraCanvasSlider>("VerticalSlider", 405, 500, 60, 20, 200);
        vSlider->SetOrientation(SliderOrientation::Vertical);
        vSlider->SetRange(0.0f, 10.0f);
        vSlider->SetValue(5.0f);
        vSlider->SetStep(0.5f);
        container->AddChild(vSlider);

        auto vSliderLabel = std::make_shared<UltraCanvasLabel>("VSliderLabel", 406, 530, 60, 150, 20);
        vSliderLabel->SetText("Vertical Slider");
        vSliderLabel->SetFontSize(12);
        container->AddChild(vSliderLabel);

        // Range Slider
//        auto rangeSlider = std::make_shared<UltraCanvasSlider>("RangeSlider", 407, 20, 150, 400, 20);
//        rangeSlider->SetOrientation(SliderOrientation::Horizontal);
//        rangeSlider->SetRangeMode(true);
//        rangeSlider->SetRange(0.0f, 1000.0f);
//        rangeSlider->SetLowerValue(200.0f);
//        rangeSlider->SetUpperValue(800.0f);
//        container->AddChild(rangeSlider);
//
//        auto rangeLabel = std::make_shared<UltraCanvasLabel>("RangeLabel", 408, 20, 175, 200, 20);
//        rangeLabel->SetText("Range Slider (0-1000)");
//        rangeLabel->SetFontSize(12);
//        container->AddChild(rangeLabel);

        return container;
    }

} // namespace UltraCanvas