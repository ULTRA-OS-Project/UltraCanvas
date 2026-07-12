// UltraCanvasOCRPlugin.cpp
// OCR plugin facade: engine selection, image bridging, and exporters.
// Version: 0.1.0
// Last Modified: 2026-05-21
// Author: UltraCanvas Framework

#include "UltraCanvasOCRPlugin.h"

#ifdef ULTRACANVAS_OCR_SUPPORT

//#include "PixelFX/PixelFX.h"

#include <cstring>
#include <iomanip>
#include <sstream>
#include <thread>
#include <utility>

#ifdef ULTRACANVAS_OCR_TESSERACT_SUPPORT
#include "TesseractOCREngine.h"
#endif

namespace UltraCanvas {

// ===== ENGINE FACTORY =====
namespace {

std::unique_ptr<IOCREngine> MakeEngine(OCREngineKind kind) {
    switch (kind) {
#ifdef ULTRACANVAS_OCR_TESSERACT_SUPPORT
        case OCREngineKind::Tesseract:
        case OCREngineKind::Auto:
            return std::make_unique<TesseractOCREngine>();
#endif
        default:
            return nullptr;
    }
}

// Convert a VImage to a tightly-packed 8-bit raster Tesseract can consume.
// Returns false on conversion failure.
bool ImageToRaw(const vips::VImage& src,
                std::vector<uint8_t>& out,
                int& outW, int& outH, int& outBpp, int& outStride) {
    try {
        // Force RGB / 8-bit; Tesseract handles greyscale internally.
        vips::VImage img = src; // VImage inherits from vips::VImage
        if (img.format() != VIPS_FORMAT_UCHAR) {
            img = img.cast(VIPS_FORMAT_UCHAR);
        }
        if (img.bands() == 1) {
            // keep greyscale
        } else if (img.bands() == 4) {
            img = img.flatten();                 // drop alpha onto white
            img = img.colourspace(VIPS_INTERPRETATION_sRGB);
        } else {
            img = img.colourspace(VIPS_INTERPRETATION_sRGB);
        }

        outW = img.width();
        outH = img.height();
        outBpp = img.bands(); // 1 or 3
        outStride = outW * outBpp;

        size_t bytes = 0;
        void* mem = img.write_to_memory(&bytes);
        if (!mem || bytes < static_cast<size_t>(outStride) * outH) {
            if (mem) g_free(mem);
            return false;
        }
        out.assign(static_cast<uint8_t*>(mem),
                   static_cast<uint8_t*>(mem) + bytes);
        g_free(mem);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void XmlEscape(std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '&':  out += "&amp;";  break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += c;        break;
        }
    }
    s.swap(out);
}

} // namespace

// ===== FACADE =====
UltraCanvasOCR::UltraCanvasOCR(const OCRConfig& cfg) : config(cfg) {
    Rebuild();
}

UltraCanvasOCR::~UltraCanvasOCR() = default;
UltraCanvasOCR::UltraCanvasOCR(UltraCanvasOCR&&) noexcept = default;
UltraCanvasOCR& UltraCanvasOCR::operator=(UltraCanvasOCR&&) noexcept = default;

void UltraCanvasOCR::Rebuild() {
    engine = MakeEngine(config.engine);
    if (engine) engine->Initialize(config);
}

void UltraCanvasOCR::SetConfig(const OCRConfig& cfg) {
    config = cfg;
    Rebuild();
}

const OCRConfig& UltraCanvasOCR::Config() const { return config; }

OCREngineKind UltraCanvasOCR::EngineInUse() const {
    return engine ? engine->Kind() : OCREngineKind::Auto;
}

OCRResult UltraCanvasOCR::RecognizeFile(const std::string& path) {
    if (!engine) { OCRResult r; r.error = "No OCR engine available"; return r; }
    return engine->RecognizeFile(path);
}

OCRResult UltraCanvasOCR::RecognizeBuffer(const void* data, size_t bytes,
                                          const std::string& formatHint) {
    if (!engine) { OCRResult r; r.error = "No OCR engine available"; return r; }
    return engine->RecognizeBuffer(data, bytes, formatHint);
}

OCRResult UltraCanvasOCR::RecognizeImage(const vips::VImage& img) {
    if (!engine) { OCRResult r; r.error = "No OCR engine available"; return r; }
    std::vector<uint8_t> raw;
    int w = 0, h = 0, bpp = 0, stride = 0;
    if (!ImageToRaw(img, raw, w, h, bpp, stride)) {
        OCRResult r; r.error = "Failed to convert VImage to raw raster";
        return r;
    }
    return engine->RecognizeRaw(raw.data(), w, h, bpp, stride);
}

std::future<OCRResult> UltraCanvasOCR::RecognizeFileAsync(const std::string& path,
                                                          std::stop_token /*st*/) {
    // Engines are not yet cancellation-aware (Phase 2). The stop_token is
    // accepted now so callers can be written against the final shape.
    return std::async(std::launch::async,
                      [this, path]() { return RecognizeFile(path); });
}

std::future<OCRResult> UltraCanvasOCR::RecognizeImageAsync(const vips::VImage& img,
                                                           std::stop_token /*st*/) {
    return std::async(std::launch::async,
                      [this, img]() { return RecognizeImage(img); });
}

std::vector<OCREngineKind> UltraCanvasOCR::AvailableEngines() {
    std::vector<OCREngineKind> out;
#ifdef ULTRACANVAS_OCR_TESSERACT_SUPPORT
    out.push_back(OCREngineKind::Tesseract);
#endif
    return out;
}

std::string UltraCanvasOCR::DefaultModelsDir() {
    // Resolved against the bundled media/ocr/ directory by the host app.
    return "media/ocr";
}

const char* UltraCanvasOCR::EngineKindName(OCREngineKind k) {
    switch (k) {
        case OCREngineKind::Auto:         return "Auto";
        case OCREngineKind::Tesseract:    return "Tesseract";
        case OCREngineKind::ONNX:         return "ONNX";
        case OCREngineKind::AppleVision:  return "AppleVision";
        case OCREngineKind::WindowsMedia: return "WindowsMedia";
    }
    return "Unknown";
}

// ===== EXPORTERS =====
std::string OCRResultToPlainText(const OCRResult& result) {
    if (!result.plainText.empty()) return result.plainText;
    std::string out;
    for (const auto& line : result.lines) {
        out += line.text;
        out += '\n';
    }
    return out;
}

std::string OCRResultToHOCR(const OCRResult& result, const Size2Di& imageSize,
                            const std::string& sourceName) {
    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\""
        << " \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">\n"
        << "<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"en\">\n"
        << " <head>\n"
        << "  <meta http-equiv=\"Content-Type\" content=\"text/html;charset=utf-8\"/>\n"
        << "  <meta name=\"ocr-system\" content=\"UltraCanvasOCR\"/>\n"
        << "  <meta name=\"ocr-capabilities\""
        << " content=\"ocr_page ocr_carea ocr_par ocr_line ocrx_word\"/>\n"
        << " </head>\n <body>\n";

    std::string src = sourceName; XmlEscape(src);
    out << "  <div class=\"ocr_page\" id=\"page_1\""
        << " title=\"image &quot;" << src << "&quot;; bbox 0 0 "
        << imageSize.width << ' ' << imageSize.height << "; ppageno 0\">\n";

    int lineId = 0, wordId = 0;
    for (const auto& line : result.lines) {
        out << "   <span class=\"ocr_line\" id=\"line_" << (++lineId) << "\""
            << " title=\"bbox " << line.box.x << ' ' << line.box.y << ' '
            << (line.box.x + line.box.width) << ' '
            << (line.box.y + line.box.height) << "\">";
        for (int idx : line.wordIndices) {
            if (idx < 0 || idx >= (int)result.words.size()) continue;
            const auto& w = result.words[idx];
            std::string t = w.text; XmlEscape(t);
            out << "<span class=\"ocrx_word\" id=\"word_" << (++wordId) << "\""
                << " title=\"bbox " << w.box.x << ' ' << w.box.y << ' '
                << (w.box.x + w.box.width) << ' ' << (w.box.y + w.box.height)
                << "; x_wconf " << (int)(w.confidence * 100.0f) << "\">"
                << t << "</span> ";
        }
        out << "</span>\n";
    }
    out << "  </div>\n </body>\n</html>\n";
    return out.str();
}

std::string OCRResultToALTO(const OCRResult& result, const Size2Di& imageSize,
                            const std::string& sourceName) {
    std::ostringstream out;
    std::string src = sourceName; XmlEscape(src);
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<alto xmlns=\"http://www.loc.gov/standards/alto/ns-v4#\">\n"
        << " <Description>\n"
        << "  <MeasurementUnit>pixel</MeasurementUnit>\n"
        << "  <sourceImageInformation><fileName>" << src
        << "</fileName></sourceImageInformation>\n"
        << " </Description>\n"
        << " <Layout>\n"
        << "  <Page ID=\"page1\" PHYSICAL_IMG_NR=\"1\""
        << " WIDTH=\""  << imageSize.width  << "\""
        << " HEIGHT=\"" << imageSize.height << "\">\n"
        << "   <PrintSpace HPOS=\"0\" VPOS=\"0\""
        << " WIDTH=\""  << imageSize.width  << "\""
        << " HEIGHT=\"" << imageSize.height << "\">\n";

    int lineId = 0, wordId = 0;
    for (const auto& line : result.lines) {
        ++lineId;
        out << "    <TextLine ID=\"line_" << lineId << "\""
            << " HPOS=\""   << line.box.x      << "\""
            << " VPOS=\""   << line.box.y      << "\""
            << " WIDTH=\""  << line.box.width  << "\""
            << " HEIGHT=\"" << line.box.height << "\">\n";
        for (int idx : line.wordIndices) {
            if (idx < 0 || idx >= (int)result.words.size()) continue;
            const auto& w = result.words[idx];
            std::string t = w.text; XmlEscape(t);
            ++wordId;
            out << "     <String ID=\"word_" << wordId << "\""
                << " CONTENT=\""  << t << "\""
                << " HPOS=\""     << w.box.x      << "\""
                << " VPOS=\""     << w.box.y      << "\""
                << " WIDTH=\""    << w.box.width  << "\""
                << " HEIGHT=\""   << w.box.height << "\""
                << " WC=\""       << std::fixed << std::setprecision(2)
                                  << w.confidence << "\"/>\n";
        }
        out << "    </TextLine>\n";
    }

    out << "   </PrintSpace>\n  </Page>\n </Layout>\n</alto>\n";
    return out.str();
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_OCR_SUPPORT
