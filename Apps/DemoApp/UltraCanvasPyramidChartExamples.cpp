// Apps/DemoApp/UltraCanvasPyramidChartExamples.cpp
// Pyramid diagram showcase: an extruded 3D pyramid with a keyed level list and
// colour ribbon callouts, a spectrum solid carrying its text inside the tiers,
// an outlined hierarchy with icon cards, two flat presentation layouts, an
// alternating infographic with ghost numerals, an area-proportional data pyramid
// with a reference shape, and a playground exposing every mode live.
// Version: 1.0.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Charts/UltraCanvasPyramidChart.h"
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

namespace UltraCanvas {

namespace {

constexpr int kTabContentW = 1240;
constexpr int kTabContentH = 700;
// The demo shell shows roughly this much of a tab without scrolling
constexpr int kVisibleW = 1020;

// Adds the heading pair every tab opens with
void AddTabHeading(const std::shared_ptr<UltraCanvasContainer>& tab, const std::string& idPrefix,
                   const std::string& title, const std::string& subtitle) {
    auto titleLabel = std::make_shared<UltraCanvasLabel>(idPrefix + "Title", 30, 12, 900, 26);
    titleLabel->SetText(title);
    titleLabel->SetFontSize(17);
    titleLabel->SetFontWeight(FontWeight::Bold);
    titleLabel->SetTextColor(Color(30, 30, 30, 255));
    tab->AddChild(titleLabel);

    auto subtitleLabel = std::make_shared<UltraCanvasLabel>(idPrefix + "Subtitle", 30, 40, 1000, 20);
    subtitleLabel->SetText(subtitle);
    subtitleLabel->SetFontSize(11);
    subtitleLabel->SetTextColor(Color(100, 100, 100, 255));
    tab->AddChild(subtitleLabel);
}

// =============================================================================
// TAB 1: 3D RIBBON PYRAMID
// =============================================================================
// The classic deck pyramid: an extruded solid with a detached capstone, a keyed
// list of the tiers down one side and a colour ribbon carrying each tier's copy
// down the other.

std::shared_ptr<UltraCanvasUIElement> Build3DRibbonTab() {
    auto tab = std::make_shared<UltraCanvasContainer>("PyramidRibbonTab", 0, 0,
                                                      kTabContentW, kTabContentH);
    AddTabHeading(tab, "Ribbon", "Product strategy pyramid",
                  "Extruded tiers with a keyed list on the left and a colour ribbon per tier "
                  "on the right");

    auto chart = std::make_shared<UltraCanvasPyramidChart>("RibbonPyramid", 20, 70, kVisibleW - 40, 600);
    chart->SetShapeMode(PyramidShapeMode::Pyramid3D);
    chart->SetScaleMode(PyramidScaleMode::EqualLevels);
    chart->SetApexMode(PyramidApexMode::PointApex);
    chart->SetApexCap(PyramidApexCap::ApexSeparateCap, 40.0);
    chart->SetDepth3D(24.0);
    chart->SetAngle3D(34.0);
    chart->SetFaceShading(0.28, 0.26);
    chart->SetLevelGap(9.0);
    chart->SetPyramidWidthRatio(0.92);

    chart->SetColorMode(PyramidColorMode::CategoricalPalette);
    chart->SetPalette({
        Color(96, 100, 108, 255), Color(150, 88, 186, 255), Color(58, 178, 190, 255),
        Color(104, 178, 76, 255), Color(214, 74, 66, 255),  Color(238, 178, 44, 255)
    });

    // The left column is the key: a colour dot, the tier's name, a leader line
    chart->SetLevelLabelPlacement(PyramidLevelLabelPlacement::OutsideStartLabels);
    chart->SetShowLevelSwatch(true);
    chart->SetLevelLabelColumnWidth(120.0);
    chart->SetLevelLabelFontSize(11.0);

    // ... and the right column is the copy, on ribbons in the tier's own colour
    chart->SetCalloutStyle(PyramidCalloutStyle::ColorRibbonCallouts);
    chart->SetCalloutSide(PyramidCalloutSide::CalloutsRight);
    chart->SetCalloutLayout(PyramidCalloutLayout::EvenlyStacked);
    chart->SetConnectorStyle(PyramidConnectorStyle::NotchPointer);
    chart->SetCalloutColumnWidth(268.0);
    chart->SetCalloutSpacing(7.0);
    chart->SetCalloutFontSizes(11.5, 9.5);

    struct Tier { const char* label; const char* title; const char* body; };
    const Tier tiers[] = {
        {"Vision",     "Vision",     "Where the product is going in three years"},
        {"Strategy",   "Strategy",   "The bets we are making to get there"},
        {"Roadmap",    "Roadmap",    "Sequenced themes for the next four quarters"},
        {"Epics",      "Epics",      "Bodies of work a squad can own end to end"},
        {"Stories",    "Stories",    "Sliced work that ships inside one sprint"},
        {"Tasks",      "Tasks",      "The day to day units the team pulls from"},
    };
    for (const auto& tier : tiers) {
        PyramidLevel level(tier.label);
        level.calloutTitle = tier.title;
        level.description = tier.body;
        chart->AddLevel(level);
    }
    tab->AddChild(chart);

    return tab;
}

// =============================================================================
// TAB 2: SPECTRUM 3D WITH TEXT IN THE TIERS
// =============================================================================
// No side column at all: every tier carries its own wrapped paragraph, measured
// against the narrow edge of the trapezoid so the last line never runs out over
// the slope.

std::shared_ptr<UltraCanvasUIElement> BuildSpectrumTab() {
    auto tab = std::make_shared<UltraCanvasContainer>("PyramidSpectrumTab", 0, 0,
                                                      kTabContentW, kTabContentH);
    AddTabHeading(tab, "Spectrum", "Maslow's hierarchy of needs",
                  "Wrapped copy inside each tier, numbered on the leading edge - the text is "
                  "fitted to the narrow end of every trapezoid");

    auto chart = std::make_shared<UltraCanvasPyramidChart>("SpectrumPyramid", 60, 70, 900, 610);
    chart->SetShapeMode(PyramidShapeMode::Pyramid3D);
    chart->SetScaleMode(PyramidScaleMode::EqualLevels);
    chart->SetApexMode(PyramidApexMode::PointApex);
    chart->SetDepth3D(30.0);
    chart->SetAngle3D(30.0);
    chart->SetLevelGap(7.0);
    chart->SetColorMode(PyramidColorMode::CategoricalPalette);
    chart->SetPalettePreset(PyramidPalettePreset::SpectrumPalette);

    chart->SetLevelLabelPlacement(PyramidLevelLabelPlacement::InsideLevelLabels);
    chart->SetInsideTextAlign(PyramidInsideTextAlign::InsideTextStart);
    chart->SetShowDescriptionInside(true);
    chart->SetWrapInsideText(true);
    chart->SetShrinkInsideText(true, 7.5);

    chart->SetBadgeShape(PyramidBadgeShape::CircleBadge);
    chart->SetBadgePlacement(PyramidBadgePlacement::BadgeOnEdge);
    chart->SetBadgeStyle(14.0, 11.0);
    chart->SetBadgeColors(Color(255, 255, 255, 235), Color(60, 62, 70, 255));

    struct Need { const char* label; const char* body; };
    const Need needs[] = {
        {"Self-actualisation", "Becoming everything one is capable of becoming"},
        {"Esteem",             "Respect, status, recognition, strength and freedom"},
        {"Love and belonging", "Friendship, intimacy, family and a sense of connection"},
        {"Safety",             "Personal security, employment, health and property"},
        {"Physiological",      "Air, water, food, shelter, sleep, clothing and warmth"},
    };
    for (const auto& need : needs) {
        PyramidLevel level(need.label);
        level.description = need.body;
        chart->AddLevel(level);
    }
    tab->AddChild(chart);

    return tab;
}

// =============================================================================
// TAB 3: OUTLINED HIERARCHY WITH ICON CARDS
// =============================================================================
// Stroked tiers with no fill at all, and a column of outlined cards each carrying
// a circular icon on the edge facing the pyramid.

std::shared_ptr<UltraCanvasUIElement> BuildOutlineTab() {
    auto tab = std::make_shared<UltraCanvasContainer>("PyramidOutlineTab", 0, 0,
                                                      kTabContentW, kTabContentH);
    AddTabHeading(tab, "Outline", "Pyramid diagram - 5 hierarchy levels",
                  "Outline-only tiers with a detached capstone, described by icon cards joined "
                  "on straight connectors");

    auto chart = std::make_shared<UltraCanvasPyramidChart>("OutlinePyramid", 20, 70, kVisibleW - 40, 600);
    chart->SetShapeMode(PyramidShapeMode::SegmentedPyramid);
    chart->SetFillStyle(PyramidFillStyle::OutlineOnly);
    chart->SetOutlineStyle(2.5, false);
    chart->SetApexMode(PyramidApexMode::PointApex);
    chart->SetApexCap(PyramidApexCap::ApexSeparateCap, 46.0);
    chart->SetLevelGap(10.0);
    chart->SetPyramidWidthRatio(0.95);

    chart->SetColorMode(PyramidColorMode::CategoricalPalette);
    chart->SetPalette({
        Color(150, 88, 186, 255), Color(58, 178, 190, 255), Color(104, 178, 76, 255),
        Color(238, 178, 44, 255), Color(214, 74, 66, 255)
    });

    chart->SetLevelLabelPlacement(PyramidLevelLabelPlacement::InsideLevelLabels);
    chart->SetLevelLabelColor(Color(70, 74, 82, 255));

    chart->SetCalloutStyle(PyramidCalloutStyle::IconCardCallouts);
    chart->SetCalloutSide(PyramidCalloutSide::CalloutsLeft);
    chart->SetCalloutLayout(PyramidCalloutLayout::AlignedToLevel);
    chart->SetConnectorStyle(PyramidConnectorStyle::StraightConnector);
    chart->SetCalloutColumnWidth(330.0);
    chart->SetCalloutCornerRadius(10.0);
    chart->SetCalloutCardStyle(Color(255, 255, 255, 255), Color(216, 222, 230, 255));
    chart->SetBadgeStyle(17.0, 11.0);

    struct Row { const char* label; const char* title; const char* body; const char* icon; };
    const Row rows[] = {
        {"Govern",  "Governance",   "Policy, ownership and the decisions only leadership makes",
         "media/icons/settings.png"},
        {"Measure", "Measurement",  "The instrumentation that tells us whether any of this works",
         "media/icons/chart-icon.png"},
        {"Build",   "Delivery",     "Teams shipping the changes the strategy asked for",
         "media/icons/computer.png"},
        {"Enable",  "Enablement",   "Tooling and training so delivery is not the bottleneck",
         "media/icons/python.png"},
        {"Operate", "Operations",   "Running what we have already shipped, reliably",
         "media/icons/check.png"},
    };
    for (const auto& row : rows) {
        PyramidLevel level(row.label);
        level.calloutTitle = row.title;
        level.description = row.body;
        level.iconPath = row.icon;
        chart->AddLevel(level);
    }
    tab->AddChild(chart);

    return tab;
}

// =============================================================================
// TAB 4: FLAT AND MINIMAL
// =============================================================================
// Two presentation layouts side by side: detached tiers labelled in a plain
// column, and a solid gradient pyramid anchored to the right of a numbered list.

std::shared_ptr<UltraCanvasUIElement> BuildFlatTab() {
    auto tab = std::make_shared<UltraCanvasContainer>("PyramidFlatTab", 0, 0,
                                                      kTabContentW, kTabContentH);
    AddTabHeading(tab, "Flat", "Flat presentation layouts",
                  "Left: detached tiers with a plain label column. Right: a solid gradient "
                  "pyramid beside a numbered list");

    // ----- Left: the plain options pyramid -----
    auto options = std::make_shared<UltraCanvasPyramidChart>("FlatOptions", 20, 70, 480, 600);
    options->SetShapeMode(PyramidShapeMode::SegmentedPyramid);
    options->SetScaleMode(PyramidScaleMode::EqualLevels);
    options->SetApexMode(PyramidApexMode::PointApex);
    options->SetLevelGap(8.0);
    options->SetPyramidWidthRatio(0.78);
    options->SetColorMode(PyramidColorMode::CategoricalPalette);
    options->SetPalette({
        Color(226, 82, 46, 255),  Color(240, 158, 46, 255), Color(46, 190, 178, 255),
        Color(58, 130, 200, 255), Color(122, 132, 144, 255)
    });
    options->SetLevelLabelPlacement(PyramidLevelLabelPlacement::OutsideEndLabels);
    options->SetLevelLabelColumnWidth(110.0);
    options->SetLevelLabelFontSize(12.0);

    options->AddLevel("Option 01");
    options->AddLevel("Option 02");
    options->AddLevel("Option 03");
    options->AddLevel("Option 04");
    options->AddLevel("Option 05");
    tab->AddChild(options);

    // ----- Right: the business solutions layout -----
    auto solutions = std::make_shared<UltraCanvasPyramidChart>("FlatSolutions", 515, 70, 505, 600);
    solutions->SetShapeMode(PyramidShapeMode::SolidPyramid);
    solutions->SetFillStyle(PyramidFillStyle::GradientFill);
    solutions->SetGradientLightening(0.28);
    solutions->SetScaleMode(PyramidScaleMode::EqualLevels);
    solutions->SetApexMode(PyramidApexMode::PointApex);
    solutions->SetPyramidWidthRatio(0.95);
    solutions->SetColorMode(PyramidColorMode::CategoricalPalette);
    solutions->SetPalette({
        Color(242, 158, 44, 255), Color(226, 74, 62, 255),
        Color(150, 42, 110, 255), Color(48, 132, 200, 255)
    });

    // The list sits in the label column, each row numbered in the tier's colour
    solutions->SetLevelLabelPlacement(PyramidLevelLabelPlacement::OutsideStartLabels);
    solutions->SetLevelLabelColumnWidth(215.0);
    solutions->SetLevelLabelFontSize(11.5);
    solutions->SetShowLevelListRules(true);
    solutions->SetBadgeShape(PyramidBadgeShape::CircleBadge);
    solutions->SetBadgePlacement(PyramidBadgePlacement::BadgeOnLabelRow);
    solutions->SetBadgeStyle(11.0, 9.5);

    solutions->AddLevel("Shared Ambition");
    solutions->AddLevel("True Results");
    solutions->AddLevel("Development");
    solutions->AddLevel("Executive coaching services");
    tab->AddChild(solutions);

    return tab;
}

// =============================================================================
// TAB 5: ALTERNATING INFOGRAPHIC
// =============================================================================
// Callouts thrown left and right of the pyramid, each behind a large ghost
// numeral, with the connector reduced to a notch on the tier edge.

std::shared_ptr<UltraCanvasUIElement> BuildAlternatingTab() {
    auto tab = std::make_shared<UltraCanvasContainer>("PyramidAlternatingTab", 0, 0,
                                                      kTabContentW, kTabContentH);
    AddTabHeading(tab, "Alternating", "Pyramid infographic",
                  "Callout blocks alternating either side of the pyramid, numbered with a ghost "
                  "numeral in the tier's colour");

    auto chart = std::make_shared<UltraCanvasPyramidChart>("AlternatingPyramid", 20, 70,
                                                           kVisibleW - 40, 600);
    chart->SetShapeMode(PyramidShapeMode::SegmentedPyramid);
    chart->SetScaleMode(PyramidScaleMode::EqualLevels);
    chart->SetApexMode(PyramidApexMode::PointApex);
    chart->SetLevelGap(6.0);
    chart->SetPyramidWidthRatio(0.85);
    chart->SetColorMode(PyramidColorMode::CategoricalPalette);
    chart->SetPalette({
        Color(58, 130, 200, 255), Color(46, 172, 162, 255),
        Color(126, 190, 78, 255), Color(240, 152, 44, 255)
    });

    chart->SetLevelLabelPlacement(PyramidLevelLabelPlacement::HideLevelLabels);
    chart->SetCalloutStyle(PyramidCalloutStyle::NumberedBlockCallouts);
    chart->SetCalloutSide(PyramidCalloutSide::CalloutsAlternating);
    chart->SetCalloutLayout(PyramidCalloutLayout::AlignedToLevel);
    chart->SetConnectorStyle(PyramidConnectorStyle::ElbowConnector);
    chart->SetCalloutColumnWidth(250.0);
    chart->SetGhostNumberStyle(30.0, 0.32);
    chart->SetCalloutFontSizes(12.0, 10.0);

    struct Block { const char* label; const char* title; const char* body; };
    const Block blocks[] = {
        {"Insight",  "Insight",  "It is now much easier and more effective to start with the "
                                 "question the business is actually asking"},
        {"Analysis", "Analysis", "Turning that question into something a dataset can answer, "
                                 "then checking the answer holds"},
        {"Delivery", "Delivery", "Shipping the finding where the decision is made, not into a "
                                 "report nobody opens"},
        {"Adoption", "Adoption", "Following through until the new behaviour is the default one"},
    };
    for (const auto& block : blocks) {
        PyramidLevel level(block.label);
        level.calloutTitle = block.title;
        level.description = block.body;
        chart->AddLevel(level);
    }
    tab->AddChild(chart);

    return tab;
}

// =============================================================================
// TAB 6: DATA PYRAMID
// =============================================================================
// The analytic side of the element: real numbers, area-proportional tiers, a
// reference shape to measure the actual proportions against, and the top-heavy
// warning that catches the ice-cream-cone anti-pattern.

std::shared_ptr<UltraCanvasUIElement> BuildDataTab() {
    auto tab = std::make_shared<UltraCanvasContainer>("PyramidDataTab", 0, 0,
                                                      kTabContentW, kTabContentH);
    AddTabHeading(tab, "Data", "Automated test suite",
                  "Area-proportional tiers, so each trapezoid's drawn area is its share of the "
                  "suite. The dashed silhouette is the 70/20/10 ideal");

    auto chart = std::make_shared<UltraCanvasPyramidChart>("DataPyramid", 20, 70, 640, 600);
    chart->SetShapeMode(PyramidShapeMode::SolidPyramid);
    chart->SetScaleMode(PyramidScaleMode::AreaProportional);
    chart->SetApexMode(PyramidApexMode::PointApex);
    chart->SetColorMode(PyramidColorMode::CategoricalPalette);
    chart->SetPalette({
        Color(198, 74, 66, 255), Color(232, 162, 58, 255), Color(96, 172, 120, 255)
    });
    chart->SetShowLevelBorder(true, Color(255, 255, 255, 255), 1.5);

    chart->SetLevelLabelPlacement(PyramidLevelLabelPlacement::OutsideStartLabels);
    chart->SetPyramidValueLabelPlacement(PyramidValueLabelPlacement::InsideValueLabels);
    chart->SetPercentPlacement(PyramidPercentPlacement::OutsideEndPercent);
    chart->SetShowPercentOfTotal(true);
    chart->SetShowRatioToLevelBelow(true);
    chart->SetLevelLabelColumnWidth(120.0);
    chart->SetPercentColumnWidth(80.0);
    chart->SetAbbreviateValues(true);

    // The shape the team says it wants, drawn behind the shape it actually has
    chart->SetReferenceShape({0.10, 0.20, 0.70});
    chart->SetHighlightTopHeavy(true);

    chart->AddLevel("End to end", 1180, "Full browser journeys");
    chart->AddLevel("Integration", 1240, "Service boundaries and contracts");
    chart->AddLevel("Unit", 4120, "Single units, no I/O");
    tab->AddChild(chart);

    auto notes = std::make_shared<UltraCanvasLabel>("DataNotes", 675, 90, 345, 560);
    notes->SetText(
        "Reading the chart\n\n"
        "The tiers are area-proportional, which is\n"
        "the setting that makes a pyramid honest.\n"
        "A pyramid's sides taper, so a band's area\n"
        "grows faster than its height: sizing tiers\n"
        "by height alone quietly overstates every\n"
        "tier near the base.\n\n"
        "Solving for the cut position instead means\n"
        "the ink a reader sees is the quantity the\n"
        "data actually holds.\n\n"
        "The reference shape\n\n"
        "The dashed silhouette and its rules are the\n"
        "70/20/10 split the team is aiming for. Read\n"
        "the gap between the drawn cut and the\n"
        "dashed one - that is the distance from the\n"
        "target, per tier.\n\n"
        "This suite is off target: 6,540 tests split\n"
        "63% unit, 19% integration and 18% end to\n"
        "end. Every dashed rule sits above the tier\n"
        "boundary it belongs to, which is the shape\n"
        "saying there is too much at the top.\n\n"
        "Top-heavy warning\n\n"
        "SetHighlightTopHeavy outlines the first\n"
        "tier that outweighs the one holding it up -\n"
        "the ice-cream-cone suite. Every tier here is\n"
        "larger than the one above it, so nothing is\n"
        "flagged - the suite is badly proportioned,\n"
        "not upside down.");
    notes->SetFontSize(10.5f);
    notes->SetTextColor(Color(70, 70, 70, 255));
    notes->SetAlignment(TextAlignment::Left, VerticalAlignment::Top);
    tab->AddChild(notes);

    return tab;
}

// =============================================================================
// TAB 7: PLAYGROUND
// =============================================================================
// Every shape, scale, direction, fill and callout mode wired to live controls,
// over a stacked revenue pyramid whose tiers carry sub-segments.

std::shared_ptr<UltraCanvasUIElement> BuildPlaygroundTab() {
    auto tab = std::make_shared<UltraCanvasContainer>("PyramidPlaygroundTab", 0, 0,
                                                      kTabContentW, kTabContentH);
    AddTabHeading(tab, "Play", "Playground",
                  "Customer base by tier, split by region. Every mode below is live");

    auto chart = std::make_shared<UltraCanvasPyramidChart>("PlayPyramid", 20, 70, 630, 600);
    chart->SetShapeMode(PyramidShapeMode::SegmentedPyramid);
    chart->SetScaleMode(PyramidScaleMode::EqualLevels);
    chart->SetApexMode(PyramidApexMode::PointApex);
    chart->SetColorMode(PyramidColorMode::CategoricalPalette);
    chart->SetPalettePreset(PyramidPalettePreset::CoolPalette);
    chart->SetLevelLabelPlacement(PyramidLevelLabelPlacement::OutsideStartLabels);
    chart->SetPyramidValueLabelPlacement(PyramidValueLabelPlacement::InsideValueLabels);
    chart->SetPercentPlacement(PyramidPercentPlacement::OutsideEndPercent);
    chart->SetShowPercentOfTotal(true);
    chart->SetLevelLabelColumnWidth(110.0);
    chart->SetPercentColumnWidth(74.0);
    chart->SetAbbreviateValues(true);
    chart->SetShowLegend(true);
    chart->SetLegendPosition(PyramidLegendPosition::LegendBottomCenter);

    // Sub-segments turn each tier into a stack: the pyramid still widens, but every
    // band also shows what it is made of.
    struct Tier {
        const char* label;
        double emea;
        double amer;
        double apac;
    };
    const Tier tiers[] = {
        {"Strategic",   40,   65,   25},
        {"Enterprise", 210,  340,  150},
        {"Mid-market", 880, 1120,  640},
        {"SMB",       3400, 4100, 2300},
    };

    const Color kEmea(64, 145, 160, 255);
    const Color kAmer(226, 152, 60, 255);
    const Color kApac(120, 110, 190, 255);

    for (const auto& row : tiers) {
        PyramidLevel level(row.label, row.emea + row.amer + row.apac);
        level.segments.push_back(PyramidSubSegment("EMEA", row.emea, kEmea));
        level.segments.push_back(PyramidSubSegment("Americas", row.amer, kAmer));
        level.segments.push_back(PyramidSubSegment("APAC", row.apac, kApac));
        level.targetValue = level.levelValue * 1.12;   // The plan for this tier
        level.description = "Accounts in this tier, split by region";
        chart->AddLevel(level);
    }
    tab->AddChild(chart);

    // ===== CONTROL PANEL =====
    int panelX = 668;
    int y = 78;

    auto controlsTitle = std::make_shared<UltraCanvasLabel>("PlayControls", panelX, y, 350, 22);
    controlsTitle->SetText("Chart controls");
    controlsTitle->SetFontSize(13);
    controlsTitle->SetFontWeight(FontWeight::Bold);
    tab->AddChild(controlsTitle);
    y += 28;

    auto shapeLabel = std::make_shared<UltraCanvasLabel>("PlayShapeLabel", panelX, y, 350, 18);
    shapeLabel->SetText("Shape mode");
    shapeLabel->SetFontSize(11);
    tab->AddChild(shapeLabel);
    y += 20;

    auto shapeDrop = std::make_shared<UltraCanvasDropdown>("PlayShape", panelX, y, 340, 26);
    shapeDrop->AddItem("Segmented tiers");
    shapeDrop->AddItem("Solid pyramid");
    shapeDrop->AddItem("Stepped ziggurat");
    shapeDrop->AddItem("Extruded 3D");
    shapeDrop->AddItem("Card rows");
    shapeDrop->AddItem("Skewed infographic");
    shapeDrop->AddItem("Exploded tiers");
    shapeDrop->AddItem("Bar style (accessible)");
    shapeDrop->SetSelectedIndex(0, false);
    shapeDrop->onSelectionChanged = [chart](int index, const DropdownItem&) {
        switch (index) {
            case 1: chart->SetShapeMode(PyramidShapeMode::SolidPyramid); break;
            case 2: chart->SetShapeMode(PyramidShapeMode::SteppedPyramid); break;
            case 3: chart->SetShapeMode(PyramidShapeMode::Pyramid3D); break;
            case 4: chart->SetShapeMode(PyramidShapeMode::CardRows); break;
            case 5: chart->SetShapeMode(PyramidShapeMode::SkewedInfographic); break;
            case 6: chart->SetShapeMode(PyramidShapeMode::ExplodedPyramid); break;
            case 7: chart->SetShapeMode(PyramidShapeMode::BarStylePyramid); break;
            default: chart->SetShapeMode(PyramidShapeMode::SegmentedPyramid); break;
        }
    };
    tab->AddChild(shapeDrop);
    y += 34;

    auto scaleLabel = std::make_shared<UltraCanvasLabel>("PlayScaleLabel", panelX, y, 350, 18);
    scaleLabel->SetText("Scale mode - what the geometry actually encodes");
    scaleLabel->SetFontSize(11);
    tab->AddChild(scaleLabel);
    y += 20;

    auto scaleDrop = std::make_shared<UltraCanvasDropdown>("PlayScale", panelX, y, 340, 26);
    scaleDrop->AddItem("Equal levels (hierarchy)");
    scaleDrop->AddItem("Height proportional");
    scaleDrop->AddItem("Width proportional");
    scaleDrop->AddItem("Area proportional (honest)");
    scaleDrop->AddItem("Volume proportional (for 3D)");
    scaleDrop->SetSelectedIndex(0, false);
    scaleDrop->onSelectionChanged = [chart](int index, const DropdownItem&) {
        switch (index) {
            case 1: chart->SetScaleMode(PyramidScaleMode::HeightProportional); break;
            case 2: chart->SetScaleMode(PyramidScaleMode::WidthProportional); break;
            case 3: chart->SetScaleMode(PyramidScaleMode::AreaProportional); break;
            case 4: chart->SetScaleMode(PyramidScaleMode::VolumeProportional); break;
            default: chart->SetScaleMode(PyramidScaleMode::EqualLevels); break;
        }
    };
    tab->AddChild(scaleDrop);
    y += 34;

    auto directionLabel = std::make_shared<UltraCanvasLabel>("PlayDirLabel", panelX, y, 350, 18);
    directionLabel->SetText("Direction");
    directionLabel->SetFontSize(11);
    tab->AddChild(directionLabel);
    y += 20;

    auto directionDrop = std::make_shared<UltraCanvasDropdown>("PlayDir", panelX, y, 340, 26);
    directionDrop->AddItem("Apex up");
    directionDrop->AddItem("Apex down (inverted)");
    directionDrop->AddItem("Apex left");
    directionDrop->AddItem("Apex right");
    directionDrop->SetSelectedIndex(0, false);
    directionDrop->onSelectionChanged = [chart](int index, const DropdownItem&) {
        switch (index) {
            case 1: chart->SetDirection(PyramidDirection::ApexDown); break;
            case 2: chart->SetDirection(PyramidDirection::ApexLeft); break;
            case 3: chart->SetDirection(PyramidDirection::ApexRight); break;
            default: chart->SetDirection(PyramidDirection::ApexUp); break;
        }
    };
    tab->AddChild(directionDrop);
    y += 34;

    auto fillLabel = std::make_shared<UltraCanvasLabel>("PlayFillLabel", panelX, y, 350, 18);
    fillLabel->SetText("Fill style (tiers here carry their own segment colours)");
    fillLabel->SetFontSize(11);
    tab->AddChild(fillLabel);
    y += 20;

    auto fillDrop = std::make_shared<UltraCanvasDropdown>("PlayFill", panelX, y, 340, 26);
    fillDrop->AddItem("Solid");
    fillDrop->AddItem("Gradient");
    fillDrop->AddItem("Outline only");
    fillDrop->AddItem("Outline with tint");
    fillDrop->SetSelectedIndex(0, false);
    fillDrop->onSelectionChanged = [chart](int index, const DropdownItem&) {
        switch (index) {
            case 1: chart->SetFillStyle(PyramidFillStyle::GradientFill); break;
            case 2: chart->SetFillStyle(PyramidFillStyle::OutlineOnly); break;
            case 3: chart->SetFillStyle(PyramidFillStyle::OutlineWithTint); break;
            default: chart->SetFillStyle(PyramidFillStyle::SolidFill); break;
        }
    };
    tab->AddChild(fillDrop);
    y += 34;

    auto apexLabel = std::make_shared<UltraCanvasLabel>("PlayApexLabel", panelX, y, 350, 18);
    apexLabel->SetText("Apex");
    apexLabel->SetFontSize(11);
    tab->AddChild(apexLabel);
    y += 20;

    auto apexDrop = std::make_shared<UltraCanvasDropdown>("PlayApex", panelX, y, 340, 26);
    apexDrop->AddItem("Closes to a point");
    apexDrop->AddItem("Closes to a plateau");
    apexDrop->AddItem("Top tier keeps a readable width");
    apexDrop->AddItem("Detached capstone");
    apexDrop->SetSelectedIndex(0, false);
    apexDrop->onSelectionChanged = [chart](int index, const DropdownItem&) {
        chart->SetApexCap(index == 3 ? PyramidApexCap::ApexSeparateCap
                                     : PyramidApexCap::ApexMergedWithTopLevel, 36.0);
        switch (index) {
            case 1: chart->SetApexMode(PyramidApexMode::FlatApex); break;
            case 2: chart->SetApexMode(PyramidApexMode::MinWidthApex); break;
            default: chart->SetApexMode(PyramidApexMode::PointApex); break;
        }
    };
    tab->AddChild(apexDrop);
    y += 34;

    auto calloutLabel = std::make_shared<UltraCanvasLabel>("PlayCalloutLabel", panelX, y, 350, 18);
    calloutLabel->SetText("Callout style");
    calloutLabel->SetFontSize(11);
    tab->AddChild(calloutLabel);
    y += 20;

    auto calloutDrop = std::make_shared<UltraCanvasDropdown>("PlayCallout", panelX, y, 340, 26);
    calloutDrop->AddItem("None");
    calloutDrop->AddItem("Plain text");
    calloutDrop->AddItem("Colour ribbons");
    calloutDrop->AddItem("Rounded cards");
    calloutDrop->AddItem("Icon cards");
    calloutDrop->AddItem("Numbered blocks");
    calloutDrop->SetSelectedIndex(0, false);
    calloutDrop->onSelectionChanged = [chart](int index, const DropdownItem&) {
        switch (index) {
            case 1: chart->SetCalloutStyle(PyramidCalloutStyle::PlainTextCallouts); break;
            case 2: chart->SetCalloutStyle(PyramidCalloutStyle::ColorRibbonCallouts); break;
            case 3: chart->SetCalloutStyle(PyramidCalloutStyle::RoundedCardCallouts); break;
            case 4: chart->SetCalloutStyle(PyramidCalloutStyle::IconCardCallouts); break;
            case 5: chart->SetCalloutStyle(PyramidCalloutStyle::NumberedBlockCallouts); break;
            default: chart->SetCalloutStyle(PyramidCalloutStyle::NoCallouts); break;
        }
    };
    tab->AddChild(calloutDrop);
    y += 36;

    auto gapLabel = std::make_shared<UltraCanvasLabel>("PlayGapLabel", panelX, y, 350, 18);
    gapLabel->SetText("Gap between tiers");
    gapLabel->SetFontSize(11);
    tab->AddChild(gapLabel);
    y += 20;

    auto gapSlider = std::make_shared<UltraCanvasSlider>("PlayGap", panelX, y, 340, 24);
    gapSlider->SetRange(0.0f, 40.0f);
    gapSlider->SetStep(1.0f);
    gapSlider->SetValue(6.0f);
    gapSlider->SetValueDisplay(SliderValueDisplay::Tooltip);
    gapSlider->onValueChanged = [chart](float value) { chart->SetLevelGap(value); };
    tab->AddChild(gapSlider);
    y += 32;

    auto halfCheck = std::make_shared<UltraCanvasCheckbox>("PlayHalf", panelX, y, 340, 24,
                                                           "Half pyramid (one vertical face)");
    halfCheck->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetHalfPyramid(newState == CheckedState::Checked);
    };
    tab->AddChild(halfCheck);
    y += 26;

