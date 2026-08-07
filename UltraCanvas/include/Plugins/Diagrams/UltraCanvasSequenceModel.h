// include/Plugins/Diagrams/UltraCanvasSequenceModel.h
// UML sequence diagram model: lifelines, messages, fragments and notes
// Version: 1.0.0
// Last Modified: 2026-08-07
// Author: UltraCanvas Framework
//
// This is the semantic model behind the sequence-diagram feature. Like
// UltraCanvasUMLModel (the class-diagram model) it is deliberately UI-free —
// it includes nothing from the widget or render stack — so that:
//   * text export (UltraCanvasSequenceTextExport.h) is a pure function of the
//     model,
//   * activation nesting and message numbering are computed here and
//     unit-tested without a window (Tests/SequenceModelTest.cpp),
//   * the renderer element (UltraCanvasSequenceDiagram) owns only layout and
//     geometry. Nothing here knows how tall a row is.
//
// The vertical dimension of a sequence diagram is pure order: every vertical
// position derives from a message index. Fragments and notes therefore anchor
// to message indices, not to coordinates.
//
// CHANGELOG 1.0.0:
//  - Initial model: SequenceLifelineKind, SequenceMessageKind,
//    SequenceFragmentKind, SequenceLifeline, SequenceMessage,
//    SequenceFragment, SequenceNote, UltraCanvasSequenceModel container with
//    activation computation, hierarchical message numbering and a validation
//    pass.

#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace UltraCanvas {

// =============================================================================
// ENUMERATIONS
// =============================================================================

// What the head of a lifeline looks like. Object is the plain named box;
// Boundary / Control / Entity are the Jacobson robustness icons.
enum class SequenceLifelineKind {
    Object,     // rectangle head: "orderForm : OrderForm"
    Actor,      // stick figure
    Boundary,   // circle attached to a vertical bar
    Control,    // circle with an arrow on its rim
    Entity,     // circle standing on a line
    Database    // cylinder
};

enum class SequenceMessageKind {
    Synchronous,   // solid line, filled triangle head
    Asynchronous,  // solid line, open (stick) arrowhead
    Return,        // dashed line, open arrowhead; ends the sender's activation
    Create,        // dashed line, open arrowhead, points at the target's head box
    Destroy,       // solid line, filled triangle, X at the target's lifeline end
    Found,         // solid line starting at a filled circle (sender unknown)
    Lost           // solid line ending at a filled circle (receiver unknown)
};

// UML 2 combined-fragment operators (the ones that matter on real diagrams).
// Alt and Par take multiple operands separated by dashed lines; the others
// have exactly one operand.
enum class SequenceFragmentKind {
    Loop,      // "loop" — guard is the iteration condition
    Alt,       // "alt" — mutually exclusive operands, one guard each
    Opt,       // "opt" — single guarded operand
    Par,       // "par" — concurrent operands
    Break,     // "break" — operand replaces the rest of the enclosing scope
    Critical   // "critical" — atomic region
};

enum class SequenceNotePlacement {
    LeftOf,
    RightOf,
    Over       // centred on one lifeline, or spanning two
};

enum class SequenceDiagnosticSeverity { Info, Warning, Error };

// =============================================================================
// LIFELINES
// =============================================================================

struct SequenceLifeline {
    std::string id;
    std::string name;         // "Player1", "aNurse : Nurse"
    std::string stereotype;   // drawn as «stereotype» above the name; optional
    SequenceLifelineKind kind = SequenceLifelineKind::Object;

    // Draw an execution bar from the top of the lifeline (the usual look of
    // the driving participant, which is never activated by a visible message).
    bool activateOnStart = false;

    // Presentation override, carried but not interpreted here. RGBA packed as
    // 0xRRGGBBAA, matching UMLClassifierStyle's convention.
    unsigned int headColor = 0;
    bool hasHeadColor = false;

    SequenceLifeline() = default;
    SequenceLifeline(const std::string& lifelineId, const std::string& lifelineName,
                     SequenceLifelineKind lifelineKind)
        : id(lifelineId), name(lifelineName), kind(lifelineKind) {}
};

// =============================================================================
// MESSAGES
// =============================================================================

// One horizontal arrow. Messages are stored in occurrence order; the index in
// UltraCanvasSequenceModel::Messages() IS the vertical order of the diagram.
struct SequenceMessage {
    std::string fromId;   // empty only for Found
    std::string toId;     // empty only for Lost
    std::string label;    // "TakeCards", "1.1: allocateBed" is generated, not stored
    SequenceMessageKind kind = SequenceMessageKind::Synchronous;

