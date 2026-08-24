// UltraAI/adapters/comfyui/src/ComfyUIWorkflows.h
// Built-in API-format workflow templates and the binding that patches an
// ImageGenRequest into them.
//
// ComfyUI's /prompt endpoint takes a whole node graph, not parameters, so
// the adapter carries one template per ImageGenMode and writes request
// fields into named node inputs. Nodes are located by their _meta.title
// (ComfyUI persists titles), falling back to a search by class_type — so a
// workflow a user exported from the GUI works too, as long as the titles or
// the node classes match.
//
// Every template uses core nodes only: shipping templates that depend on
// custom nodes would make the adapter fail on a stock install, and custom
// nodes are GPL-3.0 derivative works this repository does not redistribute.
// Version: 0.1.0
// Last Modified: 2026-08-24
// Author: UltraAI Module
#pragma once

#include "UltraAICommon.h"
#include "UltraAIImageGen.h"

#include <nlohmann/json.hpp>

#include <string>

namespace UltraAI {
namespace comfyui_detail {

using nlohmann::json;

// Node titles the templates use, and that a user-supplied workflow can
// adopt to become bindable.
constexpr const char* kTitleSampler    = "UltraAI Sampler";
constexpr const char* kTitleCheckpoint = "UltraAI Checkpoint";
constexpr const char* kTitleLatent     = "UltraAI Latent";
constexpr const char* kTitlePositive   = "UltraAI Positive";
constexpr const char* kTitleNegative   = "UltraAI Negative";
constexpr const char* kTitleSource     = "UltraAI Source";
constexpr const char* kTitleMask       = "UltraAI Mask";
constexpr const char* kTitleUpscale    = "UltraAI Upscale";

// The API-format graph for `mode`, or an empty object when the mode has no
// built-in template.
json BuiltInWorkflow(ImageGenMode mode);

// Node id whose _meta.title is `title`; when no node carries that title,
// the first node with class `classType`. Empty when neither matches.
std::string FindNodeId(const json& graph, const std::string& title,
                       const std::string& classType);

// Set graph[nodeId].inputs[field] when the node exists. Returns false when
// the node could not be found, so callers can report a template that does
// not support what was asked of it.
bool SetNodeInput(json& graph, const std::string& nodeId,
                  const std::string& field, json value);

// Convenience: locate by title/class, then set. Missing nodes are skipped
// silently — an optional field (a negative prompt in a template with no
// negative encoder) is not an error.
void SetByTitle(json& graph, const std::string& title,
                const std::string& classType, const std::string& field,
                json value);

} // namespace comfyui_detail
} // namespace UltraAI
