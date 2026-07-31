# UltraCanvasGitGraph Documentation

## Overview

**UltraCanvasGitGraph** renders a Git commit history — a directed acyclic graph
of commits decorated with branch, tag and `HEAD` refs. Two layout families share
one data model:

- **Lanes** — one lane per concurrently open line of development, newest commit
  first. This is the repository-browser view that gitk, GitKraken, SourceTree
  and `git log --graph` all show.
- **Swimlane** — one band per branch, distributed either side of a nominated
  trunk, with time running along the axis. This is the git-flow teaching
  diagram, including per-commit changed-file boxes and callout annotations.

The layout is computed by a headless core (`UltraCanvasGitGraphLayout`) that
produces `(row, lane)` geometry with no rendering dependencies, so it is unit
tested on its own in `Tests/GitGraphLayoutTest.cpp`.

**Namespace:** `UltraCanvas`
**Headers:** `include/Plugins/Diagrams/UltraCanvasGitGraph.h`,
`UltraCanvasGitGraphLayout.h`, `UltraCanvasGitGraphTypes.h`
**Base Class:** `UltraCanvasUIElement`
**Version:** 1.0.0

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasGitGraph
```

## Header Include

```cpp
#include "Plugins/Diagrams/UltraCanvasGitGraph.h"
```

## Features

- **Two layout modes**: `Lanes` (repository browser) and `Swimlane` (git-flow)
- **Two lane strategies**: `Stable` (straight branches, GitKraken-like) and
  `Compact` (lanes recycled immediately, gitk-like)
- **Four orientations**: newest-at-top, oldest-at-top, left-to-right, right-to-left
- **Four commit orderings**: as given, commit date, author date, topological
- **Four edge styles**: orthogonal with rounded corners, Bezier, arc, straight
- **Full DAG coverage**: merges, octopus merges (≥3 parents), multiple roots,
  boundary commits with parents outside the loaded window, cherry-picks
- **Refs**: local and remote branches, lightweight and annotated tags, `HEAD`,
  with overflow collapse (`+n`) when several land on one commit
- **Per-commit file boxes** with leader lines, and free-standing callout
  annotations
- **Trunk pinning** — a nominated branch always keeps lane 0
- **Virtualised rendering** — only rows inside the viewport are drawn
- **Themes**: Default, Dark, Light, Colorful, Monochrome, Custom
- **Ingest**: programmatic, `git log` text, or the authoring API
- **SVG export** via `SaveToSVG`, **Mermaid** `gitGraph` import and export
- **Lazy loading** through `IGitGraphDataSource` - pages arrive as you scroll
- **Native `.git` reader** (`UltraCanvasGitRepository`): refs, loose objects and
  packfiles with delta chains, no git executable and no new dependency
- **Filtering** by text, author, path, branch, date range and merge status, with
  edges re-pointed across the commits that were filtered out
- **Crossing reduction** - lane columns reordered to cut edge crossings
- **Search** over sha, subject, author and refs, with next/previous navigation
- **Commit table pane** row-aligned beside the graph, with optional author
  avatars, and a **minimap**
- **Time-proportional axis** with a date ruler and gap compression
- **Collapsing** long linear runs into one expandable placeholder
- **Parallel rows** for commits made at the same moment
- **Signature and build-status badges** beside a commit
- **JSON export** of the laid-out geometry
- Zoom, pan, selection, hover, tooltips, keyboard navigation

## Data Structures

### Enumerations

```cpp
enum class GitGraphOrientation  { LeftToRight, TopToBottom, BottomToTop, RightToLeft };
enum class GitGraphLayoutMode   { Lanes, Swimlane };
enum class GitGraphLaneStrategy { Compact, Stable };
enum class GitGraphOrderMode    { AsGiven, CommitDate, AuthorDate, Topological };
enum class GitGraphEdgeStyle    { Straight, Orthogonal, Bezier, Arc };
enum class GitGraphNodeShape    { Circle, Ring, Square, Diamond };
enum class GitGraphCommitType   { Normal, Reverse, Highlight };
enum class GitGraphRefType      { LocalBranch, RemoteBranch, Tag, Head, Stash };
enum class GitGraphEdgeKind     { Parent, Merge, CherryPick, Skipped };
enum class GitGraphTheme        { Default, Dark, Light, Colorful, Monochrome, Custom };
```

`LeftToRight` and `TopToBottom` put the **oldest** commit at the start of the
axis (the teaching-diagram convention); `BottomToTop` and `RightToLeft` put the
**newest** there, which is what every repository browser does.

### GitGraphCommit

```cpp
struct GitGraphCommit {
    std::string sha;
    std::vector<std::string> parents;      // [0] is the first parent

