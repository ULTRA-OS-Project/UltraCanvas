// GLContextManagerWGL_MSWindows.cpp
// WGL-based OpenGL context manager for Windows.
//
// Windows' opengl32.dll only exports OpenGL 1.1, so a modern (3.x/4.x core)
// context cannot be created directly and the modern entry points must be
// resolved at runtime. We therefore:
//   1. Create a hidden helper window and a temporary legacy GL context,
//   2. load wglCreateContextAttribsARB through it,
//   3. create the requested core/compatibility context,
//   4. initialize GLEW so the shared FBO/shader code can call modern GL.
//
// Like the other backends, the context is windowless from the caller's point
// of view: rendering goes to an offscreen FBO whose pixels are read back and
// composited into the 2D Cairo surface (see ICompositeStrategy).
#include "GL/GLContextManager.h"
#include "UltraCanvasGLSurface.h"

#ifdef ULTRACANVAS_HAS_WGL

#include <windows.h>
#include <GL/glew.h>
#include <GL/wglext.h>
#include <iostream>
#include <string>

namespace UltraCanvas {

namespace {
// Window-class name for the hidden helper window that owns our device context.
constexpr const wchar_t* kHelperClassName = L"UltraCanvasWGLHelperWindow";
}

class GLContextManagerWGL : public GLContextManager {
public:
    GLContextManagerWGL() = default;
    ~GLContextManagerWGL() override { Cleanup(); }

    bool Initialize(const GLSurfaceConfig& config) override {
        hinstance_ = GetModuleHandleW(nullptr);

        // --- Register a minimal window class (idempotent) ---
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = hinstance_;
        wc.lpszClassName = kHelperClassName;
        if (!RegisterClassExW(&wc)) {
            DWORD err = GetLastError();
            if (err != ERROR_CLASS_ALREADY_EXISTS) {
                std::cerr << "[GLContextManagerWGL] RegisterClassExW failed: " << err << std::endl;
                return false;
            }
        }
        classRegistered_ = true;

        // --- Create a hidden helper window to own the HDC ---
        hwnd_ = CreateWindowExW(0, kHelperClassName, L"UltraCanvas GL",
                                WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1, 1,
                                nullptr, nullptr, hinstance_, nullptr);
        if (!hwnd_) {
            std::cerr << "[GLContextManagerWGL] CreateWindowExW failed: " << GetLastError() << std::endl;
            Cleanup();
            return false;
        }

        hdc_ = GetDC(hwnd_);
        if (!hdc_) {
            std::cerr << "[GLContextManagerWGL] GetDC failed" << std::endl;
            Cleanup();
            return false;
        }

        // --- Choose and set a pixel format (FBO handles MSAA, so basic is fine) ---
        PIXELFORMATDESCRIPTOR pfd = {};
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = static_cast<BYTE>(config.redBits + config.greenBits + config.blueBits);
        pfd.cAlphaBits = static_cast<BYTE>(config.alphaBits);
        pfd.cDepthBits = static_cast<BYTE>(config.depthBits);
        pfd.cStencilBits = static_cast<BYTE>(config.stencilBits);
        pfd.iLayerType = PFD_MAIN_PLANE;

        int pixelFormat = ChoosePixelFormat(hdc_, &pfd);
        if (pixelFormat == 0 || !SetPixelFormat(hdc_, pixelFormat, &pfd)) {
            std::cerr << "[GLContextManagerWGL] Failed to set pixel format: " << GetLastError() << std::endl;
            Cleanup();
            return false;
        }

        // --- Bootstrap: legacy context to load the ARB context-creation entry ---
        HGLRC legacy = wglCreateContext(hdc_);
        if (!legacy) {
            std::cerr << "[GLContextManagerWGL] wglCreateContext (legacy) failed" << std::endl;
            Cleanup();
            return false;
        }
        if (!wglMakeCurrent(hdc_, legacy)) {
            std::cerr << "[GLContextManagerWGL] wglMakeCurrent (legacy) failed" << std::endl;
            wglDeleteContext(legacy);
            Cleanup();
            return false;
        }

        auto wglCreateContextAttribsARB =
            reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
                wglGetProcAddress("wglCreateContextAttribsARB"));

