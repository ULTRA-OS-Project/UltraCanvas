// include/Plugins/LaTeX/UltraCanvasLaTeXView.h
// UltraCanvas UI element that renders a LaTeX (math) formula via the embedded
// MicroTeX engine. The formula is typeset to vector paths and drawn through the
// element's IRenderContext, so it stays crisp at any zoom and participates in
// the normal layout / transform / clip pipeline.
//
// This header is dependency-free with respect to MicroTeX: consumers only need
// UltraCanvas headers. The engine type is forward-declared and held opaquely.
//
// Version: 1.0.0
// Last Modified: 2026-06-28
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"

#include <memory>
#include <optional>
#include <string>

// Forward-declared MicroTeX render handle (no MicroTeX headers leak out).
namespace microtex { class Render; }

namespace UltraCanvas {

class UltraCanvasLaTeXView : public UltraCanvasUIElement {
public:
    UltraCanvasLaTeXView(const std::string& identifier = "LaTeXView",
                         float x = 0, float y = 0, float w = 0, float h = 0);
    ~UltraCanvasLaTeXView() override;

    // ===== CONTENT =====
    void SetLaTeX(const std::string& latex);
    const std::string& GetLaTeX() const { return latex_; }

    // ===== STYLE =====
    void SetTextSize(float pixels);
    float GetTextSize() const { return textSize_; }

    void SetTextColor(const Color& color);
    const Color& GetTextColor() const { return color_; }

    // Wrap width in pixels for inter-line math; 0 (default) = unlimited, the
    // formula keeps its intrinsic width.
    void SetMaxWidth(float pixels);

    // ===== STATUS =====
    // True once the most recent SetLaTeX produced a valid render. If false,
    // GetLastError() explains why (engine not initialised, parse error, ...).
    bool IsValid() const { return render_ != nullptr; }
    const std::string& GetLastError() const { return lastError_; }

    // ===== ENGINE / RESOURCES =====
    // Optional: point the engine at a directory containing latinmodern-math.clm2
    // (+ .otf). Must be called before the first view is rendered. Normally the
    // bundled media/microtex directory next to the executable is found
    // automatically.
    static void SetFontSearchDir(const std::string& dir);

    // ===== ELEMENT OVERRIDES =====
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    void ComputeIntrinsicSizes(const CSSLayout::LayoutContext& ctx) override;
    Size2Df MeasureOwnContent(std::optional<float> definiteContentWidth,
                              const CSSLayout::LayoutContext& ctx) override;
    void InvalidateLayout() override;

private:
    // Parse `latex_` into `render_` using the given context (for text-run
    // measurement). No-op if nothing changed. Returns true if a render exists.
    bool EnsureRender(IRenderContext* ctx);
    void ReleaseRender();

    std::string latex_;
    float textSize_ = 20.f;
    Color color_ = Colors::Black;
    float maxWidth_ = 0.f;

    microtex::Render* render_ = nullptr;
    bool needsReparse_ = true;
    std::string lastError_;
};

inline std::shared_ptr<UltraCanvasLaTeXView> CreateLaTeXView(
        const std::string& identifier = "LaTeXView",
        float x = 0, float y = 0, float w = 0, float h = 0) {
    return std::make_shared<UltraCanvasLaTeXView>(identifier, x, y, w, h);
}

} // namespace UltraCanvas
