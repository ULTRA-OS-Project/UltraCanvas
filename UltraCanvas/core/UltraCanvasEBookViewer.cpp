// core/UltraCanvasEBookViewer.cpp
// eBook reading widget: engine chapters → HTML::ElementBuilder → CSSLayout.
// Version: 2.0.0
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework

#include "UltraCanvasEBookViewer.h"

#include "HTMLReader/HTMLElementBuilder.h"

#include <algorithm>

namespace UltraCanvas {

// ============================================================================
// THEMES
// ============================================================================

struct UltraCanvasEBookViewer::ThemeColors {
    HTML::CssColor pageBackground;
    HTML::CssColor text;
    HTML::CssColor link;
    Color chromeBackground;    // toolbar / TOC panel
    Color chromeText;
    bool overrideAuthorColors;
};

UltraCanvasEBookViewer::ThemeColors
UltraCanvasEBookViewer::CurrentThemeColors() const {
    switch (theme) {
        case EBookViewerTheme::Sepia:
            return {{251, 241, 219, 255},          // warm paper
                    {90, 70, 50, 255},             // brown text
                    {120, 80, 40, 255},
                    Color(240, 228, 204, 255),
                    Color(90, 70, 50, 255),
                    true};
        case EBookViewerTheme::Night:
            return {{30, 30, 30, 255},
                    {201, 201, 201, 255},
                    {120, 160, 255, 255},
                    Color(45, 45, 45, 255),
                    Color(200, 200, 200, 255),
                    true};
        case EBookViewerTheme::Normal:
        default:
            return {{255, 255, 255, 255},
                    {20, 20, 20, 255},
                    {0, 0, 238, 255},
                    Color(245, 245, 245, 255),
                    Color(40, 40, 40, 255),
                    false};
    }
}

// ============================================================================
// CONSTRUCTION / UI
// ============================================================================

UltraCanvasEBookViewer::UltraCanvasEBookViewer(const std::string& id,
                                               float w, float h)
    : UltraCanvasContainer(id, w, h) {
    BuildUI();
    ApplyThemeColors();
    RefreshToolbarState();
}

void UltraCanvasEBookViewer::BuildUI() {
    layout.SetFlex(CSSLayout::FlexDirection::Column);

    // ---- toolbar ----
    toolbar = std::make_shared<UltraCanvasContainer>(id + "_toolbar");
    toolbar->size.height = CSSLayout::Dimension::Px(40);
    toolbar->layout.SetFlex(CSSLayout::FlexDirection::Row);
    toolbar->layout.SetFlexGap(4.f);
    toolbar->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
    toolbar->box.padding.left = CSSLayout::Dimension::Px(6);
    toolbar->box.padding.right = CSSLayout::Dimension::Px(6);

    auto makeButton = [&](const char* name, const char* text) {
        auto button = std::make_shared<UltraCanvasButton>(id + name, text);
        button->size.width = CSSLayout::Dimension::Px(34);
        button->size.height = CSSLayout::Dimension::Px(28);
        toolbar->AddChild(button);
        return button;
    };

    btnPrev = makeButton("_prev", "\xE2\x97\x80");         // ◀
    btnNext = makeButton("_next", "\xE2\x96\xB6");         // ▶

    chapterLabel = std::make_shared<UltraCanvasLabel>(id + "_chapter", "");
    chapterLabel->layoutItem.SetFlexGrow(1.f);
    chapterLabel->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
    toolbar->AddChild(chapterLabel);

    btnFontMinus = makeButton("_fminus", "A-");
    btnFontPlus = makeButton("_fplus", "A+");
    btnTheme = makeButton("_theme", "\xE2\x98\xBE");       // ☾
    btnToc = makeButton("_toc", "\xE2\x98\xB0");           // ☰

    AddChild(toolbar);

    // ---- body: TOC | content ----
    bodyRow = std::make_shared<UltraCanvasContainer>(id + "_body");
    bodyRow->layoutItem.SetFlexGrow(1.f);
    bodyRow->layout.SetFlex(CSSLayout::FlexDirection::Row);

    tocList = std::make_shared<UltraCanvasListView>(id + "_toclist");
    tocList->size.width = CSSLayout::Dimension::Px(240);
    tocList->SetVisible(false);
    bodyRow->AddChild(tocList);

    contentScroll = std::make_shared<UltraCanvasContainer>(id + "_content");
    contentScroll->layoutItem.SetFlexGrow(1.f);
    bodyRow->AddChild(contentScroll);

    AddChild(bodyRow);

    // ---- wiring ----
    btnPrev->SetOnClick([this]() { PreviousChapter(); });
    btnNext->SetOnClick([this]() { NextChapter(); });
    btnFontMinus->SetOnClick([this]() { DecreaseFontSize(); });
    btnFontPlus->SetOnClick([this]() { IncreaseFontSize(); });
    btnTheme->SetOnClick([this]() {
        switch (theme) {
            case EBookViewerTheme::Normal: SetTheme(EBookViewerTheme::Sepia); break;
            case EBookViewerTheme::Sepia:  SetTheme(EBookViewerTheme::Night); break;
            case EBookViewerTheme::Night:  SetTheme(EBookViewerTheme::Normal); break;
        }
    });
    btnToc->SetOnClick([this]() { ToggleTableOfContents(); });

    tocList->onItemClicked = [this](int row) {
        if (row >= 0 && row < static_cast<int>(tocRowToChapter.size())) {
            int chapter = tocRowToChapter[static_cast<size_t>(row)];
            if (chapter >= 0) GoToChapter(chapter);
        }
    };
}

// ============================================================================
// DOCUMENT
// ============================================================================

bool UltraCanvasEBookViewer::LoadDocument(const std::string& filePath) {
    CloseDocument();

    engine = CreateEBookEngineForFile(filePath);
    if (!engine) {
        lastError = "Unsupported eBook format: " + filePath;
        if (onError) onError(lastError);
        return false;
    }
    engine->onProgress = [this](float progress, const std::string& status) {
        if (onLoadProgress) onLoadProgress(progress, status);
    };

    if (!engine->LoadFromFile(filePath)) {
        lastError = engine->GetLastError();
        engine.reset();
        if (onError) onError(lastError);
        RebuildChapterContent();
        return false;
    }
    return FinishLoad();
}

bool UltraCanvasEBookViewer::LoadDocumentFromMemory(std::vector<uint8_t> data,
                                                    const std::string& nameHint) {
    CloseDocument();

    engine = CreateEBookEngineForFile(nameHint);
    if (!engine) {
        lastError = "Unsupported eBook format: " + nameHint;
        if (onError) onError(lastError);
        return false;
    }
    engine->onProgress = [this](float progress, const std::string& status) {
        if (onLoadProgress) onLoadProgress(progress, status);
    };

    if (!engine->LoadFromMemory(std::move(data))) {
        lastError = engine->GetLastError();
        engine.reset();
        if (onError) onError(lastError);
        RebuildChapterContent();
        return false;
    }
    return FinishLoad();
}

bool UltraCanvasEBookViewer::FinishLoad() {
    lastError.clear();
    currentChapter = -1;
    PopulateTOC();
    if (onDocumentLoaded) onDocumentLoaded(engine->GetMetadata());
    GoToChapter(0);
    return true;
}

void UltraCanvasEBookViewer::CloseDocument() {
    if (engine) {
        engine->Close();
        engine.reset();
    }
    currentChapter = -1;
    lastError.clear();
    tocRowToChapter.clear();
    if (tocModel) tocModel->Clear();
    RebuildChapterContent();
    RefreshToolbarState();
}

// ============================================================================
// NAVIGATION
// ============================================================================

void UltraCanvasEBookViewer::GoToChapter(int index) {
    if (!IsDocumentLoaded()) return;
    index = std::clamp(index, 0, engine->GetChapterCount() - 1);
    if (index == currentChapter) return;

    currentChapter = index;
    RebuildChapterContent();
    RefreshToolbarState();

    if (onChapterChanged) {
        onChapterChanged(currentChapter,
                         engine->GetChapter(currentChapter).title);
    }
}

// ============================================================================
// APPEARANCE / PANELS
// ============================================================================

void UltraCanvasEBookViewer::SetTheme(EBookViewerTheme newTheme) {
    if (theme == newTheme) return;
    theme = newTheme;
    ApplyThemeColors();
    RebuildChapterContent();
}

void UltraCanvasEBookViewer::SetBaseFontSize(float px) {
    px = std::clamp(px, 8.f, 48.f);
    if (px == baseFontSizePx) return;
    baseFontSizePx = px;
    RebuildChapterContent();
}

void UltraCanvasEBookViewer::SetContentFontFamily(const std::string& family) {
    if (contentFontFamily == family) return;
    contentFontFamily = family;
    RebuildChapterContent();
}

void UltraCanvasEBookViewer::ShowTableOfContents(bool show) {
    if (tocVisible == show) return;
    tocVisible = show;
    if (tocList) tocList->SetVisible(show);
    InvalidateLayout();
    RequestRedraw();
}

void UltraCanvasEBookViewer::ShowToolbar(bool show) {
    if (toolbar) toolbar->SetVisible(show);
    InvalidateLayout();
    RequestRedraw();
}

void UltraCanvasEBookViewer::ApplyThemeColors() {
    ThemeColors colors = CurrentThemeColors();
    SetBackgroundColor(colors.chromeBackground);
    if (toolbar) toolbar->SetBackgroundColor(colors.chromeBackground);
    if (contentScroll) {
        contentScroll->SetBackgroundColor(Color(colors.pageBackground.r,
                                                colors.pageBackground.g,
                                                colors.pageBackground.b,
                                                colors.pageBackground.a));
    }
    if (chapterLabel) chapterLabel->SetTextColor(colors.chromeText);
}

// ============================================================================
// CONTENT
// ============================================================================

void UltraCanvasEBookViewer::RebuildChapterContent() {
    if (!contentScroll) return;
    contentScroll->ClearChildren();

    if (!IsDocumentLoaded() || currentChapter < 0) {
        auto placeholder = std::make_shared<UltraCanvasLabel>(
            id + "_empty",
            lastError.empty() ? "No document loaded" : lastError);
        placeholder->box.margin.top = CSSLayout::Dimension::Px(32);
        placeholder->box.margin.left = CSSLayout::Dimension::Px(32);
        placeholder->SetTextColor(Color(128, 128, 128, 255));
        contentScroll->AddChild(placeholder);
        contentScroll->ScrollToVertical(0);
        RequestRedraw();
        return;
    }

    EBookChapter chapter = engine->GetChapter(currentChapter);
    ThemeColors colors = CurrentThemeColors();

    HTML::BuildOptions options;
    options.style.baseFontSizePx = baseFontSizePx;
    options.style.baseFontFamily = contentFontFamily;
    options.style.textColor = colors.text;
    options.style.linkColor = colors.link;
    options.style.overrideAuthorColors = colors.overrideAuthorColors;
    options.resourceLoader = [this, chapter](const std::string& href) {
        return engine->GetResource(ResolveEBookHref(chapter.href, href));
    };

    HTML::ElementBuilder builder;
    HTML::BuildResult result = builder.Build(chapter.content, options);
    if (!result.root) {
        lastError = "Failed to build chapter content";
        if (onError) onError(lastError);
        return;
    }

    // Reading margins around the page content.
    result.root->box.padding.top = CSSLayout::Dimension::Px(24);
    result.root->box.padding.bottom = CSSLayout::Dimension::Px(48);
    result.root->box.padding.left = CSSLayout::Dimension::Px(28);
    result.root->box.padding.right = CSSLayout::Dimension::Px(28);

    contentScroll->AddChild(result.root);
    contentScroll->ScrollToVertical(0);
    RequestRedraw();
}

void UltraCanvasEBookViewer::PopulateTOC() {
    tocRowToChapter.clear();
    tocModel = std::make_shared<UltraCanvasSimpleListModel>();

    if (IsDocumentLoaded()) {
        std::function<void(const std::vector<EBookTOCEntry>&, int)> add =
            [&](const std::vector<EBookTOCEntry>& entries, int depth) {
                for (const auto& entry : entries) {
                    std::string label(static_cast<size_t>(depth) * 2, ' ');
                    label += entry.title.empty() ? "(untitled)" : entry.title;
                    tocModel->AddItem(label);
                    tocRowToChapter.push_back(entry.pageNumber);
                    add(entry.children, depth + 1);
                }
            };
        add(engine->GetTableOfContents(), 0);
    }

    if (tocList) tocList->SetModel(tocModel);
}

void UltraCanvasEBookViewer::RefreshToolbarState() {
    bool hasDoc = IsDocumentLoaded();
    int count = hasDoc ? engine->GetChapterCount() : 0;

    if (btnPrev) btnPrev->SetDisabled(!hasDoc || currentChapter <= 0);
    if (btnNext) btnNext->SetDisabled(!hasDoc || currentChapter >= count - 1);
    if (btnFontMinus) btnFontMinus->SetDisabled(!hasDoc);
    if (btnFontPlus) btnFontPlus->SetDisabled(!hasDoc);

    if (chapterLabel) {
        if (hasDoc && currentChapter >= 0) {
            std::string title = engine->GetChapter(currentChapter).title;
            if (title.empty()) title = engine->GetMetadata().title;
            chapterLabel->SetText(title + "  (" +
                                  std::to_string(currentChapter + 1) + "/" +
                                  std::to_string(count) + ")");
        } else {
            chapterLabel->SetText("");
        }
    }
}

// ============================================================================
// EVENTS
// ============================================================================

bool UltraCanvasEBookViewer::OnEvent(const UCEvent& event) {
    if (event.type == UCEventType::KeyDown && IsDocumentLoaded()) {
        switch (event.virtualKey) {
            case UCKeys::Right:
            case UCKeys::PageDown:
                NextChapter();
                return true;
            case UCKeys::Left:
            case UCKeys::PageUp:
                PreviousChapter();
                return true;
            case UCKeys::Home:
                GoToChapter(0);
                return true;
            case UCKeys::End:
                GoToChapter(GetChapterCount() - 1);
                return true;
            default:
                break;
        }
    }
    return UltraCanvasContainer::OnEvent(event);
}

// ============================================================================
// FACTORY
// ============================================================================

std::shared_ptr<UltraCanvasEBookViewer> CreateEBookViewer(
    const std::string& id, float w, float h) {
    return std::make_shared<UltraCanvasEBookViewer>(id, w, h);
}

} // namespace UltraCanvas
