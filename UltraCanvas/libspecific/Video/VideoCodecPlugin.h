// libspecific/Video/VideoCodecPlugin.h
// Decoding behaviour for a video codec registered in UltraCanvasMediaCodecRegistry.
//
// The registry itself answers "is this file video, and can this build play it".
// That is all the media viewer and the format inventory need, and it is all a
// public core header should have to know about — which is why the decoding half
// lives here instead: it deals in IVideoDecodeSession, a libspecific type, and
// UltraCanvasMediaCodecRegistry.h must stay includable from anywhere, video
// backend or no video backend.
//
// An application that brings its own container or codec calls
// RegisterVideoCodecPlugin once at start-up. Classification, the Filer's
// categories, the format inventory and the file dialogs then follow the
// registration, and UltraCanvasVideoPlayer / CaptureVideoThumbnail reach the
// decoder — the same shape the audio side already has through
// MediaCodecRegistration::decodeAudio.
//
// PRECEDENCE, and why it is not the audio side's. An audio plugin runs *last*,
// after the built-in backend and codec libraries have declined a file. A video
// plugin runs *first* for the sources it claims, because the platform video
// backend does not decline anything: OpenDecoder builds a pipeline and reports
// an undecodable source asynchronously on its bus, so a fallback ordering would
// leave a registered codec unreachable. That is safe precisely because the
// lookup is keyed on extensions a plugin explicitly registered — a source no
// plugin claimed goes straight to the backend, untouched.
//
// Version: 0.1.0
// Last Modified: 2026-09-07
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASVIDEOCODECPLUGIN_H
#define ULTRACANVASVIDEOCODECPLUGIN_H

#include "IVideoBackend.h"
#include "UltraCanvasMediaCodecRegistry.h"

#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {

// Open `source` for decoding. Return null when the source is not this codec's
// after all, or cannot be opened — the caller then keeps looking. The session
// is driven exactly like a backend's: frames arrive on onFrame, off the caller's
// thread.
using VideoDecoderFactory =
    std::function<std::unique_ptr<IVideoDecodeSession>(const std::string& source,
                                                       const VideoDecodeOptions& options)>;

// Optional fast single-frame grab, mirroring IVideoBackend::GrabThumbnail. A
// plugin that does not supply one still produces thumbnails: the generic
// decode-session path drives its VideoDecoderFactory instead.
using VideoThumbnailGrabber =
    std::function<UCVideoFramePtr(const std::string& source,
                                  const VideoThumbnailRequest& request)>;

// Register a video codec together with the code that decodes it. `codec` goes
// to RegisterMediaCodec (so registration, alias and upgrade rules are the
// registry's — see UltraCanvasMediaCodecRegistry.h); supplying `openDecoder`
// implies canDecode, so a caller cannot register a decoder and then have the
// format advertised as unplayable.
//
// `grabThumbnail` is optional. Pass one only when the plugin has a cheaper path
// than opening a session and waiting for a frame.
void RegisterVideoCodecPlugin(const MediaCodecRegistration& codec,
                              VideoDecoderFactory openDecoder,
                              VideoThumbnailGrabber grabThumbnail = {});

// Remove a plugin's decoding callbacks. Does NOT unregister the codec from the
// media registry — call UnregisterMediaCodec for that — so a caller can drop a
// decoder while leaving the format recognised.
void UnregisterVideoCodecPlugin(const std::string& extension);

// The decoder registered for `source`, or an empty function. Extension-keyed
// through the media registry, so aliases and content probes are honoured.
VideoDecoderFactory FindVideoDecoderFor(const std::string& source);

// The dedicated thumbnail grabber for `source`, or an empty function. Absent
// does not mean "no thumbnail": fall back to FindVideoDecoderFor.
VideoThumbnailGrabber FindVideoThumbnailGrabberFor(const std::string& source);

} // namespace UltraCanvas

#endif // ULTRACANVASVIDEOCODECPLUGIN_H
