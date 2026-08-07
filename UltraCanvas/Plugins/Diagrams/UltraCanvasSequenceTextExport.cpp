// Plugins/Diagrams/UltraCanvasSequenceTextExport.cpp
// PlantUML and Mermaid export for the sequence diagram model
// Version: 1.0.0
// Last Modified: 2026-08-07
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasSequenceTextExport.h"

#include <algorithm>
#include <map>
#include <set>
#include <vector>

namespace UltraCanvas {

namespace {

std::string ReplaceAll(std::string text, const std::string& from, const std::string& to) {
    size_t position = 0;
    while ((position = text.find(from, position)) != std::string::npos) {
        text.replace(position, from.size(), to);
        position += to.size();
    }
    return text;
}

std::string PlantLabel(const std::string& text) { return ReplaceAll(text, "\n", "\\n"); }
std::string MermaidLabel(const std::string& text) { return ReplaceAll(text, "\n", "<br/>"); }

const char* PlantParticipantKeyword(SequenceLifelineKind kind) {
    switch (kind) {
        case SequenceLifelineKind::Actor:    return "actor";
        case SequenceLifelineKind::Boundary: return "boundary";
        case SequenceLifelineKind::Control:  return "control";
        case SequenceLifelineKind::Entity:   return "entity";
        case SequenceLifelineKind::Database: return "database";
        case SequenceLifelineKind::Object:   break;
    }
    return "participant";
}

// Everything the per-message emission loop needs, precomputed once. Fragment
// events at one index are ordered outermost-first for opens and separators
// (larger range = outer) and innermost-first for closes.
struct EmissionPlan {
    struct FragmentEvent {
        const SequenceFragment* fragment;
        const SequenceFragmentOperand* operand;   // separators only
    };
    std::map<size_t, std::vector<FragmentEvent>> opensAt;        // before message
    std::map<size_t, std::vector<FragmentEvent>> separatorsAt;   // before message
    std::map<size_t, std::vector<const SequenceFragment*>> closesAt;  // after message
    std::map<int, std::vector<std::string>> activateAt;          // after message
    std::map<int, std::vector<std::string>> deactivateAt;        // after message
    std::vector<std::string> activateOnTop;                      // startMessage == -1
    std::map<int, std::vector<const SequenceNote*>> notesAt;     // -1 = before all
};

EmissionPlan BuildPlan(const UltraCanvasSequenceModel& model, bool includeActivations,
                       bool includeNotes) {
    EmissionPlan plan;

    auto span = [](const SequenceFragment* fragment) {
        return fragment->lastMessage - fragment->firstMessage;
    };

    for (const auto& fragment : model.Fragments()) {
        if (model.MessageCount() == 0 || fragment.lastMessage >= model.MessageCount()) {
            continue;   // Validate() reports these; export skips them.
        }
        plan.opensAt[fragment.firstMessage].push_back({&fragment, nullptr});
        plan.closesAt[fragment.lastMessage].push_back(&fragment);
        for (size_t i = 1; i < fragment.operands.size(); ++i) {
            plan.separatorsAt[fragment.operands[i].firstMessage].push_back(
                {&fragment, &fragment.operands[i]});
        }
    }
    for (auto& [index, events] : plan.opensAt) {
        std::stable_sort(events.begin(), events.end(),
                         [&](const auto& a, const auto& b) {
                             return span(a.fragment) > span(b.fragment);
                         });
    }
    for (auto& [index, events] : plan.separatorsAt) {
        std::stable_sort(events.begin(), events.end(),
                         [&](const auto& a, const auto& b) {
                             return span(a.fragment) > span(b.fragment);
                         });
    }
    for (auto& [index, fragmentsHere] : plan.closesAt) {
        std::stable_sort(fragmentsHere.begin(), fragmentsHere.end(),
                         [&](const SequenceFragment* a, const SequenceFragment* b) {
                             return span(a) < span(b);
                         });
    }

    if (includeActivations) {
        for (const auto& activation : model.ComputeActivations()) {
            if (activation.startMessage < 0) {
                plan.activateOnTop.push_back(activation.lifelineId);
            } else {
                plan.activateAt[activation.startMessage].push_back(activation.lifelineId);
            }
            if (activation.endMessage >= 0) {
                plan.deactivateAt[activation.endMessage].push_back(activation.lifelineId);
            }
        }
        // Inner bars end before outer ones.
        for (auto& [index, ids] : plan.deactivateAt) {
            std::reverse(ids.begin(), ids.end());
        }
    }

    if (includeNotes) {
        for (const auto& note : model.Notes()) {
            int anchor = note.afterMessage;
            if (anchor >= static_cast<int>(model.MessageCount())) continue;
            plan.notesAt[anchor].push_back(&note);
        }
    }

    return plan;
}

// Lifelines that enter the diagram through a Create message must not be
// declared up front — both dialects then draw them at the top anyway.
std::set<std::string> CreatedLifelines(const UltraCanvasSequenceModel& model) {
    std::set<std::string> result;
    for (const auto& message : model.Messages()) {
        if (message.kind == SequenceMessageKind::Create) result.insert(message.toId);
    }
    return result;
}

} // namespace

// =============================================================================
// PLANTUML
// =============================================================================

std::string SequenceTextExport::ToPlantUML(const UltraCanvasSequenceModel& model,
                                           const SequenceTextExportOptions& options) {
    std::string out = "@startuml\n";
    if (options.includeTitle && !model.Title().empty()) {
        out += "title " + PlantLabel(model.Title()) + "\n";
    }
    if (options.autonumber) out += "autonumber\n";

    std::set<std::string> created = CreatedLifelines(model);
    for (const auto& lifeline : model.Lifelines()) {
        if (created.count(lifeline.id)) continue;
        out += std::string(PlantParticipantKeyword(lifeline.kind)) + " \"" +
               PlantLabel(lifeline.name) + "\" as " + lifeline.id + "\n";
    }

    EmissionPlan plan = BuildPlan(model, options.includeActivations, options.includeNotes);
    for (const std::string& id : plan.activateOnTop) {
        out += "activate " + id + "\n";
    }

    auto emitNote = [&](const SequenceNote& note) {
        std::string text = PlantLabel(note.text);
        switch (note.placement) {
            case SequenceNotePlacement::LeftOf:
                out += "note left of " + note.lifelineId + " : " + text + "\n";
                break;
            case SequenceNotePlacement::RightOf:
                out += "note right of " + note.lifelineId + " : " + text + "\n";
                break;
            case SequenceNotePlacement::Over:
                out += "note over " + note.lifelineId;
                if (!note.secondLifelineId.empty()) out += ", " + note.secondLifelineId;
                out += " : " + text + "\n";
                break;
        }
    };

    for (const SequenceNote* note : plan.notesAt[-1]) emitNote(*note);

    const auto& messages = model.Messages();
    for (size_t i = 0; i < messages.size(); ++i) {
        if (options.includeFragments) {
            auto opens = plan.opensAt.find(i);
            if (opens != plan.opensAt.end()) {
                for (const auto& event : opens->second) {
                    out += event.fragment->Keyword();
                    std::string header = event.fragment->label;
                    const std::string& guard = event.fragment->operands.front().guard;
                    if (!guard.empty()) {
                        header += (header.empty() ? "" : " ");
                        header += "[" + guard + "]";
                    }
                    if (!header.empty()) out += " " + PlantLabel(header);
                    out += "\n";
                }
            }
            auto separators = plan.separatorsAt.find(i);
            if (separators != plan.separatorsAt.end()) {
                for (const auto& event : separators->second) {
                    out += "else";
                    if (!event.operand->guard.empty()) {
                        out += " [" + PlantLabel(event.operand->guard) + "]";
                    }
                    out += "\n";
                }
            }
        }

        const SequenceMessage& message = messages[i];
        std::string label = PlantLabel(message.label);
        switch (message.kind) {
            case SequenceMessageKind::Synchronous:
                out += message.fromId + " -> " + message.toId;
                break;
            case SequenceMessageKind::Asynchronous:
                out += message.fromId + " ->> " + message.toId;
                break;
            case SequenceMessageKind::Return:
                out += message.fromId + " --> " + message.toId;
                break;
            case SequenceMessageKind::Create: {
                const SequenceLifeline* target = model.GetLifeline(message.toId);
                out += std::string("create ") +
                       PlantParticipantKeyword(target ? target->kind
                                                     : SequenceLifelineKind::Object) +
                       " \"" + PlantLabel(target ? target->name : message.toId) +
                       "\" as " + message.toId + "\n";
                out += message.fromId + " --> " + message.toId;
                break;
            }
            case SequenceMessageKind::Destroy:
                out += message.fromId + " -> " + message.toId;
                break;
            case SequenceMessageKind::Found:
                out += "[-> " + message.toId;
                break;
            case SequenceMessageKind::Lost:
                out += message.fromId + " ->]";
                break;
        }
        if (!label.empty()) out += " : " + label;
        out += "\n";
        if (message.kind == SequenceMessageKind::Destroy) {
            out += "destroy " + message.toId + "\n";
        }

        int index = static_cast<int>(i);
        for (const std::string& id : plan.activateAt[index]) out += "activate " + id + "\n";
        for (const std::string& id : plan.deactivateAt[index]) out += "deactivate " + id + "\n";
        for (const SequenceNote* note : plan.notesAt[index]) emitNote(*note);

        if (options.includeFragments) {
            auto closes = plan.closesAt.find(i);
            if (closes != plan.closesAt.end()) {
                for (size_t c = 0; c < closes->second.size(); ++c) out += "end\n";
            }
        }
    }

    out += "@enduml\n";
    return out;
}

// =============================================================================
// MERMAID
// =============================================================================

std::string SequenceTextExport::ToMermaid(const UltraCanvasSequenceModel& model,
                                          const SequenceTextExportOptions& options) {
    const std::string& indent = options.indent;
    std::string out;
    if (options.includeTitle && !model.Title().empty()) {
        out += "---\ntitle: " + model.Title() + "\n---\n";
    }
    out += "sequenceDiagram\n";
    if (options.autonumber) out += indent + "autonumber\n";

    std::set<std::string> created = CreatedLifelines(model);
    for (const auto& lifeline : model.Lifelines()) {
        if (created.count(lifeline.id)) continue;
        const char* keyword =
            lifeline.kind == SequenceLifelineKind::Actor ? "actor" : "participant";
        out += indent + keyword + " " + lifeline.id + " as " +
               MermaidLabel(lifeline.name) + "\n";
    }

    // Mermaid cannot activate a lifeline no message has reached, so
    // activateOnTop bars are not exported; every emitted activate is closed.
    EmissionPlan plan = BuildPlan(model, options.includeActivations, options.includeNotes);

    auto emitNote = [&](const SequenceNote& note) {
        std::string text = MermaidLabel(note.text);
        switch (note.placement) {
            case SequenceNotePlacement::LeftOf:
                out += indent + "Note left of " + note.lifelineId + ": " + text + "\n";
                break;
            case SequenceNotePlacement::RightOf:
                out += indent + "Note right of " + note.lifelineId + ": " + text + "\n";
                break;
            case SequenceNotePlacement::Over:
                out += indent + "Note over " + note.lifelineId;
                if (!note.secondLifelineId.empty()) out += "," + note.secondLifelineId;
                out += ": " + text + "\n";
                break;
        }
    };

    for (const SequenceNote* note : plan.notesAt[-1]) emitNote(*note);

    std::vector<std::string> openActivations;
    const auto& messages = model.Messages();
    for (size_t i = 0; i < messages.size(); ++i) {
        if (options.includeFragments) {
            auto opens = plan.opensAt.find(i);
            if (opens != plan.opensAt.end()) {
                for (const auto& event : opens->second) {
                    const SequenceFragment& fragment = *event.fragment;
                    out += indent + fragment.Keyword();
                    std::string header = fragment.label;
                    const std::string& guard = fragment.operands.front().guard;
                    if (!guard.empty()) {
                        header += (header.empty() ? "" : " ");
                        header += "[" + guard + "]";
                    }
                    if (!header.empty()) out += " " + MermaidLabel(header);
                    out += "\n";
                }
            }
            auto separators = plan.separatorsAt.find(i);
            if (separators != plan.separatorsAt.end()) {
                for (const auto& event : separators->second) {
                    const char* keyword = "else";
                    if (event.fragment->kind == SequenceFragmentKind::Par) keyword = "and";
                    if (event.fragment->kind == SequenceFragmentKind::Critical) {
                        keyword = "option";
                    }
                    out += indent + keyword;
                    if (!event.operand->guard.empty()) {
                        out += " [" + MermaidLabel(event.operand->guard) + "]";
                    }
                    out += "\n";
                }
            }
        }

        const SequenceMessage& message = messages[i];
        std::string label = MermaidLabel(message.label);
        bool emitted = true;
        switch (message.kind) {
            case SequenceMessageKind::Synchronous:
                out += indent + message.fromId + "->>" + message.toId;
                break;
            case SequenceMessageKind::Asynchronous:
                out += indent + message.fromId + "-)" + message.toId;
                break;
            case SequenceMessageKind::Return:
                out += indent + message.fromId + "-->>" + message.toId;
                break;
            case SequenceMessageKind::Create: {
                const SequenceLifeline* target = model.GetLifeline(message.toId);
                const char* keyword =
                    target && target->kind == SequenceLifelineKind::Actor ? "actor"
                                                                          : "participant";
                out += indent + "create " + keyword + " " + message.toId + " as " +
                       MermaidLabel(target ? target->name : message.toId) + "\n";
                out += indent + message.fromId + "-->>" + message.toId;
                break;
            }
            case SequenceMessageKind::Destroy:
                out += indent + "destroy " + message.toId + "\n";
                out += indent + message.fromId + "-x" + message.toId;
                break;
            case SequenceMessageKind::Found:
            case SequenceMessageKind::Lost:
                // Mermaid has no gate syntax; keep the information as a comment.
                out += indent + "%% " +
                       std::string(message.kind == SequenceMessageKind::Found ? "found: "
                                                                              : "lost: ") +
                       label + "\n";
                emitted = false;
                break;
        }
        if (emitted) {
            out += ": " + (label.empty() ? std::string(" ") : label) + "\n";
        }

        int index = static_cast<int>(i);
        for (const std::string& id : plan.activateAt[index]) {
            out += indent + "activate " + id + "\n";
            openActivations.push_back(id);
        }
        for (const std::string& id : plan.deactivateAt[index]) {
            auto open = std::find(openActivations.rbegin(), openActivations.rend(), id);
            if (open == openActivations.rend()) continue;   // top-of-diagram bar, skipped
            openActivations.erase(std::next(open).base());
            out += indent + "deactivate " + id + "\n";
        }
        for (const SequenceNote* note : plan.notesAt[index]) emitNote(*note);

        if (options.includeFragments) {
            auto closes = plan.closesAt.find(i);
            if (closes != plan.closesAt.end()) {
                for (size_t c = 0; c < closes->second.size(); ++c) out += indent + "end\n";
            }
        }
    }

    // Mermaid rejects an activate without its deactivate; close stragglers.
    for (size_t i = openActivations.size(); i-- > 0;) {
        out += indent + "deactivate " + openActivations[i] + "\n";
    }

    return out;
}

} // namespace UltraCanvas
