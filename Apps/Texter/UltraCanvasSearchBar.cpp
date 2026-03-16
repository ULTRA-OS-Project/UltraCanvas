// Apps/Texter/UltraCanvasSearchBar.cpp
// Inline search and replace bar implementation
// Version: 1.0.0
// Last Modified: 2026-03-14
// Author: UltraCanvas Framework

#include "UltraCanvasSearchBar.h"
#include "UltraCanvasBoxLayout.h"
#include <string>

namespace UltraCanvas {

// ── LAYOUT GEOMETRY ──────────────────────────────────────────────────────────
//
//  FIND mode  (height = RowHeight):
//  ┌────────────────────────────────────────────────────────────────────────┐
//  │ 🔍  [    search text                    ]  3 / 12  ↓  ↑  ⚙  ✕      │
//  └────────────────────────────────────────────────────────────────────────┘
//
//  REPLACE mode  (height = RowHeight*2 + 2):
//  ┌────────────────────────────────────────────────────────────────────────┐
//  │ 🔍  [    search text                    ]  3 / 12  ↓  ↑  ⚙  ✕      │
//  ├╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┤
//  │ ↺  [    replace text                   ]  [Replace]  [Replace all]  │
//  └────────────────────────────────────────────────────────────────────────┘

    UltraCanvasSearchBar::UltraCanvasSearchBar(const std::string& id, long uid, int x, int y, int w)
            : UltraCanvasContainer(id, uid, x, y, w, RowHeight)
    {
    }

    int UltraCanvasSearchBar::GetBarHeight() const {
        return (mode == SearchBarMode::Replace)
               ? (RowHeight * 2 + 2)
               : RowHeight;
    }

    void UltraCanvasSearchBar::Initialize() {
        SetBackgroundColor(Color(245, 245, 245, 255));

        // Disable container scrollbars — this is a fixed-height bar
        ContainerStyle cs;
        cs.autoShowScrollbars = false;
        cs.forceShowVerticalScrollbar   = false;
        cs.forceShowHorizontalScrollbar = false;
        SetContainerStyle(cs);

        BuildFindRow(0, GetWidth());
        WireCallbacks();

        SetVisible(false); // hidden until Ctrl+F / Ctrl+H
    }

    // ── FIND ROW ─────────────────────────────────────────────────────────────

