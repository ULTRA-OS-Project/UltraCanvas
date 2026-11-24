// Apps/DemoApp/UltraCanvasSliderExamples.cpp
// Comprehensive demonstration of dual-handle range slider functionality
// Version: 1.2.0 - "Slider designs": ticks, labels, stops, the bubble, a centre origin, the
//                  bar / pointer / block / fader handles, field, seek, ruler, knob, segments
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): one card per
//                  slider, each beside its live readout, every text sized to fit
// Version: 1.0.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include <sstream>
#include <iomanip>
#include <cmath>

namespace UltraCanvas {
    namespace {
        // Mixing-desk scale: the fader's travel (0..100) against dB, steeper
        // at the bottom, as a console's is. 0 dB sits about three quarters up.
        float FaderValueToDb(float value) {
            float f = std::max(0.0f, std::min(1.0f, value / 100.0f));
            return std::pow(f, 1.0f / 1.6f) * 66.0f - 60.0f;
        }
        float FaderDbToValue(float db) {
            return std::pow((db + 60.0f) / 66.0f, 1.6f) * 100.0f;
        }

        std::string FormatMinutes(float seconds) {
            int total = static_cast<int>(seconds);
            std::ostringstream text;
            text << total / 60 << ":" << std::setfill('0') << std::setw(2) << total % 60;
            return text.str();
        }