    std::string subject, body;
    std::string authorName, authorEmail, committerName;
    int64_t     authorDate = 0, commitDate = 0;    // Unix seconds

    std::string branch;                    // Optional; Swimlane mode uses it
    std::vector<GitGraphFileChange> files;

    GitGraphCommitType type = GitGraphCommitType::Normal;
    bool  hasColorOverride  = false;
    Color colorOverride;
    std::string cherryPickSource;          // Drawn as a dashed non-parent edge

    bool IsMerge() const;                  // parents.size() > 1
    bool IsRoot()  const;                  // parents.empty()
    std::string ShortSha(size_t length = 7) const;
};
```

### GitGraphRef

```cpp
struct GitGraphRef {
    std::string     name, sha, remote;
    GitGraphRefType type      = GitGraphRefType::LocalBranch;
    bool            isCurrent = false;     // HEAD points here
    bool            annotated = false;     // Annotated (not lightweight) tag
    std::string     DisplayName() const;   // "origin/main" for remote branches
};
```

### GitGraphFileChange / GitGraphAnnotation

```cpp
struct GitGraphFileChange {
    std::string path;
    int  added = 0, removed = 0;
    char status = 'M';                     // A, M, D, R
};

struct GitGraphAnnotation {
    std::string sha, text;
    bool   hasColor = false;
    Color  color;
    double offsetAlongAxis = 0.0, offsetAcrossAxis = -46.0;
};
```

### GitGraphStyle (selected fields)

```cpp
struct GitGraphStyle {
    Color  backgroundColor = Color(255, 255, 255);
    double laneSpacing = 26.0, rowSpacing = 30.0;
    double nodeRadius = 6.0, mergeNodeRadius = 4.5, rootNodeRadius = 7.0;
    double edgeWidth = 2.0, cornerRadius = 12.0;

    GitGraphNodeShape nodeShape = GitGraphNodeShape::Circle;
    GitGraphNodeShape mergeNodeShape = GitGraphNodeShape::Ring;
    GitGraphEdgeStyle edgeStyle = GitGraphEdgeStyle::Orthogonal;
    bool   drawNodeHalo = true;            // Keeps crossing edges off the node
    bool   showEdgeArrows = false;

    bool   showTrunkBaseline = true, showTrunkArrow = true;
    bool   showSwimlaneLabels = true, showSwimlaneBands = true;

    bool   showCommitLabels = true, rotateCommitLabels = false;
    bool   showBranchChips = true, showTagChips = true;
    size_t maxChipsPerCommit = 3;          // Beyond this a "+n" chip is drawn

    bool   showFileBoxes = false;
    size_t maxFileBoxes  = 6;

    std::vector<Color> lanePalette;        // Cycled by lane index
};
```

## Class Reference

### Constructor and factory

```cpp
UltraCanvasGitGraph(const std::string& id, float x, float y, float w, float h);

std::shared_ptr<UltraCanvasGitGraph> CreateGitGraph(
        const std::string& id, float x, float y, float w, float h);
```

### Data

```cpp
void AddCommit(const GitGraphCommit& commit);
void AddCommits(const std::vector<GitGraphCommit>& newCommits);
void AppendCommits(const std::vector<GitGraphCommit>& newCommits);
void AddRef(const GitGraphRef& ref);
void AddBranchRef(const std::string& name, const std::string& sha, bool isCurrent = false);
void AddTagRef(const std::string& name, const std::string& sha, bool annotated = false);
void AddRemoteRef(const std::string& remote, const std::string& name, const std::string& sha);
void SetHead(const std::string& refNameOrSha);
void AddAnnotation(const std::string& sha, const std::string& text);
void Clear();

size_t LoadFromGitLog(const std::string& text);
static std::string GitLogFormat();

