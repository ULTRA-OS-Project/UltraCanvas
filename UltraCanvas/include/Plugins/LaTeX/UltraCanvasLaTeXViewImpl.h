// Plugins/LaTeX/UltraCanvasLaTeXViewImpl.h
// Concrete LaTeX view, living inside the on-demand LaTeX module.
//
// Implements the abstract UltraCanvas::UltraCanvasLaTeXView interface (declared
// in the core header) on top of the embedded MicroTeX engine. This header is
// module-internal - it pulls in MicroTeX and is only included by the module's
// .cpp files, never by the core or by applications.
//
// Version: 1.1.0
// Last Modified: 2026-09-08
// Author: UltraCanvas Framework
#pragma once

#ifdef ULTRACANVAS_PLUGIN_LATEX

#include "Plugins/LaTeX/UltraCanvasLaTeXView.h"
#include "Plugins/LaTeX/UltraCanvasMathEngine.h"

#include <memory>
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
    void SetDisplayStyle(bool display) override;
    bool IsDisplayStyle() const override { return displayStyle_; }
    bool IsValid() const override { return render_ != nullptr || (native_.root != nullptr && !native_.HasErrors()); }
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
    // The error message shown in place of a formula that could not be typeset.
    // Returns nullptr when there is nothing to show (no error, or no context).
    ITextLayout* EnsureErrorLayout(IRenderContext* ctx);

    std::string latex_;
    float textSize_ = 20.f;
    Color color_ = Colors::Black;
    float maxWidth_ = 0.f;
    bool displayStyle_ = true;

    microtex::Render* render_ = nullptr;      // MicroTeX result (when that engine is selected)
    MathTypesetResult native_;                // native engine result (when selected)
    bool needsReparse_ = true;
    std::string lastError_;
    // Set when the engine itself could not be initialised (math font not
    // found), together with the font-dir generation at that moment, so the
    // parse is retried once SetLaTeXFontSearchDir() changes the search path.
    bool engineInitFailed_ = false;
    unsigned engineInitFailedGeneration_ = 0;

    std::unique_ptr<ITextLayout> errorLayout_;
    std::string errorLayoutText_;   // the text errorLayout_ was built from
};

} // namespace UltraCanvas

#endif // ULTRACANVAS_PLUGIN_LATEX
