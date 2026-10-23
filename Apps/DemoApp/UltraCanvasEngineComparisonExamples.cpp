// Apps/DemoApp/UltraCanvasEngineComparisonExamples.cpp
// The "Chart Engine Comparison" page: every chart converted to the chart
// engine side by side with the legacy chart it is meant to replace, on the
// same data. One tab row per chart family, one tab per chart; the family files
// (UltraCanvasEngineComparison*.cpp) build the tabs.
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): title and
//                  description above the family tabs, which fill the rest of the
//                  area in a card; captions, side headings and strip labels
//                  sized to their text and wrapped
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasEngineComparison.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include "CSSLayout/CSSLayout.h"

namespace UltraCanvas {
namespace EngineComparison {

namespace {

// The caption over a comparison: wraps, as tall as its lines (the tab is a
// stretching column, so it gets the tab's width).
std::shared_ptr<UltraCanvasLabel> MakeCaption(const std::string& id, const std::string& text) {
    auto caption = DemoKit::CreateText(id, text);
    caption->SetTextColor(Color(90, 90, 100, 255));
    caption->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return caption;
}

// A heading over one side of the comparison.
std::shared_ptr<UltraCanvasLabel> MakeSideHeading(const std::string& id, const std::string& text,
                                                  const Color& background) {
    auto heading = DemoKit::CreateText(id, text);
    heading->SetFontSize(11);
    heading->SetFontWeight(FontWeight::Bold);
    heading->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
    heading->SetBackgroundColor(background);
    heading->SetPadding(3, 4);
    heading->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return heading;
}

std::shared_ptr<UltraCanvasContainer> MakeSide(const std::string& id, const std::string& heading,
                                               const Color& background,
                                               std::shared_ptr<UltraCanvasUIElement> chart) {
    auto side = std::make_shared<UltraCanvasContainer>(id);
    side->layout.SetFlexColumn().SetFlexGap(2)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    side->layoutItem.SetFlex(1, 1, CSSLayout::Dimension::Px(0))
                    .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    side->AddChild(MakeSideHeading(id + "Heading", heading, background));
    chart->layoutItem.SetFlex(1, 1, CSSLayout::Dimension::Px(0))
                     .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    side->AddChild(chart);
    return side;
}

std::shared_ptr<UltraCanvasContainer> MakeFamilyTabs(const std::string& id,
                                                     const std::vector<ComparisonTab>& tabs) {
    auto page = std::make_shared<UltraCanvasContainer>(id + "Page");
    page->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    auto inner = std::make_shared<UltraCanvasTabbedContainer>(id + "Tabs", 0, 0, 0, 0);
    inner->SetTabPosition(TabPosition::Top);
    inner->SetTabStyle(TabStyle::Classic);
    inner->layoutItem.SetFlex(1, 1, CSSLayout::Dimension::Px(0))
                     .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    for (const ComparisonTab& tab : tabs) inner->AddTab(tab.title, tab.build());
    page->AddChild(inner);
    return page;
}

} // namespace

std::shared_ptr<UltraCanvasContainer> MakeComparisonTab(
    const std::string& idPrefix, const std::string& caption,
    const std::string& legacyClass, std::shared_ptr<UltraCanvasUIElement> legacy,
    const std::string& engineClass, std::shared_ptr<UltraCanvasUIElement> engine,
    std::shared_ptr<UltraCanvasUIElement> controls) {
    auto tab = std::make_shared<UltraCanvasContainer>(idPrefix + "Tab");
    tab->SetPadding(6);
    tab->layout.SetFlexColumn().SetFlexGap(4)
               .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    tab->AddChild(MakeCaption(idPrefix + "Caption", caption));

    auto row = std::make_shared<UltraCanvasContainer>(idPrefix + "Row");
    row->layout.SetFlexRow().SetFlexGap(10)
               .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    row->layoutItem.SetFlex(1, 1, CSSLayout::Dimension::Px(0))
                   .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    row->AddChild(MakeSide(idPrefix + "Legacy", "Legacy: " + legacyClass,
                           Color(246, 238, 226, 255), legacy));
    row->AddChild(MakeSide(idPrefix + "Engine", "Chart engine: " + engineClass,
                           Color(226, 238, 248, 255), engine));
    tab->AddChild(row);

    if (controls) {
        controls->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        tab->AddChild(controls);
    }
    return tab;
}

std::shared_ptr<UltraCanvasContainer> MakeControlStrip(const std::string& idPrefix) {
    // Wraps onto further rows when the window is narrower than the
    // controls, and is as tall as those rows: a fixed height cut the third.
    auto strip = std::make_shared<UltraCanvasContainer>(idPrefix + "Controls");
    strip->layout.SetFlexRow().SetFlexGap(10, 4)
                 .SetFlexWrap(CSSLayout::FlexWrap::Wrap)
                 .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    strip->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return strip;
}

void AddToStrip(UltraCanvasContainer& strip, std::shared_ptr<UltraCanvasUIElement> element) {
    element->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    strip.AddChild(element);
}

void AddStripLabel(UltraCanvasContainer& strip, const std::string& id, const std::string& text,
                   int /*width*/) {
    // The callers' widths ran short of the text ("Projecti..."), and a
    // translation is longer still: the label is as wide as its text, and
    // the width the callers pass is no longer used.
    auto label = DemoKit::CreateText(id, text);
    label->SetFontSize(11);
    label->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
    AddToStrip(strip, label);
}

} // namespace EngineComparison

// =============================================================================
// PAGE
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateChartEngineComparisonExamples() {
    using namespace EngineComparison;

    auto container = DemoKit::CreatePage("EngineCompareContainer",
        "Chart Engine Comparison - Legacy Charts and Their Engine Ports",
        "Each tab shows a legacy chart (left) and its port onto UltraCanvasChartEngineElement (right) on the same data. The ports get the engine's axes, "
        "grid, measured layout, legend, solved label plan, themes, hover tooltips and entrance animation for free - compare them here before the switch.");
    // The charts take the area's height: the page is the area's height and
    // the family tabs' card takes what the title and description leave.
    container->SetFillsArea(true);

    auto card = DemoKit::CreateCard("EngineCompareCard");
    card->SetPadding(4);
    DemoKit::Add(container, card, 1);

    auto families = std::make_shared<UltraCanvasTabbedContainer>("EngineCompareFamilies", 0, 0, 0, 0);
    families->SetTabPosition(TabPosition::Top);
    families->SetTabStyle(TabStyle::Modern);
    families->AddTab("Lines & Points", MakeFamilyTabs("EngineCompareLines", LineFamilyTabs()));
    families->AddTab("Bars & Flows", MakeFamilyTabs("EngineCompareBars", BarFamilyTabs()));
    families->AddTab("Finance & Mosaics", MakeFamilyTabs("EngineCompareFinance", FinanceAndGridTabs()));
    families->AddTab("Polar", MakeFamilyTabs("EngineComparePolar", PolarFamilyTabs()));
    // From a zero basis it takes exactly the card's height.
    DemoKit::Add(card, families, 1);
    families->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0)).SetFlexShrink(1);
    return container;
}

} // namespace UltraCanvas