const GitGraphCommit* GetCommit(const std::string& sha) const;
std::vector<GitGraphRef> GetRefsForCommit(const std::string& sha) const;
size_t GetCommitCount() const;
```

### Authoring (teaching diagrams)

```cpp
std::string Commit(const std::string& subject, const std::string& tag = "");
void        Branch(const std::string& name);       // Create + check out
void        Checkout(const std::string& name);
std::string Merge(const std::string& fromBranch, const std::string& subject = "");
std::string CherryPick(const std::string& sha, const std::string& subject = "");
void        Tag(const std::string& name, const std::string& sha = "");
```

These append commits oldest-first and record the branch on each one, so
`Swimlane` mode bands them without reconstructing branch membership.

### Layout

```cpp
void SetOrientation(GitGraphOrientation orientation);
void SetLayoutMode(GitGraphLayoutMode mode);
void SetLaneStrategy(GitGraphLaneStrategy strategy);
void SetOrderMode(GitGraphOrderMode mode);
void SetTrunkBranch(const std::string& name);
void SetSwimlaneOrder(const std::vector<std::string>& branchNames);
void SetSwimlanesBothSides(bool bothSides);
void SetRowsAreNewestFirst(bool newestFirst);
void PerformLayout();

const GitGraphLayoutResult& GetLayoutResult() const;
int  GetRowCount() const;
```

### Style, selection, view and export

```cpp
void SetTheme(GitGraphTheme theme);
GitGraphStyle& GetStyle();
void SetStyle(const GitGraphStyle& newStyle);
void SetEdgeStyle(GitGraphEdgeStyle edgeStyle);
void SetShowCommitLabels(bool show);
void SetShowFileBoxes(bool show);
void SetShowBranchChips(bool show);
void SetLaneColor(int lane, const Color& color);
Color GetLaneColor(int lane) const;
Color GetBranchColor(const std::string& branchName) const;

void SelectCommit(const std::string& sha, bool addToSelection = false);
void DeselectAll();
std::vector<std::string> GetSelectedCommits() const;

void ZoomIn();  void ZoomOut();  void ZoomToFit(double padding = 24.0);
void ResetView();  void SetZoom(float zoom);  void SetPan(double x, double y);
void CenterOnCommit(const std::string& sha);
void ScrollToRef(const std::string& refName);

bool SaveToSVG(const std::string& filePath);
```

### Callbacks

```cpp
std::function<void(const std::string&)> onCommitClick;
std::function<void(const std::string&)> onCommitDoubleClick;
std::function<void(const std::string&)> onCommitRightClick;
std::function<void(const std::string&)> onCommitHover;
std::function<void(const std::string&)> onCommitSelect;
std::function<void(const GitGraphRef&)> onRefClick;
std::function<void()>                   onLayoutComplete;

std::function<std::string(const GitGraphCommit&)> commitLabelFormatter;
```

The element is **read-only**: it never executes git. `onCommitRightClick` is
where an application hangs its checkout / branch / revert menu.

## Usage Examples

All examples are drawn from `Apps/DemoApp/UltraCanvasGitGraphExamples.cpp`.

### A git-flow swimlane diagram

```cpp
auto graph = CreateGitGraph("GitFlow", 10, 88, 970, 520);

graph->SetLayoutMode(GitGraphLayoutMode::Swimlane);
graph->SetOrientation(GitGraphOrientation::LeftToRight);
graph->SetTrunkBranch("main");
graph->SetSwimlaneOrder({"hotfixes", "release branches", "develop", "feature branches"});

graph->Branch("main");
graph->Commit("initial commit", "0.1");

graph->Branch("develop");
graph->Commit("start develop");

graph->Branch("feature branches");
const std::string feature = graph->Commit("major feature for the next release");

graph->Checkout("develop");
graph->Merge("feature branches", "merge feature");

graph->Checkout("main");
graph->Merge("develop", "release 1.0");
graph->Tag("1.0");

graph->AddAnnotation(feature, "major feature for the next release");
```

### The repository lane view

```cpp
auto graph = CreateGitGraph("Repo", 10, 88, 970, 520);

// Commits in git log order (newest first)
GitGraphCommit commit;
commit.sha        = "5c7570e";
commit.parents    = {"c97feac"};
commit.subject    = "Merge branch 'release/0.1.1' into develop";
commit.authorName = "Alex";
commit.commitDate = 1750000000;
graph->AddCommit(commit);
// ...more commits

graph->AddBranchRef("develop", "5c7570e", /*isCurrent=*/true);
graph->AddRemoteRef("origin", "develop", "5c7570e");
graph->AddTagRef("v0.1.1", "637b096", /*annotated=*/true);

