// include/UltraCanvasSpacer.h
// Invisible widget that takes layout space. Used to replace the old
// `AddSpacing(n)` / `AddStretch(n)` phantom items with real children.
// Version: 1.0.0
// Last Modified: 2026-05-27
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"

namespace UltraCanvas {

    class UltraCanvasSpacer : public UltraCanvasUIElement {
    public:
        // Fixed-size spacer (AddSpacing equivalent).
        // For a row layout, set w > 0 and h = 0 — the spacer takes w px
        // along the main axis and 0 along the cross axis. The cross-axis
        // size resolves from align-items/align-self (Stretch by default).
        UltraCanvasSpacer(float w = 0, float h = 0, float grow = 0)
                : UltraCanvasUIElement("spacer", 0, 0, w, h) {
            CSSLayout::FlexItem fi;
            fi.grow = grow;
            layoutItem.data = fi;
        }

        // Invisible: no border, no fill, nothing to paint.
        void Render(IRenderContext* /*ctx*/, const Rect2Df& /*dirtyRect*/) override {}

        // Spacers never accept events.
        bool Contains(const Point2Df& /*point*/) override { return false; }
    };

} // namespace UltraCanvas
