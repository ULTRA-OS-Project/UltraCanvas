// core/UltraCanvasVideo.cpp
// UCVideoFrame resource implementation
// Version: 0.1.0
// Last Modified: 2026-06-15
// Author: UltraCanvas Framework

#include "UltraCanvasVideo.h"
#include <cstring>

namespace UltraCanvas {

std::shared_ptr<UCVideoFrame> UCVideoFrame::FromRGBA(const uint8_t* pixels,
                                                     int width, int height,
                                                     int stride, double pts) {
    if (!pixels || width <= 0 || height <= 0) return nullptr;

    auto frame = std::make_shared<UCVideoFrame>();
    VideoFrameInfo info;
    info.width  = width;
    info.height = height;
    info.pixelFormat = VideoPixelFormat::RGBA32;
    info.stride = width * 4;          // we always store tightly packed
    info.pts = pts;

    const int srcStride = (stride > 0) ? stride : width * 4;
    auto& out = frame->MutableData();
    out.resize(static_cast<size_t>(info.stride) * height);
    for (int y = 0; y < height; ++y) {
        std::memcpy(out.data() + static_cast<size_t>(y) * info.stride,
                    pixels + static_cast<size_t>(y) * srcStride,
                    static_cast<size_t>(width) * 4);
    }
    frame->SetInfo(info);
    return frame;
}

} // namespace UltraCanvas
