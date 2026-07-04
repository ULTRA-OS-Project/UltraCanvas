// Apps/DemoApp/UltraCanvasVectorizerExamples.cpp
// Demo screen for the UltraCanvas Vectorizer plugin (VTracer-backed).
// Loads Vectorizer-demo.png by default and lets the user open an
// alternative bitmap. The resulting SVG is written to a temp file and
// rendered through the existing image element (which understands SVG).
// Version: 0.1.0
// Last Modified: 2026-05-21
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"

#ifdef ULTRACANVAS_HAS_VECTORIZER_PLUGIN

#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasVectorizerPlugin.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace UltraCanvas {

namespace {

constexpr const char* kDefaultVectorizerImage = "media/images/Vectorizer-demo.png";

std::string TempSvgPath() {
    namespace fs = std::filesystem;
    fs::path p = fs::temp_directory_path() / "ultracanvas-vectorizer-output.svg";
    return p.string();
}

bool WriteFile(const std::string& path, const std::string& bytes) {
    std::ofstream os(path, std::ios::binary | std::ios::trunc);
    if (!os) return false;
    os.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return os.good();
}

} // namespace

std::shared_ptr<UltraCanvasUIElement>
UltraCanvasDemoApplication::CreateVectorizerExamples() {
    auto container = std::make_shared<UltraCanvasContainer>(
        "VectorizerDemoPage", 0, 0, 950, 750);
    container->SetBackgroundColor(Color(248, 250, 252, 255));

    // ===== HEADER =====
    auto header = std::make_shared<UltraCanvasLabel>(
        "VecHeader", 20, 16, 910, 28);
    header->SetText("Raster → SVG Vectorizer (VTracer backend)");
    header->SetFontSize(18);
    header->SetFontWeight(FontWeight::Bold);
    header->SetTextColor(Color(30, 41, 59, 255));
    container->AddChild(header);

    auto subhead = std::make_shared<UltraCanvasLabel>(
        "VecSub", 20, 46, 910, 22);
    subhead->SetText("Load any raster image, press \"Vectorize\", and "
                     "the SVG output is rendered on the right.");
    subhead->SetFontSize(12);
    subhead->SetTextColor(Color(100, 116, 139, 255));
    container->AddChild(subhead);

    // ===== LAYOUT =====
    const int topY = 80, colH = 650;
    const int leftX = 20, leftW = 440;
    const int rightX = 480, rightW = 450;

    // ===== LEFT: input image + actions =====
    auto inputCard = std::make_shared<UltraCanvasContainer>(
        "VecInputCard", leftX, topY, leftW, colH);
    inputCard->SetBackgroundColor(Color(255, 255, 255, 255));
    inputCard->SetBorders(1, Color(226, 232, 240, 255));
    container->AddChild(inputCard);

    auto inputTitle = std::make_shared<UltraCanvasLabel>(
        "VecInputTitle", 16, 12, leftW - 32, 22);
    inputTitle->SetText("Input bitmap");
    inputTitle->SetFontSize(13);
    inputTitle->SetFontWeight(FontWeight::Bold);
    inputTitle->SetTextColor(Color(30, 41, 59, 255));
    inputCard->AddChild(inputTitle);

    auto inputFrame = std::make_shared<UltraCanvasContainer>(
        "VecInputFrame", 16, 44, leftW - 32, colH - 160);
    inputFrame->SetBackgroundColor(Color(241, 245, 249, 255));
    inputFrame->SetBorders(1, Color(203, 213, 225, 255));
    inputCard->AddChild(inputFrame);

    auto inputImage = std::make_shared<UltraCanvasImageElement>(
        "VecInputImage", 4, 4, leftW - 40, colH - 168);
    const std::string defaultPath =
        NormalizePath(GetResourcesDir() + kDefaultVectorizerImage);
    inputImage->LoadFromFile(defaultPath);
    inputImage->SetFitMode(ImageFitMode::Contain);
    inputFrame->AddChild(inputImage);

    auto inputPathLabel = std::make_shared<UltraCanvasLabel>(
        "VecInputPath", 16, colH - 108, leftW - 32, 20);
    inputPathLabel->SetText(defaultPath);
    inputPathLabel->SetFontSize(10);
    inputPathLabel->SetTextColor(Color(100, 116, 139, 255));
    inputCard->AddChild(inputPathLabel);

    // ===== RIGHT: SVG output + status =====
    auto outputCard = std::make_shared<UltraCanvasContainer>(
        "VecOutputCard", rightX, topY, rightW, colH);
    outputCard->SetBackgroundColor(Color(255, 255, 255, 255));
    outputCard->SetBorders(1, Color(226, 232, 240, 255));
    container->AddChild(outputCard);

    auto outputTitle = std::make_shared<UltraCanvasLabel>(
        "VecOutputTitle", 16, 12, rightW - 32, 22);
    outputTitle->SetText("SVG output");
    outputTitle->SetFontSize(13);
    outputTitle->SetFontWeight(FontWeight::Bold);
    outputTitle->SetTextColor(Color(30, 41, 59, 255));
    outputCard->AddChild(outputTitle);

    auto outputFrame = std::make_shared<UltraCanvasContainer>(
        "VecOutputFrame", 16, 44, rightW - 32, colH - 100);
    outputFrame->SetBackgroundColor(Color(250, 250, 246, 255));
    outputFrame->SetBorders(1, Color(203, 213, 225, 255));
    outputCard->AddChild(outputFrame);

    auto outputImage = std::make_shared<UltraCanvasImageElement>(
        "VecOutputImage", 4, 4, rightW - 40, colH - 108);
    outputImage->SetFitMode(ImageFitMode::Contain);
    outputFrame->AddChild(outputImage);

    auto statusLabel = std::make_shared<UltraCanvasLabel>(
        "VecStatus", 16, colH - 48, rightW - 32, 20);
    statusLabel->SetText("(press \"Vectorize\" to trace the input)");
    statusLabel->SetFontSize(10);
    statusLabel->SetTextColor(Color(100, 116, 139, 255));
    outputCard->AddChild(statusLabel);

    // ===== Shared state =====
    auto currentPath = std::make_shared<std::string>(defaultPath);

    auto runVectorize = [outputImage, statusLabel, currentPath]() {
        statusLabel->SetText("Tracing…");
        VectorizerConfig cfg;     // colour, spline, sensible defaults
        UltraCanvasVectorizer vec(cfg);
        VectorizerResult r = vec.VectorizeFile(*currentPath);
        if (!r.Ok()) {
            statusLabel->SetText("Vectorize failed: " + r.error);
            return;
        }
        const std::string outPath = TempSvgPath();
        if (!WriteFile(outPath, r.svg)) {
            statusLabel->SetText("Vectorize succeeded but failed to "
                                 "write SVG to " + outPath);
            return;
        }
        if (!outputImage->LoadFromFile(outPath)) {
            statusLabel->SetText("SVG written to " + outPath
                               + " but image element rejected it");
            return;
        }
        const std::string status =
            "OK · " + std::to_string(r.width) + "×" + std::to_string(r.height)
          + " · " + std::to_string(r.svg.size()) + " bytes SVG · "
          + std::to_string(static_cast<long>(r.elapsedMs)) + " ms";
        statusLabel->SetText(status);
    };

    // ===== BUTTONS =====
    const int btnY = colH - 80;
    auto openBtn = std::make_shared<UltraCanvasButton>(
        "VecOpenBtn", 16, btnY, 180, 36);
    openBtn->SetText("📂 Open bitmap…");
    openBtn->SetFontSize(12);
    openBtn->SetColors(Color(241, 245, 249, 255), Color(226, 232, 240, 255));
    openBtn->SetTextColors(Color(30, 41, 59, 255));
    openBtn->SetCornerRadius(6);
    UltraCanvasWindowBase* parentWin = container->GetWindow();
    openBtn->onClick = [parentWin, inputImage, inputPathLabel, currentPath]() {
        FileDialogOptions opts;
        opts.title = "Choose a raster image to vectorize";
        opts.parentWindow = parentWin;
        opts.AddFilter("Images", {"png", "jpg", "jpeg", "webp", "tiff", "bmp", "gif"});
        UltraCanvasFileLoader::OpenFileDialog(opts,
            [inputImage, inputPathLabel, currentPath]
            (DialogResult r, const std::string& path) {
                if (r != DialogResult::OK || path.empty()) return;
                if (inputImage->LoadFromFile(path)) {
                    *currentPath = path;
                    inputPathLabel->SetText(path);
                }
            });
    };
    inputCard->AddChild(openBtn);

    auto runBtn = std::make_shared<UltraCanvasButton>(
        "VecRunBtn", 208, btnY, 180, 36);
    runBtn->SetText("✨ Vectorize");
    runBtn->SetFontSize(12);
    runBtn->SetColors(Color(168, 85, 247, 255), Color(147, 51, 234, 255));
    runBtn->SetTextColors(Color(255, 255, 255, 255));
    runBtn->SetCornerRadius(6);
    runBtn->onClick = runVectorize;
    inputCard->AddChild(runBtn);

    return container;
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_HAS_VECTORIZER_PLUGIN