        // The design gallery: one card per look the slider can take, each a
        // live slider with a line on what it is for.
        void AddSliderDesigns(const std::shared_ptr<UltraCanvasContainer>& page) {
            auto section = DemoKit::CreateColumn("SliderDesigns", DemoKit::kCardGap);
            DemoKit::Add(page, section);
            auto title = DemoKit::AddHeading(section, "SliderDesignsTitle", Tr("Slider Designs"));
            title->SetFontSize(18);

            const float cardWidth = 300 + 2 * DemoKit::kCardPadding + 2;
            auto addDesign = [cardWidth](const std::shared_ptr<UltraCanvasContainer>& row, const std::string& id,
                                         const std::string& name, const std::string& description,
                                         const std::shared_ptr<UltraCanvasSlider>& slider) {
                auto card = DemoKit::AddCard(row, id, name, description, cardWidth);
                DemoKit::AddFixed(card, slider);
            };

            // ----- Ticks and scales -----
            auto ticksRow = DemoKit::AddRow(section, "SliderDesignTicks");

            auto ramp = CreateSlider("RampTicksSlider", 0, 0, 300, 40);
            ramp->SetRange(0.0f, 100.0f);
            ramp->SetStep(25.0f);
            ramp->SetValue(75.0f);
            ramp->SetTickPosition(SliderTickPosition::Before);
            ramp->SetTickStyle(SliderTickStyle::Ramp);
            ramp->SetTickInterval(25.0f);
            ramp->SetHandleShape(SliderHandleShape::Block);
            ramp->SetHandleSize(14.0f);
            ramp->GetStyle().trackInset = true;
            ramp->GetStyle().tickLength = 12.0f;
            ramp->GetStyle().tickGap = 1.0f;
            addDesign(ticksRow, "RampTicksCard", Tr("Ramp Ticks"),
                      Tr("Ticks that grow towards the maximum, a sunken track and a block thumb"), ramp);

            auto pointer = CreateSlider("PointerTicksSlider", 0, 0, 300, 40);
            pointer->SetRange(0.0f, 100.0f);
            pointer->SetStep(10.0f);
            pointer->SetValue(60.0f);
            pointer->SetTickPosition(SliderTickPosition::After);
            pointer->SetTickInterval(10.0f);
            pointer->SetHandleShape(SliderHandleShape::Pointer);
            pointer->GetStyle().handleShadow = true;
            addDesign(ticksRow, "PointerTicksCard", Tr("Pointer and Ticks"),
                      Tr("A five-sided thumb that points at the tick row"), pointer);

            auto scale = CreateSlider("LabelledScaleSlider", 0, 0, 300, 56);
            scale->SetRange(0.0f, 100.0f);
            scale->SetValue(35.0f);
            scale->SetTickPosition(SliderTickPosition::After);
            scale->SetTickInterval(25.0f, 5.0f);
            scale->SetTickLabels(true);
            scale->tickLabelFormatter = [](float value) { return TrF("{0} ms", static_cast<int>(value)); };
            addDesign(ticksRow, "LabelledScaleCard", Tr("Labelled Scale"),
                      Tr("Major and minor ticks, the major ones labelled with their unit"), scale);

            // ----- Snapping and value display -----
            auto valuesRow = DemoKit::AddRow(section, "SliderDesignValues");

            auto stops = CreateSlider("DiscreteStopsSlider", 0, 0, 300, 56);
            stops->SetRange(0.0f, 6.0f);
            stops->SetStep(1.0f);
            stops->SetValue(3.0f);
            stops->SetTickPosition(SliderTickPosition::OnTrack);
            stops->SetTickStyle(SliderTickStyle::Dot);
            stops->SetStopLabels({Tr("Off"), "XS", "S", "M", "L", "XL", "XXL"});
            stops->SetTrackHeight(10.0f);
            stops->SetHandleSize(20.0f);
            stops->GetStyle().handleShadow = true;
            stops->GetStyle().cornerRadius = 5.0f;
            addDesign(valuesRow, "DiscreteStopsCard", Tr("Discrete Stops"),
                      Tr("A short list of choices: a dot and a name per stop"), stops);

            auto bubble = CreateSlider("ValueBubbleSlider", 0, 0, 300, 56);
            bubble->SetRange(0.0f, 100.0f);
            bubble->SetValue(72.0f);
            bubble->SetValueDisplay(SliderValueDisplay::Bubble);
            bubble->SetValueFormat("%.0f%%");
            bubble->SetTrackHeight(4.0f);
            addDesign(valuesRow, "ValueBubbleCard", Tr("Value Bubble"),
                      Tr("The value rides above the thumb in a balloon"), bubble);

            auto balance = CreateSlider("CentreOriginSlider", 0, 0, 300, 50);
            balance->SetRange(-100.0f, 100.0f);
            balance->SetValue(-30.0f);
            balance->SetFillOrigin(0.0f);
            balance->SetResetValue(0.0f);
            balance->SetTickPosition(SliderTickPosition::After);
            balance->SetTickMarks({-100.0f, 0.0f, 100.0f});
            balance->SetTickLabels(true);
            balance->tickLabelFormatter = [](float value) {
                return value < 0.0f ? Tr("L") : (value > 0.0f ? Tr("R") : Tr("C"));
            };
            balance->GetStyle().handleShadow = true;
            addDesign(valuesRow, "CentreOriginCard", Tr("Centre Origin"),
                      Tr("Fills outward from zero; a double-click returns to the centre"), balance);

            // ----- Track and handle looks -----
            auto looksRow = DemoKit::AddRow(section, "SliderDesignLooks");

            auto pill = CreateSlider("PillTrackSlider", 0, 0, 300, 40);
            pill->SetRange(0.0f, 100.0f);
            pill->SetValue(58.0f);
            pill->SetTrackHeight(16.0f);
            pill->SetHandleSize(20.0f);
            pill->SetHandleShape(SliderHandleShape::Bar);
            pill->GetStyle().cornerRadius = 8.0f;
            pill->GetStyle().handleGap = 4.0f;
            pill->GetStyle().borderWidth = 0.0f;
            addDesign(looksRow, "PillTrackCard", Tr("Pill Track and Bar Thumb"),
                      Tr("A tall rounded track split by a slim bar"), pill);

            auto soft = CreateSlider("SoftTrackSlider", 0, 0, 300, 40);
            soft->SetRange(0.0f, 100.0f);
            soft->SetValue(40.0f);
            soft->SetTrackHeight(8.0f);
            soft->SetHandleSize(28.0f);
            soft->GetStyle().handleShadow = true;
            soft->GetStyle().borderWidth = 0.0f;
            soft->GetStyle().cornerRadius = 4.0f;
            addDesign(looksRow, "SoftTrackCard", Tr("Soft Thick Track"),
                      Tr("A big touch target with a shadowed knob"), soft);

            auto field = CreateSlider("FieldSlider", 0, 0, 300, 28);
            field->SetSliderStyle(SliderStyle::Field);
            field->SetRange(0.0f, 1.0f);
            field->SetStep(0.01f);
            field->SetValue(0.65f);
            field->SetValueFormat("%.2f");
            field->SetCustomText(Tr("Roughness"));
            addDesign(looksRow, "FieldCard", Tr("Inline Value Field"),
                      Tr("For property panels: the name and the number inside the bar"), field);

            // ----- Specialised -----
            auto specialRow = DemoKit::AddRow(section, "SliderDesignSpecial");

            auto seek = CreateSlider("SeekSlider", 0, 0, 300, 50);
            seek->SetSliderStyle(SliderStyle::Seek);
            seek->SetRange(0.0f, 213.0f);
            seek->SetStep(0.0f);
            seek->SetValue(81.0f);
            seek->SetSecondaryValue(140.0f);
            seek->SetHoverPreview(true);
            seek->hoverText = [](float value) { return FormatMinutes(value); };
            seek->SetTrackHeight(4.0f);
            seek->SetHandleSize(14.0f);
            addDesign(specialRow, "SeekCard", Tr("Media Seek Bar"),
                      Tr("What has loaded, and the time under the pointer"), seek);

            auto histogram = CreateRangeSlider("HistogramRangeSlider", 0, 0, 300, 110, 0.0f, 500.0f, 140.0f, 350.0f);
            histogram->SetStep(10.0f);
            histogram->SetHistogram({2, 3, 5, 8, 6, 9, 12, 15, 13, 18, 22, 19, 16, 14,
                                     17, 11, 9, 10, 7, 6, 8, 5, 4, 3, 4, 2, 2, 1});
            histogram->SetValueDisplay(SliderValueDisplay::BubbleWhileDragging);
            histogram->SetValueFormat("%.0f");
            histogram->GetStyle().handleShadow = true;
            addDesign(specialRow, "HistogramCard", Tr("Range with Histogram"),
                      Tr("Where the data bunches up, behind a price filter"), histogram);

            auto ruler = CreateSlider("RulerSlider", 0, 0, 300, 64);
            ruler->SetSliderStyle(SliderStyle::Ruler);
            ruler->SetRange(-45.0f, 45.0f);
            ruler->SetStep(0.5f);
            ruler->SetValue(12.0f);
            ruler->SetTickInterval(10.0f, 2.0f);
            ruler->SetTickLabels(true);
            ruler->tickLabelFormatter = [](float value) { return std::to_string(static_cast<int>(value)) + "°"; };
            ruler->SetValueDisplay(SliderValueDisplay::Number);
            ruler->SetValueFormat("%+.1f°");
            ruler->SetPixelsPerUnit(6.0f);
            ruler->GetStyle().tickLength = 12.0f;
            ruler->GetStyle().minorTickLength = 6.0f;
            addDesign(specialRow, "RulerCard", Tr("Ruler Dial"),
                      Tr("The scale slides under a fixed marker: fine control in little space"), ruler);

            // ----- Rotary and vertical -----
            auto rotaryRow = DemoKit::AddRow(section, "SliderDesignRotary");
            const float smallCard = 140 + 2 * DemoKit::kCardPadding + 2;

            auto arc = CreateCircularSlider("KnobArcSlider", 0, 0, 120, 0.0f, 100.0f);
            arc->SetValue(64.0f);
            arc->SetValueDisplay(SliderValueDisplay::Number);
            arc->SetValueFormat("%.0f");
            arc->SetTrackHeight(8.0f);
            arc->SetHandleSize(14.0f);
            DemoKit::AddFixed(DemoKit::AddCard(rotaryRow, "KnobArcCard", Tr("Knob with Arc"),
                                               Tr("Drag up or down, or use the wheel"), smallCard), arc);

            auto knob = CreateSlider("KnobDotsSlider", 0, 0, 120, 120);
            knob->SetSliderStyle(SliderStyle::Knob);
            knob->SetRange(0.0f, 10.0f);
            knob->SetStep(0.1f);
            knob->SetValue(7.0f);
            DemoKit::AddFixed(DemoKit::AddCard(rotaryRow, "KnobDotsCard", Tr("Knob with Dot Ring"),
                                               Tr("A raised cap inside a ring of dots"), smallCard), knob);

            auto fader = CreateSlider("ConsoleFaderSlider", 0, 0, 100, 220);
            fader->SetOrientation(SliderOrientation::Vertical);
            fader->SetRange(0.0f, 100.0f);
            fader->SetStep(0.0f);
            fader->SetValue(FaderDbToValue(0.0f));
            fader->SetResetValue(FaderDbToValue(0.0f));
            fader->SetHandleShape(SliderHandleShape::FaderCap);
            fader->SetHandleSize(18.0f);
            fader->SetTrackHeight(6.0f);
            fader->GetStyle().trackInset = true;
            fader->SetTickPosition(SliderTickPosition::Before);
            std::vector<float> faderMarks;
            for (float db : {6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -48.0f}) faderMarks.push_back(FaderDbToValue(db));
            fader->SetTickMarks(faderMarks);
            fader->SetTickLabels(true);
            fader->tickLabelFormatter = [](float value) {
                int db = static_cast<int>(std::lround(FaderValueToDb(value)));
                return (db > 0 ? "+" : "") + std::to_string(db);
            };
            DemoKit::AddFixed(DemoKit::AddCard(rotaryRow, "ConsoleFaderCard", Tr("Console Fader"),
                                               Tr("A ribbed cap over a dB scale; double-click for 0 dB"), smallCard), fader);

            auto meter = CreateSlider("SegmentedSlider", 0, 0, 60, 220);
            meter->SetSliderStyle(SliderStyle::Segmented);
            meter->SetOrientation(SliderOrientation::Vertical);
            meter->SetRange(0.0f, 100.0f);
            meter->SetValue(70.0f);
            meter->SetSegmentCount(16);
            meter->SetColorZones({{0.0f, Color(48, 164, 108)}, {0.7f, Color(245, 165, 36)}, {0.875f, Color(229, 72, 77)}});
            meter->SetTrackHeight(26.0f);
            meter->SetHandleShape(SliderHandleShape::NoHandle);
            meter->GetStyle().cornerRadius = 2.0f;
            DemoKit::AddFixed(DemoKit::AddCard(rotaryRow, "SegmentedCard", Tr("LED Segments"),
                                               Tr("A level meter with colour zones that can be set"), smallCard), meter);
        }
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateSliderExamples() {
        auto container = DemoKit::CreatePage("SliderExamples", Tr("Simple Slider Examples"), "");

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
        auto hCard = DemoKit::AddCard(basicCards, "HSliderLabel", Tr("Horizontal Slider (0-100)"));
        auto hRow = DemoKit::AddRow(hCard, "HSliderRow", 20);
        auto hSlider = CreateSlider("HorizontalSlider", 0, 0, 300, 30);
        hSlider->SetOrientation(SliderOrientation::Horizontal);
        hSlider->SetRange(0.0f, 100.0f);
        hSlider->SetValue(50.0f);
        hSlider->SetStep(10.0f);
        hSlider->SetTickPosition(SliderTickPosition::After);
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
        auto vCard = DemoKit::AddCard(basicCards, "VSliderLabel", Tr("Vertical Slider"));
        auto vSlider = CreateSlider("VerticalSlider", 0, 0, 20, 200);
        vSlider->SetOrientation(SliderOrientation::Vertical);
        vSlider->SetRange(0.0f, 10.0f);
        vSlider->SetValue(5.0f);
        vSlider->SetStep(0.5f);
        DemoKit::AddFixed(vCard, vSlider);

        // Vertical dual-handle (range) slider, beside the simple vertical slider.
        auto volumeCard = DemoKit::AddCard(basicCards, "VolumeLabel", Tr("Vertical Volume Range (0-100)"));
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
        auto volumeDisplay = addReadout(volumeRow, "VolumeDisplay", TrF("Volume Range: {0} - {1}", 30, 80),
                                        Color(220, 255, 220));

        volumeRange->onRangeChanged = [volumeDisplay](float lower, float upper) {
            volumeDisplay->SetText(TrF("Volume Range: {0} - {1}", static_cast<int>(lower), static_cast<int>(upper)));
        };

        // ===== HORIZONTAL COLOUR PALETTE SLIDER =====
        // A hue palette painted inside the bar (colour-picker style) with a small
        // handle that fits within the bar. The field on the right previews the
        // selected colour and shows its RGB value.
        auto paletteCard = DemoKit::AddCard(basicCards, "PaletteLabel", Tr("Colour Palette Slider (Hue)"));
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
            paletteSwatch->SetText(TrF("RGB {0}, {1}, {2}", static_cast<int>(c.r),
                                       static_cast<int>(c.g), static_cast<int>(c.b)));
        };
        updateSwatch(0.0f);
        paletteSlider->onValueChanged = [updateSwatch](float value) {
            updateSwatch(value);
        };

