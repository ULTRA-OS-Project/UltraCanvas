// Plugins/Diagrams/UltraCanvasWordCloudDiagram.cpp
// Word cloud / word map diagram element implementation.
// Greedy spiral placement over an occupancy grid (wordcloud2.js-style),
// with geometric shape masks, image masks and a center image the words
// flow around.
// Version: 1.0.0
// Last Modified: 2026-07-22
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasWordCloudDiagram.h"
#include "UltraCanvasUtils.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

namespace UltraCanvas {

namespace {
    constexpr double kPi = 3.14159265358979323846;

    inline double DegToRad(double deg) { return deg * kPi / 180.0; }

    // Unpremultiply one channel of a Cairo ARGB32 pixel.
    inline uint8_t Unpremultiply(uint32_t channel, uint32_t alpha) {
        if (alpha == 0) return 0;
        return static_cast<uint8_t>(std::min<uint32_t>(255, channel * 255 / alpha));
    }

    // Default English stopword list (the Python wordcloud / Kumo intersection
    // plus common contractions).
    const std::unordered_set<std::string>& DefaultStopwords() {
        static const std::unordered_set<std::string> stopwords = {
            "a", "about", "above", "after", "again", "against", "all", "am", "an",
            "and", "any", "are", "aren't", "as", "at", "be", "because", "been",
            "before", "being", "below", "between", "both", "but", "by", "can",
            "can't", "cannot", "could", "couldn't", "did", "didn't", "do", "does",
            "doesn't", "doing", "don't", "down", "during", "each", "few", "for",
            "from", "further", "had", "hadn't", "has", "hasn't", "have", "haven't",
            "having", "he", "he'd", "he'll", "he's", "her", "here", "here's",
            "hers", "herself", "him", "himself", "his", "how", "how's", "i", "i'd",
            "i'll", "i'm", "i've", "if", "in", "into", "is", "isn't", "it", "it's",
            "its", "itself", "let's", "me", "more", "most", "mustn't", "my",
            "myself", "no", "nor", "not", "of", "off", "on", "once", "only", "or",
            "other", "ought", "our", "ours", "ourselves", "out", "over", "own",
            "same", "shan't", "she", "she'd", "she'll", "she's", "should",
            "shouldn't", "so", "some", "such", "than", "that", "that's", "the",
            "their", "theirs", "them", "themselves", "then", "there", "there's",
            "these", "they", "they'd", "they'll", "they're", "they've", "this",
            "those", "through", "to", "too", "under", "until", "up", "very", "was",
            "wasn't", "we", "we'd", "we'll", "we're", "we've", "were", "weren't",
            "what", "what's", "when", "when's", "where", "where's", "which",
            "while", "who", "who's", "whom", "why", "why's", "will", "with",
            "won't", "would", "wouldn't", "you", "you'd", "you'll", "you're",
            "you've", "your", "yours", "yourself", "yourselves"
        };
        return stopwords;
    }
}

// =============================================================================
// WORD MANAGEMENT
// =============================================================================

void UltraCanvasWordCloudElement::AddWord(const std::string& text, double weight) {
    words.emplace_back(text, weight);
    InvalidateLayout();
    RequestRedraw();
}

void UltraCanvasWordCloudElement::AddWord(const WordCloudWord& word) {
    words.push_back(word);
    InvalidateLayout();
    RequestRedraw();
}

void UltraCanvasWordCloudElement::SetWords(const std::vector<WordCloudWord>& newWords) {
    words = newWords;
    InvalidateLayout();
    RequestRedraw();
}

void UltraCanvasWordCloudElement::ClearWords() {
    words.clear();
    placedWords.clear();
    InvalidateLayout();
    RequestRedraw();
}

void UltraCanvasWordCloudElement::SetWordsFromText(const std::string& text,
                                                   const WordCloudTextOptions& options) {
    // Tokenize: letters (incl. UTF-8 multibyte sequences) and digits form
    // tokens; apostrophes are kept when surrounded by letters ("don't").
    std::vector<std::string> tokens;
    std::string current;
    auto flushToken = [&]() {
        if (!current.empty()) {
            tokens.push_back(current);
            current.clear();
        }
    };
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        bool isWordChar = std::isalnum(c) || c >= 0x80;
        if (!isWordChar && c == '\'' && !current.empty() && i + 1 < text.size() &&
            (std::isalnum(static_cast<unsigned char>(text[i + 1])) ||
             static_cast<unsigned char>(text[i + 1]) >= 0x80)) {
            isWordChar = true;
        }
        if (isWordChar) {
            current += options.toLowercase && c < 0x80
                       ? static_cast<char>(std::tolower(c)) : text[i];
        } else {
            flushToken();
        }
    }
    flushToken();

    auto isNumber = [](const std::string& token) {
        for (char ch : token) {
            if (!std::isdigit(static_cast<unsigned char>(ch))) return false;
        }
        return !token.empty();
    };
    auto isStopword = [&](const std::string& token) {
        if (options.useDefaultStopwords && DefaultStopwords().count(token)) return true;
        return options.extraStopwords.count(token) > 0;
    };
    auto isUsable = [&](const std::string& token) {
        if (static_cast<int>(token.size()) < options.minWordLength) return false;
        if (!options.includeNumbers && isNumber(token)) return false;
        return !isStopword(token);
    };

    // Frequency counting.
    std::map<std::string, double> counts;
    for (const auto& token : tokens) {
        if (isUsable(token)) counts[token] += 1.0;
    }

    // Optional collocation detection: frequent adjacent pairs become a single
    // entry and their count is removed from the component words.
    if (options.detectBigrams) {
        std::map<std::string, int> bigrams;
        for (size_t i = 0; i + 1 < tokens.size(); ++i) {
            if (isUsable(tokens[i]) && isUsable(tokens[i + 1])) {
                bigrams[tokens[i] + " " + tokens[i + 1]]++;
            }
        }
        for (const auto& [bigram, count] : bigrams) {
            if (count < options.minBigramCount) continue;
            counts[bigram] = count;
            size_t space = bigram.find(' ');
            auto reduce = [&](const std::string& part) {
                auto it = counts.find(part);
                if (it != counts.end()) {
                    it->second -= count;
                    if (it->second <= 0) counts.erase(it);
                }
            };
            reduce(bigram.substr(0, space));
            reduce(bigram.substr(space + 1));
        }
    }

