// UltraAI/ui/src/TextRecognition.cpp
// UltraAI in front of UltraCanvas's text recognizer. See
// include/UltraAITextRecognition.h.
// Version: 1.0.0
// Last Modified: 2026-11-06
// Author: UltraAI Module

#include "UltraAITextRecognition.h"

#include "UltraAIRouting.h"
#include "UltraAIVisionAnalyzer.h"
#include "UltraCanvasClipboardDib.h"   // EncodePng
#include "UltraCanvasFileLoader.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace UltraAI {

namespace {

thread_local std::string g_lastProvider;

bool ReadsText(const VisionModelInfo& model) {
    return std::find(model.supportedTasks.begin(), model.supportedTasks.end(), VisionTask::Ocr) !=
           model.supportedTasks.end();
}

// The first provider on the route that constructs and declares OCR; the
// analyzer itself in `analyzer`.
bool FindProvider(OcrProviderChoice& out, std::unique_ptr<IVisionAnalyzer>* analyzer) {
    for (const std::string& id : ResolveProviderOrder("visionanalyzer", ListVisionAnalyzerProviders())) {
        if (id == "mock") continue;   // the test double reads nothing
        VisionAnalyzerConfig config;
        config.providerId = id;
        Error error;
        std::unique_ptr<IVisionAnalyzer> candidate = CreateVisionAnalyzer(config, &error);
        if (!candidate) continue;
        const VisionProviderCapabilities capabilities = candidate->GetCapabilities();
        const auto model = std::find_if(capabilities.models.begin(), capabilities.models.end(), ReadsText);
        if (model == capabilities.models.end()) continue;
        out.providerId = id;
        out.model = model->id;
        if (analyzer) *analyzer = std::move(candidate);
        return true;
    }
    return false;
}

// The picture as a PNG: what every vision provider takes. 1 (grey), 3 (RGB)
// or 4 (RGBA) bytes a pixel, rows `bytesPerLine` apart.
bool PictureAsPng(const UltraCanvas::UltraCanvasFileLoader::TextRecognitionImage& image, std::vector<uint8_t>& png) {
    if (!image.pixels || image.width <= 0 || image.height <= 0) return false;
    const int bpp = image.bytesPerPixel;
    if (bpp != 1 && bpp != 3 && bpp != 4) return false;
    UltraCanvas::ClipboardDib::RgbaImage rgba;
    rgba.width = image.width;
    rgba.height = image.height;
    rgba.pixels.resize(static_cast<size_t>(image.width) * image.height * 4);
    size_t o = 0;
    for (int y = 0; y < image.height; ++y) {
        const uint8_t* row = image.pixels + static_cast<size_t>(y) * image.bytesPerLine;
        for (int x = 0; x < image.width; ++x) {
            const uint8_t* p = row + static_cast<size_t>(x) * bpp;
            rgba.pixels[o++] = p[0];
            rgba.pixels[o++] = bpp == 1 ? p[0] : p[1];
            rgba.pixels[o++] = bpp == 1 ? p[0] : p[2];
            rgba.pixels[o++] = 255;   // the pixels come on white already
        }
    }
    return UltraCanvas::ClipboardDib::EncodePng(rgba, png);
}

// The words of a response: the OCR text, else its spans, else the document
// regions in reading order.
std::string TextOf(const VisionAnalyzeResponse& response) {
    if (!response.ocrText.empty()) return response.ocrText;
    std::string text;
    for (const OcrSpan& span : response.ocrSpans) {
        if (span.text.empty()) continue;
        if (!text.empty()) text += "\n";
        text += span.text;
    }
    if (!text.empty()) return text;
    std::vector<DocumentRegion> regions = response.documentRegions;
    std::stable_sort(regions.begin(), regions.end(),
                     [](const DocumentRegion& a, const DocumentRegion& b) { return a.readingOrder < b.readingOrder; });
    for (const DocumentRegion& region : regions) {
        if (region.text.empty()) continue;
        if (!text.empty()) text += "\n\n";
        text += region.text;
    }
    return text;
}

} // namespace

bool FindOcrProvider(OcrProviderChoice& out) {
    out = {};
    return FindProvider(out, nullptr);
}

std::string LastTextRecognitionProvider() { return g_lastProvider; }

void RegisterUltraAITextRecognition() {
    using UltraCanvas::UltraCanvasFileLoader;
    UltraCanvasFileLoader::ImageTextRecognizer fallback = UltraCanvasFileLoader::GetImageTextRecognizer();
    UltraCanvasFileLoader::SetImageTextRecognizer(
            [fallback](const UltraCanvasFileLoader::TextRecognitionImage& image, std::string& text,
                       std::string& error) {
        g_lastProvider.clear();
        OcrProviderChoice choice;
        std::unique_ptr<IVisionAnalyzer> analyzer;
        std::string aiError;
        std::vector<uint8_t> png;
        if (FindProvider(choice, &analyzer) && PictureAsPng(image, png)) {
            VisionAnalyzeRequest request;
            request.model = choice.model;
            request.tasks = {VisionTask::Ocr};
            request.image.bytes = std::move(png);
            request.image.mimeType = "image/png";
            request.image.filename = "picture.png";
            const VisionAnalyzeResponse response = analyzer->Analyze(request);
            if (response.error.IsOk()) {
                text = TextOf(response);
                g_lastProvider = choice.providerId;
                return true;
            }
            aiError = response.error.message.empty() ? choice.providerId : choice.providerId + ": " +
                                                                             response.error.message;
        }
        // No provider reads text, or it failed: the recognizer before this one.
        if (fallback) return fallback(image, text, error);
        // Both texts are the standard set's (UltraCanvasFileLoaderText.cpp has
        // them), so they arrive translated although this module's sources
        // are not extracted.
        error = aiError.empty()
                        ? UltraCanvas::Tr("Text recognition (OCR) is not available: this build has no OCR engine.")
                        : UltraCanvas::Tr("Text recognition failed.") + "\n" + aiError;
        return false;
    });
}

} // namespace UltraAI
