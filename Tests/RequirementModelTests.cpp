// Tests/RequirementModelTests.cpp
// Unit tests for the SysML requirement model (Plugins/Diagrams).
//
// Standalone executable (target: RequirementModelTests) that compiles only
// UltraCanvasRequirementModel.cpp, so it runs headless without the UI stack.
// Exit code 0 = all tests passed.

#include "Plugins/Diagrams/UltraCanvasRequirementModel.h"

#include <cstdio>
#include <string>

using namespace UltraCanvas;

static int testsRun = 0;
static int testsFailed = 0;

#define CHECK(condition) do { \
    ++testsRun; \
    if (!(condition)) { \
        ++testsFailed; \
        std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #condition); \
    } \
} while (0)

// =============================================================================
// STRING ROUND TRIPS
// =============================================================================

static void TestStringConversions() {
    CHECK(RequirementRiskFromString("High") == RequirementRisk::High);
    CHECK(RequirementRiskFromString("  low  ") == RequirementRisk::Low);
    CHECK(RequirementRiskFromString("nonsense") == RequirementRisk::Unspecified);
    CHECK(std::string(RequirementRiskToString(RequirementRisk::Medium)) == "Medium");

    CHECK(RequirementVerifyMethodFromString("demonstration") ==
          RequirementVerifyMethod::Demonstration);
    CHECK(std::string(RequirementVerifyMethodToString(RequirementVerifyMethod::Inspection)) ==
          "Inspection");

    CHECK(RequirementNodeKindFromString("testCase") == RequirementNodeKind::TestCase);
    CHECK(RequirementNodeKindFromString("TESTCASE") == RequirementNodeKind::TestCase);
    CHECK(RequirementNodeKindFromString("") == RequirementNodeKind::Requirement);

    // Both the SysML spelling and the Mermaid keyword must map to one kind.
    CHECK(RequirementRelationKindFromString("satisfy") == RequirementRelationKind::Satisfy);
    CHECK(RequirementRelationKindFromString("satisfies") == RequirementRelationKind::Satisfy);
    CHECK(RequirementRelationKindFromString("contains") == RequirementRelationKind::Containment);
    CHECK(RequirementRelationKindFromString("derives") == RequirementRelationKind::DeriveReqt);

    // Containment and generalisation carry notation, not a keyword label.
    CHECK(std::string(RequirementRelationLabel(RequirementRelationKind::Containment)).empty());
    CHECK(std::string(RequirementRelationLabel(RequirementRelationKind::Verify)) == "verify");
}

// =============================================================================
// NODES & RELATIONS
// =============================================================================

static void TestNodeBasics() {
    RequirementModel model;
    int warnings = 0;
    model.onWarning = [&warnings](const RequirementWarning&) { ++warnings; };

    CHECK(model.AddRequirement("R1", "Root", "The system shall exist."));
    CHECK(model.GetNodeCount() == 1);
    CHECK(!model.AddRequirement("R1", "Dup", "dup"));      // duplicate id
    CHECK(warnings == 1);
    CHECK(!model.AddNode(RequirementNode()));               // empty id
    CHECK(warnings == 2);

    CHECK(model.HasNode("R1"));
    CHECK(model.GetNode("R1") != nullptr);
    CHECK(model.GetNode("nope") == nullptr);

    // GenerateNodeId never collides with an existing key.
    model.AddRequirement("REQ1", "Taken", "");
    CHECK(model.GenerateNodeId("REQ") != "REQ1");

    CHECK(model.RemoveNode("R1"));
    CHECK(!model.RemoveNode("R1"));
    CHECK(model.GetNodeCount() == 1);
}

static void TestDisplayId() {
    RequirementNode node("internal_key", "Emissions");
    CHECK(node.DisplayId() == "internal_key");
    // An authored id from Mermaid/CSV is what the id row must show.
    node.externalId = "R1.2.1";
    CHECK(node.DisplayId() == "R1.2.1");
    CHECK(RequirementModel::ResolveField(node, RequirementField::Id) == "R1.2.1");
    CHECK(RequirementModel::ResolveField(node, RequirementField::ExternalId) == "R1.2.1");
    CHECK(RequirementModel::ResolveField(node, RequirementField::Name) == "Emissions");
}

