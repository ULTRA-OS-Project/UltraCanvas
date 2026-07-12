// Apps/UltraMail/ui/UltraMailInfoTileBar.h
// The account info-tile bar: a horizontal strip of compact status tiles, one
// per account, each showing the account's short name plus its unread and
// needs-answer counts. Clicking a tile selects the account; clicking a tile
// with pending replies triggers the needs-answer view.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailTypes.h"

#include "UltraCanvasContainer.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraMail {

class InfoTileBar {
public:
    // Build the (empty) bar container. Call once; add the result to the window.
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build();

    // Repopulate the bar from the latest per-account status rollup.
    void Rebuild(const std::vector<AccountStatus>& status);

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Container() const { return bar_; }

    // Fired when a tile is clicked (account selected).
    std::function<void(const std::string& accountId)> onAccountClicked;
    // Fired when a tile's "needs answer" line is clicked (there is pending mail).
    std::function<void(const std::string& accountId)> onNeedsAnswerClicked;

private:
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> bar_;
};

} // namespace UltraMail
