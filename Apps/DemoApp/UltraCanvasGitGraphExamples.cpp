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
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
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

constexpr int kPageWidth  = 1030;
constexpr int kPageHeight = 800;
constexpr int kGraphWidth  = kPageWidth - 60;
constexpr int kGraphHeight = 520;

std::shared_ptr<UltraCanvasLabel> MakeLabel(const std::string& id, int x, int y,
                                            int w, int h, const std::string& text,
                                            float fontSize = 11.0f) {
    auto label = std::make_shared<UltraCanvasLabel>(id, x, y, w, h);
    label->SetText(text);
    label->SetFontSize(fontSize);
    return label;
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

    auto toolbar = std::make_shared<UltraCanvasContainer>(idPrefix + "Toolbar",
                                                          10, 40, kGraphWidth, 40);
    toolbar->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    toolbar->SetBackgroundColor(Color(248, 248, 252));

    toolbar->AddChild(MakeLabel(idPrefix + "ThemeLabel", 0, 0, 46, 20, "Theme:"));
    auto themeDropdown = std::make_shared<UltraCanvasDropdown>(idPrefix + "Theme", 0, 0, 110, 28);
    themeDropdown->AddItem("Default");
    themeDropdown->AddItem("Dark");
    themeDropdown->AddItem("Light");
    themeDropdown->AddItem("Colorful");
    themeDropdown->AddItem("Mono");
    themeDropdown->SetSelectedIndex(0);
    toolbar->AddChild(themeDropdown);

    toolbar->AddChild(MakeLabel(idPrefix + "EdgeLabel", 0, 0, 40, 20, "Edges:"));
    auto edgeDropdown = std::make_shared<UltraCanvasDropdown>(idPrefix + "Edge", 0, 0, 120, 28);
    edgeDropdown->AddItem("Orthogonal");
    edgeDropdown->AddItem("Bezier");
    edgeDropdown->AddItem("Arc");
    edgeDropdown->AddItem("Straight");
    edgeDropdown->SetSelectedIndex(0);
    toolbar->AddChild(edgeDropdown);

    std::shared_ptr<UltraCanvasDropdown> laneDropdown;
    std::shared_ptr<UltraCanvasDropdown> orientationDropdown;
    if (includeLaneControls) {
        toolbar->AddChild(MakeLabel(idPrefix + "LaneLabel", 0, 0, 48, 20, "Lanes:"));
        laneDropdown = std::make_shared<UltraCanvasDropdown>(idPrefix + "Lane", 0, 0, 100, 28);
        laneDropdown->AddItem("Stable");
        laneDropdown->AddItem("Compact");
        laneDropdown->SetSelectedIndex(0);
        toolbar->AddChild(laneDropdown);

        toolbar->AddChild(MakeLabel(idPrefix + "OrientLabel", 0, 0, 40, 20, "Flow:"));
        orientationDropdown = std::make_shared<UltraCanvasDropdown>(idPrefix + "Orient",
                                                                   0, 0, 130, 28);
        orientationDropdown->AddItem("Newest top");
        orientationDropdown->AddItem("Oldest top");
        orientationDropdown->AddItem("Left to right");
        orientationDropdown->SetSelectedIndex(0);
        toolbar->AddChild(orientationDropdown);
    }

    toolbar->AddStretchSpacer();

    auto zoomInBtn = std::make_shared<UltraCanvasButton>(idPrefix + "ZoomIn", 0, 0, 36, 28);
    zoomInBtn->SetText("+");
    toolbar->AddChild(zoomInBtn);

    auto zoomOutBtn = std::make_shared<UltraCanvasButton>(idPrefix + "ZoomOut", 0, 0, 36, 28);
    zoomOutBtn->SetText("-");
    toolbar->AddChild(zoomOutBtn);

    auto fitBtn = std::make_shared<UltraCanvasButton>(idPrefix + "Fit", 0, 0, 50, 28);
    fitBtn->SetText("Fit");
    toolbar->AddChild(fitBtn);

    auto svgBtn = std::make_shared<UltraCanvasButton>(idPrefix + "Svg", 0, 0, 90, 28);
    svgBtn->SetText("Save SVG");
    toolbar->AddChild(svgBtn);

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
    auto mainContainer = std::make_shared<UltraCanvasContainer>(
        "GitGraphExamples", 0, 0, kPageWidth, kPageHeight);

    auto title = MakeLabel("GitGraphTitle", 20, 10, 600, 35, "Git Graph", 18.0f);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(Color(50, 50, 150));
    mainContainer->AddChild(title);

    mainContainer->AddChild(MakeLabel(
        "GitGraphSubtitle", 20, 45, kPageWidth - 70, 25,
        "Commit-graph visualization: git-flow swimlanes, branch operations with "
        "changed-file boxes, and the repository lane view", 12.0f));

    auto statusLabel = MakeLabel("GitGraphStatus", 20, kPageHeight - 40, kPageWidth - 40, 25,
                                 "Ready - click a commit to select it, drag to pan, "
                                 "wheel to zoom");
    statusLabel->SetTextColor(Color(60, 60, 60));
    statusLabel->SetBackgroundColor(Color(240, 240, 245));
    statusLabel->SetPadding(5);
    mainContainer->AddChild(statusLabel);

    auto tabs = std::make_shared<UltraCanvasTabbedContainer>(
        "GitGraphTabs", 10, 78, kPageWidth - 20, kPageHeight - 130);
    tabs->SetTabPosition(TabPosition::Top);
    mainContainer->AddChild(tabs);

    auto wireStatus = [statusLabel](const std::shared_ptr<UltraCanvasGitGraph>& graph) {
        graph->onCommitClick = [graph, statusLabel](const std::string& sha) {
            const GitGraphCommit* commit = graph->GetCommit(sha);
            statusLabel->SetText(commit ? ("Selected " + commit->ShortSha() + " - " +
                                           commit->subject)
                                        : ("Selected " + sha));
        };
        graph->onCommitHover = [graph, statusLabel](const std::string& sha) {
            const GitGraphCommit* commit = graph->GetCommit(sha);
            if (commit) statusLabel->SetText(commit->ShortSha() + "  " + commit->subject);
        };
    };

    // ===== TAB 1: GIT FLOW =====
    {
        auto tab = std::make_shared<UltraCanvasContainer>("GitFlowTab", 0, 0,
                                                          kPageWidth - 30, kGraphHeight + 110);
        tab->AddChild(MakeLabel("GitFlowDesc", 10, 8, kPageWidth - 60, 22,
                                "Swimlane mode: one band per branch either side of the trunk, "
                                "with tags and callout annotations"));

        auto graph = CreateGitGraph("GitFlowGraph", 10, 88, kGraphWidth, kGraphHeight);
        BuildGitFlowHistory(graph.get());
        wireStatus(graph);

        tab->AddChild(MakeToolbar("GitFlow", graph, false));
        tab->AddChild(graph);
        tabs->AddTab("Git Flow", tab);
    }

    // ===== TAB 2: BRANCH OPERATIONS =====
    {
        auto tab = std::make_shared<UltraCanvasContainer>("BranchOpsTab", 0, 0,
                                                          kPageWidth - 30, kGraphHeight + 110);
        tab->AddChild(MakeLabel("BranchOpsDesc", 10, 8, kPageWidth - 60, 22,
                                "Branch and merge operations: a trunk with one branch above and "
                                "one below, each commit showing its changed files"));

        auto graph = CreateGitGraph("BranchOpsGraph", 10, 88, kGraphWidth, kGraphHeight);
        BuildBranchOperationsHistory(graph.get());
        wireStatus(graph);

        tab->AddChild(MakeToolbar("BranchOps", graph, false));
        tab->AddChild(graph);
        tabs->AddTab("Branch Operations", tab);
    }

    // ===== TAB 3: REPOSITORY BROWSER =====
    {
        auto tab = std::make_shared<UltraCanvasContainer>("RepoTab", 0, 0,
                                                          kPageWidth - 30, kGraphHeight + 110);
        tab->AddChild(MakeLabel("RepoDesc", 10, 8, kPageWidth - 60, 22,
                                "Lane mode: newest commit first with ref chips and a subject "
                                "column - the view git clients show"));

        auto graph = CreateGitGraph("RepoGraph", 10, 128, kGraphWidth, kGraphHeight - 40);
        BuildRepositoryHistory(graph.get());
        wireStatus(graph);

        tab->AddChild(MakeToolbar("Repo", graph, true));

        // Second toolbar row: search and filtering.
        auto filterBar = std::make_shared<UltraCanvasContainer>("RepoFilterBar", 10, 84,
                                                                kGraphWidth, 38);
        filterBar->layout.SetFlexRow().SetFlexGap(8)
                 .SetFlexAlignItems(CSSLayout::AlignItems::Center);

        filterBar->AddChild(MakeLabel("RepoSearchLabel", 0, 0, 50, 20, "Search:"));
        auto searchInput = std::make_shared<UltraCanvasTextInput>("RepoSearch", 0, 0, 200, 28);
        searchInput->SetPlaceholder("sha, subject, author or ref");
        filterBar->AddChild(searchInput);

        auto prevBtn = std::make_shared<UltraCanvasButton>("RepoPrev", 0, 0, 36, 28);
        prevBtn->SetText("<");
        filterBar->AddChild(prevBtn);

        auto nextBtn = std::make_shared<UltraCanvasButton>("RepoNext", 0, 0, 36, 28);
        nextBtn->SetText(">");
        filterBar->AddChild(nextBtn);

        auto matchLabel = MakeLabel("RepoMatches", 0, 0, 110, 20, "no search");
        filterBar->AddChild(matchLabel);

        auto mergesCheck = std::make_shared<UltraCanvasCheckbox>("RepoMerges", 0, 0, 130, 22);
        mergesCheck->SetText("Merges only");
        filterBar->AddChild(mergesCheck);

        auto tableCheck = std::make_shared<UltraCanvasCheckbox>("RepoTable", 0, 0, 110, 22);
        tableCheck->SetText("Table");
        tableCheck->SetChecked(true);
        filterBar->AddChild(tableCheck);

        auto minimapCheck = std::make_shared<UltraCanvasCheckbox>("RepoMinimap", 0, 0, 120, 22);
        minimapCheck->SetText("Minimap");
        minimapCheck->SetChecked(true);
        filterBar->AddChild(minimapCheck);

        auto crossingsCheck = std::make_shared<UltraCanvasCheckbox>("RepoCrossings", 0, 0, 150, 22);
        crossingsCheck->SetText("Reduce crossings");
        crossingsCheck->SetChecked(true);
        filterBar->AddChild(crossingsCheck);

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

        tab->AddChild(filterBar);
        tab->AddChild(graph);
        tabs->AddTab("Repository", tab);
    }

    return mainContainer;
}

} // namespace UltraCanvas
