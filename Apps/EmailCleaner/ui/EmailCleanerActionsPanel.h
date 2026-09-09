// Apps/EmailCleaner/ui/EmailCleanerActionsPanel.h
// The strip above the message list that turns the current selection into
// something done about it: block the sender, unsubscribe, move their mail to
// Trash.
//
// Nothing here decides anything. The three checkboxes build an ActionRequest,
// the engine's ActionPlanner works out what would happen, and the panel shows
// that plan and its warnings continuously — so the consequence is on screen
// before the button is pressed, not after. "Apply" then repeats the plan in a
// confirmation dialog and only runs it when the user says yes.
//
// The panel is also where the blocklist can be seen and undone, because a
// block the user cannot take back is not a block, it is a mistake waiting.
// Version: 0.2.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers first (X11 macro hygiene — see the engine headers).
#include "UltraCanvasContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasLabel.h"

#include "EmailCleanerActions.h"
#include "EmailCleanerStore.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace EmailCleaner {

class ActionsPanel {
public:
    // The height the panel needs; the app reserves this above the detail view.
    static constexpr float kHeight = 122.0f;

    // The store is required (the planner reads it, the executor writes it).
    // The backend is the mail side and may be absent — then blocking still
    // works and the other two steps say why they cannot run.
    void SetStore(AnalysisStore* store)        { store_ = store; }
    void SetBackend(IActionBackend* backend)   { backend_ = backend; }
    // Why the mail side is missing, shown instead of a bare refusal.
    void SetBackendUnavailableReason(const std::string& reason) { noBackend_ = reason; }

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build(float x, float y,
                                                             float width, float height);

    // Follow the map selection. An empty target disables everything.
    void SetTarget(const ActionTarget& target, const std::string& accountId);

    // Re-plan and repaint from the database as it is now.
    void Refresh();

    // Raised after a plan ran, so the app can re-read the corpus and repaint.
    std::function<void(const ActionOutcome&)> onApplied;

private:
    ActionRequest CurrentRequest() const;
    void          UpdatePlan();
    void          Confirm();
    void          Apply(const ActionPlan& plan);
    void          ShowBlocklist();

    AnalysisStore*  store_   = nullptr;
    IActionBackend* backend_ = nullptr;
    std::string     noBackend_ = "No mail account is connected.";

    ActionTarget target_;
    std::string  accountId_;
    ActionPlan   plan_;
    // The plan's warnings plus anything the panel itself has to add (a missing
    // mail connection). Shown on the strip and repeated in the confirmation.
    std::vector<std::string> warnings_;

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root_;
    std::shared_ptr<UltraCanvas::UltraCanvasCheckbox>  block_;
    std::shared_ptr<UltraCanvas::UltraCanvasCheckbox>  unsubscribe_;
    std::shared_ptr<UltraCanvas::UltraCanvasCheckbox>  deleteMail_;
    std::shared_ptr<UltraCanvas::UltraCanvasCheckbox>  unwantedOnly_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    apply_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    blocklist_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     summary_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     warning_;
};

} // namespace EmailCleaner
