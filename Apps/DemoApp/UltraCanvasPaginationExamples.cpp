// Apps/DemoApp/UltraCanvasPaginationExamples.cpp
// Demonstration of UltraCanvasPagination: numbered strip with ellipsis,
// compact and simple modes, and item-count driven paging with a live readout.
// Version: 1.0.0
// Last Modified: 2026-07-07
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasPagination.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreatePaginationExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("PaginationExamples", 0, 0, 1000, 640);
        container->SetPadding(0, 0, 10, 0);

        auto title = CreateLabel("PaginationTitle", 20, 10, 0, 30);
        title->SetText("Pagination Examples");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto subtitle = CreateLabel("PaginationSubtitle", 20, 42, 940, 40);
        subtitle->SetText("Navigate pages with First/Prev, the numbered strip (with ellipsis for large "
                          "counts), Next/Last - or with the Left/Right/Home/End keys when focused.");
        subtitle->SetFontSize(12);
        container->AddChild(subtitle);

        int y = 100;
        auto sectionLabel = [&](const std::string& id, const std::string& text) {
            auto l = CreateLabel(id, 20, y, 400, 20);
            l->SetText(text);
            l->SetFontWeight(FontWeight::Bold);
            container->AddChild(l);
            y += 26;
        };

        // ===== 1. NUMBERED (large count -> ellipsis) =====
        sectionLabel("PgL1", "Numbered (20 pages, ellipsis windowing):");
        auto readout1 = CreateLabel("PgReadout1", 640, y + 6, 320, 22);
        readout1->SetText("On page 1 of 20");
        readout1->SetBackgroundColor(Color(240, 240, 240));
        readout1->SetPadding(3);
        container->AddChild(readout1);

        auto pg1 = CreatePagination("Pg1", 20, y, 600, 34, 20, 1);
        pg1->onPageChanged = [readout1](int page) {
            readout1->SetText("On page " + std::to_string(page) + " of 20");
        };
        container->AddChild(pg1);
        y += 60;

        // ===== 2. ITEM-COUNT DRIVEN + PAGE INFO =====
        sectionLabel("PgL2", "From item count (95 items, 10 per page, with info):");
        auto readout2 = CreateLabel("PgReadout2", 640, y + 6, 320, 22);
        readout2->SetText("Showing items 1-10");
        readout2->SetBackgroundColor(Color(240, 240, 240));
        readout2->SetPadding(3);
        container->AddChild(readout2);

        auto pg2 = CreatePaginationForItems("Pg2", 20, y, 640, 34, 95, 10, 1);
        // Round page entries in a light orange pastel palette.
        {
            PaginationStyle s = pg2->GetStyle();
            s.cornerRadius     = s.cellSize / 2.0f;          // circular cells
            s.cellColor        = Color(255, 236, 214, 255);  // light orange pastel
            s.cellHoverColor   = Color(255, 224, 178, 255);
            s.cellPressedColor = Color(255, 213, 153, 255);
            s.cellBorderColor  = Color(255, 193, 130, 255);
            s.cellTextColor    = Color(120, 70, 20, 255);    // warm brown for contrast
            s.currentColor     = Color(255, 167, 74, 255);   // stronger pastel orange
            s.currentTextColor = Colors::White;
            s.navGlyphColor    = Color(150, 90, 30, 255);
            pg2->SetStyle(s);
        }
        // Read the item window (start-end) straight from the control on change.
        auto pg2ptr = pg2.get();
        pg2->onPageChanged = [readout2, pg2ptr](int) {
            readout2->SetText("Showing items " + std::to_string(pg2ptr->GetPageStartItem()) +
                              "-" + std::to_string(pg2ptr->GetPageEndItem()));
        };
        container->AddChild(pg2);
        y += 60;

        // ===== 3. COMPACT MODE =====
        sectionLabel("PgL3", "Compact (prev / info / next):");
        auto pg3 = CreatePagination("Pg3", 20, y, 260, 34, 12, 3);
        pg3->SetMode(PaginationMode::Compact);
        container->AddChild(pg3);
        y += 60;

        // ===== 4. SIMPLE MODE =====
        sectionLabel("PgL4", "Simple (prev / next only, with info):");
        auto pg4 = CreatePagination("Pg4", 20, y, 320, 34, 8, 1);
        pg4->SetMode(PaginationMode::Simple);
        pg4->SetShowPageInfo(true);
        container->AddChild(pg4);
        y += 60;

        // ===== 5. WIDE WINDOW (siblingCount = 2, no first/last) =====
        sectionLabel("PgL5", "Numbered, siblingCount=2, no First/Last buttons:");
        auto pg5 = CreatePagination("Pg5", 20, y, 600, 34, 30, 15);
        pg5->SetSiblingCount(2);
        pg5->SetShowFirstLast(false);
        container->AddChild(pg5);
        y += 60;

        // ===== INSTRUCTIONS =====
        auto instructions = CreateLabel("PaginationInstructions", 20, y + 6, 940, 90);
        instructions->SetText(
                "API:\n"
                "\xE2\x80\xA2 CreatePagination(id, x, y, w, h, pageCount, currentPage)\n"
                "\xE2\x80\xA2 CreatePaginationForItems(id, x, y, w, h, totalItems, pageSize, currentPage)\n"
                "\xE2\x80\xA2 SetMode(Numbered | Compact | Simple); SetSiblingCount / SetBoundaryCount\n"
                "\xE2\x80\xA2 onPageChanged(page); GetPageStartItem() / GetPageEndItem() for the item window");
        instructions->SetFontSize(11);
        instructions->SetBackgroundColor(Color(255, 255, 240));
        instructions->SetPadding(6);
        container->AddChild(instructions);

        return container;
    }

} // namespace UltraCanvas
