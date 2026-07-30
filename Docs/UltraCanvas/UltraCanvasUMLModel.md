# UltraCanvasUMLModel, UMLNotation, C++ reverse engineering and text export

The UI-free foundation of the class-diagram feature: a UML static-structure
model, the member-notation grammar, a heuristic C++ header scanner that builds a
model from source, and PlantUML/Mermaid export.

| Unit | Header | Source |
|---|---|---|
| Model | `include/Plugins/Diagrams/UltraCanvasUMLModel.h` | `Plugins/Diagrams/UltraCanvasUMLModel.cpp` |
| Notation grammar | `include/Plugins/Diagrams/UltraCanvasUMLNotation.h` | `Plugins/Diagrams/UltraCanvasUMLNotation.cpp` |
| C++ reverse engineer | `include/Plugins/Diagrams/UltraCanvasCppReverseEngineer.h` | `Plugins/Diagrams/UltraCanvasCppReverseEngineer.cpp` |
| Text export | `include/Plugins/Diagrams/UltraCanvasUMLTextExport.h` | `Plugins/Diagrams/UltraCanvasUMLTextExport.cpp` |

Tests: `Tests/UMLModelTest.cpp` (target `UMLModelTest`). Version: 1.0.0.

None of these include anything from the widget or render stack. The rendering
element (`UltraCanvasClassDiagram`) is a separate, later piece — see
[`UltraCanvasClassDiagramProposal.md`](UltraCanvasClassDiagramProposal.md).

---

## 1. The model

```cpp
#include "Plugins/Diagrams/UltraCanvasUMLModel.h"

UltraCanvasUMLModel model;
model.SetTitle("Class Diagram for a Banking System");

// Short form: members as UML text, parsed on entry.
model.AddClass("Bank", { "+BankId: int", "+Name: string", "+Location: string" }, {});
model.AddClass("Teller", { "+Id: int", "+Name: string" },
                         { "+CollectMoney()", "+OpenAccount()", "+LoanRequest()" });
model.AddAbstractClass("Account", { "+Id: int", "+CustomerId: int" }, { "+Close()" });
model.AddInterface("IPayable", { "+Pay(amount: double): bool" });
model.AddEnumeration("AccountKind", { "Checking", "Savings = 2" });

// Verbose form: full control.
UMLClassifier checking("checking", "Checking");
checking.attributes.push_back(UMLMember::Attribute(UMLVisibility::Public, "Id", "int"));
model.AddClassifier(checking);
```

### Classifiers

`UMLClassifierKind` covers `Class`, `AbstractClass`, `Interface`, `Enumeration`,
`DataType`, `Primitive`, `Utility`, `ActiveClass`, `Template`, `Object`,
`AssociationClass`, `Package` and `Note`.

A `UMLClassifier` carries its name, kind, stereotypes, template parameters, the
three member compartments (`attributes`, `operations`, `literals`), its
containing `packageId`, provenance (`sourceFile`), presentation overrides
(`UMLClassifierStyle`) and layout hints (`x`, `y`, `width`, `height`,
`positionPinned`, `collapsed`). Nothing in the model interprets the style or
layout fields — they are carried so a diagram round-trips without losing them.

### Members

One `UMLMember` is one row of a class box:

```cpp
struct UMLMember {
    UMLMemberKind kind;          // Attribute / Operation / Literal
    UMLVisibility visibility;    // Public + / Private - / Protected # / Package ~ / Unspecified
    std::string name, type, multiplicity, defaultValue, propertyString;
    std::vector<UMLParameter> parameters;   // operations only
    bool isStatic;    // rendered underlined
    bool isAbstract;  // rendered italic
    bool isDerived;   // rendered "/name"
};
```

Factories: `UMLMember::Attribute(...)`, `UMLMember::Operation(...)`,
`UMLMember::Literal(...)`.

### Relationships

