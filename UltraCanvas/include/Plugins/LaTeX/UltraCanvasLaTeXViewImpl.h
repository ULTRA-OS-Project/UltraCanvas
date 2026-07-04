// Plugins/LaTeX/UltraCanvasLaTeXViewImpl.h
// Concrete LaTeX view, living inside the on-demand LaTeX module.
//
// Implements the abstract UltraCanvas::UltraCanvasLaTeXView interface (declared
// in the core header) on top of the embedded MicroTeX engine. This header is
// module-internal - it pulls in MicroTeX and is only included by the module's
// .cpp files, never by the core or by applications.
//
// Version: 1.0.0
// Last Modified: 2026-06-29
// Author: UltraCanvas Framework
#pragma once

#ifdef ULTRACANVAS_PLUGIN_LATEX

#include "Plugins/LaTeX/UltraCanvasLaTeXView.h"

#include <optional>
#include <string>

namespace microtex { class Render; }

namespace UltraCanvas {

class UltraCanvasLaTeXViewImpl : public UltraCanvasLaTeXView {
public:
    UltraCanvasLaTeXViewImpl(const std::string& identifier, float x, float y, float w, float h);
    ~UltraCanvasLaTeXViewImpl() override;

    // ===== UltraCanvasLaTeXView interface =====
    void SetLaTeX(const std::string& latex) override;
    const std::string& GetLaTeX() const override { return latex_; }
    void SetTextSize(float pixels) override;
    float GetTextSize() const override { return textSize_; }
    void SetTextColor(const Color& color) override;
    const Color& GetTextColor() const override { return color_; }
    void SetMaxWidth(float pixels) override;
    bool IsValid() const override { return render_ != nullptr; }
    const std::string& GetLastError() const override { return lastError_; }

    // ===== UltraCanvasUIElement overrides =====
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    void ComputeIntrinsicSizes(const CSSLayout::LayoutContext& ctx) override;
    Size2Df MeasureOwnContent(std::optional<float> definiteContentWidth,
                              const CSSLayout::LayoutContext& ctx) override;
    void InvalidateLayout() override;

private:
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

} // namespace UltraCanvas

#endif // ULTRACANVAS_PLUGIN_LATEX
