# UltraCanvasWordCloudElement Documentation

**Version:** 1.0.0
**Last Modified:** 2026-07-22
**Author:** UltraCanvas Framework

## Overview

`UltraCanvasWordCloudElement` renders word clouds / word maps: a set of words sized by weight (frequency, importance, revenue…) and packed together without overlaps. The layout engine uses **greedy spiral placement over an occupancy grid** — the same family of algorithms used by wordcloud2.js, ECharts wordCloud and the Python `wordcloud` package. Its option set was designed as the superset of the options offered by the major word-cloud libraries (see the comparison at the end of this document), including:

- weight → font-size scaling functions (sqrt / linear / log / rank, with a rank↔frequency blend),
- geometric cloud shapes (ellipse, circle, cardioid, diamond, triangle, pentagon, star…),
- **mask images** (the cloud fills the silhouette of an image, Python-wordcloud style),
- a **center image** — a logo or photo drawn in the middle of the cloud with the words flowing around it,
- rotation schemes, color strategies (palette, gradient by weight, color sampled from an image, per-word), raw-text frequency analysis with stopwords and bigram detection, hover/click/tooltip interactivity, and a staggered fade-in animation.

Layouts are **deterministic**: the same input with the same `randomSeed` always produces the same cloud.

**Namespace:** `UltraCanvas`
**Header:** `include/Plugins/Diagrams/UltraCanvasWordCloudDiagram.h`
**Implementation:** `Plugins/Diagrams/UltraCanvasWordCloudDiagram.cpp`
**Base Class:** `UltraCanvasChartElementBase`

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasChartElementBase
            └── UltraCanvasWordCloudElement
```

## Header Include

```cpp
#include "Plugins/Diagrams/UltraCanvasWordCloudDiagram.h"
```

## Quick Start

```cpp
auto cloud = CreateWordCloud("cloud", 0, 0, 800, 600);
cloud->AddWord("Python", 100);
cloud->AddWord("JavaScript", 82);
cloud->AddWord("C++", 70);
// ... or in one call:
cloud->SetWords({{"Rust", 46}, {"Go", 44}, {"Kotlin", 34}});
window->AddChild(cloud);
```

Or build the cloud from raw prose:

```cpp
WordCloudTextOptions opts;
opts.maxWords = 100;
opts.minWordLength = 3;
opts.detectBigrams = true;       // "word cloud" becomes one entry
cloud->SetWordsFromText(longText, opts);
```

## The Center Image

The signature feature: an image is drawn in the middle of the cloud and its
region is removed from the layout grid, so words flow around it.

```cpp
WordCloudCenterImage logo;
logo.imagePath   = "media/images/UltraCanvas-logo.png";
logo.sizeRatio   = 0.34f;                       // 34% of min(width, height)
logo.padding     = 12.0f;                       // clear space around the image
logo.blockMode   = WordCloudCenterBlock::BoundingBox;
logo.clipToCircle = false;                      // true = round avatar crop
logo.drawBorder  = false;
cloud->SetCenterImage(logo);
```

`WordCloudCenterBlock` controls the cutout that words must avoid:

| Mode | Effect |
|---|---|
| `BoundingBox` | The full image rectangle plus `padding` is kept free. |
| `Ellipse` | Only the inscribed ellipse is blocked — words hug a round logo. |
| `AlphaPixels` | Only opaque pixels are blocked — words flow *into* transparent parts of a PNG. |

Additional fields: `pixelWidth`/`pixelHeight` (explicit size instead of `sizeRatio`), `offset` (shift from the cloud center), `opacity`, `cornerRadius` (rounded-rect crop), `borderColor`/`borderWidth`.

## Mask Images (shape from an image)

A mask constrains word placement to the silhouette of an image, following the
Python `wordcloud` convention: **transparent or near-white pixels are
forbidden**, everything else is allowed.

```cpp
cloud->SetMaskImage("media/images/heart.png");
cloud->SetMaskThresholds(128 /*alpha*/, 250 /*white*/);
cloud->SetMaskInvert(false);          // true = place words OUTSIDE the shape
cloud->SetShowMaskImage(true, 0.12f); // faint ghost of the image underneath
```

A mask always overrides the geometric `SetShape()` setting. `SetMaskInvert(true)` combined with a centered silhouette is another way to get the "words around an image" effect.

## Full Option Reference

### Words

```cpp
void AddWord(const std::string& text, double weight);
void AddWord(const WordCloudWord& word);
void SetWords(const std::vector<WordCloudWord>& words);
void SetWordsFromText(const std::string& text, const WordCloudTextOptions& options = {});
void ClearWords();
```

`WordCloudWord` per-word overrides (all optional): `color`, `rotationDeg`, `fontFamily`, `fontWeight` (+`hasFontWeight`), `tooltip` (extra tooltip line), `userTag` (free-form tag — URL, id — passed to callbacks).

`WordCloudTextOptions`: `maxWords` (100), `minWordLength` (2), `minFrequency` (1), `toLowercase` (true), `includeNumbers` (false), `useDefaultStopwords` (true, built-in English list), `extraStopwords`, `detectBigrams` (false), `minBigramCount` (3).

### Sizing

```cpp
void SetScaling(WordCloudScaling s);          // Linear, SquareRoot*, Logarithmic, Rank
void SetFontSizeRange(float minSize, float maxSize);   // default 10..72
void SetRelativeScaling(float rs);            // 0 = rank-based, 1 = frequency-based, default 0.5
void SetMaxWords(int maxWords);               // layout cap, default 250
```

### Fonts

```cpp
void SetFontFamily(const std::string& family);   // default "Arial"
void SetFontWeight(FontWeight weight);           // default Bold
void SetFontSlant(FontSlant slant);
```

### Rotation

```cpp
void SetRotationMode(WordCloudRotationMode mode); // NoRotation, Steps*, RandomRange, PerWordOnly
void SetRotationRange(float minDeg, float maxDeg);// default -90..90
void SetRotationSteps(int steps);                 // discrete angles in range, default 3
void SetRotationAngles(const std::vector<float>& angles); // explicit list, e.g. {0, 90}
void SetRotateRatio(float ratio);                 // probability a word rotates, default 0.35
```

### Colors

```cpp
void SetColorMode(WordCloudColorMode mode);
// Palette*          cycle the palette in placement order
// RandomFromPalette random palette pick per word (seeded)
// ByWeightGradient  interpolate low→high color by weight
// SingleColor       one color for every word
// FromImage         average image color under each word (Python ImageColorGenerator)
void SetColorPalette(const std::vector<Color>& palette);
void SetWordColor(const Color& color);
void SetColorGradient(const Color& low, const Color& high);
void SetColorImage(const std::string& path);   // FromImage source; falls back to
                                               // mask image, then center image