    void UltraCanvasSearchBar::BuildFindRow(int y, int w) {
        // Work right-to-left to calculate input width
        // Right side: [close] [options] [prev] [next] — each IconBtnSize wide
        // Left of input: small search icon label (20px)
        // After input: count label (CountLabelW)

        int rightButtonsWidth = (IconBtnSize + HSpacing) * 5; // ↓ ↑ Cc W ↕ ✕
        int searchIconW = 20;
        int inputW = w - searchIconW - HSpacing
                     - CountLabelW - HSpacing
                     - rightButtonsWidth
                     - RowPadding * 2;
        if (inputW < 80) inputW = 80;

        int inputH = RowHeight - RowPadding * 2;
        int btnY   = y + (RowHeight - IconBtnSize) / 2;
        int inputY = y + RowPadding;

        int x = RowPadding;

        // Search icon (non-interactive label)
        auto searchIcon = std::make_shared<UltraCanvasLabel>("SearchIcon", 6001, x, inputY, searchIconW, inputH);
        searchIcon->SetText("🔍");
        searchIcon->SetFontSize(12);
        AddChild(searchIcon);
        x += searchIconW + HSpacing;

        // Search input
        searchInput = std::make_shared<UltraCanvasTextInput>("SearchInput", 6002, x, inputY, inputW, inputH);
        searchInput->SetPlaceholder("Find...");
        searchInput->SetShowValidationState(false);
        searchInput->SetFontSize(11);
        AddChild(searchInput);
        x += inputW + HSpacing;

        // Match count label
        countLabel = std::make_shared<UltraCanvasLabel>("CountLabel", 6003, x, inputY, CountLabelW, inputH);
        countLabel->SetText("");
        countLabel->SetFontSize(10);
        countLabel->SetTextColor(Color(120, 120, 120, 255));
        countLabel->SetAlignment(TextAlignment::Left);
        AddChild(countLabel);
        x += CountLabelW + HSpacing;

        // ↓ Next
        nextButton = std::make_shared<UltraCanvasButton>("NextBtn", 6004, x, btnY, IconBtnSize, IconBtnSize);
        nextButton->SetText("↓");
        nextButton->SetFontSize(12);
        nextButton->SetTooltip("Find Next (Enter)");
        nextButton->SetAcceptsFocus(false);
        AddChild(nextButton);
        x += IconBtnSize + HSpacing;

        // ↑ Previous
        prevButton = std::make_shared<UltraCanvasButton>("PrevBtn", 6005, x, btnY, IconBtnSize, IconBtnSize);
        prevButton->SetText("↑");
        prevButton->SetFontSize(12);
        prevButton->SetTooltip("Find Previous (Shift+Enter)");
        prevButton->SetAcceptsFocus(false);
        AddChild(prevButton);
        x += IconBtnSize + HSpacing;

        // Cc — Match Case toggle
        caseSensitiveButton = std::make_shared<UltraCanvasButton>("CaseSensitiveBtn", 6006, x, btnY, IconBtnSize, IconBtnSize);
        caseSensitiveButton->SetText("Cc");
        caseSensitiveButton->SetFontSize(11);
        caseSensitiveButton->SetTooltip("Match Case");
        caseSensitiveButton->SetCanToggled(true);
        caseSensitiveButton->SetAcceptsFocus(false);
        if (caseSensitive) caseSensitiveButton->SetPressed(true);
        AddChild(caseSensitiveButton);
        x += IconBtnSize + HSpacing;

        // W — Whole Word toggle
        wholeWordButton = std::make_shared<UltraCanvasButton>("WholeWordBtn", 6008, x, btnY, IconBtnSize, IconBtnSize);
        wholeWordButton->SetText("W");
        wholeWordButton->SetFontSize(11);
        wholeWordButton->SetTooltip("Whole Word");
        wholeWordButton->SetCanToggled(true);
        wholeWordButton->SetAcceptsFocus(false);
        if (wholeWord) wholeWordButton->SetPressed(true);
        AddChild(wholeWordButton);
        x += IconBtnSize + HSpacing;

        // ↕ — Wrap Around toggle
        // wrapAroundButton = std::make_shared<UltraCanvasButton>("WrapAroundBtn", 6009, x, btnY, IconBtnSize, IconBtnSize);
        // wrapAroundButton->SetText("↕");
        // wrapAroundButton->SetFontSize(13);
        // wrapAroundButton->SetTooltip("Wrap Around");
        // wrapAroundButton->SetCanToggled(true);
        // wrapAroundButton->SetAcceptsFocus(false);
        // if (wrapAround) wrapAroundButton->SetPressed(true);
        // AddChild(wrapAroundButton);
        // x += IconBtnSize + HSpacing;

        // ✕ Close
        closeButton = std::make_shared<UltraCanvasButton>("CloseBtn", 6007, x, btnY, IconBtnSize, IconBtnSize);
        closeButton->SetText("✕");
        closeButton->SetFontSize(11);
        closeButton->SetTooltip("Close (Escape)");
        closeButton->SetAcceptsFocus(false);
        AddChild(closeButton);
    }

    // ── REPLACE ROW ──────────────────────────────────────────────────────────

