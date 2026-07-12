# UltraCanvasSlideshow Documentation

## Overview

**UltraCanvasSlideshow** is a timed image slideshow element. It cross-fades through a list of slides, each pairing an image with an optional info-panel title and body, and renders everything itself (image layers, info panel, and indicators are painted directly in `Render()` — no child UI elements are created). It offers six transition styles, fourteen info-panel layouts (split and overlay), five image-fit policies, and eight selectable indicator styles, plus autoplay, hover-pause, looping, and arrow-key navigation.

**Version:** 1.0.0
**Header:** `include/UltraCanvasSlideshow.h`
**Namespace:** `UltraCanvas`
**Base Class:** `UltraCanvasContainer`

## Features

- **Timed Autoplay**: Slides advance automatically on `slideDuration`, with `loop`, `autoPlay`, and `pauseOnHover`
- **Six Transition Styles**: NoFade, CrossFade, FadeOutIn, SlideHorizontal, SlideVertical, ZoomFade
- **Info Panel**: Per-slide or static title/body text, fourteen split and overlay layouts
- **Image Fitting**: Cover, Contain, Fill, ScaleDown, NoScale with a configurable focal point
- **Gap Fill**: Background color, letterbox color, or a blurred copy of the image behind letterboxed slides
- **Eight Indicator Styles**: Dots, Bars, ProgressBar, StoryBars, Counter, Thumbnails, Labels, or Hidden
- **Indicator Placement**: Any of the four edges (Bottom, Top, Left, Right), clickable with hover highlight
- **Manual Navigation**: `Next`/`Previous`/`GoTo`, clickable indicators, and arrow keys (Left/Down = previous, Right/Up = next)
- **Change Callback**: `onSlideChanged` fires with the new slide index

## Header Include

```cpp
#include "UltraCanvasSlideshow.h"
```

## Class Reference

### Constructor

```cpp
UltraCanvasSlideshow(const std::string& identifier,
                     float x, float y, float w, float h);

UltraCanvasSlideshow(const std::string& identifier, float w, float h);  // x/y = -1 (auto)

explicit UltraCanvasSlideshow(const std::string& identifier);           // all auto
```

### Factory Function

```cpp
std::shared_ptr<UltraCanvasSlideshow> CreateSlideshow(
    const std::string& identifier, float x, float y, float w, float h);
```

### SlideshowSlide Structure

One entry in the slideshow.

```cpp
struct SlideshowSlide {
    std::string imagePath;   // file path passed to UCImage::Get
    std::string infoTitle;   // top line in the info panel
    std::string infoBody;    // body paragraph below the title
};
```

### Configuration

```cpp
void SetConfig(const SlideshowConfig& cfg);
const SlideshowConfig& GetConfig() const;

void SetIndicatorStyle(const SlideshowIndicatorStyle& style);
const SlideshowIndicatorStyle& GetIndicatorStyle() const;

void SetStaticText(const std::string& title, const std::string& body);
```

### Slide Management

```cpp
void AddSlide(const SlideshowSlide& slide);
void ClearSlides();
size_t GetSlideCount() const;
const std::vector<SlideshowSlide>& GetSlides() const;
```

### Playback Control

```cpp
void Play();
void Pause();
bool IsPlaying() const;

void Next();
void Previous();
void GoTo(size_t index, bool animated = true);
size_t GetCurrentIndex() const;
```

### Events

```cpp
// Fired when the visible slide changes. The argument is the new slide index.
std::function<void(size_t)> onSlideChanged;
```

### Overrides

```cpp
void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
bool OnEvent(const UCEvent& event) override;
bool AcceptsFocus() const override;   // true — takes focus on click for arrow-key navigation
```

## Enumerations

### SlideshowFadeStyle

How slides transition. (Suffixed names because X11/Xlib.h defines `None`/`True`/`False` as macros.)

```cpp
enum class SlideshowFadeStyle {
    NoFade,           // Instant swap
    CrossFade,        // Both layers visible, sums to 1.0
    FadeOutIn,        // Old fades fully out, then new fades in
    SlideHorizontal,  // Old slides left while new slides in from the right
    SlideVertical,    // Old slides up while new slides in from the bottom
    ZoomFade          // New slide zooms in from slightly enlarged while cross-fading
};
```

### SlideshowTextMode

```cpp
enum class SlideshowTextMode {
    Static,    // infoTitle/infoBody set once, never animated
    PerSlide   // Text swaps with each slide, fades with the image
};
```

### SlideshowInfoLayout

Where and how the info panel sits relative to the image. "Split" layouts give the panel its own region and shrink the image to the remainder; "Overlay" layouts float the panel on top of a full-bleed image with a translucent scrim behind the text.

