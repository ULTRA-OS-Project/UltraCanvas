// Apps/DemoApp/UltraCanvasBarcodeExamples.cpp
// Live demos of the UltraCanvasBarcodeElement widget across every
// supported symbology, plus interactive controls for symbology, data,
// module width, bar height, HRI position, rotation, and colors.
//
// Version: 1.0.0
// Last Modified: 2026-05-19
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Barcode/UltraCanvasBarcodeElement.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasSlider.h"
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

        // Each card lays out a label + the barcode element. Returns the card
        // container, the embedded barcode (for later tweaking), and the
        // bottom edge so the parent can stack cards.
        struct BarcodeCard {
            std::shared_ptr<UltraCanvasContainer> card;
            std::shared_ptr<UltraCanvasBarcodeElement> barcode;
        };

        BarcodeCard BuildCard(int x, int y, int w, int h,
                              const SampleEntry& s, int& outBottom) {
            BarcodeCard out;
            out.card = std::make_shared<UltraCanvasContainer>(
                std::string("BarcodeCard_") + s.label, x, y, w, h);
            out.card->SetBackgroundColor(Color(252, 252, 252, 255));
            out.card->SetBorders(1, Color(220, 220, 220, 255), 4, UCDashPattern());
            out.card->SetPadding(10);

            auto title = std::make_shared<UltraCanvasLabel>(
                std::string("Title_") + s.label, 12, 6, w - 24, 20);
            title->SetText(s.label);
            title->SetFontSize(13);
            title->SetFontWeight(FontWeight::Bold);
            title->SetTextColor(Color(40, 40, 60, 255));
            out.card->AddChild(title);

            auto note = std::make_shared<UltraCanvasLabel>(
                std::string("Note_") + s.label, 12, 28, w - 24, 16);
            note->SetText(s.note);
            note->SetFontSize(10);
            note->SetTextColor(Color(110, 110, 110, 255));
            out.card->AddChild(note);

            const int barcodeY = 50;
            const int barcodeH = h - barcodeY - 8;
            out.barcode = CreateBarcode(std::string("Bc_") + s.label,
                                         12, barcodeY, w - 24, barcodeH,
                                         s.symbology, s.defaultData);
            BarcodeStyle st = out.barcode->GetStyle();
            st.moduleWidth = 1.8f;
            st.barHeight = (float)(barcodeH - 24);
            st.textFontSize = 10.0f;
            st.textPadding = 2.0f;
            st.quietZoneModules = 8.0f;
            st.autoFitToBounds = true;
            out.barcode->SetStyle(st);
            // Force an immediate encode so an error label can be shown for invalid samples.
            out.barcode->Reencode();
            out.card->AddChild(out.barcode);

            outBottom = y + h;
            return out;
        }

    } // namespace

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateBarcodeExamples() {
        const int pageW = 1100;
        const int pageH = 4000;
        auto root = std::make_shared<UltraCanvasContainer>(
            "BarcodeExamples", 0, 0, pageW, pageH);
        root->SetBackgroundColor(Color(244, 244, 248, 255));

        // ===== HEADER =====
        auto title = std::make_shared<UltraCanvasLabel>("BarcodeTitle", 20, 16, pageW - 40, 30);
        title->SetText("UltraCanvas Barcode Widget — 1D Symbologies");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(40, 40, 80, 255));
        root->AddChild(title);

        auto subtitle = std::make_shared<UltraCanvasLabel>("BarcodeSub", 20, 48, pageW - 40, 40);
        subtitle->SetText("From-scratch encoders for 12 symbologies (Code 39 / 93 / 128, GS1-128, "
                          "EAN-13/8, UPC-A/E, ISBN, ITF, ITF-14, Standard 2 of 5, Codabar, MSI, "
                          "Pharmacode). Pure C++20, rendered via Cairo IRenderContext.");
        subtitle->SetWrap(TextWrap::WrapWord);
        subtitle->SetFontSize(11);
        subtitle->SetTextColor(Color(80, 80, 100, 255));
        root->AddChild(subtitle);

        // ===== INTERACTIVE PLAYGROUND =====
        const int playgroundY = 100;
        const int playgroundH = 240;
        auto playground = std::make_shared<UltraCanvasContainer>(
            "Playground", 20, playgroundY, pageW - 40, playgroundH);
        playground->SetBackgroundColor(Colors::White);
        playground->SetBorders(1, Color(210, 210, 210, 255), 6, UCDashPattern());
        playground->SetPadding(12);
        root->AddChild(playground);

        auto playTitle = std::make_shared<UltraCanvasLabel>("PlayTitle", 14, 8, 600, 22);
        playTitle->SetText("Live editor — try any symbology with your own data");
        playTitle->SetFontSize(13);
        playTitle->SetFontWeight(FontWeight::Bold);
        playground->AddChild(playTitle);

        auto liveBarcode = CreateBarcode("LiveBc", 20, 90, 700, 130,
                                          BarcodeSymbology::Code128, "UltraCanvas");
        {
            BarcodeStyle st = liveBarcode->GetStyle();
            st.moduleWidth = 2.4f;
            st.barHeight = 90.0f;
            st.autoFitToBounds = true;
            liveBarcode->SetStyle(st);
        }
        playground->AddChild(liveBarcode);

        // Error label (shown when encoder rejects the data).
        auto liveError = std::make_shared<UltraCanvasLabel>("LiveErr", 740, 90, 280, 24);
        liveError->SetText("");
        liveError->SetWrap(TextWrap::WrapWord);
        liveError->SetFontSize(10);
        liveError->SetTextColor(Color(180, 30, 30, 255));
        playground->AddChild(liveError);

        liveBarcode->onError = [liveError](const std::string& msg) {
            liveError->SetText(msg);
        };
        liveBarcode->onEncoded = [liveError]() { liveError->SetText(""); };

        // Symbology dropdown.
        auto symLabel = std::make_shared<UltraCanvasLabel>("SymLabel", 14, 40, 80, 22);
        symLabel->SetText("Symbology:");
        symLabel->SetFontSize(11);
        playground->AddChild(symLabel);

        auto symDropdown = std::make_shared<UltraCanvasDropdown>("SymDropdown", 100, 38, 200, 28);
        for (const auto& s : kSamples) {
            symDropdown->AddItem(s.label);
        }
        symDropdown->SetSelectedIndex(3); // Code 128 default
        playground->AddChild(symDropdown);

        // Data input.
        auto dataLabel = std::make_shared<UltraCanvasLabel>("DataLabel", 320, 40, 60, 22);
        dataLabel->SetText("Data:");
        dataLabel->SetFontSize(11);
        playground->AddChild(dataLabel);

        auto dataInput = std::make_shared<UltraCanvasTextInput>("DataInput", 370, 38, 360, 28);
        dataInput->SetText("UltraCanvas");
        playground->AddChild(dataInput);

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
        auto textBtn = std::make_shared<UltraCanvasButton>("TextBtn", 740, 38, 130, 28);
        textBtn->SetText("HRI: Below");
        playground->AddChild(textBtn);
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
        auto rotBtn = std::make_shared<UltraCanvasButton>("RotBtn", 880, 38, 130, 28);
        rotBtn->SetText("Rotate: 0°");
        playground->AddChild(rotBtn);
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
        const int galleryTopY = playgroundY + playgroundH + 24;
        auto galleryTitle = std::make_shared<UltraCanvasLabel>(
            "GalleryTitle", 20, galleryTopY, pageW - 40, 26);
        galleryTitle->SetText("All symbologies — same widget, different encoders");
        galleryTitle->SetFontSize(14);
        galleryTitle->SetFontWeight(FontWeight::Bold);
        galleryTitle->SetTextColor(Color(60, 60, 80, 255));
        root->AddChild(galleryTitle);

        const int cardW = (pageW - 60) / 2;
        const int cardH = 160;
        const int cardGap = 12;
        int cursorY = galleryTopY + 36;

        for (int i = 0; i < kSampleCount; ++i) {
            int col = i % 2;
            int row = i / 2;
            int x = 20 + col * (cardW + cardGap);
            int y = cursorY + row * (cardH + cardGap);
            int bottom = 0;
            auto built = BuildCard(x, y, cardW, cardH, kSamples[i], bottom);
            root->AddChild(built.card);
        }

        // Final height = last row bottom + margin.
        int rows = (kSampleCount + 1) / 2;
        int finalH = cursorY + rows * (cardH + cardGap) + 20;
        root->SetSize(pageW, finalH);

        return root;
    }

} // namespace UltraCanvas
