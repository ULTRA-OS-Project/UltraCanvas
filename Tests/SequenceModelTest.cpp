// Tests/SequenceModelTest.cpp
// Unit tests for the sequence diagram model: activation computation, message
// numbering, fragment/note bookkeeping, validation and PlantUML/Mermaid export.
//
// Exercises the real model code with no UI stack — the whole point of keeping
// the model free of widget dependencies.
//
// Version: 1.0.0
// Last Modified: 2026-08-07
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasSequenceModel.h"
#include "Plugins/Diagrams/UltraCanvasSequenceTextExport.h"

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

// A login flow used by several tests:
//   student ->  screen   : click on login button          (0)
//   screen  ->  validate : ValidateUser(userid, password) (1)
//   validate -> database : checkUser()                    (2)
//   database -> validate : return matched                 (3)
//   validate -> screen   : return user valid              (4)
//   screen  ->  student  : return show welcome            (5)
static UltraCanvasSequenceModel LoginModel() {
    UltraCanvasSequenceModel model;
    model.SetTitle("User Login");
    model.AddActor("Student");
    model.AddLifeline("Login Screen");
    model.AddLifeline("Validate User");
    model.AddDatabase("Database");

    model.AddSynchronousMessage("Student", "Login Screen", "click on login button");
    model.AddSynchronousMessage("Login Screen", "Validate User", "ValidateUser(userid, password)");
    model.AddSynchronousMessage("Validate User", "Database", "checkUser()");
    model.AddReturnMessage("Database", "Validate User", "matched");
    model.AddReturnMessage("Validate User", "Login Screen", "user valid");
    model.AddReturnMessage("Login Screen", "Student", "show welcome");
    return model;
}

// =============================================================================
// BASICS
// =============================================================================

static void TestBuilding() {
    std::printf("Model building\n");
    UltraCanvasSequenceModel model = LoginModel();

    CHECK(model.LifelineCount() == 4, "four lifelines");
    CHECK(model.MessageCount() == 6, "six messages");
    CHECK(model.GetLifeline("Student") != nullptr, "lookup by name");
    CHECK(model.GetLifeline("l1") != nullptr, "lookup by id");
    CHECK(model.GetLifeline("l1") == model.GetLifeline("Student"),
          "name and id resolve to the same lifeline");
    CHECK(model.LifelineIndex("Database") == 3, "lifeline order preserved");
    CHECK(model.GetLifeline("Student")->kind == SequenceLifelineKind::Actor,
          "actor kind stored");
    CHECK(model.GetLifeline("Database")->kind == SequenceLifelineKind::Database,
          "database kind stored");

    const SequenceMessage* first = model.GetMessage(0);
    CHECK(first && first->fromId == "l1" && first->toId == "l2",
          "message endpoints resolved from names to ids");
    CHECK(model.GetMessage(3)->kind == SequenceMessageKind::Return, "return kind stored");
    CHECK(model.Validate().empty(), "well-formed model validates clean");
}

static void TestSelfAndGates() {
    std::printf("Self messages and gates\n");
    UltraCanvasSequenceModel model;
    model.AddLifeline("aBed");
    size_t self = model.AddSelfMessage("aBed", "isFree");
    CHECK(model.GetMessage(self)->IsSelfMessage(), "self message detected");

    size_t found = model.AddFoundMessage("aBed", "external ping");
    CHECK(model.GetMessage(found)->fromId.empty(), "found message has no sender");
    size_t lost = model.AddLostMessage("aBed", "fire and forget");
    CHECK(model.GetMessage(lost)->toId.empty(), "lost message has no receiver");
    CHECK(model.Validate().empty(), "found/lost endpoints do not trip validation");
}

// =============================================================================
// ACTIVATIONS
// =============================================================================

