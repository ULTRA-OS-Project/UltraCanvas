// UltraAI/adapters/_shared/src/MessageHistory.cpp
// Implementation of the shared conversation-history helpers.
// Version: 0.1.0
// Last Modified: 2026-11-12
// Author: UltraAI Module

#include "UltraAIMessageHistory.h"

namespace UltraAI {

namespace {

void AppendMergingSameRole(std::vector<Message>& out, const Message& m) {
    if (!out.empty()) {
        Message& prev = out.back();
        const bool mergeable = prev.role == m.role &&
                               (m.role == Role::User || m.role == Role::Assistant) &&
                               prev.name == m.name;
        if (mergeable) {
            if (prev.parts.empty() && m.parts.empty()) {
                if (prev.text.empty())       prev.text = m.text;
                else if (!m.text.empty())    prev.text += "\n\n" + m.text;
            } else {
                // prev.text (if any) is serialized as the first text part,
                // so the merged content keeps every piece in its order.
                if (!m.text.empty()) prev.parts.push_back(TextPart{m.text});
                prev.parts.insert(prev.parts.end(), m.parts.begin(), m.parts.end());
            }
            prev.toolCalls.insert(prev.toolCalls.end(),
                                  m.toolCalls.begin(), m.toolCalls.end());
            return;
        }
    }
    out.push_back(m);
}

} // namespace

std::vector<Message> MergeSameRoleTurns(const std::vector<Message>& messages) {
    std::vector<Message> out;
    out.reserve(messages.size());
    for (const auto& m : messages) AppendMergingSameRole(out, m);
    return out;
}

} // namespace UltraAI