```cpp
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

// Free helpers for classifying a layout:
inline bool SlideshowLayoutIsOverlay(SlideshowInfoLayout l);
inline bool SlideshowLayoutIsSplit(SlideshowInfoLayout l);
```

### SlideshowGapFill

What fills the area behind a letterboxed image (used by Contain / ScaleDown / NoScale fits).

```cpp
enum class SlideshowGapFill {
    BackgroundColor,  // reuse config.backgroundColor
    LetterboxColor,   // use config.letterboxColor
    BlurredImage      // a zoomed copy of the same image, softened by a scrim
};
```

### SlideshowIndicatorShape

The eight indicator styles offered by the widget. (`Hidden` instead of `None` because X11 defines `None` as a macro.)

```cpp
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
```

### SlideshowIndicatorEdge

Which edge the indicator strip hugs. Top/Bottom lay the items out in a row; Left/Right stack them in a column.

```cpp
enum class SlideshowIndicatorEdge {
    Bottom,   // default
    Top,
    Left,
    Right
};
```

## Data Structures

### SlideshowIndicatorStyle

```cpp
struct SlideshowIndicatorStyle {
    SlideshowIndicatorShape shape = SlideshowIndicatorShape::Bars;

    // Geometry of one indicator cell
    float itemWidth    = 30.0f;   // bar width / dot diameter / thumb width / label min width
    float itemHeight   = 3.0f;    // bar thickness / dot diameter / thumb height
    float spacing      = 6.0f;    // gap between cells
    float cornerRadius = 0.0f;    // 0 = sharp, itemHeight/2 = pill

    // Colors
    Color inactiveColor = Color(200, 200, 200, 255);
    Color activeColor   = Color(0, 102, 204, 255);
    Color hoverColor    = Color(140, 140, 140, 255);
    Color textColor     = Color(60, 60, 60, 255);   // counter / labels

    // Placement inside the image area. `edge` is the primary control; `vertical`
    // is kept for source compatibility (if `edge` is left at its Bottom default it
    // is derived from `vertical`).
    SlideshowIndicatorEdge edge = SlideshowIndicatorEdge::Bottom;
    VerticalAlignment vertical  = VerticalAlignment::Bottom;  // legacy Top / Bottom
    TextAlignment     horizontal = TextAlignment::Center;     // alignment along the strip
    int marginFromEdge = 12;     // distance from the chosen edge in pixels

    // Behavior
    bool clickable      = true;
    bool hoverHighlight = true;

    // Per-slide label text (used only when shape == Labels). If empty, falls back
    // to the slide's infoTitle. Indexed by slide index.
    std::vector<std::string> labels;
};
```

### SlideshowConfig

Top-level configuration applied with `SetConfig`.

```cpp
struct SlideshowConfig {
    std::chrono::milliseconds slideDuration{6000};   // time one slide stays before advancing
    std::chrono::milliseconds fadeDuration{600};     // time one transition takes

    SlideshowFadeStyle fadeStyle = SlideshowFadeStyle::CrossFade;
    SlideshowTextMode  textMode  = SlideshowTextMode::PerSlide;

    // Image fitting
    ImageFitMode     imageFit = ImageFitMode::Cover;      // Cover/Contain/Fill/ScaleDown/NoScale
    Point2Df         imageFocus{0.5f, 0.5f};              // focal point in 0..1 image space
    SlideshowGapFill gapFill = SlideshowGapFill::BackgroundColor;
    Color            letterboxColor = Color(0, 0, 0, 255);  // used when gapFill == LetterboxColor

    // Info panel layout
    SlideshowInfoLayout infoLayout = SlideshowInfoLayout::SplitRight;
    float imagePanelRatio       = 0.66f;   // SPLIT: image fraction along the split axis
    float overlaySizeRatio      = 0.34f;   // EDGE overlays: panel span along the overlay axis
    float overlayBoxWidthRatio  = 0.42f;   // CORNER/CENTER overlays: box width fraction
    float overlayBoxHeightRatio = 0.5f;    // CORNER/CENTER overlays: box height fraction
    int   overlayMargin         = 20;
    Color overlayScrimColor     = Color(0, 0, 0, 140);  // translucent backing behind overlay text

    bool autoPlay     = true;
    bool pauseOnHover = true;
    bool loop         = true;

    int animationFps = 60;   // animation-timer frame rate during a transition

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
```

## Examples

### Basic Timed Slideshow with an Info Panel

A minimal slideshow: create it, add slides (each with title and body), apply a config, and `Play()`. The default `SplitRight` layout shows the info panel beside the image, and `PerSlide` text mode fades the text with each slide.

