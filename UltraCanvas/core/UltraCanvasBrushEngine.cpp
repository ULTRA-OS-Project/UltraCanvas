// core/UltraCanvasBrushEngine.cpp
// Brush strokes (dab stamping with sub-pixel interpolation, opacity / flow
// separation, erase / clone / smudge / dodge / burn) and the one-shot
// raster operations (anti-aliased shapes, flood fill, magic wand, gradients,
// mask stamping, eyedropper).
// Version: 1.1.0 - DrawCurve / FlattenCubicBezier, DrawArrow / ArrowShape; StrokePath:
//                  a path's stroke as one coverage map, each segment visiting
//                  only the pixels near it
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "UltraCanvasBrushEngine.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace UltraCanvas {

namespace {

inline uint8_t ClampByte(float v) {
    return static_cast<uint8_t>(std::clamp(std::lround(v), 0L, 255L));
}

inline Rect2Di UnionRect(const Rect2Di& a, const Rect2Di& b) {
    if (a.width <= 0 || a.height <= 0) return b;
    if (b.width <= 0 || b.height <= 0) return a;
    const int x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
    const int x1 = std::max(a.x + a.width, b.x + b.width);
    const int y1 = std::max(a.y + a.height, b.y + b.height);
    return Rect2Di(x0, y0, x1 - x0, y1 - y0);
}

inline uint8_t SelCoverage(const UCRasterSelection* sel, int x, int y) {
    return sel ? sel->Coverage(x, y) : 255;
}

// Coverage of a round / square dab of `radius` centred on (cx, cy) at pixel
// centre (px, py). Hardness sets where the falloff starts; the last pixel
// of the edge is anti-aliased.
inline float DabCoverage(float px, float py, float cx, float cy, float radius,
                         float hardness, bool antialias, BrushShape shape) {
    float d;
    if (shape == BrushShape::Square) {
        d = std::max(std::fabs(px - cx), std::fabs(py - cy));
    } else {
        const float dx = px - cx, dy = py - cy;
        d = std::sqrt(dx * dx + dy * dy);
    }
    if (radius <= 0.5f) {
        // A one-pixel brush: the pixel under the centre, nothing else.
        return (std::fabs(px - cx) <= 0.5f && std::fabs(py - cy) <= 0.5f) ? 1.0f : 0.0f;
    }
    if (!antialias) return d <= radius ? 1.0f : 0.0f;
    const float inner = radius * std::clamp(hardness, 0.0f, 1.0f);
    if (d <= inner) return 1.0f;
    // Soft falloff from `inner` to `radius`, then a half-pixel AA ramp.
    const float edge = radius + 0.5f;
    if (d >= edge) return 0.0f;
    const float softSpan = std::max(0.5f, radius - inner);
    float t = (d - inner) / softSpan;           // 0 at inner .. 1 at radius
    if (d > radius) t = 1.0f;
    float cov = 1.0f - t;
    cov = cov * cov * (3.0f - 2.0f * cov);       // smoothstep
    if (d > radius) cov = 0.0f;
    // AA ramp over the last pixel regardless of hardness
    const float aa = std::clamp(edge - d, 0.0f, 1.0f);
    return std::max(cov, hardness >= 0.999f ? aa : std::min(aa, cov + aa * (1.0f - cov)));
}

} // namespace

// ===========================================================================
// STROKE
// ===========================================================================

void UCBrushStroke::Begin(std::shared_ptr<UCRasterLayer> target, const UCRasterSelection* sel,
                          const UCBrushSettings& s, BrushMode m, const RasterPixel& c,
                          RasterBlendMode b) {
    layer = std::move(target);
    before = layer ? layer->Clone() : nullptr;
    selection = sel;
    settings = s;
    settings.size = std::max(1.0f, settings.size);
    mode = m;
    colour = c;
    blend = b;
    strokeMask.assign(layer ? static_cast<size_t>(layer->GetWidth()) * layer->GetHeight() : 0, 0);
    dirtyBounds = Rect2Di(0, 0, 0, 0);
    hasLast = false;
    distanceCarry = 0.0f;
    smudgeLoaded = false;
    smudgeBuffer.clear();
    smudgeSize = 0;
    cloneSource = nullptr;
    cloneDx = cloneDy = 0;
    active = layer != nullptr && layer->IsValid();
}

void UCBrushStroke::SetCloneSource(std::shared_ptr<UCRasterLayer> sourceLayer, int offsetX, int offsetY) {
    cloneSource = std::move(sourceLayer);
    cloneDx = offsetX;
    cloneDy = offsetY;
}

Rect2Di UCBrushStroke::AddPoint(float x, float y, float pressure) {
    if (!active) return Rect2Di(0, 0, 0, 0);
    Rect2Di changed(0, 0, 0, 0);
    const float spacingPx = std::max(0.5f, settings.size * std::max(0.02f, settings.spacing));
    if (!hasLast) {
        changed = Dab(x, y, pressure);
        hasLast = true;
        lastX = x; lastY = y;
        distanceCarry = 0.0f;
        return changed;
    }
    const float dx = x - lastX, dy = y - lastY;
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist <= 0.0f) return changed;
    float travelled = distanceCarry;
    // Lay dabs every `spacingPx` along the segment, carrying the remainder
    // into the next segment so speed does not change the dab density.
    float pos = spacingPx - travelled;
    while (pos <= dist) {
        const float t = pos / dist;
        changed = UnionRect(changed, Dab(lastX + dx * t, lastY + dy * t, pressure));
        pos += spacingPx;
    }
    distanceCarry = dist - (pos - spacingPx);
    lastX = x; lastY = y;
    return changed;
}

