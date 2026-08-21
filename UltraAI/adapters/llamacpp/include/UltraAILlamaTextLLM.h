// UltraAI/adapters/llamacpp/include/UltraAILlamaTextLLM.h
// Local llama.cpp ITextLLM adapter: in-process GGUF inference, no network,
// no credentials. Registered as provider "llama-cpp".
// Version: 0.1.0
// Last Modified: 2026-08-21
// Author: UltraAI Module
#pragma once

#include "UltraAITextLLM.h"

#include <memory>

namespace UltraAI {

// Create a llama.cpp-backed ITextLLM. The GGUF model file is loaded
// eagerly so configuration errors surface here, not on the first Chat.
//
// config fields used:
//   defaultModel                     — path to the .gguf model file, or
//   providerOptions["model_path"]    — same, takes precedence.
//   providerOptions["n_ctx"]         — context size (int, 0 = model default)
//   providerOptions["n_threads"]     — decode threads (int, 0 = auto)
//   providerOptions["n_gpu_layers"]  — layers to offload (int, default 0)
//
// v0.1 scope: chat + streaming + exact CountTokens + JSON-constrained
// output (ResponseFormat::JsonObject/JsonSchema enforce *valid JSON* via a
// GBNF grammar; full schema constraint compilation is a follow-up).
// Tool calling is not supported yet and fails with InvalidRequest.
std::unique_ptr<ITextLLM> CreateLlamaTextLLM(const TextLLMConfig& config,
                                             Error* outError = nullptr);

} // namespace UltraAI