static void TestActivations() {
    std::printf("Activation computation\n");
    UltraCanvasSequenceModel model = LoginModel();
    model.SetActivateOnStart("Student", true);

    auto bars = model.ComputeActivations();
    // Student (top bar), Login Screen, Validate User, Database.
    CHECK(bars.size() == 4, "four execution bars");

    const SequenceActivation* student = nullptr;
    const SequenceActivation* screen = nullptr;
    const SequenceActivation* database = nullptr;
    for (const auto& bar : bars) {
        if (bar.lifelineId == "l1") student = &bar;
        if (bar.lifelineId == "l2") screen = &bar;
        if (bar.lifelineId == "l4") database = &bar;
    }
    CHECK(student && student->startMessage == -1 && student->endMessage == -1,
          "activateOnStart bar runs top to bottom");
    CHECK(screen && screen->startMessage == 0 && screen->endMessage == 5,
          "screen bar spans call to reply");
    CHECK(database && database->startMessage == 2 && database->endMessage == 3,
          "database bar spans checkUser to matched");
    for (const auto& bar : bars) {
        CHECK(bar.depth == 0, "no nesting in a linear flow");
        if (bar.depth != 0) break;
    }
}

static void TestNestedActivations() {
    std::printf("Nested activations\n");
    UltraCanvasSequenceModel model;
    model.AddLifeline("A");
    model.AddLifeline("B");
    model.AddSynchronousMessage("A", "B", "outer");   // 0: opens B depth 0
    model.AddSelfMessage("B", "inner");               // 1: opens B depth 1
    model.AddReturnMessage("B", "B", "inner done");   // 2: closes depth 1
    model.AddReturnMessage("B", "A", "outer done");   // 3: closes depth 0

    auto bars = model.ComputeActivations();
    CHECK(bars.size() == 2, "two bars on B");
    CHECK(bars[0].depth == 0 && bars[0].startMessage == 0 && bars[0].endMessage == 3,
          "outer bar spans messages 0..3");
    CHECK(bars[1].depth == 1 && bars[1].startMessage == 1 && bars[1].endMessage == 2,
          "self-call nests one level deeper");

    // An unanswered self-call collapses to a one-row stub.
    UltraCanvasSequenceModel stub;
    stub.AddLifeline("A");
    stub.AddLifeline("B");
    stub.AddSynchronousMessage("A", "B", "call");
    stub.AddSelfMessage("B", "check");
    auto stubBars = stub.ComputeActivations();
    CHECK(stubBars.size() == 2, "stub model has two bars");
    CHECK(stubBars[1].startMessage == 1 && stubBars[1].endMessage == 1,
          "unmatched self-call becomes a stub");
    CHECK(stubBars[0].endMessage == -1, "unanswered call runs to the bottom");

    // Destroy closes every open bar on the target.
    UltraCanvasSequenceModel destroy;
    destroy.AddLifeline("A");
    destroy.AddLifeline("B");
    destroy.AddSynchronousMessage("A", "B", "work");
    destroy.AddDestroyMessage("A", "B");
    auto destroyBars = destroy.ComputeActivations();
    CHECK(destroyBars.size() == 1 && destroyBars[0].endMessage == 1,
          "destroy ends the target's open bar");
}

// =============================================================================
// NUMBERING
// =============================================================================

static void TestNumbering() {
    std::printf("Message numbering\n");
    UltraCanvasSequenceModel model = LoginModel();

    auto flat = model.ComputeMessageNumbers(false);
    CHECK(flat.size() == 6 && flat[0] == "1" && flat[5] == "6", "sequential numbering");

    // Hierarchical, Visual Paradigm style:
    //   0 click            -> 1        (opens level)
    //   1 ValidateUser     -> 1.1      (opens level)
    //   2 checkUser        -> 1.1.1    (opens level)
    //   3 return matched   -> 1.1.1.1  (numbered inside, then pops)
    //   4 return valid     -> 1.1.2    (numbered inside, then pops)
    //   5 return welcome   -> 1.2      (numbered inside, then pops)
    auto nested = model.ComputeMessageNumbers(true);
    CHECK(nested[0] == "1", "first call is 1");
    CHECK(nested[1] == "1.1", "nested call is 1.1");
    CHECK(nested[2] == "1.1.1", "doubly nested call is 1.1.1");
    CHECK(nested[3] == "1.1.1.1", "reply numbered inside the activation it ends");
    CHECK(nested[4] == "1.1.2", "numbering pops with each reply");
    CHECK(nested[5] == "1.2", "outermost reply is 1.2");
}

