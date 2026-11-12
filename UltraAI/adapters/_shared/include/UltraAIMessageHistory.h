// UltraAI/adapters/_shared/include/UltraAIMessageHistory.h
// Conversation-history normalisation shared by the text-LLM adapters.
// Version: 0.1.0 - MergeSameRoleTurns
// Last Modified: 2026-11-12
// Author: UltraAI Module
#pragma once

#include "UltraAITextLLM.h"

#include <vector>

namespace UltraAI {

// Two user turns in a row, or two assistant turns in a row, become one
// message. OpenAI's hosted API accepts either shape, but a self-hosted
// server or an in-process engine hands the history to the model's chat
// template, and Gemma's, Llama 2's and Mistral's refuse it with
// "Conversation roles must alternate user/assistant/user/assistant" -
// LocalAI behind LiteLLM answered HTTP 500 to a history that kept an
// unanswered user turn. Texts are joined with a blank line; parts, tool
// calls and names are kept in order (a leading text becomes the first text
// part when parts are involved, so the pieces keep their order). Tool
// results are never merged: several in a row answer several tool calls,
// which is what every wire format expects. Two turns with different speaker
// names are different speakers and stay apart. System messages are left
// as they are.
std::vector<Message> MergeSameRoleTurns(const std::vector<Message>& messages);

} // namespace UltraAI
