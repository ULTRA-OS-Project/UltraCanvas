// Tests/UltraAITextRecognitionTest.cpp
// UltraAI in front of UltraCanvas's text recognizer
// (UltraAI/ui/include/UltraAITextRecognition.h), with stand-in providers:
//
//   - with only "mock" registered nothing counts as an OCR provider, and the
//     picture goes to the recognizer installed before (here a stand-in for
//     Tesseract);
//   - a provider the user chose (SetDefaultProvider) that declares OCR reads
//     the picture instead: it is handed a PNG of the picture's size, the
//     model that declares OCR, and its text comes back; LastTextRecognition-
//     Provider names it;
//   - a chosen provider without OCR, one that fails, and a cloud provider
//     nobody chose (cloud fallback off) all leave the picture to the fallback;
//   - spans, then document regions in reading order, when a response has no
//     plain OCR text.
//
// Headless.
// Version: 1.0.0
// Last Modified: 2026-11-06
// Author: UltraCanvas Framework

#include "UltraAIRouting.h"
#include "UltraAITextRecognition.h"
#include "UltraAIVisionAnalyzer.h"
#include "UltraCanvasClipboardDib.h"
#include "UltraCanvasFileLoader.h"

#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

// What the stand-in provider saw and answers.
struct Seen {
    int calls = 0;
    std::string model;
    int width = 0, height = 0;
    std::string mime;
};

class FakeVision : public UltraAI::IVisionAnalyzer {
public:
    FakeVision(std::string id, bool ocr, UltraAI::VisionAnalyzeResponse answer, Seen* seen)
        : id_(std::move(id)), ocr_(ocr), answer_(std::move(answer)), seen_(seen) {}
    UltraAI::VisionProviderCapabilities GetCapabilities() const override {
        UltraAI::VisionProviderCapabilities caps;
        caps.providerId = id_;
        UltraAI::VisionModelInfo captions;
        captions.id = "captions-only";
        captions.supportedTasks = {UltraAI::VisionTask::Caption};
        caps.models.push_back(captions);
        if (ocr_) {
            UltraAI::VisionModelInfo reader;
            reader.id = "reader-1";
            reader.supportedTasks = {UltraAI::VisionTask::Caption, UltraAI::VisionTask::Ocr};
            caps.models.push_back(reader);
        }
        return caps;
    }
    UltraAI::VisionAnalyzeResponse Analyze(const UltraAI::VisionAnalyzeRequest& request) override {
        if (seen_) {
            ++seen_->calls;
            seen_->model = request.model;
            seen_->mime = request.image.mimeType;
            ClipboardDib::RgbaImage decoded;
            if (ClipboardDib::DecodePng(request.image.bytes.data(), request.image.bytes.size(), decoded)) {
                seen_->width = decoded.width;
                seen_->height = decoded.height;
            }
        }
        return answer_;
    }
    std::future<UltraAI::VisionAnalyzeResponse> AnalyzeAsync(const UltraAI::VisionAnalyzeRequest& request) override {
        std::promise<UltraAI::VisionAnalyzeResponse> promise;
        promise.set_value(Analyze(request));
        return promise.get_future();
    }

private:
    std::string id_;
    bool ocr_;
    UltraAI::VisionAnalyzeResponse answer_;
    Seen* seen_;
};

UltraAI::VisionAnalyzeResponse Answer(const std::string& text) {
    UltraAI::VisionAnalyzeResponse response;
    response.ocrText = text;
    return response;
}

// A 40 x 20 grey picture, one byte a pixel.
std::vector<uint8_t> g_pixels(40 * 20, 200);

std::string Recognise(std::string& error) {
    const UltraCanvasFileLoader::ImageTextRecognizer recognizer = UltraCanvasFileLoader::GetImageTextRecognizer();
    UltraCanvasFileLoader::TextRecognitionImage image;
    image.pixels = g_pixels.data();
    image.width = 40;
    image.height = 20;
    image.bytesPerPixel = 1;
    image.bytesPerLine = 40;
    std::string text;
    error.clear();
    if (!recognizer || !recognizer(image, text, error)) return "<failed>";
    return text;
}

} // namespace

