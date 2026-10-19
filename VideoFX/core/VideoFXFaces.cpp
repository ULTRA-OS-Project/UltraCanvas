// VideoFX/core/VideoFXFaces.cpp
// The built-in face detector: OpenCV's frontal-face Haar cascade, evaluated
// the way OpenCV's CascadeClassifier does it (image pyramid, window variance
// normalisation, stump stages, rectangle grouping) so it finds what OpenCV
// finds - without linking OpenCV.
// Version: 0.6.0
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework

#include "VideoFXFaces.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <numeric>
#include <thread>

namespace VideoFX {
namespace Internal {

namespace {

struct CascadeStage { int first; int count; float threshold; };
struct CascadeWeak { int feature; float threshold; float below; float above; };
struct CascadeRect { int x, y, w, h; float weight; };
struct CascadeFeature { CascadeRect rects[3]; };

#include "VideoFXFaceCascade.inc"

// Integral images of a grey image: sums and sums of squares over any
// rectangle in four lookups
struct Integral {
    int width = 0, height = 0;                  // of the image; the tables are one larger
    std::vector<int32_t> sum;
    std::vector<int64_t> sq;

    void Build(const uint8_t* gray, int w, int h, int stride) {
        width = w;
        height = h;
        const size_t cols = static_cast<size_t>(w) + 1;
        sum.assign(cols * (h + 1), 0);
        sq.assign(cols * (h + 1), 0);
        for (int y = 0; y < h; ++y) {
            int32_t rowSum = 0;
            int64_t rowSq = 0;
            for (int x = 0; x < w; ++x) {
                const int v = gray[static_cast<size_t>(y) * stride + x];
                rowSum += v;
                rowSq += v * v;
                sum[(y + 1) * cols + x + 1] = sum[y * cols + x + 1] + rowSum;
                sq[(y + 1) * cols + x + 1] = sq[y * cols + x + 1] + rowSq;
            }
        }
    }
};

// The cascade's rectangles as offsets into one level's integral table: a
// rectangle's sum is then four lookups from the window's corner
struct Evaluator {
    struct Rect { ptrdiff_t a, b, c, d; float weight; };
    std::vector<Rect> rects;                    // three per feature
    ptrdiff_t na = 0, nb = 0, nc = 0, nd = 0;   // the normalisation area

    void Prepare(int cols) {
        auto offsets = [cols](int x, int y, int w, int h, ptrdiff_t& a, ptrdiff_t& b, ptrdiff_t& c, ptrdiff_t& d) {
            a = static_cast<ptrdiff_t>(y) * cols + x;
            b = static_cast<ptrdiff_t>(y) * cols + x + w;
            c = static_cast<ptrdiff_t>(y + h) * cols + x;
            d = static_cast<ptrdiff_t>(y + h) * cols + x + w;
        };
        rects.resize(std::size(kCascadeFeatures) * 3);
        for (size_t f = 0; f < std::size(kCascadeFeatures); ++f)
            for (int k = 0; k < 3; ++k) {
                const CascadeRect& r = kCascadeFeatures[f].rects[k];
                Rect& o = rects[f * 3 + k];
                offsets(r.x, r.y, r.w, r.h, o.a, o.b, o.c, o.d);
                o.weight = r.weight;
            }
        offsets(1, 1, kCascadeWidth - 2, kCascadeHeight - 2, na, nb, nc, nd);
    }

