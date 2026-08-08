// Apps/DemoApp/UltraCanvasCircularInfoGraphicExamples.cpp
// Circular infographic demonstration: multi-ring cell layouts, value tracks,
// decorative rings and cross-ring connections
// Version: 1.1.0
// Last Modified: 2026-08-07
// Author: UltraCanvas Framework
//
// Changelog:
//   v1.1.0 (2026-08-07):
//     - Each example now sits in its own tab, so one infographic gets the whole
//       display area instead of a quarter of it.
//     - Migrated the page from absolute positioning to flex layout: the page is
//       a stretchable flex column (title, description, tabs) that fills the host
//       and grows with the window, and every tab is a flex row whose chart takes
//       all the width the control panel leaves. No manual resize handler.
//     - The control panel is per-tab and drives only that tab's chart, so its
//       controls start out showing the geometry that chart really uses (the
//       270-degree sweep, the mandala's small centre hole, and so on).

#include "UltraCanvasDemo.h"
#include "Plugins/Charts/UltraCanvasCircularInfoGraphic.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include "CSSLayout/CSSLayout.h"
#include <cmath>
#include <cstdio>

namespace UltraCanvas {

// ===== SAMPLE DATA BUILDERS =====

    static CircularCell MakeCell(const std::string& text, double value,
                                 const Color& background,
                                 const Color& textColor = Color(255, 255, 255, 255)) {
        CircularCell cell;
        cell.text = text;
        cell.value = value;
        cell.backgroundColor = background;
        cell.textColor = textColor;
        return cell;
    }

    // Three concentric levels of an organisation, each ring using a different
    // text style and value track so one chart shows most of the vocabulary.
    static void BuildCorporateRings(std::shared_ptr<UltraCanvasCircularInfoGraphic> chart) {
        // Light cell backgrounds let the value bars be the visual: the bar
        // length reads the headcount and the scale colours it, which would be
        // lost against saturated per-cell colours.
        CircularRing exec;
        exec.label = "Executive";
        exec.textStyle = CircularTextStyle::Horizontal;
        exec.valueType = ValueVisualizationType::Bars;
        const Color execBg(238, 241, 248, 255);
        const Color execFg(35, 40, 60, 255);
        exec.cells = {
            MakeCell("CEO",  96.0, execBg, execFg),
            MakeCell("CTO",  72.0, execBg, execFg),
            MakeCell("CFO",  48.0, execBg, execFg),
            MakeCell("COO",  61.0, execBg, execFg)
        };
        chart->AddRing(exec);

        CircularRing depts;
        depts.label = "Departments";
        depts.textStyle = CircularTextStyle::Circular;
        depts.valueType = ValueVisualizationType::CircularBar;
        depts.cells = {
            MakeCell("Eng",   85.0, Color(100, 149, 237, 255)),
            MakeCell("Mktg",  78.0, Color(255,  99,  71, 255)),
            MakeCell("Sales", 82.0, Color( 60, 179, 113, 255)),
            MakeCell("HR",    75.0, Color(219, 112, 147, 255)),
            MakeCell("Fin",   80.0, Color(218, 165,  32, 255)),
            MakeCell("Ops",   77.0, Color(147, 112, 219, 255)),
            MakeCell("Legal", 70.0, Color(128, 128, 128, 255)),
            MakeCell("IT",    73.0, Color( 32, 178, 170, 255))
        };
        chart->AddRing(depts);

        CircularRing teams;
        teams.label = "Teams";
        teams.textStyle = CircularTextStyle::StarStyle;
        // A line track reads the ring as a closed series rather than as
        // independent cells, which suits per-team scores.
        teams.valueType = ValueVisualizationType::CircularLine;
        teams.valueColor = Color(30, 60, 200, 255);
        teams.decorativeType = DecorativeRingType::ColorHighlight;
        teams.decorativeColor = Color(90, 90, 100, 190);
        const char* names[] = {"Frontend", "Backend", "DevOps", "QA", "UI/UX",
                               "Content", "SEO", "Social", "Accounts", "BizDev"};
        const double scores[] = {88, 92, 85, 78, 83, 70, 75, 73, 80, 77};
        for (int i = 0; i < 10; ++i) {
            CircularCell cell = MakeCell(names[i], scores[i],
                                         Color(232, 238, 250, 255),
                                         Color(45, 45, 60, 255));
            // Grouping marks the two halves of the org that report separately.
            cell.groupName = (i < 5) ? "Product" : "Growth";
            teams.cells.push_back(cell);
        }
        chart->AddRing(teams);

        chart->SetConnectionLineType(ConnectionLineType::InsideCenter);
        chart->AddConnection(0, 0, 1, 0, 90.0);   // CEO   -> Eng
        chart->AddConnection(0, 1, 1, 7, 70.0);   // CTO   -> IT
        chart->AddConnection(0, 2, 1, 4, 60.0);   // CFO   -> Fin
        chart->AddConnection(1, 0, 2, 0, 55.0);   // Eng   -> Frontend
        chart->AddConnection(1, 0, 2, 1, 55.0);   // Eng   -> Backend
    }

