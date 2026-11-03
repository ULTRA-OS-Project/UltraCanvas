// Apps/DemoApp/UltraCanvasGourceTreeExamples.cpp
// Interactive Gource Tree diagram demonstration page
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): the tabs in a card
//                  that fills the area, each tree fills its tab, the toolbars wrap
//                  and every label and button is as wide as its text
// Version: 1.0.2
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework
//
// Changelog:
//   v1.1.0 (2026-10-23):
//     - The page is a DemoKit page whose flex layout follows the window, so
//       the ResponsiveGourceDemoContainer and its relayout lambda are gone.
//   v1.0.2 (2026-06-05):
//     - Fixed collapsed Project/Storage toolbar rows. Converted projectControls
//       and depthControls to flex-row layout (SetFlexRow + Center align-items)
//       and zeroed each toolbar child's constructor Y so they stay in-flow flex
//       items — a non-zero X/Y promotes an element to AbsoluteUI, which the flex
//       algorithm skips, leaving every control pinned at x=0 (the collapse).
//     - Replaced the manual LayoutToolbarRow() positioning with AddStretchSpacer
//       to push the right-hand button group flush right. Removed the now-unused
//       LayoutToolbarRow()/ToolbarItem helper.
//   v1.0.1 (2026-05-02):
//     - Made the demo layout fully responsive to parent window resize.
//       Introduced a private ResponsiveGourceDemoContainer subclass that
//       overrides OnEvent() to catch UCEventType::WindowResize and fires an
//       onResize callback. The construction routine captures all children in
//       a relayout lambda that runs once at startup and again on every resize.
//     - Replaced hardcoded toolbar coordinates with a LayoutToolbarRow()
//       helper that distributes left- and right-aligned control groups with a
//       flexible spacer in the middle. Eliminates the horizontal scrollbar
//       caused by the Collapse button overflowing its 980px container.
//     - Storage tab and Custom tab toolbars use the same helper, so all three
//       tabs adapt uniformly. "Size (KB):" and similar labels now have enough
//       width to render fully.
//     - mainContainer no longer has a fixed 1030px width; it adopts whatever
//       width its parent provides at construction time and on every resize,
//       down to a sensible minimum of kMinWidth = 940.

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Diagrams/UltraCanvasGourceTree.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasSlider.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasEvent.h"
#include <memory>
#include <sstream>
#include <random>
#include <ctime>
#include <vector>
#include <functional>

