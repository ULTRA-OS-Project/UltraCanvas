// Apps/DemoApp/UltraCanvasRequirementDiagramExamples.cpp
// Demo examples for UltraCanvasRequirementDiagram — SysML requirement diagram
// Version: 1.0.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework
//
// The four tabs reproduce the four reference images from
// Docs/UltraCanvas/UltraCanvasRequirementDiagramProposal.md §2:
//   1. HybridSUV      - colour-by-category containment tree, full compartments
//   2. HSVSpecification - block root, collapsed boxes, detail callout
//   3. SysML taxonomy - generalisation arrows + legend panel
//   4. Smart Home     - heterogeneous kinds, dashed «stereotype» relationships

#include "UltraCanvasDemo.h"
#include "Plugins/Diagrams/UltraCanvasRequirementDiagram.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include <sstream>

namespace UltraCanvas {

// ─────────────────────────────────────────────────────────────────────────────
// Shared helper — wires a diagram's callbacks into the status label
// ─────────────────────────────────────────────────────────────────────────────
    static void WireStatus(const std::shared_ptr<UltraCanvasRequirementDiagram>& diagram,
                           const std::shared_ptr<UltraCanvasLabel>& statusLabel) {
        auto weakStatus = std::weak_ptr<UltraCanvasLabel>(statusLabel);
        auto weakDiagram = std::weak_ptr<UltraCanvasRequirementDiagram>(diagram);

        diagram->onNodeClick = [weakStatus, weakDiagram](const std::string& id) {
            auto status = weakStatus.lock();
            auto req = weakDiagram.lock();
            if (!status || !req) return;
            const RequirementNode* node = req->GetNode(id);
            if (!node) return;

            std::string message = node->name + "  [" + node->id + "]";
            if (!node->text.empty()) message += "\n" + node->text;
            if (node->risk != RequirementRisk::Unspecified) {
                message += "\nrisk: " + std::string(RequirementRiskToString(node->risk));
            }
            if (node->verifyMethod != RequirementVerifyMethod::Unspecified) {
                message += "\nverifyMethod: " +
                           std::string(RequirementVerifyMethodToString(node->verifyMethod));
            }
            const std::string parent = req->GetParentId(id);
            if (!parent.empty()) message += "\ncontained by: " + parent;
            const std::vector<std::string> children = req->GetChildIds(id);
            if (!children.empty()) {
                message += "\nsub-requirements: " + std::to_string(children.size());
            }
            status->SetText(message);
        };

        diagram->onRelationClick = [weakStatus, weakDiagram](const std::string& id) {
            auto status = weakStatus.lock();
            auto req = weakDiagram.lock();
            if (!status || !req) return;
            const RequirementRelation* relation = req->GetRelation(id);
            if (!relation) return;
            status->SetText(std::string("«") +
                            RequirementRelationKindToString(relation->kind) + "»\n" +
                            relation->sourceId + "  ->  " + relation->targetId);
        };
    }

// ─────────────────────────────────────────────────────────────────────────────
// TAB 1 — HybridSUV  (reference image 1)
// Colour-by-category containment tree with full extendedRequirement
// compartments: id, source, text, verifyMethod, risk.
// ─────────────────────────────────────────────────────────────────────────────
    static std::shared_ptr<UltraCanvasContainer> MakeHybridSuvTab(
            std::shared_ptr<UltraCanvasLabel> statusLabel) {
        auto tab = std::make_shared<UltraCanvasContainer>("ReqHybridTab", 0, 0, 1020, 700);

        auto desc = std::make_shared<UltraCanvasLabel>("ReqHybridDesc", 10, 8, 990, 22);
        desc->SetText("Containment tree with ⊕ crosshairs, «stereotype» headers and "
                      "id/source/text/verifyMethod/risk compartments. Colour comes from the category.");
        desc->SetFontSize(11);
        tab->AddChild(desc);

        auto diagram = CreateRequirementDiagram("ReqHybrid", 10, 34, 990, 654);
        diagram->SetPalette(RequirementPaletteKind::Pastel);
        diagram->SetColorSource(RequirementColorSource::ByCategory);
        diagram->SetNodeTemplate(RequirementNodeTemplate::Extended());
        diagram->SetNodeWidthRange(130.0, 210.0);

        diagram->AddCategory("Capacity",    Color(252, 244, 200, 255), Color(190, 170, 90, 255));
        diagram->AddCategory("Environment", Color(212, 240, 226, 255), Color(110, 165, 135, 255));
        diagram->AddCategory("Performance", Color(206, 236, 240, 255), Color(100, 155, 165, 255));
        diagram->AddCategory("Ergonomics",  Color(232, 224, 246, 255), Color(140, 120, 180, 255));

        // Root — collapsed, as in the reference.
        RequirementNode root("UR1", "HybridSUV");
        root.detail = RequirementDetailLevel::Collapsed;
        diagram->AddNode(root);

        // ---- Load / Capacity branch (yellow in the reference) ----
        RequirementNode load("UR1.1", "Load");
        load.stereotype = "functionalRequirement";
        load.source = "Marketing";
        load.text = "The vehicle shall carry passengers and cargo.";
        load.verifyMethod = RequirementVerifyMethod::Test;
        load.risk = RequirementRisk::Low;
        load.category = "Capacity";
        diagram->AddNode(load);

        // ---- Eco-Friendliness branch (green) ----
        RequirementNode eco("UR1.2", "Eco-Friendliness");
        eco.source = "Marketing";
        eco.text = "The vehicle shall be eco-friendly.";
        eco.verifyMethod = RequirementVerifyMethod::Analysis;
        eco.risk = RequirementRisk::High;
        eco.category = "Environment";
        diagram->AddNode(eco);

        // ---- Performance branch (green) ----
        RequirementNode performance("UR1.3", "Performance");
        performance.stereotype = "performanceRequirement";
        performance.source = "Marketing";
        performance.text = "The vehicle shall have adequate performance.";
        performance.verifyMethod = RequirementVerifyMethod::Analysis;
        performance.risk = RequirementRisk::High;
        performance.category = "Performance";
        diagram->AddNode(performance);

        RequirementNode ergonomics("UR1.4", "Ergonomics");
        ergonomics.stereotype = "performanceRequirement";
        ergonomics.detail = RequirementDetailLevel::Collapsed;
        ergonomics.category = "Ergonomics";
        diagram->AddNode(ergonomics);

        // ---- Leaves: stereotype + name only (no compartment) ----
        struct Leaf { const char* id; const char* name; const char* parent;
                      const char* stereotype; const char* category; };
        const Leaf leaves[] = {
            {"UR1.1.1", "Passengers",   "UR1.1", "requirement", "Capacity"},
            {"UR1.1.2", "Cargo",        "UR1.1", "requirement", "Capacity"},
            {"UR1.1.3", "FuelCapacity", "UR1.1", "requirement", "Capacity"},
            {"UR1.3.1", "Acceleration", "UR1.3", "requirement", "Performance"},
            {"UR1.3.2", "Braking",      "UR1.3", "requirement", "Performance"},
            {"UR1.3.3", "Power",        "UR1.3", "requirement", "Performance"},
            {"UR1.3.4", "Range",        "UR1.3", "requirement", "Performance"}
        };
        for (const auto& leaf : leaves) {
            RequirementNode node(leaf.id, leaf.name);
            node.stereotype = leaf.stereotype;
            node.category = leaf.category;
            node.detail = RequirementDetailLevel::Collapsed;
            diagram->AddNode(node);
            diagram->AddContainment(leaf.parent, leaf.id);
        }

        // Two sub-requirements under Eco-Friendliness keep their compartments,
        // matching the reference's Emissions / FuelEconomy boxes.
        RequirementNode emissions("UR1.2.1", "Emissions");
        emissions.stereotype = "performanceRequirement";
        emissions.source = "Regulation";
        emissions.text = "The vehicle shall meet Ultra-Low Emissions Vehicle standards.";
        emissions.verifyMethod = RequirementVerifyMethod::Test;
        emissions.risk = RequirementRisk::High;
        emissions.category = "Environment";
        diagram->AddNode(emissions);

        RequirementNode fuelEconomy("UR1.2.2", "FuelEconomy");
        fuelEconomy.stereotype = "performanceRequirement";
        fuelEconomy.source = "Marketing";
        fuelEconomy.text = "The vehicle shall obtain fuel economy better than "
                           "95% of cars built in 2004.";
        fuelEconomy.verifyMethod = RequirementVerifyMethod::Test;
        fuelEconomy.risk = RequirementRisk::Medium;
        fuelEconomy.category = "Environment";
        diagram->AddNode(fuelEconomy);

        diagram->AddContainment("UR1", "UR1.1");
        diagram->AddContainment("UR1", "UR1.2");
        diagram->AddContainment("UR1", "UR1.3");
        diagram->AddContainment("UR1", "UR1.4");
        diagram->AddContainment("UR1.2", "UR1.2.1");
        diagram->AddContainment("UR1.2", "UR1.2.2");

        diagram->SetLayoutMode(RequirementLayoutMode::ContainmentTree);
        diagram->SetLayoutOrientation(RequirementOrientation::TopDown);
        diagram->SetLevelGap(58.0);
        diagram->SetSiblingGap(20.0);
        diagram->SetLegendVisible(true, RequirementPanelPosition::BottomLeft);
        diagram->SetLegendSource(RequirementLegendSource::Categories);
        diagram->RunLayout();

        WireStatus(diagram, statusLabel);
        tab->AddChild(diagram);
        return tab;
    }

// ─────────────────────────────────────────────────────────────────────────────
// TAB 2 — HSVSpecification  (reference image 2)
// A «block» root owning a wide, collapsed containment tree, with one detail
// callout anchored to the Emissions requirement.
// ─────────────────────────────────────────────────────────────────────────────
    static std::shared_ptr<UltraCanvasContainer> MakeSpecificationTab(
            std::shared_ptr<UltraCanvasLabel> statusLabel) {
        auto tab = std::make_shared<UltraCanvasContainer>("ReqSpecTab", 0, 0, 1020, 700);

        auto desc = std::make_shared<UltraCanvasLabel>("ReqSpecDesc", 10, 8, 990, 22);
        desc->SetText("Structural overview: a «block» root owns collapsed requirement boxes. "
                      "Detail lives in a draggable callout anchored to Emissions.");
        desc->SetFontSize(11);
        tab->AddChild(desc);

        auto diagram = CreateRequirementDiagram("ReqSpec", 10, 34, 990, 654);
        diagram->SetPalette(RequirementPaletteKind::Pastel);
        diagram->SetDefaultDetailLevel(RequirementDetailLevel::Collapsed);
        diagram->SetNodeWidthRange(110.0, 190.0);

        diagram->AddNode(RequirementNodeKind::Block, "HSVSpec", "HSVSpecification");

        struct Branch { const char* id; const char* name; };
        const Branch branches[] = {
            {"ECO",  "Eco-Friendliness"},
            {"PERF", "Performance"},
            {"ERGO", "Ergonomics"},
            {"QUAL", "Qualification"},
            {"CAP",  "Capacity"}
        };
        for (const auto& branch : branches) {
            RequirementNode node(branch.id, branch.name);
            node.detail = RequirementDetailLevel::Collapsed;
            diagram->AddNode(node);
            diagram->AddContainment("HSVSpec", branch.id);
        }

        struct Leaf { const char* id; const char* name; const char* parent; };
        const Leaf leaves[] = {
            {"BRAKE",  "Braking",             "PERF"},
            {"FUELEC", "FuelEconomy",         "PERF"},
            {"OFFRD",  "OffRoadCapability",   "PERF"},
            {"ACCEL",  "Acceleration",        "PERF"},
            {"SAFETY", "SafetyTest",          "QUAL"},
            {"CARGO",  "CargoCapacity",       "CAP"},
            {"PASSNG", "PassengerCapacity",   "CAP"},
            {"FUELCP", "FuelCapacity",        "CAP"}
        };
        for (const auto& leaf : leaves) {
            RequirementNode node(leaf.id, leaf.name);
            node.detail = RequirementDetailLevel::Collapsed;
            diagram->AddNode(node);
            diagram->AddContainment(leaf.parent, leaf.id);
        }

        // The one requirement whose detail is shown, via a callout.
        RequirementNode emissions("R1.2.1", "Emissions");
        emissions.text = "The vehicle shall meet Ultra-Low Emissions Vehicle standards.";
        emissions.detail = RequirementDetailLevel::Collapsed;
        diagram->AddNode(emissions);
        diagram->AddContainment("ECO", "R1.2.1");

        diagram->SetLayoutMode(RequirementLayoutMode::ContainmentTree);
        diagram->SetLevelGap(64.0);
        diagram->SetSiblingGap(18.0);
        diagram->RunLayout();

        // Placed below-left of the laid-out tree so the dashed leader line to
        // Emissions is visible; the callout is draggable.
        diagram->AddCallout("emissionsNote", "R1.2.1",
                            {RequirementField::Id, RequirementField::Text},
                            -140.0, 320.0);

        WireStatus(diagram, statusLabel);
        tab->AddChild(diagram);
        return tab;
    }

// ─────────────────────────────────────────────────────────────────────────────
// TAB 3 — SysML diagram taxonomy  (reference image 3)
// Generalisation arrows (hollow triangle) plus a legend keyed by category.
// ─────────────────────────────────────────────────────────────────────────────
    static std::shared_ptr<UltraCanvasContainer> MakeTaxonomyTab(
            std::shared_ptr<UltraCanvasLabel> statusLabel) {
        auto tab = std::make_shared<UltraCanvasContainer>("ReqTaxonomyTab", 0, 0, 1020, 700);

        auto desc = std::make_shared<UltraCanvasLabel>("ReqTaxonomyDesc", 10, 8, 990, 22);
        desc->SetText("The SysML diagram taxonomy: generalisation (solid line, hollow triangle) "
                      "over a category-coloured tree, with an auto-generated legend.");
        desc->SetFontSize(11);
        tab->AddChild(desc);

        auto diagram = CreateRequirementDiagram("ReqTaxonomy", 10, 34, 990, 654);
        diagram->SetPalette(RequirementPaletteKind::Classic);
        diagram->SetColorSource(RequirementColorSource::ByCategory);
        diagram->SetDefaultDetailLevel(RequirementDetailLevel::Collapsed);
        diagram->SetNodeWidthRange(120.0, 190.0);

        diagram->AddCategory("Behavior Diagram",   Color(255, 238, 156, 255), Color(180, 155, 40, 255));
        diagram->AddCategory("Structure Diagram",  Color(186, 222, 246, 255), Color(50, 120, 175, 255));
        diagram->AddCategory("Requirement Diagram", Color(196, 236, 210, 255), Color(50, 140, 90, 255));
        diagram->AddCategory("Abstract",           Color(248, 248, 250, 255), Color(90, 90, 100, 255));

        struct Entry { const char* id; const char* name; const char* parent; const char* category; };
        const Entry entries[] = {
            {"SYSML",  "SysML Diagram",        "",       "Abstract"},
            {"BEHAV",  "Behavior Diagram",     "SYSML",  "Abstract"},
            {"REQD",   "Requirement Diagram",  "SYSML",  "Requirement Diagram"},
            {"STRUCT", "Structure Diagram",    "SYSML",  "Abstract"},
            {"ACT",    "Activity Diagram",     "BEHAV",  "Behavior Diagram"},
            {"SEQ",    "Sequence Diagram",     "BEHAV",  "Behavior Diagram"},
            {"STATE",  "State Machine Diagram","BEHAV",  "Behavior Diagram"},
            {"USECASE","Use Case Diagram",     "BEHAV",  "Behavior Diagram"},
            {"BDD",    "Block Definition Diagram", "STRUCT", "Structure Diagram"},
            {"IBD",    "Internal Block Diagram",   "STRUCT", "Structure Diagram"},
            {"PKG",    "Package Diagram",      "STRUCT", "Structure Diagram"},
            {"PAR",    "Parametric Diagram",   "IBD",    "Structure Diagram"}
        };

        for (const auto& entry : entries) {
            RequirementNode node(entry.id, entry.name);
            node.kind = RequirementNodeKind::Block;
            node.stereotype = "diagram";
            node.category = entry.category;
            node.detail = RequirementDetailLevel::Collapsed;
            diagram->AddNode(node);
        }
        // Containment drives the layout; generalisation carries the notation.
        // The containment lines are hidden so each edge is drawn exactly once.
        for (const auto& entry : entries) {
            if (!*entry.parent) continue;
            diagram->AddContainment(entry.parent, entry.id);
            diagram->AddRelation(RequirementRelationKind::Generalization,
                                 entry.id, entry.parent);
        }
        diagram->SetRelationKindVisible(RequirementRelationKind::Containment, false);

        diagram->SetLayoutMode(RequirementLayoutMode::ContainmentTree);
        diagram->SetLevelGap(70.0);
        diagram->SetSiblingGap(18.0);
        diagram->SetLegendVisible(true, RequirementPanelPosition::BottomLeft);
        diagram->SetLegendSource(RequirementLegendSource::Categories);
        diagram->RunLayout();

        WireStatus(diagram, statusLabel);
        tab->AddChild(diagram);
        return tab;
    }

// ─────────────────────────────────────────────────────────────────────────────
// TAB 4 — Smart Home Automation  (reference image 4)
// Heterogeneous node kinds, colour-by-kind, dashed «deriveReqt» / «satisfy» /
// «verify» relationships over a non-tree topology, and a title banner.
// ─────────────────────────────────────────────────────────────────────────────
    static std::shared_ptr<UltraCanvasContainer> MakeSmartHomeTab(
            std::shared_ptr<UltraCanvasLabel> statusLabel) {
        auto tab = std::make_shared<UltraCanvasContainer>("ReqSmartHomeTab", 0, 0, 1020, 700);

        auto desc = std::make_shared<UltraCanvasLabel>("ReqSmartHomeDesc", 10, 8, 990, 22);
        desc->SetText("Traceability web: «testCase» verifies, «block» satisfies, requirements "
                      "derive from each other. Drag boxes; the dashed routes follow.");
        desc->SetFontSize(11);
        tab->AddChild(desc);

        auto diagram = CreateRequirementDiagram("ReqSmartHome", 10, 34, 990, 654);
        diagram->SetPalette(RequirementPaletteKind::Vibrant);
        diagram->SetColorSource(RequirementColorSource::ByKind);
        diagram->SetNodeTemplate(RequirementNodeTemplate::Standard());
        diagram->SetNodeWidthRange(140.0, 200.0);
        diagram->SetTitle("Smart Home Automation — System Requirement Diagram");
        diagram->SetDefaultRouting(RequirementRouting::Orthogonal);

        // ---- Requirements ---------------------------------------------------
        struct Req { const char* id; const char* name; const char* text;
                     RequirementRisk risk; RequirementVerifyMethod method; };
        const Req requirements[] = {
            {"R1", "Device Compatibility",
             "The system shall support at least 50 third-party smart devices.",
             RequirementRisk::Medium, RequirementVerifyMethod::Test},
            {"R2", "Energy Efficiency",
             "The system shall reduce household energy consumption by 15%.",
             RequirementRisk::High, RequirementVerifyMethod::Analysis},
            {"R3", "Emergency Shutdown",
             "The system shall cut power to all controlled circuits within 2 seconds.",
             RequirementRisk::High, RequirementVerifyMethod::Demonstration},
            {"R4", "Privacy Protection",
             "The system shall store all personal data encrypted at rest.",
             RequirementRisk::High, RequirementVerifyMethod::Inspection},
            {"R5", "Remote Access",
             "The system shall be controllable from outside the home network.",
             RequirementRisk::Medium, RequirementVerifyMethod::Test},
            {"R6", "Voice Command Accuracy",
             "The system shall recognise voice commands with 95% accuracy.",
             RequirementRisk::Medium, RequirementVerifyMethod::Test}
        };
        for (const auto& req : requirements) {
            RequirementNode node(req.id, req.name);
            node.text = req.text;
            node.risk = req.risk;
            node.verifyMethod = req.method;
            node.detail = RequirementDetailLevel::Standard;
            diagram->AddNode(node);
        }

        // ---- Design elements, test cases and use cases -----------------------
        diagram->AddNode(RequirementNodeKind::Block,    "SYSHUB",  "System Hub");
        diagram->AddNode(RequirementNodeKind::Block,    "AUTHSVC", "Authentication Service");
        diagram->AddNode(RequirementNodeKind::TestCase, "TC1", "Remote Access Test");
        diagram->AddNode(RequirementNodeKind::TestCase, "TC2", "Privacy Audit");
        diagram->AddNode(RequirementNodeKind::TestCase, "TC3", "Voice Accuracy Test");
        diagram->AddNode(RequirementNodeKind::UseCase,  "UC1", "User Logs In");
        diagram->AddNode(RequirementNodeKind::Actor,    "A1",  "Homeowner");

        // ---- Manual placement: the reference is a web, not a tree ------------
        struct Placement { const char* id; double x; double y; };
        const Placement placements[] = {
            {"R1",      40.0,   30.0}, {"R2",     280.0,  30.0}, {"R3",     520.0,  30.0},
            {"R4",     760.0,   30.0},
            {"R5",     160.0,  250.0}, {"R6",     640.0, 250.0},
            {"SYSHUB",  40.0,  430.0}, {"AUTHSVC", 280.0, 430.0},
            {"TC1",    520.0,  430.0}, {"TC2",     720.0, 430.0}, {"TC3", 920.0, 430.0},
            {"UC1",    320.0,  590.0}, {"A1",       80.0, 590.0}
        };
        for (const auto& placement : placements) {
            diagram->SetNodePosition(placement.id, placement.x, placement.y);
        }

        // ---- Traceability ---------------------------------------------------
        diagram->AddRelation(RequirementRelationKind::DeriveReqt, "R5", "R1");
        diagram->AddRelation(RequirementRelationKind::DeriveReqt, "R6", "R3");
        diagram->AddRelation(RequirementRelationKind::Satisfy,    "SYSHUB",  "R1");
        diagram->AddRelation(RequirementRelationKind::Satisfy,    "SYSHUB",  "R2");
        diagram->AddRelation(RequirementRelationKind::Satisfy,    "AUTHSVC", "R4");
        diagram->AddRelation(RequirementRelationKind::Satisfy,    "AUTHSVC", "R5");
        diagram->AddRelation(RequirementRelationKind::Verify,     "TC1", "R5");
        diagram->AddRelation(RequirementRelationKind::Verify,     "TC2", "R4");
        diagram->AddRelation(RequirementRelationKind::Verify,     "TC3", "R6");
        diagram->AddRelation(RequirementRelationKind::Refine,     "UC1", "R5");
        diagram->AddRelation(RequirementRelationKind::Trace,      "A1",  "R5");

        diagram->SetLayoutMode(RequirementLayoutMode::Manual);
        diagram->SetLegendVisible(true, RequirementPanelPosition::BottomRight);
        diagram->SetLegendSource(RequirementLegendSource::RelationKinds);
        diagram->FitView();

        WireStatus(diagram, statusLabel);
        tab->AddChild(diagram);
        return tab;
    }

// ─────────────────────────────────────────────────────────────────────────────
// TAB 5 — Traceability & coverage  (phase 2)
// Layered layout over a non-tree topology, SysML compartment notation
// (satisfiedBy / verifiedBy listed inside the box instead of drawn as lines),
// the coverage overlay, obstacle-aware routing, and click-to-trace.
// ─────────────────────────────────────────────────────────────────────────────
    static std::shared_ptr<UltraCanvasContainer> MakeCoverageTab(
            std::shared_ptr<UltraCanvasLabel> statusLabel) {
        auto tab = std::make_shared<UltraCanvasContainer>("ReqCoverageTab", 0, 0, 1020, 700);

        auto desc = std::make_shared<UltraCanvasLabel>("ReqCoverageDesc", 10, 8, 990, 22);
        desc->SetText("Compartment notation + coverage badges. Click a requirement to trace it; "
                      "Escape clears. Red = nothing satisfies it, amber = no test case.");
        desc->SetFontSize(11);
        tab->AddChild(desc);

        auto diagram = CreateRequirementDiagram("ReqCoverage", 10, 34, 990, 654);
        diagram->SetPalette(RequirementPaletteKind::Professional);
        diagram->SetFrame("Package", "VehicleControl", "Requirements Diagram");
        diagram->SetNodeWidthRange(150.0, 215.0);

        // The satisfiedBy / verifiedBy lists replace the drawn trace lines -
        // the SysML alternative notation for a dense diagram.
        RequirementNodeTemplate tpl = RequirementNodeTemplate::Standard();
        tpl.AddDerivedCompartment(RequirementDerivedList::SatisfiedBy);
        tpl.AddDerivedCompartment(RequirementDerivedList::VerifiedBy);
        diagram->SetNodeTemplate(tpl);

        struct Req { const char* id; const char* name; const char* text; };
        const Req requirements[] = {
            {"VC1", "Vehicle Control",  "The vehicle shall be controllable at all speeds."},
            {"VC1.1", "Steering",       "Steering shall respond within 100 ms."},
            {"VC1.2", "Braking",        "The vehicle shall stop from 100 km/h in under 40 m."},
            {"VC1.3", "Stability",      "The vehicle shall remain stable in a 30 deg turn."},
            {"VC1.4", "Diagnostics",    "Faults shall be reported to the driver within 1 s."}
        };
        for (const auto& r : requirements) {
            RequirementNode node(r.id, r.name);
            node.text = r.text;
            node.detail = RequirementDetailLevel::Full;
            diagram->AddNode(node);
        }
        diagram->AddContainment("VC1", "VC1.1");
        diagram->AddContainment("VC1", "VC1.2");
        diagram->AddContainment("VC1", "VC1.3");
        diagram->AddContainment("VC1", "VC1.4");

        diagram->AddNode(RequirementNodeKind::Block,    "SBW",  "Steer-by-Wire");
        diagram->AddNode(RequirementNodeKind::Block,    "ABS",  "ABS Module");
        diagram->AddNode(RequirementNodeKind::TestCase, "TS1",  "Steering Latency Test");
        diagram->AddNode(RequirementNodeKind::TestCase, "TS2",  "Brake Distance Test");

        // VC1.3 is satisfied but unverified; VC1.4 has nothing at all.
        diagram->AddRelation(RequirementRelationKind::Satisfy, "SBW", "VC1.1");
        diagram->AddRelation(RequirementRelationKind::Satisfy, "ABS", "VC1.2");
        diagram->AddRelation(RequirementRelationKind::Satisfy, "SBW", "VC1.3");
        diagram->AddRelation(RequirementRelationKind::Verify,  "TS1", "VC1.1");
        diagram->AddRelation(RequirementRelationKind::Verify,  "TS2", "VC1.2");

        // The trace relations live in the compartments, so their lines are
        // hidden; containment still draws its bus.
        diagram->SetRelationKindVisible(RequirementRelationKind::Satisfy, false);
        diagram->SetRelationKindVisible(RequirementRelationKind::Verify, false);

        diagram->SetCoverageOverlayVisible(true);
        diagram->SetObstacleAvoidance(true);
        diagram->SetLayoutMode(RequirementLayoutMode::ContainmentTree);
        diagram->SetLevelGap(70.0);
        diagram->SetLegendVisible(true, RequirementPanelPosition::BottomRight);
        diagram->SetLegendSource(RequirementLegendSource::Coverage);
        diagram->RunLayout();

        auto weakStatus = std::weak_ptr<UltraCanvasLabel>(statusLabel);
        auto weakDiagram = std::weak_ptr<UltraCanvasRequirementDiagram>(diagram);
        diagram->onNodeClick = [weakStatus, weakDiagram](const std::string& id) {
            auto status = weakStatus.lock();
            auto req = weakDiagram.lock();
            if (!status || !req) return;
            req->HighlightTraceChain(id, RequirementTraceDirection::Both, 0);

            const RequirementCoverage coverage = req->GetCoverage();
            std::ostringstream out;
            out << id << "\n\nCoverage over " << coverage.requirementCount << " requirements:\n"
                << "  satisfied " << coverage.satisfiedCount << " ("
                << static_cast<int>(coverage.satisfiedPercent) << "%)\n"
                << "  verified  " << coverage.verifiedCount << " ("
                << static_cast<int>(coverage.verifiedPercent) << "%)\n"
                << "  orphans   " << coverage.orphanCount;

            const std::vector<std::string> unverified = req->GetUnverifiedRequirements();
            if (!unverified.empty()) {
                out << "\n\nNot verified:";
                for (const auto& u : unverified) out << "\n  " << u;
            }
            status->SetText(out.str());
        };

        tab->AddChild(diagram);
        return tab;
    }

// ─────────────────────────────────────────────────────────────────────────────
// TAB 6 — Mermaid import & editing  (phase 2)
// The whole diagram is parsed from Mermaid requirementDiagram text, then
// edited live: expand/collapse toggles, create-node, drag-to-connect, delete.
// ─────────────────────────────────────────────────────────────────────────────
    static std::shared_ptr<UltraCanvasContainer> MakeMermaidTab(
            std::shared_ptr<UltraCanvasLabel> statusLabel) {
        auto tab = std::make_shared<UltraCanvasContainer>("ReqMermaidTab", 0, 0, 1020, 700);

        auto desc = std::make_shared<UltraCanvasLabel>("ReqMermaidDesc", 10, 8, 990, 22);
        desc->SetText("Parsed from Mermaid requirementDiagram text. Use the ⊖ toggles to collapse "
                      "a sub-tree; the buttons switch edit mode. F2 renames, Delete removes.");
        desc->SetFontSize(11);
        tab->AddChild(desc);

        auto diagram = CreateRequirementDiagram("ReqMermaid", 10, 66, 990, 622);
        diagram->SetPalette(RequirementPaletteKind::Pastel);
        diagram->SetNodeWidthRange(140.0, 220.0);
        diagram->SetDoubleClickAction(RequirementDoubleClickAction::ToggleDetail);

        static const char* kMermaidSource = R"(requirementDiagram

direction TB

requirement payment_system {
    id: R1
    text: The platform shall process payments securely.
    risk: high
    verifymethod: analysis
}

functionalRequirement card_payments {
    id: R1.1
    text: The platform shall accept major credit cards.
    risk: medium
    verifymethod: test
}

functionalRequirement refunds {
    id: R1.2
    text: A refund shall complete within two business days.
    risk: medium
    verifymethod: demonstration
}

designConstraint pci_compliance {
    id: R1.3
    text: The platform shall satisfy PCI DSS level 1.
    risk: high
    verifymethod: inspection
}

performanceRequirement throughput {
    id: R1.1.1
    text: The gateway shall sustain 500 transactions per second.
    risk: high
    verifymethod: test
}

element payment_gateway {
    type: block
    docref: arch/gateway.md
}

element audit_suite {
    type: testCase
    docref: qa/audit-plan.md
}

payment_system - contains -> card_payments
payment_system - contains -> refunds
payment_system - contains -> pci_compliance
card_payments - contains -> throughput
payment_gateway - satisfies -> card_payments
payment_gateway - satisfies -> throughput
audit_suite - verifies -> pci_compliance
refunds - derives -> card_payments
)";

