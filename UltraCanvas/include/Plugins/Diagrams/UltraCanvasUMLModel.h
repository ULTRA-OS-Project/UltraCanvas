// include/Plugins/Diagrams/UltraCanvasUMLModel.h
// UML static-structure model: classifiers, members and relationships
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework
//
// This is the semantic model behind the class-diagram feature described in
// Docs/UltraCanvas/UltraCanvasClassDiagramProposal.md. It is deliberately
// UI-free — it includes nothing from the widget or render stack — so that:
//   * the C++ reverse engineer (UltraCanvasCppReverseEngineer.h) can build a
//     model with no window in the loop,
//   * text export (UltraCanvasUMLTextExport.h) is a pure function of the model,
//   * all of the above are unit-testable (Tests/UMLModelTest.cpp).
//
// The renderer element (UltraCanvasClassDiagram) will own layout and geometry;
// this header owns meaning. Nothing here knows how wide a box is.
//
// CHANGELOG 1.0.0:
//  - Initial model: UMLVisibility, UMLClassifierKind, UMLRelationshipKind,
//    UMLParameter, UMLMember, UMLClassifier, UMLRelationshipEnd,
//    UMLRelationship, UltraCanvasUMLModel container with a fluent builder API
//    and a validation pass.

#pragma once

#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// ENUMERATIONS
// =============================================================================

enum class UMLVisibility {
    Public,       // +
    Private,      // -
    Protected,    // #
    Package,      // ~
    Unspecified   // no glyph
};

enum class UMLClassifierKind {
    Class,
    AbstractClass,   // name rendered in italics
    Interface,       // «interface»
    Enumeration,     // «enumeration», literals in the second compartment
    DataType,        // «dataType»
    Primitive,       // «primitive»
    Utility,         // «utility»
    ActiveClass,     // doubled vertical side bars
    Template,        // parameterised class
    Object,          // instance box: name : Class, underlined
    AssociationClass,
    Package,
    Note
};

enum class UMLRelationshipKind {
    Association,          // solid, no decoration
    DirectedAssociation,  // solid, open arrowhead
    Aggregation,          // solid, hollow diamond at the whole end
    Composition,          // solid, filled diamond at the whole end
    Generalization,       // solid, hollow closed triangle at the parent
    Realization,          // dashed, hollow closed triangle at the interface
    Dependency,           // dashed, open arrowhead
    NAry                  // 3+ ends meeting a diamond
};

// Which way an association name is meant to be read.
// "Unspecified" rather than "None": X11 defines None as a macro, and this
// header must stay usable in a translation unit that has included Xlib.
enum class UMLReadingDirection { Unspecified, SourceToTarget, TargetToSource };

// A member row belongs to exactly one compartment.
enum class UMLMemberKind { Attribute, Operation, Literal };

// =============================================================================
// MEMBERS
// =============================================================================

struct UMLParameter {
    std::string direction;     // "in", "out", "inout" — empty when unspecified
    std::string name;
    std::string type;
    std::string defaultValue;

    UMLParameter() = default;
    UMLParameter(const std::string& paramName, const std::string& paramType)
        : name(paramName), type(paramType) {}
};

// One row of a class box.
struct UMLMember {
    UMLMemberKind kind = UMLMemberKind::Attribute;
    UMLVisibility visibility = UMLVisibility::Public;

    std::string name;
    std::string type;            // attribute type, or operation return type
    std::string multiplicity;    // "0..1", "*"  — attribute only
    std::string defaultValue;
    std::string propertyString;  // "{readOnly}", "{query}", ...

    std::vector<UMLParameter> parameters;  // operations only

    bool isStatic = false;    // rendered underlined
    bool isAbstract = false;  // rendered italic (pure virtual)
    bool isDerived = false;   // rendered "/name"

    UMLMember() = default;

    static UMLMember Attribute(UMLVisibility vis, const std::string& memberName,
                               const std::string& memberType,
                               const std::string& memberMultiplicity = std::string());
    static UMLMember Operation(UMLVisibility vis, const std::string& memberName,
                               const std::string& returnType,
                               const std::vector<UMLParameter>& params = {});
    static UMLMember Literal(const std::string& literalName,
                             const std::string& literalValue = std::string());
};

// =============================================================================
// CLASSIFIERS
// =============================================================================

// Presentation overrides. The model carries them so a diagram round-trips
// through JSON without losing the user's styling, but nothing in this header
// interprets them — that is the element's job.
struct UMLClassifierStyle {
    bool hasCustomColors = false;
    unsigned int headerColor = 0;  // 0xRRGGBBAA
    unsigned int bodyColor = 0;
    unsigned int borderColor = 0;
    unsigned int textColor = 0;
};

struct UMLClassifier {
    std::string id;
    std::string name;
    UMLClassifierKind kind = UMLClassifierKind::Class;

    std::vector<std::string> stereotypes;      // "interface", "DataType", ...
    std::vector<std::string> templateParameters;  // "T", "T : Comparable"

    std::vector<UMLMember> attributes;
    std::vector<UMLMember> operations;
    std::vector<UMLMember> literals;           // enumerations

