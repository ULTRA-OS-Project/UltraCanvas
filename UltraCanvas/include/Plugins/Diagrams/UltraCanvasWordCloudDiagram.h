// include/Plugins/Diagrams/UltraCanvasWordCloudDiagram.h
// Word cloud / word map diagram element with greedy spiral layout,
// occupancy-grid collision detection, frequency-based sizing, shape masks,
// image masks and an optional center image with words flowing around it.
// Option set modelled on the union of wordcloud2.js, d3-cloud, the Python
// `wordcloud` package, ECharts wordCloud, Highcharts wordcloud and Kumo.
// Version: 1.0.0
// Last Modified: 2026-07-22
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Charts/UltraCanvasChartElementBase.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasTimer.h"
#include <vector>
#include <string>
#include <memory>
#include <unordered_set>
#include <functional>
#include <random>
#include <cmath>
#include <limits>

namespace UltraCanvas {

// =============================================================================
// WORD CLOUD DATA STRUCTURES
// =============================================================================

// One input word. Only `text` and `weight` are required; everything else is
// an optional per-word override of the cloud-wide settings (the pattern used
// by WordArt.com and ECharts per-datum styling).
struct WordCloudWord {
    std::string text;
    double weight = 1.0;

    // Per-word style overrides. Transparent color / NaN rotation = "use the
    // cloud-wide color mode / rotation mode for this word".
    Color color = Colors::Transparent;
    float rotationDeg = std::numeric_limits<float>::quiet_NaN();
    std::string fontFamily;                       // empty = cloud font
    FontWeight fontWeight = FontWeight::Normal;
    bool hasFontWeight = false;

    std::string tooltip;                          // extra tooltip line
    std::string userTag;                          // free-form tag (URL, id...) passed to callbacks

    WordCloudWord() = default;
    WordCloudWord(const std::string& t, double w) : text(t), weight(w) {}

    bool HasExplicitColor() const { return color.a != 0; }
    bool HasExplicitRotation() const { return !std::isnan(rotationDeg); }
};

// Layout result for one word after PerformLayout().
struct WordCloudPlacedWord {
    size_t sourceIndex = 0;       // index into the input word list
    std::string text;
    double weight = 0.0;
    float fontSize = 0.0f;
    float rotationDeg = 0.0f;
    Point2Dd center;              // element-local center of the word
    Size2Di textSize;             // unrotated text layout size
    Rect2Dd bounds;               // axis-aligned bounds of the rotated word
    Color color;
    bool placed = false;          // false = did not fit and was skipped
};

// =============================================================================
// WORD CLOUD OPTION ENUMS
// =============================================================================

// Region the words are constrained to (wordcloud2.js `shape`). When a mask
// image is set it always wins over the geometric shape.
enum class WordCloudShape {
    Rectangle,          // whole plot area (d3-cloud default)
    Ellipse,            // ellipse filling the plot area
    Circle,             // largest centered circle
    Cardioid,           // heart-like curve
    Diamond,
    Square,             // centered square
    Triangle,           // upright triangle
    TriangleForward,    // right-pointing triangle
    Pentagon,
    Star
};

// Search path used when looking for a free spot (d3-cloud `spiral`).
enum class WordCloudSpiral {
    Archimedean,        // round spiral, organic look (default everywhere)
    Rectangular         // rectangular spiral, tighter packing in corners
};

// weight -> font size mapping (Highcharts weight, Kumo FontScalar,
// Python relative_scaling family).
enum class WordCloudScaling {
    Linear,             // size ∝ weight
    SquareRoot,         // size ∝ sqrt(weight)  (default; tames outliers)
    Logarithmic,        // size ∝ log(1 + weight)
    Rank                // size from rank order only, ignores magnitudes
};

// How rotation angles are chosen (wordcloud2.js minRotation/maxRotation/
// rotationSteps, Highcharts rotation.orientations, Python prefer_horizontal).
enum class WordCloudRotationMode {
    NoRotation,         // all words horizontal
    Steps,              // discrete angles: `rotationSteps` values in [min,max]
    RandomRange,        // uniform random angle in [min,max]
    PerWordOnly         // only words with an explicit rotationDeg are rotated
};

// Word coloring strategy.
enum class WordCloudColorMode {
    Palette,            // cycle through the palette in placement order
    RandomFromPalette,  // random palette entry per word (seeded)
    ByWeightGradient,   // interpolate gradientLowColor..gradientHighColor by weight
    SingleColor,        // every word in defaultWordColor
    FromImage           // sample color under the word from the color/mask image
                        // (Python ImageColorGenerator / WordArt style)
};

// Which area the center image blocks out of the layout.
enum class WordCloudCenterBlock {
    BoundingBox,        // full image rectangle + padding
    Ellipse,            // inscribed ellipse + padding (words hug a round logo)
    AlphaPixels         // only opaque pixels + padding (words flow into
                        // transparent parts of the image)
};

// =============================================================================
// CENTER IMAGE OPTIONS
// =============================================================================

// Optional image drawn in the middle of the cloud. The covered region is
// removed from the layout grid so words flow around the image.
struct WordCloudCenterImage {
    std::string imagePath;                    // file path (loaded via UCImageRaster)

