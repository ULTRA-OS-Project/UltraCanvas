// Apps/UltraMail/ui/UltraMailAccountBar.h
// The account bar at the top of the main window. With one account it is a
// single summary strip: provider letter, account name, and three counters
// (new today · unread before · waiting for reply). With several accounts it is
// a row of card tiles carrying the same information, one per account; the
// selected tile drives the mail view below. The tiles are the items of an
// UltraCanvasToolbar with item reordering on, so a tile dragged sideways
// takes another place, and onReorderAccounts gives the new order.
//
// The three counters are filters: a click on one shows that account's inbox
// narrowed to what it counts (onFilterAccount), a click anywhere else on the
// card shows all of it again (onSelectAccount). While the list is filtered, a
// second row holds a card of the same look under the account's card, naming
// the filter, with a "Show all emails" button (onShowAll).
// Version: 0.6.0 - the counters filter the list (onFilterAccount); ShowFilter: the
//                  filter's card in a row under the account's, with "Show all
//                  emails" (onShowAll); the summary card is a click target too
// Version: 0.5.0 - the tiles can be dragged into another order (onReorderAccounts)
// Version: 0.4.0 - SetSelected: the highlight moves without rebuilding the tiles
//                  (a rebuild from a tile's own click destroyed that tile, and
//                  the account id its handler was holding, mid-click)
// Version: 0.3.0
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasToolbar.h"

#include "UltraMailMessageFilter.h"
#include "UltraMailTypes.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace UltraMail {

// The provider's initial: first letter of the domain, upper-cased ("G" for
// erika@gmail.com). Empty address → "?".
std::string ProviderLetter(const std::string& email);

class AccountBar {
public:
    // Build the (empty) bar container. Call once; add the result to the window.
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build();

    // Repopulate: one summary strip for a single account, a tile per account
    // otherwise. `selectedAccountId` marks the tile the mail view shows.
    void Rebuild(const std::vector<Account>& accounts,
                 const std::vector<AccountStatus>& status,
                 const std::string& selectedAccountId);

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Container() const { return root_; }

    // Move the highlight to `accountId`'s tile, in place: no tile is rebuilt
    // and the counts stay as they are. For a click on a tile, which must not
    // destroy the tile it came from.
    void SetSelected(const std::string& accountId);

    // Show the filter the list is under: its card under the account's card,
    // and the counter it came from marked. A filter of kind All, or an
    // account the bar does not show, takes the card away. The card is kept
    // and changed in place, never rebuilt: this runs inside the click of its
    // own button. Rebuild keeps it.
    void ShowFilter(const std::string& accountId, const MessageFilter& filter);

    // Fired when an account's card is clicked anywhere but on a counter:
    // show the account, all of its mail (a tile, or the single summary card).
    std::function<void(const std::string& accountId)> onSelectAccount;
    // Fired when a counter is clicked: show the account's inbox narrowed to
    // what the counter counts - NewToday, UnreadOlder or NeedsAnswer.
    std::function<void(const std::string& accountId, MessageFilterKind kind)> onFilterAccount;
    // Fired by the filter card's "Show all emails" button.
    std::function<void()> onShowAll;
    // Fired when a tile was dragged to another place: every account id, in
    // the tiles' new order. The tiles are in it already.
    std::function<void(const std::vector<std::string>& accountIds)> onReorderAccounts;

    // The row that holds the tiles (multi-account mode; null otherwise).
    std::shared_ptr<UltraCanvas::UltraCanvasToolbar> TileRow() const { return tileRow_; }
    // The counter of `accountId` that filters by `kind` (null when none).
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Counter(const std::string& accountId,
                                                               MessageFilterKind kind) const;
    // The second row's filter card and its button (hidden without a filter).
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> FilterCard() const { return filterCard_; }
    std::shared_ptr<UltraCanvas::UltraCanvasButton> ShowAllButton() const { return showAll_; }

private:
    static const AccountStatus& StatusFor(const std::vector<AccountStatus>& status,
                                          const std::string& accountId);
    void BuildSummary(const Account& account, const AccountStatus& status);
    void BuildTiles(const std::vector<Account>& accounts,
                    const std::vector<AccountStatus>& status,
                    const std::string& selectedAccountId);

    void BuildFilterRow();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> MakeCounter(
        const std::string& accountId, MessageFilterKind kind, int count,
        const std::string& caption, float height);
    void MarkActiveCounter();
    // How far from the bar's left edge the account's card starts.
    float CardOffset(const std::string& accountId) const;

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root_;    // a column: the two rows
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> cardRow_; // the account cards
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> summary_; // single-account card
    // The second row: a spacer that puts the filter card under the account's.
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> filterRow_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> filterSpacer_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> filterCard_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     filterTitle_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel>     filterAccount_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton>    showAll_;
    float                                              filterOffset_ = 0.0f;
    std::string                                        filterAccountId_;
    MessageFilter                                      filter_;
    std::vector<Account>                               accounts_;
    // The counters by account and kind, for the mark of the active one.
    std::map<std::pair<std::string, MessageFilterKind>,
             std::shared_ptr<UltraCanvas::UltraCanvasContainer>> counters_;
    std::shared_ptr<UltraCanvas::UltraCanvasToolbar>   tileRow_;
    // The tiles by account (multi-account mode), for SetSelected.
    std::map<std::string, std::shared_ptr<UltraCanvas::UltraCanvasContainer>> tiles_;
};

} // namespace UltraMail
