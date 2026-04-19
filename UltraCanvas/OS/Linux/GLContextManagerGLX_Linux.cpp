// GLContextManagerGLX_Linux.cpp
// GLX-based OpenGL context manager for Linux (fallback when EGL is unavailable)
#include "GL/GLContextManager.h"
#include "UltraCanvasGLSurface.h"

#ifdef ULTRACANVAS_HAS_GLX

#include <GL/glx.h>
#include <GL/gl.h>
#include <X11/Xlib.h>
#include <cstring>
#include <iostream>

namespace UltraCanvas {

// GLX extension function pointers
typedef GLXContext (*glXCreateContextAttribsARBProc)(Display*, GLXFBConfig, GLXContext, Bool, const int*);

class GLContextManagerGLX : public GLContextManager {
public:
    GLContextManagerGLX() = default;
    ~GLContextManagerGLX() override { Cleanup(); }

    bool Initialize(const GLSurfaceConfig& config) override {
        // Open X11 display
        display_ = XOpenDisplay(nullptr);
        if (!display_) {
            std::cerr << "[GLContextManagerGLX] Failed to open X11 display" << std::endl;
            return false;
        }

        // Check GLX version
        int glxMajor, glxMinor;
        if (!glXQueryVersion(display_, &glxMajor, &glxMinor)) {
            std::cerr << "[GLContextManagerGLX] Failed to query GLX version" << std::endl;
            Cleanup();
            return false;
        }

        std::cout << "[GLContextManagerGLX] GLX version: " << glxMajor << "." << glxMinor << std::endl;

        // Check for GLX_ARB_create_context extension
        const char* glxExtensions = glXQueryExtensionsString(display_, DefaultScreen(display_));
        bool hasCreateContextARB = glxExtensions && strstr(glxExtensions, "GLX_ARB_create_context");

        // Choose FBConfig
        int fbConfigAttribs[] = {
            GLX_X_RENDERABLE, True,
            GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT,
            GLX_RENDER_TYPE, GLX_RGBA_BIT,
            GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
            GLX_RED_SIZE, config.redBits,
            GLX_GREEN_SIZE, config.greenBits,
            GLX_BLUE_SIZE, config.blueBits,
            GLX_ALPHA_SIZE, config.alphaBits,
            GLX_DEPTH_SIZE, config.depthBits,
            GLX_STENCIL_SIZE, config.stencilBits,
            GLX_DOUBLEBUFFER, False,  // Not needed for offscreen
            None
        };

        int numConfigs;
        GLXFBConfig* fbConfigs = glXChooseFBConfig(display_, DefaultScreen(display_),
                                                    fbConfigAttribs, &numConfigs);
        if (!fbConfigs || numConfigs == 0) {
            std::cerr << "[GLContextManagerGLX] Failed to find suitable FBConfig" << std::endl;
            Cleanup();
            return false;
        }

        fbConfig_ = fbConfigs[0];
        XFree(fbConfigs);

        // Create PBuffer for offscreen rendering
        int pbufferAttribs[] = {
            GLX_PBUFFER_WIDTH, 1,
            GLX_PBUFFER_HEIGHT, 1,
            GLX_PRESERVED_CONTENTS, False,
            None
        };
        pbuffer_ = glXCreatePbuffer(display_, fbConfig_, pbufferAttribs);
        if (!pbuffer_) {
            std::cerr << "[GLContextManagerGLX] Failed to create PBuffer" << std::endl;
            Cleanup();
            return false;
        }

        // Create context
        if (hasCreateContextARB) {
            // Use modern context creation
            glXCreateContextAttribsARBProc glXCreateContextAttribsARB =
                (glXCreateContextAttribsARBProc)glXGetProcAddressARB(
                    (const GLubyte*)"glXCreateContextAttribsARB");

            if (glXCreateContextAttribsARB) {
                int contextAttribs[] = {
                    GLX_CONTEXT_MAJOR_VERSION_ARB, config.glVersionMajor,
                    GLX_CONTEXT_MINOR_VERSION_ARB, config.glVersionMinor,
                    GLX_CONTEXT_PROFILE_MASK_ARB,
                        config.coreProfile ? GLX_CONTEXT_CORE_PROFILE_BIT_ARB
                                          : GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
                    None
                };

                context_ = glXCreateContextAttribsARB(display_, fbConfig_, nullptr, True, contextAttribs);
            }
        }

        if (!context_) {
            // Fall back to legacy context creation
            context_ = glXCreateNewContext(display_, fbConfig_, GLX_RGBA_TYPE, nullptr, True);
        }

        if (!context_) {
            std::cerr << "[GLContextManagerGLX] Failed to create GLX context" << std::endl;
            Cleanup();
            return false;
        }

        // Make context current to query version
        if (!MakeCurrent()) {
            std::cerr << "[GLContextManagerGLX] Failed to make context current" << std::endl;
            Cleanup();
            return false;
        }

        // Query actual GL version
        glGetIntegerv(GL_MAJOR_VERSION, &glVersionMajor_);
        glGetIntegerv(GL_MINOR_VERSION, &glVersionMinor_);

        std::cout << "[GLContextManagerGLX] OpenGL " << glVersionMajor_ << "." << glVersionMinor_
                  << " context created" << std::endl;

        ReleaseCurrent();
        return true;
    }

    void Cleanup() override {
        if (display_) {
            glXMakeCurrent(display_, None, nullptr);

            if (context_) {
                glXDestroyContext(display_, context_);
                context_ = nullptr;
            }

            if (pbuffer_) {
                glXDestroyPbuffer(display_, pbuffer_);
                pbuffer_ = 0;
            }

            XCloseDisplay(display_);
            display_ = nullptr;
        }

        glVersionMajor_ = 0;
        glVersionMinor_ = 0;
    }

    bool MakeCurrent() override {
        if (!display_ || !context_ || !pbuffer_) {
            return false;
        }
        return glXMakeCurrent(display_, pbuffer_, context_) == True;
    }

    void ReleaseCurrent() override {
        if (display_) {
            glXMakeCurrent(display_, None, nullptr);
        }
    }

    bool IsValid() const override {
        return display_ != nullptr && context_ != nullptr && pbuffer_ != 0;
    }

    void* GetNativeContext() const override {
        return reinterpret_cast<void*>(context_);
    }

    void* GetNativeDisplay() const override {
        return reinterpret_cast<void*>(display_);
    }

    int GetGLVersionMajor() const override { return glVersionMajor_; }
    int GetGLVersionMinor() const override { return glVersionMinor_; }

    bool HasExtension(const char* extensionName) const override {
        if (!display_) return false;

        const char* glxExtensions = glXQueryExtensionsString(display_, DefaultScreen(display_));
        if (glxExtensions && strstr(glxExtensions, extensionName)) {
            return true;
        }

        return false;
    }

private:
    Display* display_ = nullptr;
    GLXFBConfig fbConfig_ = nullptr;
    GLXContext context_ = nullptr;
    GLXPbuffer pbuffer_ = 0;
    int glVersionMajor_ = 0;
    int glVersionMinor_ = 0;
};

// Factory implementation for GLX
std::unique_ptr<GLContextManager> CreateGLContextManagerGLX() {
    return std::make_unique<GLContextManagerGLX>();
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_HAS_GLX
