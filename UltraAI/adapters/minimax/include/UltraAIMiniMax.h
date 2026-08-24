// UltraAI/adapters/minimax/include/UltraAIMiniMax.h
// MiniMax (Hailuo) adapters: IVideoGen over the asynchronous video API
// (POST {baseUrl}/v1/video_generation -> GET /v1/query/video_generation ->
// GET /v1/files/retrieve) and IImageGen over the synchronous image API
// (POST {baseUrl}/v1/image_generation).
//
// Network I/O goes through the UltraAI transport seam: production wiring
// uses UltraNetTransport, unit tests inject a ScriptedTransport. No
// WebSocket is involved — MiniMax reports job progress by polling, which
// the shared RunJobPoll loop drives.
// Version: 0.1.0
// Last Modified: 2026-08-24
// Author: UltraAI Module
#pragma once

#include "UltraAIImageGen.h"
#include "UltraAITransport.h"
#include "UltraAIVideoGen.h"

#include <memory>

namespace UltraAI {

// Create a MiniMax-backed IVideoGen.
//
// config fields used:
//   apiKey / apiKeyVaultRef — resolved lazily on the first request and sent
//       as "Authorization: Bearer <key>". MiniMax has no keyless mode, so
//       an unresolvable key surfaces ErrorCode::AuthenticationFailed.
//       Canonical vault reference: "ai.minimax.api_key".
//   baseUrl       — default "https://api.minimax.io". Mainland-China
//       accounts use "https://api.minimaxi.com"; both speak the same API.
//   defaultModel  — used when VideoGenRequest::model is empty; falls back
//       to "MiniMax-Hailuo-02".
//   timeoutMs     — per-HTTP-request timeout, not the job deadline (see
//       "job_timeout_ms" below).
//   providerOptions / VideoGenRequest::options — passed through as
//       top-level request fields, except the reserved control keys below.
//
// Reserved option keys (consumed by the adapter, never sent to MiniMax):
//   "job_timeout_ms"  (int)  — how long to poll before giving up.
//                              Default 900000 (15 min).
//   "poll_interval_ms"(int)  — delay between polls. Default 5000.
//   "return_url_only" (bool) — when true the finished video is reported as
//                              a URL in GeneratedVideo::video.url instead of
//                              being downloaded into bytes. Default false.
//                              MiniMax download URLs are short-lived, so a
//                              URL-only result must be fetched promptly.
//
// Interface fields MiniMax does not expose are ignored: negativePrompt,
// steps, guidanceScale, seed, fps, and count (one video per request).
// VideoToVideo, FrameInterpolation and Upscale modes are rejected with
// ErrorCode::UnsupportedFormat rather than silently producing something
// else.
//
// Cancelling a StreamHandle from GenerateJob stops the polling; MiniMax
// has no task-cancel endpoint, so the job continues (and bills) server-side.
//
// `transport` is the network seam. Pass nullptr to use the production
// UltraNetTransport (requires a build with ULTRAAI_USE_ULTRANET=ON;
// otherwise construction fails with ErrorCode::NetworkError).
std::unique_ptr<IVideoGen> CreateMiniMaxVideoGen(
    const VideoGenConfig& config,
    Error* outError = nullptr,
    std::shared_ptr<ITransport> transport = nullptr);

// Create a MiniMax-backed IImageGen (POST {baseUrl}/v1/image_generation).
// Same credential and baseUrl rules; defaultModel falls back to "image-01".
//
// Supported modes: TextToImage, and Variation when a source image is given
// (sent as MiniMax's subject_reference for character consistency). Every
// other ImageGenMode is rejected with ErrorCode::UnsupportedFormat.
// ImageGenRequest::returnAsUrl selects the API's response_format: false
// (the default) asks for base64 and returns decoded bytes.
// steps, guidanceScale, seed, scheduler and negativePrompt are ignored —
// the API has no equivalent.
std::unique_ptr<IImageGen> CreateMiniMaxImageGen(
    const ImageGenConfig& config,
    Error* outError = nullptr,
    std::shared_ptr<ITransport> transport = nullptr);

} // namespace UltraAI