    // Size: fraction of min(plotWidth, plotHeight); explicit pixel size wins
    // when non-zero. Aspect ratio of the source image is always preserved.
    float sizeRatio = 0.30f;
    int pixelWidth = 0;
    int pixelHeight = 0;

    Point2Dd offset;                          // shift from the cloud center, px
    float padding = 10.0f;                    // free space kept around the image
    WordCloudCenterBlock blockMode = WordCloudCenterBlock::BoundingBox;

    float opacity = 1.0f;                     // 0..1
    bool clipToCircle = false;                // round avatar-style crop
    float cornerRadius = 0.0f;                // rounded-rect crop when > 0
    bool drawBorder = false;
    Color borderColor = Colors::White;
    float borderWidth = 3.0f;

    bool IsValid() const { return !imagePath.empty(); }
};

// =============================================================================
// TEXT TOKENIZER OPTIONS
// =============================================================================

// Options for SetWordsFromText() - building the cloud from raw prose the way
// the Python wordcloud package does (stopwords, collocations, normalization).
struct WordCloudTextOptions {
    int maxWords = 100;                       // keep the N most frequent words
    int minWordLength = 2;                    // drop shorter tokens
    int minFrequency = 1;                     // drop rarer tokens
    bool toLowercase = true;                  // case-fold before counting
    bool includeNumbers = false;              // keep pure-number tokens
    bool useDefaultStopwords = true;          // built-in English stopword list
    std::unordered_set<std::string> extraStopwords;   // user additions
    bool detectBigrams = false;               // count frequent word pairs as one
                                              // entry ("new york") - collocations
    int minBigramCount = 3;                   // pair must occur this often
};

// =============================================================================
// MAIN WORD CLOUD ELEMENT
// =============================================================================

class UltraCanvasWordCloudElement : public UltraCanvasChartElementBase {
private:
    // ===== Input data =====
    std::vector<WordCloudWord> words;

    // ===== Sizing / scaling =====
    WordCloudScaling scaling = WordCloudScaling::SquareRoot;
    float minFontSize = 10.0f;                // wordcloud2.js minSize
    float maxFontSize = 72.0f;
    float relativeScaling = 0.5f;             // 0 = rank only, 1 = frequency only
                                              // (Python relative_scaling)
    int layoutMaxWords = 250;                 // hard cap on words laid out

    // ===== Fonts =====
    std::string fontFamily = "Arial";
    FontWeight fontWeight = FontWeight::Bold;
    FontSlant fontSlant = FontSlant::Normal;

    // ===== Rotation =====
    WordCloudRotationMode rotationMode = WordCloudRotationMode::Steps;
    float minRotationDeg = -90.0f;
    float maxRotationDeg = 90.0f;
    int rotationSteps = 3;                    // Steps mode: N discrete angles
    std::vector<float> rotationAngles;        // explicit angle list; when set it
                                              // wins over min/max/steps (amCharts
                                              // `angles`, WordCram angledAt)
    float rotateRatio = 0.35f;                // probability a word is rotated
                                              // (1 - prefer_horizontal)

