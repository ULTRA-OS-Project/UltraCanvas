// UltraCanvasElementDebug.cpp
// Debug rendering implementation for UltraCanvas elements
// Version: 1.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework

#include "UltraCanvasElementDebug.h"
#include "UltraCanvasRenderInterface.h"
#include <sstream>
#include <algorithm>

namespace UltraCanvas {

// ===== STATIC MEMBER INITIALIZATION =====
    DebugRenderSettings UltraCanvasDebugRenderer::globalSettings;
    bool UltraCanvasDebugRenderer::debugEnabled = false;
}