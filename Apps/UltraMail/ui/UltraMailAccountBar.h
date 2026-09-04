// Apps/UltraMail/ui/UltraMailAccountBar.h
// The account bar at the top of the main window. With one account it is a
// single summary strip: provider letter, account name, and three counters
// (new today · unread before · waiting for reply). With several accounts it is
// a row of square tiles carrying the same information, one per account; the
// selected tile drives the mail view below.
// Version: 0.2.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasContainer.h"

#include "UltraMailTypes.h"

#include <functional>
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

    // Fired when a tile is clicked (multi-account mode).
    std::function<void(const std::string& accountId)> onSelectAccount;

private:
    static const AccountStatus& StatusFor(const std::vector<AccountStatus>& status,
                                          const std::string& accountId);
    void BuildSummary(const Account& account, const AccountStatus& status);
    void BuildTiles(const std::vector<Account>& accounts,
                    const std::vector<AccountStatus>& status,
                    const std::string& selectedAccountId);

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root_;
};

} // namespace UltraMail
