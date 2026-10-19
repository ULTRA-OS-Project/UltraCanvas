// Apps/UltraSocial/ui/UltraSocialComposeView.h
// The main window's view once an account exists, in UltraMail's look: a
// toolbar (logo, name, "Add account"), then white cards on the page - the
// composer (text, adaptation warnings, image chips, Post later / Post), the
// "Post to" list (network avatar, checkbox and a live character counter per
// account, per that network's limit - the caption limit when media is
// attached), the scheduled queue and the recent posts. Pure view - the app
// supplies accounts and handles posting.
// Version: 0.2.0 - UltraMail's look: toolbar, cards, network avatars, a
//                  flex layout that follows the window
// Version: 0.1.0 (Phase 1)
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraSocialTypes.h"

#include "UltraCanvasBadge.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextArea.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraSocial {

class ComposeView {
public:
    // Build the whole view (toolbar + cards). Call once; add the result to
    // the window and size it with Resize().
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build();

    // Size the view to the window's client area so the cards follow it.
    void Resize(float width, float height);

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Container() const { return root_; }

    // Rebuild the target list (avatar + checkbox + counter per account).
    void SetAccounts(const std::vector<Account>& accounts);

    void AddMedia(const std::string& filePath);

    PostDraft CollectDraft() const;
    std::vector<std::string> SelectedAccountIds() const;

    // Disable Post while a publish runs on the worker thread.
    void SetBusy(bool busy);

    // One row of the recent-posts card.
    struct HistoryItem {
        bool succeeded = false;
        SocialNetwork network = SocialNetwork::Mastodon;
        std::string handle;
        std::string text;    // a one-line excerpt of what was sent
        std::string url;     // permalink; empty when the network gives none
        std::string error;   // why it failed (when !succeeded)
        std::string when;    // "14:02" today, "07.10." before
    };
    void SetHistory(const std::vector<HistoryItem>& items);

    // One row of the scheduled card. Its Cancel button fires
    // onCancelScheduled with the entry's id.
    struct ScheduledItem {
        int64_t id = 0;
        SocialNetwork network = SocialNetwork::Mastodon;
        std::string handle;
        std::string when;    // "07.10. 14:00"
        int attempts = 0;    // failed tries so far (shown as "retry n")
        std::string lastError;   // why the last try failed
    };
    void SetScheduled(const std::vector<ScheduledItem>& items);

    void ClearAfterPost();

    std::function<void()> onAddMedia;     // open the file picker
    std::function<void()> onAddAccount;   // open the wizard
    std::function<void()> onPost;
    std::function<void()> onPostLater;    // open the schedule dialog
    std::function<void(int64_t)> onCancelScheduled;
    std::function<void(const std::string&)> onOpenUrl;   // a recent post's link

private:
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildToolbar();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildComposeCard();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildTargetsCard();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildScheduledCard();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildHistoryCard();

    void RebuildMediaChips();
    void UpdateCounters();

    struct TargetRow {
        Account account;
        SocialCapabilities caps;
        std::shared_ptr<UltraCanvas::UltraCanvasCheckbox> checkbox;
        std::shared_ptr<UltraCanvas::UltraCanvasBadge> counter;
    };

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextArea> text_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> length_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> warnings_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> mediaRow_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> targets_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> targetsHint_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> scheduled_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> scheduledCount_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> history_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> postButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> postLaterButton_;
    std::vector<TargetRow> targetRows_;
    std::vector<std::string> mediaPaths_;
};

} // namespace UltraSocial
