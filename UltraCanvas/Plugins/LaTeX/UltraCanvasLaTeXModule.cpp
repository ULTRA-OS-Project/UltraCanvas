// Plugins/LaTeX/UltraCanvasLaTeXModule.cpp
// C ABI entry points for the on-demand LaTeX module (libUltraCanvasLaTeX).
//
// The core loader (core/UltraCanvasLaTeXModuleLoader.cpp) resolves these
// symbols via dlsym after dlopen()ing this module. They are the only symbols
// the core depends on; everything else (MicroTeX, the backend, the concrete
// view) stays private to the module. See UltraCanvasLaTeXModuleABI.h.
//
// Version: 1.0.0
// Last Modified: 2026-06-29
// Author: UltraCanvas Framework

#ifdef ULTRACANVAS_PLUGIN_LATEX

#include "Plugins/LaTeX/UltraCanvasLaTeXModuleABI.h"
#include "Plugins/LaTeX/UltraCanvasLaTeXViewImpl.h"
#include "Plugins/LaTeX/UltraCanvasLaTeXBackend.h"

#include "microtexexport.h" // MICROTEX_EXPORT => default visibility on GCC/Clang

// Export with default visibility so the symbols are resolvable via dlsym even
// when the module is compiled with -fvisibility=hidden.
#if defined(_WIN32)
  #define UC_LATEX_API extern "C" __declspec(dllexport)
#else
  #define UC_LATEX_API extern "C" __attribute__((visibility("default")))
#endif

UC_LATEX_API int UltraCanvasLaTeXModule_ABIVersion(void) {
    return ULTRACANVAS_LATEX_ABI_VERSION;
}

UC_LATEX_API UltraCanvas::UltraCanvasLaTeXView*
UltraCanvasLaTeXModule_CreateView(const char* id, float x, float y, float w, float h) {
    try {
        return new UltraCanvas::UltraCanvasLaTeXViewImpl(id ? id : "LaTeXView", x, y, w, h);
    } catch (...) {
        return nullptr;
    }
}

UC_LATEX_API void
UltraCanvasLaTeXModule_DestroyView(UltraCanvas::UltraCanvasLaTeXView* view) {
    delete view; // virtual dtor -> UltraCanvasLaTeXViewImpl, inside this module
}

UC_LATEX_API void
UltraCanvasLaTeXModule_SetFontSearchDir(const char* dir) {
    if (dir) UltraCanvas::SetLaTeXEngineFontDir(dir); // backend-side setter
}

#endif // ULTRACANVAS_PLUGIN_LATEX
