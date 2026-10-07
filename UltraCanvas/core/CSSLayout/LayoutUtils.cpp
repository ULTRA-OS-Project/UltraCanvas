// core/CSSLayout/LayoutUtils.cpp
// Shared helpers: dimension resolution, edge resolution, constraint clamping.
// Version: 1.3.0 - hasSetSize, auditKeptSize
// Version: 1.2.0 - resolveDimension adds a Dimension's offsetPx
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework

#include "CSSLayout/LayoutUtils.h"
#include "UltraCanvasPathUtf8.h"   // GetEnvUtf8
#include <algorithm>
#include <cstdio>
#include <mutex>
#include <set>
#include <string>
#include <utility>

namespace UltraCanvas {
    namespace CSSLayout {

        bool hasSetSize(const Dimension& dim) {
            if (dim.isAuto()) return false;
            if (dim.unit == DimensionUnit::Pixels && dim.value + dim.offsetPx < 0.f) return false;
            return true;
        }

        void auditKeptSize(const Element& element, const char* axis, float setSize,
                           float stretchSize) {
            static const bool enabled = !GetEnvUtf8("ULTRACANVAS_LAYOUT_AUDIT").empty();
            if (!enabled) return;
            static std::mutex mutex;
            // Keyed by address and id, so an element at a reused address is told too.
            static std::set<std::pair<const Element*, std::string>> told;
            std::lock_guard<std::mutex> lock(mutex);
            if (!told.insert({&element, element.id + "/" + axis}).second) return;
            std::fprintf(stderr,
                         "ULTRACANVAS_LAYOUT_AUDIT: '%s' keeps its set %s of %.0f px; its container "
                         "would have stretched it to %.0f px\n",
                         element.id.c_str(), axis, setSize, stretchSize);
        }

        std::optional<float> resolveDimension(const Dimension& dim,
                                              std::optional<float> parentExtent,
                                              const LayoutContext& ctx) {
            switch (dim.unit) {
                case DimensionUnit::Pixels:
                    return dim.value + dim.offsetPx;
                case DimensionUnit::Percent:
                    if (parentExtent.has_value())
                        return (*parentExtent) * dim.value / 100.f + dim.offsetPx;
                    return std::nullopt;
                case DimensionUnit::ViewportWidth:
                    return ctx.viewportWidth  * dim.value / 100.f + dim.offsetPx;
                case DimensionUnit::ViewportHeight:
                    return ctx.viewportHeight * dim.value / 100.f + dim.offsetPx;
                case DimensionUnit::Em:
                    return ctx.fontSizePx     * dim.value + dim.offsetPx;
                case DimensionUnit::Rem:
                    return ctx.rootFontSizePx * dim.value + dim.offsetPx;
                case DimensionUnit::Auto:
                case DimensionUnit::Fr:
                case DimensionUnit::MinContent:
                case DimensionUnit::MaxContent:
                case DimensionUnit::FitContent:
                    return std::nullopt;
            }
            return std::nullopt;
        }

        InsetsPx resolveEdgeSizes(const EdgeSizes& edges,
                                  float parentInlineSize,
                                  const LayoutContext& ctx) {
            // All four sides resolve % against the parent's *inline* size (CSS 2.1 §8.3).
            auto one = [&](const Dimension& d) -> float {
                auto r = resolveDimension(d, parentInlineSize, ctx);
                return r.value_or(0.f);
            };
            return InsetsPx{ one(edges.top), one(edges.right), one(edges.bottom), one(edges.left) };
        }

        float clampToConstraints(float value,
                                 const std::optional<BoxConstraints>& bc,
                                 bool isWidth,
                                 std::optional<float> parentExtent,
                                 const LayoutContext& ctx) {
            if (!bc.has_value()) return value;
            const auto& minD = isWidth ? bc->minWidth  : bc->minHeight;
            const auto& maxD = isWidth ? bc->maxWidth  : bc->maxHeight;
            auto minR = resolveDimension(minD, parentExtent, ctx);
            auto maxR = resolveDimension(maxD, parentExtent, ctx);
            if (maxR.has_value()) value = std::min(value, *maxR);
            if (minR.has_value()) value = std::max(value, *minR);
            return value;
        }

        float resolveBorderBoxSize(float specifiedSize,
                                   BoxSizing sizing,
                                   float paddingSum,
                                   float borderSum) {
            if (sizing == BoxSizing::BorderBox) return specifiedSize;
            return specifiedSize + paddingSum + borderSum;
        }

        float borderBoxToContent(float borderBoxSize,
                                 float paddingSum,
                                 float borderSum) {
            return std::max(0.f, borderBoxSize - paddingSum - borderSum);
        }

    }
}
