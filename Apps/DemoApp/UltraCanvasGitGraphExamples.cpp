// Apps/DemoApp/UltraCanvasGitGraphExamples.cpp
// Interactive Git commit-graph demonstration page.
//
// Three tabs, one per reference layout:
//   1. Git Flow          - swimlane bands (main / hotfixes / release / develop /
//                          feature) with tags and callout annotations.
//   2. Branch Operations - a trunk with one branch above and one below, each
//                          commit carrying its changed-file boxes.
//   3. Repository        - the lane view every git client shows: newest commit
//                          at the top, ref chips, subject column, dark theme.
//
// Version: 1.1.0 - the shared page look (UltraCanvasDemoPageKit): the tabs in a card
//                  that fills the area, each graph fills its tab, the toolbars wrap
//                  and every label and button is as wide as its text
// Version: 1.0.1 - the file-list provider no longer owns the graph it is stored on
// Version: 1.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "Plugins/Diagrams/UltraCanvasGitGraph.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasTextInput.h"

#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

namespace {

// A toolbar label: as wide as its text.
std::shared_ptr<UltraCanvasLabel> MakeLabel(const std::string& id, const std::string& text) {
    auto label = DemoKit::CreateText(id, text);
    label->SetFontSize(11.0f);
    return label;
}

// A button as wide as its text, at least `minWidth` (the width it had) so a
// one-character "+" stays an easy target.
std::shared_ptr<UltraCanvasButton> MakeButton(const std::string& id, const std::string& text,
                                              float minWidth) {
    auto button = std::make_shared<UltraCanvasButton>(id, 0, 0, 0, 28, text);
    CSSLayout::BoxConstraints limits;
    limits.minWidth = CSSLayout::Dimension::Px(minWidth);
    button->boxConstraints = limits;
    return button;
}

// A checkbox as wide as its text.
std::shared_ptr<UltraCanvasCheckbox> MakeCheck(const std::string& id, const std::string& text) {
    return std::make_shared<UltraCanvasCheckbox>(id, 0, 0, 0, 22, text);
}

// A bar of controls that wraps onto a second line when the tab is narrow.
std::shared_ptr<UltraCanvasContainer> MakeBar(const std::string& id) {
    auto bar = DemoKit::CreateRow(id, 8, true);
    bar->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
    return bar;
}

// A tab's page: its description, wrapped, above the toolbars and the graph,
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

// The graph pans and zooms itself, so it fills what the tab leaves; built
// with no size, never squeezed below 300 px.
void AddGraph(const std::shared_ptr<UltraCanvasContainer>& tab,
              const std::shared_ptr<UltraCanvasGitGraph>& graph) {
    graph->size.width  = CSSLayout::Dimension::Auto();
    graph->size.height = CSSLayout::Dimension::Auto();
    CSSLayout::BoxConstraints limits;
    limits.minHeight = CSSLayout::Dimension::Px(300);
    graph->boxConstraints = limits;
    DemoKit::Add(tab, graph, 1);
}

// ---------------------------------------------------------------------------
// Tab 1 - git-flow swimlanes
// ---------------------------------------------------------------------------

void BuildGitFlowHistory(UltraCanvasGitGraph* graph) {
    graph->SetLayoutMode(GitGraphLayoutMode::Swimlane);
    graph->SetOrientation(GitGraphOrientation::LeftToRight);
    graph->SetTrunkBranch("main");
    graph->SetSwimlaneOrder({"hotfixes", "release branches", "develop", "feature branches"});
    graph->SetSwimlanesBothSides(false);   // Stack the bands in git-flow order

    // The authoring API appends oldest-first and records the branch on each
    // commit, so the swimlane bands need no first-parent reconstruction.
    graph->Branch("main");
    const std::string main0 = graph->Commit("initial commit", "0.1");

    graph->Branch("develop");
    graph->Commit("start develop");
    const std::string dev1 = graph->Commit("feature work");

    graph->Branch("feature branches");
    graph->Commit("major feature for the next release");
    const std::string feat1 = graph->Commit("finish feature");

    graph->Checkout("develop");
    graph->Merge("feature branches", "merge feature");

    graph->Branch("release branches");
    graph->Commit("release branch for 1.0");
    const std::string rel1 = graph->Commit("only bugfixes");

    graph->Checkout("main");
    graph->Merge("release branches", "release 1.0");
    graph->Tag("0.2");

    graph->Branch("hotfixes");
    const std::string hot1 = graph->Commit("hotfix 0.2");

    graph->Checkout("main");
    graph->Merge("hotfixes", "merge hotfix");
    graph->Tag("1.0");

    graph->Checkout("develop");
    graph->Merge("hotfixes", "hotfix in develop");

    graph->AddAnnotation(rel1,  "only bugfixes");
    graph->AddAnnotation(feat1, "major feature for the next release");
    graph->AddAnnotation(hot1,  "hotfix in develop");

    GitGraphStyle style = graph->GetStyle();
    style.edgeStyle        = GitGraphEdgeStyle::Bezier;
    style.rowSpacing       = 62.0;
    style.laneSpacing      = 72.0;
    style.showCommitLabels = false;
    style.showSwimlaneLabels = true;
    style.showSwimlaneBands  = true;
    style.nodeRadius       = 8.0;
    style.mergeNodeRadius  = 7.0;
    graph->SetStyle(style);
}

// ---------------------------------------------------------------------------
// Tab 2 - trunk with a branch either side, plus per-commit file boxes
// ---------------------------------------------------------------------------

void BuildBranchOperationsHistory(UltraCanvasGitGraph* graph) {
    graph->SetLayoutMode(GitGraphLayoutMode::Swimlane);
    graph->SetOrientation(GitGraphOrientation::LeftToRight);
    graph->SetTrunkBranch("Master");
    graph->SetSwimlaneOrder({"Feature-2", "Feature-1"});   // Feature-2 above, Feature-1 below

    auto withFiles = [&](const std::string& sha, const std::vector<std::string>& files) {
        const GitGraphCommit* existing = graph->GetCommit(sha);
        if (!existing) return;
        GitGraphCommit copy = *existing;
        for (const std::string& path : files) copy.files.emplace_back(path, 'M');
        graph->AddCommit(copy);
    };

    graph->Branch("Master");
    const std::string m1 = graph->Commit("initial");
    withFiles(m1, {"File 1.0.1", "File 2.0.1", "File 3.0.1"});

    graph->Branch("Feature-1");
    const std::string f1a = graph->Commit("work on Feature-1");
    withFiles(f1a, {"File 1.0.2", "File 2.0.2"});
    const std::string f1b = graph->Commit("more Feature-1");
    withFiles(f1b, {"File 3.0.2"});

    graph->Checkout("Master");
    const std::string m2 = graph->Merge("Feature-1", "merge Feature-1");
    withFiles(m2, {"File 1.0.3", "File 2.0.3"});

    graph->Branch("Feature-2");
    const std::string f2a = graph->Commit("work on Feature-2");
    withFiles(f2a, {"File 1.0.4", "File 4.0.1"});

    graph->Checkout("Master");
    const std::string m3 = graph->Merge("Feature-2", "merge Feature-2");
    withFiles(m3, {"File 1.0.5"});

    GitGraphStyle style = graph->GetStyle();
    style.edgeStyle       = GitGraphEdgeStyle::Arc;
    style.rowSpacing      = 118.0;
    style.laneSpacing     = 150.0;
    style.nodeRadius      = 10.0;
    style.mergeNodeRadius = 9.0;
    style.showFileBoxes   = true;
    style.showCommitLabels = false;
    style.showTrunkBaseline = true;
    style.showTrunkArrow    = true;
    style.showSwimlaneBands = false;
    graph->SetStyle(style);
}

// ---------------------------------------------------------------------------
// Tab 3 - the repository-browser lane view
// ---------------------------------------------------------------------------

void BuildRepositoryHistory(UltraCanvasGitGraph* graph) {
    // Built as raw commits in git log order (newest first) - what
    // LoadFromGitLog() produces from a real repository.
    struct Row {
        const char* sha;
        const char* parents;
        const char* subject;
    };

    static const Row rows[] = {
        {"5c7570e", "c97feac",           "Merge branch 'release/0.1.1' into develop"},
        {"c97feac", "459d238 e9898bb",   "increment version number"},
        {"459d238", "adfe0a0",           "finish work on feature 3"},
        {"adfe0a0", "5fd6701",           "more work on feature 3"},
        {"5fd6701", "e5d230e",           "start work on feature 3"},
        {"e5d230e", "b88040c d7a33d6",   "Merge branch 'feature/feature-5'"},
        {"b88040c", "3ec6862",           "start work on feature 5"},
        {"d7a33d6", "567694f",           "Merge branch 'hotfix/fix-it'"},
        {"3ec6862", "567694f",           "fixing a critical bug"},
        {"567694f", "20106f7 70b9d9a",   "Merge branch 'hotfix/fix-it' into main"},
        {"20106f7", "637b096",           "start work on feature 4"},
        {"70b9d9a", "637b096",           "Merge branch 'release/0.1.1'"},
        {"637b096", "eae015b",           "increment version number"},
        {"eae015b", "f1696fd",           "more work on feature 3"},
        {"f1696fd", "2003897 42b39dd",   "Merge branch 'feature/feature-2'"},
        {"2003897", "fb9203d",           "start work on feature 3"},
        {"42b39dd", "fb9203d",           "more work on feature 2"},
        {"fb9203d", "78a0042",           "even more work on feature 1"},
        {"78a0042", "687e6a4",           "start work on feature 2"},
        {"687e6a4", "9a8b8f0",           "more work on feature 1"},
        {"9a8b8f0", "aedea34",           "start work on feature 1"},
        {"aedea34", "3fcf8b2",           "start branch develop"},
        {"3fcf8b2", "",                  "initial commit"}
    };

    int64_t timestamp = 1750000000;
    for (const Row& row : rows) {
        GitGraphCommit commit;
        commit.sha        = row.sha;
        commit.subject    = row.subject;
        commit.authorName = "UltraCanvas";
        commit.commitDate = timestamp;
        commit.authorDate = timestamp;
        timestamp -= 3600;

        std::string parents(row.parents);
        std::string current;
        for (char c : parents) {
            if (c == ' ') {
                if (!current.empty()) commit.parents.push_back(current);
                current.clear();
            } else {
                current.push_back(c);
            }
        }
        if (!current.empty()) commit.parents.push_back(current);

        graph->AddCommit(commit);
    }

    graph->AddBranchRef("develop", "5c7570e", true);
    graph->AddBranchRef("main", "d7a33d6");
    graph->AddRemoteRef("origin", "develop", "5c7570e");
    graph->AddTagRef("v0.1.1", "637b096", true);

    graph->SetOrderMode(GitGraphOrderMode::AsGiven);
    graph->SetRowsAreNewestFirst(true);
    graph->SetLayoutMode(GitGraphLayoutMode::Lanes);
    graph->SetOrientation(GitGraphOrientation::BottomToTop);
    graph->SetTrunkBranch("main");
    graph->SetTheme(GitGraphTheme::Dark);

    GitGraphStyle style = graph->GetStyle();
    style.edgeStyle        = GitGraphEdgeStyle::Orthogonal;
    style.rowSpacing       = 26.0;
    style.laneSpacing      = 22.0;
    style.showCommitLabels = true;
    style.showRowStripes   = false;
    graph->SetStyle(style);

    // Repository-browser extras: a row-aligned commit table beside the graph,
    // an overview minimap, and lane reordering to cut edge crossings.
    graph->SetShowTable(true);
    graph->SetGraphPaneWidth(150.0);
    graph->SetShowMinimap(true);
    graph->SetMinimapPosition(GitGraphMinimapPosition::TopRight);
    graph->SetReduceCrossings(true);
}

// Builds the shared toolbar: theme / edge style / lane strategy / orientation
// plus the zoom buttons, all wired to `graph`.
std::shared_ptr<UltraCanvasContainer> MakeToolbar(
        const std::string& idPrefix,
        const std::shared_ptr<UltraCanvasGitGraph>& graph,
        bool includeLaneControls) {

    auto toolbar = MakeBar(idPrefix + "Toolbar");
    toolbar->SetBackgroundColor(Color(248, 248, 252));
    toolbar->SetPadding(6);

    DemoKit::Add(toolbar, MakeLabel(idPrefix + "ThemeLabel", "Theme:"));
    auto themeDropdown = std::make_shared<UltraCanvasDropdown>(idPrefix + "Theme", 0, 0, 110, 28);
    themeDropdown->AddItem("Default");
    themeDropdown->AddItem("Dark");
    themeDropdown->AddItem("Light");
    themeDropdown->AddItem("Colorful");
    themeDropdown->AddItem("Mono");
    themeDropdown->SetSelectedIndex(0);
    DemoKit::Add(toolbar, themeDropdown);

    DemoKit::Add(toolbar, MakeLabel(idPrefix + "EdgeLabel", "Edges:"));
    auto edgeDropdown = std::make_shared<UltraCanvasDropdown>(idPrefix + "Edge", 0, 0, 120, 28);
    edgeDropdown->AddItem("Orthogonal");
    edgeDropdown->AddItem("Bezier");
    edgeDropdown->AddItem("Arc");
    edgeDropdown->AddItem("Straight");
    edgeDropdown->SetSelectedIndex(0);
    DemoKit::Add(toolbar, edgeDropdown);

    std::shared_ptr<UltraCanvasDropdown> laneDropdown;
    std::shared_ptr<UltraCanvasDropdown> orientationDropdown;
    if (includeLaneControls) {
        DemoKit::Add(toolbar, MakeLabel(idPrefix + "LaneLabel", "Lanes:"));
        laneDropdown = std::make_shared<UltraCanvasDropdown>(idPrefix + "Lane", 0, 0, 100, 28);
        laneDropdown->AddItem("Stable");
        laneDropdown->AddItem("Compact");
        laneDropdown->SetSelectedIndex(0);
        DemoKit::Add(toolbar, laneDropdown);

        DemoKit::Add(toolbar, MakeLabel(idPrefix + "OrientLabel", "Flow:"));
        orientationDropdown = std::make_shared<UltraCanvasDropdown>(idPrefix + "Orient",
                                                                   0, 0, 130, 28);
        orientationDropdown->AddItem("Newest top");
        orientationDropdown->AddItem("Oldest top");
        orientationDropdown->AddItem("Left to right");
        orientationDropdown->SetSelectedIndex(0);
        DemoKit::Add(toolbar, orientationDropdown);
    }

    toolbar->AddStretchSpacer();

    auto zoomInBtn = MakeButton(idPrefix + "ZoomIn", "+", 36);
    DemoKit::Add(toolbar, zoomInBtn);

    auto zoomOutBtn = MakeButton(idPrefix + "ZoomOut", "-", 36);
    DemoKit::Add(toolbar, zoomOutBtn);

    auto fitBtn = MakeButton(idPrefix + "Fit", "Fit", 50);
    DemoKit::Add(toolbar, fitBtn);

    auto svgBtn = MakeButton(idPrefix + "Svg", "Save SVG", 90);
    DemoKit::Add(toolbar, svgBtn);

    themeDropdown->onSelectionChanged = [graph](int index, const DropdownItem&) {
        switch (index) {
            case 0: graph->SetTheme(GitGraphTheme::Default);    break;
            case 1: graph->SetTheme(GitGraphTheme::Dark);       break;
            case 2: graph->SetTheme(GitGraphTheme::Light);      break;
            case 3: graph->SetTheme(GitGraphTheme::Colorful);   break;
            case 4: graph->SetTheme(GitGraphTheme::Monochrome); break;
            default: break;
        }
    };

    edgeDropdown->onSelectionChanged = [graph](int index, const DropdownItem&) {
        switch (index) {
            case 0: graph->SetEdgeStyle(GitGraphEdgeStyle::Orthogonal); break;
            case 1: graph->SetEdgeStyle(GitGraphEdgeStyle::Bezier);     break;
            case 2: graph->SetEdgeStyle(GitGraphEdgeStyle::Arc);        break;
            case 3: graph->SetEdgeStyle(GitGraphEdgeStyle::Straight);   break;
            default: break;
        }
    };

    if (laneDropdown) {
        laneDropdown->onSelectionChanged = [graph](int index, const DropdownItem&) {
            graph->SetLaneStrategy(index == 0 ? GitGraphLaneStrategy::Stable
                                              : GitGraphLaneStrategy::Compact);
        };
    }
    if (orientationDropdown) {
        orientationDropdown->onSelectionChanged = [graph](int index, const DropdownItem&) {
            switch (index) {
                case 0: graph->SetOrientation(GitGraphOrientation::BottomToTop); break;
                case 1: graph->SetOrientation(GitGraphOrientation::TopToBottom); break;
                case 2: graph->SetOrientation(GitGraphOrientation::LeftToRight); break;
                default: break;
            }
        };
    }

    zoomInBtn->onClick  = [graph]() { graph->ZoomIn();  };
    zoomOutBtn->onClick = [graph]() { graph->ZoomOut(); };
    fitBtn->onClick     = [graph]() { graph->ZoomToFit(); };
    svgBtn->onClick     = [graph]() { graph->SaveToSVG("gitgraph.svg"); };

    return toolbar;
}

} // namespace

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateGitGraphExamples() {
    auto page = DemoKit::CreatePage("GitGraphExamples", "Git Graph",
        "Commit-graph visualization: git-flow swimlanes, branch operations with "
        "changed-file boxes, and the repository lane view");
    // The graphs pan and zoom themselves: the tabs fill the area.
    page->SetFillsArea(true);

    auto card = DemoKit::CreateCard("GitGraphCard");
    DemoKit::Add(page, card, 1);

    auto tabs = std::make_shared<UltraCanvasTabbedContainer>("GitGraphTabs");
    tabs->SetTabPosition(TabPosition::Top);
    DemoKit::Add(card, tabs, 1);

    auto statusLabel = DemoKit::AddText(card, "GitGraphStatus",
                                        "Ready - click a commit to select it, drag to pan, "
                                        "wheel to zoom");
    statusLabel->SetFontSize(11);
    statusLabel->SetTextColor(Color(60, 60, 60));
    statusLabel->SetBackgroundColor(Color(240, 240, 245));
    statusLabel->SetPadding(5);

    auto wireStatus = [statusLabel](const std::shared_ptr<UltraCanvasGitGraph>& graph) {
        graph->onCommitClick = [graph = graph.get(), statusLabel](const std::string& sha) {
            const GitGraphCommit* commit = graph->GetCommit(sha);
            statusLabel->SetText(commit ? ("Selected " + commit->ShortSha() + " - " +
                                           commit->subject)
                                        : ("Selected " + sha));
        };
        graph->onCommitHover = [graph = graph.get(), statusLabel](const std::string& sha) {
            const GitGraphCommit* commit = graph->GetCommit(sha);
            if (commit) statusLabel->SetText(commit->ShortSha() + "  " + commit->subject);
        };
    };

    // ===== TAB 1: GIT FLOW =====
    {
        auto tab = MakeTabPage("GitFlowTab", "GitFlowDesc",
                               "Swimlane mode: one band per branch either side of the trunk, "
                               "with tags and callout annotations");

        auto graph = CreateGitGraph("GitFlowGraph", 0, 0, 0, 0);
        BuildGitFlowHistory(graph.get());
        wireStatus(graph);

        DemoKit::Add(tab, MakeToolbar("GitFlow", graph, false));
        AddGraph(tab, graph);
        tabs->AddTab("Git Flow", tab);
    }

    // ===== TAB 2: BRANCH OPERATIONS =====
    {
        auto tab = MakeTabPage("BranchOpsTab", "BranchOpsDesc",
                               "Branch and merge operations: a trunk with one branch above and "
                               "one below, each commit showing its changed files. Drag a commit "
                               "onto another to merge, or into empty space to branch.");

        auto graph = CreateGitGraph("BranchOpsGraph", 0, 0, 0, 0);
        BuildBranchOperationsHistory(graph.get());
        wireStatus(graph);

        // Drag a commit onto another to merge it in, or into empty space to
        // branch off it.
        graph->SetAuthoringEnabled(true);
        graph->onAuthorBranch = [statusLabel](const std::string&, const std::string& branch) {
            statusLabel->SetText("Created branch '" + branch + "'");
        };
        graph->onAuthorMerge = [statusLabel](const std::string& source, const std::string&) {
            statusLabel->SetText("Merged " + source.substr(0, 7));
        };

        DemoKit::Add(tab, MakeToolbar("BranchOps", graph, false));
        AddGraph(tab, graph);
        tabs->AddTab("Branch Operations", tab);
    }

    // ===== TAB 3: REPOSITORY BROWSER =====
    {
        auto tab = MakeTabPage("RepoTab", "RepoDesc",
                               "Lane mode: newest commit first with ref chips and a subject "
                               "column - the view git clients show");

        auto graph = CreateGitGraph("RepoGraph", 0, 0, 0, 0);
        BuildRepositoryHistory(graph.get());
        wireStatus(graph);

        DemoKit::Add(tab, MakeToolbar("Repo", graph, true));

        // Second toolbar row: search and filtering.
        auto filterBar = MakeBar("RepoFilterBar");

        DemoKit::Add(filterBar, MakeLabel("RepoSearchLabel", "Search:"));
        auto searchInput = std::make_shared<UltraCanvasTextInput>("RepoSearch", 0, 0, 200, 28);
        searchInput->SetPlaceholder("sha, subject, author or ref");
        DemoKit::Add(filterBar, searchInput);

        auto prevBtn = MakeButton("RepoPrev", "<", 36);
        DemoKit::Add(filterBar, prevBtn);

        auto nextBtn = MakeButton("RepoNext", ">", 36);
        DemoKit::Add(filterBar, nextBtn);

        auto matchLabel = MakeLabel("RepoMatches", "no search");
        DemoKit::Add(filterBar, matchLabel);

        auto mergesCheck = MakeCheck("RepoMerges", "Merges only");
        DemoKit::Add(filterBar, mergesCheck);

        auto tableCheck = MakeCheck("RepoTable", "Table");
        tableCheck->SetChecked(true);
        DemoKit::Add(filterBar, tableCheck);

        auto minimapCheck = MakeCheck("RepoMinimap", "Minimap");
        minimapCheck->SetChecked(true);
        DemoKit::Add(filterBar, minimapCheck);

        auto crossingsCheck = MakeCheck("RepoCrossings", "Reduce crossings");
        crossingsCheck->SetChecked(true);
        DemoKit::Add(filterBar, crossingsCheck);

        auto timeCheck = MakeCheck("RepoTimeAxis", "Time axis");
        DemoKit::Add(filterBar, timeCheck);

        auto collapseCheck = MakeCheck("RepoCollapse", "Collapse");
        DemoKit::Add(filterBar, collapseCheck);

        auto avatarCheck = MakeCheck("RepoAvatars", "Avatars");
        DemoKit::Add(filterBar, avatarCheck);

        auto diffCheck = MakeCheck("RepoDiff", "Diff pane");
        DemoKit::Add(filterBar, diffCheck);

        auto laneOrderCheck = MakeCheck("RepoLaneOrder", "develop first");
        DemoKit::Add(filterBar, laneOrderCheck);

        auto jsonBtn = MakeButton("RepoJson", "Save JSON", 90);
        DemoKit::Add(filterBar, jsonBtn);

        // The demo has no repository behind it, so it stands in for the
        // providers an application would wire to UltraCanvasGitRepository.
        // Stored on the graph, so it names the graph raw (a shared_ptr would
        // keep the graph alive for ever).
        graph->SetFileListProvider([graph = graph.get()](const std::string& sha) {
            const GitGraphCommit* commit = graph->GetCommit(sha);
            std::vector<GitGraphFileChange> files;
            if (!commit) return files;
            files.emplace_back("src/" + commit->ShortSha() + ".cpp", 'M', 12, 4);
            files.emplace_back("include/" + commit->ShortSha() + ".h", 'M', 3, 1);
            if (commit->IsMerge()) files.emplace_back("CHANGELOG.md", 'A', 8, 0);
            return files;
        });
        graph->SetDiffProvider([](const std::string& sha, const std::string& path) {
            return "@@ -18,7 +18,9 @@ " + path + "\n"
                   " void Existing() {\n"
                   "-    int removed = 1;\n"
                   "+    int added = 2;\n"
                   "+    int alsoAdded = 3;\n"
                   " }\n"
                   "  (commit " + sha + ")\n";
        });

        diffCheck->onChecked   = [graph]() { graph->SetShowDiffPane(true);  };
        diffCheck->onUnchecked = [graph]() { graph->SetShowDiffPane(false); };

        auto applyLaneOrder = [graph](bool developFirst) {
            graph->SetLanePriority(developFirst ? std::vector<std::string>{"develop"}
                                                : std::vector<std::string>{});
        };
        laneOrderCheck->onChecked   = [applyLaneOrder]() { applyLaneOrder(true);  };
        laneOrderCheck->onUnchecked = [applyLaneOrder]() { applyLaneOrder(false); };

        timeCheck->onChecked = [graph]() {
            graph->SetAxisMode(GitGraphAxisMode::TimeProportional);
            graph->SetShowDateRuler(true);
        };
        timeCheck->onUnchecked = [graph]() {
            graph->SetAxisMode(GitGraphAxisMode::Rows);
            graph->SetShowDateRuler(false);
        };
        collapseCheck->onChecked   = [graph]() { graph->SetCollapseLinearRuns(true, 4); };
        collapseCheck->onUnchecked = [graph]() { graph->SetCollapseLinearRuns(false); };

        auto setAvatars = [graph](bool on) {
            GitGraphStyle style = graph->GetStyle();
            style.showAvatars = on;
            graph->SetStyle(style);
        };
        avatarCheck->onChecked   = [setAvatars]() { setAvatars(true);  };
        avatarCheck->onUnchecked = [setAvatars]() { setAvatars(false); };
        jsonBtn->onClick = [graph]() { graph->SaveToJSON("gitgraph.json"); };

        graph->onSearchChanged = [matchLabel](size_t current, size_t total) {
            matchLabel->SetText(total == 0
                                    ? "no matches"
                                    : (std::to_string(current) + " / " + std::to_string(total)));
        };
        searchInput->onTextChanged = [graph](const std::string& text) {
            if (text.empty()) graph->ClearSearch();
            else              graph->Search(text);
        };
        prevBtn->onClick = [graph]() { graph->FindPrevious(); };
        nextBtn->onClick = [graph]() { graph->FindNext(); };

        auto applyMergeFilter = [graph](bool mergesOnly) {
            GitGraphFilter filter = graph->GetFilter();
            filter.mergesOnly = mergesOnly;
            graph->SetFilter(filter);
        };
        mergesCheck->onChecked   = [applyMergeFilter]() { applyMergeFilter(true);  };
        mergesCheck->onUnchecked = [applyMergeFilter]() { applyMergeFilter(false); };

        tableCheck->onChecked   = [graph]() { graph->SetShowTable(true);  };
        tableCheck->onUnchecked = [graph]() { graph->SetShowTable(false); };
        minimapCheck->onChecked   = [graph]() { graph->SetShowMinimap(true);  };
        minimapCheck->onUnchecked = [graph]() { graph->SetShowMinimap(false); };
        crossingsCheck->onChecked   = [graph]() { graph->SetReduceCrossings(true);  };
        crossingsCheck->onUnchecked = [graph]() { graph->SetReduceCrossings(false); };

        DemoKit::Add(tab, filterBar);
        AddGraph(tab, graph);
        tabs->AddTab("Repository", tab);
    }

    return page;
}

} // namespace UltraCanvas
