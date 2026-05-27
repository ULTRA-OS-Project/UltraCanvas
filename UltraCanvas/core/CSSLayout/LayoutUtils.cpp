// core/CSSLayout/LayoutUtils.cpp
// Shared helpers: dimension resolution, edge resolution, constraint clamping.
// Version: 1.0.0
// Last Modified: 2026-05-27
// Author: UltraCanvas Framework

#include "CSSLayout/LayoutUtils.h"
#include <algorithm>

namespace UltraCanvas {
    namespace CSSLayout {

        std::optional<float> resolveDimension(const Dimension& dim,
                                              std::optional<float> parentExtent,
                                              const LayoutContext& /*ctx*/) {
            switch (dim.unit) {
                case DimensionUnit::Pixels:
                    return dim.value;
                case DimensionUnit::Percent:
                    if (parentExtent.has_value()) return (*parentExtent) * dim.value / 100.f;
                    return std::nullopt;
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
