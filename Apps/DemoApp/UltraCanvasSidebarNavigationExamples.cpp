// Apps/DemoApp/UltraCanvasSidebarNavigationExamples.cpp
// Demonstration of UltraCanvasSidebarNavigation: sections of items with
// icons and badges beside a page that follows the selection, the compact
// icons-only mode, and the keyboard.
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasSidebarNavigation.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateSidebarNavigationExamples() {
        auto page = DemoKit::CreatePage("SidebarNavigationExamples", "Sidebar Navigation Examples",
                "The navigation column of a window: sections of items with icon, label and badge, one selected; "
                "Up / Down / Home / End / Return; wheel scrolling; a compact icons-only mode.");

        // ===== 1. SECTIONS AND BADGES, BESIDE A PAGE =====
        auto storeCard = DemoKit::AddCard(page, "SnL1", "Sections and badges",
                                          "A store's column beside its page; the page follows the selection. "
                                          "Click an item, or focus the column and use the arrow keys.");
        auto frame = CreateContainer("SnFrame", 0, 0, 0, 300);
        frame->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        frame->SetBorders(1.0f, Color(224, 227, 235, 255), 6.0f);

        auto nav = CreateSidebarNavigation("SnNav");
        nav->AddItem("discover", "Discover", "media/icons/face-awesome.svg");
        nav->AddItem("categories", "Categories", "media/icons/chart.svg");
        const int library = nav->AddSection("Library");
        nav->AddItem(library, "installed", "Installed", "media/icons/clipboard-list.svg");
        nav->AddItem(library, "updates", "Updates", "media/icons/bell-icon.svg");
        nav->AddItem(library, "downloads", "Downloads", "media/icons/folder-download.svg");
        const int more = nav->AddSection("More");
        nav->AddItem(more, "mail", "Mail", "media/icons/envelope.svg");
        nav->AddItem(more, "settings", "Settings", "media/icons/copy.svg");
        nav->AddItem(more, "beta", "Beta programme", "media/icons/add-document.svg");
        nav->SetItemEnabled("beta", false);
        nav->SetBadge("updates", 3);
        nav->SetBadgeText("downloads", "2");
        nav->layoutItem.SetFlexShrink(0);
        frame->AddChild(nav);

        auto pageArea = CreateLabel("SnPage", 0, 0, 0, 0, "Discover");
        pageArea->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
        pageArea->SetFont("", 20.0f, FontWeight::Bold);
        pageArea->layoutItem.SetFlexGrow(1);
        pageArea->SetBackgroundColor(Color(250, 250, 252, 255));
        frame->AddChild(pageArea);
        nav->Select("discover", false);
        nav->onSelectionChanged = [pageArea, n = nav.get()](const std::string& id) {
            const SidebarItem* item = n->FindItem(id);
            pageArea->SetText(item ? item->label : std::string("(nothing selected)"));
        };
        auto activatedOut = DemoKit::CreateText("SnActivated", "Double-click or press Return on an item to activate it");
        activatedOut->SetBackgroundColor(Color(240, 240, 240));
        activatedOut->SetPadding(3);
        nav->onItemActivated = [activatedOut](const std::string& id) { activatedOut->SetText("Activated: " + id); };
        DemoKit::AddStretched(storeCard, frame);
        DemoKit::Add(storeCard, activatedOut);

        // ===== 2. COMPACT MODE =====
        auto compactCard = DemoKit::AddCard(page, "SnL2", "Compact mode",
                                            "Icons alone, the label as a tooltip, a badge as a dot; the column is compactWidth wide.");
        auto compactRow = DemoKit::AddRow(compactCard, "SnCompactRow", 12);
        auto toggle = CreateButton("SnToggle", 0, 0, 160, 28, "Toggle compact");
        toggle->onClick = [n = nav.get()]() { n->SetCompact(!n->IsCompact()); };
        DemoKit::AddFixed(compactRow, toggle);
        auto compactOut = DemoKit::CreateText("SnCompactOut", "Expanded (220 px)");
        compactOut->SetBackgroundColor(Color(240, 240, 240));
        compactOut->SetPadding(3);
        nav->onCompactChanged = [compactOut, n = nav.get()](bool compact) {
            compactOut->SetText(std::string(compact ? "Compact" : "Expanded") + " (" +
                                std::to_string(static_cast<int>(n->PreferredWidth())) + " px)");
        };
        DemoKit::Add(compactRow, compactOut);

        // ===== 3. KEYBOARD AND A LONG LIST =====
        auto longCard = DemoKit::AddCard(page, "SnL3", "Keyboard and scrolling",
                                         "More items than fit: the wheel scrolls, Up / Down keep the selection in view, Home / End jump.");
        auto longFrame = CreateContainer("SnLongFrame", 0, 0, 0, 220);
        longFrame->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        longFrame->SetBorders(1.0f, Color(224, 227, 235, 255), 6.0f);
        auto longNav = CreateSidebarNavigation("SnLongNav");
        for (int s = 0; s < 4; ++s) {
            const int section = longNav->AddSection("Section " + std::to_string(s + 1));
            for (int i = 0; i < 5; ++i) {
                const std::string id = "s" + std::to_string(s) + "i" + std::to_string(i);
                longNav->AddItem(section, id, "Item " + std::to_string(s * 5 + i + 1), "media/icons/copy.svg");
            }
        }
        longNav->Select("s0i0", false);
        longNav->layoutItem.SetFlexShrink(0);
        longFrame->AddChild(longNav);
        auto longOut = CreateLabel("SnLongOut", 0, 0, 0, 0, "Item 1");
        longOut->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
        longOut->layoutItem.SetFlexGrow(1);
        longOut->SetBackgroundColor(Color(250, 250, 252, 255));
        longFrame->AddChild(longOut);
        longNav->onSelectionChanged = [longOut, n = longNav.get()](const std::string& id) {
            const SidebarItem* item = n->FindItem(id);
            longOut->SetText(item ? item->label : std::string());
        };
        DemoKit::AddStretched(longCard, longFrame);

        DemoKit::AddNotes(page, "SnNotes", "Implementation notes", {
            "UltraCanvasSidebarNavigation is self-rendered: sections, items, badges and the selection bar are drawn by the element.",
            "Items added without a section go into an untitled first section; AddSection returns the index for the rest.",
            "SetBadge(id, count) shows a count pill (0 clears); in compact mode a badge is a dot at the icon's corner.",
            "onSelectionChanged fires once per change, for a click, the keyboard and Select; Select(id, false) is silent.",
            "SetCompact switches to icons with tooltips and compactWidth; onCompactChanged lets the window remember it.",
            "UltraCanvasToolbar (vertical) is a strip of actions; UltraCanvasTreeView is for places that nest.",
        });
        return page;
    }

} // namespace UltraCanvas
