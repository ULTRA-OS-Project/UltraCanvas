// VideoFX/core/VideoFXText.cpp
// Text overlays drawn by the host's text renderer (VideoFX_SetTextRenderer):
// turned into picture overlays on the calling thread, before an export or a
// preview plans anything. No backend dependency: the stub has it too.
// Version: 0.18.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework

#include "VideoFX/VideoFX.h"

#include <mutex>

namespace VideoFX {

namespace {
    std::mutex rendererMutex;
    VideoFXTextRenderer textRenderer;

    VideoFXTextRenderer CurrentRenderer() {
        std::lock_guard<std::mutex> lock(rendererMutex);
        return textRenderer;
    }
}

void VideoFX_SetTextRenderer(VideoFXTextRenderer renderer) {
    std::lock_guard<std::mutex> lock(rendererMutex);
    textRenderer = std::move(renderer);
}

bool VideoFX_HasTextRenderer() {
    std::lock_guard<std::mutex> lock(rendererMutex);
    return static_cast<bool>(textRenderer);
}

void VideoFX_DrawTextOverlays(std::vector<VideoFXSegment>& segments, int frameHeight) {
    const VideoFXTextRenderer renderer = CurrentRenderer();
    if (!renderer) return;
    const int height = frameHeight > 0 ? frameHeight : 1080;
    for (VideoFXSegment& segment : segments) {
        for (VideoFXOverlay& o : segment.overlays) {
            if (o.kind != VideoFXOverlayKind::Text) continue;
            VideoFXFrame picture;
            if (!renderer(o, height, picture) || !picture.IsValid()) continue;
            // The same place, timing, fades and opacity: a picture now,
            // sized as a share of the frame's height
            o.kind = VideoFXOverlayKind::Image;
            o.image = std::move(picture);
            o.imagePath.clear();
            o.imageHeight = static_cast<double>(o.image.height) / height;
        }
    }
}

} // namespace VideoFX
