// include/UltraCanvasSlideshow.h
// Timed image slideshow with optional info text panel and selectable indicator styles.
// Version: 1.4.0
// Last Modified: 2026-06-13
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
        SlideHorizontal,  // Old slides left while new slides in from the right
        SlideVertical,    // Old slides up while new slides in from the bottom
        ZoomFade          // New slide zooms in from slightly enlarged while cross-fading
    };

    // ===== HOW THE INFO PANEL TEXT BEHAVES =====
    enum class SlideshowTextMode {
        Static,    // infoTitle/infoBody set once, never animated
        PerSlide   // Text swaps with each slide, fades with the image
    };

    // ===== WHERE / HOW THE INFO PANEL SITS RELATIVE TO THE IMAGE =====
    // "Split" layouts give the panel its own region and shrink the image to the
    // remainder. "Overlay" layouts float the panel on top of a full-bleed image
    // with a translucent scrim behind the text.
    enum class SlideshowInfoLayout {
        Hidden,             // No info panel — full-bleed image

        // Split — panel owns its region, image takes the rest
        SplitLeft,          // Panel left,   image right
        SplitRight,         // Panel right,  image left  (legacy default look)
        SplitTop,           // Panel top,    image below
        SplitBottom,        // Panel bottom, image above

        // Overlay edges — panel spans one edge of the image
        OverlayLeft,
        OverlayRight,
        OverlayTop,
        OverlayBottom,

        // Overlay corners — panel is a floating box anchored in a corner
        OverlayTopLeft,
        OverlayTopRight,
        OverlayBottomLeft,
        OverlayBottomRight,

        // Overlay center / full
        OverlayCenter,      // Floating box centered over the image
        OverlayFull         // Scrim across the whole image
    };

    // True for layouts that float on top of a full-bleed image.
    inline bool SlideshowLayoutIsOverlay(SlideshowInfoLayout l) {
        switch (l) {
            case SlideshowInfoLayout::OverlayLeft:
            case SlideshowInfoLayout::OverlayRight:
            case SlideshowInfoLayout::OverlayTop:
            case SlideshowInfoLayout::OverlayBottom:
            case SlideshowInfoLayout::OverlayTopLeft:
            case SlideshowInfoLayout::OverlayTopRight:
            case SlideshowInfoLayout::OverlayBottomLeft:
            case SlideshowInfoLayout::OverlayBottomRight:
            case SlideshowInfoLayout::OverlayCenter:
            case SlideshowInfoLayout::OverlayFull:
                return true;
            default:
                return false;
        }
    }

    // True for layouts that carve out their own region beside the image.
    inline bool SlideshowLayoutIsSplit(SlideshowInfoLayout l) {
        switch (l) {
            case SlideshowInfoLayout::SplitLeft:
            case SlideshowInfoLayout::SplitRight:
            case SlideshowInfoLayout::SplitTop:
            case SlideshowInfoLayout::SplitBottom:
                return true;
            default:
                return false;
        }
    }

    // ===== HOW MISMATCHED IMAGES ARE FITTED INTO THE IMAGE AREA =====
    // Reuses the framework's ImageFitMode for the actual scaling policy:
    //   Cover     — auto-zoom + auto-crop to fill (no gaps, crops overflow)
    //   Contain   — fit entirely inside, preserving aspect (leaves gaps)
    //   Fill      — stretch to the exact area (distorts aspect)
    //   ScaleDown — like Contain but never upscale
    //   NoScale   — native pixel size, centered by the focal point
    // The focal point (0..1) chooses which part survives a crop / where a
    // smaller image sits. GapFill decides what shows behind letterboxed images.
    enum class SlideshowGapFill {
        BackgroundColor,  // reuse config.backgroundColor
        LetterboxColor,   // use config.letterboxColor
        BlurredImage      // a zoomed copy of the same image, softened by a scrim
    };

    // ===== INDICATOR SHAPE CATALOG =====
    // Matches the eight indicator styles offered by the slideshow widget.
    // (Hidden instead of None because X11 defines None as a macro.)
    enum class SlideshowIndicatorShape {
        Hidden,       // Indicators hidden
        Dots,         // Classic round dots (filled = active)
        Bars,         // Short rectangles / pill bars — default
        ProgressBar,  // Single bar that grows across slideDuration
        StoryBars,    // One bar per slide, each fills over its own slot (Instagram Stories)
        Counter,      // Plain "3 / 7" text
        Thumbnails,   // Tiny preview of each slide's image
        Labels        // Per-slide text label, tab-style underline on active
    };

    // ===== WHICH EDGE THE INDICATOR STRIP HUGS =====
    // Top/Bottom lay the items out in a row; Left/Right stack them in a column.
    enum class SlideshowIndicatorEdge {
        Bottom,   // default
        Top,
        Left,
        Right
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

        // Placement inside the image area.
        // `edge` is the primary control (any of the four sides). `vertical` is
        // kept for source compatibility: if `edge` is left at its Bottom default
        // it is derived from `vertical` (Top -> Top edge), so old code that only
        // set `vertical` keeps working.
        SlideshowIndicatorEdge edge = SlideshowIndicatorEdge::Bottom;
        VerticalAlignment vertical = VerticalAlignment::Bottom;  // legacy Top / Bottom
        // Alignment along the strip: for Top/Bottom edges this is the horizontal
        // alignment of the row; for Left/Right edges it selects Top/Center/Bottom
        // of the column (Left->Top, Right->Bottom, Center->Center).
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

        // ===== IMAGE FITTING =====
        // How each slide is auto-sized into the image area when its dimensions
        // don't match the layout. Defaults to Cover (auto-zoom + auto-crop).
        ImageFitMode imageFit = ImageFitMode::Cover;
        // Focal point in 0..1 image space. For a crop (Cover) it picks which part
        // survives; for a smaller image (Contain/NoScale) it picks the placement.
        // (0,0) = top-left, (0.5,0.5) = centered, (1,1) = bottom-right.
        Point2Df imageFocus{0.5f, 0.5f};
        // What fills the gaps left by Contain / ScaleDown / NoScale.
        SlideshowGapFill gapFill = SlideshowGapFill::BackgroundColor;
        Color letterboxColor = Color(0, 0, 0, 255);  // used when gapFill == LetterboxColor

        // ===== INFO PANEL LAYOUT =====
        SlideshowInfoLayout infoLayout = SlideshowInfoLayout::SplitRight;

        // For SPLIT layouts: fraction of the slideshow given to the IMAGE along
        // the split axis (SplitLeft/Right -> width, SplitTop/Bottom -> height).
        // The info panel takes the remainder. (1.0 = no panel, like Hidden.)
        // Kept named `imagePanelRatio` for source compatibility.
        float imagePanelRatio = 0.66f;

        // For EDGE overlays (OverlayLeft/Right/Top/Bottom): fraction of the image
        // spanned by the floating panel along the overlay axis.
        float overlaySizeRatio = 0.34f;

        // For CORNER / CENTER overlays: size of the floating box as a fraction of
        // the image, plus the gap kept from the image edges.
        float overlayBoxWidthRatio  = 0.42f;
        float overlayBoxHeightRatio = 0.5f;
        int   overlayMargin         = 20;

        // Background painted behind overlay text (translucent so the image shows
        // through). Ignored by split layouts, which use infoPanelColor.
        Color overlayScrimColor = Color(0, 0, 0, 140);

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
        UltraCanvasSlideshow(const std::string& identifier,
                             float x, float y, float w, float h);

        UltraCanvasSlideshow(const std::string& identifier, float w, float h)
            : UltraCanvasSlideshow(identifier, -1, -1, w, h) {}

        explicit UltraCanvasSlideshow(const std::string& identifier)
            : UltraCanvasSlideshow(identifier, -1, -1, -1, -1) {}

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
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;

        // The slideshow takes keyboard focus on click so the arrow keys can
        // drive manual navigation (Left/Down = previous, Right/Up = next).
        bool AcceptsFocus() const override { return true; }

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
        Rect2Di GetImagePanelRect() const;   // region the image is drawn into
        Rect2Di GetInfoPanelRect() const;    // region the info panel occupies (0 if hidden)
        Rect2Di GetIndicatorPanelRect() const;

        SlideshowIndicatorEdge EffectiveIndicatorEdge() const;
        bool IndicatorIsVertical() const;    // true for Left/Right edges

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

        // One image-fade pass, with opacity already chosen by caller. Applies the
        // configured fit mode + focal point, then the transition offset/zoom.
        // offsetX/offsetY translate the image (slide transitions); scale > 1
        // enlarges it around its center (zoom transition).
        void DrawSlideAt(IRenderContext* ctx, size_t slideIdx,
                         const Rect2Dd& rect, float alpha,
                         float offsetX, float offsetY = 0.0f, float scale = 1.0f);

        // Fills the area behind a letterboxed image per config.gapFill.
        void DrawGapFill(IRenderContext* ctx, size_t slideIdx,
                         const Rect2Dd& rect, float alpha);

        std::string LabelForSlide(size_t idx) const;
    };

    // ===== FACTORY =====
    inline std::shared_ptr<UltraCanvasSlideshow> CreateSlideshow(
            const std::string& identifier, float x, float y, float w, float h) {
        return std::make_shared<UltraCanvasSlideshow>(identifier, x, y, w, h);
    }

} // namespace UltraCanvas
