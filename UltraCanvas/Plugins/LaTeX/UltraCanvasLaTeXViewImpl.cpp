// Plugins/LaTeX/UltraCanvasLaTeXViewImpl.cpp
// Concrete LaTeX view implementation (module-internal). See the header.
// Version: 1.1.0
// Last Modified: 2026-09-08
// Author: UltraCanvas Framework

#include "Plugins/LaTeX/UltraCanvasLaTeXViewImpl.h"

#ifdef ULTRACANVAS_PLUGIN_LATEX

#include "Plugins/LaTeX/UltraCanvasLaTeXBackend.h"
#include "Plugins/LaTeX/UltraCanvasMathRender.h"

#include "microtex.h"
#include "render/render.h"

#include <algorithm>
#include <cmath>
#include <exception>

namespace UltraCanvas {

namespace {
std::string TrimWS(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
} // namespace

UltraCanvasLaTeXViewImpl::UltraCanvasLaTeXViewImpl(const std::string& identifier,
                                                   float x, float y, float w, float h)
    : UltraCanvasLaTeXView(identifier, x, y, w, h) {}

UltraCanvasLaTeXViewImpl::~UltraCanvasLaTeXViewImpl() { ReleaseRender(); }

// ===== content / style setters =====

void UltraCanvasLaTeXViewImpl::SetLaTeX(const std::string& latex) {
    if (latex_ == latex) return;
    latex_ = latex;
    needsReparse_ = true;
    InvalidateLayout();
    RequestRedraw();
}

void UltraCanvasLaTeXViewImpl::SetTextSize(float pixels) {
    if (pixels <= 0 || textSize_ == pixels) return;
    textSize_ = pixels;
    needsReparse_ = true;
    InvalidateLayout();
    RequestRedraw();
}

void UltraCanvasLaTeXViewImpl::SetTextColor(const Color& color) {
    color_ = color;
    needsReparse_ = true; // foreground is baked in at parse time; size unchanged
    RequestRedraw();
}

void UltraCanvasLaTeXViewImpl::SetMaxWidth(float pixels) {
    if (maxWidth_ == pixels) return;
    maxWidth_ = std::max(0.f, pixels);
    needsReparse_ = true;
    InvalidateLayout();
    RequestRedraw();
}

void UltraCanvasLaTeXViewImpl::SetDisplayStyle(bool display) {
    if (displayStyle_ == display) return;
    displayStyle_ = display;
    needsReparse_ = true;
    InvalidateLayout();
    RequestRedraw();
}

// ===== engine =====

void UltraCanvasLaTeXViewImpl::ReleaseRender() {
    delete render_;
    render_ = nullptr;
    native_ = MathTypesetResult{};
}

bool UltraCanvasLaTeXViewImpl::EnsureRender(IRenderContext* ctx) {
    // A failed engine initialisation is retried once the font search path has
    // changed (SetLaTeXFontSearchDir), so a view can recover at runtime.
    if (!needsReparse_ && engineInitFailed_ &&
        engineInitFailedGeneration_ != GetLaTeXEngineFontDirGeneration()) {
        needsReparse_ = true;
    }
    if (!needsReparse_) return render_ != nullptr || native_.root != nullptr;
    if (!ctx) return render_ != nullptr || native_.root != nullptr; // can't (re)parse without a context yet

    if (UseNativeLaTeXEngine()) {
        ReleaseRender();
        if (!EnsureNativeLaTeXEngineInitialized()) {
            lastError_ = GetNativeLaTeXEngineError();
            engineInitFailed_ = true;
            engineInitFailedGeneration_ = GetLaTeXEngineFontDirGeneration();
            needsReparse_ = false;
            return false;
        }
        engineInitFailed_ = false;
        const std::string tex = TrimWS(latex_);
        needsReparse_ = false;
        if (tex.empty()) { lastError_.clear(); return false; }
        MathContextTextFallback fallback(ctx);
        MathTypesetOptions opt;
        opt.fontSize = textSize_;
        opt.color = color_.ToARGB();
        opt.maxWidth = maxWidth_;
        opt.style = displayStyle_ ? MathStyle::Display() : MathStyle::Text();
        opt.textFallback = &fallback;
        native_ = GetSharedMathEngine().Typeset(tex, opt);
        // The formula renders even with errors; report the first one.
        lastError_ = native_.diagnostics.empty() ? std::string() : native_.diagnostics.front().message;
        return native_.root != nullptr;
    }

    if (!EnsureLaTeXEngineInitialized()) {
        lastError_ = "math font latinmodern-math.clm2 not found (engine not initialised)";
        engineInitFailed_ = true;
        engineInitFailedGeneration_ = GetLaTeXEngineFontDirGeneration();
        ReleaseRender();
        needsReparse_ = false;
        return false;
    }
    engineInitFailed_ = false;

    const std::string tex = TrimWS(latex_);
    if (tex.empty()) {
        lastError_.clear();
        ReleaseRender();
        needsReparse_ = false;
        return false;
    }

    SetLaTeXActiveContext(ctx);
    ReleaseRender();
    try {
        render_ = microtex::MicroTeX::parse(
            tex,
            maxWidth_,
            textSize_,
            textSize_ / 3.f,
            static_cast<microtex::color>(color_.ToARGB()),
            true,
            microtex::OverrideTeXStyle{true, displayStyle_ ? microtex::TexStyle::display : microtex::TexStyle::text});
        lastError_.clear();
    } catch (const std::exception& e) {
        lastError_ = e.what();
        ReleaseRender();
    } catch (...) {
        lastError_ = "Unknown LaTeX parse error";
        ReleaseRender();
    }
    needsReparse_ = false;
    return render_ != nullptr;
}

// ===== error display =====

namespace {
constexpr float kErrorFontSize = 12.f;
const Color     kErrorColor(180, 40, 40, 255);
} // namespace

ITextLayout* UltraCanvasLaTeXViewImpl::EnsureErrorLayout(IRenderContext* ctx) {
    if (render_ || native_.root || lastError_.empty()) {
        errorLayout_.reset();
        errorLayoutText_.clear();
        return nullptr;
    }
    const std::string text = "LaTeX: " + lastError_;
    if (errorLayout_ && errorLayoutText_ == text) return errorLayout_.get();
    if (!ctx) return nullptr;

    errorLayout_ = ctx->CreateTextLayout(text, /*isMarkup*/ false);
    if (!errorLayout_) return nullptr;
    UltraCanvas::FontStyle fs;
    fs.fontFamily = "Sans";
    fs.fontSize   = kErrorFontSize;
    errorLayout_->SetFontStyle(fs);
    errorLayout_->SetWrap(TextWrap::WrapWordChar);
    errorLayoutText_ = text;
    return errorLayout_.get();
}

// ===== element overrides =====

void UltraCanvasLaTeXViewImpl::InvalidateLayout() {
    CSSLayout::Element::InvalidateLayout();
}

void UltraCanvasLaTeXViewImpl::ComputeIntrinsicSizes(const CSSLayout::LayoutContext& lctx) {
    const Size2Df content = MeasureOwnContent(std::nullopt, lctx);
    const float padH = static_cast<float>(GetTotalPaddingHorizontal() + GetTotalBorderHorizontal());
    const float padV = static_cast<float>(GetTotalPaddingVertical()   + GetTotalBorderVertical());

    intrinsic.valid = true;
    intrinsic.maxContentWidth  = content.width  + padH;
    intrinsic.maxContentHeight = content.height + padV;
    intrinsic.minContentWidth  = content.width  + padH;
    intrinsic.minContentHeight = content.height + padV;
}

Size2Df UltraCanvasLaTeXViewImpl::MeasureOwnContent(std::optional<float> /*definiteContentWidth*/,
                                                    const CSSLayout::LayoutContext& /*ctx*/) {
    IRenderContext* ctx = GetRenderContext();
    if (EnsureRender(ctx) && render_) {
        return Size2Df(static_cast<float>(render_->getWidth()),
                       static_cast<float>(render_->getHeight()));
    }
    if (native_.root) {
        return Size2Df(std::ceil(native_.width), std::ceil(native_.TotalHeight()));
    }
    // No render: size to the error message so it is laid out (a 0x0 element
    // is culled by its container and would never get to draw the message).
    if (ITextLayout* err = EnsureErrorLayout(ctx)) {
        err->SetExplicitWidth(-1);
        return Size2Df(static_cast<float>(err->GetLayoutWidth()),
                       static_cast<float>(err->GetLayoutHeight()));
    }
    return Size2Df(0.f, 0.f);
}

void UltraCanvasLaTeXViewImpl::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    UltraCanvasUIElement::Render(ctx, dirtyRect); // background / border

