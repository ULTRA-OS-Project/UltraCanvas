// Apps/DemoApp/UltraCanvasFunnelChartExamples.cpp
// Funnel chart showcase: the segmented e-commerce funnel with conversion necks,
// a solid marketing cone with leader-line callouts, the rounded card layout, a
// dark presentation funnel with badges and annotations, a horizontal sales
// funnel with a terminal callout, and a playground exposing every shape, scale
// and colour mode live.
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): a page title
//                  and description, the tabs in a card that fills the area,
//                  every tab a flex page whose notes wrap beside or under the chart
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Charts/UltraCanvasFunnelChart.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasDropdown.h"

#include <memory>
#include <vector>
#include <string>
#include <cstdio>
#include <random>

namespace UltraCanvas {

namespace {

// The presentation slide keeps the size it was designed at
constexpr int kInfographicW = 1030;
constexpr int kInfographicH = 690;

// A tab's page: the chart's title and description over a row holding the
// chart and its notes. The notes sit beside the chart while both fit and move
// under it when not. The tab gives the page its exact size; content taller or
// wider than that scrolls.
struct TabPage {
    std::shared_ptr<UltraCanvasContainer> tab;
    std::shared_ptr<UltraCanvasContainer> content;   // the chart + notes row
};

std::shared_ptr<UltraCanvasContainer> MakeTabContainer(const std::string& id) {
    auto tab = std::make_shared<UltraCanvasContainer>(id);
    tab->SetPadding(12, 20, 12, 20);
    tab->layout.SetFlexColumn().SetFlexGap(6)
               .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    ContainerStyle style;
    style.autoShowScrollbars = true;
    tab->SetContainerStyle(style);
    return tab;
}

TabPage MakeTabPage(const std::string& id, const std::string& titleId, const std::string& title,
                    const std::string& subtitleId, const std::string& subtitle) {
    TabPage page;
    page.tab = MakeTabContainer(id);
    // The chart title keeps its 17 px; the description its 11 px.
    DemoKit::AddHeading(page.tab, titleId, title)->SetFontSize(17);
    DemoKit::AddCaption(page.tab, subtitleId, subtitle)->SetFontSize(11);
    page.content = DemoKit::CreateRow(id + "Content", 20);
    page.content->layout.SetFlexAlignItems(CSSLayout::AlignItems::Start);
    DemoKit::Add(page.tab, page.content);
    return page;
}

// The chart's notes: a column that takes the row's spare width (at least
// 300 px, or it wraps under the chart).
std::shared_ptr<UltraCanvasLabel> AddNotesColumn(const std::shared_ptr<UltraCanvasContainer>& content,
                                                 const std::string& id, const std::string& text) {
    auto column = DemoKit::CreateColumn(id + "Column");
    DemoKit::Add(content, column, 1);
    column->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(300));
    column->SetPadding(20, 0, 0, 0);
    auto notes = DemoKit::AddText(column, id, text);
    notes->SetFontSize(10.5f);
    notes->SetTextColor(Color(70, 70, 70, 255));
    return notes;
}

// =============================================================================
// TAB 1: SEGMENTED E-COMMERCE FUNNEL
// =============================================================================
// Detached bars whose breadth tracks the value, joined by tapering necks that
// carry the stage-to-stage conversion. The classic analytics funnel.

std::shared_ptr<UltraCanvasUIElement> BuildSegmentedTab() {
    auto page = MakeTabPage("FunnelSegmentedTab", "SegTitle", "E-commerce campaign funnel",
                            "SegSubtitle",
                            "Recipients reaching each step, with the conversion into the next step "
                            "drawn on the neck between the bars");

    auto chart = std::make_shared<UltraCanvasFunnelChart>("SegmentedFunnel", 0, 0, 660, 570);
    chart->SetShapeMode(FunnelShapeMode::SegmentedBars);
    chart->SetScaleMode(FunnelScaleMode::WidthProportional);
    chart->SetTipMode(FunnelTipMode::MinWidthTip);
    chart->SetColorMode(FunnelColorMode::SequentialRamp);
    chart->SetColorRamp(Color(158, 202, 232, 255), Color(21, 74, 120, 255));
    chart->SetStageLabelPlacement(FunnelStageLabelPlacement::OutsideStartLabels);
    chart->SetFunnelValueLabelPlacement(FunnelValueLabelPlacement::InsideValueLabels);
    chart->SetPercentPlacement(FunnelPercentPlacement::OutsideEndPercent);
    chart->SetShowPercentOfFirst(true);
    chart->SetShowConversionOnConnector(true);
    chart->SetAbbreviateValues(true);
    chart->SetStageGap(26.0);
    chart->SetStageLabelColumnWidth(90.0);
    chart->SetPercentColumnWidth(72.0);

    chart->AddStage("Sent", 5680);
    chart->AddStage("Viewed", 3875);
    chart->AddStage("Clicked", 1669);
    chart->AddStage("Add to Cart", 610);
    chart->AddStage("Purchased", 565);
    DemoKit::AddFixed(page.content, chart);

    AddNotesColumn(page.content, "SegNotes",
        "Reading the chart\n\n"
        "Each bar's width is its share of the top "
        "stage, so the taper is the funnel.\n\n"
        "The pale neck between two bars carries "
        "the conversion from the upper stage into "
        "the lower one - 68.22% of everyone who "
        "was sent the mail opened it.\n\n"
        "The right-hand column is a different "
        "figure: the share of the very first stage, "
        "so it always falls monotonically.\n\n"
        "Where the funnel leaks\n\n"
        "  - The worst step is Clicked to Add to "
        "Cart at 36.57%.\n"
        "  - Once a basket exists the funnel barely "
        "leaks at all: 92.62% check out.\n"
        "  - End to end, 9.95% of recipients buy.\n\n"
        "Hover any bar for the raw count, both "
        "percentages and the number lost at that "
        "step.");

    return page.tab;
}

// =============================================================================
// TAB 2: CONTINUOUS MARKETING CONE
// =============================================================================
// One solid funnel: the outline tapers evenly to a point while each slab's
// thickness tracks its value, and the stages name themselves on leader lines.

std::shared_ptr<UltraCanvasUIElement> BuildContinuousTab() {
    auto page = MakeTabPage("FunnelContinuousTab", "ConTitle", "Marketing funnel",
                            "ConSubtitle",
                            "Slab thickness tracks the value, so the area of each band is the "
                            "population it holds");

    auto chart = std::make_shared<UltraCanvasFunnelChart>("ContinuousFunnel", 0, 0, 700, 590);
    chart->SetShapeMode(FunnelShapeMode::ContinuousFunnel);
    chart->SetScaleMode(FunnelScaleMode::HeightProportional);
    chart->SetTipMode(FunnelTipMode::PointTip);
    chart->SetColorMode(FunnelColorMode::CategoricalPalette);
    chart->SetPalette({
        Color(58, 160, 165, 255), Color(233, 126, 43, 255), Color(18, 71, 82, 255),
        Color(150, 145, 24, 255), Color(120, 130, 30, 255), Color(211, 62, 54, 255)
    });
    chart->SetStageLabelPlacement(FunnelStageLabelPlacement::LeaderLineLabels);
    chart->SetFunnelValueLabelPlacement(FunnelValueLabelPlacement::OutsideEndValues);
    chart->SetPercentPlacement(FunnelPercentPlacement::HidePercentColumn);
    chart->SetShowConversionOnConnector(false);
    chart->SetStageLabelColumnWidth(210.0);
    chart->SetValueFormat("%.0f");

    chart->AddStage("Impressions", 1163);
    chart->AddStage("Clicks", 742);
    chart->AddStage("Site Visits", 121);
    chart->AddStage("White Paper Download", 72);
    chart->AddStage("Free Sign Up", 56);
    chart->AddStage("Upgrade", 42);
    DemoKit::AddFixed(page.content, chart);

    AddNotesColumn(page.content, "ConNotes",
        "HeightProportional scaling\n\n"
        "In this mode the funnel's outline tapers "
        "evenly from the full width down to a point, "
        "and it is the thickness of each band that "
        "carries the value.\n\n"
        "That is why Impressions occupies most of "
        "the picture while the four smallest stages "
        "are compressed into the tip.\n\n"
        "LeaderLineLabels\n\n"
        "Each stage is named outside the shape and "
        "joined to it by a leader line. Because the "
        "value labels are also placed in the end "
        "column, the two merge into one string: "
        "'Clicks (742)'.\n\n"
        "Watch the tip\n\n"
        "PointTip closes the funnel completely past "
        "the last stage. Use FlatNeck with "
        "SetNeckRatio() when the final step should "
        "keep a visible width.");

    return page.tab;
}

// =============================================================================
// TAB 3: CARD ROWS
// =============================================================================
// Every stage gets a rounded row card; the wedge runs through the cards and the
// gaps between them, with the name on the left and the percentage on the right.

std::shared_ptr<UltraCanvasUIElement> BuildCardRowsTab() {
    auto page = MakeTabPage("FunnelCardsTab", "CardTitle", "Card row funnel",
                            "CardSubtitle",
                            "The same data laid out as rounded rows - the layout dashboards reach "
                            "for when the funnel sits beside other cards");

    auto chart = std::make_shared<UltraCanvasFunnelChart>("CardFunnel", 0, 0, 700, 580);
    chart->SetShapeMode(FunnelShapeMode::CardRows);
    chart->SetScaleMode(FunnelScaleMode::WidthProportional);
    chart->SetTipMode(FunnelTipMode::PointTip);
    chart->SetColorMode(FunnelColorMode::CategoricalPalette);
    chart->SetPalette({
        Color(47, 62, 156, 255), Color(58, 88, 214, 255), Color(88, 128, 246, 255),
        Color(252, 200, 90, 255), Color(238, 122, 44, 255)
    });
    chart->SetStageLabelPlacement(FunnelStageLabelPlacement::OutsideStartLabels);
    chart->SetFunnelValueLabelPlacement(FunnelValueLabelPlacement::InsideValueLabels);
    chart->SetPercentPlacement(FunnelPercentPlacement::OutsideEndPercent);
    chart->SetShowPercentOfFirst(true);
    chart->SetShowConversionOnConnector(false);
    chart->SetShowLegend(true);
    chart->SetLegendPosition(FunnelLegendPosition::LegendTopCenter);
    chart->SetStageGap(10.0);
    chart->SetStageLabelColumnWidth(110.0);
    chart->SetPercentColumnWidth(70.0);
    chart->SetCardStyle(Color(255, 255, 255, 255), Color(214, 222, 233, 255), 20.0);

    chart->AddStage("Sent", 5680);
    chart->AddStage("Viewed", 3875);
    chart->AddStage("Clicked", 1669);
    chart->AddStage("Add to Cart", 610);
    chart->AddStage("Purchased", 565);
    DemoKit::AddFixed(page.content, chart);

    AddNotesColumn(page.content, "CardNotes",
        "CardRows\n\n"
        "Each stage is drawn inside a rounded card "
        "that spans the whole element, and the "
        "wedge passes through the card and the gap "
        "below it without breaking.\n\n"
        "The name column and the percentage column "
        "are the same ones the other shapes use; "
        "here they simply land inside the card.\n\n"
        "Hovering a row tints its border with the "
        "stage colour, which is why the cards make "
        "good click targets for drill-down.\n\n"
        "The legend is switched on for this tab. "
        "It lists every stage with its swatch and "
        "can be pinned to any of six positions.");

    return page.tab;
}

// =============================================================================
// TAB 4: PRESENTATION INFOGRAPHIC
// =============================================================================
// Sheared slabs on a dark ground, numbered badges down the left and annotated
// callouts on the right - the deck-slide funnel.

std::shared_ptr<UltraCanvasUIElement> BuildInfographicTab() {
    auto tab = MakeTabContainer("FunnelInfographicTab");
    tab->SetPadding(0);

    // The chart is the whole tab page so its own dark background is the slide background
    auto chart = std::make_shared<UltraCanvasFunnelChart>("InfographicFunnel", 0, 0,
                                                          kInfographicW, kInfographicH);
    chart->SetChartTitle("PRODUCT ADOPTION PIPELINE");
    chart->SetBackgroundColor(Color(46, 43, 74, 255));
    chart->SetShapeMode(FunnelShapeMode::SkewedInfographic);
    chart->SetScaleMode(FunnelScaleMode::WidthProportional);
    chart->SetTipMode(FunnelTipMode::PointTip);
    chart->SetColorMode(FunnelColorMode::PerStageOverride);
    chart->SetSlabSkew(18.0);
    chart->SetStageGap(14.0);
    chart->SetUseGradientFill(true, 0.28);
    chart->SetShowTipTriangle(true);

    chart->SetStageLabelPlacement(FunnelStageLabelPlacement::InsideStageLabels);
    chart->SetFunnelValueLabelPlacement(FunnelValueLabelPlacement::HideValueLabels);
    chart->SetPercentPlacement(FunnelPercentPlacement::HidePercentColumn);
    chart->SetShowConversionOnConnector(false);
    chart->SetShowBadges(true, 26.0);
    chart->SetBadgeColor(Color(190, 185, 225, 255));
    chart->SetDescriptionColumnWidth(280.0);
    chart->SetDescriptionStyle(11.0, Color(205, 200, 235, 255));
    chart->SetLeaderLineColor(Color(120, 115, 160, 255));
    chart->SetInsideLabelColor(Color(255, 255, 255, 255));
    chart->SetAutoContrastInsideLabels(false);
    chart->SetStageLabelColor(Color(225, 222, 245, 255));

    struct InfoStage {
        const char* label;
        double value;
        const char* description;
        Color color;
    };
    const InfoStage stages[] = {
        {"Awareness", 100, "Reached by the launch campaign", Color(157, 92, 214, 255)},
        {"Interest",   72, "Opened the product tour",        Color(232, 74, 190, 255)},
        {"Evaluation", 44, "Started a trial workspace",      Color(56, 199, 224, 255)},
        {"Adoption",   21, "Converted to a paid seat",       Color(84, 124, 226, 255)},
    };

    for (const auto& entry : stages) {
        FunnelStage stage(entry.label, entry.value);
        stage.description = entry.description;
        stage.stageColor = entry.color;
        chart->AddStage(stage);
    }
    DemoKit::AddFixed(tab, chart);

    return tab;
}

// =============================================================================
// TAB 5: HORIZONTAL SALES FUNNEL
// =============================================================================
// The same element rotated: the funnel flows left to right, every stage labels
// itself inside its own slab and the outcome is circled past the tip.

std::shared_ptr<UltraCanvasUIElement> BuildHorizontalTab() {
    auto page = MakeTabPage("FunnelHorizontalTab", "HorTitle", "Sales funnel - conversion rate tracking",
                            "HorSubtitle",
                            "HorizontalFunnel flows left to right; the circled figure past the tip "
                            "is the outcome the pipeline is being judged on");

    auto chart = std::make_shared<UltraCanvasFunnelChart>("HorizontalFunnel", 0, 0, 990, 330);
    chart->SetOrientation(FunnelOrientation::HorizontalFunnel);
    chart->SetShapeMode(FunnelShapeMode::ContinuousFunnel);
    chart->SetScaleMode(FunnelScaleMode::WidthProportional);
    chart->SetTipMode(FunnelTipMode::MinWidthTip);
    chart->SetColorMode(FunnelColorMode::PerStageOverride);
    chart->SetStageLabelPlacement(FunnelStageLabelPlacement::InsideStageLabels);
    chart->SetFunnelValueLabelPlacement(FunnelValueLabelPlacement::InsideValueLabels);
    chart->SetPercentPlacement(FunnelPercentPlacement::InsideStagePercent);
    chart->SetShowPercentOfFirst(true);
    chart->SetShowConversionOnConnector(false);
    chart->SetShowTerminalCallout(true, 26.0);
    chart->SetPercentFormat("%.0f%%");
    chart->SetAutoContrastInsideLabels(true);

    struct SalesStage { const char* label; double value; Color color; };
    const SalesStage stages[] = {
        {"Inquiries",   389, Color(150, 150, 150, 255)},
        {"Lead",        252, Color(0, 174, 239, 255)},
        {"Opportunity", 121, Color(252, 191, 45, 255)},
        {"Sales",        40, Color(122, 193, 66, 255)},
    };
    for (const auto& entry : stages) {
        FunnelStage stage(entry.label, entry.value);
        stage.stageColor = entry.color;
        chart->AddStage(stage);
    }
    // The wide chart takes the row; the notes go under it.
    DemoKit::AddFixed(page.content, chart);

    auto notes = DemoKit::CreateText("HorNotes",
        "Orientation\n\n"
        "SetOrientation(FunnelOrientation::HorizontalFunnel) swaps the flow axis and the cross axis. "
        "Everything else - the shape modes, the label columns, the connector necks, the hit testing - "
        "works exactly as it does vertically, so a funnel can be rotated without touching any other setting.\n\n"
        "Inside labels\n\n"
        "This tab puts the stage name, its value and its share of the first stage inside the slab. "
        "The chart measures the text against the slab before drawing it and simply omits any line that will "
        "not fit, which is why the narrow Sales band drops to fewer lines than Inquiries. "
        "SetAutoContrastInsideLabels(true) picks white or near-black text per stage from the fill's brightness, "
        "so the grey, blue, amber and green bands all stay readable.");
    notes->SetFontSize(10.5f);
    notes->SetTextColor(Color(70, 70, 70, 255));
    DemoKit::Add(page.tab, notes);

    return page.tab;
}

// =============================================================================
// TAB 6: PLAYGROUND - EVERY MODE, LIVE
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> BuildPlaygroundTab() {
    auto page = MakeTabPage("FunnelPlaygroundTab", "PlayTitle", "Playground: shape, scale and colour modes",
                            "PlaySubtitle",
                            "A recruitment pipeline broken down by source - every switch below "
                            "restyles the same data");

    auto chart = std::make_shared<UltraCanvasFunnelChart>("PlaygroundFunnel", 0, 0, 640, 590);
    chart->SetShapeMode(FunnelShapeMode::SegmentedBars);
    chart->SetScaleMode(FunnelScaleMode::WidthProportional);
    chart->SetColorMode(FunnelColorMode::SequentialRamp);
    chart->SetColorRamp(Color(150, 205, 190, 255), Color(20, 90, 100, 255));
    chart->SetStageLabelPlacement(FunnelStageLabelPlacement::OutsideStartLabels);
    chart->SetFunnelValueLabelPlacement(FunnelValueLabelPlacement::InsideValueLabels);
    chart->SetPercentPlacement(FunnelPercentPlacement::OutsideEndPercent);
    chart->SetShowPercentOfFirst(true);
    chart->SetStageLabelColumnWidth(120.0);
    chart->SetPercentColumnWidth(80.0);
    chart->SetPercentFormat("%.1f%%");

    // Sub-segments turn each stage into a stack: the funnel still narrows, but
    // every band also shows where its population came from.
    struct PipelineStage {
        const char* label;
        double referral;
        double jobBoard;
        double outbound;
    };
    const PipelineStage pipeline[] = {
        {"Applied",    420, 980, 600},
        {"Screened",   310, 420, 210},
        {"Interviewed",190, 150,  85},
        {"Offered",     72,  38,  18},
        {"Hired",       55,  24,  11},
    };

    const Color kReferral(64, 145, 160, 255);
    const Color kJobBoard(226, 152, 60, 255);
    const Color kOutbound(120, 110, 190, 255);

    for (const auto& row : pipeline) {
        FunnelStage stage(row.label, row.referral + row.jobBoard + row.outbound);
        stage.segments.push_back(FunnelSubSegment("Referral", row.referral, kReferral));
        stage.segments.push_back(FunnelSubSegment("Job board", row.jobBoard, kJobBoard));
        stage.segments.push_back(FunnelSubSegment("Outbound", row.outbound, kOutbound));
        stage.targetValue = stage.stageValue * 1.15;   // The plan for this stage
        chart->AddStage(stage);
    }
    DemoKit::AddFixed(page.content, chart);

    // ===== CONTROL PANEL =====
    // A column beside the chart (under it when the row is too narrow); the
    // controls take its width.
    auto panel = DemoKit::CreateColumn("PlayPanel", 4);
    DemoKit::Add(page.content, panel, 1);
    panel->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(330));
    panel->SetPadding(8, 0, 0, 0);

