// core/CSSLayout/LayoutAlgorithms.h
// Internal forward declarations for per-display-type algorithms.
// (Header lives under core/ so it is private to the implementation.)
// Version: 1.1.0
// Last Modified: 2026-05-27
// Author: UltraCanvas Framework
#pragma once

#include "CSSLayout/CSSLayout.h"
#include <utility>

namespace UltraCanvas {
    namespace CSSLayout {

        // Default block layout (display: block / inline-block). Implemented in Element.cpp.
        void MeasureBlock (Element& e, const MeasureConstraints& c, const LayoutContext& ctx);
        void ArrangeBlock (Element& e, const Rect2Df& finalRect,  const LayoutContext& ctx);

        // Flex layout (display: flex). Implemented in FlexLayout.cpp.
        void MeasureFlex  (Element& e, const MeasureConstraints& c, const LayoutContext& ctx);
        void ArrangeFlex  (Element& e, const Rect2Df& finalRect,  const LayoutContext& ctx);

        // Grid layout (display: grid). Implemented in GridLayout.cpp.
        void MeasureGrid  (Element& e, const MeasureConstraints& c, const LayoutContext& ctx);
        void ArrangeGrid  (Element& e, const Rect2Df& finalRect,  const LayoutContext& ctx);

        // Absolute/Relative/Fixed positioning. Implemented in AbsoluteLayout.cpp.
        // Lays out a single positioned child against its containing-block rect (border-box of the CB).
        void ArrangePositionedChild(Element& child,
                                    const Rect2Df& containingBlock,
                                    const LayoutContext& ctx);

        // Returns the (dx, dy) shift implied by position:relative top/left/right/bottom
        // for an in-flow child. cbInline / cbBlock are the containing-block content
        // extents used to resolve % insets. The caller folds (dx, dy) into the rect
        // passed to child.Arrange(...) — relative does NOT affect siblings.
        std::pair<float, float> computeRelativeOffset(const Element& child,
                                                      float cbInline,
                                                      float cbBlock,
                                                      const LayoutContext& ctx);

    }
}
