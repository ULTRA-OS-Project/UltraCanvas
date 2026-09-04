// Apps/UltraMail/ui/UltraMailAccountBar.cpp
// Version: 0.3.0 - account tiles auto-expand with their counters; counters are
//                  rounded boxes rather than pills.
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailAccountBar.h"

#include "UltraCanvasBadge.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasLabel.h"

#include "UltraMailDiscovery.h"   // EmailLocalPart / EmailDomain

#include <cctype>
#include <string>

using namespace UltraCanvas;

namespace UltraMail {

namespace {

// Counter colours, as in the design: blue = new today, lime = unread before,
// orange = waiting for a reply.
const Color kNewTodayColor(0, 160, 255, 255);
const Color kUnreadColor(170, 255, 0, 255);
const Color kWaitingColor(255, 120, 0, 255);
const Color kFrameColor(20, 20, 20, 255);

constexpr float kLetterSize    = 44.0f;   // provider initial font size
constexpr float kTileLetter    = 40.0f;
constexpr float kNameSize      = 15.0f;
constexpr float kBadgeHeight   = 34.0f;
constexpr float kBadgeFont     = 15.0f;
constexpr float kFrameWidth    = 2.0f;
constexpr float kSummaryRadius = 18.0f;
constexpr float kTileSide      = 176.0f;   // the tile's square baseline (minimum)
constexpr float kTileRadius    = 28.0f;
constexpr float kTileGap       = 20.0f;
constexpr float kBadgeRadius   = 8.0f;     // rounded box, not a pill

std::shared_ptr<UltraCanvasBadge> MakeCounter(const std::string& id, int count,
                                              const Color& color, bool darkText) {
    auto badge = CreateCountBadge(id, 0, 0, count);
    badge->SetColor(color);
    badge->SetMaxCount(9999);
    badge->SetShowZero(true);
    BadgeStyle style;
    style.height    = kBadgeHeight;
    style.minWidth  = kBadgeHeight;
    style.paddingH  = 10.0f;
    style.cornerRadius = kBadgeRadius;
    style.fontSize  = kBadgeFont;
    style.textColor = darkText ? Color(20, 20, 20, 255) : Colors::White;
    badge->SetStyle(style);
    return badge;
}

std::shared_ptr<UltraCanvasLabel> MakeLetter(const std::string& id, const std::string& email,
                                             float fontSize) {
    auto letter = CreateLabel(id, ProviderLetter(email));
    letter->SetFontSize(fontSize);
    letter->SetFontWeight(FontWeight::Bold);
    letter->SetAlignment(TextAlignment::Center);
    letter->SetTooltip(email);
    return letter;
}

std::shared_ptr<UltraCanvasLabel> MakeName(const std::string& id, const std::string& email) {
    // The local part only; the full address is one hover away.
    auto name = CreateLabel(id, EmailLocalPart(email) + "@");
    name->SetFontSize(kNameSize);
    name->SetAlignment(TextAlignment::Center);
    name->SetTooltip(email);
    return name;
}

// A square account tile; a click anywhere on it selects the account.
class AccountTile : public UltraCanvasContainer {
public:
    AccountTile(const std::string& id, std::function<void()> onSelect)
        : UltraCanvasContainer(id, 0, 0, 0, 0), onSelect_(std::move(onSelect)) {
        // Width is left AUTO so the tile grows with its counters as the numbers
        // get longer (the counter badges auto-size to their text). A fixed width
        // would clip them: containers clip children to their content area.
        // The square baseline is kept as a *minimum* width plus a fixed height —
        // a flex container honours an item's boxConstraints on the main axis
        // only, so the height must be an explicit size rather than a min.
        size.height = CSSLayout::Dimension::Px(kTileSide);
        CSSLayout::BoxConstraints limits;
        limits.minWidth = CSSLayout::Dimension::Px(kTileSide);
        boxConstraints = limits;
    }
    bool OnEvent(const UCEvent& event) override {
        if (!IsVisible() || IsDisabled()) return false;
        if (event.type == UCEventType::MouseDown && event.button == UCMouseButton::Left) {
            if (onSelect_) onSelect_();
            return true;
        }
        return UltraCanvasContainer::OnEvent(event);
    }
private:
    std::function<void()> onSelect_;
};

} // namespace

std::string ProviderLetter(const std::string& email) {
    for (char c : EmailDomain(email))
        if (std::isalnum(static_cast<unsigned char>(c)))
            return std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    return "?";
}

std::shared_ptr<UltraCanvasContainer> AccountBar::Build() {
    root_ = CreateContainer("accountBar", 0, 0, 0, 0);
    root_->layout.SetFlexRow()
                 .SetFlexGap(kTileGap)
                 .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    return root_;
}

const AccountStatus& AccountBar::StatusFor(const std::vector<AccountStatus>& status,
                                           const std::string& accountId) {
    static const AccountStatus kEmpty;
    for (const auto& s : status)
        if (s.accountId == accountId) return s;
    return kEmpty;
}

void AccountBar::Rebuild(const std::vector<Account>& accounts,
                         const std::vector<AccountStatus>& status,
                         const std::string& selectedAccountId) {
    if (!root_) Build();
    root_->ClearChildren();
    if (accounts.empty()) return;
    if (accounts.size() == 1)
        BuildSummary(accounts.front(), StatusFor(status, accounts.front().accountId));
    else
        BuildTiles(accounts, status, selectedAccountId);
}

void AccountBar::BuildSummary(const Account& account, const AccountStatus& status) {
    const std::string& acc = account.accountId;

    auto strip = CreateContainer("acctSummary_" + acc, 0, 0, 0, 0);
    strip->SetBorders(kFrameWidth, kFrameColor, kSummaryRadius);
    strip->SetPadding(8, 24);
    strip->layout.SetFlexRow()
                 .SetFlexGap(28)
                 .SetFlexAlignItems(CSSLayout::AlignItems::Center);

    strip->AddChild(MakeLetter("acctLetter_" + acc, account.email, kLetterSize));
    strip->AddChild(MakeName("acctName_" + acc, account.email));

    auto addStat = [&](const std::string& key, int count, const Color& color,
                       bool darkText, const std::string& caption) {
        auto stat = CreateContainer("acctStat_" + key + "_" + acc, 0, 0, 0, 0);
        stat->layout.SetFlexRow()
                    .SetFlexGap(14)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        stat->AddChild(MakeCounter("acctBadge_" + key + "_" + acc, count, color, darkText));
        auto label = CreateLabel("acctCaption_" + key + "_" + acc, caption);
        label->SetFontSize(kNameSize);
        stat->AddChild(label);
        strip->AddChild(stat);
    };
    addStat("today",   status.unreadToday, kNewTodayColor, false, "New today");
    addStat("older",   status.unreadOlder, kUnreadColor,   true,  "Unread");
    addStat("waiting", status.needsAnswer, kWaitingColor,  false, "Waiting for reply");

    root_->AddChild(strip);
    strip->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
}

void AccountBar::BuildTiles(const std::vector<Account>& accounts,
                            const std::vector<AccountStatus>& status,
                            const std::string& selectedAccountId) {
    for (const auto& account : accounts) {
        const std::string acc = account.accountId;
        const AccountStatus& st = StatusFor(status, acc);
        const bool selected = (acc == selectedAccountId);

        auto tile = std::make_shared<AccountTile>("acctTile_" + acc, [this, acc]() {
            if (onSelectAccount) onSelectAccount(acc);
        });
        tile->SetBorders(selected ? kFrameWidth + 1 : kFrameWidth,
                         selected ? Colors::Selection : kFrameColor, kTileRadius);
        tile->SetPadding(10);
        tile->layout.SetFlexColumn()
                    .SetFlexGap(10)
                    .SetFlexJustifyContent(CSSLayout::JustifyContent::Center)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        tile->SetTooltip(account.email);

        tile->AddChild(MakeLetter("acctLetter_" + acc, account.email, kTileLetter));
        tile->AddChild(MakeName("acctName_" + acc, account.email));

        auto counters = CreateContainer("acctCounters_" + acc, 0, 0, 0, 0);
        counters->layout.SetFlexRow()
                        .SetFlexGap(6)
                        .SetFlexJustifyContent(CSSLayout::JustifyContent::Center)
                        .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        auto today = MakeCounter("acctBadge_today_" + acc, st.unreadToday, kNewTodayColor, false);
        today->SetTooltip("New today");
        auto older = MakeCounter("acctBadge_older_" + acc, st.unreadOlder, kUnreadColor, true);
        older->SetTooltip("Unread (before today)");
        auto waiting = MakeCounter("acctBadge_waiting_" + acc, st.needsAnswer, kWaitingColor, false);
        waiting->SetTooltip("Waiting for reply");
        counters->AddChild(today);
        counters->AddChild(older);
        counters->AddChild(waiting);
        tile->AddChild(counters);

        root_->AddChild(tile);
    }
}

} // namespace UltraMail