    void UltraCanvasSearchBar::BuildReplaceRow(int y, int w) {
        replaceRow = std::make_shared<UltraCanvasContainer>("ReplaceRow", 6100, 0, y, w, RowHeight);

        ContainerStyle cs;
        cs.autoShowScrollbars = false;
        replaceRow->SetContainerStyle(cs);
        replaceRow->SetBackgroundColor(Color(0, 0, 0, 0)); // transparent

        int replaceBtnsWidth = ReplaceBtnW + HSpacing + ReplaceBtnW + HSpacing; // [Replace] + [Replace all]
        int replaceIconW = 20;
        // input spans up to the replace buttons (skip count label gap to align with find row)
        int inputW = w - replaceIconW - HSpacing
                     - CountLabelW - HSpacing
                     - replaceBtnsWidth
                     - RowPadding * 2;
        if (inputW < 80) inputW = 80;

        int inputH = RowHeight - RowPadding * 2;
        int inputY = (RowHeight - inputH) / 2;
        int btnY   = (RowHeight - IconBtnSize) / 2;

        int x = RowPadding;

        // Replace icon label
        auto replaceIcon = std::make_shared<UltraCanvasLabel>("ReplaceIcon", 6101, x, inputY, replaceIconW, inputH);
        replaceIcon->SetText("↺");
        replaceIcon->SetFontSize(13);
        replaceRow->AddChild(replaceIcon);
        x += replaceIconW + HSpacing;

        // Replace input (same width as search input)
        replaceInput = std::make_shared<UltraCanvasTextInput>("ReplaceInput", 6102, x, inputY, inputW, inputH);
        replaceInput->SetPlaceholder("Replace...");
        replaceInput->SetShowValidationState(false);
        replaceInput->SetFontSize(11);
        replaceRow->AddChild(replaceInput);
        x += inputW + HSpacing + CountLabelW + HSpacing; // skip count label space

        // [Replace]
        replaceButton = std::make_shared<UltraCanvasButton>("ReplaceBtn", 6103,
                                                            x, btnY, ReplaceBtnW, IconBtnSize);
        replaceButton->SetText("Replace");
        replaceButton->SetFontSize(11);
        replaceButton->SetAcceptsFocus(false);
        replaceRow->AddChild(replaceButton);
        x += ReplaceBtnW + HSpacing;

        // [Replace all]
        replaceAllButton = std::make_shared<UltraCanvasButton>("ReplaceAllBtn", 6104,
                                                               x, btnY, ReplaceBtnW, IconBtnSize);
        replaceAllButton->SetText("Replace all");
        replaceAllButton->SetFontSize(11);
        replaceAllButton->SetAcceptsFocus(false);
        replaceRow->AddChild(replaceAllButton);

        replaceRow->SetVisible(false);
        AddChild(replaceRow);
    }


    // ── WIRE CALLBACKS ────────────────────────────────────────────────────────

    void UltraCanvasSearchBar::WireCallbacks() {
        // Search input text changed
        searchInput->onTextChanged = [this](const std::string& text) {
            searchText = text;
            bool hasText = !text.empty();
            if (nextButton)    nextButton->SetDisabled(!hasText);
            if (prevButton)    prevButton->SetDisabled(!hasText);
            if (!hasText && countLabel) countLabel->SetText("");
            if (onSearchTextChanged) onSearchTextChanged(text);
            // Live search as user types
            if (hasText && onFindNext) {
                onFindNext(searchText, caseSensitive, wholeWord);
            }
        };

        // Enter key in search input → find next
        searchInput->onEnterPressed = [this](const std::string& text) {
            if (!text.empty() && onFindNext) {
                AddToHistory(searchHistory, text);
                onFindNext(text, caseSensitive, wholeWord);
            }
        };

        // Replace input text changed
        if (replaceInput) {
            replaceInput->onTextChanged = [this](const std::string& text) {
                replaceText = text;
            };
        }

        // ↓ Next
        nextButton->onClick = [this]() {
            if (!searchText.empty() && onFindNext) {
                AddToHistory(searchHistory, searchText);
                onFindNext(searchText, caseSensitive, wholeWord);
            }
        };

        // ↑ Previous
        prevButton->onClick = [this]() {
            if (!searchText.empty() && onFindPrevious) {
                AddToHistory(searchHistory, searchText);
                onFindPrevious(searchText, caseSensitive, wholeWord);
            }
        };

        // Cc — Match Case toggle
        caseSensitiveButton->onToggle = [this](bool isPressed) {
            caseSensitive = isPressed;
            if (!searchText.empty() && onFindNext) {
                onFindNext(searchText, caseSensitive, wholeWord);
            }
        };

        // W — Whole Word toggle
        wholeWordButton->onToggle = [this](bool isPressed) {
            wholeWord = isPressed;
            if (!searchText.empty() && onFindNext) {
                onFindNext(searchText, caseSensitive, wholeWord);
            }
        };

        // ↕ — Wrap Around toggle
        // wrapAroundButton->onToggle = [this](bool isPressed) {
        //     wrapAround = isPressed;
        // };

        // ✕ Close
        closeButton->onClick = [this]() {
            SetVisible(false);
            if (onClose) onClose();
        };

        // [Replace]
        if (replaceButton) {
            replaceButton->onClick = [this]() {
                if (!searchText.empty() && onReplace) {
                    AddToHistory(searchHistory, searchText);
                    if (!replaceText.empty()) AddToHistory(replaceHistory, replaceText);
                    onReplace(searchText, replaceText, caseSensitive, wholeWord);
                }
            };
        }

        // [Replace all]
        if (replaceAllButton) {
            replaceAllButton->onClick = [this]() {
                if (!searchText.empty() && onReplaceAll) {
                    AddToHistory(searchHistory, searchText);
                    if (!replaceText.empty()) AddToHistory(replaceHistory, replaceText);
                    onReplaceAll(searchText, replaceText, caseSensitive, wholeWord);
                }
            };
        }

        // Disable nav buttons initially
        if (nextButton) nextButton->SetDisabled(true);
        if (prevButton) prevButton->SetDisabled(true);
    }

