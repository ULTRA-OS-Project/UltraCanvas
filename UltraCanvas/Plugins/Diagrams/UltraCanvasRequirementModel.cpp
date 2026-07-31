// Plugins/Diagrams/UltraCanvasRequirementModel.cpp
// SysML requirement model implementation
// Version: 1.1.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework
//
// No UI, no rendering, no platform code - see the header for the rationale.

#include "Plugins/Diagrams/UltraCanvasRequirementModel.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace UltraCanvas {

// =============================================================================
// SMALL HELPERS
// =============================================================================

static std::string ToLowerCopy(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

static std::string Trim(const std::string& s) {
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(s[begin]))) begin++;
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
    return s.substr(begin, end - begin);
}

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

RequirementRisk RequirementRiskFromString(const std::string& text) {
    const std::string t = ToLowerCopy(Trim(text));
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
    const std::string t = ToLowerCopy(Trim(text));
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
    const std::string t = ToLowerCopy(Trim(text));
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
    const std::string t = ToLowerCopy(Trim(text));
    if (t == "containment" || t == "contains")  return RequirementRelationKind::Containment;
    if (t == "derivereqt"  || t == "derives")   return RequirementRelationKind::DeriveReqt;
    if (t == "satisfy"     || t == "satisfies") return RequirementRelationKind::Satisfy;
    if (t == "verify"      || t == "verifies")  return RequirementRelationKind::Verify;
    if (t == "refine"      || t == "refines")   return RequirementRelationKind::Refine;
    if (t == "copy"        || t == "copies")    return RequirementRelationKind::Copy;
    if (t == "generalization" || t == "generalizes") return RequirementRelationKind::Generalization;
    return RequirementRelationKind::Trace;
}