    // Partial sweep: two levels across 270 degrees, leaving a deliberate gap.
    static void BuildTechEcosystemRings(std::shared_ptr<UltraCanvasCircularInfoGraphic> chart) {
        CircularRing core;
        core.label = "Core Technologies";
        core.textStyle = CircularTextStyle::Radial;
        core.valueType = ValueVisualizationType::Bars;
        core.valueColor = Color(25, 60, 180, 255);
        core.cells = {
            MakeCell("Cloud",      95.0, Color( 25,  25, 112, 255)),
            MakeCell("AI/ML",      88.0, Color(128,   0, 128, 255)),
            MakeCell("Blockchain", 72.0, Color(184, 134,  11, 255)),
            MakeCell("IoT",        85.0, Color(  0, 128,   0, 255)),
            MakeCell("5G",         78.0, Color(220,  20,  60, 255))
        };
        chart->AddRing(core);

        CircularRing apps;
        apps.label = "Applications";
        apps.textStyle = CircularTextStyle::StarStyle;
        apps.valueType = ValueVisualizationType::CircularBar;
        apps.decorativeType = DecorativeRingType::Pattern;
        apps.decorativeColor = Color(150, 160, 175, 160);
        const char* names[] = {"FinTech", "HealthTech", "EdTech", "RetailTech",
                               "AutoTech", "AgriTech", "GovTech"};
        const double adoption[] = {92, 89, 84, 87, 81, 76, 79};
        for (int i = 0; i < 7; ++i) {
            apps.cells.push_back(MakeCell(names[i], adoption[i],
                                          Color(214, 232, 214, 255),
                                          Color(40, 55, 40, 255)));
        }
        chart->AddRing(apps);

        chart->SetTotalAngle(270.0f);
        chart->SetAngleOffset(-135.0f);
        chart->SetConnectionLineType(ConnectionLineType::DirectCurve);
        chart->AddConnection(0, 0, 1, 0, 80.0);   // Cloud      -> FinTech
        chart->AddConnection(0, 1, 1, 1, 75.0);   // AI/ML      -> HealthTech
        chart->AddConnection(0, 2, 1, 0, 45.0);   // Blockchain -> FinTech
        chart->AddConnection(0, 3, 1, 5, 60.0);   // IoT        -> AgriTech
    }

