// UltraAI/adapters/comfyui/src/ComfyUIRun.h
// Running one ComfyUI graph, independently of which capability asked for it:
// upload inputs, submit, follow the job (WebSocket, or /history polling),
// then fetch what it produced.
//
// The image and video adapters differ only in the workflow they build and
// the response type they fill, so everything between those two ends lives
// here. Progress is reported through a capability-neutral event that each
// adapter translates into its own ImageJobEvent / VideoJobEvent.
// Version: 0.1.0
// Last Modified: 2026-08-24
// Author: UltraAI Module
#pragma once

#include "ComfyUIInternal.h"

#include "UltraAICommon.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraAI {
namespace comfyui_detail {

enum class ComfyEventKind {
    Queued,
    InProgress,
    Preview
};

struct ComfyEvent {
    ComfyEventKind kind = ComfyEventKind::InProgress;
    double progress = 0.0;       // 0..1 when the server reported one
    MediaBlob preview;           // set for Preview events
};

using ComfyEmitFn      = std::function<void(const ComfyEvent&)>;
using ComfyCancelledFn = std::function<bool()>;

// Assemble the run context from a provider config and one request's
// options. `defaultUseWebSocket` is what the "use_websocket" option falls
// back to — true for the job APIs (live progress), false for the blocking
// ones, which have nowhere to put events.
RunContext MakeRunContext(const ProviderConfig& config,
                          const OptionsMap& requestOptions,
                          std::shared_ptr<ITransport> transport,
                          const std::string& baseUrl,
                          const std::string& clientId,
                          const std::string& apiKey,
                          bool defaultUseWebSocket);

// A client id for one adapter instance; ComfyUI uses it to tie a submitted
// prompt to the WebSocket that should hear about it.
std::string MakeClientId();

// ComfyUI caches results per (graph, seed): reusing a fixed seed returns the
// previous output instead of generating a new one, so an unset seed becomes
// a fresh random one rather than zero.
uint64_t RandomSeed();

// POST /upload/image. `extension` is the fallback suffix when the blob has
// no usable file name. Fills `outReference` with what LoadImage expects:
// "name", or "subfolder/name" when the server filed it away.
bool UploadImage(const RunContext& ctx, const MediaBlob& blob,
                 const std::string& extension, std::string& outReference,
                 Error* outError);

// Submit `graph` and follow it to completion. `isCancelled` and `onEvent`
// may be null. On success `outRefs` holds the files the run produced.
bool RunGraph(const RunContext& ctx, const json& graph,
              const ComfyCancelledFn& isCancelled, const ComfyEmitFn& onEvent,
              std::vector<OutputRef>& outRefs, Error* outError);

// GET /view for each produced file. With RunContext::returnAsUrl the blobs
// carry the URL and no bytes. `fallbackMime` is used when the server sends
// no content type.
bool DownloadOutputs(const RunContext& ctx, const std::vector<OutputRef>& refs,
                     const char* fallbackMime, std::vector<MediaBlob>& outBlobs,
                     Error* outError);

// GET /object_info/<nodeClass>, returning the choice list of one required
// input (the installed checkpoints, samplers, upscale models, ...). Empty
// when no server answers — capability discovery must not fail a build that
// simply has ComfyUI switched off.
std::vector<std::string> ListNodeChoices(const RunContext& ctx,
                                         const std::string& nodeClass,
                                         const std::string& inputName);

} // namespace comfyui_detail
} // namespace UltraAI
