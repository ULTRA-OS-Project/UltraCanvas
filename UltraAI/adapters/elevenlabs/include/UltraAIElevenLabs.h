// UltraAI/adapters/elevenlabs/include/UltraAIElevenLabs.h
// ElevenLabs adapter: ITextToSpeech over the text-to-speech API
// (POST {baseUrl}/v1/text-to-speech/{voice_id}, one-shot or streamed),
// voice listing (GET /v1/voices) and instant voice cloning
// (POST /v1/voices/add).
//
// Network I/O goes through the UltraAI transport seam: production wiring
// uses UltraNetTransport, unit tests inject a ScriptedTransport.
//
// Two deployments share this adapter:
//   * Hosted (ULTRA-operated relay) — the priority. `baseUrl` points at the
//     relay, the credential is the user's session token and
//     providerOptions["auth_scheme"] = "bearer". The relay holds the
//     ElevenLabs key, checks the user's plan and meters usage; the wire
//     format is ElevenLabs' own, so the relay forwards requests unchanged.
//   * Bring-your-own-key — `baseUrl` left empty and the user's own
//     ElevenLabs key in UltraVault under "ai.elevenlabs.api_key".
// Version: 0.1.0
// Last Modified: 2026-09-24
// Author: UltraAI Module
#pragma once

#include "UltraAITextToSpeech.h"
#include "UltraAITransport.h"

#include <memory>

namespace UltraAI {

// Create an ElevenLabs-backed ITextToSpeech.
//
// config fields used:
//   apiKey / apiKeyVaultRef — resolved lazily on the first request. There
//       is no keyless mode: an unresolvable credential surfaces
//       ErrorCode::AuthenticationFailed. Canonical vault reference:
//       "ai.elevenlabs.api_key".
//   baseUrl       — default "https://api.elevenlabs.io". Point it at a relay
//       for the hosted service (see above) or at a data-residency endpoint
//       such as "https://api.eu.residency.elevenlabs.io".
//   defaultModel  — used when SpeakRequest::model is empty; falls back to
//       "eleven_multilingual_v2". Any model id the account can use works.
//   timeoutMs     — per-HTTP-request timeout.
//   providerOptions / SpeakRequest::options — passed through as top-level
//       request-body fields (seed, previous_text, next_text,
//       apply_text_normalization, ...), except the reserved keys below.
//       Per-request values win over provider defaults.
//
// Reserved option keys (consumed by the adapter, never sent as body fields):
//   "auth_scheme"      (string, providerOptions only — it is a deployment
//                                 setting, not a per-request one) —
//                                 "xi-api-key" (default) sends the credential
//                                 as the xi-api-key header ElevenLabs
//                                 expects; "bearer" sends
//                                 "Authorization: Bearer <credential>" for a
//                                 relay that authenticates users itself.
//   "default_voice_id" (string) — used when SpeakRequest::voiceId is empty.
//   "output_format"    (string) — ElevenLabs output format verbatim (e.g.
//                                 "mp3_44100_192", "ulaw_8000"), overriding
//                                 the SpeakRequest::format mapping below.
//   "enable_logging"   (bool)   — false requests zero-retention mode
//                                 (enterprise accounts only).
//   "stability", "similarity_boost", "style" (double) and
//   "use_speaker_boost" (bool) — sent inside voice_settings.
//
// SpeakRequest mapping:
//   voiceId   — required (or "default_voice_id"); ListVoices() reports what
//               the account can use.
//   language  — sent as language_code (the primary subtag of a BCP-47 tag,
//               "de-DE" -> "de"). Only some models honour it.
//   speed     — voice_settings.speed when not 1.0 (the API accepts 0.7-1.2).
//   format    — Mp3 (44.1 kHz/128 kbps, or 22.05 kHz/32 kbps when
//               sampleRateHz is 22050) and PcmS16Le (8000, 16000, 22050,
//               24000 (default), 44100 or 48000 Hz). Other formats are
//               rejected with ErrorCode::UnsupportedFormat unless
//               "output_format" names one.
//   ssml      — rejected with ErrorCode::UnsupportedFormat; ElevenLabs takes
//               plain text (inline <break time="1s" /> tags are plain text
//               to it and pass through).
//   style, pitch, volume — ignored: the API has no equivalent (ElevenLabs'
//               own numeric "style" is the reserved option above).
//
// SpeakResponse::usage.units is the character cost ElevenLabs reports in
// its "character-cost" response header, falling back to the character
// count of the text — the figure a relay meters. durationSec is filled for
// PCM output only; compressed formats would need decoding to measure.
//
// SpeakStream() uses /stream/with-timestamps, whose body is newline-
// delimited JSON: audio arrives base64-encoded line by line, and an error
// body is a JSON "detail" object that cannot be mistaken for audio.
// Cancelling the returned handle cancels the HTTP request.
//
// ListVoices(language) filters by the voices' verified languages (primary
// subtag match). Voices that declare no language are kept, because the
// multilingual models speak any supported language with them.
//
// CloneVoice() performs instant voice cloning from inline samples
// (MediaBlob::bytes; URL-only samples are rejected rather than downloaded).
// CloneVoiceRequest::options may carry "description" (string),
// "remove_background_noise" (bool) and "labels" (JSON object as a string).
// consentText is not sent: ElevenLabs takes consent through its terms, not
// per request.
//
// `transport` is the network seam. Pass nullptr to use the production
// UltraNetTransport (requires a build with ULTRAAI_USE_ULTRANET=ON;
// otherwise construction fails with ErrorCode::NetworkError).
std::unique_ptr<ITextToSpeech> CreateElevenLabsTextToSpeech(
    const TextToSpeechConfig& config,
    Error* outError = nullptr,
    std::shared_ptr<ITransport> transport = nullptr);

} // namespace UltraAI
