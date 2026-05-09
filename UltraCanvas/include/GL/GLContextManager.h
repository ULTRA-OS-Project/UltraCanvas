// GL/GLContextManager.h
// Abstract interface for OpenGL context management
// Platform-specific implementations: EGL (Linux), CGL (macOS), WGL (Windows)
#pragma once

#ifndef ULTRACANVAS_ENABLE_GL
#error "GL/GLContextManager.h requires ULTRACANVAS_ENABLE_GL to be defined"
#endif

#include <memory>

namespace UltraCanvas {

struct GLSurfaceConfig;  // Forward declaration

// Abstract base class for GL context management
class GLContextManager {
public:
    virtual ~GLContextManager() = default;

    // Initialize the GL context with the given configuration
    // Returns true on success, false on failure
    virtual bool Initialize(const GLSurfaceConfig& config) = 0;

    // Clean up GL resources
    virtual void Cleanup() = 0;

    // Make this context current on the calling thread
    virtual bool MakeCurrent() = 0;

    // Release the context from the current thread
    virtual void ReleaseCurrent() = 0;

    // Check if the context is valid and usable
    virtual bool IsValid() const = 0;

    // Get the native context handle (EGLContext, CGLContextObj, HGLRC, etc.)
    // Returns nullptr if context is not valid
    virtual void* GetNativeContext() const = 0;

    // Get the native display/device handle (EGLDisplay, etc.)
    // Returns nullptr if not applicable
    virtual void* GetNativeDisplay() const = 0;

    // Query supported GL version after initialization
    virtual int GetGLVersionMajor() const = 0;
    virtual int GetGLVersionMinor() const = 0;

    // Query if context supports specific extension
    virtual bool HasExtension(const char* extensionName) const = 0;

    // Factory method - creates platform-appropriate context manager
    // Returns nullptr if no suitable implementation is available
    static std::unique_ptr<GLContextManager> Create();

    // Query which backend is available
    enum class Backend {
        Unknown,
        EGL,
        GLX,
        CGL,
        WGL
    };
    static Backend GetAvailableBackend();

protected:
    GLContextManager() = default;

    // Non-copyable
    GLContextManager(const GLContextManager&) = delete;
    GLContextManager& operator=(const GLContextManager&) = delete;
};

} // namespace UltraCanvas