    // Does the cascade see a face in the window whose corner is table entry `at`?
    bool Classify(const Integral& ii, ptrdiff_t at) const {
        const int32_t* s = ii.sum.data() + at;
        const int64_t* q = ii.sq.data() + at;
        // Normalised by the window's deviation (its inner area, as OpenCV
        // does), so a face in dim light scores like one in bright light
        const double area = static_cast<double>(kCascadeWidth - 2) * (kCascadeHeight - 2);
        const double sum = s[nd] - s[nb] - s[nc] + s[na];
        const double sq = static_cast<double>(q[nd] - q[nb] - q[nc] + q[na]);
        double nf = area * sq - sum * sum;
        nf = nf > 0.0 ? std::sqrt(nf) : 1.0;
        for (const CascadeStage& stage : kCascadeStages) {
            double total = 0.0;
            for (int i = stage.first; i < stage.first + stage.count; ++i) {
                const CascadeWeak& weak = kCascadeWeaks[i];
                const Rect* r = &rects[static_cast<size_t>(weak.feature) * 3];
                double v = 0.0;
                for (int k = 0; k < 3; ++k)
                    if (r[k].weight != 0.0f) v += r[k].weight * (s[r[k].d] - s[r[k].b] - s[r[k].c] + s[r[k].a]);
                // value / deviation < threshold, without the division
                total += v < weak.threshold * nf ? weak.below : weak.above;
            }
            if (total < stage.threshold) return false;
        }
        return true;
    }
};

// Bilinear resize of a grey image (the pyramid's levels)
void ResizeGray(const uint8_t* src, int sw, int sh, int stride, std::vector<uint8_t>& dst, int dw, int dh) {
    dst.resize(static_cast<size_t>(dw) * dh);
    const double fx = static_cast<double>(sw) / dw, fy = static_cast<double>(sh) / dh;
    for (int y = 0; y < dh; ++y) {
        const double sy = std::clamp((y + 0.5) * fy - 0.5, 0.0, sh - 1.0);
        const int y0 = static_cast<int>(sy), y1 = std::min(sh - 1, y0 + 1);
        const double ty = sy - y0;
        for (int x = 0; x < dw; ++x) {
            const double sx = std::clamp((x + 0.5) * fx - 0.5, 0.0, sw - 1.0);
            const int x0 = static_cast<int>(sx), x1 = std::min(sw - 1, x0 + 1);
            const double tx = sx - x0;
            const double top = src[static_cast<size_t>(y0) * stride + x0] * (1 - tx) +
                               src[static_cast<size_t>(y0) * stride + x1] * tx;
            const double bottom = src[static_cast<size_t>(y1) * stride + x0] * (1 - tx) +
                                  src[static_cast<size_t>(y1) * stride + x1] * tx;
            dst[static_cast<size_t>(y) * dw + x] = static_cast<uint8_t>(std::lround(top * (1 - ty) + bottom * ty));
        }
    }
}

// OpenCV's SimilarRects: corners within eps x the mean smaller side
bool Similar(const FaceBox& a, const FaceBox& b, double eps) {
    const double delta = eps * (std::min(a.w, b.w) + std::min(a.h, b.h)) * 0.5;
    return std::abs(a.x - b.x) <= delta && std::abs(a.y - b.y) <= delta &&
           std::abs(a.x + a.w - b.x - b.w) <= delta && std::abs(a.y + a.h - b.y - b.h) <= delta;
}

// OpenCV's groupRectangles: cluster similar hits, average each cluster, keep
// those more than `threshold` windows agree on, and drop a small face inside
// a larger, surer one
std::vector<FaceBox> Group(const std::vector<FaceBox>& hits, int threshold, double eps) {
    const size_t n = hits.size();
    std::vector<size_t> parent(n);
    std::iota(parent.begin(), parent.end(), 0);
    auto root = [&](size_t i) {
        while (parent[i] != i) i = parent[i] = parent[parent[i]];
        return i;
    };
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j)
            if (Similar(hits[i], hits[j], eps)) parent[root(i)] = root(j);
    std::vector<int> label(n, -1);
    std::vector<FaceBox> mean;
    std::vector<double> sx, sy, sw, sh;
    for (size_t i = 0; i < n; ++i) {
        const size_t r = root(i);
        if (label[r] < 0) {
            label[r] = static_cast<int>(mean.size());
            mean.emplace_back();
            sx.push_back(0); sy.push_back(0); sw.push_back(0); sh.push_back(0);
        }
        const int k = label[r];
        sx[k] += hits[i].x; sy[k] += hits[i].y; sw[k] += hits[i].w; sh[k] += hits[i].h;
        ++mean[k].neighbours;
    }
    for (size_t k = 0; k < mean.size(); ++k) {
        const double s = 1.0 / mean[k].neighbours;
        mean[k].x = static_cast<int>(sx[k] * s);
        mean[k].y = static_cast<int>(sy[k] * s);
        mean[k].w = static_cast<int>(sw[k] * s);
        mean[k].h = static_cast<int>(sh[k] * s);
    }
    std::vector<FaceBox> out;
    for (size_t i = 0; i < mean.size(); ++i) {
        const FaceBox& r1 = mean[i];
        if (r1.neighbours <= threshold) continue;
        bool inside = false;
        for (size_t j = 0; j < mean.size() && !inside; ++j) {
            const FaceBox& r2 = mean[j];
            if (j == i || r2.neighbours <= threshold) continue;
            const int dx = static_cast<int>(std::lround(r2.w * eps));
            const int dy = static_cast<int>(std::lround(r2.h * eps));
            inside = r1.x >= r2.x - dx && r1.y >= r2.y - dy && r1.x + r1.w <= r2.x + r2.w + dx &&
                     r1.y + r1.h <= r2.y + r2.h + dy && (r2.neighbours > std::max(3, r1.neighbours) || r1.neighbours < 3);
        }
        if (!inside) out.push_back(r1);
    }
    return out;
}

// Too little colour anywhere to judge skin by (a black and white photo)
bool IsGrey(const VideoFXFrame& image) {
    const size_t n = static_cast<size_t>(image.width) * image.height;
    const size_t step = std::max<size_t>(1, n / 20000);
    size_t tinted = 0, seen = 0;
    for (size_t i = 0; i < n; i += step, ++seen) {
        const uint8_t* p = &image.pixels[i * 4];
        if (std::max({std::abs(p[0] - p[1]), std::abs(p[1] - p[2]), std::abs(p[0] - p[2])}) > 12) ++tinted;
    }
    return tinted * 50 < seen;                  // under 2 % of the pixels tinted
}

// Is a quarter or more of the box's middle (eyes to mouth, not hair or
// background) skin-coloured? The YCbCr chroma range used for skin detection
// (Cb 77..127, Cr 133..173): light to dark skin, any brightness.
bool HasSkin(const VideoFXFrame& image, const VideoFXRect& r) {
    const int x0 = static_cast<int>((r.x + r.w * 0.25) * image.width);
    const int x1 = static_cast<int>((r.x + r.w * 0.75) * image.width);
    const int y0 = static_cast<int>((r.y + r.h * 0.30) * image.height);
    const int y1 = static_cast<int>((r.y + r.h * 0.85) * image.height);
    int skin = 0, all = 0;
    for (int y = std::max(0, y0); y < std::min(image.height, y1); ++y)
        for (int x = std::max(0, x0); x < std::min(image.width, x1); ++x) {
            const uint8_t* p = &image.pixels[(static_cast<size_t>(y) * image.width + x) * 4];
            const double cb = 128.0 - 0.168736 * p[0] - 0.331264 * p[1] + 0.5 * p[2];
            const double cr = 128.0 + 0.5 * p[0] - 0.418688 * p[1] - 0.081312 * p[2];
            if (cb >= 77 && cb <= 127 && cr >= 133 && cr <= 173) ++skin;
            ++all;
        }
    return all > 0 && skin * 4 >= all;
}

} // namespace

std::vector<FaceBox> DetectFacesGray(const uint8_t* gray, int width, int height, int stride,
                                     double scaleFactor, int minNeighbours, int minSize) {
    std::vector<FaceBox> hits;
    if (!gray || width < kCascadeWidth || height < kCascadeHeight || scaleFactor <= 1.0) return {};
    std::vector<uint8_t> level;
    Integral ii;
    Evaluator eval;
    for (double factor = 1.0;; factor *= scaleFactor) {
        const int winW = static_cast<int>(std::lround(kCascadeWidth * factor));
        const int winH = static_cast<int>(std::lround(kCascadeHeight * factor));
        if (winW > width || winH > height) break;
        if (winW < minSize || winH < minSize) continue;
        const int lw = static_cast<int>(std::lround(width / factor));
        const int lh = static_cast<int>(std::lround(height / factor));
        if (lw < kCascadeWidth || lh < kCascadeHeight) break;
        if (factor == 1.0) {
            ii.Build(gray, width, height, stride);
        } else {
            ResizeGray(gray, width, height, stride, level, lw, lh);
            ii.Build(level.data(), lw, lh, lw);
        }
        eval.Prepare(lw + 1);
        const int step = factor > 2.0 ? 1 : 2;
        const int rows = (lh - kCascadeHeight) / step + 1;
        // Rows split over the CPU's threads; each keeps its own hits, joined
        // in row order so the result does not depend on the thread count
        const int threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1,
                                       std::max(1, std::min(8, rows / 8)));
        std::vector<std::vector<FaceBox>> found(threads);
        auto scan = [&](int t) {
            for (int row = t * rows / threads; row < (t + 1) * rows / threads; ++row) {
                const int y = row * step;
                for (int x = 0; x + kCascadeWidth <= lw; x += step)
                    if (eval.Classify(ii, static_cast<ptrdiff_t>(y) * (lw + 1) + x))
                        found[t].push_back(FaceBox{static_cast<int>(std::lround(x * factor)),
                                                   static_cast<int>(std::lround(y * factor)), winW, winH, 1});
            }
        };
        std::vector<std::thread> pool;
        for (int t = 1; t < threads; ++t) pool.emplace_back(scan, t);
        scan(0);
        for (std::thread& th : pool) th.join();
        for (const auto& part : found) hits.insert(hits.end(), part.begin(), part.end());
    }
    return Group(hits, minNeighbours, 0.2);
}

