// Plugins/Diagrams/UltraCanvasUMLModel.cpp
// UML static-structure model implementation
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasUMLModel.h"
#include "Plugins/Diagrams/UltraCanvasUMLNotation.h"

#include <algorithm>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace UltraCanvas {

// =============================================================================
// MEMBER FACTORIES
// =============================================================================

UMLMember UMLMember::Attribute(UMLVisibility vis, const std::string& memberName,
                               const std::string& memberType,
                               const std::string& memberMultiplicity) {
    UMLMember m;
    m.kind = UMLMemberKind::Attribute;
    m.visibility = vis;
    m.name = memberName;
    m.type = memberType;
    m.multiplicity = memberMultiplicity;
    return m;
}

UMLMember UMLMember::Operation(UMLVisibility vis, const std::string& memberName,
                               const std::string& returnType,
                               const std::vector<UMLParameter>& params) {
    UMLMember m;
    m.kind = UMLMemberKind::Operation;
    m.visibility = vis;
    m.name = memberName;
    m.type = returnType;
    m.parameters = params;
    return m;
}

UMLMember UMLMember::Literal(const std::string& literalName,
                             const std::string& literalValue) {
    UMLMember m;
    m.kind = UMLMemberKind::Literal;
    m.visibility = UMLVisibility::Unspecified;
    m.name = literalName;
    m.defaultValue = literalValue;
    return m;
}

// =============================================================================
// CLASSIFIER
// =============================================================================

bool UMLClassifier::HasStereotype(const std::string& stereotype) const {
    return std::find(stereotypes.begin(), stereotypes.end(), stereotype) != stereotypes.end();
}

const UMLMember* UMLClassifier::FindAttribute(const std::string& attributeName) const {
    for (const auto& m : attributes) {
        if (m.name == attributeName) return &m;
    }
    return nullptr;
}

const UMLMember* UMLClassifier::FindOperation(const std::string& operationName) const {
    for (const auto& m : operations) {
        if (m.name == operationName) return &m;
    }
    return nullptr;
}

bool UMLClassifier::IsAbstractLike() const {
    return kind == UMLClassifierKind::AbstractClass || kind == UMLClassifierKind::Interface;
}

// =============================================================================
// RELATIONSHIP
// =============================================================================

bool UMLRelationship::IsDashed() const {
    return kind == UMLRelationshipKind::Realization ||
           kind == UMLRelationshipKind::Dependency;
}

UMLRelationshipHandle& UMLRelationshipHandle::SetName(const std::string& name,
                                                       UMLReadingDirection direction) {
    if (relationship) {
        relationship->name = name;
        relationship->nameDirection = direction;
    }
    return *this;
}

UMLRelationshipHandle& UMLRelationshipHandle::SetMultiplicity(
        const std::string& sourceMultiplicity, const std::string& targetMultiplicity) {
    if (relationship) {
        relationship->source.multiplicity = UMLNotation::NormalizeMultiplicity(sourceMultiplicity);
        relationship->target.multiplicity = UMLNotation::NormalizeMultiplicity(targetMultiplicity);
    }
    return *this;
}

UMLRelationshipHandle& UMLRelationshipHandle::SetRoles(const std::string& sourceRole,
                                                        const std::string& targetRole) {
    if (relationship) {
        relationship->source.roleName = sourceRole;
        relationship->target.roleName = targetRole;
    }
    return *this;
}

UMLRelationshipHandle& UMLRelationshipHandle::SetStereotype(const std::string& stereotype) {
    if (relationship) relationship->stereotype = stereotype;
    return *this;
}

UMLRelationshipHandle& UMLRelationshipHandle::SetNavigable(bool sourceNavigable,
                                                            bool targetNavigable) {
    if (relationship) {
        relationship->source.navigable = sourceNavigable;
        relationship->target.navigable = targetNavigable;
    }
    return *this;
}

UMLRelationshipHandle& UMLRelationshipHandle::SetConstraints(const std::string& sourceConstraint,
                                                              const std::string& targetConstraint) {
    if (relationship) {
        relationship->source.constraint = sourceConstraint;
        relationship->target.constraint = targetConstraint;
    }
    return *this;
}

UMLRelationshipHandle& UMLRelationshipHandle::SetAssociationClass(const std::string& classifierId) {
    if (relationship) relationship->associationClassId = classifierId;
    return *this;
}

// =============================================================================
// MODEL — IDS
// =============================================================================

