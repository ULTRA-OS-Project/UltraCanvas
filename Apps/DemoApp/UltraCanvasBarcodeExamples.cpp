// Apps/DemoApp/UltraCanvasBarcodeExamples.cpp
// Live demos of the UltraCanvasBarcodeElement widget across every
// supported symbology, plus interactive controls for symbology, data,
// module width, bar height, HRI position, rotation, and colors.
//
// Version: 2.2.0 - the shared page look (UltraCanvasDemoPageKit): the editor and
//   every symbology in kit cards, the controls row wraps, no text with a fixed
//   height; a gallery card is as tall as its title and note plus the barcode.
// Version: 2.1.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework
// V2.1.0: Single-column layout — Symbology/Data controls on one row with the live
//   barcode directly beneath, and the gallery as a vertical list (one barcode per
//   row). No horizontal scroll.
// V2.0.0: Migrated from absolute positioning to CSS flex layout, mirroring the
//   helper idiom in UltraCanvasGaugeExamples.cpp.

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Barcode/UltraCanvasBarcodeElement.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasDropdown.h"
#include <vector>
#include <string>

namespace UltraCanvas {

    namespace {

        struct SampleEntry {
            const char* label;
            BarcodeSymbology symbology;
            const char* defaultData;
            const char* note;
        };

        // One canonical sample per symbology. Choices below picked so that
        // the encoder accepts them without further options and the resulting
        // bars look distinct between symbologies.
        const SampleEntry kSamples[] = {
            { "Code 39",          BarcodeSymbology::Code39,         "ULTRACANVAS-1",   "Alphanumeric, mod-43 check" },
            { "Code 39 Extended", BarcodeSymbology::Code39Extended, "Hello, World!",   "Full ASCII via shift sequences" },
            { "Code 93",          BarcodeSymbology::Code93,         "ULTRACANVAS93",   "Denser Code 39 + C/K checks" },
            { "Code 128",         BarcodeSymbology::Code128,        "UltraCanvas-128", "Auto subset A/B/C selection" },
            { "Code 128-A",       BarcodeSymbology::Code128A,       "ULTRA-128A",      "Subset A (uppercase + control)" },
            { "Code 128-B",       BarcodeSymbology::Code128B,       "ultra-128B",      "Subset B (full keyboard)" },
            { "Code 128-C",       BarcodeSymbology::Code128C,       "12345678",        "Subset C (numeric pairs)" },
            { "GS1-128",          BarcodeSymbology::GS1_128,        "(01)04601234567893(17)241231", "FNC1 + AIs (parentheses HRI-only)" },
            { "EAN-13",           BarcodeSymbology::EAN13,          "590123412345",    "12 digits — check digit auto" },
            { "EAN-8",            BarcodeSymbology::EAN8,           "9638507",         "7 digits — check digit auto" },
            { "UPC-A",            BarcodeSymbology::UPCA,           "03600029145",     "11 digits — US retail" },
            { "UPC-E",            BarcodeSymbology::UPCE,           "0123456",         "NS + 5 data — compressed" },
            { "ISBN-13",          BarcodeSymbology::ISBN,           "9780306406157",   "EAN-13 with 978/979 prefix" },
            { "ITF",              BarcodeSymbology::ITF,            "1234567890",      "Interleaved 2 of 5" },
            { "ITF-14",           BarcodeSymbology::ITF14,          "1540014128876",   "GTIN-14 with bearer bars" },
            { "Standard 2 of 5",  BarcodeSymbology::Standard25,     "1234567890",      "Industrial 2 of 5 (Code 25)" },
            { "Codabar",          BarcodeSymbology::Codabar,        "A1234567890B",    "A/B/C/D start/stop chars" },
            { "MSI Plessey",      BarcodeSymbology::MSIPlessey,     "1234567",         "Mod-10 check digit" },
            { "Pharmacode",       BarcodeSymbology::Pharmacode,     "1234",            "Laetus, 3..131070" },
        };