Rect2Di UCBrushStroke::End() {
    active = false;
    before.reset();
    strokeMask.clear();
    smudgeBuffer.clear();
    return dirtyBounds;
}

Rect2Di UCBrushStroke::Dab(float cx, float cy, float pressure) {
    pressure = std::clamp(pressure, 0.0f, 1.0f);
    float diameter = settings.size;
    if (settings.pressureSize) diameter = std::max(1.0f, diameter * pressure);
    const float radius = diameter * 0.5f;
    float flow = std::clamp(settings.flow, 0.0f, 1.0f);
    if (settings.pressureOpacity) flow *= pressure;
    if (flow <= 0.0f) return Rect2Di(0, 0, 0, 0);

    const int W = layer->GetWidth(), H = layer->GetHeight();
    const int x0 = std::max(0, static_cast<int>(std::floor(cx - radius - 1.0f)));
    const int y0 = std::max(0, static_cast<int>(std::floor(cy - radius - 1.0f)));
    const int x1 = std::min(W - 1, static_cast<int>(std::ceil(cx + radius + 1.0f)));
    const int y1 = std::min(H - 1, static_cast<int>(std::ceil(cy + radius + 1.0f)));
    if (x1 < x0 || y1 < y0) return Rect2Di(0, 0, 0, 0);

    if (mode == BrushMode::Smudge) {
        ApplySmudge(cx, cy, radius);
        const Rect2Di r(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
        dirtyBounds = UnionRect(dirtyBounds, r);
        return r;
    }

    // Accumulate this dab into the stroke mask with `flow`.
    for (int y = y0; y <= y1; ++y) {
        uint8_t* m = strokeMask.data() + static_cast<size_t>(y) * W;
        for (int x = x0; x <= x1; ++x) {
            const float cov = DabCoverage(x + 0.5f, y + 0.5f, cx, cy, radius,
                                          settings.hardness, settings.antialias, settings.shape);
            if (cov <= 0.0f) continue;
            const float deposit = cov * flow;
            const float cur = m[x] / 255.0f;
            const float next = settings.antialias ? cur + deposit * (1.0f - cur)
                                                  : std::max(cur, deposit);
            m[x] = ClampByte(next * 255.0f);
        }
    }
    const Rect2Di r(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
    Recomposite(r);
    dirtyBounds = UnionRect(dirtyBounds, r);
    return r;
}

// Rebuild `r` of the layer from the "before" copy and the stroke mask.
void UCBrushStroke::Recomposite(const Rect2Di& r) {
    const int W = layer->GetWidth();
    const float opacity = std::clamp(settings.opacity, 0.0f, 1.0f);
    for (int y = r.y; y < r.y + r.height; ++y) {
        const uint8_t* m = strokeMask.data() + static_cast<size_t>(y) * W;
        const uint8_t* b = before->Row(y);
        uint8_t* d = layer->Row(y);
        for (int x = r.x; x < r.x + r.width; ++x) {
            const int sc = SelCoverage(selection, x, y);
            const float cov = (m[x] / 255.0f) * opacity * (sc / 255.0f);
            const uint8_t* bp = b + static_cast<size_t>(x) * 4;
            uint8_t* dp = d + static_cast<size_t>(x) * 4;
            if (cov <= 0.0f) {
                dp[0] = bp[0]; dp[1] = bp[1]; dp[2] = bp[2]; dp[3] = bp[3];
                continue;
            }
            const RasterPixel base(bp[0], bp[1], bp[2], bp[3]);
            RasterPixel out = base;
            switch (mode) {
                case BrushMode::Paint:
                    out = RasterBlendPixel(colour, base, cov, blend);
                    break;
                case BrushMode::Erase:
                    out.a = ClampByte(base.a * (1.0f - cov));
                    break;
                case BrushMode::Clone: {
                    const UCRasterLayer* src = cloneSource ? cloneSource.get() : before.get();
                    if (cloneSource.get() == layer.get()) src = before.get();
                    const RasterPixel sp = src->GetPixel(x + cloneDx, y + cloneDy);
                    out = RasterBlendPixel(sp, base, cov, RasterBlendMode::Normal);
                    break;
                }
                case BrushMode::Dodge: {
                    const float k = 1.0f + 0.5f * cov;
                    out = RasterPixel(ClampByte(base.r * k), ClampByte(base.g * k), ClampByte(base.b * k), base.a);
                    break;
                }
                case BrushMode::Burn: {
                    const float k = 1.0f - 0.5f * cov;
                    out = RasterPixel(ClampByte(base.r * k), ClampByte(base.g * k), ClampByte(base.b * k), base.a);
                    break;
                }
                case BrushMode::Smudge:
                    break;
            }
            dp[0] = out.r; dp[1] = out.g; dp[2] = out.b; dp[3] = out.a;
        }
    }
}

// Smudge: a carried colour buffer of the dab's size is blended into the
// layer with `flow` and picks up what it just covered.
void UCBrushStroke::ApplySmudge(float cx, float cy, float radius) {
    const int size = std::max(1, static_cast<int>(std::ceil(radius * 2.0f)) + 2);
    if (smudgeSize != size) {
        smudgeSize = size;
        smudgeBuffer.assign(static_cast<size_t>(size) * size * 4, 0.0f);
        smudgeLoaded = false;
    }
    const int ox = static_cast<int>(std::floor(cx - size * 0.5f));
    const int oy = static_cast<int>(std::floor(cy - size * 0.5f));
    const float strength = std::clamp(settings.flow, 0.0f, 1.0f) * std::clamp(settings.opacity, 0.0f, 1.0f);
    for (int j = 0; j < size; ++j) {
        for (int i = 0; i < size; ++i) {
            const int x = ox + i, y = oy + j;
            float* c = smudgeBuffer.data() + (static_cast<size_t>(j) * size + i) * 4;
            const RasterPixel p = layer->GetPixel(x, y);
            const bool inside = x >= 0 && y >= 0 && x < layer->GetWidth() && y < layer->GetHeight();
            const float cov = DabCoverage(x + 0.5f, y + 0.5f, cx, cy, radius,
                                          settings.hardness, settings.antialias, settings.shape);
            if (smudgeLoaded && inside && cov > 0.0f) {
                const float k = cov * strength * (SelCoverage(selection, x, y) / 255.0f);
                // premultiplied mix keeps transparent areas from bleeding grey
                const float pa = p.a / 255.0f, ca = c[3];
                const float na = pa + (ca - pa) * k;
                RasterPixel out;
                if (na > 0.0f) {
                    out.r = ClampByte((p.r * pa + (c[0] * ca - p.r * pa) * k) / na);
                    out.g = ClampByte((p.g * pa + (c[1] * ca - p.g * pa) * k) / na);
                    out.b = ClampByte((p.b * pa + (c[2] * ca - p.b * pa) * k) / na);
                }
                out.a = ClampByte(na * 255.0f);
                layer->SetPixel(x, y, out);
            }
            // pick up
            const RasterPixel q = layer->GetPixel(x, y);
            const float pickup = smudgeLoaded ? 0.5f : 1.0f;
            c[0] += (q.r - c[0]) * pickup;
            c[1] += (q.g - c[1]) * pickup;
            c[2] += (q.b - c[2]) * pickup;
            c[3] += (q.a / 255.0f - c[3]) * pickup;
        }
    }
    smudgeLoaded = true;
}

// ===========================================================================
// ONE-SHOT OPERATIONS
// ===========================================================================

namespace RasterPaint {

int PixelDistance(const RasterPixel& a, const RasterPixel& b) {
    return (std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b) + std::abs(a.a - b.a)) / 4;
}

Rect2Di FillCoverage(UCRasterLayer& layer, const UCRasterSelection* sel, const Rect2Di& bboxIn,
                     const std::function<bool(float, float)>& inside,
                     const RasterPixel& colour, bool antialias, RasterBlendMode blend) {
    const Rect2Di bbox = layer.ClipRect(bboxIn);
    if (bbox.width <= 0 || bbox.height <= 0) return Rect2Di(0, 0, 0, 0);
    const int ss = antialias ? 4 : 1;
    const float inv = 1.0f / (ss * ss);
    for (int y = bbox.y; y < bbox.y + bbox.height; ++y) {
        uint8_t* row = layer.Row(y);
        for (int x = bbox.x; x < bbox.x + bbox.width; ++x) {
            int hits = 0;
            if (ss == 1) {
                hits = inside(x + 0.5f, y + 0.5f) ? 1 : 0;
            } else {
                for (int sy = 0; sy < ss; ++sy)
                    for (int sx = 0; sx < ss; ++sx)
                        if (inside(x + (sx + 0.5f) / ss, y + (sy + 0.5f) / ss)) ++hits;
            }
            if (!hits) continue;
            const float cov = hits * inv * (SelCoverage(sel, x, y) / 255.0f);
            if (cov <= 0.0f) continue;
            uint8_t* d = row + static_cast<size_t>(x) * 4;
            const RasterPixel out = RasterBlendPixel(colour, RasterPixel(d[0], d[1], d[2], d[3]), cov, blend);
            d[0] = out.r; d[1] = out.g; d[2] = out.b; d[3] = out.a;
        }
    }
    return bbox;
}

namespace {
    // Distance from point to segment.
    inline float SegmentDistance(float px, float py, float ax, float ay, float bx, float by) {
        const float vx = bx - ax, vy = by - ay;
        const float len2 = vx * vx + vy * vy;
        float t = len2 > 0.0f ? ((px - ax) * vx + (py - ay) * vy) / len2 : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        const float dx = px - (ax + vx * t), dy = py - (ay + vy * t);
        return std::sqrt(dx * dx + dy * dy);
    }

    // The stroke of a path - `pts` joined in order, and back to the first
    // when `closed` - `strokeWidth` wide with round joins and caps, laid down
    // as one coverage map: a pixel two segments cover is painted once, as
    // FillCoverage would. FillCoverage tests every segment at every sample
    // of the path's bounding box, which for a curve flattened into hundreds
    // of segments costs their product; here each segment marks the samples
    // near it only, so the cost follows the path's length. Same samples
    // (4x4, or the centre without anti-aliasing), same inside test.
    Rect2Di StrokePath(UCRasterLayer& layer, const UCRasterSelection* sel,
                       const std::vector<Point2Df>& pts, bool closed, const ShapeStyle& style) {
        if (pts.empty() || style.strokeWidth <= 0.0f || style.stroke.a == 0) return Rect2Di(0, 0, 0, 0);
        const float hw = style.strokeWidth * 0.5f;
        float minX = pts[0].x, maxX = pts[0].x, minY = pts[0].y, maxY = pts[0].y;
        for (const auto& p : pts) {
            minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
        }
        const Rect2Di box = layer.ClipRect(Rect2Di(
                static_cast<int>(std::floor(minX - hw - 1)), static_cast<int>(std::floor(minY - hw - 1)),
                static_cast<int>(std::ceil(maxX - minX + 2 * hw)) + 3, static_cast<int>(std::ceil(maxY - minY + 2 * hw)) + 3));
        if (box.width <= 0 || box.height <= 0) return Rect2Di(0, 0, 0, 0);

        const int ss = style.antialias ? 4 : 1;
        const int samples = ss * ss;
        const uint16_t all = static_cast<uint16_t>((1u << samples) - 1u);
        std::vector<uint16_t> hit(static_cast<size_t>(box.width) * box.height, 0);
        const size_t n = pts.size();
        const size_t segs = n == 1 ? 1 : (closed ? n : n - 1);
        for (size_t i = 0; i < segs; ++i) {
            const Point2Df& a = pts[i];
            const Point2Df& b = pts[(i + 1) % n];
            const int x0 = std::max(box.x, static_cast<int>(std::floor(std::min(a.x, b.x) - hw - 1)));
            const int y0 = std::max(box.y, static_cast<int>(std::floor(std::min(a.y, b.y) - hw - 1)));
            const int x1 = std::min(box.x + box.width, static_cast<int>(std::ceil(std::max(a.x, b.x) + hw + 1)) + 1);
            const int y1 = std::min(box.y + box.height, static_cast<int>(std::ceil(std::max(a.y, b.y) + hw + 1)) + 1);
            for (int y = y0; y < y1; ++y) {
                uint16_t* row = hit.data() + static_cast<size_t>(y - box.y) * box.width;
                for (int x = x0; x < x1; ++x) {
                    uint16_t& m = row[x - box.x];
                    if (m == all) continue;
                    for (int sy = 0; sy < ss; ++sy) {
                        for (int sx = 0; sx < ss; ++sx) {
                            const uint16_t bit = static_cast<uint16_t>(1u << (sy * ss + sx));
                            if (m & bit) continue;
                            if (SegmentDistance(x + (sx + 0.5f) / ss, y + (sy + 0.5f) / ss, a.x, a.y, b.x, b.y) <= hw)
                                m = static_cast<uint16_t>(m | bit);
                        }
                    }
                }
            }
        }

        std::vector<uint8_t> mask(hit.size());
        for (size_t i = 0; i < hit.size(); ++i) {
            const int count = std::popcount(static_cast<unsigned>(hit[i]));
            mask[i] = static_cast<uint8_t>((count * 255 + samples / 2) / samples);
        }
        return StampMask(layer, sel, mask.data(), box.width, box.height, box.x, box.y,
                         style.stroke, 1.0f, style.blend);
    }

    // Inside the triangle (a, b, c), either winding.
    inline bool InTriangle(float px, float py, const Point2Df& a, const Point2Df& b, const Point2Df& c) {
        const float d1 = (px - b.x) * (a.y - b.y) - (a.x - b.x) * (py - b.y);
        const float d2 = (px - c.x) * (b.y - c.y) - (b.x - c.x) * (py - c.y);
        const float d3 = (px - a.x) * (c.y - a.y) - (c.x - a.x) * (py - a.y);
        const bool neg = d1 < 0 || d2 < 0 || d3 < 0;
        const bool pos = d1 > 0 || d2 > 0 || d3 > 0;
        return !(neg && pos);
    }
}

Rect2Di DrawLine(UCRasterLayer& layer, const UCRasterSelection* sel, Point2Df a, Point2Df b,
                 const ShapeStyle& style) {
    if (style.strokeWidth <= 0.0f || style.stroke.a == 0) return Rect2Di(0, 0, 0, 0);
    const float hw = style.strokeWidth * 0.5f;
    const Rect2Di bbox(static_cast<int>(std::floor(std::min(a.x, b.x) - hw - 1)),
                       static_cast<int>(std::floor(std::min(a.y, b.y) - hw - 1)),
                       static_cast<int>(std::ceil(std::fabs(a.x - b.x) + 2 * hw + 3)),
                       static_cast<int>(std::ceil(std::fabs(a.y - b.y) + 2 * hw + 3)));
    return FillCoverage(layer, sel, bbox,
        [&](float px, float py) { return SegmentDistance(px, py, a.x, a.y, b.x, b.y) <= hw; },
        style.stroke, style.antialias, style.blend);
}

Rect2Di DrawRectangle(UCRasterLayer& layer, const UCRasterSelection* sel, const Rect2Df& rIn,
                      const ShapeStyle& style) {
    Rect2Df r(std::min(rIn.x, rIn.x + rIn.width), std::min(rIn.y, rIn.y + rIn.height),
              std::fabs(rIn.width), std::fabs(rIn.height));
    Rect2Di changed(0, 0, 0, 0);
    const float hw = style.strokeWidth * 0.5f;
    if (style.fill.a > 0) {
        const Rect2Di bbox(static_cast<int>(std::floor(r.x)), static_cast<int>(std::floor(r.y)),
                           static_cast<int>(std::ceil(r.width)) + 2, static_cast<int>(std::ceil(r.height)) + 2);
        changed = UnionRect(changed, FillCoverage(layer, sel, bbox,
            [&](float px, float py) { return px >= r.x && px < r.x + r.width && py >= r.y && py < r.y + r.height; },
            style.fill, style.antialias, style.blend));
    }
    if (style.strokeWidth > 0.0f && style.stroke.a > 0) {
        const Rect2Di bbox(static_cast<int>(std::floor(r.x - hw - 1)), static_cast<int>(std::floor(r.y - hw - 1)),
                           static_cast<int>(std::ceil(r.width + 2 * hw)) + 3, static_cast<int>(std::ceil(r.height + 2 * hw)) + 3);
        changed = UnionRect(changed, FillCoverage(layer, sel, bbox,
            [&](float px, float py) {
                const bool outer = px >= r.x - hw && px < r.x + r.width + hw && py >= r.y - hw && py < r.y + r.height + hw;
                const bool inner = px >= r.x + hw && px < r.x + r.width - hw && py >= r.y + hw && py < r.y + r.height - hw;
                return outer && !inner;
            }, style.stroke, style.antialias, style.blend));
    }
    return changed;
}

Rect2Di DrawEllipse(UCRasterLayer& layer, const UCRasterSelection* sel, const Rect2Df& bIn,
                    const ShapeStyle& style) {
    Rect2Df b(std::min(bIn.x, bIn.x + bIn.width), std::min(bIn.y, bIn.y + bIn.height),
              std::fabs(bIn.width), std::fabs(bIn.height));
    const float cx = b.x + b.width * 0.5f, cy = b.y + b.height * 0.5f;
    const float rx = std::max(0.01f, b.width * 0.5f), ry = std::max(0.01f, b.height * 0.5f);
    const float hw = style.strokeWidth * 0.5f;
    Rect2Di changed(0, 0, 0, 0);
    auto insideRadius = [&](float px, float py, float ex, float ey) {
        if (ex <= 0.0f || ey <= 0.0f) return false;
        const float dx = (px - cx) / ex, dy = (py - cy) / ey;
        return dx * dx + dy * dy <= 1.0f;
    };
    if (style.fill.a > 0) {
        const Rect2Di bbox(static_cast<int>(std::floor(b.x)), static_cast<int>(std::floor(b.y)),
                           static_cast<int>(std::ceil(b.width)) + 2, static_cast<int>(std::ceil(b.height)) + 2);
        changed = UnionRect(changed, FillCoverage(layer, sel, bbox,
            [&](float px, float py) { return insideRadius(px, py, rx, ry); },
            style.fill, style.antialias, style.blend));
    }
    if (style.strokeWidth > 0.0f && style.stroke.a > 0) {
        const Rect2Di bbox(static_cast<int>(std::floor(b.x - hw - 1)), static_cast<int>(std::floor(b.y - hw - 1)),
                           static_cast<int>(std::ceil(b.width + 2 * hw)) + 3, static_cast<int>(std::ceil(b.height + 2 * hw)) + 3);
        changed = UnionRect(changed, FillCoverage(layer, sel, bbox,
            [&](float px, float py) {
                return insideRadius(px, py, rx + hw, ry + hw) && !insideRadius(px, py, rx - hw, ry - hw);
            }, style.stroke, style.antialias, style.blend));
    }
    return changed;
}

Rect2Di DrawPolygon(UCRasterLayer& layer, const UCRasterSelection* sel,
                    const std::vector<Point2Df>& pts, bool closed, const ShapeStyle& style) {
    if (pts.size() < 2) return Rect2Di(0, 0, 0, 0);
    Rect2Di changed(0, 0, 0, 0);
    float minX = pts[0].x, maxX = pts[0].x, minY = pts[0].y, maxY = pts[0].y;
    for (const auto& p : pts) {
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
    }
    if (closed && style.fill.a > 0 && pts.size() >= 3) {
        const Rect2Di bbox(static_cast<int>(std::floor(minX)), static_cast<int>(std::floor(minY)),
                           static_cast<int>(std::ceil(maxX - minX)) + 2, static_cast<int>(std::ceil(maxY - minY)) + 2);
        const size_t n = pts.size();
        changed = UnionRect(changed, FillCoverage(layer, sel, bbox,
            [&](float px, float py) {
                bool in = false;
                for (size_t i = 0, j = n - 1; i < n; j = i++) {
                    if ((pts[i].y > py) != (pts[j].y > py) &&
                        px < pts[i].x + (py - pts[i].y) * (pts[j].x - pts[i].x) / (pts[j].y - pts[i].y)) in = !in;
                }
                return in;
            }, style.fill, style.antialias, style.blend));
    }
    if (style.strokeWidth > 0.0f && style.stroke.a > 0) {
        changed = UnionRect(changed, StrokePath(layer, sel, pts, closed, style));
    }
    return changed;
}

std::vector<Point2Df> FlattenCubicBezier(Point2Df a, Point2Df c1, Point2Df c2, Point2Df b, float maxStep) {
    if (!(maxStep > 0.0f)) maxStep = 2.0f;
    auto dist = [](const Point2Df& p, const Point2Df& q) {
        return std::sqrt((p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y));
    };
    // The curve is no longer than its control polygon.
    const float hull = dist(a, c1) + dist(c1, c2) + dist(c2, b);
    const int n = std::clamp(static_cast<int>(std::ceil(hull / maxStep)), 8, 4096);
    std::vector<Point2Df> pts;
    pts.reserve(static_cast<size_t>(n) + 1);
    pts.push_back(a);
    for (int i = 1; i < n; ++i) {
        const float t = static_cast<float>(i) / n, mt = 1.0f - t;
        const float wa = mt * mt * mt, w1 = 3 * mt * mt * t, w2 = 3 * mt * t * t, wb = t * t * t;
        pts.emplace_back(wa * a.x + w1 * c1.x + w2 * c2.x + wb * b.x,
                         wa * a.y + w1 * c1.y + w2 * c2.y + wb * b.y);
    }
    pts.push_back(b);
    return pts;
}

Rect2Di DrawCurve(UCRasterLayer& layer, const UCRasterSelection* sel,
                  Point2Df a, Point2Df c1, Point2Df c2, Point2Df b, const ShapeStyle& style) {
    return StrokePath(layer, sel, FlattenCubicBezier(a, c1, c2, b), false, style);
}

ArrowGeometry ArrowShape(Point2Df tail, Point2Df tip, float headLength, float headWidth,
                         bool headAtTail, float strokeWidth) {
    ArrowGeometry g;
    const float dx = tip.x - tail.x, dy = tip.y - tail.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return g;
    g.valid = true;
    const float ux = dx / len, uy = dy / len;     // along the arrow
    const float nx = -uy, ny = ux;                 // across it
    const float length = std::clamp(headLength, 1.0f, headAtTail ? len * 0.5f : len);
    const float half = std::max(headWidth, std::max(0.0f, strokeWidth) + 2.0f) * 0.5f;

    const Point2Df tipBase(tip.x - ux * length, tip.y - uy * length);
    g.tipHead[0] = tip;
    g.tipHead[1] = Point2Df(tipBase.x + nx * half, tipBase.y + ny * half);
    g.tipHead[2] = Point2Df(tipBase.x - nx * half, tipBase.y - ny * half);
    g.shaftTo = tipBase;
    g.shaftFrom = tail;
    if (headAtTail) {
        const Point2Df tailBase(tail.x + ux * length, tail.y + uy * length);
        g.hasTailHead = true;
        g.tailHead[0] = tail;
        g.tailHead[1] = Point2Df(tailBase.x + nx * half, tailBase.y + ny * half);
        g.tailHead[2] = Point2Df(tailBase.x - nx * half, tailBase.y - ny * half);
        g.shaftFrom = tailBase;
    }
    return g;
}

Rect2Di DrawArrow(UCRasterLayer& layer, const UCRasterSelection* sel,
                  Point2Df tail, Point2Df tip, float headLength, float headWidth,
                  bool headAtTail, const ShapeStyle& style) {
    if (style.stroke.a == 0) return Rect2Di(0, 0, 0, 0);
    const ArrowGeometry g = ArrowShape(tail, tip, headLength, headWidth, headAtTail, style.strokeWidth);
    if (!g.valid) return Rect2Di(0, 0, 0, 0);
    const float hw = std::max(0.0f, style.strokeWidth) * 0.5f;

    // The shaft: butt-ended at a head's base - the head covers it from
    // there - and round at a tail without a head, like a line's end.
    const float sx = g.shaftTo.x - g.shaftFrom.x, sy = g.shaftTo.y - g.shaftFrom.y;
    const float shaftLen = std::sqrt(sx * sx + sy * sy);
    const float ux = shaftLen > 0.0f ? sx / shaftLen : 0.0f, uy = shaftLen > 0.0f ? sy / shaftLen : 0.0f;
    const bool roundTail = !g.hasTailHead;

    float minX = std::min({tail.x, tip.x, g.tipHead[1].x, g.tipHead[2].x});
    float maxX = std::max({tail.x, tip.x, g.tipHead[1].x, g.tipHead[2].x});
    float minY = std::min({tail.y, tip.y, g.tipHead[1].y, g.tipHead[2].y});
    float maxY = std::max({tail.y, tip.y, g.tipHead[1].y, g.tipHead[2].y});
    if (g.hasTailHead) {
        minX = std::min({minX, g.tailHead[1].x, g.tailHead[2].x});
        maxX = std::max({maxX, g.tailHead[1].x, g.tailHead[2].x});
        minY = std::min({minY, g.tailHead[1].y, g.tailHead[2].y});
        maxY = std::max({maxY, g.tailHead[1].y, g.tailHead[2].y});
    }
    const Rect2Di bbox(static_cast<int>(std::floor(minX - hw - 1)), static_cast<int>(std::floor(minY - hw - 1)),
                       static_cast<int>(std::ceil(maxX - minX + 2 * hw)) + 3,
                       static_cast<int>(std::ceil(maxY - minY + 2 * hw)) + 3);
    return FillCoverage(layer, sel, bbox,
        [&](float px, float py) {
            if (InTriangle(px, py, g.tipHead[0], g.tipHead[1], g.tipHead[2])) return true;
            if (g.hasTailHead && InTriangle(px, py, g.tailHead[0], g.tailHead[1], g.tailHead[2])) return true;
            if (hw <= 0.0f) return false;
            if (shaftLen > 0.0f) {
                const float rx = px - g.shaftFrom.x, ry = py - g.shaftFrom.y;
                const float along = rx * ux + ry * uy;
                const float across = std::fabs(rx * -uy + ry * ux);
                if (along >= 0.0f && along <= shaftLen && across <= hw) return true;
            }
            if (roundTail) {
                const float tx = px - tail.x, ty = py - tail.y;
                if (tx * tx + ty * ty <= hw * hw) return true;
            }
            return false;
        }, style.stroke, style.antialias, style.blend);
}

Rect2Di StampMask(UCRasterLayer& layer, const UCRasterSelection* sel,
                  const uint8_t* mask, int mw, int mh, int ox, int oy,
                  const RasterPixel& colour, float opacity, RasterBlendMode blend,
                  FillCompositing compositing) {
    const bool replace = compositing == FillCompositing::Replace;
    if (!mask || mw <= 0 || mh <= 0) return Rect2Di(0, 0, 0, 0);
    const Rect2Di bbox = layer.ClipRect(Rect2Di(ox, oy, mw, mh));
    if (bbox.width <= 0) return bbox;
    for (int y = bbox.y; y < bbox.y + bbox.height; ++y) {
        const uint8_t* m = mask + static_cast<size_t>(y - oy) * mw;
        uint8_t* row = layer.Row(y);
        for (int x = bbox.x; x < bbox.x + bbox.width; ++x) {
            const float cov = (m[x - ox] / 255.0f) * opacity * (SelCoverage(sel, x, y) / 255.0f);
            if (cov <= 0.0f) continue;
            uint8_t* d = row + static_cast<size_t>(x) * 4;
            const RasterPixel base(d[0], d[1], d[2], d[3]);
            const RasterPixel out = replace ? RasterReplacePixel(colour, base, cov)
                                            : RasterBlendPixel(colour, base, cov, blend);
            d[0] = out.r; d[1] = out.g; d[2] = out.b; d[3] = out.a;
        }
    }
    return bbox;
}

namespace {
    // Region of pixels matching the seed within tolerance: contiguous
    // (scanline flood) or global. Returns a W*H map (255 = in region) and
    // its bounds.
    std::vector<uint8_t> RegionMask(const UCRasterLayer& sample, int sx, int sy, int tolerance,
                                    bool contiguous, Rect2Di& bounds) {
        const int W = sample.GetWidth(), H = sample.GetHeight();
        std::vector<uint8_t> region(static_cast<size_t>(W) * H, 0);
        bounds = Rect2Di(0, 0, 0, 0);
        if (sx < 0 || sy < 0 || sx >= W || sy >= H) return region;
        const RasterPixel seed = sample.GetPixel(sx, sy);
        tolerance = std::clamp(tolerance, 0, 255);
        auto matches = [&](int x, int y) {
            const uint8_t* p = sample.Row(y) + static_cast<size_t>(x) * 4;
            return PixelDistance(RasterPixel(p[0], p[1], p[2], p[3]), seed) <= tolerance;
        };
        int minX = W, minY = H, maxX = -1, maxY = -1;
        auto mark = [&](int x, int y) {
            region[static_cast<size_t>(y) * W + x] = 255;
            minX = std::min(minX, x); maxX = std::max(maxX, x);
            minY = std::min(minY, y); maxY = std::max(maxY, y);
        };
        if (!contiguous) {
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x)
                    if (matches(x, y)) mark(x, y);
        } else {
            // Scanline fill: each stack entry is a horizontal span to expand
            // from, which keeps the stack small on large areas.
            struct Span { int x0, x1, y, dy; };
            std::vector<Span> stack;
            auto push = [&](int x0, int x1, int y, int dy) {
                if (y + dy >= 0 && y + dy < H) stack.push_back({x0, x1, y + dy, dy});
            };
            // seed span
            int l = sx, r = sx;
            while (l > 0 && matches(l - 1, sy)) --l;
            while (r < W - 1 && matches(r + 1, sy)) ++r;
            for (int x = l; x <= r; ++x) mark(x, sy);
            push(l, r, sy, 1);
            push(l, r, sy, -1);
            while (!stack.empty()) {
                Span s = stack.back(); stack.pop_back();
                int x = s.x0;
                while (x <= s.x1) {
                    if (region[static_cast<size_t>(s.y) * W + x] || !matches(x, s.y)) { ++x; continue; }
                    int a = x, b = x;
                    while (a > 0 && !region[static_cast<size_t>(s.y) * W + a - 1] && matches(a - 1, s.y)) --a;
                    while (b < W - 1 && !region[static_cast<size_t>(s.y) * W + b + 1] && matches(b + 1, s.y)) ++b;
                    for (int k = a; k <= b; ++k) mark(k, s.y);
                    push(a, b, s.y, s.dy);
                    if (a < s.x0) push(a, s.x0 - 1, s.y, -s.dy);
                    if (b > s.x1) push(s.x1 + 1, b, s.y, -s.dy);
                    x = b + 1;
                }
            }
        }
        if (maxX >= 0) bounds = Rect2Di(minX, minY, maxX - minX + 1, maxY - minY + 1);
        return region;
    }
}

Rect2Di FloodFill(UCRasterLayer& layer, const UCRasterSelection* sel, int x, int y,
                  const RasterPixel& colour, int tolerance, bool contiguous,
                  const UCRasterLayer* sampleLayer, RasterBlendMode blend, float opacity,
                  FillCompositing compositing) {
    const UCRasterLayer& sample = (sampleLayer && sampleLayer->GetWidth() == layer.GetWidth() &&
                                   sampleLayer->GetHeight() == layer.GetHeight()) ? *sampleLayer : layer;
    Rect2Di bounds;
    const std::vector<uint8_t> region = RegionMask(sample, x, y, tolerance, contiguous, bounds);
    if (bounds.width <= 0) return bounds;
    // Anti-alias the region edge a little so fills do not look jagged
    // against soft edges: a pixel adjacent to the region gets partial
    // coverage from the fraction of its 4-neighbours that are inside.
    return StampMask(layer, sel, region.data(), layer.GetWidth(), layer.GetHeight(), 0, 0,
                     colour, opacity, blend, compositing).Intersection(bounds);
}

std::vector<uint8_t> MagicWandMask(const UCRasterLayer& sample, int x, int y, int tolerance, bool contiguous) {
    Rect2Di bounds;
    return RegionMask(sample, x, y, tolerance, contiguous, bounds);
}

Rect2Di FillGradient(UCRasterLayer& layer, const UCRasterSelection* sel, Point2Df a, Point2Df b,
                     const RasterPixel& ca, const RasterPixel& cb, GradientKind kind,
                     float opacity, RasterBlendMode blend) {
    Rect2Di area = sel && sel->IsActive() ? sel->GetBounds() : layer.GetRect();
    area = layer.ClipRect(area);
    if (area.width <= 0) return area;
    const float vx = b.x - a.x, vy = b.y - a.y;
    const float len2 = std::max(1e-6f, vx * vx + vy * vy);
    const float len = std::sqrt(len2);
    for (int y = area.y; y < area.y + area.height; ++y) {
        uint8_t* row = layer.Row(y);
        for (int x = area.x; x < area.x + area.width; ++x) {
            const float sc = SelCoverage(sel, x, y) / 255.0f;
            if (sc <= 0.0f) continue;
            const float px = x + 0.5f - a.x, py = y + 0.5f - a.y;
            float t;
            switch (kind) {
                case GradientKind::Radial:    t = std::sqrt(px * px + py * py) / len; break;
                case GradientKind::Reflected: t = std::fabs((px * vx + py * vy) / len2); break;
                case GradientKind::Linear:
                default:                      t = (px * vx + py * vy) / len2; break;
            }
            t = std::clamp(t, 0.0f, 1.0f);
            // interpolate in premultiplied space so a transparent end does not
            // tint the middle
            const float aa = ca.a / 255.0f, ba = cb.a / 255.0f;
            const float na = aa + (ba - aa) * t;
            RasterPixel g;
            if (na > 0.0f) {
                g.r = ClampByte((ca.r * aa + (cb.r * ba - ca.r * aa) * t) / na);
                g.g = ClampByte((ca.g * aa + (cb.g * ba - ca.g * aa) * t) / na);
                g.b = ClampByte((ca.b * aa + (cb.b * ba - ca.b * aa) * t) / na);
            }
            g.a = ClampByte(na * 255.0f);
            uint8_t* d = row + static_cast<size_t>(x) * 4;
            const RasterPixel out = RasterBlendPixel(g, RasterPixel(d[0], d[1], d[2], d[3]), sc * opacity, blend);
            d[0] = out.r; d[1] = out.g; d[2] = out.b; d[3] = out.a;
        }
    }
    return area;
}

RasterPixel SampleColour(const UCRasterLayer& layer, int x, int y, int radius) {
    if (radius <= 0) return layer.GetPixel(x, y);
    long r = 0, g = 0, b = 0, a = 0, n = 0;
    for (int j = -radius; j <= radius; ++j) {
        for (int i = -radius; i <= radius; ++i) {
            const int px = x + i, py = y + j;
            if (px < 0 || py < 0 || px >= layer.GetWidth() || py >= layer.GetHeight()) continue;
            const RasterPixel p = layer.GetPixel(px, py);
            r += p.r; g += p.g; b += p.b; a += p.a; ++n;
        }
    }
    if (!n) return RasterPixel(0, 0, 0, 0);
    return RasterPixel(static_cast<uint8_t>(r / n), static_cast<uint8_t>(g / n),
                       static_cast<uint8_t>(b / n), static_cast<uint8_t>(a / n));
}

} // namespace RasterPaint

} // namespace UltraCanvas
