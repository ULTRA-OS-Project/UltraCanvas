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

        // Screen colour picker ("eyedropper"): left mouse targets the foreground
        // colour, right mouse the background colour. Real applications sample a
        // pixel from the screen here; this demo just cycles a couple of sample
        // colours so the wiring can be seen.
        std::weak_ptr<UltraCanvasColorPicker> weakPicker = picker;
        picker->onScreenColorPick = [weakPicker](bool foreground) {
            auto p = weakPicker.lock();
            if (!p) return;
            // Placeholder "sampled" colour; a host would read the real screen pixel.
            Color sampled = foreground ? Color(64, 160, 255, 255)
                                       : Color(255, 200, 64, 255);
            if (foreground) p->SetForegroundColor(sampled);
            else            p->SetBackgroundColor(sampled);
        };

        container->AddChild(picker);

        return container;
    }

} // namespace UltraCanvas
