// Apps/DemoApp/UltraCanvasListViewExamples.cpp
// ListView component demonstration examples

#include "UltraCanvasDemo.h"
#include "UltraCanvasListView.h"

namespace UltraCanvas {

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateListViewExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("ListViewExamples", 1200, 0, 0, 1000, 640);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("ListViewTitle", 1201, 20, 10, 600, 35);
        title->SetText("ListView Component Showcase");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        title->SetTextColor(Color(50, 50, 150, 255));
        container->AddChild(title);

        // Subtitle
        auto subtitle = std::make_shared<UltraCanvasLabel>("ListViewSubtitle", 1202, 20, 45, 600, 25);
        subtitle->SetText("Simple lists, multi-column, styled, and icon views");
        subtitle->SetFontSize(12);
        subtitle->SetTextColor(Color(120, 120, 120, 255));
        container->AddChild(subtitle);

        // Status feedback label
        auto statusLabel = std::make_shared<UltraCanvasLabel>("LVStatusLabel", 1203, 600, 10, 380, 60);
        statusLabel->SetText("Click any list item to see feedback here");
        statusLabel->SetFontSize(11);
        statusLabel->SetBackgroundColor(Color(245, 245, 245, 255));
        statusLabel->SetBorders(1.0f);
        statusLabel->SetPadding(8.0f);
        container->AddChild(statusLabel);

        // ============================================================
        // Section 1: Simple List (Single Selection) — top-left
        // ============================================================
        auto section1 = std::make_shared<UltraCanvasLabel>("LVSection1", 1210, 20, 90, 460, 25);
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

        auto simpleList = std::make_shared<UltraCanvasListView>("SimpleListView", 1211, 20, 125, 460, 225);
        simpleList->SetModel(simpleModel.get());
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

        auto desc1 = std::make_shared<UltraCanvasLabel>("SimpleListDesc", 1212, 20, 355, 460, 20);
        desc1->SetText("Single-selection list with 10 fruit items");
        desc1->SetFontSize(10);
        desc1->SetTextColor(Color(140, 140, 140, 255));
        container->AddChild(desc1);

        // ============================================================
        // Section 2: Multi-Column List with Header — top-right
        // ============================================================
        auto section2 = std::make_shared<UltraCanvasLabel>("LVSection2", 1220, 500, 90, 480, 25);
        section2->SetText("2. Multi-Column List with Header");
        section2->SetFontWeight(FontWeight::Bold);
        section2->SetTextColor(Color(0, 100, 200, 255));
        container->AddChild(section2);

        auto multiModel = std::make_shared<UltraCanvasMultiColumnListModel>();
        multiModel->AddColumn(ListColumnDef("File Name", 170, TextAlignment::Left));
        multiModel->AddColumn(ListColumnDef("Type", 90, TextAlignment::Left));
        multiModel->AddColumn(ListColumnDef("Size", 70, TextAlignment::Right));
        multiModel->AddColumn(ListColumnDef("Modified", 110, TextAlignment::Left));

        multiModel->AddItem(MultiColumnListItem({"main.cpp", "C++ Source", "2.4 KB", "2025-03-15"}));
        multiModel->AddItem(MultiColumnListItem({"utils.h", "C++ Header", "1.1 KB", "2025-03-14"}));
        multiModel->AddItem(MultiColumnListItem({"README.md", "Markdown", "3.8 KB", "2025-03-10"}));
        multiModel->AddItem(MultiColumnListItem({"Makefile", "Build Script", "0.9 KB", "2025-02-28"}));
        multiModel->AddItem(MultiColumnListItem({"config.json", "JSON", "0.5 KB", "2025-03-01"}));
        multiModel->AddItem(MultiColumnListItem({"test_main.cpp", "C++ Source", "4.2 KB", "2025-03-12"}));
        multiModel->AddItem(MultiColumnListItem({"logo.png", "PNG Image", "45.6 KB", "2025-01-20"}));
        multiModel->AddItem(MultiColumnListItem({"CHANGELOG.md", "Markdown", "12.3 KB", "2025-03-15"}));

        auto multiList = std::make_shared<UltraCanvasListView>("MultiColumnListView", 1221, 500, 125, 480, 225);
        multiList->SetModel(multiModel.get());

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
        


        multiList->onItemClicked = [statusLabel, multiModel](int row) {
            const auto& item = multiModel->GetItem(row);
            statusLabel->SetText("Multi-Column: Clicked row " + std::to_string(row) +
                                 "\nFile: " + item.labels[0]);
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

        container->AddChild(multiList);

        auto desc2 = std::make_shared<UltraCanvasLabel>("MultiColDesc", 1222, 500, 355, 480, 20);
        desc2->SetText("Multi-column with header, grid lines, multi-select (Ctrl+Click)");
        desc2->SetFontSize(10);
        desc2->SetTextColor(Color(140, 140, 140, 255));
        container->AddChild(desc2);

        // ============================================================
        // Section 3: Styled List (Alternating Rows + Multi-Select) — bottom-left
        // ============================================================
        auto section3 = std::make_shared<UltraCanvasLabel>("LVSection3", 1230, 20, 385, 460, 25);
        section3->SetText("3. Styled List (Alternating Rows + Multi-Select)");
        section3->SetFontWeight(FontWeight::Bold);
        section3->SetTextColor(Color(0, 100, 200, 255));
        container->AddChild(section3);

        auto styledModel = std::make_shared<UltraCanvasSimpleListModel>();
        styledModel->AddItem("Crimson Red");
        styledModel->AddItem("Sunset Orange");
        styledModel->AddItem("Golden Yellow");
        styledModel->AddItem("Lime Green");
        styledModel->AddItem("Forest Green");
        styledModel->AddItem("Sky Blue");
        styledModel->AddItem("Royal Purple");
        styledModel->AddItem("Hot Pink");
        styledModel->AddItem("Chocolate Brown");
        styledModel->AddItem("Silver Gray");
        styledModel->AddItem("Midnight Black");
        styledModel->AddItem("Pearl White");

        auto styledList = std::make_shared<UltraCanvasListView>("StyledListView", 1231, 20, 420, 460, 160);
        styledList->SetModel(styledModel.get());

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

        auto desc3 = std::make_shared<UltraCanvasLabel>("StyledListDesc", 1232, 20, 585, 460, 20);
        desc3->SetText("Alternating row colors, purple theme, multi-select (Ctrl/Shift+Click)");
        desc3->SetFontSize(10);
        desc3->SetTextColor(Color(140, 140, 140, 255));
        container->AddChild(desc3);

        // ============================================================
        // Section 4: Custom Delegate with Icons — bottom-right
        // ============================================================
        auto section4 = std::make_shared<UltraCanvasLabel>("LVSection4", 1240, 500, 385, 480, 25);
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

        auto iconList = std::make_shared<UltraCanvasListView>("IconListView", 1241, 500, 420, 480, 160);
        iconList->SetModel(iconModel.get());
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

        auto desc4 = std::make_shared<UltraCanvasLabel>("IconListDesc", 1242, 500, 585, 480, 20);
        desc4->SetText("Single-column with 20px icons, custom row height (28px)");
        desc4->SetFontSize(10);
        desc4->SetTextColor(Color(140, 140, 140, 255));
        container->AddChild(desc4);

        return container;
    }

} // namespace UltraCanvas