static void TestPropertyFormatting() {
    CHECK(RequirementModel::FormatPropertyRow("id", "R1",
              RequirementPropertyFormat::KeyEqualsQuotedValue) == "id=\"R1\"");
    CHECK(RequirementModel::FormatPropertyRow("id", "R1",
              RequirementPropertyFormat::KeyColonValue) == "id: R1");
}

static void TestRelationBasics() {
    RequirementModel model;
    model.AddRequirement("R1", "Root", "");
    model.AddRequirement("R1.1", "Child", "");
    model.AddNode(RequirementNodeKind::TestCase, "TC1", "Test");

    const std::string containment = model.AddContainment("R1", "R1.1");
    CHECK(!containment.empty());
    CHECK(model.GetRelationCount() == 1);
    CHECK(model.GetParentId("R1.1") == "R1");
    CHECK(model.GetChildIds("R1").size() == 1);
    CHECK(model.GetRootIds().size() == 2);          // R1 and TC1

    // An unknown endpoint is refused, not silently dropped into the model.
    CHECK(model.AddRelation(RequirementRelationKind::Trace, "R1", "ghost").empty());
    CHECK(model.GetRelationCount() == 1);

    CHECK(!model.AddRelation(RequirementRelationKind::Verify, "TC1", "R1.1").empty());
    CHECK(model.GetRelationsOf("R1.1").size() == 2);

    // Deleting a node takes its relations with it.
    CHECK(model.RemoveNode("R1.1"));
    CHECK(model.GetRelationCount() == 0);
}

static void TestRename() {
    RequirementModel model;
    model.AddRequirement("OLD", "Old", "");
    model.AddRequirement("OTHER", "Other", "");
    model.AddNode(RequirementNodeKind::Note, "N1", "note");
    model.AddContainment("OLD", "OTHER");
    RequirementCallout callout("C1", "OLD", 0, 0);
    model.AddCallout(callout);
    if (RequirementNode* note = model.GetNode("N1")) note->anchorId = "OLD";

    CHECK(model.RenameNode("OLD", "NEW"));
    CHECK(model.GetNode("OLD") == nullptr);
    CHECK(model.GetNode("NEW") != nullptr);
    CHECK(model.GetNode("NEW")->id == "NEW");
    // Every reference follows the key.
    CHECK(model.GetParentId("OTHER") == "NEW");
    CHECK(model.GetCallout("C1")->targetNodeId == "NEW");
    CHECK(model.GetNode("N1")->anchorId == "NEW");
    // Order is preserved, not reshuffled.
    CHECK(model.GetNodeOrder()[0] == "NEW");

    CHECK(!model.RenameNode("NEW", "OTHER"));   // target taken
    CHECK(!model.RenameNode("ghost", "X"));
    CHECK(model.RenameNode("NEW", "NEW"));      // no-op is success
}

// =============================================================================
// ENDPOINT SEMANTICS
// =============================================================================

static void TestEndpointLegality() {
    // Only a test case verifies.
    CHECK(RequirementModel::IsLegalEndpointPair(RequirementRelationKind::Verify,
              RequirementNodeKind::TestCase, RequirementNodeKind::Requirement));
    CHECK(!RequirementModel::IsLegalEndpointPair(RequirementRelationKind::Verify,
              RequirementNodeKind::Block, RequirementNodeKind::Requirement));
    // A design element satisfies; a requirement does not satisfy a requirement.
    CHECK(RequirementModel::IsLegalEndpointPair(RequirementRelationKind::Satisfy,
              RequirementNodeKind::Block, RequirementNodeKind::Requirement));
    CHECK(!RequirementModel::IsLegalEndpointPair(RequirementRelationKind::Satisfy,
              RequirementNodeKind::Requirement, RequirementNodeKind::Requirement));
    // deriveReqt is requirement-to-requirement.
    CHECK(RequirementModel::IsLegalEndpointPair(RequirementRelationKind::DeriveReqt,
              RequirementNodeKind::Requirement, RequirementNodeKind::Requirement));
    CHECK(!RequirementModel::IsLegalEndpointPair(RequirementRelationKind::DeriveReqt,
              RequirementNodeKind::Block, RequirementNodeKind::Requirement));
    // Trace is deliberately unconstrained - that is what makes it ambiguous.
    CHECK(RequirementModel::IsLegalEndpointPair(RequirementRelationKind::Trace,
              RequirementNodeKind::Actor, RequirementNodeKind::Block));
    // Generalisation stays within one kind.
    CHECK(!RequirementModel::IsLegalEndpointPair(RequirementRelationKind::Generalization,
              RequirementNodeKind::Block, RequirementNodeKind::Requirement));
}

