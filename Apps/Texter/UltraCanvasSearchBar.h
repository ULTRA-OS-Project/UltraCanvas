// Apps/Texter/UltraCanvasSearchBar.h
// Inline search and replace bar — embedded panel in the editor (VS Code style)
// Version: 1.0.0
// Last Modified: 2026-03-14
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasImageElement.h"
#include <string>
#include <functional>
#include <vector>

namespace UltraCanvas {

// ===== SEARCH BAR MODE =====
    enum class SearchBarMode {
        Find,       // Single row: search only
        Replace     // Two rows: search + replace
    };

// ===== INLINE SEARCH BAR =====
// A self-contained horizontal bar that embeds directly into the editor layout.
// Height: findBarHeight (32px) for Find, findBarHeight*2+2 for Replace.
// Shown/hidden via SetVisible() — UpdateChildLayout() reserves space when visible.

    class UltraCanvasSearchBar : public UltraCanvasContainer {
    private:
        // ===== MODE =====
        SearchBarMode mode = SearchBarMode::Find;

        // ===== FIND ROW COMPONENTS =====
        std::shared_ptr<UltraCanvasTextInput>   searchInput;
        std::shared_ptr<UltraCanvasLabel>       countLabel;     // "3 / 12" or "No results"
        std::shared_ptr<UltraCanvasButton>      prevButton;     // ↑
        std::shared_ptr<UltraCanvasButton>      nextButton;     // ↓
        std::shared_ptr<UltraCanvasButton>      caseSensitiveButton; // Cc
        std::shared_ptr<UltraCanvasButton>      wholeWordButton;     // W
        // std::shared_ptr<UltraCanvasButton>      wrapAroundButton;    // ↕
        std::shared_ptr<UltraCanvasButton>      closeButton;    // ✕

        // ===== REPLACE ROW COMPONENTS =====
        std::shared_ptr<UltraCanvasContainer>   replaceRow;
        std::shared_ptr<UltraCanvasTextInput>   replaceInput;
        std::shared_ptr<UltraCanvasButton>      replaceButton;
        std::shared_ptr<UltraCanvasButton>      replaceAllButton;


        // ===== STATE =====
        std::string searchText;
        std::string replaceText;
        bool caseSensitive = false;
        bool wholeWord     = false;
        // bool wrapAround    = true;

        std::vector<std::string> searchHistory;
        std::vector<std::string> replaceHistory;
        int maxHistoryItems = 20;

        // ===== LAYOUT CONSTANTS =====
        static constexpr int RowHeight     = 32;
        static constexpr int RowPadding    = 4;   // vertical inset per row
        static constexpr int HSpacing      = 4;
        static constexpr int IconBtnSize   = 28;
        static constexpr int CountLabelW   = 72;
        static constexpr int ReplaceBtnW   = 110;

        // ===== PRIVATE HELPERS =====
        void BuildFindRow(int y, int w);
        void BuildReplaceRow(int y, int w);
        void WireCallbacks();
        void AddToHistory(std::vector<std::string>& history, const std::string& text);

    public:
        UltraCanvasSearchBar(const std::string& id, long uid, int x, int y, int w);

        // ===== SETUP =====
        void Initialize();

        // ===== MODE CONTROL =====
        void SetMode(SearchBarMode newMode);
        SearchBarMode GetMode() const { return mode; }

        // Returns total height in pixels for current mode
        int GetBarHeight() const;

        // ===== CONTENT =====
        void SetSearchText(const std::string& text);
        std::string GetSearchText() const { return searchText; }

        void SetReplaceText(const std::string& text);
        std::string GetReplaceText() const { return replaceText; }

        // ===== OPTIONS =====
        void SetCaseSensitive(bool v);
        void SetWholeWord(bool v);
        // void SetWrapAround(bool v);
        bool IsCaseSensitive() const { return caseSensitive; }
        bool IsWholeWord()     const { return wholeWord; }
        // bool IsWrapAround()    const { return wrapAround; }

        // ===== STATUS =====
        // currentIndex: 1-based; 0 = no current match
        void UpdateMatchCount(int currentIndex, int totalMatches);

        // ===== HISTORY =====
        void SetSearchHistory(const std::vector<std::string>& h)  { searchHistory  = h; }
        void SetReplaceHistory(const std::vector<std::string>& h) { replaceHistory = h; }
        const std::vector<std::string>& GetSearchHistory()  const { return searchHistory; }
        const std::vector<std::string>& GetReplaceHistory() const { return replaceHistory; }

        // ===== THEME =====
        void ApplyDarkTheme();
        void ApplyLightTheme();

        // Focus the search input
        void FocusSearchInput();

        // ===== CALLBACKS =====
        std::function<void(const std::string&, bool caseSensitive, bool wholeWord)> onFindNext;
        std::function<void(const std::string&, bool caseSensitive, bool wholeWord)> onFindPrevious;
        std::function<void(const std::string& find, const std::string& replace, bool cs, bool ww)> onReplace;
        std::function<void(const std::string& find, const std::string& replace, bool cs, bool ww)> onReplaceAll;
        std::function<void()> onClose;
        std::function<void(const std::string&)> onSearchTextChanged;
    };

} // namespace UltraCanvas