```cpp
auto show = CreateSlideshow("hero-slideshow", 20, 84, 920, 480);

SlideshowConfig cfg;
cfg.slideDuration   = std::chrono::milliseconds(5000);
cfg.fadeDuration    = std::chrono::milliseconds(700);
cfg.fadeStyle       = SlideshowFadeStyle::CrossFade;
cfg.textMode        = SlideshowTextMode::PerSlide;
cfg.infoLayout      = SlideshowInfoLayout::SplitRight;
cfg.imagePanelRatio = 0.62f;
cfg.loop            = true;
cfg.pauseOnHover    = true;

show->AddSlide({"media/images/landscape.jpg",
                "Comfort redefined",
                "The interior blends comfort and design with thoughtful details."});
show->AddSlide({"media/images/portrait.jpg",
                "Design that stands out",
                "Striking lines meet aerodynamics for a distinctive look."});
show->AddSlide({"media/images/sample_photo.jpg",
                "Performance",
                "Confident acceleration and precise steering on every road."});

show->SetConfig(cfg);
show->Play();

show->onSlideChanged = [](size_t idx) {
    std::cerr << "Now showing slide " << (idx + 1) << std::endl;
};

container->AddChild(show);
```

### Bars Indicator

The default style: short pill/rectangle bars, one per slide, with the active slide highlighted.

```cpp
SlideshowIndicatorStyle style = show->GetIndicatorStyle();
style.shape        = SlideshowIndicatorShape::Bars;
style.itemWidth    = 30.0f;
style.itemHeight   = 3.0f;
style.spacing      = 6.0f;
style.cornerRadius = 0.0f;
style.activeColor  = Color(0, 122, 224, 255);
show->SetIndicatorStyle(style);
```

### Dots Indicator

Classic round dots — set `itemWidth` and `itemHeight` equal to get circles.

```cpp
SlideshowIndicatorStyle style = show->GetIndicatorStyle();
style.shape      = SlideshowIndicatorShape::Dots;
style.itemWidth  = 9.0f;
style.itemHeight = 9.0f;
style.spacing    = 10.0f;
show->SetIndicatorStyle(style);
```

### Progress Bar

A single bar that grows continuously across `slideDuration`, restarting each slide.

```cpp
SlideshowIndicatorStyle style = show->GetIndicatorStyle();
style.shape        = SlideshowIndicatorShape::ProgressBar;
style.itemWidth    = 30.0f;
style.itemHeight   = 3.0f;
style.cornerRadius = 1.5f;
show->SetIndicatorStyle(style);
```

### Story Bars

Instagram-Stories-style: one bar per slide, each filling over its own time slot.

```cpp
SlideshowIndicatorStyle style = show->GetIndicatorStyle();
style.shape        = SlideshowIndicatorShape::StoryBars;
style.itemWidth    = 30.0f;
style.itemHeight   = 3.0f;
style.spacing      = 4.0f;
style.cornerRadius = 1.5f;
show->SetIndicatorStyle(style);
```

### Counter

A plain text counter such as "3 / 5". The `textColor` field controls its color.

```cpp
SlideshowIndicatorStyle style = show->GetIndicatorStyle();
style.shape     = SlideshowIndicatorShape::Counter;
style.textColor = Color(255, 255, 255, 255);
show->SetIndicatorStyle(style);
```

### Thumbnails

A tiny preview of each slide's image, sized by `itemWidth` / `itemHeight`.

```cpp
SlideshowIndicatorStyle style = show->GetIndicatorStyle();
style.shape      = SlideshowIndicatorShape::Thumbnails;
style.itemWidth  = 60.0f;
style.itemHeight = 40.0f;
style.spacing    = 8.0f;
show->SetIndicatorStyle(style);
```

### Labels

A per-slide text label with a tab-style underline on the active slide. Supply `labels` (indexed by slide); if a label is empty, the slide's `infoTitle` is used as a fallback.

```cpp
SlideshowIndicatorStyle style = show->GetIndicatorStyle();
style.shape     = SlideshowIndicatorShape::Labels;
style.itemWidth = 80.0f;
style.spacing   = 4.0f;
style.labels    = { "Comfort", "Design", "Performance" };
show->SetIndicatorStyle(style);
```

### Choosing the Indicator Edge

`edge` is the primary placement control. Top/Bottom lay the indicators out in a row; Left/Right stack them in a column.

```cpp
SlideshowIndicatorStyle style = show->GetIndicatorStyle();
style.edge           = SlideshowIndicatorEdge::Top;
style.marginFromEdge = 18;
show->SetIndicatorStyle(style);
```

### Manual Navigation Controls

`Play`/`Pause`, `Next`/`Previous`, and `GoTo` drive the slideshow from buttons. The widget also accepts focus on click so the arrow keys work (Left/Down = previous, Right/Up = next).

