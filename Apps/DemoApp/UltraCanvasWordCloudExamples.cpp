// Apps/DemoApp/UltraCanvasWordCloudExamples.cpp
// Word Cloud diagram examples: frequency clouds, shape masks, raw-text
// tokenization and a center image with words flowing around it.
// Version: 1.0.0
// Last Modified: 2026-07-22

#include "UltraCanvasDemo.h"
#include "Plugins/Diagrams/UltraCanvasWordCloudDiagram.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasUtils.h"
#include <vector>

namespace UltraCanvas {

namespace {
    // Shared sample dataset: programming language popularity (TIOBE-like).
    std::vector<WordCloudWord> LanguageDataset() {
        std::vector<WordCloudWord> data = {
            {"Python", 100}, {"JavaScript", 82}, {"Java", 74}, {"C++", 70},
            {"C", 64}, {"C#", 58}, {"TypeScript", 52}, {"Rust", 46},
            {"Go", 44}, {"Kotlin", 34}, {"Swift", 32}, {"PHP", 30},
            {"Ruby", 24}, {"Dart", 22}, {"Scala", 18}, {"R", 17},
            {"Lua", 15}, {"Haskell", 13}, {"Elixir", 12}, {"Julia", 11},
            {"Perl", 10}, {"Fortran", 9}, {"COBOL", 8}, {"Erlang", 8},
            {"Zig", 7}, {"Nim", 6}, {"Crystal", 5}, {"Prolog", 5},
            {"Ada", 4}, {"Basic", 4}, {"Assembly", 12}, {"SQL", 40},
            {"HTML", 36}, {"CSS", 28}, {"Bash", 20}, {"PowerShell", 14},
            {"MATLAB", 16}, {"Groovy", 6}, {"Clojure", 7}, {"F#", 5}
        };
        return data;
    }

