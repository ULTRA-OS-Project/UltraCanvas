// Plugins/Diagrams/UltraCanvasRequirementDiagram.cpp
// SysML requirement diagram component implementation
// Version: 1.0.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework
//
// See the header for the notation reference and the phase-1 feature mapping.
//
// Rendering conventions followed here (lessons carried over from the sibling
// diagram elements):
//   * DrawText's Y is the TOP of the text bounding box, not the baseline
//     (NodeDiagram 2.0.6 / FlowChart 2.1.2).
//   * Arrowheads are oriented by the direction of the LAST path segment, not
//     by the straight source-to-target vector (BlockDiagram 2.2.0).
//   * Connectors that share a box face are distributed along it so parallel
//     arrows never overlap (BlockDiagram 2.3.0).
//   * World-space content is drawn inside one Push/Pop transform; overlays
//     (title, legend) are drawn after PopState in screen space (NodeDiagram
//     2.0.0).

#include "Plugins/Diagrams/UltraCanvasRequirementDiagram.h"
#include "DataFormats/UltraCanvasJSON.h"
#include <algorithm>
#include <sstream>
#include <cmath>
#include <cctype>

namespace UltraCanvas {

// =============================================================================
// CONSTANTS
// =============================================================================

static constexpr double RELATION_HIT_TOLERANCE = 6.0;   // world px to a connector
static constexpr double MIN_NODE_HEIGHT = 26.0;
static const std::string kEllipsis = "...";

// =============================================================================
// STRING CONVERSION
// =============================================================================

const char* RequirementRiskToString(RequirementRisk risk) {
    switch (risk) {
        case RequirementRisk::Low:    return "Low";
        case RequirementRisk::Medium: return "Medium";
        case RequirementRisk::High:   return "High";
        case RequirementRisk::Unspecified: break;
    }
    return "";
}

static std::string ToLowerCopy(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

RequirementRisk RequirementRiskFromString(const std::string& text) {
    const std::string t = ToLowerCopy(text);
    if (t == "low")    return RequirementRisk::Low;
    if (t == "medium") return RequirementRisk::Medium;
    if (t == "high")   return RequirementRisk::High;
    return RequirementRisk::Unspecified;
}

const char* RequirementVerifyMethodToString(RequirementVerifyMethod method) {
    switch (method) {
        case RequirementVerifyMethod::Analysis:      return "Analysis";
        case RequirementVerifyMethod::Inspection:    return "Inspection";
        case RequirementVerifyMethod::Test:          return "Test";
        case RequirementVerifyMethod::Demonstration: return "Demonstration";
        case RequirementVerifyMethod::Unspecified: break;
    }
    return "";
}

RequirementVerifyMethod RequirementVerifyMethodFromString(const std::string& text) {
    const std::string t = ToLowerCopy(text);
    if (t == "analysis")      return RequirementVerifyMethod::Analysis;
    if (t == "inspection")    return RequirementVerifyMethod::Inspection;
    if (t == "test")          return RequirementVerifyMethod::Test;
    if (t == "demonstration") return RequirementVerifyMethod::Demonstration;
    return RequirementVerifyMethod::Unspecified;
}

const char* RequirementNodeKindToString(RequirementNodeKind kind) {
    switch (kind) {
        case RequirementNodeKind::Requirement: return "requirement";
        case RequirementNodeKind::Block:       return "block";
        case RequirementNodeKind::TestCase:    return "testCase";
        case RequirementNodeKind::UseCase:     return "useCase";
        case RequirementNodeKind::Actor:       return "actor";
        case RequirementNodeKind::Package:     return "package";
        case RequirementNodeKind::Rationale:   return "rationale";
        case RequirementNodeKind::Problem:     return "problem";
        case RequirementNodeKind::Note:        return "note";
    }
    return "requirement";
}

RequirementNodeKind RequirementNodeKindFromString(const std::string& text) {
    const std::string t = ToLowerCopy(text);
    if (t == "block")     return RequirementNodeKind::Block;
    if (t == "testcase")  return RequirementNodeKind::TestCase;
    if (t == "usecase")   return RequirementNodeKind::UseCase;
    if (t == "actor")     return RequirementNodeKind::Actor;
    if (t == "package")   return RequirementNodeKind::Package;
    if (t == "rationale") return RequirementNodeKind::Rationale;
    if (t == "problem")   return RequirementNodeKind::Problem;
    if (t == "note")      return RequirementNodeKind::Note;
    return RequirementNodeKind::Requirement;
}

const char* RequirementRelationKindToString(RequirementRelationKind kind) {
    switch (kind) {
        case RequirementRelationKind::Containment:    return "containment";
        case RequirementRelationKind::DeriveReqt:     return "deriveReqt";
        case RequirementRelationKind::Satisfy:        return "satisfy";
        case RequirementRelationKind::Verify:         return "verify";
        case RequirementRelationKind::Refine:         return "refine";
        case RequirementRelationKind::Trace:          return "trace";
        case RequirementRelationKind::Copy:           return "copy";
        case RequirementRelationKind::Generalization: return "generalization";
    }
    return "trace";
}

RequirementRelationKind RequirementRelationKindFromString(const std::string& text) {
    const std::string t = ToLowerCopy(text);
    if (t == "containment" || t == "contains") return RequirementRelationKind::Containment;
    if (t == "derivereqt"  || t == "derives")  return RequirementRelationKind::DeriveReqt;
    if (t == "satisfy"     || t == "satisfies") return RequirementRelationKind::Satisfy;
    if (t == "verify"      || t == "verifies") return RequirementRelationKind::Verify;
    if (t == "refine"      || t == "refines")  return RequirementRelationKind::Refine;
    if (t == "copy"        || t == "copies")   return RequirementRelationKind::Copy;
    if (t == "generalization") return RequirementRelationKind::Generalization;
    return RequirementRelationKind::Trace;
}

const char* RequirementDefaultStereotype(RequirementNodeKind kind) {
    // Same spelling as the kind, which is what SysML tools print between the
    // guillemets. Requirement specialisations override this per node.
    return RequirementNodeKindToString(kind);
}

const char* RequirementRelationLabel(RequirementRelationKind kind) {
    switch (kind) {
        case RequirementRelationKind::DeriveReqt: return "deriveReqt";
        case RequirementRelationKind::Satisfy:    return "satisfy";
        case RequirementRelationKind::Verify:     return "verify";
        case RequirementRelationKind::Refine:     return "refine";
        case RequirementRelationKind::Trace:      return "trace";
        case RequirementRelationKind::Copy:       return "copy";
        // Containment and generalisation carry notation, not a keyword.
        case RequirementRelationKind::Containment:
        case RequirementRelationKind::Generalization: break;
    }
    return "";
}

// =============================================================================
// TEMPLATES
// =============================================================================

RequirementNodeTemplate RequirementNodeTemplate::Extended() {
    RequirementNodeTemplate tpl;
    tpl.AddPropertyRow("id", RequirementField::Id);
    tpl.AddPropertyRow("source", RequirementField::Source);
    tpl.AddPropertyRow("text", RequirementField::Text);
    tpl.AddPropertyRow("verifyMethod", RequirementField::VerifyMethod);
    tpl.AddPropertyRow("risk", RequirementField::Risk);
    return tpl;
}

RequirementNodeTemplate RequirementNodeTemplate::Standard() {
    RequirementNodeTemplate tpl;
    tpl.AddPropertyRow("id", RequirementField::Id);
    tpl.AddPropertyRow("text", RequirementField::Text);
    return tpl;
}

// =============================================================================
// PALETTE
// =============================================================================

Color RequirementPalette::FillForKind(RequirementNodeKind kind) const {
    switch (kind) {
        case RequirementNodeKind::Requirement: return requirementFill;
        case RequirementNodeKind::Block:       return blockFill;
        case RequirementNodeKind::TestCase:    return testCaseFill;
        case RequirementNodeKind::UseCase:     return useCaseFill;
        case RequirementNodeKind::Actor:       return actorFill;
        case RequirementNodeKind::Package:     return packageFill;
        case RequirementNodeKind::Rationale:
        case RequirementNodeKind::Problem:
        case RequirementNodeKind::Note:        return noteFill;
    }
    return requirementFill;
}

Color RequirementPalette::BorderForKind(RequirementNodeKind kind) const {
    switch (kind) {
        case RequirementNodeKind::Requirement: return requirementBorder;
        case RequirementNodeKind::Block:       return blockBorder;
        case RequirementNodeKind::TestCase:    return testCaseBorder;
        case RequirementNodeKind::UseCase:     return useCaseBorder;
        case RequirementNodeKind::Actor:       return actorBorder;
        case RequirementNodeKind::Package:     return packageBorder;
        case RequirementNodeKind::Rationale:
        case RequirementNodeKind::Problem:
        case RequirementNodeKind::Note:        return noteBorder;
    }
    return requirementBorder;
}

Color RequirementPalette::ColorForRelation(RequirementRelationKind kind) const {
    switch (kind) {
        case RequirementRelationKind::Containment:    return containmentColor;
        case RequirementRelationKind::DeriveReqt:     return deriveReqtColor;
        case RequirementRelationKind::Satisfy:        return satisfyColor;
        case RequirementRelationKind::Verify:         return verifyColor;
        case RequirementRelationKind::Refine:         return refineColor;
        case RequirementRelationKind::Trace:          return traceColor;
        case RequirementRelationKind::Copy:           return copyColor;
        case RequirementRelationKind::Generalization: return generalizationColor;
    }
    return traceColor;
}

Color RequirementPalette::ColorForRisk(RequirementRisk risk) const {
    switch (risk) {
        case RequirementRisk::Low:    return riskLowColor;
        case RequirementRisk::Medium: return riskMediumColor;
        case RequirementRisk::High:   return riskHighColor;
        case RequirementRisk::Unspecified: break;
    }
    return Color(235, 235, 238, 255);
}

RequirementPalette RequirementPalette::BuiltIn(RequirementPaletteKind kind) {
    RequirementPalette p;   // defaults are the Classic palette
    switch (kind) {
        case RequirementPaletteKind::Classic:
        case RequirementPaletteKind::Custom:
            break;

        case RequirementPaletteKind::Pastel:
            // Reference images 1-2: soft yellow / green fills, thin grey borders.
            p.backgroundColor = Color(253, 253, 250, 255);
            p.requirementFill = Color(226, 244, 234, 255);
            p.requirementBorder = Color(120, 160, 135, 255);
            p.blockFill = Color(214, 236, 248, 255);
            p.blockBorder = Color(110, 150, 175, 255);
            p.testCaseFill = Color(226, 240, 226, 255);
            p.testCaseBorder = Color(125, 165, 125, 255);
            p.noteFill = Color(255, 250, 220, 255);
            p.headerTextColor = Color(40, 55, 48, 255);
            p.stereotypeTextColor = Color(95, 110, 100, 255);
            p.propertyTextColor = Color(55, 68, 60, 255);
            p.dividerColor = Color(160, 190, 172, 255);
            p.categoryColors = {
                Color(252, 244, 200, 255),   // yellow  - Load
                Color(212, 240, 226, 255),   // green   - Eco-Friendliness
                Color(206, 236, 240, 255),   // teal
                Color(232, 224, 246, 255),   // lilac
                Color(244, 226, 214, 255),   // peach
                Color(226, 236, 250, 255)    // blue
            };
            break;

        case RequirementPaletteKind::Vibrant:
            // Reference image 4: saturated colour-by-kind on a light canvas.
            p.backgroundColor = Color(250, 250, 252, 255);
            p.requirementFill = Color(196, 236, 210, 255);
            p.requirementBorder = Color(60, 150, 100, 255);
            p.blockFill = Color(186, 218, 246, 255);
            p.blockBorder = Color(40, 110, 180, 255);
            p.testCaseFill = Color(180, 226, 232, 255);
            p.testCaseBorder = Color(30, 130, 150, 255);
            p.useCaseFill = Color(222, 206, 246, 255);
            p.useCaseBorder = Color(110, 70, 180, 255);
            p.actorFill = Color(250, 226, 186, 255);
            p.actorBorder = Color(190, 120, 30, 255);
            p.headerBandColor = Color(0, 0, 0, 18);
            break;

        case RequirementPaletteKind::Professional:
            p.backgroundColor = Color(247, 249, 252, 255);
            p.requirementFill = Color(255, 255, 255, 255);
            p.requirementBorder = Color(96, 116, 148, 255);
            p.headerBandColor = Color(232, 238, 246, 255);
            p.headerTextColor = Color(32, 46, 70, 255);
            p.stereotypeTextColor = Color(96, 112, 136, 255);
            p.blockFill = Color(236, 242, 250, 255);
            p.blockBorder = Color(80, 110, 150, 255);
            p.testCaseFill = Color(236, 246, 244, 255);
            p.testCaseBorder = Color(70, 130, 124, 255);
            p.dividerColor = Color(198, 210, 226, 255);
            break;

        case RequirementPaletteKind::Dark:
            p.backgroundColor = Color(30, 32, 38, 255);
            p.gridColor = Color(48, 52, 60, 255);
            p.requirementFill = Color(46, 50, 60, 255);
            p.requirementBorder = Color(120, 132, 152, 255);
            p.blockFill = Color(40, 56, 76, 255);
            p.blockBorder = Color(90, 140, 190, 255);
            p.testCaseFill = Color(38, 60, 58, 255);
            p.testCaseBorder = Color(80, 160, 150, 255);
            p.useCaseFill = Color(54, 44, 70, 255);
            p.useCaseBorder = Color(150, 120, 200, 255);
            p.actorFill = Color(60, 54, 40, 255);
            p.actorBorder = Color(180, 155, 90, 255);
            p.packageFill = Color(44, 46, 52, 255);
            p.noteFill = Color(58, 54, 38, 255);
            p.noteBorder = Color(150, 140, 90, 255);
            p.headerTextColor = Color(235, 238, 244, 255);
            p.stereotypeTextColor = Color(160, 168, 182, 255);
            p.propertyTextColor = Color(206, 212, 222, 255);
            p.dividerColor = Color(90, 96, 110, 255);
            p.containmentColor = Color(170, 178, 194, 255);
            p.traceColor = Color(140, 148, 164, 255);
            p.relationLabelColor = Color(212, 218, 228, 255);
            p.relationLabelBackground = Color(30, 32, 38, 225);
            p.calloutFill = Color(58, 54, 38, 255);
            p.calloutBorder = Color(150, 140, 90, 255);
            p.categoryColors = {
                Color(58, 60, 44, 255), Color(40, 60, 52, 255),
                Color(40, 54, 68, 255), Color(54, 44, 66, 255),
                Color(60, 48, 42, 255), Color(44, 50, 64, 255)
            };
            break;

        case RequirementPaletteKind::Monochrome:
            p.backgroundColor = Color(255, 255, 255, 255);
            p.gridColor = Color(238, 238, 238, 255);
            p.requirementFill = Color(255, 255, 255, 255);
            p.requirementBorder = Color(40, 40, 40, 255);
            p.blockFill = Color(238, 238, 238, 255);
            p.blockBorder = Color(40, 40, 40, 255);
            p.testCaseFill = Color(246, 246, 246, 255);
            p.testCaseBorder = Color(40, 40, 40, 255);
            p.useCaseFill = Color(232, 232, 232, 255);
            p.useCaseBorder = Color(40, 40, 40, 255);
            p.actorFill = Color(244, 244, 244, 255);
            p.actorBorder = Color(40, 40, 40, 255);
            p.packageFill = Color(242, 242, 242, 255);
            p.packageBorder = Color(40, 40, 40, 255);
            p.noteFill = Color(250, 250, 250, 255);
            p.noteBorder = Color(60, 60, 60, 255);
            p.headerTextColor = Color(0, 0, 0, 255);
            p.stereotypeTextColor = Color(70, 70, 70, 255);
            p.propertyTextColor = Color(35, 35, 35, 255);
            p.dividerColor = Color(90, 90, 90, 255);
            p.containmentColor = Color(20, 20, 20, 255);
            p.deriveReqtColor = Color(60, 60, 60, 255);
            p.satisfyColor = Color(60, 60, 60, 255);
            p.verifyColor = Color(60, 60, 60, 255);
            p.refineColor = Color(60, 60, 60, 255);
            p.traceColor = Color(90, 90, 90, 255);
            p.copyColor = Color(90, 90, 90, 255);
            p.generalizationColor = Color(20, 20, 20, 255);
            p.calloutFill = Color(250, 250, 250, 255);
            p.calloutBorder = Color(60, 60, 60, 255);
            p.categoryColors = {
                Color(245, 245, 245, 255), Color(232, 232, 232, 255),
                Color(220, 220, 220, 255), Color(208, 208, 208, 255),
                Color(196, 196, 196, 255), Color(184, 184, 184, 255)
            };
            break;
    }
    return p;
}

// =============================================================================
// CONSTRUCTOR
// =============================================================================

UltraCanvasRequirementDiagram::UltraCanvasRequirementDiagram(
        const std::string& id, int x, int y, int width, int height)
    : UltraCanvasUIElement(id, x, y, width, height) {
    nodeTemplate = RequirementNodeTemplate::Extended();
    palette = RequirementPalette::BuiltIn(RequirementPaletteKind::Classic);
}

// =============================================================================
// NODE MANAGEMENT
// =============================================================================

bool UltraCanvasRequirementDiagram::AddNode(const RequirementNode& node) {
    if (node.id.empty()) {
        RequirementWarning w;
        w.kind = RequirementWarning::Kind::DuplicateNodeId;
        w.message = "Node rejected: empty id";
        ReportWarning(w);
        return false;
    }
    if (nodes.find(node.id) != nodes.end()) {
        RequirementWarning w;
        w.kind = RequirementWarning::Kind::DuplicateNodeId;
        w.nodeId = node.id;
        w.message = "Duplicate node id '" + node.id + "' - node not added";
        ReportWarning(w);
        return false;
    }
    nodes[node.id] = node;
    nodeOrder.push_back(node.id);
    InvalidateMeasurement();
    return true;
}

bool UltraCanvasRequirementDiagram::AddNode(RequirementNodeKind kind,
                                            const std::string& id,
                                            const std::string& name) {
    RequirementNode node(id, name);
    node.kind = kind;
    node.detail = (kind == RequirementNodeKind::Requirement)
                      ? defaultDetail
                      : RequirementDetailLevel::Collapsed;
    return AddNode(node);
}

bool UltraCanvasRequirementDiagram::AddRequirement(const std::string& id,
                                                   const std::string& name,
                                                   const std::string& text) {
    RequirementNode node(id, name);
    node.kind = RequirementNodeKind::Requirement;
    node.text = text;
    node.detail = defaultDetail;
    return AddNode(node);
}

bool UltraCanvasRequirementDiagram::AddRequirement(const std::string& id,
                                                   const std::string& name,
                                                   const std::string& text,
                                                   RequirementRisk risk,
                                                   RequirementVerifyMethod verifyMethod) {
    RequirementNode node(id, name);
    node.kind = RequirementNodeKind::Requirement;
    node.text = text;
    node.risk = risk;
    node.verifyMethod = verifyMethod;
    node.detail = defaultDetail;
    return AddNode(node);
}

bool UltraCanvasRequirementDiagram::RemoveNode(const std::string& id) {
    auto it = nodes.find(id);
    if (it == nodes.end()) return false;

    nodes.erase(it);
    nodeOrder.erase(std::remove(nodeOrder.begin(), nodeOrder.end(), id), nodeOrder.end());
    relations.erase(std::remove_if(relations.begin(), relations.end(),
                                   [&id](const RequirementRelation& r) {
                                       return r.sourceId == id || r.targetId == id;
                                   }),
                    relations.end());
    callouts.erase(std::remove_if(callouts.begin(), callouts.end(),
                                  [&id](const RequirementCallout& c) {
                                      return c.targetNodeId == id;
                                  }),
                   callouts.end());
    selectedNodes.erase(id);
    measuredNodes.erase(id);
    InvalidateMeasurement();
    return true;
}

void UltraCanvasRequirementDiagram::Clear() {
    nodes.clear();
    nodeOrder.clear();
    relations.clear();
    callouts.clear();
    selectedNodes.clear();
    selectedRelations.clear();
    measuredNodes.clear();
    measuredCallouts.clear();
    hoveredNodeId.clear();
    hoveredRelationId.clear();
    hoveredCalloutId.clear();
    nextNodeSerial = 1;
    nextRelationSerial = 1;
    InvalidateMeasurement();
}

RequirementNode* UltraCanvasRequirementDiagram::GetNode(const std::string& id) {
    auto it = nodes.find(id);
    return it == nodes.end() ? nullptr : &it->second;
}

const RequirementNode* UltraCanvasRequirementDiagram::GetNode(const std::string& id) const {
    auto it = nodes.find(id);
    return it == nodes.end() ? nullptr : &it->second;
}

std::vector<std::string> UltraCanvasRequirementDiagram::GetAllNodeIds() const {
    return nodeOrder;
}

void UltraCanvasRequirementDiagram::SetNodePosition(const std::string& id, double x, double y) {
    if (auto* node = GetNode(id)) {
        node->x = x;
        node->y = y;
        RequestRedraw();
    }
}

void UltraCanvasRequirementDiagram::SetNodeSize(const std::string& id, double width, double height) {
    if (auto* node = GetNode(id)) {
        node->width = width;
        node->height = height;
        node->hasExplicitWidth = width > 0.0;
        InvalidateMeasurement();
    }
}

void UltraCanvasRequirementDiagram::SetNodeDetail(const std::string& id,
                                                   RequirementDetailLevel detail) {
    if (auto* node = GetNode(id)) {
        node->detail = detail;
        InvalidateMeasurement();
    }
}

void UltraCanvasRequirementDiagram::SetNodeCategory(const std::string& id,
                                                     const std::string& category) {
    if (auto* node = GetNode(id)) {
        node->category = category;
        RequestRedraw();
    }
}

void UltraCanvasRequirementDiagram::SetNodeColors(const std::string& id,
                                                   const Color& fill, const Color& border) {
    if (auto* node = GetNode(id)) {
        node->hasFillColor = true;
        node->fillColor = fill;
        node->hasBorderColor = true;
        node->borderColor = border;
        RequestRedraw();
    }
}

void UltraCanvasRequirementDiagram::SetNodePinned(const std::string& id, bool pinned) {
    if (auto* node = GetNode(id)) {
        node->pinned = pinned;
    }
}

void UltraCanvasRequirementDiagram::SetNodeTemplate(const std::string& nodeId,
                                                     const RequirementNodeTemplate& tpl) {
    if (auto* node = GetNode(nodeId)) {
        node->customTemplate = tpl;
        node->hasCustomTemplate = true;
        node->detail = RequirementDetailLevel::Custom;
        InvalidateMeasurement();
    }
}

std::string UltraCanvasRequirementDiagram::GenerateNodeId(const std::string& prefix) {
    std::string candidate;
    do {
        candidate = prefix + std::to_string(nextNodeSerial++);
    } while (nodes.find(candidate) != nodes.end());
    return candidate;
}

// =============================================================================
// RELATIONSHIP MANAGEMENT
// =============================================================================

std::string UltraCanvasRequirementDiagram::AddRelation(const RequirementRelation& relation) {
    RequirementRelation rel = relation;

    if (nodes.find(rel.sourceId) == nodes.end() ||
        nodes.find(rel.targetId) == nodes.end()) {
        RequirementWarning w;
        w.kind = RequirementWarning::Kind::UnknownEndpoint;
        w.relationId = rel.id;
        w.message = "Relation '" + rel.sourceId + "' -> '" + rel.targetId +
                    "' references an unknown node - not added";
        ReportWarning(w);
        return "";
    }

    if (rel.id.empty()) {
        do {
            rel.id = "REL" + std::to_string(nextRelationSerial++);
        } while (GetRelation(rel.id) != nullptr);
    } else if (GetRelation(rel.id) != nullptr) {
        RequirementWarning w;
        w.kind = RequirementWarning::Kind::DuplicateRelation;
        w.relationId = rel.id;
        w.message = "Duplicate relation id '" + rel.id + "' - relation not added";
        ReportWarning(w);
        return "";
    }

    // A requirement has exactly one owner in SysML. A second containment
    // parent is legal to draw but almost always a modelling mistake, so it is
    // reported while still being accepted.
    if (rel.kind == RequirementRelationKind::Containment) {
        const std::string existingParent = GetParentId(rel.targetId);
        if (!existingParent.empty() && existingParent != rel.sourceId) {
            RequirementWarning w;
            w.kind = RequirementWarning::Kind::MultipleParents;
            w.nodeId = rel.targetId;
            w.relationId = rel.id;
            w.message = "'" + rel.targetId + "' is already contained by '" +
                        existingParent + "'";
            ReportWarning(w);
        }
    }

    relations.push_back(rel);
    if (layoutMode == RequirementLayoutMode::ContainmentTree) {
        layoutDirty = true;
    }
    RequestRedraw();
    return rel.id;
}

std::string UltraCanvasRequirementDiagram::AddRelation(RequirementRelationKind kind,
                                                        const std::string& sourceId,
                                                        const std::string& targetId) {
    return AddRelation(RequirementRelation(kind, sourceId, targetId));
}

std::string UltraCanvasRequirementDiagram::AddContainment(const std::string& parentId,
                                                           const std::string& childId) {
    return AddRelation(RequirementRelation(RequirementRelationKind::Containment,
                                            parentId, childId));
}

bool UltraCanvasRequirementDiagram::RemoveRelation(const std::string& id) {
    auto it = std::find_if(relations.begin(), relations.end(),
                           [&id](const RequirementRelation& r) { return r.id == id; });
    if (it == relations.end()) return false;
    relations.erase(it);
    selectedRelations.erase(id);
    if (layoutMode == RequirementLayoutMode::ContainmentTree) layoutDirty = true;
    RequestRedraw();
    return true;
}

RequirementRelation* UltraCanvasRequirementDiagram::GetRelation(const std::string& id) {
    for (auto& r : relations) {
        if (r.id == id) return &r;
    }
    return nullptr;
}

const RequirementRelation* UltraCanvasRequirementDiagram::GetRelation(const std::string& id) const {
    for (const auto& r : relations) {
        if (r.id == id) return &r;
    }
    return nullptr;
}

std::vector<std::string> UltraCanvasRequirementDiagram::GetAllRelationIds() const {
    std::vector<std::string> ids;
    ids.reserve(relations.size());
    for (const auto& r : relations) ids.push_back(r.id);
    return ids;
}

void UltraCanvasRequirementDiagram::SetRelationLabel(const std::string& id,
                                                      const std::string& label) {
    if (auto* rel = GetRelation(id)) {
        rel->label = label;
        RequestRedraw();
    }
}

void UltraCanvasRequirementDiagram::SetRelationColor(const std::string& id, const Color& color) {
    if (auto* rel = GetRelation(id)) {
        rel->hasColor = true;
        rel->color = color;
        RequestRedraw();
    }
}

void UltraCanvasRequirementDiagram::SetRelationRouting(const std::string& id,
                                                        RequirementRouting routing) {
    if (auto* rel = GetRelation(id)) {
        rel->routing = routing;
        rel->useDefaultRouting = false;
        RequestRedraw();
    }
}

void UltraCanvasRequirementDiagram::SetRelationVisible(const std::string& id, bool visible) {
    if (auto* rel = GetRelation(id)) {
        rel->visible = visible;
        RequestRedraw();
    }
}

void UltraCanvasRequirementDiagram::SetRelationKindVisible(RequirementRelationKind kind,
                                                            bool visible) {
    for (auto& relation : relations) {
        if (relation.kind == kind) relation.visible = visible;
    }
    RequestRedraw();
}

std::string UltraCanvasRequirementDiagram::GetParentId(const std::string& nodeId) const {
    for (const auto& r : relations) {
        if (r.kind == RequirementRelationKind::Containment && r.targetId == nodeId) {
            return r.sourceId;
        }
    }
    return "";
}

std::vector<std::string> UltraCanvasRequirementDiagram::GetChildIds(const std::string& nodeId) const {
    std::vector<std::string> children;
    for (const auto& r : relations) {
        if (r.kind == RequirementRelationKind::Containment && r.sourceId == nodeId) {
            children.push_back(r.targetId);
        }
    }
    return children;
}

std::vector<std::string> UltraCanvasRequirementDiagram::GetRootIds() const {
    std::vector<std::string> roots;
    for (const auto& id : nodeOrder) {
        if (GetParentId(id).empty()) roots.push_back(id);
    }
    return roots;
}

std::vector<std::string> UltraCanvasRequirementDiagram::GetRelationsOf(
        const std::string& nodeId) const {
    std::vector<std::string> ids;
    for (const auto& r : relations) {
        if (r.sourceId == nodeId || r.targetId == nodeId) ids.push_back(r.id);
    }
    return ids;
}

void UltraCanvasRequirementDiagram::BuildFromRelations(
        const std::vector<RequirementNode>& nodeList,
        const std::vector<RequirementRelation>& relationList) {
    Clear();
    for (const auto& n : nodeList) AddNode(n);
    for (const auto& r : relationList) AddRelation(r);
    if (layoutMode == RequirementLayoutMode::Manual) {
        layoutMode = RequirementLayoutMode::ContainmentTree;
    }
    RunLayout();
}

// =============================================================================
// MODEL INTEGRITY
// =============================================================================

std::vector<std::string> UltraCanvasRequirementDiagram::GetContainmentCycleNodes() const {
    // Iterative colouring DFS over the containment edges. Any node still grey
    // when reached again is part of a cycle.
    enum class Mark { White, Grey, Black };
    std::map<std::string, Mark> mark;
    for (const auto& id : nodeOrder) mark[id] = Mark::White;

    std::vector<std::string> cycleNodes;
    std::set<std::string> cycleSet;

    for (const auto& startId : nodeOrder) {
        if (mark[startId] != Mark::White) continue;

        // (node, next child index) stack, plus the active path.
        std::vector<std::pair<std::string, size_t>> stack;
        std::vector<std::string> path;
        stack.emplace_back(startId, 0);
        mark[startId] = Mark::Grey;
        path.push_back(startId);

        while (!stack.empty()) {
            auto& [nodeId, childIndex] = stack.back();
            const std::vector<std::string> children = GetChildIds(nodeId);

            if (childIndex < children.size()) {
                const std::string child = children[childIndex++];
                auto markIt = mark.find(child);
                if (markIt == mark.end()) continue;      // dangling edge
                if (markIt->second == Mark::Grey) {
                    // Everything from `child` to the top of the path is a cycle.
                    bool collecting = false;
                    for (const auto& p : path) {
                        if (p == child) collecting = true;
                        if (collecting && cycleSet.insert(p).second) {
                            cycleNodes.push_back(p);
                        }
                    }
                } else if (markIt->second == Mark::White) {
                    markIt->second = Mark::Grey;
                    stack.emplace_back(child, 0);
                    path.push_back(child);
                }
            } else {
                mark[nodeId] = Mark::Black;
                stack.pop_back();
                if (!path.empty()) path.pop_back();
            }
        }
    }
    return cycleNodes;
}

bool UltraCanvasRequirementDiagram::HasContainmentCycle() const {
    return !GetContainmentCycleNodes().empty();
}

std::vector<RequirementWarning> UltraCanvasRequirementDiagram::Validate() const {
    std::vector<RequirementWarning> warnings;

    for (const auto& r : relations) {
        if (nodes.find(r.sourceId) == nodes.end() || nodes.find(r.targetId) == nodes.end()) {
            RequirementWarning w;
            w.kind = RequirementWarning::Kind::UnknownEndpoint;
            w.relationId = r.id;
            w.message = "Relation '" + r.id + "' references an unknown node";
            warnings.push_back(w);
        }
    }

    std::map<std::string, int> parentCount;
    for (const auto& r : relations) {
        if (r.kind == RequirementRelationKind::Containment) parentCount[r.targetId]++;
    }
    for (const auto& [nodeId, count] : parentCount) {
        if (count > 1) {
            RequirementWarning w;
            w.kind = RequirementWarning::Kind::MultipleParents;
            w.nodeId = nodeId;
            w.message = "'" + nodeId + "' has " + std::to_string(count) +
                        " containment parents";
            warnings.push_back(w);
        }
    }

    const std::vector<std::string> cycle = GetContainmentCycleNodes();
    if (!cycle.empty()) {
        RequirementWarning w;
        w.kind = RequirementWarning::Kind::ContainmentCycle;
        w.nodeId = cycle.front();
        w.message = "Containment cycle through " + std::to_string(cycle.size()) +
                    " node(s), starting at '" + cycle.front() + "'";
        warnings.push_back(w);
    }

    for (const auto& w : warnings) ReportWarning(w);
    return warnings;
}

// =============================================================================
// TEMPLATE / DETAIL LEVEL
// =============================================================================

void UltraCanvasRequirementDiagram::SetNodeTemplate(const RequirementNodeTemplate& tpl) {
    nodeTemplate = tpl;
    InvalidateMeasurement();
}

void UltraCanvasRequirementDiagram::SetPropertyFormat(RequirementPropertyFormat format) {
    nodeTemplate.format = format;
    InvalidateMeasurement();
}

void UltraCanvasRequirementDiagram::SetDefaultDetailLevel(RequirementDetailLevel detail) {
    defaultDetail = detail;
    InvalidateMeasurement();
}

const RequirementNodeTemplate& UltraCanvasRequirementDiagram::TemplateForNode(
        const RequirementNode& node) const {
    if (node.detail == RequirementDetailLevel::Custom && node.hasCustomTemplate) {
        return node.customTemplate;
    }
    return nodeTemplate;
}

std::vector<RequirementPropertyRow> UltraCanvasRequirementDiagram::RowsForNode(
        const RequirementNode& node) const {
    std::vector<RequirementPropertyRow> rows;

    switch (node.detail) {
        case RequirementDetailLevel::Collapsed:
            return rows;                                    // header only

        case RequirementDetailLevel::Standard: {
            static const RequirementNodeTemplate standard = RequirementNodeTemplate::Standard();
            rows = standard.rows;
            break;
        }
        case RequirementDetailLevel::Full:
        case RequirementDetailLevel::Custom:
            rows = TemplateForNode(node).rows;
            break;
    }

    // Q17: drop rows whose value is empty so a leaf box with no properties
    // renders as a plain name box rather than an empty compartment.
    std::vector<RequirementPropertyRow> kept;
    kept.reserve(rows.size());
    for (const auto& row : rows) {
        if (!ResolveField(node, row.field, row.customKey, row.literal).empty()) {
            kept.push_back(row);
        }
    }
    return kept;
}

std::string UltraCanvasRequirementDiagram::ResolveField(const RequirementNode& node,
                                                         RequirementField field,
                                                         const std::string& customKey,
                                                         const std::string& literal) const {
    switch (field) {
        case RequirementField::Id:   return node.id;
        case RequirementField::Name: return node.name;
        case RequirementField::Stereotype:
            return node.stereotype.empty() ? RequirementDefaultStereotype(node.kind)
                                            : node.stereotype;
        case RequirementField::Text:     return node.text;
        case RequirementField::Source:   return node.source;
        case RequirementField::Risk:     return RequirementRiskToString(node.risk);
        case RequirementField::VerifyMethod:
            return RequirementVerifyMethodToString(node.verifyMethod);
        case RequirementField::Status:   return node.status;
        case RequirementField::Owner:    return node.owner;
        case RequirementField::Priority: return node.priority;
        case RequirementField::DocRef:   return node.docRef;
        case RequirementField::Custom: {
            if (customKey.empty()) return literal;
            auto it = node.customProperties.find(customKey);
            return it == node.customProperties.end() ? std::string() : it->second;
        }
    }
    return "";
}

std::string UltraCanvasRequirementDiagram::FormatPropertyRow(
        const std::string& key, const std::string& value,
        RequirementPropertyFormat format) const {
    if (format == RequirementPropertyFormat::KeyColonValue) {
        return key + ": " + value;
    }
    return key + "=\"" + value + "\"";
}

// =============================================================================
// CALLOUTS
// =============================================================================

bool UltraCanvasRequirementDiagram::AddCallout(const RequirementCallout& callout) {
    if (callout.id.empty()) return false;
    if (nodes.find(callout.targetNodeId) == nodes.end()) {
        RequirementWarning w;
        w.kind = RequirementWarning::Kind::UnknownEndpoint;
        w.nodeId = callout.targetNodeId;
        w.message = "Callout '" + callout.id + "' targets unknown node '" +
                    callout.targetNodeId + "'";
        ReportWarning(w);
        return false;
    }
    for (const auto& c : callouts) {
        if (c.id == callout.id) return false;
    }
    callouts.push_back(callout);
    InvalidateMeasurement();
    return true;
}

bool UltraCanvasRequirementDiagram::AddCallout(const std::string& calloutId,
                                                const std::string& targetNodeId,
                                                const std::vector<RequirementField>& fields,
                                                double x, double y) {
    RequirementCallout callout(calloutId, targetNodeId, x, y);
    callout.fields = fields;
    return AddCallout(callout);
}

bool UltraCanvasRequirementDiagram::RemoveCallout(const std::string& id) {
    auto it = std::find_if(callouts.begin(), callouts.end(),
                           [&id](const RequirementCallout& c) { return c.id == id; });
    if (it == callouts.end()) return false;
    callouts.erase(it);
    measuredCallouts.erase(id);
    RequestRedraw();
    return true;
}

RequirementCallout* UltraCanvasRequirementDiagram::GetCallout(const std::string& id) {
    for (auto& c : callouts) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

std::vector<std::string> UltraCanvasRequirementDiagram::GetAllCalloutIds() const {
    std::vector<std::string> ids;
    ids.reserve(callouts.size());
    for (const auto& c : callouts) ids.push_back(c.id);
    return ids;
}

// =============================================================================
// CATEGORIES & LEGEND
// =============================================================================

void UltraCanvasRequirementDiagram::AddCategory(const RequirementCategory& category) {
    if (category.name.empty()) return;
    if (categories.find(category.name) == categories.end()) {
        categoryOrder.push_back(category.name);
    }
    categories[category.name] = category;
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::AddCategory(const std::string& name,
                                                 const Color& fill, const Color& border) {
    AddCategory(RequirementCategory(name, fill, border));
}

const RequirementCategory* UltraCanvasRequirementDiagram::GetCategory(
        const std::string& name) const {
    auto it = categories.find(name);
    return it == categories.end() ? nullptr : &it->second;
}

std::vector<std::string> UltraCanvasRequirementDiagram::GetCategoryNames() const {
    return categoryOrder;
}

void UltraCanvasRequirementDiagram::SetLegendVisible(bool visible) {
    legendConfig.visible = visible;
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SetLegendVisible(bool visible,
                                                      RequirementPanelPosition position) {
    legendConfig.visible = visible;
    legendConfig.position = position;
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SetLegendSource(RequirementLegendSource source) {
    legendConfig.source = source;
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SetLegendEntries(
        const std::vector<RequirementLegendEntry>& entries) {
    customLegendEntries = entries;
    legendConfig.source = RequirementLegendSource::Custom;
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SetLegendConfig(const RequirementLegendConfig& config) {
    legendConfig = config;
    RequestRedraw();
}

// =============================================================================
// TITLE
// =============================================================================

void UltraCanvasRequirementDiagram::SetTitle(const std::string& title) {
    titleConfig.title = title;
    titleConfig.visible = !title.empty();
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SetTitle(const std::string& title,
                                              const std::string& subtitle) {
    titleConfig.title = title;
    titleConfig.subtitle = subtitle;
    titleConfig.visible = !title.empty();
    if (!subtitle.empty() && titleConfig.height < 46.0) titleConfig.height = 46.0;
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SetTitleVisible(bool visible) {
    titleConfig.visible = visible;
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SetTitleConfig(const RequirementTitleConfig& config) {
    titleConfig = config;
    RequestRedraw();
}

// =============================================================================
// STYLING
// =============================================================================

void UltraCanvasRequirementDiagram::SetPalette(RequirementPaletteKind kind) {
    paletteKind = kind;
    palette = RequirementPalette::BuiltIn(kind);
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SetCustomPalette(const RequirementPalette& p) {
    palette = p;
    paletteKind = RequirementPaletteKind::Custom;
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SetColorSource(RequirementColorSource source) {
    colorSource = source;
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SetStatusColor(const std::string& status,
                                                    const Color& color) {
    statusColors[status] = color;
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SetStyle(const RequirementDiagramStyle& s) {
    style = s;
    InvalidateMeasurement();
}

void UltraCanvasRequirementDiagram::SetFontFamily(const std::string& fontFamily) {
    style.fontFamily = fontFamily;
    InvalidateMeasurement();
}

void UltraCanvasRequirementDiagram::SetFontSize(double propertyFontSize, double nameFontSize) {
    style.baseFontSize = propertyFontSize;
    style.nameFontSize = nameFontSize;
    style.stereotypeFontSize = std::max(7.0, propertyFontSize - 1.0);
    InvalidateMeasurement();
}

void UltraCanvasRequirementDiagram::SetNodeWidthRange(double minWidth, double maxWidth) {
    style.nodeMinWidth = minWidth;
    style.nodeMaxWidth = std::max(minWidth, maxWidth);
    InvalidateMeasurement();
}

void UltraCanvasRequirementDiagram::SetBackgroundColor(const Color& color) {
    palette.backgroundColor = color;
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SetGridVisible(bool visible, double spacing) {
    style.showGrid = visible;
    style.gridSpacing = spacing;
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SetRiskStripeVisible(bool visible) {
    showRiskStripe = visible;
    RequestRedraw();
}

// =============================================================================
// COLOUR RESOLUTION
// =============================================================================

Color UltraCanvasRequirementDiagram::ResolveFillColor(const RequirementNode& node) const {
    if (node.hasFillColor) return node.fillColor;

    switch (colorSource) {
        case RequirementColorSource::Explicit:
            return palette.FillForKind(node.kind);

        case RequirementColorSource::ByCategory: {
            if (!node.category.empty()) {
                if (const auto* cat = GetCategory(node.category)) return cat->fillColor;
                // Unregistered category: derive a stable colour from its
                // position in the diagram's category order.
                auto it = std::find(categoryOrder.begin(), categoryOrder.end(), node.category);
                if (it != categoryOrder.end() && !palette.categoryColors.empty()) {
                    const size_t index = static_cast<size_t>(it - categoryOrder.begin());
                    return palette.categoryColors[index % palette.categoryColors.size()];
                }
            }
            return palette.FillForKind(node.kind);
        }
        case RequirementColorSource::ByRisk:
            if (node.risk != RequirementRisk::Unspecified) return palette.ColorForRisk(node.risk);
            return palette.FillForKind(node.kind);

        case RequirementColorSource::ByStatus: {
            auto it = statusColors.find(node.status);
            if (it != statusColors.end()) return it->second;
            return palette.FillForKind(node.kind);
        }
        case RequirementColorSource::ByKind:
            break;
    }
    return palette.FillForKind(node.kind);
}

Color UltraCanvasRequirementDiagram::ResolveBorderColor(const RequirementNode& node) const {
    if (node.hasBorderColor) return node.borderColor;
    if (colorSource == RequirementColorSource::ByCategory && !node.category.empty()) {
        if (const auto* cat = GetCategory(node.category)) return cat->borderColor;
    }
    return palette.BorderForKind(node.kind);
}

Color UltraCanvasRequirementDiagram::ResolveTextColor(const RequirementNode& node) const {
    if (node.hasTextColor) return node.textColor;
    if (colorSource == RequirementColorSource::ByCategory && !node.category.empty()) {
        if (const auto* cat = GetCategory(node.category)) {
            if (cat->hasTextColor) return cat->textColor;
        }
    }
    return palette.headerTextColor;
}

RequirementNodeShape UltraCanvasRequirementDiagram::ResolveShape(
        const RequirementNode& node) const {
    if (node.shape != RequirementNodeShape::Auto) return node.shape;
    switch (node.kind) {
        case RequirementNodeKind::UseCase: return RequirementNodeShape::Oval;
        case RequirementNodeKind::TestCase:
        case RequirementNodeKind::Actor:   return RequirementNodeShape::RoundedRectangle;
        default: break;
    }
    return RequirementNodeShape::Rectangle;
}

// =============================================================================
// MEASUREMENT
// =============================================================================

void UltraCanvasRequirementDiagram::InvalidateMeasurement() {
    measurementDirty = true;
    if (layoutMode != RequirementLayoutMode::Manual) layoutDirty = true;
    RequestRedraw();
}

std::vector<std::string> UltraCanvasRequirementDiagram::WrapText(
        IRenderContext* ctx, const std::string& text, double maxWidth, int maxLines) const {
    std::vector<std::string> lines;
    if (text.empty() || maxWidth <= 0.0) return lines;

    // Split on spaces; existing newlines start a new line.
    std::vector<std::string> words;
    std::string current;
    for (char c : text) {
        if (c == '\n') {
            words.push_back(current);
            words.push_back("\n");
            current.clear();
        } else if (c == ' ') {
            if (!current.empty()) words.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) words.push_back(current);

    std::string line;
    for (const auto& word : words) {
        if (word == "\n") {
            lines.push_back(line);
            line.clear();
            continue;
        }
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (static_cast<double>(ctx->GetTextLineWidth(candidate)) <= maxWidth || line.empty()) {
            line = candidate;
        } else {
            lines.push_back(line);
            line = word;
            if (maxLines > 0 && static_cast<int>(lines.size()) >= maxLines) break;
        }
    }
    if (!line.empty() && (maxLines <= 0 || static_cast<int>(lines.size()) < maxLines)) {
        lines.push_back(line);
    }

    // Q15: mark the truncation rather than silently dropping text.
    if (maxLines > 0 && static_cast<int>(lines.size()) >= maxLines) {
        const bool truncated = ctx->GetTextLineWidth(text) >
                               maxWidth * static_cast<double>(maxLines);
        if (truncated && !lines.empty()) {
            std::string& last = lines.back();
            while (!last.empty() &&
                   static_cast<double>(ctx->GetTextLineWidth(last + kEllipsis)) > maxWidth) {
                last.pop_back();
            }
            last += kEllipsis;
        }
        if (static_cast<int>(lines.size()) > maxLines) lines.resize(static_cast<size_t>(maxLines));
    }
    return lines;
}

void UltraCanvasRequirementDiagram::MeasureNode(IRenderContext* ctx, RequirementNode& node,
                                                 MeasuredNode& out) {
    out = MeasuredNode();

    const double nameSize = node.fontSize > 0.0 ? node.fontSize : style.nameFontSize;
    const double propSize = node.fontSize > 0.0 ? std::max(8.0, node.fontSize - 2.0)
                                                 : style.baseFontSize;
    const Color headerColor = ResolveTextColor(node);

    const std::string stereotype = node.stereotype.empty()
                                       ? RequirementDefaultStereotype(node.kind)
                                       : node.stereotype;

    // ---- Determine the content width -------------------------------------
    // Widest of: the guillemet line, the name, and the longest single word of
    // any property row - clamped to [nodeMinWidth, nodeMaxWidth]. Rows then
    // wrap into that width.
    double contentWidth = style.nodeMinWidth - style.nodePadding * 2.0;

    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(style.stereotypeFontSize);
    const std::string stereotypeLine = "«" + stereotype + "»";
    contentWidth = std::max(contentWidth,
                            static_cast<double>(ctx->GetTextLineWidth(stereotypeLine)));

    ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
    ctx->SetFontSize(nameSize);
    contentWidth = std::max(contentWidth, static_cast<double>(ctx->GetTextLineWidth(node.name)));

    const std::vector<RequirementPropertyRow> rows = RowsForNode(node);
    const RequirementPropertyFormat format = TemplateForNode(node).format;

    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(propSize);
    for (const auto& row : rows) {
        const std::string value = ResolveField(node, row.field, row.customKey, row.literal);
        const std::string full = FormatPropertyRow(row.key, value, format);
        // A row shorter than the max width should not be wrapped at all.
        contentWidth = std::max(contentWidth,
                                std::min(static_cast<double>(ctx->GetTextLineWidth(full)),
                                         style.nodeMaxWidth - style.nodePadding * 2.0));
    }
    contentWidth = std::min(contentWidth, style.nodeMaxWidth - style.nodePadding * 2.0);
    contentWidth = std::max(contentWidth, 40.0);

    if (node.hasExplicitWidth && node.width > 0.0) {
        contentWidth = std::max(20.0, node.width - style.nodePadding * 2.0);
    }

    // ---- Header ----------------------------------------------------------
    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(style.stereotypeFontSize);
    const double stereotypeHeight = ctx->GetTextLineHeight(stereotypeLine);
    {
        MeasuredLine line;
        line.text = stereotypeLine;
        line.fontSize = style.stereotypeFontSize;
        line.centered = true;
        line.color = palette.stereotypeTextColor;
        out.headerLines.push_back(line);
    }
    out.headerHeight = stereotypeHeight;

    if (!node.name.empty()) {
        ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
        ctx->SetFontSize(nameSize);
        for (const auto& text : WrapText(ctx, node.name, contentWidth, 3)) {
            MeasuredLine line;
            line.text = text;
            line.fontSize = nameSize;
            line.bold = true;
            line.centered = true;
            line.color = headerColor;
            out.headerLines.push_back(line);
            out.headerHeight += ctx->GetTextLineHeight(text) + style.rowSpacing;
        }
    }

    // ---- Body ------------------------------------------------------------
    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(propSize);
    for (const auto& row : rows) {
        const std::string value = ResolveField(node, row.field, row.customKey, row.literal);
        const std::string full = FormatPropertyRow(row.key, value, format);
        const std::vector<std::string> wrapped =
            WrapText(ctx, full, contentWidth, style.maxTextLines);
        for (const auto& text : wrapped) {
            MeasuredLine line;
            line.text = text;
            line.fontSize = propSize;
            line.bold = row.bold;
            line.centered = false;
            line.color = row.hasColor ? row.textColor : palette.propertyTextColor;
            out.bodyLines.push_back(line);
            out.bodyHeight += ctx->GetTextLineHeight(text) + style.rowSpacing;
        }
    }
    out.hasDivider = !out.bodyLines.empty();

    // ---- Final box size --------------------------------------------------
    if (!node.hasExplicitWidth) {
        node.width = contentWidth + style.nodePadding * 2.0;
    }
    double height = style.nodePadding * 2.0 + out.headerHeight;
    if (out.hasDivider) {
        height += style.headerGap * 2.0 + out.bodyHeight;
    }
    node.height = std::max(MIN_NODE_HEIGHT, height);
}

void UltraCanvasRequirementDiagram::MeasureCallout(IRenderContext* ctx,
                                                    RequirementCallout& callout,
                                                    MeasuredNode& out) {
    out = MeasuredNode();
    const RequirementNode* target = GetNode(callout.targetNodeId);
    if (!target) return;

    const double contentWidth = std::max(60.0, callout.width - style.nodePadding * 2.0);

    // Header: the target's stereotype + name, unless overridden.
    ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
    ctx->SetFontSize(style.nameFontSize - 1.0);
    const std::string header =
        callout.headerText.empty() ? target->name : callout.headerText;
    for (const auto& text : WrapText(ctx, header, contentWidth, 2)) {
        MeasuredLine line;
        line.text = text;
        line.fontSize = style.nameFontSize - 1.0;
        line.bold = true;
        line.centered = true;
        line.color = palette.headerTextColor;
        out.headerLines.push_back(line);
        out.headerHeight += ctx->GetTextLineHeight(text) + style.rowSpacing;
    }

    // Body: the requested fields, defaulting to id + text.
    std::vector<RequirementField> fields = callout.fields;
    if (fields.empty()) {
        fields = {RequirementField::Id, RequirementField::Text};
    }

    static const std::map<RequirementField, const char*> kFieldKeys = {
        {RequirementField::Id, "id"},
        {RequirementField::Name, "name"},
        {RequirementField::Stereotype, "stereotype"},
        {RequirementField::Text, "text"},
        {RequirementField::Source, "source"},
        {RequirementField::Risk, "risk"},
        {RequirementField::VerifyMethod, "verifyMethod"},
        {RequirementField::Status, "status"},
        {RequirementField::Owner, "owner"},
        {RequirementField::Priority, "priority"},
        {RequirementField::DocRef, "docRef"}
    };

    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(style.baseFontSize);
    for (RequirementField field : fields) {
        const std::string value = ResolveField(*target, field, "", "");
        if (value.empty()) continue;
        auto keyIt = kFieldKeys.find(field);
        const std::string key = keyIt == kFieldKeys.end() ? "value" : keyIt->second;
        const std::string full = FormatPropertyRow(key, value, nodeTemplate.format);
        for (const auto& text : WrapText(ctx, full, contentWidth, style.maxTextLines)) {
            MeasuredLine line;
            line.text = text;
            line.fontSize = style.baseFontSize;
            line.color = palette.propertyTextColor;
            out.bodyLines.push_back(line);
            out.bodyHeight += ctx->GetTextLineHeight(text) + style.rowSpacing;
        }
    }
    out.hasDivider = !out.bodyLines.empty() && !out.headerLines.empty();

    double height = style.nodePadding * 2.0 + out.headerHeight + out.bodyHeight;
    if (out.hasDivider) height += style.headerGap * 2.0;
    callout.height = std::max(MIN_NODE_HEIGHT, height);
}

void UltraCanvasRequirementDiagram::MeasureAllNodes(IRenderContext* ctx) {
    measuredNodes.clear();
    for (const auto& id : nodeOrder) {
        auto it = nodes.find(id);
        if (it == nodes.end()) continue;
        MeasuredNode measured;
        MeasureNode(ctx, it->second, measured);
        measuredNodes[id] = std::move(measured);
    }
    measuredCallouts.clear();
    for (auto& callout : callouts) {
        MeasuredNode measured;
        MeasureCallout(ctx, callout, measured);
        measuredCallouts[callout.id] = std::move(measured);
    }
    measurementDirty = false;
}

void UltraCanvasRequirementDiagram::EstimateNodeSize(RequirementNode& node) const {
    // Heuristic used only before the first Render(), so RunLayout() called
    // during construction still produces usable positions. Arial averages
    // ~0.55 of the font size per glyph; bold ~0.62.
    if (node.hasExplicitWidth && node.width > 0.0) {
        // keep the caller's width
    } else {
        double width = static_cast<double>(node.name.size()) * style.nameFontSize * 0.62;
        const std::string stereotype = node.stereotype.empty()
                                           ? RequirementDefaultStereotype(node.kind)
                                           : node.stereotype;
        width = std::max(width, static_cast<double>(stereotype.size() + 2) *
                                    style.stereotypeFontSize * 0.55);
        node.width = std::min(style.nodeMaxWidth,
                              std::max(style.nodeMinWidth, width + style.nodePadding * 2.0));
    }

    const double contentWidth = std::max(20.0, node.width - style.nodePadding * 2.0);
    double height = style.nodePadding * 2.0 +
                    style.stereotypeFontSize * 1.4 + style.nameFontSize * 1.4;

    const std::vector<RequirementPropertyRow> rows = RowsForNode(node);
    if (!rows.empty()) {
        const RequirementPropertyFormat format = TemplateForNode(node).format;
        double bodyHeight = 0.0;
        for (const auto& row : rows) {
            const std::string value = ResolveField(node, row.field, row.customKey, row.literal);
            const std::string full = FormatPropertyRow(row.key, value, format);
            const double approxWidth = static_cast<double>(full.size()) *
                                       style.baseFontSize * 0.55;
            int lines = static_cast<int>(std::ceil(approxWidth / std::max(1.0, contentWidth)));
            lines = std::max(1, std::min(lines, style.maxTextLines));
            bodyHeight += static_cast<double>(lines) * (style.baseFontSize * 1.35 + style.rowSpacing);
        }
        height += style.headerGap * 2.0 + bodyHeight;
    }
    node.height = std::max(MIN_NODE_HEIGHT, height);
}

// =============================================================================
// LAYOUT
// =============================================================================

void UltraCanvasRequirementDiagram::SetLayoutMode(RequirementLayoutMode mode) {
    layoutMode = mode;
    if (mode != RequirementLayoutMode::Manual) layoutDirty = true;
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SetLayoutOrientation(RequirementOrientation o) {
    orientation = o;
    if (layoutMode != RequirementLayoutMode::Manual) layoutDirty = true;
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SetLevelGap(double gap) {
    style.levelGap = gap;
    if (layoutMode != RequirementLayoutMode::Manual) layoutDirty = true;
}

void UltraCanvasRequirementDiagram::SetSiblingGap(double gap) {
    style.siblingGap = gap;
    if (layoutMode != RequirementLayoutMode::Manual) layoutDirty = true;
}

void UltraCanvasRequirementDiagram::SetSubtreeGap(double gap) {
    style.subtreeGap = gap;
    if (layoutMode != RequirementLayoutMode::Manual) layoutDirty = true;
}

void UltraCanvasRequirementDiagram::SetDefaultRouting(RequirementRouting routing) {
    defaultRouting = routing;
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::RunLayout() {
    layoutDirty = true;
    // Give callers usable coordinates immediately, before the first frame has
    // had a chance to measure text properly. The layout is repeated with real
    // metrics at the start of the next Render().
    if (measurementDirty) {
        for (auto& [id, node] : nodes) {
            (void)id;
            EstimateNodeSize(node);
        }
    }
    if (layoutMode == RequirementLayoutMode::ContainmentTree) {
        ApplyContainmentTreeLayout();
    }
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::ComputeLevelDepths(const std::string& nodeId, int depth,
                                                        std::map<int, double>& levelSize,
                                                        std::set<std::string>& visited) {
    if (!visited.insert(nodeId).second) return;
    const auto* node = GetNode(nodeId);
    if (!node) return;

    // Depth-axis extent of a level = the tallest (or widest) box on it.
    const bool vertical = (orientation == RequirementOrientation::TopDown ||
                           orientation == RequirementOrientation::BottomUp);
    const double extent = vertical ? node->height : node->width;
    auto it = levelSize.find(depth);
    if (it == levelSize.end() || it->second < extent) levelSize[depth] = extent;

    for (const auto& childId : GetChildIds(nodeId)) {
        ComputeLevelDepths(childId, depth + 1, levelSize, visited);
    }
}

double UltraCanvasRequirementDiagram::LayoutSubtree(const std::string& nodeId, int depth,
                                                     double crossOffset,
                                                     std::map<int, double>& levelOrigin,
                                                     std::set<std::string>& visited) {
    // Tidy tree over variable-size boxes: each subtree occupies a contiguous
    // band on the sibling axis, the parent is centred over its children, and a
    // leaf occupies exactly its own size. Returns the band width consumed.
    //
    // Positions are computed in a canonical TopDown frame (sibling axis = x,
    // depth axis = y) and rotated afterwards by OrientPositions().
    auto* node = GetNode(nodeId);
    if (!node) return 0.0;
    if (!visited.insert(nodeId).second) return 0.0;   // cycle guard

    const bool vertical = (orientation == RequirementOrientation::TopDown ||
                           orientation == RequirementOrientation::BottomUp);
    const double ownSize = vertical ? node->width : node->height;
    const double depthPos = levelOrigin[depth];

    const std::vector<std::string> children = GetChildIds(nodeId);
    if (children.empty()) {
        if (vertical) {
            node->x = crossOffset;
            node->y = depthPos;
        } else {
            node->y = crossOffset;
            node->x = depthPos;
        }
        return ownSize;
    }

    double cursor = crossOffset;
    double firstCenter = 0.0;
    double lastCenter = 0.0;
    bool haveFirst = false;

    for (size_t i = 0; i < children.size(); ++i) {
        const double consumed = LayoutSubtree(children[i], depth + 1, cursor,
                                              levelOrigin, visited);
        if (consumed <= 0.0) continue;

        const auto* child = GetNode(children[i]);
        if (child) {
            const double childCenter = vertical ? child->x + child->width / 2.0
                                                : child->y + child->height / 2.0;
            if (!haveFirst) {
                firstCenter = childCenter;
                haveFirst = true;
            }
            lastCenter = childCenter;
        }
        cursor += consumed;
        if (i + 1 < children.size()) cursor += style.siblingGap;
    }

    const double bandWidth = std::max(cursor - crossOffset, ownSize);

    // Centre the parent over the span of its children.
    double parentCross;
    if (haveFirst) {
        parentCross = (firstCenter + lastCenter) / 2.0 - ownSize / 2.0;
    } else {
        parentCross = crossOffset + (bandWidth - ownSize) / 2.0;
    }

    if (vertical) {
        node->x = parentCross;
        node->y = depthPos;
    } else {
        node->y = parentCross;
        node->x = depthPos;
    }
    return bandWidth;
}

void UltraCanvasRequirementDiagram::ApplyContainmentTreeLayout() {
    if (nodes.empty()) return;

    if (HasContainmentCycle()) {
        RequirementWarning w;
        w.kind = RequirementWarning::Kind::ContainmentCycle;
        w.message = "Containment cycle detected - falling back to grid layout";
        ReportWarning(w);
        ApplyGridFallbackLayout();
        layoutDirty = false;
        return;
    }

    const bool vertical = (orientation == RequirementOrientation::TopDown ||
                           orientation == RequirementOrientation::BottomUp);

    // Depth-axis origin of every level, from the largest box on each level.
    std::map<int, double> levelSize;
    {
        std::set<std::string> visited;
        for (const auto& rootId : GetRootIds()) {
            ComputeLevelDepths(rootId, 0, levelSize, visited);
        }
        // Nodes unreachable from a root (only possible with dangling data)
        // still need a level entry.
        for (const auto& id : nodeOrder) {
            if (visited.find(id) == visited.end()) {
                ComputeLevelDepths(id, 0, levelSize, visited);
            }
        }
    }

    std::map<int, double> levelOrigin;
    double depthCursor = 0.0;
    for (const auto& [depth, size] : levelSize) {
        levelOrigin[depth] = depthCursor;
        depthCursor += size + style.levelGap;
    }

    // Lay out every root tree left to right (top to bottom for horizontal
    // orientations), separated by subtreeGap.
    std::set<std::string> visited;
    double crossCursor = 0.0;
    for (const auto& rootId : GetRootIds()) {
        const double consumed = LayoutSubtree(rootId, 0, crossCursor, levelOrigin, visited);
        if (consumed > 0.0) crossCursor += consumed + style.subtreeGap;
    }
    // Anything still unplaced (orphaned by a dangling relation) goes after.
    for (const auto& id : nodeOrder) {
        if (visited.find(id) != visited.end()) continue;
        auto* node = GetNode(id);
        if (!node) continue;
        visited.insert(id);
        if (vertical) {
            node->x = crossCursor;
            node->y = levelOrigin.empty() ? 0.0 : levelOrigin.begin()->second;
            crossCursor += node->width + style.subtreeGap;
        } else {
            node->y = crossCursor;
            node->x = levelOrigin.empty() ? 0.0 : levelOrigin.begin()->second;
            crossCursor += node->height + style.subtreeGap;
        }
    }

    OrientPositions();
    layoutDirty = false;
}

void UltraCanvasRequirementDiagram::OrientPositions() {
    // LayoutSubtree lays the tree out with the depth axis growing positive
    // (downward for TopDown, rightward for LeftRight). BottomUp and RightLeft
    // are the same layout mirrored about the depth axis.
    if (orientation == RequirementOrientation::TopDown ||
        orientation == RequirementOrientation::LeftRight) {
        return;
    }

    const bool vertical = (orientation == RequirementOrientation::BottomUp);
    double maxDepth = 0.0;
    for (const auto& [id, node] : nodes) {
        (void)id;
        maxDepth = std::max(maxDepth, vertical ? node.y + node.height : node.x + node.width);
    }
    for (auto& [id, node] : nodes) {
        (void)id;
        if (vertical) {
            node.y = maxDepth - (node.y + node.height);
        } else {
            node.x = maxDepth - (node.x + node.width);
        }
    }
}

void UltraCanvasRequirementDiagram::ApplyGridFallbackLayout() {
    const int columns = std::max(1, static_cast<int>(std::ceil(std::sqrt(
        static_cast<double>(nodes.size())))));
    double x = 0.0;
    double y = 0.0;
    double rowHeight = 0.0;
    int column = 0;

    for (const auto& id : nodeOrder) {
        auto* node = GetNode(id);
        if (!node) continue;
        node->x = x;
        node->y = y;
        rowHeight = std::max(rowHeight, node->height);
        x += node->width + style.siblingGap;
        if (++column >= columns) {
            column = 0;
            x = 0.0;
            y += rowHeight + style.levelGap;
            rowHeight = 0.0;
        }
    }
}

Rect2Dd UltraCanvasRequirementDiagram::GetContentBounds() const {
    bool any = false;
    double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;

    auto include = [&](double x, double y, double w, double h) {
        if (!any) {
            minX = x; minY = y; maxX = x + w; maxY = y + h;
            any = true;
        } else {
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x + w);
            maxY = std::max(maxY, y + h);
        }
    };

    for (const auto& [id, node] : nodes) {
        (void)id;
        include(node.x, node.y, node.width, node.height);
    }
    for (const auto& callout : callouts) {
        include(callout.x, callout.y, callout.width, callout.height);
    }
    if (!any) return Rect2Dd(0.0, 0.0, 0.0, 0.0);
    return Rect2Dd(minX, minY, maxX - minX, maxY - minY);
}

// =============================================================================
// VIEWPORT
// =============================================================================

void UltraCanvasRequirementDiagram::ClampZoom() {
    zoomLevel = std::max(minZoom, std::min(maxZoom, zoomLevel));
}

void UltraCanvasRequirementDiagram::SetZoomLevel(double zoom) {
    zoomLevel = zoom;
    ClampZoom();
    NotifyViewportChange();
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SetPanOffset(double x, double y) {
    panOffset.x = x;
    panOffset.y = y;
    NotifyViewportChange();
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::ZoomIn(double factor) {
    // Zoom about the centre of the element so the view does not drift.
    const double cx = GetWidth() / 2.0;
    const double cy = GetHeight() / 2.0;
    const double oldZoom = zoomLevel;
    zoomLevel *= factor;
    ClampZoom();
    panOffset.x = cx - (cx - panOffset.x) * (zoomLevel / oldZoom);
    panOffset.y = cy - (cy - panOffset.y) * (zoomLevel / oldZoom);
    NotifyViewportChange();
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::ZoomOut(double factor) {
    ZoomIn(1.0 / factor);
}

void UltraCanvasRequirementDiagram::FitView(double padding) {
    if (measurementDirty) {
        // Node sizes are still heuristic - fitting now would use the wrong
        // bounds. Repeat the fit once the next frame has measured the text.
        fitPending = true;
    }
    const Rect2Dd bounds = GetContentBounds();
    if (bounds.width <= 0.0 || bounds.height <= 0.0) return;

    const double availableW = std::max(1.0, GetWidth() - padding * 2.0);
    const double availableH = std::max(1.0,
        GetHeight() - padding * 2.0 - (titleConfig.visible ? titleConfig.height : 0.0));

    zoomLevel = std::min(availableW / bounds.width, availableH / bounds.height);
    ClampZoom();

    const double topInset = titleConfig.visible ? titleConfig.height : 0.0;
    panOffset.x = padding + (availableW - bounds.width * zoomLevel) / 2.0 -
                  bounds.x * zoomLevel;
    panOffset.y = topInset + padding + (availableH - bounds.height * zoomLevel) / 2.0 -
                  bounds.y * zoomLevel;

    NotifyViewportChange();
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::CenterOn(double worldX, double worldY) {
    panOffset.x = GetWidth() / 2.0 - worldX * zoomLevel;
    panOffset.y = GetHeight() / 2.0 - worldY * zoomLevel;
    NotifyViewportChange();
    RequestRedraw();
}

Point2Dd UltraCanvasRequirementDiagram::ScreenToWorld(const Point2Di& screenPos) const {
    return Point2Dd((screenPos.x - panOffset.x) / zoomLevel,
                    (screenPos.y - panOffset.y) / zoomLevel);
}

Point2Di UltraCanvasRequirementDiagram::WorldToScreen(const Point2Dd& worldPos) const {
    return Point2Di(static_cast<int>(worldPos.x * zoomLevel + panOffset.x),
                    static_cast<int>(worldPos.y * zoomLevel + panOffset.y));
}

Point2Dd UltraCanvasRequirementDiagram::SnapPoint(const Point2Dd& p) const {
    if (!snapGrid.enabled) return p;
    return Point2Dd(std::round(p.x / snapGrid.snapX) * snapGrid.snapX,
                    std::round(p.y / snapGrid.snapY) * snapGrid.snapY);
}

Rect2Dd UltraCanvasRequirementDiagram::NodeRect(const RequirementNode& node) const {
    return Rect2Dd(node.x, node.y, node.width, node.height);
}

// =============================================================================
// SELECTION
// =============================================================================

void UltraCanvasRequirementDiagram::SelectNode(const std::string& id, bool addToSelection) {
    if (!addToSelection) {
        selectedNodes.clear();
        selectedRelations.clear();
    }
    if (nodes.find(id) != nodes.end()) selectedNodes.insert(id);
    NotifySelectionChange();
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SelectRelation(const std::string& id, bool addToSelection) {
    if (!addToSelection) {
        selectedNodes.clear();
        selectedRelations.clear();
    }
    if (GetRelation(id)) selectedRelations.insert(id);
    NotifySelectionChange();
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SelectAll() {
    for (const auto& id : nodeOrder) selectedNodes.insert(id);
    for (const auto& r : relations) selectedRelations.insert(r.id);
    NotifySelectionChange();
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::DeselectAll() {
    selectedNodes.clear();
    selectedRelations.clear();
    NotifySelectionChange();
    RequestRedraw();
}

std::vector<std::string> UltraCanvasRequirementDiagram::GetSelectedNodeIds() const {
    return std::vector<std::string>(selectedNodes.begin(), selectedNodes.end());
}

std::vector<std::string> UltraCanvasRequirementDiagram::GetSelectedRelationIds() const {
    return std::vector<std::string>(selectedRelations.begin(), selectedRelations.end());
}

bool UltraCanvasRequirementDiagram::IsNodeSelected(const std::string& id) const {
    return selectedNodes.find(id) != selectedNodes.end();
}

bool UltraCanvasRequirementDiagram::IsRelationSelected(const std::string& id) const {
    return selectedRelations.find(id) != selectedRelations.end();
}

void UltraCanvasRequirementDiagram::SetSnapToGrid(bool enabled) {
    snapGrid.enabled = enabled;
}

void UltraCanvasRequirementDiagram::SetSnapGrid(double snapX, double snapY) {
    snapGrid.snapX = snapX;
    snapGrid.snapY = snapY;
}

void UltraCanvasRequirementDiagram::NotifySelectionChange() {
    for (auto& [id, node] : nodes) {
        node.isSelected = selectedNodes.find(id) != selectedNodes.end();
    }
    for (auto& r : relations) {
        r.isSelected = selectedRelations.find(r.id) != selectedRelations.end();
    }
    if (onSelectionChange) {
        onSelectionChange(GetSelectedNodeIds(), GetSelectedRelationIds());
    }
}

void UltraCanvasRequirementDiagram::NotifyViewportChange() {
    if (onViewportChange) onViewportChange(zoomLevel, panOffset.x, panOffset.y);
}

void UltraCanvasRequirementDiagram::ReportWarning(const RequirementWarning& warning) const {
    if (onValidationWarning) onValidationWarning(warning);
}

// =============================================================================
// GEOMETRY - CONNECTOR ANCHORS
// =============================================================================

UltraCanvasRequirementDiagram::NodeFace
UltraCanvasRequirementDiagram::ChooseFaceTowards(const RequirementNode& from,
                                                  const RequirementNode& to) const {
    // Prefer the axis on which the two boxes are actually separated. On a tree
    // the levels never overlap vertically, so a child two columns away still
    // leaves through its top face and the connector runs straight up instead
    // of looping around the side. Only when the boxes overlap on both axes
    // does the dominant-centre-distance rule decide.
    if (to.y >= from.y + from.height) return NodeFace::Bottom;
    if (to.y + to.height <= from.y)   return NodeFace::Top;
    if (to.x >= from.x + from.width)  return NodeFace::Right;
    if (to.x + to.width <= from.x)    return NodeFace::Left;

    const double dx = (to.x + to.width / 2.0) - (from.x + from.width / 2.0);
    const double dy = (to.y + to.height / 2.0) - (from.y + from.height / 2.0);
    if (std::abs(dx) > std::abs(dy)) {
        return dx >= 0.0 ? NodeFace::Right : NodeFace::Left;
    }
    return dy >= 0.0 ? NodeFace::Bottom : NodeFace::Top;
}

void UltraCanvasRequirementDiagram::CountFaceUsage(const std::string& nodeId, NodeFace face,
                                                    const std::string& relationId,
                                                    int& totalCount, int& myIndex) const {
    totalCount = 0;
    myIndex = 0;
    const auto* node = GetNode(nodeId);
    if (!node) return;

    // Storage order in `relations` keeps the slot index stable across frames.
    for (const auto& r : relations) {
        // Containment is drawn as a shared bus and never claims a face slot;
        // neither does a relation that is not drawn at all.
        if (r.kind == RequirementRelationKind::Containment || !r.visible) continue;

        const std::string otherId = (r.sourceId == nodeId) ? r.targetId
                                  : (r.targetId == nodeId) ? r.sourceId
                                                            : std::string();
        if (otherId.empty()) continue;
        const auto* other = GetNode(otherId);
        if (!other) continue;

        const NodeFace usedFace = ChooseFaceTowards(*node, *other);
        if (usedFace != face) continue;
        if (r.id == relationId) myIndex = totalCount;
        totalCount++;
    }
    if (totalCount == 0) totalCount = 1;
}

Point2Dd UltraCanvasRequirementDiagram::AnchorPoint(const RequirementNode& node, NodeFace face,
                                                     int slotIndex, int slotCount) const {
    const double fraction = (static_cast<double>(slotIndex) + 1.0) /
                            (static_cast<double>(std::max(1, slotCount)) + 1.0);
    switch (face) {
        case NodeFace::Top:    return Point2Dd(node.x + node.width * fraction, node.y);
        case NodeFace::Bottom: return Point2Dd(node.x + node.width * fraction,
                                               node.y + node.height);
        case NodeFace::Left:   return Point2Dd(node.x, node.y + node.height * fraction);
        case NodeFace::Right:  return Point2Dd(node.x + node.width,
                                               node.y + node.height * fraction);
    }
    return Point2Dd(node.x + node.width / 2.0, node.y + node.height / 2.0);
}

void UltraCanvasRequirementDiagram::BuildOrthogonalPath(const Point2Dd& from, NodeFace fromFace,
                                                         const Point2Dd& to, NodeFace toFace,
                                                         std::vector<Point2Dd>& outPath) const {
    // Leave the source perpendicular to its face, approach the target
    // perpendicular to its face, and join the two stubs with a Z.
    const double stub = 14.0;
    Point2Dd a = from;
    Point2Dd b = to;

    switch (fromFace) {
        case NodeFace::Top:    a.y -= stub; break;
        case NodeFace::Bottom: a.y += stub; break;
        case NodeFace::Left:   a.x -= stub; break;
        case NodeFace::Right:  a.x += stub; break;
    }
    switch (toFace) {
        case NodeFace::Top:    b.y -= stub; break;
        case NodeFace::Bottom: b.y += stub; break;
        case NodeFace::Left:   b.x -= stub; break;
        case NodeFace::Right:  b.x += stub; break;
    }

    outPath.push_back(from);
    outPath.push_back(a);

    const bool fromVertical = (fromFace == NodeFace::Top || fromFace == NodeFace::Bottom);
    if (fromVertical) {
        const double midY = (a.y + b.y) / 2.0;
        outPath.push_back(Point2Dd(a.x, midY));
        outPath.push_back(Point2Dd(b.x, midY));
    } else {
        const double midX = (a.x + b.x) / 2.0;
        outPath.push_back(Point2Dd(midX, a.y));
        outPath.push_back(Point2Dd(midX, b.y));
    }

    outPath.push_back(b);
    outPath.push_back(to);
}

void UltraCanvasRequirementDiagram::BuildRelationPath(const RequirementRelation& relation,
                                                       std::vector<Point2Dd>& outPath) const {
    outPath.clear();
    const auto* source = GetNode(relation.sourceId);
    const auto* target = GetNode(relation.targetId);
    if (!source || !target) return;

    const NodeFace sourceFace = ChooseFaceTowards(*source, *target);
    const NodeFace targetFace = ChooseFaceTowards(*target, *source);

    int sourceSlots = 1, sourceIndex = 0;
    int targetSlots = 1, targetIndex = 0;
    CountFaceUsage(relation.sourceId, sourceFace, relation.id, sourceSlots, sourceIndex);
    CountFaceUsage(relation.targetId, targetFace, relation.id, targetSlots, targetIndex);

    const Point2Dd from = AnchorPoint(*source, sourceFace, sourceIndex, sourceSlots);
    const Point2Dd to = AnchorPoint(*target, targetFace, targetIndex, targetSlots);

    RequirementRouting routing =
        relation.useDefaultRouting ? defaultRouting : relation.routing;
    // Bus routing only means anything for containment, which is drawn
    // separately; anything else falls back to orthogonal.
    if (routing == RequirementRouting::Bus) routing = RequirementRouting::Orthogonal;

    switch (routing) {
        case RequirementRouting::Straight:
            outPath.push_back(from);
            outPath.push_back(to);
            break;

        case RequirementRouting::Curved: {
            // Sampled cubic bezier so hit-testing and the arrowhead can use the
            // same polyline as the stroke.
            const double dx = to.x - from.x;
            const double dy = to.y - from.y;
            const double bend = std::max(30.0, std::sqrt(dx * dx + dy * dy) * 0.35);
            Point2Dd c1 = from;
            Point2Dd c2 = to;
            switch (sourceFace) {
                case NodeFace::Top:    c1.y -= bend; break;
                case NodeFace::Bottom: c1.y += bend; break;
                case NodeFace::Left:   c1.x -= bend; break;
                case NodeFace::Right:  c1.x += bend; break;
            }
            switch (targetFace) {
                case NodeFace::Top:    c2.y -= bend; break;
                case NodeFace::Bottom: c2.y += bend; break;
                case NodeFace::Left:   c2.x -= bend; break;
                case NodeFace::Right:  c2.x += bend; break;
            }
            const int segments = 18;
            for (int i = 0; i <= segments; ++i) {
                const double t = static_cast<double>(i) / segments;
                const double u = 1.0 - t;
                const double x = u * u * u * from.x + 3 * u * u * t * c1.x +
                                 3 * u * t * t * c2.x + t * t * t * to.x;
                const double y = u * u * u * from.y + 3 * u * u * t * c1.y +
                                 3 * u * t * t * c2.y + t * t * t * to.y;
                outPath.push_back(Point2Dd(x, y));
            }
            break;
        }
        case RequirementRouting::Orthogonal:
        case RequirementRouting::Bus:
        default:
            BuildOrthogonalPath(from, sourceFace, to, targetFace, outPath);
            break;
    }
}

double UltraCanvasRequirementDiagram::DistanceToSegment(const Point2Dd& p, const Point2Dd& a,
                                                         const Point2Dd& b) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 1e-9) {
        const double px = p.x - a.x;
        const double py = p.y - a.y;
        return std::sqrt(px * px + py * py);
    }
    double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSquared;
    t = std::max(0.0, std::min(1.0, t));
    const double cx = a.x + t * dx;
    const double cy = a.y + t * dy;
    const double ex = p.x - cx;
    const double ey = p.y - cy;
    return std::sqrt(ex * ex + ey * ey);
}

// =============================================================================
// HIT TESTING
// =============================================================================

bool UltraCanvasRequirementDiagram::PointInNode(const RequirementNode& node,
                                                 const Point2Dd& worldPos) const {
    if (ResolveShape(node) == RequirementNodeShape::Oval) {
        const double rx = node.width / 2.0;
        const double ry = node.height / 2.0;
        if (rx <= 0.0 || ry <= 0.0) return false;
        const double nx = (worldPos.x - (node.x + rx)) / rx;
        const double ny = (worldPos.y - (node.y + ry)) / ry;
        return nx * nx + ny * ny <= 1.0;
    }
    return NodeRect(node).Contains(worldPos);
}

std::string UltraCanvasRequirementDiagram::FindNodeAt(const Point2Di& screenPos) const {
    const Point2Dd world = ScreenToWorld(screenPos);
    // Reverse draw order: the topmost box wins.
    for (auto it = nodeOrder.rbegin(); it != nodeOrder.rend(); ++it) {
        auto nodeIt = nodes.find(*it);
        if (nodeIt == nodes.end()) continue;
        if (PointInNode(nodeIt->second, world)) return *it;
    }
    return "";
}

std::string UltraCanvasRequirementDiagram::FindCalloutAt(const Point2Di& screenPos) const {
    const Point2Dd world = ScreenToWorld(screenPos);
    for (auto it = callouts.rbegin(); it != callouts.rend(); ++it) {
        if (Rect2Dd(it->x, it->y, it->width, it->height).Contains(world)) return it->id;
    }
    return "";
}

std::string UltraCanvasRequirementDiagram::FindRelationAt(const Point2Di& screenPos) const {
    const Point2Dd world = ScreenToWorld(screenPos);
    const double tolerance = RELATION_HIT_TOLERANCE / std::max(0.0001, zoomLevel);

    for (const auto& relation : relations) {
        if (!relation.visible) continue;   // not drawn, so not clickable
        std::vector<Point2Dd> path;
        if (relation.kind == RequirementRelationKind::Containment) {
            // The bus route: parent bottom -> spine -> child top.
            const auto* parent = GetNode(relation.sourceId);
            const auto* child = GetNode(relation.targetId);
            if (!parent || !child) continue;
            const Point2Dd parentAnchor(parent->x + parent->width / 2.0,
                                        parent->y + parent->height);
            const Point2Dd childAnchor(child->x + child->width / 2.0, child->y);
            const double spineY = (parentAnchor.y + childAnchor.y) / 2.0;
            path = {parentAnchor, Point2Dd(parentAnchor.x, spineY),
                    Point2Dd(childAnchor.x, spineY), childAnchor};
        } else {
            BuildRelationPath(relation, path);
        }
        for (size_t i = 1; i < path.size(); ++i) {
            if (DistanceToSegment(world, path[i - 1], path[i]) <= tolerance) {
                return relation.id;
            }
        }
    }
    return "";
}

// =============================================================================
// RENDERING
// =============================================================================

void UltraCanvasRequirementDiagram::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    if (!ctx || !IsVisible()) return;

    // Text metrics are only available with a live context, so the measurement
    // and any pending layout run at the start of the frame.
    if (measurementDirty) {
        MeasureAllNodes(ctx);
        if (layoutMode != RequirementLayoutMode::Manual) layoutDirty = true;
    }
    if (layoutDirty && layoutMode == RequirementLayoutMode::ContainmentTree) {
        ApplyContainmentTreeLayout();
        if (autoFitOnLayout) fitPending = true;
    }
    layoutDirty = false;
    if (fitPending) {
        fitPending = false;   // cleared first: FitView() re-arms it only while
        FitView();            // the measurement is still dirty, which it isn't
    }

    ctx->SetFillPaint(palette.backgroundColor);
    ctx->FillRectangle(GetLocalBounds());

    ctx->PushState();
    ctx->Translate(panOffset.x, panOffset.y);
    ctx->Scale(zoomLevel, zoomLevel);

    if (style.showGrid) RenderGrid(ctx);

    RenderContainmentBuses(ctx);
    RenderRelations(ctx);
    RenderNodes(ctx);
    RenderCallouts(ctx);
    if (isSelectingBox) RenderSelectionBox(ctx);

    ctx->PopState();

    // Screen-space overlays.
    if (titleConfig.visible) RenderTitle(ctx);
    if (legendConfig.visible) RenderLegend(ctx);
}

void UltraCanvasRequirementDiagram::RenderGrid(IRenderContext* ctx) {
    ctx->SetStrokePaint(palette.gridColor);
    ctx->SetStrokeWidth(1.0 / std::max(0.0001, zoomLevel));

    const double spacing = style.gridSpacing;
    if (spacing <= 0.0) return;

    const Point2Dd topLeft = ScreenToWorld(Point2Di(0, 0));
    const Point2Dd bottomRight = ScreenToWorld(
        Point2Di(static_cast<int>(GetWidth()), static_cast<int>(GetHeight())));

    const double startX = std::floor(topLeft.x / spacing) * spacing;
    const double startY = std::floor(topLeft.y / spacing) * spacing;

    for (double x = startX; x <= bottomRight.x; x += spacing) {
        ctx->DrawLine(Point2Dd(x, topLeft.y), Point2Dd(x, bottomRight.y));
    }
    for (double y = startY; y <= bottomRight.y; y += spacing) {
        ctx->DrawLine(Point2Dd(topLeft.x, y), Point2Dd(bottomRight.x, y));
    }
}

void UltraCanvasRequirementDiagram::RenderContainmentBuses(IRenderContext* ctx) {
    // Q30: all children of one parent share a single spine - one drop from the
    // parent, one horizontal run, one riser per child. This is the notation in
    // reference images 1-3 and is why containment is not drawn per relation.
    const bool vertical = (orientation == RequirementOrientation::TopDown ||
                           orientation == RequirementOrientation::BottomUp);

    // Group containment relations by parent, preserving insertion order.
    std::vector<std::string> parents;
    std::map<std::string, std::vector<const RequirementRelation*>> byParent;
    for (const auto& r : relations) {
        if (r.kind != RequirementRelationKind::Containment || !r.visible) continue;
        if (byParent.find(r.sourceId) == byParent.end()) parents.push_back(r.sourceId);
        byParent[r.sourceId].push_back(&r);
    }

    for (const auto& parentId : parents) {
        const auto* parent = GetNode(parentId);
        if (!parent) continue;

        for (bool selectedPass : {false, true}) {
            // Selected buses are stroked a second time in the selection colour
            // so a click on a containment link is visible.
            std::vector<const RequirementRelation*> group;
            for (const auto* r : byParent[parentId]) {
                if (r->isSelected == selectedPass) group.push_back(r);
            }
            if (group.empty()) continue;

            const Color color = selectedPass ? palette.selectionColor
                                             : palette.ColorForRelation(
                                                   RequirementRelationKind::Containment);
            const double width = selectedPass ? style.selectionWidth : style.relationLineWidth;

            ctx->SetLineDash(UCDashPattern::EMPTY);
            ctx->SetStrokePaint(color);
            ctx->SetStrokeWidth(width);

            // Where the bus leaves the parent, and where it enters each child.
            const bool childrenBelow = vertical;
            double spine = 0.0;
            bool haveSpine = false;
            std::vector<std::pair<Point2Dd, Point2Dd>> risers;  // (spine point, child anchor)

            for (const auto* r : group) {
                const auto* child = GetNode(r->targetId);
                if (!child) continue;

                Point2Dd parentAnchor;
                Point2Dd childAnchor;
                if (childrenBelow) {
                    const bool below = child->y >= parent->y;
                    parentAnchor = Point2Dd(parent->x + parent->width / 2.0,
                                            below ? parent->y + parent->height : parent->y);
                    childAnchor = Point2Dd(child->x + child->width / 2.0,
                                           below ? child->y : child->y + child->height);
                    if (!haveSpine) {
                        spine = (parentAnchor.y + childAnchor.y) / 2.0;
                        haveSpine = true;
                    }
                    risers.emplace_back(Point2Dd(childAnchor.x, spine), childAnchor);
                } else {
                    const bool right = child->x >= parent->x;
                    parentAnchor = Point2Dd(right ? parent->x + parent->width : parent->x,
                                            parent->y + parent->height / 2.0);
                    childAnchor = Point2Dd(right ? child->x : child->x + child->width,
                                           child->y + child->height / 2.0);
                    if (!haveSpine) {
                        spine = (parentAnchor.x + childAnchor.x) / 2.0;
                        haveSpine = true;
                    }
                    risers.emplace_back(Point2Dd(spine, childAnchor.y), childAnchor);
                }
            }
            if (!haveSpine || risers.empty()) continue;

            // Drop from the parent to the spine.
            const Point2Dd parentCenter = childrenBelow
                ? Point2Dd(parent->x + parent->width / 2.0,
                           spine >= parent->y + parent->height ? parent->y + parent->height
                                                                : parent->y)
                : Point2Dd(spine >= parent->x + parent->width ? parent->x + parent->width
                                                              : parent->x,
                           parent->y + parent->height / 2.0);

            const Point2Dd busStart = childrenBelow ? Point2Dd(parentCenter.x, spine)
                                                     : Point2Dd(spine, parentCenter.y);
            ctx->DrawLine(parentCenter, busStart);

            // Horizontal (or vertical) spine spanning all risers.
            double minCross = busStart.x;
            double maxCross = busStart.x;
            if (!childrenBelow) {
                minCross = busStart.y;
                maxCross = busStart.y;
            }
            for (const auto& [spinePoint, childAnchor] : risers) {
                (void)childAnchor;
                const double cross = childrenBelow ? spinePoint.x : spinePoint.y;
                minCross = std::min(minCross, cross);
                maxCross = std::max(maxCross, cross);
            }
            if (childrenBelow) {
                ctx->DrawLine(Point2Dd(minCross, spine), Point2Dd(maxCross, spine));
            } else {
                ctx->DrawLine(Point2Dd(spine, minCross), Point2Dd(spine, maxCross));
            }

            // Risers into each child.
            for (const auto& [spinePoint, childAnchor] : risers) {
                ctx->DrawLine(spinePoint, childAnchor);
            }

            // Q25: the crosshair ⊕ sits at the PARENT end of the containment.
            if (!selectedPass) {
                DrawCrosshair(ctx, parentCenter, color);
            }
        }
    }
}

void UltraCanvasRequirementDiagram::RenderRelations(IRenderContext* ctx) {
    for (const auto& relation : relations) {
        if (relation.kind == RequirementRelationKind::Containment) continue;
        if (!relation.visible) continue;
        RenderRelation(ctx, relation);
    }
}

void UltraCanvasRequirementDiagram::RenderRelation(IRenderContext* ctx,
                                                    const RequirementRelation& relation) {
    std::vector<Point2Dd> path;
    BuildRelationPath(relation, path);
    if (path.size() < 2) return;

    Color color = relation.hasColor ? relation.color
                                    : palette.ColorForRelation(relation.kind);
    double width = relation.lineWidth > 0.0 ? relation.lineWidth : style.relationLineWidth;
    if (relation.isSelected) {
        color = palette.selectionColor;
        width = style.selectionWidth;
    } else if (relation.isHovered) {
        color = palette.hoverColor;
    }

    // Generalisation is a solid line; every requirement dependency is dashed.
    const bool dashed = (relation.kind != RequirementRelationKind::Generalization);
    ctx->SetLineDash(dashed ? style.relationDash : UCDashPattern::EMPTY);
    ctx->SetStrokePaint(color);
    ctx->SetStrokeWidth(width);
    ctx->DrawLinePath(path, false);
    ctx->SetLineDash(UCDashPattern::EMPTY);

    // Arrowhead oriented by the last segment, not by the overall vector.
    const Point2Dd tip = path.back();
    const Point2Dd prev = path[path.size() - 2];
    double dirX = tip.x - prev.x;
    double dirY = tip.y - prev.y;
    const double length = std::sqrt(dirX * dirX + dirY * dirY);
    if (length > 1e-6) {
        dirX /= length;
        dirY /= length;
        if (relation.kind == RequirementRelationKind::Generalization) {
            DrawHollowTriangle(ctx, tip, dirX, dirY, color, palette.backgroundColor);
        } else {
            DrawOpenArrowHead(ctx, tip, dirX, dirY, color);
        }
    }

    if (relation.showLabel) RenderRelationLabel(ctx, relation, path);
}

void UltraCanvasRequirementDiagram::RenderRelationLabel(IRenderContext* ctx,
                                                         const RequirementRelation& relation,
                                                         const std::vector<Point2Dd>& path) {
    std::string label = relation.label;
    if (label.empty()) {
        const char* defaultLabel = RequirementRelationLabel(relation.kind);
        if (!defaultLabel || !*defaultLabel) return;
        label = std::string("«") + defaultLabel + "»";
    }
    if (path.size() < 2) return;

    // Anchor to the middle of the longest segment: on an orthogonal route that
    // is the long central run, which is where a reader expects the keyword.
    size_t bestIndex = 1;
    double bestLength = -1.0;
    for (size_t i = 1; i < path.size(); ++i) {
        const double dx = path[i].x - path[i - 1].x;
        const double dy = path[i].y - path[i - 1].y;
        const double length = dx * dx + dy * dy;
        if (length > bestLength) {
            bestLength = length;
            bestIndex = i;
        }
    }
    const double midX = (path[bestIndex].x + path[bestIndex - 1].x) / 2.0;
    const double midY = (path[bestIndex].y + path[bestIndex - 1].y) / 2.0;

    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(style.relationLabelFontSize);
    const Size2Di dim = ctx->GetTextLineDimensions(label);
    const double padding = 3.0;

    ctx->SetFillPaint(palette.relationLabelBackground);
    ctx->FillRoundedRectangle(
        Rect2Dd(midX - dim.width / 2.0 - padding, midY - dim.height / 2.0 - padding,
                dim.width + padding * 2.0, dim.height + padding * 2.0),
        3.0);

    ctx->SetTextPaint(relation.isSelected ? palette.selectionColor
                                           : palette.relationLabelColor);
    ctx->DrawText(label, Point2Dd(midX - dim.width / 2.0, midY - dim.height / 2.0));
}

void UltraCanvasRequirementDiagram::DrawCrosshair(IRenderContext* ctx, const Point2Dd& center,
                                                   const Color& color) {
    const double r = style.crosshairRadius;
    ctx->SetStrokePaint(color);
    ctx->SetStrokeWidth(style.relationLineWidth);
    ctx->SetFillPaint(palette.backgroundColor);
    ctx->DrawFilledCircle(center, static_cast<float>(r), palette.backgroundColor, color,
                          static_cast<float>(style.relationLineWidth));
    ctx->SetStrokePaint(color);
    ctx->SetStrokeWidth(style.relationLineWidth);
    ctx->DrawLine(Point2Dd(center.x - r, center.y), Point2Dd(center.x + r, center.y));
    ctx->DrawLine(Point2Dd(center.x, center.y - r), Point2Dd(center.x, center.y + r));
}

void UltraCanvasRequirementDiagram::DrawOpenArrowHead(IRenderContext* ctx, const Point2Dd& tip,
                                                       double dirX, double dirY,
                                                       const Color& color) {
    // SysML dependencies use an OPEN (unfilled) arrowhead: two strokes, no fill.
    const double angle = 0.42;
    const double size = style.arrowSize;
    const double cosA = std::cos(angle);
    const double sinA = std::sin(angle);
    const double backX = -dirX;
    const double backY = -dirY;

    const double ax1 = tip.x + size * (backX * cosA - backY * sinA);
    const double ay1 = tip.y + size * (backX * sinA + backY * cosA);
    const double ax2 = tip.x + size * (backX * cosA + backY * sinA);
    const double ay2 = tip.y + size * (-backX * sinA + backY * cosA);

    ctx->SetLineDash(UCDashPattern::EMPTY);
    ctx->SetStrokePaint(color);
    ctx->SetStrokeWidth(style.relationLineWidth);
    ctx->DrawLine(Point2Dd(ax1, ay1), tip);
    ctx->DrawLine(Point2Dd(ax2, ay2), tip);
}

void UltraCanvasRequirementDiagram::DrawHollowTriangle(IRenderContext* ctx, const Point2Dd& tip,
                                                        double dirX, double dirY,
                                                        const Color& color, const Color& fill) {
    const double size = style.trianglSize;
    const double halfWidth = size * 0.5;
    const double baseX = tip.x - dirX * size;
    const double baseY = tip.y - dirY * size;
    // Perpendicular of the direction vector.
    const double px = -dirY;
    const double py = dirX;

    std::vector<Point2Dd> triangle = {
        tip,
        Point2Dd(baseX + px * halfWidth, baseY + py * halfWidth),
        Point2Dd(baseX - px * halfWidth, baseY - py * halfWidth)
    };

    ctx->SetLineDash(UCDashPattern::EMPTY);
    ctx->SetFillPaint(fill);
    ctx->FillLinePath(triangle);
    ctx->SetStrokePaint(color);
    ctx->SetStrokeWidth(style.relationLineWidth);
    ctx->DrawLinePath(triangle, true);
}

void UltraCanvasRequirementDiagram::RenderNodes(IRenderContext* ctx) {
    for (const auto& id : nodeOrder) {
        auto it = nodes.find(id);
        if (it == nodes.end()) continue;
        RenderNode(ctx, it->second);
    }
}

void UltraCanvasRequirementDiagram::RenderNode(IRenderContext* ctx, const RequirementNode& node) {
    if (node.width <= 0.0 || node.height <= 0.0) return;

    const Rect2Dd rect = NodeRect(node);
    const Color fill = ResolveFillColor(node);
    Color border = ResolveBorderColor(node);
    double borderWidth = node.borderWidth > 0.0 ? node.borderWidth : style.borderWidth;

    if (node.isSelected) {
        border = palette.selectionColor;
        borderWidth = style.selectionWidth;
    } else if (node.isHovered) {
        border = palette.hoverColor;
        borderWidth = std::max(borderWidth, style.borderWidth + 0.6);
    }

    ctx->SetLineDash(UCDashPattern::EMPTY);
    switch (ResolveShape(node)) {
        case RequirementNodeShape::Oval:
            ctx->SetFillPaint(fill);
            ctx->FillEllipse(rect);
            ctx->SetStrokePaint(border);
            ctx->SetStrokeWidth(borderWidth);
            ctx->DrawEllipse(rect);
            break;

        case RequirementNodeShape::RoundedRectangle:
            ctx->DrawFilledRectangle(rect, fill, static_cast<float>(borderWidth), border,
                                     static_cast<float>(style.nodeCornerRadius));
            break;

        case RequirementNodeShape::Rectangle:
        default:
            ctx->DrawFilledRectangle(rect, fill, static_cast<float>(borderWidth), border, 0.0f);
            break;
    }

    // Q22 (opt-in in phase 1): risk as a coloured left edge stripe.
    if (showRiskStripe && node.risk != RequirementRisk::Unspecified) {
        ctx->SetFillPaint(palette.ColorForRisk(node.risk));
        ctx->FillRectangle(Rect2Dd(rect.x, rect.y, 4.0, rect.height));
    }

    auto measuredIt = measuredNodes.find(node.id);
    if (measuredIt != measuredNodes.end()) {
        RenderNodeBody(ctx, node, measuredIt->second);
    }
}

void UltraCanvasRequirementDiagram::DrawTextLine(IRenderContext* ctx, const MeasuredLine& line,
                                                  double x, double y, double boxWidth) {
    ctx->SetFontFace(style.fontFamily,
                     line.bold ? FontWeight::Bold : FontWeight::Normal,
                     FontSlant::Normal);
    ctx->SetFontSize(line.fontSize);
    ctx->SetTextPaint(line.color);

    double drawX = x;
    if (line.centered) {
        const int textWidth = ctx->GetTextLineWidth(line.text);
        drawX = x + (boxWidth - static_cast<double>(textWidth)) / 2.0;
        if (drawX < x) drawX = x;
    }
    // Y is the TOP of the text box in this framework, not the baseline.
    ctx->DrawText(line.text, Point2Dd(drawX, y));
}

void UltraCanvasRequirementDiagram::RenderNodeBody(IRenderContext* ctx,
                                                    const RequirementNode& node,
                                                    const MeasuredNode& measured) {
    const double contentX = node.x + style.nodePadding;
    const double contentWidth = node.width - style.nodePadding * 2.0;
    double y = node.y + style.nodePadding;

    // Optional header band behind the stereotype/name block.
    if (palette.headerBandColor.a > 0 && measured.hasDivider) {
        ctx->SetFillPaint(palette.headerBandColor);
        ctx->FillRectangle(Rect2Dd(node.x + 1.0, node.y + 1.0, node.width - 2.0,
                                   measured.headerHeight + style.nodePadding * 2.0 - 2.0));
    }

    for (const auto& line : measured.headerLines) {
        ctx->SetFontFace(style.fontFamily,
                         line.bold ? FontWeight::Bold : FontWeight::Normal,
                         FontSlant::Normal);
        ctx->SetFontSize(line.fontSize);
        const double lineHeight = ctx->GetTextLineHeight(line.text);
        DrawTextLine(ctx, line, contentX, y, contentWidth);
        y += lineHeight + style.rowSpacing;
    }

    if (measured.hasDivider) {
        y += style.headerGap - style.rowSpacing;
        ctx->SetLineDash(UCDashPattern::EMPTY);
        ctx->SetStrokePaint(palette.dividerColor);
        ctx->SetStrokeWidth(1.0);
        ctx->DrawLine(Point2Dd(node.x, y), Point2Dd(node.x + node.width, y));
        y += style.headerGap;

        for (const auto& line : measured.bodyLines) {
            ctx->SetFontFace(style.fontFamily,
                             line.bold ? FontWeight::Bold : FontWeight::Normal,
                             FontSlant::Normal);
            ctx->SetFontSize(line.fontSize);
            const double lineHeight = ctx->GetTextLineHeight(line.text);
            DrawTextLine(ctx, line, contentX, y, contentWidth);
            y += lineHeight + style.rowSpacing;
        }
    }
}

void UltraCanvasRequirementDiagram::RenderCallouts(IRenderContext* ctx) {
    for (const auto& callout : callouts) {
        const auto* target = GetNode(callout.targetNodeId);
        if (!target) continue;

        const Rect2Dd rect(callout.x, callout.y, callout.width, callout.height);
        const Color fill = callout.hasFillColor ? callout.fillColor : palette.calloutFill;
        const Color border = callout.isSelected ? palette.selectionColor
                                                 : palette.calloutBorder;

        // Dashed leader line from the callout to its target, drawn first so the
        // box covers the stub.
        ctx->SetLineDash(style.leaderDash);
        ctx->SetStrokePaint(palette.calloutBorder);
        ctx->SetStrokeWidth(style.relationLineWidth);
        ctx->DrawLine(Point2Dd(rect.x + rect.width / 2.0, rect.y + rect.height / 2.0),
                      Point2Dd(target->x + target->width / 2.0,
                               target->y + target->height / 2.0));
        ctx->SetLineDash(UCDashPattern::EMPTY);

        ctx->DrawFilledRectangle(rect, fill,
                                 static_cast<float>(callout.isSelected ? style.selectionWidth
                                                                        : style.borderWidth),
                                 border, 3.0f);

        auto it = measuredCallouts.find(callout.id);
        if (it == measuredCallouts.end()) continue;
        const MeasuredNode& measured = it->second;

        const double contentX = rect.x + style.nodePadding;
        const double contentWidth = rect.width - style.nodePadding * 2.0;
        double y = rect.y + style.nodePadding;

        for (const auto& line : measured.headerLines) {
            ctx->SetFontFace(style.fontFamily,
                             line.bold ? FontWeight::Bold : FontWeight::Normal,
                             FontSlant::Normal);
            ctx->SetFontSize(line.fontSize);
            const double lineHeight = ctx->GetTextLineHeight(line.text);
            DrawTextLine(ctx, line, contentX, y, contentWidth);
            y += lineHeight + style.rowSpacing;
        }
        if (measured.hasDivider) {
            y += style.headerGap - style.rowSpacing;
            ctx->SetStrokePaint(palette.dividerColor);
            ctx->SetStrokeWidth(1.0);
            ctx->DrawLine(Point2Dd(rect.x, y), Point2Dd(rect.x + rect.width, y));
            y += style.headerGap;
        }
        for (const auto& line : measured.bodyLines) {
            ctx->SetFontFace(style.fontFamily,
                             line.bold ? FontWeight::Bold : FontWeight::Normal,
                             FontSlant::Normal);
            ctx->SetFontSize(line.fontSize);
            const double lineHeight = ctx->GetTextLineHeight(line.text);
            DrawTextLine(ctx, line, contentX, y, contentWidth);
            y += lineHeight + style.rowSpacing;
        }
    }
}

void UltraCanvasRequirementDiagram::RenderSelectionBox(IRenderContext* ctx) {
    const double x = std::min(selectionBoxStart.x, selectionBoxEnd.x);
    const double y = std::min(selectionBoxStart.y, selectionBoxEnd.y);
    const double w = std::abs(selectionBoxEnd.x - selectionBoxStart.x);
    const double h = std::abs(selectionBoxEnd.y - selectionBoxStart.y);

    ctx->SetLineDash(UCDashPattern::EMPTY);
    ctx->SetFillPaint(style.selectionBoxFill);
    ctx->FillRectangle(Rect2Dd(x, y, w, h));
    ctx->SetStrokePaint(style.selectionBoxStroke);
    ctx->SetStrokeWidth(1.0 / std::max(0.0001, zoomLevel));
    ctx->DrawRectangle(Rect2Dd(x, y, w, h));
}

void UltraCanvasRequirementDiagram::RenderTitle(IRenderContext* ctx) {
    const Rect2Dd banner(0.0, 0.0, GetWidth(), titleConfig.height);
    ctx->SetFillPaint(titleConfig.backgroundColor);
    ctx->FillRectangle(banner);

    ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
    ctx->SetFontSize(titleConfig.fontSize);
    ctx->SetTextPaint(titleConfig.textColor);

    const Size2Di titleDim = ctx->GetTextLineDimensions(titleConfig.title);
    const bool hasSubtitle = !titleConfig.subtitle.empty();
    const double totalHeight = hasSubtitle
        ? titleDim.height + titleConfig.subtitleFontSize * 1.4
        : titleDim.height;
    double y = (titleConfig.height - totalHeight) / 2.0;

    double x = 12.0;
    if (titleConfig.alignment == TextAlignment::Center) {
        x = (GetWidth() - static_cast<double>(titleDim.width)) / 2.0;
    } else if (titleConfig.alignment == TextAlignment::Right) {
        x = GetWidth() - titleDim.width - 12.0;
    }
    ctx->DrawText(titleConfig.title, Point2Dd(x, y));
    y += titleDim.height;

    if (hasSubtitle) {
        ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(titleConfig.subtitleFontSize);
        const Size2Di subDim = ctx->GetTextLineDimensions(titleConfig.subtitle);
        double sx = 12.0;
        if (titleConfig.alignment == TextAlignment::Center) {
            sx = (GetWidth() - static_cast<double>(subDim.width)) / 2.0;
        } else if (titleConfig.alignment == TextAlignment::Right) {
            sx = GetWidth() - subDim.width - 12.0;
        }
        ctx->DrawText(titleConfig.subtitle, Point2Dd(sx, y));
    }
}

std::vector<RequirementLegendEntry> UltraCanvasRequirementDiagram::BuildLegendEntries() const {
    std::vector<RequirementLegendEntry> entries;

    switch (legendConfig.source) {
        case RequirementLegendSource::Custom:
            return customLegendEntries;

        case RequirementLegendSource::Categories: {
            // Only categories actually present on the diagram, in registration
            // order, so the legend never advertises an unused colour.
            std::set<std::string> used;
            for (const auto& [id, node] : nodes) {
                (void)id;
                if (!node.category.empty()) used.insert(node.category);
            }
            for (const auto& name : categoryOrder) {
                if (used.find(name) == used.end()) continue;
                RequirementLegendEntry entry;
                entry.label = name;
                const auto* category = GetCategory(name);
                entry.color = category ? category->fillColor : Color(220, 220, 220, 255);
                entries.push_back(entry);
            }
            break;
        }
        case RequirementLegendSource::NodeKinds: {
            std::vector<RequirementNodeKind> seen;
            for (const auto& id : nodeOrder) {
                auto it = nodes.find(id);
                if (it == nodes.end()) continue;
                if (std::find(seen.begin(), seen.end(), it->second.kind) != seen.end()) continue;
                seen.push_back(it->second.kind);
                RequirementLegendEntry entry;
                entry.label = std::string("«") +
                              RequirementNodeKindToString(it->second.kind) + "»";
                entry.color = palette.FillForKind(it->second.kind);
                entries.push_back(entry);
            }
            break;
        }
        case RequirementLegendSource::RelationKinds: {
            std::vector<RequirementRelationKind> seen;
            for (const auto& r : relations) {
                if (std::find(seen.begin(), seen.end(), r.kind) != seen.end()) continue;
                seen.push_back(r.kind);
                RequirementLegendEntry entry;
                entry.label = RequirementRelationKindToString(r.kind);
                entry.color = palette.ColorForRelation(r.kind);
                entry.isLine = true;
                entry.dashed = (r.kind != RequirementRelationKind::Containment &&
                                r.kind != RequirementRelationKind::Generalization);
                entries.push_back(entry);
            }
            break;
        }
    }
    return entries;
}

void UltraCanvasRequirementDiagram::RenderLegend(IRenderContext* ctx) {
    const std::vector<RequirementLegendEntry> entries = BuildLegendEntries();
    if (entries.empty()) return;

    ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
    ctx->SetFontSize(legendConfig.fontSize + 1.0);
    const Size2Di titleDim = ctx->GetTextLineDimensions(legendConfig.title);

    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(legendConfig.fontSize);

    double contentWidth = legendConfig.title.empty() ? 0.0 : titleDim.width;
    double rowHeight = 0.0;
    for (const auto& entry : entries) {
        const Size2Di dim = ctx->GetTextLineDimensions(entry.label);
        contentWidth = std::max(contentWidth,
                                legendConfig.swatchSize + 6.0 + static_cast<double>(dim.width));
        rowHeight = std::max(rowHeight,
                             std::max(static_cast<double>(dim.height), legendConfig.swatchSize));
    }
    if (rowHeight <= 0.0) rowHeight = legendConfig.swatchSize;

    const double titleHeight = legendConfig.title.empty()
                                   ? 0.0
                                   : titleDim.height + legendConfig.rowGap;
    const double panelWidth = contentWidth + legendConfig.innerPadding * 2.0;
    const double panelHeight = titleHeight +
                               static_cast<double>(entries.size()) * (rowHeight + legendConfig.rowGap) -
                               legendConfig.rowGap + legendConfig.innerPadding * 2.0;

    double px = legendConfig.padding;
    double py = legendConfig.padding + (titleConfig.visible ? titleConfig.height : 0.0);
    switch (legendConfig.position) {
        case RequirementPanelPosition::TopLeft:
            break;
        case RequirementPanelPosition::TopRight:
            px = GetWidth() - panelWidth - legendConfig.padding;
            break;
        case RequirementPanelPosition::BottomLeft:
            py = GetHeight() - panelHeight - legendConfig.padding;
            break;
        case RequirementPanelPosition::BottomRight:
            px = GetWidth() - panelWidth - legendConfig.padding;
            py = GetHeight() - panelHeight - legendConfig.padding;
            break;
    }

    ctx->SetLineDash(UCDashPattern::EMPTY);
    ctx->DrawFilledRectangle(Rect2Dd(px, py, panelWidth, panelHeight),
                             legendConfig.backgroundColor, 1.0f,
                             legendConfig.borderColor, 4.0f);

    double y = py + legendConfig.innerPadding;
    if (!legendConfig.title.empty()) {
        ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
        ctx->SetFontSize(legendConfig.fontSize + 1.0);
        ctx->SetTextPaint(legendConfig.textColor);
        ctx->DrawText(legendConfig.title, Point2Dd(px + legendConfig.innerPadding, y));
        y += titleDim.height + legendConfig.rowGap;
    }

    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(legendConfig.fontSize);
    for (const auto& entry : entries) {
        const double swatchX = px + legendConfig.innerPadding;
        const double swatchY = y + (rowHeight - legendConfig.swatchSize) / 2.0;

        if (entry.isLine) {
            ctx->SetLineDash(entry.dashed ? style.relationDash : UCDashPattern::EMPTY);
            ctx->SetStrokePaint(entry.color);
            ctx->SetStrokeWidth(2.0);
            ctx->DrawLine(Point2Dd(swatchX, swatchY + legendConfig.swatchSize / 2.0),
                          Point2Dd(swatchX + legendConfig.swatchSize,
                                   swatchY + legendConfig.swatchSize / 2.0));
            ctx->SetLineDash(UCDashPattern::EMPTY);
        } else {
            ctx->DrawFilledRectangle(
                Rect2Dd(swatchX, swatchY, legendConfig.swatchSize, legendConfig.swatchSize),
                entry.color, 1.0f, legendConfig.borderColor, 2.0f);
        }

        ctx->SetTextPaint(legendConfig.textColor);
        const Size2Di dim = ctx->GetTextLineDimensions(entry.label);
        ctx->DrawText(entry.label,
                      Point2Dd(swatchX + legendConfig.swatchSize + 6.0,
                               y + (rowHeight - static_cast<double>(dim.height)) / 2.0));
        y += rowHeight + legendConfig.rowGap;
    }
}

// =============================================================================
// EVENTS
// =============================================================================

bool UltraCanvasRequirementDiagram::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;

    switch (event.type) {
        case UCEventType::MouseDown:  return HandleMouseDown(event);
        case UCEventType::MouseUp:    return HandleMouseUp(event);
        case UCEventType::MouseMove:  return HandleMouseMove(event);
        case UCEventType::MouseWheel: return HandleMouseWheel(event);
        case UCEventType::MouseDoubleClick: {
            if (!Contains(Point2Di(event.pointer.x, event.pointer.y))) return false;
            const std::string nodeId = FindNodeAt(Point2Di(event.pointer.x, event.pointer.y));
            if (!nodeId.empty() && onNodeDoubleClick) {
                onNodeDoubleClick(nodeId);
                return true;
            }
            return false;
        }
        case UCEventType::KeyDown: return HandleKeyDown(event);
        default: break;
    }
    return UltraCanvasUIElement::OnEvent(event);
}

bool UltraCanvasRequirementDiagram::HandleMouseDown(const UCEvent& event) {
    const Point2Di mousePos(event.pointer.x, event.pointer.y);
    if (!Contains(mousePos)) return false;
    if (!isInteractive && event.button != UCMouseButton::Middle) return false;

    lastMousePos = mousePos;
    dragStartPos = mousePos;

    if (event.button == UCMouseButton::Right) {
        if (FindNodeAt(mousePos).empty() && FindCalloutAt(mousePos).empty() &&
            FindRelationAt(mousePos).empty() && onCanvasRightClick) {
            const Point2Dd world = ScreenToWorld(mousePos);
            onCanvasRightClick(world.x, world.y);
        }
        return true;
    }

    if (event.button == UCMouseButton::Middle) {
        isDraggingViewport = true;
        return true;
    }

    // Callouts sit above the nodes and are dragged independently.
    const std::string calloutId = FindCalloutAt(mousePos);
    if (!calloutId.empty()) {
        for (auto& c : callouts) c.isSelected = (c.id == calloutId);
        if (auto* callout = GetCallout(calloutId)) {
            isDraggingCallout = true;
            draggedCalloutId = calloutId;
            calloutDragStart = Point2Dd(callout->x, callout->y);
        }
        RequestRedraw();
        return true;
    }
    for (auto& c : callouts) c.isSelected = false;

    const std::string nodeId = FindNodeAt(mousePos);
    if (!nodeId.empty()) {
        if (event.shift) {
            if (selectedNodes.find(nodeId) != selectedNodes.end()) {
                selectedNodes.erase(nodeId);
            } else {
                selectedNodes.insert(nodeId);
            }
        } else if (selectedNodes.find(nodeId) == selectedNodes.end()) {
            selectedNodes.clear();
            selectedRelations.clear();
            selectedNodes.insert(nodeId);
        }
        NotifySelectionChange();

        if (nodesDraggable) {
            isDraggingNode = true;
            dragStartPositions.clear();
            for (const auto& id : selectedNodes) {
                if (const auto* n = GetNode(id)) {
                    dragStartPositions[id] = Point2Dd(n->x, n->y);
                }
            }
        }
        if (onNodeClick) onNodeClick(nodeId);
        RequestRedraw();
        return true;
    }

    const std::string relationId = FindRelationAt(mousePos);
    if (!relationId.empty()) {
        SelectRelation(relationId, event.shift);
        if (onRelationClick) onRelationClick(relationId);
        return true;
    }

    // Empty canvas: rubber-band select, or pan when dragging is configured.
    if (event.shift || !panOnDrag) {
        isSelectingBox = true;
        selectionBoxStart = ScreenToWorld(mousePos);
        selectionBoxEnd = selectionBoxStart;
        if (!event.shift) {
            selectedNodes.clear();
            selectedRelations.clear();
            NotifySelectionChange();
        }
    } else {
        isDraggingViewport = true;
        selectedNodes.clear();
        selectedRelations.clear();
        NotifySelectionChange();
    }
    RequestRedraw();
    return true;
}

bool UltraCanvasRequirementDiagram::HandleMouseMove(const UCEvent& event) {
    const Point2Di mousePos(event.pointer.x, event.pointer.y);

    if (isDraggingViewport) {
        panOffset.x += mousePos.x - lastMousePos.x;
        panOffset.y += mousePos.y - lastMousePos.y;
        lastMousePos = mousePos;
        NotifyViewportChange();
        RequestRedraw();
        return true;
    }

    if (isDraggingCallout) {
        if (auto* callout = GetCallout(draggedCalloutId)) {
            const Point2Dd delta((mousePos.x - dragStartPos.x) / zoomLevel,
                                 (mousePos.y - dragStartPos.y) / zoomLevel);
            const Point2Dd moved = SnapPoint(Point2Dd(calloutDragStart.x + delta.x,
                                                      calloutDragStart.y + delta.y));
            callout->x = moved.x;
            callout->y = moved.y;
            RequestRedraw();
        }
        return true;
    }

    if (isDraggingNode) {
        const Point2Dd delta((mousePos.x - dragStartPos.x) / zoomLevel,
                             (mousePos.y - dragStartPos.y) / zoomLevel);
        for (const auto& [id, startPos] : dragStartPositions) {
            auto* node = GetNode(id);
            if (!node) continue;
            const Point2Dd moved = SnapPoint(Point2Dd(startPos.x + delta.x,
                                                      startPos.y + delta.y));
            node->x = moved.x;
            node->y = moved.y;
            // A dragged node keeps its position through the next auto-layout.
            node->pinned = true;
            if (onNodeDrag) onNodeDrag(id, node->x, node->y);
        }
        RequestRedraw();
        return true;
    }

    if (isSelectingBox) {
        selectionBoxEnd = ScreenToWorld(mousePos);
        RequestRedraw();
        return true;
    }

    if (!Contains(mousePos)) {
        if (!hoveredNodeId.empty() || !hoveredRelationId.empty()) {
            hoveredNodeId.clear();
            hoveredRelationId.clear();
            for (auto& [id, node] : nodes) { (void)id; node.isHovered = false; }
            for (auto& r : relations) r.isHovered = false;
            RequestRedraw();
        }
        return false;
    }

    // Hover feedback + tooltip.
    const std::string nodeId = FindNodeAt(mousePos);
    const std::string relationId = nodeId.empty() ? FindRelationAt(mousePos) : std::string();

    if (nodeId != hoveredNodeId || relationId != hoveredRelationId) {
        hoveredNodeId = nodeId;
        hoveredRelationId = relationId;
        for (auto& [id, node] : nodes) node.isHovered = (id == hoveredNodeId);
        for (auto& r : relations) r.isHovered = (r.id == hoveredRelationId);

        if (tooltipsEnabled) {
            std::string tooltip;
            if (const auto* node = GetNode(nodeId)) {
                tooltip = node->name;
                if (!node->text.empty()) tooltip += "\n" + node->text;
            } else if (const auto* relation = GetRelation(relationId)) {
                tooltip = std::string(RequirementRelationKindToString(relation->kind)) +
                          ": " + relation->sourceId + " -> " + relation->targetId;
                if (!relation->rationale.empty()) tooltip += "\n" + relation->rationale;
            }
            SetTooltip(tooltip);
        }
        if (!nodeId.empty() && onNodeHover) onNodeHover(nodeId);
        SetMouseCursor(nodeId.empty() ? UCMouseCursor::Default : UCMouseCursor::Hand);
        RequestRedraw();
    }
    lastMousePos = mousePos;
    return false;
}

bool UltraCanvasRequirementDiagram::HandleMouseUp(const UCEvent& event) {
    if (isSelectingBox) {
        const Rect2Dd box(std::min(selectionBoxStart.x, selectionBoxEnd.x),
                          std::min(selectionBoxStart.y, selectionBoxEnd.y),
                          std::abs(selectionBoxEnd.x - selectionBoxStart.x),
                          std::abs(selectionBoxEnd.y - selectionBoxStart.y));
        for (const auto& [id, node] : nodes) {
            if (box.Intersects(NodeRect(node))) selectedNodes.insert(id);
        }
        isSelectingBox = false;
        NotifySelectionChange();
        RequestRedraw();
        return true;
    }

    const bool wasInteracting = isDraggingNode || isDraggingViewport || isDraggingCallout;
    isDraggingNode = false;
    isDraggingViewport = false;
    isDraggingCallout = false;
    draggedCalloutId.clear();
    dragStartPositions.clear();
    (void)event;
    return wasInteracting;
}

bool UltraCanvasRequirementDiagram::HandleMouseWheel(const UCEvent& event) {
    const Point2Di mousePos(event.pointer.x, event.pointer.y);
    if (!Contains(mousePos) || !zoomOnScroll) return false;

    const double oldZoom = zoomLevel;
    zoomLevel *= (event.wheelDelta > 0) ? 1.1 : (1.0 / 1.1);
    ClampZoom();

    // Keep the world point under the cursor fixed.
    panOffset.x = mousePos.x - (mousePos.x - panOffset.x) * (zoomLevel / oldZoom);
    panOffset.y = mousePos.y - (mousePos.y - panOffset.y) * (zoomLevel / oldZoom);

    NotifyViewportChange();
    RequestRedraw();
    return true;
}

bool UltraCanvasRequirementDiagram::HandleKeyDown(const UCEvent& event) {
    switch (event.virtualKey) {
        case UCKeys::Escape:
            DeselectAll();
            isSelectingBox = false;
            return true;
        case UCKeys::A:
            if (event.ctrl) {
                SelectAll();
                return true;
            }
            break;
        default:
            break;
    }
    return false;
}

// =============================================================================
// SERIALIZATION
// =============================================================================

std::string UltraCanvasRequirementDiagram::ToJson(bool pretty) const {
    JSONValue root = JSONValue::MakeObject();
    root.Set("version", JSONValue("1.0"));

    JSONValue viewport = JSONValue::MakeObject();
    viewport.Set("zoom", JSONValue(zoomLevel));
    viewport.Set("panX", JSONValue(panOffset.x));
    viewport.Set("panY", JSONValue(panOffset.y));
    root.Set("viewport", std::move(viewport));

    JSONValue diagram = JSONValue::MakeObject();
    diagram.Set("palette", JSONValue(static_cast<int>(paletteKind)));
    diagram.Set("colorSource", JSONValue(static_cast<int>(colorSource)));
    diagram.Set("layoutMode", JSONValue(static_cast<int>(layoutMode)));
    diagram.Set("orientation", JSONValue(static_cast<int>(orientation)));
    diagram.Set("defaultRouting", JSONValue(static_cast<int>(defaultRouting)));
    diagram.Set("title", JSONValue(titleConfig.title));
    diagram.Set("subtitle", JSONValue(titleConfig.subtitle));
    diagram.Set("titleVisible", JSONValue(titleConfig.visible));
    diagram.Set("legendVisible", JSONValue(legendConfig.visible));
    diagram.Set("legendSource", JSONValue(static_cast<int>(legendConfig.source)));
    root.Set("diagram", std::move(diagram));

    JSONValue nodeArray = JSONValue::MakeArray();
    for (const auto& id : nodeOrder) {
        auto it = nodes.find(id);
        if (it == nodes.end()) continue;
        const RequirementNode& node = it->second;

        JSONValue value = JSONValue::MakeObject();
        value.Set("id", JSONValue(node.id));
        value.Set("name", JSONValue(node.name));
        value.Set("kind", JSONValue(RequirementNodeKindToString(node.kind)));
        if (!node.stereotype.empty()) value.Set("stereotype", JSONValue(node.stereotype));
        if (!node.text.empty())       value.Set("text", JSONValue(node.text));
        if (!node.source.empty())     value.Set("source", JSONValue(node.source));
        if (node.risk != RequirementRisk::Unspecified) {
            value.Set("risk", JSONValue(RequirementRiskToString(node.risk)));
        }
        if (node.verifyMethod != RequirementVerifyMethod::Unspecified) {
            value.Set("verifyMethod",
                      JSONValue(RequirementVerifyMethodToString(node.verifyMethod)));
        }
        if (!node.status.empty())   value.Set("status", JSONValue(node.status));
        if (!node.owner.empty())    value.Set("owner", JSONValue(node.owner));
        if (!node.priority.empty()) value.Set("priority", JSONValue(node.priority));
        if (!node.docRef.empty())   value.Set("docRef", JSONValue(node.docRef));
        if (!node.category.empty()) value.Set("category", JSONValue(node.category));
        value.Set("detail", JSONValue(static_cast<int>(node.detail)));
        value.Set("shape", JSONValue(static_cast<int>(node.shape)));
        value.Set("x", JSONValue(node.x));
        value.Set("y", JSONValue(node.y));
        value.Set("width", JSONValue(node.width));
        value.Set("height", JSONValue(node.height));
        value.Set("pinned", JSONValue(node.pinned));
        if (node.hasFillColor)   value.Set("fillColor", JSON::FromColor(node.fillColor));
        if (node.hasBorderColor) value.Set("borderColor", JSON::FromColor(node.borderColor));

        if (!node.customProperties.empty()) {
            JSONValue custom = JSONValue::MakeObject();
            for (const auto& [key, propertyValue] : node.customProperties) {
                custom.Set(key, JSONValue(propertyValue));
            }
            value.Set("customProperties", std::move(custom));
        }
        nodeArray.Append(std::move(value));
    }
    root.Set("nodes", std::move(nodeArray));

    JSONValue relationArray = JSONValue::MakeArray();
    for (const auto& relation : relations) {
        JSONValue value = JSONValue::MakeObject();
        value.Set("id", JSONValue(relation.id));
        value.Set("kind", JSONValue(RequirementRelationKindToString(relation.kind)));
        value.Set("source", JSONValue(relation.sourceId));
        value.Set("target", JSONValue(relation.targetId));
        if (!relation.label.empty())     value.Set("label", JSONValue(relation.label));
        if (!relation.rationale.empty()) value.Set("rationale", JSONValue(relation.rationale));
        if (relation.hasColor)           value.Set("color", JSON::FromColor(relation.color));
        if (!relation.visible)           value.Set("visible", JSONValue(false));
        if (!relation.useDefaultRouting) {
            value.Set("routing", JSONValue(static_cast<int>(relation.routing)));
        }
        relationArray.Append(std::move(value));
    }
    root.Set("relations", std::move(relationArray));

    JSONValue calloutArray = JSONValue::MakeArray();
    for (const auto& callout : callouts) {
        JSONValue value = JSONValue::MakeObject();
        value.Set("id", JSONValue(callout.id));
        value.Set("target", JSONValue(callout.targetNodeId));
        value.Set("x", JSONValue(callout.x));
        value.Set("y", JSONValue(callout.y));
        value.Set("width", JSONValue(callout.width));
        if (!callout.headerText.empty()) value.Set("header", JSONValue(callout.headerText));
        JSONValue fieldArray = JSONValue::MakeArray();
        for (RequirementField field : callout.fields) {
            fieldArray.Append(JSONValue(static_cast<int>(field)));
        }
        value.Set("fields", std::move(fieldArray));
        calloutArray.Append(std::move(value));
    }
    root.Set("callouts", std::move(calloutArray));

    JSONValue categoryArray = JSONValue::MakeArray();
    for (const auto& name : categoryOrder) {
        auto it = categories.find(name);
        if (it == categories.end()) continue;
        JSONValue value = JSONValue::MakeObject();
        value.Set("name", JSONValue(it->second.name));
        value.Set("fillColor", JSON::FromColor(it->second.fillColor));
        value.Set("borderColor", JSON::FromColor(it->second.borderColor));
        categoryArray.Append(std::move(value));
    }
    root.Set("categories", std::move(categoryArray));

    JSONSerializeOptions options;
    options.pretty = pretty;
    options.indentWidth = 2;
    return JSON::Serialize(root, options);
}

bool UltraCanvasRequirementDiagram::FromJson(const std::string& json) {
    JSONParseResult result;
    const JSONValue root = JSON::Parse(json, &result);
    if (!result.success || !root.IsObject()) return false;

    Clear();

    const JSONValue& diagram = root.Get("diagram");
    if (diagram.IsObject()) {
        SetPalette(static_cast<RequirementPaletteKind>(
            diagram.Get("palette").GetInteger(static_cast<int>(RequirementPaletteKind::Classic))));
        colorSource = static_cast<RequirementColorSource>(
            diagram.Get("colorSource").GetInteger(static_cast<int>(RequirementColorSource::ByKind)));
        layoutMode = static_cast<RequirementLayoutMode>(
            diagram.Get("layoutMode").GetInteger(static_cast<int>(RequirementLayoutMode::Manual)));
        orientation = static_cast<RequirementOrientation>(
            diagram.Get("orientation").GetInteger(static_cast<int>(RequirementOrientation::TopDown)));
        defaultRouting = static_cast<RequirementRouting>(
            diagram.Get("defaultRouting").GetInteger(static_cast<int>(RequirementRouting::Orthogonal)));
        titleConfig.title = diagram.Get("title").GetString();
        titleConfig.subtitle = diagram.Get("subtitle").GetString();
        titleConfig.visible = diagram.Get("titleVisible").GetBoolean(false);
        legendConfig.visible = diagram.Get("legendVisible").GetBoolean(false);
        legendConfig.source = static_cast<RequirementLegendSource>(
            diagram.Get("legendSource").GetInteger(
                static_cast<int>(RequirementLegendSource::Categories)));
    }

    const JSONValue& categoryArray = root.Get("categories");
    for (size_t i = 0; i < categoryArray.GetSize(); ++i) {
        const JSONValue& value = categoryArray.At(i);
        RequirementCategory category;
        category.name = value.Get("name").GetString();
        JSON::ToColor(value.Get("fillColor"), category.fillColor);
        JSON::ToColor(value.Get("borderColor"), category.borderColor);
        AddCategory(category);
    }

    const JSONValue& nodeArray = root.Get("nodes");
    for (size_t i = 0; i < nodeArray.GetSize(); ++i) {
        const JSONValue& value = nodeArray.At(i);
        RequirementNode node;
        node.id = value.Get("id").GetString();
        node.name = value.Get("name").GetString();
        node.kind = RequirementNodeKindFromString(value.Get("kind").GetString("requirement"));
        node.stereotype = value.Get("stereotype").GetString();
        node.text = value.Get("text").GetString();
        node.source = value.Get("source").GetString();
        node.risk = RequirementRiskFromString(value.Get("risk").GetString());
        node.verifyMethod =
            RequirementVerifyMethodFromString(value.Get("verifyMethod").GetString());
        node.status = value.Get("status").GetString();
        node.owner = value.Get("owner").GetString();
        node.priority = value.Get("priority").GetString();
        node.docRef = value.Get("docRef").GetString();
        node.category = value.Get("category").GetString();
        node.detail = static_cast<RequirementDetailLevel>(
            value.Get("detail").GetInteger(static_cast<int>(RequirementDetailLevel::Full)));
        node.shape = static_cast<RequirementNodeShape>(
            value.Get("shape").GetInteger(static_cast<int>(RequirementNodeShape::Auto)));
        node.x = value.Get("x").GetNumber(0.0);
        node.y = value.Get("y").GetNumber(0.0);
        node.width = value.Get("width").GetNumber(0.0);
        node.height = value.Get("height").GetNumber(0.0);
        node.pinned = value.Get("pinned").GetBoolean(false);
        node.hasFillColor = JSON::ToColor(value.Get("fillColor"), node.fillColor);
        node.hasBorderColor = JSON::ToColor(value.Get("borderColor"), node.borderColor);

        const JSONValue& custom = value.Get("customProperties");
        if (custom.IsObject()) {
            for (const auto& [key, propertyValue] : custom.GetMembers()) {
                node.customProperties[key] = propertyValue.GetString();
            }
        }
        AddNode(node);
    }

    const JSONValue& relationArray = root.Get("relations");
    for (size_t i = 0; i < relationArray.GetSize(); ++i) {
        const JSONValue& value = relationArray.At(i);
        RequirementRelation relation;
        relation.id = value.Get("id").GetString();
        relation.kind = RequirementRelationKindFromString(value.Get("kind").GetString("trace"));
        relation.sourceId = value.Get("source").GetString();
        relation.targetId = value.Get("target").GetString();
        relation.label = value.Get("label").GetString();
        relation.rationale = value.Get("rationale").GetString();
        relation.hasColor = JSON::ToColor(value.Get("color"), relation.color);
        relation.visible = value.Get("visible").GetBoolean(true);
        if (value.Contains("routing")) {
            relation.routing = static_cast<RequirementRouting>(
                value.Get("routing").GetInteger(static_cast<int>(RequirementRouting::Orthogonal)));
            relation.useDefaultRouting = false;
        }
        AddRelation(relation);
    }

    const JSONValue& calloutArray = root.Get("callouts");
    for (size_t i = 0; i < calloutArray.GetSize(); ++i) {
        const JSONValue& value = calloutArray.At(i);
        RequirementCallout callout;
        callout.id = value.Get("id").GetString();
        callout.targetNodeId = value.Get("target").GetString();
        callout.x = value.Get("x").GetNumber(0.0);
        callout.y = value.Get("y").GetNumber(0.0);
        callout.width = value.Get("width").GetNumber(220.0);
        callout.headerText = value.Get("header").GetString();
        const JSONValue& fieldArray = value.Get("fields");
        for (size_t f = 0; f < fieldArray.GetSize(); ++f) {
            callout.fields.push_back(
                static_cast<RequirementField>(fieldArray.At(f).GetInteger(0)));
        }
        AddCallout(callout);
    }

    const JSONValue& viewport = root.Get("viewport");
    if (viewport.IsObject()) {
        zoomLevel = viewport.Get("zoom").GetNumber(1.0);
        panOffset.x = viewport.Get("panX").GetNumber(0.0);
        panOffset.y = viewport.Get("panY").GetNumber(0.0);
        ClampZoom();
        // Explicit viewport in the file wins over the auto-fit that a
        // ContainmentTree layout would otherwise apply.
        autoFitOnLayout = false;
    }

    InvalidateMeasurement();
    return true;
}

} // namespace UltraCanvas
