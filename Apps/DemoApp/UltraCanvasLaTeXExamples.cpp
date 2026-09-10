// Apps/DemoApp/UltraCanvasLaTeXExamples.cpp
// LaTeX documents demo: scans media/LaTex for .tex files at runtime and shows
// each one in a vertical (left-side) tab — the rendered output on top and the
// LaTeX source below.
//
// Every file is rendered *live* from its source, on one of two paths:
//   * a formula-only document (a single \[ ... \] / equation body) is typeset
//     by the on-demand LaTeX module's math engine in a UltraCanvasLaTeXView;
//   * an article-style document (sections, lists, tables, figures, ...) is
//     imported by UltraCanvasLaTeXDocumentReader into the rich-document
//     model and shown as rendered Markdown in a read-only TextArea, where its
//     formulas are typeset inline by the same engine.
// Only TikZ / pgfplots pictures are outside both paths; such a document
// falls back to a reference .png/.gif sitting beside the source (labelled
// honestly as a reference render) if one exists.
//
// New examples appear automatically when a .tex file is dropped into the
// media/LaTex folder.
// Version: 3.0.0
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasImageElement.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasConfig.h"   // GetResourcesDir
#include "UltraCanvasUtils.h"    // NormalizePath, LoadFile, Trim
#include "Plugins/LaTeX/UltraCanvasLaTeXView.h"  // CreateLaTeXView (on-demand)
#include "Plugins/Documents/LaTeX/UltraCanvasLaTeXDocumentReader.h"
#include "Plugins/Documents/Word/UltraCanvasRichDocument.h"

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

    // TikZ / pgfplots pictures are beyond both the math engine and the
    // document reader (proposal Phase 4): such a file shows its reference
    // image.
    bool UsesTikZ(const std::string& source) {
        static const char* kMarkers[] = {
            "tikzpicture", "pgfplot", "\\begin{axis}", "tikzset",
            "usetikzlibrary", "\\tikz", "pgfmath", "\\draw", "\\node",
        };
        const std::string lower = ToLowerCopy(source);
        for (const char* marker : kMarkers) {
            if (lower.find(marker) != std::string::npos) return true;
        }
        return false;
    }

    // Pull the body out of a full .tex document: the content between
    // \begin{document} and \end{document}, stripped of comments.
    std::string ExtractBody(const std::string& source) {
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
        return Trim(noComments);
    }

    // A formula-only document is a body that consists of exactly one display
    // formula; everything else is an article for the document reader. The
    // returned string is the formula without its outer delimiters (empty
    // when the document is not formula-only).
    std::string ExtractSingleFormula(const std::string& source) {
        const std::string body = ExtractBody(source);
        const std::pair<std::string, std::string> delims[] = {
            {"\\[", "\\]"}, {"$$", "$$"}, {"\\(", "\\)"}, {"$", "$"},
            {"\\begin{equation*}", "\\end{equation*}"}, {"\\begin{equation}", "\\end{equation}"},
            {"\\begin{displaymath}", "\\end{displaymath}"},
        };
        for (const auto& d : delims) {
            if (body.size() >= d.first.size() + d.second.size() &&
                body.compare(0, d.first.size(), d.first) == 0 &&
                body.compare(body.size() - d.second.size(), d.second.size(), d.second) == 0) {
                std::string inner = Trim(body.substr(d.first.size(),
                                                     body.size() - d.first.size() - d.second.size()));
                // A second opener inside means prose between formulas: a document.
                if (d.first != "$" && inner.find(d.first) != std::string::npos) return std::string();
                return inner;
            }
        }
        // The alignment environments are formulas too when they are alone.
        static const char* kMathEnvs[] = {"align", "align*", "gather", "gather*",
                                          "multline", "multline*", "eqnarray", "eqnarray*"};
        for (const char* env : kMathEnvs) {
            const std::string open = std::string("\\begin{") + env + "}";
            const std::string close = std::string("\\end{") + env + "}";
            if (body.size() >= open.size() + close.size() &&
                body.compare(0, open.size(), open) == 0 &&
                body.compare(body.size() - close.size(), close.size(), close) == 0) {
                return body;
            }
        }
        return std::string();
    }

    // Reference-image fallback for TikZ documents — clearly labelled as a
    // reference render, not a live one.
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
        missing->SetText("This document uses TikZ / pgfplots graphics, which are outside the\n"
                         "built-in LaTeX support, and no reference image (" + stem +
                         ".png / .gif) was found beside the source.");
        missing->SetFontSize(12);
        missing->SetTextColor(Color(150, 60, 60, 255));
        missing->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
        missing->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        return missing;
    }

    // Article-style documents: the reader's rich document, serialized to
    // Markdown and rendered by the TextArea (formulas typeset inline).
    std::shared_ptr<UltraCanvasUIElement> CreateDocumentView(
        const std::filesystem::path& texPath, const std::string& stem, std::string& outStatus) {

        UCRichDocument document;
        std::string error;
        std::vector<LaTeXDocumentDiagnostic> diagnostics;
        if (!UltraCanvasLaTeXDocumentReader::Load(texPath.string(), document, error, &diagnostics)) {
            auto failed = std::make_shared<UltraCanvasLabel>("LaTeXDocError_" + stem, 0, 0, 0, 0);
            failed->SetText("The document could not be imported:\n" + error);
            failed->SetFontSize(12);
            failed->SetTextColor(Color(150, 60, 60, 255));
            failed->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
            failed->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                              .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
            outStatus = "Imported document (failed)";
            return failed;
        }

        RichDocumentMarkdownOptions options;
        if (!document.media.empty()) {
            static int loadCounter = 0;
            options.imageDirectory = (std::filesystem::temp_directory_path()
                / ("UltraCanvasLaTeXDemo-" + std::to_string(++loadCounter))).string();
        }

        auto view = std::make_shared<UltraCanvasTextArea>("LaTeXDoc_" + stem);
        view->SetDocumentFilePath(texPath.string());
        view->SetText(document.ToMarkdown(options), false);
        view->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
        view->SetReadOnly(true);
        view->SetCursorPosition(LineColumnIndex::INVALID);
        view->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        outStatus = "Imported document (" + std::to_string(document.blocks.size()) + " blocks";
        if (!document.media.empty()) outStatus += ", " + std::to_string(document.media.size()) + " images";
        if (!diagnostics.empty()) {
            outStatus += ", " + std::to_string(diagnostics.size()) + " diagnostic" +
                         (diagnostics.size() == 1 ? "" : "s") + ": " + diagnostics.front().message;
        }
        outStatus += "):";
        return view;
    }

    // One tab page: the rendered result above the LaTeX source itself.
    std::shared_ptr<UltraCanvasUIElement> CreateLaTeXTabPage(const std::filesystem::path& texPath) {
        const std::string stem = texPath.stem().string();
        const std::string source = LoadFile(texPath.string());

        auto page = std::make_shared<UltraCanvasContainer>("LaTeXPage_" + stem, 0, 0, 800, 600);
        page->layout.SetFlexColumn().SetFlexGap(6)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        page->SetPadding(8, 10, 8, 10);
        page->SetBackgroundColor(Colors::White);

        // Decide the rendering path.
        const bool tikz = UsesTikZ(source);
        const std::string formula = tikz ? std::string() : ExtractSingleFormula(source);
        const bool formulaDoc = !tikz && !formula.empty();
        std::shared_ptr<UltraCanvasLaTeXView> liveView;
        if (formulaDoc) {
            liveView = CreateLaTeXView("LaTeXView_" + stem, 0, 0, 0, 0);
            if (liveView) {
                liveView->SetTextSize(26.0f);
                liveView->SetTextColor(Colors::Black);
                liveView->SetLaTeX(formula);
            }
        }
        const bool live = static_cast<bool>(liveView);

        std::string headerText;
        Color headerColor(40, 110, 40, 255);
        std::shared_ptr<UltraCanvasUIElement> body;
        if (live) {
            headerText = "Rendered output (typeset live by the UltraCanvas LaTeX engine):";
            liveView->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                                .SetAlignSelf(CSSLayout::AlignSelf::Center);
            body = liveView;
        } else if (formulaDoc) {
            // Plain math, so the only reason there is no live view is that the
            // on-demand module could not be loaded. Say so - a silent blank
            // pane hides a deployment problem: the module or its math font
            // not being found next to the executable.
            headerText = "Rendered output (LaTeX engine unavailable):";
            headerColor = Color(150, 60, 60, 255);
            auto failed = std::make_shared<UltraCanvasLabel>("LaTeXModuleError_" + stem, 0, 0, 0, 0);
            failed->SetText("The LaTeX engine module could not be loaded, so this document "
                            "cannot be typeset:\n" + GetLaTeXModuleError());
            failed->SetFontSize(12);
            failed->SetTextColor(Color(150, 60, 60, 255));
            failed->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
            failed->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                              .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
            body = failed;
        } else if (!tikz) {
            body = CreateDocumentView(texPath, stem, headerText);
            headerColor = Color(40, 80, 140, 255);
        } else {
            headerText = "Reference image (this document uses TikZ / pgfplots pictures):";
            headerColor = Color(150, 90, 40, 255);
            body = CreateReferenceImage(texPath, stem);
        }

        // ----- Rendered output header -----
        auto renderedLabel = std::make_shared<UltraCanvasLabel>("LaTeXRenderedLabel_" + stem, 0, 0, 0, 20);
        renderedLabel->SetText(headerText);
        renderedLabel->SetFontSize(12);
        renderedLabel->SetFontWeight(FontWeight::Bold);
        renderedLabel->SetTextColor(headerColor);
        renderedLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        page->AddChild(renderedLabel);

        // ----- Rendered output body -----
        // A centred, growing area that holds the live view, the document view
        // or the image.
        auto renderArea = std::make_shared<UltraCanvasContainer>("LaTeXRenderArea_" + stem, 0, 0, 0, 0);
        renderArea->layout.SetFlexRow()
                          .SetFlexJustifyContent(CSSLayout::JustifyContent::Center)
                          .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        renderArea->SetBackgroundColor(Colors::White);
        renderArea->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                              .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        renderArea->AddChild(body);
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
                      ".\nEach document is rendered live from its source: formulas by the "
                      "UltraCanvas math engine, articles by the LaTeX document reader.");
        info->SetFontSize(11);
        info->SetTextColor(Color(110, 110, 110, 255));
        info->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        root->AddChild(info);

        auto note = std::make_shared<UltraCanvasLabel>("LaTeXScopeNote", 0, 0, 0, 18);
        note->SetText("Scope: math (amsmath) and the article document subset; "
                      "TikZ / pgfplots pictures fall back to a reference image beside the source.");
        note->SetFontSize(11);
        note->SetTextColor(Color(90, 90, 90, 255));
        note->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        root->AddChild(note);

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
        // Order the tabs: article documents first, then the MicroTeX gallery
        // examples ("microtex-*"), then the rest; each group alphabetical.
        auto rank = [](const std::filesystem::path& p) {
            const std::string stem = p.stem().string();
            if (stem.rfind("article-", 0) == 0) return 0;
            if (stem.rfind("microtex-", 0) == 0) return 1;
            return 2;
        };
        std::sort(texFiles.begin(), texFiles.end(),
                  [&](const std::filesystem::path& a, const std::filesystem::path& b) {
                      const int ra = rank(a), rb = rank(b);
                      if (ra != rb) return ra < rb;
                      return a < b;                       // then alphabetical
                  });

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
        // The example set is large — the tab column scrolls when it does not fit
        // the window height. The overflow dropdown is intentionally not enabled:
        // it is not supported for vertical (Left/Right) tab layouts.
        tabs->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        int firstLiveTab = -1;
        for (size_t i = 0; i < texFiles.size(); ++i) {
            const auto& texPath = texFiles[i];
            if (firstLiveTab < 0 && !UsesTikZ(LoadFile(texPath.string()))) {
                firstLiveTab = static_cast<int>(i);
            }
            tabs->AddTab(texPath.stem().string(), CreateLaTeXTabPage(texPath));
        }
        // Open on a live-rendered document so the framework's own output is
        // the first thing the user sees.
        tabs->SetActiveTab(firstLiveTab >= 0 ? firstLiveTab : 0);
        root->AddChild(tabs);

        return root;
    }

} // namespace UltraCanvas
