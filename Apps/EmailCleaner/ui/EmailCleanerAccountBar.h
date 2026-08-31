// Apps/EmailCleaner/ui/EmailCleanerAccountBar.h
// The strip along the top of the window: which account is being looked at,
// the load / re-analyse actions, the filters every view shares (search,
// category, unwanted-only) and the one-line summary of what is in the
// analysis database.
//
// It owns no data — it raises callbacks and the app decides what to do.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

// UltraCanvas UI headers first: they pull in X11 (which defines Bool/Status),
// and the engine headers below undef those macros — so the UI headers must be
// fully processed before the engine headers are seen.
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"

#include "EmailCleanerStore.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace EmailCleaner {

class AccountBar {
public:
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build(float x, float y,
                                                             float width, float height);

    // Repopulate the account dropdown ("All accounts" plus one entry each).
    void SetAccounts(const std::vector<StoredAccount>& accounts);

    // The summary line on the right ("1,204 messages from 87 senders · ...").
    void SetStatus(const std::string& text);

    // The filter the bar currently describes. The app reads this, adds its own
    // sender/domain selection, and hands it to the views.
    const MessageFilter& Filter() const { return filter_; }

    // Raised whenever any control changes the filter.
    std::function<void()> onFilterChanged;
    // "Load mail" — analyse whatever UltraMail has cached for this account.
    std::function<void()> onScan;
    // "Re-analyse" — run the classifier over the stored corpus again.
    std::function<void()> onReanalyse;
    // "Rules…" — open the keyword rule editor.
    std::function<void()> onEditRules;

private:
    void NotifyFilterChanged();

    MessageFilter filter_;
    std::vector<StoredAccount> accounts_;

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown>  accountPicker_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown>  categoryPicker_;
    std::shared_ptr<UltraCanvas::UltraCanvasTextInput> search_;
    std::shared_ptr<UltraCanvas::UltraCanvasCheckbox>  unwantedOnly_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     status_;
};

} // namespace EmailCleaner
