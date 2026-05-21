// TesseractOCREngine.cpp
// Tier-1 Tesseract 5 backend implementation.
// Version: 0.1.0
// Last Modified: 2026-05-21
// Author: UltraCanvas Framework

#include "TesseractOCREngine.h"

#if defined(ULTRACANVAS_OCR_SUPPORT) && defined(ULTRACANVAS_OCR_TESSERACT_SUPPORT)

#include <tesseract/baseapi.h>
#include <tesseract/ocrclass.h>
#include <tesseract/publictypes.h>
#include <tesseract/resultiterator.h>
#include <leptonica/allheaders.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <sstream>

namespace UltraCanvas {

namespace {

tesseract::PageSegMode ToTesseractPSM(OCRPageSegmentation s) {
    using T = tesseract::PageSegMode;
    switch (s) {
        case OCRPageSegmentation::SingleBlock: return T::PSM_SINGLE_BLOCK;
        case OCRPageSegmentation::SingleLine:  return T::PSM_SINGLE_LINE;
        case OCRPageSegmentation::SingleWord:  return T::PSM_SINGLE_WORD;
        case OCRPageSegmentation::SparseText:  return T::PSM_SPARSE_TEXT;
        case OCRPageSegmentation::RawLine:     return T::PSM_RAW_LINE;
        case OCRPageSegmentation::Auto:
        default:                               return T::PSM_AUTO;
    }
}

std::string JoinLanguages(const std::vector<std::string>& langs) {
    if (langs.empty()) return "eng";
    std::string out;
    for (size_t i = 0; i < langs.size(); ++i) {
        if (i) out += '+';
        out += langs[i];
    }
    return out;
}

} // namespace

TesseractOCREngine::TesseractOCREngine() = default;
TesseractOCREngine::~TesseractOCREngine() {
    std::lock_guard<std::mutex> lock(mtx);
    if (api) api->End();
}

std::string TesseractOCREngine::ResolveDataPath(const std::string& userPath) const {
    if (!userPath.empty()) return userPath;
    if (const char* env = std::getenv("TESSDATA_PREFIX"); env && *env) return env;
    return {}; // let tesseract probe its built-in locations
}

bool TesseractOCREngine::Initialize(const OCRConfig& cfg) {
    std::lock_guard<std::mutex> lock(mtx);
    config = cfg;
    api = std::make_unique<tesseract::TessBaseAPI>();

    const std::string dataPath = ResolveDataPath(cfg.dataPath);
    const std::string langs    = JoinLanguages(cfg.languages);

    const int rc = api->Init(dataPath.empty() ? nullptr : dataPath.c_str(),
                             langs.c_str(),
                             tesseract::OcrEngineMode::OEM_LSTM_ONLY);
    if (rc != 0) {
        api.reset();
        ready = false;
        return false;
    }

    api->SetPageSegMode(ToTesseractPSM(cfg.segmentation));
    if (cfg.dpiHint > 0) {
        api->SetVariable("user_defined_dpi", std::to_string(cfg.dpiHint).c_str());
    }
    ready = true;
    return true;
}

OCRResult TesseractOCREngine::ExtractResult() {
    OCRResult result;
    result.engineUsed = OCREngineKind::Tesseract;

    if (!api) {
        result.error = "Tesseract engine not initialised";
        return result;
    }

    api->Recognize(nullptr);

    if (char* utf8 = api->GetUTF8Text(); utf8) {
        result.plainText.assign(utf8);
        delete[] utf8;
    }

    std::unique_ptr<tesseract::ResultIterator> it(api->GetIterator());
    if (!it) return result;

    constexpr auto WORD  = tesseract::RIL_WORD;
    constexpr auto LINE  = tesseract::RIL_TEXTLINE;
    constexpr auto PARA  = tesseract::RIL_PARA;
    constexpr auto BLOCK = tesseract::RIL_BLOCK;

    int blockIdx = -1, paraIdx = -1, lineIdx = -1;
    OCRLine currentLine;
    bool    haveLine = false;
    auto flushLine = [&] {
        if (!haveLine) return;
        result.lines.push_back(std::move(currentLine));
        currentLine = OCRLine{};
        haveLine = false;
    };

    do {
        if (it->IsAtBeginningOf(BLOCK)) {
            ++blockIdx;
            int x1, y1, x2, y2;
            if (it->BoundingBox(BLOCK, &x1, &y1, &x2, &y2))
                result.blocks.emplace_back(x1, y1, x2 - x1, y2 - y1);
        }
        if (it->IsAtBeginningOf(PARA)) {
            ++paraIdx;
            int x1, y1, x2, y2;
            if (it->BoundingBox(PARA, &x1, &y1, &x2, &y2))
                result.paragraphs.emplace_back(x1, y1, x2 - x1, y2 - y1);
        }
        if (it->IsAtBeginningOf(LINE)) {
            flushLine();
            ++lineIdx;
            int x1, y1, x2, y2;
            if (it->BoundingBox(LINE, &x1, &y1, &x2, &y2))
                currentLine.box = Rect2Di(x1, y1, x2 - x1, y2 - y1);
            haveLine = true;
        }

        if (char* wt = it->GetUTF8Text(WORD); wt) {
            const float conf = it->Confidence(WORD) / 100.0f;
            if (conf >= config.minConfidence && *wt != '\0') {
                int x1, y1, x2, y2;
                if (it->BoundingBox(WORD, &x1, &y1, &x2, &y2)) {
                    OCRWord w;
                    w.text       = wt;
                    w.box        = Rect2Di(x1, y1, x2 - x1, y2 - y1);
                    w.confidence = conf;
                    w.lineIndex  = lineIdx;
                    w.paragraphIndex = paraIdx;
                    w.blockIndex = blockIdx;
                    if (!currentLine.text.empty()) currentLine.text += ' ';
                    currentLine.text += w.text;
                    currentLine.wordIndices.push_back((int)result.words.size());
                    result.words.push_back(std::move(w));
                }
            }
            delete[] wt;
        }
    } while (it->Next(WORD));

    flushLine();
    return result;
}

OCRResult TesseractOCREngine::RecognizeFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(mtx);
    if (!ready) { OCRResult r; r.error = "Engine not initialised"; return r; }

