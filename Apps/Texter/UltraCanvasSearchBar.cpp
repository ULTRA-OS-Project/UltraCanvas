// Apps/Texter/UltraCanvasSearchBar.cpp
// Inline search and replace bar implementation
// Version: 1.2.0
// Last Modified: 2026-03-19
// Author: UltraCanvas Framework

#include "UltraCanvasSearchBar.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasBoxLayout.h"
#include <string>
#include <algorithm>

namespace UltraCanvas {

// ── LAYOUT GEOMETRY ──────────────────────────────────────────────────────────
//
//  FIND mode  (height = RowHeight):
//  ┌────────────────────────────────────────────────────────────────────────┐
//  │ 🔍  [  search text ≤400  ]  3 / 12          ↓  ↑  ⚙  ✕             │
//  └────────────────────────────────────────────────────────────────────────┘
//
//  REPLACE mode  (height = RowHeight*2 + 2):
//  ┌────────────────────────────────────────────────────────────────────────┐
//  │ 🔍  [  search text ≤400  ]  3 / 12          ↓  ↑  ⚙  ✕             │
//  ├╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┤
//  │ ↺  [  replace text ≤400 ]          [Replace]  [Replace all]         │
//  └────────────────────────────────────────────────────────────────────────┘
//
//  Left group: icon + input (≤400) + countLabel — left-aligned
//  Right group: buttons — right-aligned
//  ⚙ opens popup menu with: ☑ Case sensitive / ☑ Whole words

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
        BuildSettingsMenu();
        WireCallbacks();
        UpdateLayout();

