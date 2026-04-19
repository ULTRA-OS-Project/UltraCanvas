// GLContextManager.cpp
// Factory implementation for GLContextManager
#include "GL/GLContextManager.h"

namespace UltraCanvas {

// Forward declarations for platform-specific implementations
#ifdef ULTRACANVAS_HAS_EGL
std::unique_ptr<GLContextManager> CreateGLContextManagerEGL();
#endif

#ifdef ULTRACANVAS_HAS_GLX
std::unique_ptr<GLContextManager> CreateGLContextManagerGLX();
#endif

#ifdef ULTRACANVAS_HAS_CGL
std::unique_ptr<GLContextManager> CreateGLContextManagerCGL();
#endif

#ifdef ULTRACANVAS_HAS_WGL
std::unique_ptr<GLContextManager> CreateGLContextManagerWGL();
#endif

GLContextManager::Backend GLContextManager::GetAvailableBackend() {
#if defined(ULTRACANVAS_HAS_EGL)
    return Backend::EGL;
#elif defined(ULTRACANVAS_HAS_GLX)
    return Backend::GLX;
#elif defined(ULTRACANVAS_HAS_CGL)
    return Backend::CGL;
#elif defined(ULTRACANVAS_HAS_WGL)
    return Backend::WGL;
#else
    return Backend::Unknown;
#endif
}

std::unique_ptr<GLContextManager> GLContextManager::Create() {
#if defined(ULTRACANVAS_HAS_EGL)
    // Prefer EGL on Linux
    return CreateGLContextManagerEGL();
#elif defined(ULTRACANVAS_HAS_GLX)
    // Fall back to GLX
    return CreateGLContextManagerGLX();
#elif defined(ULTRACANVAS_HAS_CGL)
    // macOS
    return CreateGLContextManagerCGL();
#elif defined(ULTRACANVAS_HAS_WGL)
    // Windows
    return CreateGLContextManagerWGL();
#else
    // No GL support available
    return nullptr;
#endif
}

} // namespace UltraCanvas
