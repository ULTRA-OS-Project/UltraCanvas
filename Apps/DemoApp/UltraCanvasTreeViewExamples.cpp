// Apps/DemoApp/UltraCanvasDemoExamples.cpp
// Implementation of all component example creators
// Version: 1.0.1
// Last Modified: 2026-05-01
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasCheckbox.h"
#include "Plugins/Charts/UltraCanvasDivergingBarChart.h"
#include <sstream>
#include <random>
#include <map>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTreeViewExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("TreeViewExamples", 0, 0, 1000, 600);
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
        documents.leftIcon = TreeNodeIcon(NormalizePath(GetResourcesDir() + "media/icons/folder.png"), 16, 16);
        fileTree->AddNode("drive_c", documents);

        TreeNodeData file1("file1", "Document.txt");
        file1.leftIcon = TreeNodeIcon(NormalizePath(GetResourcesDir() + "media/icons/text.png"), 16, 16);
        fileTree->AddNode("documents", file1);

        TreeNodeData pictures("pictures", "Pictures");
        pictures.leftIcon = TreeNodeIcon(NormalizePath(GetResourcesDir() + "media/icons/folder.png"), 16, 16);
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
        TreeNode* multiRootNode = multiTree->SetRootNode(multiRoot);

        TreeNodeData category1("cat1", "Category 1");
        multiTree->AddNode("multi_root", category1);
        multiTree->AddNode("cat1", TreeNodeData("item1", "Item 1"));
        multiTree->AddNode("cat1", TreeNodeData("item2", "Item 2"));

        TreeNodeData category2("cat2", "Category 2");
        multiTree->AddNode("multi_root", category2);
        multiTree->AddNode("cat2", TreeNodeData("item3", "Item 3"));

        multiRootNode->Expand();
        container->AddChild(multiTree);

        auto multiLabel = std::make_shared<UltraCanvasLabel>("MultiTreeLabel", 350, 260, 300, 20);
        multiLabel->SetText("Multi-Selection TreeView (Ctrl+Click)");
        multiLabel->SetFontSize(12);
        container->AddChild(multiLabel);

        // ----- Debugger "Variables" panel: Classic vs Modern columns -----
        // Demonstrates the columnar display mode (Name / Type / Value with an accent
        // Type column and section-header bars) an IDE debugger would use, plus the
        // Classic/Modern layout toggle and Alphabetic/Last-access sort options.
        auto varsTree = std::make_shared<UltraCanvasTreeView>("VarsTree", 680, 50, 300, 330);
        varsTree->SetRowHeight(26);
        varsTree->SetSelectionMode(TreeSelectionMode::Single);
        varsTree->SetShowExpandButtons(true);
        varsTree->SetDisplayMode(TreeDisplayMode::Modern);

        // Root renders as a "Variables" section-header bar; the sections hang beneath it.
        TreeNodeData varsRootData("vars_root", "Variables");
        varsRootData.isGroupHeader = true;
        TreeNode* varsRoot = varsTree->SetRootNode(varsRootData);
        varsRoot->Expand();

        // Helper: a variable row carrying Name / Type / Value + a last-access stamp.
        auto addVar = [&](const std::string& parent, const std::string& id,
                          const std::string& name, const std::string& type,
                          const std::string& value, uint64_t access) {
            TreeNodeData d(id, name);
            d.typeText = type;
            d.valueText = value;
            d.accessSequence = access;
            varsTree->AddNode(parent, d);
        };
        // Helper: a full-width section header (Line / Loop / function).
        auto addGroup = [&](const std::string& id, const std::string& title) {
            TreeNodeData g(id, title);
            g.isGroupHeader = true;
            TreeNode* n = varsTree->AddNode("vars_root", g);
            if (n) n->Expand();
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
                                         ? TreeDisplayMode::Modern
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

        return container;
    }

} // namespace UltraCanvas