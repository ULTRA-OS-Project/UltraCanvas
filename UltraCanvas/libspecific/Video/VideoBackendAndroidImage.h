// libspecific/Video/VideoBackendAndroidImage.h
// Shared by the Android video backend's playback and camera halves: turning an
// AImage (YUV_420_888, what both the decoder and the camera hand an
// AImageReader) into the packed BGRA frame every UltraCanvas video consumer
// takes, upright.
//
// Version: 1.0.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework
#pragma once

#include "IVideoBackend.h"

#include <media/NdkImage.h>

#include <algorithm>
#include <cstdint>
#include <memory>

namespace UltraCanvas {
namespace AndroidVideo {

// Which YUV -> RGB matrix and range a stream uses. The decoder reports both in
// its output format ("color-standard", "color-range"); a stream that says
// nothing follows the usual convention: BT.709 from 720 lines up, BT.601
// below, and limited (16-235) range.
struct YuvColorInfo {
    bool bt709 = true;
    bool fullRange = false;
};

// Convert one YUV_420_888 image into a BGRA32 UCVideoFrame:
//   - only the image's crop rectangle (the decoder pads to its block size),
//   - turned clockwise by `rotationDegrees` (0/90/180/270: phone recordings
//     carry their orientation as metadata, cameras as sensor orientation),
//   - mirrored horizontally after turning when `mirror` (a front camera's
//     preview, which people expect to behave like a mirror).
// Returns null for an image in any other format.
inline UCVideoFramePtr FrameFromYuvImage(AImage* image, int rotationDegrees, bool mirror,
                                         const YuvColorInfo& color, double pts) {
    if (!image) return nullptr;
    int32_t format = 0;
    if (AImage_getFormat(image, &format) != AMEDIA_OK || format != AIMAGE_FORMAT_YUV_420_888) {
        return nullptr;
    }
    int32_t imageW = 0, imageH = 0;
    AImage_getWidth(image, &imageW);
    AImage_getHeight(image, &imageH);
    AImageCropRect crop{0, 0, imageW, imageH};
    if (AImage_getCropRect(image, &crop) != AMEDIA_OK ||
        crop.right <= crop.left || crop.bottom <= crop.top) {
        crop = AImageCropRect{0, 0, imageW, imageH};
    }
    crop.left = std::clamp(crop.left, 0, imageW);
    crop.top = std::clamp(crop.top, 0, imageH);
    crop.right = std::clamp(crop.right, crop.left, imageW);
    crop.bottom = std::clamp(crop.bottom, crop.top, imageH);
    const int srcW = crop.right - crop.left;
    const int srcH = crop.bottom - crop.top;
    if (srcW <= 0 || srcH <= 0) return nullptr;

    uint8_t* planes[3] = {};
    int planeLength[3] = {};
    int32_t rowStride[3] = {}, pixelStride[3] = {};
    for (int i = 0; i < 3; ++i) {
        if (AImage_getPlaneData(image, i, &planes[i], &planeLength[i]) != AMEDIA_OK ||
            AImage_getPlaneRowStride(image, i, &rowStride[i]) != AMEDIA_OK ||
            AImage_getPlanePixelStride(image, i, &pixelStride[i]) != AMEDIA_OK ||
            !planes[i]) {
            return nullptr;
        }
    }

    const int rotation = ((rotationDegrees % 360) + 360) % 360 / 90 * 90;
    const bool swapAxes = rotation == 90 || rotation == 270;
    const int dstW = swapAxes ? srcH : srcW;
    const int dstH = swapAxes ? srcW : srcH;

    auto frame = std::make_shared<UCVideoFrame>();
    VideoFrameInfo fi;
    fi.width = dstW;
    fi.height = dstH;
    fi.stride = dstW * 4;
    fi.pixelFormat = VideoPixelFormat::BGRA32;
    fi.pts = pts;
    auto& out = frame->MutableData();
    out.resize(static_cast<size_t>(fi.stride) * static_cast<size_t>(dstH));

    // 16.16 fixed-point coefficients. Limited range scales luma by 255/219
    // and chroma by 255/224 after removing the 16 / 128 offsets.
    const double ky = color.fullRange ? 1.0 : 255.0 / 219.0;
    const double kc = color.fullRange ? 1.0 : 255.0 / 224.0;
    const double rv = (color.bt709 ? 1.5748 : 1.402) * kc;
    const double gu = (color.bt709 ? 0.1873 : 0.344136) * kc;
    const double gv = (color.bt709 ? 0.4681 : 0.714136) * kc;
    const double bu = (color.bt709 ? 1.8556 : 1.772) * kc;
    const int cY = static_cast<int>(ky * 65536.0 + 0.5);
    const int cRV = static_cast<int>(rv * 65536.0 + 0.5);
    const int cGU = static_cast<int>(gu * 65536.0 + 0.5);
    const int cGV = static_cast<int>(gv * 65536.0 + 0.5);
    const int cBU = static_cast<int>(bu * 65536.0 + 0.5);
    const int yOffset = color.fullRange ? 0 : 16;

    auto clamp8 = [](int v) -> uint8_t {
        v = (v + 32768) >> 16;   // round, so limited-range white is 255
        return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);
    };

    // Each plane's last valid byte, so a driver reporting a short final row
    // can never make the loop read past its buffer.
    const uint8_t* end[3] = { planes[0] + planeLength[0], planes[1] + planeLength[1],
                              planes[2] + planeLength[2] };

    for (int sy = 0; sy < srcH; ++sy) {
        const int imgY = crop.top + sy;
        const uint8_t* yRow = planes[0] + static_cast<size_t>(imgY) * rowStride[0];
        const uint8_t* uRow = planes[1] + static_cast<size_t>(imgY / 2) * rowStride[1];
        const uint8_t* vRow = planes[2] + static_cast<size_t>(imgY / 2) * rowStride[2];
        for (int sx = 0; sx < srcW; ++sx) {
            const int imgX = crop.left + sx;
            const uint8_t* yp = yRow + static_cast<size_t>(imgX) * pixelStride[0];
            const uint8_t* up = uRow + static_cast<size_t>(imgX / 2) * pixelStride[1];
            const uint8_t* vp = vRow + static_cast<size_t>(imgX / 2) * pixelStride[2];
            const int Y = (yp < end[0] ? *yp : 0) - yOffset;
            const int U = (up < end[1] ? *up : 128) - 128;
            const int V = (vp < end[2] ? *vp : 128) - 128;
            const int luma = Y * cY;

            int dx, dy;
            switch (rotation) {
                case 90:  dx = srcH - 1 - sy; dy = sx;            break;
                case 180: dx = srcW - 1 - sx; dy = srcH - 1 - sy; break;
                case 270: dx = sy;            dy = srcW - 1 - sx; break;
                default:  dx = sx;            dy = sy;            break;
            }
            if (mirror) dx = dstW - 1 - dx;

            uint8_t* px = out.data() + static_cast<size_t>(dy) * fi.stride + static_cast<size_t>(dx) * 4;
            px[0] = clamp8(luma + cBU * U);                  // B
            px[1] = clamp8(luma - cGU * U - cGV * V);        // G
            px[2] = clamp8(luma + cRV * V);                  // R
            px[3] = 255;
        }
    }
    frame->SetInfo(fi);
    return frame;
}

} // namespace AndroidVideo
} // namespace UltraCanvas