    DemoKit::AddHeading(panel, "PlayControls", "Chart controls");

    DemoKit::AddText(panel, "PlayShapeLabel", "Shape mode")->SetFontSize(11);

    auto shapeDrop = std::make_shared<UltraCanvasDropdown>("PlayShape", 0, 0, 330, 26);
    shapeDrop->AddItem("Segmented bars + necks");
    shapeDrop->AddItem("Continuous funnel");
    shapeDrop->AddItem("Card rows");
    shapeDrop->AddItem("Skewed infographic");
    shapeDrop->AddItem("Bar style (diminishing bars)");
    shapeDrop->SetSelectedIndex(0, false);
    shapeDrop->onSelectionChanged = [chart](int index, const DropdownItem&) {
        switch (index) {
            case 1: chart->SetShapeMode(FunnelShapeMode::ContinuousFunnel); break;
            case 2: chart->SetShapeMode(FunnelShapeMode::CardRows); break;
            case 3: chart->SetShapeMode(FunnelShapeMode::SkewedInfographic); break;
            case 4: chart->SetShapeMode(FunnelShapeMode::BarStyleFunnel); break;
            default: chart->SetShapeMode(FunnelShapeMode::SegmentedBars); break;
        }
    };
    DemoKit::AddStretched(panel, shapeDrop);

