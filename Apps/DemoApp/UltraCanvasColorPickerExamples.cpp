// Apps/DemoApp/UltraCanvasColorPickerExamples.cpp
// Demonstration of the comprehensive colour picker widget.
// Version: 1.0.0
// Last Modified: 2026-06-21
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasColorPicker.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateColorPickerExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("ColorPickerExamples", 0, 0, 1000, 800);
        container->SetPadding(0, 0, 10, 0);

        auto title = CreateLabel("ColorPickerTitle", 20, 10, 0, 30);
        title->SetText("Colour Picker Examples");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // ===== Full picker: wheel + swatches + hex + tabs + channel sliders =====
        auto fullLabel = CreateLabel("FullPickerLabel", 20, 50, 300, 20);
        fullLabel->SetText("Full picker (HSV wheel, hex, HSV/HSL/RGB, alpha)");
        fullLabel->SetFontSize(12);
        container->AddChild(fullLabel);

        auto picker = CreateColorPicker("FullColorPicker", Color(255, 0, 0, 255),
                                        20, 75, 290, 470);
        container->AddChild(picker);

        // Live read-out of the selected colour.
        auto swatch = CreateLabel("ColorReadout", 340, 75, 240, 40);
        swatch->SetText(picker->GetColor().ToHexStringWithAlpha());
        swatch->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
        swatch->SetBackgroundColor(picker->GetColor());
        container->AddChild(swatch);

        auto rgbLabel = CreateLabel("ColorRgbReadout", 340, 125, 240, 24);
        auto updateReadout = [swatch, rgbLabel](const Color& c) {
            swatch->SetText(c.ToHexStringWithAlpha());
            swatch->SetBackgroundColor(c);
            // White text on dark colours, black on light ones.
            float luminance = (0.299f * c.r + 0.587f * c.g + 0.114f * c.b) / 255.0f;
            swatch->SetTextColor(luminance < 0.5f ? Colors::White : Colors::Black);
            rgbLabel->SetText("rgba(" + std::to_string(c.r) + ", " + std::to_string(c.g) +
                              ", " + std::to_string(c.b) + ", " + std::to_string(c.a) + ")");
        };
        updateReadout(picker->GetColor());
        rgbLabel->SetFontSize(12);
        rgbLabel->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
        container->AddChild(rgbLabel);

        picker->onColorChanging = updateReadout;
        picker->onColorChanged = updateReadout;

        // ===== Compact picker: channel sliders only (no wheel) =====
        auto compactLabel = CreateLabel("CompactPickerLabel", 340, 180, 300, 20);
        compactLabel->SetText("Compact picker (sliders only, no wheel)");
        compactLabel->SetFontSize(12);
        container->AddChild(compactLabel);

        auto compact = CreateColorPicker("CompactColorPicker", Color(60, 160, 220, 255),
                                         340, 205, 260, 150);
        compact->SetShowColorWheel(false);
        container->AddChild(compact);

        return container;
    }

} // namespace UltraCanvas
