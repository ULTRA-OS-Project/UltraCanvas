// Apps/UltraSocial/ui/UltraSocialComposeView.cpp
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSocialComposeView.h"

#include "UltraSocialComposer.h"
#include "UltraSocialConnector.h"

#include "UltraCanvasChip.h"

#include <algorithm>
#include <filesystem>

using namespace UltraCanvas;

namespace UltraSocial {

std::shared_ptr<UltraCanvasContainer> ComposeView::Build() {
    root_ = CreateContainer("composeView", 0, 40, 980, 620);

    root_->AddChild(CreateLabel("cvPrompt", 12, 4, 300, 22, "What's happening?"));

    text_ = std::make_shared<UltraCanvasTextArea>("cvText", 12, 28, 600, 240);
    text_->SetEditingMode(TextAreaEditingMode::PlainText);
    text_->SetOnTextChanged([this](const std::string&) { UpdateCounters(); });
    root_->AddChild(text_);

    warnings_ = CreateLabel("cvWarnings", 12, 274, 600, 56, "");
    warnings_->SetWrap(TextWrap::WrapWord);
    root_->AddChild(warnings_);

    auto addMediaBtn = CreateButton("cvAddMedia", 12, 336, 120, 28, "Add image…");
    addMediaBtn->onClick = [this]() { if (onAddMedia) onAddMedia(); };
    root_->AddChild(addMediaBtn);

    mediaRow_ = CreateContainer("cvMediaRow", 140, 336, 640, 28);
    mediaRow_->layout.SetFlexRow()
                     .SetFlexGap(6)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    root_->AddChild(mediaRow_);

    root_->AddChild(CreateLabel("cvTargetsLabel", 640, 4, 200, 22, "Post to"));
    targets_ = CreateContainer("cvTargets", 640, 28, 330, 240);
    targets_->layout.SetFlexColumn()
                    .SetFlexGap(6)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    root_->AddChild(targets_);

    auto addAccountBtn = CreateButton("cvAddAccount", 640, 274, 150, 28,
                                      "Add account…");
    addAccountBtn->onClick = [this]() { if (onAddAccount) onAddAccount(); };
    root_->AddChild(addAccountBtn);

    postButton_ = CreateButton("cvPost", 12, 380, 140, 34, "Post");
    postButton_->onClick = [this]() { if (onPost) onPost(); };
    root_->AddChild(postButton_);

    root_->AddChild(CreateLabel("cvHistoryLabel", 12, 430, 200, 22, "History"));
    history_ = CreateContainer("cvHistory", 12, 454, 950, 150);
    history_->layout.SetFlexColumn()
                    .SetFlexGap(4)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    root_->AddChild(history_);

    return root_;
}

void ComposeView::SetAccounts(const std::vector<Account>& accounts) {
    if (!targets_) return;
    targets_->ClearChildren();
    targetRows_.clear();

    if (accounts.empty()) {
        auto empty = CreateLabel("cvNoAccounts", 0, 0, 320, 40,
            "No accounts yet — use “Add account…” to connect one.");
        empty->SetWrap(TextWrap::WrapWord);
        targets_->AddChild(empty);
        UpdateCounters();
        return;
    }

    int index = 0;
    for (const auto& account : accounts) {
        auto connector = CreateConnector(account.network);
        if (!connector) continue;

        TargetRow row;
        row.account = account;
        row.caps    = connector->Capabilities();

        const std::string id = "cvTarget" + std::to_string(index++);
        auto line = CreateContainer(id + "Row", 0, 0, 0, 26);
        line->layout.SetFlexRow()
                    .SetFlexGap(8)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Center);

        row.checkbox = UltraCanvasCheckbox::CreateCheckbox(
            id + "Check", 0, 0, 230, 24, account.handle, /*checked=*/true);
        row.checkbox->onStateChanged = [this](CheckedState, CheckedState) {
            UpdateCounters();
        };
        line->AddChild(row.checkbox);
        row.checkbox->layoutItem.SetFlexGrow(1);

        row.counter = CreateBadge(id + "Count", 0, 0, "0");
        line->AddChild(row.counter);

        targets_->AddChild(line);
        line->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        targetRows_.push_back(std::move(row));
    }
    UpdateCounters();
}

void ComposeView::AddMedia(const std::string& filePath) {
    if (filePath.empty()) return;
    mediaPaths_.push_back(filePath);
    RebuildMediaChips();
    UpdateCounters();
}

void ComposeView::RebuildMediaChips() {
    if (!mediaRow_) return;
    mediaRow_->ClearChildren();
    int index = 0;
    for (const auto& path : mediaPaths_) {
        const std::string id = "cvMedia" + std::to_string(index++);
        auto chip = CreateChip(id,
                               0, 0,
                               std::filesystem::path(path).filename().string(),
                               /*closable=*/true);
        chip->onClose = [this, path]() {
            mediaPaths_.erase(
                std::find(mediaPaths_.begin(), mediaPaths_.end(), path));
            RebuildMediaChips();
            UpdateCounters();
        };
        mediaRow_->AddChild(chip);
    }
}

PostDraft ComposeView::CollectDraft() const {
    PostDraft draft;
    if (text_) draft.text = text_->GetText();
    for (const auto& path : mediaPaths_) {
        draft.media.push_back({path, "", ""});
    }
    return draft;
}

std::vector<std::string> ComposeView::SelectedAccountIds() const {
    std::vector<std::string> ids;
    for (const auto& row : targetRows_) {
        if (row.checkbox && row.checkbox->IsChecked()) {
            ids.push_back(row.account.accountId);
        }
    }
    return ids;
}

void ComposeView::SetBusy(bool busy) {
    if (!postButton_) return;
    postButton_->SetDisabled(busy);
    postButton_->SetText(busy ? "Posting…" : "Post");
}

void ComposeView::SetHistoryLines(const std::vector<std::string>& lines) {
    if (!history_) return;
    history_->ClearChildren();
    int index = 0;
    for (const auto& line : lines) {
        auto label = CreateLabel("cvHist" + std::to_string(index++),
                                 0, 0, 940, 20, line);
        history_->AddChild(label);
        label->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    }
}

void ComposeView::ClearAfterPost() {
    if (text_) text_->SetText("");
    mediaPaths_.clear();
    RebuildMediaChips();
    UpdateCounters();
}

void ComposeView::UpdateCounters() {
    const PostDraft draft = CollectDraft();
    const int length = CountTextPoints(draft.text);
    const bool hasMedia = !draft.media.empty();

    std::string warningText;
    for (auto& row : targetRows_) {
        int limit = row.caps.maxTextChars;
        if (hasMedia && row.caps.maxMediaCaptionChars > 0) {
            limit = row.caps.maxMediaCaptionChars;
        }
        if (row.counter) {
            row.counter->SetText(std::to_string(length) + " / " +
                                 std::to_string(limit));
            row.counter->SetVariant(length > limit ? BadgeVariant::Warning
                                                   : BadgeVariant::Neutral);
        }
        if (row.checkbox && row.checkbox->IsChecked()) {
            std::vector<std::string> rowWarnings;
            AdaptDraft(draft, row.account.network, row.caps, rowWarnings);
            for (const auto& warning : rowWarnings) {
                if (!warningText.empty()) warningText += '\n';
                warningText += warning;
            }
        }
    }
    if (warnings_) warnings_->SetText(warningText);
}

} // namespace UltraSocial