graph->SetLayoutMode(GitGraphLayoutMode::Lanes);
graph->SetOrientation(GitGraphOrientation::BottomToTop);   // Newest at the top
graph->SetLaneStrategy(GitGraphLaneStrategy::Stable);      // Straight branches
graph->SetTrunkBranch("main");
graph->SetTheme(GitGraphTheme::Dark);
```

### Loading a real repository

```cpp
// Run this yourself and hand the output to the element - the widget never
// spawns a process:
//   git log --all --pretty=format:<UltraCanvasGitGraph::GitLogFormat()>
const std::string log = LoadFile("history.txt");
graph->LoadFromGitLog(log);
graph->SetOrderMode(GitGraphOrderMode::Topological);
graph->PerformLayout();
```

`GitLogFormat()` returns `%H<US>%P<US>%an<US>%ae<US>%at<US>%ct<US>%D<US>%s`
(fields separated by the ASCII unit separator, `0x1F`). The parser also accepts
`|` as the separator, which is friendlier for hand-written fixtures. `%D`
decorations are turned into branch, remote-branch, tag and `HEAD` refs
automatically.

### Per-commit changed-file boxes

```cpp
GitGraphCommit commit = *graph->GetCommit(sha);
commit.files.emplace_back("File 1.0.1", 'M');
commit.files.emplace_back("File 2.0.1", 'A');
graph->AddCommit(commit);          // Re-adding by sha replaces in place

GitGraphStyle style = graph->GetStyle();
style.showFileBoxes = true;
style.maxFileBoxes  = 6;
graph->SetStyle(style);
```

### Wiring callbacks to a status label

```cpp
graph->onCommitClick = [graph, statusLabel](const std::string& sha) {
    const GitGraphCommit* commit = graph->GetCommit(sha);
    if (commit) statusLabel->SetText(commit->ShortSha() + " - " + commit->subject);
};

graph->commitLabelFormatter = [](const GitGraphCommit& commit) {
    return commit.ShortSha(8) + "  " + commit.subject;
};
```

### Toolbar bindings

```cpp
edgeDropdown->onSelectionChanged = [graph](int index, const DropdownItem&) {
    switch (index) {
        case 0: graph->SetEdgeStyle(GitGraphEdgeStyle::Orthogonal); break;
        case 1: graph->SetEdgeStyle(GitGraphEdgeStyle::Bezier);     break;
        case 2: graph->SetEdgeStyle(GitGraphEdgeStyle::Arc);        break;
        case 3: graph->SetEdgeStyle(GitGraphEdgeStyle::Straight);   break;
    }
};

laneDropdown->onSelectionChanged = [graph](int index, const DropdownItem&) {
    graph->SetLaneStrategy(index == 0 ? GitGraphLaneStrategy::Stable
                                      : GitGraphLaneStrategy::Compact);
};

zoomFitBtn->onClick = [graph]() { graph->ZoomToFit(); };
```

### Lazy loading a large history

```cpp
class MyHistorySource : public IGitGraphDataSource {
public:
    size_t GetTotalCommitCount() override { return total; }

    std::vector<GitGraphCommit> FetchCommits(size_t offset, size_t count) override {
        return backend.Read(offset, count);      // Newest first
    }

    std::vector<GitGraphRef> FetchRefs() override { return backend.ReadRefs(); }
};

graph->SetDataSource(std::make_shared<MyHistorySource>(), /*chunkSize=*/500);
graph->SetPrefetchRows(40);        // Fetch when this close to the loaded end
graph->onChunkLoaded = [](size_t loaded, size_t total) {
    // total is 0 when the source cannot know it up front
};
```

The first chunk is fetched when the source is attached; the next one is pulled
during rendering as soon as the viewport comes within `SetPrefetchRows()` of the
end of the loaded history, and the layout is refreshed in the same frame.

### Reading a real repository

```cpp
#include "UltraCanvasGitRepository.h"

auto repository = std::make_shared<UltraCanvasGitRepository>();
if (repository->Open("/path/to/worktree")) {          // or a .git dir, or any
    graph->AddCommits(repository->ReadCommits(500));  // subdirectory of it
    for (const GitGraphRef& ref : repository->ReadRefs()) graph->AddRef(ref);
    graph->SetOrderMode(GitGraphOrderMode::Topological);
}

