// Apps/DemoApp/UltraCanvasDemoExamples.cpp
// Implementation of all component example creators
// Version: 1.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
//#include "UltraCanvasButton3Sections.h"
#include "UltraCanvasFormulaEditor.h"
#include "Plugins/Charts/UltraCanvasDivergingBarChart.h"
#include <sstream>
#include <random>
#include <map>

namespace UltraCanvas {
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTreeViewExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("TreeViewExamples", 600, 0, 0, 1000, 600);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("TreeViewTitle", 601, 10, 10, 300, 30);
        title->SetText("TreeView Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // File Explorer Style Tree
        auto fileTree = std::make_shared<UltraCanvasTreeView>("FileTree", 602, 20, 50, 300, 400);
        fileTree->SetRowHeight(22);
        fileTree->SetSelectionMode(TreeSelectionMode::Single);

        // Setup file tree structure
        TreeNodeData rootData("root", "My Computer");
        rootData.leftIcon = TreeNodeIcon("assets/icons/computer.png", 16, 16);
        TreeNode* root = fileTree->SetRootNode(rootData);

        TreeNodeData driveC("drive_c", "Local Disk (C:)");
        driveC.leftIcon = TreeNodeIcon("assets/icons/drive.png", 16, 16);
        fileTree->AddNode("root", driveC);

        TreeNodeData documents("documents", "Documents");
        documents.leftIcon = TreeNodeIcon("assets/icons/folder.png", 16, 16);
        fileTree->AddNode("drive_c", documents);

        TreeNodeData file1("file1", "Document.txt");
        file1.leftIcon = TreeNodeIcon("assets/icons/text.png", 16, 16);
        fileTree->AddNode("documents", file1);

        TreeNodeData pictures("pictures", "Pictures");
        pictures.leftIcon = TreeNodeIcon("assets/icons/folder.png", 16, 16);
        fileTree->AddNode("drive_c", pictures);

        fileTree->onNodeSelected = [](TreeNode* node) {
            std::cout << "Selected: " << node->data.text << std::endl;
        };

        root->Expand();
        container->AddChild(fileTree);

        // Multi-Selection Tree
        auto multiTree = std::make_shared<UltraCanvasTreeView>("MultiTree", 603, 350, 50, 300, 200);
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

        auto multiLabel = std::make_shared<UltraCanvasLabel>("MultiTreeLabel", 604, 350, 260, 300, 20);
        multiLabel->SetText("Multi-Selection TreeView (Ctrl+Click)");
        multiLabel->SetFontSize(12);
        container->AddChild(multiLabel);

        return container;
    }

} // namespace UltraCanvas