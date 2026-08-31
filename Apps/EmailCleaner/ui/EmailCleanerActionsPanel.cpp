// Apps/EmailCleaner/ui/EmailCleanerActionsPanel.cpp
// Version: 0.2.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "EmailCleanerActionsPanel.h"

#include "UltraCanvasModalDialog.h"

#include <ctime>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace EmailCleaner {

namespace {

constexpr float kRowY     = 6.0f;
constexpr float kControlH = 26.0f;

// Amber for "read this before you press the button", grey for the routine
// summary. Warnings are the whole point of the plan preview, so they get the
// colour and the summary does not.
const Color kWarningColor(176, 96, 0, 255);
const Color kQuietColor(96, 96, 96, 255);

// The strip has room for about three wrapped lines. Two warnings fit; beyond
// that the rest are counted rather than cut off mid-sentence, and the
// confirmation dialog spells every one of them out.
std::string JoinWarnings(const std::vector<std::string>& warnings) {
    constexpr size_t kInline = 2;
    std::string out;
    for (size_t i = 0; i < warnings.size() && i < kInline; ++i) {
        if (!out.empty()) out += "   ";
        out += "⚠ " + warnings[i];
    }
    if (warnings.size() > kInline) {
        out += "   (+" + std::to_string(warnings.size() - kInline) +
               " more, shown when you press Apply)";
    }
    return out;
}

} // namespace

std::shared_ptr<UltraCanvasContainer> ActionsPanel::Build(float x, float y,
                                                          float width, float height) {
    root_ = CreateContainer("ecActions", x, y, width, height);

    float cursor = 8.0f;
    root_->AddChild(CreateLabel("ecActionsLabel", cursor, kRowY + 4, 130, 20,
                                "Act on selection"));
    cursor += 136.0f;

    // The three actions. Each one re-plans on change, so the summary line
    // below always describes exactly what "Apply" would do.
    auto addCheckbox = [&](const std::string& id, const std::string& text, float w)
        -> std::shared_ptr<UltraCanvasCheckbox> {
        auto box = std::make_shared<UltraCanvasCheckbox>(id, cursor, kRowY, w, kControlH, text);
        box->onStateChanged = [this](CheckedState, CheckedState) { UpdatePlan(); };
        root_->AddChild(box);
        cursor += w + 8.0f;
        return box;
    };

    block_        = addCheckbox("ecActBlock", "Block sender", 130.0f);
    unsubscribe_  = addCheckbox("ecActUnsub", "Unsubscribe", 126.0f);
    deleteMail_   = addCheckbox("ecActDelete", "Move to Trash", 140.0f);
    unwantedOnly_ = addCheckbox("ecActUnwanted", "Unwanted only", 144.0f);

    cursor += 12.0f;
    apply_ = CreateButton("ecActApply", cursor, kRowY, 110, kControlH, "Apply…");
    apply_->onClick = [this]() { Confirm(); };
    root_->AddChild(apply_);
    cursor += 118.0f;

    // ---- Second row: what the user says the verdict should be --------------
    // Separate from Block on purpose: "I do not want to hear from them" and
    // "your verdict about them is wrong" are different statements, and a
    // sender can warrant either, both, or neither.
    const float row2 = kRowY + kControlH + 6;
    float cursor2 = 8.0f;

    root_->AddChild(CreateLabel("ecActVerdictLabel", cursor2, row2 + 4, 150, 20,
                                "Correct the verdict"));
    cursor2 += 156.0f;

    markFine_ = CreateButton("ecActFine", cursor2, row2, 130, kControlH, "This is fine");
    markFine_->onClick = [this]() { MarkVerdict(true); };
    root_->AddChild(markFine_);
    cursor2 += 138.0f;

    markSpam_ = CreateButton("ecActSpam", cursor2, row2, 130, kControlH, "This is spam");
    markSpam_->onClick = [this]() { MarkVerdict(false); };
    root_->AddChild(markSpam_);
    cursor2 += 150.0f;

    overrides_ = CreateButton("ecActOverrides", cursor2, row2, 190, kControlH,
                              "Corrected senders…");
    overrides_->onClick = [this]() { ShowOverrides(); };
    root_->AddChild(overrides_);
    cursor2 += 198.0f;

    blocklist_ = CreateButton("ecActBlocklist", cursor2, row2, 175, kControlH,
                              "Blocked senders…");
    blocklist_->onClick = [this]() { ShowBlocklist(); };
    root_->AddChild(blocklist_);

    // What the classifier currently says about the selection, so "correct the
    // verdict" is not a guess about what is being corrected.
    verdict_ = CreateLabel("ecActVerdict", 8, row2 + kControlH + 4, width - 16, 20, "");
    verdict_->SetTextColor(kQuietColor);
    root_->AddChild(verdict_);

    // Two lines under the controls: what would happen, and what to watch out
    // for. They are separate so a long warning cannot push the summary away.
    summary_ = CreateLabel("ecActSummary", 8, row2 + kControlH + 24, width - 16, 20,
                           "Pick a sender on the map to act on it.");
    summary_->SetTextColor(kQuietColor);
    root_->AddChild(summary_);

    // Two lines' worth, wrapped: a plan can raise more than one warning and a
    // truncated warning is worse than no warning at all.
    warning_ = CreateLabel("ecActWarning", 8, row2 + kControlH + 46, width - 16, 54, "");
    warning_->SetTextColor(kWarningColor);
    warning_->SetWrap(TextWrap::WrapWord);
    root_->AddChild(warning_);

    UpdatePlan();
    return root_;
}