// ...or page it in lazily instead:
graph->SetDataSource(std::make_shared<UltraCanvasGitRepositorySource>(repository));
```

`UltraCanvasGitRepository` reads refs (loose, `packed-refs`, annotated tags
peeled to their commit), loose objects, and packfiles including `OFS_DELTA` and
`REF_DELTA` chains, inflating with the vendored miniz. It never spawns a
process. `Open()` accepts a working tree, a `.git` directory, a `.git` file
pointing elsewhere (worktrees and submodules), or any directory inside a tree.

### Mermaid import and export

```cpp
graph->LoadFromMermaid(R"(
gitGraph LR:
    commit id: "base"
    branch develop
    commit
    checkout main
    merge develop tag: "v1.0"
)");

const std::string text = graph->ToMermaidText();
graph->SaveToMermaid("history.mmd");
```

Import covers `commit`, `branch`, `checkout`/`switch`, `merge` and
`cherry-pick`, the `id`, `tag`, `type`, `msg`, `order` and `parent` attributes,
`%%` comments, the `%%{init: ...}%%` header (`mainBranchName`, `showBranches`,
`showCommitLabel`, `rotateCommitLabel`) and the `LR` / `TB` / `BT` direction
suffixes. On a bad line `LoadFromMermaid` returns 0 and `GetLastError()` reports
the message with a line number.

### Filtering

```cpp
GitGraphFilter filter;
filter.author = "alex";
filter.since  = 1750000000;
filter.path   = "src/";
filter.hideMerges = true;
graph->SetFilter(filter);

// Commits removed by the filter: their edges are re-pointed at the nearest
// surviving ancestor and drawn dashed (GitGraphEdgeKind::Skipped).
const size_t hidden = graph->GetFilteredOutCount();
graph->ClearFilter();
```

### Crossing reduction

```cpp
graph->SetReduceCrossings(true);
const size_t crossings = graph->CountEdgeCrossings();
```

Lane columns are reordered with a barycentre heuristic, keeping whichever
permutation actually measures fewest crossings, with lane 0 (the trunk) pinned.
Measured over 300 randomised layouts it removes about a third of all crossings
and never makes a layout worse. The pass is skipped above
`crossingReductionEdgeBudget` edges (default 4000) because it counts crossings
pairwise, and it only applies to `Lanes` mode - a swimlane band's position is
semantic.

### Search

```cpp
const size_t matches = graph->Search("hotfix");
graph->FindNext();
graph->FindPrevious();
graph->onSearchChanged = [](size_t current, size_t total) { /* "3 / 12" */ };
graph->ClearSearch();
```

Matches are highlighted with a ring; the current match gets a stronger one, and
navigating centres the view on it.

### Commit table pane

```cpp
graph->SetShowTable(true);              // Vertical orientations only
graph->SetGraphPaneWidth(150.0);        // Width reserved for the graph column
graph->SetTableColumns({
    {GitGraphTableColumn::Subject, "Subject", 320.0},
    {GitGraphTableColumn::Author,  "Author",  140.0},
    {GitGraphTableColumn::Date,    "Date",    130.0},
    {GitGraphTableColumn::Sha,     "Commit",   80.0}
});
graph->SetDateFormatter([](int64_t when) { return MyRelativeTime(when); });
```

Rows are aligned by construction, so selection and scrolling are shared with the
graph - no synchronisation to wire up. To pair an external
`UltraCanvasTableView` instead, use the row-alignment API:

```cpp
const int    rows    = graph->GetRowCount();
const double top     = graph->GetRowScreenPosition(row);   // Element space
const double spacing = graph->GetRowSpacing();             // Zoom applied
const auto   visible = graph->GetVisibleRowRange();        // {first, last}
const int    row     = graph->GetRowAtScreenPosition(y);
```

### Minimap

```cpp
graph->SetShowMinimap(true);
graph->SetMinimapPosition(GitGraphMinimapPosition::TopRight);
```

Every commit is drawn as one tick in its lane colour, with a viewport rectangle
over the rows currently on screen. Clicking or dragging inside the minimap jumps
the view to that point in the history.

### Time-proportional axis

```cpp
graph->SetAxisMode(GitGraphAxisMode::TimeProportional);
graph->SetTimeRowSpacingRange(16.0, 90.0);   // Floor and ceiling per gap
graph->SetShowDateRuler(true);
```

Row position follows the commit timestamp instead of the row index. The floor
keeps a burst of same-second commits readable; the ceiling is the gap
compression, so a two-year quiet period costs one large gap rather than an
unusable amount of empty axis. The date ruler prints one label per calendar day
rather than one per commit.

### Collapsing linear runs

```cpp
graph->SetCollapseLinearRuns(true, /*minimumRun=*/8);
graph->ExpandRun(sha);        // Unfold one placeholder (also on double-click)
graph->CollapseAllRuns();     // Drop every manual expansion
const int folded = graph->GetCollapsedCount(sha);
```

A run of plain single-parent, single-child commits folds into one dashed pill
labelled with how many commits it stands for. Commits carrying a ref, merges,
roots, cherry-picks and anything in `keepExpanded` are never folded, and the
edge across the fold is drawn dashed like any other skipped link.

### Parallel rows

```cpp
graph->SetParallelCommits(true, /*toleranceSeconds=*/0);
```

Commits made at the same moment share a row instead of stacking, matching
mermaid's `parallelCommits`. Two commits only share a row when neither is the
other's parent and they sit in different lanes, so no edge is ever flattened.

### Badges

```cpp
GitGraphCommit commit = *graph->GetCommit(sha);
commit.signature   = GitGraphSignature::Good;      // Good / Bad / Unknown
commit.buildStatus = GitGraphBuildStatus::Passed;  // Pending / Passed / Failed
graph->AddCommit(commit);
```

Badges are small dots queued beside the node, ahead of the ref chips.

> The enumerators are `NoSignature` / `NoStatus` and `Passed` / `Failed` rather
> than `None` and `Success` / `Failure`, because X11's `X.h` defines `None` and
> `Success` as macros and this header reaches the window backend.

### JSON export

```cpp
const std::string json = graph->ToJSON();
graph->SaveToJSON("history.json");
```

Emits the laid-out geometry — commits with their row/lane/flags, edges with
their endpoints and kind, refs, and swimlane bands — for tooling and golden
tests. There is no PNG export: the framework has no portable image writer to
build it on, so that stays out of this element.

## Using the layout core on its own

`UltraCanvasGitGraphLayout` has no UI dependencies, so it can be used headlessly
— for tests, for exporting geometry, or for a custom renderer:

```cpp
GitGraphLayoutOptions options;
options.orderMode    = GitGraphOrderMode::Topological;
options.laneStrategy = GitGraphLaneStrategy::Stable;
options.trunkBranch  = "main";