        AddSliderDesigns(container);

        auto rangeSlidersContainer = DemoKit::CreateColumn("RangeSliderDemos", DemoKit::kPageGap);
        DemoKit::Add(container, rangeSlidersContainer);

        const int sliderWidth = 400;
        // A card is at least as wide as its slider; two share a line when they fit.
        const float rangeCardWidth = sliderWidth + 2 * DemoKit::kCardPadding + 2;

        // ===== TITLE =====
        auto rangeTitle = DemoKit::AddHeading(rangeSlidersContainer, "Title", Tr("Range Slider Demonstrations"));
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
        auto priceRow = addRangeCard("PriceLabel", Tr("Price Range Selector ($0 - $1000)"));

        auto priceRange = CreateRangeSlider("priceRange", 0, 0, sliderWidth, 40,
                                            0.0f, 1000.0f, 200.0f, 800.0f);
        priceRange->SetValueDisplay(SliderValueDisplay::AlwaysVisible);
        priceRange->SetValueFormat("$%.0f");
        priceRange->SetHandleCollisionMargin(50.0f);

        DemoKit::AddFixed(priceRow, priceRange);
        // Price range display labels
        auto priceLowerLabel = addReadout(priceRow, "PriceLower", TrF("Min: ${0}", 200), Color(220, 240, 255));

