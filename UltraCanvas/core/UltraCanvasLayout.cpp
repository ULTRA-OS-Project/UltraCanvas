// core/UltraCanvasLayout.cpp
// Implementation of base layout class
// Version: 1.0.0
// Last Modified: 2025-11-02
// Author: UltraCanvas Framework

#include "UltraCanvasLayout.h"
#include "UltraCanvasContainer.h"

namespace UltraCanvas {

    UltraCanvasLayout::UltraCanvasLayout(UltraCanvasContainer* parent)
        : parentContainer(parent) {
    }

    void UltraCanvasLayout::Invalidate() {
        layoutDirty = true;
        if (parentContainer) {
            parentContainer->RequestRedraw();
        }
    }
} // namespace UltraCanvas