`UMLRelationshipKind` is `Association`, `DirectedAssociation`, `Aggregation`,
`Composition`, `Generalization`, `Realization`, `Dependency` or `NAry`. The kind
alone determines the line style and end decorations, so a mis-drawn relationship
is not expressible.

Each end (`UMLRelationshipEnd`) carries `classifierId`, `roleName`,
`multiplicity`, `constraint`, `qualifier` and `navigable`.

Builders return a fluent handle, which keeps the six labels a UML edge can carry
readable at the call site:

```cpp
model.AddGeneralization("Checking", "Account");     // triangle at the parent
model.AddRealization("Account", "IPayable");        // dashed triangle at the interface
model.AddComposition("Customer", "Loan")            // filled diamond at the whole
     .SetMultiplicity("1", "0..*");
model.AddAggregation("Bank", "Teller")              // hollow diamond at the whole
     .SetMultiplicity("1", "1..*");
model.AddAssociation("Customer", "Account")
     .SetMultiplicity("1", "1..*")
     .SetRoles("+holder", "+accounts")
     .SetName("owns", UMLReadingDirection::SourceToTarget);
model.AddDependency("Teller", "Account", "use");    // dashed, «use»
```

Argument order follows the notation: `AddGeneralization(child, parent)`,
`AddComposition(whole, part)`, `AddDependency(client, supplier)`.

Ends accept either a classifier id or a classifier name — names are resolved to
ids on insert.

> The handle points into the model's relationship vector and is invalidated by
> the next `Add*`/`Remove*` call. Use it immediately, as in the examples above.

### Queries and maintenance

| Call | Purpose |
|---|---|
| `GetClassifier(id)` / `FindClassifierByName(name)` | Lookup |
| `RemoveClassifier(id)` | Removes the classifier **and every relationship touching it** |
| `GetParents(id)` / `GetChildren(id)` | Follow generalization edges |
| `GetRootClassifiers()` | Classifiers with no parent — the roots for a layered layout |
| `GetRelationshipsFor(id)` | Every edge touching a classifier |
| `HasRelationship(kind, sourceId, targetId)` | Duplicate check |
| `Merge(other)` | Merge another model; colliding ids are skipped, edges get fresh ids |
| `RemoveDanglingRelationships()` | Drop edges whose ends do not resolve |
| `Validate()` | Diagnose without modifying |

`Validate()` reports duplicate classifier names, interfaces that declare
attributes, dangling relationship ends, missing association classes, duplicate
relationships and inheritance cycles, each as a `UMLDiagnostic` with a severity
of `Info`, `Warning` or `Error`.

---

## 2. The notation grammar

`UMLNotation` converts between the text form and `UMLMember`, so demos stay
short while export and validation keep structured data.

```cpp
UMLMember member;
UMLNotation::ParseMember("+fees [0..1] : String", member);
// visibility = Public, name = "fees", multiplicity = "0..1", type = "String"

std::string text = UMLNotation::FormatMember(member);   // "+fees [0..1]: String"
```

Accepted forms (everything but the name is optional):

```
attribute:  [+-#~] [/]name [ '[' multiplicity ']' ] [: Type] [ '[' multiplicity ']' ]
                         [= default] [{property}]
operation:  [+-#~] name '(' [param [, param]*] ')' [: ReturnType] [{property}]
parameter:  [in|out|inout] name : Type [= default]     — or just  Type
literal:    NAME [= value]
```

- `(` outside brackets means the row is an operation; otherwise it is an
  attribute.
- `{static}` and `{abstract}` become flags; any other property string is
  preserved verbatim.
- Multiplicity is accepted before or after the type — both spellings occur in
  the wild.
- `::` and `,` inside a type do not split it: `~registry: std::map<std::string, Node>`
  parses as one type.
- A missing visibility glyph yields `UMLVisibility::Unspecified`. UML draws no
  glyph in that case, and inventing "public" would misreport the source.

