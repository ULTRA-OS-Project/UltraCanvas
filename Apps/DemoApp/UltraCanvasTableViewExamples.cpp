// Apps/DemoApp/UltraCanvasTableViewExamples.cpp
// TableView component demonstration examples: the pricing/feature comparison
// matrix design (the motivating screenshot), a banded sortable data grid,
// and a financial statement layout.

#include "UltraCanvasDemo.h"
#include "UltraCanvasTableView.h"

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTableViewExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("TableViewExamples", 0, 0, 1000, 660);

        auto title = std::make_shared<UltraCanvasLabel>("TVTitle", 20, 10, 600, 35);
        title->SetText("TableView Component Showcase");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        container->AddChild(title);

        auto subtitle = std::make_shared<UltraCanvasLabel>("TVSubtitle", 20, 45, 600, 25);
        subtitle->SetText("Predefined table designs: Comparison matrix, Professional grid, Financial statement");
        subtitle->SetFontSize(12);
        subtitle->SetTextColor(Color(120, 120, 120, 255));
        container->AddChild(subtitle);

        auto statusLabel = std::make_shared<UltraCanvasLabel>("TVStatusLabel", 620, 10, 360, 60);
        statusLabel->SetText("Click rows, buttons or sortable headers to see feedback here");
        statusLabel->SetFontSize(11);
        statusLabel->SetBackgroundColor(Color(245, 245, 245, 255));
        statusLabel->SetBorders(1.0f);
        statusLabel->SetPadding(8.0f);
        container->AddChild(statusLabel);

        // ============================================================
        // 1. Comparison / pricing matrix (TableDesign::Comparison) —
        //    the feature-matrix anatomy: section bands, check/cross
        //    glyphs, a highlighted plan column with a badge, CTA buttons.
        // ============================================================
        auto section1 = std::make_shared<UltraCanvasLabel>("TVSection1", 20, 80, 460, 22);
        section1->SetText("1. Comparison matrix (Comparison design, Warm palette)");
        section1->SetFontWeight(FontWeight::Bold);
        section1->SetTextColor(Color(0, 100, 200, 255));
        container->AddChild(section1);

        auto pricing = CreateTableView("PricingTable", 20, 106, 470, 532);
        pricing->SetDesign(TableDesign::Comparison);
        pricing->SetColumns({
            TableViewColumn("", 0.0f, 2.0f),      // Feature labels (stretch x2)
            TableViewColumn("S", 70.0f),
            TableViewColumn("M", 70.0f),
            TableViewColumn("L", 70.0f),
            TableViewColumn("XL", 70.0f),
        });
        pricing->SetHighlightedColumn(4, "Current");
        pricing->SetSelectionMode(TableSelectionMode::NoSelection);

        using C = TableViewCell;
        pricing->AddSectionRow("Orders & Accounting");
        pricing->AddRow({C("Long-term receipt archive"), C::MakeCheck(), C::MakeCheck(), C::MakeCheck(), C::MakeCheck()});
        pricing->AddRow({C("Mobile app & receipt scanner"), C::MakeCheck(), C::MakeCheck(), C::MakeCheck(), C::MakeCheck()});
        pricing->AddRow({C("Invoices & quotations"), C::MakeCross(), C::MakeCheck(), C::MakeCheck(), C::MakeCheck()});
        pricing->AddRow({C("Electronic cash book"), C::MakeCross(), C::MakeCross(), C::MakeCheck(), C::MakeCheck()});
        pricing->AddRow({C("Recurring invoices"), C::MakeCross(), C::MakeCross(), C::MakeCross(), C::MakeCheck()});
        pricing->AddRow({C("Public API"), C::MakeCross(), C::MakeCross(), C::MakeCross(), C::MakeCheck()});
        pricing->AddSectionRow("Customers & CRM");
        pricing->AddRow({C("Manage customers & suppliers"), C::MakeCheck(), C::MakeCheck(), C::MakeCheck(), C::MakeCheck()});
        pricing->AddRow({C("Customer history"), C::MakeCross(), C::MakeCheck(), C::MakeCheck(), C::MakeCheck()});
        pricing->AddRow({C("Online customer portal"), C::MakeCross(), C::MakeCheck(), C::MakeCheck(), C::MakeCheck()});
        pricing->AddTotalRow({C(""), C::MakeButton("SELECT"), C::MakeButton("SELECT"),
                              C::MakeButton("SELECT"), C::MakeButton("SELECT")});

        // Raw pointer capture: the element owning the callback must not keep
        // a shared_ptr to itself alive.
        auto* pricingPtr = pricing.get();
        pricing->onCellAction = [statusLabel, pricingPtr](int /*row*/, int col) {
            statusLabel->SetText("Plan chosen: " + pricingPtr->GetColumn(col).title +
                                 (col == pricingPtr->GetHighlightedColumn() ? " (current version)" : ""));
        };
        container->AddChild(pricing);

        // ============================================================
        // 2. Professional banded grid (sortable, numbers, badges)
        // ============================================================
        auto section2 = std::make_shared<UltraCanvasLabel>("TVSection2", 510, 80, 470, 22);
        section2->SetText("2. Data grid (Professional design) — click headers to sort");
        section2->SetFontWeight(FontWeight::Bold);
        section2->SetTextColor(Color(0, 100, 200, 255));
        container->AddChild(section2);

        auto grid = CreateTableView("ProjectGrid", 510, 106, 470, 250);
        grid->SetDesign(TableDesign::Professional);
        grid->SetColumns({
            TableViewColumn("Project", 0.0f, 2.0f),
            TableViewColumn("Owner", 0.0f, 1.5f),
            TableViewColumn("Budget", 90.0f),
            TableViewColumn("Status", 90.0f),
        });
        grid->EditColumn(0).sortable = true;
        grid->EditColumn(1).sortable = true;
        grid->EditColumn(2).sortable = true;
        grid->EditColumn(2).alignment = TextAlignment::Right;

        auto badge = [](const std::string& text, const Color& bg) {
            TableViewCell c = TableViewCell::MakeBadge(text);
            c.background = bg;
            return c;
        };
        const Color green(0, 150, 90), amber(230, 150, 0), red(210, 60, 60);
        grid->AddRow({C("Website relaunch"), C("Dana"), C::MakeNumber(48000, 0), badge("ACTIVE", green)});
        grid->AddRow({C("Mobile app"), C("Alex"), C::MakeNumber(125000, 0), badge("ACTIVE", green)});
        grid->AddRow({C("Data migration"), C("Kim"), C::MakeNumber(23500, 0), badge("PAUSED", amber)});
        grid->AddRow({C("Security audit"), C("Dana"), C::MakeNumber(18000, 0), badge("DONE", green)});
        grid->AddRow({C("Print catalogue"), C("Robin"), C::MakeNumber(9800, 0), badge("LATE", red)});
        grid->AddRow({C("CRM rollout"), C("Alex"), C::MakeNumber(67000, 0), badge("ACTIVE", green)});
        grid->AddRow({C("Support portal"), C("Kim"), C::MakeNumber(31000, 0), badge("PAUSED", amber)});

        auto* gridPtr = grid.get();
        grid->onRowClick = [statusLabel, gridPtr](int row) {
            statusLabel->SetText("Selected project: " + gridPtr->GetRow(row).cells[0].text);
        };
        grid->onColumnSort = [statusLabel, gridPtr](int col, bool ascending) {
            statusLabel->SetText("Sorted by " + gridPtr->GetColumn(col).title +
                                 (ascending ? " (ascending)" : " (descending)"));
        };
        container->AddChild(grid);

        // ============================================================
        // 3. Financial statement (sections, negatives red, double rule)
        // ============================================================
        auto section3 = std::make_shared<UltraCanvasLabel>("TVSection3", 510, 360, 470, 22);
        section3->SetText("3. Income statement (Financial design)");
        section3->SetFontWeight(FontWeight::Bold);
        section3->SetTextColor(Color(0, 100, 200, 255));
        container->AddChild(section3);

        auto statement = CreateTableView("IncomeStatement", 510, 384, 470, 272);
        statement->SetDesign(TableDesign::Financial);
        statement->SetColumns({
            TableViewColumn("", 0.0f, 2.0f),
            TableViewColumn("2025", 125.0f),
            TableViewColumn("2024", 125.0f),
        });
        statement->SetSelectionMode(TableSelectionMode::NoSelection);

        statement->AddSectionRow("Revenue");
        statement->AddRow({C("Product sales"), C::MakeNumber(1250400.50), C::MakeNumber(1104300.25)});
        statement->AddRow({C("Services"), C::MakeNumber(310200.00), C::MakeNumber(287500.75)});
        statement->AddSectionRow("Expenses");
        statement->AddRow({C("Cost of goods sold"), C::MakeNumber(-540300.10), C::MakeNumber(-498200.00)});
        statement->AddRow({C("Personnel"), C::MakeNumber(-410000.00), C::MakeNumber(-385000.00)});
        statement->AddRow({C("Marketing"), C::MakeNumber(-96500.40), C::MakeNumber(-120800.60)});
        statement->AddTotalRow({C("Net income"), C::MakeNumber(513800.00), C::MakeNumber(387800.40)});
        container->AddChild(statement);

        return container;
    }

} // namespace UltraCanvas