namespace UltraCanvas {

namespace {

// A toolbar label: as wide as its text.
std::shared_ptr<UltraCanvasLabel> MakeToolLabel(const std::string& id, const std::string& text) {
    auto label = DemoKit::CreateText(id, text);
    label->SetFontSize(11);
    return label;
}

// A button as wide as its text, at least `minWidth` (the width it had) so a
// one-character "+" stays an easy target.
std::shared_ptr<UltraCanvasButton> MakeToolButton(const std::string& id, const std::string& text,
                                                  float minWidth) {
    auto button = std::make_shared<UltraCanvasButton>(id, 0, 0, 0, 28, text);
    CSSLayout::BoxConstraints limits;
    limits.minWidth = CSSLayout::Dimension::Px(minWidth);
    button->boxConstraints = limits;
    return button;
}

// A bar of controls that wraps onto a second line when the tab is narrow.
std::shared_ptr<UltraCanvasContainer> MakeToolBar(const std::string& id) {
    auto bar = DemoKit::CreateRow(id, 8, true);
    bar->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
    bar->SetBackgroundColor(Color(248, 248, 252));
    bar->SetPadding(6);
    return bar;
}

// A tab's page: its description, wrapped, above the toolbar and the tree,
// which takes the rest of the tab's height.
std::shared_ptr<UltraCanvasContainer> MakeTabPage(const std::string& id, const std::string& descId,
                                                  const std::string& text) {
    auto tab = std::make_shared<UltraCanvasContainer>(id);
    tab->SetPadding(8, 10, 8, 10);
    tab->layout.SetFlexColumn().SetFlexGap(6)
               .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    DemoKit::AddText(tab, descId, text)->SetFontSize(11);
    return tab;
}

// The tree pans and zooms itself, so it fills what the tab leaves, never
// squeezed below 300 px. It is built at its old 980 x 540 so the first
// ZoomToFit has a size to fit into.
void AddTree(const std::shared_ptr<UltraCanvasContainer>& tab,
             const std::shared_ptr<UltraCanvasGourceTree>& tree) {
    tree->size.width  = CSSLayout::Dimension::Auto();
    tree->size.height = CSSLayout::Dimension::Auto();
    CSSLayout::BoxConstraints limits;
    limits.minHeight = CSSLayout::Dimension::Px(300);
    tree->boxConstraints = limits;
    DemoKit::Add(tab, tree, 1);
}

} // anonymous namespace

// ===== HELPER: Generate sample file system data =====
void GenerateSampleFileSystem(UltraCanvasGourceTree* tree) {
    // Create a sample project structure
    tree->SetRootNode("root", "MyProject", "/home/user/MyProject");

    // Source directory
    tree->AddDirectory("root", "src", "src");

    GourceFileInfo cppInfo;
    cppInfo.fileSize = 15000;
    cppInfo.creationTime = time(nullptr) - 86400 * 30;  // 30 days ago
    cppInfo.lastAccessTime = time(nullptr) - 3600;       // 1 hour ago
    tree->AddFile("src", "src/main.cpp", "main.cpp", cppInfo);

    cppInfo.fileSize = 8500;
    cppInfo.creationTime = time(nullptr) - 86400 * 25;
    cppInfo.lastAccessTime = time(nullptr) - 86400 * 2;
    tree->AddFile("src", "src/utils.cpp", "utils.cpp", cppInfo);

    cppInfo.fileSize = 3200;
    cppInfo.creationTime = time(nullptr) - 86400 * 20;
    cppInfo.lastAccessTime = time(nullptr) - 86400 * 5;
    tree->AddFile("src", "src/utils.h", "utils.h", cppInfo);

    // Include directory
    tree->AddDirectory("root", "include", "include");

    GourceFileInfo hInfo;
    hInfo.fileSize = 2500;
    hInfo.creationTime = time(nullptr) - 86400 * 28;
    hInfo.lastAccessTime = time(nullptr) - 86400 * 1;
    tree->AddFile("include", "include/config.h", "config.h", hInfo);

    hInfo.fileSize = 4200;
    hInfo.creationTime = time(nullptr) - 86400 * 15;
    hInfo.lastAccessTime = time(nullptr) - 86400 * 3;
    tree->AddFile("include", "include/types.h", "types.h", hInfo);

    // Docs directory
    tree->AddDirectory("root", "docs", "docs");

    GourceFileInfo mdInfo;
    mdInfo.fileSize = 12000;
    mdInfo.creationTime = time(nullptr) - 86400 * 10;
    mdInfo.lastAccessTime = time(nullptr) - 86400 * 7;
    tree->AddFile("docs", "docs/README.md", "README.md", mdInfo);

    mdInfo.fileSize = 8000;
    mdInfo.creationTime = time(nullptr) - 86400 * 5;
    mdInfo.lastAccessTime = time(nullptr) - 86400 * 1;
    tree->AddFile("docs", "docs/API.md", "API.md", mdInfo);

    // Tests directory
    tree->AddDirectory("root", "tests", "tests");

    GourceFileInfo testInfo;
    testInfo.fileSize = 5500;
    testInfo.creationTime = time(nullptr) - 86400 * 12;
    testInfo.lastAccessTime = time(nullptr) - 86400 * 2;
    tree->AddFile("tests", "tests/test_main.cpp", "test_main.cpp", testInfo);

    testInfo.fileSize = 3800;
    testInfo.creationTime = time(nullptr) - 86400 * 8;
    testInfo.lastAccessTime = time(nullptr) - 86400 * 4;
    tree->AddFile("tests", "tests/test_utils.cpp", "test_utils.cpp", testInfo);

    // Build files
    GourceFileInfo buildInfo;
    buildInfo.fileSize = 2200;
    buildInfo.creationTime = time(nullptr) - 86400 * 30;
    buildInfo.lastAccessTime = time(nullptr) - 3600 * 2;
    tree->AddFile("root", "CMakeLists.txt", "CMakeLists.txt", buildInfo);

    buildInfo.fileSize = 500;
    buildInfo.creationTime = time(nullptr) - 86400 * 30;
    buildInfo.lastAccessTime = time(nullptr) - 86400 * 20;
    tree->AddFile("root", ".gitignore", ".gitignore", buildInfo);
}

// ===== HELPER: Generate large file system for performance test =====
void GenerateLargeFileSystem(UltraCanvasGourceTree* tree, int depth, int filesPerDir) {
    tree->SetRootNode("root", "LargeProject", "/storage/LargeProject");

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> sizeDist(500, 50000);
    std::uniform_int_distribution<> daysDist(1, 365);

    std::vector<std::string> extensions = {"cpp", "h", "py", "js", "json", "md", "txt", "xml", "css", "html"};
    std::uniform_int_distribution<> extDist(0, extensions.size() - 1);

    std::function<void(const std::string&, int)> createLevel = [&](const std::string& parentId, int level) {
        if (level >= depth) return;

        // Create subdirectories
        int numDirs = (level == 0) ? 5 : 3;
        for (int d = 0; d < numDirs; d++) {
            std::string dirId = parentId + "_dir" + std::to_string(d);
            std::string dirName = "folder_" + std::to_string(level) + "_" + std::to_string(d);
            tree->AddDirectory(parentId, dirId, dirName);

            // Create files in this directory
            for (int f = 0; f < filesPerDir; f++) {
                std::string ext = extensions[extDist(gen)];
                std::string fileName = "file_" + std::to_string(f) + "." + ext;
                std::string fileId = dirId + "_" + fileName;

                GourceFileInfo info;
                info.fileSize = sizeDist(gen);
                info.creationTime = time(nullptr) - 86400 * daysDist(gen);
                info.lastAccessTime = time(nullptr) - 86400 * (daysDist(gen) % 30);

                tree->AddFile(dirId, fileId, fileName, info);
            }

            // Recurse into subdirectories
            createLevel(dirId, level + 1);
        }
    };

    createLevel("root", 0);
}

// ===== HELPER: Generate storage device visualization =====
void GenerateStorageDeviceData(UltraCanvasGourceTree* tree) {
    tree->SetRootNode("root", "Storage Drive", "/mnt/storage");

    // System folder
    tree->AddDirectory("root", "system", "System");

    GourceFileInfo sysInfo;
    sysInfo.fileSize = 500000;
    sysInfo.creationTime = time(nullptr) - 86400 * 365;
    sysInfo.lastAccessTime = time(nullptr) - 3600;
    tree->AddFile("system", "system/kernel", "kernel", sysInfo);

    sysInfo.fileSize = 150000;
    tree->AddFile("system", "system/config", "config", sysInfo);

    // User folder
    tree->AddDirectory("root", "users", "Users");
    tree->AddDirectory("users", "alice", "Alice");
    tree->AddDirectory("users", "bob", "Bob");

    // Alice's files
    tree->AddDirectory("alice", "alice_docs", "Documents");
    tree->AddDirectory("alice", "alice_pics", "Pictures");

    GourceFileInfo docInfo;
    docInfo.fileSize = 25000;
    docInfo.creationTime = time(nullptr) - 86400 * 60;
    docInfo.lastAccessTime = time(nullptr) - 86400 * 1;
    tree->AddFile("alice_docs", "alice_docs/report.docx", "report.docx", docInfo);

    docInfo.fileSize = 180000;
    docInfo.lastAccessTime = time(nullptr) - 86400 * 30;
    tree->AddFile("alice_docs", "alice_docs/presentation.pptx", "presentation.pptx", docInfo);

    GourceFileInfo picInfo;
    picInfo.fileSize = 2500000;
    picInfo.creationTime = time(nullptr) - 86400 * 45;
    picInfo.lastAccessTime = time(nullptr) - 86400 * 10;
    tree->AddFile("alice_pics", "alice_pics/vacation.jpg", "vacation.jpg", picInfo);

    picInfo.fileSize = 3200000;
    tree->AddFile("alice_pics", "alice_pics/family.png", "family.png", picInfo);

    // Bob's files
    tree->AddDirectory("bob", "bob_code", "Code");
    tree->AddDirectory("bob", "bob_music", "Music");

    GourceFileInfo codeInfo;
    codeInfo.fileSize = 8500;
    codeInfo.creationTime = time(nullptr) - 86400 * 20;
    codeInfo.lastAccessTime = time(nullptr) - 3600 * 5;
    tree->AddFile("bob_code", "bob_code/app.py", "app.py", codeInfo);

    codeInfo.fileSize = 3200;
    tree->AddFile("bob_code", "bob_code/utils.py", "utils.py", codeInfo);

    GourceFileInfo musicInfo;
    musicInfo.fileSize = 5500000;
    musicInfo.creationTime = time(nullptr) - 86400 * 90;
    musicInfo.lastAccessTime = time(nullptr) - 86400 * 60;
    tree->AddFile("bob_music", "bob_music/song1.mp3", "song1.mp3", musicInfo);

    musicInfo.fileSize = 4800000;
    musicInfo.lastAccessTime = time(nullptr) - 86400 * 120;
    tree->AddFile("bob_music", "bob_music/song2.flac", "song2.flac", musicInfo);

    // Applications folder
    tree->AddDirectory("root", "apps", "Applications");

    GourceFileInfo appInfo;
    appInfo.fileSize = 15000000;
    appInfo.creationTime = time(nullptr) - 86400 * 180;
    appInfo.lastAccessTime = time(nullptr) - 86400 * 7;
    tree->AddFile("apps", "apps/browser", "browser", appInfo);

    appInfo.fileSize = 8500000;
    appInfo.lastAccessTime = time(nullptr) - 86400 * 2;
    tree->AddFile("apps", "apps/editor", "editor", appInfo);

    appInfo.fileSize = 25000000;
    appInfo.lastAccessTime = time(nullptr) - 86400 * 14;
    tree->AddFile("apps", "apps/office", "office", appInfo);
}

// ===== MAIN DEMO IMPLEMENTATION =====
std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateGourceTreeExamples() {
    auto page = DemoKit::CreatePage("GourceTreeExamples", "Gource Tree Visualization",
        "Radial tree visualization for file systems and storage devices - inspired by Gource");
    // The trees pan and zoom themselves: the tabs fill the area.
    page->SetFillsArea(true);

    auto card = DemoKit::CreateCard("GourceCard");
    DemoKit::Add(page, card, 1);

    // ===== TABBED CONTAINER =====
    auto tabbedContainer = std::make_shared<UltraCanvasTabbedContainer>("GourceTabs");
    tabbedContainer->SetTabPosition(TabPosition::Top);
    DemoKit::Add(card, tabbedContainer, 1);

    // Status label for interaction feedback
    auto statusLabel = DemoKit::AddText(card, "GourceStatus",
        "Ready - Click on nodes to select, double-click to expand/collapse directories");
    statusLabel->SetFontSize(11);
    statusLabel->SetTextColor(Color(60, 60, 60));
    statusLabel->SetBackgroundColor(Color(240, 240, 245));
    statusLabel->SetPadding(5);

    // ========================================
    // TAB 1: PROJECT STRUCTURE EXAMPLE
    // ========================================
    auto projectContainer = MakeTabPage("ProjectTab", "ProjectDesc",
        "Project Structure: Visualize a software project with source files, docs, and tests");

    // Control panel
    auto projectControls = MakeToolBar("ProjectControls");

    DemoKit::Add(projectControls, MakeToolLabel("ThemeLabel", "Theme:"));

    auto themeDropdown = std::make_shared<UltraCanvasDropdown>(
        "ThemeDropdown", 0, 0, 110, 28
    );
    themeDropdown->AddItem("Dark");
    themeDropdown->AddItem("Light");
    themeDropdown->AddItem("Colorful");
    themeDropdown->AddItem("Mono");
    themeDropdown->SetSelectedIndex(0);
    DemoKit::Add(projectControls, themeDropdown);

    DemoKit::Add(projectControls, MakeToolLabel("LayoutLabel", "Layout:"));

    auto layoutDropdown = std::make_shared<UltraCanvasDropdown>(
        "LayoutDropdown", 0, 0, 110, 28
    );
    layoutDropdown->AddItem("Static");
    layoutDropdown->AddItem("Animated");
    layoutDropdown->AddItem("Hybrid");
    layoutDropdown->SetSelectedIndex(2);
    DemoKit::Add(projectControls, layoutDropdown);

    auto fileSizeCheck = std::make_shared<UltraCanvasCheckbox>(
        "FileSizeCheck", 0, 0, 0, 20, "Show File Size"
    );
    fileSizeCheck->SetChecked(true);
    //DemoKit::Add(projectControls, fileSizeCheck);

    DemoKit::Add(projectControls, MakeToolLabel("HighlightLabel", "Highlight:"));

    auto highlightDropdown = std::make_shared<UltraCanvasDropdown>(
        "HighlightDropdown", 0, 0, 150, 28
    );
    highlightDropdown->AddItem("None");
    highlightDropdown->AddItem("Last Access");
    highlightDropdown->AddItem("Creation Date");
    highlightDropdown->AddItem("Invert Access");
    highlightDropdown->AddItem("Invert Creation");
    highlightDropdown->SetSelectedIndex(0);
    DemoKit::Add(projectControls, highlightDropdown);

    // Stretch spacer pushes the zoom / Fit / Expand / Collapse group flush right.
    projectControls->AddStretchSpacer(1.0f);

    auto zoomInBtn = MakeToolButton("ZoomInBtn", "+", 36);
    DemoKit::Add(projectControls, zoomInBtn);

    auto zoomOutBtn = MakeToolButton("ZoomOutBtn", "-", 36);
    DemoKit::Add(projectControls, zoomOutBtn);

    auto zoomFitBtn = MakeToolButton("ZoomFitBtn", "Fit", 50);
    DemoKit::Add(projectControls, zoomFitBtn);

    auto expandAllBtn = MakeToolButton("ExpandAllBtn", "Expand", 80);
    DemoKit::Add(projectControls, expandAllBtn);

    auto collapseAllBtn = MakeToolButton("CollapseAllBtn", "Collapse", 90);
    DemoKit::Add(projectControls, collapseAllBtn);

    DemoKit::Add(projectContainer, projectControls);

    // Project Gource Tree
    auto projectTree = std::make_shared<UltraCanvasGourceTree>(
        "ProjectTree", 0, 0, 980, 540
    );

    GenerateSampleFileSystem(projectTree.get());
    projectTree->SetLayoutMode(GourceLayoutMode::Hybrid);
    projectTree->SetShowFileSizeArea(true);
    projectTree->PerformLayout();
    projectTree->ZoomToFit();

    projectTree->onNodeClick = [statusLabel](const std::string& nodeId) {
        statusLabel->SetText("Selected: " + nodeId);
    };
    projectTree->onNodeExpand = [statusLabel](const std::string& nodeId) {
        statusLabel->SetText("Expanded: " + nodeId);
    };
    projectTree->onNodeCollapse = [statusLabel](const std::string& nodeId) {
        statusLabel->SetText("Collapsed: " + nodeId);
    };
    projectTree->onNodeHover = [statusLabel](const std::string& nodeId) {
        statusLabel->SetText("Hovering: " + nodeId);
    };

    themeDropdown->onSelectionChanged = [projectTree](int index, const DropdownItem& item) {
        switch (index) {
            case 0: projectTree->SetTheme(GourceTheme::Dark); break;
            case 1: projectTree->SetTheme(GourceTheme::Light); break;
            case 2: projectTree->SetTheme(GourceTheme::Colorful); break;
            case 3: projectTree->SetTheme(GourceTheme::Monochrome); break;
        }
    };
    layoutDropdown->onSelectionChanged = [projectTree](int index, const DropdownItem& item) {
        switch (index) {
            case 0: projectTree->SetLayoutMode(GourceLayoutMode::Static); break;
            case 1: projectTree->SetLayoutMode(GourceLayoutMode::Animated); break;
            case 2: projectTree->SetLayoutMode(GourceLayoutMode::Hybrid); break;
        }
        projectTree->PerformLayout();
    };
    fileSizeCheck->onStateChanged = [projectTree](CheckedState, CheckedState newState) {
        projectTree->SetShowFileSizeArea(newState == CheckedState::Checked);
    };
    highlightDropdown->onSelectionChanged = [projectTree](int index, const DropdownItem& item) {
        switch (index) {
            case 0: projectTree->SetHighlightMode(GourceHighlightMode::NoneHighlight); break;
            case 1: projectTree->SetHighlightMode(GourceHighlightMode::LastAccess); break;
            case 2: projectTree->SetHighlightMode(GourceHighlightMode::CreationDate); break;
            case 3: projectTree->SetHighlightMode(GourceHighlightMode::InvertLastAccess); break;
            case 4: projectTree->SetHighlightMode(GourceHighlightMode::InvertCreationDate); break;
        }
    };
    zoomInBtn->onClick = [projectTree]() { projectTree->ZoomIn(); };
    zoomOutBtn->onClick = [projectTree]() { projectTree->ZoomOut(); };
    zoomFitBtn->onClick = [projectTree]() { projectTree->ZoomToFit(); };
    expandAllBtn->onClick = [projectTree]() { projectTree->ExpandAll(); };
    collapseAllBtn->onClick = [projectTree]() { projectTree->CollapseAll(); };

    AddTree(projectContainer, projectTree);

    // ========================================
    // TAB 2: STORAGE DEVICE VISUALIZATION
    // ========================================
    auto storageContainer = MakeTabPage("StorageTab", "StorageDesc",
        "Storage Device: Visualize disk contents with file sizes and access patterns");

    auto depthControls = MakeToolBar("DepthControls");

    DemoKit::Add(depthControls, MakeToolLabel("DepthLabel", "Max Depth:"));

    auto depthSlider = std::make_shared<UltraCanvasSlider>(
        "DepthSlider", 0, 0, 150, 25
    );
    depthSlider->SetRange(1, 10);
    depthSlider->SetValue(5);
    depthSlider->SetStep(1);
    DemoKit::Add(depthControls, depthSlider);

    // The value readout keeps its old 30 px as a least width, so "5" -> "10"
    // does not shift the controls after it.
    auto depthValueLabel = MakeToolLabel("DepthValue", "5");
    CSSLayout::BoxConstraints valueLimits;
    valueLimits.minWidth = CSSLayout::Dimension::Px(30);
    depthValueLabel->boxConstraints = valueLimits;
    DemoKit::Add(depthControls, depthValueLabel);

    auto unlimitedCheck = std::make_shared<UltraCanvasCheckbox>(
        "UnlimitedCheck", 0, 0, 0, 20, "Unlimited"
    );
    unlimitedCheck->SetChecked(false);
    DemoKit::Add(depthControls, unlimitedCheck);

    // Stretch spacer pushes the highlight / Reset / Export group flush right.
    depthControls->AddStretchSpacer(1.0f);

    auto showOldBtn = MakeToolButton("ShowOldBtn", "Highlight Old Files", 170);
    DemoKit::Add(depthControls, showOldBtn);

    auto showRecentBtn = MakeToolButton("ShowRecentBtn", "Highlight Recent", 165);
    DemoKit::Add(depthControls, showRecentBtn);

    auto resetStorageBtn = MakeToolButton("ResetStorageBtn", "Reset", 80);
    DemoKit::Add(depthControls, resetStorageBtn);

    auto exportBtn = MakeToolButton("ExportBtn", "Export SVG", 110);
    DemoKit::Add(depthControls, exportBtn);

    DemoKit::Add(storageContainer, depthControls);

    auto storageTree = std::make_shared<UltraCanvasGourceTree>(
        "StorageTree", 0, 0, 980, 540
    );

    GenerateStorageDeviceData(storageTree.get());
    storageTree->SetLayoutMode(GourceLayoutMode::Hybrid);
    storageTree->SetShowFileSizeArea(true);
    storageTree->SetTheme(GourceTheme::Dark);
    storageTree->SetMaxDepth(5);
    storageTree->PerformLayout();

    storageTree->onNodeClick = [statusLabel](const std::string& nodeId) {
        statusLabel->SetText("Selected: " + nodeId);
    };
    storageTree->onNodeDoubleClick = [statusLabel](const std::string& nodeId) {
        statusLabel->SetText("Double-clicked: " + nodeId + " (expanded/collapsed)");
    };

    depthSlider->onValueChanged = [storageTree, depthValueLabel, unlimitedCheck](float value) {
        if (!unlimitedCheck->IsChecked()) {
            int depth = static_cast<int>(value);
            depthValueLabel->SetText(std::to_string(depth));
            storageTree->SetMaxDepth(depth);
        }
    };
    unlimitedCheck->onStateChanged = [storageTree, depthSlider, depthValueLabel](CheckedState, CheckedState newState) {
        if (newState == CheckedState::Checked) {
            storageTree->SetMaxDepth(-1);
            depthValueLabel->SetText("∞");
        } else {
            int depth = static_cast<int>(depthSlider->GetValue());
            storageTree->SetMaxDepth(depth);
            depthValueLabel->SetText(std::to_string(depth));
        }
    };
    showOldBtn->onClick = [storageTree, statusLabel]() {
        storageTree->SetHighlightMode(GourceHighlightMode::InvertLastAccess);
        statusLabel->SetText("Highlighting old/unused files (more opaque = older)");
    };
    showRecentBtn->onClick = [storageTree, statusLabel]() {
        storageTree->SetHighlightMode(GourceHighlightMode::LastAccess);
        statusLabel->SetText("Highlighting recent files (more opaque = recently accessed)");
    };
    resetStorageBtn->onClick = [storageTree, statusLabel]() {
        storageTree->SetHighlightMode(GourceHighlightMode::NoneHighlight);
        storageTree->ResetView();
        statusLabel->SetText("View reset");
    };
    exportBtn->onClick = [storageTree, statusLabel]() {
        if (storageTree->SaveToSVG("/tmp/storage_tree.svg")) {
            statusLabel->SetText("Exported to /tmp/storage_tree.svg");
        } else {
            statusLabel->SetText("Export failed!");
        }
    };

    AddTree(storageContainer, storageTree);

//    // ========================================
//    // TAB 3: PERFORMANCE TEST
//    // ========================================
//    auto perfContainer = std::make_shared<UltraCanvasContainer>(
//        "PerfTab", 0, 0, kDefaultWidth - 30, 640
//    );
//
//    auto perfDesc = std::make_shared<UltraCanvasLabel>(
//        "PerfDesc", 10, 10, kDefaultWidth - 50, 25
//    );
//    perfDesc->SetText("Performance Test: Render large file system trees with thousands of nodes");
//    perfDesc->SetFontSize(11);
//    perfContainer->AddChild(perfDesc);
//
//    auto perfControls = std::make_shared<UltraCanvasContainer>(
//        "PerfControls", 10, 40, kDefaultWidth - 50, 45
//    );
//    perfControls->SetBackgroundColor(Color(248, 248, 252));
//
//    auto sizeBtn500 = std::make_shared<UltraCanvasButton>(
//        "Size500Btn", 0, 8, 130, 28
//    );
//    sizeBtn500->SetText("~500 nodes");
//    perfControls->AddChild(sizeBtn500);
//
//    auto sizeBtn2k = std::make_shared<UltraCanvasButton>(
//        "Size2kBtn", 0, 8, 100, 28
//    );
//    sizeBtn2k->SetText("~2000 nodes");
//    perfControls->AddChild(sizeBtn2k);
//
//    auto sizeBtn5k = std::make_shared<UltraCanvasButton>(
//        "Size5kBtn", 0, 8, 100, 28
//    );
//    sizeBtn5k->SetText("~5000 nodes");
//    perfControls->AddChild(sizeBtn5k);
//
//    auto staticModeBtn = std::make_shared<UltraCanvasButton>(
//        "StaticModeBtn", 0, 8, 90, 28
//    );
//    staticModeBtn->SetText("Static");
//    perfControls->AddChild(staticModeBtn);
//
//    auto animatedModeBtn = std::make_shared<UltraCanvasButton>(
//        "AnimatedModeBtn", 0, 8, 130, 28
//    );
//    animatedModeBtn->SetText("Animated");
//    perfControls->AddChild(animatedModeBtn);
//
//    auto nodeCountLabel = std::make_shared<UltraCanvasLabel>(
//        "NodeCount", 0, 12, 200, 20
//    );
//    nodeCountLabel->SetText("Click a button to generate");
//    nodeCountLabel->SetFontSize(11);
//    perfControls->AddChild(nodeCountLabel);
//
//    perfContainer->AddChild(perfControls);
//
//    auto perfTree = std::make_shared<UltraCanvasGourceTree>(
//        "PerfTree", 10, 90, kDefaultWidth - 50, 540
//    );
//
//    perfTree->SetRootNode("root", "EmptyProject", "/empty");
//    perfTree->SetLayoutMode(GourceLayoutMode::Hybrid);
//    perfTree->PerformLayout();
//
//    auto perfTreePtr = perfTree.get();
//    auto perfHandler = [perfTreePtr, statusLabel, nodeCountLabel](int depth, int filesPerDir) {
//        perfTreePtr->ClearAll();
//
//        auto startTime = std::chrono::high_resolution_clock::now();
//        GenerateLargeFileSystem(perfTreePtr, depth, filesPerDir);
//        perfTreePtr->PerformLayout();
//        auto endTime = std::chrono::high_resolution_clock::now();
//
//        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
//
//        std::stringstream ss;
//        ss << "Generated and laid out in " << duration << "ms";
//        statusLabel->SetText(ss.str());
//
//        nodeCountLabel->SetText("Layout time: " + std::to_string(duration) + "ms");
//    };
//
//    sizeBtn500->onClick = [perfHandler]() { perfHandler(3, 10); };
//    sizeBtn2k->onClick   = [perfHandler]() { perfHandler(4, 12); };
//    sizeBtn5k->onClick   = [perfHandler]() { perfHandler(5, 15); };
//
//    staticModeBtn->onClick = [perfTree, statusLabel]() {
//        perfTree->SetLayoutMode(GourceLayoutMode::Static);
//        perfTree->PerformLayout();
//        statusLabel->SetText("Switched to Static layout mode");
//    };
//    animatedModeBtn->onClick = [perfTree, statusLabel]() {
//        perfTree->SetLayoutMode(GourceLayoutMode::Animated);
//        perfTree->PerformLayout();
//        statusLabel->SetText("Switched to Animated layout mode");
//    };
//
//    perfContainer->AddChild(perfTree);
//
//    // ========================================
//    // TAB 4: CUSTOM BUILDER
//    // ========================================
//    auto customContainer = std::make_shared<UltraCanvasContainer>(
//        "CustomTab", 0, 0, kDefaultWidth - 30, 640
//    );
//
//    auto customDesc = std::make_shared<UltraCanvasLabel>(
//        "CustomDesc", 10, 10, kDefaultWidth - 50, 25
//    );
//    customDesc->SetText("Custom Builder: Create your own file tree by adding nodes manually");
//    customDesc->SetFontSize(11);
//    customContainer->AddChild(customDesc);
//
//    auto customControls = std::make_shared<UltraCanvasContainer>(
//        "CustomControls", 10, 40, kDefaultWidth - 50, 80
//    );
//    customControls->SetBackgroundColor(Color(248, 248, 252));
//
//    // ----- ROW 1 (y=8..36, labels y=12) -----
//    auto parentLabel = std::make_shared<UltraCanvasLabel>(
//        "ParentLabel", 0, 12, 50, 20
//    );
//    parentLabel->SetText("Parent:");
//    parentLabel->SetFontSize(11);
//    customControls->AddChild(parentLabel);
//
//    auto parentInput = std::make_shared<UltraCanvasTextInput>(
//        "ParentInput", 0, 8, 110, 28
//    );
//    parentInput->SetText("root");
//    customControls->AddChild(parentInput);
//
//    auto nameLabel = std::make_shared<UltraCanvasLabel>(
//        "NameLabel", 0, 12, 50, 20
//    );
//    nameLabel->SetText("Name:");
//    nameLabel->SetFontSize(11);
//    customControls->AddChild(nameLabel);
//
//    auto nameInput = std::make_shared<UltraCanvasTextInput>(
//        "NameInput", 0, 8, 140, 28
//    );
//    customControls->AddChild(nameInput);
//
//    auto typeLabel = std::make_shared<UltraCanvasLabel>(
//        "TypeLabel", 0, 12, 40, 20
//    );
//    typeLabel->SetText("Type:");
//    typeLabel->SetFontSize(11);
//    customControls->AddChild(typeLabel);
//
//    auto typeDropdown = std::make_shared<UltraCanvasDropdown>(
//        "TypeDropdown", 0, 8, 90, 28
//    );
//    typeDropdown->AddItem("File");
//    typeDropdown->AddItem("Folder");
//    typeDropdown->SetSelectedIndex(0);
//    customControls->AddChild(typeDropdown);
//
//    auto sizeLabel = std::make_shared<UltraCanvasLabel>(
//        "SizeLabel", 0, 12, 65, 20
//    );
//    sizeLabel->SetText("Size (KB):");
//    sizeLabel->SetFontSize(11);
//    customControls->AddChild(sizeLabel);
//
//    auto sizeInput = std::make_shared<UltraCanvasTextInput>(
//        "SizeInput", 0, 8, 70, 28
//    );
//    sizeInput->SetText("10");
//    customControls->AddChild(sizeInput);
//
//    auto addNodeBtn = std::make_shared<UltraCanvasButton>(
//        "AddNodeBtn", 0, 8, 70, 28
//    );
//    addNodeBtn->SetText("Add");
//    customControls->AddChild(addNodeBtn);
//
//    auto clearBtn = std::make_shared<UltraCanvasButton>(
//        "ClearBtn", 0, 8, 70, 28
//    );
//    clearBtn->SetText("Clear");
//    customControls->AddChild(clearBtn);
//
//    // ----- ROW 2 (y=46..72, labels y=50) -----
//    auto presetLabel = std::make_shared<UltraCanvasLabel>(
//        "PresetLabel", 0, 50, 60, 20
//    );
//    presetLabel->SetText("Presets:");
//    presetLabel->SetFontSize(11);
//    customControls->AddChild(presetLabel);
//
//    auto presetProjectBtn = std::make_shared<UltraCanvasButton>(
//        "PresetProjectBtn", 0, 46, 90, 26
//    );
//    presetProjectBtn->SetText("Project");
//    customControls->AddChild(presetProjectBtn);
//
//    auto presetStorageBtn = std::make_shared<UltraCanvasButton>(
//        "PresetStorageBtn", 0, 46, 90, 26
//    );
//    presetStorageBtn->SetText("Storage");
//    customControls->AddChild(presetStorageBtn);
//
//    customContainer->AddChild(customControls);
//
//    auto customTree = std::make_shared<UltraCanvasGourceTree>(
//        "CustomTree", 10, 125, kDefaultWidth - 50, 505
//    );
//
//    customTree->SetRootNode("root", "CustomRoot", "/custom");
//    customTree->SetLayoutMode(GourceLayoutMode::Hybrid);
//    customTree->SetShowFileSizeArea(true);
//    customTree->PerformLayout();
//
//    customTree->onNodeClick = [statusLabel, parentInput](const std::string& nodeId) {
//        statusLabel->SetText("Selected: " + nodeId);
//        parentInput->SetText(nodeId);
//    };
//
//    addNodeBtn->onClick = [customTree, parentInput, nameInput, typeDropdown, sizeInput, statusLabel]() {
//        std::string parent = parentInput->GetText();
//        std::string name = nameInput->GetText();
//        bool isFolder = (typeDropdown->GetSelectedIndex() == 1);
//
//        if (name.empty()) {
//            statusLabel->SetText("Please enter a name!");
//            return;
//        }
//
//        std::string nodeId = parent + "/" + name;
//
//        if (isFolder) {
//            customTree->AddDirectory(parent, nodeId, name);
//            statusLabel->SetText("Added folder: " + name);
//        } else {
//            GourceFileInfo info;
//            try {
//                info.fileSize = std::stoi(sizeInput->GetText()) * 1024;
//            } catch (...) {
//                info.fileSize = 10240;
//            }
//            info.creationTime = time(nullptr);
//            info.lastAccessTime = time(nullptr);
//            customTree->AddFile(parent, nodeId, name, info);
//            statusLabel->SetText("Added file: " + name);
//        }
//
//        customTree->PerformLayout();
//        nameInput->SetText("");
//    };
//
//    clearBtn->onClick = [customTree, statusLabel]() {
//        customTree->ClearAll();
//        customTree->SetRootNode("root", "CustomRoot", "/custom");
//        customTree->PerformLayout();
//        statusLabel->SetText("Tree cleared");
//    };
//
//    presetProjectBtn->onClick = [customTree, statusLabel]() {
//        customTree->ClearAll();
//        GenerateSampleFileSystem(customTree.get());
//        customTree->PerformLayout();
//        statusLabel->SetText("Loaded Project preset");
//    };
//
//    presetStorageBtn->onClick = [customTree, statusLabel]() {
//        customTree->ClearAll();
//        GenerateStorageDeviceData(customTree.get());
//        customTree->PerformLayout();
//        statusLabel->SetText("Loaded Storage preset");
//    };
//
//    customContainer->AddChild(customTree);

    // ===== ADD TABS =====
    tabbedContainer->AddTab("Project", projectContainer);
    tabbedContainer->AddTab("Storage", storageContainer);
//    tabbedContainer->AddTab("Performance", perfContainer);
//    tabbedContainer->AddTab("Custom", customContainer);

    return page;
}

} // namespace UltraCanvas