```cpp
auto prevBtn = std::make_shared<UltraCanvasButton>("prev", 20, 600, 90, 26, "< Previous");
prevBtn->SetOnClick([show]() { show->Previous(); });

auto nextBtn = std::make_shared<UltraCanvasButton>("next", 120, 600, 90, 26, "Next >");
nextBtn->SetOnClick([show]() { show->Next(); });

auto* showPtr   = show.get();
auto  pauseBtn  = std::make_shared<UltraCanvasButton>("playpause", 220, 600, 70, 26, "Pause");
auto* pausePtr  = pauseBtn.get();
pauseBtn->SetOnClick([showPtr, pausePtr]() {
    if (showPtr->IsPlaying()) {
        showPtr->Pause();
        pausePtr->SetText("Play");
    } else {
        showPtr->Play();
        pausePtr->SetText("Pause");
    }
});

container->AddChild(prevBtn);
container->AddChild(nextBtn);
container->AddChild(pauseBtn);
```

### Overlay Layout with a Scrim

Overlay layouts float the panel over a full-bleed image. Use `SlideshowLayoutIsOverlay` to pick light text for overlay scrims versus dark text for the opaque split panel.

```cpp
SlideshowConfig cfg = show->GetConfig();
cfg.infoLayout       = SlideshowInfoLayout::OverlayBottom;
cfg.overlaySizeRatio = 0.34f;
cfg.overlayScrimColor = Color(0, 0, 0, 140);
if (SlideshowLayoutIsOverlay(cfg.infoLayout)) {
    cfg.titleColor = Color(255, 255, 255, 255);
    cfg.bodyColor  = Color(235, 235, 235, 255);
}
show->SetConfig(cfg);
show->Play();
```

### Image Fitting and Gap Fill

Choose how mismatched images are scaled into the image area, set the crop focal point (used by `Cover`), and choose what fills the gaps left by letterboxed fits.

```cpp
SlideshowConfig cfg = show->GetConfig();
cfg.imageFit   = ImageFitMode::Cover;        // auto-zoom + auto-crop to fill
cfg.imageFocus = Point2Df(0.5f, 0.5f);       // keep the center when cropping
cfg.gapFill    = SlideshowGapFill::BlurredImage;  // blurred copy behind letterboxing
show->SetConfig(cfg);
show->Play();
```

### Static Text Mode

When every slide should share the same caption, use `Static` text mode and `SetStaticText`.

```cpp
SlideshowConfig cfg = show->GetConfig();
cfg.textMode = SlideshowTextMode::Static;
show->SetConfig(cfg);
show->SetStaticText("Our Gallery", "A curated selection of recent work.");
```

## Best Practices

- **Add slides before calling `Play()`**, then apply your `SlideshowConfig` with `SetConfig` so layout, fitting, and timing are all in place before the first transition.
- **Read-modify-write config.** `SetConfig` / `SetIndicatorStyle` replace the whole struct, so fetch the current value with `GetConfig()` / `GetIndicatorStyle()`, change only the fields you need, and set it back — this preserves unrelated settings.
- **Match text color to the layout.** Use `SlideshowLayoutIsOverlay(layout)` to choose light text for overlay scrims and dark text for the opaque split panel.
- **Use `Cover` with a focal point** for hero images where you never want gaps; reserve `Contain` / `ScaleDown` (plus a `gapFill`) for galleries where the whole image must always be visible.
- **Equal `itemWidth` and `itemHeight`** make `Dots` round; set `cornerRadius` to `itemHeight / 2` for pill-shaped `Bars`.
- **`ProgressBar` and `StoryBars` redraw continuously** to animate their fill, so they cost slightly more than the static indicators — prefer `Bars` or `Dots` when battery or CPU is a concern.
- **Keep the `onSlideChanged` callback cheap** and capture the widget by raw or weak pointer (not a strong `shared_ptr`) to avoid reference cycles.
- **Let users pause.** Keep `pauseOnHover` enabled and expose `Play`/`Pause` controls so readers can stop on a slide they want to read.

## See Also

- [Apps/DemoApp/UltraCanvasSlideshowExamples.cpp](../../Apps/DemoApp/UltraCanvasSlideshowExamples.cpp) - Full interactive demo of every indicator style, layout, fade, and fit
- [UltraCanvasSlideshow.h](../../UltraCanvas/include/UltraCanvasSlideshow.h) - Public API header
- [UltraCanvasContainer](UltraCanvasContainer.md) - Base class
- [UltraCanvasButton](UltraCanvasButton.md) - For playback / navigation controls
- [UltraCanvasLabel](UltraCanvasLabel.md) - Companion text element