    DemoKit::AddText(panel, "PlayScaleLabel", "Scale mode")->SetFontSize(11);

    auto scaleDrop = std::make_shared<UltraCanvasDropdown>("PlayScale", 0, 0, 330, 26);
    scaleDrop->AddItem("Width proportional");
    scaleDrop->AddItem("Height proportional");
    scaleDrop->AddItem("Area proportional");
    scaleDrop->AddItem("Equal stages (pipeline)");
    scaleDrop->SetSelectedIndex(0, false);
    scaleDrop->onSelectionChanged = [chart](int index, const DropdownItem&) {
        switch (index) {
            case 1: chart->SetScaleMode(FunnelScaleMode::HeightProportional); break;
            case 2: chart->SetScaleMode(FunnelScaleMode::AreaProportional); break;
            case 3: chart->SetScaleMode(FunnelScaleMode::EqualStages); break;
            default: chart->SetScaleMode(FunnelScaleMode::WidthProportional); break;
        }
    };
    DemoKit::AddStretched(panel, scaleDrop);

    DemoKit::AddText(panel, "PlayColorLabel", "Colour mode (stages here carry their own segment colours)")->SetFontSize(11);

    auto colorDrop = std::make_shared<UltraCanvasDropdown>("PlayColor", 0, 0, 330, 26);
    colorDrop->AddItem("Sequential ramp");
    colorDrop->AddItem("Categorical palette");
    colorDrop->AddItem("Single hue");
    colorDrop->AddItem("Colour by conversion");
    colorDrop->SetSelectedIndex(0, false);
    colorDrop->onSelectionChanged = [chart](int index, const DropdownItem&) {
        switch (index) {
            case 1: chart->SetColorMode(FunnelColorMode::CategoricalPalette); break;
            case 2: chart->SetColorMode(FunnelColorMode::SingleHue); break;
            case 3: chart->SetColorMode(FunnelColorMode::ThresholdColor); break;
            default: chart->SetColorMode(FunnelColorMode::SequentialRamp); break;
        }
    };
    DemoKit::AddStretched(panel, colorDrop);

