// Apps/UltraSocial/ui/UltraSocialComposeView.h
// The main compose surface: one text area, media chips, a per-account
// target list with live character counters (per network's limit, switching
// to the caption limit when media is attached), adaptation warnings, the
// Post button, and the recent-history strip. Pure view — the app supplies
// accounts and handles posting.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraSocialTypes.h"

#include "UltraCanvasBadge.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextArea.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraSocial {

class ComposeView {
public:
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build();

    // Rebuild the target list (checkbox + counter per account).
    void SetAccounts(const std::vector<Account>& accounts);

    void AddMedia(const std::string& filePath);

    PostDraft CollectDraft() const;
    std::vector<std::string> SelectedAccountIds() const;

    // Disable Post while a publish runs on the worker thread.
    void SetBusy(bool busy);

    // Replace the history strip with the given display lines.
    void SetHistoryLines(const std::vector<std::string>& lines);

    // Replace the scheduled strip: one closable chip per queued post.
    // Closing a chip fires onCancelScheduled with the entry's id.
    struct ScheduledItem {
        int64_t id = 0;
        std::string label;
    };
    void SetScheduled(const std::vector<ScheduledItem>& items);

    void ClearAfterPost();

    std::function<void()> onAddMedia;     // open the file picker
    std::function<void()> onAddAccount;   // open the wizard
    std::function<void()> onPost;
    std::function<void()> onPostLater;    // open the schedule dialog
    std::function<void(int64_t)> onCancelScheduled;

private:
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
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> warnings_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> mediaRow_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> targets_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> scheduled_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> history_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> postButton_;
    std::vector<TargetRow> targetRows_;
    std::vector<std::string> mediaPaths_;
};

} // namespace UltraSocial
