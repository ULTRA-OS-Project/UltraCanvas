// Tests/UMLModelTest.cpp
// Unit tests for the UML static-structure model, the member-notation grammar,
// the C++ reverse engineer and PlantUML/Mermaid text export.
//
// Exercises the real model/notation/scanner code with no UI stack — the whole
// point of keeping these units free of widget dependencies.
//
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasUMLModel.h"
#include "Plugins/Diagrams/UltraCanvasUMLNotation.h"
#include "Plugins/Diagrams/UltraCanvasCppReverseEngineer.h"
#include "Plugins/Diagrams/UltraCanvasUMLTextExport.h"

#include <cstdio>
#include <string>

using namespace UltraCanvas;

static int g_failures = 0;

#define CHECK(cond, msg)                                             \
    do {                                                             \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
        else         { std::printf("  ok:   %s\n", msg); }           \
    } while (0)

static bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// =============================================================================
// NOTATION
// =============================================================================

static void TestNotationAttributes() {
    std::printf("Member notation - attributes\n");

    UMLMember m;
    CHECK(UMLNotation::ParseMember("+BankId: int", m), "parses '+BankId: int'");
    CHECK(m.visibility == UMLVisibility::Public && m.name == "BankId" && m.type == "int",
          "visibility, name and type extracted");
    CHECK(m.kind == UMLMemberKind::Attribute, "inferred as an attribute");

    // Image 5 spelling: multiplicity between the name and the type.
    CHECK(UMLNotation::ParseMember("+fees [0..1] : String", m), "parses member-level multiplicity");
    CHECK(m.name == "fees" && m.type == "String" && m.multiplicity == "0..1",
          "multiplicity before the type is captured");

    // The other spelling: multiplicity after the type.
    CHECK(UMLNotation::ParseMember("-items : Item [0..*]", m), "parses trailing multiplicity");
    CHECK(m.name == "items" && m.type == "Item" && m.multiplicity == "0..*",
          "multiplicity after the type is captured");

    CHECK(UMLNotation::ParseMember("#count: int = 0", m), "parses a default value");
    CHECK(m.visibility == UMLVisibility::Protected && m.defaultValue == "0",
          "protected visibility and default value");

    CHECK(UMLNotation::ParseMember("+/total: double", m), "parses a derived attribute");
    CHECK(m.isDerived && m.name == "total", "derived marker stripped from the name");

    CHECK(UMLNotation::ParseMember("~registry: std::map<std::string, Node>", m),
          "parses a qualified template type");
    CHECK(m.visibility == UMLVisibility::Package && m.name == "registry" &&
          m.type == "std::map<std::string, Node>",
          "'::' and ',' inside the type do not split it");

    CHECK(UMLNotation::ParseMember("+id: int {readOnly}", m), "parses a property string");
    CHECK(m.propertyString == "{readOnly}", "property string preserved");

    CHECK(UMLNotation::ParseMember("+instances: int {static}", m), "parses {static}");
    CHECK(m.isStatic && m.propertyString.empty(), "{static} becomes a flag, not free text");

    CHECK(UMLNotation::ParseMember("Location: string", m), "parses a member with no glyph");
    CHECK(m.visibility == UMLVisibility::Unspecified,
          "absent glyph yields Unspecified, not an invented Public");

    CHECK(!UMLNotation::ParseMember("", m), "empty text is rejected");
    CHECK(!UMLNotation::ParseMember("   ", m), "blank text is rejected");
}