const char* RequirementDerivedListToString(RequirementDerivedList list) {
    switch (list) {
        case RequirementDerivedList::Derived:     return "derived";
        case RequirementDerivedList::DerivedFrom: return "derivedFrom";
        case RequirementDerivedList::SatisfiedBy: return "satisfiedBy";
        case RequirementDerivedList::VerifiedBy:  return "verifiedBy";
        case RequirementDerivedList::RefinedBy:   return "refinedBy";
        case RequirementDerivedList::TracedTo:    return "tracedTo";
        case RequirementDerivedList::Master:      return "master";
    }
    return "";
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

RequirementNodeTemplate RequirementNodeTemplate::WithTraceCompartments() {
    RequirementNodeTemplate tpl = Standard();
    tpl.AddDerivedCompartment(RequirementDerivedList::SatisfiedBy);
    tpl.AddDerivedCompartment(RequirementDerivedList::VerifiedBy);
    return tpl;
}

// =============================================================================
// FIELD RESOLUTION
// =============================================================================

std::string RequirementModel::ResolveField(const RequirementNode& node,
                                            RequirementField field,
                                            const std::string& customKey,
                                            const std::string& literal) {
    switch (field) {
        case RequirementField::Id:         return node.DisplayId();
        case RequirementField::ExternalId: return node.externalId;
        case RequirementField::Name:       return node.name;
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

std::string RequirementModel::FormatPropertyRow(const std::string& key,
                                                 const std::string& value,
                                                 RequirementPropertyFormat format) {
    if (format == RequirementPropertyFormat::KeyColonValue) {
        return key + ": " + value;
    }
    return key + "=\"" + value + "\"";
}

// =============================================================================
// NODES
// =============================================================================

void RequirementModel::ReportWarning(const RequirementWarning& warning) const {
    if (onWarning) onWarning(warning);
}

bool RequirementModel::AddNode(const RequirementNode& node) {
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
    return true;
}

bool RequirementModel::AddNode(RequirementNodeKind kind, const std::string& id,
                                const std::string& name) {
    RequirementNode node(id, name);
    node.kind = kind;
    return AddNode(node);
}

bool RequirementModel::AddRequirement(const std::string& id, const std::string& name,
                                       const std::string& text) {
    RequirementNode node(id, name);
    node.kind = RequirementNodeKind::Requirement;
    node.text = text;
    return AddNode(node);
}

bool RequirementModel::AddRequirement(const std::string& id, const std::string& name,
                                       const std::string& text, RequirementRisk risk,
                                       RequirementVerifyMethod verifyMethod) {
    RequirementNode node(id, name);
    node.kind = RequirementNodeKind::Requirement;
    node.text = text;
    node.risk = risk;
    node.verifyMethod = verifyMethod;
    return AddNode(node);
}

bool RequirementModel::RemoveNode(const std::string& id) {
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
    for (auto& [nodeId, node] : nodes) {
        (void)nodeId;
        if (node.anchorId == id) node.anchorId.clear();
    }
    return true;
}

void RequirementModel::RewriteReferences(const std::string& oldId, const std::string& newId) {
    for (auto& relation : relations) {
        if (relation.sourceId == oldId) relation.sourceId = newId;
        if (relation.targetId == oldId) relation.targetId = newId;
    }
    for (auto& callout : callouts) {
        if (callout.targetNodeId == oldId) callout.targetNodeId = newId;
    }
    for (auto& [nodeId, node] : nodes) {
        (void)nodeId;
        if (node.anchorId == oldId) node.anchorId = newId;
    }
}

bool RequirementModel::RenameNode(const std::string& oldId, const std::string& newId) {
    if (oldId == newId) return true;
    if (newId.empty()) return false;
    auto it = nodes.find(oldId);
    if (it == nodes.end()) return false;
    if (nodes.find(newId) != nodes.end()) {
        RequirementWarning w;
        w.kind = RequirementWarning::Kind::DuplicateNodeId;
        w.nodeId = newId;
        w.message = "Cannot rename '" + oldId + "': id '" + newId + "' is taken";
        ReportWarning(w);
        return false;
    }

    RequirementNode node = it->second;
    node.id = newId;
    nodes.erase(it);
    nodes[newId] = node;

    auto orderIt = std::find(nodeOrder.begin(), nodeOrder.end(), oldId);
    if (orderIt != nodeOrder.end()) *orderIt = newId;

    RewriteReferences(oldId, newId);
    return true;
}

void RequirementModel::Clear() {
    nodes.clear();
    nodeOrder.clear();
    relations.clear();
    callouts.clear();
    categories.clear();
    categoryOrder.clear();
    nextNodeSerial = 1;
    nextRelationSerial = 1;
}

RequirementNode* RequirementModel::GetNode(const std::string& id) {
    auto it = nodes.find(id);
    return it == nodes.end() ? nullptr : &it->second;
}

const RequirementNode* RequirementModel::GetNode(const std::string& id) const {
    auto it = nodes.find(id);
    return it == nodes.end() ? nullptr : &it->second;
}

bool RequirementModel::HasNode(const std::string& id) const {
    return nodes.find(id) != nodes.end();
}

std::string RequirementModel::GenerateNodeId(const std::string& prefix) {
    std::string candidate;
    do {
        candidate = prefix + std::to_string(nextNodeSerial++);
    } while (nodes.find(candidate) != nodes.end());
    return candidate;
}

// =============================================================================
// ENDPOINT LEGALITY
// =============================================================================

bool RequirementModel::IsLegalEndpointPair(RequirementRelationKind kind,
                                            RequirementNodeKind sourceKind,
                                            RequirementNodeKind targetKind) {
    const bool sourceIsRequirement = (sourceKind == RequirementNodeKind::Requirement);
    const bool targetIsRequirement = (targetKind == RequirementNodeKind::Requirement);
    const bool sourceIsNote = (sourceKind == RequirementNodeKind::Rationale ||
                               sourceKind == RequirementNodeKind::Problem ||
                               sourceKind == RequirementNodeKind::Note);
    const bool targetIsNote = (targetKind == RequirementNodeKind::Rationale ||
                               targetKind == RequirementNodeKind::Problem ||
                               targetKind == RequirementNodeKind::Note);

    switch (kind) {
        case RequirementRelationKind::Containment:
            // A namespace owns its members: requirements, packages and blocks
            // can own; notes cannot own or be owned.
            return !sourceIsNote && !targetIsNote &&
                   (sourceKind == RequirementNodeKind::Requirement ||
                    sourceKind == RequirementNodeKind::Package ||
                    sourceKind == RequirementNodeKind::Block);

        case RequirementRelationKind::DeriveReqt:
        case RequirementRelationKind::Copy:
            // Requirement to requirement only.
            return sourceIsRequirement && targetIsRequirement;

        case RequirementRelationKind::Satisfy:
            // A design element satisfies a requirement - not another
            // requirement, and not a note.
            return !sourceIsRequirement && !sourceIsNote && targetIsRequirement;

        case RequirementRelationKind::Verify:
            // Only a test case verifies.
            return sourceKind == RequirementNodeKind::TestCase && targetIsRequirement;

        case RequirementRelationKind::Refine:
            // Any non-requirement model element may refine a requirement.
            return !sourceIsRequirement && !sourceIsNote && targetIsRequirement;

        case RequirementRelationKind::Trace:
            // Deliberately unconstrained - that is what makes it ambiguous.
            return true;

        case RequirementRelationKind::Generalization:
            // Specialisation stays within one kind.
            return sourceKind == targetKind;
    }
    return true;
}

// =============================================================================
// RELATIONSHIPS
// =============================================================================

std::string RequirementModel::AddRelation(const RequirementRelation& relation) {
    RequirementRelation rel = relation;

    const RequirementNode* source = GetNode(rel.sourceId);
    const RequirementNode* target = GetNode(rel.targetId);
    if (!source || !target) {
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

    // Endpoint legality. When the pair is illegal but the *reversed* pair is
    // legal, the author almost certainly wrote the arrow backwards - flip it
    // and say so, rather than rejecting a diagram that is one keystroke from
    // correct.
    if (!IsLegalEndpointPair(rel.kind, source->kind, target->kind)) {
        if (IsLegalEndpointPair(rel.kind, target->kind, source->kind)) {
            std::swap(rel.sourceId, rel.targetId);
            RequirementWarning w;
            w.kind = RequirementWarning::Kind::DirectionCorrected;
            w.relationId = rel.id;
            w.message = std::string("«") + RequirementRelationKindToString(rel.kind) +
                        "» " + rel.targetId + " -> " + rel.sourceId +
                        " was backwards; flipped to " + rel.sourceId + " -> " + rel.targetId;
            ReportWarning(w);
        } else {
            RequirementWarning w;
            w.kind = RequirementWarning::Kind::IllegalEndpointKind;
            w.relationId = rel.id;
            w.message = std::string("«") + RequirementRelationKindToString(rel.kind) +
                        "» is not legal from «" + RequirementNodeKindToString(source->kind) +
                        "» to «" + RequirementNodeKindToString(target->kind) + "»";
            ReportWarning(w);
            if (semanticsMode == RequirementSemanticsMode::Strict) return "";
            rel.illegal = true;
        }
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
    return rel.id;
}

std::string RequirementModel::AddRelation(RequirementRelationKind kind,
                                           const std::string& sourceId,
                                           const std::string& targetId) {
    return AddRelation(RequirementRelation(kind, sourceId, targetId));
}

std::string RequirementModel::AddContainment(const std::string& parentId,
                                              const std::string& childId) {
    return AddRelation(RequirementRelation(RequirementRelationKind::Containment,
                                            parentId, childId));
}

bool RequirementModel::RemoveRelation(const std::string& id) {
    auto it = std::find_if(relations.begin(), relations.end(),
                           [&id](const RequirementRelation& r) { return r.id == id; });
    if (it == relations.end()) return false;
    relations.erase(it);
    return true;
}

RequirementRelation* RequirementModel::GetRelation(const std::string& id) {
    for (auto& r : relations) {
        if (r.id == id) return &r;
    }
    return nullptr;
}

const RequirementRelation* RequirementModel::GetRelation(const std::string& id) const {
    for (const auto& r : relations) {
        if (r.id == id) return &r;
    }
    return nullptr;
}

std::vector<std::string> RequirementModel::GetAllRelationIds() const {
    std::vector<std::string> ids;
    ids.reserve(relations.size());
    for (const auto& r : relations) ids.push_back(r.id);
    return ids;
}

// =============================================================================
// CALLOUTS & CATEGORIES
// =============================================================================

bool RequirementModel::AddCallout(const RequirementCallout& callout) {
    if (callout.id.empty()) return false;
    if (!HasNode(callout.targetNodeId)) {
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
    return true;
}

bool RequirementModel::RemoveCallout(const std::string& id) {
    auto it = std::find_if(callouts.begin(), callouts.end(),
                           [&id](const RequirementCallout& c) { return c.id == id; });
    if (it == callouts.end()) return false;
    callouts.erase(it);
    return true;
}

RequirementCallout* RequirementModel::GetCallout(const std::string& id) {
    for (auto& c : callouts) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

void RequirementModel::AddCategory(const RequirementCategory& category) {
    if (category.name.empty()) return;
    if (categories.find(category.name) == categories.end()) {
        categoryOrder.push_back(category.name);
    }
    categories[category.name] = category;
}

const RequirementCategory* RequirementModel::GetCategory(const std::string& name) const {
    auto it = categories.find(name);
    return it == categories.end() ? nullptr : &it->second;
}

// =============================================================================
// STRUCTURAL QUERIES
// =============================================================================

std::string RequirementModel::GetParentId(const std::string& nodeId) const {
    for (const auto& r : relations) {
        if (r.kind == RequirementRelationKind::Containment && r.targetId == nodeId) {
            return r.sourceId;
        }
    }
    return "";
}

std::vector<std::string> RequirementModel::GetChildIds(const std::string& nodeId) const {
    std::vector<std::string> children;
    for (const auto& r : relations) {
        if (r.kind == RequirementRelationKind::Containment && r.sourceId == nodeId) {
            children.push_back(r.targetId);
        }
    }
    return children;
}

std::vector<std::string> RequirementModel::GetRootIds() const {
    std::vector<std::string> roots;
    for (const auto& id : nodeOrder) {
        if (GetParentId(id).empty()) roots.push_back(id);
    }
    return roots;
}

std::vector<std::string> RequirementModel::GetRelationsOf(const std::string& nodeId) const {
    std::vector<std::string> ids;
    for (const auto& r : relations) {
        if (r.sourceId == nodeId || r.targetId == nodeId) ids.push_back(r.id);
    }
    return ids;
}

std::vector<std::string> RequirementModel::GetDescendants(const std::string& nodeId) const {
    std::vector<std::string> result;
    std::set<std::string> seen;
    std::vector<std::string> stack = GetChildIds(nodeId);
    while (!stack.empty()) {
        const std::string current = stack.back();
        stack.pop_back();
        if (!seen.insert(current).second) continue;   // cycle guard
        result.push_back(current);
        for (const auto& child : GetChildIds(current)) stack.push_back(child);
    }
    return result;
}

std::vector<std::string> RequirementModel::GetDerivedElements(
        const std::string& nodeId, RequirementDerivedList list) const {
    std::vector<std::string> result;

    // Each compartment heading corresponds to one relationship kind read in
    // one direction. Incoming = relations whose TARGET is this node.
    RequirementRelationKind kind = RequirementRelationKind::Trace;
    bool incoming = true;
    switch (list) {
        case RequirementDerivedList::Derived:
            kind = RequirementRelationKind::DeriveReqt; incoming = true;  break;
        case RequirementDerivedList::DerivedFrom:
            kind = RequirementRelationKind::DeriveReqt; incoming = false; break;
        case RequirementDerivedList::SatisfiedBy:
            kind = RequirementRelationKind::Satisfy;    incoming = true;  break;
        case RequirementDerivedList::VerifiedBy:
            kind = RequirementRelationKind::Verify;     incoming = true;  break;
        case RequirementDerivedList::RefinedBy:
            kind = RequirementRelationKind::Refine;     incoming = true;  break;
        case RequirementDerivedList::TracedTo:
            kind = RequirementRelationKind::Trace;      incoming = false; break;
        case RequirementDerivedList::Master:
            kind = RequirementRelationKind::Copy;       incoming = false; break;
    }

    for (const auto& r : relations) {
        if (r.kind != kind) continue;
        const std::string& near = incoming ? r.targetId : r.sourceId;
        const std::string& far  = incoming ? r.sourceId : r.targetId;
        if (near != nodeId) continue;
        if (std::find(result.begin(), result.end(), far) == result.end()) {
            result.push_back(far);
        }
    }
    return result;
}

// =============================================================================
// INTEGRITY
// =============================================================================

std::vector<std::string> RequirementModel::GetContainmentCycleNodes() const {
    // Iterative colouring DFS over the containment edges. Any node still grey
    // when reached again is part of a cycle.
    enum class Mark { White, Grey, Black };
    std::map<std::string, Mark> mark;
    for (const auto& id : nodeOrder) mark[id] = Mark::White;

    std::vector<std::string> cycleNodes;
    std::set<std::string> cycleSet;

    for (const auto& startId : nodeOrder) {
        if (mark[startId] != Mark::White) continue;

        std::vector<std::pair<std::string, size_t>> stack;   // (node, next child index)
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

bool RequirementModel::HasContainmentCycle() const {
    return !GetContainmentCycleNodes().empty();
}

std::vector<RequirementWarning> RequirementModel::Validate() const {
    std::vector<RequirementWarning> warnings;

    for (const auto& r : relations) {
        const RequirementNode* source = GetNode(r.sourceId);
        const RequirementNode* target = GetNode(r.targetId);
        if (!source || !target) {
            RequirementWarning w;
            w.kind = RequirementWarning::Kind::UnknownEndpoint;
            w.relationId = r.id;
            w.message = "Relation '" + r.id + "' references an unknown node";
            warnings.push_back(w);
            continue;
        }
        if (!IsLegalEndpointPair(r.kind, source->kind, target->kind)) {
            RequirementWarning w;
            w.kind = RequirementWarning::Kind::IllegalEndpointKind;
            w.relationId = r.id;
            w.message = std::string("«") + RequirementRelationKindToString(r.kind) +
                        "» is not legal from «" + RequirementNodeKindToString(source->kind) +
                        "» to «" + RequirementNodeKindToString(target->kind) + "»";
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

void RequirementModel::CollectHierarchicalIds(
        const std::string& nodeId, const std::string& assignedId,
        std::vector<std::pair<std::string, std::string>>& out,
        std::set<std::string>& visited) const {
    if (!visited.insert(nodeId).second) return;   // cycle guard
    out.emplace_back(nodeId, assignedId);

    const std::vector<std::string> children = GetChildIds(nodeId);
    for (size_t i = 0; i < children.size(); ++i) {
        CollectHierarchicalIds(children[i], assignedId + "." + std::to_string(i + 1),
                               out, visited);
    }
}

int RequirementModel::AssignHierarchicalIds(const std::string& prefix) {
    if (HasContainmentCycle()) {
        RequirementWarning w;
        w.kind = RequirementWarning::Kind::ContainmentCycle;
        w.message = "Cannot assign hierarchical ids: the containment graph is cyclic";
        ReportWarning(w);
        return 0;
    }

    // Compute the whole mapping first: renaming as we walk would invalidate
    // the traversal, and a target id may collide with an id still in use.
    std::vector<std::pair<std::string, std::string>> mapping;
    std::set<std::string> visited;
    int rootIndex = 0;
    for (const auto& rootId : GetRootIds()) {
        // Only roots that actually own something get a hierarchy; a
        // free-standing test case keeps its own id.
        if (GetChildIds(rootId).empty() && GetParentId(rootId).empty()) {
            const RequirementNode* node = GetNode(rootId);
            if (node && node->kind != RequirementNodeKind::Requirement) continue;
        }
        CollectHierarchicalIds(rootId, prefix + std::to_string(++rootIndex),
                               mapping, visited);
    }

    // Two-phase rename through temporary keys, so a new id that is still held
    // by another node does not collide.
    int renamed = 0;
    std::vector<std::pair<std::string, std::string>> pending;
    for (size_t i = 0; i < mapping.size(); ++i) {
        const std::string& from = mapping[i].first;
        const std::string& to = mapping[i].second;
        if (from == to) continue;
        const std::string temporary = "\x01tmp" + std::to_string(i);
        if (RenameNode(from, temporary)) pending.emplace_back(temporary, to);
    }
    for (const auto& [temporary, to] : pending) {
        if (RenameNode(temporary, to)) {
            renamed++;
        } else {
            // Target still taken (a node outside the tree owns it): keep the
            // original id rather than leaving a temporary key in the model.
            for (const auto& [from, target] : mapping) {
                if (target == to) { RenameNode(temporary, from); break; }
            }
        }
    }

    // The authored id shown in the compartment follows the model key.
    for (const auto& [from, to] : mapping) {
        (void)from;
        if (RequirementNode* node = GetNode(to)) node->externalId.clear();
    }
    return renamed;
}

// =============================================================================
// TRACEABILITY ANALYSIS
// =============================================================================

std::vector<std::string> RequirementModel::GetUncoveredRequirements() const {
    std::vector<std::string> result;
    for (const auto& id : nodeOrder) {
        const RequirementNode* node = GetNode(id);
        if (!node || node->kind != RequirementNodeKind::Requirement) continue;
        bool satisfied = false;
        for (const auto& r : relations) {
            if (r.kind == RequirementRelationKind::Satisfy && r.targetId == id) {
                satisfied = true;
                break;
            }
        }
        if (!satisfied) result.push_back(id);
    }
    return result;
}

std::vector<std::string> RequirementModel::GetUnverifiedRequirements() const {
    std::vector<std::string> result;
    for (const auto& id : nodeOrder) {
        const RequirementNode* node = GetNode(id);
        if (!node || node->kind != RequirementNodeKind::Requirement) continue;
        bool verified = false;
        for (const auto& r : relations) {
            if (r.kind == RequirementRelationKind::Verify && r.targetId == id) {
                verified = true;
                break;
            }
        }
        if (!verified) result.push_back(id);
    }
    return result;
}

std::vector<std::string> RequirementModel::GetOrphanRequirements() const {
    std::vector<std::string> result;
    for (const auto& id : nodeOrder) {
        const RequirementNode* node = GetNode(id);
        if (!node || node->kind != RequirementNodeKind::Requirement) continue;
        if (GetRelationsOf(id).empty()) result.push_back(id);
    }
    return result;
}

RequirementCoverage RequirementModel::GetCoverageForCategory(
        const std::string& category) const {
    RequirementCoverage coverage;
    const bool allCategories = category.empty();

    for (const auto& id : nodeOrder) {
        const RequirementNode* node = GetNode(id);
        if (!node || node->kind != RequirementNodeKind::Requirement) continue;
        if (!allCategories && node->category != category) continue;

        coverage.requirementCount++;
        bool satisfied = false, verified = false, derived = false, any = false;
        for (const auto& r : relations) {
            const bool touches = (r.sourceId == id || r.targetId == id);
            if (touches) any = true;
            if (r.targetId != id) continue;
            if (r.kind == RequirementRelationKind::Satisfy)    satisfied = true;
            if (r.kind == RequirementRelationKind::Verify)     verified = true;
            if (r.kind == RequirementRelationKind::DeriveReqt) derived = true;
        }
        if (satisfied) coverage.satisfiedCount++;
        if (verified)  coverage.verifiedCount++;
        if (derived)   coverage.derivedCount++;
        if (!any)      coverage.orphanCount++;
    }

    if (coverage.requirementCount > 0) {
        const double total = static_cast<double>(coverage.requirementCount);
        coverage.satisfiedPercent = 100.0 * coverage.satisfiedCount / total;
        coverage.verifiedPercent  = 100.0 * coverage.verifiedCount / total;
        coverage.derivedPercent   = 100.0 * coverage.derivedCount / total;
    }
    return coverage;
}

RequirementCoverage RequirementModel::GetCoverage() const {
    return GetCoverageForCategory("");
}

std::set<std::string> RequirementModel::GetTraceChain(const std::string& nodeId,
                                                       RequirementTraceDirection direction,
                                                       int depth) const {
    std::set<std::string> reached;
    if (!HasNode(nodeId)) return reached;

    reached.insert(nodeId);
    std::vector<std::string> frontier = {nodeId};
    int level = 0;

    while (!frontier.empty() && (depth <= 0 || level < depth)) {
        std::vector<std::string> next;
        for (const auto& current : frontier) {
            for (const auto& r : relations) {
                // Upstream = follow the arrow (this element depends on the
                // target); downstream = follow it backwards.
                if ((direction == RequirementTraceDirection::Upstream ||
                     direction == RequirementTraceDirection::Both) &&
                    r.sourceId == current && reached.insert(r.targetId).second) {
                    next.push_back(r.targetId);
                }
                if ((direction == RequirementTraceDirection::Downstream ||
                     direction == RequirementTraceDirection::Both) &&
                    r.targetId == current && reached.insert(r.sourceId).second) {
                    next.push_back(r.sourceId);
                }
            }
        }
        frontier.swap(next);
        level++;
    }
    return reached;
}

// =============================================================================
// MERMAID INTERCHANGE
// =============================================================================

// Mermaid's requirement types. Anything else is written as a plain
// `requirement`, with the original stereotype preserved only in our own JSON.
static const char* MermaidTypeForNode(const RequirementNode& node) {
    if (node.kind != RequirementNodeKind::Requirement) return "element";
    const std::string stereotype = ToLowerCopy(node.stereotype);
    if (stereotype == "functionalrequirement")  return "functionalRequirement";
    if (stereotype == "interfacerequirement")   return "interfaceRequirement";
    if (stereotype == "performancerequirement") return "performanceRequirement";
    if (stereotype == "physicalrequirement")    return "physicalRequirement";
    if (stereotype == "designconstraint")       return "designConstraint";
    return "requirement";
}

static const char* MermaidRelationKeyword(RequirementRelationKind kind) {
    switch (kind) {
        case RequirementRelationKind::Containment: return "contains";
        case RequirementRelationKind::Copy:        return "copies";
        case RequirementRelationKind::DeriveReqt:  return "derives";
        case RequirementRelationKind::Satisfy:     return "satisfies";
        case RequirementRelationKind::Verify:      return "verifies";
        case RequirementRelationKind::Refine:      return "refines";
        case RequirementRelationKind::Trace:       return "traces";
        // Mermaid has no generalisation; the closest honest mapping is a trace.
        case RequirementRelationKind::Generalization: return "traces";
    }
    return "traces";
}

// Mermaid identifiers cannot contain spaces or the block punctuation.
static std::string MermaidSanitizeId(const std::string& id) {
    std::string out;
    out.reserve(id.size());
    for (char c : id) {
        out += (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' ||
                c == '-') ? c : '_';
    }
    if (out.empty()) out = "n";
    if (std::isdigit(static_cast<unsigned char>(out[0]))) out.insert(out.begin(), 'n');
    return out;
}

std::string RequirementModel::ToMermaid(const std::string& direction) const {
    std::ostringstream out;
    out << "requirementDiagram\n";
    if (!direction.empty()) out << "\ndirection " << direction << "\n";

    for (const auto& id : nodeOrder) {
        const RequirementNode* node = GetNode(id);
        if (!node) continue;

        const std::string mermaidId = MermaidSanitizeId(node->id);
        out << "\n" << MermaidTypeForNode(*node) << " " << mermaidId << " {\n";

        if (node->kind == RequirementNodeKind::Requirement) {
            out << "    id: " << node->DisplayId() << "\n";
            out << "    text: " << (node->text.empty() ? node->name : node->text) << "\n";
            if (node->risk != RequirementRisk::Unspecified) {
                out << "    risk: " << ToLowerCopy(RequirementRiskToString(node->risk)) << "\n";
            }
            if (node->verifyMethod != RequirementVerifyMethod::Unspecified) {
                out << "    verifymethod: "
                    << ToLowerCopy(RequirementVerifyMethodToString(node->verifyMethod)) << "\n";
            }
        } else {
            out << "    type: " << RequirementNodeKindToString(node->kind) << "\n";
            if (!node->docRef.empty()) out << "    docref: " << node->docRef << "\n";
        }
        out << "}\n";
    }

    if (!relations.empty()) out << "\n";
    for (const auto& r : relations) {
        out << MermaidSanitizeId(r.sourceId) << " - " << MermaidRelationKeyword(r.kind)
            << " -> " << MermaidSanitizeId(r.targetId) << "\n";
    }
    return out.str();
}

bool RequirementModel::FromMermaid(const std::string& text, std::string* outError,
                                    std::string* outDirection) {
    auto fail = [&outError](const std::string& message) {
        if (outError) *outError = message;
        return false;
    };

    // ---- tokenise into logical lines, dropping %% comments ----
    std::vector<std::string> lines;
    {
        std::istringstream in(text);
        std::string raw;
        while (std::getline(in, raw)) {
            const size_t comment = raw.find("%%");
            if (comment != std::string::npos) raw = raw.substr(0, comment);
            const std::string trimmed = Trim(raw);
            if (!trimmed.empty()) lines.push_back(trimmed);
        }
    }
    if (lines.empty()) return fail("empty input");

    size_t index = 0;
    if (ToLowerCopy(lines[0]).rfind("requirementdiagram", 0) != 0) {
        return fail("expected 'requirementDiagram' on the first line");
    }
    index = 1;

    Clear();
    // Relations are collected first and added last: Mermaid allows a
    // relationship to name a requirement declared further down the file.
    struct PendingRelation { RequirementRelationKind kind; std::string source, target; };
    std::vector<PendingRelation> pending;
    std::set<std::string> declared;

    auto ensureNode = [this, &declared](const std::string& id) {
        if (declared.insert(id).second && !HasNode(id)) {
            // Referenced but never declared: Mermaid renders it as a bare
            // requirement box, so we do the same instead of dropping the edge.
            RequirementNode node(id, id);
            node.kind = RequirementNodeKind::Requirement;
            AddNode(node);
        }
    };

    for (; index < lines.size(); ++index) {
        const std::string& line = lines[index];
        const std::string lower = ToLowerCopy(line);

        // ---- direction directive ----
        if (lower.rfind("direction", 0) == 0) {
            if (outDirection) *outDirection = Trim(line.substr(9));
            continue;
        }

        // ---- relationship: "a - kind -> b" or "b <- kind - a" ----
        const size_t forward = line.find("->");
        const size_t backward = line.find("<-");
        if (forward != std::string::npos || backward != std::string::npos) {
            std::string left, keyword, right;
            bool reversed = false;

            if (backward != std::string::npos && (forward == std::string::npos ||
                                                   backward < forward)) {
                // "target <- keyword - source"
                left = Trim(line.substr(0, backward));
                const std::string rest = line.substr(backward + 2);
                const size_t dash = rest.find('-');
                if (dash == std::string::npos) return fail("malformed relation: " + line);
                keyword = Trim(rest.substr(0, dash));
                right = Trim(rest.substr(dash + 1));
                reversed = true;
            } else {
                // "source - keyword -> target"
                const size_t dash = line.find('-');
                if (dash == std::string::npos || dash > forward) {
                    return fail("malformed relation: " + line);
                }
                left = Trim(line.substr(0, dash));
                keyword = Trim(line.substr(dash + 1, forward - dash - 1));
                right = Trim(line.substr(forward + 2));
            }
            if (left.empty() || right.empty() || keyword.empty()) {
                return fail("malformed relation: " + line);
            }

            PendingRelation relation;
            relation.kind = RequirementRelationKindFromString(keyword);
            relation.source = reversed ? right : left;
            relation.target = reversed ? left : right;
            pending.push_back(relation);
            continue;
        }

        // ---- declaration: "<type> <name> {" ----
        const size_t brace = line.find('{');
        if (brace == std::string::npos) continue;   // stray line: ignore

        const std::string head = Trim(line.substr(0, brace));
        const size_t space = head.find_last_of(" \t");
        if (space == std::string::npos) return fail("malformed declaration: " + line);

        const std::string typeWord = ToLowerCopy(Trim(head.substr(0, space)));
        const std::string name = Trim(head.substr(space + 1));
        if (name.empty()) return fail("declaration without a name: " + line);

        RequirementNode node(name, name);
        const bool isElement = (typeWord == "element");
        if (isElement) {
            node.kind = RequirementNodeKind::Block;   // refined by the type: property
        } else {
            node.kind = RequirementNodeKind::Requirement;
            if (typeWord != "requirement") node.stereotype = Trim(head.substr(0, space));
        }

        // ---- body until the closing brace ----
        bool closed = (line.find('}', brace) != std::string::npos);
        while (!closed && ++index < lines.size()) {
            const std::string& body = lines[index];
            if (body == "}") { closed = true; break; }

            const size_t colon = body.find(':');
            if (colon == std::string::npos) {
                if (body.find('}') != std::string::npos) closed = true;
                continue;
            }
            const std::string key = ToLowerCopy(Trim(body.substr(0, colon)));
            std::string value = Trim(body.substr(colon + 1));
            if (!value.empty() && value.back() == '}') {
                value = Trim(value.substr(0, value.size() - 1));
                closed = true;
            }
            // Mermaid allows the value to be quoted for markdown support.
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.size() - 2);
            }

            if (key == "id") {
                node.externalId = value;
            } else if (key == "text") {
                node.text = value;
            } else if (key == "risk") {
                node.risk = RequirementRiskFromString(value);
            } else if (key == "verifymethod" || key == "verifymethod:") {
                node.verifyMethod = RequirementVerifyMethodFromString(value);
            } else if (key == "type") {
                // element blocks carry a free-text type; map the ones we model.
                const RequirementNodeKind mapped = RequirementNodeKindFromString(value);
                node.kind = (mapped == RequirementNodeKind::Requirement)
                                ? RequirementNodeKind::Block
                                : mapped;
                node.stereotype = value;
            } else if (key == "docref") {
                node.docRef = value;
            } else {
                node.customProperties[Trim(body.substr(0, colon))] = value;
            }
        }
        if (!closed) return fail("unterminated block for '" + name + "'");

        if (!HasNode(node.id)) {
            AddNode(node);
            declared.insert(node.id);
        }
    }

    for (const auto& relation : pending) {
        ensureNode(relation.source);
        ensureNode(relation.target);
        AddRelation(relation.kind, relation.source, relation.target);
    }
    return true;
}

// =============================================================================
// CSV INTERCHANGE
// =============================================================================

std::vector<std::string> RequirementModel::ParseCsvLine(const std::string& line,
                                                         char delimiter) {
    std::vector<std::string> fields;
    std::string current;
    bool inQuotes = false;

    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {   // escaped quote
                    current += '"';
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                current += c;
            }
        } else if (c == '"') {
            inQuotes = true;
        } else if (c == delimiter) {
            fields.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    fields.push_back(current);
    return fields;
}

static std::string CsvQuote(const std::string& value, char delimiter) {
    const bool needsQuotes = value.find(delimiter) != std::string::npos ||
                             value.find('"') != std::string::npos ||
                             value.find('\n') != std::string::npos ||
                             value.find('\r') != std::string::npos;
    if (!needsQuotes) return value;

    std::string out = "\"";
    for (char c : value) {
        if (c == '"') out += '"';
        out += c;
    }
    out += '"';
    return out;
}

std::string RequirementModel::ToCsv(const RequirementCsvSchema& schema) const {
    std::ostringstream out;
    const char d = schema.delimiter;

    // Column order matches the schema field order so a round trip is stable.
    const std::vector<std::string> header = {
        schema.idColumn, schema.nameColumn, schema.parentColumn, schema.kindColumn,
        schema.stereotypeColumn, schema.textColumn, schema.sourceColumn,
        schema.riskColumn, schema.verifyMethodColumn, schema.categoryColumn,
        schema.statusColumn, schema.ownerColumn
    };
    for (size_t i = 0; i < header.size(); ++i) {
        if (i) out << d;
        out << CsvQuote(header[i], d);
    }
    out << "\n";

    for (const auto& id : nodeOrder) {
        const RequirementNode* node = GetNode(id);
        if (!node) continue;
        const std::vector<std::string> row = {
            node->id,
            node->name,
            GetParentId(node->id),
            RequirementNodeKindToString(node->kind),
            node->stereotype,
            node->text,
            node->source,
            RequirementRiskToString(node->risk),
            RequirementVerifyMethodToString(node->verifyMethod),
            node->category,
            node->status,
            node->owner
        };
        for (size_t i = 0; i < row.size(); ++i) {
            if (i) out << d;
            out << CsvQuote(row[i], d);
        }
        out << "\n";
    }
    return out.str();
}

bool RequirementModel::FromCsv(const std::string& text, const RequirementCsvSchema& schema,
                                std::string* outError) {
    auto fail = [&outError](const std::string& message) {
        if (outError) *outError = message;
        return false;
    };

    // Split into records, keeping newlines that appear inside quoted fields.
    std::vector<std::string> records;
    {
        std::string current;
        bool inQuotes = false;
        for (size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (c == '"') {
                inQuotes = !inQuotes;
                current += c;
            } else if ((c == '\n' || c == '\r') && !inQuotes) {
                if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
                if (!Trim(current).empty()) records.push_back(current);
                current.clear();
            } else {
                current += c;
            }
        }
        if (!Trim(current).empty()) records.push_back(current);
    }
    if (records.empty()) return fail("empty input");

    // ---- header -> column index ----
    const std::vector<std::string> header = ParseCsvLine(records[0], schema.delimiter);
    std::map<std::string, int> column;
    for (size_t i = 0; i < header.size(); ++i) {
        column[ToLowerCopy(Trim(header[i]))] = static_cast<int>(i);
    }
    auto columnIndex = [&column](const std::string& name) {
        if (name.empty()) return -1;
        auto it = column.find(ToLowerCopy(Trim(name)));
        return it == column.end() ? -1 : it->second;
    };

    const int idCol = columnIndex(schema.idColumn);
    if (idCol < 0) return fail("required column '" + schema.idColumn + "' not found");

    const int nameCol   = columnIndex(schema.nameColumn);
    const int parentCol = columnIndex(schema.parentColumn);
    const int textCol   = columnIndex(schema.textColumn);
    const int sourceCol = columnIndex(schema.sourceColumn);
    const int riskCol   = columnIndex(schema.riskColumn);
    const int verifyCol = columnIndex(schema.verifyMethodColumn);
    const int kindCol   = columnIndex(schema.kindColumn);
    const int stereoCol = columnIndex(schema.stereotypeColumn);
    const int catCol    = columnIndex(schema.categoryColumn);
    const int statusCol = columnIndex(schema.statusColumn);
    const int ownerCol  = columnIndex(schema.ownerColumn);

    Clear();

    auto cell = [](const std::vector<std::string>& fields, int index) {
        return (index >= 0 && index < static_cast<int>(fields.size()))
                   ? Trim(fields[static_cast<size_t>(index)])
                   : std::string();
    };

    std::vector<std::pair<std::string, std::string>> parentage;   // (child, parent)
    for (size_t row = 1; row < records.size(); ++row) {
        const std::vector<std::string> fields = ParseCsvLine(records[row], schema.delimiter);
        const std::string id = cell(fields, idCol);
        if (id.empty()) continue;   // blank row

        RequirementNode node(id, cell(fields, nameCol));
        if (node.name.empty()) node.name = id;
        node.kind = kindCol >= 0 ? RequirementNodeKindFromString(cell(fields, kindCol))
                                 : RequirementNodeKind::Requirement;
        node.stereotype   = cell(fields, stereoCol);
        node.text         = cell(fields, textCol);
        node.source       = cell(fields, sourceCol);
        node.risk         = RequirementRiskFromString(cell(fields, riskCol));
        node.verifyMethod = RequirementVerifyMethodFromString(cell(fields, verifyCol));
        node.category     = cell(fields, catCol);
        node.status       = cell(fields, statusCol);
        node.owner        = cell(fields, ownerCol);
        AddNode(node);

        const std::string parent = cell(fields, parentCol);
        if (!parent.empty()) parentage.emplace_back(id, parent);
    }

    // Containment is wired after every row exists, so forward references work.
    for (const auto& [child, parent] : parentage) {
        if (HasNode(parent)) AddContainment(parent, child);
    }
    return true;
}

} // namespace UltraCanvas