std::vector<VideoFXRect> DetectFaces(const VideoFXFrame& image, int maxSide) {
    std::vector<VideoFXRect> faces;
    if (!image.IsValid()) return faces;
    // Grey (BT.601 luma, as OpenCV's RGB2GRAY) and no larger than maxSide
    const double shrink = std::min(1.0, static_cast<double>(std::max(32, maxSide)) /
                                            std::max(image.width, image.height));
    const int w = std::max(1, static_cast<int>(std::lround(image.width * shrink)));
    const int h = std::max(1, static_cast<int>(std::lround(image.height * shrink)));
    std::vector<uint8_t> full(static_cast<size_t>(image.width) * image.height);
    for (size_t i = 0; i < full.size(); ++i) {
        const uint8_t* p = &image.pixels[i * 4];
        full[i] = static_cast<uint8_t>((p[0] * 4899 + p[1] * 9617 + p[2] * 1868 + 8192) >> 14);
    }
    std::vector<uint8_t> gray;
    if (w == image.width && h == image.height) gray.swap(full);
    else ResizeGray(full.data(), image.width, image.height, image.width, gray, w, h);

    std::vector<FaceBox> boxes = DetectFacesGray(gray.data(), w, h, w);
    // A real face is found by many windows around it - more the larger it is
    // (0.16..0.56 windows per pixel of width in the tests); the cascade's
    // weakest stray hits by fewer (0.07). Under 0.1 a pixel is dropped.
    constexpr double kMinAgreement = 0.1;
    // In a colour photo a face also has skin in its middle; the cascade's
    // stray hits on clothing, walls and texture mostly do not. A black and
    // white photo cannot be told apart this way and keeps them.
    const bool colour = !IsGrey(image);
    boxes.erase(std::remove_if(boxes.begin(), boxes.end(),
                               [&](const FaceBox& b) {
                                   return b.neighbours < kMinAgreement * b.w ||
                                          (colour && !HasSkin(image, VideoFXRect::FromPixels(b.x, b.y, b.w, b.h, w, h)));
                               }),
                boxes.end());
    std::sort(boxes.begin(), boxes.end(), [](const FaceBox& a, const FaceBox& b) { return a.w * a.h > b.w * b.h; });
    for (const FaceBox& b : boxes) {
        VideoFXRect r = VideoFXRect::FromPixels(b.x, b.y, b.w, b.h, w, h);
        r.x = std::clamp(r.x, 0.0, 1.0);
        r.y = std::clamp(r.y, 0.0, 1.0);
        r.w = std::min(r.w, 1.0 - r.x);
        r.h = std::min(r.h, 1.0 - r.y);
        if (!r.IsEmpty()) faces.push_back(r);
    }
    return faces;
}

} // namespace Internal
} // namespace VideoFX
