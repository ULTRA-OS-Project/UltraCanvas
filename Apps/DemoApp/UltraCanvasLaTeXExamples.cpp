// Apps/DemoApp/UltraCanvasLaTeXExamples.cpp
// LaTeX documents demo: scans media/LaTex for .tex files at runtime and shows
// each one in a vertical (left-side) tab — the rendered output on top and the
// LaTeX source below. New examples appear automatically when a .tex file (plus
// its pre-rendered .png/.gif) is dropped into the media/LaTex folder.
// Version: 1.0.0
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasConfig.h"   // GetResourcesDir
#include "UltraCanvasUtils.h"    // NormalizePath, LoadFile

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace UltraCanvas {

namespace {

    // One tab page: the rendered result (a pre-rendered .png/.gif shipped next to
    // the .tex source) above the LaTeX source itself.
    std::shared_ptr<UltraCanvasUIElement> CreateLaTeXTabPage(const std::filesystem::path& texPath) {
        const std::string stem = texPath.stem().string();

        auto page = std::make_shared<UltraCanvasContainer>("LaTeXPage_" + stem, 0, 0, 800, 600);
        page->layout.SetFlexColumn().SetFlexGap(6)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        page->SetPadding(8, 10, 8, 10);
        page->SetBackgroundColor(Colors::White);

        // ----- Rendered output -----
        auto renderedLabel = std::make_shared<UltraCanvasLabel>("LaTeXRenderedLabel_" + stem, 0, 0, 0, 20);
        renderedLabel->SetText("Rendered output:");
        renderedLabel->SetFontSize(12);
        renderedLabel->SetFontWeight(FontWeight::Bold);
        renderedLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        page->AddChild(renderedLabel);

        // Look for the pre-rendered image next to the source (.png preferred,
        // .gif as fallback for animated examples).
        std::filesystem::path imagePath = texPath;
        imagePath.replace_extension(".png");
        std::error_code ec;
        if (!std::filesystem::exists(imagePath, ec)) {
            imagePath.replace_extension(".gif");
        }

        if (std::filesystem::exists(imagePath, ec)) {
            auto image = std::make_shared<UltraCanvasImageElement>("LaTeXImage_" + stem, 0, 0, 0, 0);
            image->LoadFromFile(imagePath.string());
            image->SetFitMode(ImageFitMode::Contain);
            image->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                             .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
            page->AddChild(image);
        } else {
            auto missing = std::make_shared<UltraCanvasLabel>("LaTeXNoImage_" + stem, 0, 0, 0, 0);
            missing->SetText("No rendered image (" + stem + ".png / .gif) found next to the source file.");
            missing->SetFontSize(12);
            missing->SetTextColor(Color(150, 60, 60, 255));
            missing->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
            missing->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                               .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
            page->AddChild(missing);
        }

        // ----- LaTeX source -----
        auto sourceLabel = std::make_shared<UltraCanvasLabel>("LaTeXSourceLabel_" + stem, 0, 0, 0, 20);
        sourceLabel->SetText("LaTeX source (" + texPath.filename().string() + "):");
        sourceLabel->SetFontSize(12);
        sourceLabel->SetFontWeight(FontWeight::Bold);
        sourceLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        page->AddChild(sourceLabel);

        auto source = std::make_shared<UltraCanvasTextArea>("LaTeXSource_" + stem);
        source->SetText(LoadFile(texPath.string()));
        source->SetReadOnly(true);
        source->SetShowLineNumbers(true);
        source->SetWordWrap(false);
        source->SetCursorPosition(LineColumnIndex::INVALID);
        source->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                          .SetFlexBasis(CSSLayout::Dimension::Pct(40))
                          .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        page->AddChild(source);

        return page;
    }

} // anonymous namespace

// ===== LATEX DOCUMENTS DEMO =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateLaTeXExamples() {
        // Root: a flex column that fills the display area.
        auto root = std::make_shared<UltraCanvasContainer>("LaTeXExamples", 0, 0, 1000, 700);
        root->layout.SetFlexColumn().SetFlexGap(8)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        root->SetPadding(10, 12, 10, 12);

        auto title = std::make_shared<UltraCanvasLabel>("LaTeXTitle", 0, 0, 0, 28);
        title->SetText("LaTeX Documents");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        title->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        root->AddChild(title);

        const std::string latexDir = NormalizePath(GetResourcesDir() + "media/LaTex");

        auto info = std::make_shared<UltraCanvasLabel>("LaTeXInfo", 0, 0, 0, 18);
        info->SetText("Example .tex documents scanned from " + latexDir +
                      " — pick a document in the vertical tab bar to see its rendered output and source.");
        info->SetFontSize(11);
        info->SetTextColor(Color(110, 110, 110, 255));
        info->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        root->AddChild(info);

        // Scan the LaTeX folder for .tex files (case-insensitive extension match)
        // so newly added examples show up without any code change.
        std::vector<std::filesystem::path> texFiles;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(latexDir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (ext == ".tex") {
                texFiles.push_back(entry.path());
            }
        }
        std::sort(texFiles.begin(), texFiles.end());

        if (texFiles.empty()) {
            auto empty = std::make_shared<UltraCanvasLabel>("LaTeXEmpty", 0, 0, 0, 0);
            empty->SetText("No .tex files found in " + latexDir);
            empty->SetFontSize(13);
            empty->SetTextColor(Color(150, 60, 60, 255));
            empty->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
            empty->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                             .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
            root->AddChild(empty);
            return root;
        }

        // Vertical tab bar on the left: one tab per .tex file, titled by filename.
        auto tabs = std::make_shared<UltraCanvasTabbedContainer>("LaTeXTabs", 0, 0, 0, 0);
        tabs->SetTabPosition(TabPosition::Left);
        tabs->SetTabStyle(TabStyle::Modern);
        tabs->SetCloseMode(TabCloseMode::NoClose);
        tabs->SetTabHeight(26);
        // The example set is large — allow jumping to any document through the
        // overflow dropdown when the tab column does not fit the window height.
        tabs->SetOverflowDropdownPosition(OverflowDropdownPosition::Right);
        tabs->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        for (const auto& texPath : texFiles) {
            tabs->AddTab(texPath.stem().string(), CreateLaTeXTabPage(texPath));
        }
        tabs->SetActiveTab(0);
        root->AddChild(tabs);

        return root;
    }

} // namespace UltraCanvas