    // ── MODE ─────────────────────────────────────────────────────────────────

    void UltraCanvasSearchBar::SetMode(SearchBarMode newMode) {
        if (mode == newMode) return;
        mode = newMode;

        if (mode == SearchBarMode::Replace) {
            // Build replace row if not yet created
            if (!replaceRow) {
                BuildReplaceRow(RowHeight + 2, GetWidth());

                // Wire replace callbacks now
                if (replaceInput) {
                    replaceInput->onTextChanged = [this](const std::string& text) {
                        replaceText = text;
                    };
                }
                if (replaceButton) {
                    replaceButton->onClick = [this]() {
                        if (!searchText.empty() && onReplace) {
                            AddToHistory(searchHistory, searchText);
                            if (!replaceText.empty()) AddToHistory(replaceHistory, replaceText);
                            onReplace(searchText, replaceText, caseSensitive, wholeWord);
                        }
                    };
                }
                if (replaceAllButton) {
                    replaceAllButton->onClick = [this]() {
                        if (!searchText.empty() && onReplaceAll) {
                            AddToHistory(searchHistory, searchText);
                            if (!replaceText.empty()) AddToHistory(replaceHistory, replaceText);
                            onReplaceAll(searchText, replaceText, caseSensitive, wholeWord);
                        }
                    };
                }
            }
            replaceRow->SetVisible(true);
            SetSize(GetWidth(), GetBarHeight());
        } else {
            if (replaceRow) replaceRow->SetVisible(false);
            SetSize(GetWidth(), GetBarHeight());
        }

        RequestRedraw();
    }

    // ── STATUS ───────────────────────────────────────────────────────────────

    void UltraCanvasSearchBar::UpdateMatchCount(int currentIndex, int totalMatches) {
        if (!countLabel) return;

        if (totalMatches == 0) {
            countLabel->SetText("No results");
            countLabel->SetTextColor(Color(200, 60, 60, 255));
        } else if (currentIndex > 0) {
            countLabel->SetText(std::to_string(currentIndex) + " / " + std::to_string(totalMatches));
            countLabel->SetTextColor(Color(100, 100, 100, 255));
        } else {
            countLabel->SetText(std::to_string(totalMatches) + " found");
            countLabel->SetTextColor(Color(100, 100, 100, 255));
        }
    }

    // ── CONTENT SETTERS ───────────────────────────────────────────────────────

    void UltraCanvasSearchBar::SetSearchText(const std::string& text) {
        searchText = text;
        if (searchInput) searchInput->SetText(text);
    }

    void UltraCanvasSearchBar::SetReplaceText(const std::string& text) {
        replaceText = text;
        if (replaceInput) replaceInput->SetText(text);
    }

    void UltraCanvasSearchBar::SetCaseSensitive(bool v) {
        caseSensitive = v;
    }

