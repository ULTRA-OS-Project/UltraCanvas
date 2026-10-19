// include/CSSLayout/LayoutUtils.h
// Shared helpers: dimension resolution, edge resolution, constraint clamping.
// Version: 1.1.0 - hasSetSize, auditKeptSize (stretch only over an automatic size)
// Version: 1.0.0
// Last Modified: 2026-10-06
// Author: UltraCanvas Framework
#pragma once

#include "CSSLayout/CSSLayout.h"
#include <optional>

namespace UltraCanvas {
    namespace CSSLayout {

        // Resolve a single Dimension to pixels.
        //   parentExtent: the basis a percentage resolves against (parent inline size
        //                 for widths/horizontal padding/margin; parent block size for heights).
        //                 nullopt means the basis is indefinite — percentages return nullopt.
        // Returns nullopt for Auto / Fr / *Content / FitContent (caller must handle).
        std::optional<float> resolveDimension(const Dimension& dim,
                                              std::optional<float> parentExtent,
                                              const LayoutContext& ctx);

        // Resolve all four edges (margin/padding/border). Percentages on ALL four sides
        // resolve against parentInlineSize per CSS 2.1 §8.3.
        // Auto edges become 0 px (margin: auto centering is handled by the caller).
        InsetsPx resolveEdgeSizes(const EdgeSizes& edges,
                                  float parentInlineSize,
                                  const LayoutContext& ctx);

        // Clamp a value against optional min/max BoxConstraints on one axis.
        //   isWidth: true → use minWidth/maxWidth, parentExtent = parent inline size.
        //            false → use minHeight/maxHeight, parentExtent = parent block size.
        float clampToConstraints(float value,
                                 const std::optional<BoxConstraints>& bc,
                                 bool isWidth,
                                 std::optional<float> parentExtent,
                                 const LayoutContext& ctx);

        // A width or height the element was given, which stretch must not
        // override: anything other than auto or a negative length. CSS rejects
        // a negative width or height as invalid, so it computes to auto; here
        // it is the "not set" -1, or a size computed before the window had one.
        bool hasSetSize(const Dimension& dim);

        // ULTRACANVAS_LAYOUT_AUDIT=1 in the environment: says once per
        // element and axis, on stderr, that its container asked for stretch
        // but the element keeps a size of its own - the way to find a size
        // that was only ever given because a constructor wanted one.
        void auditKeptSize(const Element& element, const char* axis, float setSize,
                           float stretchSize);

        // Convert specified width/height (content-box semantics) into the actual
        // outer extent when BoxSizing == BorderBox vs. ContentBox.
        // contentSize is the resolved value of `width`/`height`.
        // Returns the border-box size of the element.
        float resolveBorderBoxSize(float specifiedSize,
                                   BoxSizing sizing,
                                   float paddingSum,
                                   float borderSum);

        // Inverse: given a border-box extent, return the content-box extent.
        float borderBoxToContent(float borderBoxSize,
                                 float paddingSum,
                                 float borderSum);

    }
}
