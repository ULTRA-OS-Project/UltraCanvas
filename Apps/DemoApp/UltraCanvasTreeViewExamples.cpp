// Apps/DemoApp/UltraCanvasDemoExamples.cpp
// Implementation of all component example creators
// Version: 1.2.0 - the multi-selection tree shows the arrow expanders (TreeExpanderStyle::Chevron)
// Version: 1.1.0
// Last Modified: 2026-10-22
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasColumnsTreeView.h"
#include "UltraCanvasGroupBox.h"
#include "UltraCanvasSegmentedControl.h"
#include "Plugins/Charts/UltraCanvasDivergingBarChart.h"
#include <sstream>
#include <random>
#include <map>
#include "UltraCanvasDebug.h"

namespace UltraCanvas {
    // Each demo tree sits in a header-style group box: the caption names the
    // demo, and the tree plus the options that drive it stack inside the frame.
    static std::shared_ptr<UltraCanvasGroupBox> TreeDemoGroupBox(const std::string& id,
                                                                 float x, float y, float h,
                                                                 const std::string& caption) {
        auto gb = CreateGroupBox(id, x, y, 318, h, caption);
        gb->SetFrameStyle(GroupBoxFrameStyle::Header);
        GroupBoxVisualStyle st = gb->GetVisualStyle();
        st.headerBackgroundColor = Color(235, 238, 245, 255);
        st.showHeaderSeparator = true;
        // 10% under the default caption size, so the longer demo titles fit
        // the 318px box.
        st.titleFont.fontSize *= 0.9f;
        gb->SetVisualStyle(st);
        // A flex column, so the options get a gap below the tree (block layout
        // ignores child margins).
        gb->layout.SetFlex(CSSLayout::FlexDirection::Column).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        gb->layout.SetFlexGap(4.0f);
        return gb;
    }

