// Apps/DemoApp/UltraCanvasEngineComparison.h
// Shared pieces of the demo's "Chart Engine Comparison" page: every chart that
// has an engine port is shown next to the legacy chart it will replace, on the
// same data, so the two can be compared before the switch.
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasContainer.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace EngineComparison {

// One chart's comparison: the tab title and the function that builds it.
struct ComparisonTab {
    std::string title;
    std::function<std::shared_ptr<UltraCanvasUIElement>()> build;
};

// A comparison tab: the caption, then the legacy chart on the left and its
// engine port on the right, each under a heading naming its class, and an
// optional control strip (MakeControlStrip) driving both underneath.
std::shared_ptr<UltraCanvasContainer> MakeComparisonTab(
    const std::string& idPrefix, const std::string& caption,
    const std::string& legacyClass, std::shared_ptr<UltraCanvasUIElement> legacy,
    const std::string& engineClass, std::shared_ptr<UltraCanvasUIElement> engine,
    std::shared_ptr<UltraCanvasUIElement> controls = nullptr);

// A one-row strip for a tab's controls; add elements with AddToStrip.
std::shared_ptr<UltraCanvasContainer> MakeControlStrip(const std::string& idPrefix);
// Adds an element at its natural size (no flex growth) to a strip.
void AddToStrip(UltraCanvasContainer& strip, std::shared_ptr<UltraCanvasUIElement> element);
// A small text label for a strip.
void AddStripLabel(UltraCanvasContainer& strip, const std::string& id, const std::string& text,
                   int width = 80);

// The tabs of each family, one file each.
std::vector<ComparisonTab> LineFamilyTabs();      // line, area, cumulative flow, scatter, bubble
std::vector<ComparisonTab> BarFamilyTabs();       // waterfall, diverging, population, dumbbell
std::vector<ComparisonTab> FinanceAndGridTabs();  // financial, heatmap, Mekko
std::vector<ComparisonTab> PolarFamilyTabs();     // radar, radial bar, pie, polar

} // namespace EngineComparison
} // namespace UltraCanvas
