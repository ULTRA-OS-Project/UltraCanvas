// include/UltraCanvasLayoutEngine.h
// Layout engine types used by the template system
// Version: 1.0.0
#pragma once

#include "UltraCanvasCommonTypes.h"

namespace UltraCanvas {

enum class LayoutDirection {
    Horizontal,
    Vertical
};

enum class LayoutDockSide {
    Left,
    Right,
    Top,
    Bottom,
    Fill
};

struct LayoutConstraints {
    float minWidth = 0.0f;
    float minHeight = 0.0f;
    float maxWidth = 0.0f;
    float maxHeight = 0.0f;
    float preferredWidth = 0.0f;
    float preferredHeight = 0.0f;
};

} // namespace UltraCanvas
