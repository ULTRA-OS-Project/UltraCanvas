// Apps/DemoApp/UltraCanvasTextRenderingExamples.cpp
// Text rendering settings demo with configurable antialias, hint style, and hint metrics
// Version: 1.0.0
// Last Modified: 2026-04-07
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "../../UltraCanvas/libspecific/Cairo/RenderContextCairo.h"
#include <iostream>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTextRenderingSettingsExamples() {
        auto mainContainer = std::make_shared<UltraCanvasContainer>("TextRenderingExamples", 3600, 0, 0, 1020, 1400);
        mainContainer->SetBackgroundColor(Colors::White);
        mainContainer->SetPadding(0, 0, 10, 0);

        long currentY = 10;

        // ===== MAIN TITLE =====
        auto mainTitle = std::make_shared<UltraCanvasLabel>("TextRenderTitle", 3601, 20, currentY, 900, 30);
        mainTitle->SetText("Cairo Text Rendering Settings");
        mainTitle->SetFontSize(18);
        mainTitle->SetFontWeight(FontWeight::Bold);
        mainContainer->AddChild(mainTitle);
        currentY += 35;

        auto description = std::make_shared<UltraCanvasLabel>("TextRenderDesc", 3602, 20, currentY, 960, 40);
        description->SetText("Configure Cairo text antialiasing, font hinting style, and hint metrics. Changes apply globally and take effect immediately.");
        description->SetWrap(TextWrap::WrapWord);
        description->SetTextColor(Color(80, 80, 80, 255));
        mainContainer->AddChild(description);
        currentY += 45;

        // Separator
        auto sep1 = std::make_shared<UltraCanvasContainer>("TextRenderSep1", 3603, 20, currentY, 960, 2);
        sep1->SetBackgroundColor(Color(200, 200, 200, 255));
        mainContainer->AddChild(sep1);
        currentY += 15;

        // ===== CONTROLS SECTION =====
        auto controlsTitle = std::make_shared<UltraCanvasLabel>("ControlsTitle", 3604, 20, currentY, 400, 25);
        controlsTitle->SetText("Rendering Options");
        controlsTitle->SetFontSize(14);
        controlsTitle->SetFontWeight(FontWeight::Bold);
        controlsTitle->SetTextColor(Color(50, 50, 150, 255));
        mainContainer->AddChild(controlsTitle);
        currentY += 35;

        // Collect sample labels for redraw callbacks
        std::vector<std::shared_ptr<UltraCanvasLabel>> sampleLabels;

        // --- Antialias Dropdown ---
        auto aaLabel = std::make_shared<UltraCanvasLabel>("AALabel", 3605, 30, currentY + 5, 120, 20);
        aaLabel->SetText("Antialias:");
        aaLabel->SetFontSize(12);
        mainContainer->AddChild(aaLabel);

        auto aaDropdown = std::make_shared<UltraCanvasDropdown>("AADropdown", 3606, 160, currentY, 200, 30);
        aaDropdown->AddItem("Default", "0");
        aaDropdown->AddItem("None", "1");
        aaDropdown->AddItem("Gray", "2");
        aaDropdown->AddItem("Subpixel", "3");
        aaDropdown->AddItem("Fast", "4");
        aaDropdown->AddItem("Good", "5");
        aaDropdown->AddItem("Best", "6");
        aaDropdown->SetSelectedIndex(static_cast<int>(RenderContextCairo::GetTextAntialias()));
        mainContainer->AddChild(aaDropdown);

        auto aaDesc = std::make_shared<UltraCanvasLabel>("AADesc", 3607, 380, currentY + 5, 500, 20);
        aaDesc->SetText("Controls antialiasing method for text rendering");
        aaDesc->SetFontSize(11);
        aaDesc->SetTextColor(Color(120, 120, 120, 255));
        mainContainer->AddChild(aaDesc);
        currentY += 45;

        // --- Hint Style Dropdown ---
        auto hsLabel = std::make_shared<UltraCanvasLabel>("HSLabel", 3608, 30, currentY + 5, 120, 20);
        hsLabel->SetText("Hint Style:");
        hsLabel->SetFontSize(12);
        mainContainer->AddChild(hsLabel);

        auto hsDropdown = std::make_shared<UltraCanvasDropdown>("HSDropdown", 3609, 160, currentY, 200, 30);
        hsDropdown->AddItem("Default", "0");
        hsDropdown->AddItem("None", "1");
        hsDropdown->AddItem("Slight", "2");
        hsDropdown->AddItem("Medium", "3");
        hsDropdown->AddItem("Full", "4");
        hsDropdown->SetSelectedIndex(static_cast<int>(RenderContextCairo::GetTextHintStyle()));
        mainContainer->AddChild(hsDropdown);

        auto hsDesc = std::make_shared<UltraCanvasLabel>("HSDesc", 3610, 380, currentY + 5, 500, 20);
        hsDesc->SetText("Controls amount of font hinting (grid-fitting)");
        hsDesc->SetFontSize(11);
        hsDesc->SetTextColor(Color(120, 120, 120, 255));
        mainContainer->AddChild(hsDesc);
        currentY += 45;

        // --- Hint Metrics Dropdown ---
        auto hmLabel = std::make_shared<UltraCanvasLabel>("HMLabel", 3611, 30, currentY + 5, 120, 20);
        hmLabel->SetText("Hint Metrics:");
        hmLabel->SetFontSize(12);
        mainContainer->AddChild(hmLabel);

        auto hmDropdown = std::make_shared<UltraCanvasDropdown>("HMDropdown", 3612, 160, currentY, 200, 30);
        hmDropdown->AddItem("Default", "0");
        hmDropdown->AddItem("Off", "1");
        hmDropdown->AddItem("On", "2");
        hmDropdown->SetSelectedIndex(static_cast<int>(RenderContextCairo::GetTextHintMetrics()));
        mainContainer->AddChild(hmDropdown);

        auto hmDesc = std::make_shared<UltraCanvasLabel>("HMDesc", 3613, 380, currentY + 5, 500, 20);
        hmDesc->SetText("Controls whether font metrics are quantized to integer values");
        hmDesc->SetFontSize(11);
        hmDesc->SetTextColor(Color(120, 120, 120, 255));
        mainContainer->AddChild(hmDesc);
        currentY += 50;

        // Separator
        auto sep2 = std::make_shared<UltraCanvasContainer>("TextRenderSep2", 3614, 20, currentY, 960, 2);
        sep2->SetBackgroundColor(Color(200, 200, 200, 255));
        mainContainer->AddChild(sep2);
        currentY += 15;

        // ===== SAMPLE TEXT SECTION =====
        auto sampleTitle = std::make_shared<UltraCanvasLabel>("SampleTitle", 3615, 20, currentY, 400, 25);
        sampleTitle->SetText("Sample Text Preview");
        sampleTitle->SetFontSize(14);
        sampleTitle->SetFontWeight(FontWeight::Bold);
        sampleTitle->SetTextColor(Color(50, 50, 150, 255));
        mainContainer->AddChild(sampleTitle);
        currentY += 35;

        // Sample text at various sizes
        struct SampleDef {
            float fontSize;
            std::string label;
            std::string text;
        };

        std::vector<SampleDef> samples = {
            {10, "10pt", "The quick brown fox jumps over the lazy dog. AaBbCcDdEeFfGgHhIiJjKkLlMmNnOoPp 0123456789"},
            {12, "12pt", "The quick brown fox jumps over the lazy dog. AaBbCcDdEeFfGgHhIiJjKkLlMmNnOoPp 0123456789"},
            {14, "14pt", "The quick brown fox jumps over the lazy dog. AaBbCcDdEeFfGgHhIiJjKkLlMm"},
            {18, "18pt", "The quick brown fox jumps over the lazy dog. AaBbCcDdEeFf 0123456789"},
            {24, "24pt", "The quick brown fox jumps over the lazy dog."},
            {36, "36pt", "The quick brown fox jumps over the lazy dog."},
        };

        long sampleId = 3620;
        for (const auto& sample : samples) {
            // Size label
            auto sizeLabel = std::make_shared<UltraCanvasLabel>(
                "SizeLabel" + sample.label, sampleId++, 30, currentY + 2, 40, 20);
            sizeLabel->SetText(sample.label);
            sizeLabel->SetFontSize(10);
            sizeLabel->SetTextColor(Color(150, 150, 150, 255));
            mainContainer->AddChild(sizeLabel);

            // Sample text
            auto textLabel = std::make_shared<UltraCanvasLabel>(
                "SampleText" + sample.label, sampleId++, 80, currentY, 900, static_cast<long>(sample.fontSize * 2.5));
            textLabel->SetText(sample.text);
            textLabel->SetFontSize(sample.fontSize);
            textLabel->SetWrap(TextWrap::WrapWord);
            mainContainer->AddChild(textLabel);

            sampleLabels.push_back(textLabel);
            sampleLabels.push_back(sizeLabel);

            currentY += static_cast<long>(sample.fontSize * 2.5) + 10;
        }

        currentY += 10;

        // Separator
        auto sep3 = std::make_shared<UltraCanvasContainer>("TextRenderSep3", 3650, 20, currentY, 960, 2);
        sep3->SetBackgroundColor(Color(200, 200, 200, 255));
        mainContainer->AddChild(sep3);
        currentY += 15;

        // Bold samples
        auto boldTitle = std::make_shared<UltraCanvasLabel>("BoldSampleTitle", 3651, 20, currentY, 400, 25);
        boldTitle->SetText("Bold Text Samples");
        boldTitle->SetFontSize(14);
        boldTitle->SetFontWeight(FontWeight::Bold);
        boldTitle->SetTextColor(Color(50, 50, 150, 255));
        mainContainer->AddChild(boldTitle);
        currentY += 35;

        std::vector<SampleDef> boldSamples = {
            {12, "12pt Bold", "The quick brown fox jumps over the lazy dog. AaBbCcDdEeFfGgHhIiJjKkLlMmNnOoPp 0123456789"},
            {18, "18pt Bold", "The quick brown fox jumps over the lazy dog. AaBbCcDdEeFf 0123456789"},
            {24, "24pt Bold", "Pack my box with five dozen liquor jugs."},
        };

        for (const auto& sample : boldSamples) {
            auto sizeLabel = std::make_shared<UltraCanvasLabel>(
                "BoldSizeLabel" + sample.label, sampleId++, 30, currentY + 2, 70, 20);
            sizeLabel->SetText(sample.label);
            sizeLabel->SetFontSize(10);
            sizeLabel->SetTextColor(Color(150, 150, 150, 255));
            mainContainer->AddChild(sizeLabel);

            auto textLabel = std::make_shared<UltraCanvasLabel>(
                "BoldSample" + sample.label, sampleId++, 110, currentY, 870, static_cast<long>(sample.fontSize * 2.5));
            textLabel->SetText(sample.text);
            textLabel->SetFontSize(sample.fontSize);
            textLabel->SetFontWeight(FontWeight::Bold);
            textLabel->SetWrap(TextWrap::WrapWord);
            mainContainer->AddChild(textLabel);

            sampleLabels.push_back(textLabel);
            sampleLabels.push_back(sizeLabel);

            currentY += static_cast<long>(sample.fontSize * 2.5) + 10;
        }

        // ===== EVENT HANDLERS =====
        auto labelsPtr = std::make_shared<std::vector<std::shared_ptr<UltraCanvasLabel>>>(sampleLabels);

        aaDropdown->onSelectionChanged = [mainContainer](int index, const DropdownItem& item) {
            cairo_antialias_t modes[] = {
                CAIRO_ANTIALIAS_DEFAULT, CAIRO_ANTIALIAS_NONE, CAIRO_ANTIALIAS_GRAY,
                CAIRO_ANTIALIAS_SUBPIXEL, CAIRO_ANTIALIAS_FAST, CAIRO_ANTIALIAS_GOOD,
                CAIRO_ANTIALIAS_BEST
            };
            RenderContextCairo::SetTextAntialias(modes[index]);
            mainContainer->RequestRedraw();
        };

        hsDropdown->onSelectionChanged = [mainContainer](int index, const DropdownItem& item) {
            cairo_hint_style_t styles[] = {
                CAIRO_HINT_STYLE_DEFAULT, CAIRO_HINT_STYLE_NONE, CAIRO_HINT_STYLE_SLIGHT,
                CAIRO_HINT_STYLE_MEDIUM, CAIRO_HINT_STYLE_FULL
            };
            RenderContextCairo::SetTextHintStyle(styles[index]);
            mainContainer->RequestRedraw();
        };

        hmDropdown->onSelectionChanged = [mainContainer](int index, const DropdownItem& item) {
            cairo_hint_metrics_t metrics[] = {
                CAIRO_HINT_METRICS_DEFAULT, CAIRO_HINT_METRICS_OFF, CAIRO_HINT_METRICS_ON
            };
            RenderContextCairo::SetTextHintMetrics(metrics[index]);
            mainContainer->RequestRedraw();
        };

        return mainContainer;
    }

} // namespace UltraCanvas
