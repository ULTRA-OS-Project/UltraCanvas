// OS/Android/GLContextManagerEGL_Android.cpp
// EGL + OpenGL ES context manager for Android.
// Same offscreen model as the Linux EGL manager (1x1 pbuffer, real rendering
// into FBOs - GLFramebuffer.cpp already compiles against GLES3 headers on
// Android), but binds the OpenGL ES API: Android's EGL exposes no desktop-GL
// client API, so the desktop-oriented fields of GLSurfaceConfig
// (glVersionMajor/Minor, coreProfile) are ignored in favor of the newest
// available ES version (3 preferred, 2 fallback).
// Version: 1.0.0
// Last Modified: 2026-08-10
// Author: UltraCanvas Framework
#include "GL/GLContextManager.h"
#include "UltraCanvasGLSurface.h"

#ifdef ULTRACANVAS_HAS_EGL

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <cstring>
#include <iostream>

#ifndef EGL_OPENGL_ES3_BIT_KHR
#define EGL_OPENGL_ES3_BIT_KHR 0x0040
#endif

namespace UltraCanvas {

class GLContextManagerEGLAndroid : public GLContextManager {
public:
    GLContextManagerEGLAndroid() = default;
    ~GLContextManagerEGLAndroid() override { Cleanup(); }

    bool Initialize(const GLSurfaceConfig& config) override {
        eglDisplay_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (eglDisplay_ == EGL_NO_DISPLAY) {
            std::cerr << "[GLContextManagerEGLAndroid] Failed to get EGL display" << std::endl;
            return false;
        }

        EGLint major, minor;
        if (!eglInitialize(eglDisplay_, &major, &minor)) {
            std::cerr << "[GLContextManagerEGLAndroid] Failed to initialize EGL" << std::endl;
            eglDisplay_ = EGL_NO_DISPLAY;
            return false;
        }

        std::cout << "[GLContextManagerEGLAndroid] EGL version: " << major << "." << minor << std::endl;

        if (!eglBindAPI(EGL_OPENGL_ES_API)) {
            std::cerr << "[GLContextManagerEGLAndroid] Failed to bind OpenGL ES API" << std::endl;
            Cleanup();
            return false;
        }

        // ES 3 config first, ES 2 as fallback (both render into FBOs, and the
        // FBO entry points used by GLFramebuffer are core in both).
        int esVersion = 3;
        if (!ChooseConfig(config, EGL_OPENGL_ES3_BIT_KHR)) {
            esVersion = 2;
            if (!ChooseConfig(config, EGL_OPENGL_ES2_BIT)) {
                std::cerr << "[GLContextManagerEGLAndroid] No usable EGL config" << std::endl;
                Cleanup();
                return false;
            }
        }

        // 1x1 pbuffer: something to make current; all real rendering is FBO.
        EGLint pbufferAttribs[] = {
            EGL_WIDTH, 1,
            EGL_HEIGHT, 1,
            EGL_NONE
        };
        eglSurface_ = eglCreatePbufferSurface(eglDisplay_, eglConfig_, pbufferAttribs);
        if (eglSurface_ == EGL_NO_SURFACE) {
            std::cerr << "[GLContextManagerEGLAndroid] Failed to create PBuffer surface" << std::endl;
            Cleanup();
            return false;
        }

        EGLint contextAttribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, esVersion,
            EGL_NONE
        };
        eglContext_ = eglCreateContext(eglDisplay_, eglConfig_, EGL_NO_CONTEXT, contextAttribs);
        if (eglContext_ == EGL_NO_CONTEXT && esVersion == 3) {
            // Config claimed ES3 but context creation failed - retry as ES2.
            esVersion = 2;
            contextAttribs[1] = esVersion;
            eglContext_ = eglCreateContext(eglDisplay_, eglConfig_, EGL_NO_CONTEXT, contextAttribs);
        }
        if (eglContext_ == EGL_NO_CONTEXT) {
            std::cerr << "[GLContextManagerEGLAndroid] Failed to create EGL context" << std::endl;
            Cleanup();
            return false;
        }

        if (!MakeCurrent()) {
            std::cerr << "[GLContextManagerEGLAndroid] Failed to make context current" << std::endl;
            Cleanup();
            return false;
        }

        // GL_MAJOR/MINOR_VERSION queries exist from ES 3.0 on; an ES 2
        // context reports through the request that created it.
        glVersionMajor_ = esVersion;
        glVersionMinor_ = 0;
        if (esVersion >= 3) {
            glGetIntegerv(GL_MAJOR_VERSION, &glVersionMajor_);
            glGetIntegerv(GL_MINOR_VERSION, &glVersionMinor_);
        }

        std::cout << "[GLContextManagerEGLAndroid] OpenGL ES " << glVersionMajor_ << "."
                  << glVersionMinor_ << " context created" << std::endl;

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
        const char* eglExtensions = eglQueryString(eglDisplay_, EGL_EXTENSIONS);
        if (eglExtensions && strstr(eglExtensions, extensionName)) {
            return true;
        }
        return false;
    }

private:
    bool ChooseConfig(const GLSurfaceConfig& config, EGLint renderableType) {
        EGLint configAttribs[] = {
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RENDERABLE_TYPE, renderableType,
            EGL_RED_SIZE, config.redBits,
            EGL_GREEN_SIZE, config.greenBits,
            EGL_BLUE_SIZE, config.blueBits,
            EGL_ALPHA_SIZE, config.alphaBits,
            EGL_DEPTH_SIZE, config.depthBits,
            EGL_STENCIL_SIZE, config.stencilBits,
            EGL_NONE
        };
        EGLint numConfigs = 0;
        return eglChooseConfig(eglDisplay_, configAttribs, &eglConfig_, 1, &numConfigs)
               && numConfigs > 0;
    }

    EGLDisplay eglDisplay_ = EGL_NO_DISPLAY;
    EGLConfig eglConfig_ = nullptr;
    EGLContext eglContext_ = EGL_NO_CONTEXT;
    EGLSurface eglSurface_ = EGL_NO_SURFACE;
    int glVersionMajor_ = 0;
    int glVersionMinor_ = 0;
};

// Same factory symbol the platform-agnostic dispatcher (GLContextManager.cpp)
// resolves on every EGL platform; only one EGL TU is ever in a build.
std::unique_ptr<GLContextManager> CreateGLContextManagerEGL() {
    return std::make_unique<GLContextManagerEGLAndroid>();
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_HAS_EGL
