// UltraAI/ui/include/UltraAITextRecognition.h
// UltraAI as UltraCanvas's text recognizer: when an UltraAI vision provider
// that reads text (VisionTask::Ocr) is available, the words in a picture and
// in a scanned PDF page come from it instead of the framework's own OCR
// (Tesseract, the OCR plugin) - UltraCanvasFileLoader::ExtractText, and so
// UltraClipboard's Extract text.
//
// "Available" is UltraAI's default route (UltraAIRouting.h): the provider the
// user or the system set for "visionanalyzer", then the providers on this
// computer, a cloud service only when it was chosen or cloud fallback is
// allowed - never the in-process "mock". Of those, the first that constructs
// and declares OCR on one of its models (GetCapabilities) reads the picture.
// The choice is made at every picture, so a provider set up or a model
// downloaded while the application runs is used from the next one on.
//
// When no such provider is there, or it fails (offline, a quota, an error),
// the picture goes to the recognizer that was installed before this one -
// so install the framework's first:
//
//   RegisterOCRTextRecognition();        // Tesseract (UltraCanvasOCRPlugin.h)
//   UltraAI::RegisterUltraAITextRecognition();   // ... with UltraAI in front
//
// doc-check: example
//   UltraAI::OcrProviderChoice choice;
//   if (UltraAI::FindOcrProvider(choice)) { /* choice.providerId reads text */ }
//
// Version: 1.0.0
// Last Modified: 2026-11-06
// Author: UltraAI Module
#pragma once

#include <string>

namespace UltraAI {

// The provider that would read a picture now, and the model of it that
// declares OCR (empty when the provider names none).
struct OcrProviderChoice {
    std::string providerId;
    std::string model;
};

// True, with `out` filled, when a provider on the default route reads text
// (see the top of this file). Constructs the providers to ask them; cheap for
// the local ones, no network call for any.
bool FindOcrProvider(OcrProviderChoice& out);

// Installs UltraAI in front of the recognizer UltraCanvasFileLoader has now
// (GetImageTextRecognizer), which stays as the fallback. Call once, at
// start-up, after the framework's own (RegisterOCRTextRecognition). The
// recognizer runs on the thread that calls ExtractText.
void RegisterUltraAITextRecognition();

// The provider that read the last picture on this thread ("" when it was the
// fallback): for a status line that says which engine read the text.
std::string LastTextRecognitionProvider();

} // namespace UltraAI
