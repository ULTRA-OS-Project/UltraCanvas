// Apps/UltraMail/ui/UltraMailAccountBar.h
// The account bar at the top of the main window. With one account it is a
// single summary strip: provider letter, account name, and three counters
// (new today · unread before · waiting for reply). With several accounts it is
// a row of card tiles carrying the same information, one per account; the
// selected tile drives the mail view below. The tiles are the items of an
// UltraCanvasToolbar with item reordering on, so a tile dragged sideways
// takes another place, and onReorderAccounts gives the new order.
// Version: 0.5.0 - the tiles can be dragged into another order (onReorderAccounts)
// Version: 0.4.0 - SetSelected: the highlight moves without rebuilding the tiles
//                  (a rebuild from a tile's own click destroyed that tile, and
//                  the account id its handler was holding, mid-click)
// Version: 0.3.0
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasToolbar.h"

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

    // Fired when a tile is clicked (multi-account mode).
    std::function<void(const std::string& accountId)> onSelectAccount;
    // Fired when a tile was dragged to another place: every account id, in
    // the tiles' new order. The tiles are in it already.
    std::function<void(const std::vector<std::string>& accountIds)> onReorderAccounts;

    // The row that holds the tiles (multi-account mode; null otherwise).
    std::shared_ptr<UltraCanvas::UltraCanvasToolbar> TileRow() const { return tileRow_; }

private:
    static const AccountStatus& StatusFor(const std::vector<AccountStatus>& status,
                                          const std::string& accountId);
    void BuildSummary(const Account& account, const AccountStatus& status);
    void BuildTiles(const std::vector<Account>& accounts,
                    const std::vector<AccountStatus>& status,
                    const std::string& selectedAccountId);

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root_;
    std::shared_ptr<UltraCanvas::UltraCanvasToolbar>   tileRow_;
    // The tiles by account (multi-account mode), for SetSelected.
    std::map<std::string, std::shared_ptr<UltraCanvas::UltraCanvasContainer>> tiles_;
};

} // namespace UltraMail
