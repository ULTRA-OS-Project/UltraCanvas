// VideoFX/core/VideoFXKenBurns.cpp
// Still images on the timeline: motion presets and a sub-pixel renderer.
//
// Every output frame is resampled straight from the (pre-shrunk) image with
// fractional coordinates, so the camera glides. FFmpeg's zoompan rounds its
// window to whole pixels each frame, which shows as the familiar Ken Burns
// jitter - this is why the frames are made here and not in a filter.
// Version: 0.3.0
// Last Modified: 2026-09-29
// Author: UltraCanvas Framework

#include "VideoFXKenBurns.h"

#include <algorithm>
#include <cmath>
#include <thread>
#include <vector>

namespace VideoFX {
namespace Internal {

namespace {
    bool InRange(double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; }
    double Lerp(double a, double b, double f) { return a + (b - a) * f; }
}

bool ValidateMotion(const VideoFXImageMotion& m, std::string& error) {
    if (m.style != VideoFXMotionStyle::Custom) return true;
    if (!InRange(m.startZoom, 1.0, 4.0) || !InRange(m.endZoom, 1.0, 4.0)) {
        error = "Image zoom must be 1..4";
        return false;
    }
    if (!InRange(m.startX, 0.0, 1.0) || !InRange(m.startY, 0.0, 1.0) ||
        !InRange(m.endX, 0.0, 1.0) || !InRange(m.endY, 0.0, 1.0)) {
        error = "Image motion centres must be 0..1";
        return false;
    }
    return true;
}

VideoFXImageMotion ResolveMotion(const VideoFXImageMotion& m, int imageW, int imageH, int outW, int outH,
                                 size_t segmentIndex) {
    if (m.style == VideoFXMotionStyle::Custom || m.style == VideoFXMotionStyle::Still) return m;
    VideoFXMotionStyle style = m.style;
    if (style == VideoFXMotionStyle::Auto) {
        // Pan along the direction the frame crops: a panorama sideways; a
        // portrait photo - or a 4:3 one - in a 16:9 frame up and down
        const double imageAspect = imageH > 0 ? static_cast<double>(imageW) / imageH : 1.0;
        const double frameAspect = outH > 0 ? static_cast<double>(outW) / outH : 1.0;
        const bool vertical = imageAspect < frameAspect / 1.1;
        static const VideoFXMotionStyle horizontalCycle[] = {
            VideoFXMotionStyle::ZoomIn, VideoFXMotionStyle::PanRight,
            VideoFXMotionStyle::ZoomOut, VideoFXMotionStyle::PanLeft};
        static const VideoFXMotionStyle verticalCycle[] = {
            VideoFXMotionStyle::PanDown, VideoFXMotionStyle::ZoomIn,
            VideoFXMotionStyle::PanUp, VideoFXMotionStyle::ZoomOut};
        style = (vertical ? verticalCycle : horizontalCycle)[segmentIndex % 4];
    }
    VideoFXImageMotion r = m;
    r.style = VideoFXMotionStyle::Custom;
    r.startX = r.startY = r.endX = r.endY = 0.5;
    const double pan = 1.2;
    switch (style) {
        case VideoFXMotionStyle::ZoomIn:   r.startZoom = 1.0; r.endZoom = 1.25; break;
        case VideoFXMotionStyle::ZoomOut:  r.startZoom = 1.25; r.endZoom = 1.0; break;
        case VideoFXMotionStyle::PanRight: r.startZoom = r.endZoom = pan; r.startX = 0.0; r.endX = 1.0; break;
        case VideoFXMotionStyle::PanLeft:  r.startZoom = r.endZoom = pan; r.startX = 1.0; r.endX = 0.0; break;
        case VideoFXMotionStyle::PanDown:  r.startZoom = r.endZoom = pan; r.startY = 0.0; r.endY = 1.0; break;
        case VideoFXMotionStyle::PanUp:    r.startZoom = r.endZoom = pan; r.startY = 1.0; r.endY = 0.0; break;
        default: break;
    }
    return r;
}

KenBurnsView ViewAt(const VideoFXImageMotion& m, double fraction) {
    double f = std::clamp(fraction, 0.0, 1.0);
    if (m.easeInOut) f = f * f * (3.0 - 2.0 * f);          // smoothstep
    KenBurnsView v;
    if (m.style == VideoFXMotionStyle::Still) return v;
    const double z0 = std::max(1.0, m.startZoom), z1 = std::max(1.0, m.endZoom);
    v.zoom = std::exp(Lerp(std::log(z0), std::log(z1), f));
    v.centerX = Lerp(m.startX, m.endX, f);
    v.centerY = Lerp(m.startY, m.endY, f);
    return v;
}

void ViewRect(const KenBurnsView& view, int imageW, int imageH, int outW, int outH,
              double& x, double& y, double& w, double& h) {
    const double frameAspect = static_cast<double>(outW) / outH;
    // Largest frame-shaped rectangle inside the image (zoom 1)
    double baseW = imageW, baseH = imageW / frameAspect;
    if (baseH > imageH) { baseH = imageH; baseW = imageH * frameAspect; }
    w = baseW / std::max(1.0, view.zoom);
    h = baseH / std::max(1.0, view.zoom);
    // Centre on the view's point; slide back inside the image
    x = std::clamp(view.centerX * imageW - w / 2.0, 0.0, imageW - w);
    y = std::clamp(view.centerY * imageH - h / 2.0, 0.0, imageH - h);
}

VideoFXImageFit ResolveImageFit(VideoFXImageFit fit, int imageW, int imageH, int outW, int outH) {
    if (fit != VideoFXImageFit::Auto) return fit;
    const double imageAspect = imageH > 0 ? static_cast<double>(imageW) / imageH : 1.0;
    const double frameAspect = outH > 0 ? static_cast<double>(outW) / outH : 1.0;
    // Much taller than the frame (a portrait in 16:9 would lose two thirds):
    // show it whole. A 4:3 or 3:2 photo, or a panorama, still fills the frame.
    return imageAspect < frameAspect / 1.4 ? VideoFXImageFit::BlurredBackground : VideoFXImageFit::Cover;
}

void ContainViewRect(const KenBurnsView& view, int imageW, int imageH, int outW, int outH,
                     double& x, double& y, double& w, double& h) {
    const double frameAspect = static_cast<double>(outW) / outH;
    const double imageAspect = static_cast<double>(imageW) / imageH;
    double baseW, baseH;
    if (imageAspect > frameAspect) { baseW = imageW; baseH = imageW / frameAspect; }
    else { baseH = imageH; baseW = imageH * frameAspect; }
    w = baseW / std::max(1.0, view.zoom);
    h = baseH / std::max(1.0, view.zoom);
    x = w >= imageW ? (imageW - w) / 2.0 : std::clamp(view.centerX * imageW - w / 2.0, 0.0, imageW - w);
    y = h >= imageH ? (imageH - h) / 2.0 : std::clamp(view.centerY * imageH - h / 2.0, 0.0, imageH - h);
}

void MakeBlurredBackdrop(const VideoFXFrame& image, int outW, int outH, std::vector<uint8_t>& rgba) {
    // Cover the frame at 1/12 size, blur that, scale it back up: cheap, and
    // a blur this wide needs no detail anyway
    const int sw = std::max(8, outW / 12), sh = std::max(8, outH / 12);
    VideoFXFrame small;
    small.width = sw;
    small.height = sh;
    small.pixels.resize(static_cast<size_t>(sw) * sh * 4);
    double x, y, w, h;
    ViewRect(KenBurnsView{}, image.width, image.height, sw, sh, x, y, w, h);
    RenderView(image, x, y, w, h, sw, sh, small.pixels.data(), sw * 4, 1);

    // Three box-blur passes each way approximate a Gaussian
    std::vector<float> buf(small.pixels.begin(), small.pixels.end()), tmp(buf.size());
    const int radius = 2;
    for (int pass = 0; pass < 3; ++pass) {
        for (int dir = 0; dir < 2; ++dir) {
            const int len = dir == 0 ? sw : sh, lines = dir == 0 ? sh : sw;
            for (int line = 0; line < lines; ++line) {
                for (int i = 0; i < len; ++i) {
                    float sum[4] = {0, 0, 0, 0};
                    for (int k = -radius; k <= radius; ++k) {
                        const int j = std::clamp(i + k, 0, len - 1);
                        const size_t p = dir == 0 ? (static_cast<size_t>(line) * sw + j) * 4
                                                  : (static_cast<size_t>(j) * sw + line) * 4;
                        for (int c = 0; c < 4; ++c) sum[c] += buf[p + c];
                    }
                    const size_t o = dir == 0 ? (static_cast<size_t>(line) * sw + i) * 4
                                              : (static_cast<size_t>(i) * sw + line) * 4;
                    for (int c = 0; c < 4; ++c) tmp[o + c] = sum[c] / (2 * radius + 1);
                }
            }
            std::swap(buf, tmp);
        }
    }
    // Darkened, so the sharp picture in front stands out
    for (size_t i = 0; i < buf.size(); i += 4) {
        for (int c = 0; c < 3; ++c) small.pixels[i + c] = static_cast<uint8_t>(std::clamp(buf[i + c] * 0.6f, 0.0f, 255.0f));
        small.pixels[i + 3] = 255;
    }
    rgba.assign(static_cast<size_t>(outW) * outH * 4, 0);
    RenderView(small, 0, 0, sw, sh, outW, outH, rgba.data(), outW * 4);
}

void RenderView(const VideoFXFrame& image, double x, double y, double w, double h,
                int outW, int outH, uint8_t* dst, int dstStride, int threads, const uint8_t* background) {
    const int iw = image.width, ih = image.height;
    const uint8_t* src = image.pixels.data();
    const double sx = w / outW, sy = h / outH;

    // Column lookup, shared by every row: source index pair and weight
    struct Tap { int x0, x1; float fx; bool inside; };
    std::vector<Tap> cols(static_cast<size_t>(outW));
    for (int i = 0; i < outW; ++i) {
        const double px = x + (i + 0.5) * sx - 0.5;
        Tap t;
        t.inside = px >= -0.5 && px <= iw - 0.5;
        const double c = std::clamp(px, 0.0, static_cast<double>(iw - 1));
        t.x0 = static_cast<int>(std::floor(c));
        t.x1 = std::min(t.x0 + 1, iw - 1);
        t.fx = static_cast<float>(c - t.x0);
        cols[static_cast<size_t>(i)] = t;
    }

    auto renderRows = [&](int from, int to) {
        for (int j = from; j < to; ++j) {
            uint8_t* out = dst + static_cast<ptrdiff_t>(j) * dstStride;
            const uint8_t* back = background ? background + static_cast<size_t>(j) * outW * 4 : nullptr;
            auto fillOutside = [&](int i) {
                uint8_t* o = out + 4 * i;
                if (back) { o[0] = back[4 * i]; o[1] = back[4 * i + 1]; o[2] = back[4 * i + 2]; }
                else { o[0] = o[1] = o[2] = 0; }
                o[3] = 255;
            };
            const double py = y + (j + 0.5) * sy - 0.5;
            if (py < -0.5 || py > ih - 0.5) {
                for (int i = 0; i < outW; ++i) fillOutside(i);
                continue;
            }
            const double cy = std::clamp(py, 0.0, static_cast<double>(ih - 1));
            const int y0 = static_cast<int>(std::floor(cy));
            const int y1 = std::min(y0 + 1, ih - 1);
            const float fy = static_cast<float>(cy - y0);
            const uint8_t* r0 = src + static_cast<size_t>(y0) * iw * 4;
            const uint8_t* r1 = src + static_cast<size_t>(y1) * iw * 4;
            for (int i = 0; i < outW; ++i) {
                const Tap& t = cols[static_cast<size_t>(i)];
                uint8_t* o = out + 4 * i;
                if (!t.inside) { fillOutside(i); continue; }
                const uint8_t* a = r0 + 4 * t.x0; const uint8_t* b = r0 + 4 * t.x1;
                const uint8_t* c = r1 + 4 * t.x0; const uint8_t* d = r1 + 4 * t.x1;
                float v[4];
                for (int k = 0; k < 4; ++k) {
                    const float top = a[k] + (b[k] - a[k]) * t.fx;
                    const float bottom = c[k] + (d[k] - c[k]) * t.fx;
                    v[k] = top + (bottom - top) * fy;
                }
                // Transparent parts of a PNG show the background (black by default)
                const float alpha = v[3] / 255.0f;
                for (int k = 0; k < 3; ++k) {
                    const float under = back ? back[4 * i + k] : 0.0f;
                    o[k] = static_cast<uint8_t>(v[k] * alpha + under * (1.0f - alpha) + 0.5f);
                }
                o[3] = 255;
            }
        }
    };

    int n = threads > 0 ? threads : static_cast<int>(std::thread::hardware_concurrency());
    n = std::clamp(n, 1, 8);
    if (n == 1 || outH < 64) {
        renderRows(0, outH);
        return;
    }
    std::vector<std::thread> pool;
    const int band = (outH + n - 1) / n;
    for (int t = 0; t < n; ++t) {
        const int from = t * band, to = std::min(outH, from + band);
        if (from < to) pool.emplace_back(renderRows, from, to);
    }
    for (std::thread& th : pool) th.join();
}

} // namespace Internal
} // namespace VideoFX