    // Circos-style multi-track layout: a categorical ideogram ring, a value
    // line track, a progress track, and links drawn across the centre.
    static void BuildGenomicTrackRings(std::shared_ptr<UltraCanvasCircularInfoGraphic> chart) {
        // Track 1 (innermost): chromosome-style ideogram bands.
        CircularRing ideogram;
        ideogram.label = "Segments";
        ideogram.textStyle = CircularTextStyle::Circular;
        ideogram.borderColor = Color(255, 255, 255, 200);
        ideogram.borderWidth = 1.0f;
        for (int i = 0; i < 12; ++i) {
            uint8_t shade = static_cast<uint8_t>(60 + ((i * 37) % 150));
            CircularCell cell = MakeCell("S" + std::to_string(i + 1), 0.0,
                                         Color(shade, shade, static_cast<uint8_t>(shade + 40), 255));
            ideogram.cells.push_back(cell);
        }
        chart->AddRing(ideogram);

        // Track 2: coverage as a closed line series.
        CircularRing coverage;
        coverage.label = "Coverage";
        coverage.textStyle = CircularTextStyle::Horizontal;
        coverage.valueType = ValueVisualizationType::CircularLine;
        coverage.valueColor = Color(200, 40, 60, 255);
        coverage.borderColor = Color(230, 230, 235, 255);
        coverage.borderWidth = 0.5f;
        for (int i = 0; i < 36; ++i) {
            double v = 45.0 + 40.0 * std::sin(i * 0.5) + 12.0 * std::sin(i * 1.7);
            CircularCell cell = MakeCell("", v, Color(250, 250, 252, 255));
            coverage.cells.push_back(cell);
        }
        chart->AddRing(coverage);

        // Track 3 (outermost): per-bin density as progress arcs.
        CircularRing density;
        density.label = "Density";
        density.textStyle = CircularTextStyle::Horizontal;
        density.valueType = ValueVisualizationType::CircularBar;
        density.decorativeType = DecorativeRingType::Gradient;
        density.decorativeColor = Color(60, 90, 190, 70);
        density.borderColor = Color(235, 235, 240, 255);
        density.borderWidth = 0.5f;
        for (int i = 0; i < 36; ++i) {
            double v = 30.0 + 55.0 * std::fabs(std::sin(i * 0.31 + 0.7));
            CircularCell cell = MakeCell("", v, Color(246, 248, 252, 255));
            density.cells.push_back(cell);
        }
        chart->AddRing(density);

        chart->SetInnerRadiusFraction(0.30f);
        chart->SetRingSpacing(3.0f);
        chart->SetCellSpacingAngle(0.0f);
        chart->SetConnectionLineType(ConnectionLineType::InsideCenter);

        // Links between segments, weighted so thickness varies.
        const int links[][3] = {
            {0, 5, 90}, {1, 8, 70}, {2, 6, 55}, {3, 10, 85},
            {4, 9, 40}, {7, 11, 65}, {0, 9, 50}, {2, 11, 75}
        };
        ConnectionStyle linkStyle;
        linkStyle.drawStyle = ConnectionDrawStyle::CurvedLine;
        linkStyle.color = Color(50, 90, 160, 150);
        linkStyle.baseThickness = 1.0f;
        linkStyle.proportionalThickness = 4.0f;
        chart->SetDefaultConnectionStyle(linkStyle);
        for (const auto& l : links) {
            chart->AddConnection(0, static_cast<size_t>(l[0]),
                                 0, static_cast<size_t>(l[1]),
                                 static_cast<double>(l[2]));
        }
    }

    // Decorative five-ring pattern with empty cells punched out.
    static void BuildMandalaRings(std::shared_ptr<UltraCanvasCircularInfoGraphic> chart) {
        // Deterministic pseudo-random so the demo looks identical every run.
        unsigned seed = 20260729u;
        auto nextRand = [&seed]() {
            seed = seed * 1103515245u + 12345u;
            return static_cast<double>((seed >> 16) & 0x7FFFu) / 32767.0;
        };

        for (int level = 0; level < 5; ++level) {
            CircularRing ring;
            ring.textStyle = CircularTextStyle::Horizontal;
            ring.decorativeType = (level % 2 == 0) ? DecorativeRingType::Gradient
                                                   : DecorativeRingType::None;
            ring.decorativeColor = Color(120, 90, 200, 80);
            ring.borderColor = Color(25, 25, 35, 255);
            ring.borderWidth = 0.5f;

            int cellCount = 8 + level * 4;   // 8, 12, 16, 20, 24
            for (int i = 0; i < cellCount; ++i) {
                CircularCell cell;
                cell.isEmpty = nextRand() < 0.3;   // punch holes for the pattern
                double t = static_cast<double>((level * 60 + i * 15) % 360) / 360.0;
                cell.backgroundColor = Color(
                    static_cast<uint8_t>(120 + 120 * std::sin(t * 6.2832)),
                    static_cast<uint8_t>(120 + 120 * std::sin(t * 6.2832 + 2.094)),
                    static_cast<uint8_t>(120 + 120 * std::sin(t * 6.2832 + 4.189)), 255);
                ring.cells.push_back(cell);
            }
            chart->AddRing(ring);
        }

        chart->SetInnerRadiusFraction(0.12f);
        chart->SetRingSpacing(1.0f);
        chart->SetConnectionLineType(ConnectionLineType::OutsideRing);
        for (int i = 0; i < 8; ++i) {
            chart->AddConnection(0, static_cast<size_t>(i % 8),
                                 4, static_cast<size_t>((i * 3) % 24));
        }
    }

// ===== CONTROL PANEL =====