static void TestDirectionCorrection() {
    RequirementModel model;
    std::vector<RequirementWarning> warnings;
    model.onWarning = [&warnings](const RequirementWarning& w) { warnings.push_back(w); };

    model.AddRequirement("R1", "Req", "");
    model.AddNode(RequirementNodeKind::TestCase, "TC1", "Test");

    // Written backwards: requirement -> test case. The reverse is legal, so it
    // is flipped rather than rejected.
    const std::string id = model.AddRelation(RequirementRelationKind::Verify, "R1", "TC1");
    CHECK(!id.empty());
    const RequirementRelation* relation = model.GetRelation(id);
    CHECK(relation != nullptr);
    CHECK(relation->sourceId == "TC1");
    CHECK(relation->targetId == "R1");
    CHECK(warnings.size() == 1);
    CHECK(warnings[0].kind == RequirementWarning::Kind::DirectionCorrected);
}

static void TestStrictSemantics() {
    RequirementModel model;
    model.AddNode(RequirementNodeKind::Block, "B1", "Block");
    model.AddNode(RequirementNodeKind::Actor, "A1", "Actor");

    // Neither direction is legal for verify, so it cannot be auto-corrected.
    std::vector<RequirementWarning> warnings;
    model.onWarning = [&warnings](const RequirementWarning& w) { warnings.push_back(w); };

    const std::string lenient = model.AddRelation(RequirementRelationKind::Verify, "B1", "A1");
    CHECK(!lenient.empty());                                // accepted...
    CHECK(model.GetRelation(lenient)->illegal);             // ...but flagged
    CHECK(warnings.size() == 1);
    CHECK(warnings[0].kind == RequirementWarning::Kind::IllegalEndpointKind);

    model.SetSemanticsMode(RequirementSemanticsMode::Strict);
    CHECK(model.AddRelation(RequirementRelationKind::Verify, "B1", "A1").empty());
    CHECK(model.GetRelationCount() == 1);                   // the strict one was refused
}

// =============================================================================
// INTEGRITY
// =============================================================================

static void TestCycleDetection() {
    RequirementModel model;
    model.AddRequirement("A", "A", "");
    model.AddRequirement("B", "B", "");
    model.AddRequirement("C", "C", "");
    model.AddContainment("A", "B");
    model.AddContainment("B", "C");
    CHECK(!model.HasContainmentCycle());
    CHECK(model.GetContainmentCycleNodes().empty());

    model.AddContainment("C", "A");
    CHECK(model.HasContainmentCycle());
    CHECK(model.GetContainmentCycleNodes().size() == 3);

    // Descendants must terminate even with the cycle present.
    CHECK(model.GetDescendants("A").size() == 3);
}

static void TestValidation() {
    RequirementModel model;
    model.AddRequirement("R1", "One", "");
    model.AddRequirement("R2", "Two", "");
    model.AddRequirement("R3", "Three", "");
    model.AddContainment("R1", "R3");
    model.AddContainment("R2", "R3");        // second parent

    const std::vector<RequirementWarning> warnings = model.Validate();
    bool sawMultipleParents = false;
    for (const auto& w : warnings) {
        if (w.kind == RequirementWarning::Kind::MultipleParents && w.nodeId == "R3") {
            sawMultipleParents = true;
        }
    }
    CHECK(sawMultipleParents);
}