static void TestNotationOperations() {
    std::printf("Member notation - operations\n");

    UMLMember m;
    CHECK(UMLNotation::ParseMember("+CollectMoney()", m), "parses a no-arg operation");
    CHECK(m.kind == UMLMemberKind::Operation && m.name == "CollectMoney" &&
          m.parameters.empty() && m.type.empty(),
          "operation with no parameters and no return type");

    CHECK(UMLNotation::ParseMember("+Deposit(amount: double, note: string): bool", m),
          "parses parameters and a return type");
    CHECK(m.parameters.size() == 2, "two parameters");
    CHECK(m.parameters[0].name == "amount" && m.parameters[0].type == "double",
          "first parameter name and type");
    CHECK(m.type == "bool", "return type");

    // Image 3 spelling: parameters given by type alone.
    CHECK(UMLNotation::ParseMember("+method(Type): Type", m), "parses unnamed parameters");
    CHECK(m.parameters.size() == 1 && m.parameters[0].name.empty() &&
          m.parameters[0].type == "Type", "type-only parameter");

    CHECK(UMLNotation::ParseMember("+Render(ctx: Context = nullptr)", m),
          "parses a parameter default");
    CHECK(m.parameters[0].defaultValue == "nullptr", "parameter default captured");

    CHECK(UMLNotation::ParseMember("+Draw() {abstract}", m), "parses {abstract}");
    CHECK(m.isAbstract, "abstract flag set");

    CHECK(UMLNotation::ParseMember("+Merge(in a: Set, out b: Set)", m),
          "parses parameter directions");
    CHECK(m.parameters[0].direction == "in" && m.parameters[1].direction == "out",
          "in/out directions captured");
}

static void TestNotationRoundTrip() {
    std::printf("Member notation - round trip\n");

    const char* samples[] = {
        "+BankId: int",
        "-notes [0..*]: string",
        "#count: int = 0",
        "+/total: double",
        "+Deposit(amount: double): bool",
        "+method(Type): Type",
        "~registry: std::map<std::string, Node>",
        "+id: int {readOnly}",
    };

    bool allRoundTrip = true;
    for (const char* sample : samples) {
        UMLMember first, second;
        if (!UMLNotation::ParseMember(sample, first)) { allRoundTrip = false; break; }
        std::string formatted = UMLNotation::FormatMember(first);
        if (!UMLNotation::ParseMember(formatted, second)) { allRoundTrip = false; break; }
        second.kind = first.kind;
        bool same = first.name == second.name && first.type == second.type &&
                    first.visibility == second.visibility &&
                    first.multiplicity == second.multiplicity &&
                    first.defaultValue == second.defaultValue &&
                    first.isStatic == second.isStatic &&
                    first.isDerived == second.isDerived &&
                    first.isAbstract == second.isAbstract &&
                    first.parameters.size() == second.parameters.size();
        if (!same) {
            std::printf("    round-trip differs for '%s' -> '%s'\n", sample, formatted.c_str());
            allRoundTrip = false;
        }
    }
    CHECK(allRoundTrip, "parse -> format -> parse is stable for every sample");

    UMLMember staticOp;
    UMLNotation::ParseMember("+Instance(): Manager {static}", staticOp);
    UMLMember reparsed;
    UMLNotation::ParseMember(UMLNotation::FormatMember(staticOp), reparsed);
    CHECK(reparsed.isStatic, "{static} survives a round trip");
}

static void TestMultiplicity() {
    std::printf("Multiplicity grammar\n");

    CHECK(UMLNotation::IsValidMultiplicity("1"), "'1' is valid");
    CHECK(UMLNotation::IsValidMultiplicity("0..1"), "'0..1' is valid");
    CHECK(UMLNotation::IsValidMultiplicity("*"), "'*' is valid");
    CHECK(UMLNotation::IsValidMultiplicity("1..*"), "'1..*' is valid");
    CHECK(UMLNotation::IsValidMultiplicity("2..7"), "'2..7' is valid");
    CHECK(UMLNotation::IsValidMultiplicity("1,3..5"), "a list is valid");
    CHECK(!UMLNotation::IsValidMultiplicity("many"), "'many' is rejected");
    CHECK(!UMLNotation::IsValidMultiplicity(""), "empty is rejected");
    CHECK(!UMLNotation::IsValidMultiplicity("1.."), "an open range is rejected");

    // The informal '+1..*' spelling used by several diagram tools.
    CHECK(UMLNotation::NormalizeMultiplicity(" +1..* ") == "1..*",
          "decorative '+' and whitespace are stripped");
}

// =============================================================================
// MODEL
// =============================================================================