void ActionsPanel::SetTarget(const ActionTarget& target, const std::string& accountId) {
    target_    = target;
    accountId_ = accountId;

    // A whole domain covers senders the user may still want, so the safe
    // default comes on with the selection rather than being remembered from
    // the last single sender.
    if (unwantedOnly_ && target.IsDomain() && !unwantedOnly_->IsChecked())
        unwantedOnly_->SetChecked(true);

    UpdatePlan();
}

void ActionsPanel::Refresh() { UpdatePlan(); }

ActionRequest ActionsPanel::CurrentRequest() const {
    ActionRequest request;
    request.target       = target_;
    request.block        = block_ && block_->IsChecked();
    request.unsubscribe  = unsubscribe_ && unsubscribe_->IsChecked();
    request.deleteMail   = deleteMail_ && deleteMail_->IsChecked();
    request.unwantedOnly = unwantedOnly_ && unwantedOnly_->IsChecked();
    return request;
}

void ActionsPanel::UpdatePlan() {
    if (!summary_ || !warning_) return;

    const bool haveTarget = target_.Valid() && store_ && store_->IsOpen();
    if (block_)        block_->SetDisabled(!haveTarget);
    if (unsubscribe_)  unsubscribe_->SetDisabled(!haveTarget);
    if (deleteMail_)   deleteMail_->SetDisabled(!haveTarget);
    if (unwantedOnly_) unwantedOnly_->SetDisabled(!haveTarget);

    if (markFine_)  markFine_->SetDisabled(!haveTarget);
    if (markSpam_)  markSpam_->SetDisabled(!haveTarget);

    // What the corpus currently says about this selection, and whether that is
    // the classifier's reading or the user's correction of it.
    if (verdict_) {
        if (!haveTarget) {
            verdict_->SetText("");
        } else {
            VerdictOverride existing;
            const bool corrected = store_->FindOverride(target_.senderAddr,
                                                        target_.domain, existing);
            if (corrected) {
                verdict_->SetText("You marked " + existing.pattern + " as " +
                                  CategoryLabel(existing.category) +
                                  " — that beats the classifier for their mail. "
                                  "Take it back under \"Corrected senders…\".");
            } else {
                MessageFilter f;
                f.accountId = accountId_;
                if (target_.IsDomain()) f.senderDomain = target_.domain;
                else                    f.senderAddr   = target_.senderAddr;
                std::vector<CategoryTotal> totals;
                std::string dominant = "nothing yet";
                if (store_->GetCategoryTotals(f, totals) && !totals.empty()) {
                    const CategoryTotal* top = &totals.front();
                    for (const CategoryTotal& t : totals)
                        if (t.messageCount > top->messageCount) top = &t;
                    dominant = CategoryLabel(top->category);
                }
                verdict_->SetText("Classified as " + dominant +
                                  ". Say so if that is wrong.");
            }
        }
    }

    if (!haveTarget) {
        plan_ = ActionPlan{};
        warnings_.clear();
        summary_->SetText("Pick a sender or a domain on the map to act on it.");
        warning_->SetText("");
        if (apply_) apply_->SetDisabled(true);
        return;
    }

    const ActionRequest request = CurrentRequest();
    plan_     = ActionPlanner(*store_).Plan(request, accountId_);
    warnings_ = plan_.warnings;
    // The mail half needs a server; say so here rather than at the end of a
    // confirmation the user already agreed to.
    if (!backend_ && (plan_.willUnsubscribe || plan_.willDelete))
        warnings_.push_back(noBackend_ + " Blocking still works; the rest cannot run.");

    if (!request.block && !request.unsubscribe && !request.deleteMail) {
        summary_->SetText("Nothing ticked — choose what to do with " +
                          target_.Describe() + ".");
        warning_->SetText("");
        if (apply_) apply_->SetDisabled(true);
        return;
    }

    summary_->SetText(plan_.Describe());
    warning_->SetText(JoinWarnings(warnings_));

    // Something has to actually be runnable: a plan that is nothing but a
    // refused unsubscribe would apply nothing.
    const bool runnable = plan_.willBlock ||
                          ((plan_.willUnsubscribe || plan_.willDelete) && backend_ != nullptr);
    if (apply_) apply_->SetDisabled(!runnable);
}

