// include/Plugins/Diagrams/UltraCanvasCppReverseEngineer.h
// Builds a UML model from C++ headers (heuristic scanner)
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework
//
// SCOPE — read this before filing a bug.
//
// This is deliberately NOT a C++ front end. It is a brace- and
// declaration-level scanner covering the subset of C++ that this codebase (and
// most application code) actually uses in headers:
//
//   * class / struct / enum class / enum definitions, including nested ones
//   * base-clauses with access specifiers, virtual bases and template bases
//   * access sections (public/protected/private, with the class/struct default)
//   * data members, including multiple declarators, initialisers and arrays
//   * member functions, including virtual / static / const / override / final,
//     pure-virtual (= 0), defaulted and deleted, and trailing return types
//   * templates (the parameter list is recorded, the body is parsed normally)
//   * namespaces (used for scoping; the qualified name is recorded)
//
// It does NOT expand macros, evaluate #if, resolve typedefs/using-aliases,
// instantiate templates, or understand SFINAE. Declarations it cannot make
// sense of are skipped, not guessed at — a missing member is a better outcome
// than a wrong one. Every skip is counted in CppReverseEngineerResult.
//
// Typical use — document a subsystem:
//
//     UltraCanvasUMLModel model;
//     UltraCanvasCppReverseEngineer engineer;
//     engineer.ParseDirectory("UltraCanvas/include/Plugins/Diagrams", true, model);
//     engineer.ResolveInferredRelationships(model);
//     std::string plantUml = UMLTextExport::ToPlantUML(model);
//
// CHANGELOG 1.0.0:
//  - Initial implementation: classifier extraction, member extraction,
//    generalization from base-clauses, interface/abstract detection, and
//    association/aggregation/composition inference from member types.

#pragma once

#include "Plugins/Diagrams/UltraCanvasUMLModel.h"

#include <string>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// OPTIONS
// =============================================================================

struct CppReverseEngineerOptions {
    // ---- which members reach the diagram ----
    bool includeAttributes = true;
    bool includeOperations = true;
    bool includePrivateMembers = true;
    bool includeProtectedMembers = true;
    bool includeConstructors = false;   // ctors/dtors are noise on most diagrams
    bool includeOperators = false;      // operator==, operator<<, ...
    bool includeStaticMembers = true;
    // 0 = unlimited. Otherwise the first N members of a compartment are kept
    // and the overflow is reported in CppReverseEngineerResult::warnings.
    size_t maxMembersPerCompartment = 0;

    // ---- which classifiers reach the diagram ----
    bool includeClasses = true;
    bool includeStructs = true;
    bool includeEnums = true;
    bool includeNestedClasses = true;
    bool includeAnonymousTypes = false;
    // Classifiers whose (unqualified) name starts with any of these are
    // skipped — the usual "detail", "Impl", "Private" implementation noise.
    std::vector<std::string> excludeNamePrefixes;

    // ---- classification ----
    // A class with no data members whose operations are all pure virtual is
    // recorded as Interface («interface») rather than AbstractClass.
    bool detectInterfaces = true;
    // A class with at least one pure virtual operation becomes AbstractClass.
    bool detectAbstractClasses = true;

    // ---- relationship inference ----
    // Base-clauses become Generalization edges (Realization when the base is
    // an interface).
    bool inferGeneralization = true;
    // Data members whose type names another classifier become edges.
    bool inferAssociations = true;
    // Refines the inferred edge by ownership:
    //   by value, unique_ptr  -> Composition
    //   shared_ptr            -> Aggregation
    //   raw pointer, reference-> DirectedAssociation
    // With this off, every inferred edge is a DirectedAssociation.
    bool inferOwnership = true;
    // Operation parameter and return types become Dependency edges. Off by
    // default: on a real codebase this produces a hairball.
    bool inferDependencies = false;
    // Draw an edge when a member's type is an enumeration. Off by default:
    // strictly speaking a by-value enum member IS composition, but every
    // enum-typed field then sprouts an arrow and the diagram turns to noise.
    // The enum still appears as a classifier, and the member row still names
    // its type.
    bool inferEdgesToEnumerations = false;