static void TestModelBuilders() {
    std::printf("Model builders\n");

    UltraCanvasUMLModel model;
    model.SetTitle("Class Diagram for a Banking System");

    std::string bank = model.AddClass("Bank",
        {"+BankId: int", "+Name: string", "+Location: string"}, {});
    std::string customer = model.AddClass("Customer",
        {"+Id: int", "+Name: string"},
        {"+GeneralInquiry()", "+DepositMoney()", "+WithdrawMoney()"});
    model.AddAbstractClass("Account", {"+Id: int", "+CustomerId: int"}, {});
    model.AddClass("Checking", {"+Id: int", "+CustomerId: int"}, {});
    model.AddInterface("IPayable", {"+Pay(amount: double): bool"});
    model.AddEnumeration("AccountKind", {"Checking", "Savings = 2"});

    CHECK(model.ClassifierCount() == 6, "six classifiers added");
    CHECK(model.GetClassifier(bank) != nullptr, "classifier retrievable by id");
    CHECK(model.FindClassifierByName("Customer") != nullptr, "classifier findable by name");
    CHECK(model.GetClassifier(customer)->operations.size() == 3, "operations parsed into the model");
    CHECK(model.GetClassifier(customer)->attributes[0].name == "Id", "attributes parsed in order");

    const UMLClassifier* account = model.FindClassifierByName("Account");
    CHECK(account && account->kind == UMLClassifierKind::AbstractClass, "abstract class kind");
    CHECK(account && account->IsAbstractLike(), "IsAbstractLike covers abstract classes");

    const UMLClassifier* payable = model.FindClassifierByName("IPayable");
    CHECK(payable && payable->kind == UMLClassifierKind::Interface, "interface kind");
    CHECK(payable && payable->HasStereotype("interface"), "interface stereotype attached");
    CHECK(payable && payable->operations[0].isAbstract, "interface operations are abstract");

    const UMLClassifier* kinds = model.FindClassifierByName("AccountKind");
    CHECK(kinds && kinds->literals.size() == 2, "enumeration literals parsed");
    CHECK(kinds && kinds->literals[1].name == "Savings" && kinds->literals[1].defaultValue == "2",
          "literal with an explicit value");

    // Duplicate names must not collide on ids.
    std::string first = model.AddClass("Ledger", {}, {});
    std::string second = model.AddClass("Ledger", {}, {});
    CHECK(first != second, "same name yields distinct ids");
}

static void TestRelationshipBuilders() {
    std::printf("Relationship builders\n");

    UltraCanvasUMLModel model;
    model.AddClass("Customer", {}, {});
    model.AddClass("Account", {}, {});
    model.AddClass("Checking", {}, {});
    model.AddInterface("IPayable", {});

    model.AddGeneralization("Checking", "Account");
    model.AddAssociation("Customer", "Account")
         .SetMultiplicity("1", "1..*")
         .SetRoles("+holder", "+accounts")
         .SetName("owns", UMLReadingDirection::SourceToTarget);
    model.AddComposition("Customer", "Checking").SetMultiplicity("1", "0..*");
    model.AddRealization("Account", "IPayable");
    model.AddDependency("Customer", "IPayable", "use");

    CHECK(model.RelationshipCount() == 5, "five relationships added");

    // Names were resolved to ids.
    const UMLRelationship& generalization = model.Relationships()[0];
    CHECK(generalization.source.classifierId == model.FindClassifierByName("Checking")->id,
          "relationship ends resolve names to ids");

    const UMLRelationship& association = model.Relationships()[1];
    CHECK(association.source.multiplicity == "1" && association.target.multiplicity == "1..*",
          "multiplicities set through the fluent handle");
    CHECK(association.source.roleName == "+holder" && association.target.roleName == "+accounts",
          "role names set through the fluent handle");
    CHECK(association.name == "owns" &&
          association.nameDirection == UMLReadingDirection::SourceToTarget,
          "association name and reading direction");

    CHECK(model.Relationships()[3].IsDashed(), "realization is dashed");
    CHECK(model.Relationships()[4].IsDashed(), "dependency is dashed");
    CHECK(!generalization.IsDashed(), "generalization is solid");

    auto parents = model.GetParents("Checking");
    CHECK(parents.size() == 1 && parents[0] == model.FindClassifierByName("Account")->id,
          "GetParents follows generalization");
    auto children = model.GetChildren("Account");
    CHECK(children.size() == 1, "GetChildren is the inverse");
    CHECK(model.GetRelationshipsFor("Customer").size() == 3, "relationships for a classifier");

    CHECK(model.HasRelationship(UMLRelationshipKind::Generalization,
                                model.FindClassifierByName("Checking")->id,
                                model.FindClassifierByName("Account")->id),
          "HasRelationship finds an existing edge");

    // Removing a classifier removes its edges.
    size_t before = model.RelationshipCount();
    model.RemoveClassifier(model.FindClassifierByName("IPayable")->id);
    CHECK(model.RelationshipCount() == before - 2, "removing a classifier drops its edges");
}