int main() {
    std::cout << "UltraAITextRecognitionTest" << std::endl;
    UltraAI::ClearDefaultProviders();
    UltraAI::SetCloudFallbackAllowed(false);

    // The framework's recognizer (Tesseract in an application), stood in for.
    int fallbackCalls = 0;
    UltraCanvasFileLoader::SetImageTextRecognizer(
            [&fallbackCalls](const UltraCanvasFileLoader::TextRecognitionImage&, std::string& text, std::string&) {
        ++fallbackCalls;
        text = "from Tesseract";
        return true;
    });
    UltraAI::RegisterUltraAITextRecognition();
    std::string error;

    UltraAI::OcrProviderChoice choice;
    Check(!UltraAI::FindOcrProvider(choice), "only mock registered: no OCR provider (mock never counts)");
    Check(Recognise(error) == "from Tesseract" && fallbackCalls == 1 && UltraAI::LastTextRecognitionProvider().empty(),
          "... so the picture goes to the recognizer installed before");

    // A cloud provider nobody chose, cloud fallback off: not on the route.
    Seen cloudSeen;
    UltraAI::RegisterVisionAnalyzerProvider("test-cloud-ocr", [&cloudSeen](const UltraAI::VisionAnalyzerConfig&,
                                                                            UltraAI::Error*) {
        return std::make_unique<FakeVision>("test-cloud-ocr", true, Answer("from the cloud"), &cloudSeen);
    });
    Check(!UltraAI::FindOcrProvider(choice) && Recognise(error) == "from Tesseract" && cloudSeen.calls == 0,
          "a cloud provider nobody chose is not used while cloud fallback is off");

    // Chosen, with OCR: it reads the picture.
    UltraAI::SetDefaultProvider("visionanalyzer", "test-cloud-ocr");
    Check(UltraAI::FindOcrProvider(choice) && choice.providerId == "test-cloud-ocr" && choice.model == "reader-1",
          "the provider the user chose, declaring OCR: found, with the model that reads text");
    const int before = fallbackCalls;
    Check(Recognise(error) == "from the cloud" && fallbackCalls == before &&
          UltraAI::LastTextRecognitionProvider() == "test-cloud-ocr",
          "... and it reads the picture instead of the built-in OCR");
    Check(cloudSeen.model == "reader-1" && cloudSeen.mime == "image/png" && cloudSeen.width == 40 &&
          cloudSeen.height == 20,
          "... handed a PNG of the picture's size and the model that declares OCR");

    // Chosen but without OCR: the fallback.
    UltraAI::RegisterVisionAnalyzerProvider("test-captions", [](const UltraAI::VisionAnalyzerConfig&, UltraAI::Error*) {
        return std::make_unique<FakeVision>("test-captions", false, Answer("never"), nullptr);
    });
    UltraAI::SetDefaultProvider("visionanalyzer", "test-captions");
    UltraAI::SetCloudFallbackAllowed(false);
    Check(!UltraAI::FindOcrProvider(choice) && Recognise(error) == "from Tesseract",
          "a chosen provider that does not read text: the built-in OCR");

    // Chosen, with OCR, but failing: the fallback.
    UltraAI::RegisterVisionAnalyzerProvider("test-failing", [](const UltraAI::VisionAnalyzerConfig&, UltraAI::Error*) {
        UltraAI::VisionAnalyzeResponse failed;
        failed.error.code = UltraAI::ErrorCode::ProviderError;
        failed.error.message = "quota exceeded";
        return std::make_unique<FakeVision>("test-failing", true, failed, nullptr);
    });
    UltraAI::SetDefaultProvider("visionanalyzer", "test-failing");
    Check(Recognise(error) == "from Tesseract" && UltraAI::LastTextRecognitionProvider().empty(),
          "a provider that fails: the built-in OCR reads the picture after all");

    // Spans, then document regions in reading order.
    UltraAI::RegisterVisionAnalyzerProvider("test-spans", [](const UltraAI::VisionAnalyzerConfig&, UltraAI::Error*) {
        UltraAI::VisionAnalyzeResponse response;
        response.ocrSpans = {{"first line", 0.9, {}, ""}, {"second line", 0.9, {}, ""}};
        return std::make_unique<FakeVision>("test-spans", true, response, nullptr);
    });
    UltraAI::SetDefaultProvider("visionanalyzer", "test-spans");
    Check(Recognise(error) == "first line\nsecond line", "a response with spans only: the spans, a line each");
    UltraAI::RegisterVisionAnalyzerProvider("test-regions", [](const UltraAI::VisionAnalyzerConfig&, UltraAI::Error*) {
        UltraAI::VisionAnalyzeResponse response;
        UltraAI::DocumentRegion second;
        second.text = "Body";
        second.readingOrder = 2;
        UltraAI::DocumentRegion first;
        first.text = "Heading";
        first.readingOrder = 1;
        response.documentRegions = {second, first};
        return std::make_unique<FakeVision>("test-regions", true, response, nullptr);
    });
    UltraAI::SetDefaultProvider("visionanalyzer", "test-regions");
    Check(Recognise(error) == "Heading\n\nBody", "... with document regions only: in reading order");

    // No fallback and no provider: an error that says OCR is missing.
    UltraAI::SetDefaultProvider("visionanalyzer", "");
    UltraCanvasFileLoader::SetImageTextRecognizer(nullptr);
    UltraAI::RegisterUltraAITextRecognition();
    Check(Recognise(error) == "<failed>" && error.find("OCR") != std::string::npos,
          "no provider and nothing before it: an error naming OCR");

    UltraCanvasFileLoader::SetImageTextRecognizer(nullptr);
    UltraAI::ClearDefaultProviders();
    std::cout << (g_failures == 0 ? "PASS" : "FAIL") << " (" << g_failures << " failure(s))" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