void Recolor();                                // reassign colors WITHOUT re-layout
```

### Layout

```cpp
void SetShape(WordCloudShape s);      // Rectangle, Ellipse*, Circle, Cardioid, Diamond,
                                      // Square, Triangle, TriangleForward, Pentagon, Star
void SetSpiral(WordCloudSpiral s);    // Archimedean*, Rectangular
void SetGridSize(int px);             // collision cell size, default 4 (smaller = tighter+slower)
void SetWordPadding(float px);        // min space between words, default 2
void SetEllipticity(float e);         // vertical flattening, default 0.85
void SetSpiralStep(float step);       // spiral growth per turn, default 3
void SetCloudCenter(float fx, float fy);  // normalized origin, default 0.5, 0.5
void SetShrinkToFit(bool enable);     // shrink words that don't fit, default true
void SetDrawOutOfBound(bool enable);  // allow clipping at edges, default false
void SetShuffleWords(bool enable);    // random placement order, default false (weight desc)
void SetRepeatWords(bool enable);     // repeat words (down-weighted) to fill space
void SetRandomSeed(uint32_t seed);    // deterministic layout, default 1
```

### Interactivity

```cpp
void SetEnableHover(bool enable);     // highlight word under cursor (default on)
void SetHoverColor(const Color& color);
void SetHoverBold(bool bold);
void SetOnWordClick(std::function<void(const WordCloudPlacedWord&)>);
void SetOnWordHover(std::function<void(const WordCloudPlacedWord&)>);
void SetOnLayoutComplete(std::function<void(size_t placed, size_t total)>);
void SetWordTooltipGenerator(std::function<std::string(const WordCloudPlacedWord&)>);
void SetEnableTooltips(bool);         // inherited; default tooltip = word + weight
```

### Layout results

```cpp
const std::vector<WordCloudPlacedWord>& GetPlacedWords() const;
size_t GetPlacedWordCount() const;    // words that fit
size_t GetSkippedWordCount() const;   // words that did not fit
void RelayoutNow();                   // force a fresh layout on next paint
```

`WordCloudPlacedWord` exposes `text`, `weight`, `fontSize`, `rotationDeg`, `center`, `textSize`, `bounds`, `color`, `placed` and `sourceIndex` into the input list.

### Animation

The base class animation is used for a staggered fade-in (heaviest words first). Disable with the inherited `animationEnabled = false` behavior (`SetTitle`, `SetBackgroundColor` etc. also come from `UltraCanvasChartElementBase`).

## Layout Algorithm

1. **Grid seeding.** The plot area is divided into `gridSize`-pixel cells. Cells outside the geometric shape (or outside the mask-image silhouette, or covered by the center image + padding) are marked occupied before any word is placed. This unified seed is what makes shape masks, image masks and the center-image cutout all behave identically.
2. **Ordering.** Words are sorted heaviest-first (optionally shuffled), capped at `maxWords`, optionally repeated with halved weights (`SetRepeatWords`).
3. **Sizing.** Each word's font size comes from the scaling function blended with its rank (`relativeScaling`), mapped onto `[minFontSize, maxFontSize]`.
4. **Placement.** The word's rotated bounding box (plus `wordPadding`) walks an Archimedean or rectangular spiral outward from the cloud center; the first position whose grid cells are all free wins, and those cells are marked. If no position fits, the font shrinks by `shrinkStep` and retries until `minFontSize` (`shrinkToFit`), otherwise the word is reported as skipped.
5. **Painting.** Background → optional mask underlay → center image (with optional circular/rounded clip and border) → words (rotated around their centers).

Collision granularity is the rotated axis-aligned bounding box on the grid — the same trade-off wordcloud2.js makes. Pixel-perfect glyph collision (d3-cloud sprite masks) is a possible future upgrade; the public API would not change.

## Word Cloud Library Research

The option set of this element was compiled from a survey of the major word cloud libraries:

| Library | Layout algorithm | Signature options adopted here |
|---|---|---|
| **wordcloud2.js** | occupancy grid + polar shape functions | `gridSize`, shapes (circle/cardioid/diamond/triangle/pentagon/star), `ellipticity`, `minSize`, `rotateRatio`+`rotationSteps`, `shrinkToFit`, `drawOutOfBound`, `shuffle`, hover/click |
| **d3-cloud** | sprite masks + archimedean/rectangular spiral | spiral types, per-word accessors (→ per-word overrides), seeded determinism |
| **Python wordcloud** | integral-image occupancy | `mask` (white=forbidden), `relative_scaling`, `prefer_horizontal`, `repeat`, stopwords/collocations, `recolor()` without re-layout, `ImageColorGenerator` (→ `FromImage` color mode) |
| **ECharts wordCloud** | wordcloud2.js grid | `maskImage`, `sizeRange`, `rotationRange`/`rotationStep`, per-datum textStyle |
| **Highcharts wordcloud** | greedy + pluggable spiral | rotation `{from,to,orientations}`, placement strategy, min/max font size |
| **amCharts 5** | spiral point sets + pixel buffer | built-in text→frequency analysis, explicit `angles[]` list, auto-fit shrink loop |
| **Kumo (Java)** | pixel-perfect or rectangle collision | `CircleBackground`/`PixelBoundaryBackground` (→ shapes + mask), linear/sqrt/log font scalars, `AngleGenerator`, frequency analyzer |
| **WordCram (Processing)** | pluggable placers/nudgers | per-word setSize/setAngle/setColor overrides, skipped-word reporting |
| **WordArt.com** | proprietary | shape-image behind words with transparency (→ center image + mask underlay), per-word color/angle/font/URL |
| **jQCloud / react-wordcloud / react-tagcloud** | flow / d3 wrapper | weight buckets, deterministic `randomSeed`, tooltip formatter callbacks |

Center-image techniques in the wild, for reference: wordcloud2.js achieves it with `clearCanvas:false` (pre-drawn canvas pixels count as occupied), Python wordcloud with a white-center mask + post-composite, Kumo with `PixelBoundaryBackground`, WordArt with "shape image behind words". `UltraCanvasWordCloudElement` implements it as a first-class `WordCloudCenterImage` option with three cutout modes.

## Complete Example

```cpp
auto cloud = CreateWordCloud("skills", 0, 0, 900, 640);
cloud->SetTitle("Team Skills 2026");
cloud->SetWords(skillData);

