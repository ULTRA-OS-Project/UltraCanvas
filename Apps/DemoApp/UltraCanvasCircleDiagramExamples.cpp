// Apps/DemoApp/UltraCanvasCircleDiagramExamples.cpp
// Circle diagram demonstration: hub-and-spoke nodes on a backbone ring with
// satellite fans, the two P1 designs, and the palette set at varying node counts
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): one card per
//                  diagram, two rows that wrap, every text sized to fit
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
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
        auto page = DemoKit::CreatePage("CircleDiagramContainer",
            "Circle Diagram Examples - Hub, Backbone Ring and Satellite Fans",
            "Equally sized node discs threaded onto a backbone ring around a central hub, each carrying a fan of "
            "satellites on leader lines. Structure and colour are independent presets, and both work at any node "
            "count: palettes are hue ramps sampled at N points, and every layout quantity derives from the per-node "
            "arc of 360/N. Node radius is one constant per diagram - values never scale a disc.");

        // A card is at least as wide as its diagram (plus padding and border),
        // so a narrow window wraps the cards under each other instead of
        // squeezing a diagram.
        const float cardChrome = 2 * DemoKit::kCardPadding + 2;

        // ===== EXAMPLE 1: SATELLITE WHEEL, 5 NODES x 3 SATELLITES =====
        auto topRow = DemoKit::AddRow(page, "CdTopRow");
        auto saasCard = DemoKit::AddCard(topRow, "CdSaaSCard",
            "SatelliteWheel design, Vibrant palette (5 nodes, 3 satellites each)", "", 580 + cardChrome);

        auto saasDiagram = CreateCircleDiagram("SaaSCircleDiagram", 0, 0, 580, 320);
        BuildSaaSWheel(saasDiagram);
        saasDiagram->SetTooltipsEnabled(true);
        DemoKit::AddFixed(saasCard, saasDiagram);

        // ===== EXAMPLE 2: BANDED WHEEL, DOTTED LEADERS =====
        auto policyCard = DemoKit::AddCard(topRow, "CdPolicyCard",
            "BandedWheel design, Mint palette (thick band, dotted leaders)", "", 570 + cardChrome);

        auto policyDiagram = CreateCircleDiagram("PolicyCircleDiagram", 0, 0, 570, 320);
        BuildPolicyRing(policyDiagram);
        policyDiagram->SetTooltipsEnabled(true);
        DemoKit::AddFixed(policyCard, policyDiagram);

        // ===== EXAMPLE 3: NODE COUNT SWEEP =====
        auto bottomRow = DemoKit::AddRow(page, "CdBottomRow");
        auto sweepCard = DemoKit::AddCard(bottomRow, "CdSweepCard",
            "Same preset at 8 nodes - layout derives from the per-node arc", "", 580 + cardChrome);

        auto sweepDiagram = CreateCircleDiagram("SweepCircleDiagram", 0, 0, 580, 285);
        BuildCountSweep(sweepDiagram, 8);
        sweepDiagram->SetTooltipsEnabled(true);
        DemoKit::AddFixed(sweepCard, sweepDiagram);

        // ===== EXAMPLE 4: OUTSIDE LABELS, NO BACKBONE =====
        auto pastelCard = DemoKit::AddCard(bottomRow, "CdPastelCard",
            "Pastel palette, no backbone ring, labels parked outside the discs", "", 570 + cardChrome);

        auto pastelDiagram = CreateCircleDiagram("PastelCircleDiagram", 0, 0, 570, 285);
        BuildPastelOutside(pastelDiagram);
        pastelDiagram->SetTooltipsEnabled(true);
        DemoKit::AddFixed(pastelCard, pastelDiagram);

        return page;
    }

} // namespace UltraCanvas
