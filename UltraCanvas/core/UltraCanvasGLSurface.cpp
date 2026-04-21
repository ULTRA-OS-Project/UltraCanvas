// UltraCanvasGLSurface.cpp
// OpenGL 3D rendering surface implementation
#include "UltraCanvasGLSurface.h"
#include "GL/GLContextManager.h"
#include "GL/GLFramebuffer.h"
#include "GL/ICompositeStrategy.h"
#include "UltraCanvasRenderContext.h"

#include <iostream>

namespace UltraCanvas {

// Constructors
UltraCanvasGLSurface::UltraCanvasGLSurface(const std::string& id,
                                           int x, int y, int width, int height)
    : UltraCanvasUIElement(id, width, height)
{
    SetBounds(Rect2Di(x, y, width, height));
    surfaceWidth_ = width;
    surfaceHeight_ = height;
    lastRenderTime_ = std::chrono::steady_clock::now();
    lastUpdateTime_ = lastRenderTime_;
}

UltraCanvasGLSurface::UltraCanvasGLSurface(const GLSurfaceConfig& config,
                                           const std::string& id,
                                           int x, int y, int width, int height)
    : UltraCanvasGLSurface(id, x, y, width, height)
{
    config_ = config;
}

UltraCanvasGLSurface::~UltraCanvasGLSurface() {
    CleanupGL();
}

UltraCanvasGLSurface::UltraCanvasGLSurface(UltraCanvasGLSurface&& other) noexcept
    : UltraCanvasUIElement(other.GetIdentifier(), other.GetBounds().width, other.GetBounds().height)
    , config_(other.config_)
    , renderMode_(other.renderMode_)
    , updateInterval_(other.updateInterval_)
    , contextManager_(std::move(other.contextManager_))
    , framebuffer_(std::move(other.framebuffer_))
    , compositeStrategy_(std::move(other.compositeStrategy_))
    , initialized_(other.initialized_)
    , contextValid_(other.contextValid_)
    , renderRequested_(other.renderRequested_)
    , needsResize_(other.needsResize_)
    , surfaceWidth_(other.surfaceWidth_)
    , surfaceHeight_(other.surfaceHeight_)
    , frameNumber_(other.frameNumber_)
    , lastRenderTime_(other.lastRenderTime_)
    , lastUpdateTime_(other.lastUpdateTime_)
    , renderCallback_(std::move(other.renderCallback_))
    , initCallback_(std::move(other.initCallback_))
    , resizeCallback_(std::move(other.resizeCallback_))
    , cleanupCallback_(std::move(other.cleanupCallback_))
{
    SetBounds(other.GetBounds());
    other.initialized_ = false;
    other.contextValid_ = false;
}

UltraCanvasGLSurface& UltraCanvasGLSurface::operator=(UltraCanvasGLSurface&& other) noexcept {
    if (this != &other) {
        CleanupGL();

        // Copy base class state manually since it doesn't support move
        SetIdentifier(other.GetIdentifier());
        SetBounds(other.GetBounds());

        config_ = other.config_;
        renderMode_ = other.renderMode_;
        updateInterval_ = other.updateInterval_;
        contextManager_ = std::move(other.contextManager_);
        framebuffer_ = std::move(other.framebuffer_);
        compositeStrategy_ = std::move(other.compositeStrategy_);
        initialized_ = other.initialized_;
        contextValid_ = other.contextValid_;
        renderRequested_ = other.renderRequested_;
        needsResize_ = other.needsResize_;
        surfaceWidth_ = other.surfaceWidth_;
        surfaceHeight_ = other.surfaceHeight_;
        frameNumber_ = other.frameNumber_;
        lastRenderTime_ = other.lastRenderTime_;
        lastUpdateTime_ = other.lastUpdateTime_;
        renderCallback_ = std::move(other.renderCallback_);
        initCallback_ = std::move(other.initCallback_);
        resizeCallback_ = std::move(other.resizeCallback_);
        cleanupCallback_ = std::move(other.cleanupCallback_);

        other.initialized_ = false;
        other.contextValid_ = false;
    }
    return *this;
}

// Configuration
void UltraCanvasGLSurface::SetConfig(const GLSurfaceConfig& config) {
    if (initialized_) {
        std::cerr << "[UltraCanvasGLSurface] Cannot change config after initialization" << std::endl;
        return;
    }
    config_ = config;
}

void UltraCanvasGLSurface::SetRenderMode(RenderMode mode, float interval) {
    renderMode_ = mode;
    updateInterval_ = interval;
}

// Rendering control
void UltraCanvasGLSurface::RequestRender() {
    renderRequested_ = true;
    RequestRedraw();
}

bool UltraCanvasGLSurface::MakeCurrent() {
    if (!contextManager_ || !contextValid_) {
        return false;
    }
    return contextManager_->MakeCurrent();
}

void UltraCanvasGLSurface::ReleaseCurrent() {
    if (contextManager_) {
        contextManager_->ReleaseCurrent();
    }
}

void* UltraCanvasGLSurface::GetNativeGLContext() const {
    if (!contextManager_) return nullptr;
    return contextManager_->GetNativeContext();
}

// UltraCanvasUIElement overrides
void UltraCanvasGLSurface::Render(IRenderContext* ctx) {
    if (!IsVisible()) return;

    // Initialize GL on first render
    if (!initialized_) {
        if (!InitializeGL()) {
            // Draw error indicator (element-local coordinates)
            ctx->SetFillPaint(Color(200, 50, 50, 255));
            Rect2Di b = GetElementLocalBounds();
            ctx->FillRectangle(Rect2Df{static_cast<float>(b.x), static_cast<float>(b.y),
                              static_cast<float>(b.width), static_cast<float>(b.height)});
            return;
        }
    }

    // Ensure framebuffer exists and is correct size
    if (!EnsureFramebuffer()) {
        return;
    }

    // Check if we should render this frame
    bool didRender = ShouldRender();
    if (didRender) {
        RenderToFramebuffer();
    }

    // Always composite — skips glReadPixels when no new frame, but always redraws cached pixels
    CompositeToSurface(ctx, didRender);

    // Keep the animation loop alive; expensive work is gated by ShouldRender()
    if (renderMode_ == RenderMode::Continuous || renderMode_ == RenderMode::TimedUpdate) {
        RequestRedraw();
    }
}

bool UltraCanvasGLSurface::OnEvent(const UCEvent& event) {
    // Base class handling
    if (UltraCanvasUIElement::OnEvent(event)) {
        return true;
    }

    // Forward relevant events to subclass/callbacks if needed
    // For now, just handle basic interaction

    return false;
}

void UltraCanvasGLSurface::SetBounds(const Rect2Di& newBounds) {
    Rect2Di oldBounds = GetBounds();
    UltraCanvasUIElement::SetBounds(newBounds);

    // Check if size changed
    if (newBounds.width != oldBounds.width || newBounds.height != oldBounds.height) {
        surfaceWidth_ = newBounds.width;
        surfaceHeight_ = newBounds.height;
        needsResize_ = true;

        if (initialized_) {
            RequestRender();
        }
    }
}

// Virtual methods for subclassing
void UltraCanvasGLSurface::OnGLInit() {
    // Default: call user callback if set
    if (initCallback_) {
        initCallback_();
    }
}

void UltraCanvasGLSurface::OnGLRender(const RenderSurfaceInfo& info) {
    // Default: call user callback if set
    if (renderCallback_) {
        renderCallback_(info);
    }
}

void UltraCanvasGLSurface::OnGLResize(int width, int height) {
    // Default: call user callback if set
    if (resizeCallback_) {
        resizeCallback_(width, height);
    }
}

void UltraCanvasGLSurface::OnGLCleanup() {
    // Default: call user callback if set
    if (cleanupCallback_) {
        cleanupCallback_();
    }
}

// Internal methods
bool UltraCanvasGLSurface::InitializeGL() {
    if (initialized_) return true;

    std::cout << "[UltraCanvasGLSurface] Initializing GL context..." << std::endl;

    // Create context manager
    contextManager_ = GLContextManager::Create();
    if (!contextManager_) {
        std::cerr << "[UltraCanvasGLSurface] Failed to create GL context manager" << std::endl;
        return false;
    }

    // Initialize context
    if (!contextManager_->Initialize(config_)) {
        std::cerr << "[UltraCanvasGLSurface] Failed to initialize GL context" << std::endl;
        contextManager_.reset();
        return false;
    }

    contextValid_ = true;

    // Create framebuffer
    framebuffer_ = std::make_unique<GLFramebuffer>();

    // Create composite strategy
    compositeStrategy_ = CreateCompositeStrategy();
    if (!compositeStrategy_) {
        std::cerr << "[UltraCanvasGLSurface] Failed to create composite strategy" << std::endl;
        CleanupGL();
        return false;
    }

    std::cout << "[UltraCanvasGLSurface] Using composite strategy: "
              << compositeStrategy_->GetName() << std::endl;

    // Make context current for user initialization
    if (!MakeCurrent()) {
        std::cerr << "[UltraCanvasGLSurface] Failed to make context current for init" << std::endl;
        CleanupGL();
        return false;
    }

    // Call user initialization
    OnGLInit();

    ReleaseCurrent();

    initialized_ = true;
    renderRequested_ = true;
    // Reset timing so first deltaTime isn't inflated by init duration
    lastRenderTime_ = std::chrono::steady_clock::now();
    lastUpdateTime_ = lastRenderTime_;

    return true;
}

void UltraCanvasGLSurface::CleanupGL() {
    if (!initialized_) return;

    if (contextManager_ && contextValid_) {
        MakeCurrent();
        OnGLCleanup();
        ReleaseCurrent();
    }

    framebuffer_.reset();
    compositeStrategy_.reset();

    if (contextManager_) {
        contextManager_->Cleanup();
        contextManager_.reset();
    }

    initialized_ = false;
    contextValid_ = false;
}

bool UltraCanvasGLSurface::EnsureFramebuffer() {
    if (!framebuffer_ || !contextManager_) {
        return false;
    }

    int width = surfaceWidth_;
    int height = surfaceHeight_;

    if (width <= 0 || height <= 0) {
        return false;
    }

    // Check if we need to create or resize
    if (framebuffer_->GetWidth() != width || framebuffer_->GetHeight() != height) {
        if (!MakeCurrent()) {
            return false;
        }

        bool success;
        if (framebuffer_->GetWidth() == 0) {
            // First time creation
            success = framebuffer_->Create(width, height, config_);
        } else {
            // Resize
            success = framebuffer_->Resize(width, height);
        }

        if (success) {
            OnGLResize(width, height);
        }

        ReleaseCurrent();

        if (!success) {
            std::cerr << "[UltraCanvasGLSurface] Failed to create/resize framebuffer" << std::endl;
            return false;
        }

        needsResize_ = false;
    }

    return true;
}

void UltraCanvasGLSurface::RenderToFramebuffer() {
    if (!framebuffer_ || !contextManager_) {
        return;
    }

    if (!MakeCurrent()) {
        return;
    }

    // Bind framebuffer for rendering
    framebuffer_->Bind();

    // Build render info
    RenderSurfaceInfo info = BuildRenderInfo();

    // Call user render
    OnGLRender(info);

    // Unbind framebuffer
    framebuffer_->Unbind();

    ReleaseCurrent();

    // Update timing
    lastRenderTime_ = std::chrono::steady_clock::now();
    lastUpdateTime_ = lastRenderTime_;
    frameNumber_++;
    renderRequested_ = false;
}

void UltraCanvasGLSurface::CompositeToSurface(IRenderContext* ctx, bool readback) {
    if (!compositeStrategy_ || !framebuffer_) {
        return;
    }

    if (readback && !MakeCurrent()) {
        return;
    }

    // Composite to element-local origin (ctx is already translated to element origin)
    Rect2Di b = GetElementLocalBounds();
    compositeStrategy_->Composite(*framebuffer_, ctx, b.x, b.y, b.width, b.height, readback);

    if (readback) {
        ReleaseCurrent();
    }
}

bool UltraCanvasGLSurface::ShouldRender() const {
    switch (renderMode_) {
        case RenderMode::OnDemand:
            return renderRequested_ || needsResize_;

        case RenderMode::Continuous:
        case RenderMode::TimedUpdate: {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration<float>(now - lastUpdateTime_).count();
            return elapsed >= updateInterval_ || renderRequested_;
        }
    }
    return false;
}

RenderSurfaceInfo UltraCanvasGLSurface::BuildRenderInfo() {
    auto now = std::chrono::steady_clock::now();
    double deltaTime = std::chrono::duration<double>(now - lastRenderTime_).count();

    RenderSurfaceInfo info;
    info.width = surfaceWidth_;
    info.height = surfaceHeight_;
    info.devicePixelRatio = 1.0f;  // TODO: Get actual DPI scaling from window
    info.deltaTime = deltaTime;
    info.frameNumber = frameNumber_;

    return info;
}

// Factory functions
std::shared_ptr<UltraCanvasGLSurface> CreateGLSurface(
    const std::string& identifier,
    int x, int y, int width, int height,
    const GLSurfaceConfig& config)
{
    return std::make_shared<UltraCanvasGLSurface>(config, identifier, x, y, width, height);
}

std::shared_ptr<UltraCanvasGLSurface> CreateGLSurface(
    int x, int y, int width, int height,
    const GLSurfaceConfig& config)
{
    return CreateGLSurface("GLSurface", x, y, width, height, config);
}

} // namespace UltraCanvas
