// TesseractOCREngine.h
// Tier-1 Tesseract 5 backend for the UltraCanvas OCR plugin.
// Version: 0.1.0
// Last Modified: 2026-05-21
// Author: UltraCanvas Framework
#pragma once

#ifdef ULTRACANVAS_OCR_SUPPORT
#ifdef ULTRACANVAS_OCR_TESSERACT_SUPPORT

#include "UltraCanvasOCRPlugin.h"
#include <memory>
#include <mutex>

namespace tesseract { class TessBaseAPI; }

namespace UltraCanvas {

class TesseractOCREngine final : public IOCREngine {
public:
    TesseractOCREngine();
    ~TesseractOCREngine() override;

    bool Initialize(const OCRConfig& cfg) override;

    OCRResult RecognizeFile(const std::string& path) override;
    OCRResult RecognizeBuffer(const void* data, size_t bytes,
                              const std::string& formatHint) override;
    OCRResult RecognizeRaw(const uint8_t* pixels, int width, int height,
                           int bytesPerPixel, int bytesPerLine) override;

    std::vector<std::string> AvailableLanguages() const override;
    OCREngineKind            Kind() const override { return OCREngineKind::Tesseract; }

private:
    OCRResult ExtractResult();
    // Resolves a directory that directly contains "<firstLang>.traineddata".
    // Probes the explicit override, TESSDATA_PREFIX, and the executable /
    // resources relative bundle locations. Returns an empty string when none
    // of the candidates hold the data; every path tried is appended to `tried`.
    std::string ResolveDataPath(const std::string& userPath,
                                const std::string& firstLang,
                                std::vector<std::string>& tried) const;

    std::unique_ptr<tesseract::TessBaseAPI> api;
    OCRConfig                                config;
    mutable std::mutex                       mtx;
    bool                                     ready = false;
    std::string                              lastError; // why Init last failed
};

} // namespace UltraCanvas

#endif // ULTRACANVAS_OCR_TESSERACT_SUPPORT
#endif // ULTRACANVAS_OCR_SUPPORT
