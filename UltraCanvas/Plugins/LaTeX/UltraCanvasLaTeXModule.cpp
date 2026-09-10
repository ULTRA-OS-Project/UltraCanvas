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
#include "Plugins/LaTeX/UltraCanvasMathEngine.h"
#include "Plugins/LaTeX/UltraCanvasMathRender.h"

#include <string>

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

// ===== Inline math (ABI 3) =====
// The text stack (UltraCanvasTextArea's Markdown mode, the Word document
// views) typesets $...$ runs through these and draws them baseline-aligned
// inside its own text layouts. Always the native engine: it is the one with
// a baseline.

namespace {
struct InlineHandle {
    UltraCanvas::MathTypesetResult result;
    std::string error;
    uint32_t color = 0xFF000000u;
};
}

UC_LATEX_API void*
UltraCanvasLaTeXModule_TypesetInline(const char* latex, float fontSizePx, uint32_t argb,
                                     int displayStyle, UltraCanvas::IRenderContext* ctx) {
    using namespace UltraCanvas;
    if (!EnsureNativeLaTeXEngineInitialized()) return nullptr;
    try {
        auto* h = new InlineHandle();
        h->color = argb;
        MathContextTextFallback fallback(ctx);
        MathTypesetOptions opt;
        opt.fontSize = fontSizePx > 0.f ? fontSizePx : 16.f;
        opt.color = argb;
        opt.style = displayStyle ? MathStyle::Display() : MathStyle::Text();
        opt.textFallback = ctx ? &fallback : nullptr;
        h->result = GetSharedMathEngine().Typeset(latex ? latex : "", opt);
        if (!h->result.diagnostics.empty()) h->error = h->result.diagnostics.front().message;
        return h;
    } catch (...) {
        return nullptr;
    }
}

UC_LATEX_API int
UltraCanvasLaTeXModule_InlineMetrics(void* handle, float* width, float* ascent, float* descent, const char** error) {
    auto* h = static_cast<InlineHandle*>(handle);
    if (!h) { if (width) *width = 0; if (ascent) *ascent = 0; if (descent) *descent = 0; if (error) *error = nullptr; return 0; }
    if (width) *width = h->result.width;
    if (ascent) *ascent = h->result.height;
    if (descent) *descent = h->result.depth;
    if (error) *error = h->error.empty() ? nullptr : h->error.c_str();
    return h->error.empty() ? 1 : 0;
}

UC_LATEX_API void
UltraCanvasLaTeXModule_DrawInline(void* handle, UltraCanvas::IRenderContext* ctx, double x, double baselineY) {
    auto* h = static_cast<InlineHandle*>(handle);
    if (!h || !ctx || !h->result.root) return;
    UltraCanvas::DrawMathBox(ctx, *h->result.root, x, baselineY, UltraCanvas::Color::FromARGB(h->color));
}

UC_LATEX_API void
UltraCanvasLaTeXModule_ReleaseInline(void* handle) {
    delete static_cast<InlineHandle*>(handle);
}

#endif // ULTRACANVAS_PLUGIN_LATEX
