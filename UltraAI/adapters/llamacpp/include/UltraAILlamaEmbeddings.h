// UltraAI/adapters/llamacpp/include/UltraAILlamaEmbeddings.h
// Local llama.cpp IEmbeddings adapter: in-process GGUF embedding models
// (BERT-family or decoder LLMs), no network, no credentials. Registered as
// provider "llama-cpp".
// Version: 0.1.0
// Last Modified: 2026-08-21
// Author: UltraAI Module
#pragma once

#include "UltraAIEmbeddings.h"

#include <memory>

namespace UltraAI {

// Create a llama.cpp-backed IEmbeddings. The GGUF model file is loaded
// eagerly so configuration errors surface here, not on the first Embed.
//
// config fields used (same convention as the text adapter):
//   defaultModel                     — path to the .gguf model file, or
//   providerOptions["model_path"]    — same, takes precedence.
//   providerOptions["n_ctx"]         — context size (int, 0 = model default)
//   providerOptions["n_threads"]     — decode threads (int, 0 = auto)
//   providerOptions["n_gpu_layers"]  — layers to offload (int, default 0)
//
// Behavior:
//  - Mean pooling over the sequence, L2-normalized output (cosine-ready,
//    matching what hosted embedding APIs return).
//  - EmbeddingRequest::dimensions truncates the native vector and
//    re-normalizes (the text-embedding-3 convention); asking for more
//    dimensions than the model has fails with InvalidRequest.
//  - Inputs are processed sequentially; calls are serialized on an
//    internal mutex (one llama_context per adapter instance).
std::unique_ptr<IEmbeddings> CreateLlamaEmbeddings(
    const EmbeddingsConfig& config, Error* outError = nullptr);

} // namespace UltraAI