// =============================================================================
// FRAGMENTS
// =============================================================================

static void TestFragments() {
    std::printf("Fragments\n");
    UltraCanvasSequenceModel model;
    model.AddLifeline("StoreFront");
    model.AddLifeline("Cart");
    model.AddLifeline("Inventory");

    size_t add = model.AddSynchronousMessage("StoreFront", "Cart", "AddItem");
    model.AddSynchronousMessage("Cart", "Inventory", "ReserveItem");
    model.AddReturnMessage("Inventory", "Cart");
    size_t ok = model.AddReturnMessage("Cart", "StoreFront");
    size_t checkout = model.AddSynchronousMessage("StoreFront", "Cart", "Checkout");
    size_t placed = model.AddReturnMessage("Cart", "StoreFront", "ConfirmOrder");

    std::string loop = model.AddLoop(add, ok, "more items");
    CHECK(model.GetFragment(loop) != nullptr, "loop fragment stored");
    CHECK(std::string(model.GetFragment(loop)->Keyword()) == "loop", "loop keyword");
    CHECK(model.Validate().empty(), "well-nested fragment validates clean");

    std::string alt = model.AddAlt(checkout, placed, "cart not empty");
    CHECK(model.AddFragmentOperand(alt, "cart empty", placed), "alt gains an else operand");
    CHECK(!model.AddFragmentOperand(alt, "bad", checkout),
          "operand at the fragment start is rejected");
    CHECK(model.GetFragment(alt)->operands.size() == 2, "two operands stored");
    CHECK(model.Validate().empty(), "alt with two operands validates clean");

    // Partial overlap must be diagnosed.
    UltraCanvasSequenceModel bad;
    bad.AddLifeline("A");
    bad.AddLifeline("B");
    bad.AddSynchronousMessage("A", "B", "m0");
    bad.AddSynchronousMessage("A", "B", "m1");
    bad.AddSynchronousMessage("A", "B", "m2");
    bad.AddLoop(0, 1);
    bad.AddOpt(1, 2);
    bool overlapReported = false;
    for (const auto& diagnostic : bad.Validate()) {
        if (Contains(diagnostic.message, "overlap")) overlapReported = true;
    }
    CHECK(overlapReported, "partially overlapping fragments are diagnosed");

    // Loop with several operands is un-UML.
    UltraCanvasSequenceModel weird;
    weird.AddLifeline("A");
    weird.AddLifeline("B");
    weird.AddSynchronousMessage("A", "B", "m0");
    weird.AddSynchronousMessage("A", "B", "m1");
    std::string wrongLoop = weird.AddLoop(0, 1);
    weird.AddFragmentOperand(wrongLoop, "again", 1);
    bool operandWarning = false;
    for (const auto& diagnostic : weird.Validate()) {
        if (Contains(diagnostic.message, "multiple operands")) operandWarning = true;
    }
    CHECK(operandWarning, "multi-operand loop draws a warning");
}

// =============================================================================
// REMOVAL / RE-ANCHORING
// =============================================================================

