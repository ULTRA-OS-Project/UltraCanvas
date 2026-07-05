// Apps/UltraMail/ui/UltraMailToolbox.cpp
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailToolbox.h"

#include "UltraCanvasLabel.h"

#include <string>

using namespace UltraCanvas;

namespace UltraMail {

namespace {
constexpr float kTile   = 150.0f;   // square tile side
constexpr float kGap    = 16.0f;
constexpr int   kPerRow = 4;
} // namespace

std::shared_ptr<UltraCanvasContainer> Toolbox::Build() {
    grid_ = CreateContainer("toolboxGrid", 0, 0, 0, 0);
    return grid_;
}

int Toolbox::UnreadFor(const std::vector<AccountStatus>& status,
                       const std::string& accountId) const {
    for (const auto& s : status)
        if (s.accountId == accountId) return s.unread;
    return 0;
}

void Toolbox::Rebuild(const std::vector<Account>& accounts,
                      const std::vector<AccountStatus>& status) {
    if (!grid_) Build();
    grid_->ClearChildren();

    int index = 0;
    auto cellPos = [&](int i, float& x, float& y) {
        x = kGap + static_cast<float>(i % kPerRow) * (kTile + kGap);
        y = kGap + static_cast<float>(i / kPerRow) * (kTile + kGap);
    };

    // One tile per account.
    for (const auto& a : accounts) {
        float x, y; cellPos(index++, x, y);
        const std::string acc = a.accountId;

        auto tile = CreateContainer("acctTile_" + acc, x, y, kTile, kTile);

        auto glyph = CreateLabel("acctGlyph_" + acc, 0, 30, kTile, 48, "✉");
        tile->AddChild(glyph);

        auto name = CreateLabel("acctName_" + acc, 8, 90, kTile - 16, 22, a.shortName);
        tile->AddChild(name);

        int unread = UnreadFor(status, acc);
        if (unread > 0) {
            auto badge = CreateLabel("acctBadge_" + acc, 8, 112, kTile - 16, 20,
                                     std::to_string(unread) + " unread");
            tile->AddChild(badge);
        }

        auto open = [this, acc]() { if (onOpenAccount) onOpenAccount(acc); };
        glyph->onClick = open;
        name->onClick = open;

        grid_->AddChild(tile);
    }

    // The "Add email account" tile is always present, at the end of the grid.
    {
        float x, y; cellPos(index++, x, y);
        auto tile = CreateContainer("addAccountTile", x, y, kTile, kTile);

        auto glyph = CreateLabel("addAccountGlyph", 0, 30, kTile, 48, "✉ +");
        auto label = CreateLabel("addAccountLabel", 8, 90, kTile - 16, 40,
                                 "Add email account");
        auto add = [this]() { if (onAddAccount) onAddAccount(); };
        glyph->onClick = add;
        label->onClick = add;

        tile->AddChild(glyph);
        tile->AddChild(label);
        grid_->AddChild(tile);
    }
}

} // namespace UltraMail