// Looks
cloud->SetShape(WordCloudShape::Ellipse);
cloud->SetFontFamily("Ubuntu");
cloud->SetFontSizeRange(12, 68);
cloud->SetScaling(WordCloudScaling::SquareRoot);
cloud->SetColorMode(WordCloudColorMode::ByWeightGradient);
cloud->SetColorGradient(Color(180, 190, 200, 255), Color(220, 53, 69, 255));
cloud->SetRotationAngles({0.0f, 90.0f});
cloud->SetRotateRatio(0.25f);

// Center logo, words flow around the opaque pixels
WordCloudCenterImage logo;
logo.imagePath = NormalizePath(GetResourcesDir() + "media/images/UltraCanvas-logo.png");
logo.sizeRatio = 0.3f;
logo.blockMode = WordCloudCenterBlock::AlphaPixels;
logo.padding = 10.0f;
cloud->SetCenterImage(logo);

// Interactivity
cloud->SetOnWordClick([](const WordCloudPlacedWord& w) {
    printf("clicked %s (weight %.0f)\n", w.text.c_str(), w.weight);
});
cloud->SetOnLayoutComplete([](size_t placed, size_t total) {
    printf("placed %zu of %zu words\n", placed, total);
});
```

## Demo

See `Apps/DemoApp/UltraCanvasWordCloudExamples.cpp` — tabs for a frequency cloud, the center-image cloud, raw-text tokenization with bigrams, and a star-shaped cloud with repeat fill, plus live controls for shape, colors, rotation and scaling.