static void TestRemoval() {
    std::printf("Removal and re-anchoring\n");
    UltraCanvasSequenceModel model;
    model.AddLifeline("A");
    model.AddLifeline("B");
    model.AddLifeline("C");
    model.AddSynchronousMessage("A", "B", "m0");
    model.AddSynchronousMessage("B", "C", "m1");
    model.AddReturnMessage("C", "B", "m2");
    model.AddReturnMessage("B", "A", "m3");
    std::string loop = model.AddLoop(1, 2, "retry");
    model.AddNote("after m3", SequenceNotePlacement::RightOf, "A", 3);

    model.RemoveMessage(0);
    CHECK(model.MessageCount() == 3, "message removed");
    const SequenceFragment* fragment = model.GetFragment(loop);
    CHECK(fragment && fragment->firstMessage == 0 && fragment->lastMessage == 1,
          "fragment range shifts with removal");
    CHECK(model.Notes()[0].afterMessage == 2, "note anchor shifts with removal");

    // Removing the only covered message removes the fragment.
    UltraCanvasSequenceModel single;
    single.AddLifeline("A");
    single.AddLifeline("B");
    single.AddSynchronousMessage("A", "B", "only");
    single.AddLoop(0, 0);
    single.RemoveMessage(0);
    CHECK(single.Fragments().empty(), "fragment dies with its only message");

    // Removing a lifeline removes its messages and notes.
    UltraCanvasSequenceModel byLifeline;
    byLifeline.AddLifeline("A");
    byLifeline.AddLifeline("B");
    byLifeline.AddLifeline("C");
    byLifeline.AddSynchronousMessage("A", "B", "keep? no");
    byLifeline.AddSynchronousMessage("A", "C", "keep");
    byLifeline.AddNote("gone", SequenceNotePlacement::LeftOf, "B");
    byLifeline.RemoveLifeline("B");
    CHECK(byLifeline.LifelineCount() == 2, "lifeline removed");
    CHECK(byLifeline.MessageCount() == 1 &&
              byLifeline.GetMessage(0)->label == "keep",
          "messages touching the lifeline removed");
    CHECK(byLifeline.Notes().empty(), "notes anchored to the lifeline removed");
}

// =============================================================================
// VALIDATION
// =============================================================================

static void TestValidation() {
    std::printf("Validation\n");
    UltraCanvasSequenceModel model;
    model.AddLifeline("A");
    model.AddSynchronousMessage("A", "Ghost", "into the void");
    bool danglingReported = false;
    for (const auto& diagnostic : model.Validate()) {
        if (diagnostic.severity == SequenceDiagnosticSeverity::Error &&
            Contains(diagnostic.message, "unresolved receiver")) {
            danglingReported = true;
        }
    }
    CHECK(danglingReported, "dangling receiver is an error");

    UltraCanvasSequenceModel lifecycle;
    lifecycle.AddLifeline("A");
    lifecycle.AddLifeline("B");
    lifecycle.AddSynchronousMessage("A", "B", "too early");
    lifecycle.AddCreateMessage("A", "B");
    lifecycle.AddDestroyMessage("A", "B");
    lifecycle.AddSynchronousMessage("A", "B", "too late");
    int lifecycleWarnings = 0;
    for (const auto& diagnostic : lifecycle.Validate()) {
        if (Contains(diagnostic.message, "not created until")) ++lifecycleWarnings;
        if (Contains(diagnostic.message, "destroyed at")) ++lifecycleWarnings;
    }
    CHECK(lifecycleWarnings == 2, "use before create and after destroy are diagnosed");

    UltraCanvasSequenceModel orphanReturn;
    orphanReturn.AddLifeline("A");
    orphanReturn.AddLifeline("B");
    orphanReturn.AddReturnMessage("B", "A", "from nowhere");
    bool returnReported = false;
    for (const auto& diagnostic : orphanReturn.Validate()) {
        if (Contains(diagnostic.message, "ends no activation")) returnReported = true;
    }
    CHECK(returnReported, "return without an activation is diagnosed");

    UltraCanvasSequenceModel duplicate;
    duplicate.AddLifeline("Twin");
    duplicate.AddLifeline("Twin");
    bool duplicateReported = false;
    for (const auto& diagnostic : duplicate.Validate()) {
        if (Contains(diagnostic.message, "duplicate lifeline name")) duplicateReported = true;
    }
    CHECK(duplicateReported, "duplicate names are diagnosed");
}

// =============================================================================
// TEXT EXPORT
// =============================================================================

