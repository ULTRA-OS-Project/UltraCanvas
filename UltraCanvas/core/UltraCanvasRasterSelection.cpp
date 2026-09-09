// core/UltraCanvasRasterSelection.cpp
// Soft 8-bit selection mask: shapes, set algebra, feather / grow / shrink
// and the marching-ants outline.
// Version: 1.0.0
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework

#include "UltraCanvasRasterSelection.h"

#include <algorithm>
#include <cmath>

namespace UltraCanvas {

UCRasterSelection::UCRasterSelection(int w, int h) {
    Resize(w, h);
}

void UCRasterSelection::Resize(int w, int h) {
    width = std::max(0, w);
    height = std::max(0, h);
    mask.assign(static_cast<size_t>(width) * height, 0);
    active = false;
    Touch();
}

void UCRasterSelection::Touch() {
    ++version;
}

void UCRasterSelection::UpdateActive() {
    active = std::any_of(mask.begin(), mask.end(), [](uint8_t v) { return v != 0; });
}

Rect2Di UCRasterSelection::GetBounds() const {
    if (!active) return Rect2Di(0, 0, width, height);
    int minX = width, minY = height, maxX = -1, maxY = -1;
    for (int y = 0; y < height; ++y) {
        const uint8_t* row = mask.data() + static_cast<size_t>(y) * width;
        for (int x = 0; x < width; ++x) {
            if (row[x]) {
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }
    if (maxX < 0) return Rect2Di(0, 0, 0, 0);
    return Rect2Di(minX, minY, maxX - minX + 1, maxY - minY + 1);
}

void UCRasterSelection::SelectAll() {
    std::fill(mask.begin(), mask.end(), 255);
    active = !mask.empty();
    Touch();
}

void UCRasterSelection::SelectNone() {
    std::fill(mask.begin(), mask.end(), 0);
    active = false;
    Touch();
}

void UCRasterSelection::Invert() {
    if (!active) { SelectAll(); return; }
    for (auto& v : mask) v = static_cast<uint8_t>(255 - v);
    UpdateActive();
    Touch();
}

void UCRasterSelection::Combine(const std::vector<uint8_t>& shape, RasterSelectionMode mode) {
    if (shape.size() != mask.size()) return;
    switch (mode) {
        case RasterSelectionMode::Replace:
            mask = shape;
            break;
        case RasterSelectionMode::Add:
            for (size_t i = 0; i < mask.size(); ++i) mask[i] = std::max(mask[i], shape[i]);
            break;
        case RasterSelectionMode::Subtract:
            for (size_t i = 0; i < mask.size(); ++i)
                mask[i] = static_cast<uint8_t>(std::max(0, static_cast<int>(mask[i]) - shape[i]));
            break;
        case RasterSelectionMode::Intersect:
            if (!active) { mask = shape; break; }   // nothing selected == everything
            for (size_t i = 0; i < mask.size(); ++i) mask[i] = std::min(mask[i], shape[i]);
            break;
    }
    UpdateActive();
    Touch();
}

void UCRasterSelection::SetRectangle(const Rect2Di& r, RasterSelectionMode mode) {
    std::vector<uint8_t> shape(mask.size(), 0);
    const int x0 = std::max(0, r.x), y0 = std::max(0, r.y);
    const int x1 = std::min(width, r.x + r.width), y1 = std::min(height, r.y + r.height);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) shape[static_cast<size_t>(y) * width + x] = 255;
    Combine(shape, mode);
}

void UCRasterSelection::SetEllipse(const Rect2Di& b, RasterSelectionMode mode, bool antialias) {
    std::vector<uint8_t> shape(mask.size(), 0);
    if (b.width > 0 && b.height > 0) {
        const double cx = b.x + b.width * 0.5, cy = b.y + b.height * 0.5;
        const double rx = b.width * 0.5, ry = b.height * 0.5;
        const int x0 = std::max(0, b.x), y0 = std::max(0, b.y);
        const int x1 = std::min(width, b.x + b.width), y1 = std::min(height, b.y + b.height);
        const int ss = antialias ? 4 : 1;
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                int hits = 0;
                for (int sy = 0; sy < ss; ++sy) {
                    for (int sx = 0; sx < ss; ++sx) {
                        const double px = x + (sx + 0.5) / ss, py = y + (sy + 0.5) / ss;
                        const double dx = (px - cx) / rx, dy = (py - cy) / ry;
                        if (dx * dx + dy * dy <= 1.0) ++hits;
                    }
                }
                shape[static_cast<size_t>(y) * width + x] = static_cast<uint8_t>(hits * 255 / (ss * ss));
            }
        }
    }
    Combine(shape, mode);
}