static void TestHierarchicalIds() {
    RequirementModel model;
    model.AddRequirement("root", "Root", "");
    model.AddRequirement("a", "A", "");
    model.AddRequirement("b", "B", "");
    model.AddRequirement("a1", "A1", "");
    model.AddContainment("root", "a");
    model.AddContainment("root", "b");
    model.AddContainment("a", "a1");

    const int renamed = model.AssignHierarchicalIds("R");
    CHECK(renamed == 4);
    CHECK(model.HasNode("R1"));
    CHECK(model.HasNode("R1.1"));
    CHECK(model.HasNode("R1.2"));
    CHECK(model.HasNode("R1.1.1"));
    // Structure survives the rewrite.
    CHECK(model.GetParentId("R1.1.1") == "R1.1");
    CHECK(model.GetChildIds("R1").size() == 2);
    CHECK(model.GetNode("R1")->name == "Root");

    // A cyclic model is refused rather than half-renamed.
    RequirementModel cyclic;
    cyclic.AddRequirement("X", "X", "");
    cyclic.AddRequirement("Y", "Y", "");
    cyclic.AddContainment("X", "Y");
    cyclic.AddContainment("Y", "X");
    CHECK(cyclic.AssignHierarchicalIds("R") == 0);
    CHECK(cyclic.HasNode("X"));
}

// =============================================================================
// DERIVED-ELEMENT COMPARTMENTS
// =============================================================================

static void TestDerivedElements() {
    RequirementModel model;
    model.AddRequirement("R1", "Req", "");
    model.AddRequirement("R2", "Derived", "");
    model.AddNode(RequirementNodeKind::Block, "B1", "Block");
    model.AddNode(RequirementNodeKind::TestCase, "TC1", "Test");

    model.AddRelation(RequirementRelationKind::Satisfy, "B1", "R1");
    model.AddRelation(RequirementRelationKind::Verify, "TC1", "R1");
    model.AddRelation(RequirementRelationKind::DeriveReqt, "R2", "R1");

    const auto satisfiedBy = model.GetDerivedElements("R1", RequirementDerivedList::SatisfiedBy);
    CHECK(satisfiedBy.size() == 1 && satisfiedBy[0] == "B1");

    const auto verifiedBy = model.GetDerivedElements("R1", RequirementDerivedList::VerifiedBy);
    CHECK(verifiedBy.size() == 1 && verifiedBy[0] == "TC1");

    // "derived" is read incoming, "derivedFrom" outgoing - opposite ends of
    // the same relation.
    const auto derived = model.GetDerivedElements("R1", RequirementDerivedList::Derived);
    CHECK(derived.size() == 1 && derived[0] == "R2");
    const auto derivedFrom = model.GetDerivedElements("R2", RequirementDerivedList::DerivedFrom);
    CHECK(derivedFrom.size() == 1 && derivedFrom[0] == "R1");

    CHECK(model.GetDerivedElements("R1", RequirementDerivedList::RefinedBy).empty());
}

// =============================================================================
// TRACEABILITY ANALYSIS
// =============================================================================

