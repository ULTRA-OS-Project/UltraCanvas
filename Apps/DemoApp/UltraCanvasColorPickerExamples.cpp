// Apps/DemoApp/UltraCanvasColorPickerExamples.cpp
// Demonstration of the comprehensive colour picker widget.
// Version: 1.3.0
// Last Modified: 2026-10-05
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasColorPicker.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateColorPickerExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("ColorPickerExamples", 0, 0, 1000, 1680);
        container->SetPadding(0, 0, 10, 0);

        auto title = CreateLabel("ColorPickerTitle", 20, 10, 0, 30);
        title->SetText("Colour Picker Examples");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // Three columns, 310 px apart; each row is a caption line, then the
        // pickers 25 px below it.
        const float col1X = 20.0f, col2X = 330.0f, col3X = 640.0f;
        const float pickerW = 290.0f, pickerH = 470.0f;

        // ===== Row 1: full picker, 1:1 pixel-exact square, field strip =====
        const float row1Y = 75.0f;

        // --- Full picker: wheel + swatches + hex + tabs + channel sliders ---
        auto fullLabel = CreateLabel("FullPickerLabel", col1X, row1Y - 25, 300, 20);
        fullLabel->SetText("Full picker (wheel, hex, HSV/HSL/RGB)");
        fullLabel->SetFontSize(12);
        container->AddChild(fullLabel);

        // Foreground colour #85FFFBFF, background (previous) swatch red.
        auto picker = CreateColorPicker("FullColorPicker", Color(0x85, 0xFF, 0xFB, 0xFF),
                                        col1X, row1Y, pickerW, pickerH);
        picker->SetBackgroundColor(Color(255, 0, 0, 255));
        // No onScreenColorPick callback: clicking the eyedropper button arms the
        // built-in mode — the pointer becomes an eyedropper and the next click
        // samples the pixel under it (Select/left mouse -> foreground colour,
        // Adjust/right mouse -> background colour, Esc cancels).
        container->AddChild(picker);

        // --- Collapsible sliders (collapsed by default) behind the disclosure
        //     icon, with a 1:1 SV square at 256 x 256: 8-bit channels have
        //     256 steps, so each pixel across is one step of saturation and
        //     each pixel down one step of value - none skipped, none repeated.
        //     Sized for the expanded sliders, so opening them keeps it exact. ---
        auto collLabel = CreateLabel("CollapsibleVariantLabel", col2X, row1Y - 25, 300, 20);
        collLabel->SetText("256 x 256 square • collapsible sliders");
        collLabel->SetFontSize(12);
        container->AddChild(collLabel);

        auto collapsible = CreateColorPicker("CollapsiblePicker", Color(0x85, 0xFF, 0xFB, 0xFF),
                                             col2X, row1Y, pickerW, pickerH);
        collapsible->SetSliderStyle(ColorPickerSliderStyle::Thick);
        collapsible->SetWheelStyle(ColorPickerWheelStyle::Bar);
        collapsible->SetSVAreaShape(ColorPickerSVAreaShape::PixelExact);
        collapsible->SetModeSelector(ColorPickerModeSelector::Dropdown);
        collapsible->SetSlidersCollapsible(true, false);   // collapsed by default
        collapsible->SetShowValueSpinners(true);
        collapsible->SetElementSize(Size2Df(pickerW, collapsible->PreferredHeightForWidth(pickerW)));
        container->AddChild(collapsible);

        // --- The hue x lightness field with the channel sliders collapsed, so
        //     the field gets the height (like a narrow palette strip) ---
        auto stripLabel = CreateLabel("HLStripVariantLabel", col3X, row1Y - 25, 320, 20);
        stripLabel->SetText("Field, collapsible sliders • dropdown");
        stripLabel->SetFontSize(12);
        container->AddChild(stripLabel);

        auto strip = CreateColorPicker("HueLightnessStripPicker", Color(0x40, 0xFF, 0x40, 0xFF),
                                       col3X, row1Y, pickerW, pickerH);
        strip->SetWheelStyle(ColorPickerWheelStyle::HueLightnessField);
        strip->SetModeSelector(ColorPickerModeSelector::Dropdown);
        strip->SetSlidersCollapsible(true, false);
        container->AddChild(strip);

        const float row1Bottom = row1Y + std::max(pickerH, collapsible->GetHeight());

        // ===== Row 2: slider / colour-display / selector variants =====
        const float variantsTitleY = row1Bottom + 15.0f;
        auto variantsTitle = CreateLabel("VariantsTitle", col1X, variantsTitleY, 700, 22);
        variantsTitle->SetText("Variants: slider styles, colour display styles, mode selector styles");
        variantsTitle->SetFontSize(14);
        variantsTitle->SetFontWeight(FontWeight::Bold);
        container->AddChild(variantsTitle);

        const float rowY = variantsTitleY + 55.0f;

        // --- 1: thin sliders, hue ring, tab-bar selector, value spinners ---
        auto thinLabel = CreateLabel("ThinVariantLabel", col1X, rowY - 25, 290, 20);
        thinLabel->SetText("Thin sliders • ring • tab bar • < > steppers");
        thinLabel->SetFontSize(12);
        container->AddChild(thinLabel);

        auto thin = CreateColorPicker("ThinRingPicker", Color(0x85, 0xFF, 0xFB, 0xFF),
                                      col1X, rowY, pickerW, pickerH);
        thin->SetSliderStyle(ColorPickerSliderStyle::Thin);
        thin->SetWheelStyle(ColorPickerWheelStyle::Ring);
        thin->SetModeSelector(ColorPickerModeSelector::TabBar);
        thin->SetShowValueSpinners(true);
        container->AddChild(thin);

        // --- 2: thick sliders, SV rectangle + hue bar, dropdown selector ---
        auto thickLabel = CreateLabel("ThickVariantLabel", col2X, rowY - 25, 290, 20);
        thickLabel->SetText("Thick sliders • hue bar • dropdown");
        thickLabel->SetFontSize(12);
        container->AddChild(thickLabel);

        auto thick = CreateColorPicker("ThickBarPicker", Color(0x85, 0xFF, 0xFB, 0xFF),
                                       col2X, rowY, pickerW, pickerH);
        thick->SetSliderStyle(ColorPickerSliderStyle::Thick);
        thick->SetWheelStyle(ColorPickerWheelStyle::Bar);
        thick->SetModeSelector(ColorPickerModeSelector::Dropdown);
        container->AddChild(thick);

        // ===== Row 3: colour + intensity styles (full saturation), and the
        //       full picker at 60% =====
        const float hlTitleY = rowY + pickerH + 15.0f;
        auto hlTitle = CreateLabel("HueLightnessTitle", col1X, hlTitleY, 700, 22);
        hlTitle->SetText("Colour + intensity: hue x lightness field and sliders; picker scaled to 60%");
        hlTitle->SetFontSize(14);
        hlTitle->SetFontWeight(FontWeight::Bold);
        container->AddChild(hlTitle);

        const float hlRowY = hlTitleY + 55.0f;

        // --- 4: hue (top -> bottom) x lightness (black -> white) field ---
        auto fieldLabel = CreateLabel("HLFieldVariantLabel", col1X, hlRowY - 25, 290, 20);
        fieldLabel->SetText("Hue x lightness field");
        fieldLabel->SetFontSize(12);
        container->AddChild(fieldLabel);

        auto field = CreateColorPicker("HueLightnessFieldPicker", Color(0x00, 0xA0, 0xFF, 0xFF),
                                       col1X, hlRowY, pickerW, pickerH);
        field->SetWheelStyle(ColorPickerWheelStyle::HueLightnessField);
        container->AddChild(field);

        // --- 5: colour slider + intensity (white -> black) slider ---
        auto slidersLabel = CreateLabel("HLSlidersVariantLabel", col2X, hlRowY - 25, 290, 20);
        slidersLabel->SetText("Colour slider + intensity slider");
        slidersLabel->SetFontSize(12);
        container->AddChild(slidersLabel);

        auto twoSliders = CreateColorPicker("HueLightnessSlidersPicker", Color(0xFF, 0x80, 0x00, 0xFF),
                                            col2X, hlRowY, pickerW, pickerH);
        twoSliders->SetWheelStyle(ColorPickerWheelStyle::HueLightnessSliders);
        twoSliders->SetSliderStyle(ColorPickerSliderStyle::Thick);
        twoSliders->SetElementSize(Size2Df(pickerW, twoSliders->PreferredHeightForWidth(pickerW)));
        container->AddChild(twoSliders);

        // --- 6: the full picker scaled to 60% ---
        auto scaledLabel = CreateLabel("ScaledPickerLabel", col3X, hlRowY - 25, 300, 20);
        scaledLabel->SetText("Scaled to 60%");
        scaledLabel->SetFontSize(12);
        container->AddChild(scaledLabel);

        auto scaled = CreateColorPicker("ScaledColorPicker", Color(0x85, 0xFF, 0xFB, 0xFF),
                                        col3X, hlRowY, 174, 282);
        scaled->SetBackgroundColor(Color(255, 0, 0, 255));
        scaled->SetUIScale(0.6f);
        container->AddChild(scaled);

        container->SetElementSize(Size2Df(1000, hlRowY + pickerH + 20.0f));
        return container;
    }

} // namespace UltraCanvas