    // The panel starts from the geometry its own chart was built with, so a
    // control never shows a value the chart does not actually use.
    struct CircularPanelDefaults {
        float sweepAngle      = 360.0f;
        float rotation        = -90.0f;
        float innerFraction   = 0.25f;
        float ringGap         = 2.0f;
        float cellGap         = 0.5f;
        int   connectionIndex = 0;   // Centre / Curve / Outside / Hidden
    };

    static constexpr int kCircularPanelWidth   = 214;
    static constexpr int kCircularControlWidth = 190;

    // Fixed-width side column driving a single chart. A side column costs a
    // circular infographic nothing: the rings are sized by the smaller of width
    // and height, and on a normal window that is the height.
    static std::shared_ptr<UltraCanvasContainer> CreateCircularControlPanel(
        const std::string& idPrefix,
        std::shared_ptr<UltraCanvasCircularInfoGraphic> chart,
        const CircularPanelDefaults& defaults)
    {
        auto panel = std::make_shared<UltraCanvasContainer>(idPrefix + "Panel",
                                                           kCircularPanelWidth, 0);
        panel->SetBackgroundColor(Color(247, 247, 250, 255));
        panel->SetPadding(8, 10);
        panel->layout.SetFlexColumn().SetFlexGap(4)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Start);

        // Controls are in flow (no explicit origin) and never grow or shrink,
        // so the column keeps its natural height whatever the window does.
        auto addHeader = [&](const std::string& suffix, const std::string& text) {
            auto header = std::make_shared<UltraCanvasLabel>(
                idPrefix + suffix, kCircularControlWidth, 17, text);
            header->SetFontSize(11);
            header->SetFontWeight(FontWeight::Bold);
            header->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
            panel->AddChild(header);
        };

        auto addSlider = [&](const std::string& suffix, float minValue, float maxValue,
                             float step, float value, std::function<void(float)> onChange) {
            auto slider = std::make_shared<UltraCanvasSlider>(
                idPrefix + suffix, kCircularControlWidth, 24);
            slider->SetRange(minValue, maxValue);
            slider->SetStep(step);
            slider->SetValue(value);
            slider->onValueChanged = std::move(onChange);
            slider->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
            panel->AddChild(slider);
        };

        addHeader("SweepHeader", "Sweep Angle (degrees):");
        addSlider("Sweep", 90.0f, 360.0f, 5.0f, defaults.sweepAngle,
                  [chart](float value) { chart->SetTotalAngle(value); });

        addHeader("RotateHeader", "Rotation (degrees):");
        addSlider("Rotate", -180.0f, 180.0f, 5.0f, defaults.rotation,
                  [chart](float value) { chart->SetAngleOffset(value); });

        // SetRadiusRange would pin explicit pixel radii; the fraction keeps the
        // chart auto-fitting to whatever the tab gives it.
        addHeader("HoleHeader", "Centre Hole:");
        addSlider("Hole", 0.05f, 0.6f, 0.01f, defaults.innerFraction,
                  [chart](float value) { chart->SetInnerRadiusFraction(value); });

        addHeader("RingGapHeader", "Ring Gap (px):");
        addSlider("RingGap", 0.0f, 12.0f, 0.5f, defaults.ringGap,
                  [chart](float value) { chart->SetRingSpacing(value); });

