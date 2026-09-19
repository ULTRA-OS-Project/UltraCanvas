// Apps/DemoApp/UltraCanvasListViewExamples.cpp
// ListView component demonstration examples

#include "UltraCanvasDemo.h"
#include "UltraCanvasListView.h"
#include "UltraCanvasTextUtils.h"
#include <algorithm>

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateListViewExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("ListViewExamples", 0, 0, 1000, 640);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("ListViewTitle", 20, 10, 600, 35);
        title->SetText("ListView Component Showcase");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        container->AddChild(title);

        // Subtitle
        auto subtitle = std::make_shared<UltraCanvasLabel>("ListViewSubtitle", 20, 45, 600, 25);
        subtitle->SetText("Simple lists, multi-column, styled, and icon views — "
                          "hover a row or a column header for its tooltip");
        subtitle->SetFontSize(12);
        subtitle->SetTextColor(Color(120, 120, 120, 255));
        container->AddChild(subtitle);

        // Status feedback label
        // Three lines of 11px text plus padding: the cell-click feedback below
        // is the tallest thing this label has to show.
        auto statusLabel = std::make_shared<UltraCanvasLabel>("LVStatusLabel", 600, 10, 380, 74);
        statusLabel->SetText("Click any list item to see feedback here.\n"
                             "Hover a row (or a column header) to see its tooltip.");
        statusLabel->SetFontSize(11);
        statusLabel->SetBackgroundColor(Color(245, 245, 245, 255));
        statusLabel->SetBorders(1.0f);
        statusLabel->SetPadding(8.0f);
        container->AddChild(statusLabel);

        // ============================================================
        // Section 1: Simple List (Single Selection) — top-left
        // ============================================================
        auto section1 = std::make_shared<UltraCanvasLabel>("LVSection1", 20, 90, 460, 25);
        section1->SetText("1. Simple List (Single Selection)");
        section1->SetFontWeight(FontWeight::Bold);
        section1->SetTextColor(Color(0, 100, 200, 255));
        container->AddChild(section1);

        auto simpleModel = std::make_shared<UltraCanvasSimpleListModel>();
        simpleModel->AddItem(ListItem("Apple", "", "A common red fruit"));
        simpleModel->AddItem(ListItem("Banana", "", "A yellow tropical fruit"));
        simpleModel->AddItem(ListItem("Cherry", "", "Small red stone fruit"));
        simpleModel->AddItem(ListItem("Date", "", "Sweet desert fruit"));
        simpleModel->AddItem(ListItem("Elderberry", "", "Small dark purple berry"));
        simpleModel->AddItem(ListItem("Fig", "", "Soft pear-shaped fruit"));
        simpleModel->AddItem(ListItem("Grape", "", "Small round berry"));
        simpleModel->AddItem(ListItem("Honeydew", "", "Green melon variety"));
        simpleModel->AddItem(ListItem("Kiwi", "", "Brown fuzzy fruit"));
        simpleModel->AddItem(ListItem("Lemon", "", "Yellow citrus fruit"));

        auto simpleList = std::make_shared<UltraCanvasListView>("SimpleListView", 20, 125, 460, 225);
        simpleList->SetModel(simpleModel);
        simpleList->SetRowHeight(22);

        simpleList->onItemClicked = [statusLabel, simpleModel](int row) {
            const auto& item = simpleModel->GetItem(row);
            statusLabel->SetText("Simple List: Clicked row " + std::to_string(row) +
                                 "\nItem: " + item.label +
                                 "\nTooltip: " + item.tooltip);
        };
        simpleList->onItemDoubleClicked = [statusLabel, simpleModel](int row) {
            statusLabel->SetText("Simple List: Double-clicked '" + simpleModel->GetItem(row).label + "'");
        };

        container->AddChild(simpleList);

        auto desc1 = std::make_shared<UltraCanvasLabel>("SimpleListDesc", 20, 355, 460, 20);
        desc1->SetText("Single-selection list with 10 fruit items — hover a row for its description");
        desc1->SetFontSize(10);
        desc1->SetTextColor(Color(140, 140, 140, 255));
        container->AddChild(desc1);

        // ============================================================
        // Section 2: Multi-Column List with Header — top-right
        // ============================================================
        auto section2 = std::make_shared<UltraCanvasLabel>("LVSection2", 500, 90, 480, 25);
        section2->SetText("2. Multi-Column List with Header");
        section2->SetFontWeight(FontWeight::Bold);
        section2->SetTextColor(Color(0, 100, 200, 255));
        container->AddChild(section2);

        auto multiModel = std::make_shared<UltraCanvasMultiColumnListModel>();
        // The 4th ListColumnDef argument is the header tooltip, shown when the
        // pointer rests on that column's header cell.
        multiModel->AddColumn(ListColumnDef("File Name", 170, TextAlignment::Left,
                                            "Name of the file on disk"));
        multiModel->AddColumn(ListColumnDef("Type", 90, TextAlignment::Left,
                                            "File type, derived from the extension"));
        multiModel->AddColumn(ListColumnDef("Size", 70, TextAlignment::Right,
                                            "Size on disk, rounded to one decimal"));
        multiModel->AddColumn(ListColumnDef("Modified", 110, TextAlignment::Left,
                                            "Date of the last write, YYYY-MM-DD"));

        // Rows carry both a row-wide tooltip and per-column tooltips: hovering
        // a cell shows that column's text, and any column left without one
        // falls back to the row tooltip.
        struct FileRow {
            std::vector<std::string> cells;
            std::string path;
            std::string typeTip;
            std::string bytes;
            std::string modifiedTip;
        };
        const std::vector<FileRow> fileRows = {
            {{"main.cpp", "C++ Source", "2.4 KB", "2025-03-15"}, "src/main.cpp",
             "C++ translation unit, compiled by the build", "2,458 bytes", "15 Mar 2025, 09:14"},
            {{"utils.h", "C++ Header", "1.1 KB", "2025-03-14"}, "src/utils.h",
             "C++ header, included by other units", "1,132 bytes", "14 Mar 2025, 17:02"},
            {{"README.md", "Markdown", "3.8 KB", "2025-03-10"}, "README.md",
             "Markdown document", "3,890 bytes", "10 Mar 2025, 11:47"},
            {{"Makefile", "Build Script", "0.9 KB", "2025-02-28"}, "Makefile",
             "Build script, run by make", "921 bytes", "28 Feb 2025, 08:30"},
            {{"config.json", "JSON", "0.5 KB", "2025-03-01"}, "etc/config.json",
             "JSON configuration data", "512 bytes", "1 Mar 2025, 22:05"},
            {{"test_main.cpp", "C++ Source", "4.2 KB", "2025-03-12"}, "tests/test_main.cpp",
             "C++ translation unit, compiled by the build", "4,301 bytes", "12 Mar 2025, 13:20"},
            {{"logo.png", "PNG Image", "45.6 KB", "2025-01-20"}, "media/logo.png",
             "PNG bitmap image", "46,694 bytes", "20 Jan 2025, 16:58"},
            {{"CHANGELOG.md", "Markdown", "12.3 KB", "2025-03-15"}, "Docs/CHANGELOG.md",
             "Markdown document", "12,595 bytes", "15 Mar 2025, 09:41"},
        };
        // Rows are (re)filled from `fileRows` in the current sort order: the
        // view shows which column is sorted (SetSortIndicator); ordering the
        // rows stays with the model's owner.
        auto fillRows = [multiModel, fileRows](int sortColumn, bool ascending) {
            std::vector<FileRow> rows = fileRows;
            if (sortColumn >= 0) {
                auto key = [sortColumn](const FileRow& r) -> double {
                    float kb = 0;   // "2.4 KB" sorts by its number, not its text
                    return TryParseFloat(r.cells[sortColumn], kb) ? kb : 0;
                };
                std::stable_sort(rows.begin(), rows.end(),
                    [&](const FileRow& a, const FileRow& b) {
                        bool less = (sortColumn == 2) ? key(a) < key(b)
                                                      : a.cells[sortColumn] < b.cells[sortColumn];
                        bool greater = (sortColumn == 2) ? key(b) < key(a)
                                                         : b.cells[sortColumn] < a.cells[sortColumn];
                        return ascending ? less : greater;
                    });
            }
            multiModel->Clear();
            for (const auto& row : rows) {
                MultiColumnListItem item(row.cells);
                item.tooltip = row.path;                    // row-wide fallback
                item.SetCellTooltip(1, row.typeTip);
                item.SetCellTooltip(2, row.bytes);
                item.SetCellTooltip(3, row.modifiedTip);    // column 0 uses the path
                multiModel->AddItem(item);
            }
        };
        fillRows(-1, true);

        auto multiList = std::make_shared<UltraCanvasListView>("MultiColumnListView", 500, 125, 480, 225);
        multiList->SetModel(multiModel);

        ListViewStyle multiStyle;
        multiStyle.headerFontSize = 10;
        multiStyle.showHeader = true;
        multiStyle.showGridLines = true;
        multiStyle.headerHeight = 26;
        multiStyle.rowHeight = 22;
        multiList->SetStyle(multiStyle);

        auto multiListDelegate = std::make_shared<UltraCanvasDefaultListDelegate>();
        multiListDelegate->SetFontSize(10);
        multiList->SetDelegate(multiListDelegate);

        auto multiSelection = std::make_shared<UltraCanvasMultiSelection>();
        multiList->SetSelection(multiSelection);
        


        multiList->onCellClicked = [statusLabel, multiModel](int row, int column, const Point2Di&) {
            const auto& item = multiModel->GetItem(row);
            statusLabel->SetText("Multi-Column: Clicked row " + std::to_string(row) +
                                 ", column " + std::to_string(column) +
                                 " (" + multiModel->GetColumnDef(column).title + ")" +
                                 "\nFile: " + item.labels[0] +
                                 "\nCell tooltip: " + item.GetCellTooltip(column));
        };
        multiList->onSelectionChanged = [statusLabel](const std::vector<int>& rows) {
            std::string rowList;
            for (size_t i = 0; i < rows.size(); i++) {
                if (i > 0) rowList += ", ";
                rowList += std::to_string(rows[i]);
            }
            statusLabel->SetText("Multi-Column: " + std::to_string(rows.size()) +
                                 " items selected\nRows: [" + rowList + "]");
        };

        // Clicking a header sorts by that column; clicking it again flips the
        // direction. The triangle in the header cell follows SetSortIndicator.
        std::weak_ptr<UltraCanvasListView> multiListWeak = multiList;
        multiList->onHeaderClicked = [multiListWeak, multiModel, fillRows, statusLabel](int column) {
            auto view = multiListWeak.lock();
            if (!view) return;
            bool ascending = (view->GetSortColumn() == column) ? !view->GetSortAscending() : true;
            view->ResetSelection();
            fillRows(column, ascending);
            view->SetSortIndicator(column, ascending);
            statusLabel->SetText("Multi-Column: sorted by " + multiModel->GetColumnDef(column).title +
                                 (ascending ? " (ascending)" : " (descending)"));
        };

        container->AddChild(multiList);

        auto desc2 = std::make_shared<UltraCanvasLabel>("MultiColDesc", 500, 355, 480, 20);
        desc2->SetText("Header + grid lines, multi-select (Ctrl+Click), per-column and per-cell tooltips");
        desc2->SetFontSize(10);
        desc2->SetTextColor(Color(140, 140, 140, 255));
        container->AddChild(desc2);

        // ============================================================
        // Section 3: Styled List (Alternating Rows + Multi-Select) — bottom-left
        // ============================================================
        auto section3 = std::make_shared<UltraCanvasLabel>("LVSection3", 20, 385, 460, 25);
        section3->SetText("3. Styled List (Alternating Rows + Multi-Select)");
        section3->SetFontWeight(FontWeight::Bold);
        section3->SetTextColor(Color(0, 100, 200, 255));
        container->AddChild(section3);

        // ListItem's third argument is the tooltip (ToolTipRole), shown on hover
        auto styledModel = std::make_shared<UltraCanvasSimpleListModel>();
        styledModel->AddItem(ListItem("Crimson Red", "", "#DC143C — rgb(220, 20, 60)"));
        styledModel->AddItem(ListItem("Sunset Orange", "", "#FD5E53 — rgb(253, 94, 83)"));
        styledModel->AddItem(ListItem("Golden Yellow", "", "#FFDF00 — rgb(255, 223, 0)"));
        styledModel->AddItem(ListItem("Lime Green", "", "#32CD32 — rgb(50, 205, 50)"));
        styledModel->AddItem(ListItem("Forest Green", "", "#228B22 — rgb(34, 139, 34)"));
        styledModel->AddItem(ListItem("Sky Blue", "", "#87CEEB — rgb(135, 206, 235)"));
        styledModel->AddItem(ListItem("Royal Purple", "", "#7851A9 — rgb(120, 81, 169)"));
        styledModel->AddItem(ListItem("Hot Pink", "", "#FF69B4 — rgb(255, 105, 180)"));
        styledModel->AddItem(ListItem("Chocolate Brown", "", "#7B3F00 — rgb(123, 63, 0)"));
        styledModel->AddItem(ListItem("Silver Gray", "", "#C0C0C0 — rgb(192, 192, 192)"));
        styledModel->AddItem(ListItem("Midnight Black", "", "#000000 — rgb(0, 0, 0)"));
        styledModel->AddItem(ListItem("Pearl White", "", "#EAE0C8 — rgb(234, 224, 200)"));

        auto styledList = std::make_shared<UltraCanvasListView>("StyledListView", 20, 420, 460, 160);
        styledList->SetModel(styledModel);

        ListViewStyle styledStyle;
        styledStyle.backgroundColor = Color(252, 252, 255);
        styledStyle.alternateRowColors = true;
        styledStyle.alternateRowColor = Color(240, 240, 248);
        styledStyle.rowHeight = 24;
        styledStyle.selectionBackgroundColor = Color(100, 60, 180);
        styledStyle.hoverBackgroundColor = Color(220, 210, 240);
        styledList->SetStyle(styledStyle);

        auto styledDelegate = std::make_shared<UltraCanvasDefaultListDelegate>();
        styledDelegate->SetFontSize(13.0f);
        styledDelegate->SetTextPadding(10);
        styledDelegate->SetSelectedTextColor(Colors::White);
        styledList->SetDelegate(styledDelegate);

        auto styledSelection = std::make_shared<UltraCanvasMultiSelection>();
        styledList->SetSelection(styledSelection);

        styledList->onSelectionChanged = [statusLabel, styledModel](const std::vector<int>& rows) {
            std::string names;
            for (size_t i = 0; i < rows.size(); i++) {
                if (i > 0) names += ", ";
                names += styledModel->GetItem(rows[i]).label;
            }
            statusLabel->SetText("Styled List: " + std::to_string(rows.size()) +
                                 " items selected\nSelected: " + names);
        };

        container->AddChild(styledList);

        auto desc3 = std::make_shared<UltraCanvasLabel>("StyledListDesc", 20, 585, 460, 20);
        desc3->SetText("Alternating rows, purple theme, multi-select (Ctrl/Shift+Click), hover for hex value");
        desc3->SetFontSize(10);
        desc3->SetTextColor(Color(140, 140, 140, 255));
        container->AddChild(desc3);

        // ============================================================
        // Section 4: Custom Delegate with Icons — bottom-right
        // ============================================================
        auto section4 = std::make_shared<UltraCanvasLabel>("LVSection4", 500, 385, 480, 25);
        section4->SetText("4. Custom Delegate with Icons");
        section4->SetFontWeight(FontWeight::Bold);
        section4->SetTextColor(Color(0, 100, 200, 255));
        container->AddChild(section4);

        std::string iconsDir = NormalizePath(GetResourcesDir() + "media/icons/");

        auto iconModel = std::make_shared<UltraCanvasSimpleListModel>();
        iconModel->AddItem(ListItem("C++", iconsDir + "cpp.png", "Systems programming language"));
        iconModel->AddItem(ListItem("Python", iconsDir + "python.png", "General-purpose scripting language"));
        iconModel->AddItem(ListItem("Java", iconsDir + "java.png", "Enterprise application language"));
        iconModel->AddItem(ListItem("JavaScript", iconsDir + "javascript.png", "Web scripting language"));
        iconModel->AddItem(ListItem("TypeScript", iconsDir + "typescript.png", "Typed JavaScript superset"));
        iconModel->AddItem(ListItem("Rust", iconsDir + "rust.png", "Memory-safe systems language"));
        iconModel->AddItem(ListItem("C#", iconsDir + "csharp.png", "Microsoft .NET language"));
        iconModel->AddItem(ListItem("Go", iconsDir + "go.png", "Google systems language"));

        auto iconList = std::make_shared<UltraCanvasListView>("IconListView", 500, 420, 480, 160);
        iconList->SetModel(iconModel);
        iconList->SetRowHeight(28);

        auto iconDelegate = std::make_shared<UltraCanvasDefaultListDelegate>();
        iconDelegate->SetFontSize(13.0f);
        iconDelegate->SetIconSize(20);
        iconDelegate->SetIconSpacing(8);
        iconDelegate->SetTextPadding(8);
        iconDelegate->SetRowHeight(28);
        iconList->SetDelegate(iconDelegate);

        iconList->onItemClicked = [statusLabel, iconModel](int row) {
            const auto& item = iconModel->GetItem(row);
            statusLabel->SetText("Icon List: Selected '" + item.label + "'" +
                                 "\nTooltip: " + item.tooltip);
        };
        iconList->onItemDoubleClicked = [statusLabel, iconModel](int row) {
            statusLabel->SetText("Icon List: Activated '" + iconModel->GetItem(row).label + "'" +
                                 "\n(Double-click action would open details)");
        };

        container->AddChild(iconList);

        auto desc4 = std::make_shared<UltraCanvasLabel>("IconListDesc", 500, 585, 480, 20);
        desc4->SetText("Single-column with 20px icons, 28px rows — hover a row for its description");
        desc4->SetFontSize(10);
        desc4->SetTextColor(Color(140, 140, 140, 255));
        container->AddChild(desc4);

        return container;
    }

} // namespace UltraCanvas
