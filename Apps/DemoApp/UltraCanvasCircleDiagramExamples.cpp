// Apps/DemoApp/UltraCanvasCircleDiagramExamples.cpp
// Circle diagram demonstration: hub-and-spoke nodes on a backbone ring with
// satellite fans, the two P1 designs, and the palette set at varying node counts
// Version: 1.0.0
// Last Modified: 2026-08-08
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Diagrams/UltraCanvasCircleDiagram.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include <cmath>

namespace UltraCanvas {

// ===== SAMPLE DATA BUILDERS =====

    // Reference layout 1: a hairline backbone, saturated node discs and a fan of
    // three white satellites per node.
    static void BuildSaaSWheel(std::shared_ptr<UltraCanvasCircleDiagram> diagram) {
        diagram->SetDesign(CircleDiagramDesign::SatelliteWheel);
        diagram->SetPalette(CircleDiagramPaletteKind::Vibrant);
        diagram->SetHubText("Components of a\nSaaS Business");

        const std::vector<std::pair<std::string, std::vector<std::string>>> data = {
            {"Marketing\nAutomation", {"Leads targeting", "Social Media Ads", "Auto Emails"}},
            {"Billing",               {"Invoicing", "Receipts", "Subscription"}},
            {"Customer\nAnalytics",   {"Customer Reports", "Assessing Customer types"}},
            {"Customer\nSupport",     {"Live Chat", "Complaint Center", "Feedback Forms"}},
            {"Customer\nRelationship\nManagement",
                                      {"Customer Database", "Sales Records"}}
        };
        for (const auto& entry : data) {
            size_t node = diagram->AddNode(entry.first);
            for (const auto& satellite : entry.second) {
                diagram->AddSatellite(node, satellite);
            }
        }
    }

    // Reference layout 2: a thick dark backbone band, mint nodes, and a single
    // dark satellite per node on a dotted leader.
    static void BuildPolicyRing(std::shared_ptr<UltraCanvasCircleDiagram> diagram) {
        diagram->SetDesign(CircleDiagramDesign::BandedWheel);
        diagram->SetPalette(CircleDiagramPaletteKind::Mint);
        diagram->SetHubText("E-Governance\nEcosystem");

        const std::vector<std::pair<std::string, std::string>> data = {
            {"Policy\nEnvironment",   "Vision and Goals"},
            {"Human\nResource",       "Civil Society Participation"},
            {"Training\nModules",     "Knowledge Skill Transfer"},
            {"Database\nManagement",  "Open Data Digital Coordination"},
            {"Feedback\nLoop",        "Public-Private Collaboration"}
        };
        for (const auto& entry : data) {
            size_t node = diagram->AddNode(entry.first);
            diagram->AddSatellite(node, entry.second);
        }
    }

    // The same design and palette at a different node count, to show that the
    // presets are not tuned to one reference's segment count.
    static void BuildCountSweep(std::shared_ptr<UltraCanvasCircleDiagram> diagram,
                                size_t nodeCount) {
        diagram->SetDesign(CircleDiagramDesign::SatelliteWheel);
        diagram->SetPalette(CircleDiagramPaletteKind::Ocean);
        diagram->SetHubText("Phase\nModel");

        for (size_t i = 0; i < nodeCount; ++i) {
            size_t node = diagram->AddNode("Stage " + std::to_string(i + 1));
            diagram->AddSatellite(node, "Input");
            diagram->AddSatellite(node, "Output");
        }
    }

    // Outside labels, no backbone, pastel discs - the "petal-adjacent" styling
    // reachable from the primitives without a dedicated design.
    static void BuildPastelOutside(std::shared_ptr<UltraCanvasCircleDiagram> diagram) {
        diagram->SetDesign(CircleDiagramDesign::SatelliteWheel);
        diagram->SetPalette(CircleDiagramPaletteKind::Pastel);
        diagram->SetBackboneStyle(CircleBackboneStyle::NoRing);
        diagram->SetNodeLabelPlacement(CircleNodeLabelPlacement::Outside);
        diagram->SetHubText("Team\nCharter");

        const std::vector<std::string> labels = {
            "Discover", "Define", "Design", "Deliver", "Debrief", "Decide", "Deploy"
        };
        for (const auto& label : labels) diagram->AddNode(label);
    }

// ===== MAIN CIRCLE DIAGRAM EXAMPLES CREATOR =====

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateCircleDiagramExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("CircleDiagramContainer",
                                                               0, 0, 1200, 810);

