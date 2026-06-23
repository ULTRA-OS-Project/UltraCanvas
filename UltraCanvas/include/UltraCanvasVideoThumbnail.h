// include/UltraCanvasVideoThumbnail.h
// High-level, cross-platform video thumbnail (poster frame) extraction.
// Version: 0.1.0
// Last Modified: 2026-06-21
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASVIDEOTHUMBNAIL_H
#define ULTRACANVASVIDEOTHUMBNAIL_H

#include "UltraCanvasVideo.h"
#include "UltraCanvasImage.h"
#include <memory>
#include <string>

namespace UltraCanvas {

// ===== VIDEO THUMBNAILS =====
//
// One-call extraction of a single representative frame from a video file or
// URL, for previews, gallery covers (see UltraCanvasAlbum) or "poster" frames.
//
// All three functions are SYNCHRONOUS and may block for up to a few seconds
// while the source is opened and the target frame is decoded, so prefer calling
// them off the UI thread (e.g. a worker thread or background task). They are
// safe no-ops returning null/false when no video backend is available (the null
// backend, i.e. ULTRACANVAS_ENABLE_VIDEO=OFF) or the source can't be decoded.
//
// Where the platform backend implements a fast single-frame grab (GStreamer on
// Linux) it is used directly; otherwise a generic decode-session fallback that
// works with any backend capable of seeking + decoding is used instead.

// Decode the frame as a raw RGBA/BGRA UCVideoFrame. The frame is downscaled to
// fit within req.maxWidth/maxHeight (preserving aspect ratio) when those are
// set. Returns null on failure.
UCVideoFramePtr CaptureVideoThumbnail(const std::string& source,
                                      const VideoThumbnailRequest& req = {});

// Convenience: capture and upload into a ready-to-draw UCPixmap (e.g. for an
// ImageElement or custom Render()). Returns null on failure.
std::shared_ptr<UCPixmap> CaptureVideoThumbnailPixmap(const std::string& source,
                                                      const VideoThumbnailRequest& req = {});

// Convenience: capture and write the frame to an image file on disk (suitable
// for UltraCanvasAlbum::AddItem thumbnailPath). The encoder is chosen from the
// output path's extension: ".qoi" writes QOI, anything else (or no extension)
// writes PNG. Both are always available (PNG via Cairo, QOI via the bundled
// encoder) and need no libvips. Returns true on success.
bool SaveVideoThumbnail(const std::string& source,
                        const std::string& outputImagePath,
                        const VideoThumbnailRequest& req = {});

} // namespace UltraCanvas

#endif // ULTRACANVASVIDEOTHUMBNAIL_H
