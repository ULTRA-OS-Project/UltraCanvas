// Apps/DemoApp/UltraCanvasVectorizerExamples.cpp
// Demo screen for the UltraCanvas Vectorizer plugin (VTracer-backed).
// Loads Vectorizer-demo.png by default and lets the user open an
// alternative bitmap. Every tracing-quality parameter of VectorizerConfig
// is exposed in a middle "Tracing parameters" column (quality presets plus
// per-parameter dropdowns and sliders); the resulting SVG is written to a
// temp file, rendered through the existing image element (which understands
// SVG) and can be saved anywhere via the native save dialog.
// Version: 0.2.0
// Last Modified: 2026-08-04
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"

#ifdef ULTRACANVAS_HAS_VECTORIZER_PLUGIN

#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasVectorizerPlugin.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

namespace {

constexpr const char* kDefaultVectorizerImage = "media/images/Vectorizer-demo.png";

// ===== PALETTE (shared with the OCR demo screen) =====
const Color kPageBg      (248, 250, 252, 255);
const Color kCardBg      (255, 255, 255, 255);
const Color kCardBorder  (226, 232, 240, 255);
const Color kFrameBg     (241, 245, 249, 255);
const Color kOutputBg    (250, 250, 246, 255);
const Color kFrameBorder (203, 213, 225, 255);
const Color kTextStrong  ( 30,  41,  59, 255);
const Color kTextMuted   (100, 116, 139, 255);
const Color kTextFaded   (148, 163, 184, 255);
const Color kNeutralBtn  (241, 245, 249, 255);
const Color kNeutralBtnH (226, 232, 240, 255);
const Color kRunBtn      (168,  85, 247, 255);
const Color kRunBtnH     (147,  51, 234, 255);
const Color kSaveBtn     ( 16, 185, 129, 255);
const Color kSaveBtnH    (  5, 150, 105, 255);

// ===== FILE HELPERS =====
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

std::string BaseName(const std::string& path) {
    const size_t sep = path.find_last_of("/\\");
    return sep == std::string::npos ? path : path.substr(sep + 1);
}

std::string ParentDir(const std::string& path) {
    const size_t sep = path.find_last_of("/\\");
    return sep == std::string::npos ? std::string() : path.substr(0, sep);
}

// "logo.png" -> "logo.svg"; a name without an extension simply gains one.
std::string SuggestedSvgName(const std::string& sourcePath) {
    std::string name = BaseName(sourcePath);
    const size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
    if (name.empty()) name = "vectorized";
    return name + ".svg";
}

// The native dialogs do not append the filter extension on every platform,
// so make sure whatever the user typed ends up as an .svg document.
std::string EnsureSvgExtension(const std::string& path) {
    const std::string name = BaseName(path);
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0) return path + ".svg";
    std::string ext = name.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".svg" ? path : path + ".svg";
}

// ===== TEXT HELPERS =====
std::string FormatNumber(float value, int decimals) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, static_cast<double>(value));
    return std::string(buf);
}

std::string FormatBytes(size_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " bytes";
    if (bytes < 1024 * 1024) return FormatNumber(bytes / 1024.0f, 1) + " KB";
    return FormatNumber(bytes / (1024.0f * 1024.0f), 1) + " MB";
}

// ===== QUALITY PRESETS =====
// Each preset is a complete VectorizerConfig; selecting one pushes every
// value into the controls below it. Editing any single control afterwards
// flips the preset dropdown to "Custom".
struct QualityPreset {
    std::string      label;
    std::string      help;
    VectorizerConfig cfg;
};

VectorizerConfig MakeConfig(VectorizerColorMode colorMode,
                            VectorizerHierarchy hierarchy,
                            VectorizerPathMode  pathMode,
                            int speckle, int precision, int layerDiff,
                            int corner, double length, int splice, int iterations) {
    VectorizerConfig cfg;
    cfg.colorMode       = colorMode;
    cfg.hierarchy       = hierarchy;
    cfg.pathMode        = pathMode;
    cfg.filterSpeckle   = speckle;
    cfg.colorPrecision  = precision;
    cfg.layerDifference = layerDiff;
    cfg.cornerThreshold = corner;
    cfg.lengthThreshold = length;
    cfg.spliceThreshold = splice;
    cfg.maxIterations   = iterations;
    return cfg;
}