        constexpr int kSampleCount = sizeof(kSamples) / sizeof(kSamples[0]);

        // Each card is a kit card: title + note + the barcode at a set height
        // (autoFitToBounds scales the symbol to the width it receives). The
        // card grows with its texts instead of squeezing the barcode.
        std::shared_ptr<UltraCanvasContainer> BuildCard(const SampleEntry& s) {
            auto card = DemoKit::CreateCard(std::string("BarcodeCard_") + s.label);
            DemoKit::AddHeading(card, std::string("Title_") + s.label, s.label);
            DemoKit::AddCaption(card, std::string("Note_") + s.label, s.note);

            // Built unsized at (0,0) so it stays in flow; the column stretches it
            // across and autoFitToBounds scales the symbol to the cell.
            auto bc = std::make_shared<UltraCanvasBarcodeElement>(
                std::string("Bc_") + s.label, 0L, 0L, 0L, 0L);
            bc->Set(s.symbology, s.defaultData);
            BarcodeStyle st = bc->GetStyle();
            st.moduleWidth = 1.8f;
            st.textFontSize = 10.0f;
            st.textPadding = 2.0f;
            st.quietZoneModules = 8.0f;
            st.autoFitToBounds = true;
            bc->SetStyle(st);
            // Force an immediate encode so an error cross shows for invalid samples.
            bc->Reencode();
            bc->size.height = CSSLayout::Dimension::Px(84);
            DemoKit::Add(card, bc);

            return card;
        }

    } // namespace

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateBarcodeExamples() {
        auto root = DemoKit::CreatePage("BarcodeExamples", "UltraCanvas Barcode Widget — 1D Symbologies",
                "From-scratch pure-C++20 1D encoders, rendered via Cairo.");

        // ===== INTERACTIVE PLAYGROUND =====
        auto playground = DemoKit::AddCard(root, "Playground",
                "Live editor — try any symbology with your own data");

        // --- Controls row (symbology + data + HRI/rotate toggles); wraps when narrow ---
        auto controls = DemoKit::AddRow(playground, "PlayControls", 8);
        controls->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);

        auto symLabel = DemoKit::CreateText("SymLabel", "Symbology:");
        symLabel->SetFontSize(11);
        DemoKit::Add(controls, symLabel);

        auto symDropdown = std::make_shared<UltraCanvasDropdown>("SymDropdown", 0, 0, 200, 28);
        for (const auto& s : kSamples) {
            symDropdown->AddItem(s.label);
        }
        symDropdown->SetSelectedIndex(3); // Code 128 default
        symDropdown->size.width = CSSLayout::Dimension::Px(170);
        DemoKit::AddFixed(controls, symDropdown);

        auto dataLabel = DemoKit::CreateText("DataLabel", "Data:");
        dataLabel->SetFontSize(11);
        DemoKit::Add(controls, dataLabel);

        auto dataInput = std::make_shared<UltraCanvasTextInput>("DataInput", 0, 0, 200, 28);
        dataInput->SetText("UltraCanvas");
        DemoKit::Add(controls, dataInput, 1); // absorbs the horizontal slack

        auto textBtn = std::make_shared<UltraCanvasButton>("TextBtn", 0, 0, 130, 28);
        textBtn->SetText("HRI: Below");
        textBtn->size.width = CSSLayout::Dimension::Px(130);
        DemoKit::AddFixed(controls, textBtn);

        auto rotBtn = std::make_shared<UltraCanvasButton>("RotBtn", 0, 0, 130, 28);
        rotBtn->SetText("Rotate: 0°");
        rotBtn->size.width = CSSLayout::Dimension::Px(130);
        DemoKit::AddFixed(controls, rotBtn);