    // ===== Colors =====
    WordCloudColorMode colorMode = WordCloudColorMode::Palette;
    Color defaultWordColor = Color(52, 58, 64, 255);
    Color gradientLowColor = Color(173, 181, 189, 255);   // lightest weight
    Color gradientHighColor = Color(13, 110, 253, 255);   // heaviest weight
    std::vector<Color> colorPalette = {
        Color(13, 110, 253, 255),    // blue
        Color(220, 53, 69, 255),     // red
        Color(25, 135, 84, 255),     // green
        Color(255, 143, 28, 255),    // orange
        Color(111, 66, 193, 255),    // purple
        Color(32, 178, 170, 255),    // teal
        Color(214, 51, 132, 255),    // pink
        Color(73, 80, 87, 255)       // slate
    };
    std::string colorImagePath;               // FromImage mode source; falls
                                              // back to mask image when empty

    // ===== Layout =====
    WordCloudShape shape = WordCloudShape::Ellipse;
    WordCloudSpiral spiral = WordCloudSpiral::Archimedean;
    int gridSize = 4;                         // collision grid cell, px
    float wordPadding = 2.0f;                 // min space between words, px
    float ellipticity = 0.85f;                // vertical flattening of spiral/shape
    float spiralStep = 3.0f;                  // radial growth per turn, px-ish
    Point2Dd cloudCenter = Point2Dd(0.5, 0.5);// normalized origin in plot area
    bool shrinkToFit = true;                  // retry smaller until it fits
    float shrinkStep = 2.0f;                  // font px removed per retry
    bool drawOutOfBound = false;              // allow words to clip plot edges
    bool shuffleWords = false;                // randomize placement order
                                              // (default: by weight, big first)
    bool repeatWords = false;                 // repeat words (down-weighted) to
                                              // fill space (Python `repeat`)
    uint32_t randomSeed = 1;                  // deterministic layouts; same
                                              // seed = same cloud

    // ===== Mask image (shape from image, Python-wordcloud style) =====
    std::string maskImagePath;
    bool maskInvert = false;                  // place words OUTSIDE the mask
    uint8_t maskAlphaThreshold = 128;         // opaque enough to count as mask
    uint8_t maskWhiteThreshold = 250;         // near-white RGB counts as background
    bool showMaskImage = false;               // draw the mask under the words
    float maskImageOpacity = 0.12f;

    // ===== Center image =====
    WordCloudCenterImage centerImage;

    // ===== Interactivity =====
    bool enableHover = true;
    Color hoverColor = Color(255, 193, 7, 255);
    bool hoverBold = true;
    size_t hoveredWordIndex = SIZE_MAX;       // index into placedWords

    // ===== Callbacks =====
    std::function<void(const WordCloudPlacedWord&)> onWordClick;
    std::function<void(const WordCloudPlacedWord&)> onWordHover;
    std::function<void(size_t placedCount, size_t totalCount)> onLayoutComplete;
    std::function<std::string(const WordCloudPlacedWord&)> tooltipGenerator;

    // ===== Layout state =====
    std::vector<WordCloudPlacedWord> placedWords;
    bool layoutDirty = true;
    Size2Di lastLayoutSize;
    std::mt19937 rng;
    TimerId animationTimerId = 0;   // periodic frame driver for the fade-in

    // Occupancy grid: 1 byte per cell, non-zero = blocked.
    std::vector<uint8_t> occupancy;
    int gridCols = 0;
    int gridRows = 0;
    Rect2Dd plotRect;                         // area words are placed into
    Rect2Dd centerImageRect;                  // resolved on layout

    // Pixmaps resolved during layout
    UCPixmapPtr maskPixmap;
    UCPixmapPtr centerPixmap;
    UCPixmapPtr colorPixmap;

public:
    UltraCanvasWordCloudElement(const std::string& id, int x, int y, int width, int height)
        : UltraCanvasChartElementBase(id, x, y, width, height) {
        enableTooltips = true;
        backgroundColor = Colors::White;
    }

