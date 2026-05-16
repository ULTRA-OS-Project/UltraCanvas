// include/UltraCanvasSlideshow.h
// Timed image slideshow with optional info text panel and selectable indicator styles.
// Version: 1.0.0
// Last Modified: 2026-05-16
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasTimer.h"
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

    // ===== ONE ENTRY IN THE SLIDESHOW =====
    struct SlideshowSlide {
        std::string imagePath;   // file path passed to UCImage::Get
        std::string infoTitle;   // top line in the info panel
        std::string infoBody;    // body paragraph below the title
    };

    // ===== HOW SLIDES TRANSITION =====
    // (Suffixed because X11/Xlib.h defines None/True/False as preprocessor macros.)
    enum class SlideshowFadeStyle {
        NoFade,           // Instant swap
        CrossFade,        // Both layers visible, sums to 1.0
        FadeOutIn,        // Old fades fully out, then new fades in
        SlideHorizontal   // Old slides left while new slides in from the right
    };

    // ===== HOW THE INFO PANEL TEXT BEHAVES =====
    enum class SlideshowTextMode {
        Static,    // infoTitle/infoBody set once, never animated
        PerSlide   // Text swaps with each slide, fades with the image
    };

    // ===== INDICATOR SHAPE CATALOG =====
    // Matches the eight styles surveyed for the Subaru-style slideshow.
    // (Hidden instead of None because X11 defines None as a macro.)
    enum class SlideshowIndicatorShape {
        Hidden,       // Indicators hidden
        Dots,         // Classic round dots (filled = active)
        Bars,         // Short rectangles / pill bars — Subaru default
        ProgressBar,  // Single bar that grows across slideDuration
        StoryBars,    // One bar per slide, each fills over its own slot (Instagram Stories)
        Counter,      // Plain "3 / 7" text
        Thumbnails,   // Tiny preview of each slide's image
        Labels        // Per-slide text label, tab-style underline on active
    };

    struct SlideshowIndicatorStyle {
        SlideshowIndicatorShape shape = SlideshowIndicatorShape::Bars;

        // Geometry of one indicator cell
        float itemWidth   = 30.0f;   // bar width / dot diameter / thumb width  / label min width
        float itemHeight  = 3.0f;    // bar thickness / dot diameter / thumb height
        float spacing     = 6.0f;    // gap between cells
        float cornerRadius = 0.0f;   // 0 = sharp, itemHeight/2 = pill

        // Colors
        Color inactiveColor = Color(200, 200, 200, 255);
        Color activeColor   = Color(0, 102, 204, 255);
        Color hoverColor    = Color(140, 140, 140, 255);
        Color textColor     = Color(60, 60, 60, 255);   // counter / labels

        // Placement inside the slideshow bounds
        VerticalAlignment vertical = VerticalAlignment::Bottom;  // Top / Bottom
        TextAlignment     horizontal = TextAlignment::Center;
        int marginFromEdge = 12;     // distance from the chosen edge in pixels

        // Behavior
        bool clickable = true;
        bool hoverHighlight = true;

        // Per-slide label text (used only when shape == Labels). If empty,
        // falls back to the slide's infoTitle. Indexed by slide index.
        std::vector<std::string> labels;
    };

    // ===== TOP-LEVEL CONFIGURATION =====
    struct SlideshowConfig {
        std::chrono::milliseconds slideDuration{6000};   // time one slide stays before advancing
        std::chrono::milliseconds fadeDuration{600};     // time one transition takes

        SlideshowFadeStyle fadeStyle = SlideshowFadeStyle::CrossFade;
        SlideshowTextMode  textMode  = SlideshowTextMode::PerSlide;

        float imagePanelRatio = 0.66f;     // left image / right info split (1.0 = no info panel)
        bool autoPlay        = true;
        bool pauseOnHover    = true;
        bool loop            = true;

        // Frame rate of the animation timer (during a transition).
        // Outside transitions the slideshow only wakes once per slideDuration.
        int animationFps = 60;

        Color backgroundColor = Color(20, 20, 22, 255);
        Color infoPanelColor  = Color(245, 245, 245, 255);
        Color titleColor      = Color(20, 20, 22, 255);
        Color bodyColor       = Color(60, 60, 60, 255);
        int   infoPadding     = 24;
        float titleFontSize   = 22.0f;
        float bodyFontSize    = 14.0f;
        std::string fontFamily;            // empty = system default

        SlideshowIndicatorStyle indicators;
    };

    // ===== THE SLIDESHOW ELEMENT =====
    // Renders itself: image layers, info panel and indicators are all painted
    // directly inside Render() — no child UI elements are created. This keeps
    // the animation cheap (just opacity tweens) and lets the indicator drawing
    // switch on a single enum.
    class UltraCanvasSlideshow : public UltraCanvasContainer {
    public:
        UltraCanvasSlideshow(const std::string& identifier = "Slideshow",
                             long x = 0, long y = 0, long w = 800, long h = 450);
        ~UltraCanvasSlideshow() override;

        // ===== CONFIGURATION =====
        void SetConfig(const SlideshowConfig& cfg);
        const SlideshowConfig& GetConfig() const { return config; }

        void SetIndicatorStyle(const SlideshowIndicatorStyle& style);
        const SlideshowIndicatorStyle& GetIndicatorStyle() const { return config.indicators; }

        void SetStaticText(const std::string& title, const std::string& body);

        // ===== SLIDE MANAGEMENT =====
        void AddSlide(const SlideshowSlide& slide);
        void ClearSlides();
        size_t GetSlideCount() const { return slides.size(); }
        const std::vector<SlideshowSlide>& GetSlides() const { return slides; }

        // ===== PLAYBACK CONTROL =====
        void Play();
        void Pause();
        bool IsPlaying() const { return playing; }

        void Next();
        void Previous();
        void GoTo(size_t index, bool animated = true);
        size_t GetCurrentIndex() const { return currentIndex; }

        // ===== EVENTS =====
        std::function<void(size_t)> onSlideChanged;

        // ===== OVERRIDES =====
        void Render(IRenderContext* ctx, const Rect2Di& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;

    private:
        // ===== STATE =====
        SlideshowConfig config;
        std::vector<SlideshowSlide> slides;
        std::string staticTitle;
        std::string staticBody;

        size_t currentIndex = 0;
        size_t previousIndex = 0;

        bool playing = false;
        bool hovered = false;
        bool transitioning = false;
        float transitionProgress = 0.0f;   // 0..1 during a transition

        // Continuous progress through the current slide's display time
        // (drives ProgressBar and StoryBars indicator styles). 0..1.
        float slideElapsed = 0.0f;
        // While hover-paused, freeze slideElapsed at this value
        float frozenElapsed = 0.0f;
        bool elapsedFrozen = false;

        TimerId animationTimerId = 0;      // running while transitioning, drives smooth fade
        TimerId advanceTimerId = 0;        // single-shot, schedules the next transition
        TimerId redrawTimerId = 0;         // low-rate repaint for continuous indicators (ProgressBar / StoryBars)
        std::chrono::steady_clock::time_point slideStartedAt;
        std::chrono::steady_clock::time_point transitionStartedAt;

        // Per-indicator hit rects, recomputed each frame the indicator panel draws
        std::vector<Rect2Di> indicatorHitRects;
        int hoveredIndicator = -1;

        // ===== INTERNAL HELPERS =====
        Rect2Di GetImagePanelRect() const;
        Rect2Di GetInfoPanelRect() const;
        Rect2Di GetIndicatorPanelRect() const;

        void StartTransitionTo(size_t newIndex);
        void FinishTransition();
        void ScheduleNextAdvance();
        void CancelTimers();

        void OnAnimationTick();   // called by animation timer
        void OnAdvanceTick();     // called by advance timer
        void OnRedrawTick();      // called by redraw timer
        bool IndicatorNeedsContinuousRedraw() const;
        void UpdateRedrawTimer();

        // Drawing
        void DrawBackground(IRenderContext* ctx);
        void DrawSlideImages(IRenderContext* ctx);
        void DrawInfoPanel(IRenderContext* ctx);
        void DrawIndicators(IRenderContext* ctx);

        // Indicator shape renderers
        void DrawIndicatorsDots(IRenderContext* ctx, const Rect2Di& panel);
        void DrawIndicatorsBars(IRenderContext* ctx, const Rect2Di& panel);
        void DrawIndicatorsProgressBar(IRenderContext* ctx, const Rect2Di& panel);
        void DrawIndicatorsStoryBars(IRenderContext* ctx, const Rect2Di& panel);
        void DrawIndicatorsCounter(IRenderContext* ctx, const Rect2Di& panel);
        void DrawIndicatorsThumbnails(IRenderContext* ctx, const Rect2Di& panel);
        void DrawIndicatorsLabels(IRenderContext* ctx, const Rect2Di& panel);

        // Indicator hit testing — uses indicatorHitRects from the last draw.
        // Returns -1 if no indicator was hit.
        int IndicatorAt(const Point2Di& localPoint) const;

        // One image-fade pass, with opacity already chosen by caller
        void DrawSlideAt(IRenderContext* ctx, size_t slideIdx,
                         const Rect2Df& rect, float alpha,
                         float horizontalOffsetPx);

        std::string LabelForSlide(size_t idx) const;
    };

    // ===== FACTORY =====
    inline std::shared_ptr<UltraCanvasSlideshow> CreateSlideshow(
            const std::string& identifier, long x, long y, long w, long h) {
        return std::make_shared<UltraCanvasSlideshow>(identifier, x, y, w, h);
    }

} // namespace UltraCanvas
