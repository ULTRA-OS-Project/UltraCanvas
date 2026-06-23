// core/UltraCanvasVideoThumbnail.cpp
// High-level cross-platform video thumbnail extraction. Uses the backend's fast
// single-frame grab when available, otherwise falls back to a generic
// decode-session approach driven through the IVideoBackend interface.
// Version: 0.1.0
// Last Modified: 2026-06-21
// Author: UltraCanvas Framework

#include "UltraCanvasVideoThumbnail.h"
#include "../libspecific/Video/IVideoBackend.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <cairo/cairo.h>

// QOI encoder. The implementation lives in libspecific/Cairo/VipsQOILoader.cpp,
// but that translation unit is only compiled when libvips is present. To keep
// QOI thumbnails available regardless of libvips, compile the (header-only)
// implementation into this TU when libvips is absent; otherwise just take the
// declarations and link against the existing qoi_write symbol (defining
// QOI_IMPLEMENTATION in both TUs would be a duplicate-symbol clash).
#ifndef HAS_LIBVIPS
#define QOI_IMPLEMENTATION
#endif
#include "../libspecific/Cairo/qoi.h"

namespace UltraCanvas {

namespace {

// Aspect-preserving downscale of a BGRA/RGBA frame to fit within (maxW, maxH).
// Only ever shrinks; returns the input unchanged when it already fits or no
// bound is requested. Bilinear sampling — fine for preview-sized thumbnails.
UCVideoFramePtr FitWithin(const UCVideoFramePtr& src, int maxW, int maxH) {
    if (!src || !src->IsValid()) return src;
    if (maxW <= 0 && maxH <= 0) return src;

    const int sw = src->GetWidth();
    const int sh = src->GetHeight();
    double scale = 1.0;
    if (maxW > 0) scale = std::min(scale, static_cast<double>(maxW) / sw);
    if (maxH > 0) scale = std::min(scale, static_cast<double>(maxH) / sh);
    if (scale >= 1.0) return src;   // already within bounds — no upscaling

    const int dw = std::max(1, static_cast<int>(sw * scale + 0.5));
    const int dh = std::max(1, static_cast<int>(sh * scale + 0.5));

    const uint8_t* sdata = src->GetData();
    const int sstride = src->GetStride();

    auto out = std::make_shared<UCVideoFrame>();
    VideoFrameInfo fi = src->GetInfo();
    fi.width = dw;
    fi.height = dh;
    fi.stride = dw * 4;
    auto& dst = out->MutableData();
    dst.resize(static_cast<size_t>(fi.stride) * dh);

    const double xRatio = static_cast<double>(sw - 1) / std::max(1, dw - 1);
    const double yRatio = static_cast<double>(sh - 1) / std::max(1, dh - 1);

    for (int y = 0; y < dh; ++y) {
        const double fy = (dh > 1) ? y * yRatio : 0.0;
        const int y0 = static_cast<int>(fy);
        const int y1 = std::min(y0 + 1, sh - 1);
        const double wy = fy - y0;
        uint8_t* drow = dst.data() + static_cast<size_t>(y) * fi.stride;
        const uint8_t* srow0 = sdata + static_cast<size_t>(y0) * sstride;
        const uint8_t* srow1 = sdata + static_cast<size_t>(y1) * sstride;
        for (int x = 0; x < dw; ++x) {
            const double fx = (dw > 1) ? x * xRatio : 0.0;
            const int x0 = static_cast<int>(fx);
            const int x1 = std::min(x0 + 1, sw - 1);
            const double wx = fx - x0;
            const uint8_t* p00 = srow0 + static_cast<size_t>(x0) * 4;
            const uint8_t* p01 = srow0 + static_cast<size_t>(x1) * 4;
            const uint8_t* p10 = srow1 + static_cast<size_t>(x0) * 4;
            const uint8_t* p11 = srow1 + static_cast<size_t>(x1) * 4;
            uint8_t* d = drow + static_cast<size_t>(x) * 4;
            for (int c = 0; c < 4; ++c) {
                const double top = p00[c] * (1.0 - wx) + p01[c] * wx;
                const double bot = p10[c] * (1.0 - wx) + p11[c] * wx;
                d[c] = static_cast<uint8_t>(top * (1.0 - wy) + bot * wy + 0.5);
            }
        }
    }

    out->SetInfo(fi);
    return out;
}

// Generic, backend-agnostic grab: open a decode session, mute it, seek to the
// requested time, briefly run it and capture the first frame that arrives. Used
// for backends that don't implement IVideoBackend::GrabThumbnail.
UCVideoFramePtr GrabViaDecodeSession(IVideoBackend* backend,
                                     const std::string& source,
                                     const VideoThumbnailRequest& req) {
    auto session = backend->OpenDecoder(source);
    if (!session) return nullptr;

    std::mutex m;
    std::condition_variable cv;
    UCVideoFramePtr captured;
    bool finished = false;

    session->onFrame = [&](UCVideoFramePtr f) {
        std::lock_guard<std::mutex> lk(m);
        if (!captured) { captured = std::move(f); finished = true; }
        cv.notify_all();
    };
    session->onError = [&](const std::string&) {
        std::lock_guard<std::mutex> lk(m);
        finished = true;
        cv.notify_all();
    };

    session->SetMute(true);
    session->SetVolume(0.0f);

    double target = req.timeSeconds;
    if (target < 0.0) {
        double dur = session->GetDuration();
        target = (dur > 1.0) ? std::min(dur * 0.1, 1.0) : 0.0;
    }
    if (target > 0.0) session->Seek(target);

    session->Play();
    {
        std::unique_lock<std::mutex> lk(m);
        cv.wait_for(lk, std::chrono::seconds(8), [&] { return finished; });
    }
    session->Stop();
    session.reset();
    return captured;
}

// Lowercased file extension including the leading dot, or "" if none.
std::string LowerExtension(const std::string& path) {
    auto slash = path.find_last_of("/\\");
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return {};
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

// Encode a frame to PNG via Cairo (always available — Cairo is a hard dep).
bool SaveFrameAsPng(const UCVideoFramePtr& frame, const std::string& path) {
    const int w = frame->GetWidth();
    const int h = frame->GetHeight();
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surf);
        return false;
    }
    uint8_t* dst = cairo_image_surface_get_data(surf);
    const int dstStride = cairo_image_surface_get_stride(surf);
    const uint8_t* src = frame->GetData();
    const int srcStride = frame->GetStride();
    const int rowBytes = w * 4;
    for (int y = 0; y < h; ++y) {
        uint8_t* drow = dst + static_cast<size_t>(y) * dstStride;
        std::memcpy(drow, src + static_cast<size_t>(y) * srcStride, rowBytes);
        for (int x = 0; x < w; ++x) drow[x * 4 + 3] = 0xFF;   // force opaque
    }
    cairo_surface_mark_dirty(surf);
    cairo_status_t st = cairo_surface_write_to_png(surf, path.c_str());
    cairo_surface_destroy(surf);
    return st == CAIRO_STATUS_SUCCESS;
}

// Encode a frame to QOI. Frames are tightly-packed BGRA; QOI wants RGB(A), so
// repack into opaque 3-channel RGB (swizzling B<->R) and hand it to qoi_write.
bool SaveFrameAsQoi(const UCVideoFramePtr& frame, const std::string& path) {
    const int w = frame->GetWidth();
    const int h = frame->GetHeight();
    std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
    const uint8_t* src = frame->GetData();
    const int srcStride = frame->GetStride();
    for (int y = 0; y < h; ++y) {
        const uint8_t* srow = src + static_cast<size_t>(y) * srcStride;
        uint8_t* drow = rgb.data() + static_cast<size_t>(y) * w * 3;
        for (int x = 0; x < w; ++x) {
            drow[x * 3 + 0] = srow[x * 4 + 2];   // R <- src R (BGRA byte 2)
            drow[x * 3 + 1] = srow[x * 4 + 1];   // G
            drow[x * 3 + 2] = srow[x * 4 + 0];   // B <- src B (BGRA byte 0)
        }
    }
    qoi_desc desc;
    desc.width = static_cast<unsigned int>(w);
    desc.height = static_cast<unsigned int>(h);
    desc.channels = 3;
    desc.colorspace = QOI_SRGB;
    return qoi_write(path.c_str(), rgb.data(), &desc) != 0;
}

} // namespace