    // An option below the tree.
    static std::shared_ptr<UltraCanvasCheckbox> TreeDemoOption(const std::string& id,
                                                               const std::string& text,
                                                               bool checked) {
        auto cb = std::make_shared<UltraCanvasCheckbox>(id, 300.0f, 24.0f, text);
        cb->SetChecked(checked);
        return cb;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateTreeViewExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("TreeViewExamples", 0, 0, 1000, 820);
        container->SetPadding(0,0,10,0);

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("TreeViewTitle", 10, 10, 300, 30);
        title->SetText("TreeView Examples");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // File Explorer Style Tree
        auto fileBox = TreeDemoGroupBox("FileTreeBox", 10, 50, 500, "File Explorer Style TreeView");
        container->AddChild(fileBox);
        auto fileTree = std::make_shared<UltraCanvasTreeView>("FileTree", 300, 400);
        fileTree->SetRowHeight(22);
        fileTree->SetSelectionMode(TreeSelectionMode::Single);

        // Setup file tree structure: a navigation pane in the style of a desktop
        // file manager - pinned folders, then the computer with the user's home,
        // cloud storage, remote drives and the system drive. The root is hidden,
        // so "Pinned" and "Computer" are the top level.
        fileTree->SetRootNode(TreeNodeData("root", "Root"));
        fileTree->SetRootVisible(false);
        fileTree->SetShowRootLines(true);

        auto icon = [](const std::string& name) {
            return TreeNodeIcon(NormalizePath(GetResourcesDir() + "media/icons/" + name), 16, 16);
        };
        auto addNode = [&](const std::string& parentId, const std::string& id,
                           const std::string& text, const std::string& iconName) {
            TreeNodeData data(id, text);
            data.leftIcon = icon(iconName);
            return fileTree->AddNode(parentId, data);
        };
        // A folder; each name in `subfolders` becomes a child folder, so the
        // row gets an expand button.
        auto addFolder = [&](const std::string& parentId, const std::string& id,
                             const std::string& text,
                             std::initializer_list<const char*> subfolders = {}) {
            TreeNode* node = addNode(parentId, id, text, "folder-brown.svg");
            int i = 0;
            for (const char* sub : subfolders)
                addNode(id, id + "_" + std::to_string(i++), sub, "folder-brown.svg");
            return node;
        };

        TreeNode* pinned = addNode("root", "pinned", "Pinned", "rating-heart-on.svg");
        addFolder("pinned", "pin_documents", "Documents");
        addFolder("pinned", "pin_work", "Work");
        addFolder("pinned", "pin_ultraos", "ULTRA OS");
        addFolder("pinned", "pin_privat", "privat");

        TreeNode* computer = addNode("root", "computer", "Computer", "computer.png");

        TreeNode* home = addNode("computer", "home", "Home", "home-user.svg");
        addNode("home", "home_downloads", "Downloads", "folder-download.svg");
        addNode("home", "home_documents", "Dokumente", "folder-documents.svg");
        addNode("home", "home_desktop", "Desktop", "user-desktop.svg");
        addNode("home", "home_pictures", "Bilder", "folder-images.svg");
        addNode("home", "home_videos", "Videos", "folder-videos.svg");
        addNode("home", "home_music", "Music", "folder-music.svg");

        TreeNode* cloud = addNode("computer", "cloud", "Cloud Storage", "cloud.svg");
        addNode("cloud", "onedrive", "OneDrive", "cloud.svg");
        addFolder("onedrive", "onedrive_docs", "Documents");
        addFolder("onedrive", "onedrive_pics", "Pictures");
        addNode("cloud", "dropbox", "Dropbox", "cloud.svg");
        addFolder("dropbox", "dropbox_shared", "Shared");
        addFolder("dropbox", "dropbox_photos", "Photos");

        TreeNode* remote = addNode("computer", "remote", "Remote Drives", "cloud.svg");
        addNode("remote", "ftp_chutipa", "FTP: Chutipa  Cloud", "cloud.svg");
        addFolder("ftp_chutipa", "ftp_public", "public_html");
        addFolder("ftp_chutipa", "ftp_backup", "backup");

        TreeNode* driveC = addNode("computer", "drive_c", "C:", "drive.png");
        addFolder("drive_c", "c_adwcleaner", "AdwCleaner", {"Logs", "Quarantine"});
        addFolder("drive_c", "c_avermedia", "AVerMedia");
        addFolder("drive_c", "c_beckercad", "BeckerCAD14", {"Bin", "Samples"});
        addFolder("drive_c", "c_drivers", "Drivers", {"Audio", "Chipset", "Network"});
        addFolder("drive_c", "c_inetpub", "inetpub");
        addFolder("drive_c", "c_mp3", "MP3");
        addFolder("drive_c", "c_paragliding", "paragliding");
        addFolder("drive_c", "c_perflogs", "PerfLogs");
        addFolder("drive_c", "c_programfiles", "Program Files", {"Common Files", "Internet Explorer", "Windows Defender"});
        addFolder("drive_c", "c_programfiles86", "Program Files (x86)", {"Common Files", "Microsoft"});
        addFolder("drive_c", "c_projekte", "Projekte", {"UltraCanvas", "UltraOS"});
        addFolder("drive_c", "c_qt", "Qt", {"Docs", "Tools"});
        addFolder("drive_c", "c_riscos", "RISCOS", {"Apps", "Boot"});
        addFolder("drive_c", "c_security", "Security camera suggies");
        addFolder("drive_c", "c_users", "Users", {"Public", "Default"});
        addFolder("drive_c", "c_windows", "Windows", {"Fonts", "System32", "Temp"});

        fileTree->onNodeSelected = [](TreeNode* node) {
            debugOutput << "Selected: " << node->data.text << std::endl;
        };

        pinned->Expand();
        computer->Expand();
        cloud->Expand();
        remote->Expand();
        driveC->Expand();
        fileTree->SelectNode(home);
        fileBox->AddChild(fileTree);

        // Options for the File Explorer tree
        auto autoExpandCheckbox = TreeDemoOption("AutoExpandCheckbox", "Auto expand selected node", false);
        autoExpandCheckbox->onStateChanged = [fileTree](CheckedState, CheckedState newState) {
            fileTree->SetAutoExpandSelectedNode(newState == CheckedState::Checked);
        };
        fileBox->AddChild(autoExpandCheckbox);

        auto autoSelectFirstChildCheckbox = TreeDemoOption("AutoSelectFirstChildCheckbox", "Auto select first child of expanded node", false);
        autoSelectFirstChildCheckbox->onStateChanged = [fileTree](CheckedState, CheckedState newState) {
            fileTree->SetShowFirstChildOnExpand(newState == CheckedState::Checked);
        };
        fileBox->AddChild(autoSelectFirstChildCheckbox);

        // Multi-Selection Tree, with the arrow expanders: an arrow pointing
        // right on a closed row and down on an open one, instead of the +/- box.
        auto multiBox = TreeDemoGroupBox("MultiTreeBox", 340, 50, 300, "Multi-Selection (Ctrl+Click), arrows");
        container->AddChild(multiBox);
        auto multiTree = std::make_shared<UltraCanvasTreeView>("MultiTree", 300, 200);
        multiTree->SetRowHeight(20);
        multiTree->SetSelectionMode(TreeSelectionMode::Multiple);
        multiTree->SetExpanderStyle(TreeExpanderStyle::Chevron);
        multiTree->SetLineStyle(TreeLineStyle::NoLine);

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
        // "move to the top" and "go to selection" buttons in the bottom-right
        // corner show up.
        for (int cat = 3; cat <= 8; ++cat) {
            std::string catId = "cat" + std::to_string(cat);
            multiTree->AddNode("multi_root", TreeNodeData(catId, "Category " + std::to_string(cat)));
            for (int item = 1; item <= 3; ++item) {
                multiTree->AddNode(catId, TreeNodeData(catId + "_item" + std::to_string(item),
                                                       "Item " + std::to_string(item)));
            }
        }

        multiTree->ExpandAll();
        multiBox->AddChild(multiTree);

        // The expander's look: the arrows (on), or the classic +/- box with
        // the dotted connecting lines it is drawn with elsewhere on this page.
        auto arrowCheckbox = TreeDemoOption("ArrowExpanderCheckbox", "Arrow expanders (SetExpanderStyle)", true);
        arrowCheckbox->onStateChanged = [multiTree](CheckedState, CheckedState newState) {
            const bool arrows = newState == CheckedState::Checked;
            multiTree->SetExpanderStyle(arrows ? TreeExpanderStyle::Chevron : TreeExpanderStyle::Box);
            multiTree->SetLineStyle(arrows ? TreeLineStyle::NoLine : TreeLineStyle::Dotted);
            multiTree->RequestRedraw();
        };
        multiBox->AddChild(arrowCheckbox);

        // Scroll-to-top button toggle (the feature is on by default).
        auto scrollTopCheckbox = TreeDemoOption(
            "ScrollToTopCheckbox", "Show \"move to the top\" button", true);
        scrollTopCheckbox->onStateChanged = [multiTree](CheckedState, CheckedState newState) {
            multiTree->SetShowScrollToTopButton(newState == CheckedState::Checked);
        };
        multiBox->AddChild(scrollTopCheckbox);

        // Scroll-to-selection button toggle (also on by default): select a row,
        // scroll it away, and the button above the top button brings it back.
        auto scrollSelCheckbox = TreeDemoOption(
            "ScrollToSelectionCheckbox", "Show \"go to selection\" button", true);
        scrollSelCheckbox->onStateChanged = [multiTree](CheckedState, CheckedState newState) {
            multiTree->SetShowScrollToSelectionButton(newState == CheckedState::Checked);
        };
        multiBox->AddChild(scrollSelCheckbox);

        // ----- Debugger "Variables" panel: Classic vs Modern columns -----
        // Demonstrates the columnar display mode (Name / Type / Value with an accent
        // Type column and section-header bars) an IDE debugger would use, plus the
        // Classic/Modern layout toggle and Alphabetic/Last-access sort options.
        auto varsBox = TreeDemoGroupBox("VarsTreeBox", 670, 50, 430, "Debugger Variables (Modern columns)");
        container->AddChild(varsBox);
        auto varsTree = std::make_shared<UltraCanvasColumnsTreeView>("VarsTree", 300, 330);
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

        varsBox->AddChild(varsTree);

        // Layout toggle: Classic (single text) <-> Modern (columns)
        auto modernCheckbox = TreeDemoOption(
            "ModernLayoutCheckbox", "Modern layout (Name / Type / Value)", true);
        modernCheckbox->onStateChanged = [varsTree](CheckedState, CheckedState newState) {
            varsTree->SetDisplayMode(newState == CheckedState::Checked
                                         ? TreeDisplayMode::Columns
                                         : TreeDisplayMode::Classic);
        };
        varsBox->AddChild(modernCheckbox);

        // Sort toggle: Alphabetic <-> Last access
        auto sortCheckbox = TreeDemoOption(
            "SortLastAccessCheckbox", "Sort by last access (else alphabetic)", false);
        sortCheckbox->onStateChanged = [varsTree](CheckedState, CheckedState newState) {
            if (newState == CheckedState::Checked) {
                varsTree->SetSortMode(TreeSortMode::LastAccess, /*ascending=*/false);
            } else {
                varsTree->SetSortMode(TreeSortMode::Alphabetic, /*ascending=*/true);
            }
        };
        varsBox->AddChild(sortCheckbox);

        // ----- Connecting lines: None / Dotted / Solid, and the root-level trunk -----
        // A forest (hidden root, so the sections are the top level) is the case the
        // connectors were made for: without them the rows of three open sections are
        // just indentation. The segmented control switches TreeLineStyle live.
        auto linesBox = TreeDemoGroupBox("LinesTreeBox", 340, 366, 296, "Connecting lines (SetLineStyle)");
        container->AddChild(linesBox);
        auto linesTree = std::make_shared<UltraCanvasTreeView>("LinesTree", 300, 190);
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
        linesBox->AddChild(linesTree);

        auto lineStyleControl = SegmentedControlBuilder("LineStyleSegments", -1, -1, 300, 28)
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
        linesBox->AddChild(lineStyleControl);

        // Root lines: the trunk down the left margin that ties the three sections
        // together. It costs one indent of left margin, which is why it can be
        // switched off. (It only applies to a forest — a visible root row is
        // already the trunk everything hangs from.)
        auto rootLinesCheckbox = TreeDemoOption(
            "RootLinesCheckbox", "Connect the top-level rows too", true);
        rootLinesCheckbox->onStateChanged = [linesTree](CheckedState, CheckedState newState) {
            linesTree->SetShowRootLines(newState == CheckedState::Checked);
        };
        linesBox->AddChild(rootLinesCheckbox);

        // ----- Check flags: a tri-state selection independent of the row selection -----
        auto flagBox = TreeDemoGroupBox("FlagTreeBox", 670, 496, 288, "Check flags (SetShowCheckboxes)");
        container->AddChild(flagBox);
        auto flagTree = std::make_shared<UltraCanvasTreeView>("FlagTree", 300, 190);
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
        flagBox->AddChild(flagTree);

        // Propagation: on, a folder's flag carries to its files and the folder shows
        // "some" as a filled square; off, every row carries its own flag.
        auto propagateCheckbox = TreeDemoOption(
            "FlagPropagateCheckbox", "Flag the whole subtree", true);
        propagateCheckbox->onStateChanged = [flagTree](CheckedState, CheckedState newState) {
            flagTree->SetCheckPropagation(newState == CheckedState::Checked);
        };
        flagBox->AddChild(propagateCheckbox);

        auto flagStatus = std::make_shared<UltraCanvasLabel>("FlagStatusLabel", 300, 22);
        flagStatus->SetFontSize(12);
        // Weak capture would be cleaner, but the label outlives the tree here: both
        // belong to the same group box.
        auto updateFlagStatus = [flagTree, flagStatus]() {
            const size_t flagged = flagTree->GetCheckedNodes().size();
            flagStatus->SetText(std::to_string(flagged) + " of 12 rows flagged");
        };
        updateFlagStatus();
        flagTree->onNodeCheckChanged = [updateFlagStatus](TreeNode*, TreeCheckState) {
            updateFlagStatus();
        };
        flagBox->AddChild(flagStatus);

        return container;
    }

} // namespace UltraCanvas