void UCRasterSelection::SetPolygon(const std::vector<Point2Df>& pts, RasterSelectionMode mode, bool antialias) {
    std::vector<uint8_t> shape(mask.size(), 0);
    if (pts.size() >= 3) {
        float minX = pts[0].x, maxX = pts[0].x, minY = pts[0].y, maxY = pts[0].y;
        for (const auto& p : pts) {
            minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
        }
        const int x0 = std::max(0, static_cast<int>(std::floor(minX)));
        const int y0 = std::max(0, static_cast<int>(std::floor(minY)));
        const int x1 = std::min(width, static_cast<int>(std::ceil(maxX)) + 1);
        const int y1 = std::min(height, static_cast<int>(std::ceil(maxY)) + 1);
        const int ss = antialias ? 4 : 1;
        const size_t n = pts.size();
        // Scanline even-odd test per sub-sample row: collect crossings once
        // per sub-row, then fill runs — far cheaper than a point-in-polygon
        // test per sample.
        std::vector<float> xs;
        std::vector<int> rowHits(static_cast<size_t>(std::max(0, x1 - x0)));
        for (int y = y0; y < y1; ++y) {
            std::fill(rowHits.begin(), rowHits.end(), 0);
            for (int sy = 0; sy < ss; ++sy) {
                const float py = y + (sy + 0.5f) / ss;
                xs.clear();
                for (size_t i = 0, j = n - 1; i < n; j = i++) {
                    const Point2Df& a = pts[i];
                    const Point2Df& b = pts[j];
                    if ((a.y > py) != (b.y > py)) {
                        xs.push_back(a.x + (py - a.y) * (b.x - a.x) / (b.y - a.y));
                    }
                }
                std::sort(xs.begin(), xs.end());
                for (size_t k = 0; k + 1 < xs.size(); k += 2) {
                    for (int sx = 0; sx < ss; ++sx) {
                        // sample x positions between xs[k] and xs[k+1]
                        const float from = xs[k], to = xs[k + 1];
                        int xa = std::max(x0, static_cast<int>(std::floor(from)));
                        int xb = std::min(x1 - 1, static_cast<int>(std::ceil(to)));
                        for (int x = xa; x <= xb; ++x) {
                            const float px = x + (sx + 0.5f) / ss;
                            if (px >= from && px < to) ++rowHits[static_cast<size_t>(x - x0)];
                        }
                    }
                }
            }
            for (int x = x0; x < x1; ++x) {
                const int hits = std::min(rowHits[static_cast<size_t>(x - x0)], ss * ss);
                shape[static_cast<size_t>(y) * width + x] = static_cast<uint8_t>(hits * 255 / (ss * ss));
            }
        }
    }
    Combine(shape, mode);
}

void UCRasterSelection::SetMask(const std::vector<uint8_t>& coverage, RasterSelectionMode mode) {
    Combine(coverage, mode);
}

void UCRasterSelection::Assign(const UCRasterSelection& other) {
    if (other.width != width || other.height != height) Resize(other.width, other.height);
    mask = other.mask;
    active = other.active;
    Touch();
}

void UCRasterSelection::Feather(int radius) {
    if (!active || radius <= 0) return;
    // Separable box blur, run twice for a rounder falloff.
    std::vector<uint8_t> tmp(mask.size());
    for (int pass = 0; pass < 2; ++pass) {
        // horizontal
        for (int y = 0; y < height; ++y) {
            const uint8_t* row = mask.data() + static_cast<size_t>(y) * width;
            uint8_t* out = tmp.data() + static_cast<size_t>(y) * width;
            int sum = 0, count = 0;
            for (int x = -radius; x < width; ++x) {
                const int xin = x + radius;
                if (xin < width) { sum += row[xin]; ++count; }
                const int xout = x - radius - 1;
                if (xout >= 0) { sum -= row[xout]; --count; }
                if (x >= 0) out[x] = static_cast<uint8_t>(count > 0 ? sum / count : 0);
            }
        }
        // vertical
        for (int x = 0; x < width; ++x) {
            int sum = 0, count = 0;
            for (int y = -radius; y < height; ++y) {
                const int yin = y + radius;
                if (yin < height) { sum += tmp[static_cast<size_t>(yin) * width + x]; ++count; }
                const int yout = y - radius - 1;
                if (yout >= 0) { sum -= tmp[static_cast<size_t>(yout) * width + x]; --count; }
                if (y >= 0) mask[static_cast<size_t>(y) * width + x] = static_cast<uint8_t>(count > 0 ? sum / count : 0);
            }
        }
    }
    UpdateActive();
    Touch();
}