UCVideoFramePtr CaptureVideoThumbnail(const std::string& source,
                                      const VideoThumbnailRequest& req) {
    if (source.empty()) return nullptr;
    IVideoBackend* backend = GetVideoBackend();
    if (!backend) return nullptr;

    // Prefer the backend's dedicated fast path; fall back to a generic decode.
    UCVideoFramePtr frame = backend->GrabThumbnail(source, req);
    if (!frame) frame = GrabViaDecodeSession(backend, source, req);
    if (!frame || !frame->IsValid()) return nullptr;

    return FitWithin(frame, req.maxWidth, req.maxHeight);
}

std::shared_ptr<UCPixmap> CaptureVideoThumbnailPixmap(const std::string& source,
                                                      const VideoThumbnailRequest& req) {
    UCVideoFramePtr frame = CaptureVideoThumbnail(source, req);
    if (!frame) return nullptr;

    const int w = frame->GetWidth();
    const int h = frame->GetHeight();
    auto pixmap = std::make_shared<UCPixmap>();
    if (!pixmap->Init(w, h)) return nullptr;

    cairo_surface_t* surf = pixmap->GetSurface();
    if (!surf) return nullptr;
    cairo_surface_flush(surf);
    uint8_t* dst = cairo_image_surface_get_data(surf);
    const int dstStride = cairo_image_surface_get_stride(surf);
    if (!dst) return nullptr;

    const uint8_t* src = frame->GetData();
    const int srcStride = frame->GetStride();
    const int rowBytes = w * 4;
    for (int y = 0; y < h; ++y) {
        uint8_t* drow = dst + static_cast<size_t>(y) * dstStride;
        std::memcpy(drow, src + static_cast<size_t>(y) * srcStride, rowBytes);
        // Force opaque alpha: some decoders deliver BGRx with a zero alpha byte,
        // which a premultiplied ARGB32 surface would render fully transparent.
        for (int x = 0; x < w; ++x) drow[x * 4 + 3] = 0xFF;
    }
    cairo_surface_mark_dirty(surf);
    pixmap->MarkDirty();
    return pixmap;
}

bool SaveVideoThumbnail(const std::string& source,
                        const std::string& outputImagePath,
                        const VideoThumbnailRequest& req) {
    UCVideoFramePtr frame = CaptureVideoThumbnail(source, req);
    if (!frame) return false;

    // Pick the encoder from the output extension: .qoi → QOI, anything else
    // (including no extension) → PNG.
    if (LowerExtension(outputImagePath) == ".qoi")
        return SaveFrameAsQoi(frame, outputImagePath);
    return SaveFrameAsPng(frame, outputImagePath);
}

} // namespace UltraCanvas
