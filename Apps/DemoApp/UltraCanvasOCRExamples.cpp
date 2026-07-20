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
#include "UltraCanvasDropdown.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasOCRPlugin.h"

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_set>

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
    subhead->SetText("Load an image, pick a language (any Tesseract language — "
                     "packs download on first use), then press \"Run OCR\".");
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
        "OCRImageFrame", 16, 44, leftW - 32, leftH - 230);
    imageFrame->SetBackgroundColor(Color(241, 245, 249, 255));
    imageFrame->SetBorders(1, Color(203, 213, 225, 255));
    imageCard->AddChild(imageFrame);

    auto image = std::make_shared<UltraCanvasImageElement>(
        "OCRImage", 4, 4, leftW - 40, leftH - 238);
    const std::string defaultPath =
        NormalizePath(GetResourcesDir() + kDefaultOCRImage);
    image->LoadFromFile(defaultPath);
    image->SetFitMode(ImageFitMode::Contain);
    imageFrame->AddChild(image);

    // ===== Language selector =====
    // The full Tesseract catalogue is offered; languages whose traineddata is
    // not installed yet are marked and fetched on demand when OCR is run.
    auto langLabel = std::make_shared<UltraCanvasLabel>(
        "OCRLangLabel", 16, leftH - 176, 90, 24);
    langLabel->SetText("Language:");
    langLabel->SetFontSize(12);
    langLabel->SetFontWeight(FontWeight::Bold);
    langLabel->SetTextColor(Color(30, 41, 59, 255));
    imageCard->AddChild(langLabel);

    auto langDropdown = std::make_shared<UltraCanvasDropdown>(
        "OCRLangDropdown", 110, leftH - 178, leftW - 126, 28);
    {
        std::vector<std::string> installedVec =
            UltraCanvasOCR::InstalledLanguages();
        std::unordered_set<std::string> installed(installedVec.begin(),
                                                  installedVec.end());
        int selectIndex = 0, idx = 0;
        for (const OCRLanguageInfo& lang : UltraCanvasOCR::SupportedLanguages()) {
            if (lang.isScript) continue; // hide osd/equ helper models
            const bool have = installed.count(lang.code) > 0;
            std::string label = lang.englishName + " (" + lang.code + ")";
            if (!have) label += "  \xE2\xAC\x87"; // ⬇ = not installed, will download
            langDropdown->AddItem(label, lang.code);
            if (lang.code == "eng") selectIndex = idx;
            ++idx;
        }
        langDropdown->SetSelectedIndex(selectIndex, false);
    }
    imageCard->AddChild(langDropdown);

    auto selectedLang = std::make_shared<std::string>("eng");
    langDropdown->onSelectionChanged =
        [selectedLang](int, const DropdownItem& item) {
            *selectedLang = item.value;
        };

    auto pathLabel = std::make_shared<UltraCanvasLabel>(
        "OCRImagePath", 16, leftH - 140, leftW - 32, 20);
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

    auto runOCR = [resultArea, currentPath, selectedLang]() {
        const std::string lang = selectedLang->empty() ? "eng" : *selectedLang;
        const bool needsFetch = !UltraCanvasOCR::IsLanguageInstalled(lang);
        resultArea->SetText(needsFetch
            ? "Fetching language pack '" + lang + "' and recognising…"
            : "Recognising… please wait.");

        OCRConfig cfg;
        cfg.engine = OCREngineKind::Auto;
        UltraCanvasOCR ocr(cfg);

        // Provision the chosen language (downloads its traineddata the first
        // time it is used), then run recognition.
        std::string err;
        if (!ocr.EnsureLanguages({lang}, err)) {
            resultArea->SetText("Could not prepare language '" + lang +
                                "':\n" + err);
            return;
        }

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