    virtual ~UltraCanvasWordCloudElement();

    // ===== WORD MANAGEMENT =====

    void AddWord(const std::string& text, double weight);
    void AddWord(const WordCloudWord& word);
    void SetWords(const std::vector<WordCloudWord>& newWords);
    // Tokenize raw text (stopwords, frequency counting, optional bigrams)
    // and use the resulting frequency table as the word list.
    void SetWordsFromText(const std::string& text,
                          const WordCloudTextOptions& options = WordCloudTextOptions());
    void ClearWords();
    size_t GetWordCount() const { return words.size(); }
    const std::vector<WordCloudWord>& GetWords() const { return words; }

    // ===== SIZING / SCALING =====

    void SetScaling(WordCloudScaling s);
    WordCloudScaling GetScaling() const { return scaling; }
    void SetFontSizeRange(float minSize, float maxSize);
    float GetMinFontSize() const { return minFontSize; }
    float GetMaxFontSize() const { return maxFontSize; }
    // 0 = purely rank-based sizes, 1 = purely frequency-proportional.
    void SetRelativeScaling(float rs);
    void SetMaxWords(int maxWords);
    int GetMaxWords() const { return layoutMaxWords; }

    // ===== FONTS =====

    void SetFontFamily(const std::string& family);
    const std::string& GetFontFamily() const { return fontFamily; }
    void SetFontWeight(FontWeight weight);
    void SetFontSlant(FontSlant slant);

    // ===== ROTATION =====

    void SetRotationMode(WordCloudRotationMode mode);
    WordCloudRotationMode GetRotationMode() const { return rotationMode; }
    void SetRotationRange(float minDeg, float maxDeg);
    void SetRotationSteps(int steps);
    // Explicit list of allowed angles, e.g. {0, 90} or {0, -45, 45}.
    // Overrides the range/steps configuration; empty list restores it.
    void SetRotationAngles(const std::vector<float>& angles);
    // Fraction of words that get a non-zero angle (0 = all horizontal).
    void SetRotateRatio(float ratio);
    float GetRotateRatio() const { return rotateRatio; }

    // ===== COLORS =====

    void SetColorMode(WordCloudColorMode mode);
    WordCloudColorMode GetColorMode() const { return colorMode; }
    void SetColorPalette(const std::vector<Color>& palette);
    void SetWordColor(const Color& color);                 // SingleColor mode
    void SetColorGradient(const Color& low, const Color& high);
    // Image sampled for FromImage mode; when empty the mask image is used.
    void SetColorImage(const std::string& path);

    // ===== LAYOUT =====

    void SetShape(WordCloudShape s);
    WordCloudShape GetShape() const { return shape; }
    void SetSpiral(WordCloudSpiral s);
    void SetGridSize(int px);
    void SetWordPadding(float px);
    void SetEllipticity(float e);
    void SetSpiralStep(float step);
    // Normalized (0..1, 0..1) placement origin inside the plot area.
    void SetCloudCenter(float fx, float fy);
    void SetShrinkToFit(bool enable);
    void SetDrawOutOfBound(bool enable);
    void SetShuffleWords(bool enable);
    void SetRepeatWords(bool enable);
    void SetRandomSeed(uint32_t seed);

    // ===== MASK IMAGE =====

    // Words are only placed where the image has opaque, non-white pixels
    // (the Python wordcloud mask convention). Overrides SetShape().
    void SetMaskImage(const std::string& path);
    void ClearMaskImage();
    void SetMaskInvert(bool invert);
    void SetMaskThresholds(uint8_t alphaThreshold, uint8_t whiteThreshold);
    // Draw the mask image itself (faint) underneath the words.
    void SetShowMaskImage(bool show, float opacity = 0.12f);

    // ===== CENTER IMAGE =====

