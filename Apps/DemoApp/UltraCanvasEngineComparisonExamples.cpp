// Apps/DemoApp/UltraCanvasEngineComparisonExamples.cpp
// The "Chart Engine Comparison" page: every chart converted to the chart
// engine side by side with the legacy chart it is meant to replace, on the
// same data. One tab row per chart family, one tab per chart; the family files
// (UltraCanvasEngineComparison*.cpp) build the tabs.
// Version: 1.0.0
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasEngineComparison.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include "CSSLayout/CSSLayout.h"

namespace UltraCanvas {
namespace EngineComparison {

namespace {

// Captions carry explicit line breaks and a fixed height: a wrapped label
// re-measures against an unconstrained width on re-layout, so wrapping is
// not used here (as on the bar chart page).
std::shared_ptr<UltraCanvasLabel> MakeCaption(const std::string& id, const std::string& text) {
    auto caption = std::make_shared<UltraCanvasLabel>(id, 0, 34, text);
    caption->SetFontSize(12);
    caption->SetTextColor(Color(90, 90, 100, 255));
    caption->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return caption;
}

// A heading over one side of the comparison.
std::shared_ptr<UltraCanvasLabel> MakeSideHeading(const std::string& id, const std::string& text,
                                                  const Color& background) {
    auto heading = std::make_shared<UltraCanvasLabel>(id, 0, 22, text);
    heading->SetFontSize(11);
    heading->SetFontWeight(FontWeight::Bold);
    heading->SetAlignment(TextAlignment::Center);
    heading->SetBackgroundColor(background);
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
    auto strip = std::make_shared<UltraCanvasContainer>(idPrefix + "Controls", 0, 30);
    strip->layout.SetFlexRow().SetFlexGap(10)
                 .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    strip->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return strip;
}

void AddToStrip(UltraCanvasContainer& strip, std::shared_ptr<UltraCanvasUIElement> element) {
    element->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    strip.AddChild(element);
}

void AddStripLabel(UltraCanvasContainer& strip, const std::string& id, const std::string& text,
                   int width) {
    auto label = std::make_shared<UltraCanvasLabel>(id, width, 20, text);
    label->SetFontSize(11);
    AddToStrip(strip, label);
}

} // namespace EngineComparison

// =============================================================================
// PAGE
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateChartEngineComparisonExamples() {
    using namespace EngineComparison;

    auto container = std::make_shared<UltraCanvasContainer>("EngineCompareContainer");
    container->SetBackgroundColor(Color(255, 255, 255, 255));
    container->SetPadding(8, 10);
    container->layout.SetFlexColumn().SetFlexGap(6)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    container->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto titleLabel = std::make_shared<UltraCanvasLabel>("EngineCompareTitle", 0, 32);
    titleLabel->SetText("Chart Engine Comparison - Legacy Charts and Their Engine Ports");
    titleLabel->SetFontSize(18);
    titleLabel->SetFontWeight(FontWeight::Bold);
    titleLabel->SetAlignment(TextAlignment::Center);
    titleLabel->SetBackgroundColor(Color(240, 240, 250, 255));
    titleLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    container->AddChild(titleLabel);

    auto descLabel = std::make_shared<UltraCanvasLabel>("EngineCompareDesc", 0, 42);
    descLabel->SetText(
        "Each tab shows a legacy chart (left) and its port onto UltraCanvasChartEngineElement (right) on the same data. The ports get the engine's axes,\n"
        "grid, measured layout, legend, solved label plan, themes, hover tooltips and entrance animation for free - compare them here before the switch.");
    descLabel->SetFontSize(11);
    descLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    container->AddChild(descLabel);

    auto families = std::make_shared<UltraCanvasTabbedContainer>("EngineCompareFamilies", 0, 0, 0, 0);
    families->SetTabPosition(TabPosition::Top);
    families->SetTabStyle(TabStyle::Modern);
    families->layoutItem.SetFlex(1, 1, CSSLayout::Dimension::Px(0))
                        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    families->AddTab("Lines & Points", MakeFamilyTabs("EngineCompareLines", LineFamilyTabs()));
    families->AddTab("Bars & Flows", MakeFamilyTabs("EngineCompareBars", BarFamilyTabs()));
    families->AddTab("Finance & Mosaics", MakeFamilyTabs("EngineCompareFinance", FinanceAndGridTabs()));
    families->AddTab("Polar", MakeFamilyTabs("EngineComparePolar", PolarFamilyTabs()));
    container->AddChild(families);
    return container;
}

} // namespace UltraCanvas
