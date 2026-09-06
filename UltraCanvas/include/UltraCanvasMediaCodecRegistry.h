// include/UltraCanvasMediaCodecRegistry.h
// Registry of the audio and video codecs a build can actually use.
//
// Two questions get confused constantly, and this registry keeps them apart:
//
//   "Is this file audio/video?"  -> IsMediaFileOfKind. True even when nothing
//                                   can decode it, so a viewer shows a player
//                                   and a reason instead of a broken image.
//   "Can we decode it?"          -> CanDecodeMediaFile. Drives the format
//                                   inventory, file-dialog filters and any
//                                   decision to attempt a load.
//
// Before this existed, both answers came from lists hardcoded in the media
// viewer that had to be kept in step by hand with the codecs the build linked
// — and were not, which is how an .m4a came to be classified as audio by a
// build with no AAC decoder in it.
//
// Registration is the extension point. The framework registers its built-ins
// on first use (RegisterBuiltinMediaCodecs, compile-gated on what CMake
// found); an application or plugin that brings its own codec calls
// RegisterMediaCodec and the viewer, the Filer's categories, the format
// inventory and the open dialogs all follow it with no framework change. An
// audio registration may carry decode/encode callbacks, and UCAudio uses them
// once the built-in chain has declined a file.
//
// Version: 0.1.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASMEDIACODECREGISTRY_H
#define ULTRACANVASMEDIACODECREGISTRY_H

#include "UltraCanvasAudio.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

enum class MediaCodecKind {
    Audio,
    Video
};

// Decode one file into a PCM buffer. Return null when the file is not this
// codec's after all (a wrong extension, a container holding something else) —
// the caller then keeps looking.
using MediaAudioDecodeFn = std::function<std::shared_ptr<UCAudio>(const std::string& filePath)>;

// Encode `audio` to `filePath` in this codec's format. Return false on failure.
using MediaAudioEncodeFn = std::function<bool(const std::string& filePath, const UCAudio& audio)>;

// Decide whether a file whose extension matched really is this kind. Used only
// where an extension is shared with something else: ".ts" is TypeScript source
// far more often than an MPEG transport stream, and no extension table can
// tell those apart. Absent means the extension alone settles it.
using MediaCodecFileProbe = std::function<bool(const std::string& filePath)>;

struct MediaCodecRegistration {
    // ---- identity ----
    std::string extension;                  // canonical, lowercase, no dot
    std::vector<std::string> aliases;       // other extensions of the same format
    std::string description;                // human-readable format name
    MediaCodecKind kind = MediaCodecKind::Audio;

    // ---- capability ----
    // A format registered with neither is "recognised but not supported":
    // the viewer still classifies the file, and says why it cannot play it.
    bool canDecode = false;
    bool canEncode = false;

    std::string provider;                   // "FAAD2", "GStreamer", plugin name
    std::string notes;                      // caveats shown in the inventory

    // ---- behaviour (optional) ----
    // Set on an audio registration to make it a working codec rather than a
    // declaration. UCAudio::LoadFromFile / SaveToFile call these after the
    // built-in backend and codec libraries have declined the file, so a plugin
    // never has to displace anything to be reachable.
    MediaAudioDecodeFn decodeAudio;
    MediaAudioEncodeFn encodeAudio;

    // Set when this extension is shared with another kind of file. An entry
    // carrying a probe is deliberately left out of the extension-keyed format
    // inventory, because a name-only lookup cannot honour it.
    MediaCodecFileProbe probeFile;

    // True when ext (with or without a leading dot, any case) is the canonical
    // extension or one of the aliases.
    bool MatchesExtension(const std::string& ext) const;
};

// Add a codec, or upgrade the entry already registered for its extension. A
// second registration of the same extension and kind wins where it offers more
// — a decoder for a format that was only recognised, an encoder added to a
// decoder — and never duplicates the entry. Registering is thread-safe and may
// happen at any point; queries see the state at the time they are made.
void RegisterMediaCodec(const MediaCodecRegistration& codec);

// Drop every registration for an extension and kind. Mainly for tests and for
// an application swapping its own codec in over a built-in.
void UnregisterMediaCodec(MediaCodecKind kind, const std::string& extension);

// Register the codecs this build was compiled with. Idempotent, and called
// automatically by the first query — an application only needs it directly to
// force the built-ins in before replacing one of them.
void RegisterBuiltinMediaCodecs();

// Everything registered for a kind, in registration order.
std::vector<MediaCodecRegistration> GetRegisteredMediaCodecs(MediaCodecKind kind);

// The registration claiming this file, honouring probeFile, or null. The
// returned copy is a snapshot: the registry may change under a later call.
std::shared_ptr<const MediaCodecRegistration> FindMediaCodecForFile(
        MediaCodecKind kind, const std::string& filePath);

// The registration for an extension, ignoring any content probe. Use this when
// there is no file to probe — picking an encoder for a path about to be
// written — and FindMediaCodecForFile whenever the file already exists.
std::shared_ptr<const MediaCodecRegistration> FindMediaCodecByExtension(
        MediaCodecKind kind, const std::string& extension);

// Is this file this kind of media at all? True for a recognised format with no
// decoder, which is the point: the caller shows the right UI and the right
// error rather than mistaking the file for something else.
bool IsMediaFileOfKind(MediaCodecKind kind, const std::string& filePath);

// Can this build decode it? False for a recognised-but-unsupported format.
bool CanDecodeMediaFile(MediaCodecKind kind, const std::string& filePath);

// Can this build encode this extension? Extension-keyed: encoding picks a
// format, it does not inspect an existing file.
bool CanEncodeMediaExtension(MediaCodecKind kind, const std::string& extension);

} // namespace UltraCanvas

#endif // ULTRACANVASMEDIACODECREGISTRY_H