    bool IsSelfMessage() const { return !fromId.empty() && fromId == toId; }
};

// =============================================================================
// COMBINED FRAGMENTS
// =============================================================================

// One guarded section of a fragment. The first operand starts at the
// fragment's firstMessage; each further operand (alt/par only) starts at its
// own firstMessage inside the fragment's range.
struct SequenceFragmentOperand {
    std::string guard;        // rendered "[guard]"; may be empty ("else")
    size_t firstMessage = 0;
};

struct SequenceFragment {
    std::string id;
    SequenceFragmentKind kind = SequenceFragmentKind::Loop;
    std::string label;        // extra text after the operator keyword; optional

    // Inclusive message-index range the frame surrounds.
    size_t firstMessage = 0;
    size_t lastMessage = 0;

    std::vector<SequenceFragmentOperand> operands;  // never empty

    const char* Keyword() const;   // "loop", "alt", ...
};

// =============================================================================
// NOTES
// =============================================================================

struct SequenceNote {
    std::string id;
    std::string text;
    SequenceNotePlacement placement = SequenceNotePlacement::RightOf;
    std::string lifelineId;         // anchor
    std::string secondLifelineId;   // Over may span to here; optional
    int afterMessage = -1;          // vertical anchor: below message i; -1 = top
};

// =============================================================================
// DERIVED DATA
// =============================================================================

// One execution-specification bar, computed from the message sequence by
// ComputeActivations(). Message indices; -1 means "top of the lifeline" for
// start and "bottom of the lifeline" for end.
struct SequenceActivation {
    std::string lifelineId;
    int startMessage = -1;
    int endMessage = -1;
    int depth = 0;          // 0 = directly on the lifeline; >0 = nested bar
};

struct SequenceDiagnostic {
    SequenceDiagnosticSeverity severity = SequenceDiagnosticSeverity::Warning;
    std::string message;
    std::string subjectId;      // lifeline / fragment / note id, when applicable
    int messageIndex = -1;      // subject message, when applicable
};

// =============================================================================
// MODEL CONTAINER
// =============================================================================

class UltraCanvasSequenceModel {
public:
    // ---- title ----------------------------------------------------------
    void SetTitle(const std::string& value) { title = value; }
    const std::string& Title() const { return title; }

    // ---- lifelines ------------------------------------------------------
    // All return the new lifeline's id. Names do not have to be unique, but
    // non-unique names cannot be used for name-based lookup.
    std::string AddLifeline(const std::string& name,
                            SequenceLifelineKind kind = SequenceLifelineKind::Object);
    std::string AddActor(const std::string& name);
    std::string AddDatabase(const std::string& name);

    SequenceLifeline* GetLifeline(const std::string& idOrName);
    const SequenceLifeline* GetLifeline(const std::string& idOrName) const;
    const std::vector<SequenceLifeline>& Lifelines() const { return lifelines; }
    size_t LifelineCount() const { return lifelines.size(); }
    // Position in Lifelines(), or -1. Accepts an id or a name.
    int LifelineIndex(const std::string& idOrName) const;

    // Removes the lifeline, every message touching it, and re-anchors
    // fragments and notes past the removed messages.
    bool RemoveLifeline(const std::string& idOrName);

    void SetActivateOnStart(const std::string& idOrName, bool value);
    void SetLifelineColor(const std::string& idOrName, unsigned int rgba);

    // ---- messages -------------------------------------------------------
    // Endpoints accept a lifeline id or name; names resolve on insert.
    // All return the new message's index — the value fragments anchor to.
    size_t AddMessage(const std::string& from, const std::string& to,
                      const std::string& label,
                      SequenceMessageKind kind = SequenceMessageKind::Synchronous);
    size_t AddSynchronousMessage(const std::string& from, const std::string& to,
                                 const std::string& label);
    size_t AddAsynchronousMessage(const std::string& from, const std::string& to,
                                  const std::string& label);
    size_t AddReturnMessage(const std::string& from, const std::string& to,
                            const std::string& label = std::string());
    size_t AddCreateMessage(const std::string& from, const std::string& to,
                            const std::string& label = "\xC2\xAB" "create\xC2\xBB");
    size_t AddDestroyMessage(const std::string& from, const std::string& to,
                             const std::string& label = "\xC2\xAB" "destroy\xC2\xBB");
    size_t AddSelfMessage(const std::string& lifeline, const std::string& label);
    size_t AddFoundMessage(const std::string& to, const std::string& label);
    size_t AddLostMessage(const std::string& from, const std::string& label);