static void TestCoverage() {
    RequirementModel model;
    model.AddRequirement("R1", "Covered", "");
    model.AddRequirement("R2", "Unverified", "");
    model.AddRequirement("R3", "Orphan", "");
    model.AddNode(RequirementNodeKind::Block, "B1", "Block");
    model.AddNode(RequirementNodeKind::TestCase, "TC1", "Test");

    model.AddRelation(RequirementRelationKind::Satisfy, "B1", "R1");
    model.AddRelation(RequirementRelationKind::Verify, "TC1", "R1");
    model.AddRelation(RequirementRelationKind::Satisfy, "B1", "R2");

    const auto uncovered = model.GetUncoveredRequirements();
    CHECK(uncovered.size() == 1 && uncovered[0] == "R3");

    const auto unverified = model.GetUnverifiedRequirements();
    CHECK(unverified.size() == 2);              // R2 and R3

    const auto orphans = model.GetOrphanRequirements();
    CHECK(orphans.size() == 1 && orphans[0] == "R3");

    const RequirementCoverage coverage = model.GetCoverage();
    // Blocks and test cases are not requirements and must not be counted.
    CHECK(coverage.requirementCount == 3);
    CHECK(coverage.satisfiedCount == 2);
    CHECK(coverage.verifiedCount == 1);
    CHECK(coverage.orphanCount == 1);
    CHECK(coverage.satisfiedPercent > 66.0 && coverage.satisfiedPercent < 67.0);

    // Per-category slicing.
    if (RequirementNode* node = model.GetNode("R1")) node->category = "Safety";
    const RequirementCoverage safety = model.GetCoverageForCategory("Safety");
    CHECK(safety.requirementCount == 1);
    CHECK(safety.satisfiedCount == 1);
    CHECK(safety.verifiedPercent == 100.0);
}

static void TestTraceChain() {
    RequirementModel model;
    model.AddRequirement("R1", "Top", "");
    model.AddRequirement("R2", "Mid", "");
    model.AddNode(RequirementNodeKind::Block, "B1", "Block");

    model.AddRelation(RequirementRelationKind::DeriveReqt, "R2", "R1");  // R2 -> R1
    model.AddRelation(RequirementRelationKind::Satisfy, "B1", "R2");     // B1 -> R2

    // Upstream from B1 follows the arrows: B1 -> R2 -> R1.
    const auto upstream = model.GetTraceChain("B1", RequirementTraceDirection::Upstream);
    CHECK(upstream.size() == 3);

    // One hop only.
    const auto oneHop = model.GetTraceChain("B1", RequirementTraceDirection::Upstream, 1);
    CHECK(oneHop.size() == 2);
    CHECK(oneHop.count("R2") == 1);
    CHECK(oneHop.count("R1") == 0);

    // Downstream from R1 walks backwards to everything depending on it.
    const auto downstream = model.GetTraceChain("R1", RequirementTraceDirection::Downstream);
    CHECK(downstream.size() == 3);

    CHECK(model.GetTraceChain("ghost", RequirementTraceDirection::Both).empty());
}

// =============================================================================
// MERMAID INTERCHANGE
// =============================================================================

static void TestMermaidImport() {
    const std::string source = R"(requirementDiagram

direction TB

requirement test_req {
    id: 1
    text: the test text.
    risk: high
    verifymethod: test
}

functionalRequirement test_req2 {
    id: 1.1
    text: the second test text.
    risk: low
    verifymethod: inspection
}

element test_entity {
    type: simulation
    docref: reference
}

test_entity - satisfies -> test_req2
test_req - traces -> test_req2
test_req <- derives - test_req2
)";

    RequirementModel model;
    std::string error, direction;
    CHECK(model.FromMermaid(source, &error, &direction));
    CHECK(error.empty());
    CHECK(direction == "TB");
    CHECK(model.GetNodeCount() == 3);

    const RequirementNode* req = model.GetNode("test_req");
    CHECK(req != nullptr);
    CHECK(req->kind == RequirementNodeKind::Requirement);
    CHECK(req->externalId == "1");
    CHECK(req->text == "the test text.");
    CHECK(req->risk == RequirementRisk::High);
    CHECK(req->verifyMethod == RequirementVerifyMethod::Test);

    const RequirementNode* req2 = model.GetNode("test_req2");
    CHECK(req2 != nullptr);
    CHECK(req2->stereotype == "functionalRequirement");
    CHECK(req2->risk == RequirementRisk::Low);

    const RequirementNode* entity = model.GetNode("test_entity");
    CHECK(entity != nullptr);
    CHECK(entity->kind != RequirementNodeKind::Requirement);
    CHECK(entity->docRef == "reference");

    CHECK(model.GetRelationCount() == 3);
    // The reversed arrow form must produce source=test_req2, target=test_req.
    bool sawReversedDerive = false;
    for (const auto& r : model.GetRelations()) {
        if (r.kind == RequirementRelationKind::DeriveReqt &&
            r.sourceId == "test_req2" && r.targetId == "test_req") {
            sawReversedDerive = true;
        }
    }
    CHECK(sawReversedDerive);
}

