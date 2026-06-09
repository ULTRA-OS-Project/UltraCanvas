// core/UltraCanvasSlideshow.cpp
// Timed image slideshow with selectable info-panel layouts and indicator styles.
// Version: 1.3.0
// Last Modified: 2026-06-09
// Author: UltraCanvas Framework

#include "UltraCanvasSlideshow.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasImage.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace UltraCanvas {

    namespace {
        constexpr float kMinTransitionMs = 16.0f;

        // Smoothstep easing for opacity / slide offsets — looks better than linear.
        float Smoothstep(float t) {
            t = std::max(0.0f, std::min(1.0f, t));
            return t * t * (3.0f - 2.0f * t);
        }
    }

    UltraCanvasSlideshow::UltraCanvasSlideshow(const std::string& identifier,
                                               float x, float y, float w, float h)
            : UltraCanvasContainer(identifier, x, y, w, h) {
        SetMouseCursor(UCMouseCursor::Default);
        SetBackgroundColor(config.backgroundColor);
        slideStartedAt = std::chrono::steady_clock::now();
    }

    UltraCanvasSlideshow::~UltraCanvasSlideshow() {
        CancelTimers();
    }

    // ===== CONFIGURATION =====
    void UltraCanvasSlideshow::SetConfig(const SlideshowConfig& cfg) {
        bool wasPlaying = playing;
        if (wasPlaying) Pause();
        config = cfg;
        SetBackgroundColor(config.backgroundColor);
        RequestRedraw();
        if (wasPlaying && config.autoPlay) Play();
    }

    void UltraCanvasSlideshow::SetIndicatorStyle(const SlideshowIndicatorStyle& style) {
        config.indicators = style;
        UpdateRedrawTimer();
        RequestRedraw();
    }

    void UltraCanvasSlideshow::SetStaticText(const std::string& title, const std::string& body) {
        staticTitle = title;
        staticBody = body;
        RequestRedraw();
    }

    // ===== SLIDE MANAGEMENT =====
    void UltraCanvasSlideshow::AddSlide(const SlideshowSlide& slide) {
        slides.push_back(slide);
        RequestRedraw();
    }

    void UltraCanvasSlideshow::ClearSlides() {
        CancelTimers();
        slides.clear();
        currentIndex = previousIndex = 0;
        transitioning = false;
        transitionProgress = 0.0f;
        slideElapsed = 0.0f;
        playing = false;
        RequestRedraw();
    }

    // ===== PLAYBACK =====
    void UltraCanvasSlideshow::Play() {
        if (slides.size() <= 1) return;
        if (playing) return;
        playing = true;
        slideStartedAt = std::chrono::steady_clock::now();
        ScheduleNextAdvance();
        UpdateRedrawTimer();
    }

    void UltraCanvasSlideshow::Pause() {
        playing = false;
        CancelTimers();
    }

    void UltraCanvasSlideshow::Next() {
        if (slides.size() <= 1) return;
        size_t next = currentIndex + 1;
        if (next >= slides.size()) {
            if (!config.loop) return;
            next = 0;
        }
        StartTransitionTo(next);
    }

    void UltraCanvasSlideshow::Previous() {
        if (slides.size() <= 1) return;
        size_t prev;
        if (currentIndex == 0) {
            if (!config.loop) return;
            prev = slides.size() - 1;
        } else {
            prev = currentIndex - 1;
        }
        StartTransitionTo(prev);
    }

    void UltraCanvasSlideshow::GoTo(size_t index, bool animated) {
        if (index >= slides.size() || index == currentIndex) return;
        if (animated) {
            StartTransitionTo(index);
        } else {
            previousIndex = currentIndex;
            currentIndex = index;
            transitioning = false;
            transitionProgress = 0.0f;
            slideElapsed = 0.0f;
            slideStartedAt = std::chrono::steady_clock::now();
            if (onSlideChanged) onSlideChanged(currentIndex);
            if (playing) ScheduleNextAdvance();
            RequestRedraw();
        }
    }

    // ===== TIMERS =====
    void UltraCanvasSlideshow::ScheduleNextAdvance() {
        auto* app = UltraCanvasApplication::GetInstance();
        if (!app) return;
        if (advanceTimerId) {
            app->StopTimer(advanceTimerId);
            advanceTimerId = 0;
        }
        unsigned int ms = static_cast<unsigned int>(config.slideDuration.count());
        if (ms < 50) ms = 50;
        advanceTimerId = app->StartTimer(ms, false, [this](TimerId) {
            advanceTimerId = 0;
            OnAdvanceTick();
        });
    }

    void UltraCanvasSlideshow::CancelTimers() {
        auto* app = UltraCanvasApplication::GetInstance();
        if (!app) {
            animationTimerId = advanceTimerId = redrawTimerId = 0;
            return;
        }
        if (animationTimerId) {
            app->StopTimer(animationTimerId);
            animationTimerId = 0;
        }
        if (advanceTimerId) {
            app->StopTimer(advanceTimerId);
            advanceTimerId = 0;
        }
        if (redrawTimerId) {
            app->StopTimer(redrawTimerId);
            redrawTimerId = 0;
        }
    }

    bool UltraCanvasSlideshow::IndicatorNeedsContinuousRedraw() const {
        switch (config.indicators.shape) {
            case SlideshowIndicatorShape::ProgressBar:
            case SlideshowIndicatorShape::StoryBars:
                return true;
            default:
                return false;
        }
    }

    void UltraCanvasSlideshow::UpdateRedrawTimer() {
        // The continuous indicator styles need ~20fps redraws while a slide is shown,
        // so the bar visibly fills. We don't need this during a transition because
        // the animation timer is already requesting redraws.
        auto* app = UltraCanvasApplication::GetInstance();
        if (!app) return;
        bool wantTimer = playing && !transitioning && IndicatorNeedsContinuousRedraw();
        if (wantTimer && !redrawTimerId) {
            redrawTimerId = app->StartTimer(50, true, [this](TimerId) { OnRedrawTick(); });
        } else if (!wantTimer && redrawTimerId) {
            app->StopTimer(redrawTimerId);
            redrawTimerId = 0;
        }
    }

    void UltraCanvasSlideshow::OnRedrawTick() {
        if (!IsVisible()) return;
        RequestRedraw();
    }

    void UltraCanvasSlideshow::StartTransitionTo(size_t newIndex) {
        if (newIndex == currentIndex || newIndex >= slides.size()) return;

        previousIndex = currentIndex;
        currentIndex = newIndex;

        if (config.fadeStyle == SlideshowFadeStyle::NoFade ||
            config.fadeDuration.count() < (long)kMinTransitionMs) {
            transitioning = false;
            transitionProgress = 1.0f;
            FinishTransition();
            return;
        }

        transitioning = true;
        transitionProgress = 0.0f;
        transitionStartedAt = std::chrono::steady_clock::now();

        auto* app = UltraCanvasApplication::GetInstance();
        if (!app) return;
        if (advanceTimerId) { app->StopTimer(advanceTimerId); advanceTimerId = 0; }
        if (animationTimerId) { app->StopTimer(animationTimerId); animationTimerId = 0; }

        int fps = std::max(15, std::min(120, config.animationFps));
        unsigned int frameMs = static_cast<unsigned int>(1000 / fps);
        animationTimerId = app->StartTimer(frameMs, true, [this](TimerId) {
            OnAnimationTick();
        });
        // The slow redraw timer isn't needed during a transition — the animation
        // timer already drives 60fps repaints.
        UpdateRedrawTimer();
        RequestRedraw();
    }

    void UltraCanvasSlideshow::OnAnimationTick() {
        if (!transitioning) return;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - transitionStartedAt).count();
        float dur = std::max(1.0f, static_cast<float>(config.fadeDuration.count()));
        transitionProgress = std::min(1.0f, static_cast<float>(elapsed) / dur);
        if (transitionProgress >= 1.0f) {
            FinishTransition();
        } else {
            RequestRedraw();
        }
    }

    void UltraCanvasSlideshow::FinishTransition() {
        transitioning = false;
        transitionProgress = 1.0f;
        slideElapsed = 0.0f;
        slideStartedAt = std::chrono::steady_clock::now();

        auto* app = UltraCanvasApplication::GetInstance();
        if (app && animationTimerId) {
            app->StopTimer(animationTimerId);
            animationTimerId = 0;
        }

        if (onSlideChanged) onSlideChanged(currentIndex);
        RequestRedraw();
        if (playing) ScheduleNextAdvance();
        UpdateRedrawTimer();
    }

    void UltraCanvasSlideshow::OnAdvanceTick() {
        if (!playing) return;
        if (hovered && config.pauseOnHover) {
            // User is hovering — defer the next switch by one full slot.
            ScheduleNextAdvance();
            return;
        }
        Next();
    }

    // ===== GEOMETRY =====
    Rect2Di UltraCanvasSlideshow::GetImagePanelRect() const {
        auto bnds = GetLocalBounds();
        const int ox = bnds.x, oy = bnds.y, W = bnds.width, H = bnds.height;
        const auto layout = config.infoLayout;

        // Overlay layouts and Hidden draw a full-bleed image.
        if (layout == SlideshowInfoLayout::Hidden || SlideshowLayoutIsOverlay(layout)) {
            return Rect2Di(ox, oy, W, H);
        }

        // Split layouts: a ratio of ~1 means there is effectively no panel.
        float r = std::max(0.0f, std::min(1.0f, config.imagePanelRatio));
        if (r >= 0.999f) return Rect2Di(ox, oy, W, H);

        switch (layout) {
            case SlideshowInfoLayout::SplitRight: {
                int imgW = static_cast<int>(W * r);
                return Rect2Di(ox, oy, imgW, H);
            }
            case SlideshowInfoLayout::SplitLeft: {
                int imgW = static_cast<int>(W * r);
                return Rect2Di(ox + W - imgW, oy, imgW, H);
            }
            case SlideshowInfoLayout::SplitBottom: {
                int imgH = static_cast<int>(H * r);
                return Rect2Di(ox, oy, W, imgH);
            }
            case SlideshowInfoLayout::SplitTop: {
                int imgH = static_cast<int>(H * r);
                return Rect2Di(ox, oy + H - imgH, W, imgH);
            }
            default:
                return Rect2Di(ox, oy, W, H);
        }
    }

    Rect2Di UltraCanvasSlideshow::GetInfoPanelRect() const {
        auto bnds = GetLocalBounds();
        const int ox = bnds.x, oy = bnds.y, W = bnds.width, H = bnds.height;
        const auto layout = config.infoLayout;

        if (layout == SlideshowInfoLayout::Hidden) return Rect2Di(0, 0, 0, 0);

        // Split: the panel is the complement of the image region.
        if (SlideshowLayoutIsSplit(layout)) {
            float r = std::max(0.0f, std::min(1.0f, config.imagePanelRatio));
            if (r >= 0.999f) return Rect2Di(0, 0, 0, 0);
            switch (layout) {
                case SlideshowInfoLayout::SplitRight: {
                    int imgW = static_cast<int>(W * r);
                    return Rect2Di(ox + imgW, oy, W - imgW, H);
                }
                case SlideshowInfoLayout::SplitLeft: {
                    int imgW = static_cast<int>(W * r);
                    return Rect2Di(ox, oy, W - imgW, H);
                }
                case SlideshowInfoLayout::SplitBottom: {
                    int imgH = static_cast<int>(H * r);
                    return Rect2Di(ox, oy + imgH, W, H - imgH);
                }
                case SlideshowInfoLayout::SplitTop: {
                    int imgH = static_cast<int>(H * r);
                    return Rect2Di(ox, oy, W, H - imgH);
                }
                default: return Rect2Di(0, 0, 0, 0);
            }
        }

        // Edge overlays span one whole side of the image.
        float es = std::max(0.05f, std::min(1.0f, config.overlaySizeRatio));
        switch (layout) {
            case SlideshowInfoLayout::OverlayFull:
                return Rect2Di(ox, oy, W, H);
            case SlideshowInfoLayout::OverlayLeft: {
                int w = static_cast<int>(W * es);
                return Rect2Di(ox, oy, w, H);
            }
            case SlideshowInfoLayout::OverlayRight: {
                int w = static_cast<int>(W * es);
                return Rect2Di(ox + W - w, oy, w, H);
            }
            case SlideshowInfoLayout::OverlayTop: {
                int h = static_cast<int>(H * es);
                return Rect2Di(ox, oy, W, h);
            }
            case SlideshowInfoLayout::OverlayBottom: {
                int h = static_cast<int>(H * es);
                return Rect2Di(ox, oy + H - h, W, h);
            }
            default:
                break;  // corner / center fall through
        }

        // Corner / center floating box.
        int m = std::max(0, config.overlayMargin);
        int bw = std::max(40, static_cast<int>(W * config.overlayBoxWidthRatio));
        int bh = std::max(30, static_cast<int>(H * config.overlayBoxHeightRatio));
        bw = std::min(bw, std::max(1, W - 2 * m));
        bh = std::min(bh, std::max(1, H - 2 * m));
        int x = ox + m, y = oy + m;
        switch (layout) {
            case SlideshowInfoLayout::OverlayTopLeft:     x = ox + m;            y = oy + m;            break;
            case SlideshowInfoLayout::OverlayTopRight:    x = ox + W - m - bw;   y = oy + m;            break;
            case SlideshowInfoLayout::OverlayBottomLeft:  x = ox + m;            y = oy + H - m - bh;   break;
            case SlideshowInfoLayout::OverlayBottomRight: x = ox + W - m - bw;   y = oy + H - m - bh;   break;
            case SlideshowInfoLayout::OverlayCenter:      x = ox + (W - bw) / 2; y = oy + (H - bh) / 2; break;
            default: break;
        }
        return Rect2Di(x, y, bw, bh);
    }

    SlideshowIndicatorEdge UltraCanvasSlideshow::EffectiveIndicatorEdge() const {
        const auto& s = config.indicators;
        // Back-compat: if `edge` is left at its Bottom default but the legacy
        // `vertical` field asks for Top, honor the old field.
        if (s.edge == SlideshowIndicatorEdge::Bottom &&
            s.vertical == VerticalAlignment::Top) {
            return SlideshowIndicatorEdge::Top;
        }
        return s.edge;
    }

    bool UltraCanvasSlideshow::IndicatorIsVertical() const {
        auto e = EffectiveIndicatorEdge();
        return e == SlideshowIndicatorEdge::Left || e == SlideshowIndicatorEdge::Right;
    }

    Rect2Di UltraCanvasSlideshow::GetIndicatorPanelRect() const {
        Rect2Di imgRect = GetImagePanelRect();
        const auto& s = config.indicators;
        if (s.shape == SlideshowIndicatorShape::Hidden) {
            return Rect2Di(0, 0, 0, 0);
        }

        const bool vert = IndicatorIsVertical();
        const int margin = s.marginFromEdge;

        // Content extent across the strip (the thin dimension).
        float cross;
        switch (s.shape) {
            case SlideshowIndicatorShape::Dots:
                cross = std::max(s.itemHeight, 6.0f);
                break;
            case SlideshowIndicatorShape::Thumbnails:
                cross = (vert ? std::max(s.itemWidth, 32.0f)
                              : std::max(s.itemHeight, 20.0f)) + 4.0f;
                break;
            case SlideshowIndicatorShape::Counter:
                cross = vert ? 48.0f : 18.0f;
                break;
            case SlideshowIndicatorShape::Labels:
                cross = vert ? std::max(s.itemWidth, 80.0f) : 18.0f;
                break;
            default:  // Bars, ProgressBar, StoryBars
                cross = std::max(s.itemHeight, 3.0f);
                break;
        }
        int contentCross = static_cast<int>(cross);
        auto edge = EffectiveIndicatorEdge();

        if (!vert) {
            // Horizontal strip along the top or bottom of the image.
            int y = (edge == SlideshowIndicatorEdge::Top)
                    ? imgRect.y + margin
                    : imgRect.y + imgRect.height - margin - contentCross;
            return Rect2Di(imgRect.x + 8, y, imgRect.width - 16, contentCross);
        }
        // Vertical strip along the left or right of the image.
        int x = (edge == SlideshowIndicatorEdge::Left)
                ? imgRect.x + margin
                : imgRect.x + imgRect.width - margin - contentCross;
        return Rect2Di(x, imgRect.y + 8, contentCross, imgRect.height - 16);
    }

    // ===== RENDER =====
    void UltraCanvasSlideshow::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        // Container background + borders via base class
        UltraCanvasUIElement::Render(ctx, dirtyRect);

        if (slides.empty()) {
            ctx->SetTextPaint(Color(180, 180, 180));
            ctx->SetFontSize(14.0f);
            ctx->DrawTextInRect("(no slides)", Rect2Dd(GetLocalBounds()));
            return;
        }

        // Update continuous progress for ProgressBar / StoryBars
        if (elapsedFrozen) {
            slideElapsed = frozenElapsed;
        } else {
            auto now = std::chrono::steady_clock::now();
            auto sinceStart = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - slideStartedAt).count();
            float dur = std::max(1.0f, static_cast<float>(config.slideDuration.count()));
            slideElapsed = std::min(1.0f, static_cast<float>(sinceStart) / dur);
        }

        ctx->PushState();
        ctx->ClipRect(Rect2Dd(GetLocalBounds()));
        DrawBackground(ctx);
        DrawSlideImages(ctx);
        DrawInfoPanel(ctx);
        DrawIndicators(ctx);
        ctx->PopState();
    }

    void UltraCanvasSlideshow::DrawBackground(IRenderContext* ctx) {
        Rect2Di img = GetImagePanelRect();
        ctx->SetFillPaint(config.backgroundColor);
        ctx->FillRectangle(Rect2Dd(img));
    }

    void UltraCanvasSlideshow::DrawGapFill(IRenderContext* ctx, size_t slideIdx,
                                           const Rect2Dd& rect, float alpha) {
        ctx->PushState();
        ctx->ClipRect(rect);
        ctx->SetAlpha(std::max(0.0f, std::min(1.0f, alpha)));
        switch (config.gapFill) {
            case SlideshowGapFill::LetterboxColor:
                ctx->SetFillPaint(config.letterboxColor);
                ctx->FillRectangle(rect);
                break;
            case SlideshowGapFill::BlurredImage: {
                // Zoomed copy of the same image fills the gaps; a translucent
                // scrim softens it so it reads as a backdrop, not a second photo.
                if (slideIdx < slides.size()) {
                    auto img = UCImage::Get(slides[slideIdx].imagePath);
                    if (img) ctx->DrawImage(*img, rect, ImageFitMode::Cover);
                }
                ctx->SetFillPaint(Color(0, 0, 0, 120));
                ctx->FillRectangle(rect);
                break;
            }
            case SlideshowGapFill::BackgroundColor:
            default:
                ctx->SetFillPaint(config.backgroundColor);
                ctx->FillRectangle(rect);
                break;
        }
        ctx->PopState();
    }

    void UltraCanvasSlideshow::DrawSlideAt(IRenderContext* ctx, size_t slideIdx,
                                           const Rect2Dd& rect, float alpha,
                                           float offsetX, float offsetY, float scale) {
        if (slideIdx >= slides.size()) return;
        const auto& slide = slides[slideIdx];
        if (slide.imagePath.empty()) return;
        auto img = UCImage::Get(slide.imagePath);
        if (!img) return;

        int iw = img->GetWidth();
        int ih = img->GetHeight();
        const double rw = rect.width, rh = rect.height;

        // Base destination size for the chosen fit (aspect preserved except Fill).
        double dstW = rw, dstH = rh;
        bool leavesGaps = false;
        if (iw > 0 && ih > 0 && rw > 0 && rh > 0) {
            switch (config.imageFit) {
                case ImageFitMode::Cover: {
                    double s = std::max(rw / iw, rh / ih);
                    dstW = iw * s; dstH = ih * s;
                    break;
                }
                case ImageFitMode::Contain: {
                    double s = std::min(rw / iw, rh / ih);
                    dstW = iw * s; dstH = ih * s;
                    leavesGaps = true;
                    break;
                }
                case ImageFitMode::ScaleDown: {
                    double s = std::min(1.0, std::min(rw / iw, rh / ih));
                    dstW = iw * s; dstH = ih * s;
                    leavesGaps = true;
                    break;
                }
                case ImageFitMode::NoScale: {
                    dstW = iw; dstH = ih;
                    leavesGaps = true;
                    break;
                }
                case ImageFitMode::Fill:
                default:
                    dstW = rw; dstH = rh;   // stretch to the area
                    break;
            }
        }

        // Place by focal point. With this formula fx=0 keeps the left/top edge
        // (cropping the opposite side on overflow, left-aligning on underflow),
        // fx=1 keeps the right/bottom edge, 0.5 centers — for both cases.
        double fx = std::max(0.0f, std::min(1.0f, config.imageFocus.x));
        double fy = std::max(0.0f, std::min(1.0f, config.imageFocus.y));
        double dstX = rect.x + (rw - dstW) * fx;
        double dstY = rect.y + (rh - dstH) * fy;

        // Transition zoom (around the destination center) and slide translation.
        if (scale != 1.0f) {
            double dw = dstW * (scale - 1.0);
            double dh = dstH * (scale - 1.0);
            dstX -= dw / 2.0; dstY -= dh / 2.0;
            dstW += dw;       dstH += dh;
        }
        dstX += offsetX;
        dstY += offsetY;

        ctx->PushState();
        ctx->ClipRect(rect);
        if (leavesGaps) DrawGapFill(ctx, slideIdx, rect, alpha);
        ctx->SetAlpha(std::max(0.0f, std::min(1.0f, alpha)));
        // dst already encodes the aspect-correct size, so Fill maps the image to
        // it exactly without further distortion (and genuinely stretches for the
        // Fill fit mode, where dst == rect).
        ctx->DrawImage(*img, Rect2Dd(dstX, dstY, dstW, dstH), ImageFitMode::Fill);
        ctx->PopState();
    }

    void UltraCanvasSlideshow::DrawSlideImages(IRenderContext* ctx) {
        Rect2Dd imgRect = Rect2Dd(GetImagePanelRect());
        float p = Smoothstep(transitionProgress);

        if (!transitioning) {
            DrawSlideAt(ctx, currentIndex, imgRect, 1.0f, 0.0f);
            return;
        }

        switch (config.fadeStyle) {
            case SlideshowFadeStyle::NoFade: {
                DrawSlideAt(ctx, currentIndex, imgRect, 1.0f, 0.0f);
                break;
            }
            case SlideshowFadeStyle::CrossFade: {
                DrawSlideAt(ctx, previousIndex, imgRect, 1.0f - p, 0.0f);
                DrawSlideAt(ctx, currentIndex,  imgRect, p,        0.0f);
                break;
            }
            case SlideshowFadeStyle::FadeOutIn: {
                if (p < 0.5f) {
                    float a = 1.0f - (p * 2.0f);
                    DrawSlideAt(ctx, previousIndex, imgRect, a, 0.0f);
                } else {
                    float a = (p - 0.5f) * 2.0f;
                    DrawSlideAt(ctx, currentIndex, imgRect, a, 0.0f);
                }
                break;
            }
            case SlideshowFadeStyle::SlideHorizontal: {
                float w = imgRect.width;
                DrawSlideAt(ctx, previousIndex, imgRect, 1.0f, -p * w);
                DrawSlideAt(ctx, currentIndex,  imgRect, 1.0f, (1.0f - p) * w);
                break;
            }
            case SlideshowFadeStyle::SlideVertical: {
                float h = imgRect.height;
                DrawSlideAt(ctx, previousIndex, imgRect, 1.0f, 0.0f, -p * h);
                DrawSlideAt(ctx, currentIndex,  imgRect, 1.0f, 0.0f, (1.0f - p) * h);
                break;
            }
            case SlideshowFadeStyle::ZoomFade: {
                // Previous holds steady and fades out; new zooms in from slightly
                // enlarged while fading in.
                DrawSlideAt(ctx, previousIndex, imgRect, 1.0f - p, 0.0f, 0.0f, 1.0f);
                float scale = 1.08f - 0.08f * p;
                DrawSlideAt(ctx, currentIndex,  imgRect, p, 0.0f, 0.0f, scale);
                break;
            }
        }
    }

    void UltraCanvasSlideshow::DrawInfoPanel(IRenderContext* ctx) {
        Rect2Di info = GetInfoPanelRect();
        if (info.width <= 0 || info.height <= 0) return;

        // Split layouts paint an opaque panel; overlays paint a translucent scrim
        // so the image stays visible behind the text.
        bool overlay = SlideshowLayoutIsOverlay(config.infoLayout);
        ctx->SetFillPaint(overlay ? config.overlayScrimColor : config.infoPanelColor);
        ctx->FillRectangle(Rect2Dd(info));

        // Choose source text
        std::string title;
        std::string body;
        if (config.textMode == SlideshowTextMode::Static) {
            title = staticTitle;
            body  = staticBody;
        } else {
            // PerSlide: while transitioning, fade the body opacity
            const auto& cur = slides[currentIndex];
            title = cur.infoTitle;
            body  = cur.infoBody;
        }

        float textAlpha = 1.0f;
        if (transitioning && config.textMode == SlideshowTextMode::PerSlide) {
            // Two-phase fade: previous text fades out in first half, new fades in second
            float p = Smoothstep(transitionProgress);
            if (p < 0.5f) {
                textAlpha = 1.0f - p * 2.0f;
                const auto& prev = slides[previousIndex];
                title = prev.infoTitle;
                body  = prev.infoBody;
            } else {
                textAlpha = (p - 0.5f) * 2.0f;
            }
        }

        int pad = config.infoPadding;
        Rect2Di textArea(info.x + pad, info.y + pad,
                         info.width - pad * 2, info.height - pad * 2);

        ctx->PushState();
        ctx->SetAlpha(std::max(0.0f, std::min(1.0f, textAlpha)));

        // Title
        if (!title.empty()) {
            ctx->SetTextPaint(config.titleColor);
            FontStyle titleFs;
            titleFs.fontFamily = config.fontFamily;
            titleFs.fontSize = config.titleFontSize;
            titleFs.fontWeight = FontWeight::Bold;
            ctx->SetFontStyle(titleFs);
            ctx->SetTextWrap(TextWrap::WrapWord);
            int titleH = static_cast<int>(config.titleFontSize * 2.5f);
            Rect2Di titleRect(textArea.x, textArea.y, textArea.width, titleH);
            ctx->DrawTextInRect(title, Rect2Dd(titleRect));
            textArea.y += titleH + 8;
            textArea.height -= titleH + 8;
        }

        // Body
        if (!body.empty() && textArea.height > 0) {
            ctx->SetTextPaint(config.bodyColor);
            FontStyle bodyFs;
            bodyFs.fontFamily = config.fontFamily;
            bodyFs.fontSize = config.bodyFontSize;
            ctx->SetFontStyle(bodyFs);
            ctx->SetTextWrap(TextWrap::WrapWord);
            ctx->DrawTextInRect(body, Rect2Dd(textArea));
        }

        ctx->PopState();
    }

    // ===== INDICATORS =====
    void UltraCanvasSlideshow::DrawIndicators(IRenderContext* ctx) {
        indicatorHitRects.clear();
        const auto& s = config.indicators;
        if (s.shape == SlideshowIndicatorShape::Hidden || slides.empty()) return;

        Rect2Di panel = GetIndicatorPanelRect();
        if (panel.width <= 0 || panel.height <= 0) return;

        switch (s.shape) {
            case SlideshowIndicatorShape::Hidden: return;
            case SlideshowIndicatorShape::Dots:        DrawIndicatorsDots(ctx, panel); break;
            case SlideshowIndicatorShape::Bars:        DrawIndicatorsBars(ctx, panel); break;
            case SlideshowIndicatorShape::ProgressBar: DrawIndicatorsProgressBar(ctx, panel); break;
            case SlideshowIndicatorShape::StoryBars:   DrawIndicatorsStoryBars(ctx, panel); break;
            case SlideshowIndicatorShape::Counter:     DrawIndicatorsCounter(ctx, panel); break;
            case SlideshowIndicatorShape::Thumbnails:  DrawIndicatorsThumbnails(ctx, panel); break;
            case SlideshowIndicatorShape::Labels:      DrawIndicatorsLabels(ctx, panel); break;
        }
    }

    namespace {
        // Orientation-aware placement inside an indicator strip. For a Top/Bottom
        // strip the "main" axis is X and the "cross" axis is Y; for a Left/Right
        // strip they swap, so the same renderer code lays items out as a column.
        struct StripAxis {
            bool vertical;
            Rect2Di panel;
            int MainSize() const { return vertical ? panel.height : panel.width; }
            // Rect for an item `mainLen` long along the strip and `crossLen` thick,
            // starting `mainStart` into the strip and centered on the cross axis.
            Rect2Di Cell(int mainStart, int mainLen, int crossLen) const {
                if (vertical) {
                    int x = panel.x + (panel.width - crossLen) / 2;
                    return Rect2Di(x, panel.y + mainStart, crossLen, mainLen);
                }
                int y = panel.y + (panel.height - crossLen) / 2;
                return Rect2Di(panel.x + mainStart, y, mainLen, crossLen);
            }
        };

        // Start offset along the main axis for a run of `total` length given the
        // alignment field. For a vertical strip Left maps to top, Right to bottom.
        int StripStart(int mainSize, int total, TextAlignment align) {
            switch (align) {
                case TextAlignment::Left:   return 0;
                case TextAlignment::Right:  return mainSize - total;
                case TextAlignment::Center:
                case TextAlignment::Justify:
                default:                    return (mainSize - total) / 2;
            }
        }

        // Expand a hit rect on the cross axis so thin indicators are easy to click.
        Rect2Di ExpandHit(const Rect2Di& r, bool vertical, int pad) {
            if (vertical) return Rect2Di(r.x - pad, r.y, r.width + pad * 2, r.height);
            return Rect2Di(r.x, r.y - pad, r.width, r.height + pad * 2);
        }
    }

    void UltraCanvasSlideshow::DrawIndicatorsDots(IRenderContext* ctx, const Rect2Di& panel) {
        const auto& s = config.indicators;
        int count = static_cast<int>(slides.size());
        StripAxis ax{IndicatorIsVertical(), panel};
        int diameter = static_cast<int>(std::max(s.itemHeight, 6.0f));
        float radius = diameter / 2.0f;
        int step = diameter + static_cast<int>(s.spacing);
        int total = count * diameter + (count - 1) * static_cast<int>(s.spacing);
        int start = StripStart(ax.MainSize(), total, s.horizontal);

        for (int i = 0; i < count; ++i) {
            Rect2Di cell = ax.Cell(start + i * step, diameter, diameter);
            indicatorHitRects.push_back(cell);

            Color fill = (i == static_cast<int>(currentIndex)) ? s.activeColor : s.inactiveColor;
            if (s.hoverHighlight && i == hoveredIndicator && i != static_cast<int>(currentIndex)) {
                fill = s.hoverColor;
            }
            Point2Dd center(cell.x + cell.width / 2.0f, cell.y + cell.height / 2.0f);
            ctx->DrawFilledCircle(center, radius, fill);
        }
    }

    void UltraCanvasSlideshow::DrawIndicatorsBars(IRenderContext* ctx, const Rect2Di& panel) {
        const auto& s = config.indicators;
        int count = static_cast<int>(slides.size());
        bool vert = IndicatorIsVertical();
        StripAxis ax{vert, panel};
        int mainLen  = static_cast<int>(s.itemWidth);   // bar length along the strip
        int crossLen = static_cast<int>(s.itemHeight);  // bar thickness
        int step = mainLen + static_cast<int>(s.spacing);
        int total = count * mainLen + (count - 1) * static_cast<int>(s.spacing);
        int start = StripStart(ax.MainSize(), total, s.horizontal);

        for (int i = 0; i < count; ++i) {
            Rect2Di r = ax.Cell(start + i * step, mainLen, crossLen);
            indicatorHitRects.push_back(ExpandHit(r, vert, 6));

            Color fill = (i == static_cast<int>(currentIndex)) ? s.activeColor : s.inactiveColor;
            if (s.hoverHighlight && i == hoveredIndicator && i != static_cast<int>(currentIndex)) {
                fill = s.hoverColor;
            }
            ctx->DrawFilledRectangle(Rect2Dd(r), fill, 0.0f, Colors::Transparent, s.cornerRadius);
        }
    }

    void UltraCanvasSlideshow::DrawIndicatorsProgressBar(IRenderContext* ctx, const Rect2Di& panel) {
        const auto& s = config.indicators;
        bool vert = IndicatorIsVertical();
        StripAxis ax{vert, panel};
        int crossLen = static_cast<int>(s.itemHeight);
        int mainSize = ax.MainSize();

        // Single bar spanning the strip; one hit rect for "click to next".
        Rect2Di full = ax.Cell(0, mainSize, crossLen);
        ctx->DrawFilledRectangle(Rect2Dd(full), s.inactiveColor, 0.0f, Colors::Transparent, s.cornerRadius);

        float progress = transitioning ? Smoothstep(transitionProgress) : slideElapsed;
        int filledMain = std::max(0, std::min(mainSize, static_cast<int>(mainSize * progress)));
        Rect2Di filled = ax.Cell(0, filledMain, crossLen);
        ctx->DrawFilledRectangle(Rect2Dd(filled), s.activeColor, 0.0f, Colors::Transparent, s.cornerRadius);

        indicatorHitRects.push_back(ExpandHit(full, vert, 6));
    }

    void UltraCanvasSlideshow::DrawIndicatorsStoryBars(IRenderContext* ctx, const Rect2Di& panel) {
        const auto& s = config.indicators;
        int count = static_cast<int>(slides.size());
        if (count <= 0) return;
        bool vert = IndicatorIsVertical();
        StripAxis ax{vert, panel};
        int crossLen = static_cast<int>(s.itemHeight);
        // Each bar takes an equal share of the strip's main axis.
        int totalSpacing = (count - 1) * static_cast<int>(s.spacing);
        int barMain = std::max(1, (ax.MainSize() - totalSpacing) / count);

        for (int i = 0; i < count; ++i) {
            int mainStart = i * (barMain + static_cast<int>(s.spacing));
            Rect2Di r = ax.Cell(mainStart, barMain, crossLen);
            indicatorHitRects.push_back(ExpandHit(r, vert, 6));

            // Background
            ctx->DrawFilledRectangle(Rect2Dd(r), s.inactiveColor, 0.0f, Colors::Transparent, s.cornerRadius);

            // Fill: completed slides = full, current = progress, future = empty
            float fillFrac = 0.0f;
            if (i < static_cast<int>(currentIndex)) fillFrac = 1.0f;
            else if (i == static_cast<int>(currentIndex)) fillFrac = transitioning ? 1.0f : slideElapsed;

            if (fillFrac > 0.0f) {
                Rect2Di filled = ax.Cell(mainStart, static_cast<int>(barMain * fillFrac), crossLen);
                ctx->DrawFilledRectangle(Rect2Dd(filled), s.activeColor, 0.0f, Colors::Transparent, s.cornerRadius);
            }
        }
    }

    void UltraCanvasSlideshow::DrawIndicatorsCounter(IRenderContext* ctx, const Rect2Di& panel) {
        const auto& s = config.indicators;
        std::string txt = std::to_string(currentIndex + 1) + " / " + std::to_string(slides.size());

        ctx->SetTextPaint(s.textColor);
        FontStyle fs;
        fs.fontFamily = config.fontFamily;
        fs.fontSize = 14.0f;
        fs.fontWeight = FontWeight::Bold;
        ctx->SetFontStyle(fs);

        Size2Di textSize = ctx->GetTextLineDimensions(txt);
        int x, y;
        if (IndicatorIsVertical()) {
            // Centered horizontally in the narrow column; `horizontal` selects the
            // vertical position (Left->top, Right->bottom, Center->middle).
            x = panel.x + (panel.width - textSize.width) / 2;
            y = panel.y + StripStart(panel.height, textSize.height, s.horizontal);
        } else {
            switch (s.horizontal) {
                case TextAlignment::Left:   x = panel.x; break;
                case TextAlignment::Right:  x = panel.x + panel.width - textSize.width; break;
                case TextAlignment::Center:
                default:                    x = panel.x + (panel.width - textSize.width) / 2; break;
            }
            y = panel.y + (panel.height - textSize.height) / 2;
        }
        ctx->DrawText(txt, Point2Dd(static_cast<float>(x), static_cast<float>(y)));

        // No hit rects — counter is informational only
    }

    void UltraCanvasSlideshow::DrawIndicatorsThumbnails(IRenderContext* ctx, const Rect2Di& panel) {
        const auto& s = config.indicators;
        int count = static_cast<int>(slides.size());
        // Thumbnails: width is itemWidth, height is itemHeight (caller usually
        // sets both larger than for bars)
        int thumbW = static_cast<int>(std::max(s.itemWidth, 32.0f));
        int thumbH = static_cast<int>(std::max(s.itemHeight, 20.0f));
        bool vert = IndicatorIsVertical();
        StripAxis ax{vert, panel};
        // Along the strip a thumbnail occupies its width (row) or height (column).
        int mainLen  = vert ? thumbH : thumbW;
        int crossLen = vert ? thumbW : thumbH;
        int step = mainLen + static_cast<int>(s.spacing);
        int total = count * mainLen + (count - 1) * static_cast<int>(s.spacing);
        int start = StripStart(ax.MainSize(), total, s.horizontal);

        for (int i = 0; i < count; ++i) {
            Rect2Di r = ax.Cell(start + i * step, mainLen, crossLen);
            indicatorHitRects.push_back(r);

            // Frame
            bool isActive = (i == static_cast<int>(currentIndex));
            Color frame = isActive ? s.activeColor :
                          (i == hoveredIndicator ? s.hoverColor : s.inactiveColor);

            ctx->PushState();
            ctx->ClipRect(Rect2Dd(r));
            ctx->SetAlpha(isActive ? 1.0f : 0.65f);
            auto img = UCImage::Get(slides[i].imagePath);
            if (img) {
                ctx->DrawImage(*img, Rect2Dd(r), ImageFitMode::Cover);
            } else {
                ctx->SetFillPaint(Color(100, 100, 100));
                ctx->FillRectangle(Rect2Dd(r));
            }
            ctx->PopState();

            // Border on top
            ctx->DrawFilledRectangle(Rect2Dd(r), Colors::Transparent,
                                     isActive ? 2.0f : 1.0f, frame, 0.0f);
        }
    }

    std::string UltraCanvasSlideshow::LabelForSlide(size_t idx) const {
        const auto& s = config.indicators;
        if (idx < s.labels.size() && !s.labels[idx].empty()) return s.labels[idx];
        if (idx < slides.size() && !slides[idx].infoTitle.empty()) return slides[idx].infoTitle;
        return std::to_string(idx + 1);
    }

    void UltraCanvasSlideshow::DrawIndicatorsLabels(IRenderContext* ctx, const Rect2Di& panel) {
        const auto& s = config.indicators;
        int count = static_cast<int>(slides.size());
        if (count <= 0) return;

        FontStyle fs;
        fs.fontFamily = config.fontFamily;
        fs.fontSize = 13.0f;
        fs.fontWeight = FontWeight::Bold;
        ctx->SetFontStyle(fs);

        const int labelHPad = 12;
        int textH = ctx->GetTextLineHeight("Mg");
        int underlineH = 2;
        int rowH = textH + underlineH + 4;
        int spacing = static_cast<int>(s.spacing);
        bool vert = IndicatorIsVertical();

        // Per-label box size along the strip (measured text + padding).
        std::vector<int> widths(count);
        for (int i = 0; i < count; ++i) {
            Size2Di sz = ctx->GetTextLineDimensions(LabelForSlide(i));
            widths[i] = std::max(static_cast<int>(s.itemWidth), sz.width + labelHPad * 2);
        }

        // Draws one label cell and records its hit rect / active underline.
        auto drawCell = [&](int i, const Rect2Di& cell) {
            indicatorHitRects.push_back(Rect2Di(cell.x, cell.y - 4, cell.width, cell.height + 8));

            bool isActive = (i == static_cast<int>(currentIndex));
            Color textColor = isActive ? s.activeColor :
                              (i == hoveredIndicator ? s.activeColor : s.textColor);
            ctx->SetTextPaint(textColor);
            std::string label = LabelForSlide(i);
            Size2Di sz = ctx->GetTextLineDimensions(label);
            int tx = cell.x + (cell.width - sz.width) / 2;
            ctx->DrawText(label, Point2Dd(static_cast<float>(tx), static_cast<float>(cell.y)));

            if (isActive) {
                Rect2Di under(cell.x + labelHPad / 2, cell.y + textH + 2,
                              cell.width - labelHPad, underlineH);
                ctx->DrawFilledRectangle(Rect2Dd(under), s.activeColor, 0.0f,
                                         Colors::Transparent, 0.0f);
            }
        };

        if (!vert) {
            // Row of labels along the bottom/top edge.
            int totalW = spacing * (count - 1);
            for (int w : widths) totalW += w;
            int x = panel.x + StripStart(panel.width, totalW, s.horizontal);
            int y = panel.y + (panel.height - rowH) / 2;
            for (int i = 0; i < count; ++i) {
                drawCell(i, Rect2Di(x, y, widths[i], rowH));
                x += widths[i] + spacing;
            }
        } else {
            // Stacked column of labels along the left/right edge.
            int totalH = count * rowH + (count - 1) * spacing;
            int y = panel.y + StripStart(panel.height, totalH, s.horizontal);
            for (int i = 0; i < count; ++i) {
                drawCell(i, Rect2Di(panel.x, y, panel.width, rowH));
                y += rowH + spacing;
            }
        }
    }

    // ===== HIT TESTING =====
    int UltraCanvasSlideshow::IndicatorAt(const Point2Di& localPoint) const {
        for (size_t i = 0; i < indicatorHitRects.size(); ++i) {
            if (indicatorHitRects[i].Contains(localPoint)) return static_cast<int>(i);
        }
        return -1;
    }

    // ===== EVENT HANDLING =====
    bool UltraCanvasSlideshow::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseEnter: {
                hovered = true;
                if (config.pauseOnHover && playing) {
                    auto* app = UltraCanvasApplication::GetInstance();
                    if (app && advanceTimerId) {
                        app->StopTimer(advanceTimerId);
                        advanceTimerId = 0;
                    }
                    // Freeze the visible progress bars at where they are now,
                    // so they don't keep sliding while the user reads the text.
                    frozenElapsed = slideElapsed;
                    elapsedFrozen = true;
                }
                return true;
            }
            case UCEventType::MouseLeave: {
                hovered = false;
                hoveredIndicator = -1;
                if (config.pauseOnHover && playing) {
                    if (elapsedFrozen) {
                        // Rebase slideStartedAt so slideElapsed continues from frozenElapsed
                        float dur = std::max(1.0f, static_cast<float>(config.slideDuration.count()));
                        auto offsetMs = std::chrono::milliseconds(
                                static_cast<long long>(frozenElapsed * dur));
                        slideStartedAt = std::chrono::steady_clock::now() - offsetMs;
                        elapsedFrozen = false;
                    }
                    if (!transitioning && !advanceTimerId) {
                        // Schedule remaining time only
                        float remaining = std::max(0.05f, 1.0f - slideElapsed);
                        auto* app = UltraCanvasApplication::GetInstance();
                        if (app) {
                            unsigned int ms = static_cast<unsigned int>(
                                    config.slideDuration.count() * remaining);
                            if (ms < 50) ms = 50;
                            advanceTimerId = app->StartTimer(ms, false, [this](TimerId) {
                                advanceTimerId = 0;
                                OnAdvanceTick();
                            });
                        }
                    }
                }
                RequestRedraw();
                return true;
            }
            case UCEventType::MouseMove: {
                // event.pointer is element-local here (windowing layer translates).
                Point2Di p(event.pointer.x, event.pointer.y);
                int newHover = -1;
                if (config.indicators.clickable && config.indicators.hoverHighlight) {
                    newHover = IndicatorAt(p);
                }
                if (newHover != hoveredIndicator) {
                    hoveredIndicator = newHover;
                    RequestRedraw();
                }
                return false;
            }
            case UCEventType::MouseDown: {
                if (event.button != UCMouseButton::Left) return false;
                if (!config.indicators.clickable) return false;
                Point2Di p(event.pointer.x, event.pointer.y);
                int idx = IndicatorAt(p);
                if (idx < 0) return false;

                // ProgressBar uses one hit rect = "advance one"
                if (config.indicators.shape == SlideshowIndicatorShape::ProgressBar) {
                    Next();
                } else if (idx != static_cast<int>(currentIndex) &&
                           idx < static_cast<int>(slides.size())) {
                    StartTransitionTo(static_cast<size_t>(idx));
                }
                return true;
            }
            default:
                break;
        }
        return UltraCanvasContainer::OnEvent(event);
    }

} // namespace UltraCanvas