    // ---- provenance ----
    bool recordSourceFile = true;
    // Prefix the classifier name with its enclosing namespaces ("UltraCanvas::X").
    bool qualifyNamesWithNamespace = false;

    // ---- file discovery (ParseDirectory) ----
    std::vector<std::string> fileExtensions = {".h", ".hpp", ".hh", ".hxx"};
};

// =============================================================================
// RESULT
// =============================================================================

struct CppReverseEngineerResult {
    size_t filesParsed = 0;
    size_t classifiersAdded = 0;
    size_t relationshipsAdded = 0;
    size_t declarationsSkipped = 0;   // declarations the scanner did not understand
    std::vector<std::string> warnings;
    std::vector<std::string> unreadableFiles;

    void Merge(const CppReverseEngineerResult& other);
};

// =============================================================================
// ENGINE
// =============================================================================

class UltraCanvasCppReverseEngineer {
public:
    UltraCanvasCppReverseEngineer() = default;
    explicit UltraCanvasCppReverseEngineer(const CppReverseEngineerOptions& options)
        : options(options) {}

    const CppReverseEngineerOptions& Options() const { return options; }
    void SetOptions(const CppReverseEngineerOptions& value) { options = value; }

    // Parses one translation unit's worth of text. `sourceName` is recorded on
    // every classifier found (provenance) and used in warnings.
    CppReverseEngineerResult ParseSource(const std::string& sourceText,
                                         UltraCanvasUMLModel& model,
                                         const std::string& sourceName = std::string());

    CppReverseEngineerResult ParseFile(const std::string& filePath,
                                       UltraCanvasUMLModel& model);

    CppReverseEngineerResult ParseFiles(const std::vector<std::string>& filePaths,
                                        UltraCanvasUMLModel& model);

    // Scans a directory for files matching options.fileExtensions.
    CppReverseEngineerResult ParseDirectory(const std::string& directoryPath,
                                            bool recursive,
                                            UltraCanvasUMLModel& model);

    // Second pass, run once after every file has been parsed:
    //   * re-resolves relationship ends that named a classifier defined in a
    //     later file,
    //   * drops edges to types that never appeared (std::string, int, external
    //     libraries),
    //   * upgrades Generalization to Realization when the parent turned out to
    //     be an interface.
    // Returns the number of relationships removed.
    size_t ResolveInferredRelationships(UltraCanvasUMLModel& model);

    // ---- exposed for testing / reuse -------------------------------------

    // Replaces comments with spaces, blanks string and character literals, and
    // removes preprocessor directives (including line continuations). Newlines
    // are preserved so byte offsets still map to plausible line numbers.
    static std::string StripCommentsAndDirectives(const std::string& source);

    // Splits a declared C++ type into the classifier it refers to and the UML
    // multiplicity implied by its container.
    //
    //   "std::vector<std::shared_ptr<Node>>" -> core "Node",   mult "*"
    //   "std::unique_ptr<Body>"              -> core "Body",   mult "1"
    //   "const Widget&"                      -> core "Widget", mult "1"
    //   "int"                                -> core "int",    mult "1"
    //
    // `outOwnership` reports how the type holds its referent, which drives the
    // Composition / Aggregation / DirectedAssociation choice.
    enum class TypeOwnership { Value, UniquePointer, SharedPointer, RawPointer, Reference };
    static bool ExtractReferencedType(const std::string& declaredType,
                                      std::string& outCoreType,
                                      std::string& outMultiplicity,
                                      TypeOwnership& outOwnership);

private:
    CppReverseEngineerOptions options;

    // Relationship ends recorded by name during parsing, resolved afterwards.
    struct PendingEdge {
        UMLRelationshipKind kind;
        std::string sourceClassifierId;
        std::string targetTypeName;   // unqualified
        std::string roleName;
        std::string multiplicity;
    };
    std::vector<PendingEdge> pendingEdges;
};

} // namespace UltraCanvas