void ActionsPanel::Confirm() {
    if (plan_.Empty() || !store_) return;

    // The confirmation repeats the plan and every warning verbatim. Deleting
    // is the step that cannot be undone from here, so it decides the wording.
    std::string message = plan_.Describe();
    for (const std::string& w : warnings_) message += "\n\n⚠ " + w;
    if (plan_.willDelete) {
        message += "\n\nMessages are moved to the account's Trash folder, "
                   "not erased — they stay there until you empty it.";
    }

    const std::string title = plan_.willDelete ? "Move mail to Trash" : "Apply to sender";
    ActionPlan plan = plan_;   // by value: the panel may re-plan before the answer
    UltraCanvasDialogManager::ShowConfirmation(
        message, title,
        [this, plan](bool confirmed) { if (confirmed) Apply(plan); },
        nullptr);
}

void ActionsPanel::Apply(const ActionPlan& plan) {
    if (!store_) return;

    ActionExecutor executor(*store_, backend_);
    executor.now = static_cast<int64_t>(std::time(nullptr));
    const ActionOutcome outcome = executor.Execute(plan);

    // Clear the tick boxes: leaving "Move to Trash" armed while the selection
    // moves to the next sender is how an accident happens.
    if (block_)       block_->SetChecked(false);
    if (unsubscribe_) unsubscribe_->SetChecked(false);
    if (deleteMail_)  deleteMail_->SetChecked(false);

    if (!outcome.ok) {
        // Describe() already names the first failure; only a second one adds
        // anything, so the full list appears only when there is more than one.
        std::string detail = outcome.Describe();
        if (outcome.errors.size() > 1) {
            for (const std::string& e : outcome.errors) detail += "\n• " + e;
        }
        UltraCanvasDialogManager::ShowWarning(detail, "Not everything worked", nullptr);
    }

    if (onApplied) onApplied(outcome);
    UpdatePlan();
}

