// include/UltraCanvasEBookViewer.h
// eBook reading widget.
//
// Composite UltraCanvasContainer laid out by the CSSLayout engine:
//
//   ┌─────────────────────────────────────────────┐
//   │ toolbar: ◀ ▶ A- A+ ☾ ☰  chapter title      │  flex row, fixed height
//   ├──────────╥──────────────────────────────────┤
//   │ TOC list ║ chapter content (scrollable)     │  split pane, grows
//   │ (toggle) ║ built by HTML::ElementBuilder    │  (draggable splitter)
//   └──────────╨──────────────────────────────────┘
//
// Chapters come from an IEBookEngine (EPUB, TXT, ... via the engine
// registry); the chapter XHTML is converted to native elements, so text is
// real Labels laid out by CSSLayout — no page images.
// Version: 2.0.0
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasSplitPane.h"

#include "Documents/eBook/IEBookEngine.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace UltraCanvas {

// Reading color themes (author colors are overridden outside Normal mode).
enum class EBookViewerTheme {
    Normal,
    Sepia,
    Night
};

class UltraCanvasEBookViewer : public UltraCanvasContainer {
public:
    explicit UltraCanvasEBookViewer(const std::string& id,
                                    float w = 0, float h = 0);

    // ===== DOCUMENT =====
    // Uses the engine registry (call RegisterBuiltinEBookEngines() once at
    // startup). Reports failures through the return value / GetLastError /
    // onError.
    bool LoadDocument(const std::string& filePath);
    bool LoadDocumentFromMemory(std::vector<uint8_t> data,
                                const std::string& nameHint);
    void CloseDocument();
    bool IsDocumentLoaded() const { return engine && engine->IsLoaded(); }
    std::string GetLastError() const { return lastError; }

    const EBookMetadata* GetMetadata() const {
        return IsDocumentLoaded() ? &engine->GetMetadata() : nullptr;
    }
    IEBookEngine* GetEngine() const { return engine.get(); }

    // ===== NAVIGATION =====
    int GetChapterCount() const {
        return IsDocumentLoaded() ? engine->GetChapterCount() : 0;
    }
    int GetCurrentChapter() const { return currentChapter; }
    void GoToChapter(int index);
    void NextChapter() { GoToChapter(currentChapter + 1); }
    void PreviousChapter() { GoToChapter(currentChapter - 1); }

    // ===== APPEARANCE =====
    void SetTheme(EBookViewerTheme theme);
    EBookViewerTheme GetTheme() const { return theme; }

    void SetBaseFontSize(float px);
    float GetBaseFontSize() const { return baseFontSizePx; }
    void IncreaseFontSize() { SetBaseFontSize(baseFontSizePx + 2.f); }
    void DecreaseFontSize() { SetBaseFontSize(baseFontSizePx - 2.f); }

    void SetContentFontFamily(const std::string& family);

    // ===== PANELS =====
    void ShowTableOfContents(bool show);
    void ToggleTableOfContents() { ShowTableOfContents(!tocVisible); }
    bool IsTableOfContentsVisible() const { return tocVisible; }
    void ShowToolbar(bool show);

    // ===== SEARCH =====
    std::vector<EBookSearchResult> Search(const std::string& query,
                                          bool caseSensitive = false) const {
        return IsDocumentLoaded() ? engine->Search(query, caseSensitive)
                                  : std::vector<EBookSearchResult>{};
    }

    // ===== EVENTS =====
    bool OnEvent(const UCEvent& event) override;   // keyboard navigation

    // Applies a pending #fragment scroll once the freshly built chapter
    // content has been laid out (anchor positions are only valid then).
    void Arrange(const Rect2Df& finalRect,
                 const CSSLayout::LayoutContext& ctx) override;

    // ===== CALLBACKS =====
    std::function<void(const EBookMetadata&)> onDocumentLoaded;
    std::function<void(int chapterIndex, const std::string& title)> onChapterChanged;
    std::function<void(const std::string& error)> onError;
    std::function<void(float progress, const std::string& status)> onLoadProgress;
    // Content link with an external scheme (http:, https:, mailto:, ...) was
    // clicked; the viewer only follows links inside the book itself.
    std::function<void(const std::string& url)> onExternalLink;

private:
    // Engine / state
    std::shared_ptr<IEBookEngine> engine;
    std::string lastError;
    int currentChapter = -1;
    // Overridden in the constructor with the platform's UI font size, so the
    // book text starts at the same size as the application interface.
    float baseFontSizePx = 12.f;
    std::string contentFontFamily;
    EBookViewerTheme theme = EBookViewerTheme::Normal;
    bool tocVisible = true;

    // UI parts
    std::shared_ptr<UltraCanvasContainer> toolbar;
    std::shared_ptr<UltraCanvasButton> btnPrev;
    std::shared_ptr<UltraCanvasButton> btnNext;
    std::shared_ptr<UltraCanvasLabel> chapterLabel;
    std::shared_ptr<UltraCanvasButton> btnFontMinus;
    std::shared_ptr<UltraCanvasButton> btnFontPlus;
    std::shared_ptr<UltraCanvasButton> btnTheme;
    std::shared_ptr<UltraCanvasButton> btnToc;
    // TOC | content, separated by a draggable splitter. The TOC pane is
    // inserted/removed on toggle; tocList itself is retained across toggles.
    std::shared_ptr<UltraCanvasSplitPane> bodySplit;
    std::shared_ptr<UltraCanvasContainer> tocPane;
    std::shared_ptr<UltraCanvasContainer> contentPane;
    std::shared_ptr<UltraCanvasListView> tocList;
    std::shared_ptr<UltraCanvasSimpleListModel> tocModel;
    std::shared_ptr<UltraCanvasContainer> contentScroll;
    std::vector<int> tocRowToChapter;   // flattened TOC row → chapter index
    int tocWidthPx = 300;               // TOC pane width, remembered across toggles

    // #fragment targets of the current chapter (id → built element), and the
    // anchor to scroll to after the next layout pass (set when a link
    // navigates to a different chapter, whose content isn't laid out yet).
    std::unordered_map<std::string, std::shared_ptr<UltraCanvasUIElement>> chapterAnchors;
    std::string pendingAnchorId;

    void BuildUI();
    void AttachTocPane();
    void DetachTocPane();
    void RebuildChapterContent();
    void OpenLink(const std::string& baseHref, const std::string& href);
    bool ScrollToAnchor(const std::string& anchorId);
    void PopulateTOC();
    void RefreshToolbarState();
    void ApplyThemeColors();
    bool FinishLoad();

    struct ThemeColors;
    ThemeColors CurrentThemeColors() const;
};

// ===== FACTORY =====
std::shared_ptr<UltraCanvasEBookViewer> CreateEBookViewer(
    const std::string& id, float w = 0, float h = 0);

} // namespace UltraCanvas