    void SetCenterImage(const WordCloudCenterImage& image);
    void SetCenterImage(const std::string& path, float sizeRatio = 0.30f,
                        WordCloudCenterBlock blockMode = WordCloudCenterBlock::BoundingBox);
    void ClearCenterImage();
    const WordCloudCenterImage& GetCenterImage() const { return centerImage; }

    // ===== INTERACTIVITY =====

    void SetEnableHover(bool enable);
    void SetHoverColor(const Color& color);
    void SetHoverBold(bool bold);
    void SetOnWordClick(std::function<void(const WordCloudPlacedWord&)> callback);
    void SetOnWordHover(std::function<void(const WordCloudPlacedWord&)> callback);
    void SetOnLayoutComplete(std::function<void(size_t, size_t)> callback);
    void SetWordTooltipGenerator(std::function<std::string(const WordCloudPlacedWord&)> generator);

    // ===== LAYOUT RESULTS =====

    const std::vector<WordCloudPlacedWord>& GetPlacedWords() const { return placedWords; }
    size_t GetPlacedWordCount() const;
    size_t GetSkippedWordCount() const;
    void RelayoutNow() { InvalidateLayout(); RequestRedraw(); }
    // Reassign colors using the current color settings WITHOUT recomputing
    // the layout (Python wordcloud recolor()).
    void Recolor();

    // ===== RENDERING OVERRIDES =====

    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
    void RenderChart(IRenderContext* ctx) override;
    bool HandleChartMouseMove(const Point2Di& mousePos) override;
    bool OnEvent(const UCEvent& event) override;

private:
    // ===== LAYOUT PIPELINE =====

    void InvalidateLayout();
    void PerformLayout(IRenderContext* ctx);
    void PrepareOccupancyGrid();
    void ApplyShapeToGrid();
    void ApplyMaskImageToGrid();
    void ApplyCenterImageToGrid();
    void DilateBlockedCells(std::vector<uint8_t>& cells, int radiusCells);
    bool TryPlaceWord(IRenderContext* ctx, WordCloudPlacedWord& placed,
                      const WordCloudWord& word, float fontSize, float rotationDeg);
    bool RegionIsFree(int gx0, int gy0, int gx1, int gy1) const;
    void MarkRegion(int gx0, int gy0, int gx1, int gy1);

    float ComputeFontSize(double weight, double minW, double maxW, size_t rank, size_t count) const;
    float PickRotation(const WordCloudWord& word);
    Color PickColor(const WordCloudWord& word, size_t orderIndex,
                    double normWeight, const Point2Dd& center);
    Color SampleImageColor(const Point2Dd& center) const;
    bool ShapeAllowsPoint(double px, double py) const;   // px,py in plot coords
    double ShapeRadiusFactor(double theta) const;        // polar r(θ), max 1

    void ResolveCenterImage();
    void ResolveMaskImage();
    void ResolveColorImage();

    // ===== ANIMATION =====

    void StartFadeInAnimation();
    void StopAnimationTimer();

    // ===== RENDERING =====

    void RenderTitle(IRenderContext* ctx);
    void RenderMaskUnderlay(IRenderContext* ctx);
    void RenderCenterImage(IRenderContext* ctx);
    void RenderWords(IRenderContext* ctx);

    // ===== INTERACTION =====

    size_t FindWordAtPoint(const Point2Dd& point) const;
    std::string BuildWordTooltip(const WordCloudPlacedWord& word) const;
};

// =============================================================================
// FACTORY FUNCTIONS
// =============================================================================

inline std::shared_ptr<UltraCanvasWordCloudElement> CreateWordCloud(
    const std::string& id, int x, int y, int width, int height) {
    return std::make_shared<UltraCanvasWordCloudElement>(id, x, y, width, height);
}

inline std::shared_ptr<UltraCanvasWordCloudElement> CreateWordCloudWithWords(
    const std::string& id, int x, int y, int width, int height,
    const std::vector<WordCloudWord>& words, const std::string& title = "") {
    auto element = std::make_shared<UltraCanvasWordCloudElement>(id, x, y, width, height);
    element->SetWords(words);
    if (!title.empty()) element->SetTitle(title);
    return element;
}

} // namespace UltraCanvas
