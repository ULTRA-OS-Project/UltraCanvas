// Apps/UltraMail/ui/UltraMailAccountBar.cpp
// Version: 0.7.0 - the counters are click targets of their own that filter the
//                  list (onFilterAccount), the active one framed in its colour;
//                  the rest of a card shows all mail (onSelectAccount, the
//                  summary card too); the filter's card in a second row under
//                  the account's, with "Show all emails" (ShowFilter, onShowAll)
// Version: 0.6.0 - the tiles sit in a toolbar that lets them be dragged into
//                  another order (onReorderAccounts)
// Version: 0.5.0 - a click anywhere on a tile selects its account (its text
//                  and counters took the click before, and only the tile's
//                  padding answered); the counters' captions are in the
//                  tile's tooltip, beside the address.
// Version: 0.4.0 - the summary strip and the tiles are white cards on the
//                  page: initial in a tinted avatar square, local part as the
//                  name, and the three counters as tinted count · caption
//                  pills instead of saturated badges.
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailAccountBar.h"

#include "UltraCanvasLabel.h"
#include "UltraMailTheme.h"

#include "UltraMailDiscovery.h"   // EmailLocalPart / EmailDomain

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

using namespace UltraCanvas;

namespace UltraMail {

namespace {

constexpr float kTileMinWidth = 136.0f;
constexpr float kTileAvatar   = 26.0f;
constexpr float kPillHeight   = 20.0f;
constexpr float kTilePill     = 18.0f;
constexpr float kCountSize    = 11.0f;

// A counter's colours, by the filter it applies.
const Color& CounterTint(MessageFilterKind kind) {
    return kind == MessageFilterKind::NewToday    ? Theme::kNewTodayTint
         : kind == MessageFilterKind::Unread      ? Theme::kUnreadTint
                                                  : Theme::kWaitingTint;
}
const Color& CounterText(MessageFilterKind kind) {
    return kind == MessageFilterKind::NewToday    ? Theme::kNewTodayText
         : kind == MessageFilterKind::Unread      ? Theme::kUnreadText
                                                  : Theme::kWaitingText;
}

// The second row. Where the account's card sits is known only once the bar
// is laid out (the tiles are as wide as their content, and can be dragged
// into another order), so before each paint the row measures it and moves
// the filter card under it - a layout more when it moved, none otherwise.
class AlignedRow : public UltraCanvasContainer {
public:
    using UltraCanvasContainer::UltraCanvasContainer;
    std::function<void()> beforeRender;
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override {
        if (beforeRender) beforeRender();
        UltraCanvasContainer::Render(ctx, dirtyRect);
    }
};

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
    // A column: the account cards, and under them - while the list is
    // filtered - the filter's card.
    root_ = CreateContainer("accountBar", 0, 0, 0, 0);
    root_->layout.SetFlexColumn()
                 .SetFlexGap(Theme::kGap)
                 .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    cardRow_ = CreateContainer("accountCards", 0, 0, 0, 0);
    cardRow_->layout.SetFlexRow()
                    .SetFlexGap(Theme::kGap)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    root_->AddChild(cardRow_);
    cardRow_->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    BuildFilterRow();
    return root_;
}

void AccountBar::BuildFilterRow() {
    auto row = std::make_shared<AlignedRow>("acctFilterRow", 0, 0, 0, 0);
    row->beforeRender = [this]() {
        if (!filterRow_ || !filterRow_->IsVisible() || !filterSpacer_) return;
        const float want = std::max(0.0f, CardOffset(filterAccountId_));
        if (std::fabs(want - filterOffset_) < 0.5f) return;
        filterOffset_ = want;
        CSSLayout::BoxConstraints width;
        width.minWidth = width.maxWidth = CSSLayout::Dimension::Px(want);
        filterSpacer_->boxConstraints = width;
        filterSpacer_->InvalidateLayout();
        filterRow_->InvalidateLayout();
        root_->InvalidateLayout();
    };
    filterRow_ = row;
    filterRow_->layout.SetFlexRow()
                      .SetFlexGap(0)
                      .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    filterSpacer_ = CreateContainer("acctFilterSpacer", 0, 0, 0, 0);
    filterSpacer_->SetInteractive(false);
    filterSpacer_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    filterRow_->AddChild(filterSpacer_);

    // The card: the look of an account tile, with what the list is showing
    // and the way back to all of it.
    filterCard_ = CreateContainer("acctFilterCard", 0, 0, 0, 0);
    CSSLayout::BoxConstraints limits;
    limits.minWidth = CSSLayout::Dimension::Px(kTileMinWidth);
    filterCard_->boxConstraints = limits;
    filterCard_->SetBackgroundColor(Theme::kCardBackground);
    filterCard_->SetBorders(1.0f, Theme::kCardBorder, Theme::kCardRadius);
    filterCard_->SetPadding(10, 14);
    filterCard_->layout.SetFlexRow()
                       .SetFlexGap(14)
                       .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    auto what = CreateContainer("acctFilterWhat", 0, 0, 0, 0);
    what->layout.SetFlexColumn().SetFlexGap(1).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    filterTitle_ = Theme::MakeText("acctFilterTitle", "", Theme::kSizeBody, Theme::kTextPrimary,
                                   FontWeight::Bold);
    filterAccount_ = Theme::MakeText("acctFilterAccount", "", Theme::kSizeSmall,
                                     Theme::kTextSecondary);
    what->AddChild(filterTitle_);
    what->AddChild(filterAccount_);
    filterCard_->AddChild(what);
    showAll_ = CreateButton("acctFilterShowAll", 0, 0, 120, Theme::kControlHeight,
                            "Show all emails");
    Theme::FitToLabel(showAll_, 120);
    Theme::StyleSecondary(showAll_);
    showAll_->SetTooltip("Take the filter away and show every message of the folder");
    showAll_->onClick = [this]() { if (onShowAll) onShowAll(); };
    showAll_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    filterCard_->AddChild(showAll_);
    filterRow_->AddChild(filterCard_);
    filterCard_->layoutItem.SetFlexGrow(0).SetFlexShrink(0);

    root_->AddChild(filterRow_);
    filterRow_->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                          .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    filterRow_->SetVisible(false);
}

float AccountBar::CardOffset(const std::string& accountId) const {
    if (!filterRow_) return 0.0f;
    std::shared_ptr<UltraCanvasContainer> card;
    if (const auto it = tiles_.find(accountId); it != tiles_.end()) card = it->second;
    else card = summary_;
    if (!card) return 0.0f;
    return card->GetBoundsInWindow().x - filterRow_->GetBoundsInWindow().x;
}

std::shared_ptr<UltraCanvasContainer> AccountBar::Counter(const std::string& accountId,
                                                          MessageFilterKind kind) const {
    const auto it = counters_.find({accountId, kind});
    return it == counters_.end() ? nullptr : it->second;
}

std::shared_ptr<UltraCanvasContainer> AccountBar::MakeCounter(const std::string& accountId,
                                                              MessageFilterKind kind, int count,
                                                              const std::string& caption,
                                                              float height) {
    const std::string acc = accountId;
    const char* id = kind == MessageFilterKind::NewToday    ? "acctPill_today_"
                   : kind == MessageFilterKind::Unread      ? "acctPill_older_"
                                                            : "acctPill_waiting_";
    // A click target of its own on the card: it filters, the card around it
    // shows everything. Its number and caption take no pointer.
    auto pill = std::make_shared<Theme::ClickSurface>(id + acc, [this, acc, kind]() {
        const std::string account = acc;   // a copy: see the tiles' handler
        if (onFilterAccount) onFilterAccount(account, kind);
    });
    pill->SetBackgroundColor(CounterTint(kind));
    pill->SetBorders(0.0f, Colors::Transparent, height * 0.5f);
    pill->SetPadding(0, 12);
    pill->layout.SetFlexRow()
                .SetFlexGap(6)
                .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    CSSLayout::BoxConstraints tall;
    tall.minHeight = tall.maxHeight = CSSLayout::Dimension::Px(height);
    pill->boxConstraints = tall;
    pill->AddChild(Theme::MakeText(id + acc + ".n", std::to_string(count), kCountSize,
                                   CounterText(kind), FontWeight::Bold));
    if (!caption.empty())
        pill->AddChild(Theme::MakeText(id + acc + ".c", caption, Theme::kSizeSecondary,
                                       CounterText(kind)));
    pill->PassPointerThroughContent();
    // "Unread" counts the unread mail from before today (today's is the
    // first counter), but a click shows all of it, today's too.
    pill->SetTooltip(
        kind == MessageFilterKind::NewToday
            ? std::to_string(count) + " unread messages from today - click to show only these"
        : kind == MessageFilterKind::Unread
            ? std::to_string(count) + " unread messages from before today - click to show "
              "all unread mail"
            : std::to_string(count) + " messages waiting for your reply - click to show only "
              "these");
    counters_[{acc, kind}] = pill;
    return pill;
}

void AccountBar::MarkActiveCounter() {
    for (const auto& [key, pill] : counters_) {
        const bool active = filter_.Active() && key.first == filterAccountId_ &&
                            key.second == filter_.kind;
        const float radius = pill->GetHeight() > 0 ? pill->GetHeight() * 0.5f : kTilePill * 0.5f;
        pill->SetBorders(active ? 1.5f : 0.0f,
                         active ? CounterText(key.second) : Colors::Transparent, radius);
        pill->RequestRedraw();
    }
}

void AccountBar::ShowFilter(const std::string& accountId, const MessageFilter& filter) {
    filter_ = filter;
    filterAccountId_ = accountId;
    const Account* account = nullptr;
    for (const auto& a : accounts_) if (a.accountId == accountId) account = &a;
    const bool show = filter.Active() && account != nullptr;
    MarkActiveCounter();
    if (!filterRow_) return;
    if (show) {
        filterTitle_->SetText("Showing: " + Describe(filter));
        filterAccount_->SetText(account->email);
        filterOffset_ = -1.0f;   // measured again before the next paint
    }
    if (filterRow_->IsVisible() != show) {
        filterRow_->SetVisible(show);
        root_->InvalidateLayout();
    }
    filterRow_->RequestRedraw();
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
    cardRow_->ClearChildren();
    tiles_.clear();
    counters_.clear();
    tileRow_.reset();
    summary_.reset();
    accounts_ = accounts;
    if (!accounts.empty()) {
        if (accounts.size() == 1)
            BuildSummary(accounts.front(), StatusFor(status, accounts.front().accountId));
        else
            BuildTiles(accounts, status, selectedAccountId);
    }
    // The filter card stays, under the account's new card (or goes with it).
    ShowFilter(filterAccountId_, filter_);
}

void AccountBar::BuildSummary(const Account& account, const AccountStatus& status) {
    const std::string& acc = account.accountId;

    // [avatar] name / address ............ [n New today] [n Unread] [n Waiting]
    // A click on the card but not on a counter: all of the account's mail.
    auto card = std::make_shared<Theme::ClickSurface>("acctSummary_" + acc, [this, acc]() {
        const std::string id = acc;
        if (onSelectAccount) onSelectAccount(id);
    });
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
               .SetFlexJustifyContent(CSSLayout::JustifyContent::Center)
               .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    who->AddChild(MakeName("acctName_" + acc, account.email, Theme::kSizeHeading));
    auto domain = Theme::MakeText("acctDomain_" + acc, "@" + EmailDomain(account.email),
                                  Theme::kSizeSecondary, Theme::kTextSecondary);
    domain->SetTooltip(account.email);
    who->AddChild(domain);
    card->AddChild(who);

    card->AddStretchSpacer(1);

    // The avatar and the name are the card's: one target with them.
    avatar->SetInteractive(false);
    who->SetInteractive(false);

    card->AddChild(MakeCounter(acc, MessageFilterKind::NewToday, status.unreadToday,
                               "New today", kPillHeight));
    card->AddChild(MakeCounter(acc, MessageFilterKind::Unread, status.unreadOlder,
                               "Unread", kPillHeight));
    card->AddChild(MakeCounter(acc, MessageFilterKind::NeedsAnswer, status.needsAnswer,
                               "Waiting for reply", kPillHeight));

    cardRow_->AddChild(card);
    card->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    summary_ = card;
}

void AccountBar::BuildTiles(const std::vector<Account>& accounts,
                            const std::vector<AccountStatus>& status,
                            const std::string& selectedAccountId) {
    // The tiles are a toolbar's items, for its drag into another order; the
    // toolbar itself shows nothing of its own - no fill, frame or padding -
    // and keeps the bar's gap between the tiles, which stretch to one height.
    tileRow_ = std::make_shared<UltraCanvasToolbar>("acctTiles", 0, 0, 0, 0);
    ToolbarAppearance look = ToolbarAppearance::Flat();
    look.backgroundColor = Colors::Transparent;
    look.itemSpacing = Theme::kGap;
    tileRow_->SetAppearance(look);
    tileRow_->SetBorders(0.0f, Colors::Transparent);
    tileRow_->SetPadding(0);
    tileRow_->layout.SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    tileRow_->EnableItemReordering(true);
    tileRow_->onItemReordered = [this](int, int) {
        if (!tileRow_) return;
        std::vector<std::string> order;
        for (const auto& item : tileRow_->GetItems())
            for (const auto& [acc, tile] : tiles_)
                if (tile.get() == item.get()) order.push_back(acc);
        if (onReorderAccounts) onReorderAccounts(order);
        if (filterRow_) filterRow_->RequestRedraw();   // its card follows the tile
    };
    cardRow_->AddChild(tileRow_);
    tileRow_->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    for (const auto& account : accounts) {
        const std::string acc = account.accountId;
        const AccountStatus& st = StatusFor(status, acc);
        const bool selected = (acc == selectedAccountId);

        // Width is left AUTO so a tile grows with its counters; the card
        // baseline is a minimum width (a flex row honours boxConstraints on
        // its main axis).
        auto tile = std::make_shared<Theme::ClickSurface>("acctTile_" + acc, [this, acc]() {
            // A copy: whatever the handler does to the bar, the id it was
            // given stays valid (the captured one lives in this tile).
            const std::string id = acc;
            if (onSelectAccount) onSelectAccount(id);
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
        // The tile is one target but for its counters, which filter (each
        // with a tooltip of its own); its tooltip carries the address and
        // what each counter counts.
        TooltipContent tip;
        tip.AddTitle(account.email);
        tip.AddRow(Theme::kNewTodayText, "New today", std::to_string(st.unreadToday));
        tip.AddRow(Theme::kUnreadText, "Unread (before today)", std::to_string(st.unreadOlder));
        tip.AddRow(Theme::kWaitingText, "Waiting for reply", std::to_string(st.needsAnswer));
        tile->SetTooltipContent(tip);

        // Head: avatar beside the name and domain.
        auto head = CreateContainer("acctHead_" + acc, 0, 0, 0, 0);
        head->layout.SetFlexRow()
                    .SetFlexGap(10)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        head->AddChild(Theme::MakeAvatar("acctAvatar_" + acc, ProviderLetter(account.email),
                                         kTileAvatar));
        auto who = CreateContainer("acctWho_" + acc, 0, 0, 0, 0);
        who->layout.SetFlexColumn().SetFlexGap(1).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        who->AddChild(MakeName("acctName_" + acc, account.email, Theme::kSizeBody));
        who->AddChild(Theme::MakeText("acctDomain_" + acc, "@" + EmailDomain(account.email),
                                      Theme::kSizeSmall, Theme::kTextSecondary));
        head->AddChild(who);
        tile->AddChild(head);

        // Counters: compact pills, each a filter; captions in the tooltips.
        auto counters = CreateContainer("acctCounters_" + acc, 0, 0, 0, 0);
        counters->layout.SetFlexRow()
                        .SetFlexGap(6)
                        .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        counters->AddChild(MakeCounter(acc, MessageFilterKind::NewToday, st.unreadToday, "",
                                       kTilePill));
        counters->AddChild(MakeCounter(acc, MessageFilterKind::Unread, st.unreadOlder, "",
                                       kTilePill));
        counters->AddChild(MakeCounter(acc, MessageFilterKind::NeedsAnswer, st.needsAnswer, "",
                                       kTilePill));
        tile->AddChild(counters);

        // A click or hover on the name or the avatar is the tile's: one
        // target, with one tooltip. (A press between two counters the row
        // does not take reaches the tile too.)
        head->SetInteractive(false);

        tileRow_->AddChild(tile);
        tiles_[acc] = tile;
    }
}

void AccountBar::SetSelected(const std::string& accountId) {
    for (const auto& [acc, tile] : tiles_) {
        const bool selected = (acc == accountId);
        tile->SetBackgroundColor(selected ? Theme::kAccentSoft : Theme::kCardBackground);
        tile->SetBorders(selected ? 2.0f : 1.0f,
                         selected ? Theme::kAccent : Theme::kCardBorder, Theme::kCardRadius);
        tile->RequestRedraw();
    }
}

} // namespace UltraMail