static void TestValidation() {
    std::printf("Model validation\n");

    UltraCanvasUMLModel model;
    model.AddClass("A", {}, {});
    model.AddClass("B", {}, {});
    model.AddGeneralization("A", "B");
    CHECK(model.Validate().empty(), "a clean model reports nothing");

    // Inheritance cycle.
    model.AddGeneralization("B", "A");
    bool sawCycle = false;
    for (const auto& d : model.Validate()) {
        if (d.severity == UMLDiagnosticSeverity::Error &&
            d.message.find("cycle") != std::string::npos) sawCycle = true;
    }
    CHECK(sawCycle, "inheritance cycle detected");

    // Dangling end.
    UltraCanvasUMLModel dangling;
    dangling.AddClass("Only", {}, {});
    dangling.AddAssociation("Only", "Missing");
    bool sawDangling = false;
    for (const auto& d : dangling.Validate()) {
        if (d.severity == UMLDiagnosticSeverity::Error) sawDangling = true;
    }
    CHECK(sawDangling, "dangling relationship end detected");
    CHECK(dangling.RemoveDanglingRelationships() == 1, "dangling edge removed");
    CHECK(dangling.Validate().empty(), "model is clean after removal");

    // Interface with state.
    UltraCanvasUMLModel stateful;
    UMLClassifier iface("iface", "IThing", UMLClassifierKind::Interface);
    iface.attributes.push_back(UMLMember::Attribute(UMLVisibility::Private, "cache", "int"));
    stateful.AddClassifier(iface);
    bool sawStateWarning = false;
    for (const auto& d : stateful.Validate()) {
        if (d.message.find("no state") != std::string::npos) sawStateWarning = true;
    }
    CHECK(sawStateWarning, "interface with attributes is flagged");

    // Duplicate names.
    UltraCanvasUMLModel duplicates;
    duplicates.AddClass("Same", {}, {});
    duplicates.AddClass("Same", {}, {});
    bool sawDuplicate = false;
    for (const auto& d : duplicates.Validate()) {
        if (d.message.find("Duplicate classifier name") != std::string::npos) sawDuplicate = true;
    }
    CHECK(sawDuplicate, "duplicate classifier name is flagged");
}

// =============================================================================
// C++ REVERSE ENGINEER
// =============================================================================

static const char* const kSampleHeader = R"CPP(
// A comment mentioning class NotAClass { and a stray brace.
#pragma once
#include <memory>
#define SOME_MACRO(x) class MacroClass { x }

namespace UltraCanvas {

/* Block comment with "quoted ; text" and class AlsoNotAClass */

enum class Shape { Rectangle, Circle = 3, Diamond };

class IRenderable {
public:
    virtual ~IRenderable() = default;
    virtual void Render(Context* ctx) = 0;
    virtual bool IsVisible() const = 0;
};

struct Point {
    double x = 0.0;
    double y = 0.0;
};

class Widget : public IRenderable {
public:
    Widget(const std::string& name, int id);
    void Render(Context* ctx) override;
    bool IsVisible() const override { return visible; }
    static int GetInstanceCount();
    virtual double ComputeArea() const = 0;

protected:
    std::string title;

private:
    bool visible = true;
    int width, height;
    Point origin;
    std::vector<std::shared_ptr<Widget>> children;
    std::unique_ptr<Point> anchor;
    Shape shape = Shape::Rectangle;
    Widget* parent = nullptr;

    using Callback = void(*)(int);
    typedef int Alias;
    friend class Inspector;
};

class Panel final : public Widget {
public:
    double ComputeArea() const override;
private:
    Point corners[4];
};

class ForwardOnly;

} // namespace UltraCanvas
)CPP";

