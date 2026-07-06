// Apps/UltraMail/ui/UltraMailInfoTileBar.cpp
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailInfoTileBar.h"

#include "UltraCanvasLabel.h"

#include <string>

using namespace UltraCanvas;

namespace UltraMail {

namespace {
constexpr float kBarHeight  = 60.0f;
constexpr float kTileWidth  = 160.0f;
constexpr float kTileGap    = 8.0f;
} // namespace

std::shared_ptr<UltraCanvasContainer> InfoTileBar::Build() {
    bar_ = CreateContainer("infoTileBar", 0, 0, 0, kBarHeight);
    return bar_;
}

void InfoTileBar::Rebuild(const std::vector<AccountStatus>& status) {
    if (!bar_) Build();
    bar_->ClearChildren();

    float x = kTileGap;
    for (const auto& s : status) {
        const std::string acc = s.accountId;

        auto tile = CreateContainer("infoTile_" + acc, x, 4, kTileWidth, kBarHeight - 8);

        // Top line: the account's short name.
        auto name = CreateLabel("infoTileName_" + acc, 8, 4, kTileWidth - 16, 20,
                                s.shortName);
        name->onClick = [this, acc]() { if (onAccountClicked) onAccountClicked(acc); };
        tile->AddChild(name);

        // Bottom line: unread + needs-answer, or a calm "all clear" state.
        std::string statusText;
        if (s.unread == 0 && s.needsAnswer == 0) {
            statusText = "✓ all clear";
        } else {
            statusText = "✉ " + std::to_string(s.unread) + " new";
            if (s.needsAnswer > 0)
                statusText += "   ↩ " + std::to_string(s.needsAnswer) + " to answer";
        }
        auto info = CreateLabel("infoTileStatus_" + acc, 8, 26, kTileWidth - 16, 20,
                                statusText);
        const int needs = s.needsAnswer;
        info->onClick = [this, acc, needs]() {
            if (needs > 0 && onNeedsAnswerClicked) onNeedsAnswerClicked(acc);
            else if (onAccountClicked) onAccountClicked(acc);
        };
        tile->AddChild(info);

        bar_->AddChild(tile);
        x += kTileWidth + kTileGap;
    }
}

} // namespace UltraMail
