// GLContextManagerCGL_MacOS.mm
// CGL-based OpenGL context manager for macOS.
// Creates a windowless (drawable-less) Core Profile context that is used
// purely for offscreen FBO rendering; the resulting pixels are read back
// and composited into the 2D Cairo surface (see ICompositeStrategy).
#include "GL/GLContextManager.h"
#include "UltraCanvasGLSurface.h"

#ifdef ULTRACANVAS_HAS_CGL

#import <OpenGL/OpenGL.h>
#import <OpenGL/gl3.h>
#include <iostream>
#include <string>

// kCGLOGLPVersion_GL4_Core may be absent on very old SDKs; define a fallback.
#ifndef kCGLOGLPVersion_GL4_Core
#define kCGLOGLPVersion_GL4_Core ((CGLOpenGLProfile)0x4100)
#endif

namespace UltraCanvas {

class GLContextManagerCGL : public GLContextManager {
public:
    GLContextManagerCGL() = default;
    ~GLContextManagerCGL() override { Cleanup(); }

    bool Initialize(const GLSurfaceConfig& config) override {
        // macOS exposes OpenGL profiles at a coarse granularity (it does not
        // honor an arbitrary major.minor like GLX/EGL). Map the requested
        // version/profile onto the closest CGL profile constant.
        CGLOpenGLProfile profile;
        if (!config.coreProfile) {
            profile = kCGLOGLPVersion_Legacy;       // up to OpenGL 2.1
        } else if (config.glVersionMajor >= 4) {
            profile = kCGLOGLPVersion_GL4_Core;     // highest available 4.x
        } else {
            profile = kCGLOGLPVersion_3_2_Core;     // highest available 3.x
        }

        const GLint colorSize = config.redBits + config.greenBits + config.blueBits;

        CGLPixelFormatAttribute attrs[] = {
            kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)profile,
            kCGLPFAColorSize, (CGLPixelFormatAttribute)colorSize,
            kCGLPFAAlphaSize, (CGLPixelFormatAttribute)config.alphaBits,
            kCGLPFADepthSize, (CGLPixelFormatAttribute)config.depthBits,
            kCGLPFAStencilSize, (CGLPixelFormatAttribute)config.stencilBits,
            kCGLPFAAccelerated,
            kCGLPFAAllowOfflineRenderers,
            (CGLPixelFormatAttribute)0
        };

        GLint numFormats = 0;
        CGLError err = CGLChoosePixelFormat(attrs, &pixelFormat_, &numFormats);
        if (err != kCGLNoError || numFormats == 0) {
            std::cerr << "[GLContextManagerCGL] Failed to choose pixel format: " << err << std::endl;
            return false;
        }

        // Create context
        err = CGLCreateContext(pixelFormat_, nullptr, &cglContext_);
        if (err != kCGLNoError) {
            std::cerr << "[GLContextManagerCGL] Failed to create context: " << err << std::endl;
            CGLDestroyPixelFormat(pixelFormat_);
            pixelFormat_ = nullptr;
            return false;
        }

        // Make current to query version and cache extensions
        if (!MakeCurrent()) {
            std::cerr << "[GLContextManagerCGL] Failed to make context current" << std::endl;
            Cleanup();
            return false;
        }

        // Query GL version
        glGetIntegerv(GL_MAJOR_VERSION, &glVersionMajor_);
        glGetIntegerv(GL_MINOR_VERSION, &glVersionMinor_);

        // Cache the extension list. Core profiles require the indexed
        // glGetStringi API (the legacy glGetString(GL_EXTENSIONS) is removed).
        glExtensions_.clear();
        GLint numExtensions = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);
        for (GLint i = 0; i < numExtensions; ++i) {
            const GLubyte* ext = glGetStringi(GL_EXTENSIONS, i);
            if (ext) {
                glExtensions_ += reinterpret_cast<const char*>(ext);
                glExtensions_ += ' ';
            }
        }

        std::cout << "[GLContextManagerCGL] OpenGL " << glVersionMajor_ << "." << glVersionMinor_
                  << " context created" << std::endl;

        ReleaseCurrent();
        return true;
    }

    void Cleanup() override {
        if (cglContext_) {
            CGLSetCurrentContext(nullptr);
            CGLDestroyContext(cglContext_);
            cglContext_ = nullptr;
        }
        if (pixelFormat_) {
            CGLDestroyPixelFormat(pixelFormat_);
            pixelFormat_ = nullptr;
        }
        glExtensions_.clear();
        glVersionMajor_ = 0;
        glVersionMinor_ = 0;
    }

    bool MakeCurrent() override {
        if (!cglContext_) return false;
        return CGLSetCurrentContext(cglContext_) == kCGLNoError;
    }

    void ReleaseCurrent() override {
        CGLSetCurrentContext(nullptr);
    }

    bool IsValid() const override {
        return cglContext_ != nullptr;
    }

    void* GetNativeContext() const override {
        return reinterpret_cast<void*>(cglContext_);
    }

    void* GetNativeDisplay() const override {
        return nullptr;  // CGL doesn't have a display concept
    }

    int GetGLVersionMajor() const override { return glVersionMajor_; }
    int GetGLVersionMinor() const override { return glVersionMinor_; }

    bool HasExtension(const char* extensionName) const override {
        if (!extensionName || glExtensions_.empty()) return false;
        // Match against space-delimited tokens to avoid substring false positives
        std::string needle = " ";
        needle += extensionName;
        needle += " ";
        std::string haystack = " ";
        haystack += glExtensions_;
        return haystack.find(needle) != std::string::npos;
    }

private:
    CGLPixelFormatObj pixelFormat_ = nullptr;
    CGLContextObj cglContext_ = nullptr;
    std::string glExtensions_;
    int glVersionMajor_ = 0;
    int glVersionMinor_ = 0;
};

// Factory implementation for CGL
std::unique_ptr<GLContextManager> CreateGLContextManagerCGL() {
    return std::make_unique<GLContextManagerCGL>();
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_HAS_CGL
