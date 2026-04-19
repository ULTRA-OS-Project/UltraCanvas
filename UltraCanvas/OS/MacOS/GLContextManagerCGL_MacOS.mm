// GLContextManagerCGL_MacOS.mm
// CGL-based OpenGL context manager for macOS
// This is a stub implementation - full implementation deferred to future phase
#include "GL/GLContextManager.h"
#include "UltraCanvasGLSurface.h"

#ifdef ULTRACANVAS_HAS_CGL

#import <OpenGL/OpenGL.h>
#import <OpenGL/gl3.h>
#include <iostream>

namespace UltraCanvas {

class GLContextManagerCGL : public GLContextManager {
public:
    GLContextManagerCGL() = default;
    ~GLContextManagerCGL() override { Cleanup(); }

    bool Initialize(const GLSurfaceConfig& config) override {
        // Create pixel format
        CGLPixelFormatAttribute attrs[] = {
            kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,
            kCGLPFAColorSize, (CGLPixelFormatAttribute)24,
            kCGLPFAAlphaSize, (CGLPixelFormatAttribute)8,
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

        // Make current to query version
        if (!MakeCurrent()) {
            std::cerr << "[GLContextManagerCGL] Failed to make context current" << std::endl;
            Cleanup();
            return false;
        }

        // Query GL version
        glGetIntegerv(GL_MAJOR_VERSION, &glVersionMajor_);
        glGetIntegerv(GL_MINOR_VERSION, &glVersionMinor_);

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
        // Would need to query GL extensions string
        return false;
    }

private:
    CGLPixelFormatObj pixelFormat_ = nullptr;
    CGLContextObj cglContext_ = nullptr;
    int glVersionMajor_ = 0;
    int glVersionMinor_ = 0;
};

// Factory implementation for CGL
std::unique_ptr<GLContextManager> CreateGLContextManagerCGL() {
    return std::make_unique<GLContextManagerCGL>();
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_HAS_CGL