void ActionsPanel::MarkVerdict(bool wanted) {
    if (!store_ || !target_.Valid()) return;

    // One category per direction rather than a picker: the user is answering
    // "is this wanted?", and asking them to also choose between six spam
    // families turns a one-click correction into a form. The classifier keeps
    // its finer reading in the base verdict either way.
    const MessageCategory category =
        wanted ? MessageCategory::Personal : MessageCategory::ProductSpam;

    const std::string what = target_.Describe();
    std::string message =
        wanted ? ("Mark " + what + " as wanted?\n\nTheir mail stops counting as "
                  "unwanted on the map and in the totals, whatever the rules say "
                  "about it.")
               : ("Mark " + what + " as spam?\n\nTheir mail counts as unwanted "
                  "from now on, whatever the rules say about it.");
    if (target_.IsDomain()) {
        message += "\n\n⚠ This covers every sender under " + target_.domain +
                   ", including any you have not seen yet.";
    }
    message += "\n\nThis does not delete or block anything, and it can be taken "
               "back under \"Corrected senders…\".";

    UltraCanvasDialogManager::ShowConfirmation(
        message, wanted ? "Mark as wanted" : "Mark as spam",
        [this, category, wanted](bool confirmed) {
            if (!confirmed || !store_) return;
            VerdictOverride entry;
            entry.pattern  = target_.IsDomain() ? target_.domain : target_.senderAddr;
            entry.isDomain = target_.IsDomain();
            entry.category = category;
            entry.reason   = wanted ? "marked wanted" : "marked spam";
            entry.added    = static_cast<int64_t>(std::time(nullptr));

            const UltraDbResult r = store_->SetOverride(entry);
            if (!r) {
                UltraCanvasDialogManager::ShowWarning(
                    "Could not record that: " + r.message, "Not saved", nullptr);
                return;
            }
            store_->ApplyOverridesToMessages();
            UpdatePlan();
            // onApplied repaints from the database, and the app rewrites the
            // status line while it does — so the sentence goes after it.
            if (onApplied) onApplied(ActionOutcome{});
            if (onStatus) {
                onStatus("Marked " + entry.pattern + " as " +
                         (wanted ? "wanted" : "spam") +
                         ". Their mail is re-counted from here on; undo it under "
                         "\"Corrected senders…\".");
            }
        },
        nullptr);
}

void ActionsPanel::ShowOverrides() {
    if (!store_) return;

    std::vector<VerdictOverride> entries;
    store_->ListOverrides(entries);

    DialogConfig config;
    config.title      = "Corrected senders";
    config.width      = 660;
    config.height     = 420;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;

    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    auto* dlg = dialog.get();
    dialog->layout.SetFlexColumn()
                  .SetFlexGap(10)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(14);

    auto heading = CreateLabel(
        "ecOvHeading", 0, 0, 620, 40,
        entries.empty() ? "You have not corrected any verdicts yet."
                        : "These beat the classifier for their senders' mail. "
                          "Taking one back restores what the classifier "
                          "actually said, message by message.");
    heading->SetWrap(TextWrap::WrapWord);
    dialog->AddChild(heading);

    auto list = CreateScrollableContainer("ecOvList", 0, 0, 620, 280);
    float y = 0.0f;
    int index = 0;
    for (const VerdictOverride& entry : entries) {
        const std::string id = "ecOv" + std::to_string(index++);
        auto row = CreateContainer(id, 0, y, 600, 40);

        row->AddChild(CreateLabel(id + ".text", 0, 0, 480, 19,
                                  entry.isDomain ? (entry.pattern + " (whole domain)")
                                                 : entry.pattern));
        std::string sub = std::string(entry.Wanted() ? "wanted" : "spam") +
                          " · " + CategoryLabel(entry.category);
        if (entry.added > 0) sub += " · " + FormatDate(entry.added);
        auto subLabel = CreateLabel(id + ".sub", 0, 19, 480, 18, sub);
        subLabel->SetTextColor(kQuietColor);
        row->AddChild(subLabel);

        const std::string pattern = entry.pattern;
        auto undo = CreateButton(id + ".undo", 490, 8, 96, 24, "Undo");
        auto* rowPtr = row.get();
        undo->onClick = [this, pattern, rowPtr]() {
            if (!store_) return;
            store_->RemoveOverride(pattern);
            store_->ApplyOverridesToMessages();
            rowPtr->SetVisible(false);
            UpdatePlan();
            if (onApplied) onApplied(ActionOutcome{});
            if (onStatus)
                onStatus("Took back the correction for " + pattern +
                         " — the classifier's own verdict is back.");
        };
        row->AddChild(undo);

        list->AddChild(row);
        y += 44.0f;
    }
    dialog->AddChild(list);
    list->layoutItem.SetFlexGrow(1);

    auto buttonRow = CreateContainer("ecOvButtons", 0, 0, 0, 34);
    buttonRow->layout.SetFlexRow()
                     .SetFlexGap(10)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttonRow->AddStretchSpacer(1);
    auto closeBtn = std::make_shared<UltraCanvasButton>("ecOvClose", 0, 0, 90, 28);
    closeBtn->SetText("Close");
    closeBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::OK); };
    buttonRow->AddChild(closeBtn);
    dialog->AddChild(buttonRow);

    UltraCanvasDialogManager::ShowDialog(dialog, nullptr, nullptr);
}