    // Filter by frequency, order by count and cap at maxWords.
    std::vector<std::pair<std::string, double>> ranked;
    for (const auto& [word, count] : counts) {
        if (count >= options.minFrequency) ranked.emplace_back(word, count);
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });
    if (options.maxWords > 0 && ranked.size() > static_cast<size_t>(options.maxWords)) {
        ranked.resize(options.maxWords);
    }

    words.clear();
    words.reserve(ranked.size());
    for (const auto& [word, count] : ranked) {
        words.emplace_back(word, count);
    }
    InvalidateLayout();
    RequestRedraw();
}

// =============================================================================
// OPTION SETTERS
// =============================================================================

void UltraCanvasWordCloudElement::SetScaling(WordCloudScaling s) {
    scaling = s; InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetFontSizeRange(float minSize, float maxSize) {
    minFontSize = std::max(1.0f, minSize);
    maxFontSize = std::max(minFontSize, maxSize);
    InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetRelativeScaling(float rs) {
    relativeScaling = std::clamp(rs, 0.0f, 1.0f);
    InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetMaxWords(int maxWords) {
    layoutMaxWords = std::max(1, maxWords);
    InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetFontFamily(const std::string& family) {
    fontFamily = family; InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetFontWeight(FontWeight weight) {
    fontWeight = weight; InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetFontSlant(FontSlant slant) {
    fontSlant = slant; InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetRotationMode(WordCloudRotationMode mode) {
    rotationMode = mode; InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetRotationRange(float minDeg, float maxDeg) {
    minRotationDeg = minDeg;
    maxRotationDeg = std::max(minDeg, maxDeg);
    InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetRotationSteps(int steps) {
    rotationSteps = std::max(1, steps);
    InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetRotationAngles(const std::vector<float>& angles) {
    rotationAngles = angles; InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetRotateRatio(float ratio) {
    rotateRatio = std::clamp(ratio, 0.0f, 1.0f);
    InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetColorMode(WordCloudColorMode mode) {
    colorMode = mode; InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetColorPalette(const std::vector<Color>& palette) {
    if (!palette.empty()) colorPalette = palette;
    InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetWordColor(const Color& color) {
    defaultWordColor = color; InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetColorGradient(const Color& low, const Color& high) {
    gradientLowColor = low;
    gradientHighColor = high;
    InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetColorImage(const std::string& path) {
    colorImagePath = path; InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetShape(WordCloudShape s) {
    shape = s; InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetSpiral(WordCloudSpiral s) {
    spiral = s; InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetGridSize(int px) {
    gridSize = std::max(1, px);
    InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetWordPadding(float px) {
    wordPadding = std::max(0.0f, px);
    InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetEllipticity(float e) {
    ellipticity = std::clamp(e, 0.1f, 2.0f);
    InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetSpiralStep(float step) {
    spiralStep = std::max(0.5f, step);
    InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetCloudCenter(float fx, float fy) {
    cloudCenter = Point2Dd(std::clamp(fx, 0.0f, 1.0f), std::clamp(fy, 0.0f, 1.0f));
    InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetShrinkToFit(bool enable) {
    shrinkToFit = enable; InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetDrawOutOfBound(bool enable) {
    drawOutOfBound = enable; InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetShuffleWords(bool enable) {
    shuffleWords = enable; InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetRepeatWords(bool enable) {
    repeatWords = enable; InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetRandomSeed(uint32_t seed) {
    randomSeed = seed; InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetMaskImage(const std::string& path) {
    maskImagePath = path; InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::ClearMaskImage() {
    maskImagePath.clear();
    maskPixmap.reset();
    InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetMaskInvert(bool invert) {
    maskInvert = invert; InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetMaskThresholds(uint8_t alphaThreshold, uint8_t whiteThreshold) {
    maskAlphaThreshold = alphaThreshold;
    maskWhiteThreshold = whiteThreshold;
    InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetShowMaskImage(bool show, float opacity) {
    showMaskImage = show;
    maskImageOpacity = std::clamp(opacity, 0.0f, 1.0f);
    RequestRedraw();
}

void UltraCanvasWordCloudElement::SetCenterImage(const WordCloudCenterImage& image) {
    centerImage = image; InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetCenterImage(const std::string& path, float sizeRatio,
                                                 WordCloudCenterBlock blockMode) {
    centerImage = WordCloudCenterImage();
    centerImage.imagePath = path;
    centerImage.sizeRatio = sizeRatio;
    centerImage.blockMode = blockMode;
    InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::ClearCenterImage() {
    centerImage = WordCloudCenterImage();
    centerPixmap.reset();
    InvalidateLayout(); RequestRedraw();
}

void UltraCanvasWordCloudElement::SetEnableHover(bool enable) {
    enableHover = enable;
    if (!enable) hoveredWordIndex = SIZE_MAX;
    RequestRedraw();
}

void UltraCanvasWordCloudElement::SetHoverColor(const Color& color) {
    hoverColor = color; RequestRedraw();
}

void UltraCanvasWordCloudElement::SetHoverBold(bool bold) {
    hoverBold = bold; RequestRedraw();
}

void UltraCanvasWordCloudElement::SetOnWordClick(std::function<void(const WordCloudPlacedWord&)> callback) {
    onWordClick = std::move(callback);
}

void UltraCanvasWordCloudElement::SetOnWordHover(std::function<void(const WordCloudPlacedWord&)> callback) {
    onWordHover = std::move(callback);
}

void UltraCanvasWordCloudElement::SetOnLayoutComplete(std::function<void(size_t, size_t)> callback) {
    onLayoutComplete = std::move(callback);
}

void UltraCanvasWordCloudElement::SetWordTooltipGenerator(
        std::function<std::string(const WordCloudPlacedWord&)> generator) {
    tooltipGenerator = std::move(generator);
}

size_t UltraCanvasWordCloudElement::GetPlacedWordCount() const {
    size_t count = 0;
    for (const auto& word : placedWords) {
        if (word.placed) count++;
    }
    return count;
}

size_t UltraCanvasWordCloudElement::GetSkippedWordCount() const {
    return placedWords.size() - GetPlacedWordCount();
}

void UltraCanvasWordCloudElement::Recolor() {
    if (placedWords.empty()) return;

    double minW = std::numeric_limits<double>::max();
    double maxW = std::numeric_limits<double>::lowest();
    for (const auto& placed : placedWords) {
        minW = std::min(minW, placed.weight);
        maxW = std::max(maxW, placed.weight);
    }
    rng.seed(randomSeed);
    static const WordCloudWord kEmptyWord;
    for (size_t i = 0; i < placedWords.size(); ++i) {
        auto& placed = placedWords[i];
        double normWeight = (maxW > minW) ? (placed.weight - minW) / (maxW - minW) : 1.0;
        const WordCloudWord& source = placed.sourceIndex < words.size()
                                      ? words[placed.sourceIndex] : kEmptyWord;
        placed.color = PickColor(source, i, normWeight, placed.center);
    }
    RequestRedraw();
}

// =============================================================================
// LAYOUT PIPELINE
// =============================================================================

void UltraCanvasWordCloudElement::InvalidateLayout() {
    layoutDirty = true;
}

float UltraCanvasWordCloudElement::ComputeFontSize(double weight, double minW, double maxW,
                                                   size_t rank, size_t count) const {
    double normFreq = (maxW > minW) ? (weight - minW) / (maxW - minW) : 1.0;

    double scaled;
    switch (scaling) {
        case WordCloudScaling::Linear:       scaled = normFreq; break;
        case WordCloudScaling::SquareRoot:   scaled = std::sqrt(normFreq); break;
        case WordCloudScaling::Logarithmic:  scaled = std::log2(1.0 + 3.0 * normFreq) / 2.0; break;
        case WordCloudScaling::Rank:         scaled = 0.0; break; // rank-only below
    }

    double rankNorm = (count > 1) ? 1.0 - static_cast<double>(rank) / (count - 1) : 1.0;
    double blend;
    if (scaling == WordCloudScaling::Rank) {
        blend = rankNorm;
    } else {
        blend = relativeScaling * scaled + (1.0f - relativeScaling) * rankNorm;
    }

    return minFontSize + static_cast<float>(blend) * (maxFontSize - minFontSize);
}

float UltraCanvasWordCloudElement::PickRotation(const WordCloudWord& word) {
    if (word.HasExplicitRotation()) return word.rotationDeg;

    switch (rotationMode) {
        case WordCloudRotationMode::NoRotation:
        case WordCloudRotationMode::PerWordOnly:
            return 0.0f;
        case WordCloudRotationMode::Steps: {
            std::uniform_real_distribution<float> prob(0.0f, 1.0f);
            if (prob(rng) > rotateRatio) return 0.0f;
            if (!rotationAngles.empty()) {
                std::uniform_int_distribution<size_t> pick(0, rotationAngles.size() - 1);
                return rotationAngles[pick(rng)];
            }
            if (rotationSteps <= 1) return minRotationDeg;
            std::uniform_int_distribution<int> pick(0, rotationSteps - 1);
            return minRotationDeg + pick(rng) *
                   (maxRotationDeg - minRotationDeg) / (rotationSteps - 1);
        }
        case WordCloudRotationMode::RandomRange: {
            std::uniform_real_distribution<float> prob(0.0f, 1.0f);
            if (prob(rng) > rotateRatio) return 0.0f;
            std::uniform_real_distribution<float> angle(minRotationDeg, maxRotationDeg);
            return angle(rng);
        }
    }
    return 0.0f;
}

Color UltraCanvasWordCloudElement::PickColor(const WordCloudWord& word, size_t orderIndex,
                                             double normWeight, const Point2Dd& center) {
    if (word.HasExplicitColor()) return word.color;

    switch (colorMode) {
        case WordCloudColorMode::Palette:
            return colorPalette[orderIndex % colorPalette.size()];
        case WordCloudColorMode::RandomFromPalette: {
            std::uniform_int_distribution<size_t> pick(0, colorPalette.size() - 1);
            return colorPalette[pick(rng)];
        }
        case WordCloudColorMode::ByWeightGradient: {
            auto lerp = [](uint8_t a, uint8_t b, double t) {
                return static_cast<uint8_t>(a + (b - a) * t);
            };
            return Color(lerp(gradientLowColor.r, gradientHighColor.r, normWeight),
                         lerp(gradientLowColor.g, gradientHighColor.g, normWeight),
                         lerp(gradientLowColor.b, gradientHighColor.b, normWeight),
                         lerp(gradientLowColor.a, gradientHighColor.a, normWeight));
        }
        case WordCloudColorMode::SingleColor:
            return defaultWordColor;
        case WordCloudColorMode::FromImage: {
            Color sampled = SampleImageColor(center);
            return sampled.a > 0 ? sampled : defaultWordColor;
        }
    }
    return defaultWordColor;
}

Color UltraCanvasWordCloudElement::SampleImageColor(const Point2Dd& center) const {
    if (!colorPixmap || !colorPixmap->IsValid()) return Colors::Transparent;

    // The color pixmap is fitted (Contain) and centered on the plot rect;
    // map the element-local point into pixmap pixels.
    int pw = colorPixmap->GetWidth();
    int ph = colorPixmap->GetHeight();
    double ox = plotRect.x + (plotRect.width - pw) * 0.5;
    double oy = plotRect.y + (plotRect.height - ph) * 0.5;

    // Average a small neighbourhood - single pixels are too noisy on photos.
    static const int offsets[5][2] = {{0, 0}, {-3, 0}, {3, 0}, {0, -3}, {0, 3}};
    uint32_t rSum = 0, gSum = 0, bSum = 0, samples = 0;
    for (const auto& off : offsets) {
        int px = static_cast<int>(center.x - ox) + off[0];
        int py = static_cast<int>(center.y - oy) + off[1];
        if (px < 0 || py < 0 || px >= pw || py >= ph) continue;
        uint32_t pixel = colorPixmap->GetPixel(px, py);
        uint32_t a = (pixel >> 24) & 0xFF;
        if (a < 32) continue;
        rSum += Unpremultiply((pixel >> 16) & 0xFF, a);
        gSum += Unpremultiply((pixel >> 8) & 0xFF, a);
        bSum += Unpremultiply(pixel & 0xFF, a);
        samples++;
    }
    if (samples == 0) return Colors::Transparent;
    return Color(static_cast<uint8_t>(rSum / samples),
                 static_cast<uint8_t>(gSum / samples),
                 static_cast<uint8_t>(bSum / samples), 255);
}

double UltraCanvasWordCloudElement::ShapeRadiusFactor(double theta) const {
    // Polar radius functions from wordcloud2.js, normalized to max = 1 by
    // ApplyShapeToGrid's sampling pass.
    switch (shape) {
        case WordCloudShape::Cardioid:
            return 1.0 - std::sin(theta);
        case WordCloudShape::Diamond: {
            double t = std::fmod(theta + 2.0 * kPi, kPi / 2.0);
            return 1.0 / (std::cos(t) + std::sin(t));
        }
        case WordCloudShape::Square:
            return std::min(1.0 / std::max(1e-6, std::abs(std::cos(theta))),
                            1.0 / std::max(1e-6, std::abs(std::sin(theta))));
        case WordCloudShape::TriangleForward: {
            double t = std::fmod(theta + 2.0 * kPi, 2.0 * kPi / 3.0);
            return 1.0 / (std::cos(t) + std::sqrt(3.0) * std::sin(t));
        }
        case WordCloudShape::Triangle: {
            double t = std::fmod(theta + kPi * 3.0 / 2.0 + 4.0 * kPi, 2.0 * kPi / 3.0);
            return 1.0 / (std::cos(t) + std::sqrt(3.0) * std::sin(t));
        }
        case WordCloudShape::Pentagon: {
            double t = std::fmod(theta + 0.955 + 2.0 * kPi, 2.0 * kPi / 5.0);
            return 1.0 / (std::cos(t) + 0.726543 * std::sin(t));
        }
        case WordCloudShape::Star: {
            double adjusted = theta + 0.955 + 4.0 * kPi;
            double t = std::fmod(adjusted, 2.0 * kPi / 10.0);
            if (std::fmod(adjusted, 2.0 * kPi / 5.0) - (2.0 * kPi / 10.0) >= 0) {
                return 1.0 / (std::cos((2.0 * kPi / 10.0) - t) +
                              3.07768 * std::sin((2.0 * kPi / 10.0) - t));
            }
            return 1.0 / (std::cos(t) + 3.07768 * std::sin(t));
        }
        default:
            return 1.0;   // Circle / Ellipse handled separately
    }
}

bool UltraCanvasWordCloudElement::ShapeAllowsPoint(double px, double py) const {
    if (shape == WordCloudShape::Rectangle) return true;

    double cx = plotRect.x + plotRect.width * 0.5;
    double cy = plotRect.y + plotRect.height * 0.5;
    double halfW = plotRect.width * 0.5;
    double halfH = plotRect.height * 0.5;
    double dx = px - cx;
    double dy = py - cy;

    if (shape == WordCloudShape::Ellipse) {
        double nx = dx / halfW;
        double ny = dy / halfH;
        return nx * nx + ny * ny <= 1.0;
    }
    if (shape == WordCloudShape::Circle) {
        double r = std::min(halfW, halfH);
        return dx * dx + dy * dy <= r * r;
    }

    // Polar shapes: normalize the polar radius function so the shape's widest
    // point touches the shorter half-extent of the plot area. The max factor
    // only depends on the shape, so compute it once per shape change.
    static thread_local WordCloudShape cachedShape = WordCloudShape::Rectangle;
    static thread_local double cachedMaxFactor = 1.0;
    if (cachedShape != shape || cachedMaxFactor <= 0.0) {
        double maxFactor = 1.0;
        for (int i = 0; i < 360; ++i) {
            maxFactor = std::max(maxFactor, ShapeRadiusFactor(i * kPi / 180.0));
        }
        cachedShape = shape;
        cachedMaxFactor = maxFactor;
    }
    double maxFactor = cachedMaxFactor;
    double baseRadius = std::min(halfW, halfH / ellipticity);
    double theta = std::atan2(dy / ellipticity, dx);
    double allowedRadius = baseRadius * ellipticity *
                           (ShapeRadiusFactor(theta) / maxFactor);
    // Compare in the vertically-unsquished space to keep the outline true.
    double dist = std::sqrt(dx * dx + (dy / ellipticity) * (dy / ellipticity));
    return dist <= allowedRadius / ellipticity;
}

void UltraCanvasWordCloudElement::PrepareOccupancyGrid() {
    gridCols = std::max(1, static_cast<int>(std::ceil(plotRect.width / gridSize)));
    gridRows = std::max(1, static_cast<int>(std::ceil(plotRect.height / gridSize)));
    occupancy.assign(static_cast<size_t>(gridCols) * gridRows, 0);
}

void UltraCanvasWordCloudElement::ApplyShapeToGrid() {
    if (shape == WordCloudShape::Rectangle) return;
    for (int gy = 0; gy < gridRows; ++gy) {
        for (int gx = 0; gx < gridCols; ++gx) {
            double px = plotRect.x + (gx + 0.5) * gridSize;
            double py = plotRect.y + (gy + 0.5) * gridSize;
            if (!ShapeAllowsPoint(px, py)) {
                occupancy[static_cast<size_t>(gy) * gridCols + gx] = 1;
            }
        }
    }
}

void UltraCanvasWordCloudElement::ResolveMaskImage() {
    maskPixmap.reset();
    if (maskImagePath.empty()) return;
    auto raster = UCImageRaster::Get(maskImagePath);
    if (!raster || !raster->IsValid()) return;
    // Rasterize straight at grid resolution: one pixel per occupancy cell,
    // aspect-fitted the same way the underlay image is drawn.
    maskPixmap = raster->GetPixmap(gridCols, gridRows, ImageFitMode::Contain);
}

void UltraCanvasWordCloudElement::ApplyMaskImageToGrid() {
    if (!maskPixmap || !maskPixmap->IsValid()) return;

    int pw = maskPixmap->GetWidth();
    int ph = maskPixmap->GetHeight();
    int offsetX = (gridCols - pw) / 2;
    int offsetY = (gridRows - ph) / 2;

    for (int gy = 0; gy < gridRows; ++gy) {
        for (int gx = 0; gx < gridCols; ++gx) {
            int px = gx - offsetX;
            int py = gy - offsetY;
            bool insideMask = false;
            if (px >= 0 && py >= 0 && px < pw && py < ph) {
                uint32_t pixel = maskPixmap->GetPixel(px, py);
                uint32_t a = (pixel >> 24) & 0xFF;
                if (a >= maskAlphaThreshold) {
                    // The Python wordcloud convention: near-white pixels are
                    // background even when opaque.
                    uint8_t r = Unpremultiply((pixel >> 16) & 0xFF, a);
                    uint8_t g = Unpremultiply((pixel >> 8) & 0xFF, a);
                    uint8_t b = Unpremultiply(pixel & 0xFF, a);
                    insideMask = !(r >= maskWhiteThreshold && g >= maskWhiteThreshold &&
                                   b >= maskWhiteThreshold);
                }
            }
            bool allowed = maskInvert ? !insideMask : insideMask;
            if (!allowed) {
                occupancy[static_cast<size_t>(gy) * gridCols + gx] = 1;
            }
        }
    }
}

void UltraCanvasWordCloudElement::ResolveCenterImage() {
    centerPixmap.reset();
    centerImageRect = Rect2Dd(0, 0, 0, 0);
    if (!centerImage.IsValid()) return;

    auto raster = UCImageRaster::Get(centerImage.imagePath);
    if (!raster || !raster->IsValid()) return;

    double w, h;
    float aspect = raster->GetAspectRatio();
    if (centerImage.pixelWidth > 0 || centerImage.pixelHeight > 0) {
        w = centerImage.pixelWidth > 0 ? centerImage.pixelWidth
                                       : centerImage.pixelHeight * aspect;
        h = centerImage.pixelHeight > 0 ? centerImage.pixelHeight
                                        : centerImage.pixelWidth / aspect;
    } else {
        double base = std::min(plotRect.width, plotRect.height) *
                      std::clamp(centerImage.sizeRatio, 0.05f, 0.95f);
        if (aspect >= 1.0f) { w = base; h = base / aspect; }
        else                { h = base; w = base * aspect; }
    }

    double cx = plotRect.x + plotRect.width * cloudCenter.x + centerImage.offset.x;
    double cy = plotRect.y + plotRect.height * cloudCenter.y + centerImage.offset.y;
    centerImageRect = Rect2Dd(cx - w * 0.5, cy - h * 0.5, w, h);

    centerPixmap = raster->GetPixmap(static_cast<int>(std::lround(w)),
                                     static_cast<int>(std::lround(h)),
                                     ImageFitMode::Fill);
}

void UltraCanvasWordCloudElement::ApplyCenterImageToGrid() {
    if (!centerImage.IsValid() || centerImageRect.width <= 0) return;

    std::vector<uint8_t> blocked(static_cast<size_t>(gridCols) * gridRows, 0);
    double icx = centerImageRect.x + centerImageRect.width * 0.5;
    double icy = centerImageRect.y + centerImageRect.height * 0.5;
    double rx = centerImageRect.width * 0.5;
    double ry = centerImageRect.height * 0.5;

    for (int gy = 0; gy < gridRows; ++gy) {
        for (int gx = 0; gx < gridCols; ++gx) {
            double px = plotRect.x + (gx + 0.5) * gridSize;
            double py = plotRect.y + (gy + 0.5) * gridSize;
            if (!centerImageRect.Contains(Point2Dd(px, py))) continue;

            bool covered = true;
            if (centerImage.blockMode == WordCloudCenterBlock::Ellipse ||
                centerImage.clipToCircle) {
                double nx = (px - icx) / rx;
                double ny = (py - icy) / ry;
                covered = nx * nx + ny * ny <= 1.0;
            } else if (centerImage.blockMode == WordCloudCenterBlock::AlphaPixels &&
                       centerPixmap && centerPixmap->IsValid()) {
                int ix = static_cast<int>((px - centerImageRect.x) /
                                          centerImageRect.width * centerPixmap->GetWidth());
                int iy = static_cast<int>((py - centerImageRect.y) /
                                          centerImageRect.height * centerPixmap->GetHeight());
                ix = std::clamp(ix, 0, centerPixmap->GetWidth() - 1);
                iy = std::clamp(iy, 0, centerPixmap->GetHeight() - 1);
                uint32_t a = (centerPixmap->GetPixel(ix, iy) >> 24) & 0xFF;
                covered = a >= 16;
            }
            if (covered) {
                blocked[static_cast<size_t>(gy) * gridCols + gx] = 1;
            }
        }
    }

    int padCells = static_cast<int>(std::ceil(centerImage.padding / gridSize));
    DilateBlockedCells(blocked, padCells);

    for (size_t i = 0; i < blocked.size(); ++i) {
        if (blocked[i]) occupancy[i] = 1;
    }
}

void UltraCanvasWordCloudElement::DilateBlockedCells(std::vector<uint8_t>& cells, int radiusCells) {
    if (radiusCells <= 0) return;
    std::vector<uint8_t> source = cells;
    for (int gy = 0; gy < gridRows; ++gy) {
        for (int gx = 0; gx < gridCols; ++gx) {
            if (!source[static_cast<size_t>(gy) * gridCols + gx]) continue;
            int y0 = std::max(0, gy - radiusCells);
            int y1 = std::min(gridRows - 1, gy + radiusCells);
            int x0 = std::max(0, gx - radiusCells);
            int x1 = std::min(gridCols - 1, gx + radiusCells);
            for (int yy = y0; yy <= y1; ++yy) {
                for (int xx = x0; xx <= x1; ++xx) {
                    cells[static_cast<size_t>(yy) * gridCols + xx] = 1;
                }
            }
        }
    }
}

void UltraCanvasWordCloudElement::ResolveColorImage() {
    colorPixmap.reset();
    if (colorMode != WordCloudColorMode::FromImage) return;

    std::string path = colorImagePath;
    if (path.empty()) path = maskImagePath;
    if (path.empty()) path = centerImage.imagePath;
    if (path.empty()) return;

    auto raster = UCImageRaster::Get(path);
    if (!raster || !raster->IsValid()) return;
    colorPixmap = raster->GetPixmap(static_cast<int>(plotRect.width),
                                    static_cast<int>(plotRect.height),
                                    ImageFitMode::Contain);
}

bool UltraCanvasWordCloudElement::RegionIsFree(int gx0, int gy0, int gx1, int gy1) const {
    for (int gy = gy0; gy <= gy1; ++gy) {
        for (int gx = gx0; gx <= gx1; ++gx) {
            if (gx < 0 || gy < 0 || gx >= gridCols || gy >= gridRows) {
                if (!drawOutOfBound) return false;
                continue;
            }
            if (occupancy[static_cast<size_t>(gy) * gridCols + gx]) return false;
        }
    }
    return true;
}

void UltraCanvasWordCloudElement::MarkRegion(int gx0, int gy0, int gx1, int gy1) {
    gx0 = std::max(0, gx0); gy0 = std::max(0, gy0);
    gx1 = std::min(gridCols - 1, gx1); gy1 = std::min(gridRows - 1, gy1);
    for (int gy = gy0; gy <= gy1; ++gy) {
        for (int gx = gx0; gx <= gx1; ++gx) {
            occupancy[static_cast<size_t>(gy) * gridCols + gx] = 1;
        }
    }
}

bool UltraCanvasWordCloudElement::TryPlaceWord(IRenderContext* ctx, WordCloudPlacedWord& placed,
                                               const WordCloudWord& word, float fontSize,
                                               float rotationDeg) {
    ctx->SetFontFace(word.fontFamily.empty() ? fontFamily : word.fontFamily,
                     word.hasFontWeight ? word.fontWeight : fontWeight, fontSlant);
    ctx->SetFontSize(fontSize);
    Size2Di textSize = ctx->GetTextLineDimensions(word.text);
    if (textSize.width <= 0 || textSize.height <= 0) return false;

    // Axis-aligned bounds of the rotated word plus breathing room.
    double rad = DegToRad(rotationDeg);
    double cosA = std::abs(std::cos(rad));
    double sinA = std::abs(std::sin(rad));
    double boundsW = cosA * textSize.width + sinA * textSize.height + 2.0 * wordPadding;
    double boundsH = sinA * textSize.width + cosA * textSize.height + 2.0 * wordPadding;

    double startX = plotRect.x + plotRect.width * cloudCenter.x;
    double startY = plotRect.y + plotRect.height * cloudCenter.y;
    double maxRadius = std::sqrt(plotRect.width * plotRect.width +
                                 plotRect.height * plotRect.height) * 0.5 +
                       std::max(boundsW, boundsH);

    auto tryAt = [&](double cx, double cy) -> bool {
        double left = cx - boundsW * 0.5;
        double top = cy - boundsH * 0.5;

        if (!drawOutOfBound) {
            if (left < plotRect.x || top < plotRect.y ||
                left + boundsW > plotRect.x + plotRect.width ||
                top + boundsH > plotRect.y + plotRect.height) {
                return false;
            }
        }

        int gx0 = static_cast<int>(std::floor((left - plotRect.x) / gridSize));
        int gy0 = static_cast<int>(std::floor((top - plotRect.y) / gridSize));
        int gx1 = static_cast<int>(std::floor((left + boundsW - 1.0 - plotRect.x) / gridSize));
        int gy1 = static_cast<int>(std::floor((top + boundsH - 1.0 - plotRect.y) / gridSize));
        if (!RegionIsFree(gx0, gy0, gx1, gy1)) return false;

        MarkRegion(gx0, gy0, gx1, gy1);
        placed.fontSize = fontSize;
        placed.rotationDeg = rotationDeg;
        placed.center = Point2Dd(cx, cy);
        placed.textSize = textSize;
        placed.bounds = Rect2Dd(cx - (boundsW - 2.0 * wordPadding) * 0.5,
                                cy - (boundsH - 2.0 * wordPadding) * 0.5,
                                boundsW - 2.0 * wordPadding,
                                boundsH - 2.0 * wordPadding);
        placed.placed = true;
        return true;
    };

    if (spiral == WordCloudSpiral::Rectangular) {
        // d3-cloud style rectangular spiral: growing right/down/left/up runs.
        double step = std::max<double>(gridSize, spiralStep);
        double x = 0, y = 0;
        if (tryAt(startX, startY)) return true;
        int leg = 1;
        while (std::max(std::abs(x), std::abs(y)) < maxRadius) {
            int dirIndex = (leg - 1) % 4;
            int run = (leg + 1) / 2;
            for (int i = 0; i < run; ++i) {
                switch (dirIndex) {
                    case 0: x += step; break;
                    case 1: y += step * ellipticity; break;
                    case 2: x -= step; break;
                    case 3: y -= step * ellipticity; break;
                }
                if (tryAt(startX + x, startY + y)) return true;
            }
            leg++;
        }
    } else {
        // Archimedean spiral: radius grows spiralStep*gridSize px per turn.
        double growth = spiralStep * gridSize / (2.0 * kPi);
        double t = 0.0;
        while (true) {
            double r = growth * t;
            if (r > maxRadius) break;
            double cx = startX + r * std::cos(t);
            double cy = startY + r * std::sin(t) * ellipticity;
            if (tryAt(cx, cy)) return true;
            // Angular step shrinks as the radius grows so the sample spacing
            // along the curve stays roughly one grid cell.
            t += (r < gridSize) ? 0.6 : gridSize / r;
        }
    }
    return false;
}

void UltraCanvasWordCloudElement::PerformLayout(IRenderContext* ctx) {
    placedWords.clear();
    hoveredWordIndex = SIZE_MAX;

    // Plot area: element bounds minus a small margin and the title strip.
    double marginTop = chartTitle.empty() ? 10.0 : 42.0;
    plotRect = Rect2Dd(10.0, marginTop,
                       std::max(1.0, static_cast<double>(GetWidth()) - 20.0),
                       std::max(1.0, GetHeight() - marginTop - 10.0));

    lastLayoutSize = Size2Di(GetWidth(), GetHeight());
    layoutDirty = false;
    if (words.empty()) return;

    rng.seed(randomSeed);

    PrepareOccupancyGrid();
    ResolveMaskImage();
    ResolveCenterImage();
    ResolveColorImage();
    if (maskPixmap) {
        ApplyMaskImageToGrid();
    } else {
        ApplyShapeToGrid();
    }
    ApplyCenterImageToGrid();

    // Build the layout list: word indices ordered heaviest-first (or
    // shuffled), capped at layoutMaxWords, optionally repeated to fill space.
    std::vector<size_t> order(words.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [this](size_t a, size_t b) {
        return words[a].weight > words[b].weight;
    });

    // Weight rank per source index (before any shuffling) for Rank scaling.
    std::vector<size_t> rankOf(words.size());
    for (size_t i = 0; i < order.size(); ++i) rankOf[order[i]] = i;

    if (order.size() > static_cast<size_t>(layoutMaxWords)) {
        order.resize(layoutMaxWords);
    }
    if (shuffleWords) {
        std::shuffle(order.begin(), order.end(), rng);
    }

    struct LayoutEntry { size_t index; double weightFactor; };
    std::vector<LayoutEntry> entries;
    entries.reserve(layoutMaxWords);
    for (size_t idx : order) entries.push_back({idx, 1.0});
    if (repeatWords && !entries.empty()) {
        double factor = 0.5;
        while (entries.size() < static_cast<size_t>(layoutMaxWords)) {
            for (size_t idx : order) {
                if (entries.size() >= static_cast<size_t>(layoutMaxWords)) break;
                entries.push_back({idx, factor});
            }
            factor *= 0.5;
        }
    }

    double minW = std::numeric_limits<double>::max();
    double maxW = std::numeric_limits<double>::lowest();
    for (const auto& word : words) {
        minW = std::min(minW, word.weight);
        maxW = std::max(maxW, word.weight);
    }

    for (size_t orderIndex = 0; orderIndex < entries.size(); ++orderIndex) {
        const auto& entry = entries[orderIndex];
        const WordCloudWord& word = words[entry.index];
        if (word.text.empty()) continue;

        WordCloudPlacedWord placed;
        placed.sourceIndex = entry.index;
        placed.text = word.text;
        placed.weight = word.weight;
        placed.placed = false;

        float fontSize = ComputeFontSize(word.weight, minW, maxW,
                                         rankOf[entry.index], words.size());
        fontSize = std::max(minFontSize, fontSize * static_cast<float>(entry.weightFactor));
        float rotation = PickRotation(word);

        while (true) {
            if (TryPlaceWord(ctx, placed, word, fontSize, rotation)) break;
            if (!shrinkToFit || fontSize - shrinkStep < minFontSize) break;
            fontSize -= shrinkStep;
        }

        double normWeight = (maxW > minW) ? (word.weight - minW) / (maxW - minW) : 1.0;
        placed.color = PickColor(word, orderIndex, normWeight,
                                 placed.placed ? placed.center
                                               : Point2Dd(plotRect.x + plotRect.width * 0.5,
                                                          plotRect.y + plotRect.height * 0.5));
        placedWords.push_back(placed);
    }

    if (animationEnabled) {
        StartAnimation();
    }
    if (onLayoutComplete) {
        onLayoutComplete(GetPlacedWordCount(), placedWords.size());
    }
}

// =============================================================================
// RENDERING
// =============================================================================

void UltraCanvasWordCloudElement::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    RenderChart(ctx);
}

void UltraCanvasWordCloudElement::RenderChart(IRenderContext* ctx) {
    if (!ctx) return;

    if (backgroundColor.a > 0) {
        ctx->SetFillPaint(backgroundColor);
        ctx->FillRectangle(GetLocalBounds());
    }

    if (layoutDirty || lastLayoutSize.width != GetWidth() ||
        lastLayoutSize.height != GetHeight()) {
        PerformLayout(ctx);
    }

    RenderTitle(ctx);

    if (words.empty()) {
        DrawEmptyState(ctx);
        return;
    }

    RenderMaskUnderlay(ctx);
    RenderCenterImage(ctx);
    RenderWords(ctx);
}

void UltraCanvasWordCloudElement::RenderTitle(IRenderContext* ctx) {
    if (chartTitle.empty()) return;
    ctx->SetFontFace(fontFamily, FontWeight::Bold, FontSlant::Normal);
    ctx->SetFontSize(16.0);
    ctx->SetTextPaint(Color(33, 37, 41, 255));
    int tw = ctx->GetTextLineWidth(chartTitle);
    ctx->DrawText(chartTitle, Point2Dd((GetWidth() - tw) * 0.5, 12.0));
}

void UltraCanvasWordCloudElement::RenderMaskUnderlay(IRenderContext* ctx) {
    if (!showMaskImage || maskImagePath.empty()) return;
    auto raster = UCImageRaster::Get(maskImagePath);
    if (!raster || !raster->IsValid()) return;
    auto pixmap = raster->GetPixmap(static_cast<int>(plotRect.width),
                                    static_cast<int>(plotRect.height),
                                    ImageFitMode::Contain);
    if (!pixmap || !pixmap->IsValid()) return;

    Rect2Dd dest(plotRect.x + (plotRect.width - pixmap->GetWidth()) * 0.5,
                 plotRect.y + (plotRect.height - pixmap->GetHeight()) * 0.5,
                 pixmap->GetWidth(), pixmap->GetHeight());
    ctx->PushState();
    ctx->SetAlpha(maskImageOpacity);
    ctx->DrawPixmap(*pixmap, dest, ImageFitMode::Fill);
    ctx->PopState();
}

void UltraCanvasWordCloudElement::RenderCenterImage(IRenderContext* ctx) {
    if (!centerPixmap || !centerPixmap->IsValid() || centerImageRect.width <= 0) return;

    ctx->PushState();
    if (centerImage.opacity < 1.0f) {
        ctx->SetAlpha(centerImage.opacity);
    }
    if (centerImage.clipToCircle) {
        ctx->ClearPath();
        ctx->Ellipse(centerImageRect.x + centerImageRect.width * 0.5,
                     centerImageRect.y + centerImageRect.height * 0.5,
                     centerImageRect.width * 0.5, centerImageRect.height * 0.5, 0.0);
        ctx->ClipPath();
    } else if (centerImage.cornerRadius > 0.0f) {
        double r = centerImage.cornerRadius;
        ctx->ClipRoundedRectangle(centerImageRect, r, r, r, r);
    }
    ctx->DrawPixmap(*centerPixmap, centerImageRect, ImageFitMode::Fill);
    ctx->PopState();

    if (centerImage.drawBorder && centerImage.borderWidth > 0.0f) {
        ctx->SetStrokePaint(centerImage.borderColor);
        ctx->SetStrokeWidth(centerImage.borderWidth);
        if (centerImage.clipToCircle) {
            ctx->DrawEllipse(centerImageRect);
        } else if (centerImage.cornerRadius > 0.0f) {
            ctx->DrawRoundedRectangle(centerImageRect, centerImage.cornerRadius);
        } else {
            ctx->DrawRectangle(centerImageRect);
        }
    }
}

void UltraCanvasWordCloudElement::RenderWords(IRenderContext* ctx) {
    float progress = 1.0f;
    if (animationEnabled && !animationComplete) {
        UpdateAnimation();
        progress = GetAnimationProgress();
    }

    size_t total = std::max<size_t>(1, placedWords.size());
    for (size_t i = 0; i < placedWords.size(); ++i) {
        const auto& word = placedWords[i];
        if (!word.placed) continue;

        // Staggered fade-in: each word gets a slice of the animation window
        // in placement order (heaviest words appear first).
        float alpha = 1.0f;
        if (progress < 1.0f) {
            float slotStart = static_cast<float>(i) / total * 0.7f;
            alpha = std::clamp((progress - slotStart) / 0.3f, 0.0f, 1.0f);
            if (alpha <= 0.0f) continue;
        }

        bool hovered = enableHover && i == hoveredWordIndex;
        static const WordCloudWord kEmptyWord;
        const WordCloudWord& source = word.sourceIndex < words.size()
                                      ? words[word.sourceIndex] : kEmptyWord;

        FontWeight weight = source.hasFontWeight ? source.fontWeight : fontWeight;
        if (hovered && hoverBold) weight = FontWeight::ExtraBold;
        ctx->SetFontFace(source.fontFamily.empty() ? fontFamily : source.fontFamily,
                         weight, fontSlant);
        ctx->SetFontSize(word.fontSize);

        Color color = hovered ? hoverColor : word.color;
        if (alpha < 1.0f) {
            color.a = static_cast<uint8_t>(color.a * alpha);
        }
        ctx->SetTextPaint(color);

        ctx->PushState();
        ctx->Translate(word.center.x, word.center.y);
        if (word.rotationDeg != 0.0f) {
            ctx->Rotate(DegToRad(word.rotationDeg));
        }
        ctx->DrawText(word.text, Point2Dd(-word.textSize.width * 0.5,
                                          -word.textSize.height * 0.5));
        ctx->PopState();
    }

    if (progress < 1.0f) {
        RequestRedraw();   // keep the fade-in animation running
    }
}

// =============================================================================
// INTERACTION
// =============================================================================

size_t UltraCanvasWordCloudElement::FindWordAtPoint(const Point2Dd& point) const {
    // Topmost-last iteration is irrelevant here (words never overlap), but
    // testing smaller (later) words first makes dense areas feel precise.
    for (size_t i = placedWords.size(); i-- > 0; ) {
        const auto& word = placedWords[i];
        if (!word.placed) continue;
        if (!word.bounds.Contains(point)) continue;

        // Transform into the word's unrotated local space for an exact test.
        double rad = DegToRad(-word.rotationDeg);
        double dx = point.x - word.center.x;
        double dy = point.y - word.center.y;
        double localX = dx * std::cos(rad) - dy * std::sin(rad);
        double localY = dx * std::sin(rad) + dy * std::cos(rad);
        if (std::abs(localX) <= word.textSize.width * 0.5 &&
            std::abs(localY) <= word.textSize.height * 0.5) {
            return i;
        }
    }
    return SIZE_MAX;
}

std::string UltraCanvasWordCloudElement::BuildWordTooltip(const WordCloudPlacedWord& word) const {
    if (tooltipGenerator) {
        return tooltipGenerator(word);
    }
    std::ostringstream content;
    content << word.text << "\nWeight: ";
    double rounded = std::round(word.weight * 100.0) / 100.0;
    if (rounded == std::floor(rounded)) {
        content << static_cast<long long>(rounded);
    } else {
        content << rounded;
    }
    if (word.sourceIndex < words.size() && !words[word.sourceIndex].tooltip.empty()) {
        content << "\n" << words[word.sourceIndex].tooltip;
    }
    return content.str();
}

bool UltraCanvasWordCloudElement::HandleChartMouseMove(const Point2Di& mousePos) {
    if (!enableHover && !enableTooltips) return false;

    size_t newHovered = FindWordAtPoint(Point2Dd(mousePos.x, mousePos.y));
    if (newHovered != hoveredWordIndex) {
        hoveredWordIndex = newHovered;
        if (hoveredWordIndex != SIZE_MAX) {
            if (onWordHover) {
                onWordHover(placedWords[hoveredWordIndex]);
            }
            if (enableTooltips) {
                auto windowPos = MapFromLocal(mousePos, nullptr);
                UltraCanvasTooltipManager::UpdateAndShowTooltip(
                    this->window, BuildWordTooltip(placedWords[hoveredWordIndex]), windowPos);
                isTooltipActive = true;
            }
        } else if (isTooltipActive) {
            HideTooltip();
        }
        if (enableHover) RequestRedraw();
        return true;
    }
    return hoveredWordIndex != SIZE_MAX;
}

bool UltraCanvasWordCloudElement::OnEvent(const UCEvent& event) {
    switch (event.type) {
        case UCEventType::MouseMove:
            return HandleChartMouseMove(event.pointer);
        case UCEventType::MouseDown: {
            size_t index = FindWordAtPoint(Point2Dd(event.pointer.x, event.pointer.y));
            if (index != SIZE_MAX) {
                if (onWordClick) {
                    onWordClick(placedWords[index]);
                }
                return true;
            }
            return false;
        }
        case UCEventType::MouseLeave:
            if (hoveredWordIndex != SIZE_MAX) {
                hoveredWordIndex = SIZE_MAX;
                if (isTooltipActive) HideTooltip();
                if (enableHover) RequestRedraw();
            }
            return false;
        default:
            break;
    }
    return false;
}

} // namespace UltraCanvas