        addHeader("CellGapHeader", "Cell Gap (degrees):");
        addSlider("CellGap", 0.0f, 6.0f, 0.25f, defaults.cellGap,
                  [chart](float value) { chart->SetCellSpacingAngle(value); });

        // The chart is built with deliberate per-ring text styles; capture them
        // so "Per-ring" can restore the authored look after an override.
        auto originalStyles = std::make_shared<std::vector<CircularTextStyle>>();
        for (size_t r = 0; r < chart->GetRingCount(); ++r) {
            const CircularRing* ring = chart->GetRing(r);
            originalStyles->push_back(ring ? ring->textStyle : CircularTextStyle::Horizontal);
        }

        addHeader("TextHeader", "Cell Text Style:");
        auto textDropdown = std::make_shared<UltraCanvasDropdown>(
            idPrefix + "TextStyle", kCircularControlWidth, 24);
        textDropdown->AddItem("Per-ring (authored)");
        textDropdown->AddItem("Horizontal");
        textDropdown->AddItem("Circular");
        textDropdown->AddItem("Radial");
        textDropdown->AddItem("Star (outboard)");
        textDropdown->SetSelectedIndex(0);
        textDropdown->onSelectionChanged = [chart, originalStyles](int index, const DropdownItem&) {
            for (size_t r = 0; r < chart->GetRingCount(); ++r) {
                CircularTextStyle style;
                switch (index) {
                    case 1:  style = CircularTextStyle::Horizontal; break;
                    case 2:  style = CircularTextStyle::Circular;   break;
                    case 3:  style = CircularTextStyle::Radial;     break;
                    case 4:  style = CircularTextStyle::StarStyle;  break;
                    default:
                        style = (r < originalStyles->size()) ? (*originalStyles)[r]
                                                             : CircularTextStyle::Horizontal;
                        break;
                }
                chart->SetRingTextStyle(r, style);
            }
        };
        textDropdown->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        panel->AddChild(textDropdown);

        addHeader("ConnHeader", "Connection Routing:");
        auto connDropdown = std::make_shared<UltraCanvasDropdown>(
            idPrefix + "ConnType", kCircularControlWidth, 24);
        connDropdown->AddItem("Through Centre");
        connDropdown->AddItem("Direct Curve");
        connDropdown->AddItem("Around Outside");
        connDropdown->AddItem("Hidden");
        connDropdown->SetSelectedIndex(defaults.connectionIndex);
        connDropdown->onSelectionChanged = [chart](int index, const DropdownItem&) {
            ConnectionLineType type;
            switch (index) {
                case 1:  type = ConnectionLineType::DirectCurve; break;
                case 2:  type = ConnectionLineType::OutsideRing; break;
                case 3:  type = ConnectionLineType::None;        break;
                default: type = ConnectionLineType::InsideCenter; break;
            }
            chart->SetConnectionLineType(type);
        };
        connDropdown->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        panel->AddChild(connDropdown);

        auto groupCheckbox = std::make_shared<UltraCanvasCheckbox>(
            idPrefix + "Groups", kCircularControlWidth, 24);
        groupCheckbox->SetText("Show Group Outlines");
        groupCheckbox->SetChecked(false);
        groupCheckbox->onStateChanged = [chart](CheckedState, CheckedState newState) {
            chart->SetShowGroupOutlines(newState == CheckedState::Checked);
        };
        groupCheckbox->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        panel->AddChild(groupCheckbox);

        auto hoverCheckbox = std::make_shared<UltraCanvasCheckbox>(
            idPrefix + "Hover", kCircularControlWidth, 24);
        hoverCheckbox->SetText("Hover Highlight");
        hoverCheckbox->SetChecked(true);
        hoverCheckbox->onStateChanged = [chart](CheckedState, CheckedState newState) {
            chart->SetHoverHighlightEnabled(newState == CheckedState::Checked);
        };
        hoverCheckbox->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        panel->AddChild(hoverCheckbox);