    const char* kSampleText =
        "UltraCanvas is a modern cross platform user interface framework. "
        "The framework renders every user interface element on a canvas "
        "surface with high quality vector graphics. Developers build "
        "applications from windows, containers, buttons, labels, charts and "
        "diagrams. Charts include line charts, bar charts, pie charts, radar "
        "charts and heatmap charts. Diagrams include flow charts, node "
        "diagrams, block diagrams, venn diagrams, sankey diagrams, tree maps "
        "and word clouds. The rendering engine supports linux, windows and "
        "mac os with native performance. Native performance and vector "
        "graphics make the framework a great choice for data visualization. "
        "Data visualization turns numbers into pictures people understand. "
        "A word cloud shows word frequency in a text at a glance. Frequent "
        "words render large while rare words render small. The layout engine "
        "places words on a spiral and avoids collisions with an occupancy "
        "grid. Masks, shapes and center images make every word cloud unique.";
}

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateWordCloudExamples() {
    const int PAD = 15;

    auto mainContainer = std::make_shared<UltraCanvasContainer>("wordCloudMainContainer", 0, 0, 1140, 800);
    mainContainer->SetBackgroundColor(Color(248, 249, 250, 255));

    // ===== HEADER =====
    auto headerContainer = std::make_shared<UltraCanvasContainer>("wcHeaderContainer", 0, 0, 1140, 90);
    headerContainer->SetBackgroundColor(Color(255, 255, 255, 255));
    mainContainer->AddChild(headerContainer);

    auto mainTitle = std::make_shared<UltraCanvasLabel>("wcMainTitle", 30, 15, 1080, 35);
    mainTitle->SetText("Word Cloud / Word Map Suite");
    mainTitle->SetFontSize(22);
    mainTitle->SetFontWeight(FontWeight::Bold);
    mainTitle->SetAlignment(TextAlignment::Left);
    mainTitle->SetTextColor(Color(33, 37, 41, 255));
    headerContainer->AddChild(mainTitle);

    auto subtitle = std::make_shared<UltraCanvasLabel>("wcSubtitle", 30, 52, 1000, 30);
    subtitle->SetText("Frequency-sized word layouts with shapes, rotation schemes, raw-text analysis and a center image");
    subtitle->SetFontSize(13);
    subtitle->SetAlignment(TextAlignment::Left);
    subtitle->SetTextColor(Color(108, 117, 125, 255));
    headerContainer->AddChild(subtitle);

    auto contentContainer = std::make_shared<UltraCanvasContainer>("wcContentContainer", 0, 90, 1140, 710);
    mainContainer->AddChild(contentContainer);

    // ===== LEFT SIDEBAR (controls) =====
    auto leftSidebar = std::make_shared<UltraCanvasContainer>("wcLeftSidebar", 20, 20, 200, 670);
    leftSidebar->SetBackgroundColor(Color(255, 255, 255, 255));
    contentContainer->AddChild(leftSidebar);

    const int SIDEBAR_W = 170;

    auto controlsTitle = std::make_shared<UltraCanvasLabel>("wcControlsTitle", PAD, PAD, SIDEBAR_W, 22);
    controlsTitle->SetText("CONTROLS");
    controlsTitle->SetFontSize(11);
    controlsTitle->SetFontWeight(FontWeight::Bold);
    controlsTitle->SetTextColor(Color(73, 80, 87, 255));
    controlsTitle->SetAlignment(TextAlignment::Left);
    leftSidebar->AddChild(controlsTitle);

    auto shapeLabel = std::make_shared<UltraCanvasLabel>("wcShapeLabel", PAD, 48, SIDEBAR_W, 20);
    shapeLabel->SetText("Cloud Shape");
    shapeLabel->SetFontSize(10);
    shapeLabel->SetFontWeight(FontWeight::Bold);
    leftSidebar->AddChild(shapeLabel);

    auto shapeSelector = std::make_shared<UltraCanvasDropdown>("wcShapeSelector", PAD, 70, SIDEBAR_W, 28);
    shapeSelector->AddItem("Ellipse", "ellipse");
    shapeSelector->AddItem("Circle", "circle");
    shapeSelector->AddItem("Rectangle", "rectangle");
    shapeSelector->AddItem("Cardioid (heart)", "cardioid");
    shapeSelector->AddItem("Diamond", "diamond");
    shapeSelector->AddItem("Triangle", "triangle");
    shapeSelector->AddItem("Pentagon", "pentagon");
    shapeSelector->AddItem("Star", "star");
    shapeSelector->SetSelectedIndex(0);
    leftSidebar->AddChild(shapeSelector);

    auto colorLabel = std::make_shared<UltraCanvasLabel>("wcColorLabel", PAD, 108, SIDEBAR_W, 20);
    colorLabel->SetText("Color Mode");
    colorLabel->SetFontSize(10);
    colorLabel->SetFontWeight(FontWeight::Bold);
    leftSidebar->AddChild(colorLabel);

    auto colorSelector = std::make_shared<UltraCanvasDropdown>("wcColorSelector", PAD, 130, SIDEBAR_W, 28);
    colorSelector->AddItem("Palette", "palette");
    colorSelector->AddItem("Random palette", "random");
    colorSelector->AddItem("Weight gradient", "gradient");
    colorSelector->AddItem("Single color", "single");
    colorSelector->SetSelectedIndex(0);
    leftSidebar->AddChild(colorSelector);

    auto rotationLabel = std::make_shared<UltraCanvasLabel>("wcRotationLabel", PAD, 168, SIDEBAR_W, 20);
    rotationLabel->SetText("Rotation");
    rotationLabel->SetFontSize(10);
    rotationLabel->SetFontWeight(FontWeight::Bold);
    leftSidebar->AddChild(rotationLabel);

    auto rotationSelector = std::make_shared<UltraCanvasDropdown>("wcRotationSelector", PAD, 190, SIDEBAR_W, 28);
    rotationSelector->AddItem("Mixed -90..90 (3)", "steps3");
    rotationSelector->AddItem("Horizontal only", "none");
    rotationSelector->AddItem("Horizontal + vertical", "hv");
    rotationSelector->AddItem("Random -60..60", "random");
    rotationSelector->SetSelectedIndex(0);
    leftSidebar->AddChild(rotationSelector);

    auto scalingLabel = std::make_shared<UltraCanvasLabel>("wcScalingLabel", PAD, 228, SIDEBAR_W, 20);
    scalingLabel->SetText("Size Scaling");
    scalingLabel->SetFontSize(10);
    scalingLabel->SetFontWeight(FontWeight::Bold);
    leftSidebar->AddChild(scalingLabel);

    auto scalingSelector = std::make_shared<UltraCanvasDropdown>("wcScalingSelector", PAD, 250, SIDEBAR_W, 28);
    scalingSelector->AddItem("Square root", "sqrt");
    scalingSelector->AddItem("Linear", "linear");
    scalingSelector->AddItem("Logarithmic", "log");
    scalingSelector->AddItem("By rank", "rank");
    scalingSelector->SetSelectedIndex(0);
    leftSidebar->AddChild(scalingSelector);

    auto centerBlockLabel = std::make_shared<UltraCanvasLabel>("wcCenterBlockLabel", PAD, 288, SIDEBAR_W, 20);
    centerBlockLabel->SetText("Center Image Cutout");
    centerBlockLabel->SetFontSize(10);
    centerBlockLabel->SetFontWeight(FontWeight::Bold);
    leftSidebar->AddChild(centerBlockLabel);

    auto centerBlockSelector = std::make_shared<UltraCanvasDropdown>("wcCenterBlockSelector", PAD, 310, SIDEBAR_W, 28);
    centerBlockSelector->AddItem("Bounding box", "box");
    centerBlockSelector->AddItem("Ellipse", "ellipse");
    centerBlockSelector->AddItem("Opaque pixels", "alpha");
    centerBlockSelector->SetSelectedIndex(0);
    leftSidebar->AddChild(centerBlockSelector);

    auto btnSeed = std::make_shared<UltraCanvasButton>("wcBtnSeed", PAD, 352, SIDEBAR_W, 32);
    btnSeed->SetText("Shuffle Layout Seed");
    btnSeed->SetBackgroundColor(Color(13, 110, 253, 255));
    leftSidebar->AddChild(btnSeed);

    auto helpBox = std::make_shared<UltraCanvasLabel>("wcHelpBox", PAD, 400, SIDEBAR_W, 150);
    helpBox->SetText("INTERACTION TIPS\n\n"
                     "- Hover a word for its\n"
                     "  weight tooltip\n"
                     "- Click words (see the\n"
                     "  status bar)\n"
                     "- Controls apply to the\n"
                     "  active tab too\n"
                     "- Center Image tab puts\n"
                     "  a logo in the middle");
    helpBox->SetFontSize(9);
    helpBox->SetAlignment(TextAlignment::Left);
    helpBox->SetBackgroundColor(Color(255, 243, 205, 255));
    helpBox->SetPadding(10);
    leftSidebar->AddChild(helpBox);

    // ===== TABS =====
    auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>("wcTabs", 240, 20, 660, 580);
    tabbedContainer->SetTabPosition(TabPosition::Top);
    contentContainer->AddChild(tabbedContainer);

    const int CLOUD_X = 10, CLOUD_Y = 60, CLOUD_W = 640, CLOUD_H = 470;

    // ----- TAB 1: FREQUENCY CLOUD -----
    auto frequencyTab = std::make_shared<UltraCanvasContainer>("wcFrequencyTab", 0, 0, 660, 542);
    frequencyTab->SetBackgroundColor(Color(255, 255, 255, 255));

    auto frequencyTitle = std::make_shared<UltraCanvasLabel>("wcFrequencyTitle", 20, 15, 620, 30);
    frequencyTitle->SetText("Programming Language Popularity");
    frequencyTitle->SetFontSize(18);
    frequencyTitle->SetFontWeight(FontWeight::Bold);
    frequencyTitle->SetAlignment(TextAlignment::Center);
    frequencyTab->AddChild(frequencyTitle);

    auto frequencyCloud = CreateWordCloud("wcFrequencyCloud", CLOUD_X, CLOUD_Y, CLOUD_W, CLOUD_H);
    frequencyCloud->SetWords(LanguageDataset());
    frequencyCloud->SetFontSizeRange(11, 64);
    frequencyCloud->SetShape(WordCloudShape::Ellipse);
    frequencyTab->AddChild(frequencyCloud);

    // ----- TAB 2: CENTER IMAGE -----
    auto centerTab = std::make_shared<UltraCanvasContainer>("wcCenterTab", 0, 0, 660, 542);
    centerTab->SetBackgroundColor(Color(255, 255, 255, 255));

    auto centerTitle = std::make_shared<UltraCanvasLabel>("wcCenterTitle", 20, 15, 620, 30);
    centerTitle->SetText("Words Around a Center Image");
    centerTitle->SetFontSize(18);
    centerTitle->SetFontWeight(FontWeight::Bold);
    centerTitle->SetAlignment(TextAlignment::Center);
    centerTab->AddChild(centerTitle);

    auto centerCloud = CreateWordCloud("wcCenterCloud", CLOUD_X, CLOUD_Y, CLOUD_W, CLOUD_H);
    centerCloud->SetWords(LanguageDataset());
    centerCloud->SetFontSizeRange(10, 48);
    centerCloud->SetShape(WordCloudShape::Ellipse);
    centerCloud->SetColorMode(WordCloudColorMode::ByWeightGradient);
    centerCloud->SetColorGradient(Color(173, 181, 189, 255), Color(13, 110, 253, 255));
    WordCloudCenterImage logo;
    logo.imagePath = NormalizePath(GetResourcesDir() + "media/images/UltraCanvas-logo.png");
    logo.sizeRatio = 0.34f;
    logo.padding = 12.0f;
    logo.blockMode = WordCloudCenterBlock::BoundingBox;
    centerCloud->SetCenterImage(logo);
    centerTab->AddChild(centerCloud);

    // ----- TAB 3: FROM RAW TEXT -----
    auto textTab = std::make_shared<UltraCanvasContainer>("wcTextTab", 0, 0, 660, 542);
    textTab->SetBackgroundColor(Color(255, 255, 255, 255));

    auto textTitle = std::make_shared<UltraCanvasLabel>("wcTextTitle", 20, 15, 620, 30);
    textTitle->SetText("Built From Raw Text (stopwords + bigrams)");
    textTitle->SetFontSize(18);
    textTitle->SetFontWeight(FontWeight::Bold);
    textTitle->SetAlignment(TextAlignment::Center);
    textTab->AddChild(textTitle);

    auto textCloud = CreateWordCloud("wcTextCloud", CLOUD_X, CLOUD_Y, CLOUD_W, CLOUD_H);
    WordCloudTextOptions textOptions;
    textOptions.maxWords = 80;
    textOptions.minWordLength = 3;
    textOptions.detectBigrams = true;
    textOptions.minBigramCount = 2;
    textCloud->SetWordsFromText(kSampleText, textOptions);
    textCloud->SetFontSizeRange(10, 52);
    textCloud->SetColorMode(WordCloudColorMode::Palette);
    textTab->AddChild(textCloud);

    // ----- TAB 4: SHAPED CLOUD -----
    auto shapedTab = std::make_shared<UltraCanvasContainer>("wcShapedTab", 0, 0, 660, 542);
    shapedTab->SetBackgroundColor(Color(255, 255, 255, 255));

    auto shapedTitle = std::make_shared<UltraCanvasLabel>("wcShapedTitle", 20, 15, 620, 30);
    shapedTitle->SetText("Shaped Cloud: Star with Repeat Fill");
    shapedTitle->SetFontSize(18);
    shapedTitle->SetFontWeight(FontWeight::Bold);
    shapedTitle->SetAlignment(TextAlignment::Center);
    shapedTab->AddChild(shapedTitle);

    auto shapedCloud = CreateWordCloud("wcShapedCloud", CLOUD_X, CLOUD_Y, CLOUD_W, CLOUD_H);
    shapedCloud->SetWords(LanguageDataset());
    shapedCloud->SetShape(WordCloudShape::Star);
    shapedCloud->SetFontSizeRange(9, 40);
    shapedCloud->SetRepeatWords(true);
    shapedCloud->SetMaxWords(160);
    shapedCloud->SetColorMode(WordCloudColorMode::RandomFromPalette);
    shapedCloud->SetRotateRatio(0.2f);
    shapedTab->AddChild(shapedCloud);

    tabbedContainer->AddTab("Frequency Cloud", frequencyTab);
    tabbedContainer->AddTab("Center Image", centerTab);
    tabbedContainer->AddTab("From Raw Text", textTab);
    tabbedContainer->AddTab("Shaped Cloud", shapedTab);
    tabbedContainer->SetActiveTab(0);

    // ===== RIGHT SIDEBAR (info) =====
    auto rightSidebar = std::make_shared<UltraCanvasContainer>("wcRightSidebar", 920, 20, 200, 580);
    rightSidebar->SetBackgroundColor(Color(255, 255, 255, 255));
    contentContainer->AddChild(rightSidebar);

    auto statsTitle = std::make_shared<UltraCanvasLabel>("wcStatsTitle", PAD, PAD, SIDEBAR_W, 24);
    statsTitle->SetText("LAYOUT STATISTICS");
    statsTitle->SetFontSize(11);
    statsTitle->SetFontWeight(FontWeight::Bold);
    statsTitle->SetTextColor(Color(73, 80, 87, 255));
    statsTitle->SetAlignment(TextAlignment::Left);
    rightSidebar->AddChild(statsTitle);

    auto statsPanel = std::make_shared<UltraCanvasLabel>("wcStatsPanel", PAD, 48, SIDEBAR_W, 130);
    statsPanel->SetText("Words placed: -\nWords skipped: -");
    statsPanel->SetFontSize(10);
    statsPanel->SetAlignment(TextAlignment::Left);
    statsPanel->SetBackgroundColor(Color(248, 249, 250, 255));
    statsPanel->SetPadding(12);
    rightSidebar->AddChild(statsPanel);

    auto featureTitle = std::make_shared<UltraCanvasLabel>("wcFeatureTitle", PAD, 196, SIDEBAR_W, 24);
    featureTitle->SetText("FEATURE NOTES");
    featureTitle->SetFontSize(11);
    featureTitle->SetFontWeight(FontWeight::Bold);
    featureTitle->SetTextColor(Color(73, 80, 87, 255));
    featureTitle->SetAlignment(TextAlignment::Left);
    rightSidebar->AddChild(featureTitle);

    auto featurePanel = std::make_shared<UltraCanvasLabel>("wcFeaturePanel", PAD, 228, SIDEBAR_W, 250);
    featurePanel->SetText("Greedy spiral layout on\nan occupancy grid.\n\n"
                          "Sizes map frequency to\nfont size (sqrt, linear,\nlog or rank).\n\n"
                          "The center image blocks\nits region so words\nflow around it.\n\n"
                          "Mask images shape the\ncloud like the Python\nwordcloud package.");
    featurePanel->SetFontSize(9);
    featurePanel->SetAlignment(TextAlignment::Left);
    featurePanel->SetBackgroundColor(Color(248, 249, 250, 255));
    featurePanel->SetPadding(12);
    rightSidebar->AddChild(featurePanel);

    // ===== STATUS BAR =====
    auto statusBar = std::make_shared<UltraCanvasLabel>("wcStatusBar", 240, 610, 660, 60);
    statusBar->SetText("Ready - hover words for tooltips, click a word to inspect it");
    statusBar->SetFontSize(11);
    statusBar->SetAlignment(TextAlignment::Left);
    statusBar->SetBackgroundColor(Color(233, 236, 239, 255));
    statusBar->SetPadding(15);
    statusBar->SetTextColor(Color(73, 80, 87, 255));
    contentContainer->AddChild(statusBar);

    // ===== WIRING =====
    std::vector<std::shared_ptr<UltraCanvasWordCloudElement>> allClouds = {
        frequencyCloud, centerCloud, textCloud, shapedCloud
    };

    for (auto& cloud : allClouds) {
        cloud->SetOnWordClick([statusBar](const WordCloudPlacedWord& word) {
            statusBar->SetText("Clicked \"" + word.text + "\" (weight " +
                               std::to_string(static_cast<int>(word.weight)) +
                               ", " + std::to_string(static_cast<int>(word.fontSize)) + "px)");
        });
    }
    frequencyCloud->SetOnLayoutComplete([statsPanel](size_t placedCount, size_t totalCount) {
        statsPanel->SetText("Words placed: " + std::to_string(placedCount) +
                            "\nWords skipped: " + std::to_string(totalCount - placedCount));
    });

    shapeSelector->onSelectionChanged =
        [allClouds](int, const DropdownItem& item) {
            WordCloudShape shape = WordCloudShape::Ellipse;
            if (item.value == "circle") shape = WordCloudShape::Circle;
            else if (item.value == "rectangle") shape = WordCloudShape::Rectangle;
            else if (item.value == "cardioid") shape = WordCloudShape::Cardioid;
            else if (item.value == "diamond") shape = WordCloudShape::Diamond;
            else if (item.value == "triangle") shape = WordCloudShape::Triangle;
            else if (item.value == "pentagon") shape = WordCloudShape::Pentagon;
            else if (item.value == "star") shape = WordCloudShape::Star;
            for (auto& cloud : allClouds) cloud->SetShape(shape);
        };

    colorSelector->onSelectionChanged =
        [allClouds](int, const DropdownItem& item) {
            WordCloudColorMode mode = WordCloudColorMode::Palette;
            if (item.value == "random") mode = WordCloudColorMode::RandomFromPalette;
            else if (item.value == "gradient") mode = WordCloudColorMode::ByWeightGradient;
            else if (item.value == "single") mode = WordCloudColorMode::SingleColor;
            for (auto& cloud : allClouds) cloud->SetColorMode(mode);
        };

    rotationSelector->onSelectionChanged =
        [allClouds](int, const DropdownItem& item) {
            for (auto& cloud : allClouds) {
                if (item.value == "none") {
                    cloud->SetRotationMode(WordCloudRotationMode::NoRotation);
                } else if (item.value == "hv") {
                    cloud->SetRotationMode(WordCloudRotationMode::Steps);
                    cloud->SetRotationAngles({90.0f});
                    cloud->SetRotateRatio(0.4f);
                } else if (item.value == "random") {
                    cloud->SetRotationMode(WordCloudRotationMode::RandomRange);
                    cloud->SetRotationRange(-60.0f, 60.0f);
                    cloud->SetRotateRatio(0.6f);
                } else {
                    cloud->SetRotationMode(WordCloudRotationMode::Steps);
                    cloud->SetRotationAngles({});
                    cloud->SetRotationRange(-90.0f, 90.0f);
                    cloud->SetRotationSteps(3);
                    cloud->SetRotateRatio(0.35f);
                }
            }
        };

    scalingSelector->onSelectionChanged =
        [allClouds](int, const DropdownItem& item) {
            WordCloudScaling scaling = WordCloudScaling::SquareRoot;
            if (item.value == "linear") scaling = WordCloudScaling::Linear;
            else if (item.value == "log") scaling = WordCloudScaling::Logarithmic;
            else if (item.value == "rank") scaling = WordCloudScaling::Rank;
            for (auto& cloud : allClouds) cloud->SetScaling(scaling);
        };

    centerBlockSelector->onSelectionChanged =
        [centerCloud](int, const DropdownItem& item) {
            WordCloudCenterImage image = centerCloud->GetCenterImage();
            if (!image.IsValid()) return;
            if (item.value == "ellipse") image.blockMode = WordCloudCenterBlock::Ellipse;
            else if (item.value == "alpha") image.blockMode = WordCloudCenterBlock::AlphaPixels;
            else image.blockMode = WordCloudCenterBlock::BoundingBox;
            centerCloud->SetCenterImage(image);
        };

    btnSeed->onClick = [allClouds]() {
        static uint32_t seed = 1;
        seed++;
        for (auto& cloud : allClouds) cloud->SetRandomSeed(seed);
    };

    return mainContainer;
}

} // namespace UltraCanvas