    DemoKit::AddText(panel, "PlayTipLabel", "Tip mode")->SetFontSize(11);

    auto tipDrop = std::make_shared<UltraCanvasDropdown>("PlayTip", 0, 0, 330, 26);
    tipDrop->AddItem("Last stage keeps its width");
    tipDrop->AddItem("Close to a point");
    tipDrop->AddItem("Close to a flat neck");
    tipDrop->SetSelectedIndex(0, false);
    tipDrop->onSelectionChanged = [chart](int index, const DropdownItem&) {
        switch (index) {
            case 1: chart->SetTipMode(FunnelTipMode::PointTip); break;
            case 2: chart->SetTipMode(FunnelTipMode::FlatNeck); break;
            default: chart->SetTipMode(FunnelTipMode::MinWidthTip); break;
        }
    };
    DemoKit::AddStretched(panel, tipDrop);

    DemoKit::AddText(panel, "PlayGapLabel", "Gap between stages")->SetFontSize(11);

    auto gapSlider = std::make_shared<UltraCanvasSlider>("PlayGap", 0, 0, 330, 24);
    gapSlider->SetRange(0.0f, 60.0f);
    gapSlider->SetStep(1.0f);
    gapSlider->SetValue(22.0f);
    gapSlider->SetValueDisplay(SliderValueDisplay::Tooltip);
    gapSlider->onValueChanged = [chart](float value) { chart->SetStageGap(value); };
    DemoKit::AddStretched(panel, gapSlider);