        std::string error;
        if (!diagram->FromMermaid(kMermaidSource, &error)) {
            diagram->SetTitle("Mermaid import failed", error);
        }
        diagram->SetLegendVisible(true, RequirementPanelPosition::BottomRight);
        diagram->SetLegendSource(RequirementLegendSource::NodeKinds);
        // The trace lines cross the tree here, so route them around the boxes
        // rather than through them - otherwise the «satisfies» / «derives»
        // labels land behind a requirement.
        diagram->SetObstacleAvoidance(true);

        WireStatus(diagram, statusLabel);
        auto weakStatus = std::weak_ptr<UltraCanvasLabel>(statusLabel);
        auto weakDiagram = std::weak_ptr<UltraCanvasRequirementDiagram>(diagram);
        diagram->onValidationWarning = [weakStatus](const RequirementWarning& warning) {
            if (auto status = weakStatus.lock()) {
                status->SetText("Model warning:\n" + warning.message);
            }
        };
        diagram->onRelationCreated = [weakStatus](const RequirementRelation& relation) {
            if (auto status = weakStatus.lock()) {
                status->SetText(std::string("Created «") +
                                RequirementRelationKindToString(relation.kind) + "»\n" +
                                relation.sourceId + "  ->  " + relation.targetId);
            }
        };