void ActionsPanel::ShowBlocklist() {
    if (!store_) return;

    std::vector<BlockEntry> entries;
    store_->ListBlocks(entries);

    DialogConfig config;
    config.title      = "Blocked senders";
    config.width      = 660;
    config.height     = 420;
    config.dialogType = DialogType::Custom;
    config.buttons    = DialogButtons::NoButtons;   // the dialog builds its own

    auto dialog = UltraCanvasDialogManager::CreateDialog(config);
    // Raw pointer in the callbacks: the dialog owns the buttons, so capturing
    // the shared_ptr would form a reference cycle.
    auto* dlg = dialog.get();

    dialog->layout.SetFlexColumn()
                  .SetFlexGap(10)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    dialog->SetPadding(14);

    auto heading = CreateLabel(
        "ecBlHeading", 0, 0, 620, 40,
        entries.empty() ? "Nothing is blocked yet."
                        : "Blocked senders stay marked in the map. Unblocking one "
                          "puts it straight back into the wanted counts.");
    heading->SetWrap(TextWrap::WrapWord);
    dialog->AddChild(heading);

    auto list = CreateScrollableContainer("ecBlList", 0, 0, 620, 280);
    float y = 0.0f;
    int index = 0;
    for (const BlockEntry& entry : entries) {
        const std::string id = "ecBl" + std::to_string(index++);
        auto row = CreateContainer(id, 0, y, 600, 40);

        // Two lines: what is blocked, then why and since when. One line would
        // have to be truncated for a long address, and the address is the part
        // the user is looking for.
        row->AddChild(CreateLabel(id + ".text", 0, 0, 480, 19,
                                  entry.isDomain ? (entry.pattern + " (whole domain)")
                                                 : entry.pattern));
        std::string sub = entry.reason.empty() ? std::string("blocked") : entry.reason;
        if (entry.added > 0) sub += " · " + FormatDate(entry.added);
        auto subLabel = CreateLabel(id + ".sub", 0, 19, 480, 18, sub);
        subLabel->SetTextColor(kQuietColor);
        row->AddChild(subLabel);

        // Unblocking has to re-stamp the stored messages, or the map would go
        // on showing the sender as blocked until the next analysis.
        const std::string pattern = entry.pattern;
        auto unblock = CreateButton(id + ".unblock", 490, 8, 96, 24, "Unblock");
        // Raw pointer to the row: the list owns it, and capturing the
        // shared_ptr in a button the row itself owns would be a cycle.
        auto* rowPtr = row.get();
        unblock->onClick = [this, pattern, rowPtr]() {
            if (!store_) return;
            store_->RemoveBlock(pattern);
            store_->ApplyBlocklistToMessages();
            rowPtr->SetVisible(false);
            UpdatePlan();
            if (onApplied) onApplied(ActionOutcome{});
            if (onStatus) onStatus("Unblocked " + pattern + ".");
        };
        row->AddChild(unblock);

        list->AddChild(row);
        y += 44.0f;
    }
    dialog->AddChild(list);
    list->layoutItem.SetFlexGrow(1);

    auto buttonRow = CreateContainer("ecBlButtons", 0, 0, 0, 34);
    buttonRow->layout.SetFlexRow()
                     .SetFlexGap(10)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    buttonRow->AddStretchSpacer(1);
    auto closeBtn = std::make_shared<UltraCanvasButton>("ecBlClose", 0, 0, 90, 28);
    closeBtn->SetText("Close");
    closeBtn->onClick = [dlg]() { dlg->CloseDialog(DialogResult::OK); };
    buttonRow->AddChild(closeBtn);
    dialog->AddChild(buttonRow);

    UltraCanvasDialogManager::ShowDialog(dialog, nullptr, nullptr);
}

} // namespace EmailCleaner
