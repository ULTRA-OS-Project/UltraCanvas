// Apps/DemoApp/UltraCanvasRatingExamples.cpp
// Demonstration of UltraCanvasRating: built-in star/circle/square symbols,
// half-step ratings, a read-only display, and a custom rating rendered from
// vector (SVG) files for its on / off / half states.
// Version: 1.0.0
// Last Modified: 2026-07-07
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
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
        auto container = std::make_shared<UltraCanvasContainer>("RatingExamples", 0, 0, 1000, 640);
        container->SetPadding(0, 0, 10, 0);

        auto title = CreateLabel("RatingTitle", 20, 10, 0, 30);
        title->SetText("Rating Examples");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto subtitle = CreateLabel("RatingSubtitle", 20, 42, 940, 40);
        subtitle->SetText("Click to set a score, hover to preview. Half ratings, alternate symbols, a "
                          "read-only display, and a custom rating drawn from SVG files (on / off / half).");
        subtitle->SetFontSize(12);
        container->AddChild(subtitle);

        const int labelX = 360;
        int y = 100;

        auto readout = [&](const std::string& id, const std::string& initial) {
            auto l = CreateLabel(id, labelX, y + 8, 260, 22);
            l->SetText(initial);
            l->SetBackgroundColor(Color(240, 240, 240));
            l->SetPadding(3);
            container->AddChild(l);
            return l;
        };
        auto section = [&](const std::string& id, const std::string& text) {
            auto l = CreateLabel(id, 20, y - 22, 320, 20);
            l->SetText(text);
            l->SetFontWeight(FontWeight::Bold);
            container->AddChild(l);
        };

        // ===== 1. STARS (whole) =====
        section("RtL1", "Stars (whole steps):");
        auto r1 = CreateRating("Rt1", 20, y, 200, 36, 5, 3);
        r1->SetSymbolSize(32);
        container->AddChild(r1);
        auto v1 = readout("RtV1", "Rating: 3");
        r1->onRatingChanged = [v1](float v) { v1->SetText("Rating: " + std::to_string((int)v)); };
        y += 64;

        // ===== 2. STARS (half) =====
        section("RtL2", "Stars (half steps):");
        auto r2 = CreateHalfRating("Rt2", 20, y, 200, 36, 5, 3.5f);
        r2->SetSymbolSize(32);
        container->AddChild(r2);
        auto v2 = readout("RtV2", "Rating: 3.5");
        r2->onRatingChanged = [v2](float v) { v2->SetText("Rating: " + FormatScore(v)); };
        y += 64;

        // ===== 3. ALTERNATE SYMBOLS =====
        section("RtL3", "Circle and Square symbols:");
        auto r3 = CreateHalfRating("Rt3", 20, y, 200, 36, 5, 4);
        r3->SetSymbol(RatingSymbol::Circle);
        r3->SetSymbolSize(30);
        r3->SetColors(Color(0, 150, 136), Color(214, 214, 220));   // teal
        container->AddChild(r3);

        auto r3b = CreateHalfRating("Rt3b", 230, y, 200, 36, 5, 2);
        r3b->SetSymbol(RatingSymbol::Square);
        r3b->SetSymbolSize(28);
        r3b->SetColors(Color(103, 58, 183), Color(214, 214, 220)); // purple
        container->AddChild(r3b);
        y += 64;

        // ===== 4. READ-ONLY DISPLAY =====
        section("RtL4", "Read-only display (3.5 / 5):");
        auto r4 = CreateHalfRating("Rt4", 20, y, 200, 36, 5, 3.5f);
        r4->SetSymbolSize(28);
        r4->SetReadOnly(true);
        container->AddChild(r4);
        y += 64;

        // ===== 5. CUSTOM VECTOR (SVG) SYMBOLS =====
        section("RtL5", "Custom vector hearts (SVG on / off / half):");
        auto r5 = CreateHalfRating("Rt5", 20, y, 240, 40, 5, 2.5f);
        r5->SetSymbolSize(34);
        const std::string iconDir = NormalizePath(GetResourcesDir() + "media/icons/");
        r5->SetCustomSymbols(iconDir + "rating-heart-on.svg",
                             iconDir + "rating-heart-off.svg",
                             iconDir + "rating-heart-half.svg");
        r5->SetAllowHalf(true);
        container->AddChild(r5);
        auto v5 = readout("RtV5", "Rating: 2.5");
        r5->onRatingChanged = [v5](float v) { v5->SetText("Rating: " + FormatScore(v)); };
        y += 70;

        // ===== INSTRUCTIONS =====
        auto instructions = CreateLabel("RatingInstructions", 20, y, 940, 96);
        instructions->SetText(
                "API:\n"
                "\xE2\x80\xA2 CreateRating / CreateHalfRating(id, x, y, w, h, maxRating, value)\n"
                "\xE2\x80\xA2 SetSymbol(Star | Circle | Square); SetColors(on, off); SetReadOnly(bool)\n"
                "\xE2\x80\xA2 SetCustomSymbols(onPath, offPath, halfPath) - vector/image files per state\n"
                "  (if no half file is given, half is synthesised by clipping 'on' over 'off')\n"
                "\xE2\x80\xA2 Keyboard: Left/Right adjust, Home/End clear/max; onRatingChanged / onHoverChanged");
        instructions->SetFontSize(11);
        instructions->SetBackgroundColor(Color(255, 255, 240));
        instructions->SetPadding(6);
        container->AddChild(instructions);

        return container;
    }

} // namespace UltraCanvas
