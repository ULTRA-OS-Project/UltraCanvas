// Apps/DemoApp/UltraCanvasTextRenderingExamples.cpp
// Text rendering settings demo with configurable antialias, hint style, and hint metrics
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): the options and
//                  each sample group in a card, every text sized to fit and wrapping
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "../../UltraCanvas/libspecific/Cairo/RenderContextCairo.h"
#include <iostream>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTextRenderingSettingsExamples() {
        auto mainContainer = DemoKit::CreatePage("TextRenderingExamples", Tr("Cairo Text Rendering Settings"),
                Tr("Configure Cairo text antialiasing, font hinting style, and hint metrics. Changes apply globally and take effect immediately."));

        // A floor for the first column's width, so the rows line up while a
        // longer text still widens it.
        auto setMinWidth = [](const std::shared_ptr<UltraCanvasUIElement>& element, float width) {
            CSSLayout::BoxConstraints limits;
            limits.minWidth = CSSLayout::Dimension::Px(width);
            element->boxConstraints = limits;
        };

        // ===== CONTROLS SECTION =====
        auto controlsCard = DemoKit::AddCard(mainContainer, "ControlsTitle", Tr("Rendering Options"));

        // One option: its name, the dropdown, and what it does.
        auto addOption = [&controlsCard, &setMinWidth](const std::string& rowId, const std::string& labelId,
                                                       const std::string& labelText,
                                                       const std::shared_ptr<UltraCanvasDropdown>& dropdown,
                                                       const std::string& descId, const std::string& descText) {
            auto row = DemoKit::AddRow(controlsCard, rowId, 20);
            row->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
            auto label = DemoKit::AddText(row, labelId, labelText);
            setMinWidth(label, 120);
            DemoKit::AddFixed(row, dropdown);
            dropdown->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
            DemoKit::AddCaption(row, descId, descText);
        };

        // Collect sample labels for redraw callbacks
        std::vector<std::shared_ptr<UltraCanvasLabel>> sampleLabels;

        // --- Antialias Dropdown ---
        auto aaDropdown = std::make_shared<UltraCanvasDropdown>("AADropdown", 0, 0, 200, 30);
        aaDropdown->AddItem(Tr("Default"), "0");
        aaDropdown->AddItem(Tr("None"), "1");
        aaDropdown->AddItem(Tr("Gray"), "2");
        aaDropdown->AddItem(Tr("Subpixel"), "3");
        aaDropdown->AddItem(Tr("Fast"), "4");
        aaDropdown->AddItem(Tr("Good"), "5");
        aaDropdown->AddItem(Tr("Best"), "6");
        aaDropdown->SetSelectedIndex(static_cast<int>(RenderContextCairo::GetTextAntialias()));
        addOption("AARow", "AALabel", Tr("Antialias:"), aaDropdown,
                  "AADesc", Tr("Controls antialiasing method for text rendering"));

        // --- Hint Style Dropdown ---
        auto hsDropdown = std::make_shared<UltraCanvasDropdown>("HSDropdown", 0, 0, 200, 30);
        hsDropdown->AddItem(Tr("Default"), "0");
        hsDropdown->AddItem(Tr("None"), "1");
        hsDropdown->AddItem(Tr("Slight"), "2");
        hsDropdown->AddItem(Tr("Medium"), "3");
        hsDropdown->AddItem(Tr("Full"), "4");
        hsDropdown->SetSelectedIndex(static_cast<int>(RenderContextCairo::GetTextHintStyle()));
        addOption("HSRow", "HSLabel", Tr("Hint Style:"), hsDropdown,
                  "HSDesc", Tr("Controls amount of font hinting (grid-fitting)"));

        // --- Hint Metrics Dropdown ---
        auto hmDropdown = std::make_shared<UltraCanvasDropdown>("HMDropdown", 0, 0, 200, 30);
        hmDropdown->AddItem(Tr("Default"), "0");
        hmDropdown->AddItem(Tr("Off"), "1");
        hmDropdown->AddItem(Tr("On"), "2");
        hmDropdown->SetSelectedIndex(static_cast<int>(RenderContextCairo::GetTextHintMetrics()));
        addOption("HMRow", "HMLabel", Tr("Hint Metrics:"), hmDropdown,
                  "HMDesc", Tr("Controls whether font metrics are quantized to integer values"));

        // Sample text at various sizes
        struct SampleDef {
            float fontSize;
            std::string id;        // the element ids' suffix
            std::string label;
            std::string text;
        };

        // One sample: its size in grey, then the text, which takes the rest
        // of the line and wraps.
        auto addSample = [&sampleLabels, &setMinWidth](const std::shared_ptr<UltraCanvasContainer>& card,
                                                       const std::string& sizeId, const std::string& textId,
                                                       const SampleDef& sample, float sizeWidth, bool bold) {
            auto row = DemoKit::CreateRow(textId + "Row", 10, false);
            DemoKit::Add(card, row);

            auto sizeLabel = DemoKit::CreateCaption(sizeId, sample.label);
            sizeLabel->SetTextColor(Color(150, 150, 150, 255));
            setMinWidth(sizeLabel, sizeWidth);
            DemoKit::Add(row, sizeLabel);

            auto textLabel = DemoKit::CreateText(textId, sample.text);
            textLabel->SetFontSize(sample.fontSize);
            if (bold) textLabel->SetFontWeight(FontWeight::Bold);
            DemoKit::Add(row, textLabel, 1);
            textLabel->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));

            sampleLabels.push_back(textLabel);
            sampleLabels.push_back(sizeLabel);
        };

        // ===== SAMPLE TEXT SECTION =====
        auto sampleCard = DemoKit::AddCard(mainContainer, "SampleTitle", Tr("Sample Text Preview"));
        sampleCard->layout.SetFlexGap(10);

        std::vector<SampleDef> samples = {
            {10, "10pt", Tr("10pt"), Tr("The quick brown fox jumps over the lazy dog. AaBbCcDdEeFfGgHhIiJjKkLlMmNnOoPp 0123456789")},
            {12, "12pt", Tr("12pt"), Tr("The quick brown fox jumps over the lazy dog. AaBbCcDdEeFfGgHhIiJjKkLlMmNnOoPp 0123456789")},
            {14, "14pt", Tr("14pt"), Tr("The quick brown fox jumps over the lazy dog. AaBbCcDdEeFfGgHhIiJjKkLlMm")},
            {18, "18pt", Tr("18pt"), Tr("The quick brown fox jumps over the lazy dog. AaBbCcDdEeFf 0123456789")},
            {24, "24pt", Tr("24pt"), Tr("The quick brown fox jumps over the lazy dog.")},
            {36, "36pt", Tr("36pt"), Tr("The quick brown fox jumps over the lazy dog.")},
        };

        for (const auto& sample : samples) {
            addSample(sampleCard, "SizeLabel" + sample.id, "SampleText" + sample.id, sample, 40, false);
        }

        // Bold samples
        auto boldCard = DemoKit::AddCard(mainContainer, "BoldSampleTitle", Tr("Bold Text Samples"));
        boldCard->layout.SetFlexGap(10);

        std::vector<SampleDef> boldSamples = {
            {12, "12pt Bold", Tr("12pt Bold"), Tr("The quick brown fox jumps over the lazy dog. AaBbCcDdEeFfGgHhIiJjKkLlMmNnOoPp 0123456789")},
            {18, "18pt Bold", Tr("18pt Bold"), Tr("The quick brown fox jumps over the lazy dog. AaBbCcDdEeFf 0123456789")},
            {24, "24pt Bold", Tr("24pt Bold"), Tr("Pack my box with five dozen liquor jugs.")},
        };

        for (const auto& sample : boldSamples) {
            addSample(boldCard, "BoldSizeLabel" + sample.id, "BoldSample" + sample.id, sample, 70, true);
        }

        // ===== EVENT HANDLERS =====
        auto labelsPtr = std::make_shared<std::vector<std::shared_ptr<UltraCanvasLabel>>>(sampleLabels);

        aaDropdown->onSelectionChanged = [mainContainer = mainContainer.get()](int index, const DropdownItem& item) {
            cairo_antialias_t modes[] = {
                CAIRO_ANTIALIAS_DEFAULT, CAIRO_ANTIALIAS_NONE, CAIRO_ANTIALIAS_GRAY,
                CAIRO_ANTIALIAS_SUBPIXEL, CAIRO_ANTIALIAS_FAST, CAIRO_ANTIALIAS_GOOD,
                CAIRO_ANTIALIAS_BEST
            };
            RenderContextCairo::SetTextAntialias(modes[index]);
            mainContainer->RequestRedraw();
        };

        hsDropdown->onSelectionChanged = [mainContainer = mainContainer.get()](int index, const DropdownItem& item) {
            cairo_hint_style_t styles[] = {
                CAIRO_HINT_STYLE_DEFAULT, CAIRO_HINT_STYLE_NONE, CAIRO_HINT_STYLE_SLIGHT,
                CAIRO_HINT_STYLE_MEDIUM, CAIRO_HINT_STYLE_FULL
            };
            RenderContextCairo::SetTextHintStyle(styles[index]);
            mainContainer->RequestRedraw();
        };

        hmDropdown->onSelectionChanged = [mainContainer = mainContainer.get()](int index, const DropdownItem& item) {
            cairo_hint_metrics_t metrics[] = {
                CAIRO_HINT_METRICS_DEFAULT, CAIRO_HINT_METRICS_OFF, CAIRO_HINT_METRICS_ON
            };
            RenderContextCairo::SetTextHintMetrics(metrics[index]);
            mainContainer->RequestRedraw();
        };

        return mainContainer;
    }

} // namespace UltraCanvas