    void UltraCanvasSearchBar::SetWholeWord(bool v) {
        wholeWord = v;
    }

    // void UltraCanvasSearchBar::SetWrapAround(bool v) {
    //     wrapAround = v;
    // }

    void UltraCanvasSearchBar::FocusSearchInput() {
        if (searchInput) {
            searchInput->SetFocus(true);
            searchInput->SelectAll();
        }
    }

    // ── THEME ─────────────────────────────────────────────────────────────────

    void UltraCanvasSearchBar::ApplyDarkTheme() {
        SetBackgroundColor(Color(40, 40, 40, 255));

        auto applyInputDark = [](std::shared_ptr<UltraCanvasTextInput> inp) {
            if (!inp) return;
            TextInputStyle s = inp->GetStyle();
            s.backgroundColor = Color(55, 55, 55, 255);
            s.textColor       = Color(210, 210, 210, 255);
            s.borderColor     = Color(70, 70, 70, 255);
            inp->SetStyle(s);
        };
        applyInputDark(searchInput);
        applyInputDark(replaceInput);

        if (countLabel) countLabel->SetTextColor(Color(160, 160, 160, 255));

        auto applyBtnDark = [](std::shared_ptr<UltraCanvasButton> btn) {
            if (!btn) return;
            ButtonStyle s = btn->GetStyle();
            s.normalColor = Color(40, 40, 40, 255);
            s.hoverColor  = Color(60, 60, 60, 255);
            s.normalTextColor   = Color(200, 200, 200, 255);
            btn->SetStyle(s);
        };
        applyBtnDark(nextButton);
        applyBtnDark(prevButton);
        applyBtnDark(caseSensitiveButton);
        applyBtnDark(wholeWordButton);
        // applyBtnDark(wrapAroundButton);
        applyBtnDark(closeButton);
        applyBtnDark(replaceButton);
        applyBtnDark(replaceAllButton);

        if (replaceRow) replaceRow->SetBackgroundColor(Color(36, 36, 36, 255));
        RequestRedraw();
    }

    void UltraCanvasSearchBar::ApplyLightTheme() {
        SetBackgroundColor(Color(245, 245, 245, 255));

        auto applyInputLight = [](std::shared_ptr<UltraCanvasTextInput> inp) {
            if (!inp) return;
            TextInputStyle s = inp->GetStyle();
            s.backgroundColor = Color(255, 255, 255, 255);
            s.textColor       = Color(30, 30, 30, 255);
            s.borderColor     = Color(200, 200, 200, 255);
            inp->SetStyle(s);
        };
        applyInputLight(searchInput);
        applyInputLight(replaceInput);

        if (countLabel) countLabel->SetTextColor(Color(120, 120, 120, 255));

        auto applyBtnLight = [](std::shared_ptr<UltraCanvasButton> btn) {
            if (!btn) return;
            ButtonStyle s = btn->GetStyle();
            s.normalColor = Color(245, 245, 245, 255);
            s.hoverColor  = Color(225, 225, 225, 255);
            s.normalTextColor   = Color(60, 60, 60, 255);
            btn->SetStyle(s);
        };
        applyBtnLight(nextButton);
        applyBtnLight(prevButton);
        applyBtnLight(caseSensitiveButton);
        applyBtnLight(wholeWordButton);
        // applyBtnLight(wrapAroundButton);
        applyBtnLight(closeButton);
        applyBtnLight(replaceButton);
        applyBtnLight(replaceAllButton);

        if (replaceRow) replaceRow->SetBackgroundColor(Color(238, 238, 238, 255));
        RequestRedraw();
    }

    // ── HISTORY ──────────────────────────────────────────────────────────────

    void UltraCanvasSearchBar::AddToHistory(std::vector<std::string>& history, const std::string& text) {
        if (text.empty()) return;
        history.erase(std::remove(history.begin(), history.end(), text), history.end());
        history.insert(history.begin(), text);
        if (static_cast<int>(history.size()) > maxHistoryItems) {
            history.resize(maxHistoryItems);
        }
    }

} // namespace UltraCanvas