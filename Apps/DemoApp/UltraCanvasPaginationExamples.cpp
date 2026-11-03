// Apps/DemoApp/UltraCanvasPaginationExamples.cpp
// Demonstration of UltraCanvasPagination: numbered strip with ellipsis,
// compact and simple modes, and item-count driven paging with a live readout.
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): one card per
//                  example, readouts and section titles sized to their text
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasPagination.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreatePaginationExamples() {
        auto page = DemoKit::CreatePage("PaginationExamples", "Pagination Examples",
                "Navigate pages with First/Prev, the numbered strip (with ellipsis for large "
                "counts), Next/Last - or with the Left/Right/Home/End keys when focused.");

        // A live readout beside a pagination strip: as wide as its text, the
        // grey box keeps it apart from the strip.
        auto makeReadout = [](const std::string& id, const std::string& text) {
            auto readout = DemoKit::CreateText(id, text);
            readout->SetBackgroundColor(Color(240, 240, 240));
            readout->SetPadding(3);
            return readout;
        };

        // ===== 1. NUMBERED (large count -> ellipsis) =====
        auto card1 = DemoKit::AddCard(page, "PgL1", "Numbered (20 pages, ellipsis windowing):");
        auto row1 = DemoKit::AddRow(card1, "PgRow1");
        row1->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
        auto readout1 = makeReadout("PgReadout1", "On page 1 of 20");

        auto pg1 = CreatePagination("Pg1", 0, 0, 600, 34, 20, 1);
        pg1->onPageChanged = [readout1](int page) {
            readout1->SetText("On page " + std::to_string(page) + " of 20");
        };
        DemoKit::AddFixed(row1, pg1);
        DemoKit::Add(row1, readout1);

        // ===== 2. ITEM-COUNT DRIVEN + PAGE INFO =====
        auto card2 = DemoKit::AddCard(page, "PgL2", "From item count (95 items, 10 per page, with info):");
        auto row2 = DemoKit::AddRow(card2, "PgRow2");
        row2->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
        auto readout2 = makeReadout("PgReadout2", "Showing items 1-10");

        auto pg2 = CreatePaginationForItems("Pg2", 0, 0, 640, 34, 95, 10, 1);
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
        DemoKit::AddFixed(row2, pg2);
        DemoKit::Add(row2, readout2);

        // ===== 3. COMPACT MODE / 4. SIMPLE MODE =====
        auto modesRow = DemoKit::AddRow(page, "PgModesRow");
        auto card3 = DemoKit::AddCard(modesRow, "PgL3", "Compact (prev / info / next):");
        auto pg3 = CreatePagination("Pg3", 0, 0, 260, 34, 12, 3);
        pg3->SetMode(PaginationMode::Compact);
        DemoKit::AddFixed(card3, pg3);

        auto card4 = DemoKit::AddCard(modesRow, "PgL4", "Simple (prev / next only, with info):");
        auto pg4 = CreatePagination("Pg4", 0, 0, 320, 34, 8, 1);
        pg4->SetMode(PaginationMode::Simple);
        pg4->SetShowPageInfo(true);
        DemoKit::AddFixed(card4, pg4);

        // ===== 5. WIDE WINDOW (siblingCount = 2, no first/last) =====
        auto card5 = DemoKit::AddCard(page, "PgL5", "Numbered, siblingCount=2, no First/Last buttons:");
        auto pg5 = CreatePagination("Pg5", 0, 0, 600, 34, 30, 15);
        pg5->SetSiblingCount(2);
        pg5->SetShowFirstLast(false);
        DemoKit::AddFixed(card5, pg5);

        // ===== INSTRUCTIONS =====
        DemoKit::AddNotes(page, "PaginationInstructions", "API", {
                "CreatePagination(id, x, y, w, h, pageCount, currentPage)",
                "CreatePaginationForItems(id, x, y, w, h, totalItems, pageSize, currentPage)",
                "SetMode(Numbered | Compact | Simple); SetSiblingCount / SetBoundaryCount",
                "onPageChanged(page); GetPageStartItem() / GetPageEndItem() for the item window"});

        return page;
    }

} // namespace UltraCanvas