const std::vector<QualityPreset>& Presets() {
    static const std::vector<QualityPreset> presets = {
        { "Logo / icon (default)",
          "Balanced colour tracing for logos, icons and flat artwork — the framework defaults.",
          MakeConfig(VectorizerColorMode::Color, VectorizerHierarchy::Stacked,
                     VectorizerPathMode::Spline, 4, 6, 16, 60, 4.0, 45, 10) },

        { "Detailed illustration",
          "Keeps small shapes and subtle colour steps. Slower to trace and a noticeably larger SVG.",
          MakeConfig(VectorizerColorMode::Color, VectorizerHierarchy::Stacked,
                     VectorizerPathMode::Spline, 2, 8, 8, 45, 3.0, 45, 14) },

        { "Photograph (compact)",
          "Strong speckle and colour reduction so a photograph still produces a manageable SVG.",
          MakeConfig(VectorizerColorMode::Color, VectorizerHierarchy::Stacked,
                     VectorizerPathMode::Spline, 16, 4, 48, 80, 6.0, 60, 8) },

        { "Poster / flat cutout",
          "Non-overlapping polygon shapes — suits screen printing and cutting-plotter output.",
          MakeConfig(VectorizerColorMode::Color, VectorizerHierarchy::Cutout,
                     VectorizerPathMode::Polygon, 8, 5, 32, 60, 5.0, 45, 10) },

        { "Line art (black & white)",
          "Single-colour silhouette tracing for sketches, scans and stencils.",
          MakeConfig(VectorizerColorMode::Binary, VectorizerHierarchy::Stacked,
                     VectorizerPathMode::Spline, 6, 8, 16, 60, 4.0, 45, 10) },

        { "Pixel perfect (no fitting)",
          "One path per pixel run: an exact reproduction with a very large SVG. Use for pixel art.",
          MakeConfig(VectorizerColorMode::Color, VectorizerHierarchy::Stacked,
                     VectorizerPathMode::Pixel, 0, 8, 0, 60, 4.0, 45, 10) },

        { "Maximum detail (slow)",
          "Every cluster preserved with tight curve fitting — the slowest and largest output.",
          MakeConfig(VectorizerColorMode::Color, VectorizerHierarchy::Stacked,
                     VectorizerPathMode::Spline, 0, 8, 4, 30, 2.0, 30, 20) },
    };
    return presets;
}

constexpr const char* kCustomPresetLabel =
    "Custom";
constexpr const char* kCustomPresetHelp =
    "Custom parameters. Pick a preset above to return to a known-good starting point.";

bool SameConfig(const VectorizerConfig& a, const VectorizerConfig& b) {
    return a.colorMode       == b.colorMode
        && a.hierarchy       == b.hierarchy
        && a.pathMode        == b.pathMode
        && a.filterSpeckle   == b.filterSpeckle
        && a.colorPrecision  == b.colorPrecision
        && a.layerDifference == b.layerDifference
        && a.cornerThreshold == b.cornerThreshold
        && a.spliceThreshold == b.spliceThreshold
        && a.maxIterations   == b.maxIterations
        && std::fabs(a.lengthThreshold - b.lengthThreshold) < 0.01;
}

// ===== ENUM <-> DROPDOWN INDEX =====
VectorizerColorMode ColorModeFromIndex(int i) {
    return i == 1 ? VectorizerColorMode::Binary : VectorizerColorMode::Color;
}
int IndexOfColorMode(VectorizerColorMode m) {
    return m == VectorizerColorMode::Binary ? 1 : 0;
}

VectorizerHierarchy HierarchyFromIndex(int i) {
    return i == 1 ? VectorizerHierarchy::Cutout : VectorizerHierarchy::Stacked;
}
int IndexOfHierarchy(VectorizerHierarchy h) {
    return h == VectorizerHierarchy::Cutout ? 1 : 0;
}

VectorizerPathMode PathModeFromIndex(int i) {
    if (i == 1) return VectorizerPathMode::Polygon;
    if (i == 2) return VectorizerPathMode::Pixel;
    return VectorizerPathMode::Spline;
}
int IndexOfPathMode(VectorizerPathMode p) {
    if (p == VectorizerPathMode::Polygon) return 1;
    if (p == VectorizerPathMode::Pixel)   return 2;
    return 0;
}