        auto priceUpperLabel = addReadout(priceRow, "PriceUpper", TrF("Max: ${0}", 800), Color(220, 240, 255));

        priceRange->onLowerValueChanged = [priceLowerLabel](float value) {
            priceLowerLabel->SetText(TrF("Min: ${0}", static_cast<int>(value)));
        };

        priceRange->onUpperValueChanged = [priceUpperLabel](float value) {
            priceUpperLabel->SetText(TrF("Max: ${0}", static_cast<int>(value)));
        };

        // ===== EXAMPLE 2: AGE RANGE SELECTOR =====
        auto ageRow = addRangeCard("AgeLabel", Tr("Age Range Filter (18 - 100 years)"));

        auto ageRange = CreateRangeSlider("ageRange", 0, 0, sliderWidth, 40,
                                          18.0f, 100.0f, 25.0f, 65.0f);
        ageRange->SetValueDisplay(SliderValueDisplay::Number);
        ageRange->SetValueFormat("%.0f");
        ageRange->SetStep(1.0f);
        ageRange->SetHandleCollisionMargin(1.0f);

        DemoKit::AddFixed(ageRow, ageRange);
        auto ageDisplay = addReadout(ageRow, "AgeDisplay", TrF("Age Range: {0} - {1} years", 25, 65), Color(255, 240, 220));