        // ---- edit-mode buttons ------------------------------------------
        struct ModeButton { const char* id; const char* label; RequirementEditMode mode; };
        const ModeButton buttons[] = {
            {"ReqModeSelect",  "Select",       RequirementEditMode::Select},
            {"ReqModeNode",    "+ Requirement", RequirementEditMode::CreateNode},
            {"ReqModeConnect", "Connect (satisfy)", RequirementEditMode::CreateRelation}
        };
        int buttonX = 10;
        for (const auto& button : buttons) {
            auto widget = std::make_shared<UltraCanvasButton>(button.id, buttonX, 34, 150, 26,
                                                              button.label);
            const RequirementEditMode mode = button.mode;
            widget->SetOnClick([weakDiagram, weakStatus, mode]() {
                auto req = weakDiagram.lock();
                if (!req) return;
                req->SetEditMode(mode);
                if (auto status = weakStatus.lock()) {
                    status->SetText(mode == RequirementEditMode::CreateNode
                        ? "Create mode: click empty canvas to add a requirement."
                        : mode == RequirementEditMode::CreateRelation
                            ? "Connect mode: drag from a block onto a requirement."
                            : "Select mode: drag boxes, rubber-band select, F2 to rename.");
                }
            });
            tab->AddChild(widget);
            buttonX += 158;
        }