    auto horizontalCheck = std::make_shared<UltraCanvasCheckbox>("PlayHorizontal", 0, 0,
                                                                 330, 24, "Horizontal orientation");
    horizontalCheck->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetOrientation(newState == CheckedState::Checked
                                      ? FunnelOrientation::HorizontalFunnel
                                      : FunnelOrientation::VerticalFunnel);
    };
    DemoKit::AddStretched(panel, horizontalCheck);

    auto gradientCheck = std::make_shared<UltraCanvasCheckbox>("PlayGradient", 0, 0,
                                                               330, 24, "Gradient fills");
    gradientCheck->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetUseGradientFill(newState == CheckedState::Checked, 0.25);
    };
    DemoKit::AddStretched(panel, gradientCheck);

    auto targetCheck = std::make_shared<UltraCanvasCheckbox>("PlayTarget", 0, 0,
                                                             330, 24, "Target overlay (plan + 15%)");
    targetCheck->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetShowTargetOverlay(newState == CheckedState::Checked);
    };
    DemoKit::AddStretched(panel, targetCheck);

    auto bottleneckCheck = std::make_shared<UltraCanvasCheckbox>("PlayBottleneck", 0, 0,
                                                                 330, 24, "Highlight the bottleneck");
    bottleneckCheck->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetHighlightBottleneck(newState == CheckedState::Checked);
    };
    DemoKit::AddStretched(panel, bottleneckCheck);

    auto dimCheck = std::make_shared<UltraCanvasCheckbox>("PlayDim", 0, 0,
                                                          330, 24, "Dim stages you are not hovering");
    dimCheck->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetDimUnhoveredStages(newState == CheckedState::Checked);
    };
    DemoKit::AddStretched(panel, dimCheck);

    auto dropOffCheck = std::make_shared<UltraCanvasCheckbox>("PlayDropOff", 0, 0,
                                                              330, 24, "Show the loss at each step");
    dropOffCheck->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetShowDropOffLabels(newState == CheckedState::Checked);
    };
    DemoKit::AddStretched(panel, dropOffCheck);

    auto selectionTitle = DemoKit::AddHeading(panel, "PlaySelTitle", "Selected stage");
    selectionTitle->SetFontSize(12);
    selectionTitle->SetMargin(6, 0, 0, 0);

    // Rewritten by the click callbacks below; it grows with the report.
    auto selection = DemoKit::AddText(panel, "PlaySelection",
                                      "Click a stage to read its figures here.\nClick a neck to report the "
                                      "conversion through it.");
    selection->SetFontSize(11);
    selection->SetTextColor(Color(70, 70, 70, 255));

    // The callbacks report indices into the data source, so sorting cannot shift them
    auto source = chart->GetFunnelDataSource();
    chart->onStageClick = [selection, source, chart = chart.get()](size_t stageIndex) {
        if (!source || stageIndex >= source->GetPointCount()) return;
        const auto& stage = source->GetStage(stageIndex);
        FunnelStageMetrics metrics = chart->GetStageMetrics(stageIndex);

        char buffer[512];
        std::snprintf(buffer, sizeof(buffer),
                      "%s\n\nReached: %.0f\nOf first stage: %.1f%%\nFrom previous: %.1f%%\n"
                      "Lost here: %.0f\n\nReferral %.0f / Job board %.0f / Outbound %.0f",
                      stage.stageLabel.c_str(), metrics.value,
                      metrics.percentOfFirst, metrics.percentOfPrevious, metrics.dropOff,
                      stage.segments.size() > 0 ? stage.segments[0].value : 0.0,
                      stage.segments.size() > 1 ? stage.segments[1].value : 0.0,
                      stage.segments.size() > 2 ? stage.segments[2].value : 0.0);
        selection->SetText(buffer);
    };

    chart->onConnectorClick = [selection, source, chart = chart.get()](size_t upperStageIndex) {
        if (!source || upperStageIndex + 1 >= source->GetPointCount()) return;
        const auto& upper = source->GetStage(upperStageIndex);
        const auto& lower = source->GetStage(upperStageIndex + 1);
        FunnelStageMetrics metrics = chart->GetStageMetrics(upperStageIndex + 1);

        char buffer[512];
        std::snprintf(buffer, sizeof(buffer),
                      "%s -> %s\n\nConversion: %.1f%%\nLost: %.0f candidates",
                      upper.stageLabel.c_str(), lower.stageLabel.c_str(),
                      metrics.percentOfPrevious, metrics.dropOff);
        selection->SetText(buffer);
    };

    return page.tab;
}

} // anonymous namespace

