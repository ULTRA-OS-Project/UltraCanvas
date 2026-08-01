// Plugins/Diagrams/UltraCanvasRequirementDiagram.cpp
// SysML requirement diagram component - palette, model forwarding, measurement
// Version: 2.0.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework
//
// The element is split across three translation units so no single file grows
// past comprehension:
//   * this file          - palette, construction, model forwarding, style,
//                          compartment templates, text measurement
//   * ...Layout.cpp      - tree/layered layout, visibility, geometry, routing
//   * ...Render.cpp      - rendering, events, editing, serialization
//
// See the header for the notation reference and the phase mapping.
//
// Rendering conventions followed throughout (lessons from the sibling
// diagram elements):
//   * DrawText's Y is the TOP of the text bounding box, not the baseline
//     (NodeDiagram 2.0.6 / FlowChart 2.1.2).
//   * Arrowheads are oriented by the direction of the LAST path segment
//     (BlockDiagram 2.2.0).
//   * Connectors sharing a box face are distributed along it (BlockDiagram
//     2.3.0).
//   * World-space content is drawn inside one Push/Pop transform; overlays are
//     drawn after PopState in screen space (NodeDiagram 2.0.0).

#include "Plugins/Diagrams/UltraCanvasRequirementDiagram.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

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
            // Soft yellow / green fills, thin grey borders.
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
            p.compartmentHeadingColor = Color(100, 125, 110, 255);
            p.dividerColor = Color(160, 190, 172, 255);
            p.categoryColors = {
                Color(252, 244, 200, 255),   // yellow
                Color(212, 240, 226, 255),   // green
                Color(206, 236, 240, 255),   // teal
                Color(232, 224, 246, 255),   // lilac
                Color(244, 226, 214, 255),   // peach
                Color(226, 236, 250, 255)    // blue
            };
            break;

        case RequirementPaletteKind::Vibrant:
            // Saturated colour-by-kind on a light canvas.
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
            p.compartmentHeadingColor = Color(150, 158, 172, 255);
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
            p.compartmentHeadingColor = Color(70, 70, 70, 255);
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
// CONSTRUCTION
// =============================================================================

UltraCanvasRequirementDiagram::UltraCanvasRequirementDiagram(
        const std::string& id, int x, int y, int width, int height)
    : UltraCanvasUIElement(id, x, y, width, height) {
    nodeTemplate = RequirementNodeTemplate::Extended();
    palette = RequirementPalette::BuiltIn(RequirementPaletteKind::Classic);
    // Model warnings surface through the element's callback, so callers only
    // ever wire one handler.
    model.onWarning = [this](const RequirementWarning& warning) {
        if (onValidationWarning) onValidationWarning(warning);
    };
}

void UltraCanvasRequirementDiagram::NotifyModelChanged() {
    InvalidateMeasurement();
}