        auto exportButton = std::make_shared<UltraCanvasButton>(
            "ReqExportMermaid", buttonX, 34, 150, 26, "Export Mermaid");
        exportButton->SetOnClick([weakDiagram, weakStatus]() {
            auto req = weakDiagram.lock();
            auto status = weakStatus.lock();
            if (!req || !status) return;
            const std::string text = req->ToMermaid("TB");
            // Show the head of the export; the full text is what ToMermaid returns.
            std::string preview = text.substr(0, 520);
            if (text.size() > preview.size()) preview += "\n...";
            status->SetText("ToMermaid() — " + std::to_string(text.size()) +
                            " chars:\n\n" + preview);
        });
        tab->AddChild(exportButton);

        tab->AddChild(diagram);
        return tab;
    }


// ─────────────────────────────────────────────────────────────────────────────
// TAB 7 — ReqIF import & overlays  (phase 3)
// A specification imported from ReqIF, wrapped in a «package» grouping region,
// with copy/suspect links, a relationship rationale note, status badges, block
// port nubs, and the minimap + controls overlays.
// ─────────────────────────────────────────────────────────────────────────────
    static std::shared_ptr<UltraCanvasContainer> MakeReqIfTab(
            std::shared_ptr<UltraCanvasLabel> statusLabel) {
        auto tab = std::make_shared<UltraCanvasContainer>("ReqIfTab", 0, 0, 1020, 700);

        auto desc = std::make_shared<UltraCanvasLabel>("ReqIfDesc", 10, 8, 990, 22);
        desc->SetText("Imported from ReqIF XML, then annotated: package region, «copy» with a "
                      "suspect badge, relationship rationale, status chips, block ports.");
        desc->SetFontSize(11);
        tab->AddChild(desc);

        auto diagram = CreateRequirementDiagram("ReqIfDiagram", 10, 66, 990, 622);
        diagram->SetPalette(RequirementPaletteKind::Professional);
        diagram->SetNodeWidthRange(150.0, 220.0);

        static const char* kReqIfSource = R"(<?xml version="1.0" encoding="UTF-8"?>
<REQ-IF>
  <CORE-CONTENT><REQ-IF-CONTENT>
    <SPEC-TYPES>
      <SPEC-OBJECT-TYPE IDENTIFIER="ST-1" LONG-NAME="Requirement">
        <SPEC-ATTRIBUTES>
          <ATTRIBUTE-DEFINITION-STRING IDENTIFIER="AD-ID"   LONG-NAME="ReqIF.ForeignID"/>
          <ATTRIBUTE-DEFINITION-STRING IDENTIFIER="AD-NAME" LONG-NAME="ReqIF.Name"/>
          <ATTRIBUTE-DEFINITION-XHTML  IDENTIFIER="AD-TEXT" LONG-NAME="ReqIF.Text"/>
          <ATTRIBUTE-DEFINITION-STRING IDENTIFIER="AD-RISK" LONG-NAME="Risk"/>
          <ATTRIBUTE-DEFINITION-STRING IDENTIFIER="AD-STAT" LONG-NAME="Status"/>
        </SPEC-ATTRIBUTES>
      </SPEC-OBJECT-TYPE>
    </SPEC-TYPES>
    <SPEC-OBJECTS>
      <SPEC-OBJECT IDENTIFIER="SO-SYS"><VALUES>
        <ATTRIBUTE-VALUE-STRING THE-VALUE="SYS-1"><DEFINITION>
          <ATTRIBUTE-DEFINITION-STRING-REF>AD-ID</ATTRIBUTE-DEFINITION-STRING-REF></DEFINITION>
        </ATTRIBUTE-VALUE-STRING>
        <ATTRIBUTE-VALUE-STRING THE-VALUE="Landing Gear"><DEFINITION>
          <ATTRIBUTE-DEFINITION-STRING-REF>AD-NAME</ATTRIBUTE-DEFINITION-STRING-REF></DEFINITION>
        </ATTRIBUTE-VALUE-STRING>
        <ATTRIBUTE-VALUE-XHTML><DEFINITION>
          <ATTRIBUTE-DEFINITION-XHTML-REF>AD-TEXT</ATTRIBUTE-DEFINITION-XHTML-REF></DEFINITION>
          <THE-VALUE><xhtml:div>The landing gear shall extend and retract on command.</xhtml:div></THE-VALUE>
        </ATTRIBUTE-VALUE-XHTML>
        <ATTRIBUTE-VALUE-STRING THE-VALUE="Approved"><DEFINITION>
          <ATTRIBUTE-DEFINITION-STRING-REF>AD-STAT</ATTRIBUTE-DEFINITION-STRING-REF></DEFINITION>
        </ATTRIBUTE-VALUE-STRING>
      </VALUES></SPEC-OBJECT>
      <SPEC-OBJECT IDENTIFIER="SO-EXT"><VALUES>
        <ATTRIBUTE-VALUE-STRING THE-VALUE="SYS-1.1"><DEFINITION>
          <ATTRIBUTE-DEFINITION-STRING-REF>AD-ID</ATTRIBUTE-DEFINITION-STRING-REF></DEFINITION>
        </ATTRIBUTE-VALUE-STRING>
        <ATTRIBUTE-VALUE-STRING THE-VALUE="Extension Time"><DEFINITION>
          <ATTRIBUTE-DEFINITION-STRING-REF>AD-NAME</ATTRIBUTE-DEFINITION-STRING-REF></DEFINITION>
        </ATTRIBUTE-VALUE-STRING>
        <ATTRIBUTE-VALUE-XHTML><DEFINITION>
          <ATTRIBUTE-DEFINITION-XHTML-REF>AD-TEXT</ATTRIBUTE-DEFINITION-XHTML-REF></DEFINITION>
          <THE-VALUE><xhtml:div>Extension shall complete within 12 s at any airspeed below V_LO.</xhtml:div></THE-VALUE>
        </ATTRIBUTE-VALUE-XHTML>
        <ATTRIBUTE-VALUE-STRING THE-VALUE="High"><DEFINITION>
          <ATTRIBUTE-DEFINITION-STRING-REF>AD-RISK</ATTRIBUTE-DEFINITION-STRING-REF></DEFINITION>
        </ATTRIBUTE-VALUE-STRING>
        <ATTRIBUTE-VALUE-STRING THE-VALUE="Approved"><DEFINITION>
          <ATTRIBUTE-DEFINITION-STRING-REF>AD-STAT</ATTRIBUTE-DEFINITION-STRING-REF></DEFINITION>
        </ATTRIBUTE-VALUE-STRING>
      </VALUES></SPEC-OBJECT>
      <SPEC-OBJECT IDENTIFIER="SO-IND"><VALUES>
        <ATTRIBUTE-VALUE-STRING THE-VALUE="SYS-1.2"><DEFINITION>
          <ATTRIBUTE-DEFINITION-STRING-REF>AD-ID</ATTRIBUTE-DEFINITION-STRING-REF></DEFINITION>
        </ATTRIBUTE-VALUE-STRING>
        <ATTRIBUTE-VALUE-STRING THE-VALUE="Cockpit Indication"><DEFINITION>
          <ATTRIBUTE-DEFINITION-STRING-REF>AD-NAME</ATTRIBUTE-DEFINITION-STRING-REF></DEFINITION>
        </ATTRIBUTE-VALUE-STRING>
        <ATTRIBUTE-VALUE-XHTML><DEFINITION>
          <ATTRIBUTE-DEFINITION-XHTML-REF>AD-TEXT</ATTRIBUTE-DEFINITION-XHTML-REF></DEFINITION>
          <THE-VALUE><xhtml:div>Gear position shall be indicated in the cockpit within 1 s.</xhtml:div></THE-VALUE>
        </ATTRIBUTE-VALUE-XHTML>
        <ATTRIBUTE-VALUE-STRING THE-VALUE="Proposed"><DEFINITION>
          <ATTRIBUTE-DEFINITION-STRING-REF>AD-STAT</ATTRIBUTE-DEFINITION-STRING-REF></DEFINITION>
        </ATTRIBUTE-VALUE-STRING>
      </VALUES></SPEC-OBJECT>
    </SPEC-OBJECTS>
    <SPECIFICATIONS><SPECIFICATION IDENTIFIER="SPEC-1"><CHILDREN>
      <SPEC-HIERARCHY IDENTIFIER="SH-1">
        <OBJECT><SPEC-OBJECT-REF>SO-SYS</SPEC-OBJECT-REF></OBJECT>
        <CHILDREN>
          <SPEC-HIERARCHY IDENTIFIER="SH-2">
            <OBJECT><SPEC-OBJECT-REF>SO-EXT</SPEC-OBJECT-REF></OBJECT>
          </SPEC-HIERARCHY>
          <SPEC-HIERARCHY IDENTIFIER="SH-3">
            <OBJECT><SPEC-OBJECT-REF>SO-IND</SPEC-OBJECT-REF></OBJECT>
          </SPEC-HIERARCHY>
        </CHILDREN>
      </SPEC-HIERARCHY>
    </CHILDREN></SPECIFICATION></SPECIFICATIONS>
  </REQ-IF-CONTENT></CORE-CONTENT>
</REQ-IF>)";

        std::string error;
        const bool imported = diagram->FromReqIf(kReqIfSource, &error);

        if (imported) {
            // Wrap the imported tree in a package so the grouping region has
            // something to bound.
            diagram->AddNode(RequirementNodeKind::Package, "PKG", "LandingGearSpec");
            diagram->AddContainment("PKG", "SO-SYS");

            // A design element and a test case, with ports on the block.
            diagram->AddNode(RequirementNodeKind::Block, "GEARCTL", "Gear Controller");
            diagram->AddNode(RequirementNodeKind::TestCase, "TC-EXT", "Extension Timing Test");
            diagram->SetNodePortCount("GEARCTL", 3);

            const std::string satisfyId =
                diagram->AddRelation(RequirementRelationKind::Satisfy, "GEARCTL", "SO-EXT");
            diagram->AddRelation(RequirementRelationKind::Verify, "TC-EXT", "SO-EXT");
            // A second relation between the same pair, to show the fan-out.
            diagram->AddRelation(RequirementRelationKind::Trace, "GEARCTL", "SO-EXT");

            diagram->SetRelationRationale(satisfyId,
                "Hydraulic actuation chosen over electric after the weight trade study TR-114.");
            diagram->SetRationaleNotesVisible(true);

            // A copy of the extension requirement, then an edit to its master
            // so the suspect badge has something to report.
            RequirementNode copy("SYS-1.1-COPY", "Extension Time (copy)");
            diagram->AddNode(copy);
            diagram->AddRelation(RequirementRelationKind::Copy, "SYS-1.1-COPY", "SO-EXT");
            diagram->SyncCopies();
            if (RequirementNode* master = diagram->GetNode("SO-EXT")) {
                master->text = "Extension shall complete within 10 s at any airspeed below V_LO.";
            }

            diagram->SetStatusColor("Approved", Color(206, 236, 214));
            diagram->SetStatusColor("Proposed", Color(250, 238, 200));
            diagram->SetPackageRegionsVisible(true);
            diagram->SetCoverageOverlayVisible(true);
            diagram->SetLevelGap(78.0);
            diagram->SetSiblingGap(26.0);
            diagram->RunLayout();
        } else {
            diagram->SetTitle("ReqIF import failed", error);
        }

        diagram->SetMinimapVisible(true);
        diagram->SetControlsVisible(true);

        WireStatus(diagram, statusLabel);
        auto weakStatus = std::weak_ptr<UltraCanvasLabel>(statusLabel);
        auto weakDiagram = std::weak_ptr<UltraCanvasRequirementDiagram>(diagram);

        struct ActionButton { const char* id; const char* label; int action; };
        const ActionButton buttons[] = {
            {"ReqIfSearch", "Find \"cockpit\"", 0},
            {"ReqIfSync",   "Sync copies",      1},
            {"ReqIfMatrix", "Trace matrix CSV", 2},
            {"ReqIfForce",  "Force layout",     3}
        };
        int buttonX = 10;
        for (const auto& button : buttons) {
            auto widget = std::make_shared<UltraCanvasButton>(button.id, buttonX, 34, 150, 26,
                                                              button.label);
            const int action = button.action;
            widget->SetOnClick([weakDiagram, weakStatus, action]() {
                auto req = weakDiagram.lock();
                auto status = weakStatus.lock();
                if (!req || !status) return;

                if (action == 0) {
                    const std::string found = req->FindAndFocus("cockpit");
                    status->SetText(found.empty() ? "No match for \"cockpit\"."
                                                  : "Focused " + found);
                } else if (action == 1) {
                    const int synced = req->SyncCopies();
                    status->SetText("Re-synced " + std::to_string(synced) +
                                    " copy/copies.\nSuspect badges cleared.");
                } else if (action == 2) {
                    const std::string csv = req->ToTraceMatrixCsv();
                    std::string preview = csv.substr(0, 480);
                    if (csv.size() > preview.size()) preview += "\n...";
                    status->SetText("ToTraceMatrixCsv() — " + std::to_string(csv.size()) +
                                    " chars:\n\n" + preview);
                } else {
                    req->SetLayoutMode(RequirementLayoutMode::ForceDirected);
                    req->RunLayout();
                    status->SetText("Force-directed layout applied.\n"
                                    "Every visible relation acts as a spring.");
                }
            });
            tab->AddChild(widget);
            buttonX += 158;
        }

        tab->AddChild(diagram);
        return tab;
    }

