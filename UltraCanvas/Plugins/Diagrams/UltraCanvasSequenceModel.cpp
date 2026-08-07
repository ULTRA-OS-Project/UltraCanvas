// Plugins/Diagrams/UltraCanvasSequenceModel.cpp
// UML sequence diagram model: lifelines, messages, fragments and notes
// Version: 1.0.0
// Last Modified: 2026-08-07
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasSequenceModel.h"

#include <algorithm>
#include <map>
#include <set>

namespace UltraCanvas {

// =============================================================================
// FRAGMENT KEYWORDS
// =============================================================================

const char* SequenceFragment::Keyword() const {
    switch (kind) {
        case SequenceFragmentKind::Loop:     return "loop";
        case SequenceFragmentKind::Alt:      return "alt";
        case SequenceFragmentKind::Opt:      return "opt";
        case SequenceFragmentKind::Par:      return "par";
        case SequenceFragmentKind::Break:    return "break";
        case SequenceFragmentKind::Critical: return "critical";
    }
    return "loop";
}

// =============================================================================
// LIFELINES
// =============================================================================

std::string UltraCanvasSequenceModel::AddLifeline(const std::string& name,
                                                  SequenceLifelineKind kind) {
    std::string id = "l" + std::to_string(nextLifelineId++);
    lifelines.emplace_back(id, name, kind);
    return id;
}

std::string UltraCanvasSequenceModel::AddActor(const std::string& name) {
    return AddLifeline(name, SequenceLifelineKind::Actor);
}

std::string UltraCanvasSequenceModel::AddDatabase(const std::string& name) {
    return AddLifeline(name, SequenceLifelineKind::Database);
}

std::string UltraCanvasSequenceModel::ResolveLifeline(const std::string& idOrName) const {
    if (idOrName.empty()) return std::string();
    for (const auto& lifeline : lifelines) {
        if (lifeline.id == idOrName) return lifeline.id;
    }
    for (const auto& lifeline : lifelines) {
        if (lifeline.name == idOrName) return lifeline.id;
    }
    return std::string();
}

SequenceLifeline* UltraCanvasSequenceModel::GetLifeline(const std::string& idOrName) {
    std::string id = ResolveLifeline(idOrName);
    if (id.empty()) return nullptr;
    for (auto& lifeline : lifelines) {
        if (lifeline.id == id) return &lifeline;
    }
    return nullptr;
}

const SequenceLifeline* UltraCanvasSequenceModel::GetLifeline(const std::string& idOrName) const {
    return const_cast<UltraCanvasSequenceModel*>(this)->GetLifeline(idOrName);
}

int UltraCanvasSequenceModel::LifelineIndex(const std::string& idOrName) const {
    std::string id = ResolveLifeline(idOrName);
    if (id.empty()) return -1;
    for (size_t i = 0; i < lifelines.size(); ++i) {
        if (lifelines[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

bool UltraCanvasSequenceModel::RemoveLifeline(const std::string& idOrName) {
    std::string id = ResolveLifeline(idOrName);
    if (id.empty()) return false;

    // Remove messages touching the lifeline, from the back so the indices of
    // the ones still to visit stay valid. RemoveMessage re-anchors fragments
    // and notes per removal.
    for (size_t i = messages.size(); i-- > 0;) {
        if (messages[i].fromId == id || messages[i].toId == id) {
            RemoveMessage(i);
        }
    }

    notes.erase(std::remove_if(notes.begin(), notes.end(),
                               [&](const SequenceNote& note) {
                                   return note.lifelineId == id ||
                                          note.secondLifelineId == id;
                               }),
                notes.end());

    lifelines.erase(std::remove_if(lifelines.begin(), lifelines.end(),
                                   [&](const SequenceLifeline& lifeline) {
                                       return lifeline.id == id;
                                   }),
                    lifelines.end());
    return true;
}

void UltraCanvasSequenceModel::SetActivateOnStart(const std::string& idOrName, bool value) {
    if (SequenceLifeline* lifeline = GetLifeline(idOrName)) {
        lifeline->activateOnStart = value;
    }
}

void UltraCanvasSequenceModel::SetLifelineColor(const std::string& idOrName, unsigned int rgba) {
    if (SequenceLifeline* lifeline = GetLifeline(idOrName)) {
        lifeline->headColor = rgba;
        lifeline->hasHeadColor = true;
    }
}

// =============================================================================
// MESSAGES
// =============================================================================

size_t UltraCanvasSequenceModel::AddMessage(const std::string& from, const std::string& to,
                                            const std::string& label,
                                            SequenceMessageKind kind) {
    SequenceMessage message;
    // Store the resolved id when the endpoint is known; keep the raw text
    // otherwise so Validate() can name the dangling reference.
    std::string fromId = ResolveLifeline(from);
    std::string toId = ResolveLifeline(to);
    message.fromId = fromId.empty() ? from : fromId;
    message.toId = toId.empty() ? to : toId;
    message.label = label;
    message.kind = kind;
    messages.push_back(std::move(message));
    return messages.size() - 1;
}

size_t UltraCanvasSequenceModel::AddSynchronousMessage(const std::string& from,
                                                       const std::string& to,
                                                       const std::string& label) {
    return AddMessage(from, to, label, SequenceMessageKind::Synchronous);
}

size_t UltraCanvasSequenceModel::AddAsynchronousMessage(const std::string& from,
                                                        const std::string& to,
                                                        const std::string& label) {
    return AddMessage(from, to, label, SequenceMessageKind::Asynchronous);
}

size_t UltraCanvasSequenceModel::AddReturnMessage(const std::string& from,
                                                  const std::string& to,
                                                  const std::string& label) {
    return AddMessage(from, to, label, SequenceMessageKind::Return);
}

size_t UltraCanvasSequenceModel::AddCreateMessage(const std::string& from,
                                                  const std::string& to,
                                                  const std::string& label) {
    return AddMessage(from, to, label, SequenceMessageKind::Create);
}

size_t UltraCanvasSequenceModel::AddDestroyMessage(const std::string& from,
                                                   const std::string& to,
                                                   const std::string& label) {
    return AddMessage(from, to, label, SequenceMessageKind::Destroy);
}

size_t UltraCanvasSequenceModel::AddSelfMessage(const std::string& lifeline,
                                                const std::string& label) {
    return AddMessage(lifeline, lifeline, label, SequenceMessageKind::Synchronous);
}

size_t UltraCanvasSequenceModel::AddFoundMessage(const std::string& to,
                                                 const std::string& label) {
    return AddMessage(std::string(), to, label, SequenceMessageKind::Found);
}

size_t UltraCanvasSequenceModel::AddLostMessage(const std::string& from,
                                                const std::string& label) {
    return AddMessage(from, std::string(), label, SequenceMessageKind::Lost);
}

SequenceMessage* UltraCanvasSequenceModel::GetMessage(size_t index) {
    return index < messages.size() ? &messages[index] : nullptr;
}

const SequenceMessage* UltraCanvasSequenceModel::GetMessage(size_t index) const {
    return index < messages.size() ? &messages[index] : nullptr;
}

bool UltraCanvasSequenceModel::RemoveMessage(size_t index) {
    if (index >= messages.size()) return false;
    messages.erase(messages.begin() + static_cast<long>(index));
    ReanchorAfterRemoval(index);
    return true;
}

void UltraCanvasSequenceModel::ReanchorAfterRemoval(size_t removedIndex) {
    // Fragments: shift, shrink, and drop the ones that covered only the
    // removed message.
    for (size_t i = fragments.size(); i-- > 0;) {
        SequenceFragment& fragment = fragments[i];
        if (fragment.firstMessage == fragment.lastMessage &&
            fragment.firstMessage == removedIndex) {
            fragments.erase(fragments.begin() + static_cast<long>(i));
            continue;
        }
        if (fragment.firstMessage > removedIndex) --fragment.firstMessage;
        if (fragment.lastMessage >= removedIndex && fragment.lastMessage > 0) {
            --fragment.lastMessage;
        }

        for (auto& operand : fragment.operands) {
            if (operand.firstMessage > removedIndex) --operand.firstMessage;
        }
        // Clamp into the new range and drop duplicates created by the shift.
        for (auto& operand : fragment.operands) {
            operand.firstMessage = std::clamp(operand.firstMessage,
                                              fragment.firstMessage,
                                              fragment.lastMessage);
        }
        fragment.operands.front().firstMessage = fragment.firstMessage;
        auto last = std::unique(fragment.operands.begin(), fragment.operands.end(),
                                [](const SequenceFragmentOperand& a,
                                   const SequenceFragmentOperand& b) {
                                    return a.firstMessage == b.firstMessage;
                                });
        fragment.operands.erase(last, fragment.operands.end());
    }

    for (auto& note : notes) {
        if (note.afterMessage >= static_cast<int>(removedIndex)) {
            --note.afterMessage;   // may become -1: the note moves to the top
        }
    }
}

// =============================================================================
// FRAGMENTS
// =============================================================================

std::string UltraCanvasSequenceModel::AddFragment(SequenceFragmentKind kind,
                                                  size_t firstMessage, size_t lastMessage,
                                                  const std::string& guard) {
    SequenceFragment fragment;
    fragment.id = "f" + std::to_string(nextFragmentId++);
    fragment.kind = kind;
    fragment.firstMessage = std::min(firstMessage, lastMessage);
    fragment.lastMessage = std::max(firstMessage, lastMessage);
    fragment.operands.push_back({guard, fragment.firstMessage});
    fragments.push_back(std::move(fragment));
    return fragments.back().id;
}

std::string UltraCanvasSequenceModel::AddLoop(size_t firstMessage, size_t lastMessage,
                                              const std::string& guard) {
    return AddFragment(SequenceFragmentKind::Loop, firstMessage, lastMessage, guard);
}

std::string UltraCanvasSequenceModel::AddOpt(size_t firstMessage, size_t lastMessage,
                                             const std::string& guard) {
    return AddFragment(SequenceFragmentKind::Opt, firstMessage, lastMessage, guard);
}

std::string UltraCanvasSequenceModel::AddAlt(size_t firstMessage, size_t lastMessage,
                                             const std::string& firstGuard) {
    return AddFragment(SequenceFragmentKind::Alt, firstMessage, lastMessage, firstGuard);
}

std::string UltraCanvasSequenceModel::AddPar(size_t firstMessage, size_t lastMessage) {
    return AddFragment(SequenceFragmentKind::Par, firstMessage, lastMessage, std::string());
}

std::string UltraCanvasSequenceModel::AddBreak(size_t firstMessage, size_t lastMessage,
                                               const std::string& guard) {
    return AddFragment(SequenceFragmentKind::Break, firstMessage, lastMessage, guard);
}

std::string UltraCanvasSequenceModel::AddCritical(size_t firstMessage, size_t lastMessage) {
    return AddFragment(SequenceFragmentKind::Critical, firstMessage, lastMessage, std::string());
}

bool UltraCanvasSequenceModel::AddFragmentOperand(const std::string& fragmentId,
                                                  const std::string& guard,
                                                  size_t firstMessage) {
    SequenceFragment* fragment = GetFragment(fragmentId);
    if (!fragment) return false;
    if (firstMessage <= fragment->firstMessage || firstMessage > fragment->lastMessage) {
        return false;
    }
    auto position = std::find_if(fragment->operands.begin(), fragment->operands.end(),
                                 [&](const SequenceFragmentOperand& operand) {
                                     return operand.firstMessage >= firstMessage;
                                 });
    if (position != fragment->operands.end() && position->firstMessage == firstMessage) {
        return false;
    }
    fragment->operands.insert(position, {guard, firstMessage});
    return true;
}

SequenceFragment* UltraCanvasSequenceModel::GetFragment(const std::string& fragmentId) {
    for (auto& fragment : fragments) {
        if (fragment.id == fragmentId) return &fragment;
    }
    return nullptr;
}

const SequenceFragment* UltraCanvasSequenceModel::GetFragment(const std::string& fragmentId) const {
    return const_cast<UltraCanvasSequenceModel*>(this)->GetFragment(fragmentId);
}

bool UltraCanvasSequenceModel::RemoveFragment(const std::string& fragmentId) {
    size_t before = fragments.size();
    fragments.erase(std::remove_if(fragments.begin(), fragments.end(),
                                   [&](const SequenceFragment& fragment) {
                                       return fragment.id == fragmentId;
                                   }),
                    fragments.end());
    return fragments.size() != before;
}

// =============================================================================
// NOTES
// =============================================================================

std::string UltraCanvasSequenceModel::AddNote(const std::string& text,
                                              SequenceNotePlacement placement,
                                              const std::string& lifeline,
                                              int afterMessage) {
    SequenceNote note;
    note.id = "n" + std::to_string(nextNoteId++);
    note.text = text;
    note.placement = placement;
    std::string id = ResolveLifeline(lifeline);
    note.lifelineId = id.empty() ? lifeline : id;
    note.afterMessage = afterMessage;
    notes.push_back(std::move(note));
    return notes.back().id;
}

std::string UltraCanvasSequenceModel::AddNoteOver(const std::string& text,
                                                  const std::string& firstLifeline,
                                                  const std::string& secondLifeline,
                                                  int afterMessage) {
    std::string id = AddNote(text, SequenceNotePlacement::Over, firstLifeline, afterMessage);
    std::string secondId = ResolveLifeline(secondLifeline);
    notes.back().secondLifelineId = secondId.empty() ? secondLifeline : secondId;
    return id;
}

bool UltraCanvasSequenceModel::RemoveNote(const std::string& noteId) {
    size_t before = notes.size();
    notes.erase(std::remove_if(notes.begin(), notes.end(),
                               [&](const SequenceNote& note) { return note.id == noteId; }),
                notes.end());
    return notes.size() != before;
}

// =============================================================================
// ACTIVATIONS
// =============================================================================

std::vector<SequenceActivation> UltraCanvasSequenceModel::ComputeActivations() const {
    std::vector<SequenceActivation> result;
    // Per lifeline: indices into `result` of the bars still open.
    std::map<std::string, std::vector<size_t>> open;
    // Bars opened by a self-message, so an unmatched one can be shortened to
    // a stub instead of running to the bottom.
    std::set<size_t> openedBySelf;

    for (const auto& lifeline : lifelines) {
        if (!lifeline.activateOnStart) continue;
        SequenceActivation activation;
        activation.lifelineId = lifeline.id;
        activation.startMessage = -1;
        activation.depth = 0;
        open[lifeline.id].push_back(result.size());
        result.push_back(activation);
    }

    auto openBar = [&](const std::string& lifelineId, int index, bool self) {
        SequenceActivation activation;
        activation.lifelineId = lifelineId;
        activation.startMessage = index;
        activation.depth = static_cast<int>(open[lifelineId].size());
        open[lifelineId].push_back(result.size());
        if (self) openedBySelf.insert(result.size());
        result.push_back(activation);
    };

    for (size_t i = 0; i < messages.size(); ++i) {
        const SequenceMessage& message = messages[i];
        int index = static_cast<int>(i);

        switch (message.kind) {
            case SequenceMessageKind::Return: {
                auto& stack = open[message.fromId];
                if (!stack.empty()) {
                    result[stack.back()].endMessage = index;
                    openedBySelf.erase(stack.back());
                    stack.pop_back();
                }
                break;
            }
            case SequenceMessageKind::Destroy: {
                auto& stack = open[message.toId];
                while (!stack.empty()) {
                    result[stack.back()].endMessage = index;
                    openedBySelf.erase(stack.back());
                    stack.pop_back();
                }
                break;
            }
            case SequenceMessageKind::Synchronous:
            case SequenceMessageKind::Asynchronous:
            case SequenceMessageKind::Found:
                openBar(message.toId, index, message.IsSelfMessage());
                break;
            case SequenceMessageKind::Create:
            case SequenceMessageKind::Lost:
                break;
        }
    }

    // A self-call nobody replied to becomes a one-row stub; everything else
    // still open runs to the bottom.
    for (size_t barIndex : openedBySelf) {
        result[barIndex].endMessage = result[barIndex].startMessage;
    }

    return result;
}

// =============================================================================
// MESSAGE NUMBERING
// =============================================================================

std::vector<std::string> UltraCanvasSequenceModel::ComputeMessageNumbers(bool hierarchical) const {
    std::vector<std::string> result;
    result.reserve(messages.size());

    if (!hierarchical) {
        for (size_t i = 0; i < messages.size(); ++i) {
            result.push_back(std::to_string(i + 1));
        }
        return result;
    }

    std::vector<int> counters{0};
    auto joined = [&]() {
        std::string text;
        for (size_t i = 0; i < counters.size(); ++i) {
            if (i > 0) text += '.';
            text += std::to_string(counters[i]);
        }
        return text;
    };

    for (const auto& message : messages) {
        ++counters.back();
        result.push_back(joined());

        if (message.kind == SequenceMessageKind::Return) {
            // The reply is numbered inside the activation it ends, then the
            // numbering returns to the caller's level.
            if (counters.size() > 1) counters.pop_back();
        } else if (message.kind == SequenceMessageKind::Synchronous &&
                   !message.IsSelfMessage()) {
            counters.push_back(0);
        }
    }

    return result;
}

// =============================================================================
// VALIDATION
// =============================================================================

std::vector<SequenceDiagnostic> UltraCanvasSequenceModel::Validate() const {
    std::vector<SequenceDiagnostic> result;
    auto report = [&](SequenceDiagnosticSeverity severity, const std::string& text,
                      const std::string& subjectId = std::string(), int messageIndex = -1) {
        SequenceDiagnostic diagnostic;
        diagnostic.severity = severity;
        diagnostic.message = text;
        diagnostic.subjectId = subjectId;
        diagnostic.messageIndex = messageIndex;
        result.push_back(std::move(diagnostic));
    };

    // Duplicate lifeline names break name-based lookup.
    std::map<std::string, int> nameCounts;
    for (const auto& lifeline : lifelines) {
        if (++nameCounts[lifeline.name] == 2) {
            report(SequenceDiagnosticSeverity::Warning,
                   "duplicate lifeline name '" + lifeline.name + "'", lifeline.id);
        }
    }

    auto exists = [&](const std::string& id) {
        for (const auto& lifeline : lifelines) {
            if (lifeline.id == id) return true;
        }
        return false;
    };

    // Endpoints, create-before-use, use-after-destroy, unmatched returns.
    std::map<std::string, int> createdAt;    // lifelineId -> message index
    std::map<std::string, int> destroyedAt;
    std::map<std::string, int> openCount;

    for (size_t i = 0; i < messages.size(); ++i) {
        const SequenceMessage& message = messages[i];
        int index = static_cast<int>(i);

        bool needsFrom = message.kind != SequenceMessageKind::Found;
        bool needsTo = message.kind != SequenceMessageKind::Lost;
        if (needsFrom && !exists(message.fromId)) {
            report(SequenceDiagnosticSeverity::Error,
                   "message " + std::to_string(i) + " ('" + message.label +
                       "') has an unresolved sender '" + message.fromId + "'",
                   std::string(), index);
        }
        if (needsTo && !exists(message.toId)) {
            report(SequenceDiagnosticSeverity::Error,
                   "message " + std::to_string(i) + " ('" + message.label +
                       "') has an unresolved receiver '" + message.toId + "'",
                   std::string(), index);
        }

        for (const std::string& endpoint : {message.fromId, message.toId}) {
            if (endpoint.empty() || !exists(endpoint)) continue;
            auto destroyed = destroyedAt.find(endpoint);
            if (destroyed != destroyedAt.end() && index > destroyed->second) {
                report(SequenceDiagnosticSeverity::Warning,
                       "message " + std::to_string(i) + " ('" + message.label +
                           "') uses a lifeline destroyed at message " +
                           std::to_string(destroyed->second),
                       endpoint, index);
            }
        }

        switch (message.kind) {
            case SequenceMessageKind::Create: {
                auto created = createdAt.find(message.toId);
                if (created != createdAt.end()) {
                    report(SequenceDiagnosticSeverity::Warning,
                           "lifeline created twice (messages " +
                               std::to_string(created->second) + " and " +
                               std::to_string(i) + ")",
                           message.toId, index);
                } else {
                    createdAt[message.toId] = index;
                }
                break;
            }
            case SequenceMessageKind::Destroy:
                destroyedAt[message.toId] = index;
                openCount[message.toId] = 0;
                break;
            case SequenceMessageKind::Return:
                if (openCount[message.fromId] <= 0) {
                    report(SequenceDiagnosticSeverity::Warning,
                           "return at message " + std::to_string(i) +
                               " ends no activation",
                           message.fromId, index);
                } else {
                    --openCount[message.fromId];
                }
                break;
            case SequenceMessageKind::Synchronous:
            case SequenceMessageKind::Asynchronous:
            case SequenceMessageKind::Found:
                ++openCount[message.toId];
                break;
            case SequenceMessageKind::Lost:
                break;
        }
    }

    // A created lifeline used before its creation.
    for (const auto& [lifelineId, createIndex] : createdAt) {
        for (int i = 0; i < createIndex; ++i) {
            const SequenceMessage& message = messages[static_cast<size_t>(i)];
            if (message.fromId == lifelineId || message.toId == lifelineId) {
                report(SequenceDiagnosticSeverity::Warning,
                       "message " + std::to_string(i) +
                           " uses a lifeline not created until message " +
                           std::to_string(createIndex),
                       lifelineId, i);
            }
        }
    }

    // Fragment ranges and operands.
    for (const auto& fragment : fragments) {
        if (messages.empty() || fragment.lastMessage >= messages.size()) {
            report(SequenceDiagnosticSeverity::Error,
                   "fragment '" + fragment.id + "' (" + fragment.Keyword() +
                       ") reaches past the last message",
                   fragment.id);
            continue;
        }
        size_t previous = fragment.firstMessage;
        for (size_t i = 1; i < fragment.operands.size(); ++i) {
            size_t start = fragment.operands[i].firstMessage;
            if (start <= previous || start > fragment.lastMessage) {
                report(SequenceDiagnosticSeverity::Error,
                       "fragment '" + fragment.id + "' has a misplaced operand",
                       fragment.id);
                break;
            }
            previous = start;
        }
        if (fragment.operands.size() > 1 &&
            fragment.kind != SequenceFragmentKind::Alt &&
            fragment.kind != SequenceFragmentKind::Par) {
            report(SequenceDiagnosticSeverity::Warning,
                   std::string("fragment '") + fragment.id + "' (" + fragment.Keyword() +
                       ") carries multiple operands; only alt and par do in UML",
                   fragment.id);
        }
    }

    // Fragments must be disjoint or properly nested.
    for (size_t a = 0; a < fragments.size(); ++a) {
        for (size_t b = a + 1; b < fragments.size(); ++b) {
            const SequenceFragment& first = fragments[a];
            const SequenceFragment& second = fragments[b];
            bool disjoint = first.lastMessage < second.firstMessage ||
                            second.lastMessage < first.firstMessage;
            bool nested = (first.firstMessage <= second.firstMessage &&
                           second.lastMessage <= first.lastMessage) ||
                          (second.firstMessage <= first.firstMessage &&
                           first.lastMessage <= second.lastMessage);
            if (!disjoint && !nested) {
                report(SequenceDiagnosticSeverity::Error,
                       "fragments '" + first.id + "' and '" + second.id +
                           "' overlap without nesting",
                       second.id);
            }
        }
    }

    // Note anchors.
    for (const auto& note : notes) {
        if (!exists(note.lifelineId)) {
            report(SequenceDiagnosticSeverity::Error,
                   "note '" + note.id + "' anchors to unknown lifeline '" +
                       note.lifelineId + "'",
                   note.id);
        }
        if (!note.secondLifelineId.empty() && !exists(note.secondLifelineId)) {
            report(SequenceDiagnosticSeverity::Error,
                   "note '" + note.id + "' spans to unknown lifeline '" +
                       note.secondLifelineId + "'",
                   note.id);
        }
        if (note.afterMessage >= static_cast<int>(messages.size())) {
            report(SequenceDiagnosticSeverity::Error,
                   "note '" + note.id + "' anchors past the last message", note.id);
        }
    }

    return result;
}

void UltraCanvasSequenceModel::Clear() {
    title.clear();
    lifelines.clear();
    messages.clear();
    fragments.clear();
    notes.clear();
    nextLifelineId = 1;
    nextFragmentId = 1;
    nextNoteId = 1;
}

} // namespace UltraCanvas
