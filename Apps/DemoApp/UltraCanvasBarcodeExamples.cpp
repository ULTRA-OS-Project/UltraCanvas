// Apps/DemoApp/UltraCanvasBarcodeExamples.cpp
// Live demos of the UltraCanvasBarcodeElement widget across every
// supported symbology, plus interactive controls for symbology, data,
// module width, bar height, HRI position, rotation, and colors.
//
// Version: 2.1.0
// Last Modified: 2026-06-07
// Author: UltraCanvas Framework
// V2.1.0: Single-column layout — Symbology/Data controls on one row with the live
//   barcode directly beneath, and the gallery as a vertical list (one barcode per
//   row). No horizontal scroll.
// V2.0.0: Migrated from absolute positioning to CSS flex layout, mirroring the
//   helper idiom in UltraCanvasGaugeExamples.cpp.

#include "UltraCanvasDemo.h"
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

        // ===== CSS-layout helpers (mirror UltraCanvasGaugeExamples.cpp) =====
        inline void SetVBox(const std::shared_ptr<UltraCanvasContainer>& c, float gap) {
            c->layout.SetFlexColumn().SetFlexGap(gap)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        }
        inline void SetHBox(const std::shared_ptr<UltraCanvasContainer>& c, float gap) {
            c->layout.SetFlexRow().SetFlexGap(gap)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        }
        // Add a flex child with the given grow factor, stretched on the cross axis.
        inline void AddFlex(const std::shared_ptr<UltraCanvasContainer>& parent,
                            const std::shared_ptr<UltraCanvasUIElement>& child, float grow) {
            child->layoutItem.SetFlexGrow(grow).SetFlexShrink(grow > 0 ? 1.f : 0.f)
                             .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
            parent->AddChild(child);
        }
        // Each card is a flex column: title + note + the barcode (which grows to
        // fill the remaining cell height; autoFitToBounds scales the symbol).
        // Built entirely at (0,0) so the flex column — not AbsoluteUI — positions it.
        std::shared_ptr<UltraCanvasContainer> BuildCard(const SampleEntry& s) {
            auto card = std::make_shared<UltraCanvasContainer>(
                std::string("BarcodeCard_") + s.label, 0, 0, 0, 0);
            card->SetBackgroundColor(Color(252, 252, 252, 255));
            card->SetBorders(1, Color(220, 220, 220, 255), 4, UCDashPattern());
            card->SetPadding(10);
            SetVBox(card, 6);

            auto title = std::make_shared<UltraCanvasLabel>(
                std::string("Title_") + s.label, 0, 0, 0, 20);
            title->SetText(s.label);
            title->SetFontSize(13);
            title->SetFontWeight(FontWeight::Bold);
            title->SetTextColor(Color(40, 40, 60, 255));
            AddFlex(card, title, 0);

            auto note = std::make_shared<UltraCanvasLabel>(
                std::string("Note_") + s.label, 0, 0, 0, 16);
            note->SetText(s.note);
            note->SetFontSize(10);
            note->SetTextColor(Color(110, 110, 110, 255));
            AddFlex(card, note, 0);

            // Built unsized at (0,0) so it stays in flow; the flex column grows it
            // and autoFitToBounds scales the symbol to whatever cell it receives.
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
            AddFlex(card, bc, 1);

            return card;
        }

    } // namespace

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateBarcodeExamples() {
        // Flex-column root; no fixed page size. DisplayDemoItem stretches it to the
        // display area and grows it; the host displayContainer scrolls when tall.
        auto root = std::make_shared<UltraCanvasContainer>("BarcodeExamples", 0, 0, 0, 0);
        root->SetBackgroundColor(Color(244, 244, 248, 255));
        root->SetPadding(20);
        SetVBox(root, 16);

        // ===== HEADER =====
        auto title = std::make_shared<UltraCanvasLabel>("BarcodeTitle", 0, 0, 0, 30);
        title->SetText("UltraCanvas Barcode Widget — 1D Symbologies");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(40, 40, 80, 255));
        AddFlex(root, title, 0);

        auto subtitle = std::make_shared<UltraCanvasLabel>("BarcodeSub", 0, 0, 0, 20);
        subtitle->SetText("From-scratch pure-C++20 1D encoders, rendered via Cairo.");
        subtitle->SetWrap(TextWrap::WrapWord);
        subtitle->SetFontSize(11);
        subtitle->SetTextColor(Color(80, 80, 100, 255));
        AddFlex(root, subtitle, 0);

        // ===== INTERACTIVE PLAYGROUND =====
        auto playground = std::make_shared<UltraCanvasContainer>("Playground", 0, 0, 0, 0);
        playground->SetBackgroundColor(Colors::White);
        playground->SetBorders(1, Color(210, 210, 210, 255), 6, UCDashPattern());
        playground->SetPadding(12);
        SetVBox(playground, 10);
        AddFlex(root, playground, 0);

        auto playTitle = std::make_shared<UltraCanvasLabel>("PlayTitle", 0, 0, 0, 22);
        playTitle->SetText("Live editor — try any symbology with your own data");
        playTitle->SetFontSize(13);
        playTitle->SetFontWeight(FontWeight::Bold);
        AddFlex(playground, playTitle, 0);

        // --- Controls row (symbology + data + HRI/rotate toggles) ---
        auto controls = std::make_shared<UltraCanvasContainer>("PlayControls", 0, 0, 0, 0);
        SetHBox(controls, 8);
        controls->size.height = CSSLayout::Dimension::Px(30);
        AddFlex(playground, controls, 0);

        auto symLabel = std::make_shared<UltraCanvasLabel>("SymLabel");
        symLabel->SetText("Symbology:");
        symLabel->SetFontSize(11);
        symLabel->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
        AddFlex(controls, symLabel, 0);

        auto symDropdown = std::make_shared<UltraCanvasDropdown>("SymDropdown", 0, 0, 200, 28);
        for (const auto& s : kSamples) {
            symDropdown->AddItem(s.label);
        }
        symDropdown->SetSelectedIndex(3); // Code 128 default
        symDropdown->size.width = CSSLayout::Dimension::Px(170);
        AddFlex(controls, symDropdown, 0);

        auto dataLabel = std::make_shared<UltraCanvasLabel>("DataLabel");
        dataLabel->SetText("Data:");
        dataLabel->SetFontSize(11);
        dataLabel->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
        AddFlex(controls, dataLabel, 0);

        auto dataInput = std::make_shared<UltraCanvasTextInput>("DataInput", 0, 0, 200, 28);
        dataInput->SetText("UltraCanvas");
        AddFlex(controls, dataInput, 1); // absorbs the horizontal slack

        auto textBtn = std::make_shared<UltraCanvasButton>("TextBtn", 0, 0, 130, 28);
        textBtn->SetText("HRI: Below");
        textBtn->size.width = CSSLayout::Dimension::Px(130);
        AddFlex(controls, textBtn, 0);

        auto rotBtn = std::make_shared<UltraCanvasButton>("RotBtn", 0, 0, 130, 28);
        rotBtn->SetText("Rotate: 0°");
        rotBtn->size.width = CSSLayout::Dimension::Px(130);
        AddFlex(controls, rotBtn, 0);

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
        AddFlex(playground, liveBarcode, 0);

        // Error label below the preview (shown when the encoder rejects the data).
        auto liveError = std::make_shared<UltraCanvasLabel>("LiveErr", 0, 0, 0, 20);
        liveError->SetText("");
        liveError->SetWrap(TextWrap::WrapWord);
        liveError->SetFontSize(10);
        liveError->SetTextColor(Color(180, 30, 30, 255));
        AddFlex(playground, liveError, 0);

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
        textBtn->onClick = [liveBarcode, textBtn]() {
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
        rotBtn->onClick = [liveBarcode, rotBtn]() {
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
        auto galleryTitle = std::make_shared<UltraCanvasLabel>("GalleryTitle", 0, 0, 0, 26);
        galleryTitle->SetText("All symbologies — same widget, different encoders");
        galleryTitle->SetFontSize(14);
        galleryTitle->SetFontWeight(FontWeight::Bold);
        galleryTitle->SetTextColor(Color(60, 60, 80, 255));
        AddFlex(root, galleryTitle, 0);

        // Single flex column: each barcode card is full-width, stacked one under another.
        auto gallery = std::make_shared<UltraCanvasContainer>("Gallery", 0, 0, 0, 0);
        SetVBox(gallery, 12);
        AddFlex(root, gallery, 0);

        for (int i = 0; i < kSampleCount; ++i) {
            auto card = BuildCard(kSamples[i]);
            card->size.height = CSSLayout::Dimension::Px(150); // uniform card height
            AddFlex(gallery, card, 0);
        }

        return root;
    }

} // namespace UltraCanvas