Helpers: `FormatSignature` (no visibility glyph — good for tooltips),
`FormatParameters`, `VisibilityFromGlyph` / `VisibilityToGlyph`,
`NormalizeMultiplicity` (strips the decorative `+` of the `+1..*` spelling) and
`IsValidMultiplicity`.

Parse → format → parse is stable for every accepted form.

---

## 3. C++ reverse engineering

Builds a model from C++ headers. **This is not a C++ front end** — it is a
brace- and declaration-level scanner covering the subset of C++ that headers
actually use. It does not expand macros, evaluate `#if`, resolve
typedefs/using-aliases, instantiate templates, or understand SFINAE.
Declarations it cannot make sense of are **skipped, not guessed at**, and every
skip is counted in `CppReverseEngineerResult::declarationsSkipped`.

```cpp
#include "Plugins/Diagrams/UltraCanvasCppReverseEngineer.h"
#include "Plugins/Diagrams/UltraCanvasUMLTextExport.h"

UltraCanvasUMLModel model;

CppReverseEngineerOptions options;
options.includePrivateMembers   = false;
options.maxMembersPerCompartment = 8;

UltraCanvasCppReverseEngineer engineer(options);
CppReverseEngineerResult result =
    engineer.ParseDirectory("UltraCanvas/include/Plugins/Diagrams", true, model);

// Always run this once, after every file has been parsed.
size_t dropped = engineer.ResolveInferredRelationships(model);

std::string plantUml = UMLTextExport::ToPlantUML(model);
```

Entry points: `ParseSource(text, model, name)`, `ParseFile(path, model)`,
`ParseFiles(paths, model)`, `ParseDirectory(dir, recursive, model)`.

### What it recognises

- `class` / `struct` / `enum class` / `enum` definitions, including nested ones,
  and skipping forward declarations
- base-clauses with access specifiers, `virtual` bases and template bases
- `final`, and export/attribute macros between the keyword and the name
  (`class ULTRACANVAS_API Widget final : public Base`)
- access sections, with the correct `class`/`struct` default
- data members: multiple declarators on one line, initialisers, array bounds
  (which become a multiplicity), bit-fields
- member functions: `virtual`, `static`, `const` (→ `{query}`), `override`,
  `final`, pure-virtual `= 0` (→ abstract), `= default`, `= delete` (skipped),
  trailing return types, parameter names, types and defaults
- namespaces, optionally prefixed onto the classifier name

### Classification

- All operations pure virtual **and** no data members → `Interface`, with the
  `«interface»` stereotype (`detectInterfaces`).
- Any pure virtual operation → `AbstractClass` (`detectAbstractClasses`).

### Inferred relationships

Base clauses become `Generalization` — upgraded to `Realization` when the base
turns out to be an interface, which is the UML distinction that changes the line
from solid to dashed.

With `inferAssociations` on, a data member whose type names another classifier
becomes an edge. With `inferOwnership` on (the default), the kind follows the
ownership expressed by the C++ type:

| Member type | Edge | Multiplicity |
|---|---|---|
| `Point origin` | Composition | `1` |
| `std::unique_ptr<Body> body` | Composition | `1` |
| `std::shared_ptr<Node> node` | Aggregation | `0..1` |
| `Widget* parent` | DirectedAssociation | `0..1` |
| `const Widget& owner` | DirectedAssociation | `1` |
| `std::vector<Item> items` | Composition | `*` |
| `std::vector<std::shared_ptr<Node>> kids` | Aggregation | `*` |
| `std::map<std::string, Node> index` | Composition | `*` (the mapped type) |
| `std::optional<Config> config` | Composition | `0..1` |
| `Point corners[4]` | Composition | `4` |

The member's own name becomes the target role name. Self-references produce no
edge. `ExtractReferencedType(declaredType, outCore, outMultiplicity, outOwnership)`
is public, so this mapping is directly testable.

`ResolveInferredRelationships` runs after parsing and drops every edge whose
target never appeared as a classifier — which is how `std::string`, `int` and
external library types stay out of the diagram. Its return value is the number
dropped.