// ─────────────────────────────────────────────────────────────────────────────
// SCENE
// ─────────────────────────────────────────────────────────────────────────────
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateRequirementDiagramExamples() {
        auto main = std::make_shared<UltraCanvasContainer>("ReqDiagramMain", 0, 0, 1040, 780);

        main->layout.SetGrid()
                .SetGridColumns({
                        CSSLayout::GridTrackSize{.kind = CSSLayout::GridTrackSizeKind::Fr,
                                                 .value = CSSLayout::Dimension::Fr(1)},
                        CSSLayout::GridTrackSize{.kind = CSSLayout::GridTrackSizeKind::Fixed,
                                                 .value = CSSLayout::Dimension::Px(350)}})
                .SetGridRows({
                        CSSLayout::GridTrackSize{.kind = CSSLayout::GridTrackSizeKind::Fixed,
                                                 .value = CSSLayout::Dimension::Px(34)},
                        CSSLayout::GridTrackSize{.kind = CSSLayout::GridTrackSizeKind::Fixed,
                                                 .value = CSSLayout::Dimension::Px(44)},
                        CSSLayout::GridTrackSize{.kind = CSSLayout::GridTrackSizeKind::Fr,
                                                 .value = CSSLayout::Dimension::Fr(1)}})
                .SetGridGap(6);

        auto title = std::make_shared<UltraCanvasLabel>("ReqTitle");
        title->SetText("Requirement Diagram");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(40, 100, 80));
        main->AddChild(title);
        title->layoutItem.SetGridRowColSimplified(0, 0);

        auto subtitle = std::make_shared<UltraCanvasLabel>("ReqSub");
        subtitle->SetText("SysML requirement diagram: compartmented requirement boxes, "
                          "containment with ⊕ crosshairs, and dashed deriveReqt / satisfy / "
                          "verify / refine / trace relationships.");
        subtitle->SetFontSize(11);
        subtitle->SetWrap(TextWrap::WrapWord);
        subtitle->SetTextColor(Color(90, 90, 90));
        main->AddChild(subtitle);
        subtitle->layoutItem.SetGridRowColSimplified(1, 0);

        auto statusLabel = std::make_shared<UltraCanvasLabel>("ReqStatus");
        statusLabel->SetText("Click a requirement or a relationship to inspect it.");
        statusLabel->SetFontSize(10);
        statusLabel->SetWrap(TextWrap::WrapWord);
        statusLabel->SetBackgroundColor(Color(245, 245, 245));
        statusLabel->SetBorders(1.0f);
        statusLabel->SetPadding(6.0f);
        main->AddChild(statusLabel);
        statusLabel->layoutItem.SetGridRowColSimplified(0, 1, 2, 1);

        auto tabs = std::make_shared<UltraCanvasTabbedContainer>("ReqTabs");
        tabs->SetTabPosition(TabPosition::Top);
        tabs->SetTabStyle(TabStyle::Modern);
        tabs->SetTabMaxWidth(300);

        tabs->AddTab("HybridSUV (compartments)",   MakeHybridSuvTab(statusLabel));
        tabs->AddTab("Specification + callout",    MakeSpecificationTab(statusLabel));
        tabs->AddTab("SysML taxonomy + legend",    MakeTaxonomyTab(statusLabel));
        tabs->AddTab("Smart Home traceability",    MakeSmartHomeTab(statusLabel));
        tabs->AddTab("Coverage + compartments",    MakeCoverageTab(statusLabel));
        tabs->AddTab("Mermaid import + editing",   MakeMermaidTab(statusLabel));
        tabs->AddTab("ReqIF + overlays",           MakeReqIfTab(statusLabel));

        main->AddChild(tabs);
        tabs->layoutItem.SetGridRowColSimplified(2, 0, 1, 2);
        return main;
    }

} // namespace UltraCanvas
