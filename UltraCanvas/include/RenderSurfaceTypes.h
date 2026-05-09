// RenderSurfaceTypes.h
// Generic types for 3D rendering surfaces (API-agnostic)
// These types are shared across different 3D backends (OpenGL, Vulkan, Metal, etc.)
#pragma once

#include <cstdint>

namespace UltraCanvas {

// Render mode for 3D surfaces
enum class RenderMode {
    OnDemand,     // Render only when RequestRender() is called (default, lower CPU/GPU usage)
    Continuous,   // Render every frame (for animations)
    TimedUpdate   // Render at fixed interval
};

// Information passed to render callbacks (API-agnostic)
struct RenderSurfaceInfo {
    int width;                  // Surface width in pixels
    int height;                 // Surface height in pixels
    float devicePixelRatio;     // For HiDPI/Retina displays (1.0 = standard)
    double deltaTime;           // Time since last frame in seconds
    uint64_t frameNumber;       // Monotonically increasing frame counter

    RenderSurfaceInfo()
        : width(0)
        , height(0)
        , devicePixelRatio(1.0f)
        , deltaTime(0.0)
        , frameNumber(0) {}

    RenderSurfaceInfo(int w, int h, float dpr = 1.0f)
        : width(w)
        , height(h)
        , devicePixelRatio(dpr)
        , deltaTime(0.0)
        , frameNumber(0) {}
};

} // namespace UltraCanvas