// ===== SLIDER BINDING =====
// One entry per numeric VectorizerConfig field. `applies` reports whether the
// parameter has any effect in the currently selected colour / path mode, so
// irrelevant controls can be greyed out instead of quietly doing nothing.
// The widgets are referenced by raw pointer: the element tree owns them, and
// these bindings are captured by callbacks that live inside that same tree —
// owning pointers here would form a reference cycle and leak the page.
struct ParamSlider {
    UltraCanvasSlider* slider  = nullptr;
    UltraCanvasLabel*  caption = nullptr;
    std::string title;
    std::string unit;
    int         decimals = 0;
    std::function<float(const VectorizerConfig&)> get;
    std::function<void(VectorizerConfig&, float)> set;
    std::function<bool(const VectorizerConfig&)>  applies;
};

std::string CaptionText(const std::string& title, float value,
                        int decimals, const std::string& unit) {
    return title + ": " + FormatNumber(value, decimals) + unit;
}

} // namespace

std::shared_ptr<UltraCanvasUIElement>
UltraCanvasDemoApplication::CreateVectorizerExamples() {
    // ===== PAGE GEOMETRY =====
    const int pageW = 1020, pageH = 750;
    const int topY = 74, colH = 660;
    const int inputX  = 16,  inputW  = 300;   // source bitmap
    const int paramX  = 328, paramW  = 360;   // tracing parameters
    const int outputX = 700, outputW = 304;   // resulting SVG
    const int pad = 14;

    auto container = std::make_shared<UltraCanvasContainer>(
        "VectorizerDemoPage", 0, 0, pageW, pageH);
    container->SetBackgroundColor(kPageBg);
    UltraCanvasContainer* pageRaw = container.get();

    // ===== HEADER =====
    auto header = std::make_shared<UltraCanvasLabel>(
        "VecHeader", 16, 14, pageW - 32, 26);
    header->SetText("Raster → SVG Vectorizer (VTracer backend)");
    header->SetFontSize(18);
    header->SetFontWeight(FontWeight::Bold);
    header->SetTextColor(kTextStrong);
    container->AddChild(header);

    auto subhead = std::make_shared<UltraCanvasLabel>(
        "VecSub", 16, 44, pageW - 32, 20);
    subhead->SetText(std::string("Load a bitmap, tune the tracing quality, press "
                                 "\"Vectorize\", then save the SVG.  Backend: ")
                     + UltraCanvasVectorizer::BackendVersion());
    subhead->SetFontSize(12);
    subhead->SetTextColor(kTextMuted);
    container->AddChild(subhead);

    // ===== LEFT: input image + actions =====
    auto inputCard = std::make_shared<UltraCanvasContainer>(
        "VecInputCard", inputX, topY, inputW, colH);
    inputCard->SetBackgroundColor(kCardBg);
    inputCard->SetBorders(1, kCardBorder);
    container->AddChild(inputCard);

    const int inputInnerW = inputW - 2 * pad;

    auto inputTitle = std::make_shared<UltraCanvasLabel>(
        "VecInputTitle", pad, 12, inputInnerW, 20);
    inputTitle->SetText("Input bitmap");
    inputTitle->SetFontSize(13);
    inputTitle->SetFontWeight(FontWeight::Bold);
    inputTitle->SetTextColor(kTextStrong);
    inputCard->AddChild(inputTitle);

    auto inputFrame = std::make_shared<UltraCanvasContainer>(
        "VecInputFrame", pad, 40, inputInnerW, 494);
    inputFrame->SetBackgroundColor(kFrameBg);
    inputFrame->SetBorders(1, kFrameBorder);
    inputCard->AddChild(inputFrame);

    auto inputImage = std::make_shared<UltraCanvasImageElement>(
        "VecInputImage", 4, 4, inputInnerW - 8, 486);
    const std::string defaultPath =
        NormalizePath(GetResourcesDir() + kDefaultVectorizerImage);
    inputImage->LoadFromFile(defaultPath);
    inputImage->SetFitMode(ImageFitMode::Contain);
    inputFrame->AddChild(inputImage);

    auto inputPathLabel = std::make_shared<UltraCanvasLabel>(
        "VecInputPath", pad, 542, inputInnerW, 16);
    inputPathLabel->SetText(BaseName(defaultPath));
    inputPathLabel->SetTooltip(defaultPath);
    inputPathLabel->SetFontSize(10);
    inputPathLabel->SetTextColor(kTextMuted);
    inputCard->AddChild(inputPathLabel);

    auto inputSizeLabel = std::make_shared<UltraCanvasLabel>(
        "VecInputSize", pad, 560, inputInnerW, 16);
    inputSizeLabel->SetFontSize(10);
    inputSizeLabel->SetTextColor(kTextFaded);
    inputCard->AddChild(inputSizeLabel);

    // Reports the pixel dimensions of whatever is currently loaded — the
    // single biggest driver of both trace time and SVG size.
    auto describeInput = [inputImage, inputSizeLabel]() {
        const Point2Di size = inputImage->GetImageSize();
        if (size.x > 0 && size.y > 0) {
            inputSizeLabel->SetText(std::to_string(size.x) + " × "
                                  + std::to_string(size.y) + " pixels");
        } else {
            inputSizeLabel->SetText("(no bitmap loaded)");
        }
    };
    describeInput();

    // ===== MIDDLE: tracing parameters =====
    auto paramCard = std::make_shared<UltraCanvasContainer>(
        "VecParamCard", paramX, topY, paramW, colH);
    paramCard->SetBackgroundColor(kCardBg);
    paramCard->SetBorders(1, kCardBorder);
    container->AddChild(paramCard);

    const int paramInnerW = paramW - 2 * pad;
    const int ddX = pad + 118, ddW = paramInnerW - 118;

    auto paramTitle = std::make_shared<UltraCanvasLabel>(
        "VecParamTitle", pad, 12, paramInnerW, 20);
    paramTitle->SetText("Tracing parameters");
    paramTitle->SetFontSize(13);
    paramTitle->SetFontWeight(FontWeight::Bold);
    paramTitle->SetTextColor(kTextStrong);
    paramCard->AddChild(paramTitle);

    // Shared configuration edited by every control on this card.
    auto cfg     = std::make_shared<VectorizerConfig>(Presets().front().cfg);
    auto syncing = std::make_shared<bool>(false);

    // Builds "caption + dropdown" on one row and returns the dropdown.
    auto addDropdownRow = [&](const std::string& id, const std::string& caption,
                              const std::vector<std::string>& items,
                              int selected, int y) {
        auto lbl = std::make_shared<UltraCanvasLabel>(
            "VecLbl" + id, pad, y + 4, 112, 18);
        lbl->SetText(caption);
        lbl->SetFontSize(11);
        lbl->SetTextColor(kTextStrong);
        paramCard->AddChild(lbl);

        auto dd = std::make_shared<UltraCanvasDropdown>(
            "VecDD" + id, ddX, y, ddW, 26);
        for (const std::string& item : items) dd->AddItem(item);
        dd->SetSelectedIndex(selected, false);
        paramCard->AddChild(dd);
        return dd;
    };

    std::vector<std::string> presetItems;
    for (const QualityPreset& p : Presets()) presetItems.push_back(p.label);
    presetItems.push_back(kCustomPresetLabel);

    auto presetDD = addDropdownRow("Preset", "Quality preset", presetItems, 0, 42);
    auto colorDD  = addDropdownRow("Color", "Colour mode",
                                   {"Colour (clustered)", "Binary (black & white)"},
                                   IndexOfColorMode(cfg->colorMode), 78);
    auto hierDD   = addDropdownRow("Hier", "Shape hierarchy",
                                   {"Stacked (overlapping)", "Cutout (no overlap)"},
                                   IndexOfHierarchy(cfg->hierarchy), 114);
    auto pathDD   = addDropdownRow("Path", "Path mode",
                                   {"Spline (curves)", "Polygon (straight)", "Pixel (exact)"},
                                   IndexOfPathMode(cfg->pathMode), 150);

    auto presetHelp = std::make_shared<UltraCanvasLabel>(
        "VecPresetHelp", pad, 182, paramInnerW, 32);
    presetHelp->SetText(Presets().front().help);
    presetHelp->SetFontSize(10);
    presetHelp->SetWrap(TextWrap::WrapWord);
    presetHelp->SetTextColor(kTextMuted);
    paramCard->AddChild(presetHelp);

    // ===== Numeric parameters =====
    auto sliders = std::make_shared<std::vector<ParamSlider>>();

    auto addSlider = [&](const std::string& id, const std::string& title,
                         const std::string& unit, int decimals,
                         float minValue, float maxValue, float step, int y,
                         std::function<float(const VectorizerConfig&)> get,
                         std::function<void(VectorizerConfig&, float)> set,
                         std::function<bool(const VectorizerConfig&)> applies) {
        auto caption = std::make_shared<UltraCanvasLabel>(
            "VecCap" + id, pad, y, paramInnerW, 16);
        caption->SetText(CaptionText(title, get(*cfg), decimals, unit));
        caption->SetFontSize(11);
        caption->SetTextColor(kTextStrong);
        paramCard->AddChild(caption);

        auto slider = std::make_shared<UltraCanvasSlider>(
            "VecSld" + id, pad, y + 18, paramInnerW, 20);
        slider->SetRange(minValue, maxValue);
        slider->SetStep(step);
        slider->SetValue(get(*cfg));
        paramCard->AddChild(slider);

        sliders->push_back(ParamSlider{slider.get(), caption.get(), title, unit,
                                       decimals, std::move(get), std::move(set),
                                       std::move(applies)});
    };

    const auto alwaysApplies = [](const VectorizerConfig&) { return true; };
    const auto colourOnly    = [](const VectorizerConfig& c) {
        return c.colorMode == VectorizerColorMode::Color;
    };
    const auto curveOrPolygon = [](const VectorizerConfig& c) {
        return c.pathMode != VectorizerPathMode::Pixel;
    };
    const auto splineOnly = [](const VectorizerConfig& c) {
        return c.pathMode == VectorizerPathMode::Spline;
    };

    int sliderY = 220;
    const int sliderPitch = 42;

    addSlider("Speckle", "Speckle filter", " px", 0, 0, 64, 1, sliderY,
              [](const VectorizerConfig& c) { return static_cast<float>(c.filterSpeckle); },
              [](VectorizerConfig& c, float v) { c.filterSpeckle = static_cast<int>(v); },
              alwaysApplies);
    sliderY += sliderPitch;

    addSlider("Precision", "Colour precision", " bits", 0, 1, 8, 1, sliderY,
              [](const VectorizerConfig& c) { return static_cast<float>(c.colorPrecision); },
              [](VectorizerConfig& c, float v) { c.colorPrecision = static_cast<int>(v); },
              colourOnly);
    sliderY += sliderPitch;

    addSlider("Layer", "Layer difference", "", 0, 0, 255, 1, sliderY,
              [](const VectorizerConfig& c) { return static_cast<float>(c.layerDifference); },
              [](VectorizerConfig& c, float v) { c.layerDifference = static_cast<int>(v); },
              colourOnly);
    sliderY += sliderPitch;

    addSlider("Corner", "Corner threshold", "°", 0, 0, 180, 1, sliderY,
              [](const VectorizerConfig& c) { return static_cast<float>(c.cornerThreshold); },
              [](VectorizerConfig& c, float v) { c.cornerThreshold = static_cast<int>(v); },
              curveOrPolygon);
    sliderY += sliderPitch;

    addSlider("Length", "Path length threshold", " px", 1, 0.5f, 10.0f, 0.5f, sliderY,
              [](const VectorizerConfig& c) { return static_cast<float>(c.lengthThreshold); },
              [](VectorizerConfig& c, float v) { c.lengthThreshold = static_cast<double>(v); },
              curveOrPolygon);
    sliderY += sliderPitch;

    addSlider("Splice", "Splice threshold", "°", 0, 0, 180, 1, sliderY,
              [](const VectorizerConfig& c) { return static_cast<float>(c.spliceThreshold); },
              [](VectorizerConfig& c, float v) { c.spliceThreshold = static_cast<int>(v); },
              splineOnly);
    sliderY += sliderPitch;

    addSlider("Iterations", "Curve-fitting iterations", "", 0, 1, 30, 1, sliderY,
              [](const VectorizerConfig& c) { return static_cast<float>(c.maxIterations); },
              [](VectorizerConfig& c, float v) { c.maxIterations = static_cast<int>(v); },
              splineOnly);

    auto resetBtn = std::make_shared<UltraCanvasButton>(
        "VecResetBtn", pad, 524, paramInnerW, 32);
    resetBtn->SetText("↺ Reset to defaults");
    resetBtn->SetFontSize(12);
    resetBtn->SetColors(kNeutralBtn, kNeutralBtnH);
    resetBtn->SetTextColors(kTextStrong);
    resetBtn->SetCornerRadius(6);
    paramCard->AddChild(resetBtn);

    auto paramNote = std::make_shared<UltraCanvasLabel>(
        "VecParamNote", pad, 566, paramInnerW, 32);
    paramNote->SetText("Greyed-out controls have no effect in the selected "
                       "colour / path mode.");
    paramNote->SetFontSize(10);
    paramNote->SetWrap(TextWrap::WrapWord);
    paramNote->SetTextColor(kTextFaded);
    paramCard->AddChild(paramNote);

    // ===== RIGHT: SVG output + status =====
    auto outputCard = std::make_shared<UltraCanvasContainer>(
        "VecOutputCard", outputX, topY, outputW, colH);
    outputCard->SetBackgroundColor(kCardBg);
    outputCard->SetBorders(1, kCardBorder);
    container->AddChild(outputCard);

    const int outputInnerW = outputW - 2 * pad;

    auto outputTitle = std::make_shared<UltraCanvasLabel>(
        "VecOutputTitle", pad, 12, outputInnerW, 20);
    outputTitle->SetText("SVG output");
    outputTitle->SetFontSize(13);
    outputTitle->SetFontWeight(FontWeight::Bold);
    outputTitle->SetTextColor(kTextStrong);
    outputCard->AddChild(outputTitle);

    auto outputFrame = std::make_shared<UltraCanvasContainer>(
        "VecOutputFrame", pad, 40, outputInnerW, 494);
    outputFrame->SetBackgroundColor(kOutputBg);
    outputFrame->SetBorders(1, kFrameBorder);
    outputCard->AddChild(outputFrame);

    auto outputImage = std::make_shared<UltraCanvasImageElement>(
        "VecOutputImage", 4, 4, outputInnerW - 8, 486);
    outputImage->SetFitMode(ImageFitMode::Contain);
    outputFrame->AddChild(outputImage);

    auto statusLabel = std::make_shared<UltraCanvasLabel>(
        "VecStatus", pad, 542, outputInnerW, 16);
    statusLabel->SetText("(press \"Vectorize\" to trace the input)");
    statusLabel->SetFontSize(10);
    statusLabel->SetTextColor(kTextMuted);
    outputCard->AddChild(statusLabel);

    auto detailLabel = std::make_shared<UltraCanvasLabel>(
        "VecDetail", pad, 560, outputInnerW, 16);
    detailLabel->SetFontSize(10);
    detailLabel->SetTextColor(kTextFaded);
    outputCard->AddChild(detailLabel);

    auto saveBtn = std::make_shared<UltraCanvasButton>(
        "VecSaveBtn", pad, 590, outputInnerW, 36);
    saveBtn->SetText("💾 Save SVG…");
    saveBtn->SetFontSize(12);
    saveBtn->SetColors(kSaveBtn, kSaveBtnH);
    saveBtn->SetTextColors(Color(255, 255, 255, 255));
    saveBtn->SetCornerRadius(6);
    saveBtn->SetDisabled(true);   // enabled once an SVG has been produced
    outputCard->AddChild(saveBtn);

    // ===== Shared state =====
    auto currentPath = std::make_shared<std::string>(defaultPath);
    auto lastSvg     = std::make_shared<std::string>();

    // ===== Control synchronisation =====
    // The parameter widgets refer to each other, so these helpers capture raw
    // pointers (the cards own the widgets) — owning captures would build a
    // reference cycle between the controls and leak the whole page.
    auto* colorDDPtr    = colorDD.get();
    auto* hierDDPtr     = hierDD.get();
    auto* pathDDPtr     = pathDD.get();
    auto* presetDDPtr   = presetDD.get();
    auto* presetHelpPtr = presetHelp.get();

    // Pushes *cfg into every widget. `syncing` keeps the slider callbacks from
    // treating these programmatic updates as user edits (which would flip the
    // preset dropdown to "Custom").
    auto refreshControls = [sliders, colorDDPtr, hierDDPtr, pathDDPtr, cfg, syncing]() {
        *syncing = true;
        colorDDPtr->SetSelectedIndex(IndexOfColorMode(cfg->colorMode), false);
        hierDDPtr ->SetSelectedIndex(IndexOfHierarchy(cfg->hierarchy), false);
        pathDDPtr ->SetSelectedIndex(IndexOfPathMode(cfg->pathMode),   false);
        for (ParamSlider& ps : *sliders) {
            const float value = ps.get(*cfg);
            const bool  active = ps.applies(*cfg);
            ps.slider->SetValue(value);
            ps.slider->SetDisabled(!active);
            ps.caption->SetText(CaptionText(ps.title, value, ps.decimals, ps.unit));
            ps.caption->SetTextColor(active ? kTextStrong : kTextFaded);
        }
        *syncing = false;
    };

    // Selects the preset that matches the current configuration, or "Custom".
    auto refreshPresetSelection = [presetDDPtr, presetHelpPtr, cfg]() {
        const std::vector<QualityPreset>& presets = Presets();
        int index = static_cast<int>(presets.size());   // "Custom"
        for (int i = 0; i < static_cast<int>(presets.size()); ++i) {
            if (SameConfig(presets[i].cfg, *cfg)) { index = i; break; }
        }
        presetDDPtr->SetSelectedIndex(index, false);
        presetHelpPtr->SetText(index < static_cast<int>(presets.size())
                               ? presets[index].help : kCustomPresetHelp);
    };

    // Runs after any single-parameter edit.
    auto onParamEdited = [refreshControls, refreshPresetSelection]() {
        refreshControls();
        refreshPresetSelection();
    };

    refreshControls();

    // ===== Parameter callbacks =====
    presetDD->onSelectionChanged =
        [cfg, presetHelpPtr, refreshControls](int index, const DropdownItem&) {
            const std::vector<QualityPreset>& presets = Presets();
            if (index < 0 || index >= static_cast<int>(presets.size())) {
                presetHelpPtr->SetText(kCustomPresetHelp);
                return;
            }
            *cfg = presets[index].cfg;
            presetHelpPtr->SetText(presets[index].help);
            refreshControls();
        };

    colorDD->onSelectionChanged = [cfg, syncing, onParamEdited](int index, const DropdownItem&) {
        if (*syncing) return;
        cfg->colorMode = ColorModeFromIndex(index);
        onParamEdited();
    };

    hierDD->onSelectionChanged = [cfg, syncing, onParamEdited](int index, const DropdownItem&) {
        if (*syncing) return;
        cfg->hierarchy = HierarchyFromIndex(index);
        onParamEdited();
    };

    pathDD->onSelectionChanged = [cfg, syncing, onParamEdited](int index, const DropdownItem&) {
        if (*syncing) return;
        cfg->pathMode = PathModeFromIndex(index);
        onParamEdited();
    };

    for (ParamSlider& ps : *sliders) {
        auto* caption = ps.caption;
        auto  title   = ps.title;
        auto  unit    = ps.unit;
        auto  decimals = ps.decimals;
        auto  setter  = ps.set;
        ps.slider->onValueChanged =
            [cfg, syncing, caption, title, unit, decimals, setter, refreshPresetSelection]
            (float value) {
                caption->SetText(CaptionText(title, value, decimals, unit));
                if (*syncing) return;
                setter(*cfg, value);
                refreshPresetSelection();
            };
    }

    resetBtn->onClick = [cfg, presetHelpPtr, presetDDPtr, refreshControls]() {
        *cfg = Presets().front().cfg;
        presetDDPtr->SetSelectedIndex(0, false);
        presetHelpPtr->SetText(Presets().front().help);
        refreshControls();
    };

    // ===== Tracing =====
    auto runVectorize = [outputImage, statusLabel, detailLabel, saveBtn,
                         currentPath, cfg, lastSvg]() {
        statusLabel->SetText("Tracing… (large bitmaps can take a few seconds)");
        detailLabel->SetText("");

        UltraCanvasVectorizer vec(*cfg);
        VectorizerResult r = vec.VectorizeFile(*currentPath);
        if (!r.Ok()) {
            lastSvg->clear();
            saveBtn->SetDisabled(true);
            statusLabel->SetText("Vectorize failed: " + r.error);
            return;
        }

        *lastSvg = r.svg;
        saveBtn->SetDisabled(false);

        const std::string outPath = TempSvgPath();
        if (!WriteFile(outPath, r.svg)) {
            statusLabel->SetText("Traced OK, but the preview file could not be written");
            detailLabel->SetText("Use \"Save SVG…\" to write the result elsewhere");
            return;
        }
        if (!outputImage->LoadFromFile(outPath, true)) {
            statusLabel->SetText("Traced OK, but the image element rejected the SVG");
            detailLabel->SetText("Preview file: " + outPath);
            return;
        }

        statusLabel->SetText("OK · " + std::to_string(r.width) + "×"
                           + std::to_string(r.height) + " · "
                           + FormatBytes(r.svg.size()) + " · "
                           + std::to_string(static_cast<long>(r.elapsedMs)) + " ms");
        detailLabel->SetText("Preview file: " + outPath);
    };

    // ===== Saving =====
    // Writes the SVG produced by the last successful trace wherever the user
    // wants it; the temp preview file is only an implementation detail.
    saveBtn->onClick = [pageRaw, statusLabel, detailLabel, lastSvg, currentPath]() {
        if (lastSvg->empty()) {
            statusLabel->SetText("Nothing to save yet — press \"Vectorize\" first.");
            return;
        }
        FileDialogOptions opts;
        opts.SetTitle("Save vectorized SVG")
            .SetDefaultFileName(SuggestedSvgName(*currentPath))
            .SetInitialDirectory(ParentDir(*currentPath))
            .AddFilter("SVG image", "svg")
            .AddFilter("All files", "*")
            .SetParentWindow(pageRaw ? pageRaw->GetWindow() : nullptr);

        UltraCanvasFileLoader::SaveFileDialog(opts,
            [statusLabel, detailLabel, lastSvg]
            (DialogResult result, const std::string& path) {
                if (result != DialogResult::OK || path.empty()) {
                    statusLabel->SetText("Save cancelled.");
                    return;
                }
                const std::string target = EnsureSvgExtension(path);
                if (!WriteFile(target, *lastSvg)) {
                    statusLabel->SetText("Could not write " + target);
                    return;
                }
                // SaveFileDialog already registered the chosen name with the
                // shell's recent-documents list (opts.registerAsRecent).
                statusLabel->SetText("Saved " + FormatBytes(lastSvg->size())
                                   + " to " + BaseName(target));
                detailLabel->SetText(target);
            });
    };

    // ===== INPUT ACTIONS =====
    const int btnY = 590;
    auto openBtn = std::make_shared<UltraCanvasButton>(
        "VecOpenBtn", pad, btnY, 130, 36);
    openBtn->SetText("📂 Open…");
    openBtn->SetFontSize(12);
    openBtn->SetColors(kNeutralBtn, kNeutralBtnH);
    openBtn->SetTextColors(kTextStrong);
    openBtn->SetCornerRadius(6);
    openBtn->onClick = [pageRaw, inputImage, inputPathLabel, currentPath, describeInput,
                        statusLabel, detailLabel, saveBtn, lastSvg]() {
        FileDialogOptions opts;
        opts.SetTitle("Choose a raster image to vectorize")
            .SetParentWindow(pageRaw ? pageRaw->GetWindow() : nullptr)
            .AddFilter("Images", {"png", "jpg", "jpeg", "webp", "tiff", "bmp", "gif"})
            .AddFilter("All files", "*");
        UltraCanvasFileLoader::OpenFileDialog(opts,
            [inputImage, inputPathLabel, currentPath, describeInput,
             statusLabel, detailLabel, saveBtn, lastSvg]
            (DialogResult r, const std::string& path) {
                if (r != DialogResult::OK || path.empty()) return;
                if (!inputImage->LoadFromFile(path)) {
                    statusLabel->SetText("Could not load " + BaseName(path) + ": "
                                       + inputImage->GetLastError());
                    return;
                }
                *currentPath = path;
                inputPathLabel->SetText(BaseName(path));
                inputPathLabel->SetTooltip(path);
                describeInput();

                // The SVG on the right belongs to the previous bitmap — drop it
                // so "Save SVG…" can never write a stale result under the new
                // source's name.
                lastSvg->clear();
                saveBtn->SetDisabled(true);
                statusLabel->SetText("New bitmap loaded — press \"Vectorize\" to trace it.");
                detailLabel->SetText("");
            });
    };
    inputCard->AddChild(openBtn);

    auto runBtn = std::make_shared<UltraCanvasButton>(
        "VecRunBtn", pad + 138, btnY, 134, 36);
    runBtn->SetText("✨ Vectorize");
    runBtn->SetFontSize(12);
    runBtn->SetColors(kRunBtn, kRunBtnH);
    runBtn->SetTextColors(Color(255, 255, 255, 255));
    runBtn->SetCornerRadius(6);
    runBtn->onClick = runVectorize;
    inputCard->AddChild(runBtn);

    return container;
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_HAS_VECTORIZER_PLUGIN
