// Plugins/LaTeX/UltraCanvasLaTeXViewImpl.cpp
// Concrete LaTeX view implementation (module-internal). See the header.
// Version: 1.0.0
// Last Modified: 2026-06-29
// Author: UltraCanvas Framework

#include "Plugins/LaTeX/UltraCanvasLaTeXViewImpl.h"

#ifdef ULTRACANVAS_PLUGIN_LATEX

#include "Plugins/LaTeX/UltraCanvasLaTeXBackend.h"

#include "microtex.h"
#include "render/render.h"

#include <algorithm>
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

// ===== engine =====

void UltraCanvasLaTeXViewImpl::ReleaseRender() {
    delete render_;
    render_ = nullptr;
}

bool UltraCanvasLaTeXViewImpl::EnsureRender(IRenderContext* ctx) {
    if (!needsReparse_) return render_ != nullptr;
    if (!ctx) return render_ != nullptr; // can't (re)parse without a context yet

    if (!EnsureLaTeXEngineInitialized()) {
        lastError_ = "LaTeX engine not initialised (math font not found)";
        ReleaseRender();
        needsReparse_ = false;
        return false;
    }

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
            static_cast<microtex::color>(color_.ToARGB()));
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

// ===== element overrides =====

void UltraCanvasLaTeXViewImpl::InvalidateLayout() {
    CSSLayout::Element::InvalidateLayout();
}

void UltraCanvasLaTeXViewImpl::ComputeIntrinsicSizes(const CSSLayout::LayoutContext& /*ctx*/) {
    IRenderContext* ctx = GetRenderContext();
    if (!EnsureRender(ctx) || !render_) {
        intrinsic.valid = true;
        intrinsic.minContentWidth = intrinsic.maxContentWidth = 0;
        intrinsic.minContentHeight = intrinsic.maxContentHeight = 0;
        return;
    }
    const float w = static_cast<float>(render_->getWidth());
    const float h = static_cast<float>(render_->getHeight());
    const float padH = static_cast<float>(GetTotalPaddingHorizontal() + GetTotalBorderHorizontal());
    const float padV = static_cast<float>(GetTotalPaddingVertical()   + GetTotalBorderVertical());

    intrinsic.valid = true;
    intrinsic.maxContentWidth  = w + padH;
    intrinsic.maxContentHeight = h + padV;
    intrinsic.minContentWidth  = w + padH;
    intrinsic.minContentHeight = h + padV;
}

Size2Df UltraCanvasLaTeXViewImpl::MeasureOwnContent(std::optional<float> /*definiteContentWidth*/,
                                                    const CSSLayout::LayoutContext& /*ctx*/) {
    IRenderContext* ctx = GetRenderContext();
    if (!EnsureRender(ctx) || !render_) return Size2Df(0.f, 0.f);
    return Size2Df(static_cast<float>(render_->getWidth()),
                   static_cast<float>(render_->getHeight()));
}

void UltraCanvasLaTeXViewImpl::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    UltraCanvasUIElement::Render(ctx, dirtyRect); // background / border

    if (!EnsureRender(ctx) || !render_) return;

    const int contentX = GetBorderLeftWidth() + GetPaddingLeft();
    const int contentY = GetBorderTopWidth()  + GetPaddingTop();

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