static void TestMermaidRoundTrip() {
    RequirementModel model;
    RequirementNode req("safety", "safety");
    req.externalId = "R1";
    req.text = "The system shall be safe.";
    req.risk = RequirementRisk::High;
    req.verifyMethod = RequirementVerifyMethod::Analysis;
    req.stereotype = "performanceRequirement";
    model.AddNode(req);
    model.AddNode(RequirementNodeKind::TestCase, "safety_test", "safety_test");
    model.AddRelation(RequirementRelationKind::Verify, "safety_test", "safety");

    const std::string exported = model.ToMermaid("LR");

    RequirementModel reimported;
    std::string error, direction;
    CHECK(reimported.FromMermaid(exported, &error, &direction));
    CHECK(direction == "LR");
    CHECK(reimported.GetNodeCount() == model.GetNodeCount());
    CHECK(reimported.GetRelationCount() == model.GetRelationCount());

    const RequirementNode* round = reimported.GetNode("safety");
    CHECK(round != nullptr);
    CHECK(round->externalId == "R1");
    CHECK(round->text == "The system shall be safe.");
    CHECK(round->risk == RequirementRisk::High);
    CHECK(round->verifyMethod == RequirementVerifyMethod::Analysis);
    CHECK(round->stereotype == "performanceRequirement");

    // A second export of the reimported model must be identical - that is what
    // "round-trips through Mermaid unchanged" means.
    CHECK(reimported.ToMermaid("LR") == exported);
}

static void TestMermaidErrors() {
    RequirementModel model;
    std::string error;
    CHECK(!model.FromMermaid("", &error));
    CHECK(!error.empty());

    error.clear();
    CHECK(!model.FromMermaid("flowchart TD\n A --> B\n", &error));
    CHECK(!error.empty());

    // A relationship naming an undeclared requirement still produces an edge:
    // Mermaid renders a bare box for it rather than dropping the line.
    error.clear();
    CHECK(model.FromMermaid("requirementDiagram\n a - satisfies -> b\n", &error));
    CHECK(model.GetNodeCount() == 2);
    CHECK(model.GetRelationCount() == 1);
}

// =============================================================================
// CSV INTERCHANGE
// =============================================================================

static void TestCsvLineParsing() {
    auto fields = RequirementModel::ParseCsvLine("a,b,c", ',');
    CHECK(fields.size() == 3 && fields[1] == "b");

    // Quoted field containing the delimiter.
    fields = RequirementModel::ParseCsvLine("a,\"b,still b\",c", ',');
    CHECK(fields.size() == 3);
    CHECK(fields[1] == "b,still b");

    // Escaped quotes inside a quoted field.
    fields = RequirementModel::ParseCsvLine("\"say \"\"hi\"\"\",x", ',');
    CHECK(fields.size() == 2);
    CHECK(fields[0] == "say \"hi\"");

    // Trailing empty field must survive.
    fields = RequirementModel::ParseCsvLine("a,b,", ',');
    CHECK(fields.size() == 3 && fields[2].empty());
}

static void TestCsvImport() {
    const std::string csv =
        "id,name,parent,text,risk,verifyMethod,kind\n"
        "R1,Root,,\"The system shall work, always.\",High,Analysis,requirement\n"
        "R1.1,Child,R1,Sub requirement.,Low,Test,requirement\n"
        "TC1,Acceptance Test,,,,,testCase\n";

    RequirementModel model;
    std::string error;
    CHECK(model.FromCsv(csv, RequirementCsvSchema::Default(), &error));
    CHECK(error.empty());
    CHECK(model.GetNodeCount() == 3);

    const RequirementNode* root = model.GetNode("R1");
    CHECK(root != nullptr);
    CHECK(root->name == "Root");
    CHECK(root->text == "The system shall work, always.");   // comma survived quoting
    CHECK(root->risk == RequirementRisk::High);
    CHECK(root->verifyMethod == RequirementVerifyMethod::Analysis);

    CHECK(model.GetParentId("R1.1") == "R1");
    CHECK(model.GetNode("TC1")->kind == RequirementNodeKind::TestCase);

    // A file without the id column cannot be mapped.
    RequirementModel bad;
    error.clear();
    CHECK(!bad.FromCsv("name,text\nfoo,bar\n", RequirementCsvSchema::Default(), &error));
    CHECK(!error.empty());
}

