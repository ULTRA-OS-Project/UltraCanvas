// Apps/DemoApp/UltraCanvasDemoExamples.cpp
// Implementation of all component example creators
// Version: 1.0.2
// Last Modified: 2026-07-26
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasColumnsTreeView.h"
#include "UltraCanvasSegmentedControl.h"
#include "Plugins/Charts/UltraCanvasDivergingBarChart.h"
#include <sstream>
#include <random>
#include <map>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTreeViewExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("TreeViewExamples", 0, 0, 1000, 800);
        container->SetPadding(0,0,10,0);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("TreeViewTitle", 10, 10, 300, 30);
        title->SetText("TreeView Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // File Explorer Style Tree
        auto fileTree = std::make_shared<UltraCanvasTreeView>("FileTree", 20, 50, 300, 400);
        fileTree->SetRowHeight(22);
        fileTree->SetSelectionMode(TreeSelectionMode::Single);

        // Setup file tree structure
        TreeNodeData rootData("root", "My Computer");
        rootData.leftIcon = TreeNodeIcon(NormalizePath(GetResourcesDir() + "media/icons/computer.png"), 16, 16);
        TreeNode* root = fileTree->SetRootNode(rootData);

        TreeNodeData driveC("drive_c", "Local Disk (C:)");
        driveC.leftIcon = TreeNodeIcon(NormalizePath(GetResourcesDir() + "media/icons/drive.png"), 16, 16);
        fileTree->AddNode("root", driveC);

        TreeNodeData documents("documents", "Documents");
        documents.leftIcon = TreeNodeIcon(NormalizePath(GetResourcesDir() + "media/icons/folder-brown.svg"), 16, 16);
        fileTree->AddNode("drive_c", documents);

        TreeNodeData file1("file1", "Document.txt");
        file1.leftIcon = TreeNodeIcon(NormalizePath(GetResourcesDir() + "media/icons/text.png"), 16, 16);
        fileTree->AddNode("documents", file1);

        TreeNodeData pictures("pictures", "Pictures");
        pictures.leftIcon = TreeNodeIcon(NormalizePath(GetResourcesDir() + "media/icons/folder-brown.svg"), 16, 16);
        fileTree->AddNode("drive_c", pictures);

        fileTree->onNodeSelected = [](TreeNode* node) {
            debugOutput << "Selected: " << node->data.text << std::endl;
        };

        root->Expand();
        container->AddChild(fileTree);

// File Explorer Label
        auto fileLabel = std::make_shared<UltraCanvasLabel>("FileTreeLabel", 20, 460, 300, 20);
        fileLabel->SetText("File Explorer Style TreeView");
        fileLabel->SetFontSize(12);
        container->AddChild(fileLabel);

        // Options checkboxes for File Explorer Tree
        auto autoExpandCheckbox = std::make_shared<UltraCanvasCheckbox>("AutoExpandCheckbox", 20, 490, 280, 24, "Auto expand selected node");
        autoExpandCheckbox->SetChecked(false);
        autoExpandCheckbox->onStateChanged = [fileTree](CheckedState oldState, CheckedState newState) {
        fileTree->SetAutoExpandSelectedNode(newState == CheckedState::Checked);
        };
        container->AddChild(autoExpandCheckbox);

        auto autoSelectFirstChildCheckbox = std::make_shared<UltraCanvasCheckbox>("AutoSelectFirstChildCheckbox", 20, 520, 280, 24, "Auto select first child of expanded node");
        autoSelectFirstChildCheckbox->SetChecked(false);
        autoSelectFirstChildCheckbox->onStateChanged = [fileTree](CheckedState oldState, CheckedState newState) {
        fileTree->SetShowFirstChildOnExpand(newState == CheckedState::Checked);
        };
        container->AddChild(autoSelectFirstChildCheckbox);

        // Multi-Selection Tree
        auto multiTree = std::make_shared<UltraCanvasTreeView>("MultiTree", 350, 50, 300, 200);
        multiTree->SetRowHeight(20);
        multiTree->SetSelectionMode(TreeSelectionMode::Multiple);

        TreeNodeData multiRoot("multi_root", "Categories");
        multiTree->SetRootNode(multiRoot);

        TreeNodeData category1("cat1", "Category 1");
        multiTree->AddNode("multi_root", category1);
        multiTree->AddNode("cat1", TreeNodeData("item1", "Item 1"));
        multiTree->AddNode("cat1", TreeNodeData("item2", "Item 2"));

        TreeNodeData category2("cat2", "Category 2");
        multiTree->AddNode("multi_root", category2);
        multiTree->AddNode("cat2", TreeNodeData("item3", "Item 3"));

        // Enough rows to overflow the 200px viewport, so the floating
        // "move to the top" button in the bottom-right corner shows up.
        for (int cat = 3; cat <= 8; ++cat) {
            std::string catId = "cat" + std::to_string(cat);
            multiTree->AddNode("multi_root", TreeNodeData(catId, "Category " + std::to_string(cat)));
            for (int item = 1; item <= 3; ++item) {
                multiTree->AddNode(catId, TreeNodeData(catId + "_item" + std::to_string(item),
                                                       "Item " + std::to_string(item)));
            }
        }

        multiTree->ExpandAll();
        container->AddChild(multiTree);

        auto multiLabel = std::make_shared<UltraCanvasLabel>("MultiTreeLabel", 350, 260, 300, 20);
        multiLabel->SetText("Multi-Selection TreeView (Ctrl+Click)");
        multiLabel->SetFontSize(12);
        container->AddChild(multiLabel);

        // Scroll-to-top button toggle (the feature is on by default).
        auto scrollTopCheckbox = std::make_shared<UltraCanvasCheckbox>(
            "ScrollToTopCheckbox", 350, 288, 300, 24, "Show \"move to the top\" button");
        scrollTopCheckbox->SetChecked(true);
        scrollTopCheckbox->onStateChanged = [multiTree](CheckedState, CheckedState newState) {
            multiTree->SetShowScrollToTopButton(newState == CheckedState::Checked);
        };
        container->AddChild(scrollTopCheckbox);

        // ----- Debugger "Variables" panel: Classic vs Modern columns -----
        // Demonstrates the columnar display mode (Name / Type / Value with an accent
        // Type column and section-header bars) an IDE debugger would use, plus the
        // Classic/Modern layout toggle and Alphabetic/Last-access sort options.
        auto varsTree = std::make_shared<UltraCanvasColumnsTreeView>("VarsTree", 680, 50, 300, 330);
        varsTree->SetRowHeight(26);
        varsTree->SetSelectionMode(TreeSelectionMode::Single);
        varsTree->SetShowExpandButtons(true);
        varsTree->SetDisplayMode(TreeDisplayMode::Columns);
        // Define the columns explicitly and show a header row. Name (the tree column)
        // and Value flex to fill; Type is fixed with the orange accent. Column
        // boundaries are draggable to resize.
        varsTree->SetColumns({
            { "name",  "Name",  0,   0,  2.0f, TextAlignment::Left, Color(40, 40, 40), Colors::Transparent, 0, /*isTreeColumn*/true  },
            { "type",  "Type",  128, 0,  1.0f, TextAlignment::Left, Color(40, 40, 40), Color(255, 190, 130), 4, false },
            { "value", "Value", 0,   48, 1.0f, TextAlignment::Left, Color(40, 40, 40), Colors::Transparent, 0, false },
        });
        varsTree->SetShowColumnHeader(true);

        // Root renders as a "Variables" section-header bar; the sections hang beneath it.
        TreeNodeData varsRootData("vars_root", "Variables");
        varsRootData.isGroupHeader = true;
        varsTree->SetRootNode(varsRootData);
        // NOTE: don't expand here — the root has no children yet, so it is still a
        // Leaf and Expand() is a no-op. The whole tree is expanded via ExpandAll()
        // once it is fully populated (see below).

        // Helper: a variable row carrying Name / Type / Value + a last-access stamp.
        auto addVar = [&](const std::string& parent, const std::string& id,
                          const std::string& name, const std::string& type,
                          const std::string& value, uint64_t access) {
            TreeNodeData d(id, name);
            d.SetCell("type", type);
            d.SetCell("value", value);
            d.accessSequence = access;
            varsTree->AddNode(parent, d);
        };
        // Helper: a full-width section header (Line / Loop / function).
        auto addGroup = [&](const std::string& id, const std::string& title) {
            TreeNodeData g(id, title);
            g.isGroupHeader = true;
            varsTree->AddNode("vars_root", g);
        };

        addGroup("grp_line", "Line");
        addVar("grp_line", "v_y",     "y",     "int",  "1",    30);
        addVar("grp_line", "v_tint",  "tint",  "*ptr", "2x67", 10);
        addVar("grp_line", "v_xval",  "x-val", "fp",   "2,45", 20);

        addGroup("grp_loop", "Loop");
        addVar("grp_loop", "v_x",      "x",      "int", "45",  50);
        addVar("grp_loop", "v_width",  "width",  "int", "257", 40);
        addVar("grp_loop", "v_height", "height", "int", "400", 60);

        addGroup("grp_fn", "function");
        addVar("grp_fn", "v_type",   "type",   "str", "\"up\"", 90);
        addVar("grp_fn", "v_offset", "offset", "int", "13",     70);
        addVar("grp_fn", "v_border", "border", "int", "8",      80);

        // Expand root + all group headers now that every child exists. Expanding
        // earlier is a no-op because a childless node is a Leaf, not Collapsed.
        varsTree->ExpandAll();

        container->AddChild(varsTree);

        auto varsLabel = std::make_shared<UltraCanvasLabel>("VarsTreeLabel", 680, 386, 300, 20);
        varsLabel->SetText("Debugger Variables (Modern columns)");
        varsLabel->SetFontSize(12);
        container->AddChild(varsLabel);

        // Layout toggle: Classic (single text) <-> Modern (columns)
        auto modernCheckbox = std::make_shared<UltraCanvasCheckbox>(
            "ModernLayoutCheckbox", 680, 414, 280, 24, "Modern layout (Name / Type / Value)");
        modernCheckbox->SetChecked(true);
        modernCheckbox->onStateChanged = [varsTree](CheckedState, CheckedState newState) {
            varsTree->SetDisplayMode(newState == CheckedState::Checked
                                         ? TreeDisplayMode::Columns
                                         : TreeDisplayMode::Classic);
        };
        container->AddChild(modernCheckbox);

        // Sort toggle: Alphabetic <-> Last access
        auto sortCheckbox = std::make_shared<UltraCanvasCheckbox>(
            "SortLastAccessCheckbox", 680, 444, 280, 24, "Sort by last access (else alphabetic)");
        sortCheckbox->SetChecked(false);
        sortCheckbox->onStateChanged = [varsTree](CheckedState, CheckedState newState) {
            if (newState == CheckedState::Checked) {
                varsTree->SetSortMode(TreeSortMode::LastAccess, /*ascending=*/false);
            } else {
                varsTree->SetSortMode(TreeSortMode::Alphabetic, /*ascending=*/true);
            }
        };
        container->AddChild(sortCheckbox);

        // ----- Connecting lines: None / Dotted / Solid, and the root-level trunk -----
        // A forest (hidden root, so the sections are the top level) is the case the
        // connectors were made for: without them the rows of three open sections are
        // just indentation. The segmented control switches TreeLineStyle live.
        auto linesTree = std::make_shared<UltraCanvasTreeView>("LinesTree", 350, 330, 300, 190);
        linesTree->SetRowHeight(22);
        linesTree->SetSelectionMode(TreeSelectionMode::Single);
        linesTree->SetRootVisible(false);
        linesTree->SetLineStyle(TreeLineStyle::Dotted);
        linesTree->SetRootNode(TreeNodeData("lines_root", ""));

        for (int section = 1; section <= 3; ++section) {
            const std::string sectionId = "sec" + std::to_string(section);
            linesTree->AddNode("lines_root",
                               TreeNodeData(sectionId, "Section " + std::to_string(section)));
            for (int para = 1; para <= 2; ++para) {
                const std::string paraId = sectionId + "_p" + std::to_string(para);
                linesTree->AddNode(sectionId,
                                   TreeNodeData(paraId, "Paragraph " + std::to_string(section) +
                                                                "." + std::to_string(para)));
            }
        }
        // One deeper branch, so the trunk of a section that still has rows below it
        // can be seen running past its nested children.
        linesTree->AddNode("sec1_p1", TreeNodeData("sec1_p1_a", "Figure 1.1.a"));
        linesTree->AddNode("sec1_p1", TreeNodeData("sec1_p1_b", "Figure 1.1.b"));
        linesTree->ExpandAll();
        container->AddChild(linesTree);

        auto linesLabel = std::make_shared<UltraCanvasLabel>("LinesTreeLabel", 350, 524, 300, 20);
        linesLabel->SetText("Connecting lines (SetLineStyle)");
        linesLabel->SetFontSize(12);
        container->AddChild(linesLabel);

        auto lineStyleControl = SegmentedControlBuilder("LineStyleSegments", 350, 548, 300, 28)
                .AddSegment("No lines")
                .AddSegment("Dotted")
                .AddSegment("Solid")
                .SetSelectedIndex(1)
                .OnSegmentSelected([linesTree](int index) {
                    switch (index) {
                        case 0: linesTree->SetLineStyle(TreeLineStyle::NoLine); break;
                        case 2: linesTree->SetLineStyle(TreeLineStyle::Solid); break;
                        default: linesTree->SetLineStyle(TreeLineStyle::Dotted); break;
                    }
                    linesTree->RequestRedraw();
                })
                .Build();
        container->AddChild(lineStyleControl);

        // Root lines: the trunk down the left margin that ties the three sections
        // together. It costs one indent of left margin, which is why it can be
        // switched off. (It only applies to a forest — a visible root row is
        // already the trunk everything hangs from.)
        auto rootLinesCheckbox = std::make_shared<UltraCanvasCheckbox>(
            "RootLinesCheckbox", 350, 582, 300, 24, "Connect the top-level rows too");
        rootLinesCheckbox->SetChecked(true);
        rootLinesCheckbox->onStateChanged = [linesTree](CheckedState, CheckedState newState) {
            linesTree->SetShowRootLines(newState == CheckedState::Checked);
        };
        container->AddChild(rootLinesCheckbox);

        // ----- Check flags: a tri-state selection independent of the row selection -----
        auto flagTree = std::make_shared<UltraCanvasTreeView>("FlagTree", 680, 490, 300, 190);
        flagTree->SetRowHeight(22);
        flagTree->SetSelectionMode(TreeSelectionMode::Single);
        flagTree->SetShowCheckboxes(true);
        flagTree->SetRootVisible(false);
        flagTree->SetRootNode(TreeNodeData("flag_root", ""));

        const char* flagFolders[] = {"Documents", "Pictures", "Music"};
        const char* flagFiles[][3] = {
            {"Invoice.odt", "Report.pdf", "Notes.txt"},
            {"Holiday.jpg", "Portrait.png", "Sketch.svg"},
            {"Intro.mp3", "Theme.flac", "Demo.wav"},
        };
        for (int folder = 0; folder < 3; ++folder) {
            const std::string folderId = "flag_dir" + std::to_string(folder);
            TreeNodeData folderData(folderId, flagFolders[folder]);
            folderData.leftIcon = TreeNodeIcon(
                NormalizePath(GetResourcesDir() + "media/icons/folder-brown.svg"), 16, 16);
            flagTree->AddNode("flag_root", folderData);
            for (int file = 0; file < 3; ++file) {
                TreeNodeData fileData(folderId + "_f" + std::to_string(file), flagFiles[folder][file]);
                fileData.leftIcon = TreeNodeIcon(
                    NormalizePath(GetResourcesDir() + "media/icons/text.png"), 16, 16);
                flagTree->AddNode(folderId, fileData);
            }
        }
        flagTree->ExpandAll();
        // Two files pre-flagged, so the tri-state parent (a filled square rather than
        // a tick) is visible without touching anything.
        flagTree->SetNodeChecked("flag_dir0_f0", true);
        flagTree->SetNodeChecked("flag_dir1_f2", true);
        container->AddChild(flagTree);

        auto flagLabel = std::make_shared<UltraCanvasLabel>("FlagTreeLabel", 680, 684, 300, 20);
        flagLabel->SetText("Check flags (SetShowCheckboxes)");
        flagLabel->SetFontSize(12);
        container->AddChild(flagLabel);

        auto flagStatus = std::make_shared<UltraCanvasLabel>("FlagStatusLabel", 680, 738, 300, 22);
        flagStatus->SetFontSize(12);
        // Weak capture would be cleaner, but the label outlives the tree here: both
        // belong to the same page container.
        auto updateFlagStatus = [flagTree, flagStatus]() {
            const size_t flagged = flagTree->GetCheckedNodes().size();
            flagStatus->SetText(std::to_string(flagged) + " of 12 rows flagged");
        };
        updateFlagStatus();
        flagTree->onNodeCheckChanged = [updateFlagStatus](TreeNode*, TreeCheckState) {
            updateFlagStatus();
        };
        container->AddChild(flagStatus);

        // Propagation: on, a folder's flag carries to its files and the folder shows
        // "some" as a filled square; off, every row carries its own flag.
        auto propagateCheckbox = std::make_shared<UltraCanvasCheckbox>(
            "FlagPropagateCheckbox", 680, 708, 300, 24, "Flag the whole subtree");
        propagateCheckbox->SetChecked(true);
        propagateCheckbox->onStateChanged = [flagTree](CheckedState, CheckedState newState) {
            flagTree->SetCheckPropagation(newState == CheckedState::Checked);
        };
        container->AddChild(propagateCheckbox);

        return container;
    }

} // namespace UltraCanvas