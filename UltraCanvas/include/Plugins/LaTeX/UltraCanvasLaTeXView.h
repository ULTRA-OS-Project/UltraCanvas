// include/Plugins/LaTeX/UltraCanvasLaTeXView.h
// Public interface for LaTeX math rendering.
//
// LaTeX support is an ON-DEMAND module: the MicroTeX engine and its math font
// live in a separate shared object (libUltraCanvasLaTeX) that the core loads
// via dlopen the first time CreateLaTeXView() is called. Until then nothing
// LaTeX-related is mapped into the process.
//
// `UltraCanvasLaTeXView` is therefore an ABSTRACT interface here - a regular
// UltraCanvasUIElement (so it plugs into layout / render / events) whose
// concrete implementation lives in the module. Application code only ever
// touches this interface and the free factory below; it never needs the
// MicroTeX headers or the module's include paths.
//
// Version: 2.0.0
// Last Modified: 2026-06-29
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"

#include <memory>
#include <string>

namespace UltraCanvas {

// Abstract LaTeX (math) view. Concrete instances are produced by
// CreateLaTeXView() once the LaTeX module has been loaded on demand.
class UltraCanvasLaTeXView : public UltraCanvasUIElement {
public:
    UltraCanvasLaTeXView(const std::string& identifier, float x, float y, float w, float h)
        : UltraCanvasUIElement(identifier, x, y, w, h) {}
    ~UltraCanvasLaTeXView() override = default;

    // ===== CONTENT =====
    virtual void SetLaTeX(const std::string& latex) = 0;
    virtual const std::string& GetLaTeX() const = 0;

    // ===== STYLE =====
    virtual void SetTextSize(float pixels) = 0;      // default 20
    virtual float GetTextSize() const = 0;
    virtual void SetTextColor(const Color& color) = 0;
    virtual const Color& GetTextColor() const = 0;
    virtual void SetMaxWidth(float pixels) = 0;      // 0 = unlimited / intrinsic

    // ===== STATUS =====
    virtual bool IsValid() const = 0;                // last SetLaTeX produced a render
    virtual const std::string& GetLastError() const = 0;
};

// ===== On-demand factory (implemented by the core loader) =====

// Create a LaTeX view. The first call triggers a dlopen of the LaTeX module;
// subsequent calls reuse the already-loaded module. Returns nullptr (and sets
// no element) if the module cannot be found or fails to load - check
// IsLaTeXModuleAvailable()/GetLaTeXModuleError() for diagnostics.
std::shared_ptr<UltraCanvasLaTeXView> CreateLaTeXView(
    const std::string& identifier = "LaTeXView",
    float x = 0, float y = 0, float w = 0, float h = 0);

// Try to load the module (idempotent) and report whether LaTeX is usable.
bool IsLaTeXModuleAvailable();

// If the last load attempt failed, a human-readable reason; empty otherwise.
const std::string& GetLaTeXModuleError();

// Optional overrides; must be set before the first CreateLaTeXView() to take
// effect at load time.
//  - module path: explicit path to libUltraCanvasLaTeX (file or directory).
//  - font search dir: directory containing latinmodern-math.clm2 (+ .otf).
void SetLaTeXModulePath(const std::string& pathOrDir);
void SetLaTeXFontSearchDir(const std::string& dir);

// Unload the module (no-op if not loaded). Only safe once all views created
// from it have been destroyed; intended for shutdown / hot-reload scenarios.
void UnloadLaTeXModule();

} // namespace UltraCanvas
