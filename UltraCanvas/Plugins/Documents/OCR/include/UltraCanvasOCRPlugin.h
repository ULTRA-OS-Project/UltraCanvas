// UltraCanvasOCRPlugin.h
// Optical Character Recognition plugin for UltraCanvas
// Version: 0.1.0
// Last Modified: 2026-05-21
// Author: UltraCanvas Framework
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <future>
#include <stop_token>

#ifdef ULTRACANVAS_OCR_SUPPORT
#include <vips/vips8>
#include "UltraCanvasCommonTypes.h"

//namespace PixelFX { class PFXImage; }

namespace UltraCanvas {

// ===== ENGINE SELECTION =====
enum class OCREngineKind {
    Auto,           // pick the best available backend at runtime
    Tesseract,      // tier 1: libtesseract (default when compiled in)
    ONNX,           // tier 2: PP-OCRv4 via ONNX Runtime
    AppleVision,    // macOS / iOS native
    WindowsMedia    // Windows 10+ Windows.Media.Ocr
};

// ===== PAGE SEGMENTATION =====
enum class OCRPageSegmentation {
    Auto,
    SingleBlock,
    SingleLine,
    SingleWord,
    SparseText,
    RawLine
};

// ===== CONFIGURATION =====
struct OCRConfig {
    OCREngineKind            engine        = OCREngineKind::Auto;
    OCRPageSegmentation      segmentation  = OCRPageSegmentation::Auto;
    std::vector<std::string> languages     = {"eng"}; // tessdata codes
    bool                     preprocess    = true;    // deskew/binarize/denoise
    int                      dpiHint       = 0;       // 0 = auto / image metadata
    float                    minConfidence = 0.0f;    // drop words below this (0..1)
    std::string              dataPath;                // tessdata dir; empty = auto
};

// ===== RESULT TYPES =====
struct OCRWord {
    std::string text;
    Rect2Di     box;
    float       confidence = 0.0f;
    int         lineIndex = -1;
    int         paragraphIndex = -1;
    int         blockIndex = -1;
    std::string language;
};

struct OCRLine {
    std::string      text;
    Rect2Di          box;
    std::vector<int> wordIndices;
    float            baselineAngleDeg = 0.0f;
};

struct OCRResult {
    std::string             plainText;   // reading-order text
    std::vector<OCRWord>    words;
    std::vector<OCRLine>    lines;
    std::vector<Rect2Di>    paragraphs;
    std::vector<Rect2Di>    blocks;
    double                  elapsedMs  = 0.0;
    OCREngineKind           engineUsed = OCREngineKind::Auto;
    std::string             error;       // empty on success
    bool Ok() const { return error.empty(); }
};

// ===== ENGINE INTERFACE =====
class IOCREngine {
public:
    virtual ~IOCREngine() = default;
    virtual bool          Initialize(const OCRConfig& cfg)         = 0;
    virtual OCRResult     RecognizeFile(const std::string& path)   = 0;
    virtual OCRResult     RecognizeBuffer(const void* data, size_t bytes,
                                          const std::string& formatHint) = 0;
    virtual OCRResult     RecognizeRaw(const uint8_t* pixels, int width, int height,
                                       int bytesPerPixel, int bytesPerLine) = 0;
    virtual std::vector<std::string> AvailableLanguages() const     = 0;
    virtual OCREngineKind Kind() const                              = 0;
};

// ===== FACADE =====
class UltraCanvasOCR {
public:
    explicit UltraCanvasOCR(const OCRConfig& cfg = {});
    ~UltraCanvasOCR();

    UltraCanvasOCR(const UltraCanvasOCR&) = delete;
    UltraCanvasOCR& operator=(const UltraCanvasOCR&) = delete;
    UltraCanvasOCR(UltraCanvasOCR&&) noexcept;
    UltraCanvasOCR& operator=(UltraCanvasOCR&&) noexcept;

    OCRResult RecognizeFile(const std::string& path);
    OCRResult RecognizeBuffer(const void* data, size_t bytes,
                              const std::string& formatHint = "");
    OCRResult RecognizeImage(const vips::VImage& img);

    std::future<OCRResult> RecognizeFileAsync(const std::string& path,
                                              std::stop_token st = {});
    std::future<OCRResult> RecognizeImageAsync(const vips::VImage& img,
                                               std::stop_token st = {});

    void              SetConfig(const OCRConfig& cfg);
    const OCRConfig&  Config() const;
    OCREngineKind     EngineInUse() const;

    static std::vector<OCREngineKind> AvailableEngines();
    static std::string                DefaultModelsDir();
    static const char*                EngineKindName(OCREngineKind);

private:
    void Rebuild();
    std::unique_ptr<IOCREngine> engine;
    OCRConfig                   config;
};

// ===== EXPORTERS =====
std::string OCRResultToHOCR(const OCRResult& result, const Size2Di& imageSize,
                            const std::string& sourceName = "image");
std::string OCRResultToALTO(const OCRResult& result, const Size2Di& imageSize,
                            const std::string& sourceName = "image");
std::string OCRResultToPlainText(const OCRResult& result);

} // namespace UltraCanvas

#endif // ULTRACANVAS_OCR_SUPPORT
