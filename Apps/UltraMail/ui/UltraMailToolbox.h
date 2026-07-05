// Apps/UltraMail/ui/UltraMailToolbox.h
// The Toolbox start screen: a grid of large square tiles, one per configured
// account, plus an always-present "Add email account" tile. At first use the
// grid contains only that single square icon.
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

class Toolbox {
public:
    // Build the (empty) grid container. Call once; add the result to the window.
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build();

    // Repopulate the grid: one square per account (with unread badge), then the
    // "Add email account" square at the end (always present).
    void Rebuild(const std::vector<Account>& accounts,
                 const std::vector<AccountStatus>& status);

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Container() const { return grid_; }

    std::function<void()> onAddAccount;
    std::function<void(const std::string& accountId)> onOpenAccount;

private:
    int UnreadFor(const std::vector<AccountStatus>& status,
                  const std::string& accountId) const;

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> grid_;
};

} // namespace UltraMail