void UltraCanvasRequirementDiagram::InvalidateMeasurement() {
    measurementDirty = true;
    if (layoutMode != RequirementLayoutMode::Manual) layoutDirty = true;
    InvalidateRouting();
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::InvalidateRouting() {
    routingGeneration++;
    RequestRedraw();
}

// =============================================================================
// NODE MANAGEMENT (forwarding)
// =============================================================================

bool UltraCanvasRequirementDiagram::AddNode(const RequirementNode& node) {
    RequirementNode copy = node;
    // A node added without an explicit detail level follows the diagram
    // default; non-requirements collapse, since they carry no properties.
    if (copy.detail == RequirementDetailLevel::Full &&
        copy.kind != RequirementNodeKind::Requirement && !copy.hasCustomTemplate) {
        copy.detail = RequirementDetailLevel::Collapsed;
    }
    if (!model.AddNode(copy)) return false;
    InvalidateMeasurement();
    return true;
}

bool UltraCanvasRequirementDiagram::AddNode(RequirementNodeKind kind, const std::string& id,
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
    node.text = text;
    node.risk = risk;
    node.verifyMethod = verifyMethod;
    node.detail = defaultDetail;
    return AddNode(node);
}

bool UltraCanvasRequirementDiagram::RemoveNode(const std::string& id) {
    if (!model.RemoveNode(id)) return false;
    selectedNodes.erase(id);
    measuredNodes.erase(id);
    highlightedNodes.erase(id);
    InvalidateMeasurement();
    return true;
}

bool UltraCanvasRequirementDiagram::RenameNode(const std::string& oldId,
                                                const std::string& newId) {
    if (!model.RenameNode(oldId, newId)) return false;
    if (selectedNodes.erase(oldId)) selectedNodes.insert(newId);
    if (highlightedNodes.erase(oldId)) highlightedNodes.insert(newId);
    measuredNodes.erase(oldId);
    InvalidateMeasurement();
    return true;
}

void UltraCanvasRequirementDiagram::Clear() {
    model.Clear();
    selectedNodes.clear();
    selectedRelations.clear();
    measuredNodes.clear();
    measuredCallouts.clear();
    highlightedNodes.clear();
    highlightActive = false;
    hoveredNodeId.clear();
    hoveredRelationId.clear();
    hoveredCalloutId.clear();
    hoveredToggleId.clear();
    renamingNodeId.clear();
    routeCache.clear();
    InvalidateMeasurement();
}

RequirementNode* UltraCanvasRequirementDiagram::GetNode(const std::string& id) {
    return model.GetNode(id);
}

const RequirementNode* UltraCanvasRequirementDiagram::GetNode(const std::string& id) const {
    return model.GetNode(id);
}

std::vector<std::string> UltraCanvasRequirementDiagram::GetAllNodeIds() const {
    return model.GetNodeOrder();
}

void UltraCanvasRequirementDiagram::SetNodePosition(const std::string& id,
                                                     double x, double y) {
    if (auto* node = model.GetNode(id)) {
        node->x = x;
        node->y = y;
        InvalidateRouting();
    }
}

void UltraCanvasRequirementDiagram::SetNodeSize(const std::string& id,
                                                 double width, double height) {
    if (auto* node = model.GetNode(id)) {
        node->width = width;
        node->height = height;
        node->hasExplicitWidth = width > 0.0;
        InvalidateMeasurement();
    }
}

void UltraCanvasRequirementDiagram::SetNodeDetail(const std::string& id,
                                                   RequirementDetailLevel detail) {
    if (auto* node = model.GetNode(id)) {
        node->detail = detail;
        InvalidateMeasurement();
    }
}

void UltraCanvasRequirementDiagram::SetNodeCategory(const std::string& id,
                                                     const std::string& category) {
    if (auto* node = model.GetNode(id)) {
        node->category = category;
        RequestRedraw();
    }
}

void UltraCanvasRequirementDiagram::SetNodeColors(const std::string& id,
                                                   const Color& fill, const Color& border) {
    if (auto* node = model.GetNode(id)) {
        node->hasFillColor = true;
        node->fillColor = fill;
        node->hasBorderColor = true;
        node->borderColor = border;
        RequestRedraw();
    }
}

void UltraCanvasRequirementDiagram::SetNodePinned(const std::string& id, bool pinned) {
    if (auto* node = model.GetNode(id)) node->pinned = pinned;
}

void UltraCanvasRequirementDiagram::SetNodeTemplate(const std::string& nodeId,
                                                     const RequirementNodeTemplate& tpl) {
    if (auto* node = model.GetNode(nodeId)) {
        node->customTemplate = tpl;
        node->hasCustomTemplate = true;
        node->detail = RequirementDetailLevel::Custom;
        InvalidateMeasurement();
    }
}

void UltraCanvasRequirementDiagram::SetNoteAnchor(const std::string& noteId,
                                                   const std::string& anchorId) {
    if (auto* node = model.GetNode(noteId)) {
        node->anchorId = anchorId;
        RequestRedraw();
    }
}

std::string UltraCanvasRequirementDiagram::GenerateNodeId(const std::string& prefix) {
    return model.GenerateNodeId(prefix);
}

// =============================================================================
// RELATIONSHIP MANAGEMENT (forwarding)
// =============================================================================

std::string UltraCanvasRequirementDiagram::AddRelation(const RequirementRelation& relation) {
    const std::string id = model.AddRelation(relation);
    if (id.empty()) return id;
    if (layoutMode != RequirementLayoutMode::Manual) layoutDirty = true;
    InvalidateRouting();
    return id;
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
    if (!model.RemoveRelation(id)) return false;
    selectedRelations.erase(id);
    routeCache.erase(id);
    if (layoutMode != RequirementLayoutMode::Manual) layoutDirty = true;
    InvalidateRouting();
    return true;
}

RequirementRelation* UltraCanvasRequirementDiagram::GetRelation(const std::string& id) {
    return model.GetRelation(id);
}

const RequirementRelation* UltraCanvasRequirementDiagram::GetRelation(
        const std::string& id) const {
    return model.GetRelation(id);
}

std::vector<std::string> UltraCanvasRequirementDiagram::GetAllRelationIds() const {
    return model.GetAllRelationIds();
}

void UltraCanvasRequirementDiagram::SetRelationLabel(const std::string& id,
                                                      const std::string& label) {
    if (auto* rel = model.GetRelation(id)) {
        rel->label = label;
        RequestRedraw();
    }
}

void UltraCanvasRequirementDiagram::SetRelationColor(const std::string& id,
                                                      const Color& color) {
    if (auto* rel = model.GetRelation(id)) {
        rel->hasColor = true;
        rel->color = color;
        RequestRedraw();
    }
}

void UltraCanvasRequirementDiagram::SetRelationRouting(const std::string& id,
                                                        RequirementRouting routing) {
    if (auto* rel = model.GetRelation(id)) {
        rel->routing = routing;
        rel->useDefaultRouting = false;
        InvalidateRouting();
    }
}

void UltraCanvasRequirementDiagram::SetRelationVisible(const std::string& id, bool visible) {
    if (auto* rel = model.GetRelation(id)) {
        rel->visible = visible;
        InvalidateRouting();
    }
}

void UltraCanvasRequirementDiagram::SetRelationKindVisible(RequirementRelationKind kind,
                                                            bool visible) {
    for (auto& relation : model.GetRelations()) {
        if (relation.kind == kind) relation.visible = visible;
    }
    InvalidateRouting();
}

std::string UltraCanvasRequirementDiagram::GetParentId(const std::string& nodeId) const {
    return model.GetParentId(nodeId);
}

std::vector<std::string> UltraCanvasRequirementDiagram::GetChildIds(
        const std::string& nodeId) const {
    return model.GetChildIds(nodeId);
}

std::vector<std::string> UltraCanvasRequirementDiagram::GetRootIds() const {
    return model.GetRootIds();
}

std::vector<std::string> UltraCanvasRequirementDiagram::GetRelationsOf(
        const std::string& nodeId) const {
    return model.GetRelationsOf(nodeId);
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

void UltraCanvasRequirementDiagram::SetSemanticsMode(RequirementSemanticsMode mode) {
    model.SetSemanticsMode(mode);
}

int UltraCanvasRequirementDiagram::AssignHierarchicalIds(const std::string& prefix) {
    // Selection and caches are keyed by id, so they cannot survive a bulk
    // rename; clearing them is cheaper and safer than remapping.
    selectedNodes.clear();
    highlightedNodes.clear();
    highlightActive = false;
    measuredNodes.clear();
    routeCache.clear();
    const int renamed = model.AssignHierarchicalIds(prefix);
    InvalidateMeasurement();
    return renamed;
}

// =============================================================================
// CALLOUTS & CATEGORIES (forwarding)
// =============================================================================

bool UltraCanvasRequirementDiagram::AddCallout(const RequirementCallout& callout) {
    if (!model.AddCallout(callout)) return false;
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
    if (!model.RemoveCallout(id)) return false;
    measuredCallouts.erase(id);
    RequestRedraw();
    return true;
}

RequirementCallout* UltraCanvasRequirementDiagram::GetCallout(const std::string& id) {
    return model.GetCallout(id);
}

std::vector<std::string> UltraCanvasRequirementDiagram::GetAllCalloutIds() const {
    std::vector<std::string> ids;
    for (const auto& c : model.GetCallouts()) ids.push_back(c.id);
    return ids;
}

void UltraCanvasRequirementDiagram::AddCategory(const RequirementCategory& category) {
    model.AddCategory(category);
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::AddCategory(const std::string& name, const Color& fill,
                                                 const Color& border) {
    AddCategory(RequirementCategory(name, fill, border));
}

const RequirementCategory* UltraCanvasRequirementDiagram::GetCategory(
        const std::string& name) const {
    return model.GetCategory(name);
}

std::vector<std::string> UltraCanvasRequirementDiagram::GetCategoryNames() const {
    return model.GetCategoryOrder();
}

// =============================================================================
// LEGEND, TITLE & FRAME
// =============================================================================

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

void UltraCanvasRequirementDiagram::SetFrame(const std::string& elementType,
                                              const std::string& elementName,
                                              const std::string& diagramName) {
    frameConfig.elementType = elementType;
    frameConfig.elementName = elementName;
    frameConfig.diagramName = diagramName;
    frameConfig.visible = true;
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SetFrameVisible(bool visible) {
    frameConfig.visible = visible;
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SetFrameConfig(const RequirementFrameConfig& config) {
    frameConfig = config;
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

void UltraCanvasRequirementDiagram::SetFontSize(double propertyFontSize,
                                                 double nameFontSize) {
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

void UltraCanvasRequirementDiagram::SetCoverageOverlayVisible(bool visible) {
    coverageOverlay = visible;
    RequestRedraw();
}

// =============================================================================
// COLOUR RESOLUTION
// =============================================================================

Color UltraCanvasRequirementDiagram::ApplyDim(const Color& color, bool dimmed) const {
    if (!dimmed) return color;
    Color out = color;
    out.a = static_cast<uint8_t>(std::max(0.0, std::min(255.0,
                static_cast<double>(color.a) * style.dimmedAlpha)));
    return out;
}

Color UltraCanvasRequirementDiagram::ResolveFillColor(const RequirementNode& node) const {
    if (node.hasFillColor) return node.fillColor;

    switch (colorSource) {
        case RequirementColorSource::Explicit:
            return palette.FillForKind(node.kind);

        case RequirementColorSource::ByCategory: {
            if (!node.category.empty()) {
                if (const auto* cat = model.GetCategory(node.category)) return cat->fillColor;
                // Unregistered category: derive a stable colour from its
                // position in the diagram's category order.
                const std::vector<std::string>& order = model.GetCategoryOrder();
                auto it = std::find(order.begin(), order.end(), node.category);
                if (it != order.end() && !palette.categoryColors.empty()) {
                    const size_t index = static_cast<size_t>(it - order.begin());
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
        if (const auto* cat = model.GetCategory(node.category)) return cat->borderColor;
    }
    return palette.BorderForKind(node.kind);
}

Color UltraCanvasRequirementDiagram::ResolveTextColor(const RequirementNode& node) const {
    if (node.hasTextColor) return node.textColor;
    if (colorSource == RequirementColorSource::ByCategory && !node.category.empty()) {
        if (const auto* cat = model.GetCategory(node.category)) {
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
        case RequirementNodeKind::TestCase: return RequirementNodeShape::RoundedRectangle;
        case RequirementNodeKind::Actor:   return RequirementNodeShape::StickFigure;
        case RequirementNodeKind::Package: return RequirementNodeShape::Folder;
        case RequirementNodeKind::Rationale:
        case RequirementNodeKind::Problem:
        case RequirementNodeKind::Note:    return RequirementNodeShape::FoldedNote;
        default: break;
    }
    return RequirementNodeShape::Rectangle;
}

// =============================================================================
// COMPARTMENT TEMPLATE
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

    // Drop rows whose value is empty so a leaf with no properties renders as a
    // plain name box rather than an empty compartment.
    std::vector<RequirementPropertyRow> kept;
    kept.reserve(rows.size());
    for (const auto& row : rows) {
        if (!RequirementModel::ResolveField(node, row.field, row.customKey,
                                            row.literal).empty()) {
            kept.push_back(row);
        }
    }
    return kept;
}

// =============================================================================
// TEXT MEASUREMENT
// =============================================================================

static const std::string kEllipsis = "...";
static constexpr double MIN_NODE_HEIGHT = 26.0;

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

    // Mark the truncation rather than silently dropping text.
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
        if (static_cast<int>(lines.size()) > maxLines) {
            lines.resize(static_cast<size_t>(maxLines));
        }
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
    const std::string stereotypeLine = "«" + stereotype + "»";

    const RequirementNodeTemplate& tpl = TemplateForNode(node);
    const std::vector<RequirementPropertyRow> rows = RowsForNode(node);
    const bool showCompartments = (node.detail == RequirementDetailLevel::Full ||
                                   node.detail == RequirementDetailLevel::Custom);

    // ---- content width ---------------------------------------------------
    // Widest of the guillemet line, the name and each property row - clamped
    // to [nodeMinWidth, nodeMaxWidth]; rows then wrap into that width.
    double contentWidth = style.nodeMinWidth - style.nodePadding * 2.0;

    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(style.stereotypeFontSize);
    contentWidth = std::max(contentWidth,
                            static_cast<double>(ctx->GetTextLineWidth(stereotypeLine)));

    ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
    ctx->SetFontSize(nameSize);
    contentWidth = std::max(contentWidth, static_cast<double>(ctx->GetTextLineWidth(node.name)));

    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(propSize);
    for (const auto& row : rows) {
        const std::string value =
            RequirementModel::ResolveField(node, row.field, row.customKey, row.literal);
        const std::string full = RequirementModel::FormatPropertyRow(row.key, value, tpl.format);
        contentWidth = std::max(contentWidth,
                                std::min(static_cast<double>(ctx->GetTextLineWidth(full)),
                                         style.nodeMaxWidth - style.nodePadding * 2.0));
    }
    contentWidth = std::min(contentWidth, style.nodeMaxWidth - style.nodePadding * 2.0);
    contentWidth = std::max(contentWidth, 40.0);

    if (node.hasExplicitWidth && node.width > 0.0) {
        contentWidth = std::max(20.0, node.width - style.nodePadding * 2.0);
    }

    // ---- header ----------------------------------------------------------
    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(style.stereotypeFontSize);
    {
        MeasuredLine line;
        line.text = stereotypeLine;
        line.fontSize = style.stereotypeFontSize;
        line.centered = true;
        line.color = palette.stereotypeTextColor;
        out.headerLines.push_back(line);
        out.headerHeight = ctx->GetTextLineHeight(stereotypeLine);
    }

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

    // ---- property rows ---------------------------------------------------
    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(propSize);
    for (const auto& row : rows) {
        const std::string value =
            RequirementModel::ResolveField(node, row.field, row.customKey, row.literal);
        const std::string full = RequirementModel::FormatPropertyRow(row.key, value, tpl.format);
        for (const auto& text : WrapText(ctx, full, contentWidth, style.maxTextLines)) {
            MeasuredLine line;
            line.text = text;
            line.fontSize = propSize;
            line.bold = row.bold;
            line.color = row.hasColor ? row.textColor : palette.propertyTextColor;
            out.bodyLines.push_back(line);
            out.bodyHeight += ctx->GetTextLineHeight(text) + style.rowSpacing;
        }
    }

    // ---- derived-element compartments (SysML compartment notation) -------
    if (showCompartments) {
        for (RequirementDerivedList list : tpl.derivedCompartments) {
            const std::vector<std::string> members = model.GetDerivedElements(node.id, list);
            if (members.empty()) continue;   // empty compartments are not drawn

            MeasuredLine heading;
            heading.text = RequirementDerivedListToString(list);
            heading.fontSize = propSize;
            heading.bold = true;
            heading.heading = true;
            heading.color = palette.compartmentHeadingColor;
            ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
            ctx->SetFontSize(propSize);
            out.bodyLines.push_back(heading);
            out.bodyHeight += ctx->GetTextLineHeight(heading.text) + style.rowSpacing;

            ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
            for (const auto& memberId : members) {
                const RequirementNode* member = model.GetNode(memberId);
                const std::string label =
                    "  " + (member ? member->name : memberId) +
                    (member && member->kind != RequirementNodeKind::Requirement
                         ? std::string(" «") + RequirementNodeKindToString(member->kind) + "»"
                         : std::string());
                for (const auto& text : WrapText(ctx, label, contentWidth, 2)) {
                    MeasuredLine line;
                    line.text = text;
                    line.fontSize = propSize;
                    line.color = palette.propertyTextColor;
                    out.bodyLines.push_back(line);
                    out.bodyHeight += ctx->GetTextLineHeight(text) + style.rowSpacing;
                }
            }
        }
    }

    out.hasDivider = !out.bodyLines.empty();

    // ---- final box size --------------------------------------------------
    if (!node.hasExplicitWidth) {
        node.width = contentWidth + style.nodePadding * 2.0;
    }
    double height = style.nodePadding * 2.0 + out.headerHeight;
    if (out.hasDivider) height += style.headerGap * 2.0 + out.bodyHeight;
    // The stick figure needs room for the drawn body above the label.
    if (ResolveShape(node) == RequirementNodeShape::StickFigure) height += 26.0;
    node.height = std::max(MIN_NODE_HEIGHT, height);
}

void UltraCanvasRequirementDiagram::MeasureCallout(IRenderContext* ctx,
                                                    RequirementCallout& callout,
                                                    MeasuredNode& out) {
    out = MeasuredNode();
    const RequirementNode* target = model.GetNode(callout.targetNodeId);
    if (!target) return;

    const double contentWidth = std::max(60.0, callout.width - style.nodePadding * 2.0);

    ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
    ctx->SetFontSize(style.nameFontSize - 1.0);
    const std::string header = callout.headerText.empty() ? target->name : callout.headerText;
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

    std::vector<RequirementField> fields = callout.fields;
    if (fields.empty()) fields = {RequirementField::Id, RequirementField::Text};

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
        {RequirementField::DocRef, "docRef"},
        {RequirementField::ExternalId, "externalId"}
    };

    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(style.baseFontSize);
    for (RequirementField field : fields) {
        const std::string value = RequirementModel::ResolveField(*target, field);
        if (value.empty()) continue;
        auto keyIt = kFieldKeys.find(field);
        const std::string key = keyIt == kFieldKeys.end() ? "value" : keyIt->second;
        const std::string full =
            RequirementModel::FormatPropertyRow(key, value, nodeTemplate.format);
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
    for (const auto& id : model.GetNodeOrder()) {
        RequirementNode* node = model.GetNode(id);
        if (!node) continue;
        MeasuredNode measured;
        MeasureNode(ctx, *node, measured);
        measuredNodes[id] = std::move(measured);
    }
    measuredCallouts.clear();
    for (auto& callout : model.GetCallouts()) {
        MeasuredNode measured;
        MeasureCallout(ctx, callout, measured);
        measuredCallouts[callout.id] = std::move(measured);
    }
    measurementDirty = false;
    routingGeneration++;
}

void UltraCanvasRequirementDiagram::EstimateNodeSize(RequirementNode& node) const {
    // Heuristic used only before the first Render(), so RunLayout() called
    // during construction still produces usable positions. Arial averages
    // ~0.55 of the font size per glyph; bold ~0.62.
    if (!(node.hasExplicitWidth && node.width > 0.0)) {
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
            const std::string value =
                RequirementModel::ResolveField(node, row.field, row.customKey, row.literal);
            const std::string full =
                RequirementModel::FormatPropertyRow(row.key, value, format);
            const double approxWidth =
                static_cast<double>(full.size()) * style.baseFontSize * 0.55;
            int lines = static_cast<int>(std::ceil(approxWidth / std::max(1.0, contentWidth)));
            lines = std::max(1, std::min(lines, style.maxTextLines));
            bodyHeight += static_cast<double>(lines) *
                          (style.baseFontSize * 1.35 + style.rowSpacing);
        }
        height += style.headerGap * 2.0 + bodyHeight;
    }
    node.height = std::max(MIN_NODE_HEIGHT, height);
}

} // namespace UltraCanvas