static void TestPlantUMLExport() {
    std::printf("PlantUML export\n");
    UltraCanvasSequenceModel model = LoginModel();
    model.AddLoop(1, 4, "until valid");
    model.AddNote("passwords are hashed", SequenceNotePlacement::RightOf,
                  "Validate User", 2);

    std::string plant = SequenceTextExport::ToPlantUML(model);
    CHECK(Contains(plant, "@startuml") && Contains(plant, "@enduml"), "wrapper present");
    CHECK(Contains(plant, "title User Login"), "title exported");
    CHECK(Contains(plant, "actor \"Student\" as l1"), "actor declared");
    CHECK(Contains(plant, "database \"Database\" as l4"), "database declared");
    CHECK(Contains(plant, "l1 -> l2 : click on login button"), "sync arrow");
    CHECK(Contains(plant, "l4 --> l3 : matched"), "return arrow is dashed");
    CHECK(Contains(plant, "loop [until valid]"), "loop with guard");
    CHECK(Contains(plant, "end"), "fragment closed");
    CHECK(Contains(plant, "activate l2"), "activation exported");
    CHECK(Contains(plant, "deactivate l2"), "deactivation exported");
    CHECK(Contains(plant, "note right of l3 : passwords are hashed"), "note exported");

    UltraCanvasSequenceModel lifecycle;
    lifecycle.AddLifeline("App");
    lifecycle.AddLifeline("Session");
    lifecycle.AddCreateMessage("App", "Session");
    lifecycle.AddDestroyMessage("App", "Session");
    lifecycle.AddLostMessage("App", "telemetry");
    lifecycle.AddFoundMessage("App", "signal");
    std::string text = SequenceTextExport::ToPlantUML(lifecycle);
    CHECK(Contains(text, "create participant \"Session\" as l2"), "create declared inline");
    CHECK(!Contains(text, "participant \"Session\" as l2\nparticipant"),
          "created lifeline not pre-declared");
    CHECK(Contains(text, "destroy l2"), "destroy exported");
    CHECK(Contains(text, "l1 ->]"), "lost message gate");
    CHECK(Contains(text, "[-> l1"), "found message gate");
}

static void TestMermaidExport() {
    std::printf("Mermaid export\n");
    UltraCanvasSequenceModel model = LoginModel();
    std::string alt = model.AddAlt(4, 5, "credentials ok");
    model.AddFragmentOperand(alt, "rejected", 5);

    SequenceTextExportOptions options;
    options.autonumber = true;
    std::string mermaid = SequenceTextExport::ToMermaid(model, options);
    CHECK(Contains(mermaid, "sequenceDiagram"), "header present");
    CHECK(Contains(mermaid, "title: User Login"), "title in front matter");
    CHECK(Contains(mermaid, "autonumber"), "autonumber emitted");
    CHECK(Contains(mermaid, "actor l1 as Student"), "actor declared");
    CHECK(Contains(mermaid, "l1->>l2: click on login button"), "sync arrow");
    CHECK(Contains(mermaid, "l3-->>l2: user valid"), "return arrow");
    CHECK(Contains(mermaid, "alt [credentials ok]"), "alt fragment");
    CHECK(Contains(mermaid, "else [rejected]"), "else operand");
    CHECK(Contains(mermaid, "end"), "fragment closed");

    size_t activates = 0;
    size_t deactivates = 0;
    size_t position = 0;
    while ((position = mermaid.find("activate ", position)) != std::string::npos) {
        if (position == 0 || mermaid[position - 1] != 'e') ++activates;
        position += 9;
    }
    position = 0;
    while ((position = mermaid.find("deactivate ", position)) != std::string::npos) {
        ++deactivates;
        position += 11;
    }
    CHECK(activates == deactivates, "every mermaid activate is balanced");
}

// =============================================================================
// MAIN
// =============================================================================

int main() {
    TestBuilding();
    TestSelfAndGates();
    TestActivations();
    TestNestedActivations();
    TestNumbering();
    TestFragments();
    TestRemoval();
    TestValidation();
    TestPlantUMLExport();
    TestMermaidExport();

    if (g_failures == 0) {
        std::printf("\nAll sequence model tests passed.\n");
        return 0;
    }
    std::printf("\n%d sequence model test(s) FAILED.\n", g_failures);
    return 1;
}
