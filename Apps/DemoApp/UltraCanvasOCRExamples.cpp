// Apps/DemoApp/UltraCanvasOCRExamples.cpp
// Demo screen for the UltraCanvas OCR plugin (Tesseract-backed).
// Loads OCR-demo.png by default and lets the user open an alternative
// image via the native file dialog. Recognised text is shown in a
// read-only text area.
// Version: 0.3.0 - the shared page look (UltraCanvasDemoPageKit): the two
//                  cards in a wrapping row, labels and buttons sized to their text
// Version: 0.2.0 - Open offers FileLoader's picture types
// Version: 0.1.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"

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
    auto container = DemoKit::CreatePage("OCRDemoPage",
        "Optical Character Recognition (Tesseract backend)",
        "Load an image, pick a language (any Tesseract language — "
        "packs download on first use), then press \"Run OCR\".");

    // The image and its controls beside the result; the result card moves
    // under the image card when the window is narrow.
    auto cardsRow = DemoKit::AddRow(container, "OCRCardsRow");

    // ===== LEFT COLUMN: image preview + actions =====
    // The frame keeps its size: the image inside fits itself to it.
    const int frameW = 348, frameH = 420;
    auto imageCard = DemoKit::AddCard(cardsRow, "OCRImageCard", "Input image", "",
                                      frameW + 2 * DemoKit::kCardPadding);

    auto imageFrame = std::make_shared<UltraCanvasContainer>(
        "OCRImageFrame", 0, 0, frameW, frameH);
    imageFrame->SetBackgroundColor(Color(241, 245, 249, 255));
    imageFrame->SetBorders(1, Color(203, 213, 225, 255));
    DemoKit::AddFixed(imageCard, imageFrame);

    auto image = std::make_shared<UltraCanvasImageElement>(
        "OCRImage", 4, 4, frameW - 8, frameH - 8);
    const std::string defaultPath =
        NormalizePath(GetResourcesDir() + kDefaultOCRImage);
    image->LoadFromFile(defaultPath);
    image->SetFitMode(ImageFitMode::Contain);
    imageFrame->AddChild(image);

    // ===== Language selector =====
    // The full Tesseract catalogue is offered; languages whose traineddata is
    // not installed yet are marked and fetched on demand when OCR is run.
    auto langRow = DemoKit::AddRow(imageCard, "OCRLangRow", 8);
    langRow->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
    auto langLabel = DemoKit::AddText(langRow, "OCRLangLabel", "Language:");
    langLabel->SetFontWeight(FontWeight::Bold);
    langLabel->SetTextColor(DemoKit::TitleText());

    auto langDropdown = std::make_shared<UltraCanvasDropdown>(
        "OCRLangDropdown", 0, 0, 220, 28);
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
            if (!have) label += "  (download)"; // not installed: picking it downloads the model
            langDropdown->AddItem(label, lang.code);
            if (lang.code == "eng") selectIndex = idx;
            ++idx;
        }
        langDropdown->SetSelectedIndex(selectIndex, false);
    }
    DemoKit::Add(langRow, langDropdown, 1);

    auto selectedLang = std::make_shared<std::string>("eng");
    langDropdown->onSelectionChanged =
        [selectedLang](int, const DropdownItem& item) {
            *selectedLang = item.value;
        };

    auto pathLabel = DemoKit::AddCaption(imageCard, "OCRImagePath", defaultPath);

    // ===== RIGHT COLUMN: result text area =====
    auto resultCard = DemoKit::AddCard(cardsRow, "OCRResultCard", "Recognised text");

    // As wide as the card; the text scrolls inside its 586 px.
    auto resultArea = std::make_shared<UltraCanvasTextArea>(
        "OCRResultArea", 0, 0, 478, 586);
    resultArea->SetText("(no result yet — press \"Run OCR\")");
    resultArea->SetReadOnly(true);
    DemoKit::AddStretched(resultCard, resultArea);

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
    // Each as wide as its icon and text, at least the 160 x 36 it had.
    auto buttonRow = DemoKit::AddRow(imageCard, "OCRButtonRow");
    CSSLayout::BoxConstraints buttonLimits;
    buttonLimits.minWidth = CSSLayout::Dimension::Px(160);
    buttonLimits.minHeight = CSSLayout::Dimension::Px(36);

    auto openBtn = std::make_shared<UltraCanvasButton>("OCROpenBtn", "Open image");
    openBtn->boxConstraints = buttonLimits;
    SetFlatButtonIcon(*openBtn, "menu/folder.svg");
    openBtn->SetFontSize(12);
    openBtn->SetColors(Color(241, 245, 249, 255), Color(226, 232, 240, 255));
    openBtn->SetTextColors(Color(30, 41, 59, 255));
    openBtn->SetCornerRadius(6);
    UltraCanvasWindowBase* parentWin = container->GetWindow();
    openBtn->onClick = [parentWin, image, pathLabel, currentPath]() {
        FileDialogOptions opts;
        opts.title = "Choose an image to OCR";
        opts.parentWindow = parentWin;
        UltraCanvasFileLoader::AddLoadTypes(opts, "Images", {MediaFormatCategory::Bitmap});
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
    DemoKit::Add(buttonRow, openBtn);

    auto runBtn = std::make_shared<UltraCanvasButton>("OCRRunBtn", "Run OCR");
    runBtn->boxConstraints = buttonLimits;
    SetFlatButtonIcon(*runBtn, "menu/search.svg");
    runBtn->SetFontSize(12);
    runBtn->SetColors(Color(37, 99, 235, 255), Color(29, 78, 216, 255));
    runBtn->SetTextColors(Color(255, 255, 255, 255));
    runBtn->SetCornerRadius(6);
    runBtn->onClick = runOCR;
    DemoKit::Add(buttonRow, runBtn);

    return container;
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_HAS_OCR_PLUGIN