        ageRange->onRangeChanged = [ageDisplay](float lower, float upper) {
            ageDisplay->SetText(TrF("Age Range: {0} - {1} years", static_cast<int>(lower), static_cast<int>(upper)));
        };

        // ===== EXAMPLE 3: PERCENTAGE RANGE (0-100%) =====
        auto percentRow = addRangeCard("PercentLabel", Tr("Percentage Range (0% - 100%)"));

        auto percentRange = CreateRangeSlider("percentRange", 0, 0, sliderWidth, 40,
                                              0.0f, 100.0f, 30.0f, 70.0f);
        percentRange->SetValueDisplay(SliderValueDisplay::Percentage);
        percentRange->SetStep(5.0f);
        percentRange->SetHandleShape(SliderHandleShape::Square);

        DemoKit::AddFixed(percentRow, percentRange);
        auto percentDisplay = addReadout(percentRow, "PercentDisplay", TrF("Range: {0}% - {1}%", 30, 70), Color(240, 255, 220));

        percentRange->onRangeChanged = [percentDisplay](float lower, float upper) {
            percentDisplay->SetText(TrF("Range: {0}% - {1}%", static_cast<int>(lower), static_cast<int>(upper)));
        };

        // ===== EXAMPLE 4: TEMPERATURE RANGE (-50°C to +50°C) =====
        auto tempRow = addRangeCard("TempLabel", Tr("Temperature Range (-50°C to +50°C)"));

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
        auto tempDisplay = addReadout(tempRow, "TempDisplay", TrF("Temp: {0}°C - {1}°C", "10.0", "30.0"), Color(255, 220, 220));