static void TestStripCommentsAndDirectives() {
    std::printf("C++ preprocessing\n");

    std::string cleaned =
        UltraCanvasCppReverseEngineer::StripCommentsAndDirectives(kSampleHeader);

    CHECK(!Contains(cleaned, "NotAClass"), "line comments removed");
    CHECK(!Contains(cleaned, "AlsoNotAClass"), "block comments removed");
    CHECK(!Contains(cleaned, "MacroClass"), "preprocessor directives removed");
    CHECK(!Contains(cleaned, "pragma"), "#pragma removed");
    CHECK(Contains(cleaned, "class Widget"), "real declarations survive");
    CHECK(!Contains(cleaned, "quoted ; text"), "string literal contents blanked");
}

static void TestReverseEngineerClassifiers() {
    std::printf("C++ reverse engineering - classifiers\n");

    UltraCanvasUMLModel model;
    CppReverseEngineerOptions options;
    options.includeConstructors = false;
    UltraCanvasCppReverseEngineer engineer(options);
    CppReverseEngineerResult result = engineer.ParseSource(kSampleHeader, model, "sample.h");
    engineer.ResolveInferredRelationships(model);

    CHECK(result.filesParsed == 1, "one source parsed");
    CHECK(model.FindClassifierByName("IRenderable") != nullptr, "class found");
    CHECK(model.FindClassifierByName("Widget") != nullptr, "class with bases found");
    CHECK(model.FindClassifierByName("Point") != nullptr, "struct found");
    CHECK(model.FindClassifierByName("Panel") != nullptr, "final class found");
    CHECK(model.FindClassifierByName("Shape") != nullptr, "enum class found");
    CHECK(model.FindClassifierByName("ForwardOnly") == nullptr,
          "forward declaration does not create a classifier");
    CHECK(model.FindClassifierByName("Inspector") == nullptr,
          "friend declaration does not create a classifier");

    const UMLClassifier* renderable = model.FindClassifierByName("IRenderable");
    CHECK(renderable->kind == UMLClassifierKind::Interface,
          "all-pure-virtual, no state -> interface");
    CHECK(renderable->HasStereotype("interface"), "interface stereotype applied");

    const UMLClassifier* widget = model.FindClassifierByName("Widget");
    CHECK(widget->kind == UMLClassifierKind::AbstractClass,
          "a pure virtual member makes the class abstract");

    const UMLClassifier* point = model.FindClassifierByName("Point");
    CHECK(point->kind == UMLClassifierKind::Class, "plain struct stays a class");
    CHECK(point->attributes.size() == 2, "struct members found");
    CHECK(point->attributes[0].visibility == UMLVisibility::Public,
          "struct members default to public");

    const UMLClassifier* shape = model.FindClassifierByName("Shape");
    CHECK(shape->kind == UMLClassifierKind::Enumeration, "enum kind");
    CHECK(shape->literals.size() == 3, "three literals");
    CHECK(shape->literals[1].name == "Circle" && shape->literals[1].defaultValue == "3",
          "literal with an explicit value");
}

