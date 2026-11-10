// TesseractOCREngine.cpp
// Tier-1 Tesseract 5 backend implementation.
// Version: 0.1.3 - builds against Tesseract 4.1 (no version.h; GenericVector languages)
// Version: 0.1.2 - the language files read through OpenFileUtf8 (Tesseract 5's FileReader)
// Version: 0.1.1 - RecognizeFile reads the file through OpenFileUtf8 (a UTF-8 name on Windows)
// Version: 0.1.0
// Last Modified: 2026-11-06
// Author: UltraCanvas Framework

#include "TesseractOCREngine.h"
#include "UltraCanvasPathUtf8.h"

#if defined(ULTRACANVAS_OCR_SUPPORT) && defined(ULTRACANVAS_OCR_TESSERACT_SUPPORT)

#include <tesseract/baseapi.h>
// TESSERACT_MAJOR_VERSION: Tesseract 5 has version.h, 4.x (Ubuntu 22.04's
// 4.1) tess_version.h; 4.x takes the reader-less paths below.
#if __has_include(<tesseract/version.h>)
#include <tesseract/version.h>
#elif __has_include(<tesseract/tess_version.h>)
#include <tesseract/tess_version.h>   // Tesseract 4.x
#endif
#if defined(TESSERACT_MAJOR_VERSION) && TESSERACT_MAJOR_VERSION < 5
#include <tesseract/genericvector.h>  // Tesseract 4's language list is a GenericVector<STRING>
#include <tesseract/strngs.h>
#endif
#if defined(TESSERACT_MAJOR_VERSION) && TESSERACT_MAJOR_VERSION >= 5
#define UC_TESSERACT_HAS_FILE_READER 1
#endif
#include <tesseract/ocrclass.h>
#include <tesseract/publictypes.h>
#include <tesseract/resultiterator.h>
#if defined(__APPLE__)
#include <allheaders.h>
#else
#include <leptonica/allheaders.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <system_error>
#include <vector>

#include "UltraCanvasUtils.h"   // GetExecutableDir()
#include "UltraCanvasConfig.h"  // GetResourcesDir()

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

std::string TesseractOCREngine::ResolveDataPath(const std::string& userPath,
                                                const std::string& firstLang,
                                                std::vector<std::string>& tried) const {
    namespace fs = std::filesystem;

    // Tesseract 4/5 expects the data directory to *directly* contain the
    // "<lang>.traineddata" files (the old "parent of tessdata" convention was
    // dropped). For each candidate base we therefore accept either the base
    // itself or a "tessdata" subfolder underneath it.
    const std::string leaf = firstLang + ".traineddata";

    auto probe = [&](const std::string& base) -> std::string {
        if (base.empty()) return {};
        std::error_code ec;
        const fs::path b(UltraCanvas::PathFromUtf8(base));
        const fs::path direct = b / PathFromUtf8(leaf);
        tried.push_back(PathToUtf8(direct));
        if (fs::exists(direct, ec)) return PathToUtf8(b);
        const fs::path nested = b / "tessdata" / PathFromUtf8(leaf);
        tried.push_back(PathToUtf8(nested));
        if (fs::exists(nested, ec)) return PathToUtf8(b / "tessdata");
        return {};
    };

    // 1) Explicit override from the caller wins.
    if (!userPath.empty()) {
        if (std::string hit = probe(userPath); !hit.empty()) return hit;
    }
    // 2) The writable per-user pack directory, where languages fetched on
    //    demand by UltraCanvasOCR::EnsureLanguages()/DownloadLanguage() land.
    //    Probed early so a downloaded language is preferred over a stale bundle.
    if (std::string hit = probe(UltraCanvasOCR::LanguageDataDir()); !hit.empty())
        return hit;
    // 3) TESSDATA_PREFIX, honouring the user's environment.
    // Read as UTF-8 (GetEnvUtf8): a narrow getenv answers in the ANSI code page on Windows.
    if (const std::string env = UltraCanvas::GetEnvUtf8("TESSDATA_PREFIX"); !env.empty()) {
        if (std::string hit = probe(env); !hit.empty()) return hit;
    }
    // 4) Bundle locations relative to the executable and the resources dir.
    //    This is what makes a portable Windows build work without any env
    //    vars: the shipped media/ocr/tessdata/ folder is discovered here.
    const std::string exe = GetExecutableDir();
    const std::string res = GetResourcesDir(); // already ends with a separator
    for (const std::string& base : {
             res + "media/ocr",
             exe + "/media/ocr",
             res,                       // <Resources>/tessdata
             exe + "/ocr",
             exe,                       // next to the executable
         }) {
        if (std::string hit = probe(base); !hit.empty()) return hit;
    }
    return {}; // nothing found; caller falls back to tesseract's own probing
}