namespace {

// Turns "std::vector<Item>" or "My Class" into a usable id token.
std::string SanitizeId(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_') {
            out.push_back(c);
        } else if (!out.empty() && out.back() != '_') {
            out.push_back('_');
        }
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    if (out.empty()) out = "classifier";
    return out;
}

} // namespace

std::string UltraCanvasUMLModel::MakeUniqueClassifierId(const std::string& preferred) const {
    std::string base = SanitizeId(preferred);
    if (!GetClassifier(base)) return base;
    for (int i = 2; i < 100000; ++i) {
        std::string candidate = base + "_" + std::to_string(i);
        if (!GetClassifier(candidate)) return candidate;
    }
    return base;
}

std::string UltraCanvasUMLModel::MakeUniqueRelationshipId(const std::string& preferred) const {
    std::string base = preferred.empty() ? std::string("rel") : SanitizeId(preferred);
    if (!GetRelationship(base)) return base;
    for (int i = 2; i < 1000000; ++i) {
        std::string candidate = base + "_" + std::to_string(i);
        if (!GetRelationship(candidate)) return candidate;
    }
    return base;
}

std::string UltraCanvasUMLModel::ResolveClassifierId(const std::string& idOrName) const {
    if (GetClassifier(idOrName)) return idOrName;
    if (const UMLClassifier* byName = FindClassifierByName(idOrName)) return byName->id;
    return idOrName;
}

// =============================================================================
// MODEL — CLASSIFIERS
// =============================================================================

std::string UltraCanvasUMLModel::AddClassifier(const UMLClassifier& classifier) {
    UMLClassifier copy = classifier;
    if (copy.id.empty()) {
        copy.id = MakeUniqueClassifierId(copy.name);
    }
    if (UMLClassifier* existing = GetClassifier(copy.id)) {
        *existing = copy;
        return copy.id;
    }
    classifiers.push_back(copy);
    return copy.id;
}

namespace {

void FillMembers(UMLClassifier& classifier,
                 const std::vector<std::string>& attributeText,
                 const std::vector<std::string>& operationText) {
    for (const auto& text : attributeText) {
        UMLMember member;
        if (UMLNotation::ParseMember(text, member)) {
            member.kind = UMLMemberKind::Attribute;
            classifier.attributes.push_back(member);
        }
    }
    for (const auto& text : operationText) {
        UMLMember member;
        if (UMLNotation::ParseMember(text, member)) {
            member.kind = UMLMemberKind::Operation;
            classifier.operations.push_back(member);
        }
    }
}

} // namespace

std::string UltraCanvasUMLModel::AddClass(const std::string& name,
                                          const std::vector<std::string>& attributes,
                                          const std::vector<std::string>& operations) {
    UMLClassifier c(MakeUniqueClassifierId(name), name, UMLClassifierKind::Class);
    FillMembers(c, attributes, operations);
    return AddClassifier(c);
}

std::string UltraCanvasUMLModel::AddAbstractClass(const std::string& name,
                                                  const std::vector<std::string>& attributes,
                                                  const std::vector<std::string>& operations) {
    UMLClassifier c(MakeUniqueClassifierId(name), name, UMLClassifierKind::AbstractClass);
    FillMembers(c, attributes, operations);
    return AddClassifier(c);
}

std::string UltraCanvasUMLModel::AddInterface(const std::string& name,
                                              const std::vector<std::string>& operations) {
    UMLClassifier c(MakeUniqueClassifierId(name), name, UMLClassifierKind::Interface);
    c.stereotypes.push_back("interface");
    FillMembers(c, {}, operations);
    for (auto& op : c.operations) op.isAbstract = true;
    return AddClassifier(c);
}

std::string UltraCanvasUMLModel::AddEnumeration(const std::string& name,
                                                const std::vector<std::string>& literals) {
    UMLClassifier c(MakeUniqueClassifierId(name), name, UMLClassifierKind::Enumeration);
    c.stereotypes.push_back("enumeration");
    for (const auto& literalText : literals) {
        c.literals.push_back(UMLNotation::ParseLiteral(literalText));
    }
    return AddClassifier(c);
}

