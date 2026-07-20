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

// ===== LANGUAGE DATA TIER =====
// Mirrors the three upstream tessdata release repositories. Downloads pick
// their source from this, trading footprint against recognition quality.
enum class OCRDataTier {
    Fast,      // tessdata_fast  — small & quick, good quality (default)
    Standard,  // tessdata       — the balanced LSTM + legacy models
    Best       // tessdata_best  — largest, highest accuracy
};

// ===== LANGUAGE CATALOGUE ENTRY =====
// One recognisable language from the upstream Tesseract catalogue, whether or
// not its traineddata is installed locally.
struct OCRLanguageInfo {
    std::string code;         // tessdata code, e.g. "deu"
    std::string englishName;  // human-readable name, e.g. "German"
    bool        isScript = false; // helper models (osd, equ), not a language
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

    std::future<OCRResult> RecognizeFileAsync(const std::string& path);
    std::future<OCRResult> RecognizeImageAsync(const vips::VImage& img);

    void              SetConfig(const OCRConfig& cfg);
    const OCRConfig&  Config() const;
    OCREngineKind     EngineInUse() const;

    static std::vector<OCREngineKind> AvailableEngines();
    static std::string                DefaultModelsDir();
    static const char*                EngineKindName(OCREngineKind);

    // ===== LANGUAGE MANAGEMENT =====
    // The full catalogue of languages Tesseract can recognise (the upstream
    // tessdata set). This is what "all available languages" means; it does not
    // imply the data is installed — use InstalledLanguages()/EnsureLanguages()
    // for that. Sorted by English name for display.
    static const std::vector<OCRLanguageInfo>& SupportedLanguages();

    // Look up a catalogue entry by tessdata code; nullptr if unknown.
    static const OCRLanguageInfo* FindLanguage(const std::string& code);

    // Writable per-user directory that downloaded language packs are stored in
    // (…/UltraCanvas/ocr/tessdata). Created on demand by EnsureLanguages().
    static std::string LanguageDataDir();

    // Codes whose "<code>.traineddata" exists in any discoverable data
    // directory (bundle, user pack dir, TESSDATA_PREFIX, system). Sorted,
    // de-duplicated.
    static std::vector<std::string> InstalledLanguages();

    // True when "<code>.traineddata" is present. When dataDir is empty the
    // standard discovery locations are searched; otherwise only dataDir.
    static bool IsLanguageInstalled(const std::string& code,
                                    const std::string& dataDir = "");

    // Fetch a single language pack into destDir (defaults to LanguageDataDir())
    // from the given tessdata tier. Requires network support (UltraNet). On
    // failure returns false and fills outError. Only catalogue codes are
    // accepted. A no-op returning true when the pack is already present.
    static bool DownloadLanguage(const std::string& code,
                                 OCRDataTier tier,
                                 std::string& outError,
                                 const std::string& destDir = "");

    // Make every requested language usable: any missing pack is seeded from a
    // local copy if one exists, else downloaded from `tier`. All packs are
    // consolidated into a single directory (so Tesseract can load them
    // together), the config is pointed at it, and the engine is rebuilt with
    // `codes` as the active languages. Returns false (and fills outError) if a
    // language could not be provisioned.
    bool EnsureLanguages(const std::vector<std::string>& codes,
                         std::string& outError,
                         OCRDataTier tier = OCRDataTier::Fast);

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