        if (wglCreateContextAttribsARB) {
            const int profileBit = config.coreProfile
                ? WGL_CONTEXT_CORE_PROFILE_BIT_ARB
                : WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB;
            // Forward-compatible only makes sense for a core profile; setting it
            // alongside a compatibility profile can make context creation fail.
            int contextFlags = config.coreProfile ? WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB : 0;
            if (config.debugContext) {
                contextFlags |= WGL_CONTEXT_DEBUG_BIT_ARB;
            }

            const int attribs[] = {
                WGL_CONTEXT_MAJOR_VERSION_ARB, config.glVersionMajor,
                WGL_CONTEXT_MINOR_VERSION_ARB, config.glVersionMinor,
                WGL_CONTEXT_PROFILE_MASK_ARB,  profileBit,
                WGL_CONTEXT_FLAGS_ARB,         contextFlags,
                0
            };

            hglrc_ = wglCreateContextAttribsARB(hdc_, nullptr, attribs);
        } else {
            std::cerr << "[GLContextManagerWGL] wglCreateContextAttribsARB unavailable; "
                         "falling back to legacy context" << std::endl;
        }

        // Done with the bootstrap context.
        wglMakeCurrent(nullptr, nullptr);

        if (hglrc_) {
            wglDeleteContext(legacy);
        } else {
            // Fall back to the legacy context if modern creation failed.
            hglrc_ = legacy;
        }

        if (!MakeCurrent()) {
            std::cerr << "[GLContextManagerWGL] Failed to make context current" << std::endl;
            Cleanup();
            return false;
        }

        // --- Initialize GLEW so modern GL entry points resolve ---
        glewExperimental = GL_TRUE;
        GLenum glewErr = glewInit();
        // A core-profile context makes glewInit() emit a benign GL_INVALID_ENUM;
        // GLEW_ERROR_NO_GLX_DISPLAY is irrelevant on Windows. Treat only hard
        // failures as fatal.
        if (glewErr != GLEW_OK) {
            std::cerr << "[GLContextManagerWGL] glewInit failed: "
                      << reinterpret_cast<const char*>(glewGetErrorString(glewErr)) << std::endl;
            ReleaseCurrent();
            Cleanup();
            return false;
        }

        // Query GL version
        glGetIntegerv(GL_MAJOR_VERSION, &glVersionMajor_);
        glGetIntegerv(GL_MINOR_VERSION, &glVersionMinor_);

        // Cache the extension list (core profiles require indexed glGetStringi).
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

        std::cout << "[GLContextManagerWGL] OpenGL " << glVersionMajor_ << "." << glVersionMinor_
                  << " context created" << std::endl;

        ReleaseCurrent();
        return true;
    }

    void Cleanup() override {
        if (hglrc_) {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(hglrc_);
            hglrc_ = nullptr;
        }
        if (hdc_ && hwnd_) {
            ReleaseDC(hwnd_, hdc_);
        }
        hdc_ = nullptr;
        if (hwnd_) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
        if (classRegistered_) {
            UnregisterClassW(kHelperClassName, hinstance_);
            classRegistered_ = false;
        }
        glExtensions_.clear();
        glVersionMajor_ = 0;
        glVersionMinor_ = 0;
    }

    bool MakeCurrent() override {
        if (!hdc_ || !hglrc_) return false;
        return wglMakeCurrent(hdc_, hglrc_) == TRUE;
    }

    void ReleaseCurrent() override {
        wglMakeCurrent(nullptr, nullptr);
    }

    bool IsValid() const override {
        return hglrc_ != nullptr;
    }

    void* GetNativeContext() const override {
        return reinterpret_cast<void*>(hglrc_);
    }

    void* GetNativeDisplay() const override {
        return reinterpret_cast<void*>(hdc_);
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
    HINSTANCE hinstance_ = nullptr;
    HWND hwnd_ = nullptr;
    HDC hdc_ = nullptr;
    HGLRC hglrc_ = nullptr;
    bool classRegistered_ = false;
    std::string glExtensions_;
    int glVersionMajor_ = 0;
    int glVersionMinor_ = 0;
};

// Factory implementation for WGL
std::unique_ptr<GLContextManager> CreateGLContextManagerWGL() {
    return std::make_unique<GLContextManagerWGL>();
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_HAS_WGL