        SetVisible(false); // hidden until Ctrl+F / Ctrl+H
    }

    // ── SetBounds override — reposition children on resize ────────────────

    void UltraCanvasSearchBar::SetBounds(const Rect2Di& bounds) {
        UltraCanvasContainer::SetBounds(bounds);
        UpdateLayout();
    }

    // ── Compute shared input width for both rows ──────────────────────────

    int UltraCanvasSearchBar::ComputeInputWidth() const {
        int w = GetWidth();
        int rightButtonsWidth = 4 * (IconBtnSize + HSpacing); // ↓ ↑ ⚙ ✕
        int fixedLeft = RowPadding + SearchIconW + HSpacing;
        int fixedAfterInput = HSpacing + CountLabelW;
        int available = w - fixedLeft - fixedAfterInput - rightButtonsWidth - RowPadding;
        return std::min(MaxInputWidth, std::max(80, available));
    }

    // ── UpdateLayout — reposition all children based on current width ─────

    void UltraCanvasSearchBar::UpdateLayout() {
        int w = GetWidth();
        if (w <= 0) return;

        int inputW = ComputeInputWidth();
        int inputH = RowHeight - RowPadding * 2;
        int inputY = RowPadding;
        int btnY   = (RowHeight - IconBtnSize) / 2;

        // ── Find row: left group ──
        int x = RowPadding;

        if (searchIconButton)
            searchIconButton->SetBounds(Rect2Di(x, inputY, SearchIconW, inputH));
        x += SearchIconW + HSpacing;

        if (searchInput)
            searchInput->SetBounds(Rect2Di(x, inputY, inputW, inputH));
        x += inputW + HSpacing;

        if (countLabel)
            countLabel->SetBounds(Rect2Di(x, inputY, CountLabelW, inputH));

        // ── Find row: right group (right-aligned) ──
        int rx = w - RowPadding;

        rx -= IconBtnSize;
        if (closeButton)
            closeButton->SetBounds(Rect2Di(rx, btnY, IconBtnSize, IconBtnSize));
        rx -= HSpacing;

        rx -= IconBtnSize;
        if (settingsButton)
            settingsButton->SetBounds(Rect2Di(rx, btnY, IconBtnSize, IconBtnSize));
        rx -= HSpacing;

        rx -= IconBtnSize;
        if (prevButton)
            prevButton->SetBounds(Rect2Di(rx, btnY, IconBtnSize, IconBtnSize));
        rx -= HSpacing;

        rx -= IconBtnSize;
        if (nextButton)
            nextButton->SetBounds(Rect2Di(rx, btnY, IconBtnSize, IconBtnSize));

        // ── Replace row ──
        if (replaceRow) {
            int replaceY = RowHeight + 2;
            replaceRow->SetBounds(Rect2Di(0, replaceY, w, RowHeight));

            int rix = RowPadding;
            int rInputY = RowPadding;
            int rBtnY   = (RowHeight - IconBtnSize) / 2;

            if (replaceIconButton)
                replaceIconButton->SetBounds(Rect2Di(rix, rInputY, SearchIconW, inputH));
            rix += SearchIconW + HSpacing;

            if (replaceInput)
                replaceInput->SetBounds(Rect2Di(rix, rInputY, inputW, inputH));

            // Right-aligned replace buttons
            int rrx = w - RowPadding;

            rrx -= ReplaceBtnW;
            if (replaceAllButton)
                replaceAllButton->SetBounds(Rect2Di(rrx, rBtnY, ReplaceBtnW, IconBtnSize));
            rrx -= HSpacing;

            rrx -= ReplaceBtnW;
            if (replaceButton)
                replaceButton->SetBounds(Rect2Di(rrx, rBtnY, ReplaceBtnW, IconBtnSize));
        }
    }

    // ── FIND ROW ─────────────────────────────────────────────────────────────

    void UltraCanvasSearchBar::BuildFindRow(int y, int w) {
        int inputW = std::min(MaxInputWidth, std::max(80, w - 236));
        int inputH = RowHeight - RowPadding * 2;
        int btnY   = y + (RowHeight - IconBtnSize) / 2;
        int inputY = y + RowPadding;
        int x = RowPadding;

        // Search icon button
        searchIconButton = std::make_shared<UltraCanvasButton>("SearchIconBtn", 6001, x, inputY, SearchIconW, inputH);
        searchIconButton->SetIcon(GetResourcesDir() + "media/icons/texter/search.svg");
        searchIconButton->SetIconSize(14, 14);
        searchIconButton->SetIconScaleToFit(true);
        searchIconButton->SetAcceptsFocus(false);
        searchIconButton->SetTooltip("Search History");
        searchIconButton->onClick = [this]() { ShowHistoryMenu(false); };
        AddChild(searchIconButton);
        x += SearchIconW + HSpacing;

        // Search input
        searchInput = std::make_shared<UltraCanvasTextInput>("SearchInput", 6002, x, inputY, inputW, inputH);
        searchInput->SetPlaceholder("Find...");
        searchInput->SetShowValidationState(false);
        searchInput->SetFontSize(11);
        searchInput->SetShowClearButton(true);
        AddChild(searchInput);
        x += inputW + HSpacing;

        // Match count label — right after input
        countLabel = std::make_shared<UltraCanvasLabel>("CountLabel", 6003, x, inputY, CountLabelW, inputH);
        countLabel->SetText("");
        countLabel->SetFontSize(10);
        countLabel->SetTextColor(Color(120, 120, 120, 255));
        countLabel->SetAlignment(TextAlignment::Left);
        AddChild(countLabel);

        // Right-aligned buttons (positions will be corrected by UpdateLayout)
        int rx = w - RowPadding;

        rx -= IconBtnSize;
        closeButton = std::make_shared<UltraCanvasButton>("CloseBtn", 6007, rx, btnY, IconBtnSize, IconBtnSize);
        closeButton->SetText("\xe2\x9c\x95"); // ✕
        closeButton->SetFontSize(11);
        closeButton->SetTooltip("Close (Escape)");
        closeButton->SetAcceptsFocus(false);
        AddChild(closeButton);
        rx -= HSpacing;

        rx -= IconBtnSize;
        settingsButton = std::make_shared<UltraCanvasButton>("SettingsBtn", 6009, rx, btnY, IconBtnSize, IconBtnSize);
        settingsButton->SetText("\xe2\x9a\x99"); // ⚙
        settingsButton->SetFontSize(13);
        settingsButton->SetTooltip("Search Options");
        settingsButton->SetAcceptsFocus(false);
        AddChild(settingsButton);
        rx -= HSpacing;

        rx -= IconBtnSize;
        prevButton = std::make_shared<UltraCanvasButton>("PrevBtn", 6005, rx, btnY, IconBtnSize, IconBtnSize);
        prevButton->SetText("\xe2\x86\x91"); // ↑
        prevButton->SetFontSize(11);
        prevButton->SetTooltip("Find Previous (Shift+Enter)");
        prevButton->SetAcceptsFocus(false);
        AddChild(prevButton);
        rx -= HSpacing;

        rx -= IconBtnSize;
        nextButton = std::make_shared<UltraCanvasButton>("NextBtn", 6004, rx, btnY, IconBtnSize, IconBtnSize);
        nextButton->SetText("\xe2\x86\x93"); // ↓
        nextButton->SetFontSize(11);
        nextButton->SetTooltip("Find Next (Enter)");
        nextButton->SetAcceptsFocus(false);
        AddChild(nextButton);
    }

    // ── SETTINGS MENU ────────────────────────────────────────────────────────

    void UltraCanvasSearchBar::BuildSettingsMenu() {
        settingsMenu = std::make_shared<UltraCanvasMenu>("SearchSettingsMenu", 6010, 0, 0, 200, 100);
        settingsMenu->SetMenuType(MenuType::PopupMenu);

        settingsMenu->AddItem(MenuItemData::Checkbox("Case sensitive", caseSensitive, [this](bool checked) {
            caseSensitive = checked;
            if (!searchText.empty() && onFindNext) {
                onFindNext(searchText, caseSensitive, wholeWord);
            }
        }));

        settingsMenu->AddItem(MenuItemData::Checkbox("Whole words", wholeWord, [this](bool checked) {
            wholeWord = checked;
            if (!searchText.empty() && onFindNext) {
                onFindNext(searchText, caseSensitive, wholeWord);
            }
        }));
    }

    // ── REPLACE ROW ──────────────────────────────────────────────────────────

    void UltraCanvasSearchBar::BuildReplaceRow(int y, int w) {
        replaceRow = std::make_shared<UltraCanvasContainer>("ReplaceRow", 6100, 0, y, w, RowHeight);

        ContainerStyle cs;
        cs.autoShowScrollbars = false;
        replaceRow->SetContainerStyle(cs);
        replaceRow->SetBackgroundColor(Color(0, 0, 0, 0)); // transparent

        int inputW = ComputeInputWidth();
        int inputH = RowHeight - RowPadding * 2;
        int inputY = RowPadding;
        int btnY   = (RowHeight - IconBtnSize) / 2;
        int x = RowPadding;

        // Replace icon button
        replaceIconButton = std::make_shared<UltraCanvasButton>("ReplaceIconBtn", 6101, x, inputY, SearchIconW, inputH);
        replaceIconButton->SetIcon(GetResourcesDir() + "media/icons/texter/replace.svg");
        replaceIconButton->SetIconSize(14, 14);
        replaceIconButton->SetIconScaleToFit(true);
        replaceIconButton->SetAcceptsFocus(false);
        replaceIconButton->SetTooltip("Replace History");
        replaceIconButton->onClick = [this]() { ShowHistoryMenu(true); };
        replaceRow->AddChild(replaceIconButton);
        x += SearchIconW + HSpacing;

        // Replace input (same width as search input)
        replaceInput = std::make_shared<UltraCanvasTextInput>("ReplaceInput", 6102, x, inputY, inputW, inputH);
        replaceInput->SetPlaceholder("Replace...");
        replaceInput->SetShowValidationState(false);
        replaceInput->SetFontSize(11);
        replaceInput->SetShowClearButton(true);
        replaceRow->AddChild(replaceInput);

        // Right-aligned replace buttons (positions corrected by UpdateLayout)
        int rx = w - RowPadding;

        rx -= ReplaceBtnW;
        replaceAllButton = std::make_shared<UltraCanvasButton>("ReplaceAllBtn", 6104,
                                                               rx, btnY, ReplaceBtnW, IconBtnSize);
        replaceAllButton->SetText("Replace all");
        replaceAllButton->SetFontSize(11);
        replaceAllButton->SetAcceptsFocus(false);
        replaceRow->AddChild(replaceAllButton);
        rx -= HSpacing;

        rx -= ReplaceBtnW;
        replaceButton = std::make_shared<UltraCanvasButton>("ReplaceBtn", 6103,
                                                            rx, btnY, ReplaceBtnW, IconBtnSize);
        replaceButton->SetText("Replace");
        replaceButton->SetFontSize(11);
        replaceButton->SetAcceptsFocus(false);
        replaceRow->AddChild(replaceButton);

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

        // ⚙ Settings — show popup menu
        settingsButton->onClick = [this]() {
            auto* win = GetWindow();
            if (!win || !settingsMenu) return;
            win->AddChild(settingsMenu);
            Point2Di pos(settingsButton->GetXInWindow(),
                         settingsButton->GetYInWindow() + settingsButton->GetHeight() + 1);
            settingsMenu->ShowAt(pos);
        };

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

        UpdateLayout();
        RequestRedraw();
    }

    // ── STATUS ───────────────────────────────────────────────────────────────

    void UltraCanvasSearchBar::UpdateMatchCount(int currentIndex, int totalMatches) {
        if (!countLabel) return;

        if (totalMatches < 0) {
            countLabel->SetText("...");
            countLabel->SetTextColor(Color(120, 120, 120, 255));
            return;
        }

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
        applyBtnDark(searchIconButton);
        applyBtnDark(nextButton);
        applyBtnDark(prevButton);
        applyBtnDark(settingsButton);
        applyBtnDark(closeButton);
        applyBtnDark(replaceIconButton);
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
        applyBtnLight(searchIconButton);
        applyBtnLight(nextButton);
        applyBtnLight(prevButton);
        applyBtnLight(settingsButton);
        applyBtnLight(closeButton);
        applyBtnLight(replaceIconButton);
        applyBtnLight(replaceButton);
        applyBtnLight(replaceAllButton);

        if (replaceRow) replaceRow->SetBackgroundColor(Color(238, 238, 238, 255));
        RequestRedraw();
    }

    // ── HISTORY ──────────────────────────────────────────────────────────────

    void UltraCanvasSearchBar::ShowHistoryMenu(bool isReplace) {
        auto& history = isReplace ? replaceHistory : searchHistory;
        if (history.empty()) return;

        auto* win = GetWindow();
        if (!win) return;

        auto& menu = isReplace ? replaceHistoryMenu : searchHistoryMenu;
        if (!menu) {
            menu = std::make_shared<UltraCanvasMenu>(
                isReplace ? "ReplaceHistoryMenu" : "SearchHistoryMenu",
                isReplace ? 6012 : 6011, 0, 0, 300, 100);
            menu->SetMenuType(MenuType::PopupMenu);
        }
        menu->Clear();

        for (const auto& item : history) {
            menu->AddItem(MenuItemData::Action(item, [this, item, isReplace]() {
                if (isReplace) {
                    SetReplaceText(item);
                } else {
                    SetSearchText(item);
                    if (onSearchTextChanged) onSearchTextChanged(item);
                    if (onFindNext) onFindNext(item, caseSensitive, wholeWord);
                }
            }));
        }

        auto icon = isReplace ? replaceIconButton : searchIconButton;
        if (!icon) return;

        win->AddChild(menu);
        Point2Di pos(icon->GetXInWindow(),
                     icon->GetYInWindow() + icon->GetHeight() + 1);
        menu->ShowAt(pos);
    }

    void UltraCanvasSearchBar::AddToHistory(std::vector<std::string>& history, const std::string& text) {
        if (text.empty()) return;
        history.erase(std::remove(history.begin(), history.end(), text), history.end());
        history.insert(history.begin(), text);
        if (static_cast<int>(history.size()) > maxHistoryItems) {
            history.resize(maxHistoryItems);
        }
        if (onHistoryChanged) onHistoryChanged();
    }

} // namespace UltraCanvas