        auto hintLabel = std::make_shared<UltraCanvasLabel>(
            idPrefix + "Hint", kCircularControlWidth, 66,
            "Hover a cell for its value and\ngroup. Sweep below 360 leaves a\ngap: clicks in that gap are\nignored, not misrouted.");
        hintLabel->SetFontSize(10);
        hintLabel->SetTextColor(Color(110, 110, 110, 255));
        hintLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        panel->AddChild(hintLabel);

        return panel;
    }

// ===== TAB ASSEMBLY =====

    // One tab: a caption over a chart that takes every pixel the control panel
    // leaves, both horizontally and vertically.
    static std::shared_ptr<UltraCanvasContainer> MakeCircularTab(
        const std::string& idPrefix,
        const std::string& caption,
        std::shared_ptr<UltraCanvasCircularInfoGraphic> chart,
        const CircularPanelDefaults& defaults)
    {
        auto tab = std::make_shared<UltraCanvasContainer>(idPrefix + "Tab");
        tab->layout.SetFlexRow().SetFlexGap(8)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        tab->SetPadding(6);

        auto chartColumn = std::make_shared<UltraCanvasContainer>(idPrefix + "ChartColumn");
        chartColumn->layout.SetFlexColumn().SetFlexGap(4)
                           .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        chartColumn->layoutItem.SetFlex(1, 1, CSSLayout::Dimension::Px(0))
                               .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        auto captionLabel = std::make_shared<UltraCanvasLabel>(
            idPrefix + "Caption", 0, 22, caption);
        captionLabel->SetFontSize(12);
        captionLabel->SetTextColor(Color(90, 90, 100, 255));
        captionLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        chartColumn->AddChild(captionLabel);

        // The chart is built at zero size and sized entirely by the layout
        // engine, so it re-fits its rings whenever the window changes.
        chart->layoutItem.SetFlex(1, 1, CSSLayout::Dimension::Px(0))
                         .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        chartColumn->AddChild(chart);
        tab->AddChild(chartColumn);

        auto panel = CreateCircularControlPanel(idPrefix, chart, defaults);
        panel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        tab->AddChild(panel);

        return tab;
    }

    // ----- TAB 1: CORPORATE HIERARCHY -----
    static std::shared_ptr<UltraCanvasContainer> MakeCorporateTab() {
        auto chart = CreateCircularInfoGraphic("CorporateCircular", 0, 0, 0, 0);
        BuildCorporateRings(chart);
        chart->SetCenterText("Org");
        chart->SetTooltipsEnabled(true);

        return MakeCircularTab(
            "CiCorp",
            "Three rings, one per org level: horizontal labels with radial bars, circular labels with\n"
            "progress arcs, and star labels over a closed line track. Connections route through the centre.",
            chart, CircularPanelDefaults{});
    }

    // ----- TAB 2: TECH ECOSYSTEM, 270 DEGREE PARTIAL -----
    static std::shared_ptr<UltraCanvasContainer> MakeTechEcosystemTab() {
        auto chart = CreateCircularInfoGraphic("TechCircular", 0, 0, 0, 0);
        BuildTechEcosystemRings(chart);
        chart->SetTooltipsEnabled(true);

        CircularPanelDefaults defaults;
        defaults.sweepAngle      = 270.0f;
        defaults.rotation        = -135.0f;
        defaults.connectionIndex = 1;   // Direct Curve
        return MakeCircularTab(
            "CiTech",
            "A 270-degree sweep leaves a quadrant open: radial labels inside, star labels with leader lines\n"
            "outside, and connections drawn as direct curves rather than through the hole.",
            chart, defaults);
    }

    // ----- TAB 3: CIRCOS-STYLE MULTI-TRACK -----
    static std::shared_ptr<UltraCanvasContainer> MakeGenomicTrackTab() {
        auto chart = CreateCircularInfoGraphic("TrackCircular", 0, 0, 0, 0);
        BuildGenomicTrackRings(chart);
        chart->SetTooltipsEnabled(true);

        CircularPanelDefaults defaults;
        defaults.innerFraction = 0.30f;
        defaults.ringGap       = 3.0f;
        defaults.cellGap       = 0.0f;
        return MakeCircularTab(
            "CiTrack",
            "Circos-style stacked tracks: a categorical ideogram, a 36-bin coverage line, a density arc\n"
            "track, and weighted links between segments whose thickness follows the link value.",
            chart, defaults);
    }

    // ----- TAB 4: DECORATIVE MANDALA -----
    static std::shared_ptr<UltraCanvasContainer> MakeMandalaTab() {
        auto chart = CreateCircularInfoGraphic("MandalaCircular", 0, 0, 0, 0);
        BuildMandalaRings(chart);
        chart->SetShowBackground(true);
        chart->SetBackgroundColor(Color(22, 22, 32, 255));
        chart->SetCenterColor(Color(245, 245, 250, 255), Color(90, 90, 110, 255));
        chart->SetTooltipsEnabled(false);

        CircularPanelDefaults defaults;
        defaults.innerFraction   = 0.12f;
        defaults.ringGap         = 1.0f;
        defaults.connectionIndex = 2;   // Around Outside
        return MakeCircularTab(
            "CiMandala",
            "Five rings of 8 to 24 cells with roughly a third of them punched out, hue cycling by ring and\n"
            "position, and connections routed around the outside of the outermost ring.",
            chart, defaults);
    }

