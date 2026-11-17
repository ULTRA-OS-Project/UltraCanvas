// VideoFX/ultracanvas/VideoFXUltraCanvasFormats.h
// VideoFX as UltraCanvas FileLoader's video converter. The framework core
// links no video encoder, so UltraCanvasFileLoader::ConvertVideoFile writes
// only what a registered converter writes (MediaCodecRegistration::
// convertVideo). This registers VideoFX's: every video container this build's
// VideoFX writes - its muxer and its usual encoders are in the FFmpeg it is
// linked against (VideoFX_GetFileFormats, canWrite) - with a converter that
// encodes a whole file into it again (VideoFX_Transcode). From then on
// ConvertVideoFile writes those types and a video's Save as offers them
// (AddVideoSaveTypes) - the media viewer's among them.
//
// Playing is left as it was: the platform video backend (GStreamer, Media
// Foundation, AVFoundation) plays a film, and a type registered here only
// gains being written.
//
// A library of its own (VideoFXUltraCanvasFormats) beside VideoFX, which
// stays free of UltraCanvas, as VideoFXUltraCanvasText is: an application
// that does no video never loads VideoFX or FFmpeg.
// Version: 0.1.0
// Last Modified: 2026-11-15
// Author: UltraCanvas Framework
#pragma once

#include <functional>
#include <string>

namespace VideoFX {

// Registers the converter for every video container VideoFX writes here. Call
// it once at start-up (again is harmless: the first call is the one that
// asks FFmpeg which muxers and encoders it has). Returns how many types were
// registered: none from a VideoFX built without FFmpeg, and a video then has
// no Save as.
int VideoFX_RegisterWithFileLoader();

// The converter on its own: `sourcePath` encoded again into `targetPath` in
// the container its extension names, at the source's size and frame rate,
// with that container's usual codecs (VideoFX_Transcode with default
// settings). `progress` (0..1, may be empty) is called on the calling thread;
// returning false stops it, and then `error` is "Stopped". What a failed or
// stopped conversion wrote is removed. Slow: call it off the UI thread.
bool VideoFX_ConvertVideoFile(const std::string& sourcePath, const std::string& targetPath,
                              const std::function<bool(double fraction)>& progress, std::string& error);

} // namespace VideoFX
