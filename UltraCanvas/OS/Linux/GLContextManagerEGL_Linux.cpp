// GLContextManagerEGL_Linux.cpp
// EGL-based OpenGL context manager for Linux
#include "GL/GLContextManager.h"
#include "UltraCanvasGLSurface.h"

#ifdef ULTRACANVAS_HAS_EGL

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <cstring>
#include <iostream>

namespace UltraCanvas {

class GLContextManagerEGL : public GLContextManager {
public:
    GLContextManagerEGL() = default;
    ~GLContextManagerEGL() override { Cleanup(); }

    bool Initialize(const GLSurfaceConfig& config) override {
        // Get EGL display
        // First try platform-specific display, fall back to default
        eglDisplay_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (eglDisplay_ == EGL_NO_DISPLAY) {
            std::cerr << "[GLContextManagerEGL] Failed to get EGL display" << std::endl;
            return false;
        }

        // Initialize EGL
        EGLint major, minor;
        if (!eglInitialize(eglDisplay_, &major, &minor)) {
            std::cerr << "[GLContextManagerEGL] Failed to initialize EGL" << std::endl;
            eglDisplay_ = EGL_NO_DISPLAY;
            return false;
        }

        std::cout << "[GLContextManagerEGL] EGL version: " << major << "." << minor << std::endl;

        // Bind OpenGL API (not OpenGL ES)
        if (!eglBindAPI(EGL_OPENGL_API)) {
            std::cerr << "[GLContextManagerEGL] Failed to bind OpenGL API" << std::endl;
            Cleanup();
            return false;
        }

        // Choose EGL config
        EGLint configAttribs[] = {
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
            EGL_RED_SIZE, config.redBits,
            EGL_GREEN_SIZE, config.greenBits,
            EGL_BLUE_SIZE, config.blueBits,
            EGL_ALPHA_SIZE, config.alphaBits,
            EGL_DEPTH_SIZE, config.depthBits,
            EGL_STENCIL_SIZE, config.stencilBits,
            EGL_NONE
        };

        EGLint numConfigs;
        if (!eglChooseConfig(eglDisplay_, configAttribs, &eglConfig_, 1, &numConfigs) || numConfigs == 0) {
            std::cerr << "[GLContextManagerEGL] Failed to choose EGL config" << std::endl;
            Cleanup();
            return false;
        }

        // Create a 1x1 PBuffer surface (we use FBO for actual rendering)
        EGLint pbufferAttribs[] = {
            EGL_WIDTH, 1,
            EGL_HEIGHT, 1,
            EGL_NONE
        };
        eglSurface_ = eglCreatePbufferSurface(eglDisplay_, eglConfig_, pbufferAttribs);
        if (eglSurface_ == EGL_NO_SURFACE) {
            std::cerr << "[GLContextManagerEGL] Failed to create PBuffer surface" << std::endl;
            Cleanup();
            return false;
        }

        // Create OpenGL context
        EGLint contextAttribs[] = {
            EGL_CONTEXT_MAJOR_VERSION, config.glVersionMajor,
            EGL_CONTEXT_MINOR_VERSION, config.glVersionMinor,
            EGL_CONTEXT_OPENGL_PROFILE_MASK,
                config.coreProfile ? EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT : EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT,
            EGL_NONE
        };

        // Add debug flag if requested
        if (config.debugContext) {
            // Would need to rebuild attribs with EGL_CONTEXT_OPENGL_DEBUG
        }

        eglContext_ = eglCreateContext(eglDisplay_, eglConfig_, EGL_NO_CONTEXT, contextAttribs);
        if (eglContext_ == EGL_NO_CONTEXT) {
            std::cerr << "[GLContextManagerEGL] Failed to create EGL context" << std::endl;
            Cleanup();
            return false;
        }

        // Make context current to query version
        if (!MakeCurrent()) {
            std::cerr << "[GLContextManagerEGL] Failed to make context current" << std::endl;
            Cleanup();
            return false;
        }

        // Query actual GL version
        glGetIntegerv(GL_MAJOR_VERSION, &glVersionMajor_);
        glGetIntegerv(GL_MINOR_VERSION, &glVersionMinor_);

        std::cout << "[GLContextManagerEGL] OpenGL " << glVersionMajor_ << "." << glVersionMinor_
                  << " context created" << std::endl;

        ReleaseCurrent();
        return true;
    }

    void Cleanup() override {
        if (eglDisplay_ != EGL_NO_DISPLAY) {
            eglMakeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

            if (eglContext_ != EGL_NO_CONTEXT) {
                eglDestroyContext(eglDisplay_, eglContext_);
                eglContext_ = EGL_NO_CONTEXT;
            }

            if (eglSurface_ != EGL_NO_SURFACE) {
                eglDestroySurface(eglDisplay_, eglSurface_);
                eglSurface_ = EGL_NO_SURFACE;
            }

            eglTerminate(eglDisplay_);
            eglDisplay_ = EGL_NO_DISPLAY;
        }

        glVersionMajor_ = 0;
        glVersionMinor_ = 0;
    }

    bool MakeCurrent() override {
        if (eglDisplay_ == EGL_NO_DISPLAY || eglContext_ == EGL_NO_CONTEXT) {
            return false;
        }
        return eglMakeCurrent(eglDisplay_, eglSurface_, eglSurface_, eglContext_) == EGL_TRUE;
    }

    void ReleaseCurrent() override {
        if (eglDisplay_ != EGL_NO_DISPLAY) {
            eglMakeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
    }

    bool IsValid() const override {
        return eglDisplay_ != EGL_NO_DISPLAY &&
               eglContext_ != EGL_NO_CONTEXT &&
               eglSurface_ != EGL_NO_SURFACE;
    }

    void* GetNativeContext() const override {
        return reinterpret_cast<void*>(eglContext_);
    }

    void* GetNativeDisplay() const override {
        return reinterpret_cast<void*>(eglDisplay_);
    }

    int GetGLVersionMajor() const override { return glVersionMajor_; }
    int GetGLVersionMinor() const override { return glVersionMinor_; }

    bool HasExtension(const char* extensionName) const override {
        if (!IsValid()) return false;

        // Check EGL extensions
        const char* eglExtensions = eglQueryString(eglDisplay_, EGL_EXTENSIONS);
        if (eglExtensions && strstr(eglExtensions, extensionName)) {
            return true;
        }

        // Check GL extensions (need to be current)
        // This is a simplified check - full implementation would parse the string properly
        return false;
    }

private:
    EGLDisplay eglDisplay_ = EGL_NO_DISPLAY;
    EGLConfig eglConfig_ = nullptr;
    EGLContext eglContext_ = EGL_NO_CONTEXT;
    EGLSurface eglSurface_ = EGL_NO_SURFACE;
    int glVersionMajor_ = 0;
    int glVersionMinor_ = 0;
};

// Factory implementation for EGL
std::unique_ptr<GLContextManager> CreateGLContextManagerEGL() {
    return std::make_unique<GLContextManagerEGL>();
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_HAS_EGL
