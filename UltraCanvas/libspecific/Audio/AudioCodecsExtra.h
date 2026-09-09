// libspecific/Audio/AudioCodecsExtra.h
// Optional audio encoders/decoders beyond miniaudio's built-ins, backed by
// system codec libraries detected at configure time:
//   FLAC encode        - libFLAC            (ULTRACANVAS_HAS_LIBFLAC)
//   Ogg Vorbis enc/dec - libvorbis family   (ULTRACANVAS_HAS_VORBIS)
//   Opus encode        - libopusenc         (ULTRACANVAS_HAS_OPUSENC)
//   Opus decode        - opusfile           (ULTRACANVAS_HAS_OPUSFILE)
//   MP3 encode         - LAME               (ULTRACANVAS_HAS_LAME)
//   AAC decode         - FAAD2              (ULTRACANVAS_HAS_FAAD)
//                        fdk-aac            (ULTRACANVAS_HAS_FDKAAC)
//   Anything else      - GStreamer decodebin (ULTRACANVAS_HAS_GST_AUDIO_DECODE)
// Every entry degrades gracefully: a codec that was not compiled in simply
// reports false / null, and the supported-format inventory never lists it.
// The AAC and GStreamer routes live in AudioCodecsAAC.cpp.
// Version: 0.2.0
// Last Modified: 2026-09-06
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

// Try to decode a file miniaudio could not read (Ogg Vorbis, Opus, AAC/M4A,
// plus whatever the platform media plugins cover). Returns null when the file
// is not handled or no decoder for it is compiled in.
std::shared_ptr<UCAudio> TryDecode(const std::string& path);

// True when this build can decode AAC - through FAAD2, fdk-aac, or the
// platform media plugins.
bool CanDecodeAac();

// True when the platform media framework (GStreamer today) is available as a
// catch-all decoder for formats no in-tree codec covers.
bool HasPlatformAudioDecoder();

// Decode an AAC bitstream: a raw ADTS/ADIF .aac, or the AAC track of an
// MPEG-4 container (.m4a/.m4b/.mp4). Null when the file is neither, or when no
// AAC library was compiled in.
std::shared_ptr<UCAudio> TryDecodeAac(const std::string& path);

// Last-resort decode through the platform media framework, which covers the
// formats no in-tree codec does (ALAC, WMA, AIFF, ...). Null when no platform
// decoder is compiled in or the file is not one it can read.
std::shared_ptr<UCAudio> TryDecodePlatform(const std::string& path);

// A one-sentence, user-facing explanation of why `path` could not be decoded -
// naming the codec found inside an MPEG-4 container and the library that would
// unlock it. Empty when there is nothing specific to say, in which case the
// caller's generic "unsupported or damaged" wording is the better message.
std::string DescribeDecodeFailure(const std::string& path);

} // namespace AudioCodecs
} // namespace UltraCanvas
