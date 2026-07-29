// Apps/DemoApp/UltraCanvasCircularInfoGraphicExamples.cpp
// Circular infographic demonstration: multi-ring cell layouts, value tracks,
// decorative rings and cross-ring connections
// Version: 1.0.0
// Last Modified: 2026-07-29
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Charts/UltraCanvasCircularInfoGraphic.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
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

    static void CreateCircularControlPanel(
        std::shared_ptr<UltraCanvasContainer> container,
        std::vector<std::shared_ptr<UltraCanvasCircularInfoGraphic>> charts,
        int x, int y)
    {
        int yOffset = y;
        const int controlHeight = 25;
        const int spacing = 8;

        // Each chart is built with deliberate per-ring text styles; capture
        // them so the style dropdown can restore the authored look.
        auto originalStyles = std::make_shared<std::vector<std::vector<CircularTextStyle>>>();
        for (auto& c : charts) {
            std::vector<CircularTextStyle> styles;
            for (size_t r = 0; r < c->GetRingCount(); ++r) {
                const CircularRing* ring = c->GetRing(r);
                styles.push_back(ring ? ring->textStyle : CircularTextStyle::Horizontal);
            }
            originalStyles->push_back(styles);
        }

        auto geometryHeader = std::make_shared<UltraCanvasLabel>("CiGeomHeader", x, yOffset, 200, 20);
        geometryHeader->SetText("Sweep Angle (degrees):");
        geometryHeader->SetFontWeight(FontWeight::Bold);
        container->AddChild(geometryHeader);
        yOffset += 25;

        auto sweepSlider = std::make_shared<UltraCanvasSlider>("CiSweep", x, yOffset, 180, controlHeight);
        sweepSlider->SetRange(90.0f, 360.0f);
        sweepSlider->SetStep(5.0f);
        sweepSlider->SetValue(360.0f);
        sweepSlider->onValueChanged = [charts](float value) {
            for (auto& c : charts) c->SetTotalAngle(value);
        };
        container->AddChild(sweepSlider);
        yOffset += controlHeight + spacing;

        auto rotateHeader = std::make_shared<UltraCanvasLabel>("CiRotateHeader", x, yOffset, 200, 20);
        rotateHeader->SetText("Rotation (degrees):");
        rotateHeader->SetFontWeight(FontWeight::Bold);
        container->AddChild(rotateHeader);
        yOffset += 25;

        auto rotateSlider = std::make_shared<UltraCanvasSlider>("CiRotate", x, yOffset, 180, controlHeight);
        rotateSlider->SetRange(-180.0f, 180.0f);
        rotateSlider->SetStep(5.0f);
        rotateSlider->SetValue(-90.0f);
        rotateSlider->onValueChanged = [charts](float value) {
            for (auto& c : charts) c->SetAngleOffset(value);
        };
        container->AddChild(rotateSlider);
        yOffset += controlHeight + spacing;

        auto holeHeader = std::make_shared<UltraCanvasLabel>("CiHoleHeader", x, yOffset, 200, 20);
        holeHeader->SetText("Centre Hole:");
        holeHeader->SetFontWeight(FontWeight::Bold);
        container->AddChild(holeHeader);
        yOffset += 25;

        auto holeSlider = std::make_shared<UltraCanvasSlider>("CiHole", x, yOffset, 180, controlHeight);
        holeSlider->SetRange(0.05f, 0.6f);
        holeSlider->SetStep(0.01f);
        holeSlider->SetValue(0.25f);
        holeSlider->onValueChanged = [charts](float value) {
            // SetRadiusRange would pin explicit pixel radii; the fraction keeps
            // every chart auto-fitting to its own panel.
            for (auto& c : charts) c->SetInnerRadiusFraction(value);
        };
        container->AddChild(holeSlider);
        yOffset += controlHeight + spacing;

        auto ringGapHeader = std::make_shared<UltraCanvasLabel>("CiRingGapHeader", x, yOffset, 200, 20);
        ringGapHeader->SetText("Ring Gap (px):");
        ringGapHeader->SetFontWeight(FontWeight::Bold);
        container->AddChild(ringGapHeader);
        yOffset += 25;

        auto ringGapSlider = std::make_shared<UltraCanvasSlider>("CiRingGap", x, yOffset, 180, controlHeight);
        ringGapSlider->SetRange(0.0f, 12.0f);
        ringGapSlider->SetStep(0.5f);
        ringGapSlider->SetValue(2.0f);
        ringGapSlider->onValueChanged = [charts](float value) {
            for (auto& c : charts) c->SetRingSpacing(value);
        };
        container->AddChild(ringGapSlider);
        yOffset += controlHeight + spacing;

        auto cellGapHeader = std::make_shared<UltraCanvasLabel>("CiCellGapHeader", x, yOffset, 200, 20);
        cellGapHeader->SetText("Cell Gap (degrees):");
        cellGapHeader->SetFontWeight(FontWeight::Bold);
        container->AddChild(cellGapHeader);
        yOffset += 25;

        auto cellGapSlider = std::make_shared<UltraCanvasSlider>("CiCellGap", x, yOffset, 180, controlHeight);
        cellGapSlider->SetRange(0.0f, 6.0f);
        cellGapSlider->SetStep(0.25f);
        cellGapSlider->SetValue(0.5f);
        cellGapSlider->onValueChanged = [charts](float value) {
            for (auto& c : charts) c->SetCellSpacingAngle(value);
        };
        container->AddChild(cellGapSlider);
        yOffset += controlHeight + spacing * 2;

        auto textHeader = std::make_shared<UltraCanvasLabel>("CiTextHeader", x, yOffset, 200, 20);
        textHeader->SetText("Cell Text Style:");
        textHeader->SetFontWeight(FontWeight::Bold);
        container->AddChild(textHeader);
        yOffset += 25;

        auto textDropdown = std::make_shared<UltraCanvasDropdown>("CiTextStyle", x, yOffset, 180, controlHeight);
        textDropdown->AddItem("Per-ring (authored)");
        textDropdown->AddItem("Horizontal");
        textDropdown->AddItem("Circular");
        textDropdown->AddItem("Radial");
        textDropdown->AddItem("Star (outboard)");
        textDropdown->SetSelectedIndex(0);
        textDropdown->onSelectionChanged = [charts, originalStyles](int index, const DropdownItem&) {
            for (size_t ci = 0; ci < charts.size(); ++ci) {
                auto& chart = charts[ci];
                for (size_t r = 0; r < chart->GetRingCount(); ++r) {
                    CircularTextStyle style;
                    switch (index) {
                        case 1:  style = CircularTextStyle::Horizontal; break;
                        case 2:  style = CircularTextStyle::Circular;   break;
                        case 3:  style = CircularTextStyle::Radial;     break;
                        case 4:  style = CircularTextStyle::StarStyle;  break;
                        default:
                            style = (ci < originalStyles->size() && r < (*originalStyles)[ci].size())
                                  ? (*originalStyles)[ci][r] : CircularTextStyle::Horizontal;
                            break;
                    }
                    chart->SetRingTextStyle(r, style);
                }
            }
        };
        container->AddChild(textDropdown);
        yOffset += controlHeight + spacing;

        auto connHeader = std::make_shared<UltraCanvasLabel>("CiConnHeader", x, yOffset, 200, 20);
        connHeader->SetText("Connection Routing:");
        connHeader->SetFontWeight(FontWeight::Bold);
        container->AddChild(connHeader);
        yOffset += 25;

        auto connDropdown = std::make_shared<UltraCanvasDropdown>("CiConnType", x, yOffset, 180, controlHeight);
        connDropdown->AddItem("Through Centre");
        connDropdown->AddItem("Direct Curve");
        connDropdown->AddItem("Around Outside");
        connDropdown->AddItem("Hidden");
        connDropdown->SetSelectedIndex(0);
        connDropdown->onSelectionChanged = [charts](int index, const DropdownItem&) {
            ConnectionLineType type;
            switch (index) {
                case 1:  type = ConnectionLineType::DirectCurve; break;
                case 2:  type = ConnectionLineType::OutsideRing; break;
                case 3:  type = ConnectionLineType::None;        break;
                default: type = ConnectionLineType::InsideCenter; break;
            }
            for (auto& c : charts) c->SetConnectionLineType(type);
        };
        container->AddChild(connDropdown);
        yOffset += controlHeight + spacing * 2;

        auto groupCheckbox = std::make_shared<UltraCanvasCheckbox>("CiGroups", x, yOffset, 180, controlHeight);
        groupCheckbox->SetText("Show Group Outlines");
        groupCheckbox->SetChecked(false);
        groupCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
            for (auto& c : charts) c->SetShowGroupOutlines(newState == CheckedState::Checked);
        };
        container->AddChild(groupCheckbox);
        yOffset += controlHeight + spacing;

        auto hoverCheckbox = std::make_shared<UltraCanvasCheckbox>("CiHover", x, yOffset, 180, controlHeight);
        hoverCheckbox->SetText("Hover Highlight");
        hoverCheckbox->SetChecked(true);
        hoverCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
            for (auto& c : charts) c->SetHoverHighlightEnabled(newState == CheckedState::Checked);
        };
        container->AddChild(hoverCheckbox);
        yOffset += controlHeight + spacing;

        auto hintLabel = std::make_shared<UltraCanvasLabel>("CiHint", x, yOffset, 200, 70);
        hintLabel->SetText("Hover a cell for its value and\ngroup. Sweep below 360 leaves a\ngap: clicks in that gap are\nignored, not misrouted.");
        hintLabel->SetFontSize(10);
        hintLabel->SetTextColor(Color(110, 110, 110, 255));
        container->AddChild(hintLabel);
    }

