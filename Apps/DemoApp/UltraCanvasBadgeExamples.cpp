// Apps/DemoApp/UltraCanvasBadgeExamples.cpp
// Demonstration of UltraCanvasBadge: status pills, count badges (with a max),
// status dots, overlay badges anchored to icon tiles, and an interactive
// counter that shows show-zero suppression and the "99+" cap.
// Version: 1.1.1 - the bell tile carries a two-letter mark like the others
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): one card per
//                  example, badges in wrapping rows, every text sized to fit
// Version: 1.0.0
// Last Modified: 2026-10-24
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasBadge.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"

namespace UltraCanvas {

    // A small square "icon tile" used as an anchor for overlay badges. It is a
    // fixed 52 px square on purpose: a picture with a two-letter mark, not text.
    static std::shared_ptr<UltraCanvasLabel> MakeTile(const std::string& id, float x, float y,
                                                      const std::string& text, const Color& bg) {
        auto tile = CreateLabel(id, x, y, 52, 52);
        tile->SetText(text);
        tile->SetAlignment(TextAlignment::Center);
        tile->SetBackgroundColor(bg);
        tile->SetTextColor(Colors::White);
        tile->SetFontWeight(FontWeight::Bold);
        return tile;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateBadgeExamples() {
        auto page = DemoKit::CreatePage("BadgeExamples", Tr("Badge Examples"),
                Tr("Small count / status indicators: coloured status pills, count badges with a "
                "\"99+\" cap, status dots, and overlay badges anchored to a corner of an icon."));

        // Badges are as tall as their text; centring keeps the row from
        // stretching them to the tallest item.
        auto badgeRow = [](const std::shared_ptr<UltraCanvasContainer>& parent, const std::string& id,
                           float gap) {
            auto row = DemoKit::AddRow(parent, id, gap);
            row->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
            return row;
        };

        auto topRow = DemoKit::AddRow(page, "BgTopRow");

        // ===== 1. STATUS PILLS (variants) =====
        auto card1 = DemoKit::AddCard(topRow, "BgL1", Tr("Status pills (colour variants):"));
        struct V { std::string label; BadgeVariant variant; };
        V variants[] = {
            {Tr("Draft"),   BadgeVariant::Neutral},
            {Tr("New"),     BadgeVariant::Primary},
            {Tr("Active"),  BadgeVariant::Successful},
            {Tr("Pending"), BadgeVariant::Warning},
            {Tr("Error"),   BadgeVariant::Danger},
            {Tr("Beta"),    BadgeVariant::Info},
        };
        auto pillRow = badgeRow(card1, "BgPillRow", 10);
        for (int i = 0; i < 6; ++i) {
            auto b = CreateBadge("BgPill" + std::to_string(i), 0, 0, variants[i].label, variants[i].variant);
            DemoKit::Add(pillRow, b);
        }

        // ===== 2. COUNT BADGES =====
        auto card2 = DemoKit::AddCard(topRow, "BgL2", Tr("Count badges (danger, with 99+ cap):"));
        auto countRow = badgeRow(card2, "BgCountRow", 20);
        int counts[] = {1, 8, 42, 150};
        for (int i = 0; i < 4; ++i) {
            auto b = CreateCountBadge("BgCount" + std::to_string(i), 0, 0, counts[i]);
            DemoKit::Add(countRow, b);
        }
        // A zero count with show-zero off is hidden; with show-zero on it shows "0".
        auto zeroHidden = CreateCountBadge("BgCountZero", 0, 0, 0);
        DemoKit::Add(countRow, zeroHidden);
        auto zeroShown = CreateCountBadge("BgCountZeroShown", 0, 0, 0, BadgeVariant::Neutral);
        zeroShown->SetShowZero(true);
        DemoKit::Add(countRow, zeroShown);

        // ===== 3. STATUS DOTS =====
        auto card3 = DemoKit::AddCard(topRow, "BgL3", Tr("Status dots:"));
        struct D { std::string label; BadgeVariant variant; };
        D dots[] = {
            {Tr("Online"),  BadgeVariant::Successful},
            {Tr("Away"),    BadgeVariant::Warning},
            {Tr("Busy"),    BadgeVariant::Danger},
            {Tr("Offline"), BadgeVariant::Neutral},
        };
        auto dotsRow = DemoKit::AddRow(card3, "BgDotsRow", 24);
        for (int i = 0; i < 4; ++i) {
            auto pair = badgeRow(dotsRow, "BgDotPair" + std::to_string(i), 6);
            auto dot = CreateDotBadge("BgDot" + std::to_string(i), 0, 0, dots[i].variant);
            DemoKit::Add(pair, dot);
            DemoKit::AddText(pair, "BgDotLbl" + std::to_string(i), dots[i].label);
        }

        // An overlay badge straddles its tile's top-right corner and is
        // placed by the badge itself (AnchorTo takes it out of the flow), so
        // it goes into the tile's row with a plain AddChild; the row's top and
        // right padding leave room for the half that sticks out.
        auto tileRow = [](const std::shared_ptr<UltraCanvasContainer>& parent, const std::string& id) {
            auto row = DemoKit::AddRow(parent, id, 24);
            row->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
            row->SetPadding(12, 12, 0, 0);
            return row;
        };

        auto bottomRow = DemoKit::AddRow(page, "BgBottomRow");

        // ===== 4. OVERLAY BADGES (anchored to icon tiles) =====
        auto card4 = DemoKit::AddCard(bottomRow, "BgL4", Tr("Overlay badges anchored to a corner:"));
        auto tiles = tileRow(card4, "BgTileRow");

        auto inbox = MakeTile("BgTileInbox", 0, 0, "In", Color(0, 120, 215));
        DemoKit::AddFixed(tiles, inbox);
        auto inboxBadge = CreateCountBadge("BgInboxBadge", 0, 0, 12);
        inboxBadge->AnchorTo(inbox, BadgeCorner::TopRight, -4, 4);
        tiles->AddChild(inboxBadge);

        auto alerts = MakeTile("BgTileAlerts", 0, 0, "Al", Color(108, 117, 125));
        DemoKit::AddFixed(tiles, alerts);
        auto alertsBadge = CreateCountBadge("BgAlertsBadge", 0, 0, 150);   // -> 99+
        alertsBadge->AnchorTo(alerts, BadgeCorner::TopRight, -4, 4);
        tiles->AddChild(alertsBadge);

        auto chat = MakeTile("BgTileChat", 0, 0, "Ch", Color(40, 167, 69));
        DemoKit::AddFixed(tiles, chat);
        auto chatDot = CreateDotBadge("BgChatDot", 0, 0, BadgeVariant::Danger);   // unread indicator
        chatDot->AnchorTo(chat, BadgeCorner::TopRight, -4, 4);
        tiles->AddChild(chatDot);

        // ===== 5. INTERACTIVE COUNTER =====
        auto card5 = DemoKit::AddCard(bottomRow, "BgL5", Tr("Interactive (Add / Clear the overlay count):"));
        auto bellRow = tileRow(card5, "BgBellRow");
        auto bell = MakeTile("BgTileBell", 0, 0, "Bl", Color(103, 58, 183));
        DemoKit::AddFixed(bellRow, bell);
        auto bellBadge = CreateCountBadge("BgBellBadge", 0, 0, 3);
        bellBadge->AnchorTo(bell, BadgeCorner::TopRight, -4, 4);
        bellRow->AddChild(bellBadge);

        auto counter = std::make_shared<int>(3);
        auto addBtn = std::make_shared<UltraCanvasButton>("BgAdd", 0, 0, 70.0f, 30.0f, Tr("Add"));
        addBtn->onClick = [bellBadge, counter]() { bellBadge->SetCount(++(*counter)); };
        DemoKit::Add(bellRow, addBtn);

        auto clearBtn = std::make_shared<UltraCanvasButton>("BgClear", 0, 0, 70.0f, 30.0f, Tr("Clear"));
        clearBtn->onClick = [bellBadge, counter]() { *counter = 0; bellBadge->SetCount(0); };
        DemoKit::Add(bellRow, clearBtn);

        DemoKit::AddCaption(card5, "BgHint", Tr("Count 0 disappears; over 99 shows \"99+\"."));

        // ===== INSTRUCTIONS =====
        DemoKit::AddNotes(page, "BadgeInstructions", Tr("API:"), {
                Tr("CreateBadge(text, variant) / CreateCountBadge(count) / CreateDotBadge(variant)"),
                Tr("SetCount / SetMaxCount / SetShowZero / SetVariant / SetColor; AnchorTo(element, corner, dx, dy)")});

        return page;
    }

} // namespace UltraCanvas
