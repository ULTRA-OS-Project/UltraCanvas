// Apps/UltraMail/ui/UltraMailAccountBar.cpp
// Version: 0.4.0 - the summary strip and the tiles are white cards on the
//                  page: initial in a tinted avatar square, local part as the
//                  name, and the three counters as tinted count · caption
//                  pills instead of saturated badges.
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailAccountBar.h"

#include "UltraCanvasLabel.h"
#include "UltraMailTheme.h"

#include "UltraMailDiscovery.h"   // EmailLocalPart / EmailDomain

#include <cctype>
#include <string>

using namespace UltraCanvas;

namespace UltraMail {

namespace {

constexpr float kTileMinWidth = 168.0f;
constexpr float kTileAvatar   = 36.0f;
constexpr float kPillHeight   = 28.0f;
constexpr float kTilePill     = 24.0f;
constexpr float kCountSize    = 15.0f;

// A tinted "count · caption" pill. With an empty caption it is a compact
// count-only pill (the tiles), with the caption in the tooltip.
std::shared_ptr<UltraCanvasContainer> MakePill(const std::string& id, int count,
                                               const std::string& caption,
                                               const Color& tint, const Color& text,
                                               float height) {
    auto pill = CreateContainer(id, 0, 0, 0, height);
    pill->SetBackgroundColor(tint);
    pill->SetBorders(0.0f, Colors::Transparent, height * 0.5f);
    pill->SetPadding(0, 12);
    pill->layout.SetFlexRow()
                .SetFlexGap(6)
                .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    auto number = Theme::MakeText(id + ".n", std::to_string(count), kCountSize, text,
                                  FontWeight::Bold);
    pill->AddChild(number);
    if (!caption.empty())
        pill->AddChild(Theme::MakeText(id + ".c", caption, Theme::kSizeSecondary, text));
    return pill;
}

std::shared_ptr<UltraCanvasLabel> MakeName(const std::string& id, const std::string& email,
                                           float size) {
    // The local part only; the full address is one hover away.
    auto name = Theme::MakeText(id, EmailLocalPart(email), size, Theme::kTextPrimary,
                                FontWeight::Bold);
    name->SetTooltip(email);
    return name;
}

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
                 .SetFlexGap(Theme::kGap)
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

    // [avatar] name / address ............ [n New today] [n Unread] [n Waiting]
    auto card = CreateContainer("acctSummary_" + acc, 0, 0, 0, 0);
    Theme::ApplyCard(card);
    card->SetPadding(10, 16);
    card->layout.SetFlexRow()
                .SetFlexGap(14)
                .SetFlexAlignItems(CSSLayout::AlignItems::Center);

    auto avatar = Theme::MakeAvatar("acctAvatar_" + acc, ProviderLetter(account.email));
    avatar->SetTooltip(account.email);
    card->AddChild(avatar);

    auto who = CreateContainer("acctWho_" + acc, 0, 0, 0, 0);
    who->layout.SetFlexColumn()
               .SetFlexGap(2)
               .SetFlexJustifyContent(CSSLayout::JustifyContent::Center);
    who->AddChild(MakeName("acctName_" + acc, account.email, Theme::kSizeHeading));
    auto domain = Theme::MakeText("acctDomain_" + acc, "@" + EmailDomain(account.email),
                                  Theme::kSizeSecondary, Theme::kTextSecondary);
    domain->SetTooltip(account.email);
    who->AddChild(domain);
    card->AddChild(who);

    card->AddStretchSpacer(1);

    card->AddChild(MakePill("acctPill_today_" + acc, status.unreadToday, "New today",
                            Theme::kNewTodayTint, Theme::kNewTodayText, kPillHeight));
    card->AddChild(MakePill("acctPill_older_" + acc, status.unreadOlder, "Unread",
                            Theme::kUnreadTint, Theme::kUnreadText, kPillHeight));
    card->AddChild(MakePill("acctPill_waiting_" + acc, status.needsAnswer, "Waiting for reply",
                            Theme::kWaitingTint, Theme::kWaitingText, kPillHeight));

    root_->AddChild(card);
    card->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
}

void AccountBar::BuildTiles(const std::vector<Account>& accounts,
                            const std::vector<AccountStatus>& status,
                            const std::string& selectedAccountId) {
    for (const auto& account : accounts) {
        const std::string acc = account.accountId;
        const AccountStatus& st = StatusFor(status, acc);
        const bool selected = (acc == selectedAccountId);

        // Width is left AUTO so a tile grows with its counters; the card
        // baseline is a minimum width (a flex row honours boxConstraints on
        // its main axis).
        auto tile = std::make_shared<Theme::ClickSurface>("acctTile_" + acc, [this, acc]() {
            if (onSelectAccount) onSelectAccount(acc);
        });
        CSSLayout::BoxConstraints limits;
        limits.minWidth = CSSLayout::Dimension::Px(kTileMinWidth);
        tile->boxConstraints = limits;
        tile->SetBackgroundColor(selected ? Theme::kAccentSoft : Theme::kCardBackground);
        tile->SetBorders(selected ? 2.0f : 1.0f,
                         selected ? Theme::kAccent : Theme::kCardBorder, Theme::kCardRadius);
        tile->SetPadding(12, 14);
        tile->layout.SetFlexColumn()
                    .SetFlexGap(10)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        tile->SetTooltip(account.email);

        // Head: avatar beside the name and domain.
        auto head = CreateContainer("acctHead_" + acc, 0, 0, 0, 0);
        head->layout.SetFlexRow()
                    .SetFlexGap(10)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        head->AddChild(Theme::MakeAvatar("acctAvatar_" + acc, ProviderLetter(account.email),
                                         kTileAvatar));
        auto who = CreateContainer("acctWho_" + acc, 0, 0, 0, 0);
        who->layout.SetFlexColumn().SetFlexGap(1);
        who->AddChild(MakeName("acctName_" + acc, account.email, Theme::kSizeBody));
        who->AddChild(Theme::MakeText("acctDomain_" + acc, "@" + EmailDomain(account.email),
                                      Theme::kSizeSmall, Theme::kTextSecondary));
        head->AddChild(who);
        tile->AddChild(head);

        // Counters: compact pills, captions in their tooltips.
        auto counters = CreateContainer("acctCounters_" + acc, 0, 0, 0, 0);
        counters->layout.SetFlexRow()
                        .SetFlexGap(6)
                        .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        auto today = MakePill("acctPill_today_" + acc, st.unreadToday, "",
                              Theme::kNewTodayTint, Theme::kNewTodayText, kTilePill);
        today->SetTooltip("New today");
        auto older = MakePill("acctPill_older_" + acc, st.unreadOlder, "",
                              Theme::kUnreadTint, Theme::kUnreadText, kTilePill);
        older->SetTooltip("Unread (before today)");
        auto waiting = MakePill("acctPill_waiting_" + acc, st.needsAnswer, "",
                                Theme::kWaitingTint, Theme::kWaitingText, kTilePill);
        waiting->SetTooltip("Waiting for reply");
        counters->AddChild(today);
        counters->AddChild(older);
        counters->AddChild(waiting);
        tile->AddChild(counters);

        root_->AddChild(tile);
    }
}

} // namespace UltraMail