// ===== MAIN CIRCULAR INFOGRAPHIC EXAMPLES CREATOR =====

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateCircularInfoGraphicExamples() {
        // Stretchable flex column: title and description keep their height, the
        // tabs take the rest. Auto size so the host's flex-grow/stretch sizes the
        // page to the display area and a window resize propagates down through
        // Measure/Arrange to each chart.
        auto container = std::make_shared<UltraCanvasContainer>("CircularInfoContainer");
        container->SetBackgroundColor(Color(255, 255, 255, 255));
        container->SetPadding(8, 10);
        container->layout.SetFlexColumn().SetFlexGap(6)
                         .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        container->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        auto titleLabel = std::make_shared<UltraCanvasLabel>("CiTitleLabel", 0, 32);
        titleLabel->SetText("Circular Info Graphic Examples - Multi-Ring Cell Layouts");
        titleLabel->SetFontSize(18);
        titleLabel->SetFontWeight(FontWeight::Bold);
        titleLabel->SetAlignment(TextAlignment::Center);
        titleLabel->SetBackgroundColor(Color(240, 240, 250, 255));
        titleLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        container->AddChild(titleLabel);

        // Explicit line breaks and a fixed height rather than word wrap: the
        // description is re-measured whenever the page is re-laid out, and a
        // wrapped label re-measures against an unconstrained width, which leaves
        // the text on one over-long line that the page then clips.
        auto descLabel = std::make_shared<UltraCanvasLabel>("CiDescLabel", 0, 42);
        descLabel->SetText(
            "Concentric rings of individually styled cells. Each ring picks its own text style (horizontal, glyph-by-glyph\n"
            "circular, radial, or star-style with leader lines) and its own value track (radial bars, progress arcs, or a\n"
            "closed line series). Connections link cells across rings, through the centre, directly, or around the outside."
        );
        descLabel->SetFontSize(11);
        descLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        container->AddChild(descLabel);

        auto tabs = std::make_shared<UltraCanvasTabbedContainer>("CircularInfoTabs", 0, 0, 0, 0);
        tabs->SetTabPosition(TabPosition::Top);
        tabs->SetTabStyle(TabStyle::Modern);
        tabs->layoutItem.SetFlex(1, 1, CSSLayout::Dimension::Px(0))
                        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        tabs->AddTab("Corporate Hierarchy",       MakeCorporateTab());
        tabs->AddTab("Technology Ecosystem",      MakeTechEcosystemTab());
        tabs->AddTab("Multi-Track Genomic Style", MakeGenomicTrackTab());
        tabs->AddTab("Decorative Mandala",        MakeMandalaTab());

        container->AddChild(tabs);

        return container;
    }

} // namespace UltraCanvas
