// Apps/UltraSocial/ui/UltraSocialComposeView.cpp
// Version: 0.2.0 - UltraMail's look: toolbar, cards, network avatars, a
//                  flex layout that follows the window
// Version: 0.1.0 (Phase 1)
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSocialComposeView.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8

#include "UltraSocialComposer.h"
#include "UltraSocialConnector.h"
#include "UltraSocialTheme.h"

#include "UltraCanvasChip.h"
#include "UltraCanvasImageElement.h"

#include <algorithm>
#include <filesystem>

using namespace UltraCanvas;

namespace UltraSocial {

namespace {

constexpr float kRightColumnWidth = 330.0f;   // "Post to" + "Scheduled"
constexpr float kToolbarLogo      = 22.0f;
constexpr float kTextMinHeight    = 110.0f;
constexpr float kActionHeight     = 30.0f;    // Post / Post later
constexpr float kRowAvatar        = 20.0f;    // avatars in the history / scheduled rows
constexpr float kScheduledMaxH    = 168.0f;   // the queue scrolls past this

// Red for a failed post's reason - UltraMail's "scam" red.
const Color kErrorText {185, 28, 28};

// The pill counter beside an account: blue while the draft fits the
// network's limit, orange once it does not.
void StyleCounter(const std::shared_ptr<UltraCanvasBadge>& badge, bool over) {
    BadgeStyle s = badge->GetStyle();
    s.textColor  = over ? Theme::kWarningText : Theme::kAccent;
    s.fontSize   = Theme::kSizeSmall;
    s.height     = 18.0f;
    s.paddingH   = 7.0f;
    badge->SetStyle(s);
    badge->SetColor(over ? Theme::kWarningTint : Theme::kAccentSoft);
}

// A small status pill ("Posted", "Failed", "retry 2").
std::shared_ptr<UltraCanvasBadge> MakePill(const std::string& id, const std::string& text,
                                           const Color& tint, const Color& textColor) {
    auto badge = CreateBadge(id, 0, 0, text);
    BadgeStyle s = badge->GetStyle();
    s.textColor = textColor;
    s.fontSize  = Theme::kSizeSmall;
    s.height    = 18.0f;
    s.paddingH  = 7.0f;
    badge->SetStyle(s);
    badge->SetColor(tint);
    badge->layoutItem.SetFlexShrink(0);
    return badge;
}

// A flex row, children centred vertically.
std::shared_ptr<UltraCanvasContainer> MakeRow(const std::string& id, float gap,
                                              float height = 0.0f) {
    auto row = CreateContainer(id, 0, 0, 0, height);
    row->layout.SetFlexRow()
               .SetFlexGap(gap)
               .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->layoutItem.SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    Theme::NoScrollbars(row);
    return row;
}

// A card heading with an optional muted note at its right end.
std::shared_ptr<UltraCanvasContainer> MakeCardHeader(
        const std::string& id, const std::string& heading,
        std::shared_ptr<UltraCanvasLabel>* outNote = nullptr) {
    auto header = MakeRow(id, Theme::kInnerGap, 20.0f);
    header->AddChild(Theme::MakeHeading(id + "Title", heading));
    header->AddStretchSpacer(1);
    if (outNote) {
        *outNote = Theme::MakeText(id + "Note", "", Theme::kSizeSecondary, Theme::kTextMuted);
        header->AddChild(*outNote);
    }
    return header;
}

// Grow along the parent's main axis from a zero basis, so siblings share the
// space by their grow factors rather than by their content.
void Fill(const std::shared_ptr<UltraCanvasUIElement>& element, float grow = 1.0f) {
    element->layoutItem.SetFlexGrow(grow).SetFlexShrink(1)
                       .SetFlexBasis(CSSLayout::Dimension::Px(0));
}

// One line of an excerpt: line breaks become spaces.
std::string OneLine(std::string text) {
    std::replace(text.begin(), text.end(), '\n', ' ');
    std::replace(text.begin(), text.end(), '\r', ' ');
    return text;
}

} // namespace

std::shared_ptr<UltraCanvasContainer> ComposeView::Build() {
    root_ = CreateContainer("socialView", 0, 0, 0, 0);
    root_->SetPadding(Theme::kPagePadding);
    root_->layout.SetFlexColumn()
                 .SetFlexGap(Theme::kGap)
                 .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    Theme::NoScrollbars(root_);

    root_->AddChild(BuildToolbar());

    // Body: the composer and recent posts on the left, where the eye starts;
    // the targets and the queue in a fixed column on the right.
    auto body = CreateContainer("svBody", 0, 0, 0, 0);
    body->layout.SetFlexRow()
                .SetFlexGap(Theme::kGap)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    Fill(body);
    Theme::NoScrollbars(body);

    auto left = CreateContainer("svLeft", 0, 0, 0, 0);
    left->layout.SetFlexColumn()
                .SetFlexGap(Theme::kGap)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    Fill(left);
    Theme::NoScrollbars(left);
    left->AddChild(BuildComposeCard());
    left->AddChild(BuildHistoryCard());
    body->AddChild(left);

    auto right = CreateContainer("svRight", 0, 0, kRightColumnWidth, 0);
    right->layout.SetFlexColumn()
                 .SetFlexGap(Theme::kGap)
                 .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    right->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    Theme::NoScrollbars(right);
    right->AddChild(BuildTargetsCard());
    right->AddChild(BuildScheduledCard());
    body->AddChild(right);

    root_->AddChild(body);
    return root_;
}

std::shared_ptr<UltraCanvasContainer> ComposeView::BuildToolbar() {
    // [logo] UltraSocial ......................... [Add account]
    auto toolbar = MakeRow("svToolbar", Theme::kInnerGap + 2.0f, Theme::kToolbarHeight);
    toolbar->layoutItem.SetFlexGrow(0);

    auto logo = CreateImageElement("svLogo", kToolbarLogo, kToolbarLogo);
    logo->LoadFromFile(Theme::AppIconPath());
    logo->SetFitMode(ImageFitMode::Contain);
    toolbar->AddChild(logo);
    toolbar->AddChild(Theme::MakeText("svTitle", "UltraSocial", Theme::kSizeTitle,
                                      Theme::kTextPrimary, FontWeight::Bold));
    toolbar->AddStretchSpacer(1);

    auto addAccount = Theme::MakeButton("svAddAccount", "Add account", false,
                                        "user-plus.svg", 96.0f);
    addAccount->SetTooltip("Connect another social account");
    addAccount->onClick = [this]() { if (onAddAccount) onAddAccount(); };
    toolbar->AddChild(addAccount);
    return toolbar;
}

std::shared_ptr<UltraCanvasContainer> ComposeView::BuildComposeCard() {
    auto card = CreateContainer("svCompose", 0, 0, 0, 0);
    Theme::ApplyCard(card);
    Fill(card);
    Theme::NoScrollbars(card);

    card->AddChild(MakeCardHeader("svComposeHead", "New post", &length_));

    text_ = std::make_shared<UltraCanvasTextArea>("svText", 0, 0, 0, 0);
    text_->SetEditingMode(TextAreaEditingMode::PlainText);
    text_->SetWordWrap(true);
    text_->SetHighlightCurrentLine(false);
    text_->SetShowLineNumbers(false);
    Theme::StyleTextArea(text_);
    text_->SetPlaceholder("What's happening? Write it once - UltraSocial fits it "
                          "to every network you post to.");
    text_->SetOnTextChanged([this](const std::string&) { UpdateCounters(); });
    Fill(text_);
    CSSLayout::BoxConstraints textLimits;
    textLimits.minHeight = CSSLayout::Dimension::Px(kTextMinHeight);
    text_->boxConstraints = textLimits;
    card->AddChild(text_);

    // What adapting the draft will do on the chosen networks ("text shortened
    // to 300 characters"). Hidden while there is nothing to say.
    warnings_ = Theme::MakeWrapped("svWarnings", "", Theme::kSizeSecondary,
                                   Theme::kWarningText);
    warnings_->layoutItem.SetFlexShrink(0);
    warnings_->SetVisible(false);
    card->AddChild(warnings_);

    // [Add image…] [chip] [chip] …
    auto media = MakeRow("svMedia", Theme::kInnerGap, 28.0f);
    auto addMedia = Theme::MakeButton("svAddMedia", "Add image…", false, "image.svg", 96.0f);
    addMedia->SetTooltip("Attach a picture (PNG, JPEG, GIF or WebP)");
    addMedia->onClick = [this]() { if (onAddMedia) onAddMedia(); };
    media->AddChild(addMedia);
    mediaRow_ = MakeRow("svMediaChips", Theme::kInnerGap, 28.0f);
    Fill(mediaRow_);
    media->AddChild(mediaRow_);
    card->AddChild(media);

    card->AddChild(Theme::MakeDivider("svComposeRule"));

    // .................................. [Post later…] [Post]
    auto actions = MakeRow("svActions", Theme::kInnerGap + 2.0f, kActionHeight + 2.0f);
    actions->AddStretchSpacer(1);
    postLaterButton_ = Theme::MakeButton("svPostLater", "Post later…", false,
                                         "clock-five.svg", 110.0f, kActionHeight);
    postLaterButton_->SetTooltip("Queue this post for a date and time");
    postLaterButton_->onClick = [this]() { if (onPostLater) onPostLater(); };
    actions->AddChild(postLaterButton_);
    postButton_ = Theme::MakeButton("svPost", "Post", true, "send.svg", 100.0f, kActionHeight);
    postButton_->SetTooltip("Post now to every account ticked under \"Post to\"");
    postButton_->onClick = [this]() { if (onPost) onPost(); };
    actions->AddChild(postButton_);
    card->AddChild(actions);

    return card;
}

std::shared_ptr<UltraCanvasContainer> ComposeView::BuildTargetsCard() {
    auto card = CreateContainer("svTargetsCard", 0, 0, 0, 0);
    Theme::ApplyCard(card);
    Fill(card);
    Theme::NoScrollbars(card);

    card->AddChild(MakeCardHeader("svTargetsHead", "Post to"));
    targetsHint_ = Theme::MakeWrapped("svTargetsHint",
        "Tick the accounts this post goes to. Each counter is that network's limit.",
        Theme::kSizeSecondary, Theme::kTextSecondary);
    targetsHint_->layoutItem.SetFlexShrink(0);
    card->AddChild(targetsHint_);

    targets_ = CreateContainer("svTargets", 0, 0, 0, 0);
    targets_->layout.SetFlexColumn()
                    .SetFlexGap(2)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    Fill(targets_);
    card->AddChild(targets_);
    return card;
}

std::shared_ptr<UltraCanvasContainer> ComposeView::BuildScheduledCard() {
    auto card = CreateContainer("svScheduledCard", 0, 0, 0, 0);
    Theme::ApplyCard(card);
    card->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    Theme::NoScrollbars(card);

    card->AddChild(MakeCardHeader("svScheduledHead", "Scheduled", &scheduledCount_));

    scheduled_ = CreateContainer("svScheduled", 0, 0, 0, 0);
    scheduled_->layout.SetFlexColumn()
                      .SetFlexGap(4)
                      .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    CSSLayout::BoxConstraints limits;
    limits.maxHeight = CSSLayout::Dimension::Px(kScheduledMaxH);
    scheduled_->boxConstraints = limits;
    card->AddChild(scheduled_);
    return card;
}

std::shared_ptr<UltraCanvasContainer> ComposeView::BuildHistoryCard() {
    auto card = CreateContainer("svHistoryCard", 0, 0, 0, 0);
    Theme::ApplyCard(card, 4.0f);
    card->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    Theme::NoScrollbars(card);

    card->AddChild(MakeCardHeader("svHistoryHead", "Recent posts"));
    history_ = CreateContainer("svHistory", 0, 0, 0, 0);
    history_->layout.SetFlexColumn()
                    .SetFlexGap(2)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    Theme::NoScrollbars(history_);
    card->AddChild(history_);
    return card;
}

void ComposeView::Resize(float width, float height) {
    if (!root_) return;
    root_->SetElementSize(Size2Df(width, height));
}

void ComposeView::SetScheduled(const std::vector<ScheduledItem>& items) {
    if (!scheduled_) return;
    scheduled_->ClearChildren();
    if (scheduledCount_)
        scheduledCount_->SetText(items.empty() ? "" : std::to_string(items.size()) + " queued");

    if (items.empty()) {
        scheduled_->AddChild(Theme::MakeWrapped("svNoScheduled",
            "Nothing queued. \"Post later…\" puts a post here until its time comes.",
            Theme::kSizeSecondary, Theme::kTextMuted));
        return;
    }

    int index = 0;
    for (const auto& item : items) {
        const std::string id = "svSched" + std::to_string(index++);
        // [avatar] handle / when ........ [retry n] [Cancel]
        auto row = MakeRow(id, 8.0f, 34.0f);
        row->AddChild(Theme::MakeNetworkAvatar(id + "Avatar", item.network, kRowAvatar));

        auto who = CreateContainer(id + "Who", 0, 0, 0, 0);
        who->layout.SetFlexColumn().SetFlexGap(1);
        Fill(who);
        Theme::NoScrollbars(who);
        who->AddChild(Theme::MakeText(id + "Handle", item.handle, Theme::kSizeBody,
                                      Theme::kTextPrimary));
        who->AddChild(Theme::MakeText(id + "When", item.when, Theme::kSizeSmall,
                                      Theme::kTextSecondary));
        row->AddChild(who);

        if (item.attempts > 0) {
            auto retry = MakePill(id + "Retry", "retry " + std::to_string(item.attempts),
                                  Theme::kWarningTint, Theme::kWarningText);
            retry->SetTooltip(item.lastError.empty()
                                  ? std::string("The last attempt failed; UltraSocial tries again later.")
                                  : "Last attempt: " + item.lastError);
            row->AddChild(retry);
        }

        auto cancel = Theme::MakeButton(id + "Cancel", "Cancel", false, "", 0.0f, 22.0f);
        cancel->SetTooltip("Remove this post from the queue");
        const int64_t entryId = item.id;
        cancel->onClick = [this, entryId]() {
            if (onCancelScheduled) onCancelScheduled(entryId);
        };
        row->AddChild(cancel);
        scheduled_->AddChild(row);
    }
}

void ComposeView::SetAccounts(const std::vector<Account>& accounts) {
    if (!targets_) return;
    targets_->ClearChildren();
    targetRows_.clear();

    if (accounts.empty()) {
        targets_->AddChild(Theme::MakeWrapped("svNoAccounts",
            "No accounts yet - use \"Add account\" to connect one.",
            Theme::kSizeBody, Theme::kTextMuted));
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

        // [avatar] [x] handle ............ [12 / 500]
        const std::string id = "svTarget" + std::to_string(index++);
        auto line = MakeRow(id + "Row", 8.0f, 32.0f);
        line->SetTooltip(Theme::NetworkDisplayName(account.network) +
                         (account.server.empty() ? "" : " - " + account.server));
        line->AddChild(Theme::MakeNetworkAvatar(id + "Avatar", account.network));

        row.checkbox = UltraCanvasCheckbox::CreateCheckbox(
            id + "Check", 0, 0, 0, 24, account.handle, /*checked=*/true);
        Theme::StyleCheckbox(row.checkbox);
        row.checkbox->onStateChanged = [this](CheckedState, CheckedState) {
            UpdateCounters();
        };
        Fill(row.checkbox);
        line->AddChild(row.checkbox);

        row.counter = CreateBadge(id + "Count", 0, 0, "0");
        row.counter->layoutItem.SetFlexShrink(0);
        row.counter->SetTooltip(Theme::NetworkDisplayName(account.network) +
                                ": characters used / allowed");
        line->AddChild(row.counter);

        targets_->AddChild(line);
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
        const std::string id = "svMedia" + std::to_string(index++);
        auto chip = CreateChip(id, 0, 0,
                               PathToUtf8(PathFromUtf8(path).filename()),
                               /*closable=*/true);
        chip->SetIcon(Theme::IconPath("image.svg"));
        ChipStyle style = chip->GetStyle();
        style.fillColor      = Theme::kAccentSoft;
        style.hoverFillColor = Theme::kRowSelected;
        style.borderColor    = Theme::kRowSelected;
        style.textColor      = Theme::kAccentPressed;
        style.height         = 24.0f;
        style.cornerRadius   = 12.0f;
        style.iconSize       = 14.0f;
        style.fontStyle.fontSize = Theme::kSizeSecondary;
        chip->SetStyle(style);
        chip->SetTooltip(path);
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
    if (postLaterButton_) postLaterButton_->SetDisabled(busy);
}

void ComposeView::SetHistory(const std::vector<HistoryItem>& items) {
    if (!history_) return;
    history_->ClearChildren();

    if (items.empty()) {
        history_->AddChild(Theme::MakeWrapped("svNoHistory",
            "No posts yet. Each post appears here, one row per account, with a "
            "link to it.", Theme::kSizeSecondary, Theme::kTextMuted));
        return;
    }

    int index = 0;
    for (const auto& item : items) {
        const std::string id = "svHist" + std::to_string(index++);
        if (index > 1) history_->AddChild(Theme::MakeDivider(id + "Rule"));

        // [Posted] [avatar] handle  excerpt ................ when [Open]
        auto row = MakeRow(id, 8.0f, 30.0f);
        row->AddChild(item.succeeded
                          ? MakePill(id + "State", "Posted", Color(220, 252, 231),
                                     Color(21, 128, 61))
                          : MakePill(id + "State", "Failed", Color(254, 226, 226),
                                     kErrorText));
        row->AddChild(Theme::MakeNetworkAvatar(id + "Avatar", item.network, kRowAvatar));
        auto handle = Theme::MakeText(id + "Handle", item.handle, Theme::kSizeBody,
                                      Theme::kTextPrimary, FontWeight::Bold);
        handle->layoutItem.SetFlexShrink(0);
        row->AddChild(handle);

        auto excerpt = item.succeeded
            ? Theme::MakeText(id + "Text", OneLine(item.text), Theme::kSizeBody,
                              Theme::kTextSecondary)
            : Theme::MakeText(id + "Text", OneLine(item.error), Theme::kSizeBody, kErrorText);
        excerpt->SetTooltip(item.succeeded ? item.text : item.error);
        Fill(excerpt);
        row->AddChild(excerpt);

        auto when = Theme::MakeText(id + "When", item.when, Theme::kSizeSmall,
                                    Theme::kTextMuted);
        when->layoutItem.SetFlexShrink(0);
        row->AddChild(when);

        if (!item.url.empty()) {
            auto open = Theme::MakeButton(id + "Open", "Open", false, "", 0.0f, 22.0f);
            open->SetTooltip(item.url);
            const std::string url = item.url;
            open->onClick = [this, url]() { if (onOpenUrl) onOpenUrl(url); };
            row->AddChild(open);
        }
        history_->AddChild(row);
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

    if (length_) {
        length_->SetText(length == 0 ? "" : length == 1 ? "1 character"
                                                        : std::to_string(length) + " characters");
    }

    std::string warningText;
    for (auto& row : targetRows_) {
        int limit = row.caps.maxTextChars;
        if (hasMedia && row.caps.maxMediaCaptionChars > 0) {
            limit = row.caps.maxMediaCaptionChars;
        }
        if (row.counter) {
            const bool over = limit > 0 && length > limit;
            row.counter->SetText(limit > 0 ? std::to_string(length) + " / " + std::to_string(limit)
                                           : std::to_string(length));
            StyleCounter(row.counter, over);
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
    if (warnings_) {
        warnings_->SetText(warningText);
        warnings_->SetVisible(!warningText.empty());
    }
}

} // namespace UltraSocial
