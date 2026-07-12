// Apps/DemoApp/UltraCanvasOCRExamples.cpp
// Demo screen for the UltraCanvas OCR plugin (Tesseract-backed).
// Loads OCR-demo.png by default and lets the user open an alternative
// image via the native file dialog. Recognised text is shown in a
// read-only text area.
// Version: 0.1.0
// Last Modified: 2026-05-21
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"

#ifdef ULTRACANVAS_HAS_OCR_PLUGIN

#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasOCRPlugin.h"

#include <memory>
#include <string>

namespace UltraCanvas {

namespace {

constexpr const char* kDefaultOCRImage = "media/images/OCR-demo.png";

std::string BuildResultHeader(const OCRResult& r, const std::string& source) {
    std::string header;
    header += "Source: " + source + "\n";
    header += "Engine: ";
    header += UltraCanvasOCR::EngineKindName(r.engineUsed);
    header += "\n";
    header += "Words:  " + std::to_string(r.words.size())
            + "   Lines: " + std::to_string(r.lines.size())
            + "   Time: " + std::to_string(static_cast<long>(r.elapsedMs)) + " ms\n";
    header += std::string(60, '-') + "\n\n";
    return header;
}

} // namespace

std::shared_ptr<UltraCanvasUIElement>
UltraCanvasDemoApplication::CreateOCRExamples() {
    // ===== ROOT CONTAINER (matches BitmapFormatDemoPage proportions) =====
    auto container = std::make_shared<UltraCanvasContainer>(
        "OCRDemoPage", 0, 0, 950, 750);
    container->SetBackgroundColor(Color(248, 250, 252, 255));

    // ===== HEADER =====
    auto header = std::make_shared<UltraCanvasLabel>(
        "OCRHeader", 20, 16, 910, 28);
    header->SetText("Optical Character Recognition (Tesseract backend)");
    header->SetFontSize(18);
    header->SetFontWeight(FontWeight::Bold);
    header->SetTextColor(Color(30, 41, 59, 255));
    container->AddChild(header);

    auto subhead = std::make_shared<UltraCanvasLabel>(
        "OCRSub", 20, 46, 910, 22);
    subhead->SetText("Load any image containing printed text, then press "
                     "\"Run OCR\" to extract it. Result appears on the right.");
    subhead->SetFontSize(12);
    subhead->SetTextColor(Color(100, 116, 139, 255));
    container->AddChild(subhead);

    // ===== LEFT COLUMN: image preview + actions =====
    const int leftX = 20, topY = 80, leftW = 380, leftH = 650;
    auto imageCard = std::make_shared<UltraCanvasContainer>(
        "OCRImageCard", leftX, topY, leftW, leftH);
    imageCard->SetBackgroundColor(Color(255, 255, 255, 255));
    imageCard->SetBorders(1, Color(226, 232, 240, 255));
    container->AddChild(imageCard);

    auto imageTitle = std::make_shared<UltraCanvasLabel>(
        "OCRImageTitle", 16, 12, leftW - 32, 22);
    imageTitle->SetText("Input image");
    imageTitle->SetFontSize(13);
    imageTitle->SetFontWeight(FontWeight::Bold);
    imageTitle->SetTextColor(Color(30, 41, 59, 255));
    imageCard->AddChild(imageTitle);

    auto imageFrame = std::make_shared<UltraCanvasContainer>(
        "OCRImageFrame", 16, 44, leftW - 32, leftH - 160);
    imageFrame->SetBackgroundColor(Color(241, 245, 249, 255));
    imageFrame->SetBorders(1, Color(203, 213, 225, 255));
    imageCard->AddChild(imageFrame);

    auto image = std::make_shared<UltraCanvasImageElement>(
        "OCRImage", 4, 4, leftW - 40, leftH - 168);
    const std::string defaultPath =
        NormalizePath(GetResourcesDir() + kDefaultOCRImage);
    image->LoadFromFile(defaultPath);
    image->SetFitMode(ImageFitMode::Contain);
    imageFrame->AddChild(image);

    auto pathLabel = std::make_shared<UltraCanvasLabel>(
        "OCRImagePath", 16, leftH - 108, leftW - 32, 20);
    pathLabel->SetText(defaultPath);
    pathLabel->SetFontSize(10);
    pathLabel->SetTextColor(Color(100, 116, 139, 255));
    imageCard->AddChild(pathLabel);

    // ===== RIGHT COLUMN: result text area =====
    const int rightX = 420, rightW = 510, rightH = 650;
    auto resultCard = std::make_shared<UltraCanvasContainer>(
        "OCRResultCard", rightX, topY, rightW, rightH);
    resultCard->SetBackgroundColor(Color(255, 255, 255, 255));
    resultCard->SetBorders(1, Color(226, 232, 240, 255));
    container->AddChild(resultCard);

    auto resultTitle = std::make_shared<UltraCanvasLabel>(
        "OCRResultTitle", 16, 12, rightW - 32, 22);
    resultTitle->SetText("Recognised text");
    resultTitle->SetFontSize(13);
    resultTitle->SetFontWeight(FontWeight::Bold);
    resultTitle->SetTextColor(Color(30, 41, 59, 255));
    resultCard->AddChild(resultTitle);

    auto resultArea = std::make_shared<UltraCanvasTextArea>(
        "OCRResultArea", 16, 44, rightW - 32, rightH - 64);
    resultArea->SetText("(no result yet — press \"Run OCR\")");
    resultArea->SetReadOnly(true);
    resultCard->AddChild(resultArea);

    // ===== Shared state for the action buttons =====
    auto currentPath = std::make_shared<std::string>(defaultPath);

    auto runOCR = [resultArea, currentPath]() {
        resultArea->SetText("Recognising… please wait.");
        OCRConfig cfg;
        cfg.engine    = OCREngineKind::Auto;
        cfg.languages = {"eng"};
        UltraCanvasOCR ocr(cfg);
        OCRResult r = ocr.RecognizeFile(*currentPath);
        if (!r.Ok()) {
            resultArea->SetText("OCR failed: " + r.error);
            return;
        }
        const std::string text = BuildResultHeader(r, *currentPath)
                               + (r.plainText.empty()
                                      ? std::string("(no text detected)")
                                      : r.plainText);
        resultArea->SetText(text);
    };

    // ===== BUTTONS =====
    const int btnY = leftH - 80;
    auto openBtn = std::make_shared<UltraCanvasButton>(
        "OCROpenBtn", 16, btnY, 160, 36);
    openBtn->SetText("📂 Open image…");
    openBtn->SetFontSize(12);
    openBtn->SetColors(Color(241, 245, 249, 255), Color(226, 232, 240, 255));
    openBtn->SetTextColors(Color(30, 41, 59, 255));
    openBtn->SetCornerRadius(6);
    UltraCanvasWindowBase* parentWin = container->GetWindow();
    openBtn->onClick = [parentWin, image, pathLabel, currentPath]() {
        FileDialogOptions opts;
        opts.title = "Choose an image to OCR";
        opts.parentWindow = parentWin;
        opts.AddFilter("Images", {"png", "jpg", "jpeg", "webp", "tiff", "bmp"});
        UltraCanvasFileLoader::OpenFileDialog(opts,
            [image, pathLabel, currentPath]
            (DialogResult r, const std::string& path) {
                if (r != DialogResult::OK || path.empty()) return;
                if (image->LoadFromFile(path)) {
                    *currentPath = path;
                    pathLabel->SetText(path);
                }
            });
    };
    imageCard->AddChild(openBtn);

    auto runBtn = std::make_shared<UltraCanvasButton>(
        "OCRRunBtn", 188, btnY, 160, 36);
    runBtn->SetText("🔍 Run OCR");
    runBtn->SetFontSize(12);
    runBtn->SetColors(Color(37, 99, 235, 255), Color(29, 78, 216, 255));
    runBtn->SetTextColors(Color(255, 255, 255, 255));
    runBtn->SetCornerRadius(6);
    runBtn->onClick = runOCR;
    imageCard->AddChild(runBtn);

    return container;
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_HAS_OCR_PLUGIN