    std::string packageId;   // containing package/frame, empty for top level
    std::string sourceFile;  // provenance, set by the reverse engineer
    std::string documentation;

    UMLClassifierStyle style;

    // Layout hints. Populated by the element, persisted with the model.
    double x = 0.0, y = 0.0;
    double width = 0.0, height = 0.0;  // 0 = derive from content
    bool positionPinned = false;       // incremental layout must not move it
    bool collapsed = false;            // name-only badge

    UMLClassifier() = default;
    UMLClassifier(const std::string& classifierId, const std::string& classifierName,
                  UMLClassifierKind classifierKind = UMLClassifierKind::Class)
        : id(classifierId), name(classifierName), kind(classifierKind) {}

    bool HasStereotype(const std::string& stereotype) const;
    const UMLMember* FindAttribute(const std::string& attributeName) const;
    const UMLMember* FindOperation(const std::string& operationName) const;
    bool IsAbstractLike() const;  // AbstractClass or Interface
};

// =============================================================================
// RELATIONSHIPS
// =============================================================================

struct UMLRelationshipEnd {
    std::string classifierId;
    std::string roleName;      // "+contactInformation"
    std::string multiplicity;  // "1", "0..1", "1..*", "*"
    std::string constraint;    // "{ordered}", "{subsets x}"
    std::string qualifier;     // qualified association key
    bool navigable = false;

    UMLRelationshipEnd() = default;
    explicit UMLRelationshipEnd(const std::string& id) : classifierId(id) {}
};

struct UMLRelationshipStyle {
    bool hasCustomColor = false;
    unsigned int lineColor = 0;  // 0xRRGGBBAA
    double lineWidth = 0.0;      // 0 = theme default
};

struct UMLRelationship {
    std::string id;
    std::string name;        // association name: "contactInformation"
    UMLRelationshipKind kind = UMLRelationshipKind::Association;

    UMLRelationshipEnd source;
    UMLRelationshipEnd target;

    std::string stereotype;  // "use", "create", "derive" — dependencies
    UMLReadingDirection nameDirection = UMLReadingDirection::Unspecified;
    std::string associationClassId;
    std::vector<std::string> extraEndIds;  // NAry: ends beyond source/target

    UMLRelationshipStyle style;

    // Waypoints the user dragged, in the diagram's world space. Empty = auto.
    std::vector<double> waypointCoordinates;  // x0,y0,x1,y1,...

    UMLRelationship() = default;
    UMLRelationship(const std::string& relId, UMLRelationshipKind relKind,
                    const std::string& sourceId, const std::string& targetId)
        : id(relId), kind(relKind), source(sourceId), target(targetId) {}

    // True for the kinds UML draws with a dashed line.
    bool IsDashed() const;
};

// A fluent handle returned by the UltraCanvasUMLModel::Add*() helpers, so the
// six labels a UML edge can carry stay readable at the call site:
//
//   model.AddAssociation("Customer", "Account")
//        .SetMultiplicity("1", "1..*")
//        .SetRoles("+holder", "+accounts")
//        .SetName("owns", UMLReadingDirection::SourceToTarget);
//
// The handle points into the model's relationship vector; it is invalidated by
// any subsequent Add*/Remove* call, so use it immediately (the usual builder
// convention).
class UMLRelationshipHandle {
public:
    explicit UMLRelationshipHandle(UMLRelationship* rel = nullptr) : relationship(rel) {}

    UMLRelationshipHandle& SetName(const std::string& name,
                                   UMLReadingDirection direction = UMLReadingDirection::Unspecified);
    UMLRelationshipHandle& SetMultiplicity(const std::string& sourceMultiplicity,
                                           const std::string& targetMultiplicity);
    UMLRelationshipHandle& SetRoles(const std::string& sourceRole,
                                    const std::string& targetRole);
    UMLRelationshipHandle& SetStereotype(const std::string& stereotype);
    UMLRelationshipHandle& SetNavigable(bool sourceNavigable, bool targetNavigable);
    UMLRelationshipHandle& SetConstraints(const std::string& sourceConstraint,
                                          const std::string& targetConstraint);
    UMLRelationshipHandle& SetAssociationClass(const std::string& classifierId);

    bool IsValid() const { return relationship != nullptr; }
    UMLRelationship* Get() const { return relationship; }

private:
    UMLRelationship* relationship;
};

// =============================================================================
// DIAGNOSTICS
// =============================================================================

enum class UMLDiagnosticSeverity { Info, Warning, Error };

struct UMLDiagnostic {
    UMLDiagnosticSeverity severity = UMLDiagnosticSeverity::Warning;
    std::string message;
    std::string classifierId;    // subject, when applicable
    std::string relationshipId;  // subject, when applicable
};

// =============================================================================
// MODEL CONTAINER
// =============================================================================

class UltraCanvasUMLModel {
public:
    UltraCanvasUMLModel() = default;

    // ---- classifiers ----------------------------------------------------

    // Adds a classifier. If its id is empty, one is derived from the name
    // (made unique). Returns the id actually used. An existing id is
    // overwritten, so re-parsing a header updates in place.
    std::string AddClassifier(const UMLClassifier& classifier);