UltraCanvasGitGraphLayout engine(options);
GitGraphLayoutResult result = engine.Compute(commits, refs);

for (const GitGraphPlacedCommit& placed : result.commits) {
    // placed.row, placed.lane, placed.isMerge, placed.isRoot, placed.branch
}
for (const GitGraphPlacedEdge& edge : result.edges) {
    // edge.fromRow/fromLane -> edge.toRow/toLane, edge.kind, edge.turnAtChild
}
```

`GitGraphPlacedEdge::turnAtChild` tells the renderer where the lane change
happens: a first-parent edge runs down the child's lane and turns in at the
parent (a branch growing out of its base), while a merge edge leaves the merge
commit sideways and then runs down the parent's lane. That single flag is what
makes branch points and merges read correctly.

## Keyboard Navigation

| Key | Action |
|---|---|
| Up / Down | Previous / next commit along the history axis |
| Left / Right | Nearest commit in the lane to the left / right |
| Home / End | Newest / oldest commit |
| PageUp / PageDown | Jump ten rows |
| Enter | Fires `onCommitDoubleClick` for the selected commit |

Mouse: left-click selects (ctrl or shift extends), left-drag on empty space
pans, middle-drag always pans, right-click on a commit fires
`onCommitRightClick`, and the wheel zooms around the cursor.

## Performance Notes

- Lane assignment is `O(n · openBranches)` and runs in a single sweep.
- Crossing reduction is `O(edges²)` per iteration, so it is budgeted and off by
  default.
- Lazy loading keeps only the commits that have been paged in; the layout is
  extended as chunks arrive.
- Only rows inside the viewport are rendered, plus two rows of overscan, so
  scrolling cost is independent of history size.
- The layout is cached and recomputed only when data, ordering, layout mode or
  lane strategy change.

## See Also

- [`UltraCanvasGitGraphProposal.md`](UltraCanvasGitGraphProposal.md) — the
  research write-up, the full feature list and the delivery roadmap
- [`UltraCanvasNodeDiagramExamples.md`](UltraCanvasNodeDiagramExamples.md) —
  general node/link diagrams
- [`UltraCanvasGourceTreeExamples.md`](UltraCanvasGourceTreeExamples.md) —
  radial tree visualization