    SequenceMessage* GetMessage(size_t index);
    const SequenceMessage* GetMessage(size_t index) const;
    const std::vector<SequenceMessage>& Messages() const { return messages; }
    size_t MessageCount() const { return messages.size(); }

    // Removes one message and re-anchors fragments and notes behind it.
    // A fragment whose range becomes empty is removed with it.
    bool RemoveMessage(size_t index);

    // ---- fragments ------------------------------------------------------
    // Ranges are inclusive message indices. All return the fragment's id.
    std::string AddFragment(SequenceFragmentKind kind, size_t firstMessage,
                            size_t lastMessage, const std::string& guard = std::string());
    std::string AddLoop(size_t firstMessage, size_t lastMessage,
                        const std::string& guard = std::string());
    std::string AddOpt(size_t firstMessage, size_t lastMessage,
                       const std::string& guard = std::string());
    std::string AddAlt(size_t firstMessage, size_t lastMessage,
                       const std::string& firstGuard = std::string());
    std::string AddPar(size_t firstMessage, size_t lastMessage);
    std::string AddBreak(size_t firstMessage, size_t lastMessage,
                         const std::string& guard = std::string());
    std::string AddCritical(size_t firstMessage, size_t lastMessage);

    // Starts a new operand (alt: "else [guard]", par: next parallel band) at
    // the given message, which must lie inside the fragment's range.
    bool AddFragmentOperand(const std::string& fragmentId, const std::string& guard,
                            size_t firstMessage);

    SequenceFragment* GetFragment(const std::string& fragmentId);
    const SequenceFragment* GetFragment(const std::string& fragmentId) const;
    const std::vector<SequenceFragment>& Fragments() const { return fragments; }
    bool RemoveFragment(const std::string& fragmentId);

    // ---- notes ----------------------------------------------------------
    std::string AddNote(const std::string& text, SequenceNotePlacement placement,
                        const std::string& lifeline, int afterMessage = -1);
    std::string AddNoteOver(const std::string& text, const std::string& firstLifeline,
                            const std::string& secondLifeline, int afterMessage = -1);
    const std::vector<SequenceNote>& Notes() const { return notes; }
    bool RemoveNote(const std::string& noteId);

    // ---- derived data ---------------------------------------------------
    // Execution bars, computed from the message order:
    //   * Synchronous / Asynchronous / Found arrival opens a bar on the
    //     receiver (Create does not — a new object is not yet executing),
    //   * Return closes the sender's innermost open bar,
    //   * Destroy closes every open bar on the target,
    //   * activateOnStart opens a bar at the top,
    //   * a self-message's bar with no matching self-return stays one row.
    // Bars still open at the last message run to the bottom (endMessage -1).
    std::vector<SequenceActivation> ComputeActivations() const;

    // One label per message. Sequential: "1", "2", ... Hierarchical follows
    // the call nesting: a synchronous call numbered 1.1 numbers its nested
    // calls 1.1.1, 1.1.2, and its reply takes the next child number too —
    // matching the Visual Paradigm convention.
    std::vector<std::string> ComputeMessageNumbers(bool hierarchical) const;

    // Non-destructive diagnosis: dangling endpoints, messages to a lifeline
    // before its create or after its destroy, returns with nothing to end,
    // malformed fragment ranges, fragments that cross without nesting,
    // dangling note anchors, duplicate lifeline names.
    std::vector<SequenceDiagnostic> Validate() const;

    void Clear();

private:
    std::string title;
    std::vector<SequenceLifeline> lifelines;
    std::vector<SequenceMessage> messages;
    std::vector<SequenceFragment> fragments;
    std::vector<SequenceNote> notes;

    int nextLifelineId = 1;
    int nextFragmentId = 1;
    int nextNoteId = 1;

    // Returns the id for an id-or-name, or an empty string.
    std::string ResolveLifeline(const std::string& idOrName) const;
    // Shifts every message anchor > removedIndex down by one; drops fragments
    // and notes anchored at the removed message when nothing remains of them.
    void ReanchorAfterRemoval(size_t removedIndex);
};

} // namespace UltraCanvas
