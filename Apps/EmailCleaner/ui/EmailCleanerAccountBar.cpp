// Apps/EmailCleaner/ui/EmailCleanerAccountBar.cpp
// Version: 0.3.0 (Phase 3)
// Author: UltraCanvas Framework / ULTRA OS
#include "EmailCleanerAccountBar.h"

#include <string>

using namespace UltraCanvas;

namespace EmailCleaner {

namespace {
constexpr float kRowY      = 8.0f;
constexpr float kControlH  = 26.0f;
} // namespace

std::shared_ptr<UltraCanvasContainer> AccountBar::Build(float x, float y,
                                                        float width, float height) {
    root_ = CreateContainer("ecAccountBar", x, y, width, height);

    float cursor = 8.0f;

    root_->AddChild(CreateLabel("ecAccountLabel", cursor, kRowY + 4, 68, 20, "Account"));
    cursor += 72.0f;

    accountPicker_ = CreateDropdown("ecAccountPicker", cursor, kRowY, 190, kControlH);
    accountPicker_->AddItem("All accounts", "");
    accountPicker_->SetSelectedIndex(0, false);
    accountPicker_->onSelectionChanged = [this](int index, const DropdownItem&) {
        filter_.accountId = (index <= 0 || index > static_cast<int>(accounts_.size()))
                          ? std::string()
                          : accounts_[index - 1].accountId;
        NotifyFilterChanged();
    };
    root_->AddChild(accountPicker_);
    cursor += 198.0f;

    auto scanButton = CreateButton("ecScan", cursor, kRowY, 110, kControlH, "Load mail");
    scanButton->onClick = [this]() { if (onScan) onScan(); };
    root_->AddChild(scanButton);
    cursor += 118.0f;

    auto reanalyseButton = CreateButton("ecReanalyse", cursor, kRowY, 110, kControlH,
                                        "Re-analyse");
    reanalyseButton->onClick = [this]() { if (onReanalyse) onReanalyse(); };
    root_->AddChild(reanalyseButton);
    cursor += 118.0f;

    // The rules were always editable — as a text file in the data directory.
    // This is that file, in a dialog.
    auto rulesButton = CreateButton("ecRules", cursor, kRowY, 84, kControlH, "Rules…");
    rulesButton->onClick = [this]() { if (onEditRules) onEditRules(); };
    root_->AddChild(rulesButton);
    cursor += 100.0f;

    // ---- Filters -----------------------------------------------------------
    root_->AddChild(CreateLabel("ecCategoryLabel", cursor, kRowY + 4, 84, 20, "Category"));
    cursor += 88.0f;

    categoryPicker_ = CreateDropdown("ecCategoryPicker", cursor, kRowY, 150, kControlH);
    categoryPicker_->AddItem("All categories", "");
    for (MessageCategory category : AllCategories())
        categoryPicker_->AddItem(CategoryLabel(category), ToString(category));
    categoryPicker_->SetSelectedIndex(0, false);
    categoryPicker_->onSelectionChanged = [this](int index, const DropdownItem&) {
        const auto& all = AllCategories();
        if (index <= 0 || index > static_cast<int>(all.size())) {
            filter_.categorySet = false;
        } else {
            filter_.category    = all[index - 1];
            filter_.categorySet = true;
        }
        NotifyFilterChanged();
    };
    root_->AddChild(categoryPicker_);
    cursor += 162.0f;

    unwantedOnly_ = std::make_shared<UltraCanvasCheckbox>(
        "ecUnwantedOnly", cursor, kRowY, 140, kControlH, "Unwanted only");
    unwantedOnly_->onStateChanged = [this](CheckedState, CheckedState newState) {
        filter_.unwantedOnly = (newState == CheckedState::Checked);
        NotifyFilterChanged();
    };
    root_->AddChild(unwantedOnly_);
    cursor += 148.0f;

    search_ = CreateTextInput("ecSearch", static_cast<int>(cursor), static_cast<int>(kRowY),
                              180, static_cast<int>(kControlH));
    search_->SetPlaceholder("Search subject or sender");
    search_->onTextChanged = [this](const std::string& text) {
        filter_.search = text;
        NotifyFilterChanged();
    };
    root_->AddChild(search_);

    // The summary spans the second row, where it has room for a long sentence.
    status_ = CreateLabel("ecStatus", 8, kRowY + kControlH + 8, width - 16, 20,
                          "No messages analysed yet");
    root_->AddChild(status_);
    return root_;
}

void AccountBar::SetAccounts(const std::vector<StoredAccount>& accounts) {
    accounts_ = accounts;
    if (!accountPicker_) return;

    accountPicker_->ClearItems();
    accountPicker_->AddItem("All accounts", "");
    for (const StoredAccount& account : accounts_) {
        const std::string label = account.email.empty() ? account.accountId : account.email;
        accountPicker_->AddItem(label, account.accountId);
    }
    // Keep the current selection if it still exists, otherwise fall back to
    // "All accounts" — and update the filter to match what is shown.
    int index = 0;
    for (size_t i = 0; i < accounts_.size(); ++i) {
        if (accounts_[i].accountId == filter_.accountId) {
            index = static_cast<int>(i) + 1;
            break;
        }
    }
    if (index == 0) filter_.accountId.clear();
    accountPicker_->SetSelectedIndex(index, false);
}

void AccountBar::SetStatus(const std::string& text) {
    if (status_) status_->SetText(text);
}

void AccountBar::NotifyFilterChanged() {
    if (onFilterChanged) onFilterChanged();
}

} // namespace EmailCleaner