        tempRange->onRangeChanged = [tempDisplay](float lower, float upper) {
            std::ostringstream lowerText, upperText;
            lowerText << std::fixed << std::setprecision(1) << lower;
            upperText << std::fixed << std::setprecision(1) << upper;
            tempDisplay->SetText(TrF("Temp: {0}°C - {1}°C", lowerText.str(), upperText.str()));
        };

        // ===== EXAMPLE 5: TIME RANGE (24-hour format) =====
        auto timeRow = addRangeCard("TimeLabel", Tr("Time Range Selector (0:00 - 24:00)"));

        auto timeRange = CreateRangeSlider("timeRange", 0, 0, sliderWidth, 20,
                                           0.0f, 24.0f, 9.0f, 17.0f);
        timeRange->SetValueDisplay(SliderValueDisplay::NoDisplay);
        timeRange->SetStep(0.5f);  // 30-minute increments
        timeRange->SetHandleShape(SliderHandleShape::Triangle);

        DemoKit::AddFixed(timeRow, timeRange);
        auto timeDisplay = addReadout(timeRow, "TimeDisplay", TrF("Work Hours: {0} - {1}", "09:00", "17:00"), Color(220, 220, 255));

        timeRange->onRangeChanged = [timeDisplay](float lower, float upper) {
            auto formatTime = [](float hours) -> std::string {
                int h = static_cast<int>(hours);
                int m = static_cast<int>((hours - h) * 60);
                std::ostringstream oss;
                oss << std::setfill('0') << std::setw(2) << h << ":" << std::setw(2) << m;
                return oss.str();
            };

            timeDisplay->SetText(TrF("Work Hours: {0} - {1}", formatTime(lower), formatTime(upper)));
        };

        // ===== EXAMPLE 6: DATE RANGE (Days 1-31) =====
        auto dateRow = addRangeCard("DateLabel", Tr("Date Range Selector (Days 1-31)"));

        auto dateRange = CreateRangeSlider("dateRange", 0, 0, sliderWidth, 40,
                                           1.0f, 31.0f, 5.0f, 25.0f);
        dateRange->SetValueDisplay(SliderValueDisplay::AlwaysVisible);
        dateRange->SetValueFormat("Day %.0f");
        dateRange->SetStep(1.0f);

        DemoKit::AddFixed(dateRow, dateRange);
        auto dateDisplay = addReadout(dateRow, "DateDisplay", TrF("Selected: Day {0} - Day {1}", 5, 25), Color(255, 240, 255));

        dateRange->onRangeChanged = [dateDisplay](float lower, float upper) {
            dateDisplay->SetText(TrF("Selected: Day {0} - Day {1}", static_cast<int>(lower), static_cast<int>(upper)));
        };

        // ===== EXAMPLE 7: ROUNDED STYLE RANGE SLIDER =====
        auto roundedRow = addRangeCard("RoundedLabel", Tr("Diamond Style handle"));

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
        auto roundedDisplay = addReadout(roundedRow, "RoundedDisplay", TrF("Range: {0} - {1}", 25, 75), Color(240, 240, 255));

        roundedRange->onRangeChanged = [roundedDisplay](float lower, float upper) {
            roundedDisplay->SetText(TrF("Range: {0} - {1}", static_cast<int>(lower), static_cast<int>(upper)));
        };

        // ===== INSTRUCTIONS =====
        DemoKit::AddNotes(container, "Instructions", Tr("Instructions"), {
                Tr("Drag handles to adjust range"),
                Tr("Click Tab to switch between handles (when focused)"),
                Tr("Use arrow keys to adjust active handle"),
                Tr("Handles cannot overlap (enforced by collision margin)")});

        return container;
    }
} // namespace UltraCanvas