    // Convenience builders. Members are given in UML text form
    // ("+Name: string", "+GetTotal(): double") and parsed by
    // UltraCanvasUMLNotation. Returns the classifier id.
    std::string AddClass(const std::string& name,
                         const std::vector<std::string>& attributes = {},
                         const std::vector<std::string>& operations = {});
    std::string AddAbstractClass(const std::string& name,
                                 const std::vector<std::string>& attributes = {},
                                 const std::vector<std::string>& operations = {});
    std::string AddInterface(const std::string& name,
                             const std::vector<std::string>& operations = {});
    std::string AddEnumeration(const std::string& name,
                               const std::vector<std::string>& literals = {});

    UMLClassifier* GetClassifier(const std::string& id);
    const UMLClassifier* GetClassifier(const std::string& id) const;
    UMLClassifier* FindClassifierByName(const std::string& name);
    const UMLClassifier* FindClassifierByName(const std::string& name) const;
    bool RemoveClassifier(const std::string& id);  // also drops its relationships

    const std::vector<UMLClassifier>& Classifiers() const { return classifiers; }
    std::vector<UMLClassifier>& Classifiers() { return classifiers; }
    size_t ClassifierCount() const { return classifiers.size(); }

    // ---- relationships --------------------------------------------------

    UMLRelationshipHandle AddRelationship(const UMLRelationship& relationship);
    UMLRelationshipHandle AddRelationship(UMLRelationshipKind kind,
                                          const std::string& sourceIdOrName,
                                          const std::string& targetIdOrName);

    UMLRelationshipHandle AddAssociation(const std::string& source, const std::string& target);
    UMLRelationshipHandle AddDirectedAssociation(const std::string& source, const std::string& target);
    UMLRelationshipHandle AddAggregation(const std::string& whole, const std::string& part);
    UMLRelationshipHandle AddComposition(const std::string& whole, const std::string& part);
    UMLRelationshipHandle AddGeneralization(const std::string& child, const std::string& parent);
    UMLRelationshipHandle AddRealization(const std::string& implementer, const std::string& interfaceName);
    UMLRelationshipHandle AddDependency(const std::string& client, const std::string& supplier,
                                        const std::string& stereotype = std::string());

    UMLRelationship* GetRelationship(const std::string& id);
    const UMLRelationship* GetRelationship(const std::string& id) const;
    bool RemoveRelationship(const std::string& id);
    // True if a relationship of this kind already joins these two ends.
    bool HasRelationship(UMLRelationshipKind kind, const std::string& sourceId,
                         const std::string& targetId) const;

    const std::vector<UMLRelationship>& Relationships() const { return relationships; }
    std::vector<UMLRelationship>& Relationships() { return relationships; }
    size_t RelationshipCount() const { return relationships.size(); }

    // ---- queries --------------------------------------------------------

    // Direct parents / children following Generalization edges.
    std::vector<std::string> GetParents(const std::string& classifierId) const;
    std::vector<std::string> GetChildren(const std::string& classifierId) const;
    // Classifiers with no incoming Generalization from this model's edges.
    std::vector<std::string> GetRootClassifiers() const;
    // All relationships touching a classifier, in insertion order.
    std::vector<const UMLRelationship*> GetRelationshipsFor(const std::string& classifierId) const;

    // ---- whole-model ----------------------------------------------------

    void Clear();
    bool Empty() const { return classifiers.empty() && relationships.empty(); }

    const std::string& Title() const { return title; }
    void SetTitle(const std::string& value) { title = value; }

    // Merges `other` into this model: classifiers with a colliding id are
    // skipped, relationships are copied with fresh ids.
    void Merge(const UltraCanvasUMLModel& other);

    // Structural checks: dangling relationship ends, duplicate classifier
    // names, inheritance cycles, interfaces carrying attributes, relationships
    // that duplicate one another. Pure diagnosis — nothing is modified.
    std::vector<UMLDiagnostic> Validate() const;

    // Drops relationships whose ends do not resolve to a classifier. Returns
    // how many were removed. Used after a partial reverse-engineering pass.
    size_t RemoveDanglingRelationships();

    // Generates an id that is unique among current classifiers.
    std::string MakeUniqueClassifierId(const std::string& preferred) const;
    std::string MakeUniqueRelationshipId(const std::string& preferred) const;

    // Resolves an id, or failing that a classifier name, to an id. Returns the
    // input unchanged when nothing matches, so relationships to not-yet-added
    // classifiers still record their intent.
    std::string ResolveClassifierId(const std::string& idOrName) const;

private:
    std::string title;
    std::vector<UMLClassifier> classifiers;
    std::vector<UMLRelationship> relationships;
    int relationshipCounter = 0;
};

// =============================================================================
// FREE HELPERS
// =============================================================================

const char* UMLVisibilityGlyph(UMLVisibility visibility);
const char* UMLClassifierKindName(UMLClassifierKind kind);
const char* UMLRelationshipKindName(UMLRelationshipKind kind);

} // namespace UltraCanvas