        // --- Live preview barcode, full-width directly under the controls row ---
        auto liveBarcode = std::make_shared<UltraCanvasBarcodeElement>("LiveBc", 0L, 0L, 0L, 0L);
        liveBarcode->Set(BarcodeSymbology::Code128, "UltraCanvas");
        {
            BarcodeStyle st = liveBarcode->GetStyle();
            st.moduleWidth = 2.4f;
            st.barHeight = 90.0f;
            st.autoFitToBounds = true;
            liveBarcode->SetStyle(st);
        }
        liveBarcode->size.height = CSSLayout::Dimension::Px(140);
        DemoKit::Add(playground, liveBarcode);

        // Error label below the preview (shown when the encoder rejects the data).
        auto liveError = DemoKit::AddCaption(playground, "LiveErr", "");
        liveError->SetTextColor(Color(180, 30, 30, 255));

        liveBarcode->onError = [liveError](const std::string& msg) {
            liveError->SetText(msg);
        };
        liveBarcode->onEncoded = [liveError]() { liveError->SetText(""); };

        // Apply changes on dropdown/input.
        auto applyLive = [liveBarcode, symDropdown, dataInput]() {
            int idx = symDropdown->GetSelectedIndex();
            if (idx >= 0 && idx < kSampleCount) {
                const auto& s = kSamples[idx];
                liveBarcode->Set(s.symbology, dataInput->GetText());
            }
        };
        symDropdown->onSelectionChanged = [applyLive, dataInput](int idx, const DropdownItem&) {
            // When user picks a symbology, also load a known-good sample as the data
            // so the live preview always shows something.
            if (idx >= 0 && idx < kSampleCount) {
                dataInput->SetText(kSamples[idx].defaultData);
            }
            applyLive();
        };
        dataInput->onTextChanged = [applyLive](const std::string&) { applyLive(); };

        // Text position toggle button.
        textBtn->onClick = [liveBarcode, textBtn = textBtn.get()]() {
            static int state = 0;
            state = (state + 1) % 4;
            BarcodeTextPosition pos = BarcodeTextPosition::TextBelow;
            const char* label = "HRI: Below";
            switch (state) {
                case 0: pos = BarcodeTextPosition::TextBelow;          label = "HRI: Below"; break;
                case 1: pos = BarcodeTextPosition::TextAbove;          label = "HRI: Above"; break;
                case 2: pos = BarcodeTextPosition::TextBothSides;  label = "HRI: Both"; break;
                case 3: pos = BarcodeTextPosition::HiddenText;         label = "HRI: Off"; break;
            }
            liveBarcode->SetTextPosition(pos);
            textBtn->SetText(label);
        };

        // Rotation cycler.
        rotBtn->onClick = [liveBarcode, rotBtn = rotBtn.get()]() {
            static int state = 0;
            state = (state + 1) % 4;
            BarcodeRotation r = BarcodeRotation::Rotate0;
            const char* label = "Rotate: 0°";
            switch (state) {
                case 0: r = BarcodeRotation::Rotate0;   label = "Rotate: 0°";   break;
                case 1: r = BarcodeRotation::Rotate90;  label = "Rotate: 90°";  break;
                case 2: r = BarcodeRotation::Rotate180; label = "Rotate: 180°"; break;
                case 3: r = BarcodeRotation::Rotate270; label = "Rotate: 270°"; break;
            }
            liveBarcode->SetRotation(r);
            rotBtn->SetText(label);
        };

        // ===== GALLERY (one card per symbology) =====
        DemoKit::AddHeading(root, "GalleryTitle", "All symbologies — same widget, different encoders");

        // Single column: each barcode card is full-width, stacked one under another.
        auto gallery = DemoKit::CreateColumn("Gallery", DemoKit::kPageGap);
        DemoKit::Add(root, gallery);

        for (int i = 0; i < kSampleCount; ++i) {
            DemoKit::Add(gallery, BuildCard(kSamples[i]));
        }

        return root;
    }

} // namespace UltraCanvas
