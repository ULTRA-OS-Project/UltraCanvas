// UltraAI/adapters/comfyui/include/UltraAIComfyUI.h
// ComfyUI IImageGen adapter. ComfyUI is a separate, user-installed program
// (GPL-3.0); this adapter is an ordinary HTTP + WebSocket client of it and
// links none of its code.
//
// The endpoint takes a whole node graph rather than parameters, so the
// adapter carries built-in API-format workflow templates and patches the
// request into named node inputs — or uses a workflow the caller supplies.
//
//   POST /upload/image             uploads img2img / inpaint inputs
//   POST /prompt                   queues the graph, returns a prompt_id
//   GET  /ws?clientId=...          progress, previews, completion
//   GET  /history/{prompt_id}      outputs (also the no-WebSocket fallback)
//   GET  /view?filename=...        the produced image bytes
//   POST /interrupt                cancellation
//   GET  /object_info/...          checkpoint, sampler and scheduler lists
// Version: 0.1.0
// Last Modified: 2026-08-24
// Author: UltraAI Module
#pragma once

#include "UltraAIImageGen.h"
#include "UltraAITransport.h"

#include <memory>

namespace UltraAI {

// Create a ComfyUI-backed IImageGen.
//
// config fields used:
//   baseUrl       — default "http://127.0.0.1:8188". ComfyUI ships without
//       authentication, so the default is loopback on purpose; point this
//       at another host only when you have put authentication in front of
//       it, and prefer https:// when you do.
//   defaultModel  — the checkpoint file name (as ComfyUI lists it, e.g.
//       "sd_xl_base_1.0.safetensors"). Empty leaves the template's
//       checkpoint field alone, which makes ComfyUI answer with a
//       node_errors validation failure naming the installed checkpoints —
//       mapped here to ErrorCode::ModelNotFound.
//   apiKey / apiKeyVaultRef — optional. A local ComfyUI needs none; when
//       present it is sent as "Authorization: Bearer <key>" for reverse
//       proxies that require one. Canonical vault reference:
//       "ai.comfyui.api_key".
//   timeoutMs     — per-HTTP-request timeout, not the generation deadline.
//   providerOptions / ImageGenRequest::options — reserved keys below are
//       consumed by the adapter; everything else is ignored (ComfyUI takes
//       no top-level request fields beyond the graph).
//
// Reserved option keys:
//   "workflow"        (string) — an API-format workflow JSON to use instead
//                                of the built-in template for the mode.
//                                Nodes are bound by _meta.title (see
//                                "UltraAI Positive", "UltraAI Sampler", …)
//                                falling back to class_type.
//   "node_overrides"  (string) — JSON object {"<node id>": {"<input>": v}}
//                                applied after binding, for anything the
//                                interface does not model (LoRA strength,
//                                sampler names, a second pass).
//   "sampler_name"    (string) — KSampler.sampler_name. Default "euler".
//   "job_timeout_ms"  (int)    — generation deadline. Default 600000.
//   "poll_interval_ms"(int)    — /history poll delay when no WebSocket is
//                                available. Default 1000.
//   "use_websocket"   (bool)   — default true for GenerateJob (live
//                                progress and previews), false for the
//                                blocking Generate.
//
// Mode support: TextToImage, ImageToImage, Inpaint and Upscale have
// built-in templates. Outpaint, BackgroundRemoval and Variation need
// nodes a stock ComfyUI does not have, so they are rejected with
// ErrorCode::UnsupportedFormat unless a "workflow" option supplies one.
// ControlNet guidance likewise needs a workflow of its own; controlImages
// are rejected rather than silently dropped.
//
// GetCapabilities() reads GET /object_info once and reports the installed
// checkpoints as models with runsLocally = true; it returns an empty model
// list when no server answers.
//
// `transport` is the network seam. Pass nullptr to use the production
// UltraNetTransport (requires a build with ULTRAAI_USE_ULTRANET=ON;
// otherwise construction fails with ErrorCode::NetworkError).
std::unique_ptr<IImageGen> CreateComfyUIImageGen(
    const ImageGenConfig& config,
    Error* outError = nullptr,
    std::shared_ptr<ITransport> transport = nullptr);

} // namespace UltraAI