static void TestCsvRoundTrip() {
    RequirementModel model;
    RequirementNode node("R1", "Quoted \"Name\"");
    node.text = "Text with, comma and \"quotes\".";
    node.risk = RequirementRisk::Medium;
    node.category = "Safety";
    model.AddNode(node);
    model.AddRequirement("R2", "Child", "child text");
    model.AddContainment("R1", "R2");

    const std::string csv = model.ToCsv();

    RequirementModel reimported;
    std::string error;
    CHECK(reimported.FromCsv(csv, RequirementCsvSchema::Default(), &error));
    CHECK(reimported.GetNodeCount() == 2);

    const RequirementNode* round = reimported.GetNode("R1");
    CHECK(round != nullptr);
    CHECK(round->name == "Quoted \"Name\"");
    CHECK(round->text == "Text with, comma and \"quotes\".");
    CHECK(round->risk == RequirementRisk::Medium);
    CHECK(round->category == "Safety");
    CHECK(reimported.GetParentId("R2") == "R1");
    CHECK(reimported.ToCsv() == csv);
}

// =============================================================================
// TEMPLATES
// =============================================================================

static void TestTemplates() {
    const RequirementNodeTemplate extended = RequirementNodeTemplate::Extended();
    CHECK(extended.rows.size() == 5);
    CHECK(extended.rows[0].field == RequirementField::Id);
    CHECK(extended.format == RequirementPropertyFormat::KeyEqualsQuotedValue);

    const RequirementNodeTemplate standard = RequirementNodeTemplate::Standard();
    CHECK(standard.rows.size() == 2);
    CHECK(standard.derivedCompartments.empty());

    const RequirementNodeTemplate traced = RequirementNodeTemplate::WithTraceCompartments();
    CHECK(traced.derivedCompartments.size() == 2);
    CHECK(!traced.IsEmpty());

    RequirementNodeTemplate custom;
    CHECK(custom.IsEmpty());
    custom.AddCustomRow("owner", "allocatedTo");
    CHECK(custom.rows.size() == 1);
    CHECK(custom.rows[0].customKey == "allocatedTo");

    RequirementNode node("R1", "R");
    node.customProperties["allocatedTo"] = "Powertrain";
    CHECK(RequirementModel::ResolveField(node, RequirementField::Custom, "allocatedTo") ==
          "Powertrain");
    CHECK(RequirementModel::ResolveField(node, RequirementField::Custom, "missing").empty());
    CHECK(RequirementModel::ResolveField(node, RequirementField::Custom, "", "literal") ==
          "literal");
}

// =============================================================================
// MAIN
// =============================================================================

int main() {
    TestStringConversions();
    TestNodeBasics();
    TestDisplayId();
    TestPropertyFormatting();
    TestRelationBasics();
    TestRename();
    TestEndpointLegality();
    TestDirectionCorrection();
    TestStrictSemantics();
    TestCycleDetection();
    TestValidation();
    TestHierarchicalIds();
    TestDerivedElements();
    TestCoverage();
    TestTraceChain();
    TestMermaidImport();
    TestMermaidRoundTrip();
    TestMermaidErrors();
    TestCsvLineParsing();
    TestCsvImport();
    TestCsvRoundTrip();
    TestTemplates();

    std::printf("RequirementModelTests: %d checks, %d failed\n", testsRun, testsFailed);
    return testsFailed == 0 ? 0 : 1;
}
