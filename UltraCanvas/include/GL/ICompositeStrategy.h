// GL/ICompositeStrategy.h
// Abstract interface for compositing GL framebuffer content to UltraCanvas
// Allows different strategies: CPU readback (initial), GPU-native (future)
#pragma once

#ifndef ULTRACANVAS_ENABLE_GL
#error "GL/ICompositeStrategy.h requires ULTRACANVAS_ENABLE_GL to be defined"
#endif

#include <memory>

namespace UltraCanvas {

// Forward declarations
class GLFramebuffer;
class IRenderContext;
struct PixelFormat;

// Abstract interface for compositing GL content to the Cairo surface
class ICompositeStrategy {
public:
    virtual ~ICompositeStrategy() = default;

    // Composite the GL framebuffer content to the render context
    // destX, destY: Position in the render context to draw
    // destWidth, destHeight: Size to draw (may differ from FBO size for scaling)
    // readback: when false, skip glReadPixels and redraw the cached pixels
    virtual bool Composite(GLFramebuffer& framebuffer,
                          IRenderContext* ctx,
                          int destX, int destY,
                          int destWidth, int destHeight,
                          bool readback = true) = 0;

    // Get the name of this strategy (for debugging)
    virtual const char* GetName() const = 0;

    // Check if this strategy is available on the current system
    virtual bool IsAvailable() const = 0;

protected:
    ICompositeStrategy() = default;

    // Non-copyable
    ICompositeStrategy(const ICompositeStrategy&) = delete;
    ICompositeStrategy& operator=(const ICompositeStrategy&) = delete;
};

// CPU-based compositing using glReadPixels
// This is the initial implementation that works everywhere
class CompositeStrategyCPU : public ICompositeStrategy {
public:
    CompositeStrategyCPU();
    ~CompositeStrategyCPU() override;

    bool Composite(GLFramebuffer& framebuffer,
                  IRenderContext* ctx,
                  int destX, int destY,
                  int destWidth, int destHeight,
                  bool readback = true) override;

    const char* GetName() const override { return "CPU (glReadPixels)"; }
    bool IsAvailable() const override { return true; }

private:
    // Internal pixel buffer for readback (reused across frames)
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Factory function to create the best available composite strategy
std::unique_ptr<ICompositeStrategy> CreateCompositeStrategy();

// Future: GPU-native compositing via EGL Image / DMA-BUF
// class CompositeStrategyEGLImage : public ICompositeStrategy { ... };

} // namespace UltraCanvas