#ifdef UC_TESSERACT_HAS_FILE_READER
namespace {
// Tesseract's FileReader: the whole file, opened by its UTF-8 name.
bool ReadTessdataFile(const char* filename, std::vector<char>* data) {
    if (!filename || !data) return false;
    std::FILE* file = OpenFileUtf8(filename, "rb");
    if (!file) return false;
    data->clear();
    char buffer[65536];
    size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) data->insert(data->end(), buffer, buffer + read);
    const bool failed = std::ferror(file) != 0;
    std::fclose(file);
    return !failed && !data->empty();
}
} // namespace
#endif

bool TesseractOCREngine::Initialize(const OCRConfig& cfg) {
    std::lock_guard<std::mutex> lock(mtx);
    config = cfg;
    lastError.clear();
    api = std::make_unique<tesseract::TessBaseAPI>();

    const std::string firstLang = cfg.languages.empty() ? "eng"
                                                         : cfg.languages.front();
    std::vector<std::string> tried;
    const std::string dataPath = ResolveDataPath(cfg.dataPath, firstLang, tried);
    const std::string langs    = JoinLanguages(cfg.languages);

    // On Linux the system tessdata is found through Tesseract's compiled-in
    // default path even when `dataPath` is empty; on a portable Windows build
    // it is not, so an empty result here is the usual cause of failure.
#ifdef UC_TESSERACT_HAS_FILE_READER
    // The language files are read through ReadTessdataFile (OpenFileUtf8):
    // Tesseract's own reader opens them with fopen, which takes the name in
    // the ANSI code page on Windows, so a data folder under a Thai or emoji
    // user name or install path failed to load there.
    const int rc = api->Init(dataPath.c_str(), 0, langs.c_str(),
                             tesseract::OcrEngineMode::OEM_LSTM_ONLY,
                             nullptr, 0, nullptr, nullptr, false, &ReadTessdataFile);
#else
    // Tesseract 4's FileReader takes its own string and vector types; it
    // reads by itself (no Windows build uses Tesseract 4).
    const int rc = api->Init(dataPath.empty() ? nullptr : dataPath.c_str(),
                             langs.c_str(),
                             tesseract::OcrEngineMode::OEM_LSTM_ONLY);
#endif
    if (rc != 0) {
        api.reset();
        ready = false;
        std::ostringstream msg;
        msg << "could not load language '" << langs << "' — no "
            << firstLang << ".traineddata found. ";
        if (dataPath.empty()) {
            msg << "Bundle it under \"" << UltraCanvasOCR::DefaultModelsDir()
                << "/tessdata/\" beside the application, or set TESSDATA_PREFIX. ";
            if (!tried.empty()) {
                msg << "Searched:";
                for (const std::string& p : tried) msg << "\n  " << p;
            }
        } else {
            msg << "tessdata dir \"" << dataPath
                << "\" exists but Tesseract rejected it (corrupt or wrong "
                   "version?).";
        }
        lastError = msg.str();
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
    if (!ready) {
        OCRResult r;
        r.error = lastError.empty() ? std::string("Engine not initialised")
                                    : "Engine not initialised: " + lastError;
        return r;
    }

    const auto t0 = std::chrono::steady_clock::now();
    // Leptonica's pixRead takes the name in the ANSI code page on Windows,
    // so a Thai or emoji folder failed there: the bytes are read here, by
    // the UTF-8 name, and decoded from memory.
    std::vector<l_uint8> bytes;
    if (std::FILE* file = OpenFileUtf8(path, "rb")) {
        l_uint8 buffer[65536];
        size_t read = 0;
        while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
            bytes.insert(bytes.end(), buffer, buffer + read);
        std::fclose(file);
    }
    Pix* pix = bytes.empty() ? nullptr : pixReadMem(bytes.data(), bytes.size());
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
    if (!ready) {
        OCRResult r;
        r.error = lastError.empty() ? std::string("Engine not initialised")
                                    : "Engine not initialised: " + lastError;
        return r;
    }

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
    if (!ready) {
        OCRResult r;
        r.error = lastError.empty() ? std::string("Engine not initialised")
                                    : "Engine not initialised: " + lastError;
        return r;
    }

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
#if defined(TESSERACT_MAJOR_VERSION) && TESSERACT_MAJOR_VERSION < 5
    // Tesseract 4: its own vector and string types.
    GenericVector<STRING> languages;
    api->GetAvailableLanguagesAsVector(&languages);
    for (int i = 0; i < languages.size(); ++i)
        out.emplace_back(languages[i].string());   // path-string-ok: Tesseract 4's STRING holding a language code, not a path
#else
    api->GetAvailableLanguagesAsVector(&out); // Tesseract 5: std::vector<std::string>
#endif
    return out;
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_OCR_SUPPORT && ULTRACANVAS_OCR_TESSERACT_SUPPORT