    auto plinthCheck = std::make_shared<UltraCanvasCheckbox>("PlayPlinth", panelX, y, 340, 24,
                                                             "Base plinth");
    plinthCheck->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetShowBasePlinth(newState == CheckedState::Checked);
    };
    tab->AddChild(plinthCheck);
    y += 26;

    auto targetCheck = std::make_shared<UltraCanvasCheckbox>("PlayTarget", panelX, y, 340, 24,
                                                             "Target overlay (plan + 12%)");
    targetCheck->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetShowTargetOverlay(newState == CheckedState::Checked);
    };
    tab->AddChild(targetCheck);
    y += 26;

    auto topHeavyCheck = std::make_shared<UltraCanvasCheckbox>("PlayTopHeavy", panelX, y, 340, 24,
                                                               "Flag a top-heavy tier");
    topHeavyCheck->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetHighlightTopHeavy(newState == CheckedState::Checked);
    };
    tab->AddChild(topHeavyCheck);
    y += 26;

    auto dimCheck = std::make_shared<UltraCanvasCheckbox>("PlayDim", panelX, y, 340, 24,
                                                          "Dim tiers you are not hovering");
    dimCheck->onStateChanged = [chart](CheckedState, CheckedState newState) {
        chart->SetDimUnhoveredLevels(newState == CheckedState::Checked);
    };
    tab->AddChild(dimCheck);
    y += 32;

    auto selectionTitle = std::make_shared<UltraCanvasLabel>("PlaySelTitle", panelX, y, 350, 20);
    selectionTitle->SetText("Selected tier");
    selectionTitle->SetFontSize(12);
    selectionTitle->SetFontWeight(FontWeight::Bold);
    tab->AddChild(selectionTitle);
    y += 22;

    auto selection = std::make_shared<UltraCanvasLabel>("PlaySelection", panelX, y, 350, 120);
    selection->SetText("Click a tier to read its figures here.");
    selection->SetFontSize(11);
    selection->SetTextColor(Color(70, 70, 70, 255));
    selection->SetAlignment(TextAlignment::Left, VerticalAlignment::Top);
    tab->AddChild(selection);

    // The callbacks report indices into the data source, so sorting cannot shift them
    auto source = chart->GetPyramidDataSource();
    chart->onLevelClick = [selection, source, chart](size_t levelIndex) {
        if (!source || levelIndex >= source->GetPointCount()) return;
        const auto& level = source->GetLevel(levelIndex);
        PyramidLevelMetrics metrics = chart->GetLevelMetrics(levelIndex);
        chart->SetSelectedLevel(levelIndex);

        char buffer[512];
        std::snprintf(buffer, sizeof(buffer),
                      "%s\n\nAccounts: %.0f\nOf total: %.1f%%\nOf base: %.1f%%\n"
                      "Against the tier below: %.1f%%\n\nEMEA %.0f / Americas %.0f / APAC %.0f",
                      level.levelLabel.c_str(), metrics.value,
                      metrics.percentOfTotal, metrics.percentOfBase, metrics.ratioToLevelBelow,
                      level.segments.size() > 0 ? level.segments[0].value : 0.0,
                      level.segments.size() > 1 ? level.segments[1].value : 0.0,
                      level.segments.size() > 2 ? level.segments[2].value : 0.0);
        selection->SetText(buffer);
    };

    return tab;
}

} // anonymous namespace

// =============================================================================
// MAIN ENTRY POINT
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreatePyramidChartExamples() {
    auto container = std::make_shared<UltraCanvasContainer>(
        "PyramidChartExamples", 0, 0, kTabContentW + 20, kTabContentH + 60);

    auto tabs = std::make_shared<UltraCanvasTabbedContainer>(
        "PyramidChartTabs", 0, 10, kTabContentW + 10, kTabContentH + 40);
    tabs->SetTabStyle(TabStyle::Rounded);
    tabs->SetTabHeight(32);

    tabs->AddTab("3D Ribbon Pyramid", Build3DRibbonTab());
    tabs->AddTab("Spectrum 3D", BuildSpectrumTab());
    tabs->AddTab("Outline Hierarchy", BuildOutlineTab());
    tabs->AddTab("Flat & Minimal", BuildFlatTab());
    tabs->AddTab("Alternating Infographic", BuildAlternatingTab());
    tabs->AddTab("Data Pyramid", BuildDataTab());
    tabs->AddTab("Playground", BuildPlaygroundTab());
    tabs->SetActiveTab(0);

    container->AddChild(tabs);
    return container;
}

} // namespace UltraCanvas