// =============================================================================
// MAIN ENTRY POINT
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateFunnelChartExamples() {
    auto page = DemoKit::CreatePage("FunnelChartExamples", "Funnel Chart Examples",
        "The segmented e-commerce funnel with conversion necks, a solid marketing cone with "
        "leader-line callouts, the rounded card layout, a dark presentation funnel with badges "
        "and annotations, a horizontal sales funnel with a terminal callout, and a playground "
        "exposing every shape, scale and colour mode live");
    // The page is the display area's height and the tabs take what the title
    // and description leave; each tab page scrolls inside.
    page->SetFillsArea(true);

    auto card = DemoKit::CreateCard("FunnelChartCard");
    card->SetPadding(4);
    DemoKit::Add(page, card, 1);

    auto tabs = std::make_shared<UltraCanvasTabbedContainer>("FunnelChartTabs", 0, 0, 0, 0);
    tabs->SetTabStyle(TabStyle::Rounded);
    tabs->SetTabHeight(32);
    tabs->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));

    tabs->AddTab("Segmented Funnel", BuildSegmentedTab());
    tabs->AddTab("Continuous Cone", BuildContinuousTab());
    tabs->AddTab("Card Rows", BuildCardRowsTab());
    tabs->AddTab("Presentation Infographic", BuildInfographicTab());
    tabs->AddTab("Horizontal Funnel", BuildHorizontalTab());
    tabs->AddTab("Playground", BuildPlaygroundTab());
    tabs->SetActiveTab(0);

    DemoKit::Add(card, tabs, 1);
    return page;
}

} // namespace UltraCanvas
