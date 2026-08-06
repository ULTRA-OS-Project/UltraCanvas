// libspecific/Audio/AudioCodecsExtra.h
// Optional audio encoders/decoders beyond miniaudio's built-ins, backed by
// system codec libraries detected at configure time:
//   FLAC encode        - libFLAC            (ULTRACANVAS_HAS_LIBFLAC)
//   Ogg Vorbis enc/dec - libvorbis family   (ULTRACANVAS_HAS_VORBIS)
//   Opus encode        - libopusenc         (ULTRACANVAS_HAS_OPUSENC)
//   Opus decode        - opusfile           (ULTRACANVAS_HAS_OPUSFILE)
//   MP3 encode         - LAME               (ULTRACANVAS_HAS_LAME)
// Every entry degrades gracefully: a codec that was not compiled in simply
// reports false / null, and the supported-format inventory never lists it.
// Version: 0.1.0
// Last Modified: 2026-08-06
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasAudio.h"
#include <memory>
#include <string>

namespace UltraCanvas {
namespace AudioCodecs {

// True when an encoder for `format` was compiled in (beyond miniaudio's WAV).
bool CanEncode(AudioFormat format);

// Encode `audio` to `path` in `format`. Returns false when the format's
// encoder is not compiled in, the audio is invalid, or encoding fails.
bool Encode(const std::string& path, const UCAudio& audio, AudioFormat format);

// Try to decode a file miniaudio could not read (Ogg Vorbis / Opus).
// Returns null when the file is not handled or no decoder is compiled in.
std::shared_ptr<UCAudio> TryDecode(const std::string& path);

} // namespace AudioCodecs
} // namespace UltraCanvas