    const int contentX = GetBorderLeftWidth() + GetPaddingLeft();
    const int contentY = GetBorderTopWidth()  + GetPaddingTop();

    EnsureRender(ctx);
    if (native_.root) {
        ctx->PushState();
        {
            const Rect2Df crect = GetLocalContentRect();
            if (crect.width > 0 && crect.height > 0) ctx->ClipRect(crect);
            DrawMathBox(ctx, *native_.root, contentX, contentY + native_.height, color_);
        }
        ctx->PopState();
        return;
    }
    if (!render_) {
        // Nothing could be typeset: show the reason instead of a blank box.
        // The element is the LaTeX view itself painting its own content.
        if (ITextLayout* err = EnsureErrorLayout(ctx)) {
            ctx->PushState();
            ctx->SetTextPaint(kErrorColor);
            ctx->DrawTextLayout(*err, Point2Dd(contentX, contentY));
            ctx->PopState();
        }
        return;
    }

    SetLaTeXActiveContext(ctx);
    ctx->PushState();
    {
        const Rect2Df crect = GetLocalContentRect();
        if (crect.width > 0 && crect.height > 0) ctx->ClipRect(crect);

        Graphics2D_ultracanvas g2(ctx);
        render_->draw(g2, contentX, contentY);
    }
    ctx->PopState();
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_PLUGIN_LATEX