    const auto t0 = std::chrono::steady_clock::now();
    Pix* pix = pixRead(path.c_str());
    if (!pix) {
        OCRResult r; r.error = "Failed to read image: " + path; return r;
    }
    api->SetImage(pix);
    OCRResult result = ExtractResult();
    api->Clear();
    pixDestroy(&pix);
    result.elapsedMs = std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - t0).count();
    return result;
}

OCRResult TesseractOCREngine::RecognizeBuffer(const void* data, size_t bytes,
                                              const std::string& /*formatHint*/) {
    std::lock_guard<std::mutex> lock(mtx);
    if (!ready) { OCRResult r; r.error = "Engine not initialised"; return r; }

    const auto t0 = std::chrono::steady_clock::now();
    Pix* pix = pixReadMem(static_cast<const l_uint8*>(data), bytes);
    if (!pix) {
        OCRResult r; r.error = "Failed to decode image buffer"; return r;
    }
    api->SetImage(pix);
    OCRResult result = ExtractResult();
    api->Clear();
    pixDestroy(&pix);
    result.elapsedMs = std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - t0).count();
    return result;
}

OCRResult TesseractOCREngine::RecognizeRaw(const uint8_t* pixels, int width, int height,
                                           int bytesPerPixel, int bytesPerLine) {
    std::lock_guard<std::mutex> lock(mtx);
    if (!ready) { OCRResult r; r.error = "Engine not initialised"; return r; }

    const auto t0 = std::chrono::steady_clock::now();
    api->SetImage(pixels, width, height, bytesPerPixel, bytesPerLine);
    OCRResult result = ExtractResult();
    api->Clear();
    result.elapsedMs = std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - t0).count();
    return result;
}

std::vector<std::string> TesseractOCREngine::AvailableLanguages() const {
    std::lock_guard<std::mutex> lock(mtx);
    std::vector<std::string> out;
    if (!api) return out;
    api->GetAvailableLanguagesAsVector(&out); // Tesseract 5: std::vector<std::string>
    return out;
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_OCR_SUPPORT && ULTRACANVAS_OCR_TESSERACT_SUPPORT
