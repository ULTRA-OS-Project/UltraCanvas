// Apps/DemoApp/UltraCanvasColorPickerExamples.cpp
// Demonstration of the comprehensive colour picker widget.
// Version: 1.6.0 - the shared page look (UltraCanvasDemoPageKit): cards and
//                  wrapping rows instead of fixed columns, captions sized to fit
// Version: 1.5.0 - the colour + intensity sliders sit in a toolbox column with a
//                  palette: design selector, collapsible channels, auto height
// Version: 1.4.0 - the full picker shows the design selector
// Version: 1.3.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasColorPicker.h"
#include "UltraCanvasColorSwatchBar.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateColorPickerExamples() {
        auto page = DemoKit::CreatePage("ColorPickerExamples", "Colour Picker Examples", "");

        // Every picker keeps its size; the cards and columns around them wrap
        // under each other when the window is narrow.
        const float pickerW = 290.0f, pickerH = 470.0f;

        // A caption over a picker, as one column of a section's row.
        auto addColumn = [](const std::shared_ptr<UltraCanvasContainer>& row, const std::string& id,
                            const std::string& caption) {
            auto column = DemoKit::CreateColumn(id);
            DemoKit::AddCaption(column, id + "Label", caption);
            DemoKit::Add(row, column);
            return column;
        };

        // ===== Row 1: full picker, 1:1 pixel-exact square, field strip =====
        auto row1 = DemoKit::AddRow(page, "ColorPickerRow1");

        // --- Full picker: wheel + swatches + hex + tabs + channel sliders ---
        auto fullCard = DemoKit::AddCard(row1, "FullPickerCard", "Full picker • design selector (top right)");

        // Foreground colour #85FFFBFF, background (previous) swatch red.
        auto picker = CreateColorPicker("FullColorPicker", Color(0x85, 0xFF, 0xFB, 0xFF),
                                        0, 0, pickerW, pickerH);
        picker->SetBackgroundColor(Color(255, 0, 0, 255));
        // The design selector in the top-right corner switches between the
        // colour circle, the colour slider + square and the colour rectangle.
        // Over a rectangular design it hides while a drag picks a colour.
        picker->SetShowWheelStyleSelector(true);
        // No onScreenColorPick callback: clicking the eyedropper button arms the
        // built-in mode — the pointer becomes an eyedropper and the next click
        // samples the pixel under it (Select/left mouse -> foreground colour,
        // Adjust/right mouse -> background colour, Esc cancels).
        DemoKit::AddFixed(fullCard, picker);

        // --- Collapsible sliders (collapsed by default) behind the disclosure
        //     icon, with a 1:1 SV square at 256 x 256: 8-bit channels have
        //     256 steps, so each pixel across is one step of saturation and
        //     each pixel down one step of value - none skipped, none repeated.
        //     Sized for the expanded sliders, so opening them keeps it exact. ---
        auto collCard = DemoKit::AddCard(row1, "CollapsibleVariantCard", "256 x 256 square • collapsible sliders");

        auto collapsible = CreateColorPicker("CollapsiblePicker", Color(0x85, 0xFF, 0xFB, 0xFF),
                                             0, 0, pickerW, pickerH);
        collapsible->SetSliderStyle(ColorPickerSliderStyle::Thick);
        collapsible->SetWheelStyle(ColorPickerWheelStyle::Bar);
        collapsible->SetSVAreaShape(ColorPickerSVAreaShape::PixelExact);
        collapsible->SetModeSelector(ColorPickerModeSelector::Dropdown);
        collapsible->SetSlidersCollapsible(true, false);   // collapsed by default
        collapsible->SetShowValueSpinners(true);
        collapsible->SetElementSize(Size2Df(pickerW, collapsible->PreferredHeightForWidth(pickerW)));
        DemoKit::AddFixed(collCard, collapsible);

        // --- The hue x lightness field with the channel sliders collapsed, so
        //     the field gets the height (like a narrow palette strip) ---
        auto stripCard = DemoKit::AddCard(row1, "HLStripVariantCard", "Field, collapsible sliders • dropdown");

        auto strip = CreateColorPicker("HueLightnessStripPicker", Color(0x40, 0xFF, 0x40, 0xFF),
                                       0, 0, pickerW, pickerH);
        strip->SetWheelStyle(ColorPickerWheelStyle::HueLightnessField);
        strip->SetModeSelector(ColorPickerModeSelector::Dropdown);
        strip->SetSlidersCollapsible(true, false);
        DemoKit::AddFixed(stripCard, strip);

        // ===== Row 2: slider / colour-display / selector variants =====
        auto variantsCard = DemoKit::AddCard(page, "VariantsCard",
            "Variants: slider styles, colour display styles, mode selector styles");
        auto variantsRow = DemoKit::AddRow(variantsCard, "VariantsRow", 20);

        // --- 1: thin sliders, hue ring, tab-bar selector, value spinners ---
        auto thinColumn = addColumn(variantsRow, "ThinVariant", "Thin sliders • ring • tab bar • < > steppers");

        auto thin = CreateColorPicker("ThinRingPicker", Color(0x85, 0xFF, 0xFB, 0xFF),
                                      0, 0, pickerW, pickerH);
        thin->SetSliderStyle(ColorPickerSliderStyle::Thin);
        thin->SetWheelStyle(ColorPickerWheelStyle::Ring);
        thin->SetModeSelector(ColorPickerModeSelector::TabBar);
        thin->SetShowValueSpinners(true);
        DemoKit::AddFixed(thinColumn, thin);

        // --- 2: thick sliders, SV rectangle + hue bar, dropdown selector ---
        auto thickColumn = addColumn(variantsRow, "ThickVariant", "Thick sliders • hue bar • dropdown");

        auto thick = CreateColorPicker("ThickBarPicker", Color(0x85, 0xFF, 0xFB, 0xFF),
                                       0, 0, pickerW, pickerH);
        thick->SetSliderStyle(ColorPickerSliderStyle::Thick);
        thick->SetWheelStyle(ColorPickerWheelStyle::Bar);
        thick->SetModeSelector(ColorPickerModeSelector::Dropdown);
        DemoKit::AddFixed(thickColumn, thick);

        // ===== Row 3: colour + intensity styles (full saturation), and the
        //       full picker at 60% =====
        auto hlCard = DemoKit::AddCard(page, "HueLightnessCard",
            "Colour + intensity: hue x lightness field and sliders; picker scaled to 60%");
        auto hlRow = DemoKit::AddRow(hlCard, "HueLightnessRow", 20);

        // --- 4: hue (top -> bottom) x lightness (black -> white) field ---
        auto fieldColumn = addColumn(hlRow, "HLFieldVariant", "Hue x lightness field");

        auto field = CreateColorPicker("HueLightnessFieldPicker", Color(0x00, 0xA0, 0xFF, 0xFF),
                                       0, 0, pickerW, pickerH);
        field->SetWheelStyle(ColorPickerWheelStyle::HueLightnessField);
        DemoKit::AddFixed(fieldColumn, field);

        // --- 5: colour slider + intensity (white -> black) slider, in a
        //     toolbox column as a drawing program would have it: the HSV /
        //     HSL / RGB channels behind the disclosure arrow, the design
        //     selector top right, and the picker's height following what it
        //     shows - opening the channels or choosing a design moves the
        //     palette under it, so the column takes only the room it uses.
        //     The toolbox is 560 tall: the largest design (the circle) with
        //     its channels open, and the palette. ---
        auto slidersColumn = addColumn(hlRow, "HLSlidersVariant", "Sliders in a toolbox • channels fold away");

        auto toolbox = CreateContainer("HLSlidersToolbox", 0, 0, pickerW, 560);
        toolbox->layout.SetFlexColumn().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Start);
        DemoKit::AddFixed(slidersColumn, toolbox);

        auto twoSliders = CreateColorPicker("HueLightnessSlidersPicker", Color(0xFF, 0x80, 0x00, 0xFF),
                                            0, 0, pickerW, pickerH);
        twoSliders->SetWheelStyle(ColorPickerWheelStyle::HueLightnessSliders);
        twoSliders->SetSliderStyle(ColorPickerSliderStyle::Thick);
        twoSliders->SetSlidersCollapsible(true, false);    // channels folded away
        twoSliders->SetShowWheelStyleSelector(true);
        twoSliders->SetAutoHeight(true);                    // height follows the content
        twoSliders->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        toolbox->AddChild(twoSliders);

        auto palette = CreateColorSwatchBar("HLSlidersPalette", 0, 0, pickerW, 44);
        palette->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        // Raw pointer: the palette is the picker's sibling, both owned by the toolbox.
        UltraCanvasColorPicker* slidersPicker = twoSliders.get();
        palette->onColorSelected = [slidersPicker](const Color& c) { slidersPicker->SetColor(c); };
        toolbox->AddChild(palette);

        // --- 6: the full picker scaled to 60% ---
        auto scaledColumn = addColumn(hlRow, "ScaledPicker", "Scaled to 60%");

        auto scaled = CreateColorPicker("ScaledColorPicker", Color(0x85, 0xFF, 0xFB, 0xFF),
                                        0, 0, 174, 282);
        scaled->SetBackgroundColor(Color(255, 0, 0, 255));
        scaled->SetUIScale(0.6f);
        DemoKit::AddFixed(scaledColumn, scaled);

        return page;
    }

} // namespace UltraCanvas
