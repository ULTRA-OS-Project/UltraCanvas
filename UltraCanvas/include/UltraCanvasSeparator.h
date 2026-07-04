// include/UltraCanvasSeparator.h
// Thin colored divider widget for toolbars and containers. A real child
// element (replaces the old UltraCanvasToolbarSeparator wrapper) so it
// participates in CSS flex layout like any other widget.
// Version: 1.0.0
// Last Modified: 2026-06-01
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"

namespace UltraCanvas {

    class UltraCanvasSeparator : public UltraCanvasUIElement {
        bool vertical;     // true: a vertical line (used in horizontal toolbars)
        int  thickness;    // line thickness in px
        int  length;       // line length along the cross axis in px
        Color color;
    public:
        // For a horizontal toolbar the separator is a vertical line: it takes
        // `thickness` px along the main axis and `length` px along the cross axis.
        // Like UltraCanvasSpacer this has no identity/position of its own — only
        // its size — so the identifier is fixed to "separator".
        UltraCanvasSeparator(bool isVertical, int thick = 1, int len = 24,
                             Color c = Color(200, 200, 200, 255))
                : UltraCanvasUIElement("separator", 0, 0,
                      isVertical ? thick : len, isVertical ? len : thick),
                  vertical(isVertical), thickness(thick), length(len), color(c) {
            SetBackgroundColor(c);
        }

        void SetColor(const Color& c) { color = c; SetBackgroundColor(c); }

        // Separators never accept events.
        bool Contains(const Point2Df& /*point*/) override { return false; }
    };

} // namespace UltraCanvas