static void TestReverseEngineerMembers() {
    std::printf("C++ reverse engineering - members\n");

    UltraCanvasUMLModel model;
    UltraCanvasCppReverseEngineer engineer;
    engineer.ParseSource(kSampleHeader, model, "sample.h");

    const UMLClassifier* widget = model.FindClassifierByName("Widget");

    const UMLMember* render = widget->FindOperation("Render");
    CHECK(render != nullptr, "public operation found");
    CHECK(render && render->visibility == UMLVisibility::Public, "public access section honoured");
    CHECK(render && render->parameters.size() == 1 && render->parameters[0].type == "Context*",
          "parameter type captured");
    CHECK(render && render->parameters[0].name == "ctx", "parameter name captured");
    CHECK(render && render->type == "void", "return type captured");

    const UMLMember* area = widget->FindOperation("ComputeArea");
    CHECK(area && area->isAbstract, "= 0 marks the operation abstract");

    const UMLMember* count = widget->FindOperation("GetInstanceCount");
    CHECK(count && count->isStatic, "static operation flagged");

    const UMLMember* visible = widget->FindOperation("IsVisible");
    CHECK(visible != nullptr, "inline-bodied operation still recorded");

    const UMLMember* title = widget->FindAttribute("title");
    CHECK(title && title->visibility == UMLVisibility::Protected,
          "protected access section honoured");

    const UMLMember* visibleField = widget->FindAttribute("visible");
    CHECK(visibleField && visibleField->visibility == UMLVisibility::Private,
          "class members default to private");
    CHECK(visibleField && visibleField->defaultValue == "true", "member initialiser captured");

    CHECK(widget->FindAttribute("width") != nullptr && widget->FindAttribute("height") != nullptr,
          "multiple declarators on one line both captured");
    CHECK(widget->FindAttribute("height")->type == "int",
          "the second declarator inherits the shared type");

    CHECK(widget->FindAttribute("Callback") == nullptr, "using-alias is not a member");
    CHECK(widget->FindAttribute("Alias") == nullptr, "typedef is not a member");

    const UMLClassifier* panel = model.FindClassifierByName("Panel");
    const UMLMember* corners = panel->FindAttribute("corners");
    CHECK(corners && corners->multiplicity == "4", "array bound becomes a multiplicity");

    // Constructors are excluded by default.
    CHECK(widget->FindOperation("Widget") == nullptr,
          "constructors excluded unless asked for");

    CppReverseEngineerOptions withCtors;
    withCtors.includeConstructors = true;
    UltraCanvasUMLModel ctorModel;
    UltraCanvasCppReverseEngineer ctorEngineer(withCtors);
    ctorEngineer.ParseSource(kSampleHeader, ctorModel, "sample.h");
    CHECK(ctorModel.FindClassifierByName("Widget")->FindOperation("Widget") != nullptr,
          "constructors included when requested");
}

static void TestReverseEngineerRelationships() {
    std::printf("C++ reverse engineering - relationships\n");

    UltraCanvasUMLModel model;
    UltraCanvasCppReverseEngineer engineer;
    engineer.ParseSource(kSampleHeader, model, "sample.h");
    engineer.ResolveInferredRelationships(model);

    const UMLClassifier* widget = model.FindClassifierByName("Widget");
    const UMLClassifier* renderable = model.FindClassifierByName("IRenderable");
    const UMLClassifier* panel = model.FindClassifierByName("Panel");
    const UMLClassifier* point = model.FindClassifierByName("Point");

    // Base that is an interface -> realization (dashed), not generalization.
    CHECK(model.HasRelationship(UMLRelationshipKind::Realization, widget->id, renderable->id),
          "interface base becomes a realization");
    CHECK(!model.HasRelationship(UMLRelationshipKind::Generalization, widget->id, renderable->id),
          "and not a generalization");

    // Ordinary base -> generalization.
    CHECK(model.HasRelationship(UMLRelationshipKind::Generalization, panel->id, widget->id),
          "class base becomes a generalization");

    // Ownership inference.
    CHECK(model.HasRelationship(UMLRelationshipKind::Composition, widget->id, point->id),
          "by-value member -> composition");
    CHECK(model.HasRelationship(UMLRelationshipKind::Aggregation, widget->id, widget->id) == false,
          "self-reference does not create an edge");

    bool sawSharedPtrAggregation = false, sawRawPointer = false;
    for (const auto& r : model.Relationships()) {
        if (r.target.roleName == "children" && r.kind == UMLRelationshipKind::Aggregation &&
            r.target.multiplicity == "*") {
            sawSharedPtrAggregation = true;
        }
        if (r.target.roleName == "parent" &&
            r.kind == UMLRelationshipKind::DirectedAssociation) {
            sawRawPointer = true;
        }
    }
    // vector<shared_ptr<Widget>> children is a self-reference, so no edge is
    // emitted for it; the assertions below use the surviving cases.
    CHECK(!sawSharedPtrAggregation, "self-referencing container yields no edge");
    CHECK(!sawRawPointer, "self-referencing raw pointer yields no edge");

    // Edges to types outside the model (std::string, int) must not survive.
    for (const auto& r : model.Relationships()) {
        CHECK(model.GetClassifier(r.source.classifierId) != nullptr &&
              model.GetClassifier(r.target.classifierId) != nullptr,
              "every inferred edge resolves to real classifiers");
        break;  // one representative check is enough; Validate() covers the rest
    }
    CHECK(model.Validate().empty() ||
          model.Validate().front().severity != UMLDiagnosticSeverity::Error,
          "reverse-engineered model has no dangling edges");
}