// ===== MAIN CIRCULAR INFOGRAPHIC EXAMPLES CREATOR =====

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateCircularInfoGraphicExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("CircularInfoContainer", 0, 0, 1200, 810);

        // === TITLE ===
        auto titleLabel = std::make_shared<UltraCanvasLabel>("CiTitleLabel", 20, 10, 1160, 35);
        titleLabel->SetText("Circular Info Graphic Examples - Multi-Ring Cell Layouts");
        titleLabel->SetFontSize(18);
        titleLabel->SetFontWeight(FontWeight::Bold);
        titleLabel->SetAlignment(TextAlignment::Center);
        titleLabel->SetBackgroundColor(Color(240, 240, 250, 255));
        container->AddChild(titleLabel);

        // === DESCRIPTION ===
        auto descLabel = std::make_shared<UltraCanvasLabel>("CiDescLabel", 20, 55, 1160, 60);
        descLabel->SetText(
            "Concentric rings of individually styled cells. Each ring picks its own text style (horizontal, glyph-by-glyph\n"
            "circular, radial, or star-style with leader lines) and its own value track (radial bars, progress arcs, or a\n"
            "closed line series). Cells carry colours, images, groups and values; connections link cells across rings and\n"
            "route through the centre, directly, or around the outside."
        );
        descLabel->SetFontSize(11);
        descLabel->SetWrap(TextWrap::WrapWord);
        container->AddChild(descLabel);

        // ===== EXAMPLE 1: CORPORATE HIERARCHY =====
        auto corpLabel = std::make_shared<UltraCanvasLabel>("CiCorpLabel", 240, 130, 460, 25);
        corpLabel->SetText("Corporate Hierarchy (3 rings, bars + progress + line track)");
        corpLabel->SetFontSize(13);
        corpLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(corpLabel);

        auto corpChart = CreateCircularInfoGraphic("CorporateCircular", 240, 160, 460, 320);
        BuildCorporateRings(corpChart);
        corpChart->SetCenterText("Org");
        corpChart->SetTooltipsEnabled(true);
        container->AddChild(corpChart);

        // ===== EXAMPLE 2: TECH ECOSYSTEM, 270 DEGREE PARTIAL =====
        auto techLabel = std::make_shared<UltraCanvasLabel>("CiTechLabel", 720, 130, 460, 25);
        techLabel->SetText("Technology Ecosystem (270 degree sweep, radial + star labels)");
        techLabel->SetFontSize(13);
        techLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(techLabel);

        auto techChart = CreateCircularInfoGraphic("TechCircular", 720, 160, 460, 320);
        BuildTechEcosystemRings(techChart);
        techChart->SetTooltipsEnabled(true);
        container->AddChild(techChart);

        // ===== EXAMPLE 3: CIRCOS-STYLE MULTI-TRACK =====
        auto trackLabel = std::make_shared<UltraCanvasLabel>("CiTrackLabel", 240, 490, 460, 25);
        trackLabel->SetText("Multi-Track Genomic Style (ideogram + coverage + density + links)");
        trackLabel->SetFontSize(13);
        trackLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(trackLabel);

        auto trackChart = CreateCircularInfoGraphic("TrackCircular", 240, 520, 460, 280);
        BuildGenomicTrackRings(trackChart);
        trackChart->SetTooltipsEnabled(true);
        container->AddChild(trackChart);

        // ===== EXAMPLE 4: DECORATIVE MANDALA =====
        auto mandalaLabel = std::make_shared<UltraCanvasLabel>("CiMandalaLabel", 720, 490, 460, 25);
        mandalaLabel->SetText("Decorative Mandala (5 rings, punched cells, outside routing)");
        mandalaLabel->SetFontSize(13);
        mandalaLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(mandalaLabel);

        auto mandalaChart = CreateCircularInfoGraphic("MandalaCircular", 720, 520, 460, 280);
        BuildMandalaRings(mandalaChart);
        mandalaChart->SetShowBackground(true);
        mandalaChart->SetBackgroundColor(Color(22, 22, 32, 255));
        mandalaChart->SetCenterColor(Color(245, 245, 250, 255), Color(90, 90, 110, 255));
        mandalaChart->SetTooltipsEnabled(false);
        container->AddChild(mandalaChart);

        // === CONTROL PANEL (left column) ===
        CreateCircularControlPanel(container,
                                   {corpChart, techChart, trackChart, mandalaChart},
                                   20, 130);

        return container;
    }

} // namespace UltraCanvas
