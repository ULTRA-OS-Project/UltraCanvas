// core/UltraCanvasSlideshow.cpp
// Timed image slideshow with selectable indicator styles.
// Version: 1.0.0
// Last Modified: 2026-05-16
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
                                               long x, long y, long w, long h)
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
        if (config.imagePanelRatio >= 0.999f) {
            return bnds;
        }
        int imgW = static_cast<int>(bnds.width * config.imagePanelRatio);
        return Rect2Di(0, 0, imgW, bnds.height);
    }

    Rect2Di UltraCanvasSlideshow::GetInfoPanelRect() const {
        auto bnds = GetLocalBounds();
        if (config.imagePanelRatio >= 0.999f) {
            return Rect2Di(0, 0, 0, 0);
        }
        int imgW = static_cast<int>(bnds.width * config.imagePanelRatio);
        return Rect2Di(imgW, 0, bnds.width - imgW, bnds.height);
    }

    Rect2Di UltraCanvasSlideshow::GetIndicatorPanelRect() const {
        Rect2Di imgRect = GetImagePanelRect();
        const auto& s = config.indicators;
        if (s.shape == SlideshowIndicatorShape::Hidden) {
            return Rect2Di(0, 0, 0, 0);
        }
        // Reserve a strip at the top or bottom of the image panel
        int reservedH = static_cast<int>(s.itemHeight) + s.marginFromEdge * 2;
        // Thumbnails need more space
        if (s.shape == SlideshowIndicatorShape::Thumbnails) {
            reservedH = static_cast<int>(s.itemHeight) + s.marginFromEdge * 2 + 4;
        }
        // Labels / Counter need text room
        if (s.shape == SlideshowIndicatorShape::Labels ||
            s.shape == SlideshowIndicatorShape::Counter) {
            reservedH = 22 + s.marginFromEdge * 2;
        }
        int y = (s.vertical == VerticalAlignment::Top)
                ? imgRect.y + s.marginFromEdge
                : imgRect.y + imgRect.height - reservedH + s.marginFromEdge;
        return Rect2Di(imgRect.x + 8, y, imgRect.width - 16, reservedH - s.marginFromEdge * 2);
    }

    // ===== RENDER =====
    void UltraCanvasSlideshow::Render(IRenderContext* ctx, const Rect2Di& dirtyRect) {
        // Container background + borders via base class
        UltraCanvasUIElement::Render(ctx, dirtyRect);

        if (slides.empty()) {
            ctx->SetTextPaint(Color(180, 180, 180));
            ctx->SetFontSize(14.0f);
            ctx->DrawTextInRect("(no slides)", Rect2Df(GetLocalBounds()));
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
        ctx->ClipRect(Rect2Df(GetLocalBounds()));
        DrawBackground(ctx);
        DrawSlideImages(ctx);
        DrawInfoPanel(ctx);
        DrawIndicators(ctx);
        ctx->PopState();
    }

    void UltraCanvasSlideshow::DrawBackground(IRenderContext* ctx) {
        Rect2Di img = GetImagePanelRect();
        ctx->SetFillPaint(config.backgroundColor);
        ctx->FillRectangle(Rect2Df(img));
    }

    void UltraCanvasSlideshow::DrawSlideAt(IRenderContext* ctx, size_t slideIdx,
                                           const Rect2Df& rect, float alpha,
                                           float horizontalOffsetPx) {
        if (slideIdx >= slides.size()) return;
        const auto& slide = slides[slideIdx];
        if (slide.imagePath.empty()) return;
        auto img = UCImage::Get(slide.imagePath);
        if (!img) return;

        ctx->PushState();
        Rect2Df clipR = rect;
        ctx->ClipRect(clipR);
        ctx->SetAlpha(std::max(0.0f, std::min(1.0f, alpha)));

        Rect2Df dst = rect;
        dst.x += horizontalOffsetPx;
        ctx->DrawImage(*img, dst, ImageFitMode::Cover);
        ctx->PopState();
    }

    void UltraCanvasSlideshow::DrawSlideImages(IRenderContext* ctx) {
        Rect2Df imgRect = Rect2Df(GetImagePanelRect());
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
        }
    }

    void UltraCanvasSlideshow::DrawInfoPanel(IRenderContext* ctx) {
        Rect2Di info = GetInfoPanelRect();
        if (info.width <= 0 || info.height <= 0) return;

        ctx->SetFillPaint(config.infoPanelColor);
        ctx->FillRectangle(Rect2Df(info));

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
            ctx->DrawTextInRect(title, Rect2Df(titleRect));
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
            ctx->DrawTextInRect(body, Rect2Df(textArea));
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

    // Common helper: where does a row of `count` items, each `itemW` wide,
    // separated by `spacing`, start on x given the panel and horizontal alignment.
    static int RowStartX(const Rect2Di& panel, int count,
                         float itemW, float spacing,
                         TextAlignment align) {
        float total = count * itemW + (count - 1) * spacing;
        switch (align) {
            case TextAlignment::Left:   return panel.x;
            case TextAlignment::Right:  return panel.x + panel.width - static_cast<int>(total);
            case TextAlignment::Center:
            case TextAlignment::Justify:
            default:                    return panel.x + (panel.width - static_cast<int>(total)) / 2;
        }
    }

    void UltraCanvasSlideshow::DrawIndicatorsDots(IRenderContext* ctx, const Rect2Di& panel) {
        const auto& s = config.indicators;
        int count = static_cast<int>(slides.size());
        float diameter = std::max(s.itemHeight, 6.0f);
        float radius = diameter / 2.0f;
        int startX = RowStartX(panel, count, diameter, s.spacing, s.horizontal);
        int centerY = panel.y + panel.height / 2;

        for (int i = 0; i < count; ++i) {
            float cx = startX + i * (diameter + s.spacing) + radius;
            Rect2Di hit(static_cast<int>(cx - radius), static_cast<int>(centerY - radius),
                        static_cast<int>(diameter), static_cast<int>(diameter));
            indicatorHitRects.push_back(hit);

            Color fill = (i == static_cast<int>(currentIndex)) ? s.activeColor : s.inactiveColor;
            if (s.hoverHighlight && i == hoveredIndicator && i != static_cast<int>(currentIndex)) {
                fill = s.hoverColor;
            }
            ctx->DrawFilledCircle(Point2Df(cx, static_cast<float>(centerY)), radius, fill);
        }
    }

    void UltraCanvasSlideshow::DrawIndicatorsBars(IRenderContext* ctx, const Rect2Di& panel) {
        const auto& s = config.indicators;
        int count = static_cast<int>(slides.size());
        int startX = RowStartX(panel, count, s.itemWidth, s.spacing, s.horizontal);
        int y = panel.y + (panel.height - static_cast<int>(s.itemHeight)) / 2;

        for (int i = 0; i < count; ++i) {
            Rect2Di r(startX + i * static_cast<int>(s.itemWidth + s.spacing), y,
                      static_cast<int>(s.itemWidth), static_cast<int>(s.itemHeight));
            // Expand hit area vertically for easier clicking
            Rect2Di hit(r.x, r.y - 6, r.width, r.height + 12);
            indicatorHitRects.push_back(hit);

            Color fill = (i == static_cast<int>(currentIndex)) ? s.activeColor : s.inactiveColor;
            if (s.hoverHighlight && i == hoveredIndicator && i != static_cast<int>(currentIndex)) {
                fill = s.hoverColor;
            }
            ctx->DrawFilledRectangle(Rect2Df(r), fill, 0.0f, Colors::Transparent, s.cornerRadius);
        }
    }

    void UltraCanvasSlideshow::DrawIndicatorsProgressBar(IRenderContext* ctx, const Rect2Di& panel) {
        const auto& s = config.indicators;
        // Single bar spanning the panel; one hit rect for "click to next"
        Rect2Di full(panel.x, panel.y + (panel.height - static_cast<int>(s.itemHeight)) / 2,
                     panel.width, static_cast<int>(s.itemHeight));

        ctx->DrawFilledRectangle(Rect2Df(full), s.inactiveColor, 0.0f, Colors::Transparent, s.cornerRadius);

        float progress = transitioning ? Smoothstep(transitionProgress)
                                       : slideElapsed;
        int filledW = std::max(0, std::min(full.width, static_cast<int>(full.width * progress)));
        Rect2Di filled(full.x, full.y, filledW, full.height);
        ctx->DrawFilledRectangle(Rect2Df(filled), s.activeColor, 0.0f, Colors::Transparent, s.cornerRadius);

        // Single hit rect covering the whole bar — clicking it advances to next.
        Rect2Di hit(full.x, full.y - 6, full.width, full.height + 12);
        indicatorHitRects.push_back(hit);
    }

    void UltraCanvasSlideshow::DrawIndicatorsStoryBars(IRenderContext* ctx, const Rect2Di& panel) {
        const auto& s = config.indicators;
        int count = static_cast<int>(slides.size());
        if (count <= 0) return;
        // Each bar takes an equal share of panel width
        int totalSpacing = (count - 1) * static_cast<int>(s.spacing);
        int barW = std::max(1, (panel.width - totalSpacing) / count);
        int y = panel.y + (panel.height - static_cast<int>(s.itemHeight)) / 2;

        for (int i = 0; i < count; ++i) {
            int x = panel.x + i * (barW + static_cast<int>(s.spacing));
            Rect2Di r(x, y, barW, static_cast<int>(s.itemHeight));
            Rect2Di hit(r.x, r.y - 6, r.width, r.height + 12);
            indicatorHitRects.push_back(hit);

            // Background
            ctx->DrawFilledRectangle(Rect2Df(r), s.inactiveColor, 0.0f, Colors::Transparent, s.cornerRadius);

            // Fill: completed slides = full, current = progress, future = empty
            float fillFrac = 0.0f;
            if (i < static_cast<int>(currentIndex)) fillFrac = 1.0f;
            else if (i == static_cast<int>(currentIndex)) fillFrac = transitioning ? 1.0f : slideElapsed;

            if (fillFrac > 0.0f) {
                Rect2Di filled(r.x, r.y,
                               static_cast<int>(r.width * fillFrac), r.height);
                ctx->DrawFilledRectangle(Rect2Df(filled), s.activeColor, 0.0f, Colors::Transparent, s.cornerRadius);
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
        int x = panel.x;
        switch (s.horizontal) {
            case TextAlignment::Left:   x = panel.x; break;
            case TextAlignment::Right:  x = panel.x + panel.width - textSize.width; break;
            case TextAlignment::Center:
            default:                    x = panel.x + (panel.width - textSize.width) / 2; break;
        }
        int y = panel.y + (panel.height - textSize.height) / 2;
        ctx->DrawText(txt, Point2Df(static_cast<float>(x), static_cast<float>(y)));

        // No hit rects — counter is informational only
    }

    void UltraCanvasSlideshow::DrawIndicatorsThumbnails(IRenderContext* ctx, const Rect2Di& panel) {
        const auto& s = config.indicators;
        int count = static_cast<int>(slides.size());
        // Thumbnails: width is itemWidth, height is itemHeight (caller usually
        // sets both larger than for bars)
        float thumbW = std::max(s.itemWidth, 32.0f);
        float thumbH = std::max(s.itemHeight, 20.0f);
        int startX = RowStartX(panel, count, thumbW, s.spacing, s.horizontal);
        int y = panel.y + (panel.height - static_cast<int>(thumbH)) / 2;

        for (int i = 0; i < count; ++i) {
            Rect2Di r(startX + i * static_cast<int>(thumbW + s.spacing), y,
                      static_cast<int>(thumbW), static_cast<int>(thumbH));
            indicatorHitRects.push_back(r);

            // Frame
            bool isActive = (i == static_cast<int>(currentIndex));
            Color frame = isActive ? s.activeColor :
                          (i == hoveredIndicator ? s.hoverColor : s.inactiveColor);

            ctx->PushState();
            ctx->ClipRect(Rect2Df(r));
            ctx->SetAlpha(isActive ? 1.0f : 0.65f);
            auto img = UCImage::Get(slides[i].imagePath);
            if (img) {
                ctx->DrawImage(*img, Rect2Df(r), ImageFitMode::Cover);
            } else {
                ctx->SetFillPaint(Color(100, 100, 100));
                ctx->FillRectangle(Rect2Df(r));
            }
            ctx->PopState();

            // Border on top
            ctx->DrawFilledRectangle(Rect2Df(r), Colors::Transparent,
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

        // Measure each label; layout left-to-right with spacing
        std::vector<int> widths(count);
        int totalW = 0;
        const int labelHPad = 12;
        for (int i = 0; i < count; ++i) {
            Size2Di sz = ctx->GetTextLineDimensions(LabelForSlide(i));
            widths[i] = std::max(static_cast<int>(s.itemWidth), sz.width + labelHPad * 2);
            totalW += widths[i];
        }
        totalW += static_cast<int>(s.spacing) * (count - 1);

        int x;
        switch (s.horizontal) {
            case TextAlignment::Left:   x = panel.x; break;
            case TextAlignment::Right:  x = panel.x + panel.width - totalW; break;
            case TextAlignment::Center:
            default:                    x = panel.x + (panel.width - totalW) / 2; break;
        }

        int textH = ctx->GetTextLineHeight("Mg");
        int underlineH = 2;
        int rowH = textH + underlineH + 4;
        int y = panel.y + (panel.height - rowH) / 2;

        for (int i = 0; i < count; ++i) {
            Rect2Di cell(x, y, widths[i], rowH);
            Rect2Di hit(cell.x, cell.y - 4, cell.width, cell.height + 8);
            indicatorHitRects.push_back(hit);

            bool isActive = (i == static_cast<int>(currentIndex));
            Color textColor = isActive ? s.activeColor :
                              (i == hoveredIndicator ? s.activeColor : s.textColor);

            ctx->SetTextPaint(textColor);
            std::string label = LabelForSlide(i);
            Size2Di sz = ctx->GetTextLineDimensions(label);
            int tx = cell.x + (cell.width - sz.width) / 2;
            ctx->DrawText(label, Point2Df(static_cast<float>(tx), static_cast<float>(cell.y)));

            // Underline for active tab
            if (isActive) {
                Rect2Di under(cell.x + labelHPad / 2, cell.y + textH + 2,
                              cell.width - labelHPad, underlineH);
                ctx->DrawFilledRectangle(Rect2Df(under), s.activeColor, 0.0f,
                                         Colors::Transparent, 0.0f);
            }

            x += widths[i] + static_cast<int>(s.spacing);
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