### Options

| Option | Default | Effect |
|---|---|---|
| `includeAttributes` / `includeOperations` | `true` | Which compartments are filled |
| `includePrivateMembers` / `includeProtectedMembers` | `true` | Access filtering |
| `includeConstructors` | `false` | Constructors and destructors — noise on most diagrams |
| `includeOperators` | `false` | `operator==`, `operator<<`, … |
| `includeStaticMembers` | `true` | |
| `maxMembersPerCompartment` | `0` (unlimited) | Cap per compartment; the overflow is reported in `warnings`, never silently dropped |
| `includeClasses` / `includeStructs` / `includeEnums` / `includeNestedClasses` | `true` | Which classifiers are produced |
| `includeAnonymousTypes` | `false` | |
| `excludeNamePrefixes` | empty | Skip `detail`, `Impl`, … |
| `detectInterfaces` / `detectAbstractClasses` | `true` | Classification |
| `inferGeneralization` / `inferAssociations` / `inferOwnership` | `true` | Edge inference |
| `inferDependencies` | `false` | Parameter/return types → dependency edges. On a real codebase this is a hairball |
| `inferEdgesToEnumerations` | `false` | Enum-typed members do not sprout arrows; the enum is still a classifier and the member row still names its type |
| `recordSourceFile` | `true` | Provenance on each classifier |
| `qualifyNamesWithNamespace` | `false` | `UltraCanvas::Widget` instead of `Widget` |
| `fileExtensions` | `.h .hpp .hh .hxx` | `ParseDirectory` filter |

### Scale

Run over this repository's whole public header tree (217 headers) the scanner
produces 1,345 classifiers and 742 relationships, fails to interpret 55
declarations, and yields a model that passes `Validate()` with no errors.

---

## 4. Text export

```cpp
#include "Plugins/Diagrams/UltraCanvasUMLTextExport.h"

std::string plant   = UMLTextExport::ToPlantUML(model);
std::string mermaid = UMLTextExport::ToMermaid(model);
```

`TextExportOptions` toggles members, attributes, operations, stereotypes,
relationship labels, the title, the surrounding wrapper (`@startuml` /
`classDiagram`) and the indent string.

Arrow conventions:

| Relationship | PlantUML | Mermaid |
|---|---|---|
| Generalization | `Child --\|> Parent` | `Parent <\|-- Child` |
| Realization | `Class ..\|> Interface` | `Interface <\|.. Class` |
| Composition | `Whole *-- Part` | `Whole *-- Part` |
| Aggregation | `Whole o-- Part` | `Whole o-- Part` |
| Directed association | `A --> B` | `A --> B` |
| Association | `A -- B` | `A -- B` |
| Dependency | `A ..> B` | `A ..> B` |

Mermaid writes the parent first for inheritance, so generalization and
realization swap ends on export. Multiplicities are emitted as quoted end
labels; the association name, stereotype and role names are joined into the
`: label` text.

Import (PlantUML/Mermaid → model) is not part of this unit; it is items X2/X4 of
the proposal.

---

## 5. End to end

```cpp
UltraCanvasUMLModel model;
UltraCanvasCppReverseEngineer engineer;
engineer.ParseDirectory("UltraCanvas/include/Plugins/Diagrams", true, model);
engineer.ResolveInferredRelationships(model);
model.SetTitle("Diagram plugins");

for (const auto& diagnostic : model.Validate()) {
    if (diagnostic.severity == UMLDiagnosticSeverity::Error) {
        std::printf("error: %s\n", diagnostic.message.c_str());
    }
}

std::printf("%s", UMLTextExport::ToPlantUML(model).c_str());
```

## See also

- [`UltraCanvasClassDiagramProposal.md`](UltraCanvasClassDiagramProposal.md) —
  the full feature list and roadmap for the rendering element
- [`UltraCanvasDiagramRouting.md`](UltraCanvasDiagramRouting.md) — the shared
  orthogonal router the element will use
