// Apps/UltraAIApp/UltraAIAgentTools.h
// The tools the dashboard's Agents dialog offers a model: safe ones that
// touch nothing on the computer (the clock, arithmetic, text statistics,
// random numbers), so the dialog shows the agent loop without giving a
// model access to files or programs. Kept apart from the dialog so they
// can be tested without a window.
// Version: 0.1.0 - first version
// Last Modified: 2026-11-09
// Author: UltraAI Module
#pragma once

#include "UltraAIAgent.h"

#include <optional>
#include <string>
#include <vector>

namespace UltraAIApp {

// One of the dialog's tools: its id doubles as the tool's name.
struct DemoTool {
    std::string id;
    std::string label;          // the checkbox text, already translated
    UltraAI::AgentTool tool;
};

// Every demo tool, in the order the dialog lists them.
std::vector<DemoTool> DemoTools();

// The arithmetic the "calculate" tool does: + - * / ^ %, parentheses,
// unary minus, decimals. nullopt (and `error`) when the expression is
// malformed or divides by zero.
std::optional<double> EvaluateArithmetic(const std::string& expression,
                                         std::string* error = nullptr);

} // namespace UltraAIApp
