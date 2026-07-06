// Apps/DemoApp/UltraCanvasLaTeXExamples.cpp
// LaTeX documents demo: scans media/LaTex for .tex files at runtime and shows
// each one in a vertical (left-side) tab — the rendered output on top and the
// LaTeX source below.
//
// The rendered output is produced *live* by the on-demand UltraCanvas LaTeX
// engine (MicroTeX): the .tex source is parsed and typeset to vector paths at
// runtime — no pre-baked screenshot. The shipped examples (media/LaTex/*.tex)
// are all math-mode documents that the engine renders directly.
//
// The engine is a math typesetter, not a full LaTeX/TikZ implementation. If a
// document that reaches for TikZ / pgfplots graphics or document-mode content
// is dropped into the folder, it cannot be typeset by the engine; the demo
// then falls back to a reference .png/.gif sitting beside the source (labelled
// honestly as a reference render) if one exists.
//
// New examples appear automatically when a .tex file is dropped into the
// media/LaTex folder.
// Version: 2.1.0
// Last Modified: 2026-07-06
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasConfig.h"   // GetResourcesDir
#include "UltraCanvasUtils.h"    // NormalizePath, LoadFile
#include "Plugins/LaTeX/UltraCanvasLaTeXView.h"  // CreateLaTeXView (on-demand)

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace UltraCanvas {

namespace {

    std::string ToLowerCopy(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    std::string Trim(const std::string& s) {
        const size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return {};
        const size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    // A document is renderable by the math engine only if it is plain math-mode
    // LaTeX. MicroTeX is a math typesetter, not a full LaTeX implementation, so
    // anything that reaches for TikZ / pgfplots graphics, or for document-mode
    // constructs (tables, figures, captions, included images), is out of scope
    // and must fall back to its reference image. The check is deliberately
    // conservative: when in doubt we prefer the reference image over feeding the
    // engine something it will refuse to parse and leaving a blank pane.
    bool IsMathRenderable(const std::string& source) {
        static const char* kUnsupportedMarkers[] = {
            // TikZ / pgfplots graphics
            "tikzpicture", "pgfplot", "\\begin{axis}", "tikzset",
            "usetikzlibrary", "\\tikz", "pgfmath", "\\draw", "\\node",
            // document-mode / tabular / floats — not math mode
            "\\begin{tabular}", "\\begin{table}", "\\begin{figure}",
            "\\includegraphics", "\\caption", "booktabs", "\\toprule",
            "\\captionsetup",
        };
        const std::string lower = ToLowerCopy(source);
        for (const char* marker : kUnsupportedMarkers) {
            if (lower.find(marker) != std::string::npos) return false;
        }
        return true;
    }

    // Pull the math body out of a full .tex document so the engine gets exactly
    // what it can parse: the content between \begin{document} and \end{document},
    // stripped of comments and of the outer math delimiters ($ … $, \[ … \]).
    std::string ExtractRenderableMath(const std::string& source) {
        std::string body = source;

        const size_t docBegin = body.find("\\begin{document}");
        if (docBegin != std::string::npos) {
            body = body.substr(docBegin + std::string("\\begin{document}").size());
        }
        const size_t docEnd = body.find("\\end{document}");
        if (docEnd != std::string::npos) {
            body = body.substr(0, docEnd);
        }

        // Drop LaTeX comments (an unescaped '%' to end of line).
        std::string noComments;
        noComments.reserve(body.size());
        size_t lineStart = 0;
        while (lineStart <= body.size()) {
            size_t nl = body.find('\n', lineStart);
            const std::string line =
                body.substr(lineStart, nl == std::string::npos ? std::string::npos : nl - lineStart);
            size_t cut = std::string::npos;
            for (size_t i = 0; i < line.size(); ++i) {
                if (line[i] == '%' && (i == 0 || line[i - 1] != '\\')) { cut = i; break; }
            }
            noComments += (cut == std::string::npos) ? line : line.substr(0, cut);
            noComments += '\n';
            if (nl == std::string::npos) break;
            lineStart = nl + 1;
        }

        std::string math = Trim(noComments);

        // Strip a single pair of outer math delimiters if present; the engine is
        // always in math mode and does not want them.
        const std::pair<std::string, std::string> delims[] = {
            {"\\[", "\\]"}, {"$$", "$$"}, {"\\(", "\\)"}, {"$", "$"},
        };
        for (const auto& d : delims) {
            if (math.size() >= d.first.size() + d.second.size() &&
                math.compare(0, d.first.size(), d.first) == 0 &&
                math.compare(math.size() - d.second.size(), d.second.size(), d.second) == 0) {
                math = Trim(math.substr(d.first.size(),
                                        math.size() - d.first.size() - d.second.size()));
                break;
            }
        }
        return math;
    }

    // Reference-image fallback for documents the math engine cannot render
    // (TikZ / pgfplots) — clearly labelled as a reference render, not a live one.
    std::shared_ptr<UltraCanvasUIElement> CreateReferenceImage(
        const std::filesystem::path& texPath, const std::string& stem) {

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
            return image;
        }

        auto missing = std::make_shared<UltraCanvasLabel>("LaTeXNoImage_" + stem, 0, 0, 0, 0);
        missing->SetText("This document uses TikZ / pgfplots graphics, which the built-in\n"
                         "math engine cannot render, and no reference image (" + stem +
                         ".png / .gif) was found beside the source.");
        missing->SetFontSize(12);
        missing->SetTextColor(Color(150, 60, 60, 255));
        missing->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
        missing->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        return missing;
    }

    // One tab page: the rendered result above the LaTeX source itself. Math
    // documents are typeset live by the engine; TikZ documents fall back to a
    // labelled reference image.
    std::shared_ptr<UltraCanvasUIElement> CreateLaTeXTabPage(const std::filesystem::path& texPath) {
        const std::string stem = texPath.stem().string();
        const std::string source = LoadFile(texPath.string());

        auto page = std::make_shared<UltraCanvasContainer>("LaTeXPage_" + stem, 0, 0, 800, 600);
        page->layout.SetFlexColumn().SetFlexGap(6)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        page->SetPadding(8, 10, 8, 10);
        page->SetBackgroundColor(Colors::White);

        // Decide whether the engine can render this document live.
        const bool mathDoc = IsMathRenderable(source);
        std::shared_ptr<UltraCanvasLaTeXView> liveView;
        if (mathDoc) {
            const std::string math = ExtractRenderableMath(source);
            if (!math.empty()) {
                liveView = CreateLaTeXView("LaTeXView_" + stem, 0, 0, 0, 0);
                if (liveView) {
                    liveView->SetTextSize(26.0f);
                    liveView->SetTextColor(Colors::Black);
                    liveView->SetLaTeX(math);
                }
            }
        }
        const bool live = static_cast<bool>(liveView);

        // ----- Rendered output header -----
        auto renderedLabel = std::make_shared<UltraCanvasLabel>("LaTeXRenderedLabel_" + stem, 0, 0, 0, 20);
        renderedLabel->SetText(live ? "Rendered output (typeset live by the UltraCanvas LaTeX engine):"
                                    : "Reference image (this document is beyond the built-in math engine):");
        renderedLabel->SetFontSize(12);
        renderedLabel->SetFontWeight(FontWeight::Bold);
        renderedLabel->SetTextColor(live ? Color(40, 110, 40, 255) : Color(150, 90, 40, 255));
        renderedLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        page->AddChild(renderedLabel);

        // ----- Rendered output body -----
        // A centred, growing area that holds either the live view or the image.
        auto renderArea = std::make_shared<UltraCanvasContainer>("LaTeXRenderArea_" + stem, 0, 0, 0, 0);
        renderArea->layout.SetFlexRow()
                          .SetFlexJustifyContent(CSSLayout::JustifyContent::Center)
                          .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        renderArea->SetBackgroundColor(Colors::White);
        renderArea->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                              .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        if (live) {
            liveView->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                                .SetAlignSelf(CSSLayout::AlignSelf::Center);
            renderArea->AddChild(liveView);
        } else {
            renderArea->AddChild(CreateReferenceImage(texPath, stem));
        }
        page->AddChild(renderArea);

        // ----- LaTeX source -----
        auto sourceLabel = std::make_shared<UltraCanvasLabel>("LaTeXSourceLabel_" + stem, 0, 0, 0, 20);
        sourceLabel->SetText("LaTeX source (" + texPath.filename().string() + "):");
        sourceLabel->SetFontSize(12);
        sourceLabel->SetFontWeight(FontWeight::Bold);
        sourceLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        page->AddChild(sourceLabel);

        auto sourceView = std::make_shared<UltraCanvasTextArea>("LaTeXSource_" + stem);
        sourceView->SetText(source);
        sourceView->SetReadOnly(true);
        sourceView->SetShowLineNumbers(true);
        sourceView->SetWordWrap(false);
        sourceView->SetCursorPosition(LineColumnIndex::INVALID);
        sourceView->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                              .SetFlexBasis(CSSLayout::Dimension::Pct(40))
                              .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        page->AddChild(sourceView);

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

        auto info = std::make_shared<UltraCanvasLabel>("LaTeXInfo", 0, 0, 0, 32);
        info->SetText("Example .tex documents scanned from " + latexDir +
                      ".\nEach document is typeset live from its source by the on-demand "
                      "UltraCanvas LaTeX engine.");
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
            std::string ext = ToLowerCopy(entry.path().extension().string());
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

        int firstLiveTab = -1;
        for (size_t i = 0; i < texFiles.size(); ++i) {
            const auto& texPath = texFiles[i];
            if (firstLiveTab < 0 && IsMathRenderable(LoadFile(texPath.string()))) {
                firstLiveTab = static_cast<int>(i);
            }
            tabs->AddTab(texPath.stem().string(), CreateLaTeXTabPage(texPath));
        }
        // Open on a live-rendered document so the engine's own output is the
        // first thing the user sees.
        tabs->SetActiveTab(firstLiveTab >= 0 ? firstLiveTab : 0);
        root->AddChild(tabs);

        return root;
    }

} // namespace UltraCanvas