void UCRasterSelection::Grow(int pixels) {
    if (!active || pixels <= 0) return;
    for (int i = 0; i < pixels; ++i) {
        std::vector<uint8_t> next = mask;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                uint8_t m = mask[static_cast<size_t>(y) * width + x];
                if (x > 0)          m = std::max(m, mask[static_cast<size_t>(y) * width + x - 1]);
                if (x < width - 1)  m = std::max(m, mask[static_cast<size_t>(y) * width + x + 1]);
                if (y > 0)          m = std::max(m, mask[static_cast<size_t>(y - 1) * width + x]);
                if (y < height - 1) m = std::max(m, mask[static_cast<size_t>(y + 1) * width + x]);
                next[static_cast<size_t>(y) * width + x] = m;
            }
        }
        mask.swap(next);
    }
    Touch();
}

void UCRasterSelection::Shrink(int pixels) {
    if (!active || pixels <= 0) return;
    for (int i = 0; i < pixels; ++i) {
        std::vector<uint8_t> next = mask;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                uint8_t m = mask[static_cast<size_t>(y) * width + x];
                m = std::min(m, static_cast<uint8_t>(x > 0          ? mask[static_cast<size_t>(y) * width + x - 1] : 0));
                m = std::min(m, static_cast<uint8_t>(x < width - 1  ? mask[static_cast<size_t>(y) * width + x + 1] : 0));
                m = std::min(m, static_cast<uint8_t>(y > 0          ? mask[static_cast<size_t>(y - 1) * width + x] : 0));
                m = std::min(m, static_cast<uint8_t>(y < height - 1 ? mask[static_cast<size_t>(y + 1) * width + x] : 0));
                next[static_cast<size_t>(y) * width + x] = m;
            }
        }
        mask.swap(next);
    }
    UpdateActive();
    Touch();
}

void UCRasterSelection::Translate(int dx, int dy) {
    if (!active || (dx == 0 && dy == 0)) return;
    std::vector<uint8_t> next(mask.size(), 0);
    for (int y = 0; y < height; ++y) {
        const int ty = y + dy;
        if (ty < 0 || ty >= height) continue;
        for (int x = 0; x < width; ++x) {
            const int tx = x + dx;
            if (tx < 0 || tx >= width) continue;
            next[static_cast<size_t>(ty) * width + tx] = mask[static_cast<size_t>(y) * width + x];
        }
    }
    mask.swap(next);
    UpdateActive();
    Touch();
}

const std::vector<UCRasterSelection::OutlineSegment>& UCRasterSelection::GetOutline() const {
    if (outlineVersion == version) return outline;
    outlineVersion = version;
    outline.clear();
    if (!active) return outline;
    auto sel = [&](int x, int y) -> bool {
        if (x < 0 || y < 0 || x >= width || y >= height) return false;
        return mask[static_cast<size_t>(y) * width + x] >= 128;
    };
    // Horizontal edges: between row y-1 and y, merged into runs.
    for (int y = 0; y <= height; ++y) {
        int runStart = -1;
        for (int x = 0; x <= width; ++x) {
            const bool edge = (x < width) && (sel(x, y) != sel(x, y - 1));
            if (edge && runStart < 0) runStart = x;
            if (!edge && runStart >= 0) { outline.push_back({runStart, y, x, y}); runStart = -1; }
        }
    }
    // Vertical edges: between column x-1 and x.
    for (int x = 0; x <= width; ++x) {
        int runStart = -1;
        for (int y = 0; y <= height; ++y) {
            const bool edge = (y < height) && (sel(x, y) != sel(x - 1, y));
            if (edge && runStart < 0) runStart = y;
            if (!edge && runStart >= 0) { outline.push_back({x, runStart, x, y}); runStart = -1; }
        }
    }
    return outline;
}

} // namespace UltraCanvas
