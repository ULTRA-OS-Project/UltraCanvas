// Apps/DemoApp/UltraCanvasBadgeExamples.cpp
// Demonstration of UltraCanvasBadge: status pills, count badges (with a max),
// status dots, overlay badges anchored to icon tiles, and an interactive
// counter that shows show-zero suppression and the "99+" cap.
// Version: 1.0.0
// Last Modified: 2026-07-07
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasBadge.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"

namespace UltraCanvas {

    // A small square "icon tile" used as an anchor for overlay badges.
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
        auto container = std::make_shared<UltraCanvasContainer>("BadgeExamples", 0, 0, 1000, 600);
        container->SetPadding(0, 0, 10, 0);

        auto title = CreateLabel("BadgeTitle", 20, 10, 0, 30);
        title->SetText("Badge Examples");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto subtitle = CreateLabel("BadgeSubtitle", 20, 42, 940, 40);
        subtitle->SetText("Small count / status indicators: coloured status pills, count badges with a "
                          "\"99+\" cap, status dots, and overlay badges anchored to a corner of an icon.");
        subtitle->SetFontSize(12);
        container->AddChild(subtitle);

        auto section = [&](const std::string& id, const std::string& text, int y) {
            auto l = CreateLabel(id, 20, y, 500, 20);
            l->SetText(text);
            l->SetFontWeight(FontWeight::Bold);
            l->SetFontSize(12);
            container->AddChild(l);
        };

        // ===== 1. STATUS PILLS (variants) =====
        section("BgL1", "Status pills (colour variants):", 92);
        struct V { const char* label; BadgeVariant variant; };
        V variants[] = {
            {"Draft",   BadgeVariant::Neutral},
            {"New",     BadgeVariant::Primary},
            {"Active",  BadgeVariant::Successful},
            {"Pending", BadgeVariant::Warning},
            {"Error",   BadgeVariant::Danger},
            {"Beta",    BadgeVariant::Info},
        };
        int bx = 20;
        for (int i = 0; i < 6; ++i) {
            auto b = CreateBadge("BgPill" + std::to_string(i), bx, 118, variants[i].label, variants[i].variant);
            container->AddChild(b);
            bx += static_cast<int>(std::string(variants[i].label).size()) * 8 + 40;
        }

        // ===== 2. COUNT BADGES =====
        section("BgL2", "Count badges (danger, with 99+ cap):", 168);
        int cx = 20;
        int counts[] = {1, 8, 42, 150};
        for (int i = 0; i < 4; ++i) {
            auto b = CreateCountBadge("BgCount" + std::to_string(i), cx, 196, counts[i]);
            container->AddChild(b);
            cx += 54;
        }
        // A zero count with show-zero off is hidden; with show-zero on it shows "0".
        auto zeroHidden = CreateCountBadge("BgCountZero", cx, 196, 0);
        container->AddChild(zeroHidden);
        auto zeroShown = CreateCountBadge("BgCountZeroShown", cx + 54, 196, 0, BadgeVariant::Neutral);
        zeroShown->SetShowZero(true);
        container->AddChild(zeroShown);

        // ===== 3. STATUS DOTS =====
        section("BgL3", "Status dots:", 246);
        struct D { const char* label; BadgeVariant variant; };
        D dots[] = {
            {"Online",  BadgeVariant::Successful},
            {"Away",    BadgeVariant::Warning},
            {"Busy",    BadgeVariant::Danger},
            {"Offline", BadgeVariant::Neutral},
        };
        int dx = 20;
        for (int i = 0; i < 4; ++i) {
            auto dot = CreateDotBadge("BgDot" + std::to_string(i), dx, 282, dots[i].variant);
            container->AddChild(dot);
            auto lbl = CreateLabel("BgDotLbl" + std::to_string(i), dx + 16, 278, 90, 20);
            lbl->SetText(dots[i].label);
            lbl->SetFontSize(12);
            container->AddChild(lbl);
            dx += 130;
        }

        // ===== 4. OVERLAY BADGES (anchored to icon tiles) =====
        section("BgL4", "Overlay badges anchored to a corner:", 330);

        auto inbox = MakeTile("BgTileInbox", 20, 356, "In", Color(0, 120, 215));
        container->AddChild(inbox);
        auto inboxBadge = CreateCountBadge("BgInboxBadge", 0, 0, 12);
        inboxBadge->AnchorTo(inbox, BadgeCorner::TopRight, -4, 4);
        container->AddChild(inboxBadge);

        auto alerts = MakeTile("BgTileAlerts", 110, 356, "Al", Color(108, 117, 125));
        container->AddChild(alerts);
        auto alertsBadge = CreateCountBadge("BgAlertsBadge", 0, 0, 150);   // -> 99+
        alertsBadge->AnchorTo(alerts, BadgeCorner::TopRight, -4, 4);
        container->AddChild(alertsBadge);

        auto chat = MakeTile("BgTileChat", 200, 356, "Ch", Color(40, 167, 69));
        container->AddChild(chat);
        auto chatDot = CreateDotBadge("BgChatDot", 0, 0, BadgeVariant::Danger);   // unread indicator
        chatDot->AnchorTo(chat, BadgeCorner::TopRight, -4, 4);
        container->AddChild(chatDot);

        // ===== 5. INTERACTIVE COUNTER =====
        section("BgL5", "Interactive (Add / Clear the overlay count):", 440);
        auto bell = MakeTile("BgTileBell", 20, 466, "Bell", Color(103, 58, 183));
        container->AddChild(bell);
        auto bellBadge = CreateCountBadge("BgBellBadge", 0, 0, 3);
        bellBadge->AnchorTo(bell, BadgeCorner::TopRight, -4, 4);
        container->AddChild(bellBadge);

        auto counter = std::make_shared<int>(3);
        auto addBtn = std::make_shared<UltraCanvasButton>("BgAdd", 100, 476, 70.0f, 30.0f, "Add");
        addBtn->onClick = [bellBadge, counter]() { bellBadge->SetCount(++(*counter)); };
        container->AddChild(addBtn);

        auto clearBtn = std::make_shared<UltraCanvasButton>("BgClear", 178, 476, 70.0f, 30.0f, "Clear");
        clearBtn->onClick = [bellBadge, counter]() { *counter = 0; bellBadge->SetCount(0); };
        container->AddChild(clearBtn);

        auto hint = CreateLabel("BgHint", 258, 480, 400, 22);
        hint->SetText("Count 0 disappears; over 99 shows \"99+\".");
        hint->SetFontSize(11);
        container->AddChild(hint);

        // ===== INSTRUCTIONS =====
        auto instructions = CreateLabel("BadgeInstructions", 20, 528, 940, 60);
        instructions->SetText(
                "API:\n"
                "\xE2\x80\xA2 CreateBadge(text, variant) / CreateCountBadge(count) / CreateDotBadge(variant)\n"
                "\xE2\x80\xA2 SetCount / SetMaxCount / SetShowZero / SetVariant / SetColor; AnchorTo(element, corner, dx, dy)");
        instructions->SetFontSize(11);
        instructions->SetBackgroundColor(Color(255, 255, 240));
        instructions->SetPadding(6);
        container->AddChild(instructions);

        return container;
    }

} // namespace UltraCanvas
