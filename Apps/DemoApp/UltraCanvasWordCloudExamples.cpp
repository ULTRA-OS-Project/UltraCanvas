// Apps/DemoApp/UltraCanvasWordCloudExamples.cpp
// Word Cloud diagram examples: frequency clouds, shape masks, raw-text
// tokenization and a center image with words flowing around it.
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): controls, tabs and
//                  statistics as cards in a wrapping row, every label and panel
//                  sized to its text, the button as wide as its text
// Version: 1.0.0
// Last Modified: 2026-10-23

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
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

    // One tab's page: its title centred over the cloud. The tabbed container
    // gives it its size; it scrolls when a longer title wraps and the cloud
    // no longer fits under it.
    std::shared_ptr<UltraCanvasContainer> MakeWordCloudTabPage(const std::string& id,
                                                               const std::string& titleId,
                                                               const std::string& title) {
        auto tab = std::make_shared<UltraCanvasContainer>(id);
        tab->SetBackgroundColor(Color(255, 255, 255, 255));
        tab->SetPadding(15, 10, 6, 10);
        tab->layout.SetFlexColumn().SetFlexGap(8)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        ContainerStyle style;
        style.autoShowScrollbars = true;
        tab->SetContainerStyle(style);

        auto titleLabel = DemoKit::CreateText(titleId, title);
        titleLabel->SetFontSize(18);
        titleLabel->SetFontWeight(FontWeight::Bold);
        titleLabel->SetAlignment(TextAlignment::Center, VerticalAlignment::Top);
        DemoKit::Add(tab, titleLabel);
        return tab;
    }

    // A cloud keeps the size it was built with, centred in its tab.
    void AddWordCloud(const std::shared_ptr<UltraCanvasContainer>& tab,
                      const std::shared_ptr<UltraCanvasWordCloudElement>& cloud) {
        DemoKit::AddFixed(tab, cloud);
        cloud->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);
    }

    // A sidebar section label: small and bold above its control.
    void AddWordCloudControlLabel(const std::shared_ptr<UltraCanvasContainer>& parent,
                                  const std::string& id, const std::string& text) {
        auto label = DemoKit::AddText(parent, id, text);
        label->SetFontSize(10);
        label->SetFontWeight(FontWeight::Bold);
        label->SetMargin(6, 0, 0, 0);
    }

    // A sidebar heading in the sidebars' small capitals style.
    std::shared_ptr<UltraCanvasLabel> AddWordCloudSidebarHeading(const std::shared_ptr<UltraCanvasContainer>& parent,
                                                                 const std::string& id, const std::string& text) {
        auto heading = DemoKit::AddText(parent, id, text);
        heading->SetFontSize(11);
        heading->SetFontWeight(FontWeight::Bold);
        heading->SetTextColor(Color(73, 80, 87, 255));
        return heading;
    }

    // A grey or tinted text panel in a sidebar: wraps and grows with its text.
    std::shared_ptr<UltraCanvasLabel> AddWordCloudPanelText(const std::shared_ptr<UltraCanvasContainer>& parent,
                                                            const std::string& id, const std::string& text,
                                                            float fontSize, const Color& background,
                                                            int padding) {
        auto panel = DemoKit::AddText(parent, id, text);
        panel->SetFontSize(fontSize);
        panel->SetBackgroundColor(background);
        panel->SetPadding(padding);
        return panel;
    }
}

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateWordCloudExamples() {
    auto mainContainer = DemoKit::CreatePage("wordCloudMainContainer", "Word Cloud / Word Map Suite",
            "Frequency-sized word layouts with shapes, rotation schemes, raw-text analysis and a center image");

    // Controls, the tabs and the statistics side by side; a narrow window
    // puts the sidebars under the tabs.
    auto contentContainer = DemoKit::AddRow(mainContainer, "wcContentContainer");

    // ===== LEFT SIDEBAR (controls) =====
    auto leftSidebar = DemoKit::AddCard(contentContainer, "wcLeftSidebar", "", "", 200);
    AddWordCloudSidebarHeading(leftSidebar, "wcControlsTitle", "CONTROLS");

    AddWordCloudControlLabel(leftSidebar, "wcShapeLabel", "Cloud Shape");
    auto shapeSelector = std::make_shared<UltraCanvasDropdown>("wcShapeSelector", 0, 0, 170, 28);
    shapeSelector->AddItem("Ellipse", "ellipse");
    shapeSelector->AddItem("Circle", "circle");
    shapeSelector->AddItem("Rectangle", "rectangle");
    shapeSelector->AddItem("Cardioid (heart)", "cardioid");
    shapeSelector->AddItem("Diamond", "diamond");
    shapeSelector->AddItem("Triangle", "triangle");
    shapeSelector->AddItem("Pentagon", "pentagon");
    shapeSelector->AddItem("Star", "star");
    shapeSelector->SetSelectedIndex(0);
    DemoKit::AddStretched(leftSidebar, shapeSelector);

    AddWordCloudControlLabel(leftSidebar, "wcColorLabel", "Color Mode");
    auto colorSelector = std::make_shared<UltraCanvasDropdown>("wcColorSelector", 0, 0, 170, 28);
    colorSelector->AddItem("Palette", "palette");
    colorSelector->AddItem("Random palette", "random");
    colorSelector->AddItem("Weight gradient", "gradient");
    colorSelector->AddItem("Single color", "single");
    colorSelector->SetSelectedIndex(0);
    DemoKit::AddStretched(leftSidebar, colorSelector);

    AddWordCloudControlLabel(leftSidebar, "wcRotationLabel", "Rotation");
    auto rotationSelector = std::make_shared<UltraCanvasDropdown>("wcRotationSelector", 0, 0, 170, 28);
    rotationSelector->AddItem("Mixed -90..90 (3)", "steps3");
    rotationSelector->AddItem("Horizontal only", "none");
    rotationSelector->AddItem("Horizontal + vertical", "hv");
    rotationSelector->AddItem("Random -60..60", "random");
    rotationSelector->SetSelectedIndex(0);
    DemoKit::AddStretched(leftSidebar, rotationSelector);

    AddWordCloudControlLabel(leftSidebar, "wcScalingLabel", "Size Scaling");
    auto scalingSelector = std::make_shared<UltraCanvasDropdown>("wcScalingSelector", 0, 0, 170, 28);
    scalingSelector->AddItem("Square root", "sqrt");
    scalingSelector->AddItem("Linear", "linear");
    scalingSelector->AddItem("Logarithmic", "log");
    scalingSelector->AddItem("By rank", "rank");
    scalingSelector->SetSelectedIndex(0);
    DemoKit::AddStretched(leftSidebar, scalingSelector);

    AddWordCloudControlLabel(leftSidebar, "wcCenterBlockLabel", "Center Image Cutout");
    auto centerBlockSelector = std::make_shared<UltraCanvasDropdown>("wcCenterBlockSelector", 0, 0, 170, 28);
    centerBlockSelector->AddItem("Bounding box", "box");
    centerBlockSelector->AddItem("Ellipse", "ellipse");
    centerBlockSelector->AddItem("Opaque pixels", "alpha");
    centerBlockSelector->SetSelectedIndex(0);
    DemoKit::AddStretched(leftSidebar, centerBlockSelector);

    // As wide as its text, at least the width it had.
    auto btnSeed = std::make_shared<UltraCanvasButton>("wcBtnSeed", 0, 0, 0, 32, "Shuffle Layout Seed");
    btnSeed->SetBackgroundColor(Color(13, 110, 253, 255));
    {
        CSSLayout::BoxConstraints limits;
        limits.minWidth = CSSLayout::Dimension::Px(170);
        btnSeed->boxConstraints = limits;
    }
    btnSeed->SetMargin(12, 0, 0, 0);
    DemoKit::Add(leftSidebar, btnSeed);

    auto helpBox = AddWordCloudPanelText(leftSidebar, "wcHelpBox",
            "INTERACTION TIPS\n\n"
            "- Hover a word for its weight tooltip\n"
            "- Click words (see the status bar)\n"
            "- Controls apply to the active tab too\n"
            "- Center Image tab puts a logo in the middle",
            9, Color(255, 243, 205, 255), 10);
    helpBox->SetMargin(12, 0, 0, 0);

    // ===== TABS =====
    auto tabsCard = DemoKit::AddCard(contentContainer, "wcTabsCard", "", "", 690);
    auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>("wcTabs", 0, 0, 660, 580);
    tabbedContainer->SetTabPosition(TabPosition::Top);
    // As wide as the card, never narrower than the clouds need.
    DemoKit::AddStretched(tabsCard, tabbedContainer);
    {
        CSSLayout::BoxConstraints limits;
        limits.minWidth = CSSLayout::Dimension::Px(660);
        tabbedContainer->boxConstraints = limits;
    }

    const int CLOUD_W = 640, CLOUD_H = 470;

    // ----- TAB 1: FREQUENCY CLOUD -----
    auto frequencyTab = MakeWordCloudTabPage("wcFrequencyTab", "wcFrequencyTitle", "Programming Language Popularity");

    auto frequencyCloud = CreateWordCloud("wcFrequencyCloud", 0, 0, CLOUD_W, CLOUD_H);
    frequencyCloud->SetWords(LanguageDataset());
    frequencyCloud->SetFontSizeRange(11, 64);
    frequencyCloud->SetShape(WordCloudShape::Ellipse);
    AddWordCloud(frequencyTab, frequencyCloud);

    // ----- TAB 2: CENTER IMAGE -----
    auto centerTab = MakeWordCloudTabPage("wcCenterTab", "wcCenterTitle", "Words Around a Center Image");

    auto centerCloud = CreateWordCloud("wcCenterCloud", 0, 0, CLOUD_W, CLOUD_H);
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
    AddWordCloud(centerTab, centerCloud);

    // ----- TAB 3: FROM RAW TEXT -----
    auto textTab = MakeWordCloudTabPage("wcTextTab", "wcTextTitle", "Built From Raw Text (stopwords + bigrams)");

    auto textCloud = CreateWordCloud("wcTextCloud", 0, 0, CLOUD_W, CLOUD_H);
    WordCloudTextOptions textOptions;
    textOptions.maxWords = 80;
    textOptions.minWordLength = 3;
    textOptions.detectBigrams = true;
    textOptions.minBigramCount = 2;
    textCloud->SetWordsFromText(kSampleText, textOptions);
    textCloud->SetFontSizeRange(10, 52);
    textCloud->SetColorMode(WordCloudColorMode::Palette);
    AddWordCloud(textTab, textCloud);

    // ----- TAB 4: SHAPED CLOUD -----
    auto shapedTab = MakeWordCloudTabPage("wcShapedTab", "wcShapedTitle", "Shaped Cloud: Star with Repeat Fill");

    auto shapedCloud = CreateWordCloud("wcShapedCloud", 0, 0, CLOUD_W, CLOUD_H);
    shapedCloud->SetWords(LanguageDataset());
    shapedCloud->SetShape(WordCloudShape::Star);
    shapedCloud->SetFontSizeRange(9, 40);
    shapedCloud->SetRepeatWords(true);
    shapedCloud->SetMaxWords(160);
    shapedCloud->SetColorMode(WordCloudColorMode::RandomFromPalette);
    shapedCloud->SetRotateRatio(0.2f);
    AddWordCloud(shapedTab, shapedCloud);

    tabbedContainer->AddTab("Frequency Cloud", frequencyTab);
    tabbedContainer->AddTab("Center Image", centerTab);
    tabbedContainer->AddTab("From Raw Text", textTab);
    tabbedContainer->AddTab("Shaped Cloud", shapedTab);
    tabbedContainer->SetActiveTab(0);

    // ===== STATUS BAR =====
    auto statusBar = AddWordCloudPanelText(tabsCard, "wcStatusBar",
            "Ready - hover words for tooltips, click a word to inspect it",
            11, Color(233, 236, 239, 255), 15);
    statusBar->SetTextColor(Color(73, 80, 87, 255));

    // ===== RIGHT SIDEBAR (info) =====
    auto rightSidebar = DemoKit::AddCard(contentContainer, "wcRightSidebar", "", "", 200);
    AddWordCloudSidebarHeading(rightSidebar, "wcStatsTitle", "LAYOUT STATISTICS");
    auto statsPanel = AddWordCloudPanelText(rightSidebar, "wcStatsPanel",
            "Words placed: -\nWords skipped: -",
            10, Color(248, 249, 250, 255), 12);

    AddWordCloudSidebarHeading(rightSidebar, "wcFeatureTitle", "FEATURE NOTES")->SetMargin(12, 0, 0, 0);
    AddWordCloudPanelText(rightSidebar, "wcFeaturePanel",
            "Greedy spiral layout on an occupancy grid.\n\n"
            "Sizes map frequency to font size (sqrt, linear, log or rank).\n\n"
            "The center image blocks its region so words flow around it.\n\n"
            "Mask images shape the cloud like the Python wordcloud package.",
            9, Color(248, 249, 250, 255), 12);

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