        // === TITLE ===
        auto titleLabel = std::make_shared<UltraCanvasLabel>("CdTitleLabel", 20, 10, 1160, 35);
        titleLabel->SetText("Circle Diagram Examples - Hub, Backbone Ring and Satellite Fans");
        titleLabel->SetFontSize(18);
        titleLabel->SetFontWeight(FontWeight::Bold);
        titleLabel->SetAlignment(TextAlignment::Center);
        titleLabel->SetBackgroundColor(Color(240, 240, 250, 255));
        container->AddChild(titleLabel);

        // === DESCRIPTION ===
        auto descLabel = std::make_shared<UltraCanvasLabel>("CdDescLabel", 20, 55, 1160, 60);
        descLabel->SetText(
            "Equally sized node discs threaded onto a backbone ring around a central hub, each carrying a fan of\n"
            "satellites on leader lines. Structure and colour are independent presets, and both work at any node\n"
            "count: palettes are hue ramps sampled at N points, and every layout quantity derives from the per-node\n"
            "arc of 360/N. Node radius is one constant per diagram - values never scale a disc."
        );
        descLabel->SetFontSize(11);
        descLabel->SetWrap(TextWrap::WrapWord);
        container->AddChild(descLabel);

        // ===== EXAMPLE 1: SATELLITE WHEEL, 5 NODES x 3 SATELLITES =====
        auto saasLabel = std::make_shared<UltraCanvasLabel>("CdSaaSLabel", 20, 125, 580, 25);
        saasLabel->SetText("SatelliteWheel design, Vibrant palette (5 nodes, 3 satellites each)");
        saasLabel->SetFontSize(13);
        saasLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(saasLabel);

        auto saasDiagram = CreateCircleDiagram("SaaSCircleDiagram", 20, 152, 580, 320);
        BuildSaaSWheel(saasDiagram);
        saasDiagram->SetTooltipsEnabled(true);
        container->AddChild(saasDiagram);

        // ===== EXAMPLE 2: BANDED WHEEL, DOTTED LEADERS =====
        auto policyLabel = std::make_shared<UltraCanvasLabel>("CdPolicyLabel", 610, 125, 570, 25);
        policyLabel->SetText("BandedWheel design, Mint palette (thick band, dotted leaders)");
        policyLabel->SetFontSize(13);
        policyLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(policyLabel);

        auto policyDiagram = CreateCircleDiagram("PolicyCircleDiagram", 610, 152, 570, 320);
        BuildPolicyRing(policyDiagram);
        policyDiagram->SetTooltipsEnabled(true);
        container->AddChild(policyDiagram);

        // ===== EXAMPLE 3: NODE COUNT SWEEP =====
        auto sweepLabel = std::make_shared<UltraCanvasLabel>("CdSweepLabel", 20, 484, 580, 25);
        sweepLabel->SetText("Same preset at 8 nodes - layout derives from the per-node arc");
        sweepLabel->SetFontSize(13);
        sweepLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(sweepLabel);

        auto sweepDiagram = CreateCircleDiagram("SweepCircleDiagram", 20, 511, 580, 285);
        BuildCountSweep(sweepDiagram, 8);
        sweepDiagram->SetTooltipsEnabled(true);
        container->AddChild(sweepDiagram);

        // ===== EXAMPLE 4: OUTSIDE LABELS, NO BACKBONE =====
        auto pastelLabel = std::make_shared<UltraCanvasLabel>("CdPastelLabel", 610, 484, 570, 25);
        pastelLabel->SetText("Pastel palette, no backbone ring, labels parked outside the discs");
        pastelLabel->SetFontSize(13);
        pastelLabel->SetFontWeight(FontWeight::Bold);
        container->AddChild(pastelLabel);

        auto pastelDiagram = CreateCircleDiagram("PastelCircleDiagram", 610, 511, 570, 285);
        BuildPastelOutside(pastelDiagram);
        pastelDiagram->SetTooltipsEnabled(true);
        container->AddChild(pastelDiagram);

        return container;
    }

} // namespace UltraCanvas