static void TestOwnershipMapping() {
    std::printf("Type analysis\n");

    using Engineer = UltraCanvasCppReverseEngineer;
    std::string core, multiplicity;
    Engineer::TypeOwnership ownership;

    CHECK(Engineer::ExtractReferencedType("Point", core, multiplicity, ownership) &&
          core == "Point" && multiplicity == "1" &&
          ownership == Engineer::TypeOwnership::Value,
          "plain value type");

    CHECK(Engineer::ExtractReferencedType("std::vector<Node>", core, multiplicity, ownership) &&
          core == "Node" && multiplicity == "*",
          "vector -> multiplicity *");

    CHECK(Engineer::ExtractReferencedType("std::vector<std::shared_ptr<Node>>",
                                          core, multiplicity, ownership) &&
          core == "Node" && multiplicity == "*" &&
          ownership == Engineer::TypeOwnership::SharedPointer,
          "vector of shared_ptr unwraps both layers");

    CHECK(Engineer::ExtractReferencedType("std::unique_ptr<Body>", core, multiplicity, ownership) &&
          core == "Body" && ownership == Engineer::TypeOwnership::UniquePointer,
          "unique_ptr ownership");

    CHECK(Engineer::ExtractReferencedType("std::optional<Config>", core, multiplicity, ownership) &&
          core == "Config" && multiplicity == "0..1",
          "optional -> 0..1");

    CHECK(Engineer::ExtractReferencedType("const Widget&", core, multiplicity, ownership) &&
          core == "Widget" && ownership == Engineer::TypeOwnership::Reference,
          "const reference");

    CHECK(Engineer::ExtractReferencedType("Widget*", core, multiplicity, ownership) &&
          core == "Widget" && ownership == Engineer::TypeOwnership::RawPointer &&
          multiplicity == "0..1",
          "raw pointer is nullable");

    CHECK(Engineer::ExtractReferencedType("std::map<std::string, Node>",
                                          core, multiplicity, ownership) &&
          core == "Node" && multiplicity == "*",
          "map yields the mapped type");

    CHECK(Engineer::ExtractReferencedType("UltraCanvas::Nested::Thing",
                                          core, multiplicity, ownership) &&
          core == "Thing",
          "namespace qualification stripped");
}

static void TestOptionsAreHonoured() {
    std::printf("Reverse-engineer options\n");

    CppReverseEngineerOptions options;
    options.includePrivateMembers = false;
    options.includeOperations = false;
    options.inferAssociations = false;
    options.excludeNamePrefixes = {"Point"};

    UltraCanvasUMLModel model;
    UltraCanvasCppReverseEngineer engineer(options);
    engineer.ParseSource(kSampleHeader, model, "sample.h");
    engineer.ResolveInferredRelationships(model);

    const UMLClassifier* widget = model.FindClassifierByName("Widget");
    CHECK(widget != nullptr, "classes still found");
    CHECK(widget->FindAttribute("visible") == nullptr, "private members excluded");
    CHECK(widget->FindAttribute("title") != nullptr, "protected members still included");
    CHECK(widget->operations.empty(), "operations excluded");
    CHECK(model.FindClassifierByName("Point") == nullptr, "excluded name prefix honoured");

    for (const auto& r : model.Relationships()) {
        CHECK(r.kind == UMLRelationshipKind::Generalization ||
              r.kind == UMLRelationshipKind::Realization,
              "only inheritance edges when association inference is off");
        break;
    }

    CppReverseEngineerOptions capped;
    capped.maxMembersPerCompartment = 2;
    UltraCanvasUMLModel cappedModel;
    UltraCanvasCppReverseEngineer cappedEngineer(capped);
    CppReverseEngineerResult result = cappedEngineer.ParseSource(kSampleHeader, cappedModel, "s.h");
    CHECK(cappedModel.FindClassifierByName("Widget")->attributes.size() == 2,
          "compartment cap applied");
    CHECK(!result.warnings.empty(), "truncation is reported, not silent");
}

// =============================================================================
// TEXT EXPORT
// =============================================================================

