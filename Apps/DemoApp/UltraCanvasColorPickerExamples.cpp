// Apps/DemoApp/UltraCanvasColorPickerExamples.cpp
// Demonstration of the comprehensive colour picker widget.
// Version: 1.1.0
// Last Modified: 2026-07-20
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasColorPicker.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateColorPickerExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("ColorPickerExamples", 0, 0, 1000, 1120);
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

        // Foreground colour #85FFFBFF, background (previous) swatch red.
        auto picker = CreateColorPicker("FullColorPicker", Color(0x85, 0xFF, 0xFB, 0xFF),
                                        20, 75, 290, 470);
        picker->SetBackgroundColor(Color(255, 0, 0, 255));
        // No onScreenColorPick callback: clicking the eyedropper button arms the
        // built-in mode — the pointer becomes an eyedropper and the next click
        // samples the pixel under it (Select/left mouse -> foreground colour,
        // Adjust/right mouse -> background colour, Esc cancels).
        container->AddChild(picker);

        // ===== Same picker scaled to 60% =====
        auto scaledLabel = CreateLabel("ScaledPickerLabel", 340, 50, 300, 20);
        scaledLabel->SetText("Scaled to 60%");
        scaledLabel->SetFontSize(12);
        container->AddChild(scaledLabel);

        auto scaled = CreateColorPicker("ScaledColorPicker", Color(0x85, 0xFF, 0xFB, 0xFF),
                                        340, 75, 174, 282);
        scaled->SetBackgroundColor(Color(255, 0, 0, 255));
        scaled->SetUIScale(0.6f);
        container->AddChild(scaled);

        // ===== One row with the slider / colour-display / selector variants =====
        auto variantsTitle = CreateLabel("VariantsTitle", 20, 560, 700, 22);
        variantsTitle->SetText("Variants: slider styles, colour display styles, mode selector styles");
        variantsTitle->SetFontSize(14);
        variantsTitle->SetFontWeight(FontWeight::Bold);
        container->AddChild(variantsTitle);

        const float rowY = 615.0f;

        // --- 1: thin sliders, hue ring, tab-bar selector, value spinners ---
        auto thinLabel = CreateLabel("ThinVariantLabel", 20, 590, 290, 20);
        thinLabel->SetText("Thin sliders • ring • tab bar • < > steppers");
        thinLabel->SetFontSize(12);
        container->AddChild(thinLabel);

        auto thin = CreateColorPicker("ThinRingPicker", Color(0x85, 0xFF, 0xFB, 0xFF),
                                      20, rowY, 290, 470);
        thin->SetSliderStyle(ColorPickerSliderStyle::Thin);
        thin->SetWheelStyle(ColorPickerWheelStyle::Ring);
        thin->SetModeSelector(ColorPickerModeSelector::TabBar);
        thin->SetShowValueSpinners(true);
        container->AddChild(thin);

        // --- 2: thick sliders, SV rectangle + hue bar, dropdown selector ---
        auto thickLabel = CreateLabel("ThickVariantLabel", 330, 590, 290, 20);
        thickLabel->SetText("Thick sliders • hue bar • dropdown");
        thickLabel->SetFontSize(12);
        container->AddChild(thickLabel);

        auto thick = CreateColorPicker("ThickBarPicker", Color(0x85, 0xFF, 0xFB, 0xFF),
                                       330, rowY, 290, 470);
        thick->SetSliderStyle(ColorPickerSliderStyle::Thick);
        thick->SetWheelStyle(ColorPickerWheelStyle::Bar);
        thick->SetModeSelector(ColorPickerModeSelector::Dropdown);
        container->AddChild(thick);

        // --- 3: collapsible sliders (collapsed by default) behind the
        //        disclosure icon; dropdown selector, thick sliders ---
        auto collLabel = CreateLabel("CollapsibleVariantLabel", 640, 590, 320, 20);
        collLabel->SetText("Collapsible sliders (click the arrow) • dropdown");
        collLabel->SetFontSize(12);
        container->AddChild(collLabel);

        auto collapsible = CreateColorPicker("CollapsiblePicker", Color(0x85, 0xFF, 0xFB, 0xFF),
                                             640, rowY, 290, 470);
        collapsible->SetSliderStyle(ColorPickerSliderStyle::Thick);
        collapsible->SetWheelStyle(ColorPickerWheelStyle::Bar);
        collapsible->SetModeSelector(ColorPickerModeSelector::Dropdown);
        collapsible->SetSlidersCollapsible(true, false);   // collapsed by default
        collapsible->SetShowValueSpinners(true);
        container->AddChild(collapsible);

        return container;
    }

} // namespace UltraCanvas