UMLClassifier* UltraCanvasUMLModel::GetClassifier(const std::string& id) {
    for (auto& c : classifiers) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

const UMLClassifier* UltraCanvasUMLModel::GetClassifier(const std::string& id) const {
    for (const auto& c : classifiers) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

UMLClassifier* UltraCanvasUMLModel::FindClassifierByName(const std::string& name) {
    for (auto& c : classifiers) {
        if (c.name == name) return &c;
    }
    return nullptr;
}

const UMLClassifier* UltraCanvasUMLModel::FindClassifierByName(const std::string& name) const {
    for (const auto& c : classifiers) {
        if (c.name == name) return &c;
    }
    return nullptr;
}

bool UltraCanvasUMLModel::RemoveClassifier(const std::string& id) {
    auto it = std::find_if(classifiers.begin(), classifiers.end(),
                           [&](const UMLClassifier& c) { return c.id == id; });
    if (it == classifiers.end()) return false;
    classifiers.erase(it);

    relationships.erase(
        std::remove_if(relationships.begin(), relationships.end(),
                       [&](const UMLRelationship& r) {
                           return r.source.classifierId == id || r.target.classifierId == id;
                       }),
        relationships.end());
    return true;
}

// =============================================================================
// MODEL — RELATIONSHIPS
// =============================================================================

UMLRelationshipHandle UltraCanvasUMLModel::AddRelationship(const UMLRelationship& relationship) {
    UMLRelationship copy = relationship;
    if (copy.id.empty()) {
        copy.id = "rel_" + std::to_string(++relationshipCounter);
    }
    copy.id = MakeUniqueRelationshipId(copy.id);
    copy.source.classifierId = ResolveClassifierId(copy.source.classifierId);
    copy.target.classifierId = ResolveClassifierId(copy.target.classifierId);
    relationships.push_back(copy);
    return UMLRelationshipHandle(&relationships.back());
}

UMLRelationshipHandle UltraCanvasUMLModel::AddRelationship(UMLRelationshipKind kind,
                                                            const std::string& sourceIdOrName,
                                                            const std::string& targetIdOrName) {
    UMLRelationship r;
    r.kind = kind;
    r.source = UMLRelationshipEnd(sourceIdOrName);
    r.target = UMLRelationshipEnd(targetIdOrName);
    return AddRelationship(r);
}

UMLRelationshipHandle UltraCanvasUMLModel::AddAssociation(const std::string& source,
                                                           const std::string& target) {
    return AddRelationship(UMLRelationshipKind::Association, source, target);
}

UMLRelationshipHandle UltraCanvasUMLModel::AddDirectedAssociation(const std::string& source,
                                                                   const std::string& target) {
    auto handle = AddRelationship(UMLRelationshipKind::DirectedAssociation, source, target);
    handle.SetNavigable(false, true);
    return handle;
}

// The diamond sits at the *whole* end, which is the source of these two.
UMLRelationshipHandle UltraCanvasUMLModel::AddAggregation(const std::string& whole,
                                                           const std::string& part) {
    return AddRelationship(UMLRelationshipKind::Aggregation, whole, part);
}

UMLRelationshipHandle UltraCanvasUMLModel::AddComposition(const std::string& whole,
                                                           const std::string& part) {
    return AddRelationship(UMLRelationshipKind::Composition, whole, part);
}

// The triangle sits at the *parent*, which is the target of these two.
UMLRelationshipHandle UltraCanvasUMLModel::AddGeneralization(const std::string& child,
                                                              const std::string& parent) {
    return AddRelationship(UMLRelationshipKind::Generalization, child, parent);
}

UMLRelationshipHandle UltraCanvasUMLModel::AddRealization(const std::string& implementer,
                                                           const std::string& interfaceName) {
    return AddRelationship(UMLRelationshipKind::Realization, implementer, interfaceName);
}

UMLRelationshipHandle UltraCanvasUMLModel::AddDependency(const std::string& client,
                                                          const std::string& supplier,
                                                          const std::string& stereotype) {
    auto handle = AddRelationship(UMLRelationshipKind::Dependency, client, supplier);
    if (!stereotype.empty()) handle.SetStereotype(stereotype);
    return handle;
}

UMLRelationship* UltraCanvasUMLModel::GetRelationship(const std::string& id) {
    for (auto& r : relationships) {
        if (r.id == id) return &r;
    }
    return nullptr;
}

const UMLRelationship* UltraCanvasUMLModel::GetRelationship(const std::string& id) const {
    for (const auto& r : relationships) {
        if (r.id == id) return &r;
    }
    return nullptr;
}

bool UltraCanvasUMLModel::RemoveRelationship(const std::string& id) {
    auto it = std::find_if(relationships.begin(), relationships.end(),
                           [&](const UMLRelationship& r) { return r.id == id; });
    if (it == relationships.end()) return false;
    relationships.erase(it);
    return true;
}

bool UltraCanvasUMLModel::HasRelationship(UMLRelationshipKind kind,
                                          const std::string& sourceId,
                                          const std::string& targetId) const {
    std::string s = ResolveClassifierId(sourceId);
    std::string t = ResolveClassifierId(targetId);
    for (const auto& r : relationships) {
        if (r.kind == kind && r.source.classifierId == s && r.target.classifierId == t) {
            return true;
        }
    }
    return false;
}

// =============================================================================
// MODEL — QUERIES
// =============================================================================

std::vector<std::string> UltraCanvasUMLModel::GetParents(const std::string& classifierId) const {
    std::string id = ResolveClassifierId(classifierId);
    std::vector<std::string> parents;
    for (const auto& r : relationships) {
        if (r.kind == UMLRelationshipKind::Generalization && r.source.classifierId == id) {
            parents.push_back(r.target.classifierId);
        }
    }
    return parents;
}

std::vector<std::string> UltraCanvasUMLModel::GetChildren(const std::string& classifierId) const {
    std::string id = ResolveClassifierId(classifierId);
    std::vector<std::string> children;
    for (const auto& r : relationships) {
        if (r.kind == UMLRelationshipKind::Generalization && r.target.classifierId == id) {
            children.push_back(r.source.classifierId);
        }
    }
    return children;
}

std::vector<std::string> UltraCanvasUMLModel::GetRootClassifiers() const {
    std::unordered_set<std::string> hasParent;
    for (const auto& r : relationships) {
        if (r.kind == UMLRelationshipKind::Generalization) {
            hasParent.insert(r.source.classifierId);
        }
    }
    std::vector<std::string> roots;
    for (const auto& c : classifiers) {
        if (hasParent.find(c.id) == hasParent.end()) roots.push_back(c.id);
    }
    return roots;
}

std::vector<const UMLRelationship*> UltraCanvasUMLModel::GetRelationshipsFor(
        const std::string& classifierId) const {
    std::string id = ResolveClassifierId(classifierId);
    std::vector<const UMLRelationship*> result;
    for (const auto& r : relationships) {
        if (r.source.classifierId == id || r.target.classifierId == id) {
            result.push_back(&r);
        }
    }
    return result;
}

// =============================================================================
// MODEL — WHOLE-MODEL OPERATIONS
// =============================================================================

void UltraCanvasUMLModel::Clear() {
    title.clear();
    classifiers.clear();
    relationships.clear();
    relationshipCounter = 0;
}

void UltraCanvasUMLModel::Merge(const UltraCanvasUMLModel& other) {
    for (const auto& c : other.classifiers) {
        if (!GetClassifier(c.id)) classifiers.push_back(c);
    }
    for (const auto& r : other.relationships) {
        UMLRelationship copy = r;
        copy.id.clear();  // AddRelationship mints a fresh, non-colliding id
        AddRelationship(copy);
    }
}

size_t UltraCanvasUMLModel::RemoveDanglingRelationships() {
    size_t before = relationships.size();
    relationships.erase(
        std::remove_if(relationships.begin(), relationships.end(),
                       [&](const UMLRelationship& r) {
                           return !GetClassifier(r.source.classifierId) ||
                                  !GetClassifier(r.target.classifierId);
                       }),
        relationships.end());
    return before - relationships.size();
}

std::vector<UMLDiagnostic> UltraCanvasUMLModel::Validate() const {
    std::vector<UMLDiagnostic> diagnostics;

    // --- duplicate classifier names -------------------------------------
    std::unordered_map<std::string, int> nameCounts;
    for (const auto& c : classifiers) nameCounts[c.name]++;
    for (const auto& c : classifiers) {
        if (nameCounts[c.name] > 1) {
            UMLDiagnostic d;
            d.severity = UMLDiagnosticSeverity::Warning;
            d.classifierId = c.id;
            d.message = "Duplicate classifier name '" + c.name + "'";
            diagnostics.push_back(d);
        }
    }

    // --- interfaces carrying attributes ---------------------------------
    for (const auto& c : classifiers) {
        if (c.kind == UMLClassifierKind::Interface && !c.attributes.empty()) {
            UMLDiagnostic d;
            d.severity = UMLDiagnosticSeverity::Warning;
            d.classifierId = c.id;
            d.message = "Interface '" + c.name + "' declares attributes; UML interfaces "
                        "normally have no state";
            diagnostics.push_back(d);
        }
    }

    // --- dangling relationship ends -------------------------------------
    for (const auto& r : relationships) {
        if (!GetClassifier(r.source.classifierId)) {
            UMLDiagnostic d;
            d.severity = UMLDiagnosticSeverity::Error;
            d.relationshipId = r.id;
            d.message = "Relationship source '" + r.source.classifierId +
                        "' does not resolve to a classifier";
            diagnostics.push_back(d);
        }
        if (!GetClassifier(r.target.classifierId)) {
            UMLDiagnostic d;
            d.severity = UMLDiagnosticSeverity::Error;
            d.relationshipId = r.id;
            d.message = "Relationship target '" + r.target.classifierId +
                        "' does not resolve to a classifier";
            diagnostics.push_back(d);
        }
        if (!r.associationClassId.empty() && !GetClassifier(r.associationClassId)) {
            UMLDiagnostic d;
            d.severity = UMLDiagnosticSeverity::Error;
            d.relationshipId = r.id;
            d.message = "Association class '" + r.associationClassId + "' does not exist";
            diagnostics.push_back(d);
        }
    }

    // --- duplicate relationships ----------------------------------------
    std::set<std::string> seen;
    for (const auto& r : relationships) {
        std::string key = std::to_string(static_cast<int>(r.kind)) + "|" +
                          r.source.classifierId + "|" + r.target.classifierId;
        if (!seen.insert(key).second) {
            UMLDiagnostic d;
            d.severity = UMLDiagnosticSeverity::Info;
            d.relationshipId = r.id;
            d.message = std::string("Duplicate ") + UMLRelationshipKindName(r.kind) +
                        " between the same two classifiers";
            diagnostics.push_back(d);
        }
    }

    // --- inheritance cycles ---------------------------------------------
    // Iterative DFS with a colour map: white (absent), grey (1), black (2).
    std::unordered_map<std::string, int> colour;
    for (const auto& c : classifiers) {
        if (colour[c.id] != 0) continue;
        std::vector<std::pair<std::string, size_t>> stack;
        stack.emplace_back(c.id, 0);
        colour[c.id] = 1;
        while (!stack.empty()) {
            auto& [node, childIndex] = stack.back();
            std::vector<std::string> parents = GetParents(node);
            if (childIndex < parents.size()) {
                std::string next = parents[childIndex];
                ++childIndex;
                if (!GetClassifier(next)) continue;  // dangling: already reported
                if (colour[next] == 1) {
                    UMLDiagnostic d;
                    d.severity = UMLDiagnosticSeverity::Error;
                    d.classifierId = next;
                    d.message = "Inheritance cycle through classifier '" + next + "'";
                    diagnostics.push_back(d);
                } else if (colour[next] == 0) {
                    colour[next] = 1;
                    stack.emplace_back(next, 0);
                }
            } else {
                colour[node] = 2;
                stack.pop_back();
            }
        }
    }

    return diagnostics;
}

// =============================================================================
// FREE HELPERS
// =============================================================================

const char* UMLVisibilityGlyph(UMLVisibility visibility) {
    switch (visibility) {
        case UMLVisibility::Public:    return "+";
        case UMLVisibility::Private:   return "-";
        case UMLVisibility::Protected: return "#";
        case UMLVisibility::Package:   return "~";
        case UMLVisibility::Unspecified: return "";
    }
    return "";
}

const char* UMLClassifierKindName(UMLClassifierKind kind) {
    switch (kind) {
        case UMLClassifierKind::Class:            return "class";
        case UMLClassifierKind::AbstractClass:    return "abstract class";
        case UMLClassifierKind::Interface:        return "interface";
        case UMLClassifierKind::Enumeration:      return "enumeration";
        case UMLClassifierKind::DataType:         return "dataType";
        case UMLClassifierKind::Primitive:        return "primitive";
        case UMLClassifierKind::Utility:          return "utility";
        case UMLClassifierKind::ActiveClass:      return "active class";
        case UMLClassifierKind::Template:         return "template";
        case UMLClassifierKind::Object:           return "object";
        case UMLClassifierKind::AssociationClass: return "association class";
        case UMLClassifierKind::Package:          return "package";
        case UMLClassifierKind::Note:             return "note";
    }
    return "class";
}

const char* UMLRelationshipKindName(UMLRelationshipKind kind) {
    switch (kind) {
        case UMLRelationshipKind::Association:         return "association";
        case UMLRelationshipKind::DirectedAssociation: return "directed association";
        case UMLRelationshipKind::Aggregation:         return "aggregation";
        case UMLRelationshipKind::Composition:         return "composition";
        case UMLRelationshipKind::Generalization:      return "generalization";
        case UMLRelationshipKind::Realization:         return "realization";
        case UMLRelationshipKind::Dependency:          return "dependency";
        case UMLRelationshipKind::NAry:                return "n-ary association";
    }
    return "association";
}

} // namespace UltraCanvas