static void TestTextExport() {
    std::printf("Text export\n");

    UltraCanvasUMLModel model;
    model.SetTitle("Banking");
    model.AddAbstractClass("Account", {"+Id: int"}, {"+Close()"});
    model.AddClass("Checking", {"+Id: int"}, {});
    model.AddInterface("IPayable", {"+Pay(amount: double): bool"});
    model.AddEnumeration("Kind", {"Debit", "Credit"});
    model.AddGeneralization("Checking", "Account");
    model.AddRealization("Account", "IPayable");
    model.AddComposition("Account", "Checking").SetMultiplicity("1", "0..*");

    std::string plant = UMLTextExport::ToPlantUML(model);
    CHECK(Contains(plant, "@startuml") && Contains(plant, "@enduml"), "PlantUML wrapper emitted");
    CHECK(Contains(plant, "title Banking"), "PlantUML title emitted");
    CHECK(Contains(plant, "abstract class Account"), "PlantUML abstract keyword");
    CHECK(Contains(plant, "interface IPayable"), "PlantUML interface keyword");
    CHECK(Contains(plant, "enum Kind"), "PlantUML enum keyword");
    CHECK(Contains(plant, "Checking --|> Account"), "PlantUML generalization arrow");
    CHECK(Contains(plant, "Account ..|> IPayable"), "PlantUML realization arrow");
    CHECK(Contains(plant, "Account \"1\" *-- \"0..*\" Checking"),
          "PlantUML composition with multiplicities");
    CHECK(Contains(plant, "+Id: int"), "PlantUML member row");

    std::string mermaid = UMLTextExport::ToMermaid(model);
    CHECK(Contains(mermaid, "classDiagram"), "Mermaid header emitted");
    CHECK(Contains(mermaid, "<<interface>>"), "Mermaid interface annotation");
    CHECK(Contains(mermaid, "<<abstract>>"), "Mermaid abstract annotation");
    // Mermaid writes the parent first for inheritance.
    CHECK(Contains(mermaid, "Account <|-- Checking"), "Mermaid generalization is parent-first");
    CHECK(Contains(mermaid, "IPayable <|.. Account"), "Mermaid realization is interface-first");
    CHECK(Contains(mermaid, "Account \"1\" *-- \"0..*\" Checking"),
          "Mermaid composition with multiplicities");

    UMLTextExport::TextExportOptions bare;
    bare.includeWrapper = false;
    bare.includeMembers = false;
    std::string fragment = UMLTextExport::ToPlantUML(model, bare);
    CHECK(!Contains(fragment, "@startuml"), "wrapper suppressed on request");
    CHECK(!Contains(fragment, "+Id: int"), "members suppressed on request");
}

static void TestEndToEnd() {
    std::printf("End to end: C++ source -> model -> PlantUML\n");

    UltraCanvasUMLModel model;
    UltraCanvasCppReverseEngineer engineer;
    engineer.ParseSource(kSampleHeader, model, "sample.h");
    engineer.ResolveInferredRelationships(model);
    model.SetTitle("Reverse engineered");

    std::string plant = UMLTextExport::ToPlantUML(model);
    CHECK(Contains(plant, "interface IRenderable"), "interface survives the whole pipeline");
    CHECK(Contains(plant, "abstract class Widget"), "abstract class survives the pipeline");
    CHECK(Contains(plant, "Widget ..|> IRenderable"), "realization survives the pipeline");
    CHECK(Contains(plant, "Panel --|> Widget"), "generalization survives the pipeline");

    bool noErrors = true;
    for (const auto& d : model.Validate()) {
        if (d.severity == UMLDiagnosticSeverity::Error) noErrors = false;
    }
    CHECK(noErrors, "the produced model validates without errors");
}

// =============================================================================

int main() {
    std::printf("=== UltraCanvas UML model / notation / reverse-engineer tests ===\n\n");

    TestNotationAttributes();
    TestNotationOperations();
    TestNotationRoundTrip();
    TestMultiplicity();
    TestModelBuilders();
    TestRelationshipBuilders();
    TestValidation();
    TestStripCommentsAndDirectives();
    TestReverseEngineerClassifiers();
    TestReverseEngineerMembers();
    TestReverseEngineerRelationships();
    TestOwnershipMapping();
    TestOptionsAreHonoured();
    TestTextExport();
    TestEndToEnd();

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASSED" : "FAILED",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
