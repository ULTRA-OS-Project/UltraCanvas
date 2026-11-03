// Apps/DemoApp/UltraCanvasRatingExamples.cpp
// Demonstration of UltraCanvasRating: built-in star/circle/square symbols,
// half-step ratings, a read-only display, and a custom rating rendered from
// vector (SVG) files for its on / off / half states.
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): one card per
//                  example, readouts and notes sized to their text
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasRating.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasConfig.h"   // GetResourcesDir
#include "UltraCanvasUtils.h"    // NormalizePath
#include <sstream>
#include <iomanip>

namespace UltraCanvas {

    static std::string FormatScore(float v) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << v;
        return oss.str();
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateRatingExamples() {
        auto page = DemoKit::CreatePage("RatingExamples", "Rating Examples",
                "Click to set a score, hover to preview. Half ratings, alternate symbols, a "
                "read-only display, and a custom rating drawn from SVG files (on / off / half).");

        // A rating beside its live readout; the readout is as wide as its text.
        auto readout = [](const std::shared_ptr<UltraCanvasContainer>& row,
                          const std::string& id, const std::string& initial) {
            auto l = DemoKit::CreateText(id, initial);
            l->SetBackgroundColor(Color(240, 240, 240));
            l->SetPadding(3);
            l->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
            DemoKit::Add(row, l);
            return l;
        };

        auto cards = DemoKit::AddRow(page, "RatingCards");

        // ===== 1. STARS (whole) =====
        auto card1 = DemoKit::AddCard(cards, "RtL1", "Stars (whole steps):");
        auto row1 = DemoKit::AddRow(card1, "RtRow1");
        auto r1 = CreateRating("Rt1", 0, 0, 200, 36, 5, 3);
        r1->SetSymbolSize(32);
        DemoKit::AddFixed(row1, r1);
        auto v1 = readout(row1, "RtV1", "Rating: 3");
        r1->onRatingChanged = [v1](float v) { v1->SetText("Rating: " + std::to_string((int)v)); };

        // ===== 2. STARS (half) =====
        auto card2 = DemoKit::AddCard(cards, "RtL2", "Stars (half steps):");
        auto row2 = DemoKit::AddRow(card2, "RtRow2");
        auto r2 = CreateHalfRating("Rt2", 0, 0, 200, 36, 5, 3.5f);
        r2->SetSymbolSize(32);
        DemoKit::AddFixed(row2, r2);
        auto v2 = readout(row2, "RtV2", "Rating: 3.5");
        r2->onRatingChanged = [v2](float v) { v2->SetText("Rating: " + FormatScore(v)); };

        // ===== 3. ALTERNATE SYMBOLS =====
        auto card3 = DemoKit::AddCard(cards, "RtL3", "Circle and Square symbols:");
        auto row3 = DemoKit::AddRow(card3, "RtRow3");
        auto r3 = CreateHalfRating("Rt3", 0, 0, 200, 36, 5, 4);
        r3->SetSymbol(RatingSymbol::Circle);
        r3->SetSymbolSize(30);
        r3->SetColors(Color(0, 150, 136), Color(214, 214, 220));   // teal
        DemoKit::AddFixed(row3, r3);

        auto r3b = CreateHalfRating("Rt3b", 0, 0, 200, 36, 5, 2);
        r3b->SetSymbol(RatingSymbol::Square);
        r3b->SetSymbolSize(28);
        r3b->SetColors(Color(103, 58, 183), Color(214, 214, 220)); // purple
        DemoKit::AddFixed(row3, r3b);

        // ===== 4. READ-ONLY DISPLAY =====
        auto card4 = DemoKit::AddCard(cards, "RtL4", "Read-only display (3.5 / 5):");
        auto r4 = CreateHalfRating("Rt4", 0, 0, 200, 36, 5, 3.5f);
        r4->SetSymbolSize(28);
        r4->SetReadOnly(true);
        DemoKit::AddFixed(card4, r4);

        // ===== 5. CUSTOM VECTOR (SVG) SYMBOLS =====
        auto card5 = DemoKit::AddCard(cards, "RtL5", "Custom vector hearts (SVG on / off / half):");
        auto row5 = DemoKit::AddRow(card5, "RtRow5");
        auto r5 = CreateHalfRating("Rt5", 0, 0, 240, 40, 5, 2.5f);
        r5->SetSymbolSize(34);
        const std::string iconDir = NormalizePath(GetResourcesDir() + "media/icons/");
        r5->SetCustomSymbols(iconDir + "rating-heart-on.svg",
                             iconDir + "rating-heart-off.svg",
                             iconDir + "rating-heart-half.svg");
        r5->SetAllowHalf(true);
        DemoKit::AddFixed(row5, r5);
        auto v5 = readout(row5, "RtV5", "Rating: 2.5");
        r5->onRatingChanged = [v5](float v) { v5->SetText("Rating: " + FormatScore(v)); };

        // ===== INSTRUCTIONS =====
        DemoKit::AddNotes(page, "RatingInstructions", "API", {
                "CreateRating / CreateHalfRating(id, x, y, w, h, maxRating, value)",
                "SetSymbol(Star | Circle | Square); SetColors(on, off); SetReadOnly(bool)",
                "SetCustomSymbols(onPath, offPath, halfPath) - vector/image files per state "
                "(if no half file is given, half is synthesised by clipping 'on' over 'off')",
                "Keyboard: Left/Right adjust, Home/End clear/max; onRatingChanged / onHoverChanged"});

        return page;
    }

} // namespace UltraCanvas
