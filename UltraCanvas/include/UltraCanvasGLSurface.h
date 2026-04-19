// UltraCanvasGLSurface.h
// OpenGL 3D rendering surface for UltraCanvas
// Provides an EGL/OpenGL rendering context within a UltraCanvasUIElement
#pragma once

#ifndef ULTRACANVAS_ENABLE_GL
#error "UltraCanvasGLSurface.h requires ULTRACANVAS_ENABLE_GL to be defined. Build with -DULTRACANVAS_ENABLE_GL=ON"
#endif

#include "UltraCanvasUIElement.h"
#include "RenderSurfaceTypes.h"
#include <functional>
#include <memory>
#include <chrono>

namespace UltraCanvas {

// Forward declarations
class GLContextManager;
class GLFramebuffer;
class ICompositeStrategy;

// OpenGL surface configuration
struct GLSurfaceConfig {
    int glVersionMajor = 3;         // OpenGL major version (default 3.3 Core)
    int glVersionMinor = 3;         // OpenGL minor version
    bool coreProfile = true;        // Use Core profile (vs Compatibility)
    int depthBits = 24;             // Depth buffer bits (0 to disable)
    int stencilBits = 8;            // Stencil buffer bits (0 to disable)
    int samples = 0;                // MSAA samples (0 = disabled, 2/4/8/16)
    int redBits = 8;                // Red channel bits
    int greenBits = 8;              // Green channel bits
    int blueBits = 8;               // Blue channel bits
    int alphaBits = 8;              // Alpha channel bits
    bool sRGB = false;              // Request sRGB framebuffer
    bool debugContext = false;      // Enable GL debug output

    GLSurfaceConfig() = default;

    // Convenience constructor for common configurations
    static GLSurfaceConfig Default() { return GLSurfaceConfig(); }

    static GLSurfaceConfig WithMSAA(int sampleCount) {
        GLSurfaceConfig config;
        config.samples = sampleCount;
        return config;
    }

    static GLSurfaceConfig NoDepthStencil() {
        GLSurfaceConfig config;
        config.depthBits = 0;
        config.stencilBits = 0;
        return config;
    }
};

// OpenGL surface UI element
class UltraCanvasGLSurface : public UltraCanvasUIElement {
public:
    // Callback types
    using RenderCallback = std::function<void(const RenderSurfaceInfo&)>;
    using InitCallback = std::function<void()>;
    using ResizeCallback = std::function<void(int width, int height)>;
    using CleanupCallback = std::function<void()>;

private:
    // Configuration
    GLSurfaceConfig config_;
    RenderMode renderMode_ = RenderMode::OnDemand;
    float updateInterval_ = 1.0f / 60.0f;  // For TimedUpdate mode (seconds)

    // GL resources
    std::unique_ptr<GLContextManager> contextManager_;
    std::unique_ptr<GLFramebuffer> framebuffer_;
    std::unique_ptr<ICompositeStrategy> compositeStrategy_;

    // State
    bool initialized_ = false;
    bool contextValid_ = false;
    bool renderRequested_ = false;
    bool needsResize_ = false;
    int surfaceWidth_ = 0;
    int surfaceHeight_ = 0;

    // Timing
    uint64_t frameNumber_ = 0;
    std::chrono::steady_clock::time_point lastRenderTime_;
    std::chrono::steady_clock::time_point lastUpdateTime_;

    // User callbacks
    RenderCallback renderCallback_;
    InitCallback initCallback_;
    ResizeCallback resizeCallback_;
    CleanupCallback cleanupCallback_;

public:
    // Constructors
    UltraCanvasGLSurface(const std::string& identifier = "GLSurface",
                         int x = 0, int y = 0, int width = 300, int height = 300);

    UltraCanvasGLSurface(const GLSurfaceConfig& config,
                         const std::string& identifier = "GLSurface",
                         int x = 0, int y = 0, int width = 300, int height = 300);

    ~UltraCanvasGLSurface() override;

    // Non-copyable, movable
    UltraCanvasGLSurface(const UltraCanvasGLSurface&) = delete;
    UltraCanvasGLSurface& operator=(const UltraCanvasGLSurface&) = delete;
    UltraCanvasGLSurface(UltraCanvasGLSurface&&) noexcept;
    UltraCanvasGLSurface& operator=(UltraCanvasGLSurface&&) noexcept;

    // Configuration (must be set before first render)
    void SetConfig(const GLSurfaceConfig& config);
    const GLSurfaceConfig& GetConfig() const { return config_; }

    // Render mode
    void SetRenderMode(RenderMode mode, float interval = 1.0f / 60.0f);
    RenderMode GetRenderMode() const { return renderMode_; }
    float GetUpdateInterval() const { return updateInterval_; }

    // Callbacks
    void SetRenderCallback(RenderCallback callback) { renderCallback_ = std::move(callback); }
    void SetInitCallback(InitCallback callback) { initCallback_ = std::move(callback); }
    void SetResizeCallback(ResizeCallback callback) { resizeCallback_ = std::move(callback); }
    void SetCleanupCallback(CleanupCallback callback) { cleanupCallback_ = std::move(callback); }

    // Rendering control
    void RequestRender();       // Request a render (for OnDemand mode)
    bool IsInitialized() const { return initialized_; }
    bool IsContextValid() const { return contextValid_; }

    // GL context access (for advanced use)
    bool MakeCurrent();
    void ReleaseCurrent();
    void* GetNativeGLContext() const;

    // Surface dimensions (may differ from element bounds due to DPI scaling)
    int GetSurfaceWidth() const { return surfaceWidth_; }
    int GetSurfaceHeight() const { return surfaceHeight_; }

    // UltraCanvasUIElement overrides
    void Render(IRenderContext* ctx) override;
    bool OnEvent(const UCEvent& event) override;
    void SetBounds(const Rect2Di& bounds) override;

protected:
    // Virtual methods for subclassing (alternative to callbacks)
    virtual void OnGLInit();
    virtual void OnGLRender(const RenderSurfaceInfo& info);
    virtual void OnGLResize(int width, int height);
    virtual void OnGLCleanup();

private:
    // Internal methods
    bool InitializeGL();
    void CleanupGL();
    bool EnsureFramebuffer();
    void RenderToFramebuffer();
    void CompositeToSurface(IRenderContext* ctx, bool readback = true);
    bool ShouldRender() const;
    RenderSurfaceInfo BuildRenderInfo();
};

// Factory functions
std::shared_ptr<UltraCanvasGLSurface> CreateGLSurface(
    const std::string& identifier,
    int x, int y, int width, int height,
    const GLSurfaceConfig& config = GLSurfaceConfig());

std::shared_ptr<UltraCanvasGLSurface> CreateGLSurface(
    int x, int y, int width, int height,
    const GLSurfaceConfig& config = GLSurfaceConfig());

} // namespace UltraCanvas
